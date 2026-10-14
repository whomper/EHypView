#!/usr/bin/env python3
"""Build an ALL.REF test fixture.

ALL.REF is a series of REF modules behind one "HREF" header.  This joins
the given REF files and adds a module for "ghost", a hypertext that does
not exist, so tests can check that EHypView reports such entries.

usage: mkallref.py OUT.ref IN.ref...
"""
import struct
import sys


def modules(path):
    data = open(path, "rb").read()
    assert data[:4] == b"HREF", path
    pos, out = 4, []
    while pos + 8 <= len(data):
        length, count = struct.unpack(">II", data[pos:pos + 8])
        if length == 0:
            break
        out.append(data[pos:pos + 8 + length])
        pos += 8 + length
    return out


def entry(kind, name, line=None):
    body = name.encode("latin-1") + b"\0"
    if line is not None:
        body += struct.pack(">H", line)
    return bytes([kind, len(body) + 2]) + body


def main(out, inputs):
    parts = []
    for path in inputs:
        parts += modules(path)
    ghost = entry(0, "ghost") + entry(1, "Haunted") + entry(2, "Spooky")
    parts.append(struct.pack(">II", len(ghost), 3) + ghost)
    with open(out, "wb") as f:
        f.write(b"HREF" + b"".join(parts) + b"\0" * 8)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
