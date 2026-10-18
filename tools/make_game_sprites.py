#!/usr/bin/env python3
"""Sprites for the newer games (Meeuwen!, Polder Defense, Dakkenloper,
Pannenkoekenhuis). Same pipeline as make_textures.py (supersampled vector
shapes with gradients, highlights and outlines). Run from the repo root:
    python3 tools/make_game_sprites.py [name-prefix ...]
With prefixes, only the matching sprites are written."""
import random
import sys

import numpy as np
from PIL import Image, ImageDraw

from make_textures import OUT_TEX, OUTLINE, Sprite, colorize, darken, eye, fbm, hexc, lighten

JOBS = []


def job(f):
    JOBS.append(f)
    return f


# ====================================================================== Meeuwen!


@job
def tex_sand():
    W = 512
    n = fbm(W, W, 4, 6, seed=61) * 0.6 + fbm(W, W, 32, 2, seed=62) * 0.4
    im = Image.fromarray(colorize(n, (214, 184, 132), (246, 226, 180)).astype(np.uint8), "RGB")
    d = ImageDraw.Draw(im)
    rnd = random.Random(63)
    for _ in range(7000):  # grains
        x, y = rnd.uniform(0, W), rnd.uniform(0, W)
        c = rnd.choice([(190, 160, 110), (255, 240, 205), (175, 145, 100), (230, 205, 160)])
        d.point((x, y), fill=c)
    for _ in range(26):  # shell bits
        x, y = rnd.uniform(8, W - 8), rnd.uniform(8, W - 8)
        r = rnd.uniform(1.5, 3)
        d.ellipse([x - r, y - r * 0.7, x + r, y + r * 0.7], fill=rnd.choice([(255, 250, 240), (240, 200, 190), (200, 190, 200)]))
    im.save(f"{OUT_TEX}/sand.png")
    print("tex sand")



@job
def gull_body():
    s = Sprite(130, 84)
    # tail feathers, then the body: white with a grey back
    s.shape("poly", [(4, 30), (30, 34), (34, 50), (6, 46)], hexc("2a2a32"), hexc("15121c"), outline=2)
    s.shape("poly", [(8, 34), (34, 36), (34, 46), (10, 44)], hexc("f4f4f8"), hexc("c8ccd8"))
    m = s.shape("ellipse", (18, 22, 108, 70), hexc("ffffff"), hexc("c8ccd8"), outline=2.5, radial=(60, 30, 70))
    s.shape("poly", [(26, 34), (60, 24), (96, 28), (80, 40), (36, 42)], hexc("aab2c4"), hexc("7a8296"))
    s.shadow_inside(m, (0, 6), alpha=70, blur=4)
    s.highlight("ellipse", (50, 50, 96, 62), alpha=90)
    # legs tucked in
    s.shape("line", [(58, 66), (52, 80)], hexc("ff9a3a"), width=4, outline=1)
    s.shape("line", [(70, 66), (66, 80)], hexc("ff9a3a"), width=4, outline=1)
    s.save("gull_body")


@job
def gull_wing():
    # pivot at the left middle (the shoulder); tip on the right
    s = Sprite(120, 48)
    m = s.shape("poly", [(2, 18), (40, 6), (84, 8), (118, 20), (96, 30), (60, 38), (20, 36), (2, 30)],
                hexc("c4cad8"), hexc("8a92a6"), outline=2.5)
    s.shape("poly", [(84, 9), (118, 20), (96, 30), (78, 30)], hexc("2a2a32"), hexc("15121c"))  # black tip
    for x in (88, 100):
        s.shape("ellipse", (x - 3, 18, x + 3, 24), hexc("f4f4f8"))  # the white "mirrors" on the tip
    for x in (30, 46, 62):
        s.shape("line", [(x, 32), (x + 10, 20)], darken(hexc("8a92a6"), 0.1), width=1.5)
    s.shadow_inside(m, (0, 4), alpha=50)
    s.highlight("poly", [(8, 20), (40, 10), (70, 12), (30, 20)], alpha=110)
    s.save("gull_wing")


