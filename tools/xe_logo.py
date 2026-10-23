#!/usr/bin/env python3
"""Build data/images/logo_wiifin.png, WiiFin-XE's logo (512 x 128): the
HBC icon's jellyfish, "Wii" in grey and "Fin" in Jellyfin's gradient, and
the XE tag, on a transparent background (drawn on the splash, the WiiFin
menu and the home's header, light or dark).

    python tools/xe_logo.py [out.png]

Fonts: Segoe UI Bold and Black (Windows).
"""
import os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hbc_icon import PURPLE, BLUE, LILAC, diagonal, horizontal, jellyfish

W, H, SS = 512, 128, 4
GREY = (0x8E, 0x93, 0x96)        # "Wii": reads on the light and the dark themes

def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "data/images/logo_wiifin.png"
    font_path, black_path = "C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/seguibl.ttf"
    size = (W * SS, H * SS)
    big = Image.new("RGBA", size, (0, 0, 0, 0))

    # the jellyfish, on the left, with a soft glow
    js = 124 * SS
    body, shine = jellyfish(js)
    jx, jy = 0, 2 * SS
    glow = body.filter(ImageFilter.GaussianBlur(5 * SS)).point(lambda a: a * 0.35)
    gl = Image.new("RGBA", (js, js), (0, 0, 0, 0))
    gl.paste(BLUE + (255,), (0, 0), glow)
    big.alpha_composite(gl, (jx, jy))
    jelly = Image.new("RGBA", (js, js), (0, 0, 0, 0))
    jelly.paste(diagonal((js, js), LILAC, BLUE), (0, 0), body)
    jelly.paste((255, 255, 255, 255), (0, 0), shine.point(lambda a: a * 0.85))
    big.alpha_composite(jelly, (jx, jy))

    # the name, then the XE tag after it
    tx = 128 * SS
    f = ImageFont.truetype(font_path, 92 * SS)
    fx = ImageFont.truetype(black_path, 40 * SS)
    grey = Image.new("L", size, 0)
    fin = Image.new("L", size, 0)
    dg, df = ImageDraw.Draw(grey), ImageDraw.Draw(fin)
    base = 96 * SS
    dg.text((tx, base), "Wii", font=f, fill=255, anchor="ls")
    fin_x = tx + dg.textlength("Wii", font=f) + 2 * SS
    df.text((fin_x, base), "Fin", font=f, fill=255, anchor="ls")
    name_end = fin_x + df.textlength("Fin", font=f)
    tag_h = 50 * SS
    tag_w = dg.textlength("XE", font=fx) + 26 * SS
    tag_x0 = name_end + 12 * SS
    tag_x1 = tag_x0 + tag_w
    tag_y0 = base - tag_h
    if tag_x1 > size[0] - 2 * SS:
        raise SystemExit("the logo is wider than %d px" % W)
    tag = Image.new("L", size, 0)
    ImageDraw.Draw(tag).rounded_rectangle((tag_x0, tag_y0, tag_x1, tag_y0 + tag_h), radius=tag_h / 2, fill=255)
    tagtext = Image.new("L", size, 0)
    ImageDraw.Draw(tagtext).text(((tag_x0 + tag_x1) / 2, tag_y0 + tag_h / 2), "XE", font=fx, fill=255, anchor="mm")

    big.paste(GREY + (255,), (0, 0), grey)
    big.paste(horizontal(size, PURPLE, BLUE, fin_x, name_end), (0, 0), fin)
    big.paste(horizontal(size, PURPLE, BLUE, tag_x0, tag_x1), (0, 0), tag)
    big.paste((255, 255, 255, 255), (0, 0), tagtext)

    big.resize((W, H), Image.LANCZOS).save(out_path, optimize=True)

if __name__ == "__main__":
    main()
