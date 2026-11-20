/*
 * dd64.ini: settings next to the executable. Written with every setting and
 * its default on first run; unknown keys are kept when it is saved again.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "port_config.h"
#include "port_ini.h"

PortConfig gPortConfig;

#define MAX_ENTRIES 512

typedef struct {
    char section[32];
    char key[48];
    char value[128];
} IniEntry;

static IniEntry sEntries[MAX_ENTRIES];
static int sNumEntries;
static char sBaseDir[1024];
static char sIniPath[1100];

const char *Port_BaseDir(void) {
    if (sBaseDir[0] == '\0') {
        const char *b = SDL_GetBasePath();
        SDL_strlcpy(sBaseDir, b != NULL ? b : "", sizeof(sBaseDir));
    }
    return sBaseDir;
}

static char *trim(char *s) {
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

static IniEntry *find(const char *section, const char *key) {
    int i;

    for (i = 0; i < sNumEntries; i++) {
        if (SDL_strcasecmp(sEntries[i].section, section) == 0 && SDL_strcasecmp(sEntries[i].key, key) == 0) {
            return &sEntries[i];
        }
    }
    return NULL;
}

const char *Port_IniGet(const char *section, const char *key, const char *def) {
    IniEntry *e = find(section, key);

    return e != NULL ? e->value : def;
}

int Port_IniGetInt(const char *section, const char *key, int def) {
    IniEntry *e = find(section, key);

    return e != NULL ? atoi(e->value) : def;
}

void Port_IniSet(const char *section, const char *key, const char *value) {
    IniEntry *e = find(section, key);

    if (e == NULL) {
        if (sNumEntries == MAX_ENTRIES) {
            return;
        }
        e = &sEntries[sNumEntries++];
        SDL_strlcpy(e->section, section, sizeof(e->section));
        SDL_strlcpy(e->key, key, sizeof(e->key));
    }
    SDL_strlcpy(e->value, value, sizeof(e->value));
}

void Port_IniSetInt(const char *section, const char *key, int value) {
    char buf[32];

    SDL_snprintf(buf, sizeof(buf), "%d", value);
    Port_IniSet(section, key, buf);
}

static void parse(const char *text) {
    char section[32] = "";
    char line[512];
    const char *p = text;

    while (*p != '\0') {
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
        s = trim(line);
        if (*s == '\0' || *s == ';' || *s == '#') {
            continue;
        }
        if (*s == '[') {
            char *e = strchr(s, ']');
            if (e != NULL) {
                *e = '\0';
                SDL_strlcpy(section, trim(s + 1), sizeof(section));
            }
            continue;
        }
        eq = strchr(s, '=');
        if (eq != NULL) {
            *eq = '\0';
            Port_IniSet(section, trim(s), trim(eq + 1));
        }
    }
}

/* Every setting with its default and a line of help, in file order */
typedef struct {
    const char *section, *key;
    int *field;
    int def;
    const char *help;
    const char *const *names; /* the values as words (0, 1 ...) in the file, not numbers */
} Setting;

static const char *const kRendererNames[] = { "opengl", "sdlgpu", NULL };

/* A setting's default: the PC's, or on Android the phone's (DOCS_ANDROID_PORT.md,
   20/11 round: 720p output and 3D, vsync, 2x MSAA with the N64 AA, bilinear filtering,
   4x anisotropy, x8 draw distance, Vulkan, no HUD boxes, intros skipped; the 16x/x16
   defaults dropped frames to 55 fps on a Galaxy S23 Ultra) */
#ifdef __ANDROID__
#define DEF(pc, android) (android)
#else
#define DEF(pc, android) (pc)
#endif

