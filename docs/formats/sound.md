# Sound banks and the engine sound model

Code: `src/formats/sound/` (decoders), `src/audio/voice_mixer.*` (SPU2-style
mixer), `src/audio/spu_reverb.*` (SPU2 reverb), `src/audio/sequencer.*`
(sequenced music), `src/audio/engine_sound.*` (engine note model),
`src/app/screen_sound.cpp` (`gt3r --screen sound`), `src/app/screen_jukebox.cpp`
(`gt3r --screen jukebox`), `tools/gt3tool/cmd_sound.cpp`,
`tests/test_sound.cpp`, `tests/test_sound_seq.cpp`. The sequencer and the
SPU2 model are described in the headers of `src/audio/sequencer.h`,
`src/audio/voice_mixer.h` and `src/audio/spu_reverb.h`.

All game sound is SPU2 sample data (PS-ADPCM, 16-byte frames, 28 samples
each) driven with SPU2-style register values:

- **pitch** u16 in the game's units: `0x1000` = 44.1 kHz. Every value in
  the files and all of the game's pitch math use these units; the game
  converts them to SPU2 pitch (`0x1000` = 48 kHz) as `pitch * 0xEB3 >> 12` (0xEB3/0x1000 = 44100/48000).
  The hardware limits the step to `0x4000`. (Before this was found, our
  mixer treated game pitch as 48 kHz units, so everything played 1.47
  semitones sharp; renders of the sequenced music now land on the A440
  equal-tempered grid within 2 cents, which confirms the conversion.)
- **volume** s16, `0x3FFF` = full scale.
- **ADSR1/ADSR2**: the SPU2 envelope registers.
- frame flags (byte 1): bit 0 end, bit 1 repeat, bit 2 loop start. A
  loop-start frame latches the loop address; after an end frame the voice
  jumps there if repeat is set, otherwise it stops. The ADPCM predictor
  history runs on across the jump; the data is authored for that (loop
  seams measured smooth, see below), so decode loops that way.

## Inventory (`gt3tool sound-survey`)

| Path | Magic | Count | What |
|---|---|---|---|
| `sound/engine/<id>` | `ENGN` | 135 | engine ("mechanical") bank per engine sound id |
| `sound/normal0..3/<id>`, `sound/turbo0..3/<id>` | `ENGN` | 8 x 134 | exhaust bank per id and variant |
| `sound/rn_01.es` | `ENGN` | 1 | 3 layers (2500/5000/7500); loaded with the SE banks: the wind / road noise unit (below) |
| `sound/gt3sys.ins`, `gt3se01.ins`, `gt3se02.ins` | `INST` | 3 | menu sounds, race effects, ambience (named by `se.inf`) |
| `sound/menu.ins`, `sys.ins`, `se01.ins` | `INST` | 3 | not used by the game; older revisions (`sys`, `se01` share samples with `gt3sys`/`gt3se01`) and a standalone `menu` bank |
| `sound/se.inf` | `GTSE` | 1 | effect name index |
| `music/music.ins` | `INST` | 1 | instruments for the sequenced music (214 regions, 110 programs) |
| `music/music.inf`, `music/music.seq` | `MSEQ`, `SEQG` | 2 | sequenced menu music: index (18 names) and 9 sequences |

Coverage: ENGN 1209/1209 banks (8429 layers) and INST 7/7 banks (339
regions) parse and decode; every ENGN layer is exactly one looped sample.
`sound/engine/3406` (4-digit name) exists only in `engine/` and is not
reachable through the `%05d` path format. There are no `.sdb`/`.sys`/`.vag`
sound files (`.sdb` is the database, `icon.sys` the memory card icon).
`race/brake.bin` is **not sound**: magic `GTBR`, it contains a `Tex1` texture
set at 0x460 (graphics; for the car/renderer workstreams).

## ENGN engine bank ✅

