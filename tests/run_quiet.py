#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The accessory's quiet start (the AERA guidelines, 2.2), measured.

    tests/run_quiet.py PROFILE_DIR EMUTOS_IMAGE OUT_DIR

PROFILE_DIR holds an EHYPVIEW.ACC built with -DAES_PROFILE=40 (make
quiet-test): every stretch of 40 ms or more between its event calls goes
to the emulator's console as "AESGAP <ms> ms <tag>".  The accessory boots
on a Falcon (EmuTOS, VGA, at 32 MHz; CPUCLOCK=16 for a stock one) with a
library of 60 hypertexts, 6,720 pages, in C:\\GUIDES (tests/fixtures/many,
copied), and is opened from the Desk menu with the mouse (Xvfb and
xdotool, as tests/run_gui.py does):

  1. first boot, no EHYPVIEW.IDX: nothing is read before it is opened;
     opened, it looks through the library and writes EHYPVIEW.IDX;
  2. second boot: opened, it reads EHYPVIEW.IDX, finds nothing changed
     and writes nothing;
  3. a third boot, with ASKHELP.PRG (tests/askhelp.c) started: it asks
     for a page by its full path, as the AERA programs' Help keys do, and
     the page must come at once, before the index is read (it needs none).

At boot, idle, and while it reads and indexes, no stretch between event
calls may be longer than LIMIT ms ("idle", "idx-*").  What it does for an
event (opening its window: "opening", "event") and drawing the library
once its index is there ("drawing") is window work, as long as drawing
that window takes; those are printed with the rest, for the record.
"""
import os
import re
import shutil
import subprocess
import sys
import time

PDIR, EMUTOS, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
HERE = os.path.dirname(os.path.abspath(__file__))
MANY = os.path.join(HERE, "fixtures", "many")
DISPLAY = ":98"
LIMIT = 50
CPU = os.environ.get("CPUCLOCK", "32")     # MHz
# MACHINE: Hatari's options for another machine, e.g. an ST:
# MACHINE="--machine st --monitor mono --memsize 4" CPUCLOCK=8
MACHINE = os.environ.get("MACHINE", "--machine falcon --monitor vga --memsize 14").split()
GUIDES = int(os.environ.get("QUIET_GUIDES", "60"))   # an ST: fewer (its accessory's heap is smaller)
failed = 0


def check(ok, what):
    global failed
    print(("ok    " if ok else "FAIL  ") + what)
    failed += not ok


def make_drive(d):
    if os.path.exists(d):
        shutil.rmtree(d)
    os.makedirs(os.path.join(d, "GUIDES"))
    shutil.copy(os.path.join(PDIR, "EHYPVIEW.ACC"), d)
    for i in range(GUIDES):
        shutil.copy(os.path.join(MANY, "many.hyp"), os.path.join(d, "GUIDES", "G%03d.HYP" % i))
        shutil.copy(os.path.join(MANY, "many.ref"), os.path.join(d, "GUIDES", "G%03d.REF" % i))


class Mouse:
    """xdotool on the Xvfb display, in Atari screen coordinates"""

    def __init__(self):
        self.env = dict(os.environ, DISPLAY=DISPLAY)
        self.origin = None

    def xdo(self, *args):
        return subprocess.run(["xdotool", *args], env=self.env, capture_output=True, text=True).stdout

    def window(self):
        for _ in range(100):
            if self.origin:
                return self.origin
            found = (self.xdo("search", "--name", "Hatari") or self.xdo("search", "--class", "hatari")).split()
            if found:
                geo = dict(l.split("=") for l in self.xdo("getwindowgeometry", "--shell", found[0]).split())
                self.origin = (int(geo["X"]), int(geo["Y"]))
            else:
                time.sleep(0.2)
        raise RuntimeError("no Hatari window")

    def click(self, x, y):
        wx, wy = self.window()
        loc = dict(l.split("=") for l in self.xdo("getmouselocation", "--shell").split())
        x0, y0 = int(loc["X"]), int(loc["Y"])
        for i in range(1, 13):
            self.xdo("mousemove", str(x0 + (wx + x - x0) * i // 12), str(y0 + (wy + y - y0) * i // 12))
            time.sleep(0.03)
        time.sleep(0.4)
        self.xdo("click", "1")
        time.sleep(0.4)


def boot(d, name, wanted, timeout=int(os.environ.get("QUIET_TIMEOUT", "240"))):
    """boot with drive C: = d, open the accessory, wait for the events
    wanted; the console's lines"""
    log_path = os.path.join(OUT, name + ".log")
    env = dict(os.environ, DISPLAY=DISPLAY, SDL_AUDIODRIVER="dummy")
    args = ["hatari", "--tos", EMUTOS] + MACHINE + [
            "--natfeats", "yes", "--sound", "off", "--confirm-quit", "no", "--statusbar", "no",
            "--log-level", "fatal", "--harddrive", d, "--gemdos-drive", "C",
            "-w", "--borders", "no", "--zoom", "1", "--fast-forward", "no", "--cpuclock", CPU]
    h = subprocess.Popen(args, env=env, stdout=open(log_path, "w"), stderr=subprocess.STDOUT)

    def lines():
        with open(log_path, errors="replace") as f:
            return [l.strip() for l in f]

    def events():
        return [l[4:] for l in lines() if l.startswith("EHV ")]

    try:
        end = time.time() + 60
        while time.time() < end and "started" not in " ".join(events()):
            time.sleep(0.5)
        time.sleep(8)                       # the desktop, and the accessory idle
        boot_lines = len(lines())
        mouse = Mouse()
        mouse.click(18, 8)                  # the Desk menu
        mouse.click(60, 64)                 # EHypView in it
        opened = time.time()
        end = time.time() + timeout
        while time.time() < end and not all(w in events() for w in wanted):
            time.sleep(0.5)
        took = time.time() - opened
        time.sleep(3)
    finally:
        h.terminate()
        try:
            h.wait(10)
        except subprocess.TimeoutExpired:
            h.kill()
    out = lines()
    return out[:boot_lines], out[boot_lines:], events(), took


