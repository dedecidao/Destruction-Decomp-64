/*
 * Wrecked Metal Score Dopamine HUD & counter hook (port/game/wm_score.c).
 *
 * Intercepts single-player score gains in Hud_UpdateScoreCounter (src/auto/CD40.c)
 * when WM_Enabled() and [score_fx] enabled = 1. Displays a central green
 * countdown popup (e.g. 20 -> 0) with elastic pop-in, 1.08x per-tick pulse,
 * adaptive drain speed, and synchronized total-score transfer, paired with
 * procedural tiered audio (port/src/wm_score_audio.c).
 *
 * Stays 100% inactive in Play Fidelity mode and multiplayer.
 */
#include "common.h"
#include "wm_mod.h"

typedef struct { u32 w0; u32 w1; } Gfx;

typedef struct {
    u32 flags;
    u8 *widths;
    u8 height;
    u8 fmt;
    u8 siz;
    u8 pad0B;
    u8 *tex;
    u16 perRow;
    u8 pad12[2];
    u8 numChars;
    u8 pad15[3];
    u8 *map;
} ScoreFont;

typedef struct {
    s32 id;
    s32 x4;
    s32 x8;
    u8 xC;
    ScoreFont *font;
    s32 x14;
    u16 x18;
    u16 x1A;
    u8 x1C;
    s32 x20;
} ScoreUiText;

typedef struct { u8 pad[0x6BB0]; ScoreUiText text[200]; } ScoreUiTexts;
typedef struct { u8 pad[0x8B64]; s16 nText; } ScoreUiCount;
typedef struct { u8 pad[0x10FC]; s16 x10FC; } ScoreGame;

extern void *gUi[];
extern ScoreGame *gGame;
extern s8 D_800D31CC;
extern char D_801255D0[][5];

s32 Port_GameDrawsWorld(void);
s32 Port_TextFilter(void);
void Port_FontSeen(const void *font);

#define GFX2(g, a, b) { Gfx *_g = (g)++; _g->w0 = (a); _g->w1 = (u32)(b); }

#define GREEN_BASE 0x2EB043FFu
#define GREEN_POP  0x48FF68FFu

static s32 sLastTarget;
static s32 sPending;
static s32 sPeak;
static s32 sHoldFrames;
static s32 sDrainTick;
static s32 sEntryFrames;
static s32 sRecoilFrames;
static s32 sExitFrames;
static f32 sTickPulse;
static u32 sShakeSeed = 0x13579BDFu;
static ScoreFont *sCachedFont;

void WM_ScoreReset(void) {
    sLastTarget = 0;
    sPending = 0;
    sPeak = 0;
    sHoldFrames = 0;
    sDrainTick = 0;
    sEntryFrames = 0;
    sRecoilFrames = 0;
    sExitFrames = 0;
    sTickPulse = 0.0f;
    sCachedFont = NULL;
}

static s32 isScoreActive(void) {
    const WMTuning *t;

    if (!WM_Enabled() || D_800D31CC != 1) {
        return 0;
    }
    t = WM_GetTuning();
    return t->scoreFxEnabled != 0;
}

static void fmtInt(char *buf, s32 v, s32 withPlus) {
    char tmp[16];
    s32 n = 0, i = 0;

    if (v < 0) {
        v = 0;
    }
    if (withPlus) {
        buf[i++] = '+';
    }
    if (v == 0) {
        buf[i++] = '0';
        buf[i] = '\0';
        return;
    }
    while (v > 0 && n < 12) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        buf[i++] = tmp[--n];
    }
    buf[i] = '\0';
}

int WM_ScoreStep(int *curScore, int targetScore, int player) {
    s32 added, step;

    if (player != 0 || !isScoreActive()) {
        return 0;
    }

    if (targetScore < sLastTarget) {
        /* Race restarted or score reset */
        WM_ScoreReset();
        sLastTarget = targetScore;
        return 1;
    }

    if (targetScore > sLastTarget) {
        added = targetScore - sLastTarget;
        sLastTarget = targetScore;
        sPending += added;
        if (sPending > sPeak) {
            sPeak = sPending;
        }
        sHoldFrames = 12; /* brief beat so the big number reads clearly before draining */
        sDrainTick = 0;
        sEntryFrames = 10;
        sRecoilFrames = added >= 40 ? 7 : 4;
        sExitFrames = 0;
        WM_ScoreAudioHit(added, sPending);
    }

    if (sPending > 0) {
        if (sHoldFrames > 0) {
            sHoldFrames--;
            return 1;
        }
        step = 0;
        if (sPending <= 25) {
            /* Slower, readable drain: 1 pt every 2 frames (~0.67 s for 20 pts) */
            sDrainTick++;
            if (sDrainTick >= 2) {
                sDrainTick = 0;
                step = 1;
            }
        } else if (sPending <= 60) {
            step = 1;
        } else if (sPending <= 120) {
            step = 2;
        } else {
            step = sPending / 35;
            if (step < 4) {
                step = 4;
            }
        }
        if (step > sPending) {
            step = sPending;
        }
        if (step > 0) {
            sPending -= step;
            *curScore += step;
            if (*curScore > targetScore) {
                *curScore = targetScore;
            }
            sTickPulse = 1.0f;
            WM_ScoreAudioTick(sPending, sPeak);
            if (sPending == 0) {
                *curScore = targetScore;
                sExitFrames = 42; /* 10f hold at 0 + 32f smooth fade-out */
                WM_ScoreAudioBanked(sPeak);
            }
        }
    } else if (*curScore < targetScore) {
        *curScore = targetScore;
    }

    return 1;
}