```
0x00 "ENGN"
0x04 u32 0            relocation base (the game writes its load address)
0x08 u32 header size  = data offset = 0x30 + 16 * count
0x0C u32 0            SPU address (written when uploaded to SPU RAM)
0x10 u32 data size    (data offset + data size = file size)
0x14 u32 data offset
0x18 u32 layer count  (3..9; distribution 3:1 4:29 5:80 6:273 7:388 8:386 9:52)
0x1C u32 layer table offset (0x30)
0x20 u32 0, 0x24 u32 0   second table slot, unused (the loader relocates both)
0x28 u32 127, 0x2C u32 127   unknown, constant in all files
0x30 layer[count], 16 bytes:
     u16 base_rpm   RPM of the recording (500..9768, strictly increasing)
     s16 lo_rpm     first layer: 0; others: base_rpm
     s16 hi_rpm     last layer: 32767; others: base_rpm
     s16 volume     1290..16383
     u32 pitch      game pitch that reproduces base_rpm (0x210..0x1174;
                    0x1000 = 44.1 kHz)
     u32 offset     sample start relative to the data
```

Each layer is one SPU sample: an all-zero frame with flags 0, a loop-start
frame (flags 6, usually frame 2), frames with flags 2, and a final frame
with flags 3. Loops are 2268..63672 samples (median 14588), 0.09..2.5 s at
the layer's rate. Decoding a layer at `pitch * 44100 / 4096` Hz gives a
consistent engine order across all layers of a bank: e.g. for
`normal0/00001` the dominant harmonic is 1.72 x rpm/60 at every layer
(measured as 1.87 when the rate was wrongly taken as 48 kHz units), which
confirms the meaning of `pitch` and `base_rpm`.

Duplicates: `normal_i` and `turbo_i` are byte-identical for many ids, and 44
ids use the same file for all eight variants; no `engine/` file equals an
exhaust file. 21 distinct duplicate patterns over the 135 ids.

## Engine sound model (how the game plays ENGN) — `EngineSound`

How the game plays them:

- **Bank selection.** A car plays two ENGN banks: `sound/engine/%05d` and
  `sound/%s/%05d` where `%s` is `normal0..3` / `turbo0..3`. The id is a u16
  at +0x16 of the car's engine part record (parts table 6). The variant is
  the byte at +0x10 of another part record (parts table 20, presumably the
  muffler: 0..3) plus 4 when the turbo part record (table 12) has a non-zero
  byte at +0x11. Table numbers are the database part table indices
  ([database.md](database.md)).
- **Layer player** (one per bank, two voices, ticked once per game frame;
  the voices are keyed with the reverb send on):
  - rpm is clamped to >= 200;
  - if `lo <= rpm <= hi` for a layer, that layer plays alone; otherwise
    the two layers around the rpm play (`prev.hi < rpm < next.lo`);
  - crossfade position `x = (rpm - lower.hi) * 0x4000 / (upper.lo - lower.hi)`
    for the upper layer and the mirror for the lower one; gain =
    `table[x >> 4]`, a 1025-entry table equal to `4096 * sin(i/1024 * pi/2)`
    (equal-power crossfade);
  - voice pitch = `layer.pitch * rpm / layer.base_rpm`, then
    `* pitch_scale >> 12` (pitch_scale carries Doppler: (c + v_listener) /
    (c + v_source)), min 1; while a voice keeps playing its pitch may change
    by at most x2 / x0.5 per tick;
  - voice volume = `((gain * 0x3FFF >> 12) * layer.volume >> 14) * bank_vol
    >> 14` per side; while playing it changes by at most 0x1000 per tick;
  - a voice that changes layer is keyed off and a new one keyed on at the
    layer's sample start with ADSR1 `0x000F`, ADSR2 `0x0003`.
- **Bank volumes.** Two per-car levels are updated every tick as
  `level = (level + target) / 2` (integers) with
  - exhaust: `target = min(127, (1 + 3 * t) * 32 * K1)`
  - engine: `target = min(127, (|t - 0.375| + 0.375) * 127 * k * K2)`,
    `k = 1`, or `0.5` while a per-car state flag is clear, and
    `K1 = K2 = 0.13` at run time (so levels stay within 0..16),
  where `t = min(input, 1)` is a per-car float in 0..1. The bank volume is
  `level / 64 * camera_gain[view][bank]` placed in 3D (constant-power pan,
  distance and Doppler from listener and car positions; below).
