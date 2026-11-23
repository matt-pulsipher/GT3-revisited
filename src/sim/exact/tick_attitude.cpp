// Phase 12's body half: the Euler-angle axes, heave / pitch / roll with the spawn hold, the bump-stop nudge and the ground
// frame; see tick.h.

#include "base/ee_math.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

const F kZero = F::raw(0), kHalf = F::raw(0x3F000000), kThirty = F::raw(0x41F00000);
const F kPi = F::raw(0x40490FDB), kMinusPi = F::raw(0xC0490FDB), kTwoPi = F::raw(0x40C90FDB), kMinusTwoPi = F::raw(0xC0C90FDB);
const F kThreePi = F::raw(0x4116CBE4), kMinusThreePi = F::raw(0xC116CBE4);           // 0x3500F0 / 0x3500E4
const F kMaxRate = F::raw(0x3FC90FDB), kMinRate = F::raw(0xBFC90FDB);                // 0x350520..: +-pi/2 rad/s
const F kMaxAngle = F::raw(0x3F490FDB), kMinAngle = F::raw(0xBF490FDB);              // 0x350528..: +-pi/4
const F kGravity = F::raw(0x411CCCCD);                                                // 0x350540 / 0x350544: 9.8
const F kHoldTurn = F::raw(0x3F060A92);                                               // 0x35051C: 30 deg/s
const F kHoldHeight = F::raw(0x3E999999);                                             // 0x350514 / 0x350518: 0.3 m

// The angle wrap's constants: pi, 2 pi, 3 pi; 2002 Tokyo-Geneva's (Europe 0x1EAE20, its own routine) a unit or two lower.
struct Turns {
    F pi, minus_pi, two_pi, minus_two_pi, three_pi, minus_three_pi;
};
const Turns kTurns{kPi, kMinusPi, kTwoPi, kMinusTwoPi, kThreePi, kMinusThreePi};
const Turns kTurnsGeneva{F::raw(0x40490FDA), F::raw(0xC0490FDA), F::raw(0x40C90FDA), F::raw(0xC0C90FDA), F::raw(0x4116CBE3), F::raw(0xC116CBE3)};

// 0x1D8BA8: a far-out angle back by whole turns.
F angle_wrap_turns(F a, const Turns& t) {
    const F n = ee::int_to_f(ee::to_int((ee::abs(a) + t.pi) / t.two_pi));
    return a < kZero ? a + n * t.two_pi : a + n * t.minus_two_pi;
}

} // namespace

void euler_axes(F pitch, F roll, F heading, Vec3& fwd, Vec3& lat, Vec3& up) {
    const F sa = ee::sin(pitch), ca = ee::cos(pitch), sb = ee::sin(roll), cb = ee::cos(roll);
    const F sc = ee::sin(heading), cc = ee::cos(heading);
    const F ss = sc * sb, cs = cc * sb;
    const F sca = sc * ca, cca = cc * ca;
    const F ssa = ss * sa, csa = cs * sa, ccsa = cc * sa, scsa = sc * sa;
    const F csca = cs * ca, ssca = ss * ca;
    lat = {cc * cb, sc * cb, -sb};
    fwd = {csa - sca, ssa + cca, cb * sa};
    up = {csca + scsa, ssca - ccsa, cb * ca};
}

void body_axes(Car& c) { euler_axes(c.pitch, c.roll, c.heading, c.fwd, c.lat, c.up); }

F angle_wrap(F a, Program p) {
    if (p == Program::NtscJ) { // 0x1D6650: a turn at a time
        if (a < kMinusPi) {
            do a = a + kTwoPi;
            while (a < kMinusPi);
            return a;
        }
        while (kPi <= a) a = a - kTwoPi;
        return a;
    }
    const Turns& t = p >= Program::ConceptGenevaEu ? kTurnsGeneva : kTurns;
    if (a < t.minus_pi) return a < t.minus_three_pi ? angle_wrap_turns(a, t) : a + t.two_pi;
    if (!(t.pi <= a)) return a;
    return t.three_pi < a ? angle_wrap_turns(a, t) : a - t.two_pi;
}

F angle_diff(F a, F b, Program p) {
    const Turns& t = p >= Program::ConceptGenevaEu ? kTurnsGeneva : kTurns;
    const F d = angle_wrap(a, p) - angle_wrap(b, p);
    if (t.pi <= d) return d - t.two_pi;
    if (d < t.minus_pi) return d + t.two_pi;
    return d;
}