def food_base(name, w=64, h=64):
    return Sprite(w, h)


@job
def food_patat():
    s = food_base("patat")
    rnd = random.Random(5)
    for i in range(9):
        x = 18 + i * 3.6 + rnd.uniform(-2, 2)
        top = rnd.uniform(4, 16)
        ang = rnd.uniform(-5, 5)
        s.shape("poly", [(x, top), (x + 6, top + 1), (x + 5 + ang * 0.2, 40), (x - 1 + ang * 0.2, 40)], hexc("ffd84a"), hexc("e8a020"), outline=1.2)
    s.shape("poly", [(12, 30), (52, 30), (42, 62), (22, 62)], hexc("ff4a3a"), hexc("a81a1a"), outline=2)
    s.shape("poly", [(18, 38), (46, 38), (44, 44), (20, 44)], hexc("ffffff"))
    s.shape("ellipse", (22, 22, 42, 34), hexc("fffaf0"), hexc("e8e0c8"), outline=1.2)  # mayo
    s.highlight("poly", [(14, 32), (22, 32), (26, 60), (22, 60)], alpha=90)
    s.save("food_patat")


@job
def food_ijs():
    s = food_base("ijs")
    s.shape("poly", [(18, 30), (46, 30), (32, 63)], hexc("e8b060"), hexc("a8702a"), outline=2)
    for k in range(4):
        s.shape("line", [(20 + k * 7, 32), (30 + k * 3, 54)], hexc("b87a32"), width=1.3)
    s.shape("ellipse", (14, 16, 50, 38), hexc("ffd0e0"), hexc("e88aa8"), outline=2, radial=(26, 20, 30))  # strawberry
    s.shape("ellipse", (18, 2, 46, 26), hexc("fff4e0"), hexc("e8d4b0"), outline=2, radial=(28, 8, 24))  # vanilla
    s.shape("ellipse", (38, 0, 46, 8), hexc("e83a4a"), outline=1)
    s.highlight("ellipse", (22, 6, 30, 14), alpha=150)
    s.save("food_ijs")


@job
def food_kroket():
    s = food_base("kroket", 72, 48)
    s.shape("rrect", (4, 18, 68, 46), hexc("f4e8c8"), hexc("c8b088"), outline=2, r=12)  # bun
    m = s.shape("rrect", (8, 6, 64, 30), hexc("d88a3a"), hexc("8a4a14"), outline=2, r=12)
    rnd = random.Random(2)
    for _ in range(40):
        x, y = rnd.uniform(12, 60), rnd.uniform(9, 27)
        s.shape("ellipse", (x - 1, y - 1, x + 1, y + 1), hexc("f0b060"))
    s.shadow_inside(m, (0, 3), alpha=70)
    s.shape("rrect", (44, 2, 56, 10), hexc("ffd84a"), hexc("d8a020"), outline=1, r=3)  # mustard
    s.highlight("rrect", (14, 9, 40, 14), alpha=100, r=3)
    s.save("food_kroket")


@job
def food_haring():
    s = food_base("haring", 80, 44)
    m = s.shape("poly", [(4, 22), (18, 10), (54, 8), (66, 16), (78, 6), (76, 22), (78, 38), (66, 28), (54, 36), (18, 34)],
                hexc("c8d4e0"), hexc("5a6a80"), outline=2)
    s.shape("poly", [(18, 12), (54, 10), (62, 16), (18, 18)], hexc("4a5a74"))
    s.shadow_inside(m, (0, 3), alpha=60)
    for x in (24, 32, 40):  # uitjes on top
        s.shape("rrect", (x, 2, x + 6, 8), hexc("ffffff"), hexc("d8e0c8"), outline=1, r=2)
    eye(s, 12, 20, 2.2)
    s.highlight("poly", [(20, 20), (54, 18), (54, 22), (20, 24)], alpha=140)
    s.save("food_haring")


