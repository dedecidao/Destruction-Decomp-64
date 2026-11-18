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
 *
 * A PNG of the same name in Data/Textures/fonts_hd/ (any size keeping the
 * atlas's shape: 4x is 576x144 for a 144x36 atlas, with a real alpha channel)
 * is drawn in its place, glyph by glyph (gfx_hd_atlas_register). This needs
 * no environment variable: a file there is the opt-in.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "port.h"
#include "port_config.h"
#include "port_shadow.h"

void gfx_hd_atlas_register(const uint8_t *tex, uint32_t stride, uint32_t bpp, uint32_t w, uint32_t h,
                           const uint8_t *orig_rgba, const uint8_t *hd_rgba, uint32_t hw, uint32_t hh);

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

/* The atlases seen, by address, with a copy of their bytes: the game loads and
   frees fonts as screens change, and a new font can land where an old one was,
   so a font is new whenever the bytes at its atlas differ from the copy */
typedef struct {
    const uint8_t *tex;
    uint8_t *raw;
    uint32_t len;
} Seen;
static Seen sSeen[MAX_FONTS];
static int sNumSeen, sNextSlot;
static int sDump = -1;

/* Data/Textures/fonts_hd/font_<hash>_<w>x<h>.png, if there is one of this shape */
static void loadHd(uint64_t hash, const uint8_t *tex, uint32_t stride, uint32_t bpp, uint32_t w, uint32_t h,
                   const uint8_t *rgba) {
    char path[1200];
    SDL_Surface *s, *c;

    SDL_snprintf(path, sizeof(path), "%sData/Textures/fonts_hd/font_%016llx_%ux%u.png", Port_BaseDir(),
                 (unsigned long long)hash, w, h);
    if (!SDL_GetPathInfo(path, NULL)) {
        gfx_hd_atlas_register(tex, stride, bpp, w, h, NULL, NULL, 0, 0); /* none: forget an old one here */
        return;
    }
    s = SDL_LoadPNG(path);
    if (s == NULL) {
        Port_Log("fonthd: %s not read: %s", path, SDL_GetError());
        return;
    }
    if (!SDL_ISPIXELFORMAT_ALPHA(s->format) && !SDL_SurfaceHasColorKey(s)) {
        Port_Log("fonthd: %s has no transparency (alpha channel); the glyphs would draw in boxes. Not used", path);
        SDL_DestroySurface(s);
        return;
    }
    if (s->w < (int)w || s->h < (int)h ||
        SDL_fabs((double)s->w / w - (double)s->h / h) > 0.03 * ((double)s->w / w)) {
        Port_Log("fonthd: %s is %dx%d, not %ux%u scaled the same both ways. Not used", path, s->w, s->h, w, h);
        SDL_DestroySurface(s);
        return;
    }
    c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(s);
    if (c == NULL) {
        return;
    }
    if (c->pitch == c->w * 4) {
        /* (registering replaces whatever was registered over this memory) */
        gfx_hd_atlas_register(tex, stride, bpp, w, h, rgba, c->pixels, (uint32_t)c->w, (uint32_t)c->h);
    } else {
        uint8_t *packed = SDL_malloc((size_t)c->w * c->h * 4);
        int y;
        if (packed != NULL) {
            for (y = 0; y < c->h; y++) {
                SDL_memcpy(packed + (size_t)y * c->w * 4, (uint8_t *)c->pixels + (size_t)y * c->pitch, (size_t)c->w * 4);
            }
            gfx_hd_atlas_register(tex, stride, bpp, w, h, rgba, packed, (uint32_t)c->w, (uint32_t)c->h);
            SDL_free(packed);
        }
    }
    Port_Log("fonthd: font %016llx drawn from %dx%d", (unsigned long long)hash, c->w, c->h);
    SDL_DestroySurface(c);
}

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

    if (sDump < 0) {
        sDump = SDL_getenv("DD64_FONTDUMP") != NULL;
    }
    if (font == NULL) {
        return;
    }
    tex = hostPtr(Port_RdN64(f + F_TEX, 4));
    widths = hostPtr(Port_RdN64(f + F_WIDTHS, 4));
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
    {
        Seen *e = NULL;
        uint32_t len = w * h * bpp;
        for (i = 0; i < (uint32_t)sNumSeen; i++) {
            if (sSeen[i].tex == tex) {
                e = &sSeen[i];
            }
        }
        if (e != NULL && e->len == len && SDL_memcmp(e->raw, tex, len) == 0) {
            return; /* the same font as last time */
        }
        if (e == NULL) {
            e = &sSeen[sNumSeen < MAX_FONTS ? sNumSeen++ : (sNextSlot++ % MAX_FONTS)];
        }
        SDL_free(e->raw);
        e->tex = tex;
        e->len = len;
        e->raw = SDL_malloc(len);
        if (e->raw != NULL) {
            SDL_memcpy(e->raw, tex, len);
        }
    }
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
    loadHd(hash, tex, perRow * bpp, bpp, w, h, rgba);
    if (!sDump) {
        SDL_free(rgba);
        return;
    }

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
            fprintf(txt, "# index code char x y width (texels; y is the glyph's top row)\n");
            for (i = 0; i < numChars; i++) {
                uint32_t gw = rd8(widths + i);
                uint32_t c = map != NULL ? rd8(map + i) : 0;
                fprintf(txt, "%u %u %c %u %u %u\n", i, c, (c > 32 && c < 127) ? (int)c : '.', sum % perRow,
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
