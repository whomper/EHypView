#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Run EHVCHECK.PRG in Hatari and compare its report with the host build.
#
#   tests/run_hatari.sh HOST_EHVCHECK HOST_FIXT HATARI_DIR
#
# Hatari emulates a Falcon with EmuTOS; HATARI_DIR becomes drive C:, the
# program starts automatically, prints its report through NatFeats
# (NF_STDERR) and ends the emulator with NF_EXIT.  EmuTOS is fetched
# into build/ when EMUTOS does not name an image.
set -e
HOSTCHECK=$1
HOSTFIXT=$2
HDIR=$3
TOP=$(cd "$(dirname "$0")/.." && pwd)
OUT=$TOP/build/hatari-out
mkdir -p "$OUT"

EMUTOS=${EMUTOS:-$(sh "$TOP/tests/emutos.sh")}

"$HOSTCHECK" "$HOSTFIXT" > "$OUT/host.txt"

rc=0
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 300 hatari \
	--tos "$EMUTOS" --machine falcon --monitor vga --memsize 14 \
	--natfeats yes --fast-forward yes --sound off --confirm-quit no \
	--statusbar no --log-level fatal \
	--harddrive "$HDIR" --gemdos-drive C --auto 'C:\EHVCHECK.PRG' \
	--run-vbls 20000 > "$OUT/hatari.log" 2>&1 || rc=$?

# the report is everything from "EHVCHECK" to "END"
tr -d '\r' < "$OUT/hatari.log" | sed -n '/^EHVCHECK /,/^END /p' > "$OUT/atari.txt"
if [ ! -s "$OUT/atari.txt" ]; then
	echo "FAIL: no report from Hatari (exit $rc); see $OUT/hatari.log"
	tail -20 "$OUT/hatari.log"
	exit 1
fi
if diff -u "$OUT/host.txt" "$OUT/atari.txt"; then
	echo "ok    Atari report matches host ($(wc -l < "$OUT/atari.txt") lines, Hatari exit $rc)"
else
	echo "FAIL: Atari report differs from host"
	exit 1
fi
grep -q "^END failures=0" "$OUT/atari.txt"
