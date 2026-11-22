/*
 * Host-side tuning loader for Wrecked Metal custom features (Data/wm_tuning.ini).
 * Stays inactive whenever Play Fidelity (--fidelity) is selected.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "port_config.h"
#include "wm_mod.h"

static WMTuning sTuning;
static int sInitialized;

#define WM_TUNING_VERSION 7 /* 7: [hud] widescreen layout and smaller start lights */

/* [hud]: each group's top-left on a 1280x720 (16:9) screen; wider screens keep the left
   groups at the left edge and the right ones at the right edge (docs/HUD_LAYOUT.md) */
static const char *const kHudKeys[WM_HUD_GROUPS] = { "score", "time", "cars_left", "map", "damage" };
static const int kHudDefaults[WM_HUD_GROUPS][2] = {
    { 215, 12 },  /* score/place: top left, right of the L button */
    { -1, -1 },   /* time: the N64's, top centre */
    { 1028, 12 }, /* cars left: top right corner */
    { 12, 150 },  /* map: left edge, between L and the arrows */
    { 868, 92 },  /* damage gauge + T/B bars: right, above C and left of R */
};

/*
 * Weather per race track, by the game's track id (D_800958B4). Ids 0-3 are the
 * arenas and never get weather. The race menu lists the tracks in the order
 * 5 4 7 8 11 6 10 9 (the cups' order, D_80095CA8), so "track 0" for a player is
 * id 5 (Seascape Sprint). Weights: clear, rain, storm (rain + lightning), snow.
 */
static const int kWeatherDefaults[12][WM_WEATHER_KINDS] = {
    { 100, 0, 0, 0 },  /* 0 arena */
    { 100, 0, 0, 0 },  /* 1 arena */
    { 100, 0, 0, 0 },  /* 2 arena */
    { 100, 0, 0, 0 },  /* 3 arena */
    { 40, 30, 30, 0 }, /* 4  race #2: random */
    { 40, 30, 30, 0 }, /* 5  race #1 Seascape Sprint: random */
    { 0, 100, 0, 0 },  /* 6  race #6 (dirt canyon): always rain */
    { 40, 30, 30, 0 }, /* 7  race #3: random */
    { 40, 30, 30, 0 }, /* 8  race #4 (figure 8): random */
    { 100, 0, 0, 0 },  /* 9  race #8 (desert): never rain */
    { 0, 0, 0, 100 },  /* 10 race #7 (glacier, the N64's snow): always snow */
    { 0, 0, 0, 100 },  /* 11 race #5 (city at night): always snow */
};

static const char *const kWeatherNames[WM_WEATHER_KINDS] = { "clear", "rain", "storm", "snow" };

static void setDefaults(WMTuning *t) {
    t->enabled = 1;
    t->tuningVersion = WM_TUNING_VERSION;

    t->smokeMaxParticles = 280;
    t->emitOnDrift = 1;
    t->emitFrontWheels = 1;
    t->frontSlipThreshold = 0.72f;
    t->frontWheelScale = 0.30f;
    t->aiSmokeScale = 0.80f;
    t->stepDist = 5.20f;

    t->rubberFogScale = 2.35f;
    t->rubberFogGrowth = 0.30f;
    t->rubberFogLife = 22;
    t->rubberFogAlpha = 138;
    t->rubberFogLift = 0.07f;

    t->dirtScale = 2.20f;
    t->dirtGrowth = 0.28f;
    t->dirtLife = 20;
    t->dirtAlpha = 148;
    t->dirtLift = 0.08f;

    t->snowScale = 2.10f;
    t->snowGrowth = 0.26f;
    t->snowLife = 18;
    t->snowAlpha = 142;
    t->snowLift = 0.06f;

    t->scoreFxEnabled = 1;
    t->scoreFxScale = 2.35f;
    t->scoreFxAudio = 1;
    t->scoreFxVolume = 1.45f;
    /*
     * Initial random-weather attempt (rain, storms, synthesized thunder) is kept
     * intact in wm_weather.c / wm_thunder.c rather than discarded, but disabled
     * by default because it still needs further polish over time. Track 10
     * (glacier) still uses the new 3-layer parallax snow.
     */
    t->weatherRandom = 0;
    t->weatherLayers = 3;
    t->weatherDensity = 1.0f;
    memcpy(t->weatherChance, kWeatherDefaults, sizeof(t->weatherChance));

    t->hudLayout = 1;
    t->hudStartLights = 0.40f;
    memcpy(t->hudPos, kHudDefaults, sizeof(t->hudPos));
}

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static char *trimStr(char *s) {
    char *e;

    while (*s == ' ' || *s == '\t') {
        s++;
    }
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        *--e = '\0';
    }
    return s;
}

