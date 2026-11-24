/*
 * Game-side Wrecked Metal mod hooks (built in the `game` target with
 * -fsanitize=kernel-address so N64 RAM structs are accessed seamlessly).
 * Every feature here stays inactive when WM_Enabled() is 0 (Play Fidelity).
 */
#include "common.h"
#include "wm_mod.h"
#include <stdint.h>

char *getenv(const char *);
int atoi(const char *);

typedef struct { u32 w0; u32 w1; } WMGfx;
typedef struct { f32 x, y, z; } Vec3f;
typedef struct { f32 m[4][4]; } Mtx;
typedef struct { u8 pad[0x10]; f32 x10; } WMScaleInfo;
typedef struct { u8 pad[0xC]; WMScaleInfo *xC; } WMTrackInfo;
typedef struct { u8 pad[0x70]; WMTrackInfo *x70; } WMRace;
typedef struct {
    u8 pad0[0xC];
    s16 mode;
    u8 padE[0x10FC - 0xE];
    s16 x10FC;
    u8 pad10FE[2];
    WMRace *x1100;
} WMGame;

typedef struct { u8 x0; u8 x1; u8 x2; u8 x3; s16 x4; } WMCarInput;

typedef struct {
    u8 pad0[0x194];
    Vec3f groundNormal; /* groundNormal.z >= 1.0f while wheel is airborne */
    f32 staticGrip;
    f32 slideGrip;
    s32 surfaceType;    /* 0..10 (offset 0x1A8) */
    u8 isSkidding;      /* offset 0x1AC */
    u8 pad1AD[3];
    f32 skidIntensity;  /* offset 0x1B0 */
    s32 skidMode;       /* offset 0x1B4: 0=spin/brake, 1=handbrake, 2=drift */
    f32 rearGrip;
} WMWheel; /* size 0x1BC */

typedef struct {
    u8 pad0[0xD];
    u8 wrecked;
    u8 padE[2];
    s32 health;         /* offset 0x10 */
    u8 pad14[0x2C - 0x14];
    WMCarInput *carInput;
    s32 isAi;
    u8 pad34[0x3C - 0x34];
    void *playerInfo;
    u8 pad40[0x364 - 0x40];
    WMWheel wheels[4];
    u8 padA54[0xA60 - 0xA54];
    f32 allWheelsAirTime;
    u8 padA64[0xB70 - 0xA64];
    void *skidMgr[4];
} WMCar;

typedef struct {
    u8 active;          /* 0x00 */
    u8 surfaceType;     /* 0x01 (unused padding byte repurposed for terrain tint) */
    u8 pad2[2];
    Vec3f p[3];         /* 0x04..0x27 */
    s32 life;           /* 0x28 */
} WMSkidTri; /* size 0x2C */

typedef struct {
    u8 active;          /* 0x00 */
    u8 surfaceType;     /* 0x01 (unused padding byte) */
    u8 pad2[0x224 - 0x02];
    WMSkidTri *tris;    /* 0x224 (40 entries) */
} WMSkidMgr;

typedef struct {
    void *x0;
    u8 pad4[0x64];
} WMPlayerSlot; /* size 0x68 */

typedef struct WMParticle {
    struct WMParticle *next;
    Vec3f pos;
    Vec3f vel;
    Vec3f acc;
    f32 scale;
    f32 scaleRate;
    u8 life;
    u8 alphaStep;
    u8 maxAlpha;
    u8 firstFrame;
    u8 lastFrame;
    u8 r;
    u8 g;
    u8 b;
    u8 pad38;
    u8 initLife;
    u8 pad3A;
    s32 dl;
} WMParticle;

typedef struct {
    void *head;
    s32 count;
    u8 pad8[0x94 - 0x8];
    s32 maxCount;
} WMParticleObj;

typedef struct {
    u8 frame;
    u8 r, g, b;
    f32 scale;
    f32 growth;
    int life;
    int maxAlpha;
    f32 lift;
    f32 accY;
} SurfaceDustStyle;

extern u8 D_8012455C;
extern s32 D_800958A8;
extern s8 D_800D31CC;
extern WMGame *gGame;
extern u8 *D_801245A4;
extern WMPlayerSlot D_800958F0[];

s32 Port_GameDrawsWorld(void);
void Port_OptMenuPad(u16 *buttons, s8 *x, s8 *y);
f32 Port_InputGas(s32 channel);
f32 Port_InputBrake(s32 channel);

void Vec3f_Set(Vec3f *, f32, f32, f32);
void Vec3f_Sub(Vec3f *, Vec3f *, Vec3f *);
void Vec3f_Add(Vec3f *, Vec3f *, Vec3f *);
void Vec3f_Length(f32 *, Vec3f *);
void Vec3f_SetLength(Vec3f *, Vec3f *, f32);
void Mtx_FromRotPos(Mtx *, void *, void *, s32, s32);
f32 Car_GetSpeed(void *);
void *Particle_Spawn(void *, Vec3f, Vec3f, Vec3f, f32, f32, u16, u8, s32, s32, s32, s32);
void Skid_Start(void *);
void Skid_Stop(void *);
s32 rand(void);

#define WM_MAX_TRACKED_CARS 32
static void *sSkidCars[WM_MAX_TRACKED_CARS];
static u8 sSkidOnHold[WM_MAX_TRACKED_CARS][4];
static u8 sSkidOffHold[WM_MAX_TRACKED_CARS][4];
static s32 sSkidNextSlot;

static int skidCarSlot(void *car) {
    int i, slot;

    for (i = 0; i < WM_MAX_TRACKED_CARS; i++) {
        if (sSkidCars[i] == car) {
            return i;
        }
    }
    slot = sSkidNextSlot;
    sSkidNextSlot = (sSkidNextSlot + 1) % WM_MAX_TRACKED_CARS;
    sSkidCars[slot] = car;
    for (i = 0; i < 4; i++) {
        sSkidOnHold[slot][i] = 0;
        sSkidOffHold[slot][i] = 0;
    }
    return slot;
}

