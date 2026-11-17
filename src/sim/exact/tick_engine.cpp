// Phase 7's engine and clutch: torque, turbo, the clutch link and the engine speed; see tick.h.

#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

constexpr F kOne = F::raw(0x3F800000);
constexpr F kHalf = F::raw(0x3F000000);
constexpr F kTwo = F::raw(0x40000000);
constexpr F kRpmToW = F::raw(0x3DD67750); // 2 pi / 60
constexpr F kKmh10 = F::raw(0x4031C71D);  // 10 km/h in m/s
// An electric mod car's drive (Car::ev; GT3 has none): its regen fades out below 5 km/h and over the accelerator's
// first 10 %, its drive over the last 2 % below its top speed.
constexpr F kKmh5 = F::raw(0x3FB1C71D);        // 5 km/h in m/s
constexpr F kRegenPedal = F::raw(0x41200000);  // 10
constexpr F kLimiterBand = F::raw(0x3CA3D70A); // 0.02

// The pedal style that selects the clutch and gear behaviour: a pad-driven car's own, 0 for others.
u8 style_of(const Car& c) { return c.control != 1 && c.driver_kind == 0 ? c.pedal_style : 0; }

F clamp01(F x) {
    if (x < F()) return F();
    return kOne < x ? kOne : x;
}

F abs_f(F x) { return x < F() ? -x : x; }

// An electric car's one-pedal stop: below 5 km/h with the accelerator up, the friction brakes take over what the motor's
// regen loses as it fades (the same torque at the wheels, on all four), so the car stops and stays (stop_hold's braked
// stop).
void electric_hold(Car& c) {
    if (!(c.throttle_pedal == F())) return;
    const F v = abs_f(c.speed);
    if (!(v < kKmh5)) return;
    const F quarter = F::raw(0x3E800000); // (each wheel's share)
    const F per_wheel = c.ev.regen_torque * abs_f(c.p.d.ratio[c.gear]) * quarter * (kOne - v / kKmh5);
    for (Drive& t : c.t) t.brake_torque = t.brake_torque + per_wheel;
}

// The share of the regen the driven tyres can take: all of it up to each driven tyre's optimum braking slip (the ABS
// target, physics §4.5), none at twice it. ABS acts on the brake pressure only, so without this a regen stronger than
// the driven axle's grip (a low-grip surface, a lift in a corner) would drag the driven wheels down to a crawl while
// the car slides on; an EV's stability control eases the regen off the same way.
F regen_grip(const Car& c) {
    const u8 l = c.p.d.layout; // the driven axles, as driven_axle_speed reads them
    const bool front = l == 1 || l == 2 || l == 3 || l == 7, rear = l != 1 && l != 2;
    F f = kOne;
    for (size_t i = 0; i < 4; ++i) {
        if (!(i < 2 ? front : rear)) continue;
        const Wheel& w = c.w[i];
        const F best = tire_record(c.p, int(i), w.surface).slip_brake * c.p.brake_slip_scale[i >> 1];
        if (!(F() < best)) continue;
        const F g = clamp01(kTwo - w.slip_ratio / best);
        if (g < f) f = g;
    }
    return f;
}

