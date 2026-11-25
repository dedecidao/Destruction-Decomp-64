/*
 * Wrecked Metal: each car's own record (CarStats, 0x2B0 bytes: the handling, engine,
 * gearing, mass, damage zones), read from its resource as CarObj_Create does, for
 * the car-select screen's stat bars.
 */
#include "common.h"
#include "race_cars.h"
#include "wm_mod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void Port_Log(const char *, ...);
void Res_Load(s32 id, void *dst);
extern s32 D_80091AB4[]; /* the cars' record resources, by car (0-23) */

#define WM_CARS 24
#define WM_CAR_WORDS (0x2B0 / 4)

static u32 sRec[WM_CARS][WM_CAR_WORDS];
static u8 sLoaded[WM_CARS];

/* The car's record, in host order (NULL for no car) */
const u32 *WM_CarRecord(int id) {
    if (id < 0 || id >= WM_CARS) {
        return NULL;
    }
    if (!sLoaded[id]) {
        /* loaded data must sit in the game's RAM to be converted as it is read */
        PORT_DMA_BUF(u32, buf, 0x8000);
        int i;
        Res_Load(D_80091AB4[id], buf);
        for (i = 0; i < WM_CAR_WORDS; i++) {
            sRec[id][i] = buf[i];
        }
        /* The record's model (word 1) is the car itself and its health (word 2) under 300:
           if they read byte-swapped (car 0's model reads 0 either way), the words came
           through unconverted (big-endian, as in the ROM; the Android build): swap them */
        if ((sRec[id][1] != (u32)id || sRec[id][2] > 300u) && __builtin_bswap32(sRec[id][1]) == (u32)id &&
            __builtin_bswap32(sRec[id][2]) <= 300u) {
            for (i = 0; i < WM_CAR_WORDS; i++) {
                sRec[id][i] = __builtin_bswap32(sRec[id][i]);
            }
            Port_Log("cars: car %d's record read big-endian, swapped", id);
        }
        sLoaded[id] = 1;
    }
    return sRec[id];
}

/* Whether a CarStats block reads sanely (host order): model 0-23, health 1-300, mass
   500-2000 kg, torque 1-2000. Anything else and the Wrecked Metal car changes keep off. */
int WM_CarStatsSane(const void *stats) {
    const u32 *w = (const u32 *)stats;
    f32 mass, torque;
    u32 m = w[1];
    s32 hp = (s32)w[2];
    u32 wm = w[0x78 / 4], wt = w[0x30 / 4]; /* word loads: converted where the game data needs it */
    memcpy(&mass, &wm, 4);
    memcpy(&torque, &wt, 4);
    return m < 24 && hp >= 1 && hp <= 300 && mass >= 500.0f && mass <= 2000.0f && torque >= 1.0f && torque <= 2000.0f;
}

/* DD64_DUMPCARS=file (testing): every car's record, word by word, as hex, int and float */
void WM_DumpCarRecords(void) {
    static int done;
    const char *path = getenv("DD64_DUMPCARS");
    FILE *f;
    int id, i;
    if (done || path == NULL) {
        return;
    }
    done = 1;
    f = fopen(path, "w");
    if (f == NULL) {
        return;
    }
    for (id = 0; id < WM_CARS; id++) {
        const u32 *r = WM_CarRecord(id);
        fprintf(f, "car %d res %d\n", id, (int)D_80091AB4[id]);
        for (i = 0; i < WM_CAR_WORDS; i++) {
            f32 v;
            memcpy(&v, &r[i], 4);
            fprintf(f, "  %03X %08X %d %g\n", i * 4, (unsigned)r[i], (int)r[i], (double)v);
        }
    }
    fclose(f);
    Port_Log("cars: dumped %d records to %s", WM_CARS, path);
}

/* DD64_GEARLOG (testing): the player's gearbox each time its gear changes and every
   15 frames: the physics gear and rpm (Car_SelectGear: Car+0xA58, +0xA54, which set the
   drive torque), the engine sound's gear and rpm (Car_UpdateEngineRpm: +0xBA4, +0xBA8)
   and the speed */
