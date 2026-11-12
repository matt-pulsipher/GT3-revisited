# Car and wheel models (GTM1)

Implemented in `src/formats/model/` (`gtm1.*`, `car_model.*`, `wheel_model.*`)
with the generic VIF/GIF helpers in `src/formats/ps2/vif.*`.
Status: geometry, UVs, normals/colours, LODs, texture binding, paint colours
and wheel placement are decoded for every car and wheel: `gt3tool model-survey`
decodes 2073/2073 files (859 `cars/*` files at 3 LODs, 287 `menu/cars`, 464
`wheel`, 463 `menu/wheel`; 4 empty placeholders skipped). Tyres are generated,
as the game does. Open: the render-state ops, the rest of the chunk flag high
byte, time-of-day differences.

Sources: own analysis of the disc data, rendering comparisons with the original, and the course format work
(opcode table).

All values are little endian. Positions are IEEE floats in metres.

## Car file: `cars/{day,eve,night}/<id>` (gzip)

The ids are listed one per line in `cars/.list` (287 lines). `ho0100` is an
empty (0-byte) placeholder in all three folders (it reads as an empty file;
the tools skip it). `day`, `eve` and `night` share the layout. The race picks
the folder by time of day (the course's Env0 flags, [course.md](course.md)).
`eve`/`night` GTM1s are identical to each other for 285 cars and differ from
`day` (re-baked darker palettes, one more texture per set, more materials).
`menu/cars/<id>` is the same container with a smaller, single-texture-set
model.

```
0x00 u32 0              offset of this header
0x04 u32 0x40           "GTCI" car info
0x08 u32                "GTM1" body model
0x0C u32                "GTTR" tyre
0x10 u32                "GTTW" wheel
0x14 u32                end of data (file size - 64; a modified game's files may leave it 0: the reader then takes the file's end)
0x18..0x3F 0
end: 64 bytes, "dummy" + zeros
```
All 859 non-empty car files match this exactly.

### GTCI (car info)

```
0x00 "GTCI"  0x0C u32 size
0x10 a few u32/float pairs, then a table of (u32 count, u32 offset) groups
```
followed by 0x80-byte records holding a 3x4 matrix, a position and
parameters. In `am0006` they are: 6 tail lamps (z ≈ -2.05..-2.16), 4 head
lamps (z ≈ +2.38), 2 more front points, then camera-like records (position,
two angles in degrees, a float near 1.2..1.7 and a type code 3/7/0xA/0xD/0xF).
Wheel positions were **not** found here.

The brake records (see "Brake parts"): 0x14 bytes each, the front axle's at +0x14 and the
rear's at +0x28 (+0x10 is 1 on every car):

```
+0x00 u32 caliper: record (value - 2) of brake.bin's caliper table; 0 or 1 = none
+0x04 u32 rotor: record (value - 1) of the rotor table
+0x08 f32 brake radius r (m)
+0x0C f32 the parts' offset along the axle from the hub centre (m; negative = towards the outer face)
+0x10 f32 the caliper's angle (degrees)
```
`am0006`: front (6, 3, 0.192, -0.045, 0), rear (6, 3, 0.188, -0.045, -40). Every car's
records fall in the tables (rotor 1..7, caliper 1..31); r is 0.1..0.2 m, or 1.0 on a few
race and concept cars without a caliper; the offset -0.09..0.12 m. The times of day share
them but for `cv0022`'s calipers (21 by day, 9 at night).

The groups the race reads (u32 count, u32 offset from the block's start; the
race turns the offsets into pointers when it loads the car):

| Group | Records |
|---|---|
| +0x40, +0x48, +0x50 | lamp lists, 0x80 bytes each (docs/dev/RACE.md "Car lamps") |
| +0x58 count, +0x5C offset | **spark points**, 16 bytes each: f32 x, y, z in the model frame, a fourth word the race does not read |
| +0x68 count, +0x6C offset | camera mounts, 0x40 bytes each (presentation §G) |

Spark points are where the race's sparks start when the point dips below the
road at speed (presentation §L7; `car_spark_points`). 24 of the 286 models in
`cars/day/` have them, all at y ≈ 0 (the body's lowest point) and z +0.83..+1.97
(behind the origin): ml0001, ml0002 (F688/S), ml0003 (F090/S), lo0024 (F687/S),
wl0001, wl0002 / wl0003 (F094/H, F094/S), wl1001 (F686/M), pd0001, pd0010,
ma0095 (787B), ni0062 (R390 GT1), ty0056, ty0141 / ty2056 (GT-One), mb0010,
op0003 / op0011, va0003 / va0011, tf0001, cv0024, la0007 / la0008 (Stratos, one
point). `cars/eve/` has them on the same 24, `cars/night/` on the same but
ml0001 and pd0001.

### GTTR (tyre)

```
0x00 "GTTR"  0x0C u32 size  0x10 u32 texture repeats around the tread (2 or 1)
0x14 u32 flags  0x18 f32 the tyre material's shininess (8.0)  0x1C u32 0x20 (Tex1 offset)
0x20 Tex1 set (one T4 texture, 128x64 in cars/, 256x128 in menu/cars/: tread in
     the top half, sidewall lettering below)
```
Flags: bit 0 the race's tread blur (the texture redrawn as shifted copies while
the wheel turns, see "Wheel spin blur"), bit 1 a symmetric tyre (below). On the disc
(repeats, flags) are (2, 0) on 227 cars, (1, 1) on 52 and (1, 3) on 7. The
`tire/n1..n49` files have the same structure; the embedded one equals
`tire/n1`. There is no tyre geometry: the game builds it (see "Tyres" below,
`tyre_model.*`).

### GTTW (wheel) — also `wheel/<id>`

```
0x00 "GTTW"  0x0C u32 size  0x10 u32 flags (0 or 8)  0x1C u32 0x20 (GTM1 offset)
0x20 GTM1 model
```
Section 4 of a car file is byte-identical to `wheel/<id>`. There are 464 wheel
files: one per car and aftermarket rims (`enw001_s` and similar; the suffix is
probably the colour). The GTM1 holds only the **rim** at unit scale (lip
radius ≈ 1.0, outer face at x = 0 pointing -X, barrel to x ≈ +0.17), i.e. a
left-hand wheel. Wheels have 1 node with 4 LODs and 6 texture-LOD/variant
tables (+0x34). Their textured packets use the runtime texture slot (`mat`
texture field 0x1FF, set with op 0x29), which holds the rim image: the first
texture of entry 1 of the texture-LOD table (+0x34, below). The current set's
first texture is not always that image: it differs on 53 of the 286 car files'
wheels and 77 of the 464 wheel files (the Miata LS's is a dark copy under
another palette, a flat disc), and on none of the 463 `menu/wheel` files.
GTTW +0x10 flags: bit 0 = texture set per axle, bit 1 = left/right variants,
bit 2 = right wheel mirrored (asymmetric spokes), bit 3 = no variants (medium
confidence).

## GTM1 model

Offsets are relative to the `GTM1` magic. Pointer fields are relocated in
place by the game (header `+0x04` holds the relocation base at run time).

```
0x00 "GTM1"
0x10 u16 node count            0x20 u32 -> u32 node program offsets[]
0x12 u16 chunk count           0x24 u32 -> u32 chunk offsets[]
0x14 u16 material count        0x28 u32 -> material records (0x50 bytes each)
0x16 u16 texture set count     0x2C u32 -> u32 Tex1 offsets[] (0 = empty slot)
0x18 u16 texture LOD count     0x30 u32 -> bounding spheres, 16 bytes per node (centre xyz, radius)
0x1A u16 count for 0x38        0x34 u32 -> per-texture-LOD tables of Tex1 offsets (0 for cars)
                               0x38 u32 -> ? (cars: 0x1E0, small table)
                               0x3C u32 -> animation list (0 for cars; course.md)
```

Car bodies have 7 nodes. Node 0 is the body (3 LODs, or 4 for a few cars).
Node 1 is the light shadow: a band at roof-outline height (alpha 0 at the outer
edge), skirts down to y = 0 and a footprint cap; the race draws it sheared
along the light in the car's ground frame (x' = x + d.x·y, y' = 0, z' = z +
d.z·y). Node 6 is the contact shadow: the footprint at y = 0, alpha fading to
a 0 rim, drawn unsheared. Both use shape type 5 packets (positions, an RGBA
byte per vertex whose alpha is the coverage, no normals): colour = clamp(bias
+ byte·scale), with the bias = the course's Env0 +0xA60 shadow colour and
alpha = vertex alpha × Env0 +0xA6C (× a road-shade factor F for node 1,
Env0 +0xAB0/+0xAB8), normal alpha blend, no depth writes, lifted 2 cm with a
small depth bias. Node 5 is not a shadow: two flat textured quads ahead of
the nose (texture set 4 on the R34), the headlamp pool, drawn additively only
when Env0 +0x58 is non-zero (night and dusk courses). Nodes 2..4 are empty
(`00`) in race car files. In `menu/cars` files node 1 is empty, node 6 is a
textured footprint quad (the showroom shadow), and nodes 2 and 3 hold the
front and rear axles' brake disc and caliper (about 80 triangles each). They
are modelled for a left wheel, in the rim's unit frame (x about 0.31..0.54
inboard of the rim face, radius about 0.64), and drawn with the wheels (see
"Brake parts" below). Each has 3 or 4 LODs. Op 0x07 picks the LOD from the projected size of a sphere (always radius
3.0 on disc); car models use 3 levels. Wheels have 1 node with 4 LODs. Every car has 4 texture sets, one per
body LOD plus one for the shadow.

