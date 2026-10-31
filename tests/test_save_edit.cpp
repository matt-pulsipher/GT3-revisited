// The save editor's changes (game/save_edit.h, GtMode::add_car / remove_car) and its session
// hooks (game::editable_save / store_edited_save), disc-free on the synthetic world
// (gtmode_synth.h).

#include "test_framework.h"

#include "gtmode_synth.h"

#include "game/gt_mode_menu.h"
#include "game/gt_save.h"
#include "game/save_edit.h"
#include "sim/engine_condition.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

using namespace gt3r;
using namespace gt3r::game;
namespace synth = gt3r::test::gtsynth;
namespace w = gt3r::test::gtsynth::world;

namespace {

std::shared_ptr<GtData> world_data() {
    auto d = std::make_shared<GtData>();
    CHECK(d->load(synth::gtmode_reader()));
    return d;
}

void set_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unset_env(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// A GT-mode session on the synthetic world playing the save at `path` (GT3R_GTMODE_SAVE, saving
// on); the module and the variables are put back afterwards.
class Session {
public:
    explicit Session(const std::string& path) {
        for (size_t i = 0; i < 2; ++i)
            if (const char* old = std::getenv(kVars[i])) saved_[i] = old;
        set_env("GT3R_GTMODE_SAVE", path.c_str());
        unset_env("GT3R_GTMODE_NOSAVE");
        reset_gt_mode_menu_for_tests();
        CHECK(ensure_gt_mode_session(synth::gtmode_reader()) && gt_mode_session());
    }
    ~Session() {
        reset_gt_mode_menu_for_tests();
        for (size_t i = 0; i < 2; ++i) {
            if (saved_[i]) set_env(kVars[i], saved_[i]->c_str());
            else unset_env(kVars[i]);
        }
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

private:
    static constexpr const char* kVars[2] = {"GT3R_GTMODE_SAVE", "GT3R_GTMODE_NOSAVE"};
    std::optional<std::string> saved_[2];
};

} // namespace

// add_car: stock, unpaid, not a prize, newest first; the first car rides; the paint is clamped
// to the model's list; a full garage takes none. remove_car keeps the same ride car.
TEST(save_edit_garage) {
    GtMode gt(world_data());
    GameState& s = gt.state();
    CHECK(gt.add_car(w::kCoupe, 5) == 0); // the coupe has 2 paints
    CHECK(s.garage.size() == 1 && s.garage[0].car == w::kCoupe && s.garage[0].color == 1);
    CHECK(s.ride == 0 && s.credits == w::kStartCredits && s.prize_cars == 0 && s.garage[0].origin.empty());
    CHECK(s.garage[0].cond.engine_wear_m == -300000 && s.garage[0].parts == gt.make_car(w::kCoupe)->parts);
    CHECK(gt.add_car(w::kRacer, 0) == 0); // prize-only cars too
    CHECK(s.garage[0].car == w::kRacer && s.ride == 1 && s.garage[1].serial < s.garage[0].serial);
    CHECK(gt.add_car("no_such_car", 0) == -1 && s.garage.size() == 2);
    gt.remove_car(0);
    CHECK(s.garage.size() == 1 && s.ride == 0 && s.garage[0].car == w::kCoupe);
    gt.remove_car(0);
    CHECK(s.garage.empty() && s.ride == -1);
    gt.remove_car(3); // (none: nothing)
    while (int(s.garage.size()) < kMaxGarageCars) CHECK(gt.add_car(w::kWagon, 0) == 0);
    CHECK(gt.add_car(w::kWagon, 0) == -1 && int(s.garage.size()) == kMaxGarageCars);
}

// Wins never exceed races.
TEST(save_edit_counts) {
    GameState s;
    set_races(s, 10);
    set_wins(s, 4);
    CHECK(s.races == 10 && s.wins == 4);
    set_races(s, 3);
    CHECK(s.races == 3 && s.wins == 3);
    set_wins(s, 7);
    CHECK(s.races == 7 && s.wins == 7);
    set_races(s, kEditMaxCount + 5);
    CHECK(s.races == kEditMaxCount);
}

// A medal's best time: kept while it earns that medal, else the medal's limit (which earns it);
// no medal clears the record. The licence card follows the licence; the award bit is set.
TEST(save_edit_license_medals) {
    GtMode gt(world_data());
    GameState& s = gt.state();
    CHECK(license_test_code(gt, 0, 0) == "L0B0001" && license_test_code(gt, 0, 7) == "L0B0008");
    CHECK(license_test_code(gt, 3, 2) == "LIA0003"); // (not in the catalog: the usual form)
    set_license_medal(gt, 0, 0, Medal::Silver);
    const LicenseTestRecord& r = s.license_tests[0];
    CHECK(r.medal == Medal::Silver && r.best_time == 32.5 && gt.judge("L0B0001", true, r.best_time) == Medal::Silver);
    CHECK(s.arcade.awards & (0x20000u << int(Medal::Silver)));
    set_license_medal(gt, 0, 0, Medal::Bronze); // 32.5 s earns silver: the bronze limit
    CHECK(r.medal == Medal::Bronze && r.best_time == 35.0);
    s.license_tests[0].best_time = 34.0; // earns bronze: kept
    set_license_medal(gt, 0, 0, Medal::Bronze);
    CHECK(r.best_time == 34.0);
    set_license_medal(gt, 0, 0, Medal::Gold);
    CHECK(r.medal == Medal::Gold && r.best_time == 30.0);
    CHECK(!gt.license_held(0) && !(s.trophy_flags & 1u));
    for (int t = 1; t < kTestsPerLicense; ++t) set_license_medal(gt, 0, t, Medal::Gold);
    CHECK(gt.license_all_gold(0) && (s.trophy_flags & 1u));
    set_license_medal(gt, 0, 4, Medal::None);
    CHECK(s.license_tests[4].medal == Medal::None && s.license_tests[4].best_time < 0);
    CHECK(!gt.license_held(0) && !(s.trophy_flags & 1u));
    set_every_license_medal(gt, Medal::Gold);
    for (int l = 0; l < kLicenseCount; ++l) CHECK(gt.license_all_gold(l) && (s.trophy_flags & (1u << l)));
    set_every_license_medal(gt, Medal::None);
    for (const LicenseTestRecord& t : s.license_tests) CHECK(t.medal == Medal::None && t.best_time < 0);
    CHECK(s.trophy_flags == 0);
}

// Race results: a place, cleared, every slot won (the championship title too), all cleared.
TEST(save_edit_race_results) {
    GtMode gt(world_data());
    GameState& s = gt.state();
    const RaceSeries* champ = gt.data().catalog.series_for("Gsynch_n_0501");
    CHECK(champ && championship_code(*champ) == "Gsynch_n_0500");
    set_race_best(s, "Gsyn_e_0001", 2);
    CHECK(s.best_position("Gsyn_e_0001") == 2);
    set_race_best(s, "Gsyn_e_0001", kNoPosition);
    CHECK(s.race_best.empty());
    win_every_race(gt);
    CHECK(gt.all_races_won() && gt.league_complete("beginner") && gt.league_complete("amateur"));
    CHECK(s.wins == 0 && s.credits == w::kStartCredits && s.garage.empty()); // nothing else changes
    clear_race_results(s);
    CHECK(s.race_best.empty() && gt.completion() == 0);
}

// Every arcade course group (SINGLE_RACE levels 0 and 1: level 2) and car level open; an open
// game stays as open as it is.
TEST(save_edit_arcade) {
    GtMode gt(world_data());
    CHECK(arcade_course_level_max(gt.data()) == 2);
    open_arcade(gt);
    const ArcadeProgress& a = gt.state().arcade;
    CHECK(a.course_level == 2 && gt.arcade_course_open("syn_hill"));
    for (bool f : a.car_flags) CHECK(f);
    for (int k = 1; k <= kArcadeCarLevels; ++k) CHECK(gt.arcade_car_level_open(k));
}

// The engine and oil states, the odometer in the data set's unit.
TEST(save_edit_condition) {
    for (int k = 0; k < kEngineStates; ++k) CHECK(engine_state(engine_wear(EngineState(k))) == EngineState(k));
    CHECK(engine_state(-200000) == EngineState::New && engine_state(250000) == EngineState::RunIn);
    CHECK(engine_state(580000) == EngineState::Worn);
    CHECK(GtMode::engine_factor({0, engine_wear(EngineState::New), 0}) < 1);
    CHECK(GtMode::engine_factor({0, engine_wear(EngineState::RunIn), 0}) == 1);
    for (OilState o : {OilState::Fresh, OilState::Used}) CHECK(oil_state(oil_value(o)) == o);
    CHECK(GtMode::oil_age(oil_value(OilState::Fresh)) == 0 && GtMode::oil_age(oil_value(OilState::Used)) == 1);
    const std::shared_ptr<GtData> data = world_data();
    const GtData& d = *data; // US English: miles
    CHECK(d.imperial());
    CHECK(odometer_units(d, 1609.344 * 12 + 5) == 12 && odometer_metres(d, 12) == 1609.344 * 12);
    CHECK(odometer_metres(d, odometer_units_max(d)) <= double(sim::kOdometerMax) * 2);
}

// The save being played is edited as the game has it (not its file), and the game takes the
// stored edit; another save is read from and written to its file.
TEST(save_edit_session_saves) {
    const std::string playing = test::temp_path("save_edit_playing.sav").string();
    const std::string other = test::temp_path("save_edit_other.sav").string();
    Session session(playing);
    GtModeSession& gs = *gt_mode_session();
    CHECK(gs.save_path() == playing);
    gs.gt().state().credits = 1234; // a change the file has not seen yet
    std::optional<GameState> st = editable_save(playing);
    CHECK(st && st->credits == 1234);
    st->credits = 5678;
    CHECK(store_edited_save(playing, *st));
    CHECK(gs.gt().state().credits == 5678);
    std::optional<GameState> file = read_save(playing);
    CHECK(file && file->credits == 5678);

    GameState g;
    g.credits = 42;
    CHECK(write_save(other, g));
    std::string err;
    std::optional<GameState> o = editable_save(other, &err);
    CHECK(o && o->credits == 42);
    o->day = 77;
    CHECK(store_edited_save(other, *o));
    CHECK(read_save(other)->day == 77 && gs.gt().state().credits == 5678 && gs.gt().state().day != 77);
    CHECK(!editable_save(test::temp_path("save_edit_none.sav").string(), &err));
}
