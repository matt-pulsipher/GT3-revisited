// Photo mode's free camera (race/photo_camera.h): its start from the view on screen, the pad's
// moves and turns, its range, the road under it and the still time.

#include "test_framework.h"

#include "race/photo_camera.h"
#include "race/photo_history.h"
#include "render/math3d.h"

#include <array>
#include <cmath>
#include <vector>

using namespace gt3r;
using namespace gt3r::race;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

PhotoCamera looking(const render::Vec3& eye, const render::Vec3& target, float fovy = 0.8f) {
    PhotoCamera c;
    c.begin(eye, render::look_at(eye, target, {0, 1, 0}), fovy, target);
    return c;
}

} // namespace

TEST(photo_camera_begins_on_the_view) {
    PhotoCamera c = looking({0, 2, 10}, {0, 2, 0}); // along -z
    CHECK(near(c.yaw(), 0));
    CHECK(near(c.pitch(), 0));
    const render::Vec3 f = c.forward();
    CHECK(near(f.x, 0) && near(f.y, 0) && near(f.z, -1));
    PhotoCamera l = looking({10, 2, 0}, {0, 2, 0}); // along -x: a quarter turn left
    CHECK(near(l.yaw(), 1.5707963f, 1e-3f));
    PhotoCamera d = looking({0, 10, 10}, {0, 0, 0}); // 45 degrees down
    CHECK(near(d.pitch(), -0.7853982f, 1e-3f));
}

TEST(photo_camera_moves_level) {
    PhotoCamera c = looking({0, 10, 10}, {0, 0, 0}); // looking down: forward still moves level
    PhotoInput in;
    in.move_y = 1;
    CHECK(c.update(in, 0.1));
    CHECK(near(c.eye().y, 10));
    CHECK(near(c.eye().z, 10 - PhotoCamera::kSpeed * 0.1f, 1e-3f));
    in = {};
    in.move_x = 1, in.fast = true; // right, faster
    c.update(in, 0.1);
    CHECK(near(c.eye().x, PhotoCamera::kSpeed * PhotoCamera::kFastScale * 0.1f, 1e-3f));
    in = {};
    in.rise = 1;
    c.update(in, 0.1);
    CHECK(near(c.eye().y, 10 + PhotoCamera::kRiseSpeed * 0.1f, 1e-3f));
}

TEST(photo_camera_turns_and_zooms) {
    PhotoCamera c = looking({0, 2, 10}, {0, 2, 0});
    PhotoInput in;
    in.turn_x = 1; // right: the yaw goes down
    c.update(in, 0.1);
    CHECK(c.yaw() < 0);
    in = {};
    in.turn_y = 1;
    for (int i = 0; i < 100; ++i) c.update(in, 0.1);
    CHECK(near(c.pitch(), PhotoCamera::kMaxPitch));
    in = {};
    in.zoom = 1;
    const float before = c.fovy();
    c.update(in, 0.1);
    CHECK(c.fovy() < before);
    for (int i = 0; i < 200; ++i) c.update(in, 0.1);
    CHECK(near(c.fovy(), PhotoCamera::kMinFovy));
    // Zoomed in, the same stick turns less.
    PhotoCamera wide = looking({0, 2, 10}, {0, 2, 0}, 0.8f), tele = looking({0, 2, 10}, {0, 2, 0}, 0.2f);
    in = {};
    in.turn_x = 1;
    wide.update(in, 0.05), tele.update(in, 0.05);
    CHECK(std::fabs(tele.yaw()) < std::fabs(wide.yaw()));
}

