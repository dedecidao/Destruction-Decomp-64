"""Redraw the UI font atlases dumped with DD64_FONTDUMP at 4x from TrueType
fonts (Tahoma Bold for text, Impact for numbers on Windows), with a vertical
gradient and the original drop shadow, each glyph inside its original cell.

Usage: gen_hd_fonts.py <fonts_original> <fonts_original with code columns> <fonts_hd>
The output goes in Data/Textures/fonts_hd/. Requires Pillow."""
import glob, os, sys
from collections import Counter
from PIL import Image, ImageDraw, ImageFont

ORIG = sys.argv[1]          # fonts_original (PNGs + txt; txt with codes preferred)
CODES = sys.argv[2]         # a fonts_original with the newer txt (index code char x y w)
OUT = sys.argv[3]
S = 4
WIN = os.environ.get('FONTS_DIR', '/mnt/c/Windows/Fonts/')  # Windows' fonts, seen from WSL
TEXT_FONT, NUM_FONT = WIN + 'tahomabd.ttf', WIN + 'impact.ttf'
NUMERIC = {'231fc1c11fcbe347', 'ef46c82fdd369d40'}
# Glyph height as a share of the original's, on the same baseline: the race HUD's
# white numbers a little smaller, for a gap under the yellow titles above them
SHRINK = {'231fc1c11fcbe347': 0.85}
os.makedirs(OUT, exist_ok=True)

def layout(h):
    for d in (CODES, ORIG):
        p = f'{d}/font_{h}.txt'
        if os.path.exists(p):
            lines = [l.split() for l in open(p) if not l.startswith('#') and l.strip()]
            height = int(open(p).readline().split('glyph height ')[1].split(',')[0])
            out = []
            for l in lines:
                if len(l) == 6:
                    i, code, _, x, y, w = l; out.append((int(code), int(x), int(y), int(w)))
                else:
                    i, ch, x, y, w = l; out.append((ord(ch), int(x), int(y), int(w)))  # an empty cell is skipped anyway
            return height, out
    raise SystemExit('no layout for ' + h)

def art_char(code, h):
    if h == '54d3a48ed1295e63':
        return {ord('m'): 'MPH', ord('k'): 'KPH'}.get(code, '')
    c = chr(code)
    return {'[': '(', ']': ')'}.get(c, c.lower() if c.isalpha() else c)

def far(p, q):
    """A body colour, not the shadow: well away from it in RGB"""
    return sum((p[i] - q[i]) ** 2 for i in range(3)) > 60 ** 2

def colors(im):
    px = [p for p in im.convert('RGBA').get_flattened_data() if p[3] > 0] if hasattr(im, 'get_flattened_data') else [p for p in im.getdata() if p[3] > 0]
    cnt = Counter(px)
    lum = lambda p: 0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2]
    shadow = min(cnt.most_common(4), key=lambda kv: lum(kv[0]))[0]
    body = [kv for kv in cnt.most_common() if far(kv[0], shadow)]
    main = body[0][0] if body else (255, 255, 255, 255)
    top = tuple(min(255, int(c + (255 - c) * 0.45)) for c in main[:3])
    bot = tuple(int(c * 0.72) for c in main[:3])
    if main[0] > 200 and main[1] > 180 and main[2] < 80:   # yellow: down to orange, like a sunset
        top, bot = (255, 252, 140), (240, 150, 0)
    return shadow[:3], top, bot, lum, shadow

def body_box(im, x, y, w, hh, lum, shadow):
    xs, ys = [], []
    for yy in range(y, y + hh):
        for xx in range(x, x + w):
            p = im.getpixel((xx, yy))
            if p[3] > 0 and far(p, shadow):
                xs.append(xx); ys.append(yy)
    return (min(xs), min(ys), max(xs), max(ys)) if xs else None

for f in sorted(glob.glob(ORIG + '/font_*_*.png')):
    name = os.path.basename(f)
    h = name.split('_')[1]
    im = Image.open(f).convert('RGBA')
    height, glyphs = layout(h)
    shadow, top, bot, lum, shpx = colors(im)
    numeric = h in NUMERIC or all(chr(g[0]).isdigit() for g in glyphs if g[3] > 0)  # a numbers-only font
    ttf = NUM_FONT if numeric else TEXT_FONT
    # cap height and baseline from the '0' (or the first drawn glyph)
    ref = next((g for g in glyphs if chr(g[0]) == '0' and g[3] > 0), None) or next(g for g in glyphs if g[3] > 0)
    bb = body_box(im, ref[1], ref[2], ref[3], height, lum, shpx)
    cap_px = int((bb[3] - bb[1] + 1) * S * SHRINK.get(h, 1.0))
    base_off = (bb[3] + 1 - ref[2]) * S          # baseline from the cell's top
    # font size whose '0' (or 'H') is cap_px tall
    size = 10
    while True:
        fo = ImageFont.truetype(ttf, size)
        b = fo.getbbox('0' if h != '54d3a48ed1295e63' else 'H', anchor='ls')
        if b[3] - b[1] >= cap_px or size > 400:
            break
        size += 1
    fo = ImageFont.truetype(ttf, size)
    off = S                                       # the shadow: one original texel down and right
    out = Image.new('RGBA', (im.width * S, im.height * S), (0, 0, 0, 0))
    for code, x, y, w in glyphs:
        if w == 0:
            continue
        if body_box(im, x, y, w, height, lum, shpx) is None:
            continue                               # an empty cell (a space)
        ch = art_char(code, h)
        if not ch.strip():
            continue
        cw, chh = w * S, height * S
        m = Image.new('L', (cw * 3, chh * 2))
        ImageDraw.Draw(m).text((cw, base_off), ch, font=fo, fill=255, anchor='ls')
        if h == '54d3a48ed1295e63':                # MPH / KPH are italic
            m = m.transform(m.size, Image.AFFINE, (1, 0.22, -0.22 * base_off, 0, 1, 0), Image.BICUBIC)
        box = m.getbbox()
        if box is None:
            continue
        gm = m.crop((box[0], 0, box[2], chh))
        maxw = cw - off - 1
        if gm.width > maxw:                        # too wide for the cell: condense this glyph
            gm = gm.resize((maxw, chh), Image.LANCZOS)
        gx = x * S + (cw - off - gm.width) // 2
        cell = Image.new('RGBA', (cw, chh), (0, 0, 0, 0))
        shadow_l = Image.new('RGBA', gm.size, shadow + (255,))
        cell.paste(shadow_l, (gx - x * S + off, off), gm)
        fill = Image.new('RGBA', gm.size)
        gtop = base_off - cap_px
        for yy in range(gm.height):
            t = min(1, max(0, (yy - gtop) / max(1, cap_px)))
            fill.paste(tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)) + (255,), (0, yy, gm.width, yy + 1))
        cell.paste(fill, (gx - x * S, 0), gm)
        out.alpha_composite(cell, (x * S, y * S))
    out.save(OUT + '/' + name)
    print(name, out.size, 'font', os.path.basename(ttf), 'size', size, 'shadow', shadow, 'grad', top, bot)
