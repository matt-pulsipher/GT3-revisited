// Race scene: control mapping, fixed-tick stepping and interpolation, rumble,
// GTM1 render-state decoding used by the course renderer, car setup.

#include "test_framework.h"
#include "car_db_synth.h"
#include "replay_fixtures.h"
#include "course_fixtures.h"

#include "formats/course/course_file.h"
#include "formats/course/course_model.h"
#include "formats/model/gtm1.h"
#include "game/data/car_database.h"
#include "formats/db/gt_db.h"
#include "game/gt_save.h"
#include "race/backend.h"
#include "race/camera_rig.h"
#include "race/controls.h"
#include "race/demo_replay.h"
#include "race/car_sfx.h"
#include "race/field.h"
#include "race/ghost.h"
#include "race/results.h"
#include "formats/menu/mbl.h"
#include "formats/menu/pmb.h"
#include "race/hud.h"
#include "race/time_text.h"
#include "race/license.h"
#include "race/paint.h"
#include "race/race_panels.h"
#include "race/exact_car.h"
#include "race/exact_field.h"
#include "race/exact_setup.h"
#include "disc/core_image.h"
#include "sim/exact/race.h"
#include "race/interp.h"
#include "race/lamp_glows.h"
#include "race/race_audio.h"
#include "race/replay.h"
#include "sim/exact/race_record.h"
#include "sim/exact/tick.h"
#include "race/replay_cameras.h"
#include "race/point_lights.h"
#include "race/pylons.h"
#include "race/race_scene.h"
#include "race/road_shade.h"
#include "race/sun_glare.h"
#include "render/view3d.h"
#include "race/tyre_smoke.h"
#include "race/arcade_car.h"
#include "menu/flow/game_hooks.h"
#include "audio/voice_mixer.h"
#include "formats/course/road.h"
#include "track/track.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <map>
#include <cmath>
#include <set>
#include <cstdlib>
#include <cstring>

using namespace gt3r;
using namespace gt3r::race;
using gt3r::test::test_disc;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// The disc's program data (the exact core's), loaded once.
std::shared_ptr<const sim::exact::ProgramData> program() {
    static std::shared_ptr<const sim::exact::ProgramData> d;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (Disc* disc = test_disc())
            if (std::optional<Bytes> core = disc->volume().read_program())
                d = std::make_shared<const sim::exact::ProgramData>(sim::exact::ProgramData::load(CoreImage::decode(*core)));
    }
    return d;
}

PadState pad_with(PadButton b, u8 pressure) {
    PadState p;
    p.connected = true;
    p.buttons |= u16(1u << unsigned(b));
    p.pressure[size_t(b)] = pressure;
    return p;
}

} // namespace

TEST(race_stick_mapping) {
    ControlConfig cfg;
    CHECK(stick_to_steer(128, cfg) == 0);
    CHECK(stick_to_steer(131, cfg) == 0); // inside the deadzone
    CHECK(stick_to_steer(125, cfg) == 0);
    CHECK(near(stick_to_steer(255, cfg), 1.0f));
    CHECK(near(stick_to_steer(0, cfg), -1.0f));
    // Monotonic and symmetric.
    float prev = -2;
    for (int a = 0; a < 256; ++a) {
        float v = stick_to_steer(u8(a), cfg);
        CHECK(v >= prev - 1e-6f);
        prev = v;
    }
    CHECK(near(stick_to_steer(192, cfg), -stick_to_steer(64, cfg), 0.02f));
    // Response curve: half deflection gives less than half lock.
    CHECK(stick_to_steer(192, cfg) < 0.5f && stick_to_steer(192, cfg) > 0.3f);
}

TEST(race_pedal_mapping) {
    ControlConfig cfg;
    CHECK(pressure_to_pedal(0, cfg) == 0);
    CHECK(pressure_to_pedal(u8(cfg.pedal_deadzone), cfg) == 0);
    CHECK(near(pressure_to_pedal(255, cfg), 1.0f));
    CHECK(pressure_to_pedal(128, cfg) > 0.45f && pressure_to_pedal(128, cfg) < 0.55f);
}

// Reverse ("back"): Triangle on the race pad (any race layout drives it); it requests reverse while at least the
// throttle, driving through the brake in AT (the sim swaps the pedals in reverse) and the
// throttle in MT; more throttle cancels it.
TEST(race_control_reverse) {
    const double dt = 1.0 / 60.0;
    ControlMapper m;
    sim::ControlInputs in = m.update(pad_with(PadButton::Triangle, 0), dt);
    CHECK(m.reverse() && in.brake == 1.0f && in.throttle == 0);
    m.config().automatic = false;
    in = m.update(pad_with(PadButton::Triangle, 0), dt);
    CHECK(m.reverse() && in.throttle == 1.0f);
    PadState both = pad_with(PadButton::Triangle, 60);
    both.buttons |= 1u << unsigned(PadButton::Cross);
    both.pressure[size_t(PadButton::Cross)] = 255;
    m.update(both, dt);
    CHECK(!m.reverse());
    ControlMapper shift;
    shift.update(pad_with(PadButton::L2, 0), dt); // L2 is shift down, never reverse
    CHECK(!shift.reverse());
}

// Change view: GT3's two views (in-car with the mirror, chase; in-car first), plus ours: in-car
// without the mirror and a far chase.
TEST(race_camera_views) {
    CameraRig c;
    CHECK(c.mode() == CameraMode::Bumper && c.mirror());
    c.next_mode();
    CHECK(c.mode() == CameraMode::Bumper && !c.mirror());
    c.next_mode();
    CHECK(c.mode() == CameraMode::Chase);
    c.next_mode();
    CHECK(c.mode() == CameraMode::ChaseFar);
    c.next_mode();
    CHECK(c.mode() == CameraMode::Bumper && c.mirror());
    // The pre-race shot is outside the car even in the in-car view (own car drawn, no mirror).
    CHECK(c.onboard());
    c.set_intro(true, 0.5f);
    CHECK(!c.onboard());
    c.set_intro(false, 1.0f);
    CHECK(c.onboard());
    // The shot by the frames left to the start signal C: from a race's 10 s hold (C 600, t 0.1)
    // to C 60 (t 1), then the player's view; 1 s dissolves at C 180 and 120 (t 0.8 and 0.9, the
    // shot's own cuts) and 60 (the cut away). A licence test's 4 s hold starts at t 0.7.
    CHECK(start_shot_on(600) && start_shot_on(60) && !start_shot_on(59) && !start_shot_on(0));
    CHECK(near(start_shot_t(600), 0.1f) && near(start_shot_t(240), 0.7f) && near(start_shot_t(180), 0.8f) &&
          near(start_shot_t(120), 0.9f) && near(start_shot_t(60), 1.0f));
    int dissolves = 0;
    for (int f = 600; f > 0; --f) dissolves += start_shot_dissolve(f);
    CHECK(dissolves == 3 && start_shot_dissolve(180) && start_shot_dissolve(120) && start_shot_dissolve(60));
    CameraRig g;
    g.set_gt3_views(true);
    g.next_mode();
    CHECK(g.mode() == CameraMode::Chase);
    g.next_mode();
    CHECK(g.mode() == CameraMode::Bumper && g.mirror());
    g.set_view(CameraMode::ChaseFar, true); // a far chase remembered from "all" views
    g.next_mode();
    CHECK(g.mode() == CameraMode::Bumper);
}

// GT3's in-car vibration: none without tyre slip; with slip the in-car view shakes (a few
// degrees at most), the chase view never does, and the switch turns it off.
TEST(race_camera_shake) {
    sim::VehicleSetup setup;
    sim::VehicleState s;
    s.speed_kmh = 150;
    auto run = [&](CameraRig& c, int frames) {
        for (int i = 0; i < frames; ++i) c.update(s, setup, PadState{}, 1.0 / 60.0);
        return c.view();
    };
    CameraRig still, base;
    base.set_shake(false);
    render::Mat4 a = run(still, 60), b = run(base, 60);
    auto diff = [](const render::Mat4& x, const render::Mat4& y) {
        float d = 0;
        for (int i = 0; i < 16; ++i) d = std::max(d, std::fabs(x.m[i] - y.m[i]));
        return d;
    };
    CHECK(diff(a, b) < 1e-5f); // no slip: no shake
    for (sim::WheelState& w : s.wheels) w.slip_angle = 0.3f, w.slip_ratio = 0.5f;
    CameraRig shaky, off, chase;
    off.set_shake(false);
    chase.set_mode(CameraMode::Chase);
    float most = 0;
    for (int i = 0; i < 120; ++i) most = std::max(most, diff(run(shaky, 1), run(off, 1)));
    CHECK(most > 1e-3f && most < 0.2f); // 8 degrees of pitch at most: 0.14
    CameraRig chase_off;
    chase_off.set_mode(CameraMode::Chase);
    chase_off.set_shake(false);
    CHECK(diff(run(chase, 60), run(chase_off, 60)) < 1e-5f);
}

// The car LOD by camera distance (< 30 m, < 80 m, beyond; at most the model's levels), and
// [video] full_car_detail = on: full detail at every distance.
TEST(race_car_lod) {
    CHECK(car_lod(5, 3) == 0);
    CHECK(car_lod(29.9f, 3) == 0);
    CHECK(car_lod(30, 3) == 1);
    CHECK(car_lod(79.9f, 3) == 1);
    CHECK(car_lod(80, 3) == 2);
    CHECK(car_lod(500, 4) == 2);
    CHECK(car_lod(500, 2) == 1);
    CHECK(car_lod(500, 1) == 0);
    CHECK(car_lod(500, 0) == 0);
    for (float d : {0.0f, 30.0f, 80.0f, 2500.0f}) CHECK(car_lod(d, 3, true) == 0);
}

TEST(race_control_mapper) {
    ControlMapper m;
    const double dt = 1.0 / 60.0;
    // Cross pressure = throttle, Square = brake; digital (pressure 0 while held) = full.
    sim::ControlInputs in = m.update(pad_with(PadButton::Cross, 128), dt);
    CHECK(in.throttle > 0.45f && in.throttle < 0.55f && in.brake == 0);
    in = m.update(pad_with(PadButton::Square, 0), dt);
    CHECK(in.brake == 1.0f && in.throttle == 0);
    // Shift requests (R2 / L2, GT3's buttons) are edge-triggered.
    m.reset();
    CHECK(m.update(pad_with(PadButton::R2, 255), dt).shift_up);
    CHECK(!m.update(pad_with(PadButton::R2, 255), dt).shift_up);
    CHECK(!m.update(PadState{}, dt).shift_up);
    CHECK(m.update(pad_with(PadButton::L2, 255), dt).shift_down);
    CHECK(!m.update(pad_with(PadButton::R1, 255), dt).shift_up); // R1 is the camera
    // D-pad: the digital pad (kind 3) asks for full lock; the physics turns the wheel in at
    // the digital rate (physics §4.4). Released: centre.
    m.reset();
    float s = m.update(pad_with(PadButton::Right, 255), dt).steer;
    CHECK(s == 1.0f && m.kind() == 3);
    s = m.update(PadState{}, dt).steer;
    CHECK(s == 0);
    // The stick (analog pad, kind 2) wins when deflected.
    PadState st;
    st.axes[size_t(PadAxis::LX)] = 0;
    CHECK(near(m.update(st, dt).steer, -1.0f));
    // Handbrake on Circle; automatic flag passes through.
    in = m.update(pad_with(PadButton::Circle, 255), dt);
    CHECK(in.handbrake && in.automatic);
    m.config().automatic = false;
    CHECK(!m.update(PadState{}, dt).automatic);
}

// GT3's per-puff cull: centre depth inside (near, far) and under two screen widths across.
TEST(race_tyre_smoke_cull) {
    TyreSmoke::View v;
    CHECK(TyreSmoke::visible(v, -5.0f, 100.0f)); // the default view culls nothing
    v.forward = {0, 0, -1};
    v.proj_x = 1.0f / std::tan(0.5236f); // 60 degrees across: proj_x 1.732
    v.near_m = 0.1f, v.far_m = 750.0f;
    CHECK(TyreSmoke::visible(v, 10.0f, 1.0f));
    CHECK(!TyreSmoke::visible(v, 0.05f, 0.01f)); // centre in front of the near plane
    CHECK(!TyreSmoke::visible(v, -1.0f, 0.5f));  // behind the camera
    CHECK(!TyreSmoke::visible(v, 800.0f, 1.0f)); // beyond the far plane
    CHECK(TyreSmoke::visible(v, 3.0f, 3.4f));    // 1.732 x 3.4 = 5.9 < 6
    CHECK(!TyreSmoke::visible(v, 3.0f, 3.5f));   // 1.732 x 3.5 = 6.06: over two screen widths
}

