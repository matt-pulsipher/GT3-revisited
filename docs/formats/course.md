# Courses

Everything here comes from the retail disc (`PBPX-955.03`, v1.10). Values are
little endian. Code: `src/formats/course/` (container, road, AI lines, model
assembly), `src/formats/model/gtm1.*` (GTM1 decoder shared with cars, owned
by the car-models workstream), `tools/gt3tool/cmd_course.cpp`,
`src/app/screen_courses.cpp` (`gt3r --screen courses`), `tests/test_course.cpp`.

Status: container ✅, scenery (GTM1) ✅, road surface/walls/checkpoints/grid
(GTRW) ✅ (a few fields still unknown), AI lines ✅ (straight/arc chain; some
per-point flag bits unknown), licence course line (slot 27) ✅, logger maps 🔎, visibility lists, replay
cameras, GTFX/BLBd/Env0 ✅ (most fields; see below), AI driver parameter sets ✅ (CRSS: see
[sound.md](sound.md)).

Coverage: `gt3tool course-survey` decodes **320/320** course files (container,
road, AI lines, every GTM1 including textures). `test_course` checks all 80
US files.

## Files

| Path | Contents |
|---|---|
| `courses/<US,JP,UK,EU>/<name>` (gzip, 4–14 MB) | One file per course layout. 80 per region. `r_` = reverse layout, `L_` = license-test variants. 58 of the 80 names are byte-identical in every region. The other 22 differ by a few KB, inside the scenery (for example 1,812 bytes in `trialmt` US vs UK, probably regional billboards). |
| `courses/.list` | Plain-text list of region folder and course names. |
| `logger/<name>` (gzip) | `LGM0`: the data-logger course map. It is **not** an AI line (see below). |
| `cameras/*.cam` | `GTRC`: camera tracks for attach and start shots (the same format as the replay camera section). |
| `replay/*.dat`, `demo/*` | Replay data (survey only) and the demonstration runs (below). |

