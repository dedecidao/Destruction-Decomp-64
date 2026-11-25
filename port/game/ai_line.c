/*
 * Wrecked Metal: a racing line recorded from the player ([ai] record_line) and driven by
 * the computer cars ([ai] racing_line), and the pack race's attacks on the player.
 *
 * Recording: while the player drives (any mode; a time trial gives a clean lap), each
 * track segment keeps the average of where across it the player was (0 at its point a,
 * 1 at b) and how fast (m/s), over every pass, skipping the crawls (under 5 m/s). The
 * file Data/lines/track_N.txt is rewritten every few seconds.
 *
 * Driving: when the track has a file, a computer car going the recorded way aims three
 * segments ahead at the recorded point across the track (Ai_Steer's target, x5C), and
 * brakes only above the recorded speed times [ai] line_pct (WM_AiLineLimit). In the pack
 * race a car close to the player (under 35 m) and one of the aggressive ones (six in
 * ten) aims at the player instead, a little ahead of where the player is going.
 */
#include "common.h"
#include "race_cars.h"
#include "wm_mod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

void Port_Log(const char *, ...);
const char *Port_BaseDir(void);

typedef struct { f32 x, y, z; } WMV3;
WMV3 Car_GetPos(void *);
WMV3 Car_GetVelocity(void *);
f32 Car_GetSpeed(void *);

extern u8 *gGame;
extern s32 D_800958B4; /* the track */

#define RACE(g) (*(u8 **)((g) + 0x1100))
#define RACE_NSEGS(r) (*(s32 *)((r) + 0x38))
#define RACE_SEG(r, i) ((const f32 *)(*(u8 **)((r) + 0x30) + (i) * 24)) /* a.xyz, b.xyz */
#define RACE_CAR(r, i) (((u8 **)((r) + 0x74))[i])
#define RACE_NCARS(r) (*(s32 *)((r) + 0x74 + RACE_CARS * 4))
#define CAR_S32(c, o) (*(s32 *)((u8 *)(c) + (o)))

#define LINE_MAX 1024

/* the recording (this session) and the line in use (loaded per race) */
static f32 sRecF[LINE_MAX], sRecV[LINE_MAX];
static u16 sRecN[LINE_MAX];
static s32 sRecTrack = -1, sRecSegs, sRecDir;
static f32 sLineF[LINE_MAX], sLineV[LINE_MAX];
static u8 sLineOk[LINE_MAX];
static s32 sLineTrack = -2, sLineSegs, sLineDir;
static void *sLineRace;

static void linePath(char *out, size_t n, s32 track) {
    snprintf(out, n, "%sData/lines/track_%d.txt", Port_BaseDir(), (int)track);
}

static void writeRecording(void) {
    char path[600];
    FILE *f;
    s32 i;
    snprintf(path, sizeof(path), "%sData/lines", Port_BaseDir());
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0775);
#endif
    linePath(path, sizeof(path), sRecTrack);
    f = fopen(path, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "track %d segs %d dir %d\n", (int)sRecTrack, (int)sRecSegs, (int)sRecDir);
    for (i = 0; i < sRecSegs; i++) {
        if (sRecN[i] > 0) {
            fprintf(f, "%d %.3f %.2f %d\n", (int)i, (double)sRecF[i], (double)sRecV[i], (int)sRecN[i]);
        }
    }
    fclose(f);
}

/* Each frame of the player's car (Car_UpdatePhysics) */
void WM_LineRecord(void *car) {
    static u32 frames;
    u8 *race;
    s32 seg, ns;
    const f32 *s;
    WMV3 p;
    f32 dx, dz, px, pz, len2, f, v;
    if (!WM_LineRecording() || car == NULL || CAR_S32(car, 0x30) != 0 || gGame == NULL ||
        (race = RACE(gGame)) == NULL || *(s32 *)(race + 0x10) != 0) {
        return; /* race tracks only (race+0x10 == 0) */
    }
    ns = RACE_NSEGS(race);
    seg = CAR_S32(car, 0x28);
    if (ns <= 0 || ns > LINE_MAX || seg < 0 || seg >= ns) {
        return;
    }
    if (sRecTrack != D_800958B4 || sRecSegs != ns) {
        memset(sRecN, 0, sizeof(sRecN));
        sRecTrack = D_800958B4;
        sRecSegs = ns;
        Port_Log("line: recording track %d (%d segments)", (int)sRecTrack, (int)ns);
    }
    sRecDir = CAR_S32(car, 0x58);
    v = Car_GetSpeed(car);
    if (v < 5.0f) {
        return;
    }
    s = RACE_SEG(race, seg);
    p = Car_GetPos(car);
    dx = s[3] - s[0], dz = s[5] - s[2];
    px = p.x - s[0], pz = p.z - s[2];
    len2 = dx * dx + dz * dz;
    if (len2 < 0.01f) {
        return;
    }
    f = (px * dx + pz * dz) / len2;
    if (f < 0.0f || f > 1.0f) {
        return; /* off the track's width */
    }
    if (sRecN[seg] < 60000) {
        u16 n = ++sRecN[seg];
        sRecF[seg] += (f - sRecF[seg]) / n;
        sRecV[seg] += (v - sRecV[seg]) / n;
    }
    if (++frames % 300 == 0) {
        writeRecording();
    }
}

