// The shared two-car race; see race_hits.h.

#include "race_hits.h"

#include "disc/disc.h"
#include "disc/volume.h"
#include "sim/car_spec.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <utility>

namespace gt3r::diff::race_hits {

using namespace gt3r::diff::race_setup;



std::string fmt(const char* f, ...) {
    char b[1200];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}

double env_num(const char* k, double d) {
    const char* v = std::getenv(k);
    return v ? std::atof(v) : d;
}





// The straightest 140 m of the lap with tarmac 4 m to both sides of the centre line.
Lane find_lane(const track::Track& trk) {
    Lane best;
    double best_bend = 1e9;
    const float L = trk.length();
    for (float s = 0; s < L; s += 5) {
        sim::Vec3 d0 = trk.direction_at(s);
        double bend = 0;
        bool ok = true;
        for (float k = -70; k <= 70 && ok; k += 10) {
            sim::Vec3 d = trk.direction_at(std::fmod(s + k + L, L));
            bend = std::max(bend, std::acos(std::clamp(double(d.x * d0.x + d.z * d0.z), -1.0, 1.0)));
            sim::Vec3 p = trk.point_at(std::fmod(s + k + L, L));
            V2 rr{-d.z, d.x};
            for (double off : {-4.0, 4.0}) {
                V2 q = V2{p.x, p.z} + rr * off;
                sim::SurfaceHit h = trk.probe(float(q.x), float(q.z), p.y + 3.0f);
                if (!h.hit || track::surface_type(h.surface) != track::Surface::Tarmac || std::fabs(h.height - p.y) > 1.0f) ok = false;
            }
        }
        if (!ok || bend >= best_bend) continue;
        best_bend = bend;
        sim::Vec3 p = trk.point_at(s);
        best.m = {p.x, p.z};
        best.t = V2{d0.x, d0.z} * (1.0 / std::hypot(d0.x, d0.z));
        best.r = {-best.t.z, best.t.x};
        best.y = p.y;
        best.distance = s;
        best.bend = bend;
        best.ok = true;
    }
    return best;
}

// A straight wall: every point of its line within 50 m along it within 0.3 m of the edge's line, the wall reaching 45 m both
// ways, tarmac 0.5 m and 4 m inside it, parallel to the lap direction within 6 degrees; the straightest and flattest. The lane line
// runs `offset` m inside the wall.
Lane find_wall_lane(const track::Track& trk, double offset) {
    Lane best;
    double best_score = 1e9;
    const auto& lines = trk.wall_lines();
    for (const auto& wl : lines) {
        const size_t n = wl.points.size(), edges = wl.closed ? n : (n ? n - 1 : 0);
        for (size_t e = 0; e < edges; ++e) {
            const auto &pa = wl.points[e], &pb = wl.points[(e + 1) % n];
            if (pa.flags & track::kWallSensor) continue;
            V2 a{pa.pos.x, pa.pos.z}, b{pb.pos.x, pb.pos.z}, d = b - a;
            const double len = d.len();
            if (len < 1.0) continue;
            V2 t = d * (1.0 / len), nn{t.z, -t.x}; // track on the left of a->b: n into the track
            V2 mid = (a + b) * 0.5;
            double dev = 0, ext_f = 0, ext_b = 0;
            for (const auto& wp : wl.points) {
                V2 q{wp.pos.x, wp.pos.z};
                double along = (q - mid).dot(t);
                if (std::fabs(along) > 50 || (q - mid).len() > 55) continue;
                dev = std::max(dev, std::fabs((q - mid).dot(nn)));
                ext_f = std::max(ext_f, along);
                ext_b = std::min(ext_b, along);
            }
            if (dev > 0.3 || ext_f < 45 || ext_b > -45) continue;
            bool road = true;
            double y = 0, y0 = 0, y1 = 0;
            for (double off : {0.5, 4.0}) {
                V2 q = mid + nn * off;
                sim::SurfaceHit h = trk.probe(float(q.x), float(q.z), pa.pos.y + 5.0f);
                if (!h.hit || track::surface_type(h.surface) != track::Surface::Tarmac) road = false;
                if (off > 1) y = h.height;
            }
            for (double k : {-40.0, 40.0}) {
                V2 q = mid + nn * 2.0 + t * k;
                sim::SurfaceHit h = trk.probe(float(q.x), float(q.z), pa.pos.y + 10.0f);
                if (!h.hit || track::surface_type(h.surface) != track::Surface::Tarmac) road = false;
                (k < 0 ? y0 : y1) = h.height;
            }
            if (!road) continue;
            V2 c = mid + nn * 3.0;
            track::TrackPosition tp = trk.locate({float(c.x), float(y), float(c.z)});
            V2 dir{tp.direction.x, tp.direction.z};
            const double cosang = dir.dot(t);
            if (std::fabs(cosang) < std::cos(6 * kPi / 180)) continue;
            const double score = dev + 2 * std::fabs(y1 - y0) / 80;
            if (score >= best_score) continue;
            best_score = score;
            best.t = cosang < 0 ? t * -1.0 : t; // travel along the lap
            best.r = {-best.t.z, best.t.x};
            best.side = nn.dot(best.r) < 0 ? 1.0 : -1.0; // the wall is on the right when the normal into the track points left
            best.m = mid + nn * offset;
            best.wall_d = offset;
            best.y = y;
            best.distance = tp.distance;
            best.bend = dev;
            best.ok = true;
        }
    }
    return best;
}



// Octagon (spec §5.3) in the car frame: x lateral, y forward.
std::vector<V2> octagon(const Extents& e) {
    double a = e.front, b = e.rear, h = e.half_w, d = 0.4 * e.half_w;
    return {{h, a - d}, {h - d, a}, {-h + d, a}, {-h, a - d}, {-h, -b + d}, {-h + d, -b}, {h - d, -b}, {h, -b + d}};
}





// Start poses (target = car 0, striker = car 1). The cars first run kSettle ticks apart (the placement drops them onto the road),
// so the start is kSettle ticks of straight-line travel back from a pose just short of contact, which is at the lane point m.
void start_poses(const Lane& ln, const Extents& ext, const Scenario& sc, Pose& tgt, Pose& stk) {
    tgt.fwd = ln.t;
    tgt.v = sc.v_target / 3.6;
    stk.v = sc.v_striker / 3.6;
    const double w = 2 * ext.half_w, lat = sc.offset * w * (sc.wall ? ln.side : 1.0);
    const double th = sc.deg * kPi / 180 * (lat > 0 ? -1.0 : 1.0); // towards the target's centre line (+ = to the right)
    stk.fwd = ln.t * std::cos(th) + ln.r * std::sin(th);
    V2 right{-stk.fwd.z, stk.fwd.x};
    V2 rel; // the striker's CoG relative to the target's just before contact
    if (!std::isnan(sc.along)) { // side by side: the striker's outline 0.3 m from the target's side
        const V2 in = ln.r * (lat > 0 ? -1.0 : 1.0);
        double reach = 0; // how far the striker's outline reaches towards the target
        for (const V2& p : octagon(ext)) reach = std::max(reach, (right * p.x + stk.fwd * p.z).dot(in));
        rel = ln.t * sc.along - in * (ext.half_w + 0.3 + reach);
    } else { // behind: the striker's front-most point 0.3 m behind the target's tail
        double reach = 0;
        for (const V2& p : octagon(ext)) reach = std::max(reach, (right * p.x + stk.fwd * p.z).dot(ln.t));
        rel = ln.t * -(ext.rear + 0.3 + reach) + ln.r * lat;
    }
    const double T = (kSettle - 5) * kDt;
    tgt.cog = ln.m - ln.t * (tgt.v * T);
    stk.cog = ln.m + rel - stk.fwd * (stk.v * T);
}





void fill(CarRow& r, const Lane& ln, double x, double z, V2 f, V2 v, double yaw_y) {
    r.x = x;
    r.z = z;
    r.vt = v.dot(ln.t);
    r.vr = v.dot(ln.r);
    r.speed = v.len() * 3.6;
    r.hdg = std::atan2(f.dot(ln.r), f.dot(ln.t));
    // yaw about +Y turns the forward vector by (f.z, -f.x) per rad: + about +Y = to the left in this frame when right = (-t.z, t.x)
    V2 df{f.z, -f.x};
    r.yaw = df.dot(ln.r) > 0 ? yaw_y : -yaw_y;
}

// The outline's distance from the wall line of a wall lane (CoG (x, z), forward f).
double wall_gap(const Lane& ln, const Extents& ext, double x, double z, V2 f) {
    if (ln.wall_d <= 0) return 1e9;
    const double fl = f.len();
    if (fl > 0) f = f * (1.0 / fl);
    V2 right{-f.z, f.x}, to_wall = ln.r * ln.side;
    double g = 1e9;
    for (const V2& p : octagon(ext)) {
        V2 w = V2{x, z} + right * p.x + f * p.z;
        g = std::min(g, ln.wall_d - (w - ln.m).dot(to_wall));
    }
    return g;
}

// ---- original ---------------------------------------------------------------------------------------------------------------
u8 g_frame[2][16];
u32 g_car[2], g_body[2];
const Scenario* g_scenario = nullptr;
int g_tick = 0;
int g_responses = 0, g_wall[2] = {0, 0};
// Called at the collision step's entry and exit (probes) and after each tick (false ends the run early).
std::function<void(Ee&, bool)> g_step_probe;
std::function<bool(Ee&, int)> g_after_tick;
std::function<void(Ee&)> g_install; // more probes, after the race is set up
bool g_race_display = false;

Run run_original(Ee& ee, const data::CarDatabase& db, const Bytes& gtar, const data::Event& ev, ByteSpan course_file,
                 const std::string& car, const track::Track& trk, const Lane& ln, const Scenario& sc, int ticks, Extents& ext_out) {
    Run run;
    OrigRace race;
    std::string err;
    if (!setup_original_race(ee, db, gtar, ev, course_file, {car, car}, 99, 0, race, err)) {
        run.error = err;
        return run;
    }
    if (!g_race_display) {
        ee.hle(pa(0x001EC520), [](Ee&) {}, "race display time event (ignored)");
        ee.hle(pa(0x001EC620), [](Ee&) {}, "race display lap event (ignored)");
    }
    const double th[2] = {sc.throttle_target, sc.throttle_striker};
    for (int i = 0; i < 2; ++i) {
        std::memset(g_frame[i], 0, 16);
        g_frame[i][0] = 0x1F;
        g_frame[i][1] = 2;
        u16 t = u16(std::lround(std::clamp(th[i], 0.0, 1.0) * 4096));
        std::memcpy(g_frame[i] + 8, &t, 2);
        g_car[i] = car_addr(race.cond, i);
    }
    g_scenario = &sc;
    ee.hle(pa(0x00210740),
           [](Ee& e) {
               const int i = e.g32(17) == g_car[1] ? 1 : 0; // the caller 0x1E4D38's $s1 is the car (a0 is its input device)
               u8 f[16];
               std::memcpy(f, g_frame[i], 16);
               if (g_scenario->pad) {
                   std::memset(f, 0, 16);
                   g_scenario->pad(i, g_tick, f);
               }
               e.write_bytes(u32(e.a(1)), ByteSpan(f, 16));
           },
           "pad read (a frame per car)");
    ee.probe(kPairResponse, [](Ee&) { ++g_responses; });
    for (int i = 0; i < 2; ++i) g_body[i] = body_addr(race.cond, i);
    auto wall_probe = [](Ee& e) { ++g_wall[u32(e.a(0)) == g_body[1] ? 1 : 0]; };
    ee.probe(pa(0x001FDEC0), wall_probe); // point-wall impulse
    ee.probe(pa(0x001FE3A0), wall_probe); // wall-corner impulse
    if (g_install) g_install(ee);
    if (g_step_probe) {
        ee.probe(pa(0x001FA8F8), [](Ee& e) { g_step_probe(e, true); });  // all-bodies step: entry
        ee.probe(pa(0x001FAC88), [](Ee& e) { g_step_probe(e, false); }); // its epilogue
    }
    const u32 C0 = car_addr(race.cond, 0);
    Extents ext{ee.rf(C0 + 0x10), ee.rf(C0 + 0x14), 0.5 * ee.rf(C0 + 0x18)};
    ext_out = ext;
    Pose pose[2];
    start_poses(ln, ext, sc, pose[0], pose[1]);
    try {
        for (int i = 0; i < 2; ++i) {
            const u32 C = car_addr(race.cond, i), F = C + kF;
            double gx = ee.rf(F + 0), gy = ee.rf(F + 4);
            sim::SurfaceHit gg = trk.probe(float(gx), float(-gy), ee.rf(F + 8) + 3.0f);
            const double z_static = ee.rf(F + 8) - (gg.hit ? gg.height : 0.0f);
            const Pose& p = pose[i];
            sim::SurfaceHit h = trk.probe(float(p.cog.x), float(p.cog.z), float(ln.y + 30.0));
            double heading = std::atan2(-p.fwd.x, -p.fwd.z); // forward = (-sin c, -cos c) in GTRW x/z
            ee.w8(F + f_off(0x3E9), u8(sc.control)); // control source (0: pad)
            ee.w8(F + f_off(0x534), u8(sc.kind));    // driver kind (0)
            ee.wf(F + 0, float(p.cog.x));
            ee.wf(F + 4, float(-p.cog.z));
            ee.wf(F + 8, float((h.hit ? h.height : ln.y) + z_static));
            ee.wf(F + f_off(0x468), 0.0f);
            ee.wf(F + f_off(0x46C), 0.0f);
            ee.wf(F + f_off(0x470), float(heading));
            ee.wf(F + f_off(0x41C), 0.0f);
            ee.wf(F + f_off(0x424), 0.0f);
            ee.call(pa(0x1D8A80), {Arg::ptr(F + 0x0C), Arg::ptr(F + 0x18), Arg::ptr(F + 0x24), Arg::flt(0), Arg::flt(0), Arg::flt(float(heading))});
            ee.call(pa(0x1E9DE8), {Arg::ptr(C)});
            ee.call(pa(0x1E9E18), {Arg::ptr(F)});
            ee.call(pa(0x1E8B68), {Arg::ptr(C)}, 50'000'000);
            ee.call(pa(0x1EA4D8), {Arg::ptr(C)});
            ee.call(pa(0x1DD8A8), {Arg::ptr(C), Arg::flt(float(p.v))}, 20'000'000);
            if (sc.all_ascc) { // its own aid block (car + 0xE8C, physics §4.6): gains A, D, C, E, TCS lateral a / b, all flags
                const u32 A = C + 0xE8C;
                const std::pair<u32, float> words[] = {{0x00, 0.4f}, {0x04, 0.05f}, {0x08, 0.3f}, {0x10, 1.0f},
                                                       {0x14, 0.1f}, {0x18, 0.6f}, {0x24, 1.0f}, {0x28, 0.15f}};
                for (const auto& [off, val] : words) ee.wf(A + off, val);
                for (u32 b : {0x0Cu, 0x0Du, 0x0Eu, 0x0Fu, 0x1Cu, 0x1Du, 0x1Eu, 0x1Fu, 0x30u, 0x31u, 0x32u, 0x33u})
                    ee.w8(A + b, 1);
            }
            if (sc.prep) sc.prep(ee, C, i, -1);
        }
        auto set_frames = [&] {
            for (int i = 0; i < 2; ++i) {
                const u16 t = u16(std::lround(std::clamp(sc.throttle, 0.0, 1.0) * 4096));
                const u16 b = u16(std::lround(std::clamp(sc.brake, 0.0, 1.0) * 4096));
                const u16 h = u16(std::lround(std::clamp(sc.handbrake, 0.0, 1.0) * 4096));
                const s16 s = s16(std::lround(std::clamp(sc.steer, -1.0, 1.0) * 4096));
                std::memcpy(g_frame[i] + 4, &s, 2);
                std::memcpy(g_frame[i] + 8, &t, 2);
                std::memcpy(g_frame[i] + 0xA, &b, 2);
                std::memcpy(g_frame[i] + 0xE, &h, 2);
            }
        };
        run.init = fmt("orig: extents front %.3f rear %.3f width %.3f, mass %.0f", ext.front, ext.rear, 2 * ext.half_w,
                       1.0 / ee.rf(body_addr(race.cond, 0)));
        for (int k = 0; k < (sc.ticks ? sc.ticks : ticks); ++k) {
            g_tick = k;
            if (k == sc.from) set_frames();
            if (sc.prep)
                for (int i = 0; i < 2; ++i) sc.prep(ee, car_addr(race.cond, i), i, k);
            g_responses = 0;
            g_wall[0] = g_wall[1] = 0;
            ee.call(kCondStep, {Arg::ptr(race.cond), Arg::integer(0)}, 800'000'000);
            Row r;
            for (int i = 0; i < 2; ++i) {
                const u32 C = car_addr(race.cond, i), F = C + kF, B = body_addr(race.cond, i);
                double x = ee.rf(F + 0), z = -double(ee.rf(F + 4)), c = ee.rf(F + f_off(0x470));
                fill(r.c[i], ln, x, z, {-std::sin(c), -std::cos(c)}, {ee.rf(F + f_off(0x44C)), -double(ee.rf(F + f_off(0x450)))}, ee.rf(F + f_off(0x47C)));
                r.c[i].dt = ee.rf(C + 0xEC0);
                for (u32 w = 0; w < 4; ++w) r.c[i].fz += ee.rf(C + 0xFE4 + 0xA4 * w + 0x08);
                r.c[i].pitch = ee.rf(F + f_off(0x468));
                r.c[i].roll = ee.rf(F + f_off(0x46C));
                r.c[i].wgap = wall_gap(ln, ext, x, z, {-std::sin(c), -std::cos(c)});
                r.c[i].wall_hits = g_wall[i];
                for (u32 p = 0; p < 8; ++p) r.flagged += (ee.r32(B + 0x28 + 0x44 * p) >> 1) & 1;
            }
            r.contacts = g_responses;
            run.rows.push_back(r);
            if (g_after_tick && !g_after_tick(ee, k)) break;
        }
    } catch (const EeTrap& t) {
        run.error = fmt("trap at pc=%08X: %s", t.pc, t.what());
        return run;
    }
    run.ok = true;
    return run;
}

// ---- ours -------------------------------------------------------------------------------------------------------------------
// The two-car contact scenarios.
const std::vector<Scenario>& car_hit_scenarios() {
    static const double nan = std::nan("");
    static const std::vector<Scenario> scenarios = {
        {"rear-end, full", 0.0, 0, 80, 120, nan, 0.555, 0.555},
        {"rear-end, half overlap", 0.5, 0, 80, 120, nan, 0.555, 0.555},
        {"rear-end, corner", 0.85, 0, 80, 120, nan, 0.555, 0.555},
        {"rear-end, corner, 6 deg in", 0.85, 6, 80, 120, nan, 0.555, 0.555},
        {"rear-end, corner, slow", 0.85, 0, 80, 100, nan, 0.555, 0.555},
        {"rear-end, corner, fast", 0.85, 0, 60, 140, nan, 0.555, 0.555},
        {"push, corner (both full throttle)", 0.85, 0, 90, 105, nan, 1.0, 1.0},
        {"side swipe, 4 deg", 1.0, 4, 100, 100, 0.0, 0.555, 0.555},
        {"side swipe, 8 deg, striker half a car ahead", 1.0, 8, 100, 100, 2.0, 0.555, 0.555},
        {"side swipe, 8 deg, striker half a car behind", 1.0, 8, 100, 100, -2.0, 0.555, 0.555},
        // on the wall lane (the target 0.4 m from a straight wall; + offsets towards the wall)
        {"wall: side swipe into the wall, 4 deg", -1.0, 4, 100, 100, 0.0, 0.555, 0.555, true},
        {"wall: side swipe into the wall, 8 deg, half a car ahead", -1.0, 8, 100, 100, 2.0, 0.555, 0.555, true},
        {"wall: side swipe into the wall, 8 deg, half a car behind", -1.0, 8, 100, 100, -2.0, 0.555, 0.555, true},
        {"wall: rear-end, inner corner", -0.85, 0, 80, 120, nan, 0.555, 0.555, true},
        {"wall: rear-end, inner corner, 6 deg out", -0.85, -6, 80, 120, nan, 0.555, 0.555, true},
        {"wall: rear-end, full", 0.0, 0, 80, 120, nan, 0.555, 0.555, true},
        {"wall: rear-end, inner corner, fast", -0.85, 0, 60, 140, nan, 0.555, 0.555, true},
    };
    return scenarios;
}

// race_tick_exact's: the cars side by side (the striker 20 m behind in the next lane), the pad inputs from tick `from`.
const std::vector<Scenario>& tick_scenarios() {
    auto pad = [](const char* name, double kmh, int from, double throttle, double brake, double steer, double handbrake) {
        return Scenario{.name = name, .offset = 1.0, .deg = 0, .v_target = kmh, .v_striker = kmh, .along = -20,
                        .throttle_target = 0, .throttle_striker = 0, .from = from, .throttle = throttle, .brake = brake,
                        .steer = steer, .handbrake = handbrake};
    };
    static const std::vector<Scenario> scenarios = [&] {
        std::vector<Scenario> s = {
            pad("brake hard from 120 km/h", 120, 0, 0, 1, 0, 0),
            pad("brake and turn from 100 km/h", 100, 10, 0, 0.6, 0.5, 0),
            pad("lift-off turn at 100 km/h", 100, 0, 0, 0, 1, 0),
            pad("power turn at 60 km/h", 60, 0, 1, 0, 0.7, 0),
            pad("full throttle from 20 km/h", 20, 0, 1, 0, 0, 0),
            pad("handbrake turn at 80 km/h", 80, 0, 0, 0, 1, 1),
        };
        Scenario a = pad("every ASCC channel: lift-off turn at 100 km/h", 100, 0, 0, 0, 1, 0);
        a.all_ascc = true;
        s.push_back(a);
        a = pad("every ASCC channel: brake and turn from 100 km/h", 100, 10, 0, 0.6, 0.6, 0);
        a.all_ascc = true;
        s.push_back(a);
        a = pad("every ASCC channel: power turn at 60 km/h", 60, 0, 1, 0, 0.7, 0);
        a.all_ascc = true;
        s.push_back(a);
        a = pad("AI driving (control source 1)", 100, -1, 0, 0, 0, 0);
        a.control = 1;
        s.push_back(a);
        a = pad("pad car with driver kind 1 (the AI drives)", 100, -1, 0, 0, 0, 0);
        a.kind = 1;
        s.push_back(a);
        return s;
    }();
    return scenarios;
}

// ---- pad scenarios -----------------------------------------------------------------------------------------------------------
namespace {

void put_frame(u8* f, u8 flags, u8 kind, int shift, int steer, int th, int br, int back, int third) {
    f[0] = flags, f[1] = kind, f[2] = u8(s8(shift));
    auto put = [&](int off, int v) {
        const u16 x = u16(v);
        std::memcpy(f + off, &x, 2);
    };
    put(4, steer), put(8, th), put(0xA, br), put(0xC, back), put(0xE, third);
}
// amp * full lock * sin, held at full lock where |amp sin| > 1.
int sweep(int t, int period, double amp) {
    return int(std::clamp(std::lround(amp * 4096 * std::sin(2 * kPi * t / period)), -4096L, 4096L));
}
int window(int t, int from, int to, int v) { return t >= from && t < to ? v : 0; }

// Analog sweeps: car 0 a DualShock (kind 2 while the stick is centred in the first two seconds, as the licence demos), car 1 the
// table-mapped kind 2; past full lock, centred windows, small values, throttle steps, brake, third axis, back axis, shifts.
void pad_analog(int car, int t, u8* f) {
    int steer = sweep(t + 37 * car, 110, 1.6);
    if ((t / 45) % 4 == 3) steer = 0;
    if (t >= 60 && t < 70) steer = car ? 7 : -9;
    const int th[4] = {4096, 2300, 0, 4095};
    const int kind = car ? 2 : (t < 120 && steer == 0 ? 2 : 3);
    put_frame(f, 0x1F, u8(kind), t == 100 ? 1 : t == 120 ? -1 : 0, steer, th[(t / 35) % 4], (t / 50) % 5 == 4 ? 2600 + 300 * car : 0,
              window(t, 200, 215, 900) + window(t, 215, 230, 4096), window(t, 150, 165, 3900) + window(t, 165, 175, 800));
}
// Digital buttons (car 0, kind 0) and mixed axes (car 1: analog steer, brake and third axis; button throttle and back axis;
// kinds 3, 1 and 0, the last two leaving the steering as it is).
void pad_digital(int car, int t, u8* f) {
    if (car == 0) {
        const int steer[7] = {1, 1, 0, -1, -1, 0, 1};
        put_frame(f, 0, 0, t == 60 ? 1 : t == 90 ? -1 : 0, steer[(t / 18) % 7], (t / 30) % 3 != 2, (t / 40) % 4 == 3, window(t, 180, 205, 1),
                  window(t, 120, 128, 1));
        if (t >= 215) f[0] = 0xA, f[8] = f[9] = 0; // analog throttle and back axis at 0 after the button: the button's ramp runs down
        return;
    }
    const u8 kinds[4] = {3, 1, 0, 3};
    put_frame(f, 0x15, kinds[(t / 60) % 4], 0, sweep(t, 90, 1.2), (t / 25) % 3 != 0, (t / 50) % 4 == 2 ? 4096 : 0, window(t, 150, 165, 1),
              window(t, 100, 110, 4096));
}
// A steering wheel (kind 4) with analog pedals and third axis.
void pad_wheel(int car, int t, u8* f) {
    put_frame(f, 0x1F, 4, 0, sweep(t + 20 * car, 120, 1.3), (t % 80) * 60, window(t, 130, 160, 2000 + 2000 * car), window(t, 200, 210, 4096),
              window(t, 100, 115, 3500));
}
// The same, car 1 with button steering.
void pad_wheel_buttons(int car, int t, u8* f) {
    pad_wheel(car, t, f);
    if (car == 1) {
        const s16 steer[3] = {0, 1, -1};
        f[0] = 0x1E;
        std::memcpy(f + 4, &steer[(t / 30) % 3], 2);
    }
}
// Pedal styles: car 0 analog, car 1 with button throttle and back axis; the brake to full and part way (the clutch with style 3),
// the back axis above and below the throttle, shifts (style 6), the third axis.
void pad_styles(int car, int t, u8* f) {
    const bool digital = car == 1;
    const int th = (t / 40) % 3 != 2 ? (digital ? 1 : 3000) : 0;
    const int back = digital ? window(t, 120, 150, 1) : window(t, 120, 140, 3500) + window(t, 140, 150, 1000);
    put_frame(f, digital ? 0x15 : 0x1F, 3, t == 130 ? 1 : t == 160 ? -1 : 0, sweep(t, 140, 0.8), th, window(t, 60, 80, 4096) + window(t, 80, 100, 1500),
              back, window(t, 170, 185, 4000));
}
// Braking to a stop, then the back axis (the reverse request) and steering while reversing: car 0 analog, car 1 buttons.
void pad_reverse(int car, int t, u8* f) {
    const bool rev = t >= 150;
    if (car == 0)
        put_frame(f, 0x1F, 3, 0, t >= 200 ? sweep(t, 100, 1.5) : 0, t < 30 ? 2000 : 0, t >= 30 && t < 150 ? 4096 : 0, rev ? 4096 : 0, 0);
    else
        put_frame(f, 0, 0, 0, t >= 200 ? ((t / 25) % 2 ? 1 : -1) : 0, t < 30, t >= 30 && t < 150, rev, 0);
}

template <int A, int B> void prep_wheel(Ee& ee, u32 car, int i, int tick) {
    if (tick < 0) ee.w8(car + car_off(0x136F), u8(s8(i ? B : A))); // steering-wheel type
}
template <int A, int B> void prep_style(Ee& ee, u32 car, int i, int tick) {
    if (tick < 0) ee.w8(car + 0xF84 + f_off(0x461), u8(i ? B : A)); // pedal style
}
// The hold counter with driver kinds: car 0 parked (kind 8) for 50 ticks of hold, car 1 the AI (kind 1) after 130, both with
// pedal style 3 (which a driver kind other than 0 ignores); the race hold (race info +0x5C) for the first 20 ticks.
void prep_hold(Ee& ee, u32 car, int i, int tick) {
    if (tick < 0) {
        ee.w16(car + 0xF84 + f_off(0x536), u16(i ? 130 : 50));
        ee.w8(car + 0xF84 + f_off(0x534), u8(i ? 1 : 8));
        ee.w8(car + 0xF84 + f_off(0x461), 3);
    }
    if (i == 0 && tick >= 0 && tick <= 20) ee.w32(ee.r32(ee.r32(car + 4)) + 0x5C, tick < 20 ? 1u : 0u);
}

} // namespace

void pad_race_frame(int t, u8* f) {
    if ((t / 240) % 2) pad_wheel(0, t % 240, f);
    else pad_analog(0, t % 240, f);
}

const std::vector<Scenario>& pad_scenarios() {
    static const double nan = std::nan("");
    auto pad = [](const char* name, double v0, double v1, void (*fr)(int, int, u8*), void (*prep)(Ee&, u32, int, int) = nullptr, int ticks = 0) {
        Scenario s{name, 1.5, 0, v0, v1, nan, 0, 0};
        s.pad = fr, s.prep = prep, s.ticks = ticks;
        return s;
    };
    static const std::vector<Scenario> scenarios = {
        pad("pad: analog sweeps (DualShock, table-mapped)", 80, 90, pad_analog),
        pad("pad: buttons, mixed axes", 70, 100, pad_digital),
        pad("pad: steering wheel, types -1 / 0", 60, 90, pad_wheel, prep_wheel<-1, 0>),
        pad("pad: steering wheel, types 1 / 2", 60, 90, pad_wheel, prep_wheel<1, 2>),
        pad("pad: steering wheel, types 3 / 4", 60, 90, pad_wheel, prep_wheel<3, 4>),
        pad("pad: steering wheel, type 5 / buttons", 60, 90, pad_wheel_buttons, prep_wheel<5, 1>),
        pad("pad: pedal styles 3 / 5", 70, 70, pad_styles, prep_style<3, 5>),
        pad("pad: pedal styles 6 / 1", 70, 70, pad_styles, prep_style<6, 1>),
        pad("pad: pedal styles 2 / 4", 70, 70, pad_styles, prep_style<2, 4>),
        pad("pad: pedal styles 7 / 6", 70, 70, pad_styles, prep_style<7, 6>),
        pad("pad: brake to a stop, reverse", 40, 40, pad_reverse, nullptr, 420),
        pad("pad: hold counter, parked and AI kinds, race hold", 60, 60, pad_analog, prep_hold),
    };
    return scenarios;
}

bool load_hit_setup(DiffContext& ctx, HitSetup& h) {
    h.car = std::getenv("GT3R_CARHIT_CAR") ? std::getenv("GT3R_CARHIT_CAR") : "r34_gtr_v_99";
    Volume& vol = ctx.disc->volume();
    if (!h.db.load(vol)) {
        ctx.check(false, "database load: " + h.db.error());
        return false;
    }
    const Volume::Entry* dbe = vol.find(db_path(vol));
    if (!dbe) {
        ctx.check(false, db_path(ctx.disc->volume()) + " not found");
        return false;
    }
    h.gtar = vol.read(*dbe);
    if (const char* e = std::getenv("GT3R_CARHIT_EVENT")) h.ev = h.db.event(e);
    else
        for (const data::Event& e : h.db.events())
            if (e.course.rfind("trialmt", 0) == 0 && e.label[0] == 'G' && !e.entries.empty()) {
                h.ev = &e;
                break;
            }
    // Another game's database (a later build's without GT3's events): any event with entries whose course file is there,
    // one at Trial Mountain first; and a car of its own when the default is not in the database.
    if (!h.ev && !std::getenv("GT3R_CARHIT_EVENT"))
        for (int pass = 0; pass < 2 && !h.ev; ++pass)
            for (const data::Event& e : h.db.events())
                if (!e.entries.empty() && (pass == 1 || e.course.rfind("trialmt", 0) == 0) && vol.find(course_path(vol, e.course))) {
                    h.ev = &e;
                    break;
                }
    if (!h.ev) {
        ctx.check(false, "no event");
        return false;
    }
    if (!std::getenv("GT3R_CARHIT_CAR") && !h.db.car(h.car)) {
        for (const std::string& l : h.ev->entries)
            if (h.db.car(l) || h.db.variant(l)) {
                h.car = l;
                break;
            }
        if (!h.db.car(h.car) && !h.db.variant(h.car) && !h.db.cars().empty()) h.car = h.db.cars().front().label;
    }
    const Volume::Entry* ce = vol.find(course_path(vol, h.ev->course));
    if (!ce) {
        ctx.check(false, "course " + h.ev->course + " not found");
        return false;
    }
    h.course_file = vol.read(*ce);
    h.trk = track::Track::from_course_file(h.course_file, h.ev->course);
    h.ln = find_lane(*h.trk);
    std::optional<sim::CarSpec> spec = sim::build_car_spec(h.db, h.car);
    h.wl = find_wall_lane(*h.trk, (spec ? 0.5 * spec->width : 0.9) + 0.4);
    return true;
}

} // namespace gt3r::diff::race_hits
