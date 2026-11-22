// The later builds' rules of the exact core (sim/program.h's Rules after GT3's own, Gran Turismo Concept's builds): each
// port's branch against GT3's, on values of our own (no disc).

#include "base/ee_math.h"
#include "sim/collision_exact.h"
#include "sim/exact/build_math.h"
#include "sim/exact/hybrid.h"
#include "sim/exact/pit_lane.h"
#include "sim/exact/program_data.h"
#include "sim/exact/race_flow.h"
#include "sim/exact/start.h"
#include "sim/exact/tick.h"
#include "test_framework.h"

#include <cmath>
#include <cstring>
#include <string>

using namespace gt3r;
namespace ex = gt3r::sim::exact;
using ee::F;

namespace {
constexpr ex::Behaviour kGt3{ex::Program::NtscU, 60}, kTokyo{ex::Program::ConceptTokyo, 60},
    kGeneva{ex::Program::ConceptGenevaEu, 50};
}

// The programs: GT3's three releases, then the later builds; the European Tokyo-Geneva disc's own rate is PAL's.
TEST(build_rules_programs) {
    CHECK(sim::kPrograms == 8 && sim::kGt3Programs == 3 && sim::kGt3Rules == 24);
    for (int p = 0; p < sim::kPrograms; ++p) {
        const sim::Program g = sim::Program(p);
        CHECK(sim::gt3_program(g) == (p < 3) && sim::concept_program(g) == (p >= 3));
        CHECK(sim::gt3_release(g) == (p < 3 ? g : sim::Program::NtscU));
        CHECK(std::string(sim::program_name(g)).size() > 2);
        CHECK(sim::release_rate(g) == (g == sim::Program::Pal || g == sim::Program::ConceptGenevaEu ? 50 : 60));
    }
    // A later build's rules: its own for the routines it changes, a GT3 release's where a variant says so.
    ex::Behaviour b = kTokyo;
    CHECK(b.rule(sim::Rule::Atan2) == ex::Program::ConceptTokyo);
    b.variant[size_t(sim::Rule::RubberBand)] = u8(ex::Program::Pal);
    CHECK(b.rule(sim::Rule::RubberBand) == ex::Program::Pal && !b.follows_rules());
    // The synthetic program data of a later build serves GT3's NTSC-U tables.
    const ex::ProgramData d = ex::ProgramData::synthetic(ex::Program::ConceptGenevaAsia);
    CHECK(d.program() == ex::Program::ConceptGenevaAsia && d.table_release(ex::ProgramData::Table::Messages) == ex::Program::NtscU);
    CHECK(d.can_behave({ex::Program::ConceptGenevaAsia, 60}) && !d.can_behave({ex::Program::ConceptGenevaAsia, 50}) &&
          !d.can_behave({ex::Program::Pal, 60}));
    CHECK(ex::ProgramData::synthetic(ex::Program::ConceptGenevaEu).can_behave({ex::Program::ConceptGenevaEu, 50}));
}

// atan2f: GT3's is 0 whenever x + y == 0, the later builds' only at the origin; Tokyo-Geneva's pi literals one lower.
TEST(build_rules_atan2) {
    const F one = F::raw(0x3F800000), zero = F::raw(0), two = F::raw(0x40000000);
    CHECK(ex::atan2(kGt3, one, -one).bits == 0 && ee::atan2(one, -one).bits == 0);
    CHECK(ex::atan2(kTokyo, one, -one).bits != 0 && ex::atan2(kTokyo, zero, zero).bits == 0);
    for (const F y : {one, -one, two, zero})
        for (const F x : {one, two, -two, F::raw(0x3E800000)}) {
            if ((x + y).bits << 1 == 0) continue;
            CHECK(ex::atan2(kGt3, y, x).bits == ee::atan2(y, x).bits);
            CHECK(ex::atan2(kTokyo, y, x).bits == ee::atan2(y, x).bits); // (the same but for the shortcut)
        }
    // On the axes the result is the literal itself.
    CHECK(ex::atan2(kTokyo, zero, -one).bits == 0x40490FDB && ex::atan2(kGeneva, zero, -one).bits == 0x40490FDA);
    CHECK(ex::atan2(kTokyo, one, zero).bits == 0x3FC90FDB && ex::atan2(kGeneva, one, zero).bits == 0x3FC90FDA);
}

// The float-to-double conversion: GT3's exponent 255 is an infinity / NaN, the later builds' an ordinary exponent.
TEST(build_rules_widen) {
    for (const u32 f : {0x3F800000u, 0xC1234567u, 0x00000000u, 0x00000001u, 0x7F7FFFFFu}) {
        CHECK(ex::widen(kTokyo, F::raw(f)).bits == ee::widen(F::raw(f)).bits);
        CHECK(ex::widen(kGt3, F::raw(f)).bits == ee::widen(F::raw(f)).bits);
    }
    CHECK(ex::widen(kGt3, F::raw(0x7F800000)).bits == 0x7FF0000000000000ull);
    CHECK(ex::widen(kTokyo, F::raw(0x7F800000)).bits == 0x47F0000000000000ull);
    CHECK(ex::widen(kGeneva, F::raw(0xFF800001)).bits == 0xC7F0000020000000ull);
}

// The engine condition factor: 1 in the later builds, whatever the wear and oil words.
TEST(build_rules_engine_condition) {
    const ex::Condition k = ex::condition_params({30, 30, 50, 10, 20, 20, 50, 10, 30});
    const F worn = k.e3 + k.e3, old_oil = k.o1 + k.o1;
    CHECK(ex::condition_factor(kGt3, k, worn, old_oil).bits == ex::condition_factor(k, worn, old_oil).bits);
    CHECK(ex::condition_factor(k, worn, old_oil).bits != 0x3F800000);
    CHECK(ex::condition_factor(kTokyo, k, worn, old_oil).bits == 0x3F800000 && ex::condition_factor(kGeneva, k, F(), F()).bits == 0x3F800000);
}