### Node programs

These are byte-coded programs with unaligned little-endian operands, run when
the node is drawn. Operand sizes (bytes after the opcode); all
1323 car/wheel programs parse with them and every jump lands on an opcode:

| op | operands | meaning |
|---|---|---|
| 00 | 0 | end |
| 01 | 0 | no-op |
| 02 / 03 | u8 / u16 | call node (a node the model lacks: nothing) |
| 04 / 05 | u8 / u16 | draw chunk (a chunk the model lacks: nothing) |
| 06 | u16 id, u8 n, s16 rel[n] | switch on the model's callback value `id` (out of range = skip). Cars: id 0 = the brake pedal (branch 1: lit lenses); ids 1-5 set the phase of the next op 0x35 (below) and give 0 |
| 07 | 16 B sphere, u8 n, s16 rel[n] | LOD switch, branch = min(lod, n-1) |
| 08 / 09 | s8 / s16 | relative jump |
| 0A | u8 n, n×12 B points, s16 skip | bounding-box cull; skip if fully outside |
| 0B | 0 | end of culled block (re-enable clipping) |
| 0C / 0D | 0 | push / pop matrix |
| 0E | u8 | |
| 0F / 10 | 64 B | load / multiply 4x4 matrix (row vectors, v' = v·M) |
| 11 / 12 | 12 B | translate / scale |
| 13 | 16 B | axis + angle rotation |
| 14 / 15 / 16 | f32 | rotate about X / Y / Z (degrees) |
| 17, 18, 1A, 1B, 1D, 1E, 22, 23, 25, 26, 2D, 2E, 30 | 0 | render state toggles |
| 19, 1F, 29, 2A, 2F | u8 | render state |
| 1C, 20 | u8 u8 | render state |
| 21, 24, 32, 36 | 4 B | render state (24: FBMSK, below; 32 / 36: f32 reflection rows C1 / C0, below) |
| 27, 34 | f32 | |
| 28, 33, 37 | 16 B | (33 / 37: vec4 reflection rows C1 / C0, e.g. 0.8,0.8,0.8,0) |
| 31 | 12 B | |
| 2B / 2C | u8 / u16 | select texture set (index into 0x2C, or the 0x34 table of the active texture LOD) |
| 34 | f32 | phase (clamped 0..1) of the next op 0x35 |
| 35 | u16 base, u16 count | draw an animated chunk range: chunk base + min(phase x count, count - 1), morphed by the fraction (course.md "+0x3C") |

An op none of these (no disc's model has one; a modified game's may) ends the node's program there.

Render-state ops that matter for cars (each sets GS drawing state):

| op | GS state | car usage |
|---|---|---|
| 1A / 1B | TEST.ATE on / off | on for body passes, off for reflection passes |
| 1C a, r | TEST.ATST = a, AREF = r | (6 GREATER, 0x20) by default; (6, 0) or (5 GEQUAL, 0) before some passes |
| 1D / 1E | TEST.DATE on / off | on for reflection passes, which draw with DATM 0 (where the frame alpha < 0x80) |
| 20 m, f | ALPHA = m (A\|B<<2\|C<<4\|D<<6), FIX = f | 0x44 = (Cs-Cd)·As+Cd for glass/decals, 0x58 = Cs·Ad+Cd for reflections and specular overlays |
| 24 v | FRAME.FBMSK = v | 0xFF000000 = colour only (glass, decals keep the reflectivity alpha), 0xFFFFFFFF = depth only, 0 = all |
| 17 / 18 | depth test on / off (17 only when depth testing is enabled globally) | |
| 19 f | TEST.ZTST from f: 0 NEVER, 1 ALWAYS, 2 GREATER, 3 GEQUAL | not used by ml0002 (nor Laguna Seca's scenery) |
| 27 / 28 | depth offsets: 27 adds an f32 in Z units; 28 the projected depth difference of two points from its operands | not used by ml0002 (nor Laguna Seca's scenery); not decoded |
| 25 / 26 | ZBUF.ZMSK on / off (depth write off / on) | shadow nodes |
| 2D / 2E | back-face culling on / off | |
| 2F a | TEST.AFAIL = a & 3 (0 KEEP, 1 FB_ONLY, 2 ZB_ONLY, 3 RGB_ONLY) | courses (foliage) |

The entry state of a node program stream (also for course scenery) is: depth
test per the global flag, ZTST GEQUAL, ATE on, DATE off, culling on, ALPHA 0x44,
depth write on, FBMSK 0, ATST GREATER AREF 0x20, DATM 1, AFAIL KEEP.

The Z buffer is PSMZ24, written as Z = Zmax/2 · (1 − z/w) + 1 for the projected
z/w in −1..1: its maximum at the near plane, 0 at the far plane. With every pass
tested GEQUAL, a second pass over the same vertices passes on equal depth. Decals
are not such passes: they are their own geometry 1-8 mm above the body (ml0002's
sidepod and engine cover decals), so they need the depth to resolve millimetres at
the replay cameras' 100-150 m telephoto distances (docs/dev/RENDERING.md, coordinate
conventions). Most decals are drawn with depth writes off (op 0x25) and colour only
(FBMSK 0xFF000000), before the reflection passes: the body's depth and alpha stay
under them, so the reflection over the body's vertices covers the decal as it covers
the paint around it (682 draws on 265 of the 286 models in `cars/day/`). The glass,
drawn the same way, leaves its depth: in its own draw (`ty2056`), or in a depth-only
pass (FBMSK 0xFFFFFFFF) over its vertices after its reflection (`nm0006`).

Programs leave ATE on with (GREATER, 0x20) and DATE off when they end (1581
of 1607 car/wheel streams), so that is the entry state the decoder assumes.

The game sets that state once per model draw and runs the nodes one after
another without resetting it, so a node's state changes stay in force for the
nodes after it (a node called with op 02 / 03 starts from the entry state
again). The decoder starts every node from the entry state; with
`gtm1::Options::carry_state` it carries the state from node to node instead
(the licence course line, course slot 27, sets its state in a node of its own:
[course.md](course.md) "Course line").

**What this means for car rendering:** body textures carry a *reflectivity*
in their alpha (texel alpha 0 = hole, otherwise 0x21..0x80). The opaque body
passes run with the alpha test (alpha > 0x20), which cuts the wheel arches,
grilles and so on out of flat side panels; without it the wheels are hidden
behind dark panels (the R34 front wheels, for example). The decoder applies the test as a binary-alpha copy of
the texture plus `kAlphaTest` (`Options::program_alpha_test`). The opaque
passes also write the texel alpha to the frame buffer, and the
reflection passes (normal in the UV slot, env map from a runtime slot) are
blended Cs·Ad + Cd: reflection weighted by that alpha.
`Options::weighted_reflection` turns these into `kAdditive | kEnvironmentMap |
kUnlit | kNoDepthWrite` primitives. A reflection triangle that an earlier pass drew
exactly (the same three corners) gets the texture and UVs of the last such pass, and
the renderer weights the reflection per pixel by its texel alpha. Where that pass
is alpha-tested, its failing texels write nothing on the GS, so the alpha that lay
there before stays: the primitive carries the test's reference and that earlier
alpha (`scene::Primitive::under_ref` / `under_alpha`; the triangle's own under it,
else the mean of its corners', in five steps), and the renderer weights texels at or
below the reference with it. In the wheel arches that is
usually 0, so the reflection doesn't haze over the wheels; in the windows the roof
and pillar passes cut out, it is the alpha-only shell's 0x80 (the Alto Works,
`sz0004`: issue 278). Otherwise, triangles whose three corners come from the same
textured pass get that pass's texture and UVs; the remaining triangles are
untextured with a per-vertex weight. Matching corners alone took a triangle that
spans two overlapping packets (the MR2 G-Limited's hood, `ty0076`) or a window cut
into a pass as that pass's own, which left flat patches and unreflecting glass.
Checked in a GS dump of the original's garage (Alto Works): the reflection over
its windows draws on the frame alpha the shell wrote. Glass is a nearly uniform dark texture
with GS alpha ≈ 0x73, drawn with normal blending and the alpha channel
write-masked, so it is ~90 % dark plus the reflection on top. The glass's own
texture is used as its reflectivity because the real Ad (whatever lies
behind it) is unknown, unless an alpha-only pass sets it.

Alpha-only passes (FBMSK 0x00FFFFFF; 69 draws on 62 models in `cars/day/`) are
drawn over the glass before its reflection: the frame alpha they write (texel ×
vertex alpha) is the weight of the reflection passes after them over the same
vertices (`ty2056`'s canopy: an untextured pass at 0x80, full reflectivity). The
decoder takes them as the weight and draws nothing for them. Depth-only passes
(FBMSK 0xFFFFFFFF with depth writes on; 101 draws on 73 models) write the depth of
a part drawn without depth writes, after its reflection (`nm0006`'s windows);
they are `scene::kDepthOnly` primitives, drawn in model order with the blended
passes.

Offsets in `06`/`07`/`08`/`09`/`0A` are relative to the address of the operand
field that holds them. Ops 0x0C and 0x10 (matrices) appear in a few cars only
(24 matrices in total, e.g. `ty0032`).

### Chunks

```
0x00 u32 0, u32 0
0x08 u16 flags        low byte: 0 = colour+UV (unlit), 1 = normal+UV (lit),
                      2 = colour + normal in the UV slot (reflection pass), 4 = specular
                      pass, 5 = shadow, 0x41 = morph (2 key frames); high byte bit 0 =
                      lit colour at scale 255 (below); kinds: course.md "Chunk flags"
0x0A u16 packet count
0x0C u16 total vertices
0x0E u16 total triangles
0x10 packet entries: u32 offset (from chunk start), u16 size in qwords, u16 mat
```
`mat`: bits 0-8 = texture index + 1 in the current texture set (0 = none,
0x1FF = runtime slot, e.g. the environment map on reflection passes and the
rim image on wheels); bits 9-15 = material index + 1 (0 = none, 0x7F =
runtime slot).

A wheel's texture-LOD tables (+0x34; one per level of detail, and per axle when GTTW flag
bit 0 is set: table level + 3 x axle) hold four entries: 0 a set shared by every table,
1 the rim image (its first texture; the race's runtime slot), 2 the **spinning** rim, a
softer image of the turning wheel (64 x 64 for `am0006`'s 128 x 128 rim), 3 unused (0) or
entry 0 again. 279 of the 286 car files' wheels have entry 2; a few wheels have a single set
and no table.

#### Materials

Materials are 0x50-byte records: four vec4 rows, ambient Ma (+0x00), diffuse
Md (+0x10), specular Ms (+0x20) and emission Me (+0x30); f32 shininess at
+0x40; u32 flags at +0x44; +0x48 a relocated pointer (127.0f in the per-colour
sets; unused). Flag bit 0 marks **lamp** materials: the game multiplies
their rows by a global tint whose emission is (k/4, k/4, k/4) for k lamps on
(ambient, diffuse and specular × 1). It is not the body paint.

The array at +0x28 (count +0x14) is the base set. `+0x38` points to `u32
offsets[count at +0x1A]`, one full material array (same record count) per
paint colour. Selecting colour c replaces the active array with +0x38[c] when
c < count. The count equals the paint-variant count in 832 of 859 car files
and is 0 in the other 27; the base array is not one of the colour sets
(`am0006`: base 0x50, colour sets 0x200/0x390/0x520/0x6B0). The colour sets
carry the paint finish: diffuse 0.5-1.0, specular 0-3.0 (coloured on pearl
paints, e.g. `ho0099` (0.3, 0, 2.0)), shininess 0/8/16/20 (`am0006` body
material 2: metallic colours Md 0.5, Ms 1.1, shininess 8; silver Ms 1.5,
shininess 16; white Md 1, Ms 0).

Lighting of lit packets (normals, no colours) is OpenGL-style fixed function
on the VU, from the course's Env0 car lights (up to 4, Env0 +0xD0 + 0x80·i:
direction, ambient +0x10, diffuse +0x20, specular +0x30; enabled by Env0 +0x10
bit i) and the global ambient (Env0 +0xEF0): colour = Me + Ga·Ma + Σ La·Ma +
Σ max(N·L, 0)·Ld·Md + Σ max(N·H, 0)^p·Ls·Ms (infinite viewer), times 128
(255 when chunk header byte 9 bit 0 is set), clamped to 255. Textured packets
MODULATE the texel with it (0x80 = 1.0, so up to about 2× the texel);
untextured packets (TME off) show the byte as is (255 = 1.0), so a lit sum of
1.0 is mid grey at scale 128 and white at scale 255. Lights may have negative
colours (most day courses enable a second car light from below, e.g. direction
(0, −0.29, 0) with diffuse −0.21, which darkens the lower body: see
[course.md](course.md) Env0).
The exponent p is quantised from the shininess: < 6 → 4, 6-12 → 8, ≥ 12 → 16.
The specular term is used only when Ms and some light's specular are non-zero.

Reflection passes (texture field 0x1FF) sample a 256×256 sphere map of the
course that the race renders every frame from the focus car (0.8 m above it):
the course's small sky dome (slot 23) and its reflection scenery (slot 5), see
[course.md](course.md).
UVs: R = reflect(V, N) in view space, m = √(Rx² + Ry² + (Rz + 1)²),
s = 0.5 + 0.5·Rx/m, t = 0.5 − 0.5·Ry/m. Colour = vertex colour × (C0 + C1 ·
min(V̂·N, 0)) × K per channel, with C0 set by op 0x36 (f → (f, f, f, 1)) /
0x37 (vec4) and C1 by op 0x32 (f → (f, f, f, 0)) / 0x33 (vec4) (car data: C0 1,
C1 0.8: weight 0.2 head-on, 1.0 at grazing angles; prize models use coloured
rows, e.g. the gold cups C0 0.9, C1 (−0.5, −0.36, −0.26): a warm boost
head-on), K = 1 − (a per-car value, meaning open). The result is an 8-bit GS
colour (≤ 255, i.e. ≤ 255/128 of the texel after MODULATE). Blend ALPHA 0x58 (Cs·Ad + Cd) with the fog colour set to black; Ad is
the frame alpha the body pass wrote (texel alpha × lit alpha). A reflection pass
writes its own alpha too: the map is PSMCT16, cleared with alpha 0x80 and drawn
with the alpha masked, so every texel reads TEXA TA1 = 0x80, and with vertex alpha
0x80 the pass leaves Ad = 0x80 wherever it draws. Reflection chunks with the
destination-alpha test (op 0x1D; drawn with DATM 0) draw only where Ad < 0x80:
not over an earlier reflection, and not on texels at the full reflectivity 0x80
(the body textures keep their reflective texels at 0x7F or below; some trim
textures are 0x80 throughout). Checked in a GS dump of the GT-One race car
(`ty2056`) at Super Speedway: DATE on, DATM 0 on its tested reflection draws, TEXA
(TA0 0, TA1 0x80), vertex alpha 0x80 on every reflection draw. Tested passes carry
most of the reflection: 126,513 of 175,610 reflection triangles on the 124 models
of `cars/day/` with alpha-only or depth-only passes. The renderer
discards a tested pass's fragments where the weight reaches 0x80
(`scene::kDestAlphaTest`); of the earlier reflections, the decoder drops the
triangles a tested pass repeats exactly (0.2 % of them) and does not model
partial overlaps.

Specular overlays: blended (PRIM.ABE) packets that are not reflection passes but
are drawn with ALPHA 0x58 add the same way, Cs·Ad + Cd over the body pass below.
Besides the chunk kind 4 glints, ten models (day / eve / night: 787B, Calsonic
Skyline, Escudo Pikes Peak, Castrol Mugen NSX, S2000 LM, Nismo GT-R LM road car,
Cobra, both Griffith 500s, MR-S S Edition) have kind 1 lit textured overlays with a
specular-only material (Ma = Md = 0, Ms 1, the MR-S 0.1-0.5; shininess 16 / 20) on
the same vertices as a body pass: headlamp lenses, grille mesh, deck panels. Lit by
the specular term alone they are black away from the highlight, so drawn with
normal blending they would black out the part; added, they only put the highlight
on it.

### VIF packets

Each packet is a VIF stream of UNPACKs (always `usn=1 flg=1`, i.e. double
buffered relative to TOPS) followed by NOP padding:

| UNPACK | address | content |
|---|---|---|
| V3-32, num 1 | 0xC0 | GIF tag (3 words): NLOOP = vertex count, EOP, PRE=1, PRIM, PACKED, NREG=3, REGS = ST, RGBAQ, XYZF2 |
| V3-32 | 0x00 | positions (floats) |
| V3-32 | 0x20 | second key frame positions (morph chunks only) |
| S-8 | 0x40 | strip table: count, then per strip (vertices - 2) × 3 |
| V3-32 | 0x80 | normals (lit chunks) |
| V4-8 | 0x80 | colours, GS scale (0x80 = 1.0) |
| V3-32 | 0xA0 | second key frame normals |
| V2-32, masked | 0x40 | S,T texture coordinates (floats, Q = 1, origin top-left) |
| V3-32, masked | 0x40 | normal used for the environment map (reflection chunks) |
| V2-32, masked | 0x60 | second key frame S,T |

PRIM is a triangle strip with Gouraud shading; TME, FGE and ABE vary
(`0x3C`, `0x7C` with alpha blending, `0x6C` untextured with blending, and so on).
Strips use the usual alternating winding: triangle *i* is (v[i], v[i+1],
v[i+2]) for even *i* and (v[i+1], v[i], v[i+2]) for odd *i*. These triangles are
counter-clockwise when seen from the side the vertex normals point to
(1825 of 1842 triangles agree on `am0006`; the rest are degenerate stitches).
Each VIF packet stays under about 0x80 vertices, so that it fits in double-buffered VU1 memory.

## Textures

Texture sets are ordinary Tex1 sets (see FORMATS.md). Car body sets use 4-bit
textures whose CLUTs are selected with **TEX0.CSA** (entry offset in units of
16 in the CSM1 layout): palette entry *i* is at x = (i & 7) + (CSA & 1)·8,
y = (i >> 3) + (CSA >> 1)·2 of the 32-bit CLUT rect at CBP (width 1).
`GsMemory::decode_rgba` ignores CSA, which made painted panels decode grey.
`gtm1::decode_texture_set` has its own CSA-aware path for indexed textures with
32-bit CLUTs.

**Paint.** Tex1 +0x20 is an offset (from the Tex1 start, 0 = none) to
`u32 V; u32 variant_offsets[V]`, each variant `u32 n` (entry count in the low
16 bits) followed by n × u32 entries. Each entry patches three TEX0 fields of
one texture record: bits 0-4 CSA, bits 5-18 CBP (relative to the set's base,
the u16 at Tex1 +0x10, 0 on disc), bits 19-22 CPSM (0 = CT32, 2 = CT16), bits
23-31 the index of the texture record (0x28-byte records at Tex1 +0x18) that
is patched. Entries do not have to follow texture order: 1,550 of 13,909
variants (about 11 cars per time of day, e.g. `ho1086`) skip textures, and
1,044 entries switch the CLUT between CT32 and CT16 (e.g. `ar0006`; the GS widens a CT16
entry's 5-bit channels by a shift alone, low bits 0: 31 -> 248, as for every 16-bit texel and
CLUT). Variant 0
holds the TEX0 values. All CLUTs are already in the uploaded buffers, so
painting is just choosing CBP/CPSM/CSA. V equals the car's colour
count in `database/carcolor.db` (verified for 275 of 287 cars, all LOD sets). For example,
`am0006` has 7 colours: British Racing Green, Derwent Green, Grasmere Blue,
Braemar Blue, Solent Silver, Malham White and Sutherland Red. Car textures are
4-bit (all but about 30), TCC 1, TFX MODULATE; the carcolor.db RGB never reaches
the renderer (UI swatches and opponent colour picks only). The paint finish
(solid, metallic, pearl) is in the per-colour material sets (see Materials). Textures that don't take
paint (glass, lamps) have the same entry in every variant. `carcolor.db`
("GT2K") is a 288-entry index keyed by `id_hash(model id)` → colour ids →
(name string ids, representative 0x00BBGGRR RGB used for UI). The decoder
takes a colour *index* (`CarModelOptions::paint`, `gt3tool model --paint=N`).

The decoder always decodes the full power-of-two size because the UVs address
the whole texture, not the REGION_CLAMP rectangle.

## Coordinate system

+Y up, the **nose towards -Z**, +X to the car's right. The origin is the
wheelbase midpoint on the ground (y = 0), and the body is symmetric in X. The
physics puts the front axle at -wheelbase/2, the GTCI front lamps are at
-Z, and rear wings sit at +Z. Example (`am0006`, Aston Martin V8 Vantage):
x ±0.98, y 0..1.19, z -2.30..2.42 m.

## Wheel placement (`wheel_spec.*`)

The values come from the parameter database (`database/paramdb*.db`, via the
car that uses the model):

| value | source |
|---|---|
| wheelbase | table 3 (`ch_`) u16 mm at +0x18 |
| front / rear tread | table 5 (`rm_`) u16 mm at +0x32 / +0x34 |
| front / rear tread offset | table 5 (`rm_`) s8 mm at +0x3D / +0x3E (per side) |
| tyre size | tables 28/29 (`ft_`/`rt_`) u64 size id at +0x18 → table 25 (`sz…`): u8 +0x08 rim inches, +0x09 aspect/5, +0x0A width/5 mm |

width W = w mm, sidewall sw = W · aspect %, outer radius R = rim · 25.4 mm / 2 + sw.
The track (hub to hub) is the tread less twice the offset: the game draws each
hub the offset inboard of half the tread, in the menus and in races. 71 of
the 244 cars have an offset (front −29..40 mm, rear 0..40 mm); the SLK 230's
1488 / 1485 mm treads with 35 / 20 mm give 1418 / 1445 mm.
Wheel *i*: axle i >> 1, side i & 1 (left = -X). Centre = (±track/2, R,
∓wheelbase/2) with the front at -Z. The rim model is scaled by R - 0.75·sw and
offset by -W/2 along X (outer face at the outer sidewall). Right wheels are
rotated 180° about Y. Example `am0006`: wheelbase 2.611, track 1.548/1.566 (no
offset), 285/45R18 → R 0.357 m, W 0.285 m. When no database entry exists, the
loader estimates from the body bounds.

The decoder's wheel centres (y = R over a body whose lowest point is at y = 0)
are a static layout. GT3 itself puts the model origin at the physics body
height, which at rest is the ride height q (spec 0x5C × 0.001 m) above the
ground, and draws each wheel at y = R − q − (x′ − x_s) in model space (x′ the
suspension coordinate, x_s its static value; compression raises the wheel),
x = the signed hub half-track ((tread − 2·offset) × 0.0005 m, shrunk by the
swing arm spec 0x38/0x39 as the wheel moves), z = ∓wheelbase/2. Its showroom
uses x′ = x_s and lifts the body by q so the tyres touch the floor. See
docs/dev/RACE.md "Car placement".

## Tyres (`tyre_model.*`)

The game builds each tyre around its hub from R, W and the rim radius
b = R − 0.75·sw, in the wheel's frame (axle along X, the outer face towards −X
for a left wheel; right wheels turn half a turn about Y like the rim). Angle
index i of N gives θ = 2πi/N and a point (x, r sin θ, r cos θ).

| Detail | Segments N | Shoulder | Inner faces | Band | Outer face |
|---|---|---|---|---|---|
| menus (showroom) | 30 | yes | yes | yes | 1 mm inboard of the rim face |
| race level 0 | 18 | yes | yes | yes | 4 mm outboard of it |
| race level 1 | 12 | yes | yes | no | 4 mm outboard |
| race level 2 | 8 | no | no | no | 4 mm outboard |

(The race picks its level from the tyre's size on screen; a farther level 3,
a black hexagonal drum, and level 2's hexagon in place of the rim are not
reproduced; GT3 Revisited uses the body's level.)

- **Textured pieces**, lit with the tyre material (ambient 0.3, diffuse 0.15,
  specular 0.9, shininess GTTR +0x18), the texture modulated, repeating round
  the tyre and clamped across, **without mipmaps**:
  - the tread at r = R, from the shoulder (x_o + h) to the inner face (+W/2);
  - the shoulder, a 45° chamfer from (x_o, R − h) to (x_o + h, R), with
    h = k (R − b), k = 0.25 (0.3 for a symmetric tyre);
  - the outer sidewall, a flat ring at x_o = −W/2 + the inset, from b to R − h.

  Without a shoulder the tread starts at x_o and the sidewall reaches R.
  Texture T (0 = the image's top): tread 0.5 at the shoulder → 0 at the inner
  edge, shoulder 0.625 → 0.5, sidewall 1.0 at the rim → 0.625 (without a
  shoulder both meeting edges take 1 − 0.375/(1 − k)). S = φ + i·repeats/N,
  running up round a right wheel's tread and down round a left one's, down
  round every sidewall; φ is drawn at random per tyre. Normals across the
  profile (x, radial): tread (−0.383, 0.924) at the shoulder edge and (0, 1)
  at the inner edge; shoulder (−0.924, 0.383) → (−0.383, 0.924); sidewall
  (−0.707, −0.707) at the rim → (−0.924, 0.383) (without a shoulder 0.707s).
- **Black, unlit faces**: a disc closing the inner face (radius R) and, with
  the inner faces, the same disc facing out (seen between the rim's spokes)
  and the tread seen from inside.
- **Symmetric tyre** (flag bit 1): the shoulder and sidewall are mirrored on
  the inner side too (the tread stops at +W/2 − h), the disc reaches only b,
  and there is no band.
- **Band** (the arch shade): half a ring over the top, i = 0..N/2, at
  y = R sin θ + 0.003, z = √(R² + W²/4) cos θ, from x = −W/2 + 0.02 (alpha 0)
  to +W/2 + 0.02 (alpha A). A = 256 − ⌊(i − s)² · 256 / s²⌋ (s = N/4 + 1,
  whole numbers; 0 at both ends; clamped to 0..255). Black, blended
  Cd · (1 − A/128) and clamped, so it is fully black where A passes 128;
  depth tested, no depth write. It does not turn with the wheel.

For the SLK 230's front 205/55R16 in the menus: R 0.316, b 0.231, h 0.021;
the outer face at −0.1015 m from the hub.

**Showroom wheels in races** (ours; `[video] showroom_wheels`): race level 0
takes the menus' wheel in place of the race's: the `menu/cars` file's rim
(GTTW) and tyre texture (GTTR, 256x128), or a fitted wheel's
`menu/wheel/<label>` (its race `wheel/<label>` when there is none), with the
menus' tyre detail. Levels 1 and 2 keep the race's. Placement is the same for
both, so the menus' wheel sits where the race's would
(`CarModelOptions::showroom_car`, `showroom_wheel`). Whatever the setting, a rim
that draws nothing at its full detail (a modified game's wheel that only the
other kind of model has) has the other stand in at its full detail at every
level: the menus' in races, the race's in the menus
(`CarModelOptions::stand_in_rim`, `race_rim`, `menu_rim`). GT3's own race rims
draw nothing at the lowest level; those stay empty. Across the 286 cars of
NTSC-U 1.10 the menus' rims have a median of 1,413 triangles against 112 for
race level 0: about 5,200 more triangles a car with four wheels. GT3's rim
blur (wheel spin blur, below) is made for a flat rim's image: the races blur a
modelled rim with turned copies of it instead (docs/dev/RACE.md). The menus' and the race files' GTTR repeats,
flags and shininess are the same on every car, so the tread blur reads the
menus' tyre as it does the race's.

The menus' rims carry their light in the image: they are drawn with ambient 1,
diffuse 1 and no specular on nearly flat faces, and most stock rims have one side
of the spokes painted brighter. The menus never turn a rim, so that light stays
put; in races the rim turns and the painted light would turn with it. Races
(ours, `CarModelOptions::even_menu_rim_light`, `formats/model/rim_light.h`) take a
menus' rim with its images evened out round the axis: each texel's place on the
rim comes from the triangles drawing it (its position in their UVs carried to
their corners, so the axis's place in the image is wherever the layout puts it,
usually the image's centre), and on each ring of texels one texel wide the first
angular harmonic of each channel (one bright side) is divided out, fitted with
the rings two either side and alpha-weighted, keeping the ring's mean, the spokes
and the alpha. Texels a triangle samples as a single spot (a spoke's side or the
barrel in one flat colour), texels two triangles put at different places, rings
within 2 texels of the axis and rings covered on one side only are left as they
are. Measured as the first harmonic of luminance against each ring's mean, over
the mean luminance, NTSC-U 1.10's 287 stock rims go from 0.044 to 0.003 on
average (277 at or under 0.015) and the wheel shop's 181 from 0.015 to 0.002. A
rim standing still in a race looks a little flatter than in the menus.

## Brake parts (menus)

A menu car shows its own brake disc and caliper through the spokes: body
node 2 on the front wheels, node 3 on the rear, drawn in the wheel's frame
right after the rim, with the rim's placement (scaled by b = R − 0.75·sw, at
x = −W/2). They turn and steer with the wheel. A right wheel's parts are
mirrored in z before its half turn about Y, so they are the left wheel's
mirrored in x about the hub (the caliper at the same end of the car on both
sides), whether or not the wheel mirrors its rim (GTTW bit 2). The game skips
them while the camera is inboard of the rim face, where the tyre's inner disc
hides them anyway; ours always draws them (`CarModelOptions::menu_tyres`:
meshes `wheel_<x>_brake`). Races draw no body nodes 2 and 3: their brake
parts come from `race/brake.bin` instead (below).

## Brake parts (races, `brake_model.*`)

In a race the game draws brake parts inside each wheel, seen between the spokes: a rotor
image, a caliper image and, when hot, the rotor's glow. The menus' wheels have none of these:
they show the car file's own brake geometry, nodes 2 and 3 (above).

`race/brake.bin` ("GTBR", 45,488 bytes; loaded before every race):

```
0x00 "GTBR"   0x04 u32 0 (relocation base at run time)   0x0C u32 file size
0x10 u32 -> Tex1 set (0x460: 38 textures)
0x14 u32 first rotor texture (1)   0x18 u32 first caliper texture (8)   0x1C u32 glow texture (0)
0x20 u32 -> rotor table (0x30): 7 x 16 bytes, u32 flags (bit 0: the rotor glows; 0 on record 0), 12 bytes 0
0x24 u32 -> caliper table (0xA0): 30 x 32 bytes, f32 radial half size, tangential half size,
            radial centre, tangential centre (in brake radii, e.g. 0.4 / 0.85 / 0.75 / 0), 16 bytes 0
```
Textures (T4 / T8, REGION_CLAMP, no mipmaps, bilinear): 0 an orange ring on black (64 x
64, the glow), 1-7 rotor faces (64 x 64; 7 is 32 x 32), 8-37 calipers in silver, red, gold,
blue and black, some with makers' marks (32 x 32), each with a soft drop shadow at half
alpha.

Per wheel, in the wheel's frame turned by its steering and camber but not by its spin (the
game counter-rotates the parts by the spin, so they stay put; a right wheel's image is the
left one's mirrored through the car's centre plane), with r and the offset from the axle's
record, y up and z along the car (+ backwards):

- **Rotor**: a square of side 2r on the hub, at the offset; S = 0.5 + 0.5 z / r,
  T = 0.5 - 0.5 y / r. Alpha tested GREATER 0x40, opaque.
- **Caliper** (when the record has one), 5 mm outward of the rotor: the corners
  (y0, z0) = (tc + th yc, rc + rh zc) r for (yc, zc) = (±1, ±1) (radial half size rh,
  tangential th, centres rc, tc from its table record), turned by the record's angle a:
  y = y0 cos a - z0 sin a, z = y0 sin a + z0 cos a; S = 0.5 - 0.5 yc, T = 0.5 - 0.5 zc (the
  image's top at the outer edge). Blended (Cs - Cd) As + Cd, alpha tested GREATER 0x7F with
  failing fragments still blended but writing no depth (the shadow).
- Both lit with the tyre material (above) and facing out of the wheel; drawn after the
  tyre's textured pieces and before its black faces and the rim.
- **Glow** (rotor table bit 0): a square of the glow texture over the rotor, 1 mm outward of
  it, turning with the wheel; added unlit and unfogged, Cs x FIX + Cd with FIX =
  trunc(clamp(128 h^2 g, 0, 255)) / 128, h the wheel's brake heat (0..1, a physics output) and
  g the course's Env0 +0x4C (1 on most courses, 1.7 or 2 on some, 0 on one); no depth writes.

The parts are drawn on a wheel's two nearest levels of detail (below), and only when the eye
is outboard of the wheel's outer face. The glow is part of the tyre's black faces and follows
their level only.

**Wheel detail level.** The game picks each wheel's level from its size on screen:
s = the projection's x scale x sqrt(R^2 + W^2 / 4) / the hub's depth; level 0 above 0.09,
1 above 0.03, 2 above 0.015, else 3 (a drum). (The tyre's segments, its level-2 hexagon and
the drum follow it in the game; GT3 Revisited keeps the body's level for the geometry.)

## Wheel spin blur

In the race's main view (not the 2P halves or the mirrors), the game blurs spinning wheels by
their turn over part of the last frame:

- The **turn**: d = -(the wheel's spin over its last step) x b x exp(f m), with b and f the
  course's Env0 +0xEA0 / +0xEA4 (0.8 and -20 on every course) and m the distance the hub's
  centre moved on screen (normalised device coordinates, x and y) since the car was last
  drawn in that view. A wheel the camera follows is blurred over 80 % of its step; one
  crossing the screen hardly at all.
- **Tyre** (the camera's car only; level 0 or 1; the GTTR flags ask for it: bit 0, and when the
  tyre is not symmetric (bit 1 clear) only while the outer face is in view): the 128 x 64 tyre
  texture is copied, then blended three times at 50 % with itself shifted along S by
  trunc(d / 2 x repeats x 1024 / pi / 2^k) / 16 texels (k = 1, 2, 3; wrapped at 128 texels;
  the turn negated on right wheels): the mean of 8 copies spread over half the step's travel.
  59 car files have bit 0.
- **Rim** (the outer face in view, level 0..2): on the camera's car, when d is not 0, the rim
  image (scaled to a square of min(width, 64) texels) becomes the mean of 16 copies turned
  about the image centre by every sum of a subset of d / 2, d / 4, d / 8, d / 16 (negated on
  right wheels), alpha included; the rim then draws with it as usual (alpha tested GREATER
  0x20, so a ring the spokes cover a quarter of turns solid). The other cars' rims show the
  wheel's spinning image (texture-LOD table entry 2) when |d| passes 3 degrees.

## Callback morphs (mud flaps, aerials, steering)

Seven cars draw parts with op 0x06 ids 1-5 followed by op 0x35 `base, count`: that id's value,
clamped to 0..1, is the phase: chunk base + min(phase x count, count - 1), morphed towards its
second key frame by the fraction. The game's car code sets the values every frame:

- ids 1-4: the four wheels' flap pendulums (FL, FR, RL, RR), 0.5 + 0.5 x. x in [-1, 1] is a damped
  spring (stiffness 100, damping 5 per unit) driven by 5 x the longitudinal acceleration and by
  random kicks: 2 % of steps (scaled by the room left to swing) add (1 - x) 5 u (u the hub's
  forward speed), and on a surface whose tyre record is not tarmac's, with the wheel loaded and
  rolling forward, another 4 % add (1 - x) 20 w (w the rim speed). It stops at +-1.
- id 5: the steering, 0.5 + 0.5 x steering angle / full lock (0.5 = straight ahead).
- Before a race sets them (the menus): 0 for ids 1-4, 0.5 for id 5.

| model | car | ids (LOD 0; LOD 1 alike) | part |
|---|---|---|---|
| la0008 | Stratos Rally Car | 4 | a rear mud flap (its other morph draw keeps a fixed op 0x34 phase of 0) |
| la0011 | Delta HF Integrale Rally Car | 3, 4 | the rear mud flaps |
| ty0032 | Celica Rally Car | 1, 2, 3, 4 | the four mud flaps |
| ma0095 | 787B | 1, 2 | aerials |
| ma0099 | RX-7 LM Race Car | 3, 4 | aerials |
| ml0003 | F090/S | 5 | the driver's arms and steering wheel |
| pd0010 | Sample 010 | 5 | as ml0003 |

Car models' +0x3C animators (type 7, mostly without targets) are never advanced by the game.

Ours: `gtm1::RunParams::callback` (op 0x06 ids 1-5), `gtm1::MorphDraws` (`Options::morphs`,
`CarModelOptions::morphs`: the draws left out of the body), the race's flap pendulums
(physics §8.8) as `sim::WheelState::flap` / `VehicleState::steer`, `race::car_callback_values` and
`race::CarVisual`'s morph models (one per drawing of the car, rebuilt when the values change).

## Open questions

- The rest of the chunk flag high byte, and the render-state ops (renderer domain).
- What changes geometrically between `day` and `eve`/`night` models.