TEST(race_fixed_step) {
    FixedStep f(60.0);
    CHECK(f.advance(1.0 / 30.0) == 2);
    CHECK(f.alpha() < 1e-3f);
    CHECK(f.advance(1.0 / 120.0) == 0);
    CHECK(near(f.alpha(), 0.5f, 1e-3f));
    CHECK(f.advance(1.0 / 120.0) == 1);
    // A long stall is clamped instead of spiralling.
    CHECK(f.advance(2.0, 8) == 8);
    CHECK(f.alpha() >= 0 && f.alpha() < 1);
    // 1000 frames at 144 Hz give 60 ticks per second in total.
    FixedStep g(60.0);
    int ticks = 0;
    for (int i = 0; i < 1440; ++i) ticks += g.advance(1.0 / 144.0);
    CHECK(ticks == 599 || ticks == 600);
}

// Online races pace ticks by the shared clock: what is drawn (the tick run last, blended
// back by 1 - alpha) moves on with that clock at every frame rate and wherever its ticks fall
// between frames. (Blended by the frame times, at 120 fps half the start phases drew every
// other frame half a tick back: the whole view jittered.)
TEST(race_fixed_step_follow) {
    for (double hz : {60.0, 120.0, 144.0, 165.0})
        for (double phase = 0; phase < 1.0; phase += 0.0625) {
            FixedStep f(60.0);
            long long done = 0;
            double drawn_prev = -1, worst = 0;
            int ticks = 0;
            for (int frame = 0; frame < int(hz) * 2; ++frame) {
                const double clock = (frame / hz) * 60.0 + phase; // ticks due by this frame
                ticks += f.follow(clock, done, 30);
                const double drawn = double(done) - 1 + f.alpha();
                if (drawn_prev >= 0) {
                    CHECK(drawn >= drawn_prev);
                    worst = std::max(worst, std::fabs((drawn - drawn_prev) - 60.0 / hz));
                }
                drawn_prev = drawn;
            }
            CHECK(worst < 1e-4); // evenly spaced: every frame moves 60 / hz ticks on
            CHECK(ticks == int(std::floor((int(hz) * 2 - 1) / hz * 60.0 + phase)));
        }
    // A stall past the cap: the newest state, the rest next frame.
    FixedStep g(60.0);
    long long done = 0;
    CHECK(g.follow(40.5, done, 30) == 30 && done == 30 && g.alpha() == 1.0f);
    CHECK(g.follow(40.6, done, 30) == 10 && done == 40 && near(g.alpha(), 0.6f, 1e-4f));
    // The clock set back (a refined start time): nothing runs and the last tick stays drawn.
    CHECK(g.follow(39.8, done, 30) == 0 && done == 40 && g.alpha() == 0.0f);
    CHECK(g.follow(40.25, done, 30) == 0 && near(g.alpha(), 0.25f, 1e-4f));
}

TEST(race_interpolation) {
    sim::Quat a = quat_from_yaw(0), b = quat_from_yaw(1.0f);
    sim::Quat m = slerp(a, b, 0.5f);
    sim::Quat e = quat_from_yaw(0.5f);
    CHECK(near(m.w, e.w) && near(m.y, e.y));
    CHECK(near(slerp(a, b, 0).w, a.w) && near(slerp(a, b, 1).y, b.y));
    // Shortest arc even when the quaternions have opposite signs.
    sim::Quat nb{-b.w, -b.x, -b.y, -b.z};
    sim::Quat m2 = slerp(a, nb, 0.5f);
    CHECK(near(std::fabs(m2.w), e.w) && near(std::fabs(m2.y), std::fabs(e.y)));
    // Yaw +90 degrees turns the nose (-Z) towards -X.
    sim::Vec3 f = rotate(quat_from_yaw(1.5707963f), {0, 0, -1});
    CHECK(near(f.x, -1.0f) && near(f.z, 0.0f));

    sim::VehicleState s0, s1;
    s0.position = {0, 0, 0};
    s1.position = {10, 2, -4};
    s0.engine_rpm = 1000, s1.engine_rpm = 3000;
    s0.wheels[0].spin_angle = 0, s1.wheels[0].spin_angle = 2;
    s1.gear = 3;
    sim::VehicleState r = interpolate(s0, s1, 0.25f);
    CHECK(near(r.position.x, 2.5f) && near(r.position.y, 0.5f) && near(r.position.z, -1.0f));
    CHECK(near(r.engine_rpm, 1500) && near(r.wheels[0].spin_angle, 0.5f) && r.gear == 3);
}

// Frames at 120 Hz over 60 Hz ticks: the drawn car moves the same distance every frame, and a wheel turns by its speed every
// frame although its spin angle wraps (the exact core's wheel spin, keeps it within a turn).
TEST(race_interpolation_120hz) {
    namespace ex = gt3r::sim::exact;
    ex::Car c{};
    c.dt = ex::F(1.0f / 60.0f);
    c.p.max_lock = ex::F(0.5f);
    c.p.inv_tire_radius = {ex::F(1.0f / 0.3f), ex::F(1.0f / 0.3f)};
    for (ex::Wheel& w : c.w) w.rim_speed = ex::F(40.0f); // 133 rad/s: 2.2 rad a tick
    const sim::VehicleSetup setup;
    sim::VehicleState prev, state;
    exact_state(c, setup, state);
    FixedStep step(60.0);
    int ticks = 0, wraps = 0, frames = 0, bad_spin = 0, bad_move = 0;
    float last_spin = 0, last_x = 0;
    for (int f = 0; f < 600; ++f) {
        for (int n = step.advance(1.0 / 120.0); n > 0; --n) {
            prev = state;
            ex::save_previous(c);
            ex::wheel_spin(c);
            exact_state(c, setup, state);
            state.position.x = 40.0f * float(++ticks) / 60.0f;
            wraps += state.wheels[0].spin_angle < prev.wheels[0].spin_angle;
        }
        const sim::VehicleState r = interpolate(prev, state, step.alpha());
        if (frames++ > 1) { // (from the first tick on)
            const float turn = std::remainder(r.wheels[0].spin_angle - last_spin - 133.33333f / 120.0f, 6.2831853f);
            bad_spin += std::fabs(turn) > 1e-3f;
            bad_move += !near(r.position.x - last_x, 40.0f / 120.0f, 1e-3f);
        }
        last_spin = r.wheels[0].spin_angle, last_x = r.position.x;
    }
    CHECK(wraps > 50);
    CHECK(bad_spin == 0);
    CHECK(bad_move == 0);
    // Without the tick's turn (or between states that are not a tick apart) the wheel takes the shorter way round.
    sim::WheelState a, b;
    a.spin_angle = 6.2f, b.spin_angle = 0.1f;
    CHECK(near(spin_change(a, b), 0.1f + 6.2831853f - 6.2f, 1e-4f));
    b.spin_step = 4.0f, a.spin_angle = 1.0f, b.spin_angle = 5.0f - 6.2831853f; // over half a turn a tick
    CHECK(near(spin_change(a, b), 4.0f, 1e-4f));
    b.spin_angle = 1.2f; // not 4 rad on: a blend of two poses
    CHECK(near(spin_change(a, b), 0.2f, 1e-4f));
}

TEST(race_rumble) {
    RumbleModel rm;
    sim::VehicleState s;
    s.speed_kmh = 100;
    sim::ControlInputs in;
    sim::VehicleTelemetry t;
    RumbleModel::Events ev;
    t.pad_output = true;
    const double dt = 1.0 / 60.0;
    rm.update(s, &t, ev, in, dt);
    CHECK(rm.rumble(true).large == 0 && !rm.rumble(true).small);
    CHECK(rm.rumble(false).large == 0 && !rm.rumble(false).small);
    // GT3 original: the step's level goes to the large motor as it is (0..127); the small motor never runs.
    // Modern ignores it: nothing happened to the car.
    t.pad_rumble = 100;
    rm.update(s, &t, ev, in, dt);
    CHECK(rm.rumble(true).large == 100 && !rm.rumble(true).small);
    CHECK(rm.rumble(false).large == 0 && !rm.rumble(false).small);
    t.pad_rumble = 127;
    rm.update(s, &t, ev, in, dt);
    CHECK(rm.rumble(true).large == 127);
    // Modern: a hit pulses the large motor by its speed change for 0.25 s; below 1.5 m/s it is ignored.
    ev.impact = 1.0f;
    rm.update(s, &t, ev, in, dt);
    CHECK(rm.rumble(false).large == 0);
    ev.impact = 10;
    rm.update(s, &t, ev, in, dt);
    CHECK(rm.rumble(false).large == 255 && rm.rumble(true).large == 127);
    ev.impact = 0;
    for (int i = 0; i < 30; ++i) rm.update(s, &t, ev, in, dt);
    CHECK(rm.rumble(false).large == 0); // the pulse ends
    // Curbs pulse the small motor; off-road holds the large motor gently.
    ev.kerb = true;
    bool on = false, off = false;
    for (int i = 0; i < 30; ++i) {
        rm.update(s, &t, ev, in, dt);
        (rm.rumble(false).small ? on : off) = true;
    }
    CHECK(on && off && !rm.rumble(true).small);
    ev.kerb = false, ev.offroad = true;
    rm.update(s, &t, ev, in, dt);
    CHECK(rm.rumble(false).large > 40 && rm.rumble(false).large < 110);
    ev.offroad = false;
    // A slide: the tyre squeal's slip intensity past 0.35 grows the large motor, at most 0.35 of full; a wheel in the
    // air does not count.
    for (sim::WheelState& w : s.wheels) w.grounded = true;
    t.smoke = {0.3f, 0.3f, 0.3f, 0.3f};
    rm.update(s, &t, ev, in, dt);
    CHECK(rm.rumble(false).large == 0);
    t.smoke[2] = 0.8f;
    rm.update(s, &t, ev, in, dt);
    const u8 sliding = rm.rumble(false).large;
    CHECK(sliding > 30 && sliding < 50);
    t.smoke[2] = 2.0f;
    rm.update(s, &t, ev, in, dt);
    CHECK(rm.rumble(false).large == u8(0.35f * 255.0f));
    s.wheels[2].grounded = false;
    rm.update(s, &t, ev, in, dt);
    CHECK(rm.rumble(false).large == 0);
    s.wheels[2].grounded = true;
    t.smoke = {};
    // A car the AI drives sends nothing either way: the motors stop. So does a step without telemetry.
    ev.impact = 10, ev.kerb = true;
    t.pad_output = false;
    rm.update(s, &t, ev, in, dt);
    CHECK(rm.rumble(true).large == 0 && rm.rumble(false).large == 0 && !rm.rumble(false).small);
    t.pad_output = true;
    rm.update(s, &t, ev, in, dt);
    CHECK(rm.rumble(true).large == 127 && rm.rumble(false).large == 255);
    rm.update(s, nullptr, ev, in, dt);
    CHECK(rm.rumble(true).large == 0 && rm.rumble(false).large == 0);
    rm.reset();
    ev = {};
    // Wheelspin under throttle drives the right trigger (and Modern's large motor).
    in.throttle = 1;
    for (sim::WheelState& w : s.wheels) w.slip_ratio = -0.6f; // wheelspin
    rm.update(s, &t, ev, in, dt);
    CHECK(rm.trigger_right() > 0.3f);
    CHECK(rm.trigger_left() == 0);
    CHECK(rm.rumble(false).large > 40);
    // Locked wheels under braking (positive slip) chatter on the left trigger.
    in.throttle = 0, in.brake = 1;
    for (sim::WheelState& w : s.wheels) w.slip_ratio = 0.7f;
    float peak = 0;
    for (int i = 0; i < 8; ++i) rm.update(s, &t, ev, in, dt), peak = std::max(peak, rm.trigger_left());
    CHECK(peak > 0.4f && rm.trigger_right() == 0);
    rm.reset();
    CHECK(rm.trigger_right() == 0 && rm.rumble(true).large == 0 && rm.rumble(false).large == 0);
}

// Render state decoded from course node programs: alpha-only frame buffer
// passes (shadow masks) and dest-alpha additive passes (road sheen).
TEST(race_course_render_state) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    for (const char* name : {"courses/US/seattle", "courses/US/trialmt"}) {
        Bytes data = disc->volume().read(*disc->volume().find(name));
        course::CourseFile f = course::CourseFile::parse(data);
        ByteSpan s = f.section(data, course::kSlotScenery);
        gtm1::Gtm1 g = gtm1::Gtm1::parse(s);
        int alpha_only = 0, sheen = 0, normal = 0;
        for (u32 n = 0; n < g.nodes.size(); ++n)
            for (const gtm1::Draw& d : g.run(n)) {
                if ((d.fb_mask & 0x00FFFFFFu) == 0x00FFFFFFu) ++alpha_only;
                else if (d.alpha_mode == 0x58) ++sheen;
                else if (d.alpha_mode == 0x44) ++normal;
            }
        CHECK(sheen > 0 && normal > 0);
        if (std::string(name).find("seattle") != std::string::npos) CHECK(alpha_only > 0);

        course::CourseModelOptions o;
        o.reflections = true;
        scene::Model m = course::build_course_model(data, o, name);
        size_t sheen_prims = 0, masked = 0, total = 0;
        for (const scene::Mesh& mesh : m.meshes)
            for (const scene::Primitive& p : mesh.primitives) {
                if (!((p.flags & scene::kAdditive) && (p.flags & scene::kDestAlphaAdd))) continue;
                // The sheen (chunk kind 4): lit by its specular-only program, not unlit.
                if (p.material.specular_local) CHECK(p.material.valid && !(p.flags & scene::kUnlit));
                ++sheen_prims;
                for (u32 i = p.first_index; i < p.first_index + p.index_count; ++i) {
                    ++total;
                    if ((mesh.vertices[mesh.indices[i]].color >> 24) != 0xFF) ++masked;
                }
            }
        CHECK(sheen_prims > 0);
        if (std::string(name).find("trialmt") != std::string::npos) CHECK(masked > 0 && masked < total);
    }
}

