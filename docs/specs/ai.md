# GT3 AI drivers: behavioural specification

Behavioural description of how the opponent ("CPU") drivers of Gran Turismo 3
decide their controls, written from a study of the game program (CORE.GT3,
NTSC-U 1.10, load address 0x100000). It contains facts, formulas and
constants in prose; no code was transcribed. The implementation is the exact core's AI phases
(`src/sim/exact/tick_ai*.cpp`, the race in `race.cpp` / `race_flow.cpp`), described in
`docs/dev/AI.md`.

Tags: **[C]** confirmed by reading the code, **[I]** inferred (meaning or
naming deduced from use), **[?]** open.

Addresses are given so the next reader can check a statement; they are
function entry points in the program image.

Changelog:
- 2026-10-26: the engine's own AI driver (`src/ai/`: `AiDriver`, `AiRace`, the safety respawn) is removed;
  every CPU car and the AI on the player's car run on the exact core's AI phases. Earlier entries name the
  removed code where it implemented a rule.
- 2026-10-25 (the exact core on the PAL and NTSC-J programs, physics §12): PAL counts 50 steps a second, so the AI's
  start hold blips from n < 11 (NTSC 13), the lane-change cooldown is 25 steps (30), the arc ticks stop at 5000 (6000),
  the speed controller's shift lock is 10 (12), the pit stop holds 250 steps (300); its catch-up scale is capped at 1.3 in
  every mode. NTSC-J's rubber band calls its scale directly (no virtual 4), its battle conductor's catch-up answer gives
  setting 2 for any set word and its wear answer passes the word on as the setting. The AI's own decisions are the same
  code in all three programs.
- 2026-10-25 (the pit lane's state machine `0x1EDB40`, its switches and the end of a run `0x1EACB8`
  ported bit for bit, `sim::exact` tick_pit.cpp): §13 the exit (`0x1E3BF0`) sets a CPU car in a non-pit
  kind to kind 0 without a line request, and a failed line request leaves the kind; `0x1E3C78` sets
  kind 1 only when its line request succeeds; the entry of a CPU car hit on the pit surface (recovery
  state 2); the pit details in physics §8.6.
- 2026-10-25 (the AI's step, decisions and traffic ported bit for bit, `sim::exact` tick_ai.cpp, checked at every call
  site in `race_tick_exact`): a pad car with a driver kind and no hold is driven by the AI, outputs applied (§1, it was
  "display only"); the traffic step runs before the steering saturation's cut, which applies with any drive decision (§5);
  the arc modes 1 / 2 count the arc ticks too (capped at 6000), the corner bonus is skipped on the pit surface, not for
  ghosts (§5); the wanted slip angle is not clamped to the peak on dirt courses (category 2, §6, was "[?] one race mode");
  the pure pursuit's speed is the planar world speed (§6); the third collision flag (`0x1FB870`) is constant 0, so its
  state 0 / 2 → 1 rules never fire (§7); r in the start hold and the traffic delay's F+0x4E1 is the race position, 1 =
  first (car init: index + 1), and R is the up-shift rpm (§10, §14.1).
- 2026-10-25 (lines and speed plan ported to the program's own arithmetic, `src/sim/exact`, checked bit for bit against the
  routines on the 80 US course files and in whole races): the AI course object lives in the conductor at +0xBBA8 (§2.1); a
  request outside 0..7 takes line 6's fallback row (§2.1); the setup's line builder writes kind 2 into right-hand arcs on the
  files without road segments (§2.2); the span search's exact rules, the evaluation across the start line and its arc
  geometry, the stop line's own evaluation `0x1FA368` (§2.3, was open in §11); the target loop's 10 evaluations and the old
  line's search hint (§3); the braking distance's (v + v_next)(v − v_next), its software-double factor, the mode the zone
  sets, status 1's margin, the pit lane's multiplier (§4.3).