The game knows the folders `courses/US`, `courses/JP`, `courses/UK` and
`courses/EU` (names stored in the disc's CORE.GT3) and picks the region folder
by the game's region.

## Coordinate frames

Scenery (GTM1), road (GTRW) and checkpoints share one frame: metres,
**right-handed, Y up**. Billboard text reads correctly with a right-handed
camera.

The AI lines store 20.12 fixed-point X/Z in a frame whose **X is negated**:
`world_x = -x / 4096`, `world_z = z / 4096`. With that mapping, every AI
control point on all 80 US courses lies on the road mesh: the per-line
median distance is ≤ 5.7 m, and the worst single point is 16 m away.

## Container

A table of 48 u32 section offsets (0 means absent). A section ends at the next
present offset. Slot 0 is always 0, and slot 1 always starts at 0xC0.

| Slot | Tag | Present | Contents |
|---|---|---|---|
| 1 | `GTM1` | 320 | Main scenery model |
| 2–4 | `Vls0` | 320 | Visibility lists for slot 1, see "Vls0" below. The race's main view uses slot 2 (also for the GTFX lamp lists); slot 3 holds smaller lists, slot 4 omits some nodes (e.g. Route 11's stand lights, nodes 1213–1220) |
| 5 | `GTM1` | 320 | Second scenery model: ~100 nodes covering the same area, drawn into the cars' reflection map (with slot 23), not in the main view |
| 6, 7 | `Vls0` | 320, 112 | Visibility for slot 5 |
| 9, 13 | `GTM1` | 36, 40 | Wet-road reflection: slot 9 the scenery pre-mirrored below the road (drawn into a small buffer), slot 13 its alpha-only mask (`Vls0` in 10 and 14). US: route5 / route11 families, the L_circle tests; see docs/dev/RACE.md "Wet-road reflection" |
| 21 | `GTM1` | 320 | Sky dome (centred on the camera, view rotation only, no fog). The only sky the race's main view draws |
| 22 | `GTM1` | 304 | Distant backdrop (mountains, skyline) |
| 23 | `GTM1` | 320 | Small sky dome drawn only into the cars' reflection map (translation removed), before slot 5. In `courses/EU/L_apricot1` its texture set is 0x80 zero bytes: the game loads it as a set without textures and binds none, so the GS keeps an earlier draw's texture. The cars then reflect no sky there (French, German, Italian, Spanish; UK English reads `courses/UK/`, whose dome is textured): we leave the dome out, so the map's black shows |
| 24 | `GTM1` | 304 | Low-resolution sky dome (±50 m; trialmt: one 128×32 T4 texture vs slot 21's 1024×256 T8). Drawn only by the rear-view mirror, never in the main view |
| 25 | – | 28 | Small table starting with a u32 1..7 |
| 26 | `Tex1` | 316 | Extra texture set: the tyre smoke and dust puffs (four 32×64 frames; `src/race/tyre_smoke.*`) |
| 27 | `GTM1` | 52 | Licence course line: the driving line drawn on the road (13 `L_` files per region; see "Course line" below) |
| 28 | `GTRW` | 320 | Road surface, walls, checkpoints, start grid |
| 29 | `GTFX` | 320 | Point-light glows (street / stadium lamps), see below |
| 30 | `BLBd` | 320 | Billboard sprites (trees, spectators) with their Tex1, see below |
| 31 | – | 320 | AI lines |
| 32 | `Env0` | 320 | Environment block: flags, fog, lights, sun, lamp types, colour grade, see below |
| 33 | `GTCM` | 320 | HUD course map: header (`a0 00 55 00` → 160×85?) plus Tex1 |
| 34 | `GTRC` | 320 | Replay camera script (keyframed curves per track-distance range; layout in docs/dev/RACE.md "Replay (TV) cameras") |
| 35 | `CRSS` | 300 | Course ambience emitters (air, city, crowds, sea; gated crowd points). See [sound.md](sound.md) |
| 36 | – | 256 | Unit-cube vertices followed by `GTM1`-like data ❓ |
| 37–40 | – | 316 | AI driver parameter sets, one per driveline class (see "Driver parameter sets" below). The first bytes often read as ASCII (`sdd`), which is a coincidence |

## GTM1 scenery

GTM1 is the same container as car bodies. The header, chunk and VIF layouts
are in the car-models docs (`docs/formats/car_model.md`). Course-relevant
facts follow.

- **Header** (relative to `GTM1`):
  - `+0x10` u16 node count, `+0x12` u16 chunk count, `+0x16` u16 texture-set count, `+0x18` u16 texture-LOD count (2 in courses).
  - `+0x20` node program offsets, `+0x24` chunk offsets, `+0x2C` Tex1 offsets for texture LOD 0.
  - `+0x30` per-node bounding spheres (16 bytes each).
  - `+0x34` table of per-texture-LOD set tables. Courses carry two parallel lists of sets: full-resolution and small. The game picks the set table per node from the node's bounding-sphere distance.
  - `+0x3C` animation list (not textures): `u32 count`, then `count` u32 offsets (from
    `GTM1`) of entries `{u16 type, u16 n, f32 speed (per second), ...}` whose value the game
    advances every frame (1/60 s; reset at load and with every run) and writes into `n`
    targets, operands inside node programs (u32 offsets from `GTM1`), before the programs run.
    US survey: 1,269 entries in 69 files, 428 nodes, slots 1 and 22 only.
    - types 2 / 3 / 4 (11 / 46 / 58; trialmt and route11 / route5 / super and testcourse):
      `f32 start angle`, `f32 angle` (run time), targets at +0x10: angle += speed x seconds,
      wrapped once into 0..360, written as the f32 operand of op 0x14 / 0x15 / 0x16 (rotate
      about X / Y / Z): spinning parts (Route 11's backdrop Ferris wheel, 1 degree a second).
    - type 5 (403; rome, apricot, grandvalley, smtsouth, super): `u16 first, u16 end`,
      `f32 start phase`, `f32 phase` (run time), targets at +0x14: phase += speed x seconds,
      chunk = (int)(phase + 0.5); at or past `end` both go back by end - first; written as the
      u16 operand of op 0x05 (the chunk drawn): 60-frame flip-books (crowds, pit crews) at
      15-30 frames a second.
    - type 7 (751; 52 files): `f32 start phase`, `f32 phase`, targets at +0x10: phase +=
      speed x seconds, wrapped into 0..1, written as the f32 operand of op 0x34. Op 0x34 sets
      the phase (clamped to 0..1) the next op 0x35 `u16 base, u16 count` draws with: chunk
      base + min((int)(phase x count), count - 1), morphed towards its second key frame by the
      fraction, `(1 - w) key 0 + w key 1` (flags and banners).
    - Types 1 (an angle) and 6 are handled by the game but unused on the disc.
  - Texture sets can carry **CLUT animations** (Tex1 +0x24, docs/FORMATS.md "Tex1"):
    flashing and blinking lights, signs, and the start-signal lights (group 1, stepped by
    the race start). They are in the scenery, slot 5, wet-road reflection (slot 9),
    backdrop (slot 22) and billboard (slot 30) sets, both texture LODs. Route 11's scenery
    set 13 records 12-15 (one 16x16 radial glow, four palettes) are the grandstand's camera
    flashes: node 1216 draws them opaque with the default alpha test, and their palettes are
    transparent except for a flash of ~0.02 s every 0.9-1.5 s.
- **Node programs** are byte code, run when a node is drawn. Arguments follow the op byte. Multi-byte values are unaligned.
  - 00: end.
  - 01: no-op.
  - 02 `u8` / 03 `u16`: call node.
  - 04 `u8` / 05 `u16`: draw chunk.
  - 06: `s16`, then `u8 n`, then `s16 rel[n]`. A switch on an engine-supplied selector. If the selector ≥ n, the program skips the table; otherwise it jumps by `rel[sel]`, relative to that field.
  - 07: 16-byte sphere, then `u8 n`, then `s16 rel[n]`. A distance LOD switch: branch `min(lod, n-1)`.
  - 08 `s8` / 09 `s16`: relative jump (from the operand).
  - 0a: `u8 n`, `n×(f32 x,y,z)`, `s16 skip`. Bounding-box cull. If the box is fully outside, the program jumps by `skip` (relative to the skip field). Otherwise it continues and enables clipping only when some point is outside.
  - 0b: re-enable clipping.
  - 0c / 0d: push / pop matrix.
  - 0e `u8`.
  - 0f `16×f32`: load matrix (rows, row-vector convention `v' = x·r0 + y·r1 + z·r2 + r3`).
  - 10 `16×f32`: multiply matrix.
  - 11 `3×f32`: translate (`r3 += x·r0 + y·r1 + z·r2`).
  - 12 `3×f32`: scale rows.
  - 13 `4×f32`: axis-angle rotate.
  - 14 / 15 / 16 `f32`: rotate about X / Y / Z, in degrees.
  - 17 / 18, 1a / 1b, 1d / 1e, 2d / 2e: enable / disable render states 5, 4, 3 and 0xF.
  - 19, 1f, 29, 2a, 2f: `u8`.
  - 1c, 20: `u8 u8`.
  - 21, 24 (FBMSK, below), 32, 36: `u32`. 27, 34: `f32`.
  - 22, 23, 25, 26, 30: no arguments.
  - 28, 33, 37: 16 bytes. 31: 12 bytes.
  - 2b `u8` / 2c `u16`: select texture set.
  - 35: `u16 base`, `u16 count`. Animated chunk: draws `base + frame`.
- **Course specifics.** Course nodes never call other nodes. Running every node once draws every chunk exactly once (trialmt: 600 nodes, 562/562 chunks). Each node begins with `0a` (8-point box), optionally `22`, then binds a texture set and draws chunks.
- **Chunk flags** (header byte 8, then byte 9). Bits 0-4 are the chunk kind, which picks
  how the vertices are coloured (see "Lighting" below):
  - 0: pre-lit. V4-8 vertex colours passed through: `0x80` = 1.0 as a texture's MODULATE factor;
    untextured, the GS shows them as they are (`0x80` = mid grey; see "Course line").
  - 1: lit (normals, no colours) with the material and the lights; courses: diffuse only.
  - 2: environment pass: the packet's texture as a sphere map (normals in the UV slot).
  - 3: environment pass with a panoramic lookup (normals in the UV slot).
  - 4: the road sheen: a second pass over the same vertices with normals instead of colours,
    specular only (below). `CourseModelOptions::reflections` skips it by default.
  - 5: car shadows (not in courses).
  - Bits 5-7: 0x20 two morph keys (position, UV, colour), 0x40 two morph keys (position,
    UV, normal), 0x60 integer positions with 4.12 UVs.
  - Byte 9 bit 0 (0x100): the lit colour is scaled by 255 instead of 128; bit 1 (0x200): the
    alpha likewise. Only read for kinds 1-5.
  - US course counts: kind 1 on most day courses (apricot 360, grand valley 364, super 240),
    kind 4 on every day course with a sheen (seattle 152, midfield 105, laguna 79, ...), none
    on the night routes (route5, route5_dry and reverses).
- **Packet `mat`.** The low byte is texture index + 1 in the bound set (0 = untextured). The high byte holds flags (0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e, 0xfe).
- **Prim.** GIF PRIM is a triangle strip with TME and usually IIP/FGE. ABE is set on alpha-blended packets (0x7C / 0x5C / 0x6C).

## GTRW: road surface

Offsets are relative to `GTRW`. Implemented in `road.h`.

```
0x10 u32 ? (9)   u32 course id (same value in GTRC)   f32 track length (m)   f32 ? (0.025)
0x20 u16 flag, u16 marker_count u32 markers     -> {u16 kind, s16 ?, f32 track distance}
0x28 u32 grid_count            u32 grid         -> {f32 x, y, z, yaw}       start grid, 1–8 slots; heading = yaw + pi
0x30 u32 segment_count         u32 segments     -> 0x40-byte records
0x38 u32 wall_point_count      u32 wall_points  -> {f32 x, y, z; u32 tag}
0x40 u32 checkpoint_count      u32 checkpoints  -> 0x20-byte records
0x48 u32 grid_a                u32 grid_b       -> lookup grids (triangles, wall points)
0x50 u32 extra_count (4)       u32 extra        -> 0x20-byte records {0, 0, x, y, z, 0, f32, 0x80000000}
0x58 u32 lamp_count             u32 lamps        -> 0x20-byte lamp records (point-light courses; else 0, end)
```

- **Point-light courses**: header byte +0x21 = 1 (u16 +0x20 = 0x0101) on route5 / route5_dry / r_route5 /
  r_route5_dry (281 lamps), route11 / r_route11 (273) and the L_circle tests (28); every other course has 0.
  Lamp record (0x20 bytes): u32 type n (the Env0 car-light record n, Env0 +0xD0 + 0x80 n, gives the colours:
  +0x10/+0x20/+0x30 ambient/diffuse/specular, +0x40 reflection-disc size, +0x4C spot exponent, +0x50 cutoff
  in degrees (180 = none), +0x54/+0x58/+0x5C attenuation c0/c1/c2, +0x60..+0x68 shadow RGB, +0x6C shadow alpha,
  +0x70..+0x7C reflection-disc colour), vec3 spot axis ((0, 1, 0) on the disc), vec3 position (lamp heads
  11–27 m up), f32 kind (0 directional, else point; 1.0 on the disc). Records run along the track. Each car is
  lit by up to four lamps picked from the nine around the index under its front-left wheel, weighted
  w = 1 / (c0 + c1 d + c2 d²) (× max(r·axis, 0)^exp inside the cutoff), as directional lights along car →
  lamp with the record's colours × w; node 1 of the car model casts one shadow per lamp.
  - The index is attribute A of the hit road triangle's first vertex, read as a **signed** 16-bit value. The
    nine candidates start 4 before it (plus the lamp count when that is negative) and wrap from the last lamp
    to lamp 0. A car keeps its last index while that wheel finds no road.
  - On all ten point-light files every vertex's A is a lamp index, except on a strip at the start line
    (between the last lamp and lamp 0, 9–944 vertices per file) that holds 0xFFFF = -1: its candidates are
    the last five lamps and lamps 0–3. Read as unsigned (65535), the candidates would lie on the far side of
    the course and a car crossing the line would go dark.
- **Segment** (the road is cut into 20–204 pieces, about 30 m each, 0x40 bytes):
  - `u16 vertex_count`, `u16 triangle_count`, `u32 vertices`, `u32 triangles`, `u32 0`, `u32 wall_refs`, `u16 ?` (+0x14),
    `u16 neighbour_count` (+0x16), then `neighbour_count` × `u16` segment ids (+0x18, room for 20), zeros.
    The neighbour lists are symmetric, never contain the segment itself and hold 2–8 entries (the first two are the
    previous and next segment). The game's ground query uses them: after a hit it only accepts triangles of the
    same segment or its neighbours, which is how it keeps a car on the right level of a bridge. +0x14 is not
    neighbour_count + 1 (that only holds sometimes); meaning unknown.
  - Vertices are `{f32 x, y, z; u16 attrA, u16 attrB}`. The game divides the attributes by 65535, interpolates
    them barycentrically at the contact point (clamped to the triangle's min/max) and hands both to the wheel. Values:
    0xFFFF (most), 0 and small integers 1..~280; 74 of 80 courses have values other than 0xFFFF (the slalom
    courses have none). They are lighting values, not grip: on day courses B is an open-sky mask (0 in tunnels
    and under bridges) and A a direct-sun mask (0 in tunnels, under bridges with a wider soft edge, and along
    shaded stretches). The race averages them over the four wheels and scales the car lights by them: s = P2 +
    (1 - P2) b (1 - (1 - P1)(1 - a)) with (P1, P2) = Env0 +0xAC8/+0xACC (ambient), +0xAB4/+0xABC (diffuse),
    +0xAC0/+0xAC4 (specular); the car's light shadow takes F = (AB8 + (AB0 - AB8) a) b (Env0 +0xAB0/+0xAB8).
    On point-light courses A holds the index of the nearest lamp record (0xFFFF = -1 on the start-line strip,
    see "Point-light courses") and B = A + 1 (0 after the last lamp on Route 11 and the circle tests). The
    query also keeps the raw A of the hit triangle's first vertex.
  - Triangles are `{u16 a, b, c, u16 0; u16 material, u16 0; u16 flags, u16 0}` (the zero halves are zero on all
    1,142,387 triangles of the 80 US courses). The wheel query copies the material and flag bytes.
  - The wall_refs block lists u16 indices into the wall points that bound the segment (one 16-byte row in trialmt). It is kept raw.
- **Material** (triangle +0x08), all 80 US courses (triangles / area):

  | value | triangles | area (m²) | meaning (from top-down maps; see `docs/dev/TRACK.md`) |
  |---|---|---|---|
  | 0 | 646,338 | 7.00 M | tarmac (and pit lane) |
  | 1 | 132,503 | 0.16 M | kerbs: thin slivers along corner edges |
  | 2 | 236,806 | 4.87 M | grass verges |
  | 3 | 27,340 | 0.48 M | gravel run-off on corner exits |
  | 4 | 6,153 | 0.08 M | sand traps (Laguna Seca only) |
  | 5 | 80,815 | 0.75 M | dirt: only on the 15 Tahiti and Swiss Alps rally courses |
  | 7 | 9,904 | 0.06 M | paved road sections (Rome, Grand Valley, Deep Forest) |
  | 8 | 552 | 0.01 M | patches on smtsouth (meaning unknown) |
  | 9 | 1,976 | 0.01 M | road sections on Akasaka and Route 5 (meaning unknown) |

  The tyre model (physics §3.6) turns the material into a surface index: with flag 0x02 set, materials 0 and 7 become
  12, 1 becomes 13 and 9 becomes 14; otherwise the index is the material.
- **Flags** (triangle +0x0C): 0 (1,004,502), 0x01 (35,685), 0x02 (35,443), 0x10 (29,079), 0x08 (26,202), 0x18
  (7,141), 0x11 (2,635), 0x12 (1,618), 0x04 (80), 0x0A (2).
  - 0x01: pit lane (Trial Mountain map; the physics counts wheels on it).
  - 0x02: wet road (route5 and r_route5 but not the `_dry` files, and the wet circle tests).
  - 0x08: tunnels / covered sections (hypothesis; trialmt).
  - 0x10: stripes across the road and some zones (hypothesis).
  - 0x04: only on L_testcourse1.
- **Wall points** `{f32 x, y, z; u32 tag}` are concatenated polylines. The first point's tag holds the count in the low 16 bits, and the last point's tag holds `-count`. The game follows that -count back to the first point, so every polyline is a **closed loop** (all 284 lines on the 80 US courses). The upper 16 bits are point flags (the game reads the u16 at +0x0E). Bit 0 marks a corner vertex that is also tested against the car's edges (1,377 points). Bit 1 marks a sensor edge, starting at that point, that only records crossings; the game supports it but no US course uses it (0 of 154,682 points). There are 2–10 lines per course. They run along the outer edge of the road mesh (left edge, right edge, pit wall) with the drivable side on the **left** of the polyline direction in the world frame (checked for 102,473 one-sided edges against 45 the other way).
- **Checkpoint**: `{f32 x0, z0, x1, z1, distance, dir_x, dir_z, f32 ?}`. A line across the whole drivable width (including run-off; trialmt: 52 m) at `distance` metres from the start line. `(dir_x, dir_z)` is a unit vector perpendicular to the line pointing **against** the direction of travel (checked on all courses). They are about 15 m apart and stored as a ring that starts anywhere (trialmt: 254, the first one at 1957.8 m). The last float increases with distance on most courses (trialmt: distance − 1663.6); meaning unknown. `Road::centre_line()` joins the midpoints in distance order; the track runtime moves each midpoint to the middle of the racing surface (TRACK.md). On circle and slalom licence courses checkpoints only cover part of the area.
- **Start grid**: `{f32 x, y, z, yaw}`. The car's heading is `yaw + pi` (the game's accessor adds pi). With the car nose along -Z that gives forward = `(-sin h, 0, -cos h)`; this faces along the lap for every slot on all 80 US courses. y is 0 on most courses but carries real heights on some (smtsouth). Licence variants use only slot 0; the other slots are leftovers (one faces backwards in L_smtsouth1).
- **Header +0x1C**: 0 in 26 courses, the lap length in some base files, other values in licence files (L_smtsouth1: 833.8 m with slot 0 at 3246.8 m on a 3290.6 m lap), so possibly the goal distance of a licence test ❓. **+0x20**: 1, or 0x101 on the circle and route5/route11 courses ❓.
- **Grid A (+0x48): triangle lookup grid** (decoded, `Road::grid_index`). `u32 cells_off`, `s16 nx`, `s16 nz`, `f32 xs[nx]`, `f32 zs[nz]` (sorted breakpoints, uniform 8 m on every course), then at `cells_off` `(nx-1)·(nz-1)` u32 offsets, one per cell (index `zi·(nx-1)+xi`). Each points at groups `{u16 n, u16 segment, u16 triangle[n]}` ending with `n = 0`; empty cells share an empty list. Every triangle is listed in every cell it overlaps (verified on 79 of 80 courses; in r_route11 one triangle corner lies outside the breakpoint range). Points outside the breakpoint range have no road.
- **Grid B (+0x4C)**: same layout, but the ids index the wall/obstacle points (+0x3C) (decoded, `Road::wall_grid`). Every point appears exactly once, in the cell that holds it (`bp[i] ≤ x < bp[i+1]`); groups in a cell have distinct, ascending segment ids, and ids in a group ascend (all 80 US courses). The collision step walks it for the wall segments near a car (physics §5.3: the box grown by 8 m, clamped into the grid, groups of the car's road segment and its neighbours).
- **Before the segment table**: the game reads a car's neighbour list for road segment −1 (a car off the road mesh) from the words just before the segment table: a u16 count at `segments_off − 0x2A` and ids from `segments_off − 0x28` (the end of the start grid table or padding). The count is 0 on most courses and in the thousands on others (`Road::before_segments`).
- **Markers**: about 20 `(u16 kind 0/1/2, s16 -1/0/1, f32 distance)` records, distances increasing. Possibly corner/section marks ❓.

## AI lines (slot 31)

```
0x00 u32 size   u32 8   u32 0
0x0C u32 line offsets[7]        0 = absent; may point at an empty list
0x28 "build MM/DD/YY hh:mm:ss\n" (tool stamp, e.g. "build 04/14/01 05:34:42")
line: u32 n, n × 40-byte control points
  s32 flags     low byte 0/1, upper bytes small packed values
  s32 x, z      1/4096 m, sim frame (world_x = -x/4096)
  s32 0
  s32 segment   GTRW segment index
  s32 0
  s32 radius    1/4096 m: signed radius of the arc starting here (flags bit 0); 0 on straights
  s32 0, s32 0
  s32 packed    low 16 bits: heading, 4096 = one turn (sim frame)
```

- Words 3, 5, 7 and 8 are zero in every record of every course.
- Lines 0–2 and 5 cover the whole lap and are near-identical variants of the racing line. Lines 3 and 4 cover part of a lap (pit lane?). Line 6 is optional.
- Points are sparse: 4–90 per lap, denser in corners. The line is a chain of **straights and circular arcs**: a point with flags bit 0 set starts an arc to the next point whose signed radius (in metres, > 0 turning left in the world frame) is the `radius` word; otherwise a straight follows. Of the two arcs of that radius joining the points, the one that continues the previous segment's direction is meant (hairpins on the slalom courses exceed 180°). `AiLine::sample_arcs()` rebuilds it: 100% of the samples lie on the road in trialmt, 99.6% over all 80 US courses, and driving it laps every lap course (78/78; the two 60 m circle tests have a 3-point line).
- Line 0 is empty on some courses (several licence files, seattle, slalom80r, r_smtsouth); use the first non-empty of 0, 1, 2, 5, 6. The first and last point can be far apart (a long straight without points), so "closed" is better decided from coverage of the lap.

## Driver parameter sets (slots 37–40)

Each of the four slots holds the AI driver parameter set for one driveline class of car:
slot 37 + class, with class 0 = FR / MR / RR, 1 = FF and four-wheel drive system 0, 2 = other
four-wheel drive systems, 3 = four-wheel drive system 3. An offset of 0 means the course has no
set for that class; the game then uses its built-in set for the course category (ai §9.1).
Code: `src/formats/course/driver_sets.*`; the race start reads them (`src/sim/exact/start.*`).

Each section is 0x40 bytes. The first 32 bytes are the set; the other 32 are zero on the disc.
Bytes 0–26 are read, 27–31 are not. Bytes 25 and 26 are read only from the class-0 set, by the
start of a time trial or free run whose event gives no flying start (both must be non-zero).

| Byte | Meaning | Conversion |
|---|---|---|
| 0 | braking-distance percentage | k_brake = 100 / byte |
| 1 | corner speed scale | % |
| 2 | corner speed offset | (byte − 100) km/h |
| 3 | steering yaw-rate gain | byte × 5 % |
| 4, 5 | throttle cut / brake added on steering saturation | % |
| 6 | look-ahead full speed | byte × 2 km/h |
| 7, 8 | look-ahead minimum / maximum | m |
| 9, 10 | ASCC A gain / threshold | × 0.01 |
| 11, 12 | TCS lateral a / b | × 0.1 / × 0.01 |
| 13 | TCS gain | × 0.1 (0 = off) |
| 14 | front grip weight of the corner-speed estimate | % |
| 15 | corner bonus | km/h |
| 16 | steering rate | byte × 10 °/s |
| 17 | ticks in an arc before the bonus | byte × 10 |
| 18 | pit-lane speed | km/h (0 → 80) |
| 19 | ASCC channel flags: bits 0–7 = A-front, A-rear, D-front, D-rear, C-front, C-rear, E-front, E-rear | |
| 20 | ASCC D gain | × 0.01 |
| 21, 22 | ASCC C gain / threshold | × 0.01 |
| 23 | ASCC E gain | × 0.01 |
| 24 | TCS slip scale | × 0.01 (0 and 100 → 1) |
| 25 | time trial / free run flying start: distance before the line | byte × 10 m (0: none) |
| 26 | its speed | byte × 5 km/h (0: none) |

Survey of the 80 US files: 79 carry all four sets; L_circle60_wet1 has none (all four
offsets 0). The sets differ by course and often by class: braking percentages 100–200,
corner speed 90–110 %, look-ahead minima 5–30 m, pit-lane speed 78 or 80 km/h, and TCS gain 0 for most classes on
the dirt courses. Many licence files carry the built-in set's values.

## logger/: LGM0 data-logger maps

All 81 files were surveyed.

```
0x00 "LGM0"
0x10 u32 192, u32 128     map size in pixels (L_deepforest1/2: 384, 256)
0x18 f32 ox, oz, scale, rot (degrees), 0, 0   world→map transform (the GTCM fields; applied differently, below)
0x30 u32 Tex1 offset      0x34 u32 n
0x38 n × {f32 x, y, z, 0} 2..30 points along the lap
Tex1 (one T8 texture, the map image) to EOF
```

The texture is a 192×128 outline of the course (decoded and checked for trialmt), the
size of the Analyzer's map box (logger1.pmb `LoggerMapCourse_GraphMode`). A world point maps
to (w/2 + s(cos r x - sin r z - ox), h/2 + s(sin r x + cos r z - oz)), y down: the origin is
taken off after the rotation. (The HUD map's GTCM rotates the offset from its origin
instead, (w/2 + s(cos r dx - sin r dz), ...), dx = x - ox; the two agree only at r = 0.) Checked
on all 81 maps: 1347 of the 1411 points land within 2 px of their own outline, against 1109
with the GTCM formula, which misses on every course turned by more than about 15°
(trialmt -184.5°, seattle 180°, route5 -180°, super and testcourse -90°, smtsouth 17.2°;
test `analyzer_logger_maps_disc`). The points are used as the Analyzer's sector boundaries (their track distances; trialmt 19, the page has 40 sector
labels) (M: the original's use of them is not known).

## Environment, sun, lamps and billboards (slots 32, 29, 30; from the race workstream)

Facts from the race workstream (the race reads these every frame). All values f32
unless noted; offsets from the start of the slot.

### Env0 (slot 32)

A raw dump of the developers' 0xF40-byte environment struct (block size
0x1380, the rest zero); there is no separate parser: the race reads the fields
it uses (`race::SunParams::from_env0`, and the lamp glow, wet-road and tyre
smoke code).

| Off | Meaning |
|---|---|
| +0x00 | `Env0` |
| +0x10 | u32 flags: bits 0-3 car lights 0-3; 5 ambient particle field; 7 colour-grade pass; 10 sun disc + glare; 11-14 scene lights 0-3; 15 car lamp glows (and the `cars/night/` car models); 16 mirrored cars and 17 lamp streaks in the wet-road reflection; 19 the `cars/eve/` car models (before bit 15); 21 GTFX lamp glows (6, 8, 9, 18, 20, 22: replay / car-model modes, partly unknown) |
| +0x20 | far clip (m); the sun sits at far / 2 |
| +0x28 / +0x48 | yaw / pitch (degrees) of the small sky dome (slot 23) in the cars' reflection map: turned by the yaw first, then the pitch |
| +0x50 / +0x54 | scenery texture-LOD threshold / car LOD size threshold |
| +0x58 | headlamp pool strength (car model node 5, drawn only when non-zero: night and dusk courses) |
| +0x60..+0x68 | fog RGB (0..1) |
| +0x70 / +0x74 | fog distance A / minimum visibility b (1.0 = no fog): visibility = b + (1 - b) min(1, A / z), z = view depth (per vertex). The backdrop (slot 22) gets the same fog at true distance |
| +0xB0..+0xC8 | ambient particles: u32 count, box / size / speed |
| +0xD0 + 0x80 i | car light i: direction (towards the light, normalised by the renderer), ambient, diffuse, specular (w lanes 1); scaled per car by its shade (+0xAB0..+0xACC). Day courses enable light 0 (the sun) and often light 1, a light from below with a negative diffuse (rome, apricot, smtnorth/smtsouth, testcourse, super, r_deepforest, ...: direction (0, −0.29, 0) or (0, −0.79, −1), diffuse −0.2..−0.26, ambient and specular 0) that darkens the lower body. Global ambient +0xEF0 (w = 1) |
| +0x980..+0x994 | colour grade (flag 7): levels black / white (+0x98C / +0x990) stretched to 0 / 1, then per channel (g at +0x980..+0x988) out = x^2 + 2 g x (1 - x) (g = 0.5 identity), then saturation s (+0x994) around luma (0.30, 0.59, 0.11); no bloom. Only Monte Carlo (g 0.29 0.28 0.27, s 0.97) and reverse Deep Forest (0.5 0.5 0.45) change the picture |
| +0x9C0..+0x9E8 | sun: RGB, occlusion weight W, glare size k (angular half-size atan k), disc size d (angular radius atan d), disc gain, elevation, azimuth (degrees; direction (sin a cos e, -sin e, cos a cos e), e = -elev, a = -az, y up), i32 style (0 `race/glare.img` sprite, 1 72-spike burst, 2 glow disc), occlusion box N (pixels) |
| +0x9F0..+0xA1C | car-lamp glow type (as the lamp types below) |
| +0xA60..+0xA78 | car shadows (car model nodes 1 and 6, see car_model.md): RGB, alpha scale +0xA6C, direction elevation / azimuth +0xA70 / +0xA74 (degrees), length +0xA78 (shift per metre of height) |
| +0xAD0 + 0x50 i | scene light i: direction (towards the light), ambient +0x10, diffuse +0x20, specular +0x30 (+0x40 unused); enabled by +0x10 bits 11-14. The lights of the course's own lit packets (see "Lighting") |
| +0xC60 + 0x40 i | lamp type i (8): RGB, intensity, glow size (x10 = flare mesh scale), halo size, halo gain, directional exponent n, attenuation c / l / q (I = 1 / (c + l z + q z^2)), max distance, occlusion box (x128 px), blink period |
| +0xC20..+0xC54 | wet-road reflection pass: +0xC20 composite ALPHA mode (0x54 / 0x58), +0xC24 / +0xC28 buffer width / height, +0xC2C blur passes, +0xC40 fog block (the +0x60 layout: RGB, +0xC50 A, +0xC54 b) |
| +0xE60..+0xE7C | wet-road lamp streaks (`race/reflection.img`): RGB, gain, half width head-on / side-on (x lamp size), cap above the image, length below |

The sun: a 14-segment disc drawn right after the sky dome (geometry covers
it), and after the scene one additive screen-aligned glare billboard at the
sun, coloured RGB x visibility, where visibility is the passing fraction of
an N x N pixel depth test at the sun pixel. No ghosts, no falloff towards the
screen centre.

| Course | Sun elev / az | k | Fog A / b |
|---|---|---|---|
| trialmt | 31 / 92.05 | 0.40 | 120 / 0.70 |
| seattle | 16.8 / 220.25 | 0.45 | 55 / 0.65 |
| laguna | 11.6 / -31.6 | 1.10 | 4 / 1.0 (none) |
| tahitidirt | off | – | 125 / 0.50 |
| midfield | off | – | 155 / 0.70 |

### Vls0 (slots 2–4, 6–7, 10, 14)

Offsets from the block start. +0x10 u16 grid width nx, +0x12 u16 grid depth
nz, +0x14 offset of nx × nz cell offsets (row-major, z rows), +0x18 / +0x1C
f32 grid origin x / z, +0x20 / +0x24 f32 extent in x / z, +0x28 / +0x2C f32
1 / extent. A cell is a zero-terminated list of offsets of view points
{u32 offset of a node list, f32 x, y, z} (cells share points); a node list is
u16 n, then n u16 indices of the visible slot 1 nodes. The game picks the
cell under the camera (index = trunc((x − x0) · nx / extent), clamped to the
grid; the same for z), then that cell's point nearest the camera (squared 3D
distance; the first point on ties), and uses its node list. Route 11 slot 2:
16 × 16 cells over x −600..650, z −250..350, 66 points; from the start grid
111 of the 1344 nodes are visible.

### Lighting

The course's lit packets are coloured per vertex with OpenGL-style
fixed-function maths and these inputs, all from the course file:

- **Lights**: the enabled Env0 scene lights (+0xAD0, bits 11-14) and a global ambient of 0. The
  car lights (+0xD0) light only the cars. Light slots go, in light order, to the enabled lights
  with a non-zero diffuse or specular; the ambient sums every enabled light's ambient. Every
  day course enables scene light 0 only (directions and colours differ per course, e.g. Trial
  Mountain specular 1.5, Rome diffuse 0); the night routes enable none.
- **Materials**: the GTM1 material records (Ma, Md, Ms, Me, shininess; see car_model.md).
- **Kind 1** (lit): colour = s (Me + Ma (La + global ambient) + sum Md Ld max(N.L, 0) +
  sum Ms Ls max(N.H, 0)^p), clamped to 0..255, H = normalize(L + the camera's back axis)
  (infinite viewer); p = 4 / 8 / 16 for shininess < 6 / < 12 / >= 12; s = 128 or 255 (flag
  0x100). Every lit course material has Ms = 0, so courses get ambient + diffuse only.
- **Kind 4** (the road sheen): the first light slot's specular only, Phong with a local viewer
  2 m above the camera along its up axis: with D = vertex - viewer and R = D - 2 (D.N) N,
  c = max(0, L.R / |D|)^p and colour = s Ls Ms c, not clamped (the GS keeps the low 8 bits;
  no course gets there), alpha 0x7F. Drawn after the base pass with ALPHA 0x58 (Cs Ad + Cd: masked
  by the frame alpha the base pass wrote), no depth write, fog colour 0 (node op 0x21), the
  texel modulated by the colour. Sheen materials: Ms 0.19-1.0, shininess 0-20 (Super Speedway
  Ms (0.6, 0.584, 0.536), shininess 4, with scene light 0's specular (0.83, 0.80, 0.75): up to
  about half the texel, a warm tint where the camera faces the sun). The exponent comes from the
  shininess alone: the untextured flag-0x104 sheen packets of Laguna, Akasaka, Apricot, Midfield
  and Rome use a material with Ms = 0 and a shininess of about 0 (p 4), so they add nothing.
- The backdrop (slot 22) is drawn with its modelview scaled by 0.01 in eye space (depth and fog
  distance scale together, nothing changes on screen); the sheen viewer, taken through a rigid
  inverse of that modelview, then sits at 0.0001 x the camera position + 0.02 x its up axis.
- The colour is interpolated linearly in screen space (GS Gouraud), not perspective-correct;
  on the long road strips (vertices every ~23 m) this gives the sheen its flat near-road look.
- Fog (Env0 +0x70 / +0x74) is computed per vertex from the view depth W:
  F = trunc(b' + 0.1 + (255 - b') min(W, A) / W), b' = 255 b, and the GS mixes the colour
  towards the fog colour by 1 - F / 255.
- **Kind 2** (environment pass, sphere map): from the camera position E (not the light model
  viewer), D = vertex - E, R = D - 2 (D.N) N with N from the UV slot, R_eye = the view rotation x
  R / |D|, m = |R_eye + (0, 0, 1)|, UV = (0.5 + 0.5 Rx / m, 0.5 - 0.5 Ry / m); colour = vertex
  colour x (C0 + C1 min(D.N / |D|, 0)) (node ops 0x36 / 0x37 and 0x32 / 0x33; on courses C0 = 1
  and C1 = 0 except where op 0x33 sets it before a draw), not clamped, alpha too. The texture is
  the packet's own (mat low 9 bits - 1, its CLAMP from the texture record). Packets with the
  runtime slot 0x1FF (apricot, midfield: chunks 0x102) bind nothing while the course is drawn,
  so the texture the packet before bound stays. On the backdrop the 0.01 modelview scale squeezes
  the UVs to about 0.5 +- 0.005.
- **Kind 3** (environment pass, panoramic): per vertex from the camera E, D, R as for kind 2;
  r = R rotated by -phi about y (r.x = Rx cos phi + Rz sin phi, r.y = Ry, r.z = -Rx sin phi + Rz
  cos phi); u = r.z / r.x, 2 - r.x / r.z, 4 + r.z / r.x or 6 - r.x / r.z for (r.x - r.z, r.x + r.z)
  = (>= 0, >= 0), (< 0, >= 0), (< 0, < 0), (>= 0, < 0); y = r.y / |D|; UV = (0.875 - (g + 1) / 8 -
  u / 8, 0.5 + E4 - (E0 / 6)(y^3 + 2 y)) with E0 / E4 = Env0 +0xEC0 / +0xEC4 (4 / 0.25; akasaka 6
  / 0.3). phi = 45 (g + 1) degrees, g = 0 when the sum of the components of a direction is
  positive, else 4: the direction is the node op 0x31 point minus the camera (op 0x31 vec3; it
  stays set), else the camera's forward at the start of the pass (the game sums the components
  where it means to rotate them). Colour = vertex colour x (C0 + C1 min(D.N / |D|, 0)), no clamp.
  The packet's own texture, S repeat (T repeat, or clamped on akasaka's 512x128 images).
