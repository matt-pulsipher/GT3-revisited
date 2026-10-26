#pragma once

// GT Mode rules: the career logic of GT3 (NTSC-U) reimplemented from
// its documented behaviour. Randomness follows the original: trophy and
// prize-car rolls use the GT-mode state GameState::rng, opponent draws the race
// module's state (race_info_random()).
//
//   GtData   - the disc data GT mode needs (paramdb, product.gcf, carcolor,
//              gt_mode.mbl catalog), loaded once.
//   GtMode   - a GameState plus the operations the menus perform (buy / sell
//              cars, tune shop, wheels, wash, oil, licenses, races,
//              championships, prizes) and the bridge to sim::CarSpec.
//
// Every operation that can fail returns a Check naming the original's
// refusal popup (Check::page()).

#include "base/game_random.h"
#include "game/data/car_database.h"
#include "game/gcf.h"
#include "game/gt_catalog.h"
#include "game/gt_parts.h"
#include "game/gt_state.h"
#include "game/net_car.h"
#include "race/paint.h"
#include "sim/car_spec.h"
#include "sim/sim_types.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gt3r::game {

// A prize set of product.gcf section 15: one or more candidate cars.
struct PrizeCar {
    int id = 0;          // prize id (unique per entry)
    std::string car;     // car label (table 30)
    int color_id = -1;   // carcolor id, -1 (or an id not in the model's list) = a random paint
    int weight = 1;
};
struct PrizeSet {
    std::string name;    // "Gsunday_e_0000", "Galtezza_n_0500", "Erome2h_n_0001", "L0B0000", ...
    std::vector<PrizeCar> cars;
};

class GtData {
public:
    using Reader = std::function<std::optional<Bytes>(const std::string&)>;
    // Loads database/paramdb_us.db (+ strings), gameconf/product.gcf,
    // database/carcolor.db and menu/gt_mode.mbl. Returns false with error().
    bool load(const Reader& read);
    const std::string& error() const { return error_; }

    data::CarDatabase db;
    Gcf gcf;
    Catalog catalog;
    std::vector<PrizeSet> prizes;     // gcf section 15
    std::vector<std::string> race_codes; // gcf section 4: every GT-mode result slot (364), sorted
    std::map<std::string, int> unlock_level; // gcf section 4 value: 1 = needs the S license (F1)
    struct Demo {
        std::string event, course; // demo race (table 32 "demo_*") and its course
        std::string car;           // word +8: the demo's own car for slot 0 ("" on the disc: all slots drawn)
        int level = 0;             // 1 = shown only with the S license (the F1 demos)
    };
    std::vector<Demo> demos;          // gcf section 12, attract mode
    std::vector<std::string> race_songs; // bgm/ads.inf race_* files ("race_a01.ads"), index order
    int title_idle_seconds = 20;      // gcf section 0, first word: title idle time before attract mode
    std::map<std::string, std::string> name_chars; // name entry page: item -> character (menu/US/name_entry1.pmb)
    // Units of the menus' figures: the NTSC-U release shows miles, HP and ft-lb, the
    // others metric (picked by the loaded database's region at load).
    bool imperial = true;
    int region = 1;       // the game's region index (0 JP, 1 US, 2 EU, other): the loaded database's
    // The program's built-in record names (time-trial seeds, three per challenge: 30), read
    // from the disc's CORE.GT3 at load (empty when the program or its table is not found).
    std::vector<std::string> builtin_names;
    // Arcade mode (gt_mode §16). gcf tag 8: course lists per game ("SINGLE_RACE",
    // "TIME_ATTACK" = free run, "TWO_PLAYER", "I_LINK"), each course with its
    // unlock level 0..5.
    struct ArcadeCourse {
        std::string course; // "super", "r_midfield", ...
        int level = 0;
        u32 length_m = 0;   // total length (metres)
    };
    std::map<std::string, std::vector<ArcadeCourse>> arcade_courses;
    // gcf tag 14: car lists per class ("CLASS_C", "CLASS_B", "CLASS_A", "CLASS_S",
    // "CLASS_DIRT", "CLASS_COMPULSORY", "CLASS_PREVIEW").
    struct ArcadeCar {
        std::string label;     // arcade car label (table 35), e.g. "nsx_s_zero_97_290", "arm_tt_00"
        std::string race_code; // CLASS_COMPULSORY: the time trial ("AT010001"); else ""
        int level = 0;         // unlock level 0..12 (0 = always)
        int variant = 0;       // added to the last character of the class race code
        bool enabled = true;
        // The car's picture on the arcade car page ("nsx_s_zero_97", "tt_00").
        std::string image() const;
    };
    std::map<std::string, std::vector<ArcadeCar>> arcade_classes;

