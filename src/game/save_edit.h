#pragma once

// The save editor's changes to a GT Mode game (Home -> Save editor, app/screen_save_editor.cpp):
// what a player can set directly, kept consistent the way the game keeps it. Nothing here plays
// a race or pays a prize: results, medals and cars are set as chosen, and the prize cars they
// would have won are not given (a prize set not yet awarded is still given when the game next
// checks it, e.g. a series won again).

#include "game/gt_mode.h"

#include <string>

namespace gt3r::game {

// The editor's ranges. The game keeps credits and prize money in 64 bits and the counters in 32
// with no limit of its own; the editor stops at nine digits (what the menus' fields show) and
// days at five.
constexpr s64 kEditMaxMoney = 999'999'999;
constexpr u32 kEditMaxDays = 99'999;
constexpr u32 kEditMaxCount = 99'999; // races, wins

// Races and wins: wins never exceed races (the status page's win ratio), so lowering one
// lowers the other and raising wins raises races.
void set_races(GameState& s, u32 races);
void set_wins(GameState& s, u32 wins);

// --- Licences ------------------------------------------------------------------------------

// The event code of a test (license 0..5, test 0..7): the catalog's (gt_mode.mbl), else the
// usual form ("L0B0001").
std::string license_test_code(const GtMode& gt, int license, int test);
// Sets a test's medal. Its best time stays when the test's own limits still give that medal;
// otherwise it becomes the medal's limit (a time that earns exactly it), and no medal clears it.
// The licence card's flag follows whether the licence is held, and a medal's award bit is set
// as the medal jingle sets it (record_license_medal).
void set_license_medal(GtMode& gt, int license, int test, Medal m);
void set_every_license_medal(GtMode& gt, Medal m);

// --- Race results --------------------------------------------------------------------------

// A championship's own result slot (its title): the first round's code with round number 00
// ("Galtezza_n_0501" -> "Galtezza_n_0500"), as GtMode::record_round records it.
std::string championship_code(const RaceSeries& s);
// Best place of a race code, 0-based (0 = won); kNoPosition clears it ("never raced").
void set_race_best(GameState& s, const std::string& code, int place);
// Every GT Mode result slot (GtData::race_codes: races, championship titles) won.
void win_every_race(GtMode& gt);
void clear_race_results(GameState& s);

// --- Arcade --------------------------------------------------------------------------------

// The highest course level (every course group of the SINGLE_RACE list open).
int arcade_course_level_max(const GtData& data);
// Every course group and car level open.
void open_arcade(GtMode& gt);

// --- A garage car's condition ----------------------------------------------------------------

// The engine's states (physics §10.3): new (97 % power, E = -300 km), run in (100 %, E = 0)
// and worn (98 %, E = 600 km, where wear stops).
enum class EngineState { New, RunIn, Worn };
constexpr int kEngineStates = 3;
EngineState engine_state(float wear_m); // the nearest state
float engine_wear(EngineState e);
// The oil: fresh (just changed, O = -300 km) or used (O = 300 km, the race HUD's oil lamp).
enum class OilState { Fresh, Used };
OilState oil_state(float oil_m); // fresh while less than half used (GtMode::oil_age)
float oil_value(OilState o);
// The odometer in the game's distance unit (kilometres, or miles when imperial), whole units.
u32 odometer_units(const GtData& data, double odometer_m);
double odometer_metres(const GtData& data, u32 units);
u32 odometer_units_max(const GtData& data); // the odometer's own limit (sim::kOdometerMax)

} // namespace gt3r::game