- The frame alpha these ALPHA 0x58 passes read is the texel x vertex alpha of what lies under
  them (0x80 = 1.0). Opaque packets whose node program turns the alpha test off (op 0x1B) draw
  every texel whatever its alpha: their alpha is only that weight. Route 11's perforated wall
  panels (alpha 0x30 on the frame, 0 on the mesh: the orange kind-2 pass shows on the frames
  only), the road-edge strips (a ragged alpha that keeps the sheen off the dirt) and Laguna's
  wall banners (alpha 0x40 throughout) are such packets, not cut-outs.
- Dirt and wet courses have no lighting of their own: the differences are data. The wet routes
  have no sheen; their wet look is the reflection pass (+0xC20).
- The rear-view mirror draws the low-resolution sky (slot 24) and slot 1 (with the sheen),
  with its own fog: Env0 +0xA90 RGB, +0xAA0 distance A, +0xAA4 minimum visibility b (A = 0: a
  constant visibility b; Super Speedway 0 / 1 = none, route11 0 / 0.72).

### GTFX (slot 29)

`GTFX`, +0x0C block size, +0x10 u32 has-table, +0x14 offset of the table:
u32 list count, then list offsets (0 = empty); a list is u32 n then n
32-byte lamps {u32 type (0..7), vec3 position, vec3 facing, f32 blink
phase}. List i holds the lamps of slot 1 node i (inside its bounding
sphere); there are fewer lists than nodes (Route 11: 84 lists, 363 lamps).
Each frame the game visits the lists of the nodes in the slot 2 node list
of the camera (Vls0 above), in that list's order, and keeps at most 100
lamps that pass the screen test: in front of the camera, no further than the
type's max distance, and the square of the type's halo size around the lamp
overlapping the screen. The cap counts lamps facing away too.

