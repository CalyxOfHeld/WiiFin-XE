#!/usr/bin/env python3
"""Build apps/WiiFin/icon.png, WiiFin-XE's Homebrew Channel icon (128 x 48):
a Jellyfin-style mark (a rounded triangle around a smaller one, in
Jellyfin's purple-to-blue) holding a Wii Remote, and the name.

    python tools/hbc_icon.py [font.ttf]

The font defaults to Segoe UI Bold (Windows).
"""
import math, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

W, H, SS = 128, 48, 8
PURPLE, BLUE = (0xAA, 0x5C, 0xC3), (0x00, 0xA4, 0xDC)   # Jellyfin's gradient
GREY = (0x4A, 0x4D, 0x52)

def gradient(size, a=PURPLE, b=BLUE):
    """top left purple to bottom right blue"""
    w, h = size
    g = Image.new("RGB", size)
    px = g.load()
    for y in range(h):
        for x in range(w):
            t = (x / w * 0.45 + y / h * 0.55)
            px[x, y] = tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))
    return g

def rounded_triangle(cx, cy, r, round_r, n=24):
    """an upward triangle (circumradius r) with rounded corners, as points"""
    pts = []
    for k in range(3):
        ang = math.radians(-90 + 120 * k)
        vx, vy = cx + (r - round_r * 2) * math.cos(ang), cy + (r - round_r * 2) * math.sin(ang)
        for j in range(n + 1):
            a = ang - math.radians(60) + math.radians(120) * j / n
            pts.append((vx + round_r * math.cos(a), vy + round_r * math.sin(a)))
    return pts

def mark(size):
    """the mask of the mark: a thick rounded triangle, a smaller one inside"""
    s = size * SS
    m = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(m)
    cx, cy = s / 2, s * 0.6
    d.polygon(rounded_triangle(cx, cy, s * 0.56, s * 0.07), fill=255)
    d.polygon(rounded_triangle(cx, cy, s * 0.40, s * 0.05), fill=0)
    d.polygon(rounded_triangle(cx, cy + s * 0.04, s * 0.22, s * 0.035), fill=255)
    return m

def remote(size):
    """a Wii Remote standing in the mark, tilted, its D-pad and A button cut out"""
    s = size * SS
    m = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(m)
    w, h = s * 0.17, s * 0.62
    x0, y0 = (s - w) / 2, s * 0.14
    d.rounded_rectangle((x0, y0, x0 + w, y0 + h), radius=w * 0.45, fill=255)
    cx = s / 2
    pad, t = w * 0.30, w * 0.10
    py = y0 + h * 0.17
    d.rectangle((cx - pad, py - t, cx + pad, py + t), fill=0)
    d.rectangle((cx - t, py - pad, cx + t, py + pad), fill=0)
    d.ellipse((cx - w * 0.2, y0 + h * 0.33, cx + w * 0.2, y0 + h * 0.33 + w * 0.4), fill=0)
    return m.rotate(-18, resample=Image.BICUBIC, center=(s / 2, s * 0.55))

def main():
    font_path = sys.argv[1] if len(sys.argv) > 1 else "C:/Windows/Fonts/segoeuib.ttf"
    big = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))

    # the mark, 46 px, on the left
    msize = 46
    mm = mark(msize)
    rm = remote(msize)
    shape = ImageChops.lighter(mm, rm)
    col = gradient(mm.size)
    layer = Image.new("RGBA", mm.size, (0, 0, 0, 0))
    layer.paste(col, (0, 0), mm)
    white = Image.new("RGBA", mm.size, (255, 255, 255, 255))
    # the remote: white with a gradient edge where it crosses the mark
    edge = rm.filter(ImageFilter.MaxFilter(SS * 2 + 1))
    layer.paste(col, (0, 0), edge)
    layer.paste(white, (0, 0), rm)
    shadow = shape.filter(ImageFilter.MaxFilter(SS + 1)).filter(ImageFilter.GaussianBlur(SS))
    sh = Image.new("RGBA", mm.size, (0, 0, 0, 0))
    sh.paste((0, 0, 0, 70), (0, 0), shadow)
    mx, my = 0, (H - msize) // 2 * SS
    big.alpha_composite(sh, (mx + SS // 2, my + SS))
    big.alpha_composite(layer, (mx, my))

    # the name: "WiiFin" (Wii grey, Fin in the gradient), "XE" a gradient tag
    tx = (msize + 2) * SS
    f = ImageFont.truetype(font_path, 21 * SS)
    fx = ImageFont.truetype(font_path, 11 * SS)
    text = Image.new("L", big.size, 0)
    grey = Image.new("L", big.size, 0)
    dt, dg = ImageDraw.Draw(text), ImageDraw.Draw(grey)
    base = 26 * SS                                  # baseline
    dg.text((tx, base), "Wii", font=f, fill=255, anchor="ls")
    fin_x = tx + dg.textlength("Wii", font=f) + SS * 0.5
    dt.text((fin_x, base), "Fin", font=f, fill=255, anchor="ls")
    name_w = fin_x + dt.textlength("Fin", font=f) - tx
    # XE tag under the name, right-aligned with it
    tag_w, tag_h = dt.textlength("XE", font=fx) + 9 * SS, 13 * SS
    tag_x1 = tx + name_w
    tag_x0, tag_y0 = tag_x1 - tag_w, base + 4 * SS
    tag = Image.new("L", big.size, 0)
    ImageDraw.Draw(tag).rounded_rectangle((tag_x0, tag_y0, tag_x1, tag_y0 + tag_h), radius=tag_h / 2, fill=255)
    tagtext = Image.new("L", big.size, 0)
    ImageDraw.Draw(tagtext).text(((tag_x0 + tag_x1) / 2, tag_y0 + tag_h / 2), "XE", font=fx, fill=255, anchor="mm")
    # a dash before the tag, in grey, so it reads "WiiFin-XE"
    dash_y = tag_y0 + tag_h / 2
    ImageDraw.Draw(grey).rounded_rectangle((tag_x0 - 7 * SS, dash_y - SS, tag_x0 - 2 * SS, dash_y + SS), radius=SS, fill=255)

    # the name's gradient runs across "Fin" and the tag: purple to blue
    full = Image.new("RGB", big.size)
    x0, x1 = fin_x, tag_x1
    for x in range(big.width):
        t = min(1.0, max(0.0, (x - x0) / (x1 - x0)))
        full.paste(tuple(round(PURPLE[i] + (BLUE[i] - PURPLE[i]) * t) for i in range(3)), (x, 0, x + 1, big.height))
    shadow = ImageChops.lighter(ImageChops.lighter(text, grey), tag).filter(ImageFilter.GaussianBlur(SS * 0.8))
    big.paste((0, 0, 0, 60), (SS // 2, SS), shadow.crop((0, 0, big.width - SS // 2, big.height - SS)))
    big.paste(GREY + (255,), (0, 0), grey)
    big.paste(full, (0, 0), text)
    big.paste(full, (0, 0), tag)
    big.paste((255, 255, 255, 255), (0, 0), tagtext)

    out = big.resize((W, H), Image.LANCZOS)
    out.save("apps/WiiFin/icon.png", optimize=True)

if __name__ == "__main__":
    main()
