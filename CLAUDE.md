# Notes for working on EHypView

- **Every new build gets a new version.** `make atari` counts the minor
  number in `VERSION` up whenever the program was compiled again
  (`tools/bump.py`), as in EMail, ECal and EContact, and copies the
  programs to `binary/`: commit `VERSION` with `binary/`, and never reuse
  a number.
- Before pushing: `make test`, `make asan`, `make m68k-test`,
  `make hatari-test` and `make gui-test` (the last needs hatari, Xvfb and
  xdotool), and `make atari` with no warnings.
- The icon is drawn by `tools/icon/make_icon.py` (the family's SM124
  monitor); `make icons` writes src/gem/icon*.h, docs/icon*.png and
  binary/icons/EHYPVIEW.RSC. The About box (src/gem/about.c) follows
  EMail's, ECal's and EContact's.
- The font selector (src/gem/fontsel.c) is ECal's and EMail's; keep them
  alike.
- GUI events for the tests go out through NatFeats as "EHV ..." lines
  (log_event); tests/run_gui.py waits for them.
- `common/` is the AERA programs' shared code (start-up, C library, TOS,
  AES/VDI, elf2tos.py), a git subtree of github.com/whomper/AERA-Atari.
  Change it there, never here, then `tools/sync-common.sh`. Only
  `atari/plat_tos.c` is EHypView's own.
- **The name** reads "AERA | Erez Yaary" wherever it is given: copyright
  lines (`Copyright (C) 2026 AERA | Erez Yaary`), the About box, the help
  file's `@author`, the docs, and as the author of every commit:
  `git commit --author="AERA | Erez Yaary <yaary@me.com>"`, with Claude as
  the signing committer (AERA-Atari, `standards/GITHUB-IDENTITY.md`).