static f32 randRange(f32 span) {
    return ((f32)(rand() % 1001) * 0.001f - 0.5f) * 2.0f * span;
}

static void styleForSurface(s32 surface, s32 skidMode, const WMTuning *cfg, SurfaceDustStyle *out) {
    switch (surface) {
    case 2: /* ice / slippery snow */
    case 9: /* snowy asphalt */
        out->frame = 0;
        out->r = 218;
        out->g = 242;
        out->b = 255;
        out->scale = cfg->snowScale;
        out->growth = cfg->snowGrowth;
        out->life = cfg->snowLife;
        out->maxAlpha = cfg->snowAlpha;
        out->lift = cfg->snowLift;
        out->accY = -0.003f;
        break;
    case 3: /* dry dirt */
        out->frame = 3;
        out->r = 232;
        out->g = 188;
        out->b = 132;
        out->scale = cfg->dirtScale;
        out->growth = cfg->dirtGrowth;
        out->life = cfg->dirtLife;
        out->maxAlpha = cfg->dirtAlpha;
        out->lift = cfg->dirtLift;
        out->accY = -0.004f;
        break;
    case 4: /* wet dirt / mud / grass */
        out->frame = 3;
        out->r = 168;
        out->g = 132;
        out->b = 88;
        out->scale = cfg->dirtScale * 1.05f;
        out->growth = cfg->dirtGrowth * 0.95f;
        out->life = cfg->dirtLife;
        out->maxAlpha = cfg->dirtAlpha;
        out->lift = cfg->dirtLift * 0.90f;
        out->accY = -0.006f;
        break;
    case 7: /* loose gravel */
        out->frame = 3;
        out->r = 216;
        out->g = 204;
        out->b = 180;
        out->scale = cfg->dirtScale * 0.95f;
        out->growth = cfg->dirtGrowth;
        out->life = cfg->dirtLife - 4;
        out->maxAlpha = cfg->dirtAlpha - 10;
        out->lift = cfg->dirtLift;
        out->accY = -0.005f;
        break;
    case 8: /* packed gravel */
        out->frame = 0;
        out->r = 224;
        out->g = 216;
        out->b = 198;
        out->scale = cfg->rubberFogScale * 0.90f;
        out->growth = cfg->rubberFogGrowth * 0.85f;
        out->life = cfg->rubberFogLife - 6;
        out->maxAlpha = cfg->rubberFogAlpha - 20;
        out->lift = cfg->rubberFogLift;
        out->accY = -0.002f;
        break;
    case 5: /* metal */
        out->frame = 0;
        out->r = 212;
        out->g = 222;
        out->b = 236;
        out->scale = cfg->rubberFogScale * 0.95f;
        out->growth = cfg->rubberFogGrowth;
        out->life = cfg->rubberFogLife;
        out->maxAlpha = cfg->rubberFogAlpha;
        out->lift = cfg->rubberFogLift;
        out->accY = -0.002f;
        break;
    case 6: /* wood */
        out->frame = 0;
        out->r = 228;
        out->g = 200;
        out->b = 158;
        out->scale = cfg->rubberFogScale * 0.90f;
        out->growth = cfg->rubberFogGrowth * 0.90f;
        out->life = cfg->rubberFogLife - 6;
        out->maxAlpha = cfg->rubberFogAlpha - 15;
        out->lift = cfg->rubberFogLift;
        out->accY = -0.003f;
        break;
    case 0: /* default road */
    case 1: /* asphalt */
    case 10: /* curb / rough asphalt */
    default:
        out->frame = 0;
        if (skidMode != 2) {
            /* Burnout / handbrake rubber smoke is slightly denser and brighter */
            out->r = 242;
            out->g = 245;
            out->b = 255;
        } else {
            out->r = 226;
            out->g = 232;
            out->b = 244;
        }
        out->scale = cfg->rubberFogScale;
        out->growth = cfg->rubberFogGrowth;
        out->life = cfg->rubberFogLife;
        out->maxAlpha = cfg->rubberFogAlpha;
        out->lift = cfg->rubberFogLift;
        out->accY = -0.002f;
        break;
    }
}

/* Particles of the smoke pool the wheel dust leaves free for the hood smoke */
#define WM_HOOD_RESERVE 40

static void emitWheelSegment(WMParticleObj *obj, const Vec3f *from, const Vec3f *to,
                             const SurfaceDustStyle *st, f32 intensity, f32 stepDist,
                             f32 scaleMul, f32 lifeMul, f32 alphaMul, int maxSteps) {
    Vec3f diff, step, p, vel, acc;
    f32 len = 0.0f;
    f32 scale, growth;
    int life, alphaStep, maxAlpha, steps, emitted = 0;

    if (obj->count >= obj->maxCount - WM_HOOD_RESERVE) {
        return;
    }

    Vec3f_Sub(&diff, (Vec3f *)to, (Vec3f *)from);
    Vec3f_Length(&len, &diff);
    if (len > 0.001f) {
        Vec3f_SetLength(&step, &diff, stepDist);
    } else {
        Vec3f_Set(&step, 0.0f, 0.0f, 0.0f);
    }

    scale = st->scale * (0.72f + 0.38f * intensity) * scaleMul;
    growth = st->growth * (0.75f + 0.35f * intensity) * scaleMul;
    life = (int)((f32)st->life * (0.65f + 0.40f * intensity) * lifeMul);
    if (life < 8) {
        life = 8;
    } else if (life > 250) {
        life = 250;
    }
    maxAlpha = (int)((f32)st->maxAlpha * (0.55f + 0.45f * intensity) * alphaMul);
    if (maxAlpha < 36) {
        maxAlpha = 36;
    } else if (maxAlpha > 255) {
        maxAlpha = 255;
    }
    alphaStep = maxAlpha / (life / 3 + 2);
    if (alphaStep < 8) {
        alphaStep = 8;
    }

    steps = (int)(len / stepDist) + 1;
    if (steps > maxSteps) {
        steps = maxSteps;
    }

    p = *from;
    for (emitted = 0; emitted < steps && obj->count < obj->maxCount - WM_HOOD_RESERVE; emitted++) {
        Vec3f spawnPos = p;
        WMParticle *pt;

        spawnPos.x += randRange(0.45f);
        spawnPos.z += randRange(0.45f);
        Vec3f_Set(&vel, randRange(0.18f), st->lift * (0.80f + 0.40f * intensity), randRange(0.18f));
        Vec3f_Set(&acc, 0.0f, st->accY, 0.0f);

        pt = (WMParticle *)Particle_Spawn(obj, spawnPos, vel, acc, scale, growth,
                                          (u16)life, (u8)alphaStep, maxAlpha, 0, st->frame, st->frame);
        if (pt != NULL) {
            pt->r = st->r;
            pt->g = st->g;
            pt->b = st->b;
        }
        Vec3f_Add(&p, &p, &step);
    }
}

