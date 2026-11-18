// Track runtime diagnostics.
//
//   track-survey [prefix] [bench]   load every course as a track::Track and check
//                                   grid poses, lap progress along AI line 0,
//                                   probe vs Road::height_at, wall sidedness;
//                                   `bench` adds probe timings.
//   track-probe <vol-path> x z [y_max]   one probe, with the surface decode

#include "tool.h"

#include "formats/course/course_file.h"

#include "track/track.h"

#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <random>

namespace gt3r::tool {

namespace {

struct LapCheck {
    size_t wall_hits = 0; // racing-line samples within 1 m of a wall
    bool one_lap = false;
    float max_backstep = 0; // largest backwards step of race distance (m)
    int laps = 0;
    size_t samples = 0;
};

// Drives the timer along AI line 0 (road-following samples), twice the line
// so a full lap must be recorded once the start line is crossed.
LapCheck drive_ai_line(const track::Track& t) {
    LapCheck r;
    std::vector<sim::Vec3> pts = t.racing_line(-1, 2.0f);
    r.samples = pts.size();
    if (pts.size() < 10) return r;
    track::LapTimer timer(t);
    double time = 0;
    double prev = -1e30;
    bool first = true;
    // Start from the pole slot so the first crossing starts lap 1.
    track::Pose pole = t.grid_pose(0);
    timer.update(pole.position, time);
    // Find the AI sample nearest the pole and drive from there for 1.05 laps.
    size_t start = 0;
    float best = 1e30f;
    for (size_t i = 0; i < pts.size(); ++i) {
        float d = std::hypot(pts[i].x - pole.position.x, pts[i].z - pole.position.z);
        if (d < best) best = d, start = i;
    }
    size_t n = pts.size() + pts.size() / 20;
    for (size_t k = 0; k < n; ++k) {
        const sim::Vec3& p = pts[(start + k) % pts.size()];
        time += 0.05;
        timer.update({p.x, p.y, p.z}, time);
        if (k < pts.size() && t.wall_contact({p.x, p.y + 0.5f, p.z}, 1.0f).hit) {
            ++r.wall_hits;
            if (std::getenv("GT3R_TRACK_WALLS")) {
                track::WallContact w = t.wall_contact({p.x, p.y + 0.5f, p.z}, 1.0f);
                std::printf("  wall %d edge %d pen %.2f at (%.1f %.1f %.1f) s=%.1f\n", w.wall, w.edge, w.penetration,
                            p.x, p.y, p.z, timer.distance());
            }
        }
        double rd = timer.race_distance();
        if (!first && prev - rd > 1.0 && std::getenv("GT3R_TRACK_DEBUG")) {
            const sim::Vec3& q = pts[(start + k + pts.size() - 1) % pts.size()];
            sim::Vec3 dir = t.direction_at(timer.distance());
            std::printf("  backstep %.2f at k=%zu (%.1f %.1f %.1f) s=%.1f step.dir=%.2f lat=%.1f\n", prev - rd, k, p.x,
                        p.y, p.z, timer.distance(), (p.x - q.x) * dir.x + (p.z - q.z) * dir.z,
                        t.locate({p.x, p.y, p.z}, timer.distance()).lateral);
        }
        if (!first) r.max_backstep = std::max(r.max_backstep, float(prev - rd));
        prev = rd;
        first = false;
    }
    r.laps = timer.laps_completed();
    r.one_lap = r.laps == 1;
    return r;
}

// "2 Vls0, 31 -, 32 Env0": the present slots of a file with a course's offset table.
std::string describe_sections(ByteSpan data) {
    std::string out;
    try {
        const course::CourseFile file = course::CourseFile::parse(data);
        for (const course::CourseSection& s : file.sections())
            out += (out.empty() ? "slots " : ", ") + std::to_string(s.slot) + " " + (s.tag.empty() ? "-" : s.tag);
    } catch (const std::exception&) {
    }
    return out.empty() ? "no course offset table" : out;
}

int track_survey(Disc& disc, const Args& a) {
    std::string prefix = !a.empty() && a[0] != "bench" ? a[0] : disc.info().data.course_path("", disc.info().data.default_language());
    bool bench = !a.empty() && a.back() == "bench";
    size_t files = 0, ok_grid = 0, ok_lap = 0, ok_probe = 0, game_index = 0, foreign_lines = 0, not_courses = 0;
    double total_ns = 0;
    size_t total_probes = 0;
    std::printf("%-22s %5s %4s %5s %7s %6s %6s %5s %8s %6s %6s\n", "course", "index", "grid", "on", "minfwd", "laps",
                "backst", "probe", "ns/probe", "walls", "whits");
    for (const auto& e : disc.volume().entries()) {
        if (e.path.rfind(prefix, 0) != 0 || e.path.back() == '/' || e.path.find("/.") != std::string::npos) continue;
        Bytes data = disc.volume().read(e);
        if (!course::CourseFile::looks_like_course(data)) {
            // A file with a course's slot table but no scenery model (leftover data sections).
            std::printf("%s: not a course (%s)\n", e.path.c_str(), describe_sections(data).c_str());
            ++not_courses;
            continue;
        }
        ++files;
        std::unique_ptr<track::Track> t;
        try {
            t = track::Track::from_course_file(data, e.path);
        } catch (const std::exception& ex) {
            std::printf("%s: %s\n", e.path.c_str(), ex.what());
            continue;
        }
        game_index += std::string(t->index_source()) == "game";
        // Grid: on road and facing along the lap.
        int on = 0;
        float min_fwd = 1;
        // Licence variants (L_*) only use slot 0; their other slots are leftovers.
        bool licence = e.path.find("/L_") != std::string::npos;
        int slots = licence ? std::min(1, t->grid_count()) : t->grid_count();
        for (int i = 0; i < slots; ++i) {
            track::Pose p = t->grid_pose(i);
            on += t->probe(p.position.x, p.position.z, p.position.y + 1).hit;
            track::TrackPosition tp = t->locate(p.position);
            min_fwd = std::min(min_fwd, p.forward.x * tp.direction.x + p.forward.z * tp.direction.z);
        }
        bool grid_ok = on == slots && min_fwd > 0.7f;
        ok_grid += grid_ok;
        LapCheck lc = drive_ai_line(*t);
        int resp = 0, resp_on = 0, resp_wall = 0;
        for (float sd = 0; sd < t->length(); sd += 25.0f, ++resp) {
            track::Pose rp = t->centre_pose(sd);
            sim::SurfaceHit rh = t->probe(rp.position.x, rp.position.z, rp.position.y + 0.5f);
            bool good = rh.hit && track::surface_type(rh.surface) != track::Surface::Grass;
            resp_on += good;
            if (!good && std::getenv("GT3R_TRACK_CENTRE_V"))
                std::printf("    miss s=%.0f at (%.1f %.1f %.1f) hit %d surf 0x%x top %.1f\n", sd, rp.position.x,
                            rp.position.y, rp.position.z, rh.hit, rh.surface,
                            t->probe(rp.position.x, rp.position.z, 1e30f).height);
            resp_wall += t->wall_contact(rp.position + sim::Vec3{0, 0.5f, 0}, 1.5f).hit;
        }
        if (std::getenv("GT3R_TRACK_CENTRE"))
            std::printf("  %s centre-line poses %d on road %d near wall %d\n", t->label().c_str(), resp, resp_on, resp_wall);
        if (std::getenv("GT3R_TRACK_AI")) {
            for (const auto& l : t->ai_lines().lines) {
                if (l.points.empty()) continue;
                size_t onp = 0;
                for (const auto& p : l.points) onp += t->probe(p.pos.x, p.pos.z, 1e30f).hit;
                std::printf("  line pts %zu on road %zu\n", l.points.size(), onp);
                break;
            }
            auto rl = t->racing_line(-1, 2.0f);
            size_t onr = 0, under = 0;
            float maxdy = 0;
            for (const auto& p : rl) {
                onr += t->probe(p.x, p.z, p.y + 1).hit;
                sim::SurfaceHit top = t->probe(p.x, p.z, 1e30f);
                if (top.hit && top.height > p.y + 3) ++under, maxdy = std::max(maxdy, top.height - p.y);
            }
            std::printf("  %s racing line %zu on road %zu under-road %zu (max %.1f m)\n", t->label().c_str(), rl.size(), onr, under, maxdy);
        }
        // AI lines that belong to this road have their control points over it. Unfinished
        // courses carry another course's lines: the lap check cannot hold for them.
        size_t ai_points = 0, ai_on_road = 0;
        for (const auto& l : t->ai_lines().lines)
            for (const auto& p : l.points) ++ai_points, ai_on_road += t->probe(p.pos.x, p.pos.z, 1e30f).hit;
        bool foreign_ai = ai_points && ai_on_road * 2 < ai_points;
        foreign_lines += foreign_ai;
        bool lap_ok = lc.one_lap && lc.max_backstep < 1.0f;
        if (!lap_ok && std::getenv("GT3R_TRACK_DEBUG")) {
            const auto& cps = t->checkpoints();
            std::printf("  ai0 points %zu, racing line %zu samples; length %.1f, %zu checkpoints s=[%.1f..%.1f]; "
                        "lines:", t->ai_lines().lines.empty() ? 0 : t->ai_lines().lines[0].points.size(), lc.samples,
                        t->length(), cps.size(), cps.empty() ? 0 : cps.front().distance,
                        cps.empty() ? 0 : cps.back().distance);
            for (const auto& l : t->ai_lines().lines) std::printf(" %zu", l.points.size());
            std::printf("\n");
        }
        ok_lap += lap_ok;
        // Probe vs brute force at random points inside the road bounds.
        std::mt19937 rng(1234);
        float bx0 = 1e30f, bz0 = 1e30f, bx1 = -1e30f, bz1 = -1e30f;
        for (const auto& s : t->road().segments) {
            bx0 = std::min(bx0, s.bounds_min.x), bz0 = std::min(bz0, s.bounds_min.z);
            bx1 = std::max(bx1, s.bounds_max.x), bz1 = std::max(bz1, s.bounds_max.z);
        }
        std::uniform_real_distribution<float> ux(bx0, bx1), uz(bz0, bz1), uy(-50, 300);
        int mismatch = 0;
        const int kSamples = 4000;
        std::vector<std::array<float, 3>> qs;
        for (int i = 0; i < kSamples; ++i) {
            float x = ux(rng), z = uz(rng), y = uy(rng);
            qs.push_back({x, z, y});
            u32 surf = 0;
            auto ref = t->road().height_at(x, z, y, &surf);
            sim::SurfaceHit h = t->probe(x, z, y);
            if (bool(ref) != h.hit || (ref && std::fabs(*ref - h.height) > 1e-3f)) ++mismatch;
        }
        ok_probe += mismatch == 0;
        double ns = 0;
        if (bench) {
            // Probes along the AI line (realistic: mostly hits) plus random ones.
            std::vector<sim::Vec3> pts = t->racing_line(-1, 2.0f);
            for (const auto& p : pts) qs.push_back({p.x, p.z, p.y + 1.0f});
            volatile float sink = 0;
            auto t0 = std::chrono::steady_clock::now();
            const int reps = 20;
            for (int r = 0; r < reps; ++r)
                for (const auto& q : qs) sink = sink + t->probe(q[0], q[1], q[2]).height;
            auto t1 = std::chrono::steady_clock::now();
            ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / double(qs.size() * reps);
            total_ns += ns * double(qs.size());
            total_probes += qs.size();
        }
        std::string name = e.path.substr(e.path.rfind('/') + 1);
        std::printf("%-22s %5s %4d %5d %7.3f %6d %6.2f %5d %8.1f %6zu %6zu%s\n", name.c_str(), t->index_source(),
                    t->grid_count(), on, min_fwd, lc.laps, lc.max_backstep, mismatch, ns, t->walls().size(), lc.wall_hits,
                    grid_ok && lap_ok && mismatch == 0 ? "" : "  <--");
        if (foreign_ai)
            std::printf("    AI lines of another road: %zu of %zu control points over this one\n", ai_on_road, ai_points);
    }
    std::printf("\n%zu files: grid ok %zu, one lap along AI line %zu, probe == height_at %zu, game index %zu\n", files,
                ok_grid, ok_lap, ok_probe, game_index);
    if (foreign_lines) std::printf("%zu with the AI lines of another road\n", foreign_lines);
    if (not_courses) std::printf("%zu not courses\n", not_courses);
    if (bench && total_probes) std::printf("mean probe time %.1f ns\n", total_ns / double(total_probes));
    return 0;
}

int track_probe(Disc& disc, const Args& a) {
    Bytes data = disc.volume().read(need(disc.volume(), a[0]));
    auto t = track::Track::from_course_file(data, a[0]);
    float x = std::stof(a[1]), z = std::stof(a[2]);
    float y = a.size() > 3 ? std::stof(a[3]) : 1e30f;
    sim::SurfaceHit h = t->probe(x, z, y);
    std::printf("hit %d height %.3f normal (%.3f %.3f %.3f) surface 0x%04x (%s, material %u, flags 0x%02x) segment %d\n",
                h.hit, h.height, h.normal.x, h.normal.y, h.normal.z, h.surface,
                track::surface_name(track::surface_type(h.surface)), h.surface & 0xFF, (h.surface >> 8) & 0xFF,
                h.segment);
    track::TrackPosition tp = t->locate({x, h.height, z});
    std::printf("track distance %.1f / %.1f lateral %.2f\n", tp.distance, t->length(), tp.lateral);
    track::WallContact wc = t->wall_contact({x, h.height + 0.5f, z}, 2.0f);
    if (wc.hit) std::printf("wall %d edge %d penetration %.2f\n", wc.wall, wc.edge, wc.penetration);
    return 0;
}

} // namespace

GT3TOOL_COMMAND(track_survey, "track-survey", "[prefix] [bench]", "check track runtime on every course", 0, track_survey);
GT3TOOL_COMMAND(track_probe, "track-probe", "<vol-path> <x> <z> [y_max]", "probe a course surface", 3, track_probe);

} // namespace gt3r::tool
