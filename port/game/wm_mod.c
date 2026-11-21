/*
 * Game-side Wrecked Metal mod hooks (built in the `game` target with
 * -fsanitize=kernel-address so N64 RAM structs are accessed seamlessly).
 * Every feature here stays inactive when WM_Enabled() is 0 (Play Fidelity).
 */
#include "common.h"
#include "wm_mod.h"

typedef struct { f32 x, y, z; } Vec3f;
typedef struct { f32 m[4][4]; } Mtx;
typedef struct { u8 pad[0x10]; f32 x10; } WMScaleInfo;
typedef struct { u8 pad[0xC]; WMScaleInfo *xC; } WMTrackInfo;
typedef struct { u8 pad[0x70]; WMTrackInfo *x70; } WMRace;
typedef struct { u8 pad[0x1100]; WMRace *x1100; } WMGame;

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
    u8 pad14[0x30 - 0x14];
    s32 isAi;
    u8 pad34[0x364 - 0x34];
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

static void emitWheelSegment(WMParticleObj *obj, const Vec3f *from, const Vec3f *to,
                             const SurfaceDustStyle *st, f32 intensity, f32 stepDist,
                             f32 scaleMul, f32 lifeMul, f32 alphaMul, int maxSteps) {
    Vec3f diff, step, p, vel, acc;
    f32 len = 0.0f;
    f32 scale, growth;
    int life, alphaStep, maxAlpha, steps, emitted = 0;

    if (obj->count >= obj->maxCount) {
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
    for (emitted = 0; emitted < steps && obj->count < obj->maxCount; emitted++) {
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

int WM_EmitHoodSmoke(void *carPtr, void *posPtr, void *mtxPtr) {
    WMCar *car = (WMCar *)carPtr;
    const Vec3f *pos = (const Vec3f *)posPtr;
    f32 (*m)[4] = (f32 (*)[4])mtxPtr;
    WMParticleObj *smokeObj;
    WMParticle *pt;
    Vec3f p, v, acc;
    f32 spd, spdNorm, r1, r2, backPush, latVel;
    s32 life;

    if (!WM_Enabled()) {
        return 0;
    }
    if (D_8012455C || D_801245A4 == NULL) {
        return 1;
    }

    smokeObj = (WMParticleObj *)(D_801245A4 + 0xB0);
    if (smokeObj->count >= smokeObj->maxCount) {
        return 1;
    }

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

    if (car->health > 0) {
        /* Stage 1 white hood smoke: flattens nearly horizontal at speed & spreads left/right */
        r2 = randRange(10.5f);
        p.x = pos->x + m[0][0] * (15.0f - r1) - m[2][0] * r2;
        p.y = pos->y + m[0][1] * (15.0f - r1) - m[2][1] * r2 + 0.4f;
        p.z = pos->z + m[0][2] * (15.0f - r1) - m[2][2] * r2;

        backPush = spdNorm * 1.45f;
        latVel = randRange(2.15f) * (0.70f + 0.50f * spdNorm);
        v.x = -m[0][0] * backPush + m[2][0] * latVel;
        v.y = 2.85f * (1.0f - 0.82f * spdNorm);
        v.z = -m[0][2] * backPush + m[2][2] * latVel;
        Vec3f_Set(&acc, 0.0f, -0.09f * spdNorm, 0.0f);

        pt = (WMParticle *)Particle_Spawn(smokeObj, p, v, acc, 6.6f, 0.34f,
                                          (u16)(life + 1), 45, 185, 0, 0, 0);
        if (pt != NULL) {
            pt->r = 242;
            pt->g = 247;
            pt->b = 255;
        }
    } else {
        /* Stage 2 black smoke (pre-explosion): gentler tilt & lateral dispersion */
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
