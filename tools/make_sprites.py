"""Generates the sample bullet pngs in resources/.

Drop your own pngs next to them and use `sprite <name>` in patterns.txt -
the game loads any png in resources/ by bare name.

    python tools/make_sprites.py
"""

import math
import os

from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "resources")
SS = 4  # supersample factor, downsampled at the end for smooth edges


def save(name, size, draw_fn):
    w, h = size
    img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(img), w * SS, h * SS)
    img = img.resize((w, h), Image.LANCZOS)
    path = os.path.join(OUT, name + ".png")
    img.save(path)
    print("wrote %s (%dx%d)" % (path, w, h))


def orb(d, w, h):
    """Soft round shot: bright core fading to a rim."""
    r = w / 2 - SS
    for i in range(24, 0, -1):
        t = i / 24.0
        rad = r * t
        a = int(40 + 215 * (1.0 - t) ** 0.6)
        d.ellipse([w / 2 - rad, h / 2 - rad, w / 2 + rad, h / 2 + rad],
                  fill=(255, 255, 255, a))


def spear(d, w, h):
    """Narrow lance pointing right (so sprite_rot travel aims it down the shot)."""
    d.polygon([(w - SS, h / 2), (w * 0.55, SS), (0, h / 2), (w * 0.55, h - SS)],
              fill=(220, 235, 255, 255))
    d.polygon([(w - 2 * SS, h / 2), (w * 0.6, h * 0.3), (w * 0.6, h * 0.7)],
              fill=(255, 255, 255, 255))


def star(d, w, h):
    pts = []
    for i in range(10):
        ang = -math.pi / 2 + i * math.pi / 5
        rad = (w / 2 - SS) if i % 2 == 0 else (w / 5)
        pts.append((w / 2 + math.cos(ang) * rad, h / 2 + math.sin(ang) * rad))
    d.polygon(pts, fill=(255, 240, 200, 255))


def ring(d, w, h):
    """Hollow: the bullet colour shows through the middle."""
    wd = max(SS * 2, int(w * 0.16))
    d.ellipse([SS, SS, w - SS, h - SS], outline=(180, 230, 255, 255), width=wd)


def hexagon(d, w, h):
    pts = []
    for i in range(6):
        ang = i * math.pi / 3
        pts.append((w / 2 + math.cos(ang) * (w / 2 - SS), h / 2 + math.sin(ang) * (h / 2 - SS)))
    d.polygon(pts, fill=(200, 255, 210, 255))
    d.line(pts + [pts[0]], fill=(255, 255, 255, 255), width=SS, joint="curve")


if __name__ == "__main__":
    save("bullet_orb", (32, 32), orb)
    save("bullet_spear", (48, 12), spear)
    save("bullet_star", (32, 32), star)
    save("bullet_ring", (32, 32), ring)
    save("bullet_hex", (32, 32), hexagon)