void WM_LogGear(void *car, f32 speed) {
    static int on = -1, n, lastGear = -1, lastSnd = -1;
    u8 *c = (u8 *)car;
    s32 gear, rpm, sndGear;
    f32 sndRpm;
    {
        /* DD64_AIHP=n (testing): every computer car starts at n health, once a race */
        static const char *hp = (const char *)-1;
        static void *done[128];
        static int ndone;
        int i;
        if (hp == (const char *)-1) {
            hp = getenv("DD64_AIHP");
        }
        if (hp != NULL && *(s32 *)(c + 0x30) != 0) {
            for (i = 0; i < ndone && done[i] != car; i++) {
            }
            if (i == ndone && ndone < 128) {
                done[ndone++] = car;
                *(s32 *)(c + 0x10) = atoi(hp);
            }
        }
    }
    if (on < 0) {
        const char *e = getenv("DD64_GEARLOG");
        on = e == NULL ? 0 : (e[0] >= '0' && e[0] <= '9' && atoi(e) > 1) ? atoi(e) : 1;
    }
    /* the player's car (Car_Create), or with DD64_GEARLOG=n (n > 1) the car of model n - 2 */
    if (!on || (on == 1 ? *(s32 *)(c + 0x30) != 0 : *(s32 *)(c + 0x34) != on - 2)) {
        return;
    }
    gear = *(s32 *)(c + 0xA58);
    rpm = *(s32 *)(c + 0xA54);
    sndGear = *(s32 *)(c + 0xBA4);
    sndRpm = *(f32 *)(c + 0xBA8);
    n++;
    if (gear != lastGear || sndGear != lastSnd || n % 15 == 0) {
        Port_Log("gear: f%d model %d gear %d rpm %d | sound gear %d rpm %.0f | %.1f km/h (%.1f mph)%s", n,
                 *(s32 *)(c + 0x34), gear, rpm, sndGear, (double)sndRpm, (double)(speed * 3.6f),
                 (double)(speed * 2.2369f), gear != lastGear ? " <- shift" : "");
        lastGear = gear;
        lastSnd = sndGear;
    }
}

/* ---- Wrecked Metal: wrecks and who may wreck a computer car ---- */

/* A computer car (Car+0x30 != 0) is kept on 1 health unless it is near the player:
   within [cars] wreck_near metres (100: half the 200 m inside which cars are fully
   simulated), or the player dealt the blow. Far off, it never wrecks. Play Fidelity: never. */
static int wmNearPlayer(void *car) {
    typedef struct { f32 x, y, z; } V3;
    V3 Car_GetPos(void *);
    extern u8 *gGame;
    u8 *race = gGame != NULL ? *(u8 **)(gGame + 0x1100) : NULL;
    s32 i, n;
    f32 r = (f32)WM_WreckNear();
    if (race == NULL || r <= 0.0f) {
        return 0;
    }
    n = *(s32 *)(race + 0x74 + RACE_CARS * 4);
    for (i = 0; i < n && i < RACE_CARS; i++) {
        void *c = ((void **)(race + 0x74))[i];
        if (c != NULL && *(s32 *)((u8 *)c + 0x30) == 0) {
            V3 a = Car_GetPos(car), b = Car_GetPos(c);
            f32 dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            if (dx * dx + dy * dy + dz * dz < r * r) {
                return 1;
            }
        }
    }
    return 0;
}

s32 WM_AiKeepAlive(void *victim, void *attacker) {
    if (!WM_Enabled() || victim == NULL || *(s32 *)((u8 *)victim + 0x30) == 0) {
        return 0;
    }
    if (getenv("DD64_WRECKANY") != NULL) {
        return 0; /* testing: any car may finish a computer car, as on the N64 */
    }
    if ((attacker == NULL || *(s32 *)((u8 *)attacker + 0x30) != 0) && !wmNearPlayer(victim)) {
        static int log = -1, nlog;
        if (log < 0) {
            log = getenv("DD64_WRECKLOG") != NULL;
        }
        if (log && nlog++ < 200) {
            Port_Log("wrecklog: car %p model %d kept on 1 (%s, far from the player)", victim, *(s32 *)((u8 *)victim + 0x34), attacker ? "computer car" : "wall or wreck");
        }
        return 1;
    }
    return 0;
}

