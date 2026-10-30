// In-race pop-ups in the middle of the screen (presentation §D6): every tick, the lap timers'
// split-point and line crossings become messages on the followed players' event displays.
//
// - The followed car's lap time (every lap, the finishing one too) and split times
//   ("T<n> m'ss.mmm"), queued.
// - Races (one or two players, online): the gap to the first car through each timing point on
//   the lead lap, or the leader's lead when the second car passes, with the other car's
//   picture; "+N Laps" for a car a lap or more down (race::PassingOrder).
// - Time trials and free runs: the gap to the session's best lap so far (race::BestLapSplits;
//   none for the session's first lap: no stored record seeds it; kept across retries).
// - Licence tests: the split times only (their lap time is not shown in a live test).
// - Machine tests (presentation §B7, §D6): 0-400 m / 0-1000 m the split time at 400 m and, at the
//   goal, the time to it; nothing at the line and no gaps (their stored records are not lap
//   times). The top-speed test shows none (its HUD has no event display).
// - Replays (race::lap_event_rules): the re-run posts what the live run did, for the car the
//   cameras follow (presentation §G3), on the single view; no gaps to the best lap, and no
//   lap counts for the session; a licence test's time at its end as a lap time; the top-speed
//   test's split times and top speed at the goal. A solo run's car posts nothing after its
//   end (the run's laps done, or the end-of-run driver taking over). The display is drawn with
//   the REPLAY OPTIONS ITEM time only (Hud::event_row).
// Within a tick the display takes lap times, splits, laps down, gaps, the top speed and then
// the "Pitting in..." messages, in that order. Demonstrations and the attract demo show none
// (ours).

#include "app/screen_race_impl.h"

