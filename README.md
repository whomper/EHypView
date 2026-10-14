# EHypView

<img src="docs/icon16.png" width="96" align="right" alt="EHypView icon: an Atari SM124-style monitor showing a page whose link points to another page">

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
- a menu bar (Desk, File, Navigate, View) in the application; every item has a
  keyboard shortcut, and items that can't be used right now are greyed out.
  With the menu bar the program keeps running when its windows are closed,
  until you choose Quit.
- a choice of font for the text (View > Font...), with the same font
  selector as ECal and EMail. You get the system font in 8x8 or 8x16
  and, with NVDI, SpeedoGDOS or another GDOS, all monospaced GDOS fonts
  (bitmap, Speedo, TrueType, Type 1). Hypertexts are laid out in character
  cells, so proportional fonts are shown but can't be picked. Control+Plus
  and Control+Minus change the size. A font dropped on a window from a
  font selector (the font protocol) is used too. The choice is kept in
  `EHYPVIEW.INF` next to the program, and the list of which fonts are
  monospaced in `EHYPVIEW.FNT`.

**The library window** (Control+L, and what you see when no page was
asked for) lists every hypertext found, grouped by subject, with one search
box:
- Typing shows matching page names, aliases and keywords at once (exact,
  then starting with, then containing, then similar), plus matching titles.
- Return on "Search the full text" searches every page of every hypertext
  in the background; ST-Guide needed the separate HypFind tool for that.

Still to come: several viewer windows and tabs, and pop-ups as small windows (for now they open as pages). See [docs/DESIGN.md](docs/DESIGN.md) for the
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
compiler and no MiNTLib. `common/atari/` holds a small freestanding
runtime (start-up code, libc, GEMDOS file access, AES/VDI bindings), and
`common/tools/elf2tos.py` writes the TOS program file. `common/` is the
AERA programs' shared code, a copy of
[AERA-Atari](https://github.com/whomper/AERA-Atari)'s `common/` folder:
change it there and bring the copy up to date with `tools/sync-common.sh`.

```sh
sudo apt install gcc-m68k-linux-gnu      # Debian/Ubuntu
make atari          # build/hatari/: EHYPVIEW.PRG, EHYPVIEW.ACC, EHVCHECK.PRG
make hatari-test    # run EHVCHECK.PRG in Hatari as a Falcon with EmuTOS
make gui-test       # drive EHYPVIEW.PRG in Hatari with the keyboard and mouse
                    # (apt install hatari xvfb xdotool; screenshots in build/gui)
make icons          # draw the icon again (tools/icon/)
```

Every build of the program counts the version up (`VERSION`, major.minor,
shown in the About box), as in EMail, ECal and EContact, and copies the
programs to `binary/`. Commit `VERSION` together with `binary/`.

`binary/` holds the programs as released, and `binary/icons/` holds the desktop icon:
`EHYPVIEW.RSC`, a resource with the icon in monochrome (G_ICON) and 16
colours (G_CICON), each with its mask. Copy them into `DESKICON.RSC` /
`DESKCICN.RSC` with a resource editor (Interface, ORCS, RSM...).
`EHYPVIEW.ICN` and `EHYPVWMK.ICN` are the image and its mask in ICN text
format. The icon is the same SM124-style monitor as EMail's, ECal's and
EContact's, here showing a hypertext: a page whose link points to another
page.

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
| Backspace, Undo | back |
| right mouse button, Insert | a menu: Back first, the pages, Library, Font, About last |
| Help | the hypertext's help page |
| Control+I / Control+T | index / table of contents |
| Control+Left / Control+Right | previous / next page |
| Control+Clr/Home | main page |
| arrows, Shift+arrows | scroll by line / page |
| Clr/Home, Shift+Clr/Home | top / bottom |
| Control+L | the library (and back to the viewer) |
| Control+O | open a hypertext |
| Control+W or Control+U / Control+Q | close the top window / quit |
| Shift+Control+F | choose the font |
| Control+Plus / Control+Minus (keypad, or `=` and `-`) | larger / smaller text |

Desk > About EHypView shows the version, the number of hypertexts found
and the font in use. EMail, EFTP, ECal, EContact and ClaudeST open their
guides in EHypView with the Help key when it runs (as an accessory, or
as a program under MultiTOS or MagiC), and any program or desktop (Thing,
jinnee, Gemini) can hand it a hypertext and a page the same way, with the
AV protocol's VA_START. If the program itself crashes
(what would be 2 to 8 bombs), it says so, writes what happened to
`CRASH.LOG` next to `EHYPVIEW.PRG` and quits, leaving the Atari running;
under MiNT or MagiC, and in the accessory, there is no `CRASH.LOG`. The accessory has no menu bar: About is in the right button's menu. Its entry in the Desk menu (EHypView) opens the viewer,
or the library when no page is shown.

In the library: type to search, Up/Down to choose, Return or a click to
open (or to start the full-text search), Esc to clear the search,
Backspace to delete a letter.

Where the AES can iconify windows (MultiTOS, MagiC, XaAES, N.AES), the
viewer and the library have the iconify button: each shrinks to
EHypView's icon. A page asked for by another program (the Help key)
brings the viewer back.

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
