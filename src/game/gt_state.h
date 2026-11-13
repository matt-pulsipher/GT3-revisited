#pragma once

// GT Mode game state: everything the original keeps in its game state
// and writes to the memory card.
// Plain data; the rules that change it live in gt_mode.h, the native save
// format in gt_save.h.

#include "base/bytes.h"
#include "sim/car_spec.h"

#include <array>
#include <bitset>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace gt3r::game {

// Program constants (the PAL and NTSC-J programs hold the same values):
// - the garage's 200 cars: the dealer and trade checks (slti 0xC8), the
//   prize-car page and the garage's slot search;
// - the wheel inventory's 200 slots, the stock-wheel placeholder of a new game included (the
//   slot search; GameState::wheels holds the bought ones);
// - 6 licences (B, A, IB, IA, S, R = rally) of 8 tests: the completion count's loops
//   and its total 48.
constexpr int kMaxGarageCars = 200;
constexpr int kMaxWheels = 200;
constexpr int kLicenseCount = 6;
constexpr int kTestsPerLicense = 8;

// License ids in the original's order (records = license * 8 + test).
enum class License : int { None = -1, B = 0, A = 1, IB = 2, IA = 3, S = 4, R = 5 };
const char* license_name(License l);        // "B", "A", "IB", "IA", "S", "R"
const char* license_menu_id(License l);     // "b", "a", "ib", "ia", "s", "d" (lc/lc_<id>)
License license_from_menu_id(const std::string& id);

// License test result: the original stores -1 none, 0 gold, 1 silver, 2 bronze.
enum class Medal : int { None = -1, Gold = 0, Silver = 1, Bronze = 2 };
const char* medal_name(Medal m);

// Car condition (garage car save record +0x0C..+0x1B): distances in metres.
struct CarCondition {
    double odometer_m = 0;
    float engine_wear_m = -300000; // E: break-in starts at -300 km (97 % power), see engine_factor()
    float oil_m = 0;               // O: 0 = no bonus, -300,000 right after an oil change
    float dirt = 0;                // 0..0.8 (visual)
};

struct GarageCar {
    u32 serial = 0;          // acquisition order (sort by date)
    std::string car;         // database car label (table 30)
    std::string origin;      // prize car: the variant / prize key it came from ("" = bought)
    int color = 0;           // paint index (carcolor.db order for the car's model)
    std::array<u64, 27> parts{}; // fitted part ids, data::Car::parts order (kCarPartTables)
    std::bitset<88> owned;   // owned goods (gt_parts.h)
    sim::CarSettings settings; // user settings (physics §6.6), kept in step with `parts`
    int ref_final = 0;         // car setup record +0x1AC: final drive the gear-auto-level ranges are built from (gt_mode §14)
    // The settings menu's working copy only (not saved): the reference final its gear sliders' ranges stand on now. GT3
    // builds them when a slider page opens and again at each auto-level change, from +0x1AC as it then was (gt_mode §14);
    // 0 = ref_final.
    int range_ref_final = 0;
    std::string wheel;      // fitted wheel label (table 24), "" = stock
    CarCondition cond;
};

// A saved car setup (the original's 'S' setting file on the memory card): the
// car, its fitted parts when saved, and the settings. Loading copies each group
// of settings only when the part it belongs to is still the same.
struct SettingFile {
    std::string car;
    std::array<u64, 27> parts{};
    sim::CarSettings settings;
    int ref_final = 0;
    u32 day = 0; // game day it was saved
    // Memory-card file info (the file list's info panel): host time it was written
    // (seconds since 1970), the car's power then (PS), the file name (comment).
    s64 time = 0;
    int power_ps = 0;
    std::string comment;
};
constexpr int kSettingFilesPerCar = 10;

// Machine test records (best per test).
struct MachineRecords {
    double t400 = -1, t1000 = -1;   // seconds, < 0 = none
    double top_speed = -1;          // km/h
    std::string car400, car1000, car_top; // car labels of the records
};

// One line of a records ranking (time in seconds or speed in km/h).
struct RankEntry {
    double value = 0;
    std::string car;
    u32 day = 0;      // the GT-mode day it was set on
    std::string name; // entered on the name entry page (shown as stored: "" shows none)
    // When it was set, by the clock: seconds since 2000-01-01 00:00 local time, as the
    // original stamps a record (0 = none, e.g. from a save older than the stamps).
    u32 stamp = 0;
    // The 30-character block the original encodes from the run with each record (I: the
    // records page's entry code); kept from an imported PS2 save, "" for our own records.
    std::string code;
};

// Best finishing position per GT-mode race code; 0 = 1st ... (8 = never raced).
constexpr int kNoPosition = 8;