- **Camera gains** (`{engine, exhaust}` per view, from a table in the
  disc's CORE.GT3): view 0 `{3.0, 2.5}`, 1 `{0.3, 1.3}`, 2 `{1.0, 3.0}`,
  3 `{0.3, 1.3}`, 4 `{0.5, 3.3}`. Views 0/1 are one camera type for the
  player's own / another car, 2/3 a second type, 4 everything else.

- **Stereo placement** (per engine unit, every frame): with the source at
  `(x, z)` in listener space and `d = sqrt(x^2 + z^2)` (pushed out to at
  least 0.25), `angle = clamp((x/d + 1) * pi/4, 0, pi/2)`, left gain
  `cos(angle)`, right gain `sin(angle)` (constant power, 0.7071 each at the
  centre). The overall gain is `min(1, 32/d)` times the unit's input gain.
  Surround cue: two per-source flags negate the left / right volume
  (phase inversion). In front (the game's listener-space z < 0, i.e. ahead)
  the flags are kept equal (`x < 0`: R takes L's flag; `x >= 0`: L takes
  R's); behind they are kept opposite (`x < 0`: R = not L; `x >= 0`: L =
  not R). So a source behind has its channels out of phase, which a matrix
  surround decoder steers to the rear. The same rule is applied to the
  per-car effect slots. Ours: `SurroundCue`, enabled with
  `VoiceMixer::set_surround_cue` (config `[audio] surround_cue`, off by
  default) for `EngineSound::set_position` / `EngineUnit::update_at`.

Approximated in our implementation:

- `t` is taken to be the throttle (it behaves like one: the exhaust grows
  4x with it while the engine bank dips at part throttle and is loud when
  coasting), and the state flag is assumed set ("drive engaged").
- The labels of the five views are guesses; the sound test uses view 2.
- The pan is an input (`EngineSound::set_pan` or `set_position`, from the
  race code); the surround cue is optional and off by default (on plain
  stereo it smears the image).

## Race car sounds besides the engine (`car_sound_model.h`, `EngineUnit`)

Every frame the race fills per car a set of unit and effect inputs
(position in listener space, gain, pitch factor or rpm) that the sound
object places with the constant-power pan above (distance gain
`min(1, 32/d)` for units and loops; one-shots use `32/d` unclamped). Gains
are multiplied by the per-camera channel gain table (`race/car_sfx.cpp`
`sfx_gain`). The tuning values come from a static parameter block in the
disc's CORE.GT3 (bytes `B[i]`, read once at race start):

| Quantity | Formula | Value |
|---|---|---|
| wind coefficient | `B[0xE]/10 * (B[0xF]/1000)^2` | 0.001 |
| wind speed ramp | `B[0x10]`, `B[0x11]` x 10 km/h | 10..450 km/h |
| wind rpm ramp | `B[0x12]`, `B[0x13]` x 100 | 2500..7500 |
| wind gain cap | `B[0x14]/10` | 0.5 |
| in-car coefficient | `B[0xE]/10 * (B[0x16]/1000)^2` | 0.004 |
| in-car speed ramp | `B[0x17]`, `B[0x18]` x 10 km/h | 10..320 km/h |
| in-car pitch ramp | `B[0x19]/100`, `B[0x1A]/100` | 0.9..1.5 |
| in-car gain cap | `B[0x1B]/10` | 2.0 |
| whine gain ramp | `B[0]/100`, `B[1]/100` | 0.3..1.0 |
| whine torque cap | `B[2]` x 9.8 | 490 N m |
| whine pitch ramp | `B[4]/10` .. `B[3]/10` over 0..10000 rpm (1047.2 rad/s) | 0.3..3.5 |
| engine / exhaust level factors K2 / K1 | `B[0x1C]/100`, `B[0x1D]/100` | 0.13, 0.13 |
| turbo level scale | `B[0x1E]/100` | 0.2 |
| blow-off scale | `B[0x1F]/10` | 2.5 |

- **Wind / road noise** = engine unit 2, the shared bank `sound/rn_01.es`
  (3 layers recorded at 2500/5000/7500 rpm), channel "wind". Its rpm input
  is a ramp of |forward speed u|: 2500 below 10 km/h, 7500 above 450
  km/h, linear between. Gain `min(0.001 * C_d * u^2 * (1 - y), 0.5)`, with
  `C_d` the car's drag coefficient (body record +0x39 / 100) and `y` the
  filtered slipstream factor (drafting quietens the wind). The rpm defaults
  to 2500 when the wind unit is not updated.
- **In-car** loop (se.inf `in_car`, channel "in-car", only audible from the
  in-car view): gain `min(0.004 * C_d * u^2 * (1 - y), 2.0)`, pitch factor
  0.9 at 10 km/h rising linearly to 1.5 at 320 km/h.
- **Transmission whine** (se.inf `turbine2`, channel "whine"): only for cars
  whose gear part (parts table 17) has a non-zero byte +0x10 (a non-stock
  gearbox), and only while the clutch state is not "disengaged". Gain
  `0.3 + 0.7 * min(|torque|, 490) * ratio[gear] / (490 * ratio[1])`; pitch
  factor `0.3 + 3.2 * |shaft| / 1047.2` with `shaft` the driven axle's
  angular speed (rear, front or the 4WD mix by drive layout).
- **Turbo spool** (se.inf `turbine`, channel "turbo", only heard in the
  car-mounted views of the camera's target: your cockpit, a replay's car mounts): turbo cars only. Each physics step, while
  the previous boost load is above 0.75 the raw level is
  `clamp(int(128 * v^2), 1, 127)` with `v = (load - 0.75) * min(T/588, 1)`
  (T = engine torque, N m; level 1 if T <= 0); otherwise level 1. Right
  after each step (once per 60 Hz frame, before the sounds read it) the
  level is scaled by 0.2 in place. Gain level/128, pitch factor
  `max(stage-0 lag, stage-1 lag) / 6144` (the turbo lag states).
- **Blow-off** (se.inf `b_off` one-shot, channel "blow-off", in-car own
  view only): armed while the throttle is above 90 %; when the throttle
  drops under 75 % it fires once with strength `max(load - 0.8, 0) / 256`,
  scaled by 2.5, times the channel gain and `32/d`.
- **Other per-car slots**: four tyres x six surface loops
  ([sound §7.6](../behaviour/sound.md#76-tires)), `body_noiz` (wired but
  always gain 0), and one-shots `clash`, `clash2`, `clash3` by collision type.

`EngineUnit` plays the wind bank with the same layer player as
`EngineSound`; `car_sound::wind/in_car/whine` and `car_sound::Turbo` give
the gains and pitches; `LoopEffect` plays looping se.inf regions the way the
game's effect handles do (key on when audible, volume `gain * 0x4000 *
region volume >> 14`, pitch = region pitch x factor, key off at 0).

## Course ambience (CRSS, course file slot 35; `CourseAmbience`)

```
0x00 "CRSS", 0x04 u32 0, 0x08 u32 0, 0x0C u32 used size, 0x10 u32 1
0x14 u32 list_offset[5]          one list per ambience channel
list:    u32 count; u32 emitter_offset[count]
emitter: 0x00 u32 point count
         0x04..0x0F zero (0x0C = run-time gain)
         0x10 vec4 gate A, 0x20 vec4 gate B (w = 0; run-time track distance)
         0x30 point[count] = {x, y, z, weight}
```

Coverage: 75 of the 80 US course files have the section (none in the four
`L_circle*` license tests and `L_slalom80r4`); all parse (`sound_crss_on_disc`).

Channels: 0 `air`, 1 `city`, 2 unused (its effect handle is reused by
the tunnel loop and its result is forced to 1), 3 `crowds`, 4 `sea`
(loops in `sound/gt3se02.ins`). Emitters with both gates at the origin
(air, city, sea in practice) are always on. For gated ones (crowds along a
stretch of track) the two gate points are projected onto the road at load,
and the span is the shorter arc between them (it may wrap over the start
line); the target gain is the sum over all cars of `0.8^car_index` for each
car on the span, capped at 1. (In the game's ungated mode every emitter
starts and stays at gain 1.) Gains slew by 1/60 per frame.

Per listener and frame, each point contributes `w = weight * gain`, times
`32/dist` beyond 32 m; pan `p = (dot(dir, right) + 1) / 2`, left
`sin((1 - p) * pi/2) * w`, right `sin(p * pi/2) * w`; a Doppler offset
`c / (c - dot(dir, v_listener)) - 1` (c = 331.45 m/s), weighted by w and
averaged over the points and then the emitters. The channel's L/R are
clamped to 1 and multiplied by the SE master volume; the pitch is 1 + the
offset. With two listeners (split screen) the outputs add, both sides take
the larger value and the pitch is 1.

Tunnels: under each listener, road surface flags `& 0x18 == 0x08` turn on
the `tunnel` stereo pair (regions 2/3 of gt3se02), `0x10` turns on
`tunnel2` (4/5); `0x18` only raises the reverb
([sound §3.2](../behaviour/sound.md#32-race-reverb-ambience)).

## INST instrument bank ✅

```
0x00 "INST"
0x04 u32 0            relocation base (written at load)
0x08 u32 header size  (end of the program records; data offset rounds it up to 16)
0x0C u32 0            SPU address (written at load; region addresses are rebased)
0x10 u32 data size
0x14 u32 data offset
0x18 u32 region count,  0x1C u32 region table (0x28)
0x20 u32 program count, 0x24 u32 program offset table
region, 24 bytes:
  0x00 u32 sample address in 8-byte units from the data start
  0x04 u16 0
  0x06 s16 volume (0..0x4000)
  0x08 u16 center: center_note << 8 | fine (fine in 1/256 semitone); the
           sample plays at 44.1 kHz (game pitch 0x1000) when keyed at center
  0x0A u16 duration: one-shot length in 1/960 s at 44.1 kHz (0 for loops);
           only feeds the game's voice bookkeeping
  0x0C u8  voice-stealing priority (0 in every bank; behaviour sound §2.1)
  0x0D u8  pan: 0 left, 0x40 centre, 0x7F right (stereo pairs are L/R
           regions); the sequencer adds it to the channel and program pans
  0x0E u8  flags: bit 0 = reverb send (the voice command sets the SPU2
           VMIXEL/VMIXER switches). music.ins 179/214 regions, gt3sys 10/14,
           gt3se01 12/26, gt3se02 4/10 have it set
  0x0F u8  0
  0x10 u8  key_lo, 0x11 u8 key_hi (equal in the SE banks; ranges in menu/music)
  0x12 u8  pitch-bend range down, 0x13 u8 range up, in semitones (music.ins:
           0 or 2; gt3se01 up to 24/14)
  0x14 u16 ADSR1, 0x16 u16 ADSR2
program: u32 file offset -> { u8 count; u8 pan (0x40 neutral); s16 volume
         (Q14); u8 region[count] }, padded to 4 bytes
```

Every region address lands on a sample start (offset 0 or right after an
end-flag frame). Looping samples end with flags 3; one-shots with flag 1.

**Pitch.** The game converts a note to a pitch with
`pitch = 0x1000 * 2^((note*256 - center) / (12*256))`, evaluated through a
384-entry-per-octave table (1/32 semitone steps, `round(16384 * 2^(i/384))`,
then shifted per octave). Sound effects are keyed at `key_lo`, so an
effect's sample rate is `44100 * 2^((key_lo*256 - center) / 3072)`.
`sound::note_to_pitch` reproduces this bit-exactly.

**Pan law.** 128-entry table `round(16384 * sin(i/127 * pi/2))`;
left = `vol * t[127 - pan] >> 14`, right = `vol * t[pan] >> 14`.

## GTSE effect index (`sound/se.inf`) ✅

```
0x00 "GTSE", 0x04 u32 1, 0x08 u32 0, 0x0C u32 count (42)
0x10 { u32 name_offset; u32 value }[count], then NUL-terminated names
value = bank << 24 | first_region << 12 | last_region
```

Banks: 0 `sound/gt3sys.ins` (buy, cancel, cursor, ok, ...), 1
`sound/gt3se01.ins` (clash1-5, on/off_skill, on/off_bound, rumble, turbine,
rain_noiz, ...), 2 `sound/gt3se02.ins` (air, city, crowds, sea, tunnel).
How the game plays an effect (the generic sound effect, used by every menu
with volume 1.0 and pan 0x40): if first == last the region is keyed twice,
at pan 0 and pan 0x7F (hard left and hard right: centred at the region's
full level); otherwise only region `first` is keyed, at the given pan (the
second region of a pair is used only where the game asks for it, e.g. the
right halves of the stereo ambience loops). Volume = vol x 16384 x SE master
volume, clamped to 0x3FFF, x region volume >> 14; left/right by the sine pan
table. Earlier instances are not stopped. Menu sounds are skipped while the
options' menu SE switch is off.

## MSEQ music index (`music/music.inf`) ✅

```
0x00 "MSEQ", 0x04 u32 1, 0x08 u32 0 (relocation base), 0x0C u32 group count (18)
0x10 u32 group count again
0x14 { u32 record_offset; u32 record_count }[groups]   (every count is 1)
records, 20 bytes: u32 name, file, title, artist (string offsets),
                   u32 sequence index into music.seq
then NUL-terminated strings
```

| seq | title | names (the ids the game asks for) | loop | length |
|---|---|---|---|---|
| 0 | main-01 | mini01, theater01, wheelshop01 | 0:16.06 | 2:17.01 |
| 1 | tune-01 | machtest01, tune01 | 0:00.88 | 1:11.47 |
| 2 | lisence | license01, license02 | 0:13.38 | 0:53.39 |
| 3 | dealer-01 | dealer01 | 0:15.45 | 2:40.91 |
| 4 | wash-01 | wash01 | 0:00.94 | 1:31.65 |
| 5 | option | option01 | 0:00.05 | 0:43.69 |
| 6 | menu-03b | battle2p01, free01, gorace01, arcade01, timetrial01, main01 | 0:01.39 | 2:15.35 |
| 7 | mapmenu | gtmenu01 | 0:00.83 | 0:54.17 |
| 8 | home2 | home01 | 0:12.53 | 1:49.75 |

The file name is always `music.seq`, the artist "Isamu Ohira". "loop" is
where the endless loop starts, "length" where it jumps back. The game's BGM
ids 0..0x11 select these names (alphabetically: 0 arcade01 ... 17
wheelshop01); 0x12..0x22 are the streamed `.ads` groups of `bgm/ads.inf`
(MADS: groups, track records and the race songs' markers, in
[../FORMATS.md](../FORMATS.md) "bgm/ads.inf").

Which music plays when (BGM ids, race jingles, result and presentation
music) is implemented in `src/audio/bgm.h` (the BGM layer: ids, restart
rules), `src/race/race_audio.h` (race music cues) and
`src/menu/flow/game_flow.cpp` (menu music by page), and described in
[behaviour/music.md](../behaviour/music.md).

## SEQG sequences (`music/music.seq`) ✅

```
0x00 "SEQG", 0x04 u32 0 (relocation base), 0x08 u32 sequence count (9)
0x0C sequence[count], 0x48 bytes:
     u32 volume  Q14 (0x4000 in all)
     u32 tempo   microseconds per 480 ticks (1666664..3037972)
     u32 track_offset[16]  (0 = none; unused channels point at a stub
                            that only loops)
```

A track is a byte stream: an initial delta time, then `event, delta` pairs
until the end event (which has no delta). Deltas and note lengths are
MIDI-style variable-length numbers (7 bits per byte, big endian, bit 7 set
= more bytes). Events by first byte `b`:

| byte | event |
|---|---|
| `0x80..0xFF` | note: key `b & 0x7F`, u8 velocity, length (variable) |
| `0x40..0x7F` | pitch bend `((b & 0x3F) << 8 \| next) - 0x2000` (-8192..8191) |
| `0x20..0x3F` | loop start, count `(b & 0x1F) + 1` |
| `0x00` | end of track |
| `0x01 n` | loop start, count `n` (`0xFF` = endless) |
| `0x02` | loop end |
| `0x03 n` | program (index into the music.ins program table) |
| `0x04 n` | channel volume 0..127 |
| `0x05 n` | channel pan 0..127 (0x40 centre; a channel starts at 0) |
| `0x06 a b c` | tempo, 24-bit big endian |
| `0x07..0x1F` | ignored by the game (none in the file) |

Notes are note-on plus length: there are no note-off events. Loop counts
mean "jump back this many more times": at a loop end with count 0 the
loop is popped, otherwise the count is decremented (unless 0xFF) and
playback continues after the matching loop start; loops nest 4 deep.

Coverage (`gt3tool seq-survey`): 9/9 sequences, 144 tracks, 40,157 events,
35,909 notes; every track ends with one endless loop over the whole piece
(loop start, loop end, end); 72 of the 110 programs are used; no tempo
events, no undefined bytes. 84 notes (sequence 4, drum key 47) fall in no
region of their program and are silent in the game too. All 16 tracks of a
sequence have the same length. The tempo word is per 480 ticks, but the
music is written with a beat of 120 ticks: e.g. sequence 0 is 127 BPM
(onset analysis of the renders shows the beat at 120 ticks).

## Mixer (`VoiceMixer`) and reverb (`SpuReverb`)

46 voices (the game's pool), 48 kHz stereo s16 on its own SDL audio stream
(streamed music keeps its own stream). Voices read ADPCM in place and
follow the frame flags as above; pitch goes through the game's 44.1->48 kHz
conversion; interpolation is the SPU's 4-point Gaussian filter (hardware
table; Catmull-Rom remains selectable); the ADSR envelope uses the hardware
counter model; voices with the reverb send feed the SPU2 reverb unit
(libsd effect presets; the game uses "studio C" with EVOL 0x0FFF); voice
stealing follows the game's priority rule. Volume changes are slewed over
~5 ms (the SPU2 applies them at once), and a 0.5 output trim gives
headroom. Voices play on two buses, music (the sequenced menu music) and
effects (every other voice), scaled by the Music and Sound effects volumes;
the streamed music takes the same 0.5 trim, so the two meet at equal weight
([sound §1.1, §2.4](../behaviour/sound.md#24-volume-and-output)).

## Verification

- Layer decode vs FFmpeg's `vag` decoder on the same bytes: identical
  sample counts, max difference 8-9 LSB (FFmpeg rounds the predictor
  differently).
- Loop seams with hardware-style looping, 682 seams (341 layers in 50
  banks): the jump at the seam is at the median 0.28x (p95 0.86x, max 1.33x)
  the 99th-percentile sample-to-sample step of the same layer, i.e. no clicks.
- Engine render (`gt3tool engine-wav 1 0 out.wav`, before the 44.1 kHz
  pitch fix, so all frequencies were 8.8% high): the dominant harmonic
  tracks 1.81-1.96 x rpm/60 from 1600 to 7400 rpm across all layer changes.
- Effects rendered through the mixer correlate 0.988 with the direct decode
  (measured with the earlier cubic interpolation).
- Sequenced music (`gt3tool seq-wav`, all 9 sequences, two passes of the
  loop): no clipping (peak -0.9..-4.7 dBFS with the 0.5 trim), no clicks
  found by a second-difference detector, loop periods equal to the tempo
  maths within one 60 Hz update (e.g. 43.640 s expected / 43.633 s
  rendered for sequence 5). Rendered with fine timing (4800 updates/s) and
  dry, the two passes of the loop correlate 0.992-0.995 sample by sample.
  Spectral peaks sit on the A440 grid (mean offset -1.5..+2.9 cents).

## Open

- The 7408-byte block of `music.ins` not referenced by any region.
- Exact source of the engine "throttle" value; the unit driven with
  `rn_01.es` and its rpm input (a constant 2500 in at least one state).
- The sequencer's fade (the game fades a sequence's volume linearly
  over a number of frames) is not reproduced; nothing in the menus we have
  needs it yet.