TEST(photo_camera_range_and_ground) {
    PhotoCamera c = looking({0, 2, 10}, {0, 2, 0}); // the anchor at the origin's car
    PhotoInput in;
    in.move_y = -1, in.fast = true; // backwards, away from it
    for (int i = 0; i < 1000; ++i) c.update(in, 0.1);
    CHECK(render::length(c.eye() - render::Vec3{0, 2, 0}) <= PhotoCamera::kRange + 1e-2f);
    // A flat road at y = 0: the eye stops kClearance over it.
    PhotoCamera g = looking({0, 2, 10}, {0, 2, 0});
    g.set_ground([](float, float, float y_from) -> std::optional<float> { return y_from >= 0 ? std::optional<float>(0.0f) : std::nullopt; });
    in = {};
    in.rise = -1;
    for (int i = 0; i < 100; ++i) g.update(in, 0.1);
    CHECK(near(g.eye().y, PhotoCamera::kClearance));
}

TEST(photo_camera_still_after_a_second) {
    PhotoCamera c = looking({0, 2, 10}, {0, 2, 0});
    PhotoInput none;
    CHECK(!c.update(none, 0.5));
    CHECK(!c.still());
    c.update(none, 0.5);
    CHECK(c.still());
    PhotoInput in;
    in.turn_x = 0.5f;
    CHECK(c.update(in, 1.0 / 60));
    CHECK(!c.still());
    c.update(none, 0.6), c.update(none, 0.6);
    CHECK(c.still());
    c.wake();
    CHECK(!c.still());
}

TEST(photo_shutter_closes_at_the_moment) {
    PhotoShutter s = photo_shutter(-0.5f, 1.0f, -11); // one tick back from halfway into the paused one
    CHECK(near(s.open, -1.5f) && near(s.close, -0.5f));
    s = photo_shutter(-0.5f, 20.0f, -11); // no further back than the past kept
    CHECK(near(s.open, -11) && near(s.close, -0.5f));
    s = photo_shutter(-0.25f, 0.1f, -1);
    CHECK(near(s.open, -0.35f) && near(s.close, -0.25f));
    s = photo_shutter(-0.25f, 0, -1); // (none: a sharp picture)
    CHECK(near(s.open, -0.25f) && near(s.close, -0.25f));
}

TEST(photo_samples_cover_the_shutter) {
    const PhotoShutter s{-1.25f, -0.75f};
    const int n = kPhotoSamples;
    std::vector<int> strata(size_t(n), 0);
    for (int i = 0; i < n; ++i) {
        const PhotoSample p = photo_sample(i, n, s);
        CHECK(p.t >= s.open && p.t < s.close);
        CHECK(p.dx >= -0.5f && p.dx < 0.5f && p.dy >= -0.5f && p.dy < 0.5f);
        ++strata[size_t((p.t - s.open) / (s.close - s.open) * float(n))];
    }
    for (int c : strata) CHECK(c == 1); // each stratum once
    // The first k already spread over the whole shutter: the first 2 in either half, the first 4
    // in every quarter.
    for (int k : {2, 4, 8}) {
        std::vector<int> part(size_t(k), 0);
        for (int i = 0; i < k; ++i) ++part[size_t((photo_sample(i, n, s).t - s.open) / (s.close - s.open) * float(k))];
        for (int c : part) CHECK(c == 1);
    }
    // The pixel offsets average out.
    float mx = 0, my = 0;
    for (int i = 0; i < n; ++i) mx += photo_sample(i, n, s).dx, my += photo_sample(i, n, s).dy;
    CHECK(std::fabs(mx / float(n)) < 0.05f && std::fabs(my / float(n)) < 0.05f);
    // Fixed: the same sample every time.
    CHECK(photo_sample(5, n, s).t == photo_sample(5, n, s).t);
}

