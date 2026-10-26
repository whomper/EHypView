#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The speed rig (the AERA guidelines, "Speed: the stock machines set it").

    tests/hatari/speed.py BUILD_DIR OUT_DIR

BUILD_DIR holds EHYPVIEW.PRG and EHYPVIEW.ACC built with -DAES_PROFILE=20
(make speed-test), LAUNCH.PRG and ASKHELP.PRG: every stretch of 20 ms or
more between event calls goes to the emulator's console as "AESGAP <ms>
ms <tag>".  Each action is sent on its own, and its time is the longest
stretch from it to the program's next event calls (the stretches of the
background index, "idx-*", are left out).

On each machine in SPEED_MACHINES (default ST:mono, ST:med, Falcon-4:16;
"Falcon-4:16+nvdi" under Erez's NVDI, which machines.py installs):

  page     EHYPVIEW.PRG started with BIG.HYP (a page of 900 lines): the
           start; Down, Shift+Down (a line, a page); Tab (a link chosen);
           Settings (^,) opened and closed with Esc; the library (^L)
           drawn whole; letters typed into its search; a full-text search
           (Return); the library closed (^W): the page uncovered
  images   IMAGES.HYP (a 640 x 400 16-colour picture), and on a Falcon in
           256 colours COLOUR.HYP: the page drawn, a line and a page
           scrolled
  front    ECal's guide from its release (the family's front page, 4.3)
  acc      EHYPVIEW.ACC opened from the Desk menu (Xvfb and xdotool)
  help     the accessory asked for ECal's front page as a Help key asks
           (ASKHELP.PRG, VA_START)

It prints a table: each action's longest stretch on each machine, marked
"over" where it is over the guidelines' time, and fails when one is.
SPEED_ONLY=page,acc runs only those.
"""
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
import machines  # noqa: E402

BDIR, OUT = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
FIX = os.path.join(ROOT, "tests", "fixtures")
DISPLAY = ":97"
SPECS = os.environ.get("SPEED_MACHINES", "ST:mono,ST:med,Falcon-4:16").split(",")
ONLY = os.environ.get("SPEED_ONLY")
GUIDES = 60

ESC, TAB, RETURN, DOWN, CTRL, SHIFT = 1, 15, 28, 0x50, 29, 0x2A
COMMA, L, W, Q = 0x33, 0x26, 0x11, 0x10

# the guidelines' times, ms
KEY, LINE, PAGE, DIALOG, WHOLE, ACC, START = 50, 50, 150, 150, 300, 500, 2000
SLICE = 100                     # a longer job's slices: a key answered within

results = {}                    # (action, limit) -> {machine: ms}
order = []


def note(action, limit, machine, ms):
    if (action, limit) not in results:
        results[(action, limit)] = {}
        order.append((action, limit))
    old = results[(action, limit)].get(machine)
    results[(action, limit)][machine] = ms if old is None or ms is None else max(old, ms)


def gaps(lines):
    """the AESGAP lines of the user's work: (ms, tag)"""
    out = []
    for l in lines:
        m = re.match(r"AESGAP (\d+) ms (\S+)", l)
        if m and not m.group(2).startswith("idx-"):
            out.append((int(m.group(1)), m.group(2)))
    return out


def longest(lines, tags=None):
    """the longest stretch (of those tagged so, when tags are given)"""
    return max([g for g, t in gaps(lines) if tags is None or t in tags] or [0])


def ecal_guide():
    """ECal's guide from its latest release (guidelines 1.2): the family's front page"""
    d = os.path.join(OUT, "ecal")
    path = os.path.join(d, "ECAL.HYP")
    if not os.path.exists(path):
        os.makedirs(d, exist_ok=True)
        subprocess.run([sys.executable, os.path.join(ROOT, "common", "tools", "getrelease.py"),
                        "ECal", d, "ECAL.HYP"], check=True, stdout=subprocess.DEVNULL)
    return path