static const Setting sSettings[] = {
    { "video", "window_width", &gPortConfig.windowWidth, DEF(960, 1280), "Window size in pixels" },
    { "video", "window_height", &gPortConfig.windowHeight, DEF(720, 720), NULL },
    { "video", "fullscreen", &gPortConfig.fullscreen, DEF(0, 1), "1 = start fullscreen (Alt+Enter or F11 toggles)" },
    { "video", "fullscreen_exclusive", &gPortConfig.fullscreenExclusive, 0,
      "0 = borderless desktop fullscreen, 1 = exclusive mode" },
    { "video", "vsync", &gPortConfig.vsync, 1, "0 = off, 1 = on, -1 = adaptive, 2 = VRR (G-Sync/FreeSync: on, capped just under the display's rate)" },
    { "video", "max_fps", &gPortConfig.maxFps, 0,
      "Frame cap: 0 = none (with vsync, the display's rate), or 30, 40, 60, 120, 144 ... The game runs at 60 Hz either way" },
    { "video", "interpolation", &gPortConfig.interpolation, DEF(1, 0),
      "Frames shown faster or slower than the game's 60: 1 = drawn in between, smooth (up to a frame behind);"
      " 2 = drawn ahead of the newest frame (no added lag); 0 = off (max_fps 30 then runs the game at 30, as the N64 often did)" },
    { "video", "msaa", &gPortConfig.msaa, DEF(4, 2), "Multisample anti-aliasing: 1 (off), 2, 4 or 8" },
    { "video", "n64_aa", &gPortConfig.n64AA, DEF(0, 1), "1 = resolve anti-aliasing the way the N64 did (softer)" },
    { "video", "anisotropy", &gPortConfig.anisotropy, DEF(1, 4), "Anisotropic filtering: 1 (off) to 16" },
    { "video", "internal_height", &gPortConfig.internalHeight, DEF(0, 720),
      "Lines the game is drawn at: 0 = the window's, 240 = the N64's" },
    { "video", "show_fps", &gPortConfig.showFps, 1, "1 = frames per second in the top left corner" },
    { "video", "widescreen", &gPortConfig.widescreen, 1,
      "1 = a wide window shows more of the 3D view, unstretched, with the HUD and menus 4:3 in the middle" },
    { "video", "stretch", &gPortConfig.stretch, 0, "0 = keep 4:3 with side bars, 1 = stretch to the window" },
    { "video", "texture_filter", &gPortConfig.textureFilter, DEF(1, 1), "0 = nearest, 1 = bilinear, 2 = three-point (N64)" },
    { "video", "hud_boxes", &gPortConfig.hudBoxes, DEF(1, 0),
      "1 = the dark capsules behind the race HUD's score, time and cars left, as the N64; 0 = the numbers alone" },
    { "video", "hd_fonts", &gPortConfig.hdFonts, DEF(1, 0),
      "1 = the menu and HUD fonts drawn from the high-resolution ones in Data/Textures/fonts_hd, where there"
      " is one; 0 = the N64's" },
    { "video", "sharp_text", &gPortConfig.sharpText, 1,
      "1 = the menu and HUD text drawn sharp, without filtering; 0 = filtered as the N64 (soft when scaled up)" },
    { "video", "seamless_sky", &gPortConfig.seamlessSky, 1,
      "1 = the sky's pieces are filtered across their joins (no seams in the clouds); 0 = as the N64" },
    { "video", "edge_fill", &gPortConfig.edgeFill, DEF(0, 1),
      "1 = see-through texels take their neighbours' colour, so filtered cut-outs (trees) have no dark edge;"
      " 0 = as the N64" },
    { "video", "draw_distance", &gPortConfig.drawDistance, DEF(1, 8),
      "1 = as the N64; 2, 4, 8 or 16 = the scenery drawn that many times further out, and the fog"
      " (Swamp, Glacier) with it" },
    { "video", "renderer", &gPortConfig.renderer, DEF(0, 1),
      "opengl, or sdlgpu: SDL's GPU API (Vulkan). Takes effect the next time the game starts", kRendererNames },
    { "audio", "volume", &gPortConfig.volume, 100, "Master volume, 0-100" },
    { "audio", "synth_rate", &gPortConfig.synthRate, DEF(22050, 44100),
      "The game's synthesizer: 22050 = as the N64; 44100 = mixed at twice the rate, clearer highs"
      " (instruments pitched up no longer alias), same pitch and reverb. Takes effect at the next start" },
    { "audio", "latency", &gPortConfig.audioLatency, 80,
      "Output delay in milliseconds (N64: about 20); raise it if the sound stutters" },
    { "input", "rumble", &gPortConfig.rumble, 1, "1 = Rumble Pak on every controller that can rumble" },
    { "input", "rumble_strength", &gPortConfig.rumbleStrength, 100, "0-100" },
    { "input", "deadzone", &gPortConfig.deadzone, 12, "Stick dead zone, percent of full tilt" },
    { "input", "stick_range", &gPortConfig.stickRange, 80,
      "Stick value at full tilt: an N64 controller reads about 80, the maximum is 127" },
    { "input", "free_camera", &gPortConfig.freeCamera, 1,
      "1 = the right stick turns the camera around the car (and no longer presses the C buttons)" },
    { "input", "camera_deadzone", &gPortConfig.cameraDeadzone, 20,
      "Right stick dead zone (the free camera), percent of full tilt" },
    { "input", "analog_pedals", &gPortConfig.analogPedals, 1,
      "1 = the right trigger is the gas and the left the brake, by how far they are pressed"
      " (the triggers no longer press Z and R)" },
    { "input", "touch_arrows", &gPortConfig.touchArrows, 0,
      "Android: 1 = the on-screen controls steer with arrows (a d-pad) instead of the analog stick;"
      " read when the game starts (the launcher has the same switch)" },
    { "input", "keyboard_player", &gPortConfig.keyboardPlayer, 1, "Controller the keyboard drives: 1-4, 0 = none" },
    { "input", "raphnet", &gPortConfig.raphnet, 1,
      "1 = read raphnet N64 adapters raw: real stick values, no dead zone, the Rumble Pak driven directly" },
    { "input", "raphnet_player", &gPortConfig.raphnetPlayer, 1,
      "Controller the raphnet adapter's first port is, 1-4 (a second port is the next one)" },
    { "game", "drones", &gPortConfig.drones, 1,
      "The computer cars times this, 1 (as the N64) to 10, as many as the game's memory holds;"
      " the announcer counts the cars left once there are 11" },
    { "game", "drone_car", &gPortConfig.droneCar, -1,
      "The car every drone drives: -1 = the computer cars' own (with cpu_unlockables their twins too);"
      " 0-11 cars 1-12, 12-23 their unlockable twins (bonus cars 1-12). The drones only, not the game's"
      " own computer cars; never a player's car (its twin then)" },
    { "game", "announce_all_wrecks", &gPortConfig.announceAllWrecks, 0,
      "1 = while more cars are left than the announcer's numbered lines go to (the drones), each wreck"
      " gets the opening of those lines (\"that's another one down\"), no number; from 10 down as the N64" },
    { "game", "cpu_unlockables", &gPortConfig.cpuUnlockables, 0,
      "1 = computer cars may drive the unlockable cars too" },
    { "game", "skip_intros", &gPortConfig.skipIntros, DEF(0, 1),
      "1 = the logos at start pass at once, straight to the title screen" },
    { "cheats", "all_cars", &gPortConfig.cheatAllCars, 0, "1 = every car can be picked, the bonus cars too (the game's code DUD)" },
    { "cheats", "all_tracks", &gPortConfig.cheatAllTracks, 0,
      "1 = every difficulty, and with them every championship track (code BRAD)" },
    { "cheats", "invincible", &gPortConfig.cheatInvincible, 0,
      "1 = the players' cars take no damage (code BAR); from the next race" },
    { "cheats", "no_time_limit", &gPortConfig.cheatNoTimeLimit, 0,
      "1 = the clock does not end a race (code DAD); from the next race" },
    { "cheats", "super_ram", &gPortConfig.cheatSuperRam, 0,
      "1 = the players' cars' damage zones all at 100: they hit hard and take little (code LRLR)" },
    { "cheats", "heavy_car", &gPortConfig.cheatHeavyCar, 0,
      "1 = the players' cars 8.8 times as heavy (code BALD); from the next race" },
    { "saves", "pak_on_all_ports", &gPortConfig.pakOnAllPorts, 0,
      "1 = every controller has a Controller Pak; 0 = only controller 1" },
    { "saves", "pak_screens", &gPortConfig.pakScreens, 0,
      "0 = load and save without the Controller Pak screens; 1 = the N64's screens" },
};