static void writeDefaultIni(const char *path, const WMTuning *t) {
    FILE *f = fopen(path, "w");

    if (f == NULL) {
        return;
    }
    fprintf(f, "; Wrecked Metal mod tuning (ignored in Play Fidelity mode).\n");
    fprintf(f, "; Edit on device and restart a race to iterate without recompiling.\n\n");
    fprintf(f, "[general]\n");
    fprintf(f, "tuning_version = %d\n", t->tuningVersion);
    fprintf(f, "enabled = %d\n\n", t->enabled);
    fprintf(f, "[smoke]\n");
    fprintf(f, "smoke_max_particles = %d\n", t->smokeMaxParticles);
    fprintf(f, "emit_on_drift = %d\n", t->emitOnDrift);
    fprintf(f, "emit_front_wheels = %d\n", t->emitFrontWheels);
    fprintf(f, "front_slip_threshold = %.2f\n", t->frontSlipThreshold);
    fprintf(f, "front_wheel_scale = %.2f\n", t->frontWheelScale);
    fprintf(f, "ai_smoke_scale = %.2f\n", t->aiSmokeScale);
    fprintf(f, "step_dist = %.2f\n", t->stepDist);
    fprintf(f, "rubber_fog_scale = %.2f\n", t->rubberFogScale);
    fprintf(f, "rubber_fog_growth = %.2f\n", t->rubberFogGrowth);
    fprintf(f, "rubber_fog_life = %d\n", t->rubberFogLife);
    fprintf(f, "rubber_fog_alpha = %d\n", t->rubberFogAlpha);
    fprintf(f, "rubber_fog_lift = %.2f\n", t->rubberFogLift);
    fprintf(f, "dirt_scale = %.2f\n", t->dirtScale);
    fprintf(f, "dirt_growth = %.2f\n", t->dirtGrowth);
    fprintf(f, "dirt_life = %d\n", t->dirtLife);
    fprintf(f, "dirt_alpha = %d\n", t->dirtAlpha);
    fprintf(f, "dirt_lift = %.2f\n", t->dirtLift);
    fprintf(f, "snow_scale = %.2f\n", t->snowScale);
    fprintf(f, "snow_growth = %.2f\n", t->snowGrowth);
    fprintf(f, "snow_life = %d\n", t->snowLife);
    fprintf(f, "snow_alpha = %d\n", t->snowAlpha);
    fprintf(f, "snow_lift = %.2f\n", t->snowLift);
    fprintf(f, "\n[score_fx]\n");
    fprintf(f, "enabled = %d\n", t->scoreFxEnabled);
    fprintf(f, "scale = %.2f\n", t->scoreFxScale);
    fprintf(f, "audio = %d\n", t->scoreFxAudio);
    fprintf(f, "volume = %.2f\n", t->scoreFxVolume);
    fprintf(f, "\n[weather]\n");
    fprintf(f, "; random_weather = 0 (default): new 3-layer parallax snow on the glacier only.\n");
    fprintf(f, "; Experimental rain/storm/thunder is kept in code for future refinement (set 1 to try).\n");
    fprintf(f, "; layers = 1-3 particle depths (parallax); density = particle count multiplier.\n");
    fprintf(f, "; track_N = weights for clear rain storm snow, N = the game's track id:\n");
    fprintf(f, ";   race menu order 5 4 7 8 11 6 10 9 (5 = Seascape Sprint); 0-3 are arenas (no weather).\n");
    fprintf(f, "random_weather = %d\n", t->weatherRandom);
    fprintf(f, "layers = %d\n", t->weatherLayers);
    fprintf(f, "density = %.2f\n", t->weatherDensity);
    {
        int i;
        for (i = 4; i < 12; i++) {
            fprintf(f, "track_%d = %d %d %d %d\n", i, t->weatherChance[i][0], t->weatherChance[i][1],
                    t->weatherChance[i][2], t->weatherChance[i][3]);
        }
    }
    fprintf(f, "\n[hud]\n");
    fprintf(f, "; Widescreen HUD (16:9 and wider). Each group's top-left in pixels of a 1280x720 screen\n");
    fprintf(f, "; (template: docs/HUD_LAYOUT.md); -1 -1 = where the N64 had it. score and map keep to the\n");
    fprintf(f, "; left edge, cars_left and damage to the right edge, time to the centre. layout = 0: the N64's HUD.\n");
    fprintf(f, "; start_lights = the size of the red / yellow / green start lights (1.00 = the N64's).\n");
    fprintf(f, "layout = %d\n", t->hudLayout);
    fprintf(f, "start_lights = %.2f\n", t->hudStartLights);
    {
        int i;
        for (i = 0; i < WM_HUD_GROUPS; i++) {
            fprintf(f, "%s = %d %d\n", kHudKeys[i], t->hudPos[i][0], t->hudPos[i][1]);
        }
    }
    fclose(f);
}