def make_drive(d, nvdi, start=None, acc=False, ask=None):
    if os.path.exists(d):
        shutil.rmtree(d)
    fixt = os.path.join(d, "FIXT")
    os.makedirs(os.path.join(fixt, "IMAGES"))
    os.makedirs(os.path.join(fixt, "MANY"))
    shutil.copy(os.path.join(FIX, "big.hyp"), os.path.join(fixt, "BIG.HYP"))
    shutil.copy(os.path.join(FIX, "big.ref"), os.path.join(fixt, "BIG.REF"))
    shutil.copy(os.path.join(FIX, "colour.hyp"), os.path.join(fixt, "COLOUR.HYP"))
    shutil.copy(os.path.join(FIX, "images.hyp"), os.path.join(fixt, "IMAGES", "IMAGES.HYP"))
    shutil.copy(ecal_guide(), os.path.join(fixt, "ECAL.HYP"))
    for i in range(GUIDES):
        for ext in ("hyp", "ref"):
            shutil.copy(os.path.join(FIX, "many", "many." + ext),
                        os.path.join(fixt, "MANY", "G%03d.%s" % (i, ext.upper())))
    with open(os.path.join(d, "ST-GUIDE.INF"), "w", newline="") as f:
        f.write("PATHS=C:\\FIXT\r\n")
    if acc:
        shutil.copy(os.path.join(BDIR, "EHYPVIEW.ACC"), d)
    else:
        shutil.copy(os.path.join(BDIR, "EHYPVIEW.PRG"), d)
    if start is not None:
        shutil.copy(os.path.join(BDIR, "LAUNCH.PRG"), d)
        with open(os.path.join(d, "LAUNCH.TXT"), "w", newline="") as f:
            f.write("C:\\EHYPVIEW.PRG\r\n%s\r\n" % start)
    if ask is not None:
        shutil.copy(os.path.join(BDIR, "ASKHELP.PRG"), d)
        with open(os.path.join(d, "ASKHELP.TXT"), "w", newline="") as f:
            f.write(ask + "\r\n")
    m = machines.Machine(SPEC_NAME, d)
    if nvdi and not m.nvdi():
        sys.exit("speed.py: no NVDI here (machines.py, AERA_NVDI)")
    return m


