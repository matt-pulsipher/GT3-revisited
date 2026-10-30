# AI drivers: behavior

This page describes GT3's computer-controlled ("CPU") drivers as every race runs them: how a
driver reads the course's AI lines, plans its speeds, steers, recovers from crashes, deals
with other cars and starts a race, how the race grids the CPU cars, sends them to the pits,
finishes them and stretches or squeezes the field with its rubber band, and how the same
driver takes over the player's car (run-ups, the pit lane, after the run, the autopilot) and
gives the player a recommended gear. The driver is GT3's own, step for step, in the PS2's
single-precision arithmetic: it runs inside each car's race step (physics §1.2). Code
comments cite this page as `ai §n`.

| Code | Contents |
|---|---|
| `src/sim/exact/tick.h` | the phases of a car's step, with the AI driver's place in phase 3 |
| `src/sim/exact/tick_ai.cpp` | the AI's step: start and stop holds, the parked car, the clutch rule, drive decision and recovery, steering, throttle and brake by mode, steering saturation, traffic, the stop driver, the gear hint (§1, §3, §5–§8, §10, §16) |
| `src/sim/exact/tick_ai_plan.cpp` | point bookkeeping, corner speeds, braking point, lookahead and target point, speed controller, line changes (§3, §4) |
| `src/sim/exact/tick_input.cpp` | the driver's outputs into the car's controls (§1) |
| `src/sim/exact/tick_proximity.cpp` | zone cells and contact intensity between cars (§8.1, §14) |
| `src/sim/exact/tick_wear.cpp` | the tires-worn flag and the pit request (§13) |
| `src/sim/exact/ai_course.*`, `ai_setup.*` | the AI lines as the race reads them: preparation from the course file and the road, fallback, position on a line, side map, passing lines (§2, §8.2) |
| `src/formats/course/ai_lines.*`, `driver_sets.*` | the course file's AI lines (slot 31) and driver parameter sets (slots 37–40); format in `docs/formats/course.md` |
| `src/sim/exact/program_tables.cpp` | the built-in driver parameter sets and the line tables (§2.1, §9.1) |
| `src/sim/exact/start.*` | the start: parameter set, event overrides, placement, initial line, the player's run-up (§9.1, §9.2, §10, §11) |
| `src/sim/exact/race.*`, `race_flow.*`, `race_inputs.*`, `tick_race.cpp` | the race: step order, laps and finish, hand-backs, race order, the event's configuration, the rubber band (§1.1, §1.2, §9.2, §9.3, §11, §14) |
| `src/sim/exact/pit_lane.h`, `tick_pit.cpp` | the pit lane, the driver-kind switches, the end of a run (§1, §1.1, §13) |
| `src/race/exact_field.cpp` | the race for the race screen: who races, grid order, the AI take-over, the gear hint (§1.2, §11, §16) |
| `src/app/screen_race_setup.cpp`, `screen_race_drive.cpp`, `screen_race_license.cpp` | configurations 2 (attract demo) and 3 (autopilot), the race screen while the AI drives the player's car, demonstrations (§1.2) |

Implementation notes live in `docs/dev/AI.md`.

Conventions: one step is 1/60 s. Every race steps 60 times a second on every disc,
demonstrations included: the disc's demonstrations are US recordings and play with the US
release's rules on every disc. Where a disc release's rules change something here (PAL's
rubber band cap and the Japanese release's split-screen catch-up, §9.3), the race follows
the disc being played; online races and the attract demo follow the US release's rules
too. Speeds are in m/s
unless given in km/h, angles in radians unless given in degrees. The AI works in the
physics frame's horizontal plane (physics §1.3: x right, y forward at heading 0, z up; a
positive heading turns left). v is
the car's forward speed (signed, physics §10.1). A car's *lap progress* s is its distance
along the lap from the start line, as the race measures it from the road after every step.
"Left" and "right" are the driver's.

---

## §1 The driver and its outputs

Every car of a race has an AI driver: its parameters (§9), its line and speed plan
(§2–§4), its recovery state (§7) and its outputs. A car's *control source* says who drives
it:

| Control source | Driven by | Cars |
|---|---|---|
| 0 | the pad | the players' cars; ghost cars, whose pad frames come from a recording (§11) |
| 1 | the AI, with the pad decoded for the HUD only | the players' cars under the autopilot (the race's configuration 3, §1.2) |
| 2 | the AI | every CPU car; every car of the title attract demo, which runs as configuration 2 (§1.2, §9.2); the other player's car once a split-screen battle's race has ended for one of them (§1.2) |

The AI steps in phase 3 of the car's step (physics §1.2): always for control sources 1 and
2, and for a car on the pad while its *driver kind* (below) is not 0 and no hold of free
practice's start or of a pit stop runs (physics §4.3). Otherwise the pad drives the car and
its driver only keeps its plan up to date (§4.1, §4.3) for the recommended gear (§16).

The driver depends on nothing but the race's state: a replay re-runs the race from its
recorded set-up and pad frames and every CPU car drives the same.

### Step order of one AI step

1. The car's handbrake, reverse request and shift request are cleared.
2. During a hold (the race's start countdown, free practice's start or a pit stop): the
   hold's throttle (§10) and the clutch request 0; nothing else. The steering and brake
   keep the driver's last outputs (straight and off at the start).
3. Parked (driver kind 8): the parked car (§1.1); nothing else.
4. The clutch request (below).
5. The decision:
   1. the outputs cleared (the steering output starts from the current command) and the
      line blend weight decays by dt (§3);
   2. driver kind 7: the stop driver with the wheels straight (§1.1); done;
   3. point bookkeeping and braking point (§4.1, §4.3), which give the throttle multiplier;
   4. lookahead, target point and heading error (§3);
   5. drive decision and the recovery state's change (§7);
   6. the steering target (§6); driver kinds 5 and 6: the stop driver with it (§1.1); done;
   7. the steering output slews towards the target (§6);
   8. driving forward: throttle and brake by mode (§4.4, §5), times the multiplier, then the
      traffic step once its start delay has passed (§8.2, §14.1); braking or reversing:
      the pedals of §7;
   9. the steering saturation cut (§5).
6. The outputs into the car's controls.

### Outputs

