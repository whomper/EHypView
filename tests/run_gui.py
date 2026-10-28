#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""GUI test: run EHYPVIEW.PRG in Hatari and drive it.

    tests/run_gui.py HATARI_DIR OUT_DIR

The machine is the family's Falcon:16 (common/tools/machines.py), or
another 640 x 480 one in MACHINE.

HATARI_DIR holds EHYPVIEW.PRG, EHYPVIEW.ACC and the test folder FIXT; each
scenario gets its own drive C: under OUT_DIR with an ST-GUIDE.INF.  Keys go in through Hatari's command FIFO; the viewer
reports what it does through NatFeats ("EHV page ...", "EHV select ...")
and each step waits for the event it expects.  A screenshot of every
step is saved in OUT_DIR for people to look at.

The mouse scenario runs Hatari in a window on an X display (Xvfb) and
moves the mouse with xdotool, as EContact's test rig does: links, the
right button, the menus and the About box.  Without Xvfb and xdotool it
is skipped (CI installs them).
"""
import os
import shutil
import subprocess
import sys
import time
import types

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common", "tools"))
import machines  # noqa: E402

HDIR, OUT = sys.argv[1], sys.argv[2]
MACHINE = os.environ.get("MACHINE", "Falcon:16")
ACC_DIR = os.path.join(OUT, "acc")
BASIC = r"C:\FIXT\BASIC.HYP"
OTHER = r"C:\FIXT\SUB\DEEP\OTHER.HYP"

TAB, RETURN, BACKSPACE, HELP, UNDO, ESC = 15, 28, 14, 0x62, 0x61, 1
RIGHT, LEFT, Q, L, W, U, I, F, CTRL, SHIFT = 0x4D, 0x4B, 0x10, 0x26, 0x11, 0x16, 0x17, 0x21, 29, 0x2A
MINUS, EQUALS = 0x0C, 0x0D
N, D, DELETE = 0x31, 0x20, 0x53
DOWN, UP = 0x50, 0x48
IMAGES = r"C:\FIXT\IMAGES\IMAGES.HYP"

COMMA, SEMICOLON, BACKSLASH = 0x33, 0x27, 0x2B
CLRHOME = 0x47

# a folder longer than the Settings field shows (40): the field holds it
# all (guidelines 5.4) and scrolls with the cursor
LONG = "C:\\NOWHERE\\AFOLDERNAMELONGERTHANTHEFIELD\\SHOWS\\GUIDES\\EN"


def typed(text):
    """keys for text in a dialog's field, on the US keyboard: Hatari's
    keypress takes letters and digits, not ':' or '\\'. A field says
    nothing as it is typed into, so each key is given time of its own:
    sent a quarter of a second apart, Hatari once lost one"""
    keys = []
    for c in text:
        keys.append(("field",) + ((SHIFT, SEMICOLON) if c == ":" else (None, BACKSLASH) if c == "\\" else (None, c)))
    return keys


# (keys to send, events expected afterwards, in order)
STEPS = [
    ([], ["font System font 16", "menu shown", "page %s Main" % BASIC]),  # on its command line
    ([], ["index 5 guides"]),                          # background scan done
    # the font: sizes with Control+- and Control+=, then the selector
    ([("ctrl", MINUS)], ["font System font 8"]),
    ([("ctrl", EQUALS)], ["font System font 16"]),
    ([("ctrl", EQUALS)], ["status No larger size"]),
    # Esc and Undo cancel the font selector: the font stays (the guidelines, 3.5)
    ([("ctrl+shift", F)], ["dialog font"]),
    ([LEFT, ESC], ["dialog-closed font cancel", "!font *"]),
    ([("ctrl+shift", F)], ["dialog font"]),
    ([LEFT, UNDO], ["dialog-closed font cancel", "!font *"]),
    ([("ctrl+shift", F)], ["dialog font"]),
    ([TAB, TAB, UP, RETURN], ["dialog-closed font ok", "font System font 8"]),   # Size: 8 x 8, kept in EHYPVIEW.INF
    # Settings (^,): Esc and Undo are Cancel, and nothing is written (3.5)
    ([("ctrl", COMMA)], ["dialog settings *"]),
    ([ESC], ["dialog-closed settings cancel", "!settings *"]),
    ([("ctrl", COMMA)], ["dialog settings *"]),
    ([UNDO], ["dialog-closed settings cancel", "!settings *"]),
    ([TAB], ["select 2"]),                             # first link
    ([RETURN], ["page %s Second" % BASIC]),
    ([BACKSPACE], ["page %s Main" % BASIC]),
    ([TAB] * 3, ["select 2", "select 2", "select 3"]),
    ([RETURN], ["popup Pop"]),                         # a @pnode: a pop-up over the page
    ([ESC], ["popup-closed Pop"]),                     # a key closes it, the page stays
    ([TAB], ["select 4"]),                             # and so does the selection
    ([RETURN], ["page %s Start" % OTHER]),             # external link, three folders down
    ([HELP], ["status This hypertext has no help page"]),
    ([("ctrl", RIGHT)], ["page %s Detail" % OTHER]),   # next page
    ([UNDO], ["page %s Start" % OTHER]),               # back
    # the library: 4 readable hypertexts (BROKEN.HYP is left out)
    ([("ctrl", L)], ["library 4"]),
    ([("type", "det")], ['search "det" 1']),                 # page names as you type
    ([RETURN], ["page %s Detail" % OTHER]),
    ([("ctrl", L)], ["library 4"]),
    # Esc empties the search (the guidelines, 3.5, GEM's way) and the window
    # stays; Backspace takes text out a letter at a time
    ([ESC], ['search "" 4']),
    ([("type", "det")], ['search "det" 1']),
    ([BACKSPACE], ['search "de" *']),
    ([BACKSPACE, BACKSPACE], ['search "" 4']),
    ([("type", "beside")], ['search "beside" 0']),
    ([RETURN], ['fulltext-start "beside"', 'fulltext "beside" 1']),
    ([RETURN], ["page %s Main" % IMAGES]),             # the full-text hit
    ([("ctrl", I)], ["page %s Index" % IMAGES]),       # hcp made an index
    # several windows: the page in a new one; a bookmark of it
    ([("ctrl", N)], ["window new 2", "page %s Index" % IMAGES]),
    # six at most; the alert that says so: Esc and Undo close it, no window more
    ([("ctrl", N)], ["window new 3"]),
    ([("ctrl", N)], ["window new 4"]),
    ([("ctrl", N)], ["window new 5"]),
    ([("ctrl", N)], ["window new 6"]),
    ([("ctrl", N)], ["alert six-windows"]),
    ([ESC], ["alert-closed six-windows 1", "!window new *"]),
    ([("ctrl", N)], ["alert six-windows"]),
    ([UNDO], ["alert-closed six-windows 1", "!window new *"]),
    # ^W: the next window on top, in turn (3.2): after the sixth viewer the
    # library, then the viewers from the first, round to the sixth again;
    # ^U closes the one in front
    ([("ctrl", W)] * 7, ["next-window library"] + ["next-window viewer %d" % i for i in range(1, 7)]),
    ([("ctrl", U)], ["closed viewer"]),
    ([("ctrl", U)], ["closed viewer"]),
    ([("ctrl", U)], ["closed viewer"]),
    ([("ctrl", U)], ["closed viewer"]),
    ([("ctrl", D)], ["bookmark added *"]),
    ([("ctrl", D)], ["status This page is bookmarked already."]),
    ([("ctrl", U)], ["closed viewer"]),                # the first window stays
    # with the menu bar the program stays when its windows are closed
    ([("ctrl", U)], ["closed viewer"]),
    ([("ctrl", W)], ["!next-window *"]),               # one window, the library: nothing
    ([("ctrl", U)], ["closed library"]),
    ([("ctrl", L)], ["library 4"]),
    ([BACKSPACE] * 6, ['search "" 4']),                # the search typed before, taken out:
    ([ESC], []),                                       # Esc with none: nothing, the window stays
    ([RETURN], ["page %s Index" % IMAGES]),            # the bookmark, first in the library
    ([("ctrl", L)], ["library 4"]),
    ([DELETE], ["bookmark removed *"]),                # Delete removes it
    ([("ctrl", Q)], ["quit"]),
]

# started with nothing (DEFHYP in ST-GUIDE.INF isn't EHypView's): the
# library opens, fills when the index is done
# (and with the 8 x 8 font kept in EHYPVIEW.INF)
# (and with the 8 x 8 font and a bookmark kept in EHYPVIEW.INF)
LIBRARY_START = [
    ([], ["font System font 8", "library 0", "index 5 guides"]),
    ([("type", "zweite")], ['search "zweite" 1']),
    ([RETURN], ["page %s Second" % BASIC]),
    ([("ctrl", L)], ["library 4"]),
    ([BACKSPACE] * 6, ['search "" 4']),                # "zweite", taken out
    ([RETURN], ["page %s Detail" % OTHER]),            # the bookmark, first in the library
    ([("ctrl", Q)], ["quit"]),
]


DISPLAY = ":99"
# a case's first event waits for the boot: NVDI reads every Bitstream font
# first (30 s on the 14 MB Falcon, longer at 16 MHz on Falcon-4)
BOOT_WAIT = (120 if MACHINE.startswith("Falcon-4") else 90) if os.environ.get("AERA_WITH_NVDI") else 90


def start_hatari(hdir, auto, fifo, log_path, windowed=False, extra=(), machine=None):
    for name in (fifo, os.path.join(hdir, "EHYPVIEW.IDX")):
        if os.path.exists(name):
            os.remove(name)
    m = machines.Machine(machine or MACHINE, hdir)
    args = ["hatari"] + m.args() + ["--natfeats", "yes", "--statusbar", "no", "--log-level", "fatal",
                                    "--cmd-fifo", fifo] + list(extra)
    env = m.env()
    if windowed:
        env["DISPLAY"] = DISPLAY
        env.pop("SDL_VIDEODRIVER")
    # in a window, one screen pixel is one window pixel for the mouse;
    # fast-forward would outrun the mouse moves
    args += ["-w", "--borders", "no", "--zoom", "1"] if windowed else ["--fast-forward", "yes"]
    if auto:
        args += ["--auto", auto]
    return subprocess.Popen(args, env=env, stdout=open(log_path, "w"), stderr=subprocess.STDOUT)


def start_xvfb():
    """the X display, once it answers: Hatari started before it is up
    can't open its window and prints nothing (a CI run's mouse step 0)"""
    xvfb = subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1024x768x24"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, DISPLAY=DISPLAY)
    deadline = time.time() + 20
    while time.time() < deadline:
        if subprocess.run(["xdotool", "getdisplaygeometry"], env=env, stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode == 0:
            return xvfb
        time.sleep(0.2)
    print("      Xvfb on %s did not answer in 20 s" % DISPLAY)
    return xvfb


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

    def move(self, x, y):
        """glide there, as the AES only sees the mouse when it moves"""
        wx, wy = self.window()
        loc = dict(l.split("=") for l in self.xdo("getmouselocation", "--shell").split())
        x0, y0 = int(loc["X"]), int(loc["Y"])
        for i in range(1, 13):
            self.xdo("mousemove", str(x0 + (wx + x - x0) * i // 12), str(y0 + (wy + y - y0) * i // 12))
            time.sleep(0.03)
        time.sleep(0.4)

    def home(self):
        """the Atari's pointer and ours in step again: from the window's far
        corner to its top-left, a move bigger than the screen, so the AES
        pointer ends at 0,0 wherever it was (a program's start and quit,
        and NVDI's mouse handling, left it 40 px from ours)"""
        wx, wy = self.window()
        self.xdo("mousemove", str(wx + 639), str(wy + 479))
        time.sleep(0.2)
        for i in range(1, 25):
            self.xdo("mousemove", str(wx + 639 - 639 * i // 24), str(wy + 479 - 479 * i // 24))
            time.sleep(0.03)
        time.sleep(0.4)

    def click(self, x, y, button="1"):
        self.move(x, y)
        self.xdo("click", button)
        time.sleep(0.4)


def have_display():
    return shutil.which("Xvfb") and shutil.which("xdotool")


def accessory_smoke():
    """Boot with EHYPVIEW.ACC in the root and START=now in EHYPVIEW.INF: it
    must register and index in the background (by default, START=opened,
    it waits until it is opened: tests/run_quiet.py)."""
    import shutil
    if os.path.exists(ACC_DIR):
        shutil.rmtree(ACC_DIR)
    shutil.copytree(os.path.join(HDIR, "FIXT"), os.path.join(ACC_DIR, "FIXT"))
    shutil.copy(os.path.join(HDIR, "EHYPVIEW.ACC"), ACC_DIR)
    with open(os.path.join(ACC_DIR, "ST-GUIDE.INF"), "w") as f:
        f.write("PATHS=C:\\FIXT\r\nREF=C:\\FIXT\\ALL.REF\r\n")
    with open(os.path.join(ACC_DIR, "EHYPVIEW.INF"), "w") as f:
        f.write("START=now\r\n")
    fifo = os.path.join(OUT, "acc.fifo")
    log_path = os.path.join(OUT, "acc.log")
    hatari = start_hatari(ACC_DIR, None, fifo, log_path)
    ok = False
    deadline = time.time() + 40
    while time.time() < deadline and not ok:
        time.sleep(0.5)
        with open(log_path, errors="replace") as f:
            evs = [l.strip()[4:] for l in f if l.startswith("EHV ")]
        ok = "acc registered" in evs and "index 5 guides" in evs
    if os.path.exists(fifo):
        with open(fifo, "w") as ctl:
            ctl.write("hatari-debug screenshot %s\n" % os.path.abspath(os.path.join(OUT, "acc.png")))
            ctl.flush()
            time.sleep(0.5)
            ctl.write("hatari-debug quit\n")
    try:
        hatari.wait(10)
    except subprocess.TimeoutExpired:
        hatari.kill()
    print("%s  accessory: registered and indexed in the background" % ("ok  " if ok else "FAIL"))
    return ok


def absent(evs, expected):
    """"!x": no event matching x among evs"""
    return not any(matches(ev, expected[1:]) for ev in evs)


def matches(ev, expected):
    if expected.endswith("*"):
        return ev.startswith(expected[:-1])
    return ev == expected


def run_scenario(name, hdir, steps, auto=r"C:\EHYPVIEW.PRG", windowed=False, extra=(), machine=None):
    fifo = os.path.join(OUT, name + ".fifo")
    log_path = os.path.join(OUT, name + ".log")
    hatari = start_hatari(hdir, auto, fifo, log_path, windowed, extra, machine)
    mouse = Mouse() if windowed else None
    for _ in range(100):
        if os.path.exists(fifo):
            break
        time.sleep(0.1)
    ctl = open(fifo, "w")

    def send(cmd):
        ctl.write(cmd + "\n")
        ctl.flush()
        time.sleep(0.25)

    seen = []

    def events():
        with open(log_path, errors="replace") as f:
            return [l.strip()[4:] for l in f if l.startswith("EHV ")]

    def why():
        """no event at all: what Hatari said, and whether it still runs"""
        if events():
            return
        code = hatari.poll()
        print("      Hatari %s; its output:" % ("still runs" if code is None else "ended, code %d" % code))
        with open(log_path, errors="replace") as f:
            for l in f.read().splitlines()[-15:]:
                print("        " + l)

    def wait_for(expected, timeout=30):
        deadline = time.time() + timeout
        while time.time() < deadline:
            new = events()[len(seen):]
            for i, ev in enumerate(new):
                if matches(ev, expected):
                    seen.extend(new[:i + 1])
                    return True
            time.sleep(0.2)
        return False

    def type_text(text):
        """into the library's search, a letter at a time: each waits for
        the search it makes, and goes again if the emulator lost it"""
        for i, ch in enumerate(text):
            want = 'search "%s" *' % text[:i + 1]
            for _ in range(3):
                send("hatari-event keypress %s" % ch)
                deadline = time.time() + 3
                while time.time() < deadline:
                    if any(matches(ev, want) for ev in events()[len(seen):]):
                        break
                    time.sleep(0.1)
                else:
                    continue                # not taken: once more
                break

    def press(k):
        if isinstance(k, tuple) and k[0] in ("click", "rclick"):
            mouse.click(k[1], k[2], "1" if k[0] == "click" else "3")
        elif isinstance(k, tuple) and k[0] == "sclick":       # with Shift held
            send("hatari-event keydown 0x%x" % SHIFT)
            mouse.click(k[1], k[2])
            send("hatari-event keyup 0x%x" % SHIFT)
        elif isinstance(k, tuple) and k[0] == "dclick":
            mouse.move(k[1], k[2])
            mouse.xdo("click", "--repeat", "2", "--delay", "80", "1")
            time.sleep(2)
        elif isinstance(k, tuple) and k[0] == "type":
            type_text(k[1])
        elif isinstance(k, tuple) and k[0] == "field":     # a key into a dialog's field
            if k[1]:
                send("hatari-event keydown 0x%x" % k[1])
                time.sleep(0.2)
            send("hatari-event keypress %s" % (k[2] if isinstance(k[2], str) else "0x%x" % k[2]))
            time.sleep(0.3)
            if k[1]:
                send("hatari-event keyup 0x%x" % k[1])
                time.sleep(0.2)
        elif isinstance(k, tuple) and k[0] == "drag":       # press, glide there, let go
            mouse.move(k[1], k[2])
            mouse.xdo("mousedown", "1")
            time.sleep(0.4)
            mouse.move(k[3], k[4])
            time.sleep(0.3)
            mouse.xdo("mouseup", "1")
            time.sleep(1)
        elif isinstance(k, tuple) and k[0] == "hold":       # a click held so many ms
            mouse.move(k[1], k[2])
            mouse.xdo("mousedown", "1")
            time.sleep(k[3] / 1000.0)
            mouse.xdo("mouseup", "1")
            time.sleep(0.4)
        elif isinstance(k, tuple) and k[0] == "wait":
            time.sleep(k[1])
        elif isinstance(k, tuple) and k[0] == "still":     # till the screen stops changing
            still(k[1])
        elif isinstance(k, tuple) and k[0] == "home":      # the pointers in step
            mouse.home()
        elif isinstance(k, tuple) and k[0] == "hover":      # rest there
            mouse.move(k[1], k[2])
            time.sleep(2)
        elif isinstance(k, tuple) and k[0] == "menu":
            mouse.click(k[1], k[2])                # the title drops the menu down
            mouse.click(k[3], k[4])                # the item
        elif isinstance(k, tuple):
            mods = [SHIFT] if k[0] == "shift" else [CTRL] + ([SHIFT] if k[0] == "ctrl+shift" else [])
            for m in mods:
                send("hatari-event keydown 0x%x" % m)
            send("hatari-event keypress 0x%x" % k[1])
            for m in reversed(mods):
                send("hatari-event keyup 0x%x" % m)
        elif isinstance(k, str):
            send("hatari-event keypress %s" % k)
        else:
            send("hatari-event keypress 0x%x" % k)

    def still(timeout):
        """screenshots a second apart till two are the same (the desktop
        redrawn after a program ended, NVDI's banner gone), at most timeout s"""
        shot = os.path.abspath(os.path.join(OUT, name + "-still.png"))
        last = None
        deadline = time.time() + timeout
        while time.time() < deadline:
            if os.path.exists(shot):
                os.remove(shot)
            send("hatari-debug screenshot %s" % shot)
            for _ in range(20):
                if os.path.exists(shot) and os.path.getsize(shot) > 0:
                    break
                time.sleep(0.1)
            time.sleep(0.3)
            try:
                with open(shot, "rb") as f:
                    now = f.read()
            except OSError:
                now = None
            if now is not None and now == last:
                return True
            last = now
            time.sleep(1)
        print("      %s: the screen still changed after %d s" % (name, timeout))
        return False

    def wait_any(expected, timeout=30):
        """the first of these to come: its index, or -1"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            new = events()[len(seen):]
            for i, ev in enumerate(new):
                for k, e in enumerate(expected):
                    if matches(ev, e):
                        seen.extend(new[:i + 1])
                        return k
            time.sleep(0.2)
        return -1

    # a step may be a function of the rig instead of keys: it presses and
    # waits as it goes, and says False when it failed
    rig = types.SimpleNamespace(press=press, send=send, events=events, seen=seen,
                                wait_for=wait_for, wait_any=wait_any, name=name, log=log_path)

    ok = True
    try:
        for n, (keys, expect) in enumerate(steps):
            if callable(keys):
                if not keys(rig):
                    print("FAIL  %s step %d" % (name, n))
                    print("      events so far: %s" % events())
                    ok = False
            else:
                for k in keys:
                    press(k)
            if not ok:
                send("hatari-debug screenshot %s" % os.path.abspath(
                    os.path.join(OUT, "%s-%02d.png" % (name, n))))
                break
            if not expect:
                time.sleep(1.0)
            for e in expect:
                if e.startswith("!"):
                    # nothing of the kind since the last event waited for
                    time.sleep(1.5)
                    if not absent(events()[len(seen):], e):
                        print("FAIL  %s step %d: '%s' came" % (name, n, e[1:]))
                        print("      events so far: %s" % events())
                        ok = False
                        break
                    continue
                # the first event waits for the boot too
                if not wait_for(e, BOOT_WAIT if not seen else 30):
                    print("FAIL  %s step %d: no '%s'" % (name, n, e))
                    print("      events so far: %s" % events())
                    why()
                    ok = False
                    break
            send("hatari-debug screenshot %s" % os.path.abspath(
                os.path.join(OUT, "%s-%02d.png" % (name, n))))
            if not ok:
                break
            print("ok    %s step %d: %s" % (name, n, expect[-1] if expect else "(done)"))
    finally:
        send("hatari-debug quit")
        try:
            hatari.wait(10)
        except subprocess.TimeoutExpired:
            hatari.kill()
    return ok


LAUNCH = r"C:\LAUNCH.PRG"


def copy_dir(name, inf, settings=None, start=None):
    """A drive C: with the program (not the accessory), FIXT and an INF;
    with `start`, LAUNCH.PRG (tests/launch.c) to start the program with
    that command line (run_scenario's auto=LAUNCH)"""
    import shutil
    d = os.path.join(OUT, name)
    if os.path.exists(d):
        shutil.rmtree(d)
    shutil.copytree(os.path.join(HDIR, "FIXT"), os.path.join(d, "FIXT"))
    shutil.copy(os.path.join(HDIR, "EHYPVIEW.PRG"), d)
    with open(os.path.join(d, "ST-GUIDE.INF"), "w") as f:
        f.write(inf.replace("\n", "\r\n"))
    if start is not None:
        shutil.copy(os.path.join(HDIR, "LAUNCH.PRG"), d)
        with open(os.path.join(d, "LAUNCH.TXT"), "w") as f:
            f.write("C:\\EHYPVIEW.PRG\r\n%s\r\n" % start)
    if settings is not None:
        with open(os.path.join(d, "EHYPVIEW.INF"), "w") as f:
            f.write(settings.replace("\n", "\r\n"))
    return d


# with the mouse (8x16 system font, Falcon VGA): a link, the right
# button's menu, and each menu (title x, y, then the item's x, y)
DESK, FILE, EDIT, VIEW, NAV, BOOK = (56, 9), (120, 9), (168, 9), (216, 9), (280, 9), (364, 9)
OPTIONS, HELPMENU = (444, 9), (504, 9)

# the Settings dialog, centred on the screen: its pages on the left, the
# START= popup on General; on Library the layout's popup and its second
# choice (a pop-up opens over its button, the choice shown on it: the
# second just below), the first folder field
SET_LIBRARY, SET_START, SET_START_NOW, SET_FIELD = (135, 187), (320, 203), (300, 219), (300, 237)
SET_LAYOUT, SET_LAYOUT_PANES = (340, 181), (330, 197)
SET_ORIGIN = (89, 102)          # where the dialog is on 640 x 480, which SET_* are measured on


def item(title, n):
    """the n-th item (from 0) of the menu under title"""
    return ("menu", title[0], title[1], title[0] + 24, 19 + 16 * n + 8)


# the first viewer window's work area starts at x 24: the window is put
# where its work area is on the 8-pixel grid (wind_x8, the guidelines'
# Speed; it started at 27 before), and what is measured in it below is
# moved by VX (its text, 8 pixels in now, by one pixel less)
VX = -3


def vx(x):
    return x + VX


# the viewer's toolbar (src/gem/toolbar.c): the middle of each button, in
# the first window; the page starts 24 pixels lower than before it; menus
# from a button open below it, their first item at y 111, the next 18 below
TB_Y = 90
TB_BACK, TB_FORWARD, TB_HOME = (vx(43), TB_Y), (vx(69), TB_Y), (vx(95), TB_Y)
TB_PREV, TB_TOC, TB_NEXT = (vx(125), TB_Y), (vx(179), TB_Y), (vx(233), TB_Y)
TB_INDEX, TB_SEEALSO, TB_LIBRARY, TB_BOOKMARKS, TB_MENU = \
    (vx(279), TB_Y), (vx(349), TB_Y), (vx(431), TB_Y), (vx(513), TB_Y), (vx(579), TB_Y)


def tb(button, kind="click"):
    return (kind, button[0], button[1])


# the library's layouts (LIBRARY=), by the keys: the Outline folds a
# group and opens a page read lately from Continue; Two panes chooses a
# subject on the left and a hypertext on the right.  The pages read are
# kept (RECENTn) at the end
OUTLINE_STEPS = [
    ([], ["library 0", "library-layout outline", "index 5 guides"]),
    ([LEFT], ["fold Test folded"]),                    # the group of the hypertext chosen
    ([RIGHT], ["fold Test open"]),                     # ("Test/EHypView": EHypView under Test)
    ([UP, RETURN], ["page %s Detail" % OTHER]),        # above the heading: Continue
    ([("ctrl", Q)], ["quit"]),
]
PANES_STEPS = [
    ([], ["library 0", "library-layout panes", "index 5 guides"]),
    ([LEFT], ["library-focus subjects"]),
    ([DOWN], ["pane Test 3"]),
    ([DOWN], ["pane EHypView 3"]),                     # "Test/EHypView": a part, under Test
    ([RETURN], ["library-focus rows"]),
    ([RETURN], ["page %s Main" % BASIC]),             # its first hypertext
    ([("ctrl", L)], ["library *"]),
    ([("ctrl", Q)], ["quit"]),
]


MOUSE_STEPS = [
    ([], ["menu shown", "page %s Main" % BASIC, "index 5 guides"]),
    # the toolbar: Index, Back, Forward, the history on Back's right button
    ([tb(TB_INDEX)], ["toolbar index", "page %s Index" % BASIC]),
    ([tb(TB_BACK)], ["toolbar back", "page %s Main" % BASIC]),
    ([tb(TB_FORWARD)], ["toolbar forward", "page %s Index" % BASIC]),
    ([tb(TB_BACK, "rclick")], ["menu back-history"]),
    ([("click", vx(70), 111)], ["history -1", "page %s Main" % BASIC]),
    ([("shift", UNDO)], ["page %s Index" % BASIC]),        # Shift+Undo: forward
    ([BACKSPACE], ["page %s Main" % BASIC]),
    # See also: the page's @xref
    ([tb(TB_SEEALSO)], ["toolbar see-also", "menu see-also"]),
    ([("click", vx(340), 111)], ["see-also The help page", "page %s Help" % BASIC]),
    ([BACKSPACE], ["page %s Main" % BASIC]),
    ([("hover",) + TB_LIBRARY], ["tip library"]),           # a tooltip after a second
    # Bookmarks: add this page, then remove it
    ([tb(TB_BOOKMARKS)], ["toolbar bookmarks", "menu bookmarks"]),
    ([("click", vx(540), 111)], ["menu bookmark-add", "bookmark added *"]),
    ([tb(TB_BOOKMARKS)], ["toolbar bookmarks", "menu bookmarks"]),
    ([("click", vx(540), 129)], ["menu bookmark-remove", "bookmark removed *"]),
    # the menu button: About first (the accessory's way to it)
    ([tb(TB_MENU)], ["toolbar menu", "menu toolbar"]),
    ([("click", vx(600), 111)], ["menu about", "dialog about *"]),
    ([("click", 320, 300)], []),                            # a click closes it
    ([tb(TB_LIBRARY)], ["toolbar library", "library 4"]),
    ([("wait", 1)], []),                                    # (a picture of it)
    ([("type", "sec")], ['search "sec" *']),
    ([BACKSPACE] * 3, ['search "" 4']),
    ([("ctrl", U)], ["closed library"]),
    # the page: links, the right button's menu, pop-ups
    ([("click", vx(120), 142)], ["page %s Second" % BASIC]),    # "to Second"
    ([("rclick", vx(300), 174)], ["menu popup"]),               # right button: the menu,
    ([("click", vx(330), 185)], ["menu back", "page %s Main" % BASIC]),     # Back first
    ([("click", vx(120), 158)], ["popup Pop"]),                 # "popup here": the pop-up,
    ([("click", 500, 424)], ["popup-closed Pop"]),          # a click outside closes it
    ([("click", vx(120), 158)], ["popup Pop"]),                 # and its own link
    ([("click", vx(200), 199)], ["popup-link Pop", "page %s Second" % BASIC]),  # goes there
    ([BACKSPACE], ["page %s Main" % BASIC]),
    ([("rclick", vx(300), 174)], ["menu popup"]),               # and About last (rows of 18)
    ([("click", vx(330), 437)], ["menu about", "dialog about *"]),
    ([("click", 320, 300)], []),                            # a click closes it
    ([item(DESK, 0)], ["menu about", "dialog about *"]),    # the About box
    ([("click", 320, 300)], []),                            # a click closes it
    # Settings: START= from the popup, three folders on the Library page
    # (Esc in a field empties it); one that isn't there is asked about
    # (Esc: Change, back to the page), then kept; OK writes EHYPVIEW.INF
    # and looks through the library again. The third is longer than its field shows (5.4), typed with an X in
    # front that ClrHome and Delete take away at its start, then its end
    # added after Shift+Right: the whole of it is kept
    ([item(OPTIONS, 0)], ["menu settings", "dialog settings *"]),
    ([("click",) + SET_START], ["menu settings-start"]),
    ([("click",) + SET_START_NOW], ["settings-start now"]),
    ([("click",) + SET_LIBRARY], ["settings-page library"]),
    ([("click",) + SET_LAYOUT], ["menu settings-layout"]),
    ([("click",) + SET_LAYOUT_PANES], ["settings-layout panes"]),
    ([("click",) + SET_FIELD] + typed("C:\\FIXT\\SUB"), []),
    ([TAB] + typed("C:\\NOWHERE"), []),
    # Esc empties the field the cursor is in and the dialog stays (3.5)
    ([ESC] + typed("C:\\NOWHERE"), ["!dialog-closed *"]),
    ([TAB] + typed("X" + LONG[:-3]) + [("field", None, CLRHOME), ("field", None, DELETE),
                                       ("field", SHIFT, RIGHT)] + typed(LONG[-3:]), []),
    ([RETURN], ["alert no-folder C:\\NOWHERE"]),
    ([ESC], ["alert-closed no-folder change"]),
    ([RETURN], ["alert no-folder C:\\NOWHERE"]),
    ([RETURN], ["alert-closed no-folder keep", "alert no-folder " + LONG]),
    ([RETURN], ["alert-closed no-folder keep", "dialog-closed settings ok", "settings START now",
                "settings LIBRARY panes", "settings PATHS C:\\FIXT\\SUB;C:\\NOWHERE;" + LONG,
                "index again", "index 5 guides"]),
    # the guide, when it is nowhere: where to put it
    ([item(HELPMENU, 0)], ["menu guide", "alert no-guide"]),
    ([RETURN], []),
    # several windows: Shift+click opens the link in a new one
    ([("sclick", vx(120), 142)], ["window new 2", "page %s Second" % BASIC]),
    ([item(BOOK, 0)], ["menu bookmark-add", "bookmark added *"]),
    ([("ctrl", U)], ["closed viewer"]),
    ([item(BOOK, 3)], ["menu bookmark", "page %s Second" % BASIC]),   # in the first window
    ([item(BOOK, 1)], ["menu bookmark-remove", "bookmark removed *"]),
    ([BACKSPACE], ["page %s Main" % BASIC]),
    ([("rclick", vx(120), 142)], ["menu popup"]),               # on a link, the menu has
    ([("click", vx(150), 189)], ["menu link-window", "window new 2", "page %s Second" % BASIC]),
    ([("ctrl", U)], ["closed viewer"]),
    ([item(VIEW, 0)], ["menu font", "dialog font"]),        # the font selector
    ([ESC], []),
    ([item(EDIT, 1)], ["menu copy", "copy page *"]),       # the page to the clipboard
    ([item(NAV, 7)], ["menu help", "page %s Help" % BASIC]),
    ([item(NAV, 0)], ["menu back", "page %s Main" % BASIC]),
    ([item(FILE, 2)], ["menu library", "library 4"]),
    # a "quit" link with two windows open closes just its own
    ([("ctrl", N)], ["window new 2", "page %s Main" % BASIC]),
    ([HELP], ["page %s Help" % BASIC]),
    ([TAB, RETURN], ["select *", "closed viewer"]),
    ([item(FILE, 7)], ["menu quit", "quit"]),
]


# EHypView's own guide (make help): Help in the library opens its page
# of it; Help > EHypView guide finds it in C:\\GUIDES through the library
# and shows the front page's two pictures; a link on it leads to its page,
# and Help there to the guide's help page, Keys (guideline 4.3)
GUIDE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "binary", "EHYPVIEW.HYP")
GUIDE_STEPS = [
    ([], ["menu shown", "library 0", "index 6 guides"]),
    ([HELP], [r"page C:\GUIDES\EHYPVIEW.HYP The library"]),
    ([item(HELPMENU, 0)], ["menu guide", r"page C:\GUIDES\EHYPVIEW.HYP Main", "pictures 2 st-ram"]),
    ([TAB, RETURN], ["select *", r"page C:\GUIDES\EHYPVIEW.HYP The toolbar"]),
    ([HELP], [r"page C:\GUIDES\EHYPVIEW.HYP Keys"]),
    ([("ctrl", Q)], ["quit"]),
]


# EHYPVIEW.PRG and EHYPVIEW.ACC (the AERA guidelines, 2.3, 6.1): the
# accessory starts quietly (nothing read until it is opened); the program,
# started by the desktop (Hatari's --auto C:\\EHV\\EHYPVIEW.PRG, so no
# desktop icons are clicked: their places differ from TOS to TOS), writes
# C:\\EHYPVIEW.INF with FOLDER=; the accessory chosen meanwhile hands it to
# the program; the program makes the text smaller and quits; the
# accessory, opened, takes the new size
TWIN_AUTO = r"C:\EHV\EHYPVIEW.PRG"
TWIN_STEPS = [
    ([], ["acc registered", "menu shown", "library 0", "index 5 guides"]),
    ([item(DESK, 2)], ["acc to the program"]),                          # the accessory, in the Desk menu
    ([("ctrl", MINUS)], ["font System font 8"]),
    ([("ctrl", Q)], ["quit"]),
    ([("wait", 6), ("click", 18, 8), ("click", 60, 64)], ["font System font 8"]),    # the desktop's Desk menu
]


# a Falcon with 64 MB of Fast RAM (TT-RAM), where malloc gives Fast RAM: the
# pictures of a page must still be in ST RAM, which is all a blitter reads
# (a CT60 Falcon with TOS's VDI showed garbage). The program, then the
# accessory, whose ST RAM is reserved at boot
FASTRAM = ["--ttram", "64", "--addr24", "off"]
FASTRAM_STEPS = [
    ([], ["page %s Main" % IMAGES, "pictures 3 st-ram"]),    # shown, then its pictures made
    ([("ctrl", Q)], ["quit"]),
]
ACC_FASTRAM_STEPS = [
    ([], ["acc registered", "started *"]),
    # the desktop's Desk menu: the library, read once it is opened
    ([("wait", 6), ("click", 18, 8), ("click", 60, 64)], ["library 0", "index 5 guides"]),
    ([("type", "image")], ['search "image" 1']),
    ([RETURN], ["page %s Main" % IMAGES, "pictures 3 st-ram"]),
]


# The accessory with a GDOS font under NVDI (guidelines 2.2), for the hub's
# runs with Erez's NVDI (AERA_WITH_NVDI=1, common/tools/machines.py; the
# case is skipped without it). Keyboard only, but for the desktop's Desk
# menu, so no TOS's layout matters. Boot 1: EHYPVIEW.PRG (--auto) measures
# the GDOS fonts in its selector (EHYPVIEW.FNT) and quits; the accessory,
# opened from the desktop, picks the first GDOS font in its own Font dialog
# (the "font-chosen NAME" line says which): under 1.13's
# rule it is taken at the next boot (an alert), under NVDI with PR #32 at
# once; then the accessory is closed and opened again. Boot 2, with FONT=
# set: ASKHELP.PRG (--auto) asks the accessory for a page and quits, so the
# accessory draws in the font with another program in front and after it
# ended, then is closed and opened again. Screenshots of every step are in
# build/gui (accnvdi1-NN.png, accnvdi2-NN.png).
ACC_NVDI = {}
DESK_ACC = [("still", 60), ("home",), ("click", 18, 8), ("click", 60, 64)]    # the desktop's Desk menu, first accessory


def pick_gdos_font(rig):
    """in the accessory's Font dialog (common/ui/fontsel.c): the family
    under the system font, the first GDOS font it lists (only the
    fixed-width ones with letters are)"""
    rig.press(("ctrl+shift", F))
    if not rig.wait_for("dialog font"):
        return False
    rig.press(DOWN)
    time.sleep(2)
    rig.press(RETURN)
    if not rig.wait_for("dialog-closed font ok"):
        return False
    if not rig.wait_for("font-chosen *"):
        return False
    chosen = [e for e in rig.events() if e.startswith("font-chosen ")][-1][len("font-chosen "):]
    name = chosen.rsplit(" ", 1)[0]
    if name == "System font":
        print("      %s: no GDOS font in the list" % rig.name)
        return False
    k = rig.wait_any(["alert font-next-boot *", "font %s *" % name])
    if k < 0:
        return False
    ACC_NVDI["font"] = name
    ACC_NVDI["rule"] = "at the next boot (1.13)" if k == 0 else "at once (NVDI, PR #32)"
    print("ok    %s: %s chosen in the accessory, used %s" % (rig.name, name, ACC_NVDI["rule"]))
    if k == 0:
        rig.press(RETURN)                       # the alert's OK
    return True


def reopen(rig):
    """the accessory again from the desktop's Desk menu: its viewer or
    library, opened or (still open: EmuTOS keeps an accessory's windows
    when the program in front ends, TOS 4 closes them) to the front"""
    for k in DESK_ACC:
        rig.press(k)
    return rig.wait_for("acc opened")


def ask_done(rig):
    """ASKHELP.PRG ended (after the seconds in ASKHELP.TXT): the desktop's
    menu bar, and its Desk menu, are back"""
    deadline = time.time() + 60
    while time.time() < deadline:
        with open(rig.log, errors="replace") as f:
            if any(l.startswith("ASK done") for l in f):
                return True
        time.sleep(0.5)
    print("      %s: ASKHELP.PRG did not end" % rig.name)
    return False


def font_at_boot(rig):
    """boot 2: the accessory loaded the font chosen, before its first event call"""
    return rig.wait_for("font %s *" % ACC_NVDI.get("font", "?"), 90)


ACC_NVDI_BOOT1 = [
    ([], ["acc registered", "menu shown"]),
    ([("ctrl+shift", F)], ["dialog font"]),                 # the program measures the fonts
    ([ESC], ["dialog-closed font cancel"]),
    ([("ctrl", Q)], ["quit"]),
    (DESK_ACC, ["library *"]),                              # the accessory, from the desktop
    (pick_gdos_font, []),
    ([("ctrl", U)], ["closed *"]),
    (reopen, []),
]
ACC_NVDI_BOOT2 = [
    (font_at_boot, []),
    ([], ["page %s Main" % BASIC]),                         # asked by ASKHELP.PRG
    (ask_done, []),                                         # which ends: TOS closes the window
    (reopen, []),
    ([("ctrl", U)], ["closed *"]),
    (reopen, []),
]


def acc_nvdi_dir():
    """C: with the accessory in the root, the program in C:\\EHV, ASKHELP.PRG
    asking for BASIC.HYP's main page, and FIXT"""
    d = os.path.join(OUT, "accnvdi")
    if os.path.exists(d):
        shutil.rmtree(d)
    shutil.copytree(os.path.join(HDIR, "FIXT"), os.path.join(d, "FIXT"))
    os.makedirs(os.path.join(d, "EHV"))
    shutil.copy(os.path.join(HDIR, "EHYPVIEW.PRG"), os.path.join(d, "EHV"))
    shutil.copy(os.path.join(HDIR, "EHYPVIEW.ACC"), d)
    shutil.copy(os.path.join(HDIR, "ASKHELP.PRG"), d)
    with open(os.path.join(d, "ASKHELP.TXT"), "w") as f:
        f.write(BASIC + " Main\r\n5\r\n")               # and ends 5 s after asking
    with open(os.path.join(d, "ST-GUIDE.INF"), "w") as f:
        f.write("PATHS=C:\\FIXT\r\nREF=C:\\FIXT\\ALL.REF\r\n")
    # EHypView's folder from the start, so the accessory (booted before the
    # program writes it) finds the program's EHYPVIEW.FNT under 1.13's rule
    with open(os.path.join(d, "EHYPVIEW.INF"), "w") as f:
        f.write("; where EHypView keeps its files (EHYPVIEW.IDX, the library; EHYPVIEW.FNT, the fonts "
                "measured): the program writes the folder it was started from\r\nFOLDER=C:\\EHV\r\n")
    return d


def acc_nvdi():
    if not (os.environ.get("AERA_WITH_NVDI") or os.environ.get("ACC_NVDI_TRY")):
        print("skip  accnvdi: runs under Erez's NVDI only (AERA_WITH_NVDI=1 make accnvdi-test)")
        return True
    if not have_display():
        print("skip  accnvdi: needs Xvfb and xdotool")
        return True
    d = acc_nvdi_dir()
    xvfb = start_xvfb()
    try:
        ok = run_scenario("accnvdi1", d, ACC_NVDI_BOOT1, auto=r"C:\EHV\EHYPVIEW.PRG", windowed=True)
        ok = ok and check_inf(os.path.join(d, "EHYPVIEW.INF"), ["FONT=%s" % ACC_NVDI.get("font", "?")])
        ok = ok and run_scenario("accnvdi2", d, ACC_NVDI_BOOT2, auto=r"C:\ASKHELP.PRG", windowed=True)
    finally:
        xvfb.terminate()
    return ok


def acc_fastram_dir():
    """C: with the accessory in the root, and FIXT"""
    import shutil
    d = os.path.join(OUT, "accfast")
    if os.path.exists(d):
        shutil.rmtree(d)
    shutil.copytree(os.path.join(HDIR, "FIXT"), os.path.join(d, "FIXT"))
    shutil.copy(os.path.join(HDIR, "EHYPVIEW.ACC"), d)
    with open(os.path.join(d, "ST-GUIDE.INF"), "w") as f:
        f.write("PATHS=C:\\FIXT\r\nREF=C:\\FIXT\\ALL.REF\r\n")
    return d


def twin_dir():
    """C: with the accessory in the root and the program in C:\\EHV"""
    import shutil
    d = os.path.join(OUT, "twin")
    if os.path.exists(d):
        shutil.rmtree(d)
    shutil.copytree(os.path.join(HDIR, "FIXT"), os.path.join(d, "FIXT"))
    os.makedirs(os.path.join(d, "EHV"))
    shutil.copy(os.path.join(HDIR, "EHYPVIEW.PRG"), os.path.join(d, "EHV"))
    shutil.copy(os.path.join(HDIR, "EHYPVIEW.ACC"), d)
    with open(os.path.join(d, "ST-GUIDE.INF"), "w") as f:
        f.write("PATHS=C:\\FIXT\r\nREF=C:\\FIXT\\ALL.REF\r\n")
    return d


def check_inf(path, settings):
    """EHYPVIEW.INF (the AERA guidelines, 6.1): these settings ("!KEY": no
    such line), CR LF line ends, and a ';' note above each"""
    try:
        with open(path, newline="") as f:
            got = f.read()
    except OSError as e:
        got = str(e)
    lines = got.split("\r\n")
    ok = got.endswith("\r\n") and "\n" not in got.replace("\r\n", "")
    ok = ok and all(not any(l.upper().startswith(s[1:]) for l in lines) if s.startswith("!") else
                    any(l.startswith(s[:-1]) for l in lines) if s.endswith("*") else s in lines
                    for s in settings)
    ok = ok and all(i > 0 and lines[i - 1].startswith(";")
                    for i, l in enumerate(lines) if l and not l.startswith(";"))
    print("%s  %s: %r" % ("ok  " if ok else "FAIL", os.path.basename(path), got))
    return ok


def check_exists(path, what):
    ok = os.path.exists(path)
    print("%s  %s" % ("ok  " if ok else "FAIL", what))
    return ok


def in_settings(*keys):
    """a step of keys in the Settings dialog, where it is on this screen
    ("dialog settings X Y"): a ("click", x, y) measured on 640 x 480 is
    moved with it (an ST's 640 x 400 has it higher)"""
    def step(rig):
        at = [ev.split()[2:4] for ev in rig.events() if ev.startswith("dialog settings ")]
        if not at:
            return False
        dx, dy = int(at[-1][0]) - SET_ORIGIN[0], int(at[-1][1]) - SET_ORIGIN[1]
        for k in keys:
            rig.press(("click", k[1] + dx, k[2] + dy) if k[0] == "click" else k)
        return True
    return step


# the long field on an ST, TOS 1.04 in mono (guidelines 5.4): its AES can
# give Shift+Left, Shift+Right and Shift+ClrHome with kstate 0 (EFTP found
# it), so each is checked by what it does to the text: from the end,
# Shift+Left and Delete take the first letter, typed again; Shift+ClrHome
# goes to the end, where a letter typed and taken out leaves it as it was
ST_FIELD_STEPS = [
    ([], ["menu shown", "page %s Main" % BASIC, "index 5 guides", "index saved"]),
    ([item(OPTIONS, 0)], ["menu settings", "dialog settings *"]),
    (in_settings(("click",) + SET_LIBRARY), ["settings-page library"]),
    (in_settings(*([("click",) + SET_FIELD] + typed("X" + LONG[:-3]) +
                   [("field", None, CLRHOME), ("field", None, DELETE), ("field", SHIFT, RIGHT)] +
                   typed(LONG[-3:]) + [("field", SHIFT, LEFT), ("field", None, DELETE)] + typed(LONG[0]) +
                   [("field", SHIFT, CLRHOME)] + typed("Q") + [("field", None, BACKSPACE)])), []),
    ([RETURN], ["alert no-folder " + LONG]),
    ([RETURN], ["alert-closed no-folder keep", "dialog-closed settings ok", "settings PATHS " + LONG]),
    ([item(DESK, 0), ("hover", 620, 380), ("still", 5)], ["menu about", "dialog about *"]),  # its mono icon
    ([("click", 320, 200)], []),                            # a click closes it
    ([("ctrl", Q)], ["quit"]),
]


def st_field():
    d = copy_dir("stfield", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\n", start=BASIC)
    print("      stfield on %s" % machines.Machine("ST:mono", d).describe())
    xvfb = start_xvfb()
    try:
        ok = run_scenario("stfield", d, ST_FIELD_STEPS, auto=LAUNCH, windowed=True, machine="ST:mono")
    finally:
        xvfb.terminate()
    return ok and check_inf(os.path.join(d, "EHYPVIEW.INF"), ["PATHS=" + LONG])


# the dialogs' look (guidelines 5.6): 3D wherever the AES draws it (TOS 4,
# EmuTOS in colour), flat on TOS 1 to 3 (and EmuTOS in fewer colours),
# with no setting for it (27 Oct): an EHYPVIEW.INF that still has
# DIALOGS3D=0 is read without it, and the first save leaves it out. The
# screenshots show Settings' two pages (its pop-up open over its button,
# 5.7), the Font dialog and an alert (look-01, -02, -03, -05, -08)
LOOK_STEPS = [
    ([], ["menu shown", "page %s Main" % BASIC, "index 5 guides"]),
    ([item(OPTIONS, 0)], ["menu settings", "dialog settings *", "settings-look *"]),
    (in_settings(("click",) + SET_LIBRARY), ["settings-page library"]),
    (in_settings(("click",) + SET_LAYOUT), ["menu settings-layout"]),
    ([ESC], []),                                        # the pop-up closed, nothing chosen
    ([UNDO], ["dialog-closed settings cancel"]),        # (Esc would empty a folder field)
    ([("ctrl+shift", F)], ["dialog font"]),
    ([ESC], ["dialog-closed font cancel"]),
    ([("ctrl", L)], ["library *"]),
    ([HELP], ["alert no-guide"]),                       # the library's Help: no EHYPVIEW.HYP here
    ([RETURN, ("ctrl", Q)], ["quit"]),
]


def look():
    d = copy_dir("look", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\n",
                 "; the dialogs' look\nDIALOGS3D=0\n", start=BASIC)
    xvfb = start_xvfb()
    try:
        ok = run_scenario("look", d, LOOK_STEPS, auto=LAUNCH, windowed=True)
    finally:
        xvfb.terminate()
    with open(os.path.join(OUT, "look.log"), errors="replace") as f:
        flat = "EHV settings-look no-3d" in f.read()
    print("      the AES %s" % ("draws no 3D" if flat else "draws 3D"))
    return ok and check_inf(os.path.join(d, "EHYPVIEW.INF"), ["!DIALOGS3D"])


# the Subjects pane's bar (guidelines 5.1: AES objects, common/ui/panebar.c):
# twenty hypertexts, each its own subject, more than the pane shows; a
# click on the bar's down arrow scrolls it a line, one on its track below
# the slider a page, and the line chosen stays. Its screenshots show the
# bar (panebar-01, after the scroll -03); run on ST:mono, Falcon:16 (3D)
# and under XaAES for the hub
def bar_click(where):
    """a click on the bar ("library-bar X Y W H"): its down arrow, or
    its track just above the down arrow"""
    def step(rig):
        at = [ev.split()[1:5] for ev in rig.events() if ev.startswith("library-bar ")]
        if not at:
            return False
        x, y, w, h = (int(v) for v in at[-1])
        skew = ST_SKEW if MACHINE.startswith("ST") else 0
        rig.press(("click", x + w // 2 - skew, (y + h - w // 2 if where == "down" else y + h * 3 // 4) - skew))
        return True
    return step


# on the ST in mono the Atari's pointer lands 19 pixels right of and below
# where xdotool puts it in Hatari's window (measured: a click sent to
# 60,180 came at 79,199), an arrow box's height; the bar's clicks allow
# for it
ST_SKEW = 19

PANEBAR_STEPS = [
    ([], ["library *", "library-layout panes", "library-bar *", "index 20 guides"]),
    ([("home",), ("hover", 620, 380), ("still", 5)], []),   # (the pointers in step)
    (bar_click("down"), ["pane-top 1"]),
    (bar_click("track"), ["pane-top *"]),
    ([("hover", 620, 380), ("still", 5)], []),
    ([("ctrl", Q)], ["quit"]),
]


def panebar():
    d = copy_dir("panebar", "PATHS=C:\\GUIDES\n", "; the layout\nLIBRARY=panes\n")
    shutil.rmtree(os.path.join(d, "FIXT"))
    os.makedirs(os.path.join(d, "GUIDES"))
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures", "colour.hyp"), "rb") as f:
        hyp = f.read()
    assert hyp.count(b"Test/EHypView") == 1
    for i in range(20):
        with open(os.path.join(d, "GUIDES", "S%02d.HYP" % i), "wb") as f:
            f.write(hyp.replace(b"Test/EHypView", b"Sub%02d/EHypVie" % i))   # as long
    xvfb = start_xvfb()
    try:
        return run_scenario("panebar", d, PANEBAR_STEPS, windowed=True)
    finally:
        xvfb.terminate()


# one click, one step (guidelines 5.1, 28 Oct 2026): the AES sent a click
# on a window's track or arrow twice, so the window moved two pages a
# click (Erez: "still jumping two positions"); TOS 4.04 every click, TOS
# 1.04 now and then (ClaudeST); common's evnt_multi_ drops the echo. On
# the Falcon-4 with TOS 4.04 and the ST in mono with TOS 1.04: BIG.HYP,
# whose window must fit the screen (wind_x8 had moved it 7 pixels past
# the right edge); a click held 120 ms on the viewer's track below the
# slider moves one page, one on its down arrow one line, one held 20 ms
# on the track one page more, each with one WM_ARROWED ("arrowed WHAT TOP")
ARROW_MACHINES = ["Falcon-4:16", "ST:mono"]
BIG = r"C:\FIXT\BIG.HYP"


def window_fits(rig):
    """the viewer's window ("window-at X Y W H") inside the screen"""
    scr = [ev.split()[1] for ev in rig.events() if ev.startswith("screen ")]
    at = [ev.split()[1:5] for ev in rig.events() if ev.startswith("window-at ")]
    if not scr or not at:
        return False
    width = int(scr[-1].split("x")[0])
    x, y, w, h = (int(v) for v in at[-1])
    ok = x >= 0 and x + w <= width
    print("%s  the window %d..%d across, the screen %d" % ("ok  " if ok else "FAIL", x, x + w - 1, width))
    return ok


def arrow_click(where, ms, skew):
    """a click held ms on the viewer's track ("window-at X Y W H": its
    right edge, three quarters down) or its down arrow (above the sizer)"""
    def step(rig):
        at = [ev.split()[1:5] for ev in rig.events() if ev.startswith("window-at ")]
        if not at:
            return False
        x, y, w, h = (int(v) for v in at[-1])
        cell = 19                                   # an arrow box, the 8x16 font
        ay = y + h * 3 // 4 if where == "track" else y + h - cell - cell // 2
        rig.press(("hold", x + w - cell // 2 - skew, ay - skew, ms))
        return True
    return step


def arrow_tops(rig):
    """the three steps: a page, a line, a page"""
    tops = [int(ev.split()[2]) for ev in rig.events() if ev.startswith("arrowed ")]
    ok = len(tops) == 3 and tops[0] > 1 and tops[1] == tops[0] + 1 and tops[2] == tops[1] + tops[0]
    print("%s  tops after each click: %s (a page, a line, a page)" % ("ok  " if ok else "FAIL", tops))
    return ok


def arrow_steps(machine):
    skew = ST_SKEW if machine.startswith("ST") else 0
    return [
        ([], ["window-at *", "page %s Main" % BIG]),
        (window_fits, []),
        ([("home",), ("hover", 320, 300), ("still", 5)], []),   # (the pointers in step)
        (arrow_click("track", 120, skew), ["arrowed dnpage *", "!arrowed *"]),
        (arrow_click("down", 120, skew), ["arrowed dnline *", "!arrowed *"]),
        (arrow_click("track", 20, skew), ["arrowed dnpage *", "!arrowed *"]),
        (arrow_tops, []),
        ([("ctrl", Q)], ["quit"]),
    ]


def arrowed():
    ok = True
    for machine in ARROW_MACHINES:
        name = "arrowed-" + machine.split(":")[0].lower()
        d = copy_dir(name, "PATHS=C:\\FIXT\n", start=BIG)
        print("      %s on %s" % (name, machine and machines.Machine(machine, d).describe()))
        xvfb = start_xvfb()
        try:
            ok = run_scenario(name, d, arrow_steps(machine), auto=LAUNCH, windowed=True, machine=machine) and ok
        finally:
            xvfb.terminate()
    return ok


# hypertexts filed under other subjects (Erez, 27 Oct 2026; the index is
# where a hypertext belongs): the twenty of the pane bar's run, each its
# own subject, SubNN/EHypVie. A right click on a row chooses it and shows
# its menu, never opens it; S00 dragged onto Sub01 is filed there; Sub01
# renamed tools through its menu (Esc empties the name first); the edge
# between the panes dragged wider (PANECOLS=); the index written again
# holds the new subject. Screenshots refile-NN on Falcon:16
REFILE_STEPS = [
    ([], ["library *", "library-layout panes", "library-bar *", "index 20 guides"]),
    ([("home",), ("hover", 620, 380), ("still", 3)], []),   # (off the menu bar)
    ([("rclick", 330, 183)], ["menu row", "!page *"]),
    ([ESC], ["!page *"]),
    ([("drag", 330, 183, 60, 204)], [r"refiled C:\GUIDES\S00.HYP Sub01/EHypVie", "index saved edited"]),
    ([("rclick", 60, 160)], ["menu subject"]),
    ([("click", 100, 171)], ["dialog ask *"]),
    ([ESC] + typed("tools") + [RETURN], ["dialog-closed ask tools", r"refiled C:\GUIDES\S00.HYP tools/EHypVie",
                                         r"refiled C:\GUIDES\S01.HYP tools/EHypVie", "index saved edited"]),
    ([("drag", 224, 300, 304, 300)], ["pane-cols *"]),
    ([("hover", 620, 470), ("still", 5)], []),
    ([("ctrl", Q)], ["quit"]),
]


def refile():
    d = copy_dir("refile", "PATHS=C:\\GUIDES\n", "; the layout\nLIBRARY=panes\n")
    shutil.rmtree(os.path.join(d, "FIXT"))
    os.makedirs(os.path.join(d, "GUIDES"))
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures", "colour.hyp"), "rb") as f:
        hyp = f.read()
    for i in range(20):
        with open(os.path.join(d, "GUIDES", "S%02d.HYP" % i), "wb") as f:
            f.write(hyp.replace(b"Test/EHypView", b"Sub%02d/EHypVie" % i))
    xvfb = start_xvfb()
    try:
        ok = run_scenario("refile", d, REFILE_STEPS, windowed=True)
    finally:
        xvfb.terminate()
    with open(os.path.join(d, "EHYPVIEW.IDX"), "rb") as f:
        held = f.read().count(b"tools/EHypVie")
    print("%s  EHYPVIEW.IDX files %d hypertexts under tools/EHypVie" % ("ok  " if held == 2 else "FAIL", held))
    return ok and held == 2 and check_inf(os.path.join(d, "EHYPVIEW.INF"), ["PANECOLS=*"])


# the README's pictures (make shots): the family's guides from their latest
# releases and EHypView's own in C:\\GUIDES, on the Falcon in 256 colours
# (Erez, 28 Oct 2026: the pictures shown are the 256-colour ones; MACHINE=
# another); each step's screenshot named in SHOT_FILES goes to docs/
GUIDE = r"C:\GUIDES\EHYPVIEW.HYP"
SHOT_MACHINE = os.environ.get("MACHINE", "Falcon:256")
FAMILY = [("ECal", "ECAL"), ("EContact", "ECONTACT"), ("EMail", "EMAIL"), ("EFTP", "EFTP"),
          ("ClaudeST", "CLAUDEST")]
LONGDIR = r"C:\HYPTEXT\PROGRAMS\DEVELOP\GEMLIB\MANUALS\ENGLISH"


def to_pane(subject):
    """Down through the subjects till this one is chosen"""
    def step(rig):
        for _ in range(8):
            rig.press(DOWN)
            if rig.wait_for("pane %s *" % subject, 3):
                return True
        return False
    return step


SHOT_STEPS = [
    ([], ["menu shown", "page %s Main" % GUIDE, "index 6 guides"]),
    ([("ctrl", L)], ["library 6"]),
    ([("type", "viewer")], ['search "viewer" *']),
    ([RETURN], ["page %s The viewer" % GUIDE]),
    ([("hover", 620, 470), ("still", 10)], []),                       # the help's WINDOW.IMG
    ([("hover", 20, 90), ("still", 10)], []),                         # toolbar.png: Back's tip
    ([("ctrl", L)], ["library 6"]),
    ([BACKSPACE] * 6, ['search "" 6']),
    ([DOWN] * 3 + [("hover", 330, 250), ("still", 10)], []),          # screenshot.png: EMail
    ([("type", "remind")], ['search "remind" *']),
    ([("hover", 330, 250), ("still", 10)], []),                       # library.png
    ([BACKSPACE] * 6, ['search "" 6']),
    ([item(OPTIONS, 0)], ["menu settings", "dialog settings *"]),
    (in_settings(("click",) + SET_LIBRARY), ["settings-page library"]),
    (in_settings(*([("click",) + SET_FIELD] + typed(LONGDIR))), []),
    ([("hover", 620, 470), ("still", 10)], []),                       # settings.png
    (in_settings(("click",) + SET_LAYOUT), ["menu settings-layout"]),
    (in_settings(("click",) + SET_LAYOUT_PANES), ["settings-layout panes"]),
    ([RETURN], ["dialog-closed settings ok", "settings PATHS " + LONGDIR, "library-layout panes",
                "index 6 guides"]),
    ([LEFT], ["library-focus subjects"]),
    (to_pane("Communication"), []),
    ([RETURN, DOWN, ("hover", 330, 250), ("still", 10)], []),         # library-panes.png
    ([("ctrl", Q)], ["quit"]),
]
SHOT_FILES = {5: "toolbar.png", 8: "screenshot.png", 10: "library.png", 15: "settings.png",
              21: "library-panes.png"}
WINDOW_SHOT = 4                 # made into docs/help/WINDOW.IMG (helpfront.py thumb, as CLAUDE.md says)


def shots():
    import shutil
    d = copy_dir("shots", "", "BOOKMARK1=C:\\GUIDES\\ECONTACT.HYP|Groups|EContact - Groups\n"
                 "RECENT1=%s|Main|EHypView - hypertexts for the Atari\n"
                 "RECENT2=C:\\GUIDES\\ECAL.HYP|Reminders|ECal - Reminders\n" % GUIDE, start=GUIDE)
    shutil.rmtree(os.path.join(d, "FIXT"))
    g = os.path.join(d, "GUIDES")
    os.makedirs(g)
    os.makedirs(os.path.join(d, *LONGDIR[3:].split("\\")))
    tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common", "tools", "getrelease.py")
    for app, base in FAMILY:
        for f in (base + ".HYP", base + ".REF"):          # EFTP's release has no .REF
            subprocess.run([sys.executable, tool, app, g, f], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
    for f in ("EHYPVIEW.HYP", "EHYPVIEW.REF"):
        shutil.copy(os.path.join("binary", f), g)
    for f in os.listdir(g):
        if f.endswith(".VERSION"):
            os.remove(os.path.join(g, f))
    xvfb = start_xvfb()
    try:
        ok = run_scenario("shots", d, SHOT_STEPS, auto=LAUNCH, windowed=True, machine=SHOT_MACHINE)
    finally:
        xvfb.terminate()
    if ok:
        for n, name in SHOT_FILES.items():
            shutil.copy(os.path.join(OUT, "shots-%02d.png" % n), os.path.join("docs", name))
            print("      docs/%s: shots-%02d.png" % (name, n))
        helpfront = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common", "tools",
                                 "helpfront.py")
        ok = subprocess.run([sys.executable, helpfront, "thumb", os.path.join(OUT, "shots-%02d.png" % WINDOW_SHOT),
                             "docs/help/WINDOW.IMG", "4,36,632,410", "32,90", "210,68", "330,206", "412,90",
                             "596,90"]).returncode == 0
        print("      docs/help/WINDOW.IMG: shots-%02d.png" % WINDOW_SHOT)
        if os.path.exists("docs/help/WINDOW.png"):           # thumb's preview, not committed
            os.remove("docs/help/WINDOW.png")
    return ok


def main():
    os.makedirs(OUT, exist_ok=True)
    if sys.argv[3:] == ["shots"]:                   # make shots: the README's pictures
        return 0 if shots() else 1
    if sys.argv[3:] == ["arrowed"]:                 # one click, one step alone
        return 0 if arrowed() else 1
    if sys.argv[3:] == ["stfield"]:                 # the ST's long field alone
        return 0 if st_field() else 1
    if sys.argv[3:] == ["look"]:                    # the dialogs' look alone (5.6)
        return 0 if look() else 1
    if sys.argv[3:] == ["refile"]:                  # hypertexts filed under other subjects alone
        return 0 if refile() else 1
    if sys.argv[3:4] == ["panebar"]:                # the Subjects pane's bar alone (5.1)
        return 0 if panebar() else 1
    if sys.argv[3:] == ["accnvdi"]:                 # make accnvdi-test: the NVDI case alone
        print("      on %s" % machines.Machine(MACHINE, os.path.join(OUT, "accnvdi")).describe())
        return 0 if acc_nvdi() else 1
    print("      on %s" % machines.Machine(MACHINE, os.path.join(OUT, "viewer")).describe())
    d = copy_dir("viewer", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\n", start=BASIC)
    ok = run_scenario("viewer", d, STEPS, auto=LAUNCH)
    if ok:
        ok = check_inf(os.path.join(d, "EHYPVIEW.INF"), ["FONT=System font", "POINT=8", "FOLDER=C:\\",
                                                          "BOOKMARK1="])   # added, then removed
    if ok:
        # DEFHYP is ST-Guide's: EHypView starts in its library all the same
        d = copy_dir("libstart", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\nDEFHYP=C:\\FIXT\\BASIC.HYP\n",
                     "FONT=System font\nPOINT=8\n"
                     "BOOKMARK1=C:\\FIXT\\SUB\\DEEP\\OTHER.HYP|Detail|Other test - Detail\n")
        ok = run_scenario("libstart", d, LIBRARY_START)
    if ok:
        d = copy_dir("outline", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\n",
                     "; the font\nFONT=System font\n; its size\nPOINT=8\n; a page read lately\n"
                     "RECENT1=C:\\FIXT\\SUB\\DEEP\\OTHER.HYP|Detail|Other test - Detail\n")
        ok = run_scenario("outline", d, OUTLINE_STEPS)
    if ok:
        d = copy_dir("panes", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\n",
                     "; the font\nFONT=System font\n; its size\nPOINT=8\n; the layout\nLIBRARY=panes\n"
                     "; a page read lately\nRECENT1=C:\\FIXT\\SUB\\DEEP\\OTHER.HYP|Detail|Other test - Detail\n")
        ok = run_scenario("panes", d, PANES_STEPS)
        ok = ok and check_inf(os.path.join(d, "EHYPVIEW.INF"),
                              ["LIBRARY=panes", "RECENT1=C:\\FIXT\\BASIC.HYP|Main|*",
                               "RECENT2=C:\\FIXT\\SUB\\DEEP\\OTHER.HYP|Detail|Other test - Detail"])
    if ok:
        if have_display():
            d = copy_dir("mouse", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\n", start=BASIC)
            xvfb = start_xvfb()
            try:
                ok = run_scenario("mouse", d, MOUSE_STEPS, auto=LAUNCH, windowed=True)
            finally:
                xvfb.terminate()
            ok = ok and check_inf(os.path.join(d, "EHYPVIEW.INF"),
                                  ["START=now", "PATHS=C:\\FIXT\\SUB;C:\\NOWHERE;" + LONG, "LIBRARY=panes"])
            ok = ok and st_field()
            ok = ok and look()
            ok = ok and panebar()
            ok = ok and refile()
            ok = ok and arrowed()
        else:
            print("skip  mouse: needs Xvfb and xdotool")
    if ok:
        if have_display() and os.path.exists(GUIDE):
            d = copy_dir("guide", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\n")
            os.makedirs(os.path.join(d, "GUIDES"))
            shutil.copy(GUIDE, os.path.join(d, "GUIDES"))
            shutil.copy(GUIDE[:-4] + ".REF", os.path.join(d, "GUIDES"))
            xvfb = start_xvfb()
            try:
                ok = run_scenario("guide", d, GUIDE_STEPS, windowed=True)
            finally:
                xvfb.terminate()
        else:
            print("skip  guide: needs Xvfb, xdotool and binary/EHYPVIEW.HYP (make help)")
    if ok:
        ok = accessory_smoke()
    if ok:
        if have_display():
            d = twin_dir()
            xvfb = start_xvfb()
            try:
                ok = run_scenario("twin", d, TWIN_STEPS, auto=TWIN_AUTO, windowed=True)
            finally:
                xvfb.terminate()
            ok = check_inf(os.path.join(d, "EHYPVIEW.INF"), ["FOLDER=C:\\EHV", "POINT=8"]) and ok
            ok = check_exists(os.path.join(d, "EHV", "EHYPVIEW.IDX"), "the program's index, in FOLDER=") and ok
        else:
            print("skip  twin: needs Xvfb and xdotool")
    if ok:
        d = copy_dir("fastram", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\n", start=IMAGES)
        ok = run_scenario("fastram", d, FASTRAM_STEPS, auto=LAUNCH, extra=FASTRAM)
    if ok:
        if have_display():
            d = acc_fastram_dir()
            xvfb = start_xvfb()
            try:
                ok = run_scenario("accfast", d, ACC_FASTRAM_STEPS, auto=None, windowed=True, extra=FASTRAM)
            finally:
                xvfb.terminate()
        else:
            print("skip  accfast: needs Xvfb and xdotool")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
