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

    tests/hatari/firebee_test.py HATARI_DIR FB_DIR WORK  (FB_DIR: the FireBee edition, build/fb)
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "common", "tools"))
import machines  # noqa: E402

HDIR, FBDIR, WORK = sys.argv[1], sys.argv[2], os.path.abspath(sys.argv[3])
failed = 0


def check(ok, what):
    global failed
    print("%s  %s" % ("ok  " if ok else "FAIL", what), flush=True)
    failed += not ok


def boot(name, spec, prg, acc=None):
    """the AES calls of a boot that starts prg from C:, one a line; acc: an
    accessory in C:'s root, which prg (ACOPEN.PRG) then opens"""
    w = os.path.join(WORK, name)
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(hd)
    for f in (prg, acc):
        if f:
            shutil.copy(os.path.join(FBDIR if f.startswith(("EHYPFB", "ACOPEN")) else HDIR, f), hd)
    if acc:
        with open(os.path.join(hd, "ACOPEN.TXT"), "w") as t:
            t.write(acc.split(".")[0] + "\r\n")
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
    for d, prg in ((HDIR, "EHYPVIEW.PRG"), (FBDIR, "EHYPFB.PRG")):
        if not os.path.exists(os.path.join(d, prg)):
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

    # the accessories: the boot reaches the desktop (ACOPEN.PRG runs from
    # it), the Desk entry is registered, and opening it shows the alert
    for spec, acc, words in (("Falcon:16", "EHYPFB.ACC", ("EHYPFB.ACC is for the FireBee", "EHYPVIEW.ACC in its place")),
                             ("FireBee:16", "EHYPVIEW.ACC", ("EHYPVIEW.ACC is for the ST", "EHYPFB.ACC in its place"))):
        m, calls = boot("acc-" + acc.split(".")[0].lower() + "-" + spec.split(":")[0].lower(), spec, "ACOPEN.PRG", acc)
        reg = [c for c in calls if " menu_register(" in c and "EHypView" in c]
        ran = any(" appl_write(" in c for c in calls)
        a = alerts(calls)
        check(reg and ran and len(a) == 1 and all(w in a[0] for w in words),
              "%s on %s: the boot reaches the desktop, its Desk entry, its alert when opened%s"
              % (acc, m.describe(), "" if a else " (registered %d, opened %s, %d AES calls)" % (len(reg), ran, len(calls))))

    print("%d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
