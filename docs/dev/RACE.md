# Race scene

The race screen runs every race of the game: GT Mode and arcade races,
licence tests, time trials, 2P battles, online battles, replays and the
Replay Theater. On its own (`gt3r <disc> --screen race`, "Time trial" in the
main menu) it is a time trial: choose a course and a car (names from
`data::CarDatabase`), then drive.

Code: `src/race/` (engine side) and `src/app/screen_race*.cpp` (the screen; see
"Race screen code map" below).

| File | What |
|---|---|
| `race/backend.h/.cpp` | `RaceTrack` (over `track::Track`) and `RaceVehicle` (the player's car of a race on the exact core) interfaces, `car_setup_from_database()` |
| `race/controls.h/.cpp` | Pad → `sim::ControlInputs` mapping, rumble model |
| `race/interp.h/.cpp` | Fixed-tick accumulator, quaternion helpers, `VehicleState` interpolation |
| `race/camera_rig.h/.cpp` | Chase, far chase, bumper, roof and free-orbit cameras |
| `race/race_scene.h/.cpp` | GPU scene: course (scenery, backdrop, sky domes) and the car (body LODs + four wheels) |
| `race/hud.h/.cpp` | HUD (lap times, tachometer, gear, speed) using the game's own assets |
| `race/hud_layout.h/.cpp` | The HUD layout per display mode (the original's layout table) and the mode a run uses |
| `race/hud_clocks.h/.cpp` | The start title's and the replay mark's clocks (`TitleClock`, `ReplayMarkTimer`) |
| `race/machine_test.h/.cpp` | Machine tests: the test from the event, its timing points and goal (`MachineTest`) |
| `race/lap_events.h/.cpp` | The event display (the pop-ups' queue and timing), race gaps (`PassingOrder`), time trials' best lap and split times (`BestLapSplits`) |
| `race/replay.h/.cpp` | Replay format, recorder, player, save/list in `<user data>/replays/` |
| `sim/exact/race_record.h/.cpp` | A race on the exact core as a replay keeps it (`RaceRecord`: setup, every car's frames, the game's changes, the end hash), `RacePlayback`, `race_hash` |
| `race/exact_demo.h/.cpp` | The disc's demo runs as race records (`demo_record`: the race GT3's replay playback builds from the save) |
| `race/replay_library.h/.cpp` | The Replay Theater's files: list with headers, delete, rename, info texts |
| `race/replay_options.h/.cpp` | REPLAY OPTIONS: replay view mode, target, HUD items, sound; the panel |
| `race/analyzer.h/.cpp` | The Analyzer (data logger): the logger's sectors, keys and graph from the race's data logger tables, GRAPH / REPLAY screens |
| `race/ghost.h` | `GhostLap`: the Analyzer's logger ghost (a recorded lap as poses); race ghosts are cars of the exact race (see "Ghost") |
| `race/replay_cameras.h/.cpp` | GT3's replay camera script (GTRC) and the replay director |
| `race/replay_focus.h/.cpp` | The car the replay cameras follow: battles, TOP / AUTO interest scores |
| `race/procedural_cameras.h/.cpp`, `race/music_markers.h/.cpp` | View mode 1's procedural cameras; the race music's markers its cuts follow |
| `race/clut_animations.h/.cpp` | The course models' palette animations (see "Course animations") |
| `race/race_audio.h/.cpp` | Reverb ambience model, race music, opponents' engine notes |
| `race/race_playlist.h/.cpp` | The race playlist (the options' music list): next song, list order / shuffle, saved text |
| `race/race_sfx.h/.cpp`, `race/car_sfx.h/.cpp` | Named se.inf effects; tyre, kerb, road and collision sounds per car |
| `race/field.h/.cpp` | `RaceField`: the race's cars for the screen (player(s), opponents, standings); `RaceField::create` builds an `ExactField` |
| `race/exact_field.h/.cpp` | Races on the exact core: `RaceField` over `sim::exact::ExactRace` (see "Races on the exact core") |
| `race/exact_car.h/.cpp`, `race/exact_setup.h/.cpp` | A record as the screen reads a car (state, setup, telemetry, facts, condition); the program's tables, the course and each car's equipment for the exact core |
| `race/results.h/.cpp` | Results overlay, time-trial overlay, post-race quick menu (game .pmb pages) |
| `race/post_run.h/.cpp` | What follows a run per mode (prize / name entry / replay), the quick menu's backdrop, pause EXIT as a finish |
| `race/paint.h/.cpp` | carcolor.db and the race-start colour draw |
| `tests/test_race.cpp` | Control mapping, stepping, interpolation, rumble, render-state decoding, car setup |
| `tests/test_race_presentation.cpp` | Presentation rules without the disc: records and RETRY, HUD layouts, replay view modes, quick menus, what follows a run, the title and mark clocks |
| `tests/test_race_lap_events.cpp` | The pop-ups, split points, machine-test timing and the loading picture without the disc |
| `tests/test_exact_game.cpp` | Disc: the pad read's frames; whole races through `RaceField` on the exact core equal to `ExactRace` stepped directly (`exact_game_race_lockstep`); a pad-read run equal to its replay's inputs (`exact_game_pad_replay`) |
| `tests/test_exact_replay.cpp`, `tests/test_exact_demos.cpp` | Disc: races on the exact core through a replay file and back, every step (`exact_replay_round_trip`); the whole-run ghost car (`exact_replay_ghost`); the disc's demos through their race records (`exact_demo_records`) |
| `tools/gt3tool/cmd_race.cpp` | Diagnostics: `race-texlod`, `race-blend`, `race-sheen`, `race-carprims` |

## Race screen code map

The screen is one class, `RaceScreen`, declared in the private header
`src/app/screen_race_impl.h` and defined over several files next to the other
screens in `src/app/` (a `screen_race_` prefix rather than a subdirectory: the
app sources are the flat `src/app/*.cpp` glob, and `src/race/` is already the
engine side). The header lists the methods grouped by the file that defines
them, then the state.

| File | What lives there |
|---|---|
| `screen_race.h` | Public: `make_menu_race_screen()` (`make_online_race_screen()` is declared in `screen_online.h`) |
| `screen_race_impl.h` | Private: `RaceScreen`, `DebugPreset` (GT3R_RACE), `MenuLaunch`, small helpers (`BrakeLamps`, `Perf`, `menu_keys`) |
| `screen_race.cpp` | Core: constructors (menu / online / `--screen race`), `update` / `render` dispatch, `init` / `begin` and the loading picture (`render_loading`), the menu request handler, `make_*_race_screen`, the screen registration |
| `screen_race_launch.cpp` | GT3R_RACE parsing (`read_preset`), menu requests → course / car / event / options (`resolve_launch`), online launches (`resolve_online`) |
| `screen_race_setup.cpp` | Loading and (re)starting a run: `start`, `build_vehicle` (the run's `RaceField` on the exact core; a race that cannot be set up becomes the error page), `restart` / `reset_run_state`, warp, TV cameras, LOAD GHOST |
| `screen_race_drive.cpp` | The race loop: `update_drive` (one frame) and `tick` (one race step), the race's AI taking the player's car (autopilot) and scripted inputs, finish detection, gear hint, dirt, pylons, tyre smoke, force feedback |
| `screen_race_render.cpp` | `render_drive`: 3D views (1P, 2P split, 4:3 frame), draw items, reflections, mirrors, sun / lamps, `hud_data`, the results / licence / time-trial overlays |
| `screen_race_audio.cpp` | Music cues, course ambience listeners, extra car sound inputs, `silence` |
| `screen_race_select.cpp` | The standalone selection page: course / car picker and saved-replay list |
| `screen_race_quick_menu.cpp` | Pre-race and result quick menus (layout per mode, item actions, disabled items), free run before a GT race, the run's records and Top 3 (`record_run`, `fill_top3`), options / settings / records panels |
| `screen_race_pause.cpp` | The pause window (GT3's PauseMenu, REPLAY OPTIONS, our longer list) |
| `screen_race_results.cpp` | Result rows, the result latched and posted to the menus, `leave`, race display strings |
| `screen_race_replay.cpp` | Recording / playback glue, replay view modes and REPLAY OPTIONS, the replay pad and the followed car, the camera manager's 60 Hz frames (music cut requests, view mode 1's handler), the dissolve and the post pass's grade |
| `screen_race_post_run.cpp` | After a run: the exit transition, prize / trophy and name entry, the automatic replay, back to the quick menu, its backdrop (play mode 2), pause EXIT (`run_kind`, `after_run`, `return_to_menu`) |
| `screen_race_analyzer.cpp` | The Analyzer: the quick menu's data logger and the theater's logger mode |
| `screen_race_online.cpp` | Online battle glue (field from the grid, events, synchronised start, prediction, ghosting); its state is `RaceScreen::net_` |
| `screen_race_license.cpp` | Licence tests (the race's judging, medals) and the disc's demonstration runs (DEMO, preview box) |
| `screen_race_events.cpp` | The pop-ups in the middle of the screen: lap times, split times, gaps, "+N Laps", "Pitting in..." (`lap_events`, `reset_lap_events`) |
| `screen_race_vr.cpp` | VR builds only: the stereo race (`vr_stereo`, `vr_base`, `render_vr_eyes`); see [VR](VR.md) |

## Loop

```
update(dt):
  FixedStep::advance(dt)          -> n ticks of 1/60 s (sim::kDefaultTickHz), at most 8 per frame
                                     (online: FixedStep::follow, by the clock since the synchronised start)
  per tick: prev = state; inputs = ControlMapper(pad) (the race reads the pad itself)
            field.step(inputs, 1/60)     (race::ExactField: one step of GT3's race on the exact core)
            lap timer, rumble
  render_state = interpolate(prev, state, FixedStep::alpha())
                 (the wheels turn by the tick's own turn, WheelState::spin_step: their spin angles wrap)
  camera, engine audio and rumble use render_state
render:
  View3D::draw(course items + car items) with frustum culling, draw distance, sorted blending, fog
  HUD over it
```

A race that cannot be set up (the disc's program unreadable, a course or car the
race step cannot run) shows "The race cannot start." with the reason; any button
(Cross, Circle, Triangle, Start) goes back to the menu. On the standalone
selection page the reason shows under the lists instead.

## Controls

| Pad | Action |
|---|---|
| Cross (pressure) | throttle |
| Square (pressure) | brake |
| Circle | handbrake |
| Left stick X / d-pad | steer (d-pad ramps at 2.8 lock/s, returns at 5/s) |
| R2 / L2 | shift up / down (manual gearbox; toggle AT/MT with Square on the selection screen or in the pause menu) |
| R1 | change view (in-car, in-car without the mirror, chase, far chase; see "Cameras") |
| L1 (held) | look back |
| Triangle (pressure) | reverse |
| Select | no race action (no respawn, as in GT3); in time trials, free runs, machine tests and licence tests the controller's own Select (the menu pad's) shows / hides the ghost |
| Start | GT3's pause window (see "Pause"); Select there opens our list: Resume, Restart, Watch replay, Save replay, Camera, Gearbox, Exit |
| R3 | performance overlay |
| Right stick, L2/R2, mouse | free-orbit camera |

Stick: 6 % deadzone, response curve `x^1.25` (fine control near the centre).
Pedals: 8/255 bottom deadzone. Buttons held without a pressure report give
full travel (keyboards).

Rumble (`race::RumbleModel`, `InputManager::set_rumble`): each step computes two
outputs and each player's Rumble setting picks one (`InputManager::gt3_rumble`:
`[playerN] rumble`; `auto` takes GT3 original on a DualSense, Modern on other
controllers; `race_rumble_mode`). GT3 original (physics §1.2), as GT3 drives a
DualShock 2: each race step's vibration level (`VehicleTelemetry::pad_rumble`,
0-127, set in phase 12 from the front tires' drive and brake forces, the dampers and
curbs under the front wheels, and a hit's rumble) goes to the large motor as it is, so
at most about half strength; the small motor never runs. Modern (ours): a wall or car
hit (`RaceVehicle::last_impact`, the stronger impulse over the car's mass, over
1.5 m/s) holds the large motor for 0.25 s at speed change / 8 m/s; off-road
(`RaceTrack::is_offroad` under the car, above 3 m/s) holds it at 0.18 plus the speed /
150 m/s (up to 0.2 more); wheelspin under throttle at up to 0.25; a slide (the
strongest grounded wheel's slip intensity, physics §8.3, from 0.35 to 1.25) at up to
0.35; a wheel on a curb (`RaceTrack::is_kerb`) pulses the small motor at 4 Hz plus
0.4 Hz per m/s. Either way a car the pad does not drive (an AI run-up, after the
finish; `VehicleTelemetry::pad_output` clear) sends nothing, and its motors stop.
Replays and demonstrations send nothing, and the Vibration option (`vib1`, the
quick menu's OPTIONS) turns it off. Impulse triggers (`set_trigger_rumble`, ours):
left = ABS-style chatter when a grounded wheel locks under braking, right =
wheelspin under throttle (`race_rumble`).

## Rendering

- Course: slot 1 scenery (one mesh per GTM1 node; the slot 27 licence course
  line as its own item, below) with frustum and distance culling (2.5 km),
  backdrop (slot 22, never distance-culled), sky dome (slot 21 only, as the original's main view; slot 24, a low-resolution
  dome, is the rear-view mirror's) drawn first as camera-centred background without
  depth writes. The frame is cleared to black like the original. Scenery
  packets take the node programs' GS state (`gtm1::Options::gs_state`): depth
  write (ZMSK), culling, alpha test reference and AFAIL (foliage). See
  docs/formats/course.md.
- Car: `cars/day/<model>` (eve / night models on dusk and night courses, see "Car lamps")
  with all LODs; body LOD by camera distance
  (< 30 m, < 80 m, beyond). The wheels are split into four models re-centred
  on their hubs and drawn with `car * translate(hub) * rotY(-steer) *
  rotX(-spin)`.
- Car placement (as GT3 draws it): the car model's origin (wheelbase
  midpoint, y = 0 = the body's lowest point) sits at the physics body height,
  which at rest is the ride height q (the front ride height in mm x 0.001 m, both axles; physics §4.2) above
  the ground. The screen's car frame is on the ground, so the body is drawn at
  `car * translate(0, q, 0)`. Wheel centres are at (±half-track, R + travel, ∓wheelbase/2)
  in the sim frame (half-track = half the tread less its per-side offset,
  formats/car_model.md "Wheel placement"), travel = `WheelState::suspension -
  VehicleSetup::Wheel::static_suspension` (+ = compressed, the wheel rises
  into the arch; airborne = max droop). In GT3's model frame this is
  y = R - q - (x' - x_s). R34: q 0.130, R 0.327, static hub 0.197 above the
  model origin. Not drawn yet: the visual camber (setting + the camber gain x
  travel) and the swing-arm half-track shrink, both small. Each tyre's arch
  shade (formats/car_model.md "Tyres") is its own model per wheel
  (`CarVisual::bands_`), drawn at the hub with the steering but without the
  wheel's spin.
- Car lighting (docs/formats/car_model.md "Materials"): lit packets carry the
  selected paint's material (`scene::Material`, from GTM1 +0x38[paint]); the
  renderer lights them with the course's Env0 car lights (`SunParams::day_car_lights`: those
  enabled by +0x10 bits 0-3, the first the main light, the others fills, e.g. a light from
  below with a negative diffuse; direction, ambient + global ambient, diffuse, specular),
  texel x min(Me + La Ma + Ld Md N.L + Ls Ms (N.H)^p, 2) (`DrawOptions::gt3_lighting`).
  Lamp materials (flag bit 0) add their emission only while braking (see "Car lamps").
  Night point-light courses: below.
- Car reflections: GT3's per-vertex Fresnel-like weight 1 - 0.8 cos (C1 =
  0.8) times the environment. GT3 renders a 256x256 sphere map every frame
  from 0.8 m above the focus car of a scene made for it: the slot 23 sky dome
  (dark, about 0.13; turned by Env0 +0x28 about +Y, then +0x48 about +X,
  presentation §I5) and the slot 5 scenery (about 0.06 bright), on
  black, unfogged and unlit. We render that scene into a 128 px cube map
  (`View3D::render_env_cube`, within 200 m) and look it up the way GT3 reads
  its view-dependent map (`DrawOptions::env_gt3_lookup`: the angle from the
  toward-viewer direction compressed to 2 asin(0.9 sin(theta / 2)), then tilted
  15 degrees up about the camera's x axis). Courses without slot 5 fall back
  to the drawn course (`[video] car_reflections = off`: a sky / scenery-colour
  gradient instead). The paint's weight is its texel alpha (CLUT 33..127 on
  the R34); the glass reflection's is the interior behind it (1.0).
  Not reproduced: the per-car reflection dimmer K (meaning open) and the
  destination-alpha rule for overlapping reflection chunks (measured on the
  R34: the DATE chunks overlap earlier ones only along strip seams).
- Cameras (GT3's player views are two camera points of the car's GTCI block, +0x68 count / +0x6C
  offset of 0x40-byte records, translate p at +0x00; fallbacks (0, -0.8, 0) / (0, -1.5, -6)):
  the eye sits at -p in the car model's frame (origin at the body height), looking along that frame's
  forward axis, 60 degrees horizontal over the 640x448 frame (44 vertical), near 0.1. In-car (point
  0, R34 1.01 m up, 0.2 m back): the body's attitude, own car not drawn, rear-view mirror. Chase
  (point 1, R34 6 m back, 1.5 m up): heading and pitch lagged by GT3's springs at 60 Hz (yaw 30 e -
  10 w trapezoid, pitch 20 e - 10 w), no roll, no camera lag in position. Change view toggles the
  two (the view point index wraps at 2; GTCI point 2 duplicates point 1 and,
  like points 3..19, only the replay director uses it); the mirror is shown in the in-car view
  only, never while looking back, and has no option. A new game starts in-car and each player's
  last view carries over to the next race (options +0x11C / +0x120). Look back (heading + pi,
  pitch negated, no mirror) is L1 on the race pad. Ours: change view also offers in-car without
  the mirror and a far chase (bumper -> bumper, no mirror -> chase -> far chase); `[race] views =
  gt3` keeps GT3's two. The last view is remembered for the session only (GT3 keeps it in its
  options). In-car vibration (GT3's in-car camera shakers, point 0 only, also
  while looking back): amplitude A = sum over the wheels of |slip ratio| 0.3 + |slip angle| /
  (pi/2), at most 3; speed factor S = speed clamped to 10..120 km/h over 120 km/h; on about half
  the 60 Hz updates each axis gets a target (r - 0.5) A 250 through v += ((u - u_prev) - 12 v)/60,
  x += v/60; the view turns about the eye by 8 S clamp(x) degrees of pitch and 2 S clamp(x) of
  yaw (no roll, no eye movement). GT3's RNG (s = 17 s + 17, out s ^ rotl16(s), seed 0x87654321,
  two steps per draw, including the replay shakers' draws) is reproduced. `[race] camera_shake =
  off` (ours) disables it. Roof and orbit stay as extras (presets).
- Road shade (docs/formats/course.md): the four wheels' road attributes A/B scale the car's light
  colours (per draw item) and the light shadow's alpha F; `race/road_shade.*` interpolates them
  from the road mesh (test `race_road_shade_disc`).
- Point-light courses (route5, route11, circles): `race/point_lights.*` picks each car's up to four
  lamps around its lamp index (`RaceScreen::car_lamp_index`: the front-left wheel's road attribute A
  read as a signed value by `race::lamp_index`, so the 0xFFFF strip at the start line is lamp -1;
  kept per car while the wheel finds no road; tests `race_point_light_index_rule`,
  `race_point_lights_start_line_disc`); the car is drawn with its own light set (`render::ItemLights`, up to four directional
  lights) and node 1 casts one shadow per lamp (shear -A78 (r.x, 0, r.z), alpha = lamp shadow
  alpha x w). The lamps' discs in the reflection map (`ReflectionDiscMesh`): GT3 draws them
  over the sky / scenery map before each car: of the same nine candidates sorted by weight, the
  first six point lamps (kind != 0); each an untextured 10-wedge fan centred on the lamp's
  sphere-map point r / |r + z|, radius (car-light record +0x40) / distance in map units, centre
  colour record +0x70..+0x7C with alpha a sqrt(w), the rim alpha 0, normal alpha blend, no
  depth test, the destination alpha kept, weakest last. We build each disc in the map's space as
  our lookup reads it (axis towards the viewer tilted 15 degrees up, map radius sin(phi / 2)),
  map its vertices back to world directions 1 m from the map's eye and draw them last into the
  cube (the focus car only; GT3 piles every car's discs onto the shared map).
- Headlamp pool (node 5): drawn additively in the ground frame when Env0 +0x58 is non-zero
  (alpha x E58).
- Reflection dimmer: GT3 multiplies the reflection passes by 1 - dirt (the car's dirt 0..0.8,
  zero on the wet Route 5); we use the garage car's dirt (others clean). It builds up during the
  race as in GT3: + S x (8 on tarmac courses, else 1) x 0.001 per second, S = the sum over the wheels
  on loose surfaces (indices 2-6, 12-14) of min(1, slip intensity), capped at 0.8; the result goes
  back to GT mode in `GameResult::car_dirt`.
- Recommended gear ("> n" after the gear digit): the race's AI plan runs for the player's car too
  (line bookkeeping and braking points only while the pad drives); when the car is faster than the
  coming target speed T (the next point's speed; in a braking zone min(current, next)) the hint is
  the automatic box's gear for T, blinking in braking zones (ai §16; `RaceVehicle::gear_hint`).
- Reverse (GT3's "back" action): Triangle (gt3 layout) / L2 (ours), pressure like the throttle;
  it is the pad frame's back axis, which the race reads (it sets the gear logic's reverse
  request); where the screen gives no pad, `RaceVehicle::set_reverse_request` sets the back axis.
  Test `race_control_reverse`. Replays carry it in the pad frames.
- Pit lane: the race's own driver kinds 2-4 drive the player's car and the opponents through the
  pit lane (ai §13); the HUD shows AUTO DRIVE.
- Pre-race shot (every live run with a standing-start hold: races from t 0.1, standing licence
  tests from t 0.7, machine tests by their hold; not replays, demonstrations or the 2P battle,
  which has no hold): GT3's starting camera plays `cameras/start.cam`
  (GTRC, one record, kind 0x420, read from the disc: `CameraRig::set_start_shot`) in the car's frame
  (curves x right, y up, z forward) at t = 1 - (C - 60) / 600, C the frames to the signal: three cuts
  in the one record (eye curves with steps at t 0.8 and 0.9, 204 keys): t 0..0.8 a straight move
  from 30 m ahead (3.0, 2.0, 30.4) to 6 m ahead (-2.9, 2.5, 6.3); 0.8..0.9 a low sweep along the
  side towards the rear; 0.9..1 round the front corner to (-1.6, 0.6, 4.0); the target curves aim
  near the car's centre ((0.65, 0.47, 0) -> (0.65, 0.28, 0) at 0.9), zoom 2.414 (45 degrees
  horizontal); the course name in capitals over the scene; cuts to the player view one second
  before the signal (captures of the original: the approach shot from ahead). Its two cuts
  (C 180, 120) and the cut to the player view (C 60) dissolve over 1 s from the last picture
  (`race::start_shot_on` / `start_shot_t` / `start_shot_dissolve`).
- Rolling / flying run-up (physics §9.4): the race's AI drives the player's car on its line with
  the automatic box until the hand-over (driver kind 1; the race decides when it hands the car
  back), also under the test autopilot; after the finish the race's end-of-run drivers take over
  (ai §1.1); the disc's demonstrations play their recorded pad frames; the attract demo is an
  all-CPU race (see "Title attract demo").
- Options (`menu::flow` [options]; the race options of GT3's options block, menus §10), wired:
  - 2P boost (none / low / high): the battle's catch-up (the race's rubber band for the trailing
    player, ai §9.3): low 0.15 / 20 m / 1000 m, high 0.20 / 10 m / 200 m
    (`FieldConfig::battle_catchup` 1 / 2).
  - 2P handicap h (metres): both cars on the line before the start, player 2 max(h, 20) m back
    and player 1 10 m for h > 0, the other way round for h < 0 (`FieldConfig::handicap_m`, the
    race's battle start on GT3's line 6).
  - 2P tyre damage: the battle's tire wear for both cars, not on dirt courses.
  - ASM / TCS off: the player's car (and player 2's) gets its ASCC flags / TCS gain cleared, in
    arcade single race, free run and 2P only; on keeps the car's own systems.
  - gear_auto: the default AT/MT outside arcade single race and 2P (they use the car page).
  - race / replay SE off: no engine, tyre or car one-shot sounds (race SE live, replay SE in
    replays, demos and the attract mode); race BGM off: no race music (the quick menus' music
    follows Menu BGM, music §3.3).
  - vibration per player (vib1 / vib2), off in replays.
  - wide mode: the 2P split default (`[race] split = auto`): top / bottom with wide mode off (4:3),
    side by side with it on.
  - ghost: whether a better run replaces the ghost at once (on) or after "Update Ghost?" (off);
    it never hides the ghost (see "Ghost").
  Not wired: GT FORCE force feedback and the steering-assist types (wheel input only; platform's
  force-feedback path), the 1P tyre-damage option (no race reader in the original).
- 2P battle, 4:3 top / bottom halves (matched to captures of the original): ordinal rank top
  left with the map below it, Fastest / Last / Lap (small label, large time) top right, speed then
  a dark panel with the gear digit, a ghost "> 8", AT / MT, the bar tachometer and a red shift
  light; in-car views get a rear-view mirror per half (240 x 55 at (200, 16) of the half) and hide
  their own car; 2P replays draw both halves with no HUD and no mark. No 3-2-1 countdown or
  "PLAYER n WIN!" (the original's 2P battle has neither: the clock runs from the start); a
  finished player's half shows AUTO DRIVE, then the RACE RESULTS panel over that half while the
  other player races on: the full-screen panel from the top of the half, car names only, a car
  picture per row (the car model rendered on the panel's blue, like GT3's car icon maker) and
  "Running..." for a car still racing. The race's end (both finished, or pause EXIT): the exit
  transition, the split-screen replay (always; only Start opens its pause), then the 2P RESULTS
  menu (see "After a run").
- Replay pause (GT3): the frame dimmed, CONTINUE / EXIT / REPLAY OPTIONS on a dark strip
  (buttons 124..236 / 244..356 / 364..508 x 192..216); REPLAY OPTIONS opens GT3's REPLAY OPTIONS
  panel (see "Replay Theater").
- Replays rebuild the race from its record and step it with the recorded pad frames (see
  "Replays"); on the steps the race's AI drove, the recorded frame is read and ignored, as in the
  game, so the AI drives again from the replayed state. Replays end exactly where their
  recordings ended.
- Arcade time trial: "Lap 1/1" with Best / Time (the capture); the run is that one lap (presentation §B7).
- Tyre smoke puffs up to 3 m that cross the ground line are split along it (GT3): the part above
  is drawn as is, the part below fades to alpha 0 one puff size down.
- Attract demo: no HUD, the replay mark.
- The shot look of view mode 1 (`race::ShotLook`, rolled at every cut by
  `ReplayDirector::roll_look`: replays in the avant-garde mode and the attract demo when it picks
  it; presentation §G6): levels (p 0.25: black = 0.175 r, white = 0.2 + 0.8 r', swapped in 5 %
  of them), the trail (p 0.1, 0.6 after an orbit camera cut: the previous picture at
  min(timer, 0.8), timer 0.5..2.0, fading at 0.02 a frame with p 0.5), the dissolve (p 0.4, 0.1
  for a music request 3) and the colour filter (on with p 0.1, kept with p 0.8; preset red,
  blue, cyan, (0.3), (1); saturation 1 / random / 0 at p 0.7 / 0.15 / 0.15). Drawn by the
  post-process pass below.
- **Post-process pass** (`View3D`, `DrawOptions::post_grade / snapshot / dissolve / trail /
  keep_last`; presentation §G6, §G16): when anything is on, the scene renders into its own
  target and a second pass draws it over the frame through the model shader's post mode
  (`params.w` 5): the grade (GT3's 8-bit table arithmetic, `gt3_grade`; CPU twin
  `render::post_grade_pixel` for tests), then the dissolve's frozen picture at
  trunc(128 x^3) / 128 (`race::DissolveClock`), then the last picture at the trail's weight;
  the HUD, sun glare and lamp glows are 2D, after it. Every race picture is kept (a GPU copy)
  for the next frame's snapshot or trail. Grade parameters (`RaceScreen::apply_post_pass`):
  replays and demonstrations only for a shot with levels or the filter, the course's Env0
  values (+0x980..+0x994, including black / white +0x98C / +0x990) for the half the shot does
  not set; live runs on the Env0 flag 0x80 with its Env0 values. Dissolves: mode-1 cuts (0.5
  s), every replay / demonstration start (0.5 s, from the live run's last picture; the
  original's also holds the 2D frame), manual car changes in modes 0 / 3 incl. the attract
  demo's (1 s), and course records in modes 0 / 2 / 3 (their +0x64, on-board records 1 s).
- Tyre smoke and dust (`race/tyre_smoke.*`): a 256-puff pool fed each tick by every car's wheels
  with smoke-emission intensity > 0.001 (`VehicleTelemetry::smoke_emit`, physics
  physics §8.3b — not the slip intensity `smoke` of physics §8.3: race used that until now and made
  tarmac launches 5-10x too smoky, since the slip intensity reaches 1 at a slip ratio of 0.39 while the emission intensity stays
  0 below 7.6 m/s of wheelspin and tops out at 0.5 on tarmac) (one puff per wheel per tick, at the contact point, no horizontal
  velocity); smoke class from the surface (tarmac / grass / gravel / sand / dirt / wet spray), per
  course from Env0 +0x4D0 (6 x 0x44: RGB, opacity = enable, size, growth, rise, spin, fade,
  gravity, alphamix, fade spread n) and +0x910 (colour shade floor x the road sun mask); strength =
  intensity x T1[k]; linear growth / fade / rise; camera-facing quads sorted back to front, alpha
  1.99 x strength x opacity x texel, course slot 26 texture (4 frames). GT3's per-puff cull (no
  near clipping exists for puffs): a puff is drawn only while its centre's view depth d is inside
  (near, far) and it is under two screen widths across (P0 x size < 2 d, P0 the projection's x
  scale), so a camera inside or right next to a puff does not see it (test
  `race_tyre_smoke_cull`). No fog, depth test on, no depth write, normal alpha blend. Route 5
  (Env0 +0x914 / +0x91C x / y divisors 2 / 2 on route5 and r_route5 only; each 1 when <= 0 or
  > 64): GT3 draws the puffs over black into a half-resolution buffer (the frame's depth copied
  in), without the ground-line split, and adds that buffer to the frame (Cd + Cs, nearest), so
  the spray never darkens; we add the puffs straight to the frame (`TyreSmoke` additive: the same
  where puffs do not overlap; at full resolution).
  Texture alpha: `Tex1::decode` / `GsMemory::decode_rgba` already map the GS alpha 0..0x80 to
  0..255, so race code must not double it again (the smoke, BLBd billboards, glare sprite and
  HUD map art did, which made every puff's texel alpha up to 2x: opaque cores).
- Replays: no HUD (unless REPLAY OPTIONS ITEM turns parts on: the replay layout, see "HUD");
  the `gt_logo_mini` mark at the bottom right (right edge 620, bottom 420) while no ITEM is on
  (the attract demo always), faded by `race::ReplayMarkTimer` (3 s off, 2 s in, 30 s on, 2 s
  out, 30 s off, ...; stands still while paused).
- Pause: GT3's windows from `pause1..3.pmb` (`race/pause_menu.*`, presentation §E): races
  CONTINUE / EXIT, time trials / licence tests / machine tests CONTINUE / EXIT / TRY AGAIN,
  replays CONTINUE / EXIT / REPLAY OPTIONS; EXIT asks "Exit race?" (OK / CANCEL) and ends the
  race into its quick menu; the selected button blinks (30-frame fade); SE cursor / ok / cancel.
  A finish that sets a session record (`race::sets_record` / `locks_retry` in `rank_finish`:
  the player's run, not a replay, demo or failed run, a time at or under the session's best
  lap before it, none yet counting: the visit's first time-trial or 0-400 m / 0-1000 m finish
  always; licences only with a medal; never the top-speed test; the stored records take no
  part) greys TRY AGAIN out (`retry_locked_`, applied in `open_pause`) until `restart()`.
  Licence and machine-test times join the session best (`best_splits_`) after the check.
  Select opens our longer list (restart, replays, camera, gearbox); Start also continues (ours).
  `GT3R_RACE="...,pause_sel=2"` opens it with the cursor on a button.
- Quick menus (`QuickMenu`, results.*) run on menu::flow::MenuController (menus §11): the
  icon row with the scroll, focus pulse, help layer, confirm blink and the menu SE, the race
  behind them where the original has one (see "After a run"); `prerace=1` opens the pre-race
  menu (screenshots).
- The env map is quantised like GT3's PSMCT16 map (5 bits per channel).
- Arcade cars run with their table-35 parts (`race/arcade_car.*`); the car page's RACING / DRIFT
  picks the tyre records: stage 10 ("_k") / 11 ("_l"), stage 12 ("_m") on dirt courses, + 13 on the
  wet Route 5 (test `race_arcade_drift_tyres_disc`). 2P battle: player 2's car, paint, AT/MT and
  RACING/DRIFT come from `GameRequest::car2`, `car2_color`, `transmission2`, `tire_mode2` (test
  `race_two_player_drift_disc`).
- Buttons: the race reads each player's race pad (`InputManager::race_pad`), whose buttons are
  GT3's defaults: Cross accelerate, Square brake, Circle handbrake, Triangle
  reverse, R2 / L2 shift, R1 change view, L1 look back (held), Start pause; we put the
  performance overlay on R3 (Select has no race action: no respawn, as in GT3). The per-player race layout (`[playerN] race = modern
  | gt3`, custom `[playerN.race]`) maps physical inputs onto them; `gt3` is the menu pad itself.
  docs/INPUT.md "Race controls" (the touch overlay presses race pad buttons).
- Car shadows (nodes 1 and 6, docs/formats/car_model.md): drawn in the
  car's ground frame (plane through the four wheel contacts, the car's
  heading), node 1 sheared by the course's shadow direction (Env0 +0xA70
  elevation, +0xA74 azimuth, +0xA78 length: (sin a, 0, cos a) x length x
  cos e with a = -azimuth, e = -elevation), colour Env0 +0xA60, alpha =
  vertex alpha x Env0 +0xA6C (x the road-shade factor F for node 1, see
  "Road shade"), 2 cm lift and a 2 % pull towards the eye as the
  depth bias. Each is a `DrawItem::in_order` item ahead of the body items (GT3: node 1, the
  headlamp pool node 5, node 6, then the body), so the body and wheels cover them.
  `[video] car_shadows = off` disables them.
- Speed unit: `GtData::imperial` (true for the NTSC-U data set we load; mph
  without a GT-mode session); `[race] units = kmh | mph` overrides. The speedometer
  scale is 50 N mph (faces `speed_120mp` / `160mp` / `200mp`, band every 5 mph;
  `Hud::speed_scale`); the odometers and the data logger follow.
- Arcade requests: `transmission` 0/1 picks AT/MT; `hud=0` (display off) also
  hides the time-trial FAIL / record overlays.
- Transparent geometry is sorted back to front per mesh.
- Course render state from node programs (see "Course render state" below):
  alpha-only passes are skipped, the road sheen is drawn additively, masked by
  the base pass's vertex alpha. Textures with transparent texels on course
  geometry are alpha-tested, except opaque packets whose program turns the
  test off: those are drawn whole (their texel alpha is only the frame alpha).

### Road "noise" (fixed)

The noisy, smeared road came from course passes the decoder drew as normal
opaque/blended geometry although the game draws them with special frame
buffer state:

1. **Road sheen** (chunk flag 4, alpha mode 0x58): a lit second pass over the
   base road vertices with a high-contrast noise texture. The game blends it
   as `Cs * Ad + Cd`, i.e. additively, weighted by the frame buffer alpha that
   the base pass wrote from its vertex alpha (which is 0 or 0x80 per vertex:
   20909/20909 sheen vertices on Trial Mountain coincide with a base vertex;
   40 % have base alpha 0). Drawn opaque it replaced the asphalt with noise.
   Now: `gtm1::Options::road_sheen` copies the base alpha onto the sheen
   vertices and flags them `kAdditive | kDestAlphaAdd`, lit by GT3's
   specular-only sheen program with the course's scene lights
   (docs/formats/course.md "Lighting", docs/dev/RENDERING.md).
2. **Alpha-only shadow masks** (FBMSK 0x00FFFFFF): red-filled textures that
   write only the frame buffer alpha (tree and bridge shadows that switch the
   sheen off). Drawn as colour they appeared as bright red ribbons (Seattle).
   Now drawn into the frame alpha only (`scene::kAlphaOnly`, see "Known gaps").
3. **Cut-outs**: course textures with transparent texels are alpha-tested
   (foliage rendered as solid green quads before), unless the node program
   turned the test off on an opaque packet: the GS then writes every texel
   (Route 11's perforated panels and Laguna's banners were cut away before).

Texture LODs were checked and are not involved: the second texture table of
course GTM1s is a quarter-resolution copy of every set (`gt3tool
race-texlod`), the decoder uses the full-resolution one; road UVs stay in
0..1 with REPEAT wrapping (`race-blend ... all`).

### Course render state (GTM1 node program ops)

| Op | Operand | Meaning |
|---|---|---|
| 0x20 | u8 mode, u8 fix | GS ALPHA (`A | B<<2 | C<<4 | D<<6`, FIX). 0x44 = `(Cs-Cd)*As+Cd`; 0x58 = `Cs*Ad+Cd` (sheen passes); 0x5800 also seen |
| 0x24 | u32 (little endian) | GS FBMSK. 0x00FFFFFF = write alpha only (shadow masks); 0xFF000000 = colour only (decals that must not disturb the alpha); 0 = all |

Survey: Seattle 42×0x44, 37×0x58, 6×0x5800; FBMSK 0 ×51, 0xFF000000 ×42,
0x00FFFFFF ×33. Trial Mountain: 35 / 35 alpha changes, FBMSK 0 / 0xFF000000
×61 each. `Draw::alpha_mode` / `Draw::fb_mask` carry these.

### Licence course line (course slot 27)

The dashed blue / grey / red line on the road of 12 licence tests (B-3 .. B-8, A-1 .. A-3,
A-6 .. A-8) is a GTM1 in the course file, not something built at run time
(`docs/formats/course.md` "Course line"). `course::course_line_model()` decodes it with
`gtm1::Options::carry_state` (its node 0 only sets state: no culling, no depth write,
colour only, which node 1's draw inherits as in the game); `CourseScene` keeps it out of
the scenery model and draws it as its own item right after the scenery (in order, after
the scenery's blended passes, frame alpha kept). It shows in every view of those courses
(live, demonstration, preview box, replays in all four view modes) and on no other course;
the replay view mode "course line" adds nothing (the game's mode 3 only names the mode).
Before 2026-10-21 the slot was merged into the scenery with per-node default state, which
culled the downward-facing strips, so no line showed. Checks:
`GT3R_RACE="tt_event=L0B0008,demo=1,rcam=0"` (frame 1080: a braking zone),
`tt_event=L0B0001` (no line), `gt3tool course-obj <iso> courses/US/L_trialmt1 <dir>
noscenery courseline` (the line alone).

### Car morphs (mud flaps, aerials, steering)

docs/formats/car_model.md "Callback morphs": the rally cars' mud flaps, two race cars' aerials
and the formula cars' steering arms. `load_car_model` with `CarModelOptions::morphs` leaves those
draws out of the body and returns them as `gtm1::MorphDraws` (per LOD); `CarVisual` draws them
from a small model of their own textures, one per drawing of the car (`CarLook::instance`: the
car and its ghost), rebuilt when `car_callback_values(state, setup)` change. The values come from
the race: `WheelState::flap` (the car record's flap pendulums, physics §8.8) and
`VehicleState::steer` over `VehicleSetup::steer_lock`; both are interpolated between ticks. Online remote cars are not stepped here, so
theirs stay still.

### Course animations

**Moving nodes.** The GTM1 +0x3C animators (docs/formats/course.md) turn, flip and morph
course nodes: flags and banners, spinning parts, flip-book crowds and pit crews.
`build_course_model(..., animated)` leaves those nodes out of the static model and builds
`gtm1::AnimatedNodes`: parts (each chunk a node can draw, per draw state, in its own space with
key frames 0 and 1, the model's texture indices) and a copy of the node programs.
`AnimatedNodes::update` advances the animators as the game does; `meshes()` re-runs the programs
with their values (`RunParams::operands`) and places the parts drawn (matrices, op 0x35 morph
weights). `CourseScene` rebuilds them once per drawn frame into models that share the scenery's
and backdrop's textures (`GpuModel(ctx, textures)`, `GpuModel::update_geometry`), restarts them
with every run and advances them by the race steps. The scenery's moving model is drawn with the
scenery's in-order item (`DrawItem::in_order_join`): its opaque passes before the scenery's
blended ones (Apricot Hill's flag poles behind the chain-link fence), its blended ones after them. Apricot Hill, the heaviest (19 nodes, 394
parts): ~17,000 vertices a frame, ~0.1 ms of CPU.

**CLUT animations.** Course texture sets animate palettes (Tex1 +0x24, docs/FORMATS.md "Tex1"): Route 11's
grandstand camera flashes, blinking signs, the start-signal lights. `gtm1::decode_texture_set`
decodes the textures they feed in the animations' load state; `gtm1::Options::clut_sets` /
`build_course_model(..., clut_sets)` hand the sets (`ClutTextures`: the player plus the
textures as palette indices) to the race. `race::ClutAnimations` (owned by `CourseScene` for
the scenery, backdrop and slot 5, by `WetRoad` for slot 9 and by `Billboards`) restarts them in
`reset_run_state` (every live run and replay: back to frame 0, then the start signal for the
run's countdown, 0 for rolling and flying starts), advances them 1/60 s per race step (`tick`,
so not while paused) and uploads the recoloured textures once per drawn frame
(`GpuModel::update_textures`, mipmaps rebuilt). The alpha test of a primitive whose texture an
animation feeds is decided over every frame's palette (`build_model`'s minimum alpha).

## HUD

Reproduces GT3's in-race HUD (`race/hud.*`), from the race workstream's HUD
research (captures of the original, `database/racedetail.db` rows,
`display.imgs`):

- **Frame.** The game lays out a 640x448 frame. Alignment 0 = left/top, 1 =
  right/bottom, 2 = centre; alignments with bit 4 (4/5) are pushed outward by
  106.667 units in the 16:9 option. We draw that 16:9 layout on our 640x360
  UI: `x' = (x + 106.667) * 0.75`, `y' = y * 360/448`.
- **Meter cluster** (racedetail type 3 presets): remote view (chase/TV
  cameras) = `revb_NN` tachometer centred right at (620, 320) 160x140 with the
  digital speed (580, 385) and gear inside; onboard view (bumper/roof) =
  `rev_NN` tachometer at (350, 436) 170x110 + `speed_XXX` speedometer at
  (290, 436), digital speed (350, 410), gear (320, 360).
- **Round meters**: face stretched to the box; pivot = box centre + offset;
  angle = a0 + 90 + clamp(v, 0, F + 8)/F (a1 - a0) degrees, screen y down;
  tapered red needle from n0 r to n1 r (base 0.04, tip 0.02 of its length); a
  segmented band at 0.95..0.98 r (500 rpm steps, 1000 on the 18k face), red
  from the shift-up rpm.

  | meter | offset | r | a0..a1 | needle |
  |---|---|---|---|---|
  | onboard tach (rev_NN, 151x105) | (0, 23) | 75 | 72.5..287.5 | -0.2..0.95 |
  | remote tach (revb_NN, 140x140) | (3, 4) | 73 | 29.85..238.34 | 0.38..0.9 |
  | speedometer (speed_*, 151x105) | (0, 23) | 75 | 72.5..287.5 | -0.1..0.95 |

- **Scales**: tach F = floor((rev limit + 1500)/1000)*1000, face = first of
  06..12, 18 covering it; speed scale (`Hud::speed_scale`) = N*80 km/h or N*50 mph with
  N = clamp(int(vtop*1.1 / 22.2222) + 1, 2, 4) (vtop in m/s, here estimated from rev limit
  and top gear), the face the first of 180 / 240 / 320 km/h (120 / 160 / 200 mph) that
  reaches it (N = 2 draws 160 km/h on the 180 face), band segments every 10 km/h or 5 mph.
  `race/meter_*.img` and `race/disp.imgs` are never loaded by the game.
- **Digital speed**: font `speed`, "%3d", amber (GS 0x801880D0), unit in sw_12.
  **Gear**: font `gearr`, unlit "8" under the digit, amber (R red), shift light
  `:` blinking 3 frames on / 3 off above the shift-up rpm. AT/MT: `meter` font
  glyphs `>` / `=`.
- **Layouts** (`race/hud_layout.*`, presentation §D4): the original's layout table, one
  row list per display mode (which parts, where, per camera view; test
  `race_hud_layout_disc` checks it against the disc). `RaceScreen::hud_data` picks the mode
  (`race::hud_mode`): races (single, GT, championship, online) the race layout, or the
  endurance one for 10 laps or more or a time limit; the arcade time trial; arcade / GT free
  runs; the free run before a GT race (free practice); licence; the machine tests; 2P;
  replays. `Hud::draw` draws each part only where its mode has a row; the item mask
  (`HudData::items`: live all but steering and accel / brake, replays the REPLAY OPTIONS
  ITEM bits) and `live` (position, WRONG WAY / AUTO DRIVE) gate the rest.
  `GT3R_RACE="hudmode=<n>"` forces a layout for screenshots.
- **Laps/times** (right, R = 615 + 106.667): the lap count "Lap n/N" (sw_20 value, sw_16
  label; "n" alone in free runs) at y 48; time lines from y 84, 28 apart: times `M'SS.mmm`
  in sw_16, labels in sw_12 max(width, 90) + 10 left of the value.

  | Layout | Lines |
  |---|---|
  | race | Best, Last, Lap |
  | endurance | Total, Best, lap-time list (3 lines from 140, sw_16 line height x 0.8 apart, numbered), Lap (216) |
  | time trial | Best (the visit's best lap), Time (the run's laps + the current one) |
  | free run | Record Lap (the session's fastest lap: dashes until a lap is done, kept across retries), Best (the visit's best lap: the same), list, Lap |
  | free practice | P.P. (the same, with the player's car picture 99 left of the label), Best, list, Lap |
  | licence | Best (the visit's best judged time: the only "lap" a licence test posts), Time, the three targets (no lap count) |
  | machine tests | presentation §D4: Best, Time / Record, Best speeds (no lap count) |
  | replay (ITEM time) | the lap count, Lap |

  Total = the run's recorded laps + the current one, the recorded ones alone once the car has
  finished; the Lap line goes at the finish in the layouts with the list or Last.
  `RaceScreen::lap_lists_` (per half, from the lap events, every run) and the session's fastest
  lap feed them: `session_best_ms_` (races: any car's) or `best_splits_` (solo runs, also the
  gaps' reference). The session best starts empty (no stored record seeds it: dashes, and no
  gap on the first lap) when the screen loads its course and car (`reset_session_best`), and
  lasts the visit: retries and replays keep it.
- **Mini-map**: course slot 33 `GTCM`: +0x10 width, +0x14 height, +0x18/+0x1C
  world X/Z of the map centre, +0x20 scale (px per metre), +0x24 rotation
  (degrees), +0x28 offset of a Tex1 (4-bit) image. Map pixel = (W/2, H/2) +
  scale * R(rot) (X - ox, Z - oz); north-up. Drawn at its layout row: (16, 80) races,
  (16, 96) endurance and free practice, (16, 32) time trial and licence, (16, 16) free run,
  machine tests and replays; `ecar_dot` for opponents, `1pcar_dot` for the player.
  Verified on 76 course files (centre line 100 % on the map art).
- **Races**: position "1st".."12th" (one string, nothing outside 1..12; no
  "/N") in font `rank#1`, 3 px glyph spacing, at (40, 64) (endurance and free practice
  (40, 60)) pushed to the left edge in 16:9 whatever the row's alignment, like the lap
  count (the original has no push there, presumably an overscan margin), black 75 %
  shadow at +4 first, live only; "Lap 1/N" is shown from the grid. Start countdown with the `count3..1` / `start` images.
- **Messages**: WRONG WAY / AUTO DRIVE live only and not in licence tests or the top-speed
  test (their layouts have no message display); the pop-ups' anchor and width limit (320 in
  endurance, 400 in free runs: wider texts squeezed) from the layout; the course title in
  sw_20 at 160 %, squeezed to the layout's width (500 races, 400 endurance), FINISH at the same
  row. The title is the course table's name in capitals (licence sections have their own; a
  course without one shows none) and follows `race::TitleClock` from every live start and
  replay start (not the attract demo or 2P): 1-5 s for runs from rest under the race rules,
  else 0.2-2.2 s, quadratic fades. In a live run the countdown, START and FINISH share its
  message, so the first of them ends it (a race's 10 s hold lets it play out, a licence test's
  4 s hold cuts it short); replays and demonstrations keep it and draw it whatever ITEM says
  (`RaceScreen::start_hud_clocks`, presentation §B3).
- **Replays** (ITEM bits): the in-car view shows the in-car cluster; outside views a small
  cluster (the speed at (170, 433) and the gear at (250, 420) on a 70 x 30 back panel at
  (210, 436), pushed left); car name (sw_16 grey, (16, 290) left in the car, (615, 433) right
  outside, squeezed to 300 / 320); steering (the `steering` art turned up to 90 degrees about
  (40, 350)); accel / brake (`brake_gage` / `accel_gage` bars filled from the bottom at (68,
  435) / (88, 435)).
- **Colours and pens**: the GS colour is the drawn colour (amber 0x801880D0 =
  RGB 208, 128, 24 on screen, red 240, 0, 0). The pen y of the sw_*, odo and
  speed fonts is the baseline.
- **Back panels** (flat polygons, black 50 %): remote view an octagon (3 px cut
  corners) over 484..620 x 400..430 (right-aligned); onboard view the gear
  panel 281..359 x 345..376 (top corners cut 3, straight to 30 % of the
  height, tapering to a 15 % inset).
- **Indicator lamps**: glyphs `!` ASM, `"` TCS, `#` hand brake, `$` oil of font
  `odo#1`, each followed by a space; ASM green (73, 204, 43), TCS (255, 184,
  20), the others red; unlit black 50 %; "acting" blinks (counter + 21 per
  frame, lit while positive as a signed byte). Onboard centred at (435, 430),
  remote pen (620 - 125, 410). The ASM and TCS lamps are the car record's own lamp
  states (`VehicleTelemetry::asm_status` / `tcs_status`), the hand-brake lamp the input,
  the oil lamp the car's live oil counter (worn: 300 km, `sim::oil_worn`). The Options
  menu's ASM / TCS off go into the player's race entry, so each lamp goes out with its aid.
- **Odometers** (font `odo#1`, cyan 98, 192, 192, centred, baseline): "%06d"
  the car's own odometer as it drives (`RaceVehicle::engine_condition`, 2 m
  units; GT mode: it starts from the garage car's) and "%04d" trip in tenths
  since the start. Onboard (205,
  428) / (205, 370) in the speedometer face, remote (605, 424) / (525, 424).
- **Gear panel**: the digit at half brightness while the clutch is not
  engaged; a ghost "> 8" after it, lit red with the AI line's recommended
  gear when the player is faster than the line's target speed (see
  "Recommended gear").
- **Rear-view mirror** (onboard cameras, not in replays): frame rect (200, 20)
  240x64 with a 1 px black outline; the camera's view with z flipped (looking
  back, mirrored), 1.3x zoom, far 120 m; we leave the player's own car out.

- **Boost meter** (turbo cars): `boost` face 64x64 at (530, 436) onboard /
  (475, 436) chase, needle 0..2 over -45..200.32 degrees; the value is the
  pressure ratio (1.0 = the face's "0 kg/cm2", i.e. 1 + physics' boost).

Not reproduced yet: the needle's 8-pass motion blur.

## Pop-ups: lap time, splits, gaps

As the original shows them (presentation §D6). The course's split points are the GTRW markers of kind 2 with a second field of 0, in table order,
at most 3 (`track::display_checkpoints`; every race course has 2 or 3, test
`race_split_points_disc`). `track::LapTimer` reports each tick's split-point and line crossings
(`crossings()`: the point, the lap index, the race clock and the time since the lap's start;
only the car's next point counts, once on its timed laps; the line carries the number of
points passed). The field lists every car's crossings of a tick in race order
(`RaceField::last_crossings`), the player's own timer through `RaceTrack::lap_crossings`.

Each player half has an `EventDisplay`: eight message slots and two timers, the queue's end
and the parallel group's start, both counting down 1/60 s a frame. A queued message starts
when the queue runs out (one after the other), a parallel one with the current group (at
once); messages show for 3 s at full alpha, centred on the anchor ((320, 130) of the frame;
2P halves (320, 75) of 640 x 224 or (213, 165) of 426.667 x 448, squeezed to 320 / 384 units),
the pen on the baseline. Per tick the display takes, in this order:

| Message | When | Look |
|---|---|---|
| lap time "m'ss.mmm" | every lap of the followed car (races, 2P, time trials, free runs; not a live licence test; a licence replay at the run's end) | sw_20, green (73, 204, 43), queued |
| "T<n> m'ss.mmm" | every split point (licence tests number on through the laps) | sw_16, green, queued |
| "+N Laps" | a car passing a point on an earlier lap than the first car through | sw_16, red, parallel, 25 below |
| gap "+m'ss.mmm" / "-m'ss.mmm" (the font's dash) | races: behind the first car through a point on the lead lap, or (the first car) when the second passes; time trials / free runs: against the session's best lap so far with its split times (none before the first lap: no stored record; kept across retries) | sw_16, red (240, 0, 0) / blue (128, 128, 255), parallel, (+-37, +25) with the picture of the car in position 1 (behind) or 2 (ahead) at (-+62, +20), 64 x 32, blinking off 12 frames three times; (0, +25) without |
| "%s Pitting in..." | any car entering the pit lane (both halves) | sw_16, green, queued (no fade) |
| machine tests 0-400 m / 0-1000 m: "T<n> m'ss.mmm" at 400 m (0-1000 m), then the time to the goal "m'ss.mmm"; the top-speed test none live (its HUD has no event display), in replays its splits and at the goal "312.4km/h" (sw_16) | the timing points of `race::MachineTest`; nothing at the line, no gaps | the time like a lap time, the split like a split; queued |

`RaceScreen::lap_events` (every tick, after the step) builds them; `reset_lap_events` clears
them for every run and replay. The picture is the car's model (`draw_car_icon`, slots 12 / 13)
in `Hud::event_icon_rect()`. The display is drawn with the time item only (`Hud::event_row`):
live play always has it, replays only with REPLAY OPTIONS ITEM time (default off).

**Replays** (presentation §D6, §G; `race::lap_event_rules`, `race::event_display_of`): the
re-run posts as the live run did, for the car the cameras follow (`replay_focus_`: MANUAL's
choice, TOP / AUTO, an avant-garde cut), on the single view's display at (320, 130), in every
camera view; the replay layout's second event display (outside views, (615, 400)) is never
posted to in the original, so it draws nothing. Differences from live: no gaps to the best lap
(time trials, free runs) and no lap counts for the session (`best_splits_`,
`session_best_ms_`); a licence test's time at the live run's end (`music_license_end_s_`, not
failed) as a lap time; the machine test's marks re-run in playback (the result stays the live
run's), the top-speed test showing its split times and, at the goal, its top speed
(`machine_speed_message`, "%.1f" and the unit, from `hud_top_kmh_`, which replays now keep
too); a solo run's car posts nothing after its end (`replay_run_ended_`: its laps done at the
line, or the recorded end-of-run driver kind once timing began; races: the field freezes a
finished car's timer). Not reproduced: demonstrations and the attract demo (the original's
demonstration layout shows the display in the in-car view only), 2P replays (no HUD on
either half), a licence replay without its live run (e.g. from the theater: no pass time),
qualifying's icon rule, and the original's quirks for two cars passing a point in the same
tick out of time order. Screenshots: `ritems=4` (time), `rcam=2` (the in-car view instead of
the replay cameras), e.g. `course=trialmt,car=r34_gtr_v_99,auto=1,warp=200,laps=2,watch=1,
rwarp=169.5,ritems=4` with `GT3R_SHOT_FRAME=30` (the first lap's time).

## Loading a race

As the original (menus §6): there is no course picture or "Now Loading" text. The menu leaves with its Fadeout; the game keeps drawing it over the last menu frame
every frame for as long as the race loads, and 180 frames (3 s) after the menu was left the
"still loading" square (16 x 16 at (20, 20), grey 0.9, then black, every 32 frames) blinks
in the top-left corner; the race then appears as a cut. The menu screen hands its frame and
running transition over (`menu::flow::race_loading_picture()`, filled in
`start_game`); the race screen draws it (`render_loading`, the frames our blocking load took
caught up, `LoadingPicture::frames_for`) until its run is loaded. The load is split over two
frames (`init`, then `begin`) so the picture is presented before each blocking part. Races
started without a menu frame (Quick race, online) show the Fadeout on black. Before, the race
screen's own course / car picker was drawn for the frame before the load and stayed on screen
while it loaded. A scripted menu run (`GT3R_GT3MENU_SCRIPT`) reaches the race two frames later
than before. Our loads take a fraction of a second, so the square (3 s) is normally not reached.
Retry and the quick menus' Start are unchanged: the original restarts under its DelayFadeout
(nothing for the first 10 frames) or the quick menu's last frame, then cuts.

## Audio

`EngineSound` with the car's `CarDatabase::engine_sound()` id and variant
(e.g. R34: 21609, turbo0), updated every frame with the interpolated rpm and
effective throttle; the view gain follows the camera (onboard / chase).
Stopped on pause and when leaving the screen.

## Replays

**Format 11: races on the exact core replay from their own record.** A run on the exact core
(`race::ExactField`) records itself as a `sim::exact::RaceRecord` (`sim/exact/race_record.h`), the
replay file's block after the battle block: what the race was built from (the race configuration
block, the race-information words, the race mode, the course by label, every car's `StartEntry` as
it was: equipment with its parts and settings, pedal style, wheel type, ASM / TCS, condition words,
grid slot), the fingerprint of the program's tables it ran on, the behaviour it ran with (the rules
and step rate, `sim::Behaviour`: the disc's release's rules at 60 steps a second, online NTSC-U's), a
fingerprint of the game data it was built from (`exact_data_fingerprint`: the course file and every
entry's database records), every car's pad frame of every step (changed fields only; a CPU car's stream
stays empty; the free run ghost's extra steps' reads apart), the game's own changes to the race between
steps (`RaceChange`: the start countdown, the pause menu's AT / MT, a seeded engine condition, an AI
take-over) and `race_hash` after the last step. Version 3 also keeps the solo modes' recorders as the run
starts (`sim::exact::SoloRecorders`, their record memory stored sparse) and the run's own pad frame recording
(`RaceRecord::run_recording`: the player's device's block as the race's end closes it, GT3's recorder),
which a whole-run ghost plays; a version 1 or 2 record's ghost car drives its recorded frames instead. Playback (`ExactFieldOptions::playback`, set by the
screen through `exact_playback()` wherever it builds an exact field) builds the race from the record,
makes no grid, car or event decision again, and steps it from the recorded frames and changes; its
pads and inputs are ignored; at the last frame it logs whether the race's hash is the recording's
("the race matches the recording"). A replay plays on the physics it was recorded on: a record needs
the exact core, the program and the game data it was recorded on and its rules (`RaceScreen::can_play`,
`build_playback`: refused with the reason otherwise, "This replay cannot be played: recorded with ...",
never played on another program, data or rules). Saved ghosts follow the same rule (a ghost of
another program's or rules' run is not offered). **Files of versions before 11 are refused** (`ReplayTooOld`,
"This replay was recorded with an older version and cannot be played."); the Replay Theater lists them
with "Recorded with an older version", not playable (they can be deleted). Tests: `race_replay_exact_record` (the record through a file, disc-free),
`exact_replay_round_trip` (disc: a pad-read GT race with a gearbox change and a seeded condition, a
rally from inputs, a 2P battle with player 2 on the pad, an AI take-over, the autopilot: every step's
hash and every 300th step's records equal, end matches).

**Ghosts on the exact core** are GT3's: the ghost is a car of the race. A whole-run ghost (time
trial, licence test, machine test) is the session's best run's record (or a saved replay's, when it
can play): `ExactFieldOptions::ghost` adds its car 0 entry after the players, the solo conductors'
entry set-up flag it a ghost (F +0x3EA), and from the first step it plays that run's recording on the ghost's
device as GT3's playback does (`SoloRecorders::run_to_ghost`, the ghost taking a whole run: every
recorded frame but the last, then empty frames; its flying start's run-up by the race's AI as then); ghost cars are not opponents (standings, crossings,
pit entries). The screen draws it from the race (`exact_ghost`, the draw mode: not while the
computer drives it). The free run's lap ghost is the free run's ghost car (exact-modes). A replay of a
run with a ghost has the ghost car's frames in its record. Test `exact_replay_ghost` (the ghost car
equals the first run's car 0 on every step of that run, the player's car is untouched, the replay of
the run with its ghost matches).