#ifdef __ANDROID__
/*
 * The Android defaults change as the port does (DEF() above). An older dd64.ini on
 * the phone would keep the old values, so the file carries [android] config_version:
 * below the current one, every setting goes back to its default once, but for the
 * player's own adjustments kept here (volume, rumble strength, the dead zones, the
 * pedals, cheats and saves). Bump it whenever the Android defaults change.
 */
#define ANDROID_CONFIG_VERSION 3

static int keepOnUpgrade(const char *section, const char *key) {
    static const char *const keep[] = { "volume", "rumble_strength", "deadzone", "stick_range", "camera_deadzone",
                                        "analog_pedals", "touch_arrows", NULL };
    int i;

    if (SDL_strcasecmp(section, "cheats") == 0 || SDL_strcasecmp(section, "saves") == 0) {
        return 1;
    }
    for (i = 0; keep[i] != NULL; i++) {
        if (SDL_strcasecmp(key, keep[i]) == 0) {
            return 1;
        }
    }
    return 0;
}
#endif

void Port_ConfigLoad(void) {
    size_t size;
    char *text;
    size_t i;

    SDL_snprintf(sIniPath, sizeof(sIniPath), "%sdd64.ini", Port_BaseDir());
    text = SDL_LoadFile(sIniPath, &size);
    if (text != NULL) {
        parse(text);
        SDL_free(text);
    }
    for (i = 0; i < SDL_arraysize(sSettings); i++) {
        *sSettings[i].field = Port_IniGetInt(sSettings[i].section, sSettings[i].key, sSettings[i].def);
        if (sSettings[i].names != NULL) {
            const char *v = Port_IniGet(sSettings[i].section, sSettings[i].key, "");
            int n;
            for (n = 0; sSettings[i].names[n] != NULL; n++) {
                if (SDL_strcasecmp(v, sSettings[i].names[n]) == 0) {
                    *sSettings[i].field = n;
                }
            }
        }
    }
    /* drones replaced double_field (1 = twice the computer cars) */
    if (SDL_strcmp(Port_IniGet("game", "drones", ""), "") == 0 && Port_IniGetInt("game", "double_field", 0) == 1) {
        gPortConfig.drones = 2;
    }
    gPortConfig.drones = SDL_clamp(gPortConfig.drones, 1, 10);
    gPortConfig.droneCar = SDL_clamp(gPortConfig.droneCar, -1, 23);
    gPortConfig.synthRate = gPortConfig.synthRate >= 44100 ? 44100 : 22050;
#ifdef __ANDROID__
    /* Settings the Android options page hides (port/src/options.c), kept at the
       values that make sense on a phone whatever an older dd64.ini says */
    gPortConfig.fullscreen = 1;      /* the app is always full screen */
    gPortConfig.stretch = 0;         /* "stretched" removed: 4:3 or widescreen */
    gPortConfig.maxFps = 0;          /* frame limit hidden for now */
    gPortConfig.freeCamera = 1;      /* right stick camera: always on */
    gPortConfig.keyboardPlayer = 0;  /* no keyboard rows on a phone */
    gPortConfig.raphnet = 0;         /* raphnet.c is not built on Android */
    gPortConfig.pakOnAllPorts = 0;   /* controller paks row hidden */
    gPortConfig.drones = 1;          /* drones and set all drones to: removed on Android */
    gPortConfig.droneCar = -1;       /* (the drones made cars fly at arena starts) */
    gPortConfig.hdFonts = 0;         /* hd fonts hidden: they do not work on Android yet */
#endif
#ifdef __ANDROID__
    {
        int ver = Port_IniGetInt("android", "config_version", 0);
        if (ver < ANDROID_CONFIG_VERSION) {
            if (text != NULL) {
                for (i = 0; i < SDL_arraysize(sSettings); i++) {
                    if (!keepOnUpgrade(sSettings[i].section, sSettings[i].key)) {
                        *sSettings[i].field = sSettings[i].def;
                    }
                }
                Port_Log("config: the Android defaults (version %d) replace those of version %d", ANDROID_CONFIG_VERSION,
                         ver);
            }
            Port_IniSetInt("android", "config_version", ANDROID_CONFIG_VERSION);
            text = NULL; /* save it */
            /* the forced values again, after the reset */
            gPortConfig.fullscreen = 1;
            gPortConfig.stretch = 0;
            gPortConfig.maxFps = 0;
            gPortConfig.freeCamera = 1;
            gPortConfig.keyboardPlayer = 0;
            gPortConfig.raphnet = 0;
            gPortConfig.pakOnAllPorts = 0;
            gPortConfig.drones = 1;
            gPortConfig.droneCar = -1;
            gPortConfig.hdFonts = 0;
        }
    }
#endif
    if (text == NULL) {
        Port_ConfigSave();
    }
}