static ScoreFont *findHudFont(void) {
    ScoreUiTexts *ui = (ScoreUiTexts *)gUi[0];
    s32 i, n;
    ScoreFont *fallback = NULL;

    if (ui == NULL) {
        return sCachedFont;
    }
    n = ((ScoreUiCount *)ui)->nText;
    if (n > 200) {
        n = 200;
    }
    for (i = 0; i < n; i++) {
        ScoreFont *f = ui->text[i].font;
        if (f == NULL || (f->siz != 2 && f->siz != 3)) {
            continue;
        }
        if ((char *)ui->text[i].x14 == D_801255D0[0]) {
            sCachedFont = f;
            return f;
        }
        if (fallback == NULL) {
            fallback = f;
        }
    }
    if (fallback != NULL) {
        sCachedFont = fallback;
    }
    return sCachedFont;
}

static s32 glyphIndex(ScoreFont *f, u8 c) {
    s32 i;

    if (c >= 'a' && c <= 'z') {
        c -= 'a' - 'A';
    }
    for (i = 0; i < f->numChars; i++) {
        if (f->map[i] == c) {
            return i;
        }
    }
    return -1;
}

static f32 measureTextWidth(ScoreFont *f, const char *s) {
    f32 w = 0.0f;
    for (; *s != '\0'; s++) {
        s32 i = glyphIndex(f, (u8)*s);
        w += i >= 0 ? (f32)f->widths[i] : 4.0f;
    }
    return w;
}

static u32 lerpColor(u32 a, u32 b, f32 t, u8 alpha) {
    u32 ar = (a >> 24) & 0xFF, ag = (a >> 16) & 0xFF, ab = (a >> 8) & 0xFF;
    u32 br = (b >> 24) & 0xFF, bg = (b >> 16) & 0xFF, bb = (b >> 8) & 0xFF;
    u32 r = (u32)(ar + (br - (s32)ar) * t);
    u32 g = (u32)(ag + (bg - (s32)ag) * t);
    u32 bl = (u32)(ab + (bb - (s32)ab) * t);
    return (r << 24) | (g << 16) | (bl << 8) | (u32)alpha;
}

static Gfx *drawScaledText(Gfx *g, ScoreFont *f, f32 cx, f32 cy, const char *s, f32 scale, u32 rgba) {
    f32 totalW = measureTextWidth(f, s) * scale;
    f32 x = cx - totalW * 0.5f;
    f32 y = cy - ((f32)f->height * scale) * 0.5f;
    s32 dsdx = (s32)(1024.0f / scale + 0.5f);
    s32 i, j, sum, row, col;
    u8 *addr;

    if (dsdx < 1) {
        dsdx = 1;
    } else if (dsdx > 0x3FFF) {
        dsdx = 0x3FFF;
    }

    GFX2(g, 0xE7000000, 0);
    GFX2(g, 0xE3000A01, 0);
    GFX2(g, 0xE3001001, 0);
    GFX2(g, 0xE3001201, Port_TextFilter());
    GFX2(g, 0xE3000C00, 0);
    GFX2(g, 0xE200001C, 0x0F0A4000);
    GFX2(g, 0xFC119623, 0xFF2FFFFF);
    GFX2(g, 0xFA000000, rgba);
    Port_FontSeen(f);

    for (; *s != '\0'; s++) {
        i = glyphIndex(f, (u8)*s);
        if (i < 0) {
            x += 4.0f * scale;
            continue;
        }
        sum = 0;
        for (j = 0; j < i; j++) {
            sum += f->widths[j];
        }
        if (f->perRow == 0) {
            break;
        }
        row = sum / f->perRow;
        col = sum % f->perRow;
        addr = f->tex + (f->perRow * row * f->height + col) * (f->siz == 2 ? 2 : 4);
        if (f->siz == 2) {
            GFX2(g, 0xFD100000 | ((f->fmt & 7) << 21) | ((f->perRow - 1) & 0xFFF), addr);
            GFX2(g, 0xF5101200 | ((f->fmt & 7) << 21), 0x07080200);
            GFX2(g, 0xE6000000, 0);
            GFX2(g, 0xF4000000, 0x07080080);
            GFX2(g, 0xE7000000, 0);
            GFX2(g, 0xF5101200 | ((f->fmt & 7) << 21), 0x00080200);
            GFX2(g, 0xF2000000, 0x00080080);
        } else {
            GFX2(g, 0xFD180000 | ((f->fmt & 7) << 21) | ((f->perRow - 1) & 0xFFF), addr);
            GFX2(g, 0xF5181200 | ((f->fmt & 7) << 21), 0x07080200);
            GFX2(g, 0xE6000000, 0);
            GFX2(g, 0xF4000000, 0x07080080);
            GFX2(g, 0xE7000000, 0);
            GFX2(g, 0xF5181200 | ((f->fmt & 7) << 21), 0x00080200);
            GFX2(g, 0xF2000000, 0x00080080);
        }
        {
            u32 lrs = ((f->widths[i] - 1) * 4) & 0xFFF;
            u32 lrt = ((f->height - 1) * 4) & 0xFFF;
            g[-4].w1 = 0x07000000 | (lrs << 12) | lrt;
            g[-1].w1 = (lrs << 12) | lrt;
        }
        {
            s32 x0 = (s32)(x * 4.0f + 0.5f);
            s32 y0 = (s32)(y * 4.0f + 0.5f);
            s32 x1 = (s32)((x + (f32)f->widths[i] * scale) * 4.0f + 0.5f);
            s32 y1 = (s32)((y + (f32)f->height * scale) * 4.0f + 0.5f);
            if (x0 < 0) {
                x0 = 0;
            }
            if (y0 < 0) {
                y0 = 0;
            }
            GFX2(g, 0xE4000000 | (((u32)x1 & 0xFFFU) << 12) | ((u32)y1 & 0xFFFU),
                 (((u32)x0 & 0xFFFU) << 12) | ((u32)y0 & 0xFFFU));
            GFX2(g, 0xE1000000, 0);
            GFX2(g, 0xF1000000, (((u32)dsdx & 0xFFFFU) << 16) | ((u32)dsdx & 0xFFFFU));
        }
        x += (f32)f->widths[i] * scale;
    }
    return g;
}

