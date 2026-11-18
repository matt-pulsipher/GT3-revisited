// The pit lane's state machine, the AI driver's line and kind switches, the end of a run and the step's per-car loop after
// phase 13; see pit_lane.h.

#include "sim/exact/pit_lane.h"
#include "sim/exact/build_math.h"

#include "sim/exact/start.h"

namespace gt3r::sim::exact {

namespace {

const F kZero{}, kHalf = F::raw(0x3F000000);
const F kFast = F::raw(0x3FB1C71D), kFastNeg = F::raw(0xBFB1C71D); // +-5 km/h
const F kStopSpeed = F::raw(0x41DE38E4);                            // 100 km/h
const F kBoxApproach = F::raw(0x41C80000);                          // 25 m before the box
const F kSlowCorner = F::raw(0x3F4CCCCD);                           // 0.8

// The stop line's point k (from the end when negative); its point k.
F stop_point(AiPlan& plan, s32 k) { return plan.stop_distance(k < 0 ? k + plan.stop_count() : k); }

} // namespace

bool pit_exit(Car& c, AiPlan& plan) {
    c.ai.state = 0;
    if (c.control == 0) {
        if (!plan.request_line(c, 7, false)) return false;
        c.driver_kind = 0;
        return true;
    }
    const u8 kind = c.driver_kind;
    if (kind == 2 || kind == 4 || kind == 3) {
        if (!plan.request_line(c, 6, false)) return false;
    }
    c.driver_kind = 0;
    return true;
}

bool pit_entry(Car& c, AiPlan& plan) {
    if (!plan.request_line(c, 3, true)) return false;
    c.driver_kind = 2;
    c.x535 = u8(c.x535 | 8);
    return true;
}

bool pit_stop(Car& c, AiPlan& plan) {
    if (!plan.request_line(c, 4, true)) return false;
    c.driver_kind = 3;
    return true;
}

bool pit_leave(Car& c, AiPlan& plan) {
    if (!plan.request_line(c, 3, true)) return false;
    c.driver_kind = 4;
    return true;
}

bool drive_on(Car& c, AiPlan& plan) {
    const u8 kind = c.driver_kind;
    bool line = kind == 2 || kind == 4 || kind == 3;
    if (c.control == 0 && kind == 0) line = true;
    if (line && !plan.request_line(c, 6, false)) return false;
    c.driver_kind = 1;
    return true;
}

bool stop_on_line(Car& c, AiPlan& plan) {
    if (!plan.request_line(c, 6, false)) return false;
    c.driver_kind = 6;
    return true;
}

bool stop_lap(Car& c, AiPlan& plan) {
    if (!plan.request_line(c, 6, false)) return false;
    c.driver_kind = 5;
    return true;
}

bool stop_straight(Car& c) {
    c.driver_kind = 7;
    return true;
}

void slow_down(Driver& d, Program rule) {
    if (!gt3_program(rule)) { // (Rule::SlowDown, the later builds: the multiplier x 0.8, at least a half)
        const F k = F::raw(rule >= Program::ConceptGenevaEu ? 0x3F4CCCCC : 0x3F4CCCCD);
        const F t = d.throttle_scale * k;
        d.throttle_scale = t < kHalf ? kHalf : t;
        d.speed_scale = d.speed_scale * k;
        return;
    }
    const F s = d.speed_scale * kSlowCorner;
    d.throttle_scale = kHalf;
    d.speed_scale = s;
}

bool end_of_run_head(Car& c, int& mode) {
    if (mode == 4) mode = (c.x535 & 0x10) ? 2 : 0;
    if (c.finished == 0) {
        c.finished = 1;
        if (u8(c.driver_kind - 2) < 3) return false;
    }
    return true;
}

void end_of_run(Car& c, int mode, const PitInputs& in, AiPlan& plan) {
    if (end_of_run_head(c, mode)) end_of_run_drive(c, mode, in, plan);
}

void end_of_run_drive(Car& c, int mode, const PitInputs& in, AiPlan& plan) {
    if (c.control == 0) event_overrides(c, *in.rules, in.config, c.p.drive_class); // empty
    if (mode == 1) stop_on_line(c, plan);
    else if (mode < 2) drive_on(c, plan);
    else if (mode == 2) stop_straight(c);
    else {
        if (mode == 3) slow_down(c.ai, c.behaviour.rule(Rule::SlowDown));
        drive_on(c, plan);
    }
}

bool race_over(const Car& c, const PitInputs& in) {
    if (c.lap_base != 0) return false;
    if (const u32 limit = u32(in.flow->time_limit) * 60000u; limit != 0 && !(in.clock / 3u < limit)) return true;
    s32 laps = in.flow->laps;
    if (laps == 0) return false;
    if (c.f3e4 < in.lap * kHalf) laps = laps + 1;
    return !(s32(c.laps) < laps);
}

void pit_lane(Car& c, F s, F old, bool hit, bool hit_43c, const PitInputs& in, AiPlan& plan, std::vector<PitEvent>& events) {
    if (!in.pit_lines) return;
    const RaceFlowInputs& msg = *in.flow;
    const F lap = in.lap, half = lap * kHalf;
    if (s < half) s = s + lap;
    if (old < half) old = old + lap;
    if (c.hold != 0) hud_message(c, msg, 11);

    if (c.x535 & 1) { // the wrong way: a pad car on the pit surface between the stop line's points 3 and n - 2
        if (u8(u8(c.control) - 1) < 2) return;
        if (c.driver_kind != 0 || !(c.x535 & 0x10)) return;
        if (!(stop_point(plan, 3) < s)) return;
        if (!(s < stop_point(plan, -2))) return;
        const F p = stop_point(plan, -3);
        if (p < old && s <= p) {
            pit_leave(c, plan);
            hud_message(c, msg, 12);
            return;
        }
        const F v = c.speed, u = c.lat_speed;
        // (2002 Tokyo-Geneva's +-5 km/h two units smaller: Europe)
        const F kFast = folded(c.behaviour, sim::exact::kFast.bits, 0x3FB1C71B), kFastNeg = folded(c.behaviour, sim::exact::kFastNeg.bits, 0xBFB1C71B);
        if (!(kFast < v || v < kFastNeg || kFast < u || u < kFastNeg)) return;
        if (hit) return;
        hud_message(c, msg, 2);
        return;
    }

    // What a stop would service: worn tyres, a wheel's +0x28 byte, the conductor's answer.
    u32 service = 0;
    for (const Wheel& w : c.w) {
        if (!(w.wear == in.wear.fresh)) service |= 1;
        if (w.x28 != 0) service |= 2;
    }
    if (in.service_always) service |= 4;
    const bool over = race_over(c, in);

    const u8 kind = c.driver_kind;
    if (kind == 3) { // the stop line
        if (c.hold != 0) {
            if (c.hold == (pal_rate(c) ? 50 : 60)) tyres_start(c, in.wear);
            return;
        }
        const F box = plan.stop_distance(c.pit_box);
        if (service != 0 && old < box && box <= s) {
            standing_start(c);
            c.hold = pal_rate(c) ? 250 : 300;
            hud_message(c, msg, 11);
            return;
        }
        if (box + kHalf <= s) {
            pit_leave(c, plan);
            c.x535 = u8(c.x535 & 0xFB);
        }
        return;
    }
    if (kind == 2) { // in the pit lane
        const F p1 = stop_point(plan, 1);
        if (p1 <= s && p1 < lap && !(c.x535 & 0x10)) {
            pit_exit(c, plan);
            hud_message(c, msg, 0);
            return;
        }
        if (old < p1 && p1 <= s) events.push_back({1, c.index});
        const F box = plan.stop_distance(c.pit_box) - kBoxApproach;
        if (!(old < box && box <= s)) {
            hud_message(c, msg, 10);
            return;
        }
        if (over) {
            pit_leave(c, plan);
            hud_message(c, msg, 8);
        } else if (service == 0) {
            pit_leave(c, plan);
            hud_message(c, msg, 9);
        } else if (folded(c.behaviour, kStopSpeed.bits, 0x41DE38E2) <= c.speed) { // (Europe's 100 km/h)
            pit_leave(c, plan);
            hud_message(c, msg, 7);
        } else {
            pit_stop(c, plan);
        }
        return;
    }
    if (kind == 4) { // leaving
        const F p1 = stop_point(plan, 1);
        if (p1 <= s && s < lap && !(c.x535 & 0x10)) {
            pit_exit(c, plan);
            hud_message(c, msg, 0);
        }
        F exit, warn;
        if (c.control != 0 || c.finished != 0) exit = stop_point(plan, -1), warn = stop_point(plan, -2);
        else exit = stop_point(plan, -2), warn = stop_point(plan, -3);
        if (lap + lap * kHalf <= exit) exit = exit - lap;
        if (old < exit && exit <= s) {
            if (c.finished != 0) end_of_run(c, 3, in, plan);
            else {
                pit_exit(c, plan);
                if (!over) events.push_back({6, c.index});
            }
            c.x535 = u8(c.x535 & 0xF7);
            return;
        }
        if (warn <= s) {
            if (!over) hud_message(c, msg, c.full_throttle != 0 ? 5 : 4);
        } else if (c.finished == 0) {
            hud_message(c, msg, 10);
        }
        return;
    }
    // Racing (kinds 0, 1, 5 and up).
    if (c.control == 0 && kind != 1) { // a pad car: into the pit lane at the stop line's point 1 on the pit surface
        if (!(c.x535 & 0x10)) return;
        const F p1 = stop_point(plan, 1);
        if (!(old < p1 && p1 <= s)) return;
        pit_entry(c, plan);
        if (kZero < c.speed) events.push_back({1, c.index});
        return;
    }
    if ((c.x535 & 0x10) && hit_43c && s < lap) { // hit on the pit surface: into the pit lane, turning round
        pit_entry(c, plan);
        c.ai.state = 2;
        return;
    }
    if (!(c.x535 & 8) || over) return; // a pit request: in at the stop line's point 0
    const F p0 = stop_point(plan, 0);
    if (old < p0 && p0 <= s) pit_entry(c, plan);
}

void tick_flow(std::span<Car> cars, FlowStep& f, AiPlan& plan) {
    f.pit.flow = &f.in;
    RaceFlowHooks hooks;
    hooks.rolling_handback = [&](Car& c) { pit_exit(c, plan); };
    hooks.finish_drive = [&](Car& c, int, int mode) { end_of_run_drive(c, mode, f.pit, plan); };
    hooks.ai_takeover = [&](Car& c, int) { plan.replan(c); };
    hooks.drive_on = [&](Car& c, int) { drive_on(c, plan); };
    hooks.stop_drive = [&](Car& c, int) { stop_lap(c, plan); };
    hooks.ghost_step = f.ghost_step;
    hooks.save_body = f.save_body, hooks.restore_body = f.restore_body;
    if (f.mode_start) race_mode_start(cars, f.in, *f.events, hooks);
    for (size_t k = f.first; k < f.order.size() && k < cars.size(); ++k) {
        const int i = int(f.order[k]);
        hooks.pit_lane = [&, i](Car& c, int, F s, F old) {
            f.pit.clock = f.flow->clock;
            const size_t n = f.pit_events->size();
            pit_lane(c, s, old, f.hit[size_t(i)] != 0, f.hit_43c[size_t(i)] != 0, f.pit, plan, *f.pit_events);
            for (size_t q = n; q < f.pit_events->size(); ++q) { // their notes
                RaceEvent e;
                e.type = 18, e.car = (*f.pit_events)[q].car, e.arg = 2, e.value = (*f.pit_events)[q].type;
                f.events->push_back(e);
            }
        };
        race_flow_car(cars, f.order, i, f.hit[size_t(i)] != 0, f.in, *f.flow, *f.events, hooks);
    }
}

} // namespace gt3r::sim::exact