Per kept lamp (lamp types in Env0 +0xC60, see "Lighting"): directional
factor D = (facing · normalize(lamp − camera))^n with n = int(exponent + 0.5)
on a round-toward-zero FPU, so an exponent stored as 0.49999997 (Route 11
types 0 and 4) gives n = 0 and D = 1; a negative dot product (n ≥ 1) drops
the glow but not the halo. Attenuation 1 / (c + l z + q z²) with z the view
depth; blink 0.5 + 0.5 sin(2π (t / P + phase)) when P > 0. The glow is
`race/flare.bin` in view space at the lamp pulled 0.5 m towards the camera,
scaled by the glow size × 10, without depth test: vertex RGB × colour (type
RGB × intensity × attenuation × blink × D, clamped to 255), vertex alpha ×
visibility, blended Cs·As/128 + Cd. Visibility: a depth test of a 10 × 10
pixel box at the lamp, 0.2 m nearer than it, worth 2 × the passing fraction
(capped at 1). The halo is a depth-tested burst mesh of the halo size,
colour × gain × 0.5, drawn for every kept lamp further than 1 m whatever D
and the visibility.

`race/flare.bin`: +0x0A u16 mesh count, at +0x10 per mesh {u32 offset, u16
qwc, u16 texture}; each mesh is a VIF packet laid out as a GTM1 chunk
(positions V3-32 at 0x00, strip table at 0x40, V4-8 colours at 0x80),
untextured triangle strips with alpha blending. Mesh 0 is a disc of radius
0.425 (centre RGBA 0xFF, rim 0; some wedges stored twice), meshes 1 and 2
are three thin double-ended rays of half-length 1.18 through the centre at
35°, −25° and 95° (white, alpha 0x54 along the core near the centre, 0 at
the edges and tips): a six-point star.

