#pragma once

// What the tick (tick.h) reads outside the car record, built as the original builds it when a race is set up: from the
// program's own tables (ProgramData, read from the user's CORE.GT3), the race's event and course records and the race mode
// (its conductor). Each builder is the program's routine, in its arithmetic.

#include "sim/exact/ai_course.h"
#include "sim/exact/program_data.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

// ---- the program's tables (the conductor's initialisation runs these builders at every race's initialisation) --------

// Phases 5 and 7's static bytes and the rolling-resistance table (its
// points from the block's +0x0B count, +0x0C speeds (x 2 km/h) and +0x14 factors (x 0.001); its coefficients per surface
// from +0x2C (s16 x 0.01)). Throws std::runtime_error when the block's point count is not 1..8.
Statics statics(const ProgramData& d);
// The road bump tables (+0x1C amplitude mm, +0x4C wavelength dm, +0x5C, +0x6C) and the soft materials.
SurfaceBumps surface_bumps(const ProgramData& d);
// The slipstream globals from the block's bytes 0, 1, 2, 5, 6 [physics §5.2].
Slipstream slipstream(const ProgramData& d);
// condition_params on the block's bytes +0xA0 .. +0xA8.
Condition condition(const ProgramData& d);
// Phase 6's tunables (the block's +0xAD .. +0xB7); the course category byte is the race's (race_course_category).
AidInputs aid_tunables(const ProgramData& d, u8 course_category);
// The brake heat constants from the block's +0x7C .. +0x7F.
BrakeHeatConsts brake_heat_consts(const ProgramData& d);
// A pedal curve record from its type and parameter bytes.
PedalCurve pedal_curve_record(u8 type, u8 param);
// The force feedback's parameters from the controller settings +0x34 .. +0x3E.
SteerFeel steer_feel(const ProgramData& d);
// The tyre effect curves of surface classes
// 0..2 and the kerb ramp.
EffectTables effect_tables(const ProgramData& d);
// The clamps of the tyre effect block (what the program leaves after the first race's initialisation).
void clamp_effect_block(ProgramData& d);
// The engine sound scales (the block's +0x1FC .. +0x1FF).
std::array<F, 4> sound_scales(const ProgramData& d);
// The controller settings' gearbox bytes +0x14 .. +0x17 (read at run time by the gearbox).
GearSettings gear_settings(const ProgramData& d);
// The driver input's constants from the controller settings (and the surface classes).
PadConstants pad_constants(const ProgramData& d);

// ---- the race mode: the conductor ----------------------------------------------------------------------------------------

// The race modes' conductors (conductor and its subclasses): which answers the tick gets.
enum class Conductor : u8 {
    SinglePlayer, // races, GT mode and arcade
    TimeTrial,
    License,
    FreeRun,
    Battle2P,     // split-screen battle
    BattleMP,     // i.LINK battle
    MachineTest,  // (the base conductor's answers)
    FreePractice,
};
const char* conductor_name(Conductor k);

// The race configuration block (conductor + 0xE340, 0x40 bytes) as the program copies it from the event record (database table
// 32), before the battle conductors' wear setting / rubber-band switch adjust it.
using RaceConfig = std::array<u8, 0x40>;
RaceConfig race_config(ByteSpan event_record);
// The block's overrides of a CPU car's driver: throttle, brake, corner speed and corner offset per driveline class (+0xC ..
// +0x1B; event_overrides, start.h) [ai §9.2].
constexpr u32 kDriverOverridesAt = 0xC;
using DriverOverrides = std::array<u8, 0x10>;
DriverOverrides driver_overrides(const RaceConfig& c);
// The block with no slow-down or speed-up left in its rubber band, as the arcade's Professional races have it [ai §9.3].
void clear_rubber_band(RaceConfig& c);
// Whether the race has tyre wear as the initialisation hands it: the wear setting's answer, 0 on dirt. The battle conductors'
// wear setting writes the wear bytes of `cfg` for the race info's wear word (`battle_wear`: race info + 0xC; set:
// its setting 2, else none) unless the course is dirt. NTSC-J's battle conductor hands the word on as the setting (1 and 2
// are the two curves).
bool wear_answer(Conductor k, RaceConfig& cfg, u8 course_category, s32 battle_wear, Program p = Program::NtscU);
// The rubber-band switch: whether the rubber band is on. The battle conductors' version first writes its bytes of `cfg` for
// the race info's setting (`battle_catchup`: race info + 0x14). NTSC-J's battle conductor gives it setting 2 for any set word.
bool rubber_band_answer(Conductor k, RaceConfig& cfg, s32 battle_catchup, Program p = Program::NtscU);
// The wear curve (conductor + 0x84) [physics §8.1]; all zero (wear off) unless `on` (wear_answer) and the
// configuration's bytes are ordered.
WearParams wear_params(const RaceConfig& cfg, const ProgramData& d, bool on);
// Virtual 39: the mode has pit stops; the ground contact marks pit-lane wheels (1: single races, battles, free practice,
// machine test; 0: time trial, licence, free run).
bool has_pits(Conductor k);
// The wear answer (the program asks it for a car with control source 0): 3 in licences and in time trials off dirt, else 0.
s32 pit_mode(Conductor k, u8 course_category);
// The base conductor's course-out rule and its answers 37 / 38, or off.
CourseOut course_out_rule(Conductor k, u8 course_category);
// The same with the course's pit stretch (the AI course's game line 4, the pit stop line, from its point 1's race
// distance to its point n - 2's; none with fewer than 6 points) and lap length.
CourseOut course_out_rule(Conductor k, u8 course_category, const AiCourse& ai);
// The conductor's hit rule, a hit's course-out (the program asks it for the collision step's hits: kind 0 a wall, 1 a car, 2
// the other; `j` the impulse): the time trial's and licence's code set F +0x51E bit 0 of the car of index 0 that has not
// finished for a wall hit whose impulse times P +0x4C (1 / mass) exceeds 10 m/s and for every kind 2 hit; empty elsewhere.
void hit_course_out(Car& c, Conductor k, s32 kind, F j);
// The heading the spawn hold turns a car to (its mode 2, after a pit stop): game line 4's point
// `box`, the car's pit box F +0x4DE; 0 where the line has no such point (the program reads past it).
F pit_box_heading(const AiCourse& ai, int box);
// The rubber band's parameters from the configuration (+0x20 global %, +0x24 / +0x25 / +0x26 the slow-down's
// amount %, start gap m, full gap x 10 m, +0x28 / +0x29 / +0x2A the speed-up's) when `on` (the rubber-band switch), else none (scale 1).
RubberBand rubber_band_params(const RaceConfig& cfg, bool on);
// Virtual 32, the rubber band's pass: the configuration's +0x22 for single races and battles, else -1 (none).
s32 rubber_band_mode(Conductor k, const RaceConfig& cfg);
// The i.LINK battle's time-scale hook caps the scale (band_scale's cap).
bool rubber_band_cap(Conductor k);

// The race-level inputs of a race: everything RaceInputs holds but the course's ground query and the course-out stretch.
// `course_category` is the course record's byte +8 (database table 34; race_course_category).
RaceInputs race_inputs(const ProgramData& d, Conductor k, const RaceConfig& cfg, bool wear_on, u8 course_category);
// The course category byte (the course record's +8).
u8 race_course_category(ByteSpan course_record);

} // namespace gt3r::sim::exact
