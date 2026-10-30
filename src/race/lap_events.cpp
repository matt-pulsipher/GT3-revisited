#include "race/lap_events.h"

#include "race/hud.h"
#include "race/time_text.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace gt3r::race {

// --- what a run posts -------------------------------------------------------------------------

LapEventRules lap_event_rules(HudRun run, HudPlay play) {
    LapEventRules r;
    if (play == HudPlay::Demo) return r;
    const bool live = play == HudPlay::Live;
    switch (run) {
    case HudRun::Race:
    case HudRun::Battle:
        r.lap_times = r.splits = r.race_gaps = true;
        r.session_laps = live;
        break;
    case HudRun::TimeTrial:
    case HudRun::FreeRun:
    case HudRun::FreePractice:
        r.lap_times = r.splits = true;
        r.best_lap_gaps = r.session_laps = live;
        break;
    case HudRun::License:
        r.splits = true;
        r.license_time = !live;
        break;
    case HudRun::MachineTime:
        r.lap_times = r.splits = true;
        break;
    case HudRun::MachineSpeed:
        r.splits = r.top_speed = !live;
        break;
    }
    return r;
}

int event_display_of(int car, HudPlay play, int followed) {
    if (play == HudPlay::Replay) return car == followed ? 0 : -1;
    return car == -1 ? 0 : car == -2 ? 1 : -1;
}

// --- race gaps --------------------------------------------------------------------------------

void PassingOrder::reset() { points_ = {}; }

int PassingOrder::pass(int car, int lap, int point, long long ms) {
    if (point < 0 || point >= kPoints) return 0;
    Point& p = points_[size_t(point)];
    if (lap > p.lead_lap) { // the first car through on a new lap starts the point's order
        p.lead_lap = lap;
        p.count = 1;
        p.done = 0;
        p.ms[0] = ms;
        p.car[0] = car;
        return 0;
    }
    if (lap < p.lead_lap) return p.lead_lap - lap;
    if (p.count >= kCars) return 0;
    // In time order among the passes not handed out yet (a later equal time goes after).
    int at = p.count;
    while (at > p.done && ms < p.ms[size_t(at - 1)]) --at;
    for (int i = p.count; i > at; --i) {
        p.ms[size_t(i)] = p.ms[size_t(i - 1)];
        p.car[size_t(i)] = p.car[size_t(i - 1)];
    }
    p.ms[size_t(at)] = ms;
    p.car[size_t(at)] = car;
    ++p.count;
    return 0;
}

std::vector<PassingOrder::Gap> PassingOrder::take_gaps() {
    std::vector<Gap> out;
    for (Point& p : points_) {
        for (int i = p.done; i < p.count; ++i) {
            if (i == 0) continue; // the first car through: nothing yet
            out.push_back({p.car[size_t(i)], p.ms[size_t(i)] - p.ms[0]});
            if (i == 1) out.push_back({p.car[0], p.ms[0] - p.ms[1]}); // its lead over the second
        }
        p.done = p.count;
    }
    return out;
}

// --- time trials ------------------------------------------------------------------------------

void BestLapSplits::reset() {
    best_.fill(-1);
    cur_.fill(-1);
    best_lap_ = -1;
}

void BestLapSplits::begin_run() { cur_.fill(-1); }

std::optional<long long> BestLapSplits::split_gap(int point, long long split_ms) const {
    if (point < 0 || point >= kPoints || best_[size_t(point)] < 0) return std::nullopt;
    return split_ms - best_[size_t(point)];
}

std::optional<long long> BestLapSplits::lap_gap(long long lap_ms) const {
    if (best_lap_ < 0) return std::nullopt;
    return lap_ms - best_lap_;
}

void BestLapSplits::split(int point, long long split_ms) {
    if (point >= 0 && point < kPoints) cur_[size_t(point)] = split_ms;
}

void BestLapSplits::lap_done(long long lap_ms) {
    if (best_lap_ < 0 || lap_ms < best_lap_) {
        best_ = cur_;
        best_lap_ = lap_ms;
    }
    cur_.fill(-1);
}

