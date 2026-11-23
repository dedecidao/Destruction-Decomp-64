#include "common.h"
#include "m2c.h"

typedef struct { u32 w0; u32 w1; } Gfx;
typedef struct { u8 pad[0xC]; s32 xC; s32 x10; } Img;
typedef struct {
    u8 pad0[4];
    s32 x4;
    u8 pad8[8];
    Img *x10;
    void *x14;
    f32 x18;
    f32 x1C;
    f32 x20;
    f32 x24;
    f32 x28;
    f32 x2C;
    f32 x30;
    s32 x34;
    s32 x38;
    f32 x3C;
    f32 x40;
} ElemA;
typedef struct {
    u8 pad0[4];
    s32 x4;
    u8 pad8[0x10];
    u8 x18[0x60];
    f32 x78;
    f32 x7C;
    s32 x80;
    s32 x84;
    s32 x88;
    s32 x8C;
} ElemB;
typedef struct {
    u8 pad0[4];
    s32 x4;
    u8 pad8[8];
    s32 x10;
    s32 x14;
    s16 x18;
    s16 x1A;
    s8 x1C;
} ElemC;
typedef struct Node { ElemA *x0; struct Node *x4; } Node;
typedef struct { u8 pad[0x8B54]; Node *x8B54; } Ctx;

Gfx *Gfx_SetupRenderModes(Gfx *);
void Sprite_SetMtxPosF(void *, f32, f32);
void Sprite_SetBaseScaleF(void *, f32, f32);
void func_800626A8(void *, f32, f32);
void Sprite_SetRotationF(void *, f32);
void Sprite_SetScaleF(void *, f32, f32);
void Sprite_SetPosF(void *, s32, s32);
Gfx *Sprite_Draw(void *, Gfx *);
void Bg_SetScale(void *, f32, f32, s32);
void Sprite_SetSize(void *, s32, s32, s32, s32);
Gfx *Bg_Draw(void *, Gfx *, s32);
Gfx *Font_DrawText(Gfx *, s32, s32, s32, s32, s32);
Gfx *Gfx_RestoreF3dexUcode(Gfx *);

#define GFX2(g,a,b) { Gfx *_g = (g)++; _g->w0 = (a); _g->w1 = (u32)(b); }

