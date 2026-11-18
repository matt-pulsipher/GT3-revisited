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
`gt3tool tex` commands, and check the asset browser. Then add its serial and the
`Files` fingerprint `gt3tool info` prints to `kKnownDiscs` in
`src/disc/disc.cpp` (a version players report working goes in with
`tested = false` and no fingerprint: it opens without the warning, and
`gt3tool info` says "reported working").

## The disc library

Every disc image you open joins the disc library (Settings > Disc & saves, or
Discs... in the main menu; up to 32), which remembers what each one is: its
release, region and version. Picking another disc there goes straight to its
title menu.

Each disc is recognised by a fingerprint of its files: its program
(`CORE.GT3`) and the list of files in its game volume, with their places and
sizes. A disc with a release's serial and version whose fingerprint isn't that
release's is a modified copy: the library and Settings say "modified". It
counts as that release for saves, replays and mod choices.

Saves, replays and mod choices go by disc key:

| Disc | Key | Saves | Replays |
|---|---|---|---|
| NTSC-U (1.00 and 1.10) | `us` | `saves/` | `replays/` |
| PAL | `eu` | `saves/eu/` | `replays/eu/` |
| NTSC-J | `jp` | `saves/jp/` | `replays/jp/` |
| any other serial, e.g. a modified disc under a serial of its own (`SCEF_502.94`) or GT Concept | the serial as PCSX2 writes it (`SCEF-50294`) | `saves/SCEF-50294/` | `replays/SCEF-50294/` |

Earlier versions kept every disc's saves and replays in its region's folders,
so the first time a disc with a key of its own opens, its folders start with
copies of its region's (once; the originals stay).

The race physics is Gran Turismo 3's own, with its values built in. It
recognizes each release by the code of the disc's program, so another build of
the game gets its own release's rules as long as that code matches one of the
releases above; a part that is unlike every release uses the rules of the
release it is nearest, and Settings > Disc & saves > Race physics says so
(details in the log).

A modified copy of the game opens like any other disc. Where its program
changes Gran Turismo 3's physics values, its races use them (Settings shows
"Gran Turismo 3 (with this disc's changes)"); online races and the game's
demos keep Gran Turismo 3's own values. Changes a mod makes with emulator
cheat files are not applied: they patch the running game's memory, which GT3
Revisited does not run.

Other games on the same engine, such as GT Concept, open too. Gran Turismo
Concept 2001 Tokyo, 2002 Tokyo-Seoul and 2002 Tokyo-Geneva (both its European
and its Asian release) race on their own physics. A replay saved while a disc
raced on Gran Turismo 3's physics doesn't play once that disc races on its own,
and replays and ghosts saved on a Concept disc before the pod race's stops and
the free run's lap recordings were matched to the PS2 don't play either.

Gran Turismo Concept discs (2001 Tokyo, 2002 Tokyo-Seoul, 2002 Tokyo-Geneva and
its Asian release) race with their own cars, courses and events: the game reads
the car database and game configuration that the disc's own program names
(for example `database/paramdb_bs.db` and `gameconf/concept.gcf` on 2001
Tokyo), not the Gran Turismo 3 files those discs also carry. Concept has no GT
Mode. A few events on the Tokyo-Geneva discs name an opponent their database
doesn't hold; those races run without that car.

A Concept disc starts in Concept's own menus, in its own language: Japanese
(2001 Tokyo), Korean (Tokyo-Seoul, both versions), the language chosen on the
language page of Tokyo-Geneva (English, French, German, Italian, Spanish or
Portuguese; its Asian release: Traditional Chinese or English). What works
there:

- The title page with Load Game, Save Game, File Operation, Replay Theater,
  Options and the credits, and the main menu. Left alone, the title plays the
  release's demo races and movies.
- Course License: the ten tests with their medals, target times and test
  cars, in Concept's own license menu (the course map and tips, the preview
  box, Records, Save Replay), with or without the pace car. A run with a
  medal asks for a name and goes into the test's Records page.
