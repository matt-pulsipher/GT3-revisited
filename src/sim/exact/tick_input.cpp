// Phase 3, the driver input of a car on the pad path; see tick.h.

#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

const F kZero{}, kOne = F::raw(0x3F800000), kHalf = F::raw(0x3F000000), kQuarter = F::raw(0x3E800000);
const F kStick = F::raw(0x39800000);           // 1 / 4096
const F kHandbrakeClutch = F::raw(0x3F4CCCCD); // $gp 0x350408 (0.8)

// An analog axis: x + c (1 - x) once it moves.
F analog(const PadConstants& k, F x) { return x + k.analog_jump * (kOne - x); }

// (the later builds' gearbox kinds, D +0x12: 1 and 2 are without steps)
bool stepless(const Driveline& d) { return u8(d.kind - 1) < 2; }

} // namespace

F pedal_curve(const PedalCurve& k, F x) {
    F ax = x;
    if (x < kZero) ax = -x;
    F y = ax;
    if (k.type == 1) {
        const F d = ax - k.a;
        y = k.c * d * d + k.b;
    } else if (k.type == 2) {
        y = k.c * sqrt(ax - k.a) + k.b;
    }
    if (x < kZero) y = -y;
    return y;
}

void slew(F& v, F target, F step) {
    F x = v;
    if (x < target) {
        x = x + step;
        if (target < x) x = target;
    } else if (target < x) {
        x = x - step;
        if (x < target) x = target;
    }
    v = x;
}

void steer_follow(Car& c, const PadConstants& k, F target, F rate_out, F rate_back) {
    const F dt = c.dt, d = c.steer_cmd;
    F w = c.steer_rate;
    const F damp = w * k.k2;
    F acc = (target - d) * k.k1 - damp;
    F lim = rate_out;
    if ((kZero < d && target < d) || (d < kZero && d < target)) { // returning towards the centre
        lim = rate_back;
        acc = acc - d * k.k3;
    }
    w = w + acc * dt;
    if (lim < w) w = lim;
    else if (w < -lim) w = -lim;
    const F next = d + w * dt;
    if ((d <= target && target <= next) || (target <= d && next <= target)) {
        c.steer_cmd = target;
        c.steer_rate = kZero;
        return;
    }
    c.steer_rate = w;
    c.steer_cmd = next;
}

void pad_clutch(Car& c, const PadFrame& f, u8 style) {
    if (c.race_hold != 0 || c.hold != 0 || c.driver_kind == 8) {
        c.clutch_req = kZero;
        return;
    }
    if (style == 3) { // the brake axis is the clutch pedal
        const F b = c.brake_pedal;
        c.brake_pedal = kZero;
        c.clutch_req = kOne - b;
        if (c.clutch_req == c.brake_pedal) return;
        c.reverse_req = 0;
        c.shift_req = 0;
        return;
    }
    if (style == 6 && (f.shift != 0 || s16(f.back) != 0)) {
        c.clutch_req = kZero;
        return;
    }
    const Program rule = c.behaviour.rule(Rule::PadClutch);
    const bool later_clutch = !gt3_program(rule), geneva = rule >= Program::ConceptGenevaEu;
    const F k08 = geneva ? F::raw(0x3F4CCCCC) : kHandbrakeClutch; // (2002 Tokyo-Geneva's constant is its own)
    if (c.p.d.driven_axle == 1 && k08 < c.handbrake) {
        c.clutch_req = kZero;
        return;
    }
    // (Rule::PadClutch, the later builds: a stepless gearbox opens the clutch standing on the third axis)
    if (later_clutch && stepless(c.p.d) && k08 < c.handbrake &&
        sqrt(c.speed * c.speed + c.lat_speed * c.lat_speed) < F::raw(geneva ? 0x3E8E38E3 : 0x3E8E38E4)) {
        c.clutch_req = kZero;
        return;
    }
    if (c.clutch == 0) {
        c.clutch_req = c.brake_pedal < kQuarter ? kOne : kZero;
        return;
    }
    c.clutch_req = s32(c.rpm) < s32(c.p.e.idle) ? kZero : kOne;
}