/* A wreck can be pushed (no handbrake, Car_UpdateWreck) */
int WM_WreckPushable(void) {
    return WM_Enabled();
}

/* The wreck's fire (Car_EmitWreckParticles): the flames' size, 50 seconds from the wreck
   (Car+0x94 is the wreck's time + 6 s, Car_CheckWrecked), shrinking from 14 to 5; after
   that 0, and the N64's smoke takes over (*n moved past its flames). The first second is
   the N64's own burst. */
f32 WM_WreckFire(void *car, s32 *n) {
    extern u8 *gGame;
    f32 Time_ToSeconds(void *);
    f32 t;
    if (!WM_Enabled() || *n < 20) {
        return 0.0f;
    }
    {
        /* when this wreck began: its particle counter (Car+0x90) restarts at 1 on a wreck */
        static void *cars[128];
        static f32 start[128];
        f32 now = Time_ToSeconds(gGame + 0x10A8);
        int i, free = -1;
        for (i = 0; i < 128 && cars[i] != car; i++) {
            if (cars[i] == NULL && free < 0) {
                free = i;
            }
        }
        if (i == 128) {
            i = free >= 0 ? free : 0;
            cars[i] = car;
            start[i] = now - *n / 60.0f;
        } else if (*n == 20) {
            start[i] = now - *n / 60.0f; /* a new wreck (its counter starts again) */
        }
        t = now - start[i];
    }
    if (t < 0.0f || t >= 50.0f) {
        if (*n < 110) {
            *n = 110; /* the N64's smoke from here */
        }
        return 0.0f;
    }
    if (*n == 20 && getenv("DD64_WRECKLOG") != NULL) {
        Port_Log("wrecklog: car %p model %d burning (t %.1f)", car, *(s32 *)((u8 *)car + 0x34), (double)t);
    }
    return 14.0f - t * (9.0f / 50.0f);
}

/* DD64_WRECKLOG (testing): a wreck moving (pushed), now and then */
void WM_WreckLogMove(void *car, f32 speed) {
    static int on = -1;
    static u32 n;
    if (on < 0) {
        on = getenv("DD64_WRECKLOG") != NULL;
    }
    if (on && speed > 1.0f && (++n % 20) == 0) {
        Port_Log("wrecklog: wreck %p model %d moving %.1f m/s", car, *(s32 *)((u8 *)car + 0x34), (double)speed);
    }
}

/* ---- Wrecked Metal: a floor for every car's performance ([cars] perf_floor) ---- */

/* CarStats words (the record, in host order): torque at 0 rpm 0x2C, max torque 0x30,
   the gear ratios R and 1-5 0x40-0x54, the final ratio 0x58, the engine sound's gear
   speeds 0x60-0x74, mass 0x78 */
#define ST_F(r, o) (((f32 *)(r))[(o) / 4])

/* The car with the most torque for its weight (the quickest) and the one with the lowest
   top-gear ratio (5th x final: the fastest, its engine reaching its top revs latest) */
static void bestCars(s32 *quick, s32 *fast) {
    static s32 q = -1, f = -1;
    if (q < 0) {
        f32 bq = 0.0f, bf = 1e9f;
        s32 i;
        for (i = 0; i < 24; i++) {
            const u32 *r = WM_CarRecord(i);
            f32 a;
            if (!WM_CarStatsSane(r)) {
                continue;
            }
            a = ST_F(r, 0x30) / ST_F(r, 0x78);
            f32 g = ST_F(r, 0x54) * ST_F(r, 0x58);
            if (a > bq) {
                bq = a;
                q = i;
            }
            if (g > 0.0f && g < bf) {
                bf = g;
                f = i;
            }
        }
        Port_Log("cars: perf floor from car %d (quickest, %.3f torque/kg) and car %d (fastest, top gear %.2f)",
                 q + 1, (double)bq, f + 1, (double)bf);
    }
    *quick = q;
    *fast = f;
}

