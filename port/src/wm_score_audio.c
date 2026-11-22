/*
 * Wrecked Metal procedural score audio (port/src/wm_score_audio.c).
 *
 * Synthesizes tiered arcade hit chords, rising-pitch countdown ticks, and a
 * confirmation chime on the fly using a lightweight SDL3 audio stream. Zero ROM
 * assets, minimal CPU cost, and inactive in Play Fidelity mode.
 */
#include <SDL3/SDL.h>
#include <math.h>

#include "port.h"
#include "port_config.h"
#include "wm_mod.h"

#define SCORE_RATE 22050
#define SCORE_MAX_SAMPLES (SCORE_RATE / 2) /* 0.5 s max buffer */
#define TWO_PI 6.28318530718f

static SDL_AudioStream *sOut;
static s16 sPcm[SCORE_MAX_SAMPLES];
static Uint64 sLastTickMs;

static float softClip(float x) {
    float ax = x < 0.0f ? -x : x;
    return x / (1.0f + ax);
}

static float streamGain(void) {
    const WMTuning *t = WM_GetTuning();
    float master = (float)SDL_clamp(gPortConfig.volume, 0, 100) / 100.0f;
    float fx = SDL_clamp(t->scoreFxVolume, 0.0f, 1.5f);
    return SDL_clamp(master * fx, 0.0f, 1.5f);
}

static int ensureStream(void) {
    const WMTuning *t = WM_GetTuning();

    if (!WM_Enabled() || !t->scoreFxEnabled || !t->scoreFxAudio) {
        return 0;
    }
    if (sOut == NULL) {
        WM_ScoreAudioOpen();
    }
    return sOut != NULL;
}

static void submitPcm(int count, float gain) {
    int i;

    if (sOut == NULL || count <= 0) {
        return;
    }
    if (count > SCORE_MAX_SAMPLES) {
        count = SCORE_MAX_SAMPLES;
    }
    for (i = 0; i < count; i++) {
        /* keep headroom so s16 never wraps */
        float v = sPcm[i] * (1.0f / 32767.0f);
        sPcm[i] = (s16)SDL_clamp(v * 32767.0f, -32767.0f, 32767.0f);
    }
    /* Never let old sounds pile up in the queue */
    if (SDL_GetAudioStreamQueued(sOut) > (int)(SCORE_RATE * sizeof(s16) / 3)) {
        SDL_ClearAudioStream(sOut);
    }
    SDL_SetAudioStreamGain(sOut, gain);
    SDL_PutAudioStreamData(sOut, sPcm, count * (int)sizeof(s16));
}

