#!/usr/bin/env python3
"""Cuts Pieter's head (with ears, outline and neck) out of the menu artwork as a
transparent sprite for the Pieter Room. Run from the repo root:
python3 tools/make_cutout.py"""
import colorsys
import math

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SRC = "assets/images/menu_bg.png"
OUT = "assets/images/pieter_head.png"
CX, CY = 232, 448          # head centre in the 1536x1024 artwork
BOX = (82, 290, 382, 590)  # crop window around it (300x300)
RAYS = 720
NECK_Y = 575               # neck is cut off below this line

im = Image.open(SRC).convert("RGB")
px = np.asarray(im).astype(np.float32) / 255


def warm(x, y):
    r, g, b = px[y, x]
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    if s < 0.25 and v > 0.75 and abs(y - CY) < 40:  # eye whites count as face
        return True
    return h < 0.13 and s > 0.3 and v > 0.25  # skin + hair (the wall above him is mauve)


def dark(x, y):
    return px[y, x].max() < 0.2


radius = []
for i in range(RAYS):
    a = i / RAYS * 2 * math.pi
    dx, dy = math.cos(a), math.sin(a)
    last_warm, dark_run = 0, 0
    for r in range(20, 150):
        x, y = int(CX + dx * r), int(CY + dy * r)
        if warm(x, y):
            last_warm, dark_run = r, 0
        elif dark(x, y):
            dark_run += 1
            if dark_run > 12:
                break
        else:
            dark_run += 1  # eye whites etc. inside the face are fine
            if dark_run > 30:
                break
    # extend over the black outline (stops where it ends, at most ~7px)
    r = last_warm
    for k in range(1, 9):
        x, y = int(CX + dx * (last_warm + k)), int(CY + dy * (last_warm + k))
        if not dark(x, y):
            break
        r = last_warm + k
    if dy > 0:  # keep only a short neck stub; the room draws the collar over it
        r = min(r, (NECK_Y - CY) / dy)
    radius.append(r)

# smooth the profile: median then a light blur (removes stray hair/shading spikes)
rad = np.array(radius, dtype=np.float32)
med = np.array([np.median(np.take(rad, range(i - 20, i + 21), mode="wrap")) for i in range(RAYS)])
sm = np.array([np.mean(np.take(med, range(i - 3, i + 4), mode="wrap")) for i in range(RAYS)])
# the top of his head is convex: fill notches where dark hair stopped a ray early
for _ in range(40):
    for i in range(RAYS):
        a = i / RAYS * 2 * math.pi
        if math.sin(a) < -0.2 and math.cos(a) > -0.3:  # top + top-right (the ear makes the left side concave)
            chord = 0.5 * (sm[(i - 16) % RAYS] + sm[(i + 16) % RAYS]) - 0.5
            sm[i] = max(sm[i], chord)

SS = 4
W = BOX[2] - BOX[0]
mask = Image.new("L", (W * SS, W * SS), 0)
pts = []
for i in range(RAYS):
    a = i / RAYS * 2 * math.pi
    pts.append(((CX - BOX[0] + math.cos(a) * sm[i]) * SS, (CY - BOX[1] + math.sin(a) * sm[i]) * SS))
ImageDraw.Draw(mask).polygon(pts, fill=255)
mask = mask.resize((W, W), Image.LANCZOS).filter(ImageFilter.GaussianBlur(0.6))

head = im.crop(BOX).convert("RGBA")
head.putalpha(mask)
head = head.resize((512, 512), Image.LANCZOS)
head.save(OUT)
print("wrote", OUT, "profile min/max", sm.min(), sm.max())
