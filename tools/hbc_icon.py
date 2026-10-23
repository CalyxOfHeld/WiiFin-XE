#!/usr/bin/env python3
"""Build apps/WiiFin/icon.png, WiiFin-XE's Homebrew Channel icon (128 x 48):
a jellyfish in Jellyfin's purple-to-blue and the name, on a dark plate so
both read on any background.

    python tools/hbc_icon.py [out.png]

Fonts: Segoe UI Bold and Black (Windows).
"""
import math, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

W, H, SS = 128, 48, 8
PURPLE, BLUE = (0xAA, 0x5C, 0xC3), (0x00, 0xA4, 0xDC)   # Jellyfin's gradient
LILAC, SKY = (0xC9, 0x8B, 0xE0), (0x3C, 0xC2, 0xF2)     # the same, lighter: text on the plate
PLATE_TOP, PLATE_BOTTOM = (0x22, 0x26, 0x38), (0x10, 0x12, 0x1C)

def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))

def diagonal(size, a, b):
    """top left a, bottom right b"""
    w, h = size
    g = Image.new("RGB", size)
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            g.paste(lerp(a, b, min(1.0, x / w * 0.4 + y / h * 0.6)), (x, y, x + 2, y + 2))
    return g

def horizontal(size, a, b, x0, x1):
    w, h = size
    g = Image.new("RGB", size)
    for x in range(w):
        g.paste(lerp(a, b, min(1.0, max(0.0, (x - x0) / (x1 - x0)))), (x, 0, x + 1, h))
    return g

def vertical(size, a, b):
    w, h = size
    g = Image.new("RGB", size)
    for y in range(h):
        g.paste(lerp(a, b, y / (h - 1)), (0, y, w, y + 1))
    return g

def jellyfish(s):
    """masks (s x s): the body (bell and tentacles), the bell's shine"""
    body = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(body)
    cx, rw, top, bh = s * 0.5, s * 0.40, s * 0.06, s * 0.40     # bell: centre, half width, top, height
    # the bell: the top half of an ellipse, its rim scalloped
    d.pieslice((cx - rw, top, cx + rw, top + bh * 2), 180, 360, fill=255)
    rim = top + bh
    n = 5
    sr = rw / n
    for k in range(n):
        x = cx - rw + sr * (2 * k + 1)
        d.ellipse((x - sr, rim - sr * 0.9, x + sr, rim + sr * 0.9), fill=255)
    # tentacles: wavy, thinning toward their ends
    for k, x0 in enumerate((-0.55, -0.18, 0.18, 0.55)):
        phase = k * 1.7
        y_start, y_end = rim + sr * 0.5, s * (0.93 if k in (1, 2) else 0.84)
        steps = 60
        for i in range(steps):
            t = i / (steps - 1)
            y = y_start + (y_end - y_start) * t
            x = cx + x0 * rw * (1 - 0.25 * t) + math.sin(t * 2.6 * math.pi + phase) * s * 0.035
            r = s * (0.045 - 0.03 * t)
            d.ellipse((x - r, y - r, x + r, y + r), fill=255)
    # the shine: a crescent at the bell's upper left
    shine = Image.new("L", (s, s), 0)
    ds = ImageDraw.Draw(shine)
    ds.pieslice((cx - rw * 0.72, top + bh * 0.22, cx + rw * 0.5, top + bh * 1.6), 200, 265, fill=255)
    cut = Image.new("L", (s, s), 0)
    ImageDraw.Draw(cut).pieslice((cx - rw * 0.6, top + bh * 0.36, cx + rw * 0.62, top + bh * 1.8), 195, 270, fill=255)
    shine = ImageChops.subtract(shine, cut)
    return body, shine

def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "apps/WiiFin/icon.png"
    font_path, black_path = "C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/seguibl.ttf"
    size = (W * SS, H * SS)
    big = Image.new("RGBA", size, (0, 0, 0, 0))

    # the plate: dark, rounded, a faint gradient edge
    plate = Image.new("L", size, 0)
    ImageDraw.Draw(plate).rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), radius=9 * SS, fill=255)
    inner = Image.new("L", size, 0)
    ImageDraw.Draw(inner).rounded_rectangle((SS, SS, size[0] - 1 - SS, size[1] - 1 - SS), radius=8 * SS, fill=255)
    big.paste(diagonal(size, PURPLE, BLUE), (0, 0), plate)
    big.paste(vertical(size, PLATE_TOP, PLATE_BOTTOM), (0, 0), inner)

    # the jellyfish, on the left, with a soft glow
    js = 40 * SS
    body, shine = jellyfish(js)
    jx, jy = 5 * SS, 4 * SS
    glow = body.filter(ImageFilter.GaussianBlur(3 * SS)).point(lambda a: a * 0.45)
    gl = Image.new("RGBA", (js, js), (0, 0, 0, 0))
    gl.paste(BLUE + (255,), (0, 0), glow)
    big.alpha_composite(gl, (jx, jy))
    jelly = Image.new("RGBA", (js, js), (0, 0, 0, 0))
    jelly.paste(diagonal((js, js), LILAC, BLUE), (0, 0), body)
    jelly.paste((255, 255, 255, 255), (0, 0), shine.point(lambda a: a * 0.85))
    big.alpha_composite(jelly, (jx, jy))

    # the name: "Wii" white, "Fin" in the gradient, "XE" a gradient tag
    tx = 47 * SS
    f = ImageFont.truetype(font_path, 21 * SS)
    fx = ImageFont.truetype(black_path, 13 * SS)
    white = Image.new("L", size, 0)
    fin = Image.new("L", size, 0)
    dw, df = ImageDraw.Draw(white), ImageDraw.Draw(fin)
    base = 23 * SS
    dw.text((tx, base), "Wii", font=f, fill=255, anchor="ls")
    fin_x = tx + dw.textlength("Wii", font=f) + SS * 0.5
    df.text((fin_x, base), "Fin", font=f, fill=255, anchor="ls")
    name_end = fin_x + df.textlength("Fin", font=f)
    tag_w, tag_h = dw.textlength("XE", font=fx) + 10 * SS, 16 * SS
    tag_x1, tag_y0 = name_end, base + 4 * SS
    tag_x0 = tag_x1 - tag_w
    tag = Image.new("L", size, 0)
    ImageDraw.Draw(tag).rounded_rectangle((tag_x0, tag_y0, tag_x1, tag_y0 + tag_h), radius=tag_h / 2, fill=255)
    tagtext = Image.new("L", size, 0)
    ImageDraw.Draw(tagtext).text(((tag_x0 + tag_x1) / 2, tag_y0 + tag_h / 2), "XE", font=fx, fill=255, anchor="mm")
    dash_y = tag_y0 + tag_h / 2
    dw.rounded_rectangle((tag_x0 - 9 * SS, dash_y - 1.25 * SS, tag_x0 - 3 * SS, dash_y + 1.25 * SS), radius=SS, fill=255)

    grad = horizontal(size, LILAC, SKY, fin_x, tag_x1)
    big.paste((255, 255, 255, 255), (0, 0), white)
    big.paste(grad, (0, 0), fin)
    big.paste(horizontal(size, PURPLE, BLUE, tag_x0, tag_x1), (0, 0), tag)
    big.paste((255, 255, 255, 255), (0, 0), tagtext)

    big.resize((W, H), Image.LANCZOS).save(out_path, optimize=True)

if __name__ == "__main__":
    main()
