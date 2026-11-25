/*
 * Wrecked Metal: the race director ([ai] in wm_tuning.ini). In the pack race and head-on
 * modes (Race_PlaceCarsOnGrid places those starts) it keeps the race close without
 * rubber-banding anything the player can see:
 *
 *   - within 120 m of the player (80 m in the pack race) a car drives as the N64 has it;
 *   - further back, a car going the player's way is allowed past the track's speed limits,
 *     up to +15% while it is driven by physics and +30% (+35% pack) once it is far enough
 *     to be moved along the track (the kinematic mode, Car+0xBAC);
 *   - further ahead, a leader's limits and engine ease off a little, to 94% and 92%;
 *   - cars coming the other way (head-on) keep a steady 105%, and close to the player
 *     their driver turns to its most tenacious and searching;
 *   - the difficulty (D_800958B0) sets a base pace: 0.98, 1.02, 1.06, 1.10 (the pack
 *     race: 1.04, 1.08, 1.12, 1.16, its own harder rules below, and its computer cars
 *     a 12% longer top gear, WM_AiPackCar);
 *   - with [cars] perf_floor every computer car's limits rise with its faster gearbox
 *     (WM_CarPaceScale), in every mode, the classic one too.
 *
 * Ai_Steer (5B14.c) and CarAI_SteerToTarget (36FC4.c) scale the node's speed limit by
 * WM_AiSpeedScale, Port_CarDrive (pedals.c) the engine by WM_AiThrottleScale. The state
 * is updated about six times a second in one pass over the cars (O(n), no allocation, one
 * square root a car); between passes a car's scale is a table lookup. Play Fidelity and
 * the classic mode never get here (the scales stay 1).
 */
#include "common.h"
#include "race_cars.h"
#include "wm_mod.h"
#include <stdlib.h>

void Port_Log(const char *, ...);
u64 Time_Get(void *);

typedef struct { f32 x, y, z; } WMVec3;
WMVec3 Car_GetPos(void *);

extern u8 *gGame;          /* gGame+0x1100 the race, +0x10A8 the race clock */
extern u32 D_800958B0;     /* the difficulty, 0-3 */

/* The race: the nodes' speed limits at +0x34, how many at +0x38, the cars at +0x74 */
#define RACE(g) (*(u8 **)((g) + 0x1100))
#define RACE_NSEGS(r) (*(s32 *)((r) + 0x38))
#define RACE_CAR(r, i) (((u8 **)((r) + 0x74))[i])
#define RACE_NCARS(r) (*(s32 *)((r) + 0x74 + RACE_CARS * 4))

/* Car fields: health +0x10, node +0x28, computer +0x30, AiDriver +0x38, direction +0x58
   (+1 up the nodes, -1 down), lap +0x88, kinematic +0xBAC */
#define CAR_S32(c, o) (*(s32 *)((u8 *)(c) + (o)))
/* AiDriver: defence +0x3C (0-99), baseTenacity +0x50 and baseSearch +0x58 (0-9) */
#define AI_S32(c, o) (*(s32 *)(*(u8 **)((u8 *)(c) + 0x38) + (o)))

#define NEAR_M 120.0f
#define NEAR_PACK_M 80.0f
#define RAMP_M 600.0f
#define HEADON_NEAR_M 80.0f

typedef struct {
    void *car;
    f32 speed, throttle;
    s32 tenacity, search; /* the driver's own, while the director has raised them */
    u8 raised, personal;
} WMDirCar;

static WMDirCar sCars[RACE_CARS];
static s32 sCount;
static void *sRace, *sPlayer;
static u64 sLast;
static s32 sMode = -1;

/* the pointer -> slot table, so a car's lookup is not a search */
#define HASH_SIZE 256
static s16 sHash[HASH_SIZE];

static u32 hashOf(void *car) {
    return ((u32)(uintptr_t)car >> 4) * 2654435761u >> 24;
}

static WMDirCar *find(void *car) {
    u32 h = hashOf(car) & (HASH_SIZE - 1);
    s32 i;
    for (i = 0; i < HASH_SIZE; i++, h = (h + 1) & (HASH_SIZE - 1)) {
        s32 s = sHash[h];
        if (s < 0) {
            return NULL;
        }
        if (sCars[s].car == car) {
            return &sCars[s];
        }
    }
    return NULL;
}

/* A new race (Race_PlaceCarsOnGrid): forget the last one's cars */
void WM_AiRaceStart(void) {
    s32 i;
    for (i = 0; i < HASH_SIZE; i++) {
        sHash[i] = -1;
    }
    sCount = 0;
    sRace = NULL;
    sPlayer = NULL;
    sLast = 0;
    sMode = WM_AiMode();
}

