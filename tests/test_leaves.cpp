// The later builds' leaves (sim/exact/leaves.h, in the sparks' pool): none under GT3's rules; the shapes and their folded
// constants; the maker's draws (one, or thirteen with a leaf), the throw's direction and ranges, a full pool; the update's
// fall, its angles kept in 0 .. 2 pi, no draws, a leaf gone below its plane; the request's ground flags; the frame time;
// the draw's color.

#include "test_framework.h"

#include "race/leaves.h"
#include "sim/exact/leaves.h"
#include "sim/exact/sparks.h"
#include "sim/exact/tick.h"

#include <cmath>

using namespace gt3r;
namespace ex = gt3r::sim::exact;
using ex::F;
using sim::Program;

namespace {

const sim::Behaviour kTokyo{Program::ConceptTokyo, 60}, kGeneva{Program::ConceptGenevaEu, 50}, kGenevaAsia{Program::ConceptGenevaAsia, 60};

Vu0Random after(Vu0Random r, int n) {
    for (int i = 0; i < n; ++i) r.next();
    return r;
}

// A wheel's request over flat ground at height 0 (the plane raised 0.05 m, as the emitter has it), heading `yaw`, its rim
// `slip` m/s faster than its hub.
ex::TyreEffect request(float slip, float yaw = 0) {
    ex::TyreEffect q;
    q.x = F(10.0f), q.h = F(0.0f), q.z = F(-20.0f);
    q.yaw = F(yaw);
    q.normal = {F(0.0f), F(1.0f), F(0.0f)};
    q.plane_d = F(-0.05f);
    q.attr_a = F(0.75f);
    q.hub_vx = F(15.0f), q.rim_speed = F(15.0f + slip);
    q.ground_flags = 0x20;
    return q;
}

} // namespace

TEST(leaves_off_under_gt3_rules) {
    ex::Sparks s;
    CHECK(!s.leaves_on());
    s.set_leaves(sim::Behaviour{Program::NtscU, 60});
    CHECK(!s.leaves_on());
    Vu0Random r;
    r.init(0.3f);
    const Vu0Random before = r;
    s.leaf(request(30), {}, r);
    CHECK(r.r == before.r && s.first() == -1);
    s.set_leaves(kTokyo);
    CHECK(s.leaves_on());
}

TEST(leaf_shapes) {
    const ex::LeafRules t = ex::leaf_rules(kTokyo), g = ex::leaf_rules(kGeneva);
    const float sides[8] = {0.12f, 0.13f, 0.14f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f};
    for (size_t k = 0; k < 8; ++k) {
        const ex::LeafShape& s = t.shapes[k];
        const double w = s.w.value(), m = s.mass.value();
        CHECK(std::fabs(w - sides[k]) < 1e-6 && s.h.bits == s.w.bits && std::fabs(m - 0.001) < 1e-9);
        CHECK(std::fabs(s.inv_mass.value() * m - 1) < 1e-6);
        CHECK(std::fabs(s.diag.value() - w * std::sqrt(2.0)) < 1e-6);
        CHECK(std::fabs(s.ix.value() - m * w * w / 12) < 1e-9 && s.ix.bits == s.iz.bits);
        CHECK(std::fabs(s.iy.value() - m * 2 * w * w / 12) < 1e-9);
        CHECK(std::fabs(s.lift.value() + 2 * w * w) < 1e-7 && std::fabs(s.drag.value() + 0.02 * w) < 1e-7);
        CHECK(std::fabs(s.damp_x.value() + 2 * std::pow(w, 4) / 12 * 0.015) < 1e-9 && s.damp_x.bits == s.damp_z.bits);
        CHECK(s.corners[0].x.value() == -w / 2 && s.corners[3].z.value() == w / 2);
    }
    // 2002 Tokyo-Geneva's constants folded toward zero: the mass, the 0.1 and 0.14 m sides, 2 pi, g, the spin kept.
    CHECK(t.shapes[0].mass.bits == 0x3A83126Fu && g.shapes[0].mass.bits == 0x3A83126Eu);
    CHECK(t.shapes[2].w.bits == 0x3E0F5C29u && g.shapes[2].w.bits == 0x3E0F5C28u);
    CHECK(t.shapes[1].w.bits == g.shapes[1].w.bits); // (0.13: the same in both)
    CHECK(t.two_pi.bits == 0x40C90FDBu && g.two_pi.bits == 0x40C90FDAu);
    CHECK(t.gravity.bits == 0x411CF5C3u && g.gravity.bits == 0x411CF5C2u);
    CHECK(t.spin_keep.bits == 0x3F7FBE77u && g.spin_keep.bits == 0x3F7FBE76u);
    CHECK(t.light_base.bits == 0x3E99999Au && g.light_base.bits == 0x3E999999u); // the draw's 0.3 + 0.7 x shade
    CHECK(t.light_gain.bits == 0x3F333333u && g.light_gain.bits == 0x3F333333u);
    CHECK(ex::leaf_rules(kGenevaAsia).carry.bits == g.carry.bits);
}

