#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Pictures and text in colour, in every screen mode, checked pixel by pixel.

    tests/run_colour.py HATARI_DIR EMUTOS_IMAGE OUT_DIR

tests/fixtures/colour.hyp holds a picture of the 16 standard pens in
stripes of 16 pixels (256 x 32), a picture with a dither mask for mono
screens (pens in columns of 8 above, a black and white checkerboard
below), and text in colours (@{fg n}, @{bg n}).  It is opened in Hatari
(EmuTOS) in each mode below and a screenshot taken; the viewer says where
it drew each picture ("EHV picture x,y wxh").  Then every pixel of the
pictures is checked against what the mode's palette should show:

  - 16 and 256 colours: the standard colour, or the nearest the palette
    has (the TT's has light colours where the Falcon's has dark ones);
  - 4 colours: the Bayer dither between the two pens nearest;
  - high and true colour: the standard colour (within the VDI's rounding);
  - mono: black or white by brightness, or by the dither mask;
  - pixels twice as tall as wide (ST medium, 640 x 200): half the height,
    two rows made one (the checkerboard becomes grey);

and the text below for its colours.  The program won't run in a screen
less than 640 pixels wide (the AERA guidelines, 5.2), so TT low and the
Falcon's true colour on VGA (320 x 480) are the accessory's, asked for
the page by ASKHELP.PRG (tests/askhelp.c).  Screenshots stay in OUT_DIR.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import time

HDIR, EMUTOS, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = os.path.join(HERE, "fixtures", "colour.hyp")
PAGE = r"page C:\COLOUR.HYP Main"
MASK = 0xFF16          # colour.stg's %0111100001111111, by pen
failed = 0

# the standard pens (src/gem/colour.c), and the order VDI colours take them
STD = [0xFFFFFF, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xBABABA,
       0x8A8A8A, 0xAA0000, 0x00AA00, 0xAAAA00, 0x0000AA, 0xAA00AA, 0x00AAAA, 0x000000]
VDI_PEN = [0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13]
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def ring():
    """pens 16-254 of 256 colours (16 greys, four rings of hues)"""
    out = [0xFFFFFF - 0x111111 * i for i in range(16)]
    for n in (15, 11, 7, 4):
        l = n * 0x11
        for i in range(6 * n):
            s = (i % n) * 0x11
            d = l - s
            out.append([(l, 0, s), (d, 0, l), (0, s, l), (0, l, d), (s, l, 0), (l, d, 0)][i // n])
    out = [c if isinstance(c, int) else c[0] << 16 | c[1] << 8 | c[2] for c in out]
    return out + [0xFFFFFF]


# each mode's palette by pen, as EmuTOS sets it (the TT's light colours,
# 4 bits a gun), or None: true colour, or mono
FOUR = [0xFFFFFF, 0xFF0000, 0x00FF00, 0x000000]
# the ST's shifter has 3 bits a gun: the same pens, shown 0xEE at full
ST4 = (FOUR, [0xEEEEEE, 0xEE0000, 0x00EE00, 0x000000])
TT16 = [0xFFFFFF, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xAAAAAA,
        0x666666, 0xFF9999, 0x99FF99, 0xFFFF99, 0x9999FF, 0xFF99FF, 0x99FFFF, 0x000000]

FALCON = ["--machine", "falcon", "--monitor", "vga", "--memsize", "14"]
TT = ["--machine", "tt", "--monitor", "vga", "--memsize", "14"]
ST = ["--machine", "st"]
# name, Hatari's options, the Falcon's video mode (NVRAM), palette (or
# the palette the VDI gives and the colours the screen shows for it),
# the program or the accessory
MODES = [
    ("falcon-4", FALCON, 0x0019, FOUR, "prg"),
    ("falcon-16", FALCON, 0x001A, STD, "prg"),
    ("falcon-256", FALCON, 0x001B, STD + ring()[:239] + [0x000000], "prg"),
    ("falcon-stmedium", FALCON, 0x0099, FOUR, "prg"),
    ("falcon-truecolour", ["--machine", "falcon", "--monitor", "rgb", "--memsize", "14"], 0x002C, None, "prg"),
    ("falcon-vga-truecolour", FALCON, 0x0014, None, "acc"),
    ("tt-medium", TT + ["--tos-res", "ttmed"], None, TT16, "prg"),
    ("tt-low", TT + ["--tos-res", "ttlow"], None, TT16 + ring()[:239] + [0x000000], "acc"),
    ("tt-high", ["--machine", "tt", "--monitor", "mono", "--memsize", "14"], None, None, "prg"),
    # the 68000: an ST of 1 MB in high and medium, and the accessory in 4 MB
    ("st-high-1mb", ST + ["--monitor", "mono", "--memsize", "1"], None, None, "prg"),
    ("st-medium-1mb", ST + ["--monitor", "rgb", "--tos-res", "med", "--memsize", "1"], None, ST4, "prg"),
    ("st-medium-4mb", ST + ["--monitor", "rgb", "--tos-res", "med", "--memsize", "4"], None, ST4, "acc"),
]

# Hatari's NVRAM (bytes 14-63) as it writes it, with the Falcon's video
# mode in bytes 28-29 and the checksum in 62-63
NVRAM = bytes([0, 0, 0, 0, 0, 0, 0, 0, 0x11, 0x2E, 0x20, 0x01, 0xFF, 0, 0, 0x1A, 0x87] + [0] * 33)


def check(ok, what):
    global failed
    print(("ok    " if ok else "FAIL  ") + what)
    failed += not ok
    return ok


def rgb(v):
    return (v >> 16) & 255, (v >> 8) & 255, v & 255


def dist(a, b):
    a, b = rgb(a), rgb(b)
    return 2 * (a[0] - b[0]) ** 2 + 4 * (a[1] - b[1]) ** 2 + 3 * (a[2] - b[2]) ** 2


def pick(pal, c, dither):
    """colour_pick(): (a, b, t), as src/gem/colour.c works it out"""
    best, a = None, 0
    for i, p in enumerate(pal):
        d = dist(p, c)
        if best is None or d < best:
            best, a = d, i
    if not dither or best == 0 or len(pal) > 16:
        return a, a, 0
    out = (a, a, 0)
    W = (2, 4, 3)
    cc = rgb(c)
    for i, pi in enumerate(pal):
        for j, pj in enumerate(pal):
            if i == j:
                continue
            ai, aj = rgb(pi), rgb(pj)
            e = [aj[k] - ai[k] for k in range(3)]
            ln = sum(W[k] * e[k] * e[k] for k in range(3))
            if ln == 0:
                continue
            dot = sum(W[k] * (cc[k] - ai[k]) * e[k] for k in range(3))
            if dot <= 0 or dot >= ln:
                continue
            t = (dot * 16 + ln // 2) // ln
            if t <= 0 or t >= 16 or t > 8:
                continue
            p = [ai[k] + int(e[k] * t / 16) - cc[k] for k in range(3)]
            d = sum(W[k] * p[k] * p[k] for k in range(3))
            if d < best:
                best, out = d, (i, j, t)
    return out


def text_pen(pal, c, bg):
    """colour_text(): the pen of pal for text (or behind it) of colour c"""
    white = lambda v: v == 0xFFFFFF
    grey = lambda v: rgb(v)[0] == rgb(v)[1] == rgb(v)[2]
    if bg and white(c):
        return None
    for only_grey in ((True, False) if grey(c) and not white(c) else (False,)):
        best = None
        for i, p in enumerate(pal):
            if not bg and not white(c) and white(p):
                continue
            if only_grey and not grey(p):
                continue
            if best is None or dist(p, c) < dist(pal[best], c):
                best = i
        if best is not None:
            break
    return None if bg and white(pal[best]) else best


def dark(c):
    r, g, b = rgb(c)
    return 299 * r + 587 * g + 114 * b < 128000


def read_bmp(path):
    """(width, height, pixel(x, y) -> 0xRRGGBB) of an uncompressed BMP"""
    data = open(path, "rb").read()
    off = struct.unpack_from("<I", data, 10)[0]
    w, h, _, bpp = struct.unpack_from("<iiHH", data, 18)
    step = bpp // 8
    stride = (w * step + 3) & ~3
    top_down = h < 0
    h = abs(h)

    def px(x, y):
        row = y if top_down else h - 1 - y
        i = off + row * stride + x * step
        return data[i + 2] << 16 | data[i + 1] << 8 | data[i]
    return w, h, px


def make_drive(d, how, hyp):
    if os.path.exists(d):
        shutil.rmtree(d)
    os.makedirs(d)
    name = os.path.basename(hyp).upper()
    shutil.copy(hyp, os.path.join(d, name))
    if how == "prg":
        shutil.copy(os.path.join(HDIR, "EHYPVIEW.PRG"), d)
        with open(os.path.join(d, "ST-GUIDE.INF"), "w") as f:
            f.write("DEFHYP=C:\\%s\r\n" % name)
    else:
        shutil.copy(os.path.join(HDIR, "EHYPVIEW.ACC"), d)
        shutil.copy(os.path.join(HDIR, "ASKHELP.PRG"), d)
        with open(os.path.join(d, "ASKHELP.TXT"), "w") as f:
            f.write("C:\\%s Main\r\n" % name)
    return r"page C:\%s Main" % name


def boot(name, opts, vmode, how, hyp=FIXTURE, npics=2):
    """the console's lines and the screenshot's path (None: no page)"""
    work = os.path.join(OUT, name)
    drive = os.path.join(work, "c")
    page = make_drive(drive, how, hyp)
    home = os.path.join(work, "home")
    os.makedirs(os.path.join(home, ".config", "hatari"), exist_ok=True)
    nvram = bytearray(NVRAM)
    if vmode is not None:
        nvram[14], nvram[15] = vmode >> 8, vmode & 255
    s = sum(nvram[:48]) & 255
    nvram[48], nvram[49] = ~s & 255, s
    with open(os.path.join(home, ".config", "hatari", "hatari.nvram"), "wb") as f:
        f.write(nvram)
    fifo = os.path.join(work, "fifo")
    log_path = os.path.join(work, "console.log")
    shot = os.path.join(work, "screen.bmp")
    for p in (fifo, shot):
        if os.path.exists(p):
            os.remove(p)
    env = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    args = ["hatari", "--tos", EMUTOS, "--natfeats", "yes", "--sound", "off",
            "--confirm-quit", "no", "--statusbar", "no", "--log-level", "fatal", "--borders", "no",
            "--harddrive", drive, "--gemdos-drive", "C", "--cmd-fifo", fifo, "--fast-forward", "yes",
            "--auto", r"C:\EHYPVIEW.PRG" if how == "prg" else r"C:\ASKHELP.PRG"] + opts
    h = subprocess.Popen(args, env=env, stdout=open(log_path, "w"), stderr=subprocess.STDOUT)

    def lines():
        with open(log_path, errors="replace") as f:
            return [l.strip() for l in f]
    try:
        end = time.time() + 120
        while time.time() < end:
            evs = [l[4:] for l in lines() if l.startswith("EHV ")]
            if page in evs and len([e for e in evs if e.startswith("picture ")]) >= npics:
                break
            if h.poll() is not None:
                break
            time.sleep(0.5)
        time.sleep(2)
        if os.path.exists(fifo):
            with open(fifo, "w") as ctl:
                ctl.write("hatari-debug screenshot %s\n" % os.path.abspath(shot))
                ctl.flush()
                for _ in range(40):
                    time.sleep(0.25)
                    if os.path.exists(shot) and os.path.getsize(shot) > 0:
                        break
                time.sleep(0.5)
                ctl.write("hatari-debug quit\n")
        h.wait(15)
    except subprocess.TimeoutExpired:
        h.kill()
    finally:
        if h.poll() is None:
            h.kill()
    return lines(), shot if os.path.exists(shot) else None


def run_mode(name, opts, vmode, pal, how):
    shown = pal
    if isinstance(pal, tuple):
        pal, shown = pal
    out, shot = boot(name, opts, vmode, how)
    evs = [l[4:] for l in out if l.startswith("EHV ")]
    scr = next((e.split()[1:] for e in evs if e.startswith("screen ")), None)
    pics = [e.split()[1:] for e in evs if e.startswith("picture ")]
    if not check(scr is not None and PAGE in evs and len(pics) >= 2 and shot is not None,
                 "%s: the page and its pictures shown (%s)" % (name, " ".join(scr or ["no screen"]))):
        return
    W, H = map(int, scr[0].split("x"))
    planes, tall = int(scr[1]), "tall" in scr
    sw, sh, px = read_bmp(shot)
    sx, sy = sw // W, sh // H

    def at(x, y):
        return px(x * sx, y * sy)

    (pos1, size1), (pos2, size2) = pics[-2], pics[-1]
    x1, y1 = map(int, pos1.split(","))
    x2, y2 = map(int, pos2.split(","))
    h1 = int(size1.split("x")[1])
    h2 = int(size2.split("x")[1])
    check(h1 == (16 if tall else 32) and h2 == h1,
          "%s: the pictures %d rows high%s" % (name, h1, " (pixels twice as tall: two rows made one)" if tall else ""))

    # what each picture pixel should be: (expected colour, tolerance), or
    # a list of the colours that may be there
    def expect(key_pens, ix, iy):
        """key_pens: the image's pen(s) at this output pixel"""
        if planes == 1:
            blk = [MASK >> p & 1 if masked else dark(STD[p]) for p in key_pens]
            if len(set(blk)) > 1:
                return (0x000000 if 8 > BAYER[iy & 3][ix & 3] else 0xFFFFFF), 0
            return (0x000000 if blk[0] else 0xFFFFFF), 0
        c = STD[key_pens[0]]
        if len(key_pens) == 2 and key_pens[0] != key_pens[1]:
            a, b = rgb(STD[key_pens[0]]), rgb(STD[key_pens[1]])
            c = sum(((a[k] + b[k] + 1) // 2) << (16 - 8 * k) for k in range(3))
        if pal is None:
            return c, 8                                  # true colour
        a, b, t = pick(pal, c, len(pal) < 16)
        return shown[b if t > BAYER[iy & 3][ix & 3] else a], 0

    def pens_at(picture, ix, iy):
        """the image's pens behind output pixel (ix, iy)"""
        rows = [2 * iy, 2 * iy + 1] if tall else [iy]
        if picture == 1:
            return [ix // 16 for _ in rows]
        return [ix // 8 if r < 16 else 15 * ((ix + r) & 1) for r in rows]

    for picture, x0, y0, w, oh in ((1, x1, y1, 256, h1), (2, x2, y2, 128, h2)):
        masked = picture == 2
        bad = []
        for iy in range(oh):
            for ix in range(w):
                want, tol = expect(pens_at(picture, ix, iy), ix, iy)
                got = at(x0 + ix, y0 + iy)
                if any(abs(g - e) > tol for g, e in zip(rgb(got), rgb(want))):
                    bad.append((ix, iy, got, want))
        what = "the 16 pens" if picture == 1 else "the dither-mask picture"
        check(not bad, "%s: %s, every pixel as the palette shows it%s" % (
            name, what, "" if not bad else ": %d wrong, first at %d,%d %06X (not %06X)" % ((len(bad),) + bad[0])))

    # the text below: its colours there
    ch = 16 if H >= 400 else 8
    band = set()
    for y in range(y2 + h2, min(H, y2 + h2 + 4 * ch)):
        for x in range(x2, min(W, x2 + 40 * 8)):
            band.add(at(x, y))

    def near(c, tol):
        return any(all(abs(g - e) <= tol for g, e in zip(rgb(b), rgb(c))) for b in band)

    if planes == 1:
        want = {"black text": 0x000000}
        check(band <= {0x000000, 0xFFFFFF}, "%s: the text black and white only" % name)
    else:
        p = pal if pal is not None else [STD[VDI_PEN[n]] for n in range(16)]
        want = {}
        for label, n, bg in (("red", 2, 0), ("green", 3, 0), ("blue", 4, 0), ("light grey", 8, 0),
                             ("yellow behind", 6, 1), ("black behind", 1, 1)):
            i = text_pen(p, STD[VDI_PEN[n]], bg)
            want[label] = (shown if pal is not None else p)[i]
    tol = 8 if pal is None and planes > 1 else 0
    missing = [l for l, c in want.items() if not near(c, tol)]
    check(not missing, "%s: the text's colours (%s)%s" % (
        name, ", ".join("%s %06X" % (l, c) for l, c in want.items()), "" if not missing else ": missing " + ", ".join(missing)))


# the family's help front pages (guidelines 4.3: a banner and a window
# picture), from each program's latest release (guidelines 1.2), on the
# smallest machines: they must show both pictures
APPS = [("EMail", "EMAIL.HYP"), ("EContact", "ECONTACT.HYP"), ("EFTP", "EFTP.HYP"), ("ECal", "ECAL.HYP")]
SMALL = [
    ("st-1mb", ST + ["--monitor", "mono", "--memsize", "1"], None, "prg"),
    ("falcon-1mb", ["--machine", "falcon", "--monitor", "vga", "--memsize", "1"], 0x001A, "prg"),
    ("falcon-1mb-acc", ["--machine", "falcon", "--monitor", "vga", "--memsize", "1"], 0x001A, "acc"),
]


def front_pages():
    helps = os.path.join(OUT, "help")
    os.makedirs(helps, exist_ok=True)
    tool = os.path.join(HERE, "..", "common", "tools", "getrelease.py")
    for app, hyp in APPS:
        path = os.path.join(helps, hyp)
        got = subprocess.run([sys.executable, tool, app, helps, hyp], capture_output=True, text=True)
        if got.returncode != 0 or not os.path.exists(path):
            check(False, "%s's help: fetched from its release (%s)" % (app, (got.stderr or got.stdout).strip()[-200:]))
            continue
        with open(os.path.join(helps, app.upper() + ".VERSION")) as f:
            version = f.read().strip()
        for name, opts, vmode, how in SMALL:
            out, shot = boot("%s-%s" % (app.lower(), name), opts, vmode, how, path)
            evs = [l[4:] for l in out if l.startswith("EHV ")]
            pics = [e.split()[1:] for e in evs if e.startswith("picture ")]
            shown = any(e.startswith("pictures 2 ") for e in evs)
            blank = []
            if shown and shot is not None and len(pics) >= 2:
                scr = next(e.split()[1:] for e in evs if e.startswith("screen "))
                W, H = map(int, scr[0].split("x"))
                sw, sh, px = read_bmp(shot)
                for pos, size in pics[-2:]:
                    x0, y0 = map(int, pos.split(","))
                    w, h = map(int, size.split("x"))
                    seen = set(px(min(sw - 1, (x0 + x) * sw // W), min(sh - 1, (y0 + y) * sh // H))
                               for y in range(0, h, 3) for x in range(0, w, 3))
                    if len(seen) < 2:
                        blank.append(size)
            check(shown and not blank and shot is not None,
                  "%s %s's front page on %s (%s): both pictures%s" % (
                      app, version, name, how, "" if shown and not blank else
                      (": blank " + ", ".join(blank) if blank else ": not loaded")))


def main():
    if not shutil.which("hatari"):
        print("skip  colour: needs hatari")
        return 0
    os.makedirs(OUT, exist_ok=True)
    only = os.environ.get("MODES")
    for m in MODES:
        if only and m[0] not in only.split(","):
            continue
        run_mode(*m)
    if not only or "front" in only.split(","):
        front_pages()
    print("%d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
