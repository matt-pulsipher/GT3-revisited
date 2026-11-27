#pragma once

// Player-made GT Mode events (docs/MODDING.md "Adding events"): what a mods/<id>/events/<name>.ini
// holds, and what loading it made (game/data/mod_events.h). The race records themselves live in the
// ordinary event table (32), cloned from a disc event; this keeps what the disc format has no field
// for (the title, the pictures, the league page, the skills and grid place of the field, the points
// a championship scores past GT3's own table).

#include <array>
#include <string>
#include <vector>

#include "base/bytes.h"

namespace gt3r::data {

// One entry of a race's pool (event +0x10): a car (table 30) as the dealers sell it, a car variant of
// the disc (table 31: an opponent as the disc's races field it) or one of the event's tunes
// ("tune:<name>"), and its paint as the record keeps it (+0x110: a carcolor id; 0xFFFFFFFF = drawn at
// the race start).
struct ModEventCar {
    std::string label;
    u32 color = 0xFFFFFFFFu;
};

// A tuned opponent: a car with tune-shop parts (game/gt_parts.h shop items: "sus_semi_racing",
// "nat_2", "tir_soft"...), the rest its own. It becomes a car variant record (table 31) as the disc's
// opponents are, with the settings the car gets with those parts (sim::exact::variant_record).
struct ModEventTune {
    std::string name; // [tune:<name>]
    std::string car;
    std::vector<std::string> parts;
};

struct ModEventRace {
    std::string course; // course label (table 34)
    int laps = 3;
    // Its own lineup; empty = the event's field. (Championship rounds always race the field.) It is
    // the race's pool: the race draws its opponents from it as GT3's races do (every car, in the draw's
    // order), the player behind them.
    std::vector<ModEventCar> opponents;
    // How many opponents it draws from its pool (event +0x1B5 less the player's car), up to 31: more than the
    // pool has repeats its cars (GT3's draw); 0 = as many as the pool has, at most 5.
    int cars = 0;
    // How the opponents drive: the race record's own driver percentages (event +0x19C .. +0x1B3, every
    // CPU car of a driveline class alike, ai §9.2), -1 the base race's, 0 .. 3 the arcade race of that
    // difficulty's (Easy .. Professional: A{E,N,H,P}ST0001, on dirt A?AD0001).
    int skill = -1;
    // How it starts: the event record's start type (+0x1F3, with +0x198 and +0x1F2), copied from a disc race that
    // starts so: -1 the base race's, 0 standing (a GT Mode series race's), 1 rolling (an endurance race's).
    int start = -1;
    // Tire wear: the record's wear bytes (+0x1BD .. +0x1C3; GT3 has one wear curve, physics §8.1), copied from a disc
    // race: -1 the base race's, 0 off (a race without wear), 1 on (an endurance race's). Never on dirt courses (GT3).
    int wear = -1;
};

// A prize car: a car label and its paint (a carcolor id, -1 = a random paint).
struct ModEventPrize {
    std::string car;
    int paint = -1;
};

struct ModEventDef {
    std::string id;   // the file's name without .ini ("sunday_morning_cup")
    std::string mod;  // providing mod id (filled by the loader)
    std::string file; // host path (filled by the loader)

    // [event]
    std::string title, description;
    std::string picture; // host path of the event's picture (PNG, 640x448 shown), "" = none
    std::string icon;    // host path of its strip icon on the league page (PNG, 51x27 shown), "" = the picture
    // The league page it is listed on, as the disc's race map links it ("r/beginner/beginner");
    // "" or a page the open disc does not have: the Custom Events league.
    std::string league;
    // A disc event whose rules fill everything this file leaves out (start, the opponents'
    // driving, tyre wear, trophies); "" or not on this disc: a GT Mode race of the disc.
    std::string base;
    bool championship = false;
    // Credits per place as the US and PAL discs count them (stored in hundreds, like the disc's
    // events: a Japanese disc pays them x100, as it does its own prizes).
    std::array<u32, 6> prizes{};
    u32 bonus = 0; // championship bonus
    // The prize cars, a prize set as the disc's (gt_mode §10): one car is given once (for the championship, or for every
    // race won); two to four go to GT3's roulette, which picks one of them on every win of a championship or of a
    // one-race event (as the disc's championships and endurance races give theirs). A car listed twice comes up
    // twice as often. Single races of more than one race give one car only (the disc's series give one).
    std::vector<ModEventPrize> prize_cars;
    // Championship points by place (1st first), our own: GT3's table (10, 6, 4, 3, 2, 1, then none) is the
    // program's, not the event's; empty = GT3's. Fields of ours are up to 32 cars, so places past 6th may score.
    std::vector<int> points;

    // [entry] the event's rules (the disc's numbering, gt_mode EventRules)
    int license = 0;    // 0 none, 1 B, 2 A, 3 IB, 4 IA, 5 S, 6 R
    int drivetrain = 0; // 0 any, 1 FF, 2 FR, 3 4WD, 4 MR, 5 RR
    int aspiration = 0; // 0 any, 1 NA, 2 turbo
    int car_class = 0;  // 0 any, 1 race or rally cars, 2 tuned cars, 3 stock production cars
    std::vector<std::string> allowed; // only these cars (car labels); empty = any

    // [field] the opponents of every race without its own lineup, and of a championship
    std::vector<ModEventCar> field;
    // [tune:<name>] the tuned cars the lineups name
    std::vector<ModEventTune> tunes;

    // [race1] .. [race10]
    std::vector<ModEventRace> races;

    static constexpr int kMaxRaces = 10;
    static constexpr int kMaxOpponents = 31;    // in a race (a field of up to 32 cars, the player's included; GT3's races 5)
    static constexpr int kDefaultOpponents = 5; // a race without cars = (as GT3's races)
    static constexpr int kMaxPool = 32;     // in a pool (event +0x10)
    static constexpr int kMaxPrizeCars = 4; // the car_get page shows at most four (0x13F010, game::CarGetRoulette::kMaxCars)
    static constexpr int kMaxPoints = 32;   // a place each of a 32-car field
    static constexpr int kMaxPointsValue = 999;
    // The prize cars a set can give: four, or one for single races of more than one race.
    size_t prize_room() const { return !championship && races.size() > 1 ? 1 : size_t(kMaxPrizeCars); }
};

// A loaded event: its definition and the race codes (event labels) it got.
struct ModEvent {
    ModEventDef def;
    std::string series;             // the codes' common stem, "Xmy_events_sunday_morning_cup"
    std::vector<std::string> races; // race codes in order: "<series>_0001".. or, a championship, "<series>_0501"..
    std::string champ;              // a championship's own code ("<series>_0500"), else ""
    // The code a league page item is named by: the championship's, else the first race's.
    const std::string& item() const { return champ.empty() ? races.front() : champ; }
    // The lineup race `k` runs: its own, else the field.
    const std::vector<ModEventCar>& lineup(size_t k) const {
        return def.championship || def.races[k].opponents.empty() ? def.field : def.races[k].opponents;
    }
};

// Mod event race codes start with this letter: no disc code does, so they never count for a
// league, the whole game or the completion (GtMode::gt_mode_code).
constexpr char kModEventPrefix = 'X';

} // namespace gt3r::data