// The draw's color (race/leaves.h, 0x19D700): -(L.n) / |L| x (0.3 + 0.7 a), negated on the far side, at least 0; each
// channel trunc((diffuse c + ambient a) x 128), at most 0x80; alpha 0x80.
TEST(leaf_draw_color) {
    race::LeafLight l;
    l.dir = {0, 2, 0}; // (not normalised: the draw divides by its length)
    l.ambient = {0.25f, 0.5f, 2.5f};
    l.diffuse = {0.5f, 0.25f, 0};
    const render::Vec3 up{0, 1, 0};
    // The near side of a face turned to the light: c = -1 x 0.3 -> 0; ambient only, a = 0.5: 16, 32, 160 -> 0x80.
    CHECK(race::leaf_color(up, true, 0.5f, l, 0.3f, 0.7f) == 0x80802010u);
    // The far side: c = 0.3 + 0.35 = 0.65; red 0.5 x 0.65 + 0.125 = 0.45 -> 57 (0x39), green 0.1625 + 0.25 -> 52 (0x34).
    CHECK(race::leaf_color(up, false, 0.5f, l, 0.3f, 0.7f) == 0x80803439u);
    // Shade 0: no ambient, the diffuse at 0.3: red 0.15 x 128 = 19.2 -> 19.
    CHECK(race::leaf_color(up, false, 0.0f, l, 0.3f, 0.7f) == 0x80000913u);
}

TEST(leaf_maker_draws_and_throw) {
    ex::Sparks s;
    s.set_leaves(kTokyo);
    Vu0Random r;
    r.init(0.6f);
    // A slip that cannot reach 2 m/s (at most 0.95 x 2.1): one draw, no leaf.
    Vu0Random start = r;
    s.leaf(request(2.1f), {}, r);
    CHECK(r.r == after(start, 1).r && s.first() == -1);
    // Fast enough: thirteen draws, one leaf.
    int made = 0;
    for (int n = 0; n < 200; ++n) {
        const float slip = n % 2 ? 9.0f : -9.0f, yaw = 0.3f * float(n);
        start = r;
        const int before = s.active();
        s.leaf(request(slip, yaw), {F(0.0f), F(0.0f), F(0.0f)}, r);
        CHECK(r.r == after(start, 13).r && s.active() == before + 1);
        const int k = s.first();
        CHECK(s.is_leaf(k));
        const ex::Leaf& q = s.leaves()[size_t(k)];
        const ex::LeafRules& rules = s.leaf_rules();
        CHECK(q.shape < 8 && q.plane_d.bits == (F(-0.05f) + rules.shapes[q.shape].diag).bits && q.shade.value() == 0.75);
        CHECK(std::fabs(q.pos.x.value() - 10) <= 0.1 && std::fabs(q.pos.y.value()) <= 0.1 && std::fabs(q.pos.z.value() + 20) <= 0.1);
        CHECK(std::fabs(q.spin.x.value()) <= 10 && std::fabs(q.spin.y.value()) <= 4 && std::fabs(q.spin.z.value()) <= 10);
        for (F a : {q.angle.x, q.angle.y, q.angle.z}) CHECK(a.value() >= 0 && a.value() < 2 * 3.1415927);
        // Over flat ground the throw is along the wheel's heading +-15 degrees (backwards for a negative slip), 75 .. 85
        // degrees off the vertical, at 0.5 .. 0.95 of the slip speed.
        const double sp = std::hypot(std::hypot(q.vel.x.value(), q.vel.y.value()), q.vel.z.value());
        CHECK(sp >= 0.5 * 9 - 1e-3 && sp <= 0.95 * 9 + 1e-3);
        const double flat = std::hypot(q.vel.x.value(), q.vel.z.value());
        const double up = std::atan2(q.vel.y.value(), flat) * 180 / 3.14159265358979;
        CHECK(up >= 4.99 && up <= 15.01);
        const double heading = std::atan2(q.vel.x.value(), q.vel.z.value()) + (slip < 0 ? 3.14159265358979 : 0);
        CHECK(std::fabs(std::remainder(heading - yaw, 2 * 3.14159265358979)) <= 15.001 * 3.14159265358979 / 180);
        ++made;
        if (s.active() == ex::Sparks::kPool) break;
    }
    // The pool full: one draw, nothing made.
    CHECK(s.active() == ex::Sparks::kPool && made == ex::Sparks::kPool);
    start = r;
    s.leaf(request(9), {}, r);
    CHECK(r.r == after(start, 1).r && s.active() == ex::Sparks::kPool);
}