// A car's collision body: Tokyo-Geneva's chamfer constant is one unit in the last place lower (the corners differ).
TEST(build_rules_body) {
    auto corner = [](const ex::Behaviour& how) {
        sim::collide::exact::Body b;
        b.behaviour = how;
        sim::collide::exact::build(b, F(2.1f), F(2.3f), F(1.7f), F(1200.0f), F(1800.0f), 0);
        return b;
    };
    const auto gt3 = corner(kGt3), tokyo = corner(kTokyo), geneva = corner(kGeneva);
    CHECK(std::memcmp(&gt3.p, &tokyo.p, sizeof gt3.p) == 0);
    CHECK(std::memcmp(&gt3.p, &geneva.p, sizeof gt3.p) != 0);
}

// The collision step's stuck nudge (Rule::StuckNudge): GT3 turns any car nearly at rest whose last hit came early in the
// step; the later builds only a pad car (control source 0) that touches something (here: a wall hit).
TEST(build_rules_stuck_nudge) {
    auto yaw = [](const ex::Behaviour& how, s8 control, bool touch) {
        sim::collide::exact::Body b;
        b.behaviour = how;
        sim::collide::exact::build(b, F(2.1f), F(2.3f), F(1.7f), F(1200.0f), F(1800.0f), 0);
        b.vx = F(0.5f), b.vy = F(-0.25f), b.w = F(0.0f), b.dt = F(1.0f / 60.0f), b.te = F(1.0f);
        b.steer = 50, b.control = control, b.wall_hit = touch;
        sim::collide::exact::fn::stuck(b, F(0.05f));
        return b.w.bits;
    };
    CHECK(yaw(kGt3, 2, false) != 0 && yaw(kGt3, 0, false) == yaw(kGt3, 2, false));
    CHECK(yaw(kTokyo, 0, true) == yaw(kGt3, 0, false) && yaw(kGeneva, 0, true) != 0);
    CHECK(yaw(kTokyo, 2, true) == 0 && yaw(kTokyo, 0, false) == 0 && yaw(kGeneva, 1, true) == 0);
}

// The slip angle's direction: the later builds' atan2f, Tokyo-Geneva's own fade constants.
TEST(build_rules_slip_angle) {
    F fade, fade2;
    const F v = F::raw(0x41200000); // 10 m/s
    CHECK(ex::slip_angle(kGt3, v, v, fade).bits == ex::slip_angle(v, v, fade2).bits && fade.bits == fade2.bits);
    CHECK(ex::slip_angle(kGt3, v, v, fade).bits == 0); // (GT3's shortcut: atan2f(-vy, vx) with vx = vy)
    CHECK(ex::slip_angle(kTokyo, v, v, fade).bits != 0 && fade.bits == 0x3F800000);
    // Between GT3's 10 km/h and Tokyo-Geneva's (two units lower) the fade is full on Tokyo-Geneva only.
    const F edge = F::raw(0x4031C71C), nothing = F::raw(0);
    (void)ex::slip_angle(kTokyo, edge, nothing, fade), (void)ex::slip_angle(kGeneva, edge, nothing, fade2);
    CHECK(fade.bits != 0x3F800000 && fade2.bits == 0x3F800000);
}

