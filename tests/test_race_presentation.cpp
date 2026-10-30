// Race presentation rules without game data: records and the RETRY lock, the speedometer
// scale, the HUD layout per mode, the replay director's view modes, the attract demo's view
// pick, the race exit wash, the time trial's finish, the pause window's EXIT question, the
// quick menus' cancel and disabled items, what follows a run (the automatic replay, the menu's backdrop, pause
// EXIT as a finish), the start title's and the replay mark's clocks, the HUD's corner blocks
// beside touch controls. All values are invented.

#include "test_framework.h"

#include "menu_flow_synth.h"

#include "base/config.h"
#include "base/game_random.h"
#include "menu/flow/menu_controller.h"
#include "menu/flow/screen_transition.h"
#include "race/field.h"
#include "race/hud.h"
#include "race/hud_clocks.h"
#include "race/machine_test.h"
#include "race/music_markers.h"
#include "race/procedural_cameras.h"
#include "race/pause_menu.h"
#include "race/post_run.h"
#include "race/replay_cameras.h"
#include "race/replay_focus.h"
#include "race/replay_options.h"
#include "race/results.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::race;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

GtrcCurve constant(float v) {
    GtrcCurve c;
    c.keys = {{0.0f, v}};
    return c;
}

GtrcCurve ramp(float v0, float v1) {
    GtrcCurve c;
    c.keys = {{0.0f, v0}, {1.0f, v1}};
    return c;
}

// A trackside record over [start, end) with its camera at (x, 5, z), aimed at a fixed point.
GtrcRecord trackside(float start, float end, float x, float z) {
    GtrcRecord r;
    r.kind = 0;
    r.start = start, r.end = end;
    r.zoom = constant(2.0f);
    r.pos[0] = constant(x), r.pos[1] = constant(5), r.pos[2] = constant(z);
    r.target[0] = constant(x + 10), r.target[1] = constant(0), r.target[2] = constant(z);
    return r;
}

// One lap variant (L 1000 m, script end 3000 m, first record at 1000 m): D = 1000 + s.
GtrcScript script_of(std::vector<GtrcRecord> records) {
    GtrcScript g;
    g.lap_length = 1000;
    g.script_end = 3000;
    g.records = std::move(records);
    return g;
}

PauseMenu::Result press(PauseMenu& m, menu::flow::MenuKey k) {
    PauseMenu::Result r = m.tick(menu::flow::key_bit(k));
    m.tick(0);
    return r;
}

} // namespace

TEST(race_record_rank) {
    // Empty or short tables: the first empty place.
    CHECK(record_rank({}, 61.0) == 0);
    CHECK(record_rank({60.0}, 61.0) == 1);
    // A tie ranks ahead of the stored entry; slower entries move down.
    std::vector<double> t{60.0, 62.0, 62.0, 70.0};
    CHECK(record_rank(t, 59.0) == 0 && record_rank(t, 60.0) == 0);
    CHECK(record_rank(t, 62.0) == 1 && record_rank(t, 61.9) == 1);
    CHECK(record_rank(t, 62.1) == 3 && record_rank(t, 71.0) == 4);
    // Twelve places: a tie with the 12th is 12th, slower is out.
    std::vector<double> full;
    for (int i = 0; i < 12; ++i) full.push_back(50.0 + i);
    CHECK(record_rank(full, 61.0) == 11 && record_rank(full, 61.5) == -1);
    std::vector<double> longer = full;
    longer.push_back(80.0); // nothing past the 12th place counts
    CHECK(record_rank(longer, 70.0) == -1);
    // The top-speed test: higher first, ties ahead.
    std::vector<double> sp{300.0, 290.0};
    CHECK(record_rank(sp, 300.0, true) == 0 && record_rank(sp, 295.0, true) == 1 && record_rank(sp, 280.0, true) == 2);

    CHECK(std::strcmp(TimeTrialOverlay::record_label(0, false), "NEW RECORD") == 0);
    CHECK(std::strcmp(TimeTrialOverlay::record_label(0, true), "NEW RECORD") == 0);
    CHECK(std::strcmp(TimeTrialOverlay::record_label(3, false), "RECORD TIME") == 0);
    CHECK(std::strcmp(TimeTrialOverlay::record_label(11, true), "RECORD SPEED") == 0);
    CHECK(!TimeTrialOverlay::record_label(12, false) && !TimeTrialOverlay::record_label(-1, false));
    // "%d.%1d" with the unit, both parts truncated; mph x 0.62139; dashes for none.
    CHECK(record_speed_text(312.47, false) == "312.4km/h");
    CHECK(record_speed_text(200.0, true) == "124.2mph");
    CHECK(record_speed_text(0.0, true) == "\x7F\x7F\x7F.\x7Fmph"); // the font's dashes
}

TEST(race_retry_lock) {
    // presentation §E3: the session record is a time at or under the session's best lap
    // before it (whole ms); none yet: any time is one. It locks RETRY unless the run's result
    // is 0 (a licence test without a medal).
    RecordFinish f;
    f.time = 61.0;
    CHECK(sets_record(f) && locks_retry(f)); // the session's first time
    f.session_best = 61.0;
    CHECK(locks_retry(f)); // a tie
    f.session_best = 61.0004;
    CHECK(locks_retry(f)); // the same whole ms
    f.session_best = 60.999;
    CHECK(!sets_record(f) && !locks_retry(f)); // slower than the session's best
    f.session_best = 62.0;
    RecordFinish g = f;
    g.player = false;
    CHECK(!locks_retry(g));
    g = f, g.replay = true;
    CHECK(!locks_retry(g));
    g = f, g.failed = true;
    CHECK(!locks_retry(g));
    g = f, g.time = -1; // no time (the top-speed test)
    CHECK(!locks_retry(g));
    // Licence tests: a record without a medal (slower than every target) does not lock.
    g = f, g.result = 0;
    CHECK(sets_record(g) && !locks_retry(g));
    for (int medal = 1; medal <= 3; ++medal) {
        g.result = medal;
        CHECK(locks_retry(g));
    }

    // The pause window: a greyed RETRY cannot be reached or chosen.
    PauseMenu m;
    m.set_retry_enabled(false);
    m.open(PauseMenu::Kind::Retry);
    m.tick(0);
    press(m, menu::flow::MenuKey::Right);
    CHECK(m.cursor() == 1);
    press(m, menu::flow::MenuKey::Right);
    CHECK(m.cursor() == 1 && !m.retry_enabled());
    m.set_retry_enabled(true); // the next run
    m.open(PauseMenu::Kind::Retry);
    m.tick(0);
    press(m, menu::flow::MenuKey::Right);
    press(m, menu::flow::MenuKey::Right);
    CHECK(m.cursor() == 2);
    CHECK(press(m, menu::flow::MenuKey::Confirm) == PauseMenu::Result::Retry);
}

TEST(race_speedometer_scale) {
    // N = clamp(int(v_top x 1.1 / 22.2222) + 1, 2, 4), v_top in m/s.
    Hud::SpeedScale k = Hud::speed_scale(100.0f, false); // 27.8 m/s: N 2
    CHECK(k.n == 2 && k.full == 160 && std::strcmp(k.face, "speed_180km") == 0 && k.step == 10);
    Hud::SpeedScale m = Hud::speed_scale(100.0f, true);
    CHECK(m.n == 2 && m.full == 100 && std::strcmp(m.face, "speed_120mp") == 0 && m.step == 5);
    k = Hud::speed_scale(150.0f, false); // 41.7 m/s: N 3
    m = Hud::speed_scale(150.0f, true);
    CHECK(k.n == 3 && k.full == 240 && std::strcmp(k.face, "speed_240km") == 0);
    CHECK(m.n == 3 && m.full == 150 && std::strcmp(m.face, "speed_160mp") == 0);
    k = Hud::speed_scale(400.0f, false); // N 6, clamped to 4
    m = Hud::speed_scale(400.0f, true);
    CHECK(k.n == 4 && k.full == 320 && std::strcmp(k.face, "speed_320km") == 0);
    CHECK(m.n == 4 && m.full == 200 && std::strcmp(m.face, "speed_200mp") == 0);
    CHECK(Hud::speed_scale(0.0f, false).n == 2); // at least 2
    // The step to N 3: v_top x 1.1 = 2 x 22.2222 m/s at 145.45 km/h.
    CHECK(Hud::speed_scale(146.0f, false).n == 3 && Hud::speed_scale(145.0f, false).n == 2);
}

TEST(race_hud_mode_select) {
    // presentation §D4: each run's layout, the endurance one for 10 laps or more or a time
    // limit (not in a free practice or 2P), the replay / demonstration layouts.
    HudModeSelect s;
    CHECK(hud_mode(s) == HudMode::Race);
    s.event_laps = 9;
    CHECK(hud_mode(s) == HudMode::Race);
    s.event_laps = 10;
    CHECK(hud_mode(s) == HudMode::Endurance);
    s.event_laps = 0, s.time_limit_min = 120;
    CHECK(hud_mode(s) == HudMode::Endurance);
    s.run = HudRun::FreePractice;
    CHECK(hud_mode(s) == HudMode::FreePractice);
    s.run = HudRun::Battle;
    CHECK(hud_mode(s) == HudMode::BattleWide);
    s.wide = false;
    CHECK(hud_mode(s) == HudMode::Battle);
    s.time_limit_min = 0;
    const std::pair<HudRun, HudMode> solo[] = {{HudRun::TimeTrial, HudMode::TimeTrial},
                                               {HudRun::FreeRun, HudMode::FreeRun},
                                               {HudRun::License, HudMode::License},
                                               {HudRun::MachineTime, HudMode::MachineTime},
                                               {HudRun::MachineSpeed, HudMode::MachineSpeed}};
    for (const auto& [run, mode] : solo) {
        s.run = run;
        CHECK(hud_mode(s) == mode);
        s.play = HudPlay::Replay;
        CHECK(hud_mode(s) == HudMode::Replay);
        s.play = HudPlay::Demo;
        CHECK(hud_mode(s) == HudMode::Demo);
        s.play = HudPlay::Live;
    }
    // 2P: its own layout in a demonstration and in a replay drawn as both halves.
    s.run = HudRun::Battle;
    s.play = HudPlay::Replay;
    CHECK(hud_mode(s) == HudMode::Replay);
    s.battle_split_replay = true;
    CHECK(hud_mode(s) == HudMode::Battle);
    s.play = HudPlay::Demo, s.battle_split_replay = false;
    CHECK(hud_mode(s) == HudMode::Battle);
    // The lap-time list: the run's laps when 1..3, else 3.
    CHECK(lap_list_lines(0) == 3 && lap_list_lines(1) == 1 && lap_list_lines(2) == 2 && lap_list_lines(3) == 3 &&
          lap_list_lines(12) == 3);
}

