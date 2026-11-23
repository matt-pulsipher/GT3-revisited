// The later builds' leaves (src/sim/exact/leaves.h, in the sparks' pool: sparks.h) against the original's particle manager,
// bit for bit, on every later build (GT3's programs have no leaves: the tests note it and pass):
//   - leaves_exact: a ParticleManager built by the program's own constructor (GT3's 0x19EEF8's counterpart: the pool and its
//     lists, the eight leaf shapes; its allocation, its file load of race/particle.img and its texture set-up stood in for
//     here), its shapes compared with ours; then random wheel requests to the leaf maker (Rule::Leaves: any slip, heading,
//     ground plane and car velocity, into a full pool too) and frames of the manager's update (0x19F698's counterpart, each
//     leaf's vf1), after every call the VU0 random register, the lists and every leaf's record compared;
//   - leaves_emit_exact: the tire effect emitter (0x1DF150's counterpart) on our race's car driven round a course with leaf
//     ground (a triangle flag 0x20: Autumn Ring) by sim::exact::ExactRace: every frame the car record as our step left it
//     written to the original's memory (its course object with the manager and no smoke), the emitter run from our race's
//     register against our requests (tyre_effect_requests, the ground's flag byte) and leaf maker, then the update.

#include "car_exact_probe.h"
#include "harness.h"
#include "leaves_probe.h"
#include "race_setup.h"

#include "base/ee_math.h"
#include "disc/disc.h"
#include "disc/volume.h"
#include "formats/course/course_file.h"
#include "formats/course/road.h"
#include "game/data/car_database.h"
#include "race/exact_setup.h"
#include "sim/exact/race.h"
#include "sim/exact/sparks.h"

#include <cmath>
#include <cstdio>
#include <map>

using namespace gt3r;
using namespace gt3r::diff;

namespace {

namespace ex = sim::exact;
namespace ce = car_exact;
using ee::F;
using namespace leaves_probe;

// (the arguments passed as they are: integers to %d / %X, doubles to %f)
template <class... A> std::string fmt(const char* f, A... a) {
    char s[256];
    std::snprintf(s, sizeof s, f, a...);
    return s;
}

} // namespace