    u32 starting_credits() const;     // gcf "initial" (18,000 on NTSC-U)
    u32 car_wash_price() const;       // 50
    u32 oil_change_price() const;     // 250
    u32 wheel_price() const;          // 500
    const PrizeSet* prize_set(const std::string& name) const;
    // carcolor.db (via race::CarColors): the paint list of a car's model.
    int color_count(const std::string& car) const;
    int color_index(const std::string& car, int color_id) const; // 0 when unknown
    u32 color_id(const std::string& car, int index) const;       // 0xFFFFFFFF when out of range
    u32 color_rgb(u32 id) const;                                // bytes R, G, B
    std::string color_name(u32 id) const;                       // carcolor.sdb, "" when unknown
    std::string model(const std::string& car) const;            // body model id ("ni0137")

private:
    race::CarColors colors_;
    std::map<u32, std::string> color_names_;
    std::string error_;
};

// Refusals (the page the original shows instead of continuing).
enum class Check {
    Ok,
    NotForBuy,         // 00cmn/not_for_buy
    NoMoney,           // 00cmn/no_money
    MaxCar,            // 00cmn/max_car
    NotForSell,        // 00cmn/not_for_sell
    NoRideCar,         // 00cmn/ride_car
    NotForChangeWheel, // 00cmn/not_for_change_wheel
    AlreadyHave,       // ts/already_you_have (part) or 00cmn/already_you_have (wheel)
    AlreadyHaveTires,  // ts/already_you_have_all_tires
    NotExist,          // ts/not_exist
    NeedDirtTire,      // hm/need_dirt_tire
    NoLicense,         // lc/no_license_<x>
    NoLastExam,        // lc/no_last_exam
    LicenseViolation,  // 00cmn/license_violation
    RegulationViolation, // 00cmn/regulation_violation
    Unknown,           // bad label / no data
};
const char* check_name(Check c);

// Entry rules of one event for one car (gt_mode §8).
struct EntryResult {
    bool dirt_tires = false;   // dirt tyres required and not owned
    bool license = false;      // required license not held
    bool drivetrain = false;
    bool regulation = false;   // car not in the regulation list
    bool aspiration = false;   // NA only / turbo only
    bool car_class = false;    // race car only / tuned only / stock production only
    bool ok() const { return !(dirt_tires || license || drivetrain || regulation || aspiration || car_class); }
};

