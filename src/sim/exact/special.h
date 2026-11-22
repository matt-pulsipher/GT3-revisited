#pragma once

// The later builds' special cars and their own words of the car record (Gran Turismo Concept [physics §14.5]): four
// routines of the race step GT3 does not have, the state they keep (Car::later) and the tables they read. GT3's releases
// have none of it: every function here leaves a GT3 car as it is (its special kind is 0, its layout is never 7).
//
// The spec builder gives some cars a special kind (spec +0x11 -> P +0x32) by their labels (ProgramData's SpecialCars
// table): 1, a car with moods (lamps, a lean and a nod, and a count of the steps it stands on its brake, which lowers its
// suspension), and 2, a car with a wing that moves out above one speed and in below another; the 2002 builds from
// Tokyo-Seoul v2.00 on have more (two cars of kind 7, and kinds 8 .. 10 with wings of their own speeds). The eight bytes at the later builds'
// record +0x149C (Later::special) hold a special car's state (a layout 7 car's store and flow display: hybrid.h).

#include "sim/exact/car_record.h"
#include "sim/exact/program_data.h"
#include "sim/sim_types.h"

#include <span>

namespace gt3r::sim::exact {

struct GapMatrix; // race_flow.h

// Special car 1's mood tables (ProgramData::Table::PodMoods): per mood 0..15.
struct MoodTables {
    std::array<u8, 16> time{}; // +0x00 the steps a mood lasts when the caller gives none
    std::array<s8, 16> lean{}; // +0x10 1 / -1: the lean grows that way; 2: the nod runs on
    std::array<u8, 16> lamp{}; // +0x20 the lamps' class: a change of class fades through 24 steps (36 from a class not 0)
};
// The tables from the disc's program; all zero where it has none (GT3's releases).
MoodTables mood_tables(const ProgramData& d);
// The special kind (0 none) the spec builder (Rule::SpecBuild's variant) gives the car whose label's id is `label_id`, from
// the program's SpecialCars table.
u8 special_kind(const ProgramData& d, u64 label_id);

// The moods (what sets each).
enum Mood : int {
    kMoodRest = 0,
    kMoodPassed = 1,   // a car alongside on the same lap
    kMoodHappy = 2,    // a finish in first place; the slip filter at 1
    kMoodPit = 3,      // on the pit lane
    kMoodHurt = 4,     // (the collision reports')
    kMoodAngry = 5,
    kMoodWrongWay = 6, // the HUD message 1
    kMoodSad = 7,      // a finish behind the first
    kMoodScared = 8,
    kMoodHit = 9,
    kMoodDirt = 10, // all four wheels on a soft surface
};

// Later: asks for a mood for `time` steps (-1: the mood's own time; 0xFF: for good). Nothing while the lamps
// fade (unless for good), after a mood set for good, or with time 0 while another mood's time runs.
void set_mood(Car& c, const MoodTables& k, int mood, int time);

// The pass-by state bytes the race's sounds keep per pair of cars (conductor + 0xBA34 + 0x84 + 8 i + j; the
// later builds' conductor + 0xBB18): 2 and 3 are a car passing.
using PassBy = std::array<std::array<u8, 8>, 8>;
// The race's pass-by states (at the end of every race step, both entry modes: conductor + 0xBA34, the later builds'
// + 0xBA94), kept for the race's sounds and read by the later builds' special car 1: per ordered pair of cars (i, j) a timer
// (+4 + 16 i + 2 j) of the steps car i has had car j's slipstream (the proximity's slip factor above 0: up by one to 300,
// else down to 0) and a state (+0x84 + 8 i + j, PassBy) from the pair's relative-position code (the proximity's): 0 none,
// 1 the slipstream held a second (61 steps), 2 and 3 a car passing (codes 5 / 6 and 3 / 4: alongside, just ahead), the pair's
// roles swapped when it is behind again (codes 1, 2). At 50 steps a second (PAL's and 2002 Tokyo-Geneva Europe's own
// routines, Rule::PassBy) 250 and 51.
struct PassByState {
    std::array<std::array<s16, 8>, 8> timer{};
    PassBy state{};
};
// For the first `n` cars (the conductor's car count) on the step's proximity matrices; `pal_rate`: 50 steps a second.
void pass_by_step(PassByState& p, const Proximity& k, int n, bool pal_rate = false);

// Later: how close car i's nearest rival is, 0..5 (5: one passing it, or it passing one, on the same lap), from
// the pass-by states and the gap matrices (the laps between two cars, the gap folded into half a lap).
int rival_level(int i, int count, const PassBy& m, const GapMatrix& gaps, F lap_length);

// What the special cars' routines read outside the car record (RaceInputs::later).
struct LaterInputs {
    MoodTables moods;
    const PassBy* pass_by = nullptr; // the pass-by states (null: none, no rival)
    const GapMatrix* gaps = nullptr; // the conductor's gap matrices
    F lap_length;
};
// rival_level() of car i among `count` cars from the inputs (0 without the pass-by states).
int rival_level(int i, int count, const LaterInputs& in);

// Rule::HybridStep (later), after phase 1: a layout 7 car's byte special[5] is cleared.
void hybrid_step(Car& c);
// Rule::PodMood (later), before phase 2: special car 1's mood from the race (finished, the wrong way, the pit
// lane, a rival alongside (`rival`: rival_level), the slip filter, soft ground under every wheel (`all_soft`)), its time,
// the lamps' fade, the lean and the nod.
void mood_step(Car& c, const MoodTables& k, int rival, bool all_soft);
// Rule::PodStop (later), after phase 3: special car 1's stop count special[0], up to 34 (29 at 50 steps a
// second) while the car stands (under 1 km/h) with the handbrake control at 1, down otherwise.
void stop_count_step(Car& c);
// Later (the pod race's rule, Rule::PodRule): whether a car sits, its stop count special[0] at its cap (34; 29 at 50
// steps a second). Any car's byte (the rule reads it without the special kind).
bool sitting(const Car& c);
// Rule::PodSpring (later, the suspension's spring rate of wheel i, given P's rate k): special car 1's wheels lose
// their spring one after another while its stop count n is not 0, wheel i once n is past 7 r (r = (i + its index) mod 4,
// mirrored for an index of 4 and up; 6 r at 50 steps a second): from then on k x max(0, 1 - (n - 7 r)).
F pod_spring(const Car& c, int i, F k);
// Rule::HitReport (later: the collision report's moods): special car 1 hit, by the report's kind (0 a wall:
// mood 9; 2: 5) and, for a car (1), by the part hit: 1, 2 mood 5; 5, 6 mood 8; 0, 3, 4, 7 mood 4, each for its own time; 8 to
// 10 mood 5, 11 and 15 mood 4, the others 8, each for 210 steps (175 at 50 steps a second).
void hit_mood(Car& c, const MoodTables& k, s32 kind, s32 part);
// What the car model shows of a special car (race's frame: the getters,
// read every frame into the car model's callback; the sound's): kinds 2 and 8 .. 10 their wing
// (the float special[0..3]); kind 1 its tail, lean 0.5 - special[5] / (2 x the lean's limit) and nod 0.5 + its position
// / (half the nod's period), its lamps' class and fade (the fade's last steps: the class of the mood, weight (fade steps -
// special[3]) / fade steps; before them the class of the mood before, weight (special[3] - fade steps) / the rest), its
// mood (special[1] & 15) and the step's new mood (special[6]). Other cars: VehicleState::Special's defaults.
sim::VehicleState::Special special_look(const Car& c, const MoodTables& k);

// Rule::ActiveWing (later), before phase 12: special car 2's wing, the float special[0..3] towards 1 at half a
// unit a second once the speed reaches 80 km/h and towards 0 once it falls below 60 km/h (special[4]: out). From
// Tokyo-Seoul v2.00 on also kinds 8 and 10 (their own two speeds) and kind 9, whose wing follows the speed between two
// speeds at two units a second.
void wing_step(Car& c);

} // namespace gt3r::sim::exact