namespace {

// The later builds' 0x1F0020 (Concept 2001 Tokyo): the steering command in percent of 15 degrees, within +-100.
s32 hud_steer(F steer, bool geneva) {
    F x = steer * F::raw(geneva ? 0x43BEFC68 : 0x43BEFC66);
    if (F::raw(0x42C80000) < x) x = F::raw(0x42C80000);
    else if (x < F::raw(0xC2C80000)) x = F::raw(0xC2C80000);
    return ee::to_int(x);
}

} // namespace

void pad_controls(Car& c, const PadConstants& k, const PadFrame& f, u8 course_kind) {
    F v = c.speed;
    const u8 cls0 = k.surface_class[c.w[0].surface & 15], cls1 = k.surface_class[c.w[1].surface & 15];
    const F m = (c.w[0].slip_dir + c.w[1].slip_dir) * kHalf; // the front wheels' mean velocity direction
    const u8 cls = cls0 == cls1 ? cls0 : 0;
    const F dt = c.dt;
    int rate = course_kind == 1 ? 2 : course_kind == 2 ? 1 : 0; // the rate class
    if (v < kZero) v = kZero;
    const Table<6>* curve;
    if (f.kind == 4) curve = rate == 1 ? &c.p.steer_c : &c.p.steer_a;
    else {
        if (rate == 1) rate = cls;
        curve = rate == 1 ? &c.p.steer_c : &c.p.steer_b;
    }
    F hi = lookup(*curve, v), lo;
    bool extend = true; // the slip-angle extension
    if (f.kind == 4) {
        const s8 type = c.wheel_type;
        if (type >= 1 && type <= 3) { // the steering wheel's least lock
            if (type == 3) extend = false;
            const F least = k.wheel_lock[size_t((type == 2 ? 2 : 0) + (rate == 1 ? 1 : 0))];
            if (hi < least) hi = least;
        } else if (type == 4) {
            extend = false;
        }
    }
    lo = -hi;
    if (extend) {
        if (lo <= m && m <= hi) c.countersteer = kZero;
        else if (kZero < m) c.countersteer = hi = m;
        else if (m < kZero) c.countersteer = lo = m;
    }
    if (c.p.max_lock < hi) hi = c.p.max_lock;
    if (lo < -c.p.max_lock) lo = -c.p.max_lock;
    c.x420 = kOne;

    // Steering.
    if (!(f.flags & 1)) { // digital
        c.steer_class = 1;
        c.lock_hi = hi;
        c.lock_lo = lo;
        const F target = f.steer > 0 ? hi : f.steer < 0 ? lo : kZero;
        steer_follow(c, k, target, k.rate_out[size_t(rate)], k.rate_back[size_t(rate)]);
    } else if (f.kind == 4) { // the steering wheel: its position is the command
        c.steer_class = 0;
        const F s = ee::int_to_f(f.steer) * kStick;
        c.wheel_pos_prev = c.wheel_pos;
        c.wheel_pos = u16(f.steer);
        const s8 type = c.wheel_type;
        F a = kZero;
        if (type >= 0 && type < 4) {
            slew(c.lock_hi, hi, k.wheel_slew * dt);
            slew(c.lock_lo, lo, k.wheel_slew * dt);
            a = kZero <= s ? c.lock_hi * s : -c.lock_lo * s;
        } else if (type == 4) {
            a = c.p.max_lock * s;
        }
        c.steer_cmd = a;
    } else if (f.kind == 2 || f.kind == 3) {
        c.lock_hi = hi;
        c.lock_lo = lo;
        const F s = ee::int_to_f(f.steer) * kStick;
        const F target = kZero <= s ? hi * s : -lo * s;
        if (f.steer != 0) c.steer_class = f.kind == 3 ? 1 : 2;
        if (c.steer_class == 1) steer_follow(c, k, target, k.rate_out[size_t(rate)], k.rate_back[size_t(rate)]);
        else steer_follow(c, k, target, k.rate[size_t(rate)], k.rate[size_t(rate)]);
    } // other analog kinds leave the command as it is
    const Program rule = c.behaviour.rule(Rule::PadControls);
    const bool later = !gt3_program(rule), geneva = rule >= Program::ConceptGenevaEu;
    // (the later builds: the HUD's steering percentage from the command, for the followed kinds)
    if (later && (c.steer_class == 1 || c.steer_class == 2)) c.steer_pct = s8(hud_steer(c.steer_cmd, geneva));
    const F ext = c.countersteer;
    if (!(ext == kZero)) {
        const F q = c.steer_cmd / ext;
        c.countersteer = q;
        if (q < kZero) c.countersteer = kZero;
        else if (kOne < q) c.countersteer = kOne;
    }

    // Throttle and the back axis.
    u8 style = 0;
    if (c.control != 1 && c.driver_kind == 0) style = c.pedal_style;
    bool th_analog = true, back_analog = true;
    u32 th = f.throttle, back = f.back;
    if (!(f.flags & 2)) {
        th = (th << 12) & 0xFFFF;
        th_analog = false;
    }
    if (!(f.flags & 8)) {
        back = (back << 12) & 0xFFFF;
        back_analog = false;
    }
    if (c.hold != 0) back = 0;
    if (style <= 2) { // the back axis replaces a smaller throttle
        if (back < th) back = 0;
        else {
            th_analog = back_analog;
            th = back;
        }
    } else if (style == 5) {
        back = 0;
    }
    c.reverse_req = back != 0;
    F x = kZero;
    u8 state = 0;
    bool reverse_held = false; // a released back axis keeps the reverse request while the throttle falls
    if (!th_analog) {
        x = c.throttle_pedal;
        if (th != 0) {
            x = x + k.throttle_up;
            if (kOne < x) x = kOne;
            c.throttle_state = state = back != 0 ? 2 : 1;
        } else {
            x = x - k.throttle_down;
            if (x < kZero) {
                c.throttle_state = 0;
                x = kZero;
            }
            state = c.throttle_state;
            reverse_held = true;
        }
    } else if (th != 0) {
        const F p = ee::int_to_f(s32(th)) * kStick;
        c.throttle_state = 0;
        x = analog(k, p);
    } else {
        state = c.throttle_state;
        if (state != 0) {
            x = c.throttle_pedal - k.throttle_down;
            if (x <= kZero) {
                c.throttle_state = state = 0;
                x = kZero;
            }
            reverse_held = true;
        }
    }
    if (reverse_held && state == 2) c.reverse_req = 1;
    if (state == 0 && f.kind == 4) x = pedal_curve(k.wheel_throttle, x);
    c.throttle_pedal = x;

    // Brake.
    if (!(f.flags & 4)) {
        if (s16(f.brake) != 0) {
            c.brake_pedal = c.brake_pedal + k.brake_up;
            if (kOne < c.brake_pedal) c.brake_pedal = kOne;
        } else {
            c.brake_pedal = c.brake_pedal - k.brake_down;
            if (c.brake_pedal < kZero) c.brake_pedal = kZero;
        }
    } else {
        F b = ee::int_to_f(s16(f.brake)) * kStick;
        if (!(b == kZero)) b = analog(k, b);
        if (f.kind == 4) b = pedal_curve(k.wheel_brake, b);
        c.brake_pedal = b;
    }
    c.shift_req = c.hold != 0 ? 0 : f.shift;

    // Third axis.
    if (!(f.flags & 0x10)) {
        if (s16(f.third) != 0) {
            c.handbrake = c.handbrake + k.third_up;
            if (kOne < c.handbrake) c.handbrake = kOne;
        } else {
            c.handbrake = c.handbrake - k.third_down;
            if (c.handbrake < kZero) c.handbrake = kZero;
        }
    } else {
        const F h = ee::int_to_f(s16(f.third)) * kStick;
        c.handbrake = h == kZero ? h : analog(k, h);
    }
    pad_clutch(c, f, style);
    // (the later builds: a stepless gearbox creeps: the pedal is at least what holds the engine 500 rpm above idle; from
    // 2002 Tokyo-Seoul on not while it stands on the third axis)
    if (later && stepless(c.p.d)) {
        if (rule != Program::ConceptTokyo && !(c.handbrake < F::raw(geneva ? 0x3F4CCCCC : 0x3F4CCCCD)) &&
            !(F::raw(geneva ? 0x3E8E38E3 : 0x3E8E38E4) <= sqrt(c.speed * c.speed + c.lat_speed * c.lat_speed)))
            return;
        F a = ee::int_to_f(c.p.e.idle) + F::raw(0x43FA0000);
        const F b = c.torque_wot + c.torque_friction;
        a = a - ee::int_to_f(s16(c.rpm));
        const F q = c.torque_friction / b;
        a = a * F::raw(geneva ? 0x3A03126E : 0x3A03126F);
        F floor = a + q;
        if (floor < kZero) floor = kZero;
        else if (kOne < floor) floor = kOne;
        if (c.throttle_pedal < floor) c.throttle_pedal = floor;
        if (s16(f.throttle) == 0 && s16(f.back) == 0 && c.gear == 0) c.reverse_req = 1;
    }
}

