/*
 * Wrecked Metal weather (game side): random rain / thunderstorm / snow per race track.
 *
 * The N64 game has one weather system (Weather_Init / Weather_Draw / Weather_Free,
 * src/auto/1A390.c 1A78C.c 1AD74.c): 90 particles in a 1400-unit box that is drawn
 * as 2x2 tiles in front of the camera, at 1/120 scale. It knows two kinds, rain
 * (type 0, a thin streak, texture D_8008F620) and snow (type 1, a square flake,
 * texture D_8008F720), but the retail game only ever turns on the snow, on track 10.
 * Lightning is a separate per-track effect (Weather_UpdateLightning, src/auto/13148.c):
 * it flashes the track's sky/fog colour command and plays the track's thunder.
 *
 * Here the same textures and particle code are reused, with up to three layers of
 * particles at different scales and distances from the camera (far, mid, near) so
 * the weather has depth: near flakes are big and cross the screen fast, far ones
 * are small and slow. Each layer is world-locked (it moves against the camera by
 * its own scale), and snowflakes sway on a smooth per-flake sine instead of the
 * N64's random jitter. What weather a race gets is decided by WM_WeatherPick
 * (port/src/wm_tuning.c, table in Data/wm_tuning.ini [weather]).
 *
 * Play Fidelity never comes here: WM_WeatherPick returns WM_WEATHER_N64 and the
 * original code runs untouched.
 */
#include <math.h>
#include <stdint.h>
char *getenv(const char *);
int atoi(const char *);

#include "common.h"
#include "wm_mod.h"

typedef struct { u32 w0; u32 w1; } WGfx;
typedef struct { f32 x, y, z; } WVec3;
typedef struct { s32 x0; s32 x4; } WHeapBlock;
typedef struct { s32 m[4][4]; } WMtx;

typedef struct WPart {
    struct WPart *next;
    WVec3 pos;
    WVec3 vel;
} WPart;

/* The whole particle (src/auto/11728.c), for the flash */
typedef struct {
    struct WPart *next;
    WVec3 pos;
    WVec3 vel;
    WVec3 acc;
    f32 scale;
    f32 scaleRate;
    u8 life;
    u8 alphaStep;
    u8 maxAlpha;
    u8 frame0;
    u8 frame1;
    u8 r, g, b;
} WPartFull;

/* Same layout as the game's particle system (func_80010950 fills 0x00-0x2B;
   Particles_Draw reads type 0x10 and the texture at 0x14-0x27) */
typedef struct {
    WPart *head;
    s32 count;
    void *x8;
    s32 xC;
    s32 type;
    void *img;
    s32 fmt;
    s32 siz;
    s32 width;
    s32 height;
    s32 x28;
} WSys;

typedef struct {
    void *free;
    u32 itemSize;
    void *base;
    u32 size;
    u32 sizeFree;
} WPool;

typedef struct {
    WMtx *mtx;
    void *x4;
    f32 zNear;
    f32 zFar;
    f32 fovy; /* degrees */
    f32 aspect;
    u8 pad18[0x14];
    WVec3 eye; /* 0x2C */
    u8 pad38[0x10];
    WVec3 at;  /* 0x48 */
} WCam;

typedef struct { u8 pad[0x1074]; u8 paused; u8 pad1075[0x10FC - 0x1075]; s16 sub; } WGame;

/* One layer of weather particles */
typedef struct {
    f32 scale;   /* particle units -> world units */
    f32 ahead;   /* distance of the layer's centre in front of the camera (world) */
    f32 size;    /* billboard size (particle units) */
    f32 fall;    /* fall speed (particle units per frame) */
    f32 fallVar; /* + up to this much more */
    f32 sway;    /* sideways sway amplitude (particle units per frame, snow) */
    s32 count;
    s32 alpha;
} WLayerDef;

typedef struct {
    WSys sys;
    WGfx *bufs[2];
    WHeapBlock blk[2];
    WLayerDef def;
    f32 windX, windZ;
    s32 used;
} WLayer;

#define LAYERS 3
#define BOX 700.0f /* half the particle box, as the N64's (D_80124588 / D_801242AC) */

