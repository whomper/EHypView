#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""No family accessory on a machine with under 2 MB of ST RAM (decided 26 Nov
2026; the guidelines, 2.1; common's acc_small_machine): on ST-1:mono, TOS
1.04, EHYPVIEW.ACC registers its Desk entry, takes nothing (no Malloc or
Mxalloc, no file opened, no workstation) and, opened from the Desk entry
(ACOPEN.PRG, tests/acopen.c), shows acc_small_alert's words and nothing
else; the boot reaches the desktop. On ST-2:mono (2 MB, the least the
accessories run on) the same boot shows no such alert and the accessory
starts as usual (its heap taken). Hatari's GEMDOS and AES traces show each
call, the accessory's by its program counter.

The held case (--held alone, or with the rest): on ST-2:mono the accessory's
memory is its own, never the program in front's (guidelines 2.2: common's
acc_core). The desktop starts one program after another: HELDFAST.PRG at
boot, which ends, then HELDSLOW.PRG (no fast-load bit: TOS clears its
memory), each filling all it is given (tests/heldfill.c), then HELDRUN.PRG:
ACOPEN.PRG opens the library and ASKHELP.PRG asks for a page. Both must
show, and no CRASH.LOG.

    tests/hatari/small_acc.py HATARI_DIR ACOPEN_PRG WORK [--held]
"""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "common", "tools"))
import machines  # noqa: E402

HDIR, ACOPEN, WORK = sys.argv[1], sys.argv[2], os.path.abspath(sys.argv[3])
HELD_ONLY = "--held" in sys.argv[4:]
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import natfeats  # noqa: E402
WORDS = "EHypView needs an Atari with 2 MB or more. Remove EHYPVIEW.ACC from the boot drive."
TAKES = ("Malloc", "Mxalloc", "Fopen", "Fcreate")
CALL = re.compile(r"^GEMDOS 0x[0-9A-Fa-f]+ \(?(\w+)\)?.*? at PC[= ]?0x([0-9A-Fa-f]+)")
failed = 0


def check(ok, what):
    global failed
    print("%s  %s" % ("ok  " if ok else "FAIL", what), flush=True)
    failed += not ok


def boot(name, spec):
    """the trace's lines of a boot with EHYPVIEW.ACC and ACOPEN.PRG, which
    the desktop starts and which opens the accessory"""
    w = os.path.join(WORK, name)
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(hd)
    shutil.copy(os.path.join(HDIR, "EHYPVIEW.ACC"), hd)
    shutil.copy(ACOPEN, os.path.join(hd, "ACOPEN.PRG"))
    m = machines.Machine(spec, hd)
    home = w + ".home"
    os.makedirs(home, exist_ok=True)
    env = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    trace = os.path.join(w, "trace.txt")
    try:
        subprocess.run(["hatari"] + m.args() + ["--natfeats", "yes", "--fast-forward", "yes", "--statusbar", "no",
                                                "--trace", "gemdos,aes", "--trace-file", trace,
                                                "--auto", "C:\\ACOPEN.PRG", "--run-vbls", "3000"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=240, env=env)
    except subprocess.TimeoutExpired:
        pass
    lines = open(trace, encoding="latin-1").read().splitlines() if os.path.exists(trace) else []
    return m, lines


def acc_range(lines):
    """the accessory's memory, from its Mshrink as TOS loads it"""
    for i, l in enumerate(lines):
        if "now loading 'EHYPVIEW.ACC'" in l:
            for k in lines[i:i + 10]:
                n = re.search(r"Mshrink\(0x([0-9A-Fa-f]+), 0x([0-9A-Fa-f]+)\)", k)
                if n:
                    lo = int(n.group(1), 16)
                    return lo, lo + int(n.group(2), 16)
    return None


def acc_calls(lines, rng):
    out = []
    for l in lines:
        n = CALL.match(l)
        if n and rng[0] <= int(n.group(2), 16) < rng[1]:
            out.append(n.group(1))
    return out


