// Phase 3, the AI driver's step, decisions and traffic; see tick.h.

#include "base/ee_math.h"
#include "sim/exact/ai_course.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

const F kZero{}, kOne = F::raw(0x3F800000), kHalf = F::raw(0x3F000000);
const F kMinusHalf = F::raw(0xBF000000), kMinusOne = F::raw(0xBF800000), kMinusTwo = F::raw(0xC0000000);
// The look-ahead: the stop line, leaving the pit (kind 4), reversing out, turning round; the start's long look ahead.
const F kLookStop = F::raw(0x40A00000), kLookLeave = F::raw(0x41A00000), kLookReverse = F::raw(0x41200000);
const F kLookTurn = F::raw(0x42200000), kStartLook = F::raw(0x42200000), kStartMs = F::raw(0x44FA0000);
// ; each with 2002 Tokyo-Geneva's value, its compiler having folded them toward zero
// (toward_zero(), build_math.h [physics §14.2]; Europe's code, Asia's the same).
struct DecideK {
    F park_speed, park_turn;                                       // 0.25 km/h, 3 deg/s
    F theta, theta_rev, theta_speed, theta_slope, theta_span, theta_base; // 60, 10 deg; -30 km/h, 50 deg, 30 km/h, 10 deg
    F back, roll, out, round, round_speed, reverse_out, spin, rear_hit; // -2, 2, 3 km/h, 40 deg, 30 km/h, 60, 80, 45 deg
    F moving, moving_back, wrong, wrong_neg, slow, lock, lock_neg;  // 2, -2 km/h, 80, -80 deg, 5 km/h, 45, -45 deg
    F turn_neg, turn_full, turn, turn_full_pos, turn_gain;          // -20, -30, 20, 30 deg, 1.5
    F stop_speed, turn_speed, pit_margin, pit_margin2;              // 20, 50 km/h, 2 km/h
    F stop_at, stop_slow;                                           // the stop point's 1 and 10 km/h
    F blip_up, blip_down, hold_rank;                                // the start hold's
    explicit DecideK(bool z) {
        auto v = [z](u32 x, u32 y) { return F::raw(z ? y : x); };
        park_speed = v(0x3D8E38E4, 0x3D8E38E3), park_turn = v(0x3D567750, 0x3D56774E);
        theta = v(0x3F860A92, 0x3F860A90), theta_rev = v(0x3E32B8C2, 0x3E32B8C1);
        theta_speed = v(0xC1055556, 0xC1055554), theta_slope = v(0x3F5F66F3, 0x3F5F66F1);
        theta_span = v(0x41055556, 0x41055554), theta_base = v(0x3E32B8C2, 0x3E32B8C1);
        back = v(0xBF0E38E4, 0xBF0E38E3), roll = v(0x3F0E38E4, 0x3F0E38E3), out = v(0x3F555556, 0x3F555554);
        round = v(0x3F32B8C2, 0x3F32B8C1), round_speed = v(0x41055556, 0x41055554);
        reverse_out = v(0x3F860A92, 0x3F860A90), spin = v(0x3FB2B8C2, 0x3FB2B8C1), rear_hit = v(0x3F490FDB, 0x3F490FD9);
        moving = v(0x3F0E38E4, 0x3F0E38E3), moving_back = v(0xBF0E38E4, 0xBF0E38E3);
        wrong = v(0x3FB2B8C2, 0x3FB2B8C1), wrong_neg = v(0xBFB2B8C2, 0xBFB2B8C1), slow = v(0x3FB1C71D, 0x3FB1C71B);
        lock = v(0x3F490FDB, 0x3F490FD9), lock_neg = v(0xBF490FDB, 0xBF490FD9);
        turn_neg = v(0xBEB2B8C2, 0xBEB2B8C1), turn_full = v(0xBF060A92, 0xBF060A90);
        turn = v(0x3EB2B8C2, 0x3EB2B8C1), turn_full_pos = v(0x3F060A92, 0x3F060A90), turn_gain = v(0x3FC00001, 0x3FBFFFFE);
        stop_speed = v(0x40B1C71D, 0x40B1C71B), turn_speed = v(0x415E38E4, 0x415E38E2);
        pit_margin = v(0x3F0E38E4, 0x3F0E38E3), pit_margin2 = v(0x3F0E38E4, 0x3F0E38E3);
        stop_at = v(0x3E8E38E4, 0x3E8E38E3), stop_slow = v(0x4031C71D, 0x4031C71B);
        blip_up = v(0x3E4CCCCD, 0x3E4CCCCC), blip_down = v(0x3ECCCCCD, 0x3ECCCCCC), hold_rank = v(0x3B03126F, 0x3B03126E);
    }
};
const F kBlipUpRate = F::raw(0x40A00000), kBlipDownRate = F::raw(0x41200000), kHoldBase = F::raw(0x3F200000);
// 2002 Tokyo-Geneva's longer look-ahead at the start (Rule::AiDecide): 200 m over the first 5 s.
const F kStartLookLong = F::raw(0x43480000), kStartMsLong = F::raw(0x459C4000);

} // namespace