/* Snow: thick, slow, swaying flakes; three depths */
static const WLayerDef kSnow[LAYERS] = {
    /* far  */ { 1.0f / 60.0f, 20.0f, 10.0f, 3.0f, 3.0f, 2.5f, 110, 120 },
    /* mid  */ { 1.0f / 120.0f, 9.0f, 22.0f, 5.5f, 5.0f, 4.0f, 96, 180 },
    /* near */ { 1.0f / 240.0f, 3.5f, 36.0f, 10.0f, 9.0f, 7.0f, 30, 210 },
};
/* Rain: the N64's streaks (fall -70..-78), plus a fast near layer */
static const WLayerDef kRain[LAYERS] = {
    /* far  */ { 1.0f / 60.0f, 26.0f, 8.0f, 36.0f, 4.0f, 0.0f, 80, 90 },
    /* mid  */ { 1.0f / 120.0f, 15.0f, 14.0f, 70.0f, 8.0f, 0.0f, 100, 150 },
    /* near */ { 1.0f / 240.0f, 6.0f, 22.0f, 140.0f, 16.0f, 0.0f, 40, 130 },
};

static WLayer sLayers[LAYERS];
static s32 sWeather = WM_WEATHER_N64;
static s32 sActive;
static s32 sFrame;
static u32 sTick;
static WVec3 sPrevAt;
static s32 sHavePrev;

/* Storm: a full-view flash (one big particle just past the near plane) and a
   synthesized thunder (port/src/wm_thunder.c) a moment later, nearer = sooner */
static const u8 kFlashDouble[] = { 150, 230, 120, 40, 0, 0, 190, 255, 200, 140, 90, 55, 30, 15, 6 };
static const u8 kFlashSingle[] = { 220, 255, 170, 110, 70, 40, 20, 8 };
static WSys sFlashSys;
static WGfx *sFlashBufs[2];
static WHeapBlock sFlashBlk[2], sFlashTexBlk;
static WPartFull *sFlashP;
static s32 sFlashUsed;
static s32 sNextStrike, sFlashFrame = -1, sThunderIn = -1;
static const u8 *sFlashPat;
static s32 sFlashLen;
static f32 sThunderDist;

extern WGame *gGame;
extern s32 D_800958A8;
extern s32 D_800958B4;
extern u8 D_80131F08;
extern u8 gMainHeap[];
extern u8 D_8008F620[], D_8008F720[];
extern WPool gParticlePool;

void Heap_Alloc(void *, WHeapBlock *, s32, s32, s32);
void *HeapBlock_Ptr(WHeapBlock *);
void Heap_Free(void *, void *);
void func_80010950(void *, s32, void *, s32, s32, s32, s32, s32);
void ParticleList_Clear(void *);
void Particles_Update(void *);
WGfx *Particles_Draw(void *, f32 (*)[4], WGfx *);
void *Particle_Spawn(void *, WVec3, WVec3, WVec3, f32, f32, u16, u8, u8, u8, u8, u8);
void *Pool_Alloc(WPool *);
void Gfx_FreeAfterFrame(void *);
void guMtxL2F(f32 mf[4][4], WMtx *m);
void guPosition(WMtx *, f32, f32, f32, f32, f32, f32, f32);
s32 rand(void);
void Port_Log(const char *fmt, ...);

#define GFX2(g, a, b) do { WGfx *_g = (g)++; _g->w0 = (u32)(a); _g->w1 = (u32)(b); } while (0)

static f32 frand(void) {
    return rand() * (1.0f / 32767.0f);
}

/* A stable per-particle phase from its pool address (as Particles_Draw does for spin) */
static f32 phaseOf(const WPart *p) {
    return (f32)(((u32)(uintptr_t)p >> 6) & 0xFF) * (6.2831853f / 256.0f);
}

/* A soft round 32x32 flake (RGBA32) instead of the N64's 8x8 cross: it lives in
   the game's heap, as Fast3D reads textures from game RAM (seg_addr) */
#define FLAKE 32
static WHeapBlock sFlakeBlk;
static u32 *sFlake;