int WM_EmitWheelDust(void *carPtr, void *a, void *b) {
    WMCar *car = (WMCar *)carPtr;
    WMTuning cfg;
    WMParticleObj *smokeObj;
    f32 trackScale;
    s32 i;
    int slot;

    if (!WM_Enabled()) {
        return 0;
    }
    if (D_8012455C || car->wrecked || D_801245A4 == NULL) {
        return 1;
    }

    cfg = *WM_GetTuning();
    slot = skidCarSlot(carPtr);

    if (car->allWheelsAirTime <= 0.0f) {
        if (!(D_800958A8 == 3 && car->isAi != 0)) {
            smokeObj = (WMParticleObj *)(D_801245A4 + 0xB0);
            trackScale = gGame->x1100->x70->xC->x10;

            for (i = 0; i < 4; i++) {
                WMWheel *w = &car->wheels[i];
                f32 intensity, scaleMul, lifeMul, alphaMul;
                int maxSteps;
                Mtx m;
                Vec3f p1, p2;
                SurfaceDustStyle st;

                if (!w->isSkidding || w->groundNormal.z >= 1.0f) {
                    continue;
                }
                if (car->isAi != 0) {
                    /* AI bots emit only from rear wheels at 80% scale for mobile 60fps headroom */
                    if (i < 2) {
                        continue;
                    }
                    if (w->skidMode == 2 && !cfg.emitOnDrift) {
                        continue;
                    }
                    scaleMul = cfg.aiSmokeScale;
                    lifeMul = 0.80f;
                    alphaMul = 0.80f;
                    maxSteps = 1;
                } else if (i < 2) {
                    /* Player front wheels: compact 30% size of rear wheels */
                    if (!cfg.emitFrontWheels) {
                        continue;
                    }
                    if (w->skidMode == 2 && w->skidIntensity < cfg.frontSlipThreshold) {
                        continue;
                    }
                    scaleMul = cfg.frontWheelScale;
                    lifeMul = 0.70f;
                    alphaMul = 0.65f;
                    maxSteps = 1;
                } else {
                    if (w->skidMode == 2 && !cfg.emitOnDrift) {
                        continue;
                    }
                    scaleMul = 1.0f;
                    lifeMul = 1.0f;
                    alphaMul = 1.0f;
                    maxSteps = 2;
                }

                Mtx_FromRotPos(&m, a, car, i, 1);
                p1.x = m.m[3][0] * trackScale;
                p1.y = m.m[3][1] * trackScale;
                p1.z = m.m[3][2] * trackScale;
                Mtx_FromRotPos(&m, b, car, i, 1);
                p2.x = m.m[3][0] * trackScale;
                p2.y = m.m[3][1] * trackScale;
                p2.z = m.m[3][2] * trackScale;

                intensity = w->skidIntensity;
                if (w->skidMode != 2 && intensity < 0.75f) {
                    intensity = 0.75f;
                } else if (intensity < 0.45f) {
                    intensity = 0.45f;
                }

                styleForSurface(w->surfaceType, w->skidMode, &cfg, &st);
                emitWheelSegment(smokeObj, &p1, &p2, &st, intensity, cfg.stepDist,
                                 scaleMul, lifeMul, alphaMul, maxSteps);
            }
        }

        /* Realistic lock/release skidmark timing with per-wheel null safety (AI = rear-only) */
        for (i = 0; i < 4; i++) {
            WMWheel *w = &car->wheels[i];
            WMSkidMgr *sk = (WMSkidMgr *)car->skidMgr[i];
            int wantsSkid = 0;

            if (sk == NULL) {
                continue;
            }
            if (w->isSkidding && w->groundNormal.z < 1.0f) {
                if (w->skidMode != 2) {
                    wantsSkid = 1;
                } else if ((i >= 2 && w->skidIntensity >= 0.36f) ||
                           (i < 2 && w->skidIntensity >= 0.72f)) {
                    wantsSkid = 1;
                }
            }

            if (wantsSkid) {
                u8 need = (w->skidMode == 1) ? 1 : (w->skidMode == 0 ? 2 : 4);
                if (sSkidOnHold[slot][i] < 250) {
                    sSkidOnHold[slot][i]++;
                }
                if (sSkidOnHold[slot][i] >= need) {
                    sSkidOffHold[slot][i] = (w->skidIntensity > 0.65f) ? 2 : 1;
                    sk->surfaceType = (u8)w->surfaceType;
                    Skid_Start(sk);
                }
            } else {
                sSkidOnHold[slot][i] = 0;
                if (w->groundNormal.z >= 1.0f || sSkidOffHold[slot][i] == 0) {
                    sSkidOffHold[slot][i] = 0;
                    Skid_Stop(sk);
                } else {
                    sSkidOffHold[slot][i]--;
                }
            }
        }
    } else {
        for (i = 0; i < 4; i++) {
            sSkidOnHold[slot][i] = 0;
            sSkidOffHold[slot][i] = 0;
            if (car->skidMgr[i] != NULL) {
                Skid_Stop(car->skidMgr[i]);
            }
        }
    }

    return 1;
}

