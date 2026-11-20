/*
 * The PC options: pages of settings for the in-game PC options page
 * (port/game/pcmenu.c draws it), each a [section] key of dd64.ini. A change takes
 * effect at once where it can; the ones that touch the window or the renderer
 * are applied on the main thread (Port_OptionsApplyPending), which also saves
 * the file. Rows marked "restart" take effect the next time the game starts.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "port.h"
#include "port_config.h"

typedef struct {
    const char *label;
    int *field;
    const int *values; /* the setting's value for each choice */
    const char *const *names;
    int count;
    int video;         /* applied on the main thread (the window, the renderer) */
    int restart;
} Option;

#define N(a) (int)(sizeof(a) / sizeof((a)[0]))

static const int kOffOn[] = { 0, 1 };
static const char *const kOffOnN[] = { "off", "on" };

static const int kDisplay[] = { 0, 1, 2 }; /* window, borderless fullscreen, exclusive (two fields) */
static const char *const kDisplayN[] = { "window", "fullscreen", "exclusive" };
static int sDisplay;

#ifndef __ANDROID__
static const int kWindow[] = { 0, 1, 2, 3, 4, 5 };
static const char *const kWindowN[] = { "960x720", "1280x720", "1280x960", "1600x900", "1920x1080", "2560x1440" };
static const int kWindowW[] = { 960, 1280, 1280, 1600, 1920, 2560 };
static const int kWindowH[] = { 720, 720, 960, 900, 1080, 1440 };
#else
/* Android: the output resolution (GameActivity sizes the surface by its height,
   the width following the screen's shape); takes effect at the next start */
static const int kWindow[] = { 0, 1 };
static const char *const kWindowN[] = { "720p", "1080p (full hd)" };
static const int kWindowW[] = { 1280, 1920 };
static const int kWindowH[] = { 720, 1080 };
#endif
static int sWindow;

#ifndef __ANDROID__
static const int kAspect[] = { 0, 1, 2 }; /* 4:3, widescreen, stretched (two fields) */
static const char *const kAspectN[] = { "4:3", "widescreen", "stretched" };
#else
static const int kAspect[] = { 0, 1 }; /* Android: no "stretched" */
static const char *const kAspectN[] = { "4:3", "widescreen" };
#endif
static int sAspect;

static const int kVsync[] = { 0, 1, -1, 2 };
static const char *const kVsyncN[] = { "off", "on", "adaptive", "VRR" };
static const int kFps[] = { 0, 30, 40, 60, 75, 90, 120, 144, 165, 240 };
static const char *const kFpsN[] = { "no limit", "30", "40", "60", "75", "90", "120", "144", "165", "240" };
static const int kInterp[] = { 0, 1, 2 };
static const char *const kInterpN[] = { "off", "smooth", "low latency" };
#ifndef __ANDROID__
static const int kMsaa[] = { 1, 2, 4, 8 };
static const char *const kMsaaN[] = { "off", "2x", "4x", "8x" };
#else
/* Android: 2x by default (cheap on tile-based GPUs, the samples stay in tile
   memory; 4x is black with SDL_gpu on the Adreno 740, app/.../android/gpu_caps.c) */
static const int kMsaa[] = { 1, 2 };
static const char *const kMsaaN[] = { "off", "2x" };
#endif
#ifndef __ANDROID__
static const int kDistance[] = { 1, 2, 4, 8, 16 };
static const char *const kDistanceN[] = { "n64", "x2", "x4", "x8", "x16" };
#else
static const int kDistance[] = { 8, 16 }; /* Android: geometry is cheap, x16 by default */
static const char *const kDistanceN[] = { "x8", "x16" };
#endif
static const int kFilter[] = { 0, 1, 2 };
static const char *const kFilterN[] = { "nearest", "bilinear", "n64 3-point" };
static const int kAniso[] = { 1, 2, 4, 8, 16 };
static const char *const kAnisoN[] = { "off", "2x", "4x", "8x", "16x" };
static const int kRenderer[] = { 0, 1 };
#ifndef __ANDROID__
static const char *const kRendererN[] = { "opengl", "sdl gpu - vulkan" };
#else
/* Android: OpenGL ES runs (gfx_opengl.cpp) but at a few fps on the Adreno 740 */
static const char *const kRendererN[] = { "opengl es (slow)", "vulkan" };
#endif
#ifndef __ANDROID__
static const int kRes[] = { 0, 240, 480, 720, 1080 };
static const char *const kResN[] = { "window", "240 - n64", "480", "720", "1080" };
#else
static const int kRes[] = { 720, 1080 }; /* Android: 720p by default, 1080p for faster phones */
static const char *const kResN[] = { "720p", "1080p" };
#endif