TEST(race_hud_layout_rows) {
    // presentation §D4-§D9: what each layout shows.
    auto has = [](HudMode m, HudPart p, int sub = -1, int view = 1) { return hud_row(m, p, sub, view) != nullptr; };
    auto line = [](HudMode m, TimeLine t) { return hud_row(m, HudPart::TimeLine, int(t), 1); };
    // Races: Best, Last, Lap under the lap count; position, map at (16, 80).
    CHECK(line(HudMode::Race, TimeLine::Best)->y == 84 && line(HudMode::Race, TimeLine::Last)->y == 112 &&
          line(HudMode::Race, TimeLine::Lap)->y == 140);
    CHECK(hud_row(HudMode::Race, HudPart::Map, -1, 1)->y == 80 && has(HudMode::Race, HudPart::Rank));
    // Endurance: Total, Best, the lap-time list from y 140, the current lap at 216.
    CHECK(line(HudMode::Endurance, TimeLine::Total)->y == 84 && line(HudMode::Endurance, TimeLine::LapTimes)->y == 140 &&
          line(HudMode::Endurance, TimeLine::Lap)->y == 216 && !line(HudMode::Endurance, TimeLine::Last));
    CHECK(hud_row(HudMode::Endurance, HudPart::Map, -1, 1)->y == 96 && hud_row(HudMode::Endurance, HudPart::Rank, -1, 1)->y == 60);
    // Free runs: Record Lap, Best, the list, Lap; the map at the top; no position.
    CHECK(line(HudMode::FreeRun, TimeLine::RecordLap)->y == 84 && line(HudMode::FreeRun, TimeLine::LapTimes));
    CHECK(hud_row(HudMode::FreeRun, HudPart::Map, -1, 1)->y == 16 && !has(HudMode::FreeRun, HudPart::Rank));
    // The arcade time trial: Best and Time (the run's total).
    CHECK(line(HudMode::TimeTrial, TimeLine::Best)->y == 84 && line(HudMode::TimeTrial, TimeLine::TotalTime)->y == 112);
    // Free practice: P.P. with the car picture's offset.
    const HudRow* pp = line(HudMode::FreePractice, TimeLine::PolePosition);
    CHECK(pp && pp->w == -99 && pp->h == -4);
    // Licence tests: no lap count, no message display (no WRONG WAY), the three targets.
    CHECK(!has(HudMode::License, HudPart::LapCount) && !has(HudMode::License, HudPart::Messages) &&
          hud_row(HudMode::License, HudPart::Target, 2, 1)->y == 196);
    // The top-speed test: no event or message display, the speed lines.
    CHECK(!has(HudMode::MachineSpeed, HudPart::Events) && !has(HudMode::MachineSpeed, HudPart::Messages) &&
          line(HudMode::MachineSpeed, TimeLine::BestSpeed));
    // Per view: the in-car and outside clusters; in replays the outside view's small cluster.
    CHECK(has(HudMode::Race, HudPart::Meters, 0, 0) && !has(HudMode::Race, HudPart::Meters, 0, 1) &&
          has(HudMode::Race, HudPart::Meters, 1, 1));
    CHECK(has(HudMode::Replay, HudPart::Meters, 0, 0) && !has(HudMode::Replay, HudPart::Meters, 1, 1) &&
          has(HudMode::Replay, HudPart::Meters, 64, 1) && has(HudMode::Replay, HudPart::DigitalSpeed, -1, 1) &&
          !has(HudMode::Replay, HudPart::DigitalSpeed, -1, 0));
    CHECK(hud_row(HudMode::Replay, HudPart::CarName, -1, 0)->y == 290 && hud_row(HudMode::Replay, HudPart::CarName, -1, 1)->y == 433);
    // Labels.
    CHECK(std::string(time_line_label(TimeLine::RecordLap)) == "Record Lap" &&
          std::string(time_line_label(TimeLine::TotalTime)) == "Time" &&
          std::string(time_line_label(TimeLine::PolePosition)) == "P.P." && *time_line_label(TimeLine::LapTimes) == 0);
}

TEST(race_replay_event_display_rows) {
    // presentation §D6, §D9: the event display is drawn with the time item only. Live play has
    // every item but steering and accel / brake; replays the REPLAY OPTIONS ITEM bits.
    for (HudMode m : {HudMode::Race, HudMode::Endurance, HudMode::License, HudMode::MachineTime, HudMode::FreeRun,
                      HudMode::FreePractice, HudMode::TimeTrial})
        CHECK(Hud::event_row(m, kLiveItems, 0) && Hud::event_row(m, kLiveItems, 1));
    CHECK(!Hud::event_row(HudMode::MachineSpeed, kLiveItems, 1)); // its layout has none
    // The replay layout: (320, 130), centred, squeezed to 400, in every camera view.
    for (int view : {0, 1}) {
        const HudRow* r = Hud::event_row(HudMode::Replay, kItemTimes, view);
        CHECK(r && r->part == HudPart::Events && r->x == 320 && r->y == 130 && r->w == 400 && r->align == 2);
        CHECK(!Hud::event_row(HudMode::Replay, 0, view)); // the default: no items
        CHECK(!Hud::event_row(HudMode::Replay, u8(0xFF & ~kItemTimes), view));
    }
    // The demonstration layout: in the in-car view only.
    CHECK(Hud::event_row(HudMode::Demo, kItemTimes, 0) && !Hud::event_row(HudMode::Demo, kItemTimes, 1));
    // The second event display has rows (the replay's outside views at (615, 400), right-aligned;
    // every demonstration view) but nothing is ever posted to it: never the event display's row.
    const HudRow* e2 = hud_row(HudMode::Replay, HudPart::Events2, -1, 1);
    CHECK(e2 && e2->x == 615 && e2->y == 400 && e2->w == 320 && e2->align == 5 &&
          !hud_row(HudMode::Replay, HudPart::Events2, -1, 0) && hud_row(HudMode::Demo, HudPart::Events2, -1, 1));
    CHECK(Hud::event_row(HudMode::Replay, kItemTimes, 1)->part == HudPart::Events);
}

TEST(race_attract_view_pick) {
    int since = 0; // start-up
    AttractView v = pick_attract_view(0.1f, since);
    CHECK(v.mode == 0 && v.target == 0);
    v = pick_attract_view(0.34f, since); // mode 2, int(34) mod 3 = 1: TOP
    CHECK(v.mode == 2 && v.target == 1);
    v = pick_attract_view(0.36f, since); // int(36) mod 3 = 0: MANUAL
    CHECK(v.mode == 2 && v.target == 0);
    // Three demos after start-up without mode 1: the fourth is forced to mode 1 (AUTO).
    v = pick_attract_view(0.1f, since);
    CHECK(v.mode == 1 && v.target == 2);
    // After a mode-1 demo: two without it, then forced.
    v = pick_attract_view(0.5f, since); // int(50) mod 3 = 2: AUTO
    CHECK(v.mode == 2 && v.target == 2);
    v = pick_attract_view(0.2f, since);
    CHECK(v.mode == 0);
    v = pick_attract_view(0.2f, since);
    CHECK(v.mode == 1 && v.target == 2);
    // A rolled mode 1 resets the count as well.
    v = pick_attract_view(0.9f, since);
    CHECK(v.mode == 1);
    v = pick_attract_view(0.1f, since);
    CHECK(v.mode == 0);
    v = pick_attract_view(0.1f, since);
    CHECK(v.mode == 0);
    v = pick_attract_view(0.1f, since);
    CHECK(v.mode == 1);
}

