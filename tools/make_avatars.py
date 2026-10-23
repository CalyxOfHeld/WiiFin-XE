#!/usr/bin/env python3
"""Make profile pictures for WiiFin from images on Wikimedia Commons.

Each image is downloaded (a 512-pixel version, not the full original),
centre-cropped to a square, resized to 128 x 128 and saved as a PNG in
wii_avatars/.  With --circle the corners are made transparent (WiiFin draws
pictures in a disc anyway, so this only matters elsewhere).

    pip install pillow requests
    python tools/make_avatars.py [--circle] [--out wii_avatars] [--size 128]

Then copy the PNGs to sd:/apps/WiiFin/pictures/ and pick one with 2 on a
profile.  The images keep their own licences (see each Commons page): fine
for your own Wii; credit them if you share the files.
"""
import argparse, io, os, sys, urllib.parse
from PIL import Image, ImageDraw

try:
    import requests
except ImportError:          # the standard library will do
    requests = None
    import urllib.request

# The list from the notes.  Four of its files weren't on Commons
# (2026-10-23: "Crescent_Moon_above_Earth's_atmosphere", "Chureito_Pagoda_and_
# Mount_Fuji_Autumn", "Five_Flower_Lake,_Jiuzhaigou_Valley", "Leshan_Buddha_
# Statue_View"); each is replaced by a real photo of the same place.
AVATARS = {
    "Helios Bust":        "https://commons.wikimedia.org/wiki/File:Head_Helios_AM_Rhodes_E49_(bust_only).png",
    "Planet Earth":       "https://commons.wikimedia.org/wiki/File:The_Earth_seen_from_Apollo_17.jpg",
    "Full Moon":          "https://commons.wikimedia.org/wiki/File:FullMoon2010.jpg",
    "Crescent Moon":      "https://commons.wikimedia.org/wiki/File:ISS-59_Waxing_Crescent_Moon_just_above_Earth%27s_limb_(1).jpg",
    "Mount Fuji":         "https://commons.wikimedia.org/wiki/File:Chureito_Pagoda_and_Mount_Fuji_2023-03-07.jpg",
    "Jiuzhaigou":         "https://commons.wikimedia.org/wiki/File:Five_Flower_Lake,_Jiuzhaigou_Valley,_Sichuan_-_panoramio_(2380).jpg",
    "Leshan Giant Buddha": "https://commons.wikimedia.org/wiki/File:Leshan_Giant_Buddha_(1).jpg",
    "Mogao Caves":        "https://commons.wikimedia.org/wiki/File:Mogao_Caves.jpg",
}

# Wikimedia asks scripts to say who they are
USER_AGENT = "WiiFin-avatar-maker/1.0 (personal use; https://github.com/)"


def file_url(page_url, width=512):
    """A "File:" page's address -> its image, scaled to width
    (Special:FilePath redirects to the file or a thumbnail of it)."""
    name = urllib.parse.unquote(page_url.rsplit("/File:", 1)[-1])
    return ("https://commons.wikimedia.org/wiki/Special:FilePath/"
            + urllib.parse.quote(name) + "?width=%d" % width)


def download(url):
    if requests:
        r = requests.get(url, headers={"User-Agent": USER_AGENT}, timeout=30)
        r.raise_for_status()
        return r.content
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def square(img):
    """the middle square of img"""
    w, h = img.size
    s = min(w, h)
    left, top = (w - s) // 2, (h - s) // 2
    return img.crop((left, top, left + s, top + s))


def circle(img):
    """img with everything outside its inscribed circle transparent
    (drawn 4x larger and scaled down, for a smooth edge)"""
    s = img.size[0]
    mask = Image.new("L", (s * 4, s * 4), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, s * 4 - 1, s * 4 - 1), fill=255)
    mask = mask.resize((s, s), Image.LANCZOS)
    out = img.copy()
    out.putalpha(Image.composite(img.getchannel("A"), Image.new("L", (s, s), 0), mask))
    return out


def safe_name(name):
    return "".join(c if c.isalnum() or c in " -_" else "_" for c in name).strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="wii_avatars", help="the folder for the PNGs (default wii_avatars)")
    ap.add_argument("--size", type=int, default=128, help="width and height in pixels (default 128)")
    ap.add_argument("--circle", action="store_true", help="make the corners transparent")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    done, failed = 0, []
    for name, page in AVATARS.items():
        try:
            data = download(file_url(page, max(512, args.size * 2)))
            img = Image.open(io.BytesIO(data)).convert("RGBA")
            img = square(img).resize((args.size, args.size), Image.LANCZOS)
            if args.circle:
                img = circle(img)
            path = os.path.join(args.out, safe_name(name) + ".png")
            img.save(path, optimize=True)
            print("ok      %s -> %s" % (name, path))
            done += 1
        except Exception as e:   # one bad address or image: the rest still made
            print("failed  %s: %s" % (name, e), file=sys.stderr)
            failed.append(name)
    print("%d made, %d failed%s" % (done, len(failed), (": " + ", ".join(failed)) if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