@job
def food_frikandel():
    s = food_base("frikandel", 72, 40)
    s.shape("rrect", (4, 14, 68, 38), hexc("f4f0f0"), hexc("c8c0c0"), outline=2, r=4)  # tray
    m = s.shape("rrect", (8, 8, 64, 24), hexc("a85a3a"), hexc("5a2a14"), outline=2, r=8)
    s.shadow_inside(m, (0, 3), alpha=70)
    s.shape("line", [(12, 12), (24, 16), (36, 11), (48, 16), (60, 12)], hexc("ff4a3a"), width=2.6)  # curry
    s.shape("line", [(14, 15), (30, 12), (44, 15), (58, 14)], hexc("fffaf0"), width=2.4)  # mayo
    for x in (20, 34, 50):
        s.shape("ellipse", (x - 2, 6, x + 2, 10), hexc("f4f0e0"), outline=0.8)  # onions
    s.save("food_frikandel")


def beach_body(idx, skin, top, bottom, hair, extra):
    """A beachgoer seen from the side, without the arm that holds the food
    (the game draws it so it can hide the snack)."""
    s = Sprite(90, 170)
    for x in (32, 50):
        s.shape("rrect", (x, 106, x + 12, 160), skin, darken(skin, 0.25), outline=1.5, r=5)
        s.shape("rrect", (x - 2, 156, x + 14, 166), hexc("ffffff") if extra != 2 else skin, hexc("b8b8c8"), outline=1.5, r=4)
    if extra == 1:  # swimsuit
        m = s.shape("rrect", (24, 50, 66, 118), top, darken(top, 0.35), outline=2, r=14)
        for y in (62, 78, 94, 110):
            s.shape("line", [(26, y), (64, y - 4)], lighten(top, 0.45), width=3)
    else:
        s.shape("rrect", (24, 92, 66, 124), bottom, darken(bottom, 0.35), outline=2, r=8)
        m = s.shape("rrect", (20, 48, 70, 100), top, darken(top, 0.35), outline=2, r=14)
        if extra == 0:  # hawaii shirt
            rnd = random.Random(idx)
            for _ in range(7):
                x, y = rnd.uniform(26, 64), rnd.uniform(52, 92)
                s.shape("ellipse", (x - 3, y - 3, x + 3, y + 3), lighten(top, 0.6))
                s.shape("ellipse", (x - 1, y - 1, x + 1, y + 1), hexc("ffc531"))
        if extra == 2:  # tank top with stripes
            for y in (60, 72, 84):
                s.shape("line", [(22, y), (68, y)], lighten(top, 0.5), width=4)
    s.shadow_inside(m, (4, 4), alpha=70)
    s.shape("rrect", (12, 52, 24, 94), skin, darken(skin, 0.25), outline=1.5, r=5)  # back arm
    s.shape("ellipse", (28, 12, 62, 50), skin, darken(skin, 0.2), outline=2, radial=(40, 24, 34))  # head
    s.shape("ellipse", (50, 26, 56, 32), OUTLINE)
    s.shape("line", [(48, 40), (56, 40)], darken(skin, 0.5), width=1.6)  # mouth
    if hair == 0:  # sun hat
        s.shape("ellipse", (14, 12, 76, 24), hexc("f4ecd0"), hexc("c8b890"), outline=1.5)
        s.shape("ellipse", (28, 0, 62, 20), hexc("f4ecd0"), hexc("c8b890"), outline=1.5)
        s.shape("rrect", (28, 12, 62, 16), hexc("2a7ad8"), r=1)
    elif hair == 1:  # bun and sunglasses
        s.shape("ellipse", (26, 8, 60, 30), hexc("8a4a24"), hexc("4a2410"), outline=1.5)
        s.shape("ellipse", (22, 0, 38, 16), hexc("8a4a24"), hexc("4a2410"), outline=1.5)
        s.shape("rrect", (42, 22, 62, 30), hexc("1a1a24"), outline=1, r=3)
    elif hair == 2:  # bucket hat
        s.shape("poly", [(22, 22), (30, 4), (60, 4), (68, 22)], hexc("ffc531"), hexc("c88a1a"), outline=1.5)
        s.shape("rrect", (18, 18, 72, 26), hexc("ffc531"), hexc("c88a1a"), outline=1.5, r=4)
    else:  # bald + red sunburn
        s.shape("ellipse", (32, 12, 58, 28), hexc("ff8a7a"))
        s.highlight("ellipse", (38, 14, 48, 20), alpha=160)
    s.save(f"beach{idx}")