TEST(photo_follow_and_shutter_steps) {
    PhotoCamera c = looking({0, 2, 10}, {0, 2, 0});
    CHECK(c.follow() == PhotoFollow::Roadside); // (the default)
    CHECK(near(c.shutter_seconds(), 1.0f / 60));
    PhotoInput none;
    c.update(none, 2.0);
    CHECK(c.still());
    c.step_follow(1);
    CHECK(c.follow() == PhotoFollow::FixedToCar);
    CHECK(!c.still()); // (a new photo)
    c.step_follow(1);
    CHECK(c.follow() == PhotoFollow::Roadside); // round
    c.step_follow(-1);
    CHECK(c.follow() == PhotoFollow::FixedToCar);
    for (int i = 0; i < 20; ++i) c.step_shutter(1);
    CHECK(near(c.shutter_seconds(), 1.0f / 8)); // stops at the longest
    for (int i = 0; i < 20; ++i) c.step_shutter(-1);
    CHECK(near(c.shutter_seconds(), 1.0f / 1000));
    // Roadside: panning with a car under the crosshair, else still; Fixed to car either way.
    CHECK(photo_follow_mode(PhotoFollow::Roadside, true) == PhotoMode::Pan);
    CHECK(photo_follow_mode(PhotoFollow::Roadside, false) == PhotoMode::Free);
    CHECK(photo_follow_mode(PhotoFollow::FixedToCar, true) == PhotoMode::Fixed);
    CHECK(photo_follow_mode(PhotoFollow::FixedToCar, false) == PhotoMode::Fixed);
}

TEST(photo_camera_roll) {
    PhotoCamera c = looking({0, 2, 10}, {0, 2, 0});
    PhotoInput in;
    in.roll = 1;
    CHECK(c.update(in, 0.1));
    CHECK(near(c.roll(), PhotoCamera::kRollRate * 0.1f));
    // Rolled right (the camera's right side down), a point on the level to its right rises in the
    // picture, as the horizon tilts the other way.
    const render::Vec3 right = c.view().transform_point({1, 2, 0});
    CHECK(right.y > 0 && right.x > 0);
    for (int i = 0; i < 50; ++i) c.update(in, 0.1);
    CHECK(near(c.roll(), PhotoCamera::kMaxRoll));
    // The view's axis stays: the centre is still straight ahead.
    const render::Vec3 ahead = c.view().transform_point({0, 2, 0});
    CHECK(near(ahead.x, 0, 1e-3f) && near(ahead.y, 0, 1e-3f));
}

namespace {
// Where a world point lands in a view (eye-space direction, normalised).
render::Vec3 seen(const render::Mat4& view, const render::Vec3& p) { return render::normalize(view.transform_point(p)); }
bool same(const render::Vec3& a, const render::Vec3& b, float eps = 1e-4f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}
} // namespace

TEST(photo_view_modes) {
    const render::Vec3 eye{0, 2, 10};
    const render::Mat4 view = render::look_at(eye, {3, 1, 0}, {0, 1, 0});
    const render::Mat4 ref = render::translate({1, 0.5f, 0}) * render::rotate_y(0.2f);
    const render::Mat4 at = render::translate({4, 0.6f, -2}) * render::rotate_y(0.5f);
    // Free: the photo's own view.
    PhotoView v = photo_view(PhotoMode::Free, eye, view, ref, at);
    CHECK(same(v.eye, eye) && v.view.m == view.m);
    // Pan: the eye stays, the car's origin stays where the view had it.
    v = photo_view(PhotoMode::Pan, eye, view, ref, at);
    CHECK(same(v.eye, eye));
    CHECK(same(seen(v.view, at.transform_point({})), seen(view, ref.transform_point({}))));
    // ... and a still car leaves the view as it was.
    v = photo_view(PhotoMode::Pan, eye, view, ref, ref);
    CHECK(same(seen(v.view, {7, -3, 2}), seen(view, {7, -3, 2})));
    // Fixed: every point of the car stays put in the view, and the eye moves with it.
    v = photo_view(PhotoMode::Fixed, eye, view, ref, at);
    for (const render::Vec3& local : {render::Vec3{0, 0, 0}, render::Vec3{1, 0.5f, -2}, render::Vec3{-0.8f, 1, 2}})
        CHECK(same(v.view.transform_point(at.transform_point(local)), view.transform_point(ref.transform_point(local)), 1e-3f));
    CHECK(same(v.eye, (at * render::inverse(ref)).transform_point(eye), 1e-3f));
}

