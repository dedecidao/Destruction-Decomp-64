/*
 * Wrecked Metal: the wind at speed ([speedfx] wind), a procedural loop on its own SDL
 * audio stream: white noise through a one-pole low-pass whose cutoff opens with speed
 * (a low rumble at 40% of the top speed, a hiss near the top), with slow gusts. Silent
 * below 30% of the top speed, and fading out within a fifth of a second once the game
 * stops reporting a speed (a pause, a menu, the race over). No ROM assets; a few
 * multiplies a sample on the audio thread.
 */
#include <SDL3/SDL.h>
#include <math.h>

#include "port.h"
#include "port_config.h"
#include "wm_mod.h"

#define WIND_RATE 22050

static SDL_AudioStream *sWind;
static SDL_AtomicInt sSpeedMilli; /* speed 0-1000 (of the top speed) */
static SDL_AtomicInt sLastMs;
static float sLp, sLp2, sGain, sGust, sGustTarget;
static Uint32 sNoise = 0x1234567u;
static int sGustLeft;

static float noise(void) {
    sNoise ^= sNoise << 13;
    sNoise ^= sNoise >> 17;
    sNoise ^= sNoise << 5;
    return (float)(sNoise & 0xFFFF) / 32768.0f - 1.0f;
}

static void SDLCALL windFeed(void *user, SDL_AudioStream *stream, int need, int total) {
    s16 buf[512];
    (void)user;
    (void)total;
    while (need > 0) {
        int n = need / (int)sizeof(s16), i;
        float s, target, cut, a, master;
        Uint64 now = SDL_GetTicks();
        if (n > (int)SDL_arraysize(buf)) {
            n = SDL_arraysize(buf);
        }
        s = SDL_GetAtomicInt(&sSpeedMilli) / 1000.0f;
        if (now - (Uint64)(Uint32)SDL_GetAtomicInt(&sLastMs) > 200) {
            s = 0.0f; /* no news from the race: fade out */
        }
        master = (float)SDL_clamp(gPortConfig.volume, 0, 100) / 100.0f;
        target = s < 0.30f ? 0.0f : (s - 0.30f) / 0.70f;
        target = target * target * 0.55f * master * WM_GetTuning()->speedWindPct / 100.0f;
        cut = 0.03f + 0.22f * s * s; /* the low-pass: a rumble opening to a hiss */
        for (i = 0; i < n; i++) {
            if (--sGustLeft <= 0) {
                sGustLeft = WIND_RATE / 3 + (int)(sNoise % (WIND_RATE / 2));
                sGustTarget = 0.65f + 0.35f * (noise() * 0.5f + 0.5f);
            }
            sGust += (sGustTarget - sGust) * 0.00008f;
            sGain += (target - sGain) * 0.0004f;
            a = noise();
            sLp += (a - sLp) * cut;
            sLp2 += (sLp - sLp2) * (cut * 1.6f > 0.9f ? 0.9f : cut * 1.6f);
            buf[i] = (s16)SDL_clamp(sLp2 * sGain * sGust * 26000.0f, -32000.0f, 32000.0f);
        }
        SDL_PutAudioStreamData(stream, buf, n * (int)sizeof(s16));
        need -= n * (int)sizeof(s16);
    }
}

/* The game, each frame of a race with one player (wm_speedfx.c): how fast, 0-1 */
void WM_WindSet(float speedNorm) {
    if (!WM_SpeedFx() || WM_GetTuning()->speedWindPct <= 0) {
        return;
    }
    if (sWind == NULL) {
        SDL_AudioSpec spec;
        spec.format = SDL_AUDIO_S16;
        spec.channels = 1;
        spec.freq = WIND_RATE;
        sWind = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, windFeed, NULL);
        if (sWind == NULL) {
            Port_Log("wm_wind: no audio stream: %s", SDL_GetError());
            return;
        }
        SDL_ResumeAudioStreamDevice(sWind);
        Port_Log("wm_wind: on");
    }
    SDL_SetAtomicInt(&sSpeedMilli, (int)(SDL_clamp(speedNorm, 0.0f, 1.0f) * 1000.0f));
    SDL_SetAtomicInt(&sLastMs, (int)(Uint32)SDL_GetTicks());
}
