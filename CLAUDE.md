# Notes for working on EHypView

EHypView is one of the AERA Atari apps (ECal, EContact, EMail, EFTP, EHypView, ClaudeST). They are meant to
look and behave as one family.

## For every AERA app

These come from AERA-Atari's repo template and are the same in every app's CLAUDE.md.

- **Follow the AERA Atari app guidelines** ([ATARI-APP-GUIDELINES.md](https://github.com/whomper/AERA-Atari/blob/main/standards/ATARI-APP-GUIDELINES.md)). Pay most
  attention to:
  - About box
  - family shortcuts
  - PRG/ACC rules
  - context menus
  - VA_START, help, alerts
  - INF location

  When a change touches one of these, do it the guideline's way, even if the old code here did it
  differently.
- **Shared code lives in [AERA-Atari](https://github.com/whomper/AERA-Atari).** Files under `common/` come from it. Change them
  there, not here, and pull them in with `tools/sync-common.sh`. A fix made only in this repo will drift
  again.
- **Build and test before every push:** `make` with no warnings and `make test`, plus this repo's
  own checks (below): `make itest` when network code changed, the Hatari rigs in `tests/hatari/` when
  the GEM side changed.
- **Versions and releases:** `VERSION` changes only in a release commit. A release: `tools/bump.py`,
  commit, push, then run the Release workflow on `main` with the notes (or push a tag `vX.Y` made with
  `git tag -a`); it tests, builds, tags vX.Y and publishes each file and the zip as a GitHub Release.
  A build must leave every tracked file unchanged and shows the version in `VERSION` as it is (there is
  no `-dev` label). Don't commit built programs, help files or certificates (`binary/`, `dist/`). Never reuse a number.
- **Only formal releases leave the build machine.** Erez gets vX.Y releases made by the Release workflow from `main`, nothing else: no test builds, no CI artifacts, no builds from a branch. Work lands on `main`; to try a fix on real hardware, release it. If a release doesn't work, roll back with a release: revert on `main`, release the next number, and say in the bad release's notes that it is withdrawn and which one to use.
- **Testing with another AERA program uses its latest release**, what Erez runs, never a build of its
  `main`: `common/tools/getrelease.py APP DIR FILE...` fetches it. Only for a change both programs are
  still making, test against the other's `main` at a named commit, say so, and test again against its
  release when it is out.
- **Every new source file starts with the SPDX line and copyright** (see any `.c` file); a generator
  writes them into the files it makes.
- **The name** reads "AERA | Erez Yaary" wherever it is given: copyright lines
  (`Copyright (C) 2026 AERA | Erez Yaary`), the About box, the help file's `@author`, the docs, and as
  the author of every commit: `git commit --author="AERA | Erez Yaary <yaary@me.com>"`, with Claude as
  the signing committer, so GitHub shows the commits as Verified (AERA-Atari,
  `standards/GITHUB-IDENTITY.md`).
- **Never commit:**
  - real addresses from the owner's network: use `192.168.1.x`, `PI_IP`, `ATARI_IP`
  - passwords, session keys, `.KEY` / `.INF` files from a test run
  - photos that still carry EXIF/GPS data
- **README:** keep the section order from the template (Highlights, Getting started, How it reaches the
  servers, Building, Source, Tests, Licence, family footer). Use British English.

## EHypView

- **A release** carries `EHYPVIEW.PRG`, `EHYPVIEW.ACC` and the icon (`make atari` copies the
  programs to `binary/`; the icons are committed in `icons/`). The first release was 1.0.
- Before pushing: `make test`, `make asan`, `make m68k-test`,
  `make hatari-test`, `make gui-test`, `make quiet-test` (these two
  need hatari, Xvfb and xdotool) and `make colour-test`, and `make atari`
  with no warnings.
- One build, for the 68000, runs on every ST, STE, TT and Falcon. GCC 13
  joins byte reads into word reads, which fault on a 68000 at an odd
  address: keep `-fdisable-tree-bswap` (with `-fno-store-merging`) in
  XCPU/XFLAGS (never `-fno-expensive-optimizations`: GCC 13 miscompiles
  scan.c with it), and run `make hatari-test`, which runs the self-test
  on an ST too. Code an ST runs per pixel or per byte avoids 32-bit multiplies
  and long shifts (tos.c does those in software).
- Pictures and text colours go through src/gem/colour.c (no VDI; tested on
  the host). `make colour-test` checks them pixel by pixel in twelve screen
  modes, the ST's too, and the family's help front pages (from their
  releases) on 1 MB machines; in TT low and the Falcon's 320-wide true
  colour only the accessory runs (guideline 5.2), asked by tests/askhelp.c.
- The accessory starts quietly (guideline 2.2): nothing but set-up before
  its first event call, and the index (src/core/libjob.c) in short steps
  through common/ui/bgwork.c. `make quiet-test` measures it with an
  `-DAES_PROFILE` build: no stretch over 50 ms while it reads and indexes.
- The icon is drawn by `tools/icon/make_icon.py` (the family's SM124
  monitor); `make icons` writes src/gem/icon*.h, docs/icon*.png and
  icons/EHYPVIEW.RSC (committed). The About box (src/gem/about.c) follows
  EMail's, ECal's and EContact's.
- The font selector (src/gem/fontsel.c) is ECal's and EMail's; keep them
  alike.
- Control shortcuts test `key_ctrl(kstate, key)` (common/atari/gem.h),
  never `kstate & K_CTRL` alone: a ^key typed while EHypView is busy
  arrives with kstate 0 (guidelines 3.2).
- GUI events for the tests go out through NatFeats as "EHV ..." lines
  (log_event); tests/run_gui.py waits for them.
- Only `atari/plat_tos.c` is EHypView's own runtime; the rest (start-up, C library, TOS, AES/VDI,
  elf2tos.py) is `common/`.
