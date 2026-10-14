# EHypView

A new, modern viewer for **ST-Guide hypertexts** (`.HYP`) on the Atari
(Falcon, TT, FireBee). It is meant to replace `ST-GUIDE.ACC`:
- it finds every guide without `ALL.REF` maintenance
- it has a library window and one search box
- it has multiple windows, tabs and unlimited history
- programs that call ST-Guide for help keep working 

**Status:** the portable core library is done and tested.
- It reads `.HYP` files, decompresses LH5, parses pages, and reads `.REF`
  and `ST-GUIDE.INF`.
- It finds every hypertext below a set of folders, including all
  subfolders, and keeps an index of them with a cache.
- It resolves links, page names and help requests, with "did you mean"
  suggestions.

- It builds into Atari programs and is tested in Hatari (Falcon, EmuTOS).

**The first GEM viewer** (`EHYPVIEW.PRG`, also `EHYPVIEW.ACC`) works:
- one window with styled text, links, lines, boxes and images
- links followed with the mouse or keyboard, including into other hypertexts
  anywhere in the index
- history, help requests (`VA_START`, `AC_HELP`)
- it opens `DEFHYP` from an existing `ST-GUIDE.INF`
- a background index scan with cache

Still to come: the library window and search, a menu bar, several windows
and tabs, font choice, and pop-ups as small windows (for now they open as
pages). See [docs/DESIGN.md](docs/DESIGN.md) for the
assessment of ST-Guide, the goals and the roadmap, and
[docs/FORMAT.md](docs/FORMAT.md) for the file formats.

## Building and testing (host)

```sh
make test        # unit + golden tests
make asan        # the same with AddressSanitizer/UBSan
make m68k-test   # run the suite as big-endian 68020 code under QEMU
                 # (apt install gcc-m68k-linux-gnu qemu-user-static)
```

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

## Building for the Atari

The Atari programs are built the same way as ECal: with Ubuntu's stock m68k
compiler and no MiNTLib. `atari/` holds a small freestanding runtime
(start-up code, libc, GEMDOS file access, AES/VDI bindings), and
`tools/elf2tos.py` writes the TOS program file.

```sh
sudo apt install gcc-m68k-linux-gnu      # Debian/Ubuntu
make atari          # build/hatari/: EHYPVIEW.PRG, EHYPVIEW.ACC, EHVCHECK.PRG
make hatari-test    # run EHVCHECK.PRG in Hatari as a Falcon with EmuTOS
make gui-test       # drive EHYPVIEW.PRG in Hatari with the keyboard
                    # (apt install hatari; screenshots in build/gui)
```

### Using the viewer

`EHYPVIEW.PRG` opens the hypertext given on its command line, else `DEFHYP`
(or `HYPTOP`) from `ST-GUIDE.INF` in the boot drive's root. It looks for
hypertexts in the `PATHS` from that file, the folder of `REF`, `\GUIDES` and
`\GEMSYS\GUIDES`, including all subfolders. The index is kept in
`EHYPVIEW.IDX` next to the program. To replace the ST-Guide accessory,
copy `EHYPVIEW.ACC` to the boot drive's root as `ST-GUIDE.ACC`, so that
programs asking for ST-Guide find it.

| Keys | Action |
|---|---|
| click a link, or Tab / Shift+Tab then Return | follow a link |
| Backspace, Undo, right mouse button | back |
| Help | the hypertext's help page |
| Control+I / Control+T | index / table of contents |
| Control+Left / Control+Right | previous / next page |
| Control+Clr/Home | main page |
| arrows, Shift+arrows | scroll by line / page |
| Clr/Home, Shift+Clr/Home | top / bottom |
| Control+O | open a hypertext |
| Control+W or Control+U / Control+Q | close / quit |

`EHVCHECK.PRG` is a self-test, not part of the viewer. It exercises the core
library through GEMDOS on a test folder: scanning, every page and image,
links, and the cache. `make hatari-test` compares its report with the same
program built for Linux. To run it on a real Atari, copy `build/hatari/`
(the program and its `FIXT` folder) to a drive and start `EHVCHECK.PRG`.

## License

EHypView is free software under the GNU General Public License, version 2
or (at your option) any later version. See [COPYING](COPYING).

## Test fixtures

The `.hyp`/`.ref` files in `tests/fixtures` are generated from EHypView's own
sources by `tests/regen.sh`. That script uses the `hcp` compiler from
[HypView](https://github.com/th-otto/hypview) as an external tool. No HypView
code is included in EHypView.