DIFF_TEST_ALL(leaves_exact, "leaves (later builds): the manager's shapes, the leaf maker and the update vs sim::exact's, bit for bit") {
    Ee& e = ctx.ee;
    if (!sim::concept_program(program()) || !has_pa(later_fn(sim::Rule::Leaves))) {
        ctx.note("the program has no leaves (GT3's: none; a later build's leaf maker not recognised: see the rules)");
        ctx.check(!sim::concept_program(program()), "a later build without the leaf maker");
        return;
    }
    const sim::Behaviour how = disc_behaviour();
    const F dt = ex::particle_dt(how);
    const u32 maker = pa(later_fn(sim::Rule::Leaves));
    long calls = 0, made = 0, frames = 0, full = 0, gone = 0;
    std::string first;
    const int sequences = std::max(4, ctx.iterations / 50);
    for (int sq = 0; sq < sequences && first.empty(); ++sq) {
        e.reset();
        e.hle_libc_defaults();
        std::string err;
        Manager m = make_manager(e, err);
        if (!err.empty()) return ctx.check(false, err);
        ex::Sparks ours;
        ours.set_leaves(how);
        if (sq == 0)
            if (std::string d = shapes_difference(e, m, ours.leaf_rules()); !d.empty()) first = "shapes: " + d;
        Vu0Random r;
        r.init(ctx.rng.uniform(0.01f, 0.99f));
        e.vr = r.r;
        const u32 req = e.alloc(kReq), vel = e.alloc(16);
        for (int t = 0; t < 360 && first.empty(); ++t) {
            // (every other sequence a storm of requests: the pool fills and the maker finds no room)
            const int n = sq % 2 ? int(ctx.rng.range(6, 14)) : ctx.rng.chance(0.5) ? int(ctx.rng.range(1, 4)) : 0;
            for (int b = 0; b < n && first.empty(); ++b) {
                // A wheel over a plane tilted up to 25 degrees, its slip up to +-20 m/s, any heading, a car velocity of any
                // direction up to 70 m/s.
                const float tx = ctx.rng.uniform(-0.45f, 0.45f), tz = ctx.rng.uniform(-0.45f, 0.45f);
                const float len = std::sqrt(tx * tx + 1 + tz * tz);
                ex::TyreEffect q;
                q.wheel = s32(ctx.rng.range(0, 3)), q.surface = s32(ctx.rng.range(0, 15));
                q.normal = {F(tx / len), F(1 / len), F(tz / len)};
                q.x = F(ctx.rng.uniform(-800, 800)), q.h = F(ctx.rng.uniform(-30, 90)), q.z = F(ctx.rng.uniform(-800, 800));
                const F d = F(-(q.normal.x.host() * q.x.host() + q.normal.y.host() * q.h.host() + q.normal.z.host() * q.z.host()));
                q.plane_d = d - (q.normal.y * F(0.05f));
                q.yaw = F(ctx.rng.uniform(-12.6f, 12.6f));
                q.attr_a = F(ctx.rng.uniform(0, 1));
                q.smoke_emit = F(ctx.rng.uniform(0, 1)), q.slip_ratio = F(ctx.rng.uniform(-1.2f, 1.2f));
                q.hub_vx = F(ctx.rng.uniform(-60, 60));
                q.rim_speed = q.hub_vx + F(ctx.rng.chance(0.2) ? ctx.rng.uniform(-3, 3) : ctx.rng.uniform(-20, 20));
                q.ground_flags = 0x20;
                const float sp = ctx.rng.uniform(0, 70), a = ctx.rng.uniform(-3.14159f, 3.14159f);
                const ex::Vec3 v{F(sp * std::cos(a)), F(ctx.rng.uniform(-6, 6)), F(sp * std::sin(a))};
                put_request(e, req, q);
                e.w32(vel, v.x.bits), e.w32(vel + 4, v.y.bits), e.w32(vel + 8, v.z.bits);
                full += alive(ours) == ex::Sparks::kPool;
                const int before = alive(ours);
                e.call(maker, {Arg::ptr(m.pm), Arg::ptr(req), Arg::ptr(vel)}, 50'000'000);
                ours.leaf(q, v, r);
                made += alive(ours) - before, ++calls;
                ctx.sample();
                if (std::string df = difference(e, m, ours, r); !df.empty()) first = fmt("sequence %d frame %d leaf", sq, t) + ": " + df;
            }
            if (!first.empty()) break;
            const int before = alive(ours);
            e.call(kUpdate, {Arg::ptr(m.pm), Arg::integer(0), Arg::fbits(dt.bits)}, 50'000'000);
            ours.update(dt, r);
            gone += before - alive(ours), ++frames;
            ctx.sample();
            if (std::string df = difference(e, m, ours, r); !df.empty()) first = fmt("sequence %d frame %d update", sq, t) + ": " + df;
        }
    }
    ctx.note(fmt("%.0f maker calls (%.0f leaves made, ", double(calls), double(made)) + fmt("%.0f into a full pool), ", double(full)) +
             fmt("%.0f frames of updates (%.0f leaves gone), dt ", double(frames), double(gone)) + fmt("%.9g", double(dt.host())));
    ctx.check(made > 0 && gone > 0 && full > 0, "the runs made no leaf, lost none or never filled the pool");
    ctx.check(first.empty(), first.empty() ? "ok" : first);
}

