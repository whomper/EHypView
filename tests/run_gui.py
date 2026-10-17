#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""GUI test: run EHYPVIEW.PRG in Hatari (Falcon, EmuTOS) and drive it.

    tests/run_gui.py HATARI_DIR EMUTOS_IMAGE OUT_DIR

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

HDIR, EMUTOS, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
ACC_DIR = os.path.join(OUT, "acc")
BASIC = r"C:\FIXT\BASIC.HYP"
OTHER = r"C:\FIXT\SUB\DEEP\OTHER.HYP"

TAB, RETURN, BACKSPACE, HELP, UNDO, ESC = 15, 28, 14, 0x62, 0x61, 1
RIGHT, LEFT, Q, L, W, I, F, CTRL, SHIFT = 0x4D, 0x4B, 0x10, 0x26, 0x11, 0x17, 0x21, 29, 0x2A
MINUS, EQUALS = 0x0C, 0x0D
N, D, DELETE = 0x31, 0x20, 0x53
IMAGES = r"C:\FIXT\IMAGES\IMAGES.HYP"

# (keys to send, events expected afterwards, in order)
STEPS = [
    ([], ["font System font 16", "menu shown", "page %s Main" % BASIC]),  # DEFHYP
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
    ([LEFT, RETURN], ["dialog-closed font ok", "font System font 8"]),   # 8 x 8, kept in EHYPVIEW.INF
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
    # Esc clears no text (the guidelines, 3.5): the search is still "det",
    # and Backspace takes it out a letter at a time
    ([ESC], []),
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
    ([("ctrl", W)], ["closed viewer"]),
    ([("ctrl", W)], ["closed viewer"]),
    ([("ctrl", W)], ["closed viewer"]),
    ([("ctrl", W)], ["closed viewer"]),
    ([("ctrl", D)], ["bookmark added *"]),
    ([("ctrl", D)], ["status This page is bookmarked already."]),
    ([("ctrl", W)], ["closed viewer"]),                # the first window stays
    # with the menu bar the program stays when its windows are closed
    ([("ctrl", W)], ["closed viewer"]),
    ([("ctrl", W)], ["closed library"]),
    ([("ctrl", L)], ["library 4"]),
    ([BACKSPACE] * 6, ['search "" 4']),                # the search typed before, taken out:
    ([ESC], []),                                       # Esc with none: nothing, the window stays
    ([RETURN], ["page %s Index" % IMAGES]),            # the bookmark, first in the library
    ([("ctrl", L)], ["library 4"]),
    ([DELETE], ["bookmark removed *"]),                # Delete removes it
    ([("ctrl", Q)], ["quit"]),
]

# started without DEFHYP: the library opens, fills when the index is done
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


def start_hatari(hdir, auto, fifo, log_path, windowed=False, extra=()):
    for name in (fifo, os.path.join(hdir, "EHYPVIEW.IDX")):
        if os.path.exists(name):
            os.remove(name)
    if windowed:
        env = dict(os.environ, DISPLAY=DISPLAY, SDL_AUDIODRIVER="dummy")
    else:
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    args = ["hatari", "--tos", EMUTOS, "--machine", "falcon", "--monitor", "vga", "--memsize", "14",
            "--natfeats", "yes", "--sound", "off", "--confirm-quit", "no",
            "--statusbar", "no", "--log-level", "fatal", "--harddrive", hdir, "--gemdos-drive", "C",
            "--cmd-fifo", fifo] + list(extra)
    # in a window, one screen pixel is one window pixel for the mouse;
    # fast-forward would outrun the mouse moves
    args += ["-w", "--borders", "no", "--zoom", "1"] if windowed else ["--fast-forward", "yes"]
    if auto:
        args += ["--auto", auto]
    return subprocess.Popen(args, env=env, stdout=open(log_path, "w"), stderr=subprocess.STDOUT)


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


def run_scenario(name, hdir, steps, auto=r"C:\EHYPVIEW.PRG", windowed=False, extra=()):
    fifo = os.path.join(OUT, name + ".fifo")
    log_path = os.path.join(OUT, name + ".log")
    hatari = start_hatari(hdir, auto, fifo, log_path, windowed, extra)
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

    ok = True
    try:
        for n, (keys, expect) in enumerate(steps):
            for k in keys:
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
                elif isinstance(k, tuple) and k[0] == "wait":
                    time.sleep(k[1])
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
                if not wait_for(e):
                    print("FAIL  %s step %d: no '%s'" % (name, n, e))
                    print("      events so far: %s" % events())
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


def copy_dir(name, inf, settings=None):
    """A drive C: with the program (not the accessory), FIXT and an INF."""
    import shutil
    d = os.path.join(OUT, name)
    if os.path.exists(d):
        shutil.rmtree(d)
    shutil.copytree(os.path.join(HDIR, "FIXT"), os.path.join(d, "FIXT"))
    shutil.copy(os.path.join(HDIR, "EHYPVIEW.PRG"), d)
    with open(os.path.join(d, "ST-GUIDE.INF"), "w") as f:
        f.write(inf.replace("\n", "\r\n"))
    if settings is not None:
        with open(os.path.join(d, "EHYPVIEW.INF"), "w") as f:
            f.write(settings.replace("\n", "\r\n"))
    return d


