#include "common.h"
#include "m2c.h"

typedef struct { u8 pad[0x40]; s32 key; u8 pad44[0x52]; s16 vol; u8 pad98[0x88]; } Voice;
extern s32 gNumVoices;
extern Voice *gVoices;
s32 Sound_SetVolume(s32 key, s16 value) {
    s32 count; s32 i; Voice *p;
    if (key == 0) return 0;
#ifdef TARGET_PC
    {
        /* the music's key (D_8011EEE0): a replacement track takes the volume, the
           game's own song plays silent under it (port/src/music.c) */
        extern s32 D_8011EEE0;
        s32 Port_MusicSetVolume(s32 vol);
        if (key == D_8011EEE0) {
            value = (s16)Port_MusicSetVolume(value);
        }
    }
#endif
    count = 0; i = 0; p = gVoices;
    while (i < gNumVoices) {
        if (p->key == key) {
            p->vol = value;
            count++;
        }
        i++;
        p++;
    }
    return count;
}