/* The entries of a section that are not settings (key bindings, keys a newer or
   older version knows), each written once */
static void writeOthers(FILE *f, const char *section, char *written) {
    int j;
    size_t i;

    for (j = 0; j < sNumEntries; j++) {
        if (written[j] || SDL_strcasecmp(sEntries[j].section, section) != 0) {
            continue;
        }
        written[j] = 1;
        for (i = 0; i < SDL_arraysize(sSettings); i++) {
            if (SDL_strcasecmp(sEntries[j].section, sSettings[i].section) == 0 &&
                SDL_strcasecmp(sEntries[j].key, sSettings[i].key) == 0) {
                break;
            }
        }
        if (i == SDL_arraysize(sSettings) &&
            !(SDL_strcasecmp(section, "game") == 0 && SDL_strcasecmp(sEntries[j].key, "double_field") == 0)) {
            fprintf(f, "%s = %s\n", sEntries[j].key, sEntries[j].value); /* (double_field: now drones) */
        }
    }
}

/* A worded setting's value as its word (NULL: written as a number) */
static const char *settingName(const Setting *s) {
    int n;

    for (n = 0; s->names != NULL && s->names[n] != NULL; n++) {
        if (n == *s->field) {
            return s->names[n];
        }
    }
    return NULL;
}

