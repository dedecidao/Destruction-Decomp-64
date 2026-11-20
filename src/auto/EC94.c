#include "common.h"
#include "m2c.h"

typedef struct {
    s8 x0;
    s8 x1;
    s8 x2;
    s8 x3;
    s32 x4;
} Time;
extern char D_800B8270[];
extern char D_800B827C[];
int sprintf(char *, const char *, ...);

#ifdef TARGET_PC
/*
 * The callers give 8-byte buffers (Records_Draw's S8 buf, 4A128.c; TrackMenu_FormatRecords,
 * 4207C.c) and sprintf writes the whole time first: a time that does not fit ("%1d"
 * minutes past 9, an unset record) runs past the buffer, and only then is it cut at
 * len - 1. On the N64 that overran the caller's stack silently; built with the stack
 * protector (Android's NDK default) it aborts the game (__stack_chk_fail), as after a
 * time trial record. The time is formatted in a buffer that holds any of them, and
 * what the N64 left in the caller's len bytes is copied there.
 */
static void Time_FormatN64(Time t, char *buf, s32 len);
void Time_Format(Time t, char *buf, s32 len) {
    char tmp[48];
    s32 i;

    if (len <= 0 || len > (s32)sizeof(tmp) - 16) {
        return;
    }
    for (i = 0; i < (s32)sizeof(tmp); i++) {
        tmp[i] = i < len ? buf[i] : 0;
    }
    Time_FormatN64(t, tmp, len);
    for (i = 0; i < len; i++) {
        buf[i] = tmp[i];
    }
}
static void Time_FormatN64(Time t, char *buf, s32 len) {
#else
void Time_Format(Time t, char *buf, s32 len) {
#endif
    if (len == 8) {
        sprintf(buf, D_800B8270, t.x1, t.x2, t.x4);
    } else {
        sprintf(buf, D_800B827C, t.x1, t.x2, t.x4);
        if (t.x1 < 10) {
            buf[len - 9] = '0';
        }
    }
    if (t.x2 < 10) {
        buf[len - 6] = '0';
    }
    if (t.x4 < 100 && ((t.x4 >= 10 && t.x4 < 100) || t.x4 < 10)) {
        buf[len - 3] = '0';
    }
    buf[len - 1] = 0;
}