static u32 *flakeTexture(void) {
    s32 x, y;

    if (sFlake != NULL) {
        return sFlake;
    }
    Heap_Alloc(gMainHeap, &sFlakeBlk, FLAKE * FLAKE * 4, 7, 0);
    sFlake = HeapBlock_Ptr(&sFlakeBlk);
    if (sFlake == NULL) {
        return NULL;
    }
    for (y = 0; y < FLAKE; y++) {
        for (x = 0; x < FLAKE; x++) {
            f32 dx = (x + 0.5f - FLAKE * 0.5f) / (FLAKE * 0.5f);
            f32 dy = (y + 0.5f - FLAKE * 0.5f) / (FLAKE * 0.5f);
            f32 r2 = dx * dx + dy * dy;
            f32 a = 1.0f - r2;
            u32 alpha;

            /* bright core, soft edge */
            a = a <= 0.0f ? 0.0f : a * a * (1.4f - 0.4f * a);
            if (a > 1.0f) {
                a = 1.0f;
            }
            alpha = (u32)(a * 255.0f);
            /* written as u32 so Fast3D reads it in host order (port/src/shadow.c) */
            sFlake[y * FLAKE + x] = 0xFFFFFF00u | alpha;
        }
    }
    return sFlake;
}

static void flakeFree(void) {
    if (sFlake != NULL) {
        Heap_Free(gMainHeap, &sFlakeBlk);
        sFlake = NULL;
    }
}

static void layerFree(WLayer *L) {
    s32 i;

    if (!L->used) {
        return;
    }
    for (i = 0; i < 2; i++) {
        Heap_Free(gMainHeap, &L->blk[i]);
    }
    ParticleList_Clear(&L->sys);
    L->used = 0;
}

static void layerInit(WLayer *L, const WLayerDef *def, s32 snow, f32 windX, f32 windZ) {
    WVec3 zero = { 0.0f, 0.0f, 0.0f };
    WVec3 pos, vel;
    s32 i;

    L->def = *def;
    L->windX = windX;
    L->windZ = windZ;
    for (i = 0; i < 2; i++) {
        Heap_Alloc(gMainHeap, &L->blk[i], (def->count + 40) * 16, 7, 0);
        L->bufs[i] = HeapBlock_Ptr(&L->blk[i]);
    }
    L->used = 1;
    if (snow) {
        u32 *tex = flakeTexture();

        if (tex != NULL) {
            func_80010950(&L->sys, 1, tex, 0, 3, FLAKE, FLAKE, 1);
        } else {
            func_80010950(&L->sys, 1, D_8008F720, 0, 3, 8, 8, 1);
        }
    } else {
        func_80010950(&L->sys, 2, D_8008F620, 0, 3, 8, 8, 1);
    }
    for (i = 0; i < def->count; i++) {
        pos.x = -BOX + frand() * 2.0f * BOX;
        pos.y = -BOX + frand() * 2.0f * BOX;
        pos.z = -BOX + frand() * 2.0f * BOX;
        vel.x = windX;
        vel.y = -(def->fall + frand() * def->fallVar);
        vel.z = windZ;
        /* life 0xFF = never dies; size jitters a little so the flakes are not all alike */
        if (Particle_Spawn(&L->sys, pos, vel, zero, def->size * (0.75f + frand() * 0.5f), 0.0f, 0xFF, 1,
                           (u8)def->alpha, 0, 0, 1) == NULL) {
            break;
        }
    }
}

