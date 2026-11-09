# Building, source and tests

The details behind the README's [Building](../README.md#building), [Source](../README.md#source) and
[Tests](../README.md#tests).

## Building

```sh
sudo apt install gcc-m68k-linux-gnu zip   # Debian/Ubuntu; no MiNTLib needed
make           # the host tools and tests in build/host, among them hypdump
make atari     # build/hatari/: EHYPVIEW.PRG, EHYPVIEW.ACC, EHVCHECK.PRG;
               # binary/: EHYPVIEW.PRG, EHYPVIEW.ACC
make help HCP=/path/to/hcp   # binary/EHYPVIEW.HYP and .REF from docs/GUIDE.md
make dist      # dist/: a release's files, each on its own and in ehypview-x.y.zip
make icons     # the icon again from common/icons/EHYPVIEW.txt (common/tools/appicon.py; python3-pil)
make shots HCP=/path/to/hcp   # the README's pictures in docs/ and the guide's WINDOW.IMG, in Hatari
```

The Atari programs are built the same way as ECal: with Ubuntu's stock m68k compiler, for the 68000
(so the same files run on every ST, TT and Falcon), and no MiNTLib. GCC 13 joins byte reads into word
reads even for the 68000, which faults on an odd address, so the build turns that pass off
(`-fdisable-tree-bswap`, see the Makefile). `common/atari/` holds a small freestanding runtime (start-up code, libc, GEMDOS
file access, AES/VDI bindings), and `common/tools/elf2tos.py` writes the TOS program file. `common/` is
the AERA programs' shared code, a copy of [AERA-Atari](https://github.com/whomper/AERA-Atari)'s
`common/` folder: change it there and bring the copy up to date with `tools/sync-common.sh`.

**The guide.** `docs/GUIDE.md` is the source of `EHYPVIEW.HYP`: `tools/mkhelp.py` (EContact's and
EMail's) makes ST-Guide's source from it, `common/tools/helpfront.py` gives it the AERA programs' front
page from `docs/front.json`, and `hcp` compiles it; `common/tools/build-hcp.sh DIR` builds `hcp`. The
front page's window picture, `docs/help/WINDOW.IMG`, is committed: when the viewer changes, `make shots`
takes it again in Hatari (`common/tools/helpfront.py thumb`), with the README's pictures: the Falcon in 256 colours (`Falcon:256`)
with the hypertexts in the folder `SHOT_GUIDES=` names (`tests/run_gui.py`, `SHOT_STEPS`). EHypView's own reader checks every page of the result (`hypdump check`).

`build/host/hypdump` inspects any hypertext:

```sh
build/host/hypdump info  GUIDE.HYP           # header: database, author, subject ...
build/host/hypdump list  GUIDE.HYP           # index table
build/host/hypdump text  GUIDE.HYP [NODE]    # pages as UTF-8 text
build/host/hypdump links GUIDE.HYP [NODE]    # links, see-also, graphics
build/host/hypdump check *.HYP               # decode everything, report damage

# find every hypertext below some folders (with cache and ALL.REF)
build/host/hypdump scan -c guides.idx -r ALL.REF /path/to/guides /more/guides
# where does a link or help request lead?
build/host/hypdump resolve -c guides.idx -f OPEN.HYP xref "AES.HYP/appl_init"
build/host/hypdump resolve -c guides.idx request "'*:\AES.HYP' appl_init"
build/host/hypdump resolve -c guides.idx name appl_init
```

**Versions and releases.** `VERSION` holds the last release, major.minor (e.g. 1.0), and the build
never changes it. Every build shows it as it is in the About box; there is no `-dev` label. `binary/`
and `dist/` are what make builds; they are never committed, and a build changes no tracked file.
Builds between releases are never handed out: to try a change, release it. To release: `tools/bump.py`, commit and push, then **Actions > Release > Run
workflow** on `main` with what is new.
[`.github/workflows/release.yml`](../.github/workflows/release.yml) tags the commit (`v1.1`), tests,
builds the programs and the guide, and publishes each file and the zip as a GitHub Release with
those notes. Pushing
a tag `v1.1` made with `git tag -a` does the same, with the tag's message as the notes.

## Source

```
src/core/   the portable core, without GEM: .HYP files and LH5, pages, .REF and
            ST-GUIDE.INF; the folder scanner and the index (its names on the disk); links and help requests;
            the full-text search. Built for the Atari and for this computer
src/view/   the viewer and the library without GEM (links, history, search, the
            font setting), tested on this computer
src/gem/    the GEM program: the viewer and library windows, drawing, the menu bar,
            the font (its dialog is common/ui/fontsel.c), the Settings dialog, the About box
atari/      plat_tos.c: the core's file access on GEMDOS, EHypView's own
common/     the AERA programs' shared code (from AERA-Atari): the TOS runtime,
            AES/VDI, menu bar, About box, alerts, AV protocol, drag and drop,
            iconify, elf2tos.py
icons/      the desktop icon (RSC and ICN files), made by make icons
tests/      unit and golden tests, fixtures and their generators, EHVCHECK,
            the Hatari rigs
tools/      hypdump, mkhelp.py (the guide), bump.py, sync-common.sh
docs/       GUIDE.md (the source of EHYPVIEW.HYP), front.json and help/ (its front
            page), DESIGN.md, FORMAT.md, the icon pictures, the screenshots
```

[docs/DESIGN.md](DESIGN.md) has the goals, the design decisions, the architecture and the
roadmap; [docs/FORMAT.md](FORMAT.md) the `.HYP` and `.REF` file formats, with the corrections to
the published description.

## Tests

```sh
make test         # unit and golden tests on this computer
make asan         # the same with AddressSanitizer and UBSan
make m68k-test    # the suite as big-endian 68020 code under QEMU (Linux's libraries need a
                  # 68020; the 68000 is tested in Hatari, on an ST)
                  # (apt install gcc-m68k-linux-gnu qemu-user-static)
make hatari-test  # EHVCHECK.PRG in Hatari, on the family's Falcon and ST (apt install hatari)
make gui-test     # drive EHYPVIEW.PRG in Hatari with the keyboard and mouse
                  # (apt install hatari xvfb xdotool; screenshots in build/gui)
make quiet-test   # the accessory's start and indexing measured in Hatari
make colour-test  # pictures and coloured text in every screen mode, pixel by pixel
```

- **`make test`** runs the unit tests (`tests/test_*.c`: the core, the scanner and index on a real
  folder tree, the viewer, the library, the menu tree, fonts; and common/tests/dialog's, the dialog
  loop and its fields that hold more than they show) and the golden tests
  (`tests/run_golden.sh`): the text of the test hypertexts must match `hcp`'s own rendering, an
  independent implementation, and their links, index and header the reviewed files in
  `tests/expected/`.
- **`make m68k-test`** catches byte-order and alignment mistakes before they reach real hardware.
- **`make hatari-test`** runs `EHVCHECK.PRG`, a self-test, not part of the viewer, on the Falcon
  (`Falcon:16`) and on the 4 MB ST (`ST:mono`), whose 68000 faults on a word read at an odd address. It exercises the
  core through GEMDOS on a test folder: scanning, every page and image, links, and the cache. Its
  report must match the same program built for this computer, line for line. To run it on a real
  Atari, copy `build/hatari/` (the program and its `FIXT` folder) to a drive and start
  `EHVCHECK.PRG`.
- **`make gui-test`** drives `EHYPVIEW.PRG` through Hatari's command FIFO: links (also into a
  hypertext three folders down), back, help, the library and both searches, fonts and the font
  selector, Settings (Undo cancels it, Esc too where no field has the cursor), ^U and ^W, the accessory, and the program and accessory side by
  side. The mouse run (Xvfb and xdotool; skipped without them) clicks links, the right button, the
  menus, the About box and Settings: its START= popup, its Library page's folders (one longer than
  its field shows, edited at its start and end), the alert for one that isn't there, and the library
  looked through again; `EHYPVIEW.INF` must then hold them. The long folder is typed again on the
  4 MB ST in mono with TOS 1.04 (`ST:mono`), with ⇧Left, ⇧Right and ⇧Clr/Home. On a Falcon of
  4 MB with TOS 4.04 (`Falcon-4:16`) and on the ST in mono, a wide hypertext's window must fit the
  screen, and one click on its slider track must move it one page and one on its arrow one line,
  each click a single `WM_ARROWED` (guidelines 5.1: TOS 4's AES sent each click twice, TOS 1.04 now
  and then, until common's `evnt_multi_` dropped the second). The
  viewer reports what it does through NatFeats, and each step waits for it. `make accnvdi-test`, with Erez's NVDI
  (`AERA_WITH_NVDI=1`, `common/tools/machines.py`; skipped without it): the accessory picks a GDOS font in
  its own selector, by keyboard, is closed and opened again, and draws a page another program asked for
  after that program ended (screenshots `accnvdi1-NN.png`, `accnvdi2-NN.png`).
- **`make quiet-test`** boots the Falcon (`Falcon:16`) with the accessory, built with `-DAES_PROFILE`, and a
  library of 60 hypertexts and 6,720 pages, opens it from the Desk menu and boots again. Nothing may
  be read before it is opened, no stretch between its event calls while it reads and indexes may last
  more than 50 ms, and the second boot must find nothing changed and write nothing
  (`tests/run_quiet.py`; the guidelines' 2.2, "A quiet start").

- **`make colour-test`** opens a page of pictures and coloured text in eleven screen modes: the Falcon
  in 4, 16 and 256 colours, ST medium and true colour, the TT in medium, low and high, and an ST of 1
  MB in high and medium, each in Hatari. It checks every pixel of the pictures, and the text's
  colours, against what the mode's palette should show. Then it opens the family's help front page,
  from a test guide kept here (`tests/fixtures/front`), on a 1 MB ST and a 1 MB Falcon: both pictures
  must show (`tests/run_colour.py`; screenshots in `build/colour`).

The Hatari rigs run on the family's test machines (`ST`, `ST-1`, `TT`, `Falcon` and their screens,
from `common/tools/machines.py`; the guidelines, The machines). Each run names its machine and TOS.
That is EmuTOS 1.4, fetched once into `~/.cache/aera`, unless `~/.aera/tos` holds Erez's own TOS
images (never in git). `colour-test` adds the screens the machines don't have, starting from the
machines' own arguments: the Falcon in 4 colours, ST medium and true colour, TT low, and a Falcon cut
down to 1 MB.
[`.github/workflows/ci.yml`](../.github/workflows/ci.yml) runs all of the above on every push, and checks
that the build changed no tracked file.

**Test fixtures.** The `.hyp`/`.ref` files in `tests/fixtures` are generated from EHypView's own
sources (`tests/tools/mkfixtures.py`) by `tests/regen.sh /path/to/hcp`, which also writes the
reference output in `tests/expected/`; `common/tools/build-hcp.sh DIR` builds `hcp`.

