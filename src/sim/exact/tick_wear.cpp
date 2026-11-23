// Phases 8 and 13: brake heat, tyre wear and its flags, the flap pendulums; see tick.h.

#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

const F kOne = F::raw(0x3F800000);
const F kMinusOne = F::raw(0xBF800000);
const F k255 = F::raw(0x437F0000);
// The flap pendulums (0x352994 .. 0x3529AC).
const F kFlapAccel = F::raw(0x40A00000);  // 5: a_x's share
const F kFlapSpeed = F::raw(0x40A00000);  // 5: the hub-speed kick
const F kFlapChance = F::raw(0x3CA3D70A); // 0.02
const F kFlapRim = F::raw(0x41A00000);    // 20: the rim-speed kick off the tarmac
const F kFlapRough = F::raw(0x3D23D70A);  // 0.04
const F kFlapSpring = F::raw(0x42C80000); // 100
const F kFlapDamp = F::raw(0x40A00000);   // 5

} // namespace

void brake_heat(Car& c, const BrakeHeatConsts& k) {
    const F dt = c.dt;
    for (size_t i = 0; i < 4; ++i) {
        Wheel& w = c.w[i];
        const size_t a = i >> 1;
        F v = w.rim_speed;
        const F torque = c.p.brake_torque[a] * c.p.tire_radius[a];
        if (v < F()) v = -v;
        F heat = torque * w.brake;
        F cool = v + k.c2;
        heat = heat * v;
        const F y = w.brake_heat + k.c3;
        cool = cool * k.c1;
        heat = heat * k.c0;
        cool = cool * y;
        F h = w.brake_heat + (heat - cool) * dt;
        if (kOne < h) h = kOne;
        else if (h < F()) h = F();
        w.brake_heat = h;
    }
}

void tire_wear(Car& c, const WearParams& cp) {
    if (cp.max == F()) return;
    for (size_t i = 4; i-- > 0;) c.w[i].wear_byte = 0;
    if (c.x535 & 0x10) return; // on the pit surface
    const F dt = c.dt;
    for (int i = 0; i < 4; ++i) {
        Wheel& w = c.w[size_t(i)];
        if (w.surface == 1) continue; // kerb
        F u = w.load;
        if (u == F()) continue;
        const size_t a = size_t(i >> 1);
        u = c.p.wear_load_offset[a] + u * c.p.wear_load_gain[a];
        if (u < F()) u = F();
        const TireRecord& r = tire_record(c.p, i, w.surface);
        F s = w.slip_intensity;
        if (cp.slip_cap < s) s = cp.slip_cap;
        s = s * r.wear_rate;
        const F d = s * u;
        s = s * k255;
        w.wear = w.wear + d * dt;
        if (k255 < s) s = k255;
        w.wear_byte = u8(ee::to_int(s));
    }
}

void wear_flags(Car& c, const WearParams& cp, const PitMode& m) {
    if (!m.pits || (c.x535 & 0x08)) return;
    u32 f = 0;
    if (!(cp.max == F()))
        for (const Wheel& w : c.w) {
            if (w.wear < F()) continue;
            if (w.wear_grip < cp.worn) {
                f = 1;
                break;
            }
        }
    // 0x1D9FE0: the conductor's mode code, 0 for a car with +0x136D set.
    const s32 mode = c.control ? 0 : m.mode;
    if (mode == 4)
        for (const Wheel& w : c.w)
            if (w.x28) {
                f |= 2;
                break;
            }
    if (!f) return;
    const u8 flags = c.x535 | 0x04;
    c.x535 = flags;
    if (c.control == 2) c.x535 = flags | 0x08;
    else if (c.control == 1 && (m.kind != 3 || (f & 2))) c.x535 = flags | 0x08;
}

F vu0_random(Vu0Random& rng) {
    const u32 m = rng.r;
    rng.r = (((m << 1) | (((m >> 4) ^ (m >> 22)) & 1u)) & 0x7FFFFFu) | 0x3F800000u;
    return F::raw(rng.r) - kOne;
}

void flaps(Car& c, Vu0Random& rng) {
    for (int i = 0; i < 4; ++i) {
        Wheel& w = c.w[size_t(i)];
        F a = c.a_x * kFlapAccel;
        F x = w.flap, v = w.flap_v;
        const F room = F() <= w.hub_vx ? kOne - x : x + kOne;
        if (vu0_random(rng) * room < kFlapChance) a = a + (kOne - x) * kFlapSpeed * w.hub_vx;
        if (tire_record_index(c.p, i, w.surface) != 0 && F() < w.hub_vx && F() < w.load) {
            const F left = kOne - x;
            if (vu0_random(rng) * left < kFlapRough) a = a + left * kFlapRim * w.rim_speed;
        }
        const F spring = x * kFlapSpring;
        const F dt = c.dt;
        const F damp = v * kFlapDamp;
        a = a - spring;
        a = a - damp;
        v = v + a * dt;
        x = x + v * dt;
        if (kOne < x) x = kOne, v = F();
        else if (x < kMinusOne) v = F(), x = kMinusOne;
        w.flap_v = v;
        w.flap = x;
    }
}

} // namespace gt3r::sim::exact
