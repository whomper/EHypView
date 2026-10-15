#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Turn an uncompressed hypertext into an "encrypted" one.

ST-Guide can protect a hypertext by XORing every data byte with 127 and
setting bit 1 of the ST-Guide flags header (id 10).  hcp cannot write such
files, so the test suite derives one from an uncompressed (-c) build.

usage: mkencrypted.py IN.hyp OUT.hyp
"""
import struct
import sys


def main(src, dst):
    data = bytearray(open(src, "rb").read())
    assert data[:4] == b"HDOC", "not a hypertext"
    tabsize, count = struct.unpack(">IH", data[4:10])

    # data offset of the first entry = start of the data region
    first = struct.unpack(">I", data[14:18])[0]

    # find extended header 10 and set the encrypted flag
    pos = 12 + tabsize
    found = False
    while pos + 4 <= len(data):
        hid, hlen = struct.unpack(">HH", data[pos:pos + 4])
        if hid == 0:
            break
        if hid == 10 and hlen == 2:
            data[pos + 4] |= 0x02      # flags are little-endian
            found = True
        pos += 4 + hlen
    assert found, "no ST-Guide flags header"

    for i in range(first, len(data)):
        data[i] ^= 127
    open(dst, "wb").write(data)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