/* the line for this race's track, if there is one */
static int lineReady(void) {
    u8 *race;
    if (!WM_RacingLine() || gGame == NULL || (race = RACE(gGame)) == NULL || *(s32 *)(race + 0x10) != 0) {
        return 0;
    }
    if (sLineRace != race || sLineTrack != D_800958B4) {
        char path[600], buf[128];
        FILE *f;
        s32 n = 0;
        sLineRace = race;
        sLineTrack = D_800958B4;
        sLineSegs = 0;
        memset(sLineOk, 0, sizeof(sLineOk));
        linePath(path, sizeof(path), sLineTrack);
        f = fopen(path, "r");
        if (f != NULL) {
            int tr, segs, dir;
            if (fgets(buf, sizeof(buf), f) && sscanf(buf, "track %d segs %d dir %d", &tr, &segs, &dir) == 3 &&
                segs == RACE_NSEGS(race) && segs <= LINE_MAX) {
                sLineSegs = segs;
                sLineDir = dir;
                while (fgets(buf, sizeof(buf), f)) {
                    int i, cnt;
                    float lf, lv;
                    if (sscanf(buf, "%d %f %f %d", &i, &lf, &lv, &cnt) == 4 && i >= 0 && i < segs) {
                        sLineF[i] = lf;
                        sLineV[i] = lv;
                        sLineOk[i] = 1;
                        n++;
                    }
                }
            }
            fclose(f);
        }
        if (n > 1 && n < sLineSegs) {
            /* the segments the recording missed (off the width, a crawl): filled in between
               their recorded neighbours, round the lap */
            s32 i, filled = 0;
            for (i = 0; i < sLineSegs; i++) {
                s32 a, b, ga, gb;
                if (sLineOk[i]) {
                    continue;
                }
                for (ga = 1; ga < sLineSegs && !sLineOk[(i - ga + sLineSegs) % sLineSegs]; ga++) {
                }
                for (gb = 1; gb < sLineSegs && !sLineOk[(i + gb) % sLineSegs]; gb++) {
                }
                a = (i - ga + sLineSegs) % sLineSegs;
                b = (i + gb) % sLineSegs;
                sLineF[i] = sLineF[a] + (sLineF[b] - sLineF[a]) * ga / (ga + gb);
                sLineV[i] = sLineV[a] + (sLineV[b] - sLineV[a]) * ga / (ga + gb);
                filled++;
            }
            for (i = 0; i < sLineSegs; i++) {
                sLineOk[i] = 1;
            }
            Port_Log("line: %d missing segments filled in", (int)filled);
        }
        Port_Log("line: track %d %s (%d of %d segments)", (int)sLineTrack, n ? "racing line loaded" : "no racing line",
                 (int)n, (int)RACE_NSEGS(race));
        if (n == 0) {
            sLineSegs = 0;
        }
    }
    return sLineSegs > 0;
}

/* the player (for the attacks) */
static void *playerCar(u8 *race) {
    s32 i, n = RACE_NCARS(race);
    for (i = 0; i < n && i < RACE_CARS; i++) {
        void *c = RACE_CAR(race, i);
        if (c != NULL && CAR_S32(c, 0x30) == 0) {
            return c;
        }
    }
    return NULL;
}

/* Ai_Steer, before it steers: the target point (`target`, the AiDriver's x5C) */
void WM_AiSteerTarget(void *car, f32 *target) {
    u8 *race;
    void *pl;
    if (!WM_Enabled() || gGame == NULL || (race = RACE(gGame)) == NULL || *(s32 *)(race + 0x10) != 0) {
        return;
    }
    /* the pack race's attack: close to the player, an aggressive car rams them */
    if (WM_AiMode() == WM_AI_PACK_RACE && (pl = playerCar(race)) != NULL && CAR_S32(car, 0x58) == CAR_S32(pl, 0x58)) {
        WMV3 a = Car_GetPos(car), b = Car_GetPos(pl);
        f32 dx = b.x - a.x, dz = b.z - a.z;
        u32 h = ((u32)(uintptr_t)car >> 4) * 2654435761u >> 28; /* this car's own die, 0-15 */
        if (dx * dx + dz * dz < 35.0f * 35.0f && h < 10) {
            WMV3 v = Car_GetVelocity(pl);
            target[0] = b.x + v.x * 0.25f;
            target[1] = b.y;
            target[2] = b.z + v.z * 0.25f;
            return;
        }
    }
    if (lineReady() && CAR_S32(car, 0x58) == sLineDir) {
        s32 seg = CAR_S32(car, 0x28) + 3 * sLineDir;
        const f32 *s;
        seg %= sLineSegs;
        if (seg < 0) {
            seg += sLineSegs;
        }
        if (!sLineOk[seg]) {
            return;
        }
        s = RACE_SEG(race, seg);
        target[0] = s[0] + (s[3] - s[0]) * sLineF[seg];
        target[1] = s[1] + (s[4] - s[1]) * sLineF[seg];
        target[2] = s[2] + (s[5] - s[2]) * sLineF[seg];
    }
}

/* The recorded speed here (m/s) times [ai] line_pct, or 0 without a line */
f32 WM_AiLineLimit(void *car) {
    s32 seg;
    if (!lineReady() || CAR_S32(car, 0x58) != sLineDir) {
        return 0.0f;
    }
    seg = CAR_S32(car, 0x28);
    if (seg < 0 || seg >= sLineSegs || !sLineOk[seg]) {
        return 0.0f;
    }
    return sLineV[seg] * WM_LinePct() / 100.0f;
}
