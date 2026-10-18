#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Run EHVCHECK.PRG in Hatari and compare its report with the host build.
#
#   tests/run_hatari.sh HOST_EHVCHECK HOST_FIXT HATARI_DIR
#
# MACHINE is one of the family's test machines (common/tools/machines.py;
# by default Falcon:16, and the Makefile runs ST:mono too, where a word
# read at an odd address faults).  HATARI_DIR becomes drive C:, the
# program starts automatically, prints its report through NatFeats
# (NF_STDERR) and ends the emulator with NF_EXIT.
set -e
HOSTCHECK=$1
HOSTFIXT=$2
HDIR=$3
TOP=$(cd "$(dirname "$0")/.." && pwd)
MACHINE=${MACHINE:-Falcon:16}
OUT=$TOP/build/hatari-out/$(echo "$MACHINE" | tr -c 'A-Za-z0-9\n-' '_')
mkdir -p "$OUT"

# the machine's Hatari arguments, one a line; what it is, for the report
python3 "$TOP/common/tools/machines.py" args "$MACHINE" "$HDIR" > "$OUT/args.txt" 2> "$OUT/machine.txt" ||
	{ cat "$OUT/machine.txt"; exit 1; }
set --
while IFS= read -r a; do set -- "$@" "$a"; done < "$OUT/args.txt"

"$HOSTCHECK" "$HOSTFIXT" > "$OUT/host.txt"

rc=0
HOME=$(cd "$HDIR" && pwd).home SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 300 hatari "$@" \
	--natfeats yes --fast-forward yes --statusbar no --log-level fatal \
	--auto 'C:\EHVCHECK.PRG' --run-vbls 20000 > "$OUT/hatari.log" 2>&1 || rc=$?

# the report is everything from "EHVCHECK" to "END"
tr -d '\r' < "$OUT/hatari.log" | sed -n '/^EHVCHECK /,/^END /p' > "$OUT/atari.txt"
if [ ! -s "$OUT/atari.txt" ]; then
	echo "FAIL: no report from Hatari (exit $rc); see $OUT/hatari.log"
	tail -20 "$OUT/hatari.log"
	exit 1
fi
if diff -u "$OUT/host.txt" "$OUT/atari.txt"; then
	echo "ok    Atari report matches host on $(cat "$OUT/machine.txt") ($(wc -l < "$OUT/atari.txt") lines, Hatari exit $rc)"
else
	echo "FAIL: Atari report differs from host"
	exit 1
fi
grep -q "^END failures=0" "$OUT/atari.txt"
