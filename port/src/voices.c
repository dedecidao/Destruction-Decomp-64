/*
 * The announcer's voice lines ([audio] announcer_voice), and the hooks on them.
 *
 * Every narrated line of the game (the title's welcome, the course names, the
 * countdown, checkpoints, wrecks, big hits, lap and finish calls...) goes through
 * the announcer queue (Msg_Queue, Announcer_Request, Announcer_QueueRandom, 9xxx.c)
 * and is started by Sound_PlayVoice (8344.c) with its line id (100 and up; the
 * prototype's VOICE list + 100, include/ids.h). Sound effects never pass there:
 * they use the same request type (0x3F4) but other channels and ids below 100.
 *
 * Sound_PlayVoice asks Port_VoicePlay first. The queue, the pauses between lines
 * and the game's logic stay as they are; only the sound is left out. The samples
 * stay in the ROM and are not touched. docs/AUDIO_HOOKS.md (Wrecked Metal) maps the
 * ids to the events that say them.
 *
 *   [audio] announcer_voice = 0  every line muted (Wrecked Metal's default)
 *                             1  as the N64 (forced in the Fidelity profile)
 *   [audio] voice_keep = 0xED 0xD2   lines heard even with announcer_voice = 0
 *   DD64_VOICELOG=1                  logs each line asked for (id, muted or not)
 *
 * Port_VoiceAddHook registers a function called for every line before the mute
 * (a future slow motion on the big-hit lines, another sound on the checkpoint...).
 */
#include <SDL3/SDL.h>
#include <stdlib.h>
#include "port_config.h"

typedef int s32;

void Port_Log(const char *fmt, ...);
const char *Port_IniGet(const char *section, const char *key, const char *def);

#define VOICE_IDS 512
#define MAX_HOOKS 8

typedef s32 (*PortVoiceHook)(s32 id);

static unsigned char sKeep[VOICE_IDS];
static PortVoiceHook sHooks[MAX_HOOKS];
static int sNumHooks;
static int sLog = -1;
static char sKeepText[256] = "\x01"; /* not read yet */

static void readKeep(void) {
    const char *s = Port_IniGet("audio", "voice_keep", "");
    char *end;
    long v;

    if (SDL_strcmp(s, sKeepText) == 0) {
        return;
    }
    SDL_strlcpy(sKeepText, s, sizeof(sKeepText));
    SDL_memset(sKeep, 0, sizeof(sKeep));
    while (*s != 0) {
        v = strtol(s, &end, 0);
        if (end == s) {
            s++;
            continue;
        }
        if (v >= 0 && v < VOICE_IDS) {
            sKeep[v] = 1;
        }
        s = end;
    }
}

/* Registers `fn`, called with each line's id before it plays. It returns 1 to
   leave the line out (it played its own sound, say), 0 to let the mute decide */
int Port_VoiceAddHook(PortVoiceHook fn) {
    if (fn == NULL || sNumHooks >= MAX_HOOKS) {
        return 0;
    }
    sHooks[sNumHooks++] = fn;
    return 1;
}

/* Sound_PlayVoice (8344.c): 1 = play the line, 0 = leave it out */
s32 Port_VoicePlay(s32 id) {
    int i, play;

    for (i = 0; i < sNumHooks; i++) {
        if (sHooks[i](id)) {
            return 0;
        }
    }
    readKeep();
    play = gPortConfig.announcerVoice || (id >= 0 && id < VOICE_IDS && sKeep[id]);
    if (sLog < 0) {
        sLog = SDL_getenv("DD64_VOICELOG") != NULL;
    }
    if (sLog) {
        Port_Log("voice: line 0x%X (%d)%s", id, id, play ? "" : " muted");
    }
    return play;
}
