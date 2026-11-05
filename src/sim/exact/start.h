#pragma once

// The race start [physics §9, ai §9.1-§9.2]: every byte of a car's record (0x16DC bytes) as the race's initialisation, the
// conductor's race start, leaves it, from our database, course and event data and the race's setup (the entry's
// grid slot, control, aids; the start type), in GT3's own arithmetic: the record cleared and linked,
// the spec and the parameter block (params_build.h), the car's set-up (the driver parameter set, the event's
// overrides, the placement on the grid, the pose on the road, the standing start, the tyres' starting wear, the laps and
// sector, the AI driver's line and plan, a player's run-up by the AI, the moving start) and the per-car bookkeeping after it
// (phase 1's previous-tick slots, the wheel spin and drawn points). The image loads into the tick's mirror with load_image
// (car_fields.h).
//
// The race mode's conductor gives each car's control source, spec flag, pedal style and ghost flag (the entry set-up: for
// races, battles and free practice, for time trials, free runs) and its start mode, distance and speed (the start position):
// the grid (mode 0; races at the event's start speed), the rolling start (start type 3: mode 8 for car 0, every car at
// 80 km/h), the time trials' and free runs' flying starts on the main line (the flying start: mode 2 from the event, 3 from the
// course's driver set, 4 before the last tight arc), the 2-player battle's handicap (mode 1), free practice's pit box (mode
// 6) and cars on the main line behind it (mode 5), the licence tests (the event's flying start, or mode 1 at its
// distance) and the machine test (its race progress at the nose, and sectors by its type).

#include "formats/course/driver_sets.h"
#include "sim/exact/ai_course.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/course_query.h"
#include "sim/exact/params_build.h"
#include "sim/exact/program_data.h"
#include "sim/exact/race_inputs.h"
#include "sim/exact/tick.h"

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace gt3r::sim::exact {

using Record = std::array<u8, kCarSize>;

// What every car's start reads of the race.
struct StartRace {
    const data::CarDatabase* db = nullptr;
    const CourseQuery* course = nullptr;
    const AiCourse* ai = nullptr;             // the AI course object (conductor + 0xBBA8) as ai_course_setup leaves it
    const course::DriverSets* sets = nullptr; // the course file's driver parameter sets (slots 37-40)
    const ProgramData* data = nullptr;
    RaceConfig rules{};    // conductor + 0xE340 (race_config, with the race's lap count at +1)
    WearParams wear;       // conductor + 0x84 (wear_params)
    u8 category = 0;       // conductor + 0x8000 + 0x37AD: the course category (course record +8; 0xFF reads as 0)
    u8 config = 0;         // conductor + 0x8000 + 0x37AC: the configuration byte the race start is given
    s32 start_type = 0;    // race info + 0x54: 3 a rolling start
    Conductor kind = Conductor::SinglePlayer;
    s32 handicap = 0;      // the 2-player battle's handicap (race info + 0x10)
    bool mark_pit = true;  // the conductor's pit test (has_pits): wheels on the pit-lane surface set F +0x535 bit 4
    s32 cars = 0;          // the race's cars (conductor + 0x8000 + 0x6384; start_cars sets it from its entries)
    // The addresses in the record's pointer words: the race info (car + 0), the conductor (car + 4, the driver object's + 0;
    // its + 0xBBA8, the AI course object, at the driver object's + 8) and the course (car + 8).
    u32 info = 0, conductor = 0, course_addr = 0;
    // The course's road block in memory: a grid slot past the course's table reads the block's words that follow the table,
    // some of them the loader's pointers into the block (CourseQuery::slot).
    u32 road = 0;
};

// A race entry (the race info's entry list, i).
struct StartEntry {
    Equipment eq;                      // entry + 0x20: the car, its parts and settings
    u32 human = 0;                     // entry + 0x20DC (car + 0xC)
    bool player = false;               // [entry + 0x20DC] + 0x13C != 0: the entry has an input device
    s32 grid_slot = 0;                 // entry + 0x20E0: the start grid slot
    s32 pedal_style = 0;               // entry + 0x20E4
    s32 spec_flag = 0;                 // entry + 0x20E8: the spec builder's flag (a player's car)
    s32 wheel_type = 0;                // entry + 0x20EC: the steering-wheel type (5 and above read as 0)
    bool asm_on = true, tcs_on = true; // entry + 0x20F0 / + 0x20F4
    std::array<u32, 8> condition{};    // entry + 0x2104: the engine condition words
    u32 best_lap = 0;                  // entry + 0x11D8: free practice keeps the lower of it and the car's best lap
    u32 car = 0;                       // the record's address (the parameter block's tables point into it)
    // A CPU car's own overrides in place of the race's (Quick race's custom race, not in GT3). Unset: the race's.
    std::optional<DriverOverrides> driver;
};

// The per-car part for entry i of the race: the whole record.
Record start_car(const StartRace& race, const StartEntry& e, int i);
// The conductor's ranking (the race order conductor + 0xE388, one car index per position): the rank reset puts the
// cars in index order (rank = index + 1); the ranking sorts the order (an insertion sort: more laps
// ahead, then a car not finished ahead of a finished one, then the greater race progress, or for two finished cars the
// smaller F +0x530) and sets each car's rank to its position + 1.
using RaceOrder = std::array<s32, 8>;
void rank_reset(std::span<Car> cars, RaceOrder& order);
void rank(std::span<Car> cars, RaceOrder& order);
// The race start for all of the race's entries: each car's record (start_car), then the ranking the initialisation ends
// with (the ranking set-up: the rank reset and the ranking; nothing else it does changes a car's record).
struct StartResult {
    std::vector<Record> records;
    RaceOrder order{};
};
StartResult start_cars(const StartRace& race, std::span<const StartEntry> entries);

// The machine test's race progress: the race progress of the car's nose (the CoG + the forward axis x the body's
// front extent P +0x00); the other conductors' is car_progress (race_flow.h).
F nose_progress(const Car& c, const CourseQuery& course);
// The event's per-class overrides of a CPU car's driver (class `cls`'s bytes of the race configuration `rules`:
// throttle +0xC, brake +0x10, corner speed +0x14, corner offset +0x18) [ai §9.2]; in configuration 2 a CPU car's (control
// source 2) varied by its index.
void event_overrides(Car& c, const RaceConfig& rules, u8 config, int cls);
// The tyres' wear and grip as at the start (and at a pit stop's tyre change): fresh tyres (the race's scrub-in wear
// when wear is on) and cool brakes.
void tyres_start(Car& c, const WearParams& cp);
// The standing start: the dynamics zeroed, gear 1, the clutch open, engine 0, shift lock-out 12 (an AI or parked
// car, a pad car with pedal style 0 or 2), time scale 1, the steering bounds +-full lock [physics §9.3].
void standing_start(Car& c);
// In the standing start, and at a lap's end in the time trial, free practice and the licence tests: the course-out
// count F +0x4DF and flags F +0x51E cleared.
void clear_course_out(Car& c);
// The moving start at speed v (m/s): the velocity along the heading, every wheel and axle at road speed, the gear
// for the speed, the engine at v times the gear's ratio (idle to the rev limit), the clutch locked, the turbo spooled
// [physics §9.3].
void moving_start(Car& c, F v);

} // namespace gt3r::sim::exact