/* Raise the record `stats` (about to become a car, Car_Create) to the floor: its torque
   to the quickest car's for its own weight, and if its top gear is slower, the fastest
   car's whole gearbox (the ratios, the final ratio and the sound's gear speeds) */
void WM_CarApplyFloor(void *stats) {
    s32 q, f, o;
    const u32 *rq, *rf;
    f32 mass;
    if (stats == NULL || !WM_Enabled()) {
        return;
    }
    /* [cars] mass_car1: car 1 (model 0, the white "2") heavier or lighter, its springs with it */
    if (((const u32 *)stats)[1] == 0 && WM_MassCar1Pct() != 100 && WM_CarStatsSane(stats)) {
        const u32 *own = WM_CarRecord(0);
        /* only a record still at its own weight: a race sets its cars up twice, from a
           freshly loaded record each time, so the check (not the pointer) keeps it once */
        if (own != NULL && ST_F(stats, 0x78) == ST_F(own, 0x78)) {
            f32 k = WM_MassCar1Pct() / 100.0f;
            ST_F(stats, 0x78) *= k;
            ST_F(stats, 0xB0) *= k;
        }
    }
    if (!WM_PerfFloor()) {
        return;
    }
    bestCars(&q, &f);
    if (q < 0 || f < 0) {
        return;
    }
    rq = WM_CarRecord(q);
    rf = WM_CarRecord(f);
    if (!WM_CarStatsSane(stats) || !WM_CarStatsSane(rq) || !WM_CarStatsSane(rf)) {
        static int warned;
        if (!warned++) {
            Port_Log("cars: perf floor off, a car record reads wrong (model %u health %d)", ((const u32 *)stats)[1],
                     (int)((const s32 *)stats)[2]);
        }
        return;
    }
    mass = ST_F(stats, 0x78);
    if (ST_F(stats, 0x2C) < ST_F(rq, 0x2C) / ST_F(rq, 0x78) * mass) {
        ST_F(stats, 0x2C) = ST_F(rq, 0x2C) / ST_F(rq, 0x78) * mass;
    }
    if (ST_F(stats, 0x30) < ST_F(rq, 0x30) / ST_F(rq, 0x78) * mass) {
        ST_F(stats, 0x30) = ST_F(rq, 0x30) / ST_F(rq, 0x78) * mass;
    }
    if (ST_F(stats, 0x54) * ST_F(stats, 0x58) > ST_F(rf, 0x54) * ST_F(rf, 0x58)) {
        for (o = 0x40; o <= 0x58; o += 4) {
            ST_F(stats, o) = ST_F(rf, o);
        }
        for (o = 0x60; o <= 0x74; o += 4) {
            ST_F(stats, o) = ST_F(rf, o);
        }
    }
    {
        void WM_FloorLog(void *);
        WM_FloorLog(stats);
    }
}

/* DD64_FLOORLOG (testing): a car's engine and gearbox after the floor */
void WM_FloorLog(void *stats) {
    if (getenv("DD64_FLOORLOG") != NULL) {
        Port_Log("floor: model %d torque %.0f/%.0f mass %.0f gears %.1f %.1f %.1f %.1f %.1f final %.2f sound top %.0f",
                 (int)((s32 *)stats)[1], (double)ST_F(stats, 0x2C), (double)ST_F(stats, 0x30), (double)ST_F(stats, 0x78),
                 (double)ST_F(stats, 0x44), (double)ST_F(stats, 0x48), (double)ST_F(stats, 0x4C), (double)ST_F(stats, 0x50),
                 (double)ST_F(stats, 0x54), (double)ST_F(stats, 0x58), (double)ST_F(stats, 0x74));
    }
}

/* The car-select bar `bar` (0 speed, 1 acceleration, 2 handling, 3 durability) as shown */
s32 WM_CarBar(s32 bar, s32 value) {
    if (bar < 2 && WM_PerfFloor()) {
        value = value < 19 ? 19 : value; /* every car at least the best */
    }
    if (bar < 2) {
        value = (value * WM_BarsPct() + 50) / 100;
        if (value < 1) {
            value = 1;
        }
    }
    return value;
}

