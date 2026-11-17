#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""gate.py HATARI_DIR OUT [ROW ...]: EHypView on the rows of the family's
regression gate (Erez, 17 Nov 2026; AERA-Atari standards/TEST-MATRIX.md,
the gate table; guidelines 1.2) that `make test`, `make gui-test`,
`make colour-test` and the others don't already boot, one boot a row, each
named by machines.py's describe(). With ROWs (the table's numbers), those
alone.

- 1-4, 7-9: the program on the machine with a page given on its command
  line, the index built, Settings opened and cancelled, the library opened
  and closed, quit: by keys, so it holds on any screen; no CRASH.LOG.
- 5, 6: common/tests/xaaes (FreeMiNT 1.17 and XaAES on EmuTOS, Erez's NVDI)
  on its Falcon, its TT and its 68060 (a CT60's stand-in), and once with
  the guides on a case-sensitive ext2 drive (6.6): the library, Settings,
  quit.
- 10: tests/hatari/acc_files.py on ST-1:mono (first and second boot; the
  Falcon's is make acc-files-test).
- 12: one boot with no NATFEATS= (the program says nothing through
  NatFeats then): LOG=1, so its own ehypview.log shows it started and
  quit, and no CRASH.LOG.
Row 11 (network) is not EHypView's: it connects to nothing."""
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
HDIR, OUT = sys.argv[1], sys.argv[2]
WANT = set(sys.argv[3:])
sys.argv = [sys.argv[0], HDIR, OUT]
sys.path.insert(0, HERE)
import run_gui as g  # noqa: E402
import machines  # noqa: E402

GUIDES = "PATHS=C:\\FIXT\nREF=C:\\FIXT\\ALL.REF\n"
SMOKE = [
    ([], ["menu shown", "page %s Main" % g.BASIC]),
    ([], ["index 5 guides"]),
    ([("ctrl", g.COMMA)], ["dialog settings *"]),
    ([g.ESC], ["dialog-closed settings cancel"]),
    ([("ctrl", g.L)], ["library *"]),
    ([("ctrl", g.U)], ["closed library"]),
    ([("ctrl", g.Q)], ["quit", "quit-step out of the AES"]),
]
# the rows the program boots on (a screen at least 640 wide: guidelines 5.2;
# ST low and TT low are the accessory's, colour-test's)
BOOTS = [
    ("1", "Falcon-4:256", True),
    ("2", "Falcon-4:16", False),
    ("3", "Falcon:65536", True),
    ("4", "TT:ttmed", False),
    ("4", "TT:mono", False),
    ("7", "FireBee:16", False),
    ("7", "FireBee:256", False),
    ("8", "ST:mono", False),
    ("8", "ST:med", False),
    ("8", "ST-1:mono", False),
    ("9", "STE:med", False),
]
report = []


def note(row, ok, what):
    report.append((row, ok, what))
    print("%s  row %s: %s" % ("ok  " if ok else "FAIL", row, what), flush=True)


def crash_log(d):
    return any(f.upper() == "CRASH.LOG" for _, _, fs in os.walk(d) for f in fs)


def with_nvdi(on):
    if on:
        os.environ["AERA_WITH_NVDI"] = "1"
    else:
        os.environ.pop("AERA_WITH_NVDI", None)


def boots():
    for row, spec, nvdi in BOOTS:
        if WANT and row not in WANT:
            continue
        with_nvdi(nvdi)
        name = "gate-%s" % spec.replace(":", "-").lower()
        d = g.copy_dir(name, GUIDES, start=g.BASIC)
        m = machines.Machine(spec, d)
        m.args()                                     # (NVDI in AUTO, when asked: describe() sees it)
        what = m.describe()
        ok = g.run_scenario(name, d, SMOKE, auto=g.LAUNCH, machine=spec)
        ok = ok and not crash_log(d)
        note(row, ok, what + ("" if ok else " (see %s)" % os.path.join(OUT, name + ".log")))
    with_nvdi(False)


def xaaes():
    run = os.path.join(ROOT, "common", "tests", "xaaes", "run.py")
    steps = ("until EHV menu shown;until EHV library;ctrl ,;until EHV dialog settings;key 0x01;"
             "until EHV dialog-closed settings;ctrl q;until EHV quit-step out of the AES")
    for row, mach, ext2 in (("5", "falcon", False), ("5", "tt", False), ("5", "falcon", True), ("6", "ct60", False)):
        if WANT and row not in WANT:
            continue
        name = "gate-xaaes-%s%s" % (mach, "-ext2" if ext2 else "")
        work = os.path.join(OUT, name)
        src = os.path.join(OUT, name + "-files")
        shutil.rmtree(work, ignore_errors=True)
        shutil.rmtree(src, ignore_errors=True)
        os.makedirs(src)
        guides = "D:\\FIXT" if ext2 else "C:\\FIXT"
        with open(os.path.join(src, "ST-GUIDE.INF"), "w", newline="") as f:
            f.write("PATHS=%s\r\nREF=%s\\ALL.REF\r\n" % (guides, guides))
        with open(os.path.join(src, "ehypview.inf"), "w", newline="") as f:
            f.write("; NatFeats for the test rigs (written by the rigs only)\r\nNATFEATS=1\r\n")
        args = ["python3", run, work, machines.emutos(), os.path.join(HDIR, "EHYPVIEW.PRG"),
                "--copy", os.path.join(src, "ST-GUIDE.INF"), "--copy", os.path.join(src, "ehypview.inf"),
                "--nvdi", "--machine", mach, "--steps", steps]
        if ext2:
            os.makedirs(os.path.join(src, "ext2"))
            shutil.copytree(os.path.join(HDIR, "FIXT"), os.path.join(src, "ext2", "FIXT"))
            args += ["--ext2", os.path.join(src, "ext2")]
        else:
            args += ["--copy", os.path.join(HDIR, "FIXT")]
        r = subprocess.run(args, capture_output=True, text=True)
        out = os.path.join(work, "out")
        ok = r.returncode == 0 and not crash_log(out)
        what = "FreeMiNT 1.17 + XaAES on EmuTOS 1.4, NVDI, run.py --machine %s%s" % (
            mach, " with the guides on an ext2 drive" if ext2 else "")
        note(row, ok, what + ("" if ok else ": " + (r.stdout + r.stderr).strip().splitlines()[-1:][0]
                               if (r.stdout + r.stderr).strip() else ""))
    note("6", True, "plain TOS on a 68060: machines.py has no such machine; the 68060 runs as XaAES's "
         "ct60 (above) and as the FireBee stand-in (row 7)") if not WANT or "6" in WANT else None


def accessory():
    if WANT and "10" not in WANT:
        return
    out = os.path.join(OUT, "gate-acc-st1")
    r = subprocess.run(["python3", os.path.join(HERE, "hatari", "acc_files.py"), HDIR, out, "ST-1:mono"],
                       capture_output=True, text=True)
    ok = r.returncode == 0
    with open(out + ".log", "w") as f:
        f.write(r.stdout + r.stderr)
    note("10", ok, "the accessory's first and second boot, " + machines.Machine("ST-1:mono", out).describe() +
         ("" if ok else " (see %s.log)" % out))


def natfeats_off():
    """started, quit, no CRASH.LOG, and nothing said through NatFeats"""
    if WANT and "12" not in WANT:
        return
    with_nvdi(True)
    spec = machines.ERZ_MACHINE
    d = g.copy_dir("gate-nfoff", GUIDES, settings="; Write a log\nLOG=1\n")
    fifo = os.path.join(OUT, "gate-nfoff.fifo")
    log_path = os.path.join(OUT, "gate-nfoff.log")
    seed = g.natfeats.seed
    g.natfeats.seed = lambda drive: None             # this boot: no NATFEATS=
    m = machines.Machine(spec, d)
    m.args()
    what = m.describe()
    try:
        h = g.start_hatari(d, r"C:\EHYPVIEW.PRG", fifo, log_path, machine=spec)
    finally:
        g.natfeats.seed = seed

    def send(cmd):
        with open(fifo, "w") as f:
            f.write(cmd + "\n")

    def inf_has(text):
        p = machines.ci_path(d, "ehypview.inf")
        return os.path.exists(p) and text in open(p, errors="replace").read()

    def quit_logged():
        p = machines.ci_path(d, "ehypview.log")
        return os.path.exists(p) and "out of the AES" in open(p, errors="replace").read()

    ok = False
    end = time.time() + 600
    while time.time() < end and not inf_has("FOLDER="):  # it started: its folder written
        time.sleep(2)
    started = inf_has("FOLDER=")
    if started:
        time.sleep(20)
        send("hatari-event keydown 29")
        send("hatari-event keypress 0x%x" % g.Q)
        send("hatari-event keyup 29")
        end = time.time() + 300
        while time.time() < end and not quit_logged():
            time.sleep(2)
        ok = quit_logged()
    h.terminate()
    h.wait()
    said = "EHV " in open(log_path, errors="replace").read()
    ok = ok and not said and not crash_log(d)
    note("12", ok, "no NATFEATS=: started, quit (its ehypview.log), no CRASH.LOG, nothing through NatFeats, " + what +
         ("" if ok else " (started %s, quit %s, said %s)" % (started, quit_logged(), said)))
    with_nvdi(False)


def main():
    os.makedirs(OUT, exist_ok=True)
    xvfb = None
    boots()
    xaaes()
    accessory()
    natfeats_off()
    if xvfb:
        xvfb.terminate()
    print()
    for row, ok, what in report:
        print("%s  %s: %s" % ("ok  " if ok else "FAIL", row, what))
    return 0 if all(ok for _, ok, _ in report) else 1


if __name__ == "__main__":
    sys.exit(main())