/*
 * Hood smoke by damage (Car_UpdateWreck, src/auto/1C624.c, every car every frame below
 * WM_HoodSmokeStartHp). The N64 smokes only under 21 health, one particle a frame for
 * every car. Here, by health (cars start between 110 and 200):
 *   80..50   light: thin pale wisps
 *   50..21   medium: grey-white, steadier
 *   21..1    heavy: thick white, the N64's stage
 *   0        black, before the car burns out
 * The player's car smokes at the full rate; the other cars at half of it (every other
 * frame per stage), which halves their particles. The wheel dust keeps WM_HOOD_RESERVE
 * particles of the shared pool free for it, so a dusty race no longer starves it.
 */
static int wmIsHumanCar(void *carPtr);

#define WM_HOOD_LIGHT 80
#define WM_HOOD_MEDIUM 50
#define WM_HOOD_HEAVY 21

int WM_HoodSmokeStartHp(void) {
    return WM_Enabled() ? WM_HOOD_LIGHT : WM_HOOD_HEAVY;
}

int WM_EmitHoodSmoke(void *carPtr, void *posPtr, void *mtxPtr) {
    static u8 sTick[32];
    WMCar *car = (WMCar *)carPtr;
    const Vec3f *pos = (const Vec3f *)posPtr;
    f32 (*m)[4] = (f32 (*)[4])mtxPtr;
    WMParticleObj *smokeObj;
    WMParticle *pt;
    Vec3f p, v, acc;
    f32 spd, spdNorm, r1, r2, backPush, latVel, scale, spread, rise;
    s32 life, hp = car->health, every, maxAlpha, alphaStep;
    u8 *tick;
    int human;

    if (!WM_Enabled()) {
        return 0;
    }
    /* (not D_8012455C: this runs in the car update, where that is the last view drawn) */
    if (D_801245A4 == NULL || hp >= WM_HOOD_LIGHT) {
        return 1;
    }
    smokeObj = (WMParticleObj *)(D_801245A4 + 0xB0);
    if (smokeObj->count >= smokeObj->maxCount) {
        return 1;
    }

    human = wmIsHumanCar(carPtr);
    if (hp <= 0 || hp < WM_HOOD_HEAVY) {
        every = 1;
    } else if (hp < WM_HOOD_MEDIUM) {
        every = 3;
    } else {
        every = 6;
    }
    if (!human) {
        every *= 2;
    }
    tick = &sTick[((u32)(uintptr_t)carPtr >> 4) & 31];
    if (++*tick < every) {
        return 1;
    }
    *tick = 0;

    spd = Car_GetSpeed(carPtr);
    if (spd < 0.0f) {
        spd = 0.0f;
    }
    spdNorm = spd / 26.0f;
    if (spdNorm > 1.0f) {
        spdNorm = 1.0f;
    }
    r1 = (f32)((rand() & 7) - 3);
    life = 9 - D_800D31CC;
    if (life < 4) {
        life = 4;
    }

    if (hp > 0) {
        /* white to grey, thicker and longer as the health drops; flattened at speed and
           spread to the sides by the airflow */
        f32 d = 1.0f - (f32)hp / (f32)WM_HOOD_LIGHT; /* 0 light .. ~1 heavy */
        scale = 4.4f + 2.2f * d;
        spread = 6.5f + 4.0f * d;
        rise = 2.3f + 0.6f * d;
        maxAlpha = 80 + (s32)(105.0f * d);
        alphaStep = maxAlpha / 4;
        life += (s32)(2.0f * d);

        r2 = randRange(spread);
        p.x = pos->x + m[0][0] * (15.0f - r1) - m[2][0] * r2;
        p.y = pos->y + m[0][1] * (15.0f - r1) - m[2][1] * r2 + 0.4f;
        p.z = pos->z + m[0][2] * (15.0f - r1) - m[2][2] * r2;
        backPush = spdNorm * 1.45f;
        latVel = randRange(2.15f) * (0.70f + 0.50f * spdNorm);
        v.x = -m[0][0] * backPush + m[2][0] * latVel;
        v.y = rise * (1.0f - 0.82f * spdNorm);
        v.z = -m[0][2] * backPush + m[2][2] * latVel;
        Vec3f_Set(&acc, 0.0f, -0.09f * spdNorm, 0.0f);

        pt = (WMParticle *)Particle_Spawn(smokeObj, p, v, acc, scale, 0.30f + 0.04f * d,
                                          (u16)(life + 1), (u8)alphaStep, maxAlpha, 0, 0, 0);
        if (pt != NULL) {
            u8 grey = (u8)(250 - (s32)(48.0f * d)); /* pale white, then greyer */
            pt->r = grey;
            pt->g = grey;
            pt->b = (u8)(grey + (grey < 250 ? 5 : 0));
        }
    } else {
        /* black smoke before the car burns out: gentler tilt and spread */
        r2 = randRange(7.8f);
        p.x = pos->x + m[0][0] * (15.0f - r1) - m[2][0] * r2;
        p.y = pos->y + m[0][1] * (15.0f - r1) - m[2][1] * r2;
        p.z = pos->z + m[0][2] * (15.0f - r1) - m[2][2] * r2;
        backPush = spdNorm * 0.75f;
        latVel = randRange(0.95f) * (0.80f + 0.35f * spdNorm);
        v.x = -m[0][0] * backPush + m[2][0] * latVel;
        v.y = 3.90f * (1.0f - 0.42f * spdNorm);
        v.z = -m[0][2] * backPush + m[2][2] * latVel;
        Vec3f_Set(&acc, 0.0f, -0.025f * spdNorm, 0.0f);
        Particle_Spawn(smokeObj, p, v, acc, 7.6f, 0.32f, (u16)life, 50, 250, 0, 1, 1);
    }
    return 1;
}