- 2026-10-25 (exact race start, `sim::exact` `start.h` / `ai_setup.h`, every byte of the driver
  object and of the AI course object checked against the original's initialisation in 3255 race
  set-ups): §2.4 bytes 25 / 26 of the driver set are read: the time trial and free run start
  from class 0's set (`0x1F3420`: a flying start −10 × b25 m back at 5 × b26 km/h when both are
  set); §2.1 the loader `0x1F92F8` handles the lines in the order 6, 0, 1, 2, 5, 3, 4, 7 (the
  program's table 0x3529C0); without segment words in the file it traces each point's road
  segment along the line from the previous point (straights every 10 m, arcs every 10°; the
  stop line's first point probed with the main line's segments from its end) and marks every arc
  kind 1 (left, radius > 0) or 2 (right; the side map 1); each point's race progress, the road's
  height, slope and bank (four probes half a metre away) and surface come from the course (line 5
  only the progress); §2.1 a candidate initial line whose evaluation fails is compared with the
  position the previous candidate left (the program's outputs are not written).
- 2026-10-25 (rubber band exact, `sim::exact` `tick_race.cpp`, checked against the race step in
  races): §9.3 the battles run it too (their virtual 32 is the configuration's +0x22, which their
  virtual 11 sets to 0 with the catch-up setting on, 0xFF off); the i.LINK battle's virtual 4
  caps a scale at 1 + A_f; a finished car keeps the scale the pass hands it (the chain's in mode
  0), not 1; modes 1 and 2 exist in the code but no event uses them. §10 the rank byte
  car+0x1465 is 1-based: the ranking (virtual 3) writes position + 1.
- 2026-10-25 (traffic, read from the program and verified in lockstep with whole races of the
  original, new §15; implemented in `src/ai/`): the next-point index steps one point a tick and
  holds "count" past the last point until the start line is crossed (§4.1); every line request
  re-plans the segments, including the return to line 6 re-requested every free tick (§14.1);
  status 2 holds through the braking modes 3 / 4, the stop line keeps no bookkeeping and gets a
  throttle multiplier of 1 (§4.3); the top-speed parameter and C_car come from the spec converter
  (§4.2, §4.3); the pairing broad phase (active flag, pit-surface bit, 100 m, height window) and
  u / v / closing along the car's 3-D axes (§14); the side map lookup and its search order
  (§14.1); the cooldown counts only when the traffic step runs, the pit-lane halving comes before
  the cooldown test (§14.1). The contact counts of `race_contact_count` (Sunday Cup 1: original 36
  vs ours 130) were one start of a chaotic race and a harness that gave the two sides different
  driver sets; over 20 start holds the two are alike (§15).
- 2026-10-21: §1 kind 8 [C]: the clutch request 0 replaces the AI request rule (the rule
  runs only after the kind-8 test in `0x1E35D8`), for the player's car and CPU cars alike
  (implemented in `sim::Vehicle`; it used the AI rule before).
- 2026-10-20 (rules pass, implemented in `src/ai/`): the course file's own
  driver parameter sets (new §2.4; §9.1 set selection corrected: "race-supplied
  sets" are the course file's slots 37–40); the corner-speed grip exactly
  (§4.2; the bank divides only when it leans into the arc); the braking grip's
  extra factors (§4.3); the pit-lane speed sections are delimited by the STOP
  line's points (§5, §13; §2.1 corrected) and the AI drives the pit lane
  itself (§13); the recovery flags' exact meaning, the missing 1→2 rule, the
  state-2 target and the standstill steering (§7); driver kinds 5–8 and the
  end-of-run handler (§1); the arc status is set only when it is 0 and mode 4
  keeps counting the arc ticks (§5).

## 1. Where the AI runs

- Every car record has an embedded **driver object** at car + 0xEC8 (about
  0xC0 bytes). It holds the driver parameters, the current AI line, the
  state machines and the three outputs (steer angle, throttle, brake). [C]
- The AI is evaluated in step phase 3, the driver-input phase
  (`0x1E4D38`, physics spec §1.2), once per 1/60 s tick, before any physics
  of that tick. [C]
- The car's control source byte (car state + 0x3E9) selects: 0 = player pad,
  1 or 2 = CPU car. For CPU cars the phase runs the AI decision
  (`0x1E35D8` → `0x1F7AE0`) and then **applies** its outputs (`0x1F79F0`).
  A pad car (control source 0) with driver kind 0, or with the hold counter
  running, drives from its pad and only keeps the driver object current
  (`0x1F6648`: the point bookkeeping `0x1F7088` and the gear hint `0x1F6588`,
  the HUD's recommended gear); with a non-zero "driver kind" (state + 0x534)
  and no hold the AI drives it: `0x1E35D8`, `0x1F6588`, then the outputs
  applied (`0x1E4CF0`) [C, 2026-10-25; corrects "display only"].
- **Driver kinds** (F+0x534) [C, 2026-10-20]: 0 normal; 1 the AI drives
  (run-up, after the finish); 2 / 3 / 4 the pit lane, the stop line, leaving
  (§13); 5 / 6 / 7 the stop driver; 8 parked.
  - Kind 7 is tested first in `0x1F7AE0`: the stop driver `0x1F7810` runs with
    a steer target of 0 and nothing else of the decision runs.
  - Kinds 5 and 6 run the whole decision (lines, braking point, recovery,
    steering target) and then hand the steering target to the stop driver
    instead of the throttle logic (`0x1F8210`).
  - The stop driver: steering slewed to the target (`0x1F77B8`), throttle 0,
    brake 1 (kind 5: the brake word is left as it was); once the 3-D speed
    |F+0x44C..454| < 0.0694 m/s (0.25 km/h) and the angular speed
    |F+0x47C..484| < 0.0524 rad/s (3°/s) it sets kind 8 (`0x1E3E90`) and
    releases the brake.
  - Kind 8 (`0x1E35D8` → `0x1F7900`): clutch request 0, steering slewed to 0,
    throttle 0, brake 0. The decision code does not run, and neither does the AI clutch
    request rule (it follows the kind-8 test; kinds 1-7 and CPU cars get it) [C].
  - Kind 3 sets the look-ahead to 5 m; kind 4 to 20 m while the pit-limit flag
    (driver +0x85, §5) is set.
- **End of a run** (`0x1EACB8`, car, mode) [C]. Mode 4 becomes 2 on the pit
  surface (F+0x535 bit 4), else 0. If the car was not yet finished it is marked
  finished (F+0x4E0); a car in a pit kind (2–4) is left alone. A human car
  (control source 0) gets `0x1F6548` (empty) and the event overrides again
  (`0x1F6380`). Then by mode:
  - 1 → `0x1E3E38`: switch to line 6 (blended), kind 6;
  - 2 → `0x1E3E80`: kind 7;
  - 3 → `0x1F6518` (speed scale × 0.8, throttle multiplier := 0.5), then as 0;
  - 0 and > 3 → `0x1E3C78`: a car in a pit kind, or a human in kind 0,
    switches to line 6 (blended) first; then kind 1.
  Callers: race finish (DynamicsConductor vf18), time trial (vf18), battle 2P
  and machine test (vf19), the end of a run at a pit stop (`0x1EDB40`) and the
  licence tests with rule byte (event +0x19A) 0 or 3–4 pass 3; licence tests
  with rule byte 1 or 2 (stop / section) pass 1; a failed licence test
  (`0x1EF520`) passes 2; free run (RaceFreeRun vf31) and the machine test's
  RaceMachineTest vf31 pass 5, time trial vf13 passes 10. The researched
  summary also names a failed time trial and the end of a free run on the pit
  surface for kind 7 [I: the time-trial fail path was not traced here]. The
  kind 5 setter `0x1E3DF0` (line 6, kind 5) has no caller [C].
- Outputs written to the car each tick [C, `0x1F79F0`]:
  - **steering**: the AI writes the road-wheel steer angle δ (radians)
    directly. It bypasses the pad steering follower (§4.4 of the physics
    spec); its own slew limit is in §6.
  - **throttle**: written directly, except in corner mode (§5.3), where it
    may move by at most 1.0 per second (dt per tick) towards the requested
    value.
  - **brake**: the brake pedal value goes to all four wheels as for a human
    (ABS and the AI's own aid parameters then act on it).
  - a reverse-gear request flag and a shift-lock timer of 12 ticks whenever
    the throttle request is neither 0 nor 1 (keeps the automatic gearbox
    from hunting) [C, I purpose].
- Gears: CPU cars use the automatic shift mode (shift-mode byte forced to
  0 when the control source is not the pad). The driver object also
  computes a **recommended gear**: the gear for the current target speed
  while braking (`0x1F6588` → `0x1E4E80`); this is what the HUD shift
  indicator shows for the player [C structure, I use].
- Everything is deterministic: no random numbers are used anywhere in the
  driver code. The only variation between cars comes from parameters, grid
  slot, rank and the per-car start offset (§9). [C: no RNG calls in
  `0x1F5800`–`0x1F8B9C`]

## 2. AI lines

### 2.1 Numbering and roles

The course file holds 7 lines (`docs/formats/course.md`, slot 31). At run
time the game keeps a table of 8 line pointers whose entry 0 is always
null, so the **game's line k is file line k − 1**. [C: `0x1F8848`/`0x1F8868`
index the table at +8 + 4k; `0x1F9BF8` tests the same slots]

| game line | file line | role | evidence |
|---|---|---|---|
| 1 | 0 | alternative (passing) line A | lane-change target, `0x1F9CB0` |
| 2 | 1 | alternative (passing) line B | lane-change target, `0x1F9C80` |
| 3 | 2 | pit lane line: full lap through the pit lane | speed held at the pit limit while the car is between the STOP line's (game line 4) points 2 and n − 3, `0x1F7AE0` [C, corrected 2026-10-20: the code reads line 4's points]; data: survey below |
| 4 | 3 | pit stop line (partial): driven at 20 km/h with special target evaluation | `0x1F7AE0`, `0x1FA368` [C structure, I role] |
| 5 | 4 | **side map** (partial): its points' kind byte tells whether "left" means line A or B over that stretch | `0x1FA530` [C] |
| 6 | 5 | **main racing line** | initial preference and the line the AI returns to when no car is near, `0x1F6D70`, `0x1F8670` [C] |
| 7 | 6 | optional variant; falls back to 6 | fallback table [C] |

Fallback when a requested line is missing (`0x1F9BF8`, table at 0x2DA508)
[C]: 1 → 1, 6, 2; 2 → 2, 6, 1; 3 → 3; 4 → 4; 6 → 6, 1, 2; 7 → 7, 6.
(0 and 5 are never requested; their rows are empty. A request outside 0..7 takes row 6 [C, 2026-10-25].)

The **AI course object** K is embedded in the conductor at +0xBBA8 (the driver object's +0x08 points at it) [C,
2026-10-25]: K+0x00 the course (its road block's +0x18 is the lap length, `0x18FA10`), K+0x04 the AI line section (slot
31; game line k at +0x08 + 4k, null when the file line is absent or has no points), K+0x08 1 when lines 3 and 4 both exist.
The course setup `0x1F9B98` / `0x1F92F8` converts the section in place and prepares the lines in the order of the table
$gp 0x3529C0: 6, 0, 1, 2, 5, 3, 4, 7.

Initial line (`0x1F8780` → `0x1F8670`) [C]: unless the race forces one,
each candidate of {6, 2, 1} (table 0x2DA030) that exists is evaluated at the
car's current race progress, and the one whose point there is closest to the
car (within 100 m) is chosen; default 6.

### 2.2 Control point fields (40-byte records)

The game uses the file records in place; load-time code fills the words
that are zero on disc and converts the coordinates to floats. As used by
the driver code [C for the use, I for how the loader derives them]:

| offset | type at run time | meaning |
|---|---|---|
| +0x00 | u8 | **segment kind** of the segment starting here: 0 straight, 1 arc; 2 never occurs in the files, but on the files whose lines carry no road segments (some licence files, §11) the setup's line builder `0x1F8BF8` rewrites every arc's kind to 1 (radius > 0) or 2 (radius ≤ 0), and 1 on the side map: 342 points of the 80 US files have kind 2 at run time [C, 2026-10-25] |
| +0x01 | s8 | **braking-distance adjustment** in percent: braking distance × (1 + b/100), minimum factor 0.1 |
| +0x02 | s8 | **corner speed adjustment** in km/h, added to this arc's target speed |
| +0x03 | u8 | [?] (not read by the driver code) |
| +0x04, +0x08 | f32 | x, z (sim frame) |
| +0x0C | f32 | height (filled at load; interpolated for the target point) |
| +0x10 | u16 | road segment |
| +0x12 | s8 | surface index for tyre grip lookups (filled at load) [I] |
| +0x14 | f32 | **race progress** of the point (same unit as the car's progress, metres along the lap) (filled at load) |
| +0x18 | f32 | signed arc radius in metres (0 on straights) |
| +0x1C | f32 | **slope** angle (rad, filled at load): braking distance × (1 − sin slope) [I sign: uphill shortens] |
| +0x20 | f32 | **bank** angle (rad, filled at load): corner speed term divided by (1 ± sin bank) when the bank leans into the turn [I] |
| +0x24 | f32 | heading (rad) at the point (from the packed word) |

### 2.3 Evaluating a line at a progress value (`0x1FA018`) [C]

Given a progress value s (wrapped into [0, lap length)):
1. find the two consecutive control points i, j whose progress brackets s
   (searching outwards from a hint index, with wrap-around);
2. t = (s − s_i) / (s_j − s_i) (with the lap length added across the start
   line);
3. if point i is a straight: position = linear interpolation of P_i, P_j by
   t; if an arc: centre = P_i + radius × (unit normal of the heading at
   i), angles from the centre to P_i and P_j, and the position is at the
   angle interpolated by t (going round the side given by the radius sign,
   adding 2π where needed);
4. height = linear interpolation of the two points' heights.

Exactly [C, 2026-10-25; bit for bit against the routine on the 80 US files]:
- s is wrapped once (s < 0: + L; s ≥ L: − L). The span search `0x1F9F08` (line, hint h, s): before the first point →
  from the last point to point 0 (t = (s + L − d_last) / (d_0 + L − d_last)); at or past the last point → the same span
  (t = (s − d_last) / (d_0 + L − d_last)); otherwise k is tried in the order h, h + 1, h − 1, h + 2, h − 2, … (n + 1 tries;
  k wrapped once into the line, k = 0 skipped) and the first k with d_{k−1} ≤ s < d_k is the span. None found (possible
  only on unordered lines): nothing is written, and the caller (`0x1F7658`) goes on with what its stack held. A hint far
  outside the line (≥ 2n or below −n, e.g. the current line's index on a much shorter old line) reads points beyond the
  line's end.
- On an arc from point i (heading h, radius r): centre c = P_i − r (cos h, sin h); a_0 = atan2(c_x − x_i, y_i − c_y), a_1 the
  same for point j; r > 0: a_1 + 2π when a_1 < a_0; r ≤ 0: |r|, and a_0 + 2π when a_0 < a_1; a = a_0 + t (a_1 − a_0),
  position c + |r| (−sin a, cos a) (sinf and cosf, not sincosf).
- The stop line (game line 4) has its own evaluation `0x1FA368` (the target point uses it on line 4): s wrapped, then
  + L in the first half of the lap (as the loader added a lap to the line's distances); outside [d_0, d_last) nothing is
  written; otherwise the first span k with d_k ≤ s < d_{k+1} from point 0 on, **always straight** (no arcs); none found:
  point n − 2 (t = 0).

So positions along the line are parameterised by **race progress**, not by
arc length.

### 2.4 Course AI parameter sets (course file slots 37–40) [C, 2026-10-20]

`0x1F5F38` (via `0x1F5FB8` from `0x1F5FD8`): with the class byte c (≥ 4 → 0),
it reads the word at (course file) +0x94 + 4c, the course file's offset-table
entry for slot 37 + c (converted to a pointer at load). Non-zero → that
32-byte block is the car's set; zero → the built-in set of the course
category (§9.1). Bytes 0–24 are read by `0x1F5FD8`; class 0's bytes 25 / 26 by the time trials' and
free runs' start (`0x1F3420`: a flying start −10 × b25 m back at 5 × b26 km/h, physics.md §9.1)
[C, 2026-10-25]; 27–31 by neither.

Survey (scratch script over the 80 US files): 79 files carry all four sets;
L_circle60_wet1 has none. The sets differ per course and often per class:
brake % 100–200 (r_smtnorth / smtsouth 200), corner speed 90–110 %, L₀ 5–30 m,
L₁ 30–80 m, v_L byte 90–150, yaw byte 24 (44 / 30 on r_tahitimaze), steering
rate byte 6–12, bonus delay 0–5, corner bonus 20 (0 on slalom80r class 0), pit
speed 78 (25 race courses) or 80 (r_apricot, r_route5, r_route5_dry and the
licence files); TCS gain 0 on most dirt-course classes, 5.0 on many tarmac
ones; ASCC channel flags non-zero in 75 of the 316 sets. Many licence
files carry the built-in set's values.

## 3. Target point and lookahead

Each tick (`0x1F7AE0` start, `0x1F7948`, `0x1F7658`) [C]:
- Lookahead distance L (metres of progress):
  - normal driving: L = L₀ + (L₁ − L₀) × min(1, v / v_L), v = forward speed
    (L₀ if v ≤ 0). Default parameters: **L₀ = 15 m, L₁ = 40 m,
    v_L = 50 m/s** (180 km/h);
  - for the first 2 s of the race, L += 40 × (2000 − t_ms) / 2000 (a long
    look ahead off the grid);
  - recovery states (§7): 10 m while reversing (state 1), 40 m in state 2;
  - driver kinds: 5 m (kind 3, the stop line), 20 m (kind 4, leaving, while
    the pit-limit flag driver +0x85 is set, §5) [C; these come before the
    recovery-state values].
- Target point = line position at (car progress + L). If it is closer than
  5 m to the car, L grows by 1 m and the target is re-evaluated (at most 10
  times). [C, 2026-10-25: at most 10 evaluations, L grows at most 9 times; the search hint is the driver's next point
  index on its current line, also for the blend's old line, whose evaluation starts from the L the first one ended
  with.]
- **Line blending**: after a line change the previous line is remembered
  and a blend weight w is set to 1; w decreases by dt every tick (to 0 in
  one second). The target is old + (new − old) × … precisely:
  target = T_new + (T_old − T_new) × w. [C, `0x1F79A0`, `0x1F8980`]
- Heading error ε = wrapped difference between the direction to the target
  and the car heading.

## 4. Speed planning: target speeds and braking points

### 4.1 Segment bookkeeping (`0x1F7088`, `0x1F6958`, `0x1F6A20`) [C]

The driver keeps the index n (D+0x82, a byte, 0 .. count) of the **next** control point
of its line; n = count means "past the last point". Point distances (+0x14) are compared
as stored, and the lap length L comes from the course (`0x18FA10`).

Each tick (`0x1F7088`, first thing after the status is cleared; skipped entirely on the
stop line, game line 4), at most one step [C, verified in lockstep, §15]:
- n = 0 and s > L/2 → n = count (the car is still before the start line);
- else n < count and point n's distance ≤ s → n + 1;
- else n = count and s < L/2 → 0 (the start line crossed);
- else n ≠ 0 and s < point n − 1's distance → n − 1;
- otherwise unchanged.
So past the last point of a lap line the next segment is the last point's (kind and
target), and the braking distance is measured to point 0 (§4.3); point 0's arc becomes the
next segment only once the car has crossed the start line.

When n changes (`0x1F6958`):
- current segment kind (mode, D+0x83) ← the previous next kind (D+0x84);
- current target v_cur (D+0x8C) ← previous next-target (D+0x90);
- next kind ← kind byte of point n, of point count − 1 when n = count;
- next target v_next ← the car's top-speed parameter P+0xDF8 (§4.2) if that kind is 0,
  else the corner speed of that point (4.2); kinds ≥ 3 leave it unchanged.
The tick count in the arc (D+0x88) is not touched here (only the modes, §5).

**Re-plan** (`0x1F6A20`, run by every line request `0x1F8980`, §14.1, and at the start):
n ← the first point whose distance is beyond s (0 when s is before point 0, count when past
the last); the current point c = (n − 1) mod count; next kind ← kind of c, v_next ← its
target; then the shift above.

### 4.2 Corner speed of an arc (`0x1F6678`) [C]

For the arc starting at point i, radius r, surface s (point +0x12):
1. Grip [C, exact 2026-10-20]: the front record is `0x1DB9B8`(P, 0, s), the
   rear `0x1DB9B8`(P, 2, s): the runtime tyre record of the compound slot the
   surface s uses (physics §6.5). μ_f = w · rec_f+0x00 · rec_f.mu[s] (record
   +0x08 + 4s), μ_r = (1 − w) · rec_r+0x00 · rec_r.mu[s]; rec+0x00 is 9.8 for
   every compound but medium_evo (9.8 × 1.02, physics §6.5), so each is 9.8 × the compound's
   base mu × the surface's grip % (× 1.02 on medium_evo).
   When *(driver+0)+0x9C ≠ 0 and the car is not on the pit surface
   (F+0x535 bit 4): μ = ½ Σ_i grip_i · μ_axle(i) over the four wheels' wear
   grip factors (car+0x103C + 0xA4·i); otherwise μ = μ_f + μ_r. With fresh
   factors of 1 both are equal. No load sensitivity, slip curve or camber
   enters: the per-car inputs are the compound, the live wear, k and the
   set/event corner-speed scale and offset.
2. Bank: a = μ·|r|; if the bank leans into the turn (r ≥ 0 with bank < 0, or
   r < 0 with bank > 0) it is divided by (1 − sin|bank|) (more speed);
   otherwise a is unchanged [C, corrected 2026-10-20: there is no
   (1 + sin|bank|) case].
3. Downforce: **v = √(a · (1 + k·v²))** is evaluated exactly three times
   starting from v = 0, so the first evaluation gives √a, and the third
   value is used (three square roots in total, every call; execution-verified
   2026-10-11, e.g. a = 2351.8, k = 3.97e-5 gives 48.495, 50.710, 50.911).
   Here k = (front lift
   coefficient + rear lift coefficient) / mass (§5.2 of the physics spec;
   `0x1F6550`).
4. v_corner = v × speed_scale + speed_offset + 2.78 × trim (trim in steps
   of 10 km/h, ±2, a debug/tuning value) + point byte 2 (km/h) / 3.6.
5. Clamp to [20 km/h, the car's top-speed parameter].

The **top-speed parameter** P+0xDF8 is written by the spec converter (`0x1DB498` →
`0x1DB2C8`, from `0x1DBFD8`) [C, 2026-10-25]: start from the rev limit in top gear, then
walk the speed up in whole km/h steps (kmh_max / 500 + 1) through the gears (shifting up
past the upshift rpm) and keep the last speed at which the engine's force in that gear
beats rolling resistance (with downforce on the load) and drag. It is the showroom's
top-speed figure: `sim::showroom_top_speed` gives the same value to 1 mm/s on all six
Sunday Cup cars (Demio 199 km/h, Civic 247 km/h).

The tyre record value includes g (it is an acceleration scale), so step 3
is the familiar v² = μ g r with a downforce correction.

### 4.3 Braking point (`0x1F7088`) [C]

Each tick the status byte is cleared first. On the stop line (game line 4) nothing else
runs and the throttle multiplier is 1.0. Otherwise, after the point bookkeeping (§4.1):
if the mode (D+0x83) is 3 or 4 (already braking) the status is **2** and nothing is
recomputed [C, 2026-10-25]; so lane changes stay off for the whole braking zone (§14.1),
until the next point index change ends the braking mode. Else, when the next segment is
an arc and the car is faster than its target (v > v_next, v = the forward speed F+0x488):
- braking distance
  d = (v² − v_next²) × max(C_car, 1 / (2 μ_f g)) × (1 − sin slope_n)
      × max(0.1, 1 + b_n / 100) × min(1, k_brake),
  where μ_f is the front grip at point n's surface [C, 2026-10-20: rec_f.mu[s]
  × L(P+0xE50 · ½ / P+0x284) (the record's load curve +0x188 at that per-width
  load) × rec_f+0x148 (the braking-side maximum of curve C) × P+0xE0C (the Cam
  value at the camber offset), × ½(grip_0 + grip_1) unless on the pit surface;
  the coefficient is 1 / (2 · μ_f · 9.8)], C_car is a car parameter (a floor on the
  braking coefficient, i.e. a limit on the deceleration the car can
  produce [I]) and k_brake = 100 / brake-percent parameter. C_car = P+0xE34 =
  P+0x48 / (4 · P+0xE28 · P+0x27C) = mass × front tyre radius / (4 × front brake torque
  per wheel) [C, `0x1DB498`; equal to the last digit on the six Sunday Cup cars];
  the point read here is n, point 0 when n = count (§4.1);
- if the car is touching another car (contact intensity c > 0, §8.1),
  d += 20 × c (brake earlier);
- distance D = straight-line distance from the car to point n;
- if d ≥ D the car is in the **braking zone**: the current mode becomes 3
  (braking from a straight) or 4 (braking while still in an arc), and the
  status byte says "braking". Otherwise, with k_brake > 1, a soft zone of
  length d·(k_brake − 1) before the braking point scales the throttle by
  (remaining margin / soft length) [C formula].
- The function also returns the throttle multiplier: 1, × the event's
  throttle percentage (§9) on every line except the pit lane (and the stop line,
  which returns 1 at once).
- A status byte is set to "approaching braking" when the remaining margin
  is under 50 m; lane changes are not started then (§8).

Exactly [C, 2026-10-25; bit for bit against the routine in whole races]: the squared speeds are the single product
(v + v_next)(v − v_next); the coefficient is max(C_car, 1 / ((μ + μ) · 9.8)) × (1 − sinf slope_n); the factor
max(0.1, 1 + b_n / 100) is the only double arithmetic of the AI (the program's software doubles, ee::D: (double) b × 0.01
+ 1.0, rounded to a single); then × k_brake only when k_brake < 1, and + 20 c only while the lane-change cooldown is 0. The
zone: d ≥ D sets the mode to 4 when the current mode is not 0 (an arc mode), else 3, and the status 2. Otherwise the soft
length is d (k_brake − 1) when k_brake > 1 (else 0), the margin D − d; the status becomes 1 when the margin is under the
soft length + 50 m, and the throttle multiplier is margin / soft length while the margin is under a non-zero soft length.
The multiplier × the event's throttle scale (driver +0x78), except on the pit lane line 3. The other exits (a braking
mode, a straight next segment, not faster than its target) return 1 × the same scale; the stop line returns 1 at once.

### 4.4 Speed controller (`0x1F7590`, `0x1F7520`) [C]

throttle = clamp(0.75 + G × clamp(v − (v_t − 2 km/h), −10 km/h, +10 km/h),
0, 1) with speeds in m/s and a negative gain G;
brake = 1 (full) if throttle = 0 and v > v_t, else 0.

The brake is therefore **on/off**; the always-on ABS (§4.5 of the physics
spec) modulates it at the wheels.

## 5. Throttle and brake by mode (`0x1F7AE0`) [C]

| mode | situation | throttle request |
|---|---|---|
| 0 | on a straight, not braking | 1.0 |
| 1 (2) | in an arc | speed controller towards v_cur + bonus, G = −0.5; the bonus (default **+20 km/h**) applies once the car has been in the arc for the parameter number of ticks (default 0), not on the pit surface (F+0x535 bit 4) |
| 3 | braking zone entered from a straight | speed controller towards v_next, G = −2.0 |
| 4 | braking zone entered inside an arc | speed controller towards v_next, G = −1.0 |
| pit lane line | car between stop-line points 1 and 2 | the braking modes (3/4) aim at the pit-lane speed (byte 18, 0 → 80 km/h) + 2 km/h; the arc modes keep v_cur |
| pit lane line | car between stop-line points 2 and n − 3 | mode 1 towards the pit-lane speed + 2 km/h, G = −0.5; the corner bonus (after its delay) only off the pit surface; pit-limit flag driver +0x85 = 1 |
| pit stop line | always | mode 3: towards 20 km/h, G = −2; pit-limit flag = 1 |
| any line, recovery state 2 | always | towards 50 km/h (on the pit lines: their target), G = −0.5; the arc tick count is reset; no mode logic |

The stop-line distances [C, `0x1F82B8`; corrected 2026-10-20]: the code reads
game line 4's points (the line table entry +0x18), not line 3's. The loader
(`0x1F92F8`) adds one lap length to line 4's point distances from the first
point in the first half of the lap on; the car's distance F+0x3E4 gets a lap
added when it is in the first half. Then: below point 1 normal; [1, 2) section
1; [2, n − 3) section 2; beyond, normal.

Arc modes [C, `0x1F8440`]: the status byte (§14.1) is set to 4 / 3 / 5 only
when it is still 0 (a braking-point status 1 or 2 from `0x1F7088` stays).
Modes 1, 2 and 4 count the arc ticks (+0x88) on, at most 6000; modes 0 and 3, recovery
state 2 and a drive decision other than forward reset them [C, 2026-10-25].

Then [C, order corrected 2026-10-25]:
- throttle × the multiplier of 4.3 (event throttle percentage);
- traffic (§8, §14.1) scales the throttle again (forward drive only, after the start delay);
- **steering saturation** (§6), with any drive decision: if the wanted slip angle exceeds
  the tyre's peak slip angle by the ratio q > 1 and the gear is 2nd or higher,
  throttle × max(0, 1 − K_t(q − 1)) and brake + K_b(q − 1) (capped at 1).
  Defaults K_t = 1.0, K_b = 0;
- in corner mode the applied throttle is slew-limited (§1).
A drive decision other than forward (§7): throttle 0 and brake 1 (brake), or throttle 1 with
the reverse request (reverse).

## 6. Steering (`0x1F7AE0`, `0x1F77B8`) [C]

1. α_max = mean of the two front tyres' **peak slip angle** (tyre record
   +0x94, ≤ 45°) for their current surfaces.
2. If the car is standing (|v| ≤ 2 km/h): steer target 0.
3. Forward driving: desired yaw rate from a pure-pursuit arc to the target
   point: with the target vector p (from the car) expressed in the
   **velocity** direction (the car heading when slower than 5 km/h),
   lateral offset y and distance² |p|²: **r_d = 2 v y / |p|²**; v is the
   planar world speed √(v_x² + v_y²) (F+0x44C / +0x450), y the offset to the
   left of that direction [C, 2026-10-25].
4. The wanted slip angle α is computed only in the direction in which the
   car's yaw rate r is short of r_d:
   - r_d > 0 and r < r_d: α = K_y (r_d − r);
   - r_d < 0 and r > r_d: α = −K_y (r − r_d);
   - otherwise α = 0.
   Default **K_y = 1.2** (rad per rad/s).
   If |α| > α_max, q = |α| / α_max (used in §5) and α is clamped to ±α_max
   (the clamp is skipped on dirt courses, course category 2, `0x1F2F90`; q
   still applies) [C, 2026-10-25].
5. Steer target δ_t = α + mean of the front wheels' velocity-direction
   angle (the steer angle that would give zero slip, per-wheel state
   +0x78). So the AI steers to put the front tyres at a chosen slip angle
   relative to their actual direction of travel; it never asks for more
   than the peak slip angle.
6. δ_t is clamped to ±45°.
7. In a recovery state (§7) with |ε| > 80° the wanted slip angle is ±α_max
   towards the target (then steps 5–6 apply); while reversing or braking to
   turn round: δ_t = 1.5 ε clamped to ±30° (±20° switch points), mirrored
   when rolling backwards.
8. Slew: the applied steer angle moves towards δ_t by at most the steering
   rate parameter × dt per tick (default **90°/s**), starting from the
   car's current steer angle.

## 7. Recovery: wrong way, spins, stuck (`0x1F7AE0`) [C]

A drive decision and a 3-state machine (driver object +0x80):

Drive decision each tick, with threshold θ = 60°, except in state 1 where
θ = 10° when moving forward, rising linearly to 60° at −30 km/h:
- |ε| < θ (or state 2): if v > −2 km/h → **drive forward** (normal logic);
  otherwise → **brake** (still rolling backwards).
- |ε| ≥ θ: if v > 2 km/h → **brake**; otherwise → **reverse**: request
  reverse gear, full throttle, steering as in §6.7.

State changes (ε heading error, v speed; the decision above uses the state
before these):
- state 0 (normal) → 2 when |ε| > 80°, or when flag A is set and |ε| > 45°;
- state 2 (turning round) → 0 when |ε| < 40° and v > 30 km/h; otherwise → 1
  when flag B is set and |ε| > 60°;
- state 1 (reversing out) → 0 when |ε| < θ and v > 3 km/h; → 2 when |ε| < θ
  and v ≤ 3 km/h [C, 2026-10-20: missing before], or when |ε| ≥ θ and flag A
  is set.
There are no timers or dwell times. The program also tests a third flag
(`0x1FB870`) that would send state 0, and state 2 with flag B, to state 1; its
getter returns 0, so those rules never fire [C, 2026-10-25].

Flags [C, 2026-10-20]: B = collision body +0x434 (`0x1FB878`), A = +0x438
(`0x1FB880`), of the conductor's body record (+0xBBB4 + 0x4F0·i). Both are
cleared at the start of every collision step (`0x1FB1A0`) and set by that
step's responses:
- wall response (`0x1FD8xx`): the hitting octagon point k (physics §5.3
  numbering) 0–3 → B, 4–7 → A. When a wall end point hits one of the car's
  edges (index 8 + edge) the code tests the index, not the edge, so neither
  flag is set;
- car–car response (`0x1FC5xx`): the body whose edge was hit: edge 0–2 → B,
  4–6 → A (side edges 3 and 7: neither); the body whose vertex hit: vertex
  0–3 → B, 4–7 → A.
A third getter `0x1FB870` returns 0 always (its tests never fire).

State 2 drives forward at 50 km/h (§5). Steering below 2 km/h either way is
0 (the moving flag, `0x1F7F34`), so a car standing against a wall has its
wheels straight; the collision step's stuck-steer nudge (physics §5.3) reads
that steering and is then 0.

There is no respawn: CPU cars always drive themselves back.

## 8. Traffic: overtaking, defending, avoiding (`0x1F6D70`) [C]

(Exact, execution-verified facts and corrections: §14.)

### 8.1 Inputs from the car–car pass (`0x1EC098`, `0x1F6B90`, `0x1F6CC0`)

For every pair closer than 20 m (centre distance), eight **zone bytes** describe
where the other car is around this one (u along this car's forward axis, v
along its right axis, Lf/Lr the combined front/rear extents, W the combined
half width): front (u > Lf): front (centre, always), plus front-left or
front-right by the sign of v, or only front-left/front-right (value 2) when
|v| > W; rear (u < −Lr) likewise with rear, rear-left, rear-right;
alongside: left (v < 0) or right, value 2, plus front-left/right or
rear-left/right (value 1) by the sign of u.

The **contact intensity** c ∈ [0, 1] (`0x1F6CC0`) comes from the gap ahead
p and the closing speed q: q > 10 m/s → 1; q > 1 km/h and p/q < 3 s →
0.5; otherwise 0; a non-zero value is at least 0.25 when p < 2 m. The
maximum over all pairs is kept. So c > 0 means "closing on a car ahead".

### 8.2 Decisions

The traffic logic runs only after 5 s + 0.1 s × (per-car start offset) of
race time, never on the pit-stop line, and not while a lane-change cooldown
(30 ticks = 0.5 s) is running. Lane changes are also suppressed while the
status byte says "approaching braking" / "braking" / "partial throttle in a
corner".

- **Closing on a car ahead** (c > 0): look for a free alternative line:
  the "left" candidate only if the front-left zone is free, the "right"
  candidate only if front-right is free. Left and right map to lines A/B
  through the side map (game line 5) at the car's progress; from the main
  line both are possible, from line A only B and vice versa. Preference: in
  front of an arc, the first free candidate in the order (left, right);
  otherwise right first. On a change: blend (§3) and the 0.5 s cooldown.
  If no change is made, **throttle × (1 − c)** (lift behind the car).
- **Car alongside** (no closing): if the left zone is occupied (and not the
  right), move to the right-hand candidate line; if the right is occupied,
  to the left-hand one; both occupied: stay.
- **Nobody around** (front, front-left/right, left, right all free): go
  back to the main line (6).
- On the pit lane line: if a car is alongside and diagonally in front on
  the same side, throttle × 0.5.

There is no explicit "blocking" logic: defending happens only because cars
stay on the main (fastest) line and the car behind must use a slower
alternative line.

## 9. Difficulty and catch-up

### 9.1 Driver parameter sets (`0x1F5FD8`, table 0x2D9A08) [C]

Each CPU car's driver parameters come from a 32-byte set. The built-in set
is chosen by the course category (1 → set 1, 2 → set 2, else set 0); a race
can instead supply its own sets, indexed by the car's driveline class
(P+0x31). See "Set selection, corrected" below. Byte meanings and the three built-in sets:

| byte | meaning | conversion | set 0 / 1 | set 2 |
|---|---|---|---|---|
| 0 | brake percentage (k_brake = 100/x) | | 100 | 100 |
| 1 | corner speed scale | % | 100 | 100 |
| 2 | corner speed offset | (x − 100) km/h | 100 (0) | 100 |
| 3 | steering yaw-rate gain K_y | x × 5 % | 24 (1.2) | 24 |
| 4 | throttle cut on steering saturation K_t | % | 100 | 100 |
| 5 | brake on steering saturation K_b | % | 0 | 0 |
| 6 | lookahead full-speed v_L | x × 2 km/h | 90 (180 km/h) | 90 |
| 7, 8 | lookahead L₀, L₁ | m | 15, 40 | 30, 60 |
| 9, 10 | ASCC A gain, threshold | × 0.01 | 15, 5 | 0, 0 |
| 20 | ASCC D gain | × 0.01 | 0 | 0 |
| 21, 22, 23 | ASCC C gain, C threshold, E gain | × 0.01 | 0 | 0 |
| 13 | **TCS gain** | × 0.1 | 60 (6.0) | 60 |
| 11, 12 | TCS lateral a, b | × 0.1, × 0.01 | 10, 0 (1.0, 0) | 10, 0 |
| 24 | TCS slip scale | × 0.01, 0 → 1.0 | 0 (1.0) | 0 |
| 14 | front/rear grip weight w | % | 100 | 100 |
| 15 | corner bonus speed | km/h | 20 | 20 |
| 16 | steering rate | x × 10 °/s | 9 (90°/s) | 9 |
| 17 | ticks in an arc before the bonus | × 10 | 0 | 0 |
| 18 | pit-lane speed | km/h (0 → 80) | 80 | 80 |
| 19 | ASCC channel flags: bits 0–7 = A-front, A-rear, D-front, D-rear, C-front, C-rear, E-front, E-rear | | 0 | 0 |

The throttle multiplier starts at 1.0 and k (downforce) is taken from the
car (§4.2).

**The AI aid block** [C, verified by execution, physics-spec 2026-10-11].
`0x1F5FD8` writes the aid bytes above into the driver object at +0x30..+0x63,
which is **car+0xEF8**: the aid block that the physics step uses for CPU and
scripted cars (physics.md §4.6). The layout is the same as the human block:
- ASCC A gain/threshold +0x00/+0x04, D gain +0x08, flags +0x0C..+0x0F;
- C gain/threshold +0x10/+0x14, E gain +0x18, flags +0x1C..+0x1F;
- TCS gain +0x20, lateral a +0x24, lateral b +0x28, slip scale +0x2C;
- channel enables +0x30..+0x33, each 1 iff its flags ≠ 0 and its gain ≠ 0.
With every built-in set, **CPU cars run TCS with gain 6.0, a = 1.0, b = 0,
scale 1.0, and no ASCC** (flags are 0). The event overrides (§9.2) never
touch this block. This was previously listed as open.

**Set selection, corrected** (physics-spec, 2026-10-11) [C, `0x1DDAD0` →
`0x1F5FD8` → `0x1F5F38`]:
- The class byte that car init passes is **P+0x31, the driveline class**
  written by the converter (`0x1F5EF0`, physics.md §7 #21): FR/MR/RR → 0;
  FF → 1; 4WD system 0 → 1, system 3 → 3, other systems → 2. It is **not**
  chassis +0x1D. (Spec 0x31 = chassis +0x1D, the CoG-height byte, is a
  different field: values 59, 10, 39… on the disc.)
- **Course sets** [corrected 2026-10-20]: the "race-supplied" table is the
  course file's offset table: entry 37 + class (+0x94 + 4·class; class ≥ 4 →
  0). A non-zero entry is the set (§2.4); 79 of the 80 US course files have
  all four.
- **Otherwise the built-in set is chosen by the COURSE CATEGORY** (course
  table 34 byte +8, as stored in the conductor): 1 → set 1, 2 (dirt) →
  set 2, anything else → set 0. So tarmac courses use set 0 and dirt
  courses use set 2.
- The §9.2 event overrides (`0x1F6380`) take the same driveline-class byte
  as their per-class index.

**Human run-up parameters** (flying/rolling-start run-up, physics.md §9.4)
[C, car init `0x1DDAD0`]. There is no separate "human" parameter set:
- every car, human or CPU, gets the set chosen as above;
- CPU cars (control source ≠ 0) then get the event overrides of §9.2;
- human cars instead call `0x1F6540`, which is an **empty function**, so no
  event overrides apply;
- both then get the downforce term (`0x1F6550`).
So the player's run-up driver = the course's set for the car's class (the
built-in set by course category where the course has none), with a throttle
multiplier of 1.0. (At the end of a run, `0x1EACB8`
re-applies the event overrides before handing the car to the stop driver.)

### 9.2 Event overrides (`0x1DA708`, `0x1F6380`) [C]

At race start the event record (database table 32) is copied into a race
configuration block. Per **driveline class** (P+0x31, 0–3; see §9.1) the event
holds four values that modify every CPU car of that class:

| event offset | per class | effect |
|---|---|---|
| 0x1A4..0x1A7 | throttle percentage (≤ 100) | throttle multiplier (always applied) |
| 0x1A8..0x1AB | brake percentage | k_brake ×= 100 / x |
| 0x1AC..0x1AF | corner speed percentage | speed scale ×= x / 100 |
| 0x1B0..0x1B3 | s8 corner speed offset, km/h | speed offset += x / 3.6 |

(0 or 100 mean "unchanged".) 0x19C..0x1A3 are not read by the driver: the
car's converter multiplies a CPU car's front / rear axle grip factors
(P+0xDF0 / P+0xDF4) by 0x19C + class / 0x1A0 + class percent (0 = 100), and
0x19B × 10 is the event's power cap in PS for every car (physics.md §6.4)
[C, `0x1DBFD8`, 2026-10-25]. Examples: Sunday Cup events: throttle 85 %, brake 100 %,
corner speed 80 %; Clubman Cup races 1–2: 100 % / 100 % / 85 %, race 3:
100 % / 100 % / 100 %.

In one race mode (configuration byte +0x37AC = 2, 2-player style) the
values are additionally varied per grid position [C structure, ? mode].

### 9.3 Rubber band: per-car time scale (`0x1ECEB8`, `0x1ECBA8`) [C]

GT3's catch-up does not touch the driver: it **scales the simulation time
step of each CPU car**. Every car integrates with dt_car = scale / 60
(physics spec §1.1). The scale is recomputed every tick after the race
logic (step phase 17) by conductor virtual 4, in the single-player race
and battle conductors only (the others return −1 from virtual 32 and never
call it; the battles turn it on with their catch-up setting).

Configuration from the event record:

| event offset | meaning | Sunday Cup | Clubman Cup |
|---|---|---|---|
| 0x1B4 | global scale %, 0 → 100 | 100 | 100 |
| 0x1B7 | **slow-down** amount A_s % | 20 | 20 |
| 0x1B8 | slow-down start gap (m) | 20 | 30 |
| 0x1B9 | slow-down full gap (×10 m) | 8 | 8 |
| 0x1BC | **speed-up** amount A_f % | 10 | 10 |
| 0x1BA | speed-up start gap (m) | 50 | 40 |
| 0x1BB | speed-up full gap (×10 m) | 10 | 8 |

(Raw Sunday Cup bytes 0x1B4..0x1BC: 100 6 0 20 20 8 50 10 10; Clubman: 100 6
0 20 30 8 40 8 10.) So Sunday Cup: A_s = 20 %, g₀ = 20 m, g₁ = 80 m;
A_f = 10 %, start 50 m, full 100 m. Clubman: A_s = 20 %, g₀ = 30 m,
g₁ = 80 m; A_f = 10 %, start 40 m, full 80 m.

Rule for a car X against the reference car R (the player in the usual mode;
in another pass mode each car against the car ranked directly ahead) with
gap g = progress(R) − progress(X) in metres and whole laps:
- X behind R (more laps behind, or g > 0): ramp = 0 below the start gap, 1
  beyond the full gap or when a lap or more behind, linear between;
  scale = 1 + A_f × ramp; ghost cars are not sped up;
- X ahead of R: scale = 1 − A_s × ramp, with the slow-down gaps;
- dt scale = scale × global scale.

So an opponent more than 80 m ahead of the player in a Sunday Cup race
runs its whole physics (engine, tyres, everything) 20 % slower; one more
than 100 m behind runs 10 % faster. [C formulas; I that R is the player in
mode 1: the pass skips one car found by a scan of the cars]

## 10. Start of race [C; verified by executing the original, notes ailaunch.md]

**Countdown** (hold flag set while the start-sequence counter n > 0;
`0x1E35D8` → `0x1F8A00`): R = the up-shift rpm (P+0x226; RX-7 7500, R34 8000),
r = the race position (car+0x1465 = F+0x4E1, 1 = first: the conductor's ranking
writes position + 1 every step, car init index + 1) [corrected 2026-10-25].
- The clutch request is 0 (clutch open), the brake pedal 0 and the steer 0.
- n ≥ 13: hysteresis blip. The state goes to "up" when rpm < ¾R and back to
  "down" when rpm ≥ 15⁄16·R. Up: throttle += dt·(5 − 0.2r), capped at 1.
  Down: throttle −= dt·(10 − 0.4r), floored at 0.
- n ≤ 12: proportional hold, throttle = clamp(0.625 − 0.002r −
  2·(rpm − R)/P+0x1C4, 0, 1) (P+0x1C4 = rev limit rpm).
- The same blipper runs during a pit-box spawn hold.

**GO** (the first tick after the counter reaches 0):
- **Clutch request:** the AI rule (§ physics.md 4.4) gives 1.0 at once,
  because rpm is near R > ¾R. The clutch link goes open → slipping the next
  tick, so the clutch is dumped at about R.
- **Throttle:** request 1.0 × the event throttle percentage, written
  directly (no ramp, no launch limiter, no AI-side slip control; the slew of
  1/s applies only in arc modes).
- **Traction control:** the only thing that limits wheelspin is the AI aid
  block's TCS (gain 6.0, §9.1), applied after the AI writes the pedal.
  Measured on the RX-7 RS: rear slip settles at about −0.26 with the pedal
  cut to about 0.72–0.75 (about −0.43 with the block zeroed). On the R34 GT-R
  (4WD) the slip peaks at −0.13 and TCS never acts. Event throttle 85 %
  gives about −0.21.
- **Gear:** the standing-start state gives gear 1 (physics.md §9.3).
- **Lookahead:** during the first 2 s the lookahead is lengthened (§3).

Open: even with TCS the RX-7 still spins at about −0.26 in this harness
(synthetic straight line, flat ground). Whether real courses, event
percentages or something outside the AI code remove the remaining smoke
is not settled.

## 11. What is not covered / open

- The loader that fills the point fields (+0x0C, +0x12, +0x14, +0x1C, +0x20,
  +0x24): the meanings above come from their use; the exact derivation
  (which road normal, which segment) was not read [?]. Partial fact [C,
  execution-verified 2026-10-11]: on some licence courses only (L_seattle1/2/3,
  L_rome1, L_apricot1; no race course among all 75 checked), the AI course
  setup (`0x1F92F8`) runs a line builder (`0x1F8BF8`). It samples each
  straight segment every ~10 m (n = max(1, trunc(length/10)) samples) with
  the ground query `0x1F8BA0` to assign road segments. Arcs are stepped by
  angle instead.
- (Closed 2026-10-25: the pit-stop line's own evaluation `0x1FA368` is in §2.3.) The blend's old line is searched
  from the current line's next point index (§3); where the old line has fewer than about 2/3 of the current line's points
  (the test courses: line 6 with 4 points, line 2 with 9) the span search reads past the old line's last point into what
  follows it in the section (the next line's count and points), which a port can only reproduce from the section's memory
  image [C; not reached in the checked races].
- The free-run end and time-trial fail callers of the end-of-run handler (§1)
  [?: not traced].
- Note for the physics workstream: the AI line evaluator is `0x1FA018`;
  the collision manager of physics spec §5.3 starts after it (the range
  given there begins one function too early).

## 12. Data survey

`gt3tool ai-survey` (tools/gt3tool/cmd_ai.cpp) reports per course and line
the point count, lap coverage, points on the pit lane, the mean lateral
offset from the centre line and the flag byte histograms. Over the 80 US
course files (14,401 control points):

- **Roles confirmed by the data.** File line 2 (game 3) is the only full
  lap line with points on pit-lane triangles (4–13 per course); file line 3
  (game 4) has 12 points covering 13–26 % of the lap, 10 of them on the pit
  lane (pit entry, box, exit); file line 0 runs on average 1–2 m right of
  the centre line and file line 1 1–2 m left (passing lines A and B); file
  line 5 (game 6, main) stays near the centre line; file line 4 (game 5,
  side map) is partial on most courses. Licence-only files (slalom,
  circle, some `L_*`) carry only lines 5 and 6.
- **Byte 0** is 1 exactly when the radius is non-zero (7,519 arcs) and 0
  otherwise (6,882): the segment kind duplicates the radius test.
- **Byte 1** (braking-distance %) is non-zero on 337 points, from −88 to
  +50, mostly negative (brake later).
- **Byte 2** (corner speed km/h) is non-zero on 487 points, from −42 to +61;
  the largest boosts are on the banked oval (Super Speedway +49 km/h), whose
  road mesh itself is almost flat, so the designers encoded the banking as
  speed.
- **Byte 3** is always 0. The packed word's high 16 bits are 0 or 0xFFFF
  (sign extension of the heading).

## 13. Pit strategy (added by the physics-spec workstream, 2026-10-11)

Facts from `0x1E8340` (verified on 3000/3000) and `0x1EDB40` (17/17
scenarios), both checked by running the original code in the diff harness.
The pit stop itself (markers, the 5 s stop, the tire change) is in
docs/specs/physics.md §8.

- **The driver model makes no decision of its own.** It reads no tire wear
  value (wear, wear level, wear rate), no lap schedule and no random number.
  Its only tire-state read is the corner-speed grip estimate `0x1F6678`. That
  estimate scales mu by the wear grip factor (½ × Σ over the 4 wheels of
  grip_i × mu_axle) and is skipped on the pit-lane surface.
- **Arming** (`0x1E8340`, every step, CPU cars only: control source 1 or
  2). All of these must hold:
  - tire wear is enabled for the race;
  - the mode allows pits (single player, free practice, battle, machine
    test);
  - some wheel has wear ≥ 0 and a wear grip factor below cp+0x20 (0.91 in
    every shipped event).
  With the shipped curve that means wear above about 159,600, and only on
  compounds whose grip floor min(1, 1.25/mu) is below 0.91 (every compound
  except "control"). The car then gets the "tires worn" and "pit request"
  flags. Control source 1 is not armed when the race-flow byte
  (K+0x8000+0x37AC = Race +0x9784) is 3. In that flow the human entry
  itself runs on control source 1 (AI-driven with the pad decoded for
  display); value 2 is the attract/demo mode. Which menu path passes 3 is
  not traced. The human is never armed.
- **Entry:** at the armed car's next forward crossing of AI line 4 point 0,
  unless the race is over for it (last lap reached or time limit), the car
  switches to the pit lane line. It then follows the common sequence: tire
  service if any tire has been used and its speed is < 100 km/h at 25 m
  before its box; 5 s stop; tires replaced.
- **The AI driver drives the pit lane** [C, 2026-10-20]: there is no separate
  pit driver. `0x1EDB40` only switches the driver's line and kind
  (`0x1F8980` with a2 = 1: previous line := current := new, blend weight 0 —
  an immediate switch; a2 = 0 blends):
  - entry (`0x1E3D08`): line 3 at once, kind 2, F+0x535 bit 3;
  - stop decided (`0x1E3D60`): line 4 at once, kind 3;
  - drive-through decided (`0x1E3DA8`, three call sites for the three
    reasons): line 3 at once, kind 4; the same after the stop (0.5 m past the
    box);
  - a CPU car (or the AI-driven human, kind 1) on the pit surface whose collision
    body flag +0x43C is set enters at once (`0x1E3D08`) with recovery state 2
    [C, 2026-10-25];
  - exit (`0x1E3BF0`): recovery state 0; a CPU car in a pit kind: line 6
    blended, kind 0; a human: line 7 (→ 6) blended, kind 0; a CPU car in another
    kind: kind 0 with no line request; a failed request leaves the kind [C, 2026-10-25].
  The speeds are the driver's own (§5): pit speed + 2 km/h in the pit lane,
  20 km/h on the stop line; during the stop counter the countdown blipper runs
  (§10).
- **After the stop** the wear is back to the fresh value, so the flags clear.
  The car re-arms only when its wear crosses the threshold again.
- **Summary:** all CPU cars use one deterministic threshold. They pit
  earlier or later only because of their tire compound (wear rate, grip
  floor) and how much they slide.

## 14. Traffic and contact: exact facts (physics-spec, 2026-10-11) [C; execution-verified where marked]

These facts supersede §8 and §9.3 wherever they differ.

**What the AI knows about other cars.** Only the contact intensity c (driver
object +0xB0) and eight zone bytes (+0xB4..+0xBB). Both are recomputed
every tick by the proximity pass `0x1EC098`, which runs after the collision
step, so the AI reads them one tick later. The AI never reads another car's
speed, position, rank, line, the draft matrix or the relative-position codes
(those feed only a HUD state machine).

**Pairing** (`0x1EBBC8`, per unordered pair; C, every call verified in lockstep, §15
[2026-10-25]): a pair is skipped when either car is inactive (car+0x136E ≠ 0), when only
one of the two is on the pit surface (F+0x535 bit 4 differs), when the centre distance
(a true 3-D distance of the F positions, i.e. the CoG) exceeds 100 m, or when B's height
above A is outside [−(h_B + 5 m), h_A + 5 m], h = P+0x0C = the body height (1.19–1.59 m on
the Sunday Cup cars). Otherwise B's centre is taken along A's own axes: u = d · F+0x0C
(forward), v = d · F+0x18 (the lateral axis, + = right), both 3-D, and the same for A in B's
frame. The zone cells are filled within 20 m (a distance of exactly 20 m counts), the
contact pass only below 20 m.

**Contact intensity c** (`0x1EBDA0`, `0x1F6CC0`; verified 30,000 and 20,000
random states):
- Evaluated for each pair with centre distance ≤ 20 m (a true distance:
  the listing shows the sqrt.s as "c1 0x4"; corrected 2026-10-11), by each
  car against the other, keeping the maximum. Only cars with control source
  ≠ 0 or driver kind ≠ 0 get a value, so a pad-driven player never does.
- Gap p = (u − B's rear extent) − A's front extent (in that order); it must be > 0.
  Lateral window: −W ≤ v ≤ W with W = 0.5·(width A + width B). Closing speed
  q = −F+0x0C_A·(v_B·s_B − v_A·s_A) over all three components (s = per-car time
  scale F+0x524); a value exists only while approaching (q > 0). Only an A with
  control source ≠ 0 or driver kind ≠ 0 gets a value.
- c candidate:
  - q > 10 m/s → 1.0;
  - else q > 1/3.6 (the float 0.27777779 at 0x3505D8) → 0.5 if p/q < 3, else 0;
  - if the candidate is 0 and p < 2 m → 0.25.
  c = max(previous this tick, candidate), so c ∈ {0, 0.25, 0.5, 1}.
- With the 20 m gate, c is active whenever a car is closing on another
  within about 15 m of bumper gap (the 0.5/1.0 cases by closing time, the
  0.25 case below 2 m). A whole-race run of the original (diff harness,
  `race_contact_count`, Sunday Cup 3) shows c > 0 on 1,068 ticks for car 0.
  Implement it exactly:
  - use the body front/rear extents (P+0x00/P+0x04), not half-lengths;
  - scale each car's velocity by its own time scale in q;
  - keep the strict/non-strict comparisons as written.

**Reaction to a car ahead: only these two exist.**
- Throttle × (1 − c) in `0x1F6D70`, active after 5 s + 0.1 s × (car index +
  1) from the start. It is skipped for 30 ticks after a lane change, and a
  lane change replaces it on that tick (decision table verified on 221,184
  states).
- Braking distance d += 20·c in `0x1F7088`, only while the lane-change
  cooldown (driver +0x9A) is 0.
There is **no** following-distance controller, speed matching or passing-line
seeking.

**Passing lines.**
- Lane changes switch only among game lines 6 (main), 1 (A) and 2 (B).
  Normally left means B and right means A; a side map swaps them where the
  lines cross. **Left is tried first** on all shipped data.
- A and B are separately recorded polylines, not fixed offsets. On race
  courses A runs about 1.2–2.6 m right of main and B about 1.2–2.5 m left.
  They cross on 10–40 % of samples.
- Blend: on a change, weight w = 1, decreasing by the car's dt per tick (1 s
  of car time). The aim point is T_new + w·(T_old − T_new), both at the same
  lookahead.
- Cooldown: 30 ticks, started only by closing-triggered changes.
- No lateral nudge outside line changes. When a car is alongside, the AI
  takes the opposite line; a move right is blocked when status = 4, a move
  left when status = 3.

**CPU–CPU contact.** The collision manager treats every pair identically:
there is no ghosting or softening by control source, driver kind or layer.
Its only per-pair filters are "body active" (always set in single-player
races), road-segment adjacency (same or neighbouring segments) and a
bounding-box test. The response is physics.md §5.3 (j = −1.5·v_rel·n/K, no
friction). A car–car hit also sets the same recovery flags as a wall hit, so
a bumped CPU car can run its turn-round/reverse logic (§7): the edge or vertex
the response acted on decides front (B) or rear (A).

**Rubber band** (corrects §9.3):
- The pass mode is the event byte +0x1B6, which is **0 in all 454 events**.
- Mode 0 is a **chain from the reference car R** in both directions along
  the ranking. Each car's scale multiplies the next car's, so the factors
  compound outward from R. Cars ahead of R get the slow-down ramp and cars
  behind get the speed-up ramp, each against the car between it and R. R's
  own scale is never changed.
- **R** = the highest-ranked car whose control source is not 2, i.e. the
  player in a normal race (car 0 if none).
- Ranking: more laps first, then more progress; finished cars by finish
  order.
- The "not sped up" exclusion is bit 4 of F+0x535: the car is on an
  off-course/pit-surface triangle, not a ghost flag.

### 14.1 Lane-change decision table (`0x1F6D70`) [C; verified by execution on 221,184 input states, 0 mismatches; 2026-10-25: `ai::traffic_step` against the routine on 1,259,712 generated states and on every call of 10 whole races, §15]

Zone cells (driver +0xB4..+0xBB), rebuilt every tick by `0x1F6B90`:
- byte order: front-centre, front-left, front-right, left, right, rear-left,
  rear-right, rear-centre (left = negative lateral);
- values are plain stores of 1 or 2: a later pair can overwrite a 2 with a
  1, and the cells are cleared only once per tick;
- (all within the 20 m centre-distance gate) other car ahead (u > A's front
  + B's rear extent; behind: u < −(A's rear + B's front)): front-centre = 2 plus
  front-left = 1 (v ≤ 0) or front-right = 1 (v > 0); when |v| > ½(wA + wB)
  only front-left or front-right = 2;
- behind: the same pattern with the rear cells;
- alongside (|u| within the extents, no lateral window): left = 2 (v < 0)
  or right = 2 (v ≥ 0), plus front-left/front-right = 1 if u > 0, else
  rear-left/rear-right = 1.
**Every test treats a cell as "occupied" when it is non-zero** (1 or 2
alike). The lane lookups require the front-left/front-right cell to be
exactly 0.

Status byte (driver +0xAF): 0 none/straight; 1 approaching a braking point
(margin under 50 m); 2 braking zone; 3 arc mode 2 at full throttle; 4 arc
mode 1 at full throttle; 5 arc with partial throttle.

Per tick, after the throttle request is known (× the event multiplier), when the drive
decision is forward and the race time in ms (the race clock / 3) exceeds 5000 + 100 ×
F+0x4E1 (the race position, 1 = first; car index + 1 until the first ranking) (thr = throttle request, cd = cooldown counter; the routine is not
called otherwise, so the cooldown does not count then):
1. If cd > 0, decrement it.
2. On line 4 (pit stop line): nothing.
3. Eligible E = 1, except E = 0 on line 3 (pit lane) or when the status is
   1, 2 or 5.
4. Line 3 only: thr ×= 0.5 if (left and front-left are occupied) or (right
   and front-right are occupied) (also during the cooldown).
5. If cd > 0 (after step 1): stop.
6. If E = 0: thr ×= (1 − c); stop.
7. If c ≤ 0: go to step 9.
8. c > 0: try a lane change.
   - Left is looked up only if front-left = 0; right only if front-right = 0.
     The lookups (`0x1F9CE0` left, `0x1F9D38` right) read the side map, then
     `0x1F9C80` (→ B = 2 from lines 1 or 6) or `0x1F9CB0` (→ A = 1 from lines 2
     or 6), −1 when that line's slot is empty: left = B and right = A, swapped where
     the side map flags the span.
   - With P = the kind of the next control point (0 straight, 1 arc; 2 not
     on the disc):
     - P = 1: left if available, else right unless status = 4;
     - P = 2: right, else left unless status = 3;
     - otherwise: left unless status = 3, else right unless status = 4.
   - If a change is made: cd = 30, and stop (no throttle lift that tick).
   - If none: thr ×= (1 − c), then continue to step 9.
9. Alongside logic: S = sum of front, front-right, front-left, left, right.
   - **S = 0: request line 6 (main) every tick**, even when already on it.
   - S ≠ 0:
     - only left occupied: move to the right candidate unless status = 4;
     - only right occupied: move to the left candidate unless status = 3;
     - both or neither occupied: nothing.
     These lookups do not check front-left / front-right.
   - **These alongside moves and the return to line 6 start no cooldown.**

**Side map** (`0x1FA530`, with the span search `0x1F9F08`) [C, 2026-10-25; verified
on generated maps and in lockstep, §15]: with no line 5, not swapped. Otherwise the flag
is the kind byte (non-zero = swapped) of point i:
- s before point 0 or at / past the last point: i = the last point;
- else the first span [k − 1, k) with distance(k − 1) ≤ s < distance(k) found when
  trying k = h, h + 1, h − 1, h + 2, h − 2, … (wrapped once into 0 .. count − 1, k = 0
  skipped), h = the driver's next-point index on its own line (D+0x82; 0 when larger
  than the map's count); none found → not swapped.
On ordered maps the hint does not matter. midfield's and r_apricot's maps are stored in
descending order, so every distance falls under the first rule: their flags never apply.
Several licence maps (L_grandvalley2*, L_tahitidirt1, L_trialmt1/2) have a flagged last
point, which then applies outside the map's span.

**Line requests and the blend.** Every request (`0x1F8980`, a2 = 0) resolves the line by
the fallback table (§2.1); with none it does nothing. Otherwise previous line := current,
current := new, w := 1, and the segments are re-planned (§4.1) — also when the new line is
the current one. w decreases by the car's dt (car+0xEC0 = time scale / 60) at the start of
each AI tick (`0x1F79A0`), before the aim point T_new + w·(T_old − T_new) is computed, and
the traffic step runs after the aim point. Because the return to line 6 is re-requested
every tick while S = 0, the second request sets previous = current = 6: **the return to
the main line aims at the old line for one more tick, then snaps to line 6** [C]. Closing
changes and alongside changes blend over the full 1 s, unless a later request overrides
them. The every-tick re-plan is otherwise idempotent: it runs only at statuses 0, 3 and 4
(not while braking), where the mode equals the current segment's kind.

## 15. Contact counts, and how the traffic rules were checked (2026-10-25)

**The question.** `race_contact_count` on Sunday Cup 1 (Super Speedway, 6 CPU cars, 2
laps, no countdown) showed ours with 130 car-car contact starts, 122 of them one pair (cars
0 and 2) riding side by side and touching for about 16 s, against the original's 36 (15 per
pair at most).

**What it was.**
- The scraping pair is the original's behaviour too. Car 0 moves "left" (line 6 → B) away
  from a car on its right while a car on B is coming up behind-left; the two end up on the
  same line side by side, in a long left-hand arc at full throttle (status 4, which forbids
  moving right) and with nowhere further left (no left line from B). The original, run with
  a 30-frame start hold, does the same with the same two cars from 14 s (contact starts
  15–24 s, `side` contacts, both on line 2, status 4).
- The count is chaotic in both. Over 20 start holds (0–600 frames) per side, with the
  harness fixes below: contact starts, original median 82 (mean 95), ours 60 (68); touching
  pair-ticks 257 (264) vs 158 (178); the most starts of one pair 64.5 (68) vs 44 (43);
  nose-to-tail starts 37 (50) vs 17.5 (19). The hold-0 race that looked so different has
  105 starts on the original side once its drivers get the course's sets (78 for one pair).
- Two harness faults made the sides differ: the original's course container lacked slots
  37–40, so its drivers fell back to the built-in set 0 (corner speed 80 %, L₁ 40 m,
  v_L 180 km/h, TCS slip scale 1.0 instead of the course's 93 %, 50 m, 200 km/h, 0.6); and
  our side gave every car the built-in set 0 with class-0 overrides. Both now use the
  course's set per driveline class, as the game does, and our side runs the same countdown.

**Rule differences found and fixed** (all small; none explains the counts): §4.1 index
stepping and the "count" state, the arc ticks kept across point changes; §4.2 / §4.3 the
top-speed parameter (ours estimated it from power and drag, 2–5 % high) and C_car (ours
from a placeholder deceleration); §4.3 status 2 through the braking modes (ours allowed
lane changes while braking), the stop line; §14 the pit-surface and height rules of the
pairing and the 3-D axes; §14.1 the cooldown counting only when the step runs, the pit-lane
halving during the cooldown, every request re-planning, the one-tick aim at the old line on
the return to line 6, the side map's search and its last-point rule, the 1 km/h constant.

**Verification** (diff harness, `diff_ai_traffic.cpp`):
- `ai_traffic_step`: `0x1F6D70` with the real candidate lookups on generated driver states
  (every line / status / cooldown / c / near-cell / next-kind combination, random side maps,
  passing lines present or not) against `ai::traffic_step` + `ai::lane_candidates`:
  1,259,712 states, 0 mismatches (throttle within 1 ulp of the EE's rounding).
- `ai_side_map`, `ai_zone_cells`, `ai_contact_intensity`: `0x1FA530`, `0x1F6B90`,
  `0x1EBDA0` + `0x1F6CC0` on generated inputs incl. boundaries: 0 mismatches.
- `ai_traffic_race`: a whole race on the original's race step with every call of the
  traffic step, side map, point bookkeeping (index, status 2), re-plan, zone cells and
  contact replayed through `ai::` on the original's own inputs, and `ai::pair_view` on
  snapshots of the original's cars against the distance, u, v (1 mm) and closing speed
  (1 mm/s) it computed. 0 differences over 11 events (Gsunday_e_0001–0003,
  Gclubman_e_0003, Gff_e_0001–0002, Gevolution_e_0001, Ggtworld_n_1001 / 1010,
  Gpolyphony_h_1002, Gna_e_0001 on super, midfield, trialmt, deepforest, route5_dry,
  laguna, akasaka, grandvalley, r_midfield, apricot): 851,439 traffic-step calls, 77,279
  side-map lookups, 872,017 index steps, 34,042 braking-mode ticks, 491,272 re-plans,
  2,885,228 zone evaluations, 834,074 contact evaluations, 107,582 closing speeds.

Open: the rest of the counts' spread comes from driving (steering, physics), not from
traffic decisions; the AI's peak slip angle α_max (§6) and the slope / bank of the points
(§11) are still our derivations.
