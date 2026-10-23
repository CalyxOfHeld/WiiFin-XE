#!/usr/bin/env python3
"""Build the screensaver's VHS-style and CD-style logos (white on clear;
WiiFin tints them a new colour at each edge, as the WiiFin logo):

    data/images/saver_vhs.png   400 x 200: "VHS" filling a framed box
    data/images/saver_cd.png    292 x 168: "COMPACT", an outlined "disc",
                                "DIGITAL AUDIO"

    python tools/screensaver_logos.py

They are WiiFin's own drawings in the style of the old tape and disc marks
(Segoe UI Black, Windows), not the marks themselves.
"""
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

SS = 4                                   # drawn larger, then scaled down
BLACK = "C:/Windows/Fonts/seguibl.ttf"
BOLD = "C:/Windows/Fonts/segoeuib.ttf"


def fit_text(draw, text, font_path, box_w, box_h, spacing=0):
    """the biggest font for which text fits box_w x box_h"""
    size = box_h * 2
    while size > 8:
        f = ImageFont.truetype(font_path, size)
        l, t, r, b = draw.textbbox((0, 0), text, font=f)
        if (r - l) + spacing * (len(text) - 1) <= box_w and (b - t) <= box_h:
            return f
        size -= 2
    return ImageFont.truetype(font_path, size)


def spaced(draw, xy, text, font, spacing, fill=255):
    """text with extra space between its letters; xy its ink's top left"""
    x, y = xy
    for ch in text:
        l, t, r, b = draw.textbbox((0, 0), ch, font=font)
        draw.text((x - l, y - t), ch, font=font, fill=fill)
        x += (r - l) + spacing


def ink_width(draw, text, font, spacing):
    w = 0
    for ch in text:
        l, t, r, b = draw.textbbox((0, 0), ch, font=font)
        w += (r - l)
    return w + spacing * (len(text) - 1)


def vhs(path):
    W, H = 400 * SS, 200 * SS
    m = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(m)
    # the frame: a thick line just inside the edge
    edge, line = 4 * SS, 14 * SS
    d.rectangle((edge, edge, W - 1 - edge, H - 1 - edge), fill=255)
    d.rectangle((edge + line, edge + line, W - 1 - edge - line, H - 1 - edge - line), fill=0)
    # the letters: heavy, tight, filling the box
    pad = edge + line + 14 * SS
    gap = 6 * SS
    f = fit_text(d, "VHS", BLACK, W - 2 * pad, H - 2 * pad, gap)
    iw = ink_width(d, "VHS", f, gap)
    l, t, r, b = d.textbbox((0, 0), "VHS", font=f)
    spaced(d, ((W - iw) // 2, (H - (b - t)) // 2), "VHS", f, gap)
    save(m, path)


WORD = "dısc"      # a dotless i: "COMPACT" sits where its dot would be


def cd(path):
    W, H = 400 * SS, 300 * SS
    m = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(m)
    # "disc": outlined, big; the outline a ring round each letter
    f = ImageFont.truetype(BOLD, 150 * SS)
    l, t, r, b = d.textbbox((0, 0), WORD, font=f)
    dw = r - l
    dx, top = (W - dw) // 2 - l, 30 * SS
    dy = top - t
    stroke = 4 * SS
    outer = Image.new("L", (W, H), 0)
    ImageDraw.Draw(outer).text((dx, dy), WORD, font=f, fill=255, stroke_width=stroke, stroke_fill=255)
    inner = Image.new("L", (W, H), 0)
    ImageDraw.Draw(inner).text((dx, dy), WORD, font=f, fill=255)
    for _ in range(stroke // (2 * SS)):
        inner = inner.filter(ImageFilter.MinFilter(2 * SS + 1 if (2 * SS + 1) <= 9 else 9))
    m = ImageChops.lighter(m, ImageChops.subtract(outer, inner))
    d = ImageDraw.Draw(m)
    left, right = (W - dw) // 2 - stroke, (W + dw) // 2 + stroke
    ink_top, ink_bottom = top - stroke, top + (b - t) + stroke
    # the x-height: where "sc" starts
    sl, st, sr, sb = d.textbbox((dx, dy), "sc", font=f)
    x_top = st - stroke
    # "COMPACT": beside the d's tall stroke, above the short letters
    gap = 4 * SS
    d_right = d.textbbox((dx, dy), "d", font=f)[2] + stroke
    fc = fit_text(d, "COMPACT", BLACK, right - d_right - 10 * SS, x_top - ink_top - 8 * SS, gap)
    cw = ink_width(d, "COMPACT", fc, gap)
    cl, ct, cr, cb = d.textbbox((0, 0), "C", font=fc)
    spaced(d, (right - cw, ink_top + (x_top - ink_top - 8 * SS - (cb - ct))), "COMPACT", fc, gap)
    # "DIGITAL AUDIO": under it, as wide as "disc"
    fa = fit_text(d, "DIGITAL AUDIO", BLACK, right - left, 40 * SS, gap)
    aw = ink_width(d, "DIGITAL AUDIO", fa, gap)
    spaced(d, (left + (right - left - aw) // 2, ink_bottom + 10 * SS), "DIGITAL AUDIO", fa, gap)
    save(m, path)


def save(mask, path):
    """white, the mask its alpha, cropped to the ink (a 4-pixel edge, sides
    a multiple of 4), scaled down"""
    small = mask.resize((mask.width // SS, mask.height // SS), Image.LANCZOS)
    l, t, r, b = small.getbbox()
    l, t = max(0, l - 4), max(0, t - 4)
    r, b = min(small.width, r + 4), min(small.height, b + 4)
    w, h = (r - l + 3) // 4 * 4, (b - t + 3) // 4 * 4
    a = Image.new("L", (w, h), 0)
    a.paste(small.crop((l, t, r, b)), (0, 0))
    out = Image.new("RGBA", (w, h), (255, 255, 255, 0))
    out.putalpha(a)
    out.save(path, optimize=True)
    print(path, w, "x", h)


if __name__ == "__main__":
    vhs(sys.argv[1] if len(sys.argv) > 1 else "data/images/saver_vhs.png")
    cd(sys.argv[2] if len(sys.argv) > 2 else "data/images/saver_cd.png")
