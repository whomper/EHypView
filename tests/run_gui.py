#!/usr/bin/env python3
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
IMAGES = r"C:\FIXT\IMAGES\IMAGES.HYP"

# (keys to send, events expected afterwards, in order)
STEPS = [
    ([], ["font System font 16", "menu shown", "page %s Main" % BASIC]),  # DEFHYP
    ([], ["index 5 guides"]),                          # background scan done
    # the font: sizes with Control+- and Control+=, then the selector
    ([("ctrl", MINUS)], ["font System font 8"]),
    ([("ctrl", EQUALS)], ["font System font 16"]),
    ([("ctrl", EQUALS)], ["status No larger size"]),
    ([("ctrl+shift", F)], ["dialog font"]),
    ([LEFT, RETURN], ["font System font 8"]),          # 8 x 8, kept in EHYPVIEW.INF
    ([TAB], ["select 2"]),                             # first link
    ([RETURN], ["page %s Second" % BASIC]),
    ([BACKSPACE], ["page %s Main" % BASIC]),
    ([TAB] * 4, ["select 2", "select 2", "select 3", "select 4"]),
    ([RETURN], ["page %s Start" % OTHER]),             # external link, three folders down
    ([HELP], ["status This hypertext has no help page"]),
    ([("ctrl", RIGHT)], ["page %s Detail" % OTHER]),   # next page
    ([UNDO], ["page %s Start" % OTHER]),               # back
    # the library: 4 readable hypertexts (BROKEN.HYP is left out)
    ([("ctrl", L)], ["library 4"]),
    (list("det"), ['search "det" 1']),                 # page names as you type
    ([RETURN], ["page %s Detail" % OTHER]),
    ([("ctrl", L)], ["library 4"]),
    ([ESC], ['search "" 4']),
    (list("beside"), ['search "beside" 0']),
    ([RETURN], ['fulltext-start "beside"', 'fulltext "beside" 1']),
    ([RETURN], ["page %s Main" % IMAGES]),             # the full-text hit
    ([("ctrl", I)], ["page %s Index" % IMAGES]),       # hcp made an index
    # with the menu bar the program stays when its windows are closed
    ([("ctrl", W)], ["closed viewer"]),
    ([("ctrl", W)], ["closed library"]),
    ([("ctrl", L)], ["library 4"]),
    ([("ctrl", Q)], ["quit"]),
]

# started without DEFHYP: the library opens, fills when the index is done
# (and with the 8 x 8 font kept in EHYPVIEW.INF)
LIBRARY_START = [
    ([], ["font System font 8", "library 0", "index 5 guides"]),
    (list("zweite"), ['search "zweite" 1']),
    ([RETURN], ["page %s Second" % BASIC]),
    ([("ctrl", Q)], ["quit"]),
]


DISPLAY = ":99"


def start_hatari(hdir, auto, fifo, log_path, windowed=False):
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
            "--cmd-fifo", fifo]
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
    """Boot with EHYPVIEW.ACC in the root: it must register and index."""
    import shutil
    if os.path.exists(ACC_DIR):
        shutil.rmtree(ACC_DIR)
    shutil.copytree(os.path.join(HDIR, "FIXT"), os.path.join(ACC_DIR, "FIXT"))
    shutil.copy(os.path.join(HDIR, "EHYPVIEW.ACC"), ACC_DIR)
    with open(os.path.join(ACC_DIR, "ST-GUIDE.INF"), "w") as f:
        f.write("PATHS=C:\\FIXT\r\nREF=C:\\FIXT\\ALL.REF\r\n")
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


def matches(ev, expected):
    if expected.endswith("*"):
        return ev.startswith(expected[:-1])
    return ev == expected


def run_scenario(name, hdir, steps, auto=r"C:\EHYPVIEW.PRG", windowed=False):
    fifo = os.path.join(OUT, name + ".fifo")
    log_path = os.path.join(OUT, name + ".log")
    hatari = start_hatari(hdir, auto, fifo, log_path, windowed)
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

    ok = True
    try:
        for n, (keys, expect) in enumerate(steps):
            for k in keys:
                if isinstance(k, tuple) and k[0] in ("click", "rclick"):
                    mouse.click(k[1], k[2], "1" if k[0] == "click" else "3")
                elif isinstance(k, tuple) and k[0] == "menu":
                    mouse.click(k[1], k[2])                # the title drops the menu down
                    mouse.click(k[3], k[4])                # the item
                elif isinstance(k, tuple):
                    mods = [CTRL] + ([SHIFT] if k[0] == "ctrl+shift" else [])
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
DESK, FILE, NAV, VIEW = (40, 9), (88, 9), (152, 9), (216, 9)


def item(title, n):
    """the n-th item (from 0) of the menu under title"""
    return ("menu", title[0], title[1], title[0] + 24, 19 + 16 * n + 8)


MOUSE_STEPS = [
    ([], ["menu shown", "page %s Main" % BASIC, "index 5 guides"]),
    ([("click", 120, 118)], ["page %s Second" % BASIC]),    # "to Second"
    ([("rclick", 300, 150)], ["menu popup"]),               # right button: the menu,
    ([("click", 330, 161)], ["menu back", "page %s Main" % BASIC]),     # Back first
    ([("rclick", 300, 150)], ["menu popup"]),               # and About last (rows of 18)
    ([("click", 330, 341)], ["menu about", "dialog about *"]),
    ([("click", 320, 300)], []),                            # a click closes it
    ([item(DESK, 0)], ["menu about", "dialog about *"]),    # the About box
    ([("click", 320, 300)], []),                            # a click closes it
    ([item(VIEW, 0)], ["menu font", "dialog font"]),        # the font selector
    ([ESC], []),
    ([item(NAV, 5)], ["menu help", "page %s Help" % BASIC]),
    ([item(NAV, 0)], ["menu back", "page %s Main" % BASIC]),
    ([item(FILE, 1)], ["menu library", "library 4"]),
    ([item(FILE, 5)], ["menu quit", "quit"]),
]


def check_file(path, expected):
    try:
        with open(path, newline="") as f:
            got = f.read()
    except OSError as e:
        got = str(e)
    ok = got == expected
    print("%s  %s: %r" % ("ok  " if ok else "FAIL", os.path.basename(path), got))
    return ok


def main():
    os.makedirs(OUT, exist_ok=True)
    d = copy_dir("viewer", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\nDEFHYP=C:\\FIXT\\BASIC.HYP\n")
    ok = run_scenario("viewer", d, STEPS)
    if ok:
        ok = check_file(os.path.join(d, "EHYPVIEW.INF"), "FONT=System font\r\nPOINT=8\r\n")
    if ok:
        d = copy_dir("libstart", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\n",
                     "FONT=System font\nPOINT=8\n")
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
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
