#!/usr/bin/env python3
"""Writes a plain placeholder cover + preview for a game until its illustrated
cover is made. Run from the repo root:
    python3 tools/make_placeholder_cover.py <id> "<TITLE>" <hex colour>"""
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

gid, title, colour = sys.argv[1], sys.argv[2], sys.argv[3].lstrip("#")
c = tuple(int(colour[i:i + 2], 16) for i in (0, 2, 4))
W, H = 1672, 941

im = Image.new("RGB", (W, H))
d = ImageDraw.Draw(im)
for y in range(H):  # diagonal-ish gradient from the game colour to near black
    t = y / H
    d.line([(0, y), (W, y)], fill=tuple(int(c[i] * (1 - t * 0.75) + 12 * t) for i in range(3)))
glow = Image.new("L", (W, H), 0)
ImageDraw.Draw(glow).ellipse((W * 0.2, H * 0.1, W * 0.8, H * 0.9), fill=90)
im = Image.composite(Image.new("RGB", (W, H), (255, 255, 255)), im, glow.filter(ImageFilter.GaussianBlur(160)))
d = ImageDraw.Draw(im)
for x in range(-H, W, 70):  # faint stripes
    d.line([(x, H), (x + H, 0)], fill=tuple(min(255, v + 14) for v in c), width=18)

font = ImageFont.truetype("assets/fonts/NotoSans-Black.ttf", 150)
while d.textlength(title, font=font) > W * 0.86:
    font = ImageFont.truetype("assets/fonts/NotoSans-Black.ttf", font.size - 6)
small = ImageFont.truetype("assets/fonts/NotoSans-Bold.ttf", 44)
cx, cy = W / 2, H / 2 - 20
for dx, dy in ((8, 10), (0, 0)):
    d.text((cx + dx, cy + dy), title, font=font, anchor="mm", fill=(10, 6, 18) if dx else (255, 255, 255), stroke_width=10, stroke_fill=(10, 6, 18))
d.text((cx, cy + font.size * 0.75), "PIETERGAME 3  -  COVER COMING SOON", font=small, anchor="mm", fill=(255, 236, 200))

im.save(f"assets/images/covers/{gid}.png")
im.resize((640, 360), Image.LANCZOS).save(f"assets/images/preview/{gid}.png")
print("cover", gid)