// Event rule fields (table 32) used by GT mode.
struct EventRules {
    std::string code;
    std::string course;
    int required_license = 0;  // +0x19B: 0 none, 1 B .. 6 R
    int cars = 6;              // +0x1B5
    int laps = 0;              // +0x199
    std::array<u32, 6> prize{}; // +0x1D4: Cr per place 1..6
    u32 champ_bonus = 0;       // +0x1E2 x 100 Cr
    int league = 0;            // +0x1E5: 1 beginner, 2 amateur, 3 professional
    int car_class = 0;         // +0x1F4: 1 race/rally car, 2 tuned, 3 stock production
    int aspiration = 0;        // +0x1F5: 1 NA, 2 turbo
    int drivetrain = 0;        // +0x1F6: 1 FF, 2 FR, 3 4WD, 4 MR, 5 RR
    bool dirt_tires = false;   // +0x1D2 == 9
    int trophy_class = 0;      // +0x1D3: arcade 1..4, GT 5 beginner .. 8 (race/{a,g}_<easy|normal|hard|special>_*.prm)
    std::string regulation;    // table 33 label, "" = none
    std::array<u32, 3> license_ms{}; // gold / silver / bronze, ms (+0x1C4/+0x1C8/+0x1CC)
    int license_kind = 0;      // +0x19A: 1 stop, 2 section, 3 lap(s)
    std::vector<std::string> entries; // opponent pool (car variants)
};

// What a finished race / test produced.
struct Award {
    s64 money = 0;            // prize money (+ championship bonus)
    s64 bonus = 0;            // championship bonus part of `money`
    int place = -1;           // 0-based, -1 = not classified
    Medal medal = Medal::None; // license tests
    bool license_obtained = false;
    bool all_gold = false;
    bool champion = false;
    bool championship_over = false;
    int trophy = -1;           // championship won: the trophy picked (0..9, menu/champ_NN.prm)
    std::vector<int> new_cars; // garage indices of prize cars received
    std::vector<std::string> messages;
};

class GtMode {
public:
    explicit GtMode(std::shared_ptr<const GtData> data);

    const GtData& data() const { return *data_; }
    const data::CarDatabase& db() const { return data_->db; }
    const std::shared_ptr<const GtData>& data_ptr() const { return data_; }
    GameState& state() { return state_; }
    const GameState& state() const { return state_; }

    // New game: 18,000 Cr (gcf "initial"), day 1, empty garage.
    void new_game();

    // --- Cars ----------------------------------------------------------
    s64 car_price(const std::string& car) const; // catalogue price (car +0xEC)
    u8 car_flags(const std::string& car) const;  // car +0xF0: 1 not for buy, 2 not for sell, 4 no wheel change
    u8 car_category(const std::string& car) const; // car +0xF1: 0 production, 1 race, 2 rally
    s64 sell_price(const GarageCar& c) const;    // price / 4
    Check can_buy_car(const std::string& car) const;
    // Adds the car (newest first) and pays; returns its garage index or -1.
    // The first car becomes the ride car.
    int buy_car(const std::string& car, int color = 0);
    // Trade (gt_mode §19): buying a friend's car from a guest garage. The model's flag bit 1
    // refuses it (NotForBuy, e.g. the S2000 variants, Stratos, 996 GT3), then money
    // (catalogue price), then a full garage.
    Check can_buy_guest_car(const GarageCar& c) const;
    // Copies the car (parts, settings, owned parts, condition) with a stock wheel and a new
    // serial; pays the model's catalogue price. Returns the garage index or -1.
    int buy_guest_car(const GarageCar& c);
    // Free car (prize); colour id from the prize table (-1 = first colour). -1 if the garage is full.
    int add_prize_car(const std::string& car, int color_id, const std::string& origin);
    Check can_sell(int index) const;
    s64 sell_car(int index);
    void ride(int index);
    // A fresh garage car with stock parts and settings (not added).
    std::optional<GarageCar> make_car(const std::string& car) const;