class Mouse:
    """xdotool on the Xvfb display, in Atari screen coordinates (Hatari
    shows ST medium with its lines doubled: scaled by the window's size)"""

    def __init__(self, sw, sh):
        self.env = dict(os.environ, DISPLAY=DISPLAY)
        self.sw, self.sh, self.geo = sw, sh, None

    def xdo(self, *args):
        return subprocess.run(["xdotool", *args], env=self.env, capture_output=True, text=True).stdout

    def window(self):
        for _ in range(100):
            if self.geo:
                return self.geo
            found = (self.xdo("search", "--name", "Hatari") or self.xdo("search", "--class", "hatari")).split()
            if found:
                g = dict(l.split("=") for l in self.xdo("getwindowgeometry", "--shell", found[0]).split())
                self.geo = tuple(int(g[k]) for k in ("X", "Y", "WIDTH", "HEIGHT"))
            else:
                time.sleep(0.2)
        raise RuntimeError("no Hatari window")

    def click(self, x, y):
        wx, wy, ww, wh = self.window()
        x, y = wx + x * ww // self.sw, wy + y * wh // self.sh
        loc = dict(l.split("=") for l in self.xdo("getmouselocation", "--shell").split())
        x0, y0 = int(loc["X"]), int(loc["Y"])
        for i in range(1, 13):
            self.xdo("mousemove", str(x0 + (x - x0) * i // 12), str(y0 + (y - y0) * i // 12))
            time.sleep(0.03)
        time.sleep(0.4)
        self.xdo("click", "1")
        time.sleep(0.4)


class Run:
    """one boot of Hatari: keys through its command FIFO, the console read"""

    def __init__(self, name, m, auto=None, windowed=False):
        self.dir = os.path.join(OUT, name)
        os.makedirs(self.dir, exist_ok=True)
        self.log = os.path.join(self.dir, "console.log")
        self.fifo = os.path.join(self.dir, "fifo")
        if os.path.exists(self.fifo):
            os.remove(self.fifo)
        env = m.env()
        args = ["hatari"] + m.args() + ["--natfeats", "yes", "--statusbar", "no", "--log-level", "fatal",
                                        "--cmd-fifo", self.fifo, "--borders", "no"]
        if windowed:                    # the mouse: real speed, in a window on Xvfb
            env["DISPLAY"] = DISPLAY
            env.pop("SDL_VIDEODRIVER", None)
            args += ["-w", "--zoom", "1", "--fast-forward", "no"]
        else:                           # the times are the emulated machine's own
            args += ["--fast-forward", "yes"]
        if auto:
            args += ["--auto", auto]
        self.h = subprocess.Popen(args, env=env, stdout=open(self.log, "w"), stderr=subprocess.STDOUT)
        for _ in range(100):
            if os.path.exists(self.fifo):
                break
            time.sleep(0.1)
        self.ctl = open(self.fifo, "w")
        self.mark = 0

    def lines(self):
        with open(self.log, errors="replace") as f:
            return [l.strip() for l in f]

    def events(self, since=0):
        return [l[4:] for l in self.lines()[since:] if l.startswith("EHV ")]

    def send(self, cmd):
        self.ctl.write(cmd + "\n")
        self.ctl.flush()
        time.sleep(0.2)

    def key(self, k, mods=()):
        for m in mods:
            self.send("hatari-event keydown 0x%x" % m)
        self.send("hatari-event keypress %s" % (k if isinstance(k, str) else "0x%x" % k))
        for m in reversed(mods):
            self.send("hatari-event keyup 0x%x" % m)

    def wait(self, want, timeout=60, since=None):
        """till an event starting with want comes; True if it did"""
        since = self.mark if since is None else since
        end = time.time() + timeout
        if want == "index-done":        # "index N guides"
            hit = lambda e: re.match(r"index \d+ guides", e)
        else:
            hit = lambda e: e.startswith(want)
        while time.time() < end:
            if any(hit(e) for e in self.events(since)):
                return True
            if self.h.poll() is not None:
                return False
            time.sleep(0.2)
        return False

    def act(self, do, want=None, settle=1.5, timeout=60, tags=None):
        """an action: the longest stretch it caused (None: what it waits
        for never came); with tags, of those stretches only"""
        self.mark = len(self.lines())
        do()
        ok = want is None or self.wait(want, timeout)
        time.sleep(settle)
        return longest(self.lines()[self.mark:], tags) if ok else None

    def since(self, tags):
        """the longest stretch so tagged since the last action began"""
        return longest(self.lines()[self.mark:], tags)

    def stop(self):
        try:
            self.send("hatari-debug quit")
            self.h.wait(10)
        except (subprocess.TimeoutExpired, BrokenPipeError):
            pass
        if self.h.poll() is None:
            self.h.kill()


def screen(run):
    """the screen's size from the program's "screen WxH depth" line"""
    for e in run.events(0):
        if e.startswith("screen "):
            w, h = e.split()[1].split("x")
            return int(w), int(h)
    return 640, 400


def start_times(run, page):
    """the start: the first event call, and the longest stretch till the page was drawn"""
    ok = run.wait("page " + page, 120, 0)
    time.sleep(2)
    evs = run.events(0)
    first = next((int(e.split()[1]) for e in evs if e.startswith("started ")), None)
    return first, longest(run.lines()) if ok else None


def whole(run, spec, what):
    """the page drawn whole: Shift+Clr/Home to its end, Clr/Home to its top"""
    for mods in ([SHIFT], []):
        note(what + ": drawn whole (Shift+Clr/Home, Clr/Home)", WHOLE, spec,
             run.act(lambda: run.key(0x47, mods), settle=1.0))


def scrolls(run, spec, what):
    for _ in range(3):
        note(what + ": a line scrolled (Down)", LINE, spec, run.act(lambda: run.key(DOWN), settle=1.0))
    for _ in range(3):
        note(what + ": a page scrolled (Shift+Down)", PAGE, spec,
             run.act(lambda: run.key(DOWN, [SHIFT]), settle=1.0))


def page_case(spec, nvdi):
    d = os.path.join(OUT, slug(spec), "page")
    page = "C:\\FIXT\\BIG.HYP Main"
    m = make_drive(os.path.join(d, "c"), nvdi, start="C:\\FIXT\\BIG.HYP")
    run = Run(os.path.join(slug(spec), "page"), m, r"C:\LAUNCH.PRG")
    try:
        first, drawn = start_times(run, page)
        note("program started: first event call", START, spec, first)
        note("program started: its window drawn (longest stretch)", START, spec, drawn)
        run.wait("index-done", 600, 0)                  # the background index done
        time.sleep(1)
        whole(run, spec, "big page")
        scrolls(run, spec, "big page")
        note("a key: Tab chooses a link", KEY, spec, run.act(lambda: run.key(TAB)))
        note("Settings opened (^,)", DIALOG, spec,
             run.act(lambda: run.key(COMMA, [CTRL]), "dialog settings"))
        note("Settings closed (Esc): the page uncovered", WHOLE, spec,
             run.act(lambda: run.key(ESC), "dialog-closed settings"))
        note("library opened (^L): drawn whole", WHOLE, spec,
             run.act(lambda: run.key(L, [CTRL]), "library"))
        # the key's own turn; the rows it found drawn in the next
        # (lib-rows), the names searched through in slices (lib-work)
        for ch in "gen":
            note("a letter typed in the library's search", KEY, spec,
                 run.act(lambda: run.key(ch), "search", settle=2.0, tags=("event",)))
            note("search: the rows found drawn", PAGE, spec, run.since(("lib-rows",)))
            note("search: its slices through the names", SLICE, spec, run.since(("lib-work",)))
        # its slices for 30 s (the whole search takes minutes on an ST);
        # what it finds drawn in turns of their own (lib-rows)
        note("full-text search (Return): the key", KEY, spec,
             run.act(lambda: run.key(RETURN), "fulltext-start", settle=30.0, tags=("event",)))
        note("full-text search: its slices", SLICE, spec, run.since(("lib-work",)))
        note("full-text search: what it found drawn", PAGE, spec, run.since(("lib-rows",)))
        note("library closed (^W): the page uncovered", WHOLE, spec,
             run.act(lambda: run.key(W, [CTRL]), "closed library"))
    finally:
        run.stop()


def picture_case(spec, nvdi, name, hyp, what):
    d = os.path.join(OUT, slug(spec), name)
    m = make_drive(os.path.join(d, "c"), nvdi, start=hyp)
    run = Run(os.path.join(slug(spec), name), m, r"C:\LAUNCH.PRG")
    try:
        ok = run.wait("page " + hyp, 120, 0)
        run.wait("pictures", 30, 0)
        time.sleep(2)
        note(what + ": page drawn at the start (longest stretch)", WHOLE, spec,
             longest(run.lines()) if ok else None)
        scrolls(run, spec, what)
        whole(run, spec, what)
    finally:
        run.stop()


def acc_case(spec, nvdi):
    d = os.path.join(OUT, slug(spec), "acc")
    m = make_drive(os.path.join(d, "c"), nvdi, acc=True)
    run = Run(os.path.join(slug(spec), "acc"), m, windowed=True)
    try:
        run.wait("acc registered", 120, 0)
        time.sleep(10)                  # the desktop drawn, the accessory idle
        sw, sh = screen(run)
        mouse = Mouse(sw, sh)
        ch = 8 if sh < 400 else 16
        run.mark = len(run.lines())
        mouse.click(18, ch // 2)                    # the Desk menu
        mouse.click(60, 4 * ch)                     # EHypView, the first accessory in it
        ok = run.wait("acc opened", 30)
        time.sleep(4)
        mine = run.lines()[run.mark:]
        note("accessory opened from the Desk menu: its window drawn", ACC, spec,
             longest(mine) if ok else None)
    finally:
        run.stop()


def help_case(spec, nvdi):
    d = os.path.join(OUT, slug(spec), "help")
    page = "C:\\FIXT\\ECAL.HYP Main"
    m = make_drive(os.path.join(d, "c"), nvdi, acc=True, ask=page)
    run = Run(os.path.join(slug(spec), "help"), m, r"C:\ASKHELP.PRG")
    try:
        run.wait("acc registered", 120, 0)
        # from the accessory's start on: under fast-forward the request
        # can come before the rig looks
        run.mark = next((i for i, l in enumerate(run.lines()) if l == "EHV acc registered"), 0)
        ok = run.wait("page " + page, 120, 0)
        run.wait("pictures", 30, 0)
        time.sleep(3)
        note("Help key: ECal's front page in the accessory (longest stretch)", WHOLE, spec,
             longest(run.lines()[run.mark:]) if ok else None)
    finally:
        run.stop()


def front_case(spec, nvdi):
    picture_case(spec, nvdi, "front", "C:\\FIXT\\ECAL.HYP", "ECal's front page (program)")


def slug(spec):
    return spec.replace(":", "-").replace("+", "-")


SPEC_NAME = None


def main():
    global SPEC_NAME
    if not shutil.which("hatari"):
        print("skip  speed: needs hatari")
        return 0
    os.makedirs(OUT, exist_ok=True)
    xvfb = None
    want = lambda c: not ONLY or c in ONLY.split(",")
    if want("acc") and shutil.which("Xvfb") and shutil.which("xdotool"):
        xvfb = machines.xvfb(DISPLAY)
    try:
        for spec in SPECS:
            nvdi = spec.endswith("+nvdi")
            SPEC_NAME = spec[:-5] if nvdi else spec
            desc = machines.Machine(SPEC_NAME, os.path.join(OUT, "probe")).describe() + (" + NVDI" if nvdi else "")
            print("      %s: %s" % (spec, desc))
            if want("page"):
                page_case(spec, nvdi)
            if want("images"):
                picture_case(spec, nvdi, "images", "C:\\FIXT\\IMAGES\\IMAGES.HYP", "IMAGES.HYP (16-colour picture)")
                if SPEC_NAME.startswith("Falcon"):
                    SPEC_NAME = SPEC_NAME.split(":")[0] + ":256"
                    picture_case(spec, nvdi, "colour256", "C:\\FIXT\\COLOUR.HYP", "COLOUR.HYP in 256 colours")
                    SPEC_NAME = spec[:-5] if nvdi else spec
            if want("front"):
                front_case(spec, nvdi)
            if want("acc"):
                if xvfb is not None or os.environ.get("DISPLAY") is None and shutil.which("xdotool"):
                    acc_case(spec, nvdi)
                else:
                    print("skip  acc: needs Xvfb and xdotool")
            if want("help"):
                help_case(spec, nvdi)
    finally:
        if xvfb is not None:
            xvfb.terminate()
    over = 0
    w = max(len(a) for a, _ in order) if order else 10
    print("\n%-*s %6s  %s" % (w, "action", "limit", "  ".join("%14s" % s for s in SPECS)))
    for a, lim in order:
        cells = []
        for s in SPECS:
            v = results[(a, lim)].get(s)
            if v is None:
                cells.append("%14s" % ("-" if s not in results[(a, lim)] else "no event"))
            else:
                bad = v > lim
                over += bad
                cells.append("%14s" % ("%d%s" % (v, " OVER" if bad else "")))
        print("%-*s %6d  %s" % (w, a, lim, "  ".join(cells)))
    print("%d over" % over)
    return 1 if over else 0


if __name__ == "__main__":
    sys.exit(main())
