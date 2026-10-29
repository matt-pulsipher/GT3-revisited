// GT-mode menu bindings: race requests (GameRequest: the event, the garage car's spec, the
// opponents, championship rounds) and results (prize money, championship rounds, license
// medals, records, endings, prize cars and their presentations, then an autosave); resuming a
// championship; the title's attract demos.

#include "game/gt_mode_menu/internal.h"

#include "base/log.h"
#include "game/language_rules.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gt3r::game::gt_menu {

namespace {

// The car_get reason text (a hidden text element of that page) for a prize set name.
std::string prize_reason(const std::string& set) {
    if (int lic = GtMode::license_index_of(set); lic >= 0) {
        static const char* const n[] = {"b", "a", "ib", "ia", "s", "r"};
        return std::string("license_") + n[lic] + "_all_gold";
    }
    if (set == "gtmode_amateur_complete") return "gtmode_ama_complete";
    if (set == "achieve_half") return "achieve50";
    if (set == "achieve_3_quoter") return "achieve75";
    if (set == "achieve_complete") return "achieve100";
    if (set.rfind("gtmode_", 0) == 0) return set;
    if (set.rfind("E", 0) == 0) return "enduro_win";
    if (set.size() > 5 && set.compare(set.size() - 5, 5, "_0000") == 0) return "solo_race_clear";
    return "championship_win";
}

// The ride car (gt_menu::fill_car below).
void fill_car(GameRequest& r, const GtMode& gt) { gt_menu::fill_car(r, gt, gt.state().ride_car()); }

} // namespace

void fill_car(GameRequest& r, const GtMode& gt, const GarageCar* c) {
    if (!c) return;
    r.car_parts = c->parts;
    r.car_settings = c->settings;
    r.car = c->car;
    r.car_model = gt.model_of(*c);
    r.car_color = c->color;
    r.car_wheel = c->wheel;
    const ByteSpan* gear = gt.db().record(data::TableId::Gear, c->parts[size_t(part_slot(17))]);
    r.racing_gearbox = gear && gear->size() > 0x10 && (*gear)[0x10] != 0;
}

void fill_condition(GameRequest& r, const GtMode& gt, const GarageCar* c) {
    if (!GtMode::carries_engine_condition(r.mode)) return;
    const sim::EngineCondition e = c ? GtMode::run_condition(c->cond) : sim::EngineCondition{};
    r.car_condition = true;
    r.car_odometer = e.odometer;
    r.car_wear_m = e.wear_m, r.car_oil_m = e.oil_m;
    r.garage_car = c ? gt.garage_index(*c) : -1;
}

void apply_condition(GtMode& gt, const GameResult& r) {
    if (r.car_condition) gt.end_run(r.garage_car, {r.car_odometer, r.car_wear_m, r.car_oil_m}, r.odometer_added);
}

void prepare_request(GameRequest& r) {
    MenuState& ms = menu_state();
    GtModeSession* s = S();
    if (!s) return;
    GtMode& gt = s->gt();
    if (r.mode == "go_race") {
        const Championship& ch = gt.state().championship;
        if (ch.active()) {
            r.race_code = ch.races[size_t(ch.next)];
            r.opponents = ch.entrants;
            r.champ_round = ch.next + 1;
            r.champ_rounds = int(ch.races.size());
            r.champ_last = r.champ_round == r.champ_rounds;
        } else {
            r.race_code = s->race_code;
            r.opponents = gt.pick_opponents(r.race_code);
        }
        r.event = r.race_code;
        fill_car(r, gt);
        fill_condition(r, gt, gt.state().ride_car());
    } else if (r.mode == "go_license") {
        r.event = r.race_code;
        // The test's car in the game's language (gt_mode §21, docs/dev/GT_MODE.md "Language").
        if (const data::Event* ev = gt.db().event(r.race_code))
            r.car = license_car(gt.db(), *ev, gt.data().language).entry;
    } else if (r.mode == "go_machine_test") {
        r.event = r.race_code;
        fill_car(r, gt);
        fill_condition(r, gt, gt.state().ride_car());
    } else if (r.mode == "go_free_run") {
        r.event = r.race_code;
        r.course = ms.free_run_course;
        fill_car(r, gt);
        fill_condition(r, gt, gt.state().ride_car());
    } else if (r.mode == "go_preview") {
        r.race_code = s->race_code;
        r.event = s->race_code;
    }
    ms.last_request = r;
}

