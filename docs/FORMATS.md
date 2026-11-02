# File formats

Everything here was worked out from a retail disc (`PBPX-955.03`, v1.10).
All values are little endian. ✅ means implemented, 🔎 means partially
understood, ❓ means not yet investigated.

## Disc layout ✅

The disc is ISO9660 + UDF bridge, 2048-byte sectors.

| File | Purpose |
|---|---|
| `SYSTEM.CNF` | `BOOT2 = cdrom0:\PBPX_955.03;1`, `VER`, `VMODE` |
| `PBPX_955.03` | Boot ELF, about 270 KB. Loads IOP modules, then loads `CORE.GT3` |
| `CORE.GT3` | The game program (compressed), see below |
| `*.IRX`, `IOPRP224.IMG` | IOP modules: pad, memory card, sound (`LIBSD`), USB/i.LINK, plus Polyphony's `PDI*` modules (GT Force wheel and i.LINK multiplayer) |
| `GT3.VOL` | Asset archive, about 3.9 GB on disc and 6 GB unpacked |

Other games on the engine use the same layout. A dual-layer disc (the Asian
GT Concept 2002 Tokyo-Geneva, `SCPS_559.02` and `SCPS_559.03`) has a file system
per layer: the second's primary volume descriptor is the sector where the
first's volume ends, and the files are spread over both layers. Its assets are
in `GTC0.VOL` (layer 0: music and movies) and `GTC1.VOL` (layer 1: the rest);
the engine reads every `*.VOL` with the archive's magic as one set (`GT3.VOL`
or the one with the largest index first; a path two have is the first's).

## CORE.GT3 ✅ (`src/disc/core_image.*`)

```
u16  0x0101
u32  unpacked size
...  raw DEFLATE stream
```
Unpacked:
```
0x000  256 bytes   signature / key block (high entropy)
0x104  u32 1        segment count
0x108  u32          entry point     (0x00100008)
0x10C  u32          load address    (0x00100000)
0x110  u32          image size      (= unpacked size - 0x114)
0x114  ...          program image
```
Each segment is `u32 load address, u32 size`, then its bytes. Gran Turismo 3
has one; GT Concept's programs have three.
The engine **never executes this code**. It reads it to tell the release (the
car database it loads, and the code of the routines whose rules differ between
releases), to check its built-in tables against the disc's, and to look up the
time-trial record names; `gt3tool info` prints the header values.

## GT3.VOL ✅ (`src/disc/volume.*`)

```
0x00 u32 magic 0xACB990AD
0x04 u16 2, u16 2
0x08 u32 toc_size            header + node tree + name pool
0x0C u32 name_pool_offset
0x10 u32 ?                   (0x37D6 on PBPX-955.03)
0x14 node tree, root directory first
```
Each node starts with `u32 name_offset | flags << 24`. Names live in the pool
as **bitwise-inverted** bytes, terminated by `0xFF` (an inverted NUL).

| flags | node layout |
|---|---|
| `& 1` directory | `u32 count; u32 parent; u32 child_offsets[count-1]` |
| `& 2` gzip file | `u32 sector; u32 size; u32 packed_size` (RFC 1952 gzip member) |
| `0` plain file | `u32 sector; u32 size` |

Sectors are 2048 bytes, relative to the start of `GT3.VOL`. There are 5,333
files, 1,705 of them compressed. Top-level folders: `menu` (1,907), `sound`
(1,216), `cars` (863), `wheel` (464), `courses` (321), `race`, `demo`,
`logger`, `bgm`, `stream`, `tire`, `database`, `font`, `replay`, `music`,
`gameconf`, `advertise`, `gadget`, `cameras`, `text`.

## Tex1 texture sets ✅ (`src/formats/tex1.*`, `gs_memory.*`)

Found as `.img` files and embedded (16-byte aligned) inside car, course and
menu containers.

```
0x00 "Tex1"
0x0C u32 file size
0x10 u16 base block (0 on disc), u16 GS blocks the set occupies
0x14 u16 texture count, u16 buffer count
0x18 u32 texture table offset   -> 0x28-byte records
0x1C u32 buffer table offset    -> 12-byte records
0x20 u32 paint table offset     (cars; 0 = none; docs/formats/car_model.md "Paint")
0x24 u32 CLUT animation offset  (0 = none; below)
```
Texture record: GS registers `TEX0` (+0x00), `TEX1` (+0x08), two
unidentified u64s (+0x10, +0x18; probably MIPTBP), `CLAMP` (+0x20).

Buffer record: `u32 data_offset; u16 dbp; u8 dbw; u8 dpsm; u16 w; u16 h`.
The data is linear transfer data, exactly as sent with a GIF IMAGE transfer.

**Important:** buffers are usually uploaded as PSMCT32 with `dbw = 1` (a raw
copy of GS pages), while the textures sample the same memory as T8 or T4. So
the only correct way to decode them is to replay the uploads into a model of
GS memory and read back with the real swizzle. `GsMemory` does this for
CT32/24/16, T8, T4 and the T8H/T4HL/T4HH alpha-channel formats, with CSM1
palettes. `REGION_CLAMP` in `CLAMP` gives the meaningful sub-rectangle (for
example 640×448 inside a 1024×512 texture).

