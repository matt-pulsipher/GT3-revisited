// The passes after the 3D picture (presentation §L6): the heat haze lens field, the lens shape, the
// vignette ring, the screen batches View3D draws them from, and their course data on the disc.

#include "test_framework.h"

#include "formats/course/course_file.h"
#include "race/replay_cameras.h"
#include "race/replay_post.h"
#include "race/sun_glare.h"
#include "render/math3d.h"
#include "render/screen_batch.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::race;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// An Env0 block with the haze and vignette fields set (invented values, the US courses' shape).
Bytes synth_env0(u32 flags, u32 count = 400) {
    Bytes env(0xF00, 0);
    std::memcpy(env.data(), "Env0", 4);
    std::memcpy(env.data() + 0x10, &flags, 4);
    std::memcpy(env.data() + 0xB0, &count, 4);
    auto put = [&](size_t o, float v) { std::memcpy(env.data() + o, &v, 4); };
    put(0xB4, 3.5f), put(0xB8, 40), put(0xBC, 3), put(0xC0, 40), put(0xC4, 0.01f), put(0xC8, 0.01f);
    put(0x950, 420), put(0x954, 200);
    put(0x958, 0), put(0x95C, 0), put(0x960, 0), put(0x964, 0.4f); // outer: black, A 0.4
    put(0x968, 0), put(0x96C, 0), put(0x970, 0), put(0x974, 0);    // inner: clear
    return env;
}

// A camera at the origin looking along `forward` (level), and the race's projection.
render::Mat4 view_towards(render::Vec3 forward) { return render::look_at({0, 0, 0}, forward, {0, 1, 0}); }

} // namespace

TEST(race_haze_wrap) {
    // Into [-L/2, L/2) about 0: r = d - trunc(d / L) L, + L for d < 0, - L / 2.
    CHECK(near(haze_wrap(0, 10), -5));
    CHECK(near(haze_wrap(3, 10), -2));
    CHECK(near(haze_wrap(13, 10), -2));
    CHECK(near(haze_wrap(-3, 10), 2));  // -3 + 10 - 5
    CHECK(near(haze_wrap(-13, 10), 2)); // (-13 + 10) + 10 - 5
    CHECK(near(haze_wrap(25, 4), -1)); // 25 - 6 x 4 - 2
    for (float d = -100; d < 100; d += 0.37f) {
        const float w = haze_wrap(d, 4.2f);
        CHECK(w >= -2.1f - 1e-4f && w <= 2.1f + 1e-4f);
        // The same point modulo the period.
        const float k = std::round((d - w - 2.1f) / 4.2f);
        CHECK(near(d - w - 2.1f, k * 4.2f, 1e-3f));
    }
}

TEST(race_haze_hexagon) {
    // A lens away from the edges: positions (+-0.58 S, +-0.5 S field lines = +-1.0 S frame lines),
    // (+-1.16 S, 0); texels (+-0.58 S, +-0.9 S), (+-0.928 S, 0): 1.25x across, 1.11x down.
    const float S = 10;
    auto v = haze_hexagon(320, 224, S);
    CHECK(near(v[0].x, 325.8f) && near(v[0].y, 234) && near(v[0].u, 325.8f) && near(v[0].v, 233));
    CHECK(near(v[1].x, 314.2f) && near(v[1].y, 234));
    CHECK(near(v[2].x, 331.6f) && near(v[2].y, 224) && near(v[2].u, 329.28f) && near(v[2].v, 224));
    CHECK(near(v[3].x, 308.4f) && near(v[3].u, 310.72f));
    CHECK(near(v[4].y, 214) && near(v[4].v, 215) && near(v[5].x, 314.2f) && near(v[5].y, 214));
    CHECK(near((v[2].x - v[3].x) / (v[2].u - v[3].u), 1.25f, 1e-4f));
    CHECK(near((v[0].y - v[4].y) / (v[0].v - v[4].v), 0.5f / 0.45f, 1e-4f));
    // At the frame's top-left: the left-hand vertices and their texels stay at x >= 0, the
    // top-left one's y >= 0, the upper texels' v >= 0.
    auto e = haze_hexagon(2, 3, S);
    CHECK(e[1].x == 0 && e[3].x == 0 && e[5].x == 0 && e[5].y == 0 && e[1].u == 0 && e[3].u == 0);
    CHECK(e[4].v == 0 && e[5].v == 0 && e[4].y < 0); // (the top-right vertex's y is not kept)
}

