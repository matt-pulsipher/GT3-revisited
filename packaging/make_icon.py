#!/usr/bin/env python3
"""Draws the GT3 Revisited application icon (original artwork, no game assets).

Red, a sweeping right-hander seen from above with a yellow center line and a kerb, and a 3 in
Barlow Condensed SemiBold (the UI font in third_party/fonts). Writes packaging/icon.png
(1024x1024), packaging/icon.ico (Windows) and the Android launcher icons. Needs Pillow:
python3 packaging/make_icon.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONT = os.path.join(ROOT, "third_party", "fonts", "barlow", "BarlowCondensed-SemiBold.ttf")
S = 1024   # the icon's size; every coordinate below is in this space
SS = 2     # drawn this many times larger, then reduced (smooth edges)

WHITE = (240, 242, 244, 255)
ASPHALT = (54, 58, 64, 255)
DARK = (34, 36, 42, 255)
YELLOW = (255, 196, 40, 255)


def gradient(top, bottom):
    ramp = Image.linear_gradient("L").resize((S * SS, S * SS))
    return Image.composite(Image.new("RGBA", ramp.size, bottom + (255,)), Image.new("RGBA", ramp.size, top + (255,)), ramp)


def resample(pts, n):
    """n+1 points spaced evenly along the polyline pts."""
    dist = [0.0]
    for a, b in zip(pts, pts[1:]):
        dist.append(dist[-1] + math.dist(a, b))
    out, j = [], 0
    for i in range(n + 1):
        s = dist[-1] * i / n
        while j < len(dist) - 2 and dist[j + 1] < s:
            j += 1
        t = (s - dist[j]) / ((dist[j + 1] - dist[j]) or 1)
        out.append((pts[j][0] + (pts[j + 1][0] - pts[j][0]) * t, pts[j][1] + (pts[j + 1][1] - pts[j][1]) * t))
    return out


def band(d, center, inner, outer, first, last, fill):
    """The strip between two sideways offsets of center (positive: right of the direction of
    travel), over points first..last."""
    def side(off):
        out = []
        for i in range(first, last + 1):
            a, b = center[max(i - 1, 0)], center[min(i + 1, len(center) - 1)]
            dx, dy = b[0] - a[0], b[1] - a[1]
            ln = math.hypot(dx, dy) or 1
            out.append(((center[i][0] - dy / ln * off) * SS, (center[i][1] + dx / ln * off) * SS))
        return out
    d.polygon(side(inner) + side(outer)[::-1], fill=fill)


def draw(size=S):
    art = gradient((214, 34, 42), (112, 10, 20))
    d = ImageDraw.Draw(art)
    # A sweeping right-hander from the bottom left to the top right.
    pts = []
    for i in range(301):
        t = i / 300
        pts.append((120 + 820 * t + 140 * math.sin(t * math.pi) * 0.35,
                    930 - 820 * (t ** 1.35) - 180 * math.sin(t * math.pi)))
    c = resample(pts, 240)
    n = len(c) - 1
    half, edge = 125, 14
    band(d, c, -half, half, 0, n, ASPHALT)
    for k, i in enumerate(range(150, 238, 6)):  # white and dark kerb on the outside of the exit
        band(d, c, half - 1, half + 30, i, min(i + 6, 238), WHITE if k % 2 == 0 else DARK)
    for s in (-1, 1):
        band(d, c, s * (half - 2 * edge), s * (half - edge), 0, n, WHITE)
    for i in range(0, n, 10):  # yellow dashed center line
        band(d, c, -9, 9, i, min(i + 5, n), YELLOW)
    font = ImageFont.truetype(FONT, int(400 * SS * 1.36))  # a cap height of about 400
    x, y = 275, 330
    d.text(((x + 8) * SS, (y + 10) * SS), "3", font=font, fill=(90, 8, 16, 255), anchor="mm")  # shadow
    d.text((x * SS, y * SS), "3", font=font, fill=WHITE, anchor="mm")

    big = S * SS
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle((32 * SS, 32 * SS, big - 32 * SS, big - 32 * SS), radius=200 * SS, fill=255)
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    img.paste(art, (0, 0), mask)
    ImageDraw.Draw(img).rounded_rectangle((34 * SS, 34 * SS, big - 34 * SS, big - 34 * SS), radius=198 * SS,
                                          outline=WHITE, width=10 * SS)
    return img.resize((size, size), Image.LANCZOS)


def main():
    icon = draw()
    icon.save(os.path.join(HERE, "icon.png"))
    icon.save(os.path.join(HERE, "icon.ico"), sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    res = os.path.join(ROOT, "android", "app", "src", "main", "res")
    for name, px in {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}.items():
        out = os.path.join(res, "mipmap-" + name)
        os.makedirs(out, exist_ok=True)
        icon.resize((px, px), Image.LANCZOS).save(os.path.join(out, "ic_launcher.png"))
    print("icons written")


if __name__ == "__main__":
    main()
