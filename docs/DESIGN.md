# EHypView design

EHypView is a new viewer for ST-Guide hypertexts (`.HYP`). It runs on the Atari
itself, aims first at Falcon, TT and FireBee, and is meant to replace
`ST-GUIDE.ACC`.

## Why replace ST-Guide

ST-Guide (Holger Weets, 1990s; last release 1.5, 1996) shows pages well once it
has found them. Its problems lie in **finding files** and in the **accessory
interface**.

### Finding hypertexts
- **Only listed folders are searched.** `PATHS=` in `ST-GUIDE.INF` is a fixed
  list, and subfolders are not searched. A guide one level deeper is invisible.
- **The `ALL.REF` index is maintained by hand.** Page names that aren't in
  the current file are looked up in `ALL.REF`. Entries get there only when
  someone runs REFLINK, or compiles with `hcp +zz`. Newly installed or renamed
  guides are therefore missing, with the error "File not found".
- **REF entries store bare file names**, so they depend on `PATHS` anyway.
- **Old file-name rules.** The AV protocol wants full paths in upper case, and
  some names must be upper case and padded to 8 characters. The FAQ advises
  turning off lower-case file names in desktops.
- **Configuration comes only from `ST-GUIDE.INF`** in the boot root, with no
  spaces allowed in lines.

### Interface
- Desk accessory, single window, modal pop-ups.
- Back stack of 16 pages; history menu of 12 entries cut to 40 characters;
  10 bookmarks.
- The font can't be changed while a window is open.
- At most 12 "See also" entries.
- Search is split into four modes. Full-text search needs the external
  HypFind tool, which builds a results hypertext.

## Goals

1. **Find every hypertext without setup.**
   - Scan folders including all subfolders.
   - Build our own index from the HYP files themselves, so `ALL.REF` and
     REFLINK are not needed (but are still read).
   - Match file names regardless of case or length.
   - Explain unresolved links and suggest the closest match.
2. **A modern GEM application.**
   - Multiple windows, plus a tab strip in the window toolbar.
   - Unlimited back/forward history and bookmarks.
   - Non-blocking pop-ups.
   - Fonts changeable at any time, including NVDI vector fonts.
   - Iconify, full keyboard control, printing.
3. **A library window.** Every guide found, grouped by its `@subject`, with
   title, author and version, plus one search box. The search finds page
   names and keywords first, then full text.
4. **Drop-in compatibility.** Existing programs must keep finding it:
   - register as `ST-GUIDE` (`appl_find`)
   - accept `VA_START` with ST-Guide's argument forms (`file.hyp node`,
     `*:\file.hyp`, bare `node`)
   - support the Pure C help protocol (`AC_HELP`)
   - honour the `STGUIDE`/`HELPVIEWER` environment variables
   - read an existing `ST-GUIDE.INF`
5. **Faithful display.**
   - Graphics are placed on the character grid, so pages with graphics use a
     fixed-width layout.
   - Colour images are shown at full depth on Falcon true colour, TT and
     FireBee video; they are dithered only on monochrome screens.

## Targets

| Build | CPU | Systems |
|---|---|---|
| `atari` | 68020-60 | Falcon, TT; MiNT, MagiC, XaAES, or plain TOS |
| `firebee` | ColdFire 5475 | FireBee (FireTOS, MiNT) |

- The main build is a regular GEM program (`EHYPVIEW.APP`).
- An accessory build is kept for single-tasking TOS, where an accessory is
  the only way to stay available inside other programs.