std::vector<std::string> on_result(const GameResult& r) {
    MenuState& ms = menu_state();
    std::vector<std::string> log;
    ms.round_pending = false;
    const size_t first_show = ms.shows.size();
    GtModeSession* s = S();
    if (!s) return to_log_file(log);
    GtMode& gt = s->gt();
    // The run's engine condition back into the garage car, its distance into the total
    // (physics §10.3), before any prize car shifts the garage.
    apply_condition(gt, r);
    // The dirt the race built up on the car (washed off at the car wash).
    if (r.car_dirt >= 0 && r.mode != "attract_demo")
        if (GarageCar* ride = gt.state().ride_car()) ride->cond.dirt = float(std::clamp(r.car_dirt, 0.0, 0.8));
    const int place = r.finished && r.position > 0 ? r.position - 1 : -1;
    Award a;
    if (r.mode == "go_race") {
        // Only a started race counts (gt_mode §3.4, §22.2): leaving from the pre-race menu records
        // nothing. A championship round that was run counts even when the player then retires
        // from the championship (its prize page has been shown); retiring ends the championship,
        // and so does leaving a round's pre-race menu (its only way out is that EXIT).
        if (gt.state().championship.active()) {
            if (r.started) {
                // Finishing order as entrant indices (0 = the player, i + 1 = opponent i).
                std::vector<int> order;
                for (int o : r.order) order.push_back(o + 1);
                a = gt.record_round(place, order);
                if (a.championship_over)
                    log.push_back(a.champion ? "Champion! bonus " + cr(a.bonus) : "championship lost on points");
            }
            if ((r.abandoned || !r.started) && gt.state().championship.active()) {
                log.push_back("championship abandoned");
                gt.state().championship = Championship{};
            }
            ms.round_pending = gt.state().championship.active();
        } else if (r.started) {
            a = gt.record_race(r.event.empty() ? r.race_code : r.event, place);
        }
        if (place >= 0) log.push_back("finished " + std::to_string(place + 1) + (a.money ? ": " + cr(a.money) : ""));
        // The race prize the original shows before anything else (presentation §F):
        // a cup for places 1-3 (race/g_<class>_<gold|silver|bronze>.prm, the event's trophy
        // class +0x1D3 = 5..8) or the consolation prize (race/consolation.prm) further back,
        // each with the prize money counted into the credits; nothing without prize money.
        if (place >= 0 && a.money - a.bonus > 0 && !r.prize_shown)
            if (std::optional<Show> sh = race_prize_show(gt, r.event.empty() ? r.race_code : r.event, place, a.money - a.bonus))
                ms.shows.insert(ms.shows.begin() + std::ptrdiff_t(first_show), std::move(*sh));
    } else if (r.mode == "go_license") {
        if (r.license_medal >= -1) {
            // Judged race-side: take the medal as is (and cross-check with our own judgement).
            Medal ours = gt.judge(r.race_code, r.finished, r.total_time);
            if (int(ours) != r.license_medal)
                LOG_WARN("gt mode: license %s: race judged %d, GT mode %d (time %.3f)", r.race_code.c_str(),
                         r.license_medal, int(ours), r.total_time);
            a = gt.record_license_medal(r.race_code, Medal(r.license_medal), r.total_time);
        } else {
            a = gt.record_license(r.race_code, r.finished, r.total_time);
        }
        log.push_back(std::string("license test: ") + medal_name(a.medal));
        for (const std::string& m : a.messages) log.push_back(m);
    }
    // (The rankings of licence tests, machine tests and free runs went in from the race screen
    // after each run, with their name entries, as the original's drivers do (gt_mode §17);
    // nothing is recorded from the result.)
    // Endings (gt_mode §10), checked whenever the result opened the prize-car check (a race
    // won, a championship won on points whatever its last round's place, a licence all gold;
    // after the car_get check): Beginner league complete -> the short ending, then
    // Amateur or Professional complete -> the long ending, each once; both can play.
    if (a.prize_check) {
        GameState& st = gt.state();
        if (!st.ending_short_seen && gt.league_complete("beginner")) {
            st.ending_short_seen = true;
            ms.movies.push_back("stream/ending_short_full.pss");
        }
        if (!st.ending_long_seen && (gt.league_complete("amateur") || gt.league_complete("pro"))) {
            st.ending_long_seen = true;
            ms.movies.push_back("stream/ending_long_full.pss");
        }
    }
    if (a.license_obtained) {
        std::string id = license_menu_id(License(GtMode::license_index_of(r.race_code)));
        // The license card (lic_b, a, ib, ia, s, d = rally) over the two messages, shown in turn.
        ms.shows.push_back({"lc_get", "license_get", {id + "_mess0"}, "", "", 0, {}, "", -1, "menu/lic_" + id + ".prm", 0, "", true, {}});
    }
    // A championship won: its trophy (picked by record_round, menu/champ_NN.prm) and the
    // bonus, before the prize car.
    // Screenshots: GT3R_GTMODE_FAKE_TROPHY=<NN> shows trophy champ_NN after any race win.
    const char* fake_trophy = std::getenv("GT3R_GTMODE_FAKE_TROPHY");
    if (a.champion || (fake_trophy && *fake_trophy && r.mode == "go_race" && place == 0)) {
        // (GT3R_GTMODE_FAKE_TROPHY=<NN>:<bonus> also counts a bonus.)
        int t = a.champion && a.trophy >= 0 ? a.trophy : std::clamp(std::atoi(fake_trophy ? fake_trophy : "0"), 0, 9);
        s64 bonus = a.bonus;
        if (!a.champion && fake_trophy)
            if (const char* c = std::strchr(fake_trophy, ':')) bonus = std::atoll(c + 1);
        char trophy[32];
        std::snprintf(trophy, sizeof trophy, "menu/champ_%02d.prm", t);
        Show sh{"trophy_get", "trophy_get", {"mess0"}, "", "", 0, {}, "", -1, trophy, bonus, "", a.champion, {}};
        sh.counter.delay = kTrophyCountDelay;
        ms.shows.push_back(std::move(sh));
    }
    for (int i : a.new_cars)
        if (i >= 0 && i < int(gt.state().garage.size())) {
            const GarageCar& c = gt.state().garage[size_t(i)];
            log.push_back("prize car: " + car_name(gt, c.car));
            Show sh{"car_get", "car_get", {}, prize_reason(c.origin), c.car, c.color, {}, "", -1, "", 0, "", true, {}};
            if (const PrizeSet* ps = gt.data().prize_set(c.origin))
                for (const PrizeCar& pc : ps->cars) sh.candidates.push_back(gt.data().prize_candidate(pc).car);
            ms.shows.push_back(std::move(sh));
        }
    // A prize car that did not fit in the garage: "Garage is full" (as the original).
    if (gt.take_prizes_refused() > 0) {
        Show sh{};
        sh.set = "not_get_car";
        sh.page = "not_car_get";
        ms.shows.push_back(std::move(sh));
    }
    autosave(&log);
    return to_log_file(log);
}