void ai_steer(Car& c, F target) { slew(c.ai.steer, target, c.ai.steer_rate * c.dt); }

void ai_park(Car& c) {
    ai_begin(c);
    ai_steer(c, kZero);
    c.ai.brake = kZero;
    c.ai.throttle = kZero;
}

void ai_stop(Car& c, F target) {
    Driver& d = c.ai;
    ai_begin(c);
    ai_steer(c, target);
    d.throttle = kZero;
    const F v = sqrt(c.vel.x * c.vel.x + c.vel.y * c.vel.y + c.vel.z * c.vel.z);
    const F w = sqrt(c.yaw_rate * c.yaw_rate + c.roll_rate * c.roll_rate + c.pitch_rate * c.pitch_rate);
    const DecideK k(toward_zero(c.behaviour));
    if (v < k.park_speed && w < k.park_turn) {
        d.brake = kZero;
        c.driver_kind = 8;
        return;
    }
    if (c.driver_kind != 5) d.brake = kOne;
}

void ai_hold(Car& c, s32 n) {
    Driver& d = c.ai;
    const F r = ee::int_to_f(c.rank);
    const DecideK k(toward_zero(c.behaviour));
    c.clutch_req = kZero;
    const s32 up = s32(c.p.d.up_rpm), rpm = c.rpm;
    if (n < (pal_rate(c) ? 11 : 13)) {
        const F q = ee::int_to_f(rpm - up) / ee::int_to_f(s32(c.p.e.rev_limit));
        F x = (kHoldBase - r * k.hold_rank) - (q + q);
        if (kOne < x) x = kOne;
        else if (x < kZero) x = kZero;
        d.throttle = x;
        return;
    }
    bool rise;
    if (c.blip == 0) {
        rise = rpm < 3 * up / 4;
        if (rise) c.blip = 1;
    } else {
        rise = rpm < 15 * up / 16;
        if (!rise) c.blip = 0;
    }
    const F dt = c.dt;
    if (rise) {
        F x = d.throttle + dt * (kBlipUpRate - r * k.blip_up);
        d.throttle = kOne < x ? kOne : x;
    } else {
        F x = d.throttle - dt * (kBlipDownRate - r * k.blip_down);
        d.throttle = x < kZero ? kZero : x;
    }
}

