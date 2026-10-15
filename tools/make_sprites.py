#!/usr/bin/env python3
"""Cuts circular character portraits out of the main menu artwork.
Run from the repo root: python3 tools/make_sprites.py"""
from PIL import Image, ImageDraw

SRC = "assets/images/menu_bg.png"
OUT = "assets/images/"
# name: (left, top, right, bottom) in the 1536x1024 menu artwork
CROPS = {
    "pieter": (118, 318, 338, 538),
    "horse":  (1035, 50, 1335, 400),
    "davi":   (1050, 425, 1270, 645),
    "kira":   (1295, 475, 1515, 695),
}
SIZE = 256

im = Image.open(SRC).convert("RGBA")
for name, box in CROPS.items():
    p = im.crop(box).resize((SIZE, SIZE), Image.LANCZOS)
    mask = Image.new("L", (SIZE * 4, SIZE * 4), 0)
    ImageDraw.Draw(mask).ellipse((4, 4, SIZE * 4 - 4, SIZE * 4 - 4), fill=255)
    p.putalpha(mask.resize((SIZE, SIZE), Image.LANCZOS))
    p.save(OUT + name + ".png")
    print("wrote", OUT + name + ".png")
