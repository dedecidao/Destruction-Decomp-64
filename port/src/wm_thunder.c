/*
 * Wrecked Metal thunder: a rumble synthesized on the fly for the storm weather
 * (port/game/wm_weather.c). The retail game has no thunder it plays (its lightning
 * code, Weather_UpdateLightning, is never switched on by any track, and sound
 * slot 9 it would use is a different sound in each track's bank), so the sound is
 * made here: filtered noise with a few rolling swells, sharper and louder when the
 * strike is near. No samples, nothing from the ROM.
 *
 * It plays on its own SDL audio stream (like the music, port/src/music.c), opened
 * when a storm race loads and closed when it ends.
 */
#include <SDL3/SDL.h>
#include <math.h>

#include "port.h"
#include "port_config.h"
#include "wm_mod.h"

#define RATE 22050
#define SECONDS 5

static SDL_AudioStream *sOut;
static s16 *sPcm;
static u32 sRng = 0x9E3779B9u;

static float noise(void) {
    sRng ^= sRng << 13;
    sRng ^= sRng >> 17;
    sRng ^= sRng << 5;
    return (float)(sRng & 0xFFFF) * (2.0f / 65535.0f) - 1.0f;
}

static float urand(void) {
    return (noise() + 1.0f) * 0.5f;
}

void WM_ThunderOpen(void) {
    SDL_AudioSpec spec;

    if (sOut != NULL) {
        return;
    }
    if (sPcm == NULL) {
        sPcm = (s16 *)SDL_malloc(RATE * SECONDS * sizeof(s16));
        if (sPcm == NULL) {
            return;
        }
    }
    sRng ^= (u32)SDL_GetPerformanceCounter() | 1u;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;
    spec.freq = RATE;
    sOut = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (sOut == NULL) {
        Port_Log("weather: no thunder stream: %s", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(sOut);
}

void WM_ThunderClose(void) {
    if (sOut != NULL) {
        SDL_DestroyAudioStream(sOut);
        sOut = NULL;
    }
}

/* One thunder, `dist` 0 (overhead: a crack, then a heavy roll) to 1 (far: a low
   rumble only) */
void WM_ThunderPlay(float dist) {
    float tk[6], ak[6], decay, attack, cut, crack, peak = 0.0f, gain;
    float lp1 = 0.0f, lp2 = 0.0f, brown = 0.0f;
    int rolls, n = RATE * SECONDS, i, k;
    static float buf[RATE * SECONDS];

    if (sOut == NULL || sPcm == NULL) {
        return;
    }
    dist = SDL_clamp(dist, 0.0f, 1.0f);
    rolls = 2 + (int)(urand() * 4.0f);
    for (k = 0; k < rolls; k++) {
        tk[k] = 0.15f + urand() * (1.2f + dist * 1.5f);
        ak[k] = 0.35f + urand() * 0.65f;
    }
    decay = 0.6f + dist * 0.9f + urand() * 0.5f;
    attack = 0.01f + dist * 0.25f;
    cut = 0.03f + (1.0f - dist) * 0.12f; /* one-pole low-pass: far thunder loses its highs */
    crack = (1.0f - dist) * (1.0f - dist);
    for (i = 0; i < n; i++) {
        float t = (float)i / RATE, w = noise(), env, s;

        brown = brown * 0.996f + w * 0.05f;
        lp1 += (w - lp1) * cut;
        lp2 += (lp1 - lp2) * cut;
        env = expf(-t / decay);
        for (k = 0; k < rolls; k++) {
            if (t > tk[k]) {
                env += ak[k] * expf(-(t - tk[k]) / (0.25f + dist * 0.4f));
            }
        }
        env *= t < attack ? t / attack : 1.0f;
        env *= t > SECONDS - 0.5f ? (SECONDS - t) / 0.5f : 1.0f;
        s = (lp2 * 4.0f + brown * 1.2f) * env + crack * expf(-t / 0.05f) * w * 0.8f;
        buf[i] = s;
        if (fabsf(s) > peak) {
            peak = fabsf(s);
        }
    }
    gain = peak > 0.0f ? (0.9f - dist * 0.55f) / peak : 0.0f;
    for (i = 0; i < n; i++) {
        sPcm[i] = (s16)SDL_clamp(buf[i] * gain * 32767.0f, -32767.0f, 32767.0f);
    }
    if (SDL_GetAudioStreamQueued(sOut) > RATE) {
        SDL_ClearAudioStream(sOut); /* a new strike cuts the old roll's tail */
    }
    SDL_SetAudioStreamGain(sOut, (float)SDL_clamp(gPortConfig.volume, 0, 100) / 100.0f);
    SDL_PutAudioStreamData(sOut, sPcm, n * (int)sizeof(s16));
}