# with the mouse (8x16 system font, Falcon VGA): a link, the right
# button's menu, and each menu (title x, y, then the item's x, y)
DESK, FILE, EDIT, VIEW, NAV, BOOK = (40, 9), (88, 9), (136, 9), (184, 9), (248, 9), (332, 9)


def item(title, n):
    """the n-th item (from 0) of the menu under title"""
    return ("menu", title[0], title[1], title[0] + 24, 19 + 16 * n + 8)


# the viewer's toolbar (src/gem/toolbar.c): the middle of each button, in
# the first window (its work area starts at x 27); the page starts 24
# pixels lower than before it; menus from a button open below it, their
# first item at y 111, the next 18 below
TB_Y = 90
TB_BACK, TB_FORWARD, TB_HOME = (43, TB_Y), (69, TB_Y), (95, TB_Y)
TB_PREV, TB_TOC, TB_NEXT = (125, TB_Y), (179, TB_Y), (233, TB_Y)
TB_INDEX, TB_SEEALSO, TB_LIBRARY, TB_BOOKMARKS, TB_MENU = (279, TB_Y), (349, TB_Y), (431, TB_Y), (513, TB_Y), (579, TB_Y)


def tb(button, kind="click"):
    return (kind, button[0], button[1])


MOUSE_STEPS = [
    ([], ["menu shown", "page %s Main" % BASIC, "index 5 guides"]),
    # the toolbar: Index, Back, Forward, the history on Back's right button
    ([tb(TB_INDEX)], ["toolbar index", "page %s Index" % BASIC]),
    ([tb(TB_BACK)], ["toolbar back", "page %s Main" % BASIC]),
    ([tb(TB_FORWARD)], ["toolbar forward", "page %s Index" % BASIC]),
    ([tb(TB_BACK, "rclick")], ["menu back-history"]),
    ([("click", 70, 111)], ["history -1", "page %s Main" % BASIC]),
    ([("shift", UNDO)], ["page %s Index" % BASIC]),        # Shift+Undo: forward
    ([BACKSPACE], ["page %s Main" % BASIC]),
    # See also: the page's @xref
    ([tb(TB_SEEALSO)], ["toolbar see-also", "menu see-also"]),
    ([("click", 340, 111)], ["see-also The help page", "page %s Help" % BASIC]),
    ([BACKSPACE], ["page %s Main" % BASIC]),
    ([("hover",) + TB_LIBRARY], ["tip library"]),           # a tooltip after a second
    # Bookmarks: add this page, then remove it
    ([tb(TB_BOOKMARKS)], ["toolbar bookmarks", "menu bookmarks"]),
    ([("click", 540, 111)], ["menu bookmark-add", "bookmark added *"]),
    ([tb(TB_BOOKMARKS)], ["toolbar bookmarks", "menu bookmarks"]),
    ([("click", 540, 129)], ["menu bookmark-remove", "bookmark removed *"]),
    # the menu button: About first (the accessory's way to it)
    ([tb(TB_MENU)], ["toolbar menu", "menu toolbar"]),
    ([("click", 600, 111)], ["menu about", "dialog about *"]),
    ([("click", 320, 300)], []),                            # a click closes it
    ([tb(TB_LIBRARY)], ["toolbar library", "library 4"]),
    ([("wait", 1)], []),                                    # (a picture of it)
    ([("type", "sec")], ['search "sec" *']),
    ([BACKSPACE] * 3, ['search "" 4']),
    ([("ctrl", W)], ["closed library"]),
    # the page: links, the right button's menu, pop-ups
    ([("click", 120, 142)], ["page %s Second" % BASIC]),    # "to Second"
    ([("rclick", 300, 174)], ["menu popup"]),               # right button: the menu,
    ([("click", 330, 185)], ["menu back", "page %s Main" % BASIC]),     # Back first
    ([("click", 120, 158)], ["popup Pop"]),                 # "popup here": the pop-up,
    ([("click", 500, 424)], ["popup-closed Pop"]),          # a click outside closes it
    ([("click", 120, 158)], ["popup Pop"]),                 # and its own link
    ([("click", 200, 199)], ["popup-link Pop", "page %s Second" % BASIC]),  # goes there
    ([BACKSPACE], ["page %s Main" % BASIC]),
    ([("rclick", 300, 174)], ["menu popup"]),               # and About last (rows of 18)
    ([("click", 330, 437)], ["menu about", "dialog about *"]),
    ([("click", 320, 300)], []),                            # a click closes it
    ([item(DESK, 0)], ["menu about", "dialog about *"]),    # the About box
    ([("click", 320, 300)], []),                            # a click closes it
    # several windows: Shift+click opens the link in a new one
    ([("sclick", 120, 142)], ["window new 2", "page %s Second" % BASIC]),
    ([item(BOOK, 0)], ["menu bookmark-add", "bookmark added *"]),
    ([("ctrl", W)], ["closed viewer"]),
    ([item(BOOK, 3)], ["menu bookmark", "page %s Second" % BASIC]),   # in the first window
    ([item(BOOK, 1)], ["menu bookmark-remove", "bookmark removed *"]),
    ([BACKSPACE], ["page %s Main" % BASIC]),
    ([("rclick", 120, 142)], ["menu popup"]),               # on a link, the menu has
    ([("click", 150, 189)], ["menu link-window", "window new 2", "page %s Second" % BASIC]),
    ([("ctrl", W)], ["closed viewer"]),
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
    ([item(FILE, 6)], ["menu quit", "quit"]),
]