TEST(race_course_opaque_packets_whole) {
    // Opaque packets whose program turns the GS alpha test off draw every texel, whatever their
    // texel alpha: Route 11's perforated wall panels (set 3 #13: alpha only on the frame, the
    // weight of their orange kind-2 pass) and Laguna's wall banners (alpha 0x40 throughout).
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    for (const auto& [name, tex] : {std::pair<const char*, const char*>{"courses/US/route11", "set3_13"},
                                    {"courses/US/laguna", "set4_7"}}) {
        Bytes data = disc->volume().read(*disc->volume().find(name));
        course::CourseModelOptions o;
        o.reflections = true, o.frame_alpha = true;
        scene::Model m = course::build_course_model(data, o, name);
        size_t prims = 0, cut = 0;
        for (const scene::Mesh& mesh : m.meshes)
            for (const scene::Primitive& p : mesh.primitives) {
                if (p.texture < 0 || m.textures[size_t(p.texture)].name != tex) continue;
                if (p.flags & (scene::kAlphaBlend | scene::kAdditive | scene::kEnvironmentMap)) continue;
                ++prims;
                if (p.flags & scene::kAlphaTest) ++cut;
            }
        CHECK(prims > 0);
        CHECK(cut == 0);
    }
}

TEST(race_car_setup) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    CHECK(db.load(vfs));
    CarSetup c = car_setup_from_database(db, "r34_gtr_v_99");
    CHECK(c.model == "ni0137");
    CHECK(c.ratios.size() == 6);
    CHECK(c.limit_rpm > 6000 && c.limit_rpm < 10000);
    CHECK(c.wheels.wheelbase > 2.5f && c.wheels.wheelbase < 2.8f);
    CHECK(c.sound_id != 0);
}

// --- replays, ghost, replay cameras, ambience ---------------------------------

namespace {

sim::ControlInputs test_inputs(int i) {
    sim::ControlInputs in;
    in.throttle = float((i * 37) % 100) / 100.0f;
    in.brake = (i / 90) % 4 == 3 ? 0.6f : 0;
    in.steer = std::sin(float(i) * 0.013f);
    in.shift_up = i % 200 == 0;
    in.automatic = (i / 400) % 2 == 0;
    return in;
}

} // namespace

TEST(race_replay_roundtrip) {
    Replay r;
    r.course = "trialmt";
    r.car = "r34_gtr_v_99";
    r.automatic = false;
    r.paint = 3;
    r.start = {{1, 2, 3}, quat_from_yaw(0.5f)};
    for (int i = 0; i < 500; ++i) r.inputs.push_back(quantize(test_inputs(i)));
    r.exact = test::race_record_stub();
    r.best_lap = 61.25;
    r.best_lap_begin = 10, r.best_lap_end = 400;
    r.final_position = {7, 8, 9};
    r.event = "AEAT0001";
    r.opponents = {"a_car", "b_car"};
    r.car2 = "s2000_99";
    r.start_speed = 22.25f;
    for (int i = 0; i < 500; ++i) r.inputs2.push_back(quantize(test_inputs(i + 77)));
    Replay q = Replay::parse(r.serialize());
    CHECK(q.start_speed == 22.25f);
    CHECK(q.car2 == "s2000_99" && q.inputs2.size() == 500 && q.inputs2[10].steer == r.inputs2[10].steer);
    CHECK(q.event == "AEAT0001" && q.opponents.size() == 2 && q.opponents[1] == "b_car");
    CHECK(q.course == r.course && q.car == r.car && !q.automatic && q.paint == 3);
    CHECK(q.ticks() == 500 && q.tick_hz == r.tick_hz && q.exact != nullptr);
    CHECK(q.best_lap == 61.25 && q.best_lap_begin == 10 && q.best_lap_end == 400);
    CHECK(q.final_position.z == 9 && q.start.orientation.y == r.start.orientation.y);
    bool same = true;
    for (size_t i = 0; i < r.inputs.size(); ++i) {
        const sim::ControlInputs &a = r.inputs[i], &b = q.inputs[i];
        same = same && a.steer == b.steer && a.throttle == b.throttle && a.brake == b.brake &&
               a.shift_up == b.shift_up && a.shift_down == b.shift_down && a.handbrake == b.handbrake &&
               a.automatic == b.automatic;
    }
    CHECK(same); // quantised inputs survive the file exactly
    // quantize() is idempotent.
    sim::ControlInputs x = quantize(test_inputs(7));
    sim::ControlInputs y = quantize(x);
    CHECK(x.steer == y.steer && x.throttle == y.throttle && x.brake == y.brake);
    bool threw = false;
    try {
        Bytes b = r.serialize();
        b.resize(b.size() - 3);
        Replay::parse(b);
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

// The original's ghost rules (presentation §G11): a run's (a free-run lap's) recording is kept
// under 36,000 ticks; the best run replaces the ghost after a run with a time strictly under the
// ghost's (whole ms), or when there is no ghost yet; a FAIL / EXIT (no time) or a recording not
// kept never does.
TEST(race_licence_ghost_rules) {
    CHECK(ghost_recording_kept(0) && ghost_recording_kept(35999) && !ghost_recording_kept(36000));
    CHECK(replaces_ghost(35100, true, -1));             // the first finished run
    CHECK(replaces_ghost(35099, true, 35100));          // faster than the ghost
    CHECK(!replaces_ghost(35100, true, 35100));         // a tie keeps the ghost
    CHECK(!replaces_ghost(35101, true, 35100));         // slower
    CHECK(!replaces_ghost(-1, true, -1));               // no time (FAIL, EXIT)
    CHECK(!replaces_ghost(35000, false, -1));           // a run of 10 minutes or more
    // Free runs: a lap becomes the best lap only strictly under the best so far.
    CHECK(takes_best_lap(90500, true, -1) && takes_best_lap(90499, true, 90500));
    CHECK(!takes_best_lap(90500, true, 90500) && !takes_best_lap(90600, true, 90500));
    CHECK(!takes_best_lap(80000, false, 90500) && !takes_best_lap(-1, true, -1));
}

TEST(race_reverb_ambience) {
    CHECK(ReverbAmbience::target_for(0) == 0.125f);
    CHECK(ReverbAmbience::target_for(0x08) == 0.5f);
    CHECK(ReverbAmbience::target_for(0x10) == 0.25f && ReverbAmbience::target_for(0x18) == 0.25f);
    CHECK(ReverbAmbience::target_for(0x01 | 0x02) == 0.125f);
    VoiceMixer mixer;
    ReverbAmbience a;
    a.tick(mixer, 0x08);
    CHECK(near(a.depth(), 0.125f + 1.0f / 36.0f));
    for (int i = 0; i < 100; ++i) a.tick(mixer, 0x08);
    CHECK(near(a.depth(), 0.5f));
    CHECK(mixer.reverb_depth() == s16(std::lround(0.5f * 32767.0f)));
    for (int i = 0; i < 100; ++i) a.tick(mixer, 0);
    CHECK(near(a.depth(), 0.125f));
}

TEST(race_replay_cameras) {
    GtrcCurve c;
    c.keys = {{0.0f, 10.0f}, {0.5f, 20.0f}, {1.0f, 0.0f}};
    CHECK(near(c.eval(0.25f), 15) && near(c.eval(0.75f), 10) && near(c.eval(1.5f), -20) && near(c.eval(-1), -10));
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Bytes data = disc->volume().read(*disc->volume().find("courses/US/trialmt"));
    course::CourseFile f = course::CourseFile::parse(data);
    GtrcScript g = GtrcScript::parse(f.section(data, course::kSlotReplayCameras));
    course::Road road = course::Road::parse(f.section(data, course::kSlotRoad));
    CHECK(g.records.size() == 66 && g.variants() == 3);
    CHECK(g.lap_length == road.length);
    // Every trackside shot over a lap puts the camera within 160 m of the car (a centre-line point).
    ReplayDirector d(&g);
    int shots = 0, near_ok = 0;
    std::unique_ptr<track::Track> t = track::Track::from_course_file(data, "trialmt");
    for (float s = 0; s < road.length; s += 20) {
        sim::Vec3 p = t->point_at(s);
        render::Vec3 car{p.x, p.y, p.z};
        ReplayDirector::Shot shot = d.update(s, 1, car, 1.0 / 60, 16.0f / 9.0f); // (lap 1)
        if (!shot.valid || shot.onboard) continue;
        ++shots;
        if (render::length(shot.eye - car) < 160) ++near_ok;
    }
    std::printf("replay cameras: %d shots, %d within 160 m\n", shots, near_ok);
    CHECK(shots > 150 && near_ok * 20 >= shots * 19);
}

// The arcade time-trial demonstrations' first shots (presentation §H8): a flying start before
// the line has lap variant 0 (D = s), so the script's start records take the car once it is
// past their start; further back the camera turns on board (the in-car view), then another lap
// variant's record at u <= 0.6, or none.
TEST(race_replay_demo_openings_disc) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    struct Opening {
        const char* course;
        float before_line; // the demo's start, metres before the line
        int first;         // its first shot's record (-1: on board)
        int second;        // the next frame's (-1: on board)
    };
    const Opening openings[] = {
        {"laguna", 149, 0, 0},   // AT05
        {"midfield", 199, 5, 5}, // AT01 (its start records reach 1217 m back)
        {"akasaka", 199, -1, 25}, // AT03: variant 1's record at the lap's end
        {"trialmt", 129, -1, 21}, // AT02
        {"deepforest", 199, -1, -1}, // AT04: no variant's record at u <= 0.6
    };
    for (const Opening& o : openings) {
        Bytes data = disc->volume().read(*disc->volume().find(std::string("courses/US/") + o.course));
        course::CourseFile f = course::CourseFile::parse(data);
        GtrcScript g = GtrcScript::parse(f.section(data, course::kSlotReplayCameras));
        ReplayDirector d(&g);
        const float s = g.lap_length - o.before_line;
        ReplayDirector::Shot a = d.update(s, 0, {0, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
        const int first = d.on_board() ? -1 : a.record;
        ReplayDirector::Shot b = d.update(s + 0.5f, 0, {0, 0, 0}, 1.0 / 60, 16.0f / 9.0f);
        const int second = d.on_board() ? -1 : b.record;
        std::printf("demo opening %s -%.0f m: %d, %d\n", o.course, o.before_line, first, second);
        CHECK(first == o.first && second == o.second);
    }
}

TEST(race_audio_models) {
    CHECK(OpponentEngines::attenuation(1) == 1.0f && OpponentEngines::attenuation(32) == 1.0f);
    CHECK(near(OpponentEngines::attenuation(64), 0.5f) && near(OpponentEngines::attenuation(128), 0.25f));
    // A source approaching the listener sounds higher, receding lower; still air = 1.
    sim::Vec3 dir{0, 0, 1}; // listener -> source
    CHECK(near(OpponentEngines::doppler(dir, {0, 0, 0}, {0, 0, 0}), 1.0f));
    CHECK(OpponentEngines::doppler(dir, {0, 0, -30}, {0, 0, 0}) > 1.05f);
    CHECK(OpponentEngines::doppler(dir, {0, 0, 30}, {0, 0, 0}) < 0.95f);
    // Tachometer scale as the game picks it.
    CHECK(Hud::tach_scale(8000) == 9000 && Hud::tach_scale(7600) == 9000 && Hud::tach_scale(7400) == 8000);
}

TEST(race_paint_assignment) {
    PaintRng a, b;
    CHECK(a.next() == b.next());
    for (int i = 0; i < 100; ++i) {
        float f = a.rand01();
        CHECK(f >= 0 && f < 1);
    }
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    CarColors colors;
    CHECK(colors.load(vfs));
    const std::vector<u32>* r34 = colors.ids("ni0137");
    CHECK(r34 && !r34->empty());
    // Six cars of the same model: the player fixed on colour 0, five drawn.
    std::vector<PaintEntry> e(6, PaintEntry{"ni0137"});
    e[0].id = (*r34)[0];
    PaintRng rng;
    std::vector<int> p = assign_paints(colors, e, rng);
    CHECK(p.size() == 6 && p[0] == 0);
    int same_as_player = 0;
    for (int i = 1; i < 6; ++i) {
        CHECK(p[size_t(i)] >= 0 && size_t(p[size_t(i)]) < r34->size());
        same_as_player += p[size_t(i)] == 0;
    }
    CHECK(same_as_player <= 1); // the draw avoids the player's colour
    PaintRng rng2;
    CHECK(assign_paints(colors, e, rng2) == p); // deterministic
}

// Licence tests drive the event's entry 0 in the event's slot-0 colour (+0x110): every set id
// is one of the car model's carcolor.db colours, and several are not the model's first one.
TEST(race_license_car_paint_disc) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    CHECK(db.load(vfs));
    CarColors colors;
    CHECK(colors.load(vfs));
    int tests = 0, set = 0, not_first = 0;
    for (const data::Event& ev : db.events()) {
        if (ev.label.size() != 7 || ev.label[0] != 'L' || ev.entries.empty() || ev.bytes.size() < 0x114) continue;
        ++tests;
        const data::CarVariant* v = db.variant(ev.entries[0]);
        const data::Car* car = v ? db.car(v->car) : db.car(ev.entries[0]);
        CHECK(car != nullptr);
        if (!car) continue;
        u32 id = rd32(ev.bytes, 0x110);
        if (id == 0xFFFFFFFFu) continue;
        ++set;
        int k = colors.index_of(db.model_of(*car), id);
        CHECK(k >= 0);
        not_first += k > 0;
    }
    CHECK(tests == 48);
    CHECK(set >= 30);
    CHECK(not_first > 0);
}

// Which licence tests show the course line (course.md "Course line"): the line belongs to the course
// file the event names, so it is on exactly when that file has one. B-3 .. B-8 and A-1, A-2,
// A-3, A-6, A-7, A-8; none of the B-1, B-2, A-4, A-5 or IB / IA / S / rally tests.
TEST(race_license_course_line_disc) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    CHECK(db.load(vfs));
    std::vector<std::string> with_line;
    int tests = 0;
    for (const data::Event& ev : db.events()) {
        if (ev.label.size() != 7 || ev.label[0] != 'L') continue;
        ++tests;
        const Volume::Entry* e = disc->volume().find("courses/US/" + ev.course);
        CHECK(e != nullptr);
        if (!e) continue;
        Bytes data = disc->volume().read(*e);
        if (!course::course_line_model(data).meshes.empty()) with_line.push_back(ev.label);
    }
    std::sort(with_line.begin(), with_line.end());
    const std::vector<std::string> expected = {"L0A0001", "L0A0002", "L0A0003", "L0A0006", "L0A0007", "L0A0008",
                                               "L0B0003", "L0B0004", "L0B0005", "L0B0006", "L0B0007", "L0B0008"};
    CHECK(tests == 48);
    CHECK(with_line == expected);
}

TEST(race_tyre_and_collision_sounds) {
    // The road roar of a rolling wheel (the slip intensity and skid pitch are the car's own: its record's, physics §8.3, §8.3c).
    CHECK(road_noise(0.25f, 50).vol > 0 && road_noise(0.25f, 50).vol <= 0.3f);
    // Collisions: below 0.2 strength nothing.
    CHECK(wall_strength(5000) == 0 && near(wall_strength(25000), 0.5f) && wall_strength(1e6f) == 1.0f);
    CHECK(car_strength(1000) == 0 && near(car_strength(3750), 0.5f));
    CHECK(sfx_gain(SoundView::InCarTarget, kChEngine) == 3.0f && sfx_gain(SoundView::ChaseOther, kChClash) == 0.0f);
    // Clash volume: gain x s x 0.6 x 32/d, capped at 1. In the cockpit (d ~ 1 m) any audible hit is
    // full; in the chase view (gain 0.5) it falls with the camera distance.
    CHECK(clash_volume(SoundView::InCarTarget, 0.2f, 1.0f) == 1.0f);
    CHECK(near(clash_volume(SoundView::ChaseTarget, 0.25f, 8.0f), 0.5f * 0.25f * 0.6f * 4.0f));
    CHECK(near(clash_volume(SoundView::ChaseTarget, 0.5f, 0.1f), 1.0f));
    CHECK(clash_volume(SoundView::ChaseOther, 1.0f, 5.0f) == 0.0f);
}

TEST(race_results_text) {
    CHECK(result_time_text(true, true, 344.117, 344.117, 0) == " 5'44.117");
    CHECK(result_time_text(false, true, 344.55, 344.117, 0) == "+0'00.433");
    CHECK(result_time_text(false, true, 400, 344, 1) == "+1 lap");
    CHECK(result_time_text(false, true, 400, 344, 3) == "+3 laps");
    CHECK(result_time_text(false, false, 0, 344, 0) == "Running...");
}

// The race screens' car names keep the database's game text (escapes and JIS pairs) for the game
// font. INVENTED database (car_db_synth.h).
TEST(race_result_car_name_game_text) {
    auto db = test::cardb::empty_database();
    CHECK(db != nullptr);
    if (!db) return;
    test::cardb::add_car(*db, "syn_car", 300);
    test::cardb::add_arcade_car(*db, "ar_syn_car", "syn_car");
    const std::string name = "SYN V\xA1\xA6X", alt = "CAF\x81\xC9";
    const ByteSpan* rec = db->record(data::TableId::Car, "syn_car");
    CHECK(rec != nullptr);
    if (!rec) return;
    Bytes car(rec->begin(), rec->end());
    test::cardb::put16(car, 0xE4, db->add_unistring(name)); // the US names
    test::cardb::put16(car, 0xE6, db->add_unistring(alt));
    db->put_record(data::TableId::Car, car);
    db->rebuild();
    CHECK(result_car_name(*db, "syn_car") == name);
    CHECK(result_car_name(*db, "syn_car", true) == name + " " + alt);
    CHECK(result_car_name(*db, "ar_syn_car") == name); // an arcade car: its base car's name
    CHECK(result_car_name(*db, "no_such_car") == "no_such_car");
    CHECK(car_setup_from_database(*db, "syn_car").name == name); // the HUD's replay item, "Pitting in..."
}

// The R34 V-spec / M-spec names hold a JIS middle dot (0x2126): kept as game text, the game
// font draws it from the kanji font, as GT3 does.
TEST(race_result_car_name_disc) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    CHECK(db.load(vfs));
    const std::string dot = "\xA1\xA6";
    for (const char* label : {"r34_gtr_v_99", "r34_gtr_m_01", "r34_gtr_v2_00"}) {
        const std::string n = result_car_name(db, label);
        CHECK(n.find(dot + "spec") != std::string::npos); // V-spec, M-spec, V-spec II
        CHECK(car_setup_from_database(db, label).name == n);
    }
    // Laid out with sw_16 (the replay item, the results rows): the dot is the one kanji cell.
    const auto sys_data = vfs.read("font/system.fnt"), kf_data = vfs.read("font/system.kf");
    CHECK(sys_data && kf_data);
    if (!sys_data || !kf_data) return;
    const text::FontFile sys = text::FontFile::parse(*sys_data);
    const text::KanjiFont kf = text::KanjiFont::parse(*kf_data);
    const text::LatinFont* sw16 = nullptr;
    for (const text::LatinFont& f : sys.fonts)
        if (f.name == "sw_16") sw16 = &f;
    CHECK(sw16 != nullptr);
    if (!sw16) return;
    const std::string n = result_car_name(db, "r34_gtr_v_99");
    const ByteSpan bytes(reinterpret_cast<const u8*>(n.data()), n.size());
    const text::TextLayout lay = text::layout_text(text::map_game_text(bytes), *sw16, &kf, 0, 0, {});
    std::vector<u16> kanji;
    for (const text::GlyphQuad& q : lay.quads)
        if (q.kanji) kanji.push_back(q.code);
    CHECK(kanji == std::vector<u16>{0x2126});
}

