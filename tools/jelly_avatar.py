#!/usr/bin/env python3
"""Build data/images/avatar_jelly.png (128 x 128), the jellyfish profile
picture: the Homebrew Channel icon's jellyfish (hbc_icon.py) on its dark
plate, centred.  WiiFin draws it in a disc, so the corners don't show.

    python tools/jelly_avatar.py [out.png]
"""
import os, sys
from PIL import Image, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hbc_icon import jellyfish, diagonal, vertical, LILAC, BLUE, PLATE_TOP, PLATE_BOTTOM

S, SS = 128, 8

def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "data/images/avatar_jelly.png"
    size = (S * SS, S * SS)
    big = Image.new("RGBA", size, (0, 0, 0, 255))
    big.paste(vertical(size, PLATE_TOP, PLATE_BOTTOM), (0, 0))

    # the jellyfish: most of the disc, a little high (the tentacles are thin)
    js = 92 * SS
    body, shine = jellyfish(js)
    jx, jy = (size[0] - js) // 2, 16 * SS
    glow = body.filter(ImageFilter.GaussianBlur(5 * SS)).point(lambda a: a * 0.45)
    gl = Image.new("RGBA", (js, js), (0, 0, 0, 0))
    gl.paste(BLUE + (255,), (0, 0), glow)
    big.alpha_composite(gl, (jx, jy))
    jelly = Image.new("RGBA", (js, js), (0, 0, 0, 0))
    jelly.paste(diagonal((js, js), LILAC, BLUE), (0, 0), body)
    jelly.paste((255, 255, 255, 255), (0, 0), shine.point(lambda a: a * 0.85))
    big.alpha_composite(jelly, (jx, jy))

    big.resize((S, S), Image.LANCZOS).save(out_path, optimize=True)

if __name__ == "__main__":
    main()
