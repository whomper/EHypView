# EHypView - user guide

A hypertext viewer for the Atari ST, STE, TT and Falcon: EHypView reads and searches `.HYP`
hypertexts, the help that Atari programs come with. It runs as a program or as a desk accessory,
finds every hypertext on your drives by itself, and is the help viewer your other programs open
when you press Help.

## Installing

| File | What it is |
|---|---|
| `EHYPVIEW.PRG` | the viewer as a program, with a menu bar |
| `EHYPVIEW.ACC` | the same viewer as a desk accessory (one program under two names) |
| `EHYPVIEW.HYP` | this guide (and `EHYPVIEW.REF`, its index for ST-Guide) |
| `EHYPVIEW.RSC`, `.ICN` | the desktop icon, in the zip's `icons` folder |

- **The program:** copy `EHYPVIEW.PRG` into any folder and start it. It keeps its index
  (`EHYPVIEW.IDX`), its list of fonts (`EHYPVIEW.FNT`) and any `CRASH.LOG` next to itself, so a
  folder of its own, such as `C:\EHYPVIEW\`, keeps them together; `EHYPVIEW.INF` goes in the root
  of the boot drive. To open a `.HYP` with a double-click, install `EHYPVIEW.PRG` on your desktop as the
  application for `HYP` files.
- **The accessory:** copy `EHYPVIEW.ACC` to the root of your boot drive and restart. **To be the help
  viewer for every program**, copy it there as `ST-GUIDE.ACC` instead (in place of any accessory with
  that name): that is the name programs look for when you press Help.
- **This guide:** copy `EHYPVIEW.HYP` and `EHYPVIEW.REF` into `C:\GUIDES\` (or any folder
  EHypView looks in), or next to `EHYPVIEW.PRG`. **Help > EHypView guide** opens it.
- **Your hypertexts:** keep each `.HYP` with its `.REF` where EHypView looks for them, with all
  their subfolders: `\GUIDES` and `\GEMSYS\GUIDES` on every drive, the folders in `PATHS`,
  `HYPFOLD` and `REF` from `ST-GUIDE.INF`, and the folders you add in [Settings](#settings).

The program starts with the hypertext given on its command line, else `DEFHYP` (or `HYPTOP`) from
the `ST-GUIDE.INF` in your boot drive's root, else [the library](#the-library).

**To update,** copy the new files over the old ones. `EHYPVIEW.INF`, with your settings and
bookmarks, and the index stay; an index from an older EHypView that the new one can't read is made
again.

**Needs:** any ST, STE, TT or Falcon with 1 MB or more; TOS, EmuTOS, MagiC or MiNT; a screen 640
pixels wide or more (on a narrower one the accessory still shows the pages other programs ask for). NVDI, SpeedoGDOS or another GDOS is optional, for more fonts.

**Under TOS, put Atari's FOLDR100.PRG first in AUTO**, renamed for more room (FOLDR400.PRG).
TOS keeps an entry in a small internal pool for every folder a program or accessory has looked
into, and halts with "OUT OF INTERNAL MEMORY" when it runs out; FOLDRxxx makes the pool bigger.
EHypView looks into every folder of its library, with their subfolders, when it makes its index.

## The viewer

The viewer window shows one page of a hypertext. Its title is the hypertext's and the page's, and
the line under it the file it is in, or what EHypView has to say: a link that leads nowhere, and
the names closest to it ("did you mean").

- **Links** are underlined, and in colour on a screen with 16 colours or more (bold with fewer).
  Click one, or move to it with Tab and
  ⇧Tab and press Return. Links lead into any hypertext in the library, wherever it is.
- **⇧-click a link**, or ⇧Return, to follow it in a new window.
- **Back:** Backspace, Undo, the toolbar's back arrow, or the right button's menu. ⇧Undo goes
  forward again. The history has no limit.
- **Pop-ups** (links to a `@pnode`) open as a small framed box below the link. A link in it can be
  followed; any other click, or any key, closes it.
- **Pictures** and coloured text show in colour in every screen mode: 4, 16 and 256 colours, high
  and true colour, and black and white on a mono screen. With too few colours they are dithered,
  and in ST medium they keep their shape.

The right mouse button, or Insert, opens a menu: Back and Forward first (and **Link in new window**
on a link), then the pages, **New window**, **Add bookmark**, **Copy page**, **Library and
search**, **Font...** and **About EHypView...**.

## The toolbar

At the top of each viewer window, drawn like the other AERA programs' toolbars:

| Button | What it does |
|---|---|
| back, forward | back and forward; the right button lists the pages that way, to jump several at once |
| house | the hypertext's main page |
| < Contents > | the page before, the contents, the next page |
| Index | the hypertext's index |
| See also | the pages the page names with `@xref`, as a menu |
| Library | [the library](#the-library) |
| Bookmarks | add or remove this page, or go to a [bookmark](#bookmarks) |
| ≡ | a menu of the menu bar's commands, About first: the accessory, which has no menu bar, finds them here |

A button that can't be used on the page shown is greyed out, and each one says what it does when the
mouse rests on it for a second. In a narrow window the least needed buttons make room first; the ≡
menu and the keys still reach everything.

## The library

**File > Library and search** (^L, or the toolbar's **Library**) lists every hypertext found,
grouped by subject, with one search field at the top. It is also what you see when no page was
asked for.

- **Type to search.** Matching page names, aliases and keywords show at once: exact matches first,
  then names starting with what you typed, then names containing it, then similar ones.
- **Search the full text.** Return on **Search the full text for "..."** searches every page of
  every hypertext, in the background.
- **Bookmarks** come first: all of them with an empty search, the ones that match while you type.
- **Keys:** Up and Down to choose, Return or a click to open, Backspace to take out a letter,
  Delete to remove the bookmark chosen. ^V pastes into the search, ^X cuts it, ^C copies the
  hypertext chosen. Help opens this guide. Esc leaves the search as it is.

EHypView looks through its folders in the background, in short steps that pause while you use the
mouse or the keyboard, and keeps what it found in `EHYPVIEW.IDX`. The next time only new or changed
files are read, and when nothing changed nothing is written. There is no `ALL.REF` to keep up to date
(yours is still read).

## Bookmarks

**Bookmarks > Add bookmark** (^D, or the toolbar's **Bookmarks**) keeps the page shown. The
Bookmarks menu lists them, up to ten, and opens one in the window in front; **Remove bookmark**
forgets the page shown. They are first in the library too, where Return opens one and Delete
removes it. They are kept in `EHYPVIEW.INF`, which the program and the accessory share.

## Several windows

Each viewer window has its own page, history and place on the page. **File > New window** (^N) opens
the page shown in another; ⇧-click opens a link in one. Up to six are open at once, each a little
below and to the right of the last. The menu commands act on the window in front, and a
hypertext's "quit" link closes its own window.

## Fonts

**View > Font...** (⇧^F) chooses the font for the text, with the AERA programs' Font dialog, the
same as in ECal and EMail: the families on the left, each font's styles and sizes beside them, what
kind of font it is, and a sample. It lists the system font in 8 x 8 or 8 x 16 and, with NVDI,
SpeedoGDOS or another GDOS, every fixed-width GDOS font with letters. Hypertexts are laid out in
character cells, so proportional fonts are left out, as are fonts of symbols. ^+ and ^- make the text larger and smaller. A font dropped on a window from a font selector
(the font protocol) is used too. Your choice is kept in `EHYPVIEW.INF`, and which fonts can be
used in `EHYPVIEW.FNT`.

The accessory loads GDOS fonts only when the Atari starts: GDOS fonts loaded later would be lost
when the program in front ends. Under NVDI it always loads them, so its font selector lists the same
fonts as the program, and a font chosen is used at once. Under FONTGDOS or SpeedoGDOS it loads them
only when `EHYPVIEW.INF` names one, to spare a 1 MB ST their memory: there a GDOS font chosen in the
accessory, or in the program while the accessory is loaded, is the accessory's from the next boot,
and its Font dialog lists the fonts it knows "from the next start".

## Settings

**Options > Settings...** (^,; in the accessory, the toolbar's ≡ menu) shows what `EHYPVIEW.INF`
holds, on two pages. OK keeps the changes, Cancel (or Esc, or Undo) forgets them.

- **General:** when the accessory looks through the library: **When first opened** (the first time
  you open it, or a program asks it for a page), **At start**, **After a minute** or **After 5
  minutes**. It takes effect at the next start. The program looks as it starts. Under it,
  EHypView's folder: where the library's index (`EHYPVIEW.IDX`) and the fonts measured
  (`EHYPVIEW.FNT`) are kept, the folder `EHYPVIEW.PRG` was started from.
- **Library:** up to three more folders to look for hypertexts in, each with its subfolders. A
  folder that isn't there is asked about: **Keep** it (a drive you put in later), or **Change**
  it. OK with new folders looks through the library again.

The font is chosen with **View > Font...**, and bookmarks from the Bookmarks menu.

## Program and accessory

- **The program** has a menu bar, and keeps running when its windows are closed, until you choose
  **File > Quit** (^Q).
- **The accessory** has no menu bar: its commands are in the toolbar's ≡ menu, and **About
  EHypView...** in the right button's menu too. **EHypView** in the Desk menu opens the viewer, or
  the library when no page is shown. ^Q closes its windows; it stays loaded.
- **A quiet start.** At boot the accessory only registers and takes its memory; it reads nothing
  until you open it or a program asks it for a page (or as [Settings](#settings) says). Then it works
  in short steps between events, never holding the machine for long, and waits while you use the
  mouse or keyboard. A help request naming a file, as the AERA programs' Help keys do, is answered at
  once.
- **Memory.** The accessory takes its memory when it loads: a third of the largest free block (a
  quarter when there is no TT RAM), at most 2 MB, and 64 to 128 KB of ST RAM for the pictures.
- **With both installed,** they never work at once. While `EHYPVIEW.PRG` runs, **EHypView** in the
  Desk menu brings the program's window to the front, and a font chosen in the program is the
  accessory's too the next time it opens. Under MagiC or MiNT, where the accessory runs beside
  the programs, starting `EHYPVIEW.PRG` opens the accessory instead.

## With other programs

- **The Help key.** EMail, EFTP, ECal, EContact and ClaudeST open their guides in EHypView with the
  Help key when it runs (as an accessory, or as a program under MultiTOS or MagiC): they look for
  EHypView first, then ST-Guide and HypView. Any program or desktop can hand it a hypertext and a page the same way, with the AV protocol's `VA_START`, or as
  ST-Guide with `AC_HELP`.
- **Drag and drop.** Under MultiTOS, MagiC or XaAES, a hypertext dragged onto EHypView's windows
  opens. The accessory tells the desktop about its windows, so Thing and the like can drop one on
  them under plain TOS too.
- **The clipboard.** ^C copies the page shown, as text, to the GEM clipboard (`SCRAP.TXT`); in the
  library ^C copies the hypertext chosen, and ^V and ^X paste into and cut the search.
- **Iconify.** Where the AES can iconify windows (MultiTOS, MagiC, XaAES, N.AES), the viewer and the
  library shrink to EHypView's icon. A page asked for by another program brings the viewer back.

## Keys

Most menu items have a key, shown beside them in the menu.

| Keys | What they do |
|---|---|
| click a link, or Tab / ⇧Tab then Return | follow a link |
| ⇧-click a link, or ⇧Return | follow it in a new window |
| Backspace, Undo | back |
| ⇧Undo | forward again |
| ^Left / ^Right | the page before / after |
| ^Clr/Home | the main page |
| ^T / ^I | contents / index |
| Help | the hypertext's help page; in the library, this guide |
| arrows, ⇧arrows | scroll by a line / a page |
| Clr/Home, ⇧Clr/Home | top / bottom |
| right mouse button, Insert | the right button's menu |
| ^L | the library, and back to the viewer |
| ^N | a new window, with the same page |
| ^D | bookmark the page |
| ^O | open a hypertext |
| ^C | copy the page, as text |
| ⇧^F | choose the font |
| ^+ / ^- | larger / smaller text (on the keypad, or ^= and ^-) |
| ^, | settings |
| ^W or ^U | close the window in front |
| ^Q | quit (the accessory: close its windows) |

## EHYPVIEW.INF

The settings are in `EHYPVIEW.INF` in the root of the boot drive, where the accessory always finds
them. It is plain text with a note above each setting, and EHypView keeps the notes, and anything
it doesn't know, when it writes the file. [Settings](#settings) changes most of it.

| Setting | What it is |
|---|---|
| `FOLDER=` | EHypView's folder, for `EHYPVIEW.IDX` and `EHYPVIEW.FNT`: the program writes the folder it was started from |
| `START=` | when the accessory looks through the library: `opened`, `now`, or seconds after boot |
| `PATHS=` | more folders to look in, between `;` |
| `FONT=`, `POINT=` | the font and its size |
| `BOOKMARK1=` ... | the bookmarks: the hypertext, the page and the title, between `\|` |

`ST-GUIDE.INF`, ST-Guide's settings, is read and never written: its `PATHS`, `HYPFOLD`, `REF`,
`DEFHYP` and `HYPTOP` are used as ST-Guide uses them.

## When something goes wrong

- **A hypertext isn't in the library.** Is it in one of the folders EHypView looks in (see
  [Settings](#settings))? A new one is found the next time the library is looked through: when the
  program starts, when the accessory first opens after the Atari starts, or when you add a folder in
  Settings.
- **"Hypertext not found" or "Page not found".** The link names something that isn't in the
  library. The line under the title suggests the closest names.
- **Help in another program does nothing.** The accessory must be loaded (in the root of the boot
  drive) or, under MagiC or MiNT, the program running. Programs that look for ST-Guide find EHypView
  when the accessory is called `ST-GUIDE.ACC`.
- **If the program crashes** (what would be 2 to 8 bombs), it says so, writes what happened to
  `CRASH.LOG` next to `EHYPVIEW.PRG` and quits, leaving the Atari running. The next time it starts it
  offers to show the report.
- **In ST low** (320 pixels wide) EHypView says the screen is too narrow: use ST medium or more. The
  accessory still shows the pages other programs ask for there.
- **"The system has no window left for EHypView."** GEM has only a few windows to share between
  the programs and accessories: close one and try again.

## About EHypView

**About EHypView...** (the first item of the EHypView menu, or the toolbar's ≡ menu) shows the
version, the number of hypertexts found and the font in use.

EHypView is free software under the GNU General Public License, version 2 or later. Its source,
releases and what is new are at github.com/whomper/EHypView. It is one of the AERA programs, with
EMail, EFTP, ECal, EContact and ClaudeST, which look and work alike.

Created by AERA | Erez Yaary.