Car models contain several textures that share pixel data but use different
`CBP` palettes. This is how paint colours are implemented (the paint table, +0x20).

4-bit textures pick one of 16 palettes inside the 16×16 CLUT rect with
`TEX0.CSA` (entry = CSA·16 + index, CSM1 layout); car paint colours are
selected this way.

Alpha is GS-style (0x80 = opaque) and is expanded to 0..255. The game's
`TEXA` setup for 16-bit textures is not yet known.

**CLUT animation** (header +0x24; offsets from the Tex1 start): `u32 count` (low 16
bits), `u32 entry table`. Each 0x20-byte entry rewrites `words` CT32 colours of the
set's upload data at `target` (always inside a small CT32 CLUT buffer that only CLUTs
read) from a list of timed palettes:

```
+0x00 u16 group     0: runs from the set's load; 1: the race start's signal lights
+0x02 u16 mode      0 loop, 1 ping-pong, 2 once (stays on the last frame)
+0x04 u16 words     colours per frame (16, or 256 for 8-bit textures)
+0x06 u16 frames
+0x08 u32 target    offset of the colours it rewrites
+0x0C u32 frame table -> frames x {f32 seconds, u32 offset of `words` colours}
+0x10..+0x1F        run-time state (0 on disc)
```

How the game plays them (`src/formats/clut_animation.*`): on load every entry is on
frame 0, fading to frame 1 with weight 1 (so the textures show frame 0's colours, not
the stored ones). Each frame (1/60 s, not while paused) the time in the current frame
grows; past the frame's |seconds| the entry steps to the next frame (by its mode), as
often as needed. The colours written are, per byte, `cur x w + next x (4096 - w) >> 12`
with `w` = (1 - time / |seconds|) x 4096 truncated: a linear cross-fade from each frame
to the next over the frame's time. A frame with negative seconds holds the entry when it
is reached (or at load, when it is frame 0) until it is released; a release ahead of
time lets it pass the next hold. The race start (every start type) advances group 1 by
100 s (to its hold), releases it, rewinds it by the countdown's seconds (0 without one;
rewinding steps back through the frames, each fading to the one it came from) and
releases it again, so group 1 reaches its hold as the countdown ends and plays on.

64 of the 80 US course files have them (1,249 entries, in the scenery, slot 5,
wet-road reflection, backdrop and billboard sets; none in cars, wheels or `race/`):
loops of 16 or 256 colours (group 0) and 660 start-light entries (group 1, mode 2).
Route 11's grandstand glow cards are camera flashes: palettes fully transparent
(alpha 0) for 0.9-1.5 s, then a glow for ~0.02 s and a magenta flash for ~0.02 s.

## ADS music ✅ (`src/formats/ads.*`, `ps_adpcm.*`)

```
"SShd" u32 0x18  u32 codec (0x10 = PS-ADPCM, 0x01 = PCM16)  u32 rate
       u32 channels  u32 interleave (0x400)  u32 loop_start  u32 loop_end
"SSbd" u32 body_size  body...
```
Channels are interleaved every `interleave` bytes. PS-ADPCM frames are 16
bytes, 28 samples. The decoder uses the hardware's `(…+32)>>6` rounding.
FFmpeg truncates instead; the stream layout was verified bit-exact against
FFmpeg when using its arithmetic.

## bgm/ads.inf ("MADS") ✅ (`src/formats/sound/mads.*`, `bgm_index.*`)

Header: "MADS", u32 1, u32 0 (relocation base), u32 record count, then 17 groups
{u32 record offset, u32 record count} (the BGM ids 0x12..0x22; `src/formats/sound/mads.*`).
Track records are 5 × u32: offsets of name, file, title, artist strings, then
the offset of the record's **info block** (music §8) ✅:

```
+0x00 u32 relocation base (0 in the file)   +0x04 u32 sample rate (44100)
+0x08 u32 length in samples (per channel)
+0x0C u32 count[32]    markers per track
+0x8C u32 offset[32]   each track's array, from the block's start
arrays: u32 sample positions, ascending
```

