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

#define CAR_RPM 0xA54  /* s32, Car_SelectGear */
#define CAR_GEAR 0xA58 /* s32 */
#define MAX_DETAIL 1500
static int sDetail;

static int isPlayer(void *car) {
    return car != NULL && *(void **)((u8 *)car + CAR_PLAYER) != NULL;
}

/* One wheel's tyre force (Wheel_ComputeTireForce), when it is large for the car's mass */
void Port_CarTraceTire(void *car, s32 idx, s32 branch, f32 drive, f32 fl, f32 ft, f32 lon, f32 lat, f32 load,
                       f32 spin, void *res) {
    static const char *const kBranch[] = { "drive-slip", "drive-grip", "free" };
    f32 mass;
    TVec r;

    if (!traceOn() || !isPlayer(car) || sDetail >= MAX_DETAIL) {
        return;
    }
    mass = *(f32 *)((u8 *)car + CAR_MASS);
    SDL_memcpy(&r, res, sizeof(r));
    if (len(r) > mass * 15.0f) {
        sDetail++;
        Port_Log("cartrace: step %u tyre %d %-10s |F|/m %7.1f drive %8.1f fl %8.1f ft %8.1f lon %6.2f lat %6.2f load %7.1f spin %7.2f rpm %d gear %d",
                 sStep, (int)idx, kBranch[branch < 0 || branch > 2 ? 2 : branch], (double)(len(r) / mass), (double)drive,
                 (double)fl, (double)ft, (double)lon, (double)lat, (double)load, (double)spin,
                 *(s32 *)((u8 *)car + CAR_RPM), *(s32 *)((u8 *)car + CAR_GEAR));
    }
}

/* The forces summed in one of Car_UpdatePhysics's three substeps, when large */
void Port_CarTraceForces(void *car, s32 sub, void *wheel, void *susp, void *wheelTq, void *suspTq, f32 h) {
    f32 mass;
    TVec fw, fs, tw, ts;

    if (!traceOn() || !isPlayer(car) || sDetail >= MAX_DETAIL) {
        return;
    }
    mass = *(f32 *)((u8 *)car + CAR_MASS);
    SDL_memcpy(&fw, wheel, sizeof(fw));
    SDL_memcpy(&fs, susp, sizeof(fs));
    SDL_memcpy(&tw, wheelTq, sizeof(tw));
    SDL_memcpy(&ts, suspTq, sizeof(ts));
    if (len(fw) > mass * 60.0f || len(fs) > mass * 60.0f || len(tw) > mass * 40.0f || len(ts) > mass * 40.0f) {
        sDetail++;
        Port_Log("cartrace: step %u sub %d h %.4f wheelF/m %6.1f %6.1f %6.1f suspF/m %6.1f %6.1f %6.1f wheelTq/m %6.1f %6.1f %6.1f suspTq/m %6.1f %6.1f %6.1f",
                 sStep, (int)sub, (double)h, (double)(fw.x / mass), (double)(fw.y / mass), (double)(fw.z / mass),
                 (double)(fs.x / mass), (double)(fs.y / mass), (double)(fs.z / mass), (double)(tw.x / mass),
                 (double)(tw.y / mass), (double)(tw.z / mass), (double)(ts.x / mass), (double)(ts.y / mass),
                 (double)(ts.z / mass));
    }
}
