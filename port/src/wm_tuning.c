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

#define WM_TUNING_VERSION 2

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

    if (sTuning.tuningVersion < WM_TUNING_VERSION) {
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
