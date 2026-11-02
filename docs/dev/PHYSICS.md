# Vehicle physics (src/sim/)

Every race runs on GT3's own race step, written in the PS2's single-precision
arithmetic (`ee::F`, `ee::D`, `ee_math`) so that a race is step for step the
game's: the "exact core" in `src/sim/exact/`. Its behaviour (state, step order,
tire, suspension, driveline, aids and car-parameter rules) is described in
[docs/behaviour/physics.md](../behaviour/physics.md), the CPU drivers in
[docs/behaviour/ai.md](../behaviour/ai.md); section numbers below (physics §n,
ai §n) refer to those pages. The race screen drives the core through
`race::ExactField` (docs/dev/RACE.md, "Races on the exact core").

## Files

| File | Role |
|---|---|
| `sim/sim_types.h` | what the race screen, renderer, sound, HUD and rumble read of a car: `ControlInputs`, `VehicleState`, `VehicleSetup`, `VehicleTelemetry` (filled from the car record by `race/exact_car.*`), `surface_index` (the road material as the tire tables index it, wet materials remapped) |
| `sim/program.h` | `Behaviour`: the rules of a program, per routine (`rule(Rule)`), plus the step rate |
| `sim/car_spec.h/.cpp` | `CarSpec` and `build_car_spec()`: the car as the menus describe it (weight, size, torque curve, gearing, tire size, stock ride height, drag and downforce), for the car pages (power, torque, showroom top speed, gear chart), the showroom pose, GT Mode's grid order and online's plausibility checks; `CarSettings` / `stock_settings` (the garage settings), `generate_gear_ratios` / `apply_gear_slider` (computed gearboxes, in the PS2's arithmetic) |
| `sim/engine_condition.h/.cpp` | the engine condition's torque factor, its counters' advance and the odometer step (physics §10.3), for GT Mode's car pages and the race's read-back |
| `sim/collision_exact.h/.cpp` | `collide::exact`: GT3's collision manager in its own arithmetic (body record, step functions, `step_all` / `step_walls`; physics §5.3) |
| `sim/course_collision.h/.cpp` | `CourseCollision`: a course for the collision step (wall grid walk, segment links, the road segment under a point) |
| `base/ee_float.h/.cpp`, `base/ee_math.h/.cpp`, `base/ee_double.h/.cpp` | EE single precision (`ee::F`), the game's `sinf` / `cosf` / `sincosf` / `atanf` / `atan2f` and its software doubles (`ee::D`), bit for bit. `ee::F`'s add, subtract, multiply and int conversions are inline integer-only code (no float registers: on phone cores the moves between register files cost more than the arithmetic); `ee::side_sign` tells a side test's sign where it is certain, so a port can leave out a test that cannot pass; `sinf` / `cosf` keep their recent results |
| `sim/exact/` | the race step and the race (below) |
| `sim/exact/race_record.h/.cpp` | `RaceRecord`: a race as a replay keeps it (the setup without pointers, the program's fingerprint, every car's pad frames, the game's changes between steps as `RaceChange`, the end hash; the free run's recorders at the run's start); `RacePlayback` steps an `ExactRace` from it; `race_hash` |
| `sim/exact/sparks.h/.cpp` | the race's sparks (presentation §L7): the pool, bursts and update in GT3's arithmetic, each car's emission from its spark points, the VU0 matrix library's rotations and sine; `ExactRace::step` runs them after the race step (they draw the VU0 random register) |
| `sim/exact/pad_read.h/.cpp` | the pad read: a device's state into the frame the driver input reads (by pad type: digital, analog sticks, the analog pad with pressure buttons, the steering wheel), the stick response curve from `ProgramData` |
| `race/exact_car.h/.cpp` | adapters from the car record: `exact_setup` (`VehicleSetup`), `exact_state` (`VehicleState`), `exact_telemetry` (`VehicleTelemetry`), `exact_facts` (the HUD's car facts) |
| `race/exact_setup.h/.cpp`, `race/exact_field.h/.cpp` | a race's `RaceSetup` from the event, the cars and the player's settings; `race::ExactField`, the race screen's `RaceField` (docs/dev/RACE.md) |

### The exact core (`sim/exact/`)

- **The car record.** A mirror of GT3's per-car record (`car_record.h`: `Car`
  with the parameter block `Params`, the wheels, driveline and ground records and
  the AI driver object, every field by its record offset; `car_fields.h`: the
  field table, `load_image` / `store_image` for a record image).
- **The step** (`tick.h`), phase by phase: 1 and 9 (`tick_misc.cpp`); 2 the
  per-tick preparation (`tick_prep.cpp`); 3 the driver input: the pad path and
  the AI paths' apply (`tick_input.cpp`), the AI's step, decisions and traffic
  (`tick_ai.cpp`), which plan through `AiPlan` (`CoursePlan`: the AI course object
  `AiCourse` with its line queries, `ai_course.h`; the point bookkeeping, corner
  speeds, braking point, target point and speed controller, `tick_ai_plan.cpp`);
  4 the wheel steer angles (`tick_steer.cpp`); 5 the slip ratios
  (`tick_slip.cpp`); 6 the steering indicators and driving aids
  (`tick_aids.cpp`); 7 the engine, clutch, gearbox and driveline
  (`tick_engine.cpp`, `tick_drive.cpp`, `tick_gear.cpp`), the tire forces and the
  planar motion (`tick_tyre.cpp`, `tick_planar.cpp`); 8 and 13 brake heat, tire
  wear, its flags and the flap pendulums (`tick_wear.cpp`); 10 the collision step
  (`collide::exact`); 11 proximity and slipstream (`tick_proximity.cpp`); 12 the
  suspension and body attitude (`tick_suspension.cpp`, `tick_attitude.cpp`), the
  ground contact on the course's ground query `CourseGround` (`course_ground.h`,
  from our parse of the course file) and the tire effect requests
  (`tick_ground.cpp`), the effects, speeds and wheel spin (`tick_effects.cpp`,
  `tick_speed.cpp`); the shared lookups and the tire record (`tick_table.cpp`).
  Every car's phases around the collision step (`tick_phases.cpp`), then the
  step's loop over the race order (`tick_flow`; `pit_lane.h`, `tick_pit.cpp`).
- **The race flow** (`race_flow.h`): lap progress, line crossings, laps,
  sectors, the finish, the wrong-way flag and HUD messages, the hand-backs, each
  race mode's end (the time trial's laps and course-out, the license's judging,
  the machine test's split, the battles', free practice's), every race mode's
  lap handlers, the free run's lap recordings and its ghost car; the race
  display's events, the entries' and session's time records, the data logger's
  tables and the free run's recorder events as outputs; `SoloRecorders`, the
  solo race objects' input devices on the game's record memory (the player's
  recording, the ghost's playback, the free run's lap and best lap stores, the
  Analyzer's pair of playing devices) with GT3's pad frame recorder
  `ControlRecorder` (`control_record.h`: the bit stream, the frame coding,
  closing, playback and seek); the pit lane's state
  machine, the AI's pit switches and the end of a run. The race-flow gate and
  the rubber band (`tick_race.cpp`), the race clock, the gap matrices and the
  ranking.
- **The race's set-up**: the parameter block P as the initialisation builds it
  (`params_build.h`: equipment, spec builder, settings converter), byte for byte;
  the race start (`start.h`, `start_cars`: every byte of each car's record under
  every race mode's conductor: the record cleared and linked, the spec and P, the
  driver parameter set and the event's overrides, the placement on the grid, the
  main line or in the pit box, the pose on the road, the standing and moving
  starts, the tires, laps and sector, the AI driver's line and plan, a player's
  run-up by the AI, the ranking); the AI course object (`ai_setup.h`); the
  course's single-point ground query, race progress, start grid and sectors
  (`course_query.h`); everything the step reads outside the car record
  (`race_inputs.h`: the static physics block, brake heat constants, force
  feedback, tire effect curves and engine sound scales, the driver input's
  constants, the race configuration from the event, the wear curve, the rubber
  band's parameters and every race mode's conductor answers).
- **The program's tables** (`program_tables.cpp`: GT3's 20 small tables as
  values; `program_data.h` serves the disc's release's variant and checks the
  disc's tables against them, each found through the routine that addresses it
  by a hash of its masked code (`program_locate.h`) or by its content, and
  recognizes the rules' routines the same way, per routine (`sim::Rule`,
  `Behaviour::variant`). A table not found or unlike every release's, or a
  routine not recognized, is an issue (logged) and NTSC-U's values / rules
  serve, so every disc whose program can be read races. Where a routine differs between the releases the
  port branches on `Car::behaviour` (`ProgramData::behaviour()` is the disc's
  rules at 60 steps a second; `set_behaviour` picks NTSC-U's rules for online and
  the demos), the collision body on `Body::behaviour`.
- **The whole race** (`race.h`, `race.cpp`): `ExactRace` holds a race's state
  (the car records, the collision bodies, the race inputs, the AI course and
  plan, the course's ground, progress and collision queries in a shared
  `RaceCourse`, the proximity and gap matrices, the random register, the race
  flow, the rubber band, the race information's start words) and is built from a
  `RaceSetup` (the program's tables, our database, the event record, the race
  mode, the race information's words, the `StartEntry`s; with the solo modes'
  recorders the player's car records its frames on its device and the ghost
  car plays its own). `step(pads)` runs one race frame: the start countdown, the step with the collision hook-up, the tire
  effect emitter (`SmokeParticle`), the race flow, the gap matrices, the
  ranking, the rubber band and the end countdown; the frame's outputs (race
  events, pit events, collision reports, particles) and every record are
  readable, and the race is copyable. `logger_jump` is the Analyzer's REPLAY
  jump (presentation §G10): the cars back on a logger entry's snapshot, their
  playback sought to its frame. Its construction refuses what the step
  cannot run (a road material past the 16-entry surface tables, an Env0 block too
  short for the tire effects, free practice without the cars' pit boxes or with
  its slots not the cars' places, a grid slot outside 0..7); nothing a step does
  throws. Posed cars (an online race's remote players, docs/dev/ONLINE.md):
  `set_posed` / `pose`, `apply_impulse`, `car_contacts`.
- Not modeled: the race display's drawing (its events are outputs), the
  sounds' pass-by state, the pad output, the particles' aging and drawing.

## API

```cpp
// A race (what race::ExactField does for the race screen):
sim::exact::RaceSetup setup = /* race/exact_setup.h */;
sim::exact::ExactRace race(setup);          // throws if the race cannot be set up
race.step(pads);                            // one frame, 1/60 s
const sim::exact::Car& car = race.cars()[0]; // the car record
sim::VehicleSetup su = race::exact_setup(car);
sim::VehicleState s;
race::exact_state(car, su, s);

// The menus' numbers:
auto spec = sim::build_car_spec(db, "r34_gtr_v_99", parts, settings);
spec->peak_power_kw(); sim::showroom_top_speed(*spec);
```

The race screen builds every race through `race::RaceField::create`
(`race/field.cpp`), which makes an `ExactField`; single-car runs (time trial,
license, machine test, free run, free practice) are `ExactField`s of their race
mode's conductor too. If the disc's program cannot be read or the race cannot be
set up, the screen shows the reason and goes back to the menu.

## User settings (garage tuning)

`sim::CarSettings` holds the physics §6.6 settings (the garage car record's
settings words) as raw values: gear words, final drive, computed-gearbox
slider, drivetrain split; brake-controller indices, downforce front / rear,
turbo bytes; camber, ride height, toe, spring, motion ratio, damper indices,
anti-roll indices; LSD settings, TCS and ASCC indices.

`stock_settings(db, car, parts)` gives the defaults. The race converts the
settings into the parameter block exactly as the game does
(`sim/exact/params_build.h`); `build_car_spec(db, car, parts, settings)` applies
the ones the menus show (gearing, final drive, downforce, ride height, turbo
stages). `apply_gear_slider()` regenerates a computed gearbox's gear words from
the slider with `generate_gear_ratios` (physics §6.7), the game's generator in
its own arithmetic.

## Verification

Tests (`tests/`):

- `test_exact_game.cpp`, `test_exact_solo.cpp`, `test_exact_replay.cpp`,
  `test_exact_demos.cpp`, `test_net_exact.cpp`: whole races, single-car runs,
  replays and the disc's demos on the exact core (most need `GT3R_DISC`).
- `test_params_build.cpp`, `test_race_inputs.cpp`, `test_program_tables.cpp`,
  `test_program_locate.cpp`: the parameter block, the race inputs, the program
  tables and their lookup (synthetic images run without a disc).
- `test_ee_float.cpp`, `test_ee_double.cpp`: the PS2 arithmetic, including
  `ee_float_platform_hash`, which pins `ee::F` and `ee_math` results across
  platforms (arm64, x86-64, with and without FMA contraction), and
  `ee::side_sign` (never a wrong sign).
- `test_course_ground.cpp`: the ground query against a plain walk of every
  triangle of the cell, on synthetic roads.
- `test_car_spec.cpp`: the menus' car spec, the settings, the computed gearbox,
  the showroom top speed, every car of the database, the engine condition's
  rules.
- `test_mod_cars.cpp`: mod cars drive on the exact core.

The exact core is compared step for step with the game's own race step on the
four GT3 discs (every field of every car record, every step), outside this
repository.

## Performance

One race step of a six-car race (Trial Mountain, all six cars driven by the AI)
costs about 0.45 ms on one of a Galaxy A15's Cortex-A55 cores, 0.16 ms on its
Cortex-A76 and 0.04 ms on an Apple M-series Mac, the step alone in a
RelWithDebInfo build; in the game on the A15 about 0.55 ms. The step is
integer-heavy (`ee::F`), so the core leaves out work whose outcome is certain:
each road triangle's terms are computed once and the ground query skips the
triangles a wheel is certainly not on (`CourseGround`), the collision step skips
the wall tests whose side is certain (`ee::side_sign`), and `sinf` / `cosf`
keep their recent results (`ee_math.cpp`).

## Open points

- Other games on the engine (GT Concept) race on GT3's tables and NTSC-U's
  rules (Settings > Disc & saves says so); their own physics is not supported yet.
  A disc whose program cannot be read cannot race: the race screen says so.
- Electric mod cars race on their donor car's engine (docs/MODDING.md).
- The HUD's boost gauge reads the turbo's boost factor from the two lagged stages
  minus 1 (`exact_telemetry`); how the game's gauge maps it is not pinned down.