On the US disc only the 25 race songs (BGM id 0x14: the race playlist's songs, music §2.3)
and `demo_a01` have markers, in tracks 1..4: 1 every beat, 2 the phrase starts (every 4 or 8
beats), 3 and 4 the same hand-placed cut points (they time the camera cuts, presentation §G14:
view mode 1 cuts on track 3, or on track 4 with the music-timed cuts on; race_a01 track 3: 205
markers from 1791, 162047, 322560, ...). The jingle and menu groups have none. (The older
`bgm_index.*` scanner finds the records by their ".ads" file names.)

## PS2 memory-card saves ✅ (`src/formats/ps2/memcard.*`, `src/game/gt_save.*`)

The game save itself is the data file `BASCUS-97102GAMEDATA/BASCUS-97102GAMEDATA`: a
0x40-byte header {u32 header size 0x40, u32 payload size, u16 minor 1, u16 major 7,
u32 CRC-32, name "Base"}, then the payload with every byte inverted (XOR 0xFF); the
CRC is the zlib CRC-32 of the stored (inverted) payload and the file is padded to 64
bytes. This file alone is the "raw" save PS2 save editors (GT3SaveEditor) read; GT3
Revisited imports it under any name and exports it as `.raw`. What the payload holds and how GT Mode maps it is in
[dev/GT_MODE.md](dev/GT_MODE.md#saves) ("PS2 import/export"). Its containers: `.psu` (512-byte directory
entries: the save folder, ".", "..", then each file entry and its data padded to 1 KB)
and raw 8 MB card images (`.ps2` / `.mcd`: 512-byte pages with or without a 16-byte
spare area holding 3 Hamming-code bytes per 128 bytes; superblock "Sony PS2 Memory Card
Format 1.2.0.0", 1 KB clusters, two-level FAT from cluster 8, allocatable clusters from
41, 512-byte directory entries {mode, length, times in Japan time, first cluster,
parent entry, name}). Read and written; compressed `.max` / `.cbs` are not supported.

## Demo runs ✅ (`src/race/demo_replay.*`)

`demo/<event>` (the licence tests' and arcade time trials' demonstrations) are the game's
replay saves, in the same 0x40-byte envelope as the game save (CRC-32, inverted body): a
"GT2K" replay header with the car record, then the recorded controller input, re-simulated
on playback. Layout in `src/race/demo_replay.h`; [dev/RACE.md](dev/RACE.md) "Demo runs".

## Detailed format documents

Each subsystem documents its formats in `docs/formats/`:

- [text.md](formats/text.md): ✅ `MLTX` string tables (7 languages), ✅ text encoding
  (ASCII / ISO-8859-N pages / EUC-JP via `font/jis2uni.dat`), ✅ `.fnt` fonts and `.kf` kanji
  fonts with the game's kerning; ❓ `FT00`/`FT01` (`font.dat`, unreferenced)
- [course.md](formats/course.md): ✅ course files 320/320 (section table, `GTM1` scenery and
  its animations, `GTRW` road surface/walls/checkpoints, AI lines and driver parameter sets,
  the licence course line), ✅ `Vls0` visibility lists, `GTFX` lamps, `BLBd` billboards,
  `Env0` (most fields), ✅ `LGM0` data-logger maps; `GTRC` replay camera scripts are in
  [dev/RACE.md](dev/RACE.md) "Replay (TV) cameras"
- [car_model.md](formats/car_model.md): ✅ car containers (`GTCI`/`GTM1`/`GTTR`/`GTTW`),
  ✅ `GTM1` 3D models (node programs, VIF packets, LODs, paint variants; shared with course
  scenery), ✅ `GTTW` wheels; 2073/2073 model files decode, with the per-paint material sets
  and the callback morphs (mud flaps, aerials, steering). ❓ the rest of the chunk flag high
  byte, day vs eve/night geometry
- [database.md](formats/database.md): ✅ `GTAR`/`GTDT`/`STDB`/`IDDB` containers and the
  64-bit id hash, ✅ all 36 paramdb tables (cars, engines, drivetrains, gearboxes, tires,
  parts, 454 events), `racemode.db` / `racedetail.db` (the race display's layout per mode);
  also identifies `GTTR` (tire textures), `PRZ0` (trophy models, see prm.md) and `.gcf`
  (arcade class lists); ❓ `carcolor.db` colour records, `.cdt`
- [pss.md](formats/pss.md): ✅ PSS movies 51/51 (MPEG-2 program stream, MPEG-2 video,
  PS-ADPCM `.ads` in private stream 1, field-split frames), ✅ `stream/course.ipic`
  (`ipua`: 42 IPU course preview stills)
- [sound.md](formats/sound.md): ✅ `ENGN` engine banks (1209/1209), ✅ `INST` instrument
  banks (7/7) and `se.inf` effect index, ✅ `MSEQ`/`SEQG` sequenced music (9/9; sequencer
  behaviour in [behaviour/sound.md](behaviour/sound.md) §4), ✅ `CRSS` course ambience.
  Note: game pitch 0x1000 = 44.1 kHz.
- [prm.md](formats/prm.md): ✅ `PRZ0`/`PRM0` prize scenes 98/98 (race trophies and medals,
  license cards, championship trophies: GTM1 model with in-file sphere maps, camera and
  model key-frame tracks, lights, planar shadows); ❓ two menu display parameters
- [menu.md](formats/menu.md): ✅ `.pmb` menu pages (960/963; the 3 failures are
  broken Italian fragments), ✅ `.mbl` menu flow (51/51), ✅ `IMGS` image sets (23/23)

Note: `menu/cars/ho0100` is a plain entry whose contents are a gzip of an
empty file. All 1,705 entries flagged as compressed inflate to their recorded
sizes.

## Identified but not yet decoded ❓

| Magic / extension | Where | Notes |
|---|---|---|
| `GTBR` | `race/brake.bin` | the brake parts drawn inside race cars' wheels: rotor and caliper images, their tables (docs/formats/car_model.md "Brake parts") |
| `.cdt` | 30 files, gzip | ? |
