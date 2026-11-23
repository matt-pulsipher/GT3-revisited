#pragma once

// race_tick_exact's inputs outside the car record: built as the game builds them (src/sim/exact/race_inputs.h) from the
// program's tables in the disc's CORE.GT3 and the race's event, course and race mode, and what the original's memory holds
// for each (mem_*). The ports are fed the built inputs; each adapter checks them against the memory on every call
// (differ()); check_race_start() compares every one of them, and the conductor's answers (its virtuals called directly),
// when a race has been set up. The variants (Call::vary) run on the memory instead: their changes to it are not the built
// inputs'.

#include "ee.h"
#include "program_map.h"

#include "sim/exact/race_flow.h"
#include "sim/exact/race_inputs.h"

#include <string>

namespace gt3r::data {
struct Event;
class CarDatabase;
} // namespace gt3r::data

namespace gt3r::diff::tick_inputs {

namespace ex = sim::exact;

// The built inputs of the race being run.
struct Built {
    ex::ProgramData data;      // the program's tables, with the harness's changes to the original's static data mirrored
    ex::Conductor kind = ex::Conductor::SinglePlayer;
    ex::RaceConfig cfg{};      // conductor + 0xE340 after the conductor's virtuals 11 and 10
    u8 course_category = 0;    // the course record's +8
    bool wear_on = false;      // virtual 21's answer to 0x1DA998
    bool band_on = false;      // virtual 11
    ex::RaceInputs race;       // without the course's ground query and the course-out stretch (the harness adds those)
    ex::RubberBand band;       // 0x1ECBA8
    u8 flow = 0;               // the race-flow byte (conductor + 0x8000 + 0x37AC: virtual 23's argument, the race's)
    ee::F flow_scale = ee::F::raw(0x3F800000); // the race-flow time scale (+0x37B0: 1 from virtual 21)
    // The race's AI course (ai_course_setup on our parse of the race's course file; the harness sets it with the course):
    // the course-out rule's pit stretch and lap length and the spawn hold's pit box headings.
    ex::AiCourse ai;
    bool ok = false;           // reset() has run for the race
    // A new race: the pristine tables, the race mode, the event, the course's category and the battles' race info words
    // (+0xC tyre wear, +0x14 catch-up).
    void reset(const ex::ProgramData& pristine, ex::Conductor k, const data::Event& ev, u8 course_category, s32 battle_wear = 0,
               s32 battle_catchup = 0);
    // A change to the original's static data: written to the memory and to our copy, and the inputs rebuilt.
    void poke(Ee& ee, u32 addr, u8 v);
    void rebuild();
    // The course-out rule (virtual 40 and its answers) with the stretch of the AI course.
    ex::CourseOut course_out() const { return ex::course_out_rule(kind, course_category, ai, data.behaviour().rule(sim::Rule::LicenceJudge)); } // (the later builds' licence answers)
};
Built& built();
// The course category byte of an event's course in our database (the course record's +8).
u8 course_category(const data::CarDatabase& db, const data::Event& ev);

// The original's memory.
ex::Statics mem_statics(Ee& ee);
ex::SurfaceBumps mem_bumps(Ee& ee);
ex::Slipstream mem_slipstream(Ee& ee);
ex::Condition mem_condition(Ee& ee); // the block 0x2D2B00 once 0x1DAB50 built it, else from its source bytes
ex::AidInputs mem_aids(Ee& ee, u32 car);
ex::BrakeHeatConsts mem_brake_heat(Ee& ee);
ex::SteerFeel mem_steer_feel(Ee& ee);
ex::EffectTables mem_effects(Ee& ee);
std::array<ex::F, 4> mem_sound(Ee& ee);
ex::GearSettings mem_gears(Ee& ee);
ex::PadConstants mem_pad(Ee& ee);
ex::WearParams mem_wear(Ee& ee, u32 car);
ex::RubberBand mem_band(Ee& ee, u32 cond);
// The course-out rule of the race's conductor with the original's AI course object's pit stretch (0x1FA5B0: [[conductor +
// 0xBBA8] + 4] + 0x18, the stop line: its point count, point 1's and point n - 2's race distance) and the race course's lap
// length (conductor + 0xB7A8 -> +0x70 -> +0x18); the rest (on, its answers) from `rule`.
ex::CourseOut mem_course_out(Ee& ee, u32 cond, ex::CourseOut rule);
// 0x1F2F08's pit box heading in the original's AI course object (the stop line's point `box`, +0x24; read past the line as
// the program does).
ex::F mem_pit_box_heading(Ee& ee, u32 cond, int box);
// The conductor's gap matrices (+0xB8F4 / +0xB934).
ex::GapMatrix mem_gaps(Ee& ee, u32 cond);

// The first difference between ours and the original's ("" when equal), named.
std::string differ(const ex::Statics& a, const ex::Statics& b);
std::string differ(const ex::SurfaceBumps& a, const ex::SurfaceBumps& b);
std::string differ(const ex::Slipstream& a, const ex::Slipstream& b);
std::string differ(const ex::Condition& a, const ex::Condition& b);
std::string differ(const ex::AidInputs& a, const ex::AidInputs& b);
std::string differ(const ex::BrakeHeatConsts& a, const ex::BrakeHeatConsts& b);
std::string differ(const ex::SteerFeel& a, const ex::SteerFeel& b);
std::string differ(const ex::EffectTables& a, const ex::EffectTables& b);
std::string differ(const std::array<ex::F, 4>& a, const std::array<ex::F, 4>& b);
std::string differ(const ex::GearSettings& a, const ex::GearSettings& b);
std::string differ(const ex::PadConstants& a, const ex::PadConstants& b);
std::string differ(const ex::WearParams& a, const ex::WearParams& b);
std::string differ(const ex::RubberBand& a, const ex::RubberBand& b);
std::string differ(const ex::CourseOut& a, const ex::CourseOut& b);

// The race-level inputs we build against the memory at a call: "" when they all agree.
std::string race_inputs_differ(Ee& ee, u32 car);

// When a race has been set up (before its first step): every built input against the original's memory, the race
// configuration block, the wear curve, the rubber band's parameters, the race-flow globals and the proximity matrices, and the
// conductor's answers to virtuals 10, 11, 32, 35, 37, 38 and 39 (called directly), the course-out stretch and the pit
// box headings of the cars' pit boxes, the gap matrices (0x1ED458, which the initialisation's virtual 22 runs) and the ranking. ""
// when all agree.
std::string check_race_start(Ee& ee, u32 cond, int ncars);

} // namespace gt3r::diff::tick_inputs
