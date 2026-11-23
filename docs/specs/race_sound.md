# Race sound effects: tyre loops and positional effect voices

How GT3 plays each car's tyre sounds and places its effect voices, from the
race sound code of `CORE.GT3`. The slip words the loops read are in
[physics.md](physics.md) §8.3 / §8.3c; the music and effects mix is in
[sound_sequencer.md](sound_sequencer.md) "Streamed music and the output
mix". Implementation: `src/race/car_sfx.*` (`CarSfx`, `wheel_loops`,
`place_voice`, `road_noise`), `src/audio/loop_effect.*`,
`src/app/screen_race_audio.cpp` (`RaceScreen::update_tyre_sounds`).

Tags: **[C]** confirmed from the code, **[I]** inferred, **[?]** open.

## Changelog

- 2026-11-07 (public issue 222): §1 the view's camera kinds and the target rule, §2b the engine
  units and one-shots (every car attenuated, the target in the cockpit at the camera), §2c the
  turbo level's order in the frame; §4 ours now follows them (the listener moves with the camera).
- 2026-10-20: first version. §1 the per-wheel tyre slots and their surfaces, §2 the
  placement and the loop voice (priority), §3 every car runs it, §4 ours. Implemented
  (per-wheel `CarSfx` voices, opponents' tyre loops, `VoiceMixer::set_priority`, the
  `LoopEffect` volume order).

## 1. Tyre slots [C]

**Slots.** The race entry car's constructor (`0x1A66F0`, from
`RaceEntryCar` `0x1771B8`) builds 32 looping effect slots (context +0x44C,
0x1C bytes each). Each binds the **first region** of a named se.inf effect
(`0x22C5B0` with selector 0; effect ids map to names through the table at
`0x2E9380`):

| Slots | Effect |
|---|---|
| 0 / 1 / 2 / 3 | `turbine2` (whine) / `turbine` (spool) / `in_car` / `body_noiz` (always volume 0) |
| 4 + 7w .. 10 + 7w, wheel w = 0..3 | `off_skill`, `on_skill`, `off_roadnoiz`, `rumble`, `off_skill`, `on_road`, `rain_noiz` |

So every car has **one voice per wheel per tyre effect**: 28 tyre slots.
`on_skill` is gt3se01 region 12 (volume 12900); every tyre effect is a
single region.

**Per frame** (`0x177A50`), for each wheel w:

1. Every slot of the wheel gets the wheel's position (`0x1EB910`, the
   interpolated wheel position in the world) taken into the camera frame
   (`0x2580EC`), volume 0 and pitch = the car's Doppler factor D.
2. Seven routines fill volume and pitch by the wheel's surface index
   (wheel +0x24, after the wet remap of physics.md §1). G(ch) is the view's
   channel gain (table `0x2D33A8`, 5 views × 16 floats, `0x1DE050`):

| Routine | Slot effect | Channel | Surfaces | Volume | Pitch |
|---|---|---|---|---|---|
| `0x1DE548` | `off_skill` (first) | 12 | (none: the routine is empty) | 0 | D |
| `0x1DE3B8` | `on_skill` | 11 | 0, 7, 8, 9 | G × wheel +0x30 | D × wheel +0x34 |
| `0x1DE508` | `off_roadnoiz` | 14 | 5 | G × +0x30 | D × +0x34 |
| `0x1DE550` | `rumble` | 6 | 1, 13 | G × +0x30 (the kerb formula) | D × +0x34 (kerb ramp) |
| `0x1DE598` | `off_skill` (second) | 13 | 2, 3, 4 | G × +0x30 | D × +0x34 |
| `0x1DE410` | `on_road` | 9 | 0, 7, 8, 9 | G × road roar | D × road pitch |
| `0x1DE4A0` | `rain_noiz` | 10 | 6, 12, 14 | G × +0x30 | D × +0x34 |

On other surfaces (10, 11, 15) a slot keeps volume 0. +0x30 is the slip
intensity (physics.md §8.3) **unclamped** (tarmac reaches 2.0 and more), and
+0x34 the skid pitch (§8.3c). Airborne wheels follow the slip words' own
counter (physics.md §8.3: recomputed for 5 steps, then 0 and pitch 1).

**Road roar** (`0x1DF9D8`, tables from `0x1DE830` at `0x49CFD8`): silent
(volume 0, pitch 0.5) once the airborne counter (+0x2A) is 0 or on surfaces
2–6 and 10+. Otherwise, with W the axle's tyre width (car +0x10 +0x284 +
4·(w >> 1), m) and v = |rim speed| (+0x20):

- width factor f = 0.5 below 0.135, 1.0 above 0.355, else 0.5 + (W − 0.135) ×
  0.5 / 0.22;
- volume = min((v/100)² × 2.55 × f, 0.3);
- pitch = 0.5 below 8.333 m/s, 1.0 above 41.667 m/s, else 0.5 + (v − 8.333) ×
  0.5 / 33.333.

