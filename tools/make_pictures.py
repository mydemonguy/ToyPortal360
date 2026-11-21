#!/usr/bin/env python3
"""Turns your own pictures into .tpi files for Toy Portal 360's character grid.

Toy Portal 360 ships no pictures. This converts pictures you provide (for
example photos of your own figures) into the small raw format the console
can draw without decoding PNG or JPEG.

Name each picture after the figure as the grid shows it, for example
"Batman.jpg" or "Spyro.png". Characters other than letters, digits, space,
"-" and "_" become "_", as in figure file names, so "Mr. Incredible.png"
becomes "Mr_ Incredible.tpi". You can also name a picture by the figure's
number (Dimensions/Infinity: "2.png"; Skylanders: "<id>.png" or
"<id>-<variant>.png").

Usage:
    python3 make_pictures.py PICTURE_FOLDER GAME [--out FOLDER] [--fill]

GAME is Skylanders, Dimensions or Infinity. --fill crops the picture to
fill the whole space instead of fitting all of it in.

Needs Pillow (pip install pillow).
"""
import argparse
import os
import struct
import sys

WIDTH, HEIGHT = 128, 38  # the picture area of one grid square, at 1x
MAGIC = b"TPI1"
GAMES = ("Skylanders", "Dimensions", "Infinity")
EXTENSIONS = (".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp", ".tif", ".tiff")


def safe_file_name(name):
    """The same rule as safe_file_name() in src/core/figure_db.cpp."""
    out = "".join(c if (c.isascii() and (c.isalnum() or c in " -_")) else "_" for c in name)
    return out[:37]


def to_tpi(image, fill=False):
    """Returns the .tpi bytes for a Pillow image: fitted (or cropped with
    fill) and centred in WIDTH x HEIGHT, see-through round it."""
    from PIL import Image

    image = image.convert("RGBA")
    w, h = image.size
    scale = (max if fill else min)(WIDTH / w, HEIGHT / h)
    nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
    image = image.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
    canvas.paste(image, ((WIDTH - nw) // 2, (HEIGHT - nh) // 2), image)
    out = bytearray(MAGIC + struct.pack(">HH", WIDTH, HEIGHT))
    for r, g, b, a in canvas.getdata():
        out += bytes((a, r, g, b))
    return bytes(out)


def read_tpi(data):
    """(width, height, [(a, r, g, b), ...]) from .tpi bytes."""
    if data[:4] != MAGIC:
        raise ValueError("not a .tpi file")
    w, h = struct.unpack(">HH", data[4:8])
    if len(data) != 8 + w * h * 4:
        raise ValueError("wrong size")
    return w, h, [tuple(data[8 + i * 4: 12 + i * 4]) for i in range(w * h)]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("folder", help="folder with your pictures")
    parser.add_argument("game", help="Skylanders, Dimensions or Infinity")
    parser.add_argument("--out", help="where to write the .tpi files (default: <folder>/Pictures/<game>)")
    parser.add_argument("--fill", action="store_true", help="crop to fill the space instead of fitting the whole picture")
    args = parser.parse_args(argv)

    game = next((g for g in GAMES if g.lower() == args.game.lower()), None)
    if not game:
        parser.error("GAME must be one of: " + ", ".join(GAMES))
    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow is needed: pip install pillow")

    out_dir = args.out or os.path.join(args.folder, "Pictures", game)
    os.makedirs(out_dir, exist_ok=True)
    made = 0
    for name in sorted(os.listdir(args.folder)):
        stem, ext = os.path.splitext(name)
        if ext.lower() not in EXTENSIONS:
            continue
        try:
            with Image.open(os.path.join(args.folder, name)) as image:
                data = to_tpi(image, args.fill)
        except Exception as e:  # a file Pillow can't read: say so and go on
            print("skipped %s: %s" % (name, e))
            continue
        target = safe_file_name(stem) + ".tpi"
        with open(os.path.join(out_dir, target), "wb") as f:
            f.write(data)
        print("%s -> %s" % (name, target))
        made += 1

    print()
    if made:
        print("Made %d picture(s) in %s" % (made, out_dir))
        print("Copy them to the 360 at Hdd1:\\ToyPortal\\Pictures\\%s\\" % game)
        print("(the same ToyPortal folder that holds your figure files). Close and")
        print("reopen the grid to see them.")
    else:
        print("No pictures found in %s (%s)" % (args.folder, ", ".join(EXTENSIONS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
