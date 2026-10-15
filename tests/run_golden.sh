#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Compare hypdump output with the committed reference output.
#
#   tests/run_golden.sh build/host/hypdump
#
# - text:  must match hcp's own rendering (an independent implementation),
#          ignoring the blank lines hcp prints between pages
# - links, list, info: must match the reviewed golden files
#
# The encrypted fixture has no hcp rendering (hcp refuses it); its text
# must equal that of the plain build it was derived from.
DUMP=${1:?usage: tests/run_golden.sh path/to/hypdump}
DUMP=$(cd "$(dirname "$DUMP")" && pwd)/$(basename "$DUMP")
TOP=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOP/tests/fixtures" || exit 1
EXP=$TOP/tests/expected
TMP=${TMPDIR:-/tmp}/ehv-golden.$$
mkdir -p "$TMP"
fail=0

check() {
	# check NAME EXPECTED ACTUAL [diff options]
	if diff $4 "$2" "$3" > "$TMP/diff"; then
		echo "ok    $1"
	else
		echo "FAIL  $1"
		head -20 "$TMP/diff"
		fail=1
	fi
}

for f in *.hyp; do
	b=${f%.hyp}
	if [ "$b" = basic_enc ]; then
		"$DUMP" text basic.hyp > "$TMP/expect"
		"$DUMP" text "$f" > "$TMP/out"
		check "$b text (vs basic)" "$TMP/expect" "$TMP/out"
	else
		"$DUMP" text "$f" > "$TMP/out"
		check "$b text (vs hcp)" "$EXP/$b.hcp.txt" "$TMP/out" -B
	fi
	for kind in links list info; do
		"$DUMP" $kind "$f" > "$TMP/out"
		check "$b $kind" "$EXP/$b.$kind.txt" "$TMP/out"
	done
done

rm -rf "$TMP"
exit $fail
