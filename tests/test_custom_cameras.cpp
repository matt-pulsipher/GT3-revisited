// The player's own race cameras (race/custom_cameras.h): config.ini, the editor's moves, and the
// camera rig's custom view (change view's rotation, the pose, the mirror, HUD and sounds).

#include "test_framework.h"

#include "base/config.h"
#include "race/camera_rig.h"
#include "race/custom_cameras.h"
#include "race/hood_camera.h"
#include "race/interp.h"

#include <cmath>

using namespace gt3r;
using namespace gt3r::race;

namespace {

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool near(const render::Vec3& a, const render::Vec3& b, float eps = 1e-3f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}
bool near(const render::Mat4& a, const render::Mat4& b, float eps = 1e-4f) {
    for (int i = 0; i < 16; ++i)
        if (!near(a.m[size_t(i)], b.m[size_t(i)], eps)) return false;
    return true;
}
render::Vec3 view_forward(const render::Mat4& v) { return {-v.at(2, 0), -v.at(2, 1), -v.at(2, 2)}; }

// The same camera as written and read back (numbers are written to 3 decimals, for hand editing).
bool same(const CustomCamera& x, const CustomCamera& y) {
    CustomCamera r = y;
    r.offset = x.offset, r.yaw = x.yaw, r.pitch = x.pitch, r.roll = x.roll, r.fov = x.fov, r.body_roll = x.body_roll;
    r.mirror_x = x.mirror_x, r.mirror_y = x.mirror_y, r.mirror_size = x.mirror_size;
    return r == x && near(x.offset, y.offset) && near(x.yaw, y.yaw) && near(x.pitch, y.pitch) && near(x.roll, y.roll) &&
           near(x.body_roll, y.body_roll) &&
           near(x.fov, y.fov) && near(x.mirror_x, y.mirror_x) && near(x.mirror_y, y.mirror_y) &&
           near(x.mirror_size, y.mirror_size);
}

CustomCamera camera(int id, const char* name) {
    CustomCamera c;
    c.id = id;
    c.name = name;
    return c;
}

} // namespace