Seattle 69 lamps, Route 5 / 11 about 300, the circle stadiums, Smokey
Mountain North, reverse Grand Valley; most other courses have one empty
list.

### BLBd (slot 30)

Header: +0x10 u16 list count, +0x12 u16 count of 0x50-byte records (+0x20,
always 0), +0x14 u16 sprite-type count, +0x16 u16 mesh count (always 0),
+0x18 offset of the list offsets, +0x1C offset of an embedded Tex1, +0x24
offset of the sprite types {f32 width, f32 height, offset of 4 UVs, offset of
4 RGBA colours (0x80 = 1.0)}. A list (one per visibility cell): u32 count,
then 16-byte instances {u16 type, u16 texture + 1, f32 x, y, z} (bottom
centre). Corner order bottom-left, bottom-right, top-left, top-right; some
types store the image rotated in the texture. Tahiti dirt 596 sprites
(trees), Seattle 307, Rome 1073 (spectators), Laguna 12.

## Rigid bodies: pylons, cones, small boards (GTRW +0x50, slot 36; from the race workstream)

- **Placements**: GTRW header +0x50 = count, +0x54 = offset (from the GTRW start) of 32-byte
  records: +0x00 u32 type index (read as u16), +0x04 u32 0, +0x08 f32 x, y, z (course frame,
  as the triangles; y is replaced at race start, see below), +0x14 f32 Euler angles a, b, c
  (b = yaw; a and c are 0 on the disc). The game's pool holds 100 bodies: records past the
  100th are never created (L_slalom80r3 has 111).