// The original's race order and finish rule (presentation.md "Race order and finish").
TEST(race_order_and_finish_rules) {
    auto st = [](int laps, double distance, double finish = -1) {
        RaceField::Standing s;
        s.laps = laps, s.distance = distance, s.time = finish, s.finished = finish >= 0;
        return s;
    };
    CHECK(ranks_ahead(st(3, 0), st(2, 9000)));                    // laps first
    CHECK(ranks_ahead(st(2, 9000, 300), st(1, 5000, 290)));       // a lapped car finishing earlier stays behind
    CHECK(ranks_ahead(st(2, 7000), st(2, 6000)));                 // running: race distance
    CHECK(ranks_ahead(st(2, 6000), st(2, 6500, 280)));            // a running car ahead of a finished one
    CHECK(!ranks_ahead(st(2, 6500, 280), st(2, 6000)));
    CHECK(ranks_ahead(st(2, 6000, 270), st(2, 6500, 280)));       // finished: finish time
    CHECK(!ranks_ahead(st(2, 6000), st(2, 6000)));                // ties keep their places
}

// A race field on Trial Mountain: opponents from the event, player last on the
// grid, everyone held during the countdown, standings ordered by distance.
TEST(race_field_event) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    CHECK(db.load(vfs));
    Bytes course = disc->volume().read(*disc->volume().find("courses/US/trialmt"));
    std::unique_ptr<RaceTrack> track = make_track(vfs, course, "trialmt");
    CHECK(track->runtime() != nullptr);
    FieldConfig cfg;
    cfg.event = "AEAT0001";
    cfg.exact.data = program();
    cfg.exact.course_file = course;
    cfg.exact.course = "trialmt";
    cfg.exact.laps = 2;
    cfg.exact.player.car = {"r34_gtr_v_99"};
    std::string err;
    std::unique_ptr<RaceField> f = RaceField::create(*track, db, cfg, &err);
    CHECK(f != nullptr);
    if (!f) return;
    CHECK(f->opponents() == 5 && f->race_time() < 0);
    RaceVehicle& p = f->player();
    sim::Vec3 start = p.state().position;
    CHECK((start - f->player_grid().position).length() < 1e-4f);
    sim::ControlInputs gas;
    gas.throttle = 1;
    for (int i = 0; i < 540; ++i) p.step(gas, 1.0 / 60); // the start's 10 s hold: held
    CHECK(f->race_time() < 0 && (p.state().position - start).length() < 1.0f);
    for (int i = 0; i < 600; ++i) p.step(gas, 1.0 / 60);
    CHECK(f->race_time() > 8.5 && (p.state().position - start).length() > 50.0f);
    std::vector<RaceField::Standing> st = f->standings();
    CHECK(st.size() == 6 && !st[0].finished);
}

TEST(race_lap_logger_and_records) {
    LapLogger log;
    for (int lap = 0; lap < 3; ++lap) {
        log.begin_lap();
        for (int i = 0; i < 10; ++i) log.sample(float(i) * 10, 100.0f + float(lap), 1, 0);
        log.finish_lap(lap == 1 ? 50.0 : 60.0 + lap);
    }
    CHECK(log.last().time == 62.0 && log.last().size() == 10);
    CHECK(log.best().time == 50.0 && log.best().speed_kmh[0] == 101.0f);
    CHECK(record_date("trialmt_r34_gtr_v_99_20261011-094509.gt3replay") == "2026/10/11");
    CHECK(record_date("odd.gt3replay").empty());
}

TEST(race_event_start_fields) {
    // Synthetic event records: start type at +0x1F3, countdown +0x1F2, initial speed +0x198,
    // time limit +0x1E4 (minutes), flying distance +0x1E6 (s16, negative).
    Bytes b(0x200, 0);
    data::Event ev;
    ev.laps = 0;
    ev.cars_in_race = 6;
    b[0x1F3] = 3;
    b[0x1E4] = 120;
    ev.bytes = ByteSpan(b.data(), b.size());
    EventStart e = event_start(ev);
    CHECK(e.rolling() && !e.flying() && e.countdown == 0 && e.time_limit_s == 7200 && e.cars == 6);
    b[0x198] = 80;
    b[0x1E6] = u8(-120 & 0xFF);
    b[0x1E7] = u8((-120 >> 8) & 0xFF);
    e = event_start(ev);
    CHECK(e.flying() && e.initial_kmh == 80 && e.flying_m == 120);
    b[0x1F3] = 2;
    CHECK(event_start(ev).countdown == 10);
    b[0x1F3] = 1;
    CHECK(event_start(ev).countdown == 4);
    b[0x1F3] = 0;
    b[0x1F2] = 7;
    CHECK(event_start(ev).countdown == 7);

    sim::CarSpec light, heavy;
    light.engine.curve_rpm = heavy.engine.curve_rpm = {2000, 6000, 8000};
    light.engine.curve_torque = heavy.engine.curve_torque = {300, 400, 300};
    light.mass = 1000, heavy.mass = 1500;
    CHECK(power_to_weight(light) < power_to_weight(heavy));
    CHECK(std::fabs(power_to_weight(light) - 1000.0f / (400.0f * 6000 * 0.10471976f / 735.49875f)) < 0.01f);
}

