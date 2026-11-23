// The race's sparks (src/sim/exact/sparks.h, presentation §T) against the original's ParticleManager, bit for bit, on every
// program (the later builds' manager made by the program's own constructor, leaves_probe.h; their particle record is GT3's
// with the particle kind's table at +0x04, the rest 4 bytes on):
//   - sparks_burst_update_exact: the burst 0x19F450 and the update 0x19F698 (0x19E7C8) on a manager made here (the pool, its
//     lists by 0x19EFF8, the tuning block 0x2C03B0 by its constructor 0x19F898), random bursts (any direction and speed, tilted
//     planes, 1 .. 25 particles, a full pool) and frames of updates (drag, gravity, the kick window, the bounces); after every
//     call the VU0 random register, the lists and every particle record compared;
//   - sparks_emit_exact: the emission 0x1AADD0 on our race's cars (an F090/S driven at speed round Trial Mountain, its race
//     run by sim::exact::ExactRace with its spark points), every frame: each car record as ours left it written to the
//     original's memory, the random register as our race step left it, then each car's emission and the update, against
//     ours (ExactRace::step's). The road query is the original's (0x18FC18 on the course relocated by race_setup), the
//     matrix library's VU0 macro code runs in the interpreter, and the micro program 0x257A44 starts (vcallms 0x1D0, which
//     the interpreter has not got) is run from the program's own VU0 micro code (0x349600, the 0x398 bytes 0x250820 sends to
//     VU0's micro memory) by a small executor here for the instructions it uses. From 2002 Tokyo-Seoul v2.00 on the emission
//     puts the bursts on the car body's plane (Rule::SparkEmit).

#include "car_exact_probe.h"
#include "harness.h"
#include "leaves_probe.h"
#include "race_setup.h"
#include "sparks_probe.h"

#include "base/ee_math.h"
#include "disc/disc.h"
#include "disc/volume.h"
#include "formats/model/car_model.h"
#include "game/data/car_database.h"
#include "race/exact_setup.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/race.h"
#include "sim/exact/sparks.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace gt3r;
using namespace gt3r::diff;

namespace {

namespace ex = sim::exact;
using ee::F;

using namespace sparks_probe;

// (the arguments passed as they are: integers to %d / %X, doubles to %f)
template <class... A> std::string fmt(const char* f, A... a) {
    char s[256];
    std::snprintf(s, sizeof s, f, a...);
    return s;
}

void put_vec(Ee& e, u32 a, const ex::Vec3& v, F w) {
    e.w32(a, v.x.bits), e.w32(a + 4, v.y.bits), e.w32(a + 8, v.z.bits), e.w32(a + 12, w.bits);
}

} // namespace

