#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The accessory's disk use, job by job (the AERA guidelines, 6.5 and 2.2),
from Hatari's GEMDOS and AES trace:

    tests/hatari/acc_files.py DIR OUT [MACHINE]

DIR holds EHYPVIEW.ACC and ASKHELP.PRG (make acc-files-test: build/hatari).
MACHINE is common/tools/machines.py's, Falcon:16 by default (ST-1 for the
smallest ST; AERA_TOS picks the TOS as there). The library is the
family's six guides, each from its latest release (getrelease.py), in
C:\\LIB1, and two big ones in C:\\LIB2 (PATHS= in EHYPVIEW.INF).

Only the calls made from the accessory's own code count: those whose PC
is in its program, from the loader's Mshrink of its basepage. Hatari's
GEMDOS drive logs an Fsnext inside each Fsfirst; that one isn't counted.
Each scenario is a boot of its own:

  0. START=opened, never opened, a minute
  1. START=now, no EHYPVIEW.IDX: the quiet start and the first index
  2. the index up to date
  3. the same, then 5 minutes idle (ACC_FILES_IDLE seconds)
  4. one guide changed and one added, at the next boot
  5. a page asked for by VA_START, as the Help keys do
  6. (ACC_FILES_BIG=1) 60 guides of 112 pages: first and up to date

Each job's opens and looks must stay in its budget: about one open per
file the job touches, plus a safe save's rename; no file looked up one
by one where its folder's listing has it; no disk while idle; no file
open across an event call. The counts and the times go to OUT/report.txt
(the times on Hatari's GEMDOS drive, where no FAT is read: on a real
disk every open costs more).
"""
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
import machines  # noqa: E402

DIR, OUT = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
MACHINE = sys.argv[3] if len(sys.argv) > 3 else "Falcon:16"
FIX = os.path.join(ROOT, "tests", "fixtures")
IDLE = int(os.environ.get("ACC_FILES_IDLE", "300"))
FAMILY = [("ECal", "ECAL.HYP"), ("EContact", "ECONTACT.HYP"), ("EMail", "EMAIL.HYP"),
          ("EFTP", "EFTP.HYP"), ("ClaudeST", "CLAUDEST.HYP"), ("EHypView", "EHYPVIEW.HYP")]
C = os.path.join(OUT, "c")
fails = 0
report = []


def say(line):
    print(line)
    report.append(line)


def check(ok, what):
    global fails
    say(("ok    " if ok else "FAIL  ") + what)
    fails += not ok


def family():
    """the family's guides (and .REFs), from their releases, in OUT/family"""
    d = os.path.join(OUT, "family")
    os.makedirs(d, exist_ok=True)
    tool = os.path.join(ROOT, "common", "tools", "getrelease.py")
    got = []
    for app, hyp in FAMILY:
        if not os.path.exists(os.path.join(d, hyp)):
            subprocess.run([sys.executable, tool, app, d, hyp], capture_output=True, text=True)
            subprocess.run([sys.executable, tool, app, d, hyp[:-4] + ".REF"], capture_output=True, text=True)
        if os.path.exists(os.path.join(d, hyp)):
            v = os.path.join(d, app.upper() + ".VERSION")
            got.append("%s %s" % (app, open(v).read().strip() if os.path.exists(v) else "?"))
    return d, got


def make_drive(fam):
    shutil.rmtree(C, ignore_errors=True)
    os.makedirs(os.path.join(C, "LIB1"))
    os.makedirs(os.path.join(C, "LIB2"))
    shutil.copy(os.path.join(DIR, "EHYPVIEW.ACC"), C)
    for f in os.listdir(fam):
        if f.upper().endswith((".HYP", ".REF")):
            shutil.copy(os.path.join(fam, f), os.path.join(C, "LIB1", f.upper()))
    shutil.copy(os.path.join(FIX, "many", "many.hyp"), os.path.join(C, "LIB2", "BIG1.HYP"))
    shutil.copy(os.path.join(FIX, "many", "many.ref"), os.path.join(C, "LIB2", "BIG1.REF"))
    shutil.copy(os.path.join(FIX, "images.hyp"), os.path.join(C, "LIB2", "BIG2.HYP"))
    shutil.copy(os.path.join(FIX, "images.ref"), os.path.join(C, "LIB2", "BIG2.REF"))


