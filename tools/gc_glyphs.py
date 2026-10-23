#!/usr/bin/env python3
"""Build data/images/gc_buttons.png, the GameCube button glyphs of the hints.

The shapes and letters come from Dolphin's Android touch overlay
(Source/Android/app/src/main/res/drawable-xxxhdpi/gcpad_*.png, GPL-2.0-or-later,
https://github.com/dolphin-emu/dolphin).  There they are translucent white with
black letters; here the fill is made opaque and white (WiiFin tints it: green
A, red B...), the letters stay black, and a thin darker edge goes around.

    python tools/gc_glyphs.py <dolphin source dir>

Prints the table of parts for source/ui/Ui.cpp (GC_PARTS).
"""
import sys
from PIL import Image, ImageFilter

# name, height on screen (px; the sheet is drawn at half size)
BUTTONS = [("a", 18), ("b", 15), ("x", 22), ("y", 16), ("z", 10), ("l", 13), ("r", 13)]
SCALE = 2
EDGE = 0x8C            # the edge's grey, before the tint

def glyph(path, height):
    im = Image.open(path).convert("LA")
    im = im.crop(im.getchannel("A").getbbox())
    w = max(1, round(im.width * height * SCALE / im.height))
    im = im.resize((w, height * SCALE), Image.LANCZOS)
    lum, alpha = im.getchannel("L"), im.getchannel("A")
    # the fill was 80 % opaque: opaque now, its antialiased rim kept
    alpha = alpha.point(lambda a: min(255, round(a * 255 / 204)))
    pad = 3
    out = Image.new("RGBA", (w + 2 * pad, height * SCALE + 2 * pad), (0, 0, 0, 0))
    edge = Image.new("L", out.size, 0)
    edge.paste(alpha, (pad, pad))
    edge = edge.filter(ImageFilter.MaxFilter(5))        # 2 px around, at 2x
    out.paste(Image.new("RGBA", out.size, (EDGE, EDGE, EDGE, 255)), (0, 0), edge)
    shape = Image.merge("RGBA", (lum, lum, lum, alpha))
    out.alpha_composite(shape, (pad, pad))
    return out

def main():
    src = sys.argv[1] + "/Source/Android/app/src/main/res/drawable-xxxhdpi/"
    parts = [(n, glyph(src + "gcpad_%s.png" % n, h)) for n, h in BUTTONS]
    x, cells = 0, []
    for n, im in parts:
        cells.append((n, x, im))
        x += (im.width + 3) // 4 * 4 + 4
    width = (x + 3) // 4 * 4
    height = (max(im.height for _, im in parts) + 3) // 4 * 4
    sheet = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    for n, cx, im in cells:
        sheet.paste(im, (cx, 0))
    sheet.save("data/images/gc_buttons.png", optimize=True)
    print("/* name, x, y, w, h in gc_buttons.png (2x) */")
    for n, cx, im in cells:
        print('    { \'%s\', %d, %d, %d, %d },' % (n.upper(), cx, 0, im.width, im.height))

if __name__ == "__main__":
    main()
