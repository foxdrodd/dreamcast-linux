#!/usr/bin/env python3
"""
frames2lcd.py - convert an image or animation into a raw VMU LCD frame stream.

Each output frame is 192 bytes: 48x32 pixels, 1 bit per pixel, 6 bytes per row,
MSB = leftmost pixel, row 0 at the top - the exact framebuffer the vmu-lcd
character device expects. Frames are written back to back so the stream can be
fed to vmu_lcd_anim:

    frames2lcd.py clip.gif > clip.raw
    vmu_lcd_anim -l clip.raw

    frames2lcd.py clip.gif | vmu_lcd_anim -l -

Any format Pillow can open works (animated GIF, PNG, a still image, ...).
Frames are scaled to fit 48x32, centered, and thresholded to mono. Use
--invert if your source has a light subject on a dark background.

Requires Pillow:  pip install pillow
"""

import argparse
import sys

try:
    from PIL import Image, ImageSequence, ImageOps
except ImportError:
    sys.exit("frames2lcd.py: needs Pillow (pip install pillow)")

LCD_W, LCD_H, LCD_STRIDE = 48, 32, 6


def pack(img, threshold, invert):
    """Pack a 48x32 mono PIL image into 192 bytes (MSB = leftmost pixel)."""
    px = img.load()
    out = bytearray(LCD_STRIDE * LCD_H)
    for y in range(LCD_H):
        for x in range(LCD_W):
            on = px[x, y] >= threshold
            if invert:
                on = not on
            if on:
                out[y * LCD_STRIDE + x // 8] |= 0x80 >> (x % 8)
    return bytes(out)


def fit(frame):
    """Scale a frame to fit 48x32 (preserving aspect), centered on black."""
    g = frame.convert("L")
    g.thumbnail((LCD_W, LCD_H), Image.LANCZOS)
    canvas = Image.new("L", (LCD_W, LCD_H), 0)
    canvas.paste(g, ((LCD_W - g.width) // 2, (LCD_H - g.height) // 2))
    return canvas


def main():
    ap = argparse.ArgumentParser(description="Convert an image/GIF to VMU LCD frames.")
    ap.add_argument("input", help="source image or animation (GIF, PNG, ...)")
    ap.add_argument("-o", "--output", help="output file (default: stdout)")
    ap.add_argument("-t", "--threshold", type=int, default=128,
                    help="mono threshold 0-255 (default 128)")
    ap.add_argument("--invert", action="store_true",
                    help="invert black/white")
    ap.add_argument("--dither", action="store_true",
                    help="Floyd-Steinberg dither instead of hard threshold")
    args = ap.parse_args()

    src = Image.open(args.input)
    out = open(args.output, "wb") if args.output else sys.stdout.buffer

    n = 0
    for frame in ImageSequence.Iterator(src):
        img = fit(frame)
        if args.dither:
            img = img.convert("1")          # 1-bit, dithered
            img = img.convert("L")
        out.write(pack(img, args.threshold, args.invert))
        n += 1

    if args.output:
        out.close()
    print(f"frames2lcd.py: wrote {n} frame(s)", file=sys.stderr)


if __name__ == "__main__":
    main()
