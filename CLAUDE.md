# Notes for working on EHypView

- **Releases carry the programs.** `binary/` and `dist/` are build output,
  never committed (`make atari` copies the programs to `binary/`).
  `VERSION` holds the last release and `make` never writes it (a build
  shows `x.y-dev`; `RELEASE=1` shows `x.y`); the first release is 1.0. A
  release: `tools/bump.py`, commit, push, then run the Release workflow
  on main with the notes (or push a tag `vX.Y` made with `git tag -a`);
  `.github/workflows/release.yml` tags vX.Y, tests, builds the programs
  and publishes each file and the zip as a GitHub Release. Never reuse a
  number.
- Before pushing: `make test`, `make asan`, `make m68k-test`,
  `make hatari-test` and `make gui-test` (the last needs hatari, Xvfb and
  xdotool), and `make atari` with no warnings.
- The icon is drawn by `tools/icon/make_icon.py` (the family's SM124
  monitor); `make icons` writes src/gem/icon*.h, docs/icon*.png and
  icons/EHYPVIEW.RSC (committed). The About box (src/gem/about.c) follows
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
