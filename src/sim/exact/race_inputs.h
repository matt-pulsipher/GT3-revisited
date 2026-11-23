#pragma once

// What the tick (tick.h) reads outside the car record, built as the original builds it when a race is set up: from the
// program's own tables (ProgramData, read from the user's CORE.GT3), the race's event and course records and the race mode
// (its conductor). Each builder is the program's routine, in its arithmetic.

#include "sim/exact/ai_course.h"
#include "sim/exact/program_data.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

// ---- the program's tables (DynamicsConductor::vf21 0x1F2FE0 runs these builders at every race's initialisation) --------

// 0x1DAE70 (with the static block 0x2D28B0): phases 5 and 7's static bytes and the rolling-resistance table (0x2D2CE0, its
// points from the block's +0x0B count, +0x0C speeds (x 2 km/h) and +0x14 factors (x 0.001); its coefficients per surface
// from +0x2C (s16 x 0.01)). Throws std::runtime_error when the block's point count is not 1..8.
Statics statics(const ProgramData& d);
// 0x1DAE70's road bump tables (+0x1C amplitude mm, +0x4C wavelength dm, +0x5C, +0x6C) and the soft materials 0x2D53D8.
SurfaceBumps surface_bumps(const ProgramData& d);
// 0x1DB020: the slipstream globals 0x352924 .. 0x352930 from the block's bytes 0, 1, 2, 5, 6 [physics §5.2].
Slipstream slipstream(const ProgramData& d);
// 0x1DAB50 (condition_params) on the block's bytes +0xA0 .. +0xA8.
Condition condition(const ProgramData& d);
// Phase 6's tunables (the block's +0xAD .. +0xB7); the course category byte is the race's (race_course_category).
AidInputs aid_tunables(const ProgramData& d, u8 course_category);
// 0x1E6878: the brake heat constants 0x49D0B0 .. from the block's +0x7C .. +0x7F.
BrakeHeatConsts brake_heat_consts(const ProgramData& d);
// 0x1D91D8: a pedal curve record from its type and parameter bytes.
PedalCurve pedal_curve_record(u8 type, u8 param, bool toward_zero = false); // (toward_zero: 2002 Tokyo-Geneva's 0.1)
// 0x1DFF40: the force feedback's parameters from the controller settings +0x34 .. +0x3E.
SteerFeel steer_feel(const ProgramData& d);
// 0x1DE830 (with 0x1DE1E0, which first clamps the source block 0x2D35B8 in place): the tyre effect curves of surface classes
// 0..2 and the kerb ramp.
EffectTables effect_tables(const ProgramData& d);
// 0x1DE1E0's clamps of the tyre effect block (what the program leaves in 0x2D35B8 after the first race's initialisation).
void clamp_effect_block(ProgramData& d);
// 0x1DF4C0's engine sound scales 0x49D03C .. 0x49D048 (the block's +0x1FC .. +0x1FF).
std::array<F, 4> sound_scales(const ProgramData& d);
// 2002 Tokyo-Geneva's special kind 7 ride (ProgramData's Kind7Ride; off where the disc has none or its switch is 0).
Kind7Ride kind7_ride(const ProgramData& d);
// The controller settings' gearbox bytes +0x14 .. +0x17 (read at run time by the gearbox).
GearSettings gear_settings(const ProgramData& d);
// 0x1E3EA0: the driver input's constants from the controller settings (and 0x2D4948's surface classes).
PadConstants pad_constants(const ProgramData& d);

// ---- the race mode: the conductor ----------------------------------------------------------------------------------------

// The race modes' conductors (DynamicsConductor and its subclasses): which answers the tick gets.
enum class Conductor : u8 {
    SinglePlayer, // races, GT mode and arcade (ctor 0x1F2748)
    TimeTrial,    // 0x1F1AD8
    License,      // 0x1F0AD8
    FreeRun,      // 0x1F1190
    Battle2P,     // split-screen battle 0x1EFFD0
    BattleMP,     // i.LINK battle 0x1F03A8
    MachineTest,  // 0x1F05F8 (the base conductor's answers)
    FreePractice, // 0x1F2010
    // Gran Turismo Concept's licence test with the pace car (DynamicsConductorTraining, a subclass of the licence's;
    // concept §9.2): the licence conductor with no ghost car (virtual 5), the event's rubber band (11), the
    // whole collision step (12), the single races' control sources (24) and rubber band pass (32).
    Training,
};
const char* conductor_name(Conductor k);
// The licence test's conductor or its subclass: every answer the two share.
constexpr bool licence_kind(Conductor k) { return k == Conductor::License || k == Conductor::Training; }