std::optional<GameRequest> next_request() {
    MenuState& ms = menu_state();
    if (!ms.round_pending || !ms.last_request || !S()) return std::nullopt;
    ms.round_pending = false;
    GameRequest r = *ms.last_request;
    prepare_request(r);
    return r;
}

std::optional<GameRequest> resume_request() {
    GtModeSession* s = S();
    if (!s || !s->gt().state().championship.active() || !s->gt().state().ride_car()) return std::nullopt;
    GameRequest r;
    r.mode = "go_race";
    r.page = "map";
    prepare_request(r);
    return r;
}

// Attract mode: the title idle time (gcf section 0) and the demo races (gcf
// section 12, cycling; the F1 demos only once the S license is held).
int attract_idle_frames() {
    const MenuState& ms = menu_state();
    return ms.data ? ms.data->title_idle_seconds * 60 : 0;
}

std::optional<GameRequest> attract_demo() {
    MenuState& ms = menu_state();
    if (!ms.data || ms.data->demos.empty()) return std::nullopt;
    bool s_license = S() && S()->gt().license_held(4);
    for (size_t tries = 0; tries < ms.data->demos.size(); ++tries) {
        ms.attract_index = (ms.attract_index + 1) % ms.data->demos.size();
        const GtData::Demo& d = ms.data->demos[ms.attract_index];
        if (d.level > (s_license ? 1 : 0)) continue;
        GameRequest r;
        r.mode = "attract_demo";
        r.race_code = r.event = d.event;
        r.course = d.course;
        r.car = d.car; // "" for every demo on the disc: the pool fills slot 0 too
        r.page = "root";
        return r;
    }
    return std::nullopt;
}

} // namespace gt3r::game::gt_menu