// An electric car's motor torque for throttle x, at the motor's own speed w (signed: below 0 while the car rolls against
// the gear; the clutch's c.omega stops at 0): the curve at |w| (a motor has no engine condition), faded out over the last
// 2 % below the top speed (the limiter); the regen the lifted accelerator asks for (all of it with the pedal up, none
// from 10 %) against the motor's turning, capped by its power, faded out below the motor's speed at 5 km/h (so it never
// holds the driven wheels still or turns them backwards) and eased off as they slip (regen_grip); the motor's drag. None
// while the race or a stop holds the car. While a change of direction waits (ev.waiting: reverse asked at speed, or the
// accelerator while rolling backwards in reverse), the pedal that asked for it brakes like a lifted accelerator, so the
// car slows to the change instead of speeding up the way it is going.
F electric_output(Car& c, F x) {
    const Engine& e = c.p.e;
    if (c.race_hold != 0 || c.hold != 0) return F();
    if (c.ev.waiting != 0) x = F();
    const F w = driven_axle_speed(c) * c.p.d.ratio[c.gear];
    const F aw = abs_f(w);
    const F top = ee::int_to_f(e.rev_limit) * kRpmToW;
    const F fade = clamp01((top - aw) / (top * kLimiterBand));
    c.rev_limiter = fade < kOne ? 1 : 0;
    const F drag = e.friction * w;
    F t = ((x * (lookup_bisect(e.torque, aw) + abs_f(drag))) * fade) - drag;
    const F lift = c.ev.waiting != 0 ? kOne : clamp01(kOne - c.throttle_pedal * kRegenPedal);
    if (F() < lift && F() < aw) {
        F regen = c.ev.regen_torque;
        if (c.ev.regen_power / aw < regen) regen = c.ev.regen_power / aw;
        regen = (regen * lift) * clamp01(aw / (kKmh5 * abs_f(c.p.d.gear_speed[1])));
        regen = regen * regen_grip(c);
        t = F() < w ? t - regen : t + regen;
    }
    return power_cap(e, t, aw);
}

} // namespace

F driven_axle_speed(const Car& c) {
    const F wf = c.axle_speed[0], wr = c.axle_speed[1];
    switch (c.p.d.layout) {
    case 0:
    case 4:
    case 5:
    case 6: return wr;
    case 1:
    case 2: return wf;
    case 3: return (wf + wr) * kHalf;
    case 7: return wr + c.p.d.awd_k * (wf - wr);
    default: return F();
    }
}

F wheel_side_speed(const Car& c) {
    F w = driven_axle_speed(c) * c.p.d.ratio[c.gear];
    if (w <= F()) w = F();
    return w;
}

void engine_rpm(Car& c) {
    s32 n = ee::to_int((c.omega * F::raw(0x42700000)) / F::raw(0x40C90FDB));
    const s32 floor = s32(c.p.e.idle) - 1;
    if (n < floor) n = floor;
    c.rpm = s16(n);
}

void clutch_engage(Car& c) {
    if (c.ev.on) { // the motor is never disconnected
        c.clutch = 1;
        return;
    }
    if (c.clutch == 0) {
        if (!(c.clutch_req == F())) {
            const F v = c.speed;
            const bool moving = (kKmh10 < v && c.gear != 0) || (v < -kKmh10 && c.gear == 0);
            if (moving) {
                if (F::raw(0x3E800000) < c.throttle) c.clutch = 2;
            } else if (s32(c.p.d.launch_rpm) < s32(c.rpm)) c.clutch = 2;
            else c.clutch_req = F();
        }
    } else if (c.clutch == 1) {
        if (c.clutch_req == F()) c.clutch = 0;
    }
    if (u32(c.clutch) - 2u >= 2u) return;
    const u8 style = style_of(c);
    c.clutch_engine = c.omega;
    const F ws = wheel_side_speed(c);
    c.clutch_wheel = ws;
    if (style == 3 || style == 6) return;
    if (c.clutch == 3) c.throttle = ws < c.clutch_engine ? F() : kOne; // the shift cut / blip
}

void brakes_and_clutch(Car& c) {
    c.clutch_engine = F();
    c.clutch_wheel = F();
    clutch_engage(c);
    for (size_t i = 0; i < 4; ++i) c.t[i].brake_torque = c.w[i].brake * c.p.brake_torque[i >> 1];
    const F hb = c.handbrake * c.p.handbrake_torque;
    for (size_t i = 2; i < 4; ++i) c.t[i].brake_torque = c.t[i].brake_torque + hb;
    if (c.ev.on) electric_hold(c);
    for (size_t i = 0; i < 4; ++i) {
        u8 f = c.t[i].brake_torque == F() ? 0 : 2;
        if (c.w[i].load == F()) f |= 1;
        c.w[i].flags = f;
    }
}