static void flashInit(void) {
    WVec3 zero = { 0.0f, 0.0f, 0.0f };
    u32 *tex;
    s32 i;

    for (i = 0; i < 2; i++) {
        Heap_Alloc(gMainHeap, &sFlashBlk[i], 64 * 8, 7, 0);
        sFlashBufs[i] = HeapBlock_Ptr(&sFlashBlk[i]);
    }
    Heap_Alloc(gMainHeap, &sFlashTexBlk, 8 * 8 * 4, 7, 0);
    tex = HeapBlock_Ptr(&sFlashTexBlk);
    for (i = 0; i < 64; i++) {
        tex[i] = 0xFFFFFFFFu; /* plain white; the particle's colour tints it */
    }
    sFlashUsed = 1;
    func_80010950(&sFlashSys, 1, tex, 0, 3, 8, 8, 1);
    sFlashP = Particle_Spawn(&sFlashSys, zero, zero, zero, 1.0f, 0.0f, 0xFF, 1, 0, 0, 0, 1);
    if (sFlashP != NULL) {
        sFlashP->r = 0xE4;
        sFlashP->g = 0xEC;
        sFlashP->b = 0xFF;
    }
    sNextStrike = 150 + (s32)(frand() * 300.0f);
    if (getenv("DD64_STRIKE") != NULL) {
        sNextStrike = atoi(getenv("DD64_STRIKE"));
    }
    sFlashFrame = -1;
    sThunderIn = -1;
    WM_ThunderOpen();
}

static void flashFree(void) {
    s32 i;

    if (!sFlashUsed) {
        return;
    }
    for (i = 0; i < 2; i++) {
        Heap_Free(gMainHeap, &sFlashBlk[i]);
    }
    Heap_Free(gMainHeap, &sFlashTexBlk);
    ParticleList_Clear(&sFlashSys);
    sFlashP = NULL;
    sFlashUsed = 0;
    WM_ThunderClose();
}

/* Advance the storm by one frame: a strike every 4-14 s */
static void stormUpdate(void) {
    if (sThunderIn >= 0 && sThunderIn-- == 0) {
        WM_ThunderPlay(sThunderDist);
    }
    if (sFlashFrame >= 0 && ++sFlashFrame >= sFlashLen) {
        sFlashFrame = -1;
    }
    if (--sNextStrike > 0) {
        return;
    }
    sNextStrike = 240 + (s32)(frand() * 600.0f);
    if (getenv("DD64_STRIKE") != NULL) {
        sNextStrike = atoi(getenv("DD64_STRIKE")); /* tests: strike this often (frames) */
    }
    if (frand() < 0.5f) {
        sFlashPat = kFlashDouble;
        sFlashLen = sizeof(kFlashDouble);
    } else {
        sFlashPat = kFlashSingle;
        sFlashLen = sizeof(kFlashSingle);
    }
    sThunderDist = frand();
    sFlashFrame = 0;
    sThunderIn = 8 + (s32)(sThunderDist * 100.0f); /* light first, sound ~0.1-1.8 s later */
}

static WGfx *flashDraw(WCam *cam, f32 m[4][4], const WVec3 *fwd, WGfx *gfx, s32 frame) {
    f32 d, half, alpha;
    WMtx *mtx;
    WGfx *g;

    if (sFlashP == NULL || sFlashFrame < 0) {
        return gfx;
    }
    /* far strikes are dimmer */
    alpha = sFlashPat[sFlashFrame] * (0.75f - sThunderDist * 0.35f);
    if (alpha < 1.0f) {
        return gfx;
    }
    sFlashP->maxAlpha = (u8)alpha;
    d = cam->zNear * 1.5f;
    half = d * tanf(cam->fovy * (3.14159265f / 360.0f)) * (cam->aspect > 1.0f ? cam->aspect : 1.0f) * 1.6f;
    sFlashP->scale = half * 100.0f; /* the vertices are s16: draw at 1/100 */
    g = Particles_Draw(&sFlashSys, m, sFlashBufs[frame]);
    g->w0 = 0xDF000000;
    g->w1 = 0;
    mtx = Pool_Alloc(&gParticlePool);
    if (mtx == NULL) {
        return gfx;
    }
    guPosition(mtx, 0.0f, 0.0f, 0.0f, 0.01f, cam->eye.x + fwd->x * d, cam->eye.y + fwd->y * d,
               cam->eye.z + fwd->z * d);
    Gfx_FreeAfterFrame(mtx);
    GFX2(gfx, 0xDA380000, PHYS_ADDR(mtx));
    GFX2(gfx, 0xDE000000, sFlashBufs[frame]);
    GFX2(gfx, 0xD8380002, 0x40);
    return gfx;
}