// Saved as [custom_camera<n>] sections in rotation order, read back the same; other sections stay;
// broken sections are skipped and wild values brought into range.
TEST(custom_cameras_config) {
    IniDocument cfg = IniDocument::parse("[race]\nviews = gt3\n[custom_camera7]\nid = 9\nname = Old\n");
    CustomCamera a = camera(4, "Hood");
    a.enabled = false;
    a.mount = CustomCamera::Mount::Hood;
    a.offset = {0.1f, 0.25f, -0.3f}; // (a bonnet nudge: inside the mounts' bounds)
    a.yaw = 12, a.pitch = -4.5f, a.roll = 3, a.body_roll = 0.5f;
    a.fov = 55;
    a.car = CustomCamera::OwnCar::Hidden, a.incar_hud = true;
    a.mirror = true, a.mirror_x = 0.25f, a.mirror_y = 0.8f, a.mirror_size = 1.5f;
    CustomCamera b = camera(2, "Blimp");
    CustomCamera c = camera(6, "Seat");
    c.mount = CustomCamera::Mount::Incar;
    c.offset = {0.1f, 0.05f, -0.2f}; // (a seat nudge: inside the seat's bounds)
    save_custom_cameras(cfg, {a, b, c});
    CHECK(cfg.get("race", "views") == "gt3");
    CHECK(!cfg.find("custom_camera7")); // (the old sections are gone)
    CHECK(cfg.get("custom_camera1", "name") == "Hood" && cfg.get("custom_camera2", "name") == "Blimp");
    CHECK(cfg.get("custom_camera2", "mirror_y") == "0.116" && cfg.get("custom_camera2", "z") == "6");
    CHECK(cfg.get("custom_camera3", "mount") == "incar" && cfg.get("custom_camera1", "mount") == "hood");
    CHECK(cfg.get("custom_camera1", "body_roll") == "50" && cfg.get("custom_camera1", "car") == "hidden");
    const std::vector<CustomCamera> back = load_custom_cameras(cfg);
    CHECK(back.size() == 3 && same(back[0], a) && same(back[1], b) && same(back[2], c)); // (3 decimals)
    CHECK(find_custom_camera(back, 2) == &back[1] && !find_custom_camera(back, 3));
    CHECK(new_custom_camera_id(back) == 7);
    CHECK(new_custom_camera_name(back) == "Camera 4");
    CHECK(new_custom_camera_name({camera(1, "Camera 1"), camera(2, "Camera 3")}) == "Camera 4");

    // Hand-edited: no name, no id or a repeated id are skipped; numbers out of range clamped.
    const IniDocument hand = IniDocument::parse("[custom_camera1]\nid = 1\nname = Wide\nfov = 500\npitch = 200\n"
                                                "z = 100\nmirror_size = 10\nmirror_x = -2\nyaw = 270\n"
                                                "[custom_camera2]\nid = 1\nname = Same id\n"
                                                "[custom_camera3]\nid = 3\n"
                                                "[custom_camera4]\nname = No id\n"
                                                "[custom_camera_x]\nid = 5\nname = Not a camera\n");
    const std::vector<CustomCamera> h = load_custom_cameras(hand);
    CHECK(h.size() == 1 && h[0].name == "Wide");
    CHECK(h[0].fov == CustomCamera::kMaxFov && h[0].pitch == CustomCamera::kMaxPitch && near(h[0].yaw, -90));
    CHECK(near(render::length(h[0].offset), CustomCamera::kMaxDistance));
    CHECK(h[0].mirror_size == CustomCamera::kMaxMirror && h[0].mirror_x == 0);
    CHECK(h[0].enabled && h[0].mount == CustomCamera::Mount::Follow && h[0].car == CustomCamera::OwnCar::Shown &&
          !h[0].mirror && h[0].body_roll == 1);

    // The hood mount (and "bonnet"), body roll (and an early build's roll_lock, its inverse), and
    // the early "ahead" car (now shown, left out on its own), as hand-edited words and numbers.
    const std::vector<CustomCamera> bn =
        load_custom_cameras(IniDocument::parse("[custom_camera1]\nid = 7\nname = Bonnet\nmount = bonnet\ncar = ahead\n"
                                               "roll_lock = 40\n"
                                               "[custom_camera2]\nid = 8\nname = Hood\nmount = hood\nbody_roll = 300\n"
                                               "[custom_camera3]\nid = 9\nname = Far\nmount = bonnet\n"
                                               "x = 9\ny = 9\nz = 9\n"
                                               "[custom_camera4]\nid = 10\nname = Chase\nx = 9\ny = 9\nz = 9\n"));
    CHECK(bn.size() == 4);
    CHECK(bn[0].mount == CustomCamera::Mount::Hood && bn[0].car == CustomCamera::OwnCar::Shown &&
          near(bn[0].body_roll, 0.6f));
    CHECK(bn[1].mount == CustomCamera::Mount::Hood && near(bn[1].body_roll, 1.0f)); // (clamped)
    CHECK(bn[3].body_roll == 1); // (the default: rolls with the car)
    // A bonnet offset is a nudge kept inside the mounts' bounds; any other mount's is a position.
    CHECK(near(bn[2].offset.x, kHoodLateral) && near(bn[2].offset.y, kHoodHeightMax) && near(bn[2].offset.z, kHoodForward));
    CHECK(near(bn[3].offset.x, 9.0f) && near(bn[3].offset.z, 9.0f));

    // The in-car mount (and its "cockpit" alias): a seat nudge bounded like the bonnet's — the
    // default position offset clamps into the seat's box rather than the mounts'.
    const std::vector<CustomCamera> ic =
        load_custom_cameras(IniDocument::parse("[custom_camera1]\nid = 11\nname = Seat\nmount = incar\n"
                                               "x = 9\ny = -9\nz = 9\n"
                                               "[custom_camera2]\nid = 12\nname = Cockpit\nmount = cockpit\n"));
    CHECK(ic.size() == 2);
    CHECK(ic[0].mount == CustomCamera::Mount::Incar && near(ic[0].offset.x, kSeatLateral) &&
          near(ic[0].offset.y, kSeatHeightMin) && near(ic[0].offset.z, kSeatForward));
    CHECK(ic[1].mount == CustomCamera::Mount::Incar && near(ic[1].offset.y, kSeatHeightMax) &&
          near(ic[1].offset.z, kSeatForward));
}