void set_brake(Car& c, F p) {
    for (int i = 3; i >= 0; --i) c.w[size_t(i)].brake = p;
}

void pad_hud(Car& c, const PadFrame& f) {
    if (!(f.flags & 1)) c.steer_pct = s8(f.steer == 0 ? 0 : f.steer > 0 ? 100 : -100);
    else {
        const s32 p = s32(f.steer) * 100;
        c.steer_pct = s8((p < 0 ? p + 0xFFF : p) >> 12);
    }
    if (f.flags & 2) {
        if (f.throttle != 0x1000) c.full_throttle = 0;
    } else if (f.throttle == 0) {
        c.full_throttle = 0;
    }
}

void pad_input(Car& c, const PadConstants& k, const PadFrame& f, u8 course_kind) {
    pad_controls(c, k, f, course_kind);
    c.throttle = c.throttle_pedal;
    set_brake(c, c.brake_pedal);
}

bool on_pad_path(const Car& c) { return c.control == 0 && (c.driver_kind == 0 || c.hold != 0); }

void driver_input_pad(Car& c, const PadConstants& k, const PadFrame& f, u8 course_kind) {
    c.full_throttle = 1;
    pad_hud(c, f);
    pad_input(c, k, f, course_kind);
}

void set_brake_pedal(Car& c, F p) {
    c.brake_pedal = p;
    set_brake(c, p);
}