int WM_WeatherInit(void) {
    const WLayerDef *defs;
    f32 windX = 0.0f, windZ = 0.0f;
    s32 i, snow, layers;

    sWeather = WM_WeatherPick(D_800958B4, D_800958A8);
    if (sWeather == WM_WEATHER_N64) {
        return 0; /* Play Fidelity: the game's own Weather_Init */
    }
    WM_WeatherFree();
    sActive = 0;
    sHavePrev = 0;
    sTick = 0;
    if (sWeather == WM_WEATHER_CLEAR) {
        Port_Log("weather: track %d gametype %d -> clear", (int)D_800958B4, (int)D_800958A8);
        return 1;
    }
    snow = sWeather == WM_WEATHER_SNOW;
    defs = snow ? kSnow : kRain;
    if (!snow) {
        /* one wind for the race, as the N64 rain does (x = z, -3..3 per frame x10) */
        windX = windZ = (frand() * 6.0f - 3.0f) * 10.0f * (sWeather == WM_WEATHER_STORM ? 1.6f : 1.0f);
    }
    layers = WM_GetTuning()->weatherLayers;
    for (i = 0; i < LAYERS; i++) {
        WLayerDef def = defs[i];

        /* 1 layer = the mid one only (the N64's look); 2 = mid + near; 3 = all */
        if ((layers <= 1 && i != 1) || (layers == 2 && i == 0)) {
            continue;
        }
        def.count = (s32)(def.count * WM_GetTuning()->weatherDensity);
        if (sWeather == WM_WEATHER_STORM) {
            def.count = def.count * 3 / 2;
        }
        if (def.count < 4) {
            continue;
        }
        /* the wind is the same in the world for every layer */
        layerInit(&sLayers[i], &def, snow, windX / (def.scale * 120.0f), windZ / (def.scale * 120.0f));
    }
    if (sWeather == WM_WEATHER_STORM) {
        flashInit();
    }
    D_80131F08 = 0;
    sActive = 1;
    Port_Log("weather: track %d gametype %d -> %s (pool %u/%u free)", (int)D_800958B4, (int)D_800958A8,
             WM_WeatherName(sWeather), (unsigned)gParticlePool.sizeFree, (unsigned)gParticlePool.size);
    return 1;
}

void WM_WeatherFree(void) {
    s32 i;

    for (i = 0; i < LAYERS; i++) {
        layerFree(&sLayers[i]);
    }
    flakeFree();
    flashFree();
    sActive = 0;
}

int WM_WeatherActive(void) {
    return sWeather != WM_WEATHER_N64;
}

int WM_WeatherStorm(void) {
    return sActive && sWeather == WM_WEATHER_STORM;
}

/* Wrap a coordinate back into the box (the N64 moves it by one box size) */
static f32 wrap(f32 v) {
    while (v < -BOX) {
        v += 2.0f * BOX;
    }
    while (v > BOX) {
        v -= 2.0f * BOX;
    }
    return v;
}

static void layerUpdate(WLayer *L, const WVec3 *camMove, s32 snow) {
    WPart *p;
    f32 k = 1.0f / L->def.scale; /* world -> particle units: world-locked */
    f32 t = (f32)sTick;

    for (p = L->sys.head; p != NULL; p = p->next) {
        p->pos.x -= camMove->x * k;
        p->pos.y -= camMove->y * k;
        p->pos.z -= camMove->z * k;
        if (p->pos.y < -BOX) {
            /* fell out of the bottom: back to the top, somewhere new */
            p->pos.y += 2.0f * BOX;
            p->pos.x = -BOX + frand() * 2.0f * BOX;
            p->pos.z = -BOX + frand() * 2.0f * BOX;
        } else {
            p->pos.y = wrap(p->pos.y);
            p->pos.x = wrap(p->pos.x);
            p->pos.z = wrap(p->pos.z);
        }
        if (snow) {
            f32 ph = phaseOf(p);

            /* smooth flutter: two slow sines per flake, out of step with each other */
            p->vel.x = L->def.sway * sinf(ph + t * (0.035f + ph * 0.004f));
            p->vel.z = L->def.sway * 0.8f * cosf(ph * 1.7f + t * (0.027f + ph * 0.003f));
        } else {
            p->vel.x = L->windX;
            p->vel.z = L->windZ;
        }
    }
    Particles_Update(&L->sys);
}