// Camera files (Export / Import): the cameras round-trip; imported ones get new ids and, when a
// name is taken, a number; no more than the limit; anything else imports nothing.
TEST(custom_cameras_files) {
    CustomCamera a = camera(3, "Hood");
    a.mount = CustomCamera::Mount::Body;
    a.offset = {0, 0.1f, -1.25f};
    a.fov = 60, a.mirror = true, a.mirror_size = 1.5f;
    const CustomCamera b = camera(7, "Blimp");
    const std::string text = export_custom_cameras({a, b});
    CHECK(text.rfind("# GT3 Revisited cameras", 0) == 0 && text.find("[gt3r_cameras]") != std::string::npos);
    const std::vector<CustomCamera> in = import_custom_cameras(text);
    CHECK(in.size() == 2 && same(in[0], a) && same(in[1], b));

    std::vector<CustomCamera> mine = {camera(1, "Hood"), camera(2, "Hood 2")};
    CHECK(add_custom_cameras(mine, in) == 2);
    CHECK(mine.size() == 4 && mine[2].id == 3 && mine[3].id == 4);
    CHECK(mine[2].name == "Hood 3" && mine[3].name == "Blimp");
    CHECK(mine[2].mount == CustomCamera::Mount::Body && mine[2].fov == 60 && mine[2].mirror);

    std::vector<CustomCamera> full;
    for (int i = 0; i < CustomCamera::kMaxCameras - 1; ++i) full.push_back(camera(i + 1, "C"));
    CHECK(add_custom_cameras(full, in) == 1 && int(full.size()) == CustomCamera::kMaxCameras);

    // A whole config.ini imports its cameras; other text has none.
    IniDocument cfg = IniDocument::parse("[race]\nviews = all\n");
    save_custom_cameras(cfg, {a});
    CHECK(import_custom_cameras(cfg.serialize()).size() == 1);
    CHECK(import_custom_cameras("not a camera file").empty() && import_custom_cameras("").empty());

    CHECK(camera_file_name("Hood") == "Hood.ini");
    CHECK(camera_file_name("a/b:c*?\"<>|d.") == "abcd.ini");
    CHECK(camera_file_name("  ") == "Camera.ini");
}

// The editor's moves: orbit keeps the distance to the pivot and the pivot where it is in the view;
// look turns in place; pan moves along the view's axes.
TEST(custom_cameras_moves) {
    CustomCamera c;
    render::Vec3 f, u;
    c.axes(f, u);
    CHECK(near(f, {0, 0, -1}) && near(u, {0, 1, 0}));
    c.yaw = 90; // turned right: +x
    c.axes(f, u);
    CHECK(near(f, {1, 0, 0}));
    c.yaw = 0, c.pitch = 30, c.roll = 20; // up, then rolled clockwise: up leans right
    c.axes(f, u);
    CHECK(near(f, {0, 0.5f, -0.8660254f}) && u.x > 0.3f);

    // A camera 6 m behind the pivot, aimed at it.
    const render::Vec3 pivot{0, 0.5f, 0};
    c = CustomCamera{};
    c.offset = {0, 0.5f, 6};
    c.yaw = c.pitch = 0;
    for (auto [az, el] : {std::pair{0.7f, 0.0f}, std::pair{-1.2f, 0.4f}, std::pair{2.0f, -0.3f}}) {
        orbit_camera(c, pivot, az, el);
        CHECK(near(render::length(c.offset - pivot), 6.0f));
        c.axes(f, u);
        CHECK(near(render::normalize(pivot - c.offset), f)); // still aimed at it
    }
    // Going right of the car (+x) turns the view left.
    c = CustomCamera{};
    c.offset = {0, 0.5f, 6};
    orbit_camera(c, pivot, 0.5f, 0);
    CHECK(c.offset.x > 2 && c.yaw < -20);
    // Over the top: elevation stops short of straight down.
    orbit_camera(c, pivot, 0, 3.0f);
    CHECK(c.pitch >= -CustomCamera::kMaxPitch && c.offset.y > 5.5f);

    c = CustomCamera{};
    look_camera(c, 0.1f, -0.2f);
    CHECK(near(c.yaw, 5.729578f) && near(c.pitch, -11.459156f));
    look_camera(c, 0, -10);
    CHECK(c.pitch == -CustomCamera::kMaxPitch);

    c = CustomCamera{};
    const render::Vec3 o = c.offset;
    pan_camera(c, 1, 2, 3); // right, up, forward (into -z)
    CHECK(near(c.offset, o + render::Vec3{1, 2, -3}));
    pan_camera(c, 0, 0, -500); // never past the limit
    CHECK(render::length(c.offset) <= CustomCamera::kMaxDistance + 1e-3f);
}

