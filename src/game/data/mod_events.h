#pragma once

// Player-made GT Mode events: mods/<id>/events/<name>.ini (format: docs/MODDING.md "Adding
// events"), written by hand or by the Event creator.
//
// An event is one or more races, each on a course of the disc with its laps and opponents, run as
// single races or as a championship, with entry rules, prize money and an optional prize car. Each
// race becomes a record of the ordinary event table, cloned from a disc event (the file's `base`,
// else a GT Mode race of the disc) with the file's values written over it, so GT Mode's entry
// checks, race information, prize pages and results work on it as on the disc's own. The race codes
// start with data::kModEventPrefix. The menus list the event on a league page of the disc
// (game/gt_mode_mods.cpp).

#include "base/config.h"
#include "game/data/car_database.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r::data {

// Parses a definition (`id`: the file's name without .ini). nullopt with *error on a hard error;
// unknown keys and dropped values go to *warnings.
std::optional<ModEventDef> parse_mod_event(std::string_view text, const std::string& id, std::string* error,
                                           std::vector<std::string>* warnings = nullptr);
// The definition as a file parse_mod_event reads back. `picture` is written as given (the
// creator passes it relative to the mod folder).
std::string format_mod_event(const ModEventDef& def);

// The race codes' stem for an event of a mod ("X" + the mod and file names).
std::string mod_event_series(const std::string& mod, const std::string& id);

// Adds the event's race records (and its regulation, when it names its cars) to the database.
// Call db.rebuild() afterwards; load_mod_event_files does. False with *error when the event cannot
// run on this disc (a course or every opponent missing).
bool add_mod_event(CarDatabase& db, const ModEventDef& def, std::string* error);

// Loads definition files, in order (a later file with the same codes replaces an earlier one).
// CarDatabase::load does this for every enabled mod after the mod cars, from the VFS's mod list
// (Vfs::kModCarsList). Errors are logged and kept in db.mod_car_errors().
void load_mod_event_files(CarDatabase& db, const std::vector<std::string>& files, const std::string& mod = {});

// The arcade race whose driving a race's skill (0 Easy .. 3 Professional) takes, as Quick race's
// skills do: A{E,N,H,P}ST0001, on dirt A?AD0001; "" when the disc lacks it.
std::string mod_event_skill_event(const CarDatabase& db, int skill, bool dirt);

// A tune's parts: its car's own with each tune-shop item it names fitted (tyres as a pair); items the car has no
// part for go to *left_out. nullopt without the car.
std::optional<std::array<u64, 27>> mod_event_tune_parts(const CarDatabase& db, const ModEventTune& t,
                                                        std::vector<std::string>* left_out = nullptr);

// Names in the files and in the creator.
const char* license_key(int license);       // "none", "b", "a", "ib", "ia", "s", "r"
const char* drivetrain_key(int drivetrain); // "any", "ff", "fr", "4wd", "mr", "rr"
const char* aspiration_key(int aspiration); // "any", "na", "turbo"
const char* car_class_key(int car_class);   // "any", "race", "tuned", "stock"

} // namespace gt3r::data