Around the record the file keeps what the theater and the memory-card pages show (`replay.h`):
course, car, gearbox, paint, kind, laps, total time, power, save time, the engine condition the run
started with (physics §10.3), the grid by power-to-weight (GT mode), a 2P battle's Options (laps,
catch-up boost, handicap, tire damage, player 2's car, wheel and paint), the best lap's tick range,
and the players' inputs per tick for the HUD (the inputs whose frame the race read). A race replay
rebuilds its field from the record wherever it is played (`build_vehicle` while replaying;
`live_battle` otherwise), so a GT race or a battle in the theater or the Analyzer starts as it did.

- **Engine condition across runs** (gt_mode §13): a request that carries the garage car's
  condition (`GameRequest::car_condition`, GT mode's `fill_condition`) seeds every live run
  (`restart`) with `run_condition_`; when a live run ends (a retry, a rebuilt field, a replay
  starting, leaving) the screen takes the car's advanced words back (`take_live_condition`),
  so the next run starts where the last stopped, and replays run with their own start words.
  The result reports the words the live runs left and the odometer units they added
  (`GameResult::car_condition`); modes without it run every run with a fresh engine.
- Saving: every race's Save replay (pause list, the quick menus' SAVE / Save replay of GT mode,
  arcade, licence, quick race, 2P) writes at once (a toast, no slot page) to
  `<user data>/replays/<course>_<car>_<stamp>.gt3replay` (two saves in one second get the next
  stamp). That folder is all the theater lists: no slots, no capacity limit.