// The later builds' special cars (special.h): GT3's rules leave a record alone; a later build's steps move the state.
TEST(build_rules_special_cars) {
    const F one = F::raw(0x3F800000);
    ex::MoodTables k;
    k.time = {0, 180, 0, 180}, k.lean = {0, 2, 1, -1}, k.lamp = {0, 4, 4, 1};
    // Special car 1: a rival alongside (level 5) sets mood 1 for 120 steps (100 at 50 steps a second), the lamps fade
    // from class 0 over 24 steps (20), the nod runs; then the rest mood comes back.
    for (const ex::Behaviour& b : {kTokyo, kGeneva}) {
        const bool pal = b.rate == 50;
        ex::Car c{};
        c.behaviour = b, c.p.special = 1;
        ex::mood_step(c, k, 5, false);
        CHECK((c.later.special[1] & 0xF) == 1 && c.later.special[2] == (pal ? 99 : 119) && c.later.special[3] == (pal ? 19 : 23));
        CHECK(c.later.special[6] == 1 && c.later.special[4] == 1);
        // What its model shows: mood 1 asked for this step, its lamps' class 4 fading in (one step of 24; 20), the lean
        // level and the nod one step up (of half its period, 24; 20).
        const sim::VehicleState::Special look = ex::special_look(c, k);
        const float steps24 = pal ? 20.0f : 24.0f;
        CHECK(look.mood == 1 && look.mood_new == 1 && look.lamp == 4 && look.lamp_weight == 1 / steps24);
        CHECK(look.lean == 0.5f && look.nod == 0.5f + 1 / steps24 && look.wing == 0);
        int steps = 0;
        while ((c.later.special[1] & 0xF) == 1 && steps < 400) ex::mood_step(c, k, 0, false), ++steps;
        CHECK(steps == (pal ? 99 : 119) && c.later.special[1] == 0x10);
        // the stop count: up to 34 (29) while standing on the handbrake control, down again
        c.handbrake = one;
        for (int i = 0; i < 50; ++i) ex::stop_count_step(c);
        CHECK(c.later.special[0] == (pal ? 29 : 34));
        c.speed = one;
        ex::stop_count_step(c);
        CHECK(c.later.special[0] == (pal ? 28 : 33));
        // a finish: happy in first place, sad behind (once the lamps' fade is over)
        for (int i = 0; i < 40; ++i) ex::mood_step(c, k, 0, false);
        CHECK(c.later.special[3] == 0);
        c.finished = 1, c.rank = 2;
        ex::mood_step(c, k, 0, false);
        CHECK(c.later.special[6] == ex::kMoodSad);
    }
    // Special car 2: the wing moves out from 80 km/h at half a unit a second and in below 60 km/h.
    {
        ex::Car c{};
        c.behaviour = kTokyo, c.p.special = 2, c.dt = F(1.0f / 60), c.speed = F(23.0f);
        ex::wing_step(c);
        F v;
        std::memcpy(&v.bits, c.later.special.data(), 4);
        CHECK(c.later.special[4] == 1 && v.bits == (c.dt * F::raw(0x3F000000)).bits);
        for (int i = 0; i < 200; ++i) ex::wing_step(c);
        std::memcpy(&v.bits, c.later.special.data(), 4);
        CHECK(v.bits == one.bits);
        CHECK(ex::special_look(c, ex::MoodTables{}).wing == 1.0f && ex::special_look(c, ex::MoodTables{}).mood == 0);
        c.speed = F(17.0f); // between the two speeds: stays out
        ex::wing_step(c);
        CHECK(c.later.special[4] == 1);
        c.speed = F(16.0f);
        ex::wing_step(c);
        CHECK(c.later.special[4] == 0);
        // the 2002 builds' kind 9 follows the speed
        ex::Car d{};
        d.behaviour = kGeneva, d.p.special = 9, d.dt = F(1.0f / 50), d.speed = F(30.0f);
        ex::wing_step(d);
        std::memcpy(&v.bits, d.later.special.data(), 4);
        CHECK(v.bits == (F::raw(0x40000000) * d.dt).bits && d.later.special[4] == 0);
    }
    // A layout 7 car's byte; and GT3's rules: nothing of it runs in the step.
    {
        ex::Car c{};
        c.behaviour = kTokyo, c.p.d.layout = 7, c.later.special[5] = 9;
        ex::hybrid_step(c);
        CHECK(c.later.special[5] == 0);
        ex::Car g{};
        g.behaviour = kGt3;
        CHECK(!ex::later_step(g) && ex::later_step(c));
    }
    // The rival level: a car passing on the same lap is 5; a lap apart it is 1 or 2.
    {
        ex::PassBy m{};
        ex::GapMatrix gaps{};
        m[0][1] = 3;
        CHECK(ex::rival_level(0, 2, m, gaps, F(1000.0f)) == 5 && ex::rival_level(1, 2, m, gaps, F(1000.0f)) == 5);
        gaps.laps[0][1] = 1, gaps.laps[1][0] = -1;
        CHECK(ex::rival_level(0, 2, m, gaps, F(1000.0f)) == 1 && ex::rival_level(1, 2, m, gaps, F(1000.0f)) == 2);
        m[0][1] = 2, gaps = {};
        CHECK(ex::rival_level(0, 2, m, gaps, F(1000.0f)) == 4 && ex::rival_level(1, 2, m, gaps, F(1000.0f)) == 3);
        m[0][1] = 1;
        CHECK(ex::rival_level(0, 2, m, gaps, F(1000.0f)) == 0);
    }
}

// The later builds' AWD system 3 gain (Rule::AwdGain) and stepless gearbox start against GT3's.
TEST(build_rules_awd_gain_and_stepless) {
    const F one = F::raw(0x3F800000);
    ex::Awd3 k;
    k.fade_start = F::raw(0x3E800000), k.fade_floor = F(0.15f), k.negative_full = 1;
    ex::Car c{};
    c.p.d.layout = 5, c.p.d.awd_k = F(0.5f), c.p.d.awd_capacity = F(100.0f);
    c.clutch = 1, c.gear = 2, c.axle_speed = {F(10.0f), F(10.5f)};
    c.behaviour = kGt3;
    const F gt3 = ex::awd5_gain(c, -one, F(), F(), k);
    c.behaviour = kTokyo;
    CHECK(gt3.bits == 0x47BF6800);                                   // GT3: the full transfer (torque and rear term both negative)
    CHECK(ex::awd5_gain(c, -one, F(), F(), k).bits == 0xC7BF6800);   // the later builds: minus the full value
    k.negative_full = 0;
    CHECK(ex::awd5_gain(c, -one, F(2.0f), F(), k).bits == (F(0.5f) * F(0.5f)).bits); // the front term turns the sum: the gain
    c.gear = 0; // reverse: the torque's sign is turned (and the clutch engaged in reverse is the full transfer)
    CHECK(ex::awd5_gain(c, one, F(), F(), k).bits == 0x47BF6800);
    c.gear = 2, c.lat_g = one;
    CHECK(ex::awd5_gain(c, one, F(), F(), k).bits == ((F(0.5f) * F(0.5f)) * k.fade_floor).bits);
    // A stepless gearbox holds the engine at its hold speed, the ratio word between the last gear's and 1.
    ex::Driveline d{};
    d.kind = 1, d.gears = 2, d.down_rpm[0] = 3000, d.ratio[2] = F(0.4f);
    F w = F(200.0f), r{};
    CHECK(ex::stepless_ratio(d, w, r) && r.bits == one.bits && w.bits == F(200.0f).bits);
    w = F(400.0f);
    CHECK(ex::stepless_ratio(d, w, r) && r < one && F(0.4f) < r && w < F(400.0f));
    w = F(2000.0f);
    CHECK(ex::stepless_ratio(d, w, r) && r.bits == F(0.4f).bits);
    d.kind = 0;
    CHECK(!ex::stepless_ratio(d, w, r));
}