// The race configuration block (conductor + 0xE340, 0x40 bytes) as 0x1DA708 copies it from the event record (database table
// 32), before the battle conductors' virtuals 10 / 11 adjust it.
using RaceConfig = std::array<u8, 0x40>;
RaceConfig race_config(ByteSpan event_record);
// The block's overrides of a CPU car's driver: throttle, brake, corner speed and corner offset per driveline class (+0xC ..
// +0x1B; event_overrides, start.h) [ai §9.2].
constexpr u32 kDriverOverridesAt = 0xC;
using DriverOverrides = std::array<u8, 0x10>;
DriverOverrides driver_overrides(const RaceConfig& c);
// The block with no slow-down or speed-up left in its rubber band, as the arcade's Professional races have it [ai §9.3].
void clear_rubber_band(RaceConfig& c);
// Whether the race has tyre wear as virtual 21 hands it to 0x1DA998: virtual 10's answer, 0 on dirt. The battle conductors'
// virtual 10 (0x1DA238) writes the wear bytes of `cfg` for the race info's wear word (`battle_wear`: race info + 0xC; set:
// its setting 2, else none) unless the course is dirt. NTSC-J's battle conductor hands the word on as the setting (1 and 2
// are 0x1DA238's two curves).
bool wear_answer(Conductor k, RaceConfig& cfg, u8 course_category, s32 battle_wear, Program p = Program::NtscU);
// Virtual 11: whether the rubber band is on. The battle conductors' version (0x1DA1B8) first writes its bytes of `cfg` for
// the race info's setting (`battle_catchup`: race info + 0x14). NTSC-J's battle conductor gives it setting 2 for any set word.
bool rubber_band_answer(Conductor k, RaceConfig& cfg, s32 battle_catchup, Program p = Program::NtscU);
// 0x1DA998: the wear curve (conductor + 0x84) [physics §8.1]; all zero (wear off) unless `on` (wear_answer) and the
// configuration's bytes are ordered.
WearParams wear_params(const RaceConfig& cfg, const ProgramData& d, bool on);
// Virtual 39: the mode has pit stops; the ground contact marks pit-lane wheels (1: single races, battles, free practice,
// machine test; 0: time trial, licence, free run).
bool has_pits(Conductor k);
// Virtual 35 (0x1D9FE0 asks it for a car with control source 0): 3 in licences and in time trials off dirt, else 0.
// `build`: the program whose licence conductor answers (the race's Rule::LicenceJudge). The later builds' answers 0.
s32 pit_mode(Conductor k, u8 course_category, Program build = Program::NtscU);
// The base conductor's course-out rule (virtual 40) and its answers 37 / 38, or off. The later builds' licence conductor
// (`build`: the race's Rule::LicenceJudge) answers 1 / 0 on every course: only the pit stretch is out of the course there,
// soft ground is not.
CourseOut course_out_rule(Conductor k, u8 course_category, Program build = Program::NtscU);
// The same with the course's pit stretch (0x1FA5B0: the AI course's game line 4, the pit stop line, from its point 1's race
// distance to its point n - 2's; none with fewer than 6 points) and lap length.
CourseOut course_out_rule(Conductor k, u8 course_category, const AiCourse& ai, Program build = Program::NtscU);
// The conductor's virtual 41, a hit's course-out (0x1DFEB8 asks it for the collision step's hits: kind 0 a wall, 1 a car, 2
// the other; `j` the impulse): the time trial's and licence's 0x1DA130 set F +0x51E bit 0 of the car of index 0 that has not
// finished for a wall hit whose impulse times P +0x4C (1 / mass) exceeds 10 m/s and for every kind 2 hit; empty elsewhere.
void hit_course_out(Car& c, Conductor k, s32 kind, F j);
// 0x1F9E10 (through 0x1F2F08): the heading the spawn hold turns a car to (its mode 2, after a pit stop): game line 4's point
// `box`, the car's pit box F +0x4DE; 0 where the line has no such point (the program reads past it).
F pit_box_heading(const AiCourse& ai, int box);
// 0x1ECBA8: the rubber band's parameters from the configuration (+0x20 global %, +0x24 / +0x25 / +0x26 the slow-down's
// amount %, start gap m, full gap x 10 m, +0x28 / +0x29 / +0x2A the speed-up's) when `on` (virtual 11), else none (scale 1).
RubberBand rubber_band_params(const RaceConfig& cfg, bool on);
// Virtual 32, the rubber band's pass: the configuration's +0x22 for single races, battles and the pace-car test, else -1
// (none).
s32 rubber_band_mode(Conductor k, const RaceConfig& cfg);
// The i.LINK battle's virtual 4 caps the scale (band_scale's cap).
bool rubber_band_cap(Conductor k);

// The race-level inputs of a race: everything RaceInputs holds but the course's ground query and the course-out stretch.
// `course_category` is the course record's byte +8 (database table 34; race_course_category).
// The course-out rule is the disc's program's (ProgramData::program).
RaceInputs race_inputs(const ProgramData& d, Conductor k, const RaceConfig& cfg, bool wear_on, u8 course_category);
// The course category byte (0x1F2F28: the course record's +8 through 0x2389A0).
u8 race_course_category(ByteSpan course_record);

} // namespace gt3r::sim::exact