static f32 clampf(f32 v, f32 lo, f32 hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* How far round the lap a car is, in nodes, the way it drives (0 to ns-1) */
static s32 progress(void *car, s32 ns) {
    s32 seg = CAR_S32(car, 0x28);
    if (CAR_S32(car, 0x58) < 0) {
        seg = (ns - seg) % ns;
    }
    return seg;
}

/* How many nodes car `c` is ahead of the player (behind: negative), the short way round:
   the lap counter (Car+0x88) does not follow a race track's laps, so a car more than half
   a lap ahead counts as behind (it is, the lap after) */
static s32 ahead(s32 pc, s32 pp, s32 ns) {
    s32 d = (pc - pp) % ns;
    if (d < 0) {
        d += ns;
    }
    return d >= ns / 2 ? d - ns : d;
}

/* The pack race's drivers: 30% hunters, 50% as they are, 20% defensive */
static void personalize(void *car, s32 i) {
    s32 r = i % 10;
    if (r < 3) {
        AI_S32(car, 0x50) = AI_S32(car, 0x50) + 3 > 9 ? 9 : AI_S32(car, 0x50) + 3;
        AI_S32(car, 0x3C) = AI_S32(car, 0x3C) - 20 < 0 ? 0 : AI_S32(car, 0x3C) - 20;
    } else if (r >= 8) {
        AI_S32(car, 0x50) = AI_S32(car, 0x50) - 2 < 0 ? 0 : AI_S32(car, 0x50) - 2;
        AI_S32(car, 0x3C) = AI_S32(car, 0x3C) + 20 > 99 ? 99 : AI_S32(car, 0x3C) + 20;
    }
}

static void update(void) {
    static const f32 kBase[4] = { 0.98f, 1.02f, 1.06f, 1.10f };
    u8 *race = RACE(gGame);
    s32 n, ns, i, pdir, pprog;
    void *player = NULL;
    WMVec3 pp;
    f32 base, nearM;

    if (race == NULL || RACE_NSEGS(race) <= 0) {
        return;
    }
    n = RACE_NCARS(race);
    if (n > RACE_CARS) {
        n = RACE_CARS;
    }
    ns = RACE_NSEGS(race);
    for (i = 0; i < n; i++) {
        void *c = RACE_CAR(race, i);
        if (c != NULL && CAR_S32(c, 0x30) == 0) {
            player = c;
            break;
        }
    }
    if (race != sRace || player != sPlayer || n != sCount) {
        /* the cars this race: rebuild the table */
        for (i = 0; i < HASH_SIZE; i++) {
            sHash[i] = -1;
        }
        for (i = 0; i < n; i++) {
            u32 h;
            sCars[i].car = RACE_CAR(race, i);
            sCars[i].speed = sCars[i].throttle = 1.0f;
            sCars[i].raised = sCars[i].personal = 0;
            if (sCars[i].car == NULL) {
                continue;
            }
            h = hashOf(sCars[i].car) & (HASH_SIZE - 1);
            while (sHash[h] >= 0) {
                h = (h + 1) & (HASH_SIZE - 1);
            }
            sHash[h] = (s16)i;
        }
        sRace = race;
        sPlayer = player;
        sCount = n;
    }
    if (player == NULL) {
        return;
    }
    pp = Car_GetPos(player);
    pdir = CAR_S32(player, 0x58);
    pprog = progress(player, ns);
    base = kBase[D_800958B0 & 3];
    if (sMode == WM_AI_PACK_RACE) {
        static const f32 kPack[4] = { 1.04f, 1.08f, 1.12f, 1.16f }; /* the pack race: a harder field */
        base = kPack[D_800958B0 & 3];
    }
    nearM = sMode == WM_AI_PACK_RACE ? NEAR_PACK_M : NEAR_M;

    for (i = 0; i < n; i++) {
        WMDirCar *d = &sCars[i];
        void *c = d->car;
        WMVec3 p;
        f32 dx, dy, dz, d2, speed = 1.0f, throttle = 1.0f;

        if (c == NULL || c == player || CAR_S32(c, 0x30) == 0 || CAR_S32(c, 0x38) == 0) {
            continue;
        }
        if (sMode == WM_AI_PACK_RACE && !d->personal) {
            personalize(c, i);
        }
        d->personal = 1;
        p = Car_GetPos(c);
        dx = p.x - pp.x, dy = p.y - pp.y, dz = p.z - pp.z;
        d2 = dx * dx + dy * dy + dz * dz;

        if (CAR_S32(c, 0x58) != pdir) {
            /* coming the other way: a steady pace, no catching up */
            speed = sMode == WM_AI_HEAD_ON ? 1.05f : 1.0f;
            if (sMode == WM_AI_HEAD_ON) {
                s32 close = d2 < HEADON_NEAR_M * HEADON_NEAR_M;
                if (close && !d->raised) {
                    d->tenacity = AI_S32(c, 0x50);
                    d->search = AI_S32(c, 0x58);
                    AI_S32(c, 0x50) = 9;
                    AI_S32(c, 0x58) = 9;
                    d->raised = 1;
                } else if (!close && d->raised) {
                    AI_S32(c, 0x50) = d->tenacity;
                    AI_S32(c, 0x58) = d->search;
                    d->raised = 0;
                }
            }
        } else if (sMode == WM_AI_PACK_RACE) {
            /* The pack race is a fight: nobody eases off ahead of the player, and anyone
               behind comes at them hard. Close (on screen): +12% limits and +25% engine
               to pass; further back while still driven by physics: +25% / +35%; once off
               screen and moved along the track: double pace, all but a teleport back to the
               pack. */
            s32 behind = ahead(progress(c, ns), pprog, ns) < 0;
            if (!behind) {
                speed = 1.0f;
                throttle = 1.05f;
            } else if (((u8 *)c)[0xBAC]) {
                speed = 2.0f;
                throttle = 1.35f;
            } else if (d2 < nearM * nearM) {
                speed = 1.12f;
                throttle = 1.25f;
            } else {
                speed = 1.25f;
                throttle = 1.35f;
            }
        } else if (d2 >= nearM * nearM) {
            f32 m = __builtin_sqrtf(d2);
            f32 t = clampf((m - nearM) / RAMP_M, 0.0f, 1.0f);
            if (ahead(progress(c, ns), pprog, ns) < 0) {
                /* behind: catch up, most once it is off screen (moved along the track) */
                f32 most = ((u8 *)c)[0xBAC] ? (sMode == WM_AI_PACK_RACE ? 0.35f : 0.30f) : 0.15f;
                speed = 1.0f + t * most;
            } else {
                /* ahead: ease off, gently (a leader must stay hard to catch) */
                speed = 1.0f - t * 0.06f;
                throttle = 1.0f - t * 0.08f;
            }
        }
        speed *= base;
        /* smoothly: about a second to settle */
        d->speed += (speed - d->speed) * 0.35f;
        d->throttle += (throttle - d->throttle) * 0.35f;
    }
}

static int active(void) {
    if (sMode < 0) {
        sMode = WM_AiMode();
    }
    return sMode != WM_AI_CLASSIC && WM_AiDirectorOn();
}

void WM_AiLog(void);

static void tick(void) {
    u64 now;
    WM_AiLog();
    if (gGame == NULL || RACE(gGame) == NULL) {
        return;
    }
    now = Time_Get(gGame + 0x10A8);
    if (sRace != RACE(gGame) || now < sLast || now - sLast >= 166000000ull) {
        sLast = now;
        update();
    }
}

f32 WM_CarPaceScale(void *car);

/* The pack race's computer cars (Car_Create, once the car knows it is one): a final
   ratio 12% longer, so their top speed is above the player's, and the engine's torque
   curve (Car+0x1A0..0x1AC, Car_GetDriveTorque) raised to keep their pull at the wheels.
   The track limits rise with it (PACK_GEAR in WM_AiSpeedScale). */
#define PACK_GEAR 0.88f
void WM_AiPackCar(void *car) {
    u8 *c = (u8 *)car;
    s32 i;
    if (car == NULL || WM_AiMode() != WM_AI_PACK_RACE || CAR_S32(car, 0x30) == 0) {
        return;
    }
    *(f32 *)(c + 0x1CC) *= PACK_GEAR;
    for (i = 0; i < 4; i++) {
        *(f32 *)(c + 0x1A0 + i * 4) /= PACK_GEAR;
    }
}

f32 WM_AiSpeedScale(void *car) {
    WMDirCar *d;
    /* the floor's faster gearbox raises the track's limits for this car alike, in every mode */
    f32 pace = WM_CarPaceScale(car);
    if (WM_AiMode() == WM_AI_PACK_RACE) {
        pace /= PACK_GEAR;
    }
    if (!active()) {
        return pace;
    }
    tick();
    d = find(car);
    return d != NULL ? d->speed * pace : pace;
}

float WM_AiThrottleScale(void *car) {
    WMDirCar *d;
    if (!active()) {
        return 1.0f;
    }
    tick();
    d = find(car);
    return d != NULL ? d->throttle : 1.0f;
}

/* DD64_AILOG (testing): each computer car's place against the player and its scales */
void WM_AiLog(void) {
    static int on = -1;
    static u32 n;
    u8 *race;
    s32 i, ns;
    if (on < 0) {
        on = getenv("DD64_AILOG") != NULL;
    }
    if (!on || gGame == NULL || (race = RACE(gGame)) == NULL || (++n % 300) != 0) {
        return;
    }
    ns = RACE_NSEGS(race);
    for (i = 0; i < sCount; i++) {
        void *c = sCars[i].car;
        if (c == NULL) {
            continue;
        }
        Port_Log("ailog: t%u car %d ai %d dir %d seg %d lap %d prog %d kin %d hp %d speed %.2f throttle %.2f", n, i,
                 CAR_S32(c, 0x30), CAR_S32(c, 0x58), CAR_S32(c, 0x28), CAR_S32(c, 0x88), progress(c, ns),
                 ((u8 *)c)[0xBAC], CAR_S32(c, 0x10), (double)sCars[i].speed, (double)sCars[i].throttle);
    }
}