- **Types**: slot 36 (offset-table entry 0x90; 0 on courses without bodies): +0x00 u32 0,
  +0x04 zero-terminated u32 offsets (from the slot start) of type descriptors: +0x00 u32 N hull
  points, +0x04/+0x08/+0x0C f32 principal inertias, +0x10 N x {f32 x, y, z, 1} hull points
  (centre of mass at the origin, y up), +0x90 f32 bounding radius, +0x94 mass, +0x98 1/mass,
  +0xAC offset (from the descriptor) of the type's own GTM1 at +0xB0 (stream 0 = the body,
  stream 1 = a planar shadow drawn projected onto the ground along the light).
- Cone (L_testcourse1 type 0): 5 hull points, base 0.367 m square, 0.789 m high, centre of mass
  0.196 m above the base; inertia (4, 3, 4), radius 1.162, mass 15 (heavier 35 kg cones on
  L_deepforest1/2 and L_grandvalley2, 5 kg on L_slalom80r4); 8-point thin boards (mass 8) on
  the laguna and slalom families.
- Bodies on every L_* course except L_testcourse2/3 (L_slalom80r1-4: 95 / 98 / 111 / 77,
  L_laguna1 52, L_testcourse1 40, ...) and on some normal courses (laguna 47, slalom80r 79,
  tahitidirt 25, rome 17, super 8, trialmt 4, ...). `gadget/pylon.mdl` is not used by the race.
