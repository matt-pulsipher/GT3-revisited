# GT3 vehicle physics: behavioural specification

Implemented by `src/sim/` (see docs/dev/PHYSICS.md).

This is a behavioural description of how Gran Turismo 3's vehicle simulation
behaves, written from study of `CORE.GT3`. It contains prose, equations,
constants and field tables only. Implement from this text; do not look for
code here.

Tags: **[C]** confirmed by reading the code (function address given),
**[I]** inferred (plausible, not proven), **[?]** open.

## Changelog

- 2026-11-17 (the later builds, the sparks in the race): `race_tick_exact`'s `#sparks` and `#scrape` races run the
  sparks' frame between the race steps with every car's real spark points (the scraping pad car flat out along the race
  line), the register and the whole pool compared against the original's every frame; 0 differing on GT3's three discs
  and all five later builds (§14.4), every count of the default runs unchanged.
- 2026-11-17 (the later builds, the sparks): §14.3 `SparkEmit` and the sparks' row: the burst, the per-particle update and
  the tuning block are GT3's code on every later build (the particle record 4 bytes longer), 2002 Tokyo-Geneva's constants
  folded toward zero; from 2002 Tokyo-Seoul v2.00 on the emission starts each burst on the plane of the car body's drawn
  ground point and attitude and bounces it there, not on the road's (`sim/exact/sparks.h`, `SparkRules`).
  `sparks_burst_update_exact` and `sparks_emit_exact` run on every program and pass on all five later builds (§14.4), every
  count of the existing tests unchanged.
- 2026-11-17 (the later builds, the leaves): §14.3 `Leaves`: the tire effect emitter's leaf calls on ground with flag
  0x20 (Autumn Ring's verges), the leaf maker's draws of the VU0 random register, the leaves in the sparks' pool and
  their update (`sim/exact/leaves.h`); `leaves_exact`, `leaves_emit_exact` and `race_tick_exact`'s leaf checks pass on all
  five later builds with every count of the existing tests unchanged (§14.4).
- 2026-11-17 (the later builds, sixth pass): §14.3 three more rules and two more tables: the single race's flying start
  on 2001 Tokyo and Tokyo-Seoul (`FlyingGrid`, the table `FlyingStart`: `race_start_exact` passes on those three discs),
  the collision step's stuck nudge (`StuckNudge`: only a pad car that touches something; `model_collide_exact_step`
  passes on every disc), the layout 7 system (the table `Hybrid`; the showroom's power with the motors, `gt_grid_exact`
  passes; in a race the store, the rear motor, the front motors and the engine's charging, 0 differing in races with a
  layout 7 car on every later build) and the later builds' tanf (`libm_tan_exact`); §14.4 what remains.
- 2026-11-17 (the later builds, fifth pass): **2001 Tokyo races on its own core** (§14: `own_core_ready()`): its whole
  race step is proven on the disc, `race_tick_exact` passes there (170 ports and whole-step checks, 0 differing of 54.4
  million calls, a stepless car and the special car in a race included). What it took: the whole-step checks and the
  register ports run on the later builds (the step's places pinned for every build, §14.4); GT3's 24 rules each have
  their row on the later builds (PAL's rubber band cap, ghost rules and lap record, NTSC-U's licence stop but for
  Tokyo-Geneva's constants, §14.3); the stepless gearbox's three spots and its gear step, proven in a race; the special
  cars' start state; the mood tables in the race inputs; the later words of the record carried out of the start and
  kept in a race record's snapshots and hash (only where they hold something: GT3's records are unchanged).
- 2026-11-16 (the later builds, fourth pass): §14.3 fourteen more rules with rows for all eight programs, read from the
  2001 Tokyo listings and checked on the discs: the axle step with the stepless gearbox's torque, the parameter
  converter (layout 7's mass, the stepless hold speed and top speed, the rear ride height's own byte from Tokyo-Seoul
  on), the pad's clutch and control frame, phase 3's steering percentage, the AI's outputs, decision (the stop point),
  traffic step and braking, the ASCC's switches, the tire effects' surface 8, the pair gap and contact intensity, the
  run-on after a finish; two more tables (the stepless settings, the stop zone and ASCC switches); §14.4 2001 Tokyo:
  `model_car_params_exact` passes, 130 of 137 race step ports clean (105 of 136), 34,320 differing port calls of 43.3
  million (3.1 million), what is left is the harness's but for one call; the ranked list of what remains before a
  later build's own core; §14.5 the stepless tick, the stop zone and the pod race's rule, the layout 7 system as read.
- 2026-11-13 (the later builds, third pass): §14.3 thirteen more rules with rows for all eight programs: the race
  step's four routines GT3 does not have (the special cars' moods, stop count and wings, the layout 7 car's byte), the
  special kinds and the gearbox kind in the spec, the drive layout with AWD system 3's settings, the driveline block's
  gearbox kind, unequal-axle flag and stepless ratios, the AI's driveline class, the AWD system 3 gain, the stepless
  gearbox at the moving start; three tables only the later builds have; §14.4 new counts (2001 Tokyo: every car's spec
  the same, 105 of 136 ports clean); §14.5 what the four routines are, the layouts' renumbering, the steering force's
  step count in the conductor.
- 2026-11-13 (the later builds, second pass): §14.3 five more rules proven on the Concept discs (the computer drivers'
  spread, the moving start's throttle, the ground query's height off the road, the wear distance of 0, PAL's variants
  of the oversteer indicator, pad vibration and steering force); §14.4 the race tests run on the Concept programs, with
  what differs per port; §14.5 new: the later builds' record layouts, the race step's four extra per-car routines, AWD
  system 3's tuning bytes.
- 2026-10-26 (the pad output): §1.2 the pad output read in full [C]: `0x1E0618` → `0x2107D8` → the port's
  actuator bytes, pushed and cleared every frame; the large motor gets the vibration level (car+0x16D5) as it is,
  the small motor never runs in a race, and a car the AI drives sends nothing (its motors stop). The race's
  rumble now follows it (`race::RumbleModel`); the old kerb / off-road / impact / wheelspin rumble is gone.
- 2026-10-26 (old physics removed): `sim::Vehicle`, `sim::World`, `src/sim/models.*` and their placeholder body are
  removed; every race runs on `sim::exact`. The menus' car figures and the computed gearbox's ratio generator stay in
  `sim/car_spec.*` (the generator now in the PS2's arithmetic, `car_gear_generator`), the engine condition's rules in
  `sim/engine_condition.*`. Earlier entries name the removed code where it implemented a rule.

- 2026-10-26 (the pad frame recordings, the ghosts and the Analyzer's race, `sim::exact` `control_record.h` and
  `race_flow.h`): new §13.6, the recordings (AutomobileControlRecord: the recorder's bit stream and frame coding, its
  managers and stores, the solo race objects' input devices, the ghost taking a recording `0x17AAD8`), checked against
  the original's own recorder in `control_record_exact` on all three programs and the solo modes' ghosts on the
  original's own recorders in `race_ghost_exact`; §13.4 the Analyzer's race (tracks A and B in one race, B as car 1) and
  the REPLAY view's jump (`0x1873B8` -> `0x1E0AD8`: the car from the logger entry's snapshot, its playback sought to the
  entry's frame, the race on from there; after a jump the race is the pass's again, bit for bit); §13.5 the ghost car
  reads its frames from its playback, which never gives a recording's last frame.

- 2026-10-26 (wave 3 gaps, `sim::exact` and the game): §9.2 a grid slot past the course's table reads what follows the table
  in the road block (with the loader's pointers), checked in `race_start_exact`, `race_tick_exact` and `race_exact_lockstep`;
  free practice's pit box with too few stop-line points reads outside the line, and the GT event menu turns FREE RUN off on
  dirt courses (the only courses of GT mode's races without car 0's box); §13.2 the licence's sector times past the fourth run on into the race
  entry; §13.4 the Analyzer's pass and its samples (`0x135788`); §13.5 the free run's recorders live for the visit. The
  course's rigid bodies (pylons) run after the race step in the frame (RacePS2Base::vf29: RaceBase::vf29 runs vf30, then
  `0x1A21E0`), so a touch's course-out flag is first read by the next step.

- 2026-10-26 (wave 3, the race modes' remaining paths: the free run's lap handlers and ghost car, the data logger, the
  race display's events and the time records ported bit for bit, `sim::exact` `race_flow.h`, checked in `race_tick_exact`
  and `race_exact_lockstep` on the NTSC-U, PAL and NTSC-J programs): new §13 (the race event queue and the display
  events' words, the entries' and session's time records, the single races' sector order, the data logger's table, the
  free run's recordings and the ghost car's restart with the race step's entry mode 1). Corrections: race info + 0x70 /
  + 0x74 are the data logger's (playback / its table recorded), not a ghost recorder's; the free run's AI drive-on needs a
  ghost car or + 0x74; the sector order's lead laps start at -1 (`0x1EE7E8`); the race step's entry mode 1 still runs the
  rubber band. Versions (§12): PAL counts a crossing's remainder and the snapshots' clock phase in 60ths and posts no
  display events for a ghost car; NTSC-J has no general time event (its queue ids from 15 on one lower), keeps sector
  times without a lock test, has no race-flow byte test in the time differences, does not rebuild the ghost car's body,
  and its free run tests the recorder's + 0xF4 for a usable lap.
- 2026-10-26 (the tables as values): §12 the 20 program tables are written out in the source (program_tables.cpp) and
  the locator checks the disc's against them; a program that is not a GT3 release races on GT3's tables and NTSC-U's
  rules. No rule changed.
- 2026-10-25 (wave 3: the whole race step as one object, `sim::exact::ExactRace`, checked step by step
  against whole original races in `race_exact_lockstep`): §5.3 the collision hook-up (the copy-in /
  copy-out fields, each response's report `0x1DFEB8`: the conductor's virtual 41 and the car's hit
  record `0x1DFD98`, the impact effects `0x1FB890` whole: the hit sound's hold and the rumble at
  body +0x4E0 .. +0x4EC and car+0x1474); §1.2 the race frame around the step (the race
  information's start countdown `0x17A5E8` before it, the end countdown `0x1F2B18` and the other
  tail calls, the result block `0x17B030`); §3 tyre effects: the emitter `0x1A46C8`'s rule and
  particle, and the race initialisation's `0x1DE070`, which overwrites the tyre effect kinds'
  parameters in the course's Env0 block from the program's table `0x2D3510` (the file's values
  there are never used).
- 2026-10-25 (the exact core on every disc: `sim::exact` branches on the program version, every exact test run on the
  NTSC-U, PAL and NTSC-J discs): new §12 Program versions. US 1.00 and 1.10 are the same program; PAL runs the race step
  50 times a second (dt 1/50, its own tick counts) and also changes the rubber band's cap, the AI's oversteer indicator,
  the AI cars' pad outputs, the licence stop speed and the record rule; NTSC-J has no rubber-band virtual, one battle
  conductor, a turn-at-a-time angle wrap and a collision commit without heading wrap or yaw clamp.
- 2026-10-25 (regional notes): §12 confirms from the PAL program that it has no 60 Hz path (constant PAL display mode,
  no branch at the step length or the counts), lists the tick counts as a table, and records the demonstrations
  (`demo/%s%s_p` in PAL) and the engine's behaviour per disc (60 steps a second with the disc's rules; NTSC-U rules
  online and for demos; PAL at 50 only in the diff harness), the US medal times on a PAL disc, and NTSC-J's event ids
  (one lower from 14 on), battle lap handler and replay lap-start restore; the tables and rules found by masked-code
  hashes in any build (per routine: `sim::Rule`). No rule changed.
- 2026-10-25 (wave 2 integration): §4.3 / §8.6 F+0x4DE is the car's pit box on the stop line
  (game line 4: its count − (index + 4)), not a grid slot (wording only; `sim::exact::Car::pit_box`).
  The race position F+0x4E1 is signed (the program loads it with lb). No rule changed.
- 2026-10-25 (the pit lane's state machine `0x1EDB40` and the end of a run `0x1EACB8` ported bit for bit,
  `sim::exact` `tick_pit.cpp`, checked at every call site and in the whole race step in
  `race_tick_exact`): §8.6 the box is line 4's point at the car's grid slot byte F+0x4DE; a stop is
  wanted when a wheel's wear differs from the fresh value, a wheel's +0x28 byte is set or the
  conductor's virtual 42 says so (free practice: always); a car in the pit lane past line 4's
  point 1 off the pit surface leaves it at once; the exit is line 4's point n − 1 for CPU cars and
  finished cars (point n − 2 for a human not finished), a finished car gets the end of a run (mode 3)
  there; a CPU car hit (collision body +0x43C) on the pit surface enters the pit lane at once in
  recovery state 2; the wrong way on the pit surface; the HUD messages.
- 2026-10-25 (exact race start): every byte of every car's record as the race's initialisation
  (`0x1F3628`) leaves it, ported (`sim::exact` `start.h`, `ai_setup.h`, `course_query.h`) and
  checked on every byte of thousands of race set-ups under every conductor (single races, time
  trial, licence, free run, machine test, both battles, free practice; `race_start_exact`). Corrections and additions: §9.2 every car
  faces slot 0's yaw + π (its own slot gives only the point); the start pitch is the ground's
  plus atan2(ride height front − rear, wheelbase) and the CoG sits at the ground point + up.z ×
  P+0x3C after it; modes 1–5 clamp the distance to ±(lap − 20 m), take the height from a ground
  probe at the line's height (again 1 m higher from no segment when it misses) and are lap 0 /
  sector 0 behind the line; §9.1 the time trial and free run conductors give a CPU entry control
  source 1, a player 0 (1 in configurations 2 and 3), car 1 is a ghost (F+0x3EA 1, free run 2);
  mode 3 is the course driver set's flying start (class 0's bytes 25 / 26: −10 × b25 m at
  5 × b26 km/h); a 2-player battle without a handicap gets no start speed; §9.3 the shift
  lock-out is 12 only for an AI-driven car (control source 1, a driver kind) or a pad car with
  pedal style 0 or 2, else 0; after the cars the conductor ranks them (virtuals 2 / 3: laps,
  then progress; free practice by best lap); a licence test without the event's flying start
  is mode 1 at its distance (`0x1F32F0`); the machine test's progress is the nose's (`0x1EE878`)
  and its sectors its type's (`0x1F06C0` / `0x1F0780`); §4.3 the spawn hold's "grid table" is the AI
  course object's stop line (game line 4), F+0x4DE the car's pit box on it (the line's count −
  (index + 4)); §6.6 an arcade car (table 35, which `0x231DB0` looks up first) keeps the
  setters' gear ratios, drivetrain split, brake controller, turbo, motion ratio and ASCC 2–4
  words and takes the others from its record (`0x234150`).
- 2026-10-25 (exact tick inputs): everything the tick reads outside the car record is built as the
  race's initialisation builds it (`sim::exact` `race_inputs.h`: the program's own tables read
  from the user's CORE.GT3, the event and course records, the race mode's conductor) and checked
  against the original's memory in races under every conductor (single races, time trial,
  licence, free run, machine test, both battles, free practice). Corrections and additions: §5.2
  the "other global" byte0 / 10 is the air density 0x352920 (`0x1DB020`), read by the car's
  converter; §8.3b `0x1DE830` first clamps its source block in place (`0x1DE1E0`: every curve 4
  points, channel 0's first y of each half 0, the speed threshold at most 50 / 3.6 m/s, channel
  1's base 50..200, four threshold pairs kept ascending) and builds four surface classes (the
  tick reads 0..2); §11 the course-out rule's pit stretch is the AI course's game line 4 (the pit
  stop line), its points 1 and n − 2, the progress taken a lap on below half a lap, none with
  fewer than 6 points; §4.3 the spawn hold's slot heading is the same line's point at the car's
  pit box F+0x4DE (`0x1F9E10`), not a grid table; §1.1 the race-flow byte is virtual 23's argument (the race's own setting),
  the scale +0x37B0 is 1 from virtual 21; the free run conductor's virtual 22 and pad hook ask the
  race object for a second input device. The race flow after phase 13 in every race mode but
  free run (whose lap handlers run its second race object): `0x1EF060`, `0x1EE978` with the
  conductors' virtuals 17 / 18 / 19 (first crossing, lap, split line), 28, 30, 31, 33, 34, the
  virtuals 13 (the single races' rolling start, the free run's run-up, the time trial's course-out
  end) and 14 (the licence's judging, the race clock), then the gap matrices `0x1ED458` (conductor + 0xB8F4 laps, clamped to
  ±127, and + 0xB934 metres of every car behind every other, which the AI's queries read) and the
  ranking (virtual 3: `0x1ED1F0`; free practice's `0x1F24C0` by best lap, car + 0x16C4) in every
  race mode, ported and checked against the step (presentation.md "Race order and finish"
  describes it); the step runs its hooks after phase 13 (virtuals 13, 1, 14, 3 and `0x1ED458`)
  only in its entry mode 0; §11 the wrong-way flag F+0x535 bit 0 is set while the progress goes backwards
  (not only at the line) and the message needs a pad car of driver kind 0 that hit no wall or car
  that step; §11 the time trial's course-out ends car 0's run in its virtual 13, before the cars'
  race flow (event 10 only for index 0; stop driver mode 2), the licence's every car's in its
  virtual 14 (event 9, result −1); the time trial ends the run at every lap (stop driver mode 3,
  event 5, the end countdown); the machine test's at its split line (its table's last entry,
  after keeping the top speed car + 0x16AC); a licence test of type 1 when stopped in the zone
  (its time (clock + int(50 × the speed in km/h)) / 3), type 2 at the split line,
  type 3 with its laps; the split-screen battle has no time limit and hands the other pad cars to
  the AI when a car finishes; the best lap car + 0x16C4 is kept by the lap time's display event
  (`0x1EC620` → `0x1EC4C0`; the licence's for the car of index F+0x3E8); the hard wall hit's
  course-out (virtual 41, `0x1DA130` through `0x1DFEB8` at the collision step's hits) is only for
  the car of index 0 that has not finished, and every hit of kind 2 sets it too.
- 2026-10-25 (exact tick, wave 1 together): every phase of §1.2 but the collision step (10) and
  phase 3's AI paths ported in the program's arithmetic (`sim::exact`); the whole phases 7
  (`0x1E8598`) and 12 (`0x1EB218`) at their race-step calls, and the race step's phases for every
  car around the collision step (`tick_until_collision`, `tick_after_collision`), verified bit
  for bit against the whole step in races (`race_tick_exact`). No rule changed.
- 2026-10-25 (exact tick, phase 7 tyre / planar half): `0x1E84D8`, `0x1E73C0`, `0x1E7450`,
  `0x1E7C08`, `0x1E7848`, `0x1E7D90` (with `0x1E7AD8`) and `0x1E8008` ported in the program's
  arithmetic (`sim::exact`) and verified bit for bit in races. Corrections: §4.1 the stop hold
  zeroes F+0x488/0x48C/0x47C/0x44C/0x450/0x4D8/0x4C4/0x4C8/0x474 (a_y is zeroed too); the race
  hold byte F+0x4DC is the start-sequence hold; the airborne flag is the four loads' sum equal
  to 0; a_y and the yaw acceleration multiply by P+0x4C / P+0x11C. §7 #19 the angle difference
  is in [−π, π), not (−π, π].
- 2026-10-25 (phase 12, exact): the suspension and attitude half of `0x1EB218` ported bit for bit
  (`sim::exact`, race_tick_exact). §1.2 phase 12's call order. §4.2 the anti-roll sign is pinned
  (subtracted on the even wheel, d = x'₀ − x'₁ of the clamped coordinates, [C] was [I]); the
  longitudinal transfer uses F+0x4D8 − F+0x4D0; the body force holds the spring part only and the
  order of its sum. §4.3 the heave step also writes F+0x4CC; the spawn offset's exact formula and
  where the slot heading comes from; the terrain factor and the hold read the previous step's ground
  frame. No rule changed.
- 2026-10-25: §4.6 phase 6 (`0x1E3B18`) ported bit for bit (`sim::exact::aids`, `race_tick_exact`):
  the dispatch by control source / driver kind / hold counter written out (a pad car with a driver
  kind ≠ 0 runs the driver's block, its status bytes then from its own block), the indicators'
  exact rules moved into §4.6 (thresholds and gains are the static bytes 0x2D295D..60); ASCC
  status 2 only when channel A or D acted (C and E acting leave it; the spec said any channel).
- 2026-10-25: §11 course-out: no soft-surface failure on dirt courses (course table +0x08 = 2;
  licence / time-trial conductor vf38 via `0x1F2F90`) [C]; the pit-lane failure only inside the
  pit stop line's span (`0x1FA5B0`) [C]. The dirt rule is implemented (public issue 18: R-3's
  run-up starts on grass), and so is the pit-lane span (`race::PitSpan`, licence tests and time
  trials, by the car's progress as the tick began; disc survey of the licence courses).
- 2026-10-25: §10.6 implemented as the program has it: the airborne flag is the float sum of the
  four W+0x08 loads (the previous tick's phase 12) equal to 0, not the contact flags, and the
  lateral step zeroes F+0x474 as well as F+0x4C8 (ours had left g/μ stale in the air).
- 2026-10-25: §4.6 the TCS rule exactly (`0x1E3360`): wheels by the drive-layout byte (rear only
  for layouts 0/5/6, so the R34 reads its rear pair), loaded wheels only (previous tick's Fz),
  s_min from 1 with s*_drive from the wheel's surface record, skipped while T_wot ≤ 0 or the
  throttle is 0; the gain alone enables it (level 0 is not off). The race entry's ASM / TCS
  switches (`0x1DDAD0`) zero the ASCC flags / TCS gain. Implemented (ours had all driven wheels,
  `grounded`, the axle record, a level-0 cut-off and overwrote F+0x474).
- 2026-10-25 (exact tick, phase 12 ground contact): §1.3 the ground contact `0x1E8B68` and the
  query `0x2280C8` read in full: record 0's segment hint filters all four wheels, the strictly
  inside / highest rules, the scalars, slopes and plane, every result field; a miss keeps the
  last plane and renormalises it (so it flips sign every missed tick); the wheel fields,
  F+0x3EC, F+0x535 bit 4 by the conductor's virtual 39, and the course-out hook (virtual 40,
  §11: the pit-lane rule added). `0x1DF150` is the tyre smoke / dust emitter's request, not a
  query; it writes nothing in the car record. Implemented (`sim::exact::CourseGround`,
  `ground_contact`, `tyre_effect_requests`, `surface_bumps`) and verified bit for bit.
- 2026-10-25 (exact tick, phases 5, 8, 11, 13): read in full and ported bit for bit (`sim::exact`,
  `race_tick_exact`). §3.4 the gain's static bytes and formula, the steer cosine and sine phase 5
  recomputes, the dead branches (|x| < ε while braking, state 0); §5.1 the height window is the
  body height P+0x0C, the zone cells' 2 / 1 rule, rejected pairs get factor and code 0, the target
  sums the matrix row; §5.2 the lateral window is the leader's full body width (P+0x08), not half;
  §8.2 the W+0x5C bytes are cleared first; §8.7 the whole flag rule (pad cars get bit 2, control
  source 1 and the race-flow byte, a dead mode-4 branch); §7 #6 takes the distance, not its
  square. Phase 8 is the brake heat [C].
- 2026-10-25 (driver input, exact): §4.4 read from the program in full for the pad path and
  the AI's apply, and ported bit for bit (`sim::exact`, race_tick_exact). Added: the phase's
  dispatch by control source and driver kind, the conductor's pad hook (virtual 20), the HUD
  bytes F+0x522 / F+0x523, the digital throttle state O[2] (a released back axis keeps the
  reverse request while the throttle falls), which wheel types skip the slip-angle extension
  and which raise the lock to 8° / 12°, pedal styles 3 / 4 / 6 / ≥ 7 (the back axis still sets
  the reverse request; style 3's brake-clutch also clears the reverse and shift requests unless
  fully pressed; style 6's shift or back axis drops the clutch request), F+0x49C, the AI
  step's own part (`0x1E35D8`) and its apply (`0x1F79F0` / `0x1E4CF0`). Corrections: the back
  axis replaces the throttle when it is at least as large (not only larger), with its own
  analog flag; on the pad path the pad clutch rule's kind-8 test is never reached (a driver
  kind other than 0 is on the pad path only while the hold counter runs, which gives 0 first).
- 2026-10-25 (exact tick, phase 7 engine / driveline): phase 7's engine, clutch, gearbox and
  driveline calls ported in the program's arithmetic and checked bit for bit in races and
  scripted pad drives (every layout, differential and pedal style). Corrections: §2.7 an engaged
  centre clutch stays locked with Q = X without the capacity test (the test applies when Q ≠ X).
  Additions: §2.2 the torque blend's operation order and the exact power cap; §2.4 the exact
  lock test (start-of-tick and end-of-step speeds), the 3 → 0 test on the start-of-tick wheel
  side, the clutch-pedal lock (request exactly 1, no lockout); §2.6 the controller gearbox byte
  is 1 in the shipped data (rule 4 is its unused mode 2) and the gearbox by pedal style (0
  automatic, 1 manual, 2 kick-down automatic, 3 / 4 / 6 sequential, 5 sequential into reverse).
- 2026-10-25 (phase 12 effects, exact): §10.8 new: phase 12's call order and its effect and bookkeeping routines (pad
  vibration, steering force, engine sound levels, ghost cars, the lagging view angles, heading wrap, wheel spin angles and
  the drawn car's points), read in full and ported bit for bit. §8.3 the kerb formula's speed is the rim speed (m/s, wheel
  +0x20); §8.3b the dirt accumulation (classes 1 and 2 only, × 8 on category 0, at most 0.8).
- 2026-10-25 (exact tick, phase 2): §1.2 phase 2 (`0x1EA428`) read in full and ported in the
  program's arithmetic (`sim::exact::prep`), bit for bit in races: the race hold byte, the
  spawn hold and HUD message countdowns, `dt_car`, aero, T_wot / friction torque, peak
  forces, slip angles. Corrections: on the pit surface (F+0x535 bit 4) the peak force has
  no wear factor at all (§3.1, §8.4); the wear curve's double precision is libgcc's fp-bit
  (`ee::D`, §8.4); the torque lookup `0x1D8E50` bisects (§7 #73).
- 2026-10-25 (exact parameter block): §6 the spec builder `0x1DA308` / `0x2325B8` and the
  converter `0x1DBFD8` read in full and ported in the program's arithmetic (`sim::exact`,
  byte for byte on every car). P is car+0x10 .. car+0xEBF. Corrections and additions: §6.3
  the second steer-curve default tests spec 0xF5 (curve C's last x), not a count; the
  tyre-block defaults copy block 0's zero surface bytes into all six blocks (the rear ones
  too), not blocks 1 and 2; §6.6 the set-car clamps do bind (LSD defaults below their
  minimum, index settings of 0 → 1); §6.4 the mean track and P+0x20 are computed in the
  program's software doubles, the event's power cap (rule block +3 × 10 PS) and the CPU cars'
  per-class axle grip percentages (rule block +4.. / +8..) come in through the converter's
  arguments, the top speed P+0xDF8 also reads the entry's +0x2104 condition block, and the
  race's initialisation recomputes the physics globals (virtual 21 → `0x1DAE70`) before it
  builds the cars (the program image starts with another last rolling-resistance point,
  0.25 for 0.255, and no tarmac scale).
- 2026-10-22: §8.8 the per-wheel "damped vibration state" of phase 13 (`0x1E6A38`) is the flap
  pendulum the car models' mud flaps and aerials morph with [C]; implemented (`sim::exact::flaps`,
  `WheelState::flap`). §4.1: the forward step runs airborne too (`0x1E8598` calls `0x1E7848`
  unconditionally); its a_x (F+0x4C4) feeds the pendulums.
- 2026-10-21: §2.4 / §9.4 kind 8's clutch request [C]: the driver-input phase (`0x1E4D38`)
  runs the AI step `0x1E35D8` for a CPU car, and for a player car with kind != 0 and no
  spawn/stop counter; there kind 8 writes request 0 (F+0x418) and returns before the AI
  request rule (which kinds 1-7 and CPU cars get). The pad rule `0x1E4180` also gives 0 for
  kind 8 (only reached with a counter running). So kind 8 always has request 0 (implemented;
  ours used the AI rule for every non-zero kind, so a locked clutch stayed engaged above
  idle and an open one engaged above 3/4 of the upshift rpm).

- 2026-10-25 (collision step, exact): §5.3 the step in the program's own arithmetic and the
  course side read in full: the wall grid walk (box, cells, group filter by the road segment
  hint and its neighbours), the hint itself (the front-left hub's road segment, `0x2280C8`),
  the pair links (`0x228B68`, segment −1 included), sinf / cosf / sincosf per call site and the
  program's constants. Corrections: the car persistence's velocity rule runs for a vertex
  outside its edge too; wall-corner hits set no AI side; the all-bodies step's sensor check
  sets no body's flag; the effect pass caps the strength at 1. Implemented
  (`collide::exact`, `sim::CourseCollision`) and verified bit for bit in races.
- 2026-10-25 (collision step): §5.3 read from the program in full. The all-bodies step
  answers at most two events per tick for all cars together, walls and car-car hits
  in one earliest-first order; the point flags carry over between steps (only a body
  leaving car contact clears its car flags), so a pressing car-car contact is held
  by the persistence pass; the wall sweep's exact tests, the wall-corner test against
  car edges, the point-wall position correction, the push-out's exact rule (car edges
  crossing a wall, no start-side condition) and the AI flags set by the sweeps.
  Implemented (`collide::step`).

- 2026-10-20 (tyre sounds): §8.3c the second curve set of `0x1DEBB0` (wheel +0x34),
  the tyre loops' pitch: inputs, tables, b always added, kerb ramp; verified 3840/3840
  (`model_skid_pitch`). §8.3 the airborne counter recomputes the words for 5 steps
  (ours keeps them). Implemented (`sim::exact::tyre_effects`, `VehicleTelemetry::skid_pitch`).

- 2026-10-20 (AI rules pass): §9.4 the end of a run hands the player's car to
  the AI (kind 1, slowed after a race / time trial / machine test), to the stop
  driver (kinds 6 / 7) and parks it (kind 8); kind 8 holds the clutch open and
  the stop hold applies (ai.md §1). §4.6 the CPU / scripted aid block's ASCC
  channels act wherever the AI set enables them (course sets do; the built-in
  sets do not). §5.3 the collision step's front / rear impulse flags (body
  +0x434 / +0x438) are the AI's recovery flags (ai.md §7).

- 2026-10-20: §2.2 the turbo sound outputs of the engine-torque routine (spool level,
  blow-off): their throttle is the processed throttle x (after TCS and the shift cut, 0
  through an upshift), so every full-throttle upshift fires the blow-off; the spool level
  is 1 (not kept) when the previous load is at most 0.75 or the torque is not positive.
  Implemented (`CarExtraSounds::step_inputs`, `car_sound::Turbo::step`).

- 2026-10-20: §10.3 where the condition words come from and go: the entry constructor zeroes
  them (now [C], was [I]); five drivers seed them from the car before every run and copy all
  eight back after it, the odometer delta going to the total distance; the odometer word update
  in detail; word 3 is the dirt; the HUD reads the live words. No physics rule changed.

- 2026-10-16 (draft 24d, diff harness, licence-demo residual pass): §4.2 the
  static position x_s splits the **sprung** mass; §4.1 the stop clamp's
  "acceleration terms" are F+0x4D8 (and not F+0x4C8); §2.7 the viscous
  transfer's Δw is in m/s. No rule changed; these pin down wording that
  allowed a wrong reading.

- 2026-10-11 (draft 24c): the diff harness's sqrt.s audit (`race_sqrt_sites`)
  found every remaining site to be a true square root, with no other
  squared-distance misreads. Added: the licence stop check (§11) and the wall
  re-projection pass (§5.3). ai.md §4.2: the corner-speed iteration count is
  stated exactly.

- 2026-10-11 (draft 24b): **correction** (diff harness). The listing prints
  sqrt.s as "c1 0x4", and the car–car gates were misread as squared
  distances. They are true distances: broad phase 100 m, relative-position
  code 50 m, zones and contact intensity 20 m (§5.1; ai.md §14).

- 2026-10-11 (draft 24a): §8.6 clarified: the pit-surface bit filters only
  the proximity classifier, not the collision manager's contact response.

- 2026-10-11 (draft 24): **correction.** The CPU/scripted aid block
  car+0xEF8 is the driver object's +0x30 block, filled from the AI parameter
  set, so CPU cars and the flying-start run-up run **TCS (gain 6.0, a = 1,
  b = 0, scale 1)** with ASCC off. Earlier drafts said "zero block, aids off".
  The countdown and launch behaviour is in ai.md §10.

- 2026-10-11 (draft 23b): §9.4 the flying-start run-up uses the CPU AI path
  (kind 1), with aids off and the pad follower not run, then hands over at
  the first line crossing.

- 2026-10-11 (draft 23a): §4.4 the AI clutch request is recomputed every
  step from the link state (rpm ≥ idle whenever the clutch is not open).

- 2026-10-11 (draft 23):
  - §2.4: the shift clutch (state 3) transmits **0 N·m for every car**
    (spec 0xCD is never written); the shift tick itself is fully locked.
  - §2.6: the up-shift test is n > UP strict, on this tick's truncated rpm;
    the traction veto examines the driven axle only (not "front always"),
    using current values; the lockout counts 12 ticks including the lock
    tick; F+0x43B is the limiter flag.
  - §4.6 ASCC rewritten from diff's execution decode: per-tick, no
    accumulation, exact channel rules, side choice, aid block layout.

- 2026-10-11 (draft 22): new §11. The original has no hull/scenery contact
  and no automatic respawn in any mode. Course-out is only a flag (Time
  Trial and License: all four wheels on grass/gravel/sand, or a hard wall hit) that voids the lap and ends the run. Wrong way is HUD only.

- 2026-10-11 (draft 21c, from the diff launch reconciliation): the
  forward-step effective mass counts driven wheels too (flags are added
  later in the tick); longitudinal load transfer is P+0x38 × F+0x4D8. The
  launch traces of the stock RX-7 (TCS on) and the A-4 RX-7 (aids off) are
  both reproduced exactly by diff.

- 2026-10-11 (draft 21b, from diff/physics tyre-chain tests): slip gain knee
  is exactly 100/3.6 m/s; fade v₀ = byte/3.6; dead-band and tie rules for
  the state bytes. The whole-tick launch test (`phys_launch_sequence`)
  confirms the original RX-7 reaches rear slip −0.43 at 0.5 s; the spec path
  gives −0.42.

- 2026-10-11 (draft 21a): §3.4 slip state-byte table (sg = sign of rim
  minus hub speed; st per branch), and the Ia definitions (r/Ia, Ia/r²).

- 2026-10-11 (draft 21): new §3.8, the exact per-wheel force outputs
  (`0x1E73C0`, `0x1E7450`, `0x1E7C08`, flag byte), each verified 20000/20000.
  - W88 = drive-side and W8C = brake-side longitudinal force (ungained);
    T+4/T+8 are the gained copies for the wheel spin.
  - W80 = lateral grip (curve A) and W84 = along-velocity drag (curve B),
    in the velocity-direction frame; the friction circle limits only these.
  - Flag bits per layout.
  - §3.3 corrected: the longitudinal force on the car is not multiplied by
    the slip gain.

- 2026-10-11 (draft 20): §4.4 rewritten, verified frame-exact against the
  original on 8 disc demos.
  - **Frame format:** the brake comes from F[0xA], not F[0xC].
  - **Steering:** the target is a position. A persistent rate class O[3]
    (set by kind-3/digital steer ≠ 0) selects **30°/s turn-in and 120°/s
    return**, i.e. the slow digital rates. The lock grows to the front
    slip angle (extension) and is clamped to max lock.
  - **Follower:** 100/5/50.
  - **Pedals:** analog x + 0.1(1 − x).
  - **Demos:** frames are consumed from step 0, even during AI-driven
    phases; there is no hand-over marker, and an all-zero frame follows the
    last one.
  - **Settings:** the controller settings are fixed data.
  - **Spec 0xB5** (AWD strength) is never written, so the AWD capacity
    defaults always apply (784 / 1470 N·m).

- 2026-10-11 (draft 19): new §10 from a coverage survey of all 364
  functions reachable from the step (no damage, no wind, no external forces;
  nothing else feeds the dynamics).
  - **Exact rules added**, all verified by execution:
    - §10.1: ground-frame speeds and hub velocities;
    - §10.2: free-rolling (undriven, unbraked) wheels are locked to the
      ground speed;
    - §10.3: engine condition factor (GT-mode running-in and oil
      counters) on the full-throttle torque;
    - §10.4: dirt front-brake cut with steering;
    - §10.5: exact suspension step, including airborne;
    - §10.6: airborne behaviour (yaw frozen, no drag or engine force in
      the air);
    - §10.7: slopes give exactly g·sin(angle) without an explicit term.
  - §7 #64–72.
  - **Corrections:** with the handbrake on only ASCC is off; T_wot "env" is
    the condition factor; the suspension stores the clamped x.

- 2026-10-11 (draft 18b): §5.3 deep-overlap gap closed. There is no missing
  rule: the residual differences are rounding-decided on-line vertices left
  by the static separation (verified 8751/8751 with a float port).

- 2026-10-11 (draft 18a): §5.3 notes the open deep-overlap discrepancy
  (46/8751 steps, diff harness).

- 2026-10-11 (draft 18): fidelity gaps closed, all by execution (counts in
  §7 #52–63).
  - **§4.1 rewritten.** Exact forward step and yaw/lateral sums. The
    low-speed hold is Fp·**sin**(0.9π·v_h)·**cos**(0.36π·w); the sin and cos
    were swapped before. v_h is the hub speed across the wheel. The hold
    acts on lateral force and yaw only. The stop clamp also holds while the
    race is frozen, is skipped when airborne, and keeps the vertical speed.
    F+0x474 = lateral g / μ.
  - **§2.7 rewritten.**
    - Layout/AWD table with exact k and capacity per system.
    - Axle integration per layout, with reflected inertia J_e·|R_g| only
      while locked. The zero-crossing clamp applies only with brake
      torque. 'f' axles are not integrated.
    - Centre-clutch logic for L4–7.
    - Torque vectoring: bang-bang, up to 15° of steer, 10–180 km/h; the
      t type is almost nil.
    - **Viscous gain = accel × 0.98** (not × 3.086).
  - **§2.4 rewritten.** Clutch states and transitions:
    - the locked clutch opens only when the request is 0;
    - a gear change goes to the fixed-torque shift state 3;
    - state 2 also opens under brake below 10 km/h;
    - no rev cap while open; launch rpm rule.
  - **§2.6:** no automatic shifts unless the clutch is locked.
  - **§4.6:** ASCC side rule verified; TCS lateral input = |F+0x474|.
  - **§8.6:** courses with pits, the PitIn HUD event.
  - **New §9:** race starts (rolling start: every car at 80 km/h, the
    player AI-driven for 300 steps; flying starts on line 6; grid
    placement; standing and moving initial states).
  - **§4.3:** the 120-step counter is only for the pit start, not every
    spawn.

- 2026-10-11 (draft 17a): §8.3 the "ramp" is wheel +0x6C; §8.5 the brake-heat
  input; §8.4 HUD level rounding.

- 2026-10-11 (draft 17): new §8, tire wear (one shipped curve from the event
  record; slip × load × compound rate, no temperature), the slip-intensity
  input, wear → grip with a per-compound floor, brake heat (visual only),
  and pit stops (line-4 markers, auto-drive, 5 s stop, tires replaced at 1 s
  remaining, no fuel, no limiter). §7 #44–51. §5.3: the pair sweep uses
  straight chords (diff note). CPU pit strategy is in ai.md.

- 2026-10-11 (draft 16c): §5.3 early-hit threshold of `0x1FB740` is
  t < 0.1; on-line hazard approximation note.

- 2026-10-11 (draft 16b, from the diff harness reproducing the all-bodies
  step `0x1FA8F8` bit-identically on 119k samples): §5.3 car–car collision
  rewritten. At most 2 rounds of static separation (`0x1FCC98`, not the
  response), persistence (`0x1FBCE8`), end pose, pair sweep (straight-line
  vertex paths in A's frame, ties to body 0, flagged vertices skipped),
  advance, response (`0x1FC5C0`: j = −1.5·v_rel·n/K at the crossing vertex,
  no friction, no separating clamp, g/2 touch-up). The end pose is not
  recomputed after the second response. Rounding hazard noted. §7 #39–43.

- 2026-10-11 (draft 16a, from diff-harness attitude tests): §4.2 ride height
  (spec 0x5C) and bump room (0x64/0x65) are single bytes; axle block
  layout. §7 #38 `0x1E90F8`.

- 2026-10-11 (draft 16): merged the attitude/camber/toe work, all verified
  bit-exactly by executing the original functions.
  - §4.3 rewritten: exact pitch moment (A term, angular acceleration) and B
    heave term, clamp order (rate ±π/2, angle ±π/4 zeroing the rate),
    gravity in world Z only.
  - **Spawn hold:** F+0x536 is a 120-step spawn counter, not a "human share".
  - **Bump-stop nudge:** atan2 of the bump-stop penetration differences,
    only when a wheel is at a bump stop; replaces "atan2 of wheel travels".
  - Terrain factor and ground frame.
  - §3.1 camber: α = σ·(roll − ground roll) − setting·0.1°, σ = −1 left,
    +1 right (the sign was reversed before). Axle factor formula.
  - §4.4 toe: atan2((byte − 128)·0.0005, r), + right / − left; Ackermann
    ratio formulas. T_i+0x18 = sin, +0x1C = cos.
  - §6.4 P+0xE04/0xE08 and 0xE14/0xE18 are visual-only camber gain and
    swing-arm length.
  - §4.1 position/heading are advanced by the collision sweep (phase 10),
    answering the open question.
  - §2.4: clutch locks on any crossing; anti-stall uses the **brake** pedal
    F+0x42C (> 0.75), not the throttle (corrects 15e).
  - §4.6 understeer uses sin(δ) (15d).
  - §5.3 address range.
  - AI drivetrain: no AI-specific diff/clutch code (§4.4 note).

- 2026-10-11 (draft 15e, integration, from diff-harness execution of the
  differential step 0x1E6488 and clutch routines 0x1E7500 / 0x1E5B98):
  §2.7 the transfer direction (and, for viscous, the speed difference) comes
  from the *previous step's* wheel speeds, not the post-split speeds; the
  "never carry the speed difference through zero" clamp applies to every
  type except f and r (open n/y/t included); when a transfer is clamped the
  lock flag is set. The decoded rule matches 72,000/72,000 comparisons
  (`phys_diff_step_decoded`). §2.4 the clutch locks whenever engine and
  wheel-side speeds cross, in either direction, regardless of idle; a
  slipping clutch has no idle floor. Instead, while slipping, if F+0x42C >
  0.75 and engine speed drops below about idle + 500 rpm, the clutch is
  opened (anti-stall; exact condition to be confirmed by a test).

- 2026-10-11 (draft 15d, integration, from diff-harness execution of TCS
  0x1E3360, indicators 0x1E3708 and differentials 0x1E6488): §4.6 understeer
  target yaw is v·sin(δ)/L (not cos); understeer is 0 when the yaw rate
  opposes the steering (r·δ < 0; r = 0 still gives 1); there is no low-speed
  cutoff (only v ≤ 0); understeer is gated on |F+0x4C8| / Σ tyre[+0x0] > 0.2
  and adds ½ yaw acceleration (F+0x478) to the yaw rate when they share a
  sign; the oversteer blend uses total speed √(vx² + vy²); when F+0x3E9 is 1
  or 2 or F+0x534 ≠ 0, oversteer = 2·max(0, |s·r| − |lat|) / Σ tyre[0] if
  s·r·lat < 0 (unclamped), else 0. TCS (0x1E3360, a0 = car, a1 = aid block:
  +0x20 gain, +0x24 a, +0x28 b, +0x2C scale) uses driven wheels by layout
  byte car+0x208 (0/5/6 rear, 1 front, 2/3/4/7 all; Fz = 0 skipped);
  s_min = lowest slip (wheel+0x68, start 1.0) and s_opt/g come from that
  wheel; status byte F+0x521 = 0 off / 1 pass / 2 limiting. §2.7
  differentials (0x1E6488): slipping transfer is exactly ±C = max(initial ×
  9.8, ratio × |T|), no sync limit; viscous = gain × min(dw², cap) × sign;
  lock state is a flag with torque hysteresis (stays locked while
  |accL − accR| ≤ 2C, cleared when exceeded, set again when the
  half-difference d = dw/2 + (accL − accR)·(1/I)·dt/2 crosses zero, types
  m/h only); type r sets both wheel speeds to the axle base; type f
  integrates wheels separately, n/y/t/m/h/v integrate a half-difference and
  rebase wheels to base ± d.

- 2026-10-11 (draft 15c, integration, from the ai workstream): 0x1FA018 is
  the AI line evaluator, not the start of the collision manager (§5.3's range
  starts one function too early); AI recovery reads collision-body words
  +0x434/+0x438 (meaning open). CPU cars write the road-wheel angle directly
  and use the automatic gearbox.

- 2026-10-11 (draft 15b): §6.7 dispute resolved by execution. The tire set
  is front only for FF and for 4WD with AWD system 0, rear otherwise
  (impreza_lm_race_00 and zz2_00 both rear). A zero width byte becomes
  10 mm on the rear path only. Exact rounding: mul/add/sub chop, div.s rounds
  to nearest. 40,000 random specs and 522 real calls: 0 differences.

- 2026-10-11 (draft 15a, integration, from diff-harness execution of the
  gear generator 0x1E28A8 on random inputs): **disputed, being resolved** —
  the random-input test suggests AWD cars use the front tyre size like
  front-drive, but the real AWD car impreza_lm_race_00 only matches with the
  rear tyre (the original §6.7 rule); the selection may depend on the AWD
  system code. Width code 0: the test's examples conflict (no sidewall vs
  10 mm); never occurs in shipped data. §4.4
  steering (diff-verified): the speed curve is clamped to the car's max angle
  before multiplying by the stick; the centring term applies whenever the
  wheel moves back towards zero, including when the target is across zero;
  the return-to-centre rate limit is a separate argument from the turn rate.

- 2026-10-11 (draft 15): new §6.7, the computed-gearbox algorithm for the
  59 adjustable cars (design top speed = slider × 10 km/h, step table,
  reverse = (3·g₁ + g₂)/4), verified bit-exactly 522/522; §7 #30. §4.6 aid
  parameter map (ASCC channels and flags, TCS gain/a/b/scale with spec
  sources). §2.7 LSD type and setting bytes per axle; AWD-2 coupling gain.
  §4.2 anti-roll stated as roll-resisting; load transfer is explicit only
  (no tire-force moments).

- 2026-10-11 (draft 14e): body text made consistent with 14c/14d: §2.1/§2.3
  turbo cap (multiply by min(B, cap)/B), §2.2 turbo lag/load per step; §6.4
  brake and aero offsets; §6.5 force-vol bytes → surfaces 0..9 and 12..14
  (10, 11, 15 unset).

- 2026-10-11 (draft 14d, integration, from the diff harness running the
  original car-spec pipeline 0x1D93C0 for all 244 cars): §6.4 brake torques
  are at C+0xE28 (front) / +0xE2C (rear) / +0xE30 (handbrake), where C is the
  converter object (= show+0x3E4; the spec's "P+x" offsets are C+x); aero
  coefficients are C+0xE60 (Cd·A + 0.1·(Cf+Cr)) / +0xE64 (C_front) / +0xE68
  (C_rear) (= car+0xE70..0xE78), with default downforce from settings
  S[0x33]/S[0x34]. §6.5 force-vol (table 26) bytes +0x08..+0x11 map to
  surfaces 0..9 and +0x12..+0x14 to surfaces 12..14; surfaces 10, 11, 15 get
  0. Collision extents are S[0x20] (width) / S[0x22] (height); front/rear
  extents default to length/2 in integer mm. TCS is on iff S[0x117] is not 0
  or 100. The showroom's air-density global 0x352920 holds 0.8 until race
  init writes 1.2. Open: 59 cars compute their gear ratios in the
  CarEquipments default setup (0x231DB0, EQ+0xF0).

- 2026-10-11 (draft 14c, integration, from diff-harness turbo tests on all
  113 turbo cars, 0 mismatches): §2.1 the turbo torque table *multiplies*
  each point by min(B, 1 + cap)/B (B = uncapped boost at the axis rpm); it
  does not divide by B (identical for stock cars, whose points never exceed
  the cap). §2.2/§2.3 turbo per step (D = car+0xF84): target = previous load
  (D+0x440, = x·B at the lagged rpm) × n / Bcap(n, n); each active stage's
  lag (s16 D+0x444 stage 0, D+0x446 stage 1) += trunc((target − lag)·rate·dt),
  floored at 0; load = x·Bcap(lag0, lag1) stored to D+0x440; effective
  throttle x' = min(1, load / Bcap(n, n)); torque then as for NA. Stage 1 is
  active only if spec 0xAB, 0xAD, 0xAF are all non-zero; the turbo model is
  off unless 0xA7, 0xAA, 0xAC, 0xAE are all non-zero. Intake smoothing
  (D+0x4EC) and D+0x4E2/0x4E6..0x4E8 are sound/blow-off state only.
  Field map: spec 0xA7 = turbo part +0x11 (cap = 0.1·byte); stage 0:
  0xAC = +0x12 peak, 0xAA = +0x13 knee (×100 rpm), 0xAE = +0x14 rate;
  stage 1: 0xAD = +0x15, 0xAB = +0x16, 0xAF = +0x17; 0xA8/0xA9 (shape
  types) are never written (0).

- 2026-10-11 (draft 14b): new §6.6 default user settings (spring, ride
  height, motion ratio, dampers, anti-roll, LSD, aids, gears, downforce) as
  record bytes, verified against the R34 capture. §2.6: rolling backwards
  selects reverse only with the throttle at 0; with the throttle held below
  1 km/h the box goes to first.

- 2026-10-11 (draft 14): new §6.5, the complete tire byte map (database
  compound/force-vol → spec tire block → runtime record: curves A, B, C, D,
  L, per-surface mu, optimum slips, in-place zeroing quirks). §1.3 angle
  names corrected: a = **pitch** (F+0x468), b = **roll** (F+0x46C). §4.1
  stop clamp clarified: it holds only when a brake torque is present.

- 2026-10-11 (draft 13f, integration, from diff-harness execution results):
  §2.2 friction slope corrected: k_f divides by the last rpm-axis point in
  rad/s, not the last torque value (verified on all 131 NA cars), and there
  is no idle governor. §3.3 / §7 #25 (tyre force 0x1E7C08, verified to
  ~0.03 N): the slip-angle slot is S+0x2F0+0x24·i+0x20 with S = car+0xF84
  (not P+0x2F0); the record index is the byte at P[0xDD0 + 16·(i>>1) +
  surface]; outputs W.80/W.84 = u1·F_peak / u2·F_peak are *unrotated* (the
  consumer 0x1E7848 rotates them into the body frame); W.80 carries the
  fold sign from β = δ − φ (opposite to a wheel-frame slip angle); curve C
  is not read here (only by 0x1E7450); the friction-circle limit is compared
  with √(u1² + u2²) after both are multiplied by k (W.7C).

- 2026-10-11 (draft 13e): §5.3 commit step corrected: the heading is not
  wrapped (it is only reduced beyond ±15π), and the yaw-rate clamp is
  ±20π. §7 #26 a0 is the body record. #26 and #28 verified by execution.

- 2026-10-11 (draft 13d): §7 #15, #17, #21, #23 and #24 verified by
  execution, with exact semantics added. The driveline class (#21) is
  FF → 1, AWD → 1/3/2 by system, others → 0; this corrects the
  init-notes reading.

- 2026-10-11 (draft 13c): §2.6 automatic/manual shift logic now fully
  specified (thresholds, adjustments, traction veto, lockouts).

- 2026-10-11 (draft 13b): §5.3 car–car response (restitution 0.5, no
  friction, split push-out; only in modes with opponents); §6.4 converter
  quirks and more resolved DB bytes (tire compound, force vol, drivetrain);
  §7 candidates 26–29 (collision).

- 2026-10-11 (draft 13): open items merged.
  - **§5.3 wall collision found:** a 2-D collision manager runs in phase 10.
    The car is an octagon. Walls are one-sided closed GTRW polylines. Up to
    2 swept iterations per step, impulse response with restitution 0.05
    and friction μ = 0.5, a penetration push-out and a "stuck" steering
    assist. Car–car impulses run only in modes with opponents.
  - **§4.2 suspension:** full unit conversions (spring 0.1 kgf/mm →
    N/m, sqrt-law damper fitted through two points, knee, bump stop, droop,
    preload = unsprung weight, load-transfer terms).
  - **§4.5 brakes:** brake torque = pressure × brake part value × 98 N·m.
    The front/rear split is fixed by the part; the "balance" setting is the
    ABS gain. The handbrake acts on the rear.
  - **§4.7 aero:** ρ = 1.2; the area comes from the body record. The two
    terms are DOWNFORCE (front/rear), applied through the suspension, not
    lift.
  - **§3.1:** load sensitivity is evaluated at Fz / section width.
  - **§3.2/§3.3:** the wheel velocity angle and the fade weight have a
    single writer, `0x1EA3D8`; no tire force below 1 km/h.
  - **§4.4:** steering-curve selector.

- 2026-10-11 (draft 12b): §3.4 step 5 byte assignment fixed (0x2D2935 for
  state > 0, 0x2D2934 for state < 0; both are 20, so behaviour is
  unchanged). §7 #14 slip ratio verified by execution.

- 2026-10-11 (draft 12): corrections from execution in the diff harness:
  §3.4 slip ratio restated (signed denominators, ε dead bands, fade on the
  branch's reference speed with bytes at 0x2D2934/35); §3.6 bump wave is
  sin(f·x) + cos(f·z) with a signed square w·|w| in mode 1; §7 #20 args are
  (a1 = min, a2 = max, a3 = steps, t0 = setting). §7 #10–13, #16 and #19 are
  verified.

- 2026-10-11 (draft 11): §5.3 records the negative wall-collision finding
  and an interim recommendation. Open items: wall response, wheel +0x78/+0x7C
  producers, aero lift sign, spring/damper unit conversions, brake torque
  formula (still being worked on at the time).

- 2026-10-11 (draft 10b): §1.2 phase roles all filled; the time-scale
  rubber band is confirmed inactive in time trials.

- 2026-10-11 (draft 10): §7 extended to 25 candidates (clutch, rpm, TCS,
  slip gain/ratio, steer angle, slip fold, damper, bump wave, angle wrap,
  settings interp, engine torque, torque table build, gear-for-speed, tire
  force).

- 2026-10-11 (draft 9): §2.7 driveline: axle-speed model, 4WD splits,
  open/locked/clutch-pack/viscous differentials (confirmed structure).

- 2026-10-11 (draft 8): §2.2–§2.7 engine runtime torque (throttle blend,
  linear engine braking, limiter with 500 rpm hysteresis), turbo, clutch
  states and engine-speed integration, gearbox, shift logic, drive layout.
  The LSD torque split is still open.

- 2026-10-11 (draft 7): §4.4 input and steering, §4.5 brakes/ABS, §4.6
  TCS/ASCC, §4.7 aero coefficients; §3.4 slip ratio rewritten (sign: +
  braking, − wheelspin; fade below 20 km/h).

- 2026-10-11 (draft 6): §4.1 planar integration (forward speed, yaw,
  low-speed hold, stop clamp), §4.2 suspension (spring, sqrt-law damper,
  quadratic anti-roll bar), §4.3 heave/pitch/roll with clamps. Steering,
  brakes and aids are still pending.

- 2026-10-11 (draft 5): §6 spec layout (complete 0x3E2-byte table) and
  converter results: chassis geometry, mass (+120 kg), yaw/pitch/roll
  inertia parameters, steering curves, camber curve, brakes, aids.

- 2026-10-11 (draft 4c): diff-harness corrections: aero coefficients are at
  car+0xE70/E74/E78; `0x1EBF00` takes the leader in a1; §7 entries 1, 2, 4,
  5, 7, 8 and the §5.2 formulas verified by execution.

- 2026-10-11 (draft 4b): §1.3 physics ↔ course frame mapping resolved:
  GTRW (x, y, z) = (phys x, phys z, −phys y), a proper rotation; car forward
  at zero heading = GTRW −Z.

- 2026-10-11 (draft 4): §3 tires written: peak force, slip angle, force
  curves and friction circle, slip ratio, rolling resistance, per-surface
  tables, wear. Sections 2.2, 4, 5.3 and 6 are still pending.

- 2026-10-11 (draft 3c): §1.3 ground-query result layout (from track);
  physics ↔ GTRW axis mapping marked open.

- 2026-10-11 (draft 3b): §5.2 slipstream constants corrected (A = 0.5,
  B = 1.0, K_up = 2/s, K_down = 0.5/s from the static parameter block): full
  slipstream halves drag. §1.3: Z-up physics frame and the Euler → matrix
  convention (verified by the diff harness).

- 2026-10-11 (draft 3): §2.1 torque curve (confirmed); §5 car–car
  proximity and slipstream/aero drag (confirmed). Note: car–car contact
  produces no forces in the classifier itself.

- 2026-10-11 (draft 2): §1 rewritten and confirmed: dt = 1/60 s fixed,
  one step per frame, explicit Euler, full phase order with roles, state
  representation (planar + heave/pitch/roll, Euler angles → matrix).

- 2026-10-11 (draft 1b): **correction**. The constants in draft 1 were
  read from a mis-based program image (shifted by 0x114 bytes). Re-verified:
  the unit constants (1/3.6, 2π/60, 716.2, 9.8, 0.0254, π/180) are still
  present, but in different functions. The claim "dt = 1/60 is used
  throughout" was **wrong**: in the vehicle code 1/60 is referenced only by
  `0x1E0768` and `0x1ECFC8`. The tick rate is still believed to be 60 Hz,
  but §1.1 is downgraded to [I] until the timestep is located. The engine
  torque routine `0x1E5B98` does **not** use 716.2 (that is in
  `0x1E10E8`/`0x1E14E8`). Also, the libm labels sinf/cosf were swapped in
  the RE labels (now fixed). Nothing in this file depended on them yet.
- 2026-10-11 (draft 1): skeleton; simulation loop structure, phase order,
  unit conventions, car spec pipeline. Engine/tire sections are placeholders
  that will be filled within the next hours.

---

## 0. Architecture overview

- The simulation is owned by a `DynamicsConductor` object (one per race;
  subclasses per race mode: TimeTrial, FreeRun, License, SinglePlayer,
  Battle2P/MP, MachineTest, FreePractice). [C, RTTI + vtable `0x33CA20`]
- Each car is a fixed-size vehicle record (0x16DC bytes) stored in an array
  inside the conductor. [C, `0x1F2B68`]
- At race start, for each car [C, `DynamicsConductor::initialize()` =
  vtable slot 23, `0x1F3628`]:
  1. a packed integer **car spec** (0x3E2 bytes) is built from the car's
     installed parts and settings (`0x1DA308` → `0x2325B8`, documented in
     `docs/formats/database.md`), with defaults filled in for missing
     values;
  2. the vehicle record is initialised from the spec and a start pose
     (position x, y, z and heading) (`0x1DDAD0`), which converts the
     integer spec into floating-point physics parameters (`0x1DBFD8`).
- All physics math is single-precision FPU code; VU0 (COP2) is not used in
  the vehicle simulation. [C: no COP2 instructions in `0x1D8000`–`0x1F0000`]
- Physics constants show the internal unit system [C, constant pool at
  `0x350000`–`0x350700`]: speeds are converted with 1/3.6 (km/h → m/s),
  engine speed with 2π/60 (rpm → rad/s), torque/power with 716.2
  (PS = kgf·m × rpm / 716.2), forces with g = 9.8 (kgf → N), tire sizes with
  0.0254 (inch → m), angles with π/180. So the integer database values (kgf·m,
  kg, mm, rpm, inches, degrees, km/h) are converted to SI floats at init and
  the simulation runs in SI (m, s, kg, N, rad). [C for constants; I for "runs
  in SI throughout"]

## 1. Simulation loop

### 1.1 Rate and timestep

- The game loop runs one update per video frame: a draw thread calls the
  current game object's update, then draw, then waits for the next vertical
  blank. The race update (`RaceBase` virtual 2 → 29 → 30) calls the vehicle
  step `0x1F39D8` **at most once per update**. [C, `0x1012E0`, `0x17C570`,
  `0x17C7A0`]
- **dt = 1/60 s, fixed.** [C] Two independent confirmations:
  - each step advances the race clock by 50 counts, and the lap-time code
    uses 1 count = 1/3000 s, so one step is 1/60 s [C, conductor vf14
    `0x1EFCD8`, lap timing `0x1EE978`];
  - every vehicle integrator multiplies by a per-car step length
    `dt_car = time_scale / 60` (`0x1ECFC8`, recomputed at the start of every
    step), where `time_scale` defaults to 1.0. [C]
- No sub-stepping: the step function runs each phase once. There is no
  catch-up if a frame runs long; the game simply slows down. No PAL/50 Hz
  variant was found. [C for the step structure; I for "no PAL handling"]
- Integration is **explicit (forward) Euler**: `rate += accel·dt`, then
  `value += rate·dt` (semi-implicit order where both appear). [C, seen in
  the yaw, pitch/roll, longitudinal-speed and drafting-filter integrators]
- Slow motion/replay: if the conductor's time-scale gate is enabled, each
  car has a phase accumulator. Each call it is decreased by the conductor
  time scale (1.0 normally); when it goes negative, 1.0 is added back and
  the step runs for **all** cars. With scale 1.0 the step runs every frame.
  [C, `0x1F39D8`] For our implementation: ignore it and run one tick per
  1/60 s.
- Per-car time scale (`dt_car` multiplier): rewritten only by the
  mode-specific pairwise pass (conductor virtual 4), which is a rubber-band
  handicap for AI races (single player with opponents, battle modes). In a
  time trial that pass never runs and the scale stays 1.0. [C]

Recommendation: `kDefaultTickHz = 60` exactly; `dt = 1/60`.

### 1.2 Order of operations per tick

The step runs each phase for **all cars** before moving to the next phase
(phase-major). [C, `0x1F39D8`] Order (normal race mode):

| # | Function | Role |
|---|---|---|
| 1 | `0x1EADB8` | copy current state into "previous step" slots (used for finite-difference velocities) [C] |
| 2 | `0x1EA428` | prep: race hold byte F+0x4DC := (race info +0x5C > 0) (`0x1EAAE8`); spawn/start hold F+0x536 −1 if non-zero; HUD message timer F+0x51D −1 unless 0 or 0xFF (`0x1DDFF0`); recompute `dt_car` and F+0x4A0 = 60 / time scale (`0x1ECFC8`); slipstream filter and aerodynamic drag (`0x1DB118`, §5.2); T_wot F+0x4B0 and friction torque F+0x4B4 at ω (`0x1E0BC0`, `0x1E0C28`, §10.3); per-wheel wear grip and peak force (`0x1EA1E8`, §3.1, §8.4); per-wheel slip angle with low-speed fade (`0x1EA3D8`, §3) [C; X bit for bit, `race_tick_exact`] |
| 3 | `0x1E4D38` | driver input: pad/replay frame → throttle, brake, steer (§4.4) [C] |
| 4 | `0x1E8978` | per-wheel steer angle and its sin/cos (§4.4) [C] |
| 5 | `0x1E7328` | per-wheel slip ratio (§3.4) [C] |
| 6 | `0x1E3B18` | understeer/oversteer indicators, TCS, ASCC, ABS (§4.5, §4.6) [C; bit-exact port `sim::exact::aids`] |
| 7 | `0x1E8598` | **tire forces, engine, clutch, gearbox, driveline, longitudinal and yaw integration** [C: see §2, §3] |
| 8 | `0x1E6928` | brake heat per wheel (§8.5) [C] |
| 9 | `0x1DFD90` | clears one word (car+0x16C8, the collision step's hit record, §5.3); no physics [C] |
| 10 | conductor virtual 12 | **wall collision** (and car–car in some modes), §5.3 [C] |
| 11 | `0x1EC098` | car–car proximity classification and slipstream factor (no forces) [C, §5] |
| 12 | `0x1EB218` | suspension, ground contact, body heave/pitch/roll, wheel spin, effects [C, §4] |
| 13 | `0x1E8190`, `0x1E8340`, `0x1E6A38` | tire wear; wear/failure flags; per-wheel flap pendulums (§8.8) [C] |
| 14 | conductor virtual 13 | mode hook |
| 15 | `0x1E8910` wheel steer angle | a0 = P (car+0x10), a1 = wheel i, f12 = steer δ (a2 unused) | P+0x110 + 4i (ratios), P+0xE20/+0xE24 (toe front/rear) | f0 = front: δ·ratio[i] if δ ≥ 0 else δ·ratio[1−i]; then −toe for left (even i), +toe for right; rear: ±toe only | leaf | steering. **Verified** |
| 16 | conductor virtuals 1, 14, 3 | race clock +1/60 s, ranking [C] |
| 17 | `0x1E8DD0` damper curve | a0 = axle suspension block (P+0x2B8 + 0x44·axle), f12 = v | bump T/O/G at +0x18/+0x1C/+0x20, rebound at +0x24/+0x28/+0x2C | f0: v clamped to [−1, 1]; v < 0: O_b + G_b·√|−v − T_b|; else −(O_r + G_r·√|v − T_r|) | leaf (sqrt.s) | suspension. **Verified** |
| 18 | `0x1E0618` | publish vibration/pad feedback [C] |
| 19 | `0x1F2B18`, `0x1F59B0` | event countdown; overtake/following state machine (HUD/AI only) [C] |

A second entry mode (free-run/replay conductors, `0x1F1400`) steps only cars
1..n−1 and skips the hooks and the car–car pass. [C]

The race frame around the step [C, bit for bit `race_exact_lockstep`]:
- Before the step `RaceBase::vf30` runs the race information's frame `0x17A5E8`: a start request
  (race info +0x50, set by `0x179FE8`) is cleared and sets the countdown from the start type +0x54:
  −1 none (post 0x14, 0x14; seconds 0), 0 the seconds already in +0x58 (post 2 when not 0), 1 four
  seconds (posts 2 and (0x14, 0x1B)), 2 ten (posts 2 and (0x14, 0x12)), 3 the rolling start (0);
  the countdown +0x5C = 60 × seconds (and the countdown display `0x1A96B8`). Then, every frame, a
  non-zero +0x5C counts down by 1 and posts event 4 when it reaches 0; once it is 0 the
  information's timer (+0x60 on, +0x68 the steps) counts down and posts event 7 at its end. So
  the step sees the count already lowered; the first step of a countdown of n seconds sees
  60 n − 1. Posts go to the race's event queue (race info +0x84, `0x1796C0`: {type, car, arg}).
- The race-flow byte (conductor + 0x8000 + 0x37AC) is the initialisation's argument and nothing
  changes it during a race; the time scale +0x37B0 is 1 from virtual 21 (a debug replay speed
  table at `0x136838` and the setter `0x1ED950` write it).
- After the rubber band, for each car from the first: `0x1E0618` sends the pad bytes
  car+0x16D4 .. +0x16D6 to the car's input device (`0x2107D8`) unless control source 1 / 2 or a
  driver kind is set (an output only; the pad output below); `0x1F2B18`: the end countdown +0x37AE, when not 0, counts down and
  posts event 1 (the race's end) at 0; `0x1F59B0` (conductor + 0xBA34): per pair of cars a
  following counter (to 300) and a state byte from the slipstream factor `0x1EBEB8` and the
  position code `0x1EBB80`, read only by the race's sounds (`0x20DAC0`); not physics. The step
  ends here also when the race-flow gate stopped it.
- **The pad output** [C] (`0x1E0618` → `0x2107D8` → the port, NTSC-U 1.10): `0x1E0618` skips the car when
  F+0x3E9 is 1 or 2 or F+0x534 ≠ 0, else calls `0x2107D8(car+0xC = the RaceInput, &car+0x16D4)`. That returns
  when RaceInput+0x13C is 0 or +0x130 ≠ 0; with +0x138 ≠ 0 it sets actuator 0 = |byte 0| (car+0x16D4) and actuator
  1 = |byte 1| (car+0x16D5, the vibration level of `0x1DF2B0`), each capped at 255, through the port's virtual
  slot 4 (`PSPort` `0x22FE60`: port+0x38 + i); then, when the device's type byte (+0x14) >> 5 ≠ 0 (a wheel),
  actuator 2 = byte 2 (car+0x16D6, the steering force). car+0x16D4 is only ever cleared (race init `0x1DD5F0`,
  PAL's AI cars), so the small motor never runs in a race. Every frame the main loop's input update
  (`0x1012E0` → `0x22E8F0` → `0x22FB78` → `0x22FBB0`) copies port+0x38..+0x3A into the pad block
  (`0x4A09CC` + port × 0x5D, +0x40..+0x42; `0x22FA58`) and then clears them (slot 5, `0x22FE70`): a frame
  whose step sends nothing (a car the AI drives, no step) stops the motors. The pad thread (`0x22F328`)
  aligns actuator i to byte i (`0x272848`, align[i] = i below the pad's actuator count, else 0xFF) and, for pad
  IDs 0x41 / 0x53 / 0x73 / 0x79, sends act[0] = +0x40 and act[1] = +0x41 to a two-actuator pad
  (DualShock: small motor, large motor), so the large motor gets the level 0..127 as it is; a one-actuator
  pad gets act[0] = +0x40 when that is non-zero, else 1 when the level is ≥ 0x61 (97), else 0. Implemented
  in `race::RumbleModel` (`VehicleTelemetry::pad_output`, `pad_rumble`); the triggers there are ours.
- Virtual 22 at the initialisation clears the proximity matrices, builds the gap matrices, the
  collision bodies (after the copy-in) and calls `0x17B030` on race info +0x40: the result −2,
  its time 0x157529FF (none), +0x48 and the record +0x4C 0.

Phase 12 (`0x1EB218`) is only calls, in this order (car, F = car + 0xF84
where noted): `0x1E9D48` hub x/y offsets, `0x1E9E18` (F) speed ramp,
`0x1E8B68` ground queries, `0x1E91A8` terrain factor and x_i, `0x1E9348`
suspension and body forces, `0x1E96F0` pitch/roll/heave, `0x1E9998`
bump-stop nudge, `0x1DF2B0`, `0x1E0068`, `0x1DEBB0`, `0x1DF148`, `0x1DFC60`,
`0x1E06D8`, `0x1E0768` (F), `0x1E9DE8` hub offsets, `0x1D8A80` body axes
(F+0x468/+0x46C/+0x470), `0x1EA4D8` ground frame, `0x1EA9B8` speeds,
`0x1EAFC0`, `0x1DF150`. [C]

### 1.3 Car state representation

- Position: three floats, world metres, in a **Z-up** physics frame (x, y
  horizontal, z height). Mapping onto the course (GTRW, Y-up) frame [C,
  `0x1E8B68` builds the wheel query points; track workstream confirmed the
  query reads +0/+8 as GTRW x/z and +4 as height]:

  ```
  GTRW.x = phys.x      GTRW.y (height) = phys.z      GTRW.z = −phys.y
  ```

  This is a proper rotation (no mirror): a −90° rotation about x. So the
  car's forward axis at zero heading, phys (0, 1, 0), is GTRW (0, 0, −1),
  which matches the car-model convention (nose towards −Z) in
  `sim_types.h`. Heading rotates about GTRW +Y. Our sim can stay entirely in
  the GTRW/sim_types frame; convert only when comparing raw values with the
  original (e.g. in the diff harness).
- Wheel query points (one per wheel, at car+0x14BC + 0x34·i) are the hub
  positions: body position (F+0x00) + per-wheel hub offset (wheel
  +0x3C/+0x40/+0x44, the body-frame offsets rotated by the current
  attitude; added as they are), written in the course frame (x, height, −y).
  All four are queried together by `0x18FB58` (the course's road block,
  `0x2280C8`). Each wheel that found the road gets the height (+0x10) in
  wheel +0x00 and +0x04, the surface index (§3.6) in +0x24, the record's
  +0x32 (always 0) in +0x25, the triangle's flags in +0x26 and the road bump
  (§3.6) in +0x60; a miss (+0x30 ≠ 0) zeroes the road bump and leaves the
  rest of the wheel. Then F+0x3EC = wheel 0's segment (+0x0C, the collision
  step's hint, §5.3); F+0x535 bit 4 (on the pit-lane surface, §8.6) = some
  wheel's triangle has flags bit 0 and the conductor's virtual 39 answers 1
  (the base conductor, so single races; time trial, licence and free run
  answer 0); and the conductor's virtual 40 is called with the number of
  wheels on flags-bit-0 triangles and on soft materials (2, 3, 4, 6;
  `0x2D53D8`): the course-out rule of §11, empty in single races. [C,
  `0x1E8B68`; bit for bit in `sim::exact::ground_contact`]
- Ground query result (per wheel, 0x34 bytes at car+0x14BC+0x34·i): +0x00 x,
  +0x04 height (the query point's in, the ground height out), +0x08 z, +0x0C
  road segment (−1 none), +0x10 ground height (−FLT_MAX on a miss), +0x14 /
  +0x18 the vertex scalars interpolated, +0x1C..+0x24 unit normal, +0x28
  plane d (n·p + d = 0), +0x2C / +0x2E the first vertex's raw scalars, +0x30
  = 1 on a miss (no road), +0x31 material, +0x32 = 0, +0x33 flags. The
  surface index (wheel +0x24) is derived from material and flags (§3.6).
- The ground query `0x2280C8` [C; bit for bit in `sim::exact::CourseGround`]:
  - the segment hint is record 0's +0x0C from the last query (front left);
    every record's +0x10 starts at −FLT_MAX and +0x0C at −1;
  - a record's grid cell (GTRW +0x48: the last breakpoint ≤ x and ≤ z; no
    road off the breakpoints) lists groups (segment, triangles); a group
    counts when its segment is the hint, one of the hint segment's
    neighbours (segment record +0x16) or there is no hint;
  - per triangle (a, b, c): with b' = b − a, c' = c − a, p' = p − a (x, z),
    inv = 1 / (b'x·c'z − c'x·b'z) (computed for every triangle),
    u = (p'x·c'z − c'x·p'z)·inv, v = (b'x·p'z − p'x·b'z)·inv: inside only if
    u > 0, v > 0 and u + v < 1 (strictly), and taken only if its height
    h = (b'y·u + c'y·v) + a.y is above the record's best so far;
  - the taken triangle writes +0x10 = h, +0x0C its segment, +0x14 / +0x18 the
    vertex u16 scalars / 65535 interpolated the same way, +0x1C / +0x24 the
    height slopes gx = (c'z·b'y − b'z·c'y)·inv and gz = (b'x·c'y − c'x·b'y)·inv,
    +0x28 = −((gx·a.x + gz·a.z) − a.y), +0x2C / +0x2E vertex a's raw scalars,
    +0x31 / +0x33 the triangle's material and flags bytes;
  - then every record: +0x04 = +0x10, +0x30 = (+0x0C < 0), +0x32 = 0, and the
    plane is normalised: r = −1 / √((n_x² + 1) + n_z²) (rsqrt.s), +0x1C,
    +0x24, +0x28 times r, +0x20 = −r: the unit normal (−gx, 1, −gz) / length.
    On a miss the other fields keep the last tick's values and the stale
    plane is normalised again, so each missed tick flips the signs of its x,
    z and d and shrinks them.
  - The program tests each cell's triangles against all records in that
    cell together; each record's result depends only on its own point.
- Tyre effects (`0x1DF150`, the end of phase 12) [C; the requests bit for
  bit]: per wheel a request {wheel, surface index, hub x, wheel +0x00, −hub y,
  heading + steer angle, ground +0x14, the ground plane raised 0.05 m (d −
  0.05·n_y), wheel +0x2C, +0x68, +0x4C, +0x20} to the course's effects object
  (course +4: the race's particle system, RacePS2Base + 0xBC30, its parameters
  the course's Env0 block, set in `RacePS2Base::vf9`; `0x18FDE0` → `0x1A46C8`)
  [C, bit for bit `race_exact_lockstep`]: the request's surface gives an effect
  kind (`0x3306D0`, a word per surface index: 0, 0, 1, 2, 3, 4, 5, 0 × 5, 5 × 4 on the
  US disc) with parameters at Env0 +0x4D0 + 0x44·kind. No particle when the
  kind's +0x4DC is below 0.01 or the request's emission (+0x2C) is at most
  0.001; otherwise one particle (a 0x40-byte record from a pool of 256: the
  free list, or the used particle with the smallest +0xC × +0x10, `0x1A4A40`)
  with six draws of the VU0 random register R (`0x245D5C`), in this order:
  the size class c = int(8 R) (at most 7; + 0x20 for an odd wheel) at +2, the
  emission e = request × the class's scale (`0x330730`, 8 floats) at +0xC; the
  kind at +3; the colour (`0x25A090`: Env0 +0x4D0 / +0x4D4 / +0x4D8 × (a (1 −
  g) + g) × 128, alpha 0, each + 0.5 into 0..255; a the request's ground scalar
  +0x18, g Env0 +0x910) at +4; the size kind+0x4E0 × e at +8; second draw: the
  growth kind+0x4E8 × ((R − 0.2) × 0.3 + 1) at +0x18; third: the angle 360 R
  at +0x14, +0x10 = e + (1 − e) × kind+0x500; fourth: the life int(min(255, R
  (count + 0.2) + 1) × 256) at +0x1C (count = kind+0x510, unsigned); fifth:
  int(64 R) at +0x1F; sixth: int(((R − 0.5) × 0.2 + 1) × 128) at +0x1E; the hub
  point, the plane and its height at the hub, −(n_x x + n_z z + d) / n_y, at
  +0x20 .. +0x3C. A per-kind counter (+8 + 4·kind, wrapped past kind+0x510)
  is kept and read by nothing here. The particles' ageing and drawing are the
  race's (`RacePS2Base` virtuals 29 / 45 / 48). It writes nothing in the car
  record; phase 13's flaps then read R. The kinds' parameters are not the
  course file's: every race's initialisation (virtual 21, `0x1DE070`)
  overwrites them in the loaded Env0 block for 11 kinds from the program's
  table `0x2D3510` (8 bytes per kind b0..b7): +0x4DC set to 1 when below 0.01,
  +0x4E0 = b0 × 0.01, +0x4E4 = b1 × 0.1, +0x4E8 = b2 × 0.01, +0x4EC = b3,
  +0x4F0 = b4 × 0.01, +0x4FC = b5 × 0.01, +0x500 = b6 × 0.01, +0x510 = b7.
- Orientation: three Euler angles, **pitch a** (F+0x468, rotation about the
  car's lateral x axis), **roll b** (F+0x46C, about the forward y axis) and
  heading c (F+0x470, about z); F = car+0xF84. They are turned into a
  rotation matrix every step by `0x1D8A80`. Verified by execution in the
  diff harness: the
  matrix is R = Rz(c)·Ry(b)·Rx(a). With s/c the sine/cosine of each angle, it
  writes three vectors (columns of R) [C, test `phys_euler_axes`]:
  - **forward** axis (F+0x0C) = (cc·sb·sa − sc·ca, sc·sb·sa + cc·ca, cb·sa); at
    zero angles this is (0, 1, 0);
  - **lateral** axis (F+0x18) = (cc·cb, sc·cb, −sb); at zero angles (1, 0, 0);
  - **up** axis (F+0x24) = (cc·sb·ca + sc·sa, sc·sb·ca − cc·sa, cb·ca); at zero
    angles (0, 0, 1).
  The forward/lateral roles are inferred from the slipstream code, which
  measures "behind" along F+0x0C [I, strong]. Heading c rotates about world
  Z. There is no quaternion.
- The dynamics are **not** a general 6-DOF rigid body. The state is split
  into [C structure, I interpretation]:
  - planar motion: forward speed and lateral speed in the car frame, heading
    and yaw rate, integrated from tire forces (§3.5);
  - vertical/attitude motion: body height (heave), pitch and roll with their
    rates, integrated from the four suspension forces (§4.1). The pitch/roll
    angles are clamped to small values.
  This "bicycle-plus-attitude" structure is the main thing our
  implementation should copy if handling is to feel like GT3.

## 2. Engine and drivetrain

### 2.1 Torque curve [C, built at init by `0x1E10E8`]

Inputs from the car spec (filled from the engine record, table 6):

| Spec | Source | Meaning |
|---|---|---|
| `+0x82 + 2i` u16 ×16 | engine `+0x18` | full-throttle torque points, 0.01 kgf·m (read as unsigned) |
| `+0x72 + i` u8 ×16 | engine `+0x47` (+ engine-balance / NA-tune shift) | rpm axis, ×100 rpm |
| `+0x71` u8 | engine `+0x57` | number of valid points (≤ 16) |
| `+0xA2` u16 | engine `+0x42` × 10, or a CarEquipments override | overall output factor, 1000 = ×1.0 |
| `+0xA4` u16 | 1000, × upgrade parts | high-rpm factor, per mille |
| `+0xA6` u8 | 100, × upgrade parts | low-rpm factor, percent |
| `+0x6E` u8 | engine `+0x44` | idle, ×10 rpm |
| `+0x6F` u8 | engine `+0x45` (+ parts) | rev limit, ×100 rpm |
| `+0x70` u8 | engine `+0x46` (+ parts) | second rpm limit, ×100 rpm (see 2.3) |

Conversion (per point i, n = valid count, w_i = axis_i × 100 × 2π/60 rad/s):

```
f_out = spec[+0xA2] × 1e-5            (0.01 if the field is 0; so 1000 → 0.01, which
                                        together with the 0.01 kgf·m unit gives ×1.0)
s_lo  = spec[+0xA6] × 0.01             (1.0 if 0)
s_hi  = spec[+0xA4] × 0.001            (1.0 if 0)
t     = (w_i − w_0) / (w_{n−1} − w_0)
T_i   = c_i × 9.8 × f_out × (s_lo + (s_hi − s_lo) × t)      [N·m]
```

So the two upgrade factors **tilt** the curve: the percent factor applies at
the lowest rpm point, the per-mille factor at the highest, and they blend
linearly along the rpm axis. For turbo cars each T_i is then **multiplied
by min(B, B_cap)/B** (B = uncapped boost multiplier at that axis rpm), which
is 1 for every stock car. So the stored table is the full-boost torque, and
the run-time load x' (§2.2) scales it. [C, verified on all 113 turbo cars]

Note: f_out with the R34's `+0x42` = 118 gives ×1.18. That is how GT3 hides
the real output of "280 PS" cars. [C formula; I interpretation]

Lookup at run time: piecewise-linear interpolation in engine speed (rad/s),
**clamped** to the first/last torque value outside the axis (no
extrapolation). [C, generic table helper `0x1D8FB8`, structure
`{s16 n; float* x; float* y}`]

Derived values [C, `0x1E10E8`]:
- Rev limit (rpm) = spec `+0x6F` × 100 (if 0: last axis point × 100), but at
  least spec `+0x70` × 100 + 200.
- Idle (rpm) = spec `+0x6E` × 10 (e.g. R34 1000, Vitz 700).
- An engine-friction slope = (drivetrain record u16 `+0x16`, spec `+0x6C`)
  × 0.1 × 9.8 / T_{n−1}. Meaning [I]: engine braking scaled to the torque at
  the top of the curve (see 2.3).

### 2.2 Engine torque at run time [C, `0x1E17F0`, `0x1E0BC0`, `0x1E0C28`, `0x1E1768`]

State: engine speed ω (rad/s); rpm n = max(ω·60/2π, idle − 1) (integer, for
the HUD and thresholds); throttle x (0..1, after TCS); clutch-link state
(§2.4).

- Full-throttle torque T_wot(ω) = curve(ω) × f, where f is the engine
  condition factor (§10.3): 0.97–1.05 from the GT-mode wear/oil counters,
  exactly 1.0 when they are zero. It does not scale the friction torque.
  [C, verified]
- Friction/engine-braking torque F(ω) = k_f·ω, with
  k_f = (drivetrain record u16 +0x16) × 0.98 / ω_last, where ω_last is the
  last rpm-axis point in rad/s (not the last torque value). So friction at
  the end of the curve is (u16 +0x16) × 0.98 N·m, and engine braking grows
  linearly with engine speed. [C, diff harness `phys_engine_torque_spec`]
- There is no idle governor: below idle the result is the same formula.
  [C, diff harness]
- Torque: **T(ω, x') = x'·T_wot(ω) − (1 − x')·F(ω)**: closed throttle gives
  −F, full throttle T_wot. [C] The program evaluates it as x'·(T_wot + F) − F
  (T_wot = F+0x4B0, F = F+0x4B4, both from phase 2), and as 0 − F under the
  fuel cut [C, `0x1E1768`, exact port].
- Rev limiter: a flag is set when n ≥ rev limit and cleared when n <
  rev limit − 500 rpm. While it is set, T = −F (fuel cut). [C]
- Load x' = x for NA engines. With a turbo (verified by execution on all
  113 turbo cars):
  - target = previous load × n / B_cap(n, n);
  - each active stage's lag rpm (s16) += trunc((target − lag)·R_i·dt),
    floored at 0;
  - load = x·B_cap(lag₀, lag₁), stored for the next step;
  - x' = min(1, load / B_cap(n, n)).
  So at steady full throttle x' → 1, and on throttle tip-in x' lags while
  the stages spool. (The other intake-smoothing states are sound only.) [C]
- Optional power cap (power-restricted events only): torque soft-limited so
  that T·ω stays below the event's PS limit × 735 W. [C] Exactly (`0x1E0C38`,
  E+0x9B on, P = E+0x9C the limit in W, s = E+0xA0 the band): with p = T·ω,
  T is kept while p ≤ P(1 − s); else L = P(1 − s/2), less (P − p)²/(2sP) while
  p < P, and T := min(T, L/ω). So the power follows a quadratic knee from
  P(1 − s) to P(1 − s/2), which it never exceeds.
- Turbo sound outputs, in the same routine (`0x1E17F0`), turbo cars only;
  x is the routine's throttle argument, the **processed throttle** (after
  TCS and the shift cut / blip of §2.4: 0 through an upshift), L the load of
  the previous step (car+0xF84+0x43C, the stored load before this step's
  update), T this step's engine torque (`0x1E1768`):
  - blow-off (byte +0x4E7 armed when 0, f32 +0x4E8 strength, cleared every
    step): armed and x < 0.75 → strength = max(L − 0.8, 0) × 2⁻⁸ (L read
    from +0x440 before it is updated), disarm; x > 0.9 (f32 0x35035C) →
    re-arm. Since x is 0 for the shift's steps, every upshift taken at full
    throttle fires once. The sound frame plays `b_off` at strength × 2.5.
  - spool level (byte +0x4E6): L − 0.75 ≤ 0 or T ≤ 0 → 1; otherwise
    clamp(int(128·((L − 0.75)·min(T/588, 1))²), 1, 127) (f32 0x350360 =
    1/588 applied below 588 N m). The sound frame scales it by 0.2.
  - spool pitch word (+0x4E2): the larger of the two stages' lag words.
  [C; ours: `CarExtraSounds::step_inputs` reads the processed throttle, the
  load and lags from the telemetry; `car_sound::Turbo`]

### 2.3 Turbo boost [C, `0x1E0F38`, `0x1E0CD8`/`0x1E0E08`/`0x1E0ED8`]

- Turbo model active only when spec 0xA7, 0xAA, 0xAC and 0xAE are all
  non-zero; stage 1 only when 0xAB, 0xAD and 0xAF are all non-zero. Max multiplier cap = 1 + spec 0xA7 × 0.1.
- Two stages i = 0, 1: peak A_i = spec 0xAC/0xAD × 0.1; knee
  N_i = spec 0xAA/0xAB × 100 rpm; response rate R_i = spec 0xAE/0xAF ×
  0.1 /s; shape type spec 0xA8/0xA9.
- Stage shape at lagged rpm n: below N, type 0: A(1 − ((N − n)/N)²);
  type 1: A(n/N)²; type 2: A·n/N. From N to 3N: A − 0.25·A·((n − N)/N)².
  Above 3N: 0.
- B_cap(n₀, n₁) = min(1 + shape₀(n₀) + shape₁(n₁), cap). The lag update is
  given in §2.2.
- The database torque curve is the full-boost curve; at init it is only
  capped (§2.1). [C]
- The sound side reads the stored load and the lag words (spool and
  blow-off, §2.2). [C]

### 2.4 Clutch and engine-speed integration [C; `0x1E6D48` 10000/10000, `0x1E7500` 20000/20000, `0x1E77E0` 600/600, `0x1E5B98` 30000/30000, all verified by execution]

Engine inertia J_e = (drivetrain u16 +0x20, scaled by flywheel and clutch
parts) × 0.001 kg·m² (e.g. 201 → 0.201). Idle ω_idle = idle rpm·2π/60.
rpm (s16) = max(int(ω·60/2π), idle − 1).

**Clutch link state S** (F+0x437):

| S | Meaning | Transmitted torque T_c | Engine speed |
|---|---|---|---|
| 0 | open | 0 | ω = max(ω + T·dt/J_e, ω_idle); **no rev cap** (the limiter acts only through the torque) |
| 1 | locked | T (all engine torque) | ω = driven-axle speed × R_g (L3 uses the mean, L7 ω_r + k(ω_f − ω_r)), floored at 0, **no idle floor**, capped at rev limit + 10 rpm |
| 2 | slipping (launch) | 0.05·T_wot(ω) [+ s(Δω)·T if T > 0], sign of (engine − wheel side) | ω += (T − T_c)/J_e·dt |
| 3 | shift clutch | ± spec 0xCD × 9.8 N·m (fixed), which is **0 for every car** (spec 0xCD is never written) | as state 2 (engine free-wheels under the shift cut) |

with s(d) = min(1 + max(d − 52.36, 0)/837.76, 1.125).

**Transitions:**
- **0 → 2** (start, `0x1E6D48`), only while the clutch request (F+0x418) is
  non-zero:
  - if moving (v > 10 km/h in a forward gear, or v < −10 km/h in reverse):
    engage when the processed throttle > 0.25;
  - otherwise (including standstill): engage when rpm > launch rpm =
    max(engine byte +0x43 × 10, idle + 500). If not, the request is cleared
    for this tick.
  So at a standstill the engine idles with the clutch open, revs freely on
  throttle, and engages (slipping) above the launch rpm.
- **2 or 3 → 1** (lock): whenever engine side and wheel side cross this
  tick, in either direction. Then ω_e := wheel side, and a 12-tick shift
  lockout starts. Exactly (`0x1E7500`): with e₀, w₀ the engine and
  wheel-side speeds stored before the step (F+0x38C / F+0x390, by
  `0x1E6D48`), e the engine speed after the slip integration and w the wheel
  side after the axle step: lock if (e₀ ≤ w₀ and w ≤ e) or (w₀ ≤ e₀ and
  e ≤ w) (so equal speeds lock at once).
- **1 → 0** (release): **only when the clutch request is exactly 0.** There
  is no other path; a gear change never opens a locked clutch. For the
  player (automatic-style pedals) the request is set by the driver layer
  every tick:
  - 0 when the race is frozen, during the spawn/stop counter, or in driver
    kind 8 (for CPU cars too: the AI step writes 0 for kind 8 and skips the AI rule,
    `0x1E35D8` [C]);
  - 0 for a rear-driven car with the handbrake axis > 0.8;
  - with the clutch engaged (S ≠ 0): 1 unless rpm < idle (then 0);
  - with S = 0: 1 if the brake pedal < 0.25, else 0.
  So in practice the locked clutch opens only when the engine drops below
  idle, or with the handbrake on a rear-driven car. Braking alone does not
  open it.
- **2 → 0**: brake pedal (F+0x42C) > 0.75 **and either** ω < (idle rpm +
  500)·2π/60 (anti-stall) **or** |v| < 10 km/h. Both comparisons are
  strict, and only if no lock happened this tick.
- **3 → 0**: wheel side ≤ ω_idle (the wheel side w₀ stored before the step).
- **Gear change** (`0x1E77E0`): with S ≠ 0, a change to or from reverse
  gives S = 2, and a forward → forward change gives **S = 3** (the fixed-torque
  shift clutch). It lasts until the speeds cross (then locked, plus the
  12-tick lockout) or the wheel side falls to idle. There is no timer.
  **Because spec 0xCD is never written (only 0xCC, which nothing reads),
  the shift clutch transmits 0 N·m for every car** (verified by execution
  on 20000 states and against a Beetle launch trace). During a shift the
  engine free-wheels under the throttle cut. The car keeps accelerating
  briefly only because the still-spinning driven wheels return their slip
  through the tire forces while the axle speed relaxes. The gear change
  happens at the end of the tick, so the shift tick itself still transmits
  full locked torque.
  While in S = 3 the processed throttle is forced to 0 if the wheel side is
  slower than the engine, else 1 (a shift cut or blip).
- Pedal styles 3 and 6 (clutch axis, human only) can force an immediate
  lock: both axle speeds := ω_e/R_g. [C, `0x1E7500`] Only in S = 2 or 3 with
  the clutch request exactly 1.0 (style 3: request = 1 − the brake axis, so
  the pedal fully released); the wheels then all turn at r·ω_axle, and no
  lockout starts. Styles 3 and 6 also skip the S = 3 throttle cut / blip.

Driveline torque at the gearbox output: T_g = T_c × R_g; the axle
integration is in §2.7.

### 2.5 Gearbox [C, `0x1E1C98`, `0x1E2700`, `0x1E2670`]

- Gear index 0 = reverse, 1..N forward (N = spec 0xC8). Neutral is not a
  gear (it is clutch state 0).
- ratio_total[k] = gear_k × final, gear_k = spec 0xB6 + 2k (s16 ×0.001),
  final = spec 0xC6 × 0.001; reverse is negated.
- Tire radius per axle: r = 0.5 × rim (in) × 0.0254 + width × aspect (width
  = spec byte × 5 mm, aspect = byte × 5 %).
- Engine rad/s per m/s in gear k = ratio_total[k] / r_driven.

### 2.6 Shift logic [C, `0x1E4FF8`, `0x1E4E80`]

Let UP = engine byte +0x46 × 100 rpm (500 rpm below the rev limit for most
cars), n = engine rpm, v = forward speed, and g = current gear (0 =
reverse, 1..N). Downshift rpm table for gear k+1 → k:
max(UP × ratio[k+1]/ratio[k] − (rpm per 10 km/h in gear k+1), launch rpm
+ 500).

Each step (returns a new gear or "no change"):
1. Lockout: while the shift lockout counter is non-zero, decrement it and
   do nothing. (A held shift button keeps it at 1.) The anti-hunt counter
   (240 steps) also counts down.
2. Reverse request → gear 0. Then, with v = forward speed and x = throttle
   after TCS [C, `0x1E4FF8`]:
   - in reverse (g = 0) and v > 1 km/h → gear 1;
   - x > 0 and v < 1 km/h → gear 1 (if not already). **This includes
     rolling backwards with the throttle held**, e.g. after bouncing off a
     wall: the car stays in (or returns to) first;
   - x = 0 and v < −1 km/h in a forward gear → gear 0 (reverse). The auto
     box only selects reverse when coasting backwards off the throttle.
3. Over-rev: if 1 ≤ g < N and v exceeds the speed for (rev limit + 500)
   rpm in gear g, shift up.
4. Manual gearbox: shift input +1/−1 changes the gear within 1..N and sets
   a 1-step lockout and the 240-step anti-hunt counter. [C, `0x1E4FF8`] This
   is the controller settings' gearbox byte (0x2D4628 + 0x14) = 2, which the
   shipped data never has: it is 1, and then a held shift input only holds
   the automatic gear for that tick (rules 1-3 still apply). The manual boxes
   are selected by the pedal style instead (below).
5. Automatic, **only while the clutch is locked (S = 1) and not in
   reverse.** While the clutch is open, slipping (launch) or in the shift
   state, the gear does not change except by rules 2 and 3 above (verified
   3687/3687). A launch therefore happens in first gear, and there is no
   upshift during the launch slip. An over-rev upshift during slip does take
   effect and puts the clutch into S = 3.
   - gear 1: up threshold UP, no down-shift;
   - gear g ≥ 2: up threshold UP (none in top gear); down threshold =
     table[g], then +100·B₁ rpm if throttle < 0.05, +100·B₂ rpm if brake
     > 0.8, −100·B₃ rpm if throttle > 0.25 (settings bytes; B₃ = 5 and
     B₁ = B₂ = 0 in the shipped defaults);
   - n < down → g − 1; **n > up (strict)** → g + 1 unless vetoed. n is the
     s16 rpm, truncated from this tick's final engine speed. The decision
     is made at the end of the same tick's driveline step. [C, verified
     3000/3000]
   - **Traction veto** (current tick's values, strict): no up-shift while
     an examined wheel has load exactly 0, or slip ratio < (tire record
     drive optimum, +0x140, negative) × slip gain. The examined wheels are
     the **driven axle**: layouts 1, 2 → front; 0, 4, 5, 6 → rear; 3, 7 →
     all four. Positive slip never vetoes.
   - Lockout: the byte F+0x43A is set to 12 when the clutch locks and
     decremented at the start of each decision; while it is non-zero no shift
     happens. So the 12 ticks including the lock tick are blocked. The
     240-step anti-hunt counter does not block automatic up-shifts. (F+0x43B,
     the next byte, is the rev-limiter flag.)
   - An alternative mode picks the gear directly from speed: the lowest
     gear whose rpm is below UP − 500, plus a kick-down level.
6. Initial gear at the start = the lowest gear whose rpm at the current
   speed is below UP − 500.

**Gearbox by pedal style** [C, `0x1E5690`; every path checked bit for bit
in `race_tick_exact`]. During the spawn / stop counter (F+0x536) the gear
goes to 1. Otherwise the pedal style F+0x461 of a pad-driven car (control
source F+0x3E9 ≠ 1 and F+0x534 = 0; every other car uses 0) picks:
- 0: the automatic box above (`0x1E4FF8`);
- 2: the same rules 1-3, then, with the clutch locked and not in reverse, the
  gear from speed (`0x1E4E80`) plus the kick-down level, at most N. The level
  (F+0x464, `0x1E4F10`) follows trunc(3·(1 − throttle pedal)) (0 with the
  brake pedal at exactly 1, no target with the throttle pedal at 0): one step
  down after 13 ticks of a lower target, one up after 61 of a higher one;
- 1 (`0x1E5458`): reverse request → gear 0; in reverse with the throttle
  pedal down → the gear from speed (at least 1); else the 1-tick lockout,
  then shift input +1 / −1 within 1..N;
- 3, 4, 6 (`0x1E5540`): the lockout (held by a shift input or the reverse
  request), then the reverse request → gear 0, or shift input +1 / −1 within
  1..N (no rule 2 or 3);
- 5 (`0x1E5600`): as 3 without the reverse request, the down-shift going on
  from 1 into reverse (0).
Any gear change with the clutch engaged sets S = 2 (to or from reverse) or 3
(§2.4); with the clutch open the gear just changes.

### 2.7 Driveline: axle speeds, 4WD coupling, differentials [C; `0x1E5B98` verified on 30000/30000 states, all layouts and clutch states; `0x1E5768`, `0x1E57F8`, `0x1E2700` 10000–20000 each]

The driveline is modelled with **axle speeds** (front and rear, rad/s) plus
a per-axle left/right half-difference.

**Inputs.**
- Wheel net torque (`0x1E5768`): T_i = −r·(longitudinal tire force) −
  sgn(w_i)·(brake torque) (no brake term when w_i = 0 exactly), plus
  torque vectoring (below).
- Axle inertias: J_front = 0.01·h(0xCE)·fd + 2·0.01·h(0xD2) and
  J_rear = 0.01·h(0xD0)·fd + 2·0.01·h(0xD4), where h = s16 spec words
  (drivetrain +0x1C/+0x1E and +0x18/+0x1A) and fd = final drive, applied
  once, not squared.
- Reflected engine inertia **J_ref = J_e·|R_g|** (not R_g²), only while the
  clutch is locked (state 1).
- Axle input torque T_g = T_c·R_g (§2.4).

**Layout codes** (`0x1E2700`), from drive type, AWD system B3 = spec 0xB3,
share B4 = spec 0xB4 and strength B5 = spec 0xB5:

| Drive / AWD system | Layout L | k (Drv+4) | Capacity (Drv+8) | Driven axle |
|---|---|---|---|---|
| FR, MR, RR | 0 | – | – | rear |
| FF | 1 | – | – | front |
| 4WD, system 0 | 2 | B4·0.01 (N·m per rad/s) | 0 | front |
| 4WD, system 1 | 3 | B4·0.01 | 0 | rear |
| 4WD, system 2 | 4 | 1.0 if B4 ≥ 50, else B4·0.01 | (B5 or 8)·98 N·m | rear |
| 4WD, system 3 | 5 | B4·0.01·0.36 | (B5 or 15)·98 | rear |
| 4WD, system 4 | 6 | 1/(B4·π/180) per rad of steering (0 if B4 = 0) | (B5 or 15)·98 | rear |
| 4WD, systems 5, 6 | 0 (plain rear drive) | – | 0 | rear |
| 4WD, system 7 | 7 | B4·0.01 | 0 | rear |

**Axle integration** (`0x1E5B98`, tail). Ff, Fr = sums of the wheel net
torques per axle.
- **Centre clutch locked** (L4–6 with the engaged flag set, see below):
  one common speed ω += (T_g + Ff + Fr)/(J_f + J_r + J_ref)·dt. Drive
  torques are T_g/2 each.
- **L3 (50 : 50 coupling):**
  - mean m = (ω_f + ω_r)/2 integrates as above;
  - half-difference d' = d + (Ff − Fr − k·(ω_f − ω_r))·dt/(J_f + J_r) (1/(J_f +
    J_r) is the driveline constant Drv+0xB0), and
    d' := 0 if it changes sign;
  - ω_f = m + d', ω_r = m − d'.
- **Otherwise**, drive torques and inertias per layout:
  - L0: rear T_g, J_r + J_ref.
  - L1: front T_g, J_f + J_ref.
  - L2: rear k(ω_f − ω_r), front the rest; J_f + J_ref, J_r.
  - L4–7: front Q_f, rear T_g − Q_f; J_f, J_r + J_ref.
  Then for each axle whose diff type is **not 'f'** and J ≠ 0:
  ω += (drive + Σ wheel net torques)/J·dt.
- **Zero-crossing clamp only when the axle (or, for the common cases, the
  car) has brake torque:** ω := 0 if it changes sign or was 0. Without
  brake torque the axle speed may cross zero.
- An axle with an 'f' diff is not integrated; it keeps a stale base speed,
  and its wheels integrate individually (below). [? matters only for cars
  with an 'f' diff on a coupled axle]
- **Centre-clutch crossing** (L2, or L4–7 with Q_f ≠ 0): if the axle speed
  difference changes sign this step, the non-driven axle snaps to the
  driven axle's speed. For L4–6 the engaged flag is set.

**AWD front torque** for L4–7 (X = "full transfer" sentinel):
- L4: X if k = 1; 0 if handbrake > 0.2; X if T_g ≤ 0 or (reverse and
  clutch locked); else k·T_g.
- L5: 0.5·g·T_g, with g from `0x1E5998`: u = |ω_r − ω_f|·k·f, where f =
  1 for |F+0x474| ≤ 0.25, f = 0.25 for ≥ 1, linear between. It returns X
  (full) when the clutch is open, in reverse while locked, or (with f = 1)
  when slipping, braking > 0.75, or T_g and T_g + Fr have the same sign.
  Otherwise it returns u if u < 1, else X.
- L6: 0 if handbrake > 0.2; X in reverse while locked; else g = max(0,
  1 − |steer|·k), and Q = X if g ≥ 0.99, else 0.5·g·T_g.
- L7: k·T_g.
- **Centre clutch:**
  - if engaged: with Q = X it stays locked (no capacity test); otherwise it
    stays locked while |(Ff − Fr) − T_g| ≤ capacity, else it disengages
    [C, `0x1E5B98`, exact port; corrects earlier drafts, which put the
    capacity test on Q = X];
  - when not locked and Q = X, Q_f = +capacity if ω_r ≥ ω_f, else
    −capacity;
  - when Q ≠ X, Q_f = Q.

**Wheel speeds** (`0x1E6488`, the diff-harness tests
`phys_diff_step_decoded` 72,000/72,000):
- type 'f': each wheel integrates alone, w_i += T_i·(r/Ia)·dt, with the
  zero-crossing clamp when that wheel brakes;
- type 'r', and m/h while locked: both wheels = r·ω_axle;
- other types: half-difference d' += (T_L − T_R)·0.5·(r/Ia)·dt plus the
  transfers below, then w = r·ω_axle ± d'.

The transfer direction (and, for viscous, the speed difference) uses the
**previous step's** wheel speeds. The "do not carry the difference through
zero" clamp applies to all types except f and r; when a transfer is clamped,
the lock flag is set.
- **clutch-pack LSD** (m, h): C = max(initial × 9.8, ratio·|T_axle|), with
  ratio = accel × 0.01 when T_axle ≥ 0, else decel × 0.01. A slipping diff
  transfers exactly ±C. The lock flag holds while |T_L − T_R| ≤ 2C, is
  cleared when exceeded, and is set again when the half-difference crosses
  zero.
- **viscous** (v): transfer = gain·min(Δw², cap)·sign, with **cap =
  initial² and gain = accel setting × 0.98** (spec 0xDA+a × 0.1 × 9.8).
  Δw is the difference of the peripheral speeds W+0x20 in **m/s** (not
  rad/s): the whole step works on m/s with r/Ia. [diff harness,
  `phys_demo_b3`: in rad/s the transfer is 1/r², about 11×, too strong]
  Defaults 50/50 give gain 49 (draft ≤ 17 said × 3.086, which was wrong).
- **torque vectoring** (y, t; `0x1E57F8`), per axle, only for 2.78 ≤ v ≤
  50 m/s:
  - T = min(|steer|/15°, 1) × initial, where initial = spec 0xD8+a × 9.8 N·m
    for **y**, but only × 0.01 for **t** (≤ 2.55 N·m, practically nothing);
  - m = mean wheel speed; θ = 1.125·m if steer > 0, 0.875·m if steer < 0;
  - if w_odd < θ: odd wheel +0.4375·T, even wheel −0.5625·T; if w_odd > θ:
    the reverse; equal: nothing.
  This is a bang-bang regulator that holds the right − left speed
  difference at ±m/4.

Settings bytes per axle (axle 0 = front): type letter spec 0xD6/0xD7 (LSD
record +0x11/+0x1B, index in "frnmhvyt"); initial / accel / decel = spec
0xD8/0xDA/0xDC front and 0xD9/0xDB/0xDD rear (from LSD record default
bytes +0x12/+0x15/+0x18 and +0x1C/+0x1F/+0x22). R34: front 0/0/0, rear
7/30/15.

## 3. Tires

Notation: Fz = wheel normal load (N, from the suspension, §4); s = surface
index of the wheel (§3.6); each axle has 3 **tire records** (one per
compound slot). The record used is `record[axle][slot(s)]` (§3.6).

### 3.1 Peak force scale [C, `0x1EA1E8`]

Per wheel, each step:

```
F_peak = Fz × L(Fz / width[axle]) × wear_grip × axle_factor × mu[s] × Cam(a)
```

- L: load-sensitivity curve (piecewise-linear table in the record, x in
  N per metre of tire section width, 196 N per count), evaluated at
  Fz / section width (m). At init each record's base grip is also
  multiplied by L at the static load ((half the axle's sprung weight +
  unsprung weight) / width). [C, `0x1EA1C0`, loose notes]
- wear_grip: piecewise-linear in accumulated tire wear, falling to a floor
  (§8.4). The factor is skipped (not multiplied at all) when wear is disabled
  for the race and also while the car is on the pit surface (F+0x535 bit 4),
  where `0x1E9E80` returns 0 before touching the wheels. [C, X bit for bit]
- axle_factor: per-axle float, nominally 1.0. [I]
- mu[s]: the record's own per-surface friction value (16 floats in the
  record). So grip depends on the surface twice: through the compound slot
  and through the record's per-surface mu. [C]
- axle_factor f_k = (spec 0x36/0x37 + 900) × 0.001 (chassis bytes; ≈ 1.0).
- Cam(α): the per-axle camber → grip table (4 points, §6.4), at
  **α_i = σ_i·(body roll − ground roll) + camber offset_k**, with σ = −1 for
  left (even) wheels and +1 for right (odd) wheels. camber offset_k =
  −(camber setting × 0.1°) (P+0xDFC/+0xE00). This is the **only** place the
  camber setting reaches the tire forces. Product order: Fz·L, × wear,
  × f_k, then × mu × Cam. [C, verified 8000/8000 wheels by execution,
  `0x1EA1E8`]

### 3.2 Slip angle [C, `0x1EA3D8`/`0x1EA348`, `0x1E7C08`, `0x1E7A68`]

- Contact-patch velocity (vx, vy) in the wheel frame gives a raw angle
  atan2(−vy, vx) and a **low-speed fade**: 0 below 0.2778 m/s (1 km/h),
  1 above 2.778 m/s (10 km/h), linear in between. (Diff-harness verified.)
- The velocity-direction angle φ = atan2(−vy, vx) (0 when the fade is 0)
  and the fade weight are stored per wheel each step by `0x1EA3D8` (vx, vy
  = hub velocity in the body frame). [C]
- In the force routine, β = wrap(wheel steer angle (+π when rolling
  backwards) − φ), wrapped to (−π, π].
- Fold: v = |β| mapped into [0, π/2] (β < −π/2 → β + π; −π/2..0 → −β;
  0..π/2 → β; > π/2 → π − β), with a sign sg = +1/−1 that flips beyond 90°.

### 3.3 Force vs slip and combined slip [C structure, `0x1E7C08`, `0x1E73C0`]

- Two curves of slip angle from the record (A and B, byte-quantised,
  outputs up to 1.275): u1 = sg × A(v) × k, u2 = B(v) × k, where k is the
  low-speed fade weight of §3.2 (0 below 1 km/h, so there is no tire force
  at all then; 1 above 10 km/h). [C, written by `0x1EA3D8` only]
- Combined slip: a limit Lim = D(slip ratio) is taken from a third curve
  of slip ratio. If |(u1, u2)| > Lim, both are scaled by Lim / |(u1, u2)|
  (a **friction circle** in normalised units, not an ellipse).
- Output force components: F1 = u1 × F_peak (lateral-like; it carries the
  sign of the slip) and F2 = u2 × F_peak. Both are rotated into the body
  frame by the wheel's velocity-direction angle. [C rotation; I which is
  lateral]
- Longitudinal (drive/brake) force: Fx = C(slip ratio) × F_peak, signed by
  the slip direction. The force on the car is **not** gained; only the
  wheel's reaction copy is multiplied by the slip gain (§3.8).

### 3.4 Slip ratio [C, `0x1E7328` → `0x1E7068`; verified by execution in the diff harness]

Inputs per wheel: x = hub speed along the wheel heading (m/s), ω = wheel
peripheral speed (m/s), u = hub forward speed (wheel +0x4C). ε = 1/57.6 m/s.
Phase 5 (`0x1E7328`) computes x = cos δ·W+0x4C − sin δ·W+0x50 with the
program's cosf and sinf of the wheel's steer angle δ (W+0x0C), not the
values phase 4 cached in T_i.
Gain g(k) (`0x1E6FE0`; k = 1 for the "driving" branch, 0 for "braking",
and 0 in the dead band) from the static block 0x2D28B0: 1 if byte
+0x86+k is 0; else with base b = byte +0x88+k × 0.01 and knee
K = byte +0x8A+k × (1/3.6), g = b + (1 − b)·|u|/K for |u| < K, else 1.
Shipped bytes (1, 50, 100 for both k): g = 0.5 + 0.5·min(|u|·3.6/100, 1),
the knee exactly 100 km/h. The gain used is stored in W+0x48.

1. |ω| < ε (wheel stopped): state = −1; s_raw = 1 if x ≠ ω, else 0.
2. ω > ε:
   - x ≤ ω (wheel faster than ground, driving): s_raw = (x − ω)·g(1)/ω,
     state = +1;
   - otherwise (braking): s_raw = (x − ω)·g(0)/x; state = −1.
3. ω < −ε (reversing), mirrored:
   - x ≥ ω: s_raw = (x − ω)·g(1)/ω, state = +1;
   - otherwise: s_raw = (x − ω)·g(0)/x, state = −1.
   Both braking branches also test −ε < x < ε (then s_raw = 0), which can
   never hold there (|x| > |ω| ≥ ε).
   The denominators are **signed** (ω or x, not their magnitudes), so the
   sign of s flips in reverse.
4. Clamp s_raw to [−1, 1].
5. Low-speed fade: the reference speed is r = x if state < 0, else ω; the
   threshold is v₀ = byte / 3.6 (byte at 0x2D2935 for state > 0 and
   0x2D2934 for state < 0; both 20 in the shipped data, so v₀ = 5.56 m/s =
   20 km/h). If |r| < v₀, s = s_raw·|r|/v₀; otherwise s = s_raw.
6. If state is 0, s = 0 (dead: every path sets ±1).

Only the −1 clamp can act (s_raw > 1 is impossible): a driven wheel spinning
forwards while the hub still rolls backwards (or the mirror case).

Forward driving: s < 0 = wheelspin, s > 0 = braking slip.

**State bytes** for the force code (§3.8), with x = hub speed along the
heading and w = rim speed [C, `0x1E7068`]:

| Case | st (side) | sg (force sign) |
|---|---|---|
| \|w\| < ε (strict; stopped) | −1 | sign(w − x) (0 if equal) |
| w > ε, x ≤ w (wheelspin forwards) | +1 | +1 |
| w > ε, x > w (braking forwards) | −1 | −1 |
| w < −ε, x ≥ w (wheelspin in reverse) | +1 | −1 |
| w < −ε, x < w (braking in reverse) | −1 | +1 |

Outside the dead band the branch follows the sign of w (w = +ε exactly
takes the forward branch). At an exact tie x = w with |w| ≥ ε, sg =
sign(w) (in the dead band a tie gives 0). Otherwise sg is the sign of (rim
speed − hub speed) in every case. [C; diff-verified, `model_tyre_chain`] The
longitudinal force pushes the car towards the rim's motion.

**Wheel and axle inertia** [C, converter verified on 244 cars]:
- Ia = spec s16 0xD2/0xD4 × 0.01 kg·m² per wheel (front/rear).
- Wheel speed (m/s) integrates with r/Ia (Drv+0xA0).
- The forward step's extra mass per free wheel is Ia/r² (P+0x2B0/+0x2B4).
- Axle inertia J = driveline word × 0.01 × final drive + 2·Ia (§2.7).

### 3.5 Rolling resistance [C, `0x1E84D8`]

F_rr = −sign(v) × RR(|v|) × Σ_wheels Fz_i × rr[s_i], with v = forward speed.
RR is a speed curve (8 points, below) and rr[s] a per-surface coefficient.
It is applied in the forward-speed integration. If the speed would change
sign within a step, it is set to 0 instead (static hold).

RR curve (speed m/s → factor): 0 → 0, 1.11 → 0, 5.56 → 0.010,
16.1 → 0.015, 32.2 → 0.032, 55.6 → 0.072, 85.6 → 0.144, 111.1 → 0.255.

### 3.6 Per-surface tables [C]

Surface index s (0..15) per wheel, from the ground query's material byte m
and flags byte f: if f & 2 then m = 0 → 12, 7 → 12, 1 → 13, 9 → 14 (wet or
alternate variants); otherwise s = m. [C, `0x1E8A48`]

Default surface → compound slot map (copied into each car spec at
spec+0x132, `0x33C368`): s 0..15 → 0,0,1,1,1,1,2,0,0,0,0,0,2,2,2,(2).

Static per-surface table (block `0x2D28B0`, built by `0x1DAE70`):

| s | bump amplitude (m) | rolling-resistance coeff | bump wavelength (m) | amplitude scaled by speed |
|---|---|---|---|---|
| 0 | 0 | 0.10 | 0.5 | no |
| 1 | 0.010 | 0.10 | 0.5 | no |
| 2 | 0.010 | 0.20 | 2.5 | yes |
| 3 | 0.024 | 25.5 | 5.0 | yes |
| 4 | 0.020 | 0.30 | 5.0 | yes |
| 5 | 0.010 | 0.20 | 0.5 | yes |
| 6 | 0 | 0.10 | 1.0 | yes |
| 7–15 | 0 | 0.10 | 1.0 | no |

- Road bump (added to the suspension input): h = amp[s] × w' × (min(1,
  0.02 × |v_hub|) if scaled by speed else 1), with
  w = **sin(f·x) + cos(f·z)**, f = 2π / wavelength. In wave mode 1 (used by
  all surfaces) w' = w·|w| (a signed square); other modes use w' = w.
  [C, `0x1E8B68`, `0x1E8AE0`; verified by execution]
- Surface 3's coefficient of 25.5 makes it extremely draggy (gravel trap or
  sand?) [I]. Surface meanings (which index is grass, sand and so on) are
  not decoded yet [?].

### 3.7 Tire wear

Wear grip, accumulation and pit stops: §8.

### 3.8 Per-wheel force outputs, exact [C; each verified 20000/20000 by executing the original, with random and real R34 tire tables]

Notation: wheel W, per-wheel driveline record T (car+0x1274 + 0x24·i),
Fp = peak force (§3.1), g = slip gain (§3.4), st and sg = the two signed
state bytes written by the slip-ratio routine (st = +1 in the wheelspin
branch, −1 for braking or a near-stopped wheel; sg = the slip sign).

**Curve lookups** (`0x1E73C0`), x = slip ratio:
- C(x) → T+0x14, the longitudinal force fraction;
- D(x) → W+0x64, the friction-circle radius as a fraction of Fp.
Both curves share one x axis (ascending, −1…+1). The lookup:
- x ≤ x₀ gives y₀ and x ≥ x_{n−1} gives y_{n−1};
- otherwise k is the first index in 1..n−2 with x < x_k (n−1 if none), and
  y = y_{k−1} + ((x − x_{k−1})·(y_k − y_{k−1}))/(x_k − x_{k−1}).
Both curves are ≥ 0.

**Longitudinal (drive/brake) force** (`0x1E7450`):
- F = C(x)·Fp, negated if sg < 0; G = g·F.
- **Force on the car** (shipped config bytes 0x2D2936/37 = 1):
  - **W+0x88 = F if st > 0 (drive side), else 0**;
  - **W+0x8C = F if st ≤ 0 (brake side), else 0**.
  These forces are **not** multiplied by the slip gain.
- **Wheel reaction** for the wheel spin dynamics: T+4 = G if st > 0 else 0,
  and T+8 = G if st ≤ 0 else 0 (gained). The wheel net torque (§2.7) uses
  T+4 + T+8.
- Brake and drive torque are not read here.
- (Config byte value 2 would make W88/W8C the gained values; it is unused.)

**Slip-angle force** (`0x1E7C08`):
1. T+0x20 := 0. If the fade weight k = 0: W+0x80 = W+0x84 = 0, done.
2. a = wheel steer angle δ, plus π if the hub forward speed < 0.
   β = wrap(a) − wrap(φ), re-wrapped into [−π, π), where φ = W+0x78 is the
   velocity-direction angle in the body frame (§3.2). Store β at T+0x20.
3. Fold β into v ∈ [0, π/2] with sign sg_a (§3.2).
4. u₁ = (sg_a·A(v))·k; u₂ = B(v)·k.
5. m = √(u₁² + u₂²). If W+0x64 < m, both are scaled by W+0x64/m.
6. **W+0x80 = u₁·Fp** (curve A: lateral grip) and **W+0x84 = u₂·Fp**
   (curve B: always ≥ 0, along-velocity drag).

**Frame and meaning.** With φ the velocity direction, d = (cos φ, −sin φ)
the unit velocity in the body frame (x forward, y lateral) and n = (sin φ,
cos φ):

  force on the car from the slip-angle model = −W84·d − W80·n

This is exactly the W80/W84 part of Fa, Fb and F_t in §4.1. Curve B always
opposes the motion, and curve A opposes the lateral slip. **The friction
circle limits only this vector.** The drive/brake force W88/W8C acts along
the wheel heading, is not circle-limited, and is added separately (§4.1).
The D curve is how wheelspin and lock-up cost lateral grip.

**Wheel flag byte** (W+0x5F) used by the forward step's effective mass and
the free-rolling rule (§10.2):
- rebuilt every tick: bit0 = this wheel's load is exactly 0; bit1 = brake
  torque ≠ 0 (pedal or handbrake);
- bits 2/3 are then OR-ed in **only while the clutch is not open**: L0 rear
  8; L1 front 8; L2 front 8, rear 4; L3 front 4, rear 8; L4–6 rear 8; L7
  front 4, rear 8;
- for L4–7, the front wheels also get 4 whenever the centre clutch is
  locked or carrying torque, even with the clutch open.
A wheel adds its Ia/r² to the forward mass only if its byte is 0 when the
forward step runs, which is before the driven/coupled bits are added (§4.1).
In practice only airborne and braked wheels are excluded there. The full
byte matters for the free-rolling rule (§10.2), which runs at the end of the
tick.

## 4. Chassis, suspension, steering, brakes, aero, aids

### 4.1 Planar motion (forward speed, lateral speed, yaw) [C; each routine verified by executing the original, 20000/20000 random states]

Order inside the tire/engine phase (`0x1E8598`):
1. brake torques and clutch link (`0x1E6EC8`, `0x1E6D48`);
2. rolling resistance (zeroes the per-tick force sums);
3. tire force curves and drive/brake parts (`0x1E73C0`, `0x1E7450`,
   `0x1E7C08`);
4. **forward step** `0x1E7848`;
5. wheel net torque `0x1E5768` and torque vectoring `0x1E57F8`;
6. engine and axle integration `0x1E5B98`; differentials `0x1E6488`;
7. engine speed and clutch lock/open `0x1E7500`; gear change `0x1E77E0`;
8. lateral force and yaw `0x1E7D90`, then the stop clamp `0x1E8008`, both
   with an "airborne" flag: the four wheel loads summed in wheel order,
   ((Fz₀ + Fz₁) + Fz₂) + Fz₃, equal 0 (below).

Notation: k_i = cos δ_i and s_i = sin δ_i of the wheel steer angle; a_i =
the wheel's velocity-direction angle (§3.2); W80/W84 = the two tire force
outputs (§3.3); G_i = the wheel's drive/brake force parts (W88 + W8C).

**Forward step** (`0x1E7848`); u = forward speed F+0x488:
1. F_x = Σ k_i·W88_i + rolling resistance.
2. Effective mass: M = Σ over wheels whose flag byte (wheel +0x5F) is 0, of
   the axle's Ia/r² (P+0x2B0 + 4·axle). **At this point of the tick only
   bits 0/1 (load = 0, brake torque) are set.** The driven/coupled bits are
   added later by `0x1E5B98`, so driven wheels **do** count here.
   Examples: RX-7 1390 + 2×72.49 + 2×84.11 = 1703 kg; R34 1680 + 4×75 =
   1980 kg (diff-verified). The inverse mass m⁻¹ = 1/(M + mass) if M > 0,
   else the precomputed 1/mass (P+0x4C); it is kept at F+0x394.
3. a₀ = (F_x + external longitudinal force + aero drag)·m⁻¹;
   u₁ = u + a₀·dt.
4. Tire reaction F_t = Σ (k_i·W8C_i − sin a_i·W80_i − cos a_i·W84_i);
   a_t = F_t·m⁻¹; u₂ = u₁ + a_t·dt.
5. If u₁·u₂ > 0, u = u₂ and the longitudinal acceleration a_x = a₀ + a_t.
   Otherwise u = 0 and a_x = a₀ + (1/dt)·(−u₁), with 1/dt from F+0x4A0 (the
   remaining speed is removed in this tick).
u is re-derived at the start of the next tick from the world velocity
(projected on the ground-frame axes), so the world velocity is the true
state.

**Low-speed hold** (`0x1E7AD8`), per wheel; Fp = peak force (§3.1), w =
wheel speed, (u_h, v_h') = hub velocity in the body frame:
- v_h = v_h'·cos δ + u_h·sin δ is the hub speed **across** the wheel plane.
- Returns 0 if Fp = 0, |w| > 1.38889 (5 km/h), |u_h − w| > 0.555556
  (2 km/h) or |v_h| > 1.11111 (4 km/h). All tests are strict, so equality
  passes.
- Otherwise **F_hold = Fp·sin(0.9π·v_h)·cos(0.36π·w)** (constants
  2.82743 and 1.13097). This is a sideways damper of slope about 2.83·Fp per
  m/s, peaking at |v_h| = 0.35 m/s.
- It acts **only on the lateral force and yaw**, never on the forward speed.

**Lateral force and yaw** (`0x1E7D90`). If airborne: the lateral
acceleration and the grip ratio are zeroed and yaw is unchanged. Otherwise,
per wheel (each product chopped, summed in this order):
- Fa_i (lateral) = −cos a_i·W80 + sin a_i·W84 − k_i·F_hold − s_i·G_i;
- Fb_i (longitudinal) = −sin a_i·W80 − cos a_i·W84 − s_i·F_hold +
  k_i·G_i.

With levers X = (−tf, +tf, −tr, +tr) and Y = (a, a, −b, −b):
- yaw torque N = Σ (X_i·Fb_i − Y_i·Fa_i);
- yaw acceleration = N × P+0x11C (= 1/P+0x118); yaw rate += it × dt,
  clamped to ±2π;
- lateral acceleration a_y = (Σ Fa_i − external lateral force F+0x4D4) ×
  P+0x4C (1/mass);
- **F+0x474 = 4·a_y / Σ_i (rec_i[0]·mu_i[s_i])**, the lateral acceleration
  divided by the summed grip acceleration of the four wheels' tire
  records. Dimensionless and signed: this is the "lateral g / μ" used by
  TCS (§4.6) and the AWD code-5 coupling.

**Stop clamp and world velocity** (`0x1E8008`):
1. While the spawn/stop counter (F+0x536) is non-zero, the world x/y
   velocity, forward drive sum and yaw rate are zeroed.
2. If airborne: nothing.
3. Hold H if any of:
   - the race hold byte F+0x4DC ≠ 0 (written every tick by phase 2,
     `0x1EA428`, from `0x1EAAE8`: set while the start sequence holds the
     cars, §9.3);
   - driver kind 8 (F+0x534; the stop driver's car at rest, §9.4);
   - |u| < 1 km/h (strict) and all four fade weights are 0 and at least one
     wheel has brake torque.
4. If H: zeroed are the forward and lateral speed (F+0x488/0x48C), yaw rate
   (F+0x47C), world x/y velocity (F+0x44C/0x450), the longitudinal force sum
   the load transfer of §4.2 reads (F+0x4D8), a_x and a_y (F+0x4C4/0x4C8)
   and the lateral g / μ (F+0x474). Vertical velocity is kept. [C; the
   exact port `sim::exact::stop_hold` matches it bit for bit in races]
5. Otherwise the world velocity (x, y) += a_x·dt·(forward axis x, y) +
   a_y·dt·(lateral axis x, y).

So with no brake a stopped car is not clamped: below 1 km/h it has no
tire force, only the hold damper and rolling resistance. The sign-change
rule stops it reversing from resistances alone.

**Position and heading** are advanced in the collision step (§5.3, phase
10). Its sweep moves each body by velocity × dt and heading by yaw rate ×
dt, including when there is no contact. [C, `0x1FB268`/`0x1FB6D8`]

### 4.2 Suspension [C, `0x1E9348`, `0x1E8E58`, `0x1E8DD0`, `0x1E8FD0`; blocks built by `0x1DB6B8`/`0x1DB890`, checked numerically for the R34]

Per-axle block (P+0x2B8 front, P+0x2FC rear) from the spec's suspension
bytes b[0..] (spec+0x3A front, spec+0x4A rear) and c = spec 0x5E/0x5F
(percent; s = 100/c):

| Quantity | Formula | R34 front / rear |
|---|---|---|
| spring rate k (N/m) | b[0] × 980 × c/100 (b[0] in 0.1 kgf/mm) | 37240 / 46550 |
| anti-roll gain (N/m²) | b[1] × 98 | 4900 / 3430 |
| progressive gain k₂ (N/m²) | b[2] × 98000 | 1.568e6 |
| unsprung mass per wheel (kg) | b[3] × 0.25; its weight is the preload term | 10 kg (98 N) |
| bump-stop extra damping (N per m/s) | b[13] × 98 | 0 |
| damper points (bump: b[4..7], rebound: b[8..11]) | v_k = byte × 0.01 × s m/s, F_k = byte × 49 N | – |

- Each damper direction is fitted at init to F(v) = G·(√(v − T) − √(−T))
  (T < 0, so F(0) = 0), passing through its two points (closed form;
  invalid points give a zero damper). Stored as threshold T, offset
  O = −G·√(−T) and gain G. [C, `0x1DB5C0`]
- Positions use a suspension coordinate x (0 = spring force zero,
  negative = compressed):
  - static position x_s = −(half the axle's sprung weight)/k, the axle's
    sprung weight being its share (front weight fraction, or its
    complement) of the **sprung** mass, total − 2·(front + rear unsprung),
    times 9.8; each wheel's own unsprung weight is the preload on top.
    [diff harness, block +0x10 on all 234 cars and 590 random settings]
  - ride height q = spec 0x5C (u8, read as a single byte; spec 0x5D is ignored) × 0.001 m;
  - bump-stop limit = x_s − q + min(q, bump room), bump room = spec 0x64/0x65 (u8) × 0.001 m;
  - block layout: +0x00 knee, +0x04 bump limit, +0x08 max droop, +0x0C r − q + x_s, +0x10 x_s [C, `0x1DB890`];
  - knee = x_s − g₁ (g₁ = spec 0x46/0x56 × 0.001), or the bump-stop
    limit when the ride height is very low;
  - max droop = min(x_s + spec 0x66/0x67 × 0.001, preload/k).
- Per wheel per step: x = (body height + hub offset − ground height − road
  bump) × attitude factor + (x_s − q).
  - x beyond max droop: wheel in the air, Fz = 0.
  - Otherwise x' = max(x, bump-stop limit). Spring force
    Fs = −k·x' + (x' < knee ? k₂·(x' − knee)² : 0).
  - Damper velocity v = (x − x_prev)/dt (unclamped x), clamped to [−1, 1]
    m/s. The stored x_prev is the **bump-stop-clamped** x'. An airborne
    wheel stores x_prev = droop and gives body force −preload (§10.5).
    Compressing (v < 0): Fd = O_b + G_b·√(−v − T_b). Extending:
    Fd = −(O_r + G_r·√(v − T_r)). Below the knee while compressing it adds
    bump-stop damping × |v|. (R5900 sqrt.s of a negative value gives √|x|.)
  - **Tire load Fz = max(0, Fs + Fd + preload)**, preload = unsprung weight.
- Per-wheel body forces (`0x1E9348`, F+0x3C4 + 4i) = spring force
  (W+0x70, the spring part only: the damper force W+0x74 enters §4.3
  separately) + anti-roll + load transfer − half the axle's aero
  downforce (F+0x4BC front, F+0x4C0 rear) [C, bit-exact in races]:
  - anti-roll F = gain·d·|d| with d = x'₀ − x'₁ (front) or x'₂ − x'₃
    (rear), the bump-stop-clamped coordinates W+0x18 after the step;
    subtracted on the even (left) wheel and added on the odd (right)
    one, so it resists roll. [C]
  - longitudinal transfer +P+0x38 × (F+0x4D8 − F+0x4D0) on each front
    wheel and the same subtracted on each rear wheel, where F+0x4D8 =
    Σ cos δ·W88 + rolling resistance (the forward step's drive sum, §4.1
    step 1) and F+0x4D0 is always 0 (§10). P+0x38: RX-7 0.03885, R34
    0.03462;
  - lateral transfer P+0x34·(−F+0x4C8), added to the even (left) wheels
    and subtracted from the odd (right) ones;
  - each wheel's sum in this order: ((spring ∓ anti-roll) − aero/2)
    ± longitudinal ± lateral;
  - the same step writes each wheel's visual camber (W+0x10, `0x1E9218`)
    and hub half-track (W+0x14, `0x1E92A8`) from the travel x' − x_s.
  - P+0x34 = sprung mass × X / mean track, P+0x38 = (sprung/total) × X /
    wheelbase, X = 0.5·(spec 0x31 × 0.001 + ride height) (CoG height-like);
  - sprung mass = total − 2 × (front + rear unsprung).
- **Load transfer is explicit only.** [C] GT3 does not compute moments of
  the ground-level tire forces about the CoG. The only coupling from
  acceleration to body attitude is:
  - the two explicit terms above (lateral P+0x34 × lateral force sum,
    longitudinal P+0x38 × longitudinal force sums), added to the per-wheel
    body forces;
  - one longitudinal-force moment term in the pitch equation (§4.3).
  Tire load Fz is always spring + damper + preload, so transfer reaches the
  tires only through the resulting body pitch/roll/heave and spring
  compression.

### 4.3 Body heave, pitch and roll [C; all verified bit-exactly by executing the original functions: `0x1E9640` 5000/5000, `0x1E96F0` 5000/5000, scripted branch 4000/4000, `0x1E9998` 5000/5000]

Angles: pitch a (F+0x468, positive = nose up), roll b (F+0x46C, positive =
right side down), heading c (F+0x470, positive = counter-clockwise from
above). F_i = per-wheel body force of §4.2 (up positive), D_i = damper force
of wheel i. Wheels: 0 FL, 1 FR, 2 RL, 3 RR.

**Longitudinal terms** (`0x1E9640`): per wheel, q_i = cos(δ_i) ×
(longitudinal tire force of wheel i, drive + brake parts) × section width_k
(m). Then:
- A = Σ q_i × kP_k, with kP_k = 0.5·2π/(Y + m·a²) front and
  0.5·2π/(Y + m·b²) rear (Y = pitch inertia parameter, m = total mass; R34
  2.660e-4 / 2.372e-4). A is an angular acceleration (rad/s²).
- B = (q₀ + q₁)/a − (q₂ + q₃)/b, a force (N).
The lever is the tire width, as the code does.

**Update** (`0x1E96F0`), in this order:
1. If the spawn hold is active (below), handle it and stop.
2. Pitch: acc = ((F₀ + F₁) + D₀ + D₁)·L_f − ((F₂ + F₃) + D₂ + D₃)·L_r + A, with
   L_f = a·2π/Y and L_r = b·2π/Y. rate = clamp(rate + acc·dt, ±π/2);
   angle += rate·dt. If the angle passes ±π/4 it is clamped and **rate := 0**.
3. Roll: acc = ((F₀ − F₁) + D₀ − D₁)·R_f + ((F₂ − F₃) + D₂ − D₃)·R_r, with
   R = half-track·2π/Z (Z = roll inertia parameter). Same rate and angle
   clamps. No A or B term.
4. Rebuild the rotation matrix (§1.3).
5. Heave: u = (ΣF_i + ΣD_i + B) × (1/sprung mass). World velocity +=
   (new body up axis) × u·dt; **then** v_z −= 9.8·dt (gravity in world Z
   only). Position is not changed here (§4.1). F+0x4CC := u − 9.8 × (up
   axis z), the heave acceleration less gravity along the up axis. The
   sums run in the order (((((((F₀ + F₁) + F₂) + F₃) + D₀) + D₁) + D₂) +
   D₃) + B. [C, bit-exact in races]
There is no other pitch/roll damping.

**Spawn/stop hold** (`0x1E9528`, `0x1E90F8`, `0x1E9160`): F+0x536 is a
counter decremented once per step. It is set to 120 only for the
free-practice pit start, and to 300 by a pit stop (§8.6, §9.4); grid starts
do not use it.
- While it is non-zero, the height offset is f(n): 0 for n < 30; 0.3 m for
  30 ≤ n < 270; (300 − n) × 0.01 m for n ≥ 270, computed as
  float(300 − n) × 0.3 / 30.
- Target = ground height under the CoG (F+0x38, §4.3 ground frame, from
  the previous step) + f. If the body is at or below the target, it snaps
  to it: height = target, and vertical velocity, pitch, roll and both
  rates are zeroed. Then, for 30 ≤ n < 270 only, the heading eases
  towards the pit box's heading at ≤ 30°/s (snapping when within one
  step; d = the wrapped difference `0x1D8C80` of the box heading and
  heading, a step dt × 30° in radians). The box heading is course data:
  the heading (+0x24 of the point) of the AI course object's stop line
  (game line 4, the course file's AI line 3; conductor + 0xBBA8 → +0x04 →
  +0x18, 0x28 bytes a point) at the car's pit box F+0x4DE (s8; the line's
  count − (index + 4), set at the start), through `0x1F2F08` → `0x1F9E10`
  [C]. The body axes
  are rebuilt and the rest of the update is skipped. Otherwise (the car
  is above the target) the normal dynamics run.
- So a car in the pit box is held jacked up and then settles flat. The
  same counter also zeroes the clutch request and holds gear 1.

**Bump-stop attitude nudge** (`0x1E9998`): this runs only if some wheel was
beyond its bump-stop limit at the start of this step or is after the
integration (contact mask F+0x4DD: high nibble = start, low = now).
1. Recompute hub heights and x_i, using the stored terrain factor s.
2. If any wheel is now beyond its bump stop:
   - penetration d_i = max(0, bump limit_k − x_i);
   - L = d₀ + w·(d₂ − d₀), R = d₁ + w·(d₃ − d₁), with w = P+0x20 = 1 − front
     weight fraction;
   - F_m = (d₀ + d₁)/2, R_m = (d₂ + d₃)/2;
   - roll b += atan2(L − R, mean track) if L ≠ R;
   - pitch a += atan2(F_m − R_m, wheelbase) if F_m ≠ R_m;
   - height += 0.5·min(L, R) + 0.5·min(F_m, R_m).
   No clamps are applied and the rates are untouched.
3. Rebuild the matrix. Then limit the downward speed: v_z ≥ min(0, the
   vertical component of the velocity projected onto the ground plane).
   On flat ground the car cannot keep sinking.

**Terrain factor and ground frame** (`0x1E91A8`, `0x1EA4D8`):
- s = (ground normal z)/(body up z) scales the suspension coordinate (§4.2).
  The ground frame it reads is the previous step's: `0x1EA4D8` runs near the
  end of phase 12 (order in §1.2). Likewise the speed ramp W+0x6C (§8.3) is
  computed at the start of phase 12 from the previous step's hub speeds.
- Ground height under the CoG = average of the side values U = g₀ +
  w(g₂ − g₀) and V = g₁ + w(g₃ − g₁).
- Ground pitch = atan2(((g₀ + g₁) − (g₂ + g₃))/2, wheelbase); ground
  roll = atan2(U − V, mean track).
- Ground frame = the Euler matrix of (ground pitch, ground roll, heading).
- Hub height offsets (`0x1E9CE8`): W_i.z = x_long·forward.z + y_lat·
  lateral.z, with (x_long, y_lat) = (a, −tf), (a, +tf), (−b, −tr), (−b, +tr).

### 4.4 Driver input: device → controls → wheel angle [C; `0x1E4478` verified on 5975/5975 and 10000/10000 random frames and states; the whole `0x1E4D38` path in playback verified frame-exact on 8 disc demos]

**Path.** Every step `0x1E4D38`:
1. clears a 16-byte frame F;
2. fills it from the live pad (device mode 0 or 3: live, and recorded),
   or from the replay stream (any other mode; nothing while paused);
   then, unless F+0x3EA ≠ 0, the conductor's pad hook (virtual 20: empty
   except in free run, which merges a second device into the frame);
3. sets F+0x522 := 1 and dispatches on the control source F+0x3E9:
   - 0 (pad): `0x1E4290` (HUD bytes, below); then with driver kind
     F+0x534 = 0 or the hold counter F+0x536 running the pad path
     `0x1E4CB0`: `0x1E4478(car, F, O)`, the throttle F+0x428 copied to
     the engine's throttle F+0x4A4 (the TCS input) and the brake pedal
     F+0x42C written into all four wheels' brake pressure (W+0x90,
     `0x1EAC60`), followed by the AI driver object's update `0x1F6648`
     (car+0xEC8; it keeps the AI's state current while the pad drives);
     otherwise (the AI drives a player car) the AI step `0x1E35D8`,
     `0x1F6588` and the apply `0x1E4CF0`;
   - 1 (the AI drives with the pad decoded for display): `0x1E4290`,
     `0x1E35D8`, `0x1E4CF0`;
   - 2 (CPU): `0x1E35D8`, `0x1E4CF0`; any other value: nothing.
Recorded frames go through exactly the same code as live frames.

**HUD bytes** (`0x1E4290`): F+0x523 = the steering in percent (digital:
+100, 0 or −100 by the sign; analog: s·100/4096 truncated towards 0);
F+0x522 cleared when the throttle is released (digital) or not exactly
4096 (analog). For an AI-applied car `0x1F79F0` writes F+0x523 =
int(δ·100 / P+0x58) instead.

**Frame F.**

| Offset | Meaning |
|---|---|
| +0 flags | bit0 steering analog; bit1 throttle F[8] analog; bit2 brake F[0xA] analog; bit3 axis F[0xC] analog; bit4 axis F[0xE] analog. A clear bit means the axis is a 0/1 button. |
| +1 kind | 0 digital buttons, 2 table-mapped analog axis, 3 DualShock stick, 4 steering wheel |
| +2 | shift request (s8: +1 up, −1 down); acts only with a manual gearbox |
| +4 | steer s16, ±4096 = full (positive = left in the disc recordings) |
| +8 | throttle (0..4096 if analog) |
| **+0xA** | **brake** (0..4096 if analog) |
| +0xC | the back (reverse) axis: brake lamp, the reverse request O[0], and with pedal styles 0–2 the throttle when at least as large (below) |
| +0xE | third axis (handbrake): instant ramp; ≥ 0.1 disables ASCC |

**Persistent output byte O[3]** (car+0x1307), the steering rate class:
- set to 1 by a digital frame (bit0 clear) or by a kind-3 frame with steer
  ≠ 0;
- set to 2 by a kind-2 frame with steer ≠ 0, and to 0 by kind 4;
- **unchanged by a frame with steer = 0**.
Initial value 0.

**Controller settings** are fixed static data (0x2D4628). No code writes
them, so no options menu changes them. Derived constants: pedal ramp 1/6
per step (throttle and brake up/down), third axis instant; analog constant
c = 0.1; follower gains k₁ = 100, k₂ = 5, k₃ = 50.

**Steering, pad kinds (0–3):**
1. v = max(forward speed, 0).
2. Curve: course category 0 or ≥ 3 → curve B; dirt (2) → curve C if
   both front wheels are on "loose" surfaces (class table 0,0,1,1,1,1,1,
   0,0,0,0,0,2,2,2,2), else curve B (with rate index 2 when both are class
   2). Category 1 (the wet courses: Route 5 wet, the wet ovals) uses curve B
   with rate index 2, which has the same numbers. (On the Tahiti Maze the
   road is surface 0, so curve B; Smoky Mountain's road is loose.)
3. lock = curve(v).
4. **Slip-angle extension:** m = mean velocity-direction angle of the two
   front wheels (§3.2). If m > lock the upper bound becomes m; if m < −lock
   the lower bound becomes m.
5. Both bounds are clamped to ±max lock (P+0x58).
6. **Target t (a position):**
   - digital (bit0 clear): upper bound, 0 or lower bound by the sign of the
     steer;
   - analog kinds 2/3: t = (s/4096)·upper for s ≥ 0, or (s/4096)·|lower|
     for s < 0;
   - kinds 0/1 with bit0 set: the angle is left unchanged.
   - F+0x49C := m when step 4 extended a bound, 0 when m was within
     ±lock (left as it was when the extension is skipped, wheel types 3 / 4);
     after the steering a non-zero F+0x49C becomes clamp(steer command /
     F+0x49C, 0, 1). F+0x420 := 1 every pad frame.
7. **Follower** (`0x1E4368`), d = steer angle, w = its rate:
   - acc = 100(t − d) − 5w;
   - if returning (target on the centre side of d, including across
     zero): acc −= 50·d, and the return rate limit applies;
   - w = clamp(w + acc·dt, ±r); d' = d + w·dt; if d' reaches or passes t,
     then d = t and w = 0.
   - **Rate limits r:**
     - O[3] = 1 (digital, kind 3): **0.5236 rad/s (30°/s) away from
       centre, 2.0944 rad/s (120°/s) returning**;
     - dirt curve C: 2.0944 both;
     - O[3] = 2 or 0: 2.0944 both ways.
   - Because k₁ saturates the rate in one step, this is a rate-limited ramp:
     a digital turn-in from 0 to 30° takes about 1 s.

**Steering wheel (kind 4):** no follower; the wheel position is the angle.
- Angle = (s/4096)·bound, where the bounds (F+0x4AC / F+0x4A8) slew
  towards ±lock at ≤ 90°/s and lock comes from curve A (curve C on a dirt
  course, whatever the surface).
- The wheel type (car+0x136F, s8): types 1 and 3 raise the lock to at least
  8° (15° on dirt), type 2 to 12° (20°); types 3 and 4 skip the slip-angle
  extension. Type 4 uses ±max lock directly; type < 0 or > 4 gives 0.
- The raw position goes to car+0x16A4, the previous one to car+0x16A6.
- The bounds are written for digital frames and kinds 2 / 3 too (= the
  clamped lock bounds); kinds 0 / 1 with bit0 set leave them.

**Throttle** (F[8]) and **brake** (F[0xA]):
- digital: ramp ±1/6 per step;
- analog: **value = x + 0.1·(1 − x) for x > 0**, else 0 (a 10 % jump as soon
  as the pedal moves);
- kind 4 adds a pedal curve 0.5x² + 0.5x;
- the brake value goes unchanged to all four wheels; ABS, ASCC and the dirt
  cut then scale it per wheel;
- pedal styles (race entry, F+0x461; taken as 0 for control source 1 or a
  driver kind ≠ 0): 0–2 let the F[0xC] axis replace the throttle when it is
  at least as large (its own analog flag then decides digital or analog);
  5 drops it; 3, 4, 6 and ≥ 7 keep it for the reverse request only. 3 makes
  the brake axis a clutch: clutch request F+0x418 = 1 − brake, the brake
  pedal := 0, and unless the pedal is all the way down (request 0) the
  reverse and shift requests are cleared; 6: a shift request or a back axis
  ≠ 0 gives clutch request 0 (`0x1E4180`; the rest of the pad clutch rule
  is §2.4's);
- the digital throttle keeps a state O[2] (car+0x1306): 1 while it rises
  with the throttle button, 2 while it rises with the back axis, 0 at rest;
  while a released throttle ramps down (digital, or an analog frame at 0
  after a button), state 2 keeps the reverse request O[0] = 1, and the
  state returns to 0 once the value reaches 0. The kind-4 throttle curve
  applies only in state 0;
- the third axis (F+0x430) ramps by 1 per step (digital) or takes
  x + 0.1·(1 − x) (analog);
- shift request O[1] (car+0x1305) = frame byte 2, 0 while the hold counter
  runs.

**Demos** (disc `demo/<event>` and replays):
- **Consumption:** one recorded frame is consumed per race step **from step
  0**, including countdown and AI-driven run-up phases, where it is
  decoded but ignored. There is no hand-over marker. After the last frame
  the original delivers an **all-zero frame** (digital, nothing pressed).
- **Steer decoding:** the stream decoder extrapolates steer each frame (if
  |s| ≥ 16, s := clamp(2s − s_prev, ±4096)) before applying the next
  delta.
- **License demos:** every frame has flags = 31 (all analog). The kind
  alternates 2/3, and kind 3 occurs exactly when steer ≠ 0, so O[3] = 1
  (slow turn-in) throughout. Shift fields are 0.
- **Header:** gives the automatic gearbox, pedal style 0 (1 for arcade
  demos) and ASCC/TCS enabled (the car's own aid settings).
- **Control source:** the demonstration plays with the pad path driving
  (control source 0) [I: which race-flow value the license demo screen
  passes was not traced].

**Shift input:** byte 2 → O[1], used only by the manual gearbox (§2.6).

**Reverse request O[0]** (car+0x1304) [C, 2026-10-24; corrects earlier drafts, which said
reverse has no input]: in `0x1E4478`, F[0xC] (× 4096 when digital) is compared with the
throttle F[8]; when F[0xC] ≥ F[8] it replaces the throttle value and O[0] = (F[0xC] ≠ 0), else
O[0] = 0 (F[0xC] is taken as 0 while the spawn counter F+0x536 runs). The gear logic
(`0x1E77E0` → `0x1E5690` → `0x1E4FF8`, passed car+0x1304) reads O[0] as rule 2's reverse
request (§2.6); the AI's apply `0x1F79F0` writes its own reverse flag (driver +0xA8) to the same
byte (car F+0x380). The request is therefore part of the pad frame, and replays carry it.

**AI cars and the drivetrain** [C]: the drivetrain and differential code
has no branch on driver kind, so AI cars use exactly the same differential,
LSD lock and clutch models. The differences are:
- AI cars use pedal style 0 and the automatic gearbox;
- AI cars get a binary clutch request, **recomputed every step from the
  current clutch link state** (it keeps no memory of its own):
  - link state 0 (open): request = 1 iff rpm > 0.75 × upshift rpm;
  - any other state (locked, slipping, shift): request = 1 iff rpm ≥ idle.
  So after a moving start (clutch locked) an AI car stays engaged
  immediately;
- the AI's aids use a second parameter block (car+0xEF8 = driver object
  +0x30), filled at car init from the AI driver parameter set (ai.md §9.1).
  With every built-in set this gives **TCS on (gain 6.0, lateral a = 1.0,
  b = 0, slip scale 1.0) and ASCC off**. Event overrides never change it
  (verified by execution; corrects earlier drafts, which said "all zero");
- AI cars write the road-wheel angle directly.
- The AI step's own part (`0x1E35D8`, before the AI decision): F+0x430 := 0,
  O[0] = O[1] := 0; with the race hold F+0x4DC or the hold counter running:
  `0x1F8A00` (driver object, the race info's hold value +0x5C or the counter;
  it clears the clutch request and moves the AI throttle), brake pedal and
  wheels 0, steering command 0, and nothing else; driver kind 8: clutch
  request 0 and `0x1F7900`; otherwise the AI clutch request above, then the
  decision `0x1F7AE0`.
- The apply (`0x1F79F0`, then `0x1E4CF0`): throttle F+0x428 and F+0x4A4 =
  the driver object's request +0xA0, moved by at most dt per step in driver
  mode +0x83 = 1 or 2; steering command F+0x41C = +0x9C; brake pedal and
  all wheels = +0xA4; reverse request O[0] = +0xA8; the gearbox lock-out
  F+0x43A = the low byte of +0xAA when that is non-zero; F+0x523 as above;
  driver kind 8 then sets the third axis F+0x430 := 1.
Nothing special happens in tight corners. If our AI stalls on a locked
diff, the cause is elsewhere: compare its clutch request with this rule.

### 4.5 Brakes and ABS [C structure, `0x1E3068`, `0x1E2D08`]

- Brake pedal value b (0..1) is copied to each wheel's brake pressure
  p_i. The aids then modify p_i (§4.6).
- ABS / pressure limit, every step, per wheel with p_i > 0:

  ```
  p_i = p_i × clamp( F_peak,i × E[axle] × (1 − G[axle] × g_i × (s_i − s*_i)), 0, 1 )
  ```

  (0.001 instead of exactly 0). s_i is the braking slip ratio (§3.4,
  positive when braking), s*_i the tire record's optimum braking slip ×
  per-axle scale (spec 0x112/0x113 %), g_i the low-speed gain, G[axle] =
  brake-controller setting × 0.1 (front/rear), and E[axle] = (per-axle tire
  reference value) / (brake record +0x11/+0x12 × 98 N). So once the slip
  exceeds the optimum, pressure falls linearly: an ABS that is always on, its
  strength set by the brake-balance controller. [C formula; I physical
  meaning of E]
- Brake heat: a per-wheel state in [0, 1], visual only (§8.5).
- **Brake torque** per wheel [C, `0x1E6EC8`]: T_b = p_i × B[axle] N·m with
  B = brake part bytes +0x11/+0x12 (spec 0xDE/0xDF) × 98 (R34 3332 front,
  2254 rear). Rear wheels add handbrake × spec 0xE0 × 98 (R34 4900 N·m).
  The torque always opposes the wheel's spin and enters the axle-speed
  integration (§2.7). **The front/rear split is fixed by the brake part.**
  The "brake balance" setting (spec 0x10A/0x10B × 0.1) is only the ABS gain
  G above. E[axle] = tire radius / B.

### 4.6 Driving aids [C, `0x1E3B18`; bit-exact in `race_tick_exact`, `sim::exact::aids`]

**Phase 6** (`0x1E3B18`) runs the indicators, clears both status bytes
(F+0x520 ASCC, F+0x521 TCS), then by the control source F+0x3E9:
- 0 (pad): the car's own aid block (P+0xE7C) through `0x1E3A68` (TCS, then
  the brakes with ASCC enabled while the handbrake axis F+0x430 < 0.1);
  but with a driver kind F+0x534 ≠ 0 and the hold counter F+0x536 = 0 the
  driver's block through `0x1E3AD0` instead, after which the status bytes
  are set from the own block (F+0x521 = its TCS gain ≠ 0, F+0x520 = A or D
  enabled);
- 1, 2 (CPU): the driver's block (car+0xEF8) through `0x1E3AD0` (TCS, then
  the brakes with ASCC enabled);
- anything else: no aids.

Indicators (`0x1E3708`), u = F+0x39C, o = F+0x3A0, both 0 unless moving
forward (v = F+0x488 > 0); Σ = the four wheels' tyre record +0x00 (for
their surfaces):
- Understeer, only when δ·r ≥ 0 (δ = F+0x41C, r = yaw rate F+0x47C; for
  δ < 0 both, and the yaw acceleration F+0x478, change sign): target
  r_t = v·sin(δ)/L (P+0x18); when r_t > 0 and |a_y| (F+0x4C8) / Σ > 0.2:
  r += ½·yaw acceleration if they share a sign, then u = 1 − r/r_t if
  r < r_t.
- Oversteer of a pad car (control 0, driver kind 0): m = largest |slip
  angle| of the rear wheels (T+0x20, the previous tick's); q = min(1,
  0.018·√(v² + v_lat²)); threshold θ = θ₀ + q(θ₁ − θ₀) with θ₀, θ₁ = bytes
  0x2D295D / 0x2D295E (20°, 5°); when m ≥ θ: o = min(1, g·(m − θ)/(90° −
  θ)), g = g₀ + q(g₁ − g₀), bytes 0x2D295F / 0x2D2960 × 0.1 (3, 14).
- Oversteer otherwise (control 1 or 2, or a driver kind ≠ 0): with
  x = √(v² + v_lat²)·r, when x·a_y < 0: o = 2·max(0, |x| − |a_y|)/Σ
  (unclamped), else 0.

TCS (`0x1E3360`, active when its gain ≠ 0):
throttle ×= clamp(1 + gain × g × (s_min − s*_drive × lat × scale), 0, 1),
where s_min is the most negative (wheelspin) slip of the driven wheels,
s*_drive is the tire's optimum drive slip, and lat = 1 when |lateral accel|
≤ b, falling linearly with gain a above it. Exact rule [C, 2026-10-25]:
- status F+0x521 = 1 when the gain ≠ 0 (else 0 and nothing else runs); the
  lateral factor is taken; nothing more happens while T_wot (F+0x4B0, §10.3)
  ≤ 0 or the throttle (F+0x4A4) = 0.
- wheels by the drive-layout byte car+0x208: 0, 5, 6 the rear pair (so
  every AWD car with system 3 or 4, e.g. the R34, reads only its rear
  wheels), 1 the front pair, 2–4 and 7 all four. A wheel with Fz (W+0x08,
  the previous tick's phase-12 load) = 0 is skipped. s_min starts at 1; a
  slip (W+0x68) below it takes s_min, s*_drive from that wheel's tire record
  for its surface (`0x1DB9B8`, +0x140) and g = W+0x48. With no such wheel
  all three stay 1.
- the factor is clamped to [0, 1]; status 2 when it is below 1; F+0x4A4 ×=
  factor. The lateral input F+0x474 is read as the last lateral step left
  it (§4.1); the TCS does not write it.
- the spec's TCS level (spec 0x117) does not switch TCS off by itself: only
  level 100 does (the fix-up zeroes the gain, §6.3); level 0 runs with
  scale 1.0. The gain is also zeroed per race entry (below).

**ASM / TCS per race entry** [C, `0x1DDAD0`, 2026-10-25]: each race entry
carries two switches, ASM (+0x20F0) and TCS (+0x20F4), both 1 after the
entry constructor (`0x1771B8`); link battles and replays set them from their
own data. When the car initialisation builds the spec, ASM 0 clears the ASCC
channel flags (spec 0x109, so no channel is enabled) and TCS 0 clears the TCS
gain (spec 0x110), before `0x1DBFD8` builds the aid block. They act on the
car's own aid block only (the human path `0x1E3A68`); the scripted path uses
the driver's block either way. Ours: `sim::apply_aid_options`.

ASCC (`0x1E2D08`; a0 = F, a1 = P, a2 = aid block, a3 = float[4] brake
target slip, t0 = enable; decoded by execution in the diff harness). Each
tick, with o and u the oversteer and understeer indicators and p_i the wheel
brake pressures (**reset to the pedal value every tick**, so nothing
accumulates across ticks):
1. Target slip cap_i (tire record +0x144) ×= P+0xE40 (front) / +0xE44
   (rear). Always applied; this is the ABS target of §4.5.
2. If not enabled, stop.
3. Side: s2 = 0 if yaw rate > 0, else 1; s3 = 1 − s2 (wheel index s for
   the front axle, 2 + s for the rear).
4. **Brake pedal exactly 0:**
   - A (if enabled): x = (o − thr_A)·gain_A; if x > 0, p += x (capped at 1)
     on wheel s3 and/or 2 + s3, per the A front/rear flags.
   - C (if enabled and throttle exactly 0): x = (u − thr_C)·gain_C; if
     x > 0, p += x (capped at 1) on wheel s2 and/or 2 + s2.
5. **Brake pedal ≠ 0:**
   - D: f = max(0, 1 − (o − 0.1)·gain_D); if f < 1, both cap and p are
     multiplied by f on s2 / 2 + s2 per the D flags.
   - E: f = max(0, 1 − (u − 0.1)·gain_E) on s3 / 2 + s3.
6. Status F+0x520 = 2 if channel A or D acted (C and E acting do not set
   it); else 1 if A or D is enabled; else unchanged.

Aid block layout: +0x00/+0x04 A gain/threshold, +0x08 D gain, +0x10/+0x14 C
gain/threshold, +0x18 E gain; flag bytes +0x0C/+0x0D (A front/rear),
+0x0E/+0x0F (D), +0x1C/+0x1D (C), +0x1E/+0x1F (E); enables +0x30..+0x33 (A,
D, C, E). The growth seen over several ticks comes only from o and u
growing, not from accumulation.
- While the handbrake axis is ≥ 0.1 only the ASCC channels are off. TCS,
  the ABS pressure limit and the dirt brake cut (§10.4) stay active.
Arcade mode zeroes all aid parameters (§6.3).

**The CPU / scripted aid block** (driver +0x30 = car+0xEF8, ai.md §9.1) has
the same layout and the same channel rules. Its values come from the AI driver
parameter set (bytes 9–13 and 19–24), so its ASCC channels act exactly where
the set enables them: a channel needs a flag of it (byte 19) and a non-zero
gain. The built-in sets enable none (flags 0); many course sets (ai.md §2.4)
enable some, mostly front channels (C-front, often with A, D and E front). [C, `0x1F5FD8`; 2026-10-20]

Aid parameter block (P+0xE7C) and its spec sources [C, `0x1DBFD8`; spec
sources in §6.2]:

| Field | Value | Spec byte (source) |
|---|---|---|
| ASCC A gain / threshold | spec 0x10C × 0.01 / spec 0x10D × 0.01 | ASCC interp(+0x14, +0x15; setting 1) / ASCC +0x13 |
| ASCC D gain | spec 0x111 × 0.01 | ASCC interp(+0x19, +0x1A; setting 2) |
| ASCC C gain / threshold | spec 0x114 × 0.01 / spec 0x115 × 0.01 | ASCC interp(+0x1F, +0x20; setting 3) / ASCC +0x22 |
| ASCC E gain | spec 0x116 × 0.01 | ASCC interp(+0x24, +0x25; setting 4) |
| channel wheel flags | spec 0x109 bits 0–7 = A-front, A-rear, D-front, D-rear, C-front, C-rear, E-front, E-rear | ASCC +0x17, +0x16, +0x1D, +0x1C (2 bits each) |
| channel enable | A: (bit0 or bit1) and A gain ≠ 0; D: bits 2/3 and D gain ≠ 0; C: bits 4/5 and C gain ≠ 0; E: bits 6/7 and E gain ≠ 0 | – |
| TCS gain | spec 0x110 × 0.1 (0 = TCS off; also forced 0 when spec 0x117 = 100) | TCSC +0x13 |
| TCS lateral a / b | spec 0x10E × 0.1 / spec 0x10F × 0.01 | TCSC +0x14 / +0x17 |
| TCS slip scale | spec 0x117 × 0.01, or 1.0 when spec 0x117 is 0 or 100 | TCSC interp(+0x15, +0x16; setting) |

In the TCS formula of this section, gain = TCS gain, a and b are the
lateral pair, scale is the slip scale, and g is the wheel's slip gain
(§3.4). The lateral input is |F+0x474|, the lateral acceleration divided
by the mean available grip acceleration (§4.1; 1.0 ≈ 12.4 m/s² for the
R34 on tarmac; verified 300/300).

### 4.7 Aerodynamic coefficients [C, `0x1DBFD8`]

- A_f = 0.5 × ρ × A with ρ = 1.2 (a global set at race init from the static
  block byte0/10 = 12/10). A = spec 0x16 / 10000 m² (body f32 +0x44 × 10000;
  R34 2.0 m²), or width × height when that is 0.
- C_d = spec 0x32 × 0.01 (body byte; R34 0.35). Front/rear downforce
  coefficients C_f = A_f × spec 0x33 × 0.01 and C_r = A_f × spec 0x34 ×
  0.01, where spec 0x33/0x34 are the user's front/rear downforce settings.
  Drag coefficient = A_f × C_d + 0.1 × (C_f + C_r). R34: 0.511, 0.348,
  0.564.
- Forces (×v², with the slipstream factors of §5.2): drag enters the
  forward-speed sum (§4.1). **Front and rear downforce** (always ≥ 0,
  unsigned bytes) are subtracted half per wheel from the suspension body
  forces of that axle (§4.2), so they push the body down and raise tire load
  through the springs. They also enter pitch. They do not add to Fz
  directly. [C]

## 5. Car–car interaction, slipstream, walls

### 5.1 Car–car proximity (`0x1EC098`) [C]

This pass computes **no forces or impulses** (car–car impulses, when
enabled, are in the collision manager, §5.3). It classifies the
relative positions of nearby cars for HUD/AI and computes the slipstream
factor. Steps:

1. Broad phase per pair (`0x1EBBC8`): reject if either car is inactive or
   they are on different "layers" (the pit-surface bit 4 of F+0x535 differs); reject if the
   centre distance (a true distance, √(dx² + dy² + dz²)) is > 100 m, or if B's height above A
   is outside [−(h_B + 5 m), h_A + 5 m] with h the body height P+0x0C. Otherwise compute B's
   position in A's frame (u along A's forward axis, v along A's lateral axis) and vice versa.
2. Relative-position code (`0x1EBF68`, only within 50 m centre distance): a low nibble 1..6
   from u against the combined front/rear extents (×1 and ×4), and a high
   nibble 1..7 from v against the combined width W (±0.5W, ±W, ±4W).
3. Contact zones (`0x1F6B90`, within 20 m centre distance): 8 perimeter cells around each car
   (driver block +0xB4..: front, front left, front right, left, right, rear left, rear right,
   rear), cleared at the start of the pass. With "ahead" = A's front + B's rear extent,
   "behind" = A's rear + B's front and W = half the widths' sum: u > ahead is the front row,
   u < −behind the rear row, otherwise the side cells. The cell holding B's centre gets 2: in
   a row, the left / right corner when v < −W / v > W, else the row's centre cell plus 1 in its
   corner on v's side (v > 0 right); beside, left (v < 0) or right, plus 1 in the front (u > 0)
   or rear corner on that side. Later pairs overwrite earlier ones. Plus a contact intensity
   (`0x1F6CC0`) from the gap and closing speed, c ∈ {0, 0.25, 0.5, 1} (exact rule in ai.md
   §14). They feed only the AI (throttle lift, braking distance, lane changes). Contact
   forces are in §5.3.
4. A rejected pair gets slipstream factor 0 and code 0 both ways. Each car's slipstream target
   (F+0x528) is its row of the factor matrix (conductor +0xB7B4, [car][other], codes at
   +0xB8B4) summed in order, the never-written diagonal included, then clamped to 1.

### 5.2 Slipstream (drafting) [C]

- For each ordered pair (follower F, leader L), with (x, y) = F's position in
  L's frame (x forward), v_L = L's forward speed (m/s), h = L's body width
  (P+0x08, the full width: the window is a width either side of L's axis):
  factor = (v_L + x) / v_L if v_L > 27.78 m/s (100 km/h) and −v_L < x < 0 and
  −h < y < h; else 0. (1.0 when touching L's tail, 0 at one second of travel
  behind.) [C, `0x1EBF00`]
- The factors from all leaders are summed and clamped to 1 (target `x`).
  [C, `0x1EC098`]
- Each step the filtered factor y follows it: if x ≠ 0 (only zero is tested), y += x × K_up × dt
  (clamped to ≤ 1); else y −= K_down × dt (clamped to ≥ 0). [C, `0x1DB118`]
- Aerodynamic force with slipstream y (v = forward speed, m/s):
  - drag = −sign(v) × C_d × (A + (1 − A)(1 − y)) × v²
  - front and rear downforce (§4.7):
    F_j = C_j × (B + (1 − B)(1 − y)) × v².
  [C] C_d and C_j are per-car coefficients set at init (car `+0xE70`,
  `+0xE74`, `+0xE78`; diff-harness verified); their derivation from the body record is pending.
- The four tunables are globals set at race initialisation (conductor
  `initialize()` → virtual 21 → `0x1DAE70` → `0x1DB020`) from a static
  parameter block at `0x2D28B0` in the program's data [C]:
  - A = byte1 / byte0, B = byte2 / byte0;
  - K_up = 10 / byte5 (1.0 if the byte is 0), K_down = 10 / byte6 (0.25 if 0).
  With the values in the shipped image (byte0 = 12, byte1 = 6, byte2 = 12,
  byte5 = 5, byte6 = 20): **A = 0.5, B = 1.0, K_up = 2.0 /s,
  K_down = 0.5 /s**. So full slipstream **halves aerodynamic drag** and
  leaves downforce unchanged. The factor builds up in about 0.5 s and decays
  in about 2 s. [C for the formulas and the block values; I that nothing
  overwrites the block before a race, because no writer has been found]
- Another global derived from the same block, byte0 / 10 (= 1.2), is the air density
  0x352920, which the car's parameter converter reads [C, `0x1DB020`, `0x1DBFD8`].

### 5.3 Walls and contact response [C, collision manager `0x1FA6A0`–`0x1FE768`; `0x1FA018` is the AI line evaluator]

**Regional releases.** This section is NTSC-U 1.10; PAL matches it apart from its
50 Hz timing. NTSC-J 1.01 (`SCPS_150.09`, an earlier build) differs: its commit
(`0x1F8BC8` there, ours `0x1FB6D8`) has **no yaw-rate clamp (±20π)**; its wall
friction (`0x1FBB18`, ours `0x1FE660`) has **no zero-length guards**; its angle wrap
(`0x1D6650`, ours `0x1D8BA8`) adds / subtracts 2π in loops instead of the truncating
formula. Its chamfer build (`0x1FA740`), push-out (`0x1FDA10`) and vehicle step
(`0x1F39D8`) differ in shape (behaviour not checked). Constants are identical. The
engine uses the NTSC-U collisions with every disc (user decision 2026-10-25: no
option for the NTSC-J ones for now). Addresses in this note are each program's own.

Wall (and, in some modes, car–car) collision is handled by a separate
**2-D rigid-body collision manager** (conductor + 0xBBB4). It holds one body
record (0x4F0 bytes) per car. It runs in step phase 10 (§1.2, conductor
virtual 12):
- time trial, free run, license, machine test: walls only, each car
  separately (`0x1FACB8`);
- single player with opponents, free practice, battles: all bodies, walls
  **and car–car** (`0x1FA8F8`).
Before it runs, each car's horizontal position, height, velocity (3-D),
heading and yaw rate are copied in. Afterwards they are copied back, so the
collision step directly overwrites them. [C, `0x1E8730`/`0x1E8828`]
In detail [C, bit for bit `race_exact_lockstep`]: the copy-in `0x1E8730` sets body i's committed
position +0x454..+0x45C from F+0x00..+0x08, velocity +0x460..+0x468 from F+0x44C..+0x454, heading
+0x46C from F+0x470, yaw rate +0x470 from F+0x47C and the road segment hint +0x474 from F+0x3EC;
the copy-out `0x1E8828` writes position, velocity, heading and yaw rate back (not the hint). The
step reads the car's dt (car+0xEC0), steering percent (car+0x14A7) and ghost flag (car+0x136E)
through body +0x450. The base conductor's virtual 12 (`0x1F3950`) runs the walls-only step for
every car in index order between one copy-in and one copy-out; the other conductors' (`0x1F3918`)
the all-bodies step. Each response reports itself to the race (`0x1DFEB8`: the conductor's
virtual 41 with (car, kind, part, x), then the car's hit record `0x1DFD98` with the same): a
point on a wall: kind 0, the point, x = |j|; a wall corner on an edge: kind 0, the edge + 8, |j|;
a car on a car: kind 1 to the struck car with its edge + 8 and to the striking car with its
vertex, x = |j| / 15000 to both. The hit record keeps the strongest hit of the step (car+0x16C8,
cleared by phase 9) with its kind (car+0x16CC) and part (car+0x16CD): the value min(1, |j| /
50000) for a wall, min(1, 2x) for a car (kind 2: |x| / 50; −1 does nothing), 0 below 0.2; it
replaces the record when larger. The impact effects `0x1FB890` (per body after the commit):
with the step's strength s (+0x448, before its cap) > 0 and the hit sound idle (+0x4E8 = 0) or
louder than its level (+0x4EC < s) the sound holds 60 steps at level s (its call of `0x1DFD98`
has kind −1, nothing); else the hold counts down (not below 0) and the level decays × 0.98;
then s is capped at 1; with the rumble running (+0x4E0 > 0) a stronger hit (+0x4E4 < s) restarts
it at s for 24 steps (car+0x1474 raised to s when lower), else it counts down and clears
car+0x1474 at 0; idle, a hit (s > 0) sets car+0x1474 = s and starts it (24 steps at s). The
build `0x1FAE38` clears +0x4E0..+0x4EC.

Body (built at race init, `0x1FAE38`):
- inverse mass = 1/m; inverse yaw inertia = 1/(10 × I_z), with I_z = chassis
  inertia parameter × 100 (§6.4);
- an **octagon** outline in the car's ground plane, with a = CoG→front
  extent, b = CoG→rear extent, h = width/2, chamfer d = 0.2 × width. Points,
  counter-clockwise (x lateral, y forward): (h, a−d), (h−d, a), (−h+d, a),
  (−h, a−d), (−h, −b+d), (−h+d, −b), (h−d, −b), (h, −b+d). Points 0–3 are the
  front half;
- bounding radius = max point distance.

Walls [C, `0x1FD368`, GTRW wall points]:
- Each wall polyline is a **closed loop** (the point holding −count links
  back to the first). Only (x, z) are used.
- A wall is **one-sided**: the track is on the left of the polyline
  direction. Only points moving from the left side to the right side are
  stopped.
- Point flag bit 0 marks a "corner" vertex: it is also tested against the
  car's edges.
- Point flag bit 1 marks a **sensor** wall: crossing it only records an
  event, with no response.
- Candidate segments come from the GTRW wall grid (GTRW+0x4C) [C, `0x18FD00` →
  `0x228848`, read 2026-10-25]: the box of the body's 16 start- and end-world points
  (the push-out: end points 0–6 only), in world x/z, grown by 8 m on every side; each
  bound's cell by the breakpoint search `0x2273F0` (the last breakpoint ≤ the value)
  clamped into the grid (`0x227558`/`0x227578`: a box off the grid still visits the
  border cells); cells row by row (z), then column (x); in each cell its groups {u16
  count, u16 segment, u16 wall point ids} in stored order, a group taken when its
  segment is the body's road segment hint, or in the hint segment's neighbour list
  (+0x16 count, +0x18 ids; one hop), or the hint is −1. Each id is the segment from that
  wall point to the next of its loop. Every wall point is listed once, in the cell that
  holds it, so a segment is a candidate when its first point's cell is. No height test.
- The **road segment hint** (body +0x474, copied from F+0x3EC at the copy-in) is the
  segment of the highest road triangle under the front-left hub at the end of the last
  tick (the wheel ground query `0x1E8B68` → `0x2280C8`: the triangles of the hub's road
  grid cell (GTRW+0x48, no clamp: off the grid, none) whose group passes the same filter
  with the hint before, the barycentric u, v strictly inside (u > 0, v > 0, u + v < 1),
  the strictly highest from −FLT_MAX; −1 when none). Placement seeds it (`0x1DD210`).
- **Pair links** [C, `0x228B68`]: the static separation and the pair sweep of two bodies
  with different hints go ahead only when the other's hint is in the first's neighbour
  list. A hint of −1 on the first body reads the words before the segment table as its
  list (a count at seg_off − 0x2A: 0 on most courses, so a car off the road mesh meets no
  car on it; thousands on others, which then always match); −1 on the second never
  matches; two −1 hints are equal.

Per step [C, `0x1FA8F8` (all bodies) / `0x1FACB8` (one body, walls only),
read 2026-10-25]:
1. Init per body (`0x1FB140`, dt = the car's own step car+0xEC0): start pose
   = committed pose, sweep time t = 0, end time 1, the AI flags (+0x434/+0x438)
   and the step's hit words (+0x43C wall, +0x440 car, +0x444 sensor, +0x448
   strength) cleared. The start-world points are the last step's end-world
   points. **The point flags are not cleared**: they carry over from step to
   step. `0x1FB1F0` sets the body's car-contact switch (+0x430; all-bodies mode:
   car+0x136E == 0, i.e. not a ghost; walls-only mode: 0) and clears bit 1 of every
   point flag when the switch goes from on to off.
2. At most **2 rounds in all bodies together**; each round:
   a. static separation (`0x1FCC98`) for every ordered pair (i, j), i ≠ j, both
      switched on (the pair's road segments connected, `0x228B68`; centres within
      the sum of the radii);
   b. per body (`0x1FB238`): car persistence (`0x1FBCE8`, switched-on bodies) then
      the wall re-projection pass (`0x1FCF48`);
   c. end pose of every body (`0x1FB268`): end = start + v·dt·(1 − t) for x, y,
      height and heading; end-world points; the swept box (+0x3D4..+0x3E8: the
      start/end centres inflated by the radius);
   d. per body in order: reset the hit record (`0x1FB308`), the wall sweep
      (`0x1FD720`; a hit earlier than the round's best so far becomes the event,
      flagged "wall"), then the pair sweep (`0x1FBF50`) against every other body
      (recorded on this body, see below), then this body's pair hit if earlier
      than the best (strict, so ties go to the lower body and to its wall hit);
   e. the sensor check `0x1FD9D0` (a sensor crossing before the event sets +0x444): its
      loop in the all-bodies step passes the body after the last one (its index variable
      has run to the count), so no car's +0x444 is set there; the walls-only step checks
      its body (with the hit time, or 2 when there is none);
   f. no event: the rounds end. Otherwise every body advances to the event's time
      (`0x1FB328`, linear in pose) and the event is answered: a point-wall impulse
      (`0x1FDEC0`, when the body's best wall hit is a point), a wall-corner impulse
      (`0x1FE3A0`) or the car-car response (`0x1FC5C0`).
3. Per body: the stuck assist (`0x1FB740`, with the time of the step's last
   answered hit, 1 if none, so in the all-bodies mode any car's early hit counts):
   if both horizontal speeds are below 1 m/s and that time is below 0.1 (float at
   0x3506B4), yaw rate += (steer input in % / 100) × 20 × dt and the end pose is
   recomputed, so a pinned car can still be turned; the end-of-step push-out
   (`0x1FDE08`); the commit (`0x1FB6D8`): the yaw rate clamped to ±20π (62.83)
   rad/s; the heading is **not** wrapped to [−π, π]: with n = int((|θ| + π)/2π), it
   is left unchanged while n < 8, and only reduced by n·2π once |θ| ≥ 15π [C,
   verified by execution]; then the impact effects (`0x1FB890`: sound, pad vibration,
   car+0x1474, and the strength +0x448 capped at 1).
So with several cars in a race, two events per tick are answered for all cars
together; later ones are left to the next tick and the push-out.

Point flags (point record +0x20, persistent): bit 0 = held on a wall (+0x2C the
wall point record, +0x30/+0x34 its unit tangent), bit 1 = a car vertex held on
another body's edge (+0x3C that body, +0x40 its edge). A point with **any** flag
bit is skipped by the wall sweep, the pair sweep and the push-out.

Wall sweep [C, `0x1FD720` → grid callback `0x1FD368` per candidate segment A → B,
plane frame, cross(u, v) = u.x·v.y − u.y·v.x]: for each unflagged point (start S,
end E): s₁ = cross(B − A, E − A) < 0 (strictly outside at the end), s₀ = cross(B −
A, S − A) > 0 (strictly inside at the start), cross(E − S, S − A) ≥ 0 and cross(E −
S, S − B) ≤ 0 (the motion passes between A and B); t = s₀ / (s₀ − s₁) (1 if the
denominator is 0); kept per point if earlier, with the segment and its unit
tangent ((1, 0) for a zero-length segment). If A is a corner vertex (A's flag bit
0), for each car edge P → Q: cross(Qe − Pe, A − Pe) > 0 (inside at the end),
cross(Qs − Ps, A − Ps) < 0 (outside at the start), and with dP = Pe − Ps, dQ = Qe
− Qs, E₀ = Qs − Ps, f₂ = cross(dP, A − Ps), f₃ = cross(dQ, A − Qs): f₂ ≥ 0 and f₃ ≤
0, plus f₂ ≤ 0 and f₃ ≤ 0 when cross(dP, E₀) < 0, f₂ ≥ 0 and f₃ ≥ 0 when cross(dQ,
E₀) < 0; t = f₁₄ / (f₁₄ − f₁₇) of the two crosses; kept per edge if earlier. A
sensor segment (A's flag bit 1) only keeps the earliest sensor time (+0x404,
+0x400). The body's wall hit is the earliest of point k then edge k for k = 0..7
(strict); its time t_s + (1 − t_s)·f goes to +0x3FC. A point hit sets +0x3F4, the
point (+0x41C), the segment (+0x428) and the **body tangent** +0x420 (the one the
re-projection pass projects on); a corner hit sets +0x3F8 and the edge (+0x42C).
Either sets +0x43C; a point hit also sets the **AI flag** of its end (points 0–3
front, 4–7 rear) at once, whether or not this hit is answered. A corner hit sets
none: the side test there reads the hit index (the edge + 8), never below 8.

Pair sweep recording [C, `0x1FBF50`]: only switched-on bodies in connected
segments whose swept boxes overlap. The best hit (as below) is stored on body A
(+0x3F0 = 1, time +0x3FC, edge +0x408, other body +0x40C, vertex +0x410, crossing
point +0x414) unless A already holds a pair or point-wall hit (+0x3F0 or +0x3F4)
that is strictly earlier (a wall-corner hit +0x3F8 does not count: the pair hit
replaces its time, while the round still answers the corner hit it already chose);
it also sets +0x440 on both bodies and the AI flags (A by the edge, the other by the
vertex) at once. B's vertices with any flag bit (wall or car) are skipped.

Impulse response, car point hits wall [C, `0x1FDEC0`, `0x1FE100`, `0x1FE660`, `0x1FE288`]:
- r = the point relative to the CoG (rotated local point at the advanced pose),
  n = the body tangent (+0x420) turned left, (−t_y, t_x): the wall normal into
  the track; contact velocity v_p = v + ω × r.
- K = 1/m + (1/I)·(r × N)²; **j = −(1 + e)·(v_p·N)/K with restitution
  e = 0.05** (not clamped for separating contact); J = j·N.
- Friction impulse opposing the tangential contact velocity (3-D,
  including vertical velocity): magnitude = min(**μ = 0.5** × |J|, m × |v_t|).
- v += (J + F_friction)/m. Vertical velocity changes only through friction
  and never crosses zero. ω += (1/I)·(r × J); friction adds no torque.
- Position (`0x1FE288`): if the point's start-world position P is past the wall
  line (cross(P − A, t) > 0), the body moves by A + ((P − A)·t / |t|²)·t − P (onto the
  line).
- The point gets flag bit 0. The strength |j|/1e5 is added to +0x448 for sound,
  vibration and the HUD.

Wall corner hits car edge [C, `0x1FE3A0`, `0x1FE578`]: the same formulas with r = the
wall vertex relative to the CoG and N = the car edge's outward normal rotated to the
world; no position push, no flag.

Ground-query misses (no road under a wheel): the road-bump term is zeroed
and the previous ground height is kept. The car is not stopped by this;
walls do the containment. [C]

Car–car collision (all-bodies mode only; time trial has one car) [C,
the full step `0x1FA8F8` reproduced bit-identically by the diff harness on
119k samples, `tools/diffharness/diff_physics_collision.cpp`]:

The step sweeps from the committed pose (end = start + velocity × dt ×
(1 − t)). It runs **at most 2 rounds**; each round:
1. **Static separation** (`0x1FCC98`, both orderings): a vertex of one body
   that is inside the other's outline is pushed out along the line of
   centres. The deepest vertex decides, and each body moves half of the
   vector to the boundary.
2. **Contact persistence** (`0x1FBCE8`, every round, for each vertex flagged by
   an earlier response of this step or of an earlier step, against the edge it hit):
   - if the vertex has left the edge's span, the flag drops;
   - if it is outside the edge line, the flag drops and the poses do not move (the
     velocity rule below still runs);
   - if it is on or inside the line, each body moves half way onto the line;
   - then, whenever the centres approach through the edge (v_rel × edge
     < 0, using centre velocities), the relative normal velocity is removed,
     half from each body (**not** mass-weighted).
3. End pose (`0x1FB268`).
4. **Pair sweep** (`0x1FBF50`): in A's frame (rotated by −heading, at the
   start and end poses) each vertex of B moves along a **straight chord**
   between its start and end positions. This is not the true path of
   rotating bodies: compared with the true first overlap, the contact time
   differs in about 16–18 % of contacts, by up to 0.25–0.75 of a step when
   the cars spin. Reproduce the chord. It hits
   A's edge when it crosses from outside (n·q + d ≥ 0) to inside (≤ 0) within
   the edge's span, at t = t_start + (1 − t_start)·d₀/(d₀ − d₁). Both
   orderings are swept; the earliest hit wins, ties go to body 0. Vertices
   with flag bit 1 are skipped.
5. Advance all bodies to the hit time (`0x1FB328`), linear in position and
   heading.
6. **Response** (`0x1FC5C0`, impulse `0x1FC840`):
   - n = A's edge normal; the contact point is the crossing vertex itself;
   - K = 1/m_A + 1/m_B + (r_A × n)²/I_A + (r_B × n)²/I_B;
   - **j = −1.5·(v_rel·n)/K** (v_rel = v_A,pt − v_B,pt); +J on A, −J on B,
     linear and angular;
   - no friction, and no clamp for separating contact;
   - B's vertex gets flag bit 1;
   - the AI's recovery flags (body +0x434 front, +0x438 rear; cleared at the
     start of each collision step, `0x1FB1A0`): the edge body by its edge
     (0-2 front, 4-6 rear), the vertex body by its vertex (0-3 front, 4-7
     rear); a wall hit by its octagon point (0-3 front, 4-7 rear) [C,
     2026-10-20; ai.md §7];
   - touch-up (`0x1FCB20`): if a gap g remains along n, each body moves g/2
     towards the other (positions only).

After the second response the end pose is **not** recomputed (only
`0x1FB740` does that, when |vx| < 1 and |vy| < 1 m/s and the last hit time
t < **0.1** (float at 0x3506B4); it also applies the stuck-steer yaw nudge
then). So the committed
position ignores the second impulse.

A vertex lying exactly on an edge line (as right after a response or a separation)
is decided by single-precision rounding: the sweep's "starts outside?" test
(d₀ = (x·n_x + y·n_y) + d ≥ 0, chopped) then reads d₀ of ±1e-7…1e-5, and when it reads
≥ 0 the vertex is an immediate t = 0 hit with a full impulse (1.5 % of deeply
overlapping starts), or a pulling one of about −0.5·j right after a response. The
engine reproduces this by running the whole step in the program's arithmetic
(below), with the separation shifts applied by chopped adds to the stored world points.

Arithmetic [C, 2026-10-25]: every step function runs in the EE's single precision:
add, subtract and multiply chopped (the adder aligns with one guard bit and no sticky
bit), multiply-add with the product chopped first, divide, square root and reciprocal
square root rounded to nearest, no denormals (they read as zero), exponent 255 an
ordinary exponent. The responses and the pair sweep take sin and cos from the program's
`sinf` (`0x26A5F0`) and `cosf` (`0x26A1A0`); the start- and end-world points from its
`sincosf` (`0x26A4D0`, its own reduction: it can differ from the separate pair in the last
bit). Constants: chamfer 0.2 per metre of width (0x35069C); π, 2π, −2π for the heading
(0x3506A0–A8); ±20π rad/s (0x3506AC/B0); 0.1 for the stuck assist (0x3506B4); 15000 for
the car-car strength (0x3506BC); −1.05 and 1e5 for the point-wall (0x3506C0/C4) and the
wall-corner (0x3506C8/CC) impulse and strength. The order of operations of each routine
is in `src/sim/collision_exact.cpp`. [Verified bit for bit, every field of every body
record: 16,080 race steps with walls and car contact and 32,160 walls-only steps
(`race_collide_exact`), 4,000 steps on synthetic courses with corners and sensors
(`model_collide_exact_step`), the body build (`model_collide_exact_build`), the grid walk,
the pair links and the road segment query on ten courses (`model_collide_course`).]

Wall re-projection pass [C, `0x1FCF48` called from `0x1FB238` at the start
of every round of every step, after the car persistence `0x1FBCE8`;
execution-verified 2026-10-11, body effects read 2026-10-14, re-read 2026-10-25]: each
car point flagged as touching a wall (point +0x20 bit 0), at its **stored
start-world position** P (+0x10; the pass's own moves do not refresh it), is
re-projected on its segment, using wall point A at +0x2C and unit tangent t at
+0x30/+0x34. If it has slid past either end (s = (P − A)·t < 0 or > the length) it
moves once to the neighbouring segment of the closed polyline (the first point
links back to the last, the last to the first), and the unit tangent is recomputed
as (dx, −dz)/√(dx² + dz²) (a zero-length segment gives (1, 0)). With n the normal
into the track:
- the flag drops if the centre velocity points away (v·n > 0) or the point is
  on or inside the track side ((P − A)·n ≥ 0);
- otherwise the whole body moves perpendicular so that the point lies on the
  wall line, the velocity becomes (v·u)u with u the body's last point–wall
  hit tangent (body +0x420; no friction, no restitution), and the yaw rate
  gets the angular part of the impulse m(v' − v) applied at the point
  (r = the point before the move minus the new centre, I = 10·I_z).
The flag is set by the point–wall impulse and by the end-of-step push-out
when its correction is at most 0.5 m (wall-corner hits set none). Any
flagged point (wall bit 0 or car bit 1) is skipped by the wall crossing
test, the earliest-hit search and the push-out. No strength is added, and
nothing else reads the contact record for forces.

End-of-step push-out [C, `0x1FDE08` → grid callback `0x1FDA10` per segment A → B
over the end points' box, end pose only, no velocity change, read 2026-10-25]: for
the 16 values a = 0..15, k = a & 7, skipping flagged points: P = point k's end
position, P' = its neighbour's (point k − 1 for a < 8, k + 1 after); the body moves
only if P is strictly outside (cross(B − A, P − A) < 0), P' strictly inside, and the
car edge P' → P crosses the segment's line between A and B (cross(P − P', P' − A) ≥ 0,
cross(P − P', P' − B) ≤ 0). **No condition on where the point started**: a car
pushed partly through a wall (by another car) is pushed back. With D = B − A and
λ = (P − A)·D/|D|²: for λ < 0 (λ > 1) the body moves by A − (P' + μ(P − P')) (B
likewise), μ the projection of A (B) on the car edge line, so the wall's end lands
on that line; inside the span the correction C = λD − (P − A) is applied when
|C| ≤ 0.5 m (and point k gets flag bit 0 with this segment and its unit tangent),
otherwise the body moves along the wall by (1 − λ)·D for a < 8 and −λ·D after.
The end-world points are recomputed after each move.

Open details: who consumes the sensor-wall event time (probably lap/pit
logic); how the steer byte the stuck assist reads (car+0x14A7, signed percent)
is computed from the pad.

## 6. Car spec fields and their consumers

### 6.1 Pipeline [C]

1. `0x231DB0` fills a `CarEquipments` (EQ) object from the car record: part
   ids, plus default user settings (gear ratios, final drive, brake bias,
   boost, suspension, LSD, aids).
2. `0x1DA308` clears the 0x3E2-byte spec, runs the builder `0x2325B8` (one
   helper per parts table), then applies fix-ups (§6.3).
3. `0x1DBFD8` (P = car+0x10) converts the spec into floats. Sub-builders:
   engine `0x1E10E8` (E = car+0x148), drivetrain `0x1E1C98` (car+0x208),
   tire records `0x1DB9E0`/`0x1DBC50`/`0x1DBE40`/`0x1E8670` (6 records of
   0x1B4 bytes from P+0x398), springs/dampers `0x1DB6B8`/`0x1DB890` (per
   axle, P+0x2B8 + 0x44·axle).

"interp(steps, min, max, set)" = `0x233F60`: steps < 2 ? min :
min + (max − min)·(set − 1)/(steps − 1) (integer division, set is 1-based).
Percentage multipliers are integer: v = v·b/100 (truncated).

### 6.2 Spec layout (0x3E2 bytes) [C, read in code and reproduced by
running the original builder in an interpreter for the R34 GT-R]

Abbreviations: sus = suspension record (table 18), EQ = CarEquipments
settings word at that offset.

| S off | type | source | notes |
|---|---|---|---|
| 0x0F | u8 | const 9 | |
| 0x10 | u8 | const 2 (fn_001DA308) | |
| 0x12 | u8 | const 1 (fn_001DA308) | tire compound mode: 1 = 6 independent compound blocks, 0 = 3 shared by axle |
| 0x16 | u16 | (int)(body f32[0x44] * 10000) | R34: 19999 (body value 2.0) |
| 0x1A | u16 | chassis +0x14 (length mm) | |
| 0x1C,0x1E,0x20,0x22 | u16 | (int) body f32 +0x20,+0x24,+0x28,+0x2C (truncate) | mm; 0x1C,0x1E default length/2 if 0 |
| 0x24 | u16 | chassis +0x18 wheelbase mm | |
| 0x26 | u16 | body +0x32 front tread mm | |
| 0x28 | u16 | body +0x34 rear tread mm | |
| 0x2A | s16 | chassis +0x1A weight kg * lightweight(+0x12)/1000 * body(+0x30)/1000 | |
| 0x2C | u8 | chassis +0x10 front weight % | |
| 0x2D | u8 | 1 if body record stage (+0x10) nonzero (fn_00234578) | |
| 0x2E | u8 | chassis +0x1C * lightweight(+0x11)/100 * body(+0x38)/100 | 0x2F copy, 0x30 = half |
| 0x31 | u8 | chassis +0x1D | |
| 0x32 | u8 | body +0x39 | |
| 0x33,0x34 | u8 | EQ +0x124, +0x128 | |
| 0x35 | u8 | steer +0x11 | |
| 0x36 | u8 | (chassis +0x12 + 124) & 0xFF | = byte of u16 (1000+d) -> 100+d |
| 0x37 | u8 | (chassis +0x1E + 124) & 0xFF | same trick |
| 0x38,0x39 | u8 | body +0x3F, +0x40 | |
| 0x3A | u8 | EQ +0x15C | |
| 0x3B | u8 | interp(sus 0x48,0x49 / steps 0x47 / EQ +0x18C) | |
| 0x3C | u8 | sus +0x29 | |
| 0x3D | u8 | sus +0x5C | |
| 0x3E,0x40,0x42,0x44 | u8 | sus +0x70,+0x71,+0x72,+0x73 | |
| 0x3F | u8 | interp(sus 0x2C,0x2D / 0x2B / EQ +0x16C) | |
| 0x41 | u8 | interp(sus 0x2F,0x30 / 0x2B / EQ +0x170) | |
| 0x43 | u8 | interp(sus 0x33,0x34 / 0x32 / EQ +0x16C) | |
| 0x45 | u8 | interp(sus 0x36,0x37 / 0x32 / EQ +0x170) | |
| 0x46,0x47 | u8 | sus +0x78, +0x7A | |
| 0x4A | u8 | EQ +0x160 | |
| 0x4B | u8 | interp(sus 0x4C,0x4D / 0x4B / EQ +0x190) | |
| 0x4C | u8 | sus +0x2A | |
| 0x4D | u8 | sus +0x5D | |
| 0x4E,0x50,0x52,0x54 | u8 | sus +0x74..+0x77 | |
| 0x4F | u8 | interp(sus 0x3A,0x3B / 0x39 / EQ +0x17C) | |
| 0x51 | u8 | interp(sus 0x3D,0x3E / 0x39 / EQ +0x180) | |
| 0x53 | u8 | interp(sus 0x41,0x42 / 0x40 / EQ +0x184) | |
| 0x55 | u8 | interp(sus 0x44,0x45 / 0x40 / EQ +0x188) | |
| 0x56,0x57 | u8 | sus +0x79, +0x7B | |
| 0x5A,0x5B,0x5C,0x5D,0x60,0x61 | u8 | EQ +0x144,+0x148,+0x14C,+0x150,+0x154,+0x158 | ride height / spring etc. user settings |
| 0x5E,0x5F | u8 | EQ +0x164, +0x168 | |
| 0x62,0x63 | u8 | sus +0x5E, +0x5F | |
| 0x64..0x67 | u8 | sus +0x1D..+0x20 | |
| 0x68,0x69 | s8 | body +0x3D, +0x3E | tread offset (mm/2 each side) |
| 0x6A | u16 | drivetrain +0x20, x flywheel(+0x12)% x clutch(+0x12)% | |
| 0x6C | u16 | drivetrain +0x16, x flywheel(+0x11)%, clutch(+0x11)%, prop shaft(+0x11)% | |
| 0x6E | u8 | engine +0x44 | |
| 0x6F | u8 | engine +0x45 (+ eng balance/NA tune/turbo adders) | rev limit /100 |
| 0x70 | u8 | engine +0x46 (+ turbo adder) | |
| 0x71 | u8 | engine +0x57 | curve point count |
| 0x72..0x81 | u8[16] | engine +0x47..+0x56 (+ NA tune / balance +0x11 added to each) | rpm/100 |
| 0x82..0xA1 | u16[16] | engine +0x18..+0x36 | torque 0.01 kgf m |
| 0xA2 | u16 | engine +0x42 * 10, or EQ +0x1B8 | |
| 0xA4 | u16 | 1000 * part factors /100 .. | power factor per mille (high rpm) |
| 0xA6 | u8 | 100 * part factors /100 | second factor % (low rpm) |

The factors (and adders) of 0x6F/0x70/0x72../0xA4/0xA6 come from the builder's per-table helpers in
its call order: port polish (7), engine balance (8), displacement (9), computer (10), NA tune (11),
turbo kit (12), **port polish again** (`0x2337C0` is called twice), muffler (20), intercooler (19).
So a port polish's +0x11/+0x12 percentages apply squared (102 % -> 1000·102/100·102/100 = 1040,
103 % -> 1060) and each step truncates in that order [C: diff harness `gt_display_power`, every
car with random tune-shop parts].
| 0xA7 | u8 | turbo kit +0x11 | |
| 0xAA..0xAF | u8 | EQ +0x130,+0x13C,+0x12C,+0x138,+0x134,+0x140 (0xAC=0x12C, 0xAA=0x130, 0xAE=0x134, 0xAD=0x138, 0xAB=0x13C, 0xAF=0x140) | boost settings |
| 0xB2 | u8 | drivetrain +0x14 drive type (0 FR,1 FF,2 4WD,3 MR,4 RR); >=5 forced 0 by converter | |
| 0xB3 | u8 | drivetrain +0x15 | AWD system |
| 0xB4 | u8 | drivetrain +0x13 | front torque share |
| 0xB6..0xC5 | s16[8] | EQ +0xF0 + 4i (i=0 reverse, 1.. forward gears) x1000; slots after gear count = -1 | |
| 0xC6 | u16 | EQ +0x110 final drive x1000 | |
| 0xC8 | u8 | gear +0x11 gear count | |
| 0xC9 | u8 | engine +0x43 | |
| 0xCA | u8 | 0 | |
| 0xCB | u8 | EQ +0x114 | |
| 0xCC | u8 | clutch +0x15 | |
| 0xCE | u16 | drivetrain +0x1C x prop shaft(+0x14)% | |
| 0xD0 | u16 | drivetrain +0x1E x prop shaft(+0x15)% | |
| 0xD2 | u16 | drivetrain +0x18 x flywheel(+0x13)% x clutch(+0x13)% x prop(+0x12)% | |
| 0xD4 | u16 | drivetrain +0x1A x flywheel(+0x14)% x clutch(+0x14)% x prop(+0x13)% | |
| 0xD6,0xD7 | u8 | LSD +0x11, +0x1B | |
| 0xD8..0xDD | u8 | EQ +0x194,+0x198,+0x19C,+0x1A0,+0x1A4,+0x1A8 | LSD settings |
| 0xDE,0xDF,0xE0 | u8 | brake +0x11,+0x12,+0x13 | |
| 0xE2 | u8 | const 6 | steer curve B point count |
| 0xE3..0xEE | u8[12] | steer +0x1E..+0x29 | 6 x values then 6 y values |
| 0xEF | u8 | const 6 | steer curve C count |
| 0xF0..0xFB | u8[12] | steer +0x2A..+0x35 | x6 then y6 |
| 0xFC | u8 | const 6 | steer curve A count |
| 0xFD..0x108 | u8[12] | steer +0x12..+0x1D | x6 then y6 |
| 0x109 | u8 | ASCC flags: (a+0x17&3) | (a+0x16&3)<<2 | (a+0x1D&3)<<4 | (a+0x1C&3)<<6 | zeroed in some modes |
| 0x10A,0x10B | u8 | brake ctrl: interp(+0x12,+0x13/+0x11 / EQ+0x11C), interp(+0x16,+0x17/+0x15 / EQ+0x120) | front/rear brake bias % |
| 0x10C | u8 | ASCC interp(+0x14,+0x15 / +0x11 / EQ+0x1BC) | |
| 0x10D | u8 | ASCC +0x13 | |
| 0x10E | u8 | TCSC +0x14 | |
| 0x10F | u8 | TCSC +0x17 | |
| 0x110 | u8 | TCSC +0x13 (zeroed if S[0x117]==100) | |
| 0x111 | u8 | ASCC interp(+0x19,+0x1A / +0x18 / EQ+0x1C0) | |
| 0x112,0x113 | u8 | default 100 if 0 (set in fn_001DA308) | |
| 0x114 | u8 | ASCC interp(+0x1F,+0x20 / +0x1E / EQ+0x1C4) | |
| 0x115 | u8 | ASCC +0x22 | |
| 0x116 | u8 | ASCC interp(+0x24,+0x25 / +0x23 / EQ+0x1C8) | |
| 0x117 | u8 | TCSC interp(+0x15,+0x16 / +0x12 / EQ+0x1B0) | 100 = off |
| 0x11A / 0x11B | u8 | tire size (table 25) +0x0A: front / rear (section width mm/5) | |
| 0x11C / 0x11D | u8 | tire size +0x08: front / rear (rim inches) | |
| 0x11E / 0x11F | u8 | tire size +0x09: front / rear (aspect/5) | |
| 0x120, 0x121 | u8 | const 4 | suspension angle curve point count (front, rear) |
| 0x122..0x125 / 0x126..0x129 | u8[4] | sus +0x60..0x63 / +0x68..0x6B | angle codes (front / rear) |
| 0x12A..0x12D / 0x12E..0x131 | u8[4] | sus +0x64..0x67 / +0x6C..0x6F | multiplier codes |
| 0x132..0x141 | u8[16] | defaults copied from a static table (0x33C368) | per-slot tire-block class 0..2 |
| 0x142 + 0x70*i, i=0..2 | 112 B | FRONT tire blocks i (compounds 1..3 of front tire record: +0x20/+0x28/+0x30 with force-vol +0x40/+0x48/+0x50) | see 2.1 |
| 0x292 + 0x70*i, i=0..2 | 112 B | REAR tire blocks | |

Tire blocks (spec+0x142 front, +0x292 rear): see §6.5.

### 6.3 Fix-ups after the builder [C, `0x1DA308`]

- Body extents front/rear (spec 0x1C/0x1E) default to length/2.
- Default steer curves (count 6 plus two 6-byte arrays from 0x352900/
  0x352908): curve A when its count (spec 0xFC) is 0, curve C when its last
  x byte (spec 0xF5) is 0. The builder always writes both counts (6), so in
  practice only an empty curve C gets the default. [C]
- Arcade/restricted mode (mode 2) zeroes all ASCC/TCSC bytes.
  Spec 0x112/0x113 default to 100. TCSC setting 100 means off.
- Surface → compound-slot map (spec 0x132..0x141) = static default
  (§3.6).
- Tire blocks: a default byte 0x2D (45) and default 6-point curves (static
  arrays 0x352910/0x352918) when empty; front block 0's wet-surface defaults
  and the copy of its surface bytes into the other blocks (§6.5).
- The car set-up `0x1DDAD0` then clears spec 0x109 (ASCC flags) when the
  race entry's ASM word (+0x20F0) is 0 and spec 0x110 (TCS gain) when its
  TCS word (+0x20F4) is 0, before the converter runs. [C]

### 6.4 Converter results (P = car+0x10) [C unless noted; R34 values in
brackets]

Chassis and mass:
- Wheelbase L = spec 0x24 × 0.001 m (P+0x18); front weight fraction
  wf = spec 0x2C × 0.01. CoG to front axle a = L·(1 − wf) (P+0x10), to rear
  axle b = L·wf (P+0x14); P+0x20 = 1 − wf, P+0x24 = wf. [2.665, 1.173,
  1.492]
- Half tracks: front (spec 0x26 − 2·spec 0x68) × 0.0005 m (P+0x28), rear
  likewise from 0x28/0x69 (P+0x2C). Mean track P+0x1C =
  2·(h_f·wf + h_r·(1 − wf)) with h the half tracks, and P+0x20 = 1 − wf:
  both computed in the program's software doubles (GCC fp-bit `__muldf3` /
  `__adddf3` / `__subdf3`, `__extendsfdf2` / `__truncdfsf2`) and rounded to
  single. P+0x54 = (b − a)/2 moves the extents P+0x00 / P+0x04 to the CoG.
  [C]
- **Mass = catalogue weight × lightweight × body factors + 120 kg** (the
  extra 120 kg = two bytes 65 + 55 of the static block at 0x2D28B0, probably
  driver + fuel [I]). P+0x48 = mass, P+0x4C = 1/mass, weight force = 9.8 ×
  mass. [1560 + 120 = 1680 kg]
- Yaw inertia-like constant: P+0x118 = spec 0x2E × 100 (6000 if 0), used as
  1/P+0x118 in the yaw integration [9500]. Pitch and roll equivalents:
  Y = spec 0x2F × 100 (4000 if 0), Z = spec 0x30 × 100 (2000 if 0; spec 0x30
  = spec 0x2E / 2, and spec 0x2F is a copy of 0x2E). P+0x120/0x124 =
  a·2π/Y, b·2π/Y (pitch lever scales), P+0x128/0x12C = half-track·2π/Z
  (roll lever scales). **So chassis byte +0x1C (×100) is the inertia
  parameter: the same value for yaw and pitch, and half of it for roll.**
  [C formulas; I units]
- Steering lock: P+0x58 = spec 0x35 (steer record +0x11) degrees → rad.

Engine: §2.1.

Steering curves (3 curves, 6 points each, from steer record bytes
+0x12..+0x35): x = byte × 1/3.6 (km/h → m/s), y = byte × 0.2° (rad). That
is speed-dependent steering angle tables. [C conversion; role of each curve
§4]

Camber and toe (verified on 40 perturbed specs) [C]:
- camber offset P+0xDFC/+0xE00 = −(spec 0x5A/0x5B × 0.1°) in rad. The
  setting comes from EQ+0x144/0x148; range 0..120 front, 0..100 rear.
- Cam table value at that offset is cached at P+0xE0C/+0xE10. Only the AI
  grip estimate reads it.
- toe P+0xE20/+0xE24 = atan2((spec 0x60/0x61 − 128) × 0.0005, tire
  radius).
- P+0xE04/+0xE08 = (signed spec 0x62/0x63) × π/180 (camber change per metre
  of travel) and P+0xE14/+0xE18 = spec 0x38/0x39 × 0.01 m (swing-arm
  length) are **visual only**. They give the drawn wheel camber and track
  change, and no force reads them.
- P+0xE1C (steer-coupled camber) is always 0.

Suspension angle (camber) curve per axle (4 points): x = (128 − code) ×
0.0698/32 rad, y = code / 200. [R34: x = −0.277, −0.074, 0, 0.279;
y = 0.82, 0.995, 1.0, 0.8] This is the Cam() table of §3.1. [C]

Tire width per axle: P+0x284 = spec 0x11A/0x11B × 5 mm. [0.245 m]

Brakes: brake torques per wheel at full pressure are P+0xE28 (front) =
spec 0xDE × 98 N·m, P+0xE2C (rear) = spec 0xDF × 98, and handbrake P+0xE30
= spec 0xE0 × 98 (rear wheels). Here P is the converter object; P+x =
car+0x10+x, so the aero coefficients P+0xE60/E64/E68 are at
car+0xE70/E74/E78. Brake bias per axle from spec 0x10A/0x10B (%).
[C; units I]

Race arguments the converter reads [C, `0x1DBFD8`; the race passes a2 = the
control source F+0x3E9, t0 = the race-rule block K+0xE340 (event record
+0x198..), t1 = the entry's block at +0x2104]:
- **Power cap**: rule block +3 × 10 = the event's power limit in PS (0 =
  none) goes to the engine builder: E+0x9B = 1, E+0xA0 = static byte 30 ×
  0.01 = 0.3 (the soft share), E+0x9C = PS × 9.8 × 716.2 × 2π / 60 /
  (1 − 0.3/2) W. Torque at ω is unchanged while T·ω ≤ (1 − 0.3)·E+0x9C and
  soft-limited above (`0x1E0C38`). For every car, not only CPU ones.
- **CPU axle grip**: P+0xDF0 / P+0xDF4 = (spec 0x36 / 0x37 +
  900) × 0.001 (the axle grip factor of §3.1); for a CPU car (control source 2) × rule block
  +4 + class / +8 + class percent (0 → 100), class = the driveline class
  P+0x31. (These are event record bytes 0x19C..0x1A3.)
- **Top speed** P+0xDF8 (§ ai.md 4.2) scales the torque by an entry
  condition factor (`0x1DAD28`) from the block's +4 / +8 words against the
  limits at 0x2D2B00: 1 − 0.03 below −300000, linear to 1 at 0, 1 up to
  500000, then down by 0.02 at 600000 (+4); 1 + 0.05 below −100000, linear
  to 1 at 0 (+8) [C; the block's meaning I: an engine-condition state]. Its
  resistance uses the tarmac rolling-resistance curve 0x2D2CE0 × the tarmac
  scale 0x2D2CF0[0] × (weight + downforce) + drag.

Aids (offsets from P+0xE7C) [C]: ASCC levels spec 0x10C, 0x10D, 0x111,
0x114, 0x115, 0x116 × 0.01; ASCC flag bits from spec 0x109; TCSC spec 0x10E
× 0.1, 0x10F × 0.01, 0x110 × 0.1; TCSC level = 1.0 (off) when spec 0x117 is
0 or 100, else spec 0x117 × 0.01.

Quirks of the original converter worth reproducing [C, ps-init, checked
on all 244 cars by running the original converter]:
- the second ride-height value (P+0x44) reads spec 0x5C again, not 0x5D;
- the clutch capacity reads spec 0xCD, which is never written (always 0),
  instead of 0xCC;
- each tire record's base grip ends up as 9.8 × D(0) with no load term, because the
  tires are built before the sprung loads exist: 9.8 for every compound but medium_evo,
  whose D(0) is 1.02 (`params_tire_base_grip_disc`: every car on every tyre part).

Unknown database bytes resolved by this mapping:
- steer table 2: +0x11 = max steering lock (degrees); +0x12..+0x35 = three
  6-point speed (km/h) → angle (0.2° units) curves.
- chassis +0x1C = yaw inertia parameter (×100); +0x1D → spec 0x31 = CoG
  height-like term (mm) used in load transfer (§4.2);
  chassis +0x12/+0x1E: only the low byte of (1000 + value) is kept.
- body f32 +0x20..+0x2C = body extents in mm (front and rear of the CoG,
  width, height), used for the collision outline (§5.3); body f32 +0x44 =
  frontal area in m² (§4.7); body byte +0x39 = drag coefficient × 100.
- suspension +0x60..+0x6F = camber curve codes (angle, multiplier).
- tire compound (table 27) +0x08 = mu × 100; the remaining bytes are the
  curve axes and values of §3 (A, B, C, D and the load-sensitivity table).
- tire force vol (table 26) = 16 per-surface grip percentages (the mu[s]
  of §3.1).
- drivetrain +0x20 = engine + flywheel inertia in 0.001 kg·m²; +0x16 =
  engine friction (§2.2).

### 6.5 Tire data: database → spec tire block → runtime record [C; verified on all 244 cars by running the original converter]

**Sources.** The front tire part (table 28) and rear tire part (table 29)
each list compound ids at +0x20 (4 × u64) and force-vol ids at +0x40
(4 × u64). The builder uses the first three of each: block k (k = 0..2) =
compound record k (table 27) + force-vol record k (table 26). There are 3
front blocks at spec+0x142 + 0x70·k and 3 rear blocks at spec+0x292 +
0x70·k.

**Spec tire block (0x70 bytes)**, with c = compound record and f = force-vol
record (offsets are record offsets):

| Block bytes | Content |
|---|---|
| [0x00] | c+0x08: base mu × 100 (0 → 100) |
| [0x01] | c+0x09: slip-angle range of curve A in degrees (0 → 90; default 0x2D = 45 if 0 after the builder) |
| [0x02] | constant 8: curve A point count |
| [0x03..0x0A] | c+0x12..+0x19: curve A x bytes |
| [0x0B..0x12] | c+0x1A..+0x21: curve A y bytes |
| [0x13] | constant 6: negative-side count n₁ of curves C/D |
| [0x14..0x19] | c+0x22..+0x27: negative-side slip x bytes |
| [0x1A..0x1F] | c+0x28..+0x2D: curve C negative-side y |
| [0x20..0x25] | c+0x2E..+0x33: curve D negative-side y |
| [0x26] | constant 6: positive-side count n₂ |
| [0x27..0x2C] | c+0x34..+0x39: positive-side slip x bytes |
| [0x2D..0x32] | c+0x3A..+0x3F: curve C positive-side y |
| [0x33..0x38] | c+0x40..+0x45: curve D positive-side y |
| [0x39] | constant 4: load-sensitivity point count |
| [0x3A..0x3D] | c+0x0A..+0x0D: load-sensitivity x bytes |
| [0x3E..0x41] | c+0x0E..+0x11: load-sensitivity y bytes |
| [0x4B..0x54] | f+0x08..+0x11: grip % for surfaces 0..9 |
| [0x57..0x59] | f+0x12..+0x14: grip % for surfaces 12..14 |
| [0x55], [0x56], [0x5A] | 0: surfaces 10, 11, 15 (so they use plain mu) |
| [0x5E] | constant 6: curve B count (6 + default curve bytes from 0x352910/0x352918 when 0) |
| [0x5F..0x64] | c+0x46..+0x4B: curve B x bytes |
| [0x65..0x6A] | c+0x4C..+0x51: curve B y bytes |
| [0x6B] | c+0x52: a record scalar (×0.01) [? use] |

Defaults: in block 0, bytes [0x57]/[0x58]/[0x59] (surfaces 12, 13, 14:
the wet variants of 0, 1, 9) default to [0x4B]/[0x4C]/[0x54] when 0. In every
other block (front 1, 2 and all three rear blocks), zero entries of
[0x4B..0x5A] are copied from front block 0. [C, `0x1DA308`]

**The converter overwrites some block bytes with 0 before building the
curves** (so the first point of each curve sits at the origin): [0x03],
[0x0B], [0x14], [0x1A], [0x27], [0x2D], [0x3A], [0x5F], [0x65] = 0, and
[0x20] = [0x33] (the two sides of curve D meet at slip 0).

**Which record is built** (`0x1DBFD8`): slot s → class c_s = spec
0x132 + s (≥ 3 counts as 0). The front record used on surface s is c_s; the
rear is c_s + 3. Only records actually referenced are built. With spec
0x12 = 1 (always set), record r uses block r (front 0..2, rear 3..5 → rear
blocks 0..2).

**Runtime record** (0x1B4 bytes, P+0x398 + 0x1B4·r), with B = block:

- mu = B[0] × 0.01 (1.45 for the "normal" compound).
- **Per-surface grip** mu[s] (record +0x08 + 4s) = mu × B[0x4B + s] × 0.01,
  or mu if that byte is 0. Exception: mu[11] = min(1.0, 1.25 / mu) (static
  block byte 0x2D293F = 125). Shipped force-vol values (%) per surface:
  100, 90, 60, 60, 60, 80, 80, 100, 100, 110, –, –, 80, 80, 80. This is the
  mu[s] of §3.1.
- **Curve A** (+0x48, lateral force vs slip angle): n = 8; θ_max =
  min(B[1] (90 if 0) × π/180, π/2); x_i = B[3+i] × θ_max / 255 (rad);
  y_i = B[0xB+i] / 200.
- **Curve B** (+0x98, second force component vs slip angle): n = 6;
  x_j = B[0x5F+j] × (π/2) / 255; y_j = B[0x65+j] / 200.
- **Curves C and D** (+0xD4 and +0x14C) share one slip-ratio axis of
  n₁ + n₂ − 1 = 11 points:
  - negative side, i = 0..5, stored in reverse order:
    x = −B[0x14+i]/255, C = B[0x1A+i]/200, D = B[0x20+i]/200;
  - positive side, j = 0..5, starting at the shared zero point:
    x = B[0x27+j]/255, C = B[0x2D+j]/200, D = B[0x33+j]/200.
  - C(slip ratio) is the longitudinal (drive/brake) force factor (§3.3);
    D(slip ratio) is the friction-circle limit. [C: consumers `0x1E7450`,
    `0x1E73C0`]
- **Optimum slips:** +0x140 = x of the first maximum of C on the negative
  (wheelspin) side, the drive optimum used by TCS and the shift veto;
  +0x144 = x of the first maximum of C on the positive (braking) side,
  the brake optimum used by ABS; +0x148 = that maximum value.
- **Load sensitivity L** (+0x188): n = 4; x_j = B[0x3A+j] × 196 N per metre
  of section width; y_j = B[0x3E+j] / 200. Evaluated at Fz / width
  (§3.1).
- Peak slip angle +0x94 = min(π/4, the first whole degree 0..45 that
  maximises curve A) [C, `0x1E8670`].
- +0x04 = B[0x6B] × 0.01 (wear rate factor, used by §3.7 [I]).
- +0x00 = 9.8 × D(0) (an init-order quirk: no load term): 9.8 for every compound but
  medium_evo, 9.8 × 1.02 (`params_tire_base_grip_disc`); not used by the force model as far
  as traced.

### 6.6 Default user settings (stock car, before tuning) [C, `0x231DB0` and its initialisers; all 56 R34 words match a capture from the original code]

Each setting = clamp(min, default, max) of three record bytes: max with
the minimum first, then min with the maximum (`0x235F18`; an empty range
gives its maximum). Index settings (brake controller, dampers, anti-roll,
TCS, ASCC) have the minimum 1. On the 244 stock cars the clamp changes
nothing, so their **default = the record's default byte**; with other parts
it binds: an "off" TCS or ASCC record with default 0 gives index 1, and
`evo6_wrc_99_special`'s LSD front decel default 0 becomes its minimum 5.
`sim::stock_settings` reads the default bytes; `sim::exact::stock_equipment`
applies the clamps. Settings stored as an index are 1-based. The spec builder
turns an index into a value with interp(steps, min, max, index) (§6.1):
index 1 gives the min byte, index = steps gives the max byte.

| Setting (EQ offset) | Default source (record table + byte) | R34 |
|---|---|---|
| gear ratios (0xF0 reverse, 0xF4.. gears 1–7, ×1000) | gear (17) u16 +0x12, +0x14..+0x20 (−999 = no gear) | 3280; 3827, 2360, 1685, 1312, 1000, 793 |
| final drive (0x110) | gear u16 +0x22 (range +0x26..+0x24) | 3545 |
| gearbox slider (0x114) | gear +0x29 (range +0x2A..+0x2B) | 0 |
| drivetrain split (0x118) | drivetrain (13) +0x13 (range +0x11..+0x12) | 20 |
| brake controller index front/rear (0x11C/0x120) | brake controller (1) +0x14 (steps +0x11/+0x15) | 1 / 1 |
| front/rear downforce (0x124/0x128) → spec 0x33/0x34 | body (5) +0x13 (range +0x11..+0x12) / +0x3C (range +0x3A..+0x3B) | 29 / 47 |
| turbo (0x12C..0x140) | turbo (12) +0x12..+0x17 raw | 2, 35, 125, 6, 55, 105 |
| camber-like pair (0x144/0x148) → spec 0x5A/0x5B | suspension (18) +0x13 / +0x16 | 0 / 0 |
| ride height front/rear (0x14C/0x150), mm → spec 0x5C/0x5D | suspension u16 +0x54 / +0x5A (ranges +0x50..+0x52 / +0x56..+0x58) | 130 / 130 |
| toe-like pair (0x154/0x158) → spec 0x60/0x61 | suspension +0x19 / +0x1C raw (128 = neutral [I]) | 128 / 128 |
| spring rate front/rear (0x15C/0x160), 0.1 kgf/mm → b[0] | suspension +0x23 / +0x26 (ranges +0x21..+0x22 / +0x24..+0x25) | 40 / 50 |
| motion ratio front/rear % (0x164/0x168) → c of §4.2 | suspension +0x27 / +0x28 raw | 95 / 95 |
| damper indices (0x16C..0x188) | suspension default bytes +0x2E, +0x31, +0x35, +0x38, +0x3C, +0x3F, +0x43, +0x46 (group steps +0x2B, +0x32, +0x39, +0x40) | all 1 |
| anti-roll indices (0x18C/0x190) | suspension +0x4A / +0x4E (steps +0x47 / +0x4B) | 1 / 1 |
| LSD (0x194..0x1A8) | LSD (21) default bytes +0x12, +0x1C, +0x15, +0x1F, +0x18, +0x22 | 0, 7, 0, 30, 0, 15 |
| TCSC index (0x1B0) | TCSC (22) +0x11 (steps +0x12) | 6 |
| ASCC indices (0x1BC..0x1C8) | ASCC (23) +0x12, +0x1B, +0x21, +0x26 | 11, 1, 1, 1 |

Then §6.2 builds the spec. For the suspension: b[0] = spring setting;
b[1] = interp(anti-roll range, index); damper force bytes b[5], b[7],
b[9], b[11] = interp(damper ranges, index) — **the front second-stage
dampers reuse the first-stage indices** (0x16C/0x170) [C, ps-init]; the
damper velocity bytes b[4], b[6], b[8], b[10] = suspension +0x70..+0x73
(rear +0x74..+0x77). Cars with an adjustable gearbox (gear +0x28 ≠ 0, 59
cars) have their ratios regenerated from the slider by `0x1E28A8` (§6.7). Arcade cars (table 35) [C, 2026-10-25]: `0x231DB0` looks the id up
in table 35 first (`0x231F60`); an arcade car gets the record's parts as they
are (a zero part stays empty), the same part setters, the record's base car id
as EQ+0x00 and EQ+0x10 = 3, then `0x234150` writes these settings from the
record's bytes over the setters' (u16 where marked): final drive 0x110 ← u16
+0x10C, slider 0x114 ← +0xEE, downforce 0x124/0x128 ← +0xF8/+0xF9, camber
0x144/0x148 ← +0xFA/+0xFB, ride height 0x14C/0x150 ← u16 +0x100/+0x102, toe
0x154/0x158 ← +0xFC/+0xFD, springs 0x15C/0x160 ← u16 +0x104/+0x106, dampers
0x16C..0x188 ← +0xF0..+0xF7, anti-roll 0x18C/0x190 ← +0xFE/+0xFF, LSD
0x194/0x19C/0x1A4/0x198/0x1A0/0x1A8 ← +0xE8..+0xED, TCSC 0x1B0 ← +0x108,
0x1B8 ← u16 +0x10A, ASCC 0x1BC ← +0x109. The gear ratios (from the setters'
slider), drivetrain split, brake controller, turbo, motion ratio and ASCC 2–4
words stay the setters'.

Event entries (table 31) [C, 2026-10-31, issue 112]: `0x231DB0` never reads table
31 (35, then 30). The race information builds an event slot's car with
`0x238550` (event, slot, EQ): the variant id from the event's entry list, then
the same `0x231C50` (parts as they are) and `0x234150` (the settings above), the
slot's colour (`0x235F30`) and CarEquipments +0x1E0 = 1. Its callers: the
opponent draw `RaceInformation::vf13` (0x179D70, every drawn slot), the licence
records menu (`0x132030`, slots 0 / 1) and a name label (`0x11E120`). So every
opponent and a licence test's car carry their record's settings, most visibly
+0x10A, the output factor (spec 0xA2): 247 of the 707 raced variants differ
from their engine's own (243 higher, up to ×3.0; e.g. `smr_a_corolla_wrc_99`
1000 → 1320). Checked on hardware recordings: with it, all 48 licence demos'
car records give our live car's spec (B-8's springs 24 / 27, IB-6 / IB-7's
camber 10 among them). The harness's `race_setup::build_entry` builds event cars
the same way (`0x2315D8` for the record, then `0x231C50` / `0x234150`); before
this it composed them from their base car with stock settings, so no test
covered it (the pool draw test stubs `0x238550`). `gt_grid_exact` now runs the
original's showroom and grids on them: 742 cars and 1492 grids equal to ours.
The original's `0x238550` → `0x1D93C0` → `0x12E670` on `Gjapan_n_0505`'s pool
gives our places; EQ +0x1B8 = 1250 / 1230 there, 348–352 PS (280–336 PS
without it). End to end, harness `gt_field_exact` runs GT Mode's own field
build (RaceGTmodeInformation 0x18F3E0, the event, its virtual 12 0x18F540: the
draw and `0x238550`) for all 297 GT Mode races, three draw states: 4,455 drawn
cars with our spec on every program (2,181 with an output factor above 1000).
The factors and settings bytes are the same in the US, PAL and Japanese
databases (725 variants); the regions differ in pools and in the events' AI
percentages (ai §9.2: Japan's arcade races 90–100 % where US / PAL have 85–95 %).

Course tyres [C, 2026-10-31, issue 112]: `RaceBase::vf40` (0x17CC80, every Race
class inherits it unchanged) runs over the race information's entries before the
race: the course category (`0x2389A0`, course record +8) 1 (wet): for each entry
`0x236798` (unless both tyre stages are 13..24, `0x236600`: the front stage s < 12
→ s + 13 through the jump table at 0x342260), then `0x236600`; 2 (dirt): `0x2366D8`
(unless both stages are 12, `0x2345A8`: stage 12), then `0x2345A8`. Both set the
tyres through `0x236668` (front and rear records of the stage for the car,
`0x233F98`; both or neither). The race's own spec build passes no tyre override
(0x1F377C's delay slot clears $t0). Replays and demos hold the fitted car;
without it the R-licence demos' inputs drive our live cars off the course in all
8 tests. Harness `race_course_tires_exact` runs vf40 over every car of tables
30 / 31 / 35 on a dirt, a wet and a road course: equal to `course_tires` on every
program. (The wet licences S-5, IB-1 and IB-2 finish their demos on the same time
with either set: their wet records drive the same.)

### 6.7 Computed gearboxes (59 cars with gear record +0x28 ≠ 0) [C; verified bit-exactly by running the original `0x1E28A8` in the diff-harness interpreter: 59/59 cars and 522/522 calls at 9 slider values]

After the default settings, `0x2369A8` builds the spec and calls the ratio
generator `0x1E28A8`. Its output overwrites the gear words EQ+0xF0..0x10C
(reverse, gears 1..n; final drive unchanged).

Inputs (nothing else is read: no torque curve, mass or drag):
- n = forward gear count (spec 0xC8);
- rev limit rpm = 100 × spec 0x6F (the raw byte);
- final drive F = spec 0xC6 × 0.001, computed in double precision and then
  rounded to float;
- the tire set (verified by execution over all 45 drive-type × AWD-system
  combinations): **front** tire bytes only for FF (drive type 1) and for
  4WD with AWD system 0. **Rear** for FR/MR/RR and for 4WD with any other
  system (1–7, 0xFE). All shipped adjustable 4WD cars (systems 1, 2, 4) use
  the rear set, e.g. zz2_00 and impreza_lm_race_00. Drive types ≥ 5 leave
  the selector undefined;
- tire radius R = rim (in) × 0.0254 × 0.5 + H, where H = W × aspect byte ×
  0.05 and W = 0.001 × 5 × width byte m. **Only on the rear path** a width
  byte of 0 becomes W = 0.010 m; on the front path W stays 0 (no sidewall).
  An aspect byte of 0 gives H = 0; a rim byte of 0 gives R = H;
- slider s (EQ+0x114, default = gear record +0x29, range +0x2A..+0x2B). The
  **slider × 10 is the design top speed in km/h** (zz2: 35 → 350 km/h);
- a step table (bytes at 0x2D43C8, one row of 6 per gear count; n ≤ 5 uses
  the n = 5 row): n = 5: 155, 165, 188, 190, 190, 200; n = 6: 165, 175, 190,
  192, 190, 200; n = 7: 170, 180, 192, 194, 190, 200.

Algorithm:
1. t = (10·s − 150)/250 (0 at 150 km/h, 1 at 400 km/h; not clamped).
2. v = 10·s × 0.277778 m/s. Top-gear ratio r = ((rpm × 0.10472) × R / v) / F,
   i.e. the rev limit is reached exactly at the design speed.
3. q₁ = 200/b₁ + t(200/b₀ − 200/b₁); q₂ = 200/b₃ + t(200/b₂ − 200/b₃);
   q₃ = 200/b₅ + t(200/b₄ − 200/b₅).
4. For i = n down to 1: if i = 1, r ×= q₃; gear_i = trunc(r × 1000) as s16;
   then r ×= q₁ and q₁ ×= q₂. (Steps widen geometrically towards first gear.)
5. Reverse = trunc((3·gear₁ + gear₂)/4), in integers.
6. Gear words above n are not written.

Exact arithmetic (needed for bit-exact matching; verified on 40,000
random specs, about 217,000 ratio words, 0 differences, plus 522/522 real
calls):
- constants are the float words 0.10472 = 0x3DD67750, 0.277778 =
  0x3E8E38E4, 0.001 = 0x3A83126F, 0.0254 = 0x3CD013A9, 0.05 = 0x3D4CCCCD;
- mul.s, add.s and sub.s **chop** toward zero (add/sub also drop the
  smaller operand's low bits beyond one guard bit);
- **div.s rounds to nearest-even.** This covers every division: the three
  200/b quotients, t, and the r chain;
- F = nearest float of (double)S[0xC6] × 0.001 (soft-float double);
- cvt.w.s truncates and saturates; the gear store keeps the low 16 bits;
- there are no fused multiply-adds or reciprocal steps;
- slider 0 gives a division by zero → float max → every gear word −1.
Ordinary IEEE arithmetic gets within one count. The chop/nearest split
matters only for exact matching (the diff harness).

Check, zz2_00 (n = 6, 9000 rpm, F = 4.176, R = 0.3268 m, slider 35):
reverse 2.920; gears 3.194, 2.101, 1.512, 1.144, 0.909, 0.758. ✓

## 7. Candidate functions for differential testing

ABI: GCC 2.95 EABI on R5900. Integer/pointer args go in a0–a3, t0–t3;
float args in f12, f13, f14, ... in order; integer return in v0, float
return in f0. "car" = vehicle record (conductor + 0xC8 + i·0x16DC);
F = car + 0xF84; wheel i = car + 0xFE4 + 0xA4·i. Leaf = no calls.

| # | Address | Args | Reads | Writes / returns | Callees | Our equivalent |
|---|---|---|---|---|---|---|
| 1 | `0x1D8A80` Euler → axes | a0, a1, a2 = float[3] out; f12 = roll, f13 = pitch, f14 = heading | – | a0 = forward, a1 = lateral, a2 = up (§1.3) | sinf, cosf | orientation build. **Tested** (`phys_euler_axes`) |
| 2 | `0x1D8FB8` table lookup | a0 = `{s16 n; float* x; float* y}`; f12 = x | table | f0 = clamped piecewise-linear y(x) | leaf | torque curve, tire curves. **Verified** |
| 3 | `0x1D8DD0` table build | a0 = table, others: arrays, count | – | fills the table object | leaf [I] | – |
| 4 | `0x1EA348` patch slip angle | a0 = float* fade out; f12 = vx, f13 = vy | – | f0 = atan2f(−vy, vx) if fade > 0 else 0; *a0 = fade (0 at ≤ 0.2778 m/s, 1 at ≥ 2.778 m/s, linear ×0.4 between) | atan2f, sqrtf/sqrt.s | slip angle. **Executed** by diff |
| 5 | `0x1EBF00` slipstream factor | f12 = x, f13 = y (follower in leader frame); **a1** = leader car (a0 unused) | leader F+0x488 (= car+0x140C, speed), car+0x18 (body width) | f0 = factor (§5.2) | leaf | slipstream. **Verified** |
| 6 | `0x1EBF68` relative-position code | f12 = centre distance, f13 = u, f14 = v, f15..f17 = extents | – | v0 = code byte (§5.1) | leaf | – (HUD/AI) |
| 7 | `0x1DB118` slipstream filter + aero | a0 = car | F+0x528, F+0x52C, F+0x488, car+0xEC0 (dt), car+0xE70/E74/E78, globals A 0x352924, B 0x352928, K_up 0x35292C, K_down 0x352930 | F+0x52C, F+0x4B8, F+0x4BC, F+0x4C0 | leaf | aero drag. **Verified bit for bit** (`race_tick_exact`) |
| 8 | `0x1ECFC8` dt update | a0 = car | F+0x524 | car+0xEC0 = F+0x524/60, F+0x4A0 = 60/F+0x524 | leaf | – **Verified** |
| 9 | `0x1EBDA0` overlap/closing speed | a0 = A, a1 = B, f12 = u, f13 = v, a2/a3 = float* out | F+0x3E9, F+0x534, F+0x44C (vel), F+0x524 | v0, *a2 = gap, *a3 = closing speed | leaf | – |

| 10 | `0x1E5B30` clutch slip factor | f12 = Δω (rad/s) | – | f0 = min(1 + max(|Δω| − 52.36, 0)/837.76, 1.125) (§2.4) | leaf | clutch |
| 11 | `0x1E6D00` rpm from ω | a0 = car | F+0x448 (ω), engine idle (car+0x148+0x8E) | F+0x490 s16 = max(int(ω·60/2π), idle − 1) | leaf | – |
| 12 | `0x1E32D8` TCS lateral factor | f12 = x, f13 = a, f14 = b | – | f0 = 1 if |x| ≤ b else max(0, 1 − (|x| − b)·a) | leaf | TCS |
| 13 | `0x1E6FE0` slip gain | a0 = 0 (braking) / 1 (driving); f12 = hub speed | static block 0x2D28B0 +0x86/+0x88/+0x8A | f0 = 0.5 + 0.5·min(|u|/27.78, 1) with shipped data | leaf | slip ratio |
| 14 | `0x1E7068` slip ratio | a0 = per-wheel slip struct (car+0x1274+0x24i), a1 = wheel (car+0xFE4+0xA4i), f12 = x (hub speed along wheel) | wheel +0x20 (wheel speed), +0x4C (gain speed); bytes 0x2D2934/35 | slip struct +0x10/+0x12 (s8 states); wheel +0x48 (gain), +0x68 (slip ratio) (§3.4) | `0x1E6FE0` | slip ratio |
| 15 | `0x1E8910` wheel steer angle | a0 = P (car+0x10), a1 = wheel i, f12 = steer δ, a2 = 0 | P+0x110/+0x114 (ratios), P+0xE20/+0xE24 (toe) | f0 = wheel angle (§4.4) | leaf | steering |
| 16 | `0x1E7A68` slip-angle fold | a0 = float* sign out, f12 = β | – | f0 = folded angle in [0, π/2], *a0 = ±1 (§3.2) | leaf | tire |
| 17 | `0x1E8DD0` damper curve | a0 = axle suspension block (P+0x2B8 + 0x44·axle), f12 = v | block +0x18..+0x2C | f0 = damper force (§4.2) | leaf (sqrt.s) | suspension |
| 18 | `0x1E8AE0` road-bump wave | a0 = mode, f12 = x, f13 = z, f14 = spatial frequency | – | f0 = w = sin(f·x) + cos(f·z); mode 1 returns w·|w| | sinf, cosf | surface bumps. **Verified** |
| 19 | `0x1D8C80` / `0x1D8C00` angle difference / wrap | f12, f13 = angles | – | f0 = wrap(a) − wrap(b) into [−π, π) (π itself → −π); the wrap adds or takes off one turn below −π / from π on, and up to ±3π; beyond, `0x1D8BA8` removes int((\|a\| + π)/2π) turns | `0x1D8BA8` | – **Exact port** (`sim::exact::angle_diff` / `angle_wrap`, bit for bit) |
| 20 | `0x233F60` settings interpolation | a0 unused, a1 = min, a2 = max, a3 = steps, t0 = setting | – | v0 = min + (max − min)(set − 1)/(steps − 1), integer | leaf | car spec build |
| 21 | `0x1F5EF0` driveline class | a0 = drive type, a1 = AWD system | – | v0: FF → 1; AWD → 1 (system 0), 3 (system 3), 2 (other); all other types → 0 | leaf | – **Verified** |
| 22 | `0x1E17F0` engine torque | a0 = car, f12 = throttle | E = car+0x148 (curve, +0x8C/+0x8E/+0x90, turbo params), F+0x448, F+0x43B, intake state | f0 = engine torque N·m (§2.2); updates limiter flag and turbo state | table lookups `0x1D8FB8`, turbo `0x1E0CD8` | engine torque |
| 23 | `0x1E10E8` torque table build (**verified on all 131 NA cars**; table {s16 n; float* x; float* y} at E+0x00, s16 rev limit rpm at E+0x8C, s16 idle rpm at E+0x8E) | a0 = E (car+0x148), a1 = spec, a2 = power cap (PS, 0 = none), a3 = 0 | spec 0x6C..0xAF | E block (§2.1) | `0x1D8DD0`, turbo `0x1E0E08`/`0x1E0ED8` | spec → curve |
| 24 | `0x1E4E80` gear for speed | a0 = car, f12 = speed (m/s) | Drv = car+0x208: +0x02 u8 N, +0x2E u16 UP rpm; car+0x264 + 4k = rad/s per m/s in gear k | v0 = 0 if speed < 0; else lowest k in 1..N−1 with speed < (UP − 500)·2π/60 / g_k; else N | leaf | AT gear from speed. **Verified** |
| 25 | `0x1E7C08` tire force (per car) | a0 = car | wheel +0x0C, +0x24, +0x4C, +0x54, +0x64, +0x78, +0x7C; tire records | wheel +0x80/+0x84; P+0x2F0+0x24i+0x20 | `0x1E7A68`, `0x1D8FB8`, `0x1D8C80` | tire force (needs a prepared car record) |

| 26 | `0x1FAE38` collision body build | a0 = **body record** (manager + 0xC + i·0x4F0), a1 = car index (→ +0x44C), a2 = course (→ +0x478), a3 = car (→ +0x450); f12..f18 = front extent, rear extent, width, 0.2·width, 0.2·width, mass, I_z | – | octagon points (+0x08 + 0x44k), edge normals (+0x228 + 0x2Ck + 0x10/0x14), radius (+0x388), 1/mass (+0x00), 1/(10·I_z) (+0x04) | leaf | wall collision. **Verified** |
| 27 | `0x1FE660` wall friction impulse | body, contact velocity, normal impulse  | body mass +0x488 | friction impulse vector | sqrt | wall response |
| 28 | `0x1FB628` / `0x1FB698` | f12 = angle / rate | – | `0x1FB628`: n = int((|a| + π)/2π); returns a unchanged if n < 8, else a ∓ n·2π. `0x1FB698`: clamp to ±20π | leaf | – **Verified** |
| 29 | `0x1FB420` world points from pose | a0 = body | body pose, local points | world point positions | sinf, cosf | – |

| 30 | `0x1E28A8` gear-ratio generator | a0 = spec, a1 = 10 × slider (km/h), a2 = s16[8] out, a3 = flag (≠ 0: writes spec 0xCA = 0) | spec 0x6F, 0xB2, 0xC6, 0xC8, 0x11A..0x11F; table 0x2D43C8 | out[0] = reverse, out[1..n] = gears (§6.7) | `0x1E2700`, `0x1E2670`, soft-float `__extendsfdf2`/`__muldf3`/`__truncdfsf2` | computed gearboxes. **Verified, 522/522** |

| 31 | `0x1E9640` longitudinal pitch terms | a0 = car, a1 = float* A, a2 = float* B | T_i+0x1C (cos δ), wheel +0x88/+0x8C, P+0x284 (width), P+0x130/+0x134 (kP), P+0x10/+0x14 | *a1 = A, *a2 = B (§4.3) | leaf | pitch/heave. **Verified 5000/5000** |
| 32 | `0x1E96F0` heave/pitch/roll update | a0 = car | F+0x3C4..0x3D0, wheel +0x74, P+0x120..0x12C, P+0x50, F+0x468..0x484, F+0x536 | pitch/roll angles and rates, world velocity, matrix (§4.3) | `0x1E9640`, `0x1E9528`, `0x1D8A80` | **Verified 5000/5000 (+4000 spawn-hold)** |
| 33 | `0x1E9998` bump-stop nudge | a0 = car | F+0x4DD, x_i inputs, P+0x18/+0x1C/+0x20, bump limits | F+0x8, F+0x468/+0x46C, F+0x454, F+0x4DD, x_i, wheel +0x44, matrix | atan2f, `0x1E9CE8`, `0x1E9010`, `0x1D8A80` | **Verified 5000/5000** |
| 34 | `0x1E91A8` terrain factor and x_i | a0 = car | F+0x5C, F+0x2C, wheel heights | F+0x3A4, F+0x3B4.., F+0x4DD | `0x1E9010`, `0x1E90A0` | **Verified 2000/2000** |
| 35 | `0x1EA4D8` ground frame | a0 = car | wheel +0x04 (ground heights), P+0x18/+0x1C/+0x20, F+0x470 | F+0x30..0x5C, F+0x494/+0x498 | atan2f, `0x1D8A80` | **Verified 2000/2000** |
| 36 | `0x1EA1E8` peak tire force | a0 = car (with parameter block and tire records) | §3.1 inputs | wheel +0x54 | `0x1EA1C0`, `0x1D8FB8`, `0x1E9E80` | **Verified 8000/8000 wheels** |
| 37 | `0x1E9218` / `0x1E92A8` visual camber / hub half-track | a0 = P, a1 = wheel, f12 = steer, f13 = travel | P+0xDFC.., P+0xE04.., P+0xE14.., half tracks | f0 | atan2f, cosf | visual. **Verified 4000/4000** |

| 38 | `0x1E90F8` spawn height offset | a0 = n (counter), a1 = int* mode | – | f0 = offset (m), *a1 = mode (§4.3) | leaf | spawn hold. **Verified** |

| 39 | `0x1FA8F8` all-bodies collision step | a0 = collision manager (conductor + 0xBBB4) | bodies (0x4F0 each at manager + 0xC + i·0x4F0: start pose +0x390, end +0x3A4, displacement +0x3B4, velocity +0x3C4, times +0x38C/+0x3A0, committed +0x454/+0x460/+0x46C/+0x470, points +0x08 + 0x44k, edges +0x228 + 0x2Ck) | committed poses and velocities | the functions below plus the wall passes | **Verified bit-identical, 119k samples** |
| 40 | `0x1FCC98` static separation | a0/a1 = the two bodies (see harness header) | outlines, poses | positions | leaf [I] | verified within #39 |
| 41 | `0x1FBCE8` contact persistence | bodies | flagged points, edges, centre velocities | positions, velocities, flags | leaf [I] | verified within #39 |
| 42 | `0x1FBF50` pair sweep | a0 = body A, a1 = other body index (see harness header) | start/end poses, points, edges | hit time, point, edge | sinf, cosf | verified within #39 |
| 43 | `0x1FC5C0` pair response | a0 = A (uses its hit record +0x408..+0x418), see harness header | masses, inertias, velocities | velocities, flags; calls impulse `0x1FC840` and touch-up `0x1FCB20` | – | **Verified** |

ABIs for #39–43 are documented exactly in the header of
`tools/diffharness/diff_physics_collision.cpp`.

| 44 | `0x1E8190` wear accumulation | a0 = car | cp+0x18/+0x24, dt, F+0x535, wheel +0x08/+0x24/+0x30/+0xA0, P+0xE6C/+0xE74, tire record +0x04 | wheel +0xA0, +0x5C | `0x1DB9B8` | **Verified 3000/3000** |
| 45 | `0x1E9E80` wear → grip | a0 = S (car+0xF84), a1 = P, a2 = cp | cp, F+0x535, wheel +0xA0/+0x24, record +0x34 | wheel +0xA0 (clamp), +0x58, +0x5D | `0x1DB9B8`, soft-float double | v0 = 1 if wear active. **Verified 3000/3000** |
| 46 | `0x1E8340` wear/pit flags | a0 = car | cp+0x18/+0x20, wheel +0xA0/+0x58, F+0x3E9, K+0x8000+0x37AC, conductor vf39 | F+0x535 bits 2, 3 | `0x1D9FE0` | **Verified 3000/3000** |
| 47 | `0x1DD110` fresh tire set | a0 = car | cp+0x00/+0x04/+0x18 | wheel +0x5D, +0x5C, +0xA0, +0x58, +0x94 | leaf | **Verified 1500/1500** |
| 48 | `0x1DEBB0` slip intensity (smoke/squeal) | a0 = car | wheel +0x08/+0x0C/+0x20/+0x24/+0x4C/+0x50/+0x68/+0x6C, tables built by `0x1DE830` | wheel +0x2A, +0x2C, +0x30, +0x34 | cosf, sinf, `0x1D8FB8` | **Verified 2000×4** (+0x30); +0x34 3840/3840 (`model_skid_pitch`, §8.3c) |
| 49 | `0x1E6928` brake heat | a0 = car | wheel +0x20/+0x90/+0x94, car+0xE38/+0x284, constants from `0x1E6878` | wheel +0x94 | leaf | **Verified 3000/3000** |
| 50 | `0x1DA708` / `0x1DA998` event → rule block → cp | a0 = block (K+0xE340), a1 = event context / a0 = cp, a1 = block, a2 = enable | event record bytes 0x198..0x1E7 / block bytes | block / cp | memset / `0x1DA968` | **Verified 1500/1500, 3000/3000** |
| 51 | `0x1EDB40` pit state machine | a0 = car, f12 = progress now, f13 = progress previous step | §8.6 fields | F+0x534/+0x535/+0x536, HUD message | state setters `0x1E3D08`/`0x1E3D60`/`0x1E3DA8`/`0x1E3BF0`, `0x1DD5F0`, `0x1DD110`, line-4 queries `0x1F2EC8`/`0x1F2EE8` | **Verified 17/17 scenarios** |

| 52 | `0x1E7AD8` low-speed hold force | a0 = wheel | wheel +0x0C, +0x20, +0x4C, +0x50, +0x54 | f0 = F_hold (§4.1) | sinf, cosf | **Verified 20000/20000** |
| 53 | `0x1E7848` forward step | a0 = car | §4.1 inputs | F+0x488, +0x4C4, +0x4D8, +0x394 | sinf, cosf | **Verified 20000/20000** |
| 54 | `0x1E7D90` lateral force and yaw | a0 = car, a1 = airborne flag | §4.1 inputs | F+0x474, +0x478, +0x47C, +0x4C8 | `0x1E7AD8`, `0x1DB9B8` | **Verified 20000/20000** |
| 55 | `0x1E8008` stop clamp / world velocity | a0 = car, a1 = airborne flag | §4.1 | F+0x44C/+0x450 etc. | leaf | **Verified 20000/20000** |
| 56 | `0x1E5B98` engine and axle integration | a0 = car | §2.4, §2.7 | engine speed, axle speeds, drive torques, flags | `0x1E17F0`, `0x1E5998`, `0x1E5B30` | **Verified 30000/30000** |
| 57 | `0x1E5768` wheel net torque | a0 = car | per-wheel tire force parts, brake torque, wheel speed | T_i+0xC | leaf | **Verified 10000/10000** |
| 58 | `0x1E57F8` torque vectoring | a0 = car | Drv+0x10/+0x14, F+0x41C, F+0x488, wheel speeds | T_i+0xC | leaf | **Verified 20000/20000** |
| 59 | `0x1E2700` layout/AWD builder | a0 = spec, a1..: out ptrs (mode, driven axle, k, cap) | spec 0xB2..0xB5 | outputs | leaf | **Verified 10000/10000** |
| 60 | `0x1E6D48` / `0x1E7500` / `0x1E77E0` clutch transitions, engine speed, gear-change link | a0 = car | §2.4 | F+0x437, +0x418, +0x448, +0x490, +0x43A | `0x1E6CA8`, `0x1E6D00`, `0x1E5600` | **Verified 10000/20000/600** |
| 61 | `0x1E5998` AWD code-5 coupling factor | a0 = car  | §2.7 | f0 = g or the full-transfer sentinel 98000 | leaf | **Verified 10000/10000** |
| 62 | `0x1DD8A8` moving-start state | a0 = car, f12 = v (m/s) | §9.3 | §9.3 | `0x1E4E80`, `0x1DD5C8`, `0x1E0ED8` | **Verified 244 cars × 2 speeds** |
| 63 | `0x1DD5F0` standing-start / pit reset state | a0 = car | – | §9.3 | `0x1DD5B8`, `0x1DD1A8`, `0x1E4E78` | **Verified** |

| 64 | `0x1EA6B0` ground-frame speeds, hub velocities | a0 = car | car+0xFC0 (t1[3]), car+0xFCC (t2[3]), car+0x13D0 (v[3]), P+0x10, +0x14, +0x28, +0x2C, F+0x47C | F+0x488, F+0x48C, W_i+0x4C, W_i+0x50 | `0x1EA5F8` | **Verified 20000/20000** (S1) |
| 65 | `0x1EA720` free-wheel speed | a0 = car | W_i+0x0C, +0x4C, +0x50, +0x20, +0x5F, car+0x218/0x219, car+0x28C/0x290 | W_i+0x20 (if W_i+0x5F == 0), car+0x13DC/0x13E0 | sinf, cosf | **Verified 20000/20000** (S2) |
| 66 | `0x1DAD28` engine-condition factor | a0 = 0x2D2B00 (parameter block), a1 = float[3] counters (word 0 not read, [1] = E, [2] = O) | block built by `0x1DAB50` from bytes 0x2D2950.. | f0 = f_E x f_O (S3) | `0x1DAB50` | **Verified 20000/20000** |
| 67 | `0x1D9C80` E counter / `0x1D9D78` O counter | a0 = counters, f12 = d (m) | static block | counters[1] / counters[2] updated (S3) | `0x1DAC70` / `0x1DAC98`, `0x1DACC8` | **Verified 20000/20000 each** |
| 68 | `0x1EA9B8` per-tick speed bookkeeping | a0 = car | S1 inputs, dt, car+0x137C.., F+0x3F0/0x3F4, F+0x4E0, car+0x16AC, car+0x16D8 | F+0x488/0x48C, W_i+0x4C/0x50, car+0x16A8, +0x16AC, +0x16D8, odometer, E, O, F+0x3F0, F+0x3F4, (S2 outputs) | `0x1EA6B0`, `0x1EA908`, `0x1DAD18`, `0x1D9C80`, `0x1D9D78`, `0x1EA870`, `0x1EA720` | **Verified 10000/10000** (odometer, E, O, |u|, max, speedo, F+0x3F0/3F4) |
| 69 | `0x1E0BC0` full-throttle torque with condition | a0 = engine block car+0x148 `{s16 n; float* rpm_axis; float* torque}`, a1 = counters (car+0x137C), f12 = omega | table, counters | f0 = `0x1D8E50`(table, omega) x `0x1DAD28` | `0x1D9B10`, `0x1D8E50`, `0x1DAD28` | **Verified 3000/3000** |
| 70 | `0x1E3068` ABS / ASCC dispatcher (dirt prelude) | a0 = car, a1 = aid block (E8C or EF8), a2 = ASCC enable | conductor byte K+0x8000+0x37AD (car+4 -> conductor), F+0x41C, W_i+0x90/0x54/0x48/0x68, tire records, P+0xE38..0xE48, bytes 0x2D2965..67 | W_i+0x90 | `0x1F2F90`, `0x1DB9B8`, `0x1E2D08` | **Verified 8000/8000** (dirt vs non-dirt, differential); bit-exact port `sim::exact::brakes` (race_tick_exact) |
| 71 | `0x1E8E58` wheel suspension step | a0 = F, a1 = wheel, a2 = axle block (P+0x2B8+0x44 k), a3 = wheel index, f12 = x | block, F+0x4A0, W+0x18 | W+0x18, +0x08, +0x1C, +0x70, +0x74; f0 = body force (S5) | `0x1E8DD0` | **Verified 20000/20000** |
| 72 | `0x1D9BB0` odometer add | a0 = u32*, f12 = metres | – | *a0 += trunc(x/2) saturating at 0xEE6B27A6; f0 = remainder (0 on saturation) | leaf | Verified inside #68 |
| 73 | `0x1D8E50` bisecting table lookup | a0 = `{s16 n; float* x; float* y}`; f12 = x | table | f0: clamped at both ends as #2; then, while ≥ 4 segments remain, bisect (mid = (lo + hi) / 2; x == x[mid] returns y[mid]); then the first k ≥ lo with x < x[k] (else hi): y[k−1] + (y[k] − y[k−1])(x − x[k−1]) / (x[k] − x[k−1]) | leaf | engine torque (#69). **Verified bit for bit** (`prep_leaves_exact`) |

Argument lists for entries 10–38 come from reading the code unless marked
verified. Confirm them
by execution before relying on them.

## 8. Tire wear, brake heat and pit stops

All functions were verified by executing the original code in the
diff-harness interpreter (counts given). Notation as before: wheel W =
car+0xFE4 + 0xA4·i, S = F = car+0xF84, P = car+0x10, K = the conductor,
cp = K+0x84 (wear parameter block).

### 8.1 Wear parameters per race [C, `0x1DA708` 1500/1500, `0x1DA998` 3000/3000]

- **Source.** Event record (table 32) bytes 0x1BD..0x1C3 are copied into the
  race-rule block (K+0xE340) at race creation. At race init they become ten
  floats cp:

| cp | Meaning | Formula (block bytes B) | Shipped value |
|---|---|---|---|
| +0x00 | wear of a fresh set (scrub-in start) | −B[0x2F] × 10000 | −20000 |
| +0x04 | grip loss at a fresh set | B[0x30] × 0.01 | 0.06 |
| +0x08 | knee 1 | B[0x31] × 10000 (B[0x31] forced to 4) | 40000 |
| +0x0C | loss at knee 1 | B[0x32] × 0.01 (forced 0) | 0 |
| +0x10 | knee 2 | B[0x33] × 10000 | 150000 |
| +0x14 | loss at knee 2 | B[0x34] × 0.01 | 0.05 |
| +0x18 | maximum wear; **0 = wear disabled** | B[0x2C] × 10000 | 210000 |
| +0x1C | loss at maximum | B[0x2D] × 0.01 | 0.30 |
| +0x20 | "tires worn" threshold on the grip factor | (100 − B[0x2E]) × 0.01 | 0.91 |
| +0x24 | cap on the slip input | static byte 0x2D293E / 100 | 1.0 |

- **Enable and ordering check.** cp is filled only when the mode enables it,
  B[0x2C] ≠ 0, and the ordering checks hold (B[0x33] < B[0x2C],
  B[0x34] < B[0x2D], B[0x31] < B[0x33], B[0x32] < B[0x34],
  B[0x2E] < B[0x2D]). Otherwise all fields are 0 and wear is off.
- **Who enables it:**
  - single player (GT mode) and free practice: from the event, unless the
    course category is dirt (course table 34 +0x08 = 2);
  - battle modes: a built-in preset (identical bytes) when the VS
    "tire damage" option is on;
  - time trial, license, free run, machine test: off.
- **Shipped events:** 178 of 454 have wear, all with the same bytes
  21, 30, 9, 2, 6, 15, 5. That covers all `Gxxx_h_` events, all endurance
  `E…` events and some others. So there is **one wear curve in the game**;
  the per-event choice is only on/off.

### 8.2 Wear accumulation [C, `0x1E8190`, 3000/3000]

Per step (phase 13), skipped entirely if wear is off, and also while the
car is on the pit-lane surface (§8.5). For each wheel with Fz ≠ 0 that is
not on surface 1 (kerb):

```
u      = max(0, 0.4·Fz/width_axle − 1960)          (load term)
s      = min(1, smoke_i)                          (slip intensity, §8.3)
rate   = compound byte +0x52 / 100                (tire record +0x04)
wear_i += ((s × rate) × u) × dt
```

- The −1960 and 0.4/width come from static bytes 20 and 60 × 98 N (P+0xE6C
  and P+0xE74 per axle).
- **Wear depends on slip, load and compound only.** There is no speed
  term and no temperature.
- Compound rates: f1 and ssoft 1.00; soft and arc_racing 0.60; normal,
  control and arc normals 0.40; mediums 0.30; medium and arc_d_racing 0.24;
  sports 0.20; mediumh 0.18; hard 0.12; shard 0.06; dirt and medium_evo 0.
- Example: R34 on normal tires, outer wheel at 6000 N in a full slide
  wears about 3100 units/s.
- A byte copy of s × rate × 255 is stored per wheel (wheel +0x5C; no reader).
  While wear is on, all four bytes are cleared first (also on the pit surface;
  kerb and zero-load wheels keep 0).

### 8.3 Slip intensity ("smoke") [C, `0x1DEBB0`, 2000×4/2000×4]

Computed every step in phase 12. It is also the skid sound/smoke channel.

- **Airborne wheels:** they keep their values for 5 more steps, then go to
  0. [C, 2026-10-20, read again from `0x1DEBB0`] More exactly, wheel +0x2A
  is a counter: 6 while the wheel carries load (+0x08 ≠ 0); on a step with
  no load it is decremented, and while it is still non-zero the three words
  are **recomputed** from that step's inputs (not kept); once it reaches 0
  they are +0x2C = 0, +0x30 = 0, +0x34 = 1.0. Ours keeps the last values for
  the 5 steps (the kerb formula gives 0 with no load either way; the other
  classes differ only by how the airborne wheel's lateral speed and slip
  ratio move).
- **Kerb surfaces 1 and 13:**
  smoke = clamp01((|v_r| − 1.389)·0.144) × clamp01(Fz/2450 − 0.25) × 0.5, v_r the rim speed (wheel +0x20, m/s).
- **Other surfaces, by class** (surfaces 2–5 → class 1; 6 and 12–14 →
  class 2; the rest → class 0):
  - inputs: lat = |cos(δ)·v_y + sin(δ)·v_x| (lateral speed of the wheel);
    A = curve1(lat) and B = curve2(slip ratio);
  - m = √(A² + B²), plus (ramp − r0)·g when ramp > r0. ramp is the
    wheel's speed ramp, wheel +0x6C = min(1, 0.02·|hub speed|) (0..1,
    dimensionless, §3.6), so r0 = 1/60, 1/180 and 1/18 correspond to hub
    speeds of 0.83, 0.28 and 2.78 m/s;
  - smoke = m + b when m ≠ 0.

| Class | curve1 (lateral m/s → value) | curve2 (slip ratio → value) | r0 | g | b |
|---|---|---|---|---|---|
| 0 | (1.111, 0) (9.556, 1.192) (12.22, 1.6) (15.0, 2.0) | (−1, 2.388) (−0.580, 2.388) (−0.314, 1.176) (−0.0353, 0) (0.0353, 0) (0.314, 0.784) (0.580, 1.592) (1, 1.592) | 0.01667 | 0 | 0 |
| 1 | (1.556, 0) (3.333, 0.0627) (13.11, 0.3639) (14.11, 0.4) | (−0.502, 0.2) (−0.439, 0.1757) (−0.1255, 0.0251) (−0.0471, 0) (0.0471, 0) (0.1098, 0.0408) (0.2353, 0.1757) (0.2667, 0.2) | 0.005556 | 0.5455 | 0.07 |
| 2 | (1.556, 0) (3.333, 0.0471) (13.11, 0.2729) (14.11, 0.3) | (−0.502, 0.4) (−0.439, 0.3514) (−0.1255, 0.0502) (−0.0471, 0) (0.0471, 0) (0.1098, 0.0612) (0.2353, 0.2635) (0.2667, 0.3) | 0.05556 | 0.24 | 0 |

On tarmac, the smoke value reaches 1 at a lateral wheel speed of about
8.2 m/s, or at a slip ratio of about +0.385 / −0.272.

### 8.3b Smoke-emission intensity (wheel +0x2C) [C, `0x1DEBB0` / `0x1DE830`, 3840/3840]

`0x1DE830` builds **three** curve sets from its static block (after `0x1DE1E0` has clamped the
block in place: each curve has 4 points, channel 0's first y of each half is 0, a speed
threshold is at most 50 / 3.6 m/s, channel 1's base is 50..200 and four threshold pairs are
kept ascending; four surface classes are built, the tick reads classes 0..2), and `0x1DEBB0`
writes one word per set: +0x30 is the slip intensity of §8.3 (wear, skid
sound), +0x34 a second channel, and **+0x2C is the value the race's tyre-smoke
/ dust emitter reads** (`0x1DF150` → the race smoke object, see
docs/dev/RACE.md "Tyre smoke"). It is also what the car-dirt accumulation
sums: over the wheels on class 1 and 2 surfaces, dirt (engine condition word
3, car+0x1388) += Σ value × 0.001 × dt, × 8 on courses of category 0 (or
0xFF), at most 0.8 (`0x1D9F38`). Same structure as §8.3, but with different
inputs and curves:

- inputs: lat = |cos(δ)·v_y + sin(δ)·v_x| (as §8.3) and the **slip speed**
  s = hub − rim (m/s), hub = cos(δ)·v_x − sin(δ)·v_y, rim = wheel +0x20,
  negated when hub < 0 (s < 0 is wheelspin, s > 0 lock-up). Not the slip
  ratio.
- m = √(curve1(lat)² + curve2(s)²), plus (ramp − r0)·g when ramp > r0;
  value = **min(1, m + b)** when m ≠ 0, else 0.
- Class by surface as in §8.3 (kerb 1 → class 0, kerb 13 → class 2; the
  kerb formula of §8.3 does not apply here).
- Airborne: the same 5-step counter as §8.3 (0 after it runs out).

| Class | curve1 (lateral m/s → value) | curve2 (slip speed m/s → value) | r0 | g | b |
|---|---|---|---|---|---|
| 0 | (11.556, 0) (12.889, 0.0471) (13.778, 0.1506) (16.333, 0.1776) | (−10, 0.1) (−8.889, 0.0878) (−8, 0.0125) (−7.556, 0) (8, 0) (8.444, 0.0204) (9.333, 0.0878) (10.444, 0.1) | 0 | 0 | 0.3 |
| 1 | (3.111, 0) (6.667, 0.0784) (26.222, 0.4549) (28.333, 0.5) | (−7.111, 0.5) (−6.222, 0.4392) (−1.778, 0.0627) (−0.667, 0) (0.667, 0) (1.556, 0.0408) (3.333, 0.1757) (3.778, 0.2) | 0.005556 | 0.7273 | 0 |
| 2 | (5.333, 0) (9.778, 0.0157) (26.222, 0.0910) (28.333, 0.1) | (−9.111, 0.1) (−7.556, 0.0878) (−6, 0.0125) (−4.667, 0) (3.111, 0) (4.222, 0.0204) (5.556, 0.0910) (7.778, 0.1) | 0.05556 | 0.2 | 0.2 |

Consequences: on tarmac a wheel emits nothing until it spins more than
7.56 m/s (27 km/h) faster than the road, locks 8 m/s slower, or slides 11.6
m/s sideways, and then emits at 0.30–0.50 (never 1). Typical launches (R34,
RX-7, Viper GTS from standstill, player or CPU clutch rule) emit no puffs at
all, although their §8.3 slip intensity reaches 0.3–1.2. On loose surfaces
(class 1) the ramp term alone gives up to 0.72 at speed: dust trails from
just driving. Class 2 (wet) gives a steady 0.2 + 0.19 spray when moving.
Implemented in `sim::exact::tyre_effects` (`VehicleTelemetry::smoke_emit`).

### 8.3c Skid pitch (wheel +0x34) [C, `0x1DEBB0` / `0x1DE830`, diff harness `model_skid_pitch` 3840/3840]

The second curve set: the **pitch factor of the tyre loops** (race_sound.md
§1; read by `0x1DE3B8` and its sibling per-surface routines). Same inputs
as §8.3: the input selector bytes of the static block (`0x2D35B8` + class ×
0x28, +0xA0 lateral input, +0xBF slip input) pick the lateral wheel speed
and the slip ratio, as +0x00 / +0x1F do for §8.3 (set 3's +0x140 / +0x15F
pick the slip speed). Built tables at `0x49CBC0` + class × 0x84 (curve1
header +0x00, curve2 +0x4C, b +0x58, r0 +0x5C, g +0x60).

- **Kerb surfaces 1 and 13:** a ramp on |rim speed| (+0x20): 0.5 up to
  2.778 m/s (10 km/h), 4.3 from 55.556 m/s (200 km/h), linear between
  (table `0x49CFC4`..`0x49CFD0`, built from bytes `0x2D37B9`..`0x2D37BC`;
  `0x49CFC0` = 0.5 is §8.3's kerb factor).
- **Other surfaces, by class** (as §8.3): p = (b + √(A² + B²)) + (ramp − r0)·g
  when ramp > r0. Unlike §8.3, **b is added always**, also when A = B = 0:
  a tarmac wheel rolling straight sits at 0.75, loose and wet at 0.5.
- **Airborne:** recomputed while the counter of §8.3 runs, 1.0 after it.

| Class | curve1 (lateral m/s → value) | curve2 (slip ratio → value) | r0 | g | b |
|---|---|---|---|---|---|
| 0 | (0.4444, 0) (3.3333, 0.1098) (6.0, 0.2275) (9.2222, 0.3510) | (−1, 0.4612) (−0.6588, 0.2886) (−0.2510, 0.1129) (−0.0157, 0) (0, 0) (0.2039, 0.0878) (0.7216, 0.3263) (1, 0.4612) | 0.01667 | 0 | 0.75 |
| 1 | (1.1111, 0) (6.8889, 0.4706) (18.0, 1.5373) (28.333, 1.5922) | (−0.3451, 0.8) (−0.2980, 0.7027) (−0.1098, 0.1004) (−0.0471, 0) (0.0471, 0) (0.0784, 0.1631) (0.1882, 0.7027) (0.2824, 0.8) | 0.05556 | 0.2 | 0.5 |
| 2 | (1.5556, 0) (5.1111, 0.1506) (11.778, 0.3012) (28.333, 0.2933) | (−0.5020, 0.3) (−0.4392, 0.2635) (−0.1255, 0.0376) (−0.0471, 0) (0.0471, 0) (0.1098, 0.0612) (0.2353, 0.2635) (0.2667, 0.3) | 0.16667 | 0.3857 | 0.5 |

(Exact floats in `sim::exact::tyre_effects`.) The class-2 ramp term reaches 0.15 at
100 km/h; an earlier reading of the sound tables (0.10) was wrong.
Implemented in `sim::exact::tyre_effects` (`VehicleTelemetry::skid_pitch`).

### 8.4 Wear → grip [C, `0x1E9E80`, 3000/3000]

Runs in phase 2 (through the peak-force code of §3.1) when wear is on and the
car is not on the pit surface. Per wheel, with w = wear:

| w | grip factor wear_grip | HUD level |
|---|---|---|
| w ≥ max | w := max (clamped); 1 − loss_max | 127 |
| knee 2 ≤ w < max | linear from 1 − loss₂ to 1 − loss_max | 64..127 |
| knee 1 ≤ w < knee 2 | linear from 1 − loss₁ to 1 − loss₂ | 32..64 |
| 0 ≤ w < knee 1 | 1 − loss₁·w/knee 1 | 0..31 |
| fresh ≤ w < 0 | 1 − loss_fresh·w/fresh (scrub-in) | −63..0 |
| w < fresh | 1.0 | – |

Then wear_grip = max(wear_grip, floor), with floor = min(1, 1.25/mu)
(tire record mu[11] slot, §6.5). So a "control" compound never loses grip.
Other floors range from 0.61 (f1) to 0.86 (normal).

With the shipped curve: 0.94 when new, 1.0 from wear 0 to 40000, 0.95 at
150000, 0.70 at 210000. The two middle segments compute
(double)1 − (double)loss_lo − (double)(t × (loss_hi − loss_lo)) (t and the product in single,
t = (w − knee_lo) / (knee_hi − knee_lo)), rounded back to single: the program's
libgcc software double (fp-bit: `__extendsfdf2` 0x27AFA0, `__subdf3` 0x279418,
`__truncdfsf2` 0x279C50; round to nearest even, no denormal inputs, exponent-255 singles
read as Inf/NaN, denormal results truncated; `ee::D` in src/base/ee_double.h). For
normal operands and results, as here, it gives IEEE double's bits. The HUD level (each segment's int() is a truncation toward zero; the
negative segment is −trunc(t·63.9); t ≥ 1 gives the segment's end level, unreachable in the
three middle segments for any curve with knees ≥ 0) drives the tire gauge colour (blue → green →
yellow → orange → red). On the pit surface (F+0x535 bit 4) the routine returns 0 at once:
no clamp, no level, and the peak force of §3.1 then has no wear factor.

Fresh set (`0x1DD110`, 1500/1500), at car init and in the pit: wear =
cp+0x00 (−20000), grip factor = 1 − cp+0x04, brake heat = 0.

### 8.5 Brake heat [C, `0x1E6928`, 3000/3000]

There is **no tire temperature**. The per-wheel 0..1 state is brake-disc
heat:

y += dt·(T_b × pedal × |ω_r| × c0 − (|ω_r| + c2) × c1 × (y + c3)), then
clamped to [0, 1].

- T_b = the axle's maximum brake torque (P+0xE28/E2C) × tire radius;
  "pedal" is that wheel's brake pressure after ABS and aids (0..1).
- c0 = 1/68600, c1 = 0.005, c2 = 27.78 m/s, c3 = 0.05 (static bytes 7, 20,
  100, 5).
- Its only reader is the brake-glow visual (`0x1E6A08`); it affects neither
  braking nor grip.

### 8.6 Pit stops [C, `0x1EDB40` verified on 17/17 scenarios]

- **Requirements:**
  - the course must have AI lines 3 (pit lane) and 4 (pit stop line);
  - the mode must allow it: single player, free practice, battle, machine
    test. Time trial, license and free run do not.
- **Pit-lane surface:** a road triangle flag bit 0 (GTRW triangle +0x0C) under
  any wheel sets the "on pit surface" bit. While it is set:
  - wear is frozen and wear grip is not applied;
  - the car–car proximity classifier (AI contact intensity and slipstream,
    §5.1) ignores pairs with different bits. The collision manager (§5.3)
    does **not** check this bit: physical car–car contact still happens;
  - the rubber band is off;
  - the AI's grip scaling is off.
- **Markers:** points of AI line 4 (progress values; the setup added a lap from the
  first point in the first half of the lap on, and the car's progress gets a lap
  added in the first half). The pit box of a car is line 4's point s, s = its pit
  box byte F+0x4DE (the line's count − (index + 4), set at the start) [C, 2026-10-25]. A step runs the state machine with the new
  and the old progress (`0x1EF060` → `0x1EDB40`), nothing while the start countdown
  runs, and only on courses with both pit lines.
- **State byte F+0x534:**
  - 0 = normal;
  - 2 = pit lane (the car is auto-driven along line 3);
  - 3 = pit stop line (line 4 at 20 km/h);
  - 4 = leaving.
- **Player entry:** in state 0, on the pit surface, crossing line-4 point 1
  forwards → state 2 ("AUTO DRIVE"). The game drives the car; there is no
  speed limiter on the player's own driving. The pit lane is taken at the
  AI pit speed (driver parameter, default 80 km/h + 2).
- **CPU entry** (and a pad car the AI drives, kind 1) [C, 2026-10-25]: with the pit
  request (F+0x535 bit 3), unless the race is over for the car (`0x1EAB80`: no lap
  base, and the time limit passed or the last lap reached), crossing line-4 point 0
  forwards. A CPU car on the pit surface whose collision body flag +0x43C is set
  (and progress below one lap) enters at once, in recovery state 2.
- **In the pit lane (state 2):** past line-4 point 1 (below a lap) off the pit
  surface → back to state 0 at once (`0x1E3BF0`).
- **Decision 25 m before the box** (crossing box − 25 m; before it HUD message 10
  every step):
  - race over → drive through (message 8);
  - no service wanted → drive through (message 9). A service is wanted when a
    wheel's wear differs from the fresh value (so never in races without wear), a
    wheel's +0x28 byte is set, or the conductor's virtual 42 answers 1 (free
    practice; 0 in every other mode) [C, 2026-10-25];
  - speed ≥ 100 km/h → drive through (message 7);
  - otherwise → state 3.
- **The stop**, on crossing the box point:
  - all velocities and dynamic states are zeroed (`0x1DD5F0`);
  - the spawn/stop counter is set to **300 steps (5.0 s)**. The body is
    lifted 0.01 m per step to +0.30 m over the first 30 steps, held, then
    lowered over the last 30 (the scripted pose of §4.3);
  - the heading turns to the box heading at ≤ 30°/s;
  - **at counter = 60 the tires are replaced** (fresh set, §8.4).
- **After the stop:** 0.5 m past the box → state 4 and the "tyres worn" bit
  (F+0x535 bit 2) cleared. Leaving (state 4): past line-4 point 1 (below a lap) off
  the pit surface → state 0; at the exit marker → state 0 and the pit request bit 3
  cleared. The exit marker is point n − 1 for CPU cars and finished cars, point
  n − 2 for a human not finished (less a lap when it is 1.5 laps or more); a
  finished car gets the end of a run (`0x1EACB8` mode 3) there instead of the exit.
  Past the next marker (n − 2 / n − 3) HUD message 5 (full throttle) or 4 unless
  the race is over; before it message 10 unless finished [C, 2026-10-25].
- **The wrong way on the pit surface** (a pad car in state 0): between line-4
  points 3 and n − 2, crossing point n − 3 backwards → state 4 (message 12);
  moving faster than 5 km/h (forward or lateral speed) and not hit → message 2.
- **Only the tires are serviced.** There is no fuel and no time penalty
  beyond the 5 s.
- **Courses with pits:** 54 of the 80 US course files have AI lines 3 and 4
  (28 of the 36 non-license courses). They are missing on the smt* and
  tahiti* courses and their reverses. Line 4 has 12 points (13 on 5
  courses).
- **HUD:** on pit entry and exit an information event is posted (type 1
  pit-in, type 6 pit-out; car index in the low nibble). Type 1 draws the
  text key 'PitIn' ("<car name> has pitted", `0x1941C0`); type 6 only
  plays a sound for the player.
- **Fuel: not modelled.** Mass is fixed at init (catalogue mass + 65 + 55
  kg). No consumption, no refill.

### 8.7 Wear and pit flags [C, `0x1E8340`, 3000/3000; exact in `race_tick_exact`]

Each step after the wear accumulation, in modes that allow pits (conductor
virtual 39), and only while the pit request (F+0x535 bit 3) is clear. With
wear on (cp+0x18 ≠ 0), the car is "worn" when some wheel has wear ≥ 0 and
wear_grip < cp+0x20 (0.91). A worn car gets "tires worn" (bit 2) whatever its
control source (a pad car too, every step); the pit request (bit 3) as well
for control source 2, and for control source 1 unless the race-flow byte
K+0x8000+0x37AC is 3. A second trigger would also arm control source 1 in
flow 3: conductor virtual 35 (asked through `0x1D9FE0` only for a car with
car+0x136D = 0) returning 4 while some wheel's byte W+0x28 is set; no
conductor's virtual 35 returns 4 (0, or 3 for licence and time trial), so it
never acts. The CPU strategy is in docs/specs/ai.md.

### 8.8 Flap pendulums (mud flaps, aerials) [C, `0x1E6A38`]

Visual only. Each wheel W has a pendulum: position x = W+0x98 in [-1, 1] and velocity v =
W+0x9C, zeroed by the standing-start / pit reset (`0x1DD5F0` -> `0x1DD1A8`). Every step, phase 13,
for every car, wheels 0-3 in order (floats; constants at 0x352994..0x3529AC):

```
a  = 5 a_x                                   a_x = F+0x4C4, the forward step's (§4.1)
r  = rnd() x (u >= 0 ? 1 - x : 1 + x)        u = W+0x4C, hub forward speed
if r < 0.02:  a += (1 - x) x 5 x u
if P[0xDD0 + 16 (i >> 1) + s] > 0 and u > 0 and W+0x08 > 0:   s = W+0x24 (§3.6), W+0x08 = Fz
    r = rnd() x (1 - x)
    if r < 0.04:  a += (1 - x) x 20 x W+0x20                  W+0x20 = rim speed
a -= 100 x;  a -= 5 v
v += a dt;   x += v dt                       dt = car+0xEC0
x > 1: x = 1, v = 0;  else x < -1: x = -1, v = 0
```

- rnd() (`0x245D5C`) is the VU0 random register: `vrnext`, the float 1.m minus 1. The register is a
  23-bit shift register (m = m << 1 | (bit 4 ^ bit 22) as emulators model it), seeded by `vrinit`
  with 0.1f (`0x245D78` from `0x1A4630`) when each run is set up (`RacePS2Base::vf9`). Other users
  draw from it too (race effects); the draw order across them is not reproduced.
- The byte P[0xDD0 + 16·axle + s] is the surface's tyre record index (§3.3): > 0 = not the tarmac
  record, so the kicks with the rim speed are for dirt, gravel and grass.
- At rest x = 0. Accelerating, x settles at 5 a_x / 100 (0.05 per m/s^2) and swings with the kicks.
- Readers: the per-step snapshot (`0x1EADB8`, car+0x1684 + 4i = x), and the car model's callback
  (`0x1AB0F8`, every frame): value = 0.5 + 0.5 x, interpolated between the snapshot and the
  current x by the frame's sub-step fraction. The car programs morph their flap and aerial chunks
  with it (docs/formats/car_model.md op 0x06): rally cars' mud flaps (Stratos, Delta and Celica
  rally cars), the 787B's and RX-7 LM's aerials.
- The same callback's fifth value is the steering: clamp(0.5 - 0.5 F+0x41C / P+0x58, 0, 1) (the
  steering command over full lock; stored by `0x1EAFC0` with its previous value), the formula
  cars' driver's arms and wheel.

## 9. Race start: placement, initial state, rolling and flying starts [C; `0x1DD8A8` run on 244 cars × 2 speeds, `0x1DD5F0` and the conductor start virtuals executed]

### 9.1 Decision

- **Event fields:** +0x198 = start speed (km/h), +0x1E6 = flying-start
  distance (s16 m, negative = before the line), +0x1F3 = start type
  (+0x1F2 = countdown seconds).
- **The effective start type** is overridden at race start by the mode:
  - single player, battle, license, machine test keep the event's type
    when the event has no start speed;
  - other modes with a start speed get "none";
  - time trial and free run get type 3 (no hold);
  - free practice gets "none".
- Countdown by type: 0 = event seconds; 1 = 4 s; 2 = 10 s; 3 and none = no
  countdown.
- For each car the mode returns a **control source** (0 pad, 1 AI drives
  with the pad decoded for display, 2 AI only), a **start mode**, a signed
  distance and a speed:
  - default: mode 0 with speed = event start speed;
  - single player with start type 3 (**rolling start**): car 0 gets mode 8;
    **every car gets 80 km/h**;
  - license / time trial / free run with a negative distance and a speed
    (**flying start**): mode 2;
  - licence otherwise (`0x1F0D00`) [C, 2026-10-25]: mode 1 at the event's distance when it
    is not 0, with the event's start speed;
  - time trial / free run otherwise (virtual 26, `0x1F34B8`) [C, 2026-10-25]: mode 3
    when the course's driver set for class 0 (`0x1F5F38`, §2.4 of ai.md) has bytes 25
    and 26: −10 × b25 m at 5 × b26 km/h; else mode 4 (§9.4);
  - battle 2P handicap h: mode 1 at −10 m / min(±h, −20) m (with the event's start
    speed; no handicap: no start speed at all);
  - free practice: car 0 in the pit box (mode 6), cars i ≥ 1 on line 6 at
    −(200i + 300) m and 120 km/h (mode 5).
- **Control source, spec flag, pedal style, ghost** (virtual 24) [C, 2026-10-25]:
  races, battles and free practice (`0x1F3188`): a player's entry (its human
  object +0x13C set) 0, or 1 / 2 in configurations 3 / 2, with its spec flag and
  pedal style; other entries 2. Time trials and free runs (`0x1F3218`): every
  entry's spec flag and pedal style; a player's entry 0 (1 in configurations 2
  and 3), other entries 1; car 1 has the ghost byte F+0x3EA set (1; free runs,
  `0x1F1268`, 2).

### 9.2 Placement

- **Modes 0 and 8:** the GTRW grid slot (§1.3 frames: phys (x, y, z) =
  (slot x, −slot z, slot y)), every car facing **slot 0's** yaw + π
  (`0x18F9C8` is asked for slot 0) [C, 2026-10-25], moved **back** along
  the heading by the front extent P+0x00 (2.10 m R34), so the nose is at
  the slot point. `0x18F9F0` reads slot i at the grid table + 16 i with no
  check against the table's count [C, 2026-10-26]: a slot past the table reads
  what follows it in the road block (padding, then the segment table, whose
  vertex / triangle / wall-reference offsets the course loader `0x227980` has
  turned into pointers: the block's address + the offset), so such cars stand
  near the course's origin, mostly off the road, where the start's ground
  queries miss and the wheel records keep `0x18FA80`'s uninitialised stack
  words (normal, plane, raw scalars). No GT3 menu makes such a field: every
  event's cars fit its course (the rally events race two cars), and the arcade
  races dirt courses one against one.
- **Modes 1–5** (`0x1F2DB0` → `0x1F9880`): on AI line 6 at race progress =
  distance, clamped to ±(lap − 20 m) and wrapped into the lap. The heading
  is atan2 towards the line's point 10 m on; the height is the road under
  the point probed at the line's height (`0x1F8EC8`; on a miss again 1 m
  higher with no segment hint). If the course has no line 6, the car falls
  back to the grid slot (mode 0).
- **Modes 6/7** (`0x1F9E30`): the car's pit box, the stop line's (game line
  4) point count − (index + 4), 0.5 m forward, at the point's height and
  heading; the car takes the stop line at once with driver kind 3 and the
  spawn counter 120. With fewer points the program reads outside the line:
  before its points in the course's AI line block, or, with no stop line (its
  slot null: the line loader `0x1F92F8` nulls an empty line, and `0x1F9D90`
  gives 0), from address 4 on [C, 2026-10-26]. Only free practice's car 0
  starts in its box, and free practice never starts there: only GT mode's race
  menus offer it, the GT event pre-race menu (`0x132BA0`) sets its FREE RUN
  switch off (`0x12C430` refuses `free_session`) when the event's course record
  +8 (`0x2389A0`) is 2, dirt, and every course of GT mode's races without car
  0's box has no stop line and is a dirt course (`race_start_exact`: 8 of the
  34); the championship pre-race menu (`0x133388`) keeps it, but no
  championship runs on dirt. The licence, arcade and demonstration courses
  without a stop line have no free practice.
- **Height and attitude** (`0x1DD210`): the CoG's ground query, the CoG at
  that height + the static CoG height (P+0x3C, 0.13 m), the wheels' queries
  from the CoG's segment; then pitch / roll = the ground's, the pitch plus
  atan2(P+0x40 − P+0x44, wheelbase) (the ride heights), and the CoG at the
  ground point + up.z × P+0x3C. There is no drop. Each wheel starts with its
  axle's static load (P+0xE54 / +0xE58 halved) + its unsprung weight, at the
  static travel, the straight-ahead steer, camber and half-track.
- **Laps:** with the race progress (`0x18FA20`) past every sector start (cars
  behind the line, or placed on line 6 before it) lap 0 and sector 0, else
  lap 1 in the first sector whose start is ahead; a pit box past half the
  lap also gets the lap base 1 (lap 0). So the first crossing starts lap 1.
- **Rank:** after every car is built the conductor ranks them (virtuals 2 / 3,
  `0x1ED358` / `0x1ED1F0`): an insertion sort from index order by laps less
  the lap base, then (both unfinished) the greater progress, the rank byte
  F+0x4E1 = position + 1. Free practice orders by grid slot and sorts by the
  best lap car+0x16C4 (0x157529FF, none, for everyone at the start).

### 9.3 Initial state

- **Standing** (`0x1DD5F0`):
  - all dynamics zeroed; gear 1; **clutch open**; engine speed 0 (floored to
    idle on the first step); shift lockout 12 for an AI-driven car (control
    source 1 or a driver kind) or a pad car with pedal style 0 or 2, else 0
    [C, 2026-10-25]; time scale 1; steer limits ±max lock.
  - The race hold (F+0x4DC, set while the start-sequence counter runs) then
    zeroes the horizontal velocities every step, and the vertical velocity
    is kept (the car settles).
  - The player's clutch request is forced to 0 during the hold. CPU cars run
    the countdown blipper (ai.md §10).
- **Moving** (`0x1DD8A8`, speed v in m/s, km/h × 0.2777778):
  - world velocity = forward axis × v; yaw rate 0;
  - axle speeds = v / tire radius; forward speed and all wheel speeds = v;
  - gear = the gear-for-speed rule (§7 #24; at 80 km/h gear 2 for 222 of
    244 cars);
  - engine speed = clamp(v·ratio, idle, rev limit); rpm truncated;
  - **clutch locked**, request 1.0;
  - turbo lag states set to the current rpm (turbo cars only).

### 9.4 Who drives, hand-over

- **Rolling start** (mode 8, the player): the player's car is driven by the
  AI for **300 steps (5.0 s)**, then control returns and the START event is
  posted. CPU cars race from step 1. There is no hold.
- **Flying start** (mode 2): the player is AI-driven on line 6 until the
  first start-line crossing (lap 0 → 1), which returns control and starts
  the clock.
  - The run-up uses the **CPU AI path** (kind 1): the driver model writes
    the road-wheel angle and pedals directly (ai.md). The §4.4 pad mapping,
    follower and O bytes are not run.
  - The gearbox is automatic and the clutch request follows the AI rule.
  - It uses the AI aid block: TCS on (gain 6.0, a = 1, b = 0, scale 1),
    ASCC off; ABS stays on.
  - Hand-over: the conductor's lap-start hook sets kind 0 and posts START.
    From the next tick the pad path runs with the current recorded frame
    (frames are consumed one per tick from race step 0). The steer
    follower continues from the AI's last angle [I: no reset found].
- **Mode 4 fallback** (time trial / free run with no distance in the event):
  the distance is found by scanning line 6 backwards for the start of the
  last tight arc (radius ≤ 350 m; 200 m on dirt), minus 5 m. The speed is
  the car's top-speed estimate [I, probably unintended].
- The **spawn/stop counter** (F+0x536 = 120, §4.3) is used **only** for the
  free-practice pit start (modes 6/7) and the pit stop (300), not for grid
  starts.
- **End of the run** [C, `0x1EACB8`; ai.md §1; 2026-10-20]: at a race finish,
  the end of a time trial or a machine test the car is driven on by the AI
  (kind 1; a human switches to line 6 first) with its corner-speed scale × 0.8
  and throttle multiplier 0.5, after the event overrides are re-applied for a
  human. A failed licence test (and, per the researched summary, a failed
  time trial or a free run ending on the pit surface) gives kind 7: the stop
  driver with the wheels straight. Licence tests with rule byte 1 or 2 give
  kind 6: the stop driver steering along line 6. The stop driver (throttle 0,
  brake 1) sets kind 8 once |v| < 0.0694 m/s and |ω| < 0.0524 rad/s and
  releases the brake; kind 8 holds the clutch request at 0 and the stop
  clamp's hold applies.

## 10. Remaining per-tick details (coverage survey)

Survey [C]: 364 functions are reachable from the step function (including
mode hooks); 131 are specified above. All remaining ones are runtime, AI
(ai.md), sound, rumble, skid marks, HUD/lap events, replay, accessors or
ground-query internals. For each of those, readers of the fields they write
were searched for: none feed the dynamics.

- **No damage model.** Impacts only drive pad rumble; no grip, power or
  steering value is ever changed by a hit.
- **No wind and no external forces.** F+0x4D0/0x4D4 are only ever zeroed.
- **Kerbs** (surfaces 1, 13) have no special force code: they act through
  the compound slot and mu, the bump wave, smoke/sound and the wear
  exclusion.
- **Dirt** (course category 2): steering curve C (§4.4) and the brake cut
  (§10.4) are the only force changes.

In this section [X n/n] means verified by executing the original function
on n random states.

### 10.1 Ground-frame speeds and hub velocities (0x1EA6B0, 0x1EA5F8) [X 20000/20000]

Called at the very end of the suspension phase (inside 0x1EA9B8, after body attitude, ground frame, collision commit and heave have all been done this tick), so the values feed the *next* tick's
phases 2-7 (slip angle, slip ratio, forward step, aero, ...). Notation: v = world velocity (F+0x44C, +0x450, +0x454; car+0x13D0..), t1 = ground-frame "forward" tangent (F+0x3C, +0x40, +0x44 = car+0xFC0),
t2 = ground-frame "lateral" tangent (F+0x48, +0x4C, +0x50 = car+0xFCC), both written by 0x1EA4D8 from (ground pitch, ground roll, heading) (§4.3); r = yaw rate F+0x47C.

- **u = F+0x488 = t1 . v** and **l = F+0x48C = t2 . v**: full three-component dot products (the vertical velocity enters, so u is the true speed along the slope).
- Hub velocity of wheel i in the car frame: **W_i+0x4C = u + X_i r** (forward component) and **W_i+0x50 = l - Y_i r** (lateral component), with half-track levers
  X = (-tf, +tf, -tr, +tr) (tf = P+0x28, tr = P+0x2C) and axle distances Y = (a, a, -b, -b) (a = P+0x10, b = P+0x14). These are exactly the "u_h, v_h'" of the low-speed hold (§4.1), the
  "x" source of the slip ratio (3.4) and the slip-angle velocity (3.2). Because t1/t2 are *ground* tangents, hub speeds are measured in the ground plane, not in the body plane.
- Aero drag and downforce use u only (S8); the stop clamp, rpm-from-speed and the HUD speed use |u|.
- car+0x16A8 = |u| (speed magnitude); car+0x16AC = running maximum (only while F+0x4E0 == 0); car+0x16D8 = speedometer value following |u| at most 111.111 m/s per second.

### 10.2 Free-rolling wheels (0x1EA720) [X 20000/20000]

After the hub velocities, for every wheel whose flag byte **W+0x5F is 0** (bit0 airborne, bit1 brake torque non-zero including handbrake, bit2 coupled to the centre clutch, bit3 driven; flags rebuilt by 0x1E6EC8 each tick):

  **W_i+0x20 (wheel peripheral speed, m/s) := cos(delta_i) * W_i+0x4C - sin(delta_i) * W_i+0x50**, with delta_i = W_i+0x0C the wheel steer angle.

That is, a grounded, unbraked, undriven wheel is **kinematically locked to the ground**: its peripheral speed equals the hub speed along its heading, whatever the driveline integration (phase 7) computed
earlier in the same tick. The overwritten value is what slip ratio sees next tick, so such wheels have slip ratio 0 (the "x <= omega" branch gives s_raw = 0) and produce no longitudinal slip force.
(Their inertia is already accounted for in the forward step as extra mass, §4.1 step 2.) Flagged wheels keep their integrated speed.
Also: car+0x13DC + 4k = 0.5 (W_{2k}+0x20 + W_{2k+1}+0x20) * P+0x27C+4k (P+0x27C = 1/tire radius), computed for axle k iff the axle has a free wheel or the byte at car+0x218+k is 0. Display only; nothing reads it.
Example of free wheels: the rear wheels of an FF car while the brake and handbrake are off.

### 10.3 Engine condition (break-in / wear and oil) [X 20000/20000 on each routine]

State per car: 8 words at car+0x137C: word 0 = u32 odometer (units of 2 m, saturating at 0xEE6B27A6), word 1 = **E** (float, metres; engine wear / running-in), word 2 = **O** (float, metres; oil), words 3..7 carried but unused by physics.
Initial values are copied at car creation by 0x1EA8C0 from the race entry record (entry + 0x2104..+0x2123, passed by DynamicsConductor::vf23 as a stack argument). **Zero counters give factor exactly 1.0**.
(GT mode stores and restores them per owned car; the meaning "new car E = -300000, oil change O = -300000" is in gt_mode.md section 7.)

**Where the words come from and go** [C]:
- The race entry constructor (RaceEntryCar 0x1771B8) clears entry +0x2104..+0x2123 (0x1D9B10 → memset 0x20 bytes): a run nobody seeds runs with a fresh engine (factor 1).
- Seed, before every run: 0x12EE78(car, race, i) copies the car's eight words (CarEquipments/garage car +0x204..+0x223 in memory) to entry i +0x2104.
- Copy back, after every run: 0x12EED8(car, race, i) copies the race car's words (car+0x137C..+0x139B, all eight) to the car +0x204 and returns word 0 minus entry +0x2104 word 0 (the run's odometer delta, 2 m units). The driver sums the deltas and adds the sum to GameStatus +0x28 in memory (= save +0x20, the total distance) when it ends.
- Drivers that do both: arcade race 0x12F840 (skipped while its stack flag +0x8 is set), free run 0x12FF48 (arcade and GT), machine test 0x131690, GT race 0x132BA0, championship 0x133388. The time trial 0x130A50, licence 0x132030 and 2P battle 0x133F48 drivers never call either: zeros, nothing copied back, no distance.
- The car is the race setup's car pointer (GameStatus +0x307C8 +0x540; +0x544 player 2): a garage car itself (0x219840 / 0x217F88, also from the arcade car pick 0x114610 for garage cars) or the setup's own CarEquipments buffer (+0xD8 / +0x308, built from the car code at each arcade car pick by 0x217F20; 0x219800 for player 1; whether the build clears the condition words is not traced).
- Word 3 is the car's dirt: 0x1D9F38 (from the per-car step 0x1DEBB0) adds to it with an upper clamp, 0x1D9F18 sets it clamped. Words 4..7: no reader found.
- The race HUD (RaceDisplay::vf3) reads the live words: the odometer 0x1D9B60 (word 0 / 50, or / 80 with the miles unit, then / 10) and the oil lamp 0x1EB340 → 0x1D9E20 (O >= block +0x20 = 300000).

Parameters (static bytes at 0x2D2950..0x2D2958 = 30, 30, 50, 10, 20, 20, 50, 10, 30, expanded by 0x1DAB50 into a block at 0x2D2B00 on first use):
E1 = -300000 (byte0 x 10000), c1 = 0.03 (byte1 x 0.001), E2 = 500000 (byte2 x 10000), E3 = 600000 (byte3 x 10000 + E2), c2 = 0.02 (byte4 x 0.001), O1 = -100000 (-byte7 x 10000),
cO = 0.05 (byte6 x 0.001), Omax = 300000 ((byte5 + byte7) x 10000). Byte8 (300000) is unused here.

**Torque factor** f = f_E(E) x f_O(O) (0x1DAD28):

```
f_E(E) = 1 - c1                                   for E < E1                 (97 %)
         1 - c1 * E / E1                           for E1 <= E < 0            (linear 97 % -> 100 %)
         1                                         for 0 <= E <= E2
         1 - c2 * (E - E2) / (E3 - E2)             for E2 < E < E3            (linear 100 % -> 98 %)
         1 - c2                                    for E >= E3                (98 %)
f_O(O) = 1 + cO                                    for O < O1                 (105 %)
         1 + cO * O / O1                           for O1 <= O < 0            (linear 105 % -> 100 %)
         1                                         for O >= 0
```

**Use:** each tick in phase 2 (0x1EA428) **F+0x4B0 := torque_curve(omega) x f** with omega = F+0x448 (0x1E0BC0; torque_curve is the clamped piecewise-linear lookup of §2.1, here the bisecting 0x1D8E50 (§7 #73); for turbo cars the table is the full-boost table and
the throttle blend of 2.2 handles boost). F+0x4B0 is the "T_wot" that 0x1E1768 (throttle blend), the TCS (exits if <= 0) and 0x1E5B98 use. F+0x4B4 = engine friction torque (unscaled).
**So E and O scale the whole full-throttle torque curve, and not the engine-braking (friction) torque.** [X 3000/3000 for 0x1E0BC0 = 0x1D8E50 x 0x1DAD28.]

**Counters** (0x1EA9B8, once per tick at the end of phase 12, d = |u| dt in metres where u = F+0x488 from S1):
- E := min(E + d, 600000) (0x1D9C80; no lower clamp);
- O: if O < 0: O' = O + d, and if O' >= 0 then O := 300000 (jump, "worn"), else O := O'; if O >= 0: O := min(O + d, 300000) (0x1D9D78);
- odometer (0x1EA908, skipped when u < 0; car+0x16A8 holds the signed u at that point and becomes |u| right after): d = u dt (mul.s, dt = car+0xEC0), x = carry + d (add.s); 0x1D9BB0(word0, x): x <= 0 → carry 0, word unchanged; else n = cvt.w.s(x) halved towards zero, word0 += n, if word0 > 0xEE6B27A5 (unsigned) it becomes 0xEE6B27A6 and the carry 0, else carry F+0x3F4 := x - 2n; F+0x3F0 += d (display total, wraps at 8,000,000).
  [X 10000/10000 for the whole 0x1EA9B8: all of odometer, E, O, |u|, max speed, speedo value, F+0x3F0, F+0x3F4.]
Typical GT-mode effects: a new car (E = -300000) makes 97 % power, 100 % after 300 km; a car with E >= 500 km loses up to 2 % (98 % from 600 km); fresh oil gives up to 105 % for 200 km, fading to 100 % at 300 km; with O >= 0 (new car or after the jump) the oil factor is 1 and O just accumulates up to 300 km (HUD warning, no power effect).

### 10.4 Dirt brake cut (prelude of 0x1E3068) [X 8000/8000, 3433 of them on dirt]

Runs every tick in phase 6, for human and AI cars alike (it is before the ASCC enable test and before the ABS loop, so it is also active while the handbrake disables ASCC), **only when the course category byte equals 2**.
Static bytes at 0x2D2965, 0x2D2966, 0x2D2967 = B5 = 15, B6 = 100, B7 = 0. If B5 = 0 the block is skipped.

  s = min(1, |delta| / (B5 degrees)),  delta = F+0x41C, the current steering command (rad; output of the steering follower, before per-wheel Ackermann/toe)
  front brake pressures (W_0, W_1 +0x90) *= 1 - s B6/100;  rear (W_2, W_3 +0x90) *= 1 - s B7/100.

With the shipped bytes: **the front brake is released linearly with steering, completely at 15 degrees of steering command or more, rear unaffected.** (delta is limited by the speed -> angle curve of 4.4, so the fade is partial or complete depending on speed [I].) The pressure is the variable that the ABS then scales and the brake torque uses. The handbrake torque is separate and unaffected.
This is the only dirt-specific change to forces in the vehicle code.

### 10.5 Per-wheel suspension step, exact (0x1E8E58) [X 20000/20000; 3331 airborne, 8782 below the knee, 7893 with bump-stop damping]

(Complements §4.2; additions marked *new*.) Inputs: x = suspension coordinate from 0x1E91A8 (F+0x3B4+4i); block = P+0x2B8+0x44(i>>1): knee +0x00, bump limit +0x04, max droop +0x08, spring k +0x14,
damper T/O/G bump +0x18/+0x1C/+0x20 and rebound +0x24/+0x28/+0x2C, k2 +0x34, preload +0x3C, bump-stop damping +0x40; 1/dt = F+0x4A0; x_prev = W+0x18.

- If **droop < x (strict): airborne**. W+0x18 := droop; W+0x08 (Fz) := 0; W+0x1C (damper speed) := 0; W+0x74 (damper force) := 0; W+0x70 (spring force) := -preload; **returns -preload** (*new: the body feels minus the wheel's unsprung weight*).
- Otherwise x' = max(x, bump limit); e = x' - knee; Fs = -k x' (+ k2 e^2 if e < 0). v = (x - x_prev)/dt (**uses the unclamped x**; stored unclamped in W+0x1C); Fd = damper(v) from 0x1E8DD0; if v < 0 and e < 0: Fd -= (bump-stop damping) x max(v, -1).
  **W+0x18 := x' (the clamped value, *new*)**; W+0x70 = Fs; W+0x74 = Fd; **Fz = W+0x08 = max(0, Fs + Fd + preload)**; returns Fs + Fd (the body force of the wheel; preload is not included).
- The anti-roll term (0x1E8FD0) uses the W+0x18 values of the two wheels: airborne wheels contribute the same droop value, so anti-roll vanishes when both wheels of an axle are airborne.

### 10.6 Airborne summary (answers (d))

1. Per wheel: S5 above (Fz = 0, damper 0, body force -preload). W+0x5F bit 0 is set for the wheel; the effective mass of the forward step excludes it, tire force peak F_peak = Fz-proportional = 0, so no tire force.
2. All four Fz = 0 (sum of W+0x08 equals 0): 0x1E8598 calls 0x1E7D90 (lateral / yaw) and 0x1E8008 (stop clamp / world velocity) with the airborne flag. The lateral step then only sets F+0x474 := 0 and F+0x4C8 := 0: **yaw rate is frozen**
  and no lateral/yaw accelerations are produced. The stop clamp then does nothing except the spawn-counter zeroing: **the world x/y velocity is not touched, so drag, engine and brake force do not act in the air** (the forward-speed
  variable F+0x488 is recomputed from the world velocity at the end of the tick anyway). The collision sweep still integrates x, y and heading with the frozen velocity and yaw rate.
3. Partially airborne (at least one wheel loaded): nothing special; the loaded wheels carry all forces.
4. Attitude: pitch and roll integrate the spring/preload/aero body forces only (§4.3), with the clamps (rates +-pi/2, angles +-pi/4). No air control, no damping of the rates.
5. Free fall: u_body = (sum F_i + sum D_i + B)/m_sprung along the body up axis; with the four wheels airborne F_i = -preload_i - (aero downforce)/2 per wheel, so for the R34 (preload 98 N per wheel, sprung mass 1640 kg)
  the body accelerates at -(9.8 + 392/1640) = **-10.039 m/s^2 along its up axis** (measured -10.0390 in the closed loop, exact), plus the in-plane component from any tilt. The nose-up drift for the R34 block is about 0.069 rad/s^2
  from the front/rear preload lever difference (2 x 98 x 2 pi (b - a)/Y; a, b = axle distances, Y = pitch inertia parameter) before aero.
6. Smoke/squeal state of a wheel that leaves the ground persists for 5 more steps (§8.3). The bump-stop attitude nudge (4.3) runs only when a wheel was or is at a bump stop.
7. Landing: the first loaded step uses v = (x - droop)/dt (a large compression speed), but 0x1E8DD0 clamps the damper speed to +-1 m/s, so the damper force is bounded.

### 10.7 Slope and bank (answers the slope-term question) [closed loop with the original functions; scripts/survey/slope.py, slope_t.py, bank_t.py]

There is **no gravity term in the forward or lateral force sums.** Gravity is applied once per tick as v_z -= 9.8 dt in the heave step (4.3) and nowhere else. The along-slope component emerges because
(i) the forward and lateral speeds are dot products with the *ground-frame tangents* (S1), (ii) the world horizontal velocity is advanced with the body-frame axes (4.1 step 5) and (iii) the suspension forces
act along the body up axis, which is tilted by the ground pitch/roll through the wheel heights.

Test: R34 parameter block, analytic ground plane replacing the ground query (0x1E8B68 only), original 0x1E9D48, 0x1E9E18, 0x1E91A8, 0x1E9348, 0x1E96F0, 0x1E9998, 0x1E9DE8, 0x1EA4D8, 0x1EA9B8, world position advanced by v dt,
no tire/drive/aero forces (coasting), 200 steps after settling, linear fits:

| slope theta (deg) | du/dt (m/s^2) | -g sin(theta) | horizontal dv/dt | -g sin cos |
|---|---|---|---|---|
| 3 | -0.5131 | -0.5129 | -0.5124 | -0.5122 |
| 6 | -1.0253 | -1.0244 | -1.0197 | -1.0188 |
| 10 | -1.7042 | -1.7018 | -1.6783 | -1.6759 |
| 15 | -2.5416 | -2.5364 | -2.4551 | -2.4500 |
| 20 | -3.3599 | -3.3518 | -3.1579 | -3.1497 |
| -5 (downhill) | +0.8535 | +0.8541 | +0.8502 | +0.8509 |
| -10 (downhill) | +1.6993 | +1.7018 | +1.6735 | +1.6759 |

Bank (ground rising to the right, car moving along the road at 20 m/s, no lateral tire force): d(l)/dt = -1.7017 at 10 degrees (g sin = 1.7018), -2.5363 at 15 degrees (2.5364); horizontal dv_x/dt = -g sin cos; forward speed unchanged (20.01 -> 20.0).
**Conclusion: the effective slope gravity is exactly g sin(angle) along the ground tangent, for both pitch and bank.** A reimplementation that projects gravity on the ground-plane tangents gets the same steady-state
acceleration. The vertical component is handled by the suspension, not by an explicit force: the transient (spring compression and the vertical-velocity limiter in 4.3) is what matters at crests and dips.
Tire force directions are not tilted by the slope (they act in the body-frame horizontal plane); the load Fz changes only through the spring compression.

### 10.8 Phase 12's call order, its effects and bookkeeping [C; ported exactly in `sim::exact`, race_tick_exact]

`0x1EB218` (a0 = car) calls, in order: `0x1E9D48`, `0x1E9E18` (F), `0x1E8B68` (ground queries), `0x1E91A8`, `0x1E9348`,
`0x1E96F0`, `0x1E9998`; the effects `0x1DF2B0`, `0x1E0068`, `0x1DEBB0` (§8.3), `0x1DF148` (returns at once), `0x1DFC60`,
`0x1E06D8`, `0x1E0768` (F); then `0x1E9DE8`, `0x1D8A80` (body axes from F+0x468..0x470), `0x1EA4D8`, `0x1EA9B8`
(§10.1–§10.3), `0x1EAFC0` and `0x1DF150` (the tyre smoke emitter). Only `0x1EA9B8` and the heading wrap of `0x1EAFC0`
feed the dynamics; the rest are outputs (pad, sound, camera, drawing). None writes outside the car record.

- **Pad vibration** (`0x1DF2B0`, car+0x16D5 = min(127, int(256 L))): L = max(bump + kerb, drive + brake, hit), where drive =
  min(0.333, |(0.5 (s₀² + s₁²) + 0.1) (D₀ + D₁)| × 0.00065039) (front slip ratios W+0x68, drive forces W+0x88), brake =
  min(0.333, ((B₀ + B₁)/19600)²) (front W+0x8C), hit = 8h + 0.25 when the last wall hit's strength h (F+0x4F0, written by
  the collision step) is > 0; and, unless the race hold F+0x4DC is on, bump = Σ over the four wheels with damper force
  W+0x74 > 0 of min(1, force/19600), kerb = Σ over the front wheels on surface 1 of min(0.25, |Fz/2940 × u/64|).
- **Steering force** (`0x1E0068`, car+0x16D6 = −128 − int(127 y) as a byte, 0x80 centred; filter state F+0x514 / +0x518):
  x = clamp((Fy₀ + Fy₁)/P+0xE54 (× F+0x420 when setting +0x37 is set) × g, ±1) from the front lateral tyre forces W+0x80,
  through an odd curve (`0x1D92E0`; identity with the shipped setting), plus a pad term (60 × the change of the pad's
  steering value car+0x16A4 − car+0x16A6), the clamped steering command and the clamped front slip angles, each with its
  gain. Filter mode (setting +0x39; shipped 1): 0 none, 1 the mean of this and the last step, 2 the larger magnitude of the
  two when they have the same sign, 3 a peak that holds 7 steps and then decays by 8 per second; 4+ gives 0. Then the
  aligning term Σ over the front wheels of (wheel lateral axis · road normal) × Fz × (front spring rate + 49000)/196000,
  times its gain over P+0xE54; below 2 km/h y is clamp(δ, ±5°) / 10° instead; y is clamped to ±1. The gains and the curve
  are built at race start by `0x1DFF40` from the static block 0x2D4628 + 0x34 .. 0x3E. Only the pad reads it (`0x1E0618`).
- **Engine sound levels** (`0x1DFC60`; L = min(turbo load F+0x440, 1); c₀..c₃ = 0x49CFD8 + 0x64 .. 0x70, built by
  `0x1DF4C0`): F+0x4E5 := (F+0x4E5 + min(127, int(32 (3L + 1) c₁)))/2; F+0x4E4 := (F+0x4E4 + min(127, int(127 k (|L −
  0.375| + 0.375) c₀)))/2 with k = 1 while the clutch is locked (S = 1), else 0.5; F+0x4E6 := min(127, int(F+0x4E6 c₂));
  F+0x4E8 ×= c₃.
- **Ghost cars** (`0x1E06D8`): when the draw mode `0x1E0668` is not 3 (a ghost car, F+0x3EA ≠ 0; presentation.md), the tyre
  effects W+0x2C = W+0x30 = 0, W+0x34 = 1 and the sound levels F+0x4E4..0x4E6 = 0.
- **Lagging view angles** (`0x1E0768`, fixed step 1/60; read by the on-board camera): with Δ = wrap(angle) − wrap(follower)
  into [−π, π) (`0x1D8C80`), the heading follower (F+0x500, rate +0x504, acceleration +0x508): a = 30 Δ − 10 ω,
  ω += ½ (a_last + a)/60, follower := wrap(follower + ω/60); the pitch follower (F+0x50C, rate +0x510): a = 20 Δ − 10 ω,
  ω += a/60, follower := wrap(follower + ω/60). F+0x4F8 / +0x4FC = Δ after the update (phase 1 saves them).
- **`0x1EAFC0`**: the heading F+0x470 loses 2π when ≥ 2π and gains it when ≤ −2π, and so does the previous-tick heading
  car+0x15C8; each wheel's spin angle W+0x38 += rim speed × P+0x27C (1/radius) × dt (nothing during the race hold),
  brought into (−2π, 2π) by whole 2π steps that move the previous-tick angle (car+0x1614 + 4i) with it; the drawn car's
  points car+0x158C = position − P+0x54 × forward axis and car+0x1598 = ground point (F+0x30) − P+0x54 × ground forward
  axis, both in the course frame (x, height, −y), last tick's at car+0x15A8 / +0x15B4 (read through the interpolating
  accessors `0x1EB400`..); car+0x15A4 = clamp(0.5 − 0.5 δ/P+0x58, 0, 1) (§8.8), last tick's at car+0x1694.


## 11. Hull contact, course-out and resets [C; negative results exhaustive over the readers and writers of the car state]

- **No hull or scenery contact.** The dynamics touch the ground only through
  the four wheel probes (highest road triangle under each hub, strictly
  interior hit test, §1.3). The body is limited only by the bump stops and
  the §4.3 nudge. There is no body-bottom clearance, scrape force or
  scenery collision. The collision manager sees only GTRW walls and other
  cars.
- **No respawn or reset-to-track** in any mode, for the player or the AI.
  No code tests position, height, speed or a timer and then moves a car.
  The only stuck aid is the collision step's steering nudge (§5.3). Wheel
  misses keep the previous ground height; the closed one-sided walls keep
  cars on the mesh (the mesh covers the area inside the walls on all US
  courses except tiny holes on the wall line).
- **Course-out is a flag, not a reset**, and only in Time Trial and License:
  - all four wheels on soft surfaces (surfaces 2, 3, 4, 6: grass, gravel,
    sand) in the same tick sets the course-out flag (player car only),
    **except on a dirt course** (course table +0x08 = 2: the Tahiti and
    Swiss Alps / Smokey Mountain courses and their licence sections), where
    four wheels off never fail. A hard wall hit also sets a flag in both
    modes: velocity change |j|/m > 10 m/s (car–car hits never do);
  - the check [C, `DynamicsConductor::vf40` 0x1DA020, called each tick from
    `0x1E8B68` with the counts of grounded wheels on the pit flag (ground
    record +0x33 bit 0) and on soft materials (table 0x2D53D8)]: nothing once
    a failure bit (F+0x51E) is set or for a car whose control byte F+0x3E9 is
    not 0. Four pit wheels ask the conductor's vf37 (licence 0x1F10C8 and
    time trial 0x1F1F30: 1 = fail; the base and free run: 0) only
    while the car's progress F+0x3E4 (+ a lap when in the first half of the
    lap) lies strictly between the pit stop line's (game line 4) points 1 and
    n − 2 (`0x1FA5B0`; no line 4 or fewer than 6 points: never). Four soft
    wheels ask vf38: the licence (0x1F10D0) and time-trial (0x1F1F38)
    conductors return 1 (fail) unless the conductor's course kind
    K+0x8000+0x37AD (set from course table +0x08 by `0x1F2F28`, read by
    `0x1F2F68` / `0x1F2F90`) is 2. The larger answer 1 counts F+0x4DF up, 2
    sets it to 0xFF, 0 clears it, and a nonzero F+0x4DF sets F+0x51E = 2.
    Single races, free practice and battles replace the check by an empty
    function;
  - the pit span [C, 2026-10-25; implemented in the exact core's race flow]:
    `0x1FA5B0(ctx, k, s)` is called with ctx = K+0xBBA8, k = 0, s = F+0x3E4. Game
    line 4 = the line table ([ctx+4]) entry +0x18; no line or its count word < 6 → 0.
    s gets the lap length ([[ctx]+0x70]+0x18 via `0x18FA10`) added when s < 0.5 × lap
    (float); 1 iff point[1 + k]+0x14 < s < point[n − 2 − k]+0x14 (strict). `0x1ED8D8`
    passes k = 1 and `0x1ED900` k = 0 (their callers not traced). The AI's own pit
    sections are inline code in `0x1F7AE0` (points 1, 2, n − 3, no count test; ai §5);
  - the point distances +0x14 are the loader's (`0x1F92F8`): for line 4 one lap length
    is added from the first point whose distance is below half a lap on;
  - F+0x3E4 there is the previous tick's: the step (`0x1F39D8`) runs `0x1EF060`, which
    writes it, after phases 12–13 (ours: the race distance before the tick's lap update);
  - survey (US licence courses, our located point distances): 28 of 46 courses carry a 12-point line 4 (montecarlo
    13), 18 none (circles, slaloms, test course, rally courses, L_midfield3,
    L_seattle3). L_akasaka2 (IA-1): point 0 at 2521.5 m is below half the 5120.9 m lap
    and point 1 (2710.8 m) past it, so every point has the lap added and no s reaches
    the span (39 m margin on point 0). L_rome1 (IA-6): inside only from point 1
    (1730.5 m) to half a lap (1979.4 m);
  - effects: the lap time is not registered. Time Trial posts "TimeTrialFail"
    and License posts "ExamFail", and the car is handed to the stop driver
    (finished flag set; no teleport). Other modes never set the flag.
- **Licence stop check** [C, `0x1EF488`, sole caller `0x1EF780` from the
  licence conductor; execution-verified 2026-10-11]: "stopped" holds only
  when all four wheels' ground triangles carry flag 0x04 (ground record
  +0x33 bit 2) **and** the 3-D speed √(|v|²) (from F+0x44C) is ≤ 0.2777778 m/s
  (1 km/h); otherwise it is false. It also outputs |v| in km/h.
- **Wrong way** (all modes, player): while the lap progress goes backwards
  (`0x1EE978`, a crossing of the start line backwards included) F+0x535 bit 0
  is set, and above 5 km/h (√(v_x² + v_y²) of F+0x488 / F+0x48C) a pad car
  (control source 0, driver kind 0) that hit no wall or car this step gets the
  HUD message 1 (`0x1DE018`); below 3 km/h the message is cleared; going
  forwards clears the bit and the message [C, `0x1EF060`]. No effect on the
  car.
- **The only relocations** are race-start placement (§9), the pit stop
  (§8.6), and the player's own Restart/Retry in Free Run, which restores a
  recorded lap-start snapshot (`0x1EE6D8`, via `0x1F1400`).

## 12. Program versions: NTSC-U, PAL, NTSC-J [C; every exact test run on each disc]

Three programs exist: **NTSC-U** (US 1.00 and 1.10 ship the identical CORE.GT3 and GT3.VOL; the discs differ only in the
boot file and system area), **PAL** and **NTSC-J** (the earlier build). The same routines sit at other addresses (the
diff harness maps them, docs/dev/DIFF_HARNESS.md); the program's own tables (the static physics block, tyre effect block,
controller settings, surface tables, AI driver sets and line tables, gear kinds, spec defaults, differential letters,
machine test sectors, stick curve, tyre effect kinds / scales / parameters) are byte-identical in all three, except the
HUD message display times (PAL 100 steps where NTSC has 120). Each disc's own car database (paramdb_us / _eu / paramdb)
and course files feed the physics.

**PAL runs the race step 50 times a second** (NTSC 60). There is no single rate value: each routine has its own constants.
The step length `0x1ECFC8`: dt_car = time scale x 0.02 ($gp constant; NTSC 1/60) and its inverse 50 / time scale (an
immediate; NTSC 60). The view follower `0x1E0768` integrates with its own 1/50. The pad's digital ramps (`0x1E3EA0`) are
100 / (50 x byte). Everything integrated physically (forces, speeds, filters, tyres, engine, aero) uses dt_car. The
counts in steps (NTSC-U addresses):

| Count | Routine | NTSC | PAL |
|---|---|---|---|
| shift lock-out (standing start, a player's run-up, the clutch's lock, the AI's speed control) | `0x1DD5F0`, `0x1DDAD0`, `0x1E7500`, `0x1F7590` | 12 | 10 |
| pit box start hold; the run-up's AI holds | `0x1DDAD0` | 120; 180 / 300 | 100; 150 / 250 |
| pit stop hold; its tyre change at | `0x1EDB40` | 300; 60 | 250; 50 |
| spawn hold; rising from ... over ...; held below | `0x1E90F8` | 300; 270 over 30; 30 | 250; 225 over 25; 25 |
| smoke count | `0x1DEBB0` | 6 | 5 |
| steering force: wheel delta x; filter count | `0x1E0068` | 60; 60 / 7 | 50; 50 / 6 |
| kick-down counts; anti-hunt | `0x1E4F10`; `0x1E4FF8` | 13 / 61 / 60; 240 | 11 / 51 / 50; 200 |
| AI start hold; lane-change cooldown; arc tick cap | `0x1F8A00`; `0x1F6D70`; `0x1F7AE0` | n < 13; 30; 6000 | n < 11; 25; 5000 |
| end countdown; start countdown | `0x1F2AE8`; `0x17A5E8` | 3 x 60 + 1; seconds x 60 | 3 x 50 + 1; seconds x 50 |
| collision effect counter | `0x1FB890` | 24 | 20 |
| HUD message display time | the message table | 120 | 100 |
| race clock (3000 a second): per step; a line crossing's fraction of a step | `0x1EFCD8`; `0x1EE978`, `0x1EF780` | + 50; x 50.0 | + 60; x 60.0 |

**No 60 Hz path in the PAL program.** The PAL program has no way to run at 60 Hz: its display mode is a constant PAL
(sceGsResetGraph's mode 3) both where it starts the display (PAL `0x102CE8` -> `0x251928` / `0x251910`) and in the main
loop's display call every frame (PAL `0x1013C0` -> `0x2441E8`, mode 3), with no other caller reaching the mode (NTSC-U
passes 2 at the same places); its step length (PAL `0x1EDF40`) has no branch (the 1/50 word is read only there, the 50.0
is an immediate); every count in the table is an immediate with no mode test beside it; no string names a refresh rate,
and no setting reaches the display mode or the step. The only 1/60 words in PAL's data belong to two menu spring integrators (PAL `0x1D2940` / `0x1D2980`, the
menus' cameras), and the 60.0 immediates in its race code are the same as NTSC-U's (unit conversions) apart from the
race clock's line-crossing factor above.

**Demonstrations.** The PAL program loads only its own re-recorded runs, `demo/%s%s_p` (the event, `_uk` for L0S0001
in English, then `_p`; thumbnails `demo/thumbnail/%s%s_p`), recorded on the 50 Hz step with PAL's own finish times;
NTSC-U and NTSC-J load `demo/%s`. The PAL disc also carries the NTSC-U runs under their NTSC-U names, byte-identical to
the US disc's.

**In the engine** (`sim::Behaviour` = rules + rate, `ProgramData::behaviour()`): every race runs 60 steps a second
with the rules of the disc's release (PAL's or NTSC-J's rule branches below on those discs); online races use NTSC-U's
rules on every disc; the demonstrations and licence demos play the NTSC-U runs every disc carries, at 60 with NTSC-U's
rules. PAL's full release behaviour (50 steps a second with its rules) exists only for the diff harness, which checks
each disc's own release behaviour against its program (docs/dev/DIFF_HARNESS.md).
On a PAL disc the licence tests' and time attacks' medal times (event + 0x1C4 / + 0x1C8 / + 0x1CC) come from the
disc's paramdb_us.db (the same file as the US disc's), since the PAL release retuned 39 of them (29 licence tests, the 10
AT targets) for its 50 Hz step; the rest of each event is paramdb_eu.db's (`data::CarDatabase`). NTSC-J's own times are
NTSC-U's already.

**The tables as values, checked on the disc; the rules found in any build** (`program_tables.cpp`, `ProgramData`,
`program_locate.h`). The 20 tables are written out in the source (named fields, packed back into the tables' bytes; a
pinned hash), the same in the three releases but for PAL's HUD message times; the locator below finds the disc's tables
and recognises the rules' routines. A table GT3's code places (tier 1, or a table at a fixed offset from one) is the
disc's: GT3's values on the four discs, and a modified game's own values where it changes them (`ProgramData::changed`,
reported "changed on this disc"), with whatever values the engine can take (the limits: the rolling curve's 1 .. 8
points, effect kinds below 16). A table found only by its content must equal a release's; one unlike every release's, or
not found, is an issue and GT3's values of the program's release serve. Online races and the game's own recorded demos
always run on GT3's values (`ProgramData::gt3_tables`, the same fingerprint as the disc's when nothing is changed), so a
changed disc's races are its own. A program it does not know races on the exact core with these tables and its nearest
release's rules where its routines are not recognised (`race::race_physics`). Each instruction of the program's
code is masked of what moves between builds: jump targets, the halves of an address a lui and its addiu / load / store
form, `$gp` offsets (`$gp` from the start-up code at the entry point), and a load from a fixed address is replaced by the
value it loads (0 when that value is itself an address), so constants such as PAL's 1.5 km/h stop speed still tell the
routines apart. A table is found through the routine that addresses it: the hash of a window of that routine's masked code
(the whole routine up to 256 instructions, else 32 either side of the address) found anywhere in the code, the address
decoded from the window's lui / addiu (or `$gp` addiu), within the engine's limits (tier 1). Failing that, a table next to
another in every release is taken from that one's place, else the one place in the program's data whose bytes pass its
content check (shapes as GT3's tables have them: counts, ranges, rising breakpoints, symmetry; tier 2). A table neither
finds is reported and GT3's values serve. The routines whose rules differ (`sim::Rule`, 23 of them)
are recognised by the hash of their whole masked code; each follows the release whose variant the program has (one a
release lacks, such as NTSC-J's i.LINK battle conductor, counts as that release's when the program is that release), and
one unlike every release keeps the program's release's rules (the nearest guess for a build's routine of its own;
online races follow NTSC-U's there too). The program is the release every recognised piece agrees on. On the four
discs every table is found through its routine (tier 1) at the address the per-release rows had, and every routine is the
release's own; a masked hash of most tables' routines is the same in all three releases.

The masking against a corpus of nine builds (the four GT3 discs and five GT Concept discs, `--locate-survey`): of US
1.10's 8253 functions of 8 instructions or more, US 1.00 has all, PAL 7795 (94 %), NTSC-J 7406 (90 %) and each Concept
build 552 .. 576 (7 %; with loads' offsets and every register masked too still only 8 %: the Concept builds are other
code, not a masking problem). The Concept Seoul revision pair: 7529 of v1.02's 8550 functions are in v2.00 (88 %; 93 %
with load / store offsets masked). The loaded values matter: without them 50 more PAL functions match NTSC-U's, functions
that differ only in a constant (the rate constants, PAL's 1.5 km/h licence stop), so the PAL licence stop's routine
would match NTSC-U's row; masking load / store offsets would match routines whose structure layout differs (PAL's draw
mode). Every row is found in exactly the GT3 builds it names and in no Concept build: no false match. On the Concept
builds no table's routine and no rule's routine is found; by content the spec lock curve, curve B, the stick curve and
the HUD messages are found with GT3's own bytes (the European Tokyo-Geneva disc's messages are PAL's, the others'
NTSC-U's) and the tyre effect block with other values; the other 15 tables are not found, so their races run on GT3's
tables and NTSC-U's rules (no recognised piece names a release; the European disc's GT3-format car database races; the
others' databases can't be read yet). A modified PAL GT3 disc checked in 2026-11 has PAL's program byte for byte: every
table at its routine with GT3's values, every routine PAL's; its changes are in GT3.VOL and in emulator cheats that write
the running game's memory, which the engine does not read.

**PAL's other changes** (not the rate): its rubber band (virtual 4 `0x1ECEB8`) caps every race mode's time scale at
1.3, and its i.LINK battle has no cap of its own (NTSC-U's `0x1F0478` caps at 1 + the speed-up amount); the AI's
oversteer indicator (§4.6, `0x1E3708`) is 0 below 25 km/h; the pad vibration (`0x1DF2B0`) and the steering-wheel force
(`0x1E0068`) of a car the AI drives (control source 1 / 2 or a driver kind) are left at 0 / 0x80 without computing them
(NTSC-U computes them for every car and only its pad writer `0x1E0618` skips those cars); the licence's stop speed is
1.5 km/h (NTSC 1 km/h, `0x1EF488`); a lap equal to the record is no longer a new record (`0x1EFDE8`). The race object at
race info + 0x3C is 0x3FC bytes longer past + 0x20000 (the display's and ghost's side).

**NTSC-J** differs from NTSC-U in: the conductors (no virtual 4: the rubber band's scale is called directly, and every
later virtual is one slot lower; one battle conductor, with the split-screen battle's virtuals and its handicap only with
exactly two cars, `0x1ED888`, and whose lap handler (NTSC-J `0x1ED9A8`) hands an i.LINK battle (a RaceLinkBattle race object)
to the base conductor's and treats every other battle as NTSC-U's split-screen `0x1F0118`; its wear answer hands the race info's word on as the setting, so setting 1's curve is
reachable (knees 80000 / 300000, max 420000), and its catch-up answer gives setting 2 for any set word); the angle wrap
`0x1D8C00` adds or takes 2 pi a turn at a time (NTSC-U takes whole turns at once beyond 3 pi); a player's run-up start has
no shift lock-out (`0x1DDAD0`); the collision commit `0x1FB6D8` keeps the heading and yaw rate as they are (no wrap
`0x1FB628`, no yaw-rate clamp `0x1FB698`), the wall friction `0x1FE660` and push-out `0x1FDA10` have no zero-length
tests; the race display has no general time event (NTSC-U's 14), so the race event queue's ids from 14 on are one lower (§13.1); the replay's
lap-start restore (`0x1EE6D8`, NTSC-J `0x1EBF60`) does not rebuild the car's collision body (NTSC-J has no `0x1FA710`); the race object's layout differs (the ghost
switch word at + 0x1B778, no race info + 0x70 test in the draw mode `0x1E0668`). Its VU0 random draw `0x245D5C` first
copies its argument into vf1 (the same R sequence).

## 13. The race display's events, the time records, the data logger and the free run's recordings [C; ported in `sim::exact` `race_flow.h`, every call checked in `race_tick_exact` and `race_exact_lockstep` on all three programs]

The race flow's outputs besides the cars: what the race display shows (its event objects' posts to the race's queue),
the time records (the race entries' and the session's), the data logger's table and the free run's lap recordings and
ghost car.

### 13.1 The race event queue

Race info + 0x84 holds the race's event queue: entries {s16 type, s16 priority, u32 word}, their count at + 0x818 and a
count per type at + 0x800 + type (the display takes and clears them every frame). The race flow posts with `0x1796C0`: 1
the end countdown's end, 4 a hand-back to the player, 5 a car finished (car, priority 2), 9 a licence test failed, 10 the
time trial's course-out, 13 a data logger mark ended (the car's index), 19 a new record (with a result). The race
display's event objects post with `0x1901B8`: the type is the object's virtual 1 (11 lap time, 12 check point, 14 general
time, 15 time difference, 16 lap difference, 17 speed, 18 information), priority 2, and the word its virtual 2 packs:
the car (4 bits, clamped to 0..15), then for 11 the lap (0..999) at bit 8; 12 the sector (0..15) at bit 4 and the lap at
bit 8; 14-16 the value (ms or laps, clamped to ±0x7FFFFFF) at bit 4; 17 the speed x 10 (0..9999) at bit 4; 18 the note
(4 bits) at bit 4. The packing sets only those bits; the rest of the word is whatever the object held (not defined).
NTSC-J has no general time event, and its later types are one lower (14 time difference, 15 lap difference, 16 speed,
17 information, 18 new record).

What posts them (all in the race step's loop after phase 13 or the conductor's virtuals 1 / 13 / 14):
- the lap time `0x1EC620` (each lap of every mode but the machine test's split, which uses it for its time): the car's
  best lap car + 0x16C4 keeps the lower (`0x1EC4C0`), then 11 (the lap number) and 14 (the lap time);
- the sector time `0x1EC520` (each sector line, the licence's laps as sectors): 12 (lap, sector) and 14 (the time since
  the car's last crossing);
- the time trial's and free run's differences to the session's best (`0x1EC758` a sector, `0x1EC818` the lap): 15,
  not with the race-flow byte set (NTSC-J: also then) nor for a car out of the course, only when the best exists;
  free practice's lap (`0x1F22C0`): 15 to the session's best and 18 (2) for a new best;
- the single races' and battles' sector order (virtual 15 `0x1EF920`, below) and its display (virtual 1 `0x1EF268`): 15
  for every car behind the lead car (its time behind) and once for the lead car (ahead of the second), 16 for a car
  that reaches a sector laps behind the lead lap;
- the machine test's top speed `0x1EC8C0`: 17 in the display's unit ([0x2F59D8]: 0 km/h, m/s x 3.6; 1 mph, x 3.6 x
  0.62137; else m/s); free practice's start 18 (3); the pit lane's 18 (1 in, 6 out).
PAL: a ghost car's lap, sector and speed events are not posted at all (its best lap is still kept).

### 13.2 The time records

Each race entry has a lap list (RaceLapTime, entry + 0x210: counted-only flag, count, dropped, the laps, at most 1000 kept,
the oldest dropped; reset at every run's start `0x17B4E8`), its best lap's splits and its current lap's (+ 0x11C8 /
+ 0x11DC: four sector times and the lap), its best lap (+ 0x11F0) and the machine test's top speed (+ 0x2100); the
session's best splits and lap are race info + 0x8AC .. + 0x8BC (+ 0x8BC is the record time a new record beats; the
visit's: not reset per run). "None" is 0x157529FF throughout. A car's times stay out of the records (`0x1EC4E8`) for a
ghost, with the race-flow byte set, and while race info + 0x7C or + 0x6C (a replay) is set.
- A lap (`0x1EC620`), unless locked: into the lap list (`0x17B508`); unless the car is out of the course: the entry's best
  (`0x17B6D0`, the lower), the current lap's time, then the entry's best splits take the current ones when its lap is
  better, and the session's the same (`0x17AF10` -> `0x1786E8`, `0x17B810`). NTSC-J keeps a locked car's lap in its
  entry's splits too (only the list, the best and the session's skip it).
- A sector (`0x1EC520`), unless locked or out of the course: the current lap's sector time (`0x17AEA8`). NTSC-J has no
  lock test there. `0x17AEA8` stores at entry + 0x11DC + 4 × sector with no bound [C, 2026-10-26]: a licence test counts its
  sectors on over its laps (a type 1 or 2 test lapping on), so its fifth sector on writes the current lap's time (+0x11EC),
  the entry's best lap (+0x11F0) and the words after them.
- The machine test's top speed (`0x1EC8C0`) unless locked.

### 13.3 The single races' sector order (conductor + 0x8000 + 0x3AF8 .. + 0x3BA7)

Per sector s (0..3): the lead lap (s16, + 0x3B78 + 2 s; -1 at the start, `0x1EE7E8`), the cars that reached the sector
on it (+ 0x3B80 + s) and those posted (+ 0x3B84 + s), their times in order (+ 0x3AF8 + 32 s + 4 p, none at the start) and
their cars (+ 0x3B88 + 8 s + p). Virtual 15 (`0x1EF920`, at each sector line and the start line) restarts the sector's
list for the first car on a new lead lap, inserts a car on the lead lap by its time (the cars' bytes are not moved on an
insertion: the original's), and posts 16 for a car laps behind; virtual 1 (`0x1EF268`, every step) posts the time
differences of the new entries.

### 13.4 The data logger (race info + 0x70 / + 0x74)

+ 0x70 set: the data logger plays a run (the draw mode reads it, §10.8; its marks are timed: each mark's end posts 13).
+ 0x74 set too: its table is recorded (the analysis pass over a replay). Each car's table is its input object's (car +
0xC) + 0x12C, entries of 0x1B00 bytes: +0x02 the crossing's remainder of the step (50 less the step's 50ths before it;
PAL 60), +0x03 (clock + 50) mod 3 (PAL + 60), +0x04 the input object's frame (+ 0xFC; less the first entry's when its
+ 0xC8 is 2), +0x08 the frames to the entry's end, +0x0C the crossing's fraction, +0x10 the time to the entry's end,
+0x24 the car's record (`0x1EE638`), +0x1700 its collision body's points (`0x1FBA08`). The marks are the road's markers of
group 1 (`0x228B00`); entry k is the mark count F + 0x493 (the licence's laps test counts on over its laps, the course's
marks + 1 a lap). An entry starts (`0x1E0940`) at a mark, at the first crossing of the time trial and free run, and at
the countdown's last step in the machine test and a standing licence test (virtual 6, `0x1EFCD8`), and ends (`0x1E0A18`)
at the next mark, the split line, the lap and the licence's stop. With + 0x74 the free run's lap handlers let the AI
drive car 0 on (as a ghost's).

The Analyzer's pass over a replay (`0x135788`, the logger menu's "processing") [C, 2026-10-26] sets + 0x74, runs the race's
frames (its virtual 2) until the replay ends, and after each samples car 0 (track A) and, when the ghost's playback has a
recording, car 1 (track B): the distance d = F + 0x3E4 + (laps F + 0x434 − 1) × lap length (the first sample at most 0; d
never goes back), its speed (car + 0x16A8), throttle (F + 0x428), brake (F + 0x42C), gear (F + 0x436) and drawn position
(`0x1EB400` / `0x1EB490`: car + 0x15A8 × flow + car + 0x158C × (1 − flow), and z the same). The distance is cut into bins of
w = max(lap / 16383, 0.5) m from 0 (at most 16384: `0x4000`); each step adds to the open bin the area of each value's
line between the two steps over the bin (`0x135638`); a bin d passes ends as a sample of 8 bytes: u16 gear (3 bits) |
min(speed area × 40 / w + 0.5, 8191) << 3 (`0x27AEA0` truncating), u8 min(throttle area × 255 / w + 0.5, 255), the same
for the brake, s16 x and z (cvt.w.s) at the bin's end (`0x135600`, linear between the steps). At the end every sample's
speed, throttle and brake become (previous + 2 × own + next) >> 2 of the unsmoothed values. The status panel's and the
graph's own computations are not traced.

The Analyzer's race [C, 2026-10-26]. Over a time trial's, licence test's or machine test's two tracks the pass is one race
of the run's race object with its logger devices (RaceSolitaire::vf58: the devices at + 0xCB50 and + 0xCC98, both
playing, each in mode 0 over a whole pool, §13.6): car 0 plays track A's recording from pool 0 and car 1, a ghost car with
track B's car, plays track B's from pool 1 (without track B the ghost's playback is empty and only car 0 is sampled). The
race runs until car 0's playback ends; each device's logger table is at its + 0x12C, so both cars get their entries at
the marks.

The REPLAY view's jump (`0x1873B8`, called by the view `0x136838` with the logger entry k of the sector chosen): race
info + 0x50 (the countdown's start request) cleared, then car 0 and, when the ghost's playback has a recording (its
+ 0x140 clear), car 1 put back on entry k of their tables (`0x1E0AD8`): the car record from the entry's snapshot as the ghost car's restart
(`0x1EE6D8`: all but its ghost flag, index and control source; its body rebuilt and its points from the snapshot,
§13.5), and its device's playback sought from the recording's start to the entry's frame (+ 0x04; the first entry of a
table counting from it, + 0xC8 2: frame 0; `0x1F47D8` reads the frames and drops them). The race then steps on from there:
the cars are simulated, reading their recordings from that frame; there is no replay from the lap's start and nothing
else of the race is restored (the race clock, the time records, the sector order run on). After a jump the cars' motion
is the pass's at the same frame, bit for bit, as long as the recordings play (checked in the harness on jumps to
entries 0, 3, 5 and 10 of the time trial's eleven and to the licence and machine tests' one, 600 steps after each).

### 13.5 The free run's lap handlers and its ghost car

The free run records car 0's laps through the race object's (race info + 0x3C) replay recorders (its virtual 27: 0x100
the player's recorder, 1 the ghost's playback; two sets, chosen by the object's + 0x217A8 or race info + 0x70). Virtual 17
(`0x1F1540`, the first crossing): car 0's run-up hand-back (the AI drove it: event 4), the ghost car put on the ghost's
lap start (without the logger, with two cars), the lap's start into the lap recording (`0x1F1348`, race-flow byte clear),
the AI's drive-on for a ghost car or with race info + 0x74. Virtual 18 (`0x1F16B8`, each lap): the lap difference (15),
the lap time (11 / 14 and the records), then for car 0, with the race-flow byte clear: the lap recording closed
(`0x2106D8`); a usable recording (not closed before, `0x210690`, and under 36,000 frames; NTSC-J: the recorder's + 0xF4
clear instead) takes the lap's time into its header (+ 0x10); a lap under the best lap's (+ 0x124) becomes the best
(`0x210700`) and a new record (race info + 0x4C); under the ghost's too, or with the ghost's playback empty (+ 0x140), it
becomes the ghost at once (`0x17AAD8 (1, 1)`, race info + 0x48), so the ghost car restarts on this lap; then the ghost
car's restart (without the logger, two cars), the next lap's start into the recording, the drive-on as in virtual 17.

The race object (and with it the recorders: the lap recording, the best lap's store, the ghost's playback) lives for the
free run's visit [C, 2026-10-26]: the free run's driver (`0x12FF48`) builds `RaceFreeRun` once and runs each run with its
virtual 9 (`0x1893D8`), which keeps a ghost playback that has a recording (+0x140 clear; an empty one is rewound,
`0x1F4FF0`); so a restart's first crossing puts the ghost car on the visit's ghost.

The ghost car's restart (`0x1F1400`): car 1 becomes the recording's lap-start snapshot (`0x1EE6D8`: the whole record but
its ghost flag, index and control source; its last crossing's time (clock + the snapshot's remainder) / 3; its collision
body rebuilt from the car, `0x1FA710`, and its points from the snapshot, `0x1FBB50`; NTSC-J does not rebuild the body),
its laps the crossing car's, its playback restarted (`0x1F52B0`); its step accumulator (F + 0x404) is the crossing's
fraction less the snapshot's, and when that is below 0 it gains 1 and (without the logger, the ghost car of index 1) the
race step runs once more inside this one for it: `0x1F39D8` in its entry mode 1 (the race-flow gate over every car, the
phases from car 1, walls only for its body, no proximity, the loop over the race order from position 1, none of the
virtuals 13 / 1 / 14, the gap matrices or the ranking; the rubber band, the pad output `0x1E0618` and the end countdown
`0x1F2B18` run). The ghost car reads its pad from its playback (§13.6): one recorded frame a step, one more in that extra
step.

Corrections to earlier sections: race info + 0x70 / + 0x74 are the data logger's (not a ghost recorder's); the free run's
AI drive-on needs a ghost car or + 0x74 (not the free run alone).

### 13.6 The pad frame recordings (AutomobileControlRecord) [C, 2026-10-26; ported in `sim::exact` `control_record.h` and `race_flow.h` `SoloRecorders`, checked in `control_record_exact` on all three programs and `race_ghost_exact`]

What a car's input device keeps of its driving: the solo modes' replays and ghosts, the free run's lap recordings and the
Analyzer's tracks are recordings of the frames the pad read (`0x210740`) gives the car's phase 3. The disc's
demonstrations are such recordings too (`race_exact_demos` plays them with the original's own playback).

**The frame.** The device's 16-byte frame: + 0x00 the analog flags (u8), + 0x01 the controller kind (u8), + 0x02 the
shift request (s8), + 0x04 the steering (s16, ±4096), + 0x06 a halfword nothing sets, + 0x08 the throttle, + 0x0A the
brake, + 0x0C the back axis, + 0x0E the third axis (u16 each). Each field has a bit in the change mask; in the order they
are coded: 0x100 (+ 0x00), 0x80 (+ 0x01), 0x10 (+ 0x02), 0x02 (+ 0x04), 0x01 (+ 0x06), 0x04 (+ 0x08), 0x08 (+ 0x0A),
0x40 (+ 0x0C), 0x20 (+ 0x0E).

**The block and the stream.** A recording is a block: + 0x00 the frames recorded, + 0x04 the stream's bytes + 8 (both
written when it closes), + 0x08 the stream, filled from each byte's top bit down (BitStreamBuffer `0x26C8B8`: write
`0x26C9B0`, read `0x26C930`). Two numbers are coded in it:
- a count c ≥ 1 (`0x1F4830`, read `0x1F4570`): with 2^s ≤ c < 2^(s+1), s one bits, a zero bit, then c's low s bits;
- a signed difference d ≠ 0 (`0x1F48D8`, read `0x1F45D0`): its class k in 3 bits, its sign (1 negative), then |d| less
  the class's start in 2k + 1 bits; the starts are 0, 2, 10, 42, 170, ... (each class holds 2^(2k+1) values; the
  playback's table `0x2D8EE0`). A difference of 43,690 or more (class 8, only a halfword can change that much) is
  written with its class's low 3 bits, which the playback reads as another class; no pad frame reaches it.

**Recording** (`0x1F4998`, each frame unless the recording is closed). The last frame's steering is first replaced by
its linear prediction (`0x1F4518`): when |last| ≥ 16, 2 × last − the frame before's, clamped to ±0x1000 (last's own
steering is kept as the frame before's for the next prediction). The fields that differ from that last frame make the mask. A frame with an
empty mask, not the recording's first, only lengthens the run; otherwise the run so far (when there is one) is written
as a count, then the mask + 1 as a count and each changed field's difference (new − last: + 0x02 and the halfwords as
signed, + 0x00 and + 0x01 unsigned) in the order above, and the run becomes 1. The frame then becomes the last. After a written change,
when the stream has come within 0x40 bytes of the block's capacity, the recording closes itself. Closing (`0x1F4C00`,
once, for a recording with a frame): the run as a count, then a count of 1 (an empty mask), and the header (frames,
bytes + 8).

**Playback** (`0x1F4660`). Each read counts a frame; when the count reaches the header's frames the playback ends: that
read and every one after it gives an empty frame, so a recording's last frame is never played. Otherwise the last
frame's steering is predicted as in recording, then either one frame of the current run is used up (the frame is the
predicted last) or the next change is read: the mask (count − 1), each changed field's difference added to the last
frame's, and the run that follows (count − 1). A seek (`0x1F47D8`) rewinds the playback (`0x1F44C8`) and reads n frames.

**Managers and memory.** A recorder is bound by a manager (`0x1F4ED0`) to the game's record memory (`0x1F4D80`: two
pools of 0x20000 bytes, each four stores of 0x8000). Mode 0 takes a whole pool (the block at its start, the stream's
capacity 0x20000 − 8); modes 1 (the stores in turn from store 0) and 2 (one store) take a store: its first 0x1B00 bytes
a car snapshot (the logger entry's layout, §13.4; + 0x10 its time), then the block (capacity 0x64F8). The device's mode
(`0x210658`): 0 live (the frame the pad read gives is recorded), else playing (the frame is the recording's next; with no
recording, + 0x140 set, an empty frame).

**The solo race objects' devices** (RaceSolitaire `0x186E10`: the time trial, licence test and machine test; RaceFreeRun
`0x189138` derives from it): the player's (+ 0xC8C0, live, mode 0 on pool 0: the run's recording, its replay); the
ghost's (+ 0xCA08, playing, mode 0 on pool 1: a whole run; the free run's mode 2 on pool 1's store 0: a lap); the
logger's pair (+ 0xCB50 on pool 0 and + 0xCC98 on pool 1, both playing in mode 0: the Analyzer's race, §13.4); the free
run's lap recording (+ 0x21744, mode 2 on store 1: car 0's frames from its lap start, recorded by the conductor's
virtual 20, `0x1F14E8` -> `0x2106B0`, §13.5) and best lap (+ 0x216E4, store 2, its time "none", 0x157529FF, at the
start). A run's start (RaceSolitaire::vf9 `0x187598`, RaceFreeRun::vf9 `0x1893D8`, `0x17A970`) rewinds the lap's and
best lap's recorders, puts an empty free run ghost back on store 0 and restarts the player's and the ghost's devices
(`0x1F52B0`): the player's records the new run from its start, the ghost's plays from the start. The race's end closes
every device's recording (`0x17AA48`; RaceSolitaire::vf36 the lap's and best lap's).

**The ghost taking a recording** (`0x17AAD8 (race object, kind, now)`): kind 0, a whole run: pool 0 (the player's
recording) copied over pool 1, the ghost's playback; kind 1, the free run's best lap: store 2 (its snapshot and
recording) copied over store 0. With `now` (the free run's lap handlers, §13.5) only when the Options menu's Ghost is on
(off: nothing happens, and the quick menu's "Update Ghost?" takes it later without `now`): the ghost's playback
restarted, race info + 0x48 cleared, the player's race entry copied into the ghost car's (`0x177488`) and that entry's
lap list restarted counting only (`0x17B4E8 (1)`).

**The ghost car** reads one frame of the ghost's playback each step (the free run's: one more in its extra step,
§13.5), so a whole-run ghost drives the recorded run's frames but the last, then empty frames.

## 14. Gran Turismo Concept: the later builds of the engine [X where a diff harness test is named; the rest from the map, marked]

Gran Turismo Concept (2001 Tokyo, 2002 Tokyo-Seoul v1.02 and v2.00, 2002 Tokyo-Geneva for Europe and for Asia) is the same
source tree as GT3, built later from GT3's PAL release's line with another compiler. `sim::Program` names the five builds
after GT3's three releases; `ProgramData` tells each by its own code (§14.1). **All five race on their own core** (2001
Tokyo, 2002 Tokyo-Seoul v1.02 and v2.00, and since this pass 2002 Tokyo-Geneva for Europe and for Asia): every routine of
their race step that differs is ported and the whole step is proven on each disc (§14.4), so their `ProgramData::known()`
is true and their races run their own rules (`own_core_ready()` in `program_data.cpp`, by the recognized program; the diff
harness checks them as `release_behaviour(program)`). A race record keeps the program's fingerprint, which tells a later
build on its own core from the same disc on GT3's (the fingerprint mixes the switch in): a replay made before the switch
is refused (`concept_disc_own_core_race_and_replay` on 2001 Tokyo, both Tokyo-Seoul discs and both Tokyo-Geneva discs).

Addresses below are each build's own: GT3 NTSC-U / Concept 2001 Tokyo / Tokyo-Seoul v2.00 / Tokyo-Geneva Europe /
Tokyo-Geneva Asia.

### 14.1 Identity, tables

- All 20 program tables are found through the routine that addresses them on every Concept build (tier 1; the compiler
  forms some addresses as a load's own offset from the upper half, and one table by a field 4 bytes in: `TableSite::off`).
  Their routines' code hashes are per build or shared by builds whose code is the same; together with the rule rows they
  tell every pair of the eight programs apart (10 rows or more).
- Tables equal to GT3's on every build: 14 of 20. The ones that differ serve the disc's bytes ("changed on this disc"):
  the static block (bytes +0x07 50 -> 100, +0xAE 20 -> 7, +0xB0 140 -> 60 in the part the core reads; other data from
  +0xBC), the tyre effect block (its last 8 bytes, not read), the gear kinds (the last of 88 entries 0, GT3 24), the
  controller settings on Tokyo-Seoul v2.00 and Tokyo-Geneva Asia (+0x34 200 -> 250), the tyre effect scales on
  Tokyo-Geneva (3 of 8 floats one unit in the last place off). Tokyo-Geneva Europe's HUD message times are GT3 PAL's.
  Online races and GT3's demos take GT3's values (`gt3_tables()`), as on a modified GT3.
- Three tables only the later builds have (`ProgramData::Table` after `Count`; `has()` is false on GT3's discs, whose
  fingerprints do not change): the special cars' labels (`SpecialCars`, 0x80 bytes from 32 bytes before the first
  build's first label; found through the spec builder), AWD system 3's settings (`Awd3`, 0x14 bytes of small data, found
  through the layout routine's byte load from the global pointer) and special car 1's mood tables (`PodMoods`, three
  rows of 16 bytes). Each is served as the disc holds it; all three are found by their routine on all five builds.
  Two more since the fourth pass, found the same way: the stepless gearboxes' settings (`Stepless`, 0x10 bytes of small
  data, through the gearbox routine's byte load: +0 / +1 kind 1's ratio rates at full and no throttle, 40 and 15 % a
  second, +0xC / +0xD kind 2's rates down and up, 150 %; the routine keeps them x 0.01 as floats at +4, +8, +0x10, +0x14)
  and the race switches (`RaceSwitches`, 6 bytes, through the stop zone's set-up: +0 the zone's start 608 m as an s16 and
  +2 its length 33 m, +3 .. +5 the ASCC's three switches, all 1). One more on the 2002 Tokyo-Geneva builds alone
  (`Kind7Ride`, 8 bytes of small data, found through the race initialization's routine that converts them, Europe
  `0x1DC5B0` / Asia `0x1DC548`; its comment `later3 0x3367B8` tells `tools/re/locate_spec.py` it starts at the fourth later
  build): +0 on (1), +1 the travel 25 mm, +2 the lift 50 %, +3 / +4 its share per axle 75 %, +5 the drop 15 mm reached
  between +6 125 and +7 150 km/h (special kind 7's ride, §14.3).
  Two more since the sixth pass: the single race's flying start grid (`FlyingStart`, 0x15 bytes of small data, only 2001
  Tokyo and both Tokyo-Seoul builds: its comment `later-2 0x334C90` tells `locate_spec.py` the first to the third later
  build; found through the conductor's virtual 25, which forms the address of its +8): +0 the line of car i mod 6 (6, 2,
  1, 1, 2, 6), +8 the step from car i to car i + 1 (20, 8, 20, 15, 8 m, i mod 5), +0x10 the speed step (5, 2, 5, 1,
  3 km/h); and the layout 7 system's settings (`Hybrid`, 0x1B bytes after the stepless settings' flag, Tokyo
  `0x334C51`, the same bytes on all five builds, found through the set-up routine's byte loads; their meaning in §14.5).
  One more in the seventh pass: the HUD messages 0x10 and 0x11 (`PodMessages`, two 8-byte records right after GT3's 16,
  the pod race's prompts, display time 0 on every build), placed beside the HUD messages' table (tier 2: no routine
  addresses them apart; only on a later build, so GT3's discs and fingerprints are as before).
- Tokyo-Geneva Europe's program holds PAL's 50 Hz constants (from the map: 1/50 for 1/60 among its constants);
  `release_rate()` gives 50 for it (its release behaviour, what the diff harness runs the disc's races at; a race of the
  game steps at 60 by default, as on GT3's PAL disc), proven by `race_tick_exact` and `race_start_exact` on the disc:
  every count the ports take from `pal_rate()` matches. Tokyo-Geneva Asia steps at 60, with its 1 / 60
  folded one unit lower (0x3C888888: the step length, the view's lag).

### 14.2 Two properties of the 2002 Tokyo-Geneva builds

- **Constants.** Where GT3 has a decimal constant or a constant expression, Tokyo-Geneva's value can differ by one or two
  units in the last place in either direction: its compiler folded constants in arithmetic that rounds toward zero (pi
  0x40490FDA for GT3's 0x40490FDB; 10 / 3.6 two units lower; the slope 1 / (10 / 3.6 - 1 / 3.6) two units higher). The
  differences are not derivable from the decimal text; each port reads the build's constants at the aligned positions of
  the routine and keeps them as that build's constants.
- **Soft doubles.** Tokyo-Geneva's double arithmetic rounds toward zero where GT3's and the earlier Concept builds' round
  to nearest: its pack routines cut the guard bits off and its multiply has no rounding step (`Rule::SoftDouble`,
  recognised by the multiply's code; `ee::Fp::LaterTowardZero`). The same rule gates the ports' folded constants
  (`toward_zero()`, `folded()` in `sim/exact/build_math.h`): each value is the build's own, read from its routine.

Every Concept build's soft doubles differ from GT3's at the edges (`ee::Fp::Later`, ported): the float-to-double
conversion has no infinity / NaN class (§14.3), a result below the smallest normal is a signed zero where GT3 keeps a
denormal, and the narrowing tells only the zero class apart (exponent 128 an ordinary one, above it Inf). Proven by
`libgcc_double_exact` on every disc (0 differing); not compared: 2001 Tokyo's and Tokyo-Seoul's narrowing of a double
with exponent 2047, whose unpack routine leaves the exponent field unset (the result reads the caller's stack; 837
samples, counted). Tokyo-Geneva's unpack sets it (0x3FF) and its narrowing of one is compared.

### 14.3 Rules ported and proven

Each is a `Rule` after GT3's 24 (`sim/program.h`), recognised by the routine's code hash per build, with a branch in the
port; the harness test named runs the disc's routine against it, bit for bit, on Tokyo, Tokyo-Seoul v2.00 and both
Tokyo-Geneva discs.

| Rule | Routine | What differs | Proven by |
|---|---|---|---|
| `Atan2` | `0x269FC8` / `0x26BD48` / `0x26FA60` / `0x26BB40` / `0x26C060` | GT3 returns 0 whenever x + y == 0; Concept only when both are 0. Tokyo-Geneva's pi / 2 and pi are 0x3FC90FDA and 0x40490FDA. atanf, sinf, cosf are the same in every build | `libm_atan_exact`, `libm_trig_exact` (pass on all four) |
| `FloatToDouble` | `0x27AFA0` / `0x27ABF0` / `0x27EB18` / `0x27AD70` / `0x27B290` | exponent 255 is an ordinary exponent (2^128 x 1.f), no infinity / NaN | `libgcc_double_exact` (its conversions) |
| `EngineCondition` | `0x1DAD28` / `0x1D5928` / `0x1D7C78` / `0x1D60A8` / `0x1D6048` | a stub that returns 1: no engine wear or oil factor on the torque (the block `0x1DAB50` builds is not built by it). Recognised by GT3's routine's absence | `prep_leaves_exact` (the factor) |
| `SlipAngle` | `0x1EA348` / `0x1E0F30` / `0x1E38F8` / `0x1E16C0` / `0x1E1658` | Concept's atan2f; Tokyo-Geneva's fade speeds 0x3E8E38E3 / 0x4031C71B and slope 0x3ECCCCCF (Europe `0x334270`, `0x33426C`, `0x334274`) | `prep_leaves_exact` (the slip angle) |
| `SpecBuild` | `0x1DA308` / `0x1D52E8` / `0x1D75A8` / `0x1D5A00` / `0x1D59A0` | the default of spec byte +0x113 (0 in the record) is 90, GT3's 100 (Tokyo `0x1D5584`); a special kind in spec +0x11 for the cars whose labels the program names (kind 1 and 2 on 2001 Tokyo and Tokyo-Seoul v1.02; from v2.00 on also two cars of kind 7 and kinds 8, 9, 10), which the converter copies to P +0x32 | `model_car_params_exact` (every car's spec the same on all four: 2,044 / 1,993 / 1,541 / 1,535 runs) |
| `BodyBuild` | `0x1FA740` / `0x1FA898` / `0x1FD098` / `0x1FA148` / `0x1FA0E0` | Tokyo-Geneva's chamfer constant 0x3E4CCCCC (Europe `0x3345B8`, Asia `0x334D78`); Tokyo and Tokyo-Seoul as GT3 | `model_collide_exact_build` (pass on all four) |
| `DriverSpread` | `0x1F6380` / `0x1F5D30` / `0x1F8530` / `0x1F5718` / `0x1F56B0` | the event's driver overrides spread the computer's drivers by their car's index (corner speed, braking, yaw gain, speed offset) in every race; GT3 only in configuration 2. Tokyo-Geneva has no speed offset of the event's (the rule block's +0x18 .. +0x1B are its classes' grip percentages, below): the spread's alone | `race_start_exact` (2001 Tokyo: every computer's car of 4,918 set-ups) |
| `MovingStart` | `0x1DD8A8` / `0x1EBED0` / `0x1EE5D8` / `0x1EBF98` / `0x1EBF38` | a car whose AI segment mode is 0 (a straight) starts with pedal and throttle at 1; the record's ratio word (+0x1328) is set to 1 and a gearbox of kind 1 takes the engine speed through `0x1D9948` (`stepless_ratio`: above the hold speed the ratio falls, down to the last gear's relative ratio). The gear for a speed (`0x1E4E80`, 2001 Tokyo `0x1F0C88`) is first for a gearbox of kind 1 (with `Driveline`) | `race_start_exact`'s moving starts (2001 Tokyo: all 1,156 the same) |
| `GroundMiss` | `0x2280C8` / `0x229398` / `0x22C468` / `0x228B10` / `0x228AA0` | the height of a wheel record off the road is 0xFFFFFFFF (the word at Tokyo `0x334F14`), GT3's 0xFF7FFFFF (`0x352AFC`) | `model_ground_course` (pass on Tokyo and Tokyo-Seoul v2.00, 79,540 samples each) |
| `EngineCondition` (its second part) | `0x1DAD18` / `0x1D5918` / `0x1D7C68` / `0x1D6098` / `0x1D6038` | the step's wear distance is 0: the engine wear and oil counters do not advance in a race (their limits still apply) | `race_tick_exact`'s speed bookkeeping (Tokyo: 472,689 calls the same) |
| `Oversteer`, `PadRumble`, `SteerForce` (GT3's rules) | `0x1E3708`, `0x1DF2B0`, `0x1E0068` | the later builds' routines follow PAL's variants (none below 25 km/h; a car the AI drives left at 0 / 0x80): recognised by their code as PAL's rule (`RuleSite::as`) | `race_tick_exact`'s steer indicators and pad vibration (Tokyo: 0 differing); the steering force: 11,731 of 472,689 calls still differ (its counter, §14.5) |
| `GearSpec` | `0x232E18` / `0x235498` / `0x238928` / `0x234D88` / `0x234E40` | the gear record's +0x30 is the gearbox kind, spec +0xE1 | `model_car_params_exact` (the spec, all four) |
| `Layout` | `0x1E2700` / `0x1DCE80` / `0x1DF848` / `0x1DD860` / `0x1DD7F8` | the showroom adds a layout 7 car's motors to the peak power (the layout 7 system's row); AWD system 3: the gain is also scaled by the settings' byte +0 over 100 and the capacity is their byte +1 x 10 x 9.8 (not the spec's byte); system 5 is layout 7 (GT3: none), system 7 layout 8 (GT3: 7); in the race step, the automatic box's traction veto and the TCS's wheels take layout 7 as the rear-driven layouts and layout 8 as GT3's 7 | `model_car_params_exact` on 2001 Tokyo (passes: P of 2,044 runs), Tokyo-Seoul v2.00 (P of 1,993 runs) and Tokyo-Geneva (P of 1,604 / 1,598 runs, with its constants: `SoftDouble`); the veto: `race_tick_exact`'s pad drive with layout 8 on Tokyo-Geneva (9 of 1,876 calls differed before) |
| `Driveline` | `0x1E1C98` / `0x1DC278` / `0x1DEC40` / `0x1DCCD8` / `0x1DCC70` | D +0x12 the gearbox kind (3: one gear; 1 needs two; at most 7 gears), D +0x01 set for layout 5 with unequal tyre radii, the differential defaults for layouts below 9; kinds 1 and 2 (stepless): the launch speed is the idle speed and the ratios from the second on are the gear words over the first's | the same |
| `DriveClass` | `0x1F5EF0` / `0x1F5808` / `0x1F8008` / `0x1F5270` / `0x1F5208` | AWD system 5 is class 0 | the same |
| `AwdGain` | `0x1E5998` / `0x1F18B8` / `0x1F3FF0` / `0x1F1528` / `0x1F14C0` | the lateral fade from the settings (start and floor, bytes +8 / +0x10 over 100; GT3 0.25 and 0.25), the torque's sign turned in reverse gear, minus the full-transfer value for a negative torque with the settings' switch (+2), the full-transfer test on the torque plus both axles' terms | `race_tick_exact`'s gain port (2001 Tokyo and Tokyo-Seoul v2.00: 33,633 calls each, 0 differing; Tokyo before 11,204) |
| `HybridStep` | none / `0x1DA0A8` / `0x1DC838` / `0x1DA848` / `0x1DA7E0` | after phase 1: a layout 7 car's state byte 5 (the flow display) cleared | `race_tick_exact` with a layout 7 car (below) |
| `FlyingGrid` | none / `0x1E73E0` (virtual 25) and `0x1E7530` (virtual 22) / v2.00 `0x1E9EC0` and `0x1EA010` / none / none | the single race conductor: an event with a start distance (not 0) and a start speed puts car i in mode 1 at the distance plus the first i of the `FlyingStart` steps and the speed plus the first i speed steps; then (virtual 22, before the ranking) car i again at its distance on the table's line (the routine that places a car on the line takes the line, -1 the main one), posed there (`0x1EB848`, GT3's `0x1DD210`; its speed and the rest of its start kept) and given that line at once (`0x1F89B8`). Tokyo-Geneva's conductor has GT3's virtuals again: its rows are absent, as NTSC-U's | `race_start_exact` (2001 Tokyo: 18,750 records; Tokyo-Seoul v1.02 and v2.00: 20,051 each; 0 differing; 422 / 392 / 392 before) |
| `RecordStore` | `0x1F4FF0` / `0x1E9FB0` | the pad recorder's store holds a snapshot 4 bytes longer (the later record's extra word): its recording's block from +0x1B04, 0x64F4 bytes (GT3: +0x1B00, 0x64F8). `ControlManager::later`; since the seventh pass the races' own solo recorders too (`SoloRecorders::later`: the free run's lap, best lap and ghost stores, so a lap's recording closes itself where the original's does; a race takes recorders without recordings in its program's layout, `set_layout`, and refuses a free run's stores of the other; a saved best lap brings its layout to an empty visit). A Concept disc's fingerprint mixes the layout in, so a replay or ghost saved before is another program's and refused | `control_record_exact` on every later build (passes; before, the store's sequences differed from the stream's start on); the solo recorders: unit tests only (`build_rules_solo_recorder_layout`; `race_ghost_exact` runs on GT3's discs) |
| `StuckNudge` | `0x1FB740` / `0x1FB840` / `0x1FE040` / `0x1FB0D0` / `0x1FB068` | the collision step's stuck nudge turns only a pad car (control source 0, car +0x136D) and only while it touches something: a point or corner hit this round, a wall hit, a point held on a wall or crossing one, an edge's corner crossing (each body's word at its edges' +0x1C too, which nothing writes) | `model_collide_exact_step` (every later build: 5 of 680 steps before, now 0 of 4,000 samples; a quarter of the test's cars are computer cars) |
| the layout 7 system (with `HybridStep`; the table `Hybrid`) | none / the set-up `0x1D9CF0` / `0x1DC480` / `0x1DA4F8` / `0x1DA490` (v1.02 `0x1DA378`); in the race step Tokyo `0x1DA218`, `0x1DA380`, `0x1DA6B0`, `0x1DAE50` | §14.5: the showroom's peak power plus the motors' ratings over 0.7355 (`0x1D42F0` through `0x1DB098`, when spec +0xB2 / +0xB3 are 2 / 5); in a race the store, the engine's throttle share and the rear motor in the axle step, the front motors before the torque vectoring, the engine's charging with the clutch open, the store full at the start (`0x1DA060`). Tokyo-Geneva's set-up folds 0.1 and 980 toward zero (0x3DCCCCCC, 0x4474FFFE) and its race routines 1.04791e-4, 9542.84, 0.00100068, 1 km/h, 2 pi, 9.8 and 716.2 (one to three units); the flow display's scale is 6000 but on Tokyo-Geneva Europe, 5000 (its 50 steps a second) | `gt_grid_exact` (the showroom, every build); `race_tick_exact` with a layout 7 car (`super_sports_tmv_01` on every later disc, `acura_dnx_02` on Tokyo-Geneva), every port and whole-step check 0 differing (2001 Tokyo: 2,490 of 5,460 phase 7 calls before); the set-up's 25 floats against the program's globals on every disc |
| `PodMood` | none / `0x1DB2F8` / `0x1DDA88` / `0x1DB980` / `0x1DB918` | before phase 2: special car 1's mood (§14.5) | `race_tick_exact` with the special cars (§14.5): 2001 Tokyo, Tokyo-Seoul v2.00 and Tokyo-Geneva Europe, 0 differing, the rival level tapped in the original |
| `PodStop` | none / `0x1DBAB0` / `0x1DE240` / `0x1DC118` / `0x1DC0B0` | after phase 3: special car 1's stop count | the same |
| `ActiveWing` | none / `0x1DBC48` / `0x1DE568` / `0x1DC430` / `0x1DC3C8` | before phase 12: the special cars' wings (v2.00 on: a routine for kinds 2, 8, 9, 10) | the same |
| `StopZone` | none / `0x1EE280` / `0x1F0988` / `0x1EE190` / `0x1EE128` | the conductor's initialisation: the pod race's stop zone (§14.5) from `RaceSwitches` +0 / +2 for a race of test kind 5 (`stop_zone_setup`; `RaceFlow::zone`) | `race_tick_exact`'s race start check on every later build (the zone our set-up gives against the conductor's +0xE408 after the original's: 5 pod races each, all equal) |
| `PodRule` | none / `0x1EE8C0` / `0x1F0FC8` / `0x1EE7B8` / `0x1EE750` | the single race and split-screen conductors' virtual 14, before the race clock: each car that owes a stop gets it sitting in the zone (information note 7 for a player's car, any car in a race of two), HUD message 0x10 on the zone's ground, 0x11 past its end (a player's car); §14.5 | `race_tick_exact`'s pod races (below): virtual 14 on every step, 0 differing on all five builds |
| `PodLine` | none / `0x1EE7F0` / `0x1F0EF8` / `0x1EE6E8` / `0x1EE680` | first in the base and split-screen lap handlers: a car that owes two stops gets the stop driver (GT3's uncalled `0x1E3DF0`, `stop_lap`), its lap put back, mood 7 for good, the later builds' race event 11 (`kStopsMissedEvent`); the handler returns | the pod races' per-car race flow and the step's loop after phase 13, 0 differing (2 or 3 cars disqualified per disc) |
| `PodSpring` | none / `0x1DBB28` / `0x1DE2B8` / `0x1DC190` / `0x1DC128` | the suspension's spring rate for each wheel (the later builds' caller passes it to the wheel suspension, GT3's reads P's): special car 1's wheels lose their spring as its stop count rises (`pod_spring`; Tokyo-Geneva Europe 6 r for 7 r) | the wheel suspension port's spring argument and result on every later build (34,075 to 79,438 cut rates in the pod races, 0 differing) |
| `HitReport` | `0x1DFEB8` / `0x1D8758` / `0x1DAE90` / `0x1D8FD8` / `0x1D8F78` | the collision report first sets special car 1's mood: a wall 9, kind 2 5, a car by the part hit (`hit_mood`: 5, 8 or 4 for the table's time, from part 8 on for 210 steps, Europe 175) | the hit check from the report's entry on every later build: 3,184 to 6,880 reports, 1,244 to 2,472 moods set, 0 differing |
| `PassBy` | `0x1F59B0` / `0x1F5280` / `0x1F7A80` / `0x1F4CE8` / `0x1F4C80` | none of the later builds' own: NTSC-U's counts, 2002 Tokyo-Geneva Europe's PAL's (its 50 steps a second: the timer's cap 250 for 300, the second 51 steps for 61). The pass-by states at the race step's end (`pass_by_step`: per pair a slipstream timer and a passing state from the relative-position codes); ported this pass, so the game's race gives special car 1 its rival (mood 1) | the pass-by check at its call on all eight programs: `race_tick_exact`'s default races (NTSC-U 247,815 calls, PAL 247,661, NTSC-J 245,320, Tokyo 205,157, Tokyo-Seoul 205,159 each, Tokyo-Geneva Europe 58,213) and the pod races (52,800 calls a disc), 0 differing |
| `LicenceJudge` | `0x1EF780` / `0x1F4808` / `0x1F6F40` / `0x1F4208` / `0x1F41A0` | the licence judge clears a hit and goes on, judges no computer's car, and the licence conductor's answers (soft ground 0, pit mode 0) come with it: `RaceFlowInputs::behaviour`, `pit_mode`, `course_out_rule` follow the rule (until this pass the disc's program) | `race_tick_exact`'s licence races and conductor answers, `concept_licence_judge` |

| `AxleStep` | `0x1E5B98` / `0x1F1B10` / `0x1F4248` / `0x1F1730` / `0x1F16C8` | a stepless gearbox's torque through its ratio word (`0x1D9A08`, §14.5) and the ratio on the engine's reflected inertia; gearbox kind 2's gear index at most 1; with unequal axles (D +0x01) the other axle's wheel torques x its radius over the driven axle's; AWD system 7 is layout 8, layout 7 drives the rear axle and keeps the clutch torque word; the center clutch stays locked while the torque difference is within the requested front torque's magnitude (GT3: the capacity) and opens at minus the full-transfer value; the front torque is the request's magnitude with the sign of the axles' speed difference. Tokyo-Geneva: 98000 is 0x47BF67FF, its 0.05, 0.2, 0.99 and the slip factor's two constants one or two units lower | `race_tick_exact`'s axle step (2001 Tokyo: 472,689 calls, 0 differing) |
| `SoftDouble` | `0x279480` / `0x27AF50` / `0x27EE78` / `0x27B0D0` / `0x27B5F0` | Tokyo-Geneva's fp-bit rounds toward zero (§14.2); with it the folded constants of the ports (`toward_zero()`): the input builders (`statics`, `surface_bumps`, `condition`, `brake_heat_consts`, `steer_feel`, `pad_constants`, `effect_tables`, `pedal_curve_record`), the slip ratio's 1 / 3.6 and 1 / 57.6 m/s, the speed ramp's gain (0x3CA3D70B), the wheel spin's 2 pi, the attitude's pi / 2, pi / 4 and 9.8, the engine's 0.8, 588 and 1 / 588, the clutch slip factor, the ABS floor 0.001. This pass (from a constants survey of every routine of the race step, each build's against 2001 Tokyo's, with gp-relative loads): the parameter converter's (`params_build.cpp`'s `K`: 0.001, 0.1, 9.8, pi / 180, pi / 4 (two units), 2 pi, 2 pi / 60, 1 / 3.6, 0.2, 0.05, 98, 980, 9800, 716.2, 10 / 3.6, AWD system 3's 0.36 (0x3EB851ED, two units higher), curve A's pi / 2 cap 0x3FC90FDA and curve B's span 0x3FC90FD9, the camber step 4 degrees) and the air density's 0.1; the AI's decision, stop driver, start hold, corner speed, braking, speed controller and its throttle law (±10 km/h); the line evaluation's and the AI course set-up's 2 pi, pi and 10 degrees; the slip angle fold's pi / 2 and pi and the tire force's backwards pi; the tire effects (2450, 5 km/h, 0x3E1374BD one unit higher, 0.001, the dirt cap 0.8), the pad rumble, the steering force, the steer indicators (0.2, 0.018 one unit higher, pi / 180, 0.1, pi / 2 two lower, 25 km/h), the dirt brake cut's pi / 180 and the pad car's handbrake 0.1, the spawn hold's 30 deg/s, the torque vectoring (10 km/h, 50 m/s, 1 / 15 deg three units higher), the automatic box and the gear for a speed, the clutch's and the engine speed's 2 pi / 60 and 10 km/h, the rpm's 2 pi, the low-speed hold, the yaw rate's 2 pi bound, the stop hold's 1 km/h, the wear level's 63.9, the speedometer's 400 km/h a second, the effect request's 0.05, the pit lane's 5 and 100 km/h, the driver parameters (1 / 3.6, pi / 180, 0.1), the moving start's 2 pi / 60 and 2 pi, the collision step's yaw cap 20 pi, its stuck nudge's 0.1, its sound decay 0.98 and its heading wrap's pi and 2 pi. Every atan2f of the ports is the build's own (`atan2(behaviour, ...)`: the AI's heading error and pursuit, the line arcs, the start's line heading and pitch, the attitude, the hub half-track, the Ackermann ratios and the toe) | `libgcc_double_exact` (Tokyo-Geneva: 21,260 before, 0); `prep_leaves_exact` (448 before, 0); `model_ground_course` (3,959 before, 0); `race_tick_exact` on Tokyo-Geneva Europe and Asia: passes, every port and whole-step check (Europe 466,323 of 10,514,566 calls differed at 4f6bd631, Asia 730,163); `race_start_exact` (every record differed) and `model_car_params_exact` (P of every run) pass on both |
| `AngleWrap` (GT3's rule, Tokyo-Geneva's own row) | `0x1D8C00` / `0x1EA908` / `0x1ED3F0` / `0x1EAE20` / `0x1EADC0` | Tokyo-Geneva has the routine after all (another code; placed by `program_map.pins`): pi, 2 pi and 3 pi a unit lower | `race_tick_exact`'s view lag on Tokyo-Geneva Europe |
| (with `SoftDouble`) the class grip | phase 7, Europe `0x1DFB40` / Asia `0x1DFAD8` | after the tire curves, a car the AI drives (control source 1 or 2, or any driver kind) gets the race rule block's percentage of its driveline class (+0x18 + P +0x31, signed) on each wheel's slip fade and friction circle, x (1 + percent x 0.01) (`class_grip_scale`, `RaceInputs::class_grip`) | `race_tick_exact`'s phase 7 on Tokyo-Geneva Europe |
| (the table `Kind7Ride`) special kind 7's ride | Europe `0x1DC6E0` in the wheel's suspension step `0x1E02E0` (GT3 `0x1E8E58`) | a car of special kind 7 with the table's switch on adds to its damper force: from its coordinate less the static one, lowered by the drop (in full from the higher speed, from none at the lower one), a lift below 0 (lift / the axle's share x the axle's sprung weight / 2, in full past -travel, in proportion above), less the spring rate x the drop (`kind7_ride_force`, `kind7_ride`) | `race_tick_exact`'s wheel suspension on Tokyo-Geneva Europe (16,050 of 378,720 calls differed before, 0) |
| `Aids` | `0x1E3B18` / `0x1EF5A0` / `0x1F1CA8` / `0x1EF3C8` / `0x1EF360` | from Tokyo-Seoul v2.00 on, a computer's car (control source 2) shows its own aid block's switches in its TCS and ASCC status bytes after the scripted aids, as a pad car the AI drives does (2001 Tokyo and Tokyo-Seoul v1.02: both 0) | `race_tick_exact`'s phase 6 (Tokyo-Seoul v2.00: 124,115 calls differed before) |
| `ParamsBuild` | `0x1DBFD8` / `0x1D6818` / `0x1D8F48` / `0x1D7260` / `0x1D7200` | a layout 7 car's mass adds the static block's byte +3 alone; a stepless gearbox of kind 1 gets its hold speed, the peak power's speed or 500 rpm below the up-shift speed, in the down-shift word D +0x30 (`0x1D5C68`) and its top speed walk holds the engine there without shifting; with `Driveline`: layout 7's front differential is none, as layout 0's, and the other axle's shaft inertia of unequal axles x the driven axle's radius over its own. From Tokyo-Seoul v1.02 on the rear ride height reads its own spec byte +0x5D (the load transfer gains P +0x34 / +0x38 follow); Tokyo-Seoul v2.00: no pitch-tire terms for special kind 7 | `model_car_params_exact`: passes on 2001 Tokyo (P of 2,044 runs); Tokyo-Seoul v2.00 P of 1,993 runs the same (its other failures are the harness's) |
| `PadClutch` | `0x1E4180` / `0x1EFC78` / `0x1F2368` / `0x1EFA30` / `0x1EF9C8` | a stepless gearbox opens the clutch with the third axis above 0.8 below 1 km/h | `race_tick_exact`'s pad clutch (no stepless car on the pad in its races: the new branch is unit-tested only) |
| `PadControls` | `0x1E4478` / `0x1F0080` / `0x1F2770` / `0x1EFDE0` / `0x1EFD78` | the HUD's steering percentage is written from the command (percent of 15 degrees, within +-100, `0x1F0020`) for the followed steering classes; a stepless gearbox creeps: the pedal is at least ((idle + 500) - rpm) x 0.0005 + friction / (full + friction torque), and a released pedal in reverse keeps the reverse request; from Tokyo-Seoul on not while standing on the third axis | `race_tick_exact`'s pad controls and pad input (2001 Tokyo: 105,585 calls, 0 differing; before 48,257) |
| `DriverInput` | `0x1E4D38` / `0x1F0AE8` / `0x1F3220` / `0x1F07E0` / `0x1F0778` | where the AI drives, the phase ends with the steering percentage two thirds of the way to the command's | `race_tick_exact`'s phase 3 as a whole |
| `AiOutputs` | `0x1F79F0` / `0x1F7840` / `0x1FA040` / `0x1F71D8` / `0x1F7170` | no steering percentage; the handbrake at 1 while the driver's stop state (+0xAE) is 2 | `race_tick_exact`'s AI outputs and controls (2001 Tokyo: 367,104 calls, 0 differing; before 234,855) |
| `AiTraffic` | `0x1F6D70` / `0x1F67E8` / `0x1F8FE8` / `0x1F60E8` / `0x1F6080` | the cooldown only takes the contact away (GT3 returns); no lane pick while a line change blends; a lane picked sets the cooldown to 180 steps (Tokyo-Geneva Europe 150; GT3 30) and goes on to the rest of the routine | `race_tick_exact`'s traffic step |
| `AiBraking` | `0x1F7088` / `0x1F6BB0` / `0x1F93B0` / `0x1F64A0` / `0x1F6438` | a throttle scale below 1 scales only the full-throttle torque's share: x (scale x full + friction) / (full + friction); a car that owes a stop brakes to its point in the stop zone (§14.5) | `race_tick_exact`'s braking (2001 Tokyo: 322,192 calls, 0 differing; before 201,349); the stop zone's part in the pod races (§14.4: every later build, 54 or 55 stop points, 0 differing) |
| `AiDecide` | `0x1F7AE0` / `0x1F7918` / `0x1FA118` / `0x1F72A0` / `0x1F7238` | the stop point's speeds and the stop state's steps (§14.5). Tokyo-Geneva's own additions: a computer's car (control source 2) not on dirt, without a stop zone and at a start speed (the rule block's byte 0) below 60 looks 200 m further at the start, falling to none over 5 s (GT3 40 m over 2 s; Europe `0x1F7458`); in recovery state 2 a hit at the front also turns to state 1 when the line's own heading there (`ai_line_heading`, Europe `0x1F6E38`: from its point at the car's race distance to the first at least 0.2 m on in 0.5 m steps, at most 10, atan2(-dx, dy)) is more than 60 degrees off the car's heading, unwrapped | `race_tick_exact`'s decision (Tokyo-Geneva Europe: 65,871 calls, 0 differing; before every one) |
| `Ascc` | `0x1E2D08` / `0x1EEAF0` / `0x1F11F8` / `0x1EE9D0` / `0x1EE968` | three switches of the program's data (`RaceSwitches` +3 .. +5): channel C also acts with the throttle applied, its acting and its being on count for the status byte, channel E's would too with the third switch 0; an ASM switched off keeps channel E's wheel flags (the car's set-up) | `race_tick_exact`'s ASCC and aids |
| `TyreEffects` | `0x1DEBB0` / `0x1ECE68` / `0x1EF570` / `0x1ECE80` / `0x1ECE20` | channel 2 is silent on surface 8 | `race_tick_exact`'s tire effects (2001 Tokyo: 472,689 calls, 0 differing) |
| `GapClosing`, `ContactIntensity` | `0x1EBDA0`, `0x1F6CC0` / `0x1F2FE8`, `0x1F6720` | a pair's gap is given for cars drawing apart too; the intensity's speeds are 5 and 0.1 km/h (GT3 10 m/s and 1 km/h), its time 5 s and its gap 4 m | `race_tick_exact`'s proximity (2001 Tokyo: 203,441 calls, 0 differing; before 18,489) |
| `SlowDown` | `0x1F6518` / `0x1F5EE0` / `0x1F86E0` / `0x1F58A8` / `0x1F5840` | the run-on after a finish scales the throttle multiplier by 0.8, a half at least (GT3 sets a half) | `race_tick_exact`'s end of a run |
| `Leaves` (the leaves, `sim/exact/leaves.h`) | none / `0x19C4E8` / `0x19D448` / `0x19C578` / `0x19C538` (Tokyo-Seoul v1.02 `0x19CA20`) | the tire effect emitter (`0x1DF150`'s counterpart: 2001 Tokyo `0x1ED4F0`, Europe `0x1ED4B8`) hands each wheel's request, after its smoke, to the particle manager's leaf maker (course object + 0xC, `0x18E5C0`) when the wheel's ground record's flag byte (+0x33) has 0x20 (`TyreEffect::ground_flags`): one draw of the VU0 random register (the rim's slip speed less the hub's x (0.5 + 0.45 r)); at 2 m/s or more and with a slot in the pool, one leaf with twelve draws more (heading +-15 degrees, the throw 75 .. 85 degrees off the ground's slope, 0.85 of the car's velocity, the position +-0.1 m, the spin, the shape, its three angles: the init, Europe `0x19D230`). The leaves share the sparks' pool of 128 and lists (`Sparks`); their update (LeafParticle vf1, Europe `0x19D308`) draws nothing: Euler's equations with the air's torque, the turn's angles from the spin (none while cos a is 0) each wrapped once into 0 .. 2 pi, the air's force on the plate (0x19D070's matrix: -2 w h along the normal, -0.01 (w + h) in its plane) and gravity, the spin x 0.999, gone below the ground's plane less the shape's diagonal. Eight shapes (Europe `0x19CF10` from the manager's constructor): 1 g squares of 0.12, 0.13, 0.14 m and five of 0.1 m. The update's time is RaceBase::vf2's (`particle_dt`: 1/60; 1/50 on Europe's disc at its own rate; Asia's 0x3C888888). The code is the same on every later build; 2002 Tokyo-Geneva's constants folded toward zero | `leaves_exact`, `leaves_emit_exact`, `race_tick_exact`'s leaf checks (§14.4) on all five later builds |
| `SparkEmit` (the sparks, `sim/exact/sparks.h`) | `0x1AADD0` / `0x1A8CE8` / `0x1AA988` / `0x1A9788` / `0x1A9740` (Tokyo-Seoul v1.02 `0x1A91F0`) | the sparks' code is GT3's on every later build: the burst (`0x19F450`; Tokyo `0x19C810`, Europe `0x19C888`), the update and each spark's step (SparkParticle::vf1, GT3's `0x19E7C8`; Tokyo `0x19BB00`, Europe `0x19BBC0`: the manager's update calls each particle's own, the leaves' too), the tuning block (`0x2C03B0` by `0x19F898`; Tokyo `0x2BA378` by `0x19CCD0`, Europe `0x2BAAF8` by `0x19CD48`). The particle record is GT3's 0x54 bytes with the particle kind's table at +0x04 (GT3's flags word +0x04 at +0x08, its first speed at +0x0A, the rest 4 bytes on). 2002 Tokyo-Geneva's constants are folded toward zero (`SparkRules`): the tuning block's 0.35, 9.81, 0.5, 0.7, 0.2, 0.01, 0.03, 0.6, 2, 0.11, 0.9 and 0.025 one unit lower (0.3, 0.09 and the exact ones as GT3's), the burst's pi / 180 (0x3C8EFA34), the emission's 180 / pi (0x42652EE0) and lift 0.05 (0x3D4CCCCC); 1 / 3.6^2 the same. The emission of 2001 Tokyo and Tokyo-Seoul v1.02 is GT3's (NTSC-U's rule; one subtraction merged). From Tokyo-Seoul v2.00 on (`0x1A9D50`, Europe `0x1A8B50`, Asia `0x1A8B08`) it first makes the car body's plane: the drawn heading, ground pitch and roll (car +0x13F4 / +0x1418 / +0x141C against their copies +0x15C8 .. +0x15D0) turned as the body's matrix, its up column the normal, through the drawn ground point (+0x1598 against +0x15B4); the road query still decides which points burst and how many, but a burst starts 0.05 above that plane under the point (-(nx x + nz z + d) / ny) and its particles bounce on it | `sparks_burst_update_exact`, `sparks_emit_exact` on all five later builds (§14.4); with GT3's emission rule on v2.00 or Tokyo-Geneva, or v2.00's on 2001 Tokyo, `sparks_emit_exact` fails at the first burst; with GT3's constants on Tokyo-Geneva both fail |
| `RubberBand`, `BattleCap`, `LapRecord`, `SpeedEvent`, `RecordsLocked`, `LapTime`, `SectorTime` (GT3's rules) | `0x1ECEB8`, `0x1F0478`, `0x1EFDE8`, `0x1EC8C0`, `0x1EC4E8`, `0x1EC620`, `0x1EC520` | every later build has PAL's variant: the time scale's cap at 1.3 (the same constant in each), no cap of the i.LINK battle's own (its virtual 4 only calls the base one: recognised by the routine's lack, as PAL's), a new record only for a lower time, nothing for a ghost. Read beside PAL's on 2001 Tokyo; one code hash on all five builds but the rubber band's (Tokyo-Geneva's own, the same rule) | `race_tick_exact`'s race flow ports (2001 Tokyo: 0 differing) |
| `LicenceStop` (GT3's rule) | `0x1EF488` / `0x1F5168` / `0x1F7968` / `0x1F4BD8` / `0x1F4B70` | 2001 Tokyo and Tokyo-Seoul: NTSC-U's 1 km/h. Tokyo-Geneva: its own constants, Europe's 1.5 km/h two units lower (0x3ED55554), Asia's 1 km/h one lower (0x3E8E38E3) | the race flow's stop (Tokyo); Tokyo-Geneva's constants read, the harness's input check on Europe's |
| the rest of GT3's rules (`SectorDiff`, `LapDiff`, `AngleWrap`, `RunUp`, `BattleStart`, `CatchupAnswer`, `WearAnswer`, `DrawMode`, `LapStartRestore`, `CollisionCommit`, `WallFriction`, `PushSegment`, `LapUsable`) | | NTSC-U's variant (NTSC-J's own changes are not in the later builds); Tokyo-Geneva has no angle wrap routine (inlined: no row) | `race_tick_exact` and the collision tests with NTSC-U's rules (2001 Tokyo: 0 differing) |

The stepless gearbox outside the axle step (the kind is `Driveline`'s D +0x12, 0 on GT3's discs, so these follow that rule;
2001 Tokyo's addresses; proven in `race_tick_exact` with a kind 1 car: phase 7 as a whole and its clutch engagement,
7,950 calls each, 4,980 of them a stepless car's, 0 differing):
the wheel-side speed (`0x1DD798`) takes kind 2's second ratio from the second gear on; a locked clutch with the request
still on writes the wheel-side speed (`0x1DD860`); the engine speed step (`0x1DE0B8`) sets a locked stepless clutch's
engine speed to the ratio word x the wheel-side speed, and its pedal-clutch branch uses the same gear; the gear step
(`0x1DE430`) leaves kind 2's clutch as it is on a change between forward gears; kind 1 has no gear step at all
(`0x1DE4D8` in its place: reverse on the request, first above 1 km/h or under throttle, reverse again below -1 km/h
off the throttle, the clutch to 2 after either; the shift lock-out is not counted down).

The special cars' start (the car's race start `0x1DDAD0`, Tokyo `0x1EC160`): special car 1's stop count 0 and its mood
bytes cleared but for the time, 60 steps (`0x1DB0F8`; Tokyo-Geneva Europe 50); a car with a wing starts with it in.
`race_tick_exact` compares the later words at the start too (2001 Tokyo: 31 starts, 0 differing).

`Layout` now also gives the driven axle speed its cases (`0x1E6C28`, 2001 Tokyo `0x1DD718`: layout 7 the rear axle's, layout
8 GT3's blend).

Tokyo-Geneva Asia's four routines are recognised (its mood routine has its own row, with 2001 Tokyo's step counts; its
wing routine is Europe's, its stop count 2001 Tokyo's); its default races run them (94,680 calls each, 0 differing; no
special car in them: the special-car run above is Europe's).

Also proven: the angle wrap `0x1D8C00` on Tokyo and Tokyo-Seoul v2.00 is NTSC-U's variant (6,000 direct calls in
`race_tick_exact`); Tokyo-Geneva's is its own (`AngleWrap` row above).

### 14.4 What still differs (the diff harness on each disc)

Mismatching samples of every all-program test. The seventh pass (the pod race, the pass-by states, the licence judge as a
rule, the solo recorders' layout, expf) leaves every count of the tests below as it was: `race_tick_exact` 51,906,285
(Tokyo), 51,905,595 / 52,889,535 (Tokyo-Seoul), 13,027,018 / 13,041,934 (Tokyo-Geneva), and GT3's 66,339,021 (NTSC-U),
66,443,667 (PAL), 64,470,121 (NTSC-J); the new checks (the stop zone, the spring rate, the hit report's moods, the
pass-by states) add none. In brackets: the numbers before the fifth pass (commit 4f6bd631) where
they changed then, and before the sixth (commit a898c2c7) for what it changed (the flying start, the stuck nudge, the
showroom, `model_ai_course`, `control_record_exact`). `race_tick_exact`'s counts are unchanged by the sixth pass (its races have no layout 7 car and no flying start;
the layout 7 runs are separate, §14.3). The leaves (§14.3) leave every count as it was too: `race_tick_exact` gives each race's course object the
original's particle manager (its own constructor), runs our leaf calls on our requests inside the step check and compares
the register after each car's emitter and the manager at the end of phase 13 (and after both updates between the CPU
races' steps), reported in one note and counted in no sample; the default races make a few leaves on 2002 Tokyo-Geneva
(its `LCeu0004` is on Autumn Ring: Europe 24 maker calls, Asia 28, 6 leaves each, 0 differing), none on 2001 Tokyo and
Tokyo-Seoul (no default race there). The sparks (§14.3 `SparkEmit`) change no count either: the default races of
`race_tick_exact` have no spark points. Its `#sparks` / `#scrape` races (DIFF_HARNESS.md) give every car its model's
points as the app does and run the sparks' frame between the steps (RacePS2Base::vf30's emission of each car, then
RacePS2Base::vf29's update) in the original and ours, the register compared after each car's emission and the whole pool
after the update; the frame's register is the next step's in both runs, so the sparks' draws run through the race step
(its tire effect emitter, flaps and, on a later build, the leaves in the same pool). Run after the default races
(`GT3R_TICK_EVENTS="+..."`), so a run's count is the default count plus these races': every disc passes with 0 differing.
GT3's discs and the earlier Concept builds race Formula GT at Grand Valley, a pad car scraping at Rome, a GT World race at
Grand Valley and two touring car races (one with a pad car scraping); NTSC-U 21 of 24 cars with spark points, 36,450 frames,
528 making sparks (539 bursts, 3,383 sparks), 87,728,058 samples; NTSC-J the same, 85,859,183; PAL 17 of 20 (its Formula
GT race has two cars), 140 frames making sparks (907 sparks), 83,481,482. 2002 Tokyo-Geneva races its DTM event at Laguna
Seca, on Trial Mountain with a pad car scraping and on Grand Valley (no Formula car models on Europe's disc).

| Test | Tokyo 2001 | Tokyo-Seoul v1.02 | Tokyo-Seoul v2.00 | Tokyo-Geneva Europe | Tokyo-Geneva Asia |
|---|---|---|---|---|---|
| `libm_trig_exact`, `libm_atan_exact`, `libm_tan_exact` | pass | pass | pass | pass | pass |
| `libm_exp_exact` (seventh pass) | pass: 21,318 samples (the later builds' expf is another routine, `ee::exp_later`; GT3's `ee::exp` passes on GT3's three discs) | pass | pass | pass (its constants folded toward zero) | pass |
| the pod races (`race_tick_exact` with `GT3R_TICK_EVENTS` the four pod races, DIFF_HARNESS.md; seventh pass) | **pass**: 151 ports called, 0 differing of 29,347,271 calls; 28,559,231 samples | **pass**: 151, 0 of 29,346,362; 28,561,367 | **pass**: 151, 0 of 30,031,622; 29,246,627 | **pass**: 143, 0 of 28,397,772; 27,613,221 | **pass**: 143, 0 of 28,385,894; 27,601,536 |
| `libgcc_double_exact` | pass (2,180), 837 narrowings not compared (§14.2) | the same | the same | pass (21,260) | pass (21,260) |
| `prep_leaves_exact` | pass | pass | pass | pass (448) | pass (448) |
| `leaves_exact` (the leaves, §14.3) | **pass**: 47,565 samples, 40,363 maker calls (14,943 leaves made, 19,277 into a full pool), 7,200 updates; the shapes the same | **pass** (the same) | **pass** (the same) | **pass**: 16,623 made, 16,877 into a full pool (dt 1/50) | **pass**: 14,815 made, 19,479 into a full pool |
| `leaves_emit_exact` | **pass**: Autumn Ring (`demo_gtc_a` on `r_autumn`), 6,000 frames, 18,576 maker calls, 3,849 leaves made | **pass**: `r_autumn`, 13,362 calls, 2,864 made | **pass**: the same | **pass**: `LCeu0004` (`autumn`), 17,088 calls, 2,117 made | **pass**: `LCeu0004`, 13,441 calls, 1,812 made |
| `race_tick_exact`'s leaf checks with a `#leaves` pad race (DIFF_HARNESS.md: the event's race, car 0 over the verges) | **pass**: `demo_gtc_a#leaves+pad`, 15,623 maker calls in the step, 2,014 leaves made, 3,355 updates; 9,861,626 samples | **pass**: `demo_nissan_primera#leaves+pad`, 2,723 calls, 87 made; 6,529,413 | **pass**: 2,723 calls, 87 made; 6,659,826 | **pass**: `LCeu0004#leaves+pad`, 7,890 calls, 621 made; 4,707,014 | **pass**: 9,509 calls, 732 made; 4,705,691 |
| `sparks_burst_update_exact` (the sparks, §14.3) | **pass**: 7,091 samples, 2,290 bursts (11,558 sparks made, 827 into a full pool), 4,800 updates (751 with kick draws) | **pass** (the same) | **pass** (the same) | **pass**: 13,124 made, 736 into a full pool, 783 with kick draws (dt 1/50) | **pass** (as Tokyo) |
| `sparks_emit_exact` (21,600 frames: Trial Mountain, Grand Valley, Rome) | **pass**: F090/S, 110 frames with bursts (38 / 41 / 31) | **pass**: the same | **pass**: 115 (38 / 44 / 33), on the body's plane | **pass**: Calibra Touring Car (no F090/S model on the disc), 19 (17 / 0 / 2) | **pass**: F090/S, 87 (35 / 18 / 34) |
| `race_tick_exact`'s spark checks with the `#sparks` / `#scrape` races after the default ones (DIFF_HARNESS.md; the frames between the steps) | **pass**: 21 of 24 cars with spark points, 36,450 frames, 461 making sparks (470 bursts, 3,181 sparks made); 70,639,518 samples | **pass**: the same; 70,638,828 | **pass**: 468 making sparks (477 bursts, 3,184 made), on the body's plane; 72,039,108 | **pass**: 18 of 18 cars, 19,470 frames, 337 making sparks (341 bursts, 3,258 made); 25,647,668 | **pass**: 407 making sparks (412 bursts, 3,939 made); 25,639,268 |
| `model_collide_exact_build`, `model_collide_course` | pass | pass | pass | pass | pass (no course read) |
| `model_ground_course` | pass | pass | pass | pass (3,959: the bump tables' amplitude constant) | pass (no course read) |
| `model_car_params_exact` | pass | pass | pass (81 checks: the harness's database index) | **pass**: P of 1,604 runs, 600 perturbed specs and 16 race calls (its own events) the same (8: P of every run) | **pass**: 1,598 runs (8) |
| `model_collide_exact_step` | **pass** (5 of 680 steps: the stuck nudge) | **pass** (5) | **pass** (5) | **pass** (5) | **pass** (5) |
| `model_ai_course` | **pass**: 1,568,269 samples (stopped: the harness's, below) | **pass** (1,568,326) | **pass** (1,568,326) | **pass** (1,568,227) | **pass** (1,568,227) |
| `control_record_exact` | **pass**: 165,789 samples (the store's stream 4 bytes later: `RecordStore`; the classes' table pinned) | **pass** | **pass** | **pass** | **pass** |
| `race_start_exact` | **passes**: 18,750 records (422: the flying start); moving starts all 1,156 | **passes**: 20,051 records (392); moving starts all 1,176 | **passes**: 20,051 records (392); moving starts all 1,176 | **passes** on its own events: 13,195 records the same, moving starts all 1,492 (every record; 729 of 1,492) | **passes**: 12,265 records, moving starts all 1,492 |
| `race_tick_exact` | **passes**: 170 ports and whole-step checks called, 0 differing of 54,445,241 calls | **passes**: 170 called, 0 of 54,444,688 | **passes**: 170 called, 0 of 55,428,628 (164 called, 147 with none; 2,865,445 of 55,462,487) | **passes** on its own events: 145 ports and whole-step checks called, 0 differing of 13,735,348 calls, with the pad drives and the pit-lane run (114 with none; 466,323 of 10,514,566) | **passes**: 145 called, 0 of 13,751,486 (113 with none; 730,163 of 10,505,098) |
| `race_course_tires_exact`, `gt_field_exact` | pass | pass | pass (20 / pass) | pass / no samples (20) | pass / no samples (20) |
| `gt_grid_exact` | **pass**: 804 showroom builds, 1,492 grids (1: a layout 7 car's showroom power) | **pass**: 809, 1,492 grids | **pass**: 809, the grids (1) | **pass**: the showroom alone, 373 cars (no GT Mode races on the disc) | **pass**: 373 |

**2001 Tokyo's race step is proven as a whole**: on every step of `race_tick_exact`'s races all cars are taken at phase
1, our phases 1-9 must match every car at the collision step and our phases 11-13 at the end of phase 13 (203,589
steps each, 0 differing), the race flow after phase 13 (with the pit lane's machine) and the rubber band likewise,
13,950 steps carried on our own results from our own start records (the later words compared at the start: 31
starts). The run with a stepless car and the special car (`demo_tmv_g%fit_w_ff_01%tmvr_fit_w_ff_01`,
DIFF_HARNESS.md) passes too: the stepless ratio moved on 4,467 of 4,980 axle steps. About 35 ports have no call site
of their own in a later build's program (routines its compiler inlined or enters by a jump: the engine speed step, the
gear step and its shift routines, the pit lane's machine, the brakes' two callers); what they do is checked by the
ports around them (phase 6 and phase 7 as a whole) and by the whole step. What the test does not run there: the
variants and direct calls every 30 ticks (they poke GT3's layout; 14 checks only they or a jumped call reach are
listed as never called: the slip angles' leaf, the AI's gear hint in the update, pit entry / exit / leave cases, the
end of a run at the pit exit, the emitter's requests as a port), the i.LINK conductor, a stepless gearbox of kind 2, and the stop zone (no race of test kind 5: the pod races run on their own, below). A layout 7 car runs in its own race (`demo_tmv_g%super_sports_tmv_01`, DIFF_HARNESS.md):
640,084 calls, 0 differing, the whole-step checks and the start records included.

What made the earlier differences on 2001 Tokyo, all the harness's: a negative record offset read as a large one (the
course category of the dirt events); the event queue's count and the session's times 4 higher in a later build's race
information, its ids from 11 on one higher, a data logger entry 4 bytes longer and without feedback words; the pit
exit's second line request and the end of a run's last call (made by a jump) not on the plan tape; the licence
conductor's answers asked for GT3's program.

`race_start_exact` passes on 2001 Tokyo since the sixth pass: what it left was the placement of a flying-start event
raced under the single race conductor (`FlyingGrid`, below; no menu of the game makes such a race, the license
conductor's own starts are the same).

**Tokyo-Seoul's race step is proven as a whole on both discs** (v1.02 and v2.00, `race_tick_exact` with every whole-step
check, 0 differing): what made the earlier differences on v2.00 were the harness's but one. Its own: the computer car's
aid status (`Aids`, §14.3). The harness's: v2.00's database holder keeps an index of each table at +4 (256 chain heads
by the sum of a record's id bytes and its byte +0x10, then a next word per record; the loader builds it, the part
lookups walk it: `race_setup::set_database` builds it as the loader leaves it), so the holder's event record and course
id are 8 higher (`holder_off`: the licence's judging times, the championship's places); the per-wheel suspension
routine takes the car itself where GT3 takes F; v1.02's conductors keep GT3's 43 virtual slots (v2.00 has the two more).
The start's driver words were the same database index. One start rule of the later builds: a layout 7 car's record byte
+0x16D0 (GT3: set unless the gearbox part's kind is 0x13) is set too (Tokyo `0x1E8460`, through `0x1D9FF0`; `Layout`).
`race_start_exact` passes on both Tokyo-Seoul discs too (it left 2001 Tokyo's flying start).

The flying-start placement under the single race conductor (2001 Tokyo and Tokyo-Seoul; ported, `FlyingGrid` in §14.3,
`start.cpp`'s `flying_grid`): that conductor's virtual 25 (Tokyo `0x1E73E0`) gives an event with a start distance (not 0)
and a start speed mode 1, car i at the distance plus the first i of the `FlyingStart` table's steps (+8: 20, 8, 20, 15,
8 m) and the speed plus its speed steps (+0x10: 5, 2, 5, 1, 3 km/h); its virtual 22 (`0x1E7530`) then poses each car again
at that distance on the line the table's +0 names (6, 2, 1, 1, 2, 6), asks for that line at once, and runs GT3's virtual
22 (the ranking). Tokyo-Geneva's conductor is GT3's again. The harness's races of the license events with the event's
cars reach it (the default set does).

**Tokyo-Geneva's race step is proven as a whole on both discs** (Europe at its release rate of 50 steps a second, Asia at
60; `race_tick_exact` with every whole-step check, the pad drives and the pit-lane run, 0 differing; the run with special
car 1 and a winged car, `A_cien_dmv_02_0003%gtcrr_pod_tmv_01%gtcrr_crossfire_dmv_02`, 134 ports called, 0 differing on
each). What made the differences, all its own (§14.3): the constants its compiler folded toward zero in the converter, the
AI, the tire and effect routines, the gearbox and engine, the collision step and the race flow (found by a survey of every
routine of the race step against 2001 Tokyo's, gp-relative loads included); its own atan2f wherever a port calls one (the
AI's heading error made every decision differ); the AI decision's longer start look-ahead and the line's heading in the
recovery state; phase 7's class grip; special kind 7's ride (`Kind7Ride`); the driver spread without the event's speed
offset; Asia's 1 / 60. The later builds' layouts 7 and 8 in the automatic box's traction veto and the TCS's wheels (every
later build: layout 7 reads the rear wheels alone, layout 8 both) showed only in a pad drive that sets layout 8. The
harness's: the AI decision's call of the aim point placed one instruction late by the matcher (a pin), the pad drives, the
pit lane and the parameter test's races on the disc's own events, an event whose course file the disc lacks left out of
`race_start_exact`.

What remains, by impact:

Done in the sixth pass: the flying-start placement (`race_start_exact` passes on 2001 Tokyo and Tokyo-Seoul), the layout
7 system (§14.5; the showroom and the race), the collision step's stuck nudge (`model_collide_exact_step` passes
everywhere).

Done in the seventh pass: the pod race's rule (the stop zone's set-up, the per-step rule with the HUD messages 0x10 / 0x11
and the stop counted, the line's disqualification, the spring rates, the collision report's moods; `StopZone`, `PodRule`,
`PodLine`, `PodSpring`, `HitReport`, the table `PodMessages`), proven by four pod races on every later build (the table
above: the computer's pods stopping, a pad pod stopping on the odd laps, one never stopping and disqualified, the
split-screen race) and wired into the game's race (`ExactRace`: the zone, the AI's stop points, the stop driver; the
race screen's prompts and notes; checked in the game on Tokyo-Geneva Europe and 2001 Tokyo, with the pod display's
disqualification as the original presents it and each half's prompts with two players: concept §9.2); the pass-by
routine (`PassBy`, `pass_by_step`: checked on all eight programs, the game's race now gives special car 1 its rival);
the licence conductor's answers and judge as a located rule (`LicenceJudge`); the races' solo recorders on the later
builds' store layout (`SoloRecorders::later`; fingerprints mix it in); expf (`libm_exp_exact`).

1. **The variants and direct calls** on the later builds' layout (`GT3R_TICK_VARY=2`): the 14 checks only they or a
   jumped call reach are still listed as never called there.
2. **The solo recorders' layout in the harness**: `race_ghost_exact` (the original's own recorders) runs on GT3's discs
   only; the later layout is unit-tested, and `control_record_exact` proves the manager's store on every later build.

What made `model_ai_course` stop and differ on the later builds, both the harness's: the passing-line lookups `0x1F9C80` /
`0x1F9CB0` have no routine of their own there (their compiler put them inside the lane lookups, which the test checks),
and the target point's race progress is at F +0x3E8 in their record (GT3's +0x3E4). `control_record_exact`'s other
difference was the harness's too: the playback's table of the difference classes' starts (NTSC-U `0x2D8EE0`) placed on
a zero block by the matcher (now a pin: Tokyo `0x2BB070`).

### 14.5 Record layouts, the race step and the new parts of the later builds [X: the harness runs on them; the rest read from the programs, marked]

Found while making the race tests run (`tools/diffharness/program_map.h` holds the layouts; every later build has the same):

- **The car record** is 0x16E0 bytes: one word more at +0x1328 (F +0x3A4), everything after it 4 higher. The word is a
  float the moving start sets to 1 and `0x1D9948` (Tokyo) changes together with the engine speed when the driveline
  block's +0x12 is 1: a stepless gearbox's ratio (`Car::later.ratio`; the gear record's +0x30 -> spec +0xE1 -> D +0x12
  is the gearbox kind: 1 and 2 without steps, `0x1D9930`; 3 one gear). The builders and the moving start are ported
  (§14.3), and the tick's part (`0x1D9A08`, called by the axle step in place of the engine torque; `stepless_torque`):
  with the clutch locked in a forward gear the ratio is the engine's speed over the wheel side's, between the last gear's
  relative ratio and 1; its target is, for kind 1, the hold speed (D +0x30 as rpm) x (0.4 x pedal + 0.6) over the wheel
  side's speed, within the same bounds, at a rate between the settings' two by the pedal; for kind 2 the selected gear's
  relative ratio (1 below second) at the rate down or up, with the gearbox lock-out set while it moves for a car the AI
  drives or pedal style 0. The ratio moves by rate x dt x the stored ratio a step and is stored; the torque is the
  engine's times it. Otherwise the ratio is 1. Proven by execution with a kind 1 car in a race (§14.4), with the
  gearbox's other spots (§14.3: the wheel-side speed, the clutch engagement, the engine speed step, the gear step).
  The routine computes its four rates once, at its first call (a flag beside the settings); until then the words hold
  the program's own initial values, which nothing reads.
- **The feedback words** left the car record: the bytes GT3 keeps at +0x16D4 .. +0x16D6 (the pad's vibration, the
  steering wheel's force) and the force's filter F +0x514 are in the conductor, 8 bytes a car at +0xB7C8 (`0x1EDA60`
  returns a car's block: three bytes, the steering force's step count as a byte (GT3's word F +0x518), the float). The record's words at the old places of F +0x514
  and +0x518 are used by the new phases below. The harness reads and writes records in GT3's layout with the block's
  words at their old places.
- **The conductor**: eight car slots of 0x16E0 from +0xC8, the feedback blocks at +0xB7C8, GT3's +0xB7A8 .. +0xE3A7 at
  +0x60 more (the configuration +0xB808, the AI course object +0xBC08, the collision manager +0xBC14, the rule block
  +0xE3A0, the car count +0xE3E4, the order +0xE3E8), the vtable pointer at +0xE47C. 2001 Tokyo's and Tokyo-Seoul v1.02's
  conductors keep GT3's 43 virtual slots; from Tokyo-Seoul v2.00 on two more at 15 and 16. Its constructor does not clear the unused car slots'
  collision bodies (GT3's does), and the collision step reads their in-use words.
- **A race entry**: GT3's +0x20DC and after (the input object, the grid slot, the settings, the condition words) are
  0x44 higher. The database's event table is the archive's table 31 (GT3's 32); the harness asks `CarDatabase` for each
  table's place.
- **The race step** `0x1F39D8` (Tokyo `0x1E8840`) calls four per-car routines GT3 does not have, each in a loop of its
  own over the cars: `0x1DA0A8` after phase 1, `0x1DB2F8` before phase 2, `0x1DBAB0` after phase 3 and `0x1DBC48`
  between the proximity phase and phase 12 (Tokyo's addresses). Ported in `sim/exact/special.h` (`hybrid_step`,
  `mood_step`, `stop_count_step`, `wing_step`; `tick_until_collision` / `tick_after_collision` call them under a later
  build's rules). They act on the **special cars**: the spec builder gives a car a kind (spec +0x11 -> P +0x32) when its
  label is one the program names, and they keep their state in the eight bytes at the later builds' record +0x149C
  (`Car::later.special`; the places GT3 keeps the steering force's filter and count at). For every other car they do
  nothing.
  - Kind 1 (one car): a mood 0..15 (low nibble of byte 1, the one before in the high nibble) with a time (byte 2; 0xFF:
    for good), the lamps' fade (byte 3: 24 steps, 36 from a lamp class that is not 0), a nod (byte 4, a phase of 48
    steps that runs while the mood's lean entry is 2, else until it is level), a lean (byte 5, up to +-30, moved while
    the nod is level), the mood asked for this step (byte 6) and the stop count (byte 0). Each step asks for a mood:
    finished (2 in first place, else 7), the HUD message 1 (6), on the pit lane (3), a rival alongside (1, for 120
    steps), the slip filter at 1 (2, for 120 steps), all four wheels on a soft material (10), else the rest mood when
    the time has run out. The rival is `0x1F5560` (Tokyo): over the pass-by states the race's sounds keep per pair
    (`0x1F59B0`, states 2 and 3) and the gap matrices, level 5 when a car on the same lap passes or is passed. The
    collision reports (`0x1DFEB8`) and the race flow ask for moods 4, 5, 8 and 9 through the same setter
    (`set_mood`, Tokyo `0x1DB160`); both are ported (`hit_mood`, the line's mood 7). The pass-by states are ported too
    (`pass_by_step`, `ExactRace::pass_by`): the game's race gives the mood its rival. The stop count goes up to 34 while the car stands
    (under 1 km/h) with the handbrake control at 1 and down otherwise; the suspension (`0x1E9348`, through Tokyo
    `0x1DBB28`) and the single races' virtual 14 (Tokyo `0x1EE8C0`: the messages 0x10 / 0x11) read it: ported below.
    Tokyo-Geneva Europe's counts are five sixths of these (100, 20 / 30, 40, 25, 29), as its rate.
  - Kind 2: a wing, a float in bytes 0..3 that moves to 1 at half a unit a second once the speed reaches 80 km/h and
    back to 0 once it falls below 60 km/h (byte 4: out). From Tokyo-Seoul v2.00 on the routine also serves kind 8
    (about 113 / 105 km/h), kind 10 (120 / 80 km/h) and kind 9, whose wing follows the speed between about 56 and
    80 km/h at two units a second; Tokyo-Geneva's speeds are its own constants, a few units in the last place lower.
    Only the drawing reads the wing.
  - Kind 7 (two cars from v2.00 on): nothing in these four routines; its readers were not looked for.
  - A layout 7 car (AWD system 5 in the later builds, below): its byte 5 (the flow display) is cleared after phase 1; the
    float in bytes 0..3 is the system's store (the layout 7 system, below; `sim/exact/hybrid.h`), shown by the race
    display.
- **The layouts**: the later builds' AWD system 5 is a layout of its own, 7, and system 7's layout is 8 (GT3: 7); the
  AI's driveline class of system 5 is 0.
- **AWD system 3** (`0x1E2700`, Tokyo `0x1DCE80`; the gain `0x1E5998`, Tokyo `0x1F18B8`): the settings are 0x14 bytes of
  the program's small data (`Awd3`; Tokyo `0x334C78`: +0 the gain 200 %, +1 the capacity 0, +2 a switch 1, +8 / +0x10
  the lateral fade's start 25 % and floor 15 %; the driveline builder writes the two as floats to +4 / +0xC). Builder
  and gain are ported and proven (§14.3). **The axle step** (`0x1E5B98`, Tokyo `0x1F1B10`) is not: read so far, it keeps
  minus the full-transfer value apart (no front torque, and the centre clutch unlocks), holds the centre clutch locked
  while the torque difference is within the requested front torque (GT3: within the capacity) and takes the request's
  magnitude. Ported as `axle_step_later` (§14.3).
- **The stop zone** (the pod race's rule; the conductor's +0xE408 .. +0xE47B in every later build, `StopZone`): a flag,
  a count of stops made per car, the zone's start and length, and per car a stop point (x, y, race distance). The
  set-up (Tokyo `0x1EE280`, from the conductor's initialisation) turns it on when the race configuration's byte +2 is 5
  and the program's two constants (`RaceSwitches` +0 / +2: 608 m, 33 m) are not 0: the start is wrapped into the lap and
  the zone must not cross the line; the counts and points are cleared. A car **owes** its laps counted less its stops
  made (`0x1EE6A0`).
  - **The computer's cars** (ported: `ai_braking`, `ai_decide`, `ai_outputs`): the braking, for a car the AI drives that
    owes a stop and whose stop state (driver +0xAE) is 0, takes the point at start + length x (0.75, 0.5, 0.25, 0 by
    (rank - 1) / 2); when it lies in the segment driven and ahead of the car, the next target speed is 0, the point's
    place on the line (`0x1FA018`) and its race distance (`0x18FA20`) are stored, the braking distance is measured to it
    and the state becomes 1 when the braking starts. The decision, state 1: no target at 10 km/h or more or within
    0.5 m, 10 km/h below that within 20 m, the state back to 0 further away; state 2 once under 1 km/h within 0.5 m. State
    2: target 0 and the handbrake (the outputs) until the car owes nothing.
  - **The rule each step** (Tokyo `0x1EE8C0`, before the single and split-screen conductors' virtual 14; ported,
    `Rule::PodRule`): for every car that owes a stop, with "on" = all four wheels' ground records carrying flag bit 2. A pad car
    (control 0, no driver kind, lap base 0): on and sitting (the sit counter `special[0]` at its cap, 34; Tokyo-Geneva
    Europe 29), the stop is counted; on and not sitting, HUD message 0x10; not on and past start + length, message
    0x11. A car the computer drives: past the start and sitting, counted; else on, message 0x10. Both messages have
    display time 0: they show on the steps the rule sets them. A stop counted (`0x1EE708`) adds one (255 at most) and
    posts the information note 7 for a pad car, or in a race of two cars.
  - **At the line** (Tokyo `0x1EE7F0`, first in the base and split-screen lap handlers; ported, `Rule::PodLine`): a car that
    owes two stops or more is given the stop driver (line 6 blended, driver kind 5: GT3's uncalled `0x1E3DF0`), its lap
    count is put back, its mood set to 7 for good and the later builds' race event 0xB posted (their display event
    types from 0xB on are GT3's plus one); the lap handler returns at once. The lap count runs from the first crossing
    after the start, so the line after a lap without its stop is where a car owes two. The pod display shows the event
    for its own car as the time trial's FAIL and then ends the race (concept §9.2).
  - **The pod**: the sit counter starts at 0 (the car's start also sets the mood bytes: time 0x3C, Europe 0x32); the
    suspension takes each wheel's spring rate through `0x1DBB28`: with n = the counter not 0, r = (wheel + car index)
    mod 4, mirrored for an index of 4 and up, t = 7 r (Europe 6 r): from n = t on the rate is x max(0, 1 - (n - t)): the
    wheels lose their spring one after another. The collision report sets moods 9 (a wall), 5 (the other kind) and by
    the part hit 5, 8 or 4 for a car (time 0xD2 for parts 8 and up; Europe 0xAF). Ported (`pod_spring`, `hit_mood`;
    `Rule::PodSpring`, `Rule::HitReport`), with the set-up (`Rule::StopZone`).
- **The layout 7 system** (AWD system 5; Tokyo `0x1D9CF0` .. `0x1DB098`; ported, `sim/exact/hybrid.{h,cpp}`, proven in
  races with a layout 7 car on every later build, §14.3): an engine driving the rear axle with a motor of its own there, a
  motor on each front wheel, and a store of energy (the float in `special[0..3]`, full from the start, `0x1DA060`) with a
  flow display (`special[5]`, -100 .. 100: each charge or draw adds the amount x the flow scale, 6000; Tokyo-Geneva
  Europe 5000). The set-up (`0x1D9CF0`, from the layout routine and the showroom) makes floats of the `Hybrid` table
  (disc values 20 35 6 15 6 15 50 15 20 35 20 50 75 70 1 50 10 1 7 -1 20 50 50 10 50 50 10): +0 / +1 the front motor's
  (each front wheel) and the rear motor's ratings (20 and 35 kW); +2 .. +5 x 10 the front motors' torque factor and the
  rear motor's torque cap, driving and charging; +6 x 0.1 the front torque scale (the front cap 300 N m); +7 the capacity
  in units of the rating sum (2 x front + rear: 1125 kW s); +8 / +9 x 5 the braking charge caps a second (100 a front
  wheel, 175 the rear pair); +0xA x 100 the speed below which the engine's throttle rises to charge with the clutch open
  (2000 rpm), +0xB / 100000 that throttle per rpm, +0xC / 100 the charging torque's cap as a share of the full-throttle
  torque, +0xD the charging power's cap (70 kW); +0xE / +0xF the low and high marks (% of the capacity: 1 and 50; the
  level runs from 0 to 1 between them, `0x1DA3F8`); +0x10 / +0 the front motors' reserve share at the low mark; +0x11 /
  +0x12 the gears in which the rear demand may be positive (1 .. 7), +0x13 (signed) the gear above which the rear motor
  drives (-1); +0x14 / 100 the front motors' share of the engine's power; +0x15 .. +0x17 and +0x18 .. +0x1A the yaw
  split's gains (/ 980) and dead band (/ 100), driving and charging. In the axle step the engine's throttle is the
  throttle over the engine's share P_e / (P_e + 2 x the rear rating) (`0x1DA380`) and the rear motor's torque is added
  (`0x1DAE50`: the rear wheels' braking charges the store; the demand, `0x1DA460`, from the throttle (the pedal while the
  clutch slips at a launch) by the store's level, draws the motor's power for a positive demand, scaled by what came out,
  and charges for a negative one); the torque vectoring step first adds the front motors (`0x1DA6B0`: the front wheels'
  braking charges, |brake reaction x rim speed| x 0.00100068 within the caps; a wheel under 1 km/h unbraked gets the
  motor's whole cap and draws nothing; the demand and the engine's power ask for a share of the motors' torque, split
  between the wheels by the yaw the steering wants, `0x1DA588` with tanf, against the car's lateral acceleration;
  driving draws from the store, charging adds braking torque only; the clutch torque word F +0x4F4 (later +0x4F8) is set
  to 0 and then to the power drawn over the front rating); the engine speed step with the clutch open (`0x1DA218`) charges
  the store from the engine below a full store, its throttle raised for it and that torque taken off. The showroom's
  peak power adds (2 x +0 + +1) / 0.7355 PS. The converter's mass rule and the driven axle speed are the later builds'
  (§14.3). Not ported (display only): `0x1DA040` (the flow byte's sign) and `0x1DB020`.
- **The AI's outputs** `0x1F79F0` (Tokyo `0x1F7840`): the steering percentage F +0x523 is not written; the driver
  object's byte +0xAE at 2 sets the handbrake. Ported (§14.3).
