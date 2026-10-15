#!/usr/bin/env python3
"""Builds the 512x512 Discord Rich Presence images in assets/discord/.
Upload them in the Discord developer portal (Rich Presence -> Art Assets);
the file name without .png is the asset key the game uses.
Run from the repo root: python3 tools/make_discord_assets.py"""
from PIL import Image, ImageDraw, ImageFont

SRC = Image.open("assets/images/menu_bg.png").convert("RGBA")
OUT = "assets/discord/"
S = 512
PURPLE, DEEP, GOLD, GREEN = (107, 63, 209), (26, 15, 46), (255, 197, 49), (31, 138, 76)
FONT = "assets/fonts/NotoSans-Black.ttf"


def base(color):
    im = Image.new("RGBA", (S, S), DEEP)
    ImageDraw.Draw(im).rounded_rectangle((0, 0, S - 1, S - 1), 90, fill=color)
    return im


def portrait(box, size):
    p = SRC.crop(box).resize((size, size), Image.LANCZOS)
    mask = Image.new("L", (size * 4, size * 4), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, size * 4, size * 4), fill=255)
    p.putalpha(mask.resize((size, size), Image.LANCZOS))
    return p


def ringed(im, box, size, ring):
    d = ImageDraw.Draw(im)
    o = (S - size) // 2
    d.ellipse((o - 14, o - 14, o + size + 14, o + size + 14), fill=ring)
    im.alpha_composite(portrait(box, size), (o, o))
    return im


CROPS = {"pieter": (118, 318, 338, 538), "horse": (1035, 50, 1335, 400),
         "davi": (1050, 425, 1270, 645), "kira": (1295, 475, 1515, 695)}

# logo: Pieter with a big golden 3
logo = ringed(base(PURPLE), CROPS["pieter"], 400, GOLD)
d = ImageDraw.Draw(logo)
f = ImageFont.truetype(FONT, 200)
d.text((400, 400), "3", font=f, anchor="mm", fill=GOLD, stroke_width=12, stroke_fill=DEEP)
logo.save(OUT + "logo.png")

ringed(base((160, 88, 42)), CROPS["horse"], 420, GOLD).save(OUT + "horse.png")
ringed(base(PURPLE), CROPS["davi"], 420, (60, 214, 255)).save(OUT + "davi.png")
ringed(base(PURPLE), CROPS["kira"], 420, (255, 111, 181)).save(OUT + "kira.png")

# casino: a big Pietercoin on green felt
casino = base(GREEN)
d = ImageDraw.Draw(casino)
d.ellipse((76, 96, 436, 456), fill=(199, 134, 26))
d.ellipse((76, 76, 436, 436), fill=GOLD, outline=DEEP, width=10)
d.ellipse((126, 126, 386, 386), fill=(255, 214, 90))
d.text((256, 256), "P", font=ImageFont.truetype(FONT, 230), anchor="mm", fill=(199, 134, 26))
casino.save(OUT + "casino.png")

# stroopwafel: waffle seen from above
w = base((201, 138, 31))
d = ImageDraw.Draw(w)
d.ellipse((56, 56, 456, 456), fill=(224, 166, 74), outline=DEEP, width=12)
d.ellipse((96, 96, 416, 416), outline=(184, 101, 31), width=8)
grid = Image.new("L", (S, S), 0)
g = ImageDraw.Draw(grid)
for k in range(-400, 900, 56):
    g.line((k, 0, k + S, S), fill=255, width=10)
    g.line((k + S, 0, k, S), fill=255, width=10)
clip = Image.new("L", (S, S), 0)
ImageDraw.Draw(clip).ellipse((100, 100, 412, 412), fill=255)
from PIL import ImageChops
w.paste((176, 112, 40, 255), (0, 0), ImageChops.multiply(grid, clip))
w.save(OUT + "stroopwafel.png")
print("wrote", ", ".join(["logo", "horse", "davi", "kira", "casino", "stroopwafel"]))