TEST(race_haze_setup_and_drift) {
    HeatHaze h;
    h.setup(SunParams::from_env0(synth_env0(0x20 | 0x400)));
    CHECK(h.active() && h.particles().size() == 400 && near(h.size(), 3.5f));
    // The course's pass: inside the box, the vertical drift constant, the horizontal within range.
    for (const HeatHaze::Particle& p : h.particles()) {
        CHECK(std::fabs(p.x) <= 40 && std::fabs(p.z) <= 40 && std::fabs(p.y) <= 1.5f);
        CHECK(std::fabs(p.vx) <= 0.01f && std::fabs(p.vz) <= 0.01f && near(p.vy, 0.01f, 1e-7f));
    }
    // The generator: seed 0x7F7F7F7F, s = s * 0x5D588B65 + 1; the first pass draws 5 per particle
    // (last particle first), so the course's pass starts at draw 2001 with the last particle's x.
    u32 s = HeatHaze::kSeed;
    for (int i = 0; i < 2001; ++i) s = s * HeatHaze::kMultiplier + 1u;
    u32 bits = 0x3F800000u | (s & 0x7FFFFFu);
    float r;
    std::memcpy(&r, &bits, 4);
    CHECK(near(h.particles().back().x, (r - 1.0f) * 80.0f - 40.0f, 1e-4f));
    // The same set-up gives the same field (every run, replay and demonstration).
    HeatHaze h2;
    h2.setup(SunParams::from_env0(synth_env0(0x20)));
    CHECK(std::memcmp(h.particles().data(), h2.particles().data(), h.particles().size() * sizeof(HeatHaze::Particle)) == 0);
    // One frame: each particle by its drift, no time step.
    const HeatHaze::Particle before = h.particles()[7];
    h.step();
    CHECK(near(h.particles()[7].x, before.x + before.vx, 1e-5f) && near(h.particles()[7].y, before.y + 0.01f, 1e-5f));
    // No field without the flag; at most the buffer's 800.
    HeatHaze off;
    off.setup(SunParams::from_env0(synth_env0(0x400)));
    CHECK(!off.active());
    HeatHaze big;
    big.setup(SunParams::from_env0(synth_env0(0x20, 5000)));
    CHECK(big.particles().size() == HeatHaze::kMaxLenses);
}

TEST(race_haze_lenses_any_heading) {
    HeatHaze h;
    h.setup(SunParams::from_env0(synth_env0(0x20)));
    // A telephoto camera (zoom 19 over 640 x 448): the original projects the camera-relative,
    // world-axis field with the view's rotation and the projection, so the box ahead is in view
    // whichever way the camera looks; the same field wraps the same way about each heading.
    const float fovy = 2.0f * std::atan(0.7f / 19.0f);
    const render::Mat4 proj = render::perspective(fovy, 640.0f / 448.0f, 0.1f, 6000.0f);
    std::vector<HazeLens> along, across, up;
    h.lenses(view_towards({0, 0, -1}), proj, along);
    h.lenses(view_towards({1, 0, 0}), proj, across);
    h.lenses(view_towards(render::normalize(render::Vec3{-0.6f, 0.1f, 0.8f})), proj, up);
    CHECK(along.size() > 100 && across.size() > 100 && up.size() > 100);
    const float m = proj.at(0, 0);
    for (const HazeLens& l : along) {
        CHECK(near(l.size, 3.5f * std::sqrt(m), 1e-3f));
        CHECK(l.x >= 0 && l.x <= 640 && l.y >= 0 && l.y <= 448 && l.depth > 0 && l.depth < 1);
    }
    // Wide (zoom 1.5): a box 2 x 40 / m across, the lenses a few pixels.
    const render::Mat4 wide = render::perspective(2.0f * std::atan(0.7f / 1.5f), 640.0f / 448.0f, 0.1f, 6000.0f);
    std::vector<HazeLens> w;
    h.lenses(view_towards({0, 0, -1}), wide, w);
    CHECK(!w.empty() && near(w[0].size, 3.5f * std::sqrt(wide.at(0, 0)), 1e-3f) && w[0].size < 5);
    // The batch: six vertices and four triangles a lens, in the 640 x 448 frame.
    render::ScreenBatch b;
    add_haze_lenses(b, along);
    CHECK(b.verts.size() == along.size() * 6 && b.idx.size() == along.size() * 12 && b.draws.size() == 1);
    CHECK(b.draws[0].blend == render::BlendMode::Alpha && near(b.rect.w, 640) && near(b.rect.h, 448));
}