Gfx *Ui_Draw(Ctx *ctx, Gfx *g) {
    Node *n;
    ElemA *e;
#ifdef TARGET_PC
    /* the widescreen HUD layout's offset last put in the display list (port/game/wm_mod.c) */
    s32 hudDx = 0, hudDy = 0, hudS = 4096;
#endif

    if (ctx->x8B54 == NULL) {
        return g;
    }
    g = Gfx_SetupRenderModes(g);
    GFX2(g, 0xED000000, 0x5003C0);
    for (n = ctx->x8B54; n != NULL; n = n->x4) {
        e = n->x0;
#ifdef TARGET_PC
        {
            /* [video] hud_boxes: the PC options may hide some 2D elements */
            s32 Port_UiHidden(s32 id, s32 type);
            if (Port_UiHidden(*(s32 *)e, e->x4)) {
                continue;
            }
        }
        {
            /* Wrecked Metal's widescreen HUD: the element's group moved toward the screen's edge */
            void WM_HudOffset(int id, int *dx4, int *dy4, int *scale4096);
            s32 dx, dy, s;
            WM_HudOffset(*(s32 *)e, &dx, &dy, &s);
            if (dx != hudDx || dy != hudDy || s != hudS) {
                GFX2(g, 0x00490000, s);
                GFX2(g, 0x00480000 | (dy & 0xFFFF), dx);
                hudDx = dx;
                hudDy = dy;
                hudS = s;
            }
        }
#endif
        switch (e->x4) {
        case 1: {
            ElemA *a = e;
            Sprite_SetMtxPosF(a->x14, a->x18, a->x1C);
            if (a->x28 != 1.0f || a->x2C != 1.0f) {
                Sprite_SetBaseScaleF(a->x14, a->x28, a->x2C);
            }
            if (a->x20 != 0.0f || a->x24 != 0.0f) {
                func_800626A8(a->x14, a->x20, a->x24);
            }
            if (a->x30 != 0.0f) {
                Sprite_SetRotationF(a->x14, a->x30);
            }
            if ((f32)a->x10->xC != a->x3C || (f32)a->x10->x10 != a->x40) {
                Sprite_SetScaleF(a->x14, a->x3C, a->x40);
            }
            if (a->x3C != 0.0f || a->x40 != 0.0f) {
                Sprite_SetPosF(a->x14, a->x34, a->x38);
            }
            g = Sprite_Draw(a->x14, g);
            break;
        }
        case 2: {
            ElemB *b = (ElemB *)e;
#ifdef TARGET_PC
            {
                /* Wrecked Metal: some pictures drawn smaller, about their centre (the start lights) */
                float WM_HudScale(int id);
                f32 k = WM_HudScale(*(s32 *)e);
                if (k != 1.0f) {
                    f32 sx = b->x78 * k, sy = b->x7C * k;
                    Bg_SetScale(b->x18, sx, sy, 0);
                    Sprite_SetSize(b->x18, b->x80 + (s32)(b->x88 * b->x78 * (1.0f - k) / 2.0f),
                                   b->x84 + (s32)(b->x8C * b->x7C * (1.0f - k) / 2.0f), b->x88 * sx, b->x8C * sy);
                    g = Bg_Draw(b->x18, g, 0);
                    break;
                }
            }
#endif
            if (b->x78 != 1.0 || b->x7C != 1.0) {
                Bg_SetScale(b->x18, b->x78, b->x7C, 0);
            }
            Sprite_SetSize(b->x18, b->x80, b->x84, b->x88 * b->x78, b->x8C * b->x7C);
#ifdef TARGET_PC
            {
                /* where a sprite was drawn this frame (the title's credit line, pcmenu.c) */
                void Port_UiSpriteDrawn(s32 id, s32 x, s32 y, s32 w, s32 h);
                Port_UiSpriteDrawn(*(s32 *)e, b->x80, b->x84, b->x88 * b->x78, b->x8C * b->x7C);
            }
#endif
            g = Bg_Draw(b->x18, g, 0);
            break;
        }
        case 0: {
            ElemC *c = (ElemC *)e;
            GFX2(g, 0xE200001C, 0x503048);
            GFX2(g, 0xE3000A01, 0);
#ifdef TARGET_PC
            {
                /* DD64_UILOG: which text is which element */
                void Port_UiLogText(s32 id, const u8 *str, s32 x, s32 y);
                Port_UiLogText(*(s32 *)c, (const u8 *)c->x14, c->x18, c->x1A);
            }
            {
                /* [video] sharp_text: the text point sampled, not bilinear. At
                   320x240 the filter hardly shows; scaled up it blurs the glyphs */
                s32 Port_TextFilter(void);
                GFX2(g, 0xE3001201, Port_TextFilter());
            }
#else
            GFX2(g, 0xE3001201, 0x2000);
#endif
#ifdef TARGET_PC
            {
                s32 Port_UiTextDY(s32 id); /* lines moved for the HD fonts */
                g = Font_DrawText(g, c->x10, c->x14, c->x18, c->x1A + Port_UiTextDY(*(s32 *)c), c->x1C);
            }
#else
            g = Font_DrawText(g, c->x10, c->x14, c->x18, c->x1A, c->x1C);
#endif
            break;
        }
        }
    }
#ifdef TARGET_PC
    if (hudDx != 0 || hudDy != 0 || hudS != 4096) {
        GFX2(g, 0x00490000, 4096); /* the HUD scale and offset back off */
        GFX2(g, 0x00480000, 0);
    }
#endif
    return Gfx_RestoreF3dexUcode(g);
}
