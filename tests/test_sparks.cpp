// The race's sparks (sim/exact/sparks.h, presentation §L7): the pool's lists, a burst's six random draws per particle and its
// ranges, the update's drag, gravity, kick window and bounce, the VU0 sine and cosine, the emission's gates (speed, road
// material, a point below the road, a hidden ghost), their colour keys, the race record's spark points; on the disc the 24
// car models with spark points and an F090/S race that sparks (and replays to the same race).

#include "test_framework.h"
#include "exact_drive.h"

#include "disc/core_image.h"
#include "disc/volume.h"
#include "formats/model/car_model.h"
#include "game/data/car_database.h"
#include "race/backend.h"
#include "race/exact_field.h"
#include "race/sparks.h"
#include "sim/exact/course_query.h"
#include "sim/exact/race.h"
#include "sim/exact/race_record.h"
#include "sim/exact/sparks.h"
#include "vfs/vfs.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>

using namespace gt3r;
namespace ex = gt3r::sim::exact;
using ex::F;

namespace {

// Draws n times from a copy of the register: where n draws leave it.
Vu0Random after(Vu0Random r, int n) {
    for (int i = 0; i < n; ++i) r.next();
    return r;
}

int draws_between(Vu0Random from, const Vu0Random& to, int max = 100000) {
    for (int n = 0; n <= max; ++n) {
        if (from.r == to.r) return n;
        from.next();
    }
    return -1;
}

// A flat 16 x 16 m road square at height 0 (two triangles of `material`, normals up), one grid cell.
course::Road flat_road(u16 material) {
    course::Road r;
    r.length = 100;
    course::RoadSegment s;
    s.vertices = {{0, 0, 0}, {0, 0, 16}, {16, 0, 0}, {16, 0, 16}};
    s.vertex_attrs = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    course::RoadTriangle a, b;
    a.a = 0, a.b = 1, a.c = 2, a.material = material; // (b - a) x (c - a) points up
    b.a = 2, b.b = 1, b.c = 3, b.material = material;
    s.triangles = {a, b};
    r.segments = {s};
    r.grid_index.xs = {0, 16}, r.grid_index.zs = {0, 16};
    r.grid_index.cell_begin = {0, 2}, r.grid_index.refs = {{0, 0}, {0, 1}};
    r.grid_index.cell_group_begin = {0, 1}, r.grid_index.groups = {{0, 0, 2}};
    return r;
}

// A car over the square's middle, its body point `h` above the road, at `kmh` along -Z (the physics frame: Z up, y = -z).
ex::Car car_at(float h, float kmh) {
    ex::Car c;
    c.draw_pos = c.draw_pos_prev = {F(8.0f), F(h), F(8.0f)};
    c.vel = {F(0.0f), F(kmh / 3.6f), F(0.0f)};
    c.road_seg = -1;
    return c;
}

} // namespace

TEST(sparks_pool_lists) {
    ex::Sparks s;
    CHECK(s.first() == -1 && s.active() == 0);
    Vu0Random r;
    r.init(Vu0Random::kRunSeed);
    const ex::Vec3 p{F(0.0f), F(0.05f), F(0.0f)}, v{F(30.0f), F(0.0f), F(0.0f)}, n{F(0.0f), F(1.0f), F(0.0f)};
    s.burst(p, v, n, F(0.0f), 5, r);
    CHECK(s.active() == 5);
    // Newest first: the active list's serials fall; the first particle made came off the free list's head (pool 0).
    u32 last = ~0u;
    int k = s.first(), at = 0;
    for (; k >= 0; k = s.pool()[size_t(k)].next, ++at) {
        CHECK(s.pool()[size_t(k)].serial < last);
        last = s.pool()[size_t(k)].serial;
        CHECK(k == 4 - at);
    }
    // A full pool makes no more and steals none: 128 at most, the rest of the burst's draws not made.
    s.burst(p, v, n, F(0.0f), 200, r);
    CHECK(s.active() == ex::Sparks::kPool);
    const Vu0Random before = r;
    s.burst(p, v, n, F(0.0f), 20, r);
    CHECK(s.active() == ex::Sparks::kPool && r.r == before.r);
    s.reset();
    CHECK(s.active() == 0 && s.first() == -1);
}