// Change view: the enabled custom cameras follow the last built-in view, in order, then the
// bumper again; GT3's two views too. A remembered camera that is gone: the in-car view.
TEST(race_camera_custom_views) {
    CameraRig c;
    c.set_custom_cameras({camera(5, "A"), camera(2, "B")});
    c.next_mode(), c.next_mode(), c.next_mode(); // bumper without mirror, chase, far chase
    CHECK(c.mode() == CameraMode::ChaseFar);
    c.next_mode();
    CHECK(c.mode() == CameraMode::Custom && c.custom() && c.custom()->name == "A" && c.view_name() == "A");
    const CameraRig::Shot a = c.shot();
    c.next_mode();
    CHECK(c.mode() == CameraMode::Custom && c.custom_id() == 2);
    CHECK(!(c.shot() == a)); // a cut between them
    c.next_mode();
    CHECK(c.mode() == CameraMode::Bumper && c.mirror());

    CameraRig g;
    g.set_gt3_views(true);
    g.set_custom_cameras({camera(5, "A")});
    g.next_mode();
    CHECK(g.mode() == CameraMode::Chase);
    g.next_mode();
    CHECK(g.mode() == CameraMode::Custom && g.custom_id() == 5);
    g.next_mode();
    CHECK(g.mode() == CameraMode::Bumper);

    CameraRig none; // no cameras: as before
    for (int i = 0; i < 4; ++i) none.next_mode();
    CHECK(none.mode() == CameraMode::Bumper && none.mirror());

    CameraRig r;
    r.set_custom_cameras({camera(5, "A"), camera(2, "B")});
    r.set_view(CameraMode::Custom, true, 2);
    CHECK(r.custom() && r.custom()->name == "B");
    r.set_view(CameraMode::Custom, true, 9); // deleted since
    CHECK(r.mode() == CameraMode::Bumper && r.mirror());
    CameraRig f;
    f.set_custom_cameras({camera(5, "A"), camera(2, "B")});
    f.set_mode(CameraMode::Custom); // (no id: the first)
    CHECK(f.custom_id() == 5);
}

// A custom camera made from a built-in view is that view; the HUD, the car, the mirror and the
// sounds follow the camera's choices; look back turns it round.
TEST(race_camera_custom_pose) {
    sim::VehicleSetup setup;
    sim::VehicleState s;
    s.position = {10, 2, -30};
    const float h = 0.4f;
    s.orientation = {std::cos(h / 2), 0, std::sin(h / 2), 0}; // turned, so the frames are not the axes
    const double dt = 1.0 / 60.0;
    for (CameraMode m : {CameraMode::Bumper, CameraMode::Chase}) {
        CameraRig built, custom;
        built.set_shake(false);
        built.set_mode(m);
        CustomCamera k = built.preset(m, setup);
        k.id = 1, k.name = "Copy";
        custom.set_custom_cameras({k});
        custom.set_mode(CameraMode::Custom);
        for (int i = 0; i < 3; ++i) built.update(s, setup, PadState{}, dt), custom.update(s, setup, PadState{}, dt);
        CHECK(near(built.eye(), custom.eye()) && near(built.view(), custom.view()) && near(built.fovy(), custom.fovy(), 1e-5f));
        CHECK(near(custom.mirror_view(), built.mirror_view()) == (m == CameraMode::Bumper));
        CHECK(custom.onboard() == built.onboard() && custom.incar_hud() == built.incar_hud());
        CHECK(custom.sound_kind() == built.sound_kind());
        CHECK(custom.shows_mirror() == (m == CameraMode::Bumper));
    }

    // A chase-like camera with its mirror on: the mirror looks back from the driver's eye.
    CameraRig c, bumper;
    bumper.set_shake(false);
    CustomCamera k = camera(1, "High");
    k.offset = {0, 3, 8};
    k.pitch = -15;
    k.mirror = true;
    c.set_custom_cameras({k});
    c.set_mode(CameraMode::Custom);
    c.update(s, setup, PadState{}, dt);
    bumper.update(s, setup, PadState{}, dt);
    CHECK(!c.onboard() && !c.incar_hud() && c.shows_mirror() && c.sound_kind() == CameraSound::Chase);
    CHECK(near(c.mirror_eye(), bumper.eye()) && near(c.mirror_view(), bumper.view()));
    CHECK(near(c.fovy(), k.fovy(), 1e-5f));
    const render::Vec3 ahead = view_forward(c.view());
    c.set_look_back(true);
    c.update(s, setup, PadState{}, dt);
    // (Pitched 15 degrees down both ways: -cos 30 degrees.)
    CHECK(near(render::dot(view_forward(c.view()), ahead), -0.8660254f) && !c.shows_mirror());
    // Hidden car, in-car HUD, fixed to the body: the in-car look, a car-mounted sound.
    k.car = CustomCamera::OwnCar::Hidden, k.incar_hud = true, k.mount = CustomCamera::Mount::Body;
    c.set_custom_cameras({k});
    CHECK(c.onboard() && c.incar_hud() && c.sound_kind() == CameraSound::Cockpit);
    c.set_intro(true, 0.5f); // the pre-race shot is outside the car
    CHECK(!c.onboard() && !c.incar_hud() && !c.shows_mirror());
}

