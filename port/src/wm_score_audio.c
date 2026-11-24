/*
 * Wrecked Metal procedural score audio (port/src/wm_score_audio.c).
 *
 * Synthesizes punchy, high-presence tiered arcade hit chords, frenetic
 * rising-pitch countdown ticks, and a resonant confirmation bell on the fly
 * using a dedicated SDL3 audio stream. Zero ROM assets, minimal CPU cost,
 * and inactive in Play Fidelity mode.
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

/* The score sounds are made on the game thread when a hit lands or the counter ticks
   (every frame while it drains): a table sine instead of sinf, 4096 points with linear
   steps between them (about -90 dB of error), so a tick costs a few microseconds */
#define WM_SIN_N 4096
static float sSinTab[WM_SIN_N + 1];

static float wmSin(float ph) {
    float x;
    int i;

    if (sSinTab[WM_SIN_N / 4] == 0.0f) {
        for (i = 0; i <= WM_SIN_N; i++) {
            sSinTab[i] = sinf((float)i * TWO_PI / WM_SIN_N);
        }
    }
    x = ph * (WM_SIN_N / TWO_PI);
    x -= (float)WM_SIN_N * floorf(x / (float)WM_SIN_N);
    i = (int)x;
    if (i >= WM_SIN_N) {
        i = 0;
        x = 0.0f;
    }
    return sSinTab[i] + (sSinTab[i + 1] - sSinTab[i]) * (x - (float)i);
}

static float saturateDrive(float x, float drive) {
    float d = x * drive;
    float ax = d < 0.0f ? -d : d;
    return d / (1.0f + ax);
}

