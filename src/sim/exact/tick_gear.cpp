// Phase 7's gearbox: the gear decision by gearbox mode and the gear change; see tick.h.

#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

constexpr F kRpmToW = F::raw(0x3DD67750); // 2 pi / 60
constexpr F kKmh1 = F::raw(0x3E8E38E4);   // 1 km/h in m/s
constexpr F kKmh5 = F::raw(0x3FB1C71D);   // 5 km/h in m/s

// A held lockout ends its count at 1 while a shift input is held.
int lockout_tick(Car& c, bool held) {
    c.shift_lockout = u8(c.shift_lockout - 1);
    if (c.shift_lockout == 0 && held) c.shift_lockout = 1;
    return -1;
}

// The +1 / -1 shift input within 1..N (`lowest` = 0 lets mode 5 shift down into reverse).
int manual_step(Car& c, int lowest) {
    const int g = c.gear;
    if (c.shift_req > 0 && g < c.p.d.gears) {
        c.shift_lockout = 1;
        return g + 1;
    }
    if (c.shift_req < 0 && g > lowest) {
        c.shift_lockout = 1;
        return g - 1;
    }
    return -1;
}

} // namespace

void set_gear(Car& c, u8 gear) { c.gear = gear; }

int gear_for_speed(const Car& c, F v) {
    if (v < F()) return 0;
    // (Rule::Driveline, the later builds: a stepless gearbox of kind 1 is always in first)
    if (c.p.d.kind == 1 && !gt3_program(c.behaviour.rule(Rule::Driveline))) return 1;
    const int n = c.p.d.gears;
    if (!(1 < n)) return n;
    const F w = ee::int_to_f(s32(c.p.d.up_rpm) - 500) * folded(c.behaviour, kRpmToW.bits, 0x3DD6774E); // (Europe 0x1F0980's)
    for (int k = 1; k < n; ++k)
        if (v < w / c.p.d.gear_speed[size_t(k)]) return k;
    return n;
}

void kickdown_step(Car& c) {
    s32 level = -1;
    if (!(c.throttle_pedal <= F())) level = ee::to_int((F::raw(0x3F800000) - c.throttle_pedal) * F::raw(0x40400000));
    if (c.brake_pedal == F::raw(0x3F800000)) level = 0;
    if (c.kickdown_timer != 0) c.kickdown_timer = u8(c.kickdown_timer - 1);
    const s32 cur = c.kickdown;
    if (level == cur || level == -1) {
        c.kickdown_count = 0;
        return;
    }
    c.kickdown_count = u8(c.kickdown_count + 1);
    if (c.kickdown_count < (level < cur ? (pal_rate(c) ? 11 : 13) : (pal_rate(c) ? 51 : 61))) return;
    c.kickdown_timer = pal_rate(c) ? 50 : 60;
    c.kickdown_count = 0;
    c.kickdown = u8(level < cur ? cur - 1 : cur + 1);
}

int shift_auto(Car& c, const GearSettings& s, bool kickdown) {
    const int g = c.gear;
    const Driveline& d = c.p.d;
    if (c.antihunt != 0) c.antihunt = u16(c.antihunt - 1);
    if (kickdown) kickdown_step(c);
    if (c.shift_lockout != 0) return lockout_tick(c, c.shift_req != 0);
    if (c.reverse_req != 0) return g != 0 ? 0 : -1;
    const F v = c.speed;
    // (2002 Tokyo-Geneva's constants folded toward zero: Europe 0x1F0B18)
    const F kKmh1 = folded(c.behaviour, sim::exact::kKmh1.bits, 0x3E8E38E3), kRpmToW = folded(c.behaviour, sim::exact::kRpmToW.bits, 0x3DD6774E);
    if (kKmh1 < v && g == 0) return 1;
    if (!(c.throttle == F())) {
        if (v < kKmh1) return g != 1 ? 1 : -1;
    } else if (v < -kKmh1 && g != 0) return 0; // coasting backwards: reverse
    if (g > 0 && g < d.gears) { // over-rev
        const F top = (ee::int_to_f(s32(c.p.e.rev_limit) + 500) * kRpmToW) / d.gear_speed[size_t(g)];
        if (top < v) return g + 1;
    }
    if (s.mode == 1) {
        if (c.shift_req != 0) return -1;
    } else if (s.mode == 2) {
        if (c.shift_req > 0 && g < d.gears) {
            c.shift_lockout = 1;
            c.antihunt = pal_rate(c) ? 200 : 240; // 4 s
            return g + 1;
        }
        if (c.shift_req < 0 && !(g < 2)) {
            c.antihunt = pal_rate(c) ? 200 : 240; // 4 s
            c.shift_lockout = 1;
            return g - 1;
        }
        if (c.antihunt != 0) return -1;
    }
    if (c.clutch != 1) return -1;
    if (kickdown) {
        if (g == 0) return -1;
        int n = gear_for_speed(c, c.speed) + c.kickdown;
        if (d.gears < n) n = d.gears;
        return g == n ? -1 : n;
    }
    if (g == 0) return -1;
    s32 down, up;
    if (g == 1) {
        if (d.gears == 1) return -1;
        down = -20000;
        up = d.up_rpm;
    } else {
        up = g < d.gears ? s32(d.up_rpm) : 20000;
        down = d.down_rpm[size_t(g - 2)];
        if (c.throttle_pedal < folded(c.behaviour, 0x3D4CCCCD, 0x3D4CCCCC)) down += 100 * s32(s.down_lift);
        if (folded(c.behaviour, 0x3F4CCCCD, 0x3F4CCCCC) < c.brake_pedal) down += 100 * s32(s.down_brake);
        if (F::raw(0x3E800000) < c.throttle_pedal) down -= 100 * s32(s.down_throttle);
    }
    bool front = false, rear = false; // the driven wheels the traction veto examines
    // (Rule::Layout, the later builds: layout 7, AWD system 5, as the rear-driven ones; layout 8, system 7, as GT3's 7)
    const bool later = !gt3_program(c.behaviour.rule(Rule::Layout));
    switch (d.layout) {
    case 0:
    case 4:
    case 5:
    case 6: rear = true; break;
    case 1:
    case 2: front = true; break;
    case 3: front = rear = true; break;
    case 7: front = !later, rear = true; break;
    case 8: front = rear = later; break;
    default: break;
    }
    if (c.rpm < down) return g - 1;
    if (!(up < c.rpm)) return -1;
    for (int i = 0; i < 4; ++i) {
        if (!front && i < 2) continue;
        if (!rear && i >= 2) break;
        const Wheel& w = c.w[size_t(i)];
        if (w.load == F()) return -1;
        if (w.slip_ratio < tire_record(c.p, i, w.surface).slip_drive * w.slip_gain) return -1;
    }
    return g + 1;
}