- Selection screen: Circle lists saved replays; Cross plays one.
- During playback: the replay cameras, the original's replay pad (see REPLAY OPTIONS below),
  Start pauses (the replay pause; Select there: Resume, Restart replay, Camera, Save, Stop
  replay, Exit).

### After a run

As the original's race drivers go on after every run (presentation §A8, §C4, §C5, §E5, §F,
menus §11.1; `race/post_run.h`, `screen_race_post_run.cpp`; tests `race_post_run_sequence`,
`race_menu_backdrop`, `race_pause_exit_finish`, `gtmode_synth_prizes_before_the_replay`):

- The live run ends (its result screens' time or confirm, pause EXIT): the race-exit transition
  over its last frame (`begin_exit_transition`, 10 frames, then the grey wash), then
  `after_run`: the result latched (every run: the replays re-run it), the prize page (a finished
  GT race or championship round, `game::queue_race_prize`) or the arcade win's cup
  (`game::queue_arcade_trophy`), the record (`record_run`) with its name entry, all shown by
  `make_presentation_screen()`; then the run's replay by itself (`start_auto_replay`), and at its
  end or its pause's EXIT the same transition over its last frame and the quick menu
  (`return_to_menu`). `GameResult::prize_shown` keeps the menus from showing the page again.
  - Arcade race: cup (a win) -> replay -> result menu. 2P: replay (always) -> result menu. GT race,
    championship round: prize (finished) -> replay -> result menu (the championship's points page
    is not reproduced). Time trial, free run, machine test: name entry (ranked) -> replay -> menu.
    Licence: the menu, with the name entry (ranked) over it, no replay (the preview box). Free
    practice before a GT race: the transition -> the pre-race menu.
  - A replay plays once, the whole recorded run (the auto-drive after the finish included), with
    no result panel, record overlay or medal; only Start opens its pause (CONTINUE / EXIT / REPLAY
    OPTIONS); music: the replay BGM switch, race songs at 0.7 (the run's music is stopped first).
- The quick menu's backdrop (`start_menu_backdrop`): time trial, free run and machine test menus,
  and the GT race / championship pre-race menus, run the race in play mode 2 (a replay: records
  off; the view mode picked as for the attract demo, `pick_attract_view`; silent): a run alone is
  its last run from the start, which never ends: once the recording runs out the car gets a
  cleared input frame (no steering, throttle or brake) and rolls on; before any run its start with
  no input; a field stands on its grid (not simulated) under the director's cameras. The result
  menus (arcade race, 2P, GT race, championship) draw no race (their layouts' opaque checker
  background); the licence menu its preview box. Items that keep the menu (save, load ghost)
  start the backdrop again, as the original's driver loop does.
- Licence: a run left with EXIT puts the demonstration in the preview box (the original loads it
  in the run's place) and disables REPLAY until the next run (below).
- Pause EXIT in a free run with a completed lap, and in the top-speed test once the car has moved
  (top speed over 1 m/s) before its goal, is the run's finish (`pause_exit`, `exit_as_finish`):
  the record overlay (5 s; the top-speed test 0.1 s), the end-of-run driver (free run:
  `RunEnd::FreeRunEnd`), the recording closed at the press (the replay ends there); a second EXIT
  ends the run. `exit_laps=N` presses it.

### Replay Theater

Title > Replay Theater (`replay_theater` page, menus §13; captures of the
original's pages: theater, DEMONSTRATION list, Analyzer top, REPLAY OPTIONS):

- **Load Replay**: the saved replays on the original's DEMONSTRATION page art
  (`memorycard.mbl` page `demo_loader`, `menu/flow/theater_page.*`): 8 icons to a row, rows
  scrolling, the focused file's info (Size, Date, Mode, File Name, Car, Power, Course, Record =
  best lap, Laps), title "Load replay". Confirm plays it at once; Start deletes it after one
  "Delete OK?" (the file page's confirm art); cancel goes back. The original goes through the
  memory-card file page (slots, confirm, "working"); ours is deliberately native storage
  (`race/replay_library.*`, app backend `game/replay_theater.*`). The replay plays once
  (`GameRequest` mode `theater_replay`; Start = the replay pause with REPLAY OPTIONS, EXIT or
  its end) and the theater page comes back with focus on its first item (as the original).
- **Demonstration**: the disc's demonstration runs (`demo/<event>`, 48 licence tests then 10
  arcade time trials, the original's order, names and info) on the same page; one plays as its
  licence test / time trial driven by the demo's inputs, TV cameras (the REPLAY OPTIONS view
  mode; it opens like any replay, see "Replay (TV) cameras"), the mark only, Start =
  replay pause, then back. The demonstration's car is the demo's own car record (parts and
  settings: the arcade time trials' dirt tyres, stage 12, differ from the event car's); the
  flying start's run-up is driven by the race's AI (physics §9.4, also for time trials),
  the recorded frames ignored until the line. The run is the race's own replay of the save (see
  "Demo runs").
- **Analyzer** (`logger.mbl` page `top`, GameFlow control "logger"): TRACK A / TRACK B panels
  (Car, Power, Course, Time, Date, Mode, File Name) and REPLAY / GRAPH / LOAD A / LOAD B / EXIT;
  LOAD opens the list titled "Load track"; REPLAY / GRAPH need a track (B on another course is
  left out) and start the race screen in mode `logger`. A replay on the exact core is played
  again on its own field with the data logger on (`exact_logger_track`: ExactFieldOptions::logger /
  logger_record, race info + 0x70 / + 0x74; the logger's tables from `exact_logger_tables`), its car
  0 binned after every step as GT3's Analyzer pass does (`race::LoggerBinner`: bins of lap /
  16383, at least 0.5 m, speed, pedals, gear, position, the 1 2 1 smoothing; `logger_track`), the
  sectors at the course's logger marks (CourseQuery::logger_mark), SECTOR TIME / AVER. SPEED from
  the logger's table; the bins' frames give REPLAY's seeks and cursor. Two time trial, licence test
  or machine test replays run as GT3's Analyzer runs them, in one race (`exact_analysis_pass`,
  ExactFieldOptions::analysis): car 0 plays track A's recording on the player's device, car 1, a ghost
  car with track B's car, plays track B's on the ghost's (`SoloRecorders::analysis`; only track A's
  record's changes are made), both binned after every step by their devices' frames, the logger's
  tables kept. REPLAY plays that race again (track A's replay built as the Analyzer's race with the
  pass's tables); a seek jumps to the logger entry of the sector the cursor is in
  (`exact_analysis_jump` -> `ExactRace::logger_jump`, as GT3's REPLAY jump: both cars from
  the entry's snapshot, their playback sought to its frame, the race simulated on from there;
  `RacePlayback::seek` and `ReplayPlayer::seek` move the record on without playing the frames
  between), so no jump replays from the lap's start. Test: `exact_game_analysis` (disc). Free run
  replays keep a pass each, as above (GT3's free run Analyzer plays its best lap stores). A replay
  that cannot play on this disc is not offered. The screen is logger1.pmb page
  `replay_logger`: bottom band with per-track status (SPEED, LAP TIME, SECTOR TIME, SECTOR AVER.
  SPEED at the cursor) and the sector bar (`icon_sector<n>`; sectors = the points of the
  course's `logger/<course>` LGM0 map, formats/course.md). GRAPH: speed / accel / brake traces of
  A (red) and B (blue) in a window round the cursor, the LGM0 picture with both cars, Left /
  Right move the cursor, L1 / R1 (L2 / R2) jump sectors, Circle / Cross open EXPRESSION (SPEED /
  ACCEL / BRAKE on/off, DISPLAY A/B/AB). REPLAY: track A's lap plays (looping) with B as the
  translucent ghost (drawn with A's model), START / STOP / SLOW (x0.25) / CAMERA. Triangle back to
  the top page. The input details and the drawing of traces, dots and the sector bar are ours
  (they are not specified; no captures with data, the PS2 cards were
  empty). `GT3R_RACE="lcursor=<m>"` places the cursor for screenshots.
- **REPLAY OPTIONS** (`race/replay_options.*`, presentation §G): MODE (view modes 0..3: the
  manual's Normal, Video, Race, Training), TARGET (MANUAL, TOP, AUTO: `race/replay_focus.*`,
  below), ITEM (HUD parts shown in
  replays, on the replay layout: meters, car name, lap count, current lap and the followed
  car's pop-ups, map, steering, tyres, accel / brake; `GT3R_RACE="ritems=<bits>"` sets them for a screenshot without storing
  them), SOUND (replay BGM / SE), 2P SCREEN; stored in `[options]`
  (`replay_mode`, `replay_targets`, `replay_items`, `replay_bgm`, `replay_se`, `replay_screen`; view mode 1's pad: `replay_level`, `replay_music_cuts`).
  Modes 0 / 3 run the GTRC course shots (identical: no race lines were found for Training);
  mode 2 the same, framing the followed car with its battle partner, with, at a new shot, an
  on-board cut on the partner's car until the shot ends (p 0.07, only with a partner); mode 1
  (avant-garde) stores BGM on and cuts on the music (below) or the pad: AUTO a random other car, a free
  course shot (u <= 0.8, x s left, 15 m from the last) with a roll of -10..10 degrees (a wide
  zoom with p 0.17, a +-100 degree roll swing with p 0.1), or with p 0.25 one of five
  procedural cameras (`race::ProceduralCamera`, below), and the shot look. SCREEN
  single shows a 2P replay as player 1's view only. `GT3R_RACE="ropts=<row>"` /
  `"rmode=<n>,rtarget=<n>"` for screenshots (log lines "Replay cameras: car, partner").
- **The replay pad and the followed car** (presentation §G3; `RaceScreen::replay_pad`,
  `step_replay_focus`, `aim_replay_camera`; `race::ReplayFocus`, 60 Hz): nothing changes the
  view mode (only REPLAY OPTIONS; no mode indicator). Modes 0 / 3, and 2 with MANUAL: Up / Down
  follow the next / previous car (the player's car, then the opponents; wrapping). Modes 0, 2,
  3: Cross toggles the driver's view (the view last driven with, on the followed car) and back
  to the replay cameras. Mode 1: Cross, Circle, L1, R1 cut; Up / Down +-0.1 the race-sound
  level of the music's marked sections (none on the disc), Right / Left the music-timed cuts
  on / off (request 4 / 3: only the dissolve's odds differ), stored as `[options]
  replay_level` / `replay_music_cuts` (the original's options +0x114 / +0x118,
  `game::ps2_replay_pad`; the attract demo uses off). L2 / R2, Square, Triangle do nothing. These are the pad's own buttons (the menu pad), not the race layout's
  actions (under "modern", R2 accelerates but is still R2 in a replay). TOP / AUTO (mode 2): interest scores per car (TOP: the leader; AUTO:
  rank, random growth, places gained / lost, battles), re-picked no sooner than 4 s after the
  last switch, with a chance growing to certainty at 30 s, applied when the followed car's 2 s
  hold is over, the camera's shot is 3 s old (`ReplayDirector::camera_fresh`) and its switch gate
  is open (`ReplayDirector::switch_gate`). Battles: a car near another (10 m) for 2 s after 4 s
  alone becomes a pair (ends beyond 30 m); in modes 1 / 2 the camera aims between the pair and
  widens to keep both. A new car is panned to over 1 s (mode 0 / 3: 0.25 s); a manual change
  cuts to a new shot. The attract demo in mode 0 moves to the next car every 5 s. 2P replays
  keep each player's car.

### Replay (TV) cameras

The course's own camera script, GTRC (course slot 34), decoded by the race
workstream (all 80 US courses, 4493 records, no errors):

- Header: +0x14 course id (= GTRW id), +0x18 lap length L (= GTRW length),
  +0x1C script end T, +0x24 record count, +0x28 records (0x80 bytes, sorted),
  +0x2C/+0x30 a second list (course stills; never shown in play), +0x34/+0x38 zones
  (gate the automount camera only).
- Record: +0x00 kind, +0x04/+0x08 start/end track distance D, +0x0C duration,
  +0x18 on-board mount, +0x28/+0x2C zoom min/max, curve pointers +0x34 zoom,
  +0x40/44/48 camera x/y/z (world), +0x58/5C/60 look-at x/y/z.
- Curve: `u32 n; n x {f32 t, f32 v}`, piecewise linear in t with linear
  extrapolation.
- Kind: `&3` mode (0 trackside, 3 on-board, 1 car-local), `&0xC` look at the
  car (+0.4 m) instead of the target curves, `0x10` zoom = distance x zoom(0).
- D = k*L + s with a lap variant k (nvar = max(1, int(T/L) - 1 - int(first.start/L))): 0
  before the car's first crossing of the start line (D = s: the first records, from
  first.start, cover the start and run on across the line), then ((lap - 1) mod nvar) + 1 on
  lap `lap`, so consecutive laps get different cuts (`GtrcScript::lap_variant`,
  `race::lap_counter`); t = (D - start)/(end - start). A record that aims at its look-at
  curves (kind & 0xC = 0) with a duration of 1 s or more runs on time instead: t = seconds
  since the cut / duration (`ReplayDirector::shot_t`).
- The shot picker of view modes 0, 2, 3 (presentation §H8, `ReplayDirector`): every replay and
  demonstration starts with the camera on the script's last record and a reset, so its first
  frame cuts to the record at D whatever its progress (`GtrcScript::record_at`); with none
  (a car further back than first.start) the camera turns on board, the followed car's in-car
  view, and the next frame takes another lap variant's record (u <= 0.6, held 3 s) or stays
  on board until the start record. A shot then runs until it runs out (its u past 1, or its
  time): modes 0 / 3 cut at once, mode 2 once the shot length is 1.3 s or more or a music
  request is pending; the cut takes the record at D (u <= 0.6, not the same one), else another
  variant's, else turns on board. A new followed car re-picks the same way as a reset.
- zoom = 1/tan(half horizontal FOV) over the 640-pixel frame; the 448-line frame gives
  `fovy = 2 atan(0.7/zoom)` (`ReplayDirector::zoom_fovy`; the player views use the same).
  In the telephoto shots (100-150 m, down to 1.4 degrees across) the cars' decals, a few mm
  above the body, stay in front because the depth is reversed like GT3's Z buffer
  (docs/dev/RENDERING.md, coordinate conventions).
- +0x64: seconds of dissolve when a shot of the record ends and the picker cuts away (modes
  0 / 2 / 3; 1 s from an on-board shot; Akasaka 0.5 on most records, licence courses 0.3,
  most race courses 0).

Courses without a script fall back to placeholder trackside cameras every
90 m. On-board records and the in-car turn use the car's camera mount (GTCI, through
`race::ProceduralCamera`'s mount kind); mode 2's cut on the battle partner a bumper view.

**View mode 1** (presentation §G2): the camera manager runs every 60 Hz frame
(`RaceScreen::update_camera_manager`, live too): the race music's markers step
(`race::MusicMarkers`, music §8: the playing song's ads.inf tracks at its audible position +
1/30 s), then `race::CutClock` gives the shot length x (the time to the next track-3/4
marker, or the countdown / 60), mode 1's handler (`avant_garde_frame`) cuts on last frame's
request (not during the standing-start hold), the view's reset (replay start, every REPLAY
OPTIONS apply) or the pad, and the clock makes the next request (a track-3 / 4 event; without
a marked song a countdown from 60 frames, then every 121). With BGM off the race's requests
still play, silently (`race::RaceMusicLevel`), so the cuts keep the markers. Replays re-run the
start sequence (music §2.5: the start jingle over the hold, the song from the signal), and their
finish jingle is the followed car's (`race::FinishCue`). View mode 1's on-board car mounts
(GTCI flags bit 1 clear: in-car, bumpers, the driver's head) hide the followed car
(`CameraRig::replay_on_board`), as the original's car passes skip the camera's target then. A cut
(`ReplayDirector::avant_garde_cut`) draws as the original (two generator steps a draw): car,
course shot and its extras, procedural pick (table by x; the hold always the car mount), the
look. The procedural cameras (`race::ProceduralCamera`, own generator seeded from the clock per
shot, stepped at 60 Hz, following the cut's car only): Roadside (still, near the road, on the
car's path or beside it; the road from `Track::probe`), Hover (carried, 1-9 m out, 3-7 m
up, spring wobble), Orbit (sweeping 100..760 degrees over x s, ending behind),
Telephoto (2.5-6.5 m past a bumper, speed shake) and a random GTCI mount 3..n-1
(`race::car_camera_mounts`, all 20 records). Course shots get the roll as Rz in eye space.

## Races on the exact core

Every race runs on the exact core: `race::ExactField` (`race/exact_field.*`) is the `RaceField`
(`RaceField::create`), over `sim::exact::ExactRace`, GT3's own race step on its own car records
(docs/dev/PHYSICS.md, `sim/exact/`): single races in GT mode and arcade, rallies, 2P battles, the solo
modes (below, "Solo modes") and online races (below). A race the exact core cannot set up (the
disc's program unreadable, a course or car it refuses) shows the error page with the reason and
goes back to the menu; there is no other physics. Every race steps 60 times a second
on every disc (`RaceField::tick_hz`, the one place the screen takes the rate from). The races the
player drives follow the rules of the disc's release (its program version: NTSC-U, PAL's rule
branches, NTSC-J's); online races and the game's demos (the attract demo, the quick menu's and the
theater's demonstrations, the licence preview) follow NTSC-U's on every disc
(`ExactFieldOptions::rules`: a copy of the program data with that `sim::Behaviour`). The log's
"Race (exact)" line names the rules, the program and the rate.

- **Set-up** (`race/exact_setup.*`): the program data once per CORE.GT3 (`exact_program_data`: GT3's tables, the disc's
  program checked; `race_physics` the one place a disc's physics is chosen: GT3's, `own` false for a program that is not a
  GT3 release, logged and shown under Settings > Game disc; a program that cannot be read cannot race), the course once per course file (`exact_course`, `sim::exact::RaceCourse`), the
  event record (no event: an empty one, the laps set), the course category, the race mode (single
  player, or the split-screen battle), the start type and countdown from the event (+0x1F3 / +0x1F2;
  battles none), the race's laps, and one race entry per car as GT3's race information holds it: car 0
  player 1 (car 1 player 2), then the opponents. A car's equipment (`exact_equipment`): a GT-mode garage
  car's parts and settings (`GameRequest::car_parts` / `car_settings`; `make_equipment`), a variant
  (table 31: its base car with the variant's parts, `stock_equipment`), an arcade car (table 35:
  `arcade_equipment`, the tyres switched to the car page's stage), a database car with its parts; the
  screen keeps the launch's source (`launch_source_`). A player's entry: pedal style 0
  (automatic) or 1 (manual; the pause menu's AT / MT changes it on the record while the pad drives), the
  steering-wheel type 1 when the player's controller is a wheel (`DeviceKind::Wheel`), ASM / TCS from the Options (arcade single race,
  free run, 2P battle), the engine condition words and the car's dirt. Grid: the opponents in their order
  (GT mode: by power-to-weight) on the slots before the players' (the players behind them), never more
  cars than the course's grid slots (more are left out, logged): GT3's menus never make such a field (every
  event's cars fit its course; the arcade's dirt races are one against one). The start port itself reads a
  slot past the course's grid table as the program does (CourseQuery::slot: what follows the table in the
  road block, the loader's pointer words with the block's address, RaceSetup::road_addr). An electric mod
  car races with its donor's stock engine, engine parts, drivetrain and gearbox (`exact_equipment`; the log
  and the mods screen say so) until the exact core has the EV drive. The test autopilot (`GT3R_RACE auto=1`)
  sets the configuration byte 3: the race's own AI drives the players' cars with their pads decoded.
- **Inputs**: the race pad (the player's race layout in GT3's type A meanings) through the pad read
  (`sim::exact::pad_read`, the live path: the analog pad with pressure buttons, pad type 8; the
  wheel kind the steering-wheel path, the stick's x as its position, Cross / Square as its pedals):
  `RaceVehicle::set_pad` each step while the pad drives. Where the screen gives no pad (scripts, the
  autopilot, demos, replays) the step's `ControlInputs` become an all-analog frame
  (`exact_inputs_frame`). **Analog response** (Settings > Controls, `[race] analog_response`): `linear`
  (default) keeps the pad read's frame (its kind, flags, buttons, shifts) but sets every analog axis
  linear in the race pad's: the stick's (or wheel's) x straight to the steering, the d-pad's pressures as
  GT3 steers with the d-pad, and each pressure to the frame value whose throttle, brake, handbrake or
  back axis after phase 3's shaping (x + c (1 - x), c = the program's 0.1; a wheel's pedal curves) is the
  pressure's fraction (`exact_pad_frame`); `original` is the pad read as it is (full throttle at 18 % of a
  pressure button's range, brake 33 %, handbrake 10 %; the stick curve's dead band over the inner 29 %).
- **What the screen reads** (`race/exact_car.*`): each car's `VehicleState` from its record (the CoG and
  body axes in the physics frame, GTRW = (x, z, -y); the speedometer F +0x16D8; wheel contact = the
  ground under the hub, its raw surface = the triangle's material | flags << 8), `VehicleSetup` from the
  parameter block, the telemetry the sounds, smoke, dirt, HUD lamps (F +0x521 / +0x520) and force
  feedback (car +0x16D6 the steering wheel's force) read, the car's facts (mass, extents, gears, drive)
  and engine condition (`RaceVehicle::telemetry()` / `facts()`).
  Positions (F +0x4E1), laps and splits (line crossings from the laps, times from F +0x530; sector crossings from the race display's event 12, their time from its event 14),
  finishes (F +0x4E0, the time F +0x530), player 1's lap clock and best lap (`RaceTrack::set_lap_source`:
  the HUD, lap list and results), pit entries and the race clock (`-start count / 60` during the
  countdown, then the clock / 3000) come from the race. The race's own AI drives the run-ups, the pit
  lane and the end of a run, and the gear hint is the AI driver object's (+0xAC / +0xAD).
- **Replays**: the race's own record (format 11, "Replays"): the setup with every car's entry and every
  step's pad frames; the screen's recorded inputs, for a pad-read step the ones whose frame is the one the
  pad read gave (`exact_frames_of`), are for the replay's HUD only.
- **Solo modes** (`app/screen_race_solo.cpp`): the time trial, licence test, machine test, free run and
  GT mode's free run before a race as one race with their conductor (`ExactFieldOptions::kind`, as
  `run_kind()` tells the runs apart: the licence tests (L...) the licence's, the time attacks (AT...) and
  other time trials of set laps the time trial's, machine tests the machine test's, a time trial of no set
  laps the free run's, GT mode's pre-race free run free practice's; no opponents), built by `build_exact_solo` in `build_vehicle_inner`'s solo path and held
  as `RaceScreen::solo_field_` (field_ stays null: the screen's solo paths; `exact_field()` is the field on
  the exact core either way). The race information's words: the visit's session best (race info + 0x8AC,
  carried from run to run), the event's judging times, the speed unit; the start type as the race's start-up
  sets it (`race::demo_start_type`). The run ends as its conductor decides (`exact_solo_step` after every
  step): the time trial at its first lap (or its course-out, event 10: failed), the licence test with the
  race info's result words (2 .. 4 a medal, 0 failed, -1 out of the course; an unjudged test keeps none),
  the machine test at its split line (the time to it, or the top speed); a time attack's medal from its
  judging times. The race's clock and start hold feed the screen's (`tt_hold_`, `race_time_`);
  `MachineTest` only places the pop-ups' split marks. A pylon touch reaches the race as the conductor's hit rule of kind 2
  (`exact_report_hit`, a `RaceChange::Hit` in the record), every step the player's car touches one; as in
  GT3, whose frame runs the race step and then the course's rigid bodies (the race's frame update: the race's frame update
  with the race step first), the touch reaches the car's record before the next step. The free run keeps its recorders for
  the visit (`sim::exact::SoloRecorders` in the screen's `exact_free_run_`, `ExactFieldOptions::free_run`:
  the record memory with the lap recording, the best lap's store and the ghost's playback; GT3's free run keeps
  its race object, and so the best lap and the ghost, from run to run until the player leaves; the record keeps
  them as the run starts, RaceRecord version 3) and, without
  a whole-run ghost, drives car 1 as its ghost car (the player's car once more; the start flags it a ghost)
  from the ghost's playback, the ghost's extra step through the race's nested pads, live and in replays
  alike. Restarts build a new race (the free run's with the visit's recorders); replays and the disc
  demonstrations play their record on a solo field. GT mode's free practice starts the player's car in its pit
  box (the start's mode 6); the pre-race menu offers it only where there is one (QuickMenuItems::visit: not on
  a dirt course, as GT3's; every course of GT mode's races without a stop line is a dirt course, checked
  in the start's checks; `start_free_run` refuses a course whose stop line has no box for it, none of GT
  mode's).
  Check: `exact_game_solo_modes` (disc).
- **Online races** (docs/dev/ONLINE.md): the i.LINK
  battle's conductor (`Conductor::BattleMP`, start type 1), our car stepped, the other players' cars posed
  from their owners' states (`FieldConfig::remote` with each car's `ExactCarSource` from its online
  descriptor, `exact_online_source`; `RaceField::set_remote_pose` with the prediction's record,
  `ExactRace::pose`), their prediction shadows one-car races from the same setup (`exact_shadow`), the
  hits with them from the collision step's car contacts, the owners' finish times in the standings.
- **Not yet on it**: the tyre effect
  emitter's particles (the game's `TyreSmoke` still emits from the wheels' smoke emission).
- **Checks**: `exact_game_race_lockstep` (disc) runs a whole Sunday Cup, the Tahiti Maze rally and a
  2P battle (player 2 through the pad read) through `RaceField` and on an `ExactRace` built and stepped
  directly: every record, the race order, clock and countdown equal after every step.
  `GT3R_EXACT_DEBUG=1` logs every car's record every 2 s; the step's cost is logged every 600 steps
  (about 0.10-0.14 ms for 6 cars on an M-series Mac).

## Races with opponents

`race/field.*` (`RaceField`), on the exact core: the player and up to 5 opponents (the event's
entries, driven by the race's AI with the event's parameters and rubber band) in one race (car-to-car
contact, slipstream). The player's `RaceVehicle` steps the whole race, so the fixed-tick loop,
replays and HUD are unchanged; the player starts on the last grid slot. A car finishes at a line
crossing (presentation §B9): after the event's laps, or once the leader has finished (a lapped car,
the player included, finishes at its next crossing: "+1 lap"), or at its first crossing after a
time limit (nothing ends at the limit itself); its laps stop counting there. The race order (kept
every step, ties keep their places) is the HUD position and the player's place: more laps first,
then running cars by distance, a running car ahead of a finished one, finished cars by finish time.
`GT3R_RACE="event=AEAT0001"` starts one directly.

### Starts and event fields

From the event record (database table 32; `race::event_start`):

| Field | Meaning |
|---|---|
| +0x198 u8 | initial speed, km/h (0 for every race; 50–220 flying starts, 80 demos / free time trial) |
| +0x199 u8 | laps; machine tests: the test (1 0-400 m, 2 0-1000 m, 3 top speed; `race::MachineTest`) |
| +0x1B5 u8 | cars in the race, player included (opponents = min(cars - 1, 5)) |
| +0x1E4 u8 | time limit, minutes (120 for the two 2-hour endurance races, which have 0 laps) |
| +0x1E6 s16 | flying-start distance, metres before the line (negative) |
| +0x1F2 u8 | countdown seconds (start type 0 only) |
| +0x1F3 u8 | start type: 0 demo / machine test, 1 standing 240 frames (licences from rest), 2 standing 600 frames (every single race), 3 rolling (GT endurance) / flying (time attack, moving licence tests) |

- **Standing** (type 2): all cars held 10 s; `count3..1` and beeps over the
  last 3 s, then START and `count_s`. Type 1 holds 4 s.
- **Rolling** (type 3 races): every car leaves its grid slot at 80 km/h
  (the race's moving start: gear and rpm matched), no countdown; the computer drives the player's car for 300 frames (5 s), then
  START and `count_s` at the hand-over, and the race music starts there (`rolling_runup()`).
- **Grid** (physics §9): standing starts place every car at rest, its nose on the
  slot point (moved back by the front extent).
  Flying starts sit on the racing line (game line 6) at -distance, heading
  along the line's next 10 m.
- **Grid order**: player last. Arcade: opponents in draw order. GT mode: ranked by
  power-to-weight (kg/PS = mass / peak of torque x rpm), strongest on pole
  (`FieldConfig::grid_by_power`; `GameResult::order` still indexes
  `GameRequest::opponents`). Replays record which (format v9, see "Replays").
- **Flying starts** (type 3 with an initial speed: time attack, moving
  licence tests): the car starts at the event's speed, `flying_m` metres
  before the line along the lap (on the start slot's lateral position), and
  the lap clock starts at the line. The computer drives up to the line (AUTO
  DRIVE; the AI driver, physics §9.4) in licence tests and time trials alike
  (`flying_runup()`). Crossing the line is the start signal
  (`flying_start_signal`): `count_s`, START for 1 s, the race music (silent during the
  run-up); a free run (ATAF0001: 80 km/h from the line itself) also shows "Timer starts at
  start/finish line" 2 s later, then "Press START button to end session", 3 s each, in the
  event display. Live runs only. `GT3R_RACE="tt_event=L0A0006"` runs one; the log says
  "Race: flying start signal at <t> s (count_s)".
- Time-limited races, like every race of 10 laps or more, use the endurance HUD layout
  (Total, Best, the lap-time list, Lap; "Lap n" without a total); no time remaining is shown.

## Results and flags

From the race workstream's research (presentation §B, `result1.pmb`,
`qm_*.pmb`):

- **Start**: `count3..1` images 1 s each with `count_d` beeps, `start` image
  and `count_s` at the green light (cars held by the race's start hold).
- **Finish**: the `finish` image for 4 s, top edge at y 160. After the finish
  the game drives the player's car (auto drive). 2P: no finish message (the
  half shows AUTO DRIVE, then the results panel; see "2P battle" above).
- **Wrong way**: red "WRONG WAY" centred at (320, 305) (2P half: 200).
  GT3 has no penalties, no lap invalidation, no "final lap" message in normal
  races (endurance only); `win`/`lose`/`replay` images are unused.
- **Results overlay** (races): `result1.pmb` page "result" elements drawn
  over the running race: title bar 5 s after the finish, rank bars at
  6.0 + 0.12 i s (fade-in, unrolling 0.19 per frame out of the bar above),
  then the "OFFICIAL TIMER SEIKO" bar. Names/times sw_16 at 87 %, the
  player's row 0xFFC800, others 0xCCCCCC. Time column: winner "m'ss.mmm",
  "+gap", "+N lap(s)", "Running...". Leaves on Circle / Cross after it is fully shown,
  or 15 s after every car finished (2P: 15 s).
- **Time trial finish**: the arcade time trial is one timed lap (`race::kTimeTrialLaps`,
  whatever the event's laps byte says; presentation §B7): the car finishes at its first
  completed lap (`race::solo_run_finished`), the laps freeze. Debug runs with `laps=N` finish
  after N.
- **Time-trial overlay** (time trials and machine tests, `TimeTrialOverlay`): from 5 s after
  the finish; the race ends on Circle / Cross from then, or 10 s later
  (`TimeTrialOverlay::ends_race`). The run's place in its stored records (`race::record_rank`
  over `stored_records()`, the GT-mode save's `at:` / `mt:` / `ct:` ranking; ties rank ahead,
  nothing past 12th; none stored = an empty table) and a flickering NEW RECORD / RECORD TIME / RECORD
  SPEED, "Lap" + the best lap (or the time to the mark) at y 270, or "Max Speed" + the top
  speed.
- **Quick menu**: the layout per mode follows the original's race drivers
  (`RaceScreen::quick_menu_layout`): arcade race `qm_arc_result1.pmb` and 2P
  `qm_arc_2p_result1.pmb` after the race (the race runs first); arcade time trial
  `qm_arc_tt_ltd1.pmb` and time attack / GT free run `qm_arc_tt_free1.pmb`, machine tests
  `qm_gt_mt_4001/10001/max1.pmb` and licence tests `qm_gt_license1.pmb` **before the
  first run and after every run** (the drivers' loop opens with the menu); GT races
  `qm_gt_event1.pmb` (championship rounds `qm_gt_event_champ1.pmb`) before and
  `qm_gt_result1.pmb` / `qm_gt_result_champ1.pmb` after. Background, title, result list
  widgets filled with the standings, item icons in the band (scrolling past 5), the
  focused item's help layer (item order and help layers from the `.mbl`). Items are
  dispatched on their `.mbl` action string (`race::quick_menu_action`: qm_start /
  qm_try_again, qm_free_run, qm_replay, qm_demo, qm_save_replay, qm_save_ghost,
  qm_load_ghost, qm_records, qm_logger, qm_option, qm_setting, qm_next_session,
  qm_save_game, qm_exit, qm_yes, qm_no; qm_go_race, qm_load_setting, qm_save_setting,
  qm_best_time and qm_dummy are also action names, which no layout uses);
  test `race_quick_menu_actions_disc` checks that every item of the disc's 12 layouts
  has one, or a link to a page of its layout. Championship rounds: Next session (back to
  the menus, which chain the next round), Save game (the save page in Save game mode,
  `make_game_save_screen`, docs/dev/GT_MODE.md "Saves": a new or existing save gets the
  game with this round recorded, the running game and its save untouched, as
  the original stores the championship's progress around its save), and
  Exit through the layout's "Retire from championship?" page (Yes posts
  `GameResult::abandoned`). Time trial and machine test menus show the ranking's Top 3
  (`RaceScreen::fill_top3`, read again before every menu and after a run's name entry):
  always three rows, an empty entry with no name and `--'--.---` (or ` 0.0 km/h`), a top
  speed as `%4.1f %s` in the Options unit (rounded), the free time trial's car list
  (`qm_list_car_name<n>`) as the entry's car name or `---`. A licence run with a medal, a
  finished machine test or time-trial challenge, and a free run with a best lap (the arcade
  time attack, GT Mode's free run: `course_time_trial()`, ranking `ct:<course>`, however the
  run ended) go into their ranking at the exit transition (`record_run`), the name entry (if
  it ranked) before the run's replay and the quick menu (licence: over the menu; "After a
  run"); nothing else records them (the menus take no record from
  the posted `GameResult`; a time-trial EXIT before its lap records nothing). A free run's
  Records item opens its course's ranking. Triangle / Square on a root page with the cancel
  flag 0x100 (all but the championship menus) is EXIT at once (SE `return`;
  `QuickMenu::tick`, test `race_quick_menu_cancel`). Every item works:
  - Retry / Start, Replay (returns to the quick menu at its end), Save
    replay, Save ghost (the visit's best run), Load ghost (at once; see "Ghost").
  - Demonstration: the computer drives the course from the TV cameras;
    Cross/Start/Circle return; REPLAY is disabled after it (its run replaced the last one in
    the original); the player's run stays the one save uses.
  - Records: the run's ranking in the GT-mode save (licence, machine test, time trial, a free
    run's course; `make_records_screen`); without one (Quick race practice) the best laps on
    the course from the saved replays, fastest first (car, time, date from the file name),
    both on `records1.pmb` page `records_free_tt`.
  - Data logger: the Analyzer's GRAPH (see "Replay Theater") over the last (A)
    and best (B) laps of the session.
  - Option: transmission, camera, music, vibration, HUD, 2P split. Setting: GT mode's car
    settings screen for a garage car (see "GT-mode flow around a race"), else transmission.
  - Disabled items (menus §11, the original's per-driver switches: `race::QuickMenuItems`,
    `RaceScreen::update_quick_menu_items`): REPLAY / SAVE REPLAY until a run of the visit (and
    after DEMONSTRATION or a licence run left with EXIT), SAVE GHOST / DATA LOGGER until a run
    sets a session record (the time trial's lap, a 0-400 m / 0-1000 m goal or a judged licence
    run at or under the session's best, a free-run lap under it), SETTING in the time trial
    and with an arcade (non-garage) car in the arcade race / free run, FREE RUN before a GT
    race on dirt, EXIT / SAVE GAME after a championship's last round. They stay focusable, are
    drawn darkened under the layout's `disable` widget (the focused one's highlight at half
    brightness) and refuse confirm with SE `disable`. Not reproduced: a confirmed SETTING page
    clearing REPLAY / SAVE GHOST / DATA LOGGER in the free run and machine test.
  `GT3R_RACE="...,quick=1,qm=<item>"` chooses an item when the menu opens (a disabled one is
  refused, logged "Quick menu: <item> is disabled").

## 2-player battle

Arcade "2 Player Battle" (or `GT3R_RACE="p2=1,car2=<label>"`): both cars in
one race with car contact (the exact core's split-screen battle); player 2 uses pad 2 (R1 camera),
its own camera, rumble and engine note. Layout as GT3's
16:9 option: side by side, each 3D view letterboxed to (x0, H/6, W/2,
8H/11); per-half HUD of racemode 8 (426.667x448 units): rank ("1st".."12th" at
80 %), lap count (pushed right like every lap count: x 310 + 106.667) and
Fastest/Last/Lap lines (values and labels as the full-screen time lines;
Fastest is the race's fastest lap of either player, Lap goes once the half's
player has finished), digital speed, gear, the `meter_mini` bar
tachometer and the course map at 70 % with both players' dots (no 3-2-1
countdown or winner message: see "2P battle" under "Rendering").

`[race] split = auto|horizontal|vertical` (or `GT3R_RACE="split=h"`):
horizontal is the PS2's 4:3 layout, player 1 on top, each half a 640x224
racemode-7 HUD (text at 70 %); vertical is side by side; auto follows the
Options' wide mode (top / bottom with it off, side by side with it on).

2P races are recorded like every race (the race record keeps both players' pad frames); playback
re-runs both cars and ends where the race ended.

## Race sounds

From the research into GT3's race sounds (effects, per-view gain
table):

- Channel gains per view (in-car/chase x target/other, other cameras) for
  engine, exhaust, turbo, blow-off, whine, clash, rumble, in-car wind, road
  rush, road noise, rain, squeal, slide, dirt (`sfx_gain`).
- Tyres (`CarSfx`, `RaceScreen::update_tyre_sounds`): every car, player and
  opponents, plays each tyre loop once **per wheel**, on the wheel's surface:
  tarmac/paving `on_skill` squeal + `on_road` roar, kerbs `rumble`,
  grass/gravel/sand `off_skill`, dirt `off_roadnoiz`, wet `rain_noiz`. Volume
  = the view's channel gain x the wheel's slip intensity
  (`VehicleTelemetry::smoke`, unclamped; the kerb formula on kerbs); pitch =
  Doppler x `VehicleTelemetry::skid_pitch` (the second curve set,
  physics §8.3c); road roar from tyre width and rim speed, silent 6
  steps after a wheel leaves the ground. Each voice sits at its wheel seen
  from the camera (`place_voice`: min(1, 32/d), cos/sin pan) and sends the
  stealing priority 255 (1 - min(1, gain)) every step (`LoopEffect`,
  `VoiceMixer::set_priority`), so a loud squeal is never stolen and far,
  quiet loops give way. Opponents use the other-car gains; replays and the
  TV camera the "other camera" column for every car. A car without telemetry
  (an online remote car's pose) is silent here.
- Collisions (the followed car only; GT3 reads no other car's hits): the
  strongest hit of the step, wall |j|/50000 -> `clash`, car-car |j|/7500 ->
  `clash2` (both cars of a pair; `clash3`, objects, is not played); clamped to 1,
  below 0.2 ignored. Volume gain x s x 0.6 x 32/d with d the camera-to-car
  distance (0.25 m at least), capped at full, centred; in the cockpit any
  audible hit is full volume. Keyed again on every step with such a hit (no
  cooldown). A wall hit is audible from about |j| = 10,000 N s: roughly a
  15 degree hit at 100 km/h or 8 degrees at 180 km/h; shallower scrapes are
  silent in GT3 too. Most car-car contact steps are light rubbing below the
  0.2 threshold (4 AI event races: 1037 of 1045 steps under |j| = 1500,
  about 6 km/h of closing speed along the normal).
- Other cars' engines: min(1, 32/d) attenuation (horizontal distance), Doppler
  (c + v_listener)/(c + v_source) with c = 331.45 m/s, "other" view gains; GT3
  sounds every car (voice priority decides); we voice the nearest 5.
- Countdown beeps `count_d` / `count_s`.
- Stereo: every engine note is panned with `EngineSound::pan_for_offset`
  from the car's offset in the viewing camera's frame (own car centred in
  the onboard views); opponents' notes the same way.
- Engine notes (own car, player 2, opponents) are placed with
  `EngineSound::set_position(x, z)` in the viewing camera's frame (x right,
  z forward; the followed car's own sounds sit in front in the in-car view),
  which also drives the optional surround cue.
- Wind / road noise: the shared `sound/rn_01.es` unit per followed car
  (`EngineUnit::update_at`), rpm and gain from `car_sound::wind(u, y, C_d)`;
  C_d = body (racing-modify, table 5) record +0x39 / 100 (R34: 0.35), y =
  `VehicleTelemetry::slipstream`.
- In-car loop (`in_car`), transmission whine (`turbine2`; only with a
  non-stock gearbox: gear part +0x10 != 0, clutch engaged; driven-axle speed
  = rear, front or the 4WD mean of the wheel speeds), turbo spool
  (`turbine`, `car_sound::Turbo` stepped every physics tick from
  `CarExtraSounds::step_inputs`: the processed throttle, `turbo_load` /
  `turbo_lag`) and blow-off (`b_off`, 32/d unclamped; the processed throttle
  is 0 through an upshift, so every full-throttle upshift fires it), all
  through the per-view channel gains. Followed cars only (player 1 and 2);
  opponents have their engine note and, for the nearest two, the wind unit
  at the "other car" gain (turbo / whine / in-car are 0 for other cars in
  the in-car and chase views).
- Course ambience: the CRSS emitters (course slot 35) through
  `CourseAmbience` / `CourseAmbiencePlayer`: one listener per screen (camera
  position and right axis, the followed car's velocity and the surface flags
  under it), every car's track distance for the gated crowd emitters, and
  the tunnel loops. Missing on the licence layouts without the section.
- GT-mode garage cars: the whine flag is `GameRequest::racing_gearbox`.
- Music vs effects: the game plays its `.ads` music on two SPU2 voices, so
  music and effects meet at equal weight. The voice mixer's 0.5 output trim
  (`VoiceMixer::kOutputTrim`, headroom) is applied to the music stream too
  (`AudioEngine::stream_output_gain`); before, every effect sat 6 dB below
  the music. The Music and Sound effects settings scale the two on top.

## Paint colours

`database/carcolor.db`: per model (id_hash) a list of colour ids, plus colour
definitions (id -> RGB). A car's paint index is the position of its colour id
in its model's list. Variant records carry no colour; the event record has
colour ids at +0x110 (u32 per slot, 0xFFFFFFFF = choose). Licence tests: slot 0
is the test car (entry 0) and gives its paint (e.g. B-2 Viper GTS 0x101, red
with silver stripes; 31 of the 48 tests set one, all in the model's list; the
rally tests and most race cars are 0xFFFFFFFF). Race reads it for `go_license` and
`tt_event=L…` (test `race_license_car_paint_disc`). Machine tests, arcade time
trials (`AT…`) and the attract demo's player slot are 0xFFFFFFFF. Unset
cars get a colour at race start in entry order (player first): each candidate
weighted 0.984375^j, divided by 1 + similarity to every other car's
candidates (fixed colours: 10000 sim^64), sorted ascending, then picked with
the game's RNG (`x = x*17 + 17; r = x ^ rotl(x, 16)`) on the race module's
state (`race_info_random()`, gt_mode §20): seeded 0x87654321 at start
and never reseeded, so it continues from race to race and from the opponent
pool draw of the same race (GT mode, arcade Single Race). A replay stores the
state the paint draw started from (`Replay::paint_seed`, format v6; older
replays use 0x87654321, which is what they were recorded with) and re-runs the
draw from it.

## Ghost

The original's ghost rules (presentation §G11), drawn as a translucent car (`DrawItem::alpha`
0.4) in live runs only. The ghost is a car of the exact race (see "Ghosts on the exact core"
under "Replays"); which runs have one (`RaceScreen::whole_run_ghost`, `lap_ghost`, by
`run_kind`):

- **Licence tests, time trials, machine tests: the best run.** After the run `solo_run_ghost`
  keeps the run's race record as the best run (`best_run_record_`, `best_run_time_`,
  `best_run_replay_`) when it set the session record (`run_session_record_`), and gives the
  ghost the best run when the run's time (`ghost_run_time`) beats the ghost's or there is none
  (`race::replaces_ghost`). A recording of 36,000 steps or more keeps nothing (and clears the
  session record). The next run has the ghost record's car 0 as its ghost car
  (`ExactFieldOptions::ghost`), drawn from the race (`exact_ghost`; not while the computer
  drives it).
- **Free runs: the best lap.** The free run keeps its own recorders for the visit
  (`exact_free_run_`, `sim::exact::SoloRecorders`: the record memory with the lap recording, the
  best lap's store and the ghost's playback; the visit takes the field's as each run ends), as
  GT3's free run promotes a better lap itself; the race's ghost car plays the ghost's store.
- **The Ghost option** (`ghost_on_`, `SoloRecorders::ghost_option`): on, the ghost is replaced at
  once (a free run's lap by its lap handlers); off, `ghost_ask_` opens "Update Ghost?"
  (`ghost_dlg`) on the next quick menu once its root page can take the popup (a free run's: when
  a lap beat the ghost, race info + 0x48 left set), and YES calls `take_best_run_ghost` (a free
  run's best lap store becomes the ghost's: `exact_best_lap_to_ghost`).
- **Select**: the menu pad's Select flips `ghost_shown_` (the visit's).
  `GT3R_RACE="ghost=0"` turns ghosts off (screenshots).
- **LOAD GHOST** (`load_saved_ghost`, at once): the newest ten saved replays of the course and
  car that can play on this disc; a whole-run mode takes the fastest run of the same kind and
  start (`Replay::start`, `start_speed`) as the next run's ghost record; a free run plays the
  fastest saved free run headless to its end (`solo_recorders_of`) and takes its best lap's store
  as the visit's ghost (`SoloRecorders::ghost_from_best`; the live run's too, `exact_ghost_from_best`). **SAVE GHOST** writes `best_run_replay_`.
- No ghost is loaded when a visit starts, and no "GHOST" time is drawn.
- Best in these runs is the visit's best (`best_splits_`): `RaceScreen::hud_data`.

Test `exact_replay_ghost` (the ghost car equals the first run's car 0 on every step of that
run). Not compared with the original on screen: how translucent the ghost is, and the in-car
view during a standing start, where the ghost stands around the camera (the original draws it
there too).

## Audio ambience and music

Reverb depth (EVOL) follows the game's ambience rule every tick: target 0.5
when the surface flags under the car have `(flags & 0x18) == 0x08` (tunnels,
covered sections), 0.25 for 0x10/0x18, else 0.125; step at most 1/36 per
frame (`ReverbAmbience`). Race music (`RaceMusic`, music §2-3): each race
song is the next one of the race playlist (`RacePlaylist`, the options' music
list: list order or shuffled, every song once per pass), kept in config.ini
`[options] music_playlist` and saved whenever a song is taken, so it carries
on from race to race (the Options music list edits it and starts a new pass,
GT_MODE.md); the next song starts when one ends; stopped when leaving
the race. A run that starts behind a quick menu (the pre-race menu's Start, the
result menu's Retry) plays its start jingle as the menu closes, as a direct start
does when the hold begins. Every request has its level (music §6): race songs 0.5 live and 0.7
in replays, jingles and the demo track 1.0, the quick menus 0.8 (as the menus);
the Music slider scales them all.

## Menus

`screen_race.cpp` registers a `menu::flow` GameRequest handler: arcade time
trial ("compulsory", course ids `tt01`..`tt10` = events `AT010001`..: course
and car from the event), free run / time attack (`ATAF0001`, the menu's
course and car), single race (`A{E,N,H,P}{C,B,A,S}T0001` events: opponents,
laps, AI parameters) and the GT-mode requests (`go_race` with opponents;
`go_license`, `go_machine_test`, `go_free_run`, `go_preview` as time trials; a
GT-mode garage car comes as a full `sim::CarSpec`). 2P battle runs as a time
trial. Before popping, the screen posts a `menu::flow::GameResult`
(finished, position, cars, total time, best lap, lap times, distance). Menu car ids are database labels without their
leading '_'; arcade variants map to their base car. "Exit to menu" pops the
race screen and the menus resume. Scripted check:

```bash
GT3R_GT3MENU_SCRIPT="cross,wait:90,down,cross,wait:90,cross,wait:90,cross,wait:90,cross,wait:150" \
  gt3r disc.iso --screen gt3menu --shot tt.png     # Arcade > Time Trial > first course/car
```

## Screenshots, scripted runs

With `--shot` the app steps exactly 1/60 s per frame (not wall-clock time), so a
shot of frame N (`GT3R_SHOT_FRAME`) is identical on every run: same simulation
time, AI positions, interpolation and reflection cube.

```bash
GT3R_RACE="course=trialmt,car=r34_gtr_v_99,cam=0,auto=1,warp=20" \
  gt3r disc.iso --screen race --shot out.png
```

| Key | Effect |
|---|---|
| `course`, `car` | course label / car label or model id; skips the selection |
| `cam` | 0 chase, 1 far chase, 2 bumper, 3 roof, 4 free orbit (`oyaw`, `opitch`, `odist`), 5 replay (TV) |
| `auto=1` | autopilot: the race's own AI drives the player's car (the race's configuration 3: the same driver as the CPU cars, its line, pit stops and gearbox) |
| `warp=N` | simulate N seconds before the first frame |
| `mt=1` | manual gearbox |
| `pause=1` | open the pause menu |
| `hud=0`, `perf=1` | hide HUD / show the performance line |
| `sheen=0` | disable the road sheen passes |
| `quit=N` | exit after N seconds (frame-time logging) |
| `watch=1`, `rwarp=N` | after `warp`, play the run back (and skip N s into it) |
| `save=1` | save the run's replay after `warp` |
| `replay=<file>|last` | watch a saved replay |
| `ghost=0`, `music=0` | no ghost / no race music |
| `event=<label>` | race against the event's field (start type, laps, cars, time limit from the record) |
| `p2=1`, `car2=<label>`, `split=h\|v` | 2P battle, player 2's car, split layout |
| `laps=N`, `quick=1`, `qm=<item>` | time trial lap count; open the quick menu after the finish; choose an item |
| `exit_laps=N` | the live run ends as the pause menu's Exit once it has N laps (screenshots of a free run's record and name entry) |
| `demo=1` | start the demonstration (licence: the disc's demo run) |
| `theater=<event>` | the Replay Theater's Demonstration of an event (AT010001 .. AT100001, L0B0001 ...) |
| `script=thr:1@0;steer:-0.3@4` | scripted player input (steer / thr / brk set at times in seconds; `rev:1` / `rev:0` the reverse request, as the back input with the automatic's brake driving it: e.g. `brk:0.6@5;rev:1@5`) |
| `hudmode=<n>`, `ritems=<bits>` | draw the HUD in layout n (`race::HudMode`: 1 endurance, 6 free run, 13 free practice, ...); replays: these ITEM bits (not stored) |
| `rcam=<n>` | replays: watch from camera mode n (as `cam`; 2 the in-car view) instead of the replay cameras |
| `rmode=<n>`, `rtarget=<n>` | replays: REPLAY OPTIONS view mode 0..3 and its TARGET 0..2 (not stored); with `watch=1` set before `rwarp` |
| `tt_event=<label>` | time trial on the event's course with its start (flying starts of licence tests / time attack) |
| `gpusync=1` | wait for the GPU after the 3D pass so "scene draw" includes GPU time |

`GT3R_RACE_DEBUG=1` logs vehicle telemetry twice a second. `GT3R_RACE_TRACE=<file>` writes one
line per race step with the player's car after it (`L` live / `R` replay, the ticks simulated, the
driver kind, position and orientation as hex floats, the speed), to compare a run with its replay
tick by tick: e.g. `tt_event=AT020001,script=thr:1@0,warp=30,save=1,quit=1`, then
`replay=last,warp=31,quit=1` (licence tests on their own layouts: add `tt_event=` to the replay's
preset too, which lists the course).

## Touch controls

On phones the HUD's meter cluster and tyre gauge move inside
`App::touch().safe_area()` (the part of the 640x360 UI no visible touch
control covers; the whole area when the overlay is hidden, so desktop output
is unchanged). `HudData::safe_area`. When the area is narrow (16:9 / 16:10
with the right-hand pedal cluster on screen, width 446) the meter cluster is
also scaled by clamp(width / 560, 0.7, 1) about the area's bottom-right
corner, so it does not cover the car.

The top corner blocks (laps/times on the right; the rank and the mini-map on
the left, each on its own) keep their corners, as without controls.
`HudData::touch_controls` holds the visible controls' drawn bounds
(`TouchControls::control_bounds()`); the HUD measures a block without drawing
it (`Hud::measure`, one rectangle per text and picture) and checks it
against them. A control that only comes up under the block's bottom line
(its top within 24 units of the block's bottom) is ignored, so the licence
targets and the lap list's Lap line underlap the shift buttons. Any other
control moves the block sideways toward the middle until it is 4 units
clear; when that is further than the safe area's edge on that side, the
block goes inside the safe area (below the top buttons) instead, and the
rank takes the map with it. Desktop check: a portable copy of the app with
`[touch] mode = on` in its `config.ini` and `GT3R_TOUCH_SHOW=1`
(`GT3R_PHONE=1` for a 20:9 phone).

## Licence tests

The race judges a licence test itself: the exact core's licence conductor (its clock, ends,
failures and result words, `sim/exact/race_flow.*`); `screen_race_solo.cpp` reads the result
(`solo_run_ended`), `race/license.*` keeps the run's state, medal times and failure text for the
screen, `screen_race_license.cpp` the flow. The rules (presentation §A):

- Event fields: +0x19A kind (1 stop test, 2 section, 3 lap(s)), +0x199 laps,
  start type +0x1F3 (1 = standing, 240-frame hold; 3 = flying), gold /
  silver / bronze at +0x1C4 / +0x1C8 / +0x1CC ({u8 min, u8 s, u16 ms}). The
  car is the event's entry 0 (a variant with its own parts). 48 tests: 2 stop,
  33 section, 13 lap tests; 16 standing, 32 flying.
- **Standing start**: 4 s hold with 3-2-1, the clock starts at the signal (GO), and
  a standing test of every kind is timed from GO, including the run-up to the line
  (the first line crossing does not restart it). **Flying start**: the computer
  drives (AUTO DRIVE) from the start distance at the event's speed; the clock origin
  is the first line crossing and the player takes over there.
- **Clock** (the original's licence clock): 1/3000 s units, +50 per tick from GO;
  the checks see the value before the tick's advance (the first racing tick sees 0).
  An event inside a tick adds trunc(50 f), f its fraction of the tick, and the time is
  ms = floor(units / 3) - start. Lap counter: 0 when placed behind the line, 1 past it,
  +1 per forward line crossing.
- **Ends**: kind 1 (stop) on the first tick all four wheels' hub ground-query
  triangles carry road flag 0x04 (grounded or not; on L_testcourse1 a 24 m wide box
  1000-1025 m from the line) and the 3-D body speed is <= 0.27777779 m/s (1 km/h,
  equality passes); no hold time, no line-crossing gate; f = |v| / 1 km/h; a partial
  stop keeps waiting (no fail, no timeout). Kind 2 when the lap's progress crosses the
  course's goal distance (GTRW header +0x1C) with the lap counter >= 1, f from the
  progress before / after. Kind 3 on the line crossing whose old lap counter equals the
  laps (+0x199): one clock difference, not a sum of lap times. The HUD time is the same
  clock - start.
- **Failures** (immediate): all four wheels on grass / gravel / sand
  (materials 2, 3, 4, 6) except on a dirt course (course table +0x08 = 2, the
  rally tests; R-3's run-up starts on a grass patch, public issue 18), all four on the pit lane (flag 0x01) inside the pit span (below), a wall hit
  changing the speed by more than 10 m/s, and **any touch of a pylon**
  (failure "pylon"; no threshold, also on a cone at rest). Wrong way, car contact,
  stopping and time never fail. There is no stop-box overrun rule: B-1 / B-2's row
  of 20 cones 1.9 m past the box is the limit. A fail plays the fail cue and shows
  FAIL (no time under it), then the licence quick menu; from the failing tick the HUD
  Time reads the car's recorded time, 359,999,999 ms, which GT3's formatter shows as
  `-'--.---` (it also dashes negative times and clamps at 999'59.999; test
  `race_hud_time_format`). After a finish the HUD shows the recorded time, frozen.
  Time trial stops the car on a pylon hit too (the same failure bit as a wall hit)
  and its HUD Time then shows the run's best lap, or dashes.
- **The pit lane** (physics §11) fails only inside the course's **pit span**: four grounded
  wheels on flag 0x01 while the car's track distance as the step began lies strictly between the
  pit stop line's (game line 4, file AI line 3) points 1 and n - 2, both sides compared
  lap-unwrapped as the AI keeps them (the points from the first one in the first half of the lap
  on have a lap added; the car's distance has one added in the first half). No line 4, or fewer
  than 6 points on it: the pit lane never fails. On the US disc 28 of the 46 licence courses
  carry a stop line (12 points; montecarlo 13) and 18 none (the circles, slaloms, test course,
  rally courses, L_midfield3, L_seattle3). Two quirks follow from the rule: L_akasaka2's (IA-1)
  stop line starts before half a lap and its point 1 lies past it, so every point has a lap
  added and no distance is ever inside; L_rome1's (IA-6) span ends at half a lap. A dirt course
  does not switch the rule off (none of them has a stop line). Time trials share the rule
  ("Course-out; no respawns").
- **Pylons** (`race/pylons.*`, docs/formats/course.md "Rigid bodies"): created in every
  race mode on courses that have them and reset on every run / retry; asleep while
  three hull points rest on the ground; awake bodies tumble (torque-free rotation,
  angular velocity x 0.999 per frame and at most 20 rad/s, gravity, the deepest ground
  point bouncing with restitution 0.25 (0 under 0.9 m/s) and friction 0.5). Each car's
  box (front / rear extent, half width, 1 m high, sides leaning in 5 %) pushes a body
  out through its nearest face and hits it (restitution 0.01, inertia x 1.5); the car
  itself feels nothing. A touch by the player's car is reported to the race as a hit (the
  conductor's rigid-body hit), which fails a licence and stops a time trial (test
  `race_pylons_disc`). Awake bodies then meet the course walls (GTRW wall points; sensor edges
  skipped; edges within 8 m of the motion): the centre against each edge in x/z with the type's
  radius (+0x90): overlapping (foot strictly inside the edge, distance < r) puts the centre r
  from the edge on its drivable side n = (s.z, 0, -s.x) / |s|; else, when the centre crossed the
  edge this frame, it goes back along its motion onto the wall line; then the ground contact's
  impulse (restitution 0.25, none under 0.9 m/s, friction 0.5) along n at the hull point with the
  least n.p. Every body, asleep too, is drawn after its planar shadow: the type's GTM1 node 1,
  projected along Env0 scene light 0's direction (+0xAD0, not the car shadow's +0xA70..A78)
  onto the plane of the road triangle under the body's centre (probed at placement and every
  awake step; p' = p - L (n.p + d) / (n.L)), no depth writes, 5 mm nearer the camera (GT3 moves
  near / far by 0.005). The pylon draw sets no colour: it inherits the GS colour scale left by
  the car shadows (Env0 +0xA60 colour, vertex alpha x +0xA6C), which we use (the order of the two
  draws was not confirmed). Checked on L_testcourse1 against a capture of B-1: the cones' long
  shadows join into a dark band along the row.
- **Medal**: time in ms (rounded) <= target, gold first; slower than bronze =
  no prize. `GameResult::license_medal` (-1 none / failed, 0 gold, 1 silver,
  2 bronze), `failure` ("out of the course", "over the times") and `total_time`.
- **HUD**: Best, the elapsed Time and the three targets with the
  `mark_1st/2nd/3rd` icons at (615, 140 / 168 / 196); no lap or position. Best is the visit's
  best judged time, medal or not (`best_splits_`, which `rank_finish` feeds), dashes until a
  run reaches the end, kept across retries; never the lap timer's laps.
- **Ghost**: the visit's best run (see "Ghost").
- **Result** (presentation §A4; `LicenseResultOverlay`): the live race keeps
  running behind it (the car under the stop driver, the race camera, the HUD; no FINISH
  message). Frames from the result: wait 6, a black dim 0 -> 1/3 over 30 frames, the medal
  jingle at its end (frame 37, `RaceMusic::kMedalDelay`), a further 180 (gold) / 30
  (silver, bronze) frames, then the `race/gold|silver|bronze.prm` model (composited over
  the race, weight over 60 frames, 2 s fly-in then its loop), the word (GOLD / SILVER /
  BRONZE, sw_20 x 1.25 at (320, 336), black shadow then the colour copy at (+2, -2), alpha
  over ~31 frames) and the time (same colour, (320, 112), over 60 frames); a 300-frame
  hold, then 300 frames that Circle / Cross can skip (about 14.2 s gold, 11.7 s silver / bronze).
  No medal: the fail jingle at once, then after 60 frames a (160, 160, 255) wash rising to
  50 % over 30 frames and the text (160, 192, 64) in two copies 30 px above / below y 224
  merging over ~31 frames: TIME EXPIRED with the time at y 140 (slower than bronze) or
  FAIL (the US text table's RACEDISPLAY_ExamTimeOver / ExamFail); 300 + 48 frames. A
  time-trial FAIL (RACEDISPLAY_TimeTrialFail) uses the same wash and text, 300 frames then
  240 Circle / Cross-skippable ones. Leaving the race (results, presenter end): the last frame
  stays 10 frames, then a grey wash (0.1, the menus' DelayFadeout) at alpha 0.5 per frame,
  then a cut to the quick menu (presentation §C1). The licence quick menu
  (`qm_gt_license1.pmb`, the test name, targets and description panel) shows the run just
  driven replaying in a loop in its preview box (the disc's demonstration before a run).
- Machine tests (`go_machine_test`, `race::MachineTest`, presentation §B7): a 7 s
  hold, then no laps: the event's lap-count byte is the test (M400 1, M1000 2,
  MMAX 3), and the run ends at its goal, a track distance from the control line
  once the car has crossed it (400 m, 1000 m, 6845 m: the top-speed run ends on
  the test course's first lap). On the way: split points at 400 m (M1000) or
  400 m and 1000 m (MMAX). Crossings are timed from GO like lap lines
  (`LapTimer::crossing_ms`, the lap timer's unwrapped distance); the time to the
  goal is `total_time` for M400 / M1000, MMAX records its top speed. The HUD
  (the original's machine-test layouts): no lap count, the map at the top; M400 /
  M1000 "Best" (the visit's best time to the goal: dashes until the first) and "Time" (from
  GO, then the goal time); MMAX two speed lines instead, "Record" (always dashes: the original
  never sets it) and "Best" (the highest speed so far, `Hud::speed_text`), and
  no pop-ups or WRONG WAY.
  `GameResult::top_speed_kmh` = the run's best speed until its finish (every
  race). Since 2026-10-21; earlier builds measured M400 / M1000 as distance
  driven from GO (about 2 m short, roughly 0.05 s faster) and ran MMAX for 3
  laps, and saved records from them are kept as they are.
- `GT3R_RACE="tt_event=L0B0003"` runs a test (car, course, start from the
  event).
- **Medal model**: `race/gold|silver|bronze.prm` drawn by `render::PrizeView`
  (shared with GT mode's menus) with the file's animation and camera (fly-in
  over frames 0-120, then one turn per 200 frames; 60 degrees horizontal),
  over a dark backdrop from 0.6 s after the finish, the medal word and time
  on top. No model for NO PRIZE. `GT3R_RACE="...,prize=0"` shows a medal for
  screenshots.
- **Demo runs** (`race/demo_replay.*`): `demo/<event>` on the disc (48
  licence tests, 10 arcade time attacks) are the game's replay saves of the
  demonstration drive: controller input, re-simulated on playback (format in
  `demo_replay.h`; steering +-4096, positive = left; frame 0 = the race's
  first step, countdown included; the car record says AT/MT). A demonstration
  is played as a replay of its race record (`race::demo_record`): the race GT3's
  replay playback builds from the save (race replay information: the event under the licence or time
  trial conductor, the header's car record as the race entry as the program loads it: equipment,
  condition words, grid slot, pedal style, spec flag, wheel type, ASM, TCS; the start type as
  the race's start-up sets it: the event's +0x1F3, the time trial's start type the moving start 3) and car
  0's frames as the input device's recorder hands them to the pad read (the recording's
  T - 1, then empty frames; the stream read on past its block as the game's reader does). The race
  judges it and drives its end itself, and every demo plays as it does in the game: 7 of them end on
  the time the save records (A-1, A-2, A-3, A-7, B-1, B-2, IB-4); several within a few ms (A-6, A-8,
  B-6, R-1, IA-2); the rest, the arcade time attacks among them, leave the course in the game too
  (course-outs from 3 to 22 s after the line): the saves were recorded on a build whose race differs
  from the release's (the Japanese disc's data, same files, gives the same results). The licence
  menu's preview box is no movie: GT3 runs the demo live behind the menu and draws the scene into
  (32, 58, 280, 210); we do the same (looping, TV cameras, silent), and "Demonstration" runs it full
  screen. Test `exact_demo_records`.
  `GT3R_RACE="tt_event=L0B0003,demo=1"` starts it, `theater=<event>` as the
  Replay Theater does. The arcade time-trial demos (AT01..AT10) start like the
  licences (event speed 100..220 km/h, -100..-399 m on line 6); GT3 drives every
  flying run-up (licence, time trial, free run with a distance) with the CPU AI
  path until the line (physics §9.1).

## GT-mode flow around a race

- A finished GT race or championship round: RACE RESULTS overlay (car pictures in every row)
  -> the race-exit transition -> the prize page (cup with the prize money counting into the
  credits, or the consolation prize; `game::queue_race_prize`, shown by
  `make_presentation_screen()` over the race) -> the race's replay starts by itself -> its end
  or EXIT, the transition again -> the GT result quick menu (finished cars with their own race
  times, `--'--.---` for a car still racing when the race ended; no race behind it). A race left
  with EXIT: no prize, the replay all the same ("After a run"). `GameResult::prize_shown` tells
  GT mode not to show the page again. The result is latched when the live race ends (`RaceResultLatch`, as the
  original records the place and prize before the replay) and is what every exit posts: quick
  menu EXIT, replay pause EXIT, leaving from a replay; Retry drops it. The result menu's rows
  are kept with it (`latched_rows_`): the replay re-runs the whole field, so the standings
  after it are the replay's, not the race's. A finished GT single race
  is also written to the save at that moment (`game::save_race_run`). Screenshots: `gt3r --screen gtprize` (`GT3R_PRIZE=<event>:<place>`,
  `GT3R_GTMODE_PRIZE_FRAME=<n>`), `GT3R_RACE="...,quick=1,gtq=1"` for the GT result menu.

- GT-mode races and machine tests open with the game's pre-race quick menu
  (`qm_gt_event1.pmb`, championship rounds `qm_gt_event_champ1.pmb`,
  machine tests `qm_gt_mt_4001/10001/max1.pmb`; licence tests their licence menu
  with the disc's demonstration in the preview box; menu launches with `GT3R_RACE`
  set skip it unless `qm=` is given): Start, Free run (the
  course alone without laps; Exit in the pause menu returns to this menu),
  Setting (GT mode's car settings screen; the race is rebuilt with the ride car's parts and
  settings when it closes), Option, Exit.
- Bought wheels (`GameRequest::car_wheel`): the car model is loaded with
  `wheel/<label>` as its wheel.
- Title attract demo (`attract_demo`): an AI race, as GT3's: the demo event's cars all drawn from
  its pool (the demo has no player car), run with the race's play kind 2 as the race-flow byte
  (`ExactFieldOptions::config` 2, what GT3's demo runner passes: every car, slot 0 too, is a CPU
  car, the event's overrides varied by grid slot, no records); demo events start every car at
  their +0x198 speed, no countdown,
  TV cameras, no HUD; any button ends it (finished = false), else it ends
  after 1800 steps (30 s, the attract demo's timer) or 5 s after the finish. Its replay
  view is picked per demo (`race::pick_attract_view`, `attract_view_`): mode 0 MANUAL, mode
  2 with a random target or mode 1 AUTO, mode 1 forced after two demos without it.

## Pit stops and tyre wear

- **Wear** (physics §8): endurance events carry the wear bytes
  (+0x1BD..+0x1C3); the race configuration turns wear on as the game does.
- **Tyre gauge**: `tire_gage` at (20, 435), each tyre body (alpha codes
  5/10/15/20 in GS units) in the colour of its wear level (-63..127: blue,
  green, yellow, orange, red; black <= -64), outlines dark grey. Shown when
  wear is on.
- **Pit lane** (physics §8.6, ai §13): the race's own pit lane (the exact core's pit lane state
  machine and the AI's pit switches, `sim/exact/pit_lane.h`, `tick_pit.cpp`): markers are the
  points of AI line 4 (file line 3); entry at its point 1 on the pit surface -> AUTO DRIVE; the
  race's AI drives the car (driver kinds 2-4) through the lane, the stop in the box (new tyres)
  and back to the main line. HUD: "AUTO DRIVE" and "<car> Pitting in..." (green, (320, 130), 3 s).
- **CPU cars** (ai §13): with wear on, a car is armed when the race sets "tires worn" and enters
  at its next pass of the stop line's first point (not on its last lap); then the same lane, stop
  and exit. "<car> Pitting in..." for every car.

## Course-out; no respawns (physics §11)

- GT3 never respawns or resets a car, and neither does the game: no player respawn, no CPU
  respawn, no fail-safe. **Course-out** is the race's own rule in Time Trial and License only
  (the exact core's race flow): all four (grounded) wheels on grass / gravel / sand (materials 2,
  3, 4, 6; never on a dirt course, course table +0x08 = 2: arcade time trials 8 and 9, the rally
  tests), or a wall hit with a speed change above 10 m/s (car-car hits never count), or a pylon
  touch. The lap is not recorded, FAIL is shown and the car is braked to a stop; then the quick
  menu (Retry). Four grounded wheels on the pit lane inside the course's pit span fail too (the
  span as in "Licence tests", "The pit lane"). Free runs have no course-out.
- **After the run** (ai §1.1, physics §9.4): the race hands the player's car to its AI. After a
  race, time trial or machine test it drives on along the main line (driver kind 1) at 80 %
  corner speed and half throttle (CPU finishers get the same slowdown); after a failed licence
  test or time trial (course-out, wall) it stops with the wheels straight (kind 7); after a stop
  or section licence test it stops along the main line (kind 6); once still it is parked (kind 8:
  brake off, clutch request 0 so the clutch opens and stays open, held in place). Replays play the
  same: the race decides the kinds again. Restart / Retry stay.

## Original resolution and field of view

- `[video] original_res = on` (or `GT3R_ORIGINAL_RES=1`): the 3D scene is
  rendered at the PS2's 640x448 into a 4:3 frame (pillarboxed) and upscaled
  with `[video] original_filter = nearest` (default) or `bilinear`
  (`DrawOptions::fixed_width/height`, `nearest`); the HUD is drawn in the 4:3
  frame with the original layout (no 16:9 widening, `HudData::four_three`).
  2P split screen keeps the normal path.
- Field of view is Hor+: every camera fixes the vertical FOV and the
  horizontal one follows the aspect, so 16:9 shows more at the sides than
  4:3 and nothing is cropped vertically. (GT3's two player views, the pre-race
  shot and the replay cameras use the original's FOVs, see "Cameras"; our
  extra views use our own values.)

## Sun glare and course effects (Env0 / GTFX / BLBd)

From the race workstream's research (facts for docs/formats/course.md, owned
by the course area):

- **Env0** (slot 32) is the course's environment block, read every frame:
  flags at +0x10 (car lights 0-3, ambient particles 0x20, colour-grade pass
  0x80, sun 0x400, scene lights bits 11-14, car lamp glows 0x8000, GTFX lamps
  0x200000), far clip +0x20 (the sun sits at far/2), texture / car LOD
  thresholds +0x50 / +0x54, fog RGB +0x60, distance A +0x70 and minimum
  visibility b +0x74 (visibility = b + (1 - b) * min(1, A / z)), four car
  lights at +0xD0 (0x80 each), a colour-grade curve +0x980..988 with
  levels black / white +0x98C / +0x990 and saturation +0x994, the sun at +0x9C0, eight lamp types at +0xC60 and four
  scene lights at +0xAD0.
- **Sun** (+0x9C0..+0x9E8, flag 0x400): RGB, occlusion weight, glare size k
  (angular half-size atan k), disc size / gain, elevation / azimuth (degrees;
  direction (sin a cos e, -sin e, cos a cos e) with e = -elev, a = -az),
  style (0 = `race/glare.img` 512x512 star-burst sprite, 1 procedural
  72-spike burst, 2 glow disc), occlusion box N. The original draws one
  additive screen-aligned billboard at the sun after the scene, coloured RGB
  x visibility from an N x N depth test at the sun pixel; no ghosts, no
  falloff towards the centre, no bloom. A sun disc (14-segment fan) is drawn
  right after the sky dome. Trial Mountain: elev 31, az 92, k 0.40; Seattle
  16.8 / 220.2 / 0.45; Laguna 11.6 / -31.6 / 1.10; Tahiti dirt and Midfield
  have no sun.
- `race::SunGlare` draws the glare billboard (style 0, `race/glare.img`
  for every style). Visibility: the original depth-tests an N x N pixel box
  around the sun; we cast a 3 x 3 grid of rays through the same angular box
  against the course's opaque scenery and backdrop triangles
  (`race::Occluder`, an XZ grid; about 0.1 ms) and use the open fraction.
  The sun disc is not drawn yet.
- **Fog**: the course's Env0 fog replaces our linear placeholder: the model
  shader's `fog_gt3` mode, amount (1 - b)(1 - min(1, A / d)) towards the
  fog colour (`GT3R_NO_COURSE_FOG=1` restores the old fog for comparison).
- **Colour grade** (flag 0x80): the post-process pass over the finished 3D picture
  (above, "Post-process pass"), with the Env0 levels +0x98C / +0x990 (black / white),
  curve +0x980..988 and saturation +0x994, in GT3's 8-bit arithmetic; live on the flag
  (Monte Carlo, Trial Mountain, Akasaka, reverse Deep Forest, reverse Grand Valley), in
  replays only under a view mode 1 shot's levels or filter.
- **Lamp glows** (`race::LampGlows`, GTFX + lamp types, docs/formats/course.md
  "GTFX"): the lamp lists of the nodes visible from the camera (slot 2 Vls0,
  `course::Visibility`), at most 100 lamps passing the original's screen test
  (halo-sized), its attenuation, directional factor, blink, and
  `race/flare.bin`'s three meshes (the six-point star) drawn with
  `SDL_RenderGeometry` in the original's colour arithmetic (vertex RGB × colour,
  alpha × visibility, Cs·As/128 as two additive passes), crisp at the output
  resolution. In the original-resolution frame they go into a 640 × 448 layer
  added with bilinear filtering, as the PS2 shows them: the rays are 1-2 pixel
  slivers there, soft and broken up (compared with a PCSX2 capture, 2026-10-22).
  At high resolution that layer mangled the diagonal rays, so it is only used there. Visibility: a 3 × 3 ray
  grid over the original's 10-pixel box, to 0.2 m in front of the lamp, against
  the course occluder (cars do not occlude; the original's depth test sees
  them). The halo is still a soft disc (the original draws a 72-spike burst
  mesh), tested with one ray. Seattle 69 lamps (stadium lights), Route 5 / 11
  about 300 (street and tunnel lamps).
- **Billboards** (`race::Billboards`, BLBd): every sprite as a quad turning
  about the vertical to face the camera (vertices re-uploaded when the
  camera turns, `GpuModel::update_vertices`), alpha-tested, unlit vertex
  colour x 2. Tahiti dirt 596 trees, Seattle 307, Rome 1073 spectators.
- Slot 23 (a small sky dome) is drawn only into the cars' reflection map (see "Car reflections").
- **Car lamps** (research notes, not in the repository): every car (player, 2P, opponents; replays
  from the recorded pedal) keeps a 4-frame history of "brake pedal > 0"; k = frames on.
  Lamp materials (GTM1 material flag bit 0) add their emission x k/4
  (`Material::lamp_emission`, `DrawItem::lamp`), and the node programs' op 0x06 switch
  with selector id 0 takes its "brake pressed now" branch (lit lens chunks;
  `gtm1::RunParams::brake`, a second body model `CarVisual::body_brake_`). Car model time
  of day from Env0: flag 0x80000 `cars/eve/`, else 0x8000 `cars/night/`, else
  `cars/day/`. On Env0 flag 0x8000 courses (night and dusk: route5 / route11 families,
  L_circle*, seattle, laguna, smtnorth, r_deepforest, r_grandvalley, r_smtsouth ...) the
  car lamp glow pass (`race::CarLampGlows`) draws the GTCI lists (+0x40 headlamps always,
  +0x50 brake lamps while braking at k/4 or 1, +0x48 tail lamps as a halo only) with the
  Env0 +0x9F0 lamp type (colour x intensity, glow x 8, halo, gain, attenuation c / l / q,
  max distance). At most 100 records pass the screen test at the record's test point (in
  front, within the max distance, a square of halo x max(a, b) on screen). Round head and
  brake lamps (|a - b| < 0.01) get the `race/flare.bin` star (`race::Flare`, as the course
  lamps) at the lamp, its depth moved by + 0.5 x the viewing axis' view-space z, scaled by
  glow x a, colour x D x I with D = (viewing axis . normalize(test point - camera))^n (1 for
  n = 0; a negative product: no glow) and the occlusion box's visibility at the test point;
  tail lamps never get it. Every kept record gets a halo (colour x gain x 0.5, not D or I);
  stretched (bar) lamps an elliptical one. No reversing lamps and no headlight switch exist.
  The 10x10 depth-box occlusion test is a 3 x 3 ray grid here (`race::Occluder`; cars do not
  occlude). The car lamps' streaks on wet roads
  belong to the wet-road reflection (below).
- **Wet-road reflection** (`race::WetRoad`; courses with a slot 9
  model: route5, route5_dry, route11 and their reverse layouts, three L_circle tests). After the
  scenery and before the cars: the frame alpha is cleared to 0 and slot 13 (alpha-only packets,
  alpha test off, depth-tested, no depth write) writes the mask, texel x vertex alpha; a small
  buffer (Env0 +0xC24 x +0xC28, 64 x 128 on all of them), colour and depth cleared to 0, gets
  slot 9 (the scenery stored already mirrored below the road; the main view and projection
  unchanged) with its own fog (Env0 +0xC40: RGB, +0xC50 A, +0xC54 b; route5 none, route5_dry a
  constant 27 %, route11 A 4.5 / b 0.17, black), the cars mirrored about the road plane under
  each (Env0 flag 0x10000, wet route5: body and wheels; `CarVisual::add_mirror_items`) and the
  lamp streaks (flag 0x20000); then Env0 +0xC2C blur passes (the buffer onto itself, bilinear,
  -0.5 / +0.5 texel; wet 1, dry 6) and the composite over the frame (bilinear, net -0.5 texel)
  with Env0 +0xC20: 0x54 (Cs - Cd) Ad + Cd on the wet courses, 0x58 Cs Ad + Cd on the dry
  ones. Streaks: per car the headlamps (I 1), brake lamps (k for flag-bit-0 records, else 1 while
  the pedal is down) and tail lamps (0.3) at the lamp's mirror image; c = cos(lamp axis, eye ->
  lamp), skipped when negative or behind the eye; a camera-facing quad 0.5 m nearer the eye,
  half width (E74 + (E70 - E74) c) x lamp size (+0x40), from min(E78, lamp height) above the
  image (the road surface) down to E7C below it, `race/reflection.img`, colour lamp RGB x I x c
  x E60..E68 x E6C, added; no attenuation or occlusion (wet route5: width 0.3, cap 1, length 1.8,
  gain 1; route5_dry 0.5 / 1 / 0.5 / 0.3; route11 0.63..0.03 / 0.6 / 0.2 / 0.07). Ours: the
  buffer and blur through `View3D::render_offscreen`, the frame items (alpha clear quad, mask,
  composite quad with `DrawItem::texture` / `dest_alpha_blend`) just past the near plane,
  `in_order` items inserted right after the scenery (before the cars and their shadows); the frame alpha's GS range above 0x80 (weights up to 2) is clamped
  to 1; the main view only (not 2P halves or the mirror). Slots 9 / 13 were drawn as visible
  scenery before.
- **Draw order of translucent scenery**: the PS2 draws a course's packets in file order, opaque
  and blended interleaved, all before the wet-road pass and the cars (presentation §I7).
  Trial Mountain's tree avenue has untextured white light-shaft sheets (vertex alpha <= 0x34)
  late in the file; drawn earlier (our old per-mesh distance sort) they blocked the canopy and
  the road's tree-shadow overlay behind them (rectangular holes). Drawn after the cars (our old
  blended pass), Laguna's ground overlay node 575, which floats 10 to 20 cm above the road near
  x -323, z -292, cut a translucent road-coloured band across the lower body of the time-trial
  demonstration's Viper. The course scenery item is drawn `DrawItem::in_order`: its opaque
  meshes, then its blended ones in model order, before the next items' opaque meshes (cars,
  pylons); the distance-sorted blended items (billboards, smoke, glass) come last.
- **Lamp glow**: Trial Mountain and Deep Forest have no GTFX lamps (Env0 flag 0x200000 off) and
  no bloom / haze pass exists in races; their tunnel lamps are plain fogged scenery quads whose
  glow is painted in the 32x32 texture times an over-bright vertex colour (up to 1.99).

## Controller force feedback

Once per frame while someone drives (not paused, in replays, menus, after
the finish or with Vibration off): `ForceFeedbackState` with speed, the
applied brake, ABS, the steering force -(fy_FL + fy_FR) / front axle load
(tyre fy points to the wheel's right) and a jolt from wall / car impulses
(/ 5000). Player 2 too in a 2P battle.

## Performance

`Race perf [...]` is logged every 3 s (frame time, 3D pass time, meshes
drawn/culled, draw calls, triangles, active voices). This Mac (Metal,
4× MSAA, 2560×1440, 120 Hz vsync), auto-drive, `gpusync=1`:

| Course | Frame | 3D pass (CPU+GPU) | Meshes drawn / total | Draw calls | Triangles |
|---|---|---|---|---|---|
| Trial Mountain | 8.3 ms (vsync) | 1.3–2.0 ms | 41–119 / 400 | 330–1100 | 11–32 k |
| Seattle | 8.3 ms (vsync) | 1.8–3.7 ms | 46–291 / 400 | 710–6070 | 19–155 k |
| Rome | 8.3 ms (vsync) | 0.3–0.5 ms (no sync) | 113–199 / 812 | 920–1870 | 25–53 k |

## Known gaps

- The road sheen follows docs/formats/course.md
  "Lighting"; checked against a lossless PCSX2 capture at Super Speedway (bumper view on
  the grid, facing the sun): the shape matches, ours adds about 25 % more (+44 vs +34
  levels), which our own bumper camera (field of view and yaw are not the original's)
  accounts for at least in part. Its mask is the PS2's: the course scenery draws with the
  frame-buffer alpha (`DrawItem::frame_alpha`: every pass writes its texel x vertex alpha,
  FBMSK 0xFF000000 passes keep it, the alpha-only shadow masks write only it), and the sheen
  adds Cs x Ad (`BlendMode::DestAlphaAdd`). The sheen pass's own alpha (0x7F x texel alpha)
  is not written to the frame alpha (FBMSK 0 on the PS2): only later ALPHA 0x58 passes over
  the same pixels would see the difference.
- Course environment passes (chunk kinds 2 and 3) follow the original's kernels; the
  op 0x31 yaw point is taken from the chunk's own node (the original keeps the last one set in
  draw order, culled nodes included). Checked on Route 11's grid (the facade and the round
  building's reflections match the original) and at Laguna's media tower (arcade time trial,
  stopped about 66 m past the line, turned right: walls within 4 luma, the windows' sunset
  glare in the same places).
- Near-road "shimmer" at speed (Super Speedway, Trial Mountain) is the sheen's noise texture
  (256x128) moving under the camera; the original shows the same grain. Our textures are
  mipmapped, so this is not minification aliasing.
- Lamp glows (see "Lamp glows", "Car lamps"): the halo is a soft disc (the original draws a
  72-spike burst mesh), and cars do not hide lamps (the original's depth test sees them).
- Not drawn yet: the sun disc (see "Sun glare"), the cars' visual camber (see "Rendering"),
  the tachometer needle's motion blur.
- Car rear-window reflections match the original in matched chase views at Laguna (dusk, 50 vs
  46 luma), Route 11 (night, 9 vs 9) and wet Route 5 (7 vs 9), but are darker at Super Speedway
  (day) in all nine matched shots, by a factor that follows the car's heading: 1.2x along the
  back straight (four positions, identical values), 1.5x side-on to the sun, 2-3.7x facing it
  (grid 20 vs 70). The body matches. (Measured before the reflection cube's faces were turned
  the way the lookup reads them; not measured again since.)
- Vertex colours above 0x80 (up to 2.0 under MODULATE) are clamped to 1.0 by the decoder; a
  few course packets use them (e.g. Laguna's pavilion roof reflections, raw 0xFF).
- The cars' reflection map draws slots 23 and 5 with the course's scene lights; the original
  leaves whatever the last car left in the light slots (in that pass's eye space).
- The visibility lists (Vls0) are used only for the lamp glows' lamp lists: for the scenery
  they only spare the PS2 drawing hidden nodes, and frustum / distance culling per node keeps
  us at the vsync (see "Performance"). Course node programs have no LOD switches: run
  on Trial Mountain, Seattle, Route 5 and Rome, the only op 0x07 switches
  executed are the car models' (n = 4).
- Kerb / off-road classification for rumble comes from `track::surface_type`
  (being refined by the track area).