void ai_traffic(Car& c, AiPlan& plan) {
    Driver& d = c.ai;
    if (d.cooldown > 0) d.cooldown = s16(d.cooldown - 1);
    const u8 line = d.line;
    if (line == 4) return;
    bool lanes = true;
    const auto z = [&](int k) { return s8(d.zone[size_t(k)]); }; // front, front-left, front-right, left, right, ...
    if (line == 3) {
        lanes = false;
        if ((z(3) != 0 && z(1) != 0) || (z(4) != 0 && z(2) != 0)) d.throttle = d.throttle * kHalf;
    }
    const s8 st = d.status;
    if (st > 0 && (st < 3 || st == 5)) lanes = false;
    // (Rule::AiTraffic, the later builds: the cooldown only takes the contact away, a line change still blending keeps the
    // lanes, and a lane picked goes on to the rest with a cooldown of three seconds)
    const bool later = !gt3_program(c.behaviour.rule(Rule::AiTraffic));
    F contact = d.contact;
    if (d.cooldown > 0) {
        if (!later) return;
        contact = kZero;
    }
    if (later && kZero < d.blend) lanes = false;
    if (lanes && kZero < contact) {
        const s32 left = z(1) != 0 ? -1 : plan.lane_left(d.line, d.next, c.f3e4);
        const s32 right = z(2) != 0 ? -1 : plan.lane_right(d.line, d.next, c.f3e4);
        const s32 right_ok = right >= 0 && d.status != 4 ? right : -1;
        s32 pick;
        if (d.next_kind == 1) pick = left >= 0 ? left : right_ok;
        else if (d.next_kind == 2 && right >= 0) pick = right;
        else pick = left >= 0 && d.status != 3 ? left : right_ok;
        if (pick >= 0) {
            plan.request_line(c, pick, false);
            if (!later) {
                d.cooldown = pal_rate(c) ? 25 : 30;
                return;
            }
            d.cooldown = pal_rate(c) ? 150 : 180;
            contact = kZero;
        }
    }
    d.throttle = d.throttle * (kOne - contact);
    if (!lanes) return;
    const s8 l = z(3), r = z(4);
    if (s32(z(0)) + s32(r) + (s32(z(2)) + s32(z(1))) + s32(l) == 0) {
        plan.request_line(c, 6, false);
        return;
    }
    if ((l != 0) == (r != 0)) return;
    if (l != 0) {
        if (d.status == 4) return;
        const s32 to = plan.lane_right(d.line, d.next, c.f3e4);
        if (to != -1) plan.request_line(c, to, false);
    } else {
        if (d.status == 3) return;
        const s32 to = plan.lane_left(d.line, d.next, c.f3e4);
        if (to != -1) plan.request_line(c, to, false);
    }
}

