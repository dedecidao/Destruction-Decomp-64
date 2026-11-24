/*
 * Wrecked Metal: each car's own record (CarStats, 0x2B0 bytes: the handling, engine,
 * gearing, mass, damage zones), read from its resource as CarObj_Create does, for
 * the car-select screen's stat bars.
 */
#include "common.h"
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
        sLoaded[id] = 1;
    }
    return sRec[id];
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