void pitch_terms(const Car& c, F& a, F& b) {
    F sa{}, sb{};
    for (size_t i = 0; i < 4; ++i) {
        const Wheel& w = c.w[i];
        const F q = (c.t[i].steer_cos * (w.fx_drive + w.fx_brake)) * c.p.width[i >> 1];
        sa = sa + q * c.p.pitch_tire[i >> 1];
        if (i < 2) sb = sb + q / c.p.dist_front;
        else sb = sb - q / c.p.dist_rear;
    }
    a = sa;
    b = sb;
}

F spawn_offset(s32 n, s32& mode, bool pal_rate) {
    const bool pal = pal_rate; // 250 steps (NTSC 300)
    if (n >= (pal ? 225 : 270)) {
        mode = 1;
        return (ee::int_to_f((pal ? 250 : 300) - n) * kHoldHeight) / (pal ? F::raw(0x41C80000) : kThirty);
    }
    if (n < (pal ? 25 : 30)) {
        mode = 0;
        return kZero;
    }
    mode = 2;
    return kHoldHeight;
}

bool spawn_settle(Car& c, F offset) {
    const F target = c.ground_pos.z + offset;
    if (target < c.pos.z) return false;
    c.pos.z = target;
    c.vel.z = kZero;
    c.roll = kZero;
    c.pitch = kZero;
    c.roll_rate = kZero;
    c.pitch_rate = kZero;
    return true;
}

bool spawn_hold(Car& c, F box_heading) {
    if (c.hold == 0) return false;
    const F dt = c.dt;
    s32 mode = 0;
    const F offset = spawn_offset(s32(c.hold), mode, pal_rate(c));
    if (!spawn_settle(c, offset)) return false;
    if (mode == 2) {
        const F d = angle_diff(box_heading, c.heading, c.behaviour.rule(Rule::AngleWrap));
        if (!(d == kZero)) {
            const F step = dt * folded(c.behaviour, kHoldTurn.bits, 0x3F060A90); // (2002 Tokyo-Geneva's: Europe 0x1E09A8)
            if (step < d) c.heading = c.heading + step;
            else if (d < -step) c.heading = c.heading - step;
            else c.heading = box_heading;
        }
    }
    body_axes(c);
    return true;
}

void attitude(Car& c, F box_heading) {
    const F dt = c.dt;
    // (2002 Tokyo-Geneva's pi / 2, pi / 4 and 9.8 a unit or two lower: Europe 0x1E0B68)
    const bool z = toward_zero(c.behaviour);
    const F kMaxRate = z ? F::raw(0x3FC90FDA) : sim::exact::kMaxRate, kMinRate = z ? F::raw(0xBFC90FDA) : sim::exact::kMinRate;
    const F kMaxAngle = z ? F::raw(0x3F490FD9) : sim::exact::kMaxAngle, kMinAngle = z ? F::raw(0xBF490FD9) : sim::exact::kMinAngle;
    const F kGravity = z ? F::raw(0x411CCCCC) : sim::exact::kGravity;
    F a{}, b{};
    pitch_terms(c, a, b);
    if (spawn_hold(c, box_heading)) return;
    const auto& f = c.body_force;
    const F d0 = c.w[0].damper_f, d1 = c.w[1].damper_f, d2 = c.w[2].damper_f, d3 = c.w[3].damper_f;
    // Pitch.
    const F front = (((f[0] + f[1]) + d0) + d1) * c.p.pitch_lever[0];
    const F rear = (((f[2] + f[3]) + d2) + d3) * c.p.pitch_lever[1];
    F rate = c.pitch_rate + ((front - rear) + a) * dt;
    if (kMaxRate < rate) rate = kMaxRate;
    else if (rate < kMinRate) rate = kMinRate;
    F angle = c.pitch + rate * dt;
    if (angle < kMinAngle) angle = kMinAngle, rate = kZero;
    else if (kMaxAngle < angle) angle = kMaxAngle, rate = kZero;
    c.pitch = angle;
    c.pitch_rate = rate;
    // Roll.
    const F left = (((f[0] - f[1]) + d0) - d1) * c.p.roll_lever[0];
    const F right = (((f[2] - f[3]) + d2) - d3) * c.p.roll_lever[1];
    rate = c.roll_rate + (left + right) * dt;
    if (kMaxRate < rate) rate = kMaxRate;
    else if (rate < kMinRate) rate = kMinRate;
    angle = c.roll + rate * dt;
    if (angle < kMinAngle) angle = kMinAngle, rate = kZero;
    else if (kMaxAngle < angle) angle = kMaxAngle, rate = kZero;
    c.roll = angle;
    c.roll_rate = rate;
    body_axes(c);
    // Heave along the new up axis, then gravity.
    const F u = (((((((f[0] + f[1]) + f[2]) + f[3]) + d0) + d1) + d2) + d3 + b) * c.p.inv_sprung_mass;
    c.heave_acc = u - c.up.z * kGravity;
    const F du = u * dt;
    c.vel.x = c.vel.x + c.up.x * du;
    c.vel.y = c.vel.y + c.up.y * du;
    c.vel.z = c.vel.z + c.up.z * du;
    c.vel.z = c.vel.z - dt * kGravity;
}