DIFF_TEST_ALL(sparks_burst_update_exact, "sparks: burst 0x19F450 and update 0x19F698 vs sim::exact::Sparks, bit for bit") {
    Ee& e = ctx.ee;
    const sim::Behaviour how = disc_behaviour();
    const F dt = ex::particle_dt(how); // (GT3's 1/60, 0x34FCD8; a later build's at its own rate)
    long bursts = 0, frames = 0, made = 0, kicks = 0, full = 0;
    std::string first;
    const int sequences = std::max(4, ctx.iterations / 50);
    for (int sq = 0; sq < sequences && first.empty(); ++sq) {
        e.reset();
        e.hle_libc_defaults();
        std::string err;
        Manager m = make_manager(e, err);
        if (!err.empty()) return ctx.check(false, err);
        ex::Sparks ours;
        ours.set_rules(how);
        Vu0Random r;
        r.init(ctx.rng.uniform(0.01f, 0.99f));
        e.vr = r.r;
        const u32 pos = e.alloc(16), vel = e.alloc(16), plane = e.alloc(16);
        for (int t = 0; t < 240 && first.empty(); ++t) {
            const int n = ctx.rng.chance(0.25) ? int(ctx.rng.range(1, 3)) : 0;
            for (int b = 0; b < n && first.empty(); ++b) {
                // A plane tilted up to 25 degrees, a point 0.05 above it, a velocity of any direction up to 90 m/s.
                const float tx = ctx.rng.uniform(-0.45f, 0.45f), tz = ctx.rng.uniform(-0.45f, 0.45f);
                const float len = std::sqrt(tx * tx + 1 + tz * tz);
                const ex::Vec3 nrm{F(tx / len), F(1 / len), F(tz / len)};
                const ex::Vec3 p{F(ctx.rng.uniform(-500, 500)), F(ctx.rng.uniform(-20, 80)), F(ctx.rng.uniform(-500, 500))};
                const F d = F(-(nrm.x.host() * p.x.host() + nrm.y.host() * p.y.host() + nrm.z.host() * p.z.host()) + 0.05f);
                const float sp = ctx.rng.uniform(0, 90), a = ctx.rng.uniform(-3.14159f, 3.14159f);
                const ex::Vec3 v{F(sp * std::cos(a)), F(ctx.rng.uniform(-6, 6)), F(sp * std::sin(a))};
                const s32 count = s32(ctx.rng.range(1, 25));
                put_vec(e, pos, p, F(1.0f)), put_vec(e, vel, v, F(0.0f)), put_vec(e, plane, nrm, d);
                full += ours.active() == ex::Sparks::kPool;
                const int before = ours.active();
                e.call(kBurst, {Arg::ptr(m.pm), Arg::ptr(pos), Arg::ptr(vel), Arg::ptr(plane), Arg::integer(count)}, 50'000'000);
                ours.burst(p, v, nrm, d, count, r);
                made += ours.active() - before, ++bursts;
                ctx.sample();
                if (std::string df = difference(e, m, ours, r); !df.empty()) first = fmt("sequence %d frame %d burst", sq, t) + ": " + df;
            }
            if (!first.empty()) break;
            const u32 r0 = r.r;
            e.call(kUpdate, {Arg::ptr(m.pm), Arg::integer(0), Arg::fbits(dt.bits)}, 50'000'000);
            ours.update(dt, r);
            kicks += r.r != r0, ++frames;
            ctx.sample();
            if (std::string df = difference(e, m, ours, r); !df.empty()) first = fmt("sequence %d frame %d update", sq, t) + ": " + df;
        }
    }
    ctx.note(fmt("%.0f bursts (%.0f particles made, ", double(bursts), double(made)) + fmt("%.0f into a full pool), ", double(full)) +
             fmt("%.0f frames of updates (%.0f with kick draws)", double(frames), double(kicks)));
    ctx.check(first.empty(), first.empty() ? "ok" : first);
}