# EHYPVIEW.PRG and EHYPVIEW.ACC (the AERA guidelines, 2.3, 6.1), under
# EmuTOS: the accessory starts quietly (nothing read until it is opened);
# the program, started from the
# desktop (C:\\EHV\\EHYPVIEW.PRG), writes C:\\EHYPVIEW.INF with FOLDER=; the
# accessory chosen meanwhile hands it to the program; the program makes
# the text smaller and quits; the accessory, opened, takes the new size
TWIN_STEPS = [
    ([], ["acc registered", "started *"]),
    ([("dclick", 195, 40), ("dclick", 60, 158), ("dclick", 60, 158)],    # DISK C, EHV, the program
     ["menu shown", "library 0", "index 5 guides"]),
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
    ([], ["pictures 3 st-ram", "page %s Main" % IMAGES]),    # laid out, then shown
    ([("ctrl", Q)], ["quit"]),
]
ACC_FASTRAM_STEPS = [
    ([], ["acc registered", "started *"]),
    # the desktop's Desk menu: the library, read once it is opened
    ([("wait", 6), ("click", 18, 8), ("click", 60, 64)], ["library 0", "index 5 guides"]),
    ([("type", "image")], ['search "image" 1']),
    ([RETURN], ["pictures 3 st-ram", "page %s Main" % IMAGES]),
]


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
    """EHYPVIEW.INF (the AERA guidelines, 6.1): these settings, CR LF line
    ends, and a ';' note above each"""
    try:
        with open(path, newline="") as f:
            got = f.read()
    except OSError as e:
        got = str(e)
    lines = got.split("\r\n")
    ok = got.endswith("\r\n") and "\n" not in got.replace("\r\n", "")
    ok = ok and all(s in lines for s in settings)
    ok = ok and all(i > 0 and lines[i - 1].startswith(";")
                    for i, l in enumerate(lines) if l and not l.startswith(";"))
    print("%s  %s: %r" % ("ok  " if ok else "FAIL", os.path.basename(path), got))
    return ok


def check_exists(path, what):
    ok = os.path.exists(path)
    print("%s  %s" % ("ok  " if ok else "FAIL", what))
    return ok


def main():
    os.makedirs(OUT, exist_ok=True)
    d = copy_dir("viewer", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\nDEFHYP=C:\\FIXT\\BASIC.HYP\n")
    ok = run_scenario("viewer", d, STEPS)
    if ok:
        ok = check_inf(os.path.join(d, "EHYPVIEW.INF"), ["FONT=System font", "POINT=8", "FOLDER=C:\\",
                                                          "BOOKMARK1="])   # added, then removed
    if ok:
        d = copy_dir("libstart", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\n",
                     "FONT=System font\nPOINT=8\n"
                     "BOOKMARK1=C:\\FIXT\\SUB\\DEEP\\OTHER.HYP|Detail|Other test - Detail\n")
        ok = run_scenario("libstart", d, LIBRARY_START)
    if ok:
        if have_display():
            d = copy_dir("mouse", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\nDEFHYP=C:\\FIXT\\BASIC.HYP\n")
            xvfb = subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1024x768x24"],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(1)
            try:
                ok = run_scenario("mouse", d, MOUSE_STEPS, windowed=True)
            finally:
                xvfb.terminate()
        else:
            print("skip  mouse: needs Xvfb and xdotool")
    if ok:
        ok = accessory_smoke()
    if ok:
        if have_display():
            d = twin_dir()
            xvfb = subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1024x768x24"],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(1)
            try:
                ok = run_scenario("twin", d, TWIN_STEPS, auto=None, windowed=True)
            finally:
                xvfb.terminate()
            ok = check_inf(os.path.join(d, "EHYPVIEW.INF"), ["FOLDER=C:\\EHV", "POINT=8"]) and ok
            ok = check_exists(os.path.join(d, "EHV", "EHYPVIEW.IDX"), "the program's index, in FOLDER=") and ok
        else:
            print("skip  twin: needs Xvfb and xdotool")
    if ok:
        d = copy_dir("fastram", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\nDEFHYP=%s\n" % IMAGES)
        ok = run_scenario("fastram", d, FASTRAM_STEPS, extra=FASTRAM)
    if ok:
        if have_display():
            d = acc_fastram_dir()
            xvfb = subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1024x768x24"],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(1)
            try:
                ok = run_scenario("accfast", d, ACC_FASTRAM_STEPS, auto=None, windowed=True, extra=FASTRAM)
            finally:
                xvfb.terminate()
        else:
            print("skip  accfast: needs Xvfb and xdotool")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