static float streamGain(void) {
    const WMTuning *t = WM_GetTuning();
    float master = (float)SDL_clamp(gPortConfig.volume, 0, 100) / 100.0f;
    float fx = SDL_clamp(t->scoreFxVolume, 0.0f, 2.0f);
    return SDL_clamp(master * fx, 0.0f, 2.0f);
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
    if (sOut == NULL || count <= 0) {
        return;
    }
    if (count > SCORE_MAX_SAMPLES) {
        count = SCORE_MAX_SAMPLES;
    }
    /* Keep latency razor-sharp: clear stale tail if queue exceeds ~120 ms */
    if (SDL_GetAudioStreamQueued(sOut) > (int)(SCORE_RATE * sizeof(s16) / 8)) {
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
        /* Tier 1 (1-15 pts): punchy arcade coin/pop with bright harmonics (~110 ms) */
        float ph1 = 0.0f, ph2 = 0.0f, ph3 = 0.0f;
        float env = 1.0f, envK = expf(-1.0f / (SCORE_RATE * 0.042f));
        n = (SCORE_RATE * 110) / 1000;
        for (i = 0; i < n; i++) {
            float t = (float)i / (float)n;
            float f = 784.0f + 392.0f * (t < 0.35f ? t / 0.35f : 1.0f);
            float atk = i < 24 ? (float)i / 24.0f : 1.0f;
            float click = i < 40 ? (1.0f - (float)i / 40.0f) * 0.35f : 0.0f;
            float s = (wmSin(ph1) * 0.65f + wmSin(ph2) * 0.30f + wmSin(ph3) * 0.15f) * env * atk + click;
            ph1 += TWO_PI * f / SCORE_RATE;
            ph2 += TWO_PI * (f * 2.0f) / SCORE_RATE;
            ph3 += TWO_PI * (f * 3.0f) / SCORE_RATE;
            env *= envK;
            sPcm[i] = (s16)(saturateDrive(s, 1.75f) * 32767.0f);
        }
    } else if (tier == 2) {
        /* Tier 2 (16-40 pts): chest-punch + bright metallic power chord (~190 ms) */
        float phSub = 0.0f, ph1 = 0.0f, ph2 = 0.0f, ph3 = 0.0f;
        float subEnv = 1.0f, subK = expf(-1.0f / (SCORE_RATE * 0.045f));
        float env = 1.0f, envK = expf(-1.0f / (SCORE_RATE * 0.068f));
        n = (SCORE_RATE * 190) / 1000;
        for (i = 0; i < n; i++) {
            float atk = i < 24 ? (float)i / 24.0f : 1.0f;
            float fSub = 145.0f - 85.0f * ((float)i / (float)n);
            float chord = (wmSin(ph1) * 0.45f + wmSin(ph2) * 0.35f + wmSin(ph3) * 0.30f) * env;
            float s = (wmSin(phSub) * subEnv * 0.55f + chord) * atk;
            phSub += TWO_PI * fSub / SCORE_RATE;
            ph1 += TWO_PI * 523.25f / SCORE_RATE;  /* C5 */
            ph2 += TWO_PI * 783.99f / SCORE_RATE;  /* G5 */
            ph3 += TWO_PI * 1046.50f / SCORE_RATE; /* C6 */
            subEnv *= subK;
            env *= envK;
            sPcm[i] = (s16)(saturateDrive(s, 1.85f) * 32767.0f);
        }
    } else if (tier == 3) {
        /* Tier 3 (41-99 pts): heavy sub-bass punch + frenetic 4-note arpeggio (~260 ms) */
        static const float kNotes4[4] = { 523.25f, 659.25f, 783.99f, 1046.50f };
        float phSub = 0.0f, phNote = 0.0f, phHarm = 0.0f, phOct = 0.0f;
        float subEnv = 1.0f, subK = expf(-1.0f / (SCORE_RATE * 0.055f));
        float noteEnv = 1.0f, noteK = expf(-1.0f / (SCORE_RATE * 0.065f));
        int stepLen = (SCORE_RATE * 46) / 1000;
        int prevStep = -1;
        n = (SCORE_RATE * 260) / 1000;
        for (i = 0; i < n; i++) {
            int step = i / stepLen;
            float fSub = 150.0f - 95.0f * ((float)i / (float)n);
            float fNote, s;
            if (step > 3) {
                step = 3;
            }
            if (step != prevStep) {
                noteEnv = 1.0f;
                prevStep = step;
            }
            fNote = kNotes4[step];
            s = wmSin(phSub) * subEnv * 0.65f +
                (wmSin(phNote) * 0.60f + wmSin(phHarm) * 0.35f + wmSin(phOct) * 0.20f) * noteEnv * 0.65f;
            phSub += TWO_PI * fSub / SCORE_RATE;
            phNote += TWO_PI * fNote / SCORE_RATE;
            phHarm += TWO_PI * (fNote * 2.0f) / SCORE_RATE;
            phOct += TWO_PI * (fNote * 3.0f) / SCORE_RATE;
            subEnv *= subK;
            noteEnv *= noteK;
            sPcm[i] = (s16)(saturateDrive(s, 1.90f) * 32767.0f);
        }
    } else {
        /* Tier 4 (100+ pts, Mega Crash): seismic punch + 5-note hyper jackpot cascade (~360 ms) */
        static const float kNotes5[5] = { 523.25f, 659.25f, 783.99f, 1046.50f, 1567.98f };
        float phSub = 0.0f, phNote = 0.0f, phHarm = 0.0f, phOct = 0.0f;
        float subEnv = 1.0f, subK = expf(-1.0f / (SCORE_RATE * 0.075f));
        float noteEnv = 1.0f, noteK = expf(-1.0f / (SCORE_RATE * 0.075f));
        int stepLen = (SCORE_RATE * 44) / 1000;
        int prevStep = -1;
        n = (SCORE_RATE * 360) / 1000;
        for (i = 0; i < n; i++) {
            int step = i / stepLen;
            float fSub = 165.0f - 115.0f * ((float)i / (float)n);
            float fNote, s;
            if (step > 4) {
                step = 4;
            }
            if (step != prevStep) {
                noteEnv = 1.0f;
                prevStep = step;
            }
            fNote = kNotes5[step];
            s = wmSin(phSub) * subEnv * 0.72f +
                (wmSin(phNote) * 0.58f + wmSin(phHarm) * 0.36f + wmSin(phOct) * 0.24f) * noteEnv * 0.72f;
            phSub += TWO_PI * fSub / SCORE_RATE;
            phNote += TWO_PI * fNote / SCORE_RATE;
            phHarm += TWO_PI * (fNote * 2.0f) / SCORE_RATE;
            phOct += TWO_PI * (fNote * 3.0f) / SCORE_RATE;
            subEnv *= subK;
            noteEnv *= noteK;
            sPcm[i] = (s16)(saturateDrive(s, 2.00f) * 32767.0f);
        }
    }

    submitPcm(n, gain);
}

