#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Run PAGEBENC.PRG (tests/pagebench.c) in Hatari on MACHINE (default
# ST:mono) and print its report: the checksums and the times.
#
#   tests/run_bench.sh DIR      (DIR becomes drive C:)
set -e
DIR=$1
TOP=$(cd "$(dirname "$0")/.." && pwd)
MACHINE=${MACHINE:-ST:mono}
OUT=$TOP/build/pagebench-out
mkdir -p "$OUT"
python3 "$TOP/common/tools/machines.py" args "$MACHINE" "$DIR" > "$OUT/args.txt" 2> "$OUT/machine.txt"
set --
while IFS= read -r a; do set -- "$@" "$a"; done < "$OUT/args.txt"
HOME=$(cd "$DIR" && pwd).home SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 600 hatari "$@" \
	--natfeats yes --fast-forward yes --statusbar no --log-level fatal \
	--auto 'C:\PAGEBENC.PRG' --run-vbls 30000 > "$OUT/hatari.log" 2>&1 || true
echo "on $(cat "$OUT/machine.txt")"
tr -d '\r' < "$OUT/hatari.log" | sed -n '/^PAGEBENCH/,/^END/p'