def inf(start, paths="C:\\LIB1;C:\\LIB2"):
    with open(os.path.join(C, "EHYPVIEW.INF"), "wb") as f:
        f.write(b"START=%s\r\n" % start.encode())
        if paths:
            f.write(b"PATHS=%s\r\n" % paths.encode())


# an accessory's first boot (no MEMKB=, no HEAPKB= in its INF yet: each
# case below that starts from inf()) may look once more, at the boot
# root's *.ACC, to share the memory with the others loading (common's
# accmem.c acc_count; guideline 6.5, a2a12bc). Later boots read only the
# INF
FIRST_BOOT_LOOK = 1


def boot(name, wanted, idle=0, auto=None, timeout=900):
    """boot, wait for the EHV events wanted, idle seconds more: the trace,
    and the events with their times (the host's clock, fast-forward off)"""
    trace = os.path.join(OUT, name + ".trace")
    log = os.path.join(OUT, name + ".log")
    m = machines.Machine(MACHINE, C)
    args = ["hatari"] + m.args() + ["--natfeats", "yes", "--statusbar", "no", "--log-level", "fatal",
                                    "--fast-forward", "no", "--trace", "gemdos,aes", "--trace-file", trace]
    if auto:
        args += ["--auto", auto]
    h = subprocess.Popen(args, env=m.env(), stdout=open(log, "w"), stderr=subprocess.STDOUT)
    seen = {}
    t0 = time.time()
    try:
        while time.time() - t0 < timeout:
            for l in open(log, errors="replace"):
                l = l.strip()
                if (l.startswith("EHV ") or l.startswith("ASK")) and l not in seen:
                    seen[l] = time.time() - t0
            if all(any(w in e for e in seen) for w in wanted):
                break
            time.sleep(0.2)
        time.sleep(3 + idle)
    finally:
        h.terminate()
        try:
            h.wait(15)
        except subprocess.TimeoutExpired:
            h.kill()
    return trace, seen


CALL = re.compile(r"^GEMDOS 0x([0-9A-Fa-f]+) (.*?) at PC[= ]?0x([0-9A-Fa-f]+)")
EVENT = re.compile(r"^AES 0x(19|18|32|34|5A|5B) ")   # evnt_multi, evnt_timer, form_do, form_alert, fsel


def parse(trace):
    """the accessory's GEMDOS calls, and how many event calls came while
    one of its files was open"""
    lines = open(trace, encoding="latin-1").read().splitlines()
    lo = hi = None
    for i, l in enumerate(lines):
        if "now loading 'EHYPVIEW.ACC'" in l:
            for k in lines[i:i + 10]:
                m = re.search(r"Mshrink\(0x([0-9A-Fa-f]+), 0x([0-9A-Fa-f]+)\)", k)
                if m:
                    lo = int(m.group(1), 16)
                    hi = lo + int(m.group(2), 16)
                    break
            break
    if lo is None:
        raise SystemExit("no basepage for EHYPVIEW.ACC in " + trace)
    calls, fds, last, across, events = [], {}, None, 0, 0
    for l in lines:
        if EVENT.match(l):
            events += 1
            across += bool(fds)
            last = None
            continue
        m = CALL.match(l)
        if m:
            pc = int(m.group(3), 16)
            body = m.group(2)
            fn = re.match(r"\(?(\w+)\)?", body).group(1)
            if fn == "Fsnext" and last is not None and last["fn"] == "Fsfirst" and last["pc"] == pc:
                last = None
                continue
            if not (lo <= pc < hi):
                last = None
                continue
            c = {"fn": fn, "body": body, "pc": pc, "file": None, "bytes": 0, "res": None}
            a = re.search(r'"([^"]*)"', body)
            if a:
                c["file"] = a.group(1).upper()
            n = re.match(r"(?:Fread|Fwrite)\((\d+), (\d+)", body)
            if n:
                c["file"] = fds.get(int(n.group(1)), "?")
                c["bytes"] = int(n.group(2))
            n = re.match(r"Fclose\((\d+)\)", body)
            if n:
                c["file"] = fds.pop(int(n.group(1)), "?")
            n = re.match(r"Fseek\((-?\w+), (\d+), (\d+)\)", body)
            if n:
                c["file"] = fds.get(int(n.group(2)), "?")
            calls.append(c)
            last = c
            continue
        if last is not None and l.startswith("-> "):
            last["res"] = l[3:]
            fd = re.match(r"-> FD (\d+)", l)
            if fd and last["fn"] in ("Fopen", "Fcreate"):
                fds[int(fd.group(1))] = last["file"]
    return calls, across, events