int WM_CanInitAiSkid(void) {
    if (!WM_Enabled() || D_800D31CC != 1 || gGame == NULL) {
        return 0;
    }
    if (*(s16 *)((u8 *)gGame + 0xC) != 2) {
        return 0;
    }
    if (gGame->x1100 == NULL || gGame->x1100->x70 == NULL || gGame->x1100->x70->xC == NULL) {
        return 0;
    }
    return 1;
}

static int skidHasWork(const WMSkidMgr *s) {
    s32 i;

    if (s == NULL) {
        return 0;
    }
    if (s->active) {
        return 1;
    }
    if (s->tris != NULL) {
        for (i = 0; i < 40; i++) {
            if (s->tris[i].active) {
                return 1;
            }
        }
    }
    return 0;
}

int WM_SkidShouldUpdate(void *skidPtr) {
    if (skidPtr == NULL) {
        return 0;
    }
    if (!WM_Enabled()) {
        return 1;
    }
    return skidHasWork((const WMSkidMgr *)skidPtr);
}

int WM_SkidShouldDraw(void *skidPtr) {
    if (skidPtr == NULL) {
        return 0;
    }
    if (!WM_Enabled()) {
        return 1;
    }
    return skidHasWork((const WMSkidMgr *)skidPtr);
}

int WM_SkidClipLife(void) {
    return WM_Enabled() ? 26 : 20;
}

void WM_SkidTagSurface(void *skidPtr, void *triPtr) {
    const WMSkidMgr *sk = (const WMSkidMgr *)skidPtr;
    WMSkidTri *tri = (WMSkidTri *)triPtr;

    if (sk != NULL && tri != NULL) {
        tri->surfaceType = sk->surfaceType;
    }
}

void WM_SkidColorAlpha(int surface, int life, unsigned char *r, unsigned char *g,
                       unsigned char *b, unsigned char *a) {
    int maxAlpha = 82;
    int alpha;

    switch (surface) {
    case 2: /* ice / snow */
    case 9:
        *r = 148;
        *g = 172;
        *b = 196;
        maxAlpha = 62;
        break;
    case 3: /* dry dirt */
    case 7: /* loose gravel */
    case 8: /* packed gravel */
        *r = 56;
        *g = 38;
        *b = 20;
        maxAlpha = 76;
        break;
    case 4: /* wet mud / grass */
        *r = 34;
        *g = 24;
        *b = 12;
        maxAlpha = 88;
        break;
    case 6: /* wood */
        *r = 46;
        *g = 30;
        *b = 16;
        maxAlpha = 72;
        break;
    default: /* asphalt / road / curb / metal */
        *r = 10;
        *g = 10;
        *b = 12;
        maxAlpha = 82;
        break;
    }

    if (life <= 0) {
        alpha = 0;
    } else if (life >= 26) {
        alpha = maxAlpha;
    } else {
        alpha = (life * maxAlpha) / 26;
    }
    *a = (unsigned char)alpha;
}

/* ---- Live brake & handbrake physics tuning helpers (human player cars only) ---- */

static int wmIsHumanCar(void *carPtr) {
    return carPtr != NULL && ((WMCar *)carPtr)->isAi == 0;
}

f32 WM_BrakeForceScale(void *carPtr) {
    return wmIsHumanCar(carPtr) ? WM_BrakeForceMult() : 1.0f;
}

f32 WM_BrakeBiteScale(void *carPtr) {
    return wmIsHumanCar(carPtr) ? WM_BrakeBiteMult() : 1.0f;
}

f32 WM_BrakeLockThreshold(void *carPtr) {
    return wmIsHumanCar(carPtr) ? WM_BrakeLockThresholdVal() : 10.0f;
}

f32 WM_BrakeLatGripScale(void *carPtr) {
    return wmIsHumanCar(carPtr) ? WM_BrakeLatGripMult() : 1.0f;
}

f32 WM_HandbrakeLatGrip(void *carPtr) {
    return wmIsHumanCar(carPtr) ? WM_HandbrakeGripVal() : 0.55f;
}

f32 WM_HandbrakeLonDrag(void *carPtr) {
    return wmIsHumanCar(carPtr) ? WM_HandbrakeDragMult() : 1.0f;
}

f32 WM_HandbrakeSlipThreshold(void *carPtr) {
    return wmIsHumanCar(carPtr) ? WM_HandbrakeSlipVal() : 1.0f;
}

/* ---- HUD Throttle (T, blue) & Brake (B, red) vector bars ---- */

#define WM_GFX2(g, a, b) do { WMGfx *_g = (g)++; _g->w0 = (u32)(a); _g->w1 = (u32)(b); } while (0)

/* ---- Widescreen HUD layout ([hud] in wm_tuning.ini, docs/HUD_LAYOUT.md) ---- */

extern u8 D_800D2E58; /* the views drawn: 1 = one player */
float gfx_hud_side_room(void);

/* Each group's top-left as the N64 draws it (N64 pixels) and the edge it keeps to on a
   wider screen: -1 left, 0 centre, 1 right */
static const struct {
    s16 x, y;
    s8 anchor;
} kHudGroup[WM_HUD_GROUPS] = {
    { 21, 21, -1 },  /* score/place: SCREEN_BOX1 and its text */
    { 139, 22, 0 },  /* time: SCREEN_BOX3 */
    { 219, 21, 1 },  /* cars left: SCREEN_BOX2 */
    { 32, 160, -1 }, /* map: RADAR and the cars on it */
    { 258, 150, 1 }, /* damage: HEALTH / WIRE */
    { 99, 193, 0 },  /* pedals: the T/B bars (WM_HudDraw), centred at 160, 199 */
};