// A championship in progress (the original saves one suspended championship).
struct Championship {
    std::string series;           // series id (RaceSeries::id)
    std::vector<std::string> races; // race codes in order
    int next = 0;                 // index of the next race
    std::vector<int> points;      // player first, then the opponents (by entry)
    std::vector<std::string> entrants;
    // The entrants' paints (index in the model's colour list, -1 none), drawn in the first round and kept with the
    // field (GT3 keeps each entrant's car as built, paint included); empty until that round has run.
    std::vector<int> paints;
    bool active() const { return !series.empty() && next < int(races.size()); }
};

struct LicenseTestRecord {
    Medal medal = Medal::None;
    double best_time = -1; // seconds, < 0 = none
};

// Arcade mode progression. The original keeps it
// in the same game state as GT mode (and saves it with it).
// The Single Race results: one per difficulty and course of the gcf SINGLE_RACE list (34
// courses on every disc), record difficulty x <list size> + course; the save's
// block has room for 144 (0xB40 bytes of 0x14-byte records). GtMode::arcade_course_count().
constexpr int kArcadeResultSlots = 144;
constexpr int kArcadeDifficulties = 4;  // easy, normal, hard, professional (< 4)
constexpr int kArcadeCarLevels = 12;    // car unlock flags 1..12
// The time trials (AT010001..AT100001, the gcf CLASS_COMPULSORY list): program constant, the
// game state's 10 records (0x2A4 bytes each, the completion count's loop) and the
// completion total 10.
constexpr int kTimeTrials = 10;
constexpr int kCourseTimeTrials = 44;   // course time-trial rankings in the PS2 save (TIME_ATTACK list order)
constexpr int kArcadeNoPosition = 8;    // "no result" in the arcade blocks

struct ArcadeProgress {
    int course_level = 0;                         // 0..6: course groups open (level <= this)
    std::array<bool, kArcadeCarLevels> car_flags{}; // flag k (index k-1) opens cars of level k
    // Award bits (scalar +0xC4; gt_mode §3.2): 1 << ((cup class - 5) x 3 + place) a GT cup,
    // 0x1000 << difficulty an arcade race won, 0x10000 a consolation prize, 0x20000 << medal
    // a license medal.
    u32 awards = 0;
    // Best Single Race position per (difficulty, course): index difficulty x the SINGLE_RACE
    // list's size + course (list order), 0 = won, kArcadeNoPosition = none.
    std::array<int, kArcadeResultSlots> best;
    std::array<int, kTimeTrials> at_best_rank;    // best time-trial rank, 0 = beat the record
    ArcadeProgress() {
        best.fill(kArcadeNoPosition);
        at_best_rank.fill(kArcadeNoPosition);
    }
};

struct GameState {
    s64 credits = 0;
    u32 day = 1;              // advances by 1 for every race / test entered
    u32 races = 0, wins = 0;
    s64 prize_money = 0;      // total prize money won
    double distance_m = 0;    // total distance driven
    u32 next_serial = 0;
    u32 trophies = 0;
    u32 prize_cars = 0;       // prize cars received
    u32 trophy_flags = 0;     // prize models received (scalar +0xC0): 1 << license card (B, A, IB, IA,
                              // S, R), 0x40 << i championship trophy champ_0i
    u32 rng = 0x87654321;     // GT-mode generator state (trophy / prize-car rolls, gt_mode §20), saved at +0xCC
    std::vector<GarageCar> garage; // list order: newest first (the original inserts at the head)
    int ride = -1;            // index of the current car in `garage`, -1 = none
    std::vector<std::string> wheels; // wheel inventory (labels)
    std::array<LicenseTestRecord, kLicenseCount * kTestsPerLicense> license_tests{};
    std::map<std::string, int> race_best;   // race code -> best position (0 = won)
    std::set<std::string> prizes_given;     // prize keys whose prize was awarded
    Championship championship;
    std::vector<SettingFile> setting_files; // oldest first
    MachineRecords machine;
    bool ending_short_seen = false, ending_long_seen = false;
    // Records rankings: "lic:<test>" (6, fastest first), "mt:M400" / "mt:M1000" (12,
    // fastest first), "mt:MMAX" (12, top speed first), "at:<time trial code>" (12).
    std::map<std::string, std::vector<RankEntry>> rankings;
    std::string player_name; // last name entered (the name entry's starting text; empty in a new game, as the original)
    std::string file_comment;           // the game file's name (memory-card File Operation, Edit file name)
    ArcadeProgress arcade;
    // The options block (0x888 bytes) of an imported PS2 save, kept as it is so that an
    // export back to the PS2 restores the player's options, music list and key layout
    // (empty: never imported).
    Bytes ps2_options;

    GarageCar* ride_car() { return ride >= 0 && ride < int(garage.size()) ? &garage[size_t(ride)] : nullptr; }
    const GarageCar* ride_car() const {
        return ride >= 0 && ride < int(garage.size()) ? &garage[size_t(ride)] : nullptr;
    }
    int best_position(const std::string& race) const {
        auto it = race_best.find(race);
        return it == race_best.end() ? kNoPosition : it->second;
    }
};

} // namespace gt3r::game
