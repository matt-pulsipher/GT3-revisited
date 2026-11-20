# Text and fonts

Worked out from PBPX-955.03 (v1.10): the files under `font/` and `text/`,
plus how the game uses them (its observable behaviour; only facts are
recorded here). Code: `src/formats/text/` (decoders and layout),
`src/text/game_font.h` (SDL rendering API), `gt3tool text` / `text-render`,
screen `text`.

| File | Magic | Used by the game | Contents |
|---|---|---|---|
| `text/realtime.dat` | `MLTX` | yes | 193 in-race / memory card / parts strings × 7 languages |
| `font/system.fnt` | `FT01` | yes | fonts `sw_16`, `sw_12` (default), `sq_9`, `sw_20` |
| `font/menu.fnt` | `FT01` | yes | digit fonts `speed`, `meter`, `odo`, `rank`, `gearr` |
| `font/system.kf` | `KNJI` | yes | 22×22 kanji font, 7,318 glyphs |
| `font/jis2uni.dat` | `UTBL` | no (used by us) | JIS X 0208 (+extensions) and Shift-JIS rows → Unicode |
| `font/kanji22.kf` | `KFT0` | no | older 22×22 kanji font, 7,482 glyphs (incl. NEC row 13) |
| `font/fonttest.dat` | `FT01` | no | test font "mokeke", older FT01 layout (not decoded) |
| `font/font.dat` | `FT00` | no | older font container with `UTex` textures (not decoded) |

A scan of all 5,333 VOL files for `MLTX`, `FT00`, `FT01`, `KNJI`, `KFT0`
and `UTBL` finds only these files. Menu text lives inside the `.pmb` files
(menu workstream) and uses the encoding below.

## Text encoding

Strings are byte strings walked by the renderer as follows:

| Bytes | Meaning |
|---|---|
| `00` | end |
| `0A` / `0D` | new line / back to the line start |
| `20..7F` | ASCII (Latin font) |
| `80+N, c` (N = 0..15) | character `c` (0x20..0xFF) of ISO-8859-N, mapped to a Latin font code |
| `A1..FE, A1..FE` | EUC-JP: JIS X 0208 code `(b0 & 7F) << 8 | (b1 & 7F)`, drawn with the kanji font |
| `81..A0` other | invalid |

Page 0 means "raw font code" (Latin-1 plus the Windows-1252 symbols at
0x80..0x9F). Page 1 (`0x81`) is what the European menus use, for example
French `d\x81\xE9j\x81\xE0` = "déjà". Pages 2..9, 14 and 15 are the standard
ISO-8859 tables, pages 10..13 fall back to Latin-1. The game's tables differ
from today's standards in a few places: 8859-6 maps ASCII digits to
Arabic-Indic digits, 8859-7 is the 1987 edition (0xA1/0xA2 = U+02BD/U+02BC,
no 0xA4/0xA5/0xAA), 8859-8 has 0xAF = U+203E and no 0xFD/0xFE, and 8859-15's
euro maps to font code 0x80. Our tables were generated from the standards
plus these differences and checked to match the game's behaviour for all
16 pages.

Japanese text is EUC-JP; there are no half-width katakana (`8E` is the
ISO-8859-14 escape). Full-width ASCII (`ＧＴ３`) and symbols such as `°`
(`A1 EB`) also come from the kanji font when written in EUC-JP.

**Latin font codes** are Unicode for U+0020..U+017F (Latin-1 and Latin
Extended-A), except that 0x80..0x9F hold the Windows-1252 symbols (0x80 = €,
0x99 = ™, ...).

## jis2uni.dat (`UTBL`)

```
0x00 "UTBL"
0x04 u16 row count (87), u16 total entries (18126)
0x08 u32 row table offset (0x10)  -> {u8 first low byte, u8 high byte, u16 first index}
0x0C u32 data offset (0x16C)      -> u16 Unicode values
```
Row `i` covers indices `[first_index(i), first_index(i+1))` for codes
`high << 8 | (first_low + k)`; row 0 (high 0) is the identity for single
bytes. High bytes 0x21..0x74 are JIS X 0208 rows; 0x81, 0x87, 0x88, 0xE0,
0xED and 0xFA are Shift-JIS lead bytes. Value 0 = unmapped. Compared with
JIS X 0208 it maps 0x2140 to U+005C (not U+FF3C) and adds Polyphony's own
assignments in otherwise empty cells: Latin/Greek/Cyrillic letters and
diacritics in rows 0x22, 0x26, 0x27, 0x29..0x2B (e.g. 0x2A31 = É) and a few
extra kanji in row 0x4F. NEC row 13 (0x2D, circled numbers) is not mapped
although `system.kf` has glyphs for it. 7,188 JIS codes map.

