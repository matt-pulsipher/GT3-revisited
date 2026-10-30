// The end of a run: result rows (names, times), the result latched and posted to the
// menus (menu::flow::GameResult), leaving the screen, and the race display strings.
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"

namespace gt3r::race_screen {

// Game text cut to ASCII (a lead byte becomes '-'), for text drawn with the UTF-8 path.
std::string RaceScreen::ascii(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (c < 0x80) out += char(c);
        else if ((c & 0xC0) == 0xC0) out += '-';
    return out;
}

// Game text (race::result_car_name): every consumer draws it with the game font.
std::string RaceScreen::car_display_name(const std::string& label, bool full) const {
    return race::result_car_name(db_, label, full);
}

std::vector<ResultRow> RaceScreen::result_rows(bool full_names) const {
    std::vector<ResultRow> rows;
    if (!field_) {
        rows.push_back({car_.name, menu_time_text(track_->best_lap_time()), true}); // (none: kNoTimeText)
        return rows;
    }
    std::vector<RaceField::Standing> st = field_->standings();
    if (st.empty()) return rows;
    const RaceField::Standing& w = st[0];
    for (size_t i = 0; i < st.size(); ++i) {
        const RaceField::Standing& c = st[i];
        ResultRow r;
        if (c.car == -1) r.name = full_names && db_.car(car_.label) ? car_display_name(car_.label, true) : car_.name;
        else if (c.car == -2) r.name = car_display_name(preset_.car2.empty() ? car_.label : preset_.car2, full_names);
        else if (online_ && size_t(c.car) < net_.names.size()) // (a player's name is UTF-8)
            r.name = text::encode_game_text(text::utf8_decode(net_.names[size_t(c.car)]), hud_.fonts().jis());
        else r.name = car_display_name(field_->opponent_label(c.car), full_names);
        r.time = result_time_text(i == 0, c.finished, c.time, w.time, std::max(0, w.laps - c.laps));
        r.player = c.car == -1 && !two_player_;
        r.car = c.car;
        rows.push_back(r);
    }
    return rows;
}

// The result quick menu's rows, in race order: every finished car's own race time (the
// reference capture), not the gaps the overlay shows, in the menus' format; "no time" for a
// car still racing.
std::vector<ResultRow> RaceScreen::result_menu_rows() const {
    std::vector<ResultRow> rows = result_rows();
    if (field_) {
        std::vector<RaceField::Standing> st = field_->standings();
        for (size_t i = 0; i < rows.size() && i < st.size(); ++i)
            rows[i].time = st[i].finished ? menu_time_text(st[i].time) : std::string(kNoTimeText);
    }
    return rows;
}

// The live run ended (results shown / left): latch its result (RaceResultLatch) and, in a
// race, the result menu's rows; every run, as its replay and the quick menu's backdrop re-run
// it (a licence's result still merges the visit's best run, build_result). GT single races
// also go into the save at once, as the original records them before the replay.
void RaceScreen::latch_result() {
    if (replaying_ || attract_ || demo_ || result_latch_.has()) return;
    menu::flow::GameResult r = build_result();
    result_latch_.capture(r);
    if (field_) latched_rows_ = result_menu_rows();
    LOG_INFO("Race: result latched: finished %d position %d", int(r.finished), r.position);
    if (from_menu_ && r.mode == "go_race" && r.finished && !championship() && !launch_.request.practice)
        game::save_race_run(r);
}

menu::flow::GameResult RaceScreen::build_result() const {
    const menu::flow::GameRequest& rq = launch_.request;
    menu::flow::GameResult r;
    r.mode = rq.mode;
    r.race_code = rq.race_code;
    r.event = rq.event;
    r.lap_times = lap_times_;
    r.best_lap = track_ ? track_->best_lap_time() : -1;
    // The engine condition the live runs leave (physics §10.3), for the garage car.
    if (has_condition_ && vehicle_ && vehicle_->facts()) {
        const sim::EngineCondition c = live_condition();
        r.car_condition = true;
        r.car_odometer = c.odometer;
        r.odometer_added = c.odometer - start_condition_.odometer;
        r.car_wear_m = c.wear_m, r.car_oil_m = c.oil_m;
        r.garage_car = rq.garage_car;
    }
    if (field_) {
        r.finished = finished_;
        r.cars = field_->opponents() + 1;
        r.position = finished_ ? finish_position_ : 0;
        if (finished_) {
            double sum = 0;
            for (double t : lap_times_) sum += t;
            r.total_time = sum;
        }
    } else {
        r.cars = 1;
        // Time trial: finished at the line (not a FAIL); free runs have no finish: a run with a
        // lap. The time is the run's best lap (the one it ranks with). Licence test: the
        // judged run.
        r.finished = race_laps_ > 0 ? finished_ && !tt_failed_ : !lap_times_.empty();
        r.position = r.finished ? 1 : 0;
        if (r.finished && track_) r.total_time = track_->best_lap_time();
        if (!license_event_.empty()) {
            r.finished = license_.state() == LicenseRun::State::Finished;
            r.position = r.finished ? 1 : 0;
            r.total_time = r.finished ? license_.time() : -1;
        }
    }
    // Finishing order as indices into rq.opponents (-1 = the player), when GameResult has it.
    [&](auto& res) { // generic: compiles whether or not GameResult has `order` yet
        if constexpr (requires { res.order; }) {
            if (field_)
                for (const RaceField::Standing& st : field_->standings())
                    if (st.car >= -1) res.order.push_back(st.car);
        }
    }(r);
    r.abandoned = abandoned_;
    r.started = !prerace_; // left from the pre-race menu (a free session ends back in it): no race
    [&](auto& res) {
        if constexpr (requires { res.records_done; }) res.records_done = any_run_recorded_;
        if constexpr (requires { res.prize_shown; }) res.prize_shown = prize_shown_;
    }(r);
    if (attract_) r.finished = !attract_interrupted_;
    if (machine_) { // machine tests: finished at the goal (the top-speed test: or by its pause EXIT);
                    // 0-400 m / 0-1000 m: the time to it
        r.finished = machine_time_ > 0 || (exit_finished_ && machine_test_top_speed());
        r.position = r.finished ? 1 : 0;
        r.total_time = machine_time_test() ? machine_time_ : -1;
    }
    [&](auto& res) {
        if constexpr (requires { res.top_speed_kmh; }) res.top_speed_kmh = top_speed_kmh_;
        if constexpr (requires { res.car_dirt; }) res.car_dirt = car_dirt_; // GT mode: the garage car's dirt after the race
    }(r);
    // Licence tests: the medal (GT mode records it as is) and why a run failed.
    if (!license_event_.empty())
        if (const data::Event* ev = db_.event(license_event_))
            [&](auto& res) {
                if constexpr (requires { res.license_medal; }) {
                    res.license_medal = res.finished ? license_medal(license_times(*ev), res.total_time) : -1;
                    res.failure = license_.failure();
                    // A finished run earlier in this visit (then Retry, Replay...): its result.
                    if (best_license_ && (!res.finished || (best_license_->medal >= 0 &&
                                                            (res.license_medal < 0 || best_license_->medal < res.license_medal)))) {
                        res.finished = true;
                        res.position = 1;
                        res.total_time = best_license_->time;
                        res.license_medal = best_license_->medal;
                        res.failure.clear();
                    }
                }
            }(r);
    if (!license_event_.empty())
        [&](const auto& res) {
            if constexpr (requires { res.license_medal; })
                LOG_INFO("license result posted: %s medal %d time %.3f finished %d%s%s", license_event_.c_str(),
                         res.license_medal, res.total_time, int(res.finished), res.failure.empty() ? "" : " failure ",
                         res.failure.c_str());
        }(r);
    LOG_INFO("Race: result to the menus: %s started %d finished %d position %d/%d best %.3f laps %zu",
             r.mode.c_str(), int(r.started), int(r.finished), r.position, r.cars, r.best_lap, r.lap_times.size());
    return r;
}

void RaceScreen::leave(App& app) {
    if (free_run_) {
        end_free_run(app);
        return;
    }
    if (online_) {
        if (!finished_) net::online().retire();
        net::online().poll();
        silence(app);
        music_.stop(app.audio());
        ambience_.reset(app.audio().voices());
        app.pop(); // back to the lobby
        return;
    }
    silence(app);
    music_.stop(app.audio());
    ambience_.reset(app.audio().voices());
    log_perf(true);
    bool was_replay = replaying_;
    replaying_ = false;
    paused_ = false;
    if (from_menu_ && theater_) {
        app.pop(); // the theater: nothing to report
        return;
    }
    if (from_menu_) {
        if (!was_replay) latch_result(); // left from the live race
        // The latched live run (also when left from a replay); a licence keeps its judged run.
        if (!was_replay || result_latch_.has() || !license_event_.empty()) post_result();
        else LOG_INFO("Race: no result posted (left from a replay)");
        app.pop(); // back to the menus
    }
    else stage_ = Stage::Select;
}

// The licence presenter's medal: 0..2, or -1 (FAIL, or slower than bronze).
int RaceScreen::presented_medal() const {
    return license_.state() == LicenseRun::State::Finished && shown_medal_ >= 0 ? shown_medal_ : -1;
}

// A race display string in the disc's text table (text/realtime.dat) in the game's language
// (else US English), or `fallback`.
std::string RaceScreen::race_text(const char* key, const char* fallback) {
    if (!race_strings_tried_ && app_ && app_->vfs()) {
        race_strings_tried_ = true;
        try {
            if (std::optional<Bytes> d = app_->vfs()->read("text/realtime.dat")) race_strings_ = text::StringTable::parse(*d);
        } catch (const std::exception& e) {
            LOG_WARN("Race: text/realtime.dat: %s", e.what());
        }
    }
    if (race_strings_) {
        int i = race_strings_->find(key);
        if (i >= 0) {
            const text::Language own = text::language_from_code(lang_).value_or(text::Language::US);
            for (text::Language l : {own, text::Language::US}) {
                if (int(l) >= race_strings_->language_count()) continue;
                std::string v = race_strings_->utf8(size_t(i), l, nullptr);
                if (!v.empty()) return v;
            }
        }
    }
    return fallback;
}

} // namespace gt3r::race_screen
