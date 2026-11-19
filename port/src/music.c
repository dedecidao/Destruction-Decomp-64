/*
 * Replacement music: the game's sequenced songs replaced by recorded tracks.
 *
 * The game plays its music with the libultra synthesizer: each song is a sequence
 * (notes for the instruments of the game's sound banks), started by
 * Res_LoadSoundBank (src/auto/90EC.c, resource type 13), which copies it from the
 * ROM and calls Seq_StartWithBank. A song is known by where its sequence starts in
 * the ROM. When Data/Music/song_XXXXXXXX.ogg (or .mp3) exists for it, that file
 * plays instead, looping, on its own SDL audio stream: the game's song still runs
 * (its flow waits on the music voices), muted (Sound_SetVolume, 66B24.c, asks
 * Port_MusicSetVolume), and the volume the game gives it (the options' music
 * slider) and its fade-outs (Sound_FadeOutAll, 66960.c) go to the file.
 *
 * Decoders: stb_vorbis (Ogg Vorbis) and dr_mp3, both single-file and public domain
 * (port/third_party).
 *
 * DD64_MUSICDUMP=1: each song the synthesizer plays is recorded to
 * Data/MusicDump/song_XXXXXXXX.wav (the whole game output while it is the current
 * song: dump from the options screen's "playsong" for the music alone), the
 * starting point for remastered replacements. Every song started is logged.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "port.h"
#include "port_config.h"
#include "port_shadow.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "../third_party/stb_vorbis.c"
#undef L
#undef C
#undef R
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "../third_party/dr_mp3.h"

enum { TRACK_NONE, TRACK_OGG, TRACK_MP3 };

static SDL_Mutex *sLock;
static SDL_AudioStream *sOut;
static int sKind;
static stb_vorbis *sOgg;
static drmp3 sMp3;
static void *sData;
static int sChannels, sRate;
static u32 sSong;            /* the current song's ROM address */
static float sGameVol = 1.0f; /* the game's volume for its music, 0-1 */
static float sFade = 1.0f;    /* 1, down to 0 while fading out */
static float sFadeStep;       /* per sample frame */

/* ---- Dump ---- */
static FILE *sDump;
static u32 sDumpBytes, sDumpRate;

static void dumpClose(void) {
    if (sDump != NULL) {
        u32 riff = 36 + sDumpBytes;
        fseek(sDump, 4, SEEK_SET);
        fwrite(&riff, 4, 1, sDump);
        fseek(sDump, 40, SEEK_SET);
        fwrite(&sDumpBytes, 4, 1, sDump);
        fclose(sDump);
        sDump = NULL;
    }
}

static void dumpOpen(u32 song) {
    static int on = -1;
    char path[1024];

    if (on < 0) {
        on = SDL_getenv("DD64_MUSICDUMP") != NULL;
    }
    dumpClose();
    if (!on) {
        return;
    }
    SDL_snprintf(path, sizeof(path), "%sData/MusicDump", Port_BaseDir());
    SDL_CreateDirectory(path);
    SDL_snprintf(path, sizeof(path), "%sData/MusicDump/song_%08X.wav", Port_BaseDir(), song);
    sDump = fopen(path, "wb");
    sDumpBytes = 0;
    sDumpRate = 0;
    if (sDump != NULL) {
        static const u8 hdr[44] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                                    0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0 };
        fwrite(hdr, 1, sizeof(hdr), sDump);
        Port_Log("music: dumping to %s", path);
    }
}

/* The game's output as it is queued (s16 stereo in N64 order, in game memory): into
   the current dump */
void Port_MusicDumpSamples(const void *samples, u32 bytes, u32 rate) {
    const u8 *p = samples;
    u16 buf[2048];
    u32 i, n;

    if (sDump == NULL) {
        return;
    }
    if (sDumpRate == 0) {
        u32 byteRate = rate * 4;
        sDumpRate = rate;
        fseek(sDump, 24, SEEK_SET);
        fwrite(&rate, 4, 1, sDump);
        fwrite(&byteRate, 4, 1, sDump);
        fseek(sDump, 0, SEEK_END);
    }
    for (i = 0; i < bytes; i += n * 2) {
        u32 k;
        n = SDL_min((bytes - i) / 2, (u32)SDL_arraysize(buf));
        for (k = 0; k < n; k++) {
            buf[k] = (u16)Port_RdN64(p + i + k * 2, 2);
        }
        fwrite(buf, 2, n, sDump);
    }
    sDumpBytes += bytes & ~1u;
}

/* ---- Playback ---- */

static void closeTrack(void) {
    if (sOgg != NULL) {
        stb_vorbis_close(sOgg);
        sOgg = NULL;
    }
    if (sKind == TRACK_MP3) {
        drmp3_uninit(&sMp3);
    }
    SDL_free(sData);
    sData = NULL;
    sKind = TRACK_NONE;
}

/* Frames of s16 interleaved, looping at the end */
static int decode(s16 *out, int frames) {
    int got = 0, n, tries = 0;

    while (got < frames && tries < 2) {
        if (sKind == TRACK_OGG) {
            n = stb_vorbis_get_samples_short_interleaved(sOgg, sChannels, out + got * sChannels, (frames - got) * sChannels);
        } else {
            n = (int)drmp3_read_pcm_frames_s16(&sMp3, (drmp3_uint64)(frames - got), out + got * sChannels);
        }
        if (n <= 0) { /* the end: loop */
            if (sKind == TRACK_OGG) {
                stb_vorbis_seek_start(sOgg);
            } else {
                drmp3_seek_to_pcm_frame(&sMp3, 0);
            }
            tries++;
            continue;
        }
        tries = 0;
        got += n;
    }
    return got;
}

