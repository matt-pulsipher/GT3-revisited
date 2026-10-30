# Disc compatibility

Any image that contains `CORE.GT3` and a valid `GT3.VOL` is accepted (another
game on the same engine: its `*.VOL` files, read as one set). Dual-layer images
open too, with their files on either layer.
Images not in this list run with a warning until someone verifies them.

| Serial (BOOT2) | Version | Region / notes | Status |
|---|---|---|---|
| `PBPX_955.03` | 1.10 | NTSC-U/C (menus for BR/FR/GR/IT/JP/SP/US present on disc) | ✅ verified |
| `SCUS_971.02` | 1.00 | NTSC-U retail. Same game as 1.10: CORE.GT3 and GT3.VOL are byte-identical; only the boot files' product code (SYSTEM.CNF, boot ELF string) and the disc headers differ | ✅ verified |
| `SCES_502.94` | 1.00 | PAL (English, French, German, Italian, Spanish) | ✅ verified |
| `SCES_502.94` | 2.00 | PAL (Europe, Australia; English, French, German, Italian, Spanish) | ✅ reported working |
| `SCPS_150.09` | 1.01 | NTSC-J | ✅ verified |

To verify a new version, run `gt3tool info`, `gt3tool bgm` and a few
`gt3tool tex` commands, and check the asset browser. Then add its serial to
`kKnownDiscs` in `src/disc/disc.cpp` (a version players report working goes in
with `tested = false`: it opens without the warning, and `gt3tool info` says
"reported working").

The race physics is Gran Turismo 3's own, with its values built in. It
recognizes each release by the code of the disc's program, so another build of
the game gets its own release's rules as long as that code matches one of the
releases above; a part that is unlike every release uses the US rules, and
Settings > Game disc > Race physics says so (details in the log).

Other games on the same engine, such as GT Concept, open too. Their races will
use Gran Turismo 3's physics (Settings > Game disc shows "Race physics: Gran
Turismo 3 (this disc's own physics isn't supported yet)"); their own physics
is planned. Their car database isn't supported yet either, so most of them
can't race yet: the game says "This disc's car database isn't supported yet".
The European GT Concept 2002 Tokyo-Geneva disc also carries a car database in
Gran Turismo 3's format, which the game reads, so it races.

A disc image that is shorter than the disc says it should be (a dump that
stopped early) opens with the message "This disc image is incomplete (2.47 of
3.67 GB). Make the image again from your disc." on the start screen and under
Settings > Game disc.

## Regions

The engine tells the release from the car database the disc's program loads
(`disc/region.h`; the boot file name if the program cannot be read) and reads
the same files that release does:

| Release | Database | Game config | Courses | Languages |
|---|---|---|---|---|
| NTSC-J | `database/paramdb.db` | `gameconf/product.gcf` | `courses/<label>` | Japanese |
| NTSC-U | `database/paramdb_us.db` | `gameconf/product.gcf` | `courses/US/` | English (US) |
| PAL | `database/paramdb_eu.db` | `gameconf/product_eu.gcf` | `courses/UK/` (English), `courses/EU/` (others) | English (UK), French, German, Italian, Spanish |

The game's language decides the rest, as in the original releases:

| Language | Menus / race HUD | Speed, distance | Power | Torque |
|---|---|---|---|---|
| Japanese | `JP` | km/h, km | PS | kgf·m |
| English (US) | `US` | mph, miles | HP | ft·lb |
| English (UK) | `BR` | km/h, km | BHP | kgf·m |
| French | `FR` | km/h, km | CV | kgf·m |
| German | `GR` | km/h, km | PS | kgf·m |
| Italian | `IT` | km/h, km | BHP | kgf·m |
| Spanish | `SP` | km/h, km | BHP | kgf·m |

- HP, BHP and CV figures are PS × 0.98619. Weights are kg and sizes mm everywhere.
- Settings > Display > Speed units (`[race] units`) can pick km/h or mph
  instead, in races and menus (distances follow); the default is the
  language's.
- In Japanese every price, prize and game-config sum is × 100 (a new GT Mode
  game starts with 1,500,000 Cr.) and record dates read YY/MM/DD.
- A PAL disc asks for the language at the first start (the disc's own language
  page); Settings > Game disc changes it later. The choice is kept per region in
  `config.ini` (`[gt3menu] lang_eu`). NTSC-U and NTSC-J discs run in their own
  language only, as the originals do.
- Each region keeps its own GT Mode saves (`saves/` for NTSC-U, `saves/jp/`,
  `saves/eu/`), and PS2 saves import and export under the release's own memory
  card name (`BASCUS-97102`, `BESCES-50294`, `BISCPS-15009`). Versions up to
  0.3 kept every disc's games in `saves/`; the first time a PAL disc opens GT
  Mode, its folder starts with copies of them (the originals stay).

## Physics on European and Japanese discs

Races run at 60 Hz on every disc, each with the rules of that disc's own release.

- The European release ran its races at 50 Hz. On a European disc, timing, lap
  times and handling follow the 60 Hz releases, while the European release's own
  rule changes still apply (for example, a lap that only ties the record is not
  a new record).
- License tests and arcade time-attack targets use the US medal times on every
  disc. The European release changed 39 of them to suit its 50 Hz timing; since
  races run at 60 Hz here, the US times apply (a European disc includes them).
- The Japanese release is an earlier version with rules of its own (its
  collisions among them); a Japanese disc uses them.
- The demonstrations and license demo runs shown on a European disc are the US
  recordings included on that disc, not the European release's own 50 Hz runs.
- Online races always use the US rules, whatever disc each player has.

Only `.iso` images (2048-byte sectors) are supported for now; `.bin/.cue` (2352-byte) and CHD support are planned.