static int hudGroupOf(int id) {
    switch (id) {
        case 168: case 436: case 444: case 413: case 518: /* SCREEN_BOX1, SCORE/, score, PLACE, place */
            return WM_HUD_SCORE;
        case 170: case 434: case 512: /* SCREEN_BOX3, TIME, time */
            return WM_HUD_TIME;
        case 169: case 504: case 424: /* SCREEN_BOX2, CARS LEFT, cars left */
            return WM_HUD_CARS;
        case 522: /* the speed under the damage gauge */
            return WM_HUD_DAMAGE;
        default:
            break;
    }
    if (id >= 14 && id <= 29) { /* RADAR_SP, CAR_RADAR_USER0..AI10_SP */
        return WM_HUD_MAP;
    }
    if ((id >= 40 && id <= 55) || (id >= 228 && id <= 259)) { /* HEALTH_*, WIRE_*, IMPACT_* (the hit flashes) */
        return WM_HUD_DAMAGE;
    }
    return -1;
}

/* One player's race on a wide screen, the layout on */
static int hudLayoutOn(void) {
    return WM_Enabled() && WM_GetTuning()->hudLayout && D_800D2E58 == 1 && Port_GameDrawsWorld() &&
           gfx_hud_side_room() > 0.0f;
}

void WM_HudGroupOffset(int group, int *dx4, int *dy4, int *scale4096) {
    const WMTuning *t = WM_GetTuning();
    float room, tx, s;

    *dx4 = *dy4 = 0;
    *scale4096 = 4096;
    if (group < 0 || group >= WM_HUD_GROUPS || !hudLayoutOn() || t->hudPos[group][0] < 0) {
        return;
    }
    /* the template is 1280x720: 3 pixels an N64 pixel, the 4:3 area from x 160 to 1120 */
    room = gfx_hud_side_room();
    if (kHudGroup[group].anchor < 0) {
        tx = -room + t->hudPos[group][0] / 3.0f;
    } else if (kHudGroup[group].anchor > 0) {
        tx = 320.0f + room - (1280 - t->hudPos[group][0]) / 3.0f;
    } else {
        tx = 160.0f + (t->hudPos[group][0] - 640) / 3.0f;
    }
    /* drawn at s about the screen's top-left, then moved so the group's top-left lands on tx */
    s = t->hudScale[group];
    *scale4096 = (int)(s * 4096.0f);
    *dx4 = (int)((tx - kHudGroup[group].x * s) * 4.0f);
    if (t->hudPos[group][1] >= 0) {
        *dy4 = (int)((t->hudPos[group][1] / 3.0f - kHudGroup[group].y * s) * 4.0f);
    } else {
        *dy4 = (int)(kHudGroup[group].y * (1.0f - s) * 4.0f);
    }
}

void WM_HudOffset(int id, int *dx4, int *dy4, int *scale4096) {
    WM_HudGroupOffset(hudGroupOf(id), dx4, dy4, scale4096);
}

/* The start lights (READY / SET / GO and their halos, the orange light) */
float WM_HudScale(int id) {
    if ((id == 161 || id == 162 || (id >= 260 && id <= 265)) && WM_Enabled()) {
        return WM_GetTuning()->hudStartLights;
    }
    return 1.0f;
}

static WMGfx *wmBeginRects(WMGfx *g) {
    WM_GFX2(g, 0xE7000000, 0);
    WM_GFX2(g, 0xE3000A01, 0);          /* one cycle */
    WM_GFX2(g, 0xE200001C, 0x0F0A4000); /* blended overlay surface */
    WM_GFX2(g, 0xFCFFFFFF, 0xFFFDF6FB); /* output = primitive color + alpha */
    return g;
}

static WMGfx *wmRect(WMGfx *g, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba) {
    if (x1 <= x0 || y1 <= y0) {
        return g;
    }
    WM_GFX2(g, 0xFA000000, rgba);
    WM_GFX2(g, 0xF6000000 | (((u32)(x1 * 4) & 0xFFFU) << 12) | ((u32)(y1 * 4) & 0xFFFU),
               (((u32)(x0 * 4) & 0xFFFU) << 12) | ((u32)(y0 * 4) & 0xFFFU));
    return g;
}

static WMGfx *wmDrawFrame(WMGfx *g, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba) {
    g = wmRect(g, x0, y0, x1, y0 + 1, rgba);
    g = wmRect(g, x0, y1 - 1, x1, y1, rgba);
    g = wmRect(g, x0, y0 + 1, x0 + 1, y1 - 1, rgba);
    g = wmRect(g, x1 - 1, y0 + 1, x1, y1 - 1, rgba);
    return g;
}

static WMGfx *wmDrawGlyphT(WMGfx *g, s32 x, s32 y, u32 rgba) {
    g = wmRect(g, x + 1, y + 1, x + 9, y + 3, 0x000000D0);
    g = wmRect(g, x + 4, y + 3, x + 6, y + 10, 0x000000D0);
    g = wmRect(g, x, y, x + 8, y + 2, rgba);
    g = wmRect(g, x + 3, y + 2, x + 5, y + 9, rgba);
    return g;
}

static WMGfx *wmDrawGlyphBParts(WMGfx *g, s32 x, s32 y, u32 rgba) {
    g = wmRect(g, x, y, x + 2, y + 9, rgba);
    g = wmRect(g, x + 2, y, x + 6, y + 2, rgba);
    g = wmRect(g, x + 6, y + 1, x + 8, y + 4, rgba);
    g = wmRect(g, x + 2, y + 4, x + 6, y + 5, rgba);
    g = wmRect(g, x + 6, y + 5, x + 8, y + 8, rgba);
    g = wmRect(g, x + 2, y + 7, x + 6, y + 9, rgba);
    return g;
}

static WMGfx *wmDrawGlyphB(WMGfx *g, s32 x, s32 y, u32 rgba) {
    g = wmDrawGlyphBParts(g, x + 1, y + 1, 0x000000D0);
    return wmDrawGlyphBParts(g, x, y, rgba);
}