// A judged licence run as GT3's licence display shows it, from the race flow's result words: a medal with its time; slower
// than bronze (0 with the failed event) a finished run with no medal and its time (TIME EXPIRED with the time); out of the
// course (-1) failed with no time (FAIL); a test with no judging times (0, no failed event) finished with its best.
TEST(race_license_outcome) {
    LicenseOutcome o = license_outcome(3, 35321, false, -1);
    CHECK(o.state == LicenseRun::State::Finished && o.medal == 1 && o.time == 35.321);
    o = license_outcome(0, 41007, true, 40.0);
    CHECK(o.state == LicenseRun::State::Finished && o.medal == -1 && o.time == 41.007);
    o = license_outcome(-1, 0x157529FF, true, 40.0);
    CHECK(o.state == LicenseRun::State::Failed && o.medal == -1 && o.time < 0 && o.failure == "out of the course");
    o = license_outcome(0, 0, false, 40.5);
    CHECK(o.state == LicenseRun::State::Finished && o.medal == -1 && o.time == 40.5);
}

TEST(race_license_medals) {
    Bytes b(0x200, 0);
    auto put = [&](size_t o, int m, int s, int ms) { b[o] = u8(m), b[o + 1] = u8(s), b[o + 2] = u8(ms & 0xFF), b[o + 3] = u8(ms >> 8); };
    put(0x1C4, 0, 35, 100);
    put(0x1C8, 0, 35, 400);
    put(0x1CC, 0, 35, 800);
    b[0x19A] = 2;
    LicenseTimes t = license_times(ByteSpan(b.data(), b.size()));
    CHECK(t.valid() && t.kind == 2 && t.ms[0] == 35100 && t.ms[2] == 35800);
    CHECK(license_medal(t, 35.1) == 0 && license_medal(t, 35.1004) == 0 && license_medal(t, 35.1006) == 1);
    CHECK(license_medal(t, 35.8) == 2 && license_medal(t, 35.801) == -1 && license_medal(t, -1) == -1);
    Disc* disc = test_disc();
    if (!disc) return;
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    CHECK(db.load(vfs));
    const data::Event* ev = db.event("L0B0001");
    CHECK(ev != nullptr);
    if (ev) CHECK(license_times(*ev).ms[0] == 35100 && license_times(*ev).ms[1] == 35400 && license_times(*ev).ms[2] == 35800);
}

// Licence pylons (GT3's rigid bodies): B-1/B-2's row of 20 cones 1.9 m past the stop box,
// knocked about by a car's box; any touch is reported (the race's licence judge fails on it).
TEST(race_pylons_disc) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Bytes data = disc->volume().read(*disc->volume().find("courses/US/L_testcourse1"));
    Pylons p;
    CHECK(p.load(nullptr, ByteSpan(data.data(), data.size())));
    CHECK(p.records().size() == 40 && p.types().size() == 1);
    if (p.records().size() != 40 || p.types().empty()) return;
    CHECK(p.types()[0].hull.size() == 5 && std::fabs(p.types()[0].mass - 15) < 1e-3f);
    // Row 1: z = -590.7, x from -979.3 to -956.5 every 1.2 m (file order shuffled).
    std::vector<float> xs;
    for (int i = 0; i < 20; ++i) {
        CHECK(std::fabs(p.records()[size_t(i)].pos.z + 590.7f) < 0.02f);
        xs.push_back(p.records()[size_t(i)].pos.x);
    }
    std::sort(xs.begin(), xs.end());
    CHECK(std::fabs(xs.front() + 979.3f) < 0.01f && std::fabs(xs.back() + 956.5f) < 0.01f);
    for (size_t i = 1; i < xs.size(); ++i) CHECK(std::fabs(xs[i] - xs[i - 1] - 1.2f) < 0.01f);
    struct Flat : sim::TrackQuery {
        sim::SurfaceHit probe(float, float, float) const override {
            sim::SurfaceHit h;
            h.hit = true;
            return h;
        }
    } flat;
    p.reset(flat);
    // Asleep on the ground: the base (hull point 0) at y = 0, nothing moves on its own.
    std::vector<Pylons::Car> none;
    for (int i = 0; i < 60; ++i) p.step(flat, none, 1.0 / 60);
    CHECK(std::fabs(p.bodies()[0].pos.y - 0.19607f) < 0.01f && p.bodies()[0].vel.length() < 1e-6f);
    // A car (nose to +z, the course direction) stopping 0.5 m short of the row: no touch.
    Pylons::Car car;
    float yaw = 3.14159265f; // nose (-z in the car frame) turned to +z
    car.orientation = {std::cos(yaw / 2), 0, std::sin(yaw / 2), 0};
    car.front = 2.0f, car.rear = 2.0f, car.half_width = 0.85f;
    car.pos = {-972.5f, 0, -590.7f - 0.18f - 2.0f - 0.5f};
    CHECK(p.step(flat, {car}, 1.0 / 60) == 0);
    // Driving through at 20 m/s: touched within a few frames, the struck cones fly off.
    car.vel = {0, 0, 20};
    unsigned hit = 0;
    for (int i = 0; i < 30 && !hit; ++i) {
        car.pos.z += 20.0f / 60;
        hit = p.step(flat, {car}, 1.0 / 60);
    }
    CHECK(hit == 1);
    float fastest = 0;
    for (const Pylons::Body& b : p.bodies()) fastest = std::max(fastest, b.vel.length());
    CHECK(fastest > 15);
    // Struck cones meet a wall: a wall edge 3.7 m past the row whose drivable side
    // (s.z, -s.x) is -z: no body ends up behind it or closer than its radius.
    track::WallLine wall;
    wall.points = {{{-1050, 0, -587.0f}, 0}, {{-900, 0, -587.0f}, 0}};
    p.reset(flat);
    car.pos = {-972.5f, 0, -590.7f - 0.18f - 2.0f - 0.5f};
    for (int i = 0; i < 90; ++i) {
        car.pos.z += 20.0f / 60;
        p.step(flat, {car}, 1.0 / 60, std::span<const track::WallLine>(&wall, 1));
        car.pos.z = std::min(car.pos.z, -592.0f); // the car stops before the wall
    }
    float deepest = -1e9f;
    for (const Pylons::Body& b : p.bodies()) deepest = std::max(deepest, b.pos.z);
    CHECK(deepest <= -587.0f - p.types()[0].radius + 0.01f);
    // The planar shadow: every point lands on the body's ground plane (y = 0 here).
    render::Mat4 m;
    CHECK(Pylons::shadow_matrix(p.bodies()[0], {0.3f, 0.9f, 0.2f}, m));
    render::Vec3 tip = m.transform_point({0, 0.5f, 0});
    CHECK(std::fabs(tip.y) < 0.01f);
    CHECK(!Pylons::shadow_matrix(p.bodies()[0], {1, 0, 0}, m)); // grazing light: none
}

// The race display's time formatter: the font's dashes (0x7F) for "no time" (negative, or the
// 359,999,999 ms a FAIL records), one for the minutes; clamp at 999'59.999, no leading zero on
// the minutes.
TEST(race_hud_time_format) {
    CHECK(Hud::format_time(-1) == "\x7F'\x7F\x7F.\x7F\x7F\x7F" && Hud::format_time(-1) == kHudNoTimeText);
    CHECK(Hud::format_time(359999.999) == kHudNoTimeText);
    CHECK(Hud::format_time(35.1) == "0'35.100");
    CHECK(Hud::format_time(125.007) == "2'05.007");
    CHECK(Hud::format_time(100000.0) == "999'59.999");
}

TEST(race_hud_speed_lines) {
    // presentation §D4: the top-speed test's speed lines, "%.1f" (rounded) in km/h or mph,
    // at most 999.9, dashes for none.
    CHECK(Hud::speed_text(312.46f, false) == "312.5");
    CHECK(Hud::speed_text(200.0f, true) == "124.3");
    CHECK(Hud::speed_text(0.0f, false) == "\x7F\x7F\x7F.\x7F" && Hud::speed_text(-1.0f, true) == kNoSpeedText);
    CHECK(Hud::speed_text(1200.0f, false) == "999.9");
}

TEST(race_hud_layout_disc) {
    // presentation §D4: our HUD layout table is the disc's (database/racemode.db: a mode
    // number and 32 racedetail ids; racedetail.db: id, part, sub, then valign at +0x0C,
    // align at +0x12, view, x, y, w, h from +0x18), row for row, the empty rows left out.
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Volume& vol = disc->volume();
    Bytes mode_bytes = vol.read(*vol.find("database/racemode.db"));
    Bytes detail_bytes = vol.read(*vol.find("database/racedetail.db"));
    db::Table modes = db::Table::parse(mode_bytes), details = db::Table::parse(detail_bytes);
    std::map<u64, ByteSpan> by_id;
    for (size_t i = 0; i < details.size(); ++i) by_id[details.id_of(i)] = details.records[i];
    const u64 none = db::id_hash("(Null)");
    const HudMode ours[] = {HudMode::Race,         HudMode::Endurance, HudMode::License, HudMode::MachineTime,
                            HudMode::FreeRun,      HudMode::Battle,    HudMode::BattleWide, HudMode::MachineSpeed,
                            HudMode::Replay,       HudMode::Demo,      HudMode::FreePractice, HudMode::TimeTrial};
    int matched = 0;
    for (size_t m = 0; m < modes.size(); ++m) {
        ByteSpan rec = modes.records[m];
        const HudMode mode = HudMode(rec[8]);
        bool used = false;
        for (HudMode o : ours) used = used || o == mode;
        if (!used) continue;
        std::vector<HudRow> disc_rows;
        for (size_t k = 0; k < 32; ++k) {
            const u64 id = rd64(rec, 0x10 + 8 * k);
            if (id == none) break;
            auto it = by_id.find(id);
            CHECK(it != by_id.end());
            if (it == by_id.end()) continue;
            ByteSpan r = it->second;
            const u16 part = rd16(r, 8);
            if (part == 0) continue;
            disc_rows.push_back({HudPart(part), u8(rd16(r, 10)), u8(s16(rd16(r, 0x18))), s16(rd16(r, 0x1A)),
                                 s16(rd16(r, 0x1C)), s16(rd16(r, 0x1E)), s16(rd16(r, 0x20)), r[0x12], r[0x0C]});
        }
        std::span<const HudRow> table = hud_layout(mode);
        CHECK(table.size() == disc_rows.size());
        for (size_t i = 0; i < std::min(table.size(), disc_rows.size()); ++i) {
            const HudRow &a = table[i], &b = disc_rows[i];
            const bool same = a.part == b.part && a.sub == b.sub && a.view == b.view && a.x == b.x && a.y == b.y &&
                              a.w == b.w && a.h == b.h && a.align == b.align && a.valign == b.valign;
            if (!same) std::printf("mode %d row %zu differs (part %d)\n", int(mode), i, int(b.part));
            CHECK(same);
        }
        ++matched;
    }
    CHECK(matched == 12);

    // The endurance layout is a races' matter: no licence, time trial, free run or machine
    // test event has 10 laps or more or a time limit.
    data::CarDatabase cars;
    CHECK(cars.load(vol));
    int endurance = 0;
    for (const data::Event& e : cars.events()) {
        const bool long_run = e.laps >= 10 || (e.bytes.size() > 0x1E4 && e.bytes[0x1E4] != 0);
        const bool solo = e.label.rfind("AT", 0) == 0 || e.label.rfind("L", 0) == 0 || e.label == "M400" ||
                          e.label == "M1000" || e.label == "MMAX";
        if (long_run) CHECK(!solo);
        endurance += long_run;
    }
    CHECK(endurance > 0);
}

TEST(race_demo_replay_decode) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    std::optional<Bytes> b = vfs.read("demo/L0B0003");
    CHECK(b.has_value());
    if (!b) return;
    std::string err;
    std::optional<DemoReplay> d = DemoReplay::parse(*b, &err);
    CHECK(d.has_value());
    if (!d) return;
    CHECK(d->time_ms == 30939 && d->automatic);
    // 240 hold steps + 30.939 s: the run lasts past step 2096.
    CHECK(d->size() > 2096);
    int throttled = 0;
    for (size_t i = 240; i < 2096; ++i) throttled += d->frames[i].throttle > 0;
    CHECK(throttled > 1000);
}

