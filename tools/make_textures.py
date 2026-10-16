#!/usr/bin/env python3
"""Generates the textures and shaded sprites used by Kart, Dungeon, Fietsen,
Crash and Plinko. Everything is drawn here (no external art):
tileable noise textures and supersampled vector sprites with gradients,
highlights and outlines. Run from the repo root:  python3 tools/make_textures.py
"""
import math
import os
import random

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

OUT_TEX = "assets/images/tex"
OUT_SPR = "assets/images/spr"
os.makedirs(OUT_TEX, exist_ok=True)
os.makedirs(OUT_SPR, exist_ok=True)
SS = 4  # supersampling for sprites
OUTLINE = (10, 6, 18, 255)

# ====================================================================== noise


def vnoise(w, h, cells, rng):
    """Tileable value noise in [0,1]."""
    grid = rng.random((cells, cells))
    ys = np.arange(h) * cells / h
    xs = np.arange(w) * cells / w
    x0 = np.floor(xs).astype(int)
    y0 = np.floor(ys).astype(int)
    fx = xs - x0
    fy = ys - y0
    fx = fx * fx * (3 - 2 * fx)
    fy = fy * fy * (3 - 2 * fy)
    x1 = (x0 + 1) % cells
    y1 = (y0 + 1) % cells
    x0 %= cells
    y0 %= cells
    a = grid[np.ix_(y0, x0)]
    b = grid[np.ix_(y0, x1)]
    c = grid[np.ix_(y1, x0)]
    d = grid[np.ix_(y1, x1)]
    top = a + (b - a) * fx[None, :]
    bot = c + (d - c) * fx[None, :]
    return top + (bot - top) * fy[:, None]


def fbm(w, h, base=4, octaves=5, seed=0, gain=0.5):
    rng = np.random.default_rng(seed)
    total = np.zeros((h, w))
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        total += vnoise(w, h, base * 2**o, rng) * amp
        norm += amp
        amp *= gain
    return total / norm


def colorize(v, c0, c1):
    """Map a [0,1] field to a colour ramp."""
    v = np.clip(v, 0, 1)[..., None]
    c0 = np.array(c0, float)
    c1 = np.array(c1, float)
    return c0 + (c1 - c0) * v


def save_rgb(arr, name):
    Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB").save(f"{OUT_TEX}/{name}.png")
    print("tex", name)


def hexc(h, a=255):
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