TEST(leaf_carries_the_cars_velocity) {
    ex::Sparks a, b;
    a.set_leaves(kTokyo), b.set_leaves(kTokyo);
    Vu0Random ra, rb;
    ra.init(0.4f), rb.init(0.4f);
    a.leaf(request(9), {F(0.0f), F(0.0f), F(0.0f)}, ra);
    b.leaf(request(9), {F(20.0f), F(0.0f), F(-10.0f)}, rb);
    const ex::Leaf &p = a.leaves()[size_t(a.first())], &q = b.leaves()[size_t(b.first())];
    CHECK(std::fabs(q.vel.x.value() - p.vel.x.value() - 17) < 1e-4 && std::fabs(q.vel.z.value() - p.vel.z.value() + 8.5) < 1e-4);
}

TEST(leaf_update_fall_angles_gone) {
    ex::Sparks s;
    s.set_leaves(kTokyo);
    Vu0Random r;
    r.init(0.2f);
    s.leaf(request(12), {}, r);
    CHECK(s.active() == 1);
    const int k = s.first();
    const ex::Leaf start = s.leaves()[size_t(k)];
    const Vu0Random before = r;
    const F dt = ex::particle_dt(kTokyo);
    int steps = 0;
    double lowest = 1e9, fastest_down = 0;
    while (s.active() == 1 && steps < 2000) {
        s.update(dt, r);
        ++steps;
        if (s.active() == 0) break;
        const ex::Leaf& q = s.leaves()[size_t(k)];
        for (F a : {q.angle.x, q.angle.y, q.angle.z}) CHECK(a.value() >= 0 && a.value() < 2 * 3.1415927);
        lowest = std::min(lowest, q.pos.y.value());
        fastest_down = std::min(fastest_down, q.vel.y.value());
        CHECK(std::fabs(q.spin.x.value()) < 1000);
    }
    CHECK(r.r == before.r);                        // the update draws nothing
    CHECK(s.active() == 0 && steps > 10);          // thrown up, it comes down and is gone below its plane
    CHECK(fastest_down < 0 && fastest_down > -9);  // a light plate: the air slows its fall well below free fall's
    CHECK(lowest < start.pos.y.value() + 3);
    // Gone: the plane's distance of its last position is below 0 (its center lower than the ground by the diagonal).
    const ex::Leaf& q = s.leaves()[size_t(k)];
    CHECK(q.pos.y.value() + q.plane_d.value() < 0);
}

TEST(leaf_request_ground_flags_and_frame_time) {
    ex::Car c;
    for (size_t i = 0; i < 4; ++i) c.g[i].flags = u8(i == 2 ? 0x21 : 0x01);
    const auto q = ex::tyre_effect_requests(c);
    CHECK(q[2].ground_flags == 0x21 && q[0].ground_flags == 0x01);
    CHECK(ex::particle_dt(sim::Behaviour{Program::NtscU, 60}).bits == 0x3C888889u);
    CHECK(ex::particle_dt(sim::Behaviour{Program::Pal, 50}).bits == 0x3C888889u); // (GT3's: 1/60 at either rate)
    CHECK(ex::particle_dt(kTokyo).bits == 0x3C888889u);
    CHECK(ex::particle_dt(kGeneva).bits == 0x3CA3D70Au);
    CHECK(ex::particle_dt(kGenevaAsia).bits == 0x3C888888u);
}