## MLTX string tables (`text/realtime.dat`)

```
0x00 "MLTX"
0x04 u32 version = 2
0x08 u32 pool offset             (0x1B7B)
0x0C u32 entry count             (193)
0x10 u32 pool size               (0xB91)
0x14 entries[count]: u32 key offset (absolute), u32 value offset (relative to a pool)
...  keys: NUL-terminated ASCII, sorted ("DATABASEPARTS_PTEXT_ASCC", "RACEDISPLAY_PitIn", ...)
...  7 pools, pool L at pool offset + L * pool size
```
Every language pool has the same layout: each value is padded with NULs to
the longest translation so one offset works for all languages (the file
ends exactly after pool 6). Language index order is the game's language
enum: **0 JP, 1 US, 2 BR (British English), 3 FR, 4 GR (German), 5 IT,
6 SP** (the same codes name the `menu/<LANG>/` folders).

The game has groups of string slots with built-in default texts; on load and on language change it looks up
`<group prefix>_<name>` and replaces the default only when the value in the
current language's pool is non-empty. So **an empty value means "use the
built-in default"**. On this disc JP and US have 193/193 values, BR 135/193,
FR/GR/IT/SP 0 (all zero bytes). Values are printf formats where needed
(`%s Pitting In...`, `%d MORE LAPS`). All 7 × 193 values decode without
unmapped characters.

#### More pools (Gran Turismo Concept)

The later games add languages and their files hold more pools; `StringTable` reads every whole
pool after the pool offset (up to the 11 languages of `text::Language`) and takes the order of
the ones after SP from the pool count:

| Pools | Order after SP | Seen on |
|---|---|---|
| 7 | - | GT3; Concept 2001 Tokyo (which has a `menu/KR/` folder but no Korean pool) |
| 9 | KR, TW | Concept 2002 Tokyo-Seoul |
| 11 | PT, KR, CN, TW | Concept 2002 Tokyo-Geneva (both releases) |

The order of 11 is the language table of those games' programs (JP, US, BR, FR, GR, IT, SP, PT,
KR, CN, TW: the codes of the `menu/<LANG>/` folders), and `text::Language` follows it. The
9-pool file predates PT: the 2001 program's table ends ..., SP, KR, and the pools' contents
say which is which (pool 7 is EUC-KR text, pool 8 the same Chinese bytes as the 11-pool file's
pool 10). The Tokyo-Seoul program's own table is already the order of 11, so by its index it
would read pool 7 as PT and pool 8 as KR; the keys are GT3's parts shop and race messages, and
whether that game shows them from this file was not checked.

- **KR** values are EUC-KR: a pair `A1..FE, A1..FE` is the KS X 1001 code with the high bits
  set, the code `font/system.hf` is indexed by.
- **TW** values are Big5's characters renumbered, 94 to a row from 0x2121, so that they fit the
  same pair rule: `index = (lead - 0xA1) * 157 + (trail < 0x7F ? trail - 0x40 : trail - 0xA1 + 63)`,
  `code = (0x21 + index / 94) << 8 | (0x21 + index % 94)` (`tw_code_to_big5`, `big5_to_tw_code`).
  `font/system.tf` is indexed by that code. Checked: all 215 values of the 11-pool file turn
  into valid Big5 this way and read as the Chinese of the other pools' text.
- **CN** is empty (0 of 215) and **PT** partial (125 of 215, Latin text with the `81` escapes).
  The `menu/CN/` pages of every disc hold Korean text.
- The discs have no table from KS X 1001 or Big5 to Unicode, so `gt3tool text` writes these
  pools' pairs as `{XXXX}` font codes. The later discs' Korean pool uses one code (0x2D50)
  five times that `system.hf` has no glyph for.