void ai_outputs(Car& c) {
    const Driver& d = c.ai;
    F x = d.throttle;
    if (u8(d.mode - 1) < 2) {
        const F th = c.throttle_pedal;
        if (th < d.throttle) {
            x = th + c.dt;
            if (d.throttle < x) x = d.throttle;
        } else {
            x = th - c.dt;
            if (x < d.throttle) x = d.throttle;
        }
    }
    c.throttle_pedal = x;
    c.throttle = x;
    c.steer_cmd = d.steer;
    set_brake_pedal(c, d.brake);
    c.reverse_req = s8(d.reverse);
    if (d.shift_lock != 0) c.shift_lockout = u8(d.shift_lock);
    // (Rule::AiOutputs, the later builds: no steering percentage here (driver_input filters it); the handbrake while the
    // car stands on its stop point)
    if (!gt3_program(c.behaviour.rule(Rule::AiOutputs))) {
        if (d.stop == 2) c.handbrake = kOne;
        return;
    }
    c.steer_pct = s8(ee::to_int(d.steer * F::raw(0x42C80000) / c.p.max_lock));
}

void ai_steer_pct(Car& c) {
    const Program rule = c.behaviour.rule(Rule::DriverInput);
    if (gt3_program(rule)) return;
    c.steer_pct = s8((s32(c.steer_pct) + 2 * hud_steer(c.steer_cmd, rule >= Program::ConceptGenevaEu)) / 3);
}

void ai_controls(Car& c) {
    ai_outputs(c);
    if (c.driver_kind == 8) c.handbrake = kOne;
}

} // namespace gt3r::sim::exact