void *WM_ScoreDraw(void *gfxPtr) {
    Gfx *g = (Gfx *)gfxPtr;
    ScoreFont *f;
    char buf[16];
    f32 baseScale, scale, cx, cy, popT = 0.0f;
    u8 alpha = 255;
    u32 col;

    if (!isScoreActive() || !Port_GameDrawsWorld() || gGame == NULL) {
        return gfxPtr;
    }
    if (gGame->x10FC != 0x1B && gGame->x10FC != 0x1C) {
        return gfxPtr;
    }
    if (sPending <= 0 && sExitFrames <= 0) {
        return gfxPtr;
    }

    f = findHudFont();
    if (f == NULL) {
        return gfxPtr;
    }

    baseScale = WM_GetTuning()->scoreFxScale;
    scale = baseScale;

    /* 2D HUD coordinates (0..320, 0..240) stay centered in the 4:3 middle area
       even when widescreen is enabled: (160, 96) sits big and centered right
       above the player's car. */
    cx = 160.0f;
    cy = 96.0f;

    if (sEntryFrames > 0) {
        /* Elastic pop-in overshoot (1.40x -> 1.0x) */
        f32 t = (f32)sEntryFrames / 10.0f;
        scale *= 1.0f + 0.40f * t * t;
        popT = t;
        sEntryFrames--;
    } else if (sTickPulse > 0.01f) {
        /* 1.08x micro-pulse per countdown tick */
        scale *= 1.0f + 0.08f * sTickPulse;
        popT = sTickPulse * 0.45f;
        sTickPulse -= 0.22f;
        if (sTickPulse < 0.0f) {
            sTickPulse = 0.0f;
        }
    }

    if (sRecoilFrames > 0) {
        s32 amp = sPeak >= 40 ? 3 : 2;
        sShakeSeed ^= sShakeSeed << 13;
        sShakeSeed ^= sShakeSeed >> 17;
        sShakeSeed ^= sShakeSeed << 5;
        cx += (f32)((s32)(sShakeSeed % (u32)(amp * 2 + 1)) - amp);
        cy += (f32)((s32)((sShakeSeed >> 8) % (u32)(amp * 2 + 1)) - amp);
        sRecoilFrames--;
    }

    if (sPending == 0 && sExitFrames > 0) {
        /* Brief hold at 0 (frames 42..33), then smooth gradient fade-out (32..1) */
        if (sExitFrames <= 32) {
            f32 fadeT = 1.0f - (f32)sExitFrames / 32.0f;
            cy -= fadeT * 8.0f;
            alpha = (u8)((1.0f - fadeT) * 255.0f);
        }
        sExitFrames--;
    }
    fmtInt(buf, sPending, 0);
    col = lerpColor(GREEN_BASE, GREEN_POP, popT, alpha);
    if (alpha > 8) {
        g = drawScaledText(g, f, cx + 1.0f, cy + 1.0f, buf, scale, (u32)((alpha * 3) / 4));
        g = drawScaledText(g, f, cx, cy, buf, scale, col);
    }

    GFX2(g, 0xE7000000, 0);
    return g;
}