DIFF_TEST_ALL(sparks_emit_exact, "sparks: emission 0x1AADD0 + update on an F090/S race vs sim::exact::ExactRace's, bit for bit") {
    Ee& e = ctx.ee;
    const F dt = ex::particle_dt(disc_behaviour());
    Volume& vol = ctx.disc->volume();
    data::CarDatabase db;
    if (!db.load(vol)) return ctx.check(false, "database: " + db.error());
    const ex::ProgramData& data = car_exact::program_data(*ctx.disc);
    const char* courses[] = {"trialmt", "grandvalley", "rome"};
    // The F090/S (its model ml0003); a disc without its model (2002 Tokyo-Geneva Europe): the Calibra Touring Car (GT3R_SPARK_CAR:
    // another car).
    std::string car, model;
    const Volume::Entry* ce = nullptr;
    for (const char* l : {static_cast<const char*>(std::getenv("GT3R_SPARK_CAR")), "mclaren_mp4_5b", "opel_calibra_race_95"}) {
        const data::Car* c = l ? db.car(l) : nullptr;
        if (!c || !(ce = vol.find("cars/day/" + db.model_of(*c)))) continue;
        car = l, model = db.model_of(*c);
        break;
    }
    if (!ce) return ctx.check(false, "no car with a model (cars/day/)");
    const Bytes car_file = vol.read(*ce);
    const CarFile cf = CarFile::parse(car_file);
    std::vector<ex::SparkPoint> points;
    for (const std::array<u32, 3>& p : car_spark_points(cf.info)) points.push_back({F::raw(p[0]), F::raw(p[1]), F::raw(p[2])});
    if (points.empty()) return ctx.check(false, model + " has no spark points");
    long emitting = 0;
    std::string first, micro_err;
    for (const char* name : courses) {
        if (!first.empty()) break;
        const Volume::Entry* ke = vol.find(course_path(vol, name));
        if (!ke) continue;
        const Bytes course = vol.read(*ke);
        // Ours: a time trial from a moving start, the spark points of the car's model.
        std::string err;
        std::optional<ex::Equipment> eq = race::exact_equipment(db, race::ExactCarSource{car}, &err);
        if (!eq) return ctx.check(false, car + ": " + err);
        ex::RaceSetup s;
        s.data = &data, s.db = &db;
        static const std::vector<u8> kNoEvent(0x200, 0);
        s.event = ByteSpan(kNoEvent.data(), kNoEvent.size());
        s.course = ex::RaceCourse::load(course, data);
        s.kind = ex::Conductor::TimeTrial;
        s.start_type = 3;
        ex::StartEntry en;
        en.eq = *eq, en.human = 1, en.player = true;
        s.entries = {en};
        s.sparks = {points};
        ex::ExactRace race(s);
        // The original's side: the course (for the road query), a car record, its model (GTCI with the points) and a manager.
        e.reset();
        e.hle_libc_defaults();
        install_micro(e, micro_err);
        const u32 container = race_setup::load_course(e, course, err);
        if (!container) return ctx.check(false, std::string(name) + ": " + err);
        const u32 kcourse = e.alloc(0x40);
        e.w32(kcourse, container);
        Manager m = make_manager(e, err);
        if (!err.empty()) return ctx.check(false, err);
        const u32 rec = e.alloc(car_stride() + 0x40, 16), rcm = make_holder(e, cf.info, rec);
        long frames = 0, bursts = 0;
        for (int t = 0; t < 60 * 120 && first.empty(); ++t) {
            // The test's driver on the pad: at the AI line ahead, flat out below a speed that falls with the angle to it.
            const ex::Car& c = race.cars()[0];
            ex::PadFrame f;
            f.flags = 0x1F, f.kind = 2;
            F dx, dy;
            ex::ai_target(c, race.course().ai, c.ai.line ? c.ai.line : 6, c.ai.next, F(12.0f) + F(0.6f) * c.speed, dx, dy);
            const F angle = ee::atan2(c.fwd.x * dy - c.fwd.y * dx, c.fwd.x * dx + c.fwd.y * dy);
            f.steer = s16(std::lround(std::clamp(angle.host() * 2.5f, -1.0f, 1.0f) * 4096.0f));
            if (c.speed * F(3.6f) < F(330.0f) - F(300.0f) * ee::abs(angle)) f.throttle = 4096;
            else f.brake = 2048;
            race.step(std::span<const ex::PadFrame>(&f, 1));
            // The original's emission and update on the car as our step left it, from our step's register.
            car_exact::write_car(e, rec, race.cars()[0]);
            e.w32(rec + 8, kcourse);
            e.vr = race.random_before_sparks().r;
            e.call(kEmit, {Arg::ptr(rcm), Arg::ptr(m.pm), Arg::fbits(0)}, 50'000'000);
            const u32 r_emit = e.vr;
            e.call(kUpdate, {Arg::ptr(m.pm), Arg::integer(0), Arg::fbits(dt.bits)}, 50'000'000);
            ++frames;
            ctx.sample();
            bursts += r_emit != race.random_before_sparks().r;
            if (!micro_err.empty()) first = micro_err;
            else if (std::string df = difference(e, m, race.sparks(), race.random()); !df.empty())
                first = std::string(name) + fmt(" frame %d", t) + ": " + df;
        }
        const int alive = leaves_probe::alive(race.sparks());
        emitting += bursts;
        ctx.note(car + ", " + std::string(name) + fmt(": %.0f frames, %.0f with bursts, %.0f alive at the end", double(frames), double(bursts),
                                         double(alive)));
    }
    ctx.check(emitting > 0, "no burst in the runs");
    ctx.check(first.empty(), first.empty() ? "ok" : first);
}