void WM_ScoreAudioTick(int remaining, int peak) {
    Uint64 now = SDL_GetTicks();
    float progress, freq, ph1 = 0.0f, ph2 = 0.0f, ph3 = 0.0f;
    float env = 1.0f, envK, gain;
    int n, i;

    if (!ensureStream()) {
        return;
    }
    if (now - sLastTickMs < 16) {
        return;
    }
    sLastTickMs = now;
    gain = streamGain();
    if (gain <= 0.001f) {
        return;
    }
    /* Let a big hit chord finish its initial transient (~65 ms) before ticks cut in */
    if (SDL_GetAudioStreamQueued(sOut) > (int)(SCORE_RATE * sizeof(s16) / 10)) {
        return;
    }

    if (peak <= 0) {
        peak = 1;
    }
    progress = 1.0f - SDL_clamp((float)remaining / (float)peak, 0.0f, 1.0f);
    freq = 587.33f + progress * 892.67f; /* D5 (587 Hz) -> ~F#6 (1480 Hz) frenetic climb */
    n = (SCORE_RATE * 30) / 1000;
    envK = expf(-1.0f / (SCORE_RATE * 0.011f));
    for (i = 0; i < n; i++) {
        float atk = i < 16 ? (float)i / 16.0f : 1.0f;
        float click = i < 24 ? (1.0f - (float)i / 24.0f) * 0.25f : 0.0f;
        float s = (wmSin(ph1) * 0.62f + wmSin(ph2) * 0.28f + wmSin(ph3) * 0.16f) * env * atk + click;
        ph1 += TWO_PI * freq / SCORE_RATE;
        ph2 += TWO_PI * (freq * 2.0f) / SCORE_RATE;
        ph3 += TWO_PI * (freq * 3.0f) / SCORE_RATE;
        env *= envK;
        sPcm[i] = (s16)(saturateDrive(s, 1.75f) * 32767.0f);
    }
    submitPcm(n, gain);
}

void WM_ScoreAudioBanked(int peak) {
    float ph1 = 0.0f, ph2 = 0.0f, ph3 = 0.0f, env = 1.0f, envK, gain;
    int n, i;

    if (!ensureStream() || peak < 5) {
        return;
    }
    gain = streamGain();
    if (gain <= 0.001f) {
        return;
    }
    n = (SCORE_RATE * 170) / 1000;
    envK = expf(-1.0f / (SCORE_RATE * 0.060f));
    for (i = 0; i < n; i++) {
        float atk = i < 20 ? (float)i / 20.0f : 1.0f;
        float s = (wmSin(ph1) * 0.50f + wmSin(ph2) * 0.35f + wmSin(ph3) * 0.25f) * env * atk;
        ph1 += TWO_PI * 1046.50f / SCORE_RATE; /* C6 */
        ph2 += TWO_PI * 1567.98f / SCORE_RATE; /* G6 */
        ph3 += TWO_PI * 2093.00f / SCORE_RATE; /* C7 */
        env *= envK;
        sPcm[i] = (s16)(saturateDrive(s, 1.80f) * 32767.0f);
    }
    submitPcm(n, gain);
}