## FT01 bitmap fonts (`*.fnt`)

```
0x00 "FT01"
0x10 u16 font count
0x14 u32 offset of the font offset table    (u32[count])
0x18 u32 offset of the texture offset table (u32[count] -> embedded Tex1 set per font)
```
Font record (0x20 bytes; +0x14..0x1F are zero on disc and used at run time,
+0x1C receives the texture pointer):

| Offset | Type | Meaning | sw_16 |
|---|---|---|---|
| 0x00 | u32 | name offset | `sw_16` |
| 0x04 | u32 | glyph table offset | |
| 0x08 | u16 | first code | 0x20 |
| 0x0A | u16 | last code | 0x17F |
| 0x0C | u8 | size (read by an accessor; role not confirmed) | 22 |
| 0x0D | u8 | line height | 28 |
| 0x0E | s8 | ascent (baseline below the line top) | 21 |
| 0x0F | u8 | kerning profile rows (= line height) | 28 |
| 0x10 | u8 | kanji width | 22 |
| 0x11 | u8 | kanji height | 22 |
| 0x12 | s8 | kanji x offset | -2 |
| 0x13 | s8 | kanji y offset (from the baseline) | -20 |

Glyph record, 16 bytes per code from first to last (all-zero = no glyph):

| Offset | Type | Meaning |
|---|---|---|
| 0x0 | s8 | left bearing |
| 0x1 | s8 | top, relative to the baseline |
| 0x2 | u8 | width |
| 0x3 | u8 | height |
| 0x4 | u8 | advance |
| 0x5 | u8 | atlas x |
| 0x6 | u16 | atlas y |
| 0x8 | u32 | left kerning profile offset (0 = none) |
| 0xC | u32 | right kerning profile offset (0 = none) |

A kerning profile is `profile rows` signed bytes (stored padded to 16-byte
multiples with `0x55` filler), one per pixel row of the line: the gap
between the cell edge and the ink in that row. Digits share an all-zero
profile, the space has none.

The texture is an ordinary `Tex1` set with two T4 textures sharing one
bitmap and differing only in CLUT: **texture 0** gives white glyphs with a
dark anti-aliased outline (opaque grey ramp), **texture 1** white glyphs on
an alpha ramp without outline. Atlases are 128 texels wide (sw_16: 128×649
used of 128×1024).

### Font specs

Text styles name fonts as `name[#V][*X[,Y]]`, e.g. `sw_16#1*90`
(as the game reads them):

- `#V`: one digit, the texture (palette variant) index. A character below
  `'0'` gives 0, so `#!1` (seen in some .pmb specs) selects variant 0 and the
  `1` is skipped.
- `*X[,Y]`: scale in percent. X multiplies the horizontal scale, Y the
  vertical scale; **without `,Y` the same value is used for both axes**. Both
  are multiplied into the current scale, which feeds the glyph-width /
  advance scale and the glyph-height / line scale respectively.
- Unknown font names fall back to the default font (`sw_12`).

### Layout (as the game does it)

State: pen x, baseline y, x/y scale `sx, sy`, extra glyph spacing and extra
line spacing (both 0 by default), and the previous Latin glyph (none at the
start, after a new line, after a kanji and after an out-of-range code).

For a Latin glyph `g` after `p`:
```
kern = min over rows r of clamp(g.left_profile[r] + p.right_profile[r]) & 0x7F
x0   = pen + (g.left - kern) * sx         y0 = baseline + g.top * sy
quad = (x0, y0, g.width * sx, g.height * sy), texels (atlas x, atlas y, width, height)
pen  = x0 + g.advance * sx + spacing
```
Each row's sum saturates, and rows past the count count as 0x7F. Without a previous
glyph kern is 0. When either glyph has no profile (the space) the
original's result is undefined; **we treat that as kern 0**.

For a kanji (EUC-JP pair): `x0 = pen + kanji_x * sx`, size
`kanji_w * sx × kanji_h * sy`, `y0 = baseline + kanji_y * sy`,
`pen = x0 + kanji_w * sx + spacing`. The 22×22 kanji cell is stretched to
the Latin font's kanji size (16×16 for sw_12, 28×28 for sw_20).