    // --- Tune shop (always the ride car) ---------------------------------
    std::optional<PartOffer> part_offer(int goods) const;
    bool owns(int goods) const;
    Check can_buy_part(int goods) const;
    bool buy_part(int goods);
    bool fit_part(int goods);         // must be owned (stage-0 goods = back to stock)
    // Fits a goods on any car (the shop's fitting rules; ownership not checked).
    bool fit_goods(GarageCar& c, int goods) const { return fit(c, goods); }
    Check can_buy_all_tires() const;
    s64 all_tires_price() const;
    bool buy_all_tires();
    // Power (PS) of the ride car now and with a goods fitted (the shop's before/after).
    float ride_power_ps() const;
    float power_ps(const GarageCar& c) const; // display power (PS) with parts and condition
    float power_with(int goods) const;
    // The original's computed max power of a garage car (parts and engine condition) and its
    // rpm.
    PowerPeak power_peak(const GarageCar& c) const;
    // A power as the menus print it: the truncated figure in the data set's unit, then `sep`
    // and "HP" (NTSC-U) or "PS": "325HP", "325 HP".
    std::string power_text(float ps, const char* sep = "") const;
    // A catalogue power (car database, PS) in the data set's unit, truncated.
    int catalogue_power(float ps) const { return display_power(ps, data_->imperial); }

    // --- Wheels, wash, oil -----------------------------------------------
    Check can_buy_wheel(const std::string& wheel) const;
    bool buy_wheel(const std::string& wheel);
    bool fit_wheel(int car_index, const std::string& wheel); // "" = stock
    Check can_wash() const;
    bool wash();
    Check can_change_oil() const;
    bool change_oil();
    // Engine output factor from the car's condition (break-in, wear, oil; physics §10.3).
    static float engine_factor(const CarCondition& c);
    // How used the oil is, 0 (fresh) .. 1 (used), as the oil change page shows it: after an oil
    // change (O < 0) the share of its 300 km driven, (O + 300,000) / 300,000; from O = 0 (a new
    // car's oil, or 300 km after a change, when O jumps to 300,000) O / 300,000, at most 1
    // (gt_mode §13).
    static float oil_age(float oil_m);

    // --- Engine condition through a run (gt_mode §13.1, physics §10.3) ----------
    // The runs that carry the car's engine condition and hand it back: GT races and
    // championships, machine tests, free runs, arcade races. Licence tests, time trials and
    // 2 Player Battle run with a fresh engine and hand nothing back.
    static bool carries_engine_condition(const std::string& mode);
    // The condition words a run of a garage car starts with (odometer in 2 m units).
    static sim::EngineCondition run_condition(const CarCondition& c);
    // The same words after `metres` driven forward in one piece (the race's counters
    // advance this way tick by tick); for tests and the development fake race.
    static sim::EngineCondition driven(sim::EngineCondition c, float metres);
    // After such a run: the garage car (index into the garage; -1 = not a garage car)
    // takes the words back, and the total distance grows by the odometer units the run
    // added (2 m each).
    void end_run(int car_index, const sim::EngineCondition& end, u32 odometer_added);
    // Index of a car of this garage, -1 for any other car (a guest garage's, a copy).
    int garage_index(const GarageCar& c) const;

    // --- Licenses ---------------------------------------------------------
    static int license_index_of(const std::string& code); // 0..5 for L0B/L0A/LIB/LIA/L0S/L0R, else -1
    bool license_held(int lic) const;      // all 8 tests have a medal
    bool license_all_gold(int lic) const;
    int highest_license() const;           // -1 none, 0 B .. 4 S (rally not counted)
    Check can_open_license_page(int lic) const; // earlier licenses (B, A, IB, IA, S) needed
    Check can_take_test(const std::string& code) const; // 8th test needs tests 1-7
    Medal judge(const std::string& code, bool finished, double seconds) const;
    Award record_license(const std::string& code, bool finished, double seconds);
    // Same with a medal judged elsewhere (the race's license result screen).
    Award record_license_medal(const std::string& code, Medal medal, double seconds);