def help_boot(d, page):
    """boot with drive C: = d and ASKHELP.PRG started: the console's lines,
    and the seconds from its request to the page (None: no page)"""
    log_path = os.path.join(OUT, "help.log")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    args = ["hatari", "--tos", EMUTOS] + MACHINE + [
            "--natfeats", "yes", "--sound", "off", "--confirm-quit", "no", "--statusbar", "no",
            "--log-level", "fatal", "--harddrive", d, "--gemdos-drive", "C", "--fast-forward", "no",
            "--cpuclock", CPU, "--auto", r"C:\ASKHELP.PRG"]
    h = subprocess.Popen(args, env=env, stdout=open(log_path, "w"), stderr=subprocess.STDOUT)
    sent = shown = None
    try:
        end = time.time() + 90
        while time.time() < end and shown is None:
            with open(log_path, errors="replace") as f:
                out = [l.strip() for l in f]
            if sent is None and "ASK sent" in out:
                sent = time.time()
            if sent is not None and "EHV " + page in out:
                shown = time.time()
            time.sleep(0.1)
        time.sleep(1)
    finally:
        h.terminate()
        try:
            h.wait(10)
        except subprocess.TimeoutExpired:
            h.kill()
    with open(log_path, errors="replace") as f:
        out = [l.strip() for l in f]
    return out, (shown - sent if shown is not None else None)


def gaps(lines):
    """the AESGAP lines: (ms, tag)"""
    return [(int(m.group(1)), m.group(2)) for m in (re.match(r"AESGAP (\d+) ms (\S+)", l) for l in lines) if m]


def background(tag):
    return tag == "idle" or tag.startswith("idx-")


def report(name, boot_lines, after, evs, took):
    started = next((e for e in evs if e.startswith("started ")), "started ? ms")
    g_boot, g_after = gaps(boot_lines), gaps(after)
    worst_boot = max([g for g, _ in g_boot] or [0])
    worst = max([g for g, t in g_after if background(t)] or [0])
    print("      %s: first event call after %s; at boot %d stretches of 40 ms or more (longest %d ms)"
          % (name, started.split()[1] + " ms", len(g_boot), worst_boot))
    print("      %s: opened, library ready in %.1f s; reading and indexing: %d stretches of 40 ms or more,"
          " the longest %d ms" % (name, took, len([1 for _, t in g_after if background(t)]), worst))
    for tag in sorted(set(t for _, t in g_after if not background(t))):
        print("      %s: window work, %s: %s ms" % (name, tag, ", ".join(str(g) for g, t in g_after if t == tag)))
    return worst_boot, worst


def main():
    if not (shutil.which("Xvfb") and shutil.which("xdotool")):
        print("skip  quiet start: needs Xvfb and xdotool")
        return 0
    os.makedirs(OUT, exist_ok=True)
    xvfb = subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "800x600x24"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    try:
        d = os.path.join(OUT, "c")
        make_drive(d)
        idx = os.path.join(d, "EHYPVIEW.IDX")

        # 1. the first boot: no index yet
        b, a, evs, took = boot(d, "first", ["index %d guides" % GUIDES, "index saved"])
        check("acc registered" in evs, "first boot: the accessory registered")
        first = next((i for i, e in enumerate(evs) if e.startswith("started")), None)
        check(first is not None and not any(e.startswith("index") for e in evs[:first]),
              "first boot: nothing indexed before its first event call")
        wb, wa = report("first boot", b, a, evs, took)
        check(not gaps(b), "first boot: no stretch of 40 ms or more before it was opened")
        check("index %d guides" % GUIDES in evs and "index saved" in evs, "first boot, opened: indexed and saved")
        check(wa <= LIMIT, "first boot, opened: reading and indexing, the longest stretch %d ms (at most %d)" % (wa, LIMIT))
        check(os.path.exists(idx), "EHYPVIEW.IDX written")
        stamp = os.stat(idx).st_mtime if os.path.exists(idx) else 0

        # 2. the second boot: the index read, nothing changed, nothing written
        time.sleep(2)
        b, a, evs, took = boot(d, "second", ["index %d guides" % GUIDES, "index unchanged"])
        wb, wa = report("second boot", b, a, evs, took)
        check(not gaps(b), "second boot: no stretch of 40 ms or more before it was opened")
        check("index %d guides" % GUIDES in evs and "index unchanged" in evs,
              "second boot, opened: the index read, nothing changed")
        check(wa <= LIMIT, "second boot, opened: reading, the longest stretch %d ms (at most %d)" % (wa, LIMIT))
        check(os.path.exists(idx) and os.stat(idx).st_mtime == stamp, "EHYPVIEW.IDX not written again")

        # 3. a Help key's request, by full path: no index needed
        shutil.copy(os.path.join(PDIR, "ASKHELP.PRG"), d)
        page = r"page C:\GUIDES\G000.HYP Main"
        out, took = help_boot(d, page)
        evs = [l[4:] for l in out if l.startswith("EHV ")]
        check("ASK sent" in out, "help: ASKHELP.PRG asked for the page")
        before = page in evs and not any(e.startswith("index ") for e in evs[:evs.index(page)])
        check(took is not None and before, "help: the page came before the index was read")
        if took is not None:
            print("      help: the page came %.1f s after the request (the emulator's console, read every 0.1 s)" % took)
    finally:
        xvfb.terminate()
    print("%d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
