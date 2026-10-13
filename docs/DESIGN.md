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
- Toolchain: m68k-atari-mint gcc with gemlib.

## Architecture

```
src/core/      portable C, no GEM: builds on the Atari and on the host
  lh5.c        LH5 decompressor
  hypfile.c    .HYP header, index, extended headers, object loading
  page.c       page byte stream -> lines of runs, graphics, xrefs
  ref.c        .REF / ALL.REF reader
  inf.c        ST-GUIDE.INF reader
  charset.c    Atari character set -> Unicode (host tools/export)
  (next) index.c   folder scanner + own index cache
  (next) image.c   planar image -> VDI raster for any screen depth
  (next) search.c  name, keyword and full-text search
src/gem/       (next) the GEM application
tools/hypdump  host command-line inspector, used by the tests
tests/         unit tests, golden tests, fixture generators
```

Rules for `src/core`:
- C99 subset. No GEM, no POSIX beyond stdio, no globals except the LH5
  scratch state.
- Text stays in the file's character set. GEM fonts use the Atari set, so
  the viewer never converts.
- Files are **not kept open** between calls. An accessory on plain TOS
  must not hold handles, because they belong to the program that was
  running when they were opened.
- Every parser takes untrusted input. The mutation tests run under
  AddressSanitizer.

## Testing

- `make test`: unit tests plus golden tests on the host.
- `make asan`: the same under AddressSanitizer and UBSan.
- `make m68k-test`: the same suite compiled as big-endian 68020 code and run
  under QEMU, to catch byte-order and alignment mistakes before they reach
  real hardware.
- Fixtures are generated from our own sources (`tests/tools/mkfixtures.py`)
  and compiled by HypView's `hcp`, used only as an external tool.
- `hcp -v` also provides an **independent** text rendering that our output
  must match.

## Roadmap

1. ~~Core library: HYP, LH5, pages, REF, INF, with tests~~ ✔
2. Folder scanner and index cache. Resolving `file.hyp/node` links and
   bare node names without `ALL.REF`.
3. First GEM version: one window, page display with attributes and
   graphics, links, back/forward, answering `VA_START` and `AC_HELP`.
   Tested in Hatari/ARAnyM and on the Falcon.
4. Library window and search.
5. Multiple windows, tabs, bookmarks, settings dialog, font selection,
   colour images.
6. Accessory build, FireBee build, release packaging.
