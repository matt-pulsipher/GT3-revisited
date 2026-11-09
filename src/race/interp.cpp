#include "race/interp.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gt3r::race {

int FixedStep::advance(double frame_dt, int max_ticks) {
    accum_ += std::max(0.0, frame_dt);
    int n = int(accum_ / step_);
    if (n > max_ticks) {
        n = max_ticks;
        accum_ = std::fmod(accum_, step_);
    } else {
        accum_ -= n * step_;
    }
    return n;
}

int FixedStep::follow(double clock_ticks, long long& done, int max_ticks) {
    const long long due = (long long)std::floor(clock_ticks);
    const int n = int(std::clamp<long long>(due - done, 0, max_ticks));
    done += n;
    // Behind (a stall past max_ticks): the newest state; ahead (the clock was set back): the last.
    accum_ = std::clamp(clock_ticks - double(done), 0.0, 1.0) * step_;
    return n;
}

double FramePacer::step(double frame_dt) {
    if (frame_dt <= 0) return 0;
    if (frame_dt > 0.25) { // a stall (a load, the window hidden): as it is, then start over
        period_ = 0;
        return frame_dt;
    }
    if (period_ <= 0) { // the first frame: the period starts from it
        period_ = frame_dt, owed_ = 0, behind_ = 0, spread_ = 0, off_ = 0, off_time_ = 0, frames_ = 1, short_ = 0, locked_ = false;
        return frame_dt;
    }
    // The period: the mean of the frames that took about one, for the first 64 (then the rest's
    // own lean trims it, below: an average of single frames moves with the readings' wander).
    const bool about_one = frame_dt > period_ * 0.6 && frame_dt < period_ * 1.5;
    if (about_one) {
        off_ = 0, off_time_ = 0;
        if (frames_ < 64) period_ += (frame_dt - period_) / ++frames_;
    } else if (off_time_ += frame_dt; ++off_ >= 8) { // another refresh rate or frame cap
        period_ = off_time_ / off_, owed_ = 0, off_ = 0, off_time_ = 0, frames_ = 1;
    }
    // The period's steps: whole periods, two once the clock is three quarters of a period over
    // one (a late reading is not yet a missed refresh); the rest (the clock's wander, and the
    // average's own error) goes in by a tenth a frame.
    const double owed_before = owed_;
    owed_ += frame_dt;
    const double whole = std::max(1.0, std::floor(owed_ / period_ + 0.25)) * period_;
    const double rest = owed_ - whole, paced = whole + rest * 0.1;
    owed_ = std::clamp(owed_ - paced, -period_, period_);
    if (about_one && frames_ >= 64) period_ += std::clamp(rest, -0.3 * period_, 0.3 * period_) * 0.002;
    // On a steady refresh the rest stays a small part of a period (the readings wander about
    // the refreshes, now and then by a third of one); where frames keep to no refresh (variable
    // refresh at a changing frame rate, a new rate not yet learned) it is large most frames.
    // The clock is the better step then, as it is where the readings wander too far to tell.
    spread_ += ((std::fabs(rest) > 0.2 * period_ ? 1.0 : 0.0) - spread_) / 32.0;
    // Two frames in a row well under a period: a faster rate (a reading that wanders early is
    // followed by a late one).
    short_ = frame_dt < period_ * 0.6 ? short_ + 1 : 0;
    if (short_ >= 2) spread_ = 1;
    if (locked_ ? spread_ > 0.4 : spread_ < 0.15) locked_ = !locked_;
    // Either way back to the clock (or locked, to the period's steps) by a tenth a frame.
    const double out = locked_ ? paced + (behind_ - owed_before) * 0.1 : frame_dt + behind_ * 0.1;
    behind_ += frame_dt - out;
    if (std::fabs(behind_) > 0.25) return out + std::exchange(behind_, 0.0);
    return out;
}

sim::Quat normalize(const sim::Quat& q) {
    float l = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (l <= 0) return {};
    return {q.w / l, q.x / l, q.y / l, q.z / l};
}

sim::Quat slerp(const sim::Quat& a, const sim::Quat& b0, float t) {
    sim::Quat b = b0;
    float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (d < 0) b = {-b.w, -b.x, -b.y, -b.z}, d = -d;
    if (d > 0.9995f)
        return normalize({a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
    float th = std::acos(std::clamp(d, -1.0f, 1.0f));
    float s = std::sin(th);
    float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return {a.w * wa + b.w * wb, a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb};
}

sim::Quat quat_mul(const sim::Quat& a, const sim::Quat& b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

sim::Vec3 rotate(const sim::Quat& q, const sim::Vec3& v) {
    sim::Vec3 u{q.x, q.y, q.z};
    sim::Vec3 t = u.cross(v) * 2.0f;
    return v + t * q.w + u.cross(t);
}

sim::Quat quat_from_yaw(float yaw) { return {std::cos(yaw * 0.5f), 0, std::sin(yaw * 0.5f), 0}; }

namespace {
sim::Vec3 lerp(const sim::Vec3& a, const sim::Vec3& b, float t) { return a + (b - a) * t; }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
} // namespace

float spin_change(const sim::WheelState& a, const sim::WheelState& b) {
    constexpr float kTwoPi = 6.2831853f;
    const float d = b.spin_angle - a.spin_angle;
    const float e = std::remainder(d - b.spin_step, kTwoPi);
    if (std::fabs(e) < 1e-2f) return b.spin_step + e; // b one tick after a: the tick's own turn
    return std::remainder(d, kTwoPi);
}

sim::VehicleState interpolate(const sim::VehicleState& a, const sim::VehicleState& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    sim::VehicleState r = b;
    r.position = lerp(a.position, b.position, t);
    r.orientation = slerp(a.orientation, b.orientation, t);
    r.velocity = lerp(a.velocity, b.velocity, t);
    r.angular_velocity = lerp(a.angular_velocity, b.angular_velocity, t);
    r.engine_rpm = lerp(a.engine_rpm, b.engine_rpm, t);
    r.throttle = lerp(a.throttle, b.throttle, t);
    r.speed_kmh = lerp(a.speed_kmh, b.speed_kmh, t);
    r.steer = lerp(a.steer, b.steer, t);
    for (size_t i = 0; i < r.wheels.size(); ++i) {
        const sim::WheelState &wa = a.wheels[i], &wb = b.wheels[i];
        sim::WheelState& w = r.wheels[i];
        w.contact = lerp(wa.contact, wb.contact, t);
        w.steer_angle = lerp(wa.steer_angle, wb.steer_angle, t);
        w.spin_angle = wa.spin_angle + spin_change(wa, wb) * t;
        w.angular_velocity = lerp(wa.angular_velocity, wb.angular_velocity, t);
        w.suspension = lerp(wa.suspension, wb.suspension, t);
        w.flap = lerp(wa.flap, wb.flap, t);
        w.camber = lerp(wa.camber, wb.camber, t);
        w.brake_heat = lerp(wa.brake_heat, wb.brake_heat, t);
    }
    return r;
}

} // namespace gt3r::race
