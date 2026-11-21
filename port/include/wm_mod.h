#ifndef WM_MOD_H
#define WM_MOD_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Central tuning parameters for Wrecked Metal enhancements (Phase 0+).
 * Loaded from Data/wm_tuning.ini when not in Play Fidelity mode.
 */
typedef struct {
    int enabled;

    /* Quick Win 1.1: terrain-aware wheel smoke, dust, and dense rubber fog */
    int smokeMaxParticles;
    int emitOnDrift;
    int emitFrontWheels;
    float frontSlipThreshold;
    float stepDist;

    float rubberFogScale;
    float rubberFogGrowth;
    int rubberFogLife;
    int rubberFogAlpha;
    float rubberFogLift;

    float dirtScale;
    float dirtGrowth;
    int dirtLife;
    int dirtAlpha;
    float dirtLift;

    float snowScale;
    float snowGrowth;
    int snowLife;
    int snowAlpha;
    float snowLift;
} WMTuning;

/* Host-side tuning API (port/src/wm_tuning.c) */
int WM_Enabled(void);
void WM_ReloadTuning(void);
const WMTuning *WM_GetTuning(void);
int WM_SmokePoolMax(void);

/* Game-side hooks (port/game/wm_mod.c) */
int WM_EmitWheelDust(void *car, void *a, void *b);

#ifdef __cplusplus
}
#endif

#endif
