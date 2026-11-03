// The race screen's pop-ups (presentation §D6) and the race loading screen (menus §6), without
// game data: the race gaps' passing order, the time trials' best lap and split times, the event
// display's queue and timing, the message texts, the lap timer's split-point crossings, the
// course's split points, the machine tests' timing points, goal and pop-ups (presentation §B7),
// what replays post and for which car, the time trial's one lap, and the loading picture's
// frame catch-up. All times are invented.
// The disc tests check every course's split points and the font's dash glyph, and the machine
// tests' events and test course.

#include "test_framework.h"

#include "disc/disc.h"
#include "formats/course/course_file.h"
#include "formats/course/road.h"
#include "formats/text/fnt.h"
#include "game/data/car_database.h"
#include "menu/flow/screen_transition.h"
#include "race/lap_events.h"
#include "race/machine_test.h"
#include "race/results.h"
#include "track/track.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::race;

namespace {

// Frames (after the frame it was added in) on which a message is drawn.
std::vector<int> shown_frames(EventDisplay& d, const std::string& text, int frames) {
    std::vector<int> out;
    for (int f = 1; f <= frames; ++f) {
        d.tick();
        for (const EventDisplay::Message* m : d.visible())
            if (m->text == text) out.push_back(f);
    }
    return out;
}

} // namespace

TEST(race_lap_event_texts) {
    CHECK(lap_time_text(83456) == "1'23.456");
    CHECK(lap_time_text(5007) == "0'05.007");
    CHECK(split_text(2, 32123) == "T2 0'32.123");
    CHECK(gap_text(1234) == "+0'01.234");
    CHECK(gap_text(0) == "+0'00.000");
    CHECK(gap_text(-456) == "\x7F" "0'00.456"); // the font's dash glyph for a lead
    CHECK(lap_diff_text(1, "%d Laps") == "+1 Laps");
    CHECK(lap_diff_text(3, "%d Laps") == "+3 Laps");
}

// A licence test's splits count on over its laps, the line as each lap's last timing point (the licence conductor's
// checkpoint numbers + 1): IB-2's circle has no split points, so its 3-lap run shows T1 and T2 at the line before its end.
TEST(race_license_split_numbers) {
    CHECK(license_split_number(true, 0, 0, 1) == 1);
    CHECK(license_split_number(true, 1, 0, 1) == 2);
    // Two split points (3 timing points a lap): 1, 2, the line 3, then 4, 5, the line 6.
    CHECK(license_split_number(false, 0, 0, 3) == 1 && license_split_number(false, 0, 1, 3) == 2);
    CHECK(license_split_number(true, 0, 3, 3) == 3); // (the line whatever the points passed)
    CHECK(license_split_number(false, 1, 0, 3) == 4 && license_split_number(false, 1, 1, 3) == 5);
    CHECK(license_split_number(true, 1, 2, 3) == 6);
}

TEST(race_passing_order_gaps) {
    PassingOrder po;
    po.reset();
    // Lap 0, point 0: car 4 first (no gap yet), then car -1 1.5 s later.
    CHECK(po.pass(4, 0, 0, 10000) == 0);
    CHECK(po.take_gaps().empty());
    CHECK(po.pass(-1, 0, 0, 11500) == 0);
    std::vector<PassingOrder::Gap> g = po.take_gaps();
    CHECK(g.size() == 2);
    CHECK(g[0].car == -1 && g[0].ms == 1500); // behind the first car through
    CHECK(g[1].car == 4 && g[1].ms == -1500); // the first car's lead over the second
    // The third car: its gap to the first car through, not to the car ahead; no lead for car 4.
    CHECK(po.pass(2, 0, 0, 14000) == 0);
    g = po.take_gaps();
    CHECK(g.size() == 1 && g[0].car == 2 && g[0].ms == 4000);
    // Two cars in one tick, handed in race order but the second passed earlier: time order.
    CHECK(po.pass(7, 0, 1, 50000) == 0);
    CHECK(po.take_gaps().empty());
    CHECK(po.pass(5, 0, 1, 52300) == 0);
    CHECK(po.pass(6, 0, 1, 51200) == 0);
    g = po.take_gaps();
    CHECK(g.size() == 3);
    CHECK(g[0].car == 6 && g[0].ms == 1200);
    CHECK(g[1].car == 7 && g[1].ms == -1200);
    CHECK(g[2].car == 5 && g[2].ms == 2300);
    // A new lead lap starts the point's order again; a car on an earlier lap is a lap down.
    CHECK(po.pass(4, 1, 0, 100000) == 0);
    CHECK(po.take_gaps().empty());
    CHECK(po.pass(3, 0, 0, 101000) == 1);
    CHECK(po.pass(3, -1, 0, 101000) == 2);
    CHECK(po.take_gaps().empty());
    CHECK(po.pass(-1, 1, 0, 103000) == 0);
    g = po.take_gaps();
    CHECK(g.size() == 2 && g[0].car == -1 && g[0].ms == 3000 && g[1].car == 4 && g[1].ms == -3000);
    // Points are independent; out-of-range points are ignored.
    CHECK(po.pass(1, 0, 3, 7000) == 0 && po.pass(1, 0, 4, 7000) == 0);
    CHECK(po.take_gaps().empty());
}