static const int kVolume[] = { 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
static const char *const kVolumeN[] = { "0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" };
static const int kSynth[] = { 22050, 44100 };
static const char *const kSynthN[] = { "n64 - 22 khz", "high - 44 khz" };
static const int kLatency[] = { 40, 60, 80, 100, 150 };
static const char *const kLatencyN[] = { "40 ms", "60 ms", "80 ms", "100 ms", "150 ms" };

static const int kStrength[] = { 25, 50, 75, 100 };
static const char *const kStrengthN[] = { "25%", "50%", "75%", "100%" };
static const int kDeadzone[] = { 0, 4, 8, 12, 16, 20, 25, 30 };
static const char *const kDeadzoneN[] = { "0%", "4%", "8%", "12%", "16%", "20%", "25%", "30%" };
static const int kRange[] = { 64, 70, 75, 80, 85, 90, 100, 110, 127 };
static const char *const kRangeN[] = { "64", "70", "75", "80 - n64", "85", "90", "100", "110", "127" };
static const int kKeyboard[] = { 0, 1, 2, 3, 4 };
static const char *const kKeyboardN[] = { "nobody", "player 1", "player 2", "player 3", "player 4" };
static const int kDrones[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
static const char *const kDronesN[] = { "1x", "2x", "3x", "4x", "5x", "6x", "7x", "8x", "9x", "10x" };
static const int kDroneCar[] = { -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };
static const char *const kDroneCarN[] = { "default", "car 1", "car 2", "car 3", "car 4", "car 5", "car 6",
                                          "car 7", "car 8", "car 9", "car 10", "car 11", "car 12",
                                          "bonus car 1", "bonus car 2", "bonus car 3", "bonus car 4",
                                          "bonus car 5", "bonus car 6", "bonus car 7", "bonus car 8",
                                          "bonus car 9", "bonus car 10", "bonus car 11", "bonus car 12" };
static const int kPaks[] = { 0, 1 };
static const char *const kSteerN[] = { "analog stick", "arrows" };
static const char *const kPaksN[] = { "player 1 only", "every player" };

#ifndef __ANDROID__
static const Option kVideo[] = {
    { "display", &sDisplay, kDisplay, kDisplayN, N(kDisplay), 1, 0 },
    { "window size", &sWindow, kWindow, kWindowN, N(kWindow), 1, 0 },
    { "screen shape", &sAspect, kAspect, kAspectN, N(kAspect), 1, 0 },
    { "vsync", &gPortConfig.vsync, kVsync, kVsyncN, N(kVsync), 1, 0 },
    { "frame limit", &gPortConfig.maxFps, kFps, kFpsN, N(kFps), 1, 0 },
    { "interpolation", &gPortConfig.interpolation, kInterp, kInterpN, N(kInterp), 1, 0 },
    { "3d resolution", &gPortConfig.internalHeight, kRes, kResN, N(kRes), 1, 0 },
    { "anti-aliasing", &gPortConfig.msaa, kMsaa, kMsaaN, N(kMsaa), 1, 0 },
    { "n64 aa blur", &gPortConfig.n64AA, kOffOn, kOffOnN, N(kOffOn), 1, 0 },
    { "texture filter", &gPortConfig.textureFilter, kFilter, kFilterN, N(kFilter), 1, 0 },
    { "clean cut-outs", &gPortConfig.edgeFill, kOffOn, kOffOnN, N(kOffOn), 1, 0 },
    { "hd fonts", &gPortConfig.hdFonts, kOffOn, kOffOnN, N(kOffOn), 1, 0 },
    { "hud boxes", &gPortConfig.hudBoxes, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    { "seamless sky", &gPortConfig.seamlessSky, kOffOn, kOffOnN, N(kOffOn), 1, 0 },
    { "draw distance", &gPortConfig.drawDistance, kDistance, kDistanceN, N(kDistance), 1, 0 },
    { "anisotropy", &gPortConfig.anisotropy, kAniso, kAnisoN, N(kAniso), 1, 1 },
    { "fps counter", &gPortConfig.showFps, kOffOn, kOffOnN, N(kOffOn), 1, 0 },
    { "renderer", &gPortConfig.renderer, kRenderer, kRendererN, N(kRenderer), 0, 1 },
};
static const Option kAudio[] = {
    { "master volume", &gPortConfig.volume, kVolume, kVolumeN, N(kVolume), 0, 0 },
    { "synth quality", &gPortConfig.synthRate, kSynth, kSynthN, N(kSynth), 0, 1 },
    { "audio delay", &gPortConfig.audioLatency, kLatency, kLatencyN, N(kLatency), 0, 1 },
};
static const Option kGame[] = {
    { "drones", &gPortConfig.drones, kDrones, kDronesN, N(kDrones), 0, 1 },
    { "set all drones to", &gPortConfig.droneCar, kDroneCar, kDroneCarN, N(kDroneCar), 0, 0 },
    { "announce every wreck", &gPortConfig.announceAllWrecks, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    { "cpu unlockables", &gPortConfig.cpuUnlockables, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    { "skip intros", &gPortConfig.skipIntros, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
};
static const Option kControls[] = {
    { "rumble", &gPortConfig.rumble, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    { "rumble strength", &gPortConfig.rumbleStrength, kStrength, kStrengthN, N(kStrength), 0, 0 },
    { "stick dead zone", &gPortConfig.deadzone, kDeadzone, kDeadzoneN, N(kDeadzone), 0, 0 },
    { "stick range", &gPortConfig.stickRange, kRange, kRangeN, N(kRange), 0, 0 },
    { "right stick camera", &gPortConfig.freeCamera, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    { "camera dead zone", &gPortConfig.cameraDeadzone, kDeadzone, kDeadzoneN, N(kDeadzone), 0, 0 },
    { "analog pedals", &gPortConfig.analogPedals, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    { "keyboard drives", &gPortConfig.keyboardPlayer, kKeyboard, kKeyboardN, N(kKeyboard), 0, 0 },
    { "raphnet adapter", &gPortConfig.raphnet, kOffOn, kOffOnN, N(kOffOn), 0, 1 },
    { "controller paks", &gPortConfig.pakOnAllPorts, kPaks, kPaksN, N(kPaks), 0, 1 },
};

#else
static const Option kVideo[] = {
    /* display: removed (always full screen); frame limit: hidden for now */
    { "resolution", &sWindow, kWindow, kWindowN, N(kWindow), 1, 1 },
    { "vsync", &gPortConfig.vsync, kVsync, kVsyncN, N(kVsync), 1, 0 },
    /* { "frame limit", &gPortConfig.maxFps, kFps, kFpsN, N(kFps), 1, 0 }, */
    /* interpolation, clean cut-outs, seamless sky: in the advanced page */
    { "3d resolution", &gPortConfig.internalHeight, kRes, kResN, N(kRes), 1, 0 },
    { "anti-aliasing", &gPortConfig.msaa, kMsaa, kMsaaN, N(kMsaa), 1, 0 },
    /* { "hd fonts", &gPortConfig.hdFonts, kOffOn, kOffOnN, N(kOffOn), 1, 0 },
       hidden on Android for now: the HD fonts do not work there yet (config.c keeps them off) */
    { "draw distance", &gPortConfig.drawDistance, kDistance, kDistanceN, N(kDistance), 1, 0 },
    { "fps counter", &gPortConfig.showFps, kOffOn, kOffOnN, N(kOffOn), 1, 0 },
    { "renderer", &gPortConfig.renderer, kRenderer, kRendererN, N(kRenderer), 0, 1 },
};
/* The technical rows, out of the video page */
static const Option kAdvanced[] = {
    { "interpolation", &gPortConfig.interpolation, kInterp, kInterpN, N(kInterp), 1, 0 },
    { "clean cut-outs", &gPortConfig.edgeFill, kOffOn, kOffOnN, N(kOffOn), 1, 0 },
    { "seamless sky", &gPortConfig.seamlessSky, kOffOn, kOffOnN, N(kOffOn), 1, 0 },
    { "screen shape", &sAspect, kAspect, kAspectN, N(kAspect), 1, 0 },
    { "n64 aa blur", &gPortConfig.n64AA, kOffOn, kOffOnN, N(kOffOn), 1, 0 },
    { "texture filter", &gPortConfig.textureFilter, kFilter, kFilterN, N(kFilter), 1, 0 },
    { "hud boxes", &gPortConfig.hudBoxes, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    { "anisotropy", &gPortConfig.anisotropy, kAniso, kAnisoN, N(kAniso), 1, 1 },
    { "skip intros", &gPortConfig.skipIntros, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    { "cpu unlockables", &gPortConfig.cpuUnlockables, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
};
static const Option kAudio[] = {
    { "master volume", &gPortConfig.volume, kVolume, kVolumeN, N(kVolume), 0, 0 },
    { "synth quality", &gPortConfig.synthRate, kSynth, kSynthN, N(kSynth), 0, 1 },
    { "audio delay", &gPortConfig.audioLatency, kLatency, kLatencyN, N(kLatency), 0, 1 },
};
static const Option kGame[] = {
    /* drones and set all drones to: removed on Android (not what the port is for; config.c
       keeps them at 1x / default). cpu unlockables: in the advanced page.
    { "drones", &gPortConfig.drones, kDrones, kDronesN, N(kDrones), 0, 1 },
    { "set all drones to", &gPortConfig.droneCar, kDroneCar, kDroneCarN, N(kDroneCar), 0, 0 }, */
    { "announce every wreck", &gPortConfig.announceAllWrecks, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    /* skip intros: in the advanced page */
};
static const Option kControls[] = {
    { "rumble", &gPortConfig.rumble, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    { "rumble strength", &gPortConfig.rumbleStrength, kStrength, kStrengthN, N(kStrength), 0, 0 },
    { "stick dead zone", &gPortConfig.deadzone, kDeadzone, kDeadzoneN, N(kDeadzone), 0, 0 },
    { "stick range", &gPortConfig.stickRange, kRange, kRangeN, N(kRange), 0, 0 },
    /* { "right stick camera", &gPortConfig.freeCamera, kOffOn, kOffOnN, N(kOffOn), 0, 0 }, forced on (config.c) */
    { "camera dead zone", &gPortConfig.cameraDeadzone, kDeadzone, kDeadzoneN, N(kDeadzone), 0, 0 },
    { "analog pedals", &gPortConfig.analogPedals, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    /* the touch controls are drawn in Java (TouchControlsView), which reads this when the
       game starts: hence "restart" */
    { "touch steering", &gPortConfig.touchArrows, kOffOn, kSteerN, N(kOffOn), 0, 1 },
    /* { "keyboard drives", ... }, { "raphnet adapter", ... }, { "controller paks", ... }:
       not on a phone (config.c keeps them off) */
};

#endif

/* The game's own cheats (DOCS_INTERNALS_AND_OPTIONS.md), as switches */
static const Option kCheats[] = {
    { "unlock all cars", &gPortConfig.cheatAllCars, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    { "unlock all tracks", &gPortConfig.cheatAllTracks, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    { "indestructible car", &gPortConfig.cheatInvincible, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    { "no time limit", &gPortConfig.cheatNoTimeLimit, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    { "super ram", &gPortConfig.cheatSuperRam, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    { "heavy car", &gPortConfig.cheatHeavyCar, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
};

static SDL_AtomicInt sApplyVideo, sSave; /* for the main thread */
static int sCaptureRow;

typedef struct {
    const char *name;
    const Option *opts;
    int count;
    int bind; /* a bindings page: 1 keyboard, 2 gamepad */
} Page;

static const Page kPages[] = {
    { "video", kVideo, N(kVideo), 0 },
    { "sound", kAudio, N(kAudio), 0 },
    { "controls", kControls, N(kControls), 0 },
    { "game", kGame, N(kGame), 0 },
#ifndef __ANDROID__
    { "keys", NULL, 0, 1 },
    { "pad", NULL, 0, 2 },
#else
    { "advanced", kAdvanced, N(kAdvanced), 0 },
    /* keys and pad: hidden on Android (touch controls and gamepads work as they are) */
#endif
    { "cheats", kCheats, N(kCheats), 0 },
};

#ifdef __ANDROID__
/* The Fidelity profile (config.c): the controls as an N64 pad, without the port's
   right stick camera, analog pedals and touch steering */
static const Option kControlsFidelity[] = {
    { "rumble", &gPortConfig.rumble, kOffOn, kOffOnN, N(kOffOn), 0, 0 },
    { "rumble strength", &gPortConfig.rumbleStrength, kStrength, kStrengthN, N(kStrength), 0, 0 },
    { "stick dead zone", &gPortConfig.deadzone, kDeadzone, kDeadzoneN, N(kDeadzone), 0, 0 },
    { "stick range", &gPortConfig.stickRange, kRange, kRangeN, N(kRange), 0, 0 },
};
static const Page kPagesFidelity[] = {
    { "video", kVideo, N(kVideo), 0 },
    { "sound", kAudio, N(kAudio), 0 },
    { "controls", kControlsFidelity, N(kControlsFidelity), 0 },
    { "game", kGame, N(kGame), 0 },
#ifndef __ANDROID__
    { "keys", NULL, 0, 1 },
    { "pad", NULL, 0, 2 },
#else
    { "advanced", kAdvanced, N(kAdvanced), 0 },
    /* keys and pad: hidden on Android (touch controls and gamepads work as they are) */
#endif
    { "cheats", kCheats, N(kCheats), 0 },
};
#define PAGES (gPortConfig.fidelity ? kPagesFidelity : kPages)
#else
#define PAGES kPages
#endif

int Port_InputCount(void);
const char *Port_InputLabel(int i);
const char *Port_InputBinding(int device, int i);
void Port_InputResetBinding(int device, int i);
void Port_InputBeginCapture(int device, int i);
int Port_InputCapturing(void);

static int sOpen;

/* The derived rows (display, window size, screen shape) from their settings */
static void loadDerived(void) {
    int i;

    sDisplay = !gPortConfig.fullscreen ? 0 : gPortConfig.fullscreenExclusive ? 2 : 1;
    sAspect = gPortConfig.stretch ? 2 : gPortConfig.widescreen ? 1 : 0;
    sWindow = 0;
    for (i = 0; i < N(kWindowW); i++) {
        if (kWindowW[i] == gPortConfig.windowWidth && kWindowH[i] == gPortConfig.windowHeight) {
            sWindow = i;
        }
    }
}

static void storeDerived(void) {
    gPortConfig.fullscreen = sDisplay != 0;
    gPortConfig.fullscreenExclusive = sDisplay == 2;
    gPortConfig.stretch = sAspect == 2;
    gPortConfig.widescreen = sAspect == 1;
    gPortConfig.windowWidth = kWindowW[sWindow];
    gPortConfig.windowHeight = kWindowH[sWindow];
}

static int choice(const Option *o) {
    int i, best = 0;

    for (i = 0; i < o->count; i++) {
        if (o->values[i] == *o->field) {
            return i;
        }
        /* not one of the choices (edited by hand): the nearest below it */
        if (o->values[i] < *o->field) {
            best = i;
        }
    }
    return best;
}

int Port_OptPages(void) {
    return N(kPages);
}

const char *Port_OptPageName(int page) {
    return PAGES[page].name;
}

/* Every settings page ends with "restore defaults" (its rows back to their defaults) */
#define RESTORE_LABEL "restore defaults"

int Port_OptCount(int page) {
    return PAGES[page].bind ? Port_InputCount() + 1 : PAGES[page].count + 1; /* + reset all / restore defaults */
}

const char *Port_OptLabel(int page, int i) {
    if (PAGES[page].bind) {
        return i < Port_InputCount() ? Port_InputLabel(i) : "reset all";
    }
    if (i >= PAGES[page].count) {
        return RESTORE_LABEL;
    }
    return PAGES[page].opts[i].label;
}

/* The row's value as shown */
const char *Port_OptValue(int page, int i) {
    if (PAGES[page].bind) {
        static char text[48];
        int dev = PAGES[page].bind - 1;
        if (i >= Port_InputCount()) {
            return "";
        }
        if (Port_InputCapturing() && i == sCaptureRow) {
            return dev == 0 ? "press a key" : "press a button";
        }
        SDL_strlcpy(text, Port_InputBinding(dev, i), sizeof(text));
        for (char *p = text; *p != '\0'; p++) {
            if (*p == ',') {
                *p = '/'; /* the font has no comma tail */
            }
        }
        return text[0] != '\0' ? text : "-";
    }
    if (i >= PAGES[page].count) {
        return "";
    }
    {
        const Option *o = &PAGES[page].opts[i];
        return o->names[choice(o)];
    }
}

int Port_OptNeedsRestart(int page, int i) {
    return PAGES[page].bind || i >= PAGES[page].count ? 0 : PAGES[page].opts[i].restart;
}

static void setDefault(int *field) {
    int v;

    if (Port_ConfigDefault(field, &v)) {
        *field = v;
    }
}

/* "restore defaults": every row of the page back to its default (config.c), the
   derived rows through the settings they stand for */
static void restorePage(int page) {
    const Page *pg = &PAGES[page];
    int i, video = 0;

    for (i = 0; i < pg->count; i++) {
        const Option *o = &pg->opts[i];
        if (o->field == &sWindow) {
            setDefault(&gPortConfig.windowWidth);
            setDefault(&gPortConfig.windowHeight);
        } else if (o->field == &sAspect) {
            setDefault(&gPortConfig.widescreen);
            setDefault(&gPortConfig.stretch);
        } else if (o->field == &sDisplay) {
            setDefault(&gPortConfig.fullscreen);
            setDefault(&gPortConfig.fullscreenExclusive);
        } else {
            setDefault(o->field);
        }
        video |= o->video;
    }
    loadDerived();
    if (video) {
        SDL_SetAtomicInt(&sApplyVideo, 1);
    }
    SDL_SetAtomicInt(&sSave, 1);
}

int Port_OptIsBindPage(int page) {
    return PAGES[page].bind != 0;
}

int Port_OptCapturing(void) {
    return Port_InputCapturing();
}

/* Left (-1) or right (+1) on a row: the next choice, applied and saved. On a
   bindings page: right (or A) waits for the new key or button, left restores the
   default; on "reset all", either resets every input. */
void Port_OptChange(int page, int i, int dir) {
    if (PAGES[page].bind) {
        int dev = PAGES[page].bind - 1, k;
        if (i >= Port_InputCount()) {
            for (k = 0; k < Port_InputCount(); k++) {
                Port_InputResetBinding(dev, k);
            }
        } else if (dir < 0) {
            Port_InputResetBinding(dev, i);
        } else {
            sCaptureRow = i;
            Port_InputBeginCapture(dev, i);
            return;
        }
        SDL_SetAtomicInt(&sSave, 1);
        return;
    }
    if (i >= PAGES[page].count) {
        restorePage(page);
        return;
    }
    const Option *o = &PAGES[page].opts[i];
    int c = choice(o) + dir;

    if (c < 0) {
        c = o->count - 1;
    } else if (c >= o->count) {
        c = 0;
    }
    *o->field = o->values[c];
    storeDerived();
    if (o->video) {
        SDL_SetAtomicInt(&sApplyVideo, 1);
    }
    SDL_SetAtomicInt(&sSave, 1);
}

static int sHoldGame; /* closed, the button that closed it still down */

void Port_OptSetOpen(int open) {
    if (open) {
        loadDerived();
    }
    if (sOpen && !open) {
        sHoldGame = 1;
    }
    sOpen = open;
}

/* Whether the game gets no input: while the page is open, and after it closes until
   player 1 lets go of every button, so the B (or Start) that closed it is not taken
   by the options screen too, which would go back to the main menu */
int Port_OptHoldsInput(u16 buttons) {
    if (sOpen) {
        return 1;
    }
    if (sHoldGame && buttons != 0) {
        return 1;
    }
    sHoldGame = 0;
    return 0;
}

int Port_OptIsOpen(void) {
    return sOpen;
}

/* Main thread, each time round its loop */
void Port_OptionsApplyPending(void) {
    if (SDL_CompareAndSwapAtomicInt(&sApplyVideo, 1, 0)) {
        Port_VideoApplySettings();
    }
    if (SDL_CompareAndSwapAtomicInt(&sSave, 1, 0)) {
        Port_ConfigSave();
    }
}

/* Player 1's pad, as the game last read it (the game thread, osContGetReadData) */
static u16 sMenuButtons;
static s8 sMenuX, sMenuY;

/*
 * The cheats page sets the flags of the game's own cheat codes (typed on the Game
 * Mode screen, src/auto/2E00C.c Cheat_UpdateInput), at their N64 addresses:
 *   0x801253F8 u8   every car (DUD)            read by the car select screens
 *   0x801253F9 u8   every difficulty (BRAD)    3FF30.c, 404DC.c, 40808.c
 *   0x8012540C + 0x18 * player: x0 indestructible (BAR), x1 no time limit (DAD),
 *              x2 damage zones at 100 (LRLR), x4 heavy (BALD), copied into the car
 *              when it is made (1E308.c), so those count from the next race
 * Cheat_ResetAll (2DF20.c) clears them when the game changes mode, so they are set
 * again each time the game reads the pads. A switch turned off clears its flag once
 * (a code typed in the game works as before otherwise).
 */
#define CHEAT_ALL_CARS 0x801253F8u
#define CHEAT_ALL_TRACKS 0x801253F9u
#define CHEAT_PLAYERS 0x8012540Cu
#define CHEAT_PLAYER_SIZE 0x18u

static void cheatByte(u32 addr, int on, int *was) {
    u8 *p = (u8 *)Port_N64ToHost(addr);

    if (p != NULL && (on || *was)) {
        *p = on ? 1 : 0;
    }
    *was = on;
}

static void applyCheats(void) {
    static int was[2 + 4 * 4];
    const int perPlayer[4] = { gPortConfig.cheatInvincible, gPortConfig.cheatNoTimeLimit, gPortConfig.cheatSuperRam,
                               gPortConfig.cheatHeavyCar };
    static const u32 perPlayerOff[4] = { 0, 1, 2, 4 };
    int pl, k;

    cheatByte(CHEAT_ALL_CARS, gPortConfig.cheatAllCars, &was[0]);
    cheatByte(CHEAT_ALL_TRACKS, gPortConfig.cheatAllTracks, &was[1]);
    for (pl = 0; pl < 4; pl++) {
        for (k = 0; k < 4; k++) {
            cheatByte(CHEAT_PLAYERS + CHEAT_PLAYER_SIZE * pl + perPlayerOff[k], perPlayer[k], &was[2 + pl * 4 + k]);
        }
    }
}

void Port_InputNoteMenuPad(const OSContPad *pad) {
    applyCheats();
    sMenuButtons = pad->button;
    sMenuX = pad->stick_x;
    sMenuY = pad->stick_y;
}

void Port_OptMenuPad(u16 *buttons, s8 *x, s8 *y) {
    *buttons = sMenuButtons;
    *x = sMenuX;
    *y = sMenuY;
}