def count(calls, *fns):
    return sum(1 for c in calls if c["fn"] in fns)


def summary(name, calls, seen, note=None):
    say("== " + name)
    say("   events: " + "; ".join("%s @%.1fs" % (e[4:] if e.startswith("EHV ") else e, t)
                                  for e, t in sorted(seen.items(), key=lambda x: x[1])))
    if note:
        say("   " + note)
    fns = ["Fopen", "Fcreate", "Fclose", "Fsfirst", "Fsnext", "Fdelete", "Frename", "Fseek", "Fread",
           "Fwrite", "Fxattr", "Dopendir", "Dclosedir", "Mxalloc", "Malloc"]
    parts = []
    for fn in fns:
        cs = [c for c in calls if c["fn"] == fn]
        if cs:
            b = sum(c["bytes"] for c in cs)
            parts.append("%s %d%s" % (fn, len(cs), " (%d B)" % b if b else ""))
    say("   " + ", ".join(parts))
    files = {}
    for c in calls:
        if c["fn"] in ("Fopen", "Fcreate", "Fsfirst", "Fdelete", "Frename", "Fread", "Fwrite", "Fseek"):
            f = files.setdefault(c["file"] or "?", {})
            k = c["fn"] + ("(append)" if c["fn"] == "Fopen" and "write" in c["body"] else "")
            if c["fn"] in ("Fopen", "Fsfirst") and (c["res"] or "").startswith("ERROR"):
                k += "(none)"
            f[k] = f.get(k, 0) + 1
            if c["fn"] in ("Fread", "Fwrite"):
                f[c["fn"] + " B"] = f.get(c["fn"] + " B", 0) + c["bytes"]
    for f in sorted(files):
        say("     %-26s %s" % (f, ", ".join("%s %d" % kv for kv in sorted(files[f].items()))))
    return {"name": name, "files": files, "events": seen}


def opens(calls):
    return count(calls, "Fopen", "Fcreate")


def looks(calls):
    """Fsfirst, and a folder read with Dopendir (MiNT, MagiC: closed with
    Dclosedir); TOS refuses Dopendir and Fxattr (EINVFN) without a look"""
    return count(calls, "Fsfirst", "Dclosedir")


def inf_written(calls):
    """the accessory's MEMKB= after its first index (common/ui/accmem.c,
    guideline 2.1): EHYPVIEW.INF read again and written, once a start"""
    return any(c["fn"] == "Frename" and "EHYPVIEW.IN" in (c["file"] or "") for c in calls)


def not_inf(calls):
    return [c for c in calls if "EHYPVIEW.IN" not in (c["file"] or "")]


def probes(name, calls):
    check(count(calls, "Fxattr") <= 1 and count(calls, "Dopendir") <= 1 + count(calls, "Dclosedir"),
          "%s: Fxattr and Dopendir asked once (%d, %d)" % (name, count(calls, "Fxattr"), count(calls, "Dopendir")))


def took(seen, a, b):
    ta = next((v for e, v in seen.items() if a in e), None)
    tb = next((v for e, v in seen.items() if b in e), None)
    return "%.1f s" % (tb - ta) if ta is not None and tb is not None else "?"