def held():
    """the library and a viewer after two programs in front used all
    their memory, one of them cleared by TOS"""
    w = os.path.join(WORK, "held")
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(os.path.join(hd, "GUIDES"))
    shutil.copy(os.path.join(HDIR, "EHYPVIEW.ACC"), hd)
    shutil.copy(ACOPEN, os.path.join(hd, "ACOPEN.PRG"))
    for f in ("ASKHELP.PRG", "HELDRUN.PRG", "HELDFAST.PRG", "HELDSLOW.PRG"):
        shutil.copy(os.path.join(HDIR, f), hd)
    fix = os.path.join(ROOT, "tests", "fixtures")
    for f in ("basic", "other", "big"):
        for e in (".hyp", ".ref"):
            shutil.copy(os.path.join(fix, f + e), os.path.join(hd, "GUIDES", f.upper() + e.upper()))
    with open(os.path.join(hd, "ehypview.inf"), "wb") as f:
        f.write(b"PATHS=C:\\GUIDES\r\n")
    natfeats.seed(hd)
    with open(os.path.join(hd, "ASKHELP.TXT"), "wb") as f:
        f.write(b"C:\\GUIDES\\BASIC.HYP Main\r\n5\r\n")
    m = machines.Machine("ST-2:mono", hd)
    home = w + ".home"
    os.makedirs(home, exist_ok=True)
    env = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    try:
        out = subprocess.run(["hatari"] + m.args() + ["--natfeats", "yes", "--fast-forward", "yes", "--statusbar", "no",
                                                      "--auto", "C:\\HELDFAST.PRG", "--run-vbls", "6000"],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                             timeout=400, env=env).stdout
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
    lines = out.splitlines()
    fills = [l for l in lines if l.startswith("HELDFILL filled ")]
    ran = [l for l in lines if l.startswith("HELDRUN ")]
    lib = [l for l in lines if l.startswith("EHV library ")]
    page = [l for l in lines if l.startswith("EHV page ") and "BASIC.HYP" in l.upper()]
    crash = [f for f in os.listdir(hd) if f.upper() == "CRASH.LOG"]
    sizes = [int(l.split()[-1]) // 1024 for l in fills]
    check(len(fills) == 2 and "HELDRUN failed" not in ran and "HELDRUN done" in ran and lib and page and not crash,
          "EHYPVIEW.ACC on %s, held: two programs in front filled %s KB (one with no fast-load bit), then its "
          "library and a viewer of BASIC.HYP%s"
          % (m.describe(), " and ".join(str(k) for k in sizes) or "nothing",
             "" if lib and page and not crash and len(fills) == 2 else
             " (fills %d, runs %r, library %d, page %d, CRASH.LOG %s)" % (len(fills), ran, len(lib), len(page),
                                                                          bool(crash))))


def main():
    if not shutil.which("hatari"):
        print("skip  small-acc: needs hatari")
        return 0
    for f in [os.path.join(HDIR, n) for n in ("EHYPVIEW.ACC", "ASKHELP.PRG", "HELDRUN.PRG", "HELDFAST.PRG",
                                               "HELDSLOW.PRG")] + [ACOPEN]:
        if not os.path.exists(f):
            print("FAIL  %s: not built" % f)
            return 1

    if HELD_ONLY:
        held()
        print("%d failed" % failed)
        return 1 if failed else 0

    m, lines = boot("st-1", "ST-1:mono")
    aes = [l for l in lines if l.startswith("AES ")]
    rng = acc_range(lines)
    calls = acc_calls(lines, rng) if rng else []
    reg = [c for c in aes if " menu_register(" in c and "EHypView" in c]
    ran = any(" appl_write(" in c for c in aes)
    alerts = [c.replace("|", " ") for c in aes if " form_alert(" in c]
    took = [c for c in calls if c in TAKES]
    check(rng is not None and reg and ran and len(alerts) == 1 and WORDS in alerts[0] and "[ OK ]" in alerts[0]
          and not took and not any(" wind_create(" in c for c in aes),
          "EHYPVIEW.ACC on %s: its Desk entry, the 2 MB alert when opened, nothing taken%s"
          % (m.describe(), "" if alerts and not took else
             " (registered %d, opened %s, alerts %r, its calls %s)" % (len(reg), ran, alerts, sorted(set(calls)))))

    m, lines = boot("st-2", "ST-2:mono")
    aes = [l for l in lines if l.startswith("AES ")]
    rng = acc_range(lines)
    calls = acc_calls(lines, rng) if rng else []
    alerts = [c for c in aes if " form_alert(" in c and "2 MB or more" in c]
    check(rng is not None and not alerts and "Malloc" in calls,
          "EHYPVIEW.ACC on %s: no 2 MB alert, its heap taken%s"
          % (m.describe(), "" if not alerts and "Malloc" in calls else " (its calls %s)" % sorted(set(calls))))

    held()

    print("%d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