- L_testcourse1 (B-1 / B-2): row 1 of 20 cones at z = -590.7 (about 1026.9 m from the start
  line, 1.9 m past the stop box end), x = -979.3 .. -956.5 every 1.2 m; row 2 (20 cones) 352.9 m
  behind the start line, never reached.

## Other files

- `cameras/attach.cam`, `start.cam`: `GTRC`, the layout of slot 34 (docs/dev/RACE.md "Replay (TV) cameras"); the race's pre-race shot plays `start.cam`'s record 0.
- `replay/*.dat`: 3 files of 87,128 bytes. Start `u32 7200, u32 7200` (frames?), with a flag at 0x0C.
- `demo/*`: 67 files in the game's save envelope (docs/FORMATS.md "Demo runs"): `u32 0x40`, `u32` body size, `u32 0x40000/0x30000/0x10000`, `u32` CRC-32, then `"Base"` at 0x10. The licence and arcade time-trial demonstrations are decoded (`src/race/demo_replay.*`).
- `demo/thumbnail/*`: 68 files of 96 bytes.

## Tools

```
gt3tool course-info   <iso> courses/US/trialmt
gt3tool course-obj    <iso> courses/US/trialmt <outdir> [road] [lines] [sky] [scenery2]
gt3tool course-shot   <iso> courses/US/trialmt <out.png> [top|persp] [road] [lines] [noscenery] [courseline] [size=N]
gt3tool course-obj    <iso> courses/US/L_trialmt1 <outdir> noscenery courseline   # the course line alone
gt3tool course-survey <iso> [courses/] [textures]
gt3r <iso> --screen courses
```

