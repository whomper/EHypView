#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Each file names the other (the guidelines, 7.4; Erez, 22 Nov 2026: "Make
the other apps native on the FireBee too"): the 68000 build on the FireBee
stand-in (machines.py's FireBee: a 68060 Falcon with FireTOS's cookies, _CF_
among them) shows cpu_cf_alert naming EHYPFB.PRG and goes on to nothing
else; the FireBee build (ColdFire code, its first code cpucheck_cf.S, which
a 68000 runs alike) on a Falcon shows cpu_alert naming EHYPVIEW.PRG; the
68000 build on a Falcon starts as before. Hatari's AES trace shows each
call: the alert's text, and whether a window ever came.

    tests/hatari/firebee_test.py HATARI_DIR WORK
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "common", "tools"))
import machines  # noqa: E402

HDIR, WORK = sys.argv[1], os.path.abspath(sys.argv[2])
failed = 0


def check(ok, what):
    global failed
    print("%s  %s" % ("ok  " if ok else "FAIL", what), flush=True)
    failed += not ok


def boot(name, spec, prg):
    """the AES calls of a boot that starts prg from C:, one a line"""
    w = os.path.join(WORK, name)
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(hd)
    shutil.copy(os.path.join(HDIR, prg), hd)
    m = machines.Machine(spec, hd)
    args = m.args()
    home = w + ".home"
    os.makedirs(home, exist_ok=True)
    env = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    try:
        out = subprocess.run(["hatari"] + args + ["--natfeats", "yes", "--fast-forward", "yes", "--statusbar", "no",
                                                  "--trace", "aes", "--auto", "C:\\" + prg, "--run-vbls", "3000"],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                             timeout=240, env=env).stdout
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
    return m, [l for l in out.splitlines() if l.startswith("AES ")]


def alerts(calls):
    return [c for c in calls if " form_alert(" in c]


def windows(calls):
    return [c for c in calls if " wind_create(" in c or " wind_open(" in c]


def main():
    if not shutil.which("hatari"):
        print("skip  firebee: needs hatari")
        return 0
    for prg in ("EHYPVIEW.PRG", "EHYPFB.PRG"):
        if not os.path.exists(os.path.join(HDIR, prg)):
            print("FAIL  %s: not built (make atari)" % prg)
            return 1

    m, calls = boot("cf-68000", "FireBee:16", "EHYPVIEW.PRG")
    a = alerts(calls)
    check(len(a) == 1 and "EHYPVIEW.PRG is for the ST, TT" in a[0] and "run EHYPFB.PRG" in a[0] and not windows(calls),
          "EHYPVIEW.PRG on %s: its alert names EHYPFB.PRG, no window%s"
          % (m.describe(), "" if a else " (no alert: %d AES calls)" % len(calls)))

    m, calls = boot("falcon-fb", "Falcon-4:16", "EHYPFB.PRG")
    a = alerts(calls)
    check(len(a) == 1 and "EHYPFB.PRG is for the FireBee" in a[0] and "run EHYPVIEW.PRG" in a[0] and not windows(calls),
          "EHYPFB.PRG on %s: its alert names EHYPVIEW.PRG, no window%s"
          % (m.describe(), "" if a else " (no alert: %d AES calls)" % len(calls)))

    m, calls = boot("falcon-68000", "Falcon-4:16", "EHYPVIEW.PRG")
    check(not alerts(calls) and windows(calls),
          "EHYPVIEW.PRG on %s: no alert, its window" % m.describe())

    print("%d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
