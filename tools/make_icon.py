#!/usr/bin/env python3
"""Draws the iPad home-screen icon: web/apple-touch-icon.png (180x180).

Usage: make_icon.py [OUT.png]

A sun behind a cloud on a sky gradient. Drawn at 4x and scaled down for
smooth edges. iOS adds the rounded corners itself, so the square is full.
Needs Pillow; the output PNG is committed, so the firmware build doesn't.
"""
import sys

from PIL import Image, ImageDraw, ImageFilter

SIZE = 180
S = 4  # supersampling
W = SIZE * S


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def main() -> int:
    out = sys.argv[1] if len(sys.argv) > 1 else "web/apple-touch-icon.png"
    img = Image.new("RGB", (W, W))
    d = ImageDraw.Draw(img)

    # Sky: deep blue at the top to light blue at the bottom.
    top, bottom = (24, 92, 170), (120, 186, 236)
    for y in range(W):
        d.line([(0, y), (W, y)], fill=lerp(top, bottom, y / (W - 1)))

    # Sun with a soft glow, upper right.
    cx, cy, r = int(W * 0.62), int(W * 0.38), int(W * 0.20)
    glow = Image.new("L", (W, W), 0)
    ImageDraw.Draw(glow).ellipse([cx - r * 1.5, cy - r * 1.5, cx + r * 1.5, cy + r * 1.5], fill=110)
    glow = glow.filter(ImageFilter.GaussianBlur(W * 0.05))
    img.paste((255, 214, 90), (0, 0), glow)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 200, 40))

    # Cloud: overlapping circles on a flat base, with a faint shadow.
    def cloud(dx, dy, color):
        parts = [
            (0.30, 0.62, 0.14),
            (0.45, 0.53, 0.19),
            (0.62, 0.60, 0.15),
            (0.74, 0.66, 0.11),
        ]
        for px, py, pr in parts:
            x, y, rr = (px * W + dx, py * W + dy, pr * W)
            d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=color)
        d.rounded_rectangle(
            [0.18 * W + dx, 0.62 * W + dy, 0.85 * W + dx, 0.77 * W + dy], radius=int(0.075 * W), fill=color
        )

    cloud(0, int(W * 0.015), (70, 120, 175))  # shadow
    cloud(0, 0, (250, 252, 255))

    img = img.resize((SIZE, SIZE), Image.LANCZOS)
    img.save(out, optimize=True)
    print(f"make_icon: wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