void ai_decide(Car& c, const AiInputs& in, AiPlan& plan) {
    Driver& d = c.ai;
    const DecideK k(toward_zero(c.behaviour));
    const bool geneva = c.behaviour.rule(Rule::AiDecide) >= Program::ConceptGenevaEu;
    ai_begin(c);
    if (c.driver_kind == 7) {
        ai_stop(c, kZero);
        return;
    }
    const F scale = plan.braking(c);
    F q = kZero; // the steering saturation ratio
    s32 drive = 0; // 1 forward, -1 reverse, 0 brake
    bool brake = false;
    F nsin = -ee::sin(c.heading), cosh = ee::cos(c.heading);

    // The look-ahead and the aim point (blended with the old line's after a change), the heading error.
    F look;
    if (c.driver_kind == 3) look = kLookStop;
    else if (c.driver_kind == 4 && d.pit_limit != 0) look = kLookLeave;
    else if (d.state != 0) look = d.state == 1 ? kLookReverse : kLookTurn;
    else {
        look = ai_lookahead(d, c.speed);
        // (Rule::AiDecide, 2002 Tokyo-Geneva: a computer's car looks further ahead over the first 5 s, 200 m at the start,
        // except on dirt, with a stop zone, or at a start speed of 60 km/h or more; Europe)
        const StopZone* zone = geneva ? plan.stop_zone() : nullptr;
        if (geneva && c.control == 2 && in.course_category != 2 && !(zone && zone->active == 1) && in.start_speed < 60) {
            const s32 left = s32(5000u - in.clock / 3u);
            if (left > 0) look = look + ee::int_to_f(left) * kStartLookLong / kStartMsLong;
        } else {
            const s32 left = s32(2000u - in.clock / 3u);
            if (left > 0) look = look + ee::int_to_f(left) * kStartLook / kStartMs;
        }
    }
    F x, y;
    look = plan.target(c, d.line, d.next, look, x, y);
    if (kZero < d.blend) {
        F xo, yo;
        plan.target(c, d.prev_line, d.next, look, xo, yo);
        const F w = d.blend;
        x = x + (xo - x) * w;
        y = y + (yo - y) * w;
    }
    const F eps = angle_diff(atan2(c.behaviour, -x, y), c.heading, c.behaviour.rule(Rule::AngleWrap));
    F aeps = eps;
    if (eps < kZero) aeps = -eps;

    // The drive decision with the heading threshold, then the recovery state.
    const u8 st = d.state;
    F v = c.speed;
    F theta = k.theta;
    if (st == 1) {
        if (kZero <= v) theta = k.theta_rev;
        else if (k.theta_speed < v) theta = k.theta_base - v * k.theta_slope / k.theta_span;
    }
    if (aeps < theta || st == 2) {
        if (k.back < v) drive = 1;
        else brake = true;
    } else if (k.roll < v) brake = true;
    else drive = -1;
    if (st == 1) {
        if (aeps < theta) d.state = k.out < v ? 0 : 2;
        else if (in.hit_rear != 0) d.state = 2;
    } else if (st == 2) {
        if (aeps < k.round && k.round_speed < v) d.state = 0;
        else if (in.hit_front != 0) {
            // (Rule::AiDecide, 2002 Tokyo-Geneva: also when the line's own heading there is more than 60 degrees off the
            // car's, unwrapped; Europe, the heading)
            bool off = false;
            if (geneva) {
                F dh = plan.line_heading(c, d.line, d.next, c.f3e4) - c.heading;
                if (dh < kZero) dh = -dh;
                off = k.reverse_out < dh;
            }
            if (off || in.hit_x != 0 || k.reverse_out < aeps) d.state = 1;
        }
    } else {
        if (k.spin < aeps) d.state = 2;
        else if (in.hit_x != 0) d.state = 1;
        else if (in.hit_rear != 0 && k.rear_hit < aeps) d.state = 2;
    }
    v = c.speed;
    s32 moving = 0;
    if (k.moving < v) moving = 1;
    else if (v < k.moving_back) moving = -1;

    // The steering target.
    F target = kZero;
    if (moving != 0) {
        if (drive == 1) {
            const F amax = (tire_record(c.p, 0, c.w[0].surface).peak_slip + tire_record(c.p, 1, c.w[1].surface).peak_slip) * kHalf;
            if (d.state != 0) {
                if (k.wrong < eps) target = amax;
                else if (eps < k.wrong_neg) target = -amax;
            }
            if (target == kZero) {
                // Pure pursuit: the yaw rate of the arc to the aim point, in the velocity's direction (the heading's when slow).
                const F p2 = x * x + y * y;
                const F vx = c.vel.x, vy = c.vel.y;
                const F vxy = sqrt(vx * vx + vy * vy);
                F cs = cosh, ns = nsin;
                if (!(vxy < k.slow)) {
                    const F a = atan2(c.behaviour, -vx, vy);
                    ns = -ee::sin(a);
                    cs = ee::cos(a);
                }
                F rd = vxy * kMinusTwo * (cs * x - ns * y) / p2;
                if (c.speed < kZero) rd = -rd;
                const F r = c.yaw_rate;
                if (kZero < rd) {
                    if (r < rd) {
                        target = target + (rd - r) * d.yaw_gain;
                        if (amax < target) {
                            q = target / amax;
                            if (!dirt_course(in.course_category)) target = amax;
                        }
                    }
                } else if (rd < kZero && rd < r) {
                    const F neg = -amax;
                    target = target - (r - rd) * d.yaw_gain;
                    if (target < neg) {
                        q = -target / amax;
                        if (!dirt_course(in.course_category)) target = neg;
                    }
                }
            }
            target = target + (c.w[0].slip_dir + c.w[1].slip_dir) * kHalf;
            if (k.lock < target) target = k.lock;
            else if (target < k.lock_neg) target = k.lock_neg;
        } else if (eps < k.turn_neg) target = k.turn_full;
        else if (k.turn < eps) target = k.turn_full_pos;
        else target = eps * k.turn_gain;
        if (moving == -1) target = -target;
    }
    const u8 kind = c.driver_kind;
    if (u8(kind - 5) < 2) {
        ai_stop(c, target);
        return;
    }
    if (u8(u8(c.control) - 1) < 2 || kind != 0) ai_steer(c, target);

    // Throttle and brake.
    if (drive == 1) {
        const u8 line = d.line;
        d.pit_limit = 0;
        F aim = d.v_cur, next = d.v_next;
        u8 mode = d.mode;
        if (line == 4) { // the pit stop line: 20 km/h, braking from a straight
            aim = next = k.stop_speed;
            mode = 3;
            d.pit_limit = 1;
        } else if (line == 3) { // the pit lane, by the stop line's points
            const F lap = plan.lap_length();
            F s = c.f3e4;
            if (s < lap * kHalf) s = s + lap;
            if (s < plan.stop_distance(1)) {
            } else if (s < plan.stop_distance(2)) next = d.pit_speed + k.pit_margin;
            else if (s < plan.stop_distance(plan.stop_count() - 3)) {
                aim = next = d.pit_speed + k.pit_margin2;
                mode = 1;
                d.pit_limit = 1;
            }
        } else {
            // (Rule::AiDecide, the later builds: the stop point. Heading for it (1): no target at 10 km/h or more and within
            // half a metre, 10 km/h below that within 20 m, given up further away; standing on it (2) once under 1 km/h
            // within half a metre, until the race flow has counted the stop)
            if (!gt3_program(c.behaviour.rule(Rule::AiDecide)) && d.stop != 0) {
                StopZone* zone = plan.stop_zone();
                F t = kZero;
                if (d.stop == 1) {
                    F s_stop = x; // (the program's stack slot when the conductor has no point)
                    if (zone && zone->has(c.index)) s_stop = zone->dist[c.index];
                    F dist = s_stop - c.f3e4;
                    const F lap = plan.lap_length(), half = lap * kHalf;
                    if (half < dist) dist = dist - lap;
                    else if (s_stop < -half) dist = dist + lap;
                    const F v2 = sqrt(c.speed * c.speed + c.lat_speed * c.lat_speed);
                    if (dist < kHalf) {
                        if (v2 < k.stop_at) d.stop = 2;
                    } else if (v2 < k.stop_slow) {
                        if (dist < F::raw(0x41A00000)) t = k.stop_slow;
                        else d.stop = 0;
                    }
                } else if (d.stop == 2) {
                    if (!zone || zone->owed(c) == 0) d.stop = 0;
                }
                if (d.stop != 0) aim = next = t;
            }
            if (d.state == 2) aim = k.turn_speed;
        }
        F thr;
        bool skip_clamp = false;
        if (d.state == 2) {
            plan.speed_control(c, aim, kMinusHalf);
            d.arc_ticks = 0;
            thr = d.throttle;
            skip_clamp = true;
        } else if (mode >= 5) thr = d.throttle;
        else if (mode == 0) {
            d.throttle = kOne;
            d.arc_ticks = 0;
            thr = kOne;
        } else if (mode == 3) {
            plan.speed_control(c, next, kMinusTwo);
            d.arc_ticks = 0;
            thr = d.throttle;
        } else if (mode == 4) {
            plan.speed_control(c, next, kMinusOne);
            thr = d.throttle;
            d.arc_ticks = u16(d.arc_ticks + 1);
        } else { // the arc modes 1 / 2: the corner bonus after its delay (not on the pit surface)
            if (!(c.x535 & 0x10) && !(s32(d.arc_ticks) < d.bonus_ticks)) aim = aim + d.corner_bonus;
            plan.speed_control(c, aim, kMinusHalf);
            thr = d.throttle;
            if (d.status == 0) d.status = !(thr == kOne) ? 5 : mode == 1 ? 4 : 3;
            d.arc_ticks = u16(d.arc_ticks + 1);
        }
        if (!skip_clamp && d.arc_ticks >= (pal_rate(c) ? 5001 : 6001)) d.arc_ticks = pal_rate(c) ? 5000 : 6000; // 100 s
        d.throttle = thr * scale;
        if (s32(5000 + 100 * s32(c.rank)) < s32(in.clock / 3u)) ai_traffic(c, plan);
    } else {
        if (drive != -1) d.throttle = kZero;
        else {
            d.reverse = 1, d.xa9 = 0;
            d.throttle = kOne;
        }
        d.brake = brake ? kOne : kZero;
        d.arc_ticks = 0;
    }

    // The steering saturation: the throttle cut and the brake for a wanted slip angle beyond the tyres' peak.
    if (kZero < q && !(c.gear < 2)) {
        const F e = q - kOne;
        F k = kOne - d.sat_throttle * e;
        if (k < kZero) k = kZero;
        d.throttle = d.throttle * k;
        const F b = d.brake + e * d.sat_brake;
        d.brake = kOne < b ? kOne : b;
    }
}