void bump_nudge(Car& c) {
    if (c.bump_mask == 0) return;
    hub_offsets_z(c);
    suspension_coords(c, c.terrain);
    const u32 mask = bump_mask(c, u32(c.bump_mask) << 4);
    c.bump_mask = u8(mask);
    if (mask & 0xF) {
        F d[4];
        for (size_t i = 0; i < 4; ++i) {
            const F p = c.p.susp[i >> 1].bump_limit - c.susp_x[i];
            d[i] = p < kZero ? kZero : p;
        }
        const F w = c.p.rear_weight;
        const F left = d[0] + (d[2] - d[0]) * w, right = d[1] + (d[3] - d[1]) * w;
        const F front = (d[0] + d[1]) * kHalf, rear = (d[2] + d[3]) * kHalf;
        F lift = kZero;
        if (!(left == kZero && right == kZero)) {
            if (right < left) {
                lift = right * kHalf;
                c.roll = c.roll + atan2(c.behaviour, left - right, c.p.track);
            } else {
                lift = left * kHalf;
                if (left < right) c.roll = c.roll + atan2(c.behaviour, left - right, c.p.track);
            }
        }
        if (!(front == kZero && rear == kZero)) {
            F half;
            if (rear < front) {
                c.pitch = c.pitch + atan2(c.behaviour, front - rear, c.p.wheelbase);
                half = rear * kHalf;
            } else if (front < rear) {
                c.pitch = c.pitch + atan2(c.behaviour, front - rear, c.p.wheelbase);
                half = front * kHalf;
            } else {
                half = front * kHalf;
            }
            lift = lift + half;
        }
        c.pos.z = c.pos.z + lift;
    }
    body_axes(c);
    // The downward speed is limited to the vertical part of the velocity in the ground plane.
    F along{}, side{};
    along = along + c.ground_fwd.x * c.vel.x, side = side + c.ground_lat.x * c.vel.x;
    along = along + c.ground_fwd.y * c.vel.y, side = side + c.ground_lat.y * c.vel.y;
    along = along + c.ground_fwd.z * c.vel.z, side = side + c.ground_lat.z * c.vel.z;
    F floor = along * c.ground_fwd.z + side * c.ground_lat.z;
    if (kZero < floor) floor = kZero;
    if (c.vel.z < floor) c.vel.z = floor;
}

void ground_frame(Car& c) {
    const F g0 = c.w[0].ground, g1 = c.w[1].ground, g2 = c.w[2].ground, g3 = c.w[3].ground;
    const F w = c.p.rear_weight;
    c.ground_pos.x = c.pos.x;
    c.ground_pos.y = c.pos.y;
    const F pitch_rise = ((g0 + g1) - (g2 + g3)) * kHalf;
    const F u = g0 + (g2 - g0) * w, v = g1 + (g3 - g1) * w;
    c.ground_pos.z = (u + v) * kHalf;
    c.ground_pitch = atan2(c.behaviour, pitch_rise, c.p.wheelbase);
    c.ground_roll = atan2(c.behaviour, u - v, c.p.track);
    euler_axes(c.ground_pitch, c.ground_roll, c.heading, c.ground_fwd, c.ground_lat, c.ground_up);
}

} // namespace gt3r::sim::exact
