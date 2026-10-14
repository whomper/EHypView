#!/usr/bin/env python3
"""GUI test: run EHYPVIEW.PRG in Hatari (Falcon, EmuTOS) and drive it.

    tests/run_gui.py HATARI_DIR EMUTOS_IMAGE OUT_DIR

HATARI_DIR holds EHYPVIEW.PRG, EHYPVIEW.ACC and the test folder FIXT; each
scenario gets its own drive C: under OUT_DIR with an ST-GUIDE.INF.  Keys go in through Hatari's command FIFO; the viewer
reports what it does through NatFeats ("EHV page ...", "EHV select ...")
and each step waits for the event it expects.  A screenshot of every
step is saved in OUT_DIR for people to look at.
"""
import os
import subprocess
import sys
import time

HDIR, EMUTOS, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
ACC_DIR = os.path.join(OUT, "acc")
BASIC = r"C:\FIXT\BASIC.HYP"
OTHER = r"C:\FIXT\SUB\DEEP\OTHER.HYP"

TAB, RETURN, BACKSPACE, HELP, UNDO, ESC = 15, 28, 14, 0x62, 0x61, 1
RIGHT, Q, L, CTRL = 0x4D, 0x10, 0x26, 29
IMAGES = r"C:\FIXT\IMAGES\IMAGES.HYP"

# (keys to send, events expected afterwards, in order)
STEPS = [
    ([], ["page %s Main" % BASIC]),                    # DEFHYP from ST-GUIDE.INF
    ([], ["index 5 guides"]),                          # background scan done
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
    ([("ctrl", Q)], ["quit"]),
]

# started without DEFHYP: the library opens, fills when the index is done
LIBRARY_START = [
    ([], ["library 0", "index 5 guides"]),
    (list("zweite"), ['search "zweite" 1']),
    ([RETURN], ["page %s Second" % BASIC]),
    ([("ctrl", Q)], ["quit"]),
]


def start_hatari(hdir, auto, fifo, log_path):
    for name in (fifo, os.path.join(hdir, "EHYPVIEW.IDX")):
        if os.path.exists(name):
            os.remove(name)
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    args = ["hatari", "--tos", EMUTOS, "--machine", "falcon", "--monitor", "vga", "--memsize", "14",
            "--natfeats", "yes", "--fast-forward", "yes", "--sound", "off", "--confirm-quit", "no",
            "--statusbar", "no", "--log-level", "fatal", "--harddrive", hdir, "--gemdos-drive", "C",
            "--cmd-fifo", fifo]
    if auto:
        args += ["--auto", auto]
    return subprocess.Popen(args, env=env, stdout=open(log_path, "w"), stderr=subprocess.STDOUT)


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


def run_scenario(name, hdir, steps, auto=r"C:\EHYPVIEW.PRG"):
    fifo = os.path.join(OUT, name + ".fifo")
    log_path = os.path.join(OUT, name + ".log")
    hatari = start_hatari(hdir, auto, fifo, log_path)
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
                if isinstance(k, tuple):
                    send("hatari-event keydown 0x%x" % CTRL)
                    send("hatari-event keypress 0x%x" % k[1])
                    send("hatari-event keyup 0x%x" % CTRL)
                elif isinstance(k, str):
                    send("hatari-event keypress %s" % k)
                else:
                    send("hatari-event keypress 0x%x" % k)
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
            print("ok    %s step %d: %s" % (name, n, expect[-1]))
    finally:
        send("hatari-debug quit")
        try:
            hatari.wait(10)
        except subprocess.TimeoutExpired:
            hatari.kill()
    return ok


def copy_dir(name, inf):
    """A drive C: with the program (not the accessory), FIXT and an INF."""
    import shutil
    d = os.path.join(OUT, name)
    if os.path.exists(d):
        shutil.rmtree(d)
    shutil.copytree(os.path.join(HDIR, "FIXT"), os.path.join(d, "FIXT"))
    shutil.copy(os.path.join(HDIR, "EHYPVIEW.PRG"), d)
    with open(os.path.join(d, "ST-GUIDE.INF"), "w") as f:
        f.write(inf.replace("\n", "\r\n"))
    return d


def main():
    os.makedirs(OUT, exist_ok=True)
    d = copy_dir("viewer", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\nDEFHYP=C:\\FIXT\\BASIC.HYP\n")
    ok = run_scenario("viewer", d, STEPS)
    if ok:
        d = copy_dir("libstart", "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\n")
        ok = run_scenario("libstart", d, LIBRARY_START)
    if ok:
        ok = accessory_smoke()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