int shift_manual(Car& c) {
    const int g = c.gear;
    if (c.reverse_req != 0) return g != 0 ? 0 : -1;
    if (g == 0 && !(c.throttle_pedal == F())) {
        const int n = gear_for_speed(c, c.speed);
        return n == 0 ? 1 : n;
    }
    if (c.shift_lockout != 0) return lockout_tick(c, c.shift_req != 0);
    return manual_step(c, 1);
}

int shift_sequential(Car& c) {
    const int g = c.gear;
    if (c.shift_lockout != 0) return lockout_tick(c, c.shift_req != 0 || c.reverse_req != 0);
    if (c.reverse_req != 0) {
        if (g == 0) return -1;
        c.shift_lockout = 1;
        return 0;
    }
    return manual_step(c, 1);
}

int shift_sequential_reverse(Car& c) {
    if (c.shift_lockout != 0) return lockout_tick(c, c.shift_req != 0);
    return manual_step(c, 0);
}

int gear_decision(Car& c, const GearSettings& s) {
    if (c.hold != 0) return c.gear != 1 ? 1 : -1;
    const u8 mode = c.control != 1 && c.driver_kind == 0 ? c.pedal_style : 0;
    switch (mode) {
    case 0: return shift_auto(c, s, false);
    case 1: return shift_manual(c);
    case 2: return shift_auto(c, s, true);
    case 3:
    case 4:
    case 6: return shift_sequential(c);
    case 5: return shift_sequential_reverse(c);
    default: return -1;
    }
}

// The later builds' stepless gearbox of kind 1 (D +0x12; their Rule::Driveline's, 0 on GT3's discs) has no gear step
// (Concept 2001 Tokyo 0x1DE4D8 in its place): forward or reverse alone, the clutch slipping in again after either; the
// shift lock-out is not counted down.
void stepless_gear(Car& c) {
    const F kKmh1 = folded(c.behaviour, sim::exact::kKmh1.bits, 0x3E8E38E3); // (2002 Tokyo-Geneva's)
    int g = -1;
    if (c.reverse_req != 0) {
        if (c.gear != 0) g = 0;
    } else if (kKmh1 < c.speed || F() < c.throttle) {
        if (c.gear == 0) g = 1;
    } else if (c.speed < -kKmh1 && c.gear != 0) g = 0;
    if (g == -1) return;
    set_gear(c, u8(g));
    c.clutch = 2;
}

void gear_step(Car& c, const GearSettings& s) {
    if (c.p.d.kind == 1) return stepless_gear(c);
    const int g = gear_decision(c, s);
    c.ev.waiting = 0;
    if (g == -1) return;
    if (c.ev.on) {
        // An electric car's clutch stays locked, and it changes between forward and reverse only below 5 km/h, as
        // electric cars do: at speed its motor would brake the driven wheels alone at full torque, past ABS. Until then
        // the change waits, and the motor brakes (electric_output).
        const F v = c.speed < F() ? -c.speed : c.speed;
        if ((g == 0) != (c.gear == 0) && !(v < kKmh5)) {
            c.ev.waiting = 1;
            return;
        }
    } else if (c.clutch != 0) {
        // (a stepless gearbox of kind 2 changes its ratio with the clutch as it is)
        if (g == 0 || c.gear == 0) c.clutch = 2;
        else if (c.p.d.kind != 2) c.clutch = 3;
    }
    set_gear(c, u8(g));
}

} // namespace gt3r::sim::exact