def mixc(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def lighten(c, t):
    return mixc(c, (255, 255, 255, c[3] if len(c) > 3 else 255), t)


def darken(c, t):
    return mixc(c, (0, 0, 0, c[3] if len(c) > 3 else 255), t)


# ====================================================================== textures


def tex_grass():
    W = 512
    n = fbm(W, W, 4, 6, seed=1)
    n2 = fbm(W, W, 16, 3, seed=2)
    img = colorize(n * 0.7 + n2 * 0.3, (62, 122, 44), (110, 170, 70))
    im = Image.fromarray(img.astype(np.uint8), "RGB")
    d = ImageDraw.Draw(im)
    rnd = random.Random(3)
    for _ in range(9000):
        x, y = rnd.uniform(0, W), rnd.uniform(0, W)
        ln = rnd.uniform(3, 8)
        ang = -math.pi / 2 + rnd.uniform(-0.5, 0.5)
        c = rnd.choice([(80, 140, 50), (130, 190, 80), (60, 110, 40), (150, 205, 95)])
        for ox in (-W, 0, W):
            for oy in (-W, 0, W):
                d.line([(x + ox, y + oy), (x + ox + math.cos(ang) * ln, y + oy + math.sin(ang) * ln)], fill=c, width=1)
    for _ in range(60):
        x, y = rnd.uniform(0, W), rnd.uniform(0, W)
        c = rnd.choice([(255, 255, 255), (255, 230, 90), (255, 160, 200)])
        d.ellipse([x - 1.6, y - 1.6, x + 1.6, y + 1.6], fill=c)
    im.save(f"{OUT_TEX}/grass.png")
    print("tex grass")


def tex_asphalt(name, c0, c1, seed):
    W = 512
    n = fbm(W, W, 8, 6, seed=seed)
    img = colorize(n, c0, c1)
    rng = np.random.default_rng(seed + 10)
    speck = rng.random((W, W))
    img[speck > 0.985] += 38
    img[speck < 0.015] -= 30
    stain = fbm(W, W, 3, 3, seed=seed + 20)
    img *= (0.9 + 0.12 * stain)[..., None]
    save_rgb(img, name)


def bevel_tile(img, x0, y0, w, h, base, rnd, grout=3, bev=3, crack=0.0):
    """Draws a bevelled slab into a float RGB array."""
    H, W, _ = img.shape
    c = np.array(base, float) * rnd.uniform(0.86, 1.12)
    for y in range(y0 + grout, y0 + h):
        for x in range(x0 + grout, x0 + w):
            yy, xx = y % H, x % W
            col = c.copy()
            dx, dy = x - (x0 + grout), y - (y0 + grout)
            ex, ey = (x0 + w) - 1 - x, (y0 + h) - 1 - y
            if dx < bev or dy < bev:
                col = col * 1.25 + 10
            elif ex < bev or ey < bev:
                col = col * 0.62
            img[yy, xx] = img[yy, xx] * 0.35 + col * 0.65


def tex_stone():
    W = 256
    n = fbm(W, W, 8, 5, seed=5)
    img = colorize(n, (60, 52, 78), (96, 86, 120))
    grout = np.array([28, 22, 36], float)
    img[:] = img * 0.6 + grout * 0.4
    rnd = random.Random(6)
    for gy in range(2):
        for gx in range(2):
            bevel_tile(img, gx * 128, gy * 128, 128, 128, (104, 94, 128), rnd, grout=4, bev=4)
    grain = fbm(W, W, 16, 4, seed=7)
    img *= (0.82 + 0.3 * grain)[..., None]
    im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")
    d = ImageDraw.Draw(im)
    for _ in range(3):  # a few hairline cracks
        x, y = rnd.uniform(20, W - 20), rnd.uniform(20, W - 20)
        ang = rnd.uniform(0, math.tau)
        pts = [(x, y)]
        for _k in range(6):
            ang += rnd.uniform(-0.6, 0.6)
            x += math.cos(ang) * 7
            y += math.sin(ang) * 7
            pts.append((x, y))
        d.line(pts, fill=(62, 54, 80), width=1)
    for _ in range(140):  # moss near the grout
        gx = rnd.choice([0, 128, 256]) + rnd.uniform(-6, 6)
        gy = rnd.uniform(0, W)
        if rnd.random() < 0.5:
            gx, gy = gy, gx
        d.ellipse([gx - 1.5, gy - 1.5, gx + 1.5, gy + 1.5], fill=(70, 110, 60))
    im.save(f"{OUT_TEX}/stone.png")
    print("tex stone")


def tex_bricks(name, base, mortar, seed, bw=64, bh=28, size=256):
    n = fbm(size, size, 8, 4, seed=seed)
    img = colorize(n, np.array(mortar) * 0.8, np.array(mortar) * 1.1)
    rnd = random.Random(seed)
    for r in range(size // bh + 1):
        off = (r % 2) * bw // 2
        for k in range(-1, size // bw + 1):
            bevel_tile(img, k * bw + off, r * bh, bw, bh, base, rnd, grout=3, bev=2)
    grain = fbm(size, size, 32, 3, seed=seed + 1)
    img *= (0.85 + 0.25 * grain)[..., None]
    save_rgb(img, name)


def tex_water():
    W = 256
    n = fbm(W, W, 4, 5, seed=9)
    m = fbm(W, W, 6, 4, seed=10)
    caustic = np.abs(np.sin((n * 3 + m * 2) * math.pi * 3))
    caustic = np.clip(1 - caustic * 4, 0, 1) ** 2
    img = colorize(n, (28, 78, 132), (48, 112, 168))
    img += caustic[..., None] * np.array([90, 130, 150])
    save_rgb(img, "water")


def tex_wood():
    W = 256
    img = np.zeros((W, W, 3))
    n = fbm(W, W, 4, 5, seed=12)
    for p in range(4):
        y0 = p * 64
        tone = random.Random(p).uniform(0.85, 1.1)
        ys = np.arange(64)
        grain = np.sin((np.arange(W)[None, :] * 0.02 + n[y0:y0 + 64] * 9 + ys[:, None] * 0.35) * 4) * 0.5 + 0.5
        base = colorize(grain * 0.6 + n[y0:y0 + 64] * 0.4, (110, 70, 40), (160, 108, 64)) * tone
        base[:3] *= 1.2
        base[-3:] *= 0.55
        img[y0:y0 + 64] = base
    save_rgb(img, "wood")


def tex_cobble():
    W = 256
    n = fbm(W, W, 8, 4, seed=14)
    img = colorize(n, (40, 38, 46), (60, 58, 66))
    im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")
    d = ImageDraw.Draw(im)
    rnd = random.Random(15)
    for gy in range(8):
        for gx in range(8):
            cx = gx * 32 + 16 + rnd.uniform(-3, 3) + (gy % 2) * 16
            cy = gy * 32 + 16 + rnd.uniform(-3, 3)
            r = rnd.uniform(12, 14)
            base = rnd.uniform(110, 150)
            for ox in (-W, 0, W):
                for oy in (-W, 0, W):
                    x, y = cx + ox, cy + oy
                    d.ellipse([x - r, y - r, x + r, y + r], fill=(int(base * 0.6), int(base * 0.58), int(base * 0.62)))
                    d.ellipse([x - r + 1, y - r + 1, x + r - 3, y + r - 3], fill=(int(base), int(base * 0.96), int(base * 1.02)))
                    d.ellipse([x - r * 0.5, y - r * 0.6, x, y - r * 0.2], fill=(int(min(255, base * 1.25)),) * 3)
    im.save(f"{OUT_TEX}/cobble.png")
    print("tex cobble")


def tex_tulips():
    W = 256
    soil = fbm(W, W, 8, 4, seed=16)
    img = colorize(soil, (70, 46, 28), (100, 66, 38))
    im = Image.fromarray(img.astype(np.uint8), "RGB")
    d = ImageDraw.Draw(im)
    rnd = random.Random(17)
    colors = [(230, 50, 70), (255, 200, 50), (255, 120, 180), (150, 90, 230), (255, 140, 40), (250, 250, 245), (230, 50, 70), (255, 200, 50)]
    for row in range(8):
        y = row * 32 + 16
        c = colors[row]
        for x in range(0, W, 9):
            xx = x + rnd.uniform(-2, 2)
            yy = y + rnd.uniform(-5, 5)
            d.ellipse([xx - 4, yy - 2, xx + 4, yy + 8], fill=(50, 120, 50))  # leaves
            d.ellipse([xx - 3.5, yy - 3.5, xx + 3.5, yy + 3.5], fill=darken(c + (255,), 0.25)[:3])
            d.ellipse([xx - 3, yy - 4, xx + 2.5, yy + 1.5], fill=c)
            d.ellipse([xx - 2, yy - 3, xx, yy - 1], fill=lighten(c + (255,), 0.5)[:3])
    im.save(f"{OUT_TEX}/tulips.png")
    print("tex tulips")


# ====================================================================== sprite helpers


class Sprite:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))

    def S(self, pts):
        return [(x * SS, y * SS) for x, y in pts]

    def box(self, b):
        return [b[0] * SS, b[1] * SS, b[2] * SS, b[3] * SS]

    def mask(self, kind, geom, **kw):
        m = Image.new("L", self.img.size, 0)
        d = ImageDraw.Draw(m)
        if kind == "ellipse":
            d.ellipse(self.box(geom), fill=255)
        elif kind == "rrect":
            d.rounded_rectangle(self.box(geom), radius=kw.get("r", 8) * SS, fill=255)
        elif kind == "poly":
            d.polygon(self.S(geom), fill=255)
        elif kind == "line":
            d.line(self.S(geom), fill=255, width=int(kw.get("width", 4) * SS), joint="curve")
        return m

    def fill(self, m, c0, c1=None, direction="v", box=None, radial=None):
        """Fill mask with a flat colour, a linear gradient (v/h/diag) or a radial one."""
        W, H = self.img.size
        if c1 is None:
            layer = Image.new("RGBA", (W, H), c0)
        else:
            bb = box or m.getbbox() or (0, 0, W, H)
            if isinstance(bb[0], float) or box is not None:
                bb = [v * SS for v in box] if box is not None else bb
            yy, xx = np.mgrid[0:H, 0:W].astype(float)
            if radial is not None:
                cx, cy, r = radial[0] * SS, radial[1] * SS, radial[2] * SS
                t = np.clip(np.hypot(xx - cx, yy - cy) / r, 0, 1)
            elif direction == "v":
                t = np.clip((yy - bb[1]) / max(1, bb[3] - bb[1]), 0, 1)
            elif direction == "h":
                t = np.clip((xx - bb[0]) / max(1, bb[2] - bb[0]), 0, 1)
            else:
                t = np.clip(((xx - bb[0]) / max(1, bb[2] - bb[0]) + (yy - bb[1]) / max(1, bb[3] - bb[1])) / 2, 0, 1)
            a = np.array(c0, float)
            b = np.array(c1, float)
            arr = a + (b - a) * t[..., None]
            layer = Image.fromarray(arr.astype(np.uint8), "RGBA")
        layer.putalpha(ImageChops.multiply(layer.getchannel("A"), m))
        self.img.alpha_composite(layer)

    def shape(self, kind, geom, c0, c1=None, direction="v", outline=0.0, radial=None, **kw):
        m = self.mask(kind, geom, **kw)
        if outline > 0:
            o = m.filter(ImageFilter.MaxFilter(int(outline * SS * 2) | 1))
            self.fill(o, OUTLINE)
        box = None
        if kind in ("ellipse", "rrect"):
            box = geom
        elif kind == "poly":
            xs = [p[0] for p in geom]
            ys = [p[1] for p in geom]
            box = (min(xs), min(ys), max(xs), max(ys))
        self.fill(m, c0, c1, direction, box=box, radial=radial)
        return m

    def highlight(self, kind, geom, alpha=90, **kw):
        m = self.mask(kind, geom, **kw).filter(ImageFilter.GaussianBlur(2 * SS))
        self.fill(m, (255, 255, 255, alpha))

    def shadow_inside(self, m, offset=(3, 4), alpha=90, blur=3):
        """Darken the inside of a shape along its lower-right edge (form shadow)."""
        inv = ImageChops.invert(m).filter(ImageFilter.GaussianBlur(blur * SS))
        sh = ImageChops.offset(inv, -offset[0] * SS, -offset[1] * SS)
        sh = ImageChops.multiply(sh, m)
        self.fill(sh.point(lambda v: v * alpha // 255), (0, 0, 0, 255))

    def save(self, name, outline=0):
        im = self.img.resize((self.w, self.h), Image.LANCZOS)
        if outline:
            a = im.getchannel("A")
            o = a.filter(ImageFilter.MaxFilter(outline * 2 + 1))
            base = Image.new("RGBA", im.size, OUTLINE)
            base.putalpha(o)
            base.alpha_composite(im)
            im = base
        im.save(f"{OUT_SPR}/{name}.png")
        print("spr", name)


# ====================================================================== sprites


def spr_kart(name, body):
    """Top-down kart facing right."""
    s = Sprite(140, 100)
    tire0, tire1 = hexc("2a2632"), hexc("15121c")
    for (x, y) in [(18, 6), (18, 72), (92, 10), (92, 70)]:
        m = s.shape("rrect", (x, y, x + 30, y + 22), tire0, tire1, outline=1.5, r=7)
        for k in range(5):
            s.shape("rrect", (x + 3 + k * 5.5, y + 2, x + 5 + k * 5.5, y + 20), (60, 56, 70, 255), r=1)
    b = body
    # side pods and floor pan
    s.shape("rrect", (24, 26, 120, 74), darken(b, 0.5), darken(b, 0.7), outline=2, r=16)
    # main body: tapered nose
    body_poly = [(16, 34), (60, 22), (104, 30), (134, 44), (134, 56), (104, 70), (60, 78), (16, 66)]
    m = s.shape("poly", body_poly, lighten(b, 0.25), darken(b, 0.25), outline=2.2)
    s.shadow_inside(m, (0, 6), alpha=110, blur=4)
    s.highlight("poly", [(30, 32), (100, 32), (124, 44), (40, 42)], alpha=110)
    # racing stripe
    s.shape("poly", [(40, 46), (132, 47), (132, 53), (40, 54)], (255, 255, 255, 230))
    # cockpit + seat
    s.shape("ellipse", (46, 32, 86, 68), hexc("15121c"), hexc("2a2632"), outline=1.5)
    s.shape("ellipse", (52, 38, 74, 62), hexc("3a3448"), hexc("201c2a"))
    # steering wheel
    s.shape("ellipse", (78, 41, 88, 59), hexc("2a2632"), outline=1.2)
    # rear wing
    s.shape("rrect", (4, 18, 18, 82), lighten(b, 0.1), darken(b, 0.35), outline=2, r=4)
    s.highlight("rrect", (6, 22, 12, 78), alpha=80, r=2)
    # exhausts
    for y in (40, 56):
        s.shape("ellipse", (12, y - 2, 22, y + 6), hexc("8a8498"), hexc("4a4458"), outline=1)
    # number roundel on the nose
    s.shape("ellipse", (106, 42, 122, 58), (255, 255, 255, 255), outline=1)
    s.save(name)


def spr_tree():
    s = Sprite(200, 200)
    rnd = random.Random(30)
    blobs = []
    for i in range(14):
        a = i / 14 * math.tau + rnd.uniform(-0.2, 0.2)
        r = rnd.uniform(44, 64)
        blobs.append((100 + math.cos(a) * r * 0.85, 100 + math.sin(a) * r * 0.85, rnd.uniform(30, 42)))
    blobs.append((100, 100, 58))
    # dark under-layer for depth, then lit blobs
    for x, y, r in blobs:
        s.shape("ellipse", (x - r - 3, y - r + 2, x + r + 3, y + r + 6), hexc("1c4a22"))
    for x, y, r in sorted(blobs, key=lambda b: b[1]):
        s.shape("ellipse", (x - r, y - r, x + r, y + r), hexc("2e7a34"), hexc("8ad06a"), radial=(x - r * 0.4, y - r * 0.45, r * 1.5))
    for _ in range(80):
        x, y = rnd.uniform(40, 160), rnd.uniform(40, 160)
        if math.hypot(x - 100, y - 100) < 80:
            s.shape("ellipse", (x - 3, y - 2, x + 3, y + 2), (170, 225, 120, 140))
    s.save("tree", outline=2)


def spr_bush():
    s = Sprite(110, 90)
    rnd = random.Random(31)
    for i in range(7):
        x, y, r = 25 + i * 10 + rnd.uniform(-4, 4), 55 + rnd.uniform(-12, 6), rnd.uniform(18, 26)
        s.shape("ellipse", (x - r, y - r, x + r, y + r), hexc("2e7a34"), hexc("7ac85a"), radial=(x - r * 0.4, y - r * 0.5, r * 1.6))
    for _ in range(8):
        x, y = rnd.uniform(20, 90), rnd.uniform(30, 70)
        s.shape("ellipse", (x - 3, y - 3, x + 3, y + 3), rnd.choice([hexc("ff5a7a"), hexc("ffd84a"), hexc("ffffff")]))
    s.save("bush", outline=2)


def spr_router():
    s = Sprite(120, 110)
    # antennas
    for x in (30, 90):
        s.shape("line", [(x, 52), (x + (x - 60) * 0.25, 8)], hexc("2a2632"), width=6, outline=1.5)
        s.shape("ellipse", (x + (x - 60) * 0.25 - 6, 2, x + (x - 60) * 0.25 + 6, 14), hexc("ff4a5a"), hexc("8a1a2a"), outline=1.5)
    m = s.shape("rrect", (8, 46, 112, 100), hexc("5a5a6e"), hexc("2a2a36"), outline=2.5, r=12)
    s.shadow_inside(m, (0, 6), alpha=90)
    s.highlight("rrect", (16, 50, 104, 62), alpha=90, r=6)
    s.shape("rrect", (18, 60, 102, 82), hexc("101018"), hexc("1c1c28"), r=6)  # face screen
    s.shape("poly", [(26, 64), (52, 72), (50, 78), (26, 70)], hexc("ff3040"))  # angry eyes
    s.shape("poly", [(94, 64), (68, 72), (70, 78), (94, 70)], hexc("ff3040"))
    for i in range(5):
        s.shape("ellipse", (28 + i * 14, 88, 36 + i * 14, 96), hexc("3ddc84") if i % 2 == 0 else hexc("1a5a2a"))
    s.save("router")


def spr_drone():
    s = Sprite(130, 130)
    for a in range(4):
        ang = math.pi / 4 + a * math.pi / 2
        x, y = 65 + math.cos(ang) * 44, 65 + math.sin(ang) * 44
        s.shape("line", [(65, 65), (x, y)], hexc("3a3448"), width=8, outline=1.5)
        s.shape("ellipse", (x - 22, y - 22, x + 22, y + 22), (200, 210, 230, 70))
        s.shape("ellipse", (x - 8, y - 8, x + 8, y + 8), hexc("6a6478"), hexc("2a2632"), outline=1.5)
    m = s.shape("ellipse", (38, 38, 92, 92), hexc("8a8498"), hexc("2a2632"), outline=2.5, radial=(55, 52, 46))
    s.highlight("ellipse", (48, 44, 70, 58), alpha=120)
    s.shape("ellipse", (56, 56, 74, 74), hexc("ff6070"), hexc("8a0a1a"), outline=1.5, radial=(62, 62, 12))
    s.shape("ellipse", (60, 59, 65, 64), (255, 255, 255, 220))
    s.save("drone")


def spr_turret():
    s = Sprite(130, 120)
    s.shape("rrect", (52, 92, 78, 112), hexc("3a3448"), hexc("1a1622"), outline=2, r=4)  # stand
    s.shape("rrect", (34, 104, 96, 116), hexc("3a3448"), hexc("1a1622"), outline=2, r=5)
    m = s.shape("rrect", (6, 8, 124, 96), hexc("d8d0c0"), hexc("8a8478"), outline=2.5, r=14)  # beige CRT
    s.shadow_inside(m, (0, 6), alpha=70)
    s.shape("rrect", (16, 16, 114, 82), hexc("0c1a2a"), outline=1.5, r=10)
    s.shape("rrect", (20, 20, 110, 78), hexc("1a5a8a"), hexc("0c2a4a"), r=8, radial=(65, 45, 70))
    s.shape("ellipse", (36, 34, 50, 48), hexc("8ae8ff"))  # :(
    s.shape("ellipse", (80, 34, 94, 48), hexc("8ae8ff"))
    s.shape("line", [(46, 68), (65, 58), (84, 68)], hexc("8ae8ff"), width=4)
    s.highlight("poly", [(24, 22), (70, 22), (40, 50), (24, 50)], alpha=50)
    s.shape("ellipse", (104, 86, 112, 92), hexc("3ddc84"))
    s.save("turret")


def spr_printer():
    s = Sprite(140, 120)
    s.shape("rrect", (30, 4, 110, 40), hexc("f4f4f8"), hexc("c8c8d8"), outline=2, r=3)  # paper sticking out
    for i in range(4):
        s.shape("rrect", (40, 10 + i * 7, 96 - i * 8, 13 + i * 7), hexc("b8bccc"), r=1)
    m = s.shape("rrect", (6, 28, 134, 104), hexc("e8e8f0"), hexc("9a9aaa"), outline=2.5, r=12)
    s.shadow_inside(m, (0, 6), alpha=80)
    s.shape("rrect", (16, 64, 124, 76), hexc("2a2632"), r=4)  # output slot
    s.shape("rrect", (92, 36, 126, 56), hexc("1a1a24"), r=4)  # display
    s.shape("ellipse", (96, 41, 104, 49), hexc("ff4a5a"))
    s.shape("poly", [(20, 40), (52, 48), (50, 54), (20, 48)], hexc("2a2632"))  # angry brow
    s.highlight("rrect", (14, 32, 126, 44), alpha=90, r=6)
    s.save("printer")


def spr_chair():
    s = Sprite(240, 240)
    for a in range(5):
        ang = a / 5 * math.tau - math.pi / 2
        x, y = 120 + math.cos(ang) * 104, 120 + math.sin(ang) * 104
        s.shape("line", [(120, 120), (x, y)], hexc("2a2632"), width=14, outline=2)
        s.shape("ellipse", (x - 13, y - 13, x + 13, y + 13), hexc("4a4458"), hexc("15121c"), outline=2)
    m = s.shape("rrect", (48, 52, 192, 188), hexc("2a9ad8"), hexc("124a7a"), outline=3, r=36)  # seat
    s.shadow_inside(m, (0, 8), alpha=90, blur=6)
    s.shape("rrect", (60, 30, 180, 80), hexc("15121c"), hexc("2a2632"), outline=3, r=22)  # backrest top
    s.shape("rrect", (100, 40, 140, 62), hexc("2a9ad8"), r=8)
    for x in (34, 186):  # armrests
        s.shape("rrect", (x - 14, 80, x + 14, 170), hexc("2a2632"), hexc("15121c"), outline=2.5, r=12)
    s.highlight("rrect", (64, 60, 176, 96), alpha=80, r=20)
    for y in range(70, 180, 14):  # stitching
        s.shape("rrect", (70, y, 74, y + 7), (140, 210, 255, 180), r=1)
        s.shape("rrect", (166, y, 170, y + 7), (140, 210, 255, 180), r=1)
    s.save("chair")


def spr_rocket():
    s = Sprite(280, 120)
    for k in (-1, 1):  # fins
        s.shape("poly", [(40, 60 + k * 18), (8, 60 + k * 54), (22, 60 + k * 58), (96, 60 + k * 20)], hexc("ff5a6a"), hexc("8a1a2a"), outline=2.5)
    m = s.shape("rrect", (20, 32, 210, 88), hexc("f4f4ff"), hexc("8a8aa8"), outline=3, r=26)
    s.shadow_inside(m, (0, 8), alpha=90, blur=5)
    s.highlight("rrect", (40, 38, 190, 50), alpha=150, r=6)
    s.shape("poly", [(196, 34), (272, 60), (196, 86)], hexc("ff6a7a"), hexc("8a1a2a"), outline=3)  # nose
    s.highlight("poly", [(200, 40), (250, 56), (200, 52)], alpha=120)
    s.shape("rrect", (70, 32, 84, 88), hexc("8f5cff"), hexc("4a2a9a"), r=2)  # band
    s.shape("ellipse", (126, 40, 166, 80), hexc("2a2632"), outline=2)  # porthole
    s.shape("ellipse", (131, 45, 161, 75), hexc("8fd0ff"), hexc("2a6aa8"), radial=(140, 54, 30))
    s.highlight("ellipse", (135, 48, 147, 58), alpha=180)
    for x in range(36, 200, 16):
        s.shape("ellipse", (x, 82, x + 3, 85), (90, 90, 120, 255))
    s.shape("rrect", (6, 44, 24, 76), hexc("4a4458"), hexc("2a2632"), outline=2, r=5)  # engine bell
    s.save("rocket")


def spr_house(idx, wall, roof, gable):
    """Canal house facade, 170x380, brick texture, white windows."""
    W, H = 170, 380
    s = Sprite(W, H)
    brick = Image.open(f"{OUT_TEX}/brick_red.png").convert("RGB").resize((128 * SS, 128 * SS))
    # body silhouette (with gable)
    top = 70
    if gable == 0:  # stepped
        poly = [(8, H), (8, top + 40), (28, top + 40), (28, top + 20), (52, top + 20), (52, top), (118, top), (118, top + 20), (142, top + 20), (142, top + 40), (162, top + 40), (162, H)]
    elif gable == 1:  # bell
        poly = [(8, H), (8, top + 60)]
        for i in range(21):
            a = math.pi + i / 20 * math.pi
            poly.append((85 + math.cos(a) * 77, top + 60 + math.sin(a) * 60))
        poly += [(162, top + 60), (162, H)]
    else:  # neck
        poly = [(8, H), (8, top + 50), (40, top + 50), (40, top + 10), (85, top - 20), (130, top + 10), (130, top + 50), (162, top + 50), (162, H)]
    m = s.mask("poly", poly)
    o = m.filter(ImageFilter.MaxFilter(int(3 * SS * 2) | 1))
    s.fill(o, OUTLINE)
    # brick fill tinted to the wall colour
    tiled = Image.new("RGB", s.img.size)
    for ty in range(0, s.img.size[1], brick.size[1]):
        for tx in range(0, s.img.size[0], brick.size[0]):
            tiled.paste(brick, (tx, ty))
    arr = np.array(tiled).astype(float) / 160.0
    tint = np.array(wall[:3], float)
    arr = np.clip(arr * tint, 0, 255).astype(np.uint8)
    layer = Image.fromarray(arr, "RGB").convert("RGBA")
    layer.putalpha(m)
    s.img.alpha_composite(layer)
    # light from the left, ambient occlusion at the bottom
    yy, xx = np.mgrid[0:H * SS, 0:W * SS]
    shade = np.clip(xx / (W * SS), 0, 1) * 70
    sh = Image.fromarray(shade.astype(np.uint8), "L")
    s.fill(ImageChops.multiply(sh, m), (0, 0, 0, 255))
    # cornice under the gable
    s.shape("rrect", (4, top + 70, 166, top + 80), hexc("f4f0e8"), hexc("b8b0a8"), outline=1.5, r=2)
    # hoist beam (typical canal house)
    s.shape("rrect", (80, top + 2, 90, top + 22), hexc("3a2418"), outline=1, r=1)
    # windows
    rows = 4
    for r in range(rows):
        for c in range(3):
            x0 = 22 + c * 46
            y0 = top + 100 + r * 62
            if r == rows - 1 and c == 1:
                continue
            s.shape("rrect", (x0 - 3, y0 - 3, x0 + 33, y0 + 47), hexc("f4f0e8"), hexc("c8c0b8"), outline=1.5, r=3)
            s.shape("rrect", (x0, y0, x0 + 30, y0 + 44), hexc("6a8ab8"), hexc("1a2a48"), r=2, direction="d")
            s.highlight("poly", [(x0 + 2, y0 + 2), (x0 + 14, y0 + 2), (x0 + 2, y0 + 18)], alpha=110)
            s.shape("rrect", (x0 + 13, y0, x0 + 17, y0 + 44), hexc("f4f0e8"), r=1)
            s.shape("rrect", (x0, y0 + 20, x0 + 30, y0 + 24), hexc("f4f0e8"), r=1)
    # door with steps
    s.shape("rrect", (66, H - 76, 104, H - 12), roof, darken(roof, 0.4), outline=1.5, r=14)
    s.shape("ellipse", (95, H - 46, 100, H - 41), hexc("ffc531"))
    s.shape("rrect", (58, H - 14, 112, H), hexc("8a8a92"), hexc("5a5a62"), outline=1.5, r=2)
    s.save(f"house{idx}")


def spr_tourist(idx, shirt, shorts, skin, hat):
    s = Sprite(90, 170)
    for x in (32, 50):  # legs
        s.shape("rrect", (x, 110, x + 12, 160), skin, darken(skin, 0.25), outline=1.5, r=5)
        s.shape("rrect", (x - 3, 154, x + 15, 166), hexc("f4f4f4"), hexc("a8a8b8"), outline=1.5, r=4)
    s.shape("rrect", (26, 90, 66, 124), shorts, darken(shorts, 0.35), outline=2, r=8)
    m = s.shape("rrect", (20, 46, 70, 100), shirt, darken(shirt, 0.35), outline=2, r=14)
    s.shadow_inside(m, (4, 4), alpha=70)
    # flower print
    rnd = random.Random(idx)
    for _ in range(6):
        x, y = rnd.uniform(26, 64), rnd.uniform(52, 92)
        s.shape("ellipse", (x - 3, y - 3, x + 3, y + 3), lighten(shirt, 0.6))
    s.shape("rrect", (10, 52, 22, 92), skin, darken(skin, 0.25), outline=1.5, r=5)   # arm holding the map
    s.shape("rrect", (0, 70, 24, 98), hexc("f4f0e0"), hexc("c8c0a8"), outline=1.5, r=2)  # map
    s.shape("rrect", (68, 54, 80, 90), skin, darken(skin, 0.25), outline=1.5, r=5)
    s.shape("rrect", (36, 62, 56, 76), hexc("2a2632"), hexc("15121c"), outline=1.5, r=3)  # camera
    s.shape("ellipse", (41, 64, 51, 74), hexc("4a6a9a"), outline=1)
    s.shape("ellipse", (28, 14, 62, 50), skin, darken(skin, 0.2), outline=2, radial=(40, 26, 34))  # head
    s.shape("ellipse", (36, 28, 41, 33), OUTLINE)
    s.shape("ellipse", (49, 28, 54, 33), OUTLINE)
    if hat == 0:  # sun hat
        s.shape("ellipse", (16, 14, 74, 26), hexc("f4ecd0"), hexc("c8b890"), outline=1.5)
        s.shape("ellipse", (30, 2, 60, 22), hexc("f4ecd0"), hexc("c8b890"), outline=1.5)
        s.shape("rrect", (30, 14, 60, 18), hexc("ff6a7a"), r=1)
    else:  # cap
        s.shape("ellipse", (28, 8, 62, 28), hexc("2a7ad8"), hexc("124a8a"), outline=1.5)
        s.shape("rrect", (54, 18, 76, 24), hexc("2a7ad8"), outline=1.5, r=3)
    s.save(f"tourist{idx}")


def spr_scooter():
    s = Sprite(170, 130)
    for x in (34, 136):
        s.shape("ellipse", (x - 20, 88, x + 20, 128), hexc("2a2632"), hexc("15121c"), outline=2)
        s.shape("ellipse", (x - 8, 100, x + 8, 116), hexc("8a8498"))
    s.shape("poly", [(30, 96), (60, 70), (130, 70), (150, 100), (120, 104), (50, 104)], hexc("ff4a5a"), hexc("8a1a2a"), outline=2.5)
    s.highlight("poly", [(60, 74), (128, 74), (134, 82), (64, 82)], alpha=110)
    s.shape("line", [(138, 80), (146, 30)], hexc("8a8498"), width=6, outline=1.5)
    s.shape("rrect", (130, 24, 160, 32), hexc("2a2632"), outline=1.5, r=3)
    s.shape("ellipse", (150, 34, 166, 50), hexc("fff3a0"), hexc("ffc531"), outline=1.5)
    # rider: delivery jacket + helmet
    s.shape("rrect", (70, 30, 112, 74), hexc("2a9a5a"), hexc("1a5a3a"), outline=2, r=12)
    s.shape("rrect", (60, 24, 96, 62), hexc("3ddc84"), hexc("1a8a4a"), outline=2, r=6)  # delivery box
    s.shape("ellipse", (98, 2, 130, 34), hexc("2a2632"), hexc("15121c"), outline=2, radial=(108, 10, 26))
    s.highlight("ellipse", (104, 6, 116, 14), alpha=120)
    s.save("scooter")


def spr_boat():
    s = Sprite(340, 110)
    s.shape("poly", [(10, 60), (330, 60), (300, 104), (30, 104)], hexc("2a4a7a"), hexc("15253a"), outline=2.5)
    s.shape("rrect", (40, 26, 300, 62), hexc("dfe8f4"), hexc("8aa0b8"), outline=2, r=16)  # glass roof
    for i in range(9):
        s.shape("rrect", (52 + i * 27, 32, 72 + i * 27, 54), hexc("6a9ad8"), hexc("2a4a7a"), r=4)
        s.highlight("rrect", (54 + i * 27, 34, 62 + i * 27, 40), alpha=120, r=2)
    s.shape("rrect", (10, 64, 330, 70), hexc("ffc531"), r=2)
    s.save("boat")


def spr_bollard():
    s = Sprite(36, 92)
    s.shape("rrect", (8, 14, 28, 90), hexc("8a2a24"), hexc("4a1210"), outline=2, r=6, direction="h")
    s.shape("ellipse", (6, 4, 30, 22), hexc("8a2a24"), hexc("4a1210"), outline=2)
    for y in (34, 48, 62):  # the three X's of Amsterdam
        s.shape("line", [(12, y - 5), (24, y + 5)], hexc("f4f0e8"), width=2.4)
        s.shape("line", [(24, y - 5), (12, y + 5)], hexc("f4f0e8"), width=2.4)
    s.highlight("rrect", (10, 18, 15, 84), alpha=90, r=2)
    s.save("bollard")


def spr_duck():
    s = Sprite(70, 56)
    s.shape("ellipse", (14, 22, 64, 52), hexc("f4f0e8"), hexc("b8b0a0"), outline=2, radial=(30, 30, 40))
    s.shape("poly", [(52, 28), (68, 18), (62, 34)], hexc("d8d0c0"), outline=1.5)  # tail
    s.shape("ellipse", (4, 4, 30, 30), hexc("2a8a4a"), hexc("124a2a"), outline=2, radial=(12, 10, 22))
    s.shape("poly", [(0, 18), (-4 + 4, 22), (8, 22)], hexc("ff9a2a"))
    s.shape("ellipse", (0, 16, 10, 24), hexc("ff9a2a"), outline=1.2)
    s.shape("ellipse", (12, 12, 17, 17), OUTLINE)
    s.shape("rrect", (6, 28, 30, 31), (255, 255, 255, 255), r=1)
    s.save("duck")


def spr_canal_tree():
    s = Sprite(180, 260)
    s.shape("rrect", (80, 120, 100, 260), hexc("5a3a24"), hexc("2a1a10"), outline=2, r=6, direction="h")
    rnd = random.Random(40)
    blobs = [(90 + rnd.uniform(-55, 55), 90 + rnd.uniform(-60, 40), rnd.uniform(26, 40)) for _ in range(16)]
    blobs.append((90, 80, 52))
    for x, y, r in blobs:
        s.shape("ellipse", (x - r - 2, y - r + 4, x + r + 2, y + r + 6), hexc("1c4a22"))
    for x, y, r in sorted(blobs, key=lambda b: b[1]):
        s.shape("ellipse", (x - r, y - r, x + r, y + r), hexc("2e7a34"), hexc("8ad06a"), radial=(x - r * 0.4, y - r * 0.5, r * 1.6))
    s.save("canaltree", outline=2)


if __name__ == "__main__":
    tex_grass()
    tex_asphalt("asphalt", (52, 52, 60), (84, 84, 94), 3)
    tex_asphalt("fietspad", (130, 48, 40), (172, 70, 58), 4)
    tex_stone()
    tex_bricks("brick_red", (150, 70, 52), (190, 180, 170), 21)
    tex_bricks("brick_dark", (60, 50, 74), (30, 26, 38), 22)
    tex_water()
    tex_wood()
    tex_cobble()
    tex_tulips()
    for name, col in [("kart_pieter", hexc("6b3fd1")), ("kart_davi", hexc("2a9ad8")), ("kart_kira", hexc("e85a9a")), ("kart_horse", hexc("d8822a"))]:
        spr_kart(name, col)
    spr_tree()
    spr_bush()
    spr_router()
    spr_drone()
    spr_turret()
    spr_printer()
    spr_chair()
    spr_rocket()
    walls = [hexc("b8604a"), hexc("8a3a2a"), hexc("d8c8a8"), hexc("5a6a8a"), hexc("7a5038"), hexc("c88a4a")]
    doors = [hexc("2a5a3a"), hexc("1a2a4a"), hexc("6a1a1a"), hexc("2a2a2a"), hexc("2a5a6a"), hexc("4a2a6a")]
    for i in range(6):
        spr_house(i, walls[i], doors[i], i % 3)
    spr_tourist(0, hexc("ff6a8a"), hexc("2a4a8a"), hexc("f0c8a0"), 0)
    spr_tourist(1, hexc("3ab8e8"), hexc("c8b890"), hexc("c88a5a"), 1)
    spr_tourist(2, hexc("ffc531"), hexc("3a3a4a"), hexc("8a5a3a"), 0)
    spr_scooter()
    spr_boat()
    spr_bollard()
    spr_duck()
    spr_canal_tree()