F turbo_stage(const Engine& e, s16 n, int i) {
    const size_t k = size_t(i);
    const s32 knee = e.turbo_knee[k];
    if (n < knee) {
        switch (e.turbo_shape[k]) {
        case 0: {
            const F x = ee::int_to_f(n - knee);
            return e.turbo_peak[k] - e.turbo_coef[k] * x * x;
        }
        case 1: {
            const F x = ee::int_to_f(n);
            return e.turbo_coef[k] * x * x;
        }
        case 2: return (e.turbo_peak[k] * ee::int_to_f(n)) / ee::int_to_f(knee);
        default: return F();
        }
    }
    if (!(n < 3 * knee)) return F();
    const F x = ee::int_to_f(n - knee);
    return e.turbo_peak[k] - e.turbo_coef[k] * x * x * F::raw(0x3E800000);
}

F boost_raw(const Engine& e, s16 n0, s16 n1) {
    const s16 n[2] = {n0, n1};
    F sum{};
    for (int i = 0; i < 2; ++i)
        if (!(e.turbo_peak[size_t(i)] == F())) sum = sum + turbo_stage(e, n[i], i);
    return sum + kOne;
}

F boost(const Engine& e, s16 n0, s16 n1) {
    F b = boost_raw(e, n0, n1);
    const F cap = e.boost_max + kOne;
    if (cap < b) b = cap;
    return b;
}

F power_cap(const Engine& e, F t, F w) {
    if (e.power_cap_on == 0) return t;
    const F p = t * w;
    const F soft = e.power_cap_soft, cap = e.power_cap;
    if (p <= cap * (kOne - soft)) return t;
    F lim = cap * (kOne - soft * kHalf);
    if (!(cap <= p)) {
        const F d = cap - p;
        lim = lim - (d * d) / ((soft + soft) * cap);
    }
    lim = lim / w;
    return lim < t ? lim : t;
}

F engine_output(Car& c, F x) {
    if (c.ev.on) return electric_output(c, x);
    const Engine& e = c.p.e;
    if (c.rev_limiter == 0) {
        if (!(s32(c.rpm) < s32(e.rev_limit))) c.rev_limiter = 1;
    } else if (s32(c.rpm) < s32(e.rev_limit) - 500) c.rev_limiter = 0;
    F t{};
    if (c.rev_limiter == 0) t = x * (c.torque_wot + c.torque_friction);
    t = t - c.torque_friction;
    return power_cap(e, t, c.omega);
}

