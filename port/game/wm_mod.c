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
    u8 padE[0x30 - 0xE];
    s32 isAi;
    u8 pad34[0x364 - 0x34];
    WMWheel wheels[4];
    u8 padA54[0xA60 - 0xA54];
    f32 allWheelsAirTime;
    u8 padA64[0xB70 - 0xA64];
    void *skidMgr[4];
} WMCar;

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
extern WMGame *gGame;
extern u8 *D_801245A4;

void Vec3f_Set(Vec3f *, f32, f32, f32);
void Vec3f_Sub(Vec3f *, Vec3f *, Vec3f *);
void Vec3f_Add(Vec3f *, Vec3f *, Vec3f *);
void Vec3f_Length(f32 *, Vec3f *);
void Vec3f_SetLength(Vec3f *, Vec3f *, f32);
void Mtx_FromRotPos(Mtx *, void *, void *, s32, s32);
void *Particle_Spawn(void *, Vec3f, Vec3f, Vec3f, f32, f32, u16, u8, s32, s32, s32, s32);
void Skid_Start(void *);
void Skid_Stop(void *);
s32 rand(void);

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
                             const SurfaceDustStyle *st, f32 intensity, f32 stepDist) {
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

    scale = st->scale * (0.72f + 0.38f * intensity);
    growth = st->growth * (0.75f + 0.35f * intensity);
    life = (int)((f32)st->life * (0.65f + 0.40f * intensity));
    if (life < 10) {
        life = 10;
    } else if (life > 250) {
        life = 250;
    }
    maxAlpha = (int)((f32)st->maxAlpha * (0.55f + 0.45f * intensity));
    if (maxAlpha < 40) {
        maxAlpha = 40;
    } else if (maxAlpha > 255) {
        maxAlpha = 255;
    }
    alphaStep = maxAlpha / (life / 3 + 2);
    if (alphaStep < 8) {
        alphaStep = 8;
    }

    steps = (int)(len / stepDist) + 1;
    if (steps > 4) {
        steps = 4; /* keep per-wheel burst bounded on fast spikes */
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

    if (!WM_Enabled()) {
        return 0;
    }
    if (D_8012455C || car->wrecked || D_801245A4 == NULL) {
        return 1;
    }

    cfg = *WM_GetTuning();

    if (car->allWheelsAirTime <= 0.0f) {
        if (!(D_800958A8 == 3 && car->isAi != 0)) {
            smokeObj = (WMParticleObj *)(D_801245A4 + 0xB0);
            trackScale = gGame->x1100->x70->xC->x10;

            for (i = 0; i < 4; i++) {
                WMWheel *w = &car->wheels[i];
                f32 intensity;
                Mtx m;
                Vec3f p1, p2;
                SurfaceDustStyle st;

                if (!w->isSkidding || w->groundNormal.z >= 1.0f) {
                    continue;
                }
                if (i >= 2) {
                    if (w->skidMode == 2 && !cfg.emitOnDrift) {
                        continue;
                    }
                } else {
                    if (!cfg.emitFrontWheels) {
                        continue;
                    }
                    if (w->skidMode == 2 && w->skidIntensity < cfg.frontSlipThreshold) {
                        continue;
                    }
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
                if (i < 2) {
                    intensity *= 0.65f;
                }

                styleForSurface(w->surfaceType, w->skidMode, &cfg, &st);
                emitWheelSegment(smokeObj, &p1, &p2, &st, intensity, cfg.stepDist);
            }
        }

        if (car->skidMgr[0] != NULL) {
            for (i = 0; i < 4; i++) {
                if (car->wheels[i].isSkidding) {
                    Skid_Start(car->skidMgr[i]);
                } else {
                    Skid_Stop(car->skidMgr[i]);
                }
            }
        }
    } else if (car->skidMgr[0] != NULL) {
        for (i = 0; i < 4; i++) {
            Skid_Stop(car->skidMgr[i]);
        }
    }

    return 1;
}
