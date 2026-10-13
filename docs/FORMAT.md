# ST-Guide file formats

EHypView's own notes on the `.HYP` and `.REF` formats. They are based on the
format description shipped with the [HypView](https://github.com/th-otto/hypview)
documentation, then checked against real files produced by `hcp`.
**Corrections** marks places where the published description is incomplete or
misleading. Each correction is covered by a test.

All multi-byte numbers are big-endian unless noted otherwise.

## .HYP layout

| Part | Contents |
|---|---|
| Header (12 bytes) | `"HDOC"`, u32 index size, u16 entry count, u8 compiler version, u8 OS (0 unknown, 1 Amiga, 2 Atari, 3 Mac, 4 Windows, 5 Unix, 6 macOS) |
| Index | one record per entry (see below) |
| Extended headers | `u16 id, u16 length, data`, repeated until id 0 |
| Data | the objects, normally LH5 compressed |

### Index records

| Size | Field |
|---|---|
| 1 | record length, including this byte and the name padding |
| 1 | type: 0 page, 1 popup, 2 external reference, 3 image, 4 system, 5 REXX script, 6 REXX command, 7 quit, 8 close (HypView), 0xFF end-of-data marker |
| 4 | file offset of the data |
| 2 | `comp_diff`: unpacked size minus packed size (low 16 bits) |
| 2 | next page (images: high bits of the size, see below) |
| 2 | previous page |
| 2 | table-of-contents page |
| n | NUL-terminated name, padded to an even length |

- Packed size = offset of the next entry − own offset.
- Newer compilers write a final 0xFF entry whose offset marks the end of the data. Without it, the end of the file is used.
- Unpacked size = packed + `comp_diff`, plus `next << 16` **for images only** (images can exceed 64 KB).
- An object is compressed when its unpacked size differs from its packed size.
- External reference names have the form `FILE.HYP/node`.

### Extended headers

| Id | Meaning | Id | Meaning |
|---|---|---|---|
| 1 | `@database` | 7 | `@help` |
| 2 | `@default` | 8 | `@subject` |
| 3 | `@hostname` | 9 | HypTree title bitmap |
| 4 | `@options` | 10 | ST-Guide flags |
| 5 | `@author` | 11 | `@width` |
| 6 | `@$VER:` | 30 | charset name (HypView) |
|   |   | 31 | language (HypView) |

Unknown headers must be skipped.

**Correction:** headers 10 and 11 hold a 2-byte value of which HCP fills only the
**first** byte. Read them little-endian. Flag `0x0002` means "encrypted".

### Encryption

Encrypted files XOR every data byte with 127. This is applied **after**
decompression (on disk: `lh5(plain XOR 127)`).

### LH5

This is LHarc `-lh5-`: LZSS with an 8 KB window and static Huffman blocks.
- Each block starts with a 16-bit symbol count, followed by three table descriptions:
  - T: 19 symbols, 5-bit count, special run after symbol 3.
  - C: 510 symbols, 9-bit count.
  - P: 14 symbols, 4-bit count.
- Bits are read MSB first.
- Symbols 0–255 are literals. Symbol `c` ≥ 256 is a match of length `c − 253`, at distance `p + 1`, where `p` comes from the P code plus `slot − 1` extra bits.
- A big image needs several blocks; the test fixture uses 4.

## Text pages

A page is a byte stream: plain bytes are text, NUL ends a line, and ESC (27) starts a command.

Numbers marked *enc* use base 255 with every digit stored plus 1, so no
NUL bytes appear: `value = (b1 − 1) × 255 + b0 − 1`.

| Command | Bytes after ESC | Meaning |
|---|---|---|
| `1B` | — | literal ESC |
| `23` | title, NUL | `@title` (no padding byte follows) |
| `24` / `26` | node(enc), len, [text] | link / alink |
| `25` / `27` | line(enc), node(enc), len, [text] | link / alink to a line |
| `28`–`2E` | len, data | data block (len counts from ESC) |
| `2F` | len=5, u16 mask | dither mask for the next image |
| `30` | len, node(enc), text, NUL | `@xref` ("See also") |
| `31` | line, tree, object, node (all enc) | `@tree` object table |
| `32` | image(enc), x, y(enc), w, h | `@image` / `@limage` |
| `33` | x, y(enc), w, h, attr | `@line` |
| `34` / `35` | x, y(enc), w, h, pattern | `@box` / `@rbox` |
| `64`–`A3` | — | text attributes = value − 0x64: 1 bold, 2 ghosted, 4 italic, 8 underlined, 16 outlined, 32 shadowed |
| `A5` / `A6` | colour | foreground / background colour (HypView) |

Link text: `len` ≤ 32 means "show the target's name"; otherwise `len − 32` bytes of text follow.

**Corrections:**
- Link line numbers are stored **0-based**. A source line 3 is stored as 2.
- `@xref` text is stored with blanks around it; trim them.
- `@line`:
  - Width is stored **+128**, so it can be negative (right-to-left).
  - Height is stored +1.
  - `attr` is stored +1. Bits 0–1 are the arrows (1 start, 2 end); `(attr >> 3) + 1` is the VDI line style 1–7.
- `@box`/`@rbox`: the pattern is stored +1.
- `@limage` stores `w == 1`.
- Graphics x/y are character cells. y is the text line where the command appeared; x = 0 centres an image.
- Dither mask bits are indexed by **hardware pixel value**. The compiler remaps the `%` bits written in source order to these values.

## Images

| Size | Field |
|---|---|
| 2 | width in pixels |
| 2 | height in pixels |
| 1 | planes (1–8) |
| 1 | planes present (bit n = plane n is stored) |
| 1 | planes filled (bit n = absent plane is all ones) |
| 1 | padding |
| … | planes, each `((width + 15) / 16) × 2 × height` bytes |

## .REF files (and ALL.REF)

```
"HREF"
module*:  u32 data length, u32 entry count, entries
end:      8 zero bytes
entry:    u8 kind, u8 total length (incl. these 2 bytes), string NUL [, u16 line]
```

| Kind | Meaning |
|---|---|
| 0 | file name (often without path or extension; `.HYP` is implied) |
| 1 | page name |
| 2 | alias of the preceding page |
| 3 | label of the preceding page, followed by a u16 line |
| 4 | database name |
| 5 | OS as a digit |
| 6 | title of the preceding page |
| 30 | charset (HypView) |

**Corrections:**
- Kinds 30, 5 and 4 can appear **before** the file name entry, so they belong to the module, not to "the last file entry".
- `@keywords` are written as labels on line 0.
- HCP writes plain `@label`s only when asked to.