// --- texts ------------------------------------------------------------------------------------

std::string lap_time_text(long long ms) { return Hud::format_time(double(ms) / 1000.0); }

std::string split_text(int number, long long ms) { return "T" + std::to_string(number) + " " + lap_time_text(ms); }

std::string gap_text(long long gap_ms) {
    return std::string(1, gap_ms >= 0 ? '+' : kDash) + lap_time_text(std::llabs(gap_ms));
}

std::string lap_diff_text(int laps, const std::string& format) {
    std::string s = format;
    size_t at = s.find("%d");
    std::string n = std::to_string(std::abs(laps));
    if (at != std::string::npos) s.replace(at, 2, n);
    else s = n + " " + s;
    return std::string(1, laps >= 0 ? '+' : kDash) + s;
}

// --- the event display ------------------------------------------------------------------------

void EventDisplay::add(Message m, float duration, float delay) {
    Slot* free = nullptr;
    for (Slot& s : slots_)
        if (s.duration <= s.elapsed) {
            free = &s;
            break;
        }
    if (!free) return; // all eight busy: dropped
    float start;
    if (delay == 0.0f) { // queued after the others
        start = queue_end_;
        queue_end_ = start + duration;
    } else { // in parallel with the current group
        start = std::max(delay, 0.0f) + group_start_;
        queue_end_ = std::max(queue_end_, start + duration);
        group_start_ = start;
    }
    free->m = std::move(m);
    free->duration = duration;
    free->elapsed = -start;
    free->used = true;
}

void EventDisplay::queue(Message m, float duration) { add(std::move(m), duration, 0.0f); }

void EventDisplay::parallel(Message m, float duration) { add(std::move(m), duration, -duration); }

void EventDisplay::after(Message m, float delay, float duration) { add(std::move(m), duration, delay); }

void EventDisplay::gap(long long gap_ms, bool picture, int car) {
    Message m;
    m.text = gap_text(gap_ms);
    if (gap_ms >= 0) m.r = 240, m.g = 0, m.b = 0;
    else m.r = 128, m.g = 128, m.b = 255;
    m.dy = 25;
    if (picture) {
        m.dx = gap_ms >= 0 ? 37.0f : -37.0f;
        icon_.set = true;
        icon_.car = car;
        icon_.dx = gap_ms >= 0 ? -62.0f : 62.0f;
        icon_.dy = 20;
        icon_.duration = kDuration;
        icon_.elapsed = -group_start_; // starts with the group the gap joins
        icon_.blink = 72;
    }
    parallel(std::move(m));
}

void EventDisplay::lap_down(std::string text) {
    Message m;
    m.text = std::move(text);
    m.r = 240, m.g = 0, m.b = 0;
    m.dy = 25;
    parallel(std::move(m));
}

void EventDisplay::tick() {
    for (Slot& s : slots_)
        if (s.elapsed < s.duration) s.elapsed += kFrame;
    group_start_ = std::max(group_start_ - kFrame, 0.0f);
    queue_end_ = std::max(queue_end_ - kFrame, 0.0f);
    if (icon_.elapsed < icon_.duration) icon_.elapsed += kFrame;
    if (icon_.blink > 0) --icon_.blink;
}

void EventDisplay::clear() { *this = EventDisplay{}; }

std::vector<const EventDisplay::Message*> EventDisplay::visible() const {
    std::vector<const Message*> out;
    for (const Slot& s : slots_)
        if (s.used && s.duration > 0 && s.elapsed >= 0 && s.elapsed <= s.duration) out.push_back(&s.m);
    return out;
}

EventDisplay::Icon EventDisplay::icon() const {
    const IconState& i = icon_;
    if (!i.set || i.duration <= 0 || i.elapsed < 0 || i.elapsed > i.duration || ((i.blink / 12) & 1)) return {};
    return {true, i.car, i.dx, i.dy};
}

} // namespace gt3r::race