TEST(race_vignette_ring) {
    const SunParams p = SunParams::from_env0(synth_env0(0x40));
    CHECK(p.vignette && !p.haze && near(p.vignette_outer, 420) && near(p.vignette_inner, 200));
    const auto ring = vignette_ring(p);
    // Outer and inner circles alternate; 32 steps of 2 pi / 31, the last back at angle 0.
    CHECK(near(ring[0].x, 420) && near(ring[0].y, 0) && near(ring[1].x, 200));
    CHECK(near(ring[62].x, ring[0].x) && near(ring[62].y, ring[0].y) && near(ring[63].x, 200));
    const float a = 6.28318548f / 31.0f;
    CHECK(near(ring[2].x, 420 * std::cos(a), 1e-3f) && near(ring[2].y, 420 * std::sin(a), 1e-3f));
    // GS bytes: alpha 0.4 x 128 + 0.5 -> 51; the inner circle clear.
    CHECK(ring[0].rgba[3] == 51 && ring[0].rgba[0] == 0 && ring[1].rgba[3] == 0);
    render::ScreenBatch b;
    add_vignette(b, p);
    CHECK(b.verts.size() == 64 && b.idx.size() == 62 * 3 && b.draws[0].blend == render::BlendMode::Alpha);
    CHECK(near(b.verts[0].x, 320 + 420) && near(b.verts[0].y, 224) && near(b.verts[0].rgba[3], 51.0f / 128.0f));
    // A colour over 1 clamps at 255.
    Bytes env = synth_env0(0x40);
    const float big = 9;
    std::memcpy(env.data() + 0x958, &big, 4);
    CHECK(vignette_ring(SunParams::from_env0(env))[0].rgba[0] == 255);
}

TEST(render_screen_batch) {
    render::ScreenBatch b;
    const float white[4] = {1, 1, 1, 1};
    b.add_rect(nullptr, SDL_FRect{10, 20, 30, 40}, white);
    b.add_rect(nullptr, SDL_FRect{0, 0, 1, 1}, white);
    CHECK(b.verts.size() == 8 && b.idx.size() == 12 && b.draws.size() == 1 && b.draws[0].count == 12); // joined
    CHECK(near(b.verts[3].x, 40) && near(b.verts[3].y, 60) && near(b.verts[3].u, 1) && near(b.verts[3].v, 1));
    render::ScreenBatch::Vertex v[3];
    const u32 tri[3] = {0, 1, 2};
    b.add(nullptr, v, 3, tri, 3, 2); // another weight: a draw of its own
    b.add(nullptr, v, 3, tri, 3, 1, render::BlendMode::Alpha);
    CHECK(b.draws.size() == 3 && b.draws[1].passes == 2 && b.draws[1].first == 12 && b.idx[12] == 8);
    b.clear();
    CHECK(b.empty() && b.verts.empty());
}

TEST(race_haze_course_data_disc) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    struct Course {
        const char* name;
        size_t records, hazy;
        bool haze, vignette;
    };
    // Every record flagged on Grand Valley and Laguna; none on Akasaka; Seattle's flagged records
    // have no Env0 0x20; the vignette on Trial Mountain, not on Grand Valley.
    const Course courses[] = {{"grandvalley", 97, 97, true, false},
                              {"laguna", 44, 44, true, true},
                              {"trialmt", 66, 65, true, true},
                              {"akasaka", 80, 0, true, true},
                              {"seattle", 84, 84, false, false}};
    for (const Course& c : courses) {
        const std::string path = std::string("courses/US/") + c.name;
        Bytes data = disc->volume().read(*disc->volume().find(path));
        course::CourseFile f = course::CourseFile::parse(data);
        GtrcScript g = GtrcScript::parse(f.section(data, course::kSlotReplayCameras));
        size_t hazy = 0;
        for (const GtrcRecord& r : g.records) hazy += r.hazy() ? 1 : 0;
        const SunParams p = SunParams::from_env0(f.section(data, 32));
        std::printf("%s: %zu of %zu records hazy, haze %d, vignette %d, %u lenses\n", c.name, hazy, g.records.size(),
                    int(p.haze), int(p.vignette), unsigned(p.haze_count));
        CHECK(g.records.size() == c.records && hazy == c.hazy && p.haze == c.haze && p.vignette == c.vignette);
        CHECK(p.haze_count == 400);
    }
}
