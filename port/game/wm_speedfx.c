/*
 * Wrecked Metal: the feel of speed on the chase cameras ([speedfx] in wm_tuning.ini),
 * one player only, at a handful of float operations a frame:
 *
 *   - FOV: the field of view widens with speed, smoothstepped, up to fov_pct % wider
 *     at the top speed, so walls and road stream past the edges (Camera_BuildProjection);
 *   - the spring: the camera hangs back as the car pulls away and closes in as it brakes,
 *     a damped spring on the acceleration (not the speed), and sits lower at speed
 *     (Camera_Update's chase distance and height);
 *   - the shake: from 70% of the top speed a tiny high-frequency tremor of the eye and
 *     the point looked at, twice as strong off the tarmac, and a short kick on a crash
 *     (a sudden loss of speed).
 */
#include "common.h"
#include "wm_mod.h"
#include <math.h>

f32 Car_GetSpeed(void *);

#define TOP_SPEED 70.0f /* m/s, about the floor's 250 km/h */

static f32 sSpeed, sPull, sAccel, sFovMul = 1.0f, sShake, sKick, sTime, sLow;
static s32 sOffroad;

static f32 clamp01(f32 v) {
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

static f32 smooth(f32 x) {
    x = clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}

/* Camera_Update, the chase cameras (modes 1 and 2), once a frame: the car's speed, the
   spring, and the chase distance (*dist) and height (*height) to use */
void WM_SpeedCam(void *car, f32 dt, f32 *dist, f32 *height) {
    extern s8 D_800D31CC; /* players */
    f32 v, s, a, k;
    if (!WM_SpeedFx() || car == NULL || D_800D31CC != 1 || dt <= 0.0f || dt > 0.25f) {
        sFovMul = 1.0f;
        sShake = 0.0f;
        return;
    }
    v = Car_GetSpeed(car);
    if (v < 0.0f) {
        v = -v;
    }
    a = (v - sSpeed) / dt;
    if (v - sSpeed < -6.0f) {
        sKick = 1.0f; /* a crash: the speed gone in a frame */
    }
    sSpeed = v;
    sAccel += (a - sAccel) * (1.0f - expf(-dt * 6.0f)); /* the acceleration, filtered */
    {
        /* the spring: back up to +30% of the distance pulling away, in to -18% braking */
        f32 want = sAccel / 30.0f;
        want = want > 0.30f ? 0.30f : want < -0.18f ? -0.18f : want;
        sPull += (want - sPull) * (1.0f - expf(-dt * 3.0f));
    }
    s = v / TOP_SPEED;
    k = smooth(s);
    sLow += (k - sLow) * (1.0f - expf(-dt * 2.0f));
    *dist *= 1.0f + sPull;
    *height *= 1.0f - 0.20f * sLow; /* lower at speed: the road streams faster */
    sFovMul = 1.0f + WM_SpeedFovPct() / 100.0f * k;
    /* the tremor: from 70% of the top speed, doubled off the tarmac (the wheel's surface,
       Car+0x50C: as the pad's rumble reads it) */
    {
        u32 surf = *(u32 *)((u8 *)car + 0x364 + 0x1A8);
        sOffroad = surf == 2 || surf == 4 || surf == 6 || surf == 7 || surf == 8;
    }
    sShake = clamp01((s - 0.70f) / 0.30f) * (sOffroad ? 2.0f : 1.0f);
    sKick *= expf(-dt * 7.0f);
    sTime += dt;
    {
        void WM_WindSet(float); /* the wind (port/src/wm_wind.c) */
        WM_WindSet(clamp01(s));
    }
}

/* Camera_BuildProjection: the field of view, and the shake added to the eye and target */
f32 WM_SpeedFov(f32 fovy) {
    extern s8 D_800D31CC;
    if (!WM_SpeedFx() || D_800D31CC != 1) {
        return fovy;
    }
    return fovy * sFovMul;
}

void WM_SpeedShake(f32 *eye, f32 *at) {
    extern s8 D_800D31CC;
    f32 amp, x, y;
    if (!WM_SpeedFx() || D_800D31CC != 1) {
        return;
    }
    amp = (0.020f * sShake + 0.12f * sKick) * WM_SpeedShakePct() / 100.0f;
    if (amp <= 0.0001f) {
        return;
    }
    /* two incommensurate high frequencies: nervous, not a wobble */
    x = sinf(sTime * 61.0f) * 0.6f + sinf(sTime * 97.0f) * 0.4f;
    y = cosf(sTime * 73.0f) * 0.6f + sinf(sTime * 131.0f) * 0.4f;
    eye[0] += x * amp;
    eye[1] += y * amp;
    at[0] += x * amp * 0.5f;
    at[1] += y * amp * 0.5f;
}

/* how fast, 0-1 of the top speed (the speed lines, the wind) */
f32 WM_SpeedNorm(void) {
    return clamp01(sSpeed / TOP_SPEED);
}