TEST(race_best_lap_splits) {
    // presentation §D6: the session starts with no best lap (no stored record seeds it), so
    // the first lap and its split points show no gap.
    BestLapSplits b;
    CHECK(b.best_lap() < 0 && !b.lap_gap(91000) && !b.split_gap(0, 30000));
    b.split(0, 30000), b.split(1, 61000);
    b.lap_done(91000); // the first lap is the best so far, with its split times
    CHECK(b.best_lap() == 91000);
    CHECK(b.split_gap(0, 29000) == std::optional<long long>(-1000));
    b.split(0, 30500), b.split(1, 62000);
    CHECK(b.lap_gap(92000) == std::optional<long long>(1000)); // taken before the lap updates it
    b.lap_done(92000); // slower: kept as it was
    CHECK(b.best_lap() == 91000 && b.split_gap(0, 30000) == std::optional<long long>(0));
    b.split(0, 29500), b.split(1, 59000);
    CHECK(b.lap_gap(89000) == std::optional<long long>(-2000));
    b.lap_done(89000); // faster: the lap and its split times
    CHECK(b.best_lap() == 89000);
    CHECK(b.split_gap(0, 30000) == std::optional<long long>(500));
    CHECK(b.split_gap(1, 58000) == std::optional<long long>(-1000));
    CHECK(!b.split_gap(2, 70000)); // a point the best lap did not pass
    // A retry is a new run of the same session: the best lap and its split times stay, the
    // unfinished lap's split times go.
    b.split(0, 28000);
    b.begin_run();
    CHECK(b.best_lap() == 89000 && b.lap_gap(90000) == std::optional<long long>(1000));
    b.split(1, 58500);
    b.lap_done(88000); // faster, but the new run passed only the second point
    CHECK(b.best_lap() == 88000 && !b.split_gap(0, 29000) && b.split_gap(1, 58000) == std::optional<long long>(-500));
    // A new session (another visit) starts from nothing again.
    b.reset();
    CHECK(b.best_lap() < 0 && !b.lap_gap(60000) && !b.split_gap(1, 58000));
}

