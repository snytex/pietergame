#!/usr/bin/env python3
"""Exhaustively computes the return-to-player of PieterGame 3's slot machine.
Keep these numbers in sync with src/game/scenes/SlotsScene.cpp."""
from itertools import product

WEIGHTS = [2, 2, 3, 4, 6, 7, 8]          # Pieter, Horse, Kira, Davi, Boba, Wafel, Coin
PAY3 = [500, 100, 50, 30, 20, 15, 10]    # three of a kind on the payline
PAY_2_PIETER, PAY_1_PIETER = 10, 1

n = sum(WEIGHTS)
rtp = 0.0
for a, b, c in product(range(len(WEIGHTS)), repeat=3):
    p = WEIGHTS[a] * WEIGHTS[b] * WEIGHTS[c] / n**3
    if a == b == c:
        win = PAY3[a]
    else:
        pieters = [a, b, c].count(0)
        win = PAY_2_PIETER if pieters == 2 else PAY_1_PIETER if pieters == 1 else 0
    rtp += p * win
print(f"RTP: {rtp:.4%}")