    // --- Races ------------------------------------------------------------
    std::optional<EventRules> rules(const std::string& code) const;
    EntryResult check_entry(const std::string& code, const GarageCar& car) const;
    Check can_enter(const std::string& code) const; // ride car + entry rules
    bool series_unlocked(const std::string& code) const; // F1 needs the S license
    bool tuned(const GarageCar& c) const;
    // Entering a race / test / machine test / free run costs one day.
    void enter() { ++state_.day; }
    // Single race result: place 0-based (-1 = retired). Pays the place prize,
    // records the best place, counts races / wins, awards prize cars.
    Award record_race(const std::string& code, int place);
    // A GT-mode race prize page's trophy count and award bit (cup or consolation prize).
    void note_race_prize(const EventRules& r, int place, s64 money);
    // Championship: start with the round-1 code (the menu's champ item + 1).
    bool start_championship(const std::string& first_round);
    // Records a round: `order` = finishing order as entrant indices (0 =
    // the player); empty = only the player's place is known (others keep
    // their grid order). Returns the award; championship_over when done.
    Award record_round(int place, const std::vector<int>& order = {});
    // Opponents of the current championship (car variant labels).
    const std::vector<std::string>& championship_field() const { return state_.championship.entrants; }
    // Random opponents for a race (the original's pool draw, gt_mode §20.4), cars - 1 of them.
    std::vector<std::string> pick_opponents(const std::string& code, GameRandom& rng = race_info_random());

    // Records (the records menu): best results per key, best first (gt_mode §17). The new
    // entry goes to the first place that is empty or whose value is not better, so it ranks
    // ahead of an equal one; it gets the last name entered, the day and the clock's stamp.
    // Returns the rank the new entry got (0 = best), -1 when it did not make the list.
    int add_record(const std::string& key, double value, const std::string& car, bool ascending, size_t max);
    // A finished machine-test run (`code` M400 / M1000: its time in seconds; MMAX: its top
    // speed in km/h) with the ride car `car`: its ranking ("mt:<code>", 12 places) and the
    // best of the test (GameState::machine). Returns the rank (-1 = not ranked).
    int record_machine_test(const std::string& code, double value, const std::string& car);
    // Course time trials (gt_mode §17): the free runs' rankings, one per course of the arcade
    // TIME_ATTACK course list (its index = the save's slot, -1 = no ranking for the course).
    int course_time_trial_index(const std::string& course) const;
    // A free run (the arcade time attack or GT Mode's free run) that ended with a best lap
    // (seconds) driven in `car`: its course's ranking ("ct:<course>", 12 places). Returns the
    // rank (-1 = not ranked, no such course, or no lap).
    int record_course_time_trial(const std::string& course, double best_lap, const std::string& car);

    // Championship trophy for the trophy_get presentation (menu/champ_NN.prm):
    // weighted random over the ten trophies, each weight + 165.333 x completion^3;
    // sets the trophy's flag. Returns 0..9. (record_round calls it for a title.)
    int pick_trophy();
    // The GT-mode generator (gt_mode §20.2): the menus step it once per frame; the
    // trophy and prize-car presentations open with a stir of 1..12 steps.
    void tick_random() { GameRandom::next(state_.rng); }
    void stir_random();

    // Prize cars a result unlocks (scans every prize set).
    std::vector<int> award_prizes(const std::string& code);
    // A GT-mode code (gt_mode §23): GT races and championships (G...), endurance (E...),
    // rally (D...), licences (L.B, L.A, L.S, L.R: tests and licence sets), machine tests
    // (M4.., M1.., MM..); not arcade (A...) or anything else. Only a check with such a
    // code can award the whole-game prize set (gtmode_complete).
    static bool gt_mode_code(const std::string& code);
    // Every GT-mode result slot (gcf section 4: 364 on this disc) won.
    bool all_races_won() const;
    // Prize cars refused since the last call because the garage was full (the
    // original's "Garage is full" page, gt_mode §18b).
    int take_prizes_refused() { return std::exchange(prizes_refused_, 0); }
    bool league_complete(const std::string& tier) const;
    float completion() const; // GT races + arcade clears + time trials + license tests, over their totals
    // The original's completion texts: the status page truncates
    // the ratio to per mille, "2.0 %" ("100.0 %" only when complete); the save thumbnail holds
    // the per cent (ratio x 100, PS2 float multiply) and the file lists truncate it to one
    // decimal, "2.0%". Nothing is rounded.
    std::string completion_text() const;
    float completion_percent() const;
    static std::string completion_file_text(float percent);