// The later builds' rules of the race step ported with the Tokyo listings [physics §14.3]: each branch against GT3's.
TEST(build_rules_later_step) {
    const F one = F::raw(0x3F800000), half = F::raw(0x3F000000);
    // The run-on after a finish: GT3 sets the throttle multiplier to a half, the later builds scale it, a half at least.
    ex::Driver d{};
    d.throttle_scale = one, d.speed_scale = one;
    ex::slow_down(d);
    const F k08 = F::raw(0x3F4CCCCD);
    CHECK(d.throttle_scale.bits == half.bits && d.speed_scale.bits == (one * k08).bits);
    d.throttle_scale = one, d.speed_scale = one;
    ex::slow_down(d, ex::Program::ConceptTokyo);
    CHECK(d.throttle_scale.bits == (one * k08).bits && d.speed_scale.bits == (one * k08).bits);
    ex::slow_down(d, ex::Program::ConceptTokyo), ex::slow_down(d, ex::Program::ConceptTokyo), ex::slow_down(d, ex::Program::ConceptTokyo);
    ex::slow_down(d, ex::Program::ConceptTokyo);
    CHECK(d.throttle_scale.bits == half.bits);
    d.throttle_scale = one;
    ex::slow_down(d, ex::Program::ConceptGenevaEu);
    CHECK(d.throttle_scale.bits == (one * F::raw(0x3F4CCCCC)).bits && d.throttle_scale.bits != (one * k08).bits);
    // The ASCC's switches: channel C alone counts as on with its status switch.
    ex::Aids a{};
    a.on[2] = 1;
    CHECK(ex::ascc_on(a) == 0 && ex::ascc_on(a, {1, 0, 1, 1}) == 0 && ex::ascc_on(a, {1, 1, 1, 1}) == 1);
    a.on[2] = 0, a.on[3] = 1;
    CHECK(ex::ascc_on(a, {1, 1, 1, 1}) == 0 && ex::ascc_on(a, {1, 1, 1, 0}) == 1);
    // The AI's contact: the later builds' 4 m and 5 km/h.
    ex::Car c{};
    c.behaviour = kGt3;
    ex::contact_intensity(c, F(3.0f), F());
    CHECK(c.ai.contact.bits == 0);
    c.behaviour = kTokyo;
    ex::contact_intensity(c, F(3.0f), F());
    CHECK(c.ai.contact.bits == 0x3E800000);
    ex::contact_intensity(c, F(30.0f), F(2.0f));
    CHECK(c.ai.contact.bits == one.bits);
    // The AI's outputs: no steering percentage, the handbrake standing on the stop point; the phase's filter instead.
    c = {};
    c.p.max_lock = F(0.5f), c.ai.steer = F(0.25f), c.dt = F(1.0f / 60.0f);
    c.behaviour = kGt3;
    ex::ai_outputs(c);
    CHECK(c.steer_pct == 50 && c.handbrake.bits == 0);
    ex::ai_steer_pct(c);
    CHECK(c.steer_pct == 50);
    c.behaviour = kTokyo;
    c.steer_pct = 0, c.ai.stop = 2;
    ex::ai_outputs(c);
    CHECK(c.steer_pct == 0 && c.handbrake.bits == one.bits);
    ex::ai_steer_pct(c); // 0.25 rad is 95 % of 15 degrees: two thirds of the way there
    CHECK(c.steer_pct == s8((0 + 2 * 95) / 3));
    // The axle step with the clutch open: layout 7 drives the rear axle and keeps the clutch torque word; the unequal
    // axles' wheel torques are scaled to the driven axle's radius.
    c = {};
    c.behaviour = kTokyo;
    c.dt = F(1.0f / 60.0f);
    c.p.d.layout = 7, c.p.d.diff_type = {0, 2}, c.p.d.axle_inertia = {F(1.0f), F(2.0f)}, c.p.d.ratio[1] = F(3.0f), c.gear = 1;
    c.clutch_torque = F(5.0f), c.t[2].torque = F(6.0f), c.t[3].torque = F(6.0f);
    ex::axle_step(c);
    CHECK(c.clutch_torque.bits == F(5.0f).bits && c.axle_speed[0].bits == 0 && c.axle_speed[1].bits == ((F(12.0f) / F(2.0f)) * c.dt).bits);
    c.behaviour = kGt3; // (GT3: layout 7 is the blended AWD; the word is the transmitted torque's magnitude)
    c.axle_speed = {};
    ex::axle_step(c);
    CHECK(c.clutch_torque.bits == 0);
    c = {};
    c.behaviour = kTokyo;
    c.dt = F(1.0f / 60.0f);
    c.p.d.layout = 5, c.p.d.unequal = 1, c.p.d.driven_axle = 1, c.p.d.diff_type = {2, 2}, c.p.d.axle_inertia = {F(1.0f), F(2.0f)};
    c.p.tire_radius = {F(0.25f), F(0.5f)}, c.p.inv_tire_radius = {F(4.0f), F(2.0f)};
    c.gear = 1, c.p.d.ratio[1] = F(3.0f), c.t[0].torque = F(4.0f), c.t[1].torque = F(4.0f);
    ex::axle_step(c); // no capacity, the axles at one speed: no coupling; the front sum 8 x (0.25 x 2)
    CHECK(c.axle_speed[0].bits == ((F(4.0f) / F(1.0f)) * c.dt).bits);
}

