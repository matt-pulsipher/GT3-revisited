# Vehicle simulation

This page describes how GT3 Revisited drives a car: GT3's own car step and race step,
run in the PS2's single-precision arithmetic so that a race is step for step the game's.
Every race runs on it: single races, battles, online races, the solo modes (time trial,
license test, machine test, free run, GT Mode's free practice), replays, ghosts and
demonstrations. It covers the car's state, the fixed step and the order of its phases, the engine and
driveline, the tires, the suspension and body motion, the driver aids, and the contact
between cars and with walls (§0–§5), then the car's parameters from the database and the
player's settings, tire wear, pit stops, the visual flap pendulums, race starts,
ground-frame details and leaving the road (§6–§11). Code comments cite its sections as
`physics §n`. The CPU drivers are described in [ai.md](ai.md).

| Code | Role |
|---|---|
| `src/sim/exact/tick.h`, `tick_*.cpp` | one car step, phase by phase: preparation (`tick_prep`), driver input (`tick_input`, the AI in `tick_ai`, `tick_ai_plan`), steer angles (`tick_steer`), slip ratios (`tick_slip`), aids (`tick_aids`), engine, gearbox and driveline (`tick_engine`, `tick_gear`, `tick_drive`), tire forces and planar motion (`tick_tyre`, `tick_planar`), suspension and attitude (`tick_suspension`, `tick_attitude`), ground contact (`tick_ground`), effects and speeds (`tick_effects`, `tick_speed`), proximity (`tick_proximity`), wear, brake heat and flaps (`tick_wear`), the pit lane (`tick_pit`); the phases for every car (`tick_phases`) |
| `src/sim/exact/car_record.h`, `car_fields.*` | the car's state and constant parameters, as GT3 keeps them |
| `src/sim/exact/race.*`, `race_flow.*`, `tick_race.cpp`, `pit_lane.h`, `race_record.*` | a whole race: the step for all cars, laps, finish, ranking, rubber band, pit lane; a race kept for replays |
| `src/sim/exact/start.*` | placing the cars at the start (§9) |
| `src/sim/exact/params_build.*`, `spec_build.cpp` | a car's parameters from the database and the player's settings (§6) |
| `src/sim/exact/race_inputs.*`, `program_data.*`, `program_tables.*`, `program_locate.*` | the race's constants: the game's small built-in tables (GT3's values, checked against the player's disc), the event's rules, the race mode |
| `src/sim/exact/course_ground.*`, `course_query.*` | the ground under the wheels; race progress, grid and sectors |
| `src/sim/exact/pad_read.*` | the pad read: a controller into the frame the driver input reads (§4.4) |
| `src/sim/exact/ai_course.*`, `ai_setup.*` | the AI's lines and driver parameters ([ai.md](ai.md)) |
| `src/sim/collision_exact.*`, `src/sim/course_collision.*` | the collision step, and the course's walls and road segments as it sees them (§5) |
| `src/sim/program.h` | which release's rules a race follows, and its step rate (§1.1) |
| `src/race/exact_setup.*` | the disc's built-in tables, which physics a disc's races run on (§1.1), each car's equipment (§6), whether a course has a pit box (§9.4) |
| `src/base/ee_float.*`, `ee_math.*`, `ee_double.*` | the PS2's arithmetic and the game's own math routines (§0) |
| `src/sim/car_spec.*` | a car's figures for the menus (power, torque, weight, gearing, showroom top speed, drag, ride height) and the garage settings |
| `src/sim/engine_condition.*` | the engine condition's rules outside a race (§10.3) |
| `src/sim/sim_types.h` | what the race screen, renderer, sound and HUD read of a car |
| `src/race/exact_car.*` | the car's state as the renderer, sound, HUD and rumble read it |
| `src/race/exact_field.cpp` | runs a race for the race screen: the players' pads, the cars, the race's outputs |

## Overview

**Frames.** The race screen, renderer and course data use the world frame: right-handed,
+Y up, meters. A car's drawn frame has +X to the right, +Y up and the nose towards −Z,
with its origin at the wheelbase midpoint on the ground. The car step itself works in a
Z-up *physics frame* and tracks the center of gravity (CoG) and three Euler angles (§1.3).

**Units.** SI throughout (m, s, kg, N, N·m, rad, rad/s), except engine speeds in rpm where
named. Gravity is 9.8 m/s² (§0).

**Time.** One step of 1/60 s per frame, explicit Euler, no sub-steps (§1.1). A race step
runs each phase for every car before the next phase; the collision step moves all cars
together (§1.2, §5.3).

**State.** A car is a planar rigid body (horizontal velocity, heading, yaw rate) with a
separate heave, pitch and roll driven by four independent suspension corners, plus the
engine, clutch, gearbox, driveline and per-wheel spin, slip and suspension states (§1.3).
Every car, the player's and the CPU cars alike, runs the same step; only its driver
input differs (§4.4).

---

## §0 Constants and conventions

| Name | Value | Use |
|---|---|---|
| g | 9.8 m/s² | weight, static loads, gravity in the heave step |
| kgf·m → N·m | × 9.8 | torque and force bytes in the database |
| rpm → rad/s | × 2π/60 | engine speeds |
| km/h → m/s | ÷ 3.6 | every speed threshold given in km/h |
| step | 1/60 s | §1.1 |

**Arithmetic.** Every value of the step is a PS2 single-precision float, computed in the
game's own order of operations: additions, subtractions and multiplications round
towards zero, an addition drops the smaller operand's bits beyond one guard bit,
divisions and square roots round to nearest, there are no denormals (they read as 0) and
no infinities or NaNs (an overflow saturates at the largest value). Where the game computes
in double precision (a few steps of the car's parameter build, the wear grip and the AI's
speed planning), it uses
its own software doubles, and so does GT3 Revisited. Sines, cosines and arctangents are
the game's own routines, quirks included: atan2(y, x) is 0 whenever x + y is exactly 0.
All of it is integer code, so the same inputs give the same bits on every platform.

**Sign conventions.**

- **Yaw rate** and **heading**: positive = turning left (counterclockwise seen from above).
- **Pitch**: positive = nose up. **Roll**: positive = right side down.
- **Lateral** axis and speeds: positive = to the right.
- **Steering**: the pad frame, the steering command and the wheels' steer angles are
  positive to the left (the yaw sense). The race screen's steering values are positive to
  the right.
- **Velocity-direction angle** φ of a hub: positive = the hub moves to the left (§3.2).
- **Slip ratio**: > 0 braking (wheel slower than the road), < 0 wheelspin (§3.4).
- **Suspension coordinate x**: 0 where the spring force is zero, negative = compressed (§4.2).
- Wheel order: FL, FR, RL, RR (index 0–3); axle 0 = front, 1 = rear.

## §1 State and time step

### §1.1 Time step

The race steps every car once per frame, 60 times a second (the game's frame rate), with
explicit Euler integration, "rate then value": each velocity is updated from its
acceleration first, and positions and angles then move with the new velocity. There are no
sub-steps.

Each car's step length is **dt = time scale / 60**, and 60 / time scale is its inverse
(the damper speeds and the stop rule of the forward step use it). The time scale is 1 except
where the race's rubber band sets it (ai §9.3): the band runs after the race flow at the
end of a step and sets every car's scale for the next one. When the race's configuration
lets the AI drive the players' cars (attract mode, the test autopilot), a step runs only
when some car's time accumulator, which loses the race's own time scale (1) every frame,
drops below 0; at scale 1 that is every frame.

The step is deterministic: it reads only the car records, the race's constants and the
frame's pad inputs. One random sequence, the game's own, feeds the flap pendulums (§8.8)
and the tire smoke particles; it never feeds the driving state.

**Releases.** Every race runs at 60 steps a second, on every disc. The rules (the few
behaviors that differ between the releases) follow the disc being played: NTSC-U, PAL or
NTSC-J. Online races, the attract demo and the demonstrations follow NTSC-U's rules on
every disc (every disc carries the NTSC-U recordings). A disc build that has another
release's version of one of these behaviors follows that version for it. Those that touch
§0–§5: NTSC-J wraps an angle into [−π, π) a turn at a time (the others take whole turns
at once beyond 3π), NTSC-J's collision step neither wraps the heading nor clamps the yaw
rate when it commits (§5.3) and its wall friction and push-out lack two zero-length tests,
and PAL's oversteer indicator for AI-driven cars is 0 below 25 km/h (§4.6). The step counts
on this page are at 60 steps a second.

A disc of another game on the same engine, or a GT3 build that GT3 Revisited does not fully
recognize, races on GT3's physics with GT3's built-in tables and NTSC-U's rules for each
behavior it does not recognize. Settings > Game disc shows the race physics, with a note
when GT3's stands in for the disc's own.

In a free run, when the ghost car restarts its lap a fraction of a step behind, the race
runs one extra step for the ghost car alone during the race flow (§1.2).

### §1.2 Step order

One race step runs, after the start countdown and the gate of §1.1, these phases. Each
phase runs for every car (in car order) before the next one starts; phase 10 is one
collision step for the whole field.

| Phase | Step | Section |
|---|---|---|
| 1 | Keep the last step's attitude, suspension, steering, wheel spin and ground points (for drawing between steps) | – |
| 2 | Race hold from the start countdown; the spawn/stop counter counts down; step length; slipstream filter, drag and downforce; full-throttle and friction torque at the stored engine speed; each wheel's wear grip and peak force from the last step's load; each hub's velocity-direction angle and low-speed fade from the last step's hub velocity | §1.1, §5.2, §2.1–§2.2, §3.1–§3.2, §3.7 |
| 3 | Driver input: the pad (steering follower, pedals, shift and reverse requests, clutch request) or the AI | §4.4, §2.4, §2.6, ai |
| 4 | Each wheel's steer angle (Ackermann ratios, toe) | §4.4 |
| 5 | Each wheel's slip ratio and slip gain | §3.4 |
| 6 | Under/oversteer indicators; TCS; the dirt brake cut; ASCC; ABS | §4.5, §4.6, §10.4 |
| 7 | Clutch before the step and brake torques; rolling resistance; tire forces; forward step; wheel torques and torque vectoring; clutch torque, 4WD coupling and axle speeds; differentials and wheel speeds; engine speed and the clutch after the step; gear decision; lateral force and yaw; holds and the velocity update | §2.2–§2.7, §3.3–§3.5, §3.8, §4.1 |
| 8 | Brake heat | §8.5 |
| 9 | Clears the pad's hit feedback | §5.3 |
| 10 | The collision step: position, height and heading move with the velocities, against the walls and the other cars | §5.3 |
| 11 | Proximity: relative positions, the AI's zones and contact, slipstream targets | §5.1, §5.2, ai §14 |
| 12 | Ground query at the moved car and the course-out check (§11); terrain factor and suspension coordinates; springs, dampers, anti-roll bars and load transfer; attitude and heave (or the spawn hold); bump-stop nudge; pad vibration and wheel force; slip intensity, skid pitch, smoke and dirt; engine sounds; ground frame; speeds on the ground frame and hub velocities; odometer and engine condition; free wheels; wheel spin angles | §1.3, §3.6, §4.2–§4.3, §8.3, §10 |
| 13 | Tire wear, the worn flag and pit request, flap pendulums | §8 |

After phase 13 the race runs its own flow for each car in race order (laps, sectors, the
finish, the pit lane, the end of a run), then the ranking and the gaps between cars, then
the rubber band (§1.1).

In an online race the other players' cars are posed from their owners' states: they skip
phases 1–9, 12 and 13, and take part in the collision step (§5.3) and proximity.

Consequences of this order:

- **The tires lag the loads by one step.** Phase 12 runs at the end of the step, so a
  step's peak forces (phase 2) use the loads the previous step left, and its slip angles and
  slip ratios use the hub velocities the previous step left.
- **A shift takes effect for the next step.** The gear decision comes at the end of
  phase 7, after the step has transmitted through the old gear.
- **The collision step comes after the forces.** A wall or car hit changes the velocities
  in phase 10, and the tires, engine and aids see it on the next step.

**Pad vibration.** Phase 12 sets each car's vibration level: 256 × the largest of these, at
most 127:

- the forces: |(the mean squared front slip ratio + 0.1) × the front tires' summed drive
  force| × 0.00065, at most 0.333, plus (the front tires' summed brake force / 19600)², at
  most 0.333;
- the road: each wheel's damper force / 19600 (compression only, each at most 1) plus, for a
  front wheel on a curb, |its load / 2940 × the speed / 64| (each at most 0.25); neither
  counts while the race holds the car;
- a hit: 8 × its strength for the rumble (§5.3) + 0.25, while the rumble holds it.

After every step the race sends the level of a car the pad drives to its controller's large
motor as it is (so at most about half strength); the small motor never runs. A car the AI
drives (a run-up, after the finish) sends nothing, and its controller's motors stop. This is
the Rumble setting's GT3 original (Auto plays it on a DualSense); its Modern rumble and the
triggers' ABS and wheelspin effects are the engine's own (`docs/INPUT.md`).

### §1.3 State, frames and the ground query

**Physics frame.** The step is written in a Z-up frame: x right, y forward (at heading 0),
z up. It maps to the world frame as world = (x, z, −y). The body axes come from three
Euler angles, R = R_z(heading) · R_y(roll) · R_x(pitch); the columns of R are the body's
lateral (right), forward and up axes. At zero angles forward is (0, 1, 0), world −Z, the
car's nose; a heading c puts the nose along (−sin c, −cos c) in world x/z, so a positive
heading turns the nose left. Phase 12 takes a whole turn off the heading once it reaches
±2π.

**Car state:**

| Group | State |
|---|---|
| Body | CoG position, velocity (x/y horizontal, z vertical), heading, pitch, roll, yaw rate, pitch rate, roll rate; body axes |
| Per wheel | suspension coordinate x′ (the clamped value), damper speed, spin angle and rim (peripheral) speed, steer angle, slip ratio, slip gain, velocity-direction angle, low-speed fade, load, peak force, force outputs, brake pressure, surface, ground height under the hub, road bump, flags |
| Per wheel, ground | the hub's query point, the road segment, height, plane and triangle bytes the ground query found |
| Engine | speed (rad/s) and integer rpm, fuel-cut flag, turbo lag states and load (§2.3), odometer and condition counters (§10.3) |
| Transmission | gear (reverse, 1…N), clutch state 0–3 (§2.4), shift lockout counter, the automatic's counters |
| Driveline | axle speeds, the differentials' lock flags, the center clutch's engaged flag (§2.7) |
| Controls | throttle and brake pedals, handbrake, steering command and its follower rate, steering bounds, reverse and shift requests, clutch request |
| Kept for the next step | forward and lateral speed on the ground frame, longitudinal and lateral acceleration, lateral g/μ, yaw acceleration, the longitudinal force sum, the ground frame and the ground's pitch and roll |
| Other | slipstream target and filter (§5.2), the race and spawn/stop holds (§4.1, §4.3), driver kind and control source (ai §1), wear, smoke, skid pitch, brake heat and flaps (§8) |

The CoG sits a = L(1 − w_f) behind the front axle and b = L·w_f ahead of the rear axle,
where L is the wheelbase and w_f the front weight fraction, so it is L(w_f − 0.5) ahead
of the wheelbase midpoint. At rest on flat ground it is q above the ground, q being the
ride height (§4.2). The race screen draws the car from the wheelbase midpoint on the
ground: the CoG moved q down its up axis and (a − b)/2 along its forward axis.

**Ground query** (phase 12). Each wheel's *query point* is the CoG plus the hub's offset:
± half the axle's track along the body's lateral axis and a (front) or −b (rear) along its
forward axis, with the body axes as the last step left them. So pitch and roll move the
query point up and down with the body, but only by the hub's lever, not by its height above
the CoG. Each point is looked up in the course's road grid (`docs/formats/course.md`): among
the road triangles listed in the grid cell under the point (none when the point is off the
grid), the highest one that strictly contains the point in plan, taking only the road
segment that the front-left wheel found on the last step and that segment's neighbors
(every segment when it found none). There is no height test: the segment filter is what keeps a car on a
bridge from finding the road below. The query gives the height, plane and triangle bytes
(material and flags, §3.6); the front-left wheel's segment becomes the car's road segment
hint for the next query and for the collision step (§5.1).

A **miss** (no triangle under the point) keeps the wheel's previous ground height and
surface, with no road bump. The wheel stands on that height, so a car driven off the road
mesh keeps its wheels on the ground at the old height.

The four ground heights also define the *ground frame* (§4.3), whose forward and lateral
tangents are what the car's speeds are measured on (§10.1). The forward speed u the next
step's aero, steering curve, clutch, aids and indicators read is the one phase 12 left; the
forward step of phase 7 then updates it.

---

## §2 Engine, transmission and driveline

The engine makes a crank torque from a full-throttle curve, the throttle, engine braking
and a fuel cut (§2.1–§2.2); turbo engines lag their boost (§2.3). A four-state clutch links
the engine to the gearbox (§2.4). The gearbox ratio and the drive layout (§2.5) feed an axle
driveline with differentials (§2.7). Shift decisions are §2.6.

### §2.1 Torque curve

Curves are piecewise-linear tables looked up with clamping: below the first point the first
value, above the last the last value. Every curve of the step uses this lookup.

The full-throttle torque T_wot(ω) is a lookup on the car's torque curve, built from the
engine record (format: `docs/formats/database.md`; the part multipliers are §6.2):

- up to 16 points; the rpm axis in 100 rpm (converted to rad/s), the torque in
  0.01 kgf·m; a last torque point of 0 takes the one before it;
- T_i = t_i × 0.01 × 9.8 × F × s(ω_i), with F the output factor / 1000 (engine +0x42 × 10,
  so 1000 = ×1) and s a linear tilt from the low-rpm factor (percent) at the first axis
  point to the high-rpm factor (per mille) at the last;
- with a turbo, each point is multiplied by min(B, cap) / B, where B is the uncapped boost
  at that rpm with both stages at the axis rpm (§2.3), so the curve holds the capped
  full-boost torque.

Idle = engine +0x44 × 10 rpm. Rev limit = engine +0x45 × 100 rpm plus the part adders (the
last axis point if the byte is 0), but at least the up-shift rpm + 200 (§2.6).

T_wot is evaluated once per step, in phase 2, at the stored engine speed, and multiplied
by the engine condition factor (§10.3).

### §2.2 Engine torque, engine braking and fuel cut

The net crank torque at the stored engine speed is

  T = x′ · T_wot − (1 − x′) · F(ω),  F(ω) = k_f · ω

- x′ is the effective throttle after traction control, the shift cut or blip (§2.4) and
  the turbo lag (§2.3), 0…1.
- **Engine braking** F is linear in engine speed: k_f = the drivetrain's friction word
  (§6.2) × 0.98 / ω_last, where ω_last is the last torque-curve rpm in rad/s, so engine
  braking at the top of the curve is the word × 0.98 N·m.
- **Fuel cut**: sets when rpm ≥ rev limit and clears when rpm < rev limit − 500. While cut,
  T = −F.
- The engine condition factor (§10.3) scales T_wot, not F.

The rpm the fuel cut reads is the integer rpm of the stored engine speed, at least
idle − 1, from the end of the last step's engine phase. With a locked clutch the stored
speed is the wheel side's from the end of the last step (§2.4).

Engine inertia J_e = the drivetrain's inertia word × 0.001 kg·m² (§6.2).

The game also has a power cap for power-restricted races (a race-rule byte × 10 PS, with
the torque softly limited over the top 30 % of the cap); no race mode sets the byte, so it
never acts. There is no electric motor (GT3 has none); electric mod cars race on their
donor car's engine (§6).

### §2.3 Turbo

Turbo engines have one or two stages. From the turbo part and the player's turbo settings
(§6.6):

| Parameter | Value |
|---|---|
| cap | 1 + 0.1 × turbo +0x11 |
| stage peak A | setting byte × 0.1 (stock: turbo +0x12 / +0x15) |
| stage knee N | setting byte × 100 rpm (+0x13 / +0x16) |
| stage rate R | setting byte × 0.1 per second (+0x14 / +0x17) |

The turbo is on when the cap and stage 0's three values are all non-zero; stage 1 is
active when its three values are non-zero.

**Boost shape** of a stage at rpm n:

- n < N: A (1 − ((N − n)/N)²)
- N ≤ n < 3N: A − ¼ A ((n − N)/N)²
- n ≥ 3N: 0

(The game has two other shapes below the knee, a rising parabola and a straight line; the
car builder never selects them.) Uncapped boost B(n₀, n₁) = 1 + shape₀(n₀) + shape₁(n₁),
capped B_cap = min(B, cap).

**Lag step**, once per step with n the integer engine rpm:

1. target = (last step's load) · n / B_cap(n, n);
2. each active stage: lag += trunc((target − lag) · R · dt), an integer rpm, set to 0 if it
   goes negative;
3. load = x · B_cap(lag₀, lag₁), where x is the throttle after TCS and the shift cut or
   blip;
4. x′ = min(1, load / B_cap(n, n)).

So x′ is the throttle scaled by how much of the full boost the lagged stages give, and
boost decays during a shift cut. The clutch's engage rule reads x, not x′. Without a turbo
x′ = x. The same step drives the turbo's spool and blow-off sounds ([sound.md](sound.md)).

The HUD's boost value is B_cap(lag₀, lag₁) − 1 (at most cap − 1).

### §2.4 Clutch

The clutch link has four states:

| State | Meaning | Torque to the gearbox |
|---|---|---|
| 0 open | engine free | none |
| 1 locked | engine turns with the wheels | the engine torque T |
| 2 slipping | launch, or a change to or from reverse | T_c from the slip rule below |
| 3 shift | forward-to-forward gear change | ± the shift-clutch torque (0 for every car) |

The torque into the axles is T_g = T_clutch × R_g, with R_g the gear's total ratio (§2.5).
There is no driveline efficiency.

**Clutch request**, set by the driver input every step (the pad clutch is automatic):

- 0 during the race hold (the start countdown), while the spawn/stop counter runs (§4.3)
  and for a parked car (driver kind 8, ai §1.1);
- 0 with the handbrake above 0.8 on a car whose driven axle is the rear (every layout but
  FF and 4WD system 0, §2.5);
- with the clutch open: 1 while the brake pedal < 0.25;
- with the clutch engaged (state ≠ 0): 1 unless rpm < idle.

When the AI drives (CPU cars, a player's car in its run-up, pit lane or end of a run):
0 during the race hold and the counter (the AI holds the revs instead, ai §10) and for a
parked car; with the clutch open, 1 when rpm > ¾ of the up-shift rpm; engaged, 1 unless
rpm < idle.

**Before the step:**

- A locked clutch opens when the request is 0. Nothing else leaves state 1 before the step,
  and states 2 and 3 are not opened by the request.
- From open, with the request on: when the car moves faster than 10 km/h in the gear's
  direction, → 2 once the throttle (after TCS) is above 0.25; when it does not, → 2 once the
  rpm is above the launch rpm (engine +0x43 × 10, at least idle + 500), and otherwise the
  request is cleared.
- In state 3 the throttle is cut to 0 while the wheel side turns slower than the engine and
  blipped to 1 while it turns faster.

The game also has clutch-pedal setups (pedal styles 3 and 6); GT3 Revisited's pads use
styles 0 and 1 (§2.6).

**Gear change with the clutch engaged** (§2.6): a change to or from reverse → 2; a forward
→ forward change → 3. A locked clutch never just opens on a shift.

**Engine speed and transmitted torque** in the step (ω_in = the wheel side's speed: the
driven axle speed × R_g, at least 0; §2.7):

- **Open (0)**: ω_e ← max(ω_e + dt · T / J_e, idle). No rev cap.
- **Locked (1)**: T_g = T · R_g; at the end of the step ω_e = ω_in.
- **Slipping (2)**: Δω = ω_e − ω_in (both from the start of the step). The clutch torque is

    |T_c| = 0.05 · T_wot + (T > 0 ? s(|Δω|) · T : 0),  s(d) = min(1 + max(d − 52.36, 0) / 837.76, 1.125)

  signed by Δω (52.36 rad/s = 500 rpm, 837.76 rad/s = 8000 rpm), and
  ω_e ← ω_e + dt (T − T_c) / J_e. T_g = T_c · R_g.
- **Shift (3)**: T_c = ± the shift-clutch torque (0), and the engine speed integrates as in
  state 2.

In states 1–3 the engine speed then stays within 0 … rev limit + 10 rpm.

**After the step:**

- 2 or 3 → 1 when the engine and wheel-side speeds crossed during the step (or met), in
  either direction; the engine takes the wheel side's speed and a 12-step shift lockout
  starts;
- 3 → 0 when the wheel side turned at idle or slower at the start of the step;
- 2 → 0 when the brake pedal > 0.75 and either the engine is below idle + 500 rpm or the
  car is slower than 10 km/h either way.

### §2.5 Gearbox and drive layout

Gears are reverse and 1…N; there is no neutral. The total ratio is R_g = ratio_k × final
drive for a forward gear and −(reverse ratio × final drive) in reverse; the gear words come
from the player's settings (× 0.001), and computed gearboxes generate them from a slider
(§6.7). Shifts are instantaneous: the new gear transmits from the next step, through the
clutch state of §2.4.

Road speed converts to engine speed through the **driven axle's tire radius**: the front
for FF and 4WD system 0, the rear for every other layout.

The **layout code L** comes from the drive type, the 4WD system byte (drivetrain +0x15)
and the 4WD share byte (drivetrain +0x13); the table is §7 #59. L0 is FR, MR and RR; L1 is
FF; L2–L7 are the 4WD systems. What each layout does is §2.7.

### §2.6 Shift logic

The gear decision comes at the end of phase 7, after the engine speed, with that step's
integer rpm and forward speed u (§4.1). A decided change takes effect at once (§2.4,
§2.5). The race entry's **pedal style** picks the gearbox: 0 the automatic, 1 the manual
(GT3 Revisited's AT / MT setting). The game's other styles (2 an automatic with kick-down,
3–6 sequential boxes for other controllers) exist but no pad uses them. CPU cars and every
car the AI drives use the automatic, whatever the entry says, and while the spawn/stop
counter runs every car is held in first.

**Reverse** is a request, not a gear position: the reverse (back) axis of the pad
(Triangle in GT3's layout, `docs/INPUT.md`) sets it while it is pressed harder than the
accelerator, and it then also acts as the accelerator. A digital press keeps the request
until the throttle it gave has fallen back to 0. CPU cars request reverse to back out of
trouble (ai §7).

**Automatic**, in order:

1. While the shift lockout counts, no change (the counter ends at 1 while a shift input is
   held).
2. Reverse request → reverse, and no other change while it lasts.
3. In reverse and moving forward faster than 1 km/h → first.
4. Throttle (after TCS) not 0 and forward speed < 1 km/h (rolling backwards included) →
   first (already in first: no change, and the rules below are skipped); throttle 0,
   rolling backwards faster than 1 km/h in a forward gear → reverse.
5. Over-rev: in a forward gear below the top, when the speed is past what the rev limit
   + 500 rpm gives in that gear → up one.
6. A held shift input stops further automatic changes (the game's controller setting for
   the automatic).
7. Only with the clutch locked, from a forward gear g:
   - **down** to g − 1 when rpm < D_g, D_g = max(UP · r_g / r_{g−1} − (the rpm of 10 km/h
     in gear g), launch rpm + 500), lowered by 500 rpm while the accelerator is above 0.25;
     first gear never shifts down;
   - **up** when rpm > UP and below the top gear, unless a driven wheel is in the air or is
     spinning past its optimum: slip ratio < the tire's optimum drive slip × that wheel's
     slip gain (the *traction veto*).

UP is the up-shift rpm, engine +0x46 × 100 rpm plus the turbo adder (§6.2). The
−10 km/h term gives the down-shift an anti-hunt band below the speed at which the lower gear
would reach UP. The driven wheels of the veto: the front pair for L1 and L2, all four for
L3 and L7, the rear pair otherwise.

After an automatic change the clutch is in state 3 (or 2), so rule 7 waits until it locks
again, which starts a 12-step lockout (§2.4). A standing start also begins with a
12-step lockout for the automatic (§9.3).

**Manual**:

- reverse request → reverse;
- in reverse with the accelerator pressed → the gear for the road speed (§7 #24; first at a
  standstill or rolling backwards);
- otherwise each shift input moves one gear within 1…N, and the next step's lockout makes
  a held button shift only once.

A manual car shifts nothing else: no over-rev up-shift, no down-shift as it slows.

### §2.7 Axle driveline and differentials

The driveline moves the clutch output T_g to two axles and then to four wheels.

**Inertias.** Per axle, J_axle = shaft × final drive + 2 I_a, with shaft = 0.01 × the axle's
shaft inertia word and I_a = 0.01 × its spin inertia word (§6.2). Each wheel's reaction
gain is r / I_a and a free wheel adds I_a / r² to the car's mass in the forward step (both 0
when the spin word is 0). The engine's reflected inertia J_ref = J_e · |R_g| is added to the
driven side only while the clutch is locked.

**Wheel net torque** of wheel i:

  τ_i = −r · (gained longitudinal tire force, §3.8) − sgn(w_i) · T_brake,i

with no brake term while the rim speed w_i is exactly 0. Torque vectoring (below) adds to
τ. Each axle's sum is F_f = τ₀ + τ₁ and F_r = τ₂ + τ₃.

**Drive torque per axle**:

| L | Front | Rear | Engine inertia on |
|---|---|---|---|
| 0 | none (axle not integrated) | T_g | rear |
| 1 | T_g | none (axle not integrated) | front |
| 2 | T_g − k(ω_f − ω_r) | k(ω_f − ω_r) | front |
| 3 | T_g / 2 | T_g / 2 | both (in the mean) |
| 4–7 | Q | T_g − Q | rear |

k is the layout's coupling (§7 #59). With the center clutch engaged (L4–L6) both axles turn
at one common speed and each gets T_g / 2.

**Front torque Q of L4–L7**; "full" is the full-transfer case that locks or saturates the
center clutch:

- **L4**: full when k = 1; 0 with the handbrake above 0.2; full when T_g ≤ 0 or in reverse
  with the clutch locked; else Q = k · T_g.
- **L5**: from the lateral g/μ value of the last step (§4.1): f = 1 for |g/μ| ≤ 0.25,
  0.25 for |g/μ| ≥ 1, linear between. Full when the clutch is open, or in reverse with it
  locked; when f = 1, also full with the clutch slipping, the brake pedal above 0.75, or
  T_g and T_g + F_r of the same sign; otherwise u = |ω_r − ω_f| · k · f, full if u ≥ 1,
  else Q = ½ · u · T_g.
- **L6**: 0 with the handbrake above 0.2; full in reverse with the clutch locked; g =
  max(0, 1 − |steering command| · k), full if g ≥ 0.99, else Q = ½ · g · T_g.
- **L7**: Q = k · T_g; there is no common-speed lock.

**Center clutch** (L4–L7), with its capacity (§7 #59) and the engaged flag:

- engaged: stays locked (common speed) while Q is full or |(F_f − F_r) − T_g| ≤ capacity;
  otherwise the flag clears;
- not engaged: a full Q transmits +capacity when ω_r ≥ ω_f, else −capacity; otherwise Q.

The flag starts engaged for L4–L6 at the race start (§9.3); L7 never engages it.

**Axle speeds**:

- common speed (L4–L6 engaged): ω += (T_g + F_f + F_r) / (J_f + J_r + J_ref) · dt;
- L3: the mean speed integrates with the total inertia; the half-difference h = (ω_f − ω_r)/2
  integrates as h + (F_f − F_r − k(ω_f − ω_r)) dt / (J_f + J_r), set to 0 if it changes sign;
- otherwise each axle: ω += (drive + F) / J · dt, except an axle with no inertia term (the
  undriven axle of L0 and L1) or a type `f` differential, which is not integrated here;
- a speed is set to 0 when it changes sign or was 0, but only on an axle with brake torque;
- coupled layouts (L2, and L4–L7 when not common and Q ≠ 0): when the front and rear speeds
  cross (or meet), the non-driven axle takes the driven one's speed; for L4–L6 the center
  clutch becomes engaged.

The engine side's axle speed is the driven axle's (front for L1 and L2, rear for L0 and
L4–L6), the mean of the two for L3, ω_r + k(ω_f − ω_r) for L7.

**Wheel flags** set here: while the clutch is not open, the driven wheels (the rear pair
for L0 and L4–L6, the front pair for L1 and L2, the rear pair for L3 and L7) are flagged
*driven*, and the other axle of L2, L3 and L7 *coupled*; for L4–L7 the front pair is also
flagged coupled whenever the center clutch is engaged or carries torque, even with the clutch
open. Free wheels (§10.2) are the ones with no flag at all.

**Differentials**, one per axle. The type comes from the LSD part as a letter; its index in
"frnmhvyt" selects the rule; a letter not in the string acts as open. The undriven axle of
L0 and L1 is always type `f` (each wheel alone); a driven axle without an LSD (or with `f`)
is open (`n`). The settings are the player's LSD values (§6.6). The step works on rim speeds
(m/s), each wheel with gain r / I_a, from the previous step's rim speed difference
Δv = left − right and the axle's drive torque T:

| Type | Rule |
|---|---|
| f | each wheel alone: w += τ · r / I_a · dt; a braked wheel whose speed crosses zero (or was 0) stops at 0 |
| r | locked: both wheels at the axle speed |
| n, y, t | open |
| m, h | clutch pack, capacity C = max(initial, T ≥ 0 ? accel · T : decel · (−T)) with the settings converted (§6.6): stays locked while flagged and \|τ_l − τ_r\| ≤ 2C; otherwise it unlocks and moves C from the faster wheel's torque to the slower's |
| v | viscous: moves accel × min(Δv², initial) from the faster wheel's torque to the slower's (initial is the square of the setting, accel × 0.98) |

Then the half-difference h = Δv/2 + (τ_l − τ_r) · (r / I_a) · dt / 2; if h has the opposite
sign to Δv it is set to 0 (and a clutch pack locks). Left = axle + h, right = axle − h, with
axle = r × the axle speed. A locked clutch pack keeps both wheels at the axle speed. Clutch
packs start locked at the race start.

**Torque vectoring** (types `y`, `t`), between 10 km/h and 50 m/s:
T_v = min(|steering command| / 15°, 1) × the initial setting (× 9.8 for `y`, × 0.01 for `t`;
§6.6). With m̄ the axle's mean rim speed, θ = 1.125 m̄ when steering left and 0.875 m̄ when steering right. If the right
wheel turns slower than θ, it gains 0.4375 T_v and the left loses 0.5625 T_v; if faster,
the right loses 0.5625 T_v and the left gains 0.4375 T_v.

---

## §3 Tires

Each axle has up to three tire records (compound slots 0–2, chosen by the surface class,
§3.6), each with curves A, B, C, D and L, a per-surface friction table μ[s], the base grip
and the optimum slips (decoding: §6.5). Each step and wheel, the car computes the peak force
(§3.1), the slips (§3.2, §3.4) and the force outputs (§3.3, §3.8).

| Curve | Input | Output |
|---|---|---|
| L | load per meter of section width, N/m | load factor |
| A | folded slip angle, rad (0…π/2) | lateral force fraction |
| B | folded slip angle, rad | force fraction along the hub velocity (drag) |
| C | signed slip ratio (−1…1) | longitudinal force fraction |
| D | signed slip ratio | friction-circle radius for the lateral pair |

A compound with no B curve gets the game's default B curve (§6.5).

### §3.1 Peak force

The peak force of a wheel (phase 2):

  F_p = F_z · L(F_z / width) · wear factor · axle factor · μ[s] · Cam(α)

- **F_z** is the wheel load the previous step's phase 12 left (§1.2); 0 gives 0.
- **μ[s]** is the record's friction for surface index s (§3.6, §6.5).
- **Camber factor** Cam(α): a 4-point curve per axle from the suspension part (angle codes →
  (128 − code) × 0.125°, multiplier codes / 200; §6.4). The camber angle is
  α = σ · (body roll − ground roll) + camber offset, with σ = −1 on the left wheels and +1 on
  the right, and the camber offset = −(camber setting × 0.1°).
- **Axle factor** = (b + 900) × 0.001, with b = (chassis byte + 124) mod 256 (the chassis
  byte at +0x12 front, +0x1E rear). For CPU cars the event can scale it by a front and a rear
  percentage for the car's driveline class (§7 #21; 0 reads as 100 %).
- **Wear factor**: §3.7.

### §3.2 Slip angle and low-speed fade

The *velocity-direction angle* of a hub is φ = atan2(−l_h, u_h), where (u_h, l_h) is the
hub velocity along the car and to the right, on the ground frame, as the last step left it
(positive φ = moving to the left). The hub velocity carries only the yaw rate r:
u_h = u + X·r, l_h = l − Y·r, with lateral offsets X = (−t_f/2, +t_f/2, −t_r/2, +t_r/2) and
longitudinal offsets Y = (a, a, −b, −b) (§10.1).

The **fade weight** k follows the hub's speed √(u_h² + l_h²): 0 at ≤ 1 km/h, 1 at ≥ 10 km/h,
linear between. When it is 0, φ is taken as 0 and the wheel makes no slip-angle force. The
fade multiplies the lateral pair (§3.3), so lateral grip builds up between 1 and 10 km/h.

The **tire slip angle** (phase 7) is β = wrap(a) − wrap(φ), re-wrapped into [−π, π), with
a = the wheel's steer angle (+ left), plus π when the hub moves backwards (u_h < 0). It is
folded to v ∈ [0, π/2] with a sign:

| β | v | sign |
|---|---|---|
| < −π/2 | β + π | + |
| −π/2 … 0 | −β | − |
| 0 … π/2 | β | + |
| ≥ π/2 | π − β | − |

The mean φ of the two front wheels also extends the steering bounds (§4.4).

### §3.3 Tire force

From the folded slip angle v, the fade k, the slip ratio s (§3.4) and F_p:

- **Lateral pair**: u₁ = sign · A(v) · k and u₂ = B(v) · k. If |(u₁, u₂)| exceeds the
  friction-circle radius D(s), both are scaled down to it.
- **Slip grip** F₁ = u₁ · F_p acts across the hub velocity, against the slide; **slip drag**
  F₂ = u₂ · F_p ≥ 0 acts along it, against the motion. On the car, in the body frame
  (forward, right): F = −F₂ (cos φ, −sin φ) − F₁ (sin φ, cos φ).
- **Longitudinal**: F = C(s) · F_p along the wheel heading, negated when the slip sign sg
  (§3.4) is negative. C and D are looked up at the *signed* slip ratio.

The D limit applies only to the lateral pair: it is the room the slip ratio leaves for
cornering force. The longitudinal force is not reduced by the slip angle.

### §3.4 Slip ratio, slip state and slip gain

With x = the hub speed along the wheel heading (u_h cos δ − l_h sin δ for a steer angle δ,
+ left), w = the wheel's rim speed and ε = 1/57.6 m/s (phase 5):

| Case | Raw slip | State st | Sign sg |
|---|---|---|---|
| \|w\| < ε | 1 if x ≠ w, else 0 | −1 | sign(w − x), 0 when equal |
| w ≥ ε, x ≤ w (wheelspin) | g (x − w) / w | +1 | +1 |
| w ≥ ε, x > w (braking) | g (x − w) / x, 0 if \|x\| < ε | −1 | −1 |
| w ≤ −ε, x ≥ w | g (x − w) / w | +1 | −1 |
| w ≤ −ε, x < w | g (x − w) / x, 0 if \|x\| < ε | −1 | +1 |

The raw value is clamped to [−1, 1]. A low-speed fade then multiplies it by |r_ref| / 20 km/h
when |r_ref| < 20 km/h, with r_ref = x for state −1 and w for state +1.

The **slip gain** g = 0.5 + 0.5 · min(|u_h| / 100 km/h, 1), from the hub's speed along the
car, rises from 0.5 at a standstill to 1 at 100 km/h (the same for the driving and braking
sides). It enters the slip ratio, the gained wheel reaction (§3.8), ABS (§4.5), TCS (§4.6)
and the shift veto (§2.6).

The **slip state** st is +1 in the wheelspin branches and −1 when braking or stopped; the
**slip sign** sg is the direction the longitudinal force acts in.

Free wheels are set to roll with the ground at the end of each step (§10.2), so their next
slip ratio is exactly 0.

### §3.5 Rolling resistance

Rolling resistance acts on the forward speed u in the forward step (§4.1):

  F_rr = −sgn(u) · RR(|u|) · Σ F_z,i · rr[s_i]

over the four wheels, with F_z the previous step's loads, rr[s] the surface coefficient (§3.6)
and the speed curve RR:

| km/h | 0 | 4 | 20 | 58 | 116 | 200 | 308 | 400 |
|---|---|---|---|---|---|---|---|---|
| RR | 0 | 0 | 0.010 | 0.015 | 0.032 | 0.072 | 0.144 | 0.255 |

On tarmac (rr = 0.1) a 16.5 kN car at 100 km/h feels about 45 N; on gravel (surface 3,
rr = 25.5) the same car feels about 11 kN, which stops it quickly.

### §3.6 Surfaces

The ground query gives each wheel the triangle's material and flags bytes
(`docs/formats/course.md`). The surface index is the material, except that with flag 0x02
(wet) materials 0 and 7 become 12, 1 becomes 13 and 9 becomes 14. A course whose road uses a
material past the 16-entry tables is refused when the race is set up.

The game's per-surface table:

| s | Surface | Bump amplitude (m) | Wavelength λ (m) | Speed-scaled | rr[s] | Tire slot |
|---|---|---|---|---|---|---|
| 0 | tarmac | 0 | 0.5 | no | 0.10 | 0 |
| 1 | curb | 0.010 | 0.5 | no | 0.10 | 0 |
| 2 | grass | 0.010 | 2.5 | yes | 0.20 | 1 |
| 3 | gravel | 0.024 | 5.0 | yes | 25.5 | 1 |
| 4 | sand | 0.020 | 5.0 | yes | 0.30 | 1 |
| 5 | dirt | 0.010 | 0.5 | yes | 0.20 | 1 |
| 6 | – | 0 | 1.0 | yes | 0.10 | 2 |
| 7–11 | paving, others | 0 | 1.0 | no | 0.10 | 0 |
| 12–15 | wet tarmac, wet curb, wet road, – | 0 | 1.0 | no | 0.10 | 2 |

**Tire slot**: the compound record a wheel uses on that surface (§6.5).

**Road bump**, subtracted from the wheel's travel like a rise in the ground (§4.2), on
surfaces with a non-zero amplitude: with w = sin(2πx/λ) + cos(2πz/λ) at the query point's
world (x, z), the bump is amplitude × w·|w| × scale, where scale = min(1, 0.02 · |hub speed|)
on speed-scaled surfaces and 1 otherwise. A curb therefore moves the ground by up to ±4 cm
in a 0.5 m pattern. The same speed ramp feeds the slip intensity (§8.3).

The pit-lane flag (0x01) is read by the wear and pit rules (§8.6). Grass, gravel, sand and
material 6 count as soft ground for the course-out rule (§11).

### §3.7 Wear grip factor

The wear factor in the peak force (§3.1) is the grip factor of the wheel's wear (§8.4) when
the race has tire wear (§8.1), never below the floor of the tire record the wheel is on, and
it is not applied while the car is on the pit-lane surface. Without wear it is 1.

### §3.8 Per-wheel force outputs

Each wheel hands the planar step (§4.1) and the driveline (§2.7) these values:

| Output | Meaning |
|---|---|
| φ | velocity-direction angle (§3.2) |
| slip grip F₁, slip drag F₂ | the lateral pair (§3.3), in the velocity-direction frame |
| drive part | F = ±C(s) F_p when st = +1 (wheelspin branch), else 0; along the heading |
| brake part | the same F when st = −1, else 0 |
| G = drive part + brake part | the longitudinal force on the car (not gained) |
| gained copies | g · drive part, g · brake part: the reaction on the wheel's spin (§2.7) |
| low-speed hold | §4.1 |

So the car is pushed by the ungained force C · F_p, while the wheel is slowed or sped up by
g · C · F_p, with g the slip gain of §3.4. An unloaded wheel has F_p = 0 and makes no force.

Each wheel also has **flags**, set at the start of phase 7: *no load* (its load is 0) and
*braked* (it has brake torque); the driveline adds *driven* and *coupled* (§2.7). The forward
step reads only the first two (§4.1); a wheel with no flag is free (§10.2).

---

## §4 Body motion, suspension, steering, brakes and aids

### §4.1 Planar motion

The body's horizontal motion is integrated from the tire outputs (§3.8), not from forces
applied at the contact points. It works in the body frame (forward, right), with each
wheel's steer angle d (+ left), k = cos d and s = sin d.

**Forward step.** With u the forward speed on the ground frame (§10.1):

- F_x = Σ k · drive part + F_rr (§3.5);
- effective mass m_eff = M + Σ I_a / r² over the wheels with no flag (loaded and not
  braked: a free wheel's spin inertia adds to the car's);
- a₀ = (F_x + drag) / m_eff (drag from §5.2), u₁ = u + a₀ dt; if u₁ is exactly 0 the step
  ends here;
- F_t = Σ (k · brake part − sin φ · F₁ − cos φ · F₂), a_t = F_t / m_eff, u₂ = u₁ + a_t dt;
- if u₁ · u₂ > 0, u ← u₂ and a_x = a₀ + a_t; otherwise the tire reactions stop the car:
  u ← 0 and a_x = a₀ − u₁/dt.

The **longitudinal force sum** the load transfer reads (§4.2) is F_x, plus F_t when the
reactions did not stop the car.

**Lateral force and yaw.** For each wheel, with G = drive part + brake part and H the
low-speed hold:

- F_a (right) = −cos φ · F₁ + sin φ · F₂ − k · H − s · G
- F_b (forward) = −sin φ · F₁ − cos φ · F₂ − s · H + k · G

Yaw torque N = Σ (X · F_b − Y · F_a) with X = (−t_f/2, +t_f/2, −t_r/2, +t_r/2) and
Y = (a, a, −b, −b). Yaw acceleration = N / I_z; the yaw rate is clamped to ±2π rad/s;
a_y = Σ F_a / M; the lateral **g/μ** value = 4 a_y / Σ μ_i[s_i] · g_i, with g_i the tire
record's base grip (§6.5), so it is the lateral acceleration over
the mean surface μ. These values are kept for the next step's aids (§4.6), the 4WD coupling
(§2.7) and this step's load transfer (§4.2).

**Yaw inertia** I_z = 100 × the chassis inertia byte (chassis +0x1C × lightweight % × body %,
§6.4; 6000 if 0).

**Velocity update.** The world velocity gains a_x · dt along the horizontal part of the
body's forward axis and a_y · dt along the horizontal part of its lateral axis; the
vertical velocity comes from the attitude step (§4.3).

**Low-speed hold**, a sideways damper for a wheel almost at rest: with w the rim speed,
u_h the hub's speed along the car and y the hub's speed across the wheel, the hold is
F_p · sin(0.9π · y) · cos(0.36π · w), and 0 when F_p = 0, |w| > 5 km/h, |u_h − w| > 2 km/h
or |y| > 4 km/h. It enters F_a and F_b as above, and so acts on the lateral force and yaw
only.

**Holds.** The planar velocity, the yaw rate, the forward and lateral speeds, both
accelerations, the lateral g/μ and the longitudinal force sum are zeroed, so the car stands
still in the plane:

- during the **race hold** (the start countdown) and for a **parked car** (driver kind 8,
  which also has its handbrake fully on, ai §1.1), whatever the speed; the vertical velocity
  is kept, so a car settles on slopes;
- by the **stop clamp**: |u| < 1 km/h, every wheel's fade weight 0 and some wheel braked.

While the **spawn/stop counter** runs (§4.3) the planar velocity, yaw rate and longitudinal
force sum are zeroed too, even in the air.

With all four wheels unloaded (the loads the tires read add up to 0) the lateral step only
zeroes a_y and g/μ (the yaw rate is left as it is) and the velocity update is skipped, so the
car keeps its horizontal velocity; the race hold, a parked car and the stop clamp do not act
then (§10.6).

**Position and heading.** The collision step (phase 10) then moves the CoG by velocity × dt
and the heading by yaw rate × dt, against the walls and the other cars (§5.3); phase 12
queries the ground at the moved car.

### §4.2 Suspension

Each corner is an independent spring, damper, bump stop and droop limit in the *suspension
coordinate* x (0 = spring force zero, negative = compressed), from the axle's suspension
block. The ground query and suspension run in phase 12, at the end of the step, after the
car moved; the next step's tires read the loads they leave.

**Parameters** (from the player's settings and the suspension part; §6.6):

| Parameter | Value |
|---|---|
| spring k | spring setting (0.1 kgf/mm) × 980 × c / 100 N/m, c = motion ratio % |
| progressive k₂ | byte × 98000 N/m², below the knee |
| anti-roll gain | anti-roll setting value × 98 N/m² |
| unsprung mass m_u | byte × 0.25 kg per wheel; its weight is the wheel's preload |
| bump damping below the knee | byte × 98 N per m/s |
| dampers | square-root law through two points per direction (below) |
| ride height q | the **front** ride-height setting × 0.001 m, for both axles |
| bump room, droop, knee gap g₁ | bytes × 0.001 m |

Both axles read the front ride height; the rear setting has no effect. The stock R34 GT-R
V-spec stands at q = 0.130 m on both axles (`car_spec_r34_gtr`).

**Static position**: x_s = −½ · share · m_s · g / k, with m_s = M − 2(m_u,f + m_u,r) the sprung
mass and share = w_f for the front and 1 − w_f for the rear: the weight split applies to the
sprung mass, and each wheel's unsprung weight (the preload) comes on top.

**Limits**:

- bump-stop limit = x_s − q + min(bump room, q);
- knee = max(x_s − g₁, bump-stop limit);
- droop = min(x_s + droop, preload / k).

**Suspension coordinate from the ground.** With h the query point's height (§1.3), g the
ground height under it, β the road bump (§3.6) and s_t the terrain factor (§4.3):

  x = x_s − q + s_t · (h − g − β)

At the static pose (h − g = q on flat ground) x = x_s. The triangle's slope does not enter.

**One wheel**, with v = (x − x′) · 60 / time scale from the previous clamped value x′:

- **airborne** when x > droop: x′ := droop, spring force = −preload (the body feels the
  hanging wheel), no damper, load 0;
- otherwise x is clamped to the bump-stop limit (x_c = max(x, limit)), and
  - spring F_s = −k x_c + (x_c < knee ? k₂ (x_c − knee)² : 0),
  - damper F_d from the fitted law, plus bump damping × min(|v|, 1 m/s) while compressing
    below the knee,
  - tire load F_z = max(0, F_s + F_d + preload);
- x_c is stored as the next x′ (§10.5).

**Dampers**: F(v) = o + g √(v − t) with o = −g √(−t), so F(0) = 0, fitted through two
(speed, force) points per direction (speeds = byte × 0.01 × 100/c m/s, forces = setting value
× 49 N). The speed is clamped to ±1 m/s. Compressing (v < 0) uses the bump curve and gives a
positive force; extending uses the rebound curve, negated. Points that do not fit give no
damping.

**Anti-roll bar**: with d = x′_left − x′_right of the axle's stored coordinates (a hanging
wheel counts at full droop), the left wheel's body force gets −gain · d · |d| and the right
the opposite. It acts on the body forces, not on the tire loads.

**Load transfer.** The tire forces make no moments about the CoG; instead the body forces are
shifted explicitly, using this step's a_y and longitudinal force sum (§4.1), on all four
wheels:

- longitudinal: each front wheel +d_long, each rear wheel −d_long, with
  d_long = (m_s / M) · X / L · (force sum);
- lateral: each left wheel −d_lat, each right wheel +d_lat, with d_lat = m_s · X · a_y / t̄;

with t̄ = w_f t_f + (1 − w_f) t_r the mean track and X the transfer height ½ (chassis +0x1D ×
0.001 m + ride height). The
loaded side's support force drops, so the body sinks there and its springs and tire loads
take the transfer.

**Body force** of a wheel = F_s ± anti-roll ± transfer − ½ its axle's downforce (§5.2). The
damper force enters the attitude step separately (§4.3).

The race screen draws each wheel at its stored coordinate x′, so an airborne wheel hangs at
full droop.

### §4.3 Attitude, heave and the ground frame

**Ground frame**, from the ground heights g₀…g₃ (FL, FR, RL, RR) under the four hubs, with
w = 1 − w_f (built at the end of phase 12):

- U = g₀ + w (g₂ − g₀), V = g₁ + w (g₃ − g₁) (the left and right heights at the CoG);
- height under the CoG = (U + V) / 2;
- ground pitch = atan2(((g₀ + g₁) − (g₂ + g₃)) / 2, L), nose up positive;
- ground roll = atan2(U − V, t̄), right side down positive.

The ground frame's axes are the Euler axes of (ground pitch, ground roll, heading). The
**terrain factor** s_t = (ground up · world up) / (body up · world up) scales the suspension
coordinate (§4.2).

**Attitude step**, with F_i the body force (§4.2) and D_i the damper force of wheel i, and
lever factors l_f = a · 2π / I_p, l_r = b · 2π / I_p, r_f = (t_f / 2) · 2π / I_r,
r_r = (t_r / 2) · 2π / I_r, where the pitch inertia I_p is I_z and the roll inertia I_r is
100 × half the chassis inertia byte, about I_z / 2 (4000 and 2000 when the byte is 0; §6.4):

- pitch acceleration = (F₀ + F₁ + D₀ + D₁) l_f − (F₂ + F₃ + D₂ + D₃) l_r + A;
- roll acceleration = (F₀ − F₁ + D₀ − D₁) r_f + (F₂ − F₃ + D₂ − D₃) r_r;
- rates are clamped to ±π/2 rad/s, then angles move by rate × dt; an angle beyond ±π/4 is
  clamped there and its rate zeroed;
- heave force H = Σ F + Σ D + B; the velocity gains H / m_s · dt along the body's new up
  axis, then −g · dt vertically.

The **pitch terms** A and B come from the longitudinal tire forces: q_i = cos d_i × G_i × the
axle's section width, with G the drive + brake parts only (the lateral outputs do not enter,
even on a steered wheel); A = (q₀ + q₁) k_f + (q₂ + q₃) k_r with k_f = ½ · 2π / (I_p + M a²),
k_r = ½ · 2π / (I_p + M b²); B = (q₀ + q₁)/a − (q₂ + q₃)/b. Drive force lifts the nose and
braking dips it.

The heave changes only the velocity; the position moves in the next collision step (§4.1).

**Bump-stop nudge.** Only when some wheel's coordinate was beyond its bump stop at the start
of phase 12: the coordinates are recomputed with the new attitude; if a wheel is still beyond,
with penetrations p_i = max(0, bump-stop limit − x_i) of all four wheels,
l = p₀ + w(p₂ − p₀), r = p₁ + w(p₃ − p₁), front mean f̄ and rear mean r̄:
roll += atan2(l − r, t̄) (if l ≠ r), pitch += atan2(f̄ − r̄, L) (if they differ),
height += ½ min(l, r) + ½ min(f̄, r̄). Then, whether or not a wheel was still beyond, the
downward velocity is limited to the vertical part of the velocity's projection on the last ground frame's plane: the body
may not sink faster than sliding along the ground would. There is no other body-ground
contact: the ground acts only through the four wheels, the bump stops and this nudge (§11).

A car starts on the ground frame at its static ride height and loads, with no settling pass
(§9.3).

**Spawn/stop hold.** A counter n, set to 120 for the free-practice pit start and to 300 for a
pit stop (§8.6), counts down by one at each step's phase 2. While n > 0:

- the clutch request is 0 and the gear is first (§2.4, §2.6), and the planar motion is held
  (§4.1);
- target height = the ground under the CoG + offset(n), with offset 0 for n < 30, 0.3 m for
  30 ≤ n < 270, and (300 − n) × 0.01 m above;
- if the CoG is at or below the target, the car is lifted to it, level (pitch and roll 0)
  and at rest vertically, and the attitude step is skipped; for 30 ≤ n < 270 the heading
  turns towards the car's pit box heading at up to 30°/s;
- if the CoG is above the target (the last 30 steps), it falls normally.

So a pit stop lifts the car until its CoG is 0.3 m above the ground (within the first half
second), holds it there while it turns to its pit box, and lets it drop for the last half
second; the pit start begins with the car held at that height. Nothing else uses the hold:
there is no respawn (§11).

### §4.4 Steering and pad input

**The pad frame.** Phase 3 reads one frame per car and step: steering ±4096 for full lock
(positive left), throttle, brake, the third axis (handbrake), the back axis (reverse), a shift
request, which axes are analog, and the device kind. The players' pads go through the game's
pad read as a DualShock 2 with pressure buttons in GT3's layout (`docs/INPUT.md`): the left
stick steers (kind 2), the d-pad's pressure steers as the game's digital turn-in (kind 3), and
a steering-wheel setup goes through the game's steering-wheel path (kind 4).

**Analog response** (Settings > Controls > Analog response, `[race] analog_response`). By
default (`linear`) the frame's analog values are chosen so that the steering, throttle,
brake and handbrake after phase 3's shaping follow the controller's travel linearly: the
stick's (or wheel's) position goes straight to the steering, the d-pad's pressures to the
kind-3 steering, and each pressure to the frame value whose shaped pedal is that fraction
(any press gives at least the shaping's least step, 0.1). `original` keeps the game's
DualShock 2 curves as they are: a dead band over the stick's inner 29 %, full throttle at
18 % of the pressure range, brake at 33 %, handbrake at 10 %. The step itself is the same
either way, and a replay keeps the frames the race got.
Where the race screen gives no pad (scripted runs), the frame is all analog, kind 2, or
kind 3 for a d-pad input. A disc demo past its last frame gives an empty frame (digital,
nothing pressed).

**Pedals.** An analog pedal x becomes x + 0.1 (1 − x) once it is above 0: any press gives at
least 0.1. A digital (button) throttle or brake ramps by 1/6 per step up and down (full in
0.1 s); a digital handbrake is on or off at once. A steering wheel's pedals also go through
the game's pedal curves. The brake pedal sets every wheel's brake pressure (§4.5).

**Steering bounds.** The lock at forward speed u (at least 0) comes from one of the car's
three lock curves (6 points each, km/h → degrees; §6.4): the pad curve B, the steering-wheel
curve A and the dirt curve C. On a dirt course (course category 2), while both front wheels
are on loose ground (surfaces 2–6), a pad uses curve C; a steering wheel uses C anywhere on a
dirt course and A elsewhere; otherwise a pad uses B. The mean velocity-direction angle φ̄ of
the two front wheels (§3.2) extends the bound on its own side: when φ̄ lies beyond the bound
it becomes the bound, so when the car slides the steering can follow the slide. Both bounds
are then limited to the full lock (steer part +0x11 degrees).

**Target and follower.** A digital press targets the bound on its side, an analog input (the
stick, or the d-pad's pressure) its deflection × the bound on its side. A second-order
follower moves the steering command δ towards the target t:

- acceleration = k₁ (t − δ) − k₂ ω, and additionally − k₃ δ while returning towards the
  center (the target on the center side of δ); k₁ = 100, k₂ = 5, k₃ = 50;
- the rate is limited by the device: the d-pad (digital or pressure) turns in at up to
  30°/s and returns at up to 120°/s; the stick turns both ways at up to 120°/s; on a dirt
  course with both front wheels on loose ground both turn both ways at 120°/s;
- when δ would cross the target it lands on it and the rate is zeroed.

A steering wheel's position is the command directly, with its bounds following the lock curve
at up to 90°/s.

**Wheel angles** (phase 4). The front wheels get δ × their Ackermann ratio: inner =
atan2(L, L − t_f/2) / (π/4), outer = atan2(L, L + t_f/2) / (π/4), the inner wheel (on the
side the car turns to) getting the larger. Every wheel then gets its
axle's toe, atan2((toe setting − 128) × 0.0005, tire radius), as toe-in: subtracted on the
left wheel and added on the right.

**When the AI drives** (CPU cars, and a player's car in its run-up, in the pit lane and after
the finish; ai §1): the AI sets the steering command directly (slewed at its own steering
rate, ai §6), the throttle (in its arc modes moving by at most dt per step), the brake pedal,
the reverse request and a gearbox lockout; the pedals are not shaped. It drives with the
automatic (§2.6), its own clutch rule (§2.4) and its driver's aid block instead of the car's
own TCS and ASCC (§4.6).

### §4.5 Brakes and ABS

Per wheel (phase 6):

1. pressure p = the brake pedal (on a dirt course the front is reduced with steering,
   §10.4), then ASCC (§4.6);
2. **ABS** on every wheel with p > 0 (always on):

     p ← p · clamp(F_p · E · (1 − G · g · (s − s*)), 0, 1),  E = r / T_max

   with F_p this step's peak force, r the tire radius, T_max the axle's maximum brake torque,
   G the axle's ABS gain, g the slip gain, s the slip ratio and s* the tire's optimum braking
   slip (× the ASCC factor of §4.6). A result of exactly 0 becomes 0.001; an unloaded wheel
   (F_p = 0) therefore keeps 0.001;
3. brake torque (phase 7) = p · T_max, plus the handbrake × its torque on each rear wheel.

| Parameter | Value |
|---|---|
| T_max front / rear | brake part +0x11 / +0x12 × 98 N·m per wheel |
| handbrake | brake part +0x13 × 98 N·m per rear wheel |
| ABS gain G | brake-controller setting value × 0.1, front and rear (§6.6) |

Brake torque opposes the wheel's spin (§2.7).

### §4.6 Traction control, ASCC and the under/oversteer indicators

**Which aids.** A car the pad drives uses its own aid block (from its TCSC and ASCC parts and
settings, §6.6), with ASCC off while the handbrake is at 0.1 or more. A car the AI drives
uses its driver's aid block (below), ASCC included. On a dirt course every car's own TCS and
ASCC are off: the race clears them when it builds the car. ABS is not part of either block
(§4.5).

**Indicators** (every car moving forward, u > 0), from the last step's values:

- **understeer**: 0 unless the steering command δ and the yaw rate r do not turn opposite
  ways and |a_y| / Σ g_i > 0.2, with g_i the tire records' base grip (§4.1); r gets half the
  yaw acceleration added when the two share a sign; with the target r_t = u · sin|δ| / L,
  understeer = 1 − |r| / r_t while |r| < r_t, else 0;
- **oversteer**, a car the pad drives: from the larger rear |β| of the last step's tires and
  the planar speed v = √(u² + l²): q = min(1, 0.018 v), threshold θ = 20° → 5° and gain
  3 → 14 as q goes 0 → 1; oversteer = min(1, gain · (|β| − θ) / (90° − θ)) once |β| ≥ θ;
- **oversteer**, a car the AI drives: 2 · max(0, |v · r| − |a_y|) / Σ g_i when v · r and
  a_y have opposite signs, else 0 (not clamped; PAL's: 0 below 25 km/h).

**Traction control (TCS)**, active when its gain is not 0, with a full-throttle torque above 0
and the throttle not 0:

- the TCS wheels by drive layout: the front pair for FF; the rear pair for FR, MR, RR and 4WD
  systems 3–6; all four for 4WD systems 0, 1, 2 and 7. A wheel without
  load is left out;
- s_min = the smallest slip ratio below 1 among them, with g its slip gain and s* the optimum
  drive slip (negative) of its tire on the surface under it; with no such wheel s_min, g and
  s* are 1 (no cut);
- lateral factor λ = 1 if |g/μ| ≤ b, else max(0, 1 − (|g/μ| − b) · a), with g/μ the last
  step's value (§4.1; §7 #12);
- throttle × clamp(1 + gain · g · (s_min − s* · λ · scale), 0, 1).

| Source | gain | a | b | scale |
|---|---|---|---|---|
| a car's own (TCSC part) | +0x13 × 0.1 (0 at level 100) | +0x14 × 0.1 | +0x17 × 0.01 | level × 0.01 (0 or 100 → 1) |
| the AI's aid block | p13 × 0.1 | p11 × 0.1 | p12 × 0.01 | p24 × 0.01 (0 or 100 → 1) |

The TCS level is the setting interpolated between the part's minimum and maximum (§6.1);
level 100 means off.

**ASCC** acts on the brake pressures, after the dirt cut. The inner wheels are the left ones
while the yaw rate is positive (turning left) and the right ones otherwise. Each channel has a
gain, A and C a threshold, and per channel a front and a rear enable bit; a channel acts only
when one of its bits is set and its gain is not 0.

| Channel | When | Wheels | Effect |
|---|---|---|---|
| A | brake pedal exactly 0 | outer | p += (oversteer − threshold_A) · gain_A when above the threshold |
| C | brake pedal and throttle pedal exactly 0 | inner | p += (understeer − threshold_C) · gain_C when above |
| D | brake pressed | inner | p ×= clamp(1 − (oversteer − 0.1) · gain_D, 0, 1) |
| E | brake pressed | outer | p ×= clamp(1 − (understeer − 0.1) · gain_E, 0, 1) |

D and E scale the wheel's ABS target slip by the same factor. The pressure starts from the
pedal every step and is kept within [0, 1].

**The AI's aid block.** A CPU car and a player's car the AI drives read the block of the
car's AI driver parameter set (bytes p0…p24, ai §9.1): the TCS terms above, and the ASCC
channels with gain_A = p9 × 0.01, threshold_A = p10 × 0.01, enable bits = p19,
gain_D = p20 × 0.01, gain_C = p21 × 0.01, threshold_C = p22 × 0.01, gain_E = p23 × 0.01.
Every built-in set gives TCS 6.0 / 1.0 / 0 / 1.0 and enable bits 0 (no ASCC); a course's own
set with non-zero bits turns the channels on.

**The Options menu's ASM / TCS.** In arcade single races, free runs and two-player battles
(both players' cars) the race applies the two switches when it builds the car; other modes
always keep the aids; a saved replay keeps those of its run and a demonstration those of its
recording. ASM off clears the ASCC
enable bits, so no channel acts; TCS off sets the TCS gain to 0. ABS and the AI's aid block
are not affected.

**Dash lamps.** The TCS lamp is lit while the car's TCS is on and blinks while it cuts the
throttle; the ASM lamp is lit while channel A or D is enabled and ASCC is not switched off by
the handbrake (presentation §D3).

### §4.7 Aerodynamic parameters

From the body part:

| Parameter | Value |
|---|---|
| frontal area A | body f32 +0x44 m², truncated to 0.0001 m² (the body's width × height if that is 0) |
| C_d | body +0x39 / 100 |
| air density ρ | 1.2 kg/m³ |
| A_f | ½ ρ A |
| downforce C_front, C_rear | A_f × the front / rear downforce setting % (stock: body +0x13 / +0x3C) |
| drag coefficient C_drag | A_f · C_d + 0.1 (C_front + C_rear) |

The stock R34 GT-R V-spec gets C_drag = 0.511, C_front = 0.348, C_rear = 0.564 N/(m/s)²
(`car_spec_r34_gtr`). The forces are §5.2.

---

## §5 Air, contact and collisions

### §5.1 Proximity and broad phase

**Proximity** (phase 11, after the collision step). Two cars *see* each other when neither is
a ghost, both are on the pit-lane surface or both are off it, their CoGs are within 100 m,
and the higher CoG is at most the lower car's body height + 5 m above the other. For each such
pair the phase computes, in each car's frame:

- a **relative-position code** (within 50 m): along the car, 6 beyond 4 × the combined
  extents ahead, 5 beyond them, 4 overlapping in front, 3 overlapping behind, 2 behind
  within 4 × the combined extents, 1 farther behind; across, seven bands from far left to far
  right at ±½, ±1 and ±4 × half the summed widths;
- the AI's **zone cells** around the car and its **contact intensity** from the gap and
  closing speed to a car ahead (within 20 m; ai §14);
- the **slipstream factor** (§5.2).

**Collision broad phase.** Each car carries a **road segment hint**: the road segment its
front-left wheel's ground query found on the last step (§1.3), −1 off the road. Two cars on
different segments meet only when the second's segment is in the first's list of neighbors,
so cars on a bridge do not touch cars below; a car off the road uses the course's list for
"no segment" (the words stored before the segment table). Within that, a pair is swept only
when the boxes swept by the two outlines overlap and the closest approach of the two centers'
straight paths is within the sum of their bounding radii; the static separation needs the
centers within that sum.

Walls come from the course's wall grid: the cells of the box of the car's start and end
outline points, grown by 8 m (clamped into the grid), and in them the wall points of the
hint's segment and its neighbors (all of them without a hint); each starts a candidate wall
segment. There is no height test.

### §5.2 Aero forces and slipstream

**Slipstream factor** of a follower behind a leader (phase 11), with the follower at
(x forward, y lateral) in the leader's frame, the leader's forward speed v and width w:

- (v + x) / v when v > 100 km/h, −v < x < 0 (within one second of the leader's travel) and
  |y| < w;
- 0 otherwise.

A car's **target** is the sum of its factors behind every other car, at most 1.

**Filter** (phase 2 of the next step): y ← min(1, y + target × 2/s × dt) while the target is
not 0, else y ← max(0, y − 0.5/s × dt).

**Forces**, with u the forward speed on the ground frame:

- drag = −sgn(u) · C_drag · (A + (1 − A)(1 − y)) · u², A = 0.5;
- downforce_j = C_j · (B + (1 − B)(1 − y)) · u², B = 1 (the slipstream does not change
  downforce).

Full slipstream halves the drag. The drag enters the forward step (§4.1); each wheel's body
force loses half its axle's downforce (§4.2), so the body sinks and the tire loads grow.

### §5.3 Collisions

Collisions are 2-D, in the horizontal plane of the physics frame. A car is an **octagon**
around its CoG: with a = CoG → front, b = CoG → rear, h = width / 2 and chamfer c = 0.2 ×
width, the points (x lateral, y forward) are (h, a − c), (h − c, a), (−h + c, a),
(−h, a − c), (−h, −b + c), (−h + c, −b), (h − c, −b), (h, −b + c); points 0–3 are at the
front, 4–7 at the rear, and edge k runs from point k to point k + 1. The extents come from the
body record measured from the wheelbase midpoint (§6.2), so a = front extent − L(w_f − 0.5)
and b = rear extent + L(w_f − 0.5). The body has the car's mass and a **yaw inertia of
10 · I_z**.

#### The collision step

Course walls are polylines whose drivable side is on the **left** of the line direction
(`docs/formats/course.md`). Walls are one-sided: only a point that crosses from that side to
the other is stopped. A wall point flagged as a **corner** is also tested against the car's
edges. Edges flagged as sensors record a crossing without a response, and nothing reads it.

Races, both kinds of battle and free practice move every car in one step, with car contact;
time trials, license tests, free runs and machine tests step each car against the walls only.
A ghost car takes no part in car contact. Each body starts from its car's position, height
and heading as the last step left them, with the car's current velocity and yaw rate and its
own step length dt. With t the fraction of the step already used (0 at first), at most
**2 rounds in all** of:

1. **Static separation** of every ordered pair of car-contact bodies: for each point of one
   body inside the other's outline, the vector along the line of centers to the boundary; the
   deepest decides, and each body moves half of it.
2. Per body, the **persistence** of its flagged points (below): car contacts, then wall
   contacts.
3. **End pose** = start + v · dt · (1 − t) for position, height and heading.
4. **Sweeps**, body by body: its unflagged points against each candidate wall segment
   (strictly on the track side at the start, strictly outside at the end, moving between the
   segment's ends; t = s₀ / (s₀ − s₁)); the walls' corner points against its edges (outside at
   the start, inside at the end, within the area the edge swept); and every other body's
   unflagged points, each on a straight chord in this body's frame, against its edges (a
   crossing from outside to inside with the chord passing between the edge's ends). Per body
   the earliest wall hit is kept (point k before edge k); a car hit replaces it unless the body
   already holds an earlier car or point-wall hit.
5. The **earliest hit of all bodies** wins (ties: the lower body, its wall hit first). Every
   body advances to it (linear in pose), then the hit is answered:
   - **point on a wall**: a point past the wall line moves onto it; then
     j = −1.05 (v_p · n) / K, K = 1/m + (r × n)² / (10 I_z), with n the wall's normal into the
     track (also for a separating contact); **friction** min(0.5 |j|, m |v_t|) against the
     contact's velocity off the normal, the vertical velocity included (which it never
     reverses); the yaw rate changes by the moment of the normal impulse alone; the point is
     flagged on that wall;
   - **wall corner on a car edge**: the same impulse and friction at the corner with the
     edge's outward normal; no move, no flag;
   - **car on car**: if the hitting point is still outside the edge line, each body moves half
     the gap along the normal (**touch-up**); then
     j = −1.5 (v_rel · n) / K, K = 1/m_a + 1/m_b + (r_a × n)²/(10 I_a) + (r_b × n)²/(10 I_b),
     restitution 0.5, no friction, also for a separating contact; the hitting point is flagged
     on that edge of that car.

The bodies end at the last end pose computed, so an impulse in the last round changes only
velocities. Then per body: if it is nearly stopped (|v_x| and |v_y| below 1 m/s) and the
step's last answered hit came early (t < 0.1), the **stuck-steer nudge** adds
20 × s × dt to the yaw rate, s the steering input as a signed fraction in whole percent
(+ left; for the AI its steering command over full lock), and recomputes the end pose; then the **push-out**; then the commit, with the yaw
rate clamped to ±20π rad/s and the heading reduced by whole turns only once it is 8 turns or
more from zero. The car takes the body's position, height, velocity, heading and yaw rate.

**Push-out**, end pose only, no velocity change: per candidate segment of the box of the end
points 0–6, two passes over the unflagged points. A point beyond the wall whose car edge to
its neighbor (the previous point on the first pass, the next on the second) reaches back to
the track side and crosses the segment's line between its ends moves the body: outside the
segment, so that the wall's end lands on that car edge's line; inside it, perpendicular onto
the wall line when that is at most 0.5 m (the point is then flagged on the wall), else along
the wall ((1 − λ) of the segment on the first pass, −λ on the second). A car pushed partly
through a wall is pushed back whichever way its points started.

**Flags** (kept from step to step, cleared when the car is placed): flagged points are
skipped by every sweep and by the push-out. Each round:

- a point flagged on a **car edge** drops its flag when it has left the edge's span (and is
  skipped) or lies outside the edge line; otherwise both bodies move half-way to put it on the
  line. In both of the last two cases, while the bodies' centers approach through the edge,
  the relative velocity across the edge is removed, half from each body (not mass-weighted).
  A car that keeps pressing on another therefore pushes it instead of bouncing off it on every
  step. A body without car contact (a ghost, or a walls-only step) drops its car flags;
- a point flagged on a **wall** moves on to the neighboring segment once if it slid past an
  end of its segment; the flag drops when the CoG velocity points away from the wall or the
  point is not strictly beyond the line. Otherwise the body moves so the point lies on the
  wall line, the velocity is projected on the tangent of the last point-wall hit a sweep found,
  and the yaw rate gets the angular part of that velocity change at the point. A car that
  keeps pressing in slides along the wall; once a flag drops, a new crossing takes a fresh
  impulse with friction.

**What the race does with the hits.** Every answered hit is reported to the race in the order
the step made it:

- the course-out rule of time trials and license tests (a wall hit whose impulse changes the
  car's speed by more than 10 m/s, §11);
- the pad's hit feedback: the step's strongest hit of the car, a wall hit as |j| / 50000, a
  car hit (on both cars) as 2 × |j| / 15000, at most 1 and ignored below 0.2;
- the hit strength for sounds and rumble: the step's summed |j| / 10⁵ for walls and |j| / 15000
  for cars, at most 1; the hit sound holds a stronger hit for 60 steps (its level decaying by
  0.98 a step otherwise) and the rumble for 24;
- the race screen's wall impulse (the summed |j| of the car's wall hits) and each car-to-car
  contact's point and impulse.

In online races the other players' cars are posed from their owners' states: such a car's
body starts back along its posed motion with no contact flags, takes part in the step, and
keeps its pose afterwards; its owner's machine answers its hits (`docs/dev/ONLINE.md`).

#### Front and rear hit flags

Each step a car's collision body reports which end of it the step's hits were on. The AI's
recovery reads them at the next step's driver input (ai §7). With the octagon points and edges
numbered as above, the sweeps set them for every hit they keep, whether or not that hit is the
one answered:

- **walls**: a body's earliest point-wall hit of a round sets the side of its point (0–3
  front, 4–7 rear); a wall corner on a car edge sets neither; push-outs and held contacts set
  nothing;
- **car to car**: every car hit a sweep keeps sets, on the car whose point hit, that point's
  side, and on the car whose edge was hit, the edge's side (edges 0–2 front, 4–6 rear, the side
  edges 3 and 7 neither).

The flags start clear at every collision step.

## §6 Car parameters from the database and the player's settings

| File | Role |
|---|---|
| `src/sim/exact/spec_build.cpp`, `src/sim/exact/params_build.h`, `params_build.cpp` | a car's equipment, its packed spec and its parameter block, built as the race builds them |
| `src/race/exact_setup.cpp` | which equipment each car of a race gets |
| `src/sim/car_spec.h`, `car_spec.cpp` | the menus' car spec (power, torque, weight, gearing, showroom top speed, drag, ride height), the garage settings, the computed gearbox's ratio generator |
| `src/sim/exact/program_tables.cpp` | the game's small tables the builders read (default curves, surface classes, the static physics bytes) |

Every car of a race gets its parameters when the race starts, as GT3 builds them. Its
**equipment** (the car, one part per parts table and the settings words of §6.6) is packed
into a **spec** of integer bytes (§6.1, §6.2), and the spec is converted into the parameter
block the step reads (SI values, §6.2–§6.5). Both stages use the PS2's own integer and
single-precision arithmetic, so a car's parameters are bit for bit the game's. The step
never reads the database itself.

Where a car's equipment comes from:

- a GT Mode garage car (also a replay's car and an online player's garage car): its parts and
  the player's settings, as they are;
- an opponent or prize car (a car variant, table 31): the base car's parts with the variant's
  non-zero part slots, and the stock settings of those parts clamped into each part's ranges
  (§6.6);
- an arcade car (table 35): its parts and clamped stock settings, then the record's own
  settings bytes (+0xE8 to +0x10D: final drive, gearbox slider, LSD, downforce, dampers,
  camber, toe, anti-roll bars, ride heights, springs, TCS, ASCC and an engine output
  override); the arcade car page's tire choice swaps both tire parts;
- any other database car: its own parts with clamped stock settings.

An electric mod car races on its donor car's stock engine, engine parts, drivetrain and
gearbox: the race has no electric drive yet.

**The menus' numbers** (the car pages' power and torque, the gear chart, the showroom top
speed, the showroom ride height, GT Mode's grid order by power-to-weight and the online
plausibility checks) come from a separate builder over the same records and settings. It
applies the rules below to what it shows, with these differences:

- it fills a missing value with a conventional default: mass at least 300 kg, a front weight
  share of 0.55 kept to 0.3–0.7, idle 900 rpm kept to 500–2500 rpm, a rev limit less than
  1500 rpm above idle replaced by the curve's last point, C_d 0.33, the width × height box
  when the stored frontal area is 0.1 m² or less, extents of 0.3 m or less replaced by half
  the length, a 5-speed gearbox with a final drive of 4.0 when the car has none, and an
  ordinary gearbox's unset (−999) ratio words filled in geometrically;
- it skips a part's factor byte of 0 and caps the engine factors P and Q at 65535 and 255,
  where the race multiplies by 0 and keeps the low bits (§6.2);
- it shows a ride height only for a car whose suspension has a spring and a motion ratio.

A few of GT3's own rules look like quirks. They change how cars drive:

- Both suspension blocks read the **front** ride-height setting (its low byte); the rear
  setting has no effect (§6.6).
- The drivetrain-split setting (word 0x118) is never read: the 4WD share is drivetrain +0x13
  (§7 #59).
- Three spec values are never filled in, so they stay fixed: the shift clutch's torque
  (0 N·m, §2.4), the AWD strength B5 (the default capacities of §7 #59) and the
  steer-coupled camber (none).
- The race's power limit is never set by any race mode, so no car races power-capped.
- The front dampers' rebound stages read the front bound settings (§6.6).
- On a dirt course the car's own TCS and ASCC are removed (§10.4).
- A tire record's base grip is taken at zero slip, with no load term (§6.5): 9.8 for
  every compound on the disc except `medium_evo` (9.8 × 1.02; test
  `params_tire_base_grip_disc`).

### §6.1 Adjustable settings (step, min, max triplets)

Many tuning parts store an adjustable value as three bytes: a step count *n*, a minimum *a*
and a maximum *b*. The player's setting is an index *i* from 1 to *n*. The value is

  v = a  if n < 2,  otherwise  v = a + (b − a)·(i − 1) / (n − 1),

in 32-bit integers; the division truncates toward zero and v is kept as a byte. This gives
the front and rear ABS gains, the damper forces, the anti-roll bar rates, the TCS level and
the four ASCC gains. §6.6 gives each one's bytes and scale.

### §6.2 How installed parts change the parameters

The spec builder copies each part's bytes in a fixed order and scales a few by the part's
integer percentages (one in per mille): x ← ⌊x·f / 100⌋, the product taken in 32 bits and
the result kept to the field's width (8 or 16 bits). A part that is not fitted is skipped; a
fitted part's factor byte of 0 makes the product 0.

**Engine output factors.** Two factors start at P = 1000 (per mille, 16 bits) and Q = 100
(percent, 8 bits). The engine parts act in this order: port polish, engine balance,
displacement, computer, NA tune, turbo kit, **port polish again**, muffler, intercooler.
Port polish runs twice, so its factor applies squared (102 % gives
⌊⌊1000 × 102/100⌋ × 102/100⌋ = 1040 ‰).

| Part | Effect |
|---|---|
| port polish, displacement, computer, muffler, intercooler | P ← P·(+0x11)/100, Q ← Q·(+0x12)/100 |
| engine balance, NA tune | +0x11 added to every torque-curve rpm byte (the curve moves up the rev range); +0x12 added to the rev-limit byte; P ← P·(+0x13)/100, Q ← Q·(+0x14)/100 |
| turbo kit | P ← P·(u16 +0x1C)/100, Q ← Q·(+0x1B)/100; +0x18 added to the rev-limit byte and +0x19 to the up-shift byte; boost cap 1 + 0.1·(+0x11) (the stages come from the settings, §6.6) |

The torque curve (§2.1) is built from the engine record's points (count +0x57, rpm bytes
+0x47 onwards × 100, torque words +0x18 onwards in 0.01 kgf·m) with three scales: the output
factor (engine +0x42 × 10 per mille; an arcade car's settings word 0x1B8 replaces it when
non-zero), Q at the curve's first point and P at its last, interpolated linearly in engine
speed between them. A P, Q or output factor of 0 counts as 100 %. A last torque word of 0
takes the previous point's value. With a turbo, each point is multiplied by
min(B, 1 + cap) / B, B the uncapped boost at the point's rpm (§2.3).

| Parameter | Rule |
|---|---|
| idle | engine +0x44 × 10 rpm |
| clutch bite ("launch") rpm | engine +0x43 × 10 rpm, at least idle + 500 rpm |
| up-shift rpm UP | up-shift byte × 100 rpm (engine +0x46 plus the turbo's adder) |
| rev limit | rev-limit byte × 100 rpm (engine +0x45 plus the adders), or the last curve point's rpm when the byte is 0; at least UP + 200 rpm |
| engine friction | k_f = word × 0.98 / ω_last N·m per rad/s, with the friction word below and ω_last the last curve point in rad/s, so engine braking at the top of the curve is word × 0.98 N·m (§2.2) |

**Drivetrain** (table 13, scaled by the flywheel, clutch and propeller-shaft parts):

| Parameter | Rule |
|---|---|
| drive type | +0x14: 0 FR, 1 FF, 2 4WD, 3 MR, 4 RR (5 and up read as FR) |
| AWD system B3 | +0x15 |
| 4WD share B4 | +0x13 |
| engine inertia J_e | u16 +0x20 × flywheel +0x12 % × clutch +0x12 %, × 0.001 kg·m² |
| engine friction word | u16 +0x16 × flywheel, clutch and shaft +0x11 % each |
| wheel spin inertia I_w, front / rear | s16 +0x18 / +0x1A × flywheel +0x13 / +0x14 % × clutch +0x13 / +0x14 % × shaft +0x12 / +0x13 %, × 0.01 |
| axle inertia, front / rear | s16 +0x1C / +0x1E × shaft +0x14 / +0x15 %, × 0.01 × final drive, plus 2 I_w |
| shift-clutch torque | none: always 0 N·m (§2.4) |

**Differentials** (LSD part, table 21): the type letter at +0x11 (front) and +0x1B (rear)
becomes its index in the string `frnmhvyt`; a letter outside the string gives no
parameters. An undriven axle (§7 #59) is set to `f`; a driven axle whose letter is `f`
becomes `n`. The settings' initial, acceleration and deceleration values I, A, D (§6.6)
convert by type:

| Type | Initial | Accel | Decel |
|---|---|---|---|
| `f`, `r`, `n` | 0 | 0 | 0 |
| `m`, `h`, `y` | I × 9.8 N·m | A × 0.01 | D × 0.01 |
| `v` | I² | A × 0.98 | 0 |
| `t` | I × 0.01 | A × 0.01 | D × 0.01 |

Types `m` and `h` start a race locked (§9.3). Their effect is §2.7.

**Body and chassis geometry.**

- Collision box: the body record's floats at +0x20 / +0x24 are the front and rear extent
  (mm, from the wheelbase midpoint), +0x28 / +0x2C the width and height, each truncated to
  whole millimeters. A zero extent means half the chassis length (+0x14, whole mm). The race
  measures the extents from the center of gravity: front − L·(w_f − 0.5) and rear +
  L·(w_f − 0.5), with L the wheelbase and w_f the front weight share. They give the
  collision octagon (§5.3) and the grid placement (§9.2).
- Half-track per axle: (tread (body +0x32 / +0x34) − 2 × wheel offset (signed +0x3D /
  +0x3E)) / 2, in mm. The mean track used by the load transfer (§4.2) is
  2·(h_f·w_f + h_r·(1 − w_f)) for half-tracks h_f, h_r.

**Brakes** (table 0): front / rear maximum brake torque per wheel = +0x11 / +0x12 × 98 N·m;
the handbrake adds +0x13 × 98 N·m on each rear wheel.

**Aerodynamics** (body record): frontal area A = the float at +0x44, truncated to
0.0001 m², or the collision box's width × height when that is 0. The drag coefficient C_d is
+0x39 / 100, and the air density ρ is the game's 1.2. With A_f = ½·ρ·A:

  C_front = A_f · (front downforce setting %)/100,  C_rear = A_f · (rear setting %)/100,
  C_drag = A_f · C_d + 0.1·(C_front + C_rear).

The stock R34 GT-R V-spec gets C_drag 0.511, C_front 0.348 and C_rear 0.564
(`car_spec_r34_gtr`). How these coefficients turn
into forces is in §4.7 and §5.2.

**Axle grip factors** (§3.1): ((chassis +0x12 / +0x1E + 124) mod 256 + 900) / 1000 for the
front / rear axle. A CPU car's are also multiplied by the event's percentages for its
driveline class (§7 #21): front event +0x19C + class, rear +0x1A0 + class (0 = 100 %).

### §6.3 Derived values and defaults

Besides the rules of §6.2–§6.6, the spec builder and the converter fill in or derive:

- a collision extent of 0: half the chassis length (§6.2);
- the pitch and roll inertias from the yaw byte, and their defaults (§6.4);
- the dirt lock curve: the game's default curve when the car's last speed point is 0
  (§6.4);
- a tire block's slip-angle range of 0: 45°, and the empty surface bytes (§6.5);
- a TCS level of 100: TCS gain 0 (§6.6);
- each gear's speed factor (engine rad/s per m/s: the gear's total ratio over the driven
  tire radius, front for FF and 4WD system 0, rear otherwise);
- the automatic's down-shift rpm for gears 2..N (§2.6): for gear g + 1, the rpm it runs at
  right after an up-shift from gear g at UP, less its rpm at 10 km/h (each truncated), at
  least the launch rpm + 500;
- each wheel's static load: half its axle's static sprung load plus its unsprung weight
  (§6.4, §9.3);
- a braking coefficient for the AI: mass × front tire radius / (4 × front brake torque)
  (ai §4.3);
- the car's **top speed estimate**: from 0 km/h to the rev limit in top gear, in steps of
  (that speed in km/h) / 500 + 1 km/h, shifting up once past UP, the last speed at which the
  full-throttle drive force (with the engine condition factor, §10.3) beats the tarmac
  rolling resistance on the weight plus downforce and the drag. The AI takes it as its
  straight-line target (ai §4.2). The menus' showroom top speed is the same walk without the
  engine condition (menus §9.3).

### §6.4 Mass, inertia, steering and camber data

**Mass.** With W = chassis weight (kg, +0x1A), l = the lightweight part's u16 +0x12 (‰,
only when one is fitted) and k = body u16 +0x30 (‰):

  m = ⌊⌊W·l / 1000⌋·k / 1000⌋ + 120 kg,

the 120 kg being two of the game's built-in values (65 + 55). The R34 GT-R V-spec comes out at
1560 + 120 = 1680 kg (`car_spec_r34_gtr`). The sprung mass is m less twice the front and rear unsprung masses
(suspension +0x5C / +0x5D ÷ 4 kg per wheel); the front axle's static sprung load is the
sprung weight × w_f, the rear's × (1 − w_f).

**Inertia.** With c = chassis +0x1C, the lightweight part's +0x11 (%, when fitted) and the
body's +0x38 (%), the inertia byte is y = ⌊⌊c·lw% / 100⌋·body% / 100⌋, kept to 8 bits. Then

  I_yaw = 100·y,  I_pitch = 100·y,  I_roll = 100·⌊y / 2⌋,

with 6000, 4000 and 2000 when the result is 0. These are game units.
The planar step divides the yaw torque by I_yaw (§4.1); the attitude step's gains are each
lever × 2π / I_pitch or I_roll, the levers being a and b for pitch and the half-tracks for
roll (§4.3); the collision body uses I_yaw (§5.3).

**Other chassis values:** front weight share w_f = chassis +0x10 / 100; wheelbase +0x18 mm;
length +0x14 mm. The center-of-gravity height parameter is ½·(chassis +0x1D mm + front ride
height) (§4.2). A car at rest stands with its reference point the ride height above the
ground (§9.2).

**Steering.** Full lock = steer part +0x11 degrees. The steer part also
holds three six-point curves of the lock limit against forward speed, speeds in km/h and
limits × 0.2°; §4.4 says which one applies when:

| Curve | Speeds | Limits |
|---|---|---|
| pad | +0x1E..+0x23 | +0x24..+0x29 |
| steering wheel | +0x12..+0x17 | +0x18..+0x1D |
| dirt | +0x2A..+0x2F | +0x30..+0x35 |

When the dirt curve's last speed is 0, the game's default curve replaces it: 51° at
0 km/h, 40.4° at 32, 27.6° at 76, 18.8° at 112, 12.4° at 166 and 9.4° at 255 km/h. The front
wheels' Ackermann shares are atan2(L, L − h_f) / (π/4) and atan2(L, L + h_f) / (π/4) (§4.4).

**Camber curves** (§3.1): per axle, four angle codes a (suspension +0x60.. front, +0x68..
rear) and four factor codes m (+0x64.. front, +0x6C.. rear) give the points
(angle (128 − a)·4°/32, factor m/200), taken in reverse order. The camber offset comes from
the settings (§6.6). The drawn wheels also change camber with suspension travel (suspension
+0x5E / +0x5F degrees per meter) and swing on an arm (body +0x3F / +0x40 × 0.01 m); this is
visual only.

### §6.5 Tire records

Each axle's tire part (tables 28 / 29) points to a tire size and to three pairs of a
compound record (table 27) and a force-volume record (table 26): compound slots 0, 1 and 2.
The race builds a record per slot for the front tire and for the rear tire, and each wheel
uses its axle's record for the class of the surface under it:

| Surface index (§3.6) | Slot |
|---|---|
| 0, 1, 7–11 (tarmac, curb, paving, ...) | 0 |
| 2–5 (grass, gravel, sand, dirt) | 1 |
| 6, 12–15 (6 and the wet surfaces) | 2 |

**Tire size** (table 25): rim inches +0x08, aspect code +0x09 (× 5 %), width code +0x0A
(× 5 mm). The section width is W = 5 × width code mm (a rear width code of 0 reads as
10 mm); the radius is rim × 0.0254 / 2 + W × aspect code × 0.05.

**Record contents** (curve offsets are in the compound record):

| Field | Source |
|---|---|
| base μ | compound +0x08 / 100 (1.00 if 0) |
| slip-angle range θ | compound +0x09 degrees (45° if 0), at most 90° |
| A: lateral force fraction vs. \|slip angle\| | 8 points: x = (+0x12..+0x19) × θ/255, y = (+0x1A..+0x21)/200 |
| C: longitudinal force fraction vs. slip ratio, D: friction-circle limit | one 11-point slip axis from −1 to 1. Wheel-spin side: x = −(+0x22..+0x27)/255, C = (+0x28..)/200, D = (+0x2E..)/200. Braking side: x = (+0x34..+0x39)/255, C = (+0x3A..)/200, D = (+0x40..)/200. The two sides share the point at 0, where C = 0 and D takes the braking side's value |
| B: force along the patch velocity vs. slip angle | 6 points: x = (+0x46..+0x4B) × 90°/255, y = (+0x4C..+0x51)/200 |
| L: load sensitivity | 4 points: x = (+0x0A..+0x0D) × 196 N per meter of section width, y = (+0x0E..+0x11)/200 |
| μ[s] per surface | μ × force-volume byte % (0 = 100 %): +0x08..+0x11 for surfaces 0–9, +0x12..+0x14 for surfaces 12–14 |
| wear rate | compound +0x52 / 100 (§8.2) |

The first point of curves A, B and C (both sides) is (0, 0), and L's first x is 0; D and L
keep their first y.

Empty force-volume bytes: in the front tire's slot-0 block, surfaces 12 / 13 / 14 take the
bytes of surfaces 0 / 1 / 9; then in every block, front and rear, any zero surface byte
takes the front slot-0 block's. Entry 11 of the μ table is not a surface: it holds
min(1, 1.25 / μ), the floor of the wear grip factor (§8.4).

Derived from the curves:

- the **optimum slips**: going outward from 0 on each side of curve C, the first maximum.
  The wheel-spin side gives the drive optimum (the TCS target and the shift veto), the
  braking side the brake optimum (the ABS target, §4.5); the braking side's maximum C is
  kept for the AI's braking (1 when C never rises above 0);
- the **peak slip angle**: the whole degree from 0 to 45 where curve A peaks (45° when A
  never rises above 0); the AI's steering limit (ai §6.1);
- the **base grip**: 9.8 × D(0), the friction-circle curve at zero slip. The steering
  indicators, the lateral g / μ and the AI's corner speeds use it (§4.1, §4.6, ai §4.2).
  It depends only on the compound, the same on every car: on this disc it is 9.8 for
  every compound except `medium_evo`, whose D(0) is 1.02, so 9.8 × 1.02 (17 compounds
  over 5717 builds, every car on every tire part it has; test `params_tire_base_grip_disc`).

The tire force model itself is §3.

### §6.6 The player's settings

The settings words of a garage car (offsets 0xF0 to 0x1C8 of the saved car record) go to
the race as raw values. The stock settings are the installed parts' defaults; for a car
that is not a garage car GT3 clamps them into each part's ranges: the final drive into the
gear record's [+0x26, +0x24], the gearbox slider into [+0x2A, +0x2B] (a computed gearbox is
then regenerated, §6.7), the drivetrain split, downforce, camber, ride heights, springs and
LSD values into their parts' minimum and maximum, and the brake controller, damper,
anti-roll bar, TCS and ASCC indices into 1..steps. Toe, motion ratio and the turbo bytes
are taken as they are. GT Mode's Settings pages edit a copy of a garage car's words.

| Words | Setting | Stock value from | Conversion |
|---|---|---|---|
| 0xF0, 0xF4..0x10C | reverse, gears 1-7 (× 1000) | gear +0x12, +0x14.. | ratio = word × 0.001 (reverse: the same, driving backward); gears past the box's count (gear +0x11) are not used |
| 0x110 | final drive (× 1000) | gear +0x22 | × 0.001 |
| 0x114 | gearbox slider (design top speed / 10 km/h) | gear +0x29 | the generator's input (§6.7); the race does not read it |
| 0x118 | drivetrain split | drivetrain +0x13 | not read |
| 0x11C / 0x120 | brake controller index, front / rear | brake controller +0x14 | ABS gain = §6.1 value × 0.1 (front: steps +0x11, min +0x12, max +0x13; rear: +0x15, +0x16, +0x17) |
| 0x124 / 0x128 | downforce front / rear, % | body +0x13 / +0x3C | C_front / C_rear = A_f × % (§6.2) |
| 0x12C..0x140 | turbo: stage-0 peak, knee, rate; stage-1 peak, knee, rate | turbo +0x12..+0x17 | peak × 0.1, knee × 100 rpm, rate × 0.1 /s. The turbo is on if the cap and stage 0's three values are all non-zero; stage 1 is active if its three values are (§2.3) |
| 0x144 / 0x148 | camber, front / rear | suspension +0x13 / +0x16 | camber offset −setting × 0.1° (§3.1) |
| 0x14C / 0x150 | ride height, mm | suspension u16 +0x54 / +0x5A | q = **front** value (low byte) × 0.001 m on both axles |
| 0x154 / 0x158 | toe (128 = neutral) | suspension +0x19 / +0x1C | toe angle atan2((byte − 128) × 0.0005, r) for tire radius r (§4.4) |
| 0x15C / 0x160 | spring, 0.1 kgf/mm | suspension +0x23 / +0x26 | k = spring × 980 × c / 100 N/m |
| 0x164 / 0x168 | motion ratio c, % | suspension +0x27 / +0x28 | scales k and the damper reference speeds |
| 0x16C..0x188 | damper indices: front bound 1, 2, rebound 1, 2; rear the same | suspension +0x2E, +0x31, +0x35, +0x38, +0x3C, +0x3F, +0x43, +0x46 | force at each reference speed = §6.1 value × 49 N; reference speed = byte × 0.01 × 100/c m/s |
| 0x18C / 0x190 | anti-roll bar index | suspension +0x4A / +0x4E | gain = §6.1 value × 98 N/m² |
| 0x194..0x1A8 | LSD initial front, rear; accel front, rear; decel front, rear | LSD +0x12, +0x1C, +0x15, +0x1F, +0x18, +0x22 | by type (§6.2) |
| 0x1B0 | TCS index | TCSC +0x11 | see below |
| 0x1B8 | engine output override (arcade cars) | none (0) | replaces the output factor when non-zero (§6.2) |
| 0x1BC..0x1C8 | ASCC indices A, D, C, E | ASCC +0x12, +0x1B, +0x21, +0x26 | see below |

The **front** rebound stages reuse the front bound indices (0x16C for stage 1, 0x170 for
stage 2), so the front words 0x174 / 0x178 have no effect. The rear uses all four of its
words. The other suspension values come from the suspension part, not from the settings
(§4.2 says what they do):

| Value | Front / rear |
|---|---|
| progressive (bump-stop) rate | +0x29 / +0x2A × 98 000 N/m² |
| unsprung mass per wheel | +0x5C / +0x5D ÷ 4 kg |
| bump room, droop | +0x1D / +0x1E, +0x1F / +0x20 mm |
| knee | +0x78 / +0x79 mm |
| extra damping below the knee | +0x7A / +0x7B × 98 |
| damper reference speeds (bound 1, 2; rebound 1, 2) | +0x70..+0x73 / +0x74..+0x77 |

**Traction control** (TCSC part, table 22). The level is the §6.1 value (steps +0x12, min
+0x15, max +0x16, index 0x1B0), in percent:

- TCS is active unless the level is 100 (its gain is then 0);
- slip scale = level / 100 (1 if the level is 0 or 100);
- gain = +0x13 × 0.1;
- lateral terms a = +0x14 × 0.1, b = +0x17 × 0.01.

**Stability control** (ASCC part, table 23):

- gains A = §6.1(+0x11, +0x14, +0x15) / 100, D = §6.1(+0x18, +0x19, +0x1A) / 100,
  C = §6.1(+0x1E, +0x1F, +0x20) / 100, E = §6.1(+0x23, +0x24, +0x25) / 100 (steps, min,
  max);
- thresholds A = +0x13 / 100, C = +0x22 / 100;
- enable flags: a byte made from the low two bits of +0x17, +0x16, +0x1D and +0x1C (in that
  order, from bit 0). Its bits 0-7 are A-front, A-rear, D-front, D-rear, C-front, C-rear,
  E-front, E-rear. A channel is on when it has a wheel and a non-zero gain.

The player's ASM and TCS switches (where the game offers them: arcade single races, free
runs, the two-player battle) clear the ASCC flags and the TCS gain when off; a dirt course
clears both blocks for every car (§10.4). How these values act is §4.5 and §4.6.

As an example, the stock R34 GT-R V-spec's settings include front / rear springs 40 / 50
(0.1 kgf/mm), a front motion ratio of 95 %, a 130 mm ride height, downforce 29 / 47 % and a
final drive of 3.545 (`car_spec_settings`).

### §6.7 Computed (adjustable racing) gearboxes

A gear record with +0x28 non-zero describes a computed gearbox: its ratio words are
generated from the gearbox slider *s* (design top speed / 10 km/h) instead of being stored.
The generator works with:

- n = forward gear count (gear +0x11);
- the rev byte R_b = engine +0x45 + engine balance +0x12 + NA tune +0x12 + turbo +0x18;
- the final-drive word F (setting 0x110).

The tire used is the **driven** one: front for FF and for 4WD system 0, rear otherwise. Its
rolling radius is

  r = rim × 0.0254 / 2 + W × aspect code × 0.05,

where W = 5 × width code mm. A width code of 0 gives no sidewall on the front path and
10 mm on the rear one.

The steps:

1. t = (10s − 150) / 250; design speed v = 10s km/h in m/s.
2. Top-gear ratio g = (100·R_b rpm in rad/s) · r / v / (F × 0.001). This puts the rev limit
   at the design speed in top gear.
3. Step factors from the table below (b₀..b₅, chosen by n, clamped to 5-7):
   q(h, l) = 200/b_l + t·(200/b_h − 200/b_l); q₁ = q(0, 1), q₂ = q(2, 3), q₃ = q(4, 5).

   | n | b₀ | b₁ | b₂ | b₃ | b₄ | b₅ |
   |---|---|---|---|---|---|---|
   | 5 | 155 | 165 | 188 | 190 | 190 | 200 |
   | 6 | 165 | 175 | 190 | 192 | 190 | 200 |
   | 7 | 170 | 180 | 192 | 194 | 190 | 200 |

4. From gear n down to gear 1: gear 1 is first multiplied by q₃. The gear's word is the
   truncated value of 1000·g, kept to 16 bits. Then g ← g·q₁ and q₁ ← q₁·q₂. Each lower gear
   therefore has a larger ratio than the one above it, by a growing step.
5. Reverse word = (3 × gear-1 word + gear-2 word) / 4, in integers.

The arithmetic is the PS2's single precision; the final drive is converted in double
precision and rounded once, and the conversion to an integer truncates and saturates, so
the words are bit for bit the game's.

When the generator runs: for a computed gearbox's stock settings (at the stock slider and
final drive, clamped as in §6.6), and when the player moves the slider in GT Mode's
Settings (at the current final drive; the words are then kept within ranges around the
reference ratios). The race itself never runs it: it uses the gear words of the car's
settings, and so do the menus' figures. Check value: the zz2 car's first gear is 3.194 at
its stock slider, and raising the slider by 3 makes its top gear taller.

## §7 Numbered reference rules

A few small rules are cited by number (#n) because several parts of the game share them.

**#12, TCS lateral factor.** For the lateral input x (lateral g over the mean surface μ,
§4.6) and the TCS terms a, b (§6.6):

  f = 1 if |x| ≤ b, otherwise max(0, 1 − (|x| − b)·a).

**#21, driveline class**: FF → 1; 4WD with system 0 → 1, system 3 → 3, any other system
→ 2; FR, MR, RR → 0. The race uses the class to choose the course's AI parameter set and an
event's per-class AI overrides (ai §9.1, §9.2) and a CPU car's per-class grip percentages
(§6.2).

**#24, gear for a road speed.** For speed v (m/s), up-shift rpm UP and the gears' speed
factors (§6.3):

- 0 if v < 0;
- otherwise the lowest gear k from 1 to N − 1 in which v is below the speed of UP − 500 rpm;
- otherwise N.

The moving start (§9.3), the manual's change out of reverse (§2.6), the AI's recommended
gear (ai §16) and the automatic's kick-down (§2.6) use it.

**#59, driveline layout.** This gives the layout code L, the driven axle, the coupling k and
the center-clutch capacity used by the driveline (§2.7). B4 = drivetrain +0x13; B5 is the
AWD strength, a spec byte GT3's builder never fills, so it is always 0 and the defaults
below apply.

| Drive / AWD system | L | Driven axle | k | Capacity |
|---|---|---|---|---|
| FR, MR, RR | 0 | rear | – | – |
| FF | 1 | front | – | – |
| 4WD, system 0 | 2 | front | B4 × 0.01 | – |
| 4WD, system 1 | 3 | both (mean; the rear tire for the gear speeds) | B4 × 0.01 | – |
| 4WD, system 2 | 4 | rear | 1 if B4 ≥ 50, else B4 × 0.01 | (B5 or 8) × 98 N·m |
| 4WD, system 3 | 5 | rear | B4 × 0.01 × 0.36 | (B5 or 15) × 98 N·m |
| 4WD, system 4 | 6 | rear | 1 / (B4 degrees in radians), 0 if B4 = 0 | (B5 or 15) × 98 N·m |
| 4WD, system 7 | 7 | rear | B4 × 0.01 | – |
| 4WD, systems 5, 6 or unknown | 0 | rear | – | – |

## §8 Tire wear, tire smoke and brake heat

| File | Role |
|---|---|
| `src/sim/exact/tick_prep.cpp`, `tick_wear.cpp`, `tick_effects.cpp` | wear grip; brake heat, wear, its flags and the flap pendulums; slip intensity, skid pitch, smoke emission and dirt |
| `src/sim/exact/race_inputs.cpp` | the race's wear curve; the tire effect curves and brake heat constants from the game's built-in tables |
| `src/sim/exact/pit_lane.h`, `tick_pit.cpp` | the pit lane and the stop |

Every car has, per wheel, a wear value w (in wear units), a grip factor and a gauge level,
and the race computes, per wheel, three tire effect values (slip intensity, skid pitch and
smoke emission), the brake heat and a flap pendulum. In the race step (§1.2):

- **phase 2**: each wheel's wear grip factor (§8.4), while wear is on and the car is off the
  pit surface; it multiplies the wheel's peak force (§3.1);
- **phase 8**: the brake heat (§8.5);
- **phase 12**: the slip intensity (§8.3), smoke emission (§8.3b), skid pitch (§8.3c) and the
  car's dirt;
- **phase 13**: the wear increment (§8.2), the tires-worn flag and pit request (§8.7) and
  the flap pendulums (§8.8).

Tire wear exists only in races that turn it on (§8.1). With wear off, w stays 0 and the
grip factor stays 1. Every car starts a run on a fresh set (§8.4); there is no respawn
(§11).

### §8.1 Wear parameters of a race

The wear curve comes from seven event bytes, 0x1BD-0x1C3. Two more values are fixed: the
first knee is always 4 and its loss always 0.

| Byte | Parameter | Unit | Battle's tire damage |
|---|---|---|---|
| 0x1BD | maximum wear w_max | × 10000 | 21 → 210000 |
| 0x1BE | grip loss at w_max | % | 30 → 0.30 |
| 0x1BF | "worn" margin; threshold = (100 − byte) / 100 | % | 9 → 0.91 |
| 0x1C0 | fresh set: w_fresh = −byte × 10000 | × 10000 | 2 → −20000 |
| 0x1C1 | grip loss of a fresh set | % | 6 → 0.06 |
| (fixed) | knee 1 | × 10000 | 4 → 40000 |
| (fixed) | loss at knee 1 | % | 0 |
| 0x1C2 | knee 2 | × 10000 | 15 → 150000 |
| 0x1C3 | loss at knee 2 | % | 5 → 0.05 |

The slip-intensity cap of §8.2 is 1 (a built-in value). Wear is **off** when:

- the race mode does not enable it;
- the maximum byte is 0;
- or the ordering check fails: knee 2 < maximum, loss 2 < maximum loss, knee 1 < knee 2,
  loss 1 < loss 2, and margin < maximum loss must all hold.

Where wear is on, it applies to every car of the field:

- single races and GT Mode's free practice: the event's bytes, never on dirt courses (course
  category 2);
- the split-screen battle with the "tire damage" option: the curve in the last column, never
  on dirt. NTSC-J's rules also have a slower battle curve (maximum 42, knee 1 8, knee 2 30,
  the rest the same); the option here selects the standard one under every release's rules;
- online battles, time trials, license tests, free runs and machine tests have no wear.

### §8.2 Wear accumulation

For each wheel, each step:

  Δw = min(cap, S) · k · max(0, 0.4·F_z / b − 1960) · Δt,

where:

- S = the slip intensity (§8.3);
- k = the compound's wear rate (§6.5);
- F_z = the tire load in N;
- b = the axle's section width in meters;
- Δt = the car's step length (§1.1).

The 0.4 and 1960 N come from two of the game's load knees (20 and 60 × 98 N). The 1960
offset means a lightly loaded tire does not wear even when it slides. A wheel adds no wear
if it has no load or stands on the curb surface (index 1; the wet curb wears), and no wheel
wears while the car is on the pit surface. Wear counts up from w_fresh; the next step's grip
factor clamps it at w_max (§8.4).

### §8.3 Slip intensity

The slip intensity S of a wheel drives the wear and the volume of the tire sounds (sound
§7.6). It is computed from:

- the wheel's lateral speed (the hub velocity across its heading, m/s);
- its slip ratio (§3.4);
- its rim speed |ω·r|;
- its load F_z;
- a speed ramp ρ = min(1, 0.02·|hub speed|).

**Curbs** (surface 1 and the wet curb 13):

  S = clamp((|ω·r| − 1.389)·0.144, 0, 1) · clamp(F_z / 2450 − 0.25, 0, 1) · 0.5.

**Every other surface** uses one of three curve classes. Class 1 is surfaces 2-5 (grass,
gravel, sand, dirt). Class 2 is surface 6 and the wet surfaces 12-14. Class 0 is everything
else. With a = A_c(|lateral speed|) and b = B_c(slip ratio), both piecewise linear and
clamped at the ends:

  m = √(a² + b²);  m ← m + (ρ − ρ₀)·g if ρ > ρ₀;  S = m + o if m ≠ 0, else 0.

| Class | A_c points (lateral m/s → value) | B_c points (slip ratio → value) | ρ₀ | g | o |
|---|---|---|---|---|---|
| 0 | 1.111→0, 9.556→1.192, 12.22→1.6, 15→2 | −1→2.388, −0.580→2.388, −0.314→1.176, −0.0353→0, 0.0353→0, 0.314→0.784, 0.580→1.592, 1→1.592 | 0.01667 | 0 | 0 |
| 1 | 1.556→0, 3.333→0.0627, 13.11→0.3639, 14.11→0.4 | −0.502→0.2, −0.439→0.1757, −0.1255→0.0251, −0.0471→0, 0.0471→0, 0.1098→0.0408, 0.2353→0.1757, 0.2667→0.2 | 0.005556 | 0.5455 | 0.07 |
| 2 | 1.556→0, 3.333→0.0471, 13.11→0.2729, 14.11→0.3 | −0.502→0.4, −0.439→0.3514, −0.1255→0.0502, −0.0471→0, 0.0471→0, 0.1098→0.0612, 0.2353→0.2635, 0.2667→0.3 | 0.05556 | 0.24 | 0 |

On tarmac, S reaches about 1 at 8.2 m/s of sideways speed, or at a slip ratio of +0.385
(locking) or −0.272 (wheelspin). A tire that grips stays at 0. The curves are the game's
(its tire effect block, with ρ₀ and g from speeds in km/h).

**Unloaded wheels.** A wheel with load keeps a counter at 6 steps; without load it counts
down, and the three effect values (S, the smoke emission and the skid pitch) are still
computed from the wheel's motion until the counter reaches 0, 5 steps later. From then on S
and the smoke emission are 0 and the skid pitch is 1. A ghost car's effects are cleared every step: it makes no smoke and no tire
sounds.

### §8.3b Smoke emission

A third set of curves gives the **smoke-emission intensity** E from 0 to 1. Tire smoke, dust
puffs and the car's dirt read E, not S. The structure is the same as §8.3, except that:

- the second input is the signed slip speed sgn(hub)·(hub − rim) in m/s (positive = the
  wheel turns slower than the road, as when braking), hub being the hub speed along the
  wheel's heading;
- curbs have no special rule (surface 1 is class 0, 13 is class 2);
- the result is E = min(1, m + o), with o added only when m ≠ 0.

| Class | A_c points (lateral m/s) | B_c points (slip speed m/s) | ρ₀ | g | o |
|---|---|---|---|---|---|
| 0 | 11.56→0, 12.89→0.047, 13.78→0.151, 16.33→0.178 | −10→0.1, −8.89→0.088, −8.0→0.0125, −7.56→0, 8.0→0, 8.44→0.0204, 9.33→0.088, 10.44→0.1 | 0 | 0 | 0.3 |
| 1 | 3.11→0, 6.67→0.078, 26.22→0.455, 28.33→0.5 | −7.11→0.5, −6.22→0.439, −1.78→0.063, −0.67→0, 0.67→0, 1.56→0.041, 3.33→0.176, 3.78→0.2 | 0.00556 | 0.727 | 0 |
| 2 | 5.33→0, 9.78→0.016, 26.22→0.091, 28.33→0.1 | −9.11→0.1, −7.56→0.088, −6.0→0.0125, −4.67→0, 3.11→0, 4.22→0.0204, 5.56→0.091, 7.78→0.1 | 0.0556 | 0.2 | 0.2 |

Tarmac therefore smokes only beyond about 7.6 m/s of wheelspin, 8 m/s of lock-up or
11.6 m/s of sideways speed, and then at roughly 0.3-0.5. Loose surfaces throw dust from low
speeds up (the speed ramp alone makes m non-zero). Readers:

- the race's tire smoke: the race screen draws a puff from each loaded wheel with
  E > 0.001 (`src/race/tyre_smoke.h`). The race also runs the game's own particle emitter
  (a particle where the surface's effect is on and E > 0.001, drawing from the race's random
  sequence, §8.8), but the screen does not draw those particles;
- the car's **dirt** level: each step it grows by 0.001 × Δt × the sum of E over the wheels
  on loose and wet surfaces (classes 1 and 2), eight times that on a tarmac course (course
  category 0), up to 0.8. It dims the player's car's reflections (not on the wet Route 5
  courses); in GT Mode it is the garage car's dirt, which the run starts with and hands back.

### §8.3c The tire sounds' pitch factor

A second curve set, computed beside S from the same inputs, gives each wheel's **skid
pitch** P, the pitch factor of its tire sound loops (sound §7.6). The load does not enter.

- Curbs (surfaces 1 and 13): P rises linearly with the rim speed |ω·r| from 0.5 at
  2.78 m/s (10 km/h) to 4.3 at 55.6 m/s (200 km/h), clamped outside.
- Every other surface, by the classes of S: P = o + √(a² + b²), plus (ρ − ρ₀)·g when
  ρ > ρ₀. Unlike S, the offset o is added even when the curves give 0.

| Class | A_c points (lateral m/s → value) | B_c points (slip ratio → value) | ρ₀ | g | o |
|---|---|---|---|---|---|
| 0 | 0.444→0, 3.333→0.1098, 6.0→0.2275, 9.222→0.3510 | −1→0.4612, −0.6588→0.2886, −0.2510→0.1129, −0.0157→0, 0→0, 0.2039→0.0878, 0.7216→0.3263, 1→0.4612 | 0.01667 | 0 | 0.75 |
| 1 | 1.111→0, 6.889→0.4706, 18.0→1.5373, 28.33→1.5922 | −0.3451→0.8, −0.2980→0.7027, −0.1098→0.1004, −0.0471→0, 0.0471→0, 0.0784→0.1631, 0.1882→0.7027, 0.2824→0.8 | 0.05556 | 0.2 | 0.5 |
| 2 | 1.556→0, 5.111→0.1506, 11.78→0.3012, 28.33→0.2933 | −0.502→0.3, −0.439→0.2635, −0.1255→0.0376, −0.0471→0, 0.0471→0, 0.1098→0.0612, 0.2353→0.2635, 0.2667→0.3 | 0.1667 | 0.3857 | 0.5 |

So a gripping tire on tarmac idles at 0.75 (above ρ₀ the class-0 slope g is 0), and the
pitch rises with sliding (above 1 from about 9 m/s sideways); loose and wet surfaces idle at
0.5 at rest. A car placed on the track starts at P = 1 on every wheel, and an unloaded wheel
returns to 1 as §8.3 says.

### §8.4 Grip factor, gauge level and a fresh set

Each step the wheel's wear w maps to a grip factor G and a gauge level from −63 to 127. The
curve is piecewise linear. Wear beyond w_max is clamped back to w_max.

| Wear range | G | Gauge level |
|---|---|---|
| w < w_fresh (never reached) | 1 | −128 |
| w_fresh ≤ w < 0 | 1 − loss_fresh · w / w_fresh | −trunc(63.9 · w / w_fresh) |
| 0 ≤ w < knee 1 | 1 − loss₁ · w / knee₁ | trunc(31.9 · w / knee₁) |
| knee 1 ≤ w < knee 2 | linear from 1 − loss₁ to 1 − loss₂ | 32 + trunc(31.9 f) |
| knee 2 ≤ w < w_max | linear from 1 − loss₂ to 1 − loss_max | 64 + trunc(63.9 f) |
| w ≥ w_max | 1 − loss_max | 127 |

Here f is the fraction of the way through the range. The factor never drops below the
floor min(1, 1.25 / μ) of the tire record the wheel is on (§6.5). Compounds with μ ≤ 1.25
therefore never lose grip to wear, and a μ 1.5 tire stops at 0.833 instead of 0.70.

With the battle's curve (§8.1):

| w | −20000 | −10000 | 0 | 40000 | 100000 | 150000 | 180000 | 210000 |
|---|---|---|---|---|---|---|---|---|
| G (before the floor) | 0.94 | 0.97 | 1.00 | 1.00 | 0.973 | 0.95 | 0.825 | 0.70 |
| level | −63 | −31 | 0 | 32 | 49 | 64 | 95 | 127 |

So a new set is slightly slow until it is scrubbed in (w reaches 0), is at its best up to
knee 1, then fades, slowly at first and then quickly. The HUD's tire gauge colors each tire
by its level.

The factor is computed and applied only while the car is **off** the pit surface. On the
pit surface the peak force is not reduced, and the factor and level keep their last values.

A **fresh set** sets w = w_fresh, G = 1 − loss_fresh (0.94 with the battle's curve), level
−63 (0 when w_fresh is 0) and brake heat 0; with wear off it sets w = 0, G = 1 and level 0. Every car gets one at
the start of a run (§9.3) and at a pit stop (§8.6).

### §8.5 Brake heat

Brake-disc heat y, from 0 to 1, is computed for every wheel of every car (phase 8):

  y ← clamp(y + Δt·(T·p·|ω r| / 68600 − (|ω r| + 27.78)·0.005·(y + 0.05)), 0, 1),

where:

- T = the axle's maximum brake torque × tire radius;
- p = the wheel's brake pressure after the aids and ABS (§4.5, §4.6);
- |ω r| = the rim speed.

The constants are the game's (heating 1 / (7 × 9800), cooling 0.005, a speed offset of
100 km/h and a heat offset of 0.05). Hard braking from speed heats the discs; the cooling
term grows with speed and never quite stops, so the discs cool off after braking. No driving
rule reads it.

### §8.6 Pit lane and pit stops

**Pit surface.** A wheel whose ground triangle has flag bit 0 set (bit 8 of the raw surface
value) is on the pit lane. In the race modes with pit stops (single races, battles, free
practice, machine tests) a car with any such wheel is "on the pit surface" for that step.
The effects are:

- no wear accumulates (§8.2) and the wear grip factor is not applied (§8.4);
- two cars see each other for the slipstream and the AI's traffic only when both are on
  the pit surface or both are off it (§5.1, ai §14), and a car on it gets no rubber-band
  speed-up of its own (ai §9.3);
- the pit lane's sequence uses it (ai §13).

Car-to-car contact ignores the flag (§5.3). In time trials and license tests the pit lane is
a course-out instead (§11).

**The pit lane** is the race's: a player's car that drives onto the pit surface past the
pit lane's entry, and a CPU car with a pit request (§8.7), are driven by the race's AI from
then on (driver kinds 2, 3 and 4, ai §1, §13), along the pit lane to the car's box. 25 m
before the box the car drives through if its race is over, if it is at 100 km/h or faster,
or if there is nothing to service: with wear off the tires are always fresh, so a stop
happens only with wear on (and at every stop in free practice). The AI hands a player's car
back at the pit exit.

**The stop.** Crossing its box with something to service, the car is stopped dead: the
velocities and the dynamic states are cleared as at a standing start (§9.3: wheel speeds and
slips, engine speed, clutch open, 1st gear, differential, turbo and steering states), and a
300-step counter starts (5 s). While it runs:

- the car is held in the scripted pose of §4.3: lifted to 0.3 m over the first 30 steps, its
  heading turning toward the box's heading at up to 30°/s, and let down onto the ground for
  the last 30 steps;
- every step zeroes the planar velocity, the yaw rate and the longitudinal force sum, holds
  1st gear, ignores shift and reverse requests and keeps the clutch request at 0 (open): a
  player can rev the engine, and a CPU car blips it (ai §10);
- 60 steps before the end (1 s) the car gets a fresh set of tires (§8.4).

After the counter the AI drives the car off; 0.5 m past the box it is leaving the pits and
its tires-worn flag is cleared. There is no fuel.

**Driver kinds and the vehicle.** While the race's AI drives a car (a CPU car and the
autopilot's car always; a player's car in a driver kind other than 0 outside a hold), the
car uses the automatic
gearbox, the AI's clutch rule, its driver's aid block instead of its own, with ASCC enabled,
and the AI's oversteer indicator (§2.4, §2.6, §4.6; ai §1). The kinds:

| Kind | Meaning |
|---|---|
| 0 | the car's own driver: the pad, or the CPU car's AI |
| 1 | the race's AI drives on (a player's run-up, after the run, a take-over) |
| 2, 3, 4 | the pit lane, the stop line, leaving the pits |
| 5, 6, 7 | the stop driver: no throttle, brakes on (kind 5 off), steering along the line (5, 6) or straight (7) |
| 8 | parked |

A stopping car is **parked** (kind 8) once it is at rest: 3-D speed below 0.0694 m/s
(0.25 km/h) and angular speed below 0.0524 rad/s (3°/s). Parked, its brake comes off, the
handbrake goes on, the steering returns to straight, the clutch request stays 0 (the clutch
opens and stays open, §2.4), and every step zeroes the planar velocity, the yaw rate and
the longitudinal force sum (the hold of §4.1, as the race hold of §9.3).

### §8.7 Tires-worn flag

In the race modes with pit stops, while wear is on, a car's **tires-worn** flag is set when
some wheel has w ≥ 0 and a grip factor below the "worn" threshold (0.91 with the battle's
curve). A CPU car also gets a **pit request** and heads for the pits (ai §13). A player's
car only gets the flag, on its pad or under the autopilot: the player drives in. A tire
whose floor (§8.4) is above the threshold never sets the flag. Leaving the box clears the
flag; the pit exit clears the request.

### §8.8 Flap pendulums

Each wheel carries a pendulum x ∈ [−1, 1] with rate v. It is visual only: the car model's
mud flaps and aerials take the morph phase 0.5 + 0.5 x. Each step (phase 13), with a_x the
forward step's acceleration (§4.1; computed even with all wheels in the air), u the hub's
speed along the car (§10.1) and w the rim speed:

- a = 5 a_x;
- one random draw r ∈ [0, 1): if r · room < 0.02, a += 5 u (1 − x), with room = 1 − x for
  u ≥ 0, else 1 + x;
- for a rear wheel, and for a front wheel on a surface whose tire slot is not 0 (§6.5),
  while loaded and rolling forward, a second draw: if r · (1 − x) < 0.04, a += 20 w (1 − x);
- a −= 100 x + 5 v; then v += a·dt and x += v·dt; at ±1 the pendulum stops (v = 0).

A steady a_x therefore holds x at 5 a_x / 100 (the flaps lean back under throttle and
forward under braking), and the kicks grow with speed and on rough ground. All cars draw
from the race's one random sequence (the tire effect particles draw from it too), seeded at
every run start. A standing start and a pit stop reset a car's pendulums.

## §9 Race starts

| File | Role |
|---|---|
| `src/sim/exact/start.h`, `start.cpp` | every car's record at the start: parameters, placement, pose, standing and moving start, run-up |
| `src/sim/exact/race.cpp`, `race_flow.cpp` | the countdown, the rolling start's hand-back, the first crossing |
| `src/race/exact_field.cpp` | the start type and countdown from the event and the race mode |

How the cars are placed and started depends on the race mode, the event's start type
(event +0x1F3) and its initial speed (+0x198). The race's AI drives the player's car where
the table says (ai §1.2, §10). Counts are steps at 60 a second.

| Start | Used by | Placement (§9.2) | Speed (§9.3) | Afterwards |
|---|---|---|---|---|
| standing, 10 s countdown | start type 2: every single race | grid slot | rest, race hold | the player drives from GO |
| standing, 4 s countdown | start type 1: license tests from rest, online battles | grid slot (a license test with a start distance: on the main line there) | rest, race hold | as above |
| the event's countdown | start type 0: demonstrations, machine tests (countdown byte +0x1F2) | grid slot | rest | as above |
| no countdown | the split-screen battle | grid slot; with a handicap on the main line (below) | rest (with a handicap: the event's speed) | as above |
| rolling | start type 3 in a single race (GT endurance) | grid slot | 80 km/h, every car | the AI drives the player's car for 300 steps (5 s) |
| flying | time trials, free runs, license tests with a flying start | on the main line before the start line | the event's speed (time trials and free runs: see ai §10) | the AI drives the player's car until its first crossing of the start line |
| pit box | GT Mode's free practice (refused on a course without a pit box, §9.4) | the player's car in its pit box | rest, held 120 steps | the AI drives it out of the pits |

Rolling and flying starts, and a license or machine test whose event has an initial speed,
have no countdown. During a countdown every car is held by the race hold (§9.3). A
split-screen battle with a handicap starts both cars on the main line at the event's speed:
one 10 m before the start line, the other the handicap's distance (at least 20 m) before it.
Replays keep the race's setup and every car's pad frames, so they re-run the same start.

### §9.1 Flying starts

Time trials, free runs and license tests whose event has a flying start (a negative distance
at +0x1E6 and an initial speed) start moving:

- the car starts on the main line (line 6), at the event's speed (+0x198 km/h), that
  distance before the start line; time trials and free runs without one start from the
  course's driver set or before the last tight corner (ai §10);
- the race's AI drives the player's car (driver kind 1) with the course's parameter set for
  its driveline class (§7 #21), else the built-in set of the course's category, without the
  event's overrides (ai §9.1); the pad is not read;
- at the car's first crossing of the start line the AI hands it back and the lap clock
  starts. A free run placed from the driver set is also handed back after 180 steps (3 s).

### §9.2 Placement

**Grid slots.** A course's grid slot marks the **nose** of the car, so the car is moved back
along its heading by its front extent measured from the center of gravity:

  front extent from CoG = front extent − L·(w_f − 0.5),

where L is the wheelbase and w_f the front weight share (§6.2). Every car faces slot 0's
heading turned by 180°. A race never has more cars than the course's grid has slots (at
most 8): opponents beyond them are left out, and a course whose grid cannot hold the
players gives the page "The race cannot start.". The race start itself would read a grid
slot past the course's table (up to slot 7) from the words that follow the table in the
course's road data, as GT3 does, putting such a car mostly off the road; a slot of 8 or
more is refused.

**On the main line** (flying starts, a license test's start distance, the battle's
handicap): the car sits at the main line's point that far before
the start line (within the lap less 20 m), on the road under that point, facing the line
point 10 m further on. A course without the main line uses the grid slot instead.

**The pit box** of car i is the pit stop line's point n − 4 − i (n points), half a meter on
along its heading. A stop line without that point has no pit box for the car: the race is
not built (§9.4).

**The pose.** The car is set on the road: its pitch and roll are the ground's under the four
hubs (the ride heights are equal, §6.6, so no extra pitch), its reference point stands the
ride height above the ground, and its race progress, lap and sector come from its position:
a car placed before the start line starts at lap 0, sector 0.

### §9.3 The car's state at the start

**At rest** (the standing start). Every car is placed with:

- its suspension at the static position of each axle, and each wheel's load at its static
  corner load (§6.3);
- every wheel stopped, every velocity and slip 0;
- the engine speed 0; the first step lifts it to idle, because an open clutch floors the
  engine at idle (§2.4);
- 1st gear, clutch open;
- a shift lockout of 12 steps for every car but a player's on a manual gearbox: no gear
  change during the first 12 steps;
- the differentials of types `m` and `h` locked, the center clutch of layouts L4-L6 engaged
  (§2.7);
- the steering bounds at ± full lock, time scale 1, the aids', turbo, smoke, skid pitch,
  flap and brake-heat states cleared;
- a fresh set of tires (§8.4) and the engine condition and dirt the run starts with
  (§10.3, §8.3b).

**The race hold** keeps every car in place while the start countdown runs. Each step it:

- zeroes the planar velocity, the yaw rate and the longitudinal force sum that feeds the
  load transfer (§4.2), and the lateral acceleration;
- keeps the vertical velocity, so the car settles on its springs, on slopes too;
- keeps every car's clutch request at 0, so the clutch opens and the engine revs freely.

The hold applies **no** brakes, and the drawn wheels do not turn. CPU cars blip the throttle
during the countdown (ai §10).

**Moving** (rolling and flying starts, an event's initial speed). After the standing start:

- the velocity v along the car's forward axis, yaw rate 0;
- every wheel at road speed and the axles with them;
- the gear from §7 #24 (at least 1st);
- the engine speed = clamp(v × the gear's speed factor, idle, rev limit), the rpm
  truncated;
- the clutch locked (request 1);
- a turbo spooled to that rpm: its lag states at the rpm, its load 1 (§2.3);
- no hold. The 12-step shift lockout still applies, and a player's car the AI drives up to
  the start gets it again (not with NTSC-J's rules).

### §9.4 Run-up, hand-over and the pit start

- **Rolling start:** the race's AI drives the player's car (driver kind 1) for 300 steps
  (5 s) from the moving start, then hands it back with the START signal.
- **Flying start:** the AI drives until the car's first start-line crossing (§9.1).
- **Free practice:** the player's car starts in its pit box on the pit stop line, held for
  120 steps (2 s) as at the end of a pit stop (§8.6); then the AI drives it out of the pits
  and hands it back at the exit. On a course whose pit stop line has fewer than 4 points
  there is no pit box: choosing free practice (FREE RUN in GT Mode's pre-race menu) shows
  "Free run is not available on this course." and the pre-race menu stays. GT Mode's
  courses without a pit box are its dirt courses (no pit stop line), where the menu already
  turns FREE RUN off.

While the AI drives, the car behaves as §8.6 says (automatic gearbox, the AI's clutch rule,
its driver's aid block). The hand-back changes the driver kind to 0; the player's pad is
read from the next step.

**After the run** the race's AI drives the car again (ai §1.1): on along the main line at
0.8 × the corner speeds and half the throttle after a race finish, a time trial's lap, a
machine test's goal (or the top-speed test's pause EXIT counted as its finish) or most
license tests; stopping along the main line after a license stop test; stopping with the
wheels straight after a course-out (§11). A free run's pause EXIT counted as its finish
(presentation §E5) stops the car with the wheels straight on the pit surface, else drives
it on without that slowdown. A stopping car parks
(§8.6). A car whose run ends in the pit lane is driven out first. The autopilot and a
demonstration the disc has no recording for hand the player's car to the AI as ai §1.2
lists; the disc's recorded demonstrations play their pad frames with no take-over.

## §10 Ground frame, free wheels, engine condition, dirt and airborne cars

### §10.1 Speeds in the ground frame

The car's speeds are measured along the **ground**, not along the body. The ground frame is
the one §4.3 builds from the ground heights under the four hubs. It has the car's heading
and the ground's pitch and roll. At the end of each step (phase 12, after the ground frame),
its forward and lateral tangents give:

  u = v · t_fwd,  l = v · t_lat,

where v is the body's world velocity. Because of this, a vertical velocity contributes along
the slope of the road, not along the body's pitch. u is the forward speed that the next
step's aerodynamics, steering curves, clutch and shift rules, aids and planar forward step
read, and that advances the engine-condition counters (§10.3).

Each wheel's **hub velocity** uses only the yaw rate r, through fixed levers:

  u_i = u + X_i·r,  l_i = l − Y_i·r,

with:

- X = (−t_f/2, +t_f/2, −t_r/2, +t_r/2) for FL, FR, RL, RR, where t_f and t_r are the front
  and rear track widths;
- Y = (a, a, −b, −b), where a = L·(1 − w_f) is the distance from the center of gravity to
  the front axle and b = L·w_f the distance to the rear axle.

Pitch and roll rates and the individual triangle normals do not enter. A steered wheel's
speeds along and across its heading are the hub velocity turned by its steer angle; the next
step's slip ratio (§3.4), the tire effects (§8.3) and the free wheels (§10.2) read them.

### §10.2 Free-rolling wheels

At the end of each step (phase 12, after the speeds of §10.1), a wheel that is **free** is
locked to the ground. A free wheel:

- carries load;
- has no brake torque at all (pedal or handbrake);
- is neither driven nor coupled this step.

Its rim speed is set to its hub speed along its heading, the same expression the next
step's slip ratio uses, so the next slip ratio is exactly 0. An axle with a free wheel, and
an undriven axle (its differential type `f`, §6.2), then takes the mean of its two wheels'
rim speeds as its own speed.

A wheel counts as driven or coupled while the clutch is not open and its axle carries drive
in the layout (§7 #59):

- rear for L0 and L4-L6;
- front for L1;
- both axles for L2, L3 and L7;

and, for L4-L7, the front axle also whenever the center clutch is engaged or carries torque,
even with the clutch open.

So a coasting front wheel of a rear-driven car shows a slip ratio of exactly 0 and makes no
longitudinal force.

### §10.3 Engine condition

Two distance counters in meters describe the engine's condition:

- **E**, running-in and wear;
- **O**, the oil.

They scale the full-throttle torque T_wot (not the engine friction) by
f = f_E(E)·f_O(O):

| E | f_E | | O | f_O |
|---|---|---|---|---|
| < −300 km | 0.97 | | < −100 km | 1.05 |
| −300 km … 0 | 1 − 0.03·E / (−300 km) | | −100 km … 0 | 1 + 0.05·O / (−100 km) |
| 0 … 500 km | 1 | | ≥ 0 | 1 |
| 500 … 600 km | 1 − 0.02·(E − 500 km) / 100 km | | | |
| ≥ 600 km | 0.98 | | | |

The limits and slopes are the game's (its built-in engine condition values). Driving d meters
updates the counters:

- E ← min(E + d, 600 km);
- if O < 0: O ← O + d, and when it reaches 0 it jumps to 300 km;
- if O ≥ 0: O ← min(O + d, 300 km).

In a race every car carries its counters in its record. Each step (phase 2) the
full-throttle torque uses the factor of the current counters, and at the end of the step
(phase 12) the counters advance by |u|·Δt (§10.1), so reversing counts too. The race's top
speed estimate (§6.3) takes the factor the car starts with.

**Odometer.** The car also keeps an odometer word in 2 m units. Each step with u ≥ 0 (not
while reversing) it adds x = carry + u·Δt meters: the odometer gains trunc(trunc(x) / 2)
units and the rest, x − 2 × gained, is carried to the next step (an odd meter waits for the
next one). The odometer stops at 0xEE6B27A6 units and the carry is then 0. The race HUD
shows it and lights the oil lamp once O ≥ 300 km, the worn value.

The resulting behavior:

- a new engine (E = −300 km) gives 97 % and frees up over its first 300 km;
- it gives full power until 500 km, then loses 2 % by 600 km;
- fresh oil (O = −300 km after an oil change) gives up to 5 % extra, which fades over its
  last 100 km. After that the oil counts as used (O jumps to 300 km) and gives no bonus.

**Through a run.** A car's engine condition is the odometer, E and O (all zero is a fresh
engine with f = 1). GT Mode stores it per garage car: a new car starts at E = −300 km,
O = 0, and an oil change sets O = −300 km.

- **Which runs carry it**: GT Mode races and championships, machine tests and free runs, and
  arcade races and free runs. Such a run of a garage car starts with that car's values; in
  these modes a car that is not a garage car starts with zeros. License tests, time trials
  and the two-player battle always start with a fresh engine and hand nothing back.
- **During the run** the race seeds the player's car with the values at every start and
  restart. A restart or retry starts from the values the previous live run left, so every
  live run of the session keeps advancing them.
- **After the run** the values the last live run left go back to the garage car, and the
  total distance grows by 2 m for each odometer unit the runs added. Replays do not advance
  them; a replay re-runs from the values its run started with.

In an online race each player's garage car races with its own E and O (the odometer from
0). CPU opponents and the second player of a battle always start with zeros (f = 1).

### §10.4 Dirt courses: front brake cut when steering

On a course whose category byte (course table +0x08) is 2 (dirt), every car of the race has
its **front** brake pressure scaled before ASCC and ABS:

  p_front ← p_front · (1 − min(1, |δ| / 15°)),

where δ is the steering command (the front road-wheel angle before Ackermann and toe, §4.4).
The rear brakes are unchanged. The 15° and the cuts (100 % front, 0 % rear) are the
game's. Braking while turning therefore loads the rear brakes only, which helps the car
rotate on loose ground; with 15° of lock or more the fronts do not brake at all, and at 7.5°
they brake at half pressure.

On a dirt course every car's own TCS and ASCC are removed when the race builds it (§6.6);
the AI's aid block (ai §9.1) is unaffected. Dirt courses also have no tire wear (§8.1).

### §10.5 A wheel in the air

A wheel is in the air when its suspension coordinate x lies beyond the maximum droop
(x > droop limit, §4.2). Then:

- the stored coordinate (the next step's x_prev) is the droop limit, not the raw value, so
  the damper rate starts from the limit when the wheel lands;
- the spring force on the body is −preload: the wheel's unsprung weight hangs on the body;
- the damper speed and force and the tire load are 0; the anti-roll bar still acts, with the
  wheel at the droop limit;
- with no load the wheel has no tire forces (§3.1), and the drawn wheel hangs at full droop.

A ground query that misses the course (off the mesh) does **not** put a wheel in the air: it
keeps the previous ground height and surface (§1.3).

### §10.6 All four wheels in the air

If all four wheels carried no load after the previous step (the loads the tires read add up
to 0; a wheel can touch the ground and still carry none), the planar step (§4.1) adds
nothing to the velocity: no tire, engine, brake, rolling-resistance or drag force acts in
the plane, and the yaw rate is left unchanged; the lateral acceleration and the lateral
g / μ the aids and the 4WD coupling read are 0. The race hold, a parked car's hold and the
stop clamp (§4.1) do not act either, only the spawn/stop counter. Heave, gravity and the
attitude step (§4.3) still run, so the car falls and the airborne wheels' preload still acts
on the body.

The only horizontal effect left is the heave force acting along the tilted body axis, so the
horizontal speed changes only slightly while the car is in the air.

## §11 Leaving the road

**No hull contact.** The body never touches the ground. The ground acts only through:

- the four wheel probes;
- the bump stops (§4.2);
- the bump-stop attitude nudge (§4.3).

A car that bottoms out rides on its bump stops. Walls and other cars act through the
collision step (§5.3).

**Course-out.** In time trials and license tests only, the player's car (on its pad) goes
out of the course when:

- all four wheels stand on soft ground (grass, gravel, sand and surface 6; not on dirt
  courses);
- all four wheels stand on pit-lane triangles while the car is within the pit stretch (from
  the pit stop line's second point to its second-to-last);
- a wall hit's impulse changes the car's speed by more than 10 m/s (impulse / mass) in one
  step;
- it touches a pylon.

Hits on other cars never count. The race checks the wheels each step and stops checking
once the car is out. A time trial that goes out ends: FAIL, no time, and the AI stops the
car with the wheels straight (§9.4). A license test that goes out fails with the "out of
the course" result and stops the same way. A run that is out sets no best times. Races,
machine tests, free runs and free practice have no course-out.

**No respawn.** As in GT3, nothing puts a car back on the track: the player has no respawn
control, and CPU cars are not put back either (ai §12). A car that leaves the road stays
where it ends up until its driver gets it back.

## Verification

Run `TMPDIR=$PWD/build/tmp GT3R_DISC=disc/GT3.iso ./build/gt3r_tests <name>` to run a test
below; those marked (disc) need the disc.

| Test | What it checks |
|---|---|
| `exact_game_rules` (disc) | a race follows the disc's rules, or NTSC-U's on request (online, the demos); another release's rules are refused; every race steps 60 times a second (§1.1) |
| `race_inputs_program_behaviour` | the rules and step rate a program allows: its own release's rules at 60 steps a second by default; NTSC-U's rules, or the release's own rate (PAL's 50, which no race uses), on request; nothing else (§1.1) |
| `ee_float_platform_hash`, `ee_double_rules` | the PS2's single-precision arithmetic, pinned by a hash, and the software doubles' rules (§0) |
| `exact_game_pad_read` (disc) | the pad read: the pressure pad at rest, a light and a full throttle press, the stick's curve and dead band, the d-pad's pressure as kind 3 (§4.4) |
| `car_spec_r34_gtr` (disc) | the menus' spec of the R34 GT-R V-spec: 4WD, 1680 kg (1560 + 120), six gears, turbo, C_drag 0.511, C_front 0.348, C_rear 0.564, the 130 mm stock ride height on both axles (§6.2, §6.4) |
| `car_spec_settings` (disc) | the R34's stock settings (springs 40 / 50, front motion ratio 95 %, ride height 130 mm, downforce 29 / 47 %, final drive 3.545); explicit stock settings give the same spec; changed settings: the front ride height on both axles, the downforce coefficients, the final drive and a gear (§6.6) |
| `car_spec_computed_gearbox_slider` (disc) | the zz2 car's first gear 3.194 at its stock slider; a higher slider gives a taller top gear (§6.7) |
| `car_gear_generator_values` | the ratio generator: falling gear words, reverse = (3 × first + second) / 4, a taller top gear for a higher slider (§6.7) |
| `car_spec_showroom_top_speed` (disc) | the showroom top speed against the game's gauges, ±4 km/h (§6.3) |
| `car_spec_every_car_builds` (disc) | every car of the database builds with a curve, gearing and tires |
| `params_build_platform_hash` | the race's parameter builder on synthetic specs, pinned by a hash: the same bits on every platform (§6) |
| `program_tables_values` | the game's built-in tables (default curves, surface classes, static physics bytes, tire effect curves), pinned by a hash |
| `race_inputs_platform_hash` | what each race mode builds from the game's built-in tables and the event (wear on or off and its curve, the course-out rule, the tire effect curves, the brake heat and engine condition constants), on random tables, pinned by a hash |
| `engine_condition_rules` | the torque factor's running-in, wear and oil terms, the counters' advance, the odometer step and the oil lamp (§10.3) |
| `gtmode_engine_factor`, `gtmode_engine_condition_run` | which GT Mode runs carry the engine condition, what a run starts with and hands back (§10.3) |
| `exact_game_race_lockstep` (disc) | a GT Mode race, a dirt rally and a split-screen battle stepped through the race screen and directly: the same records, order and clock every step (§9 starts, §8 wear and effects, §10.4) |
| `exact_game_solo_modes` (disc) | a time trial (the AI's run-up and hand-back, one timed lap), a license test (judged), a machine test (ended at its goal), a free run and free practice (§9.1, §9.4, §11) |
| `exact_demo_records` (disc) | the disc's recorded demonstrations replay on the race to their recorded ends |
| `exact_replay_round_trip` (disc) | a race with a seeded engine condition replays to the same end (§10.3) |
| `exact_replay_ghost` (disc) | a time trial's whole-run ghost drives the earlier run again step for step and takes no part in the player's (walls-only) collision step (§5.3) |
| `net_exact_posed_car_contact` (disc) | an online player's posed car driving into ours: our car is pushed, and the hit reported for its owner pushes the posed car back (§5.3) |
| `race_rumble` | GT3 original: a step's vibration level goes to the large motor as it is (100, 127) and the small motor stays off; Modern: hits, curbs, off-road, slides and wheelspin; both stop for a car the AI drives or a step without telemetry (§1.2) |
| `exact_game_exit_as_finish` (disc) | a car the pad drives sends vibration through a lap; after the finish, the AI driving it, it sends none (§1.2) |
| `race_pylons_disc` (disc) | the license pylons are knocked about by a car's box and every touch is reported (§11) |

## Open points

- The brake heat (§8.5) is computed for every car, but nothing draws it yet.
- The race runs the game's own tire smoke and dust particle emitter, but the race screen
  does not draw its particles: it draws its own puffs from the smoke emission (§8.3b).
- The menus' spec keeps conventional defaults for missing values (§6), so a mod car whose
  records lack a value can show figures that differ from how it races.
- Electric mod cars race on their donor car's engine (§6).
- The HUD's boost gauge reads the turbo's boost factor from the two lagged stages minus 1
  (§2.3); how the game's gauge maps it is not pinned down.
- A car whose wheel spin-inertia word is 0 would get no torque response and no free-wheel
  mass (§2.7); whether any disc car has such a word is not checked.