// The demo runs' car records (CarSaveEquipments): the car the run was recorded with. The arcade
// time trials on dirt ran on the stage-12 ("_m") tyres, the licence tests on the stock ones.
TEST(race_demo_car_record_disc) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    CHECK(db.load(vfs));
    auto tyres = [&](const char* code, std::string& car) {
        std::optional<Bytes> b = vfs.read(std::string("demo/") + code);
        std::optional<DemoReplay> d = b ? DemoReplay::parse(*b) : std::nullopt;
        std::array<u64, 27> parts{};
        sim::CarSettings st;
        int ref_final = 0;
        if (!d || d->car_record.size() != 0x1D0) return u64(0);
        game::decode_car_equipments(d->car_record, db, car, parts, st, ref_final);
        for (size_t i = 0; i < data::kCarPartTables.size(); ++i)
            if (data::kCarPartTables[i] == int(data::TableId::FrontTire)) return parts[i];
        return u64(0);
    };
    std::string car;
    CHECK(tyres("AT080001", car) == db::id_hash("ft_impreza_wrc_prot_01_m"));
    CHECK(car == "impreza_wrc_prot_01");
    CHECK(tyres("AT010001", car) == db::id_hash("ft_motersport_elise_00_k"));
    CHECK(tyres("L0B0003", car) == db::id_hash("ft_coupe_turboplus_00_a"));
    CHECK(car == "coupe_turboplus_00");
}

// cameras/start.cam: one record whose eye curves hold the pre-race shot's three cuts.
TEST(race_start_cam_disc) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    std::optional<Bytes> b = vfs.read("cameras/start.cam");
    CHECK(b && GtrcScript::is_gtrc(*b));
    if (!b) return;
    GtrcScript s = GtrcScript::parse(*b);
    CHECK(s.records.size() == 1);
    if (s.records.empty()) return;
    const GtrcRecord& r = s.records[0];
    render::Vec3 a = r.position(0), m = r.position(0.85f), e = r.position(1);
    CHECK(std::fabs(a.z - 30.44f) < 0.05f && std::fabs(a.x - 3.04f) < 0.05f); // 30 m ahead
    CHECK(m.z < 0 && std::fabs(m.x) < 3.0f);                                   // beside / behind
    CHECK(std::fabs(e.z - 3.97f) < 0.05f && std::fabs(e.x + 1.64f) < 0.05f);  // front corner
    CHECK(std::fabs(r.zoom.eval(0.5f) - 2.414f) < 0.01f);
}

// Route 5's wet-road reflection models (slot 9 pre-mirrored scenery, slot 13 alpha-only mask)
// are not part of the visible scenery; the mask's passes write only the frame alpha.
TEST(race_wet_road_models_disc) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Bytes data = disc->volume().read(*disc->volume().find("courses/US/route5"));
    course::CourseModelOptions o;
    o.textures = false;
    scene::Model visible = course::build_course_model(data, o, "route5");
    for (const scene::Mesh& m : visible.meshes) CHECK(m.name.find("reflection") == std::string::npos);
    o.scenery = o.course_line = o.backdrop = false;
    o.road_reflection = true;
    scene::Model refl = course::build_course_model(data, o, "r");
    CHECK(!refl.meshes.empty());
    float lowest = 0;
    for (const scene::Mesh& m : refl.meshes)
        for (const scene::Vertex& v : m.vertices) lowest = std::min(lowest, v.pos[1]);
    CHECK(lowest < -20.0f); // mirrored below the road (lamp heads 17..27 m up)
    o.road_reflection = false;
    o.road_reflection_mask = o.frame_alpha = true;
    scene::Model mask = course::build_course_model(data, o, "m");
    int alpha_only = 0, other = 0;
    for (const scene::Mesh& m : mask.meshes)
        for (const scene::Primitive& p : m.primitives) ((p.flags & scene::kAlphaOnly) ? alpha_only : other)++;
    CHECK(alpha_only > 0 && other == 0);
}

// Road shade (vertex attributes A/B): the grid lookup agrees with testing every triangle,
// open road is fully lit and the Trial Mountain bridge / tunnel stretches are shaded.
TEST(race_road_shade_disc) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Bytes data = disc->volume().read(*disc->volume().find("courses/US/trialmt"));
    course::CourseFile f = course::CourseFile::parse(data);
    course::Road road = course::Road::parse(f.section(data, course::kSlotRoad));
    std::vector<course::Vec3> line = road.centre_line();
    CHECK(line.size() > 100);
    int same = 0, lit = 0, shaded = 0, n = 0;
    for (size_t i = 0; i < line.size(); i += 3) {
        const course::Vec3& p = line[i];
        race::RoadShade g = race::road_shade(road, p.x, p.z, p.y + 1.0f);
        race::RoadShade b = race::road_shade(road, p.x, p.z, p.y + 1.0f, false);
        if (!b.hit) continue;
        ++n;
        if (g.hit && std::fabs(g.a - b.a) < 1e-4f && std::fabs(g.b - b.b) < 1e-4f) ++same;
        if (b.a > 0.99f && b.b > 0.99f) ++lit;
        if (b.b < 0.01f) ++shaded;
    }
    CHECK(n > 50);
    CHECK(same == n);
    CHECK(lit > n / 4); // Trial Mountain: forest shade and tunnels on much of the lap
    CHECK(shaded > 0);
}

// Point-light courses: road attribute A is a signed lamp index (0xFFFF = -1, the lamp before
// lamp 0), and the nine candidates start 4 before it, wrapping past either end of the list.
TEST(race_point_light_index_rule) {
    CHECK(race::lamp_index(0xFFFF) == -1);
    CHECK(race::lamp_index(0) == 0 && race::lamp_index(272) == 272);
    CHECK(race::first_candidate(-1, 273) == 268); // lamps 268..272, 0..3
    CHECK(race::first_candidate(272, 273) == 268);
    CHECK(race::first_candidate(0, 273) == 269);
    CHECK(race::first_candidate(4, 273) == 0);
    CHECK(race::first_candidate(100, 273) == 96);
    CHECK(race::first_candidate(-1, 5) == 0);
    // A ring of 24 lamps (GTRW +0x21 = 1; +0x58 count, +0x5C records {type, axis, pos, kind}).
    const int n = 24;
    Bytes b(0x60 + 0x20 * size_t(n), 0);
    auto put32 = [&](size_t at, u32 v) { for (int i = 0; i < 4; ++i) b[at + size_t(i)] = u8(v >> (8 * i)); };
    auto putf = [&](size_t at, float f) { u32 v; std::memcpy(&v, &f, 4); put32(at, v); };
    std::memcpy(b.data(), "GTRW", 4);
    b[0x21] = 1;
    put32(0x58, u32(n)), put32(0x5C, 0x60);
    const float kPi = 3.14159265f, r = 50.0f;
    for (int i = 0; i < n; ++i) {
        size_t o = 0x60 + 0x20 * size_t(i);
        float t = 2 * kPi * float(i) / float(n);
        putf(o + 8, 1.0f);                                                          // axis (0, 1, 0)
        putf(o + 0x10, r * std::cos(t)), putf(o + 0x14, 10.0f), putf(o + 0x18, r * std::sin(t));
        putf(o + 0x1C, 1.0f);                                                       // point lamp
    }
    race::CoursePointLights pl;
    pl.load(b);
    CHECK(pl.active() && pl.lamps().size() == size_t(n));
    race::SunParams e;
    e.lamps[0].c2 = 0.01f; // w = 1 / (1 + 0.01 d^2)
    // A car on the ring between lamp 23 and lamp 0, over the strip that holds 0xFFFF.
    float t = -kPi / float(n);
    render::Vec3 p{r * std::cos(t), 0, r * std::sin(t)};
    std::vector<race::SelectedLamp> sel = pl.select(e, p, race::lamp_index(0xFFFF));
    CHECK(sel.size() == 4);
    std::set<int> got;
    for (const race::SelectedLamp& l : sel) {
        got.insert(l.lamp);
        CHECK(l.w > 0.05f);
    }
    CHECK(got == (std::set<int>{22, 23, 0, 1}));
    // Read as unsigned (65535), the window (lamps 11..19) would lie away from the car.
    float far = 0;
    for (const race::SelectedLamp& l : pl.select(e, p, 0xFFFF)) far = std::max(far, l.w);
    CHECK(far < 0.1f * sel[0].w);
    std::vector<race::ReflectionDisc> discs = pl.reflection_discs(e, p, race::lamp_index(0xFFFF));
    CHECK(discs.size() == 6 && discs[0].sqrt_w == std::sqrt(sel[0].w));
}

// The night routes and circle tests: every road vertex's A names a lamp or is 0xFFFF, which
// only the strip at the start line holds (between the last lamp and lamp 0); a car there is
// lit by the lamps around the line about as strongly as just before or after it.
TEST(race_point_lights_start_line_disc) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    for (const char* name : {"route11", "r_route11", "route5", "route5_dry", "r_route5", "r_route5_dry", "L_circle60_dry1",
                             "L_circle60_wet1", "L_circle100_dry1", "L_circle100_wet1"}) {
        Bytes data = disc->volume().read(*disc->volume().find(std::string("courses/US/") + name));
        course::CourseFile f = course::CourseFile::parse(data);
        ByteSpan g = f.section(data, course::kSlotRoad);
        race::CoursePointLights pl;
        pl.load(g);
        CHECK(pl.active());
        int n = int(pl.lamps().size());
        course::Road road = course::Road::parse(g);
        int strip = 0, other = 0;
        for (const course::RoadSegment& s : road.segments)
            for (u32 w : s.vertex_attrs) {
                int a = int(w & 0xFFFF);
                if (a == 0xFFFF) ++strip;
                else if (a >= n) ++other;
            }
        CHECK(strip > 0 && other == 0);
        if (name[0] == 'L') continue;
        race::SunParams e = race::SunParams::from_env0(f.section(data, course::kSlotEnv));
        // Every triangle whose first vertex holds 0xFFFF (the wheel query's lamp index) vs the
        // triangles over lamps 0 and n - 1, the stretches on either side.
        double strip_min = 1e9, side_sum = 0;
        int strip_tris = 0, side_tris = 0;
        for (const course::RoadSegment& s : road.segments)
            for (const course::RoadTriangle& t : s.triangles) {
                const course::Vec3 &a = s.vertices[t.a], &b = s.vertices[t.b], &c = s.vertices[t.c];
                float x = (a.x + b.x + c.x) / 3, y = (a.y + b.y + c.y) / 3, z = (a.z + b.z + c.z) / 3;
                race::RoadShade rs = race::road_shade(road, x, z, y + 1.0f);
                if (!rs.hit || (rs.raw_a != 0xFFFF && rs.raw_a != 0 && rs.raw_a != n - 1)) continue;
                render::Vec3 p{x, y + 0.5f, z};
                std::vector<race::SelectedLamp> sel = pl.select(e, p, race::lamp_index(rs.raw_a));
                double w = 0;
                for (const race::SelectedLamp& l : sel) w += l.w;
                if (rs.raw_a != 0xFFFF) {
                    side_sum += w, ++side_tris;
                    continue;
                }
                ++strip_tris, strip_min = std::min(strip_min, w);
                CHECK(sel.size() == 4);
                for (const race::SelectedLamp& l : sel) CHECK(l.lamp >= n - 5 || l.lamp <= 3); // around the line
                CHECK(!pl.reflection_discs(e, p, race::lamp_index(rs.raw_a)).empty());
            }
        CHECK(strip_tris > 0 && side_tris > 0);
        CHECK(strip_min > 0.5 * side_sum / side_tris);
    }
}

// RACING / DRIFT from the arcade car page: the request's tire_mode picks the car's stage-10 /
// stage-11 tyre records.
TEST(race_arcade_drift_tyres_disc) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    CHECK(db.load(vfs));
    // A car-page request as the menus build it (car id without the leading '_').
    menu::flow::GameRequest rq;
    rq.mode = "single_race";
    rq.course = "trialmt";
    rq.car = "arm_gt_one_race_99";
    std::optional<race::ArcadeCar> racing, drift;
    rq.tire_mode = 0;
    racing = race::arcade_car(db, rq.car, rq.tire_mode, rq.course);
    rq.tire_mode = 1;
    drift = race::arcade_car(db, rq.car, rq.tire_mode, rq.course);
    CHECK(racing && drift);
    if (!racing || !drift) return;
    CHECK(racing->stage == 10 && drift->stage == 11);
    // The race's equipment: the stage's tyre records (arc_racing / arc_d_racing).
    std::optional<sim::exact::Equipment> er = exact_equipment(db, {racing->label, std::nullopt, std::nullopt, racing->stage});
    std::optional<sim::exact::Equipment> ed = exact_equipment(db, {drift->label, std::nullopt, std::nullopt, drift->stage});
    CHECK(er && ed && er->part[size_t(data::TableId::FrontTire)] != ed->part[size_t(data::TableId::FrontTire)]);
    // Dirt courses force stage 12.
    std::optional<race::ArcadeCar> dirt = race::arcade_car(db, rq.car, 1, "tahitidirt");
    CHECK(dirt && dirt->stage == 12);
    CHECK(!race::arcade_car(db, "r34_gtr_v_99", 0, "trialmt")); // a catalogue car, not an arcade entry
}