DIFF_TEST_ALL(leaves_emit_exact, "leaves (later builds): the tire effect emitter + update on a race over leaf ground vs sim::exact's, bit for bit") {
    Ee& e = ctx.ee;
    if (!sim::concept_program(program()) || !has_pa(later_fn(sim::Rule::Leaves))) {
        ctx.note("the program has no leaves");
        ctx.check(!sim::concept_program(program()), "a later build without the leaf maker");
        return;
    }
    Volume& vol = ctx.disc->volume();
    data::CarDatabase db;
    if (!db.load(vol)) return ctx.check(false, "database: " + db.error());
    const ex::ProgramData& data = ce::program_data(*ctx.disc);
    const sim::Behaviour how = disc_behaviour();
    const F dt = ex::particle_dt(how);
    // The first event on a course with leaf ground (GT3R_LEAF_EVENT: another), its first car.
    const data::Event* ev = std::getenv("GT3R_LEAF_EVENT") ? db.event(std::getenv("GT3R_LEAF_EVENT")) : nullptr;
    Bytes course;
    for (const data::Event& x : db.events()) {
        if (ev && &x != ev) continue;
        const Volume::Entry* ke = x.entries.empty() ? nullptr : vol.find(course_path(vol, x.course));
        if (!ke) continue;
        Bytes f = vol.read(*ke);
        const course::CourseFile cf = course::CourseFile::parse(f);
        const ByteSpan rs = cf.section(f, 28);
        if (rs.empty()) continue;
        bool leaves = false;
        for (const course::RoadSegment& s : course::Road::parse(rs).segments)
            for (const course::RoadTriangle& t : s.triangles) leaves = leaves || (t.flags & 0x20) != 0;
        if (!leaves) continue;
        ev = &x, course = std::move(f);
        break;
    }
    if (!ev || course.empty()) return ctx.check(false, "no event on a course with leaf ground");
    // The leaf ground's triangles' centers (the verges, 8 .. 30 m off the road's center line at Autumn Ring): the driver's
    // targets.
    struct Patch {
        float x, z;
        bool used = false;
    };
    std::vector<Patch> patches;
    {
        const course::CourseFile cf = course::CourseFile::parse(course);
        for (const course::RoadSegment& sg : course::Road::parse(cf.section(course, 28)).segments)
            for (const course::RoadTriangle& t : sg.triangles)
                if (t.flags & 0x20) {
                    const course::Vec3 &a = sg.vertices[t.a], &b = sg.vertices[t.b], &c = sg.vertices[t.c];
                    patches.push_back({(a.x + b.x + c.x) / 3, (a.z + b.z + c.z) / 3});
                }
    }
    std::string car = std::getenv("GT3R_LEAF_CAR") ? std::getenv("GT3R_LEAF_CAR") : "";
    for (const std::string& l : ev->entries)
        if (car.empty() && (db.car(l) || db.variant(l))) car = l;
    std::string err;
    std::optional<ex::Equipment> eq = race::exact_equipment(db, race::ExactCarSource{car}, &err);
    if (!eq) return ctx.check(false, car + ": " + err);
    // Ours: the event's race, the car alone from a moving start, laps enough not to finish, without the race's own particle
    // systems (the emitter is the test's).
    ex::RaceSetup s;
    s.data = &data, s.db = &db;
    s.event = ByteSpan(ev->bytes.data(), ev->bytes.size());
    s.course = ex::RaceCourse::load(course, data);
    s.kind = ex::Conductor::SinglePlayer;
    s.start_type = 3;
    s.laps = 99;
    s.effects = false;
    ex::StartEntry en;
    en.eq = *eq, en.human = 1, en.player = true;
    s.entries = {en};
    ex::ExactRace race(s);
    race.cars()[0].control = 0, race.cars()[0].driver_kind = 0; // on the pad from the start (no run-up by the AI)
    // The original's side: the manager, a course object holding it (no smoke emitter: +4 0), a car record.
    e.reset();
    e.hle_libc_defaults();
    Manager m = make_manager(e, err);
    if (!err.empty()) return ctx.check(false, err);
    const u32 kcourse = e.alloc(0x40), rec = e.alloc(car_stride() + 0x40, 16);
    e.w32(kcourse + 0xC, m.pm);
    ex::Sparks ours;
    ours.set_leaves(how);
    long frames = 0, on_leaves = 0, calls = 0, made = 0;
    std::string first;
    const int seconds = int(std::max(60L, long(ctx.iterations) / 10));
    int target = -1, play = 0, stuck = 0, cool = 0;
    for (int t = 0; t < 60 * seconds && first.empty(); ++t) {
        // The driver on the pad: to the nearest leaf patch not yet played on (at most 90 km/h on the way); on it, wheelspin and
        // locked wheels in turn with the steering swung for 3 s, then the patches within 40 m are done with and the next one
        // sought (4 s before the leaf ground counts again); backing out when it is stuck. The patches all played: from the
        // first again.
        const ex::Car& c = race.cars()[0];
        const float px = c.pos.x.host(), pz = -c.pos.y.host(), kmh = c.speed.host() * 3.6f;
        bool on = false;
        for (const ex::Ground& g : c.g) on = on || (g.flags & 0x20);
        if (target < 0 || patches[size_t(target)].used) {
            float best = 1e30f;
            target = -1;
            for (size_t k = 0; k < patches.size(); ++k)
                if (!patches[k].used && std::hypot(patches[k].x - px, patches[k].z - pz) < best)
                    best = std::hypot(patches[k].x - px, patches[k].z - pz), target = int(k);
            if (target < 0) {
                for (Patch& q : patches) q.used = false;
                target = 0;
            }
        }
        ex::PadFrame f;
        f.flags = 0x1F, f.kind = 2;
        const float dx = patches[size_t(target)].x - px, dy = -patches[size_t(target)].z - c.pos.y.host();
        const float ang = std::atan2(c.fwd.x.host() * dy - c.fwd.y.host() * dx, c.fwd.x.host() * dx + c.fwd.y.host() * dy);
        f.steer = s16(std::lround(std::clamp(ang * 2.5f, -1.0f, 1.0f) * 4096.0f));
        stuck = std::abs(kmh) < 3 && play == 0 ? stuck + 1 : 0;
        if (cool > 0) --cool;
        if ((on && cool == 0) || play > 0) {
            if (++play > 180) {
                for (Patch& q : patches)
                    if (std::hypot(q.x - px, q.z - pz) < 40) q.used = true;
                patches[size_t(target)].used = true, play = 0, cool = 240;
            }
            if ((t / 20) % 2) f.throttle = 4096;
            else f.brake = 4096;
            f.steer = s16(std::lround(std::sin(float(t) * 0.08f) * 4096.0f));
        } else if (stuck > 90 && stuck < 200) {
            f.back = 4096, f.steer = s16(-f.steer);
        } else {
            if (stuck >= 200) stuck = 0;
            if (kmh < 90.0f - 60.0f * std::min(1.0f, std::abs(ang))) f.throttle = 4096;
            else f.brake = 2048;
        }
        race.step(std::span<const ex::PadFrame>(&f, 1));
        // Ours: the emitter's leaf calls on our requests; the original's emitter on the same record and register.
        const ex::Car& now = race.cars()[0];
        Vu0Random r = race.random();
        const int before = alive(ours);
        bool any = false;
        for (const ex::TyreEffect& q : ex::tyre_effect_requests(now))
            if (q.ground_flags & 0x20) ours.leaf(q, ex::course_velocity(now), r), any = true, ++calls;
        ce::write_car(e, rec, now);
        e.w32(rec + 8, kcourse);
        e.vr = race.random().r;
        e.call(kEmitter, {Arg::ptr(rec)}, 50'000'000);
        made += alive(ours) - before, on_leaves += any, ++frames;
        ctx.sample();
        if (std::string df = difference(e, m, ours, r); !df.empty()) first = fmt("frame %d emitter", t) + ": " + df;
        e.call(kUpdate, {Arg::ptr(m.pm), Arg::integer(0), Arg::fbits(dt.bits)}, 50'000'000);
        ours.update(dt, r);
        if (first.empty())
            if (std::string df = difference(e, m, ours, r); !df.empty()) first = fmt("frame %d update", t) + ": " + df;
    }
    ctx.note(ev->label + " (" + ev->course + "), " + car + fmt(": %.0f frames, %.0f with a wheel on leaf ground (", double(frames), double(on_leaves)) +
             fmt("%.0f maker calls), %.0f leaves made", double(calls), double(made)));
    ctx.check(made > 0, "no leaf made in the run");
    ctx.check(first.empty(), first.empty() ? "ok" : first);
}