TEST(sparks_burst_draws_and_ranges) {
    ex::Sparks s;
    Vu0Random r;
    r.init(Vu0Random::kRunSeed);
    const Vu0Random start = r;
    const ex::Vec3 p{F(1.0f), F(0.05f), F(2.0f)}, v{F(40.0f), F(-1.0f), F(-20.0f)}, n{F(0.0f), F(1.0f), F(0.0f)};
    s.burst(p, v, n, F(0.0f), 20, r);
    CHECK(r.r == after(start, 6 * 20).r); // six draws a particle
    const double vx = 40, vz = -20, h = std::sqrt(vx * vx + vz * vz);
    for (int k = s.first(); k >= 0; k = s.pool()[size_t(k)].next) {
        const ex::Spark& q = s.pool()[size_t(k)];
        CHECK(q.pos.x.bits == p.x.bits && q.pos.y.bits == p.y.bits && q.pos.z.bits == p.z.bits); // (no position spread)
        const double hx = q.vel.x.value(), hz = q.vel.z.value(), hk = std::sqrt(hx * hx + hz * hz) / h;
        CHECK(hk > 0.2999 && hk < 0.7);                              // the speed factor 0.3 .. 0.7
        const double turn = std::atan2(hx * vz - hz * vx, hx * vx + hz * vz) * 180 / 3.14159265358979;
        CHECK(std::fabs(turn) <= 5.0001);                            // +-5 degrees about the vertical
        CHECK(std::fabs(q.vel.y.value() + 1.0) <= 0.1 * h + 1e-4);   // vy +- 0.1 of the horizontal speed
        CHECK(q.size.value() >= 0.0099 && q.size.value() < 0.03);
        CHECK(std::fabs(q.inv_mass.value() * 500 * std::pow(q.size.value(), 3) - 1) < 1e-4);
        CHECK(q.total.bits == q.life.bits && q.life.value() >= 0.5999 && q.life.value() < 2.0);
        const double sp = std::sqrt(hx * hx + hz * hz + q.vel.y.value() * q.vel.y.value());
        CHECK(std::abs(q.speed0 - int(sp * 1024)) <= 1);
    }
}

TEST(sparks_update_drag_gravity_kick_bounce) {
    // One particle by hand: no drag at w = 0 isn't possible (w >= 0.01), so check against the update's own formulas.
    ex::Sparks s;
    Vu0Random r;
    r.init(Vu0Random::kRunSeed);
    const ex::Vec3 p{F(0.0f), F(0.05f), F(0.0f)}, v{F(30.0f), F(0.0f), F(0.0f)}, n{F(0.0f), F(1.0f), F(0.0f)};
    s.burst(p, v, n, F(0.0f), 1, r);
    const int k = s.first();
    const F dt = F::raw(0x3C888889);
    const ex::Spark start = s.pool()[size_t(k)];
    Vu0Random before = r;
    s.update(dt, r);
    const ex::Spark& q = s.pool()[size_t(k)];
    CHECK(r.r == before.r); // age 1/60: outside the kick window, no draw
    CHECK(q.last.x.bits == start.pos.x.bits && q.last.y.bits == start.pos.y.bits);
    CHECK(std::fabs(q.pos.x.value() - start.vel.x.value() / 60) < 1e-5);
    CHECK(q.vel.x.value() < start.vel.x.value() && q.vel.x.value() > 0.9 * start.vel.x.value()); // drag against the motion
    CHECK(q.vel.y.value() < start.vel.y.value() - 9.81 / 60 + 1e-3);                            // gravity (and drag)
    CHECK(std::fabs(q.life.value() - (start.life.value() - 1.0 / 60)) < 1e-6);
    // The kick window 0.09 .. 0.11 s of age: one draw a frame there (without a bounce only frame 6, age 0.1), none
    // elsewhere; a bounce cuts the life (x 0.6) and so moves the age on.
    int kicks = 0;
    for (int f = 2; f <= 12 && s.active() > 0; ++f) {
        const ex::Spark b = s.pool()[size_t(k)];
        before = r;
        s.update(dt, r);
        const int d = draws_between(before, r, 4);
        CHECK(d == 0 || d == 1);
        if (s.active() == 0) break; // (its life ran out)
        const ex::Spark& a = s.pool()[size_t(k)];
        const double age = a.total.value() - (b.life.value() - 1.0 / 60);
        if (d == 1) {
            ++kicks;
            CHECK(age > 0.0899 && age < 0.11);
        }
        // Below the plane after the move: back on it with the life cut.
        if (a.pos.y.value() == 0.0 && b.pos.y.value() + b.vel.y.value() / 60 < 0)
            CHECK(std::fabs(a.life.value() - (b.life.value() - 1.0 / 60) * 0.6) < 1e-5);
        CHECK(a.pos.y.value() >= -1e-6); // never below the plane after the update
    }
    CHECK(kicks <= 2);
    // Its life runs out: freed.
    for (int f = 0; f < 200; ++f) s.update(dt, r);
    CHECK(s.active() == 0);
}