static WGfx *layerDraw(WLayer *L, WCam *cam, f32 m[4][4], const WVec3 *fwd, WGfx *gfx, s32 frame, s32 snow) {
    static const f32 offs[4][2] = { { -1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f } };
    f32 half = BOX * L->def.scale;
    WVec3 c;
    WGfx *g;
    WMtx *mtx;
    s32 i;

    g = Particles_Draw(&L->sys, m, L->bufs[frame]);
    g->w0 = 0xDF000000;
    g->w1 = 0;
    c.x = cam->eye.x + fwd->x * L->def.ahead;
    /* lifted a little, rain twice as much (the N64's yoff) */
    c.y = cam->eye.y + fwd->y * L->def.ahead + half * (snow ? 0.143f : 0.286f);
    c.z = cam->eye.z + fwd->z * L->def.ahead;
    for (i = 0; i < 4; i++) {
        mtx = Pool_Alloc(&gParticlePool);
        if (mtx == NULL) {
            break;
        }
        guPosition(mtx, 0.0f, 0.0f, 0.0f, L->def.scale, c.x + offs[i][0] * half, c.y,
                   c.z + offs[i][1] * half);
        Gfx_FreeAfterFrame(mtx);
        GFX2(gfx, 0xDA380000, PHYS_ADDR(mtx));
        GFX2(gfx, 0xDE000000, L->bufs[frame]);
        GFX2(gfx, 0xD8380002, 0x40);
    }
    return gfx;
}

void *WM_WeatherDraw(void *camPtr, void *gfxPtr) {
    WCam *cam = (WCam *)camPtr;
    WGfx *gfx = (WGfx *)gfxPtr;
    WVec3 move = { 0.0f, 0.0f, 0.0f };
    WVec3 fwd;
    f32 m[4][4];
    f32 len;
    s32 i, snow, paused;

    if (!sActive) {
        return gfx;
    }
    if (gGame->sub == 0x1C && D_80131F08) {
        return gfx;
    }
    snow = sWeather == WM_WEATHER_SNOW;
    paused = gGame->paused != 0;
    sFrame ^= 1;
    if (!paused) {
        sTick++;
        if (sHavePrev) {
            move.x = cam->at.x - sPrevAt.x;
            move.y = cam->at.y - sPrevAt.y;
            move.z = cam->at.z - sPrevAt.z;
            /* a camera cut (replay, respawn): don't sweep the particles across */
            if (move.x * move.x + move.y * move.y + move.z * move.z > 400.0f * 400.0f) {
                move.x = move.y = move.z = 0.0f;
            }
        }
        sPrevAt = cam->at;
        sHavePrev = 1;
        for (i = 0; i < LAYERS; i++) {
            if (sLayers[i].used) {
                layerUpdate(&sLayers[i], &move, snow);
            }
        }
        if (sFlashUsed) {
            stormUpdate();
        }
    }
    fwd.x = cam->at.x - cam->eye.x;
    fwd.y = cam->at.y - cam->eye.y;
    fwd.z = cam->at.z - cam->eye.z;
    len = sqrtf(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (len > 0.0001f) {
        fwd.x /= len;
        fwd.y /= len;
        fwd.z /= len;
    }
    guMtxL2F(m, cam->mtx);
    /* far to near, so the near flakes blend over the far ones */
    for (i = 0; i < LAYERS; i++) {
        if (sLayers[i].used) {
            gfx = layerDraw(&sLayers[i], cam, m, &fwd, gfx, sFrame, snow);
        }
    }
    gfx = flashDraw(cam, m, &fwd, gfx, sFrame);
    return gfx;
}

/* Render_DrawSkyAndWorld asks whether to run the track's lightning: `n64` is the
   track's own flag (no retail track sets it, and the sky block that
   Weather_UpdateLightning flashes is disabled on every race track). Wrecked Metal's
   storm draws its own lightning (flashDraw), so that code stays off. */
s32 Port_TrackLightning(s32 n64) {
    return sWeather == WM_WEATHER_N64 ? n64 : 0;
}