// 2002 Tokyo-Geneva's own parts of the race step: the class grip of phase 7 (AI-driven cars only), special kind 7's ride
// on the damper force, and the later builds' layout 7 (rear-driven) in the TCS's wheels.
TEST(build_rules_geneva_step) {
    // The class grip: the rule block's percentage of the car's driveline class on its slip fade and friction circle.
    ex::Car c{};
    c.behaviour = kGeneva;
    c.p.drive_class = 2;
    for (ex::Wheel& w : c.w) w.slip_fade = F(1.0f), w.circle = F(0.5f);
    const ex::ClassGrip grip{0, 0, 20, 0};
    c.control = 0, c.driver_kind = 0;
    ex::class_grip_scale(c, grip); // (a car on the pad: none)
    CHECK(c.w[0].circle.bits == F(0.5f).bits);
    c.control = 2;
    ex::class_grip_scale(c, grip);
    const F k = ee::int_to_f(20) * F::raw(0x3C23D70A) + F::raw(0x3F800000);
    CHECK(c.w[3].slip_fade.bits == (F(1.0f) * k).bits && c.w[3].circle.bits == (F(0.5f) * k).bits);
    c.behaviour = kTokyo; // (the earlier builds: none)
    ex::class_grip_scale(c, grip);
    CHECK(c.w[3].slip_fade.bits == (F(1.0f) * k).bits);
    // Special kind 7's ride: no drop below the lower speed; a lift below the static coordinate, in full past the travel.
    ex::Kind7Ride r;
    r.on = true, r.travel = F(0.025f), r.lift = F(0.5f), r.share = {F(0.75f), F(0.75f)}, r.drop = F(0.015f);
    r.v0 = F(125.0f / 3.6f), r.v1 = F(150.0f / 3.6f);
    ex::Car k7{};
    k7.behaviour = kGeneva;
    k7.p.susp[0].x_static = F(-0.1f), k7.p.susp[0].k = F(30000.0f), k7.p.xe54 = F(6000.0f);
    k7.speed = F(10.0f);
    CHECK(ex::kind7_ride_force(k7, 0, F(-0.05f), r).bits == 0);                                      // above the static point
    CHECK(ex::kind7_ride_force(k7, 1, F(-0.2f), r).bits == (((r.lift / r.share[0]) * k7.p.xe54) * F(0.5f)).bits); // past -travel
    k7.speed = F(50.0f); // (past the higher speed: the whole drop, and its spring force taken off)
    CHECK(ex::kind7_ride_force(k7, 0, F(0.0f), r).bits == (F() - k7.p.susp[0].k * r.drop).bits);
    // The TCS: a later build's layout 7 reads the rear wheels alone; GT3's layout 7 (AWD) both.
    CHECK(ex::tcs_reads_axle(7, 0) && !ex::tcs_reads_axle(7, 0, true) && ex::tcs_reads_axle(7, 1, true));
}

// The later builds' layout 7 system: its settings from the Hybrid table (the discs' bytes), the store's start, charge and
// draw with the flow display, the level between the marks, the rear motor's demand, and tanf.
TEST(build_rules_hybrid) {
    const std::array<u8, 0x1B> table = {20, 35, 6, 15, 6, 15, 50, 15, 20, 35, 20, 50, 75, 70, 1, 50, 10, 1, 7, 255, 20, 50, 50, 10, 50, 50, 10};
    CHECK(!ex::hybrid_settings(table, kGt3).on); // (GT3's rules: none)
    const ex::Hybrid h = ex::hybrid_settings(table, kTokyo);
    CHECK(h.on && h.capacity.bits == F(1125.0f).bits && h.front_charge_cap.bits == F(100.0f).bits && h.rear_charge_cap.bits == F(175.0f).bits);
    CHECK(h.front_drive.bits == F(60.0f).bits && h.rear_drive.bits == F(150.0f).bits && h.front_scale.bits == (F(50.0f) * F::raw(0x3DCCCCCD)).bits);
    CHECK(h.low.bits == ((F(1125.0f) * F(1.0f)) / F(100.0f)).bits && h.high.bits == F(562.5f).bits && h.reserve.bits == F(0.5f).bits);
    CHECK(h.flow_scale.bits == (F(6000.0f) / F(75.0f)).bits && h.gear_min == -1 && h.gear_lo == 1 && h.gear_hi == 7);
    CHECK(ex::hybrid_settings(table, kGeneva).flow_scale.bits == (F(5000.0f) / F(75.0f)).bits); // (Europe's 5000)
    CHECK(ex::hybrid_settings(table, kGeneva).drive_accel.bits == (F(50.0f) / F::raw(0x4474FFFE)).bits);
    ex::Car c{};
    c.behaviour = kTokyo;
    c.p.d.layout = 7;
    CHECK(ex::hybrid_car(c, h) && !ex::hybrid_car(c, {}));
    c.later.special[4] = 9, c.later.special[5] = 9;
    ex::hybrid_start(c, h);
    CHECK(ex::hybrid_store(c).bits == (F(1.0f) * h.capacity).bits && c.later.special[4] == 0 && c.later.special[5] == 0);
    CHECK(ex::hybrid_level(c, h).bits == F(1.0f).bits);
    // A full store takes nothing more; the display moves by the amount x the flow scale (truncated) up to 100.
    ex::set_hybrid_store(c, h.capacity);
    CHECK(ex::hybrid_charge(c, h, F(2.0f)).bits == 0 && c.later.special[5] == 100);
    CHECK(ex::hybrid_draw(c, h, F(0.5f)).bits == F(0.5f).bits && s8(c.later.special[5]) == 100 - 40);
    ex::set_hybrid_store(c, F(10.0f));
    CHECK(ex::hybrid_draw(c, h, F(30.0f)).bits == F(10.0f).bits && ex::hybrid_store(c).bits == 0 && s8(c.later.special[5]) == -100);
    CHECK(ex::hybrid_level(c, h).bits == 0);
    ex::set_hybrid_store(c, F(286.875f)); // half way between the marks
    CHECK(ex::hybrid_level(c, h).bits == ((F(286.875f) - h.low) / (h.high - h.low)).bits);
    // The demand: -1 with the throttle at or below the engine's share of the power, else up from there in the gears above +0x13.
    c.torque_wot = F(300.0f), c.torque_friction = F(20.0f), c.rpm = 3000, c.gear = 2, c.clutch = 1;
    c.throttle = F(0.0f);
    CHECK(ex::hybrid_demand(c, h).bits == F(-1.0f).bits);
    c.throttle = F(1.0f);
    CHECK(F(-1.0f) < ex::hybrid_demand(c, h) && !(F(1.0f) < ex::hybrid_demand(c, h)));
    CHECK(ex::hybrid_engine_throttle(c, h).bits == F(1.0f).bits);
    // tanf: odd, the reduction past pi / 4 (cotangent for an odd quarter turn), near the host's.
    for (float x : {0.1f, 0.5f, 0.78f, 1.0f, 1.4f, 2.0f, 3.0f, -2.5f})
        CHECK(std::abs(double(ee::tan(F(x)).host()) - std::tan(double(x))) < 1e-5 * (1 + std::abs(std::tan(double(x)))));
    CHECK(ee::tan(F(-0.5f)).bits == (ee::tan(F(0.5f)).bits ^ 0x80000000u));
}

