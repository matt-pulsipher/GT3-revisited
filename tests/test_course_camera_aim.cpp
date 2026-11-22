// The course camera's operator (presentation §H6a): the filtered aim and the zoom spring of
// replay course shots (the designer camera).

#include "test_framework.h"

#include "race/course_camera_aim.h"
#include "race/replay_cameras.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace gt3r;
using namespace gt3r::race;

namespace {

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

CourseCameraAim::In car_shot(const render::Vec3& eye, const render::Vec3& car, float zoom) {
    CourseCameraAim::In in;
    in.eye = eye, in.aim = car;
    in.zoom = zoom;
    in.at_car = true;
    in.seeds[0] = 3, in.seeds[1] = 17, in.seeds[2] = 40;
    return in;
}

} // namespace

// GT3's angle helpers: wraps by one turn into -180 .. 180, by whole turns into 0 .. 360; the yaw
// and pitch of a direction and back.
TEST(course_camera_aim_angles) {
    CHECK(near(angle_diff(350, 10), -20) && near(angle_diff(10, 350), 20) && near(angle_diff(90, 30), 60));
    CHECK(near(angle_wrap360(370), 10) && near(angle_wrap360(-10), 350) && near(angle_wrap360(725), 5));
    const render::Vec3 v{3, -2, -5};
    const render::Vec3 d = aim_direction(aim_yaw(v), aim_pitch(v));
    const render::Vec3 u = v * (1.0f / render::length(v));
    CHECK(near(d.x, u.x) && near(d.y, u.y) && near(d.z, u.z));
    CHECK(aim_pitch({0, -1, 1}) > 0); // looking down: positive
}

// The lead: the target angle plus 5 times its last change, led on by k (2u - u^2) with
// u = min(5 |change| / 3k, 1).
TEST(course_camera_aim_lead) {
    AimFilter f;
    f.reset(100, 100, 20, 0, 1, 1);
    CHECK(near(f.lead(20), 100)); // one angle known: no change
    f.push(101);
    const float u = 5.0f / 60.0f;
    CHECK(near(f.lead(20), 101 + 5 + 20 * (2 * u - u * u)));
    f.push(71); // a big change (-30): u capped at 1
    CHECK(near(f.lead(20), 71 - 150 - 20));
}

// A still car: the yaw settles on it, the pitch on 0.4 k cos(pitch) degrees above it (the car
// below the middle of the picture), whatever the noise does to the gain.
TEST(course_camera_aim_still_car) {
    CourseCameraAim aim;
    const render::Vec3 eye{0, 5, 0}, car{20, 0.4f, 30};
    const float zoom = 2.0f, k = 15.0f;
    CourseCameraAim::Out o = aim.step(car_shot(eye, car, zoom));
    const float pitch = aim_pitch(car - eye), yaw = aim_yaw(car - eye);
    CHECK(near(o.yaw, yaw));
    CHECK(near(o.pitch, pitch - 0.4f * k * std::cos(pitch * 3.14159265f / 180.0f), 1e-2f));
    for (int i = 0; i < 600; ++i) o = aim.step(car_shot(eye, car, zoom));
    CHECK(near(angle_diff(o.yaw, yaw), 0, 0.05f));
    CHECK(near(o.pitch, pitch - 0.4f * k * std::cos(pitch * 3.14159265f / 180.0f), 0.05f));
    CHECK(near(o.zoom, zoom));
}

// A car crossing the view at 40 m/s, 30 m away: the camera turns late (it trails the car once it
// moves, then runs a few degrees ahead of it), never faster than 250 degrees a second nor by more than 25 degrees a second per frame,
// then keeps the car in view, leading or trailing it by a few degrees.
TEST(course_camera_aim_follows_late) {
    CourseCameraAim aim;
    const render::Vec3 eye{0, 2, 0};
    render::Vec3 car{-20, 0.4f, 30};
    aim.step(car_shot(eye, car, 2.0f));
    float last_v = 0, max_lag = 0, last_out = aim_yaw(car - eye);
    bool trailed = false;
    for (int i = 0; i < 120; ++i) {
        car.x += 40.0f / 60.0f;
        const CourseCameraAim::Out o = aim.step(car_shot(eye, car, 2.0f));
        const float lag = angle_diff(aim_yaw(car - eye), o.yaw);
        if (i < 10 && std::fabs(lag) > 0.5f) trailed = true;
        max_lag = std::max(max_lag, std::fabs(lag));
        const float v = angle_diff(o.yaw, last_out) * 60.0f; // (starting still)
        CHECK(std::fabs(v) <= 250.0f + 1e-2f);
        CHECK(std::fabs(v - last_v) <= 25.0f + 0.5f);
        last_v = v;
        last_out = o.yaw;
    }
    CHECK(trailed);
    CHECK(max_lag < 20.0f);
}