| Output | Meaning |
|---|---|
| steering command | the road-wheel steering command (before each wheel's ratio and toe, physics §4.4), set directly; the driver never asks for more than ±45°, and the car's steering lock does not limit it |
| throttle | 0..1. In the arc modes 1 and 2 (§5) the applied throttle moves towards the driver's by at most dt per step (1 per second); elsewhere it takes it at once |
| brake | 0 or 1, except where the saturation term adds to it (§5) |
| reverse request | while the drive decision is "reverse" (§7) |
| gearbox lock-out | 12 steps whenever the speed controller asks for part throttle (§4.4): the gearbox does not change gear while it runs |
| HUD steering | the command in percent of the car's full lock |
| handbrake | on while parked (kind 8) |
| recommended gear | the gear for the speed the plan aims at (§16) |

### The AI-driven car in the physics

A car the AI drives (control source 1 or 2, or a car on the pad in a driver kind other
than 0 outside a hold):

- uses the automatic gearbox, whatever the entry's pedal style (physics §2.6);
- uses the AI's clutch request: while the clutch is open it asks to engage once the engine
  turns faster than ¾ of the car's upshift rpm; otherwise it keeps it engaged unless the
  engine is below idle. Every hold and the parked car keep the request at 0 (the clutch
  opens and stays open, physics §2.4);
- reads its driver's aid block (§9.1) instead of the car's own, with ASCC enabled
  (physics §4.6); a player's car keeps showing its own TCS and ASCC settings on the HUD;
- takes the AI's oversteer indicator (physics §4.6).

### Driver kinds

| Kind | Meaning |
|---|---|
| 0 | normal: a CPU car races, a player's car is on its pad |
| 1 | the AI drives on: a player's run-up, after a finish, a demonstration's take-over, a free run's ghost past its line (§1.1, §1.2) |
| 2 | the pit lane, on the pit lane line (§13) |
| 3 | the pit stop line, into the box |
| 4 | leaving the pits, on the pit lane line |
| 5 | the stop driver along its line, brake off (no end of a run asks for it) |
| 6 | the stop driver along the main line, braking |
| 7 | the stop driver with the wheels straight |
| 8 | parked |

The race switches the kinds; each switch first changes the driver's line where it says
so, and changes nothing when the course has no line for it (§2.1):

- **drive on** (kind 1): a car in a pit kind, or a car on the pad in kind 0, first changes
  to the main line (blended, §3); others keep their line;
- **stop along the line** (kind 6): to the main line, blended;
- **stop straight** (kind 7): no line change;
- **into the pit lane** (kind 2): the pit lane line at once; **to the stop line** (kind 3):
  the stop line at once; **leaving** (kind 4): the pit lane line at once;
- **back to racing** (kind 0, also the hand-back to a player): the recovery state back to
  0; a car on the pad back to line 7 (or its fallback, the main line), a CPU car in a pit
  kind back to the main line, both blended.

### §1.1 End of a run and the stop driver

When a car's run ends, the race marks it finished and the AI drives it from then on (the
player's pad is no longer read):

| Race | The run ended with | Driving |
|---|---|---|
| single races (GT mode, arcade), online battles | the car's line crossing once the leader has finished, its laps are done, or the event's time limit has passed | the run-on (below) |
| split-screen battle | the car's line crossing once the leader has finished or its laps are done | the run-on; the other player's car becomes a CPU car (§1.2) |
| time trial | its timed lap (a time trial is one lap) | the run-on |
| time trial | a course-out or a hard wall hit (physics §11) | stop with the wheels straight (kind 7) |
| license test | the end of a stop test (type 1) | stop along the main line (kind 6) |
| license test | the end of any other test | the run-on |
| license test | a course-out or a hard wall hit | stop with the wheels straight (kind 7) |
| machine test | its goal line (all three tests) | the run-on |
| top-speed machine test | a pause EXIT counted as its finish (presentation §E5) | the run-on, as at its goal |
| free run | a pause EXIT counted as its finish, after a lap (presentation §E5) | the free run's case (below) |

A free run ends only by that pause EXIT; free practice has no end of a run. A replay judges no finish in time
trials and machine tests: their recorded pad frames drive on.

A player's car first takes the event's overrides for its driveline class (§9.2). A car
that is in the pit lane when its run ends stays with the pit lane, which drives it out and
gives it the run-on at the pit exit (§13).

**The run-on**: the corner speed scale × 0.8 and the throttle multiplier set to 0.5 (in
place of the event's throttle, §4.3), then drive on (kind 1). Corner speeds already planned
stay until they are planned again (§4.1); a player's car changes to the main line, which
plans them again at once. CPU cars get the run-on too when they finish.

The end-of-run rule has one more case, for the end of a free run: stop with the wheels
straight (kind 7) on the pit surface, else drive on (kind 1) without the slowdown. A free
run's pause EXIT counted as its finish ends the run with it (`ExactRace::end_run`; test
`exact_game_exit_as_finish`).

**The stop driver** (kinds 5, 6, 7): no throttle, the steering output slews towards its
target (kind 7: straight; kinds 5 and 6: the target §6 computes towards the line), and the
brake full (kind 5: off). Once the car is at rest (3-D speed below 0.25 km/h and angular
speed below 3°/s) its kind becomes 8 and the brake comes off on that step.

**Parked** (kind 8): the steering slews back to straight, throttle and brake are 0, the
handbrake is on and the clutch request is 0; the physics holds the car's planar motion
(physics §4.1).

### §1.2 When the AI drives the player's car

The race's AI is the only computer driver: it drives the player's car with the player's own
driver (the course's parameter set for the car's driveline class, else the built-in set,
§9.1; the event's overrides only under the autopilot and after the run). On its pad the
player's driver plans along line 7, which falls back to the main line (§2.1).

| Situation | The AI takes the car | It hands it back | What the player sees |
|---|---|---|---|
| rolling start (GT mode's endurance events) | from the start, moving at 80 km/h; it changes to the main line | after 300 steps (5 s) | the car driving itself through the first 5 s |
| flying start (time trials, free runs, flying license tests) | from its place before the start line (§10) | at its first crossing of the start line; a free run whose start came from the course's driver set also after 180 steps (3 s) | AUTO DRIVE up to the line (not in license tests or the top-speed test, presentation §B4) |
| the pit lane | when the car crosses the pit lane's entry on the pit surface (§13) | at the pit exit | AUTO DRIVE, the pit-in pop-up, the stop in the box with new tires |
| the wrong way in the pit lane | when the car, the wrong way on the pit surface, passes the third stop-line point from its end backwards; the AI turns it round and drives it out | at the pit exit | AUTO DRIVE |
| free practice (GT mode's free run before a race) | the car starts in its pit box, held for 120 steps (2 s) | at the pit exit | the car driving itself out of the pits |
| after the run (§1.1) | at the finish, course-out or test end, or a pause EXIT counted as the finish (a free run, the top-speed test) | never | the car driving on slowly, or braking to a stop and parking; a finished 2P half shows AUTO DRIVE |
| split-screen battle, the other player | when the race ends for the first car: the other player's car becomes a CPU car (control source 2) and races on at full pace | never; at its own finish it gets the run-on | that player's pad no longer drives |
| a race's demonstration (quick menu DEMO) | when the race restarts for it, driving on (kind 1) | never | the TV camera |
| a license test's demonstration | the disc's recorded run plays as a race record of its pad frames, with no AI take-over; only when the disc has none for the test does the AI drive on (kind 1) | never | the TV camera |
| the autopilot (the `auto=1` test preset) | the race's configuration 3: from the start, as a CPU car with the event's overrides, the pad decoded for the HUD only | never | the car driving itself |
| the title attract demo | every car, the one the demo follows included: the race runs as configuration 2, every car a CPU car from the start | never | every car driven, the TV camera, no HUD |
| a free run's ghost car | at each of its own line crossings, driving on (kind 1) | when the player's next lap puts it back on its recording | the ghost is not drawn while the AI drives it |
| the Analyzer's pass over a free run | every car at each of its line crossings, driving on (kind 1) | never | (the pass records the data logger's tables) |

In every case the run-up, the pit lane and the end of the run are the race's own decisions:
a replay plays them again from the recorded pad frames.

---

## §2 AI lines

A course file carries up to seven AI lines (format: `docs/formats/course.md`, "AI lines
(slot 31)"). A line is a list of sparse control points; from each point a straight or a
circular arc runs to the next. The race prepares them once per course, before the cars are
built, and every car's driver reads the same lines.

### §2.1 Line numbers, roles, fallback, initial line and line changes

The driver numbers the lines 1–7; line k is file line k − 1.

| Line | Role | Notes (`gt3tool ai-survey` over the US courses) |
|---|---|---|
| 1 | line A, a passing line (the right one, §8.2) | a full lap beside the main line, mostly 1–2 m off it |
| 2 | line B, a passing line (the left one) | a full lap on the main line's other side, mostly 1–2 m off it |
| 3 | pit lane | a lap that passes through the pit lane |
| 4 | pit stop line | 12 points over part of the lap; its points mark the pit lane (§13) |
| 5 | side map | never driven: a per-point flag that swaps left and right (§8.2) |
| 6 | main racing line | the line the driver normally follows; on some license courses the only line besides line 7 |
| 7 | the player's line | present on some courses; a car on its pad plans along it (§1.2, §16) |

A line is present when it has at least one point (and the course file has the AI line
section at all). When the driver asks for a line, the first present line of its row is
used:

| Wanted | Tried in order |
|---|---|
| 1 | 1, 6, 2 |
| 2 | 2, 6, 1 |
| 3 | 3 |
| 4 | 4 |
| 5 | none |
| 6 | 6, 1, 2 |
| 7 | 7, 6 |

A number outside 0–7 asks for 6; 0, like 5, has no row. If nothing in the row is present
the request fails and the driver stays on its line.

**Initial line.** At the start a CPU car considers lines 6, 2 and 1, each only if it is
present itself (not through the fallback): it takes the one whose position at the car's
lap progress (§2.3) is horizontally nearest the car, if nearer than 100 m (ties to the
earlier of 6, 2, 1), else line 6 through its fallback. The rolling start's player car does
the same. A car placed on the main line for a flying start starts on the main line (§10);
any other car on its pad starts on line 7.

**Line changes.** A change is either at once (the old line is forgotten) or blended over
1 s (§3); both plan the segments again (§4.1). The pit lane's changes into the lane, the
stop line and back to the lane are at once; the return to racing, the run-up, the end of a
run and the traffic step's changes are blended.

### §2.2 Control-point fields

The race reads each 40-byte point of the course file as:

| Field | Use |
|---|---|
| x, z | position: physics x = −x / 4096, y = −z / 4096 |
| flags byte 0 | *kind*: 0 a straight from here, non-zero an arc (§2.3). On the side map (line 5) it is the swap flag (§8.2) |
| flags byte 1 (signed) | brake adjustment, percent: the braking distance to this point is scaled by 1 + b/100, at least 0.1 (§4.3) |
| flags byte 2 (signed) | speed adjustment, km/h, added to the corner speed of the arc starting here (§4.2) |
| radius | signed arc radius in meters (> 0 turns left) |
| heading | the travel direction at the point (4096 = one turn), used to build the arc (§2.3) |
| road segment | the road segment under the point |

When the course is loaded each point also gets, from the road under it:

- **road segment**: the file's, when any point of any line carries one; otherwise the
  segment is traced along each line from point to point (straights every 10 m, arcs every
  10°; the stop line's points are probed where they lie), and the tracing also marks each
  arc's kind 1 (turning left) or 2 (turning right). 8 of the 80 US course files, all
  license-test courses, carry no segments and are traced;
- **lap distance**: its lap progress, from its segment. On the pit stop line (line 4) a
  lap is added from the first point in the first half of the lap on, so its distances run
  on across the start line;
- **height, slope and bank**: the road's height under the point, its slope along the
  point's heading (uphill positive) and its bank across it (positive when the left side is
  higher), from probes half a meter ahead, behind, left and right (one side and the center
  where the other misses); all 0 off the road or where both probes of a pair miss;
- **surface**: the road's surface index under the point (0 off the road), which picks the
  tire records of the corner speed and braking plans (§4.2, §4.3).

The side map's points get only their segment and lap distance.

### §2.3 Position on a line at a lap progress

The position of a line at lap progress s (s wrapped once into the lap):

1. The span is the pair of consecutive points (from, to) with dist(from) ≤ s < dist(to),
   searched from the driver's next point outward. Before the line's first point or at or
   past its last, the span runs from the last point across the start line to the first.
2. t = (s − dist(from)) / (dist(to) − dist(from)), with a lap added across the start line;
   the height is interpolated linearly.
3. A straight (from's kind 0): from + t (to − from).
4. An arc: the circle of radius |r| (r = from's radius) whose center lies |r| to the left
   of from's stored heading for r > 0, to the right for r < 0. The angle runs from from's
   to to's, counter-clockwise for r > 0 and clockwise for r < 0 (so a sweep can exceed 180°
   in hairpins), at the fraction t.

If no span is found the position is not updated: the target search (§3) keeps its previous
try's point, or the course's origin (x = y = 0) on its first try.

The pit stop line has its own evaluation: straight spans only, its lap-shifted distances,
and nothing outside its first and last points.

### §2.4 Other users of the line data

The race takes the points from the course file parser (`course::AiLines::parse`) and reads
them only as §2.1–§2.3 describe. The parser's other results serve tools and course previews:
it marks a line closed when it has at least four points and its ends are less than 250 m
apart; `course::AiLine::sample_arcs` samples the straight and arc chain;
`AiLine::sample(road, step)` and `track::Track::racing_line` add road heights and close the
loop when the line is closed or, from three points up, its points cover more than 60 % of
the lap with less than 40 % of it from the last point back to the first.

---

## §3 Target point

The driver steers towards a point on its line a *lookahead* L ahead of its lap progress.
The first row that applies:

| Situation | L |
|---|---|
| driver kind 3 (on the pit stop line, §13) | 5 m |
| driver kind 4 (leaving the pits) while held at the pit speed on the last step (on the stop line, or in the pit lane's limit section, §4.4) | 20 m |
| recovery state 1 (reversing out, §7) | 10 m |
| recovery state 2 (turning round) | 40 m |
| otherwise | L₀ + (L₁ − L₀) · min(1, v / v_L); L₀ for v ≤ 0 |

L₀, L₁ and v_L come from the parameter set (§9.1). In the last row only, for the first 2 s
of race time L is lengthened by 40 m × (2 − t)/2 (the full 40 m at the start), so cars
leaving the grid aim far down the road.

The target is the line's position at s + L (§2.3). While the line blend weight w is above
0 (after a blended line change) the target is the blend of the old line's and the new
line's positions at the same s + L, weighted w towards the old one; w starts at 1 and falls
by dt per step, so a change is blended over 1 s. If the target is nearer than 5 m
horizontally, L grows by 1 m and the target is found again (at most ten tries in all).

The **heading error** ε is the horizontal angle from the car's heading to the direction of
the target.

---

## §4 Speed planning

The driver works segment by segment along its line: the *current* segment (the one it is
on) and the *next* one (from its next control point on). A straight's target speed is the
car's top speed v_top, an arc's its corner speed.

### §4.1 Point index change

Each step (except on the pit stop line) the driver's *next point* moves by at most one:

- forward, when the car's lap progress reaches the next point's distance;
- back, when the progress falls below the previous point's distance;
- across the start line: from the first point to "past the last" when the progress is in
  the second half of the lap, and back to the first point when it is in the first half.

When it moves, the next segment becomes the current one (its kind becomes the *mode*, its
target the current target), and the segment from the new next point is planned: its kind,
and its target speed, the top speed for a straight or its corner speed (§4.2) for an arc.
Past the last point the last point's segment is planned.

**Planning again** (at the start, at every line change, and when the AI takes a car over):
the next point is the first one whose distance is beyond the car's progress; the segment
before it is planned and becomes the current one, the segment from the next point the
next one.

### §4.2 Corner speed

The corner speed of the arc from point P (radius r, surface σ, bank β, speed adjustment Δv
in km/h) is planned when the segment is planned (§4.1):

- each axle's grip: g_a = G · μ_a(σ), the tire record the axle uses on surface σ (physics
  §6.5): its grip for that surface times the record's base grip G (9.8 × D(0): 9.8 for
  every compound on the disc except `medium_evo`, 9.8 × 1.02; test
  `params_tire_base_grip_disc`); weighted
  μ = w · g_f + (1 − w) · g_r, w the parameter set's front weight;
- with tire wear on in the race and the car off the pit surface, each axle's term is
  multiplied by the mean wear grip factor of its two wheels (physics §8.4);
- lateral acceleration a = μ · |r|, divided by 1 − sin|β| when the road banks into the arc
  (its outside higher);
- with downforce: from v = 0, three times v ← √(a (1 + k v²)) (the first gives √a), where
  k = (front + rear downforce coefficients) / (m g);
- v_c = k_speed · v + v_off + (Δv + 10 · trim) / 3.6, with k_speed and v_off the parameter
  set's corner speed scale and offset after the event's overrides (§9.2) and the run-on
  (§1.1); the trim is a debug term, 0 in every race;
- clamped to 20 km/h below and v_top above.

**Top speed.** v_top is the car's top speed as its parameters are built: walking up from 0
in whole-km/h steps to the speed at the rev limit in top gear, shifting up past the upshift
rpm, the last speed at which the full-throttle drive force (with the engine condition and
any power cap of the event) still beats rolling resistance (with the downforce load) and
drag. The driver is flat out on straights.

### §4.3 Braking point

Each step (except on the pit stop line) the driver checks whether it must brake for the next
segment. The check applies only when it is not already braking (modes 3 and 4), the next
segment is an arc, and v is above its corner speed v_n.

Braking distance:

d_b = (v² − v_n²) · max(C, 1 / (2 g μ_b)) · (1 − sin θ) · max(0.1, 1 + b / 100)

where θ is the slope and b the brake adjustment of the next point (the first point once
past the last). C = m · r_f / (4 · T_f) is the car's braking constant (front tire radius
and front brake torque). μ_b is the front tire record's grip for the point's surface times
its load sensitivity at half the car's weight per front tire width, its peak braking
fraction and the front camber factor (physics §3, §6.5), times the mean wear grip factor of
the two front wheels unless the car is on the pit surface. Then:

- if the brake factor k_brake (§9) is below 1, d_b is multiplied by it (later braking);
- if the traffic cooldown is over and the contact intensity c is above 0, 20 m × c is
  added (§8.1).

D is the horizontal distance from the car to the next point.

- **D ≤ d_b**: braking starts: mode 4 if the car is in an arc (mode 1 or 2), else mode 3;
  status 2. The braking mode lasts until the next point index change (or a line change,
  §4.1).
- **otherwise**, with the *soft margin* m_s = d_b · (k_brake − 1) when k_brake > 1 (else 0):
  status 1 if D − d_b < m_s + 50 m; and if D − d_b < m_s the throttle multiplier is
  (D − d_b) / m_s, so a cautious driver lifts gradually before its braking point.

The throttle multiplier (1 unless lifted) is then multiplied by the event's throttle (§9.2),
or 0.5 in the run-on (§1.1), except on the pit lane line; on the pit stop line it is 1.

### §4.4 Speed controller

Wherever the driver holds a speed it uses one controller. For a target speed v_t and gain K
(per m/s):

e = clamp(v − (v_t − 2 km/h), −10 km/h, +10 km/h)
throttle = clamp(0.75 + K · e, 0, 1)
brake = 1 if the throttle is 0 and v > v_t, else 0

So the car holds about 2 km/h under the target with three-quarter throttle, lifts above it,
and brakes fully only when it is over the target with the throttle already shut. At part
throttle (between 0 and 1) it also locks the gearbox for 12 steps (§1).

| Where | Target | K |
|---|---|---|
| braking from a straight (mode 3) | v_n | −2 |
| braking in an arc (mode 4) | v_n | −1 |
| in an arc (modes 1, 2) | current corner speed + corner bonus (§5) | −0.5 |
| recovery state 2 (turning round, §7), whatever the mode | 50 km/h (on the pit lines, below) | −0.5 |

On the pit lines the plan changes:

| Where | Plan |
|---|---|
| on the pit stop line (4) | mode 3 at 20 km/h (K = −2); in recovery state 2, 20 km/h with K = −0.5 |
| on the pit lane line (3), from stop-line point 1 to point 2 | the braking modes aim at the pit speed + 2 km/h instead of v_n; in recovery state 2 the current segment's target |
| on the pit lane line, from stop-line point 2 to point n − 3 (the *limit section*) | arc mode 1 at the pit speed + 2 km/h (K = −0.5), also in recovery state 2 |
| on the pit lane line elsewhere | as on any line, but in recovery state 2 the current segment's target |

The sections are found by the stop line's point distances (n points; lap-shifted, §2.2),
with the car's progress taken a lap on when it is in the first half of the lap. The pit
speed is the parameter set's (§9.1). "Held at the pit speed" (§3) means on the stop line or
in the limit section.

The result is multiplied by the throttle multiplier of §4.3.

---

## §5 Modes, corner bonus and steering saturation

| Mode | Meaning | Throttle |
|---|---|---|
| 0 | straight | full (× the throttle multiplier) |
| 1 | arc | speed controller at the corner target |
| 2 | arc turning right (only on courses whose lines were traced, §2.2) | as mode 1 |
| 3 | braking, entered on a straight | speed controller at v_n, K = −2 |
| 4 | braking, entered in an arc | speed controller at v_n, K = −1 |

**Corner bonus.** In modes 1 and 2, once the car has spent the parameter set's bonus delay
in arcs, the target is the corner speed plus the set's corner bonus, except on the pit
surface, where there is no bonus. In the pit lane's limit section the arc target is the
pit speed + 2 km/h (§4.4), with the bonus only off the pit surface. The driver deliberately
aims above its planned corner speed and lets steering saturation hold it back. The step
count runs in modes 1, 2 and 4 (braking inside an arc keeps counting; it stops at 6000,
100 s) and goes back to 0 in modes 0 and 3, in recovery state 2, and while braking or
reversing (§7).

**Status.** The traffic step (§8.2) reads a status the speed planning sets each step: 0
nothing; 1 a braking point is near (§4.3); 2 braking; in an arc, when neither 1 nor 2 is
set: 4 at full throttle in mode 1, 3 at full throttle in mode 2, 5 at part throttle.

**Steering saturation.** When the steering asks for more front slip than the front tires'
peak (§6) the saturation S = |α_wanted| / α_peak exceeds 1. From 2nd gear up:

throttle ← throttle · max(0, 1 − K_t (S − 1)), brake ← min(1, brake + K_b (S − 1))

with K_t = 1 and K_b = 0 in every built-in set: the driver lifts in proportion to how far
it is past the grip limit, and does not brake for it.

---

## §6 Steering

The steering target is worked out only while the car moves (|v| > 2 km/h); below that it
is straight. The recovery state used here is the one after this step's change (§7).

**Driving forward.**

1. In a recovery state (1 or 2) with |ε| > 80°, the wanted front slip angle is the peak
   slip angle α_peak (§6.1), towards the target.
2. Otherwise the driver wants the yaw rate that carries it on a circle through the target
   (pure pursuit): r_des = 2 · v_h · y / d², where y is the target's offset to the left of
   the direction of the horizontal velocity (of the heading below 5 km/h), d its distance
   and v_h the horizontal speed, negated while v < 0. With the car's yaw rate r, the
   wanted slip angle is α = K_y (r_des − r), but only while the car turns too little in
   the wanted direction (r_des > 0 and r < r_des, or r_des < 0 and r > r_des); otherwise
   α = 0.
3. If |α| > α_peak the saturation S = |α| / α_peak goes to §5, and α is clamped to
   ±α_peak, except on dirt courses (course category 2), where it is not.
4. The target is α + β, clamped to ±45°, where β is the mean direction of travel of the
   two front wheels relative to the car (the steer angle that gives them no slip; physics
   §3.2).

**Braking or reversing.** The target is ±30° towards the error when |ε| > 20°, otherwise
1.5 ε; while the car moves backwards (v < −2 km/h) its sign is reversed.

**Slew.** The steering output moves towards the target by at most the parameter set's
steering rate times dt: 90°/s in every built-in set.

### §6.1 Grip and peak slip angle

The **peak slip angle** α_peak is the mean of the two front tire records' peak slip angles
(the slip angle at which each record's lateral curve peaks, at most 45°) for the surfaces
the front wheels are on, every step.

The **grip** is not fixed either: each arc's corner speed and each braking distance read
the tire records' grip for the point's surface, with the live wear factors, when they are
planned (§4.2, §4.3). The top speed and the braking constant are the car's own, built with
its parameters.

---

## §7 Drive decision and recovery

Each step the driver decides to drive forward, brake, or reverse, and keeps a three-state
recovery machine: 0 racing, 1 reversing out, 2 turning round.

**Heading threshold** θ: 60°, except in state 1, where it is 10° while v ≥ 0 and
10° + 50° · (−v) / (30 km/h) while reversing slower than 30 km/h (so it opens up to 60° as
the reverse speed builds).

**Drive decision.**

| Condition | Decision |
|---|---|
| \|ε\| < θ, or state 2 | forward if v > −2 km/h, else brake |
| otherwise | brake if v > 2 km/h, else reverse |

Braking: no throttle, brake 1. Reversing: the reverse request with full throttle, no brake.

**Hit flags.** The state machine reads two flags of the car's last collision step
(physics §5.3, "Front and rear hit flags"): a *front hit* and a *rear hit*. A wall hit at
one of the car's collision points 0–3 is a front hit, at points 4–7 a rear hit; a wall
corner against the car's edge sets neither. In a car-car hit the car whose corner struck
gets that corner's end, and the other car the end of the edge that was struck (edges 0–2
front, 4–6 rear, the side edges neither).

**State changes.** There are no timers: each step the state follows from the heading error,
the forward speed and the two flags.

| From | To | When |
|---|---|---|
| 0 | 2 | \|ε\| > 80°, or a rear hit with \|ε\| > 45° |
| 1 | 0 | \|ε\| < θ and v > 3 km/h |
| 1 | 2 | \|ε\| < θ and v ≤ 3 km/h, or \|ε\| ≥ θ and a rear hit |
| 2 | 0 | \|ε\| < 40° and v > 30 km/h |
| 2 | 1 | otherwise, a front hit with \|ε\| > 60° |

Otherwise the state stays. The drive decision and the lookahead (§3) use the state before
this step's change; the steering (§6) and the speed plan (§4.4) the state after it. In
state 1 the lookahead is 10 m, in state 2 40 m (§3); in state 2 the car drives forward at
50 km/h (§4.4).

So a car facing the wrong way turns round in state 2 and races on; a car driven nose first
into a wall reverses out in state 1 until it faces the road, then drives on.

---

## §8 Traffic

### §8.1 Traffic inputs

After every step's collision step the race compares every pair of cars (§14) and gives each
driver two inputs, which its next step reads.

**Zone cells.** The other car's position in this car's frame: u along its forward axis, ℓ
across it. With the combined extents l_f = own front + other's rear, l_r = own rear +
other's front and W = the sum of the half widths, for a car within 20 m:

| Where the other car is | Cells set |
|---|---|
| ahead (u > l_f), beyond W to the left | front-left |
| ahead, beyond W to the right | front-right |
| ahead, overlapping sideways | front, and the front corner on its side |
| behind (u < −l_r), beyond W to the left / right | rear-left / rear-right |
| behind, overlapping sideways | rear, and the rear corner on its side |
| alongside, to the left | left, and front-left if it is ahead of the center, else rear-left |
| alongside, to the right | right, and front-right or rear-right likewise |

The cells of all nearby cars are combined. The rear cells are kept but no decision reads
them.

**Contact intensity** c ∈ {0, 0.25, 0.5, 1}, for a car the AI drives (not a car on its pad
in driver kind 0): for another car within 20 m that is ahead and overlapping sideways
(|ℓ| ≤ W) with a bumper gap p = u − l_f > 0 that this car is closing on (closing speed
q > 0, from the two cars' velocities each multiplied by its car's time scale, §9.3):

| Condition | c |
|---|---|
| q > 10 m/s | 1 |
| q > 1 km/h and p / q < 3 s | 0.5 |
| otherwise, p < 2 m | 0.25 |

c is the largest over all cars. It adds 20 c meters to the braking distance (§4.3) and
drives the decisions below.

### §8.2 Traffic decisions

Traffic decisions start once the race clock passes 5 s + 0.1 s × the car's race position
(1 for the leader), so the field leaves the grid on its lines and spreads out first. They
are taken only while the drive decision is "forward", never on the pit stop line, and not
during the cooldown after a lane change. The exact order is §14.1.

**Left and right lines.** From the main line or line A, *left* is line B; from the main
line or line B, *right* is line A. Where the side map (line 5) flags the span at the car's
progress (§2.3), the two are swapped. There is no left line from line B and no right line
from line A (unswapped), none from the pit lines or line 7, and none when the line is not
present.

**Passing.** With contact (c > 0), lane changes allowed (status not 1, 2 or 5) and the
cooldown over, the left line is a candidate unless something is front-left, the right line
unless something is front-right. Before an arc of kind 1 (every arc on a course whose
lines carry their road segments, a left-hand arc on a traced course, §2.2) the driver takes
the left line, else the right line; before a right-hand arc of a traced course (kind 2) the
right line if there is one; otherwise (on a straight, or with no right line) the left line
unless the status is 3, else the right line. The right line is never taken at status 4
except before a right-hand arc. A change is blended over 1 s (§3) and starts a cooldown of
30 steps (0.5 s).

**Lift.** If no passing line was taken, the throttle is multiplied by (1 − c).

**Making room and going back.** If nothing is front, front-left, front-right, left or right,
the driver asks for the main line (blended; on the main line already this only plans its
segments again, §4.1, which also ends a braking mode until the next step's braking check,
§4.3). If a car is alongside on exactly one side, the driver moves to the
line on the other side (not to the right at status 4, not to the left at status 3).

**Pit lane.** On the pit lane line the driver never changes lanes; it halves its throttle
when a car is both left and front-left, or both right and front-right.

---

## §9 Driver parameters

### §9.1 Driver parameter sets

A parameter set is 32 bytes (layout also in `docs/formats/course.md`, "Driver parameter
sets"). The game has three built-in sets:

| Byte | Meaning | Conversion | Set 0 | Set 1 | Set 2 |
|---|---|---|---|---|---|
| 0 brake % | braking distance factor | k_brake = 100 / % | 100 | 100 | 100 |
| 1 speed % | corner speed scale | k_speed = % / 100 | 100 | 100 | 100 |
| 2 speed offset | corner speed offset | (byte − 100) km/h | 100 | 100 | 100 |
| 3 yaw gain | K_y (§6) | byte × 0.05 | 24 | 24 | 24 |
| 4 saturation throttle | K_t (§5) | % / 100 | 100 | 100 | 100 |
| 5 saturation brake | K_b (§5) | % / 100 | 0 | 0 | 0 |
| 6 lookahead speed | v_L (§3) | byte × 2 km/h | 90 | 90 | 90 |
| 7 lookahead min | L₀ (m) | byte | 15 | 15 | 30 |
| 8 lookahead max | L₁ (m) | byte | 40 | 40 | 60 |
| 9, 10 ASCC A gain / threshold | aid block | × 0.01 | 15, 5 | 15, 5 | 0, 0 |
| 11, 12 TCS lateral a / b | aid block | × 0.1 / × 0.01 | 10, 0 | 10, 0 | 10, 0 |
| 13 TCS gain | aid block | × 0.1 (0 = off) | 60 | 60 | 60 |
| 14 grip weight | w (§4.2) | % / 100 | 100 | 100 | 100 |
| 15 corner bonus | km/h (§5) | byte | 20 | 20 | 20 |
| 16 steering rate | slew (§6) | byte × 10 °/s | 9 | 9 | 9 |
| 17 bonus delay | steps (§5) | byte × 10 | 0 | 0 | 0 |
| 18 pit speed | km/h (0 → 80) (§4.4) | byte | 80 | 80 | 80 |
| 19 ASCC channel flags | aid block | bits 0–7: A-front, A-rear, D-front, D-rear, C-front, C-rear, E-front, E-rear | 0 | 0 | 0 |
| 20 ASCC D gain | aid block | × 0.01 | 0 | 0 | 0 |
| 21, 22 ASCC C gain / threshold | aid block | × 0.01 | 0, 0 | 0, 0 | 0, 0 |
| 23 ASCC E gain | aid block | × 0.01 | 0 | 0 | 0 |
| 24 TCS slip scale | aid block | × 0.01 (0 and 100 → 1) | 0 | 0 | 0 |
| 25 flying start distance | time trials and free runs (§10) | byte × 10 m before the line | 0 | 0 | 0 |
| 26 flying start speed | time trials and free runs (§10) | byte × 5 km/h | 0 | 0 | 0 |

Bytes 27–31 are not read. So every built-in set uses K_y = 1.2, v_L = 180 km/h (50 m/s),
90°/s steering, a 20 km/h corner bonus with no delay and traction control with gain 6.0,
and no ASCC (no channel flag set); set 2 looks further ahead (30–60 m).

**Which set.** A car gets the course file's set for its *driveline class* (course file
slots 37–40, slot 37 + class). Only where that slot is empty does it get the built-in set
of the course category: category 1 (the wet Route 5 courses) set 1, category 2 (dirt)
set 2, anything else set 0. The driveline class: front-engine, mid- or rear-engine
rear-drive 0; front-drive 1; four-wheel drive by its system: system 0 → 1, system 3 → 3,
others → 2. The same class picks the event's overrides (§9.2). The flying start of time
trials and free runs reads the class 0 set's bytes 25 and 26.

Every car's driver gets its set at the start; a car the AI drives (control source 1 or 2)
then takes the event's overrides (§9.2), a player's car on its pad only at the end of its
run (§1.1).

79 of the 80 US course files carry a set for all four classes; L_circle60_wet1 has none and
uses the built-in set. Most sets equal built-in set 0. Every course set's pit speed is 78 or
80 km/h. Every course file that is not a license-test course (36 of them) sets bytes 25 and
26, always with byte 26 = 6 (30 km/h); the license-test courses set neither. Trial
Mountain's class 0 set, for example: brake 115 % (k_brake ≈ 0.87, later braking), lookahead
18–50 m, bonus delay 30 steps, pit speed 78 km/h, TCS gain 5.0 with slip scale 0.5, the
ASCC C-front channel on (gain 1.0, threshold 0.1), and a flying start 940 m before the line.

**Aid block.** A car the AI drives uses its driver's aid block instead of its own (§1): the
set's TCS gain, lateral terms and slip scale, and its ASCC channels, each acting only when
its flag bit is set and its gain is not 0 (physics §4.6). ABS is not part of the block.

### §9.2 Event difficulty

An event record (database table 32) overrides the set per driveline class c (0–3):

| Event bytes | Meaning | Default when | Effect |
|---|---|---|---|
| 0x1A4 + c | throttle % | 0 or above 100 → 100 | the throttle multiplier: every throttle except on the pit lines is multiplied by it (§4.3) |
| 0x1A8 + c | brake % | 0 → 100 | k_brake × 100 / %: below 100 the braking point stays but the driver lifts gradually before it (§4.3); above 100 it brakes later |
| 0x1AC + c | corner speed % | 0 → 100 | k_speed × % / 100 |
| 0x1B0 + c | corner speed offset, signed km/h | 0 = none | added to v_off |

In a race of configuration 2 (every car driven by the AI: the title attract demo) each
CPU car's overrides are varied by its index i in the race: the corner speed 4 points lower
for odd i, the brake 7 × (i mod 3) points lower, the yaw gain × 1.00, 1.03 or 0.97 for
i mod 3 = 0, 1, 2, and the corner offset 2 × (7 i mod 5) km/h lower.

For example, Sunday Cup race 1 gives throttle 85 %, corner speed 80 %, brake unchanged: its
CPU cars leave the line at 0.85 throttle.

### §9.3 Rubber band

The race can make cars run their whole step faster or slower depending on how far they are
from the reference car (§14). The factor is the car's *time scale*: its step length is the
time scale / 60 s (physics §1.1), so the car literally moves in faster or slower time.

| Event bytes | Meaning |
|---|---|
| 0x1B4 | global scale % (0 → 100) |
| 0x1B6 | the pass mode (§14) |
| 0x1B7 | slow-down amount A_s, % |
| 0x1B8 | slow-down start, m |
| 0x1B9 | slow-down full, × 10 m |
| 0x1BA | speed-up start, m |
| 0x1BB | speed-up full, × 10 m |
| 0x1BC | speed-up amount A_f, % |

The gap between two cars is counted in whole laps and meters (the lap counts' and the lap
progresses' differences, a lap taken over when they disagree in sign). For a car behind its
reference by g:

- speed-up = 1 + A_f when it is a lap or more behind or g ≥ full; 1 when g ≤ start;
  linear between; always 1 on the pit surface;

and for a car ahead by g:

- slow-down = 1 − A_s when it is a lap or more ahead or g ≥ full; 1 when g ≤ start; linear
  between.

A finished car takes its neighbor's factor in the chained pass, else 1 (§14). Under PAL's
rules (a PAL disc) the factor is capped at 1.3.

Who has a rubber band: single races (GT mode, arcade and the attract demo) with the event's
values; the split-screen battle with its catch-up option (off; low: speed-up 15 % from 20 m
to 1000 m behind; high: 20 % from 10 m to 200 m; the chained pass; under the Japanese
release's rules any catch-up setting gives high). Online battles have none (their catch-up
is never set; with one, the factor would be capped at 1 + A_f). Time trials, license tests,
free runs, free practice and machine tests have none.

Sunday Cup, for example: slow-down 20 % from 20 m to 80 m ahead, speed-up 10 % from 50 m to
100 m behind, so one link 50 m ahead runs at 0.9, 500 m ahead at 0.8, 75 m behind at 1.05.

---

## §10 Countdown and start

The event's start type gives the countdown: the event's own countdown seconds (type 0),
4 s (type 1), 10 s (type 2), none for the rolling start (type 3). The split-screen battle,
time trials and free runs have none, nor do the other solo modes (license and machine tests,
free practice) when their event has an initial speed; online battles count 4 s; a race
without an event counts 10 s. During it
every car is held by the race hold (physics §9.3): no horizontal motion, brakes off, the
engine free to rev.

**The hold's throttle.** A car the AI drives, during the race hold and during a pit stop's
hold, keeps its clutch request at 0 (it revs with the clutch open) and works the throttle by
the steps n left of the hold, with R the car's upshift rpm and r its race position (on the
grid, its place in the starting order):

- while n ≥ 13: a switch turns on below ¾ R and off at or above 15/16 R; while on, the pedal
  rises by (5 − 0.2 r) per second up to 1, while off it falls by (10 − 0.4 r) per second
  down to 0 (a blip around R);
- in the last 12 steps: pedal = clamp(0.625 − 0.002 r − 2 (rpm − R) / rev limit, 0, 1), a
  proportional hold at about R.

At GO the hold ends; the AI's clutch rule (§1) engages the clutch from the revs, and the
first decision's throttle applies at once (slewed in an arc, §1). For the first 2 s the
lookahead reaches 40 m further (§3), and traffic decisions wait 5 s + 0.1 s × the race
position (§8.2).

**Moving starts.** A single race whose event has an initial speed starts every car on its
grid slot at that speed. The rolling start (GT mode's endurance events) starts every car at
80 km/h with no countdown; the AI drives the player's car for the first 5 s (§1.2). A
split-screen battle with a handicap starts both cars on the main line before the start
line, at the event's speed: the handicapped car |h| m (at least 20 m) before it, the other
10 m before it.

**Flying starts** (time trials, free runs, license tests with a flying start): the player's
car starts on the main line before the start line, moving, and the AI drives it to the line
(§1.2; physics §9.4). Where:

1. the event's flying start: its distance before the line, at its initial speed;
2. else, in time trials and free runs, the course's class 0 driver set when its bytes 25
   and 26 are both set (§9.1): 10 m × byte 25 before the line at 5 km/h × byte 26. Every
   US course that is not a license-test course has them (30 km/h);
3. else, in time trials and free runs: 5 m before the start of the main line's last tight
   corner (a run of arcs with |radius| ≤ 350 m; 200 m on dirt) that begins at least 400 m
   (300 m on dirt) before the line, or 405 m (305 m) before the line when there is none, at
   the speed the AI plans for the segment ahead.

A license test with a start distance but no initial speed starts at rest on the main line
at that distance. **Free practice** starts the player's car in its pit box on the pit stop
line, held for 120 steps (2 s); then the AI drives it out of the pits (§1.2, §13). It needs
a pit box for the car on the course's stop line (at least four points): on a course without
one, free practice is refused with the message "Free run is not available on this course."

---

## §11 The race field and the race step

**Who races.** The configured opponents, or else the event's entries, cut to the event's
car count less one (at most 5) and to the course grid's slots less the players' cars. A car
whose car data cannot be built is left out. Time trials, license tests, free runs, free
practice and machine tests have no opponents (a ghost car aside), online battles none
besides the other players' cars.

**Grid.** The players take the slot after the opponents (or a configured slot), player 2
the next; the opponents take the other slots in their order or, in GT mode, by a
whole-number weight-to-power figure: the car's kg over the whole power the menus show, in
the language's unit, truncated; the lowest on pole, equal figures in the order drawn (gt_mode
§22.1). Each car is placed with its nose at its slot point (moved back along
its heading by its front extent from the center of gravity), every car facing the grid's
direction (physics §9.2).

**Per car at the start.** Its control source (§1); its driver's parameter set and, for a
car the AI drives, the event's overrides (§9); its placement, at rest or moving (§10);
fresh tires; its pit box (§13); its race position; its line (the initial line, §2.1) and
plan (§4.1).

**The race step.** Each race frame:

1. the start countdown (§10);
2. phases 1–9 of every car, phase by phase, in car order (physics §1.2); phase 3 is the
   driver: the pad, or the AI step (§1);
3. the collision step (physics §5.3);
4. phase 11: the zone cells and contact intensities (§8.1) and the slipstream (physics §5.2);
5. phases 12 and 13 (the tires-worn flag and pit request, §13);
6. the rolling start's and the free run's timed hand-backs (§1.2), then for every car in
   race order: its lap progress, its line crossings (laps, sectors, the finish and the end
   of its run, §1.1; the first crossing's hand-back, §1.2), the wrong-way flag, the pit
   lane (§13);
7. the race clock, the race order, the rubber band pass (§14).

So every driver decides before any car moves, and sees the other cars only through the last
frame's zone cells, contact intensity and hit flags.

**Finish and race order.** In a single race a car's race ends at its line crossing once the
leader has finished, its laps are done, or the event's time limit has passed; it then gets
the run-on (§1.1). The race order: more laps ahead; at equal laps a car still racing ahead
of a finished one, two finished cars by their crossing times, others by lap progress. Free
practice orders the cars by their best laps.

Online, the other players' cars are posed from their owners' machines and have no driver
here (`docs/dev/ONLINE.md`). Ghost cars (free runs, time-trial ghosts) are driven by their
recorded pad frames (a free run's ghost by the AI past its line, §1.2); no driver sees them.

---

## §12 No respawn

No car is ever respawned or reset onto the road: a car that leaves the road, spins or gets
stuck finds its way back with the recovery machine (§7) or stays where it is.

---

## §13 The pit lane and pit stops

The pit lane works in the race modes with pit stops (single races, battles, free practice,
machine tests; in the others no car is ever on the pit surface or gets a pit request) on
courses that have both the pit lane line (3) and the pit stop line (4). Its marks are the
stop line's points by their lap-shifted distances (§2.2; n points):

| Mark | Stop-line point |
|---|---|
| entry of a car with a pit request | 0 |
| entry of a player's car, on the pit surface | 1 |
| the box of car i (car 0 the player) | n − 4 − i |
| exit of a CPU car or a finished car | n − 1 |
| exit of a player's car | n − 2 |

**Arming.** With tire wear on, a car's tires are *worn* when some wheel's wear grip falls
below the race's worn threshold (physics §8.7). A CPU car with worn tires gets a *pit
request*. A player's car never does, on its pad or under the autopilot (configuration 3
leaves a car driven with the pad decoded out): the player drives in.

**States** (the driver kinds, §1). The lane runs after each car's step, so a switch takes
effect from the next step.

1. **Racing** (kinds 0, 1, 5–8). A car with a pit request enters (kind 2, the pit lane line
   at once) when it next passes stop-line point 0, unless its race is over (it is on its
   last lap, or the time limit has passed). A car on its pad (not in kind 1) enters when it
   passes point 1 on the pit surface. A car the AI drives that hits a wall on the pit
   surface in the second half of the lap enters too, turning round (recovery state 2).
2. **Lane** (kind 2). Passing point 1 shows the pit-in pop-up for the car. A car that is past
   point 1 and off the pit surface goes back to racing. 25 m before its box the car decides:
   it drives through (kind 4) if its race is over, if its tires are still fresh (nothing to
   service; in free practice every stop services the car), or if it is at 100 km/h or
   faster; otherwise it takes the stop line (kind 3, at once).
3. **Stopping** (kind 3). The driver runs the stop line at 20 km/h with a 5 m lookahead
   (§3, §4.4). Crossing its box with something to service, the car stops dead (its motion
   cleared, 1st gear, clutch open) and is held for 300 steps (5 s; physics §8.6); 60 steps
   before the end of the hold it gets a fresh set of tires. During the hold a CPU car revs
   as at the start (§10); a player's car is on its pad, with the clutch held open. Once
   0.5 m past the box it leaves (kind 4, the pit lane line at once) and its worn flag is
   cleared.
4. **Leaving** (kind 4). A car past point 1 and off the pit surface goes back to racing. At
   its exit the car goes back to racing (kind 0: a CPU car to the main line, a player's car
   to line 7, both blended; recovery state 0), its pit request is cleared and, unless its
   race is over, the out-of-the-pit-lane note is posted; a car that finished meanwhile gets
   the run-on instead (§1.1).

On the pit lane line the driver holds the parameter set's pit speed + 2 km/h through the
limit section (§4.4: 78 or 80 km/h + 2 with the disc's course sets), with a 20 m lookahead
while leaving in it (§3); on the pit surface it gets no corner bonus (§5) and plans without
the wear factors (§4.2, §4.3); on the pit lane line it does not change lanes (§8.2). Cars
on the pit surface and cars off it do not see each other (§14).

**The wrong way.** A player's car on its pad going the wrong way on the pit surface between
stop-line points 3 and n − 2 is taken over when it passes point n − 3 backwards: the AI
drives it out of the pits (kind 4).

The rubber band keeps acting in the pit lane (§9.3); on the pit surface a car gets no
speed-up of its own.

---

## §14 Car-to-car interaction and the rubber band chain

**Pairs.** Every pair of cars is compared after the collision step. Two cars see each other
when both are in the race (not ghosts), both are on the pit surface or both are off it,
their centers are within 100 m, and the other car is no more than its body height + 5 m
below or this car's body height + 5 m above. Of such a pair, the zone cells and the contact
intensity (§8.1) use only cars within 20 m; the relative-position codes and the slipstream
are the physics' (physics §5.1, §5.2).

**Hits.** A car-car hit gives both cars hit flags for the next step: the car whose corner
struck gets that corner's end, the other the end of the edge that was struck. These are
the front and rear hit flags of §7, the same as a wall's.

**No following.** The driver has no following brake and no speed matching: a car caught
behind a slower one lifts (throttle × (1 − c)), lengthens its braking distance by 20 c m,
looks for a passing line, and otherwise relies on contact.

**The rubber band pass.** After every step the race sets the time scales of §9.3 from the
*reference car* R, the first car of the race order that is not driven by the AI alone
(control source not 2; car 0 when there is none, as in the attract demo). The mode (§9.3):

- **0, the chain**: from R outwards along the race order, each car's factor is taken
  against its neighbor towards R (ahead of R: the slow-down against the car just behind;
  behind R: the speed-up against the car just ahead) and multiplied by that neighbor's;
- **1**: every car against R directly;
- **2**: as 0, each car against its neighbor towards R, without the multiplication.

R's own scale is not changed. Every other car's time scale is its factor times the global
scale. In the chain the factors compound outwards: a car two places behind a slowed car is
slowed through its neighbors, not by its own distance to R alone. The pass runs after every
step, the countdown included.

### §14.1 Order of the traffic decision

Per AI step, after the speed planning (§4, §5) and the steering (§6):

1. Count the cooldown down by one step.
2. On the pit stop line: stop here.
3. On the pit lane line: halve the throttle when boxed in on one side (left and front-left,
   or right and front-right); lane changes are not allowed.
4. At status 1, 2 or 5 lane changes are not allowed.
5. During the cooldown: stop here.
6. With contact (c > 0) and lane changes allowed: choose a passing line as in §8.2 (the side
   map decides which line is left); if one is taken, start the 0.5 s cooldown and stop here.
7. Throttle × (1 − c). Without lane changes: stop here.
8. Nothing front, front-left, front-right, left or right: ask for the main line (blended).
9. Exactly one of left / right occupied: move to the line on the other side (not right at
   status 4, not left at status 3).

---

## §15 (removed)

CPU cars race only in the race itself (§11); the title attract demo is an all-CPU race (§1.2).

---

## §16 Recommended-gear hint

The HUD's recommended gear (presentation §D2) is the one the player's own driver keeps.
While the pad drives the car, the driver's point bookkeeping and braking check (§4.1–§4.3)
still run every step on its line (line 7, else the main line) with its parameter set (§1.2),
so it always knows the speed it would aim at; while the AI drives the car its own plan
gives it. Under the autopilot the hint is not updated (none from a standing start). Live
runs only, not replays.

The target speed T is the next segment's target (the top speed for a straight, else its
corner speed), or in braking mode 4 the lower of the current and the next target. When the
car is slower than T there is no hint. Otherwise the hint is the lowest gear in which T is
below the speed at 500 rpm under the upshift rpm (the top gear when none is), blinking
while the driver is in a braking mode (3 or 4).

---

## Verification

The public tests that drive the AI (the disc tests need the player's disc, `GT3R_DISC`):

| Test | Checks |
|---|---|
| `exact_game_race_lockstep` | a GT-mode race (Sunday Cup, 6 cars), a Tahiti Maze rally and a split-screen battle through the race screen's field: every car record, the race order and clock equal after every step to the same race built directly; the CPU field races on to its finishes |
| `exact_game_rules` | whose rules a race follows (the disc's, the US release's online and for demos); every race steps 60 times a second |
| `race_field_event` | an arcade event on Trial Mountain: five opponents from the event, the player's car held through the 10 s countdown and moving after GO, six standings |
| `exact_game_solo_modes` | the time trial's AI run-up and its hand-back, one timed lap ending the run; a license test judged; the 0–400 m machine test; a free run whose ghost car is put back on its recording at each crossing; free practice running laps |
| `exact_game_free_run_visit` | a free run's ghost kept from run to run of a visit, and a loaded ghost |
| `exact_replay_round_trip` | races recorded through the race screen (on the pad, from inputs, a split-screen battle, a race demonstration with the AI's take-over, the autopilot) replay to the same race, every car record equal |
| `exact_replay_ghost` | a time trial's whole-run ghost drives the earlier run again from its pad frames |
| `exact_demo_records` | the disc's demonstrations built as race records, through a replay file, play on the race; some reach a judged or lap time, and at least one the time its save records (on the US disc 7 of 58) |
| `net_exact_duel_ai`, `net_exact_duel_autopilot` | online, the race's AI drives both players' cars to the finish, with the autopilot's configuration 3 |
| `race_flow_ranking_and_gaps` | the race order (laps, finished cars, progress; free practice by best lap) and the gaps between cars |
| `race_replay_driver_kind` | the player's driver kind per step (the run-up 1, the stop driver 5) survives a replay file |
| `program_tables_values` | the built-in tables (driver parameter sets, line fallback, initial lines) pinned by their hash |

## Open points

- **Short stop lines.** A car whose pit box falls before the stop line's first point (more
  cars than the line has boxes) reads its box at distance 0; it then never reaches its
  stop decision and should drive through the lane, but no test runs such a race.
- **A wheel's service flag.** The stop also services a car for a per-wheel flag nothing in
  the race sets.
