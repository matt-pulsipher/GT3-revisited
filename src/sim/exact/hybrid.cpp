// The later builds' layout 7 system; see hybrid.h. Concept 2001 Tokyo's addresses; each build's constants from its own
// routines (2002 Tokyo-Geneva's folded toward zero).

#include "sim/exact/hybrid.h"

#include "base/ee_math.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

#include <array>
#include <cstring>

namespace gt3r::sim::exact {

namespace {

using T = ProgramData::Table;
using ee::int_to_f;

constexpr F kZero{}, kOne = F::raw(0x3F800000), kMinusOne = F::raw(0xBF800000), kHalf = F::raw(0x3F000000);

// The routines' constants: (rad/s x N m to kW: 2 pi / 60000 x 1.0007, read 0x332580 ..), kW x this over rpm is N m (9542.84,
// 0x332578 / 0x3325A4), N x m/s to kW over a second (0.00100068, 0x332590 / 0x3325A8) and 1 km/h (0x33258C).
F k_power(const Car& c) { return folded(c.behaviour, 0x38DBC304, 0x38DBC307); }
F k_torque(const Car& c) { return folded(c.behaviour, 0x46151B5E, 0x46151B5C); }
F k_charge(const Car& c) { return folded(c.behaviour, 0x3A832920, 0x3A832923); }

// 0x1DA0D8: the flow display.
void flow(Car& c, const Hybrid& h, F x) {
    s32 v = s32(s8(c.later.special[5])) + ee::to_int(x * h.flow_scale);
    if (v < -100) v = -100;
    if (!(v < 101)) v = 100;
    c.later.special[5] = u8(v);
}

} // namespace

Hybrid hybrid_settings(const ProgramData& d) {
    if (!d.has(T::Hybrid)) return {};
    std::array<u8, 0x1B> t{};
    for (u32 k = 0; k < t.size(); ++k) t[k] = d.b(T::Hybrid, k);
    return hybrid_settings(t, d.behaviour());
}

Hybrid hybrid_settings(std::span<const u8> table, const Behaviour& b) {
    Hybrid h;
    if (table.size() < 0x1B || gt3_program(b.rule(Rule::HybridStep))) return h;
    auto byte = [&](u32 k) { return table[k]; };
    auto f = [&](u32 k) { return int_to_f(s32(byte(k))); };
    auto tens = [&](u32 k) { return int_to_f(s32(byte(k)) * 10); };
    const F hundred = F::raw(0x42C80000), g100 = folded(b, 0x44750000, 0x4474FFFE); // 100, 980
    h.on = 1;
    h.front = f(0), h.rear = f(1);
    const F sum = (h.front + h.front) + h.rear;
    h.front_drive = tens(2), h.rear_drive = tens(3), h.front_regen = tens(4), h.rear_regen = tens(5);
    h.front_scale = f(6) * folded(b, 0x3DCCCCCD, 0x3DCCCCCC); // 0.1 (0x332570)
    h.capacity = f(7) * sum;
    h.front_charge_cap = int_to_f(s32(byte(8)) * 5), h.rear_charge_cap = int_to_f(s32(byte(9)) * 5);
    h.idle_rpm = f(0xA) * hundred;
    h.idle_gain = f(0xB) / F::raw(0x47C35000); // 100000 (0x33256C)
    h.idle_share = f(0xC) / hundred;
    h.idle_power = f(0xD);
    h.low = (h.capacity * f(0xE)) / hundred, h.high = (h.capacity * f(0xF)) / hundred;
    h.reserve = f(0x10) / h.front;
    h.gear_lo = s8(byte(0x11)), h.gear_hi = s8(byte(0x12)), h.gear_min = s8(byte(0x13));
    h.front_share = f(0x14) / hundred;
    h.drive_accel = f(0x15) / g100, h.drive_lat = f(0x16) / g100, h.drive_dead = f(0x17) / hundred;
    h.regen_accel = f(0x18) / g100, h.regen_lat = f(0x19) / g100, h.regen_dead = f(0x1A) / hundred;
    // (0x332574: 6000; 2002 Tokyo-Geneva Europe's routine 0x1DA4F8 has 5000)
    h.flow_scale = F::raw(b.rule(Rule::HybridStep) == Program::ConceptGenevaEu ? 0x459C4000 : 0x45BB8000) / sum;
    return h;
}

bool hybrid_car(const Car& c, const Hybrid& h) { return h.on && c.p.d.layout == 7 && !gt3_program(c.behaviour.rule(Rule::HybridStep)); }

F hybrid_store(const Car& c) {
    u32 bits;
    std::memcpy(&bits, c.later.special.data(), 4);
    return F::raw(bits);
}

void set_hybrid_store(Car& c, F v) { std::memcpy(c.later.special.data(), &v.bits, 4); }

void hybrid_start(Car& c, const Hybrid& h) {
    set_hybrid_store(c, kOne * h.capacity);
    c.later.special[5] = 0;
    c.later.special[4] = 0;
}

F hybrid_charge(Car& c, const Hybrid& h, F x) {
    const F s = hybrid_store(c);
    flow(c, h, x);
    const F t = s + x;
    if (h.capacity < t) {
        set_hybrid_store(c, h.capacity);
        return x - (t - h.capacity);
    }
    set_hybrid_store(c, t);
    return x;
}

F hybrid_draw(Car& c, const Hybrid& h, F x) {
    const F s = hybrid_store(c);
    flow(c, h, -x);
    if (s < x) {
        set_hybrid_store(c, kZero);
        return x - (x - s);
    }
    set_hybrid_store(c, s - x);
    return x;
}

F hybrid_level(const Car& c, const Hybrid& h) {
    const F s = hybrid_store(c);
    if (h.high <= s) return kOne;
    if (s <= h.low) return kZero;
    return (s - h.low) / (h.high - h.low);
}

F hybrid_demand(const Car& c, const Hybrid& h) {
    const F p = ((c.torque_wot + c.torque_friction) * int_to_f(c.rpm)) * k_power(c);
    const F share = p / ((p + h.rear) + h.rear);
    const F x = c.clutch == 2 ? c.throttle_pedal : c.throttle;
    if (x <= share) return kMinusOne;
    const F d = x - share;
    F r = (d + d) / (kOne - share);
    r = r * hybrid_level(c, h);
    if (!(s32(h.gear_min) < s32(c.gear))) r = kZero;
    else if (kOne < r && (s32(c.gear) < s32(h.gear_lo) || s32(h.gear_hi) < s32(c.gear))) r = kOne;
    return r + kMinusOne;
}

F hybrid_wanted_lateral(const Car& c) {
    const F steer = c.steer_cmd;
    F a = c.speed * ((c.speed * ee::tan(steer)) / c.p.wheelbase);
    if (a < kZero) a = -a;
    const int side = kZero < steer ? 1 : 0;
    const TireRecord& r = tire_record(c.p, side, c.w[size_t(side)].surface);
    F s = steer * F::raw(0x3FC00000); // 1.5
    if (s < kZero) s = -s;
    F g = lookup(r.curve_a, s) * r.grip;
    g = g + g;
    if (g < kZero) g = -g;
    if (g < a) a = g;
    if (steer < kZero) a = -a;
    return a;
}

F hybrid_engine_throttle(const Car& c, const Hybrid& h) {
    const F p = ((c.torque_wot + c.torque_friction) * int_to_f(c.rpm)) * k_power(c);
    const F share = p / ((p + h.rear) + h.rear);
    F x = c.throttle / share;
    if (kOne < x) x = kOne;
    return x;
}

F hybrid_rear_torque(Car& c, const Hybrid& h) {
    const F demand = hybrid_demand(c, h);
    const F kt = k_torque(c), dt = c.dt;
    // the rear wheels' braking (their brake pressure not 0) charges the store
    F g{};
    for (size_t i = 2; i < 4; ++i) {
        if (c.w[i].brake == kZero) continue;
        F p = c.t[i].react_brake * c.w[i].rim_speed;
        if (p < kZero) p = -p;
        g = g + p * k_charge(c);
    }
    if (h.rear_charge_cap < g) g = h.rear_charge_cap;
    hybrid_charge(c, h, g * dt);
    const F rpm = int_to_f(c.rpm);
    F t = (h.rear * kt) / rpm; // the motor's torque at the engine's speed
    if (kZero < demand) {
        if (h.rear_drive < t) t = h.rear_drive;
        F e = (t * rpm) / kt;
        t = t * demand;
        e = (e * demand) * dt;
        const F got = hybrid_draw(c, h, e);
        return t * (got / e);
    }
    if (h.rear_regen < t) t = h.rear_regen;
    F e = (t * rpm) / kt;
    t = t * demand;
    e = -(e * demand);
    hybrid_charge(c, h, e * dt);
    return t;
}

F hybrid_open_clutch_torque(Car& c, const Hybrid& h) {
    const F rpm = int_to_f(c.rpm);
    F x = c.throttle;
    if (kOne <= hybrid_store(c) / h.capacity) return engine_torque(c, x);
    const F m = (h.idle_power * k_torque(c)) / rpm;
    F t = c.torque_wot * h.idle_share;
    F lo = (h.idle_rpm - rpm) * h.idle_gain;
    if (m < t) t = m;
    const F fr = c.torque_friction;
    lo = lo + (t + fr) / (c.torque_wot + fr);
    if (lo < kZero) lo = kZero;
    if (x < lo) x = lo;
    if (kOne < x) x = kOne;
    c.throttle = x;
    const F e = engine_torque(c, x);
    const F q = ((t * rpm) * k_power(c)) * c.dt;
    const F r = e - t;
    hybrid_charge(c, h, q);
    return r;
}

void hybrid_front_motors(Car& c, const Hybrid& h) {
    const F dt = c.dt;
    const F x = c.clutch == 3 ? c.throttle_pedal : c.throttle;
    c.clutch_torque = kZero;
    // the front motors' share: the engine's power with the rear motor's demand, the front share of it over the two motors
    F p = (engine_output(c, x) * int_to_f(c.rpm)) * k_power(c);
    const F demand = hybrid_demand(c, h);
    p = p + h.rear * demand;
    p = ((p * h.front_share) / (kOne - h.front_share)) / (h.front + h.front);
    if (kOne < p) p = kOne;
    else if (p < kMinusOne) p = kMinusOne;
    const F level = hybrid_level(c, h);
    if (const F cap = level + h.reserve * (kOne - level); cap < p) p = cap;
    // the front wheels' braking charges the store; a wheel under 1 km/h is slow
    std::array<int, 2> slow{};
    for (size_t i = 0; i < 2; ++i) {
        F v = c.w[i].rim_speed;
        if (v < kZero) v = -v;
        slow[i] = v < folded(c.behaviour, 0x3E8E38E4, 0x3E8E38E3) ? 1 : 0;
        F r = c.t[i].react_brake;
        if (r < kZero) r = -r;
        F g = (r * v) * k_charge(c);
        if (h.front_charge_cap < g) g = h.front_charge_cap;
        hybrid_charge(c, h, g * dt);
    }
    // a front wheel's speed (m/s) to its motor's rpm x 716.2 / 44.13 (0x332594 2 pi, 0x332598 9.8, 0x3325A0 716.2,
    // 0x33259C 44.13)
    const F k = (((c.p.tire_radius[0] * folded(c.behaviour, 0x40C90FDB, 0x40C90FDA)) * folded(c.behaviour, 0x411CCCCD, 0x411CCCCC)) *
                 folded(c.behaviour, 0x44330CCD, 0x44330CCC)) /
                F::raw(0x4230851E);
    std::array<F, 2> s{p, p}; // per front wheel: the share, then the torque
    const F lat = hybrid_wanted_lateral(c);
    if (kZero < p) {
        // driving: the yaw the steering wants beyond the car's lateral acceleration moves torque to the outer wheel
        const F tmax = h.front_scale * h.front_drive;
        F y = lat * h.drive_lat - c.a_y * h.drive_accel;
        if (kZero < y) {
            y = y - h.drive_dead;
            if (!(y < kZero)) {
                y = y * kHalf;
                const F a = y + kOne, b = kOne - y;
                s[1] = p * a, s[0] = p * b;
                if (kOne < s[1]) {
                    const F o = s[1] - kOne;
                    s[1] = kOne;
                    s[0] = s[0] - o;
                }
                if (s[0] < kMinusOne) s[0] = kMinusOne;
            }
        } else if (y < kZero) {
            y = y + h.drive_dead;
            if (!(kZero < y)) {
                y = y * kHalf;
                const F a = kOne - y, b = y + kOne;
                s[0] = p * a, s[1] = p * b;
                if (kOne < s[0]) {
                    const F o = s[0] - kOne;
                    s[0] = kOne;
                    s[1] = s[1] - o;
                }
                if (s[1] < kMinusOne) s[1] = kMinusOne;
            }
        }
        std::array<F, 2> out{}; // the power each motor draws
        for (size_t i = 0; i < 2; ++i) {
            if (slow[i] == 0) {
                const F v = c.w[i].rim_speed;
                F q = s[i] * ((h.front * k) / v);
                if (v < kZero) q = -q;
                s[i] = q;
                if (tmax < q) s[i] = tmax;
                else if (q < -tmax) s[i] = -tmax;
                F o = (s[i] * v) / k;
                if (v < kZero) o = -o;
                out[i] = o;
            } else if (c.brake_pedal == out[i]) {
                s[i] = tmax; // standing, off the brake: the motor's whole torque
            }
        }
        F o0 = out[0], o1 = out[1];
        if (const F e = (o0 + o1) * dt; kZero < e) {
            const F f = hybrid_draw(c, h, e) / e;
            s[1] = s[1] * f, o0 = o0 * f, s[0] = s[0] * f, o1 = o1 * f;
        }
        F sum = kZero;
        if (kZero < o0) sum = o0 + sum;
        if (kZero < o1) sum = sum + o1;
        c.clutch_torque = sum / h.front;
        if (c.gear == 0) s[0] = -s[0], s[1] = -s[1];
        c.t[0].torque = c.t[0].torque + s[0];
        c.t[1].torque = c.t[1].torque + s[1];
        return;
    }
    // regenerating: the same split of the braking torque (the outer wheel's towards 0), nothing drawn
    const F rmax = h.front_scale * h.front_regen;
    F y = lat * h.regen_lat - c.a_y * h.regen_accel;
    if (kZero < y) {
        y = y - h.regen_dead;
        if (!(y < kZero)) {
            y = y * kHalf;
            const F a = y + kOne, b = kOne - y;
            s[0] = s[0] * a, s[1] = s[1] * b;
            if (s[0] < kMinusOne) {
                const F o = s[0] + kOne;
                s[0] = kMinusOne;
                s[1] = s[1] - o;
            }
            if (kZero < s[1]) s[1] = kZero;
        }
    } else if (y < kZero) {
        y = y + h.regen_dead;
        if (!(kZero < y)) {
            y = y * kHalf;
            const F a = kOne - y, b = y + kOne;
            s[1] = s[1] * a, s[0] = s[0] * b;
            if (s[1] < kMinusOne) {
                const F o = s[1] + kOne;
                s[1] = kMinusOne;
                s[0] = s[0] - o;
            }
            if (kZero < s[0]) s[0] = kZero;
        }
    }
    for (size_t i = 0; i < 2; ++i) {
        F q = s[i];
        if (slow[i] == 0) { // (a slow wheel takes the share itself)
            const F v = c.w[i].rim_speed;
            q = q * ((h.front * k) / v);
            if (v < kZero) q = -q;
            if (q < -rmax) q = -rmax;
            else if (rmax < q) q = rmax;
        }
        if (c.gear == 0) q = -q;
        c.t[i].torque = c.t[i].torque + q;
    }
}

} // namespace gt3r::sim::exact