void ai_step(Car& c, const AiInputs& in, AiPlan& plan) {
    c.handbrake = kZero;
    c.reverse_req = 0;
    c.shift_req = 0;
    if (c.race_hold != 0 || c.hold != 0) {
        ai_hold(c, c.race_hold != 0 ? in.start_count : s32(c.hold));
        set_brake_pedal(c, kZero);
        c.steer_cmd = kZero;
        return;
    }
    if (c.driver_kind == 8) {
        c.clutch_req = kZero;
        ai_park(c);
        return;
    }
    if (c.clutch == 0) c.clutch_req = 3 * s32(c.p.d.up_rpm) / 4 < s32(c.rpm) ? kOne : kZero;
    else c.clutch_req = s32(c.rpm) < s32(c.p.e.idle) ? kZero : kOne;
    ai_decide(c, in, plan);
}

void ai_gear_hint_clear(Driver& d) {
    d.gear_braking = 0;
    d.gear_hint = -1;
}

void ai_gear_hint(Car& c) {
    Driver& d = c.ai;
    ai_gear_hint_clear(d);
    F v = d.v_next;
    if (d.mode == 4 && !(d.v_next < d.v_cur)) v = d.v_cur;
    if (c.speed < v) return;
    if (d.mode == 3 || d.mode == 4) d.gear_braking = 1;
    d.gear_hint = s8(gear_for_speed(c, v));
}