/* How much faster a car's top gear is than its own record's (the floor's gearbox): the
   computer cars' track speed limits scale with it, so they keep their pace against the
   faster field (Ai_Steer, CarAI_SteerToTarget through WM_AiSpeedScale). 1 without it. */
f32 WM_CarPaceScale(void *car) {
    const u32 *r;
    const u8 *st;
    f32 orig, cur;
    s32 model;
    if (!WM_PerfFloor() || car == NULL) {
        return 1.0f;
    }
    model = *(s32 *)((u8 *)car + 0x34);
    r = WM_CarRecord(model);
    if (r == NULL || !WM_CarStatsSane(r)) {
        return 1.0f;
    }
    st = (const u8 *)car + 0xBEC; /* the car's own copy of its stats */
    orig = ST_F(r, 0x54) * ST_F(r, 0x58);
    cur = ST_F(st, 0x54) * ST_F(st, 0x58);
    if (cur < 0.1f || orig < cur) {
        return 1.0f;
    }
    orig /= cur;
    return orig > 1.5f ? 1.5f : orig;
}

/* The player's car harder to spin ([cars] player_yaw): its yaw inertia (the body's
   inertia about y, Car+0x1EC+0x18, and its inverse +0x24) scaled after RigidBody_Init */
void WM_PlayerStability(void *car) {
    u8 *b;
    f32 k;
    if (car == NULL || *(s32 *)((u8 *)car + 0x30) != 0 || WM_PlayerYawPct() == 100) {
        return;
    }
    b = (u8 *)car + 0x1EC;
    k = WM_PlayerYawPct() / 100.0f;
    *(f32 *)(b + 0x18) *= k;
    *(f32 *)(b + 0x24) /= k;
}

/* ---- Wrecked Metal: a head-on crash at speed wrecks the car ([cars] wall_wreck_mph) ---- */

/* each car's speed on its last physics frame (the hit has already slowed it by the time
   the damage is dealt) */
#define SPEED_SLOTS 128
static void *sSpeedCar[SPEED_SLOTS];
static f32 sSpeed[SPEED_SLOTS];

static s32 speedSlot(void *car) {
    u32 h = ((u32)(uintptr_t)car >> 4) * 2654435761u >> 25; /* 0-127 */
    s32 i;
    for (i = 0; i < SPEED_SLOTS; i++, h = (h + 1) & (SPEED_SLOTS - 1)) {
        if (sSpeedCar[h] == car || sSpeedCar[h] == NULL) {
            sSpeedCar[h] = car;
            return (s32)h;
        }
    }
    return (s32)(h & (SPEED_SLOTS - 1));
}

void WM_NoteSpeed(void *car) {
    f32 Car_GetSpeed(void *);
    f32 speed = Car_GetSpeed(car);
    s32 s = speedSlot(car);
    sSpeedCar[s] = car;
    sSpeed[s] = speed;
}

/* Car_ApplyDamage (a wall, a wreck): a hit on the front (zone 0, or the front corners
   1 and 4) at wall_wreck_mph or more wrecks the car there and then: no health left and
   the wreck flag (Car+0xE, as a finishing blow sets it). A computer car only within
   wreck_near of the player. Returns 1 when it did. */
s32 WM_HeadOnWreck(void *car, s32 zone) {
    f32 mph;
    s32 lim = WM_WallWreckMph();
    if (lim <= 0 || car == NULL || (zone != 0 && zone != 1 && zone != 4) || ((u8 *)car)[0xBE0]) { /* (0xBE0: no damage) */
        return 0;
    }
    mph = sSpeed[speedSlot(car)] * 2.2369f;
    if (mph < (f32)lim) {
        return 0;
    }
    if (*(s32 *)((u8 *)car + 0x30) != 0 && WM_AiKeepAlive(car, NULL)) {
        return 0;
    }
    *(s32 *)((u8 *)car + 0x10) = 0;
    ((u8 *)car)[0xE] = 1;
    Port_Log("cars: head-on at %.0f mph, car %p (model %d) wrecked", (double)mph, car, *(s32 *)((u8 *)car + 0x34));
    return 1;
}
