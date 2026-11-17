#!/usr/bin/env python3
"""Draws the iPad home-screen icon: web/apple-touch-icon.png (180x180).

Usage: make_icon.py [OUT.png]

A little house with a sun and a cloud peeking out from behind it, on a sky
gradient. Drawn at 4x and scaled down for smooth edges. iOS adds the rounded
corners itself, so the square is full.
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

    # Sun with a soft glow, upper right, behind everything else.
    cx, cy, r = int(W * 0.68), int(W * 0.30), int(W * 0.17)
    glow = Image.new("L", (W, W), 0)
    ImageDraw.Draw(glow).ellipse([cx - r * 1.5, cy - r * 1.5, cx + r * 1.5, cy + r * 1.5], fill=110)
    glow = glow.filter(ImageFilter.GaussianBlur(W * 0.05))
    img.paste((255, 214, 90), (0, 0), glow)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 200, 40))

    # Cloud, upper left, partly in front of the sun.
    def cloud(dx, dy, color):
        parts = [
            (0.20, 0.42, 0.11),
            (0.33, 0.34, 0.15),
            (0.48, 0.40, 0.12),
            (0.58, 0.45, 0.09),
        ]
        for px, py, pr in parts:
            x, y, rr = (px * W + dx, py * W + dy, pr * W)
            d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=color)
        d.rounded_rectangle(
            [0.10 * W + dx, 0.42 * W + dy, 0.66 * W + dx, 0.54 * W + dy], radius=int(0.06 * W), fill=color
        )

    cloud(0, int(W * 0.012), (70, 120, 175))  # shadow
    cloud(0, 0, (250, 252, 255))

    # Grass along the bottom.
    d.rectangle([0, 0.88 * W, W, W], fill=(92, 160, 72))

    # The house, in front: roof, walls, door, window, chimney.
    def px(x, y):
        return (x * W, y * W)

    wall = (250, 238, 214)
    roof = (196, 66, 52)
    trim = (120, 46, 36)
    d.rectangle([*px(0.62, 0.40), *px(0.70, 0.58)], fill=trim)              # chimney
    d.rectangle([*px(0.26, 0.60), *px(0.78, 0.90)], fill=wall)              # walls
    d.polygon([px(0.18, 0.62), px(0.52, 0.36), px(0.86, 0.62)], fill=roof)  # roof
    d.line([px(0.18, 0.62), px(0.52, 0.36), px(0.86, 0.62)], fill=trim, width=int(W * 0.018))
    d.rounded_rectangle([*px(0.33, 0.70), *px(0.46, 0.90)], radius=int(W * 0.02), fill=(120, 72, 44))  # door
    d.ellipse([*px(0.425, 0.795), *px(0.445, 0.815)], fill=(240, 200, 90))  # door knob
    d.rectangle([*px(0.56, 0.68), *px(0.71, 0.80)], fill=(255, 220, 120))   # lit window
    d.line([px(0.635, 0.68), px(0.635, 0.80)], fill=trim, width=int(W * 0.012))
    d.line([px(0.56, 0.74), px(0.71, 0.74)], fill=trim, width=int(W * 0.012))
    d.rectangle([*px(0.56, 0.68), *px(0.71, 0.80)], outline=trim, width=int(W * 0.012))

    img = img.resize((SIZE, SIZE), Image.LANCZOS)
    img.save(out, optimize=True)
    print(f"make_icon: wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
