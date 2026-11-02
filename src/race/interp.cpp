#include "race/interp.h"

#include <algorithm>
#include <cmath>

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
