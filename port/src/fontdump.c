/*
 * DD64_FONTDUMP=1 (environment): write each font Font_DrawText uses, once, as
 * its whole atlas, to Data/Textures/fonts_original/:
 *
 *   font_<hash>_<w>x<h>.png  the atlas: every glyph, in the game's own layout
 *   font_<hash>.txt          where each glyph is in it (x, y, width; one height)
 *
 * The atlas is read from the font as the game loaded it from the ROM, not from
 * what the renderer drew, so it is whole and the same on every run. <hash> names
 * the atlas by its texels, for a high-resolution replacement to be matched to it.
 * The files come from the user's own ROM: never commit or share them.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "port.h"
#include "port_config.h"
#include "port_shadow.h"

/* The game's Font (src/auto/5F52C.c), by offset */
#define F_WIDTHS 0x04
#define F_HEIGHT 0x08
#define F_FMT 0x09
#define F_SIZ 0x0A
#define F_TEX 0x0C
#define F_PERROW 0x10
#define F_NUMCHARS 0x14
#define F_MAP 0x18

#define MAX_FONTS 64

static const void *sSeen[MAX_FONTS];
static int sNumSeen;
static int sEnabled = -1;

static const uint8_t *hostPtr(uint32_t v) {
    if (v >= 0x80000000u && v < 0x82000000u) {
        return (const uint8_t *)Port_N64ToHost(v);
    }
    return (const uint8_t *)(uintptr_t)v;
}

static uint8_t rd8(const uint8_t *p) {
    return *p; /* a byte is the same in either order */
}

static uint8_t scale5(uint32_t v) {
    return (uint8_t)((v * 0xFF) / 0x1F);
}

void Port_FontSeen(const void *font) {
    const uint8_t *f = font;
    const uint8_t *widths, *tex, *map;
    uint32_t height, fmt, siz, perRow, numChars, total, rows, w, h, bpp, x, y, i;
    uint64_t hash = 0xCBF29CE484222325ull;
    uint8_t *rgba;
    char dir[1100], path[1200];
    FILE *txt;

    if (sEnabled < 0) {
        sEnabled = SDL_getenv("DD64_FONTDUMP") != NULL;
    }
    if (!sEnabled || font == NULL) {
        return;
    }
    for (i = 0; i < (uint32_t)sNumSeen; i++) {
        if (sSeen[i] == font) {
            return;
        }
    }
    if (sNumSeen < MAX_FONTS) {
        sSeen[sNumSeen++] = font;
    }

    widths = hostPtr(Port_RdN64(f + F_WIDTHS, 4));
    tex = hostPtr(Port_RdN64(f + F_TEX, 4));
    map = hostPtr(Port_RdN64(f + F_MAP, 4));
    height = rd8(f + F_HEIGHT);
    fmt = rd8(f + F_FMT);
    siz = rd8(f + F_SIZ);
    perRow = Port_RdN64(f + F_PERROW, 2);
    numChars = rd8(f + F_NUMCHARS);
    if (widths == NULL || tex == NULL || height == 0 || perRow == 0 || numChars == 0 || (siz != 2 && siz != 3)) {
        Port_Log("fontdump: font at %08X not read (height %u, perRow %u, chars %u, siz %u)",
                 Port_HostToN64(font), height, perRow, numChars, siz);
        return;
    }
    total = 0;
    for (i = 0; i < numChars; i++) {
        total += rd8(widths + i);
    }
    rows = (total + perRow - 1) / perRow;
    w = perRow;
    h = rows * height;
    bpp = siz == 2 ? 2 : 4;
    rgba = SDL_malloc((size_t)w * h * 4);
    if (rgba == NULL) {
        return;
    }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            const uint8_t *p = tex + ((size_t)y * perRow + x) * bpp;
            uint8_t *o = &rgba[((size_t)y * w + x) * 4];
            if (siz == 2) {
                uint32_t c = Port_RdN64(p, 2);
                if (fmt == 3) { /* IA16: intensity, alpha */
                    o[0] = o[1] = o[2] = (uint8_t)(c >> 8);
                    o[3] = (uint8_t)c;
                } else { /* RGBA16 */
                    o[0] = scale5(c >> 11);
                    o[1] = scale5((c >> 6) & 0x1F);
                    o[2] = scale5((c >> 1) & 0x1F);
                    o[3] = (c & 1) ? 255 : 0;
                }
            } else {
                uint32_t c = Port_RdN64(p, 4);
                o[0] = (uint8_t)(c >> 24);
                o[1] = (uint8_t)(c >> 16);
                o[2] = (uint8_t)(c >> 8);
                o[3] = (uint8_t)c;
            }
        }
    }
    for (i = 0; i < w * h * 4; i++) {
        hash = (hash ^ rgba[i]) * 0x100000001B3ull;
    }
    hash = (hash ^ w) * 0x100000001B3ull;
    hash = (hash ^ h) * 0x100000001B3ull;

    SDL_snprintf(dir, sizeof(dir), "%sData/Textures/fonts_original", Port_BaseDir());
    SDL_CreateDirectory(dir);
    SDL_snprintf(path, sizeof(path), "%s/font_%016llx_%ux%u.png", dir, (unsigned long long)hash, w, h);
    if (!SDL_GetPathInfo(path, NULL)) {
        SDL_Surface *s = SDL_CreateSurfaceFrom((int)w, (int)h, SDL_PIXELFORMAT_ABGR8888, rgba, (int)w * 4);
        if (s != NULL) {
            SDL_SavePNG(s, path);
            SDL_DestroySurface(s);
        }
        SDL_snprintf(path, sizeof(path), "%s/font_%016llx.txt", dir, (unsigned long long)hash);
        txt = fopen(path, "w");
        if (txt != NULL) {
            uint32_t sum = 0;
            fprintf(txt, "# Destruction Derby 64 font atlas %ux%u, glyph height %u, %s\n", w, h, height,
                    siz == 3 ? "RGBA32" : fmt == 3 ? "IA16" : "RGBA16");
            fprintf(txt, "# index char x y width (texels; y is the glyph's top row)\n");
            for (i = 0; i < numChars; i++) {
                uint32_t gw = rd8(widths + i);
                uint32_t c = map != NULL ? rd8(map + i) : 0;
                fprintf(txt, "%u %c %u %u %u\n", i, (c > 32 && c < 127) ? (int)c : '?', sum % perRow,
                        (sum / perRow) * height, gw);
                sum += gw;
            }
            fclose(txt);
        }
        Port_Log("fontdump: font %016llx, %ux%u, %u glyphs %u high", (unsigned long long)hash, w, h, numChars,
                 height);
    }
    SDL_free(rgba);
}