// A record aimed at its target curves: no filter, no offset.
TEST(course_camera_aim_target_curves) {
    CourseCameraAim aim;
    CourseCameraAim::In in = car_shot({0, 5, 0}, {10, 0, 10}, 2.0f);
    in.at_car = false;
    const render::Vec3 to = in.aim - in.eye;
    for (int i = 0; i < 5; ++i) {
        const CourseCameraAim::Out o = aim.step(in);
        CHECK(near(o.yaw, aim_yaw(to)) && near(o.pitch, aim_pitch(to)));
    }
}

// Mode 1's yaw seed: the first frame shows the car, then the camera swings off by the seed's
// speed before coming back.
TEST(course_camera_aim_yaw_seed) {
    CourseCameraAim aim;
    const render::Vec3 eye{0, 5, 0}, car{0, 0.4f, 40};
    CourseCameraAim::In in = car_shot(eye, car, 2.0f);
    in.yaw_offset = -60.0f;
    const CourseCameraAim::Out first = aim.step(in);
    CHECK(near(first.yaw, aim_yaw(car - eye)));
    in.yaw_offset.reset();
    const CourseCameraAim::Out next = aim.step(in);
    CHECK(angle_diff(next.yaw, first.yaw) > 0.5f); // swinging at +60 degrees a second at first
}

// The zoom spring: in at most 10 a second (scaled by the aim's move), out at most 30; mode 1's
// wide shot starts at 1.0 and zooms in.
TEST(course_camera_aim_zoom_spring) {
    ZoomSpring z;
    z.reset(1.0f);
    for (int i = 0; i < 60; ++i) z.step(20.0f, 1.0f);
    CHECK(z.x <= 1.0f + 10.0f + 1e-3f && z.x > 5.0f);
    ZoomSpring held;
    held.reset(1.0f);
    for (int i = 0; i < 60; ++i) held.step(20.0f, 0.0f);
    CHECK(near(held.x, 1.0f)); // no inward pull while the aim's move starts
    ZoomSpring out;
    out.reset(20.0f);
    for (int i = 0; i < 30; ++i) out.step(1.0f, 1.0f);
    CHECK(out.x >= 20.0f - 15.0f - 1e-3f && out.x < 19.0f);
    CourseCameraAim aim;
    CourseCameraAim::In in = car_shot({0, 5, 0}, {0, 0.4f, 40}, 4.0f);
    in.wide = 1.0f;
    CHECK(near(aim.step(in).zoom, 1.0f));
    in.wide.reset();
    float zoom = 1.0f;
    for (int i = 0; i < 120; ++i) zoom = aim.step(in).zoom;
    CHECK(zoom > 3.5f && zoom < 4.5f);
}

// Issue 402 (replays shaking on an uneven ~60 fps phone): real time steps the operator on the car
// where it was at each 60 Hz frame, so frames shown unevenly see the same camera as frames at an
// even 60 Hz. Before, a frame with no step followed by one with two fed the filter the same car
// twice, and its lead (5 times the last change) swung the camera to and fro.
TEST(course_camera_aim_uneven_frames) {
    const render::Vec3 eye{0, 5, 0};
    auto car_at = [](double t) { return render::Vec3{-60.0f + 45.0f * float(t), 0.4f, 25.0f}; };
    constexpr double kFrame = 1.0 / 60.0;
    // The reference: one step per 60 Hz frame on the car at that frame.
    CourseCameraAim ref;
    std::vector<CourseCameraAim::Out> steps;
    for (int k = 0; k <= 200; ++k) steps.push_back(ref.step(car_shot(eye, car_at(k * kFrame), 4.0f)));
    // Shown frames: (lowest, highest) frame time in 60 Hz frames, and the refresh a late frame
    // adds (0: none).
    struct Pace {
        double lo, hi, late;
    };
    const Pace paces[] = {
        {0.85, 1.25, 1.0},        // 48..70 fps, some a refresh late (the reporter's phone)
        {0.5, 0.833, 0.5},        // 72..120 Hz, some a 120 Hz refresh late
        {0.75, 0.75, 0},          // an even 80 Hz
        {0.5, 0.5, 0},            // an even 120 Hz
        {2.0 / 3.0, 2.0 / 3.0, 0.5}, // 90 Hz, some a refresh of a 120 Hz panel late
        {0.12, 0.84, 0},          // vsync off: 70..500 fps
    };
    for (const Pace& p : paces) {
        CourseCameraAim aim;
        aim.advance(car_shot(eye, car_at(0), 4.0f), 0);
        u32 r = 12345;
        double t = 0, acc = 0;
        size_t k = 0;
        float worst = 0;
        while (k + 1 < steps.size()) {
            r = r * 1103515245u + 12345u;
            double dt = kFrame * (p.lo + (p.hi - p.lo) * double((r >> 16) & 0x7FFF) / 32767.0);
            if (((r >> 8) & 7) == 0) dt += kFrame * p.late;
            t += dt;
            acc += dt;
            for (int n = 0; acc >= kFrame && n < 12; ++n) acc -= kFrame, ++k; // (advance's own count)
            if (k + 1 >= steps.size()) break;
            const CourseCameraAim::Out o = aim.advance(car_shot(eye, car_at(t), 4.0f), dt);
            const CourseCameraAim::Out& a = steps[k > 0 ? k - 1 : 0];
            const float f = float(acc * 60.0);
            const float want_yaw = a.yaw + angle_diff(steps[k].yaw, a.yaw) * f;
            const float want_pitch = a.pitch + angle_diff(steps[k].pitch, a.pitch) * f;
            worst = std::max({worst, std::fabs(angle_diff(o.yaw, want_yaw)), std::fabs(angle_diff(o.pitch, want_pitch))});
        }
        CHECK(worst < 0.05f); // degrees
    }
}

