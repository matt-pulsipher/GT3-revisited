#pragma once

// The exact core's race setup from the game's: the program's tables from the disc's CORE.GT3 (loaded once), the course
// (loaded once per course file), and each car's equipment (the database car, a variant or arcade car, a GT-mode garage
// car with its parts and settings) as GT3's race entries hold them.

#include "race/field.h"
#include "sim/car_spec.h"
#include "sim/exact/params_build.h"
#include "sim/exact/program_data.h"
#include "sim/exact/race.h"

#include <memory>
#include <optional>
#include <span>
#include <string>

namespace gt3r {
class Vfs;
namespace data {
class CarDatabase;
}
namespace game {
struct OnlineCar;
}
} // namespace gt3r

namespace gt3r::race {

namespace ex = sim::exact;

// The program data of the disc in `vfs` (its CORE.GT3), checked once per program; null with *error set only when the disc
// has no program that decodes.
std::shared_ptr<const ex::ProgramData> exact_program_data(const Vfs& vfs, std::string* error = nullptr);
// Which physics a disc's races run on: the one place a game's core is chosen from its program. GT3's for GT3's releases
// (own); any other program races on GT3's core too (own false: GT3's tables, NTSC-U's rules for the routines not
// recognised) until a core of its own exists.
enum class RaceCore : u8 { Gt3 };
struct RacePhysics {
    RaceCore core = RaceCore::Gt3;
    bool own = true; // the disc's own game's physics
};
RacePhysics race_physics(const ex::ProgramData& d);
// "Gran Turismo 3".
const char* race_core_name(RaceCore c);
// The course of a course file (the last one loaded is kept: a restart and a replay of the race reuse it).
std::shared_ptr<const ex::RaceCourse> exact_course(ByteSpan course_file, const std::string& label, const ex::ProgramData& d);

// The equipment of a car source as GT3's race entry holds it: a garage car's parts and settings (make_equipment), a
// variant's base car with the variant's parts (stock_equipment), an arcade car's record (arcade_equipment, its tyres
// switched to the stage's records), a database car with its own parts. nullopt with *error set for an unknown label.
// An electric mod car races on its donor car's engine (its stock engine, engine parts, drivetrain and gearbox): the exact
// core has no EV drive yet.
std::optional<ex::Equipment> exact_equipment(const data::CarDatabase& db, const ExactCarSource& src, std::string* error = nullptr);
// The same without the electric mod cars' donor engine (the equipment as the database builds it).
std::optional<ex::Equipment> exact_equipment_as_built(const data::CarDatabase& db, const ExactCarSource& src,
                                                      std::string* error = nullptr);
// An online player's car (game::OnlineCar, docs/dev/ONLINE.md "Cars") as a car source: a garage car with its parts and
// settings, an arcade car with its arcade tyres for the course (race::arcade_car's stage), a disc or mod car by its label;
// its engine condition (a garage car's) in *condition.
ExactCarSource exact_online_source(const data::CarDatabase& db, const game::OnlineCar& c, const std::string& course,
                                   sim::EngineCondition* condition = nullptr);
// A car's GT Mode grid figure (gt_grid_figure, race/field.h) from its race equipment, as GT3's grids build every entry for it:
// the showroom build with a fresh engine (sim::exact::showroom_build), the spec's mass over the power the menus show for its
// peak (sim::exact::shown_power; `hp`: in HP, BHP or CV).
int exact_grid_figure(const data::CarDatabase& db, const ex::ProgramData& d, const ex::Equipment& eq, bool hp);
// The base car (table 30 label) of a car source label.
std::string exact_base_car(const data::CarDatabase& db, const std::string& label);
// The course category byte of a course (the course record's +8; 0 when the database has no record).
u8 exact_course_category(const data::CarDatabase& db, const std::string& course);
// A hash of the game data a race on the exact core is built from besides the program's tables: the course file and, for
// every entry, the database records its equipment names (the car, each part, the tyre parts' compound, force and size
// records). A replay keeps it and is refused on data that differ (another disc's cars or courses, a changed mod car).
u64 exact_data_fingerprint(const data::CarDatabase& db, std::span<const ex::StartEntry> entries, ByteSpan course_file);
// Whether car 0 of a free practice on this course has a pit box (the stop line, AI line 4, has at least 4 points; with
// fewer the program reads outside the line, and the race start refuses the race). GT3 offers no free practice on such a
// course (every one on the discs is a dirt course, where its pre-race menu turns FREE RUN off).
bool exact_has_pit_box(const ex::RaceCourse& c);

} // namespace gt3r::race
