#!/usr/bin/env python3
"""Build data/cursors/mouse_arrow.png, the USB mouse's pointer: a plain
desktop arrow, white with a black edge and a soft shadow, drawn at its size
on screen (the tip is MOUSE_TIP pixels in from the top left, see Ui.cpp).

    python tools/mouse_cursor.py
"""
from PIL import Image, ImageDraw, ImageFilter

SS = 8                # supersampling
UNIT = 1.3            # screen pixels per unit of the outline below
PAD = 2               # tip offset from the image's top left
# the arrow, tip at 0, 0 (units)
ARROW = [(0, 0), (0, 16), (3.8, 12.4), (6.4, 18.4), (8.9, 17.3), (6.4, 11.4), (11.4, 11.4)]
EDGE = 1.2            # black edge, screen pixels

def main():
    w, h = 20, 28
    big = (w * SS, h * SS)
    pts = [((PAD + x * UNIT) * SS, (PAD + y * UNIT) * SS) for x, y in ARROW]
    shape = Image.new("L", big, 0)
    ImageDraw.Draw(shape).polygon(pts, fill=255)
    edge = shape.filter(ImageFilter.MaxFilter(int(EDGE * SS) * 2 + 1))
    shadow = edge.filter(ImageFilter.GaussianBlur(1.2 * SS)).point(lambda a: a * 0.45)
    out = Image.new("RGBA", big, (0, 0, 0, 0))
    out.paste((0, 0, 0, 255), (int(1 * SS), int(1.5 * SS)), shadow.crop((0, 0, big[0] - SS, big[1] - int(1.5 * SS))))
    out.paste((0, 0, 0, 255), (0, 0), edge)
    out.paste((255, 255, 255, 255), (0, 0), shape)
    out = out.resize((w, h), Image.LANCZOS)
    out.save("data/cursors/mouse_arrow.png", optimize=True)

if __name__ == "__main__":
    main()
