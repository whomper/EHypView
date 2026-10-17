#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Generate the EHypView test fixture sources.

Writes ST-Guide source files (*.stg, Atari character set) and GEM images
(*.img) into tests/fixtures/src.  They are compiled to *.hyp/*.ref by
tests/regen.sh using the hcp compiler from the HypView project.

Everything is deterministic so regenerating produces identical files.
"""
import os
import struct
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(__file__), "..", "fixtures", "src")

# Atari ST character set: the few non-ASCII characters the fixtures use
ATARI = {
    "ä": 0x84, "ö": 0x94, "ü": 0x81, "Ä": 0x8E, "Ö": 0x99, "Ü": 0x9A,
    "ß": 0x9E, "é": 0x82, "©": 0xBD, "α": 0xE0, "∞": 0xDF, "א": 0xC2,
}


def atari(text):
    out = bytearray()
    for ch in text:
        if ord(ch) < 128:
            out.append(ord(ch))
        else:
            out.append(ATARI[ch])
    return bytes(out)


def write_stg(name, text):
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(atari(text).replace(b"\n", b"\r\n"))


class Lcg:
    """Small deterministic PRNG (same results on every Python)."""

    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFF

    def next(self):
        self.state = (self.state * 1103515245 + 12345) & 0xFFFFFFFF
        return self.state >> 16


def write_img(name, width, height, planes, pixel):
    """Write a GEM IMG file using uncompressed bit-string records.

    pixel(x, y) returns the colour index (0 .. 2**planes - 1).
    """
    bpl = (width + 7) // 8
    data = bytearray()
    for y in range(height):
        rows = [bytearray(bpl) for _ in range(planes)]
        for x in range(width):
            c = pixel(x, y)
            for p in range(planes):
                if c & (1 << p):
                    rows[p][x >> 3] |= 0x80 >> (x & 7)
        for row in rows:
            pos = 0
            while pos < bpl:
                n = min(255, bpl - pos)
                data += bytes([0x80, n]) + row[pos:pos + n]
                pos += n
    header = struct.pack(">8H", 1, 8, planes, 2, 372, 372, width, height)
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(header + data)


BASIC = r"""@if VERSION >= 6
@os Atari
@charset atarist
@inputenc atarist
@endif
@database "EHypView basic test"
@subject "Test/EHypView"
@author "EHypView project"
@$VER: basic.hyp 1.0 (13.10.2026)
@help "Help"
@options "-a +z -s"
@width 70

@node "Main" "Basic test - main page"
@xref "Help" "The help page"
@xref "other.hyp/Main"
This is the main page.

Links: @{"to Second" link "Second"} and @{"line 3" alink "Second" 3}
Popup: @{"popup here" link "Pop"}
External: @{"other file" link "other.hyp/Start"}
@{B}bold@{b} @{I}italic@{i} @{U}under@{u} @{G}ghost@{g} @{O}outline@{o} @{S}shadow@{s}
Mixed: @{B}bold @{U}bold+under@{u}@{b} plain
Umlauts: äöüÄÖÜß é © α ∞ א
Default label: @{"Second" link "Second"}
@endnode

@node "Second"
@alias "Zweite"
@keywords "secondkw"
Line 0
Line 1
@label "here"
Line 3 target
@box 2 10 3 4
@line 2 10 2 1
@line 30 -8 0 3
@rbox 20 8 2 2
Back to @{"Main" link "Main"}
@endnode

@pnode "Pop"
I am a popup.
It links to @{"Second" link "Second"}.
@endnode

@node "Help"
Help text.
@{"Quit" quit}
@endnode
"""

OTHER = r"""@if VERSION >= 6
@os Atari
@charset atarist
@inputenc atarist
@endif
@database "EHypView other test"
@subject "Test/EHypView"
@default "Start"
@options "-a +z -s"

@node "Start"
@alias "Anfang"
Start of other.
Back: @{"basic second" link "basic.hyp/Second"}
@endnode

@node "Detail"
Detail page.
@endnode
"""

IMAGES = r"""@if VERSION >= 6
@os Atari
@charset atarist
@inputenc atarist
@endif
@database "EHypView image test"
@subject "Test/EHypView"
@options "-a +z -s"

@node "Main"
@image "small.img" 2
@limage "small.img" 0
Text beside a limage.
@image "big.img" 0
After the big image.
@endnode
"""

# pictures and text in colour: every standard pen, a picture with a
# dither mask for mono screens, and @{fg}/@{bg} (tests/run_colour.py)
COLOUR = r"""@if VERSION >= 6
@os Atari
@charset atarist
@inputenc atarist
@endif
@database "EHypView colour test"
@subject "Test/EHypView"
@options "-a +z -s"

@node "Main"
@limage "palette.img" 1
@limage "dither.img" 1 %0111100001111111
@{fg 2}Red@{fg 1} @{fg 3}Green@{fg 1} @{fg 4}Blue@{fg 1} @{fg 8}Light grey@{fg 1} plain
@{bg 6}Yellow behind@{bg 0} @{bg 1}@{fg 0}White on black@{fg 1}@{bg 0} plain
@endnode
"""

WORDS = ("atari falcon tt firebee gem aes vdi window menu dialog resource "
         "hypertext page link node index search reference compiler viewer "
         "accessory desktop font bitmap colour palette plane screen memory "
         "disk folder file path drive mint magic tos xaaes").split()


def big_text():
    rnd = Lcg(1992)
    lines = []
    for i in range(900):
        n = 4 + rnd.next() % 9
        words = [WORDS[rnd.next() % len(WORDS)] for _ in range(n)]
        lines.append("%04d %s" % (i, " ".join(words)))
    body = "\n".join(lines)
    return ('@if VERSION >= 6\n@os Atari\n@charset atarist\n@inputenc atarist\n@endif\n'
            '@database "EHypView big test"\n@options "+a +z -s"\n@width 100\n\n'
            '@node "Main"\nBig page with autoreferences to Index and Other.\n'
            + body + '\n@endnode\n\n@node "Other"\nOther page.\n@endnode\n')


def many_text():
    """one guide of a large library: 112 pages, each with a keyword, and an
    alias on every fifth (tests/run_quiet.py copies it 60 times: 6,720
    pages, 14,880 names)"""
    w = ("alpha beta gamma delta window menu dialog resource object tree string icon "
         "desk file path drive memory screen font colour").split()
    name = lambda p: "Main" if p == 0 else "%s_%s_%d" % (w[p % len(w)], w[(p * 7) % len(w)], p)
    out = ['@if VERSION >= 6', '@os Atari', '@charset atarist', '@inputenc atarist', '@endif',
           '@database "EHypView many pages"', '@subject "Test/Many"', '@options "+z -s"', '@width 75', '']
    for p in range(112):
        out += ['@node "%s"' % name(p), '@keywords "kw_%d"' % p]
        if p % 5 == 0:
            out.append('@alias "Alias %d"' % p)
        out += [" ".join(w[(p + k + j) % len(w)] for j in range(10)) for k in range(6)]
        if p + 1 < 112:
            out.append('See also @{"the next page" link "%s"}' % name(p + 1))
        out += ['@endnode', '']
    return "\n".join(out)


def main():
    os.makedirs(OUT, exist_ok=True)
    write_stg("many.stg", many_text())
    write_stg("basic.stg", BASIC)
    write_stg("other.stg", OTHER)
    write_stg("images.stg", IMAGES)
    write_stg("big.stg", big_text())
    write_stg("colour.stg", COLOUR)

    write_img("small.img", 32, 16, 4,
              lambda x, y: (x // 4 + y // 2) & 15)
    rnd = Lcg(1040)
    noise = [rnd.next() & 15 for _ in range(4096)]
    # 640x400, 16 colours = 128000 bytes: larger than 64 KB on purpose
    write_img("big.img", 640, 400, 4,
              lambda x, y: ((x // 16) ^ (y // 16)) & 15 if (x + y) % 7 else noise[(x * 31 + y) & 4095])

    # pens 0-15 in stripes of 16 pixels
    write_img("palette.img", 256, 32, 4, lambda x, y: x // 16)
    # above, pens 0-15 in columns of 8; below, white and black in a
    # checkerboard (two rows made one: grey)
    write_img("dither.img", 128, 32, 4,
              lambda x, y: x // 8 if y < 16 else 15 * ((x + y) & 1))


if __name__ == "__main__":
    main()
