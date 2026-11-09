// The bonnet camera's seat on a car (race/hood_camera.h): the mounts worked out from the
// body's LOD-0 mesh, and a Mount::Hood custom camera's place, look-back body hiding and
// horizon roll lock in the rig (race/camera_rig.h, race/custom_cameras.h).

#include "test_framework.h"

#include "race/camera_rig.h"
#include "race/custom_cameras.h"
#include "race/hood_camera.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace gt3r;
using namespace gt3r::race;

namespace {

constexpr float kDeg = 3.14159265f / 180.0f;

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool finite(const render::Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

scene::Vertex vertex(float x, float y, float z) {
    scene::Vertex v;
    v.pos[0] = x, v.pos[1] = y, v.pos[2] = z;
    return v;
}

void add_quad(scene::Mesh& m, const std::array<render::Vec3, 4>& p, u32 flags = 0) {
    const u32 at = u32(m.vertices.size());
    for (const render::Vec3& v : p) m.vertices.push_back(vertex(v.x, v.y, v.z));
    for (u32 i : {at, at + 1, at + 2, at, at + 2, at + 3}) m.indices.push_back(i);
    m.primitives.push_back({u32(m.indices.size()) - 6, 6, -1, flags});
}

scene::Model body(float half_w, float front, float rear, float deck, float cabin, float cabin_front) {
    scene::Model body;
    scene::Mesh m;
    m.name = "body";
    add_quad(m, {{{-half_w, deck, front}, {half_w, deck, front}, {half_w, deck, cabin_front}, {-half_w, deck, cabin_front}}});
    add_quad(m, {{{-half_w, cabin, cabin_front}, {half_w, cabin, cabin_front}, {half_w, cabin, rear}, {-half_w, cabin, rear}}});
    add_quad(m, {{{-half_w, 0, front}, {half_w, 0, front}, {half_w, 0, rear}, {-half_w, 0, rear}}});
    add_quad(m, {{{-half_w, 0, front}, {-half_w, deck, front}, {-half_w, deck, rear}, {-half_w, 0, rear}}});
    add_quad(m, {{{half_w, 0, front}, {half_w, deck, front}, {half_w, deck, rear}, {half_w, 0, rear}}});
    body.meshes.push_back(m);
    body.compute_bounds();
    return body;
}

scene::Model sedan() { return body(0.9f, -2.25f, 2.25f, 0.7f, 1.35f, -0.5f); }

render::Vec3 row(const render::Mat4& v, int r) { return {v.at(r, 0), v.at(r, 1), v.at(r, 2)}; }
render::Vec3 view_up(const CameraRig& c) { return row(c.view(), 1); }
render::Vec3 view_fwd(const CameraRig& c) { return row(c.view(), 2) * -1.0f; }

sim::Quat axis_angle(float ax, float ay, float az, float radians) {
    const float s = std::sin(radians * 0.5f);
    return {std::cos(radians * 0.5f), ax * s, ay * s, az * s};
}

bool orthonormal(const render::Mat4& v) {
    const render::Vec3 r = row(v, 0), u = row(v, 1), f = row(v, 2);
    for (const render::Vec3& w : {r, u, f})
        if (!finite(w) || !near(render::length(w), 1.0f, 1e-3f)) return false;
    return near(render::dot(r, u), 0, 1e-3f) && near(render::dot(r, f), 0, 1e-3f) && near(render::dot(u, f), 0, 1e-3f);
}

void settle(CameraRig& c, const sim::VehicleState& s, const sim::VehicleSetup& setup, int frames = 90) {
    for (int i = 0; i < frames; ++i) c.update(s, setup, PadState{}, 1.0 / 60.0);
}

// A Mount::Hood custom camera: on the hood, the car shown (left out on its own looking back), the
// in-car look; `lock`: the share of the body's roll levelled out (body roll 1 - lock).
CustomCamera bonnet(float lock = 0) {
    CustomCamera c;
    c.id = 1, c.name = "Bonnet";
    c.mount = CustomCamera::Mount::Hood;
    c.offset = {};
    c.car = CustomCamera::OwnCar::Shown;
    c.incar_hud = c.mirror = true;
    c.body_roll = 1.0f - lock;
    c.fov = 55;
    return c;
}

CameraRig rig_on_bonnet(const CustomCamera& cam, const HoodCameraProfile& p) {
    CameraRig c;
    c.set_custom_cameras({cam});
    c.set_mode(CameraMode::Custom);
    c.set_hood_profile(p);
    return c;
}

}

TEST(hood_camera_profile_sedan) {
    const scene::Model b = sedan();
    HoodCameraProfile p = make_hood_camera_profile(b, 2.6f);
    CHECK(p.valid);
    CHECK(near(p.bounds_min.z, -2.25f) && near(p.bounds_max.z, 2.25f) && near(p.bounds_max.y, 1.35f));
    const float length = p.bounds_max.z - p.bounds_min.z;
    CHECK(near(p.eyes[8].z, -0.78f));
    for (size_t i = 0; i < HoodCameraProfile::kMountCount; ++i) {
        CHECK(finite(p.eyes[i]) && std::isfinite(p.surface_y[i]) && std::isfinite(p.pitch_down[i]));
        CHECK(p.pitch_down[i] >= 0 && p.pitch_down[i] <= 22.0f * kDeg + 1e-4f);
        CHECK(std::fabs(p.eyes[i].x) < 1e-5f);
        CHECK(p.eyes[i].z >= p.bounds_min.z + length * 0.25f - 1e-4f);
        CHECK(p.eyes[i].z <= p.bounds_min.z + length * 0.55f + 1e-4f);
        CHECK(near(p.eyes[i].y, p.surface_y[i] + 0.40f));
        if (i) CHECK(p.eyes[i].z <= p.eyes[i - 1].z + 1e-5f);
    }
    CHECK(near(p.surface_y[8], 0.7f));
}

TEST(hood_camera_profile_shapes) {
    for (const auto& [b, wb] : {std::pair{body(0.8f, -1.8f, 1.8f, 0.9f, 1.9f, -0.4f), 2.4f},
                                std::pair{body(0.9f, -3.2f, 1.6f, 0.65f, 1.3f, 0.4f), 2.6f},
                                std::pair{body(0.95f, -2.4f, 2.0f, 0.45f, 0.9f, 1.2f), 3.0f},
                                std::pair{sedan(), 2.6f}}) {
        HoodCameraProfile p = make_hood_camera_profile(b, wb);
        CHECK(p.valid);
        const float length = p.bounds_max.z - p.bounds_min.z;
        for (size_t i = 0; i < HoodCameraProfile::kMountCount; ++i) {
            CHECK(finite(p.eyes[i]) && std::isfinite(p.surface_y[i]));
            CHECK(p.eyes[i].z >= p.bounds_min.z + length * 0.25f - 1e-4f);
            CHECK(p.eyes[i].z <= p.bounds_min.z + length * 0.55f + 1e-4f);
            CHECK(p.eyes[i].y >= p.surface_y[i] + 0.40f - 1e-4f);
        }
    }
    const scene::Model b = sedan();
    HoodCameraProfile near_ = make_hood_camera_profile(b, 2.0f), far = make_hood_camera_profile(b, 3.4f);
    CHECK(near_.valid && far.valid && near_.eyes[8].z > far.eyes[8].z);
    HoodCameraProfile no_wb = make_hood_camera_profile(b, 0.0f);
    CHECK(no_wb.valid && finite(no_wb.eyes[8]));
    scene::Model moved = sedan();
    moved.meshes[0].transform = render::translate({0, 0.5f, 0}).m;
    HoodCameraProfile up = make_hood_camera_profile(moved, 2.6f);
    CHECK(up.valid && near(up.bounds_min.y, 0.5f) && near(up.surface_y[8], 1.2f, 1e-3f));
}

TEST(hood_camera_exclusions_and_degenerate) {
    scene::Model b = sedan();
    scene::Mesh wheel;
    wheel.name = "wheel_fl";
    add_quad(wheel, {{{-1.4f, 9.0f, -1.6f}, {-1.0f, 9.0f, -1.6f}, {-1.0f, 9.0f, -1.2f}, {-1.4f, 9.0f, -1.2f}}});
    b.meshes.push_back(wheel);
    scene::Mesh node;
    node.name = "node1";
    add_quad(node, {{{-2, 20, -3}, {2, 20, -3}, {2, 20, 3}, {-2, 20, 3}}});
    b.meshes.push_back(node);
    scene::Mesh high;
    high.name = "body_hi";
    high.lod = 1;
    add_quad(high, {{{-3, 30, -4}, {3, 30, -4}, {3, 30, 4}, {-3, 30, 4}}});
    b.meshes.push_back(high);
    scene::Mesh passes;
    passes.name = "shell_passes";
    add_quad(passes, {{{0, 50, -1}, {0.1f, 50, -1}, {0.1f, 50, -0.9f}, {0, 50, -0.9f}}}, scene::kEnvironmentMap);
    add_quad(passes, {{{0, 60, -1}, {0.1f, 60, -1}, {0.1f, 60, -0.9f}, {0, 60, -0.9f}}}, scene::kAdditive);
    add_quad(passes, {{{0, 70, -1}, {0.1f, 70, -1}, {0.1f, 70, -0.9f}, {0, 70, -0.9f}}}, scene::kDepthOnly);
    b.meshes.push_back(passes);
    HoodCameraProfile p = make_hood_camera_profile(b, 2.6f);
    CHECK(p.valid && near(p.bounds_max.y, 1.35f) && near(p.surface_y[8], 0.7f));

    CHECK(!make_hood_camera_profile(scene::Model{}, 2.6f).valid);
    scene::Model wheels_only;
    wheels_only.meshes.push_back(wheel);
    CHECK(!make_hood_camera_profile(wheels_only, 2.6f).valid);
    scene::Model broken = sedan();
    broken.meshes[0].vertices[0].pos[0] = std::nanf("");
    CHECK(make_hood_camera_profile(broken, 2.6f).valid);
    scene::Model nan_body;
    scene::Mesh nm;
    nm.name = "body";
    nm.vertices = {vertex(std::nanf(""), 0, 0), vertex(1, 0, 0), vertex(0, 1, 0)};
    nm.indices = {0, 1, 2};
    nm.primitives.push_back({0, 3});
    nan_body.meshes.push_back(nm);
    CHECK(!make_hood_camera_profile(nan_body, 2.6f).valid);
    scene::Model flat;
    scene::Mesh fm;
    fm.name = "body";
    fm.vertices = {vertex(0, 1, 0), vertex(0, 1, 0), vertex(0, 1, 0)};
    fm.indices = {0, 1, 2};
    fm.primitives.push_back({0, 3});
    flat.meshes.push_back(fm);
    CHECK(!make_hood_camera_profile(flat, 2.6f).valid);

    scene::Model collinear;
    scene::Mesh cm;
    cm.name = "body";
    cm.vertices = {vertex(-0.9f, 0, -2), vertex(0, 0, 0), vertex(0.9f, 0, 2)};
    cm.indices = {0, 1, 2};
    cm.primitives.push_back({0, 3});
    collinear.meshes.push_back(cm);
    CHECK(!make_hood_camera_profile(collinear, 2.6f).valid);
    scene::Model mixed = sedan();
    mixed.meshes[0].vertices.push_back(vertex(-0.9f, 0, -2));
    mixed.meshes[0].vertices.push_back(vertex(0, 0, 0));
    mixed.meshes[0].vertices.push_back(vertex(0.9f, 0, 2));
    const u32 base = u32(mixed.meshes[0].vertices.size()) - 3;
    for (u32 i : {base, base + 1, base + 2}) mixed.meshes[0].indices.push_back(i);
    mixed.meshes[0].primitives.push_back({u32(mixed.meshes[0].indices.size()) - 3, 3});
    CHECK(make_hood_camera_profile(mixed, 2.6f).valid);
    scene::Model huge;
    scene::Mesh hm;
    hm.name = "body";
    add_quad(hm, {{{-3e38f, 0, -1}, {3e38f, 0, -1}, {3e38f, 1, 1}, {-3e38f, 1, 1}}});
    huge.meshes.push_back(hm);
    CHECK(!make_hood_camera_profile(huge, 2.6f).valid);
}

TEST(hood_camera_eye_offsets_and_fallbacks) {
    scene::Model narrow;
    scene::Mesh sides;
    sides.name = "body";
    add_quad(sides, {{{0.5f, 0.7f, -2.2f}, {0.9f, 0.7f, -2.2f}, {0.9f, 0.7f, 2.2f}, {0.5f, 0.7f, 2.2f}}});
    add_quad(sides, {{{-0.9f, 0.7f, -2.2f}, {-0.5f, 0.7f, -2.2f}, {-0.5f, 0.7f, 2.2f}, {-0.9f, 0.7f, 2.2f}}});
    narrow.meshes.push_back(sides);
    HoodCameraProfile sparse = make_hood_camera_profile(narrow, 2.6f);
    CHECK(sparse.valid);
    for (size_t i = 0; i < HoodCameraProfile::kMountCount; ++i)
        CHECK(finite(sparse.eyes[i]) && std::isfinite(sparse.surface_y[i]) && sparse.eyes[i].y > 0);

    const HoodCameraProfile p = make_hood_camera_profile(sedan(), 2.6f);
    sim::VehicleSetup setup;
    for (size_t i = 0; i < HoodCameraProfile::kMountCount; ++i) {
        CustomCamera c = bonnet();
        c.offset.z = -0.40f + float(i) * 0.05f;
        render::Vec3 e = hood_camera_eye(p, setup, c);
        CHECK(finite(e) && near(e.z, p.eyes[i].z) && near(e.y, p.eyes[i].y) && near(e.x, 0));
        CHECK(near(hood_camera_pitch(p, c), p.pitch_down[i]));
    }
    for (float h : {-0.10f, 0.0f, 0.35f}) {
        for (size_t i = 0; i < HoodCameraProfile::kMountCount; ++i) {
            CustomCamera c = bonnet();
            c.offset.z = -0.40f + float(i) * 0.05f;
            c.offset.y = h;
            render::Vec3 e = hood_camera_eye(p, setup, c);
            CHECK(e.y >= p.surface_y[i] + 0.30f - 1e-4f && near(e.z, p.eyes[i].z));
        }
    }
    // Sideways nudges move the eye laterally, at the centre strip's height.
    {
        CustomCamera c = bonnet();
        c.offset.x = 0.3f;
        CHECK(near(hood_camera_eye(p, setup, c).x, 0.3f));
        c.offset.x = 9;
        CHECK(near(hood_camera_eye(p, setup, c).x, 0.4f));
        c.offset.x = std::nanf("");
        CHECK(near(hood_camera_eye(p, setup, c).x, 0.0f));
    }
    for (float off : {-0.6f, -0.43f, 0.37f, 0.44f, 0.8f}) {
        CustomCamera c = bonnet();
        c.offset.z = off;
        const int i = std::clamp(int(std::lround((std::clamp(off, -0.40f, 0.40f) + 0.40f) / 0.05f)), 0, 16);
        CHECK(near(hood_camera_eye(p, setup, c).z, p.eyes[size_t(i)].z));
    }
    HoodCameraProfile none;
    CHECK(hood_camera_pitch(none, bonnet()) == 0);
    for (const sim::VehicleSetup* su : {&setup, [] {
             static sim::VehicleSetup bad;
             bad.length = bad.width = bad.height = bad.wheelbase = std::nanf("");
             return &bad;
         }()}) {
        for (float off : {-0.4f, 0.0f, 0.4f})
            for (float h : {-0.1f, 0.0f, 0.35f}) {
                CustomCamera c = bonnet();
                c.offset.z = off, c.offset.y = h;
                render::Vec3 e = hood_camera_eye(none, *su, c);
                CHECK(finite(e));
                if (su == &setup) CHECK(near(e.y, 1.1425f + h, 1e-3f));
            }
    }
}

TEST(hood_camera_pitch_down) {
    scene::Model wedge;
    scene::Mesh wm;
    wm.name = "body";
    add_quad(wm, {{{-0.9f, 1.0f, -1.5f}, {0.9f, 1.0f, -1.5f}, {0.9f, 1.0f, 0.5f}, {-0.9f, 1.0f, 0.5f}}});
    add_quad(wm, {{{-0.9f, 0, -1.5f}, {0.9f, 0, -1.5f}, {0.9f, 0, 0.5f}, {-0.9f, 0, 0.5f}}});
    wedge.meshes.push_back(wm);
    wedge.compute_bounds();
    const HoodCameraProfile w = make_hood_camera_profile(wedge, 2.6f);
    CHECK(w.valid);
    const float alpha = std::atan2(w.eyes[8].y - 1.0f, w.eyes[8].z - (-1.5f));
    const float want = std::clamp(alpha - 20.0f * kDeg, 0.0f, 22.0f * kDeg);
    CHECK(alpha > 20.0f * kDeg && want > 0);
    CHECK(near(w.pitch_down[8], want, 1e-3f));

    scene::Model fin = sedan();
    add_quad(fin.meshes[0], {{{-0.5f, 0.7f, -0.9f}, {0.5f, 0.7f, -0.9f}, {0.5f, 1.6f, -0.9f}, {-0.5f, 1.6f, -0.9f}}});
    const HoodCameraProfile f = make_hood_camera_profile(fin, 2.6f);
    CHECK(f.valid);
    bool clamped = false;
    for (size_t i = 0; i < HoodCameraProfile::kMountCount; ++i)
        clamped = clamped || near(f.pitch_down[i], 22.0f * kDeg, 1e-3f);
    CHECK(clamped);
    for (size_t i = 0; i < HoodCameraProfile::kMountCount; ++i)
        CHECK(f.pitch_down[i] >= 0 && f.pitch_down[i] <= 22.0f * kDeg + 1e-4f);

    scene::Model outside;
    scene::Mesh om;
    om.name = "body";
    add_quad(om, {{{1.5f, 0.7f, -2.2f}, {2.0f, 0.7f, -2.2f}, {2.0f, 0.7f, 2.2f}, {1.5f, 0.7f, 2.2f}}});
    add_quad(om, {{{1.5f, 0, -2.2f}, {2.0f, 0, -2.2f}, {2.0f, 0, 2.2f}, {1.5f, 0, 2.2f}}});
    outside.meshes.push_back(om);
    const HoodCameraProfile r = make_hood_camera_profile(outside, 2.6f);
    CHECK(r.valid);
    for (size_t i = 0; i < HoodCameraProfile::kMountCount; ++i)
        CHECK(finite(r.eyes[i]) && std::isfinite(r.surface_y[i]) && r.pitch_down[i] == 0);
}

TEST(hood_camera_rig_pose) {
    sim::VehicleSetup setup;
    setup.wheels[0].ride_height = 0.10f;
    const HoodCameraProfile p = make_hood_camera_profile(sedan(), 2.6f);
    CameraRig c = rig_on_bonnet(bonnet(), p);
    const double dt = 1.0 / 60.0;
    sim::VehicleState s;
    s.position = {10, 0.5f, 20};
    c.update(s, setup, PadState{}, dt);
    CHECK(near(c.eye().x, 10.0f) && near(c.eye().y, 0.5f + 0.10f + p.eyes[8].y) && near(c.eye().z, 20 + p.eyes[8].z));
    CHECK(near(view_fwd(c).z, -1.0f, 0.02f));
    CHECK(near(view_up(c).y, 1.0f, 0.1f));
    // The car is shown ahead (not the in-car "onboard"): its bonnet, the in-car HUD and sounds.
    CHECK(!c.onboard() && c.incar_hud() && !c.hides_car() && c.shows_mirror() && c.sound_kind() == CameraSound::Cockpit);
    CHECK(near(c.fovy(), 55.0f * kDeg));
    CameraRig b;
    b.update(s, setup, PadState{}, dt);
    CHECK(!near(b.fovy(), c.fovy()));
    s.orientation = axis_angle(0, 1, 0, 3.14159265f * 0.5f);
    c.update(s, setup, PadState{}, dt);
    CHECK(near(c.eye().x, 10 + p.eyes[8].z, 1e-3f) && near(c.eye().z, 20.0f) && near(view_fwd(c).x, -1.0f, 1e-2f));
    s.orientation = {1, 0, 0, 0};
    // Shown: the body blocks the view (the screen fades the car out) only looking back across it;
    // it is never hidden outright.
    for (int i = 0; i < 30; ++i) c.set_look(1, 0), c.update(s, setup, PadState{}, dt);
    CHECK(!c.body_blocks_view() && !c.hides_car());
    for (int i = 0; i < 60; ++i) c.set_look(0, 1), c.update(s, setup, PadState{}, dt);
    CHECK(c.body_blocks_view() && !c.hides_car() && !c.shows_mirror() && view_fwd(c).z > 0.9f);
    c.set_look(0, 0);
    c.set_look_back(true);
    c.update(s, setup, PadState{}, dt);
    CHECK(c.body_blocks_view() && view_fwd(c).z > 0.9f);
    c.set_look_back(false);
    settle(c, s, setup);
    CHECK(!c.body_blocks_view() && c.shows_mirror());
    CustomCamera no_mirror = bonnet();
    no_mirror.mirror = false;
    c.set_custom_cameras({no_mirror});
    CHECK(!c.shows_mirror());
    // Shown: the car is left out only on or in the body looking back across it; a camera clear of
    // the body (a chase 6 m back, 2 m up) draws it looking back too. Hidden never draws it.
    CustomCamera chase = bonnet();
    chase.mount = CustomCamera::Mount::Follow;
    chase.offset = {0, 2, 6};
    c.set_custom_cameras({chase});
    c.set_look_back(true);
    c.update(s, setup, PadState{}, dt);
    CHECK(!c.body_blocks_view() && !c.hides_car());
    CustomCamera hidden = bonnet();
    hidden.car = CustomCamera::OwnCar::Hidden;
    c.set_custom_cameras({hidden});
    c.update(s, setup, PadState{}, dt);
    CHECK(c.hides_car() && c.onboard() && !c.body_blocks_view()); // (hidden: no fade, never drawn)
    c.set_look_back(false);
    c.set_custom_cameras({bonnet()});
    c.set_intro(true, 0.5f);
    c.update(s, setup, PadState{}, dt);
    CHECK(!c.onboard() && !c.hides_car() && !c.shows_mirror() && c.sound_kind() == CameraSound::Other);
}

TEST(hood_camera_rig_horizon) {
    constexpr float kPi = 3.14159265f;
    sim::VehicleSetup setup;
    const HoodCameraProfile p = make_hood_camera_profile(sedan(), 2.6f);
    sim::VehicleState s;
    const double dt = 1.0 / 60.0;
    auto up_at = [&](float lock) {
        CameraRig c = rig_on_bonnet(bonnet(lock), p);
        c.update(s, setup, PadState{}, dt);
        return view_up(c);
    };
    s.orientation = axis_angle(0, 0, -1, 0);
    CHECK(near(up_at(0.5f).x, 0) && near(up_at(1.0f).x, 0));
    s.orientation = axis_angle(0, 0, 1, -kPi / 6);
    const render::Vec3 u0 = up_at(0), u5 = up_at(0.5f), u1 = up_at(1);
    CHECK(near(u0.x, 0.5f, 1e-3f) && near(u0.y, std::cos(kPi / 6), 1e-3f));
    CHECK(near(u1.x, 0.0f, 1e-4f) && near(u1.y, 1.0f, 1e-4f));
    CHECK(u5.x < u0.x && u5.x > u1.x && u5.y > u0.y && u5.y < u1.y);
    CHECK(near(u5.x, std::sin(kPi / 12), 0.01f));
    s.orientation = axis_angle(1, 0, 0, kPi / 9);
    CustomCamera level = bonnet(1.0f);
    CameraRig pitched = rig_on_bonnet(level, p);
    pitched.update(s, setup, PadState{}, dt);
    const float tilt = hood_camera_pitch(p, level);
    const render::Vec3 f = view_fwd(pitched);
    CHECK(near(f.y, std::sin(kPi / 9 - tilt), 1e-3f));
    s.orientation = axis_angle(1, 0, 0, kPi / 2);
    pitched.update(s, setup, PadState{}, dt);
    {
        const render::Mat4 v = pitched.view();
        CHECK(std::all_of(v.m.begin(), v.m.end(), [](float x) { return std::isfinite(x); }));
    }
    CHECK(finite(pitched.eye()));
    s.orientation = axis_angle(0, 1, 0, kPi * 1.5f);
    pitched.update(s, setup, PadState{}, dt);
    {
        const render::Mat4 v = pitched.view();
        CHECK(std::all_of(v.m.begin(), v.m.end(), [](float x) { return std::isfinite(x); }));
    }
    s.orientation = axis_angle(0, 0, 1, kPi);
    for (float lock : {0.0f, 0.5f, 1.0f}) {
        const render::Vec3 u = up_at(lock);
        CHECK(finite(u) && render::length(u) > 0.9f);
    }
}

TEST(hood_camera_rig_look_around) {
    constexpr float kPi = 3.14159265f;
    sim::VehicleSetup setup;
    const HoodCameraProfile p = make_hood_camera_profile(sedan(), 2.6f);
    const double dt = 1.0 / 60.0;
    for (const auto& [label, ori] : {std::pair{"identity", sim::Quat{1, 0, 0, 0}},
                                     std::pair{"pitched", axis_angle(1, 0, 0, kPi / 9)}}) {
        for (float lock : {0.0f, 0.5f, 1.0f})
            for (float angle : {45.0f, 90.0f, 135.0f, 180.0f}) {
                sim::VehicleState s;
                s.orientation = ori;
                CameraRig c = rig_on_bonnet(bonnet(lock), p);
                const float rad = angle * kDeg;
                for (int i = 0; i < 120; ++i) c.set_look(std::sin(rad), -std::cos(rad)), c.update(s, setup, PadState{}, dt);
                const render::Mat4 v = c.view();
                CHECK(orthonormal(v));
                CHECK(view_up(c).y > 0.0f);
                CHECK(!c.hides_car()); // (shown: faded by the screen when it blocks, never hidden)
                if (angle <= 90.0f) CHECK(!c.body_blocks_view());
                else CHECK(c.body_blocks_view());
            }
    }
}

TEST(hood_camera_rig_steps_and_cuts) {
    sim::VehicleSetup setup;
    const HoodCameraProfile p = make_hood_camera_profile(sedan(), 2.6f),
                             q = make_hood_camera_profile(body(0.9f, -2.0f, 2.0f, 0.8f, 1.5f, -0.4f), 3.0f);
    CameraRig c = rig_on_bonnet(bonnet(), p);
    sim::VehicleState s;
    s.velocity = {0, 0, -20};
    for (double dt : {1.0 / 30.0, 1.0 / 60.0, 1.0 / 120.0, 0.0, 0.5}) {
        c.update(s, setup, PadState{}, dt);
        const render::Mat4 v = c.view();
        CHECK(finite(c.eye()) && std::all_of(v.m.begin(), v.m.end(), [](float x) { return std::isfinite(x); }));
        s.position = s.position + s.velocity * 0.01f;
    }
    const render::Vec3 before = c.eye();
    c.set_hood_profile(q);
    c.update(s, setup, PadState{}, 1.0 / 60.0);
    CHECK(render::length(c.eye() - before) > 0.05f);
    CHECK(near(c.eye_velocity().x, 0) && near(c.eye_velocity().z, 0));
    c.set_hood_profile(HoodCameraProfile{});
    c.update(s, setup, PadState{}, 1.0 / 60.0);
    CHECK(finite(c.eye()) && !near(c.eye().z, 0) && near(c.eye_velocity().z, 0));
    s.position = s.position + s.velocity * float(1.0 / 60.0);
    c.update(s, setup, PadState{}, 1.0 / 60.0);
    CHECK(near(c.eye_velocity().z, -20.0f, 0.1f));
    c.set_mode(CameraMode::Chase);
    c.update(s, setup, PadState{}, 1.0 / 60.0);
    CHECK(c.eye_velocity().z == 0);
}