`course-shot` is a small software rasterizer (z-buffer, nearest texture
sampling, alpha test) for checking decodes offline.

## Sky dome (slot 21)

- One node, one chunk, PRIM 0x1C (textured, Gouraud, no fog, no blend), ALPHA test entry state.
  Trial Mountain: a 1024×256 T8 texture (CT32 CLUT, REGION_CLAMP over the whole texture,
  TEX1 bilinear magnify / minify, no mipmaps) on a 100 m dome reaching y = -13.5 below the eye,
  plus one 128×16 T4 cut-out quad (alpha 0 / 0x80) inside the dome. Vertex colours are 0x80
  (1.0) everywhere: no colour scale.
- The race clears the frame to black (RGBA 0) with depth before drawing (colour and depth
  both cleared). Domes that stop at y = 0 (Rome: y 0..40)
  leave black wherever no scenery is drawn below the horizon.

## Render state in GTM1 node programs (from the race workstream)

- Node op `0x20` (u8 mode, u8 fix) sets the GS `ALPHA` register: `0x44` is
  normal blending, `0x58` is additive weighted by framebuffer alpha (used by
  the road "sheen" passes).
- Node op `0x24` (u32) sets the GS framebuffer write mask (`FBMSK`):
  `0x00FFFFFF` writes alpha only (shadow masks that switch the sheen off under
  trees and bridges), `0xFF000000` writes colour only (decals).
- Chunk flag 4 marks the second road "sheen" pass; base road vertex alpha
  (0 or 0x80) is the per-vertex sheen mask it is weighted by.
- Course GTM1 texture table 1 holds quarter-resolution copies of every set.
- Op `0x2F` (u8) is TEST.AFAIL. Course foliage (6,274 packets on the 19 base US courses) is
  drawn with PRIM ABE, ALPHA 0x44, alpha test GREATER 0x7F and AFAIL 3 (RGB_ONLY): fully
  opaque texels (alpha 0x80) pass and write colour and depth; translucent ones fail and
  still blend their colour, but write no depth (and no alpha). The node restores AFAIL 0 and
  AREF 0x20 afterwards. Blending these edges *with* depth writes hides foliage drawn after
  them (sky showing through tree canopies).
- Ops `0x25` / `0x26` (depth write off / on) wrap most blended scenery: decals, ground shadow
  overlays (ALPHA 0x44, ATE off, FBMSK 0xFF000000; 7,562 packets) and the 0x58 sheen passes.
- Ops `0x2D` / `0x2E` turn back-face culling on / off. Culling is on at stream entry; nodes
  turn it off around double-sided parts (animated flags and banners).
- ALPHA 0x58 only applies to packets with PRIM.ABE. Some road base passes (Laguna, Rome,
  Tahiti dirt walls) are opaque packets (PRIM 0x3C) drawn while the register still holds
  0x58; they are ordinary opaque geometry, not sheen passes.
- State histogram per slot: `gt3tool course-states <iso> courses/US/<name> [slot] [tex | cover x z r]`.

## Course line (slot 27)

The licence tests' driving line: a dashed ribbon on the road along the test's section.
Code: `course::course_line_model()` (`src/formats/course/course_model.*`), drawn by
`race::CourseScene`; tests `course_line_all_us`, `course_line_synthetic`,
`gtm1_state_carried_between_nodes`, `race_license_course_line_disc`.

- **Files.** 13 course files in every region folder (52 of 320): `L_deepforest1a`,
  `L_deepforest1b`, `L_deepforest2a`, `L_grandvalley1`, `L_grandvalley2`, `L_grandvalley2a`,
  `L_grandvalley2b`, `L_laguna1`, `L_midfield1`, `L_midfield2`, `L_midfield3`, `L_seattle2`,
  `L_trialmt1` (10–32 KB). The `a` / `b` variants of one section carry different lines (each
  test's own car); `L_deepforest1`, `L_deepforest2`, `L_deepforest2b` and `L_grandvalley3`
  have none.
- **Model.** A GTM1 with 2 nodes, 1 chunk, no textures and an all-zero bounding-sphere table.
  Node 0 only sets render state: `2E` (culling off), `25` (depth write off), `24 0xFF000000`
  (FBMSK: colour only, the frame alpha is kept), end. Node 1 selects texture set 0 and draws
  chunk 0. The game draws a course model's nodes in order with the render state carried from
  one node into the next (the default state, as in [car_model.md](car_model.md), is set once
  per model), so node 0's state applies to node 1's draw.
- **Geometry.** PRIM 0x2C (triangle strips, Gouraud, fogged, untextured, no blending). Every
  strip has 4 vertices: one dash, 0.40 m wide; dashes and the gaps between them are each 2.0–2.7 m
  long (104–315 dashes per file, 480–1450 m of road). Dashes sit about 0.1 m above the
  road surface. The first dash starts 0–3 m before the start line (track distance just under the lap
  length), the last at the end of the test's section.
- **Colour.** Pre-lit vertex colours (alpha 0x80), one colour per dash, from one signed value:
  blue `(0, 0, 0xFF)` through grey `(0x80, 0x80, 0x80)` to red `(0xFF, 0, 0)`, i.e. `(c, c, 255 − c)`
  on the blue side and `(255 − c, c, c)` on the red side. The packets are untextured and
  kind-0 colours pass through unchanged, so the GS shows them as they are: the grey is mid
  grey, not white (checked on a PCSX2 capture of B-3: grey dashes ≈ (113, 115, 115) under fog,
  blue (5, 4, 241)). The value is the licence demonstration's pedals at that dash, (brake −
  throttle): blue full throttle, grey neither (lifts, gear changes), red braking, the shades
  partial pedal (each line matches its own test's demonstration best: B-8 Trial Mountain 25 of
  26 non-blue dashes). Static: no animators, textures or palette, and nothing in the game
  changes it while driving.
- **When it shows.** Always when the course file has it: the game's scenery draw draws the
  slot after the main scenery and the billboards, in every view that draws the course (live
  run, demonstration, replays in all four replay modes, the licence menu's preview), with no
  condition. Which tests show it is therefore decided by the course file the event names
  (database table 32 +0x08): B-3 .. B-8 and A-1, A-2, A-3, A-6, A-7, A-8 (12 of the 48 tests;
  `L_grandvalley2` is not used by any event). The replay view mode "course line" (mode 3)
  does not switch it: no line is drawn on any other course in that mode.