- Single Race at Normal and Professional, and at Ace once L1 and R1 are held
  on the level page: a course opens once its license test has a medal; the
  category and car pages offer the cars that are open. The car page asks for
  the transmission and the driving aids (not on a dirt course); it has no
  tire choice, as on the PS2. The race opens and ends in Concept's own
  Single Race menu; a win shows the cup and, when it opens a prize, the "You
  will receive a prize after this session." notice; the prize cars and, in
  time, the two movies are shown on the way back to the menus.
- Vehicle Settings in the Single Race and Free Run menus: Normal or Custom,
  and on Custom the ride height, springs, dampers, brake balance, downforce,
  final gear and tires. Triangle leaves the menu, as Return does. A car keeps
  its settings.
- Free Run and 2P Battle on every course (in 2P Battle each player chooses
  their own driving aids).
- i.LINK Battle opens the online lobby. A Concept disc races online against
  players on the same disc, on its own physics. CPU cars race at Normal,
  Professional or Ace, each car as it drives in its own Single Race at that
  level.
- Quick race offers the license tests, Single Race on road and dirt courses
  (at Normal, Professional or Ace, your car's own race when it has one), the
  pod race, the arcade time trials and the custom race. The custom race's
  opponents race at Normal, Professional or Ace, as the online CPU cars do.
- Game Status: the map with each course's license medal and cups, the prize
  cars won so far, the completion figure, and the show room with the car and
  its specification table. The directions move over the map.
- The credit transfer (Game Status, once every course is won): it lists the
  Gran Turismo 3 Revisited saves it can go to, asks "OK to transfer money and
  licenses?", and only then adds the credits, the license cards and a bronze
  on every license test without a medal. A save takes it once. The European
  Tokyo-Geneva disc adds 10,000,000 Cr., the other Concept discs
  1,000,000,000 Cr. Any Gran Turismo 3 release's save can take it; the list
  starts with the release each disc transfers to on the PS2 (the European
  (PAL) Gran Turismo 3 for the European Tokyo-Geneva disc, the Japanese
  Gran Turismo 3 for the others).
- The pod race (Game Status, once all ten licenses have a medal), for one or
  two players: three laps, six pods. Every pod must stop once a lap in the
  stop area: pull up on it and hold the handbrake until the pod sits down.
  On the area the race tells you to stop and pull the handbrake, past it to
  go back to the stop area; a pod that has missed two stops is disqualified
  at the line, as on the PS2.
- Progress (license medals and records, race results, prizes, each car's
  settings) is saved in the game's own save folder, apart from Gran Turismo
  3's saves; the save page shows the game's completion figure.
- Saves from and to the PS2: "FROM A FILE" on the Load Game and File Operation
  pages imports a Concept memory-card save (a `.psu`, a PCSX2 `.ps2` card
  image, a folder memory card or the data file), and "Export to PS2" writes
  one the original game loads. As on the PS2, each release reads its own
  save: `BESCES-50858GAMEDATA` (Tokyo-Geneva, Europe), `BISCPS-55903GAMEDATA`
  (Tokyo-Geneva, Asia), `BKSCPS-56005GAMEDATA` (Tokyo-Seoul),
  `BISCPS-15010GAMEDATA` (2001 Tokyo). Medals, record times and names, race
  results, prizes, car settings and the options travel both ways.

A disc image that is shorter than the disc says it should be (a dump that
stopped early) opens with the message "This disc image is incomplete (2.47 of
3.67 GB). Make the image again from your disc." on the start screen and under
Settings > Disc & saves.

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
  page); Settings > Disc & saves changes it later. The choice is kept per region in
  `config.ini` (`[gt3menu] lang_eu`). NTSC-U and NTSC-J discs run in their own
  language only, as the originals do.
- Each region keeps its own GT Mode saves (`saves/` for NTSC-U, `saves/jp/`,
  `saves/eu/`; a disc with a serial of its own keeps its own too: [The disc
  library](#the-disc-library)), and PS2 saves import and export under the release's own memory
  card name (`BASCUS-97102`, `BESCES-50294`, `BISCPS-15009`; a Gran Turismo Concept disc's
  own, listed above). Versions up to
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

Use `.iso` images (2048-byte sectors) or `.chd` images made from the ISO with `chdman createdvd`. `chdman createcd` images also work when the first track is data (Mode 1 or Mode 2, raw or 2048-byte sectors); only that track is read. Direct `.bin/.cue` support is planned.