Verified exactly (diff harness `race_road_roar`, 2000 calls).

**View** (`0x177E14`, the camera manager's `CameraManager::vf15`): in-car → 0
for the followed car ("target", `0x2091E0`), 1 for the others; chase → 2 /
3; any other camera (replays, TV) → 4 for **every** car. The same view drives
every unit and slot of the car (§2b). [C]

- `CameraManager::vf15` reads the current camera's kind (its vf10, the view
  slot's +0x5D4 camera): 0 or 1 → type 0 (in-car), 2 → 1 (chase), anything
  else → 2 (other). The kinds: `OnBoardCamera::vf10` gives 0 for mount 0
  (the in-car view), 2 for mount 1, else the mount's outside flag (+0x109:
  0 on board, 1 outside); `DesignerCamera::vf10` is the on-board camera's
  while turned on board (+0xA78 == 3), else 3; `LoggerCamera` 2;
  `AutomaticCamera`, `AutomountCamera`, `HeliCamera`, `MatrixCamera`,
  `ZoomupCamera`, `PasslineCamera` / `StartingCamera` and
  `VariableCameraBase` 3. So every car mount of a replay (presentation §G4:
  kind 4 mounts, inside or outside, the course camera turned on board, mode
  2's on-board cut) is the in-car type; the course shots, the other
  procedural cameras and the pre-race shot are "other".
- `0x2091E0(CM, car)`: with the view's target state (+0x4A0) pair flag
  +0x16 clear, the car is the target when it is the state's car (+0x18);
  with a pair, **no** car is the target (every car takes 1 / 3 / 4).
- In-car (type 0) the target's camera-frame position (sp+0x380) is replaced
  by (0, 0, 0) (`0x2B2CC0`) before the units and slots are filled: its units
  sit at the camera (§2b). The copy at sp+0x4 (the one-shots', §2b) is made
  before, so it keeps the car's real offset.

**Doppler** (per car, `0x177C94`..`0x177DD0`): with û the unit vector from
the camera to the car and Δcam, Δcar the camera's and the car's displacement
over the frame, D = (û·Δcam + c′) / (û·Δcar + c′), c′ = 5.524167 m per frame
(331.45 m/s at 60 Hz). A sound-context flag (race +0x5D4 → +0xD8) off: D
stays 1 and the context's voices are stopped (`0x1A70C0`, `0x1A6B80`).

## 2. Placement and the loop voice [C]

`0x1A6C38` sends the three engine units (`0x1A7140`) and then the 32 slots
(`0x1A74A0`). For a slot at (x, y, z) in the camera frame (x right, z
forward):

1. d = √(x² + z²). If d < 0.25: z ← z − 0.25 + d and d is computed again
   (a source at the camera is moved behind it).
2. x/d and z/d drive the slot's two surround flags exactly as for the engine
   units (the rear phase inversion; ours `SurroundCue`).
3. Angle a = clamp((x/d + 1) · π/4, 0, π/2); left = cos a, right = sin a
   (each negated by its surround flag).
4. Gain g = clamp(32/d × 1.0, −1, 1) × slot volume × the race-SE switch
   (`0x3521E0`) × the SE fade (`0x3521E4`): min(1, 32/d) since d > 0.
5. Stealing priority: 0 when |g| ≥ 1, else min(255, int((1 − |g|) × 255))
   (truncated). The louder the voice, the lower the value, the fewer
   requests may take it.
6. `0x22BD00` with left × g, right × g, the pitch and the priority.

`0x22BD00` (also used by the course ambience player `0x1A79C0`): side =
clamp((int(gain × SE master × 16384) × region volume) >> 14, −0x4000,
0x3FFF), SE master 1.0 (`0x352B88`); the product is taken **before** the
cap, so a gain above 1 is louder than the region's own volume. Both sides 0:
key off. Pitch = region pitch × int(p × 4096) >> 12. A playing voice gets its
priority (`0x267C18`, voice +3), pitch and volume again every frame; else it
is keyed on with that priority (the region's own when negative).

**Consequences.** A car sliding on all four wheels is four `on_skill`
voices, each at its wheel, plus four `on_road` voices; nothing takes "the
loudest wheel". Four equal wheels sum about 6 dB above one (uncorrelated
loops), a power drift (rear pair loud, front pair quieter) about 3–4 dB. A
squeal past full gain (G × slip ≥ 1) has priority 0 and is never stolen.

## 2b. The engine units and one-shots [C]

`0x177A50` fills a frame context on its stack (passed to `0x1A6C38` as a1):
+0x0 the target flag, +0x4..+0xC the car's camera-frame position, +0x14 the
distance scale **1.0** (`0x177DFC`, the same for every car), +0x18 / +0x34 /
+0x50 the three engine units (+0x10 volume, +0x14 pitch, +0x18 rpm), then
the 32 slots, and +0x36C..+0x378 the one-shot strengths (blow-off, the
collision kinds).

- Units (`0x177E94..0x178174`, by the view's channel gains `0x1DE050`):
  unit 0 = G(engine) × car +0xF84+0x4E4 / 64, unit 1 = G(exhaust) ×
  +0x4E5 / 64, both pitch D and rpm +0x490; unit 2 (wind) = G(wind) ×
  `0x1DF840`, pitch D, rpm sp+0x390 (2500 = 0x9C4 unless set). Slot 0
  (whine) G(whine) × `0x1DFB58`, pitch D × its pitch; slot 1 (spool)
  G(turbo) × +0x4E6 / 128, pitch +0x4E2 / 6144 (**no** D); slot 2 (in-car)
  G(in-car) × `0x1DF910`, pitch D × its pitch; slot 3 volume 0. Blow-off:
  G(blow-off) × +0x4E8.
- `0x1A7140` places unit u like a slot (§2): the 0.25 rule, the surround
  flags, the pan, and gain clamp(32/d × ctx+0x14, −1, 1) × the unit's volume
  × the SE switch × the SE fade. With +0x14 = 1 every car's units, the
  target's too, are min(1, 32/d).
- The one-shots (`0x1A6C38` after the slots, the effect ids at `0x330910`):
  d from ctx +0x4 / +0xC (horizontal in the camera frame, 0.25 rule), each
  strength at least 2^-14 plays with gain strength and 32/d **unclamped**;
  the blow-off (first) with the car's pan, the others centred (both sides
  max(|l|, |r|)).

## 2c. The turbo level's order [C]

`RaceBase::vf30` runs the dynamics frame `0x1F39D8` (one 60 Hz step): the
phase `0x1E8598` (the turbo step `0x1E17F0`: the raw level at +0x4E6, sound
§7.5) and later `0x1EB218` → `0x1DFC60` (the bank levels' halving, the
level × B[0x1E]/100 = 0.2 in place, the blow-off × B[0x1F]/10 = 2.5). Then
`RacePS2Base::vf30` sends every car (`0x177A50`). So per frame: step, scale,
read: the sent level is the step's level / 5, once per step.

## 3. Every car [C]

`RacePS2Base::vf30` (`0x17DA58`) calls `0x177A50` for every car of the race
(the car list at the race's +0x18, skipped while its +0x1788 flag is set: the SE mute, on
while records are off, so the race behind every quick menu and the licence preview box send no
car sound; presentation.md §C5).
Opponents run the same routine with the other-car gains (views 1, 3, or 4)
and the same placement, so their squeal and road roar fade with 32/d and
pan with their wheels. No distance limit: the voice allocator's priorities
decide which far, quiet loops sound.

Collisions are read for the followed car only (unchanged, sound §7.7).

## 4. Ours

- `CarSfx` per car: 24 `LoopEffect`s (6 tyre effects × 4 wheels; the empty
  slot and `body_noiz` are not modelled), updated every physics step from
  `VehicleTelemetry::smoke` / `skid_pitch` and the wheel's contact point
  seen from the camera's eye and axes (`place_voice`). Cars without a
  simulation (stand-ins) estimate the slip words from their state.
- `RaceScreen::update_tyre_sounds`: player 1 and every opponent from
  `camera_`, player 2 from `camera2_`, each car's view from
  `CameraRig::sound_kind` and the target (`RaceScreen::sound_listener`).
  Doppler from velocities (`OpponentEngines::doppler`, clamped as for the
  engines) with the camera's eye velocity for the listener
  (`CameraRig::eye_velocity`) rather than per-frame camera and car
  displacements; ours zeroes the eye velocity on the frame of a cut [I:
  whether GT3's previous camera position (camera +0xA0..+0xA8) follows a cut
  is not known].
- `RaceScreen::update_car_sounds` sends the engines and the extra sounds the
  same way (§2b): every car attenuated, the target at the camera in a
  car-mounted view, the one-shots at the car's own offset; the opponents'
  turbo models step with the race (`OpponentSound`), their extras play for
  the target and the five voiced cars (the wind stays `OpponentEngines`',
  nearest two).
- The surround flags are applied only with the mixer's surround-cue option
  (off by default), as for the engines.
- `LoopEffect` takes the product before the cap and re-sends the priority
  (`VoiceMixer::set_priority`) while the voice plays; the stealing rule is
  `VoiceMixer::key_on`'s (value ≥ request, highest value, then oldest by
  key-on order).
- Measured (scratch harness, chase view, the disc's gt3se01 bank, through
  the voice mixer): the per-wheel loops (squeal + road roar) are +4.3 dB
  over the old loudest-wheel voice in an FR power drift, +5.3 dB in an R34
  four-wheel slide and +4.0 dB in hard cornering. A 6-car race fills all 46
  voices with tyre loops when nothing else plays (the allocator then keeps
  the loudest); the six updates take about 2 µs per step.
