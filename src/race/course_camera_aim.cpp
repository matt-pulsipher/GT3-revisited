#include "race/course_camera_aim.h"

#include "base/ee_float.h"
#include "base/ee_math.h"

#include <algorithm>
#include <cmath>

namespace gt3r::race {

namespace {

using ee::F;

constexpr float kPi = 3.14159265f;
constexpr float kToDeg = 57.29578f;

F f(float x) { return F(x); }

// A - b, brought into -180 .. 180 by one turn.
F diff(F a, F b) {
    F d = a - b;
    if (f(180.0f) < d) d = d - f(360.0f);
    if (d < f(-180.0f)) d = d + f(360.0f);
    return d;
}

// Into 0 .. 360 by whole turns (truncated), from below as 360 - (|a| mod 360).
F wrap360(F a) {
    const F turn = f(360.0f);
    if (turn < a) a = a - ee::int_to_f(ee::to_int(a / turn)) * turn;
    if (a < f(0.0f)) {
        a = -a;
        a = a - ee::int_to_f(ee::to_int(a / turn)) * turn;
        a = turn - a;
    }
    return a;
}

// On the curve (1, 0, 2): ((0 t - 0.5) t + 1) t 2, t clamped to 0 .. 1.
F ease(F t) {
    if (t < f(0.0f)) t = f(0.0f);
    if (f(1.0f) < t) t = f(1.0f);
    F p = f(0.0f) * t;
    p = p + f(-0.5f);
    p = p * t;
    p = p + f(1.0f);
    p = p * t;
    return p * f(2.0f);
}

} // namespace

float angle_diff(float a, float b) { return diff(F(a), F(b)).host(); }
float angle_wrap360(float a) { return wrap360(F(a)).host(); }
float aim_yaw(const render::Vec3& v) { return (kPi - std::atan2(v.x, v.z)) * kToDeg; }
float aim_pitch(const render::Vec3& v) { return -std::atan2(v.y, std::hypot(v.x, v.z)) * kToDeg; }

render::Vec3 aim_direction(float yaw, float pitch) {
    const float a = kPi - yaw / kToDeg, p = pitch / kToDeg;
    return {std::cos(p) * std::sin(a), -std::sin(p), std::cos(p) * std::cos(a)};
}

void OperatorNoise::reset(u32 seed) {
    x = target = v = 0;
    frames = calls = 0;
    rng.x = seed;
}

void OperatorNoise::step(float amp_in) {
    // A draw every frame; every third frame a chance of a new target.
    rng.next();
    const F r = F(rng.rand01());
    if (calls < 2) {
        ++calls;
    } else {
        calls = 0;
        F amp = F(amp_in);
        if (amp < f(0.0f)) amp = -amp;
        if (f(1.0f) < amp) amp = f(1.0f);
        const F p = amp * F::raw(0xBE2147A8u) + F::raw(0x3F7E4B17u); // -0.1575, 0.99333
        if (p < r) {
            rng.next();
            (void)rng.rand01(); // a draw whose value is never used
            const F r3 = F(rng.rand01());
            frames = 0;
            target = (((r3 * f(0.8f)) + f(0.2f)) * f(256.0f) - f(128.0f)).host();
        }
    }
    // v += (60 n (target - x) - 10 v) / 60, x += v / 60 (n the frames, at least 1).
    if (frames <= 0) ++frames;
    const F n = ee::int_to_f(frames);
    F a = n * f(60.0f);
    const F e = F(target) - F(x);
    const F b = F(v) * f(-10.0f);
    a = a * e;
    a = a + b;
    a = a / f(60.0f);
    const F nv = F(v) + a;
    v = nv.host();
    x = (F(x) + nv / f(60.0f)).host();
}

void AimFilter::push(float theta) {
    // The history (128 long; only the last two count) and its newest change.
    if (count < 128) ++count;
    const float prev = newest;
    newest = theta;
    delta = count >= 2 ? diff(F(theta), F(prev)).host() : 0.0f;
}

float AimFilter::lead(float k_in) const {
    const F k = F(k_in);
    const F d = F(delta) * f(5.0f);
    const F at = F(newest) + d;
    if (f(0.0f) < d) {
        const F kp = k * ease(d / (k * f(3.0f)));
        if (!(kp < f(0.0f))) return (at + kp).host();
        const F nd = -kp;
        return (at + (-k) * ease(nd / (k * f(3.0f)))).host();
    }
    if (d < f(0.0f)) {
        const F nd = -d;
        return (at + (-k) * ease(nd / (k * f(3.0f)))).host();
    }
    return (at + d).host();
}

float AimFilter::reset(float cur, float target, float k, int mode_in, float scale, u32 seed) {
    F c = F(cur);
    if (mode_in == 1) { // scale ((k 0.4) cos(cur))
        const F a = c * f(kPi) / f(180.0f);
        F off = F(k) * f(0.4f);
        off = off * ee::cos(a);
        off = F(scale) * off;
        c = c - off;
    }
    out = c.host();
    count = 0, delta = 0, newest = target; // the history holds the target
    push(out);
    v = diff(c, F(target)).host();
    noise.reset(seed);
    mode = mode_in;
    return out;
}

float AimFilter::step(float theta_in, float k_in, float scale, bool full_gain) {
    noise.step(1.0f);
    const F k = F(k_in);
    F theta = F(theta_in);
    if (mode == 1) { // ((scale 0.4) k) cos(theta)
        const F a = theta * f(kPi) / f(180.0f);
        F off = F(scale) * f(0.4f);
        off = off * k;
        off = off * ee::cos(a);
        theta = theta - off;
    }
    push(theta.host());
    const F ahead = F(lead(k_in));
    const F e0 = diff(theta, F(out));
    F e = ahead - theta;
    e = e0 + e;
    F g = (F(noise.value()) + f(1.0f)) * f(0.5f);
    g = g + f(0.2f);
    if (full_gain) g = f(1.0f);
    F a = g * f(300.0f);
    a = a * e;
    a = a + F(v) * f(-30.0f);
    a = a / f(60.0f);
    F dv = a + a;
    if (!full_gain) {
        if (f(0.0f) < dv) {
            if (f(25.0f) < dv) dv = f(25.0f);
        } else if (dv < f(-25.0f)) {
            dv = f(-25.0f);
        }
    }
    F nv = F(v) + dv;
    if (nv < f(0.0f)) {
        if (nv < f(-250.0f)) nv = f(-250.0f);
    } else if (f(250.0f) < nv) {
        nv = f(250.0f);
    }
    v = nv.host();
    out = wrap360(F(out) + nv / f(60.0f)).host();
    return out;
}

float ZoomSpring::step(float want_in, float progress) {
    const F x0 = F(x), w = F(want_in);
    want = want_in;
    F pull = (w - x0) * f(30.0f);
    if (x0 < w) pull = pull * F(progress);
    F nv = F(v) + (pull + F(v) * f(-10.0f)) / f(60.0f);
    if (f(10.0f) < nv) nv = f(10.0f);
    if (nv < f(-30.0f)) nv = f(-30.0f);
    v = nv.host();
    x = (x0 + nv / f(60.0f)).host();
    return x;
}

CourseCameraAim::Out CourseCameraAim::step(const In& in) {
    const render::Vec3 to = in.aim - in.eye;
    const float yaw_raw = aim_yaw(to), pitch_raw = aim_pitch(to);
    Out o;
    if (!started_) {
        // The shot's first frame (designer camera +0xDC): the zoom set, the filters reset. A car
        // shot's filters start turning from where the aim was a frame ago (target state +0x94:
        // the aim point without its lift, kept while the cameras follow the same car), plus
        // mode 1's yaw seed.
        started_ = true;
        const float k = (F(30.0f) / F(in.zoom)).host(); // 30 / the record's zoom
        float yaw_t = yaw_raw, pitch_t = pitch_raw;
        if (in.at_car) {
            if (!in.kind_400 && last_aim_) {
                const render::Vec3 back = *last_aim_ - in.eye;
                pitch_t = aim_pitch(back), yaw_t = aim_yaw(back);
            }
            if (in.yaw_offset) yaw_t = (F(yaw_t) + F(*in.yaw_offset)).host();
        }
        bob_.reset(in.seeds[0]);
        o.yaw = yaw_.reset(yaw_raw, yaw_t, k, 0, 1.0f, in.seeds[1]);
        o.pitch = pitch_.reset(pitch_raw, pitch_t, k, in.at_car ? 1 : 0, 1.0f, in.seeds[2]);
        o.zoom = in.wide ? *in.wide : in.zoom;
        zoom_.reset(o.zoom);
        o.target = in.eye + aim_direction(o.yaw, o.pitch) * render::length(to);
        prev_ = cur_ = o;
        return o;
    }
    o.zoom = zoom_.step(in.zoom, in.progress);
    const F k = F(30.0f) / F(o.zoom);
    if (in.at_car) {
        o.yaw = yaw_.step(yaw_raw, k.host(), 1.0f, false);
        o.pitch = pitch_.step(pitch_raw, k.host(), 1.0f, false);
        // The bob: a second spring, its odds raised by the yaw's speed, tips the pitch by up to
        // a third of min(k, 5) degrees as the camera pans.
        F pan = F(yaw_.v);
        if (f(30.0f) <= pan) pan = f(30.0f);
        if (pan <= f(-30.0f)) pan = f(-30.0f);
        bob_.step((pan / f(30.0f)).host());
        const F k5 = f(5.0f) < k ? f(5.0f) : k;
        F tip = F(bob_.value()) / f(3.0f);
        tip = tip * k5;
        tip = tip * pan;
        tip = tip / f(30.0f);
        o.pitch = (F(o.pitch) + tip).host();
    } else { // aimed at its target curves: no filter
        o.yaw = yaw_raw, o.pitch = pitch_raw;
    }
    o.target = in.eye + aim_direction(o.yaw, o.pitch) * render::length(to);
    prev_ = cur_, cur_ = o;
    return o;
}

CourseCameraAim::Out CourseCameraAim::advance(const In& in, double dt) {
    constexpr double kFrame = 1.0 / 60.0;
    if (!started_) {
        step(in);
        acc_ = 0;
    } else {
        acc_ += dt;
        for (int n = 0; acc_ >= kFrame && n < 12; ++n) {
            acc_ -= kFrame;
            // This frame's moment: acc_ seconds before now, within the last dt.
            const float t = dt > 0 ? float(std::clamp(1.0 - acc_ / dt, 0.0, 1.0)) : 1.0f;
            In at = in;
            at.eye = last_in_.eye + (in.eye - last_in_.eye) * t;
            at.aim = last_in_.aim + (in.aim - last_in_.aim) * t;
            at.zoom = last_in_.zoom + (in.zoom - last_in_.zoom) * t;
            at.progress = last_in_.progress + (in.progress - last_in_.progress) * t;
            step(at);
        }
        if (acc_ > kFrame) acc_ = 0;
    }
    last_in_ = in;
    return blend(float(acc_ * 60.0));
}

CourseCameraAim::Out CourseCameraAim::blend(float frac) const {
    const float t = std::clamp(frac, 0.0f, 1.0f);
    Out o = cur_;
    o.yaw = prev_.yaw + angle_diff(cur_.yaw, prev_.yaw) * t;
    // The pitch wraps too: just above level it is 359.x one frame and -0.x the next (the
    // filter's angle is kept in 0 .. 360, the bob's tip is added after). Blended straight, it
    // swung through 180 degrees for a frame (issue 402's flashes of sky or road).
    o.pitch = prev_.pitch + angle_diff(cur_.pitch, prev_.pitch) * t;
    o.zoom = prev_.zoom + (cur_.zoom - prev_.zoom) * t;
    return o;
}

} // namespace gt3r::race