void ai_update(Car& c, AiPlan& plan) {
    plan.braking(c);
    ai_gear_hint(c);
}

void driver_input(Car& c, const PadConstants& k, const PadFrame& f, u8 course_kind, const AiInputs& in, AiPlan& plan) {
    c.full_throttle = 1;
    const s8 src = c.control;
    if (src == 0) {
        pad_hud(c, f);
        if (c.driver_kind == 0 || c.hold != 0) {
            pad_input(c, k, f, course_kind);
            ai_update(c, plan);
        } else {
            ai_step(c, in, plan);
            ai_gear_hint(c);
            ai_controls(c);
            ai_steer_pct(c);
        }
    } else if (src == 1) {
        pad_hud(c, f);
        ai_step(c, in, plan);
        ai_controls(c);
        ai_steer_pct(c);
    } else if (src == 2) {
        ai_step(c, in, plan);
        ai_controls(c);
        ai_steer_pct(c);
    }
}

F CoursePlan::braking(Car& c) { return ai_braking(c, k_, cp_, zone_, course_); }
F CoursePlan::target(const Car& c, int line, int hint, F look, F& dx, F& dy) { return ai_target(c, k_, line, hint, look, dx, dy); }
F CoursePlan::line_heading(const Car&, int line, int hint, F s) { return ai_line_heading(k_, line, hint, s); }
void CoursePlan::speed_control(Car& c, F target, F gain) { ai_speed_control(c, target, gain); }
bool CoursePlan::request_line(Car& c, int line, bool at_once) { return ai_request_line(c, k_, cp_, line, at_once); }
void CoursePlan::replan(Car& c) { ai_replan(c, k_, cp_); }
s32 CoursePlan::lane_left(int line, int hint, F s) { return k_.lane_left(line, hint, s); }
s32 CoursePlan::lane_right(int line, int hint, F s) { return k_.lane_right(line, hint, s); }
F CoursePlan::lap_length() { return k_.lap_length; }
s32 CoursePlan::stop_count() { return s32(k_.lines[4].size()); }
F CoursePlan::stop_distance(s32 k) { // the program reads past the line for an index outside it (a line of fewer than 3 points)
    return k >= 0 && size_t(k) < k_.lines[4].size() ? k_.lines[4][size_t(k)].dist : F();
}

} // namespace gt3r::sim::exact