// The later builds' pod race (race_flow.h): the stop zone from the program's two constants, a stop counted for a car that
// sits in it, the HUD prompts 0x10 / 0x11, the line's disqualification; special car 1's springs as it sits and its moods
// from the collision report. GT3's rules: none of it.
TEST(build_rules_pod_race) {
    const F lap = F(680.0f);
    // The zone: test kind 5 only, its start wrapped into the lap, none across the line, without a constant or under GT3's rules.
    const ex::StopZone z = ex::stop_zone_setup(kTokyo, 5, 608, 33, lap, 3);
    CHECK(z.active == 1 && z.base.bits == F(608.0f).bits && z.spacing.bits == F(33.0f).bits && z.cars == 3);
    CHECK(ex::stop_zone_setup(kTokyo, 5, -72, 33, lap, 3).base.bits == F(608.0f).bits);
    CHECK(ex::stop_zone_setup(kTokyo, 5, 608 + 2 * 680, 33, lap, 3).base.bits == F(608.0f).bits);
    CHECK(ex::stop_zone_setup(kTokyo, 5, 660, 33, lap, 3).active == 0);
    CHECK(ex::stop_zone_setup(kTokyo, 1, 608, 33, lap, 3).active == 0 && ex::stop_zone_setup(kGt3, 5, 608, 33, lap, 3).active == 0);
    CHECK(ex::stop_zone_setup(kTokyo, 5, 0, 33, lap, 3).active == 0 && ex::stop_zone_setup(kTokyo, 5, 608, 0, lap, 3).active == 0);
    // The rule each step (the race clock): car 0 the player's, cars 1 and 2 the computer's, each owing its first lap's stop.
    ex::RaceFlowInputs in;
    in.kind = ex::Conductor::SinglePlayer, in.behaviour = kTokyo, in.laps = 3;
    in.messages[0] = 0;
    in.moods.time.fill(60);
    auto race = [&](int sit0, float s0, bool on0, int sit1, float s1) {
        std::vector<ex::Car> cars(3);
        for (int i = 0; i < 3; ++i) {
            ex::Car& c = cars[size_t(i)];
            c.behaviour = kTokyo, c.index = u8(i), c.laps = 1, c.control = s8(i == 0 ? 0 : 2), c.p.special = 1;
            for (ex::Ground& g : c.g) g.flags = 4;
        }
        cars[0].later.special[0] = u8(sit0), cars[0].f3e4 = F(s0);
        if (!on0) cars[0].g[3].flags = 0;
        cars[1].later.special[0] = u8(sit1), cars[1].f3e4 = F(s1);
        cars[2].f3e4 = F(620.0f); // (on the zone, not sitting: the prompt)
        return cars;
    };
    {
        ex::RaceFlow flow;
        flow.zone = z;
        std::vector<ex::RaceEvent> events;
        std::vector<ex::Car> cars = race(0x22, 615.0f, true, 0x22, 610.0f);
        ex::race_mode_clock(cars, in, flow, events, {});
        // car 0 sits on the zone: its stop, the note 7 a player's stop gets; car 1 past the start, sitting: its stop, no note
        CHECK(flow.zone.stops[0] == 1 && flow.zone.stops[1] == 1 && flow.zone.stops[2] == 0);
        CHECK(events.size() == 1 && events[0].type == 18 && events[0].car == 0 && events[0].value == 7 && ex::display_word(events[0]) == 0x70);
        CHECK(cars[0].x51c == 0 && cars[2].x51c == 0x10);
        // the stops made: nothing owed, nothing more
        events.clear();
        ex::race_mode_clock(cars, in, flow, events, {});
        CHECK(flow.zone.stops[0] == 1 && events.empty());
    }
    {
        ex::RaceFlow flow;
        flow.zone = z;
        std::vector<ex::RaceEvent> events;
        std::vector<ex::Car> cars = race(0x21, 615.0f, true, 0x22, 600.0f); // car 0 not sitting yet; car 1 before the start
        ex::race_mode_clock(cars, in, flow, events, {});
        CHECK(cars[0].x51c == 0x10 && flow.zone.stops[0] == 0 && flow.zone.stops[1] == 0 && cars[1].x51c == 0x10 && events.empty());
        cars = race(0, 650.0f, false, 0, 600.0f); // car 0 past the zone's end, off its ground
        ex::race_mode_clock(cars, in, flow, events, {});
        CHECK(cars[0].x51c == 0x11);
        cars = race(0, 630.0f, false, 0, 600.0f); // off its ground inside it: nothing
        ex::race_mode_clock(cars, in, flow, events, {});
        CHECK(cars[0].x51c == 0);
        // Under the time trial's conductor (its own the race clock) nothing; nor at 2 cars' race of GT3.
        in.kind = ex::Conductor::TimeTrial;
        cars = race(0x22, 615.0f, true, 0x22, 610.0f);
        ex::race_mode_clock(cars, in, flow, events, {});
        CHECK(flow.zone.stops[0] == 0 && cars[2].x51c == 0);
        in.kind = ex::Conductor::SinglePlayer;
        // A race of two cars: a computer's stop is noted too.
        std::vector<ex::Car> two(cars.begin() + 1, cars.end());
        two[0].index = 0, two[1].index = 1;
        ex::RaceFlow f2;
        f2.zone = ex::stop_zone_setup(kTokyo, 5, 608, 33, lap, 2);
        events.clear();
        ex::race_mode_clock(two, in, f2, events, {});
        CHECK(f2.zone.stops[0] == 1 && events.size() == 1 && events[0].car == 0 && events[0].value == 7);
    }
    {
        // At the line: car 0 owes two stops: the stop driver, its lap put back, mood 7 for good, the event; the handler stops
        // there (no lap time, no finish). Owing one: the lap as any other.
        ex::RaceFlow flow;
        flow.zone = z;
        std::vector<ex::RaceEvent> events;
        std::vector<ex::Car> cars = race(0, 2.0f, false, 0, 2.0f);
        cars[0].laps = 2, cars[1].laps = 2, cars[1].x530 = 7;
        flow.zone.stops[1] = 1;
        int drove = -1;
        ex::RaceFlowHooks hooks;
        hooks.stop_drive = [&](ex::Car& c, int i) { drove = i, c.driver_kind = 5; };
        const std::vector<s32> order{0, 1, 2};
        ex::race_mode_lap(cars, order, 0, 1, 40000, 40000, 50, F(0.5f), in, flow, events, hooks);
        CHECK(drove == 0 && cars[0].laps == 1 && cars[0].driver_kind == 5 && cars[0].x530 == 0);
        CHECK((cars[0].later.special[1] & 0xF) == ex::kMoodSad && cars[0].later.special[2] == 0xFF);
        CHECK(events.size() == 1 && events[0].type == ex::kStopsMissedEvent && events[0].car == 0 && events[0].arg == 0);
        events.clear(), drove = -1;
        ex::race_mode_lap(cars, order, 1, 1, 40000, 40000, 50, F(0.5f), in, flow, events, hooks);
        CHECK(drove == -1 && cars[1].laps == 2 && cars[1].x530 == 40000 && !events.empty());
        // the split-screen battle's lap handler has the rule too; the time trial's has not
        in.kind = ex::Conductor::Battle2P;
        events.clear();
        cars[2].laps = 2;
        ex::race_mode_lap(cars, order, 2, 1, 40000, 40000, 50, F(0.5f), in, flow, events, hooks);
        CHECK(drove == 2 && cars[2].laps == 1);
        in.kind = ex::Conductor::SinglePlayer;
        flow.zone.active = 0; // (no zone: GT3's races)
        cars[0].laps = 2, drove = -1;
        ex::race_mode_lap(cars, order, 0, 1, 40000, 40000, 50, F(0.5f), in, flow, events, hooks);
        CHECK(drove == -1 && cars[0].laps == 2);
    }
    // The springs: special car 1's wheel i loses its spring once the sit count n is past 7 r (r = (i + index) mod 4,
    // mirrored from index 4 on; 6 r at 50 steps a second), down to 0 the step after.
    {
        const F k = F(30000.0f);
        ex::Car c{};
        c.behaviour = kTokyo, c.p.special = 1;
        for (int i = 0; i < 4; ++i) CHECK(ex::pod_spring(c, i, k).bits == k.bits);
        c.later.special[0] = 7;
        CHECK(ex::pod_spring(c, 0, k).bits == 0 && ex::pod_spring(c, 1, k).bits == k.bits && ex::pod_spring(c, 2, k).bits == k.bits);
        c.later.special[0] = 8;
        CHECK(ex::pod_spring(c, 1, k).bits == 0 && ex::pod_spring(c, 3, k).bits == k.bits);
        c.index = 5; // mirrored: wheel 0's r 3 - 1 = 2 (t 14), wheel 1's 1 (7), wheel 2's 0, wheel 3's 3 (21)
        c.later.special[0] = 14;
        CHECK(ex::pod_spring(c, 0, k).bits == k.bits && ex::pod_spring(c, 1, k).bits == 0 && ex::pod_spring(c, 2, k).bits == 0 &&
              ex::pod_spring(c, 3, k).bits == k.bits);
        c.behaviour = kGeneva, c.index = 0, c.later.special[0] = 6;
        CHECK(ex::pod_spring(c, 1, k).bits == k.bits && ex::pod_spring(c, 0, k).bits == 0);
        c.behaviour = kGt3, c.later.special[0] = 30;
        CHECK(ex::pod_spring(c, 0, k).bits == k.bits);
        c.behaviour = kTokyo, c.p.special = 2;
        CHECK(ex::pod_spring(c, 0, k).bits == k.bits);
        // the sitting test reads any car's byte
        c.later.special[0] = 0x22;
        CHECK(ex::sitting(c));
        c.behaviour = kGeneva;
        CHECK(!ex::sitting(c));
        c.later.special[0] = 0x1D;
        CHECK(ex::sitting(c));
    }
    // The collision report's moods: a wall 9, kind 2 5, a car by the part (5, 8, 4 for their table times; 210 steps from
    // part 8 on, 175 at 50 steps a second).
    {
        ex::MoodTables k;
        k.time.fill(40);
        auto hit = [&](const ex::Behaviour& b, s32 kind, s32 part) {
            ex::Car c{};
            c.behaviour = b, c.p.special = 1;
            ex::hit_mood(c, k, kind, part);
            return std::pair<int, int>(c.later.special[1] & 0xF, c.later.special[2]);
        };
        CHECK(hit(kTokyo, 0, 3) == std::make_pair(9, 40) && hit(kTokyo, 2, 0) == std::make_pair(5, 40));
        CHECK(hit(kTokyo, 1, 1) == std::make_pair(5, 40) && hit(kTokyo, 1, 6) == std::make_pair(8, 40) && hit(kTokyo, 1, 7) == std::make_pair(4, 40));
        CHECK(hit(kTokyo, 1, 9) == std::make_pair(5, 0xD2) && hit(kTokyo, 1, 0xB) == std::make_pair(4, 0xD2));
        CHECK(hit(kTokyo, 1, 0xF) == std::make_pair(4, 0xD2) && hit(kTokyo, 1, 0xC) == std::make_pair(8, 0xD2));
        CHECK(hit(kGeneva, 1, 9) == std::make_pair(5, 0xAF));
        CHECK(hit(kTokyo, 3, 0) == std::make_pair(0, 0) && hit(kGt3, 0, 0) == std::make_pair(0, 0));
    }
}