TEST(race_event_display_timing) {
    // One message: drawn from the frame after it is added, for 3 s.
    {
        EventDisplay d;
        EventDisplay::Message m;
        m.text = "1'23.456";
        m.large = true;
        d.queue(m);
        std::vector<int> f = shown_frames(d, "1'23.456", 400);
        CHECK(!f.empty() && f.front() == 1 && f.back() >= 179 && f.back() <= 181 && int(f.size()) == f.back());
    }
    // Queued messages run one after the other; a gap joins the current group at once.
    {
        EventDisplay d;
        EventDisplay::Message a, b;
        a.text = "A", b.text = "B";
        d.queue(a);
        d.queue(b);
        d.gap(1500, true, 3);
        CHECK(std::fabs(d.queue_end() - 6.0f) < 1e-4f && d.group_start() == 0.0f);
        bool a_first = false, b_later = false, gap_now = false;
        int together = 0;
        for (int f = 1; f <= 400; ++f) {
            d.tick();
            bool sa = false, sb = false, sg = false;
            for (const EventDisplay::Message* m : d.visible()) {
                sa |= m->text == "A", sb |= m->text == "B", sg |= m->text == "+0'01.500";
                if (m->text == "+0'01.500") CHECK(m->dx == 37 && m->dy == 25 && m->r == 240 && m->g == 0 && !m->large);
            }
            if (f == 1) a_first = sa && !sb, gap_now = sg;
            if (f == 200) b_later = sb && !sa && !sg;
            together += sa && sb;
        }
        CHECK(a_first && gap_now && b_later && together <= 1); // (float timers: at most a frame's overlap)
    }
    // A lead: blue, the other way round; without a picture the text is centred.
    {
        EventDisplay d;
        d.gap(-800, true, 1);
        d.tick();
        std::vector<const EventDisplay::Message*> v = d.visible();
        CHECK(v.size() == 1 && v[0]->dx == -37 && v[0]->r == 128 && v[0]->g == 128 && v[0]->b == 255);
        EventDisplay::Icon ic = d.icon();
        CHECK(!ic.on); // the first frame after the post is in the first blink-off phase
        EventDisplay e;
        e.gap(2000, false);
        e.tick();
        CHECK(e.visible().size() == 1 && e.visible()[0]->dx == 0 && e.visible()[0]->dy == 25 && !e.icon().on);
    }
    // The car picture: 62 units beside the anchor, blinking off for 12 frames three times.
    {
        EventDisplay d;
        d.gap(1000, true, -1);
        std::string pattern;
        for (int f = 1; f <= 80; ++f) {
            d.tick();
            EventDisplay::Icon ic = d.icon();
            if (ic.on) CHECK(ic.car == -1 && ic.dx == -62 && ic.dy == 20);
            pattern += ic.on ? '1' : '0';
        }
        // Counter 72 - f: hidden while (counter / 12) is odd.
        CHECK(pattern.substr(0, 12) == std::string(12, '0'));  // 71..60
        CHECK(pattern.substr(12, 12) == std::string(12, '1')); // 59..48
        CHECK(pattern.substr(24, 12) == std::string(12, '0')); // 47..36
        CHECK(pattern.substr(36, 12) == std::string(12, '1'));
        CHECK(pattern.substr(48, 12) == std::string(12, '0'));
        CHECK(pattern.substr(60) == std::string(20, '1'));
    }
    // Eight slots: a ninth message while all are busy is dropped.
    {
        EventDisplay d;
        for (int i = 0; i < 9; ++i) {
            EventDisplay::Message m;
            m.text = "M" + std::to_string(i);
            d.parallel(m);
        }
        d.tick();
        CHECK(d.visible().size() == 8);
    }
    // A free run's start signal (presentation §B2): a note 2 s after the signal (in parallel,
    // delayed), the next one queued behind it: 2-5 s and 5-8 s.
    {
        EventDisplay d;
        EventDisplay::Message ready, help;
        ready.text = "ready", help.text = "help";
        d.after(ready, 2.0f);
        d.queue(help);
        CHECK(std::fabs(d.queue_end() - 8.0f) < 1e-4f && std::fabs(d.group_start() - 2.0f) < 1e-4f);
        EventDisplay e = d;
        std::vector<int> r = shown_frames(d, "ready", 600), h = shown_frames(e, "help", 600);
        CHECK(!r.empty() && r.front() >= 119 && r.front() <= 121 && r.back() >= 299 && r.back() <= 301);
        CHECK(!h.empty() && h.front() >= 299 && h.front() <= 301 && h.back() >= 479 && h.back() <= 481);
    }
    // A queued message waits for the queue, also behind a message still showing.
    {
        EventDisplay d;
        EventDisplay::Message a, b;
        a.text = "pit", b.text = "lap";
        d.queue(a);
        for (int i = 0; i < 60; ++i) d.tick();
        d.queue(b);
        std::vector<int> f = shown_frames(d, "lap", 400);
        CHECK(!f.empty() && f.front() >= 119 && f.front() <= 121);
    }
}