TEST(photo_history_between_ticks) {
    auto car_at = [](float x) {
        sim::VehicleState s;
        s.position = {x, 0, 0};
        return s;
    };
    PhotoHistory h;
    CHECK(h.span() == -1);
    CHECK(!h.at(0, 0));
    for (u32 t = 0; t < 5; ++t) h.push(t, {car_at(float(t)), car_at(10.0f * float(t))});
    CHECK(h.span() == 4);
    CHECK(near(h.at(0, 0)->position.x, 4));       // the newest
    CHECK(near(h.at(0, -1.5f)->position.x, 2.5f)); // between ticks 2 and 3
    CHECK(near(h.at(1, -0.25f)->position.x, 37.5f));
    CHECK(near(h.at(0, -9)->position.x, 0)); // (clamped to the oldest)
    CHECK(!h.at(2, 0));                      // (no such car)
    for (u32 t = 5; t < 40; ++t) h.push(t, {car_at(float(t)), car_at(0)});
    CHECK(h.span() == PhotoHistory::kStates - 1);
    h.push(100, {car_at(100), car_at(0)}); // not the next tick: the past starts again
    CHECK(h.span() == 0);
    CHECK(near(h.at(0, -3)->position.x, 100));
}

TEST(photo_camera_touch_aim) {
    PhotoCamera c = looking({0, 2, 10}, {0, 2, 0});
    PhotoInput none;
    c.update(none, 2.0);
    CHECK(!c.aim(0, 0, 1)); // (nothing: still)
    CHECK(c.still());
    CHECK(c.aim(0.1f, -0.05f, 2.0f));
    CHECK(!c.still());
    CHECK(near(c.yaw(), 0.1f) && near(c.pitch(), -0.05f) && near(c.fovy(), 0.4f));
    c.aim(0, 0, 1000); // held to the zoom's range
    CHECK(near(c.fovy(), PhotoCamera::kMinFovy));
    c.aim(0, 10, 1);
    CHECK(near(c.pitch(), PhotoCamera::kMaxPitch));
}

TEST(photo_aspects_and_grids) {
    CHECK(near(photo_aspect_ratio(PhotoAspect::Wide), 16.0f / 9));
    CHECK(near(photo_aspect_ratio(PhotoAspect::Standard), 4.0f / 3));
    CHECK(near(photo_frame_ratio(PhotoAspect::Wide, PhotoOrientation::Landscape), 16.0f / 9));
    CHECK(near(photo_frame_ratio(PhotoAspect::Wide, PhotoOrientation::Portrait), 9.0f / 16));
    CHECK(near(photo_frame_ratio(PhotoAspect::Standard, PhotoOrientation::Portrait), 3.0f / 4));
    // A portrait frame's grids: the square diagonals still at 45 degrees (one frame width down).
    for (const auto& l : photo_grid_lines(PhotoGrid::Diagonals, 9.0f / 16))
        CHECK(near(std::fabs(l[2] - l[0]) * (9.0f / 16), std::fabs(l[3] - l[1])));
    CHECK(photo_grid_lines(PhotoGrid::Off, 1.5f).empty());
    // Thirds: two lines each way at a third and two.
    const auto thirds = photo_grid_lines(PhotoGrid::Thirds, 16.0f / 9);
    CHECK(thirds.size() == 4);
    CHECK(near(thirds[0][0], 1 / 3.0f) && near(thirds[3][1], 2 / 3.0f));
    // Square diagonals: 45 degrees in the frame (as far across as down, in its own units).
    for (float aspect : {16.0f / 9, 4.0f / 3}) {
        for (const auto& l : photo_grid_lines(PhotoGrid::Diagonals, aspect))
            CHECK(near(std::fabs(l[2] - l[0]) * aspect, std::fabs(l[3] - l[1])));
        // Golden triangle: the corner-to-corner diagonal, and the other two lines meet it square.
        const auto g = photo_grid_lines(PhotoGrid::GoldenTriangle, aspect);
        CHECK(g.size() == 3);
        const float dx = (g[0][2] - g[0][0]) * aspect, dy = g[0][3] - g[0][1];
        for (int i = 1; i < 3; ++i) {
            const float px = (g[size_t(i)][2] - g[size_t(i)][0]) * aspect, py = g[size_t(i)][3] - g[size_t(i)][1];
            CHECK(std::fabs(px * dx + py * dy) < 1e-4f);
            // ... with its foot on the diagonal: (x, y) with y = 1 - x.
            CHECK(near(g[size_t(i)][3], 1 - g[size_t(i)][2]));
        }
    }
}