@job
def beach_people():
    beach_body(0, hexc("f0c8a0"), hexc("3ab8e8"), hexc("f4ecd0"), 0, 0)
    beach_body(1, hexc("c88a5a"), hexc("ff4a7a"), hexc("000000"), 1, 1)
    beach_body(2, hexc("ffd8c0"), hexc("ff8a2a"), hexc("2a3a5a"), 3, 2)
    beach_body(3, hexc("8a5a3a"), hexc("3ddc84"), hexc("6a3fd1"), 2, 0)


@job
def parasol():
    s = Sprite(220, 230)
    # strandstoel (beach chair) under the umbrella
    s.shape("poly", [(70, 170), (150, 170), (160, 212), (60, 212)], hexc("2a7ad8"), hexc("124a8a"), outline=2)
    for x in (86, 110, 134):
        s.shape("line", [(x, 172), (x + 4, 210)], hexc("f4f4f8"), width=6)
    s.shape("poly", [(130, 120), (166, 120), (162, 176), (138, 176)], hexc("2a7ad8"), hexc("124a8a"), outline=2)
    s.shape("line", [(66, 212), (60, 228)], hexc("8a8a92"), width=4, outline=1)
    s.shape("line", [(154, 212), (162, 228)], hexc("8a8a92"), width=4, outline=1)
    s.shape("line", [(110, 40), (106, 228)], hexc("f4f0e8"), width=5, outline=1.5)
    # umbrella: alternating red/white panels
    cx, cy = 110, 60
    pts = [(cx - 104, cy + 12), (cx - 70, cy - 24), (cx - 30, cy - 40), (cx, cy - 44), (cx + 30, cy - 40), (cx + 70, cy - 24), (cx + 104, cy + 12)]
    s.shape("poly", pts + [(cx, cy - 6)], hexc("ffffff"), outline=2.5)
    for i in range(0, 6, 2):
        a, b = pts[i], pts[i + 1]
        s.shape("poly", [a, b, (cx, cy - 6)], hexc("ff4a5a"), hexc("b01a2a"))
    for i in range(6):
        a, b = pts[i], pts[i + 1]
        mid = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2 + 10)
        s.shape("poly", [a, b, mid], hexc("ffffff") if i % 2 else hexc("ff4a5a"), outline=1.5)
    s.highlight("poly", [(cx - 60, cy - 20), (cx - 10, cy - 36), (cx, cy - 20), (cx - 50, cy - 8)], alpha=90)
    s.shape("ellipse", (cx - 5, cy - 52, cx + 5, cy - 42), hexc("ffc531"), outline=1.5)
    s.save("parasol")


@job
def kraam():
    s = Sprite(280, 230)
    s.shape("rrect", (20, 100, 260, 226), hexc("f4f0e8"), hexc("c8c0b0"), outline=2.5, r=6)  # counter
    for i in range(6):
        x = 24 + i * 40
        s.shape("rrect", (x, 150, x + 32, 222), hexc("ffc531") if i % 2 else hexc("ff4a3a"), outline=1.5, r=3)
    s.shape("rrect", (10, 96, 270, 112), hexc("8a5a3a"), hexc("4a2a14"), outline=2, r=3)  # counter top
    s.shape("rrect", (34, 20, 44, 100), hexc("8a8a92"), outline=1.5, r=2)
    s.shape("rrect", (236, 20, 246, 100), hexc("8a8a92"), outline=1.5, r=2)
    # striped awning
    s.shape("poly", [(4, 30), (276, 30), (264, 0), (16, 0)], hexc("ffffff"), outline=2.5)
    for i in range(0, 10, 2):
        x0, x1 = 16 + i * 24.8, 16 + (i + 1) * 24.8
        s.shape("poly", [(x0 - 12 + i * 0.1, 30), (x1 - 12, 30), (x1, 0), (x0, 0)], hexc("ff4a3a"))
    for i in range(10):
        x = 4 + i * 27.2
        s.shape("ellipse", (x, 22, x + 27, 44), hexc("ff4a3a") if i % 2 == 0 else hexc("ffffff"), outline=1.5)
    s.shape("rrect", (70, 120, 210, 146), hexc("2a2632"), outline=2, r=6)  # sign
    s.save("kraam")