- Toolchain: Ubuntu's stock `gcc-m68k-linux-gnu`, as for ECal. There is
  no MiNTLib: `common/atari/` is a small freestanding runtime (start-up
  code for both program and accessory, libc, GEMDOS, AES/VDI bindings)
  shared with ECal, EMail, EContact, EFTP and ClaudeST through
  [AERA-Atari](https://github.com/whomper/AERA-Atari), and
  `common/tools/elf2tos.py` writes the TOS program file.

## Architecture

```
src/core/      portable C, no GEM: builds on the Atari and on the host
  lh5.c        LH5 decompressor
  hypfile.c    .HYP header, index, extended headers, object loading
  page.c       page byte stream -> lines of runs, graphics, xrefs
  ref.c        .REF / ALL.REF reader
  inf.c        ST-GUIDE.INF reader; EHYPVIEW.INF written back (inf_put)
  charset.c    Atari character set: Unicode, case folding
  pool.c       string pool for the index
  fsys.c       directories, paths, case-insensitive file lookup
  index.c      the guide index: lookups, "similar" suggestions, cache
  scan.c       step-wise folder scanner (all subfolders), default roots
  resolve.c    links, bare names and help requests -> file + page
  plat.h       the platform layer: every disk access goes through it
  plat.c       whole-file load/save on top of it
  plat_posix.c the platform layer on POSIX (host tools and tests)
atari/         plat_tos.c: the platform layer on GEMDOS (Dopendir/Fxattr
               on MiNT and MagiC, Fsfirst/Fsnext on TOS and EmuTOS)
common/        the AERA programs' shared code (git subtree of AERA-Atari):
               atari/ is the TOS runtime, tools/elf2tos.py the linker's
               last step; tools/sync-common.sh brings it up to date
  (next) image.c   planar image -> VDI raster for any screen depth
  search.c     step-wise full-text search through every hypertext
src/view/      without GEM, tested on the host:
  view.c       the viewer model: open, links, history back and forward,
               See also (@xref), link selection
  library.c    the library: grouped list, name search, full-text hits
  fontpref.c   the font setting, size steps
  bookmark.c   bookmarks, as BOOKMARKn lines in EHYPVIEW.INF
src/gem/       the GEM program
  ehypview.c   viewer windows (up to six), events, the toolbar's drawing,
               menus and tooltips, pop-up boxes, bookmarks, AV protocol,
               startup
  toolbar.c    the toolbar's buttons and layout (no AES calls; tested on
               the host)
  pagedraw.c   a page laid out and drawn (the viewer's and a pop-up's)
  stmem.c      ST RAM for the pictures copied to the screen: a blitter
               can't read TT/Fast RAM (no AES calls; tested on the host)
  libwin.c     library window
  menu.c       the menu bar, built in code (no resource file)
  fontsel.c    the text font: selector dialog (from ECal), sizes, font protocol
  about.c      the About box (as EMail's, ECal's and EContact's), icon*.h
tools/hypdump  host command-line inspector, used by the tests
tools/icon     the icon (make_icon.py) and the desktop icon RSC (make_rsc.py)
tools/bump.py  counts VERSION up on every build
tests/         unit tests, golden tests, fixture generators
```

Rules for `src/core`:
- C99 subset. No GEM, and no file access except through `plat.h`.
  Only what the freestanding libc in `common/atari/` offers (no stdio
  streams, no `sscanf`). No globals except the LH5 scratch state.
- Text stays in the file's character set. GEM fonts use the Atari set, so
  the viewer never converts.
- Files are **not kept open** between calls. An accessory on plain TOS
  must not hold handles, because they belong to the program that was
  running when they were opened.
- Every parser takes untrusted input. The mutation tests run under
  AddressSanitizer.

## Finding hypertexts

### The index
- **Scanner.** It walks every root including all subfolders, up to 16
  levels deep.
  - A folder reachable from several roots, or through a symbolic link loop,
    is read once. Folders are identified by device and inode, or by their
    path ignoring case where there are no inodes (TOS).
  - The scan runs in small steps (one folder or one file per unit), so the
    GEM program stays responsive.
- **Sources of names.**
  - Every `.HYP` contributes its title data and all page names, read from
    its own index table without decompressing anything.
  - A `.REF` file beside it (matched ignoring case) adds aliases, labels
    and `@keywords`.
  - `ALL.REF` fills in the same for guides that have no REF of their own.
    Entries for files that don't exist are listed so the user can see
    them; that is RefCheck's job done automatically.
- **Cache.** The index is saved as a small text file. On the next start it
  loads instantly, and a rescan re-reads only files whose size, date or
  REF changed.
- **Default roots.** `PATHS`, `HYPFOLD` and the folder of `REF` from an
  existing `ST-GUIDE.INF`, then `*:\GUIDES` and `*:\GEMSYS\GUIDES`
  (`*:` = every hard disk).

### Resolving
- **Files** are found in this order:
  1. the path as written, when it has one
  2. the folder of the open hypertext
  3. the index (earlier roots first, readable files first)

  All matching ignores case. `FOO.HYP` finds `foo.hyp` and long names, and
  `*:\FOO.HYP` means "from anywhere".
- **Links** `FILE.HYP/node` split right after `.hyp`, so page names may
  contain slashes. Without an extension, the split is at the first
  separator, or the last when the file's ST-Guide flag allows folders in
  links. `FILE.HYP/Main` means the default page when there is no page
  called `Main` (that is what HCP writes for links to a whole file).
- **Pages** match exactly first, then ignoring case, then as an alias or
  label. A bare name is looked up in the open hypertext first, then
  everywhere; other places with the same name are reported too.
- **Help requests** (`VA_START`, `AC_HELP`, command line) accept
  ST-Guide's forms: `'C:\AES.HYP appl_init'`, `'*:\AES.HYP'`,
  `'C:\AES.REF appl_init'` and a bare `appl_init`, with AV-protocol
  quoting.
- **Nothing found:** the viewer gets "did you mean" suggestions instead of
  a dead end. Matching is ST-Guide style ("similar"): case, umlauts
  written as ae/oe/ue, blanks, `_` and `-` are ignored, a typed prefix
  counts, and one to three typos are tolerated depending on length.

## Testing

- `make test`: unit tests plus golden tests on the host.
- `make asan`: the same under AddressSanitizer and UBSan.
- `make m68k-test`: the same suite compiled as big-endian 68020 code and run
  under QEMU, to catch byte-order and alignment mistakes before they reach
  real hardware.
- `make hatari-test` builds `EHVCHECK.PRG` for the Atari and runs it in
  Hatari as a Falcon with EmuTOS. The program scans a test folder, decodes
  every page and image, resolves links and saves, reloads and reuses the
  cache, all through GEMDOS. Its report must match the same program built
  for the host, line for line. The program reports through NatFeats and
  ends the emulator with `NF_EXIT`, so the run takes a few seconds.
- `make gui-test` drives `EHYPVIEW.PRG` in Hatari through its command
  FIFO with the keyboard: following links (also into a hypertext three
  folders down, found by the background index), back, help, next page and
  quit; then the library: opening it, searching names as you type,
  opening a hit, the full-text search and opening its hit; closing both
  windows (the program, with its menu bar, stays) and reopening the
  library; font sizes with the keyboard and choosing one in the font
  selector, which must end up in `EHYPVIEW.INF` (and a run started with
  that file must come up in that font). Another run
  starts without `DEFHYP` and must open the library. The viewer reports
  its page changes, searches and selections through NatFeats, and each
  step waits for the expected event. A second run boots with
  `EHYPVIEW.ACC` and checks that the accessory registers and indexes.
  Every step leaves a screenshot in `build/gui/`. A mouse run puts
  Hatari in a window on Xvfb and moves the mouse with xdotool (as
  EContact's rig does): clicking a link, the right button for back, the
  About box from the Desk menu, the font selector, Navigate and File
  items, and the toolbar: Index, Back, Forward, the history on Back's
  right button, See also, a tooltip, the Bookmarks and menu buttons, the
  library. A last run has the accessory in the root and the program in
  `C:\EHV`: the program, started from the desktop, writes `FOLDER=` and
  its index there; the accessory, chosen meanwhile, hands it to the
  program; the font the program chose is the accessory's when it next
  opens (the AERA guidelines, 2.3 and 6.1). Two more runs give the Falcon
  64 MB of Fast RAM, where malloc returns Fast RAM: a page's pictures
  must still be in ST RAM, in the program and in the accessory (which
  reserves its ST RAM at boot). `tests/test_menu.c` also checks
  the menu tree (links, positions on 320- to 1280-pixel screens, commands)
  on the host, and `tests/test_toolbar.c` the toolbar's layout (every
  button at a window's width, the least needed going first as it narrows),
  `tests/test_stmem.c` and `test_stmem_acc.c` the pictures' ST RAM (blocks
  apart and intact, joined when freed, the accessory's one block).
- `tests/test_index.c` builds a real folder tree for the scanner and
  resolver tests: mixed-case names, a folder with spaces, a duplicate, a
  damaged file, a symbolic link loop, a REF beside a file and an ALL.REF
  naming a missing guide.
- Fixtures are generated from our own sources (`tests/tools/mkfixtures.py`)
  and compiled by HypView's `hcp`, used only as an external tool.
- `hcp -v` also provides an **independent** text rendering that our output
  must match.

## Roadmap

1. ~~Core library: HYP, LH5, pages, REF, INF, with tests~~ ✔
2. ~~Folder scanner and index cache. Resolving `file.hyp/node` links and
   bare node names without `ALL.REF`.~~ ✔
3. ~~Atari build with the shared runtime; the platform layer on GEMDOS,
   tested in Hatari.~~ ✔ ~~The first GEM version: one window, page display with attributes and
   graphics, links, back/forward, answering `VA_START` and `AC_HELP`.
   Tested in Hatari/ARAnyM and on the Falcon.~~ ✔ (in Hatari; the Falcon
   and MiNT/MagiC are still to try)
4. ~~Library window and search.~~ ✔ ~~Menu bar.~~ ✔ ~~Font choice.~~ ✔
   (GDOS fonts are untested: EmuTOS in Hatari has no GDOS)
5. ~~Pop-ups in a box over the page.~~ ✔ (modal, as ST-Guide's: the next
   click or key closes them) ~~Several windows.~~ ✔ ~~Bookmarks.~~ ✔
   ~~A toolbar (ST-Guide's buttons, back and forward, See also,
   tooltips).~~ ✔ Tabs, settings dialog, colour images, tooltips through
   BubbleGEM when it runs.
6. Accessory build, FireBee build, release packaging.