void WM_ScoreAudioOpen(void) {
    SDL_AudioSpec spec;

    if (sOut != NULL) {
        return;
    }
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;
    spec.freq = SCORE_RATE;
    sOut = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (sOut == NULL) {
        Port_Log("wm_score: no audio stream: %s", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(sOut);
}

void WM_ScoreAudioClose(void) {
    if (sOut != NULL) {
        SDL_DestroyAudioStream(sOut);
        sOut = NULL;
    }
}

void WM_ScoreAudioHit(int added, int pendingTotal) {
    int score = pendingTotal > added ? pendingTotal : added;
    int tier, n, i;
    float gain;

    if (!ensureStream() || score <= 0) {
        return;
    }
    gain = streamGain();
    if (gain <= 0.001f) {
        return;
    }

    if (score >= 100) {
        tier = 4;
    } else if (score >= 41) {
        tier = 3;
    } else if (score >= 16) {
        tier = 2;
    } else {
        tier = 1;
    }

    if (tier == 1) {
        /* Tier 1 (1-15 pts): crisp rising arcade blip (~90 ms) */
        float ph1 = 0.0f, ph2 = 0.0f;
        float env = 1.0f, envK = expf(-1.0f / (SCORE_RATE * 0.032f));
        n = (SCORE_RATE * 90) / 1000;
        for (i = 0; i < n; i++) {
            float t = (float)i / (float)n;
            float f = 660.0f + 240.0f * t;
            float atk = i < 48 ? (float)i / 48.0f : 1.0f;
            float s = (sinf(ph1) * 0.72f + sinf(ph2) * 0.28f) * env * atk * 0.55f;
            ph1 += TWO_PI * f / SCORE_RATE;
            ph2 += TWO_PI * (f * 2.0f) / SCORE_RATE;
            env *= envK;
            sPcm[i] = (s16)(softClip(s) * 32767.0f);
        }
    } else if (tier == 2) {
        /* Tier 2 (16-40 pts): bright metallic power chord (~160 ms) */
        float ph1 = 0.0f, ph2 = 0.0f, ph3 = 0.0f;
        float env = 1.0f, envK = expf(-1.0f / (SCORE_RATE * 0.055f));
        n = (SCORE_RATE * 160) / 1000;
        for (i = 0; i < n; i++) {
            float atk = i < 44 ? (float)i / 44.0f : 1.0f;
            float s = (sinf(ph1) * 0.45f + sinf(ph2) * 0.35f + sinf(ph3) * 0.25f) * env * atk * 0.65f;
            ph1 += TWO_PI * 523.25f / SCORE_RATE;  /* C5 */
            ph2 += TWO_PI * 783.99f / SCORE_RATE;  /* G5 */
            ph3 += TWO_PI * 1046.50f / SCORE_RATE; /* C6 */
            env *= envK;
            sPcm[i] = (s16)(softClip(s) * 32767.0f);
        }
    } else if (tier == 3) {
        /* Tier 3 (41-99 pts): punchy sub-bass + 3-note rising arpeggio (~230 ms) */
        static const float kNotes3[3] = { 523.25f, 659.25f, 783.99f }; /* C5 E5 G5 */
        float phSub = 0.0f, phNote = 0.0f, phHarm = 0.0f;
        float subEnv = 1.0f, subK = expf(-1.0f / (SCORE_RATE * 0.045f));
        float noteEnv = 1.0f, noteK = expf(-1.0f / (SCORE_RATE * 0.065f));
        int stepLen = (SCORE_RATE * 55) / 1000;
        int prevStep = -1;
        n = (SCORE_RATE * 230) / 1000;
        for (i = 0; i < n; i++) {
            int step = i / stepLen;
            float fSub = 120.0f - 65.0f * ((float)i / (float)n);
            float fNote;
            float s;
            if (step > 2) {
                step = 2;
            }
            if (step != prevStep) {
                noteEnv = 1.0f;
                prevStep = step;
            }
            fNote = kNotes3[step];
            s = sinf(phSub) * subEnv * 0.50f +
                (sinf(phNote) * 0.65f + sinf(phHarm) * 0.30f) * noteEnv * 0.45f;
            phSub += TWO_PI * fSub / SCORE_RATE;
            phNote += TWO_PI * fNote / SCORE_RATE;
            phHarm += TWO_PI * (fNote * 2.0f) / SCORE_RATE;
            subEnv *= subK;
            noteEnv *= noteK;
            sPcm[i] = (s16)(softClip(s) * 32767.0f);
        }
    } else {
        /* Tier 4 (100+ pts, Mega Crash): deep sub-bass + 4-note jackpot cascade (~330 ms) */
        static const float kNotes4[4] = { 523.25f, 659.25f, 783.99f, 1046.50f }; /* C5 E5 G5 C6 */
        float phSub = 0.0f, phNote = 0.0f, phHarm = 0.0f;
        float subEnv = 1.0f, subK = expf(-1.0f / (SCORE_RATE * 0.065f));
        float noteEnv = 1.0f, noteK = expf(-1.0f / (SCORE_RATE * 0.080f));
        int stepLen = (SCORE_RATE * 55) / 1000;
        int prevStep = -1;
        n = (SCORE_RATE * 330) / 1000;
        for (i = 0; i < n; i++) {
            int step = i / stepLen;
            float fSub = 135.0f - 85.0f * ((float)i / (float)n);
            float fNote;
            float s;
            if (step > 3) {
                step = 3;
            }
            if (step != prevStep) {
                noteEnv = 1.0f;
                prevStep = step;
            }
            fNote = kNotes4[step];
            s = sinf(phSub) * subEnv * 0.55f +
                (sinf(phNote) * 0.60f + sinf(phHarm) * 0.35f) * noteEnv * 0.55f;
            phSub += TWO_PI * fSub / SCORE_RATE;
            phNote += TWO_PI * fNote / SCORE_RATE;
            phHarm += TWO_PI * (fNote * 2.0f) / SCORE_RATE;
            subEnv *= subK;
            noteEnv *= noteK;
            sPcm[i] = (s16)(softClip(s) * 32767.0f);
        }
    }

    submitPcm(n, gain);
}

void WM_ScoreAudioTick(int remaining, int peak) {
    Uint64 now = SDL_GetTicks();
    float progress, freq, ph = 0.0f, env = 1.0f, envK, gain;
    int n, i;

    if (!ensureStream()) {
        return;
    }
    if (now - sLastTickMs < 28) {
        return;
    }
    sLastTickMs = now;
    gain = streamGain();
    if (gain <= 0.001f) {
        return;
    }
    /* Avoid piling ticks behind a hit chord */
    if (SDL_GetAudioStreamQueued(sOut) > (int)(SCORE_RATE * sizeof(s16) / 6)) {
        return;
    }

    if (peak <= 0) {
        peak = 1;
    }
    progress = 1.0f - SDL_clamp((float)remaining / (float)peak, 0.0f, 1.0f);
    freq = 540.0f + progress * 540.0f; /* 540 Hz -> 1080 Hz as it drains */
    n = (SCORE_RATE * 26) / 1000;
    envK = expf(-1.0f / (SCORE_RATE * 0.009f));
    for (i = 0; i < n; i++) {
        float atk = i < 24 ? (float)i / 24.0f : 1.0f;
        float s = sinf(ph) * env * atk * 0.38f;
        ph += TWO_PI * freq / SCORE_RATE;
        env *= envK;
        sPcm[i] = (s16)(s * 32767.0f);
    }
    submitPcm(n, gain);
}

void WM_ScoreAudioBanked(int peak) {
    float ph1 = 0.0f, ph2 = 0.0f, env = 1.0f, envK, gain;
    int n, i;

    if (!ensureStream() || peak < 15) {
        return;
    }
    gain = streamGain();
    if (gain <= 0.001f) {
        return;
    }
    n = (SCORE_RATE * 130) / 1000;
    envK = expf(-1.0f / (SCORE_RATE * 0.045f));
    for (i = 0; i < n; i++) {
        float atk = i < 32 ? (float)i / 32.0f : 1.0f;
        float s = (sinf(ph1) * 0.6f + sinf(ph2) * 0.4f) * env * atk * 0.48f;
        ph1 += TWO_PI * 1046.50f / SCORE_RATE; /* C6 */
        ph2 += TWO_PI * 1567.98f / SCORE_RATE; /* G6 */
        env *= envK;
        sPcm[i] = (s16)(softClip(s) * 32767.0f);
    }
    submitPcm(n, gain);
}