// The races' solo recorders in the later builds' store layout (Rule::RecordStore): a snapshot 4 bytes longer, so a lap's
// recording closes itself sooner; recorders without recordings take a race's layout, a free run's recordings of the other
// layout are refused; a saved best lap brings its layout to an empty visit.
TEST(build_rules_solo_recorder_layout) {
    auto event = [](ex::RecorderEvent::Kind k, u32 time = 0) {
        ex::RecorderEvent e;
        e.kind = k, e.time = time;
        return e;
    };
    CHECK(ex::later_store(kTokyo) && ex::later_store(kGeneva) && !ex::later_store(kGt3));
    ex::SoloRecorders g(true), l(true, true);
    CHECK(!g.later && l.later && l.lap.later && l.best.later && l.ghost.later && l.player.later);
    CHECK(g.lap.rec.block == ex::kRecordStore + 0x1B00 && g.lap.rec.capacity == 0x64F8);
    CHECK(l.lap.rec.block == ex::kRecordStore + 0x1B04 && l.lap.rec.capacity == 0x64F4);
    CHECK(l.best.rec.block == 2 * ex::kRecordStore + 0x1B04 && l.ghost.rec.block == 0x1B04);
    CHECK(g.stores_empty() && l.stores_empty());
    // the same frames on both lap recordings: the later layout's closes itself first
    auto frames = [](ex::SoloRecorders& r) {
        u32 n = 0;
        for (; !r.lap.rec.done && n < 200000; ++n) {
            ex::PadFrame f{};
            f.steer = s16((n * 37) % 4096), f.throttle = u16((n * 11) % 4096);
            r.lap_frame(f);
        }
        return n;
    };
    const u32 ng = frames(g), nl = frames(l);
    CHECK(nl < ng && ng < 200000);
    CHECK(std::equal(g.pool1.begin() + ex::kRecordStore + 0x1B08, g.pool1.begin() + ex::kRecordStore + 0x1B08 + 0x6000,
                     l.pool1.begin() + ex::kRecordStore + 0x1B0C)); // (the streams alike, 4 bytes on)
    // A race's layout: empty recorders are bound anew; a free run's lap of the other layout is refused.
    ex::SoloRecorders e(true);
    e.ghost_option = false;
    e.set_layout(true);
    CHECK(e.later && e.lap.later && e.lap.rec.block == ex::kRecordStore + 0x1B04 && !e.ghost_option && e.stores_empty());
    bool threw = false;
    g.apply(event(ex::RecorderEvent::close));
    try {
        g.set_layout(true);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw && !g.later);
    // (a race mode without the free run's stores: its devices take whole pools, any layout)
    ex::SoloRecorders t(false);
    ex::PadFrame f{};
    f.throttle = 4096;
    t.player_frame(f);
    t.set_layout(true);
    CHECK(t.later && t.player.rec.written);
    // A saved best lap (the later layout's) as an empty visit's ghost: the visit takes its layout; a visit with a lap of
    // its own in the other layout does not take it.
    l.apply(event(ex::RecorderEvent::close));
    l.apply(event(ex::RecorderEvent::lap, 61000));
    l.apply(event(ex::RecorderEvent::best));
    CHECK(l.best_time() == 61000);
    ex::SoloRecorders visit(true);
    CHECK(visit.ghost_from_best(l) && visit.later && !visit.ghost_empty && visit.ghost.rec.block == 0x1B04);
    CHECK(!g.ghost_from_best(l) && !g.later);
}