TEST(photo_lens_depth_of_field) {
    // A 35 mm equivalent focal length from the field of view: 50 mm is about 46.8 degrees across
    // the diagonal (27.0 degrees high in 3:2).
    const float fovy_50 = 2 * std::atan(12.0f / 50.0f); // 24 mm high at 50 mm
    CHECK(near(photo_focal_mm(fovy_50, 1.5f), 50.0f, 0.05f));
    CHECK(photo_focal_mm(fovy_50 * 0.5f, 1.5f) > 90); // zoomed in: longer
    // The aperture: 50 mm at f/2 is 25 mm across.
    CHECK(near(photo_aperture_radius(50, 2), 0.0125f));
    CHECK(photo_aperture_radius(50, 16) < photo_aperture_radius(50, 2));
    // Through a point of the lens the plane of focus stays put in the picture; nearer and farther
    // points move apart, the more the farther from it.
    const render::Vec3 eye{0, 2, 10};
    const render::Mat4 view = render::look_at(eye, {0, 2, 0}, {0, 1, 0});
    const render::Mat4 proj = render::perspective(0.8f, 16.0f / 9, 0.1f, 1000);
    const float focus = 10;
    const PhotoLens l = photo_lens(eye, view, 0.02f, -0.01f, focus);
    auto ndc = [](const render::Mat4& m, const render::Vec3& p) {
        const render::Vec3 c = m.transform_point(p);
        const float w = m.at(3, 0) * p.x + m.at(3, 1) * p.y + m.at(3, 2) * p.z + m.at(3, 3);
        return std::array<float, 2>{c.x / w, c.y / w};
    };
    const render::Mat4 sharp = proj * view, through = proj * l.shear * l.view;
    for (const render::Vec3& p : {render::Vec3{0, 2, 0}, render::Vec3{1.5f, 3, 0}, render::Vec3{-2, 1, 0}}) { // on the plane
        const auto a = ndc(sharp, p), b = ndc(through, p);
        CHECK(near(a[0], b[0], 1e-4f) && near(a[1], b[1], 1e-4f));
    }
    const auto far_a = ndc(sharp, {0, 2, -40}), far_b = ndc(through, {0, 2, -40});
    const auto mid_a = ndc(sharp, {0, 2, -5}), mid_b = ndc(through, {0, 2, -5});
    CHECK(std::fabs(far_b[0] - far_a[0]) > std::fabs(mid_b[0] - mid_a[0]) && std::fabs(mid_b[0] - mid_a[0]) > 1e-4f);
    CHECK(near(l.eye.x, 0.02f) && near(l.eye.y, 2 - 0.01f)); // (right and up of the eye)
    // The samples' lens points: within the disc, and spread over it.
    float mx = 0, my = 0, rr = 0;
    for (int i = 0; i < kPhotoSamples; ++i) {
        const PhotoSample p = photo_sample(i, kPhotoSamples, {0, 1});
        const float r2 = p.lens_x * p.lens_x + p.lens_y * p.lens_y;
        CHECK(r2 <= 1.0001f);
        mx += p.lens_x, my += p.lens_y, rr += r2;
    }
    CHECK(std::fabs(mx / kPhotoSamples) < 0.08f && std::fabs(my / kPhotoSamples) < 0.08f);
    CHECK(near(rr / kPhotoSamples, 0.5f, 0.08f)); // (an even disc's mean r^2)
}
