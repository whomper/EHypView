#!/bin/sh
# Print the path of the EmuTOS image for the emulator tests, fetching
# EmuTOS 1.3 (GPL, 512 KB build: ST to Falcon) into build/emutos first
# when needed.
set -e
TOP=$(cd "$(dirname "$0")/.." && pwd)
IMG=$TOP/build/emutos/etos512us.img
if [ ! -f "$IMG" ]; then
	mkdir -p "$TOP/build/emutos"
	curl -sSL -o "$TOP/build/emutos/emutos.zip" \
		https://downloads.sourceforge.net/project/emutos/emutos/1.3/emutos-512k-1.3.zip
	python3 -I -c 'import zipfile,sys; z=zipfile.ZipFile(sys.argv[1]); open(sys.argv[2],"wb").write(z.read("emutos-512k-1.3/etos512us.img"))' \
		"$TOP/build/emutos/emutos.zip" "$IMG"
fi
echo "$IMG"