// 2P battle from the arcade 2P car page: player 2's own arcade car with DRIFT tyres (the request's
// car2 / tire_mode2) runs as the race's second player with them.
TEST(race_two_player_drift_disc) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    CHECK(db.load(vfs));
    menu::flow::GameRequest rq;
    rq.mode = "two_player_battle";
    rq.course = "trialmt";
    rq.car = "arm_gt_one_race_99";
    rq.car2 = "arm_gt_one_race_99";
    rq.tire_mode = 0;
    rq.tire_mode2 = 1;
    rq.transmission2 = 1;
    std::optional<race::ArcadeCar> p2 = race::arcade_car(db, rq.car2, rq.tire_mode2, rq.course);
    std::optional<race::ArcadeCar> p1 = race::arcade_car(db, rq.car, rq.tire_mode, rq.course);
    CHECK(p1 && p2);
    if (!p1 || !p2) return;
    Bytes course = disc->volume().read(*disc->volume().find("courses/US/trialmt"));
    std::unique_ptr<RaceTrack> track = make_track(vfs, course, "trialmt");
    FieldConfig cfg;
    cfg.max_opponents = 0;
    cfg.exact.data = program();
    cfg.exact.course_file = course;
    cfg.exact.course = "trialmt";
    cfg.exact.kind = sim::exact::Conductor::Battle2P;
    cfg.exact.laps = 2;
    cfg.exact.player.car = {p1->label, std::nullopt, std::nullopt, p1->stage};
    cfg.exact.second.car = {p2->label, std::nullopt, std::nullopt, p2->stage};
    std::string err;
    std::unique_ptr<RaceField> f = RaceField::create(*track, db, cfg, &err);
    CHECK(f != nullptr);
    if (!f) return;
    CHECK(f->has_second());
    // Player 2's entry: the drift tyres.
    std::shared_ptr<const sim::exact::RaceRecord> rec = exact_recording(*f);
    std::optional<sim::exact::Equipment> drift = exact_equipment(db, cfg.exact.second.car);
    CHECK(rec && rec->entries.size() == 2 && drift);
    if (rec && drift && rec->entries.size() == 2)
        CHECK(rec->entries[1].eq.part[size_t(data::TableId::FrontTire)] == drift->part[size_t(data::TableId::FrontTire)] &&
              rec->entries[1].eq.part[size_t(data::TableId::FrontTire)] != rec->entries[0].eq.part[size_t(data::TableId::FrontTire)]);
}

// The driver kind (the AI's run-up 1, the stop driver 5) survives a replay file round trip.
TEST(race_replay_driver_kind) {
    ReplayRecorder rec;
    rec.begin("trialmt", "r34_gtr_v_99", true, 0, {}, 60.0);
    for (int i = 0; i < 6; ++i) {
        rec.record(sim::ControlInputs{});
        rec.set_driver_kind(i < 2 ? 1 : i < 4 ? 0 : 5);
    }
    Replay recorded = rec.replay();
    recorded.exact = test::race_record_stub(); // (the race's record: a replay has one)
    Replay r = Replay::parse(recorded.serialize());
    ReplayPlayer p(&r);
    int kinds[6];
    for (int& k : kinds) p.next_inputs(), k = p.driver_kind();
    CHECK(kinds[0] == 1 && kinds[1] == 1 && kinds[2] == 0 && kinds[3] == 0 && kinds[4] == 5 && kinds[5] == 5);
}

// The players' reverse requests survive a file round trip (format v10); a file of a version before
// 11 is refused (ReplayTooOld), not played approximately.
TEST(race_replay_reverse) {
    ReplayRecorder rec;
    rec.begin("trialmt", "r34_gtr_v_99", true, 0, {}, 60.0);
    rec.set_second_car("s2000_99");
    const bool rev[6] = {false, true, true, false, true, false};
    for (int i = 0; i < 6; ++i) {
        rec.record_second(sim::ControlInputs{});
        rec.set_second_reverse(!rev[i]);
        rec.record(sim::ControlInputs{});
        rec.set_reverse(rev[i]);
    }
    Replay recorded = rec.replay();
    recorded.exact = test::race_record_stub(); // (the race's record: a replay has one)
    Replay r = Replay::parse(recorded.serialize());
    bool same = r.reverse.size() == 6 && r.reverse2.size() == 6;
    for (size_t i = 0; same && i < 6; ++i) same = (r.reverse[i] != 0) == rev[i] && (r.reverse2[i] != 0) == !rev[i];
    CHECK(same);

    // Versions 1..10: refused with the older-version message; a later version: not a ReplayTooOld.
    for (u32 v : {1u, 9u, 10u, 12u}) {
        Bytes b = recorded.serialize();
        std::memcpy(b.data() + 8, &v, 4);
        bool too_old = false, refused = false;
        try {
            Replay::parse(b);
        } catch (const ReplayTooOld& e) {
            too_old = refused = std::string(e.what()) == kReplayTooOld;
        } catch (const FormatError&) {
            refused = true;
        }
        CHECK(refused && too_old == (v < 11));
    }
}

// A race on the exact core in a replay (format v11): its record (setup, race entries, every car's
// frames, the game's changes, the end hash) survives the file bit for bit; a CPU car's frames cost
// nothing; a damaged record is refused.
TEST(race_replay_exact_record) {
    namespace ex = sim::exact;
    ex::RaceRecord x;
    x.program = 0x0123456789ABCDEFull, x.data = 0x1122334455667788ull, x.behaviour = {sim::Program::Pal, 60};
    x.course = "trialmt";
    x.course_category = 2;
    x.kind = ex::Conductor::Battle2P;
    x.config = 3;
    x.laps = 5;
    x.start_type = -1, x.countdown = 3, x.battle_laps = 5, x.battle_wear = 2, x.handicap = -20, x.battle_catchup = 1;
    x.replay = true, x.unjudged = false, x.effects = true;
    x.locked = true, x.logger = 1, x.logger_record = 0, x.speed_unit = 1;
    x.session.lap = 61234, x.session.sector[2] = 20000, x.licence_times = {1, 2, 3}, x.ghost_switch = 1;
    x.records.resize(2);
    x.records[1].laps.laps = {60000, 59000}, x.records[1].laps.count = 2, x.records[1].best_lap = 59000;
    x.records[1].best.sector[0] = 15000, x.records[1].top_speed = ex::F(250.5f);
    for (size_t k = 0; k < x.rules.size(); ++k) x.rules[k] = u8(k * 7);
    x.info = 0x100, x.conductor = 0x200, x.course_addr = 0x300;
    for (int i = 0; i < 3; ++i) {
        ex::StartEntry e;
        e.eq.car = 0x1000 + u64(i);
        for (size_t k = 0; k < e.eq.part.size(); ++k) e.eq.part[k] = k * 31 + u64(i);
        for (size_t k = 0; k < e.eq.setting.size(); ++k) e.eq.setting[k] = s32(k) - 20 * i;
        e.human = i < 2 ? 0x500 : 0, e.player = i < 2, e.grid_slot = 5 - i, e.pedal_style = i, e.spec_flag = 1, e.wheel_type = i;
        e.asm_on = i != 1, e.tcs_on = i != 2;
        for (size_t k = 0; k < 8; ++k) e.condition[k] = u32(k * 1000 + size_t(i));
        e.best_lap = 0x157529FF, e.car = 0x4000 + u32(i);
        x.entries.push_back(e);
    }
    x.pads.assign(3, {});
    for (u32 t = 0; t < 700; ++t) {
        if (t == 0) x.add_change({0, ex::RaceChange::StartCountdown});
        if (t == 250) x.add_change({0, ex::RaceChange::PedalStyle, 0, 1});
        if (t == 250) x.add_change({0, ex::RaceChange::Condition, 1, 77, 0x3F800000, 0x40000000});
        if (t == 600) x.add_change({0, ex::RaceChange::DriveOn, 0});
        ex::PadFrame p[3];
        p[0].flags = 0x1F, p[0].kind = 2, p[0].steer = s16(int(t % 97) * 80 - 3000), p[0].throttle = u16(t < 300 ? 4096 : 0);
        p[0].shift = s8(t % 50 == 0 ? 1 : 0), p[0].brake = u16(t >= 300 ? t : 0), p[0].third = u16(t > 640 ? 4096 : 0);
        if (t >= 100) p[1].flags = 0x1F, p[1].kind = 3, p[1].steer = -4096, p[1].back = u16(t % 3 ? 4096 : 0);
        x.add_frame(p);
    }
    x.final_hash = 0xFEEDFACECAFEBEEFull;
    {
        ex::PadFrame g[2];
        g[1].flags = 0x1F, g[1].kind = 2, g[1].steer = -77, g[1].throttle = 4096;
        x.frames = 401, x.add_nested(g), x.frames = 700; // (an extra step inside frame 400)
    }
    CHECK(x.frames == 700 && x.pads[2].empty() && x.pads[1].size() == 700 && x.pads[0].size() == 700);
    const Bytes xb = x.serialize();
    const ex::RaceRecord y = ex::RaceRecord::parse(xb);
    bool same = y.program == x.program && y.data == x.data && y.behaviour == x.behaviour && y.course == x.course && y.course_category == 2 && y.kind == x.kind && y.config == 3 &&
                y.laps == 5 && y.start_type == -1 && y.countdown == 3 && y.battle_laps == 5 && y.battle_wear == 2 &&
                y.handicap == -20 && y.battle_catchup == 1 && y.replay && !y.unjudged && y.effects && y.locked && y.logger == 1 &&
                y.speed_unit == 1 && y.session.lap == 61234 && y.session.sector == x.session.sector && y.records.size() == 2 &&
                y.records[1].laps.laps == x.records[1].laps.laps && y.records[1].laps.count == 2 && y.records[1].best_lap == 59000 &&
                y.records[1].best.sector == x.records[1].best.sector && y.records[1].top_speed.bits == x.records[1].top_speed.bits &&
                y.licence_times == x.licence_times && y.ghost_switch == 1 && y.rules == x.rules && y.info == 0x100 &&
                y.conductor == 0x200 && y.course_addr == 0x300 && y.frames == 700 && y.final_hash == x.final_hash &&
                y.changes == x.changes && y.entries.size() == 3 && y.nested.size() == 1 && y.nested[0].frame == 400 &&
                y.nested[0].pads.size() == 2 && y.nested[0].pads[1].steer == -77 && y.nested[0].pads[1].throttle == 4096;
    for (size_t i = 0; same && i < 3; ++i) {
        const ex::StartEntry &a = x.entries[i], &b = y.entries[i];
        same = a.eq.car == b.eq.car && a.eq.part == b.eq.part && a.eq.setting == b.eq.setting && a.human == b.human &&
               a.player == b.player && a.grid_slot == b.grid_slot && a.pedal_style == b.pedal_style && a.spec_flag == b.spec_flag &&
               a.wheel_type == b.wheel_type && a.asm_on == b.asm_on && a.tcs_on == b.tcs_on && a.condition == b.condition &&
               a.best_lap == b.best_lap && a.car == b.car;
    }
    for (int i = 0; same && i < 3; ++i)
        for (u32 t = 0; same && t < 710; ++t) {
            const ex::PadFrame a = x.pad(i, t), b = y.pad(i, t);
            same = a.flags == b.flags && a.kind == b.kind && a.shift == b.shift && a.steer == b.steer && a.throttle == b.throttle &&
                   a.brake == b.brake && a.back == b.back && a.third == b.third;
        }
    CHECK(same);
    CHECK(xb.size() < 700 * 12); // (changed fields only, under half of 3 x 16 bytes a frame; the CPU car's frames are one run)
    // In a replay file.
    Replay r;
    r.course = "trialmt", r.car = "r34_gtr_v_99";
    r.inputs.resize(700);
    r.exact = std::make_shared<const ex::RaceRecord>(x);
    const Replay q = Replay::parse(r.serialize());
    CHECK(q.exact && q.exact->serialize() == xb && q.ticks() == 700);
    r.exact.reset();
    bool too_old = false; // (a run before every race ran on the exact core: refused)
    try {
        Replay::parse(r.serialize());
    } catch (const ReplayTooOld&) {
        too_old = true;
    }
    CHECK(too_old);
    // Damaged: refused.
    int refused = 0;
    for (size_t cut : {size_t(3), size_t(40), xb.size() / 2, xb.size() - 1}) {
        Bytes b(xb.begin(), xb.end() - std::ptrdiff_t(cut));
        try {
            ex::RaceRecord::parse(b);
        } catch (const FormatError&) {
            ++refused;
        }
    }
    CHECK(refused == 4);
}

