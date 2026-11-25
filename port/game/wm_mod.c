/*
 * Game-side Wrecked Metal mod hooks (built in the `game` target with
 * -fsanitize=kernel-address so N64 RAM structs are accessed seamlessly).
 * Every feature here stays inactive when WM_Enabled() is 0 (Play Fidelity).
 */
#include "common.h"
#include "wm_mod.h"
void Port_Log(const char *, ...);
#include <string.h>
#include <math.h>
#include <stdint.h>

char *getenv(const char *);
int atoi(const char *);
double atof(const char *);

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
 *   21..1    heavy: thick white, the N64's stage, more and more of it black and greyer
 *            as the health nears 0 (one extra particle under 10)
 *   0        black, before the car burns out
 * The player's car smokes at the full rate; the other cars at half of it (every other
 * frame per stage), which halves their particles. The wheel dust keeps WM_HOOD_RESERVE
 * particles of the shared pool free for it, so a dusty race no longer starves it.
 */
static int wmIsHumanCar(void *carPtr);

#define WM_HOOD_LIGHT 80
#define WM_HOOD_MEDIUM 50
#define WM_HOOD_HEAVY 21

/* The smoke follows the health as a share of a full car's: with [cars] durability every
   car starts on that % of the Baja Buggy's, so the stages scale with it (at 50, light
   smoke from 40, black near 0), or every car would smoke from the start */
static s32 wmSmokeHp(s32 hp) {
    int pct = WM_DurabilityPct();
    return pct > 0 ? hp * 100 / pct : hp;
}