TEST(sparks_vu0_sin_cos) {
    double worst = 0;
    for (int i = -3600; i <= 3600; ++i) {
        const float a = float(i) * 0.37f;
        F sn, cs;
        ex::vu0_sin_cos(F(a), sn, cs);
        const double r = double(a) * 3.14159265358979323846 / 180;
        worst = std::max({worst, std::fabs(sn.value() - std::sin(r)), std::fabs(cs.value() - std::cos(r))});
    }
    std::printf("  vu0 sin / cos: worst error %.3g\n", worst);
    CHECK(worst < 4e-6);
    F sn, cs;
    ex::vu0_sin_cos(F(0.0f), sn, cs);
    // (the reduction's 180 x pi/180 is not quite the program's pi: sin 0 is a few units of 1e-7, as on the PS2)
    CHECK(std::fabs(sn.value()) < 3e-7 && std::fabs(cs.value() - 1) < 2e-7);
    // The matrix library's rotations post-multiply like glRotate: RotY(90) takes the model's +X to world -Z.
    ex::Vu0Matrix m = ex::Vu0Matrix::identity();
    m.rotate_y(F(90.0f));
    const ex::Vec3 q = m.point(F(1.0f), F(0.0f), F(0.0f));
    CHECK(std::fabs(q.x.value()) < 1e-6 && std::fabs(q.z.value() + 1) < 1e-6);
    m = ex::Vu0Matrix::identity();
    m.translate(F(1.0f), F(2.0f), F(3.0f));
    m.rotate_x(F(90.0f)); // the model's +Y to world +Z
    const ex::Vec3 u = m.point(F(0.0f), F(1.0f), F(0.0f));
    CHECK(std::fabs(u.x.value() - 1) < 1e-6 && std::fabs(u.y.value() - 2) < 1e-6 && std::fabs(u.z.value() - 4) < 1e-6);
}

TEST(sparks_emit_gates) {
    const ex::CourseQuery tarmac(flat_road(0)), grass(flat_road(2)), paved(flat_road(9));
    const std::vector<ex::SparkPoint> pts = {{F(0.0f), F(0.0f), F(1.0f)}, {F(0.5f), F(0.0f), F(1.5f)}};
    auto bursts = [&](const ex::Car& c, const ex::CourseQuery& q, bool drawn, std::span<const ex::SparkPoint> p) {
        ex::Sparks s;
        Vu0Random r;
        r.init(Vu0Random::kRunSeed);
        s.emit(c, p, q, drawn, r);
        return s.active();
    };
    // 5 cm below the road at 150 km/h: int(0.05 x 200) = 9 or 10 (EE float) per point, at least 5.
    const int n = bursts(car_at(-0.05f, 150), tarmac, true, pts);
    CHECK(n >= 2 * 9 && n <= 2 * 10);
    CHECK(bursts(car_at(-0.5f, 150), tarmac, true, pts) == 2 * 20);  // at most 20 a point
    CHECK(bursts(car_at(-0.001f, 150), tarmac, true, pts) == 2 * 5); // at least 5
    CHECK(bursts(car_at(0.01f, 150), tarmac, true, pts) == 0);       // above the road: none
    CHECK(bursts(car_at(-0.05f, 99), tarmac, true, pts) == 0);       // below 100 km/h: none
    CHECK(bursts(car_at(-0.05f, 150), grass, true, pts) == 0);       // grass: none
    CHECK(bursts(car_at(-0.05f, 150), paved, true, pts) > 0);        // material 9 sparks
    CHECK(bursts(car_at(-0.05f, 150), tarmac, false, pts) == 0);     // a hidden ghost: none
    CHECK(bursts(car_at(-0.05f, 150), tarmac, true, {}) == 0);       // a model without spark points
    ex::Car off = car_at(-0.05f, 150);
    off.draw_pos = off.draw_pos_prev = {F(30.0f), F(-0.05f), F(8.0f)}; // off the road: none
    CHECK(bursts(off, tarmac, true, pts) == 0);
    // The burst sits 0.05 m above the road under the point, the car's velocity in the course frame (x, height, -y).
    ex::Sparks s;
    Vu0Random r;
    r.init(Vu0Random::kRunSeed);
    s.emit(car_at(-0.05f, 150), std::span<const ex::SparkPoint>(pts.data(), 1), tarmac, true, r);
    const ex::Spark& q = s.pool()[size_t(s.first())];
    CHECK(std::fabs(q.pos.y.value() - 0.05) < 1e-6 && std::fabs(q.pos.z.value() - 9.0) < 1e-5);
    CHECK(q.vel.z.value() < -150 / 3.6 * 0.29); // forward along -Z
}