static void SDLCALL feed(void *userdata, SDL_AudioStream *stream, int additional, int total) {
    s16 buf[4096];

    (void)userdata;
    (void)total;
    SDL_LockMutex(sLock);
    while (additional > 0 && sKind != TRACK_NONE) {
        int frames = SDL_min((int)(sizeof(buf) / sizeof(buf[0])) / sChannels, additional / (2 * sChannels));
        int got, i;
        if (frames <= 0) {
            break;
        }
        got = decode(buf, frames);
        if (got <= 0) {
            break;
        }
        if (sFadeStep > 0.0f) { /* fading out */
            for (i = 0; i < got; i++) {
                int c;
                for (c = 0; c < sChannels; c++) {
                    buf[i * sChannels + c] = (s16)(buf[i * sChannels + c] * sFade);
                }
                sFade = SDL_max(sFade - sFadeStep, 0.0f);
            }
        }
        SDL_PutAudioStreamData(stream, buf, got * 2 * sChannels);
        additional -= got * 2 * sChannels;
        if (sFadeStep > 0.0f && sFade <= 0.0f) {
            closeTrack(); /* faded out: the game's next song starts a new one */
        }
    }
    SDL_SetAudioStreamGain(stream, sGameVol * (float)gPortConfig.volume / 100.0f);
    SDL_UnlockMutex(sLock);
}

static int openTrack(const char *path, int kind) {
    size_t size;
    void *data = SDL_LoadFile(path, &size);
    int err = 0;

    if (data == NULL) {
        return 0;
    }
    if (kind == TRACK_OGG) {
        stb_vorbis_info info;
        sOgg = stb_vorbis_open_memory(data, (int)size, &err, NULL);
        if (sOgg == NULL) {
            Port_Log("music: %s: not an Ogg Vorbis file (%d)", path, err);
            SDL_free(data);
            return 0;
        }
        info = stb_vorbis_get_info(sOgg);
        sChannels = info.channels;
        sRate = (int)info.sample_rate;
    } else {
        if (!drmp3_init_memory(&sMp3, data, size, NULL)) {
            Port_Log("music: %s: not an MP3 file", path);
            SDL_free(data);
            return 0;
        }
        sChannels = (int)sMp3.channels;
        sRate = (int)sMp3.sampleRate;
    }
    if (sChannels < 1 || sChannels > 2) {
        Port_Log("music: %s: %d channels (mono or stereo only)", path, sChannels);
        sKind = kind;
        sData = data;
        closeTrack();
        return 0;
    }
    sKind = kind;
    sData = data;
    return 1;
}

/* Res_LoadSoundBank is starting the song whose sequence is at `rom` in the ROM */
void Port_MusicStart(u32 rom) {
    char path[1024];
    SDL_AudioSpec spec;

    if (sLock == NULL) {
        sLock = SDL_CreateMutex();
    }
    dumpOpen(rom);
    SDL_LockMutex(sLock);
    closeTrack();
    sSong = rom;
    sFade = 1.0f;
    sFadeStep = 0.0f;
    SDL_snprintf(path, sizeof(path), "%sData/Music/song_%08X.ogg", Port_BaseDir(), rom);
    if (!openTrack(path, TRACK_OGG)) {
        SDL_snprintf(path, sizeof(path), "%sData/Music/song_%08X.mp3", Port_BaseDir(), rom);
        openTrack(path, TRACK_MP3);
    }
    SDL_UnlockMutex(sLock);
    if (sKind == TRACK_NONE) {
        Port_Log("music: song_%08X (the game's)", rom);
        if (sOut != NULL) {
            SDL_PauseAudioStreamDevice(sOut);
        }
        return;
    }
    Port_Log("music: song_%08X from %s (%d Hz, %d channel%s)", rom, path, sRate, sChannels, sChannels > 1 ? "s" : "");
    spec.format = SDL_AUDIO_S16;
    spec.channels = sChannels;
    spec.freq = sRate;
    if (sOut != NULL) {
        SDL_DestroyAudioStream(sOut);
    }
    sOut = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (sOut == NULL) {
        Port_Log("music: no audio stream: %s", SDL_GetError());
        SDL_LockMutex(sLock);
        closeTrack();
        SDL_UnlockMutex(sLock);
        return;
    }
    SDL_ResumeAudioStreamDevice(sOut);
}

/* Sound_SetVolume on the music's key: the volume (0-128) to give it. Returns what
   the game's own song gets: nothing while a file plays instead. */
s32 Port_MusicSetVolume(s32 vol) {
    sGameVol = SDL_clamp(vol, 0, 128) / 128.0f;
    return sKind != TRACK_NONE ? 0 : vol;
}

/* Sound_FadeOutAll with the music's bit: the file fades out too (frames of 60 Hz) */
void Port_MusicFadeOut(s32 frames) {
    if (sLock == NULL || sKind == TRACK_NONE) {
        return;
    }
    SDL_LockMutex(sLock);
    if (frames <= 0) {
        closeTrack();
    } else {
        sFadeStep = 1.0f / ((float)frames / 60.0f * (float)sRate);
    }
    SDL_UnlockMutex(sLock);
}

int Port_MusicReplaced(void) {
    return sKind != TRACK_NONE;
}
