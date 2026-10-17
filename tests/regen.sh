#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Regenerate the test fixtures and the reference output.
#
#   tests/regen.sh /path/to/hcp
#
# hcp is the hypertext compiler from https://github.com/th-otto/hypview
# (GPL).  It is used only as an external tool to produce real ST-Guide
# files and an independent plain-text rendering of them; no HypView code
# is part of EHypView.
#
# Outputs (all committed):
#   tests/fixtures/*.hyp, *.ref      compiled fixtures
#   tests/fixtures/all.ref           ALL.REF: basic + other + a missing "ghost"
#   tests/expected/*.hcp.txt         hcp's own text rendering (oracle)
#   tests/expected/*.links.txt       hypdump link/graphics listing
#   tests/expected/*.list.txt        hypdump index listing
#   tests/expected/*.info.txt        hypdump header listing
set -e

HCP=${1:?usage: tests/regen.sh /path/to/hcp}
HCP=$(cd "$(dirname "$HCP")" && pwd)/$(basename "$HCP")
TOP=$(cd "$(dirname "$0")/.." && pwd)
FIX=$TOP/tests/fixtures
EXP=$TOP/tests/expected
DUMP=$TOP/build/host/hypdump

python3 "$TOP/tests/tools/mkfixtures.py" "$FIX/src"
make -C "$TOP" build/host/hypdump >/dev/null

# hcp insists on updating a default reference file
export HOME=${TMPDIR:-/tmp}/ehv-regen-home
mkdir -p "$HOME/.hypview/guides"

cd "$FIX/src"
for src in basic other images big; do
	"$HCP" -q -o "$FIX/$src.hyp" "$src.stg" >/dev/null
	"$HCP" -q -c -o "$FIX/${src}_raw.hyp" "$src.stg" >/dev/null
done
# a guide of a large library (tests/run_quiet.py), in a folder of its own
# so that the golden files below leave it out
mkdir -p "$FIX/many"
"$HCP" -q -o "$FIX/many/many.hyp" many.stg >/dev/null
mv -f "$FIX/many.ref" "$FIX/src/many.ref" "$FIX/many/" 2>/dev/null || true
mv -f "$FIX/src/"*.ref "$FIX/" 2>/dev/null || true
rm -f "$FIX/"*_raw.ref
python3 "$TOP/tests/tools/mkencrypted.py" "$FIX/basic_raw.hyp" "$FIX/basic_enc.hyp"
python3 "$TOP/tests/tools/mkallref.py" "$FIX/all.ref" "$FIX/basic.ref" "$FIX/other.ref"

cd "$FIX"
for f in *.hyp; do
	b=${f%.hyp}
	[ "$b" = basic_enc ] || "$HCP" -v "$f" > "$EXP/$b.hcp.txt" 2>/dev/null || true
	"$DUMP" links "$f" > "$EXP/$b.links.txt"
	"$DUMP" list "$f" > "$EXP/$b.list.txt"
	"$DUMP" info "$f" > "$EXP/$b.info.txt"
done
echo "fixtures regenerated"
