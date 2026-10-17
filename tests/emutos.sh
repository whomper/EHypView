#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Print the path of the EmuTOS image for the emulator tests, fetching
# EmuTOS 1.4 (GPL, 512 KB build: ST to Falcon; the first with true colour
# on the Falcon) into build/emutos first when needed.
set -e
V=1.4
TOP=$(cd "$(dirname "$0")/.." && pwd)
IMG=$TOP/build/emutos/etos512us-$V.img
if [ ! -f "$IMG" ]; then
	mkdir -p "$TOP/build/emutos"
	curl -sSL -o "$TOP/build/emutos/emutos-$V.zip" \
		https://downloads.sourceforge.net/project/emutos/emutos/$V/emutos-512k-$V.zip
	python3 -I -c 'import zipfile,sys; z=zipfile.ZipFile(sys.argv[1]); open(sys.argv[3],"wb").write(z.read("emutos-512k-%s/etos512us.img" % sys.argv[2]))' \
		"$TOP/build/emutos/emutos-$V.zip" "$V" "$IMG"
fi
echo "$IMG"