F engine_torque(Car& c, F x) {
    const Engine& e = c.p.e;
    if (e.boost_max == F()) {
        c.turbo_load = x;
        c.turbo_spool = 0;
        c.turbo_load_prev = x;
        return engine_output(c, x);
    }
    const F dt = c.dt;
    const F b = boost(e, c.rpm, c.rpm);
    c.turbo_load_prev = c.turbo_load;
    const F target = (c.turbo_load * ee::int_to_f(c.rpm)) / b;
    for (size_t i = 0; i < 2; ++i) {
        if (e.turbo_peak[i] == F()) continue;
        const F d = ((target - ee::int_to_f(c.turbo_lag[i])) * e.turbo_rate[i]) * dt;
        const u32 lag = u32(u16(c.turbo_lag[i])) + u32(ee::to_int(d));
        c.turbo_lag[i] = s16(u16(lag));
        if (s32(lag << 16) < 0) c.turbo_lag[i] = 0;
    }
    const F load = x * boost(e, c.turbo_lag[0], c.turbo_lag[1]);
    F k = kHalf - (ee::int_to_f(c.rpm) * F::raw(0x39800000)) * F::raw(0x3E800000);
    if (k < F::raw(0x3D800000)) k = F::raw(0x3D800000);
    c.blowoff = F();
    c.intake = (c.intake + (load + (kOne - x) * k)) * kHalf;
    if (c.blowoff_state == 0) {
        if (x < F::raw(0x3F400000)) {
            F l = c.turbo_load - F::raw(0x3F4CCCCD);
            if (l < c.blowoff) l = c.blowoff;
            c.blowoff_state = 1;
            c.blowoff = l * F::raw(0x3B800000);
        }
    } else if (c.blowoff_state == 1) {
        if (F::raw(0x3F666666) < x) c.blowoff_state = 0;
    }
    c.turbo_load = load;
    F xe = kOne;
    if (!(b <= load)) xe = load / b;
    c.turbo_pitch = u16(c.turbo_lag[0]);
    if (s32(u16(c.turbo_lag[0])) < s32(c.turbo_lag[1])) c.turbo_pitch = u16(c.turbo_lag[1]);
    const F t = engine_output(c, xe);
    if (!(F() < e.boost_max)) return t;
    const F l = c.turbo_load_prev - F::raw(0x3F400000);
    if (l <= F() || t <= F()) {
        c.turbo_spool = 1;
        return t;
    }
    F q = F::raw(0x44130000) <= t ? l : (l * t) * F::raw(0x3ADEE95C); // 588 N m, 1 / 588
    q = q * q;
    s32 n = ee::to_int(q * F::raw(0x43000000));
    if (!(n < 128)) n = 127;
    if (!(0 < n)) n = 1;
    c.turbo_spool = u8(n);
    return t;
}

void engine_speed(Car& c) {
    const Engine& e = c.p.e;
    const F dt = c.dt;
    if (c.clutch == 0) {
        const F idle = ee::int_to_f(e.idle) * kRpmToW;
        const F t = engine_torque(c, c.throttle);
        const F w = c.omega + (t * c.p.d.inv_engine_inertia) * dt;
        c.omega = w;
        if (w < idle) c.omega = idle;
        engine_rpm(c);
        return;
    }
    const F ws = wheel_side_speed(c);
    F w;
    u8 s = c.clutch;
    if (s == 1) {
        w = ws;
        c.omega = ws;
    } else if (u32(s) - 2u < 2u) {
        const u8 style = style_of(c);
        if ((style == 3 || style == 6) && c.clutch_req == kOne) {
            // the pedal clutch pressed home: both axles to the engine's speed
            const F q = c.omega / c.p.d.ratio[c.gear];
            for (size_t a = 0; a < 2; ++a) {
                c.axle_speed[a] = q;
                const F v = c.p.tire_radius[a] * q;
                c.w[2 * a].rim_speed = v;
                c.w[2 * a + 1].rim_speed = v;
            }
            s = 1;
            c.clutch = 1;
            w = c.omega;
        } else {
            const F e0 = c.clutch_engine, w0 = c.clutch_wheel;
            w = c.omega;
            if ((e0 <= w0 && ws <= w) || (w0 <= e0 && w <= ws)) { // the speeds crossed: lock
                c.clutch = 1;
                w = ws;
                c.shift_lockout = pal_rate(c) ? 10 : 12;
                s = 1;
                c.omega = ws;
            }
        }
        if (s == 3 && c.clutch_wheel <= ee::int_to_f(e.idle) * kRpmToW) {
            c.clutch = 0;
            s = 0;
        }
        if (s == 2 && F::raw(0x3F400000) < c.brake_pedal) {
            if (w < ee::int_to_f(s32(e.idle) + 500) * kRpmToW) c.clutch = 0;
            else if (c.speed < kKmh10 && -kKmh10 < c.speed) c.clutch = 0;
        }
    } else w = c.omega;
    if (w < F()) c.omega = F();
    else {
        const F cap = ee::int_to_f(s32(e.rev_limit) + 10) * kRpmToW;
        if (cap <= w) c.omega = cap;
    }
    engine_rpm(c);
}

} // namespace gt3r::sim::exact