int WM_HoodSmokeStartHp(void) {
    int pct = WM_DurabilityPct();
    if (!WM_Enabled()) {
        return WM_HOOD_HEAVY;
    }
    return pct > 0 ? (WM_HOOD_LIGHT * pct + 99) / 100 : WM_HOOD_LIGHT;
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
    s32 life, hp = wmSmokeHp(car->health), every, maxAlpha, alphaStep;
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

    {
    int n = (hp > 0 && hp < 10) ? 2 : 1, k;
    for (k = 0; k < n && smokeObj->count < smokeObj->maxCount; k++) {
    /* under 21 health, a share of the particles black: none at 21, nearly all at 1 */
    int black = hp <= 0 || (hp < WM_HOOD_HEAVY && (rand() % WM_HOOD_HEAVY) >= hp);
    if (!black) {
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
            s32 g = 250 - (s32)(48.0f * d); /* pale white, then greyer */
            u8 grey;
            if (hp < WM_HOOD_HEAVY) {
                g -= (WM_HOOD_HEAVY - hp) * 4; /* sooty near the end: down to about 120 */
            }
            grey = (u8)g;
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
    }
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

/* ---- Impact sparks (the spark pool, D_801245A4 + 0xC) ---- */

/*
 * The N64 marks a hard wall hit (surfaces 5 and 8) with Particles_EmitTrail: a streak
 * of still spark sprites along the car's last move. On top of it, and on car-to-car
 * hits, Wrecked Metal throws a burst from the contact point: sparks flying out and up,
 * carried a little by the car, falling back under gravity, shrinking and fading. Their
 * number follows the hit's force, halved when no player's car is in it.
 */
typedef struct {
    u8 pad0[4];
    s32 count;          /* 0x04 */
    u8 pad8[0xA0 - 0x8];
    s32 maxCount;       /* 0xA0 (Particles_EmitTrail's limit) */
} WMSparkObj;

static int wmIsHumanCar(void *carPtr);

void Frame_ToMatrix(f32 m[4][4], void *car, s32 world);

void WM_EmitSparks(void *poolPtr, void *carPtr, f32 offX, f32 offY, f32 offZ, f32 force, void *otherCar) {
    WMSparkObj *pool = (WMSparkObj *)poolPtr;
    f32 m[4][4];
    f32 s;
    Vec3f base, v, acc;
    int n, i;
    static f32 kv = -1.0f, sz = 12.0f;

    if (!WM_Enabled() || pool == NULL || gGame == NULL) {
        return;
    }
    if (kv < 0.0f) {
        const char *e = getenv("DD64_SPARKV"); /* testing: the speed scale */
        const char *z = getenv("DD64_SPARKSIZE"); /* testing: the size */
        kv = e != NULL ? (f32)atof(e) : 1.0f;
        sz = z != NULL ? (f32)atof(z) : 12.0f;
    }
    s = gGame->x1100->x70->xC->x10;
    n = 6 + (int)(force * 0.6f);
    if (n > 18) {
        n = 18;
    }
    /* (the computer cars' own hits get the full burst too: they are seen from afar) */
    /* where the car is, as the hood smoke takes it (Car_UpdateWreck), plus the contact's offset */
    Frame_ToMatrix(m, carPtr, 1);
    base.x = (m[3][0] + offX) * s;
    base.y = (m[3][1] + offY) * s;
    base.z = (m[3][2] + offZ) * s;
    Vec3f_Set(&acc, 0.0f, -0.11f * kv, 0.0f);
    for (i = 0; i < n && pool->count < pool->maxCount; i++) {
        int far = rand() % 7 == 0; /* about one in seven flies off further and lives longer */
        f32 spread = (far ? 1.4f + (f32)(rand() % 100) * 0.01f : 0.45f + (f32)(rand() % 100) * 0.007f) * kv;
        f32 ang = (f32)(rand() % 628) * 0.01f;
        WMParticle *pt;
        v.x = cosf(ang) * spread;
        v.y = (0.45f + (f32)(rand() % 100) * 0.006f) * kv; /* 0.45 .. 1.05 up: a low spray */
        v.z = sinf(ang) * spread;
        pt = (WMParticle *)Particle_Spawn(poolPtr, base, v, acc, sz + (f32)(rand() % 100) * 0.03f, -sz * 0.05f,
                                          (u16)((far ? 16 : 9) + rand() % 6), 0x12, 0xFF, 0, 0, 0);
        if (pt != NULL) {
            /* hot metal: white, yellow, orange and a few red-orange embers */
            static const u8 kSpark[6][3] = { { 255, 252, 230 }, { 255, 240, 150 }, { 255, 214, 70 },
                                             { 255, 170, 40 }, { 255, 128, 24 }, { 240, 82, 20 } };
            const u8 *c = kSpark[far ? 3 + rand() % 3 : rand() % 5];
            pt->r = c[0];
            pt->g = c[1];
            pt->b = c[2];
        }
    }
}

/* ---- Headlight beams (Car_BuildLightDisplayLists, src/auto/1EDF4.c) ---- */

typedef struct { s16 x, y, z; u16 flag; s16 s, t; u8 r, g, b, a; } WMVtx;

/* The headlights' glow texture (D_80090930: 32x32 RGBA32, bytes R G B A in memory) warmer,
   once: a 1990s sealed-beam yellow-white. Green a touch down, blue well down */
void WM_WarmHeadlightTexture(void *tex) {
    static void *done;
    u8 *p = (u8 *)tex;
    int i;

    if (!WM_Enabled() || tex == NULL || done == tex) {
        return;
    }
    done = tex;
    if (getenv("DD64_DUMPLAMP") != NULL) { /* testing: the texture before and after, raw */
        void Port_DumpBytes(const char *name, const void *p, int n);
        Port_DumpBytes("lamp_before.bin", tex, 4096);
    }
    for (i = 0; i < 32 * 32; i++) {
        p[i * 4 + 1] = (u8)(p[i * 4 + 1] * 238 / 255);
        p[i * 4 + 2] = (u8)(p[i * 4 + 2] * 160 / 255);
    }
    if (getenv("DD64_DUMPLAMP") != NULL) {
        void Port_DumpBytes(const char *name, const void *p, int n);
        Port_DumpBytes("lamp_after.bin", tex, 4096);
    }
}

/*
 * The headlights' light on the road ahead: one soft fan per car, as a light pool would
 * fall on the asphalt. A 5 x 5 grid of vertices in the car's own space (drawn in its display
 * list, so it follows the car for free): it is born at the two lamps themselves, drops to
 * just above the road, opens to a few car widths and fades in its vertices, with alpha 0 on
 * every edge (no straight cut anywhere) and the far end rounded. Untextured, the colour and
 * the fade in the vertices: 25 vertices, 32 small triangles a car.
 * WM_HeadlightFade, each frame the car is drawn: the fan's size once the car's driver is
 * known (70% for a player, 50% for the computer cars), its fade on a tilted or flying car,
 * and the lamps breaking with the front corners' damage.
 */
#define WM_FAN_ROWS 5
#define WM_FAN_COLS 5

typedef struct {
    WMVtx *v;           /* the fan's 25 vertices (the key) */
    u8 sized, broken;   /* broken: bit 0 lamp 0, bit 1 lamp 1 (what is drawn dark now) */
    void *car;          /* the car it belongs to (WM_HeadlightFade) */
    s32 zmax[3];        /* the front, left-front and right-front zones' health at its best */
    /* a failing lamp (first hit on its corner) flickers: per lamp, frames to the next change,
       blinks left in the burst, and whether it is out right now */
    s16 timer[2];
    u8 blinks[2], out[2];
    /* the glow quads' four G_TRI1 commands, kept to turn a lamp back on */
    WMGfx *tri[4];
    u32 triW0[4], triW1[4];
    u8 triSaved;
    u32 used;           /* when it was last drawn (sFanClock); 0 never */
} WMFan;
/* A slot per car display list: a race sets its cars up twice and every race makes new
   lists, so slots nobody draws any more are reused, the longest unused first. (It used to
   clear every slot once all 32 were taken, a few races in: the cars set up before that
   lost theirs and their lamps no longer broke.) */
#define WM_FAN_SLOTS 64
static WMFan sFans[WM_FAN_SLOTS];
static u32 sFanClock;

static WMFan *wmFanSlot(WMVtx *v, int make) {
    int i, free = -1;
    for (i = 0; i < WM_FAN_SLOTS; i++) {
        if (sFans[i].v == v) {
            return &sFans[i];
        }
        if (sFans[i].v == NULL && free < 0) {
            free = i;
        }
    }
    if (!make) {
        return NULL;
    }
    if (free < 0) {
        free = 0;
        for (i = 1; i < WM_FAN_SLOTS; i++) {
            if (sFans[i].used < sFans[free].used) {
                free = i;
            }
        }
    }
    memset(&sFans[free], 0, sizeof(sFans[free]));
    sFans[free].v = v;
    return &sFans[free];
}

/* The vertices for size k, with broken lamps dark (their side of the fan gone) */
static int wmFanBuild(WMVtx *v, const f32 *rear, const f32 *front, f32 k, int broken) {
    /* along the beam (car lengths), the outer half width (in half lamp spacings), alpha */
    static const f32 kDist[WM_FAN_ROWS] = { 0.0f, 1.0f, 2.6f, 4.4f, 6.0f };
    static const f32 kHalf[WM_FAN_ROWS] = { 1.35f, 3.2f, 5.2f, 6.4f, 5.8f };
    static const u8 kAlphaLamp[WM_FAN_ROWS] = { 110, 145, 115, 55, 0 }; /* columns 1 and 3 */
    static const u8 kAlphaMid[WM_FAN_ROWS] = { 20, 140, 145, 62, 0 };   /* column 2 */
    f32 fwd[3], side[3], up[3], mid[3], len, sl, ul, halfLamp, ground;
    int r, c, kk;

    for (kk = 0; kk < 3; kk++) {
        fwd[kk] = (front[kk] + front[3 + kk]) * 0.5f - (rear[kk] + rear[3 + kk]) * 0.5f;
        side[kk] = front[3 + kk] - front[kk];
        mid[kk] = (front[kk] + front[3 + kk]) * 0.5f;
    }
    len = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    sl = sqrtf(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
    if (len < 0.001f || sl < 0.001f) {
        return 0;
    }
    for (kk = 0; kk < 3; kk++) {
        fwd[kk] /= len;
        side[kk] /= sl;
    }
    up[0] = fwd[1] * side[2] - fwd[2] * side[1];
    up[1] = fwd[2] * side[0] - fwd[0] * side[2];
    up[2] = fwd[0] * side[1] - fwd[1] * side[0];
    ul = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    if (ul < 0.001f) {
        return 0;
    }
    for (kk = 0; kk < 3; kk++) {
        up[kk] /= ul;
    }
    halfLamp = sl * 0.5f;
    ground = len * 0.02f;

    for (r = 0; r < WM_FAN_ROWS; r++) {
        /* row 0 sits on the lamps: its inner columns are the lamps themselves */
        f32 outer = r == 0 ? kHalf[0] * halfLamp : kHalf[r] * k * halfLamp;
        f32 inner = r == 0 ? halfLamp : outer * 0.5f;
        if (inner < halfLamp) {
            inner = halfLamp; /* never narrower than the lamps */
        }
        if (outer < inner * 1.35f) {
            outer = inner * 1.35f;
        }
        for (c = 0; c < WM_FAN_COLS; c++) {
            WMVtx *q = &v[r * WM_FAN_COLS + c];
            f32 d = kDist[r] * len * k, h = r == 0 ? 0.0f : -ground, w, p[3];
            f32 a;
            w = c == 0 ? -outer : c == 1 ? -inner : c == 2 ? 0.0f : c == 3 ? inner : outer;
            if (r == WM_FAN_ROWS - 1) {
                /* the far end rounded: the outer columns come back toward the lamps */
                d -= (c == 0 || c == 4 ? 1.3f : c == 2 ? 0.0f : 0.35f) * len * k;
            }
            for (kk = 0; kk < 3; kk++) {
                p[kk] = mid[kk] + fwd[kk] * d + side[kk] * w + up[kk] * h;
            }
            a = (c == 0 || c == 4) ? 0.0f : (c == 2 ? kAlphaMid[r] : kAlphaLamp[r]);
            /* a broken lamp's side goes dark, the middle at half */
            if ((broken & 1) && c < 2) a = 0.0f;
            if ((broken & 2) && c > 2) a = 0.0f;
            if (c == 2 && broken) a *= broken == 3 ? 0.0f : 0.5f;
            q->x = (s16)(p[0] * 100.0f);
            q->y = (s16)(p[1] * 100.0f);
            q->z = (s16)(p[2] * 100.0f);
            q->flag = 0;
            q->s = q->t = 0;
            q->r = 255;
            q->g = (u8)(240 - r * 4); /* white-yellow, a little warmer far out */
            q->b = (u8)(196 - r * 10);
            q->a = (u8)a;
        }
    }
    return 1;
}

void *WM_HeadlightBeams(void *gfxPtr, void *vtxPtr, const float *rear, const float *front) {
    WMGfx *g = (WMGfx *)gfxPtr;
    WMVtx *v = (WMVtx *)vtxPtr;
    WMFan *fan;
    int r, c;

    if (!WM_Enabled()) {
        return gfxPtr;
    }
    fan = wmFanSlot(v, 1);
    /* (a race sets its cars up twice while loading: start this car's fan over) */
    fan->sized = 0;
    fan->broken = 0;
    fan->triSaved = 0;
    memset(fan->zmax, 0, sizeof(fan->zmax));
    memset(fan->timer, 0, sizeof(fan->timer));
    memset(fan->out, 0, sizeof(fan->out));
    if (!wmFanBuild(v, rear, front, 1.0f, 0)) {
        fan->v = NULL;
        return gfxPtr;
    }
    WM_GFX2(g, 0xE7000000, 0);
    WM_GFX2(g, 0xD9FDF9FF, 0x00200004);          /* no lighting, no culling; shade, smooth */
    WM_GFX2(g, 0xFC41C683, 0xFF8FFFFF);          /* shade x primitive, colour and alpha */
    WM_GFX2(g, 0xFA0000FE, 0xFFFFFFFF);          /* the primitive colour: WM_HeadlightFade sets its alpha */
    WM_GFX2(g, 0xE200001C, 0x00504A50);          /* depth tested, not written, blended */
    WM_GFX2(g, 0x01000000 | ((WM_FAN_ROWS * WM_FAN_COLS) << 12) | ((WM_FAN_ROWS * WM_FAN_COLS) << 1),
            (u32)(uintptr_t)v);                  /* 25 vertices into slots 0-24 */
    for (r = 0; r + 1 < WM_FAN_ROWS; r++) {
        for (c = 0; c + 1 < WM_FAN_COLS; c++) {
            u32 a0 = r * WM_FAN_COLS + c, a1 = a0 + 1, b0 = a0 + WM_FAN_COLS, b1 = b0 + 1;
            WM_GFX2(g, 0x06000000 | (a0 * 2) << 16 | (a1 * 2) << 8 | (b1 * 2), (a0 * 2) << 16 | (b1 * 2) << 8 | (b0 * 2));
        }
    }
    WM_GFX2(g, 0xD9FFFFFF, 0x00020000);          /* lighting back on for what follows */
    return g;
}

/* How much of the headlights' light on the road and walls is left: 1, half with one lamp
   broken, 0 with both (Light_UpdateVertexColors, through WM_LightMul) */
static f32 sLightMul = 1.0f;

void WM_SetLightCar(void *carPtr) {
    int i;
    WMFan *best = NULL;
    sLightMul = 1.0f;
    /* the car's slot drawn last (an old race's car may have had the same address) */
    for (i = 0; i < WM_FAN_SLOTS; i++) {
        if (sFans[i].v != NULL && sFans[i].car == carPtr && (best == NULL || sFans[i].used > best->used)) {
            best = &sFans[i];
        }
    }
    if (best != NULL) {
        int b = best->broken;
        sLightMul = b == 3 ? 0.0f : b ? 0.55f : 1.0f;
    }
}

float WM_LightMul(void) {
    return sLightMul;
}

/* A small random sequence of our own, so the flicker leaves the game's rand() alone */
static u32 wmRand(void) {
    static u32 s = 0x9E3779B9u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/* A front corner's health (Car.damage[zone], the u32 at +8 of each 0x14-byte zone) */
static s32 wmZoneHealth(void *carPtr, int zone) {
    return *(s32 *)((u8 *)carPtr + 0xA6C + zone * 0x14 + 8);
}

void WM_HeadlightFade(void *carPtr, void *dlPtr) {
    static void *sCar[24];
    static f32 sK[24];
    WMCar *car = (WMCar *)carPtr;
    WMGfx *g = (WMGfx *)dlPtr, *s, *prim = NULL;
    WMVtx *v = NULL;
    WMFan *fan;
    f32 m[4][4], upLen, upright, target;
    int i, air = 0, slot, broken = 0;
    static int test = -2, testFlick = -1;

    if (!WM_Enabled() || dlPtr == NULL) {
        return;
    }
    for (i = 0, s = g; i < 96 && (s->w0 >> 24) != 0xDF; i++, s++) {
        if (s->w0 == 0xFA0000FE) {
            prim = s;
        } else if (prim != NULL && (s->w0 >> 24) == 0x01) {
            v = (WMVtx *)(uintptr_t)s->w1;
            break;
        }
    }
    if (prim == NULL || v == NULL || (fan = wmFanSlot(v, 0)) == NULL) {
        return;
    }
    fan->car = carPtr;
    fan->used = ++sFanClock;

    /* The lamps follow the front corners' damage (each zone starts at 3): lamp 0 (the car's
       right) the right front (zone 1), lamp 1 (its left) the left front (zone 4), both the
       front (zone 0). The first hit on a corner leaves its lamp failing: it flickers, in
       uneven bursts, now and then; the next hit breaks it (dark for good).
       DD64_BREAKLAMP=zone / DD64_FLICKLAMP=zone (testing) break / fail that zone at once */
    if (test == -2) {
        const char *e = getenv("DD64_BREAKLAMP");
        const char *f = getenv("DD64_FLICKLAMP");
        test = e != NULL ? atoi(e) : -1;
        testFlick = f != NULL ? atoi(f) : -1;
    }
    {
        static const int kZone[3] = { 0, 1, 4 };
        int failing = 0, dark;
        for (i = 0; i < 3; i++) {
            int bits = i == 0 ? 3 : i; /* zone 0 both lamps, zone 1 lamp 0 (bit 1), zone 4 lamp 1 (bit 2) */
            s32 h = wmZoneHealth(carPtr, kZone[i]);
            if (h > fan->zmax[i]) {
                fan->zmax[i] = h;
            }
            if (test == kZone[i]) {
                h = 0;
            } else if (testFlick == kZone[i] && fan->zmax[i] > 0) {
                h = fan->zmax[i] - 1;
            }
            /* by the hits taken (the same for every car: some cars' corners take 10-12
               hits, which a share of the zone's health left unbroken all race) */
            if (fan->zmax[i] > 0 && fan->zmax[i] - h >= 2) {
                broken |= bits;
            } else if (fan->zmax[i] > 0 && h < fan->zmax[i]) {
                failing |= bits;
            }
        }
        failing &= ~broken;
        /* the flicker: a lamp mostly on, going out in short bursts of 1-3 blinks (2-6 frames
           out, 2-5 back on), the bursts 10 frames to 3 seconds apart, sometimes close together */
        for (i = 0; i < 2; i++) {
            if (!(failing & (1 << i))) {
                fan->out[i] = 0;
                fan->timer[i] = 0;
                continue;
            }
            if (--fan->timer[i] > 0) {
                continue;
            }
            if (fan->out[i]) {
                fan->out[i] = 0; /* back on */
                fan->timer[i] = fan->blinks[i] > 0 ? (s16)(2 + wmRand() % 4)
                                                   : (s16)(wmRand() % 4 == 0 ? 10 + wmRand() % 30 : 40 + wmRand() % 140);
            } else {
                if (fan->blinks[i] == 0) {
                    u32 r = wmRand() % 10;
                    fan->blinks[i] = (u8)(r < 5 ? 1 : r < 8 ? 2 : 3);
                }
                fan->blinks[i]--;
                fan->out[i] = 1;
                fan->timer[i] = (s16)(2 + wmRand() % 5);
            }
        }
        dark = broken | (fan->out[0] ? 1 : 0) | (fan->out[1] ? 2 : 0);
        if (!fan->sized || dark != fan->broken) {
            f32 k = wmIsHumanCar(carPtr) ? 0.70f : 0.50f;
            /* the lamps from the car's own stats (CarStats at Car+0xBEC: +0x128 the rear
               lamps, +0x170 the front ones), as Car_BuildLightDisplayLists read them */
            const f32 *st = (const f32 *)((u8 *)carPtr + 0xBEC);
            wmFanBuild(v, st + 0x128 / 4, st + 0x170 / 4, k, dark);
            /* the lamps' glow quads (G_TRI1, vertices 0-3 lamp 0, 4-7 lamp 1): off for a dark
               lamp (the car shows its own unlit lamp texture), back on when it lights again */
            if (!fan->triSaved) {
                int n = 0;
                for (s = g; s < prim && n < 4; s++) {
                    if ((s->w0 >> 24) == 0x05) {
                        fan->tri[n] = s;
                        fan->triW0[n] = s->w0;
                        fan->triW1[n] = s->w1;
                        n++;
                    }
                }
                fan->triSaved = 1;
            }
            for (i = 0; i < 4; i++) {
                if (fan->tri[i] != NULL) {
                    int lamp = ((fan->triW0[i] >> 16) & 0xFF) / 2 >= 4;
                    fan->tri[i]->w0 = (dark & (1 << lamp)) ? 0 : fan->triW0[i];
                    fan->tri[i]->w1 = (dark & (1 << lamp)) ? 0 : fan->triW1[i];
                }
            }
            fan->sized = 1;
            fan->broken = (u8)dark;
        }
    }

    Frame_ToMatrix(m, carPtr, 1);
    upLen = sqrtf(m[1][0] * m[1][0] + m[1][1] * m[1][1] + m[1][2] * m[1][2]);
    upright = upLen > 0.0001f ? m[1][1] / upLen : 1.0f;
    for (i = 0; i < 4; i++) {
        if (car->wheels[i].groundNormal.z >= 1.0f) { /* a wheel in the air */
            air++;
        }
    }
    /* full on a level car with its wheels down; gone past ~25 degrees of tilt or two wheels up */
    target = (upright - 0.90f) / 0.06f;
    if (target > 1.0f) target = 1.0f;
    if (target < 0.0f) target = 0.0f;
    if (air >= 2) target = 0.0f;
    slot = (int)(((u32)(uintptr_t)carPtr >> 4) % 24);
    if (sCar[slot] != carPtr) {
        sCar[slot] = carPtr;
        sK[slot] = target;
    }
    sK[slot] += (target - sK[slot]) * 0.25f;
    prim->w1 = 0xFFFFFF00u | (u32)(sK[slot] * 255.0f);
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

/* [speedfx] lines: faint white streaks rushing out at the screen's sides above 60% of
   the top speed, 16 of them, each a 1-pixel-high fill rect sliding outward and
   lengthening, respawned near the middle third: the old comic-book speed lines, on the
   HUD's own fill rects (no framebuffer copy, no texture) */
static WMGfx *wmSpeedLines(WMGfx *g) {
    f32 WM_SpeedNorm(void);
    static f32 sx[16], sl[16];
    static s16 sy[16];
    static u8 sOn;
    f32 s = WM_SpeedNorm(), k;
    s32 i;
    if (!WM_GetTuning()->speedLines || s < 0.60f) {
        sOn = 0;
        return g;
    }
    k = (s - 0.60f) / 0.40f; /* 0-1 */
    if (!sOn) {
        for (i = 0; i < 16; i++) {
            sx[i] = 999.0f; /* all due a respawn */
        }
        sOn = 1;
    }
    g = wmBeginRects(g);
    for (i = 0; i < 16; i++) {
        s32 left = i & 1, x0, x1;
        u32 a;
        if (sx[i] > 170.0f) {
            /* respawn: out from 50-110 px off the middle, at any height but the HUD's */
            sx[i] = 50.0f + (f32)(wmRand() % 60);
            sl[i] = 6.0f + (f32)(wmRand() % 10);
            sy[i] = (s16)(40 + wmRand() % 160);
        }
        sx[i] += 9.0f + 10.0f * k + (f32)(i % 3);
        sl[i] += 2.5f + 3.0f * k;
        a = (u32)(0x18 + (s32)(0x24 * k) * (s32)(sx[i] < 150.0f ? 1 : 0));
        x0 = left ? 160 - (s32)(sx[i] + sl[i]) : 160 + (s32)sx[i];
        x1 = x0 + (s32)sl[i];
        if (x0 < 0) {
            x0 = 0;
        }
        if (x1 > 320) {
            x1 = 320;
        }
        g = wmRect(g, x0, sy[i], x1, sy[i] + 1, 0xFFFFFF00u | a);
    }
    return g;
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
        /* the touch controls hide the up/down arrows during a race, pause aside (video.c) */
        void Port_NoteDriving(int driving);
        Port_NoteDriving(Port_GameDrawsWorld() && gGame != NULL);
    }
    (void)wmSpeedLines; /* (the 2D streaks: replaced by the 3D wind, WM_EmitWind) */
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
    {
        /* DD64_SPARKTEST=n (testing): a spark burst off the player's car every n frames */
        static int every = -1;
        static int f;
        if (every < 0) {
            const char *e = getenv("DD64_SPARKTEST");
            every = e != NULL ? atoi(e) : 0;
        }
        if (every > 0 && car != NULL && D_801245A4 != NULL && ++f % every == 0) {
            WM_EmitSparks(D_801245A4 + 0xC, car, 0.0f, 0.5f, 0.0f, 15.0f, NULL);
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

/* ---- Wrecked Metal: the wreck's explosion and fire (Car_EmitWreckParticles) ----
 *
 * A real car fire does not throw balls about: the fuel goes up in a fireball that swells
 * where the car is, rolls upward and turns into a thick black column, while hot bits arc
 * out and fall. So, on the game's own flame and smoke particles (a few a frame):
 *   the blast (the first 0.6 s): flames born small all over the car, barely moving
 *     (outward at 1-2.5, up at 1.5-3.5) but swelling fast, staggered over the first
 *     frames so the fireball builds and rolls up as one mass; a dark core of smoke inside
 *     it; a handful of hot fragments arcing out and down under gravity; then a black
 *     column billowing up slowly and widening;
 *   the fire (to 50 s): two tongues of flame a frame from the car itself, the engine bay
 *     most (in its middle and both sides), the cabin (both windows, licking up) and the
 *     boot, each flickering, rising slowly and short-lived, with now and then a bigger
 *     flare; a black plume rolling off the roof; past 40 s it dies down; after 50 s the
 *     N64's smoke.
 * The matrix rows: m[0..2] the car's forward, m[4..6] up, m[8..10] its side (the units
 * those of the N64's own wreck flames: the hood is 15 forward). */
f32 WM_WreckAge(void *car, s32 n);

static f32 fxRand(void) { /* 0..1, our own sequence */
    return (f32)(wmRand() & 0xFFFF) / 65535.0f;
}

static f32 fxSigned(void) { /* -1..1 */
    return fxRand() * 2.0f - 1.0f;
}

static void fxAt(Vec3f *p, f32 px, f32 py, f32 pz, const f32 *m, f32 fwd, f32 side, f32 up) {
    p->x = px + m[0] * fwd + m[8] * side + m[4] * up;
    p->y = py + m[1] * fwd + m[9] * side + m[5] * up;
    p->z = pz + m[2] * fwd + m[10] * side + m[6] * up;
}

static void fxFlameAcc(Vec3f p, Vec3f v, f32 ay, f32 size, f32 grow, s32 life) {
    WMParticleObj *o = (WMParticleObj *)(D_801245A4 + 0x148);
    Vec3f acc;
    if (o->count >= o->maxCount) {
        return;
    }
    Vec3f_Set(&acc, 0.0f, ay, 0.0f);
    Particle_Spawn(o, p, v, acc, size, grow, (u16)life, (u8)(0xFF / life), 0xFF, 0, 0, 5);
}

static void fxFlame(Vec3f p, Vec3f v, f32 size, f32 grow, s32 life) {
    fxFlameAcc(p, v, 0.0f, size, grow, life);
}

static void fxSmoke(Vec3f p, Vec3f v, f32 size, f32 grow, s32 life) {
    WMParticleObj *o = (WMParticleObj *)(D_801245A4 + 0xB0);
    Vec3f acc;
    if (o->count >= o->maxCount) {
        return;
    }
    Vec3f_Set(&acc, 0.0f, 0.015f, 0.0f);
    Particle_Spawn(o, p, v, acc, size, grow, (u16)life, 50, 250, 0, 1, 1); /* black */
}

int WM_WreckFx(void *car, f32 px, f32 py, f32 pz, f32 *m, s32 n) {
    s32 life = 0x13 - D_800D31CC * 3, frame, i;
    f32 age;
    Vec3f p, v;
    if (!WM_Enabled() || D_801245A4 == NULL) {
        return 0;
    }
    age = WM_WreckAge(car, n);
    if (age < 0.0f || age >= 50.0f) {
        return 0; /* the N64's smoke */
    }
    frame = (s32)(age * 60.0f);
    if (age < 0.6f) {
        /* the blast */
        f32 k = 1.0f - age / 0.6f; /* 1 at the start, 0 at the end */
        if (frame < 8) {
            /* the fireball: born small over the whole car, swelling where it is, rolling up */
            for (i = 0; i < 3; i++) {
                f32 fw = fxSigned() * 20.0f, sd = fxSigned() * 9.0f, up = 2.0f + fxRand() * 6.0f;
                f32 out = 1.0f + fxRand() * 1.5f;
                fxAt(&p, px, py, pz, m, fw, sd, up);
                v.x = (m[0] * fw + m[8] * sd) / 20.0f * out;
                v.y = 1.5f + fxRand() * 2.0f + frame * 0.25f;
                v.z = (m[2] * fw + m[10] * sd) / 20.0f * out;
                fxFlame(p, v, 10.0f + fxRand() * 8.0f, 1.9f + fxRand() * 0.6f, life + 8);
            }
        }
        if (frame == 0) {
            /* hot fragments, arcing out and falling */
            for (i = 0; i < 6; i++) {
                f32 a = fxRand() * 6.2832f, sp = 4.0f + fxRand() * 4.0f;
                fxAt(&p, px, py, pz, m, fxSigned() * 10.0f, fxSigned() * 5.0f, 6.0f);
                v.x = __builtin_cosf(a) * sp;
                v.y = 5.0f + fxRand() * 4.0f;
                v.z = __builtin_sinf(a) * sp;
                fxFlameAcc(p, v, -0.45f, 1.6f + fxRand() * 1.2f, 0.0f, life + 18);
            }
        }
        if (frame >= 3 && frame % 2 == 0) {
            /* the core going dark, and the column it becomes */
            fxAt(&p, px, py, pz, m, fxSigned() * 12.0f, fxSigned() * 6.0f, 9.0f + frame * 0.3f);
            Vec3f_Set(&v, fxSigned() * 0.6f, 2.0f + fxRand() * 1.4f, fxSigned() * 0.6f);
            fxSmoke(p, v, 11.0f + fxRand() * 5.0f, 0.55f, life + 26);
        }
        if (frame >= 8) {
            /* the fireball's tail: flames dying back into the car */
            fxAt(&p, px, py, pz, m, fxSigned() * 16.0f, fxSigned() * 7.0f, 4.0f + fxRand() * 4.0f);
            Vec3f_Set(&v, fxSigned() * 0.5f, 1.8f + fxRand() * 1.2f, fxSigned() * 0.5f);
            fxFlame(p, v, 8.0f + 10.0f * k, 0.9f, life);
        }
        return 1;
    }
    {
        /* the fire: two places a frame, in turn; dying down after 40 s */
        static const f32 kPlace[8][5] = {
            /* forward, side, up, size, rise */
            { 15.0f, 0.0f, 4.0f, 7.5f, 1.9f },   /* the engine bay */
            { -1.0f, -7.0f, 7.0f, 4.5f, 2.2f },  /* the left window, licking up */
            { 13.0f, 4.0f, 3.5f, 6.0f, 1.7f },   /* the engine's side */
            { -20.0f, 0.0f, 4.0f, 4.0f, 1.6f },  /* the boot */
            { 16.0f, -4.0f, 3.5f, 6.0f, 1.7f },  /* the engine's other side */
            { -1.0f, 7.0f, 7.0f, 4.5f, 2.2f },   /* the right window */
            { 11.0f, 0.0f, 6.0f, 5.0f, 2.0f },   /* over the bonnet */
            { -6.0f, 0.0f, 9.0f, 3.5f, 2.4f },   /* through the roof */
        };
        f32 fade = age > 40.0f ? 1.0f - (age - 40.0f) / 20.0f : 1.0f; /* to half by 50 s */
        for (i = 0; i < 2; i++) {
            const f32 *pl = kPlace[(frame * 2 + i) % 8];
            f32 flick = 0.7f + fxRand() * 0.6f;
            if (fxRand() < 0.04f) {
                flick *= 1.8f; /* a flare */
            }
            fxAt(&p, px, py, pz, m, pl[0] + fxSigned() * 2.0f, pl[1] + fxSigned() * 1.5f, pl[2]);
            Vec3f_Set(&v, fxSigned() * 0.5f, pl[4] * (0.8f + fxRand() * 0.4f), fxSigned() * 0.5f);
            fxFlame(p, v, pl[3] * flick * fade, 0.22f, life - 4);
        }
        if (frame % 4 == 0) {
            /* the plume rolling off the roof */
            fxAt(&p, px, py, pz, m, fxSigned() * 5.0f, fxSigned() * 3.0f, 10.0f);
            Vec3f_Set(&v, fxSigned() * 0.4f, 2.3f + fxRand() * 0.8f, fxSigned() * 0.4f);
            fxSmoke(p, v, (7.0f + fxRand() * 2.5f) * (0.6f + 0.4f * fade), 0.42f, life + 14);
        }
    }
    return 1;
}

/* ---- Wrecked Metal: the wind at speed, in 3D ([speedfx] lines) ----
 * Above 55% of the top speed, specks of air are left hanging in the world ahead of the
 * player's car, off to its sides and above and below its line: the car drives into them,
 * so they stream past the camera at the edges of the screen (all of it, widescreen too)
 * the way air would, bigger and faster as they near, as fast as the car. Bluish, grey
 * and clear, faint (a third opaque at most), short-lived; 2-4 a frame on the smoke
 * pool, its reserve for the hood smoke kept. pos/m: Car_UpdateWreck's, the units the
 * N64's own wreck flames use (the hood 15 forward); m[0..2] forward, m[4..6] up, m[8..10]
 * the side. */
void WM_EmitWind(void *carPtr, void *posPtr, void *mtxPtr) {
    f32 WM_SpeedNorm(void);
    const Vec3f *pos = (const Vec3f *)posPtr;
    const f32 *m = (const f32 *)mtxPtr;
    WMParticleObj *o;
    f32 s, k;
    s32 n, i;
    if (!WM_Enabled() || !WM_GetTuning()->speedLines || D_801245A4 == NULL || D_800D31CC != 1 ||
        !wmIsHumanCar(carPtr)) {
        return;
    }
    s = WM_SpeedNorm();
    if (s < 0.55f) {
        return;
    }
    k = (s - 0.55f) / 0.45f;
    o = (WMParticleObj *)(D_801245A4 + 0xB0);
    n = 1 + (s32)(k * 1.6f); /* 1-2 a frame */
    for (i = 0; i < n && o->count < o->maxCount - WM_HOOD_RESERVE - 8; i++) {
        Vec3f p, v, acc;
        WMParticle *pt;
        /* far out to the sides for their distance ahead: they pass in the outer quarter of
           each side of the (widescreen) screen, never across the middle */
        f32 ahead = 70.0f + (f32)(wmRand() % 110);
        f32 side = (f32)((wmRand() & 1) ? 1 : -1) * (ahead * 0.55f + (f32)(wmRand() % 45));
        f32 up = (f32)(wmRand() % 40);
        u32 tone = wmRand() % 3;
        p.x = pos->x + m[0] * ahead + m[8] * side + m[4] * up;
        p.y = pos->y + m[1] * ahead + m[9] * side + m[5] * up;
        p.z = pos->z + m[2] * ahead + m[10] * side + m[6] * up;
        /* a little drift back along the car, as air past a moving car */
        v.x = -m[0] * (1.0f + 2.0f * k);
        v.y = 0.0f;
        v.z = -m[2] * (1.0f + 2.0f * k);
        Vec3f_Set(&acc, 0.0f, 0.0f, 0.0f);
        /* a small bright fleck that swells as it nears and fades as it goes: small, big, gone */
        s32 ma = 130 + (s32)(80.0f * k);
        pt = (WMParticle *)Particle_Spawn(o, p, v, acc, 0.8f, 0.10f + 0.06f * k, 20, (u8)(ma / 3), ma, 0, 0, 0);
        {
            static int nlog;
            if (nlog++ < 5 && getenv("DD64_WINDLOG") != NULL) {
                Port_Log("wind: s %.2f count %d/%d pt %p at %.0f %.0f %.0f (car %.0f %.0f %.0f)", (double)s, (int)o->count,
                         (int)o->maxCount, (void *)pt, (double)p.x, (double)p.y, (double)p.z, (double)pos->x, (double)pos->y,
                         (double)pos->z);
            }
        }
        if (pt != NULL) {
            if (tone == 0) { /* silver */
                pt->r = 205; pt->g = 212; pt->b = 225;
            } else if (tone == 1) { /* white, a hint of blue */
                pt->r = 238; pt->g = 244; pt->b = 255;
            } else { /* white */
                pt->r = 250; pt->g = 250; pt->b = 252;
            }
        }
    }
}