`\n` sets pen back to the start x and adds `line_height * sy + line
spacing` to the baseline; `\r` only resets x. A code outside the font's
range draws nothing, does not advance, and restarts kerning.

Text measurement returns the widest line and `line_height * sy + newlines
* (line_height * sy + line spacing)`. Alignment flags shift the whole block:
horizontal right = x − width, centre = x − width / 2; vertical top =
baseline at y + ascent * sy, bottom = top − height, middle = top − height / 2;
with no vertical flag y is the baseline. In half-height (interlaced field)
mode the game halves sy.

## Kanji fonts

### KNJI (`font/system.kf`)

```
0x00 "KNJI"
0x10 u16 block count (1), u16 palette count (2)
0x14 u32 offset of the block offset table (u32[count])
0x18 u32 palette offset: count × 16 RGBA entries, GS alpha (0x80 = opaque)
block (0x18 bytes):
  +0x00 u16 first JIS code (0x2121), u16 last (0x7F7E)
  +0x04 u32 glyph data offset (0xBDB0)
  +0x08 u32 glyph stride (0x100)
  +0x0C u16 cell width (22), u16 cell height (22)
  +0x10 u32 index table offset: u16 glyph index per code first..last
  +0x14 u32 name offset ("KANJI22")
```
Cells are 4bpp, row-major, low nibble first, 11 bytes per row (242 of the
256-byte stride). Palette 0 is the outlined look (index 1 brightest, high
indices dark), palette 1 white on an alpha ramp with the outline indices
transparent, matching the two Latin texture variants. Codes that JIS X 0208
leaves undefined (1,501 of them) all point to glyph 108, a "missing"
placeholder; row 0x7F and 0x2121 point to glyph 0 (blank). 7,335 JIS codes
have real glyphs (rows 0x21..0x2F incl. NEC row 13, levels 1 and 2 kanji).

#### `font/system.hf` and `font/system.tf` (Gran Turismo Concept)

Both are `KNJI` files with the layout above and `KanjiFont::parse` reads them unchanged: one
block, codes 0x2121..0x7D7E, 22 x 22 cells, two palettes, name `HANGLE22` in both.

| File | Glyphs | Indexed by | Contents |
|---|---|---|---|
| `system.hf` | 8,055 to 8,206 | KS X 1001 code | symbols, Latin, kana, Cyrillic, the 2,350 hangul from 0x3021, hanja |
| `system.tf` | 8,240 / 8,249 | Big5 renumbered (MLTX "More pools") | Big5's symbols, then its characters in Big5 (stroke count) order from code 0x2622 (Big5 A440) |

Codes without a character share one glyph (index 11 in `system.hf`, 147 in `system.tf`).
`kanji_font_path(language)` names the file a language's double-byte text draws with
(KR `system.hf`, TW `system.tf`, everything else `system.kf`); `GameFonts::load` and
`gt3tool menu-shot` (by the page's `menu/<LANG>/` folder) use it, so a `menu/KR/` or `menu/TW/`
page renders with its own glyphs. `gt3tool text-render <font> key:<KEY> out.png [scale] --lang=KR`
draws a string table value in that language; `gt3tool text` also writes both fonts' atlases
with their code lists. Which file the program uses for CN was not determined (`system.tf` is
assumed; the CN pages on the discs are Korean).

### KFT0 (`font/kanji22.kf`, unused by the game)

```
0x00 "KFT0"
0x04 u16 glyph stride (0x100)
0x08 u16 index[94 * 94] by (row - 0x21) * 94 + (col - 0x21), 0xFFFF = none
0x4510 glyph data (7,482 cells of 22×22, same pixel layout as KNJI)
```
It has no palette; we draw it white on a linear alpha ramp.

## Not decoded

- `font/font.dat` (`FT00`): u32 count 5 followed by six offsets; contains
  a `UTex` texture header and 16-byte glyph-like records. Not used by
  the game.
- `font/fonttest.dat` (`FT01` with a different header: 0x04 = 0x280,
  0x08 = 0x100, name "mokeke"). Not referenced.
- The built-in default strings that empty MLTX values fall back to live in
  the game's executable file, not in any VOL file.