namespace {

// A flat ring of 200 m (radius R), 16 m wide, with a checkpoint every 1/32 of the lap and
// split-point markers: kind 2 / 0 are split points (at most 3), others not.
constexpr float kRingR = 200.0f / (2 * std::numbers::pi_v<float>);

course::Road ring_road() {
    course::Road road;
    const float R = kRingR;
    road.length = 200;
    for (int i = 0; i < 32; ++i) {
        float t0 = 2 * std::numbers::pi_v<float> * float(i) / 32, t1 = 2 * std::numbers::pi_v<float> * float(i + 1) / 32;
        course::RoadSegment s;
        for (auto [r, t] : {std::pair{R - 8, t0}, {R + 8, t0}, {R + 8, t1}, {R - 8, t1}})
            s.vertices.push_back({r * std::cos(t), 0, r * std::sin(t)});
        s.triangles.push_back({0, 1, 2, 0, 0, 0, 0});
        s.triangles.push_back({0, 2, 3, 0, 0, 0, 0});
        s.bounds_min = {-R - 8, 0, -R - 8}, s.bounds_max = {R + 8, 0, R + 8};
        road.segments.push_back(std::move(s));
        course::Checkpoint c;
        c.x0 = (R - 8) * std::cos(t0), c.z0 = (R - 8) * std::sin(t0);
        c.x1 = (R + 8) * std::cos(t0), c.z1 = (R + 8) * std::sin(t0);
        c.distance = R * t0;
        c.dir_x = std::sin(t0), c.dir_z = -std::cos(t0);
        road.checkpoints.push_back(c);
    }
    road.markers = {{0xFFFF, 0}, {2 | (1u << 16), 30}, {2, 50}, {1, 70}, {2, 120}, {2, 160}, {2, 180}};
    return road;
}

// The point s metres along the ring's centre line.
sim::Vec3 ring_at(float s) {
    float th = s / kRingR;
    return sim::Vec3{kRingR * std::cos(th), 0, kRingR * std::sin(th)};
}

} // namespace

TEST(race_lap_timer_split_points) {
    course::Road road = ring_road();
    CHECK(track::display_checkpoints(road) == std::vector<float>({50, 120, 160}));
    track::Track t(road);
    track::LapTimer lt(t);
    CHECK(lt.checkpoints().size() == 3);
    // From 5 m behind the line round twice, 1 m a tick at 50 m/s.
    double time = 0;
    std::vector<track::LapTimer::Crossing> all;
    auto at = ring_at;
    for (float s = -5; s < 2 * 200 + 10; s += 1.0f) {
        time += 0.02;
        lt.update(at(s), time);
        for (const auto& c : lt.crossings()) all.push_back(c);
    }
    CHECK(all.size() == 8); // 3 split points and the line, twice (not the start crossing)
    if (all.size() == 8) {
        for (int k = 0; k < 8; ++k) {
            CHECK(all[size_t(k)].lap == k / 4);
            CHECK(all[size_t(k)].line == (k % 4 == 3));
            CHECK(all[size_t(k)].point == k % 4); // the line: the 3 points passed this lap
        }
        // Lap 1 from the line (time trial default): splits 50 m, 120 m at 50 m/s = 1 s, 2.4 s.
        CHECK(std::llabs(all[0].split_ms - 1000) <= 20 && std::llabs(all[1].split_ms - 2400) <= 20);
        CHECK(std::llabs(all[3].split_ms - 4000) <= 20 && all[4].split_ms < all[5].split_ms);
        CHECK(all[4].ms - all[3].ms == all[4].split_ms); // lap 2's splits from its start
    }
    // A missed split point (re-placed past it): no further splits that lap, the line counts
    // the points passed.
    track::LapTimer m(t);
    time = 0;
    std::vector<track::LapTimer::Crossing> got;
    for (float s = -5; s < 40; s += 1.0f) m.update(at(s), time += 0.02);
    m.teleport(at(130));
    for (float s = 130; s < 210; s += 1.0f) {
        m.update(at(s), time += 0.02);
        for (const auto& c : m.crossings()) got.push_back(c);
    }
    CHECK(got.size() == 1 && got[0].line && got[0].point == 0);
}

TEST(race_time_trial_one_lap) {
    // presentation §B7: a flying start's first line crossing starts the time trial's lap; the
    // car finishes at the next one, its first completed lap (not after the event's 2 laps).
    track::Track t(ring_road());
    track::LapTimer lt(t);
    double time = 0;
    float finish_s = -1;
    double lap = -1;
    lt.update(ring_at(-30), time);
    for (float s = -29; s < 2 * 200 + 10 && finish_s < 0; s += 1.0f) {
        time += 0.02; // 50 m/s
        if (lt.update(ring_at(s), time) && solo_run_finished(lt.laps_completed(), kTimeTrialLaps))
            finish_s = s, lap = lt.last_lap_time();
    }
    CHECK(finish_s >= 200 && finish_s < 201);
    CHECK(std::fabs(lap - 4.0) <= 0.03 && lt.best_lap_time() == lap && lt.laps_completed() == 1);
}