static void applyKey(WMTuning *t, const char *section, const char *key, const char *val) {
    if (SDL_strcasecmp(section, "general") == 0) {
        if (SDL_strcasecmp(key, "enabled") == 0) {
            t->enabled = atoi(val) != 0;
        } else if (SDL_strcasecmp(key, "tuning_version") == 0) {
            t->tuningVersion = atoi(val);
        }
        return;
    }
    if (SDL_strcasecmp(section, "score_fx") == 0) {
        if (SDL_strcasecmp(key, "enabled") == 0) {
            t->scoreFxEnabled = atoi(val) != 0;
        } else if (SDL_strcasecmp(key, "scale") == 0) {
            t->scoreFxScale = clampf((float)atof(val), 1.0f, 3.5f);
        } else if (SDL_strcasecmp(key, "audio") == 0) {
            t->scoreFxAudio = atoi(val) != 0;
        } else if (SDL_strcasecmp(key, "volume") == 0) {
            t->scoreFxVolume = clampf((float)atof(val), 0.0f, 2.0f);
        }
        return;
    }
    if (SDL_strcasecmp(section, "hud") == 0) {
        int i;
        if (SDL_strcasecmp(key, "layout") == 0) {
            t->hudLayout = atoi(val) != 0;
        } else if (SDL_strcasecmp(key, "start_lights") == 0) {
            t->hudStartLights = clampf((float)atof(val), 0.10f, 1.50f);
        } else {
            for (i = 0; i < WM_HUD_GROUPS; i++) {
                int x, y;
                if (SDL_strcasecmp(key, kHudKeys[i]) == 0 && sscanf(val, "%d %d", &x, &y) == 2) {
                    t->hudPos[i][0] = clampi(x, -1, 1280);
                    t->hudPos[i][1] = clampi(y, -1, 720);
                }
            }
        }
        return;
    }
    if (SDL_strcasecmp(section, "weather") == 0) {
        if (SDL_strcasecmp(key, "random_weather") == 0) {
            t->weatherRandom = atoi(val) != 0;
        } else if (SDL_strcasecmp(key, "layers") == 0) {
            t->weatherLayers = clampi(atoi(val), 1, 3);
        } else if (SDL_strcasecmp(key, "density") == 0) {
            t->weatherDensity = clampf((float)atof(val), 0.1f, 2.0f);
        } else if (SDL_strncasecmp(key, "track_", 6) == 0) {
            int id = atoi(key + 6), w[WM_WEATHER_KINDS] = { 0, 0, 0, 0 }, i;

            if (id >= 4 && id < 12 && sscanf(val, "%d %d %d %d", &w[0], &w[1], &w[2], &w[3]) >= 1) {
                for (i = 0; i < WM_WEATHER_KINDS; i++) {
                    t->weatherChance[id][i] = clampi(w[i], 0, 1000);
                }
            }
        }
        return;
    }
    if (SDL_strcasecmp(section, "smoke") != 0) {
        return;
    }
    if (SDL_strcasecmp(key, "smoke_max_particles") == 0) {
        t->smokeMaxParticles = clampi(atoi(val), 100, 1200);
    } else if (SDL_strcasecmp(key, "emit_on_drift") == 0) {
        t->emitOnDrift = atoi(val) != 0;
    } else if (SDL_strcasecmp(key, "emit_front_wheels") == 0) {
        t->emitFrontWheels = atoi(val) != 0;
    } else if (SDL_strcasecmp(key, "front_slip_threshold") == 0) {
        t->frontSlipThreshold = clampf((float)atof(val), 0.05f, 1.0f);
    } else if (SDL_strcasecmp(key, "front_wheel_scale") == 0) {
        t->frontWheelScale = clampf((float)atof(val), 0.10f, 1.0f);
    } else if (SDL_strcasecmp(key, "ai_smoke_scale") == 0) {
        t->aiSmokeScale = clampf((float)atof(val), 0.20f, 1.0f);
    } else if (SDL_strcasecmp(key, "step_dist") == 0) {
        t->stepDist = clampf((float)atof(val), 1.5f, 12.0f);
    } else if (SDL_strcasecmp(key, "rubber_fog_scale") == 0) {
        t->rubberFogScale = clampf((float)atof(val), 1.0f, 15.0f);
    } else if (SDL_strcasecmp(key, "rubber_fog_growth") == 0) {
        t->rubberFogGrowth = clampf((float)atof(val), 0.0f, 3.0f);
    } else if (SDL_strcasecmp(key, "rubber_fog_life") == 0) {
        t->rubberFogLife = clampi(atoi(val), 8, 90);
    } else if (SDL_strcasecmp(key, "rubber_fog_alpha") == 0) {
        t->rubberFogAlpha = clampi(atoi(val), 32, 255);
    } else if (SDL_strcasecmp(key, "rubber_fog_lift") == 0) {
        t->rubberFogLift = clampf((float)atof(val), 0.0f, 3.0f);
    } else if (SDL_strcasecmp(key, "dirt_scale") == 0) {
        t->dirtScale = clampf((float)atof(val), 1.0f, 15.0f);
    } else if (SDL_strcasecmp(key, "dirt_growth") == 0) {
        t->dirtGrowth = clampf((float)atof(val), 0.0f, 3.0f);
    } else if (SDL_strcasecmp(key, "dirt_life") == 0) {
        t->dirtLife = clampi(atoi(val), 8, 90);
    } else if (SDL_strcasecmp(key, "dirt_alpha") == 0) {
        t->dirtAlpha = clampi(atoi(val), 32, 255);
    } else if (SDL_strcasecmp(key, "dirt_lift") == 0) {
        t->dirtLift = clampf((float)atof(val), 0.0f, 3.0f);
    } else if (SDL_strcasecmp(key, "snow_scale") == 0) {
        t->snowScale = clampf((float)atof(val), 1.0f, 15.0f);
    } else if (SDL_strcasecmp(key, "snow_growth") == 0) {
        t->snowGrowth = clampf((float)atof(val), 0.0f, 3.0f);
    } else if (SDL_strcasecmp(key, "snow_life") == 0) {
        t->snowLife = clampi(atoi(val), 8, 90);
    } else if (SDL_strcasecmp(key, "snow_alpha") == 0) {
        t->snowAlpha = clampi(atoi(val), 32, 255);
    } else if (SDL_strcasecmp(key, "snow_lift") == 0) {
        t->snowLift = clampf((float)atof(val), 0.0f, 3.0f);
    }
}

