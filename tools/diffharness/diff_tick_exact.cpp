// The vehicle tick in the original's own arithmetic (src/sim/exact/tick.h) against the original's race step 0x1F39D8, bit for
// bit (race_tick_exact).
//
// Ports (kPorts): each ported function is probed where it is called, at a call site in the race step or inside a phase
// function: at its entry, only when $ra is that call's return address, the car record is snapshotted (Port::car says where
// the record is when a0 is not the car: a register holding car + an offset); at the return address the port runs on the
// snapshot and every field of the field table (src/sim/exact/car_fields.h) must equal the original's result. What the call left
// outside the record (a return value, a caller's array, the VU0 random register, the conductor's matrices) the port compares
// itself into Call::out (check_f0 / check_v0 for results). A call's inputs besides the record are in Call: the argument
// registers, f12..f19, t0..t3, $sp, the random register, v0 at Port::taps inside the call (the conductor's answers to
// virtual calls), our ground query for the race's course. Port::only limits a port to the calls on its path (the others are
// counted as skipped); Port::vary runs a port's variants every kVaryEvery ticks of the CPU races on changed copies of each
// car's record against the original called directly (GT3R_TICK_VARY=0: off). A phase called once for all cars (kRacePorts,
// a0 = the conductor) is checked the same way on every car's record and the conductor bytes it keeps. Words a call changed
// that no field covers are listed, so a port knows which fields it still has to add.
//
// The race step (StepCheck): sim::exact::tick_until_collision and tick_after_collision, the step's phases phase-major for
// every car of the race, against the whole step: all cars from their records at phase 1, compared at the collision step's
// call and, from the records after it, at the end of phase 13 (with the proximity matrices and the random register). The
// step's own parts (FlowCheck): the race-flow gate and the rubber band (tick_race.cpp).
//
// The inputs outside the record (tick_inputs.h) are built as the game builds them (sim/exact/race_inputs.h: the program's
// tables from the disc's CORE.GT3, the race's event, course and conductor) and fed to every port and the step check, each
// checked against the original's memory on every call; every race checks them all, and the conductor's answers, at its start.
//
// The races: the two-car contact races (race_hits.h: two R34s, pad input without steering, car contact and wall hits;
// GT3R_CARHIT_SWEEP=1 adds the 50 side swipes into the wall), the pad scenarios of tick_scenarios() (braking, steering, the
// handbrake, wheelspin, AI driving, every ASCC channel) and of pad_scenarios() (per-tick pad frames: steering, pedals, every
// controller kind, pedal styles, reversing, the hold), pad drives on the same placement (full lock, a locked stop, reversing
// with steering, off the road), both cars rolling back on full throttle, scripted pad manoeuvres (braking to a stop,
// reversing, full lock in reverse, handbrake, the kind 8 and spawn-counter holds), one contact race with the longitudinal
// force's config bytes at 2, a run down Grand Valley's pit lane, whole CPU races (race_setup.h: up to six AI cars from an
// event after a start countdown with the race hold; kPokes adds rare states: spawn / stop holds, a tipped body, a dropped
// car; each race with other force feedback settings and condition words near their limits) and scripted pad drives
// (kPadDrives: one car alone on the Test Course per layout / differential / pedal style, some in synthetic states).
//
// Options: GT3R_TICK_ONLY=<substring of a port's name> ("race step" for the step check), GT3R_CARHIT_ONLY=<scenario index>,
// GT3R_CARHIT_TICKS, GT3R_TICK_EVENTS=<event label,...> (the CPU races, "-" for none, "+<event label,...>" the default ones
// and these after them; default Sunday Cup 3 at Trial Mountain
// (small FF / FR cars), the Evolution meeting at Laguna Seca (4WD), a GT World race at Grand Valley (race cars), the Tahiti
// Maze rally (dirt; one car), Sunday Cup 2 at Midfield (FF cars: the course set's rear ASCC channel), the Spider meeting at
// Deep Forest (FR / MR cars: a TCS lateral factor), the 4WD meeting at Deep Forest (4WD layouts 2 and 3), the wet Route 5
// demo race (wet surfaces; four cars), the Swiss Alps rally (loose surfaces; one car), a GT World race at Trial Mountain with
// tyre wear from part-worn tyres ("/worn": pit requests and stops, a control source 1 car, a pad car), with car 0 on the pad
// ("+pad") the Smoky Mountain rally (dirt, loose surfaces) and the wet Route 5 demo race (course category 1), a Turbo
// Sports race at Midfield with tyre wear and seeded start states (condition counters, wear, the HUD timer, a spawn hold),
// races under the other race modes' conductors ("@tt", "@licence", "@freerun", "@machine", "@practice": car 0 alone on the
// pad, "#runup" driven by the AI to its first crossing, "#ai" by the AI throughout; "@battle2p" two cars, "@battlemp" the
// event's, with their wear and catch-up settings), "#finish" one-lap races and synthetic race settings ("#band1", "#band2":
// the rubber band's modes 1 / 2; "#flow": the race-flow byte set, time scale 0.37); "#sparks": every car with its model's
// spark points and the sparks' frame between the steps checked (StepCheck::spark_frame), "#scrape" the same with car 0 on
// the pad driven flat out along the race line (GT3R_TICK_SCRAPE_PACE, default 2.5)),
// GT3R_TICK_AI_SECONDS (each CPU race's length, default 40), GT3R_TICK_WORN_SECONDS (a "/worn" race's, default 120),
// GT3R_TICK_HOLD (the CPU races' start countdown in frames, default 90), GT3R_TICK_MANOEUVRES=0 (no pad manoeuvres),
// GT3R_TICK_PIT=0 (no pit lane run), GT3R_TICK_DRIVES=<substring of a pad drive's name> ("-" for none).

#include "ai_exact_probe.h"
#include "car_exact_probe.h"
#include "collide_exact_probe.h"
#include "harness.h"
#include "leaves_probe.h"
#include "race_hits.h"
#include "race_setup.h"
#include "sparks_probe.h"
#include "start_setup.h"
#include "tick_inputs.h"

#include "disc/disc.h"
#include "disc/volume.h"
#include "formats/course/course_file.h"
#include "formats/course/road.h"
#include "formats/model/car_model.h"
#include "game/data/car_database.h"
#include "race/demo_replay.h"
#include "race/exact_field.h"
#include "race/exact_setup.h"

#include "sim/exact/ai_course.h"
#include "sim/exact/build_math.h"
#include "sim/exact/params_build.h"
#include "sim/exact/course_ground.h"
#include "sim/exact/course_query.h"
#include "sim/exact/hybrid.h"
#include "sim/exact/pit_lane.h"
#include "sim/exact/race.h"
#include "sim/exact/race_flow.h"
#include "sim/exact/race_inputs.h"
#include "sim/exact/tick.h"
#include "track/track.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::diff;

namespace {

namespace ce = car_exact;
namespace ex = sim::exact;
namespace rh = race_hits;
namespace ti = tick_inputs;

// The AI's calls into the line evaluation and speed planning (sim::exact::AiPlan) in the original's run: each call made at
// one of kPlanSites (the AI ports' own calls) with its arguments, the car record before and after it and its results. A port
// of the AI's decisions runs on these results (TapePlan), so it is checked on its own; the plan's ports are checked at their
// own call sites.
enum PlanKind : int { kPlanBraking, kPlanTarget, kPlanSpeed, kPlanRequest, kPlanLaneLeft, kPlanLaneRight, kPlanLap };
struct PlanSite {
    Pa fn, ra; // the function and the return address of its call (NTSC-U; Pa reads the disc's)
    PlanKind kind;
};
const PlanSite kPlanSites[] = {
    {0x001F7088, 0x001F7B7C, kPlanBraking},  {0x001F7088, 0x001F665C, kPlanBraking},  {0x001F7658, 0x001F7CE8, kPlanTarget},
    {0x001F7658, 0x001F7D1C, kPlanTarget},   {0x001F7590, 0x001F83A0, kPlanSpeed},    {0x001F7590, 0x001F83E4, kPlanSpeed},
    {0x001F7590, 0x001F8400, kPlanSpeed},    {0x001F7590, 0x001F8448, kPlanSpeed},    {0x001F8980, 0x001F6F38, kPlanRequest},
    {0x001F8980, 0x001F7004, kPlanRequest},  {0x001F8980, 0x001F7048, kPlanRequest},  {0x001F9CE0, 0x001F6E90, kPlanLaneLeft},
    {0x001F9CE0, 0x001F7030, kPlanLaneLeft}, {0x001F9D38, 0x001F6EB4, kPlanLaneRight}, {0x001F9D38, 0x001F6FEC, kPlanLaneRight},
    {0x0018FA10, 0x001F82C0, kPlanLap},
    // (a later build's traffic step jumps to its last line request: the request returns where the step's call does)
    {0x001F8980, 0x001F8520, kPlanRequest},
    // the pit lane's and the end of a run's line requests (pit_lane.h)
    {0x001F8980, 0x001E3C54, kPlanRequest}, {0x001F8980, 0x001E3D28, kPlanRequest}, {0x001F8980, 0x001E3D80, kPlanRequest},
    {0x001F8980, 0x001E3DC8, kPlanRequest}, {0x001F8980, 0x001E3CD8, kPlanRequest}, {0x001F8980, 0x001E3E58, kPlanRequest},
    {0x001F8980, later_place(kPitExitLine6Return), kPlanRequest},
    // (a later build's decision: the lap length for the stop point, through the conductor's 0x1F2EA8, which jumps there)
    {0x0018FA10, later_place(kDecideStopLapReturn), kPlanLap},
};
struct PlanCall {
    PlanKind kind{};
    u32 ra = 0;
    u64 a[4]{};
    u32 f12 = 0, f13 = 0;
    u32 car = 0;          // the record the call works on (0: none)
    ex::Car pre, post;    // that record at the entry and the return
    u32 v0 = 0, f0 = 0;   // the results
    u32 out_x = 0, out_y = 0; // 0x1F7658's aim point (*t0, *t1)
    u32 t0 = 0, t1 = 0;
    bool zone = false;        // a later build's braking: the stop zone as the call left it
    ex::StopZone zone_post;
};

// What a port is given besides the record (see the top): the call's inputs at the entry and the interpreter (for anything
// else it reads, such as another car or a course, which the adapter turns into the port's inputs).
struct Call {
    const std::vector<PlanCall>* tape = nullptr; // the AI's plan calls (kPlanSites) of this race step, in order:
    size_t tape0 = 0, tape1 = 0;                 // [tape0, tape1) were made during the call
    size_t pev0 = 0, pev1 = 0;                   // the pit lane's race display events (g_pit_events) during the call
    std::span<const PlanCall> plan() const {
        return tape ? std::span<const PlanCall>(tape->data() + tape0, tape1 - tape0) : std::span<const PlanCall>();
    }
    Ee* ee = nullptr;
    u32 car = 0;
    u64 a[4]{};
    u32 f[8]{}; // f12..f19
    u64 t[4]{}; // t0..t3 (arguments 5..8)
    u32 sp = 0; // $sp at the entry
    u32 vr = 0; // the VU0 random register R at the entry
    std::array<u64, 2> tap{}; // v0 at the port's tap addresses during the call (the conductor's answers to virtual calls)
    std::array<bool, 2> tapped{}; // the call reached the tap
    bool vary = false;        // a variant (run_variants): the inputs come from the changed memory, not the built ones
    std::vector<u8> cond;     // race ports: the conductor bytes [keep, keep + keep_size) at the entry
    const ex::CourseGround* course = nullptr; // our ground query on the race's course file
    ex::StopZone zone;        // a later build's stop zone (the step's conductor + 0xE408) at the entry
    // An output outside the record that differs from the original's (a return value, the caller's array), set by the port.
    mutable std::string out;
    void differ(const std::string& d) const {
        if (out.empty()) out = d;
    }
};

// The race being set up, for its built inputs: its conductor (0: the pad races', car 0 - 0xC8), event (null: the hit set-up's)
// and race mode; the data logger's race info words +0x70 / +0x74 as the race sets them ("#logger" races).
struct RunInfo {
    u32 cond = 0;
    const data::Event* event = nullptr;
    ex::Conductor kind = ex::Conductor::SinglePlayer;
    s32 battle_wear = 0, battle_catchup = 0; // the race info's words +0xC / +0x14
    u32 logger = 0, logger_record = 0;
};
RunInfo g_run;

// The inputs a port is fed outside the record: the built ones (tick_inputs.h: from the program's tables in CORE.GT3 and the
// race's records, as the game builds them), each checked against the original's memory at the call; a variant (Call::vary)
// runs on the memory.
template <class T> T checked(const Call& call, const T& ours, const T& memory) {
    if (call.vary) return memory;
    call.differ(ti::differ(ours, memory));
    return ours;
}
const ti::Built& built() { return ti::built(); }
ex::Statics statics(const Call& call) { return checked(call, built().race.statics, ti::mem_statics(*call.ee)); }
ex::SurfaceBumps bumps(const Call& call) { return checked(call, built().race.bumps, ti::mem_bumps(*call.ee)); }
ex::AidInputs aid_inputs(const Call& call) { return checked(call, built().race.aids, ti::mem_aids(*call.ee, call.car)); }
ex::BrakeHeatConsts brake_heat_consts(const Call& call) { return checked(call, built().race.brake_heat, ti::mem_brake_heat(*call.ee)); }
ex::WearParams wear_params(const Call& call) { return checked(call, built().race.prep.wear, ti::mem_wear(*call.ee, call.car)); }
ex::Condition condition(const Call& call) { return checked(call, built().race.prep.condition, ti::mem_condition(*call.ee)); }
ex::GearSettings gear_settings(const Call& call) { return checked(call, built().race.gears, ti::mem_gears(*call.ee)); }
ex::EffectTables effect_tables(const Call& call) { return checked(call, built().race.effects, ti::mem_effects(*call.ee)); }
ex::SteerFeel steer_feel(const Call& call) { return checked(call, built().race.feel, ti::mem_steer_feel(*call.ee)); }
// The race rule block's +0x18 .. +0x1B (conductor + 0xE358), 2002 Tokyo-Geneva's phase 7 percentages.
ex::ClassGrip class_grip(const Call& call) {
    ex::ClassGrip m{};
    const u32 at = call.ee->r32(call.car + 4) + cond_off(0xE358);
    for (u32 k = 0; k < 4; ++k) m[k] = s8(call.ee->r8(at + k));
    if (call.vary) return m;
    if (m != built().race.class_grip) call.differ("the rule block's class grip percentages differ from the built inputs'");
    return built().race.class_grip;
}
std::array<ee::F, 4> sound_scales(const Call& call) { return checked(call, built().race.sound_scales, ti::mem_sound(*call.ee)); }
ex::Slipstream slipstream(const Call& call) { return checked(call, built().race.prep.slip, ti::mem_slipstream(*call.ee)); }
// Phase 2's inputs outside the record: the race's start countdown (conductor (car + 4) -> race info + 0x5C: the race's start
// sequence, the caller's), the slipstream globals, the conductor's wear curve (+0x84) and the engine condition parameters.
ex::PrepInput prep_input(const Call& call) {
    Ee& e = *call.ee;
    ex::PrepInput in;
    in.start_count = s32(e.r32(e.r32(e.r32(call.car + 4)) + 0x5C));
    in.slip = slipstream(call);
    in.wear = wear_params(call);
    in.condition = condition(call);
    return in;
}
// The course category byte (conductor + 0x8000 + 0x37AD, 0x1F2F28's copy of the course record's +8).
u8 course_byte(const Call& call) {
    const u8 theirs = call.ee->r8(call.ee->r32(call.car + 4) + 0x8000 + k_off(0x37AD));
    if (call.vary) return theirs;
    if (theirs != built().course_category) call.differ(rh::fmt("course category %u vs %u", built().course_category, theirs));
    return built().course_category;
}

// The tyre records the wheels use now must keep their tables where the record mirror reads them (the arrays inside the
// record, as the car build lays them out).
void check_tires(const Call& call, const ex::Car& c) {
    Ee& ee = *call.ee;
    for (int i = 0; i < 4; ++i) {
        const u8 r = c.p.tire_index[size_t(i >> 1)][c.w[size_t(i)].surface];
        const u32 R = call.car + ce::kParams + 0x398 + 0x1B4 * u32(r);
        const ex::TireRecord& t = c.p.tire[r];
        const bool ok = r < 6 && ee.r32(R + 0x4C) == R + 0x54 && ee.r32(R + 0x50) == R + 0x74 && ee.r32(R + 0x9C) == R + 0xA4 &&
                        ee.r32(R + 0xA0) == R + 0xBC && ee.r32(R + 0xD8) == R + 0xE0 && ee.r32(R + 0xDC) == R + 0x110 &&
                        ee.r32(R + 0x154) == R + 0x158 && t.curve_a.n >= 1 && t.curve_a.n <= 8 && t.curve_b.n >= 1 && t.curve_b.n <= 6 &&
                        t.curve_c.n >= 1 && t.curve_c.n <= 12;
        if (!ok) throw std::runtime_error(rh::fmt("wheel %d: tyre record %u not laid out as the mirror reads it", i, r));
    }
}

bool airborne_arg(const Call& call, const ex::Car& c) {
    const bool a = call.a[1] != 0;
    if (a != ex::airborne(c)) throw std::runtime_error("0x1E8598's airborne flag differs from ex::airborne");
    return a;
}

// Where a port's car record is at the function's entry: register `reg` holds car + off. The default is a0 = the car; a
// function given a block of the car (F = car + 0xF84, P = car + 0x10, an axis vector, ...) has a0 = car + the block's offset;
// a callee given something else finds the car in one of its caller's saved registers.
// A later build's compiler keeps other registers than GT3's code: there a car a port finds in a register that is not an
// argument's is the car of the step's per-car routine that runs (g_phase_car: a0 at the entry of the last one called).
u32 g_phase_car = 0;
constexpr Pa kPhaseFns[] = {0x001EADB8, 0x001EA428, 0x001E4D38, 0x001E8978, 0x001E7328, 0x001E3B18, 0x001E8598,
                            0x001E6928, 0x001DFD90, 0x001EB218, 0x001E8190, 0x001E8340, 0x001E6A38};
struct CarAt {
    int reg = 4;
    s32 off = 0;
    bool car_from_v2 = false; // from 2002 Tokyo-Seoul v2.00 on the routine takes the car itself there
    u32 of(Ee& ee) const {
        if (reg > 7 && sim::concept_program(program())) return g_phase_car;
        if (car_from_v2 && program() >= Program::ConceptSeoul2) return ee.g32(reg);
        return ee.g32(reg) - (off < 0 ? u32(off) : car_off(u32(off))); // (`off`: NTSC-U's record offset; below 0: before the record)
    }
};
constexpr CarAt at_a0(s32 off) { return {4, off}; }
constexpr CarAt at_reg(int reg, s32 off = 0) { return {reg, off}; }

// A variant of a call, for states the races rarely or never reach: the record is changed (and memory bytes poked for the
// call, restored afterwards), then the original (called directly) and ours run on a copy of the record.
struct Vary {
    Ee& ee;
    u32 car; // the copy (its pointers to the conductor and course are the car's; the driver object's car pointer D +0x04 is the copy)
    Rng& rng;
    std::vector<std::pair<u32, u8>> poke;
    std::vector<Arg> args; // the call's arguments after a0 (integers to a1 .., floats to f12 ..)
    float u(float lo, float hi) { return rng.uniform(lo, hi); }
    ee::F f(float lo, float hi) { return ee::F(rng.uniform(lo, hi)); }
    void poke32(u32 a, u32 v) {
        for (u32 i = 0; i < 4; ++i) poke.push_back({a + i, u8(v >> (8 * i))});
    }
};

struct Port {
    const char* name;
    Pa fn;    // the original function (NTSC-U addresses; Pa reads the disc's, program_map.h)
    Pa site;  // its call (jal) in the race step 0x1F39D8 or in a phase function; the return address is site + 8
    void (*run)(ex::Car& c, const Call& call);
    CarAt car{}; // where the car record is at the function's entry (default: a0)
    bool (*only)(const ex::Car& pre, const Call& call) = nullptr; // compares only the calls it accepts (the others are counted as skipped)
    std::array<Pa, 2> taps{};     // addresses inside the call where v0 is recorded into Call::tap (0 = none)
    // Variant k of `variants` (a0 = car + car.off): every kVaryEvery ticks of the CPU races, on a changed copy of each car.
    void (*vary)(ex::Car& c, Vary& v, int k) = nullptr;
    int variants = 0;
};
constexpr int kVaryEvery = 30;


// Coverage counts, by what was reached (reported with the phase 7 coverage note).
std::map<std::string, long> g_cover;

// The conductor of the race step that runs. GT3's step keeps it in a register the probes inside the step read
// (race_step_reg); a later build's compiler keeps it elsewhere, so its step's entry (a0) is remembered.
u32 g_step_cond = 0;
u32 step_conductor(Ee& ee) { return sim::concept_program(program()) ? g_step_cond : ee.g32(race_step_reg(20)); }
// The register that holds the car where the driver input 0x1E4D38 reads the pad (0x210740): GT3's $s1, a later build's $s0.
// Phase 3's pad frame is in 0x1E4D38's stack frame, this far below its caller's $sp (a later build's frame is smaller).
u32 pad_frame_below() { return sim::concept_program(program()) ? 0x30 : 0x40; }
int pad_read_car_reg() { return sim::concept_program(program()) ? 16 : 17; }

// The pit box heading the spawn hold turns to (0x1F2F08 -> 0x1F9E10: the AI course's stop line at the car's pit box F +0x4DE),
// built from our AI course (Built::ai) and read only where the hold's mode needs it; its difference from the original's AI
// course object (ti::mem_pit_box_heading) goes to `bad`. A variant (Call::vary) gets the original's.
ee::F box_heading(Ee& e, u32 car, const ex::Car& c, std::string& bad, bool vary = false) {
    if (c.hold < (ex::pal_rate(c) ? 25 : 30) || c.hold >= (ex::pal_rate(c) ? 225 : 270)) return {}; // spawn_offset's mode 2
    const ee::F ours = ex::pit_box_heading(ti::built().ai, c.pit_box), theirs = ti::mem_pit_box_heading(e, e.r32(car + 4), c.pit_box);
    if (vary) return theirs;
    ++g_cover["spawn hold: the pit box heading"];
    if (ours.bits != theirs.bits && bad.empty()) bad = rh::fmt("pit box %d heading 0x%08X vs 0x%08X", c.pit_box, ours.bits, theirs.bits);
    return ours;
}
ee::F box_heading(const Call& call, const ex::Car& c) {
    std::string bad;
    const ee::F r = box_heading(*call.ee, call.car, c, bad, call.vary);
    call.differ(bad);
    return r;
}


// A port's return value against the original's v0 / f0 at the return address.
void check_v0(const Call& call, u32 ours) {
    const u32 theirs = u32(call.ee->gpr[2].lo);
    if (ours != theirs && call.out.empty()) call.out = rh::fmt("v0 0x%X vs 0x%X", ours, theirs);
}
void check_f0(const Call& call, ee::F ours) {
    const u32 theirs = call.ee->fpr[0];
    if (ours.bits != theirs && call.out.empty())
        call.out = rh::fmt("f0 0x%08X vs 0x%08X (%.9g vs %.9g)", ours.bits, theirs, double(ours.host()),
                           double(ee::F::raw(theirs).host()));
}

// The aid block an argument points at: the car's own (car + 0xE8C) or its driver's (car + 0xEF8).
const ex::Aids& aid_block(const ex::Car& c, const Call& call, u64 ptr) {
    if (u32(ptr) == call.car + 0xEF8) return c.ai.aids;
    if (u32(ptr) != call.car + 0xE8C && call.out.empty()) call.out = rh::fmt("aid block at car + 0x%X", u32(ptr) - call.car);
    return c.p.aids;
}
void run_tire_record(ex::Car& c, const Call& call) {
    const u8 surface = u8(call.a[2]);
    if (surface >= 16) {
        call.out = rh::fmt("surface %u", surface);
        return;
    }
    check_v0(call, call.car + 0x10 + 0x398 + 0x1B4 * u32(ex::tire_record_index(c.p, int(call.a[1]), surface)));
}
void run_tcs_on(ex::Car& c, const Call& call) { check_v0(call, ex::tcs_on(aid_block(c, call, call.a[0]))); }
void run_ascc_on(ex::Car& c, const Call& call) { check_v0(call, ex::ascc_on(aid_block(c, call, call.a[0]), aid_inputs(call).ascc)); }
void run_brake_add(ex::Car& c, const Call& call) {
    const u32 k = (u32(call.a[0]) - call.car - ce::kWheel0) / ce::kWheelSize;
    ex::brake_add(c.w[k & 3], ee::F::raw(call.f[0]));
}
// The ASCC also scales the caller's target slips (a3): they start as the tyres' braking slips (0x1E3068 wrote them).
void run_ascc(ex::Car& c, const Call& call) {
    std::array<ee::F, 4> cap;
    for (int i = 0; i < 4; ++i) cap[size_t(i)] = ex::tire_record(c.p, i, c.w[size_t(i)].surface).slip_brake;
    ex::ascc(c, aid_block(c, call, call.a[2]), cap, call.t[0] != 0, aid_inputs(call).ascc);
    for (u32 i = 0; i < 4; ++i)
        if (const u32 theirs = call.ee->r32(u32(call.a[3]) + 4 * i); cap[i].bits != theirs && call.out.empty())
            call.out = rh::fmt("cap[%u] 0x%08X vs 0x%08X", i, cap[i].bits, theirs);
}
void run_tcs(ex::Car& c, const Call& call) { ex::tcs(c, aid_block(c, call, call.a[1])); }
void run_brakes(ex::Car& c, const Call& call) {
    ex::brakes(c, aid_block(c, call, call.a[1]), call.a[2] != 0, aid_inputs(call));
}
void run_scripted_aids(ex::Car& c, const Call& call) { ex::scripted_aids(c, aid_inputs(call)); }

std::string hexf(u32 a, u32 b) { return rh::fmt("0x%08X vs 0x%08X", a, b); }

// The car's conductor (car +4) virtual 39 (vtable at conductor +0xE3A8, entry +0x13C), by the routine it is: 1 in the
// base conductor (single races), 0 in time trial, licence and free run.
bool conductor_marks_flagged(Ee& ee, u32 car) { return ee.r32(ee.r32(ee.r32(car + 4) + cond_off(0xE3A8)) + 8 * u32(conductor_slot(39)) + 4) == pa(0x001D9FD8); }
// Virtual 39's answer for the race's conductor (ex::has_pits), checked against the routine the conductor has.
bool mark_flagged(const Call& call) {
    const bool theirs = conductor_marks_flagged(*call.ee, call.car), ours = ex::has_pits(built().kind);
    if (ours != theirs) call.differ(rh::fmt("conductor virtual 39: %d vs %d", ours, theirs));
    return ours;
}
// The course-out rule of the race's conductor (virtual 40) with the course's pit stretch (0x1FA5B0: the AI course's stop line,
// point 1's to point n - 2's race distance) and lap length, built from our AI course (Built::course_out); its difference from
// the original's AI course object and race course (ti::mem_course_out) goes to `bad`.
ex::CourseOut course_out_rule(Ee& ee, u32 car, std::string& bad) {
    const ex::CourseOut ours = built().course_out();
    if (bad.empty()) bad = ti::differ(ours, ti::mem_course_out(ee, ee.r32(car + 4), ours));
    return ours;
}
ex::CourseOut course_out_rule(const Call& call) {
    std::string bad;
    const ex::CourseOut r = course_out_rule(*call.ee, call.car, bad);
    if (call.vary) return ti::mem_course_out(*call.ee, call.ee->r32(call.car + 4), r);
    call.differ(bad);
    return r;
}

int ground_index(const Call& call, u32 g) { return int((g - call.car - car_off(ce::kGround0)) / ce::kGroundSize); }

// A lane down the course's pit lane: the longest run of checkpoints (in lap order) whose line crosses triangles with flags
// bit 0, the lane through the flagged strip's middle a quarter and three quarters of the way along the run.
rh::Lane pit_lane(const course::Road& road, const ex::CourseGround& g) {
    std::vector<const course::Checkpoint*> cps;
    for (const course::Checkpoint& c : road.checkpoints) cps.push_back(&c);
    std::sort(cps.begin(), cps.end(), [](auto* a, auto* b) { return a->distance < b->distance; });
    struct Mid {
        bool ok = false;
        rh::V2 p;
        double y = 0;
    };
    auto probe = [&](double x, double z, double* y) {
        std::array<ex::Ground, 4> r{};
        for (ex::Ground& q : r) q.x = ee::F(float(x)), q.z = ee::F(float(z)), q.seg = -1;
        g.query(r);
        if (y) *y = r[0].h.host();
        return !r[0].miss && (r[0].flags & 1);
    };
    std::vector<Mid> mids;
    for (const course::Checkpoint* c : cps) {
        int first = -1, last = -1;
        for (int k = 0; k <= 80; ++k) {
            const double t = k / 80.0;
            if (probe(c->x0 + t * (c->x1 - c->x0), c->z0 + t * (c->z1 - c->z0), nullptr)) last = k, first = first < 0 ? k : first;
        }
        Mid m;
        if (first >= 0) {
            const double t = 0.5 * (first + last) / 80.0;
            m.p = {c->x0 + t * (c->x1 - c->x0), c->z0 + t * (c->z1 - c->z0)};
            m.ok = probe(m.p.x, m.p.z, &m.y);
        }
        mids.push_back(m);
    }
    size_t best = 0, best_n = 0;
    for (size_t i = 0; i < mids.size();) {
        size_t j = i;
        while (j < mids.size() && mids[j].ok) ++j;
        if (j - i > best_n) best = i, best_n = j - i;
        i = j + 1;
    }
    rh::Lane ln;
    if (best_n < 8) return ln;
    const rh::V2 a = mids[best + best_n / 4].p, b = mids[best + 3 * best_n / 4].p;
    ln.t = (b - a) * (1.0 / (b - a).len());
    ln.r = {-ln.t.z, ln.t.x};
    ln.m = mids[best + best_n / 2].p;
    ln.y = mids[best + best_n / 2].y;
    ln.distance = cps[best + best_n / 2]->distance;
    ln.ok = true;
    return ln;
}

// A tyre effect request in memory (0x3C bytes) against ours, word by word.
std::string tyre_effect_diff(Ee& ee, u32 q, const ex::TyreEffect& e) {
    const u32 ours[15] = {u32(e.wheel),   u32(e.surface),    e.x.bits,          e.h.bits,        e.z.bits,
                          e.yaw.bits,     e.attr_a.bits,     e.normal.x.bits,   e.normal.y.bits, e.normal.z.bits,
                          e.plane_d.bits, e.smoke_emit.bits, e.slip_ratio.bits, e.hub_vx.bits,   e.rim_speed.bits};
    for (u32 k = 0; k < 15; ++k)
        if (ee.r32(q + 4 * k) != ours[k]) return rh::fmt("wheel %d request +0x%02X ", e.wheel, 4 * k) + hexf(ours[k], ee.r32(q + 4 * k));
    return {};
}

// Phase 3's inputs besides the record: the pad frame (at a pointer, or in 0x1E4D38's stack frame at its $sp - 0x40, which
// nothing writes after the pad read and the conductor's pad hook) and the course category as 0x1F2F68 returns it.
ex::PadFrame pad_frame(Ee& ee, u32 a) {
    ex::PadFrame f;
    f.flags = ee.r8(a), f.kind = ee.r8(a + 1), f.shift = s8(ee.r8(a + 2)), f.steer = s16(ee.r16(a + 4));
    f.throttle = ee.r16(a + 8), f.brake = ee.r16(a + 0xA), f.back = ee.r16(a + 0xC), f.third = ee.r16(a + 0xE);
    return f;
}
u8 course_kind(const Call& call) { return ex::course_category(course_byte(call)); }

// The driver input's constants (ex::PadConstants), checked against the memory with the lock curves' arrays inline in P (the
// mirror reads them there).
ex::PadConstants pad(const Call& call) {
    for (u32 off : {0x5Cu, 0x98u, 0xD4u}) {
        const u32 t = call.car + 0x10 + off;
        Ee& ee = *call.ee;
        if (ee.r32(t + 4) != t + 0xC || ee.r32(t + 8) != t + 0x24 || s16(ee.r16(t)) > 6) call.differ(rh::fmt("lock curve P+0x%X", off));
    }
    return checked(call, built().race.pad, ti::mem_pad(*call.ee));
}

ex::F freg(const Call& call, int i) { return ex::F::raw(call.f[i - 12]); }

// Phase 7 coverage (from our ports' own transitions): layouts and differentials, clutch states and transitions, gear changes
// by gearbox mode, LSD and centre-clutch locks, the fuel cut, turbo and power-cap engines (g_cover, declared above).
void cover_clutch(const char* where, const ex::Car& pre, const ex::Car& post) {
    if (pre.clutch != post.clutch) ++g_cover[rh::fmt("clutch %d->%d (%s)", pre.clutch, post.clutch, where)];
}
void p7_brakes_and_clutch(ex::Car& c, const Call&) {
    const ex::Car pre = c;
    ex::brakes_and_clutch(c);
    cover_clutch("engage", pre, c);
    if (ex::F() < c.handbrake) ++g_cover["handbrake"];
    if (pre.clutch == 0 && !(pre.clutch_req == ex::F()) && c.clutch_req == ex::F()) ++g_cover["launch: request cleared"];
    if (pre.clutch == 0 && c.clutch == 2 && !(c.speed < ex::F(2.78f) && ex::F(-2.78f) < c.speed))
        ++g_cover[c.speed < ex::F() ? "engage while moving backwards" : "engage while moving forwards"];
    const int style = c.control != 1 && c.driver_kind == 0 ? c.pedal_style : 0;
    if (c.clutch == 3 && style != 3 && style != 6) ++g_cover[c.throttle == ex::F() ? "shift: throttle cut" : "shift: throttle blip"];
}
// The later builds' AWD system 3 settings as built, checked for a car with that system against the program's data (the
// two floats the driveline builder writes there when it builds such a car, the switch byte).
ex::Awd3 awd3(const Call& call, const ex::Car& c) {
    const ex::Awd3 k = built().race.statics.awd3;
    using T = ex::ProgramData::Table;
    if (built().data.has(T::Awd3) && !call.vary && c.p.d.layout == 5) {
        Ee& e = *call.ee;
        const u32 at = built().data.address(T::Awd3, 0);
        if (e.r32(at + 4) != k.fade_start.bits || e.r32(at + 0xC) != k.fade_floor.bits || e.r8(at + 2) != k.negative_full)
            call.differ(rh::fmt("AWD system 3 settings: fade 0x%08X / 0x%08X, switch %u vs 0x%08X / 0x%08X, %u", k.fade_start.bits,
                                k.fade_floor.bits, k.negative_full, e.r32(at + 4), e.r32(at + 0xC), e.r8(at + 2)));
    }
    return k;
}
// The later builds' stepless gearbox settings as built, checked for a car with such a gearbox against the floats its
// routine leaves in the program's data.
ex::Stepless stepless(const Call& call, const ex::Car& c) {
    const ex::Stepless k = built().race.statics.stepless;
    using T = ex::ProgramData::Table;
    if (built().data.has(T::Stepless) && !call.vary && u8(c.p.d.kind - 1) < 2) {
        Ee& e = *call.ee;
        const u32 at = built().data.address(T::Stepless, 0);
        // (the routine computes them at its first call, its flag at +0x18 then set: before that the words are the program's
        // own initial values, which nothing reads)
        if (e.r8(at + 0x18) != 0 &&
            (e.r32(at + 4) != k.full.bits || e.r32(at + 8) != k.none.bits || e.r32(at + 0x10) != k.down.bits || e.r32(at + 0x14) != k.up.bits))
            call.differ(rh::fmt("stepless gearbox settings: 0x%08X 0x%08X 0x%08X 0x%08X vs 0x%08X 0x%08X 0x%08X 0x%08X", k.full.bits,
                                k.none.bits, k.down.bits, k.up.bits, e.r32(at + 4), e.r32(at + 8), e.r32(at + 0x10), e.r32(at + 0x14)));
    }
    return k;
}
// The later builds' layout 7 settings as built, checked for a layout 7 car against the floats the set-up routine leaves in
// its globals (found from the routine that reads the Hybrid table: its first store of $f3, the front motors' rating).
void check_hybrid(const Call& call, const ex::Car& c) {
    const ex::Hybrid& k = built().race.statics.hybrid;
    using T = ex::ProgramData::Table;
    if (call.vary || !ex::hybrid_car(c, k) || !built().data.has(T::Hybrid)) return;
    Ee& e = *call.ee;
    const u32 site = built().data.table_site(T::Hybrid).at;
    u32 g = 0;
    for (u32 i = 0; i < 0x100 && site != 0 && g == 0; ++i)
        if (const u32 w = e.r32(site + 4 * i); (w & 0xFFFF0000u) == 0xE7830000u) g = e.g32(28) + u32(s32(s16(u16(w)))); // swc1 $f3, x($gp)
    if (g == 0) return call.differ("the layout 7 settings' globals not found");
    const ex::F f[] = {k.front, k.rear, k.front_drive, k.rear_drive, k.front_regen, k.rear_regen, k.front_scale, k.capacity,
                       k.front_charge_cap, k.rear_charge_cap, k.idle_rpm, k.idle_gain, k.idle_share, k.idle_power, k.low, k.high,
                       k.reserve, k.front_share, k.drive_accel, k.drive_lat, k.drive_dead, k.regen_accel, k.regen_lat, k.regen_dead,
                       k.flow_scale};
    for (u32 i = 0; i < std::size(f); ++i)
        if (e.r32(g + 4 * i) != f[i].bits) return call.differ(rh::fmt("layout 7 settings: +0x%X 0x%08X vs 0x%08X", 4 * i, f[i].bits, e.r32(g + 4 * i)));
    ++g_cover["layout 7 settings checked"];
}
void p7_axle_step(ex::Car& c, const Call& call) {
    const ex::Car pre = c;
    ex::axle_step(c, awd3(call, c), stepless(call, c), built().race.statics.hybrid);
    if (const ex::Hybrid& h = built().race.statics.hybrid; ex::hybrid_car(pre, h) && pre.clutch != 0 && u8(c.p.d.kind - 1) >= 2) {
        ex::Car q = pre;
        const ex::F d = ex::hybrid_demand(q, h);
        ++g_cover[rh::fmt("layout 7 axle step: rear motor %s%s", ex::F() < d ? "driving" : "charging",
                          ex::hybrid_level(q, h) == ex::F::raw(0x3F800000) ? ", store above the high mark" : ex::hybrid_level(q, h) == ex::F() ? ", store at the low mark" : "")];
        if (ex::hybrid_store(c) == ex::F()) ++g_cover["layout 7 axle step: store emptied"];
        if (ex::hybrid_store(c) == h.capacity) ++g_cover["layout 7 axle step: store full"];
        if (pre.later.special[5] != c.later.special[5] && (s8(c.later.special[5]) == 100 || s8(c.later.special[5]) == -100)) ++g_cover["layout 7 axle step: flow at its limit"];
    }
    if (u8(c.p.d.kind - 1) < 2) ++g_cover[rh::fmt("axle step: a stepless gearbox of kind %u%s", c.p.d.kind, c.later.ratio.bits != pre.later.ratio.bits ? ", the ratio moved" : "")];
    if (c.p.d.unequal) ++g_cover["axle step: unequal axles"];
    static const char kTypes[] = "frnmhvyt";
    auto letter = [](u8 t) { return t < 8 ? kTypes[t] : '?'; };
    ++g_cover[rh::fmt("layout %d diffs %c/%c", c.p.d.layout, letter(c.p.d.diff_type[0]), letter(c.p.d.diff_type[1]))];
    ++g_cover[rh::fmt("clutch state %d", pre.clutch)];
    if (!(c.p.e.boost_max == ex::F())) {
        ++g_cover["turbo engine"];
        for (size_t i = 0; i < 2; ++i)
            if (!(c.p.e.turbo_peak[i] == ex::F()))
                ++g_cover[rh::fmt("turbo stage shape %d, %s", c.p.e.turbo_shape[i],
                                  c.turbo_lag[i] < c.p.e.turbo_knee[i] ? "below the knee" : c.turbo_lag[i] < 3 * c.p.e.turbo_knee[i] ? "past the knee" : "past 3 knees")];
    }
    if (c.p.e.power_cap_on) ++g_cover["power cap"];
    if (pre.centre_locked != c.centre_locked) ++g_cover[rh::fmt("centre clutch %d->%d", pre.centre_locked, c.centre_locked)];
    if (c.centre_locked && c.axle_speed[0] == c.axle_speed[1]) ++g_cover["centre clutch locked step"];
    if (pre.rev_limiter != c.rev_limiter) ++g_cover[rh::fmt("fuel cut %d->%d", pre.rev_limiter, c.rev_limiter)];
    if (pre.blowoff_state != c.blowoff_state) ++g_cover[rh::fmt("blow-off %d->%d", pre.blowoff_state, c.blowoff_state)];
}
void p7_torque_vectoring(ex::Car& c, const Call&) {
    const ex::Car pre = c;
    const ex::Hybrid& h = built().race.statics.hybrid;
    ex::torque_vectoring(c, h);
    if (!ex::hybrid_car(pre, h)) return;
    ex::Car q = pre;
    const ex::F d = ex::hybrid_demand(q, h), lat = ex::hybrid_wanted_lateral(q);
    const bool drive = !(c.clutch_torque == ex::F()) || ex::hybrid_store(c).bits < ex::hybrid_store(pre).bits;
    ++g_cover[rh::fmt("layout 7 front motors: demand %s, wanted lateral %s%s", ex::F() < d ? "> 0" : "<= 0", lat < ex::F() ? "< 0" : ex::F() < lat ? "> 0" : "0",
                      drive ? ", drawing" : "")];
    for (size_t i = 0; i < 2; ++i) {
        ex::F v = pre.w[i].rim_speed;
        if (v < ex::F()) v = -v;
        if (v < ex::F::raw(0x3E8E38E4)) ++g_cover[pre.brake_pedal == ex::F() ? "layout 7 front motors: a slow wheel, off the brake" : "layout 7 front motors: a slow wheel, braking"];
    }
    if (pre.gear == 0) ++g_cover["layout 7 front motors: reverse"];
}
void p7_wheel_speeds(ex::Car& c, const Call&) {
    const ex::Car pre = c;
    ex::wheel_speeds(c);
    for (size_t a = 0; a < 2; ++a)
        if (pre.diff_lock[a] != c.diff_lock[a]) ++g_cover[rh::fmt("LSD lock %d->%d", pre.diff_lock[a], c.diff_lock[a])];
}
void p7_engine_speed(ex::Car& c, const Call&) {
    const ex::Car pre = c;
    ex::engine_speed(c, built().race.statics.hybrid);
    if (const ex::Hybrid& h = built().race.statics.hybrid; ex::hybrid_car(pre, h) && pre.clutch == 0)
        ++g_cover[ex::hybrid_store(pre) / h.capacity < ex::F::raw(0x3F800000) ? (c.throttle.bits != pre.throttle.bits ? "layout 7 open clutch: charging, throttle raised" : "layout 7 open clutch: charging") : "layout 7 open clutch: store full"];
    cover_clutch("engine speed", pre, c);
    if (pre.clutch >= 2 && c.clutch == 1 && c.shift_lockout != 12) ++g_cover["clutch pedal lock"];
}
void p7_engine_output(ex::Car& c, const Call& call) {
    const ex::F x = freg(call, 12);
    const ex::F t = ex::engine_output(c, x);
    if (c.p.e.power_cap_on) {
        const ex::F raw = (c.rev_limiter == 0 ? x * (c.torque_wot + c.torque_friction) : ex::F()) - c.torque_friction;
        const ex::F p = raw * c.omega, cap = c.p.e.power_cap;
        ++g_cover[p < cap * ex::F::raw(0x3F666666) ? "power cap: below the band" : p < cap ? "power cap: in the band" : "power cap: at the cap"];
    }
    check_f0(call, t);
}
void p7_awd5_gain(ex::Car& c, const Call& call) {
    const ex::F g = ex::awd5_gain(c, freg(call, 12), freg(call, 13), freg(call, 14), awd3(call, c));
    ++g_cover[g == ex::F::raw(0x47BF6800) ? "L5 gain: full" : g == ex::F::raw(0x3F800000) ? "L5 gain: 1" : "L5 gain: coupling"];
    check_f0(call, g);
}
void p7_gear_step(ex::Car& c, const Call& call) {
    const ex::Car pre = c;
    const ex::GearSettings s = gear_settings(call);
    ex::gear_step(c, s);
    const int style = c.control != 1 && c.driver_kind == 0 ? c.pedal_style : 0;
    ++g_cover[rh::fmt("gearbox mode %d, controller mode %d", style, s.mode)];
    if (pre.kickdown != c.kickdown) ++g_cover["kick-down level change"];
    if (pre.hold != 0) ++g_cover["gear decision during the hold"];
    if (pre.gear != c.gear)
        ++g_cover[rh::fmt("mode %d: %s", style, c.gear == 0 ? "into reverse" : pre.gear == 0 ? "out of reverse" : c.gear > pre.gear ? "up" : "down")];
    cover_clutch("gear change", pre, c);
}

// The race object's (race info + 0x3C: RaceSolitaire) words past its recorders by their NTSC-U offsets: PAL's object is 0x3FC
// longer before them, NTSC-J's 0x602C shorter (its ghost switch +0x1B778, the recorders' selector +0x1B77C).
u32 race_object_word(u32 ntsc_u_off) {
    return program() == Program::Pal ? ntsc_u_off + 0x3FC : program() == Program::NtscJ ? ntsc_u_off - 0x602C : ntsc_u_off;
}
constexpr u32 kGhostSwitch = 0x217A4, kRecorderSelect = 0x217A8;
// The size the harness allocates for a stand-in race object.
u32 race_object_size() { return race_object_word(0x217B0); }

// The data logger's flag as the vehicle tick's draw mode reads it (NTSC-J's does not read it: 0).
u32 run_x70() { return program() == Program::NtscJ ? 0 : g_run.logger; }
// 0x1E0668's race words: [car +0] + 0x70 (car +0: the race info), and (only when that is 0) [[car +0] + 0x3C] + 0x217A4.
// NTSC-J's reads only the switch.
void draw_mode_words(Ee& ee, u32 car, u32& x70, u32& ghost_switch) {
    const u32 k = ee.r32(car);
    x70 = program() == Program::NtscJ ? 0 : ee.r32(k + 0x70);
    ghost_switch = x70 ? 0 : ee.r32(ee.r32(k + 0x3C) + race_object_word(kGhostSwitch));
}
// The race's words for the draw mode: the data logger's flag as the race sets it, no ghost switch (the races here show no
// ghost), checked against the memory.
void draw_words(const Call& call, u32& x70, u32& ghost_switch) {
    draw_mode_words(*call.ee, call.car, x70, ghost_switch);
    if (call.vary) return;
    if (x70 != run_x70() || ghost_switch != 0)
        call.differ(rh::fmt("draw mode words 0x%X / 0 vs 0x%X / 0x%X", run_x70(), x70, ghost_switch));
    x70 = run_x70(), ghost_switch = 0;
}
// The wear flags' conductor answers: virtual 39 (ex::has_pits), 0x1D9FE0's (virtual 35's, ex::pit_mode, asked for a car with
// control source 0) and the race-flow byte (the race's), checked against the taps where the call reached them.
ex::PitMode pit_mode(const Call& call, const ex::Car& c) {
    ex::PitMode m;
    m.pits = ex::has_pits(built().kind);
    m.mode = ex::pit_mode(built().kind, built().course_category, built().data.behaviour().rule(sim::Rule::LicenceJudge));
    m.kind = built().flow;
    const u8 flow = call.ee->r8(call.ee->r32(call.car + 4) + 0x8000 + k_off(0x37AC));
    if (flow != m.kind) call.differ(rh::fmt("race-flow byte %u vs %u", m.kind, flow));
    if (call.tapped[0] && (u32(call.tap[0]) != 0) != m.pits) call.differ(rh::fmt("virtual 39 %d vs %d", m.pits, int(call.tap[0])));
    if (call.tapped[1] && s32(u32(call.tap[1])) != (c.control ? 0 : m.mode))
        call.differ(rh::fmt("0x1D9FE0 %d vs %d", c.control ? 0 : m.mode, int(call.tap[1])));
    return m;
}

// Phase 12's variants: reversing, kerbs and every surface class at low and high slip, airborne wheels, the race hold, the
// force feedback's modes and curves, the effect states of 0x1E0668, engine condition words at their limits, a finished car.
void vary_rumble(ex::Car& c, Vary& v, int k) {
    if (k == 0) {
        c.race_hold = 1;
        return;
    }
    const float fx = k == 1 ? 20000.0f : 200000.0f;
    for (auto& w : c.w) {
        w.damper_f = v.f(-5000, 40000), w.load = v.f(0, 8000), w.surface = u8(v.rng.range(0, 2)), w.slip_ratio = v.f(-1.2f, 1.2f);
        w.fx_drive = v.f(-fx, fx), w.fx_brake = v.f(-fx, fx);
    }
    c.speed = v.f(-40, 40), c.hit_rumble = v.f(-0.1f, 0.5f);
}

void vary_steer_force(ex::Car& c, Vary& v, int k) {
    if (k < 4) {
        v.poke.push_back({pa(0x002D4661), u8(k < 3 ? k : v.rng.range(3, 4))}); // filter mode (4: none)
        v.poke.push_back({pa(0x002D465F), u8(v.rng.range(0, 1))});         // lateral term scaled by F +0x420
        v.poke32(pa(0x0049D050), u32(v.rng.range(0, 2)));                  // curve kind, and arbitrary parameters
        v.poke32(pa(0x0049D054), ee::F(v.u(0, 0.5f)).bits), v.poke32(pa(0x0049D058), ee::F(v.u(-0.2f, 0.2f)).bits),
            v.poke32(pa(0x0049D05C), ee::F(v.u(-2, 2)).bits);
        c.ffb = v.f(-1.2f, 1.2f), c.ffb_count = s32(v.rng.range(0, 70));
        for (int i = 0; i < 2; ++i) c.w[size_t(i)].force_a = v.f(-20000, 20000), c.t[size_t(i)].slip_angle = v.f(-0.6f, 0.6f);
        c.wheel_pos = s16(v.rng.range(-128, 127)), c.wheel_pos_prev = s16(v.rng.range(-128, 127)), c.x420 = v.f(0, 1);
    }
    c.steer_cmd = v.f(-0.6f, 0.6f);
    if (k == 4) c.vel.x = v.f(-0.5f, 0.5f), c.vel.y = v.f(-0.5f, 0.5f);
}

void vary_tyre_effects(ex::Car& c, Vary& v, int k) {
    static const u8 kinds[4] = {0, 1, 2, 0xFF};
    v.poke.push_back({v.ee.r32(v.car + 4) + cond_off(0xB7AD), kinds[v.rng.range(0, 3)]});
    if (k == 0) // the curves' input selectors
        for (u32 ch = 0; ch < 3; ++ch)
            for (u32 cl = 0; cl < 3; ++cl) {
                const u32 sel = pa(0x002D35B8) + 0xA0 * ch + 0x28 * cl;
                v.poke.push_back({sel, u8(v.rng.range(0, 1))}), v.poke.push_back({sel + 0x1F, u8(v.rng.range(0, 1))});
            }
    c.dirt = v.f(0.79f, 0.8f);
    for (size_t i = 0; i < 4; ++i) {
        auto& w = c.w[i];
        w.load = v.rng.chance(0.3) ? ee::F() : v.f(0, 8000);
        w.smoke_count = u8(v.rng.range(0, 6));
        w.speed_ramp = v.f(0, 1);
        w.slip_ratio = v.f(-1.2f, 1.2f);
        c.t[i].slip_angle = v.f(-1, 1);
        if (k == 0) w.surface = u8(v.rng.range(0, 15)), w.rim_speed = v.f(-40, 40), w.hub_vx = v.f(-40, 40), w.hub_vy = v.f(-20, 20);
        if (k == 1) w.surface = v.rng.chance(0.5) ? 1 : 13, w.rim_speed = v.f(-5, 5), w.hub_vx = v.f(-5, 5);
        if (k == 2) w.surface = u8(v.rng.range(2, 6)), w.hub_vy = v.f(10, 40), w.rim_speed = v.f(-40, 60);
    }
}

void vary_engine_sound(ex::Car& c, Vary& v, int) {
    c.turbo_load = v.f(-0.5f, 2), c.clutch = u8(v.rng.range(0, 3)), c.blowoff = v.f(0, 1);
    c.x4e4 = u8(v.rng.range(0, 255)), c.x4e5 = u8(v.rng.range(0, 255)), c.turbo_spool = u8(v.rng.range(0, 255));
}

void vary_effects_reset(ex::Car& c, Vary& v, int k) {
    const u32 cond = v.ee.r32(v.car);
    c.ghost = k == 3 ? 0 : 1;
    c.control = s8(v.rng.range(0, 2)), c.driver_kind = u8(v.rng.range(0, 2));
    v.poke32(cond + 0x70, k == 1 ? 1 : 0);
    v.poke32(v.ee.r32(cond + 0x3C) + race_object_word(kGhostSwitch), k == 2 ? 1 : 0);
    for (auto& w : c.w) w.smoke_emit = v.f(0, 1), w.slip_intensity = v.f(0, 1), w.skid_pitch = v.f(0, 2);
    c.x4e4 = u8(v.rng.range(1, 127)), c.x4e5 = u8(v.rng.range(1, 127)), c.turbo_spool = u8(v.rng.range(1, 127));
}

void vary_view(ex::Car& c, Vary& v, int) {
    c.lag_heading = v.f(-12, 12), c.lag_pitch = v.f(-12, 12), c.heading = v.f(-7, 7), c.pitch = v.f(-1, 1);
    c.lag_heading_rate = v.f(-5, 5), c.lag_pitch_rate = v.f(-5, 5), c.lag_heading_acc = v.f(-50, 50);
}

void vary_speeds(ex::Car& c, Vary& v, int k) {
    if (k == 0) {
        c.odometer = 0xEE6B27A6u - u32(v.rng.range(0, 3));
        c.odo_carry = v.f(0, 3), c.engine_wear = ee::F(600000.0f - v.u(0, 1)), c.oil = v.f(-0.5f, 0);
        c.dist_total = ee::F(8000000.0f - v.u(0, 1)), c.finished = 1, c.max_speed = v.f(0, 100);
    } else if (k == 1) {
        c.oil = ee::F(300000.0f - v.u(0, 1)), c.vel = {-c.vel.x, -c.vel.y, c.vel.z}, c.speedo = v.f(0, 80);
        for (auto& w : c.w) w.flags = u8(v.rng.chance(0.5) ? 0 : v.rng.range(1, 15));
        c.p.d.diff_type = {u8(v.rng.range(0, 1)), u8(v.rng.range(0, 1))};
    } else {
        c.vel = {}, c.odo_carry = {};
    }
}

void vary_wheel_spin(ex::Car& c, Vary& v, int k) {
    if (k == 0) {
        c.race_hold = 1;
        c.heading = ee::F(-6.2831855f - v.u(0, 0.1f));
        return;
    }
    for (auto& w : c.w) w.rim_speed = v.f(-80, 80), w.spin = v.f(-6.28f, 6.28f);
    c.heading = v.f(-6.4f, 6.4f), c.steer_cmd = c.p.max_lock * v.f(-2, 2);
}

// The race's progress query on our road (the race flow's and the AI's stop point, set per race).
const ex::CourseQuery* g_query = nullptr;

// A later build's stop zone in its conductor (+0xE408 .. +0xE47B, sim::exact::StopZone; GT3's releases: none, with the car
// count), and ours against it (its points and counts only while it is on).
constexpr u32 kZoneAt = 0xE408; // (the later builds' conductor's own offset)
ex::StopZone read_zone(Ee& ee, u32 cond) {
    ex::StopZone z;
    z.cars = int(ee.r32(cond + race_setup::kK8 + k_off(0x6384)));
    if (!sim::concept_program(program())) return z;
    const u32 a = cond + kZoneAt;
    z.active = ee.r8(a);
    if (z.active != 1) return z; // (what it holds then is read by nothing)
    z.base = ee::F::raw(ee.r32(a + 0xC)), z.spacing = ee::F::raw(ee.r32(a + 0x10));
    for (u32 i = 0; i < 8; ++i) {
        z.stops[i] = ee.r8(a + 2 + i);
        z.x[i] = ee::F::raw(ee.r32(a + 0x14 + 4 * i)), z.y[i] = ee::F::raw(ee.r32(a + 0x34 + 4 * i)), z.dist[i] = ee::F::raw(ee.r32(a + 0x54 + 4 * i));
    }
    return z;
}
std::string zone_differ(const ex::StopZone& a, const ex::StopZone& b) {
    if (a.active != b.active) return rh::fmt("stop zone %u vs %u", a.active, b.active);
    if (a.active != 1) return {};
    if (a.base.bits != b.base.bits || a.spacing.bits != b.spacing.bits)
        return rh::fmt("stop zone 0x%08X + 0x%08X vs 0x%08X + 0x%08X", a.base.bits, a.spacing.bits, b.base.bits, b.spacing.bits);
    for (size_t i = 0; i < 8; ++i) {
        if (a.stops[i] != b.stops[i]) return rh::fmt("stop zone: car %zu's stops %u vs %u", i, a.stops[i], b.stops[i]);
        if (a.x[i].bits != b.x[i].bits || a.y[i].bits != b.y[i].bits || a.dist[i].bits != b.dist[i].bits)
            return rh::fmt("stop zone: car %zu's point 0x%08X 0x%08X 0x%08X vs 0x%08X 0x%08X 0x%08X", i, a.x[i].bits, a.y[i].bits,
                           a.dist[i].bits, b.x[i].bits, b.y[i].bits, b.dist[i].bits);
    }
    return {};
}

// sim::exact::AiPlan on a tape of the original's plan calls (PlanCall): each call takes the next entry, which must be the same
// call with the same arguments; the record changes the original's call made are applied and its results returned. The pit
// stop line's points come from the original's memory (the course's AI lines at D +0x08).
class TapePlan final : public ex::AiPlan {
public:
    // `zone`: a later build's stop zone as the original had it when the port's call began (the braking calls on the tape
    // leave it as theirs did).
    TapePlan(std::span<const PlanCall> tape, Ee& ee, u32 car, const ex::StopZone* zone = nullptr) : tape_(tape), ee_(ee), car_(car) {
        if (zone) zone_ = *zone;
    }
    ex::StopZone* stop_zone() override { return sim::concept_program(program()) ? &zone_ : nullptr; }
    std::string err;
    bool done() const { return at_ == tape_.size(); }
    size_t used() const { return at_; }

    ex::F braking(ex::Car& c) override {
        const PlanCall* e = next(kPlanBraking, "0x1F7088");
        if (!e) return {};
        ce::copy_changed(c, e->pre, e->post);
        if (e->zone) zone_ = e->zone_post;
        return ex::F::raw(e->f0);
    }
    ex::F target(const ex::Car&, int line, int hint, ex::F look, ex::F& dx, ex::F& dy) override {
        const PlanCall* e = next(kPlanTarget, "0x1F7658");
        if (!e) return look;
        if (u32(e->a[2]) != u32(line) || u32(e->a[3]) != u32(hint) || e->f12 != look.bits)
            fail(rh::fmt("0x1F7658(line %d, hint %d, L 0x%08X) vs (%u, %u, 0x%08X)", line, hint, look.bits, u32(e->a[2]),
                         u32(e->a[3]), e->f12));
        dx = ex::F::raw(e->out_x), dy = ex::F::raw(e->out_y);
        return ex::F::raw(e->f0);
    }
    // (2002 Tokyo-Geneva's line heading is no plan call: computed on the course object the races use, ai_course)
    ex::F line_heading(const ex::Car&, int line, int hint, ex::F s) override;
    void speed_control(ex::Car& c, ex::F target, ex::F gain) override {
        const PlanCall* e = next(kPlanSpeed, "0x1F7590");
        if (!e) return;
        if (e->f12 != target.bits || e->f13 != gain.bits)
            fail(rh::fmt("0x1F7590(0x%08X, 0x%08X) vs (0x%08X, 0x%08X)", target.bits, gain.bits, e->f12, e->f13));
        ce::copy_changed(c, e->pre, e->post);
    }
    bool request_line(ex::Car& c, int line, bool at_once) override {
        const PlanCall* e = next(kPlanRequest, "0x1F8980");
        if (!e) return false;
        if (u32(e->a[1]) != u32(line) || (e->a[2] != 0) != at_once)
            fail(rh::fmt("0x1F8980(line %d, %d) vs (%u, %u)", line, int(at_once), u32(e->a[1]), u32(e->a[2])));
        ce::copy_changed(c, e->pre, e->post);
        return e->v0 != 0;
    }
    void replan(ex::Car&) override { fail("0x1F6A20 (the AI's take-over) is not on the plan tape"); }
    s32 lane_left(int line, int hint, ex::F s) override { return lane(kPlanLaneLeft, "0x1F9CE0", line, hint, s); }
    s32 lane_right(int line, int hint, ex::F s) override { return lane(kPlanLaneRight, "0x1F9D38", line, hint, s); }
    ex::F lap_length() override {
        const PlanCall* e = next(kPlanLap, "0x18FA10");
        return e ? ex::F::raw(e->f0) : ex::F();
    }
    s32 stop_count() override { return s32(ee_.r32(stop_line())); }
    ex::F stop_distance(s32 k) override { return ex::F::raw(ee_.r32(stop_line() + 4 + u32(0x28 * k) + 0x14)); }

private:
    std::span<const PlanCall> tape_;
    Ee& ee_;
    u32 car_;
    size_t at_ = 0;
    ex::StopZone zone_;

    void fail(const std::string& d) {
        if (err.empty()) err = rh::fmt("plan call %zu: ", at_) + d;
    }
    const PlanCall* next(PlanKind k, const char* what) {
        if (at_ >= tape_.size() || tape_[at_].kind != k) {
            fail(rh::fmt("%s called, the original made %s", what, at_ < tape_.size() ? rh::fmt("call kind %d", tape_[at_].kind).c_str() : "no more calls"));
            return nullptr;
        }
        return &tape_[at_++];
    }
    s32 lane(PlanKind k, const char* what, int line, int hint, ex::F s) {
        const PlanCall* e = next(k, what);
        if (!e) return -1;
        if (u32(e->a[1]) != u32(line) || u32(e->a[2]) != u32(hint) || e->f12 != s.bits)
            fail(rh::fmt("%s(line %d, hint %d, s 0x%08X) vs (%u, %u, 0x%08X)", what, line, hint, s.bits, u32(e->a[1]), u32(e->a[2]), e->f12));
        return s32(e->v0);
    }
    u32 stop_line() { // [[D +0x08] +0x04] +0x18: game line 4
        const u32 l = ee_.r32(ee_.r32(ee_.r32(car_ + 0xEC8 + 8) + 4) + 0x18);
        if (!l) fail("no line 4");
        return l;
    }
};

// The AI driver's functions in the original (0x1E35D8, 0x1E4D38 and the 0x1F6xxx - 0x1F8xxx ones of sim::exact's tick_ai.cpp, the
// pit lane's of tick_pit.cpp):
// every instruction they execute in the races is marked, and the report lists the stretches no race reached.
const std::pair<Pa, Pa> kAiCode[] = {
    {0x001E35D8, 0x001E3708}, {0x001E4D38, 0x001E4E74}, {0x001F6588, 0x001F6674}, {0x001F6D70, 0x001F7074}, {0x001F77B8, 0x001F780C},
    {0x001F7810, 0x001F7944}, {0x001F7AE0, 0x001F8634}, {0x001F8A00, 0x001F8B9C},
    // the pit lane and the end of a run (pit_lane.h)
    {0x001E3BF0, 0x001E3E90}, {0x001EAB80, 0x001EAC5C}, {0x001EACB8, 0x001EADB4}, {0x001EDB40, 0x001EE284}, {0x001F6518, 0x001F653C},
};

// A port of the AI on the call's plan tape, which it must use up.
template <class Fn> void with_tape(const Call& call, Fn&& fn) {
    TapePlan plan(call.plan(), *call.ee, call.car, &call.zone);
    fn(plan);
    if (!plan.err.empty()) call.differ(plan.err);
    else if (!plan.done()) call.differ(rh::fmt("plan tape: %zu of %zu calls used", plan.used(), call.plan().size()));
}

// The collision step's flags of a car's body (conductor + 0xBBB4 + 0xC + 0x4F0 index, +0x434 B / +0x438 A) the AI reads.
u32 body_flags(Ee& e, u32 car) { return e.r32(car + 4) + cond_off(0xBBB4) + 0x4F0 * u32(e.r8(car + ce::kF + f_off(0x3E8))) + 0xC; }

// The AI's variants: states the races rarely or never reach.
void vary_ai_steer(ex::Car& c, Vary& v, int) {
    c.ai.steer = v.f(-0.8f, 0.8f), c.ai.steer_rate = v.f(0, 3);
    const ex::F t = v.rng.chance(0.2) ? c.ai.steer : v.f(-0.8f, 0.8f);
    v.args = {Arg::fbits(t.bits)};
}
void vary_ai_stop(ex::Car& c, Vary& v, int k) {
    v.args = {Arg::fbits(v.f(-0.8f, 0.8f).bits)};
    c.driver_kind = u8(5 + k);
    const float s = v.rng.chance(0.5) ? 0.05f : 3.0f, w = v.rng.chance(0.5) ? 0.04f : 1.0f;
    c.vel = {v.f(-s, s), v.f(-s, s), v.f(-s, s)};
    c.yaw_rate = v.f(-w, w), c.pitch_rate = v.f(-w, w), c.roll_rate = v.f(-w, w);
    c.ai.brake = v.f(0, 1), c.ai.steer = v.f(-0.5f, 0.5f), c.ai.blend = v.rng.chance(0.5) ? ex::F() : v.f(0, 1);
}
void vary_ai_hold(ex::Car& c, Vary& v, int k) {
    v.args = {Arg::integer(k == 0 ? v.rng.range(0, 12) : v.rng.range(13, 300))};
    const s64 up = c.p.d.up_rpm;
    c.rpm = s16(v.rng.chance(0.5) ? v.rng.range(0, 2 * up + 1000) : v.rng.range(3 * up / 4 - 2, 15 * up / 16 + 2));
    c.blip = u8(v.rng.range(0, 1)), c.rank = s8(v.rng.range(0, 7)), c.ai.throttle = v.f(-0.1f, 1.1f);
}
void vary_ai_traffic(ex::Car& c, Vary& v, int) {
    ex::Driver& d = c.ai;
    static const u8 kLines[] = {1, 2, 3, 4, 6, 7};
    if (v.rng.chance(0.5)) d.line = kLines[v.rng.range(0, 5)];
    d.status = s8(v.rng.range(0, 5)), d.next_kind = u8(v.rng.range(0, 2));
    d.cooldown = s16(v.rng.chance(0.7) ? 0 : v.rng.range(1, 3));
    static const float kContact[] = {0, 0.25f, 0.5f, 1};
    d.contact = ee::F(kContact[v.rng.range(0, 3)]), d.throttle = v.f(0, 1);
    for (u8& z : d.zone) z = u8(v.rng.chance(0.6) ? 0 : v.rng.range(1, 2));
}
void vary_ai_gear_hint(ex::Car& c, Vary& v, int) {
    c.ai.mode = u8(v.rng.range(0, 5)), c.ai.v_cur = v.f(0, 80), c.ai.v_next = v.f(0, 80), c.speed = v.f(-5, 85);
}
// The decision: 0 no planar velocity (the pure pursuit's zero yaw rate) and the pit kinds' look-ahead; 1 the arc modes (2 too),
// the arc tick cap, the saturation's gains, the bonus; 2 recovery states with the car turned and the collision flags; 3 the
// stop driver's kinds; 4 the first 2 s's look-ahead (the race clock).
void vary_ai_decide(ex::Car& c, Vary& v, int k) {
    ex::Driver& d = c.ai;
    const u32 cond = v.ee.r32(v.car + 4);
    switch (k) {
    case 0:
        c.vel.x = c.vel.y = ee::F();
        if (v.rng.chance(0.7)) c.driver_kind = u8(v.rng.range(3, 4)), d.pit_limit = u8(v.rng.range(0, 1));
        d.state = u8(v.rng.range(0, 2));
        break;
    case 1:
        d.mode = u8(v.rng.range(0, 5)), d.arc_ticks = u16(v.rng.chance(0.5) ? v.rng.range(5995, 6010) : v.rng.range(0, 20));
        d.sat_brake = v.f(0, 1.5f), d.sat_throttle = v.f(0, 3), d.status = s8(v.rng.chance(0.5) ? 0 : v.rng.range(1, 5));
        d.bonus_ticks = s32(v.rng.range(0, 20)), c.x535 = u8(c.x535 ^ (v.rng.chance(0.5) ? 0x10 : 0));
        d.yaw_gain = v.f(0.5f, 6);
        break;
    case 2: {
        d.state = u8(v.rng.range(0, 2));
        c.heading = c.heading + ee::F(v.u(-3.3f, 3.3f));
        c.speed = v.f(-12, 15);
        const u32 b = body_flags(v.ee, v.car);
        v.poke32(b + 0x434, u32(v.rng.range(0, 1))), v.poke32(b + 0x438, u32(v.rng.range(0, 1)));
        break;
    }
    case 3: c.driver_kind = u8(v.rng.range(5, 7)); break;
    default: v.poke32(cond + 0x8000 + k_off(0x6380), u32(v.rng.range(0, 6500))); break;
    }
}

// The AI's step's inputs outside the record (D +0x00 is the conductor): the race's start countdown, the race clock, the
// collision step's flags of the car's body (index F +0x3E8) and the course category byte.
ex::AiInputs ai_inputs(Ee& e, u32 car) {
    ex::AiInputs in;
    const u32 cond = e.r32(car + 0xEC8);
    if (cond != e.r32(car + 4)) throw std::runtime_error("the driver object's conductor is not the car's");
    in.start_count = s32(e.r32(e.r32(cond) + 0x5C));
    in.clock = e.r32(cond + 0x8000 + k_off(0x6380));
    const u32 body = body_flags(e, car);
    in.hit_x = 0, in.hit_front = e.r32(body + 0x434), in.hit_rear = e.r32(body + 0x438);
    in.course_category = e.r8(cond + 0x8000 + k_off(0x37AD));
    in.start_speed = e.r8(cond + cond_off(0xE340));
    return in;
}

// ---- the AI's lines and speed plan ----------------------------------------------------------------------------------------
// The AI course object of a car's driver (driver +0x08), read once per race (the course setup converted the lines in place when
// the race was set up; nothing writes them later), and the wear curve of the driver's conductor (driver +0x00; the corner speeds
// read its max +0x9C). AI coverage: paths taken, from our ports' inputs and results.
std::map<u32, ex::AiCourse> g_ai_courses;
std::map<std::string, long> g_ai_cover;
// The ports use our course setup's object (sim::exact::ai_course_setup from our parse of the race's course file, g_ai_built),
// compared with the original's at its first use in each race (the first difference in g_ai_built_diff); the original's when
// ours could not be built.
std::unique_ptr<start_setup::Course> g_ai_built;
std::string g_ai_built_diff, g_ai_built_err;
const std::string* g_ai_scenario = nullptr;
long g_ai_built_races = 0;
const ex::AiCourse& ai_course(Ee& ee, u32 car) {
    const u32 k = ee.r32(car + 0xEC8 + 8);
    auto it = g_ai_courses.find(k);
    if (it == g_ai_courses.end()) {
        ex::AiCourse theirs = ai_exact::read_course(ee, k);
        if (g_ai_built) {
            ++g_ai_built_races;
            const std::string d = ai_exact::course_difference(g_ai_built->ai, theirs);
            if (!d.empty() && g_ai_built_diff.empty()) g_ai_built_diff = (g_ai_scenario ? *g_ai_scenario + ": " : std::string()) + d;
            it = g_ai_courses.emplace(k, g_ai_built->ai).first;
        } else {
            it = g_ai_courses.emplace(k, std::move(theirs)).first;
        }
    }
    return it->second;
}
const ex::AiCourse& ai_course(const Call& call) { return ai_course(*call.ee, call.car); }
ex::F TapePlan::line_heading(const ex::Car&, int line, int hint, ex::F s) { return ex::ai_line_heading(ai_course(ee_, car_), line, hint, s); }
ex::WearParams ai_wear(Ee& ee, u32 car) {
    const u32 cp = ee.r32(car + 0xEC8) + 0x84;
    auto f = [&](u32 o) { return ee::F::raw(ee.r32(cp + o)); };
    return {f(0x00), f(0x04), f(0x08), f(0x0C), f(0x10), f(0x14), f(0x18), f(0x1C), f(0x20), f(0x24)};
}
ex::WearParams ai_wear(const Call& call) { return ai_wear(*call.ee, call.car); }
void ai_cover(const std::string& what) { ++g_ai_cover[what]; }

void ai_braking_port(ex::Car& c, const Call& call) {
    const ex::Driver pre = c.ai;
    // (a later build's stop zone: the point a car owing a stop brakes to, stored in the conductor)
    ex::StopZone zone = call.zone;
    const ex::F m = ex::ai_braking(c, ai_course(call), ai_wear(call), &zone, g_query);
    check_f0(call, m);
    if (sim::concept_program(program())) {
        call.differ(zone_differ(zone, read_zone(*call.ee, g_step_cond)));
        if (c.ai.stop != pre.stop) ai_cover("braking: the stop zone's point");
    }
    const ex::Driver& d = c.ai;
    if (pre.line == 4) return ai_cover("braking: stop line");
    const int count = int(ai_course(call).lines[pre.line].size());
    if (pre.next != d.next)
        ai_cover(d.next == count && pre.next == 0 ? "braking: next 0 -> count (before the start line)"
                 : d.next == 0 && pre.next == count ? "braking: next count -> 0 (the start line)"
                 : d.next == pre.next + 1           ? "braking: next + 1"
                                                    : "braking: next - 1");
    if (pre.mode == 3 || pre.mode == 4) ai_cover("braking: in a braking mode");
    else if (d.mode == 3 || d.mode == 4) ai_cover(d.mode == 4 ? "braking: zone entered in an arc (mode 4)" : "braking: zone entered (mode 3)");
    if (d.status == 1) ai_cover("braking: status 1 (approaching)");
    if (!(m == ex::F::raw(0x3F800000)) && !(m == d.throttle_scale)) ai_cover("braking: soft zone throttle");
    if (pre.line == 3) ai_cover("braking: pit lane line");
    if (pre.cooldown == 0 && ex::F() < pre.contact && d.next_kind != 0 && d.v_next < c.speed) ai_cover("braking: contact term");
    if ((c.x535 & 0x10) && d.next_kind != 0 && d.v_next < c.speed) ai_cover("braking: on the pit surface");
}
void ai_corner_port(ex::Car& c, const Call& call) {
    const ex::AiCourse& k = ai_course(call);
    const int i = int(call.a[1]);
    const ex::AiPoint& pt = k.lines[c.ai.line][size_t(i)];
    const ex::WearParams cp = ai_wear(call);
    const ex::F v = ex::ai_corner_speed(c, k, cp, i);
    check_f0(call, v);
    if ((ex::F() <= pt.radius && pt.bank < ex::F()) || (pt.radius < ex::F() && ex::F() < pt.bank)) ai_cover("corner: the bank divides");
    else if (!(pt.bank == ex::F())) ai_cover("corner: a bank leaning out");
    ai_cover(!(cp.max == ex::F()) && !(c.x535 & 0x10) ? "corner: wear grip" : !(cp.max == ex::F()) ? "corner: wear on, pit surface" : "corner: wear off");
    if (c.ai.trim != 0) ai_cover("corner: trim");
    if (pt.speed_adjust != 0) ai_cover("corner: point speed adjustment");
    if (v == c.p.top_speed) ai_cover("corner: at the top speed");
    if (v == ex::F::raw(0x40B1C71D)) ai_cover("corner: at 20 km/h");
    if (pt.kind == 2) ai_cover("corner: kind 2");
}
void ai_target_port(ex::Car& c, const Call& call) {
    ex::F dx, dy;
    bool missed = false;
    const ex::F look = ex::F::raw(call.f[0]);
    const ex::F l = ex::ai_target(c, ai_course(call), int(call.a[2]), int(call.a[3]), look, dx, dy, &missed);
    if (missed) call.differ("a line evaluation found no span (the original reads its stack)");
    check_f0(call, l);
    if (call.ee->r32(u32(call.t[0])) != dx.bits || call.ee->r32(u32(call.t[1])) != dy.bits)
        call.differ(rh::fmt("target 0x%08X 0x%08X vs 0x%08X 0x%08X", dx.bits, dy.bits, call.ee->r32(u32(call.t[0])), call.ee->r32(u32(call.t[1]))));
    if (!(l == look)) ai_cover("target: look-ahead extended");
    if (call.a[2] == 4) ai_cover("target: stop line");
}
void ai_evaluate_port(ex::Car&, const Call& call) {
    const ex::AiCourse& k = ai_course(call);
    ex::F x, y, h;
    const int line = int(call.a[1]), hint = int(call.a[2]);
    const ex::F s = ex::F::raw(call.f[0]);
    int to = 0, from = 0;
    bool far = false;
    ex::F ws = s;
    if (ws < ex::F()) ws = ws + k.lap_length;
    else if (k.lap_length <= ws) ws = ws - k.lap_length;
    const int sp = k.span(line, hint, ws, to, from, &far);
    if (far) return call.differ("the span search reads past the line");
    if (!k.evaluate(line, hint, s, x, y, h)) return call.differ("no span (the original leaves the outputs)");
    Ee& e = *call.ee;
    if (e.r32(u32(call.a[3])) != x.bits || e.r32(u32(call.t[0])) != y.bits || e.r32(u32(call.t[1])) != h.bits)
        call.differ(rh::fmt("line %d at 0x%08X: 0x%08X 0x%08X 0x%08X vs 0x%08X 0x%08X 0x%08X", line, s.bits, x.bits, y.bits, h.bits,
                            e.r32(u32(call.a[3])), e.r32(u32(call.t[0])), e.r32(u32(call.t[1]))));
    ai_cover(rh::fmt("evaluate: span case %d, %s", sp, k.lines[size_t(line)][size_t(from)].kind ? "arc" : "straight"));
}
void ai_evaluate_stop_port(ex::Car&, const Call& call) {
    ex::F x, y, h;
    const int r = ai_course(call).evaluate_stop(int(call.a[1]), ex::F::raw(call.f[0]), x, y, h);
    check_v0(call, u32(r));
    Ee& e = *call.ee;
    if (r >= 0 && (e.r32(u32(call.a[3])) != x.bits || e.r32(u32(call.t[0])) != y.bits || e.r32(u32(call.t[1])) != h.bits))
        call.differ(rh::fmt("stop line: 0x%08X 0x%08X 0x%08X vs 0x%08X 0x%08X 0x%08X", x.bits, y.bits, h.bits, e.r32(u32(call.a[3])),
                            e.r32(u32(call.t[0])), e.r32(u32(call.t[1]))));
    ai_cover(r < 0 ? "stop line: outside" : "stop line: inside");
}
void ai_speed_port(ex::Car& c, const Call& call) {
    ex::ai_speed_control(c, ex::F::raw(call.f[0]), ex::F::raw(call.f[1]));
    if (c.ai.brake == ex::F::raw(0x3F800000)) ai_cover("speed control: brake");
    if (c.ai.shift_lock == 12) ai_cover("speed control: part throttle (lock-out)");
}
void ai_request_port(ex::Car& c, const Call& call) {
    const bool ok = ex::ai_request_line(c, ai_course(call), ai_wear(call), int(call.a[1]), call.a[2] != 0);
    check_v0(call, u32(ok));
    ai_cover(!ok ? "line request: none" : call.a[2] ? "line request: at once" : "line request: blended");
}
void ai_lane_port(ex::Car&, const Call& call, bool left) {
    const ex::AiCourse& k = ai_course(call);
    const ex::F s = ex::F::raw(call.f[0]);
    const int line = int(call.a[1]), hint = int(call.a[2]);
    check_v0(call, u32(left ? k.lane_left(line, hint, s) : k.lane_right(line, hint, s)));
    if (k.side_swapped(hint, s)) ai_cover("lane lookup: side map swapped");
}

// Variants of the AI's plan on a copy of the record (the driver's car pointer +0x04 poked to the copy): speeds above the next
// target, every mode and next kind, the soft zone (k_brake > 1), contact with and without the cooldown, the pit surface, the
// race distance around the lap's half and the point distances, the next index at 0 or past the end, a trim, the stop line and
// the pit lane line, wear off.
void ai_vary_common(ex::Car& c, Vary& v) {
    v.poke32(v.car + 0xEC8 + 4, v.car);
    const ex::AiCourse& k = ai_course(v.ee, v.car);
    ex::Driver& d = c.ai;
    const auto& p = k.lines[d.line];
    if (p.empty()) return;
    const int n = int(p.size());
    switch (v.rng.range(0, 4)) {
    case 0: c.f3e4 = ex::F(k.lap_length.host() * v.u(0.45f, 0.55f)); break;
    case 1: c.f3e4 = p[size_t(v.rng.range(0, n - 1))].dist; break;
    case 2: c.f3e4 = ex::F(k.lap_length.host() * v.u(0, 1)); break;
    default: break;
    }
    if (v.rng.chance(0.2)) d.next = u8(v.rng.chance(0.5) ? 0 : n);
    if (v.rng.chance(0.3)) c.x535 ^= 0x10;
    if (v.rng.chance(0.2)) d.trim = s8(v.rng.range(-2, 2));
}
void vary_braking(ex::Car& c, Vary& v, int k) {
    ex::Driver& d = c.ai;
    if (k == 3) d.line = u8(v.rng.chance(0.5) ? 3 : 4);
    if (ai_course(v.ee, v.car).lines[d.line].empty()) d.line = 6;
    ai_vary_common(c, v);
    // the next point (often an arc) and its kind, as the bookkeeping keeps them
    const auto& p = ai_course(v.ee, v.car).lines[d.line];
    const int n = int(p.size());
    for (int tries = 0; tries < 8; ++tries) {
        d.next = u8(v.rng.range(0, n));
        if (p[size_t(d.next < n ? d.next : n - 1)].kind != 0 || v.rng.chance(0.2)) break;
    }
    d.next_kind = p[size_t(d.next < n ? d.next : n - 1)].kind;
    d.mode = u8(v.rng.range(0, 4));
    d.v_next = v.f(5, 40);
    c.speed = d.v_next + v.f(0, 30);
    if (k >= 1) d.brake_factor = v.f(0.5f, 2.5f);
    if (k >= 2) d.contact = v.rng.chance(0.5) ? ex::F() : v.f(0.25f, 1), d.cooldown = s16(v.rng.chance(0.5) ? 0 : 10);
}
void vary_replan(ex::Car& c, Vary& v, int k) {
    ai_vary_common(c, v);
    if (k == 1) v.poke32(v.ee.r32(v.car + 0xEC8) + 0x84 + 0x18, 0); // the conductor's wear curve max: wear off
    if (k == 1 && v.rng.chance(0.5)) c.ai.speed_scale = v.f(0.05f, 0.3f), c.ai.speed_offset = v.f(-3, 0); // the 20 km/h floor
}
void vary_begin(ex::Car& c, Vary& v, int) {
    v.poke32(v.car + 0xEC8 + 4, v.car);
    c.ai.blend = v.rng.chance(0.3) ? ex::F() : v.f(-0.1f, 0.05f);
    c.dt = v.f(0, 0.03f);
}
void vary_speed(ex::Car& c, Vary& v, int) {
    v.poke32(v.car + 0xEC8 + 4, v.car);
    c.speed = v.f(-5, 80);
    const ex::F vt = c.speed + v.f(-6, 6);
    v.args = {Arg::fbits(vt.bits), Arg::fbits(ex::F(v.rng.chance(0.5) ? -0.5f : v.u(-2.5f, 0)).bits)};
}

// ---- the pit lane and the end of a run (pit_lane.h) ------------------------------------------------------------------------
// The race's progress query on our road (the race flow's, set per race), the road's split line, and the pit lane's race
// display events in the original's run (0x1ED9F0 type 1 from 0x1EDB40, 0x1EDA40 type 6; cleared every race step).
float g_split = 0;
std::vector<ex::PitEvent> g_pit_events;

// The free run's recorder calls in the original's run (their HLE stubs, free_run_recorders): close 0x2106D8, best 0x210700,
// ghost 0x17AAD8, ghost_restart 0x1F52B0 (the ghost's playback), lap_start 0x210668 (the lap recording's restart).
struct RecCall {
    ex::RecorderEvent::Kind kind;
    u32 a0 = 0, a1 = 0, a2 = 0;
};
std::vector<RecCall> g_rec_calls;

// ---- the race flow's inputs and state in the original's memory (conductor `cond`, its race info [cond]) --------------------
// Car i's race entry ([[race info + 0x18] + 8] + 4 i), the data logger's table of car record `car` (its input object car + 0xC:
// + 0x12C) and its entries' range read.
u32 race_entry(Ee& ee, u32 info, int i) {
    const u32 list = ee.r32(info + 0x18);
    return list ? ee.r32(ee.r32(list + 8) + 4 * u32(i)) : 0;
}
u32 logger_table(Ee& ee, u32 car) {
    const u32 h = ee.r32(car + 0xC);
    return h ? ee.r32(h + 0x12C) : 0;
}
constexpr s32 kLoggerFirst = -1, kLoggerEnd = 40;
// An entry of the data logger's table or the free run's recordings: 0x1B00 bytes, a later build's 4 more (its record's).
u32 snap_size() { return 0x1B00 + car_stride() - ce::kCarSize; }
// Such an entry at `h`.
ex::Snapshot read_snapshot(Ee& ee, u32 h) {
    ex::Snapshot s;
    s.sub = ee.r8(h + 2), s.phase = ee.r8(h + 3), s.frame = ee.r32(h + 4), s.frames = ee.r32(h + 8);
    s.frac = ee::F::raw(ee.r32(h + 0xC)), s.time = ee.r32(h + 0x10);
    s.car = ce::read_car(ee, h + 0x24);
    return s;
}
std::string snapshot_differ(const ex::Snapshot& a, const ex::Snapshot& b, bool frames) {
    if (a.sub != b.sub || a.phase != b.phase || a.frac.bits != b.frac.bits)
        return rh::fmt("+2 %u +3 %u +0xC 0x%08X vs %u %u 0x%08X", a.sub, a.phase, a.frac.bits, b.sub, b.phase, b.frac.bits);
    if (frames && (a.frame != b.frame || a.frames != b.frames || a.time != b.time))
        return rh::fmt("+4 %u +8 %u +0x10 %u vs %u %u %u", a.frame, a.frames, a.time, b.frame, b.frames, b.time);
    // A later build's record has no feedback words (they are the conductor's, docs/specs/physics.md §14.5): a snapshot holds none.
    ex::Car theirs = b.car;
    if (sim::concept_program(program()))
        theirs.x16d4 = a.car.x16d4, theirs.rumble = a.car.rumble, theirs.steer_force = a.car.steer_force, theirs.ffb = a.car.ffb,
        theirs.ffb_count = a.car.ffb_count;
    if (std::string d = ce::first_difference(a.car, theirs); !d.empty()) return "its car: " + d;
    return {};
}
// The free run's recorders (the race object race info + 0x3C, its virtual 27: 0x1874B0 the player's, 0x1874E8 the ghost's
// playback) and a store's header (0x1F5188).
u32 player_recorder(Ee& ee, u32 info) {
    const u32 obj = ee.r32(info + 0x3C);
    if (!obj) return 0;
    return obj + (ee.r32(obj + race_object_word(kRecorderSelect)) != 0 || ee.r32(info + 0x70) != 0 ? 0xCB50 : 0xC8C0);
}
u32 ghost_recorder(Ee& ee, u32 info) {
    const u32 obj = ee.r32(info + 0x3C);
    if (!obj) return 0;
    return obj + (ee.r32(obj + race_object_word(kRecorderSelect)) != 0 || ee.r32(info + 0x70) != 0 ? 0xCC98 : 0xCA08);
}
u32 store_header(Ee& ee, u32 store) { return store && ee.r32(store + 4) ? ee.r32(store + 0x50) : 0; }

// The race flow's inputs as the step's loop after phase 13 sees them: the course, the race's countdown, start type, laps and
// configuration bytes +0 .. +2 and the race info's words from the memory, the time limit from the built configuration, the
// race-flow byte and the program's tables built; the cars' input objects (the data logger), the speed unit, the free run's
// recorders (the ghost's lap start into `ghost`, the lap recording's into `lap`).
ex::RaceFlowInputs flow_inputs(Ee& ee, u32 cond, ex::Snapshot* ghost, ex::Snapshot* lap) {
    const ti::Built& b = ti::built();
    ex::RaceFlowInputs r;
    const u32 I = ee.r32(cond);
    r.kind = b.kind;
    r.course = g_query;
    r.split = ee::F(g_split);
    r.start_count = s32(ee.r32(I + 0x5C));
    r.start_type = s32(ee.r32(I + 0x54));
    for (u32 k = 0; k < 3; ++k) r.config[k] = ee.r8(cond + cond_off(0xE340) + k);
    ex::RaceConfig cfg = b.cfg;
    cfg[1] = r.config[1];
    r.laps = ex::race_laps(b.kind, cfg, s32(ee.r32(I + 8)));
    r.time_limit = u16(b.cfg[0x38] | b.cfg[0x39] << 8);
    r.flow = b.flow;
    r.messages = ex::hud_messages(b.data);
    r.machine_sectors = ex::machine_sectors(b.data);
    r.moods = ex::mood_tables(b.data);
    r.behaviour = b.data.behaviour();
    r.replay = ee.r32(I + 0x6C) != 0, r.locked = ee.r32(I + 0x7C) != 0, r.unjudged = ee.r32(I + 0x80) != 0;
    r.logger = ee.r32(I + 0x70) != 0, r.logger_record = ee.r32(I + 0x74) != 0;
    const u32 rec = ee.r32(ee.r32(I + 0x28) + holder_off(0x18)); // the licence's record (0x17AD78), read as the program reads it
    for (u32 k = 0; k < 3; ++k)
        r.licence_times[k] = u32(ee.r8(rec + 0x1C4 + 4 * k)) * 60000u + u32(ee.r8(rec + 0x1C5 + 4 * k)) * 1000u + ee.r16(rec + 0x1C6 + 4 * k);
    const int n = int(ee.r32(cond + race_setup::kK8 + k_off(0x6384)));
    for (int i = 0; i < n && i < 8; ++i)
        if (const u32 h = ee.r32(race_setup::car_addr(cond, i) + 0xC))
            r.logger_frame[size_t(i)] = ee.r32(h + 0xFC), r.logger_relative[size_t(i)] = ee.r32(h + 0xC8) == 2;
    const u32 unit = ee.r32(pa(0x002F59D8));
    r.speed_unit = u8(unit == 0 ? 0 : unit == 1 ? 1 : 2);
    if (b.kind == ex::Conductor::FreeRun) {
        ex::FreeRunRecorder& f = r.recorder;
        if (const u32 p = player_recorder(ee, I)) {
            const u32 store = ee.r32(p + 0x128);
            // NTSC-U / PAL (0x210690): the store not closed (+0x30); NTSC-J: the recorder's +0xF4 clear (read after the close)
            const bool open = program() == Program::NtscJ ? ee.r32(p + 0xF4) == 0 : !store || ee.r32(store + 0x30) == 0;
            f.lap_ok = open && (!store || s32(ee.r32(store + 0x38)) < 0x8CA0);
            if (const u32 h = store_header(ee, ee.r32(p + 0x124))) f.best = ee.r32(h + 0x10);
            if (const u32 h = store_header(ee, store); h && lap) *lap = read_snapshot(ee, h), f.lap_start = lap;
        }
        if (const u32 g = ghost_recorder(ee, I)) {
            f.ghost = ee.r32(g + 0x140) == 0;
            if (const u32 h = store_header(ee, g + 0xC4)) {
                f.ghost_time = ee.r32(h + 0x10);
                if (ghost) *ghost = read_snapshot(ee, h), f.ghost_start = ghost;
            }
        }
    }
    return r;
}

// The race flow's state: the clock and end countdown, the result words, the session's and the entries' records, the sector
// order, the data logger's tables (with the data logger on).
ex::RaceFlow read_flow(Ee& ee, u32 cond) {
    ex::RaceFlow f;
    const u32 I = ee.r32(cond), K = cond + race_setup::kK8;
    f.clock = ee.r32(cond + cond_off(0xE380)), f.end_count = ee.r16(K + k_off(0x37AE));
    f.zone = read_zone(ee, cond);
    f.result = s32(ee.r32(I + 0x40)), f.result_time = ee.r32(I + 0x44), f.ghost = ee.r32(I + 0x48), f.record = ee.r32(I + 0x4C);
    auto splits = [&](u32 a) {
        ex::Splits s;
        for (u32 k = 0; k < 4; ++k) s.sector[k] = ee.r32(a + 4 * k);
        s.lap = ee.r32(a + 0x10);
        return s;
    };
    f.session = splits(I + info_off(0x8AC));
    const int n = int(ee.r32(K + k_off(0x6384)));
    for (int i = 0; i < n; ++i) {
        ex::EntryRecords e;
        if (const u32 a = race_entry(ee, I, i)) {
            ex::LapList& l = e.laps;
            l.counted_only = ee.r32(a + 0x210), l.count = ee.r32(a + 0x214), l.dropped = ee.r32(a + 0x218), l.total = ee.r32(a + 0x220);
            const u32 m = std::min<u32>(ee.r32(a + 0x21C), 1000);
            for (u32 k = 0; k < m; ++k) l.laps.push_back(ee.r32(a + 0x224 + 4 * k));
            e.best = splits(a + 0x11C8), e.current = splits(a + 0x11DC);
            e.best_lap = ee.r32(a + 0x11F0), e.top_speed = ee::F::raw(ee.r32(a + entry_off(0x2100)));
        }
        f.entries.push_back(std::move(e));
    }
    ex::SectorOrder& o = f.order;
    for (u32 s = 0; s < 4; ++s) {
        for (u32 p = 0; p < 8; ++p) o.time[s][p] = ee.r32(K + k_off(0x3AF8) + 32 * s + 4 * p), o.car[s][p] = s8(ee.r8(K + k_off(0x3B88) + 8 * s + p));
        o.lap[s] = s16(ee.r16(K + k_off(0x3B78) + 2 * s)), o.count[s] = s8(ee.r8(K + k_off(0x3B80) + s)), o.shown[s] = s8(ee.r8(K + k_off(0x3B84) + s));
    }
    if (ee.r32(I + 0x70) != 0)
        for (int i = 0; i < n; ++i) {
            std::map<s32, ex::Snapshot> t;
            if (const u32 tab = logger_table(ee, race_setup::car_addr(cond, i)))
                for (s32 k = kLoggerFirst; k < kLoggerEnd; ++k) t[k] = read_snapshot(ee, tab + u32(s32(snap_size()) * k));
            f.logger.push_back(std::move(t));
        }
    return f;
}

// Ours against the memory's: the race flow's state (but the recorder events), its words and records.
std::string flow_differ(Ee& ee, u32 cond, const ex::RaceFlow& ours) {
    const ex::RaceFlow t = read_flow(ee, cond);
    if (ours.clock != t.clock || ours.end_count != t.end_count)
        return rh::fmt("clock %u, end countdown %u vs %u, %u", ours.clock, ours.end_count, t.clock, t.end_count);
    if (std::string d = zone_differ(ours.zone, t.zone); !d.empty()) return d;
    if (ours.result != t.result || ours.result_time != t.result_time || ours.record != t.record || ours.ghost != t.ghost)
        return rh::fmt("result %d %u, ghost %u, record %u vs %d %u, %u, %u", ours.result, ours.result_time, ours.ghost, ours.record,
                       t.result, t.result_time, t.ghost, t.record);
    auto splits_differ = [](const char* what, const ex::Splits& a, const ex::Splits& b) {
        for (size_t k = 0; k < 4; ++k)
            if (a.sector[k] != b.sector[k]) return rh::fmt("%s sector %zu: %u vs %u", what, k, a.sector[k], b.sector[k]);
        return a.lap != b.lap ? rh::fmt("%s lap: %u vs %u", what, a.lap, b.lap) : std::string();
    };
    if (std::string d = splits_differ("the session's", ours.session, t.session); !d.empty()) return d;
    for (size_t i = 0; i < t.entries.size(); ++i) {
        const ex::EntryRecords none, &a = i < ours.entries.size() ? ours.entries[i] : none, &b = t.entries[i];
        const ex::LapList &la = a.laps, &lb = b.laps;
        if (la.counted_only != lb.counted_only || la.count != lb.count || la.dropped != lb.dropped || la.total != lb.total ||
            la.laps != lb.laps)
            return rh::fmt("entry %zu lap list: %u laps (%zu listed, total %u) vs %u (%zu, %u)", i, la.count, la.laps.size(), la.total,
                           lb.count, lb.laps.size(), lb.total);
        if (std::string d = splits_differ("best splits", a.best, b.best); !d.empty()) return rh::fmt("entry %zu ", i) + d;
        if (std::string d = splits_differ("current splits", a.current, b.current); !d.empty()) return rh::fmt("entry %zu ", i) + d;
        if (a.best_lap != b.best_lap) return rh::fmt("entry %zu best %u vs %u", i, a.best_lap, b.best_lap);
        if (a.top_speed.bits != b.top_speed.bits)
            return rh::fmt("entry %zu top speed 0x%08X vs 0x%08X", i, a.top_speed.bits, b.top_speed.bits);
        // The entry's other words our stores reached (a licence's sectors past the fourth): the original's memory there.
        if (const u32 at = race_entry(ee, ee.r32(cond), int(i)))
            for (const auto& [off, v] : a.words)
                if (ee.r32(at + off) != v) return rh::fmt("entry %zu word +0x%X: 0x%08X vs 0x%08X", i, off, v, ee.r32(at + off));
    }
    const ex::SectorOrder &oa = ours.order, &ob = t.order;
    for (size_t s = 0; s < 4; ++s) {
        if (oa.lap[s] != ob.lap[s] || oa.count[s] != ob.count[s] || oa.shown[s] != ob.shown[s])
            return rh::fmt("sector order %zu: lap %d, %d cars, %d shown vs %d, %d, %d", s, oa.lap[s], oa.count[s], oa.shown[s], ob.lap[s],
                           ob.count[s], ob.shown[s]);
        if (oa.time[s] != ob.time[s] || oa.car[s] != ob.car[s]) return rh::fmt("sector order %zu: the times or cars", s);
    }
    for (size_t i = 0; i < t.logger.size(); ++i)
        for (const auto& [k, e] : t.logger[i]) {
            static const std::map<s32, ex::Snapshot> empty;
            const std::map<s32, ex::Snapshot>& m = i < ours.logger.size() ? ours.logger[i] : empty;
            static const ex::Snapshot none; // an entry not written: the table's memory as it starts, zero
            const auto it = m.find(k);
            if (std::string d = snapshot_differ(it == m.end() ? none : it->second, e, true); !d.empty())
                return rh::fmt("car %zu logger entry %d: ", i, k) + d;
        }
    return {};
}

// A queue entry as a RaceEvent (a display event's values from its word).
ex::RaceEvent queue_event(s32 type, s32 prio, u32 w) {
    ex::RaceEvent e;
    e.type = type, e.car = s32(w), e.arg = prio;
    if (!ex::display_event(type)) return e;
    e.car = s32(w & 15);
    switch (type) {
    case 11: e.lap = s32(w >> 8 & 0x3FF); break;
    case 12: e.sector = s32(w >> 4 & 15), e.lap = s32(w >> 8 & 0x3FF); break;
    case 17: e.speed = ee::F((float(w >> 4 & 0x3FFF) + 0.5f) / 10.0f); break;
    case 18: e.value = s32(w >> 4 & 15); break;
    default: e.value = s32(w) >> 4; break;
    }
    return e;
}

// The race queue's posts from entry `from` on against ours: the type and priority, the word (a display event's bits it sets).
std::string queue_differ(Ee& ee, u32 info, u32 from, const std::vector<ex::RaceEvent>& ours) {
    const u32 q = info + 0x84, n = ee.r32(q + queue_count_off());
    auto describe = [&] {
        std::string all;
        for (u32 k = from; k < n; ++k) all += rh::fmt(" {%d %d 0x%X}", ntsc_u_event(s16(ee.r16(q + 8 * k))), s16(ee.r16(q + 8 * k + 2)), ee.r32(q + 8 * k + 4));
        all += ";";
        for (const ex::RaceEvent& e : ours) all += rh::fmt(" {%d %d 0x%X}", e.type, e.arg, ex::display_word(e));
        return all;
    };
    if (n < from || n - from != ours.size()) return rh::fmt("%zu race events vs %u:", ours.size(), n - std::min(n, from)) + describe();
    for (u32 k = 0; k < ours.size(); ++k) {
        const u32 a = q + 8 * (from + k);
        const ex::RaceEvent& e = ours[k];
        const s32 type = ntsc_u_event(s16(ee.r16(a))), prio = s16(ee.r16(a + 2));
        const u32 word = ee.r32(a + 4), mask = ex::display_event(e.type) ? ex::display_bits(e.type) : 0xFFFFFFFFu;
        if (type != e.type || prio != e.arg || ((word ^ ex::display_word(e)) & mask) != 0)
            return rh::fmt("race event %u: {%d %d 0x%X} vs {%d %d 0x%X}:", k, e.type, e.arg, ex::display_word(e) & mask, type, prio,
                           word & mask) +
                   describe();
    }
    return {};
}

// The free run's recorder events against the original's calls (g_rec_calls from `from` on), the lap recording's header (its
// lap time, its lap start).
std::string recorder_differ(Ee& ee, u32 info, size_t from, const std::vector<ex::RecorderEvent>& ours) {
    size_t k = from;
    const u32 p = player_recorder(ee, info);
    const u32 lap = p ? store_header(ee, ee.r32(p + 0x128)) : 0;
    for (const ex::RecorderEvent& e : ours) {
        if (e.kind == ex::RecorderEvent::lap) {
            if (!lap || ee.r32(lap + 0x10) != e.time) return rh::fmt("the lap recording's time %u vs %u", e.time, lap ? ee.r32(lap + 0x10) : 0);
            continue;
        }
        if (k >= g_rec_calls.size()) return rh::fmt("recorder event %d not called", int(e.kind));
        const RecCall& c = g_rec_calls[k++];
        if (c.kind != e.kind) return rh::fmt("recorder event %d vs call %d", int(e.kind), int(c.kind));
        if (e.kind == ex::RecorderEvent::ghost && (c.a1 != 1 || c.a2 != 1)) return rh::fmt("0x17AAD8 (%u, %u)", c.a1, c.a2);
        if (e.kind == ex::RecorderEvent::lap_start && lap)
            if (std::string d = snapshot_differ(e.snapshot, read_snapshot(ee, lap), false); !d.empty()) return "the lap start: " + d;
    }
    if (k != g_rec_calls.size())
        return rh::fmt("%zu recorder calls not ours (the first %d)", g_rec_calls.size() - k, int(g_rec_calls[k].kind));
    return {};
}

// The harness's own memory the races write (the free run's recorders): restored after the direct calls.
std::vector<std::pair<u32, u32>> g_scratch;

// The free run's recorders on the race object `obj` (race info + 0x3C, its virtual 27 0x187548): the player's at +0xC8C0 (and
// +0xCB50, the data logger's) with its best lap's store (+0x124) and lap recording (+0x128), the ghost's playback at +0xCA08
// (and +0xCC98) with its store at +0xC4, empty (+0x140); each store with its 0x1B00-byte header (+0x50), no time. The recorders'
// own work is stubbed, the calls logged into g_rec_calls: closing a lap 0x2106D8, keeping the best 0x210700 (the lap's header,
// its start and time, into the best's), the ghost 0x17AAD8 (the best's header becomes the ghost's, the playback not empty), the
// ghost's restart 0x1F52B0, the lap recording's restart 0x210668, the pad frame's record 0x2106B0 (nothing).
void free_run_recorders(Ee& e, u32 obj) {
    auto store = [&] {
        const u32 s = e.alloc(0x60), h = e.alloc(snap_size(), 16);
        e.w32(s + 4, 1), e.w32(s + 0x50, h), e.w32(h + 0x10, ex::kNoTime);
        g_scratch.push_back({s, 0x60}), g_scratch.push_back({h, snap_size()});
        return s;
    };
    const u32 best = store(), lap = store(), ghost = store();
    for (u32 p : {obj + 0xC8C0, obj + 0xCB50}) e.w32(p + 0x124, best), e.w32(p + 0x128, lap), e.w32(p + 0x130, 0);
    for (u32 g : {obj + 0xCA08, obj + 0xCC98}) {
        e.w32(g + 0x140, 1), e.w32(g + 0xC4 + 4, 1), e.w32(g + 0xC4 + 0x50, e.r32(ghost + 0x50));
        g_scratch.push_back({g + 0xC4, 0x60}), g_scratch.push_back({g + 0x140, 4});
    }
    e.hle(pa(0x002106D8), [](Ee& ee) { g_rec_calls.push_back({ex::RecorderEvent::close, u32(ee.a(0))}); }, "free run: close the lap's recording");
    e.hle(pa(0x00210700), [](Ee& ee) {
        const u32 r = u32(ee.a(0));
        g_rec_calls.push_back({ex::RecorderEvent::best, r});
        const u32 from = store_header(ee, ee.r32(r + 0x128)), to = store_header(ee, ee.r32(r + 0x124));
        if (from && to) {
            std::vector<u8> h(snap_size());
            ee.read_bytes(from, h.data(), snap_size());
            ee.write_bytes(to, ByteSpan(h.data(), h.size()));
        }
    }, "free run: the lap into the best lap's store");
    e.hle(pa(0x0017AAD8), [](Ee& ee) {
        const u32 info = u32(ee.a(0));
        g_rec_calls.push_back({ex::RecorderEvent::ghost, info, u32(ee.a(1)), u32(ee.a(2))});
        const u32 p = player_recorder(ee, info), g = ghost_recorder(ee, info);
        const u32 from = p ? store_header(ee, ee.r32(p + 0x124)) : 0, to = g ? store_header(ee, g + 0xC4) : 0;
        if (from && to) {
            std::vector<u8> h(snap_size());
            ee.read_bytes(from, h.data(), snap_size());
            ee.write_bytes(to, ByteSpan(h.data(), h.size()));
            ee.w32(g + 0x140, 0);
        }
    }, "free run: the best lap becomes the ghost");
    e.hle(pa(0x001F52B0), [](Ee& ee) { g_rec_calls.push_back({ex::RecorderEvent::ghost_restart, u32(ee.a(0))}); }, "free run: the ghost's playback restarted");
    e.hle(pa(0x00210668), [](Ee& ee) { g_rec_calls.push_back({ex::RecorderEvent::lap_start, u32(ee.a(0))}); }, "free run: the lap recording restarted");
    e.hle(pa(0x002106B0), [](Ee&) {}, "free run: the pad frame's record");
}

// The data logger's tables ("#logger" races): car i's entry gets its own input object (entry + 0x20DC, car + 0xC) with a table
// at + 0x12C (entries -1 .. kLoggerEnd - 1), car 0's counting frames from its first entry (+ 0xC8 = 2), the frame counter
// + 0xFC (logger_frames steps it every race step).
std::vector<u32> g_logger_inputs;
void logger_tables(Ee& e, const race_setup::OrigRace& o) {
    g_logger_inputs.clear();
    for (int i = 0; i < o.ncars; ++i) {
        const u32 entry = race_entry(e, o.info, i), h = e.alloc(0x200), tab = e.alloc(snap_size() * u32(kLoggerEnd + 1), 16) + snap_size();
        e.w32(entry + entry_off(0x20DC), h), e.w32(h + 0x12C, tab), e.w32(h + 0xC8, i == 0 ? 2 : 0), e.w32(h + 0xFC, 100 + 7 * u32(i));
        g_logger_inputs.push_back(h);
    }
}
void logger_frames(Ee& e) {
    for (u32 h : g_logger_inputs) e.w32(h + 0xFC, e.r32(h + 0xFC) + 1);
}

// The pit lane's inputs for car `car`: the race flow's, the clock, our road's lap length, the AI course object's pit lines, the
// conductor's virtual 42, the wear curve, the configuration block and byte, and the car's collision body flags.
struct PitIn {
    ex::Snapshot ghost, lap;
    ex::RaceFlowInputs flow;
    ex::RaceConfig rules{};
    ex::PitInputs pit;
    bool hit = false, hit_43c = false;
    u32 lap_bits = 0; // 0x1F2BA8's lap length (the conductor's course), for the check against ours
    PitIn(Ee& ee, u32 car) {
        const u32 cond = ee.r32(car + 4);
        flow = flow_inputs(ee, cond, &ghost, &lap);
        ee.read_bytes(cond + cond_off(0xE340), rules.data(), u32(rules.size()));
        pit.flow = &flow;
        pit.clock = ee.r32(cond + cond_off(0xE380));
        pit.lap = g_query ? g_query->lap_length() : ex::F();
        lap_bits = ee.r32(ee.r32(ee.r32(ee.r32(cond + cond_off(0xB7A8))) + 0x70) + 0x18);
        pit.pit_lines = ai_course(ee, car).pit_lines;
        pit.service_always = ee.r32(ee.r32(cond + 0x8000 + k_off(0x63A8)) + 8 * u32(conductor_slot(42)) + 4) == pa(0x001F2640); // virtual 42: free practice's
        pit.wear = ai_wear(ee, car);
        pit.rules = &rules;
        pit.config = ee.r8(cond + 0x8000 + k_off(0x37AC));
        const u32 body = body_flags(ee, car);
        hit = ee.r32(body + 0x43C) != 0 || ee.r32(body + 0x440) != 0;
        hit_43c = ee.r32(body + 0x43C) != 0;
    }
    PitIn(const PitIn&) = delete;
    void check(const Call& k) const {
        std::string d;
        check_lap(d);
        if (!d.empty()) k.differ(d);
    }
    void check_lap(std::string& d) const {
        if (lap_bits != pit.lap.bits && d.empty()) d = rh::fmt("lap length 0x%08X vs 0x%08X", pit.lap.bits, lap_bits);
    }
};

// The pit lane's inputs (the race's laps, its virtual 30) are the race flow's: only for the conductors whose race flow is ported.
bool flow_ported(const ex::Car&, const Call&) { return ex::race_flow_ported(ti::built().kind); }

void run_pit_lane(ex::Car& c, const Call& k) {
    PitIn in(*k.ee, k.car);
    in.check(k);
    std::vector<ex::PitEvent> ev;
    with_tape(k, [&](TapePlan& p) { ex::pit_lane(c, freg(k, 12), freg(k, 13), in.hit, in.hit_43c, in.pit, p, ev); });
    const size_t n = k.pev1 - k.pev0;
    if (ev.size() != n) return k.differ(rh::fmt("%zu pit events vs %zu", ev.size(), n));
    for (size_t q = 0; q < n; ++q)
        if (ev[q].type != g_pit_events[k.pev0 + q].type || ev[q].car != g_pit_events[k.pev0 + q].car)
            return k.differ(rh::fmt("pit event %zu: %d %d vs %d %d", q, ev[q].type, ev[q].car, g_pit_events[k.pev0 + q].type,
                                    g_pit_events[k.pev0 + q].car));
}
template <bool (*Fn)(ex::Car&, ex::AiPlan&)> void run_switch(ex::Car& c, const Call& k) {
    bool v = false;
    with_tape(k, [&](TapePlan& p) { v = Fn(c, p); });
    check_v0(k, u32(v));
}
void run_end_of_run(ex::Car& c, const Call& k) {
    PitIn in(*k.ee, k.car);
    with_tape(k, [&](TapePlan& p) { ex::end_of_run(c, int(s32(k.a[1])), in.pit, p); });
}

// The pit lane's variants: a car in every pit kind and section with and without a service due, the race over, a held car, the
// wrong way on the pit surface, a pit request near the stop line's point 0.
void vary_pit_lane(ex::Car& c, Vary& v, int k) {
    v.poke32(v.car + 0xEC8 + 4, v.car);
    const ex::AiCourse& ai = ai_course(v.ee, v.car);
    const std::vector<ex::AiPoint>& stop = ai.lines[4];
    if (stop.size() < 4 || !g_query) return;
    const ex::F lap = g_query->lap_length();
    const auto at = [&](size_t i) { return stop[i].dist; };
    // The progress pair around a point of the stop line (from the end when negative), wrapped into the lap.
    auto around = [&](int i, float before, float after) {
        const ex::F d = at(i < 0 ? size_t(int(stop.size()) + i) : size_t(i));
        ex::F s = d + ee::F(v.u(-after, after)), o = d - ee::F(v.u(0, before));
        if (!(s < lap)) s = s - lap;
        if (!(o < lap)) o = o - lap;
        v.args = {Arg::fbits(s.bits), Arg::fbits(o.bits)};
    };
    static const u8 kKinds[] = {0, 1, 2, 3, 4, 0};
    c.driver_kind = kKinds[k % 6];
    c.control = s8(v.rng.chance(0.3) ? 0 : 2);
    c.x535 = u8((c.x535 & ~0x19) | (v.rng.chance(0.6) ? 0x10 : 0) | (v.rng.chance(0.5) ? 8 : 0) | (k == 5 ? 1 : 0));
    c.hold = u16(v.rng.chance(0.2) ? (v.rng.chance(0.5) ? 60 : v.rng.range(1, 300)) : 0);
    c.finished = u8(v.rng.chance(0.2));
    c.lap_base = u8(v.rng.chance(0.1));
    c.laps = s16(v.rng.range(0, 3));
    c.speed = v.f(-10, 40), c.lat_speed = v.f(-3, 3);
    c.pit_box = s8(v.rng.range(0, std::min<int>(5, int(stop.size()) - 1)));
    for (ex::Wheel& w : c.w) {
        w.wear = v.rng.chance(0.5) ? ai_wear(v.ee, v.car).fresh : v.f(0, 100000);
        w.x28 = u8(v.rng.chance(0.1));
    }
    const u32 body = body_flags(v.ee, v.car);
    v.poke32(body + 0x43C, u32(v.rng.chance(0.2))), v.poke32(body + 0x440, u32(v.rng.chance(0.1)));
    // Backwards across a point (the wrong way): the old progress past it, the new before it.
    auto back = [&](int i) {
        const ex::F d = at(i < 0 ? size_t(int(stop.size()) + i) : size_t(i));
        ex::F s = d - ee::F(v.u(0, 2)), o = d + ee::F(v.u(0.01f, 2));
        if (!(s < lap)) s = s - lap;
        if (!(o < lap)) o = o - lap;
        v.args = {Arg::fbits(s.bits), Arg::fbits(o.bits)};
    };
    static const int kLeave[] = {1, -1, -2, -3};
    switch (c.driver_kind) {
    case 2:
        if (v.rng.chance(0.5)) around(1, 3, 2);
        else {
            around(c.pit_box, 30, 2);
            if (v.rng.chance(0.3)) // nothing to service
                for (ex::Wheel& w : c.w) w.wear = ai_wear(v.ee, v.car).fresh, w.x28 = 0;
        }
        break;
    case 3: around(c.pit_box, 3, 2); break;
    case 4: around(kLeave[v.rng.range(0, 3)], 3, 2); break;
    default:
        if (k == 5) c.control = s8(v.rng.chance(0.8) ? 0 : 2), c.driver_kind = 0, v.rng.chance(0.5) ? back(-3) : around(int(v.rng.range(3, 6)), 3, 2);
        else v.rng.chance(0.5) ? around(0, 3, 2) : around(1, 3, 2);
        break;
    }
}

// The switches' and the end of a run's variants: every driver kind and control source; the end of a run in every mode, finished
// or not, on the pit surface or not.
void vary_switch(ex::Car& c, Vary& v, int) {
    v.poke32(v.car + 0xEC8 + 4, v.car);
    c.driver_kind = u8(v.rng.range(0, 8)), c.control = s8(v.rng.range(0, 2)), c.ai.state = u8(v.rng.range(0, 2));
}
void vary_end_of_run(ex::Car& c, Vary& v, int k) {
    v.poke32(v.car + 0xEC8 + 4, v.car);
    v.args = {Arg::integer(k)};
    c.driver_kind = u8(v.rng.range(0, 8)), c.control = s8(v.rng.range(0, 2)), c.finished = u8(v.rng.chance(0.5));
    c.x535 = u8(c.x535 ^ (v.rng.chance(0.5) ? 0x10 : 0));
}

// The rival level special car 1's mood reads (the later builds' 0x1F5560), from the original's pass-by states and gap
// matrices and the race course's lap length.
int mem_rival_level(const Call& call, const ex::Car& c) {
    Ee& ee = *call.ee;
    const u32 cond = ee.r32(call.car + 4);
    ex::PassBy m{};
    for (u32 i = 0; i < 8; ++i) ee.read_bytes(cond + cond_off(0xBAB8) + 8 * i, m[i].data(), 8);
    const ee::F lap = ee::F::raw(ee.r32(ee.r32(ee.r32(ee.r32(cond + 0x8000 + k_off(0x37A8))) + 0x70) + 0x18));
    return ex::rival_level(int(c.index), int(ee.r32(cond + race_setup::kK8 + k_off(0x6384))), m, ti::mem_gaps(ee, cond), lap);
}

const Port kPorts[] = {
    {"phase 1 save_previous (0x1EADB8)", 0x001EADB8, 0x001F3AFC, [](ex::Car& c, const Call&) { ex::save_previous(c); }},
    // The later builds' own routines of the race step (special.h; GT3's releases have none).
    {"later: hybrid_step (after phase 1)", later_fn(sim::Rule::HybridStep), later_site(sim::Rule::HybridStep),
     [](ex::Car& c, const Call&) {
         ++g_cover[rh::fmt("later: layout %u, special kind %u", c.p.d.layout, c.p.special)];
         ex::hybrid_step(c);
     }},
    {"later: mood_step (before phase 2)", later_fn(sim::Rule::PodMood), later_site(sim::Rule::PodMood),
     [](ex::Car& c, const Call& call) {
         if (c.p.special != 1) return;
         bool soft = true;
         for (const ex::Ground& g : c.g) soft = soft && ex::soft_material(g, built().race.bumps);
         const int rival = mem_rival_level(call, c);
         if (call.tapped[0] && int(call.tap[0]) != rival) call.differ(rh::fmt("rival level %d vs %d", rival, int(call.tap[0])));
         ex::mood_step(c, ex::mood_tables(built().data), rival, soft);
         ++g_cover[rh::fmt("later: mood %u (rival level %d%s)", c.later.special[1] & 0xF, rival, call.tapped[0] ? "" : ", not tapped")];
     },
     {},
     nullptr,
     {Pa(later_tap(sim::Rule::PodMood)), Pa()}},
    {"later: stop_count_step (after phase 3)", later_fn(sim::Rule::PodStop), later_site(sim::Rule::PodStop),
     [](ex::Car& c, const Call&) {
         ex::stop_count_step(c);
         if (c.p.special == 1 && c.later.special[0]) ++g_cover[rh::fmt("later: stop count %s", c.later.special[0] == 0x22 ? "34" : "under 34")];
     }},
    {"later: wing_step (before phase 12)", later_fn(sim::Rule::ActiveWing), later_site(sim::Rule::ActiveWing),
     [](ex::Car& c, const Call&) {
         ex::wing_step(c);
         if (c.p.special == 2) ++g_cover[c.later.special[4] ? "later: wing out" : "later: wing in"];
     }},
    // Phase 2 (0x1EA428, race step site 0x1F3B2C): its calls, then the whole phase.
    {"phase 2 step_x51d (0x1DDFF0)", 0x001DDFF0, 0x001EA468, [](ex::Car& c, const Call&) { ex::step_x51d(c); }},
    {"phase 2 step_length (0x1ECFC8)", 0x001ECFC8, 0x001EA474, [](ex::Car& c, const Call&) { ex::step_length(c); }},
    {"phase 2 aero (0x1DB118)", 0x001DB118, 0x001EA47C, [](ex::Car& c, const Call& call) { ex::aero(c, slipstream(call)); }},
    {"phase 2 full_throttle_torque (0x1E0BC0 in 0x1EA428)", 0x001E0BC0, 0x001EA48C,
     [](ex::Car& c, const Call& call) { check_f0(call, ex::full_throttle_torque(c, condition(call), ee::F::raw(call.f[0]))); },
     at_a0(0x148)}, // a0 = the engine block, a1 = the condition counters (car + 0x137C)
    {"phase 2 friction_torque (0x1E0C28 in 0x1EA428)", 0x001E0C28, 0x001EA4A0,
     [](ex::Car& c, const Call& call) { check_f0(call, ex::friction_torque(c, ee::F::raw(call.f[0]))); }, at_a0(0x148)},
    {"phase 2 wear_grip (0x1E9E80 in 0x1EA1E8)", 0x001E9E80, 0x001EA240,
     [](ex::Car& c, const Call& call) { check_v0(call, u32(ex::wear_grip(c, wear_params(call)))); }, at_a0(ce::kF)},
    {"phase 2 peak_force (0x1EA1E8)", 0x001EA1E8, 0x001EA4AC, [](ex::Car& c, const Call& call) { ex::peak_force(c, wear_params(call)); }},
    {"phase 2 slip_angles (0x1EA3D8)", 0x001EA3D8, 0x001EA4B4, [](ex::Car& c, const Call&) { ex::slip_angles(c); }},
    {"phase 2 prep (0x1EA428)", 0x001EA428, 0x001F3B2C, [](ex::Car& c, const Call& call) { ex::prep(c, prep_input(call)); }},
    // Phase 3, the pad path: its parts at their call sites, then the phase.
    {"phase 3 0x1EAC60 set_brake", 0x001EAC60, 0x001E4CD8, [](ex::Car& c, const Call& k) { ex::set_brake(c, ex::F::raw(k.f[0])); }},
    {"phase 3 0x1E4320 slew (upper bound)", 0x001E4320, 0x001E4778,
     [](ex::Car& c, const Call& k) { ex::slew(c.lock_hi, ex::F::raw(k.f[0]), ex::F::raw(k.f[1])); }, at_a0(ce::kF + 0x4AC)},
    {"phase 3 0x1E4320 slew (lower bound)", 0x001E4320, 0x001E478C,
     [](ex::Car& c, const Call& k) { ex::slew(c.lock_lo, ex::F::raw(k.f[0]), ex::F::raw(k.f[1])); }, at_a0(ce::kF + 0x4A8)},
    {"phase 3 0x1E4368 steer_follow (class 1)", 0x001E4368, 0x001E4874,
     [](ex::Car& c, const Call& k) { ex::steer_follow(c, pad(k), ex::F::raw(k.f[0]), ex::F::raw(k.f[1]), ex::F::raw(k.f[2])); }},
    {"phase 3 0x1E4368 steer_follow (classes 0 / 2)", 0x001E4368, 0x001E4898,
     [](ex::Car& c, const Call& k) { ex::steer_follow(c, pad(k), ex::F::raw(k.f[0]), ex::F::raw(k.f[1]), ex::F::raw(k.f[2])); }},
    {"phase 3 0x1E4180 pad_clutch", 0x001E4180, 0x001E4C6C,
     [](ex::Car& c, const Call& k) { ex::pad_clutch(c, pad_frame(*k.ee, u32(k.a[1])), u8(k.a[3])); }},
    {"phase 3 0x1E4478 pad_controls", 0x001E4478, 0x001E4CC0,
     [](ex::Car& c, const Call& k) { ex::pad_controls(c, pad(k), pad_frame(*k.ee, u32(k.a[1])), course_kind(k)); }},
    {"phase 3 0x1E4290 pad_hud", 0x001E4290, 0x001E4DD4,
     [](ex::Car& c, const Call& k) { ex::pad_hud(c, pad_frame(*k.ee, u32(k.a[1]))); }, at_a0(ce::kF)},
    {"phase 3 0x1E4CB0 pad_input", 0x001E4CB0, 0x001E4DF4,
     [](ex::Car& c, const Call& k) { ex::pad_input(c, pad(k), pad_frame(*k.ee, u32(k.a[1])), course_kind(k)); }},
    // Phase 3, the AI paths' application of the AI driver object's outputs (the AI's step itself is a later port).
    {"phase 3 0x1EAC90 set_brake_pedal", 0x001EAC90, 0x001F7A74, [](ex::Car& c, const Call& k) { ex::set_brake_pedal(c, ex::F::raw(k.f[0])); }},
    {"phase 3 0x1F79F0 ai_outputs", 0x001F79F0, 0x001E4D00, [](ex::Car& c, const Call&) { ex::ai_outputs(c); }, at_a0(0xEC8)},
    {"phase 3 0x1E4CF0 ai_controls (control source 2)", 0x001E4CF0, 0x001E4E58, [](ex::Car& c, const Call&) { ex::ai_controls(c); }},
    {"phase 3 0x1E4CF0 ai_controls (control sources 0 / 1, the AI drives)", 0x001E4CF0, 0x001E4E3C, [](ex::Car& c, const Call&) { ex::ai_controls(c); }},
    // Phase 3, the AI's step and its parts at their call sites, each on the original's results of its plan calls (TapePlan).
    {.name = "phase 3 ai_steer (0x1F77B8) in the decision", .fn = 0x001F77B8, .site = 0x001F8254,
     .run = [](ex::Car& c, const Call& k) { ex::ai_steer(c, freg(k, 12)); }, .car = at_a0(0xEC8), .vary = vary_ai_steer, .variants = 1},
    {"phase 3 ai_steer (0x1F77B8) in the stop driver", 0x001F77B8, 0x001F783C,
     [](ex::Car& c, const Call& k) { ex::ai_steer(c, freg(k, 12)); }, at_a0(0xEC8)},
    {"phase 3 ai_steer (0x1F77B8) of the parked car", 0x001F77B8, 0x001F7920,
     [](ex::Car& c, const Call& k) { ex::ai_steer(c, freg(k, 12)); }, at_a0(0xEC8)},
    {"phase 3 ai_park (0x1F7900)", 0x001F7900, 0x001E366C, [](ex::Car& c, const Call&) { ex::ai_park(c); }, at_a0(0xEC8)},
    {"phase 3 ai_stop (0x1F7810), driver kind 7", 0x001F7810, 0x001F7B64,
     [](ex::Car& c, const Call& k) { ex::ai_stop(c, freg(k, 12)); }, at_a0(0xEC8)},
    {.name = "phase 3 ai_stop (0x1F7810), driver kinds 5 / 6", .fn = 0x001F7810, .site = 0x001F8224,
     .run = [](ex::Car& c, const Call& k) { ex::ai_stop(c, freg(k, 12)); }, .car = at_a0(0xEC8), .vary = vary_ai_stop, .variants = 3},
    {.name = "phase 3 ai_hold (0x1F8A00)", .fn = 0x001F8A00, .site = 0x001E363C,
     .run = [](ex::Car& c, const Call& k) { ex::ai_hold(c, s32(k.a[1])); }, .car = at_a0(0xEC8), .vary = vary_ai_hold, .variants = 2},
    {.name = "phase 3 ai_traffic (0x1F6D70)", .fn = 0x001F6D70, .site = 0x001F8518,
     .run = [](ex::Car& c, const Call& k) { with_tape(k, [&](TapePlan& p) { ex::ai_traffic(c, p); }); }, .car = at_a0(0xEC8),
     .vary = vary_ai_traffic, .variants = 2},
    {.name = "phase 3 ai_decide (0x1F7AE0)", .fn = 0x001F7AE0, .site = 0x001E36E4,
     .run = [](ex::Car& c, const Call& k) { with_tape(k, [&](TapePlan& p) { ex::ai_decide(c, ai_inputs(*k.ee, k.car), p); }); },
     .car = at_a0(0xEC8), .vary = vary_ai_decide, .variants = 5},
    {"phase 3 ai_step (0x1E35D8), control source 2", 0x001E35D8, 0x001E4E50,
     [](ex::Car& c, const Call& k) { with_tape(k, [&](TapePlan& p) { ex::ai_step(c, ai_inputs(*k.ee, k.car), p); }); }},
    {"phase 3 ai_step (0x1E35D8), control source 1", 0x001E35D8, 0x001E4E34,
     [](ex::Car& c, const Call& k) { with_tape(k, [&](TapePlan& p) { ex::ai_step(c, ai_inputs(*k.ee, k.car), p); }); }},
    {"phase 3 ai_step (0x1E35D8), a pad car the AI drives", 0x001E35D8, 0x001E4E0C,
     [](ex::Car& c, const Call& k) { with_tape(k, [&](TapePlan& p) { ex::ai_step(c, ai_inputs(*k.ee, k.car), p); }); }},
    {"phase 3 ai_gear_hint (0x1F6588), a pad car the AI drives", 0x001F6588, 0x001E4E14,
     [](ex::Car& c, const Call&) { ex::ai_gear_hint(c); }, at_a0(0xEC8)},
    {.name = "phase 3 ai_gear_hint (0x1F6588) in the update", .fn = 0x001F6588, .site = 0x001F665C,
     .run = [](ex::Car& c, const Call&) { ex::ai_gear_hint(c); }, .car = at_a0(0xEC8), .vary = vary_ai_gear_hint, .variants = 1},
    {"phase 3 ai_update (0x1F6648)", 0x001F6648, 0x001E4DFC,
     [](ex::Car& c, const Call& k) { with_tape(k, [&](TapePlan& p) { ex::ai_update(c, p); }); }, at_a0(0xEC8)},
    {.name = "phase 3 driver_input (0x1E4D38)", .fn = 0x001E4D38, .site = 0x001F3B64,
     .run =
         [](ex::Car& c, const Call& k) {
             with_tape(k, [&](TapePlan& p) {
                 // (the pad frame is the routine's first local: its stack frame is 0x40 in GT3's programs, 0x30 in the later builds')
                 ex::driver_input(c, pad(k), pad_frame(*k.ee, k.sp - pad_frame_below()), course_kind(k), ai_inputs(*k.ee, k.car), p);
             });
         }},
    // The race step's loop after phase 13 (pit_lane.h): the pit lane's state machine at its call in 0x1EF060, its parts at their
    // calls (the rolling start's hand-back too), the end of a run in the pit lane and at the lap handler's finish; each on the
    // original's line requests.
    {.name = "pit 0x1EDB40 pit_lane", .fn = 0x001EDB40, .site = 0x001EF23C, .run = run_pit_lane, .only = flow_ported,
     .vary = vary_pit_lane, .variants = 6},
    {"pit 0x1EAB80 race_over", 0x001EAB80, 0x001EDD8C,
     [](ex::Car& c, const Call& k) {
         PitIn in(*k.ee, k.car);
         check_v0(k, u32(ex::race_over(c, in.pit)));
     },
     {}, flow_ported},
    {"pit 0x1E3BF0 pit_exit (lane, past point 1 off the pit surface)", 0x001E3BF0, 0x001EDE04, run_switch<ex::pit_exit>},
    {"pit 0x1E3BF0 pit_exit (leaving, past point 1)", 0x001E3BF0, 0x001EDFE8, run_switch<ex::pit_exit>},
    {.name = "pit 0x1E3BF0 pit_exit (leaving, the exit)", .fn = 0x001E3BF0, .site = 0x001EE090, .run = run_switch<ex::pit_exit>,
     .vary = vary_switch, .variants = 2},
    {"pit 0x1E3BF0 pit_exit (the rolling start's hand-back)", 0x001E3BF0, 0x001F28B8, run_switch<ex::pit_exit>},
    {"pit 0x1E3D08 pit_entry (a pad car)", 0x001E3D08, 0x001EE180, run_switch<ex::pit_entry>},
    {"pit 0x1E3D08 pit_entry (hit on the pit surface)", 0x001E3D08, 0x001EE1F8, run_switch<ex::pit_entry>},
    {.name = "pit 0x1E3D08 pit_entry (a pit request)", .fn = 0x001E3D08, .site = 0x001EE24C, .run = run_switch<ex::pit_entry>,
     .vary = vary_switch, .variants = 1},
    {.name = "pit 0x1E3D60 pit_stop", .fn = 0x001E3D60, .site = 0x001EDEF8, .run = run_switch<ex::pit_stop>, .vary = vary_switch,
     .variants = 1},
    {"pit 0x1E3DA8 pit_leave (the wrong way)", 0x001E3DA8, 0x001EDC78, run_switch<ex::pit_leave>},
    {"pit 0x1E3DA8 pit_leave (the race over)", 0x001E3DA8, 0x001EDE8C, run_switch<ex::pit_leave>},
    {"pit 0x1E3DA8 pit_leave (no service)", 0x001E3DA8, 0x001EDEB0, run_switch<ex::pit_leave>},
    {"pit 0x1E3DA8 pit_leave (too fast)", 0x001E3DA8, 0x001EDEDC, run_switch<ex::pit_leave>},
    {.name = "pit 0x1E3DA8 pit_leave (after the stop)", .fn = 0x001E3DA8, .site = 0x001EDF80, .run = run_switch<ex::pit_leave>,
     .vary = vary_switch, .variants = 1},
    {.name = "pit 0x1EACB8 end_of_run (the pit exit)", .fn = 0x001EACB8, .site = 0x001EE0B4, .run = run_end_of_run,
     .vary = vary_end_of_run, .variants = 6},
    {"pit 0x1EACB8 end_of_run (the lap handler's finish)", 0x001EACB8, 0x001EFC78, run_end_of_run},
    {"pit 0x1E3C78 drive_on (end of a run, mode 3)", 0x001E3C78, 0x001EAD64, run_switch<ex::drive_on>},
    {.name = "pit 0x1E3C78 drive_on (end of a run, mode 0)", .fn = 0x001E3C78, .site = 0x001EAD94, .run = run_switch<ex::drive_on>,
     .vary = vary_switch, .variants = 2},
    {.name = "pit 0x1E3E38 stop_on_line", .fn = 0x001E3E38, .site = 0x001EAD74, .run = run_switch<ex::stop_on_line>,
     .vary = vary_switch, .variants = 1},
    {"pit 0x1E3E80 stop_straight", 0x001E3E80, 0x001EAD84, [](ex::Car& c, const Call& k) { check_v0(k, u32(ex::stop_straight(c))); }},
    {"pit 0x1F6518 slow_down", 0x001F6518, 0x001EAD5C, [](ex::Car& c, const Call&) { ex::slow_down(c.ai, c.behaviour.rule(sim::Rule::SlowDown)); }, at_a0(0xEC8)},
    {"phase 4 wheel_steer (0x1E8978)", 0x001E8978, 0x001F3B9C, [](ex::Car& c, const Call&) { ex::wheel_steer(c); }},
    {"phase 5 slip_ratios (0x1E7328)", 0x001E7328, 0x001F3BD4,
     [](ex::Car& c, const Call& call) { ex::slip_ratios(c, statics(call)); }},
    // Phase 6 and its parts, each at its call sites.
    {"phase 6 tire_record (0x1DB9B8) in the indicators", 0x001DB9B8, 0x001E376C, run_tire_record, at_a0(ce::kParams)},
    {"phase 6 tire_record (0x1DB9B8) in the TCS", 0x001DB9B8, 0x001E34E0, run_tire_record, at_a0(ce::kParams)},
    {"phase 6 tire_record (0x1DB9B8) in the brakes", 0x001DB9B8, 0x001E31A4, run_tire_record, at_a0(ce::kParams)},
    {"phase 6 tcs_on (0x1E3338) in the TCS", 0x001E3338, 0x001E33AC, run_tcs_on, at_reg(22)},
    {"phase 6 tcs_on (0x1E3338) in the phase", 0x001E3338, 0x001E3BA4, run_tcs_on, at_reg(17, ce::kF)},
    {"phase 6 tcs_lateral (0x1E32D8)", 0x001E32D8, 0x001E33C8,
     [](ex::Car&, const Call& call) {
         check_f0(call, ex::tcs_lateral(ee::F::raw(call.f[0]), ee::F::raw(call.f[1]), ee::F::raw(call.f[2])));
     },
     at_reg(22)},
    {"phase 6 ascc_on (0x1E2CF8) in the ASCC", 0x001E2CF8, 0x001E3028, run_ascc_on, at_reg(17, ce::kF)},
    {"phase 6 ascc_on (0x1E2CF8) in the phase", 0x001E2CF8, 0x001E3BB0, run_ascc_on, at_reg(17, ce::kF)},
    {"phase 6 course_category (0x1F2F68)", 0x001F2F68, 0x001F2F98,
     [](ex::Car&, const Call& call) { check_v0(call, ex::course_category(course_byte(call))); },
     at_a0(-0xC8)}, // a0 = the conductor: car 0
    {"phase 6 dirt_course (0x1F2F90)", 0x001F2F90, 0x001E30A4,
     [](ex::Car&, const Call& call) { check_v0(call, ex::dirt_course(course_byte(call))); },
     at_reg(19, ce::kF)}, // s3 = F
    {"phase 6 brake_add (0x1E2CC8) A front", 0x001E2CC8, 0x001E2DF8, run_brake_add, at_reg(17, ce::kF)},
    {"phase 6 brake_add (0x1E2CC8) A rear", 0x001E2CC8, 0x001E2E18, run_brake_add, at_reg(17, ce::kF)},
    {"phase 6 brake_add (0x1E2CC8) C front", 0x001E2CC8, 0x001E2E74, run_brake_add, at_reg(17, ce::kF)},
    {"phase 6 brake_add (0x1E2CC8) C rear", 0x001E2CC8, 0x001E2E94, run_brake_add, at_reg(17, ce::kF)},
    {"phase 6 ascc (0x1E2D08)", 0x001E2D08, 0x001E31D4, run_ascc, at_a0(ce::kF)},
    {"phase 6 tcs (0x1E3360) of a pad car", 0x001E3360, 0x001E3A88, run_tcs},
    {"phase 6 tcs (0x1E3360) of an AI-driven car", 0x001E3360, 0x001E3AE8, run_tcs},
    {"phase 6 brakes (0x1E3068) of a pad car", 0x001E3068, 0x001E3AB0, run_brakes},
    {"phase 6 brakes (0x1E3068) of an AI-driven car", 0x001E3068, 0x001E3AF8, run_brakes},
    {"phase 6 steer_indicators (0x1E3708)", 0x001E3708, 0x001E3B28,
     [](ex::Car& c, const Call& call) { ex::steer_indicators(c, aid_inputs(call)); }},
    {"phase 6 driver_aids (0x1E3A68)", 0x001E3A68, 0x001E3B8C,
     [](ex::Car& c, const Call& call) { ex::driver_aids(c, aid_inputs(call)); }},
    {"phase 6 scripted_aids (0x1E3AD0), pad car with an AI driver kind", 0x001E3AD0, 0x001E3B9C, run_scripted_aids},
    {"phase 6 scripted_aids (0x1E3AD0), control 1", 0x001E3AD0, 0x001E3BC0, run_scripted_aids},
    {"phase 6 scripted_aids (0x1E3AD0), control 2", 0x001E3AD0, 0x001E3BD0, run_scripted_aids},
    {"phase 6 aids (0x1E3B18)", 0x001E3B18, 0x001F3C0C,
     [](ex::Car& c, const Call& call) { ex::aids(c, aid_inputs(call)); }},
    // Phase 7 (0x1E8598, race step site 0x1F3C44): the engine, clutch, gearbox and driveline, the tyre forces and the planar
    // motion, at their calls in 0x1E8598 in its order; each sub-function at its own call site.
    {"phase 7 dynamics (0x1E8598)", 0x001E8598, 0x001F3C44, [](ex::Car& c, const Call& call) {
         check_tires(call, c);
         const ex::Car pre = c;
         const ex::Statics s = statics(call);
         check_hybrid(call, c);
         ex::dynamics(c, s, gear_settings(call), class_grip(call));
         // (the later builds' engine speed step is not a port of its own: the layout 7 open clutch's charging, seen here)
         if (ex::hybrid_car(pre, s.hybrid) && pre.clutch == 0 && c.clutch == 0)
             ++g_cover[ex::hybrid_store(pre) / s.hybrid.capacity < ex::F::raw(0x3F800000) ? "layout 7 open clutch: charging" : "layout 7 open clutch: store full"];
     }},
    {"phase 7 brakes_and_clutch (0x1E6EC8)", 0x001E6EC8, 0x001E85A8, p7_brakes_and_clutch},
    {"phase 7 clutch_engage (0x1E6D48 in 0x1E6EC8)", 0x001E6D48, 0x001E6EF0, [](ex::Car& c, const Call&) { ex::clutch_engage(c); }},
    {"phase 7 wheel_side_speed (0x1E6CA8 in 0x1E6D48)", 0x001E6CA8, 0x001E6E5C,
     [](ex::Car& c, const Call& call) { check_f0(call, ex::wheel_side_speed(c)); }},
    {"phase 7 rolling_resistance (0x1E84D8)", 0x001E84D8, 0x001E85B4,
     [](ex::Car& c, const Call& call) { ex::rolling_resistance(c, statics(call)); }},
    {"phase 7 tyre_curves (0x1E73C0)", 0x001E73C0, 0x001E85BC, [](ex::Car& c, const Call& call) {
         check_tires(call, c);
         ex::tyre_curves(c);
     }},
    {"phase 7 tyre_long_force (0x1E7450)", 0x001E7450, 0x001E85C4,
     [](ex::Car& c, const Call& call) { ex::tyre_long_force(c, statics(call)); }},
    {"phase 7 tyre_slip_force (0x1E7C08)", 0x001E7C08, 0x001E85CC, [](ex::Car& c, const Call& call) {
         check_tires(call, c);
         ex::tyre_slip_force(c);
     }},
    {"phase 7 forward_step (0x1E7848)", 0x001E7848, 0x001E85D4, [](ex::Car& c, const Call&) { ex::forward_step(c); }},
    {"phase 7 wheel_net_torque (0x1E5768)", 0x001E5768, 0x001E85DC, [](ex::Car& c, const Call&) { ex::wheel_net_torque(c); }},
    {"phase 7 torque_vectoring (0x1E57F8)", 0x001E57F8, 0x001E85E4, p7_torque_vectoring},
    {"phase 7 axle_step (0x1E5B98)", 0x001E5B98, 0x001E85EC, p7_axle_step},
    {"phase 7 engine_torque (0x1E17F0 in 0x1E5B98)", 0x001E17F0, 0x001E5CA0,
     [](ex::Car& c, const Call& call) { check_f0(call, ex::engine_torque(c, ee::F::raw(call.f[0]))); }},
    {"phase 7 boost at rpm (0x1E0ED8 in 0x1E17F0)", 0x001E0ED8, 0x001E185C,
     [](ex::Car& c, const Call& call) { check_f0(call, ex::boost(c.p.e, s16(call.a[1]), s16(call.a[2]))); }, at_a0(0x148)},
    {"phase 7 boost at the lags (0x1E0ED8 in 0x1E17F0)", 0x001E0ED8, 0x001E1918,
     [](ex::Car& c, const Call& call) { check_f0(call, ex::boost(c.p.e, s16(call.a[1]), s16(call.a[2]))); }, at_a0(0x148)},
    {"phase 7 engine_output (0x1E1768 in 0x1E17F0)", 0x001E1768, 0x001E1A4C, p7_engine_output},
    {"phase 7 awd5_gain (0x1E5998 in 0x1E5B98)", 0x001E5998, 0x001E5E90, p7_awd5_gain},
    {"phase 7 clutch_slip_factor (0x1E5B30 in 0x1E5B98)", 0x001E5B30, 0x001E5CF4,
     [](ex::Car&, const Call& call) { check_f0(call, ex::clutch_slip_factor(ee::F::raw(call.f[0]), ex::toward_zero(disc_behaviour()))); }, at_reg(18)}, // car in s2
    {"phase 7 wheel_speeds (0x1E6488)", 0x001E6488, 0x001E85F4, p7_wheel_speeds},
    {"phase 7 engine_speed (0x1E7500)", 0x001E7500, 0x001E85FC, p7_engine_speed},
    {"phase 7 engine_torque (0x1E17F0 in 0x1E7500)", 0x001E17F0, 0x001E7554,
     [](ex::Car& c, const Call& call) { check_f0(call, ex::engine_torque(c, ee::F::raw(call.f[0]))); }},
    {"phase 7 wheel_side_speed (0x1E6CA8 in 0x1E7500)", 0x001E6CA8, 0x001E7588,
     [](ex::Car& c, const Call& call) { check_f0(call, ex::wheel_side_speed(c)); }},
    {"phase 7 engine_rpm (0x1E6D00 in 0x1E7500)", 0x001E6D00, 0x001E77B4, [](ex::Car& c, const Call&) { ex::engine_rpm(c); }},
    {"phase 7 gear_step (0x1E77E0)", 0x001E77E0, 0x001E8604, p7_gear_step},
    {"phase 7 gear_decision (0x1E5690 in 0x1E77E0)", 0x001E5690, 0x001E77F0,
     [](ex::Car& c, const Call& call) { check_v0(call, u32(ex::gear_decision(c, gear_settings(call)))); }},
    {"phase 7 set_gear (0x1E4E78 in 0x1E77E0)", 0x001E4E78, 0x001E7830,
     [](ex::Car& c, const Call& call) { ex::set_gear(c, u8(call.a[1])); }, at_a0(ce::kF)},
    {"phase 7 shift_auto (0x1E4FF8 in 0x1E5690, mode 0)", 0x001E4FF8, 0x001E5704,
     [](ex::Car& c, const Call& call) { check_v0(call, u32(ex::shift_auto(c, gear_settings(call), false))); }},
    {"phase 7 shift_auto (0x1E4FF8 in 0x1E5690, mode 2)", 0x001E4FF8, 0x001E5718,
     [](ex::Car& c, const Call& call) { check_v0(call, u32(ex::shift_auto(c, gear_settings(call), true))); }},
    {"phase 7 shift_manual (0x1E5458 in 0x1E5690, mode 1)", 0x001E5458, 0x001E5728,
     [](ex::Car& c, const Call& call) { check_v0(call, u32(ex::shift_manual(c))); }},
    {"phase 7 shift_sequential (0x1E5540 in 0x1E5690, modes 3 4 6)", 0x001E5540, 0x001E5738,
     [](ex::Car& c, const Call& call) { check_v0(call, u32(ex::shift_sequential(c))); }},
    {"phase 7 shift_sequential_reverse (0x1E5600 in 0x1E5690, mode 5)", 0x001E5600, 0x001E5748,
     [](ex::Car& c, const Call& call) { check_v0(call, u32(ex::shift_sequential_reverse(c))); }},
    {"phase 7 lateral_yaw (0x1E7D90)", 0x001E7D90, 0x001E8644, [](ex::Car& c, const Call& call) {
         check_tires(call, c);
         ex::lateral_yaw(c, airborne_arg(call, c));
     }},
    {"phase 7 stop_hold (0x1E8008)", 0x001E8008, 0x001E8650,
     [](ex::Car& c, const Call& call) { ex::stop_hold(c, airborne_arg(call, c)); }},
    {"phase 8 brake_heat (0x1E6928)", 0x001E6928, 0x001F3C7C,
     [](ex::Car& c, const Call& call) { ex::brake_heat(c, brake_heat_consts(call)); }},
    {"phase 9 clear_feedback (0x1DFD90)", 0x001DFD90, 0x001F3CB4, [](ex::Car& c, const Call&) { ex::clear_feedback(c); }},
    // Phase 12 (0x1EB218): the whole phase, its calls in order, then sub-functions at their own call sites.
    {"phase 12 chassis (0x1EB218)", 0x001EB218, 0x001F3D34, [](ex::Car& c, const Call& call) {
         const ex::SurfaceBumps sb = bumps(call);
         const ex::SteerFeel feel = steer_feel(call);
         const ex::EffectTables effects = effect_tables(call);
         const ex::CourseOut rule = course_out_rule(call);
         ex::ChassisInputs in;
         in.course = call.course, in.bumps = &sb, in.mark_flagged = mark_flagged(call);
         in.box_heading = box_heading(call, c), in.feel = &feel, in.effects = &effects;
         in.course_kind = course_byte(call);
         in.sound_scales = sound_scales(call);
         draw_words(call, in.x70, in.ghost_switch);
         in.condition = condition(call);
         in.course_out = rule.on ? &rule : nullptr;
         in.ride = built().race.ride.on ? &built().race.ride : nullptr;
         (void)ex::chassis(c, in);
     }},
    {"phase 12 hub_offsets_xy (0x1E9D48)", 0x001E9D48, 0x001EB22C, [](ex::Car& c, const Call&) { ex::hub_offsets_xy(c); }},
    {"phase 12 speed_ramp (0x1E9E18)", 0x001E9E18, 0x001EB234, [](ex::Car& c, const Call&) { ex::speed_ramp(c); }, at_a0(ce::kF)},
    // Phase 12's ground contact (0x1E8B68): the course query and the surface helpers at their calls in it, then all of it.
    {"phase 12 course ground query (0x18FB58)", 0x0018FB58, 0x001E8C14, [](ex::Car& c, const Call& call) { call.course->query(c.g, c.behaviour); },
     at_reg(5, ce::kGround0)},
    {"phase 12 surface_index (0x1E8A48)", 0x001E8A48, 0x001E8C60,
     [](ex::Car& c, const Call& call) {
         const int i = ground_index(call, u32(call.a[0]));
         const u32 theirs = call.ee->g32(2), ours = u32(ex::surface_index(c.g[size_t(i & 3)]));
         if (theirs != ours) call.differ(rh::fmt("wheel %d: %u vs %u", i, ours, theirs));
     },
     at_reg(20)},
    {"phase 12 soft_material (0x1E8AC0)", 0x001E8AC0, 0x001E8C88,
     [](ex::Car& c, const Call& call) {
         const int i = ground_index(call, u32(call.a[0]));
         const u32 theirs = call.ee->g32(2), ours = u32(ex::soft_material(c.g[size_t(i & 3)], bumps(call)));
         if (theirs != ours) call.differ(rh::fmt("wheel %d: %u vs %u", i, ours, theirs));
     },
     at_reg(20)},
    {"phase 12 bump_wave (0x1E8AE0)", 0x001E8AE0, 0x001E8CE8,
     [](ex::Car&, const Call& call) {
         check_f0(call, ex::bump_wave(int(s32(call.a[0])), ee::F::raw(call.f[0]), ee::F::raw(call.f[1]), ee::F::raw(call.f[2])));
     },
     at_reg(20)},
    {"phase 12 ground_contact (0x1E8B68)", 0x001E8B68, 0x001EB23C,
     [](ex::Car& c, const Call& call) {
         const ex::GroundCounts n = ex::ground_contact(c, *call.course, bumps(call), mark_flagged(call));
         if (const ex::CourseOut rule = course_out_rule(call); rule.on) ex::course_out(c, rule, n);
     }},
    // the counts it hands the conductor's course-out hook (virtual 40; the single race conductor's 0x1F2918, empty)
    {"phase 12 course-out hook counts (0x1E8B68 -> vf40)", 0x001F2918, 0x001E8D94,
     [](ex::Car& c, const Call& call) {
         const ex::GroundCounts n = ex::ground_counts(c, bumps(call));
         if (u64(n.flagged) != call.a[2] || u64(n.soft) != call.a[3])
             call.differ(rh::fmt("counts %d / %d vs %d / %d", n.flagged, n.soft, int(call.a[2]), int(call.a[3])));
     },
     at_reg(20)},
    // the base conductor's virtual 40 there (time trial, licence, free run, machine test): the course-out rule (a1 = the car)
    {"phase 12 course_out (0x1DA020, the base conductor's virtual 40)", 0x001DA020, 0x001E8D94,
     [](ex::Car& c, const Call& call) {
         const ex::GroundCounts n = ex::ground_counts(c, bumps(call));
         if (u64(n.flagged) != call.a[2] || u64(n.soft) != call.a[3])
             call.differ(rh::fmt("counts %d / %d vs %d / %d", n.flagged, n.soft, int(call.a[2]), int(call.a[3])));
         const ex::CourseOut rule = course_out_rule(call);
         if (!rule.on) call.differ(std::string("the ") + ex::conductor_name(built().kind) + " conductor has no course-out rule");
         ex::course_out(c, rule, n);
     },
     at_reg(5)},
    {"phase 12 terrain_factor (0x1E91A8)", 0x001E91A8, 0x001EB244, [](ex::Car& c, const Call&) { ex::terrain_factor(c); }},
    {"phase 12 suspension (0x1E9348)", 0x001E9348, 0x001EB24C, [](ex::Car& c, const Call&) { ex::suspension(c, built().race.ride.on ? &built().race.ride : nullptr); }},
    {"phase 12 attitude (0x1E96F0)", 0x001E96F0, 0x001EB254,
     [](ex::Car& c, const Call& k) { ex::attitude(c, box_heading(k, c)); }},
    {"phase 12 bump_nudge (0x1E9998)", 0x001E9998, 0x001EB25C, [](ex::Car& c, const Call&) { ex::bump_nudge(c); }},
    {.name = "phase 12 pad_rumble (0x1DF2B0)", .fn = 0x001DF2B0, .site = 0x001EB264,
     .run = [](ex::Car& c, const Call&) { ex::pad_rumble(c); }, .vary = vary_rumble, .variants = 3},
    {.name = "phase 12 steer_force (0x1E0068)", .fn = 0x001E0068, .site = 0x001EB26C,
     .run = [](ex::Car& c, const Call& k) { ex::steer_force(c, steer_feel(k)); }, .vary = vary_steer_force, .variants = 5},
    {.name = "phase 12 tyre_effects (0x1DEBB0)", .fn = 0x001DEBB0, .site = 0x001EB274,
     .run = [](ex::Car& c, const Call& k) { ex::tyre_effects(c, effect_tables(k), course_byte(k)); },
     .vary = vary_tyre_effects, .variants = 3},
    {"phase 12 stub (0x1DF148)", 0x001DF148, 0x001EB27C, [](ex::Car& c, const Call&) { ex::phase12_stub(c); }},
    {.name = "phase 12 engine_sound (0x1DFC60)", .fn = 0x001DFC60, .site = 0x001EB284,
     .run =
         [](ex::Car& c, const Call& k) { ex::engine_sound(c, sound_scales(k)); },
     .vary = vary_engine_sound, .variants = 1},
    {.name = "phase 12 effects_reset (0x1E06D8)", .fn = 0x001E06D8, .site = 0x001EB28C,
     .run =
         [](ex::Car& c, const Call& k) {
             u32 x70, ghost_switch;
             draw_words(k, x70, ghost_switch);
             ex::effects_reset(c, x70, ghost_switch);
         },
     .vary = vary_effects_reset, .variants = 4},
    {.name = "phase 12 view_follow (0x1E0768)", .fn = 0x001E0768, .site = 0x001EB294,
     .run = [](ex::Car& c, const Call&) { ex::view_follow(c); }, .car = at_a0(ce::kF), .vary = vary_view, .variants = 1},
    {"phase 12 hub_offsets (0x1E9DE8)", 0x001E9DE8, 0x001EB29C, [](ex::Car& c, const Call&) { ex::hub_offsets(c); }},
    {"phase 12 body_axes (0x1D8A80)", 0x001D8A80, 0x001EB2B8, [](ex::Car& c, const Call&) { ex::body_axes(c); }, at_a0(ce::kF + 0xC)},
    {"phase 12 ground_frame (0x1EA4D8)", 0x001EA4D8, 0x001EB2C0, [](ex::Car& c, const Call&) { ex::ground_frame(c); }},
    {.name = "phase 12 speed_bookkeeping (0x1EA9B8)", .fn = 0x001EA9B8, .site = 0x001EB2C8,
     .run = [](ex::Car& c, const Call& k) { ex::speed_bookkeeping(c, condition(k)); },
     .vary = vary_speeds, .variants = 3},
    {.name = "phase 12 wheel_spin (0x1EAFC0)", .fn = 0x001EAFC0, .site = 0x001EB2D0,
     .run = [](ex::Car& c, const Call&) { ex::wheel_spin(c); }, .vary = vary_wheel_spin, .variants = 2},
    // phase 12's last step: each tyre effect request at the emitter's call, then the whole of 0x1DF150 (no record change)
    {"phase 12 tyre effect request (0x1DF150 -> 0x18FDE0)", 0x0018FDE0, 0x001DF270,
     [](ex::Car& c, const Call& call) {
         const u32 q = u32(call.a[1]);
         const s32 i = s32(call.ee->r32(q));
         if (i < 0 || i > 3) return call.differ(rh::fmt("wheel index %d", i));
         call.differ(tyre_effect_diff(*call.ee, q, ex::tyre_effect_requests(c)[size_t(i)]));
     },
     at_reg(17)},
    {"phase 12 tyre_effect_requests (0x1DF150)", 0x001DF150, 0x001EB2D8, [](ex::Car& c, const Call&) { (void)ex::tyre_effect_requests(c); }},
    {"phase 12 wheel_suspension (0x1E8E58 in 0x1E9348)", 0x001E8E58, 0x001E93D8,
     [](ex::Car& c, const Call& k) {
         const size_t i = size_t(k.a[3]);
         // (a later build's caller gives the spring rate, f13: special car 1's sitting lowers it, Rule::PodSpring)
         if (sim::concept_program(program())) {
             const ex::F rate = ex::pod_spring(c, int(i), c.p.susp[i >> 1].k);
             if (rate.bits != k.f[1]) k.differ(rh::fmt("spring rate 0x%08X vs 0x%08X", rate.bits, k.f[1]));
             if (!(rate == c.p.susp[i >> 1].k)) ++g_cover["later: special car 1's spring rate cut"];
         }
         ex::wheel_suspension(c.w[i], c.p.susp[i >> 1], c.inv_dt, ee::F::raw(k.f[0]), built().race.ride.on ? &built().race.ride : nullptr, &c, int(i));
     },
     CarAt{4, ce::kF, true}},
    {"phase 12 suspension_coords (0x1E9010 in 0x1E9998)", 0x001E9010, 0x001E9A00,
     [](ex::Car& c, const Call& k) { ex::suspension_coords(c, ee::F::raw(k.f[0])); }, at_a0(ce::kF)},
    {"phase 12 spawn_hold (0x1E9528 in 0x1E96F0)", 0x001E9528, 0x001E9724,
     [](ex::Car& c, const Call& k) { ex::spawn_hold(c, box_heading(k, c)); }},
    {"phase 12 body_axes (0x1D8A80 in 0x1E96F0)", 0x001D8A80, 0x001E98CC,
     [](ex::Car& c, const Call&) { ex::body_axes(c); }, at_a0(ce::kF + 0xC)},
    {"phase 12 hub_offsets_z (0x1E9CE8 in 0x1E9998)", 0x001E9CE8, 0x001E99F0,
     [](ex::Car& c, const Call&) { ex::hub_offsets_z(c); }},
    {"phase 12 hub_offsets_xy (0x1E9D48 in 0x1E9DE8)", 0x001E9D48, 0x001E9DFC,
     [](ex::Car& c, const Call&) { ex::hub_offsets_xy(c); }},
    {"phase 13 tire_wear (0x1E8190)", 0x001E8190, 0x001F3D6C,
     [](ex::Car& c, const Call& call) { ex::tire_wear(c, wear_params(call)); }},
    // Taps: v0 after the conductor's virtual 39 (0x1E8388) and at 0x1D9FE0's return (0x1DA014: virtual 35's answer, or 0).
    {"phase 13 wear_flags (0x1E8340)", 0x001E8340, 0x001F3DA4,
     [](ex::Car& c, const Call& call) {
         const ex::PitMode m = pit_mode(call, c);
         ex::wear_flags(c, wear_params(call), m);
     },
     {}, nullptr, {0x001E8388, 0x001DA014}},
    {"phase 13 flaps (0x1E6A38)", 0x001E6A38, 0x001F3DDC,
     [](ex::Car& c, const Call& call) {
         Vu0Random rng;
         rng.r = call.vr;
         ex::flaps(c, rng);
         if (rng.r != call.ee->vr) call.differ(rh::fmt("VU0 R 0x%08X vs 0x%08X", rng.r, call.ee->vr));
     }},
    // The AI's lines and speed plan (ai §2-§4), in the AI's decision 0x1F7AE0 (phase 3's AI paths), the driver update 0x1F6648
    // (the pad path), the traffic step 0x1F6D70 and the pit lane's line switches; a0 = the driver (car + 0xEC8) unless noted.
    {.name = "AI 0x1F79A0 ai_begin (in 0x1F7AE0)", .fn = 0x001F79A0, .site = 0x001F7B48,
     .run = [](ex::Car& c, const Call&) { ex::ai_begin(c); }, .car = at_a0(0xEC8), .vary = vary_begin, .variants = 1},
    {"AI 0x1F79A0 ai_begin (in the stop driver 0x1F7810)", 0x001F79A0, 0x001F7830, [](ex::Car& c, const Call&) { ex::ai_begin(c); }, at_a0(0xEC8)},
    {"AI 0x1F79A0 ai_begin (in the parked car 0x1F7900)", 0x001F79A0, 0x001F7914, [](ex::Car& c, const Call&) { ex::ai_begin(c); }, at_a0(0xEC8)},
    {.name = "AI 0x1F7088 ai_braking (in 0x1F7AE0)", .fn = 0x001F7088, .site = 0x001F7B74, .run = ai_braking_port, .car = at_a0(0xEC8),
     .vary = vary_braking, .variants = 4},
    {"AI 0x1F7088 ai_braking (in the pad path's 0x1F6648)", 0x001F7088, 0x001F6654, ai_braking_port, at_a0(0xEC8)},
    {"AI 0x18FA10 lap length (in 0x1F7088)", 0x0018FA10, 0x001F7108,
     [](ex::Car&, const Call& call) { check_f0(call, ai_course(call).lap_length); }, at_reg(17, 0xEC8)},
    {"AI 0x1F6958 ai_next_segment (in 0x1F7088)", 0x001F6958, 0x001F71E8,
     [](ex::Car& c, const Call& call) { ex::ai_next_segment(c, ai_course(call), ai_wear(call)); }, at_a0(0xEC8)},
    {"AI 0x1F6958 ai_next_segment (in 0x1F6A20)", 0x001F6958, 0x001F6B24,
     [](ex::Car& c, const Call& call) { ex::ai_next_segment(c, ai_course(call), ai_wear(call)); }, at_a0(0xEC8)},
    {"AI 0x1F6678 ai_corner_speed (in 0x1F6958)", 0x001F6678, 0x001F69F8, ai_corner_port, at_a0(0xEC8)},
    {"AI 0x1F6678 ai_corner_speed (in 0x1F6A20)", 0x001F6678, 0x001F6B18, ai_corner_port, at_a0(0xEC8)},
    {.name = "AI 0x1F6A20 ai_replan (in 0x1F8980)", .fn = 0x001F6A20, .site = 0x001F89CC,
     .run = [](ex::Car& c, const Call& call) { ex::ai_replan(c, ai_course(call), ai_wear(call)); }, .car = at_a0(0xEC8),
     .vary = vary_replan, .variants = 2},
    {"AI 0x1F6A20 ai_replan (in 0x1F8958)", 0x001F6A20, 0x001F896C,
     [](ex::Car& c, const Call& call) { ex::ai_replan(c, ai_course(call), ai_wear(call)); }, at_a0(0xEC8)},
    {"AI 0x1F8958 ai_set_line (in 0x1F8980)", 0x001F8958, 0x001F89DC,
     [](ex::Car& c, const Call& call) { ex::ai_set_line(c, ai_course(call), ai_wear(call), int(call.a[1])); }, at_a0(0xEC8)},
    {"AI 0x1F9BF8 resolve (in 0x1F8980)", 0x001F9BF8, 0x001F8998,
     [](ex::Car&, const Call& call) { check_v0(call, u32(ai_course(call).resolve(int(call.a[1])))); }, at_reg(16, 0xEC8)},
    {"AI 0x1F8980 ai_request_line (traffic: closing)", 0x001F8980, 0x001F6F30, ai_request_port, at_a0(0xEC8)},
    {"AI 0x1F8980 ai_request_line (traffic: alongside, right)", 0x001F8980, 0x001F6FFC, ai_request_port, at_a0(0xEC8)},
    {"AI 0x1F8980 ai_request_line (traffic: alongside left, the main line)", 0x001F8980, 0x001F7040, ai_request_port, at_a0(0xEC8)},
    {"AI 0x1F8980 ai_request_line (pit exit 0x1E3BF0)", 0x001F8980, 0x001E3C4C, ai_request_port, at_a0(0xEC8)},
    {"AI 0x1F8980 ai_request_line (end of a run 0x1E3C78)", 0x001F8980, 0x001E3CD0, ai_request_port, at_a0(0xEC8)},
    {"AI 0x1F8980 ai_request_line (pit entry 0x1E3D08)", 0x001F8980, 0x001E3D20, ai_request_port, at_a0(0xEC8)},
    {"AI 0x1F8980 ai_request_line (pit stop 0x1E3D60)", 0x001F8980, 0x001E3D78, ai_request_port, at_a0(0xEC8)},
    {"AI 0x1F8980 ai_request_line (drive-through 0x1E3DA8)", 0x001F8980, 0x001E3DC0, ai_request_port, at_a0(0xEC8)},
    {"AI 0x1F8980 ai_request_line (stop driver 0x1E3E38)", 0x001F8980, 0x001E3E50, ai_request_port, at_a0(0xEC8)},
    {"AI 0x1F9CE0 lane_left (traffic: closing)", 0x001F9CE0, 0x001F6E88,
     [](ex::Car& c, const Call& call) { ai_lane_port(c, call, true); }, at_reg(16, 0xEC8)},
    {"AI 0x1F9CE0 lane_left (traffic: alongside)", 0x001F9CE0, 0x001F7028,
     [](ex::Car& c, const Call& call) { ai_lane_port(c, call, true); }, at_reg(16, 0xEC8)},
    {"AI 0x1F9D38 lane_right (traffic: closing)", 0x001F9D38, 0x001F6EAC,
     [](ex::Car& c, const Call& call) { ai_lane_port(c, call, false); }, at_reg(16, 0xEC8)},
    {"AI 0x1F9D38 lane_right (traffic: alongside)", 0x001F9D38, 0x001F6FE4,
     [](ex::Car& c, const Call& call) { ai_lane_port(c, call, false); }, at_reg(16, 0xEC8)},
    {"AI 0x1F7948 ai_lookahead", 0x001F7948, 0x001F7C68,
     [](ex::Car& c, const Call& call) { check_f0(call, ex::ai_lookahead(c.ai, ex::F::raw(call.f[0]))); }, at_a0(0xEC8 + 0xC)},
    {"AI 0x1F7658 ai_target (the line)", 0x001F7658, 0x001F7CE0, ai_target_port, at_reg(5, ce::kF)},
    {"AI 0x1F7658 ai_target (the blend's old line)", 0x001F7658, 0x001F7D14, ai_target_port, at_reg(5, ce::kF)},
    {"AI 0x1FA018 evaluate (in 0x1F7658)", 0x001FA018, 0x001F76F0, ai_evaluate_port, at_reg(17, ce::kF)},
    {"AI 0x1FA368 evaluate_stop (in 0x1F7658)", 0x001FA368, 0x001F7714, ai_evaluate_stop_port, at_reg(17, ce::kF)},
    {"AI 0x1F7590 ai_speed_control (recovery state 2)", 0x001F7590, 0x001F8398, ai_speed_port, at_a0(0xEC8)},
    {.name = "AI 0x1F7590 ai_speed_control (mode 3)", .fn = 0x001F7590, .site = 0x001F83DC, .run = ai_speed_port, .car = at_a0(0xEC8),
     .vary = vary_speed, .variants = 1},
    {"AI 0x1F7590 ai_speed_control (mode 4)", 0x001F7590, 0x001F83F8, ai_speed_port, at_a0(0xEC8)},
    {"AI 0x1F7590 ai_speed_control (arc modes)", 0x001F7590, 0x001F8440, ai_speed_port, at_a0(0xEC8)},
    {"AI 0x1F7520 ai_throttle_for", 0x001F7520, 0x001F75C8,
     [](ex::Car&, const Call& call) {
         check_f0(call, ex::ai_throttle_for(ex::F::raw(call.f[0]), ex::F::raw(call.f[1]), ex::F::raw(call.f[2]), ex::F::raw(call.f[3]), ex::toward_zero(disc_behaviour())));
     },
     at_a0(0xEC8)},
    // The race progress the conductor's virtual 28 gives (0x1EE900 inside it; the machine test's 0x1EE878 at the nose): our
    // course query on the race's course, the result in f0 (a1 = the car).
    {"race progress (0x1EE900 / 0x18FA20, the conductor's virtual 28)", 0x001EE900, 0x001EE960,
     [](ex::Car& c, const Call& call) { check_f0(call, ex::car_progress(c, *g_query)); }, at_reg(5)},
    {"race progress at the nose (0x1EE878, the machine test's virtual 28)", 0x001EE878, 0x001F06A8,
     [](ex::Car& c, const Call& call) { check_f0(call, ex::nose_progress(c, *g_query)); }, at_reg(5)},
};

// A phase the race step calls once for all cars (a0 = the conductor): every car's record is snapshotted, the port runs on all
// of them and every car's fields must match. The conductor bytes [keep, keep + keep_size) the call also writes are the port's
// state: snapshotted at the entry (Call::cond) and compared by the port (Call::out).
struct RacePort {
    const char* name;
    Pa fn, site; // NTSC-U addresses (Pa reads the disc's)
    Co keep; // a conductor offset (NTSC-U's)
    u32 keep_size;
    void (*run)(std::vector<ex::Car>& cars, const Call& call);
};

constexpr Co kProxSlip = 0xB7B4;                   // conductor offset of the proximity matrices (NTSC-U's)
constexpr u32 kProxCodeAt = 0xB8B4 - 0xB7B4; // the codes, from kProxSlip

ex::Proximity read_proximity(const u8* b) { // b = conductor + kProxSlip
    ex::Proximity k;
    for (size_t i = 0; i < 8; ++i)
        for (size_t j = 0; j < 8; ++j) {
            k.slip[i][j] = ee::F::raw(rd32(ByteSpan(b, 0x140), 0x20 * i + 4 * j));
            k.code[i][j] = b[kProxCodeAt + 8 * i + j];
        }
    return k;
}

const RacePort kRacePorts[] = {
    {"phase 11 proximity (0x1EC098)", 0x001EC098, 0x001F3D08, kProxSlip, 0x140,
     [](std::vector<ex::Car>& cars, const Call& call) {
         ex::Proximity k = read_proximity(call.cond.data());
         ex::proximity(cars, k);
         std::vector<u8> post(0x140);
         call.ee->read_bytes(call.car + kProxSlip, post.data(), 0x140);
         const ex::Proximity theirs = read_proximity(post.data());
         for (size_t i = 0; i < 8; ++i)
             for (size_t j = 0; j < 8; ++j) {
                 if (k.slip[i][j].bits != theirs.slip[i][j].bits)
                     call.differ(rh::fmt("slip[%zu][%zu] 0x%08X vs 0x%08X", i, j, k.slip[i][j].bits, theirs.slip[i][j].bits));
                 else if (k.code[i][j] != theirs.code[i][j])
                     call.differ(rh::fmt("code[%zu][%zu] 0x%02X vs 0x%02X", i, j, k.code[i][j], theirs.code[i][j]));
             }
     }},
};

struct Stats {
    long calls = 0, bad = 0, active = 0, skipped = 0; // active: calls that changed the record; skipped: refused by Port::only
    std::string first;
    std::set<u32> unmirrored;
    long vcalls = 0, vbad = 0, vactive = 0; // the variants
    std::string vfirst;
};

// Runs every variant of the ports that have them on each car of a CPU race: a copy of the record at `copy`, changed, then
// the original called directly on it and ours on the changed mirror.
// The base conductor's course-out rule (virtual 40, 0x1DA020) called directly on a copy of each car's record, in the races
// whose conductor has it: the flag, the control source and the course-out ticks in their states, the lap progress in, at
// and around the pit stretch (and below half a lap), every pair of wheel counts; ours (ex::course_out) must leave the same
// record.
void run_course_out(Ee& ee, Rng& rng, Stats& s, u32 cond, int ncars, u32 copy, const std::string& where) {
    const u32 vt = ee.r32(cond + race_setup::kK8 + k_off(0x63A8)), e40 = vt + 8 * u32(conductor_slot(40));
    if (ee.r32(e40 + 4) != pa(0x001DA020)) return;
    const u32 self = cond + u32(s32(s16(ee.r16(e40))));
    for (int i = 0; i < ncars; ++i) {
        const std::vector<u8> raw = ce::snapshot(ee, race_setup::car_addr(cond, i));
        for (int k = 0; k < 12; ++k) {
            ee.write_bytes(copy, ByteSpan(raw.data(), raw.size()));
            ex::Car m = ce::read_car(ee, copy);
            std::string bad;
            const ex::CourseOut rule = course_out_rule(ee, copy, bad);
            m.course_out = u8(rng.chance(0.8) ? 0 : 2);
            m.control = s8(rng.chance(0.8) ? 0 : 1);
            const int t = int(rng.range(0, 3));
            m.off_count = u8(t == 0 ? 0 : t == 1 ? rng.range(1, 254) : t == 2 ? 0xFE : 0xFF);
            const float lap = rule.lap.host(), lo = rule.lo.host(), hi = rule.hi.host();
            switch (k % 4) {
            case 0: m.f3e4 = ee::F(rng.uniform(lo, hi)); break;
            case 1: m.f3e4 = rng.chance(0.5) ? rule.lo : rule.hi; break;
            case 2: m.f3e4 = ee::F(rng.uniform(-50.0f, lap * 0.5f)); break;
            default: m.f3e4 = ee::F(rng.uniform(0.0f, lap + 50.0f)); break;
            }
            ce::write_car(ee, copy, m);
            const ex::GroundCounts n{k < 6 ? 4 : int(rng.range(0, 4)), k % 3 == 0 ? 4 : int(rng.range(0, 4))};
            const std::vector<u8> pre = ce::snapshot(ee, copy);
            ee.call(pa(0x001DA020), {Arg::ptr(self), Arg::ptr(copy), Arg::integer(n.flagged), Arg::integer(n.soft)}, 10'000'000);
            ex::Car ours = m;
            ex::course_out(ours, rule, n);
            if (m.course_out == 0 && m.control == 0) {
                if (n.flagged == 4 && rule.pit > 0 && ex::in_pit_stretch(rule, m.f3e4)) ++g_cover["course-out: all wheels on the pit stretch"];
                if (n.soft == 4 && rule.soft > 0) ++g_cover["course-out: all wheels soft"];
            }
            ++s.calls;
            std::string d = ce::first_difference(ours, ce::read_car(ee, copy));
            if (d.empty()) d = bad;
            if (!d.empty() && ++s.bad == 1) s.first = rh::fmt("%s car %d case %d: ", where.c_str(), i, k) + d;
            const std::vector<u8> post = ce::snapshot(ee, copy);
            if (post != pre) ++s.active;
            for (u32 off : ce::unmirrored_changes(pre, post)) s.unmirrored.insert(off);
        }
    }
}

// The gap matrices (0x1ED458) and the ranking (the conductor's virtual 3) called directly on changed race states: each car's
// laps, lap base, progress, finish flag, crossing time and best lap set at random (laps and progress near the others', ties
// included) and the order shuffled; ours (ex::gap_matrix, ex::race_ranking) must leave the same matrices, order and records.
// The race's state is restored afterwards.
void run_rank_direct(Ee& ee, Rng& rng, Stats& gs, Stats& rs, u32 cond, int ncars, float lap, ex::Conductor kind, const std::string& where) {
    if (ncars < 2) return;
    const u32 K = cond + race_setup::kK8;
    std::vector<std::vector<u8>> raw;
    for (int i = 0; i < ncars; ++i) raw.push_back(ce::snapshot(ee, race_setup::car_addr(cond, i)));
    std::vector<u8> mats(0x140), order_raw(32);
    ee.read_bytes(cond + cond_off(0xB8F4), mats.data(), u32(mats.size()));
    ee.read_bytes(cond + cond_off(0xE388), order_raw.data(), u32(order_raw.size()));
    const u32 vt = ee.r32(K + k_off(0x63A8)), e3 = vt + 8 * 3;
    const u32 self = cond + u32(s32(s16(ee.r16(e3)))), fn3 = ee.r32(e3 + 4);
    for (int k = 0; k < 6; ++k) {
        std::vector<ex::Car> cars;
        const s64 base = rng.range(0, 3);
        for (int i = 0; i < ncars; ++i) {
            ex::Car m = ce::read_car(ee, race_setup::car_addr(cond, i));
            m.laps = s16(base + rng.range(0, 2));
            m.lap_base = u8(rng.chance(0.2) ? 1 : 0);
            m.f3e4 = i > 0 && rng.chance(0.15) ? cars[0].f3e4 : ee::F(rng.uniform(-20.0f, lap + 20.0f));
            m.finished = u8(k >= 2 && rng.chance(0.5) ? 1 : 0);
            m.x530 = i > 0 && rng.chance(0.2) ? cars[0].x530 : u32(rng.range(1000, 5000));
            m.best_lap = i > 0 && rng.chance(0.2) ? cars[0].best_lap : u32(rng.range(60000, 90000));
            ce::write_car(ee, race_setup::car_addr(cond, i), m);
            cars.push_back(m);
        }
        ex::RaceOrder order;
        order.fill(-1);
        for (int i = 0; i < ncars; ++i) order[size_t(i)] = i;
        for (int i = ncars - 1; i > 0; --i) std::swap(order[size_t(i)], order[size_t(rng.range(0, i))]);
        for (int i = 0; i < ncars; ++i) ee.w32(cond + cond_off(0xE388) + 4 * u32(i), u32(order[size_t(i)]));
        const ex::RaceOrder shuffled = order;
        ex::GapMatrix ours = ti::mem_gaps(ee, cond);
        ee.call(pa(0x001ED458), {Arg::ptr(cond)}, 10'000'000);
        ex::gap_matrix(cars, ee::F(lap), ours);
        const ex::GapMatrix theirs = ti::mem_gaps(ee, cond);
        std::string d;
        for (size_t i = 0; i < 8 && d.empty(); ++i)
            for (size_t j = 0; j < 8 && d.empty(); ++j)
                if (ours.laps[i][j] != theirs.laps[i][j] || ours.gap[i][j].bits != theirs.gap[i][j].bits)
                    d = rh::fmt("[%zu][%zu]: %d 0x%08X vs %d 0x%08X", i, j, ours.laps[i][j], ours.gap[i][j].bits, theirs.laps[i][j],
                                theirs.gap[i][j].bits);
        for (size_t i = 0; i < size_t(ncars); ++i)
            for (size_t j = 0; j < size_t(ncars); ++j)
                if (i != j && ours.laps[i][j] != 0) ++g_cover[ours.laps[i][j] > 0 ? "gap matrix direct: laps behind" : "gap matrix direct: laps ahead"];
        ++gs.calls, ++gs.active;
        if (!d.empty() && ++gs.bad == 1) gs.first = where + rh::fmt(" case %d: ", k) + d;
        std::vector<ex::Car> ranked = cars;
        ee.call(fn3, {Arg::ptr(self)}, 10'000'000);
        ex::race_ranking(ranked, order, kind);
        d.clear();
        for (int i = 0; i < ncars && d.empty(); ++i)
            if (const s32 t = s32(ee.r32(cond + cond_off(0xE388) + 4 * u32(i))); t != order[size_t(i)]) d = rh::fmt("order [%d]: %d vs %d", i, order[size_t(i)], t);
        for (int i = 0; i < ncars && d.empty(); ++i)
            if (std::string di = ce::first_difference(ranked[size_t(i)], ce::read_car(ee, race_setup::car_addr(cond, i))); !di.empty())
                d = rh::fmt("car %d: ", i) + di;
        int done = 0;
        for (const ex::Car& c : cars) done += c.finished;
        ++g_cover[done == 0 ? "ranking direct: none finished" : done == ncars ? "ranking direct: all finished" : "ranking direct: some finished"];
        ++rs.calls, ++rs.active;
        if (!d.empty() && ++rs.bad == 1) rs.first = where + rh::fmt(" case %d: ", k) + d;
        // The free practice conductor's virtual 3 (0x1F24C0, by best lap) on the same state, unless the race's own was that.
        if (kind == ex::Conductor::FreePractice) continue;
        for (int i = 0; i < ncars; ++i) {
            ce::write_car(ee, race_setup::car_addr(cond, i), cars[size_t(i)]);
            ee.w32(cond + cond_off(0xE388) + 4 * u32(i), u32(shuffled[size_t(i)]));
        }
        ranked = cars, order = shuffled;
        ee.call(pa(0x001F24C0), {Arg::ptr(cond)}, 10'000'000);
        ex::race_ranking(ranked, order, ex::Conductor::FreePractice);
        d.clear();
        for (int i = 0; i < ncars && d.empty(); ++i)
            if (const s32 t = s32(ee.r32(cond + cond_off(0xE388) + 4 * u32(i))); t != order[size_t(i)]) d = rh::fmt("order [%d]: %d vs %d", i, order[size_t(i)], t);
        for (int i = 0; i < ncars && d.empty(); ++i)
            if (std::string di = ce::first_difference(ranked[size_t(i)], ce::read_car(ee, race_setup::car_addr(cond, i))); !di.empty())
                d = rh::fmt("car %d: ", i) + di;
        ++g_cover["ranking direct: free practice's (by best lap)"];
        ++rs.calls, ++rs.active;
        if (!d.empty() && ++rs.bad == 1) rs.first = where + rh::fmt(" case %d (0x1F24C0): ", k) + d;
    }
    for (int i = 0; i < ncars; ++i) ce::write_image(ee, race_setup::car_addr(cond, i), raw[size_t(i)].data());
    ee.write_bytes(cond + cond_off(0xB8F4), ByteSpan(mats.data(), mats.size()));
    ee.write_bytes(cond + cond_off(0xE388), ByteSpan(order_raw.data(), order_raw.size()));
}

void run_variants(Ee& ee, Rng& rng, const std::vector<const Port*>& ports, std::vector<Stats>& stats, u32 cond, int ncars, u32 copy,
                  const std::string& where, const std::vector<PlanCall>* tape) {
    for (int i = 0; i < ncars; ++i) {
        const u32 car = race_setup::car_addr(cond, i);
        const std::vector<u8> raw = ce::snapshot(ee, car);
        for (size_t p = 0; p < ports.size(); ++p) {
            const Port& port = *ports[p];
            if (port.variants && port.car.reg != 4) throw std::runtime_error(std::string(port.name) + ": variants need a0 = car + off");
            for (int k = 0; k < port.variants; ++k) {
                ee.write_bytes(copy, ByteSpan(raw.data(), raw.size()));
                ee.w32(copy + 0xEC8 + 4, copy); // the AI driver object's car
                ex::Car m = ce::read_car(ee, copy);
                if (port.only) { // only on the port's path
                    Call probe;
                    probe.ee = &ee, probe.car = copy;
                    if (!port.only(m, probe)) continue;
                }
                Vary v{ee, copy, rng, {}, {}};
                port.vary(m, v, k);
                ce::write_car(ee, copy, m);
                const std::vector<u8> pre = ce::snapshot(ee, copy);
                std::vector<u8> old;
                for (auto& [a, b] : v.poke) old.push_back(ee.r8(a)), ee.w8(a, b);
                Call call;
                call.ee = &ee, call.car = copy, call.a[0] = copy + car_off(u32(port.car.off)), call.vary = true;
                std::vector<Arg> args = {Arg::ptr(copy + car_off(u32(port.car.off)))};
                for (size_t q = 0, ia = 1, fa = 0; q < v.args.size(); ++q) {
                    args.push_back(v.args[q]);
                    if (v.args[q].kind == Arg::Float) call.f[fa++ & 7] = v.args[q].f;
                    else call.a[ia++ & 3] = v.args[q].i;
                }
                call.tape = tape, call.tape0 = tape ? tape->size() : 0;
                call.pev0 = g_pit_events.size();
                // the race's event queue (race info + 0x84, its count at +0x818): a variant's posts (the pit lane's display events)
                // are undone, so the race's own still fit
                const u32 queue = ee.r32(cond) + 0x84 + queue_count_off(), posts = ee.r32(queue);
                ee.call(port.fn, args, 50'000'000);
                ee.w32(queue, posts);
                call.tape1 = tape ? tape->size() : 0;
                call.pev1 = g_pit_events.size();
                const ex::Car theirs = ce::read_car(ee, copy);
                ex::Car ours = m;
                port.run(ours, call);
                for (size_t q = v.poke.size(); q-- > 0;) ee.w8(v.poke[q].first, old[q]);
                Stats& s = stats[p];
                ++s.vcalls;
                std::string d = ce::first_difference(ours, theirs);
                if (d.empty()) d = call.out;
                if (!d.empty() && ++s.vbad == 1) s.vfirst = rh::fmt("%s car %d variant %d: ", where.c_str(), i, k) + d;
                const std::vector<u8> post = ce::snapshot(ee, copy);
                if (post != pre) ++s.vactive;
                for (u32 off : ce::unmirrored_changes(pre, post)) s.unmirrored.insert(off);
            }
        }
    }
}

// Phase 12's settings and states the CPU races would not reach otherwise: the force feedback's static settings per race
// (0x2D4628 + 0x35 curve kind, + 0x37 the lateral term by F +0x420, + 0x39 filter mode; rebuilt by 0x1DFF40), and engine
// condition words and the distance total close to their limits (they reach them during the race).
// The force feedback bytes 0x2D465D / 0x2D465F / 0x2D4661 of race `race_index`.
const u8* race_ffb(int race_index) {
    static const u8 kFfb[4][3] = {{0, 1, 1}, {1, 1, 0}, {2, 0, 2}, {0, 1, 3}};
    return kFfb[race_index % 4];
}
void phase12_race_settings(Ee& ee, u32 cond, int ncars, int race_index) {
    const u8* f = race_ffb(race_index);
    ti::Built& b = ti::built();
    b.poke(ee, 0x002D465D, f[0]), b.poke(ee, 0x002D465F, f[1]), b.poke(ee, 0x002D4661, f[2]);
    ee.call(pa(0x001DFF40), {});
    auto car = [&](int i) { return race_setup::car_addr(cond, i); };
    ee.wf(car(0) + car_off(0x1388), 0.7995f);                 // dirt
    ee.wf(car(0) + 0xF84 + f_off(0x3F0), 7999800.0f);        // distance total
    if (ncars > 1) ee.w32(car(1) + car_off(0x137C), 0xEE6B2706); // odometer, 320 m from its limit
    if (ncars > 2) ee.wf(car(2) + car_off(0x1380), 599800.0f), ee.wf(car(2) + car_off(0x1384), -150.0f);
    if (ncars > 3) ee.wf(car(3) + car_off(0x1384), 299800.0f);
}

// State pokes in the CPU races (before the race step of `tick`, on car `car`) for paths a 40 s race does not reach by itself: the
// spawn / stop hold as a pit stop (300) or the free-practice pit start (120) sets it, with the heading some turns out (as after a
// few laps), optionally facing across the +-pi seam from the pit box's heading; a body tipped and spinning past the attitude
// clamps; a car dropped from 2 m; the steer-coupled camber and a zero swing arm (both constant in the shipped cars).
struct Poke {
    const char* event;
    int tick, car;
    void (*apply)(Ee& e, u32 car);
};
void poke_hold(Ee& e, u32 car, u16 n, int turns, bool across) {
    const u32 F = car + race_setup::kF;
    e.w16(F + f_off(0x536), n);
    const double tau = 2 * rh::kPi;
    double h = std::remainder(double(e.rf(F + f_off(0x470))), tau);
    if (across) {
        const u32 ai = e.r32(e.r32(car + 4) + cond_off(0xBBA8) + 4), table = e.r32(ai + 0x18);
        const double box = std::remainder(double(e.rf(table + u32(s32(s8(e.r8(F + f_off(0x4DE)))) * 0x28) + 0x28)), tau);
        h = box >= 0 ? 0.05 - rh::kPi : rh::kPi - 0.05;
    }
    e.wf(F + f_off(0x470), float(h + turns * tau));
}
void poke_tip(Ee& e, u32 car, float s) {
    const u32 F = car + race_setup::kF;
    e.wf(F + f_off(0x468), 0.78f * s), e.wf(F + f_off(0x480), 3.0f * s), e.wf(F + f_off(0x46C), -0.78f * s), e.wf(F + f_off(0x484), -3.0f * s);
}
// A pass-by state byte (the race's sounds', 0x1F59B0's) of the car at `car` towards car j, and its lap count against it in
// the gap matrix: what special car 1's mood reads of its rivals (a later build's race; the step's end sets both again).
void poke_rival(Ee& e, u32 car, u32 j, u8 state, s8 laps = 0) {
    const u32 cond = e.r32(car + 4), i = e.r8(car + race_setup::kF + f_off(0x3E8));
    e.w8(cond + cond_off(0xBAB8) + 8 * i + j, state);
    e.w8(cond + cond_off(0xB8F4) + 8 * i + j, u8(laps));
}
const Poke kPokes[] = {
    // A later build's special car 1 among its like (Concept 2001 Tokyo's demo_tmv_g): rivals alongside, ahead, behind, laps apart.
    {"demo_tmv_g", 300, 1, [](Ee& e, u32 car) { poke_rival(e, car, 2, 3); }},
    {"demo_tmv_g", 700, 3, [](Ee& e, u32 car) { poke_rival(e, car, 0, 2); }},
    {"demo_tmv_g", 1100, 4, [](Ee& e, u32 car) { poke_rival(e, car, 5, 3, 1); }},
    {"demo_tmv_g", 1500, 5, [](Ee& e, u32 car) { poke_rival(e, car, 0, 2, -1); }},
    {"demo_tmv_g", 1900, 2, [](Ee& e, u32 car) { poke_rival(e, car, 1, 3), poke_rival(e, car, 4, 2); }},
    {"Gsunday_e_0003", 0, 0, [](Ee& e, u32 car) { e.wf(car + 0x10 + 0xE1C, 0.05f); }},
    {"Gsunday_e_0003", 0, 5, [](Ee& e, u32 car) { e.wf(car + 0x10 + 0xE14, 0.0f), e.wf(car + 0x10 + 0xE18, 0.0f); }},
    {"Gsunday_e_0003", 600, 1, [](Ee& e, u32 car) { poke_hold(e, car, 300, 1, true); }},
    {"Gsunday_e_0003", 600, 2, [](Ee& e, u32 car) { poke_hold(e, car, 120, -1, false); }},
    {"Gsunday_e_0003", 900, 3, [](Ee& e, u32 car) { poke_hold(e, car, 200, 2, true); }},
    {"Gsunday_e_0003", 900, 4, [](Ee& e, u32 car) { poke_hold(e, car, 280, -2, false); }},
    {"Ggtworld_n_1010", 600, 1, [](Ee& e, u32 car) { poke_hold(e, car, 200, 0, true); }},
    {"Ggtworld_n_1010", 600, 2, [](Ee& e, u32 car) { poke_hold(e, car, 120, -3, true); }},
    {"Gevolution_e_0001", 600, 1, [](Ee& e, u32 car) { poke_tip(e, car, 1); }},
    {"Gevolution_e_0001", 600, 2, [](Ee& e, u32 car) { poke_tip(e, car, -1); }},
    {"Gevolution_e_0001", 900, 3, [](Ee& e, u32 car) { e.wf(car + race_setup::kF + 8, e.rf(car + race_setup::kF + 8) + 2.0f); }},
    // The AI: the stop driver's kinds 6, 7 and 5 (it parks the cars, kind 8), a car turned round (recovery state 2), then hit at
    // the front (state 1: braking, reversing out), a car turned 57 degrees and hit at the rear (0 -> 2), the arc tick cap.
    {"Gsunday_e_0002", 600, 1, [](Ee& e, u32 car) { e.w8(car + race_setup::kF + f_off(0x534), 6); }},
    {"Gsunday_e_0002", 600, 2, [](Ee& e, u32 car) { e.w8(car + race_setup::kF + f_off(0x534), 7); }},
    {"Gsunday_e_0002", 600, 3, [](Ee& e, u32 car) { e.w8(car + race_setup::kF + f_off(0x534), 5); }},
    {"Gsunday_e_0002", 900, 4, [](Ee& e, u32 car) { e.wf(car + race_setup::kF + f_off(0x470), e.rf(car + race_setup::kF + f_off(0x470)) + float(rh::kPi)); }},
    {"Gsunday_e_0002", 960, 4, [](Ee& e, u32 car) { e.w32(body_flags(e, car) + 0x434, 1); }},
    {"Gsunday_e_0002", 1200, 5,
     [](Ee& e, u32 car) {
         e.wf(car + race_setup::kF + f_off(0x470), e.rf(car + race_setup::kF + f_off(0x470)) + 1.0f);
         e.w32(body_flags(e, car) + 0x438, 1);
     }},
    {"Gsunday_e_0002", 1500, 0, [](Ee& e, u32 car) { e.w16(car + 0xEC8 + 0x88, 6000); }},
    {"Gsunday_e_0002", 1500, 5, [](Ee& e, u32 car) { e.w16(car + 0xEC8 + 0x88, 6000); }},
    // the end of a run (0x1EACB8) by mode: 0 the AI drives on (line 6, kind 1), 1 the stop driver along line 6 (kind 6), 2 the
    // stop driver with the wheels straight (kind 7), 3 the run-on after a finish (corner speeds x 0.8, throttle 0.5, then 0)
    {"Gspider_e_0001", 900, 1, [](Ee& e, u32 car) { e.call(pa(0x001EACB8), {Arg::ptr(car), Arg::integer(0)}); }},
    {"Gspider_e_0001", 900, 2, [](Ee& e, u32 car) { e.call(pa(0x001EACB8), {Arg::ptr(car), Arg::integer(1)}); }},
    {"Gspider_e_0001", 1200, 3, [](Ee& e, u32 car) { e.call(pa(0x001EACB8), {Arg::ptr(car), Arg::integer(2)}); }},
    {"Gspider_e_0001", 1200, 4, [](Ee& e, u32 car) { e.call(pa(0x001EACB8), {Arg::ptr(car), Arg::integer(3)}); }},
    // a human's end of a run (the pad car, kind 0): back to line 6 (blended), then the AI drives on
    {"Dr_smokymt_0001", 1500, 0, [](Ee& e, u32 car) { e.call(pa(0x001EACB8), {Arg::ptr(car), Arg::integer(0)}); }},
};

// ---- scripted pad drives ----------------------------------------------------------------------------------------------------
// One pad-driven car alone on the Test Course (event Gturbo_e_0002's grid), with a pedal style (the gearbox / clutch mode) and a
// driving script: a standing start and full-throttle shifts, braking to a stop, reverse and out of it, steering, the handbrake.
// They reach the clutch, gearbox and driveline paths the races above miss: every layout and differential of the disc's cars,
// every pedal style. "Synthetic" ones poke the record or the controller settings into states no disc car has (layout 7, the
// 'r' / 't' diffs, a power cap, controller gearbox modes 0 and 2), so that those paths of the ports are checked too.
constexpr Pa kPadRead = 0x00210740, kControllerSettings = 0x002D4628;

struct PadDrive {
    const char* name;
    const char* car;
    u8 style;                                  // F+0x461
    void (*poke)(Ee& ee, u32 car) = nullptr;   // after the set-up (synthetic states)
    int gear_mode = -1;                        // controller settings +0x14 for the drive (-1 = the shipped byte)
};

const PadDrive kPadDrives[] = {
    {"FR, open / clutch-pack rear, automatic", "s2000_99", 0},
    {"MR, open rear, manual (style 1)", "mr2_glimited_98", 1},
    {"RR, kick-down automatic (style 2)", "r_gt_00", 2},
    {"FF clutch-pack front, clutch pedal (style 3)", "integra_r_98", 3},
    {"FF viscous front, sequential (style 4)", "civic_sir2_93", 4},
    {"4WD L2 (system 0), sequential with reverse (style 5)", "tt_00", 5},
    {"4WD L3 (system 1), h / n, style 6", "altoworks_sport_ltd_97", 6},
    {"4WD L3, n / h, automatic", "legacy_gtb_96", 0},
    {"4WD L3, m / y, automatic", "evo6_wrc_99_special", 0},
    {"4WD L4, n / v, manual", "gto_tt_95", 1},
    {"4WD L4, open / open, automatic", "storia_x4_98", 0},
    {"4WD L4, h / y, automatic", "lancer_evo5_gsr_98", 0},
    {"4WD L5, h / m, automatic", "mines_r34_00", 0},
    {"4WD L6, h / y, automatic", "lancer_evo7_gsr_01", 0},
    {"the spawn / stop hold (F+0x536, as after a pit stop) at the start", "integra_r_98", 0,
     [](Ee& ee, u32 car) { ee.w16(car + 0xF84 + f_off(0x536), 120); }},
    {"synthetic: layout 7 (4WD system 7)", "lancer_evo6_gsr_99", 0, // (the later builds' system 7 is layout 8)
     [](Ee& ee, u32 car) { ee.w8(car + 0x208, sim::concept_program(program()) ? 8 : 7); }},
    {"synthetic: 'r' front and 't' rear diffs", "r34_gtr_v_99", 0,
     [](Ee& ee, u32 car) {
         ee.w8(car + 0x218, 1);
         ee.w8(car + 0x219, 7);
         ee.wf(car + 0x220, 2.0f); // the rear 't' initial (spec byte x 0.01)
     }},
    {"synthetic: power cap 150 kW", "r34_gtr_v_99", 0,
     [](Ee& ee, u32 car) {
         ee.w8(car + 0x148 + 0x9B, 1);
         ee.wf(car + 0x148 + 0x9C, 150000.0f);
         ee.wf(car + 0x148 + 0xA0, 0.1f);
     }},
    {"synthetic: turbo stage shapes 1 and 2, a low knee", "r34_gtr_v_99", 0,
     [](Ee& ee, u32 car) {
         const u32 E = car + 0x148;
         const float peak = ee.rf(E + 0xA4), rate = ee.rf(E + 0xAC);
         ee.w16(E + 0x94, 1500), ee.w8(E + 0x98, 1), ee.wf(E + 0xB4, peak / (1500.0f * 1500.0f));
         ee.w16(E + 0x96, 4000), ee.w8(E + 0x99, 2), ee.wf(E + 0xA8, 0.4f), ee.wf(E + 0xB0, rate);
         ee.wf(E + 0xB8, 0.4f / (4000.0f * 4000.0f));
     }},
    {"synthetic: controller gearbox mode 0", "s2000_99", 0, nullptr, 0},
    {"synthetic: controller gearbox mode 2", "s2000_99", 0, nullptr, 2},
};

struct DrivePad {
    double throttle = 0, brake = 0, back = 0, third = 0, steer = 0;
    int shift = 0;
};

// The script: phases by time, braking until (nearly) stopped. `v` = forward speed (m/s), `gear` the gear byte.
struct Script {
    int phase = 0, start = 0;
    DrivePad step(int t, double v, int gear, u8 style) {
        DrivePad p;
        const int k = t - start;
        auto next = [&] { ++phase, start = t; };
        // A one-tick shift input in the middle of every `every` ticks (the automatic styles see it as a held shift input); with
        // style 3 the brake axis is the clutch pedal, pressed home around each shift.
        auto pulse = [&](int dir, int every) {
            const int m = k % every - every / 2;
            if (m == 0) p.shift = dir;
            if (style == 3 && m >= -4 && m <= 4) p.brake = 1;
        };
        switch (phase) {
        case 0: // standing start, full throttle (braking hard while the clutch slips), shifting up
            p.throttle = 1;
            if (style != 3 && k >= 30 && k < 45) p.brake = 1;
            pulse(+1, 60);
            if (k >= 480) next();
            break;
        case 1: // brake to a stop, shifting down
            p.brake = 1;
            pulse(-1, 40);
            if (v < 0.3 || k >= 420) next();
            break;
        case 2: // reverse (the back axis; style 5 shifts down into it)
            if (style == 5) {
                if (gear != 0) pulse(-1, 20);
                else p.throttle = 0.6;
            } else p.back = 0.7;
            if (k >= 240) next();
            break;
        case 3: // forwards again, steering
            p.throttle = 1;
            p.steer = 0.6 * std::sin(k * (2 * rh::kPi / 120));
            pulse(+1, 60);
            if (k >= 300) next();
            break;
        case 4: // the handbrake at speed
            p.throttle = 0.5;
            p.third = k < 45 ? 1.0 : 0.5;
            if (k >= 90) next();
            break;
        case 5: // coasting, medium braking, steering
            p.brake = 0.4;
            p.steer = 0.5 * std::sin(k * (2 * rh::kPi / 90));
            pulse(-1, 40);
            if (k >= 150) next();
            break;
        default: break;
        }
        return p;
    }
    bool done() const { return phase > 5; }
};

void drive_frame(const DrivePad& p, u8 f[16]) {
    std::memset(f, 0, 16);
    f[0] = 0x1F; // every axis analog
    f[1] = 2;
    f[2] = u8(s8(p.shift));
    auto axis = [&](int off, double x) {
        const u16 v = u16(std::lround(std::clamp(x, 0.0, 1.0) * 4096));
        std::memcpy(f + off, &v, 2);
    };
    const s16 st = s16(std::lround(std::clamp(p.steer, -1.0, 1.0) * 4096));
    std::memcpy(f + 4, &st, 2);
    axis(8, p.throttle), axis(0xA, p.brake), axis(0xC, p.back), axis(0xE, p.third);
}

// Pad drives on the race_hits placement (two cars side by side) with a scripted frame per car and tick (Scenario::pad): full
// lock and circles, a locked stop, reversing with steering and running off the road. Frame [physics §4.4]: +0 analog flags,
// +1 kind 2, +4 steer, +8 throttle, +0xA brake, +0xC the back axis (reverse).
void put_frame(u8* f, s16 st, u16 th, u16 br, u16 bk, u16 hand = 0) {
    f[0] = 0x1F, f[1] = 2;
    std::memcpy(f + 4, &st, 2), std::memcpy(f + 8, &th, 2), std::memcpy(f + 10, &br, 2), std::memcpy(f + 12, &bk, 2);
    std::memcpy(f + 14, &hand, 2);
}
// A pad driver that follows a line round the course (the AI's race line, or the road's centre line: the checkpoints'
// midpoints in lap order; in the physics frame): steering towards a point ahead by the speed (at most 3 % of full lock more a
// step), throttle up to the speed the line's bend over the next stretch allows, brake above it; stuck (below 1 m/s for 2 s),
// it backs away for 1.5 s with the steering the other way. GT3R_FOLLOW=<look, look per m/s, gain, top speed, bend and aim
// factors> overrides its constants.
struct Follower {
    std::vector<std::array<double, 2>> line;
    double pace = 1; // the target speed's scale
    // A later build's pod race ("#pod"): on the odd laps it stops in the stop zone (10 m in) and sits on the handbrake until
    // the stop is counted; the even laps' stops it misses.
    bool pod = false;
    int slow = 0, back = 0;
    double last = 0;
    void frame(Ee& e, u32 car, u8* f) {
        if (line.size() < 4) return put_frame(f, 0, 2000, 0, 0);
        static const std::array<double, 6> q = [] {
            std::array<double, 6> r{10, 0.7, 1.5, 34, 35, 20};
            if (const char* t = std::getenv("GT3R_FOLLOW")) std::sscanf(t, "%lf,%lf,%lf,%lf,%lf,%lf", &r[0], &r[1], &r[2], &r[3], &r[4], &r[5]);
            return r;
        }();
        const u32 F = car + ce::kF;
        const double x = e.rf(F), y = e.rf(F + 4), fx = e.rf(F + 0xC), fy = e.rf(F + 0x10), v = double(e.rf(F + f_off(0x488)));
        size_t j = 0;
        double best = 1e18;
        for (size_t k = 0; k < line.size(); ++k)
            if (const double d = (line[k][0] - x) * (line[k][0] - x) + (line[k][1] - y) * (line[k][1] - y); d < best) best = d, j = k;
        auto along = [&](size_t from, double dist) { // the line point `dist` metres on from point `from`
            size_t k = from;
            for (double run = 0; run < dist;) {
                const size_t n = (k + 1) % line.size();
                run += std::hypot(line[n][0] - line[k][0], line[n][1] - line[k][1]);
                k = n;
                if (k == from) break;
            }
            return k;
        };
        auto heading = [&](size_t k) {
            const size_t n = (k + 1) % line.size();
            return std::atan2(line[n][1] - line[k][1], line[n][0] - line[k][0]);
        };
        auto wrap = [](double a) { return std::remainder(a, 2 * 3.14159265358979); };
        const size_t k = along(j, q[0] + q[1] * std::max(0.0, v));
        const double dx = line[k][0] - x, dy = line[k][1] - y;
        const double a = std::atan2(fx * dy - fy * dx, fx * dx + fy * dy); // > 0: the point is to the left
        const double steer = last = std::clamp(std::clamp(a * q[2], -1.0, 1.0), last - 0.03, last + 0.03);
        if (back > 0) {
            --back;
            return put_frame(f, s16(std::lround(-steer * 4096)), 0, 0, 3500);
        }
        int stopping = 0; // the pod race: 1 heading for the stop, 2 on it
        double stop_target = 0;
        if (pod) {
            const u32 cond = e.r32(car + 4), z = cond + kZoneAt;
            const int laps = s16(e.r16(F + f_off(0x434))), owed = laps - e.r8(z + 2 + (e.r8(F + f_off(0x3E8)) & 7));
            const double s = e.rf(F + f_off(0x3E4)), base = e.rf(z + 0xC), len = e.rf(z + 0x10);
            if (e.r8(z) == 1 && owed > 0 && laps % 2 == 1 && s > base - 80 && s < base + len) {
                const double at = base + 10;
                stopping = s < at - 1 ? 1 : 2;
                stop_target = std::max(1.5, (at - s) * 0.5);
            }
        }
        slow = std::abs(v) < 1 && !stopping ? slow + 1 : 0;
        if (slow > 120) slow = 0, back = 90;
        double bend = 0; // the line's largest turn from here over the stopping stretch
        for (double d = 10; d <= 20 + 2.5 * std::max(0.0, v); d += 10) bend = std::max(bend, std::abs(wrap(heading(along(j, d)) - heading(j))));
        double target = std::clamp(q[3] * pace - q[4] * bend - q[5] * std::abs(a), 10.0, q[3] * pace);
        if (stopping == 1) target = std::min(target, stop_target);
        if (stopping == 2) return put_frame(f, s16(std::lround(steer * 4096)), 0, 4096, 0, std::abs(v) < 0.5 ? 4096 : 0);
        const u16 th = v < target ? u16(3200) : u16(0), br = v > target + 2 ? u16(3000) : u16(0);
        if (const char* tr = std::getenv("GT3R_TICK_TRACE"); tr && tr[0] == '2')
            std::fprintf(stderr, "follow v %.1f a %.3f bend %.3f target %.1f off %.1f j %zu/%zu\n", v, a, bend, target, std::sqrt(best), j, line.size());
        put_frame(f, s16(std::lround(steer * 4096)), th, br, 0);
    }
};
const std::vector<rh::Scenario>& placed_drives() {
    auto drive = [](const char* name, double kmh, int ticks, void (*pad)(int, int, u8*)) {
        rh::Scenario s{name, 3.0, 0, kmh, kmh, 0.0, 0, 0};
        s.pad = pad, s.ticks = ticks;
        return s;
    };
    static const std::vector<rh::Scenario> drives = {
        drive("pad drive: circles at full lock, then a locked stop", 40, 600,
              [](int car, int k, u8* f) {
                  if (k < 480) put_frame(f, s16(car ? -4096 : 4096), u16(car ? 1600 : 1200), 0, 0);
                  else put_frame(f, 0, 0, 4096, 0);
              }),
        drive("pad drive: stop, reverse with steering, then full throttle", 60, 480,
              [](int car, int k, u8* f) {
                  if (k < 150) put_frame(f, 0, 0, 4096, 0);
                  else if (k < 360) put_frame(f, s16(car ? -2000 : 2000), 0, 0, 4096);
                  else put_frame(f, 0, 4096, 0, 0);
              }),
        drive("pad drive: off the road", 110, 360,
              [](int car, int k, u8* f) {
                  put_frame(f, s16(k < 120 ? (car ? -900 : 900) : 0), u16(k > 300 ? 0 : 3000), u16(k > 300 ? 2000 : 0), 0);
              }),
    };
    return drives;
}

// The race step's vehicle phases as one (sim::exact::tick_until_collision / tick_after_collision) for every car of the race,
// against the original's whole step: all cars are snapshotted at phase 1's first call, our phases 1-9 run from there and
// must match every car at the collision step's call; the cars are snapshotted again after it, our phases 11-13 run from
// there and must match every car, the proximity matrices and the VU0 random register at the end of phase 13. What the step
// does that is not ported comes from the original's run: phase 3's AI paths (the changes its call made to the record) and
// the AI driver object's update on the pad path, the tyre effect emitter's draws of the random register (its value after
// each car's 0x1DF150), the conductor's answers in the wear flags (taps).
// The free run ghost's extra step (0x1F1400 runs the race step in its second entry mode, a1 = 1, inside the race flow of the
// outer step: its call at 0x1F14A4 returns to 0x1F14AC): the depth (the checks of the outer step ignore the nested one's
// probes) and what it changed (every car, the queue's posts, the pit events), which ours apply at their ghost_step hook.
int g_nested = 0;
struct GhostStep {
    std::vector<ex::Car> pre, post;
    std::vector<ex::RaceEvent> events;
    std::vector<ex::PitEvent> pit_events;
    u32 q0 = 0;    // the queue's count at its start
    size_t p0 = 0; // and the pit events'
};
std::vector<GhostStep> g_ghost_steps; // this race step's, in order
template <class Fn> void guarded(Ee& e, u32 a, Fn fn) {
    e.add_probe(a, [fn](Ee& ee) {
        if (g_nested == 0) fn(ee);
    });
}
// A ghost step's changes applied to ours (`events` / `pit_events` take its posts).
void apply_ghost_step(const GhostStep& g, std::span<ex::Car> cars, std::vector<ex::RaceEvent>* events, std::vector<ex::PitEvent>* pit_events) {
    for (size_t i = 0; i < cars.size() && i < g.pre.size(); ++i) ce::copy_changed(cars[i], g.pre[i], g.post[i]);
    if (events) events->insert(events->end(), g.events.begin(), g.events.end());
    if (pit_events) pit_events->insert(pit_events->end(), g.pit_events.begin(), g.pit_events.end());
    ++g_cover["free run: the ghost's extra step"];
}

// The "#sparks" races' cars: each car label's spark points as the app gives them (race::exact_spark_points: the base car's
// model, its GTCI's list; the day models, cars/day/) and that GTCI block for the original's model holder (`gtci`: empty when
// the disc has not got the model).
std::vector<std::vector<ex::SparkPoint>> spark_models(const Volume& vol, const data::CarDatabase& db, const std::vector<std::string>& labels,
                                                      std::vector<Bytes>& gtci) {
    std::map<std::string, Bytes> info; // per model: its GTCI block
    auto source = [&](const std::string& model) {
        std::vector<ex::SparkPoint> out;
        const Volume::Entry* f = vol.find("cars/day/" + model);
        if (!f) return out;
        const Bytes file = vol.read(*f);
        if (!CarFile::is_car(file)) return out;
        const CarFile cf = CarFile::parse(file);
        info[model] = Bytes(cf.info.begin(), cf.info.end());
        for (const std::array<u32, 3>& p : car_spark_points(cf.info)) out.push_back({ex::F::raw(p[0]), ex::F::raw(p[1]), ex::F::raw(p[2])});
        return out;
    };
    const std::vector<std::vector<ex::SparkPoint>> points = race::exact_spark_points(db, labels, source);
    gtci.assign(labels.size(), {});
    for (size_t i = 0; i < labels.size(); ++i)
        if (const data::Car* c = db.car(race::exact_base_car(db, labels[i])))
            if (const auto it = info.find(db.model_of(*c)); it != info.end()) gtci[i] = it->second;
    return points;
}

class StepCheck {
public:
    Stats until, after; // phases 1-9 / 11-13
    Stats flow;         // the loop after phase 13 (0x1F3DF0 .. 0x1F3E4C: the race flow, the pit lane, the end of a run)
    Stats carried;      // the steps run on our own previous results (carry_from)
    std::string* scenario = nullptr;
    const ex::CourseGround* const* ground = nullptr;
    const std::vector<PlanCall>* tape = nullptr; // the race step's AI plan calls

    // For the next `steps` steps our phases run on our own results, from `mine` (our records when the original's were
    // `base`): at each step's phase 1 and after the collision step, what the original's records changed since (the step's
    // parts outside the ported phases, the collision step, the harness's pokes) is copied into ours; the compares count in
    // `carried` too. Off again once a step differs.
    void carry_from(std::vector<ex::Car> mine, std::vector<ex::Car> base, int steps) {
        mine_ = std::move(mine), base_ = std::move(base), carry_ = steps;
    }

    // The later builds' leaves (sim/exact/leaves.h): at each race's set-up the original's particle manager (its own
    // constructor) put in the course object the cars' + 8 points to, as RacePS2Base::vf9 puts it, and our pool beside it
    // (none on GT3's programs, or when the program's leaf maker is not recognised). The step check then runs the emitter's
    // leaf calls (Rule::Leaves) on our requests and records: the register after each car's emitter against the original's,
    // and at the end of phase 13 the manager against our pool. leaves_frame(): both managers' update between two steps (the
    // CPU races; the original's race step alone never updates them).
    Stats leaves;          // steps with a leaf call or a leaf alive
    long leaf_calls = 0, leaves_made = 0, leaf_frames = 0;
    std::string leaf_err;
    void install_leaves(Ee& e, u32 car0) {
        leaves_on_ = sparks_on_ = false; // (a race's sparks: install_sparks after this)
        if (!sim::concept_program(program()) || !has_pa(later_fn(sim::Rule::Leaves))) return;
        const u32 course = e.r32(car0 + 8);
        if (!course) return;
        std::string err;
        lm_ = leaves_probe::make_manager(e, err);
        if (!err.empty()) {
            if (leaf_err.empty()) leaf_err = err;
            return;
        }
        e.w32(course + 0xC, lm_.pm);
        lp_ = ex::Sparks();
        lp_.set_leaves(disc_behaviour());
        leaves_on_ = lp_.leaves_on();
    }
    void leaves_frame(Ee& e) {
        if (sparks_on_) return spark_frame(e);
        if (!leaves_on_ || leaves_probe::alive(lp_) == 0) return;
        const ex::F dt = ex::particle_dt(disc_behaviour());
        const u32 vr = e.vr;
        e.call(leaves_probe::kUpdate, {Arg::ptr(lm_.pm), Arg::integer(0), Arg::fbits(dt.bits)}, 50'000'000);
        Vu0Random r;
        r.r = vr;
        lp_.update(dt, r);
        ++leaf_frames;
        if (e.vr != vr && leaf_err.empty()) leaf_err = *scenario + ": the leaves' update drew the register";
        if (std::string d = leaves_probe::difference(e, lm_, lp_, r); !d.empty()) {
            if (++leaves.bad == 1) leaves.first = *scenario + ": the update: " + d;
            leaves_on_ = false; // (one difference is enough: the race's further leaves are not checked)
        }
    }

    // The sparks ("#sparks" races, sim/exact/sparks.h): every car's model holder (its model's GTCI with the spark points, the
    // car record at its model_car()), the original's manager (a later build's: the leaves' one, the pool they share; GT3's made
    // here) with its tuning block, and our pool (the leaves' on a later build). Between two race steps spark_frame() runs
    // what RacePS2Base::vf30 and RacePS2Base::vf29 run after the step: each car's emission 0x1AADD0 in the race's entry
    // order (ours: Sparks::emit on the original's record as the step left it, its model's spark points as the app gives
    // them, our road query on the race's course file, the car's draw mode from the race's words), the register compared
    // after each car, then both updates and the whole pool. The register the frame leaves is the next step's, in the
    // original's run and so in the step check, which starts each step from the original's; the leaf checks above see the
    // sparks in the shared pool.
    struct SparkCar {
        u32 holder = 0; // 0: the car has no model on the disc (none drawn: no emission)
        std::vector<ex::SparkPoint> points;
    };
    Stats sparks;                                                      // spark frames (calls), with a burst (active)
    long spark_calls = 0, spark_bursts = 0, sparks_made = 0, spark_cars = 0, spark_entries = 0; // emissions, ones that drew, sparks made
    std::string spark_err;
    const ex::CourseQuery* const* query = nullptr;
    void install_sparks(Ee& e, u32 cond, const std::vector<Bytes>& gtci, const std::vector<std::vector<ex::SparkPoint>>& points) {
        sparks_on_ = false;
        spark_cond_ = cond;
        spark_cars_.assign(gtci.size(), {});
        if (!query || !*query) {
            if (spark_err.empty()) spark_err = *scenario + ": no road query for the sparks";
            return;
        }
        sparks_probe::install_micro(e, spark_micro_err_);
        if (leaves_on_) {
            sm_ = sparks_probe::adopt_manager(e, lm_);
        } else {
            std::string err;
            sm_ = sparks_probe::make_manager(e, err);
            if (!err.empty()) {
                if (spark_err.empty()) spark_err = *scenario + ": the sparks' manager: " + err;
                return;
            }
            lp_ = ex::Sparks();
        }
        lp_.set_rules(disc_behaviour());
        for (size_t i = 0; i < gtci.size(); ++i) {
            if (gtci[i].empty()) continue;
            spark_cars_[i].holder = sparks_probe::make_holder(e, ByteSpan(gtci[i].data(), gtci[i].size()), race_setup::car_addr(cond, int(i)));
            spark_cars_[i].points = i < points.size() ? points[i] : std::vector<ex::SparkPoint>{};
            spark_cars += !spark_cars_[i].points.empty();
        }
        spark_entries += long(gtci.size());
        sparks_on_ = true;
    }
    void spark_frame(Ee& e) {
        const ex::F dt = ex::particle_dt(disc_behaviour());
        Vu0Random r;
        r.r = e.vr;
        const int alive0 = leaves_probe::alive(lp_);
        std::string d;
        const int n = std::min(int(spark_cars_.size()), int(e.r32(spark_cond_ + race_setup::kK8 + k_off(0x6384))));
        for (int i = 0; i < n; ++i) {
            const SparkCar& sc = spark_cars_[size_t(i)];
            if (!sc.holder) continue;
            const u32 car = race_setup::car_addr(spark_cond_, i);
            const ex::Car c = ce::read_car(e, car);
            u32 x70 = 0, ghost_switch = 0;
            draw_mode_words(e, car, x70, ghost_switch);
            const u32 r0 = e.vr;
            e.call(sparks_probe::kEmit, {Arg::ptr(sc.holder), Arg::ptr(sm_.pm), Arg::fbits(0)}, 50'000'000);
            lp_.emit(c, sc.points, **query, ex::draw_mode(c, x70, ghost_switch) >= 2, r);
            ++spark_calls, spark_bursts += e.vr != r0;
            if (d.empty() && e.vr != r.r) d = rh::fmt("car %d: the emission: VU0 R 0x%08X vs ours 0x%08X (from 0x%08X)", i, e.vr, r.r, r0);
        }
        const int made = leaves_probe::alive(lp_) - alive0;
        sparks_made += std::max(0, made);
        e.call(sparks_probe::kUpdate, {Arg::ptr(sm_.pm), Arg::integer(0), Arg::fbits(dt.bits)}, 50'000'000);
        lp_.update(dt, r);
        ++sparks.calls, sparks.active += made > 0;
        if (!spark_micro_err_.empty() && d.empty()) d = spark_micro_err_;
        if (d.empty()) d = sparks_probe::difference(e, sm_, lp_, r);
        if (!d.empty()) {
            if (++sparks.bad == 1) sparks.first = *scenario + ": the sparks' frame: " + d;
            sparks_on_ = leaves_on_ = false; // (one difference is enough: the race's further particles are not checked)
        }
    }

    void install(Ee& e) {
        on_ = false;
        guarded(e, race_setup::kCondStep, [this](Ee& ee) {
            armed_ = u32(ee.a(1)) == 0; // the second entry mode (car 0 and phase 11 skipped) is not checked
            cond_ = u32(ee.a(0));
            on_ = false;
        });
        guarded(e, pa(0x001EADB8), [this](Ee& ee) {
            if (!armed_ || u32(ee.gpr[31].lo) != pa(0x001F3B04)) return;
            armed_ = false, on_ = true;
            n_ = int(ee.r32(cond_ + race_setup::kK8 + k_off(0x6384)));
            pre_.assign(size_t(n_), {}), mid_ = pre_, tape0_.assign(size_t(n_), 0), tape1_.assign(size_t(n_), 0);
            pad_.assign(size_t(n_), {}), pit_.assign(size_t(n_), {}), vr_emit_.assign(size_t(n_), 0);
            for (int i = 0; i < n_; ++i) pre_[size_t(i)] = ce::read_car(ee, car(i));
            zone_ = read_zone(ee, cond_);
            carrying_ = carry_ > 0 && mine_.size() == size_t(n_);
            if (carrying_)
                for (int i = 0; i < n_; ++i) {
                    ex::Car c = mine_[size_t(i)];
                    ce::copy_changed(c, base_[size_t(i)], pre_[size_t(i)]);
                    pre_[size_t(i)] = c;
                }
        });
        guarded(e, pa(0x001E4D38), [this](Ee& ee) {
            if (!on_ || u32(ee.gpr[31].lo) != pa(0x001F3B6C)) return;
            cur_ = index(u32(ee.a(0)));
            tape0_[size_t(cur_)] = tape->size();
            sp_ = u32(ee.gpr[29].lo);
        });
        guarded(e, pa(0x001F3B6C), [this](Ee& ee) {
            if (!on_) return;
            tape1_[size_t(cur_)] = tape->size();
            pad_[size_t(cur_)] = pad_frame(ee, sp_ - pad_frame_below());
        });
        guarded(e, pa(0x001F3CC8), [this](Ee& ee) { // after phase 9 for every car, before the collision step
            if (on_) check_until(ee), prox_.clear();
        });
        guarded(e, pa(0x001F3D00), [this](Ee& ee) { // both collision calls return here
            if (!on_) return;
            for (int i = 0; i < n_; ++i) mid_[size_t(i)] = ce::read_car(ee, car(i));
            if (carrying_)
                for (int i = 0; i < n_; ++i) {
                    ex::Car c = mine_[size_t(i)];
                    ce::copy_changed(c, base_[size_t(i)], mid_[size_t(i)]);
                    mid_[size_t(i)] = c;
                }
            prox_.resize(0x140);
            ee.read_bytes(cond_ + kProxSlip, prox_.data(), 0x140);
            vr_mid_ = ee.vr;
        });
        guarded(e, pa(0x001EB218), [this](Ee& ee) {
            if (on_ && u32(ee.gpr[31].lo) == pa(0x001F3D3C)) cur_ = index(u32(ee.a(0)));
        });
        guarded(e, pa(0x001EB2E0), [this](Ee& ee) { // 0x1EB218's return from 0x1DF150 (the emitter has run)
            if (on_) vr_emit_[size_t(cur_)] = ee.vr;
        });
        guarded(e, pa(0x001E8340), [this](Ee& ee) {
            if (!on_ || u32(ee.gpr[31].lo) != pa(0x001F3DAC)) return;
            cur_ = index(u32(ee.a(0)));
            ex::PitMode& m = pit_[size_t(cur_)];
            m = {};
            m.kind = ee.r8(ee.r32(car(cur_) + 4) + 0x8000 + k_off(0x37AC));
        });
        guarded(e, pa(0x001E8388), [this](Ee& ee) { if (on_) pit_[size_t(cur_)].pits = u32(ee.gpr[2].lo) != 0; });
        guarded(e, pa(0x001DA014), [this](Ee& ee) { if (on_) pit_[size_t(cur_)].mode = s32(u32(ee.gpr[2].lo)); });
        guarded(e, pa(0x001F3DF0), [this](Ee& ee) {
            if (!on_ || prox_.empty()) return;
            on_ = false;
            check_after(ee);
        });
        guarded(e, pa(0x001F3E4C), [this](Ee& ee) { // after the per-car loop
            if (flow_on_) flow_on_ = false, check_flow(ee);
        });
    }

private:
    bool leaves_on_ = false, sparks_on_ = false;
    leaves_probe::Manager lm_;
    sparks_probe::Manager sm_;
    ex::Sparks lp_;
    u32 spark_cond_ = 0;
    std::vector<SparkCar> spark_cars_;
    std::string spark_micro_err_;
    bool armed_ = false, on_ = false, carrying_ = false;
    u32 cond_ = 0, sp_ = 0, vr_mid_ = 0;
    int n_ = 0, cur_ = 0, carry_ = 0;
    std::vector<ex::Car> pre_, mid_;
    ex::StopZone zone_; // a later build's stop zone at the step's start (the AI's braking stores its points)
    std::vector<size_t> tape0_, tape1_; // per car: its phase 3's plan calls on the tape
    std::vector<ex::Car> mine_, base_; // carry_from: our records and the original's when ours were made
    std::vector<ex::PadFrame> pad_;
    std::vector<ex::PitMode> pit_;
    std::vector<u32> vr_emit_;
    std::vector<u8> prox_;

    u32 car(int i) const { return race_setup::car_addr(cond_, i); }
    int index(u32 car) const { return std::clamp(int((car - race_setup::car_addr(cond_, 0)) / car_stride()), 0, n_ - 1); }

    // The built race-level inputs with the course, the start countdown (race info + 0x5C) and the course-out stretch; their
    // differences from the memory go to `bad`.
    ex::RaceInputs race_inputs(Ee& ee, std::string& bad) const {
        ex::RaceInputs r = ti::built().race;
        const u32 c0 = car(0);
        r.prep.start_count = s32(ee.r32(ee.r32(cond_) + 0x5C));
        r.course = *ground;
        r.course_out = course_out_rule(ee, c0, bad);
        if (bad.empty()) bad = ti::race_inputs_differ(ee, c0);
        // A later build's special car 1 reads the pass-by states the race's sounds keep (0x1F59B0, at the step's end: not
        // ported) and the gap matrices: the original's, as the step finds them.
        if (sim::concept_program(program())) {
            for (u32 i = 0; i < 8; ++i) ee.read_bytes(cond_ + cond_off(0xBAB8) + 8 * i, pass_by_[i].data(), 8);
            gaps_ = ti::mem_gaps(ee, cond_);
            r.later.pass_by = &pass_by_, r.later.gaps = &gaps_;
            r.later.lap_length = ee::F::raw(ee.r32(ee.r32(ee.r32(ee.r32(cond_ + 0x8000 + k_off(0x37A8))) + 0x70) + 0x18));
        }
        return r;
    }
    mutable ex::PassBy pass_by_{};
    mutable ex::GapMatrix gaps_{};

    void compare(Ee& ee, Stats& s, const std::vector<ex::Car>& ours, std::string extra) {
        ++s.calls, ++s.active;
        std::string d;
        for (int i = 0; i < n_ && d.empty(); ++i)
            if (std::string di = ce::first_difference(ours[size_t(i)], ce::read_car(ee, car(i))); !di.empty()) d = rh::fmt("car %d: ", i) + di;
        if (d.empty()) d = std::move(extra);
        if (!d.empty() && ++s.bad == 1) s.first = *scenario + ": " + d;
        if (carrying_) {
            ++carried.calls, ++carried.active;
            if (!d.empty() && ++carried.bad == 1) carried.first = *scenario + ": " + d;
            if (!d.empty()) carry_ = 0, carrying_ = false; // one difference is enough: back to the original's records
            else {
                mine_ = ours;
                base_.resize(size_t(n_));
                for (int i = 0; i < n_; ++i) base_[size_t(i)] = ce::read_car(ee, car(i));
            }
        }
    }

    void check_until(Ee& ee) {
        std::vector<ex::Car> ours = pre_;
        std::vector<ex::CarInputs> in(pre_.size());
        for (int i = 0; i < n_; ++i) in[size_t(i)].pad = pad_[size_t(i)], in[size_t(i)].ai = ai_inputs(ee, car(i));
        std::string bad;
        const ex::RaceInputs race = race_inputs(ee, bad);
        if (rh::env_num("GT3R_TICK_AI_TAPE", 0) != 0) { // phase 3 on the original's plan results (TapePlan), for diagnosis
            std::vector<PlanCall> calls; // phase 3's plan calls, car by car
            for (size_t k = 0; k < size_t(n_); ++k)
                if (tape0_[k] <= tape1_[k] && tape1_[k] <= tape->size())
                    calls.insert(calls.end(), tape->begin() + ptrdiff_t(tape0_[k]), tape->begin() + ptrdiff_t(tape1_[k]));
            TapePlan plan(calls, ee, car(0), &zone_);
            ex::tick_until_collision(ours, in, race, plan);
            if (bad.empty()) bad = plan.err;
            if (bad.empty() && !plan.done()) bad = rh::fmt("plan tape: %zu of %zu calls used", plan.used(), calls.size());
        } else { // our course setup's AI course object and the ported plan
            const ex::WearParams cp = ai_wear(ee, car(0));
            ex::StopZone zone = zone_;
            ex::CoursePlan plan(ai_course(ee, car(0)), cp, &zone, g_query);
            ex::tick_until_collision(ours, in, race, plan);
            if (bad.empty()) bad = zone_differ(zone, read_zone(ee, cond_));
        }
        compare(ee, until, ours, bad);
    }

    void check_after(Ee& ee) {
        std::vector<ex::Car> ours = mid_;
        std::vector<ex::CarInputs> in(pre_.size());
        std::string bad;
        const ti::Built& b = ti::built();
        for (int i = 0; i < n_; ++i) {
            ex::CarInputs& c = in[size_t(i)];
            c.mark_flagged = ex::has_pits(b.kind), c.box_heading = box_heading(ee, car(i), mid_[size_t(i)], bad);
            if (c.mark_flagged != conductor_marks_flagged(ee, car(i)) && bad.empty()) bad = "conductor virtual 39";
            u32 x70 = 0, ghost = 0;
            draw_mode_words(ee, car(i), x70, ghost);
            if ((x70 != run_x70() || ghost != 0) && bad.empty()) bad = "draw mode words";
            c.x70 = run_x70(), c.ghost_switch = 0;
            c.pit = {ex::has_pits(b.kind), ex::pit_mode(b.kind, b.course_category, b.data.behaviour().rule(sim::Rule::LicenceJudge)), b.flow};
            const ex::PitMode& t = pit_[size_t(i)]; // the original's (taps; mode 0 when 0x1D9FE0 was not reached)
            if (bad.empty() && (t.pits != c.pit.pits || t.kind != c.pit.kind || (t.mode != 0 && t.mode != c.pit.mode)))
                bad = rh::fmt("car %d: pit mode %d %d %d vs %d %d %d", i, c.pit.pits, c.pit.mode, c.pit.kind, t.pits, t.mode, t.kind);
        }
        ex::Proximity k = read_proximity(prox_.data());
        Vu0Random rng;
        rng.r = vr_mid_;
        // The emitter: the original's register after it (its smoke is not run here: the course object has none), and on a
        // later build with the leaves set up, our leaf calls on our requests before that.
        std::string leaf_d;
        long calls = 0;
        const int alive0 = leaves_on_ ? leaves_probe::alive(lp_) : 0;
        ex::tick_after_collision(ours, in, race_inputs(ee, bad), &k, rng, [&](int i, const ex::ChassisOutputs& o, Vu0Random& r) {
            if (leaves_on_) {
                const u32 r0 = r.r;
                for (const ex::TyreEffect& q : o.effects)
                    if (q.ground_flags & 0x20) lp_.leaf(q, ex::course_velocity(ours[size_t(i)]), r), ++calls;
                if (r.r != vr_emit_[size_t(i)] && leaf_d.empty())
                    leaf_d = rh::fmt("car %d: the emitter's leaves: VU0 R 0x%08X vs 0x%08X (from 0x%08X)", i, r.r, vr_emit_[size_t(i)], r0);
            }
            r.r = vr_emit_[size_t(i)];
        });
        if (leaves_on_ && (calls > 0 || alive0 > 0)) {
            leaf_calls += calls, leaves_made += std::max(0, leaves_probe::alive(lp_) - alive0);
            Vu0Random lr;
            lr.r = ee.vr; // (the manager compared alone: the register is the step's check)
            if (leaf_d.empty()) leaf_d = leaves_probe::difference(ee, lm_, lp_, lr);
            ++leaves.calls, ++leaves.active;
            if (!leaf_d.empty()) {
                if (++leaves.bad == 1) leaves.first = *scenario + ": " + leaf_d;
                leaves_on_ = false;
            }
        }
        std::vector<u8> post(0x140);
        ee.read_bytes(cond_ + kProxSlip, post.data(), 0x140);
        const ex::Proximity theirs = read_proximity(post.data());
        std::string extra;
        for (size_t i = 0; i < 8 && extra.empty(); ++i)
            for (size_t j = 0; j < 8 && extra.empty(); ++j)
                if (k.slip[i][j].bits != theirs.slip[i][j].bits || k.code[i][j] != theirs.code[i][j]) extra = rh::fmt("proximity [%zu][%zu]", i, j);
        if (extra.empty() && rng.r != ee.vr) extra = rh::fmt("VU0 R 0x%08X vs 0x%08X", rng.r, ee.vr);
        if (extra.empty()) extra = bad;
        compare(ee, after, ours, extra);
        if (carrying_) --carry_;
        flow_start(ee, ours);
    }

    // The loop after phase 13 from our records after it: its inputs as the step has them at 0x1F3DF0 (the race flow's, the
    // clock and end countdown, the race order, the collision bodies' flags, the pit lane's), the event queue's and the pit
    // events' counts, the plan tape's position. Only for the conductors whose race flow is ported, in the step's first entry mode.
    bool flow_on_ = false;
    std::vector<ex::Car> flow_cars_;
    ex::Snapshot flow_ghost_, flow_lap_;
    ex::RaceFlowInputs flow_in_;
    ex::RaceFlow flow_state_;
    std::vector<s32> order_;
    std::vector<u8> hit_, hit43_;
    bool rolling_ = false;
    size_t first_ = 0, queue0_ = 0, pev0_ = 0, flow_tape0_ = 0, rec0_ = 0;
    std::unique_ptr<PitIn> pit_in_;

    void flow_start(Ee& ee, const std::vector<ex::Car>& ours) {
        const u32 sp = u32(ee.gpr[29].lo);
        if (!ex::race_flow_ported(ti::built().kind) || ee.r32(sp) != 0) {
            ++flow.skipped;
            return;
        }
        flow_cars_ = ours;
        flow_in_ = flow_inputs(ee, cond_, &flow_ghost_, &flow_lap_);
        flow_state_ = read_flow(ee, cond_);
        order_.assign(size_t(n_), 0), hit_.assign(size_t(n_), 0), hit43_.assign(size_t(n_), 0);
        for (int i = 0; i < n_; ++i) {
            order_[size_t(i)] = s32(ee.r32(cond_ + cond_off(0xE388) + 4 * u32(i)));
            const u32 body = cond_ + cond_off(0xBBB4) + 0xC + 0x4F0 * u32(i);
            hit_[size_t(i)] = ee.r32(body + 0x43C) != 0 || ee.r32(body + 0x440) != 0;
            hit43_[size_t(i)] = ee.r32(body + 0x43C) != 0;
        }
        rolling_ = true; // the step's first entry mode: virtual 13 runs
        first_ = program() == Program::NtscJ ? ee.g32(23) : ee.r32(sp + 4); // the loop's first position (NTSC-J's step: $s7)
        queue0_ = ee.r32(ee.r32(cond_) + 0x84 + queue_count_off());
        pev0_ = g_pit_events.size(), flow_tape0_ = tape->size(), rec0_ = g_rec_calls.size();
        pit_in_ = std::make_unique<PitIn>(ee, car(0));
        flow_on_ = true;
    }

    void check_flow(Ee& ee) {
        std::vector<ex::Car> ours = flow_cars_;
        ex::FlowStep f;
        f.in = flow_in_;
        f.flow = &flow_state_;
        f.order = order_, f.hit = hit_, f.hit_43c = hit43_;
        f.first = first_, f.mode_start = rolling_;
        f.pit = pit_in_->pit;
        std::vector<ex::RaceEvent> events;
        std::vector<ex::PitEvent> pit_events;
        f.events = &events, f.pit_events = &pit_events;
        size_t ghost_next = 0;
        f.ghost_step = [&] {
            if (ghost_next < g_ghost_steps.size()) apply_ghost_step(g_ghost_steps[ghost_next++], ours, &events, &pit_events);
        };
        std::string extra;
        pit_in_->check_lap(extra);
        try {
            if (rh::env_num("GT3R_TICK_AI_TAPE", 0) != 0) {
                const std::vector<PlanCall> calls(tape->begin() + ptrdiff_t(std::min(flow_tape0_, tape->size())), tape->end());
                TapePlan plan(calls, ee, car(0));
                ex::tick_flow(ours, f, plan);
                if (extra.empty()) extra = plan.err;
                if (extra.empty() && !plan.done()) extra = rh::fmt("plan tape: %zu of %zu calls used", plan.used(), calls.size());
            } else {
                const ex::WearParams cp = ai_wear(ee, car(0));
                ex::CoursePlan plan(ai_course(ee, car(0)), cp, &flow_state_.zone, g_query);
                ex::tick_flow(ours, f, plan);
            }
        } catch (const std::exception& x) {
            if (extra.empty()) extra = x.what();
        }
        if (extra.empty() && ghost_next != g_ghost_steps.size()) extra = rh::fmt("%zu ghost steps vs %zu", ghost_next, g_ghost_steps.size());
        // what the loop left outside the records: the race flow's state, the race's event queue, the recorders, the pit events
        if (extra.empty()) extra = flow_differ(ee, cond_, flow_state_);
        if (extra.empty()) extra = queue_differ(ee, ee.r32(cond_), u32(queue0_), events);
        if (extra.empty()) extra = recorder_differ(ee, ee.r32(cond_), rec0_, flow_state_.recorder);
        if (extra.empty()) {
            const size_t n = g_pit_events.size() - std::min(pev0_, g_pit_events.size());
            if (n != pit_events.size()) extra = rh::fmt("%zu pit events vs %zu", pit_events.size(), n);
            else
                for (size_t k = 0; k < n && extra.empty(); ++k)
                    if (pit_events[k].type != g_pit_events[pev0_ + k].type || pit_events[k].car != g_pit_events[pev0_ + k].car)
                        extra = rh::fmt("pit event %zu", k);
        }
        ++flow.calls, ++flow.active;
        std::string d;
        for (int i = 0; i < n_ && d.empty(); ++i)
            if (std::string di = ce::first_difference(ours[size_t(i)], ce::read_car(ee, car(i))); !di.empty()) d = rh::fmt("car %d: ", i) + di;
        if (d.empty()) d = extra;
        if (!d.empty() && ++flow.bad == 1) flow.first = *scenario + ": " + d;
        if (carrying_) {
            ++carried.calls, ++carried.active;
            if (!d.empty() && ++carried.bad == 1) carried.first = *scenario + ": " + d;
            if (!d.empty()) carry_ = 0, carrying_ = false;
            else {
                mine_ = ours;
                for (int i = 0; i < n_; ++i) base_[size_t(i)] = ce::read_car(ee, car(i));
            }
        }
    }
};

// The race step's own parts the physics reads back (tick_race.cpp), against the step: the race-flow gate at its start (with
// the race-flow byte set: every car's accumulator car +0xEC4 and whether the step runs, $s1 at 0x1F3AB8) and the rubber band
// after the race flow's hooks (every car, from the records and the ranking order conductor + 0xE388 at 0x1F3EAC, compared
// at 0x1F4254), with the built parameters and mode and our course's lap length.
class FlowCheck {
public:
    Stats gate, band, pass_by;
    std::string* scenario = nullptr;
    const float* lap = nullptr; // the course's lap length (our road parse's +0x18)

    void install(Ee& e) {
        on_ = false;
        e.add_probe(race_setup::kCondStep, [this](Ee& ee) {
            cond_ = u32(ee.a(0));
            n_ = int(ee.r32(cond_ + race_setup::kK8 + k_off(0x6384)));
            K_ = cond_ + race_setup::kK8;
            pre_.clear();
            for (int i = 0; i < n_; ++i) pre_.push_back(ce::read_car(ee, car(i)));
            flow_ = ee.r8(K_ + k_off(0x37AC)), scale_ = ee::F::raw(ee.r32(K_ + k_off(0x37B0)));
            on_ = true;
        });
        e.add_probe(pa(0x001F3AB8), [this](Ee& ee) { // after the accumulators (the race-flow byte set)
            if (!on_) return;
            on_ = false;
            std::vector<ex::Car> ours = pre_;
            const ti::Built& b = ti::built();
            const bool run = ex::race_flow_gate(ours, b.flow, b.flow_scale);
            // (the flag's register: GT3's $s1, NTSC-J's $s3; a later build's is the one its gate tests here)
            const bool theirs = ee.g32(sim::concept_program(program()) ? int(ee.r32(ee.cur_pc) >> 21 & 31) : race_step_reg(17)) != 0;
            std::string extra = run != theirs ? rh::fmt("the step runs: %d vs %d", run, theirs) : std::string();
            if (extra.empty() && (b.flow != flow_ || b.flow_scale.bits != scale_.bits))
                extra = rh::fmt("race flow %u 0x%08X vs %u 0x%08X", b.flow, b.flow_scale.bits, flow_, scale_.bits);
            compare(ee, gate, ours, extra, true);
        });
        e.add_probe(pa(0x001F3AC8), [this](Ee& ee) { // the race-flow byte clear: the step runs
            if (!on_) return;
            on_ = false;
            std::vector<ex::Car> ours = pre_;
            const ti::Built& b = ti::built();
            const bool run = ex::race_flow_gate(ours, b.flow, b.flow_scale);
            std::string extra = run ? std::string() : "the step runs: 0 vs 1";
            if (extra.empty() && (b.flow != flow_ || b.flow_scale.bits != scale_.bits))
                extra = rh::fmt("race flow %u 0x%08X vs %u 0x%08X", b.flow, b.flow_scale.bits, flow_, scale_.bits);
            compare(ee, gate, ours, extra, false);
        });
        e.add_probe(pa(0x001F3EAC), [this](Ee& ee) { // after the race flow's hooks
            band_pre_.clear(), order_.clear();
            for (int i = 0; i < n_; ++i) band_pre_.push_back(ce::read_car(ee, car(i))), order_.push_back(s32(ee.r32(cond_ + cond_off(0xE388) + 4 * u32(i))));
            band_on_ = true;
        });
        e.add_probe(pa(0x001F4254), [this](Ee& ee) { // after the rubber band (every pass mode, none included)
            if (!band_on_) return;
            band_on_ = false;
            const ti::Built& b = ti::built();
            std::vector<ex::Car> ours = band_pre_;
            std::string extra = ti::differ(b.band, ti::mem_band(ee, cond_));
            const ee::F len = ee::F(*lap), theirs = ee::F::raw(ee.r32(ee.r32(ee.r32(ee.r32(K_ + k_off(0x37A8))) + 0x70) + 0x18));
            if (extra.empty() && len.bits != theirs.bits) extra = rh::fmt("lap length 0x%08X vs 0x%08X", len.bits, theirs.bits);
            const s32 mode = ex::rubber_band_mode(b.kind, b.cfg);
            try {
                ex::rubber_band(ours, order_, mode, b.band, len, ex::rubber_band_cap(b.kind));
            } catch (const std::exception& x) {
                if (extra.empty()) extra = x.what();
            }
            bool changed = false;
            for (int i = 0; i < n_; ++i) changed = changed || ce::read_car(ee, car(i)).time_scale.bits != band_pre_[size_t(i)].time_scale.bits;
            compare(ee, band, ours, extra, changed);
            ++modes_[mode];
        });
        // 0x1F59B0 at the step's end (every entry mode): the pass-by states on the step's proximity matrices.
        if (const u32 ret = pa(later_place(kPassByReturn)); ret != 0) {
            e.add_probe(pa(0x001F59B0), [this, ret](Ee& ee) {
                if (u32(ee.gpr[31].lo) != ret) return;
                pass_obj_ = u32(ee.a(0)), pass_pre_ = read_pass_by(ee, pass_obj_), pass_on_ = true;
                const u32 cond = ee.r32(pass_obj_);
                pass_n_ = int(ee.r32(cond + race_setup::kK8 + k_off(0x6384)));
                std::vector<u8> prox(0x140);
                ee.read_bytes(cond + kProxSlip, prox.data(), 0x140);
                pass_prox_ = read_proximity(prox.data());
            });
            e.add_probe(ret, [this](Ee& ee) {
                if (!pass_on_) return;
                pass_on_ = false;
                ex::PassByState ours = pass_pre_;
                ex::pass_by_step(ours, pass_prox_, pass_n_, ti::built().data.behaviour().pal_rate());
                const ex::PassByState theirs = read_pass_by(ee, pass_obj_);
                ++pass_by.calls;
                if (theirs.timer != pass_pre_.timer || theirs.state != pass_pre_.state) ++pass_by.active;
                std::string d;
                for (size_t i = 0; i < 8 && d.empty(); ++i)
                    for (size_t j = 0; j < 8 && d.empty(); ++j) {
                        if (ours.timer[i][j] != theirs.timer[i][j] || ours.state[i][j] != theirs.state[i][j])
                            d = rh::fmt("[%zu][%zu]: timer %d state %u vs %d %u", i, j, ours.timer[i][j], ours.state[i][j], theirs.timer[i][j],
                                        theirs.state[i][j]);
                        if (ours.state[i][j] != pass_pre_.state[i][j]) ++g_cover[rh::fmt("pass-by: state %u -> %u", pass_pre_.state[i][j], ours.state[i][j])];
                    }
                if (!d.empty() && ++pass_by.bad == 1) pass_by.first = *scenario + ": pass-by " + d;
            });
        }
    }
    std::string mode_counts() const {
        std::string s;
        for (const auto& [m, n] : modes_) s += rh::fmt("%smode %d: %ld", s.empty() ? "" : ", ", m, n);
        return s;
    }

private:
    bool on_ = false, band_on_ = false, pass_on_ = false;
    u32 cond_ = 0, K_ = 0, pass_obj_ = 0;
    int pass_n_ = 0;
    ex::PassByState pass_pre_;
    ex::Proximity pass_prox_;
    static ex::PassByState read_pass_by(Ee& ee, u32 obj) { // timers at +4 + 16 i + 2 j, states at +0x84 + 8 i + j
        ex::PassByState p;
        for (u32 i = 0; i < 8; ++i)
            for (u32 j = 0; j < 8; ++j) p.timer[i][j] = s16(ee.r16(obj + 4 + 16 * i + 2 * j)), p.state[i][j] = ee.r8(obj + 0x84 + 8 * i + j);
        return p;
    }
    int n_ = 0;
    u8 flow_ = 0;
    ee::F scale_;
    std::vector<ex::Car> pre_, band_pre_;
    std::vector<s32> order_;
    std::map<s32, long> modes_;

    u32 car(int i) const { return race_setup::car_addr(cond_, i); }
    void compare(Ee& ee, Stats& s, const std::vector<ex::Car>& ours, std::string extra, bool active) {
        ++s.calls;
        if (active) ++s.active;
        std::string d;
        for (int i = 0; i < n_ && d.empty(); ++i)
            if (std::string di = ce::first_difference(ours[size_t(i)], ce::read_car(ee, car(i))); !di.empty()) d = rh::fmt("car %d: ", i) + di;
        if (d.empty()) d = std::move(extra);
        if (!d.empty() && ++s.bad == 1) s.first = *scenario + ": " + d;
    }
};

// The race flow after phase 13 (race_flow.h) against the step, in every race mode: the conductor's virtual 13
// (ex::race_mode_start) at its call; each car's 0x1EF060 from the step's loop over the ranking order (ours with the progress
// query on our road, CourseQuery, and the built inputs); the conductor's virtual 1 (ex::race_mode_display: the sector
// order's time differences) and 14 (ex::race_mode_clock: the licence's judging, the data logger's first entries, the race
// clock); then the gap matrices 0x1ED458 and the ranking (virtual 3). Each is compared at its return: the cars, the race
// flow's state (the clock, end countdown and result words, the session's and entries' records, the sector order, the data
// logger's tables), its event posts (the race display's among them) and the free run's recorder calls. The unported calls
// inside (the pit lane's state machine 0x1EDB40, 0x1EACB8's AI driver calls, the hand-backs to the player 0x1E3BF0, the
// split-screen battle's AI take-over 0x1F6A20, the free run's AI drive-on 0x1E3C78) are applied to ours from what they
// changed in the original's run.
class RaceFlowCheck {
public:
    Stats car, start, display, clock, gaps, ranking, direct, hits;
    std::string* scenario = nullptr;
    const ex::CourseQuery* const* course = nullptr;
    const float* split = nullptr; // the road's +0x1C
    const float* lap = nullptr;   // the road's +0x18

    void install(Ee& e) {
        part_ = Part::none, sub_on_ = gap_on_ = rank_on_ = run_end_on_ = false;
        // Virtual 13: its call at 0x1F3E0C returns to 0x1F3E14.
        guarded(e, pa(0x001F3E0C), [this](Ee& ee) { begin(ee, step_conductor(ee), Part::start, true); });
        guarded(e, pa(0x001F3E14), [this](Ee& ee) {
            if (part_ != Part::start) return;
            finish(ee, start, [&](std::vector<ex::Car>& cars, ex::RaceFlow&, std::vector<ex::RaceEvent>& events, const ex::RaceFlowHooks& hooks) {
                ex::race_mode_start(cars, in_, events, hooks);
            });
        });
        // Each car's 0x1EF060 (its call in the step's loop returns to 0x1F3E40).
        guarded(e, pa(0x001EF060), [this](Ee& ee) {
            if (u32(ee.gpr[31].lo) != pa(0x001F3E40)) return;
            if (!ex::race_flow_ported(ti::built().kind)) {
                ++car.skipped;
                return;
            }
            i_ = int(ee.a(1));
            const ex::Conductor k = ti::built().kind;
            begin(ee, u32(ee.a(0)), Part::car, k == ex::Conductor::Battle2P || k == ex::Conductor::FreeRun || k == ex::Conductor::License);
            const u32 body = cond_ + race_setup::kCm + race_setup::kBody0 + race_setup::kBodyStride * u32(i_);
            hit_ = ee.r32(body + 0x43C) != 0 || ee.r32(body + 0x440) != 0;
        });
        guarded(e, pa(0x001F3E40), [this](Ee& ee) {
            if (part_ != Part::car) return;
            if (sub_on_) { // (a later build's pit machine, entered by a jump)
                sub_on_ = false;
                effects_.push_back({Group::pit, sub_car_, sub_pre_, ce::read_car(ee, car_at(sub_car_)), queue_events(ee, sub_queue_)});
            }
            finish(ee, car, [&](std::vector<ex::Car>& cars, ex::RaceFlow& flow, std::vector<ex::RaceEvent>& events, const ex::RaceFlowHooks& hooks) {
                const ex::Car pre = cars[size_t(i_)];
                ex::race_flow_car(cars, order_, i_, hit_, in_, flow, events, hooks);
                const ex::Car& now = cars[size_t(i_)];
                if (now.laps != pre.laps) ++g_cover[pre.laps == 0 ? "race flow: the first crossing" : "race flow: a lap done"];
                if (now.sector != pre.sector && now.sector != 0) ++g_cover["race flow: a sector line"];
                if (now.lap_base > pre.lap_base) ++g_cover["race flow: the start line backwards"];
                if (now.lap_base < pre.lap_base) ++g_cover["race flow: the start line forwards with a lap base"];
                if (now.finished != pre.finished) ++g_cover[rh::fmt("race flow: finished (%s)", ex::conductor_name(in_.kind))];
                if ((now.x535 & 1) && !(pre.x535 & 1)) ++g_cover["race flow: wrong way"];
                if (now.x51c != pre.x51c || now.x51d != pre.x51d) ++g_cover[rh::fmt("race flow: HUD message %u", now.x51c)];
                if (now.laps < pre.laps) ++g_cover["race flow: a lap put back (the pod race's missed stops)"];
                if (now.max_speed.bits != pre.max_speed.bits) ++g_cover["race flow: the machine test's top speed"];
                if (now.x493 != pre.x493 && now.x493 != 0) ++g_cover["race flow: a data logger mark"];
                for (const ex::RaceEvent& x : events) {
                    ++g_cover[rh::fmt("race flow: %s event %d", ex::conductor_name(in_.kind), x.type)];
                    if (x.type == 12 && x.sector >= 4) ++g_cover["race flow: a sector time past the entry's fourth split"];
                    if (x.type == 13 && in_.kind == ex::Conductor::License && pre.laps > 1) ++g_cover["race flow: a licence's logger mark past its first lap"];
                }
                for (const ex::RecorderEvent& x : flow.recorder) ++g_cover[rh::fmt("race flow: recorder event %d", int(x.kind))];
            });
        });
        // Virtual 1: its call at 0x1F3E68 returns to 0x1F3E70.
        guarded(e, pa(0x001F3E68), [this](Ee& ee) { begin(ee, step_conductor(ee), Part::display, true); });
        guarded(e, pa(0x001F3E70), [this](Ee& ee) {
            if (part_ != Part::display) return;
            finish(ee, display, [&](std::vector<ex::Car>& cars, ex::RaceFlow& flow, std::vector<ex::RaceEvent>& events, const ex::RaceFlowHooks&) {
                ex::race_mode_display(cars, in_, flow, events);
                if (!events.empty()) ++g_cover["race flow: the sector order's time differences"];
            });
        });
        // Virtual 14: its call at 0x1F3E80 returns to 0x1F3E88.
        guarded(e, pa(0x001F3E80), [this](Ee& ee) { begin(ee, step_conductor(ee), Part::clock, true); });
        guarded(e, pa(0x001F3E88), [this](Ee& ee) {
            if (part_ != Part::clock) return;
            finish(ee, clock, [&](std::vector<ex::Car>& cars, ex::RaceFlow& flow, std::vector<ex::RaceEvent>& events, const ex::RaceFlowHooks& hooks) {
                const s32 result = flow.result;
                const std::vector<ex::Car> pre = cars;
                const ex::StopZone zone = flow.zone;
                ex::race_mode_clock(cars, in_, flow, events, hooks);
                if (flow.result != result) ++g_cover[rh::fmt("race flow: the licence's result %d", flow.result)];
                for (size_t k = 0; k < cars.size(); ++k)
                    if (cars[k].x51c != pre[k].x51c && (cars[k].x51c == 0x10 || cars[k].x51c == 0x11))
                        ++g_cover[rh::fmt("pod race: HUD message 0x%X (control %d)", cars[k].x51c, cars[k].control)];
                for (size_t k = 0; k < 8; ++k)
                    if (flow.zone.stops[k] != zone.stops[k]) ++g_cover[rh::fmt("pod race: a stop counted (car %zu)", k)];
                for (const ex::RaceEvent& x : events) ++g_cover[rh::fmt("race flow clock: event %d value %d", x.type, x.value)];
            });
        });
        // Virtual 41 at the collision step's hits (0x1DFEB8's call at 0x1DFF04 returns to 0x1DFF0C; a1 the car, a2 the kind, f12
        // the impulse).
        // (a later build's report first sets special car 1's mood, Rule::HitReport: from the report's entry, a0 the car, a1 the
        // kind, a2 the part)
        const bool later = sim::concept_program(program());
        guarded(e, pa(later ? 0x001DFEB8 : 0x001DFF04), [this, later](Ee& ee) {
            hit_car_ = u32(ee.a(later ? 0 : 1)), hit_kind_ = s32(ee.a(later ? 1 : 2)), hit_part_ = s32(ee.a(2)), hit_j_ = ee::F::raw(ee.fpr[12]);
            hit_pre_ = ce::read_car(ee, hit_car_), hit_raw_ = ce::snapshot(ee, hit_car_), hit_on_ = true;
        });
        guarded(e, pa(0x001DFF0C), [this, later](Ee& ee) {
            if (!hit_on_) return;
            hit_on_ = false;
            ex::Car ours = hit_pre_;
            if (later) {
                ex::hit_mood(ours, ex::mood_tables(ti::built().data), hit_kind_, hit_part_);
                if (ours.later.special != hit_pre_.later.special)
                    ++g_cover[rh::fmt("hit report: special car 1's mood %u (kind %d, part %d)", ours.later.special[1] & 0xF, hit_kind_, hit_part_)];
            }
            ex::hit_course_out(ours, ti::built().kind, hit_kind_, hit_j_);
            ++hits.calls;
            if (ce::snapshot(ee, hit_car_) != hit_raw_) ++hits.active, ++g_cover[rh::fmt("hit course-out: kind %d", hit_kind_)];
            ++g_cover[rh::fmt("hit course-out call: %s kind %d", ex::conductor_name(ti::built().kind), hit_kind_)];
            if (std::string d = ce::first_difference(ours, ce::read_car(ee, hit_car_)); !d.empty() && ++hits.bad == 1)
                hits.first = *scenario + ": " + d;
        });
        // The unported calls inside: their changes to the car they are given, in order.
        struct Sub {
            Pa fn, ret;
            Group group;
            s32 off; // a0 = the car + off (-1: the per-car call's car)
        };
        static constexpr Sub kSubs[] = {
            {0x001EDB40, 0x001EF244, Group::pit, -1},
            {0x001F6548, 0x001EAD2C, Group::finish, 0xEC8}, {0x001F6380, 0x001EAD38, Group::finish, 0xEC8},
            {0x001F6518, 0x001EAD64, Group::finish, 0xEC8}, {0x001E3C78, 0x001EAD6C, Group::finish, 0},
            {0x001E3C78, 0x001EAD9C, Group::finish, 0},      {0x001E3E38, 0x001EAD7C, Group::finish, 0},
            {0x001E3E80, 0x001EAD8C, Group::finish, 0},
            {0x001E3BF0, 0x001F28C0, Group::handback, 0}, // the single races' virtual 13
            {0x001E3BF0, 0x001F19B8, Group::handback, 0}, // the free run's virtual 13
            {0x001E3BF0, 0x001F1D04, Group::handback, 0}, // the time trial's virtual 17
            {0x001E3BF0, 0x001F0E94, Group::handback, 0}, // the licence's virtual 17
            {0x001E3BF0, 0x001F15A8, Group::handback, 0}, // the free run's virtual 17
            {0x001F6A20, 0x001F024C, Group::takeover, 0xEC8},
            {0x001E3C78, 0x001F1680, Group::drive, 0},    // the free run's virtual 17 (race info + 0x74)
            {0x001E3C78, 0x001F1920, Group::drive, 0},    // and 18
        };
        for (const Sub& s : kSubs) {
            if (!has_pa(s.fn.ntsc_u)) continue; // not in the disc's program
            // (a later build's per-car routine jumps to the pit machine at its end: it returns to the step's loop, where
            // the per-car check takes its effects before it runs ours)
            const bool tail = s.group == Group::pit && sim::concept_program(program()) && !has_pa(s.ret.ntsc_u);
            // (and its end of a run 0x1EACB8 jumps to its last call, which returns to the routine's own caller: the call's
            // return address is turned to a place of the harness, kJumpReturn, that takes the effect and returns there)
            if (s.group == Group::finish && sim::concept_program(program()) && !has_pa(s.ret.ntsc_u)) {
                guarded(e, s.fn, [this, s](Ee& ee) {
                    if (part_ == Part::none || !run_end_on_ || sub_on_ || u32(ee.gpr[31].lo) != run_end_ra_) return;
                    sub_car_ = int((u32(ee.a(0)) - car_off(u32(s.off)) - car_at(0)) / car_stride());
                    sub_pre_ = ce::read_car(ee, car_at(sub_car_)), sub_queue_ = queue_count(ee), sub_on_ = true;
                    jump_group_ = s.group, ee.gpr[31].lo = kJumpReturn;
                });
                continue;
            }
            if (!has_pa(s.ret.ntsc_u) && !tail) continue;
            const u32 ret = tail ? pa(0x001F3E40) : u32(s.ret);
            guarded(e, s.fn, [this, s, ret](Ee& ee) {
                if (part_ == Part::none || u32(ee.gpr[31].lo) != ret) return;
                sub_car_ = s.off < 0 ? i_ : int((u32(ee.a(0)) - car_off(u32(s.off)) - car_at(0)) / car_stride());
                sub_pre_ = ce::read_car(ee, car_at(sub_car_)), sub_queue_ = queue_count(ee), sub_on_ = true;
            });
            if (tail) continue;
            guarded(e, s.ret, [this, s](Ee& ee) {
                if (part_ == Part::none || !sub_on_) return;
                sub_on_ = false;
                effects_.push_back({s.group, sub_car_, sub_pre_, ce::read_car(ee, car_at(sub_car_)), queue_events(ee, sub_queue_)});
            });
        }
        // A later build's pod race at the line: its stop driver (later 0x1EF8A0, called from 0x1EE7F0 for the car the lap
        // handler has: the per-car flow's or the direct call's)
        if (const u32 call = pa(later_place(kPodStopDriverCall)); call != 0) {
            guarded(e, call, [this](Ee& ee) {
                if (part_ == Part::none) return;
                sub_car_ = i_, sub_pre_ = ce::read_car(ee, car_at(sub_car_)), sub_queue_ = queue_count(ee), sub_on_ = true;
            });
            guarded(e, call + 8, [this](Ee& ee) {
                if (part_ == Part::none || !sub_on_) return;
                sub_on_ = false;
                effects_.push_back({Group::stop, sub_car_, sub_pre_, ce::read_car(ee, car_at(sub_car_)), queue_events(ee, sub_queue_)});
            });
        }
        if (sim::concept_program(program())) {
            guarded(e, pa(0x001EACB8), [this](Ee& ee) { run_end_on_ = part_ != Part::none, run_end_ra_ = u32(ee.gpr[31].lo); });
            e.hle(kJumpReturn, [this](Ee& ee) {
                if (sub_on_ && part_ != Part::none)
                    effects_.push_back({jump_group_, sub_car_, sub_pre_, ce::read_car(ee, car_at(sub_car_)), queue_events(ee, sub_queue_)});
                sub_on_ = run_end_on_ = false;
                ee.gpr[31].lo = run_end_ra_;
            });
        }
        // 0x1ED458 (its call at 0x1F3E88 returns to 0x1F3E90).
        guarded(e, pa(0x001ED458), [this](Ee& ee) {
            if (u32(ee.gpr[31].lo) != pa(0x001F3E90)) return;
            cond_ = u32(ee.a(0));
            gap_pre_ = read_gaps(ee);
            gap_on_ = true;
        });
        guarded(e, pa(0x001F3E90), [this](Ee& ee) {
            if (!gap_on_) return;
            gap_on_ = false;
            const int n = int(ee.r32(cond_ + race_setup::kK8 + k_off(0x6384)));
            std::vector<ex::Car> cars;
            for (int i = 0; i < n; ++i) cars.push_back(ce::read_car(ee, car_at(i)));
            ex::GapMatrix ours = gap_pre_;
            ex::gap_matrix(cars, ee::F(*lap), ours);
            const ex::GapMatrix theirs = read_gaps(ee);
            ++gaps.calls;
            std::string d;
            bool changed = false;
            for (size_t i = 0; i < 8 && d.empty(); ++i)
                for (size_t j = 0; j < 8 && d.empty(); ++j) {
                    changed = changed || theirs.laps[i][j] != gap_pre_.laps[i][j] || theirs.gap[i][j].bits != gap_pre_.gap[i][j].bits;
                    if (ours.laps[i][j] != theirs.laps[i][j] || ours.gap[i][j].bits != theirs.gap[i][j].bits)
                        d = rh::fmt("[%zu][%zu]: %d laps 0x%08X vs %d 0x%08X", i, j, ours.laps[i][j], ours.gap[i][j].bits, theirs.laps[i][j],
                                    theirs.gap[i][j].bits);
                    if (ours.laps[i][j] > 0) ++g_cover["gap matrix: a lap behind"];
                    if (ours.laps[i][j] < 0) ++g_cover["gap matrix: a lap ahead"];
                }
            if (changed) ++gaps.active;
            if (!d.empty() && ++gaps.bad == 1) gaps.first = *scenario + ": gap matrix " + d;
        });
        // Virtual 3 (its call at 0x1F3EA0 returns to 0x1F3EA8).
        guarded(e, pa(0x001F3EA0), [this](Ee& ee) {
            cond_ = step_conductor(ee);
            const int n = int(ee.r32(cond_ + race_setup::kK8 + k_off(0x6384)));
            rank_pre_.clear();
            rank_raw_.clear();
            for (int i = 0; i < n; ++i) rank_pre_.push_back(ce::read_car(ee, car_at(i))), rank_raw_.push_back(ce::snapshot(ee, car_at(i)));
            rank_order_.fill(-1);
            for (int k = 0; k < n; ++k) rank_order_[size_t(k)] = s32(ee.r32(cond_ + cond_off(0xE388) + 4 * u32(k)));
            rank_on_ = true;
        });
        guarded(e, pa(0x001F3EA8), [this](Ee& ee) {
            if (!rank_on_) return;
            rank_on_ = false;
            std::vector<ex::Car> ours = rank_pre_;
            ex::RaceOrder order = rank_order_;
            ex::race_ranking(ours, order, ti::built().kind);
            const int n = int(ours.size());
            ++ranking.calls;
            std::string d;
            bool changed = false;
            for (int k = 0; k < n && d.empty(); ++k) {
                const s32 theirs = s32(ee.r32(cond_ + cond_off(0xE388) + 4 * u32(k)));
                if (theirs != rank_order_[size_t(k)]) changed = true;
                if (order[size_t(k)] != theirs) d = rh::fmt("order [%d]: %d vs %d", k, order[size_t(k)], theirs);
            }
            for (int i = 0; i < n && d.empty(); ++i) {
                if (ce::snapshot(ee, car_at(i)) != rank_raw_[size_t(i)]) changed = true;
                if (std::string di = ce::first_difference(ours[size_t(i)], ce::read_car(ee, car_at(i))); !di.empty()) d = rh::fmt("car %d: ", i) + di;
            }
            if (changed) ++ranking.active, ++g_cover["ranking: the order changed"];
            if (!d.empty() && ++ranking.bad == 1) ranking.first = *scenario + ": " + d;
        });
    }


    // The race mode's virtuals 1, 13-19 and 41 called directly on changed race states (every 30 ticks of the races): car i's
    // finish, ghost, control source, driver kind, course-out, index, best and last crossing, top speed, stop-zone ground and
    // speed, run-up; the other cars' control sources; the race info's replay, records lock, judging, result words, the
    // session's best splits and the licence's judging times (a stand-in record); the entries' current and best splits; the
    // sector order; the race-flow byte; with the data logger's tables its flags; the calls' arguments. Ours (race_mode_display
    // / _start / _clock / _sector_order / _sector / _first_crossing / _lap / _split, hit_course_out) must leave the same cars,
    // race flow state, events and recorder calls. The race's state is restored after.
    void run_direct(Ee& ee, Rng& rng, u32 cond, const std::string& where) {
        const ti::Built& b = ti::built();
        cond_ = cond;
        const u32 K = cond + race_setup::kK8, I = info(ee), vt = ee.r32(K + k_off(0x63A8));
        const int n = int(ee.r32(K + k_off(0x6384)));
        if (n < 1) return;
        std::vector<std::vector<u8>> saved;
        for (int k = 0; k < n; ++k) saved.push_back(ce::snapshot(ee, car_at(k)));
        // the race info, the race-flow byte, the sector order, the entries' records, the data logger's tables, the free run's
        // recorder headers
        std::vector<std::pair<u32, std::vector<u8>>> regions;
        auto keep = [&](u32 a, u32 size) {
            std::vector<u8> v(size);
            ee.read_bytes(a, v.data(), size);
            regions.push_back({a, std::move(v)});
        };
        keep(I, 0x1000), keep(K + k_off(0x37AC), 8), keep(K + k_off(0x3AF8), 0xB0);
        for (int i = 0; i < n; ++i)
            if (const u32 e = race_entry(ee, I, i)) keep(e + 0x210, 0x10F0), keep(e + entry_off(0x2100), 4); // (to +0x1300: a licence's later sectors)
        bool tables = true;
        for (int i = 0; i < n; ++i) {
            const u32 tab = logger_table(ee, car_at(i));
            tables = tables && tab != 0;
            if (tab) keep(tab - snap_size(), snap_size() * u32(kLoggerEnd + 1));
        }
        for (const auto& [a, size] : g_scratch) keep(a, size);
        if (sim::concept_program(program())) keep(cond + kZoneAt, 0x74); // (the stop zone's counts)
        const size_t rec_calls = g_rec_calls.size();
        const u32 clock0 = ee.r32(cond + cond_off(0xE380)), holder = ee.r32(I + 0x28), holder_rec = ee.r32(holder + holder_off(0x18));
        if (!rec_) rec_ = ee.alloc(0x200, 16);
        ee.w32(holder + holder_off(0x18), rec_);
        const int sectors = course && *course ? ex::sector_count(b.kind, ee.r8(cond + cond_off(0xE341)), **course) : 1;
        for (int k = 0; k < 6; ++k) {
            const int i = int(rng.range(0, n - 1));
            for (int j = 0; j < n; ++j) {
                const u32 C = car_at(j);
                ex::Car m = ce::read_car(ee, C);
                if (j == i) {
                    m.finished = u8(rng.chance(0.25));
                    m.ghost = u8(rng.chance(0.15));
                    m.control = s8(rng.chance(0.5) ? 0 : rng.chance(0.4) ? 1 : 2);
                    m.driver_kind = u8(rng.chance(0.5) ? 0 : rng.chance(0.4) ? 1 : rng.chance(0.5) ? 3 : 5);
                    m.course_out = u8(rng.chance(0.5) ? 0 : rng.range(1, 3));
                    m.index = u8(rng.chance(0.7) ? j : rng.range(0, n - 1));
                    m.best_lap = rng.chance(0.4) ? 0x157529FFu : u32(rng.range(30000, 200000));
                    m.x530 = u32(rng.range(0, 100000));
                    m.speed_abs = ee::F(rng.uniform(0.0f, 90.0f)), m.max_speed = ee::F(rng.uniform(0.0f, 90.0f));
                    m.x535 = u8((m.x535 & ~0x10) | (rng.chance(0.3) ? 0x10 : 0));
                    const bool zone = rng.chance(0.5);
                    for (ex::Ground& g : m.g) g.flags = u8((g.flags & ~4) | (zone || rng.chance(0.2) ? 4 : 0));
                    const float v = rng.chance(0.5) ? rng.uniform(0.0f, 0.3f) : rng.uniform(0.0f, 30.0f);
                    m.vel = {ee::F(v * 0.6f), ee::F(v * 0.0f), ee::F(v * 0.8f)};
                    m.ai_hold = u16(rng.chance(0.5) ? rng.range(0, 2) : rng.range(3, 200));
                    m.x493 = u8(rng.range(0, 3));
                } else {
                    m.control = s8(rng.chance(0.5) ? 0 : 2);
                }
                ce::write_car(ee, C, m);
            }
            ee.w32(I + 0x6C, u32(rng.chance(0.15))), ee.w32(I + 0x80, u32(rng.chance(0.2))), ee.w32(I + 0x7C, u32(rng.chance(0.15)));
            ee.w32(I + 0x54, rng.chance(0.5) ? 3u : 2u);
            // the ghost's playback empty: no ghost restart (its extra step, a whole race step, is checked in the races)
            if (const u32 g = ghost_recorder(ee, I)) ee.w32(g + 0x140, 1);
            ee.w32(I + 0x5C, rng.chance(0.8) ? 0u : u32(rng.range(1, 2)));
            if (tables) ee.w32(I + 0x70, u32(rng.chance(0.7))), ee.w32(I + 0x74, u32(rng.chance(0.7)));
            const u32 laptime = u32(rng.range(0, 120000));
            auto near = [&](s64 t) { return rng.chance(0.3) ? 0x157529FFu : u32(std::max<s64>(0, t + rng.range(-5000, 5000))); };
            for (u32 s = 0; s < 5; ++s) ee.w32(I + info_off(0x8AC) + 4 * s, near(s == 4 ? s64(laptime) : s64(laptime) * (s + 1) / 5));
            for (int j = 0; j < n; ++j)
                if (const u32 e = race_entry(ee, I, j))
                    for (u32 s = 0; s < 10; ++s) ee.w32(e + 0x11C8 + 4 * s, near(s64(laptime) * ((s % 5) + 1) / 5));
            for (u32 s = 0; s < 4; ++s) { // the sector order: a lead lap, the cars in order of their times, some posted
                const u32 cnt = u32(rng.range(0, std::min(n, 7))), shown = u32(rng.range(0, s32(cnt)));
                ee.w16(K + k_off(0x3B78) + 2 * s, u16(rng.range(-1, 3))), ee.w8(K + k_off(0x3B80) + s, u8(cnt)), ee.w8(K + k_off(0x3B84) + s, u8(shown));
                u32 t = u32(rng.range(0, 50000));
                for (u32 p = 0; p < 8; ++p)
                    ee.w32(K + k_off(0x3AF8) + 32 * s + 4 * p, t), t += u32(rng.range(0, 3000)), ee.w8(K + k_off(0x3B88) + 8 * s + p, u8(rng.range(0, n - 1)));
            }
            ee.w32(I + 0x40, u32(rng.chance(0.5) ? 0 : rng.chance(0.5) ? 1 : -2));
            ee.w8(K + k_off(0x37AC), u8(rng.chance(0.8) ? 0 : 2));
            for (u32 r = 0; r < 3; ++r) { // gold, silver, bronze around the lap time
                const u32 t = u32(std::max<s64>(0, s64(laptime) + rng.range(-20000, 20000) + 8000 * s64(r)));
                ee.w8(rec_ + 0x1C4 + 4 * r, u8(t / 60000)), ee.w8(rec_ + 0x1C5 + 4 * r, u8(t / 1000 % 60)), ee.w16(rec_ + 0x1C6 + 4 * r, u16(t % 1000));
            }
            static constexpr int kVirtuals[] = {1, 13, 14, 15, 16, 17, 18, 19, 41};
            const int vf = kVirtuals[rng.range(0, int(std::size(kVirtuals)) - 1)];
            const u32 e = vt + 8 * u32(conductor_slot(vf)), self = cond + u32(s32(s16(ee.r16(e)))), fn = ee.r32(e + 4);
            const s32 units = program() == Program::Pal ? 60 : 50; // the race clock's units a step
            const s32 v = s32(rng.range(0, units - 1));
            const u32 clock_v = ee.r32(cond + cond_off(0xE380)) + u32(v), t = clock_v / 3;
            s32 laps = rng.chance(0.5) ? s32(ee.r8(cond + cond_off(0xE341))) : s32(rng.range(0, 3));
            const s32 sector = s32(rng.range(0, std::max(0, std::min(sectors, 4) - 1)));
            // the licence's sector numbers count on over the laps (from lap 1: these virtuals run once a car has crossed the line),
            // past the entry's fourth split into its following words
            if (b.kind == ex::Conductor::License && (vf == 16 || vf == 18)) laps = std::max(laps, s32(1));
            const ee::F frac = ee::F(rng.uniform(0.0f, 1.0f));
            i_ = i;
            begin(ee, cond, Part::direct, true);
            const u8 pre_finished = cars_[size_t(i)].finished;
            const u32 pre_best = cars_[size_t(i)].best_lap;
            const u8 pre_out = cars_[size_t(i)].course_out;
            in_.flow = ee.r8(K + k_off(0x37AC));
            std::vector<Arg> args{Arg::ptr(self), Arg::integer(i)};
            if (vf == 15)
                for (u32 a : {u32(laps - 1), u32(sector), t, laptime}) args.push_back(Arg::integer(s64(s32(a))));
            if (vf == 16)
                for (u32 a : {u32(laps), u32(sector), clock_v, t, laptime}) args.push_back(Arg::integer(s64(a)));
            if (vf == 17) args.push_back(Arg::integer(t)), args.push_back(Arg::integer(units - v)), args.push_back(Arg::fbits(frac.bits));
            if (vf == 18 || vf == 19)
                for (u32 a : {u32(laps), clock_v, t, laptime, u32(units - v)}) args.push_back(Arg::integer(s64(a)));
            if (vf == 18) args.push_back(Arg::fbits(frac.bits));
            if (vf == 1 || vf == 13 || vf == 14) args.resize(1);
            // virtual 41: the car, the hit's kind (0 wall, 1 car, 2 other, 3) and its impulse (up to twice the 10 m/s at the mass)
            const s32 hit_kind = s32(rng.range(0, 3));
            const ee::F hit_j = ee::F(rng.uniform(0.0f, 20.0f) * cars_[size_t(i)].p.mass.host());
            if (vf == 41)
                args = {Arg::ptr(self), Arg::ptr(car_at(i)), Arg::integer(hit_kind), Arg::integer(0), Arg::fbits(hit_j.bits)};
            ++g_cover[rh::fmt("race flow direct: %s virtual %d", ex::conductor_name(b.kind), vf)];
            try {
                ee.call(fn, args, 50'000'000);
            } catch (const std::exception& x) {
                part_ = Part::none;
                if (++direct.bad == 1) direct.first = where + ": the original's virtual " + std::to_string(vf) + ": " + x.what();
                continue;
            }
            const std::string label = where + rh::fmt(" case %d (virtual %d, car %d)", k, vf, i);
            std::string* keep_scenario = scenario;
            std::string tmp = label;
            scenario = &tmp;
            finish(ee, direct, [&](std::vector<ex::Car>& cars, ex::RaceFlow& flow, std::vector<ex::RaceEvent>& events, const ex::RaceFlowHooks& hooks) {
                switch (vf) {
                case 1: ex::race_mode_display(cars, in_, flow, events); break;
                case 13: ex::race_mode_start(cars, in_, events, hooks); break;
                case 14: ex::race_mode_clock(cars, in_, flow, events, hooks); break;
                case 15: ex::race_mode_sector_order(cars, i, laps - 1, sector, t, in_, flow, events); break;
                case 16: ex::race_mode_sector(cars, i, laps, sector, laptime, in_, flow, events); break;
                case 17: ex::race_mode_first_crossing(cars, i, t, units - v, frac, in_, flow, events, hooks); break;
                case 18: ex::race_mode_lap(cars, order_, i, laps, t, laptime, units - v, frac, in_, flow, events, hooks); break;
                case 41: ex::hit_course_out(cars[size_t(i)], in_.kind, hit_kind, hit_j); break;
                default: ex::race_mode_split(cars, i, laps, t, laptime, in_, flow, events, hooks); break;
                }
                for (const ex::RaceEvent& x : events) {
                    ++g_cover[rh::fmt("race flow direct: event %d", x.type)];
                    if (x.type == 12 && x.sector >= 4) ++g_cover["race flow direct: a sector time past the entry's fourth split"];
                }
                if (flow.result != flow_.result) ++g_cover[rh::fmt("race flow direct: result %d", flow.result)];
                if (flow.record != flow_.record) ++g_cover["race flow direct: a new record"];
                if (flow.end_count != flow_.end_count) ++g_cover["race flow direct: the end countdown"];
                if (cars[size_t(i)].finished != pre_finished) ++g_cover["race flow direct: finished"];
                if (cars[size_t(i)].best_lap != pre_best) ++g_cover["race flow direct: a best lap"];
                if (vf == 41 && cars[size_t(i)].course_out != pre_out) ++g_cover[rh::fmt("race flow direct: a hit's course-out (kind %d)", hit_kind)];
                for (const ex::RecorderEvent& x : flow.recorder) ++g_cover[rh::fmt("race flow direct: recorder event %d", int(x.kind))];
            });
            scenario = keep_scenario;
        }
        for (int k = 0; k < n; ++k) ce::write_image(ee, car_at(k), saved[size_t(k)].data());
        for (const auto& [a, v] : regions) ee.write_bytes(a, ByteSpan(v.data(), v.size()));
        g_rec_calls.resize(rec_calls);
        ee.w32(cond + cond_off(0xE380), clock0), ee.w32(holder + holder_off(0x18), holder_rec);
    }

private:
    enum class Part { none, start, car, display, clock, direct };
    enum class Group { pit, finish, handback, takeover, drive, stop };
    struct Effect {
        Group group;
        int car;
        ex::Car pre, post;
        std::vector<ex::RaceEvent> events; // what the call posted to the race's queue
    };
    Part part_ = Part::none;
    bool sub_on_ = false, hit_ = false, gap_on_ = false, rank_on_ = false, all_ = false;
    // A later build's end of a run that is running, its return address, and the group of the call it jumped to.
    static constexpr u32 kJumpReturn = 0x000FFFF0;
    bool run_end_on_ = false;
    u32 run_end_ra_ = 0;
    Group jump_group_ = Group::finish;
    ex::GapMatrix gap_pre_;
    std::vector<ex::Car> rank_pre_;
    std::vector<std::vector<u8>> rank_raw_;
    ex::RaceOrder rank_order_{};
    u32 cond_ = 0, queue_ = 0, sub_queue_ = 0;
    int n_ = 0, i_ = 0, sub_car_ = 0;
    std::vector<s32> order_;
    std::vector<ex::Car> cars_;
    std::vector<std::vector<u8>> raws_;
    ex::Car sub_pre_;
    std::vector<Effect> effects_;
    ex::Snapshot ghost_, lap_;
    ex::RaceFlowInputs in_;
    ex::RaceFlow flow_;
    size_t rec0_ = 0, ghost0_ = 0;
    std::string in_bad_;
    u32 rec_ = 0; // the direct calls' stand-in licence record
    bool hit_on_ = false;
    u32 hit_car_ = 0;
    s32 hit_kind_ = 0, hit_part_ = 0;
    ee::F hit_j_;
    ex::Car hit_pre_;
    std::vector<u8> hit_raw_;

    u32 car_at(int i) const { return race_setup::car_addr(cond_, i); }
    u32 info(Ee& ee) const { return ee.r32(cond_); }
    ex::GapMatrix read_gaps(Ee& ee) const { return ti::mem_gaps(ee, cond_); }
    u32 queue_count(Ee& ee) const { return ee.r32(info(ee) + 0x84 + queue_count_off()); }
    // A part's start: the cars (all, or car i_ and the leader), the order, the inputs, the race's state and queue.
    void begin(Ee& ee, u32 cond, Part part, bool all) {
        cond_ = cond, part_ = part, all_ = all;
        n_ = int(ee.r32(cond_ + race_setup::kK8 + k_off(0x6384)));
        order_.clear();
        for (int k = 0; k < n_; ++k) order_.push_back(s32(ee.r32(cond_ + cond_off(0xE388) + 4 * u32(k))));
        cars_.assign(size_t(n_), ex::Car{});
        raws_.assign(size_t(n_), {});
        for (int k = 0; k < n_; ++k)
            if (all || k == i_ || k == order_[0]) cars_[size_t(k)] = ce::read_car(ee, car_at(k)), raws_[size_t(k)] = ce::snapshot(ee, car_at(k));
        in_ = inputs(ee);
        flow_ = read_flow(ee, cond_);
        queue_ = queue_count(ee), rec0_ = g_rec_calls.size(), ghost0_ = g_ghost_steps.size();
        effects_.clear();
        sub_on_ = false;
    }
    // A part's end: ours (`run`, with the unported calls' effects as hooks) against the original's.
    template <class Run> void finish(Ee& ee, Stats& s, Run run) {
        part_ = Part::none;
        std::vector<ex::RaceEvent> events;
        ex::RaceFlow flow = flow_;
        size_t next = 0;
        std::string extra = in_bad_;
        auto apply = [&](ex::Car& c, Group g) {
            const int j = int(&c - cars_.data());
            for (; next < effects_.size() && effects_[next].group == g; ++next) {
                const Effect& f = effects_[next];
                if (f.car != j && extra.empty()) extra = rh::fmt("an unported call on car %d, ours on car %d", f.car, j);
                ce::copy_changed(c, f.pre, f.post);
                events.insert(events.end(), f.events.begin(), f.events.end());
                if (ce::first_difference(f.pre, f.post) != "")
                    ++g_cover[g == Group::pit        ? "race flow: the pit lane changed the car"
                              : g == Group::finish   ? "race flow: the finish's AI calls"
                              : g == Group::handback ? "race flow: a hand-back to the player"
                              : g == Group::drive    ? "race flow: the free run's drive-on"
                              : g == Group::stop     ? "race flow: the pod race's stop driver"
                                                     : "race flow: the AI took a pad car over"];
            }
        };
        ex::RaceFlowHooks hooks;
        hooks.pit_lane = [&](ex::Car& c, int, ex::F, ex::F) { apply(c, Group::pit); };
        hooks.finish_drive = [&](ex::Car& c, int, int) { apply(c, Group::finish); };
        hooks.rolling_handback = [&](ex::Car& c) { apply(c, Group::handback); };
        hooks.ai_takeover = [&](ex::Car& c, int) { apply(c, Group::takeover); };
        hooks.drive_on = [&](ex::Car& c, int) { apply(c, Group::drive); };
        hooks.stop_drive = [&](ex::Car& c, int) { apply(c, Group::stop); };
        size_t ghost_next = ghost0_;
        hooks.ghost_step = [&] {
            if (ghost_next < g_ghost_steps.size()) apply_ghost_step(g_ghost_steps[ghost_next++], cars_, &events, nullptr);
        };
        try {
            run(cars_, flow, events, hooks);
        } catch (const std::exception& x) {
            if (extra.empty()) extra = x.what();
        }
        if (extra.empty() && next != effects_.size()) extra = rh::fmt("%zu of the original's unported calls not made", effects_.size() - next);
        if (extra.empty() && ghost_next != g_ghost_steps.size()) extra = rh::fmt("%zu ghost steps vs %zu", ghost_next - ghost0_, g_ghost_steps.size() - ghost0_);
        const ex::RaceFlow theirs = read_flow(ee, cond_);
        if (extra.empty()) extra = flow_differ(ee, cond_, flow);
        if (extra.empty()) extra = queue_differ(ee, info(ee), queue_, events);
        if (extra.empty()) extra = recorder_differ(ee, info(ee), rec0_, flow.recorder);
        ++s.calls;
        bool changed = false;
        std::string d;
        for (int k = 0; k < n_; ++k) {
            if (raws_[size_t(k)].empty()) continue;
            const std::vector<u8> post = ce::snapshot(ee, car_at(k));
            if (post != raws_[size_t(k)]) changed = true;
            for (u32 off : ce::unmirrored_changes(raws_[size_t(k)], post)) s.unmirrored.insert(off);
            if (d.empty())
                if (std::string dk = ce::first_difference(cars_[size_t(k)], ce::read_car(ee, car_at(k))); !dk.empty()) d = rh::fmt("car %d: ", k) + dk;
        }
        if (changed || theirs.clock != flow_.clock) ++s.active;
        if (d.empty()) d = extra;
        if (!d.empty() && ++s.bad == 1) {
            s.first = *scenario + (part_name(s) + ": ") + d;
            if (std::getenv("GT3R_TICK_DEBUG")) s.first += rh::fmt(" [car %d ghost %d control %d flow %d rule %d]", i_, cars_[size_t(i_)].ghost, cars_[size_t(i_)].control, in_.flow, int(in_.behaviour.rule(sim::Rule::SectorTime)));
            if (std::getenv("GT3R_TICK_DEBUG")) for (const Effect& f : effects_) s.first += rh::fmt(" [effect group %d car %d: %s]", int(f.group), f.car, ce::first_difference(f.pre, f.post).c_str());
        }
    }
    std::string part_name(const Stats& s) const { return &s == &car ? rh::fmt(" (car %d)", i_) : std::string(); }
    // The race flow's inputs (flow_inputs), the race's settings checked against the built ones.
    ex::RaceFlowInputs inputs(Ee& ee) {
        const ti::Built& b = ti::built();
        ex::RaceFlowInputs r = flow_inputs(ee, cond_, &ghost_, &lap_);
        r.course = *course;
        r.split = ee::F(*split);
        in_bad_.clear();
        if (r.config[0] != b.cfg[0] || r.config[2] != b.cfg[2]) in_bad_ = "race configuration +0 / +2 differ from the built";
        // (Rule::LicenceStop: PAL's 1.5 km/h; 2002 Tokyo-Geneva's own constants)
        if (ee.r32(pa(0x003505A0)) != (program() == Program::Pal                 ? 0x3ED55556u
                                       : program() == Program::ConceptGenevaEu   ? 0x3ED55554u
                                       : program() == Program::ConceptGenevaAsia ? 0x3E8E38E3u
                                                                                 : 0x3E8E38E4u))
            in_bad_ = rh::fmt("the licence's stop speed [0x3505A0] is 0x%08X", ee.r32(pa(0x003505A0)));
        for (u32 a : {0x00350A10u, 0x00350A14u, 0x00350A18u})
            if (const u32 w = ee.r32(pa(a)); w != (a == 0x00350A18u ? 0x3F1F136Au : 0x40666666u) && in_bad_.empty())
                in_bad_ = rh::fmt("the speed unit factor [0x%06X] is 0x%08X", a, w);
        if (part_ != Part::direct && (r.logger != (g_run.logger != 0) || r.logger_record != (g_run.logger_record != 0)) && in_bad_.empty())
            in_bad_ = "race info + 0x70 / + 0x74 differ from the race's";
        return r;
    }
    // The queue's entries from `from` on.
    std::vector<ex::RaceEvent> queue_events(Ee& ee, u32 from) const {
        std::vector<ex::RaceEvent> out;
        const u32 q = info(ee) + 0x84;
        for (u32 k = from; k < queue_count(ee); ++k)
            out.push_back(queue_event(ntsc_u_event(s32(s16(ee.r16(q + 8 * k)))), s32(s16(ee.r16(q + 8 * k + 2))), ee.r32(q + 8 * k + 4)));
        return out;
    }
};

} // namespace

DIFF_TEST_ALL(race_tick_exact, "Vehicle tick phases of the race step 0x1F39D8 (two cars, pad input, contact, walls) vs sim::exact, bit for bit") {
    const int ticks = int(rh::env_num("GT3R_CARHIT_TICKS", rh::kSettle + 150));
    const int only = int(rh::env_num("GT3R_CARHIT_ONLY", -1));
    const char* filter = std::getenv("GT3R_TICK_ONLY");
    rh::HitSetup hs;
    rh::g_race_display = true;
    if (!rh::load_hit_setup(ctx, hs)) return;
    // A later build's places inside routines that the version table does not have: their probes are left out and listed.
    struct Places {
        DiffContext& ctx;
        LenientPlaces lenient;
        ~Places() {
            if (const std::string m = lenient.missing(); !m.empty()) ctx.note("places not in the disc's program (their checks left out): " + m);
        }
    } places{ctx, {}};
    // Ports whose function or call site the disc's program does not have (another version's code) are left out and listed.
    std::string absent;
    auto in_program = [&](const char* name, std::initializer_list<Pa> addrs) {
        for (const Pa& a : addrs)
            if (a.ntsc_u && !has_pa(a.ntsc_u)) {
                absent += std::string(absent.empty() ? "" : "; ") + name;
                return false;
            }
        return true;
    };
    std::vector<const Port*> ports;
    for (const Port& p : kPorts) {
        if (filter && std::string(p.name).find(filter) == std::string::npos) continue;
        if (later_key(p.fn.ntsc_u) && !sim::concept_program(program())) continue; // (a later build's own routine)
        if (in_program(p.name, {p.fn, p.site, p.taps[0], p.taps[1]})) ports.push_back(&p);
    }
    std::vector<const RacePort*> race_ports;
    for (const RacePort& p : kRacePorts)
        if ((!filter || std::string(p.name).find(filter) != std::string::npos) && in_program(p.name, {p.fn, p.site}))
            race_ports.push_back(&p);
    if (!absent.empty()) ctx.note("ports not in the disc's program (" + std::string(sim::exact::program_name(program())) + "): " + absent);
    std::vector<Stats> stats(ports.size()), race_stats(race_ports.size());
    struct Pending {
        bool on = false;
        Call call;
        ex::Car pre;
        std::vector<u8> raw;
    };
    struct RacePending {
        bool on = false;
        Call call;
        std::vector<ex::Car> pre;
        std::vector<std::vector<u8>> raw;
    };
    std::vector<Pending> pending(ports.size());
    std::vector<RacePending> race_pending(race_ports.size());
    std::string scenario, start_bad;
    long start_checks = 0;
    const ex::ProgramData& prog = ce::program_data(*ctx.disc);
    // Our ground query on the course file of the race being run.
    std::unique_ptr<ex::CourseGround> ground;
    std::unique_ptr<ex::CourseQuery> query; // the race flow's progress query and lines
    float lap_length = 0, split = 0;        // the road's +0x18 / +0x1C
    // With it the race's AI course (ai_course_setup on the file's AI lines, Built::ai): the course-out stretch, the pit box headings.
    auto make_ground = [&](ByteSpan file) {
        const course::CourseFile cf = course::CourseFile::parse(file);
        ByteSpan rs = cf.section(file, 28);
        const course::Road road = rs.empty() ? course::Road{} : course::Road::parse(rs);
        lap_length = road.length, split = road.start_distance;
        ground = rs.empty() ? nullptr : std::make_unique<ex::CourseGround>(road);
        // our AI course object for the file (the AI ports use it)
        g_ai_built = std::make_unique<start_setup::Course>();
        std::string err;
        if (!start_setup::load_course(file, prog, *g_ai_built, err)) {
            if (g_ai_built_err.empty()) g_ai_built_err = err;
            g_ai_built.reset();
        }
        query = rs.empty() ? nullptr : std::make_unique<ex::CourseQuery>(road);
        ti::built().ai = {};
        if (query) {
            const ByteSpan ls = cf.section(file, 31);
            const course::AiLines lines = ls.empty() ? course::AiLines{} : course::AiLines::parse(ls);
            ti::built().ai = ex::ai_course_setup(*query, ls.empty() ? nullptr : &lines, prog);
        }
        g_query = query.get();
    };
    g_ai_scenario = &scenario;
    make_ground(hs.course_file);
    // A call's inputs at the function's entry.
    auto start = [&](Call& c, Ee& ee, u32 car) {
        c.ee = &ee;
        c.car = car;
        for (int i = 0; i < 4; ++i) c.a[i] = ee.a(i);
        for (int i = 0; i < 8; ++i) c.f[i] = ee.fpr[size_t(12 + i)];
        for (int i = 0; i < 4; ++i) c.t[i] = ee.g64(8 + i);
        c.sp = u32(ee.gpr[29].lo);
        c.vr = ee.vr;
        c.tap = {};
        c.tapped = {};
        c.vary = false;
        c.course = ground.get();
        c.out.clear();
    };
    std::vector<std::vector<u8>> ai_hit(std::size(kAiCode)); // the AI's code the races executed (kAiCode)
    bool ai_cover = false;
    for (const Port* p : ports) ai_cover = ai_cover || (p->fn.ntsc_u == 0x001E35D8 || p->fn.ntsc_u == 0x001E4D38 || p->fn.ntsc_u == 0x001EDB40);
    std::vector<PlanCall> tape; // this race step's plan calls
    constexpr size_t kNone = ~size_t(0);
    std::vector<size_t> tape_open; // per plan site: the tape entry of its call in progress
    StepCheck step;
    step.scenario = &scenario;
    step.tape = &tape;
    const ex::CourseGround* ground_ptr = nullptr;
    step.ground = &ground_ptr;
    const bool check_step = !filter || std::string("race step").find(filter) != std::string::npos;
    FlowCheck flow;
    flow.scenario = &scenario, flow.lap = &lap_length;
    RaceFlowCheck race_flow;
    const ex::CourseQuery* query_ptr = nullptr;
    race_flow.scenario = &scenario, race_flow.course = &query_ptr, race_flow.split = &split, race_flow.lap = &lap_length;
    step.query = &query_ptr; // (the "#sparks" races' emission: the road under the car)
    const bool check_flow = !filter || std::string("race flow").find(filter) != std::string::npos;
    const bool check_course_out = !filter || std::string("phase 12 course_out").find(filter) != std::string::npos;
    Stats course_out_direct, gaps_direct, rank_direct;
    rh::g_install = [&](Ee& e) {
        // The free run ghost's extra step: the depth up first (every probe after sees it), down last (end of g_install).
        g_nested = 0, g_ghost_steps.clear();
        e.add_probe(pa(0x001F14A4), [](Ee& ee) {
            ++g_nested;
            const u32 cond = sim::concept_program(program()) ? g_step_cond : ee.g32(18), info = ee.r32(cond); // $s2: the conductor (the ghost car's + 4)
            GhostStep g;
            const int n = int(ee.r32(cond + race_setup::kK8 + k_off(0x6384)));
            for (int i = 0; i < n; ++i) g.pre.push_back(ce::read_car(ee, race_setup::car_addr(cond, i)));
            g.q0 = ee.r32(info + 0x84 + queue_count_off()), g.p0 = g_pit_events.size();
            g_ghost_steps.push_back(std::move(g));
        });
        e.call(pa(0x00245D78), {Arg::flt(0.1f)});
        g_ai_courses.clear(); // a new race: its course setup has run
        ground_ptr = ground.get();
        query_ptr = query.get();
        g_split = split;
        if (check_step) step.install(e);
        if (ai_cover)
            for (size_t r = 0; r < std::size(kAiCode); ++r) {
                ai_hit[r].resize((kAiCode[r].second - kAiCode[r].first) / 4);
                for (u32 a = kAiCode[r].first; a < kAiCode[r].second; a += 4)
                    e.add_probe(a, [&, r, a](Ee&) { ai_hit[r][(a - kAiCode[r].first) / 4] = 1; });
            }
        if (sim::concept_program(program()))
            for (const Pa fn : kPhaseFns) e.add_probe(fn, [](Ee& ee) { g_phase_car = u32(ee.a(0)); });
        // The AI's plan calls (kPlanSites) onto the tape, which every race step starts empty.
        tape_open.assign(std::size(kPlanSites), kNone);
        e.add_probe(race_setup::kCondStep, [&](Ee& ee) {
            g_step_cond = u32(ee.a(0));
            if (u32(ee.a(1)) != 0) return;
            tape.clear(), g_pit_events.clear(), g_ghost_steps.clear();
            // the race display takes the race's event queue every frame: empty at each step's start
            const u32 q = ee.r32(u32(ee.a(0))) + 0x84;
            for (u32 k = 0; k < queue_count_off() + 4 - 0x800; k += 4) ee.w32(q + 0x800 + k, 0);
            logger_frames(ee);
        });
        e.add_probe(pa(0x001ED9F0), [&](Ee& ee) {
            if (const u32 ra = u32(ee.gpr[31].lo); ra == pa(0x001EDE4C) || ra == pa(0x001EE1AC)) g_pit_events.push_back({1, s32(ee.a(1))});
        });
        e.add_probe(pa(0x001EDA40), [&](Ee& ee) {
            if (u32(ee.gpr[31].lo) == pa(0x001EE0AC)) g_pit_events.push_back({6, s32(ee.a(1))});
        });
        for (size_t s = 0; s < std::size(kPlanSites); ++s) {
            e.add_probe(kPlanSites[s].fn, [&, s](Ee& ee) {
                const PlanSite& ps = kPlanSites[s];
                if (u32(ee.gpr[31].lo) != ps.ra) return;
                PlanCall c;
                c.kind = ps.kind, c.ra = ps.ra;
                for (int i = 0; i < 4; ++i) c.a[i] = ee.a(i);
                c.f12 = ee.fpr[12], c.f13 = ee.fpr[13];
                c.t0 = u32(ee.g64(8)), c.t1 = u32(ee.g64(9));
                if (ps.kind == kPlanBraking || ps.kind == kPlanSpeed || ps.kind == kPlanRequest) {
                    c.car = u32(c.a[0]) - 0xEC8;
                    c.pre = ce::read_car(ee, c.car);
                }
                tape_open[s] = tape.size();
                tape.push_back(std::move(c));
            });
            e.add_probe(kPlanSites[s].ra, [&, s](Ee& ee) {
                if (tape_open[s] == kNone) return;
                PlanCall& c = tape[tape_open[s]];
                tape_open[s] = kNone;
                c.v0 = u32(ee.gpr[2].lo), c.f0 = ee.fpr[0];
                if (c.car) c.post = ce::read_car(ee, c.car);
                if (c.kind == kPlanTarget) c.out_x = ee.r32(c.t0), c.out_y = ee.r32(c.t1);
                if (c.kind == kPlanBraking && sim::concept_program(program())) c.zone = true, c.zone_post = read_zone(ee, g_step_cond);
            });
        }
        if (check_flow) flow.install(e), race_flow.install(e);
        // The race's built inputs (its event, course and conductor; the pristine tables), and the check of all of them at
        // race start.
        const u32 cond = g_run.cond ? g_run.cond : rh::g_car[0] - race_setup::kCarBase;
        if (check_step) step.install_leaves(e, race_setup::car_addr(cond, 0)); // (a later build: its leaves)
        const data::Event& run_ev = g_run.event ? *g_run.event : *hs.ev;
        ti::built().reset(prog, g_run.kind, run_ev, ti::course_category(hs.db, run_ev), g_run.battle_wear, g_run.battle_catchup);
        ++start_checks;
        if (const std::string d = ti::check_race_start(e, cond, int(e.r32(cond + race_setup::kK8 + k_off(0x6384)))); !d.empty() && start_bad.empty())
            start_bad = scenario + ": " + d;
        std::map<u32, std::vector<size_t>> by_fn;
        for (size_t k = 0; k < ports.size(); ++k) by_fn[ports[k]->fn].push_back(k);
        for (auto& [fn, list] : by_fn)
            e.add_probe(fn, [&, list](Ee& ee) {
                for (size_t k : list) {
                    if (u32(ee.gpr[31].lo) != ports[k]->site + 8) continue;
                    Pending& p = pending[k];
                    p.on = true;
                    start(p.call, ee, ports[k]->car.of(ee));
                    if (sim::concept_program(program())) p.call.zone = read_zone(ee, g_step_cond);
                    p.call.tape = &tape, p.call.tape0 = p.call.tape1 = tape.size();
                    p.call.pev0 = p.call.pev1 = g_pit_events.size();
                    p.pre = ce::read_car(ee, p.call.car);
                    p.raw = ce::snapshot(ee, p.call.car);
                }
            });
        for (size_t k = 0; k < ports.size(); ++k) {
            for (size_t t = 0; t < ports[k]->taps.size(); ++t)
                if (ports[k]->taps[t])
                    e.add_probe(ports[k]->taps[t], [&, k, t](Ee& ee) {
                        if (pending[k].on) pending[k].call.tap[t] = ee.gpr[2].lo, pending[k].call.tapped[t] = true;
                    });
            e.add_probe(ports[k]->site + 8, [&, k](Ee& ee) {
                Pending& p = pending[k];
                if (!p.on) return;
                p.on = false;
                p.call.tape1 = std::max(p.call.tape0, tape.size());
                p.call.pev1 = std::max(p.call.pev0, g_pit_events.size());
                if (p.call.tape0 > tape.size()) p.call.tape0 = p.call.tape1 = 0, p.call.differ("plan tape cleared during the call");
                Stats& s = stats[k];
                if (ports[k]->only && !ports[k]->only(p.pre, p.call)) {
                    ++s.skipped;
                    return;
                }
                ++s.calls;
                ex::Car ours = p.pre;
                try {
                    ports[k]->run(ours, p.call);
                } catch (const EeTrap& t) {
                    // A later build: an input the port reads from the original's memory at a place the version table
                    // does not have right (a global another compiler laid out elsewhere). The call counts as differing.
                    if (!sim::concept_program(program())) throw;
                    p.call.differ(std::string("an input of the port is not readable on this build: ") + t.what());
                }
                const ex::Car theirs = ce::read_car(ee, p.call.car);
                std::string d = ce::first_difference(ours, theirs);
                if (d.empty()) d = p.call.out;
                if (!d.empty() && ++s.bad == 1) s.first = scenario + ": " + d;
                const std::vector<u8> post = ce::snapshot(ee, p.call.car);
                if (post != p.raw || theirs.later.ratio.bits != p.pre.later.ratio.bits || theirs.later.special != p.pre.later.special) ++s.active;
                for (u32 off : ce::unmirrored_changes(p.raw, post)) s.unmirrored.insert(off);
                ctx.sample();
            });
        }
        for (size_t k = 0; k < race_ports.size(); ++k) {
            e.add_probe(race_ports[k]->fn, [&, k](Ee& ee) {
                if (u32(ee.gpr[31].lo) != race_ports[k]->site + 8) return;
                RacePending& p = race_pending[k];
                p.on = true;
                start(p.call, ee, u32(ee.a(0)));
                const u32 cond = p.call.car;
                const int n = int(ee.r32(cond + race_setup::kK8 + k_off(0x6384)));
                p.pre.clear(), p.raw.clear();
                for (int i = 0; i < n; ++i) {
                    p.pre.push_back(ce::read_car(ee, race_setup::car_addr(cond, i)));
                    p.raw.push_back(ce::snapshot(ee, race_setup::car_addr(cond, i)));
                }
                p.call.cond.resize(race_ports[k]->keep_size);
                ee.read_bytes(cond + race_ports[k]->keep, p.call.cond.data(), race_ports[k]->keep_size);
            });
            e.add_probe(race_ports[k]->site + 8, [&, k](Ee& ee) {
                RacePending& p = race_pending[k];
                if (!p.on) return;
                p.on = false;
                Stats& s = race_stats[k];
                ++s.calls;
                std::vector<ex::Car> ours = p.pre;
                race_ports[k]->run(ours, p.call);
                std::string d;
                bool active = false;
                for (size_t i = 0; i < ours.size(); ++i) {
                    const u32 car = race_setup::car_addr(p.call.car, int(i));
                    const std::string di = ce::first_difference(ours[i], ce::read_car(ee, car));
                    if (d.empty() && !di.empty()) d = rh::fmt("car %zu: ", i) + di;
                    const std::vector<u8> post = ce::snapshot(ee, car);
                    active = active || post != p.raw[i];
                    for (u32 off : ce::unmirrored_changes(p.raw[i], post)) s.unmirrored.insert(off);
                }
                std::vector<u8> cond(race_ports[k]->keep_size);
                ee.read_bytes(p.call.car + race_ports[k]->keep, cond.data(), race_ports[k]->keep_size);
                active = active || cond != p.call.cond;
                if (d.empty()) d = p.call.out;
                if (!d.empty() && ++s.bad == 1) s.first = scenario + ": " + d;
                if (active) ++s.active;
                ctx.sample();
            });
        }
        e.add_probe(pa(0x001F14AC), [](Ee& ee) {
            if (g_ghost_steps.empty()) return;
            GhostStep& g = g_ghost_steps.back();
            const u32 cond = sim::concept_program(program()) ? g_step_cond : ee.g32(18), info = ee.r32(cond); // $s2: the conductor
            for (size_t i = 0; i < g.pre.size(); ++i) g.post.push_back(ce::read_car(ee, race_setup::car_addr(cond, int(i))));
            const u32 q = info + 0x84, to = ee.r32(q + queue_count_off());
            for (u32 k = g.q0; k < to; ++k) g.events.push_back(queue_event(ntsc_u_event(s16(ee.r16(q + 8 * k))), s16(ee.r16(q + 8 * k + 2)), ee.r32(q + 8 * k + 4)));
            g.pit_events.assign(g_pit_events.begin() + ptrdiff_t(std::min(g.p0, g_pit_events.size())), g_pit_events.end());
            --g_nested;
        });
    };
    auto run = [&](const rh::Scenario& sc, const rh::Lane& lane) {
        rh::Extents ext;
        rh::Run o = rh::run_original(ctx.ee, hs.db, hs.gtar, *hs.ev, hs.course_file, hs.car, *hs.trk, lane, sc, ticks, ext);
        if (!o.ok) ctx.check(false, scenario + ": " + o.error);
    };
    int idx = 0;
    for (const rh::Scenario& sc : rh::car_hit_scenarios()) {
        const int si = idx++;
        if ((only >= 0 && si != only) || (sc.wall && !hs.wl.ok)) continue;
        scenario = rh::fmt("%d %s", si, sc.name);
        run(sc, sc.wall ? hs.wl : hs.ln);
    }
    for (const rh::Scenario& sc : rh::tick_scenarios()) {
        const int si = idx++;
        if (only >= 0 && si != only) continue;
        scenario = rh::fmt("%d %s", si, sc.name);
        run(sc, hs.ln);
    }
    for (const rh::Scenario& sc : rh::pad_scenarios()) { // varying pad input
        const int si = idx++;
        if (only >= 0 && si != only) continue;
        scenario = rh::fmt("%d %s", si, sc.name);
        run(sc, hs.ln);
    }
    for (const rh::Scenario& sc : placed_drives()) {
        if (only >= 0) break;
        scenario = sc.name;
        run(sc, hs.ln);
    }
    if (rh::env_num("GT3R_CARHIT_SWEEP", 0) != 0 && hs.wl.ok)
        for (double deg : {3.0, 5.0, 7.0, 9.0, 12.0})
            for (double along : {-3.0, -1.5, 0.0, 1.5, 3.0})
                for (double kmh : {80.0, 110.0}) {
                    scenario = rh::fmt("sweep %.0f deg, %.1f m, %.0f km/h", deg, along, kmh);
                    run(rh::Scenario{"sweep", -1.0, deg, kmh, kmh, along, 0.555, 0.555, true}, hs.wl);
                }
    // Both cars rolling back at 30 km/h, side by side, on full throttle: the rims spin up forwards while the hubs still
    // move backwards (slip ratios below -1).
    if (only < 0) {
        scenario = "rolling back on full throttle";
        run(rh::Scenario{"rolling back", 3.0, 0, -30, -30, 0.0, 1.0, 1.0}, hs.ln);
    }
    // Pad manoeuvres (GT3R_TICK_MANOEUVRES=0 skips them): the two cars side by side at 60 km/h with scripted pads, for what
    // the contact races and the CPU races don't do. Car 0 brakes to a stop and holds the brake, reverses, swings round at full
    // left lock in reverse and then on the handbrake, and brakes. Car 1 locks its rear wheels on the handbrake, stops on the
    // brake, is held at rest with the brake off by driver kind 8 (as the stop driver leaves a car, physics §9.4) and then by
    // the spawn counter (120, as the free-practice pit start sets it), then reverses and swings round at full right lock.
    if (rh::env_num("GT3R_TICK_MANOEUVRES", 1) != 0 && only < 0) {
        struct Pad {
            int from; // the first tick of this frame
            s16 steer;
            u16 throttle, brake, back, hand; // +8, +0xA, +0xC (the reverse axis), +0xE (handbrake)
        };
        static const Pad pads[2][8] = {
            {{0, 0, 1229, 0, 0, 0}, {60, 0, 0, 4096, 0, 0}, {220, 0, 0, 0, 4096, 0}, {400, 4096, 0, 0, 4096, 0}, {460, 4096, 0, 0, 0, 4096},
             {520, 0, 0, 4096, 0, 0}, {-1, 0, 0, 0, 0, 0}, {-1, 0, 0, 0, 0, 0}},
            {{0, 0, 1229, 0, 0, 0}, {30, 0, 0, 0, 0, 4096}, {70, 0, 0, 4096, 0, 0}, {200, 0, 0, 0, 0, 0}, {420, 0, 0, 0, 4096, 0},
             {600, -4096, 0, 0, 4096, 0}, {660, -4096, 0, 0, 0, 4096}, {720, 0, 0, 4096, 0, 0}},
        };
        rh::g_after_tick = [&](Ee& e, int k) {
            const int n = k + 1; // the tick the frames are for
            for (int i = 0; i < 2; ++i)
                for (const Pad& p : pads[i])
                    if (p.from == n) {
                        u8* f = rh::g_frame[i];
                        std::memcpy(f + 4, &p.steer, 2), std::memcpy(f + 8, &p.throttle, 2), std::memcpy(f + 0xA, &p.brake, 2);
                        std::memcpy(f + 0xC, &p.back, 2), std::memcpy(f + 0xE, &p.hand, 2);
                    }
            const u32 F1 = rh::g_car[1] + ce::kF;
            if (n == 200) e.w8(F1 + f_off(0x534), 8);
            if (n == 300) e.w8(F1 + f_off(0x534), 0), e.w16(F1 + f_off(0x536), 120);
            return true;
        };
        scenario = "pad manoeuvres";
        rh::Extents ext;
        rh::Run o = rh::run_original(ctx.ee, hs.db, hs.gtar, *hs.ev, hs.course_file, hs.car, *hs.trk, hs.ln,
                                     rh::Scenario{"manoeuvres", 1.0, 0, 60, 60, 0.0, 0.3, 0.3}, 780, ext);
        if (!o.ok) ctx.check(false, scenario + ": " + o.error);
        rh::g_after_tick = nullptr;
    }
    // The longitudinal force's config bytes (0x2D2936 / 7; 1 in the shipped data) at 2, which makes the car's drive and brake
    // forces the gained ones: one contact race, for 0x1E7450's other branch.
    if (only < 0) {
        const auto install = rh::g_install;
        rh::g_install = [&](Ee& e) {
            install(e);
            ti::built().poke(e, 0x002D2936, 2), ti::built().poke(e, 0x002D2937, 2);
        };
        scenario = "0 with the force config bytes at 2";
        run(rh::car_hit_scenarios()[0], hs.ln);
        rh::g_install = install;
    }
    // A pad run down a pit lane (triangles with flags bit 0, which the ground contact counts and the races do not reach):
    // Grand Valley's, two cars in a row at 60 km/h. GT3R_TICK_PIT=0 leaves it out.
    if (only < 0 && rh::env_num("GT3R_TICK_PIT", 1) != 0) {
        const data::Event* ev = hs.db.event("Ggtworld_n_1010");
        Volume& vol = ctx.disc->volume();
        // (a database without GT3's events, the 2002 builds of Gran Turismo Concept: the first of its events whose course
        // has a pit lane)
        if (!ev)
            for (const data::Event& e : hs.db.events()) {
                const Volume::Entry* c = e.entries.empty() ? nullptr : vol.find(course_path(vol, e.course));
                if (!c) continue;
                const Bytes f = vol.read(*c);
                make_ground(f);
                if (pit_lane(course::Road::parse(course::CourseFile::parse(f).section(f, 28)), *ground).ok) {
                    ev = &e;
                    break;
                }
            }
        if (!hs.db.event("Ggtworld_n_1010")) make_ground(hs.course_file);
        const Volume::Entry* ve = ev ? vol.find(course_path(vol, ev->course)) : nullptr;
        if (!ve) ctx.check(false, "pit lane: no Grand Valley event or course");
        else {
            const Bytes file = ctx.disc->volume().read(*ve);
            make_ground(file);
            const rh::Lane lane = pit_lane(course::Road::parse(course::CourseFile::parse(file).section(file, 28)), *ground);
            std::unique_ptr<track::Track> trk = track::Track::from_course_file(file, ev->course);
            scenario = "pit lane";
            rh::Extents ext;
            rh::Run o;
            g_run = {0, ev, ex::Conductor::SinglePlayer};
            if (lane.ok && trk)
                o = rh::run_original(ctx.ee, hs.db, hs.gtar, *ev, file, hs.car, *trk, lane,
                                     rh::Scenario{"pit lane", 0.0, 0, 60, 60, std::nan(""), 0.3, 0.3}, ticks, ext);
            g_run = {};
            ctx.check(o.ok, scenario + ": " + (lane.ok ? o.error : std::string("no pit lane found")));
            make_ground(hs.course_file);
        }
    }
    // Whole CPU races, each after a start hold of GT3R_TICK_HOLD frames (default 60; the cars held on the grid).
    const char* default_events =
        "Gsunday_e_0003,Gevolution_e_0001,Ggtworld_n_1010,Dr_tahitimaze_0001,Gsunday_e_0002,Gspider_e_0001,G_4wd_e_0001,demo_wet,Dr_swiss_0001,"
        "Ggtworld_h_1004/worn,Dr_smokymt_0001+pad,demo_wet+pad,Gturbo_h_0001,"
        "AT020001@tt#runup,AT020001@tt#ai,L0A0001@licence#runup,AT040001@freerun,M400@machine,M400@machine#ai,"
        "Gsunday_e_0003@battle2p#finish,Gsunday_e_0003@battlemp,Gsunday_e_0002@practice#ai,Gsunday_e_0002#band1,"
        "Gspider_e_0001#band2,G_4wd_e_0001#flow,Gsunday_e_0003#finish,Gsunday_e_0002+pad#reverse,Gspider_e_0001+pad#rolling,"
        "Gsunday_e_0001~L_seattle1,AT040001@freerun#ai,AT040001@freerun#follow,AT020001@tt#runup#follow#logger,"
        "AT020001@tt#runup#follow#logger0,L0A0001@licence#follow#logger,M400@machine#follow#logger,AT040001@freerun#follow#logger";
    // (GT3R_TICK_EVENTS="+<races>": the default races and these after them)
    const char* events_env = std::getenv("GT3R_TICK_EVENTS");
    const bool more_events = events_env && events_env[0] == '+';
    std::string events = events_env && !more_events ? events_env : default_events;
    // A database without GT3's events (the 2002 builds of Gran Turismo Concept): its own first races on six courses.
    if ((!events_env || more_events) && !hs.db.event("Gsunday_e_0003")) {
        events.clear();
        std::set<std::string> courses;
        for (const data::Event& e : hs.db.events()) {
            if (e.entries.size() < 2 || !ctx.disc->volume().find(course_path(ctx.disc->volume(), e.course))) continue;
            if (courses.size() < 6 && courses.insert(e.course).second) events += (events.empty() ? "" : ",") + e.label;
        }
        if (!events.empty()) events += "," + events.substr(0, events.find(',')) + "+pad";
        ctx.note("the database has none of GT3's events; CPU races: " + events);
    }
    if (more_events) events += (events.empty() ? "" : ",") + std::string(events_env + 1);
    const int ai_ticks = int(rh::env_num("GT3R_TICK_AI_SECONDS", 40) * 60);
    const int worn_ticks = int(rh::env_num("GT3R_TICK_WORN_SECONDS", 120) * 60);
    const int finish_ticks = int(rh::env_num("GT3R_TICK_FINISH_SECONDS", 150) * 60);
    const int follow_ticks = int(rh::env_num("GT3R_TICK_FOLLOW_SECONDS", 240) * 60);
    const int start_hold = int(rh::env_num("GT3R_TICK_HOLD", 90));
    int race_index = 0;
    // (A later build: the direct calls every kVaryEvery ticks are off. Their arguments, memory pokes and virtual slots
    // are GT3's routines'; another build's routine may take others.)
    // (a later build's: the variants' pokes and direct calls are GT3's layout's; GT3R_TICK_VARY=2 runs them there too)
    const bool vary = rh::env_num("GT3R_TICK_VARY", 1) != 0 && (!sim::concept_program(program()) || rh::env_num("GT3R_TICK_VARY", 1) == 2);
    if (!vary && sim::concept_program(program())) ctx.note("variants and direct calls (every 30 ticks of the CPU races): not run on a later build");
    // The CPU races also start from our start records (start_setup.h): compared with the original's after the initialisation,
    // then our phases carried on our own results for the start hold and GT3R_TICK_FROM_START steps more (default 60; 0 off).
    const int from_start = int(rh::env_num("GT3R_TICK_FROM_START", 60));
    Stats start_records;
    long missed_start_wheels = 0; // wheels starting off the road (their ground records' stack words taken from the original's)
    // The seeded race (a GT mode race with tyre wear): start states racing would not reach, written to the original's records
    // before the first tick: the engine condition counters in every segment of the condition curve, tyre wear in every segment
    // of the wear curve, the HUD message timer F +0x51D running (and at 0xFF, which stays) and the spawn hold F +0x536.
    const std::string seeded = "Gturbo_h_0001";
    auto seed = [&](const race_setup::OrigRace& race) {
        const float condition[6][2] = {{-400000, -200000}, {-150000, -50000}, {100000, 50000}, {550000, -99999}, {700000, 300000}, {0, 0}};
        const float wear[3][4] = {{-30000, -20000, -5000, 0}, {20000, 40000, 100000, 150000}, {180000, 209000, 210000, 250000}};
        for (int i = 0; i < race.ncars && i < 6; ++i) {
            const u32 C = race_setup::car_addr(race.cond, i), F = C + ce::kF;
            ctx.ee.wf(C + car_off(0x1380), condition[i][0]);
            ctx.ee.wf(C + car_off(0x1384), condition[i][1]);
            for (u32 w = 0; i < 3 && w < 4; ++w) ctx.ee.wf(C + ce::kWheel0 + ce::kWheelSize * w + 0xA0, wear[i][w]);
            if (i < 2) ctx.ee.w8(F + f_off(0x51D), i == 0 ? 45 : 0xFF);
            if (i == 2) ctx.ee.w16(F + f_off(0x536), 30);
        }
    };
    bool device_busy = false; // a CPU car's input device had a frame for the pad read (+pad races hand the others an empty one)
    for (size_t pos = 0; events != "-" && pos < events.size();) {
        size_t end = events.find(',', pos);
        if (end == std::string::npos) end = events.size();
        std::string label = events.substr(pos, end - pos);
        pos = end + 1;
        // "%<car label>" (any number): cars after the event's own entries (a later build's special cars among others).
        std::vector<std::string> extra_cars;
        for (size_t at; (at = label.rfind('%')) != std::string::npos; label.erase(at)) extra_cars.insert(extra_cars.begin(), label.substr(at + 1));
        // "@mode": the race under another race mode's conductor: tt (time trial), licence, freerun, machine and practice run
        // car 0 alone on the pad; battle2p races two cars, battlemp the event's, with car 0 on the pad.
        ex::Conductor kind = ex::Conductor::SinglePlayer;
        bool alone = false;
        if (const size_t at = label.find('@'); at != std::string::npos) {
            static const std::pair<const char*, ex::Conductor> kModes[] = {
                {"tt", ex::Conductor::TimeTrial},        {"licence", ex::Conductor::License},
                {"freerun", ex::Conductor::FreeRun},     {"battle2p", ex::Conductor::Battle2P},
                {"battlemp", ex::Conductor::BattleMP},   {"machine", ex::Conductor::MachineTest},
                {"practice", ex::Conductor::FreePractice}};
            const size_t to = label.find_first_of("#+/", at + 1);
            const std::string m = label.substr(at + 1, to == std::string::npos ? std::string::npos : to - at - 1);
            label.erase(at, m.size() + 1);
            bool known = false;
            for (const auto& [name, k] : kModes)
                if (m == name) kind = k, known = true;
            if (!known) {
                ctx.check(false, label + ": no race mode " + m);
                continue;
            }
            if (!race_setup::has_conductor(size_t(kind))) {
                ctx.note(label + "@" + m + ": left out, the program has no such conductor");
                continue;
            }
            alone = kind != ex::Conductor::Battle2P && kind != ex::Conductor::BattleMP;
        }
        // "/worn": the cars start on part-worn tyres (car k's wear 150000 + 3000 k, the "tyres worn" grip 0.91 is at about
        // 159600), car 0 runs as control source 1 (the AI-driven human entry) and the last car as a pad car left on the grid
        // (control source 0, a neutral pad frame), so the wear flags, the pit requests, the pit lane and the pit stop happen
        // within the race.
        const bool worn = label.size() > 5 && label.substr(label.size() - 5) == "/worn";
        if (worn) label.resize(label.size() - 5);
        // "#band1" / "#band2": the rubber band's pass in mode 1 / 2 (the configuration's +0x22; every event has 0); "#flow": the
        // race-flow byte set with a time scale of 0.37 (some steps skipped, as a slow replay).
        int band_mode = -1;
        bool flow_gate = false;
        for (const char* tag : {"#band1", "#band2", "#flow"})
            if (const size_t at = label.find(tag); at != std::string::npos) {
                if (tag[1] == 'b') band_mode = tag[5] - '0';
                else flow_gate = true;
                label.erase(at, std::strlen(tag));
            }
        // "#finish": a one-lap race of GT3R_TICK_FINISH_SECONDS (default 150): the cars cross the line, finish and are handed
        // to the AI.
        int laps = 2;
        bool finish = false;
        if (const size_t at = label.find("#finish"); at != std::string::npos) laps = 1, finish = true, label.erase(at, 7);
        // "#rolling": the rolling start's hand-back of car 0 (start type 3, car 0's AI run-up 120 steps); "#reverse": car 0 on the
        // pad goes over the start line, backs over it and goes again (a lap base up and down).
        bool rolling = false, reverse = false;
        if (const size_t at = label.find("#rolling"); at != std::string::npos) rolling = true, label.erase(at, 8);
        if (const size_t at = label.find("#reverse"); at != std::string::npos) reverse = true, label.erase(at, 8);
        // "#ai": car 0 driven by the AI (control source 2) from the start, for GT3R_TICK_FINISH_SECONDS; "#runup": car 0 on the
        // pad with driver kind 1 (the AI drives it) until the time trial's or licence's first crossing hands it back.
        // "#logger": the data logger's recording pass over the run (race info + 0x70 and + 0x74 set, each car with its table);
        // "#logger0": its playback (+ 0x70 only).
        int logger = 0;
        if (const size_t at = label.find("#logger0"); at != std::string::npos) logger = 2, label.erase(at, 8);
        if (const size_t at = label.find("#logger"); at != std::string::npos) logger = 1, label.erase(at, 7);
        bool ai0 = false, runup = false, follow = false, ghost = false;
        // "#ghost" (free run): a second car, the ghost (F +0x3EA, driven by the AI), put back on the best lap's start by the lap
        // handlers once the first lap is done.
        if (const size_t at = label.find("#ghost"); at != std::string::npos) ghost = true, label.erase(at, 6);
        // "#follow": car 0's pad follows the road's centre line (follow_frame), for the length of a "#finish" race; "#pod" the
        // same with the pod race's stops on the odd laps (a later build's pod race: Follower::pod).
        bool pod_stops = false;
        if (const size_t at = label.find("#pod"); at != std::string::npos) follow = pod_stops = true, label.erase(at, 4);
        // "#leaves": the same over a later build's leaf ground (road triangles with flag 0x20: Autumn Ring's verges), so the
        // emitter's leaf calls happen (leaf_line).
        bool leaf_line = false;
        if (const size_t at = label.find("#leaves"); at != std::string::npos) follow = leaf_line = true, label.erase(at, 7);
        // "#sparks": every car with its model's spark points (as the app gives them) and the sparks' frame between the steps
        // (StepCheck::spark_frame: each car's emission and the update, against ours); "#scrape" the same with car 0 on the pad
        // following the race line as "#follow" at GT3R_TICK_SCRAPE_PACE times its pace (default 2.5: flat out but in the
        // tightest bends), so it bottoms out over the crests and kerbs at speed.
        bool spark_race = false, scrape = false;
        if (const size_t at = label.find("#sparks"); at != std::string::npos) spark_race = true, label.erase(at, 7);
        if (const size_t at = label.find("#scrape"); at != std::string::npos) spark_race = scrape = follow = true, label.erase(at, 7);
        if (const size_t at = label.find("#follow"); at != std::string::npos) follow = true, label.erase(at, 7);
        if (const size_t at = label.find("#ai"); at != std::string::npos) ai0 = true, label.erase(at, 3);
        if (const size_t at = label.find("#runup"); at != std::string::npos) runup = true, label.erase(at, 6);
        // "+pad": car 0 on the pad path (rh::pad_race_frame).
        bool pad = label.size() > 4 && label.compare(label.size() - 4, 4, "+pad") == 0;
        if (pad) label.resize(label.size() - 4);
        if (alone || scrape) pad = true;
        // "~<course>": the event's cars and rules on another course file
        std::string course_name;
        if (const size_t at = label.find('~'); at != std::string::npos) course_name = label.substr(at + 1), label.resize(at);
        const data::Event* ev = hs.db.event(label);
        if (ev && course_name.empty()) course_name = ev->course;
        const Volume::Entry* ce = ev ? ctx.disc->volume().find(course_path(ctx.disc->volume(), course_name)) : nullptr;
        if (!ce) {
            ctx.check(false, label + ": no such event or course");
            continue;
        }
        const Bytes course = ctx.disc->volume().read(*ce);
        make_ground(course);
        // The one-player modes keep the event's laps byte (the licence's laps, the machine test's type).
        if (alone && !finish) laps = ex::race_config(ByteSpan(ev->bytes.data(), ev->bytes.size()))[1];
        std::vector<std::string> labels;
        for (const std::string& e : ev->entries)
            if (!e.empty() && e != "-" && labels.size() < (alone ? 1u : kind == ex::Conductor::Battle2P ? 2u : 6u)) labels.push_back(e);
        for (const std::string& e : extra_cars)
            if (labels.size() < 6) labels.push_back(e);
        if (ghost && !labels.empty()) labels.resize(2, labels[0]);
        race_setup::OrigRace race;
        std::string err;
        g_scratch.clear(), g_rec_calls.clear(), g_logger_inputs.clear();
        scenario = "CPU race " + label + (course_name != ev->course ? " on " + course_name : std::string()) + (worn ? " (worn)" : "") +
                   (ai0 ? ", car 0 by the AI" : runup ? ", car 0's run-up" : pad ? ", car 0 on the pad" : "") + (ghost ? ", a ghost car" : "") +
                   (kind != ex::Conductor::SinglePlayer ? std::string(", ") + ex::conductor_name(kind) + " conductor" : std::string()) +
                   (logger == 1 ? ", the data logger recording" : logger == 2 ? ", the data logger playing" : "") +
                   (scrape ? ", sparks, car 0 scraping" : spark_race ? ", sparks" : "");
        // The battles' race info words (+8 the battle's laps, the race's; +0xC tyre wear: its setting 2 when set; +0x14 the catch-up
        // setting 1 or 2), and the free
        // run conductor's race object (race info + 0x3C; its virtual 22 and pad hook ask it for the second input device,
        // RaceSolitaire::vf27 0x187548 with 0x100: none here).
        const s32 battle_wear = kind == ex::Conductor::Battle2P ? 1 : 0;
        const s32 battle_catchup = kind == ex::Conductor::Battle2P ? 1 : kind == ex::Conductor::BattleMP ? 2 : 0;
        auto before_init = [&](Ee& e, const race_setup::OrigRace& o) {
            e.w32(o.info + 0xC, u32(battle_wear)), e.w32(o.info + 0x14, u32(battle_catchup));
            if (kind == ex::Conductor::Battle2P || kind == ex::Conductor::BattleMP) e.w32(o.info + 8, u32(laps));
            if (logger) {
                logger_tables(e, o);
                e.w32(o.info + 0x70, 1), e.w32(o.info + 0x74, logger == 1 ? 1 : 0);
            }
            if (kind != ex::Conductor::FreeRun) return;
            const u32 obj = e.alloc(race_object_size(), 64), vt = e.alloc(0x100);
            e.w32(obj, vt), e.w32(obj + 4, o.info);
            e.w32(vt + 8 * 27 + 4, pa(0x00187548));
            free_run_recorders(e, obj);
            e.w32(o.info + 0x3C, obj);
        };
        if (!race_setup::setup_original_race(ctx.ee, hs.db, hs.gtar, *ev, course, labels, laps, start_hold, race, err, before_init,
                                             race_setup::kConductors[size_t(kind)])) {
            ctx.check(false, scenario + ": " + err);
            continue;
        }
        if (from_start > 0 && check_step) {
            std::string serr;
            const ex::ProgramData* pd = start_setup::program_data(ctx, serr);
            start_setup::Course oc;
            try {
                if (!pd || !start_setup::load_course(course, *pd, oc, serr)) throw std::runtime_error(serr);
                std::vector<ex::Car::Later> later;
                std::vector<ex::Record> mine = start_setup::start_records(ctx.ee, race, hs.db, *ev, labels, oc, *pd, 0, kind, {}, &later);
                std::vector<ex::Car> m(mine.size()), b(mine.size());
                std::string d;
                for (size_t i = 0; i < mine.size(); ++i) {
                    m[i].behaviour = disc_behaviour();
                    ex::load_image(m[i], mine[i].data());
                    m[i].later = later[i];
                    b[i] = ce::read_car(ctx.ee, race_setup::car_addr(race.cond, int(i)));
                    // (a start off the road: the missed ground records' stack words from the original's)
                    if (const int k = ce::take_missed_ground(m[i], b[i])) missed_start_wheels += k, ex::store_image(m[i], mine[i].data());
                    const std::vector<u8> theirs = ce::snapshot(ctx.ee, race_setup::car_addr(race.cond, int(i)));
                    for (u32 o = 0; o < ce::kCarSize && d.empty(); ++o)
                        if (mine[i][o] != theirs[o]) d = rh::fmt("car %zu: record byte +0x%X", i, o);
                    // (a later build's own words of the record, which the image does not hold)
                    if (d.empty())
                        if (const std::string dl = ce::first_difference(m[i], b[i]); !dl.empty()) d = rh::fmt("car %zu: ", i) + dl;
                }
                ++start_records.calls, ++start_records.active;
                if (!d.empty() && ++start_records.bad == 1) start_records.first = scenario + ": " + d;
                step.carry_from(std::move(m), std::move(b), start_hold + from_start);
            } catch (const std::exception& e) {
                ctx.check(false, scenario + ": our start: " + e.what());
            }
        }
        int t = 0;
        if (worn)
            for (int i = 0; i < race.ncars; ++i) {
                const u32 car = race_setup::car_addr(race.cond, i);
                for (u32 w = 0; w < 4; ++w) // W +0xA0 wear
                    ctx.ee.wf(car + ce::kWheel0 + ce::kWheelSize * w + 0xA0, float(150000 + 3000 * i));
                if (i == 0) ctx.ee.w8(car + ce::kF + f_off(0x3E9), 1); // F +0x3E9 control source
                if (i == race.ncars - 1) {
                    ctx.ee.w8(car + ce::kF + f_off(0x3E9), 0);
                    ctx.ee.hle(pa(0x00210740), [](Ee& e) {
                        const u8 frame[16]{0x1F, 2}; // analog axes, all at rest
                        e.write_bytes(u32(e.a(1)), ByteSpan(frame, 16));
                    }, "pad read (neutral frame)");
                }
            }
        Follower follower;
        follower.pod = pod_stops;
        if (scrape) follower.pace = rh::env_num("GT3R_TICK_SCRAPE_PACE", 2.5);
        if (follow && !leaf_line) // the course's AI line 0 (the race line), else the road's centre line
            for (const ex::AiPoint& q : ti::built().ai.lines[0]) follower.line.push_back({double(q.x.host()), double(q.y.host())});
        if (leaf_line) { // the center line's points, each moved onto the leaf ground beside it on the side that has the most
            const course::CourseFile cf = course::CourseFile::parse(course);
            const course::Road road = course::Road::parse(cf.section(course, 28));
            const std::vector<course::Vec3> cl = road.centre_line();
            std::vector<std::array<double, 3>> side[2]; // per side: (x, z, count) at each center point
            side[0].assign(cl.size(), {0, 0, 0}), side[1].assign(cl.size(), {0, 0, 0});
            long n[2] = {0, 0};
            for (const course::RoadSegment& sg : road.segments)
                for (const course::RoadTriangle& tr : sg.triangles) {
                    if (!(tr.flags & 0x20) || cl.size() < 4) continue;
                    const course::Vec3 &a = sg.vertices[tr.a], &b = sg.vertices[tr.b], &c = sg.vertices[tr.c];
                    const double x = (a.x + b.x + c.x) / 3, z = (a.z + b.z + c.z) / 3;
                    size_t i = 0;
                    double best = 1e18;
                    for (size_t k = 0; k < cl.size(); ++k)
                        if (const double d = std::hypot(cl[k].x - x, cl[k].z - z); d < best) best = d, i = k;
                    const course::Vec3 &p0 = cl[i], &p1 = cl[(i + 1) % cl.size()];
                    const int sd = (p1.x - p0.x) * (z - p0.z) - (p1.z - p0.z) * (x - p0.x) > 0 ? 1 : 0;
                    side[sd][i][0] += x, side[sd][i][1] += z, side[sd][i][2] += 1, ++n[sd];
                }
            const auto& use = side[n[1] > n[0] ? 1 : 0];
            for (size_t k = 0; k < cl.size(); ++k)
                if (use[k][2] > 0) follower.line.push_back({use[k][0] / use[k][2], -use[k][1] / use[k][2]});
                else follower.line.push_back({cl[k].x, -cl[k].z});
        }
        if (follow && follower.line.size() < 4) {
            const course::CourseFile cf = course::CourseFile::parse(course);
            for (const course::Vec3& q : course::Road::parse(cf.section(course, 28)).centre_line()) follower.line.push_back({q.x, -q.z});
        }
        if (ghost && race.ncars == 2) {
            const u32 car1 = race_setup::car_addr(race.cond, 1);
            ctx.ee.w8(car1 + ce::kF + f_off(0x3EA), 1), ctx.ee.w8(car1 + ce::kF + f_off(0x3E9), 2);
        }
        if (pad) {
            const u32 car0 = race_setup::car_addr(race.cond, 0);
            ctx.ee.w8(car0 + ce::kF + f_off(0x3E9), ai0 ? 2 : 0);     // control source: pad (the AI)
            ctx.ee.w8(car0 + ce::kF + f_off(0x534), runup ? 1 : 0); // driver kind 0 (1: the AI drives the pad car)
            ctx.ee.hle(pa(0x00210740),
                       [&, car0](Ee& e) {
                           u8 f[16] = {};
                           if (e.g32(pad_read_car_reg()) == car0) { // the caller's $s1 is the car (a later build's: $s0)
                               const int k = std::max(0, t - start_hold);
                               if (follow) follower.frame(e, car0, f);
                               else if (!reverse) rh::pad_race_frame(k, f);
                               else // 3 s on, 3 s braking, 15 s backing, then on
                                   put_frame(f, 0, u16(k < 180 || k >= 1260 ? 4096 : 0), u16(k >= 180 && k < 360 ? 4096 : 0),
                                             u16(k >= 360 && k < 1260 ? 4096 : 0));
                           }
                           else if (e.r32(u32(e.a(0)) + 0x13C) != 0) device_busy = true;
                           e.write_bytes(u32(e.a(1)), ByteSpan(f, 16));
                       },
                       "pad read (car 0 scripted, the others' devices idle)");
        }
        g_run = {race.cond, ev, kind, battle_wear, battle_catchup, logger ? 1u : 0u, logger == 1 ? 1u : 0u};
        rh::g_install(ctx.ee);
        if (spark_race && check_step) {
            std::vector<Bytes> gtci;
            const std::vector<std::vector<ex::SparkPoint>> points = spark_models(ctx.disc->volume(), hs.db, labels, gtci);
            step.install_sparks(ctx.ee, race.cond, gtci, points);
        }
        if (band_mode >= 0) {
            ctx.ee.w8(race.cond + cond_off(0xE340) + 0x22, u8(band_mode));
            ti::built().cfg[0x22] = u8(band_mode);
        }
        if (rolling) ctx.ee.w32(race.info + 0x54, 3), ctx.ee.w16(race_setup::car_addr(race.cond, 0) + car_off(0x16CE), 120);
        if (flow_gate) {
            ctx.ee.w8(race.cond + race_setup::kK8 + k_off(0x37AC), 1), ctx.ee.wf(race.cond + race_setup::kK8 + k_off(0x37B0), 0.37f);
            ti::built().flow = 1, ti::built().flow_scale = ee::F(0.37f);
        }
        phase12_race_settings(ctx.ee, race.cond, race.ncars, race_index++);
        if (label == seeded) seed(race);
        const u32 copy = ctx.ee.alloc(ce::kCarSize, 64);
        try {
            for (; t < start_hold + (worn ? worn_ticks : follow ? follow_ticks : finish || ai0 ? finish_ticks : ai_ticks); ++t) {
                ctx.ee.w32(race.info + 0x5C, u32(std::max(0, start_hold - t)));
                for (const Poke& k : kPokes)
                    if (label == k.event && t - start_hold == k.tick && k.car < race.ncars)
                        k.apply(ctx.ee, race_setup::car_addr(race.cond, k.car));
                ctx.ee.call(race_setup::kCondStep, {Arg::ptr(race.cond), Arg::integer(0)}, 500'000'000);
                if (check_step) step.leaves_frame(ctx.ee); // (a later build's leaves: the particle manager's update)
                if (std::getenv("GT3R_TICK_TRACE") && t % 120 == 0) { const u32 F0 = race_setup::car_addr(race.cond, 0) + ce::kF; std::fprintf(stderr, "t %d progress %.1f laps %d speed %.1f kind %u ctl %d x493 %u\n", t, ctx.ee.rf(F0 + f_off(0x3E4)), s16(ctx.ee.r16(F0 + f_off(0x434))), ctx.ee.rf(F0 + f_off(0x488)), ctx.ee.r8(F0 + f_off(0x534)), s8(ctx.ee.r8(F0 + f_off(0x3E9))), ctx.ee.r8(F0 + f_off(0x493))); }
                if (vary && t % kVaryEvery == kVaryEvery - 1) {
                    run_variants(ctx.ee, ctx.rng, ports, stats, race.cond, race.ncars, copy, rh::fmt("%s tick %d", scenario.c_str(), t), &tape);
                    if (check_course_out)
                        run_course_out(ctx.ee, ctx.rng, course_out_direct, race.cond, race.ncars, copy, rh::fmt("%s tick %d", scenario.c_str(), t));
                    if (check_flow) {
                        run_rank_direct(ctx.ee, ctx.rng, gaps_direct, rank_direct, race.cond, race.ncars, lap_length, kind,
                                        rh::fmt("%s tick %d", scenario.c_str(), t));
                        race_flow.run_direct(ctx.ee, ctx.rng, race.cond, rh::fmt("%s tick %d", scenario.c_str(), t));
                    }
                }
            }
        } catch (const std::exception& e) {
            ctx.check(false, rh::fmt("%s: tick %d: %s", scenario.c_str(), t, e.what()));
        }
    }
    // Scripted pad drives (GT3R_TICK_DRIVES: "-" none, else a substring of the drives' names; default all).
    const char* drives = std::getenv("GT3R_TICK_DRIVES");
    const data::Event* dev = hs.db.event("Gturbo_e_0002");
    if (!dev && !hs.db.event("Gsunday_e_0003")) dev = hs.ev; // (a database without GT3's events: the hit set-up's event)
    const Volume::Entry* dce = dev ? ctx.disc->volume().find(course_path(ctx.disc->volume(), dev->course)) : nullptr;
    if (!dce) ctx.check(false, "pad drives: no event Gturbo_e_0002 or its course");
    const Bytes dcourse = dce ? ctx.disc->volume().read(*dce) : Bytes();
    if (dce) make_ground(dcourse);
    for (const PadDrive& pd : kPadDrives) {
        if (!dce || (drives && (std::string(drives) == "-" || std::string(pd.name).find(drives) == std::string::npos))) continue;
        race_setup::OrigRace race;
        std::string err;
        // (a database without the drive's car: the hit set-up's)
        const std::string pcar = hs.db.car(pd.car) || hs.db.variant(pd.car) ? std::string(pd.car) : hs.car;
        scenario = rh::fmt("pad drive %s (%s, style %d)", pd.name, pcar.c_str(), pd.style);
        if (!race_setup::setup_original_race(ctx.ee, hs.db, hs.gtar, *dev, dcourse, {pcar}, 2, 0, race, err)) {
            ctx.check(false, scenario + ": " + err);
            continue;
        }
        const u32 car = race_setup::car_addr(race.cond, 0), F = car + race_setup::kF;
        ctx.ee.w8(F + f_off(0x3E9), 0); // control source: pad
        ctx.ee.w8(F + f_off(0x534), 0);
        ctx.ee.w8(F + f_off(0x461), pd.style);
        if (pd.poke) pd.poke(ctx.ee, car);
        static u8 frame[16];
        ctx.ee.hle(kPadRead, [](Ee& e) { e.write_bytes(u32(e.a(1)), ByteSpan(frame, 16)); }, "pad read (scripted)");
        g_run = {race.cond, dev, ex::Conductor::SinglePlayer};
        rh::g_install(ctx.ee);
        if (pd.gear_mode >= 0) ti::built().poke(ctx.ee, kControllerSettings.ntsc_u + 0x14, u8(pd.gear_mode)); // poke takes NTSC-U addresses
        Script script;
        int t = 0;
        try {
            for (; !script.done() && t < 60 * 60; ++t) {
                drive_frame(script.step(t, ctx.ee.rf(F + f_off(0x488)), ctx.ee.r8(F + f_off(0x436)), pd.style), frame);
                ctx.ee.w32(race.info + 0x5C, 0);
                ctx.ee.call(race_setup::kCondStep, {Arg::ptr(race.cond), Arg::integer(0)}, 500'000'000);
            }
        } catch (const std::exception& e) {
            ctx.check(false, rh::fmt("%s: tick %d: %s", scenario.c_str(), t, e.what()));
        }
    }
    rh::g_install = nullptr;
    g_run = {};
    ctx.check(!device_busy, "a +pad race: a CPU car's input device was active (its pad read is not empty)");
    ctx.note(rh::fmt("built inputs at race start (race_inputs.h against the memory and the conductor's answers): %ld races", start_checks) +
             (start_bad.empty() ? std::string(", all equal") : "; first difference: " + start_bad));
    ctx.check(start_bad.empty(), "built inputs at race start: " + start_bad);
    // The angle helpers on all their branches (the tyre phase reaches only |a| < 2 pi): 0x1D8C00 and 0x1D8C80 run directly at
    // random angles in +-12 pi, at far ones and next to the wrap edges.
    if (!has_pa(0x001D8C00) || !has_pa(0x001D8C80)) {
        ctx.note("angle_wrap / angle_diff (0x1D8C00 / 0x1D8C80) direct: the program has no such routine (inlined)");
    } else {
        long bad = 0;
        std::string first;
        std::vector<u32> edges;
        for (u32 e : {0x40490FDBu, 0x40C90FDBu, 0x4116CBE4u, 0x3FC90FDBu})
            for (u32 s : {0u, 0x80000000u})
                for (u32 d : {e - 1, e, e + 1}) edges.push_back(s | d);
        for (int i = 0; i < 6000; ++i) {
            const auto pick = [&](int k) {
                if (k % 3 == 0) return ee::F::raw(edges[size_t(ctx.rng.uniform(0, float(edges.size()) - 0.01f))]);
                return ee::F(ctx.rng.uniform(-37.7f, 37.7f) * (k % 7 == 1 ? 300.0f : 1.0f));
            };
            const ee::F a = pick(i), b = pick(i / 3);
            const u32 w = ctx.ee.call(pa(0x001D8C00), {Arg::fbits(a.bits)}).f0;
            const u32 d = ctx.ee.call(pa(0x001D8C80), {Arg::fbits(a.bits), Arg::fbits(b.bits)}).f0;
            if (ex::angle_wrap(a, program()).bits != w && ++bad == 1) first = rh::fmt("angle_wrap(0x%08X): 0x%08X vs 0x%08X", a.bits, ex::angle_wrap(a, program()).bits, w);
            if (ex::angle_diff(a, b, program()).bits != d && ++bad == 1)
                first = rh::fmt("angle_diff(0x%08X, 0x%08X): 0x%08X vs 0x%08X", a.bits, b.bits, ex::angle_diff(a, b, program()).bits, d);
            ctx.sample();
        }
        ctx.note(rh::fmt("angle_wrap / angle_diff (0x1D8C00 / 0x1D8C80) direct: 6000 pairs, %ld differing", bad) + (first.empty() ? "" : "; first: " + first));
        ctx.check(bad == 0, "angle helpers: " + first);
    }
    auto report = [&](const std::string& name, const Stats& s) {
        std::string un;
        int shown = 0;
        for (u32 off : s.unmirrored) {
            if (shown++ == 24) {
                un += " ...";
                break;
            }
            un += rh::fmt(" +0x%X", off);
        }
        ctx.note(rh::fmt("%s: %ld calls (%ld changed the record), %ld differing", name.c_str(), s.calls, s.active, s.bad) +
                 (s.skipped ? rh::fmt(", %ld skipped (not on its path)", s.skipped) : std::string()) +
                 (s.first.empty() ? std::string() : "; first: " + s.first) +
                 (s.vcalls ? rh::fmt("; variants: %ld calls (%ld changed the record), %ld differing", s.vcalls, s.vactive, s.vbad) +
                                 (s.vfirst.empty() ? std::string() : "; first: " + s.vfirst)
                           : std::string()) +
                 (s.unmirrored.empty() ? std::string() : "; record words it changed outside the field table:" + un));
        ctx.check(s.bad == 0, std::string(name) + ": " + s.first);
        ctx.check(s.vbad == 0, std::string(name) + " (variants): " + s.vfirst);
        // (a later build: without the variants and direct calls, and with calls its code makes by a jump, what those alone
        // reach is listed, not failed)
        if (s.calls == 0 && !vary && sim::concept_program(program())) ctx.note(std::string(name) + ": never called (a later build: no variants, or its call is a jump)");
        else ctx.check(s.calls > 0, std::string(name) + ": never called");
    };
    for (size_t k = 0; k < ports.size(); ++k) report(ports[k]->name, stats[k]);
    for (size_t k = 0; k < race_ports.size(); ++k) report(race_ports[k]->name, race_stats[k]);
    if (check_step) {
        report("race step phases 1-9 (0x1F39D8 until the collision step, every car)", step.until);
        report("race step phases 11-13 (0x1F39D8 after the collision step, every car)", step.after);
        report("race step loop after phase 13 (0x1F3DF0 .. 0x1F3E4C: the race flow, the pit lane, every car)", step.flow);
        // (the leaves: checked inside the step check, counted only here, so the races' sample counts stay what the step's
        // ports and checks make them)
        if (sim::concept_program(program()))
            ctx.note(rh::fmt("leaves (the emitter's leaf calls, later 0x19C4E8, on our requests; the particle manager at the end of "
                             "phase 13 and after its update): %ld steps with leaves, %ld maker calls, %ld leaves made, %ld updates, "
                             "%ld differing",
                             step.leaves.calls, step.leaf_calls, step.leaves_made, step.leaf_frames, step.leaves.bad) +
                     (step.leaves.first.empty() ? std::string() : "; first: " + step.leaves.first) +
                     (step.leaf_err.empty() ? std::string() : "; " + step.leaf_err));
        if (step.leaves.bad != 0) ctx.check(false, "leaves: " + step.leaves.first);
        if (!step.leaf_err.empty()) ctx.check(false, "leaves: " + step.leaf_err);
        // (the "#sparks" races' sparks: checked between the steps, counted only here, as the leaves)
        if (step.sparks.calls > 0 || !step.spark_err.empty()) {
            ctx.note(rh::fmt("sparks (#sparks races: each car's emission 0x1AADD0 and the update 0x19F698 between the race steps, "
                             "against ours; the register after each car and the whole pool after the update): %ld of %ld cars with "
                             "spark points, %ld frames (%ld making sparks), %ld emissions (%ld with bursts), %ld sparks made, %ld differing",
                             step.spark_cars, step.spark_entries, step.sparks.calls, step.sparks.active, step.spark_calls, step.spark_bursts, step.sparks_made,
                             step.sparks.bad) +
                     (step.sparks.first.empty() ? std::string() : "; first: " + step.sparks.first) +
                     (step.spark_err.empty() ? std::string() : "; " + step.spark_err));
            ctx.check(step.sparks.bad == 0, "sparks: " + step.sparks.first);
            ctx.check(step.spark_err.empty(), "sparks: " + step.spark_err);
        }
        if (from_start > 0 && start_records.calls > 0) {
            report("race start records (start_setup / start_cars vs the original's after 0x1F3628, every byte)", start_records);
            if (missed_start_wheels) ctx.note(rh::fmt("%ld wheels started off the road (a grid slot past the course's table)", missed_start_wheels));
            report("race step from our start records (phases 1-9 and 11-13 carried on our own results)", step.carried);
        }
    }
    if (ai_cover) {
        std::string gaps;
        for (size_t r = 0; r < std::size(kAiCode); ++r) {
            const u32 lo = kAiCode[r].first;
            for (size_t k = 0; k < ai_hit[r].size();) {
                if (ai_hit[r][k]) {
                    ++k;
                    continue;
                }
                size_t j = k;
                bool code = false; // a stretch of nops only (padding) is not listed
                for (; j < ai_hit[r].size() && !ai_hit[r][j]; ++j) code = code || ctx.ee.r32(lo + 4 * u32(j)) != 0;
                if (code) gaps += rh::fmt(" 0x%X-0x%X", lo + 4 * u32(k), lo + 4 * u32(j) - 4);
                k = j;
            }
        }
        ctx.note("AI code never executed (kAiCode):" + (gaps.empty() ? std::string(" none") : gaps));
    }
    if (check_flow) {
        report("race flow gate (0x1F39D8's start, every car)", flow.gate);
        report("race flow rubber band (0x1F39D8 after the race flow, every car; " + flow.mode_counts() + ")", flow.band);
        // (the pass-by states: counted only when they differ, so the races' sample counts stay what the step's ports and
        // checks make them)
        ctx.note(rh::fmt("pass-by states (0x1F59B0 at the race step's end, every pair of cars): %ld calls (%ld changed them), %ld differing",
                         flow.pass_by.calls, flow.pass_by.active, flow.pass_by.bad) +
                 (flow.pass_by.first.empty() ? std::string() : "; first: " + flow.pass_by.first));
        if (flow.pass_by.bad != 0) ctx.check(false, "pass-by states: " + flow.pass_by.first);
        report("race flow per car (0x1EF060 in the step's loop over the order)", race_flow.car);
        report("race flow virtual 13 (the rolling start, the free run's run-up, the time trial's course-out)", race_flow.start);
        report("race flow virtual 1 (the sector order's time differences)", race_flow.display);
        report("race flow virtual 14 (the licence's judging, the race clock)", race_flow.clock);
        report("race flow gap matrices (0x1ED458 after the race clock)", race_flow.gaps);
        report("race flow ranking (the conductor's virtual 3)", race_flow.ranking);
        report("hit course-out (the conductor's virtual 41 at the collision step's hits, 0x1DFEB8)", race_flow.hits);
    }
    if (check_course_out) report("phase 12 course_out (0x1DA020) called directly on changed copies", course_out_direct);
    if (vary && check_flow) {
        report("race flow gap matrices (0x1ED458) called directly on changed race states", gaps_direct);
        report("race flow ranking (virtual 3) called directly on changed race states", rank_direct);
        report("race flow virtuals 1 / 13-19 / 41 called directly on changed race states", race_flow.direct);
    }
    if (!g_cover.empty()) {
        std::string cov;
        for (const auto& [k, n] : g_cover) cov += rh::fmt("%s%s %ld", cov.empty() ? "" : "; ", k.c_str(), n);
        ctx.note("coverage (phase 7, the course-out rule): " + cov);
    }
    ctx.note(rh::fmt("AI course objects: %ld races on our course setup's object (ai_course_setup)", g_ai_built_races) +
             (g_ai_built_diff.empty() ? std::string(", each the same as the original's") : "; first difference: " + g_ai_built_diff));
    ctx.check(g_ai_built_diff.empty(), "our AI course object: " + g_ai_built_diff);
    ctx.check(g_ai_built_err.empty(), "our AI course setup: " + g_ai_built_err);
    g_ai_built.reset(), g_ai_scenario = nullptr;
    if (!g_ai_cover.empty()) {
        std::string cov;
        for (const auto& [k, n] : g_ai_cover) cov += rh::fmt("%s%s %ld", cov.empty() ? "" : "; ", k.c_str(), n);
        ctx.note("AI coverage: " + cov);
    }
}

// ---- the whole race as one object (race_exact_lockstep) ---------------------------------------------------------------------
// sim::exact::ExactRace (src/sim/exact/race.h) against the original's race, side by side: the original set up from the disc
// with its own code (race_setup.h), ours from the same setup made from our own data (RaceCourse from our parse of the course
// file, each car's StartEntry from our database, start_cars), nothing copied from the original's memory. Both are fed the same
// pad frames (each car's frame as the original's phase 3 had it) and the same start countdown (race info + 0x5C, which the
// harness drives); the scenario's own changes to the original between steps (race_tick_exact's CPU race pokes, its end-of-run
// calls, the pad races' placement and record writes, the body flags) are made to ours as the same field changes. After every
// step: every car record (the field table; the record words the step changed outside it are listed), the race flow (clock,
// end countdown, result words, the order), the proximity and gap matrices, every collision body record, the VU0 random
// register, the race's event queue posts, the pit events, every collision response's report (0x1DFEB8) and every tyre effect
// particle (the original runs the course's emitter: RacePS2Base's particle system on the course's Env0 block). A race stops at
// its first difference.
namespace {

namespace lx = sim::collide::exact;

// RacePS2Base's particle system at RaceCourse + 4 for the race of conductor `cond` (its parameters the course's Env0 block,
// container slot 32), reset by 0x1A4630, which seeds the VU0 random register with 0.1 as every run's start does.
void install_emitter(Ee& ee, u32 cond) {
    const u32 course = ee.r32(ee.r32(cond) + 0x38), container = ee.r32(course);
    const u32 E = ee.alloc(0x40, 64), pool = ee.alloc(0x4000, 64);
    ee.w32(E, pool);
    ee.w32(E + 0x38, ee.r32(container + 4 * 32));
    ee.call(0x001A4630, {Arg::ptr(E)});
    ee.w32(course + 4, E);
}

ex::SmokeParticle particle_of(const std::array<u8, 0x40>& b, int car, int wheel) {
    auto w = [&](u32 o) { return u32(b[o]) | u32(b[o + 1]) << 8 | u32(b[o + 2]) << 16 | u32(b[o + 3]) << 24; };
    auto f = [&](u32 o) { return ee::F::raw(w(o)); };
    ex::SmokeParticle p;
    p.car = car, p.wheel = wheel;
    p.size_class = b[2], p.kind = b[3], p.colour = w(4);
    p.size = f(8), p.emit = f(0xC), p.x10 = f(0x10), p.angle = f(0x14), p.growth = f(0x18);
    p.life = s16(u16(b[0x1C] | b[0x1D] << 8)), p.x1e = b[0x1E], p.x1f = b[0x1F];
    p.x = f(0x20), p.h = f(0x24), p.z = f(0x28), p.ground = f(0x2C);
    p.normal = {f(0x30), f(0x34), f(0x38)}, p.plane_d = f(0x3C);
    return p;
}

std::string particle_difference(const ex::SmokeParticle& a, const ex::SmokeParticle& b) {
    auto all = [](const ex::SmokeParticle& p) {
        return std::array<u32, 21>{u32(p.car),      u32(p.wheel),    p.size_class,     p.kind,           p.colour,      p.size.bits,
                                   p.emit.bits,     p.x10.bits,      p.angle.bits,     p.growth.bits,    u32(u16(p.life)), p.x1e,
                                   p.x1f,           p.x.bits,        p.h.bits,         p.z.bits,         p.ground.bits, p.normal.x.bits,
                                   p.normal.y.bits, p.normal.z.bits, p.plane_d.bits};
    };
    static const char* names[] = {"car", "wheel", "size class", "kind", "colour", "size", "emit", "+0x10", "angle", "growth", "life",
                                  "+0x1E", "+0x1F", "x", "h", "z", "ground", "normal.x", "normal.y", "normal.z", "plane_d"};
    const auto x = all(a), y = all(b);
    for (size_t k = 0; k < x.size(); ++k)
        if (x[k] != y[k]) return rh::fmt("%s 0x%X vs 0x%X", names[k], x[k], y[k]);
    return {};
}

// The lockstep of one race at a time. begin() when the original's race is set up (before the scenario's changes and the first
// step), with our race's setup; entry() at each race step's start (the original's state then: the scenario's changes are made
// to ours); exit() after it (ours runs its step and everything is compared; false at a difference, which ends the race).
class Lockstep {
public:
    long races = 0, steps = 0, gated = 0, hits = 0, wall_hits = 0, car_hits = 0, particles = 0, events = 0, pit_events = 0;
    long bad = 0, stopped = 0, missed_ground = 0;
    std::string first, stops;
    std::set<u32> unmirrored;
    std::map<std::string, long> conductors;
    double ours_s = 0, replay_s = 0, replay_s6 = 0;
    long ours_cars = 0, replays = 0, replay_steps = 0, replay_cars = 0, replay_steps6 = 0;

    // The probes on the original (once per race, after its set-up): the race step's entry (entry() runs from it when
    // `entry_probe`), each car's phase 3 frame, the collision responses' reports, the emitter's particles, the pit events.
    void install(Ee& e, bool entry_probe) {
        on_ = false;
        if (entry_probe)
            e.add_probe(race_setup::kCondStep, [this](Ee& x) {
                if (race_ && u32(x.a(1)) == 0) entry(x);
            });
        e.add_probe(0x001E4D38, [this](Ee& x) {
            if (on_ && u32(x.gpr[31].lo) == 0x001F3B6C) cur_ = index(u32(x.a(0))), sp_ = u32(x.gpr[29].lo);
        });
        e.add_probe(0x001F3B6C, [this](Ee& x) {
            if (on_) (nested_ ? nested_frames_ : frames_)[size_t(cur_)] = pad_frame(x, sp_ - pad_frame_below());
        });
        // the free run ghost's extra step (0x1F1400's call of the race step in its second entry mode): its own frames
        e.add_probe(pa(0x001F14A4), [this](Ee&) { nested_ = true, nested_frames_.fill({}); });
        e.add_probe(pa(0x001F14AC), [this](Ee&) { nested_ = false; });
        e.add_probe(0x001DFEB8, [this](Ee& x) {
            if (on_) hits_.push_back({index(u32(x.a(0))), s32(x.a(1)), s32(x.a(2)), ee::F::raw(x.fpr[12])});
        });
        e.add_probe(0x001EB218, [this](Ee& x) {
            if (on_ && u32(x.gpr[31].lo) == 0x001F3D3C) cur_ = index(u32(x.a(0)));
        });
        e.add_probe(0x001A4A14, [this](Ee& x) { // the emitter's particle made ($s1 the particle, $s3 the request)
            if (!on_) return;
            std::array<u8, 0x40> b;
            x.read_bytes(x.g32(17), b.data(), 0x40);
            particles_.push_back(particle_of(b, cur_, s32(x.r32(x.g32(19)))));
        });
        e.add_probe(pa(0x001ED9F0), [this](Ee& x) {
            if (const u32 ra = u32(x.gpr[31].lo); on_ && (ra == pa(0x001EDE4C) || ra == pa(0x001EE1AC))) pit_.push_back({1, s32(x.a(1))});
        });
        e.add_probe(0x001EDA40, [this](Ee& x) {
            if (on_ && u32(x.gpr[31].lo) == 0x001EE0AC) pit_.push_back({6, s32(x.a(1))});
        });
    }

    // Our race from `setup` against the original's of conductor `cond` as its initialisation left it. False (and counted) when
    // ours cannot be built or the start differs.
    bool begin(Ee& ee, u32 cond, const ex::RaceSetup& setup, const std::string& scenario) {
        race_.reset();
        scenario_ = scenario;
        cond_ = cond, info_ = ee.r32(cond);
        n_ = int(ee.r32(cond + race_setup::kK8 + k_off(0x6384)));
        try {
            race_ = std::make_unique<ex::ExactRace>(setup);
        } catch (const std::exception& e) {
            fail(std::string("our race: ") + e.what());
            return false;
        }
        if (race_->size() != n_) {
            fail(rh::fmt("our race has %d cars, the original's %d", race_->size(), n_));
            race_.reset();
            return false;
        }
        ++races, ++conductors[ex::conductor_name(setup.kind)];
        last_.assign(size_t(n_), {});
        for (int i = 0; i < n_; ++i) {
            last_[size_t(i)] = ce::read_car(ee, car(i));
            missed_ground += ce::take_missed_ground(race_->cars()[size_t(i)], last_[size_t(i)]); // (a start off the road)
            if (const std::string d = ce::first_difference(race_->cars()[size_t(i)], last_[size_t(i)]); !d.empty()) {
                fail(rh::fmt("start, car %d: ", i) + d);
                race_.reset();
                return false;
            }
        }
        cm_ = cond + race_setup::kCm, walls_ = collide_exact::wall_table(ee, ee.r32(collide_exact::body_at(cm_, 0) + 0x478));
        last_bodies_ = collide_exact::read_bodies(ee, cm_, walls_);
        if (const std::string d = collide_exact::first_difference(race_->bodies(), last_bodies_); !d.empty()) {
            fail("start, bodies: " + d);
            race_.reset();
            return false;
        }
        t_ = 0;
        records_ = ex::SoloRecorders(true);
        return true;
    }
    ex::ExactRace* race() { return race_.get(); }

    // The race step's start in the original: the scenario's changes since the last step made to ours, the countdown read.
    void entry(Ee& ee) {
        if (!race_) return;
        race_->set_start_count(s32(ee.r32(info_ + 0x5C)));
        for (int i = 0; i < n_; ++i) ce::copy_changed(race_->cars()[size_t(i)], last_[size_t(i)], ce::read_car(ee, car(i)));
        const std::vector<lx::Body> now = collide_exact::read_bodies(ee, cm_, walls_);
        for (int i = 0; i < n_; ++i) {
            lx::Body& b = race_->bodies()[size_t(i)];
            if (now[size_t(i)].front != last_bodies_[size_t(i)].front) b.front = now[size_t(i)].front;
            if (now[size_t(i)].rear != last_bodies_[size_t(i)].rear) b.rear = now[size_t(i)].rear;
        }
        // A copy of our race at its first step, replayed at the race's end from the same frames and countdown (the copy is a
        // whole snapshot): the same results when the scenario changed nothing after the first step; and the step's cost.
        if (t_ == 0) copy_ = std::make_unique<ex::ExactRace>(*race_), replay_.clear(), changed_ = false;
        else if (!changed_)
            for (int i = 0; i < n_ && !changed_; ++i) changed_ = !ce::first_difference(race_->cars()[size_t(i)], last_[size_t(i)]).empty();
        raw_.assign(size_t(n_), {});
        for (int i = 0; i < n_; ++i) raw_[size_t(i)] = ce::snapshot(ee, car(i));
        for (u32 k = 0; k < queue_count_off() + 4 - 0x800; k += 4) ee.w32(info_ + 0x84 + 0x800 + k, 0); // the race's event queue emptied (nothing in
                                                                                 // the step reads it)
        hits_.clear(), particles_.clear(), pit_.clear();
        rec0_ = g_rec_calls.size();
        frames_.fill({});
        on_ = true;
    }

    bool exit(DiffContext& ctx) {
        Ee& ee = ctx.ee;
        if (!race_ || !on_) return race_ != nullptr;
        on_ = false;
        Input input{race_->start_count(), frames_, nested_frames_, {}, {}};
        for (int i = 0; i < n_ && i < 8; ++i)
            if (const u32 h = ee.r32(car(i) + 0xC)) input.logger_frame[size_t(i)] = ee.r32(h + 0xFC), input.logger_rel[size_t(i)] = ee.r32(h + 0xC8) == 2;
        replay_.push_back(input);
        const std::string where = rh::fmt("step %d", t_++);
        const auto t0 = std::chrono::steady_clock::now();
        inputs(*race_, records_, replay_.back());
        try {
            race_->race_step(std::span<const ex::PadFrame>(frames_.data(), size_t(n_)));
        } catch (const std::exception& e) {
            if (std::string(e.what()).find("not ported") != std::string::npos) {
                ++stopped;
                stops += (stops.empty() ? "" : "; ") + scenario_ + " at " + where + ": " + e.what();
                race_.reset();
                return false;
            }
            return fail(where + ": ours: " + e.what());
        }
        ours_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        ours_cars += n_;
        ++steps;
        if (!race_->ran()) ++gated;
        ctx.sample();
        std::string d;
        // the cars
        for (int i = 0; i < n_; ++i) {
            const ex::Car theirs = ce::read_car(ee, car(i));
            if (d.empty())
                if (std::string di = ce::first_difference(race_->cars()[size_t(i)], theirs); !di.empty()) d = rh::fmt("car %d: ", i) + di;
            for (u32 off : ce::unmirrored_changes(raw_[size_t(i)], ce::snapshot(ee, car(i)))) unmirrored.insert(off);
            last_[size_t(i)] = theirs;
        }
        // the bodies and the random register
        last_bodies_ = collide_exact::read_bodies(ee, cm_, walls_);
        if (d.empty())
            if (std::string db = collide_exact::first_difference(race_->bodies(), last_bodies_); !db.empty()) d = "bodies: " + db;
        if (d.empty() && race_->random().r != ee.vr) d = rh::fmt("VU0 R 0x%08X vs 0x%08X", race_->random().r, ee.vr);
        // the race flow (the clock, end countdown, result words, the records, the sector order, the logger's tables) and the
        // matrices
        if (d.empty())
            if (std::string df = flow_differ(ee, cond_, race_->flow()); !df.empty()) d = "race flow: " + df;
        for (int k = 0; k < n_ && d.empty(); ++k)
            if (race_->order()[size_t(k)] != s32(ee.r32(cond_ + cond_off(0xE388) + 4 * u32(k))))
                d = rh::fmt("order [%d]: %d vs %d", k, race_->order()[size_t(k)], s32(ee.r32(cond_ + cond_off(0xE388) + 4 * u32(k))));
        if (d.empty()) {
            std::vector<u8> pb(0x140);
            ee.read_bytes(cond_ + kProxSlip, pb.data(), 0x140);
            const ex::Proximity theirs = read_proximity(pb.data());
            const ex::GapMatrix gaps = ti::mem_gaps(ee, cond_);
            for (size_t i = 0; i < 8 && d.empty(); ++i)
                for (size_t j = 0; j < 8 && d.empty(); ++j) {
                    if (race_->proximity().slip[i][j].bits != theirs.slip[i][j].bits || race_->proximity().code[i][j] != theirs.code[i][j])
                        d = rh::fmt("proximity [%zu][%zu]", i, j);
                    else if (race_->gaps().laps[i][j] != gaps.laps[i][j] || race_->gaps().gap[i][j].bits != gaps.gap[i][j].bits)
                        d = rh::fmt("gap matrix [%zu][%zu]", i, j);
                }
        }
        // the race's event queue posts (the race display's among them), the free run's recorder calls and the pit events
        if (d.empty()) {
            const auto& ours = race_->events();
            if (std::string dq = queue_differ(ee, info_, 0, ours); !dq.empty()) d = dq;
            if (d.empty())
                if (std::string dr = recorder_differ(ee, info_, rec0_, race_->flow().recorder); !dr.empty()) d = "recorder: " + dr;
            for (const ex::RecorderEvent& r : race_->flow().recorder) records_.apply(r);
            events += long(ours.size());
            const auto& op = race_->pit_events();
            if (d.empty() && op.size() != pit_.size()) d = rh::fmt("%zu pit events vs %zu", op.size(), pit_.size());
            for (size_t k = 0; k < op.size() && d.empty(); ++k)
                if (op[k].type != pit_[k].type || op[k].car != pit_[k].car) d = rh::fmt("pit event %zu", k);
            pit_events += long(op.size());
        }
        // the collision responses' reports and the tyre effect particles
        if (d.empty()) {
            const auto& oh = race_->hits();
            if (oh.size() != hits_.size()) d = rh::fmt("%zu collision reports vs %zu", oh.size(), hits_.size());
            for (size_t k = 0; k < oh.size() && d.empty(); ++k) {
                const ex::CollisionHit &a = oh[k], &b = hits_[k];
                if (a.car != b.car || a.kind != b.kind || a.part != b.part || a.strength.bits != b.strength.bits)
                    d = rh::fmt("collision report %zu: car %d kind %d part %d 0x%08X vs %d %d %d 0x%08X", k, a.car, a.kind, a.part,
                                a.strength.bits, b.car, b.kind, b.part, b.strength.bits);
                ++(a.kind == 0 ? wall_hits : car_hits);
            }
            hits += long(oh.size());
            const auto& opp = race_->particles();
            if (d.empty() && opp.size() != particles_.size()) d = rh::fmt("%zu particles vs %zu", opp.size(), particles_.size());
            for (size_t k = 0; k < opp.size() && d.empty(); ++k)
                if (std::string dp = particle_difference(opp[k], particles_[k]); !dp.empty()) d = rh::fmt("particle %zu: ", k) + dp;
            particles += long(opp.size());
        }
        if (!d.empty()) return fail(where + ": " + d);
        return true;
    }

    // After a change made to both races between steps and checked by the caller (the Analyzer's jump): the original's records
    // and bodies now the last seen, so the next step does not take the change as the scenario's.
    void accept(Ee& ee) {
        for (int i = 0; i < n_; ++i) last_[size_t(i)] = ce::read_car(ee, car(i));
        last_bodies_ = collide_exact::read_bodies(ee, cm_, walls_);
        copy_.reset(); // (no replay of the copy: the race jumped)
    }

    // The race's end: the copy replayed (when the race ran to its end without a difference).
    void end() {
        if (race_ && copy_ && !replay_.empty()) {
            // GT3R_LOCK_REPLAYS: replays of a fresh copy each (the fastest is counted)
            const int reps = std::max(1, int(rh::env_num("GT3R_LOCK_REPLAYS", 1)));
            double best = 1e30;
            const ex::ExactRace start = *copy_;
            for (int r = 0; r < reps; ++r) {
                if (r > 0) *copy_ = start;
                const auto t0 = std::chrono::steady_clock::now();
                ex::SoloRecorders rec(true);
                for (const Input& in : replay_) {
                    copy_->set_start_count(in.start_count);
                    inputs(*copy_, rec, in);
                    copy_->race_step(std::span<const ex::PadFrame>(in.frames.data(), size_t(n_)));
                    for (const ex::RecorderEvent& r : copy_->flow().recorder) rec.apply(r);
                }
                best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            }
            replay_s += best;
            replay_steps += long(replay_.size()), replay_cars += long(replay_.size()) * n_;
            if (n_ == 6) replay_s6 += best, replay_steps6 += long(replay_.size());
            if (!changed_) {
                ++replays;
                std::string d;
                for (int i = 0; i < n_ && d.empty(); ++i)
                    if (std::string di = ce::first_difference(copy_->cars()[size_t(i)], race_->cars()[size_t(i)]); !di.empty())
                        d = rh::fmt("car %d: ", i) + di;
                if (d.empty() && copy_->random().r != race_->random().r) d = "VU0 R";
                if (d.empty() && copy_->flow().clock != race_->flow().clock) d = "race clock";
                if (!d.empty()) fail("the copy replayed: " + d);
            }
        }
        race_.reset(), copy_.reset(), replay_.clear(), on_ = false;
    }

    void report(DiffContext& ctx) const {
        std::string un, modes;
        for (u32 off : unmirrored) un += rh::fmt(" +0x%X", off);
        for (const auto& [k, v] : conductors) modes += rh::fmt("%s%s %ld", modes.empty() ? "" : ", ", k.c_str(), v);
        ctx.note(rh::fmt("%ld races (%s), %ld steps (%ld gated), %ld collision reports (%ld wall, %ld car), %ld particles, %ld race "
                         "events, %ld pit events; %ld differing",
                         races, modes.c_str(), steps, gated, hits, wall_hits, car_hits, particles, events, pit_events, bad) +
                 (first.empty() ? std::string() : "; first: " + first));
        ctx.note(rh::fmt("ours: %.2f us per step, %.2f us per car beside the original; replayed alone (%ld races, %ld compared): %.2f us "
                         "per step, %.2f us per car (this build)",
                         steps ? 1e6 * ours_s / double(steps) : 0.0, ours_cars ? 1e6 * ours_s / double(ours_cars) : 0.0, races, replays,
                         replay_steps ? 1e6 * replay_s / double(replay_steps) : 0.0,
                         replay_cars ? 1e6 * replay_s / double(replay_cars) : 0.0) +
                 (replay_steps6 ? rh::fmt("; the 6-car races alone: %.2f us per step", 1e6 * replay_s6 / double(replay_steps6)) : std::string()));
        if (!un.empty()) ctx.note("record words the steps changed outside the field table:" + un);
        if (stopped) ctx.note(rh::fmt("%ld races stopped at an unported path: ", stopped) + stops);
        if (missed_ground) ctx.note(rh::fmt("%ld wheels started off the road (a grid slot past the course's table)", missed_ground));
        ctx.check(bad == 0, "race lockstep: " + first);
        ctx.check(races > 0, "race lockstep: no race ran");
    }

private:
    std::unique_ptr<ex::ExactRace> race_, copy_;
    struct Input {
        s32 start_count;
        std::array<ex::PadFrame, 8> frames, nested; // the step's frames, its ghost step's
        std::array<u32, 8> logger_frame;             // the cars' input objects for the data logger
        std::array<u8, 8> logger_rel;
    };
    // A step's inputs besides the frames: the free run's recorders, the ghost step's frames, the data logger's input objects.
    void inputs(ex::ExactRace& r, const ex::SoloRecorders& rec, const Input& in) const {
        r.set_recorder(rec.inputs(r.flow_inputs().behaviour));
        r.set_nested_pads([nested = in.nested](std::span<ex::PadFrame> f) {
            for (size_t i = 1; i < f.size() && i < 8; ++i) f[i] = nested[i];
        });
        for (int i = 0; i < n_ && i < 8; ++i) r.set_logger_input(i, in.logger_frame[size_t(i)], in.logger_rel[size_t(i)] != 0);
    }
    std::vector<Input> replay_;
    bool changed_ = false;
    std::string scenario_;
    u32 cond_ = 0, info_ = 0, cm_ = 0, walls_ = 0, sp_ = 0;
    int n_ = 0, cur_ = 0, t_ = 0;
    bool on_ = false;
    std::vector<ex::Car> last_;
    std::vector<lx::Body> last_bodies_;
    std::vector<std::vector<u8>> raw_;
    std::array<ex::PadFrame, 8> frames_{};
    std::vector<ex::CollisionHit> hits_;
    std::vector<ex::SmokeParticle> particles_;
    std::vector<ex::PitEvent> pit_;
    ex::SoloRecorders records_{true}; // the free run's recorders, from our race's recorder events
    size_t rec0_ = 0;
    bool nested_ = false;
    std::array<ex::PadFrame, 8> nested_frames_{};

    u32 car(int i) const { return race_setup::car_addr(cond_, i); }
    int index(u32 car) const { return std::clamp(int((car - race_setup::car_addr(cond_, 0)) / car_stride()), 0, std::max(0, n_ - 1)); }
    bool fail(const std::string& d) {
        ++bad;
        if (first.empty()) first = scenario_ + ": " + d;
        race_.reset(), copy_.reset(), replay_.clear();
        on_ = false;
        return false;
    }
};

// Our race's setup for the original's race of conductor `cond` (set up by race_setup from event `ev` with the cars `labels`,
// before its first step) under the race mode `kind`: our parse of the course file, our database's entries with the race's
// setup inputs as the original's entries hold them, the race information's words as the original's race information holds
// them. `course` is the course file, `data` the program's tables as the original's are, `rules` (optional) the configuration.
ex::RaceSetup lockstep_setup(Ee& ee, u32 cond, const rh::HitSetup& hs, const data::Event& ev, ByteSpan course, const std::vector<std::string>& labels,
                             ex::Conductor kind, const ex::ProgramData& data, const ex::RaceConfig* rules) {
    race_setup::OrigRace o;
    o.cond = cond, o.info = ee.r32(cond), o.course = ee.r32(o.info + 0x38), o.container = ee.r32(o.course);
    o.ncars = int(ee.r32(cond + race_setup::kK8 + k_off(0x6384)));
    ex::RaceSetup s;
    s.data = &data, s.db = &hs.db;
    s.event = ByteSpan(ev.bytes.data(), ev.bytes.size());
    s.course_category = ti::course_category(hs.db, ev);
    s.kind = kind;
    s.laps = ee.r8(cond + cond_off(0xE340) + 1);
    s.start_type = s32(ee.r32(o.info + 0x54));
    s.battle_laps = s32(ee.r32(o.info + 8)), s.battle_wear = s32(ee.r32(o.info + 0xC)), s.handicap = s32(ee.r32(o.info + 0x10));
    s.battle_catchup = s32(ee.r32(o.info + 0x14));
    const ex::RaceFlow f = read_flow(ee, cond); // the session's and the entries' time records as the visit holds them
    s.session = f.session, s.records = f.entries;
    s.replay = ee.r32(o.info + 0x6C) != 0, s.unjudged = ee.r32(o.info + 0x80) != 0, s.locked = ee.r32(o.info + 0x7C) != 0;
    s.logger = ee.r32(o.info + 0x70), s.logger_record = ee.r32(o.info + 0x74);
    const u32 unit = ee.r32(pa(0x002F59D8));
    s.speed_unit = u8(unit == 0 ? 0 : unit == 1 ? 1 : 2);
    const u32 rec = ee.r32(ee.r32(o.info + 0x28) + holder_off(0x18)); // the licence's record (the event's)
    for (u32 k = 0; k < 3; ++k)
        s.licence_times[k] = u32(ee.r8(rec + 0x1C4 + 4 * k)) * 60000u + u32(ee.r8(rec + 0x1C5 + 4 * k)) * 1000u + ee.r16(rec + 0x1C6 + 4 * k);
    s.rules = rules;
    s.info = o.info, s.conductor = cond, s.course_addr = o.course;
    s.road_addr = ee.r32(o.container + 4 * 28);
    s.course = ex::RaceCourse::load(course, data);
    for (int i = 0; i < o.ncars; ++i) s.entries.push_back(start_setup::start_entry(ee, o, hs.db, labels[size_t(i)], i));
    return s;
}

// The race information's frame 0x17A5E8 (the start countdown, before every race step) against race_info_frame, called
// directly on a race information object: every start type with seconds 0 and 3, a start request in the middle of a countdown,
// the timer; its countdown display 0x1A96B8 stubbed. Each frame's start words (+0x50 .. +0x68) and queue posts compared.
void check_countdown(DiffContext& ctx, long& frames, std::string& first) {
    Ee& ee = ctx.ee;
    ee.hle(0x001A96B8, [](Ee&) {}, "countdown display (stubbed)");
    const u32 info = ee.alloc(0x1000, 64), obj = ee.alloc(0x40), a = ee.alloc(0x40), b = ee.alloc(0x40);
    ee.w32(info + 0x3C, obj), ee.w32(obj + 4, a), ee.w32(a + 0x38, b);
    struct Case {
        s32 type, seconds, timer, restart; // restart: a second start request at that frame (0 none)
    };
    const Case cases[] = {{-1, 3, 0, 0}, {0, 0, 0, 0}, {0, 3, 0, 0}, {1, 0, 0, 0}, {2, 3, 0, 0}, {3, 3, 0, 0},
                          {7, 3, 0, 0},  {0, 3, 50, 0}, {2, 0, 30, 100}, {1, 0, 0, 120}};
    for (const Case& c : cases) {
        ex::RaceInfoStart ours;
        ours.start = 1, ours.start_type = c.type, ours.seconds = c.seconds, ours.timer_on = c.timer != 0, ours.timer = c.timer;
        ee.w32(info + 0x50, 1), ee.w32(info + 0x54, u32(c.type)), ee.w32(info + 0x58, u32(c.seconds)), ee.w32(info + 0x5C, 0);
        ee.w32(info + 0x60, u32(ours.timer_on)), ee.w32(info + 0x68, u32(c.timer));
        for (int t = 0; t < 700; ++t) {
            if (c.restart && t == c.restart) ours.start = 1, ee.w32(info + 0x50, 1);
            ee.w32(info + 0x84 + queue_count_off(), 0);
            ee.call(0x0017A5E8, {Arg::ptr(info)});
            std::vector<ex::RaceEvent> ev;
            ex::race_info_frame(ours, ev);
            ++frames;
            std::string d;
            const s32 w[6] = {ours.start, ours.start_type, ours.seconds, ours.start_count, ours.timer_on, ours.timer};
            const u32 offs[6] = {0x50, 0x54, 0x58, 0x5C, 0x60, 0x68};
            for (int k = 0; k < 6 && d.empty(); ++k)
                if (w[k] != s32(ee.r32(info + offs[k]))) d = rh::fmt("+0x%X %d vs %d", offs[k], w[k], s32(ee.r32(info + offs[k])));
            const u32 m = ee.r32(info + 0x84 + queue_count_off());
            if (d.empty() && m != ev.size()) d = rh::fmt("%zu posts vs %u", ev.size(), m);
            for (u32 k = 0; k < m && d.empty(); ++k) {
                const u32 q = info + 0x84 + 8 * k;
                if (ev[k].type != s32(s16(ee.r16(q))) || ev[k].car != s32(ee.r32(q + 4)) || ev[k].arg != s32(s16(ee.r16(q + 2))))
                    d = rh::fmt("post %u", k);
            }
            if (!d.empty()) {
                if (first.empty()) first = rh::fmt("start type %d, %d s, timer %d, frame %d: ", c.type, c.seconds, c.timer, t) + d;
                break;
            }
        }
    }
}

} // namespace

// The CPU races of race_tick_exact (every conductor, pad cars, wear and the pit lane, the race-flow gate, the rubber band's
// modes, the rolling start, finishes, its pokes) and its two-car pad races on race_hits' placements (contact, walls, the pad
// scenarios, the pit lane run) as whole races in lockstep. Options: GT3R_LOCK_EVENTS (race_tick_exact's labels and tags;
// default its list; "-" none), GT3R_LOCK_PAD=0 (no pad races), GT3R_TICK_AI_SECONDS / GT3R_TICK_WORN_SECONDS /
// GT3R_TICK_FINISH_SECONDS / GT3R_TICK_HOLD / GT3R_CARHIT_TICKS / GT3R_CARHIT_SWEEP as there.
DIFF_TEST(race_exact_lockstep, "Whole races as sim::exact::ExactRace (built from our own data) vs the original's races, every step, bit for bit") {
    rh::HitSetup hs;
    if (!rh::load_hit_setup(ctx, hs)) return;
    std::string perr;
    const ex::ProgramData* prog = start_setup::program_data(ctx, perr);
    if (!prog) return ctx.check(false, "program tables: " + perr);
    Lockstep lock;
    // ---- the CPU races ----
    std::function<void(int, int, u8*)> frame_fn; // the current CPU race's pad frame of car i at step t
    int tick = 0;
    u32 car0 = 0;
    const char* default_events =
        "Gsunday_e_0003,Gevolution_e_0001,Ggtworld_n_1010,Dr_tahitimaze_0001,Gsunday_e_0002,Gspider_e_0001,G_4wd_e_0001,demo_wet,Dr_swiss_0001,"
        "Ggtworld_h_1004/worn,Dr_smokymt_0001+pad,demo_wet+pad,Gturbo_h_0001,"
        "AT020001@tt#runup,AT020001@tt#ai,L0A0001@licence#runup,AT040001@freerun,M400@machine,M400@machine#ai,"
        "Gsunday_e_0003@battle2p#finish,Gsunday_e_0003@battlemp,Gsunday_e_0002@practice#ai,Gsunday_e_0002#band1,"
        "Gspider_e_0001#band2,G_4wd_e_0001#flow,Gsunday_e_0003#finish,Gsunday_e_0002+pad#reverse,Gspider_e_0001+pad#rolling,"
        "Gsunday_e_0001~L_seattle1,AT040001@freerun#ai,AT040001@freerun#ai#ghost,AT040001@freerun#follow#ghost,"
        "AT020001@tt#runup#follow#logger,L0A0001@licence#follow#logger,M400@machine#follow#logger,AT040001@freerun#follow#logger";
    const std::string events = std::getenv("GT3R_LOCK_EVENTS") ? std::getenv("GT3R_LOCK_EVENTS") : default_events;
    const int ai_ticks = int(rh::env_num("GT3R_TICK_AI_SECONDS", 40) * 60);
    const int worn_ticks = int(rh::env_num("GT3R_TICK_WORN_SECONDS", 120) * 60);
    const int finish_ticks = int(rh::env_num("GT3R_TICK_FINISH_SECONDS", 150) * 60);
    const int start_hold = int(rh::env_num("GT3R_TICK_HOLD", 90));
    const std::string seeded = "Gturbo_h_0001";
    int race_index = 0;
    for (size_t pos = 0; events != "-" && pos < events.size();) {
        size_t end = events.find(',', pos);
        if (end == std::string::npos) end = events.size();
        std::string label = events.substr(pos, end - pos);
        pos = end + 1;
        // race_tick_exact's tags
        ex::Conductor kind = ex::Conductor::SinglePlayer;
        bool alone = false;
        if (const size_t at = label.find('@'); at != std::string::npos) {
            static const std::pair<const char*, ex::Conductor> kModes[] = {
                {"tt", ex::Conductor::TimeTrial},      {"licence", ex::Conductor::License},   {"freerun", ex::Conductor::FreeRun},
                {"battle2p", ex::Conductor::Battle2P}, {"battlemp", ex::Conductor::BattleMP}, {"machine", ex::Conductor::MachineTest},
                {"practice", ex::Conductor::FreePractice}};
            const size_t to = label.find_first_of("#+/", at + 1);
            const std::string m = label.substr(at + 1, to == std::string::npos ? std::string::npos : to - at - 1);
            label.erase(at, m.size() + 1);
            for (const auto& [name, k] : kModes)
                if (m == name) kind = k;
            alone = kind != ex::Conductor::Battle2P && kind != ex::Conductor::BattleMP;
        }
        const bool worn = label.size() > 5 && label.substr(label.size() - 5) == "/worn";
        if (worn) label.resize(label.size() - 5);
        int band_mode = -1;
        bool flow_gate = false;
        for (const char* tag : {"#band1", "#band2", "#flow"})
            if (const size_t at = label.find(tag); at != std::string::npos) {
                if (tag[1] == 'b') band_mode = tag[5] - '0';
                else flow_gate = true;
                label.erase(at, std::strlen(tag));
            }
        int laps = 2;
        bool finish = false;
        if (const size_t at = label.find("#finish"); at != std::string::npos) laps = 1, finish = true, label.erase(at, 7);
        bool rolling = false, reverse = false, ai0 = false, runup = false, follow = false, ghost = false;
        int logger = 0;
        if (const size_t at = label.find("#logger0"); at != std::string::npos) logger = 2, label.erase(at, 8);
        if (const size_t at = label.find("#logger"); at != std::string::npos) logger = 1, label.erase(at, 7);
        if (const size_t at = label.find("#follow"); at != std::string::npos) follow = true, label.erase(at, 7);
        if (const size_t at = label.find("#ghost"); at != std::string::npos) ghost = true, label.erase(at, 6);
        if (const size_t at = label.find("#rolling"); at != std::string::npos) rolling = true, label.erase(at, 8);
        if (const size_t at = label.find("#reverse"); at != std::string::npos) reverse = true, label.erase(at, 8);
        if (const size_t at = label.find("#ai"); at != std::string::npos) ai0 = true, label.erase(at, 3);
        if (const size_t at = label.find("#runup"); at != std::string::npos) runup = true, label.erase(at, 6);
        bool pad = label.size() > 4 && label.compare(label.size() - 4, 4, "+pad") == 0;
        if (pad) label.resize(label.size() - 4);
        if (alone) pad = true;
        std::string course_name;
        if (const size_t at = label.find('~'); at != std::string::npos) course_name = label.substr(at + 1), label.resize(at);
        const data::Event* ev = hs.db.event(label);
        if (ev && course_name.empty()) course_name = ev->course;
        const Volume::Entry* cfe = ev ? ctx.disc->volume().find("courses/US/" + course_name) : nullptr;
        if (!cfe) {
            ctx.check(false, label + ": no such event or course");
            continue;
        }
        const Bytes course = ctx.disc->volume().read(*cfe);
        if (alone && !finish) laps = ex::race_config(ByteSpan(ev->bytes.data(), ev->bytes.size()))[1];
        std::vector<std::string> labels;
        for (const std::string& e : ev->entries)
            if (!e.empty() && e != "-" && labels.size() < (alone ? 1u : kind == ex::Conductor::Battle2P ? 2u : 6u)) labels.push_back(e);
        if (ghost && !labels.empty()) labels.resize(2, labels[0]);
        g_scratch.clear(), g_rec_calls.clear(), g_logger_inputs.clear();
        const std::string scenario = "CPU race " + label + (course_name != ev->course ? " on " + course_name : std::string()) +
                                     (worn ? " (worn)" : "") + (ai0 ? ", car 0 by the AI" : runup ? ", car 0's run-up" : pad ? ", car 0 on the pad" : "") +
                                     (kind != ex::Conductor::SinglePlayer ? std::string(", ") + ex::conductor_name(kind) + " conductor" : std::string()) +
                                     (ghost ? ", a ghost car" : "") + (follow ? ", car 0 following the line" : "") +
                                     (logger == 1 ? ", the data logger recording" : logger == 2 ? ", the data logger playing" : "");
        const s32 battle_wear = kind == ex::Conductor::Battle2P ? 1 : 0;
        const s32 battle_catchup = kind == ex::Conductor::Battle2P ? 1 : kind == ex::Conductor::BattleMP ? 2 : 0;
        auto before_init = [&](Ee& e, const race_setup::OrigRace& o) {
            e.w32(o.info + 0xC, u32(battle_wear)), e.w32(o.info + 0x14, u32(battle_catchup));
            if (kind == ex::Conductor::Battle2P || kind == ex::Conductor::BattleMP) e.w32(o.info + 8, u32(laps));
            if (logger) {
                logger_tables(e, o);
                e.w32(o.info + 0x70, 1), e.w32(o.info + 0x74, logger == 1 ? 1 : 0);
            }
            if (kind != ex::Conductor::FreeRun) return;
            const u32 obj = e.alloc(race_object_size(), 64), vt = e.alloc(0x100);
            e.w32(obj, vt), e.w32(obj + 4, o.info);
            e.w32(vt + 8 * 27 + 4, 0x00187548);
            free_run_recorders(e, obj);
            e.w32(o.info + 0x3C, obj);
        };
        race_setup::OrigRace race;
        std::string err;
        if (!race_setup::setup_original_race(ctx.ee, hs.db, hs.gtar, *ev, course, labels, laps, start_hold, race, err, before_init,
                                             race_setup::kConductors[size_t(kind)])) {
            ctx.check(false, scenario + ": " + err);
            continue;
        }
        install_emitter(ctx.ee, race.cond);
        lock.install(ctx.ee, false);
        car0 = race_setup::car_addr(race.cond, 0);
        ctx.ee.hle(kPadRead,
                   [&](Ee& x) {
                       const int i = std::clamp(int((x.g32(17) - car0) / car_stride()), 0, 7); // 0x1E4D38's $s1 is the car
                       u8 f[16] = {};
                       if (frame_fn) frame_fn(i, tick, f);
                       x.write_bytes(u32(x.a(1)), ByteSpan(f, 16));
                   },
                   "pad read (the lockstep's frames)");
        // phase12_race_settings pokes the force feedback bytes through ti::built() after the initialisation: ours is built
        // from the same tables.
        ti::built().reset(*prog, kind, *ev, ti::course_category(hs.db, *ev), battle_wear, battle_catchup);
        const int ri = race_index++;
        ex::ProgramData data = *prog;
        const u8* ff = race_ffb(ri);
        poke_table(data, 0x002D465D, ff[0]), poke_table(data, 0x002D465F, ff[1]), poke_table(data, 0x002D4661, ff[2]);
        ex::RaceConfig rules = ex::race_config(ByteSpan(ev->bytes.data(), ev->bytes.size()));
        if (band_mode >= 0) rules[0x22] = u8(band_mode);
        try {
            const ex::RaceSetup s = lockstep_setup(ctx.ee, race.cond, hs, *ev, ByteSpan(course.data(), course.size()), labels, kind, data,
                                                   band_mode >= 0 ? &rules : nullptr);
            if (!lock.begin(ctx.ee, race.cond, s, scenario)) continue;
        } catch (const std::exception& e) {
            ctx.check(false, scenario + ": our setup: " + e.what());
            continue;
        }
        // the scenario's changes after the initialisation (race_tick_exact's)
        Ee& e = ctx.ee;
        if (worn)
            for (int i = 0; i < race.ncars; ++i) {
                const u32 c = race_setup::car_addr(race.cond, i);
                for (u32 w = 0; w < 4; ++w) e.wf(c + ce::kWheel0 + ce::kWheelSize * w + 0xA0, float(150000 + 3000 * i));
                if (i == 0) e.w8(c + ce::kF + f_off(0x3E9), 1);
                if (i == race.ncars - 1) e.w8(c + ce::kF + f_off(0x3E9), 0);
            }
        if (pad) e.w8(car0 + ce::kF + f_off(0x3E9), ai0 ? 2 : 0), e.w8(car0 + ce::kF + f_off(0x534), runup ? 1 : 0);
        if (ghost && race.ncars == 2) {
            const u32 car1 = race_setup::car_addr(race.cond, 1);
            e.w8(car1 + ce::kF + f_off(0x3EA), 1), e.w8(car1 + ce::kF + f_off(0x3E9), 2);
        }
        Follower follower;
        if (follow)
            for (const ex::AiPoint& q : lock.race()->course().ai.lines[0]) follower.line.push_back({double(q.x.host()), double(q.y.host())});
        if (band_mode >= 0) e.w8(race.cond + cond_off(0xE340) + 0x22, u8(band_mode));
        if (rolling) e.w32(race.info + 0x54, 3), e.w16(car0 + car_off(0x16CE), 120), lock.race()->set_start_type(3);
        if (flow_gate) {
            e.w8(race.cond + race_setup::kK8 + k_off(0x37AC), 1), e.wf(race.cond + race_setup::kK8 + k_off(0x37B0), 0.37f);
            lock.race()->set_flow_byte(1), lock.race()->set_time_scale(ee::F(0.37f));
        }
        phase12_race_settings(e, race.cond, race.ncars, ri);
        if (label == seeded) {
            const float condition[6][2] = {{-400000, -200000}, {-150000, -50000}, {100000, 50000}, {550000, -99999}, {700000, 300000}, {0, 0}};
            const float wear[3][4] = {{-30000, -20000, -5000, 0}, {20000, 40000, 100000, 150000}, {180000, 209000, 210000, 250000}};
            for (int i = 0; i < race.ncars && i < 6; ++i) {
                const u32 C = race_setup::car_addr(race.cond, i), F = C + ce::kF;
                e.wf(C + car_off(0x1380), condition[i][0]), e.wf(C + car_off(0x1384), condition[i][1]);
                for (u32 w = 0; i < 3 && w < 4; ++w) e.wf(C + ce::kWheel0 + ce::kWheelSize * w + 0xA0, wear[i][w]);
                if (i < 2) e.w8(F + f_off(0x51D), i == 0 ? 45 : 0xFF);
                if (i == 2) e.w16(F + f_off(0x536), 30);
            }
        }
        frame_fn = [&](int i, int t, u8* f) {
            if (worn) f[0] = 0x1F, f[1] = 2;
            if (!pad || i != 0) return;
            const int k = std::max(0, t - start_hold);
            if (follow) follower.frame(ctx.ee, car0, f);
            else if (!reverse) rh::pad_race_frame(k, f);
            else
                put_frame(f, 0, u16(k < 180 || k >= 1260 ? 4096 : 0), u16(k >= 180 && k < 360 ? 4096 : 0), u16(k >= 360 && k < 1260 ? 4096 : 0));
        };
        const int follow_ticks = int(rh::env_num("GT3R_TICK_FOLLOW_SECONDS", 240) * 60);
        const int len = start_hold + (worn ? worn_ticks : follow ? follow_ticks : finish || ai0 ? finish_ticks : ai_ticks);
        for (int t = 0; t < len; ++t) {
            tick = t;
            e.w32(race.info + 0x5C, u32(std::max(0, start_hold - t)));
            for (const Poke& k : kPokes)
                if (label == k.event && t - start_hold == k.tick && k.car < race.ncars) k.apply(e, race_setup::car_addr(race.cond, k.car));
            logger_frames(e);
            lock.entry(e);
            try {
                e.call(race_setup::kCondStep, {Arg::ptr(race.cond), Arg::integer(0)}, 500'000'000);
            } catch (const std::exception& x) {
                ctx.check(false, rh::fmt("%s: step %d: the original: %s", scenario.c_str(), t, x.what()));
                break;
            }
            if (!lock.exit(ctx)) break;
        }
        lock.end();
        frame_fn = nullptr;
        g_logger_inputs.clear();
    }
    // ---- the two-car pad races (race_hits: the cars placed on a lane, pad frames per car) ----
    if (rh::env_num("GT3R_LOCK_PAD", 1) != 0) {
        const int ticks = int(rh::env_num("GT3R_CARHIT_TICKS", rh::kSettle + 150));
        const data::Event* run_ev = hs.ev;
        const Bytes* run_course = &hs.course_file;
        std::string scenario;
        rh::g_install = [&](Ee& e) {
            const u32 cond = rh::g_car[0] - race_setup::kCarBase;
            install_emitter(e, cond);
            lock.install(e, true);
            try {
                const ex::RaceSetup s = lockstep_setup(e, cond, hs, *run_ev, ByteSpan(run_course->data(), run_course->size()), {hs.car, hs.car},
                                                       ex::Conductor::SinglePlayer, *prog, nullptr);
                lock.begin(e, cond, s, scenario);
            } catch (const std::exception& x) {
                ctx.check(false, scenario + ": our setup: " + x.what());
            }
        };
        rh::g_after_tick = [&](Ee&, int) { return lock.exit(ctx); };
        auto run = [&](const rh::Scenario& sc, const rh::Lane& lane, const track::Track& trk, int n) {
            rh::Extents ext;
            rh::Run o = rh::run_original(ctx.ee, hs.db, hs.gtar, *run_ev, *run_course, hs.car, trk, lane, sc, n, ext);
            lock.end();
            if (!o.ok) ctx.check(false, scenario + ": " + o.error);
        };
        int idx = 0;
        for (const rh::Scenario& sc : rh::car_hit_scenarios()) {
            const int si = idx++;
            if (sc.wall && !hs.wl.ok) continue;
            scenario = rh::fmt("pad race %d %s", si, sc.name);
            run(sc, sc.wall ? hs.wl : hs.ln, *hs.trk, ticks);
        }
        for (const rh::Scenario& sc : rh::tick_scenarios()) {
            scenario = rh::fmt("pad race %d %s", idx++, sc.name);
            run(sc, hs.ln, *hs.trk, ticks);
        }
        for (const rh::Scenario& sc : rh::pad_scenarios()) {
            scenario = rh::fmt("pad race %d %s", idx++, sc.name);
            run(sc, hs.ln, *hs.trk, ticks);
        }
        for (const rh::Scenario& sc : placed_drives()) {
            scenario = sc.name;
            run(sc, hs.ln, *hs.trk, ticks);
        }
        if (rh::env_num("GT3R_CARHIT_SWEEP", 0) != 0 && hs.wl.ok)
            for (double deg : {3.0, 5.0, 7.0, 9.0, 12.0})
                for (double along : {-3.0, -1.5, 0.0, 1.5, 3.0})
                    for (double kmh : {80.0, 110.0}) {
                        scenario = rh::fmt("sweep %.0f deg, %.1f m, %.0f km/h", deg, along, kmh);
                        run(rh::Scenario{"sweep", -1.0, deg, kmh, kmh, along, 0.555, 0.555, true}, hs.wl, *hs.trk, ticks);
                    }
        scenario = "rolling back on full throttle";
        run(rh::Scenario{"rolling back", 3.0, 0, -30, -30, 0.0, 1.0, 1.0}, hs.ln, *hs.trk, ticks);
        // race_tick_exact's pad manoeuvres (the frames and the record writes of its g_after_tick, after ours)
        if (rh::env_num("GT3R_TICK_MANOEUVRES", 1) != 0) {
            struct Pad {
                int from;
                s16 steer;
                u16 throttle, brake, back, hand;
            };
            static const Pad pads[2][8] = {
                {{0, 0, 1229, 0, 0, 0}, {60, 0, 0, 4096, 0, 0}, {220, 0, 0, 0, 4096, 0}, {400, 4096, 0, 0, 4096, 0}, {460, 4096, 0, 0, 0, 4096},
                 {520, 0, 0, 4096, 0, 0}, {-1, 0, 0, 0, 0, 0}, {-1, 0, 0, 0, 0, 0}},
                {{0, 0, 1229, 0, 0, 0}, {30, 0, 0, 0, 0, 4096}, {70, 0, 0, 4096, 0, 0}, {200, 0, 0, 0, 0, 0}, {420, 0, 0, 0, 4096, 0},
                 {600, -4096, 0, 0, 4096, 0}, {660, -4096, 0, 0, 0, 4096}, {720, 0, 0, 4096, 0, 0}},
            };
            rh::g_after_tick = [&](Ee& e, int k) {
                if (!lock.exit(ctx)) return false;
                const int nk = k + 1;
                for (int i = 0; i < 2; ++i)
                    for (const Pad& p : pads[i])
                        if (p.from == nk) {
                            u8* f = rh::g_frame[i];
                            std::memcpy(f + 4, &p.steer, 2), std::memcpy(f + 8, &p.throttle, 2), std::memcpy(f + 0xA, &p.brake, 2);
                            std::memcpy(f + 0xC, &p.back, 2), std::memcpy(f + 0xE, &p.hand, 2);
                        }
                const u32 F1 = rh::g_car[1] + ce::kF;
                if (nk == 200) e.w8(F1 + f_off(0x534), 8);
                if (nk == 300) e.w8(F1 + f_off(0x534), 0), e.w16(F1 + f_off(0x536), 120);
                return true;
            };
            scenario = "pad manoeuvres";
            run(rh::Scenario{"manoeuvres", 1.0, 0, 60, 60, 0.0, 0.3, 0.3}, hs.ln, *hs.trk, 780);
            rh::g_after_tick = [&](Ee&, int) { return lock.exit(ctx); };
        }
        // A contact race with the longitudinal force's config bytes 0x2D2936 / 7 at 2 (program data: ours from the same tables).
        {
            ex::ProgramData poked = *prog;
            poke_table(poked, 0x002D2936, 2), poke_table(poked, 0x002D2937, 2);
            const ex::ProgramData* plain = prog;
            prog = &poked;
            const auto install = rh::g_install;
            rh::g_install = [&](Ee& e) {
                e.w8(0x002D2936, 2), e.w8(0x002D2937, 2);
                install(e);
            };
            scenario = "pad race 0 with the force config bytes at 2";
            run(rh::car_hit_scenarios()[0], hs.ln, *hs.trk, ticks);
            rh::g_install = install;
            prog = plain;
        }
        // A run down Grand Valley's pit lane (as race_tick_exact's).
        if (const data::Event* gv = hs.db.event("Ggtworld_n_1010")) {
            if (const Volume::Entry* ve = ctx.disc->volume().find("courses/US/" + gv->course)) {
                const Bytes file = ctx.disc->volume().read(*ve);
                const course::Road road = course::Road::parse(course::CourseFile::parse(file).section(file, 28));
                const ex::CourseGround ground(road);
                const rh::Lane lane = pit_lane(road, ground);
                std::unique_ptr<track::Track> trk = track::Track::from_course_file(file, gv->course);
                run_ev = gv, run_course = &file;
                scenario = "pad race: the pit lane";
                if (lane.ok && trk) run(rh::Scenario{"pit lane", 0.0, 0, 60, 60, std::nan(""), 0.3, 0.3}, lane, *trk, ticks);
                run_ev = hs.ev, run_course = &hs.course_file;
            }
        }
        rh::g_install = nullptr;
        rh::g_after_tick = nullptr;
    }
    // ---- race_tick_exact's scripted pad drives (one car alone on the Test Course) ----
    const data::Event* dev = hs.db.event("Gturbo_e_0002");
    const Volume::Entry* dce = dev ? ctx.disc->volume().find("courses/US/" + dev->course) : nullptr;
    if (dce && rh::env_num("GT3R_LOCK_PAD", 1) != 0) {
        const Bytes dcourse = ctx.disc->volume().read(*dce);
        for (const PadDrive& pd : kPadDrives) {
            race_setup::OrigRace race;
            std::string err;
            const std::string scenario = rh::fmt("pad drive %s (%s, style %d)", pd.name, pd.car, pd.style);
            if (!race_setup::setup_original_race(ctx.ee, hs.db, hs.gtar, *dev, dcourse, {pd.car}, 2, 0, race, err)) {
                ctx.check(false, scenario + ": " + err);
                continue;
            }
            install_emitter(ctx.ee, race.cond);
            lock.install(ctx.ee, false);
            static u8 frame[16];
            ctx.ee.hle(kPadRead, [](Ee& e) { e.write_bytes(u32(e.a(1)), ByteSpan(frame, 16)); }, "pad read (scripted)");
            ex::ProgramData data = *prog;
            if (pd.gear_mode >= 0) poke_table(data, kControllerSettings.ntsc_u + 0x14, u8(pd.gear_mode));
            try {
                const ex::RaceSetup s = lockstep_setup(ctx.ee, race.cond, hs, *dev, ByteSpan(dcourse.data(), dcourse.size()), {pd.car},
                                                       ex::Conductor::SinglePlayer, data, nullptr);
                if (!lock.begin(ctx.ee, race.cond, s, scenario)) continue;
            } catch (const std::exception& e) {
                ctx.check(false, scenario + ": our setup: " + e.what());
                continue;
            }
            const u32 car = race_setup::car_addr(race.cond, 0), F = car + race_setup::kF;
            ctx.ee.w8(F + f_off(0x3E9), 0), ctx.ee.w8(F + f_off(0x534), 0), ctx.ee.w8(F + f_off(0x461), pd.style);
            if (pd.poke) pd.poke(ctx.ee, car);
            if (pd.gear_mode >= 0) ctx.ee.w8(kControllerSettings + 0x14, u8(pd.gear_mode));
            Script script;
            for (int t = 0; !script.done() && t < 60 * 60; ++t) {
                drive_frame(script.step(t, ctx.ee.rf(F + f_off(0x488)), ctx.ee.r8(F + f_off(0x436)), pd.style), frame);
                ctx.ee.w32(race.info + 0x5C, 0);
                lock.entry(ctx.ee);
                try {
                    ctx.ee.call(race_setup::kCondStep, {Arg::ptr(race.cond), Arg::integer(0)}, 500'000'000);
                } catch (const std::exception& x) {
                    ctx.check(false, rh::fmt("%s: step %d: the original: %s", scenario.c_str(), t, x.what()));
                    break;
                }
                if (!lock.exit(ctx)) break;
            }
            lock.end();
        }
    }
    {
        long frames = 0;
        std::string first;
        check_countdown(ctx, frames, first);
        ctx.note(rh::fmt("the start countdown 0x17A5E8 against race_info_frame: %ld frames, ", frames) + (first.empty() ? "all equal" : "first difference: " + first));
        ctx.check(first.empty(), "start countdown: " + first);
    }
    lock.report(ctx);
}

// ---- GT3's ghosts on its own recorders (race_ghost_exact) ------------------------------------------------------------------
// The solo modes' ghosts with the original's own recorders: the race object's input devices (RaceInput, 0x2104E8) and record
// memory (two pools of 0x20000, [0x3529B0] / [0x3529B4]) built in the original's memory as RaceSolitaire's constructor
// (0x186E10) builds them (RaceFreeRun's 0x189138: its devices' mode 2), each run started as its virtuals 9 and 10 start it
// (0x1F52B0, 0x210658, 0x17A970's restarts) and ended as the race's end ends it (0x17AA48, RaceSolitaire::vf36: 0x1F4C00). The
// pad read 0x210740 runs: the player's device (car 0, live) records the line follower's frame (the pads' read 0x211430 gives it),
// the ghost's (car 1, playing) decodes its own. The free run's recorder calls run too (0x2106B0, 0x2106D8, 0x210700, 0x17AAD8,
// 0x1F1400's 0x1F52B0, 0x210668; the Options' Ghost on: [0x352A94] + 0x2FF38 + 0x28). Ours: ExactRace with SoloRecorders, car 0
// fed the original's frame, car 1's from its own device (RaceSetup::ghost_car). Per race mode: run 1 records a run (every step
// in lockstep; then the record memory and the devices' recorders compared byte for byte); run 2 races its ghost (the time trial,
// licence test and machine test: the whole run, 0x17AAD8's kind 0; the free run: run 1's ghost carried, the visit's next run),
// every car every step in lockstep, the free run ghost's extra steps among them. Options: GT3R_GHOST_EVENTS
// (<event>@<mode>,...), GT3R_GHOST_SECONDS (each run's length, default 240), GT3R_GHOST_PACE (the follower's speed scale,
// default 1: a licence run slower than bronze at 0.4).
namespace {

constexpr Pa kRaceInputCtor = 0x002104E8, kRecManagerCtor = 0x001F4ED0, kRecBindPool = 0x001F4F78, kRecBindStore = 0x001F4FF0,
             kRecStoreHeader = 0x001F5188, kRecRestart = 0x001F52B0, kRecPlayMode = 0x00210658, kRecClose = 0x001F4C00,
             kPadsRead = 0x00211430, kWheelRead = 0x002115F8;
constexpr Pa kRecPoolBase = 0x003529B0, kRecPoolSize = 0x003529B4, kGameStatus = 0x00352A94;

// The race object's devices in the original's memory.
struct OrigSolo {
    u32 pools = 0, obj = 0, P = 0, G = 0, M1 = 0, M2 = 0;
};

// Built as RaceSolitaire's constructor builds them on race info `info` of conductor `cond`, the record memory `memory` (both
// pools: a visit's run after the first; none: cleared, the best lap's store "no time") and the ghost device's +0x140; then a
// run's start.
OrigSolo build_solo(Ee& e, u32 info, u32 cond, bool free_run, const std::vector<u8>* memory, bool ghost_empty) {
    OrigSolo s;
    s.pools = e.alloc(2 * ex::kRecordPool, 64);
    const std::vector<u8> zero(2 * ex::kRecordPool, 0);
    e.write_bytes(s.pools, ByteSpan(memory ? memory->data() : zero.data(), zero.size()));
    e.w32(kRecPoolBase, s.pools), e.w32(kRecPoolSize, ex::kRecordPool);
    const u32 status = e.alloc(0x30000, 16); // the game's status: the Options menu's Ghost on
    e.w32(status + 0x2FF38 + 0x28, 1);
    e.w32(kGameStatus, status);
    s.obj = e.alloc(race_object_size(), 64);
    const u32 vt = e.alloc(0x100);
    e.w32(s.obj, vt), e.w32(s.obj + 4, info), e.w32(s.obj + 8, cond);
    e.w32(vt + 8 * 27 + 4, 0x00187548); // virtual 27: the devices
    e.w32(info + 0x3C, s.obj);
    for (u32 o : {0xC8C0u, 0xCA08u, 0xCB50u, 0xCC98u}) e.call(kRaceInputCtor, {Arg::ptr(s.obj + o)});
    s.P = s.obj + 0xC8C0, s.G = s.obj + 0xCA08, s.M2 = s.obj + 0x216E4, s.M1 = s.obj + 0x21744;
    e.call(kRecManagerCtor, {Arg::ptr(s.M2)}), e.call(kRecManagerCtor, {Arg::ptr(s.M1)});
    e.w32(s.P + 0xC4 + 4, 0), e.call(kRecBindPool, {Arg::ptr(s.P + 0xC4), Arg::integer(0)});
    e.w32(s.M1 + 4, 2), e.call(kRecBindPool, {Arg::ptr(s.M1), Arg::integer(1)}), e.call(kRecBindStore, {Arg::ptr(s.M1), Arg::integer(1)});
    e.w32(s.P + 0x128, s.M1);
    e.w32(s.M2 + 4, 2), e.call(kRecBindPool, {Arg::ptr(s.M2), Arg::integer(1)}), e.call(kRecBindStore, {Arg::ptr(s.M2), Arg::integer(2)});
    if (!memory) e.w32(u32(e.call(kRecStoreHeader, {Arg::ptr(s.M2)}).v0.lo) + 0x10, ex::kNoTime);
    e.w32(s.P + 0x124, s.M2);
    e.w32(s.G + 0xC4 + 4, 0), e.call(kRecBindPool, {Arg::ptr(s.G + 0xC4), Arg::integer(1)});
    e.w32(s.obj + 0xCB50 + 0xC8, 0), e.w32(s.obj + 0xCC98 + 0xC8, 0);
    e.w32(s.G + 0x140, ghost_empty ? 1 : 0), e.w32(s.obj + 0xCB50 + 0x140, 1), e.w32(s.obj + 0xCC98 + 0x140, 1);
    e.w32(s.obj + 0x217A4, 1), e.w32(s.obj + 0x216E0, 0), e.w32(s.obj + 0x217A8, 0);
    if (free_run) e.w32(s.G + 0xC4 + 4, 2), e.w32(s.obj + 0xCB50 + 0xC4 + 4, 2), e.w32(s.obj + 0xCC98 + 0xC4 + 4, 2);
    // the run's start: RaceSolitaire::vf9 (the lap and best lap's recorders), RaceFreeRun::vf9 (the ghost on store 0, where the
    // visit's first run bound it), RaceSolitaire::vf10 (the play modes), RaceBase::vf10's 0x17A970 (the entries' devices)
    e.call(kRecRestart, {Arg::ptr(s.M1)}), e.call(kRecRestart, {Arg::ptr(s.M2)});
    if (free_run) e.call(kRecBindStore, {Arg::ptr(s.G + 0xC4), Arg::integer(0)});
    e.call(kRecPlayMode, {Arg::ptr(s.P), Arg::integer(0)}), e.call(kRecPlayMode, {Arg::ptr(s.obj + 0xCB50), Arg::integer(0)});
    e.call(kRecPlayMode, {Arg::ptr(s.G), Arg::integer(1)});
    e.call(kRecRestart, {Arg::ptr(s.P + 0xC4)}), e.call(kRecRestart, {Arg::ptr(s.G + 0xC4)});
    return s;
}

// The race's end: every device's recording (0x17AA48) and the lap and best lap's (RaceSolitaire::vf36) closed.
void end_solo(Ee& e, const OrigSolo& s) {
    for (u32 r : {s.P + 0xCC, s.G + 0xCC, s.M1 + 8, s.M2 + 8}) e.call(kRecClose, {Arg::ptr(r)});
}

// A recorder's words (the original's at r, its memory at `base`) against ours.
std::string rec_differ(Ee& ee, u32 r, u32 base, const ex::ControlRecorder& o) {
    auto cmp = [&](const char* what, u32 a, u32 b) { return a == b ? std::string() : rh::fmt("%s 0x%X vs 0x%X", what, b, a); };
    for (const std::string& d : {cmp("stream at", ee.r32(r + 4), base + o.ptr), cmp("byte", ee.r8(r + 8), o.cur), cmp("bits", ee.r32(r + 0xC), o.bits),
                                 cmp("steer before", ee.r32(r + 0x14), u32(o.steer_before)), cmp("done", ee.r32(r + 0x28), o.done ? 1 : 0),
                                 cmp("written", ee.r32(r + 0x2C), o.written ? 1 : 0), cmp("count", ee.r32(r + 0x30), o.count),
                                 cmp("run", ee.r32(r + 0x34), o.run), cmp("block", ee.r32(r + 0x38), o.bound ? base + o.block : 0),
                                 cmp("capacity", ee.r32(r + 0x3C), o.capacity)})
        if (!d.empty()) return d;
    for (u32 k = 0; k < 16; ++k)
        if (ee.r8(r + 0x18 + k) != o.last[k]) return rh::fmt("last frame +0x%X", k);
    return {};
}

// The record memory and the devices' recorders after a run, ours against the original's (the pool 1 stores' snapshots by
// their records).
std::string solo_differ(Ee& ee, const OrigSolo& s, const ex::SoloRecorders& o, std::vector<u8>& memory) {
    memory.assign(2 * ex::kRecordPool, 0);
    ee.read_bytes(s.pools, memory.data(), u32(memory.size()));
    for (u32 k = 0; k < ex::kRecordPool; ++k)
        if (memory[k] != o.pool0[k]) return rh::fmt("pool 0 +0x%X: 0x%02X vs 0x%02X", k, o.pool0[k], memory[k]);
    for (u32 k = 0; k < ex::kRecordPool; ++k) {
        if (o.free_run && k % ex::kRecordStore < ex::kStoreSnapshot) continue; // (the stores' snapshots: by their records)
        if (memory[ex::kRecordPool + k] != o.pool1[k]) return rh::fmt("pool 1 +0x%X: 0x%02X vs 0x%02X", k, o.pool1[k], memory[ex::kRecordPool + k]);
    }
    if (o.free_run)
        for (u32 j = 0; j < 3; ++j) {
            const ex::Snapshot t = read_snapshot(ee, s.pools + ex::kRecordPool + j * ex::kRecordStore);
            if (o.snapshot[j].time != t.time) return rh::fmt("store %u's time %u vs %u", j, o.snapshot[j].time, t.time);
            if (o.snapshot[j].time != ex::kNoTime)
                if (std::string d = snapshot_differ(o.snapshot[j], t, false); !d.empty()) return rh::fmt("store %u's snapshot: ", j) + d;
        }
    const std::pair<const char*, std::pair<u32, const ex::ControlManager*>> recs[] = {
        {"the player's", {s.P + 0xCC, &o.player}}, {"the ghost's", {s.G + 0xCC, &o.ghost}}, {"the lap's", {s.M1 + 8, &o.lap}}, {"the best lap's", {s.M2 + 8, &o.best}}};
    for (const auto& [name, r] : recs)
        if (std::string d = rec_differ(ee, r.first, s.pools + (r.second->pool == 1 ? ex::kRecordPool : 0), r.second->rec); !d.empty())
            return std::string(name) + " recorder: " + d;
    if ((ee.r32(s.G + 0x140) != 0) != o.ghost_empty) return "the ghost's +0x140";
    return {};
}

} // namespace

DIFF_TEST(race_ghost_exact, "The solo modes' ghosts on the original's own recorders and playback vs sim::exact::SoloRecorders, every step") {
    rh::HitSetup hs;
    if (!rh::load_hit_setup(ctx, hs)) return;
    std::string perr;
    const ex::ProgramData* prog = start_setup::program_data(ctx, perr);
    if (!prog) return ctx.check(false, "program tables: " + perr);
    Lockstep lock;
    const std::string events = std::getenv("GT3R_GHOST_EVENTS") ? std::getenv("GT3R_GHOST_EVENTS")
                                                                  : "AT020001@tt,L0A0001@licence,M400@machine,AT040001@freerun";
    const int run_ticks = int(rh::env_num("GT3R_GHOST_SECONDS", 240) * 60);
    const int start_hold = int(rh::env_num("GT3R_TICK_HOLD", 90));
    long runs = 0, recordings = 0, ghost_steps = 0, ghost_reads = 0, restarts = 0, extra = 0, bad = 0;
    std::string first;
    auto fail = [&](const std::string& d) {
        if (++bad == 1) first = d;
    };
    for (size_t pos = 0; events != "-" && pos < events.size();) {
        size_t end = events.find(',', pos);
        if (end == std::string::npos) end = events.size();
        std::string label = events.substr(pos, end - pos);
        pos = end + 1;
        ex::Conductor kind = ex::Conductor::TimeTrial;
        if (const size_t at = label.find('@'); at != std::string::npos) {
            const std::string m = label.substr(at + 1);
            label.resize(at);
            kind = m == "licence" ? ex::Conductor::License : m == "machine" ? ex::Conductor::MachineTest : m == "freerun" ? ex::Conductor::FreeRun
                                                                                                          : ex::Conductor::TimeTrial;
        }
        const bool fr = kind == ex::Conductor::FreeRun;
        const data::Event* ev = hs.db.event(label);
        const Volume::Entry* cfe = ev ? ctx.disc->volume().find("courses/US/" + ev->course) : nullptr;
        if (!cfe || ev->entries.empty()) {
            ctx.check(false, label + ": no such event or course");
            continue;
        }
        const Bytes course = ctx.disc->volume().read(*cfe);
        const int laps = ex::race_config(ByteSpan(ev->bytes.data(), ev->bytes.size()))[1];
        std::vector<u8> memory;        // the original's record memory after the last run
        bool ghost_empty = true;       // its ghost device's +0x140
        ex::SoloRecorders ours(fr);    // ours after the last run
        std::vector<u8> run_recording; // ours: the last run's recording
        for (int run = 0; run < 2; ++run) {
            // run 1 records (a free run: with its ghost car from the start, empty until a lap becomes the ghost); run 2 races
            // run 1's ghost (the free run: the visit's next run), its player slower
            const bool with_ghost = fr || run == 1;
            const std::vector<std::string> labels(with_ghost ? 2 : 1, ev->entries[0]);
            const std::string scenario = rh::fmt("%s (%s), run %d", label.c_str(), ex::conductor_name(kind), run + 1);
            OrigSolo solo;
            auto before_init = [&](Ee& e, const race_setup::OrigRace& o) {
                if (run == 1 && !fr) std::copy_n(memory.begin(), ex::kRecordPool, memory.begin() + ex::kRecordPool); // 0x17AAD8 (0)
                solo = build_solo(e, o.info, o.cond, fr, run == 0 ? nullptr : &memory, run == 0 ? true : fr ? ghost_empty : false);
                const u32 list = e.r32(o.info + 0x18), arr = e.r32(list + 8);
                e.w32(e.r32(arr) + entry_off(0x20DC), solo.P);
                if (with_ghost) e.w32(e.r32(arr + 4) + entry_off(0x20DC), solo.G);
            };
            race_setup::OrigRace race;
            std::string err;
            if (!race_setup::setup_original_race(ctx.ee, hs.db, hs.gtar, *ev, course, labels, laps, start_hold, race, err, before_init,
                                                 race_setup::kConductors[size_t(kind)])) {
                fail(scenario + ": " + err);
                break;
            }
            Ee& e = ctx.ee;
            install_emitter(e, race.cond);
            lock.install(e, false);
            const u32 car0 = race_setup::car_addr(race.cond, 0);
            Follower follower;
            follower.pace = (run == 0 ? 1.0 : 0.85) * rh::env_num("GT3R_GHOST_PACE", 1.0);
            e.hle(kPadsRead, [&, P = solo.P](Ee& x) {
                if (u32(x.a(0)) == P) follower.frame(x, car0, x.host_ptr(u32(x.a(1)), 16));
            }, "the pads' read (the line follower's frame)");
            e.hle(kWheelRead, [](Ee&) {}, "the steering wheel's read (none)");
            // the free run's recorder calls (running), for the lockstep's check of our recorder events
            g_rec_calls.clear();
            e.add_probe(pa(0x002106D8), [](Ee& x) { g_rec_calls.push_back({ex::RecorderEvent::close, u32(x.a(0))}); });
            e.add_probe(pa(0x00210700), [](Ee& x) { g_rec_calls.push_back({ex::RecorderEvent::best, u32(x.a(0))}); });
            e.add_probe(pa(0x0017AAD8), [](Ee& x) { g_rec_calls.push_back({ex::RecorderEvent::ghost, u32(x.a(0)), u32(x.a(1)), u32(x.a(2))}); });
            e.add_probe(pa(0x001F52B0), [](Ee& x) {
                if (u32(x.gpr[31].lo) == pa(0x001F146C)) g_rec_calls.push_back({ex::RecorderEvent::ghost_restart, u32(x.a(0))}); // (0x1F1400's)
            });
            e.add_probe(pa(0x00210668), [](Ee& x) { g_rec_calls.push_back({ex::RecorderEvent::lap_start, u32(x.a(0))}); });
            ti::built().reset(*prog, kind, *ev, ti::course_category(hs.db, *ev), 0, 0);
            // ours: the recorders as the run starts (run 2: the ghost's recording on its device, or the visit's carried)
            ex::SoloRecorders start = run == 0 ? ex::SoloRecorders(fr) : ours;
            if (run == 1 && !fr) start.run_to_ghost(run_recording);
            start.start();
            try {
                ex::RaceSetup s = lockstep_setup(e, race.cond, hs, *ev, ByteSpan(course.data(), course.size()), labels, kind, *prog, nullptr);
                s.recorders = std::make_shared<const ex::SoloRecorders>(std::move(start));
                s.ghost_car = with_ghost ? 1 : -1;
                s.ghost_switch = 1;
                if (!lock.begin(e, race.cond, s, scenario)) {
                    fail(scenario + ": " + lock.first);
                    break;
                }
            } catch (const std::exception& x) {
                fail(scenario + ": our setup: " + x.what());
                break;
            }
            // the follower on the AI's race line (race_tick_exact's #follow), else the road's centre line
            for (const ex::AiPoint& q : ti::built().ai.lines[0]) follower.line.push_back({double(q.x.host()), double(q.y.host())});
            if (follower.line.size() < 4) {
                const course::CourseFile cf = course::CourseFile::parse(course);
                for (const course::Vec3& q : course::Road::parse(cf.section(course, 28)).centre_line()) follower.line.push_back({q.x, -q.z});
            }
            phase12_race_settings(e, race.cond, race.ncars, 0);
            bool ok = true;
            const long bad0 = lock.bad;
            // (run 2 ten seconds longer: the ghost past its end; a free run twice as long: laps after its ghost)
            const int ticks = start_hold + (fr ? 2 * run_ticks : run_ticks) + (run == 1 ? 600 : 0);
            for (int t = 0; t < ticks && ok; ++t) {
                e.w32(race.info + 0x5C, u32(std::max(0, start_hold - t)));
                lock.entry(e);
                const u32 g0 = with_ghost ? e.r32(solo.G + 0xCC + 0x30) : 0;
                try {
                    e.call(race_setup::kCondStep, {Arg::ptr(race.cond), Arg::integer(0)}, 500'000'000);
                } catch (const std::exception& x) {
                    fail(rh::fmt("%s: step %d: the original: %s", scenario.c_str(), t, x.what()));
                    ok = false;
                    break;
                }
                ok = lock.exit(ctx);
                if (std::getenv("GT3R_GHOST_TRACE") && t % 300 == 0)
                    std::fprintf(stderr, "%s t %d: car 0 laps %d control %d kind %d, recorded %u (stream +0x%X), last steer %d throttle %u\n",
                                 scenario.c_str(), t, s32(s16(e.r16(car0 + ce::kF + f_off(0x434)))), s32(s8(e.r8(car0 + ce::kF + f_off(0x3E9)))),
                                 e.r8(car0 + ce::kF + f_off(0x534)), e.r32(solo.P + 0xCC + 0x30), e.r32(solo.P + 0xCC + 4) - e.r32(solo.P + 0xCC),
                                 s32(s16(e.r16(solo.P + 0xCC + 0x1C))), e.r16(solo.P + 0xCC + 0x20));
                if (const u32 g1 = with_ghost ? e.r32(solo.G + 0xCC + 0x30) : 0; with_ghost && (run == 1 || fr))
                    ++ghost_steps, ghost_reads += long(g1 >= g0 ? g1 - g0 : g1);
                if (ok && lock.race())
                    for (const ex::RecorderEvent& r : lock.race()->flow().recorder)
                        if (r.kind == ex::RecorderEvent::ghost_restart) ++restarts, extra += r.extra ? 1 : 0;
            }
            if (lock.bad != bad0) fail(lock.first);
            if (!ok || !lock.race()) {
                lock.end();
                break;
            }
            ex::SoloRecorders after = *lock.race()->recorders();
            if (kind == ex::Conductor::License) // the judged result as the race display reads it (race info + 0x40 / + 0x44)
                ctx.note(rh::fmt("%s: the original's result %d, time %u ms (ours %d, %u)", scenario.c_str(), s32(e.r32(race.info + 0x40)),
                                 e.r32(race.info + 0x44), lock.race()->flow().result, lock.race()->flow().result_time));
            lock.end();
            end_solo(e, solo), after.end();
            ++runs;
            if (std::string d = solo_differ(e, solo, after, memory); !d.empty()) {
                fail(scenario + ": after the run: " + d);
                break;
            }
            ++recordings;
            ghost_empty = e.r32(solo.G + 0x140) != 0;
            ours = after, run_recording = after.run_recording();
            ctx.note(rh::fmt("%s: the recording %u frames, %u bytes; the ghost played %u frames", scenario.c_str(), after.player.rec.count,
                             u32(run_recording.size()), after.ghost.rec.count));
        }
    }
    ctx.note(rh::fmt("%ld runs, %ld record memories the same, %ld steps with a ghost car (%ld frames played, %ld ghost restarts, %ld "
                     "extra steps); %ld differing",
                     runs, recordings, ghost_steps, ghost_reads, restarts, extra, bad));
    ctx.check(bad == 0, first);
    ctx.check(runs > 0, "no run");
}

// ---- the Analyzer's race and its REPLAY jumps (analyzer_replay_exact) ------------------------------------------------------
// GT3's Analyzer over two runs of a time trial, licence test and machine test: runs A and B recorded by the original's own
// recorders (as race_ghost_exact's run 1, B's player slower), then the logger's race (RaceSolitaire::vf58's devices, the race
// object's +0xCB50 and +0xCC98, both playing: car 0 run A's recording, car 1, a ghost car, run B's; race info + 0x70 and + 0x74
// set, each device's table at its +0x12C). The pass runs to A's recording's end in lockstep with ours (SoloRecorders::analysis):
// every step, the logger's tables among the race flow. Then the REPLAY view's jumps (0x1873B8 on the race object; ours
// ExactRace::logger_jump) to entries along the lap: the cars, their bodies and their devices' recorders compared at once, then
// GT3R_ANALYZER_STEPS (default 600) steps in lockstep after each. And whether GT3's race after a jump is the pass's again: car
// 0's record (and car 1's) at each device frame against the pass's at that frame (the first frame it differs: GT3's drift).
// Options: GT3R_ANALYZER_EVENTS (<event>@<mode>,...), GT3R_GHOST_SECONDS (the runs' length, default 240).
DIFF_TEST(analyzer_replay_exact, "GT3's Analyzer race (tracks A and B in one race) and its REPLAY jumps vs ours, every step") {
    rh::HitSetup hs;
    if (!rh::load_hit_setup(ctx, hs)) return;
    std::string perr;
    const ex::ProgramData* prog = start_setup::program_data(ctx, perr);
    if (!prog) return ctx.check(false, "program tables: " + perr);
    Lockstep lock;
    const std::string events = std::getenv("GT3R_ANALYZER_EVENTS") ? std::getenv("GT3R_ANALYZER_EVENTS")
                                                                     : "AT020001@tt,L0A0001@licence,M400@machine";
    const int run_ticks = int(rh::env_num("GT3R_GHOST_SECONDS", 240) * 60);
    const int after_steps = int(rh::env_num("GT3R_ANALYZER_STEPS", 600));
    const int start_hold = int(rh::env_num("GT3R_TICK_HOLD", 90));
    long passes = 0, pass_steps = 0, jumps = 0, jump_steps = 0, drifted = 0, bad = 0;
    std::string first, drift_note, labelled;
    auto fail = [&](const std::string& d) {
        if (++bad == 1) first = d;
    };
    for (size_t pos = 0; events != "-" && pos < events.size();) {
        size_t end = events.find(',', pos);
        if (end == std::string::npos) end = events.size();
        std::string label = events.substr(pos, end - pos);
        pos = end + 1;
        ex::Conductor kind = ex::Conductor::TimeTrial;
        if (const size_t at = label.find('@'); at != std::string::npos) {
            const std::string m = label.substr(at + 1);
            label.resize(at);
            kind = m == "licence" ? ex::Conductor::License : m == "machine" ? ex::Conductor::MachineTest : ex::Conductor::TimeTrial;
        }
        const data::Event* ev = hs.db.event(label);
        const Volume::Entry* cfe = ev ? ctx.disc->volume().find("courses/US/" + ev->course) : nullptr;
        if (!cfe || ev->entries.empty()) {
            ctx.check(false, label + ": no such event or course");
            continue;
        }
        const Bytes course = ctx.disc->volume().read(*cfe);
        const int laps = ex::race_config(ByteSpan(ev->bytes.data(), ev->bytes.size()))[1];
        Ee& e = ctx.ee;
        auto line_of = [&](Follower& f) {
            for (const ex::AiPoint& q : ti::built().ai.lines[0]) f.line.push_back({double(q.x.host()), double(q.y.host())});
            if (f.line.size() < 4) {
                const course::CourseFile cf = course::CourseFile::parse(course);
                for (const course::Vec3& q : course::Road::parse(cf.section(course, 28)).centre_line()) f.line.push_back({q.x, -q.z});
            }
        };
        // ---- runs A and B: the original's recordings (and ours of the same frames, checked) ----
        std::array<std::vector<u8>, 2> recording; // the original's pool 0 after each run
        bool ok = true;
        for (int run = 0; run < 2 && ok; ++run) {
            const std::string scenario = rh::fmt("%s (%s), run %c", label.c_str(), ex::conductor_name(kind), run == 0 ? 'A' : 'B');
            OrigSolo solo;
            auto before_init = [&](Ee& x, const race_setup::OrigRace& o) {
                solo = build_solo(x, o.info, o.cond, false, nullptr, true);
                x.w32(x.r32(x.r32(x.r32(o.info + 0x18) + 8)) + entry_off(0x20DC), solo.P);
            };
            race_setup::OrigRace race;
            std::string err;
            if (!race_setup::setup_original_race(e, hs.db, hs.gtar, *ev, course, {ev->entries[0]}, laps, start_hold, race, err, before_init,
                                                 race_setup::kConductors[size_t(kind)])) {
                fail(scenario + ": " + err), ok = false;
                break;
            }
            install_emitter(e, race.cond);
            lock.install(e, false);
            const u32 car0 = race_setup::car_addr(race.cond, 0);
            Follower follower;
            follower.pace = run == 0 ? 1.0 : 0.9;
            e.hle(kPadsRead, [&, P = solo.P](Ee& x) {
                if (u32(x.a(0)) == P) follower.frame(x, car0, x.host_ptr(u32(x.a(1)), 16));
            }, "the pads' read (the line follower's frame)");
            e.hle(kWheelRead, [](Ee&) {}, "the steering wheel's read (none)");
            ti::built().reset(*prog, kind, *ev, ti::course_category(hs.db, *ev), 0, 0);
            try {
                ex::RaceSetup s = lockstep_setup(e, race.cond, hs, *ev, ByteSpan(course.data(), course.size()), {ev->entries[0]}, kind, *prog, nullptr);
                s.recorders = std::make_shared<const ex::SoloRecorders>(ex::SoloRecorders(false));
                s.ghost_switch = 1;
                if (!lock.begin(e, race.cond, s, scenario)) {
                    fail(scenario + ": " + lock.first), ok = false;
                    break;
                }
            } catch (const std::exception& x) {
                fail(scenario + ": our setup: " + x.what()), ok = false;
                break;
            }
            line_of(follower);
            phase12_race_settings(e, race.cond, race.ncars, 0);
            for (int t = 0; t < start_hold + run_ticks && ok; ++t) {
                e.w32(race.info + 0x5C, u32(std::max(0, start_hold - t)));
                lock.entry(e);
                e.call(race_setup::kCondStep, {Arg::ptr(race.cond), Arg::integer(0)}, 500'000'000);
                ok = lock.exit(ctx);
            }
            if (!ok || !lock.race()) {
                fail(lock.first);
                lock.end();
                break;
            }
            ex::SoloRecorders after = *lock.race()->recorders();
            lock.end();
            end_solo(e, solo), after.end();
            std::vector<u8> memory;
            if (std::string d = solo_differ(e, solo, after, memory); !d.empty()) {
                fail(scenario + ": after the run: " + d), ok = false;
                break;
            }
            recording[size_t(run)] = after.run_recording();
        }
        if (!ok) continue;
        // ---- the logger's race: the pass ----
        const std::string scenario = rh::fmt("%s (%s), the Analyzer's race", label.c_str(), ex::conductor_name(kind));
        OrigSolo solo;
        u32 P2 = 0, G2 = 0;
        auto before_init = [&](Ee& x, const race_setup::OrigRace& o) {
            solo = build_solo(x, o.info, o.cond, false, nullptr, true);
            P2 = solo.obj + 0xCB50, G2 = solo.obj + 0xCC98;
            // RaceSolitaire::vf58: the logger's devices on their own memory (here the two pools), both playing
            x.write_bytes(solo.pools, ByteSpan(recording[0].data(), recording[0].size()));
            x.write_bytes(solo.pools + ex::kRecordPool, ByteSpan(recording[1].data(), recording[1].size()));
            for (const auto& [dev, pool] : {std::pair{P2, 0}, std::pair{G2, 1}}) {
                x.w32(dev + 0xC4 + 4, 0), x.call(kRecBindPool, {Arg::ptr(dev + 0xC4), Arg::integer(pool)});
                x.w32(dev + 0x140, 0);
                x.call(kRecPlayMode, {Arg::ptr(dev), Arg::integer(1)});
                x.call(kRecRestart, {Arg::ptr(dev + 0xC4)});
                x.w32(dev + 0x12C, x.alloc(snap_size() * u32(kLoggerEnd + 1), 16) + snap_size()); // its table
            }
            x.w32(solo.obj + 0x217A8, 1); // (the logger's set)
            const u32 arr = x.r32(x.r32(o.info + 0x18) + 8);
            x.w32(x.r32(arr) + entry_off(0x20DC), P2), x.w32(x.r32(arr + 4) + entry_off(0x20DC), G2);
            x.w32(o.info + 0x70, 1), x.w32(o.info + 0x74, 1);
        };
        race_setup::OrigRace race;
        std::string err;
        const std::vector<std::string> two(2, ev->entries[0]);
        if (!race_setup::setup_original_race(e, hs.db, hs.gtar, *ev, course, two, laps, start_hold, race, err, before_init,
                                             race_setup::kConductors[size_t(kind)])) {
            fail(scenario + ": " + err);
            continue;
        }
        install_emitter(e, race.cond);
        lock.install(e, false);
        e.hle(kPadsRead, [](Ee&) {}, "the pads' read (none live)");
        e.hle(kWheelRead, [](Ee&) {}, "the steering wheel's read (none)");
        ti::built().reset(*prog, kind, *ev, ti::course_category(hs.db, *ev), 0, 0);
        try {
            ex::RaceSetup s = lockstep_setup(e, race.cond, hs, *ev, ByteSpan(course.data(), course.size()), two, kind, *prog, nullptr);
            ex::SoloRecorders r(false);
            r.analysis(recording[0], recording[1]);
            s.recorders = std::make_shared<const ex::SoloRecorders>(std::move(r));
            s.ghost_car = 1, s.ghost_switch = 1;
            if (!lock.begin(e, race.cond, s, scenario)) {
                fail(scenario + ": " + lock.first);
                continue;
            }
        } catch (const std::exception& x) {
            fail(scenario + ": our setup: " + x.what());
            continue;
        }
        phase12_race_settings(e, race.cond, race.ncars, 0);
        const u32 car0 = race_setup::car_addr(race.cond, 0), car1 = race_setup::car_addr(race.cond, 1);
        // the pass's cars' motion (position, velocity) by their devices' frames
        using Motion = std::array<u32, 6>;
        auto motion = [&](u32 car) {
            const ex::Car c = ce::read_car(e, car);
            return Motion{c.pos.x.bits, c.pos.y.bits, c.pos.z.bits, c.vel.x.bits, c.vel.y.bits, c.vel.z.bits};
        };
        auto apart = [](const Motion& a, const Motion& b) {
            auto f = [](u32 v) { return double(ee::F::raw(v).host()); };
            return std::hypot(f(a[0]) - f(b[0]), f(a[1]) - f(b[1]), f(a[2]) - f(b[2]));
        };
        std::map<u32, Motion> pass0, pass1;
        int t = 0;
        auto step = [&](const char* what) {
            e.w32(race.info + 0x5C, u32(std::max(0, start_hold - t)));
            lock.entry(e);
            e.call(race_setup::kCondStep, {Arg::ptr(race.cond), Arg::integer(0)}, 500'000'000);
            ++t;
            if (!lock.exit(ctx)) {
                fail(rh::fmt("%s, %s: %s", scenario.c_str(), what, lock.first.c_str()));
                return false;
            }
            return true;
        };
        bool done = false;
        for (int tail = 0; ok && tail < 2;) {
            if (!(ok = step("the pass"))) break;
            ++pass_steps;
            // (by frame while the device plays: past its recording's end its frame count stays)
            if (e.r32(P2 + 0xCC + 0x28) == 0) pass0[e.r32(P2 + 0xCC + 0x30)] = motion(car0);
            if (e.r32(G2 + 0xCC + 0x28) == 0) pass1[e.r32(G2 + 0xCC + 0x30)] = motion(car1);
            done = done || e.r32(P2 + 0xCC + 0x28) != 0;
            if (done) ++tail;
        }
        if (!ok) {
            lock.end();
            continue;
        }
        ++passes;
        // ---- the REPLAY view: + 0x74 cleared, jumps along the lap ----
        e.w32(race.info + 0x74, 0), lock.race()->set_logger_record(false);
        const std::map<s32, ex::Snapshot>& table = lock.race()->flow().logger[0];
        std::vector<int> ks;
        for (const auto& [k, sn] : table)
            if (k >= 0) ks.push_back(k);
        if (ks.empty()) {
            fail(scenario + ": no logger entries");
            lock.end();
            continue;
        }
        std::vector<int> picks{ks.front(), ks[ks.size() / 3], ks[ks.size() / 2], ks.back(), ks.front()};
        for (int k : picks) {
            e.call(pa(0x001873B8), {Arg::ptr(solo.obj), Arg::integer(k)});
            const bool ours_ok = lock.race()->logger_jump(k);
            ++jumps;
            std::string d = ours_ok ? std::string() : "ours has no such entry";
            for (int i = 0; i < 2 && d.empty(); ++i)
                if (std::string dc = ce::first_difference(lock.race()->cars()[size_t(i)], ce::read_car(e, race_setup::car_addr(race.cond, i)));
                    !dc.empty())
                    d = rh::fmt("car %d: ", i) + dc;
            if (d.empty()) {
                const auto cm = race.cond + race_setup::kCm;
                const auto walls = collide_exact::wall_table(e, e.r32(collide_exact::body_at(cm, 0) + 0x478));
                if (std::string db = collide_exact::first_difference(lock.race()->bodies(), collide_exact::read_bodies(e, cm, walls)); !db.empty())
                    d = "bodies: " + db;
            }
            if (d.empty()) d = rec_differ(e, P2 + 0xCC, solo.pools, lock.race()->recorders()->player.rec);
            if (d.empty()) d = rec_differ(e, G2 + 0xCC, solo.pools + ex::kRecordPool, lock.race()->recorders()->ghost.rec);
            if (!d.empty()) {
                fail(rh::fmt("%s: the jump to entry %d: %s", scenario.c_str(), k, d.c_str()));
                break;
            }
            lock.accept(e);
            // on from it, and the pass again?
            int drift0 = -1, drift1 = -1, seen0 = 0, seen1 = 0;
            double far0 = 0, far1 = 0;
            for (int j = 0; j < after_steps && ok; ++j) {
                if (!(ok = step(rh::fmt("after the jump to entry %d", k).c_str()))) break;
                ++jump_steps;
                const u32 f0 = e.r32(P2 + 0xCC + 0x30), f1 = e.r32(G2 + 0xCC + 0x30);
                if (auto it = pass0.find(f0); it != pass0.end() && e.r32(P2 + 0xCC + 0x28) == 0) {
                    const Motion m = motion(car0);
                    ++seen0;
                    if (drift0 < 0 && it->second != m) drift0 = j;
                    far0 = std::max(far0, apart(it->second, m));
                }
                if (auto it = pass1.find(f1); it != pass1.end() && e.r32(G2 + 0xCC + 0x28) == 0) {
                    const Motion m = motion(car1);
                    ++seen1;
                    if (drift1 < 0 && it->second != m) drift1 = j;
                    far1 = std::max(far1, apart(it->second, m));
                }
            }
            if (!ok) break;
            const std::string jn = rh::fmt("entry %d: car 0 %s over %d frames (%.3f m at most), car 1 %s over %d (%.3f m)", k,
                                           drift0 < 0 ? "as the pass" : rh::fmt("off the pass from step %d", drift0).c_str(), seen0,
                                           far0, drift1 < 0 ? "as the pass" : rh::fmt("off from step %d", drift1).c_str(), seen1, far1);
            drift_note += (drift_note.empty() ? "" : "; ") + (labelled != scenario ? rh::fmt("%s (%zu entries): ", scenario.c_str(), ks.size()) : std::string()) + jn;
            labelled = scenario;
            if (drift0 >= 0 || drift1 >= 0) ++drifted;
        }
        lock.end();
    }
    ctx.note(rh::fmt("%ld passes (%ld steps), %ld jumps (%ld steps after them), %ld of them off the pass afterwards (GT3's own); %ld differing",
                     passes, pass_steps, jumps, jump_steps, drifted, bad));
    if (!drift_note.empty()) ctx.note("after the jumps, the motion against the pass's at the same frame: " + drift_note);
    ctx.check(bad == 0, first);
    ctx.check(passes > 0, "no pass");
}

// ---- the disc's demo runs (race_exact_demos) --------------------------------------------------------------------------------
// The disc's demo runs (demo/<event>: GT3's own recordings of the licence tests' and the arcade time attacks' demonstrations,
// race/demo_replay.h) as whole races in lockstep, the original's against sim::exact::ExactRace: the original's race of the event
// under its conductor (licence or time trial) with the demo's car (its CarEquipments record written over the entry's), the
// player's entry with an input device and the record's gearbox (pedal style 0 AT / 1 MT), the event's start type and countdown
// run by the race information's frame before every step (ours, race_info_frame, written to the original's: check_countdown
// checks the two are the same); the pad read gives car 0 the demo's frame of each step (frame 0 the race's first step, the
// countdown and a flying start's run-up included; empty frames after the recording), as GT3's replay playback hands the
// recorded frames to the race. Ours from the same setup (lockstep_setup, car 0's equipment the record's) and frames. Every step
// compared (Lockstep); at the end the run's time (the licence's judged time, the time trial's lap) against the time the demo
// file records. Options: GT3R_DEMOS=<code,...> (default every demo on the disc), GT3R_DEMO_EXTRA=<steps after the recording>.
DIFF_TEST(race_exact_demos, "The disc's demo runs on sim::exact::ExactRace vs the original's race, from the same setup and recorded frames, every step") {
    rh::HitSetup hs;
    if (!rh::load_hit_setup(ctx, hs)) return;
    std::string perr;
    const ex::ProgramData* prog = start_setup::program_data(ctx, perr);
    if (!prog) return ctx.check(false, "program tables: " + perr);
    const Volume& vol = ctx.disc->volume();
    std::vector<std::string> codes;
    if (const char* e = std::getenv("GT3R_DEMOS")) {
        for (std::string s = e; !s.empty();) {
            const size_t c = s.find(',');
            codes.push_back(s.substr(0, c));
            s = c == std::string::npos ? std::string() : s.substr(c + 1);
        }
    } else {
        for (const std::string& n : vol.list_dir("demo"))
            if (!n.empty() && n.back() != '/' && hs.db.event(n)) codes.push_back(n);
    }
    const int extra = int(rh::env_num("GT3R_DEMO_EXTRA", 600));
    Lockstep lock;
    int runs = 0, timed = 0, time_match = 0, decode_bad = 0;
    std::string mismatches;
    for (const std::string& code : codes) {
        const Volume::Entry* de = vol.find("demo/" + code);
        const data::Event* ev = hs.db.event(code);
        if (!de || !ev || ev->entries.empty()) {
            ctx.check(false, code + ": no demo file or event");
            continue;
        }
        std::string derr;
        const Bytes demo_file = vol.read(*de);
        const std::optional<race::DemoReplay> demo = race::DemoReplay::parse(demo_file, &derr);
        if (!demo || demo->car_record.size() < ex::kEquipmentImageSize) {
            ctx.check(false, code + ": " + derr);
            continue;
        }
        if (std::getenv("GT3R_DEMO_DUMP")) { // (the replay header's car record past its equipment, and the header's words)
            const u32 hdr = rd32(demo_file, 0), len = rd32(demo_file, 4);
            Bytes body(len);
            for (u32 i = 0; i < len; ++i) body[i] = u8(~demo_file[hdr + i]);
            std::string line = code + " hdr:";
            for (u32 o = 0; o < 0x100; o += 4) line += rh::fmt(" %X:%08X", o, rd32(body, o));
            line += "\n car+1D0:";
            for (u32 o = 0x1D0; o < 0x280; o += 4) line += rh::fmt(" %X:%08X", o, rd32(body, 0x518 + o));
            line += "\n car2+0:";
            for (u32 o = 0; o < 0x20; o += 4) line += rh::fmt(" %X:%08X", o, rd32(body, 0x518 + 0x280 + o));
            std::printf("%s\n", line.c_str());
        }
        const Volume::Entry* cfe = vol.find("courses/US/" + ev->course);
        if (!cfe) {
            ctx.check(false, code + ": no course " + ev->course);
            continue;
        }
        const Bytes course = vol.read(*cfe);
        const ex::Conductor kind = code[0] == 'L' ? ex::Conductor::License : ex::Conductor::TimeTrial;
        const ex::RaceConfig cfg = ex::race_config(ByteSpan(ev->bytes.data(), ev->bytes.size()));
        // RaceBase::vf9: the event's start type (0x17A578), then the conductor's virtual 9 (the time trial's 2: the moving
        // start 3; the base conductor's: the event's when the configuration's +0 is 0, else none)
        s32 start_type = ev->bytes.size() > 0x1F3 ? s32(ev->bytes[0x1F3]) : 2;
        if (kind == ex::Conductor::TimeTrial) start_type = 3;
        else if (cfg[0] != 0) start_type = -1;
        const s32 seconds = ev->bytes.size() > 0x1F3 ? s32(ev->bytes[0x1F2]) : 0;
        const std::string scenario = rh::fmt("demo %s (%s, %zu frames, %s, start type %d)", code.c_str(), ex::conductor_name(kind),
                                             demo->size(), demo->automatic ? "AT" : "MT", start_type);
        // The demo's recorder block (after the replay header's mode word: u32 frames, u32 bytes, the bit stream) for the
        // original's AutomobileControlRecord::Recorder (the input device + 0xCC: +0 the bit stream, +0x38 the block).
        const u32 hdr = rd32(demo_file, 0), blen = rd32(demo_file, 4);
        Bytes body(blen);
        for (u32 i = 0; i < blen; ++i) body[i] = u8(~demo_file[hdr + i]);
        u32 human = 0, check = 0;
        auto recorder = [&](Ee& e, u32 r, u32 block) {
            e.w32(r, block + 8), e.w32(r + 0x38, block);
            e.call(0x001F44C8, {Arg::ptr(r)}); // reset: the stream from its start
        };
        auto before_init = [&](Ee& e, const race_setup::OrigRace& o) {
            const u32 entry = e.r32(e.r32(e.r32(o.info + 0x18) + 8));
            e.write_bytes(entry + race_setup::kEntryEq, ByteSpan(demo->car_record.data(), ex::kEquipmentImageSize));
            // The player's input device (RaceInput) playing the recording: +0x13C set (an input device: control source 0),
            // +0x130 1 (playback: the pad read 0x210740 decodes the next frame, 0x1F4660), +0x140 clear (a recording).
            human = e.alloc(0x200, 16);
            Bytes tail(body.begin() + 0x191C + 4, body.end());
            tail.resize(tail.size() + 0x1000, 0); // (zeros after the save: a stream read past its end)
            const u32 block = e.alloc_bytes(ByteSpan(tail), 16);
            e.w32(human + 0x13C, 1), e.w32(human + 0x130, 1), e.w32(human + 0x140, 0);
            recorder(e, human + 0xCC, block);
            check = e.alloc(0x60, 16); // a second decoder of the same stream (its frame at +0x44): against DemoReplay's
            recorder(e, check, block);
            e.w32(entry + entry_off(0x20DC), human);
            // the rest of the replay header's car record (RaceBase::vf8)
            e.w32(entry + race_setup::kEntryEq + 0x1E8, demo->eq_1e8);
            e.w32(entry + entry_off(0x20E0), u32(demo->grid_slot));
            e.w32(entry + entry_off(0x20E4), demo->pedal_style), e.w32(entry + entry_off(0x20E8), demo->spec_flag), e.w32(entry + entry_off(0x20EC), demo->wheel_type);
            e.w32(entry + entry_off(0x20F0), demo->asm_on ? 1 : 0), e.w32(entry + entry_off(0x20F4), demo->tcs_on ? 1 : 0);
            for (u32 k = 0; k < 8; ++k) e.w32(entry + entry_off(0x2104) + 4 * k, demo->condition[k]);
            e.w32(o.info + 0x50, 0), e.w32(o.info + 0x54, u32(start_type)), e.w32(o.info + 0x58, u32(seconds)), e.w32(o.info + 0x5C, 0);
        };
        race_setup::OrigRace race;
        std::string err;
        if (!race_setup::setup_original_race(ctx.ee, hs.db, hs.gtar, *ev, course, {ev->entries[0]}, cfg[1], 0, race, err, before_init,
                                             race_setup::kConductors[size_t(kind)])) {
            ctx.check(false, scenario + ": " + err);
            continue;
        }
        install_emitter(ctx.ee, race.cond);
        lock.install(ctx.ee, false);
        int step = 0, decode_diff = -1;
        // (experiments on the original alone: GT3R_DEMO_ORIG=1; GT3R_DEMO_R=<hex> the random register at the start,
        // GT3R_DEMO_SHIFT=<n> the recording n frames later (> 0: n frames skipped first), GT3R_DEMO_REPLAY=1 race info +0x6C)
        const bool orig_only = rh::env_num("GT3R_DEMO_ORIG", 0) != 0;
        if (const char* rv = std::getenv("GT3R_DEMO_R")) ctx.ee.vr = u32(std::strtoul(rv, nullptr, 16));
        const int shift = int(rh::env_num("GT3R_DEMO_SHIFT", 0));
        for (int k = 0; k < shift; ++k) ctx.ee.call(0x001F4660, {Arg::ptr(human + 0xCC), Arg::ptr(check + 0x44)});
        if (rh::env_num("GT3R_DEMO_REPLAY", 0) != 0) ctx.ee.w32(race.info + 0x6C, 1);
        ex::RaceSetup s;
        try {
            s = lockstep_setup(ctx.ee, race.cond, hs, *ev, ByteSpan(course.data(), course.size()), {ev->entries[0]}, kind, *prog, nullptr);
            s.entries[0].eq = ex::equipment_image(ByteSpan(demo->car_record.data(), demo->car_record.size()));
        } catch (const std::exception& e) {
            ctx.check(false, scenario + ": our setup: " + e.what());
            continue;
        }
        if (!orig_only && !lock.begin(ctx.ee, race.cond, s, scenario)) continue;
        ++runs;
        ex::RaceInfoStart info;
        info.start = 1, info.start_type = start_type, info.seconds = seconds;
        const int len = int(demo->size()) + extra;
        bool ok = true, done = false;
        int first_hit = -1;
        long hit_count = 0;
        u32 result_time = 0, lap = 0;
        s32 result = 0;
        for (step = 0; step < len && ok && !done; ++step) {
            std::vector<ex::RaceEvent> posts;
            ex::race_info_frame(info, posts);
            const s32 w[6] = {info.start, info.start_type, info.seconds, info.start_count, info.timer_on, info.timer};
            const u32 offs[6] = {0x50, 0x54, 0x58, 0x5C, 0x60, 0x68};
            for (int k = 0; k < 6; ++k) ctx.ee.w32(race.info + offs[k], u32(w[k]));
            ctx.ee.w32(human + 0x140, step < -shift ? 1 : 0); // (a later recording: the frames held back)
            if (decode_diff < 0 && shift == 0) { // the original's decoder against ours
                const u32 f = check + 0x44;
                ctx.ee.call(0x001F4660, {Arg::ptr(check), Arg::ptr(f)});
                u8 theirs[16], ours[16];
                ctx.ee.read_bytes(f, theirs, 16);
                demo->pad_bytes(size_t(step), ours);
                if (std::memcmp(theirs, ours, 16) != 0) {
                    decode_diff = step, ++decode_bad;
                    std::string a, b;
                    for (int k = 0; k < 16; ++k) a += rh::fmt("%02X", theirs[k]), b += rh::fmt("%02X", ours[k]);
                    ctx.note(rh::fmt("%s frame %d: the original's %s, ours %s", code.c_str(), step, a.c_str(), b.c_str()));
                }
            }
            lock.entry(ctx.ee);
            try {
                ctx.ee.call(race_setup::kCondStep, {Arg::ptr(race.cond), Arg::integer(0)}, 500'000'000);
            } catch (const std::exception& x) {
                ctx.check(false, rh::fmt("%s: step %d: the original: %s", scenario.c_str(), step, x.what()));
                break;
            }
            if (orig_only) {
                const s32 res = s32(ctx.ee.r32(race.info + 0x40));
                if (kind == ex::Conductor::License && !result && res != 0 && res != -2) result = res, result_time = ctx.ee.r32(race.info + 0x44);
                const u32 bl = ctx.ee.r32(race_setup::car_addr(race.cond, 0) + car_off(0x16C4));
                if (kind == ex::Conductor::TimeTrial && !lap && bl != ex::kNoTime) lap = bl;
                done = (result || lap) && step > int(demo->size());
                continue;
            }
            ok = lock.exit(ctx);
            const ex::ExactRace* r = lock.race();
            if (!r) break;
            if (std::getenv("GT3R_DEMO_TRACE") && (step % 30 == 0 || !r->events().empty())) {
                const ex::Car& c = r->cars()[0];
                const ex::PadFrame f = demo->pad_frame(size_t(step));
                std::string evs;
                for (const ex::RaceEvent& q : r->events()) evs += rh::fmt(" [%d %d %d]", q.type, q.car, q.arg);
                std::printf("%s %5d: start %d pos (%.1f %.1f %.1f) %.1f km/h gear %d control %d kind %d laps %d progress %.1f "
                            "x530 %u fin %d cout %d x535 %02X | pad k%d s%d t%d b%d%s\n",
                            code.c_str(), step, info.start_count, c.pos.x.host(), c.pos.y.host(), c.pos.z.host(),
                            c.speed.host() * 3.6f, int(c.gear), int(c.control), int(c.driver_kind), int(c.laps), c.f3e4.host(),
                            c.x530, int(c.finished), int(c.course_out) | int(c.off_count) << 8, int(c.x535), f.kind, f.steer, f.throttle, f.brake, evs.c_str());
            }
            for (const ex::CollisionHit& h : r->hits())
                if (h.car == 0 && first_hit < 0) first_hit = step;
            hit_count += long(r->hits().size());
            // The run's time: the licence's judged result, the time trial's first lap.
            if (kind == ex::Conductor::License && !result && r->flow().result != 0 && r->flow().result != -2)
                result = r->flow().result, result_time = r->flow().result_time;
            if (kind == ex::Conductor::TimeTrial && !lap && r->cars()[0].best_lap != ex::kNoTime) lap = r->cars()[0].best_lap;
            done = (result || lap) && step > int(demo->size());
        }
        if (const ex::ExactRace* r = lock.race(); r && std::getenv("GT3R_DEMO_VERBOSE")) {
            const ex::Car& c = r->cars()[0];
            ctx.note(rh::fmt("%s: %d steps, laps %d base %d, best lap %u, +0x530 %u, finished %d, result %d %u, clock %u, control %d "
                             "kind %d, pos (%.1f %.1f %.1f) %.1f km/h",
                             scenario.c_str(), step, int(c.laps), int(c.lap_base), c.best_lap, c.x530, int(c.finished),
                             r->flow().result, r->flow().result_time, r->flow().clock, int(c.control), int(c.driver_kind),
                             c.pos.x.host(), c.pos.y.host(), c.pos.z.host(), c.speed.host() * 3.6f));
        }
        if (ok && (lock.race() || orig_only)) {
            const u32 t = kind == ex::Conductor::License ? result_time : lap;
            if (t) ++timed;
            if (t && t == demo->time_ms) ++time_match;
            else
                mismatches += rh::fmt("%s%s %u ms (result %d) vs recorded %u", mismatches.empty() ? "" : "; ", code.c_str(), t, result,
                                      demo->time_ms);
            if (std::getenv("GT3R_DEMO_VERBOSE"))
                ctx.note(rh::fmt("%s: %s, %ld collision reports, car 0's first at step %d", code.c_str(),
                                 t == demo->time_ms ? "the recorded time" : "another time", hit_count, first_hit));
            if (decode_diff >= 0) mismatches += rh::fmt("%s%s decodes differently from frame %d", mismatches.empty() ? "" : "; ",
                                                        code.c_str(), decode_diff);
        }
        lock.end();
    }
    ctx.check(decode_bad == 0, rh::fmt("%d demo streams decode other frames than the original's 0x1F4660", decode_bad));
    ctx.note(rh::fmt("%d demo runs, %d timed, %d with the recorded time, %d decoded differently", runs, timed, time_match, decode_bad) +
             (mismatches.empty() ? std::string() : "; the others: " + mismatches));
    lock.report(ctx);
    ctx.check(runs == int(codes.size()), "every demo ran");
}
