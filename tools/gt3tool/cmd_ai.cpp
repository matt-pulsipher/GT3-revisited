// AI line diagnostics (ai workstream).
//
//   ai-survey [prefix]   per course and AI line: point count, lap coverage,
//                        share of points on the pit lane, mean lateral offset
//                        from line 5, and histograms of the per-point flag
//                        bytes.

#include "tool.h"

#include "track/track.h"

#include <cmath>
#include <cstdio>
#include <map>

namespace gt3r::tool {

namespace {

int ai_survey(Disc& disc, const Args& a) {
    std::string prefix = !a.empty() ? a[0] : disc.info().data.course_path("", disc.info().data.default_language());
    std::map<int, size_t> hist[4], kinds_by_radius[2], hi16;
    size_t files = 0;
    for (const auto& e : disc.volume().entries()) {
        if (e.path.rfind(prefix, 0) != 0 || e.path.back() == '/') continue;
        Bytes data = disc.volume().read(e);
        std::unique_ptr<track::Track> t;
        try {
            t = track::Track::from_course_file(data, e.path);
        } catch (const std::exception& ex) {
            std::printf("%s: %s\n", e.path.c_str(), ex.what());
            continue;
        }
        ++files;
        std::string name = e.path.substr(e.path.rfind('/') + 1);
        std::printf("%-20s len %6.0f", name.c_str(), t->length());
        const auto& lines = t->ai_lines().lines;
        const course::AiLine* ref = lines.size() > 5 && !lines[5].points.empty() ? &lines[5] : nullptr;
        for (size_t li = 0; li < lines.size(); ++li) {
            const auto& l = lines[li];
            if (l.points.empty()) {
                std::printf(" | %zu: -", li);
                continue;
            }
            // Coverage: forward progress summed over consecutive points.
            float cov = 0, prev = -1, lat = 0;
            size_t pit = 0;
            for (const auto& p : l.points) {
                sim::Vec3 w{p.pos.x, 0, p.pos.z};
                sim::SurfaceHit h = t->probe(w.x, w.z, 1e30f);
                w.y = h.height;
                if (h.hit && (track::surface_flags(h.surface) & track::kSurfacePitLane)) ++pit;
                track::TrackPosition tp = t->locate(w, prev);
                if (prev >= 0) {
                    float d = tp.distance - prev;
                    if (d < -0.5f * t->length()) d += t->length();
                    if (d > 0 && d < 0.5f * t->length()) cov += d;
                }
                prev = tp.distance;
                lat += tp.lateral;
                for (int b = 0; b < 4; ++b) ++hist[b][int(s8(p.flags >> (8 * b)))];
                ++kinds_by_radius[p.radius() != 0][int(p.flags & 0xFF)];
                ++hi16[int(p.packed >> 16)];
            }
            (void)ref;
            std::printf(" | %zu: %2zu pts cov %3.0f%% pit %2zu lat %+5.1f", li, l.points.size(),
                        100.0f * cov / t->length(), pit, lat / float(l.points.size()));
        }
        std::printf("\n");
    }
    std::printf("\n%zu files\n", files);
    const char* names[4] = {"byte0 (kind)", "byte1", "byte2", "byte3"};
    for (int b = 0; b < 4; ++b) {
        std::printf("%s:", names[b]);
        for (auto [v, n] : hist[b]) std::printf(" %d:%zu", v, n);
        std::printf("\n");
    }
    for (int r = 0; r < 2; ++r) {
        std::printf("kind when radius %s:", r ? "!= 0" : "== 0");
        for (auto [v, n] : kinds_by_radius[r]) std::printf(" %d:%zu", v, n);
        std::printf("\n");
    }
    std::printf("packed high 16 bits:");
    for (auto [v, n] : hi16) std::printf(" %d:%zu", v, n);
    std::printf("\n");
    return 0;
}

} // namespace

GT3TOOL_COMMAND(ai_survey, "ai-survey", "[prefix]", "AI line roles and per-point flag statistics", 0, ai_survey);

} // namespace gt3r::tool