TEST(race_loading_picture_frames) {
    using menu::flow::LoadingPicture;
    CHECK(LoadingPicture::frames_for(0) == 1);                  // every drawn frame advances it
    CHECK(LoadingPicture::frames_for(16'666'667) == 1);
    CHECK(LoadingPicture::frames_for(1'000'000'000) == 60);     // a 1 s blocking load: 60 frames
    CHECK(LoadingPicture::frames_for(3'500'000'000) == 210);    // past the 180-frame cursor wait
    CHECK(LoadingPicture::frames_for(60'000'000'000) == 600);   // at most 10 s at once
    LoadingPicture p;
    CHECK(!p.valid());
    p.transition.start(menu::flow::TransitionType::Fadeout);
    CHECK(!p.valid()); // no frame
}

TEST(race_split_points_disc) {
    Disc* d = test::test_disc();
    if (!d) SKIP("needs GT3R_DISC");
    // Every course's split points: 2 or 3 on every race course, at most 3 on all.
    int courses = 0, races = 0;
    for (const auto& e : d->volume().entries()) {
        const std::string prefix = "courses/US/";
        if (e.path.rfind(prefix, 0) != 0 || e.path.back() == '/' || e.path.find(".list") != std::string::npos) continue;
        Bytes data = d->volume().read(e);
        course::CourseFile file = course::CourseFile::parse(data);
        ByteSpan road = file.section(data, course::kSlotRoad);
        if (road.empty()) continue;
        course::Road r = course::Road::parse(road);
        std::vector<float> cps = track::display_checkpoints(r);
        ++courses;
        CHECK(cps.size() <= 3);
        for (float c : cps) CHECK(c > 0 && c < r.length);
        const std::string name = e.path.substr(prefix.size());
        if (name.rfind("L_", 0) != 0 && name != "super") {
            ++races;
            if (cps.size() < 2) std::printf("  %s: %zu split points\n", name.c_str(), cps.size());
            CHECK(cps.size() >= 2);
        }
    }
    std::printf("  %d courses, %d race courses with 2-3 split points\n", courses, races);
    CHECK(courses >= 80);
    // The gap text's lead sign is the race font's dash glyph.
    const Volume::Entry* fe = d->volume().find("font/system.fnt");
    CHECK(fe != nullptr);
    if (fe) {
        text::FontFile ff = text::FontFile::parse(d->volume().read(*fe));
        bool found = false;
        for (const text::LatinFont& f : ff.fonts)
            if (f.name == "sw_16") {
                found = true;
                CHECK(f.has(0x7F));
            }
        CHECK(found);
    }
}

namespace {

// A machine test's run from the grid (the car's centre 2 m behind the line), accelerating at
// 5 m/s^2 up to 80 m/s: the timing points with the top speed so far and the lap they fell on.
struct MachineRun {
    struct Mark {
        MachineTest::Crossing c;
        float top_kmh = 0;
        int lap = 0;
    };
    std::vector<Mark> marks;
    int ticks = 0;
};

MachineRun run_machine_test(MachineTest& mt, double lap_m, double until_m) {
    MachineRun r;
    const double dt = 1.0 / 60.0;
    double x = -2.0, v = 0, t = 0;
    float top = 0;
    mt.reset();
    while (x < until_m && r.ticks < 60 * 600) {
        const double x0 = x, t0 = t;
        v = std::min(80.0, v + 5.0 * dt);
        x += v * dt;
        t += dt;
        top = std::max(top, float(v * 3.6));
        for (const MachineTest::Crossing& c : mt.update(x0, x, lap_m, t0, t))
            r.marks.push_back({c, top, int(std::floor(x / lap_m))});
        ++r.ticks;
    }
    return r;
}

} // namespace

TEST(race_machine_test_marks) {
    // presentation §B7: the event's test byte picks the test; the timing points are the first
    // 1, 2 or 3 of 400 m, 1000 m and 6845 m past the control line, the last one the goal.
    CHECK(MachineTest(1).points() == 1 && MachineTest(1).goal_m() == 400 && MachineTest(1).timed());
    CHECK(MachineTest(2).points() == 2 && MachineTest(2).goal_m() == 1000 && MachineTest(2).timed());
    CHECK(MachineTest(3).points() == 3 && MachineTest(3).goal_m() == 6845 && !MachineTest(3).timed());
    for (int b : {0, 4, 9}) // 4 or more reads as 0, which runs like the top-speed test
        CHECK(MachineTest(b).type() == 0 && MachineTest(b).points() == 3 && !MachineTest(b).timed());

    // Timed like a lap line: the clock before the tick plus the passed fraction of it.
    const double L = 10342.26, dt = 1.0 / 60.0;
    MachineTest m(2);
    CHECK(m.update(-0.5, 0.5, L, 0, dt).empty());     // the start crossing: nothing
    CHECK(m.update(-400, 401, L, 1, 1 + dt).empty()); // across the line: nothing else counts
    std::vector<MachineTest::Crossing> c = m.update(399, 401, L, 10, 10 + dt);
    CHECK(c.size() == 1 && c[0].point == 0 && !c[0].goal && c[0].ms == 10008); // (30000 + 25) / 3
    CHECK(m.update(401, 403, L, 10 + dt, 10 + 2 * dt).empty() && !m.done());
    c = m.update(999.5, 1000.5, L, 20, 20 + dt);
    CHECK(c.size() == 1 && c[0].point == 1 && c[0].goal && c[0].ms == 20008 && m.done());
    CHECK(m.update(1000.5, 1100, L, 20 + dt, 20 + 2 * dt).empty()); // the run is over

    // The top-speed test on the test course (10342 m a lap): T1 at 400 m, T2 at 1000 m and the
    // goal 6845 m into the first lap; the line is not crossed again before it.
    MachineTest top(3);
    MachineRun r = run_machine_test(top, L, 8000);
    std::printf("  top-speed test: %zu timing points, goal at %.3f s with %.1f km/h\n", r.marks.size(),
                r.marks.empty() ? 0.0 : double(r.marks.back().c.ms) / 1000.0,
                r.marks.empty() ? 0.0 : double(r.marks.back().top_kmh));
    CHECK(r.marks.size() == 3 && top.done());
    if (r.marks.size() == 3) {
        CHECK(r.marks[0].c.point == 0 && !r.marks[0].c.goal && r.marks[1].c.point == 1 && !r.marks[1].c.goal);
        CHECK(r.marks[2].c.point == 2 && r.marks[2].c.goal && r.marks[2].lap == 0);
        // 2 m to the line, then 400 m: 402 m from rest at 5 m/s^2 takes 12.68 s (from GO).
        CHECK(std::abs(double(r.marks[0].c.ms) - 12681.0) <= 20);
        CHECK(r.marks[2].top_kmh == 288.0f && r.marks[2].c.ms > r.marks[1].c.ms);
    }
    // 0-400 m: the goal is the only timing point.
    MachineTest t400(1);
    r = run_machine_test(t400, L, 1000);
    CHECK(r.marks.size() == 1 && t400.done());
    if (!r.marks.empty()) CHECK(r.marks[0].c.goal && r.marks[0].c.point == 0);

    // On a course shorter than the goal the top-speed run never ends; the split points start
    // again after every line crossing.
    MachineTest short_course(3);
    r = run_machine_test(short_course, 5000, 3 * 5000 - 100);
    CHECK(r.marks.size() == 6 && !short_course.done());
    for (size_t i = 0; i < r.marks.size(); ++i)
        CHECK(!r.marks[i].c.goal && r.marks[i].c.point == int(i % 2) && r.marks[i].lap == int(i / 2));
}

TEST(race_machine_test_popups) {
    // presentation §B7, §D6: the time tests show their split times and, at the goal, the time
    // to it (like a lap time): green, queued, one after the other. Nothing at the line, and
    // never a gap. (The top-speed test's HUD has no event display: the race posts nothing.)
    const double L = 10342.26;
    for (int type : {1, 2}) {
        MachineTest mt(type);
        MachineRun r = run_machine_test(mt, L, 8000);
        EventDisplay d;
        for (const MachineRun::Mark& mk : r.marks) {
            EventDisplay::Message m = machine_test_message(mk.c);
            CHECK(m.large == mk.c.goal);
            d.queue(std::move(m));
        }
        std::vector<std::string> shown;
        bool green = true, icon = false, gap = false;
        for (int f = 0; f < 60 * 12; ++f) {
            d.tick();
            for (const EventDisplay::Message* m : d.visible()) {
                if (shown.empty() || shown.back() != m->text) shown.push_back(m->text);
                green = green && m->r == 73 && m->g == 204 && m->b == 43;
                gap = gap || m->text.empty() || m->text[0] == '+' || m->text[0] == '\x7F';
            }
            icon = icon || d.icon().on;
        }
        std::vector<std::string> want;
        for (const MachineRun::Mark& mk : r.marks)
            want.push_back(mk.c.goal ? lap_time_text(mk.c.ms) : split_text(mk.c.point + 1, mk.c.ms));
        std::printf("  machine test %d:", type);
        for (const std::string& t : shown) std::printf(" [%s]", t.c_str());
        std::printf("\n");
        CHECK(shown == want && green && !icon && !gap);
    }
}

TEST(race_replay_lap_event_rules) {
    // presentation §D6, §D9: a replay re-runs the race and posts what the live run did, except
    // the gaps to the session's best lap and the session's laps; a licence test's time at its
    // end comes as a lap time; the top-speed test's split times and top speed show.
    // Demonstrations get none (ours).
    auto none = [](const LapEventRules& r) {
        return !r.lap_times && !r.splits && !r.race_gaps && !r.best_lap_gaps && !r.session_laps && !r.license_time &&
               !r.top_speed;
    };
    for (HudRun run : {HudRun::Race, HudRun::Battle}) {
        const LapEventRules live = lap_event_rules(run, HudPlay::Live), rp = lap_event_rules(run, HudPlay::Replay);
        CHECK(live.lap_times && live.splits && live.race_gaps && live.session_laps && !live.best_lap_gaps);
        CHECK(rp.lap_times && rp.splits && rp.race_gaps && !rp.session_laps && !rp.best_lap_gaps && !rp.license_time);
    }
    for (HudRun run : {HudRun::TimeTrial, HudRun::FreeRun, HudRun::FreePractice}) {
        const LapEventRules live = lap_event_rules(run, HudPlay::Live), rp = lap_event_rules(run, HudPlay::Replay);
        CHECK(live.lap_times && live.splits && live.best_lap_gaps && live.session_laps && !live.race_gaps);
        CHECK(rp.lap_times && rp.splits && !rp.best_lap_gaps && !rp.session_laps && !rp.race_gaps);
    }
    const LapEventRules lic = lap_event_rules(HudRun::License, HudPlay::Live),
                        lic_rp = lap_event_rules(HudRun::License, HudPlay::Replay);
    CHECK(lic.splits && !lic.lap_times && !lic.license_time);
    CHECK(lic_rp.splits && !lic_rp.lap_times && lic_rp.license_time);
    for (HudPlay play : {HudPlay::Live, HudPlay::Replay}) {
        const LapEventRules mt = lap_event_rules(HudRun::MachineTime, play);
        CHECK(mt.lap_times && mt.splits && !mt.top_speed && !mt.race_gaps && !mt.best_lap_gaps);
    }
    CHECK(none(lap_event_rules(HudRun::MachineSpeed, HudPlay::Live))); // its HUD has no event display
    const LapEventRules ms = lap_event_rules(HudRun::MachineSpeed, HudPlay::Replay);
    CHECK(ms.splits && ms.top_speed && !ms.lap_times);
    for (HudRun run : {HudRun::Race, HudRun::TimeTrial, HudRun::FreeRun, HudRun::FreePractice, HudRun::License,
                       HudRun::MachineTime, HudRun::MachineSpeed, HudRun::Battle})
        CHECK(none(lap_event_rules(run, HudPlay::Demo)));
}

TEST(race_replay_followed_car_events) {
    // presentation §D6, §G3: live, each player's display shows that player's messages; in a
    // replay the single view shows the messages of the car the cameras follow, whichever it is.
    CHECK(event_display_of(-1, HudPlay::Live, 3) == 0 && event_display_of(-2, HudPlay::Live, 3) == 1 &&
          event_display_of(3, HudPlay::Live, 3) == -1);
    CHECK(event_display_of(3, HudPlay::Replay, 3) == 0 && event_display_of(-1, HudPlay::Replay, 3) == -1 &&
          event_display_of(-1, HudPlay::Replay, -1) == 0 && event_display_of(-2, HudPlay::Replay, -1) == -1);
    // A replay of three cars (the player -1, opponents 0 and 1) on a course with one split
    // point (timing points 0 and 1, the line), fed as the race screen does: every car's
    // crossings through the passing order, the followed car's messages on the display. The
    // cameras follow opponent 0 through the split point, then the player at the line.
    struct Pass {
        int frame, car, point;
        long long ms;
    };
    const Pass passes[] = {{10, 1, 0, 30000}, {40, 0, 0, 30500}, {70, -1, 0, 31000},
                           {500, 1, 1, 90000}, {548, -1, 1, 90800}, {560, 0, 1, 91000}};
    PassingOrder po;
    po.reset();
    EventDisplay d;
    std::vector<std::string> shown;
    std::vector<int> icon_cars;
    for (int f = 0; f < 900; ++f) {
        const int followed = f < 300 ? 0 : -1;
        for (const Pass& p : passes) {
            if (p.frame != f) continue;
            if (event_display_of(p.car, HudPlay::Replay, followed) == 0) {
                EventDisplay::Message m;
                m.text = p.point == 1 ? lap_time_text(p.ms) : split_text(p.point + 1, p.ms);
                m.large = p.point == 1;
                d.queue(std::move(m));
            }
            CHECK(po.pass(p.car, 0, p.point, p.ms) == 0);
        }
        for (const PassingOrder::Gap& g : po.take_gaps())
            if (event_display_of(g.car, HudPlay::Replay, followed) == 0)
                d.gap(g.ms, true, g.ms >= 0 ? 1 : 0); // the picture: position 1 (car 1) when behind
        d.tick();
        for (const EventDisplay::Message* m : d.visible())
            if (std::find(shown.begin(), shown.end(), m->text) == shown.end()) shown.push_back(m->text);
        if (d.icon().on && (icon_cars.empty() || icon_cars.back() != d.icon().car)) icon_cars.push_back(d.icon().car);
    }
    std::printf("  replay pop-ups:");
    for (const std::string& t : shown) std::printf(" [%s]", t.c_str());
    std::printf("\n");
    // Opponent 0: its split and its gap to car 1 (first through); the player: its lap time and
    // its gap to car 1. Not car 1's lead, the player's split or opponent 0's lap.
    const std::vector<std::string> want = {"T1 0'30.500", "+0'00.500", "1'30.800", "+0'00.800"};
    CHECK(shown == want);
    CHECK(icon_cars == std::vector<int>{1});
}

TEST(race_replay_top_speed_popup) {
    // presentation §D6: the top-speed test's goal in its replay: the top speed so far, "%.1f"
    // (rounded, unlike the records' truncated tenths) and the unit with no space, green sw_16.
    const EventDisplay::Message m = machine_speed_message(312.44, false);
    CHECK(m.text == "312.4km/h" && !m.large && m.r == 73 && m.g == 204 && m.b == 43 && m.dx == 0 && m.dy == 0);
    CHECK(machine_speed_message(312.46, false).text == "312.5km/h");
    CHECK(record_speed_text(312.46, false) == "312.4km/h");
    CHECK(machine_speed_message(200.0, true).text == "124.3mph"); // x 0.62139
}

TEST(race_machine_test_events_disc) {
    Disc* d = test::test_disc();
    if (!d) SKIP("needs GT3R_DISC");
    // The machine tests' events: the lap-count byte is the test (presentation §B7); every test
    // course is longer than the top-speed goal, with the grid's pole on the control line.
    data::CarDatabase db;
    CHECK(db.load(d->volume()));
    const std::pair<const char*, int> tests[] = {{"M400", 1}, {"M1000", 2}, {"MMAX", 3}};
    for (const auto& [code, type] : tests) {
        const data::Event* ev = db.event(code);
        CHECK(ev != nullptr);
        if (!ev) continue;
        CHECK(MachineTest(ev->laps).type() == type);
        const Volume::Entry* e = d->volume().find("courses/US/" + ev->course);
        CHECK(e != nullptr);
        if (!e) continue;
        Bytes data = d->volume().read(*e);
        std::unique_ptr<track::Track> t = track::Track::from_course_file(data, ev->course);
        track::TrackPosition pole = t->locate(t->grid_pose(0).position);
        std::printf("  %s: test %d on %s (%.1f m a lap), pole %.2f m before the line\n", code, int(ev->laps),
                    ev->course.c_str(), double(t->length()), double(t->length() - pole.distance));
        CHECK(t->length() > MachineTest::kMarks[2] && t->length() - pole.distance < 1.0f);
        // Along the centre line from the grid, the lap timer's unwrapped distance feeding the
        // test: the goal comes on the first lap.
        track::LapTimer lt(*t);
        MachineTest mt(ev->laps);
        double time = 0;
        int goal_lap = -1;
        lt.update(t->point_at(-2.0f), time); // places the car
        for (float s = -1.0f; s < 7000.0f && !mt.done(); s += 1.0f) {
            const double before = lt.race_distance(), t0 = time;
            time += 1.0 / 60.0;
            lt.update(t->point_at(s), time);
            for (const MachineTest::Crossing& c : mt.update(before, lt.race_distance(), t->length(), t0, time))
                if (c.goal) goal_lap = lt.laps_completed();
        }
        CHECK(mt.done() && goal_lap == 0);
    }
}