TEST(sparks_colour_keys) {
    CHECK(race::spark_colour(1.5f) == (127u | 76u << 8 | 25u << 16 | 127u << 24)); // key 0 above 0.8 s
    const u32 mid = race::spark_colour(0.6f); // halfway from key 0 to key 1
    CHECK((mid & 0xFF) == 127 && ((mid >> 8) & 0xFF) == u32(0.55f * 127.5f) && ((mid >> 16) & 0xFF) == u32(0.1f * 127.5f));
    CHECK((race::spark_colour(0.1f) & 0xFF) < (race::spark_colour(0.3f) & 0xFF)); // dark red at the end
    CHECK(race::spark_colour(0.0f) == 0);                                          // no key below 0: nothing added
}

TEST(race_record_spark_points_round_trip) {
    ex::RaceRecord r;
    r.entries.resize(2);
    r.pads.resize(2);
    r.sparks = {{{F(0.1f), F(0.0f), F(1.5f)}, {F(-0.1f), F(0.0f), F(1.5f)}}, {}};
    const ex::RaceRecord back = ex::RaceRecord::parse(r.serialize());
    CHECK(back.sparks.size() == 2 && back.sparks[0].size() == 2 && back.sparks[1].empty());
    CHECK(back.sparks[0][1] == r.sparks[0][1]);
}

TEST(car_spark_points_disc) {
    Disc* d = test::any_region_disc();
    if (!d) SKIP("needs GT3R_DISC");
    Vfs vfs(&d->volume());
    std::optional<Bytes> list = vfs.read("cars/.list");
    CHECK(list.has_value());
    if (!list) return;
    std::istringstream in(std::string(list->begin(), list->end()));
    std::set<std::string> day, eve, night;
    int models = 0;
    for (std::string id; std::getline(in, id);) {
        while (!id.empty() && (id.back() == '\r' || id.back() == ' ')) id.pop_back();
        if (id.empty()) continue;
        for (int pass = 0; pass < 3; ++pass) {
            std::optional<Bytes> f = vfs.read((pass == 2 ? "cars/night/" : pass ? "cars/eve/" : "cars/day/") + id);
            if (!f || !CarFile::is_car(*f)) continue;
            if (pass == 0) ++models;
            const std::vector<std::array<u32, 3>> p = car_spark_points(CarFile::parse(*f).info);
            if (p.empty()) continue;
            (pass == 2 ? night : pass ? eve : day).insert(id);
            for (const std::array<u32, 3>& w : p) { // at the body's lowest point, behind the origin
                float y, z;
                std::memcpy(&y, &w[1], 4), std::memcpy(&z, &w[2], 4);
                CHECK(std::fabs(y) < 0.1f && z > 0.8f && z < 2.0f);
            }
        }
    }
    std::printf("  %d car models: %zu with spark points (day), %zu (eve), %zu (night)\n", models, day.size(), eve.size(),
                night.size());
    CHECK(!day.empty());
    for (const std::string& id : night) CHECK(day.count(id) == 1);
    if (!test::test_disc()) return; // (PAL: 14 models, NTSC-J: 22; their car lists differ)
    CHECK(models == 286 && day.size() == 24 && eve == day && night.size() == 22);
    for (const char* id : {"ml0003", "ty0141", "ty2056", "ni0062", "la0007", "cv0024"}) CHECK(day.count(id) == 1);
    CHECK(day.count("ml0001") == 1 && night.count("ml0001") == 0 && night.count("pd0001") == 0);
}

namespace {

data::CarDatabase* database() {
    static data::CarDatabase db;
    static bool tried = false, ok = false;
    if (!tried) {
        tried = true;
        if (Disc* d = test::test_disc()) ok = db.load(d->volume());
    }
    return ok ? &db : nullptr;
}

std::shared_ptr<const ex::ProgramData> program() {
    static std::shared_ptr<const ex::ProgramData> d;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (Disc* disc = test::test_disc())
            if (std::optional<Bytes> core = disc->volume().read_program())
                d = std::make_shared<const ex::ProgramData>(ex::ProgramData::load(CoreImage::decode(*core)));
    }
    return d;
}

ex::PadFrame flat_out(const ex::Car& c, const ex::RaceCourse& course) {
    ex::PadFrame f;
    f.flags = 0x1F;
    f.kind = 2;
    ex::F dx, dy;
    const int line = c.ai.line ? c.ai.line : 6;
    ex::ai_target(c, course.ai, line, c.ai.next, ex::F(12.0f) + ex::F(0.6f) * c.speed, dx, dy);
    const ex::F angle = test::heading_angle(c, dx, dy);
    f.steer = s16(std::lround(std::clamp(angle.host() * 2.5f, -1.0f, 1.0f) * 4096.0f));
    if (c.speed * ex::F(3.6f) < ex::F(330.0f) - ex::F(300.0f) * ee::abs(angle)) f.throttle = 4096;
    else f.brake = 2048;
    return f;
}

} // namespace