    // --- Arcade progression (gt_mode §16) ---------------------------------------
    // Difficulty of an arcade race code (2nd character E/N/H/P -> 0..3), -1 otherwise.
    static int arcade_difficulty(const std::string& race_code);
    // Index of a course in the SINGLE_RACE list (= result slot), -1 if absent.
    int arcade_course_index(const std::string& course) const;
    // gcf unlock level of a course (-1 = not an arcade course).
    int arcade_course_level(const std::string& course) const;
    bool arcade_course_open(const std::string& course) const;
    bool arcade_course_dirt(const std::string& course) const; // database course surface 2
    bool arcade_car_level_open(int level) const;
    // Cars of an arcade class the player can pick ("class_c"... or "CLASS_C"...;
    // locked and region-hidden cars removed), in list order. Empty for the garage classes.
    std::vector<const GtData::ArcadeCar*> arcade_cars(const std::string& car_class) const;
    // Courses of an arcade game list ("SINGLE_RACE", ...) that are open, in list order.
    std::vector<std::string> arcade_courses(const std::string& list) const;
    // Single Race result (place 0-based, the only arcade mode that records one).
    // Keeps the best place per (difficulty, course); a win adds a trophy and the
    // difficulty's award bit; an improved record reruns the unlock pass.
    struct ArcadeOutcome {
        bool recorded = false, improved = false;
        int courses_opened = 0;     // course groups opened by this result
        std::vector<int> car_levels; // car unlock levels newly opened
    };
    ArcadeOutcome arcade_single_race(const std::string& race_code, const std::string& course, int place);
    // Has course `i` (SINGLE_RACE index) been won at difficulty `d` or a harder
    // one (Easy..Hard only, as the Clear Status page counts)?
    bool arcade_won_at_least(int course, int d) const;
    // Every course of group `g` (the courses of level g) won at `d` or harder.
    bool arcade_group_won(int g, int d) const;
    // Every course won at exactly difficulty `d`.
    bool arcade_all_won_at(int d) const;
    int arcade_completion() const; // Clear Status count, 0..102
    // Endings (ar/goodies "Goal"): any ending / the long one available.
    bool arcade_ending_open() const;
    bool arcade_long_ending_open() const;
    // Time Trial: index 0..9 of an "AT010001"-style code (or a "tt01" course id), -1 otherwise.
    static int time_trial_index(const std::string& code_or_course);
    // The challenge's three default records (seconds, best first) from its race
    // record (table 32 +0x1C4/+0x1C8/+0x1CC).
    std::vector<double> time_trial_defaults(const std::string& code) const;
    // A time trial time: enters it in the "at:<code>" ranking (seeded with the
    // defaults) and keeps the best rank; all ten at rank 0 award the
    // arcade_compulsory_complete prize car. Returns the rank (-1 = not ranked).
    int arcade_time_trial(const std::string& code, double seconds, const std::string& car);
    // The arcade car filter's region test (gt_mode §16): true = not offered in this region.
    bool arcade_car_region_hidden(const std::string& label) const;
    // A record name as shown: a name stored as one byte >= 0x80 (a seeded record's mark)
    // is entry ~byte of the program's built-in name table (gt_mode §16), "-" when the table
    // has no such entry; any other name as it is.
    std::string record_name(const std::string& stored) const;
    // The mark the original stores as the name of the k-th (0..2) seeded record of time
    // trial `challenge` (0..9): the byte 0xFF - 3 challenge - k, i.e. built-in name
    // 3 challenge + k.
    static std::string time_trial_seed_name(int challenge, int k) {
        return std::string(1, char(0xFF - 3 * challenge - k));
    }
    // True for a stored name that is such a mark (one byte >= 0x80).
    static bool is_record_mark(const std::string& name) { return name.size() == 1 && u8(name[0]) >= 0x80; }
    // A new game's ranking of a time trial (gt_mode §16): the race record's three default
    // times (+0x1C4/+0x1C8/+0x1CC), each inserted before the first equal or slower time,
    // named with its mark (time_trial_seed_name(challenge, k)), no car, day 0. Empty for
    // an unknown code.
    static std::vector<RankEntry> time_trial_seed_ranking(const data::CarDatabase& db, const std::string& code);
    // The first n entries of a ranking ("at:", "mt:", "lic:" keys), as the race quick menus'
    // "Top 3" show them before and after each run (the original's time-trial and machine-test
    // drivers read entries 0..2 of the record: time and name): an arcade time trial with no
    // ranking yet shows its seeded entries. ranking_name() is an entry's name as shown (an
    // entry stored without a name shows none).
    std::vector<RankEntry> ranking_top(const std::string& key, size_t n) const;
    std::string ranking_name(const RankEntry& e) const { return record_name(e.name); }

