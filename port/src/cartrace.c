/*
 * DD64_CARTRACE=1 (a debug switch, off by default): logs what changes a player's
 * car's motion sharply, stage by stage of the physics step (Car_PhysicsStep and
 * World_UpdateEffects), to find which one throws a car into the air.
 * A line is written when a stage changes the car's speed by more than 2 m/s, or
 * its spin by more than 1.5 rad/s, or leaves it rising faster than 3 m/s.
 * Reads the body the way the physics does (offsets as in src/auto/2F700.c).
 */
#include <SDL3/SDL.h>
#include <math.h>

#include "port.h"

typedef struct { f32 x, y, z; } TVec;

#define BODY_POS 0x290
#define BODY_VEL 0x29C
#define BODY_ANGVEL 0x2A8
#define BODY_UP 0x2C0 /* the second basis row, by.y = how upright */
#define CAR_MODEL 0x34
#define CAR_PLAYER 0x3C
#define CAR_MASS (0x1EC + 0xC)
#define MAX_LINES 3000

static int sOn = -1;
static int sLines;
static u32 sStep;
static struct { void *car; TVec vel, ang; } sLast[4];

static int traceOn(void) {
    if (sOn < 0) {
        const char *e = SDL_getenv("DD64_CARTRACE");
        sOn = e != NULL && SDL_atoi(e) != 0;
        if (sOn) {
            Port_Log("cartrace: on");
        }
    }
    return sOn;
}

static TVec rd(const void *car, u32 off) {
    TVec v;
    SDL_memcpy(&v, (const u8 *)car + off, sizeof(v));
    return v;
}

static f32 len(TVec a) {
    return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
}

static f32 dist(TVec a, TVec b) {
    TVec d = { a.x - b.x, a.y - b.y, a.z - b.z };
    return len(d);
}

void Port_CarTraceStep(f32 dt) {
    if (traceOn()) {
        sStep++;
        (void)dt;
    }
}

void Port_CarTrace(const char *stage, void *car, f32 dt) {
    int i, slot = -1;
    TVec vel, ang, pos, up;
    f32 dv, dw;

    if (!traceOn() || car == NULL || *(void **)((u8 *)car + CAR_PLAYER) == NULL) {
        return;
    }
    for (i = 0; i < 4; i++) {
        if (sLast[i].car == car) {
            slot = i;
            break;
        }
    }
    vel = rd(car, BODY_VEL);
    ang = rd(car, BODY_ANGVEL);
    if (slot < 0) {
        for (i = 0; i < 4 && sLast[i].car != NULL; i++) {
        }
        if (i == 4) {
            return;
        }
        slot = i;
        sLast[slot].car = car;
        sLast[slot].vel = vel;
        sLast[slot].ang = ang;
        Port_Log("cartrace: player car type %d, mass %.0f", *(s32 *)((u8 *)car + CAR_MODEL),
                 (double)*(f32 *)((u8 *)car + CAR_MASS));
    }
    dv = dist(vel, sLast[slot].vel);
    dw = dist(ang, sLast[slot].ang);
    if ((dv > 2.0f || dw > 1.5f || vel.y > 3.0f) && sLines < MAX_LINES) {
        pos = rd(car, BODY_POS);
        up = rd(car, BODY_UP);
        sLines++;
        Port_Log("cartrace: step %u dt %.4f %-9s dv %6.2f dw %5.2f | vel %6.2f %6.2f %6.2f ang %5.2f %5.2f %5.2f | y %.2f up.y %.2f",
                 sStep, (double)dt, stage, (double)dv, (double)dw, (double)vel.x, (double)vel.y, (double)vel.z,
                 (double)ang.x, (double)ang.y, (double)ang.z, (double)pos.y, (double)up.y);
    }
    sLast[slot].vel = vel;
    sLast[slot].ang = ang;
}
