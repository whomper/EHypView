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

The GEM application is next. See [docs/DESIGN.md](docs/DESIGN.md) for the
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

This needs the [m68k-atari-mint cross tools](http://vincent.riviere.free.fr/soft/m68k-atari-mint/).

```sh
make atari       # 68020-60 (Falcon, TT)
make firebee     # ColdFire (FireBee)
```

For now these build only the core library.

## License

EHypView is free software under the GNU General Public License, version 2
or (at your option) any later version. See [COPYING](COPYING).

## Test fixtures

The `.hyp`/`.ref` files in `tests/fixtures` are generated from EHypView's own
sources by `tests/regen.sh`. That script uses the `hcp` compiler from
[HypView](https://github.com/th-otto/hypview) as an external tool. No HypView
code is included in EHypView.
