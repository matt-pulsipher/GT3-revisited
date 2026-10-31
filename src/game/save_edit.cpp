#include "game/save_edit.h"

#include "sim/engine_condition.h"

#include <algorithm>

namespace gt3r::game {

namespace {

constexpr float kNewEngine = -300000, kWornEngine = 600000; // physics §10.3
constexpr float kFreshOil = -300000, kUsedOil = 300000;     // the oil change's value, the worn one
constexpr double kMile = 1609.344;

LicenseTestRecord& test_record(GameState& s, int license, int test) {
    return s.license_tests[size_t(license * kTestsPerLicense + test)];
}

} // namespace

void set_races(GameState& s, u32 races) {
    s.races = std::min(races, kEditMaxCount);
    s.wins = std::min(s.wins, s.races);
}

void set_wins(GameState& s, u32 wins) {
    s.wins = std::min(wins, kEditMaxCount);
    s.races = std::max(s.races, s.wins);
}

std::string license_test_code(const GtMode& gt, int license, int test) {
    if (const LicenseCourse* lc = gt.data().catalog.license(license_menu_id(License(license))))
        for (const std::string& code : lc->tests)
            if (GtMode::license_index_of(code) == license && code.size() >= 7 && code[6] - '1' == test) return code;
    static const char* const kPrefix[kLicenseCount] = {"L0B", "L0A", "LIB", "LIA", "L0S", "L0R"};
    if (license < 0 || license >= kLicenseCount) return {};
    return std::string(kPrefix[license]) + "000" + char('1' + test);
}

void set_license_medal(GtMode& gt, int license, int test, Medal m) {
    if (license < 0 || license >= kLicenseCount || test < 0 || test >= kTestsPerLicense) return;
    GameState& s = gt.state();
    LicenseTestRecord& rec = test_record(s, license, test);
    if (m == Medal::None) {
        rec = LicenseTestRecord{};
    } else {
        const std::string code = license_test_code(gt, license, test);
        rec.medal = m;
        if (rec.best_time <= 0 || gt.judge(code, true, rec.best_time) != m) {
            std::optional<EventRules> r = gt.rules(code);
            const u32 limit = r ? r->license_ms[size_t(m)] : 0;
            rec.best_time = limit ? limit / 1000.0 : -1; // (judge: a time at the limit earns it)
        }
        s.arcade.awards |= 0x20000u << int(m);
    }
    if (gt.license_held(license)) s.trophy_flags |= 1u << license;
    else s.trophy_flags &= ~(1u << license);
}

void set_every_license_medal(GtMode& gt, Medal m) {
    for (int l = 0; l < kLicenseCount; ++l)
        for (int t = 0; t < kTestsPerLicense; ++t) set_license_medal(gt, l, t, m);
}

std::string championship_code(const RaceSeries& s) {
    if (s.races.empty() || s.races.front().size() < 2) return {};
    return s.races.front().substr(0, s.races.front().size() - 2) + "00";
}

void set_race_best(GameState& s, const std::string& code, int place) {
    if (place < 0 || place >= kNoPosition) s.race_best.erase(code);
    else s.race_best[code] = place;
}

void win_every_race(GtMode& gt) {
    for (const std::string& code : gt.data().race_codes) gt.state().race_best[code] = 0;
}

void clear_race_results(GameState& s) { s.race_best.clear(); }

int arcade_course_level_max(const GtData& data) {
    // As the unlock pass opens them (GtMode::arcade_unlock_pass): the level after the highest
    // course group.
    auto it = data.arcade_courses.find("SINGLE_RACE");
    int max_level = 0;
    if (it != data.arcade_courses.end())
        for (const GtData::ArcadeCourse& c : it->second) max_level = std::max(max_level, c.level);
    return max_level + 1;
}

void open_arcade(GtMode& gt) {
    ArcadeProgress& a = gt.state().arcade;
    a.course_level = std::max(a.course_level, arcade_course_level_max(gt.data()));
    a.car_flags.fill(true);
}

EngineState engine_state(float wear_m) {
    if (wear_m < kNewEngine / 2) return EngineState::New;
    if (wear_m > kWornEngine - 50000) return EngineState::Worn; // (100 % up to 500 km, 98 % from 600 km)
    return EngineState::RunIn;
}

float engine_wear(EngineState e) {
    switch (e) {
    case EngineState::New: return kNewEngine;
    case EngineState::Worn: return kWornEngine;
    default: return 0;
    }
}

OilState oil_state(float oil_m) { return GtMode::oil_age(oil_m) < 0.5f ? OilState::Fresh : OilState::Used; }

float oil_value(OilState o) { return o == OilState::Fresh ? kFreshOil : kUsedOil; }

u32 odometer_units(const GtData& data, double odometer_m) {
    const double u = std::max(0.0, odometer_m) / (data.imperial() ? kMile : 1000.0);
    return u32(std::min(u, double(odometer_units_max(data))));
}

double odometer_metres(const GtData& data, u32 units) {
    return std::min(double(units) * (data.imperial() ? kMile : 1000.0), double(sim::kOdometerMax) * 2);
}

u32 odometer_units_max(const GtData& data) {
    return u32(double(sim::kOdometerMax) * 2 / (data.imperial() ? kMile : 1000.0));
}

} // namespace gt3r::game
