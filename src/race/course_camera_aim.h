#pragma once

// The course camera's operator (presentation §H6a): how GT3's replay course shots that look at
// the car aim and zoom. The camera is not locked on the car: each of its two angles (yaw and
// pitch) follows the car through a filter that leads a moving car, turns with a limited
// acceleration and speed, and whose gain wanders with a random spring, so the car drifts about
// the picture and the camera catches up late, like a hand-held TV camera. The pitch also aims a
// little high (the car sits below the middle) and bobs while the camera pans. The zoom moves
// with a spring that zooms out faster than in.
//
// All of it steps at 60 Hz, in degrees, in the EE's order of operations (the designer camera
// and its helpers).

#include "base/game_random.h"
#include "render/math3d.h"

#include <cstdint>
#include <optional>

namespace gt3r::race {

// The operator's random spring (reset, step, pick, value):
// a value chasing random targets in -76.8 .. 128 (shown / 128).
struct OperatorNoise {
    float x = 0, target = 0, v = 0;
    short frames = 0, calls = 0;
    GameRandom rng;
    void reset(u32 seed);
    // One frame; `amp` in -1 .. 1 raises the odds of a new target (0.67 % at 0, 16.4 % at 1).
    void step(float amp);
    float value() const { return x * (1.0f / 128.0f); }
};

// One angle's filter (reset, step, the lead).
struct AimFilter {
    float out = 0;     // the angle shown (0 .. 360)
    float v = 0;       // its speed (degrees per second, |v| <= 250)
    float newest = 0;  // the last target angle
    float delta = 0;   // its change over the last frame (0 until two are known)
    int count = 0;     // target angles known (at most 128)
    int mode = 0;      // 1: aim 0.4 k cos(angle) degrees below the target (the pitch)
    OperatorNoise noise;

    // The shot's first frame: shows `cur` (less the mode-1 offset) and starts turning at
    // wrap(shown - target) degrees per second. Returns the angle shown.
    float reset(float cur, float target, float k, int mode, float scale, u32 seed);
    // One frame on the target angle `theta`; k = 30 / zoom; `full_gain`: the gain fixed at 1
    // and no acceleration limit (the camera target's type 3 or 5, never seen in replays).
    float step(float theta, float k, float scale, bool full_gain);
    // Where the car will be: theta + 5 delta, led on by up to k degrees (2u - u^2,
    // u = min(5 |delta| / 3k, 1)).
    float lead(float k) const;
    void push(float theta);
};

// The zoom's spring (reset, step): towards `want`, zooming in at most 10 a
// second (scaled by `progress`, the aim's move between cars), out at most 30.
struct ZoomSpring {
    float x = 1, want = 1, v = 0;
    void reset(float z) { x = want = z, v = 0; }
    float step(float want, float progress);
};

// GT3's angle helpers: yaw = 180 - atan2(x, z), pitch = -atan2(y, |xz|) (degrees; positive
// pitch looks down), the -180 .. 180 difference and the 0 .. 360 wrap.
float aim_yaw(const render::Vec3& v);
float aim_pitch(const render::Vec3& v);
float angle_diff(float a, float b);
float angle_wrap360(float a);
// The unit direction with that yaw and pitch.
render::Vec3 aim_direction(float yaw, float pitch);

// The course camera's aim and zoom for one shot.
class CourseCameraAim {
public:
    // A new shot (the record set or re-armed): the next update is its first frame.
    void restart() { started_ = false; }
    // The cameras' target is now another car: the last aim point is forgotten (target state
    // +0x92).
    void forget_last_aim() { last_aim_.reset(); }
    // Every frame after the cameras: the target state's aim point (the followed car, or between
    // a pair; without the 0.4 m lift), for the next shot's first frame (target state +0x94).
    void remember(const render::Vec3& point) { last_aim_ = point; }

    struct In {
        render::Vec3 eye, aim;       // the camera and the point it aims at
        float zoom = 1;              // the record's zoom after its limits (and a pair's widening)
        bool at_car = true;          // the record looks at the car (kind & 0xC)
        bool kind_400 = false;       // kind 0x400
        std::optional<float> wide;   // mode 1's wide shot: the first frame's zoom (1.0)
        std::optional<float> yaw_offset; // mode 1's yaw seed: added to the first frame's target
        float progress = 1;          // the aim's move between cars (1 when done or a pair)
        u32 seeds[3] = {0, 0, 0};    // the first frame's noise seeds (hardware values): bob, yaw, pitch
    };
    struct Out {
        float yaw = 0, pitch = 0, zoom = 1;
        render::Vec3 target; // a point along the aim, as far as `aim` (step only)
    };
    // One 60 Hz frame.
    Out step(const In& in);
    // `dt` seconds of real time: the 60 Hz frames due in it, each on the input as it was at that
    // frame's moment (between the last call's input and this one: frames shown unevenly, or at
    // another rate, must not step the filters on a target sampled early or twice, which its
    // lead turns into a shake), then the result blended to now. A shot's first call is its
    // first frame.
    Out advance(const In& in, double dt);
    // The last two frames' result blended by `frac` (0 .. 1) of the next frame.
    Out blend(float frac) const;

private:
    AimFilter yaw_, pitch_;
    OperatorNoise bob_;
    ZoomSpring zoom_;
    bool started_ = false;
    std::optional<render::Vec3> last_aim_;
    Out prev_, cur_;
    In last_in_;       // the last call's input (advance)
    double acc_ = 0;   // time into the next 60 Hz frame (advance)
};

} // namespace gt3r::race
