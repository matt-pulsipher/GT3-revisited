// The AI's lines and speed plan; see tick.h. (with the
// point helpers).

#include "sim/exact/course_query.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

#include "base/ee_double.h"
#include "base/ee_math.h"
#include "sim/exact/ai_course.h"

namespace gt3r::sim::exact {

namespace {

const F kZero = F::raw(0), kHalf = F::raw(0x3F000000), kOne = F::raw(0x3F800000);
const F kKmh = F::raw(0x3E8E38E4);       // 1 / 3.6
const F kCornerMin = F::raw(0x40B1C71D); // 20 km/h
const F kG = F::raw(0x411CCCCD);         // 9.8
const F kBrakeMin = F::raw(0x3DCCCCCD);  // 0.1
// -+10 km/h (ai_throttle_for)
const F kSpeedBelow = F::raw(0x3F0E38E4); // 2 km/h

const std::vector<AiPoint>& line_points(const AiCourse& k, const Driver& d) { return k.lines[d.line]; }

// The next point index, 0 when it is the count (the point the braking distance is measured to).
int braking_point(int next, int count) { return next != count ? next : 0; }

} // namespace

void ai_begin(Car& c) {
    Driver& d = c.ai;
    const F w = d.blend;
    d.steer = c.steer_cmd;
    d.throttle = kZero;
    const F zero = d.throttle;
    d.brake = kZero;
    d.reverse = 0, d.xa9 = 0;
    d.shift_lock = 0;
    if (zero < w) {
        const F left = w - c.dt;
        d.blend = left;
        if (left < zero) d.blend = zero;
    }
}

F ai_corner_speed(const Car& c, const AiCourse& k, const WearParams& cp, int i) {
    const Driver& d = c.ai;
    const AiPoint& pt = line_points(k, d)[size_t(i)];
    const F w = d.front_weight;
    const s8 s = pt.surface;
    const F r = pt.radius;
    const TireRecord& rf = tire_record(c.p, 0, u8(s));
    const TireRecord& rr = tire_record(c.p, 2, u8(s));
    std::array<F, 2> mu;
    mu[0] = (w * rf.mu[size_t(s)]) * rf.grip;
    mu[1] = ((kOne - w) * rr.mu[size_t(s)]) * rr.grip;
    F a;
    if (!(cp.max == kZero) && !(c.x535 & 0x10)) {
        a = kZero;
        for (size_t j = 0; j < 4; ++j) a = a + (c.w[j].wear_grip * mu[j >> 1]) * kHalf;
    } else {
        a = mu[0] + mu[1];
    }
    // the bank divides only when it leans into the arc
    if (kZero <= r) {
        a = a * r;
        if (pt.bank < kZero) a = a / (ee::sin(pt.bank) + kOne);
    } else {
        a = a * -r;
        if (kZero < pt.bank) a = a / (kOne - ee::sin(pt.bank));
    }
    // v = sqrt(a (1 + k v^2)), three times from v = 0
    F v, q = kOne;
    for (int n = 0; n < 3; ++n) {
        v = sqrt(a * q);
        q = ((d.downforce * v) * v) + kOne;
    }
    const s32 trim = s32(d.trim) * 10;
    const F scaled = d.speed_scale * v;
    // (2002 Tokyo-Geneva's 1 / 3.6 and 20 km/h one and two units lower: Europe)
    const F kmh = folded(c.behaviour, kKmh.bits, 0x3E8E38E3), corner_min = folded(c.behaviour, kCornerMin.bits, 0x40B1C71B);
    F off = d.speed_offset + ee::int_to_f(trim) * kmh;
    off = off + ee::int_to_f(pt.speed_adjust) * kmh;
    F out = scaled + off;
    if (c.p.top_speed < out) out = c.p.top_speed;
    else if (out < corner_min) out = corner_min;
    return out;
}

void ai_next_segment(Car& c, const AiCourse& k, const WearParams& cp) {
    Driver& d = c.ai;
    const std::vector<AiPoint>& p = line_points(k, d);
    const int count = int(p.size());
    d.mode = d.next_kind;
    d.v_cur = d.v_next;
    const int n = d.next;
    d.next_kind = n != count ? p[size_t(n)].kind : p[size_t(count - 1)].kind;
    if (d.next_kind == 0) d.v_next = c.p.top_speed;
    else if (d.next_kind < 3) d.v_next = ai_corner_speed(c, k, cp, n != count ? n : count - 1);
}

void ai_replan(Car& c, const AiCourse& k, const WearParams& cp) {
    Driver& d = c.ai;
    const std::vector<AiPoint>& p = line_points(k, d);
    const int count = int(p.size());
    const F s = c.f3e4;
    d.next = u8(count);
    if (count > 0) {
        if (s < p[0].dist) d.next = 0;
        else
            for (int v = 1; v < count; ++v)
                if (s < p[size_t(v)].dist) {
                    d.next = u8(v);
                    break;
                }
    }
    int cur = int(d.next) + count - 1; // the point before the next one, (next - 1) mod count
    while (!(cur < count)) cur -= count;
    d.next_kind = p[size_t(cur)].kind;
    d.v_next = d.next_kind == 0 ? c.p.top_speed : ai_corner_speed(c, k, cp, cur);
    ai_next_segment(c, k, cp);
}

void ai_set_line(Car& c, const AiCourse& k, const WearParams& cp, int line) {
    c.ai.prev_line = u8(line);
    c.ai.line = u8(line);
    c.ai.blend = kZero;
    ai_replan(c, k, cp);
}

bool ai_request_line(Car& c, const AiCourse& k, const WearParams& cp, int line, bool at_once) {
    const int r = k.resolve(line);
    if (r == -1) return false;
    if (at_once) {
        ai_set_line(c, k, cp, r);
    } else {
        c.ai.prev_line = c.ai.line;
        c.ai.line = u8(r);
        c.ai.blend = kOne;
        ai_replan(c, k, cp);
    }
    return true;
}

F ai_braking(Car& c, const AiCourse& k, const WearParams& cp, StopZone* zone, const CourseQuery* course) {
    Driver& d = c.ai;
    const bool later = !gt3_program(c.behaviour.rule(Rule::AiBraking));
    F mult = kOne;
    d.status = 0;
    if (d.line == 4) return mult;
    const std::vector<AiPoint>& p = line_points(k, d);
    const int count = int(p.size());
    const F lap = k.lap_length;
    // the next point index steps at most once a tick [ai §4.1]
    const int n = d.next;
    bool changed = false;
    if (n == 0 && lap * kHalf < c.f3e4) {
        d.next = u8(count);
        changed = true;
    } else if (n < count && p[size_t(n)].dist <= c.f3e4) {
        d.next = u8(n + 1);
        changed = true;
    } else if (n == count && c.f3e4 < lap * kHalf) {
        d.next = 0;
        changed = true;
    } else if (n != 0 && c.f3e4 < p[size_t(n - 1)].dist) {
        d.next = u8(n - 1);
        changed = true;
    }
    if (changed) ai_next_segment(c, k, cp);
    if (u8(d.mode - 3) < 2) { // braking already
        d.status = 2;
    } else {
    // (Rule::AiBraking, the later builds: a car that owes a stop brakes to its point in the stop zone, a quarter of the
    // zone further on for every two places, when the point lies in the segment driven and ahead of the car)
    u8 kind = d.next_kind;
    F vn = d.v_next;
    bool stop = false;
    if (later && zone && course && zone->active == 1 && d.stop == 0 && (u8(u8(c.control) - 1) < 2 || c.driver_kind != 0) &&
        zone->owed(c) > 0 && count > 0) {
        static constexpr u32 kRow[4] = {0x3F400000, 0x3F000000, 0x3E800000, 0};
        const F at = zone->base + (zone->spacing * F::raw(kRow[size_t((s32(c.rank) - 1) / 2) & 3]));
        const F s = c.f3e4;
        const int m = d.next;
        if (m == count) stop = (p[size_t(count - 1)].dist < at && s < at) || at < p[0].dist;
        else if (m == 0) stop = at < p[0].dist && s < at;
        else stop = p[size_t(m - 1)].dist < at && at < p[size_t(m)].dist && s < at;
        if (stop) {
            vn = kZero;
            kind = 1;
            F x, y, h;
            k.evaluate(d.line, d.next, at, x, y, h);
            const F g = course->progress(x, h, -y, c.road_seg);
            if (zone->has(c.index)) zone->x[c.index] = x, zone->y[c.index] = y, zone->dist[c.index] = g;
        }
    }
    if (kind != 0 && vn < c.speed) {
        const F v = c.speed;
        const F dv = v - vn, sv = v + vn;
        const F v2 = sv * dv;
        const int at = braking_point(d.next, count);
        const s8 surf = p[size_t(at)].surface;
        F mu = tire_record(c.p, 0, u8(surf)).mu[size_t(surf)];
        const TireRecord& rf = tire_record(c.p, 0, u8(surf));
        const F load = (c.p.weight * kHalf) / c.p.width[0];
        mu = mu * lookup_bisect(rf.load_curve, load);
        mu = mu * rf.brake_peak;
        mu = mu * c.p.cam_offset[0];
        F grip;
        if (c.x535 & 0x10) grip = mu;
        else grip = (mu * (c.w[0].wear_grip + c.w[1].wear_grip)) * kHalf;
        F coef = c.p.brake_coef;
        const F floor = kOne / ((grip + grip) * folded(c.behaviour, kG.bits, 0x411CCCCC)); // (Europe's)
        if (coef < floor) coef = floor;
        coef = coef * (kOne - ee::sin(p[size_t(braking_point(d.next, count))].slope));
        const s8 adj = p[size_t(braking_point(d.next, count))].brake_adjust;
        F dist = v2 * coef;
        const Behaviour& h = c.behaviour; // (the build's soft doubles: build_math.h)
        F adjust = narrow(h, add(h, mul(h, widen(h, ee::int_to_f(adj)), ee::D::raw(0x3F847AE147AE147Bull)), ee::D::raw(0x3FF0000000000000ull)));
        if (const F lo = folded(c.behaviour, kBrakeMin.bits, 0x3DCCCCCC); adjust < lo) adjust = lo;
        const F kb = d.brake_factor;
        dist = dist * adjust;
        if (kb < kOne) dist = dist * kb;
        if (d.cooldown == 0 && kZero < d.contact) dist = dist + d.contact * F::raw(0x41A00000); // 20
        const AiPoint& q = p[size_t(braking_point(d.next, count))];
        F qx = q.x, qy = q.y;
        if (stop && zone->has(c.index)) qx = zone->x[c.index], qy = zone->y[c.index];
        const F ex = qx - c.pos.x, ey = qy - c.pos.y;
        const F to = sqrt((ex * ex) + (ey * ey));
        if (dist < to) {
            F soft = kZero;
            if (kOne < kb) soft = dist * (kb - kOne);
            const F margin = to - dist;
            if (margin < soft + F::raw(0x42480000)) d.status = 1; // 50 m
            if (kZero < soft && margin < soft) mult = margin / soft;
        } else {
            d.mode = d.mode != 0 ? 4 : 3;
            d.status = 2;
            if (stop) d.stop = 1;
        }
    }
    }
    if (d.line != 3) {
        // (the later builds: a throttle scale below 1 scales the full-throttle torque's share of the engine's torque)
        const F ts = d.throttle_scale;
        if (later && ts < kOne) {
            const F sum = c.torque_wot + c.torque_friction;
            if (sum == kZero) mult = mult * ts;
            else mult = mult * (((ts * c.torque_wot) + c.torque_friction) / sum);
        } else mult = mult * ts;
    }
    return mult;
}

F ai_lookahead(const Driver& d, F v) {
    if (v <= kZero) return d.look_min;
    const F f = d.look_speed * v;
    if (kOne <= f) return d.look_max;
    return d.look_min + f * (d.look_max - d.look_min);
}

F ai_target(const Car& c, const AiCourse& k, int line, int hint, F look, F& dx, F& dy, bool* missed) {
    F x, y, h;
    for (int i = 0; i < 10; ++i) {
        const F s = c.f3e4 + look;
        const bool ok = line == 4 ? k.evaluate_stop(4, s, x, y, h) >= 0 : k.evaluate(line, hint, s, x, y, h);
        if (!ok && missed) *missed = true;
        dx = x - c.pos.x;
        dy = y - c.pos.y;
        const F dist = sqrt((dx * dx) + (dy * dy));
        if (F::raw(0x40A00000) <= dist) break; // 5 m
        if (i < 9) look = look + kOne;
    }
    return look;
}

F ai_throttle_for(F v, F vt, F base, F gain, bool toward_zero) {
    // (2002 Tokyo-Geneva's -+10 km/h two units smaller: Europe)
    const F kSpeedLo = F::raw(toward_zero ? 0xC031C71B : 0xC031C71D), kSpeedHi = F::raw(toward_zero ? 0x4031C71B : 0x4031C71D);
    F dv = v - vt;
    if (dv < kSpeedLo) dv = kSpeedLo;
    else if (kSpeedHi < dv) dv = kSpeedHi;
    const F t = base + gain * dv;
    if (kOne < t) return kOne;
    if (t < kZero) return kZero;
    return t;
}

void ai_speed_control(Car& c, F vt, F gain) {
    Driver& d = c.ai;
    const bool z = toward_zero(c.behaviour);
    const F below = z ? F::raw(0x3F0E38E3) : kSpeedBelow; // (2002 Tokyo-Geneva's: Europe)
    const F t = ai_throttle_for(c.speed, vt - below, F::raw(0x3F400000), gain, z); // base 0.75
    d.throttle = t;
    if (t == kZero && vt < c.speed) d.brake = kOne;
    if (d.throttle == kOne) return;
    if (d.throttle == kZero) return;
    d.shift_lock = pal_rate(c) ? 10 : 12;
}

} // namespace gt3r::sim::exact