// The race result GT mode records (place, prize, trophy mark, best time) is latched when the
// live race ends, before the replay: every way out reports that run once.
TEST(race_result_latch_exit_paths) {
    using gt3r::menu::flow::GameResult;
    auto live = [] {
        GameResult r;
        r.mode = "go_race";
        r.race_code = r.event = "Gsunday_e_0001";
        r.finished = true;
        r.position = 2;
        r.cars = 6;
        r.total_time = 170.5;
        r.best_lap = 56.1;
        return r;
    };
    // During the replay the field is re-run: the screen's own state is mid-race (unfinished).
    auto mid_replay = [] {
        GameResult r;
        r.mode = "go_race";
        r.race_code = r.event = "Gsunday_e_0001";
        r.finished = false;
        r.position = 0;
        r.cars = 6;
        return r;
    };
    // 1. Auto replay, left with the replay pause EXIT (then the quick menu's EXIT).
    {
        gt3r::race::RaceResultLatch l;
        l.capture(live());
        GameResult r = l.for_leave(mid_replay());
        CHECK(r.finished && r.position == 2 && r.total_time == 170.5 && r.best_lap == 56.1);
    }
    // 2. Left straight from the replay (Triangle / back / our Select list's exit).
    {
        gt3r::race::RaceResultLatch l;
        l.capture(live());
        GameResult cur = mid_replay();
        cur.prize_shown = true;
        GameResult r = l.for_leave(cur);
        CHECK(r.finished && r.position == 2 && r.prize_shown);
    }
    // 3. Quick-menu EXIT without watching the replay.
    {
        gt3r::race::RaceResultLatch l;
        l.capture(live());
        GameResult r = l.for_leave(live());
        CHECK(r.finished && r.position == 2);
    }
    // 4. A second capture (e.g. a replay's end) does not replace the live run.
    {
        gt3r::race::RaceResultLatch l;
        l.capture(live());
        GameResult other = live();
        other.position = 5;
        l.capture(other);
        CHECK(l.for_leave(mid_replay()).position == 2);
    }
    // 5. Retry: a new live run; leaving it unfinished reports it as such.
    {
        gt3r::race::RaceResultLatch l;
        l.capture(live());
        l.reset();
        CHECK(!l.has());
        CHECK(!l.for_leave(mid_replay()).finished);
    }
    // 6. Retiring from a championship after the race: the flag decided later is kept.
    {
        gt3r::race::RaceResultLatch l;
        l.capture(live());
        GameResult cur = mid_replay();
        cur.abandoned = true;
        CHECK(l.for_leave(cur).abandoned && l.for_leave(cur).finished);
    }
    // 7. The engine condition goes back as the live runs left it (physics §10.3), not as the
    //    latch caught it: the race screen keeps the live words through replays.
    {
        gt3r::race::RaceResultLatch l;
        GameResult at_finish = live();
        at_finish.car_condition = true, at_finish.car_odometer = 2100, at_finish.odometer_added = 2000;
        at_finish.car_wear_m = -296000, at_finish.garage_car = 3;
        l.capture(at_finish);
        GameResult cur = mid_replay();
        cur.car_condition = true, cur.car_odometer = 2150, cur.odometer_added = 2050;
        cur.car_wear_m = -295900, cur.car_oil_m = 4100, cur.garage_car = 3;
        GameResult r = l.for_leave(cur);
        CHECK(r.finished && r.position == 2 && r.car_condition && r.car_odometer == 2150 && r.odometer_added == 2050);
        CHECK(r.car_wear_m == -295900 && r.car_oil_m == 4100 && r.garage_car == 3);
    }
}

// Every item of the disc's race quick-menu layouts (menu/qm_*.mbl in the game's format, the
// ones the race drivers load) does something: its action is a known QmAction, or it opens a
// page of its layout, or it closes the popup ("No").
TEST(race_quick_menu_actions_disc) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    int layouts = 0, items = 0;
    std::set<std::string> actions;
    for (const std::string& name : vfs.list_dir("menu")) {
        std::string base = name.substr(name.find_last_of('/') + 1);
        if (base.rfind("qm_", 0) != 0 || base.size() < 4 || base.compare(base.size() - 4, 4, ".mbl") != 0) continue;
        std::optional<Bytes> b = vfs.read("menu/" + base);
        CHECK(b.has_value());
        if (!b) continue;
        menu::MblFile f = menu::MblFile::parse(*b);
        if (f.legacy) continue; // early test files the program never loads
        ++layouts;
        for (const menu::MblPage& p : f.pages)
            for (const menu::MblItem& it : p.items) {
                ++items;
                bool ok;
                if (!it.action.empty()) {
                    actions.insert(it.action);
                    ok = quick_menu_action(it.action) != QmAction::None;
                } else {
                    ok = (!it.link.empty() && f.find_page(it.link) >= 0) || (it.unk142 & 0xF) == 3;
                }
                if (!ok) std::printf("  %s %s:%s action '%s' link '%s' not handled\n", base.c_str(), p.name.c_str(),
                                     it.name.c_str(), it.action.c_str(), it.link.c_str());
                CHECK(ok);
            }
    }
    std::printf("  %d layouts, %d items, %zu actions\n", layouts, items, actions.size());
    CHECK(layouts == 12);
    CHECK(actions.size() >= 16);
}

TEST(race_quick_menu_disable_disc) {
    // menus §11, §5.3: every race quick menu's root page has the `disable` widget the disabled
    // items are drawn under (black at alpha 0xB1, an icon-sized 88 x 76 mask); the item names
    // the availability rules ask for are the disc's root-page items.
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    const char* const kLayouts[] = {"qm_arc_result", "qm_arc_2p_result", "qm_arc_tt_free", "qm_arc_tt_ltd",
                                    "qm_gt_event",   "qm_gt_event_champ", "qm_gt_license",  "qm_gt_mt_1000",
                                    "qm_gt_mt_400",  "qm_gt_mt_max",     "qm_gt_result",   "qm_gt_result_champ"};
    std::set<std::string> items;
    for (const char* l : kLayouts) {
        std::optional<Bytes> pb = vfs.read(std::string("menu/US/") + l + "1.pmb");
        std::optional<Bytes> mb = vfs.read(std::string("menu/") + l + ".mbl");
        CHECK(pb && mb);
        if (!pb || !mb) continue;
        menu::PmbFile f = menu::PmbFile::parse(*pb);
        const int root = f.find_page("root");
        CHECK(root >= 0);
        if (root < 0) continue;
        const menu::Element* dis = nullptr;
        for (const menu::Layer& ly : f.pages[size_t(root)].layers)
            if (ly.name == "defaultLayer")
                for (const menu::Element& e : ly.widgets)
                    if (e.name == "disable") dis = &e;
        CHECK(dis && dis->widget_class && *dis->widget_class == "disable" && dis->image && dis->colors);
        if (dis && dis->colors)
            for (const menu::Rgba& c : *dis->colors) CHECK(c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0xB1);
        if (dis) CHECK(dis->w == 88 && dis->h == 76);
        menu::MblFile m = menu::MblFile::parse(*mb);
        if (int p = m.find_page("root"); p >= 0)
            for (const menu::MblItem& it : m.pages[size_t(p)].items) items.insert(it.name);
    }
    for (const char* i : {"replay", "save_replay", "save_ghost", "data_logger", "setting", "exit", "save_game",
                          "free_session", "start", "demonstration", "next_session"})
        CHECK(items.count(i) == 1);
    // Of all the disc's items, before a time trial's first run: REPLAY, SAVE REPLAY, SAVE GHOST,
    // DATA LOGGER and SETTING are disabled.
    QuickMenuItems q;
    q.visit(RunKind::TimeTrial, false, false);
    std::set<std::string> disabled;
    for (const std::string& i : items)
        if (!q.enabled(i)) disabled.insert(i);
    CHECK((disabled == std::set<std::string>{"data_logger", "replay", "save_ghost", "save_replay", "setting"}));
}

TEST(race_quick_menu_backdrop_disc) {
    // menus §11.3, presentation §C5: the result menus (arcade race, 2P, GT race, championship)
    // have no race behind them, and their root page's `bg` is the opaque, textured checker over
    // the whole frame; the menus with the race behind them (time trial, free run, machine test,
    // the GT pre-race menus) have no such layer.
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    // The root page's full-frame opaque textured element, if any.
    auto opaque_bg = [&](const std::string& layout) -> std::optional<bool> {
        std::optional<Bytes> b = vfs.read("menu/US/" + layout);
        if (!b) return std::nullopt;
        menu::PmbFile f = menu::PmbFile::parse(*b);
        const int root = f.find_page("root");
        if (root < 0) return std::nullopt;
        for (const menu::Layer& l : f.pages[size_t(root)].layers)
            for (const menu::Element& e : l.elements) {
                if (!e.image || !e.colors || e.x != 0 || e.y != 0 || e.w < 640 || e.h < 448) continue;
                bool opaque = true;
                for (const menu::Rgba& c : *e.colors) opaque = opaque && c.a == 0xFF;
                if (opaque) return true;
            }
        return false;
    };
    for (const char* l : {"qm_arc_result1.pmb", "qm_arc_2p_result1.pmb", "qm_gt_result1.pmb", "qm_gt_result_champ1.pmb"})
        CHECK(opaque_bg(l) == std::optional<bool>(true));
    for (const char* l : {"qm_arc_tt_ltd1.pmb", "qm_arc_tt_free1.pmb", "qm_gt_mt_4001.pmb", "qm_gt_mt_10001.pmb",
                          "qm_gt_mt_max1.pmb", "qm_gt_event1.pmb", "qm_gt_event_champ1.pmb"})
        CHECK(opaque_bg(l) == std::optional<bool>(false));
}

// race/flare.bin layout: one mesh of one 4-vertex strip (positions, strip table, colours).
TEST(race_flare_mesh_parse) {
    Bytes b(0x20 + 0x50, 0);
    auto put32 = [&](size_t at, u32 v) { for (int i = 0; i < 4; ++i) b[at + size_t(i)] = u8(v >> (8 * i)); };
    auto putf = [&](size_t at, float f) { u32 v; std::memcpy(&v, &f, 4); put32(at, v); };
    b[0x0A] = 1;                           // mesh count
    put32(0x10, 0x20), b[0x14] = 5;        // offset, qwc
    size_t o = 0x20;
    put32(o, 0x6202C040), b[o + 4] = 1, b[o + 5] = 6, o += 8; // S-8 at 0x40: 1 strip of 4
    put32(o, 0x6804C000), o += 4;                             // V3-32 at 0x00: 4 positions
    const float p[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0.5f}};
    for (const auto& v : p)
        for (float c : v) putf(o, c), o += 4;
    put32(o, 0x6E04C080), o += 4;                             // V4-8 at 0x80: 4 colours
    for (int i = 0; i < 4; ++i) b[o + 4 * size_t(i)] = 0xFF, b[o + 4 * size_t(i) + 3] = u8(0x54 * i);
    std::vector<race::Flare::Mesh> m = race::Flare::parse(b);
    CHECK(m.size() == 1);
    if (m.size() != 1) return;
    CHECK(m[0].verts.size() == 4);
    CHECK((m[0].tris == std::vector<int>{0, 1, 2, 1, 2, 3}));
    CHECK(m[0].verts[3].x == 1 && m[0].verts[3].y == 1 && m[0].verts[3].z == 0.5f);
    CHECK(m[0].verts[2].rgba[0] == 0xFF && m[0].verts[2].rgba[1] == 0 && m[0].verts[2].rgba[3] == 0xA8);
    b[0x14] = 2; // packet cut short: the strip table points past the vertices it holds
    CHECK(race::Flare::parse(b).empty());
}

TEST(race_course_scene_lights) {
    // A synthetic Env0: scene lights 0..2 enabled (bits 11..13), light 1 ambient only.
    Bytes env(0x1380, 0);
    std::memcpy(env.data(), "Env0", 4);
    auto put = [&](size_t o, float v) { std::memcpy(env.data() + o, &v, 4); };
    u32 flags = (1u << 11) | (1u << 12) | (1u << 13) | 1u;
    std::memcpy(env.data() + 0x10, &flags, 4);
    auto light = [&](int n, std::array<float, 3> dir, float amb, float dif, float spec) {
        size_t o = 0xAD0 + 0x50 * size_t(n);
        for (int i = 0; i < 3; ++i) {
            put(o + 4 * size_t(i), dir[size_t(i)]);
            put(o + 0x10 + 4 * size_t(i), amb), put(o + 0x20 + 4 * size_t(i), dif), put(o + 0x30 + 4 * size_t(i), spec);
        }
    };
    light(0, {0, 2, 0}, 0.45f, 0.9f, 0.83f);
    light(1, {1, 0, 0}, 0.1f, 0.0f, 0.0f);
    light(2, {0, 0, 3}, 0.0f, 0.5f, 0.0f);
    light(3, {1, 1, 1}, 0.7f, 0.7f, 0.7f); // not enabled
    race::SunParams p = race::SunParams::from_env0(env);
    CHECK(p.scene_light_mask == 0x7);
    render::ItemLights il;
    p.course_lights(il);
    CHECK(il.count == 2); // light 1 has no diffuse or specular: no slot
    CHECK(std::fabs(il.dir[0][1] - 1.0f) < 1e-6f && std::fabs(il.dir[1][2] - 1.0f) < 1e-6f);
    CHECK(std::fabs(il.specular[0][0] - 0.83f) < 1e-6f && std::fabs(il.diffuse[1][0] - 0.5f) < 1e-6f);
    CHECK(std::fabs(il.ambient[0] - 0.55f) < 1e-5f); // the enabled lights' ambients, global ambient 0
}