    // --- Car spec bridge ------------------------------------------------------
    // The physics spec of a garage car with its fitted parts and settings. The engine
    // condition is not in it: a run carries the car's condition words (run_condition) and
    // the menus' power figures apply the factor (power_peak).
    std::optional<sim::CarSpec> car_spec(const GarageCar& c, std::string* error = nullptr) const;

    // Garage display order (indices into state().garage) for a sort key:
    // 0 date (newest first), 1 name, 2 maker, 3 colour, 4 price, 5 distance,
    // 6 power, 7 drive, 8 aspiration (the garage panel's keys, gt_mode §5).
    std::vector<int> garage_order(int key) const;
    // The sort column text of a car for a key ("" for date / name).
    std::string sort_value(const GarageCar& c, int key) const;

    // Model id of a garage car (cars/<time>/<model>).
    std::string model_of(const GarageCar& c) const;

private:
    float rand01() { return GameRandom::rand01(state_.rng); }
    int prize_paint(const PrizeCar& pc);   // paint index of a prize candidate (random when unset)
    int pick_prize(const PrizeSet& set);   // weighted candidate pick
    int add_prize_car_paint(const std::string& car, int paint, const std::string& origin);
    void set_stock_ownership(GarageCar& c) const;
    void arcade_unlock_pass(ArcadeOutcome& out);
    bool fit(GarageCar& c, int goods) const;
    int place_in_garage(GarageCar c);

    std::shared_ptr<const GtData> data_;
    GameState state_;
    int prizes_refused_ = 0;
};

// The original's draws on the GT-mode generator state (gt_mode §20.3), one step each.
// Paint of a prize candidate without a fixed colour: int(count x unit + 0.5), count -> 0.
int prize_paint_index(int count, u32& rng);
// Prize-car pick: weights (at least 1) summed as PS2 floats; the first whose running
// sum reaches unit x total (none: 0).
int pick_weighted(const std::vector<int>& weights, u32& rng);
// Championship trophy 0..9 for a completion ratio (no stir).
int pick_trophy_index(float completion, u32& rng);

// Formatting helpers for the menus.
std::string format_credits(s64 v);      // "18,000"
std::string format_time_ms(u32 ms);     // "1'02.400" (as the original's menus)

// Record stamps (RankEntry::stamp): the clock now, in seconds since 2000-01-01 00:00 local
// time (at least 1, so it never reads as "none").
u32 record_stamp_now();
// A record's date on the records page (gt_mode §17.2): the stamp's calendar date as
// "DD/MM/YY" (the NTSC-U order), "--/--/--" for no stamp. As the original converts it,
// only the year 2000 gets February 29 (later leap years run on from February 28).
std::string record_date_text(u32 stamp);

} // namespace gt3r::game