TEST(race_replay_broadcast_onboard_cut) {
    // Twenty 50 m records over the lap; the car drives 40 laps, a new shot at every record.
    std::vector<GtrcRecord> recs;
    for (int i = 0; i < 20; ++i) recs.push_back(trackside(1000.0f + 50.0f * float(i), 1050.0f + 50.0f * float(i), float(i) * 50, 30));
    GtrcScript g = script_of(recs);
    auto run = [&](int mode, u32 seed, int* onboard_shots, bool pair = true) {
        ReplayDirector d(&g);
        d.set_view_mode(mode);
        d.seed(seed);
        d.set_pair(pair);
        int last = d.cut_serial(), shots = 0;
        *onboard_shots = 0;
        for (int lap = 1; lap <= 40; ++lap)
            for (float s = 0; s < 1000; s += 5) {
                ReplayDirector::Shot shot = d.update(s, lap, {s, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
                if (d.cut_serial() != last) {
                    last = d.cut_serial();
                    ++shots;
                    if (shot.onboard) ++*onboard_shots;
                }
            }
        return shots;
    };
    int onboard = 0;
    const int shots = run(2, 12345u, &onboard);
    CHECK(shots == 800);
    // One draw per new shot from the director's generator (a step, then a unit draw): p 0.07.
    GameRandom r;
    r.x = 12345u;
    int expect = 0;
    for (int i = 0; i < shots; ++i) {
        r.next();
        expect += r.rand01() < 0.07f;
    }
    CHECK(onboard == expect && expect > 30 && expect < 90);
    // Traditional and course line never cut on board; broadcast only while the followed car
    // has a battle partner.
    CHECK(run(0, 12345u, &onboard) == 800 && onboard == 0);
    CHECK(run(3, 12345u, &onboard) == 800 && onboard == 0);
    CHECK(run(2, 12345u, &onboard, false) == 800 && onboard == 0);

    // The cut turns the camera on board the partner's car until the shot ends (TOP / AUTO wait
    // its first 3 s); a new target ends it at once.
    GtrcScript one = script_of({trackside(1000, 1900, 0, 30)});
    u32 seed = 1;
    for (;; ++seed) { // a seed whose first draw cuts on board
        GameRandom q;
        q.x = seed;
        q.next();
        if (q.rand01() < 0.07f) break;
    }
    ReplayDirector d(&one);
    d.set_view_mode(2);
    d.set_pair(true);
    d.seed(seed);
    ReplayDirector::Shot shot = d.update(10, 1, {10, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(shot.onboard && shot.on_partner && d.onboard_cut() && d.camera_fresh() && !d.settling());
    for (int i = 1; i < 300; ++i) shot = d.update(10.0f + float(i) * 0.1f, 1, {10, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(shot.onboard && shot.on_partner && !d.onboard_cut() && !d.camera_fresh());
    d.target_set();
    shot = d.update(40, 1, {40, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(!shot.onboard && !d.on_board() && shot.record == 0);
    // Past its end with no record and no other lap variant: on board, the followed car's in-car view.
    d.update(950, 1, {950, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    shot = d.update(951, 1, {951, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(d.on_board() && !shot.on_partner && shot.procedural == 4 && d.procedural_shot().mount == 0);
}

// The lap counter and the lap variants; the records the game finds at a distance (presentation §H6).
TEST(race_replay_lap_variants) {
    CHECK(lap_counter(false, 0) == 0 && lap_counter(true, 0) == 1 && lap_counter(true, 3) == 4);
    // L 1000, two variants (script end 3100, first record at 900).
    GtrcScript g;
    g.lap_length = 1000, g.script_end = 3100;
    g.records = {trackside(900, 1100, 0, 0), trackside(1100, 2000, 0, 0), trackside(2000, 2100, 0, 0),
                 trackside(2050, 2100, 0, 0), trackside(2100, 3000, 0, 0), trackside(3000, 3100, 0, 0)};
    CHECK(g.variants() == 2 && near(g.period(), 2000));
    CHECK(g.lap_variant(0) == 0 && g.lap_variant(1) == 1 && g.lap_variant(2) == 2 && g.lap_variant(3) == 1);
    // Before the start line (k 0) D = s: the first record covers the start.
    CHECK(g.records_at(950) == std::vector<int>{0} && g.records_at(899).empty());
    // Only the last record starting at or before D: 2 also contains 2060 but is not found.
    CHECK(g.records_at(2060) == std::vector<int>{3});
    // Past the end of the list: on with D - period from the first record.
    CHECK((g.records_at(3050) == std::vector<int>{5, 0}));
    CHECK(g.record_at(1500, 0.6f) == 1 && g.record_at(1700, 0.6f) == -1 && g.record_at(1700, 1.0f) == 1);
    CHECK(g.record_at(3050, 1.0f) == 5); // (record 0's u uses D, not D - period)
    // Another variant: from k + 1 (past nvar: 1), u in [0, 0.6].
    auto v = g.other_variant(1, 50); // variant 2: D 2050 -> record 3 (2 is never found there)
    CHECK(v && v->first == 3 && v->second == 2);
    v = g.other_variant(0, 950); // variant 1: D 1950, u 0.94 in record 1; variant 2: D 2950, u 0.94
    CHECK(!v);
    v = g.other_variant(0, 200); // variant 1: D 1200, record 1 at u 0.11
    CHECK(v && v->first == 1 && v->second == 1);
}

// A replay's or demonstration's first shot (presentation §H8): the camera starts on the
// script's last record, the view's reset re-arms it, and as the car is outside it the record
// at D whatever its progress follows; before its first crossing of the start line a car's
// D = s (lap variant 0): the script's first records, which cover the start.
TEST(race_replay_start_shot) {
    GtrcScript g;
    g.lap_length = 1000, g.script_end = 3100;
    g.records = {trackside(900, 1100, 1, 0), trackside(1100, 1500, 2, 0), trackside(1500, 1700, 3, 0),
                 trackside(1700, 1900, 4, 0), trackside(1900, 2100, 5, 0), trackside(2100, 3000, 6, 0),
                 trackside(3000, 3100, 7, 0)};
    for (int mode : {0, 2, 3}) {
        ReplayDirector d(&g);
        d.set_view_mode(mode);
        d.seed(1u);
        // A flying start 50 m before the line (inside the start record at u 0.25, not variant
        // 1's record at the lap's end).
        ReplayDirector::Shot shot = d.update(950, 0, {950, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
        CHECK(shot.valid && shot.record == 0 && near(shot.eye.x, 1) && d.take_dissolve() == 0);
        // Across the line (lap 1, k 1): D = 1000 + s goes on in the same record, then the next.
        shot = d.update(60, 1, {60, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
        CHECK(shot.record == 0);
        d.update(101, 1, {101, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
        shot = d.update(102, 1, {102, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
        CHECK(shot.record == 1);
    }
    // Further back (200 m, before the start record): the first frame on board (the in-car
    // view), then variant 1's record at the lap's end (u 0.5), held 3 s and while it lasts;
    // then the start record, with the outgoing shot's dissolve (1 s on board: none here).
    ReplayDirector d(&g);
    ReplayDirector::Shot shot = d.update(800, 0, {800, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(d.on_board() && shot.procedural == 4 && d.procedural_shot().mount == 0);
    shot = d.update(801, 0, {801, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(!d.on_board() && shot.record == 3 && near(shot.eye.x, 4) && near(d.take_dissolve(), 1.0f));
    int frames = 1;
    float s = 802;
    for (; shot.record == 3 && s < 1000; s += 0.5f, ++frames) shot = d.update(s, 0, {s, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(frames >= 181 && s > 900.5f); // run out at D 1900 (s 900), held 180 frames
    CHECK(shot.record == 0 && d.take_dissolve() == 0);
    // A REPLAY OPTIONS apply re-arms the shot (it goes on: D inside it).
    d.reset();
    shot = d.update(s, 0, {s, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(shot.record == 0 && near(shot.seconds, 0));
}

// The gate (presentation §H8): modes 0 / 3 cut as soon as the shot runs out; mode 2 once the
// shot length x is 1.3 s or more or a cut request is pending (0x400 records in their last tenth
// too); a variant's shot holds 3 s.
TEST(race_replay_shot_gate) {
    CHECK(ReplayDirector::shot_state(0, true, false, false, 1, false) == 2);
    CHECK(ReplayDirector::shot_state(3, false, true, false, 0.95f, false) == 0);
    CHECK(ReplayDirector::shot_state(2, true, false, false, 1, false) == 1);
    CHECK(ReplayDirector::shot_state(2, false, false, false, 0.95f, false) == 0);
    CHECK(ReplayDirector::shot_state(2, false, true, false, 0.95f, false) == 1);
    CHECK(ReplayDirector::shot_state(2, false, true, false, 0.05f, true) == 1);
    CHECK(ReplayDirector::shot_state(2, false, true, false, 0.5f, false) == 0);
    CHECK(ReplayDirector::shot_state(2, true, true, false, 1, false) == 2);
    CHECK(ReplayDirector::shot_state(2, false, true, true, 0.95f, false) == 0); // on board: as plain records
    CHECK(ReplayDirector::gate_open(2, 0, false) && !ReplayDirector::gate_open(0, 9, true));
    CHECK(!ReplayDirector::gate_open(1, 1.2f, false) && ReplayDirector::gate_open(1, 1.3f, false));
    CHECK(ReplayDirector::gate_open(1, 0.5f, true));
    // Mode 2 holds a run-out shot until the gate opens.
    GtrcScript g = script_of({trackside(1000, 1100, 0, 0), trackside(1100, 1300, 1, 0)});
    ReplayDirector d(&g);
    d.set_view_mode(2);
    d.set_cut_gate(0.5f, false);
    ReplayDirector::Shot shot = d.update(50, 1, {50, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(shot.record == 0);
    for (int i = 0; i < 3; ++i) shot = d.update(105, 1, {105, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(shot.record == 0); // run out, x 0.5 s
    d.set_cut_gate(0.5f, true);
    shot = d.update(106, 1, {106, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(shot.record == 1); // a request
}

// The camera's view widened to keep a pair in view: 1.1 x the cars' angle apart plus the angle
// of 5 m at the farther car, at most 70 degrees; a nearer partner widens to the limit.
TEST(race_replay_pair_zoom) {
    const float deg = 3.14159265f / 180.0f;
    ReplayDirector::Pair p{{-5, 0, 20}, {5, 0, 20}};
    const float apart = 2.0f * std::atan(5.0f / 20.0f);
    const float need = 1.1f * apart + std::atan2(5.0f, std::sqrt(425.0f));
    CHECK(near(ReplayDirector::pair_angle({0, 0, 0}, p, 10 * deg), need));
    CHECK(near(ReplayDirector::pair_angle({0, 0, 0}, p, 60 * deg), 60 * deg)); // already wide enough
    ReplayDirector::Pair wide{{-30, 0, 20}, {30, 0, 20}};
    CHECK(near(ReplayDirector::pair_angle({0, 0, 0}, wide, 10 * deg), 70 * deg));
    ReplayDirector::Pair nearer{{0, 0, 40}, {1, 0, 30}}; // the partner 10 m nearer: x = 1 m
    CHECK(near(ReplayDirector::pair_angle({0, 0, 0}, nearer, 10 * deg), 70 * deg));
    // A record's shot aimed at the pair widens its zoom (within the record's limits).
    GtrcRecord r = trackside(1000, 1900, 0, 0);
    r.kind = 4; // looks at the car
    r.zoom = constant(8.0f);
    r.zoom_min = 0.2f, r.zoom_max = 20.0f;
    GtrcScript g = script_of({r});
    ReplayDirector a(&g), b(&g);
    ReplayDirector::Pair cars{{-5, 0, 20}, {5, 0, 20}};
    const float alone = a.update(10, 1, {0, 0, 20}, 1.0 / 60, 16.0f / 9.0f).fovy;
    const float both = b.update(10, 1, {0, 0, 20}, 1.0 / 60, 16.0f / 9.0f, &cars).fovy;
    CHECK(both > alone * 2.0f);
}

namespace {

std::vector<ReplayFocus::Car> focus_field(std::vector<std::pair<float, int>> x_rank) {
    std::vector<ReplayFocus::Car> cars;
    for (auto [x, rank] : x_rank) cars.push_back({{x, 0, 0}, rank});
    return cars;
}

} // namespace

// MANUAL Up / Down: the next / previous car, wrapping (the player's car is car 0).
TEST(replay_focus_manual) {
    CHECK(ReplayFocus::next_car(0, 1, 4) == 1 && ReplayFocus::next_car(3, 1, 4) == 0);
    CHECK(ReplayFocus::next_car(0, -1, 4) == 3 && ReplayFocus::next_car(2, -1, 4) == 1);
    CHECK(ReplayFocus::next_car(0, 1, 1) == 0);
    // MANUAL scores keep the followed car: nothing is ever pending.
    ReplayFocus f;
    f.begin();
    std::vector<ReplayFocus::Car> cars = focus_field({{0, 1}, {500, 0}, {1000, 2}});
    f.reset_interest(0, ReplayFocus::Manual, cars, 99u);
    for (int i = 0; i < 2000; ++i) f.step(cars, 0, ReplayFocus::Manual, true, true);
    CHECK(f.pending() == -1 && f.score(0) == 1200 && f.score(1) == 0);
}

// TOP: the leader, never in the first 240 frames after the target was set or a switch, then
// at a 30-frame check with a chance growing to 1 at 1800 frames.
TEST(replay_focus_top) {
    std::vector<ReplayFocus::Car> cars = focus_field({{0, 1}, {500, 0}, {1000, 2}});
    for (u32 seed : {1u, 77u, 4242u}) {
        ReplayFocus f;
        f.begin();
        f.reset_interest(0, ReplayFocus::Top, cars, seed);
        int at = -1;
        for (int i = 1; i <= 2000 && at < 0; ++i) {
            f.step(cars, 0, ReplayFocus::Top, true, true);
            if (f.pending() >= 0) at = i;
        }
        CHECK(at > 240 && at <= 1800 && at % 30 == 0);
        CHECK(f.pending() == 1 && f.score(1) == 1200);
        // The check's draw: the generator's next value under (frames - 240) / 1560.
        GameRandom q;
        q.x = seed;
        int expect = -1;
        for (int i = 30; i <= 1800 && expect < 0; i += 30)
            if (q.rand01() < std::max(float(i - 240) / 1560.0f, 0.0f)) expect = i;
        CHECK(at == expect);
        CHECK(f.take_pending(3) == 1 && f.pending() == -1 && f.frames() == 0);
        f.step(cars, 1, ReplayFocus::Top, true, true);
        CHECK(f.pending() == -1);
    }
}

// AUTO: scores from the rank's base, -1/30 a frame for the followed car, +10 at random by rank,
// +150 / -100 for a place gained / lost.
TEST(replay_focus_auto_scores) {
    std::vector<ReplayFocus::Car> cars = focus_field({{0, 0}, {500, 1}, {1000, 2}});
    ReplayFocus f;
    f.begin();
    f.reset_interest(0, ReplayFocus::Auto, cars, 5u);
    CHECK(f.score(0) == 400 && f.score(1) == 300 && f.score(2) == 200);
    GameRandom q;
    q.x = 5u;
    float s1 = 300, s2 = 200;
    for (int i = 0; i < 100; ++i) {
        f.step(cars, 0, ReplayFocus::Auto, true, true);
        q.next();
        if (q.rand01() < 0.01f) s1 += 10;
        q.next();
        if (q.rand01() < 1.0f / 150) s2 += 10;
        if ((i + 1) % 30 == 0) q.rand01(); // the re-pick check (its chance is 0 yet)
    }
    CHECK(near(f.score(0), 400.0f - 100.0f / 30.0f, 1e-2f) && f.score(1) == s1 && f.score(2) == s2);
    // Car 2 passes car 1: +150 / -100.
    std::vector<ReplayFocus::Car> passed = focus_field({{0, 0}, {500, 2}, {1000, 1}});
    f.step(passed, 0, ReplayFocus::Auto, true, true);
    q.next();
    if (q.rand01() < 1.0f / 150) s1 += 10; // car 1 now third
    q.next();
    if (q.rand01() < 0.01f) s2 += 10;
    CHECK(f.score(1) == s1 - 100 && f.score(2) == s2 + 150 && f.bonus(1) == 0);
}

// Battles: a car stands 240 frames (after the 40 every car is held at the start), then takes
// the car within 10 m as its candidate; 121 frames together make state 3 (the partner counts
// from then on); 30 m apart ends it. A held car's state stands still; an unsettled shot drops
// a candidate; nothing moves before the start.
TEST(replay_focus_battles) {
    ReplayFocus f;
    f.begin();
    std::vector<ReplayFocus::Car> cars = focus_field({{0, 0}, {5, 1}});
    f.reset_interest(0, ReplayFocus::Auto, cars, 3u);
    int frame = 0;
    auto run = [&](int to) {
        for (; frame < to; ++frame) f.step(cars, 0, ReplayFocus::Auto, true, true);
    };
    run(280);
    CHECK(f.battle(0) == ReplayFocus::None && f.hold(0) == 0);
    run(281);
    CHECK(f.battle(0) == ReplayFocus::Candidate && f.partner(0) == -1 && !f.battle_changed(0));
    CHECK(f.bonus(0) == 100.0f * 1.5f && f.bonus(1) == 100.0f * 0.95f);
    run(401);
    CHECK(f.battle(0) == ReplayFocus::Candidate);
    run(402);
    CHECK(f.battle(0) == ReplayFocus::After && f.partner(0) == 1 && f.battle_changed(0));
    CHECK(f.bonus(0) == 200.0f * 1.5f);
    cars[1].pos.x = 40; // 40 m apart: over
    run(403);
    CHECK(f.battle(0) == ReplayFocus::None && f.partner(0) == -1 && f.battle_changed(0));

    ReplayFocus g;
    g.begin();
    cars[1].pos.x = 5;
    for (int i = 0; i < 281; ++i) g.step(cars, 0, ReplayFocus::Auto, true, true);
    CHECK(g.battle(0) == ReplayFocus::Candidate);
    g.set_hold(0, 10);
    for (int i = 0; i < 10; ++i) g.step(cars, 0, ReplayFocus::Auto, true, false);
    CHECK(g.battle(0) == ReplayFocus::Candidate);
    g.step(cars, 0, ReplayFocus::Auto, true, false);
    CHECK(g.battle(0) == ReplayFocus::None && g.battle_changed(0));

    ReplayFocus h;
    h.begin();
    for (int i = 0; i < 400; ++i) h.step(cars, 0, ReplayFocus::Auto, false, true);
    CHECK(h.battle(0) == ReplayFocus::None && h.hold(0) == ReplayFocus::kStartHold);
}

// The switch: the new car's score + 200 (clamped), the others x 0.98 / 0.84 / 0.95 by its
// battle state (none: x 1), the left car back to its rank's base.
TEST(replay_focus_switch) {
    std::vector<ReplayFocus::Car> cars = focus_field({{0, 1}, {500, 0}, {1000, 2}});
    ReplayFocus f;
    f.begin();
    f.reset_interest(0, ReplayFocus::Top, cars, 8u);
    for (int i = 0; i < 2000 && f.pending() < 0; ++i) f.step(cars, 0, ReplayFocus::Top, true, true);
    CHECK(f.pending() == 1 && f.take_pending(3) == 1);
    CHECK(f.score(1) == 1200 && f.score(0) == 300 && f.score(2) == 0);
}

TEST(race_replay_record_timing) {
    // A record aiming at its target curves with a 4 s duration plays over that time: the car
    // standing still inside its range, the camera still moves.
    GtrcRecord timed = trackside(1000, 1500, 0, 0);
    timed.duration = 4.0f;
    timed.pos[0] = ramp(0, 100);
    GtrcScript g = script_of({timed});
    ReplayDirector d(&g);
    ReplayDirector::Shot shot;
    for (int i = 0; i <= 120; ++i) shot = d.update(100, 1, {100, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(near(shot.eye.x, 50.0f, 0.01f) && near(shot.seconds, 2.0f, 1e-3f)); // 2 s of 4
    for (int i = 0; i < 119; ++i) shot = d.update(100, 1, {100, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(near(shot.eye.x, 100.0f, 0.5f) && !d.on_board()); // at the end
    // Run out with the car still inside it (no other record, no other lap variant): the camera
    // turns on board, the followed car's in-car view (its mount 0).
    for (int i = 0; i < 3; ++i) shot = d.update(100, 1, {100, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(d.on_board() && shot.procedural == 4 && d.procedural_shot().mount == 0);
    CHECK(near(ReplayDirector::shot_t(timed, 0.2f, 1.0), 0.25f));
    // Under 1 s, or looking at the car: by the car's distance along the range.
    GtrcRecord short_rec = timed;
    short_rec.duration = 0.5f;
    CHECK(near(ReplayDirector::shot_t(short_rec, 0.2f, 1.0), 0.2f));
    GtrcRecord at_car = timed;
    at_car.kind = 4;
    CHECK(near(ReplayDirector::shot_t(at_car, 0.2f, 1.0), 0.2f) && near(ReplayDirector::shot_t(at_car, 1.5f, 0), 1.0f));
    GtrcScript g2 = script_of({at_car});
    ReplayDirector d2(&g2);
    for (int i = 0; i < 120; ++i) shot = d2.update(100, 1, {100, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(near(shot.eye.x, 20.0f, 0.01f)); // u = 100 / 500
}

TEST(race_replay_avant_garde) {
    // Procedural cameras by the shot length (the hold: always the car mount).
    CHECK(ReplayDirector::procedural_kind(0.0f, 2.0f) == 1 && ReplayDirector::procedural_kind(0.29f, 2.0f) == 1);
    CHECK(ReplayDirector::procedural_kind(0.3f, 2.0f) == 2 && ReplayDirector::procedural_kind(0.69f, 2.0f) == 3);
    CHECK(ReplayDirector::procedural_kind(0.7f, 2.0f) == 4 && ReplayDirector::procedural_kind(0.999f, 2.0f) == 4);
    CHECK(ReplayDirector::procedural_kind(0.34f, 3.0f) == 0 && ReplayDirector::procedural_kind(0.35f, 3.0f) == 1);
    CHECK(ReplayDirector::procedural_kind(0.65f, 3.0f) == 2 && ReplayDirector::procedural_kind(0.8f, 3.0f) == 3);
    CHECK(ReplayDirector::procedural_kind(0.9f, 3.0f) == 4);
    CHECK(ReplayDirector::procedural_kind(0.0f, 5.0f, true) == 4 && ReplayDirector::procedural_kind(0.5f, 1.0f, true) == 4);

    // Course shots for a car: u in [0, 0.8], x s left at the car's speed (3 m/s at least),
    // the camera 15 m or more from the last course shot's.
    GtrcRecord a = trackside(1000, 1100, 0, 0);
    GtrcRecord b = trackside(1050, 1300, 10, 0);  // 10 m from a's camera
    GtrcRecord c = trackside(1050, 1400, 0, 20);  // 20 m from a's camera
    GtrcScript g = script_of({a, b, c});
    ReplayDirector d(&g);
    d.set_view_mode(1);
    ReplayDirector::FieldCar car{60, 0, 20}; // D 1060
    auto f = d.find_free_shot(car, 2.0f);
    CHECK(f && f->first == 0 && f->second == 1); // a: u 0.6, 40 m at 20 m/s = 2 s
    car.speed = 21; // 1.9 s left in a: b
    f = d.find_free_shot(car, 2.0f);
    CHECK(f && f->first == 1);
    car.s = 85; // a at u 0.85: not a; b
    f = d.find_free_shot(car, 2.0f);
    CHECK(f && f->first == 1);
    car = {60, 0, 0}; // standing: 3 m/s
    CHECK(d.find_free_shot(car, 2.0f)->first == 0);

    // The view's reset cuts on the first frame; then only a request (not in the hold) or the pad.
    d.seed(777u);
    CHECK(d.avant_garde_due(0, false, false));
    CHECK(!d.avant_garde_due(0, false, false));
    CHECK(d.avant_garde_due(3, false, false) && !d.avant_garde_due(3, true, false) && d.avant_garde_due(0, true, true));
    // A cut: the draws (two generator steps each) in the original's order.
    std::vector<ReplayDirector::FieldCar> field{{60, 0, 20, 20}, {60, 0, 20, 20}, {60, 0, 20, 20}};
    int serial = d.cut_serial();
    ReplayDirector::Cut cut = d.avant_garde_cut(field, 0, true, 2.0f, 3, false, 99u);
    CHECK(cut.car != 0 && d.cut_serial() == serial + 1);
    GameRandom r;
    r.x = 777u;
    auto draw = [&r] {
        r.next();
        return r.rand01();
    };
    int pick = int(draw() * 3.0f);
    if (pick == 0) ++pick;
    CHECK(cut.car == pick && cut.pair); // request 3: with the partner
    float roll = draw() * 20.0f - 10.0f;
    draw();                                  // the wide zoom
    if (draw() < 0.3f) r.rand01();           // the yaw seed
    if (draw() < 0.1f) draw();               // the roll swing
    const bool procedural = draw() < 0.25f;
    int kind = -1;
    if (procedural) {
        kind = ReplayDirector::procedural_kind(draw(), 2.0f);
        if (kind == 4) CHECK(d.procedural_shot().mount == std::clamp(int(draw() * 17.0f), 0, 16) + 3);
    }
    ReplayDirector::Shot shot = d.update(60, 0, {60, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    CHECK(shot.valid);
    if (!procedural) CHECK(shot.record == 0 && near(shot.roll, roll * 3.14159265f / 180.0f, 1e-3f));
    else CHECK(shot.procedural == kind && d.procedural_shot().seed == 99u && d.procedural_shot().x == 2.0f);
    // The look: levels, trail (the orbit camera: p 0.6), dissolve (request 3: p 0.1), filter.
    const float lv = draw();
    if (lv < 0.25f) draw(), draw();
    CHECK(d.look().levels == (lv < 0.25f));
    const float tr = draw();
    CHECK(d.look().trail == (tr - (kind == 2 ? 0.5f : 0.0f) < 0.1f));
    if (d.look().trail) draw(), draw();
    const float dv = draw();
    CHECK((cut.dissolve > 0) == (dv + 0.3f < 0.4f) && (cut.dissolve == 0 || cut.dissolve == 0.5f));
    // The next course shot avoids the last one (a, also when a procedural camera replaced it)
    // and any camera within 15 m of its camera (b).
    std::vector<ReplayDirector::FieldCar> one{{60, 0, 20}};
    std::optional<std::pair<int, int>> next = d.find_free_shot(one[0], 2.0f);
    CHECK(next && next->first == 2);
    cut = d.avant_garde_cut(one, 0, true, 2.0f, 1, false, 1u);
    // A single car: no other to pick; request 1 (the countdown) with a course shot for the
    // first car tried: the car alone.
    CHECK(cut.car == 0 && !cut.pair);
}

TEST(race_cut_clock) {
    // Without a marked song (presentation §G14): set-up request 1 and x 4; the countdown from 60
    // gives request 1 61 frames on, then every 121 frames; x = countdown / 60.
    CutClock c;
    GameRandom rng;
    c.setup(0.1f, 0.0f);
    CHECK(c.request() == 1 && near(c.shot_length(), 4.0f));
    CHECK(near(c.update_shot_length(nullptr, 1), 1.0f)); // 60 / 60
    std::vector<int> cuts;
    for (int f = 0; f < 400; ++f) {
        c.update_shot_length(nullptr, 1);
        c.update_request(nullptr, 1, false, rng);
        if (c.request() == 1) cuts.push_back(f);
    }
    CHECK(cuts.size() >= 3 && cuts[0] == 60 && cuts[1] == 181 && cuts[2] == 302);
    CHECK(near(c.update_shot_length(nullptr, 1), float(c.countdown()) / 60.0f));
    // The standing-start hold keeps the countdown at 120: no request.
    for (int f = 0; f < 300; ++f) {
        c.update_request(nullptr, 1, true, rng);
        CHECK(c.request() == 0);
    }
    CHECK(c.countdown() == 119); // (set to 120, then this frame's count)
    // The generator steps once a frame.
    GameRandom a, b;
    a.x = b.x = 5;
    c.update_request(nullptr, 0, false, a);
    b.next();
    CHECK(a.x == b.x);

    // A marked song: tracks 3 and 4 hold the cuts; with music-timed cuts off (mode 1) a track-3
    // event is request 3, on a track-4 event request 4; x = the interval to the next marker.
    sound::MadsMarkers song;
    song.rate = 100, song.length = 10000;
    song.tracks[1] = {50, 100, 150, 200};
    song.tracks[2] = {100, 300};
    song.tracks[3] = song.tracks[4] = {100, 250, 600};
    MusicMarkers m;
    m.start(&song);
    c.setup(0.1f, 0.0f);
    m.step(0.0);              // the song starts: every marked track fires
    c.update_shot_length(&m, 1);
    c.update_request(&m, 1, false, rng);
    CHECK(c.request() == 3);
    m.step(0.5); // nothing passed: no request, x = 1.0 - (0.5 + 1/30)
    CHECK(near(c.update_shot_length(&m, 1), 1.0f - 0.5f - 1.0f / 30.0f, 1e-4f));
    c.update_request(&m, 1, false, rng);
    CHECK(c.request() == 0);
    m.step(1.0); // 1.0 < 1.0 + 1/30: the first marker
    c.update_request(&m, 1, false, rng);
    CHECK(c.request() == 3);
    CHECK(c.step_music_cuts(+1) && c.music_cuts_on(1) && near(c.music_cuts(), 1.0f));
    CHECK(!c.step_music_cuts(+1));
    m.step(2.5); // 2.5 marker
    c.update_request(&m, 1, false, rng);
    CHECK(c.request() == 4);
    m.step(3.0);
    c.update_request(&m, 1, true, rng); // in the hold the request still comes (the handler drops it)
    CHECK(c.request() == 0);
    // Other view modes: track 3 gives 3, phrases (track 2) 2 once in use.
    m.step(5.99);
    c.update_request(&m, 0, false, rng);
    CHECK(c.request() == 3);
    // Up / Down +-0.1, Right / Left +-1.1, clamped; the level counts in marked sections only.
    c.reset(0.95f, 0.0f);
    c.step_level(+1);
    CHECK(near(c.level(), 1.0f) && near(c.sound_level(), 1.0f));
    c.step_level(-1);
    CHECK(near(c.level(), 0.9f));
    CHECK(!c.step_music_cuts(-1) && near(c.music_cuts(), 0.0f));
    CHECK(c.countdown() == 60);
}

TEST(race_music_markers) {
    sound::MadsMarkers song;
    song.rate = 44100, song.length = 441000; // 10 s
    song.tracks[3] = {1791, 162047, 322560};
    MusicMarkers m;
    CHECK(!m.marked() && m.interval(3) == MusicMarkers::kNone && !m.event(3));
    m.start(&song);
    m.step(0.0); // the start fires every marked track, the interval to the first marker
    CHECK(m.event(3) && !m.event(4) && m.interval(4) == MusicMarkers::kNone);
    CHECK(near(m.interval(3), 1791.0f / 44100.0f - 1.0f / 30.0f, 1e-5f));
    m.step(0.01); // 0.0406 < 0.01 + 1/30: the first marker passes (slightly negative interval)
    CHECK(m.event(3) && m.interval(3) < 0);
    m.step(0.02);
    CHECK(!m.event(3) && near(m.interval(3), 162047.0f / 44100.0f - 0.02f - 1.0f / 30.0f, 1e-4f));
    // One marker per frame at most: a jump past two fires one now, one next frame.
    m.step(8.0);
    CHECK(m.event(3));
    m.step(8.0);
    CHECK(m.event(3));
    // Past the last: the next is the first plus the song's length.
    m.step(8.1);
    CHECK(!m.event(3) && near(m.interval(3), 1791.0f / 44100.0f + 10.0f - 8.1f - 1.0f / 30.0f, 1e-4f));
    // A jingle (no markers): nothing.
    m.start(nullptr);
    m.step(1.0);
    CHECK(!m.marked() && !m.event(3) && m.interval(3) == MusicMarkers::kNone);
}

TEST(race_shot_look) {
    // The trail: the previous picture at min(timer, 0.8); fading timers lose 0.02 a frame.
    ShotLook l;
    CHECK(l.trail_weight() == 0);
    l.trail = true, l.trail_timer = 1.5f, l.trail_fade = true;
    CHECK(near(l.trail_weight(), 0.8f));
    for (int i = 0; i < 40; ++i) l.step();
    CHECK(near(l.trail_weight(), 0.7f, 1e-4f));
    for (int i = 0; i < 40; ++i) l.step();
    CHECK(l.trail_weight() == 0 && !l.trail_fade); // ran out: off
    l.trail_timer = 0.6f; // not fading: constant
    for (int i = 0; i < 100; ++i) l.step();
    CHECK(near(l.trail_weight(), 0.6f));
    // The rolls' odds over many cuts (the generator is not biased).
    GtrcScript g = script_of({trackside(1000, 1100, 0, 0)});
    ReplayDirector d(&g);
    d.seed(12345u);
    int levels = 0, trail = 0, dissolve = 0, filter = 0;
    const int n = 20000;
    for (int i = 0; i < n; ++i) {
        dissolve += d.roll_look() ? 1 : 0;
        levels += d.look().levels, trail += d.look().trail, filter += d.look().filter;
    }
    CHECK(std::abs(levels / float(n) - 0.25f) < 0.02f && std::abs(trail / float(n) - 0.1f) < 0.015f);
    CHECK(std::abs(dissolve / float(n) - 0.4f) < 0.02f && std::abs(filter / float(n) - 1.0f / 3.0f) < 0.04f);
    int low = 0;
    for (int i = 0; i < n; ++i) low += d.roll_look(0, -0.3f) ? 1 : 0; // request 3
    CHECK(std::abs(low / float(n) - 0.1f) < 0.015f);
}

TEST(race_procedural_cameras) {
    using PC = ProceduralCamera;
    // GT3's angles: yaw turns the view onto a direction, positive pitch looks down.
    CHECK(near(PC::yaw_of({0, 0, -1}), 0.0f, 1e-3f) && near(PC::yaw_of({1, 0, 0}), 90.0f, 1e-3f));
    CHECK(near(PC::pitch_of({0, -1, 1}), 45.0f, 1e-3f));
    const render::Vec3 dir = render::normalize({0.3f, -0.4f, 0.8f});
    const render::Vec3 v = PC::rotation(0, PC::pitch_of(dir), PC::yaw_of(dir)).transform_point(dir);
    CHECK(near(v.x, 0, 1e-4f) && near(v.y, 0, 1e-4f) && near(v.z, -1, 1e-4f));
    // The orbit's easing: through the quadrants' ends, fast at 0 / 180, slow at 90 / 270.
    CHECK(near(orbit_ease(0), 0) && near(orbit_ease(90), 90, 1e-3f) && near(orbit_ease(180), 180, 1e-3f));
    CHECK(near(orbit_ease(270), 270, 1e-3f) && near(orbit_ease(45), 53.196f, 1e-2f) && near(orbit_ease(135), 126.804f, 1e-2f));

    // A car at the origin heading -z (yaw 0) at 30 m/s on flat road (y 0).
    CameraCar car;
    car.pos = {0, 0.3f, 0}, car.velocity = {0, 0, -30};
    car.front = car.rear = 2.2f, car.half_width = 0.9f;
    car.body = render::translate(car.pos);
    const GroundHeight flat = [](float, float, float) { return std::optional<float>(0.0f); };
    const std::vector<CarMount> none;
    auto local = [&](const render::Vec3& e) { return e - car.pos; }; // (yaw 0: world axes)
    for (u32 seed = 1; seed < 200; ++seed) {
        PC c;
        // Hover: 1 .. 9 m out (+-1 of wobble), 3 .. 7 m up.
        c.start({1, 0, 3.0f, seed}, car, flat, none);
        for (int i = 0; i < 30; ++i) c.step(car);
        c.view(car, flat);
        const render::Vec3 a = local(c.eye());
        const float r = std::hypot(a.x, a.z);
        CHECK(r > -0.5f && r < 10.5f && a.y >= 3.0f - 1e-3f && a.y <= 7.0f + 1e-3f);
        CHECK(c.zoom() >= PC::zoom_of(85.0f) - 1e-3f && c.zoom() <= PC::zoom_of(55.0f) + 1e-3f);
        // Orbit: after its x seconds it holds its end, within 30 degrees of behind the car
        // (+z), 0.1 .. 1.1 m up, the outline + 0.2 .. 5.2 m out (the centre within 0.5 m).
        PC m;
        m.start({2, 0, 2.0f, seed}, car, flat, none);
        for (int i = 0; i < 130; ++i) m.step(car);
        m.view(car, flat);
        const render::Vec3 e = local(m.eye());
        CHECK(e.y >= 0.1f - 1e-3f && e.y <= 1.1f + 1e-3f);
        const float az = std::atan2(-e.x, e.z) * 180.0f / 3.14159265f; // 0 = straight behind
        CHECK(std::fabs(az) < 30.0f + 15.0f); // (+ the slid centre's angle)
        CHECK(std::hypot(e.x, e.z) > 2.4f - 0.8f && std::hypot(e.x, e.z) < 7.4f + 0.8f);
        // Telephoto: 2.5 .. 6.5 m past a bumper, 0.1 .. 1.3 m up, looking back at the car.
        PC z;
        z.start({3, 0, 2.0f, seed}, car, flat, none);
        z.step(car);
        const render::Mat4 zv = z.view(car, flat);
        const render::Vec3 ze = local(z.eye());
        CHECK(std::fabs(ze.z) >= 2.2f + 2.5f - 1e-3f && std::fabs(ze.z) <= 2.2f + 6.5f + 1e-3f);
        CHECK(ze.y >= 0.1f - 1e-3f && ze.y <= 1.3f + 1e-3f && std::fabs(ze.x) <= 1.5f + 1e-3f);
        const render::Vec3 at = zv.transform_point(car.pos); // the car ahead in the view (-z)
        CHECK(at.z < 0 && std::fabs(at.x) < -at.z * 0.5f);
        // Roadside: still, near the road (0.1 .. 1.1 m up) or 1 m above the car without road.
        PC s;
        s.start({0, 0, 3.0f, seed}, car, flat, none);
        const render::Mat4 v0 = s.view(car, flat);
        CameraCar moved = car;
        moved.pos = {0, 0.3f, -20};
        const render::Mat4 v1 = s.view(moved, flat);
        CHECK(v0.m == v1.m && s.eye().y >= 0.1f - 1e-3f && s.eye().y <= 1.1f + 1e-3f);
    }
    // No road anywhere: the Roadside falls back to 1 m above the car, along its motion.
    PC s;
    s.start({0, 0, 3.0f, 7u}, car, [](float, float, float) { return std::optional<float>(); }, none);
    s.view(car, nullptr);
    CHECK(near(s.eye().y, 1.3f) && near(s.eye().x, 0) && near(s.eye().z, 0));
    // A mount: the eye at -pos in the model frame, its angles and zoom.
    std::vector<CarMount> mounts(4);
    mounts[3].pos = {0, -0.15f, 1.73f}, mounts[3].zoom = 1.69f; // the front bumper
    PC b;
    b.start({4, 3, 2.0f, 1u}, car, flat, mounts);
    const render::Mat4 bv = b.view(car, flat);
    CHECK(near(b.eye().x, 0, 1e-4f) && near(b.eye().y, 0.45f, 1e-4f) && near(b.eye().z, -1.73f, 1e-4f));
    CHECK(near(b.zoom(), 1.69f) && near(bv.transform_point(car.pos + render::Vec3{0, 0.15f, -11.73f}).z, -10.0f, 1e-3f));
    // Flags bit 1: a view from outside (the car drawn); the bumper (0xD) is on board.
    mounts[3].flags = 0xD;
    b.start({4, 3, 2.0f, 1u}, car, flat, mounts);
    CHECK(b.on_board());
    mounts[3].flags = 0x3; // an outside follow point
    b.start({4, 3, 2.0f, 1u}, car, flat, mounts);
    CHECK(!b.on_board());
    PC h;
    h.start({1, 0, 2.0f, 1u}, car, flat, mounts);
    CHECK(!h.on_board());
}

TEST(race_dissolve_clock) {
    // 0.5 s: the frozen picture at (29/30)^3 on its first frame, 29 frames in all; the footage's
    // measured cut (presentation §G16): .92 .82 .75 .66 .60 .52 at frames 0..5, .35 at 8, .04 at 19.
    DissolveClock d;
    d.start(0.5f);
    CHECK(d.snapshot && near(d.x, 29.0f / 30.0f));
    d.step(); // the snapshot's own frame does not count
    CHECK(near(d.x, 29.0f / 30.0f));
    d.snapshot = false;
    const float measured[6] = {0.92f, 0.82f, 0.75f, 0.66f, 0.60f, 0.52f};
    for (int k = 0; k < 20; ++k) {
        if (k < 6) CHECK(std::fabs(d.weight() - measured[k]) < 0.035f);
        if (k == 8) CHECK(std::fabs(d.weight() - 0.35f) < 0.02f);
        if (k == 19) CHECK(std::fabs(d.weight() - 0.04f) < 0.01f);
        d.step();
    }
    for (int k = 20; k < 29; ++k) d.step();
    CHECK(d.x == 0 && d.weight() == 0);
    // 1 s (a manual car change): 59 frames.
    d.start(1.0f);
    d.snapshot = false;
    for (int k = 0; k < 57; ++k) d.step();
    CHECK(d.weight() == 0 && d.x > 0.01f); // (x^3 under 1/128 already)
    d.step(), d.step();
    CHECK(d.x < 1e-5f);
}

TEST(race_replay_pad_settings) {
    // View mode 1's Up / Down level and Right / Left music-timed cuts, stored with the options.
    IniDocument cfg;
    ReplaySettings rs;
    CHECK(near(rs.level, 0.1f) && rs.music_cuts == 0);
    rs.level = 0.4f, rs.music_cuts = 1;
    rs.save(cfg);
    CHECK(cfg.get("options", "replay_level") == std::string("0.4") && cfg.get("options", "replay_music_cuts") == std::string("on"));
    ReplaySettings back = ReplaySettings::load(cfg);
    CHECK(near(back.level, 0.4f) && back.music_cuts == 1.0f);
    cfg.set("options", "replay_level", "7");
    CHECK(near(ReplaySettings::load(cfg).level, 1.0f));
}

TEST(race_replay_record_dissolve) {
    // Modes 0 / 2 / 3: a cut when a shot runs out dissolves for the outgoing shot's record's
    // +0x64 seconds (at most 4), 1 s from an on-board record; none for 0, and none for the
    // replay's first shot (presentation §H8, §G16).
    GtrcRecord a = trackside(1000, 1100, 0, 0), b = trackside(1100, 1300, 0, 0), c = trackside(1300, 1500, 0, 0);
    a.dissolve = 0.5f;
    b.kind = 3; // on board
    c.dissolve = 2.0f;
    GtrcScript g = script_of({a, b, c});
    ReplayDirector d(&g);
    d.update(10, 1, {10, 0, 0}, 1.0 / 60, 16.0f / 9.0f); // D 1010: a, the first shot
    CHECK(d.take_dissolve() == 0);
    d.update(20, 1, {20, 0, 0}, 1.0 / 60, 16.0f / 9.0f); // still a
    CHECK(d.take_dissolve() == 0);
    d.update(110, 1, {110, 0, 0}, 1.0 / 60, 16.0f / 9.0f); // a runs out (the next frame cuts)
    CHECK(d.take_dissolve() == 0);
    ReplayDirector::Shot shot = d.update(112, 1, {112, 0, 0}, 1.0 / 60, 16.0f / 9.0f); // b, a's 0.5 s
    CHECK(shot.record == 1 && near(d.take_dissolve(), 0.5f) && d.take_dissolve() == 0);
    d.update(310, 1, {310, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
    shot = d.update(312, 1, {312, 0, 0}, 1.0 / 60, 16.0f / 9.0f); // c, from on board: 1 s
    CHECK(shot.record == 2 && near(d.take_dissolve(), 1.0f));
}

TEST(race_exit_wash) {
    // The race exit's delayed fade-out: nothing for 10 frames, then grey 0.1 at alpha 0.5 a frame.
    using menu::flow::ScreenTransition;
    CHECK(ScreenTransition::delay_fadeout_cover(0).a == 0 && ScreenTransition::delay_fadeout_cover(10).a == 0);
    SDL_FColor w = ScreenTransition::delay_fadeout_cover(11);
    CHECK(near(w.a, 0.5f) && near(w.r, 12.8f / 255.0f) && w.r == w.g && w.g == w.b);
    CHECK(near(ScreenTransition::delay_fadeout_cover(18).a, 1.0f - 1.0f / 256.0f));
}

TEST(race_time_trial_finish) {
    // presentation §B7: the arcade time trial is one timed lap; the car finishes on the tick it
    // completes it (the event's lap count is never read). Free runs (no laps) never finish.
    static_assert(kTimeTrialLaps == 1);
    CHECK(!solo_run_finished(0, kTimeTrialLaps) && solo_run_finished(1, kTimeTrialLaps));
    CHECK(!solo_run_finished(1, 2) && !solo_run_finished(5, 0));
    // The record overlay from 5 s after the finish; confirm ends the race from then on, or it
    // ends by itself 10 s later.
    CHECK(!TimeTrialOverlay::ends_race(4.99, true) && !TimeTrialOverlay::ends_race(5.0, true));
    CHECK(TimeTrialOverlay::ends_race(5.02, true) && !TimeTrialOverlay::ends_race(5.02, false));
    CHECK(!TimeTrialOverlay::ends_race(15.0, false) && TimeTrialOverlay::ends_race(15.02, false));
}

TEST(race_pause_exit_question) {
    // presentation §E3: EXIT asks "Exit race?" with the cursor on CANCEL; CANCEL or Triangle /
    // Square go back to the row (cursor on CONTINUE); OK ends the race. Sounds "cursor" on a
    // move, "ok" on a choice, "cancel" on Triangle / Square.
    using menu::flow::MenuKey;
    using Sounds = std::vector<std::string>;
    PauseMenu m;
    m.open(PauseMenu::Kind::Retry);
    m.tick(0);
    CHECK(press(m, MenuKey::Right) == PauseMenu::Result::None && m.cursor() == 1);
    CHECK(m.take_sounds() == Sounds{"cursor"});
    CHECK(press(m, MenuKey::Confirm) == PauseMenu::Result::None && m.confirming() && m.cursor() == 1);
    CHECK(m.take_sounds() == Sounds{"ok"});
    // CANCEL: back to the row.
    CHECK(press(m, MenuKey::Confirm) == PauseMenu::Result::None && !m.confirming() && m.cursor() == 0);
    press(m, MenuKey::Right);
    press(m, MenuKey::Confirm);
    m.take_sounds();
    CHECK(m.confirming() && press(m, MenuKey::Cancel) == PauseMenu::Result::None && !m.confirming() &&
          m.cursor() == 0);
    CHECK(m.take_sounds() == Sounds{"cancel"});
    press(m, MenuKey::Right);
    press(m, MenuKey::Confirm);
    press(m, MenuKey::Left); // OK
    CHECK(m.cursor() == 0 && press(m, MenuKey::Confirm) == PauseMenu::Result::Exit);
    // Triangle / Square in the button row: CONTINUE.
    m.open(PauseMenu::Kind::Retry);
    m.tick(0);
    CHECK(press(m, MenuKey::Cancel) == PauseMenu::Result::Continue);
}

namespace {

// A quick-menu set "qm_syn" (menu/qm_syn.mbl, menu/US/qm_syn1.pmb): the root page with START
// and EXIT and the page flags `root_flags`; `link_exit`: EXIT opens the "exit_sure" popup
// instead (the championship menus).
menu::flow::FileReader quick_menu_files(u16 root_flags, bool link_exit) {
    using namespace test::menusynth;
    auto button = [](const char* name, float x) {
        SynElement e;
        e.name = name;
        e.x = x, e.y = 380, e.w = 60, e.h = 60;
        e.flags = 0x1E;
        e.has_alt = true;
        return e;
    };
    SynPage root{"root", {button("start", 100), button("exit", 200)}, {{"defaultLayer", {}}}};
    SynPage sure{"exit_sure", {button("yes1", 300)}, {{"defaultLayer", {}}}};
    SynItem start{"start", "", "", "qm_start", "", -1, 2, {}};
    SynItem exit = link_exit ? SynItem{"exit", "", "", "", "exit_sure", 1, 0, {}}
                             : SynItem{"exit", "", "", "qm_exit", "", -1, 2, {}};
    SynItem yes{"yes1", "", "", "qm_exit", "", -1, 2, {}};
    auto files = std::make_shared<std::map<std::string, Bytes>>();
    (*files)["menu/qm_syn.mbl"] =
        write_mbl({{"exit_sure", 1, 0xE1, "", "root", "", {yes}}, {"root", 1, root_flags, "", "", "", {start, exit}}});
    (*files)["menu/US/qm_syn1.pmb"] = write_pmb({sure, root});
    return [files](const std::string& p) -> std::optional<Bytes> {
        auto it = files->find(p);
        if (it == files->end()) return std::nullopt;
        return it->second;
    };
}

} // namespace

TEST(race_quick_menu_cancel) {
    // menus §11: on a quick menu's root page with the cancel flag (0x100) Triangle / Square end
    // the menu at once as EXIT, with the "return" sound; without it (the championship menus)
    // they do nothing.
    using menu::flow::MenuKey;
    text::GameFonts fonts;
    auto settle = [](QuickMenu& q) {
        for (int i = 0; i < 120 && q.busy(); ++i) q.tick(0);
        q.tick(0);
        q.take_sounds();
    };
    QuickMenu q;
    CHECK(q.load(quick_menu_files(0x1E1, false), nullptr, fonts, "qm_syn1.pmb"));
    settle(q);
    CHECK(q.on_root() && q.focused() == "start");
    QuickMenu::Choice c = q.tick(menu::flow::key_bit(MenuKey::Cancel));
    CHECK(c.item == "exit" && c.page == "root" && quick_menu_action(c.action) == QmAction::Exit);
    CHECK(q.take_sounds() == std::vector<std::string>{"return"});
    // No cancel flag: Triangle is ignored, the page stays as it was.
    QuickMenu champ;
    CHECK(champ.load(quick_menu_files(0xE1, true), nullptr, fonts, "qm_syn1.pmb"));
    settle(champ);
    for (int i = 0; i < 30; ++i) CHECK(champ.tick(i % 2 ? 0 : menu::flow::key_bit(MenuKey::Cancel)).empty());
    CHECK(champ.take_sounds().empty() && champ.on_root() && champ.page() == "root" && !champ.busy());
}

TEST(race_quick_menu_items) {
    // menus §11: the switches the race drivers keep for a visit and the root page's rule by
    // item name.
    QuickMenuItems tt;
    tt.visit(RunKind::TimeTrial, false, false);
    // Before any run: no REPLAY, SAVE GHOST or DATA LOGGER; the rest can be chosen.
    for (const char* i : {"replay", "save_replay", "save_ghost", "data_logger", "setting"}) CHECK(!tt.enabled(i));
    for (const char* i : {"start", "demonstration", "record", "load_ghost", "option", "exit"}) CHECK(tt.enabled(i));
    tt.after_run(RunKind::TimeTrial, true, false, false); // a lap slower than the session's best
    CHECK(tt.enabled("replay") && tt.enabled("save_replay") && !tt.enabled("save_ghost") && !tt.enabled("data_logger"));
    tt.after_run(RunKind::TimeTrial, false, false, false); // left with EXIT: still a replay
    CHECK(tt.enabled("replay"));
    tt.after_run(RunKind::TimeTrial, true, true, false); // a session record
    CHECK(tt.enabled("save_ghost") && tt.enabled("data_logger"));
    tt.after_demonstration(); // its run replaces the last one; the record stays
    CHECK(!tt.enabled("replay") && tt.enabled("save_ghost") && tt.enabled("data_logger"));
    tt.after_run(RunKind::TimeTrial, true, false, false);
    CHECK(tt.enabled("replay") && tt.enabled("save_ghost"));
    // Licence: a run left before its end has no replay (the demonstration is loaded instead).
    QuickMenuItems lic;
    lic.visit(RunKind::Licence, false, false);
    lic.after_run(RunKind::Licence, false, false, false);
    CHECK(!lic.enabled("replay"));
    lic.after_run(RunKind::Licence, true, true, false);
    CHECK(lic.enabled("replay") && lic.enabled("save_ghost") && lic.enabled("data_logger"));
    // SETTING: never in a time trial; in an arcade race or free run only with a garage car.
    for (RunKind k : {RunKind::FreeRun, RunKind::ArcadeRace}) {
        QuickMenuItems q;
        q.visit(k, false, false);
        CHECK(!q.enabled("setting") && !q.enabled("save_setting") && !q.enabled("load_setting"));
        q.visit(k, true, false);
        CHECK(q.enabled("setting"));
    }
    for (RunKind k : {RunKind::MachineTest, RunKind::GtRace, RunKind::Championship}) {
        QuickMenuItems q;
        q.visit(k, false, false);
        CHECK(q.enabled("setting"));
    }
    // FREE RUN: not before a GT race on a dirt course; a championship round keeps it.
    QuickMenuItems gt, champ;
    gt.visit(RunKind::GtRace, true, true);
    champ.visit(RunKind::Championship, true, true);
    CHECK(!gt.enabled("free_session") && champ.enabled("free_session") && gt.enabled("start"));
    gt.visit(RunKind::GtRace, true, false);
    CHECK(gt.enabled("free_session"));
    // Races keep no session record; the championship's last round locks EXIT and SAVE GAME.
    gt.after_run(RunKind::GtRace, true, true, false);
    CHECK(gt.enabled("replay") && !gt.enabled("save_ghost"));
    champ.after_run(RunKind::Championship, true, false, false);
    CHECK(champ.enabled("exit") && champ.enabled("save_game") && champ.enabled("next_session"));
    champ.after_run(RunKind::Championship, true, false, true);
    CHECK(!champ.enabled("exit") && !champ.enabled("save_game") && champ.enabled("next_session") &&
          champ.enabled("replay"));
}

namespace {

// A time trial's quick menu in miniature (menu/qm_tt.mbl, menu/US/qm_tt1.pmb): START, REPLAY
// and EXIT, the `disable` widget, and the "ghost_dlg" popup.
menu::flow::FileReader quick_menu_replay_files() {
    using namespace test::menusynth;
    auto button = [](const char* name, float x) {
        SynElement e;
        e.name = name;
        e.x = x, e.y = 380, e.w = 88, e.h = 76;
        e.flags = 0x1E;
        e.has_alt = true;
        return e;
    };
    SynElement dis;
    dis.name = "disable";
    dis.x = 700, dis.y = 300, dis.w = 88, dis.h = 76;
    dis.rgba = 0xB1000000;
    dis.widget = true;
    dis.widget_class = "disable";
    SynPage root{"root", {button("start", 100), button("replay", 200), button("exit", 300)},
                 {{"defaultLayer", {dis}}}};
    SynPage dlg{"ghost_dlg", {button("yes1", 300)}, {{"defaultLayer", {}}}};
    SynItem start{"start", "", "", "qm_start", "", -1, 2, {}};
    SynItem replay{"replay", "", "", "qm_replay", "", -1, 2, {}};
    SynItem exit{"exit", "", "", "qm_exit", "", -1, 2, {}};
    SynItem yes{"yes1", "", "", "qm_yes", "", -1, 3, {}};
    auto files = std::make_shared<std::map<std::string, Bytes>>();
    (*files)["menu/qm_tt.mbl"] =
        write_mbl({{"ghost_dlg", 1, 0xC1, "", "", "", {yes}}, {"root", 1, 0x1E1, "", "", "", {start, replay, exit}}});
    (*files)["menu/US/qm_tt1.pmb"] = write_pmb({dlg, root});
    return [files](const std::string& p) -> std::optional<Bytes> {
        auto it = files->find(p);
        if (it == files->end()) return std::nullopt;
        return it->second;
    };
}

} // namespace

TEST(race_quick_menu_disabled_item) {
    // menus §11, §3: a disabled item of the root page stays focusable and refuses confirm with
    // SE "disable", nothing else; once the switches allow it, it is chosen as any other. Without
    // switches every item can be chosen; dialog pages have no rule.
    using menu::flow::MenuKey;
    text::GameFonts fonts;
    auto settle = [](QuickMenu& q) {
        for (int i = 0; i < 120 && q.busy(); ++i) q.tick(0);
        q.tick(0);
        q.take_sounds();
    };
    auto press = [](QuickMenu& q, MenuKey k) {
        QuickMenu::Choice c = q.tick(menu::flow::key_bit(k));
        for (int i = 0; i < 60 && c.empty(); ++i) c = q.tick(0);
        return c;
    };
    QuickMenu q;
    CHECK(q.load(quick_menu_replay_files(), nullptr, fonts, "qm_tt1.pmb"));
    CHECK(q.item_enabled("replay")); // no switches yet
    QuickMenuItems items;
    items.visit(RunKind::TimeTrial, false, false); // before the first run
    q.set_items(items);
    settle(q);
    CHECK(!q.item_enabled("replay") && q.item_enabled("start") && q.item_enabled("exit"));
    CHECK(q.focus("replay") && q.focused() == "replay");
    CHECK(press(q, MenuKey::Confirm).empty());
    CHECK(q.take_sounds() == std::vector<std::string>{"disable"});
    CHECK(q.on_root() && !q.busy() && q.focused() == "replay");
    press(q, MenuKey::Right); // the focus moves on past it as usual
    CHECK(q.focused() == "exit");
    // After a run: REPLAY is chosen.
    items.after_run(RunKind::TimeTrial, true, false, false);
    q.set_items(items);
    CHECK(q.focus("replay"));
    QuickMenu::Choice c = press(q, MenuKey::Confirm);
    CHECK(c.item == "replay" && quick_menu_action(c.action) == QmAction::Replay);
    // A popup's items have no rule.
    settle(q);
    CHECK(q.open_page("ghost_dlg"));
    settle(q);
    CHECK(q.item_enabled("yes1"));
    // A new layout starts without switches.
    QuickMenu r;
    CHECK(r.load(quick_menu_replay_files(), nullptr, fonts, "qm_tt1.pmb"));
    settle(r);
    CHECK(r.item_enabled("replay"));
}

TEST(race_post_run_sequence) {
    // presentation §A8, §C4, §F: after every run but a licence test and the free practice the
    // replay plays by itself, after the prize page (a finished GT race or championship round),
    // the arcade win's trophy or the record's name entry; then the quick menu.
    auto steps = [](const PostRun& p) { return (p.prize ? 1 : 0) | (p.name_entry ? 2 : 0) | (p.replay ? 4 : 0); };
    const RunOutcome won{true, 0, false}, third{true, 2, false}, left{false, -1, false}, ranked{true, -1, true};
    CHECK(steps(post_run(RunKind::ArcadeRace, won)) == (1 | 4));
    CHECK(steps(post_run(RunKind::ArcadeRace, third)) == 4); // the trophy is the win's only
    CHECK(steps(post_run(RunKind::ArcadeRace, left)) == 4);
    for (RunKind k : {RunKind::GtRace, RunKind::Championship}) {
        CHECK(steps(post_run(k, third)) == (1 | 4));
        CHECK(steps(post_run(k, left)) == 4); // left with EXIT: no prize, the replay all the same
    }
    CHECK(steps(post_run(RunKind::TwoPlayer, left)) == 4 && steps(post_run(RunKind::TwoPlayer, won)) == 4);
    for (RunKind k : {RunKind::TimeTrial, RunKind::FreeRun, RunKind::MachineTest}) {
        CHECK(steps(post_run(k, ranked)) == (2 | 4));
        CHECK(steps(post_run(k, left)) == 4);
    }
    CHECK(steps(post_run(RunKind::Licence, ranked)) == 2 && steps(post_run(RunKind::Licence, left)) == 0);
    CHECK(steps(post_run(RunKind::FreePractice, won)) == 0 && steps(post_run(RunKind::Other, won)) == 0);
}

TEST(race_menu_backdrop) {
    // menus §11.3: the time trial's, free run's and machine test's menus and the GT race's /
    // championship's pre-race menus run the race behind them (play mode 2); the result menus
    // have none; the licence menu its preview box.
    for (bool before : {true, false}) {
        for (RunKind k : {RunKind::TimeTrial, RunKind::FreeRun, RunKind::MachineTest})
            CHECK(menu_backdrop(k, before) == MenuBackdrop::Race);
        CHECK(menu_backdrop(RunKind::Licence, before) == MenuBackdrop::Preview);
        CHECK(menu_backdrop(RunKind::ArcadeRace, before) == MenuBackdrop::None);
        CHECK(menu_backdrop(RunKind::TwoPlayer, before) == MenuBackdrop::None);
    }
    for (RunKind k : {RunKind::GtRace, RunKind::Championship}) {
        CHECK(menu_backdrop(k, true) == MenuBackdrop::Race);
        CHECK(menu_backdrop(k, false) == MenuBackdrop::None);
    }
    // A replay stops when its recording runs out; the backdrop runs on; a live run has none.
    CHECK(!replay_over(PlayMode::Replay, false) && replay_over(PlayMode::Replay, true));
    CHECK(!replay_over(PlayMode::Backdrop, true) && !replay_over(PlayMode::Live, true));
}

TEST(race_pause_exit_finish) {
    // presentation §E: pause EXIT ends a run, except in a free run with a completed lap and in
    // the top-speed test once its car has moved (over 1 m/s), before its goal; a second EXIT ends
    // it. The top-speed test's overlay then comes after 0.1 s, the free run's after 5 s.
    PauseExitRun r;
    r.kind = RunKind::FreeRun;
    CHECK(!pause_exit_finishes(r));
    r.laps_completed = 1;
    CHECK(pause_exit_finishes(r) && exit_finish_overlay_delay(r) == kRecordOverlayDelay);
    r.finished = true;
    CHECK(!pause_exit_finishes(r));
    r = {};
    r.kind = RunKind::MachineTest;
    r.top_speed_mps = 50;
    CHECK(!pause_exit_finishes(r)); // a time test
    r.top_speed_test = true;
    CHECK(pause_exit_finishes(r) && exit_finish_overlay_delay(r) == kTopSpeedExitOverlayDelay);
    r.top_speed_mps = 1.0f;
    CHECK(!pause_exit_finishes(r)); // not moved yet
    r.top_speed_mps = 50, r.finished = true;
    CHECK(!pause_exit_finishes(r)); // past its goal
    for (RunKind k : {RunKind::TimeTrial, RunKind::ArcadeRace, RunKind::GtRace, RunKind::Licence, RunKind::TwoPlayer,
                      RunKind::FreePractice}) {
        PauseExitRun o;
        o.kind = k, o.laps_completed = 3, o.top_speed_mps = 50;
        CHECK(!pause_exit_finishes(o));
    }
}

// The course name over a start (presentation §B3): long form hidden for 1 s, in over 0.3 s
// (quadratic), full, out over the last second, gone after 5 s; short form 0.2 s to 2.2 s.
TEST(race_title_clock) {
    using gt3r::race::TitleClock;
    auto run = [](TitleClock& c, int steps) {
        for (int i = 0; i < steps; ++i) c.step();
    };
    TitleClock c;
    CHECK(c.alpha() == 0);
    c.start(true);
    run(c, 59);
    CHECK(c.alpha() == 0); // t < 0
    run(c, 1 + 9);         // t = 0.15: half the fade in, squared
    CHECK(std::fabs(c.alpha() - 0.25f) < 0.02f);
    run(c, 60);
    CHECK(c.alpha() == 1);
    run(c, 156); // t = 3.75: a quarter of the fade out left
    CHECK(std::fabs(c.alpha() - 0.0625f) < 0.02f);
    run(c, 60);
    CHECK(c.alpha() == 0); // at the end it stays hidden
    run(c, 600);
    CHECK(c.alpha() == 0);
    c.start(false);
    run(c, 11);
    CHECK(c.alpha() == 0);
    run(c, 1 + 12); // t = 0.2: past the 0.1 s fade in
    CHECK(c.alpha() == 1);
    run(c, 120);
    CHECK(c.alpha() == 0);
    c.start(true), c.stop();
    run(c, 120);
    CHECK(c.alpha() == 0);
}

// The replay mark (presentation §G): hidden 3 s, in over 2 s, shown 30 s, out over 2 s, hidden
// 30 s, in again.
TEST(race_replay_mark_timer) {
    gt3r::race::ReplayMarkTimer m;
    m.start();
    int step = 0, first_lit = -1, full = -1, out = -1, back = -1;
    for (; step < 6000; ++step) {
        m.step();
        const float a = m.alpha();
        if (first_lit < 0 && a > 0) first_lit = step;
        if (full < 0 && a >= 1) full = step;
        if (full >= 0 && out < 0 && a < 1) out = step;
        if (out >= 0 && back < 0 && a <= 0) back = step;
        if (back >= 0 && step > back + 10 && a > 0) break;
    }
    CHECK(first_lit >= 180 && first_lit <= 182);
    CHECK(full - first_lit >= 118 && full - first_lit <= 121);
    CHECK(out - full >= 1799 && out - full <= 1802);
    CHECK(back - out >= 118 && back - out <= 121);
    CHECK(step - back >= 1799 && step - back <= 1803); // 30 s hidden before it comes back
}

// Touch controls (presentation §D11): the top corner blocks stay in their corners beside the
// controls (and over one that only comes up under their bottom line), move sideways just clear
// of a control that would cover them, and go inside the safe area when that is further than its
// edge. Drawn without game data (placeholder text) on a 640x360 software target.
TEST(hud_top_blocks_beside_touch_controls) {
    SDL_Surface* surface = SDL_CreateSurface(640, 360, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* r = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    CHECK(r != nullptr);
    if (!r) {
        SDL_DestroySurface(surface);
        return;
    }
    Hud hud;
    // The lit columns' extent in rows 0..199 of x0..x1 (-1 / 640: none).
    auto lit = [&](const HudData& d, int x0, int x1, bool rightmost) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        hud.draw(r, d);
        SDL_FlushRenderer(r);
        int edge = rightmost ? -1 : 640;
        for (int y = 0; y < 200; ++y)
            for (int x = x0; x < x1; ++x) {
                const auto* p = static_cast<const unsigned char*>(surface->pixels) + y * surface->pitch + x * 4;
                if (p[0] | p[1] | p[2]) edge = rightmost ? std::max(edge, x) : std::min(edge, x);
            }
        return edge;
    };
    HudData d;
    d.total_laps = 3, d.lap = 1, d.lap_time = 31;
    d.cars = 6, d.position = 6;
    const int times = lit(d, 330, 640, true), rank = lit(d, 0, 320, false);
    CHECK(times > 600 && rank > 20 && rank < 40); // the lap count and the rank in the corners
    // The driving layout on 16:9: pedals and shift buttons bottom right, pause row top centre.
    d.safe_area = {0, 44, 446, 316};
    d.touch_controls = {{492, 214, 134, 132}, {501, 160, 116, 44}, {446, 310, 36, 36}, {266, 14, 108, 30}};
    CHECK(lit(d, 330, 640, true) == times && lit(d, 0, 320, false) == rank);
    // A control coming up under the bottom line (Lap, 112.5..120.5): underlapped.
    d.touch_controls.push_back({560, 110, 66, 40});
    CHECK(lit(d, 330, 640, true) == times);
    // Over the time lines: the block moves left until it is 4 units clear.
    d.touch_controls.back() = {560, 80, 66, 40};
    const int moved = lit(d, 330, 640, true);
    CHECK(moved < 556 && moved > 500);
    // Clearing it would take the block past the safe area's edge: inside the area.
    d.touch_controls.back() = {300, 80, 326, 40};
    const int inside = lit(d, 330, 640, true);
    CHECK(std::abs(inside - (times - 194)) <= 1);
    // The rank (at 30): moved right, mirrored, within the safe area's left inset.
    d.safe_area = {150, 44, 296, 316};
    d.touch_controls = {{20, 30, 30, 30}};
    CHECK(lit(d, 0, 320, false) >= 54 && lit(d, 0, 320, false) <= 56);
    SDL_DestroyRenderer(r);
    SDL_DestroySurface(surface);
}