// A headset's seat for a custom camera (CameraRig::custom_seat; docs/dev/VR.md): the camera's eye
// and heading, its pitch and tilt left to the head.
TEST(race_camera_custom_seat) {
    sim::VehicleSetup setup;
    sim::VehicleState s;
    s.position = {10, 2, -30};
    const float n = std::sqrt(0.9f * 0.9f + 0.1f * 0.1f + 0.4f * 0.4f + 0.15f * 0.15f);
    s.orientation = {0.9f / n, 0.1f / n, 0.4f / n, 0.15f / n}; // turned, pitched and rolled
    const double dt = 1.0 / 60.0;
    auto vec = [](const sim::Vec3& v) { return render::Vec3{float(v.x), float(v.y), float(v.z)}; };
    const render::Vec3 car_up = vec(rotate(s.orientation, {0, 1, 0})), car_fwd = vec(rotate(s.orientation, {0, 0, -1}));

    CameraRig c;
    c.set_shake(false);
    CustomCamera k = camera(1, "Left seat");
    k.mount = CustomCamera::Mount::Body;
    k.offset = {-0.35f, 0.1f, 0.2f};
    k.yaw = 30;
    auto place = [&] {
        c.set_custom_cameras({k});
        c.set_mode(CameraMode::Custom);
        c.update(s, setup, PadState{}, dt);
    };
    // A camera with no pitch or tilt, fixed to the car: the seat is its view.
    place();
    const render::Mat4 seat = c.custom_seat(false, {});
    CHECK(near(render::inverse(seat), c.view()));
    CHECK(near(seat.transform_point({0, 0, 0}), c.eye()));
    // Pitched and tilted: the same seat.
    k.pitch = -20, k.roll = 10;
    place();
    CHECK(near(c.custom_seat(false, {}), seat) && !near(render::inverse(seat), c.view()));
    // The nudge moves it in the car's frame (z back), whichever way the camera is turned.
    CHECK(near(c.custom_seat(false, {0, 0.1f, -0.2f}).transform_point({0, 0, 0}), c.eye() + car_up * 0.1f + car_fwd * 0.2f));
    // Level: the same eye and heading, upright.
    {
        const render::Mat4 level = c.custom_seat(true, {});
        const render::Vec3 up = level.transform_point({0, 1, 0}) - c.eye();
        const render::Vec3 a = level.transform_point({0, 0, -1}) - c.eye(), b = seat.transform_point({0, 0, -1}) - c.eye();
        CHECK(near(level.transform_point({0, 0, 0}), c.eye()) && near(up, {0, 1, 0}));
        CHECK(a.x * b.x + a.z * b.z > 0.9f * std::hypot(b.x, b.z));
    }
    // None of the body's roll (body_roll 0): facing ahead, the seat's side axis is level.
    k.body_roll = 0, k.yaw = 0;
    place();
    CHECK(near((c.custom_seat(false, {}).transform_point({1, 0, 0}) - c.eye()).y, 0.0f));
    CHECK(!near((seat.transform_point({1, 0, 0}) - seat.transform_point({0, 0, 0})).y, 0.0f, 0.05f)); // (the car is rolled)
    // Look back: the seat turns round with the camera.
    k.pitch = k.roll = 0, k.body_roll = 1, k.yaw = 30;
    c.set_look_back(true);
    place();
    CHECK(near(render::inverse(c.custom_seat(false, {})), c.view()));
    c.set_look_back(false);
    // Following the car: level, along the camera's heading.
    k.mount = CustomCamera::Mount::Follow;
    k.offset = {0, 3, 8};
    k.pitch = -15;
    place();
    {
        const render::Mat4 follow = c.custom_seat(false, {});
        const render::Vec3 f = follow.transform_point({0, 0, -1}) - c.eye(), v = view_forward(c.view());
        CHECK(near(follow.transform_point({0, 0, 0}), c.eye()) && near(follow.transform_point({0, 1, 0}) - c.eye(), {0, 1, 0}));
        CHECK(near(f.y, 0.0f) && f.x * v.x + f.z * v.z > 0.99f * std::hypot(v.x, v.z));
    }
}
