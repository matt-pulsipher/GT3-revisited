#pragma once

// Our race start (src/sim/exact/start.h) for a race race_setup built from the user's disc: the course's road queries, AI course
// and driver parameter sets from the course file, the rule block and tyre wear from the event, each entry's equipment from
// the database, and the race's setup inputs (grid slot, player, pedal style, aids, condition words) as the race's entries hold
// them; the record's pointer words get the addresses of the original's objects. Shared by race_start_exact and race_tick_exact.

#include "harness.h"
#include "race_setup.h"

#include "formats/course/ai_lines.h"
#include "formats/course/driver_sets.h"
#include "formats/course/road.h"
#include "sim/exact/ai_setup.h"
#include "sim/exact/start.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gt3r::diff::start_setup {

namespace ex = sim::exact;

// A course file as the start reads it.
struct Course {
    course::Road road;
    course::AiLines lines;
    bool has_lines = false;
    course::DriverSets sets;
    std::unique_ptr<ex::CourseQuery> query;
    ex::AiCourse ai;
};
// no_segments: the AI line points' segment words cleared (the loader then traces them along the lines).
bool load_course(ByteSpan file, const ex::ProgramData& d, Course& out, std::string& err, bool no_segments = false);
// The same for the original's container of race o (before the initialisation loads the lines).
void clear_line_segments(Ee& ee, const race_setup::OrigRace& o);

// The program's tables from the disc's CORE.GT3 (read once); null with err set when they cannot be read.
const ex::ProgramData* program_data(DiffContext& ctx, std::string& err);

// The race-wide inputs of race o (under conductor `kind` with configuration byte `config`; the battles' handicap, tyre wear
// and catch-up words from the race info).
ex::StartRace start_race(Ee& ee, const race_setup::OrigRace& o, const data::CarDatabase& db, const data::Event& ev, const Course& c,
                         const ex::ProgramData& d, int config, ex::Conductor kind = ex::Conductor::SinglePlayer);
// Entry i of race o (label: the car or variant it was built from).
ex::StartEntry start_entry(Ee& ee, const race_setup::OrigRace& o, const data::CarDatabase& db, const std::string& label, int i);

// Our start records of every car of race o.
// `adjust` changes the race-wide inputs first (a scenario's change to the race, made to the original's too).
std::vector<ex::Record> start_records(Ee& ee, const race_setup::OrigRace& o, const data::CarDatabase& db, const data::Event& ev,
                                      const std::vector<std::string>& labels, const Course& c, const ex::ProgramData& d,
                                      int config, ex::Conductor kind = ex::Conductor::SinglePlayer,
                                      const std::function<void(ex::StartRace&)>& adjust = {},
                                      std::vector<ex::Car::Later>* later = nullptr); // (`later`: the cars' Car::later)

} // namespace gt3r::diff::start_setup