static WMGfx *wmDrawVectorBar(WMGfx *g, s32 x0, s32 y0, s32 x1, s32 y1, f32 val,
                              u32 bgRgba, u32 borderRgba, u32 fillRgba, u32 capRgba) {
    s32 innerH = (y1 - 2) - (y0 + 2);
    s32 quarter = innerH / 4;
    s32 fillH;

    if (val < 0.0f) {
        val = 0.0f;
    } else if (val > 1.0f) {
        val = 1.0f;
    }
    g = wmRect(g, x0 + 1, y0 + 1, x1 - 1, y1 - 1, bgRgba);
    g = wmDrawFrame(g, x0, y0, x1, y1, borderRgba);

    g = wmRect(g, x0 + 2, y0 + 2 + quarter, x1 - 2, y0 + 2 + quarter + 1, 0xFFFFFF28);
    g = wmRect(g, x0 + 2, y0 + 2 + quarter * 2, x1 - 2, y0 + 2 + quarter * 2 + 1, 0xFFFFFF38);
    g = wmRect(g, x0 + 2, y0 + 2 + quarter * 3, x1 - 2, y0 + 2 + quarter * 3 + 1, 0xFFFFFF28);

    if (val > 0.005f) {
        s32 fillTop;
        s32 capBottom;
        fillH = (s32)(val * (f32)innerH + 0.5f);
        if (fillH < 2) {
            fillH = 2;
        } else if (fillH > innerH) {
            fillH = innerH;
        }
        fillTop = (y1 - 2) - fillH;
        capBottom = fillTop + 2;
        if (capBottom > y1 - 2) {
            capBottom = y1 - 2;
        }
        g = wmRect(g, x0 + 2, fillTop, x1 - 2, y1 - 2, fillRgba);
        g = wmRect(g, x0 + 2, fillTop, x1 - 2, capBottom, capRgba);
    }
    return g;
}

/* A horizontal bar filling with val from its left end, or from its right end */
static WMGfx *wmDrawHBar(WMGfx *g, s32 x0, s32 y0, s32 x1, s32 y1, f32 val, int fromRight,
                         u32 bgRgba, u32 borderRgba, u32 fillRgba, u32 capRgba) {
    s32 innerW = (x1 - 2) - (x0 + 2);
    s32 quarter = innerW / 4;
    s32 fillW, i;

    if (val < 0.0f) {
        val = 0.0f;
    } else if (val > 1.0f) {
        val = 1.0f;
    }
    g = wmRect(g, x0 + 1, y0 + 1, x1 - 1, y1 - 1, bgRgba);
    g = wmDrawFrame(g, x0, y0, x1, y1, borderRgba);
    for (i = 1; i <= 3; i++) {
        g = wmRect(g, x0 + 2 + quarter * i, y0 + 2, x0 + 2 + quarter * i + 1, y1 - 2, i == 2 ? 0xFFFFFF38 : 0xFFFFFF28);
    }
    if (val > 0.005f) {
        fillW = (s32)(val * (f32)innerW + 0.5f);
        if (fillW < 2) {
            fillW = 2;
        } else if (fillW > innerW) {
            fillW = innerW;
        }
        if (!fromRight) {
            g = wmRect(g, x0 + 2, y0 + 2, x0 + 2 + fillW, y1 - 2, fillRgba);
            g = wmRect(g, x0 + fillW, y0 + 2, x0 + 2 + fillW, y1 - 2, capRgba);
        } else {
            g = wmRect(g, x1 - 2 - fillW, y0 + 2, x1 - 2, y1 - 2, fillRgba);
            g = wmRect(g, x1 - 2 - fillW, y0 + 2, x1 - fillW, y1 - 2, capRgba);
        }
    }
    return g;
}

/* ---- LED pedal bars: segments lit along a ramp in the HUD's colours ---- */

/* A colour on the ramp: t 0..1 through the stops (r g b) */
static u32 wmRamp(const u8 (*stops)[3], int n, f32 t, u32 alpha) {
    f32 p;
    int i;
    u32 r, gg, b;

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    p = t * (f32)(n - 1);
    i = (int)p;
    if (i >= n - 1) i = n - 2;
    p -= (f32)i;
    r = (u32)(stops[i][0] + (stops[i + 1][0] - stops[i][0]) * p);
    gg = (u32)(stops[i][1] + (stops[i + 1][1] - stops[i][1]) * p);
    b = (u32)(stops[i][2] + (stops[i + 1][2] - stops[i][2]) * p);
    return (r << 24) | (gg << 16) | (b << 8) | (alpha & 0xFF);
}

/* Throttle: dark green, green, yellow, a white-hot tip. Brake: dark olive, yellow, white */
static const u8 kRampGas[4][3] = { { 0x1A, 0x6A, 0x28 }, { 0x3C, 0xD2, 0x4A }, { 0xF2, 0xE6, 0x40 }, { 0xFF, 0xFF, 0xF0 } };
static const u8 kRampBrake[4][3] = { { 0x5A, 0x54, 0x16 }, { 0xD8, 0xB8, 0x22 }, { 0xFF, 0xE6, 0x50 }, { 0xFF, 0xFF, 0xF0 } };

#define WM_LED_SEGS 11

/* 48x12: a dark panel, 11 segments of 3 with a 1 gap, lit from the left (or the right)
   up to val, the edge segment lit in part; each lit segment has a glossy top line. Full,
   the frame pulses; `hot` (the handbrake) turns the frame white */
