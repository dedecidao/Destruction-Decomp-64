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
    int tuningVersion;

    /* Quick Win 1.1: terrain-aware wheel smoke, dust, and dense rubber fog */
    int smokeMaxParticles;
    int emitOnDrift;
    int emitFrontWheels;
    float frontSlipThreshold;
    float frontWheelScale;
    float aiSmokeScale;
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

    /* Score Dopamine HUD & procedural audio ([score_fx] in wm_tuning.ini) */
    int scoreFxEnabled;         /* 1 = central green popup + countdown + audio */
    float scoreFxScale;         /* base scale of the central score popup (1.0 - 2.5) */
    int scoreFxAudio;           /* 1 = procedural tiered score audio */
    float scoreFxVolume;        /* 0.0 - 1.5 relative to master volume */
    /* Weather ([weather] in wm_tuning.ini, port/game/wm_weather.c) */
    int weatherRandom;          /* 0 = the N64's weather (snow on track 10 only) */
    int weatherLayers;          /* 1-3 particle layers (parallax) */
    float weatherDensity;       /* particle count multiplier */
    int weatherChance[12][4];   /* per track id: clear, rain, storm, snow (weights) */
    /* Widescreen HUD ([hud] in wm_tuning.ini, docs/HUD_LAYOUT.md) */
    int hudLayout;              /* 1 = the HUD groups at hudPos, 0 = the N64's places */
    float hudStartLights;       /* the start lights' size, 1.0 = the N64's */
    int hudPos[6][2];           /* WM_HUD_*: top-left on a 1280x720 screen, -1 = the N64's */
    float hudScale[6];          /* WM_HUD_*: size, 1.0 = the N64's */
    /* Car lights ([lights] in wm_tuning.ini) */
    int lightsAllTracks;        /* the night tracks' head and tail lights on every car, every track */
    int lightsGround;           /* with them, the player's headlights light the road */
    /* Computer cars ([ai] in wm_tuning.ini, port/game/ai_director.c) */
    int aiMode;                 /* WM_AI_CLASSIC, WM_AI_PACK_RACE, WM_AI_HEAD_ON */
    int aiDirector;             /* the race director in the pack and head-on modes */
    int aiFixBugs;              /* the two AI aiming bugs fixed (docs/bugs.md 19, 20) */
} WMTuning;

/* How the race tracks' computer cars start and drive ([ai] mode) */
enum { WM_AI_CLASSIC, WM_AI_PACK_RACE, WM_AI_HEAD_ON, WM_AI_MODES };

/* The HUD groups the widescreen layout moves ([hud] in wm_tuning.ini) */
enum { WM_HUD_SCORE, WM_HUD_TIME, WM_HUD_CARS, WM_HUD_MAP, WM_HUD_DAMAGE, WM_HUD_PEDALS, WM_HUD_GROUPS };

/* Weather kinds (WM_WeatherRoll / WM_WeatherPick) */
enum {
    WM_WEATHER_N64 = -1, /* Play Fidelity: the game's own weather code */
    WM_WEATHER_CLEAR = 0,
    WM_WEATHER_RAIN,
    WM_WEATHER_STORM, /* rain + lightning and thunder */
    WM_WEATHER_SNOW,
    WM_WEATHER_KINDS
};

/* Host-side tuning API (port/src/wm_tuning.c) */
int WM_Enabled(void);
void WM_ReloadTuning(void);
const WMTuning *WM_GetTuning(void);
int WM_SmokePoolMax(void);
int WM_HudBarsEnabled(void);
float WM_BrakeForceMult(void);
float WM_BrakeBiteMult(void);
float WM_BrakeLockThresholdVal(void);
float WM_BrakeLatGripMult(void);
float WM_HandbrakeGripVal(void);
float WM_HandbrakeDragMult(void);
float WM_HandbrakeSlipVal(void);
void WM_ScoreAudioOpen(void);
void WM_ScoreAudioClose(void);
void WM_ScoreAudioHit(int added, int pendingTotal);
void WM_ScoreAudioTick(int remaining, int peak);
void WM_ScoreAudioBanked(int peak);
int WM_WeatherRoll(const int chance[WM_WEATHER_KINDS], unsigned int *rng);
int WM_WeatherPick(int track, int gameType);
const char *WM_WeatherName(int kind);
void WM_ThunderOpen(void);
void WM_ThunderClose(void);
void WM_ThunderPlay(float dist);

/* Game-side hooks (port/game/wm_mod.c, port/game/wm_score.c) */
int WM_EmitWheelDust(void *car, void *a, void *b);
int WM_EmitHoodSmoke(void *car, void *pos, void *mtx);
int WM_HoodSmokeStartHp(void);
void WM_HeadlightFade(void *car, void *dl);
void WM_SetLightCar(void *car);
float WM_LightMul(void);
void WM_EmitSparks(void *pool, void *car, float offX, float offY, float offZ, float force, void *otherCar);
int WM_NightCarLights(int track);
int WM_NightGroundLight(int track);
int WM_CanInitAiSkid(void);
int WM_AiMode(void);
int WM_AiDirectorOn(void);
int WM_AiFixBugs(void);
int WM_SkidShouldUpdate(void *skidMgr);
int WM_SkidShouldDraw(void *skidMgr);
int WM_SkidClipLife(void);
void WM_SkidTagSurface(void *skidMgr, void *tri);
void WM_SkidColorAlpha(int surface, int life, unsigned char *r, unsigned char *g, unsigned char *b,
                       unsigned char *a);
void *WM_HudDraw(void *gfx);
float WM_BrakeForceScale(void *car);
float WM_BrakeBiteScale(void *car);
float WM_BrakeLockThreshold(void *car);
float WM_BrakeLatGripScale(void *car);
float WM_HandbrakeLatGrip(void *car);
float WM_HandbrakeLonDrag(void *car);
float WM_HandbrakeSlipThreshold(void *car);
void WM_ScoreReset(void);
int WM_ScoreStep(int *curScore, int targetScore, int player);
void *WM_ScoreDraw(void *gfxPtr);
int WM_WeatherInit(void);
void WM_WeatherFree(void);
int WM_WeatherActive(void);
int WM_WeatherStorm(void);
void *WM_WeatherDraw(void *cam, void *gfx);
/* Widescreen HUD: the offset a 2D element is drawn with (quarter N64 pixels) and its scale */
void WM_HudOffset(int id, int *dx4, int *dy4, int *scale4096);
float WM_HudScale(int id);
void WM_HudGroupOffset(int group, int *dx4, int *dy4, int *scale4096);

#ifdef __cplusplus
}
#endif

#endif