namespace gt3r::race_screen {

namespace {

EventDisplay::Message green(std::string text, bool large) {
    EventDisplay::Message m;
    m.text = std::move(text);
    m.large = large;
    return m;
}

} // namespace

void RaceScreen::LapList::add(double t) {
    recent.push_back(t);
    if (recent.size() > 8) recent.erase(recent.begin());
    ++count;
    sum += t;
}

// Every run and replay. The session's fastest lap (the gaps' reference and Record Lap / P.P. /
// Fastest) is kept: it lasts the visit, retries included (reset_session_best()).
void RaceScreen::reset_lap_events() {
    for (EventDisplay& d : event_display_) d.clear();
    lap_lists_ = {};
    passing_.reset();
    pending_info_.clear();
    event_icon_car_ = {-1, -1};
    best_splits_.begin_run();
    replay_run_ended_ = replay_license_posted_ = false;
}

// A new visit (the course and car loaded): no fastest lap yet; no stored record seeds it.
void RaceScreen::reset_session_best() {
    best_splits_.reset();
    session_best_ms_ = -1;
    exact_session_.fill(0x157529FF); // (the exact core's: race info + 0x8AC, none)
    exact_free_run_.reset();
}

std::optional<int> RaceScreen::car_at_position(int position) const {
    if (!field_) return position == 1 ? std::optional<int>(-1) : std::nullopt; // alone: the player is first
    std::vector<RaceField::Standing> st = field_->standings();
    if (position < 1 || size_t(position) > st.size()) return std::nullopt;
    return st[size_t(position - 1)].car;
}

void RaceScreen::lap_events(bool timer_updated) {
    struct Pending {
        std::vector<EventDisplay::Message> laps, splits, downs, speeds;
        std::vector<std::pair<long long, std::optional<int>>> gaps; // gap, the other car's picture
    };
    Pending h[2];
    const HudPlay play = replaying_ && !logger_mode_       ? HudPlay::Replay
                         : !replaying_ && !demo_ && !attract_ ? HudPlay::Live
                                                              : HudPlay::Demo;
    const LapEventRules rules =
        lap_event_rules(field_ ? (two_player_ ? HudRun::Battle : HudRun::Race) : hud_run(), play);
    // The car the replay's cameras follow (presentation §G3): the player, or an opponent.
    const int followed = field_ ? replay_focus_ : -1;
    if (play == HudPlay::Demo) {
        // Demonstrations and the attract demo: none (ours).
    } else if (field_) {
        // Races: every car's crossings in race order; the followed players' own messages (in
        // replays the followed car's).
        const std::string laps_fmt = race_text("RACEDISPLAY_LapDiffInfo", "%d Laps");
        for (const RaceField::CarCrossing& x : field_->last_crossings()) {
            const LapCrossing& c = x.crossing;
            const int half = event_display_of(x.car, play, followed);
            if (rules.session_laps && c.line && (session_best_ms_ < 0 || c.split_ms < session_best_ms_))
                session_best_ms_ = c.split_ms;
            // The players' own lap-time lists (the HUD's Last and lap list, presentation §D4).
            const int own = x.car == -1 ? 0 : x.car == -2 ? 1 : -1;
            if (c.line && own >= 0) lap_lists_[size_t(own)].add(double(c.split_ms) / 1000.0);
            if (half >= 0) {
                if (c.line) h[half].laps.push_back(green(lap_time_text(c.split_ms), true));
                else h[half].splits.push_back(green(split_text(c.point + 1, c.split_ms), false));
            }
            const int down = passing_.pass(x.car, c.lap, c.point, c.ms);
            if (down > 0 && half >= 0) {
                EventDisplay::Message m;
                m.text = lap_diff_text(down, laps_fmt);
                h[half].downs.push_back(m);
            }
        }
        for (const PassingOrder::Gap& g : passing_.take_gaps()) {
            const int half = event_display_of(g.car, play, followed);
            if (half < 0) continue;
            // The picture: the race leader when behind, the second car when ahead.
            h[half].gaps.push_back({g.ms, car_at_position(g.ms >= 0 ? 1 : 2)});
        }
    } else if (machine_) {
        // Machine tests: the timing points of this tick (race::MachineTest); the time to the
        // goal comes like a lap time. The top-speed test's live HUD has no event display:
        // nothing; its replays show the split times and, at the goal, the top speed so far.
        for (const MachineTest::Crossing& c : machine_marks_) {
            if (c.goal && rules.top_speed) h[0].speeds.push_back(machine_speed_message(hud_top_kmh_, imperial_));
            else if (c.goal ? rules.lap_times : rules.splits)
                (c.goal ? h[0].laps : h[0].splits).push_back(machine_test_message(c));
        }
    } else if (timer_updated && !tt_failed_ && !replay_run_ended_) {
        const bool license = !license_event_.empty();
        const int points = int(track_->split_points().size()) + 1; // with the line
        for (const LapCrossing& c : track_->lap_crossings()) {
            if (license) { // splits numbered on through the laps; no lap time in a live test
                if (!c.line) h[0].splits.push_back(green(split_text(c.lap * points + c.point + 1, c.split_ms), false));
                continue;
            }
            // Time trials and free runs: the gap to the best lap so far, taken before this lap
            // updates it; the player's own picture when slower (it is first, and alone).
            std::optional<long long> gap;
            if (c.line) {
                if (rules.best_lap_gaps) gap = best_splits_.lap_gap(c.split_ms);
                h[0].laps.push_back(green(lap_time_text(c.split_ms), true));
                // A free run's lap under the session's best (none yet: any) enables the quick
                // menu's SAVE GHOST and DATA LOGGER (menus §11).
                if (rules.session_laps && run_kind() == RunKind::FreeRun &&
                    (best_splits_.best_lap() < 0 || c.split_ms < best_splits_.best_lap()))
                    run_session_record_ = true;
                if (rules.session_laps) best_splits_.lap_done(c.split_ms);
                lap_lists_[0].add(double(c.split_ms) / 1000.0);
                // A replay's run ends at the line where the live one did (its laps done).
                if (play == HudPlay::Replay && solo_run_finished(track_->laps_completed(), race_laps_))
                    replay_run_ended_ = true;
            } else {
                if (rules.best_lap_gaps) gap = best_splits_.split_gap(c.point, c.split_ms);
                h[0].splits.push_back(green(split_text(c.point + 1, c.split_ms), false));
                if (rules.session_laps) best_splits_.split(c.point, c.split_ms);
            }
            if (gap) h[0].gaps.push_back({*gap, car_at_position(*gap >= 0 ? 1 : 2)});
        }
        // A licence test's replay: at the time its live run ended (passed, or slower than
        // bronze; not failed), that time as a lap time (the licence display shows it in
        // replays only).
        if (rules.license_time && !replay_license_posted_ && music_license_end_s_ >= 0 &&
            race_time_ >= music_license_end_s_) {
            replay_license_posted_ = true;
            if (!music_license_failed_)
                h[0].laps.push_back(green(lap_time_text(std::llround(license_.time() * 1000.0)), true));
        }
    }
    const int halves = two_player_ && field_ && field_->has_second() ? 2 : 1;
    for (int i = 0; i < halves; ++i) {
        EventDisplay& d = event_display_[size_t(i)];
        for (EventDisplay::Message& m : h[i].laps) d.queue(std::move(m));
        for (EventDisplay::Message& m : h[i].splits) d.queue(std::move(m));
        for (EventDisplay::Message& m : h[i].downs) d.lap_down(std::move(m.text));
        for (const auto& [gap, car] : h[i].gaps) d.gap(gap, car.has_value(), car.value_or(0));
        for (EventDisplay::Message& m : h[i].speeds) d.queue(std::move(m));
        for (const std::string& t : pending_info_) d.queue(green(t, false)); // every car's, on both halves
        d.tick();
    }
    pending_info_.clear();
}

void RaceScreen::draw_event_icons(App& app, int half) {
    const SDL_FRect& rc = hud_.event_icon_rect();
    if (rc.w > 0) draw_car_icon(app, event_icon_car_[size_t(half)], rc, size_t(12 + half), true);
    // The free practice's P.P. line: the picture of the car holding the fastest lap.
    const SDL_FRect& pp = hud_.pole_icon_rect();
    if (half == 0 && pp.w > 0) draw_car_icon(app, practice_pole_car_, pp, 14, true);
}

} // namespace gt3r::race_screen