@job
def kite():
    s = Sprite(80, 100)
    s.shape("poly", [(40, 2), (76, 40), (40, 98), (4, 40)], hexc("ffc531"), outline=2.5)
    s.shape("poly", [(40, 2), (76, 40), (40, 40)], hexc("ff4a5a"))
    s.shape("poly", [(40, 40), (4, 40), (40, 98)], hexc("2a7ad8"))
    s.shape("line", [(40, 4), (40, 96)], hexc("2a2632"), width=1.5)
    s.shape("line", [(6, 40), (74, 40)], hexc("2a2632"), width=1.5)
    s.highlight("poly", [(40, 8), (66, 38), (44, 36)], alpha=110)
    s.save("kite")


# ====================================================================== Polder Defense


@job
def tex_klinker():
    """Herringbone brick road (klinkers), tileable."""
    W = 256
    im = Image.new("RGB", (W, W), (120, 90, 80))
    d = ImageDraw.Draw(im)
    rnd = random.Random(71)
    bw, bh = 32, 16
    for gy in range(-2, W // bh + 3):
        for gx in range(-2, W // bw + 3):
            for k in range(2):
                if k == 0:
                    x0, y0, x1, y1 = gx * bw + gy * bh, gy * bh, gx * bw + gy * bh + bw, gy * bh + bh
                else:
                    x0, y0, x1, y1 = gx * bw + gy * bh + bw - bh, gy * bh + bh, gx * bw + gy * bh + bw, gy * bh + bh + bw
                base = rnd.choice([(150, 70, 52), (138, 62, 48), (164, 82, 60), (128, 58, 46), (156, 90, 70)])
                for ox in (-W, 0, W):
                    for oy in (-W, 0, W):
                        d.rectangle([x0 + ox + 1, y0 + oy + 1, x1 + ox - 1, y1 + oy - 1], fill=base)
                        d.line([(x0 + ox + 1, y0 + oy + 1), (x1 + ox - 2, y0 + oy + 1)], fill=lighten(base + (255,), 0.18)[:3])
    n = fbm(W, W, 8, 4, seed=72)
    arr = np.array(im, float) * (0.85 + 0.3 * n[..., None])
    Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB").save(f"{OUT_TEX}/klinker.png")
    print("tex klinker")


@job
def tw_pedestal():
    s = Sprite(90, 80)
    m = s.shape("rrect", (8, 22, 82, 74), hexc("a8503a"), hexc("5a2418"), outline=2.5, r=10, direction="h")
    for y in (34, 46, 58):
        s.shape("line", [(10, y), (80, y)], hexc("e8d8c8"), width=1.2)
    for row, y in enumerate((22, 34, 46, 58)):
        for x in range(14 + (row % 2) * 9, 80, 18):
            s.shape("line", [(x, y), (x, y + 12)], hexc("e8d8c8"), width=1.2)
    s.shadow_inside(m, (6, 0), alpha=90, blur=5)
    s.shape("ellipse", (6, 8, 84, 38), hexc("c8c0b8"), hexc("8a8078"), outline=2.5)  # stone cap
    s.shape("ellipse", (16, 12, 74, 32), hexc("a8a098"), hexc("d8d0c8"))
    s.highlight("rrect", (14, 40, 22, 70), alpha=70, r=3)
    s.save("tw_pedestal")


@job
def tw_mill():
    """A Dutch windmill body (the sails are drawn by the game so they can turn)."""
    s = Sprite(110, 170)
    s.shape("poly", [(10, 168), (100, 168), (96, 150), (14, 150)], hexc("6a6a72"), hexc("3a3a42"), outline=2)  # stone base
    m = s.shape("poly", [(18, 152), (92, 152), (76, 40), (34, 40)], hexc("5a6a5a"), hexc("2a3a2a"), outline=2.5, direction="h")
    for y in range(52, 150, 10):
        w = 21 + (y - 40) * 0.15
        s.shape("line", [(55 - w, y), (55 + w, y)], hexc("3a4a3a"), width=1.2)
    s.shadow_inside(m, (10, 0), alpha=90, blur=6)
    s.shape("rrect", (6, 104, 104, 112), hexc("7a5030"), hexc("4a2a14"), outline=2, r=2)  # stage (gallery)
    for x in range(10, 104, 10):
        s.shape("line", [(x, 112), (x, 122)], hexc("4a2a14"), width=1.5)
    s.shape("rrect", (44, 128, 66, 152), hexc("2a5a3a"), hexc("123a20"), outline=1.5, r=8)  # door
    s.shape("rrect", (46, 70, 62, 86), hexc("f4ecd8"), outline=1.5, r=2)  # window
    s.shape("line", [(54, 70), (54, 86)], OUTLINE, width=1)
    s.shape("poly", [(28, 42), (82, 42), (74, 18), (55, 6), (36, 18)], hexc("3a3a42"), hexc("1a1a22"), outline=2.5)  # cap
    s.highlight("poly", [(40, 20), (55, 10), (52, 22), (38, 36)], alpha=90)
    s.shape("ellipse", (46, 20, 64, 38), hexc("6a4a2a"), hexc("3a2410"), outline=2)  # axle
    s.save("tw_mill")


@job
def farm():
    s = Sprite(220, 190)
    s.shape("rrect", (16, 92, 204, 186), hexc("a8503a"), hexc("6a2a1a"), outline=2.5, r=4)  # brick walls
    for y in range(100, 186, 9):
        s.shape("line", [(18, y), (202, y)], hexc("c88a72"), width=1)
    for x, w in ((34, 30), (156, 30)):  # windows with green shutters
        s.shape("rrect", (x, 118, x + w, 150), hexc("f4f0e8"), outline=2, r=2)
        s.shape("rrect", (x + 3, 121, x + w - 3, 147), hexc("6a9ad8"), hexc("2a4a7a"), r=1)
        s.shape("line", [(x + w / 2, 121), (x + w / 2, 147)], hexc("f4f0e8"), width=2)
        s.shape("rrect", (x - 10, 118, x - 1, 150), hexc("2a7a4a"), outline=1.5, r=1)
        s.shape("rrect", (x + w + 1, 118, x + w + 10, 150), hexc("2a7a4a"), outline=1.5, r=1)
    s.shape("rrect", (86, 120, 134, 186), hexc("2a7a4a"), hexc("124a2a"), outline=2, r=20)  # big barn door
    s.shape("line", [(110, 124), (110, 186)], hexc("124a2a"), width=2)
    m = s.shape("poly", [(2, 100), (110, 8), (218, 100)], hexc("c8a050"), hexc("7a5a24"), outline=3)  # thatched roof
    rnd = random.Random(9)
    for _ in range(140):
        x = rnd.uniform(14, 206)
        top = 100 - (106 - abs(x - 110)) * 0.85
        y = rnd.uniform(max(top, 14), 98)
        s.shape("line", [(x, y), (x + rnd.uniform(-2, 2), y + 7)], hexc("a8803a"), width=1)
    s.shadow_inside(m, (0, 8), alpha=80, blur=6)
    s.shape("poly", [(96, 20), (110, 8), (124, 20), (124, 30), (96, 30)], hexc("8a3a2a"), outline=2)  # ridge
    s.shape("ellipse", (98, 44, 122, 68), hexc("f4ecd8"), outline=2)  # round loft window
    s.shape("line", [(110, 44), (110, 68)], OUTLINE, width=1)
    s.save("farm")


if __name__ == "__main__":
    only = sys.argv[1:]
    for f in JOBS:
        if not only or any(f.__name__.startswith(p) for p in only):
            f()