void WM_ReloadTuning(void) {
    char dir[1024], path[1100], section[32] = "";
    size_t size = 0;
    char *data, *p;

    setDefaults(&sTuning);
    sInitialized = 1;
    if (gPortConfig.fidelity) {
        return;
    }

    SDL_snprintf(dir, sizeof(dir), "%sData", Port_BaseDir());
    SDL_CreateDirectory(dir);
    SDL_snprintf(path, sizeof(path), "%s/wm_tuning.ini", dir);

    data = (char *)SDL_LoadFile(path, &size);
    if (data == NULL) {
        writeDefaultIni(path, &sTuning);
        Port_Log("wm_mod: created default %s (smoke_max=%d, drift=%d, front=%d, step=%.1f, fog_life=%d)",
                 path, sTuning.smokeMaxParticles, sTuning.emitOnDrift, sTuning.emitFrontWheels,
                 sTuning.stepDist, sTuning.rubberFogLife);
        return;
    }

    sTuning.tuningVersion = 0; /* detect pre-v2 wm_tuning.ini files */
    p = data;
    while (*p != '\0') {
        char line[256];
        size_t n = strcspn(p, "\n");
        char *s, *eq;

        if (n >= sizeof(line)) {
            n = sizeof(line) - 1;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        p += strcspn(p, "\n");
        if (*p == '\n') {
            p++;
        }
        s = trimStr(line);
        if (*s == '\0' || *s == ';' || *s == '#') {
            continue;
        }
        if (*s == '[') {
            char *e = strchr(s, ']');
            if (e != NULL) {
                *e = '\0';
                SDL_strlcpy(section, trimStr(s + 1), sizeof(section));
            }
            continue;
        }
        eq = strchr(s, '=');
        if (eq != NULL) {
            *eq = '\0';
            applyKey(&sTuning, section, trimStr(s), trimStr(eq + 1));
        }
    }
    SDL_free(data);

    if (sTuning.tuningVersion >= 2 && sTuning.tuningVersion < WM_TUNING_VERSION) {
        if (sTuning.tuningVersion <= 4 && sTuning.scoreFxScale < 2.0f) {
            sTuning.scoreFxScale = 2.35f;
        }
        if (sTuning.tuningVersion <= 4 && sTuning.scoreFxVolume < 1.35f) {
            sTuning.scoreFxVolume = 1.45f;
        }
        /* [weather] arrived on with random weather (v3); it is off by default now, the
           new parallax snow staying on track 10 */
        if (sTuning.tuningVersion <= 3) {
            sTuning.weatherRandom = 0;
        }
        sTuning.tuningVersion = WM_TUNING_VERSION;
        writeDefaultIni(path, &sTuning);
        Port_Log("wm_mod: updated [score_fx], [weather] and [hud] in %s (v%d, random_weather=%d)", path,
                 WM_TUNING_VERSION, sTuning.weatherRandom);
    } else if (sTuning.tuningVersion < WM_TUNING_VERSION) {
        int keepEnabled = sTuning.enabled;
        setDefaults(&sTuning);
        sTuning.enabled = keepEnabled;
        writeDefaultIni(path, &sTuning);
        Port_Log("wm_mod: upgraded %s to v%d 60fps defaults (smoke_max=%d, front=%.2f, ai=%.2f)",
                 path, WM_TUNING_VERSION, sTuning.smokeMaxParticles, sTuning.frontWheelScale,
                 sTuning.aiSmokeScale);
        return;
    }

    Port_Log("wm_mod: tuning loaded (v%d, enabled=%d, smoke_max=%d, step=%.1f, front=%.2f, ai=%.2f)",
             sTuning.tuningVersion, sTuning.enabled, sTuning.smokeMaxParticles, sTuning.stepDist,
             sTuning.frontWheelScale, sTuning.aiSmokeScale);
}

int WM_Enabled(void) {
    if (!sInitialized) {
        WM_ReloadTuning();
    }
    return !gPortConfig.fidelity && sTuning.enabled;
}

const WMTuning *WM_GetTuning(void) {
    if (!sInitialized) {
        WM_ReloadTuning();
    }
    return &sTuning;
}

int WM_SmokePoolMax(void) {
    WM_ReloadTuning();
    return WM_Enabled() ? sTuning.smokeMaxParticles : 0xC8;
}

int WM_HudBarsEnabled(void) {
    return !gPortConfig.fidelity && gPortConfig.devHudBars != 0;
}

float WM_BrakeForceMult(void) {
    if (gPortConfig.fidelity) {
        return 1.0f;
    }
    return clampi(gPortConfig.devBrakeForce, 50, 250) * 0.01f;
}

float WM_BrakeBiteMult(void) {
    if (gPortConfig.fidelity) {
        return 1.0f;
    }
    return clampi(gPortConfig.devBrakeBite, 40, 300) * 0.01f;
}

float WM_BrakeLockThresholdVal(void) {
    if (gPortConfig.fidelity) {
        return 10.0f;
    }
    return (float)clampi(gPortConfig.devBrakeLock, 0, 30);
}

float WM_BrakeLatGripMult(void) {
    if (gPortConfig.fidelity) {
        return 1.0f;
    }
    return clampi(gPortConfig.devBrakeLatGrip, 50, 150) * 0.01f;
}

float WM_HandbrakeGripVal(void) {
    if (gPortConfig.fidelity) {
        return 0.55f;
    }
    return clampi(gPortConfig.devHbrakeGrip, 15, 100) * 0.01f;
}

float WM_HandbrakeDragMult(void) {
    if (gPortConfig.fidelity) {
        return 1.0f;
    }
    return clampi(gPortConfig.devHbrakeDrag, 25, 250) * 0.01f;
}

float WM_HandbrakeSlipVal(void) {
    if (gPortConfig.fidelity) {
        return 1.0f;
    }
    return clampi(gPortConfig.devHbrakeSlip, 5, 100) * 0.1f;
}

/* ---- Weather ---------------------------------------------------------------- */

const char *WM_WeatherName(int kind) {
    return kind >= 0 && kind < WM_WEATHER_KINDS ? kWeatherNames[kind] : "n64";
}

/* Reusable weighted roll: picks a weather kind from `chance` (weights for clear,
   rain, storm, snow; any scale) with the xorshift state `rng` (seeded if 0).
   All-zero weights give clear. */
int WM_WeatherRoll(const int chance[WM_WEATHER_KINDS], unsigned int *rng) {
    unsigned int x = *rng, total = 0, r;
    int i;

    if (x == 0) {
        x = (unsigned int)SDL_GetPerformanceCounter() | 1u;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    for (i = 0; i < WM_WEATHER_KINDS; i++) {
        total += (unsigned int)(chance[i] > 0 ? chance[i] : 0);
    }
    if (total == 0) {
        return WM_WEATHER_CLEAR;
    }
    r = x % total;
    for (i = 0; i < WM_WEATHER_KINDS; i++) {
        unsigned int w = (unsigned int)(chance[i] > 0 ? chance[i] : 0);
        if (r < w) {
            return i;
        }
        r -= w;
    }
    return WM_WEATHER_CLEAR;
}

/* The weather for a race about to start on `track` (D_800958B4) in game type
   `gameType` (D_800958A8; 3 = multiplayer, which the game draws no weather in).
   WM_WEATHER_N64 = leave it to the game's code (Play Fidelity). DD64_WEATHER =
   clear/rain/storm/snow forces a kind on race tracks (tests). */
int WM_WeatherPick(int track, int gameType) {
    static unsigned int sRng;
    static int sLastTrack = -1, sLastType = -1, sLastKind;
    static Uint64 sLastTicks;
    const char *force = SDL_getenv("DD64_WEATHER");
    int i;

    if (!WM_Enabled()) {
        return WM_WEATHER_N64;
    }
    WM_ReloadTuning(); /* edits to the ini apply from the next race */
    if (track < 4 || track >= 12 || gameType == 3) {
        return WM_WEATHER_CLEAR; /* arenas and arena modes: none */
    }
    if (force != NULL && *force != '\0') {
        for (i = 0; i < WM_WEATHER_KINDS; i++) {
            if (SDL_strcasecmp(force, kWeatherNames[i]) == 0) {
                return i;
            }
        }
    }
    if (!sTuning.weatherRandom) {
        return track == 10 ? WM_WEATHER_SNOW : WM_WEATHER_CLEAR;
    }
    /* The race's scene is set up twice in a row as it loads (Render_Init runs
       Weather_Init each time): keep the first roll for that race */
    if (track == sLastTrack && gameType == sLastType && SDL_GetTicks() - sLastTicks < 15000) {
        sLastTicks = SDL_GetTicks();
        return sLastKind;
    }
    sLastKind = WM_WeatherRoll(sTuning.weatherChance[track], &sRng);
    sLastTrack = track;
    sLastType = gameType;
    sLastTicks = SDL_GetTicks();
    return sLastKind;
}