void Port_ConfigSave(void) {
    FILE *f = fopen(sIniPath, "w");
    char written[MAX_ENTRIES] = { 0 };
    const char *section = NULL;
    size_t i;
    int j;

    if (f == NULL) {
        Port_Log("could not write %s", sIniPath);
        return;
    }
    fprintf(f, "; Destruction Derby 64 settings. Delete this file to restore the defaults.\n");
    for (i = 0; i < SDL_arraysize(sSettings); i++) {
        const Setting *s = &sSettings[i];
        const char *name = settingName(s);
        if (name != NULL) {
            Port_IniSet(s->section, s->key, name);
        } else {
            Port_IniSetInt(s->section, s->key, *s->field);
        }
        if (section == NULL || strcmp(section, s->section) != 0) {
            if (section != NULL) {
                writeOthers(f, section, written);
            }
            section = s->section;
            fprintf(f, "\n[%s]\n", section);
        }
        if (s->help != NULL) {
            fprintf(f, "; %s\n", s->help);
        }
        if (name != NULL) {
            fprintf(f, "%s = %s\n", s->key, name);
        } else {
            fprintf(f, "%s = %d\n", s->key, *s->field);
        }
    }
    if (section != NULL) {
        writeOthers(f, section, written);
    }
    /* sections with no settings, in the order they were read */
    for (j = 0; j < sNumEntries; j++) {
        if (!written[j]) {
            fprintf(f, "\n[%s]\n", sEntries[j].section);
            writeOthers(f, sEntries[j].section, written);
        }
    }
    fclose(f);
}

/* The default of the setting stored in `field` (an int in gPortConfig): 1 and the
   value in *out, 0 if it is not a setting. The options pages' "restore defaults". */
int Port_ConfigDefault(const int *field, int *out) {
    size_t i;

    for (i = 0; i < SDL_arraysize(sSettings); i++) {
        if (sSettings[i].field == field) {
            *out = sSettings[i].def;
            return 1;
        }
    }
    return 0;
}

/* [game] skip_intros, for the logo screens (src/auto/3C010.c, 3C108.c, 3C22C.c) */
int Port_SkipIntros(void) {
    return gPortConfig.skipIntros != 0;
}
