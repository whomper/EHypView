#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Each build names the other package (the guidelines, 7.4; Erez, 22 Nov
2026: "Make the other apps native on the FireBee too", then two packages
with "Same names"): the standard build on the FireBee stand-in (machines.py's
FireBee: a 68060 Falcon with FireTOS's cookies, _CF_ among them) shows
cpu_cf_alert naming the FireBee package and goes on to nothing else; the
FireBee build (ColdFire code, its first code cpucheck_cf.S, which a 68000
runs alike) on a Falcon and an ST shows cpu_alert naming the standard
package; the standard build on a Falcon starts as before. Each accessory on
the wrong machine lets the boot reach the desktop, registers its Desk entry
and shows its alert when opened (ACOPEN.PRG, tests/acopen.c). Hatari's AES
trace shows each call: the alert's text, and whether a window ever came.

    tests/hatari/firebee_test.py HATARI_DIR FB_DIR WORK  (FB_DIR: the FireBee package's build, build/fb)
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "common", "tools"))
import machines  # noqa: E402

HDIR, FBDIR, WORK = sys.argv[1], sys.argv[2], os.path.abspath(sys.argv[3])
STD, FB = HDIR, FBDIR
# the stand-in's rows (FIREBEE_STANDIN=FireBee:16,FireBee:256 from the gate's
# row 7, tests/gate.py)
STANDIN = os.environ.get("FIREBEE_STANDIN", "FireBee:16").split(",")
failed = 0


def check(ok, what):
    global failed
    print("%s  %s" % ("ok  " if ok else "FAIL", what), flush=True)
    failed += not ok


def boot(name, spec, files, auto):
    """the AES calls of a boot with files ((dir, name)...) in C:'s root that
    starts C:\\auto, one a line"""
    w = os.path.join(WORK, name)
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(hd)
    for d, f in files:
        shutil.copy(os.path.join(d, f), hd)
    m = machines.Machine(spec, hd)
    args = m.args()
    home = w + ".home"
    os.makedirs(home, exist_ok=True)
    env = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    try:
        out = subprocess.run(["hatari"] + args + ["--natfeats", "yes", "--fast-forward", "yes", "--statusbar", "no",
                                                  "--trace", "aes", "--auto", "C:\\" + auto, "--run-vbls", "3000"],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                             timeout=240, env=env).stdout
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
    return m, [l for l in out.splitlines() if l.startswith("AES ")]


def alerts(calls):
    """the alerts' texts, their lines joined by spaces"""
    return [c.replace("|", " ") for c in calls if " form_alert(" in c]


def windows(calls):
    return [c for c in calls if " wind_create(" in c or " wind_open(" in c]


TO_FB = "is for the ST, TT and Falcon. On the FireBee, install EHypView's FireBee package."
TO_STD = "is for the FireBee. On an ST, a TT or a Falcon, install EHypView's standard package."


def main():
    if not shutil.which("hatari"):
        print("skip  firebee: needs hatari")
        return 0
    for d in (STD, FB):
        for f in ("EHYPVIEW.PRG", "EHYPVIEW.ACC"):
            if not os.path.exists(os.path.join(d, f)):
                print("FAIL  %s: not built (make atari)" % os.path.join(d, f))
                return 1

    # the programs: the alert at once, and nothing after it
    for name, spec, d, words in ([("std-" + s.replace(":", "-").lower(), s, STD, TO_FB) for s in STANDIN] +
                                 [("fb-falcon", "Falcon-4:16", FB, TO_STD),
                                  ("fb-st", "ST:mono", FB, TO_STD)]):
        m, calls = boot(name, spec, [(d, "EHYPVIEW.PRG")], "EHYPVIEW.PRG")
        a = alerts(calls)
        check(len(a) == 1 and "EHYPVIEW.PRG " + words in a[0] and "[ Quit ]" in a[0] and not windows(calls),
              "the %s EHYPVIEW.PRG on %s: its alert names the %s package, no window%s"
              % ("FireBee" if d == FB else "standard", m.describe(), "standard" if d == FB else "FireBee",
                 "" if a else " (no alert: %d AES calls)" % len(calls)))

    m, calls = boot("std-falcon", "Falcon-4:16", [(STD, "EHYPVIEW.PRG")], "EHYPVIEW.PRG")
    check(not alerts(calls) and windows(calls),
          "the standard EHYPVIEW.PRG on %s: no alert, its window" % m.describe())

    # the accessories: the boot reaches the desktop (ACOPEN.PRG runs from
    # it), the Desk entry is registered, and opening it shows the alert
    for name, spec, d, words in ([("acc-fb-falcon", "Falcon:16", FB, TO_STD),
                                  ("acc-fb-st", "ST:mono", FB, TO_STD)] +
                                 [("acc-std-" + s.replace(":", "-").lower(), s, STD, TO_FB) for s in STANDIN]):
        m, calls = boot(name, spec, [(d, "EHYPVIEW.ACC"), (FB, "ACOPEN.PRG")], "ACOPEN.PRG")
        reg = [c for c in calls if " menu_register(" in c and "EHypView" in c]
        ran = any(" appl_write(" in c for c in calls)
        a = alerts(calls)
        check(reg and ran and len(a) == 1 and "EHYPVIEW.ACC " + words in a[0] and "[ OK ]" in a[0],
              "the %s EHYPVIEW.ACC on %s: the boot reaches the desktop, its Desk entry, its alert when opened%s"
              % ("FireBee" if d == FB else "standard", m.describe(),
                 "" if a else " (registered %d, opened %s, %d AES calls)" % (len(reg), ran, len(calls))))

    print("%d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