static WMGfx *wmDrawLedBar(WMGfx *g, s32 x0, s32 y0, s32 x1, s32 y1, f32 val, int fromRight,
                           const u8 (*ramp)[3], u32 frame, int hot) {
    s32 i, sx;
    f32 lit;
    u32 border = 0x2A8A3AE0;

    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;
    if (hot) {
        border = 0xFFFFFFF0;
    } else if (val >= 0.98f) {
        border = (frame & 8) ? 0xC8FF70FF : 0x58E060F0; /* a slow blink at full */
    }
    g = wmRect(g, x0 + 1, y0 + 1, x1 - 1, y1 - 1, 0x06140AC8);
    g = wmDrawFrame(g, x0, y0, x1, y1, border);
    lit = val * (f32)WM_LED_SEGS;
    for (i = 0; i < WM_LED_SEGS; i++) {
        f32 on = lit - (f32)i; /* 1 lit, 0 dark, in between the edge */
        u32 a;
        sx = fromRight ? (x1 - 2 - 4 * i - 3) : (x0 + 2 + 4 * i);
        if (on > 1.0f) on = 1.0f;
        if (on < 0.0f) on = 0.0f;
        a = 0x30 + (u32)(on * (f32)(0xFF - 0x30));
        g = wmRect(g, sx, y0 + 2, sx + 3, y1 - 2, wmRamp(ramp, 4, (f32)i / (f32)(WM_LED_SEGS - 1), a));
        if (on > 0.5f) {
            g = wmRect(g, sx, y0 + 2, sx + 3, y0 + 3, 0xFFFFFF60); /* gloss */
        }
    }
    return g;
}

static f32 stepSmoothBar(f32 cur, f32 target, f32 riseRate, f32 fallRate) {
    if (target > cur) {
        cur += (target - cur) * riseRate + 0.025f;
        if (cur > target) {
            cur = target;
        }
    } else if (target < cur) {
        cur -= (cur - target) * fallRate + 0.03f;
        if (cur < target) {
            cur = target;
        }
    }
    return cur;
}

void *WM_HudDraw(void *gfxPtr) {
    static f32 sSmoothT, sSmoothB;
    static u32 sFrame;
    WMGfx *g = (WMGfx *)gfxPtr;
    WMCar *car;
    u16 btn = 0;
    s8 sx = 0, sy = 0;
    s32 ch = 0;
    f32 rawT = 0.0f, rawB = 0.0f;
    int handbrake = 0;
    int hudDx4 = 1; /* (always reset after the bars) */

    {
        /* the touch controls hide the up/down arrows while the player drives (video.c) */
        void Port_NoteDriving(int driving);
        int Port_OptIsOpen(void);
        Port_NoteDriving(Port_GameDrawsWorld() && gGame != NULL &&
                         (gGame->x10FC == 0x1B || gGame->x10FC == 0x1C) && !Port_OptIsOpen());
    }
    if (!WM_HudBarsEnabled() || !Port_GameDrawsWorld() || gGame == NULL || D_800D31CC != 1) {
        sSmoothT = 0.0f;
        sSmoothB = 0.0f;
        return WM_ScoreDraw(gfxPtr);
    }
    if (gGame->x10FC != 0x1B && gGame->x10FC != 0x1C) {
        if (gGame->x10FC != 0x1F && gGame->x10FC != 0x20) {
            sSmoothT = 0.0f;
            sSmoothB = 0.0f;
        }
        return WM_ScoreDraw(gfxPtr);
    }

    car = (WMCar *)D_800958F0[0].x0;
    {
        /* DD64_CARHP=n (testing): the player's car at n health, once per race */
        static void *applied;
        const char *e = getenv("DD64_CARHP");
        if (e != NULL && car != NULL && applied != (void *)car) {
            car->health = atoi(e);
            applied = car;
        }
    }
    if (car == NULL || car->wrecked) {
        sSmoothT = 0.0f;
        sSmoothB = 0.0f;
        return WM_ScoreDraw(gfxPtr);
    }

    if (car->isAi == 0 && car->playerInfo != NULL) {
        ch = *(s8 *)((u8 *)car->playerInfo + 0x10);
    } else {
        ch = -1;
    }

    Port_OptMenuPad(&btn, &sx, &sy);
    if ((btn & 0x8000) || (car->carInput != NULL && car->carInput->x0 && !car->carInput->x3)) {
        rawT = ch >= 0 ? Port_InputGas(ch) : 1.0f;
    }
    if ((btn & 0x4000) || (car->carInput != NULL && car->carInput->x1)) {
        rawB = ch >= 0 ? Port_InputBrake(ch) : 1.0f;
    }
    if ((btn & 0x0010) || (car->carInput != NULL && car->carInput->x2)) {
        handbrake = 1;
        if (rawB < 1.0f) {
            rawB = 1.0f;
        }
    }

    sSmoothT = stepSmoothBar(sSmoothT, rawT, 0.28f, 0.30f);
    sSmoothB = stepSmoothBar(sSmoothB, rawB, 0.34f, 0.32f);

    /* Directly left of the car damage gauge and level with it: the bars and their
       T / B letters span the gauge's height (about y 152-220 of the 320x240 screen) */
    {
        /* where the widescreen layout puts the pedals */
        int dx4, dy4, s4096;
        WM_HudGroupOffset(WM_HUD_PEDALS, &dx4, &dy4, &s4096);
        hudDx4 = dx4 | dy4 | (s4096 - 4096);
        WM_GFX2(g, 0x00490000, s4096);
        WM_GFX2(g, 0x00480000 | (dy4 & 0xFFFF), dx4);
    }
    /* Side by side under the car, centred at 160, 199: T fills from the left toward the
       middle, B from the right toward the middle, the letters at the outer ends */
    sFrame++;
    g = wmBeginRects(g);
    g = wmDrawLedBar(g, 109, 193, 157, 205, sSmoothT, 0, kRampGas, sFrame, 0);
    g = wmDrawLedBar(g, 163, 193, 211, 205, sSmoothB, 1, kRampBrake, sFrame, handbrake);
    g = wmDrawGlyphT(g, 98, 195, 0x5CFF6AFF);
    g = wmDrawGlyphB(g, 214, 195, 0xFFE14AFF);
    WM_GFX2(g, 0xE7000000, 0);
    if (hudDx4 != 0) {
        WM_GFX2(g, 0x00490000, 4096); /* the scale and offset back off */
        WM_GFX2(g, 0x00480000, 0);
    }
    return WM_ScoreDraw(g);
}