// The F090/S on a time trial at speed: bursts from its spark points, the random register drawn for them (the same run
// without spark points leaves another register and other flaps), and the run's record replays to the same race.
TEST(race_sparks_disc) {
    Disc* d = test::test_disc();
    if (!d || !database() || !program()) SKIP("needs GT3R_DISC");
    data::CarDatabase& db = *database();
    Vfs vfs(&d->volume());
    const std::string course = "trialmt";
    std::optional<Bytes> file = vfs.read("courses/US/" + course);
    CHECK(file.has_value());
    if (!file) return;
    std::unique_ptr<race::RaceTrack> track = race::make_track(vfs, *file, course);
    auto points = [&](const std::string& model) {
        std::vector<ex::SparkPoint> out;
        if (std::optional<Bytes> f = vfs.read("cars/day/" + model); f && CarFile::is_car(*f))
            for (const std::array<u32, 3>& p : car_spark_points(CarFile::parse(*f).info))
                out.push_back({F::raw(p[0]), F::raw(p[1]), F::raw(p[2])});
        return out;
    };
    struct Run {
        int bursts = 0, max_active = 0;
        u32 r = 0;
        std::shared_ptr<const ex::RaceRecord> rec;
    };
    auto run = [&](bool sparks, int steps) {
        Run out;
        race::FieldConfig cfg;
        race::ExactFieldOptions o;
        o.data = program();
        o.course_file = std::make_shared<const Bytes>(*file);
        o.course = course;
        o.kind = ex::Conductor::TimeTrial;
        o.laps = 2;
        o.player.car = {"mclaren_mp4_5b"};
        if (sparks) o.spark_points = points;
        cfg.exact = o;
        std::string err;
        std::unique_ptr<race::RaceField> field = race::RaceField::create(*track, db, cfg, &err);
        if (!field) std::printf("  %s\n", err.c_str());
        CHECK(field != nullptr);
        if (!field) return out;
        const ex::ExactRace& r = *race::exact_race_of(*field);
        CHECK(r.has_sparks() == sparks);
        u32 last = 0;
        for (int k = 0; k < steps; ++k) {
            const ex::PadFrame f = flat_out(r.cars()[0], r.course());
            sim::ControlInputs in;
            in.steer = -float(f.steer) / 4096.0f, in.throttle = float(f.throttle) / 4096.0f, in.brake = float(f.brake) / 4096.0f;
            in.controller_kind = 2;
            field->player().step(in, 1.0 / 60.0);
            const ex::Sparks& s = r.sparks();
            if (s.first() >= 0 && s.pool()[size_t(s.first())].serial != last) ++out.bursts, last = s.pool()[size_t(s.first())].serial;
            out.max_active = std::max(out.max_active, s.active());
        }
        out.r = r.random().r;
        out.rec = race::exact_recording(*field);
        return out;
    };
    const int steps = 60 * 90;
    const Run with = run(true, steps), without = run(false, steps);
    std::printf("  F090/S at %s, %d frames: %d frames with new sparks, at most %d alive\n", course.c_str(), steps, with.bursts,
                with.max_active);
    CHECK(with.bursts > 0);
    CHECK(with.r != without.r);
    // Its record keeps the spark points and plays back to the same race.
    CHECK(with.rec && !with.rec->sparks.empty() && !with.rec->sparks[0].empty());
    if (!with.rec) return;
    const auto rec = std::make_shared<const ex::RaceRecord>(ex::RaceRecord::parse(with.rec->serialize()));
    race::FieldConfig pc;
    pc.exact.data = program();
    pc.exact.course_file = std::make_shared<const Bytes>(*file);
    pc.exact.course = course;
    pc.exact.kind = ex::Conductor::TimeTrial;
    pc.exact.player.car = {"mclaren_mp4_5b"};
    pc.exact.playback = rec;
    std::string err;
    std::unique_ptr<race::RaceField> play = race::RaceField::create(*track, db, pc, &err);
    CHECK(play != nullptr);
    if (!play) return;
    for (u32 k = 0; k < rec->frames; ++k) play->player().step({}, 1.0 / 60.0);
    CHECK(race::exact_playback_matches(*play) == std::optional<bool>(true));
    CHECK(race::exact_race_of(*play)->random().r == with.r);
}
