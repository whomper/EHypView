# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""NATFEATS=1 in a test drive's EHYPVIEW.INF.

EHypView looks for NatFeats (its "EHV ..." lines to the rigs) only when its
INF says NATFEATS=1, and only the rigs write that (Erez, 15 Nov 2026: "make
it a .INF setting so that it can be added in our testing but have no bearing
outside"; the guidelines, 6.5). Every rig that waits for those lines calls
seed() on the drive it boots, after writing any INF of its own. A file that
already has the key is left as it is, date and all."""
import os


def inf_path(drive):
    """the drive's ehypview.inf, in whatever case it is there"""
    for name in os.listdir(drive):
        if name.lower() == "ehypview.inf":
            return os.path.join(drive, name)
    return os.path.join(drive, "ehypview.inf")


def seed(drive):
    path = inf_path(drive)
    text = b""
    if os.path.exists(path):
        with open(path, "rb") as f:
            text = f.read()
        if any(line.strip() == b"NATFEATS=1" for line in text.splitlines()):
            return
    nl = b"\r\n" if b"\r\n" in text or not text else b"\n"
    if text and not text.endswith(b"\n"):
        text += nl
    with open(path, "wb") as f:              # a note above it, as every setting has (6.1)
        f.write(text + b"; NatFeats for the test rigs (written by the rigs only)" + nl + b"NATFEATS=1" + nl)