// The pass-by states: a slipstream held a second makes state 1, a car alongside 2, just ahead 3; behind again the
// pair's roles swap; at 50 steps a second the timer's cap and the second are PAL's (250, 51).
TEST(build_rules_pass_by) {
    for (const bool pal : {false, true}) {
        ex::PassByState p;
        ex::Proximity k{};
        k.slip[0][1] = F(0.5f);
        const int second = pal ? 51 : 61;
        for (int t = 0; t < second - 1; ++t) ex::pass_by_step(p, k, 2, pal);
        CHECK(p.timer[0][1] == second - 1 && p.state[0][1] == 0);
        ex::pass_by_step(p, k, 2, pal);
        CHECK(p.state[0][1] == 1 && p.state[1][0] == 0);
        for (int t = 0; t < 400; ++t) ex::pass_by_step(p, k, 2, pal);
        CHECK(p.timer[0][1] == (pal ? 250 : 300));
        k.code[0][1] = 5; // alongside
        ex::pass_by_step(p, k, 2, pal);
        CHECK(p.state[0][1] == 2);
        k.code[0][1] = 4; // just ahead
        ex::pass_by_step(p, k, 2, pal);
        CHECK(p.state[0][1] == 3);
        k.code[0][1] = 1, k.code[1][0] = 5; // behind again (the other alongside it): the other car's pass
        ex::pass_by_step(p, k, 2, pal);
        CHECK(p.state[0][1] == 0 && p.state[1][0] == 2);
        k = {};
        ex::pass_by_step(p, k, 2, pal);
        CHECK(p.state[1][0] == 1 && p.timer[0][1] == (pal ? 249 : 299)); // (code 0: state 2 falls back to 1)
    }
}