def main():
    if shutil.which("hatari") is None:
        print("skip  acc_files: needs hatari")
        return 0
    os.makedirs(OUT, exist_ok=True)
    fam, versions = family()
    make_drive(fam)
    guides = len([f for f in os.listdir(os.path.join(C, "LIB1")) if f.endswith(".HYP")]) + 2
    refs = len([f for d in ("LIB1", "LIB2") for f in os.listdir(os.path.join(C, d)) if f.endswith(".REF")])
    folders = 2
    roots = 4                  # LIB1, LIB2, C:\GUIDES, C:\GEMSYS\GUIDES
    say("on %s; %d guides (%s; BIG1, BIG2), %d .REF" % (machines.Machine(MACHINE, C).describe(), guides,
                                                       ", ".join(versions), refs))
    results = []

    def each(name, calls, across, events, seen, note=None):
        results.append(summary(name, calls, seen, note))
        check(across == 0, "%s: no file open across an event call (%d event calls)" % (name.split(".")[0], events))
        probes(name.split(".")[0], calls)

    # 0. the defaults: nothing but the settings until it is opened
    inf("opened")
    tr, seen = boot("s0", ["started"], idle=60)
    calls, across, events = parse(tr)
    each("0. START=opened, never opened, 60 s", calls, across, events, seen)
    check(opens(calls) <= 1 and looks(calls) <= FIRST_BOOT_LOOK,
          "0: EHYPVIEW.INF read once, nothing else (%d opens, %d looks; a first boot)" % (opens(calls), looks(calls)))

    # 1. the first index
    inf("now")
    tr, seen = boot("s1", ["index saved"])
    calls, across, events = parse(tr)
    each("1. first index, no EHYPVIEW.IDX", calls, across, events, seen,
         "first event call to 'index saved': " + took(seen, "started", "index saved"))
    # INF, ST-GUIDE.INF (none), the IDX (none), each guide and .REF, the ID$
    # twice (its names as they are sorted, then its head), and EHYPVIEW.INF
    # again for MEMKB=, read and written once
    budget = 3 + guides + refs + 2 + (2 if inf_written(calls) else 0)
    check(opens(calls) <= budget, "1: %d opens (budget %d: one per file)" % (opens(calls), budget))
    check(looks(calls) <= roots + folders + 1 + FIRST_BOOT_LOOK,
          "1: %d looks (budget %d: the roots and folders once; a first boot)"
          % (looks(calls), roots + folders + 1 + FIRST_BOOT_LOOK))
    rest = not_inf(calls)
    check(count(rest, "Frename") == 1 and count(rest, "Fwrite") == 2,
          "1: the index written in two goes: its names, its head")
    with open(os.path.join(C, "EHYPVIEW.INF"), "rb") as f:
        memkb = [l for l in f.read().decode("latin-1").splitlines() if l.startswith("MEMKB=")]
    check(len(memkb) == 1 and inf_written(calls) and count(calls, "Frename") == 2,
          "1: the memory it needs written once, %s" % (memkb[0] if memkb else "no MEMKB="))

    # 2. up to date, 3. then idle
    tr, seen = boot("s2", ["index unchanged"])
    c2, across, events = parse(tr)
    each("2. index up to date", c2, across, events, seen,
         "first event call to 'index unchanged': " + took(seen, "started", "index unchanged"))
    check(opens(c2) <= 3, "2: %d opens (budget 3: INF, ST-GUIDE.INF, the index read in one go)" % opens(c2))
    check(looks(c2) <= roots + folders + 1, "2: %d looks (budget %d: no guide looked up one by one)"
          % (looks(c2), roots + folders + 1))
    tr, seen = boot("s3", ["index unchanged"], idle=IDLE)
    c3, across, events = parse(tr)
    each("3. up to date, then %d s idle" % IDLE, c3, across, events, seen)
    check(len(c3) == len(c2), "3: no disk while idle (%d calls more than 2)" % (len(c3) - len(c2)))

    # 4. one changed, one added
    shutil.copy(os.path.join(FIX, "big.hyp"), os.path.join(C, "LIB2", "BIG2.HYP"))
    shutil.copy(os.path.join(FIX, "other.hyp"), os.path.join(C, "LIB2", "NEW.HYP"))
    old = os.path.getsize(os.path.join(C, "EHYPVIEW.IDX"))
    tr, seen = boot("s4", ["index saved"])
    calls, across, events = parse(tr)
    each("4. one guide changed, one added", calls, across, events, seen,
         "first event call to 'index saved': " + took(seen, "started", "index saved"))
    # INF, ST-GUIDE.INF, IDX's head; BIG2.HYP, BIG2.REF, NEW.HYP; the IDX's
    # names of the others (8 KB a read); the ID$ twice (names, head); and
    # EHYPVIEW.INF again when MEMKB= moved
    budget = 3 + 3 + (old + 8191) // 8192 + 2 + (2 if inf_written(calls) else 0)
    check(opens(calls) <= budget, "4: %d opens (budget %d)" % (opens(calls), budget))
    check(looks(calls) <= roots + folders + 1, "4: %d looks (budget %d: the folders read once)"
          % (looks(calls), roots + folders + 1))

    # 5. a Help key's request
    inf("opened")
    shutil.copy(os.path.join(DIR, "ASKHELP.PRG"), C)
    with open(os.path.join(C, "ASKHELP.TXT"), "w") as f:
        f.write("C:\\LIB2\\BIG1.HYP Main\r\n")
    page = "page C:\\LIB2\\BIG1.HYP Main"
    tr, seen = boot("s5", [page, "index"], auto="C:\\ASKHELP.PRG", idle=20)
    calls, across, events = parse(tr)
    each("5. VA_START C:\\LIB2\\BIG1.HYP Main", calls, across, events, seen,
         "ASK sent to the page: " + took(seen, "ASK sent", page))
    big1 = [c for c in calls if c["file"] == "C:\\LIB2\\BIG1.HYP"]
    check(count(big1, "Fopen") == 1, "5: the page asked for, one open (%d)" % count(big1, "Fopen"))
    check(count(big1, "Fsfirst") <= 1, "5: and one look (%d)" % count(big1, "Fsfirst"))
    os.remove(os.path.join(C, "ASKHELP.PRG"))

    # 6. the quiet test's library
    if os.environ.get("ACC_FILES_BIG"):
        shutil.rmtree(C)
        os.makedirs(os.path.join(C, "GUIDES"))
        shutil.copy(os.path.join(DIR, "EHYPVIEW.ACC"), C)
        for i in range(60):
            shutil.copy(os.path.join(FIX, "many", "many.hyp"), os.path.join(C, "GUIDES", "G%03d.HYP" % i))
            shutil.copy(os.path.join(FIX, "many", "many.ref"), os.path.join(C, "GUIDES", "G%03d.REF" % i))
        inf("now", None)
        tr, seen = boot("s6a", ["index saved"])
        calls, across, events = parse(tr)
        each("6a. 60 guides, first index", calls, across, events, seen,
             "first event call to 'index saved': " + took(seen, "started", "index saved"))
        size = os.path.getsize(os.path.join(C, "EHYPVIEW.IDX"))
        # the names sorted in batches of 24 KB: each batch to EHYPVIEW.RUN
        # in a write or two, then read back while they are merged into the
        # ID$, written 16 KB at a time; then its head in one write
        idw = [c for c in calls if (c["file"] or "").endswith("EHYPVIEW.ID$")]
        run = [c for c in calls if (c["file"] or "").endswith("EHYPVIEW.RUN")]
        writes = count(idw, "Fwrite")
        check(writes <= 2 + size // 16384, "6a: the %d KB index in %d writes (16 KB pieces, then the head)"
              % (size // 1024, writes))
        batches = size // 24576 + 1
        check(count(run, "Fopen") <= 2 * batches + 4,
              "6a: EHYPVIEW.RUN opened %d times (budget %d: each 24 KB batch written once, read back once)"
              % (count(run, "Fopen"), 2 * batches + 4))
        budget = 3 + 120 + opens(idw) + opens(run) + (2 if inf_written(calls) else 0)
        check(opens(calls) <= budget, "6a: %d opens (budget %d: one per file and piece)" % (opens(calls), budget))
        tr, seen = boot("s6b", ["index unchanged"])
        calls, across, events = parse(tr)
        each("6b. 60 guides, index up to date", calls, across, events, seen,
             "first event call to 'index unchanged': " + took(seen, "started", "index unchanged"))
        want = 3 + (2 if inf_written(calls) else 0)
        check(opens(calls) <= want, "6b: %d opens (budget %d: INF, ST-GUIDE.INF, the index's head in one go)"
              % (opens(calls), want))
        check(looks(calls) <= 4, "6b: %d looks (the roots and the folder)" % looks(calls))

    say("%d failed" % fails)
    with open(os.path.join(OUT, "report.txt"), "w") as f:
        f.write("\n".join(report) + "\n")
    json.dump(results, open(os.path.join(OUT, "results.json"), "w"), indent=1)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
