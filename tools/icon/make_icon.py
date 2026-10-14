#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""EHypView's icon, drawn as 32x32 1-bit pixel art: the same SM124-style
monochrome monitor as EMail's, ECal's and Claude ST's icons, whose dark
screen shows a hypertext: a page of text with an underlined link, and an
arrow from the link to the page it leads to. Adapted from EMail's
make_icon.py. Writes:
  src/gem/icon.h     the bitmap for EHYPVIEW (16-bit words, MSB first)
  src/gem/icon16.h   the 16-colour version and its palette
  docs/icon16.png    an enlarged preview of that
  docs/icon.png      an enlarged preview
"""
import os

W = H = 32
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
px = [[0] * W for _ in range(H)]


def put(x, y, v=1):
    if 0 <= x < W and 0 <= y < H:
        px[y][x] = v


def rect(x0, y0, x1, y1, v=1):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            put(x, y, v)


def hline(x0, x1, y, v=1):
    rect(x0, y, x1, y, v)


def vline(x, y0, y1, v=1):
    rect(x, y0, x, y1, v)


# --- monitor case: a rounded box with a thicker bottom bezel ---
hline(3, 28, 0)
hline(2, 29, 1)
vline(1, 2, 21)
vline(30, 2, 21)
put(2, 1); put(29, 1)
hline(2, 29, 22)
hline(3, 28, 23)
put(2, 21); put(29, 21)
# bezel shading on the right/bottom for a little depth
vline(29, 3, 21)
hline(3, 28, 21)

# --- screen: dark CRT with rounded corners ---
rect(4, 3, 27, 18)
for x, y in [(4, 3), (27, 3), (4, 18), (27, 18)]:
    put(x, y, 0)

# --- the hypertext on the screen, kept as roles so that both versions
# draw the same thing: "paper", "text", "link" (the underlined link and
# its arrow on paper) and "ray" (the arrow crossing the dark screen) ---
role = {}


def rrect(x0, y0, x1, y1, r):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            role[(x, y)] = r


# the page shown: a folded top-right corner, lines of text, a link
P0, P1, PY0, PY1 = 6, 13, 6, 16
rrect(P0, PY0, P1, PY1, "paper")
for (x, y) in [(P1, PY0), (P1 - 1, PY0), (P1, PY0 + 1)]:
    role.pop((x, y))                     # the dog-ear, cut away ...
role[(P1 - 1, PY0 + 1)] = "text"         # ... and its fold
rrect(P0 + 2, PY0 + 2, P1 - 3, PY0 + 2, "text")       # a heading
rrect(P0 + 2, PY0 + 4, P1 - 2, PY0 + 4, "text")
rrect(P0 + 2, PY0 + 6, P1 - 3, PY0 + 6, "link")       # the link ...
rrect(P0 + 2, PY0 + 7, P1 - 3, PY0 + 7, "link")       # ... underlined
rrect(P0 + 2, PY0 + 9, P1 - 4, PY0 + 9, "text")
LINK_Y = PY0 + 6

# the page it leads to, higher up on the right
Q0, Q1, QY0, QY1 = 18, 25, 5, 13
rrect(Q0, QY0, Q1, QY1, "paper")
rrect(Q0 + 2, QY0 + 2, Q1 - 2, QY0 + 2, "link")       # its title, as linked
rrect(Q0 + 2, QY0 + 4, Q1 - 2, QY0 + 4, "text")
rrect(Q0 + 2, QY0 + 6, Q1 - 3, QY0 + 6, "text")

# the arrow: from the link's end across the screen into the page
for x in range(P1 - 2, Q0):
    role[(x, LINK_Y)] = "link" if x <= P1 else "ray"
for (x, y) in [(Q0 - 2, LINK_Y - 1), (Q0 - 2, LINK_Y + 1), (Q0 - 3, LINK_Y - 2), (Q0 - 3, LINK_Y + 2)]:
    role[(x, y)] = "ray"                 # the arrowhead, its tip at the page

# in one colour: the papers white on the dark screen, text and the link
# dark on them, the arrow white across the screen
for (x, y), r in role.items():
    put(x, y, 0 if r in ("paper", "ray") else 1)

# --- power LED on the bezel, like the SM124's ---
rect(25, 21, 26, 21, 0)
put(25, 21, 1)

# --- neck and tilting foot ---
rect(13, 24, 18, 25)
hline(8, 23, 26)
hline(7, 24, 27)
vline(6, 28, 29); vline(25, 28, 29)
hline(7, 24, 28, 0)
hline(6, 25, 30)
hline(7, 24, 29)
put(7, 28); put(24, 28)
hline(8, 23, 31, 0)

# ---------------------------------------------------------------------------
# 16-colour version: the same shapes, coloured. Values are VDI pens: 0 is
# the background (not drawn), 1 black, 8..15 set from PALETTE while shown.
# ---------------------------------------------------------------------------
PALETTE = {                     # pen: (r, g, b) in 0..255
    8: (226, 216, 190),         # case: Atari putty beige
    9: (168, 156, 132),         # case shading
    10: (64, 60, 56),           # outline, and the text
    11: (16, 22, 42),           # CRT screen
    12: (246, 240, 222),        # the pages' paper
    13: (40, 96, 220),          # the link: hypertext blue
    14: (96, 232, 120),         # green phosphor: the arrow and the power LED
    15: (52, 66, 104),          # screen glare
}
col = [[0] * W for _ in range(H)]


def cput(x, y, v):
    if 0 <= x < W and 0 <= y < H:
        col[y][x] = v


def crect(x0, y0, x1, y1, v):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            cput(x, y, v)


# case: fill the outline's interior with beige, outline in dark grey
crect(2, 1, 29, 22, 8)
crect(3, 0, 28, 0, 8)
crect(3, 23, 28, 23, 8)
for y in range(H):
    for x in range(W):
        if px[y][x] and y <= 23:
            cput(x, y, 10)
# bevel shading: the double line on the right and the bottom bezel
for y in range(3, 22):
    cput(28, y, 9); cput(29, y, 9)
for x in range(3, 29):
    cput(x, 20, 9); cput(x, 21, 9)
for y in range(1, 23):
    cput(30, y, 10)
for x in range(3, 29):
    cput(x, 22, 10)
cput(29, 22, 10); cput(2, 22, 10)
# screen
for y in range(3, 19):
    for x in range(4, 28):
        if not (x in (4, 27) and y in (3, 18)):
            cput(x, y, 11)
# glare in the top-left corner of the CRT
for x, y in [(6, 4), (7, 4), (8, 4), (5, 5)]:
    cput(x, y, 15)
# the hypertext
PEN = {"paper": 12, "text": 10, "link": 13, "ray": 14}
for (x, y), r in role.items():
    cput(x, y, PEN[r])
# power LED
cput(26, 21, 14)
# neck and foot
for y in range(24, 31):
    for x in range(W):
        if px[y][x]:
            cput(x, y, 10)
crect(14, 24, 17, 25, 8)
crect(8, 28, 23, 28, 8)
crect(9, 27, 22, 27, 9)

if __name__ == "__main__":
    with open(os.path.join(ROOT, "src", "gem", "icon16.h"), "w") as f:
        f.write("/* EHypView icon, 32x32 in 16 colours (VDI pens, 2 per byte), from\n"
                " * tools/icon/make_icon.py. 0 = background, not drawn. */\n")
        f.write("static const unsigned char icon16_px[32 * 16] = {\n")
        for y in range(H):
            row = [(col[y][x] << 4) | col[y][x + 1] for x in range(0, W, 2)]
            f.write("\t" + ", ".join("0x%02x" % b for b in row) + ",\n")
        f.write("};\n/* pens 8..15 as VDI RGB (0..1000) */\n")
        f.write("static const short icon16_rgb[8][3] = {\n")
        for pen in range(8, 16):
            r, g, b = PALETTE[pen]
            f.write("\t{ %d, %d, %d },\n" % (r * 1000 // 255, g * 1000 // 255, b * 1000 // 255))
        f.write("};\n")

    words = []
    for y in range(H):
        for half in range(2):
            v = 0
            for b in range(16):
                v = (v << 1) | px[y][half * 16 + b]
            words.append(v)

    with open(os.path.join(ROOT, "src", "gem", "icon.h"), "w") as f:
        f.write("/* EHypView icon, 32x32 1-bit, generated by tools/icon/make_icon.py */\n")
        f.write("#define ICON_W 32\n#define ICON_H 32\n")
        f.write("static const unsigned short icon_bits[ICON_H * 2] = {\n")
        for y in range(H):
            f.write("\t0x%04x, 0x%04x,\n" % (words[2 * y], words[2 * y + 1]))
        f.write("};\n")

    try:
        from PIL import Image
        img = Image.new("RGB", (W, H), "white")
        for y in range(H):
            for x in range(W):
                if px[y][x]:
                    img.putpixel((x, y), (0, 0, 0))
        img.resize((W * 8, H * 8), Image.NEAREST).save(os.path.join(ROOT, "docs", "icon.png"))
        rgb = {0: (255, 255, 255), 1: (0, 0, 0), **PALETTE}
        img = Image.new("RGB", (W, H), "white")
        for y in range(H):
            for x in range(W):
                img.putpixel((x, y), rgb[col[y][x]])
        img.resize((W * 8, H * 8), Image.NEAREST).save(os.path.join(ROOT, "docs", "icon16.png"))
    except ImportError:
        pass

    for y in range(H):
        print("".join("#" if v else "." for v in px[y]))