// Issue 402's one-frame flashes of sky or road: the pitch is 359.x one frame and -0.x the next
// as the camera aims across level; the blend between frames goes the short way, as the yaw's.
TEST(course_camera_aim_pitch_wrap) {
    CourseCameraAim aim;
    const render::Vec3 eye{0, 5, 0};
    bool crossed = false;
    for (int i = 0; i < 240; ++i) { // the car climbs from below the camera to above it
        const CourseCameraAim::In in = car_shot(eye, {0, -5.0f + 0.1f * float(i), 30.0f}, 4.0f);
        const CourseCameraAim::Out prev = i ? aim.blend(1.0f) : CourseCameraAim::Out{};
        const CourseCameraAim::Out cur = aim.step(in);
        if (!i) continue;
        const float step = std::fabs(angle_diff(cur.pitch, prev.pitch));
        crossed = crossed || std::fabs(cur.pitch - prev.pitch) > 180.0f;
        for (float f : {0.25f, 0.5f, 0.75f}) {
            const float mid = aim.blend(f).pitch;
            CHECK(std::fabs(angle_diff(mid, prev.pitch)) <= step + 1e-3f);
            CHECK(std::fabs(angle_diff(mid, cur.pitch)) <= step + 1e-3f);
        }
    }
    CHECK(crossed); // (the test reaches the wrap)
}

// The director: a course shot looking at the car aims through the operator (the car below the
// middle of the picture on the cut), one aimed at its target curves straight at them.
TEST(replay_director_course_aim) {
    GtrcRecord at_car;
    at_car.kind = 4; // looks at the car
    at_car.start = 1000, at_car.end = 1900;
    at_car.zoom = GtrcCurve{{{0.0f, 2.0f}}};
    at_car.pos[0] = GtrcCurve{{{0.0f, 0.0f}}}, at_car.pos[1] = GtrcCurve{{{0.0f, 5.0f}}},
    at_car.pos[2] = GtrcCurve{{{0.0f, 0.0f}}};
    GtrcScript g;
    g.lap_length = 1000, g.script_end = 3000;
    g.records = {at_car};
    ReplayDirector d(&g);
    const render::Vec3 car{10, 0, 30};
    const ReplayDirector::Shot s = d.update(10, 1, car, 1.0 / 60, 16.0f / 9.0f);
    CHECK(s.valid);
    const render::Vec3 lifted = car + render::Vec3{0, 0.4f, 0};
    CHECK(near(aim_yaw(s.target - s.eye), aim_yaw(lifted - s.eye), 1e-2f));
    CHECK(aim_pitch(s.target - s.eye) < aim_pitch(lifted - s.eye) - 3.0f); // aims above the car
    CHECK(near(render::length(s.target - s.eye), render::length(lifted - s.eye), 1e-2f));
}

// Issue 402's flicker at the line: the lap counter steps on the physics step that crosses it,
// the drawn distance a frame later (or the other way round); the director's lap steps with the
// drawn distance.
TEST(replay_director_seam_lap) {
    GtrcScript g;
    g.lap_length = 1000, g.script_end = 3000;
    ReplayDirector d(&g);
    CHECK(d.seam_lap(990, 1) == 1);
    CHECK(d.seam_lap(998, 2) == 1);  // counted, not yet drawn across
    CHECK(d.seam_lap(999.5f, 2) == 1);
    CHECK(d.seam_lap(1.0f, 2) == 2); // drawn across
    CHECK(d.seam_lap(5.0f, 2) == 2);
    CHECK(d.seam_lap(995, 2) == 2);  // a counter that is late:
    CHECK(d.seam_lap(2.0f, 2) == 3); // drawn across, not yet counted
    CHECK(d.seam_lap(6.0f, 2) == 3);
    CHECK(d.seam_lap(10.0f, 3) == 3);
    CHECK(d.seam_lap(900, 3) == 3);  // backwards over the line: the counter's own lap
    CHECK(d.seam_lap(500, 7) == 7);  // a jump (a warp): taken as it is
    d.car_changed(0, false);
    CHECK(d.seam_lap(999, 4) == 4);  // another car: taken as it is
}
