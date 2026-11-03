// Renderer workstream tests (CPU only: math, camera, procedural models).

#include "render/camera.h"
#include "render/dynamic_scale.h"
#include "render/gamma_pass.h"
#include "render/gpu_context.h"
#include "render/gpu_model.h"
#include "render/guard_band.h"
#include "render/math3d.h"
#include "render/test_models.h"
#include "render/view3d.h"
#include "test_framework.h"

#include <algorithm>
#include <cmath>

using namespace gt3r;
using namespace gt3r::render;

namespace {
bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
}

TEST(render_mat4_inverse) {
    Mat4 m = translate({1, 2, 3}) * rotate_y(0.7f) * scale({2, 3, 4});
    Mat4 i = m * inverse(m);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) CHECK(near(i.at(r, c), r == c ? 1.0f : 0.0f));
    Vec3 p = translate({1, 2, 3}).transform_point({1, 1, 1});
    CHECK(near(p.x, 2) && near(p.y, 3) && near(p.z, 4));
}

TEST(render_mip_chain_down_to_one_texel) {
    // Down to 1 x 1, the shorter side staying at 1 (GT3's car textures are mostly 2:1).
    CHECK(mip_levels(1, 1) == 1);
    CHECK(mip_levels(64, 64) == 7);
    CHECK(mip_levels(32, 16) == 6);
    CHECK(mip_levels(256, 1) == 9);
    std::vector<u32> t(32 * 16, 0x80402010u);
    u32 w = 32, h = 16;
    for (u32 i = 1; i < mip_levels(32, 16); ++i) {
        t = half_size(t.data(), w, h);
        w = std::max(1u, w / 2), h = std::max(1u, h / 2);
        CHECK(t.size() == size_t(w) * h && t[0] == 0x80402010u);
    }
    CHECK(w == 1 && h == 1);
}

TEST(render_half_size_averages_each_block) {
    // Per channel, alpha too, rounded to nearest: (1 + 2 + 3 + 4 + 2) / 4 = 3.
    const u32 px[4] = {0x01FF0001u, 0x02000002u, 0x03FF0003u, 0x04000004u};
    std::vector<u32> h = half_size(px, 2, 2);
    CHECK(h.size() == 1 && h[0] == 0x03800003u);
    // A one-texel-wide level halves only its height.
    const u32 col[4] = {0x00000000u, 0x000000FFu, 0x00000010u, 0x00000030u};
    h = half_size(col, 1, 4);
    CHECK(h.size() == 2 && h[0] == 0x00000080u && h[1] == 0x00000020u);
}

TEST(render_pending_gpu_work_waits_past_a_limit_until_a_frame) {
    PendingGpuWork w;
    for (u32 i = 1; i < PendingGpuWork::kMaxSubmits; ++i) CHECK(!w.add(1, 1024));
    CHECK(w.add(1, 1024)); // the 128th submit since the last frame: wait, counting from 0
    CHECK(!w.add(1, 0));
    // Staged data alone: three 30 MB model uploads.
    w.frame_presented();
    CHECK(!w.add(1, 30ull << 20) && !w.add(1, 30ull << 20));
    CHECK(w.add(1, 30ull << 20));
    // A presented frame recycled everything: a frame's own submits never wait.
    for (int frame = 0; frame < 1000; ++frame) {
        for (int i = 0; i < 40; ++i) CHECK(!w.add(1, 4096));
        w.frame_presented();
    }
}

namespace {
// `seconds` of frames on a vsynced `hz` display: the GPU takes gpu_at_1 x scale^2 ms (fill
// time), the CPU `cpu_ms`; a frame lasts whole refreshes and waits in present for the rest.
// Returns how often the scale changed in the last `tail_s` seconds.
int run_frames(render::DynamicScale& d, double hz, double gpu_at_1, double cpu_ms, double seconds,
               double tail_s = 0) {
    const double refresh = 1000.0 / hz;
    int changes = 0;
    for (double t = 0; t < seconds * 1000;) {
        const float s = d.scale();
        const double busy = std::max(gpu_at_1 * s * s, cpu_ms);
        const double frame = std::ceil(busy / refresh - 1e-9) * refresh;
        d.frame(frame, frame - cpu_ms);
        if (t >= (seconds - tail_s) * 1000 && d.scale() != s) ++changes;
        t += frame;
    }
    return changes;
}
} // namespace

TEST(render_dynamic_scale_targets_whole_refreshes) {
    render::DynamicScale d;
    d.reset(1.0f, 60);
    CHECK(near(float(d.target_ms()), 16.667f, 0.01f));
    d.reset(1.0f, 90); // 11.1 ms refreshes: 60 fps is no cadence there, 45 is
    CHECK(near(float(d.target_ms()), 22.222f, 0.01f));
    d.reset(1.0f, 120);
    CHECK(near(float(d.target_ms()), 16.667f, 0.01f));
    d.reset(1.0f, 0);
    CHECK(near(float(d.target_ms()), 16.667f, 0.01f));
}

TEST(render_dynamic_scale_lowers_gpu_bound_and_settles) {
    // 33 ms of GPU at full size on a 60 Hz display: 60 fps needs a scale of 0.7 or less.
    render::DynamicScale d;
    d.reset(1.0f, 60);
    int late_changes = run_frames(d, 60, 32.0, 2.0, 300, 120);
    CHECK(d.scale() <= 0.71f && d.scale() >= 0.6f);
    CHECK(late_changes <= 8); // the odd step up that is undone, no oscillation
    // Never below 50%, however slow the GPU.
    d.reset(1.0f, 60);
    run_frames(d, 60, 120.0, 2.0, 60);
    CHECK(near(d.scale(), render::DynamicScale::kMin));
}

TEST(render_dynamic_scale_keeps_cpu_bound_and_fast_frames) {
    // Slow frames that are the CPU's: a smaller picture would not help.
    render::DynamicScale d;
    d.reset(0.85f, 60);
    run_frames(d, 60, 5.0, 30.0, 60);
    CHECK(near(d.scale(), 0.85f));
    // Headroom: back up to the configured scale (never past it).
    d.reset(0.85f, 60);
    run_frames(d, 60, 40.0, 2.0, 10); // heavy for a while...
    CHECK(d.scale() < 0.8f);
    run_frames(d, 60, 8.0, 2.0, 120); // ...then light
    CHECK(near(d.scale(), 0.85f));
    // Loads and pauses (over 250 ms) are not frames, and the 5 s after one are not judged.
    d.reset(0.85f, 60);
    for (int i = 0; i < 20; ++i) d.frame(900, 890);
    CHECK(near(d.scale(), 0.85f));
    run_frames(d, 60, 40.0, 2.0, 4.9); // the race settling after its load
    CHECK(near(d.scale(), 0.85f));
    run_frames(d, 60, 40.0, 2.0, 1.5); // then still slow: lowered
    CHECK(d.scale() < 0.85f);
}

TEST(render_dynamic_scale_is_calm) {
    // A GPU right at the edge: 17.5 ms at 70% would miss a 60 Hz refresh every frame, 14.8 ms at
    // 65% never does. The scale steps down to 65% and stays there: the odd probe of 70% is undone
    // and then not retried for a minute.
    render::DynamicScale d;
    d.reset(0.7f, 60);
    int changes = run_frames(d, 60, 35.0, 2.0, 180, 170);
    CHECK(near(d.scale(), 0.65f));
    CHECK(changes <= 6);
    // A few missed refreshes a second while GPU-bound (no bad average): stepped down, not held.
    d.reset(0.8f, 60);
    for (int w = 0; w < 3; ++w) // seconds of 55 frames on time and 3 that missed
        for (int i = 0; i < 58; ++i) d.frame(i < 55 ? 16.67 : 33.3, i < 55 ? 8.0 : 25.0);
    CHECK(d.scale() < 0.8f);
}

TEST(render_dynamic_scale_ignores_a_capped_wobble) {
    // A Pixel 10 holds games to 60 fps on its 120 Hz screen: frame times wobble around 16.7 ms,
    // a 22 ms frame made up by an 11 ms one, a few times a second, while the GPU keeps up with
    // room to spare. Not misses: the scale climbs from the Auto start to full resolution.
    render::DynamicScale d;
    d.reset(1.0f, 120, 0.83f);
    for (int s = 0; s < 60; ++s)
        for (int i = 0; i < 60; ++i) d.frame(i % 20 == 7 ? 22.0 : i % 20 == 8 ? 11.3 : 16.67, 9.0);
    CHECK(near(d.scale(), 1.0f));
    // A frame really shown a refresh late (25 ms, not made up) still counts: three a second
    // while waiting on the GPU step the scale down.
    d.reset(1.0f, 120);
    for (int i = 0; i < 180; ++i) d.frame(i % 20 == 7 ? 25.0 : 16.67, 9.0);
    CHECK(d.scale() < 1.0f);
}

TEST(render_dynamic_scale_climbs_quickly_after_a_load) {
    // A race start (a frame over 250 ms) on a phone with room to spare: from the Auto start to
    // full resolution in about 5 s, a step every clean second, the slow first second included.
    render::DynamicScale d;
    d.reset(1.0f, 120, 0.83f);
    d.frame(400, 390);
    for (int i = 0; i < 60; ++i) d.frame(i == 30 ? 104.0 : 16.67, 9.0); // the start's hitch
    run_frames(d, 120, 8.0, 5.0, 5.0);
    CHECK(near(d.scale(), 1.0f));
    // A phone that is slow while the race starts neither climbs nor drops in those 5 s, then
    // steps down as usual.
    d.reset(1.0f, 90, 0.83f);
    d.frame(400, 390);
    run_frames(d, 90, 60.0, 5.0, 4.9);
    CHECK(near(d.scale(), 0.83f));
    run_frames(d, 90, 60.0, 5.0, 2.0);
    CHECK(d.scale() < 0.83f);
    // Once a step up has missed, the rest are 5 s apart again: a GPU that keeps up at 70% but not
    // at 75% is at most at 75% for a second at a time.
    d.reset(1.0f, 60, 0.5f);
    d.frame(400, 390);
    int changes = run_frames(d, 60, 32.0, 2.0, 120, 100);
    CHECK(d.scale() <= 0.71f);
    CHECK(changes <= 8);
}

TEST(render_dynamic_scale_starts_below_its_ceiling) {
    // Auto on a phone: starts at the fixed Auto size and climbs to full resolution when the GPU
    // keeps up...
    render::DynamicScale d;
    d.reset(1.0f, 60, 0.83f);
    CHECK(near(d.scale(), 0.83f));
    run_frames(d, 60, 8.0, 2.0, 120);
    CHECK(near(d.scale(), 1.0f));
    // ...and a slow GPU never goes above what it keeps up with.
    d.reset(1.0f, 60, 0.83f);
    run_frames(d, 60, 32.0, 2.0, 300);
    CHECK(d.scale() <= 0.71f);
    // The start is kept between the floor and the ceiling.
    d.reset(0.7f, 60, 0.9f);
    CHECK(near(d.scale(), 0.7f));
    d.reset(0.7f, 60, 0.2f);
    CHECK(near(d.scale(), render::DynamicScale::kMin));
}

TEST(render_dynamic_scale_supersamples) {
    // 3D resolution 200% with dynamic resolution: from 200% down to what the GPU keeps up with
    // (6 ms at 100%: 60 fps up to about 165%), and back up to 200% when it has room again.
    render::DynamicScale d;
    d.reset(2.0f, 60);
    CHECK(near(d.scale(), 2.0f));
    run_frames(d, 60, 6.0, 2.0, 300);
    CHECK(d.scale() > 1.0f && d.scale() <= 1.66f);
    run_frames(d, 60, 2.0, 2.0, 600);
    CHECK(near(d.scale(), 2.0f));
    d.reset(9.0f, 60); // at most 400%
    CHECK(near(d.scale(), 4.0f));
}

TEST(render_perspective_depth_range) {
    // Reversed depth like GT3's Z buffer: the near plane maps to 1, the far plane to 0.
    Mat4 p = perspective(1.0f, 16.0f / 9.0f, 0.5f, 100.0f);
    auto depth = [&](const Mat4& m, float z) {
        float zc = m.at(2, 2) * z + m.at(2, 3), wc = m.at(3, 2) * z;
        return zc / wc;
    };
    CHECK(near(depth(p, -0.5f), 1.0f));
    CHECK(near(depth(p, -100.0f), 0.0f));
    CHECK(depth(p, -10.0f) > depth(p, -10.5f)); // nearer = larger (GT3's GEQUAL test)
    Mat4 f = frustum(-0.8f, 1.1f, -0.9f, 0.7f, 0.5f, 100.0f); // VR eyes: the same depth
    CHECK(near(depth(f, -0.5f), 1.0f));
    CHECK(near(depth(f, -100.0f), 0.0f));
}

TEST(render_perspective_depth_resolves_millimetres_far_away) {
    // A decal 2 mm in front of a car body seen by a replay camera 150 m away (fovy 0.024 rad, the
    // race's 0.1 / 6000 m planes), computed in float like the GPU (one mvp, then z / w): the
    // decal's depth must be the larger one. The usual 0..1 mapping put both near 1, where a
    // float resolves only ~1 cm at that distance (the decals flickered against the body).
    const Vec3 eye{420.0f, 12.0f, -310.0f}, car{300.0f, 4.0f, -215.0f};
    const Mat4 view = look_at(eye, car, {0, 1, 0});
    const Mat4 mvp = perspective(0.024f, 16.0f / 9.0f, 0.1f, 6000.0f) * view * translate(car);
    const Vec3 toward = normalize(eye - car);
    auto depth = [&](const Vec3& p) {
        float zc = mvp.at(2, 0) * p.x + mvp.at(2, 1) * p.y + mvp.at(2, 2) * p.z + mvp.at(2, 3);
        float wc = mvp.at(3, 0) * p.x + mvp.at(3, 1) * p.y + mvp.at(3, 2) * p.z + mvp.at(3, 3);
        return zc / wc;
    };
    for (float side : {-0.8f, 0.0f, 0.6f}) {
        const Vec3 body{side, 0.5f, 0.3f * side};
        CHECK(depth(body + toward * 0.002f) > depth(body));
    }
}

TEST(render_look_at_maps_target_forward) {
    Mat4 v = look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    Vec3 t = v.transform_point({0, 0, 0});
    CHECK(near(t.x, 0) && near(t.y, 0) && near(t.z, -5)); // camera looks down -Z
}

TEST(render_orbit_camera_frames_bounds) {
    OrbitCamera cam;
    float bmin[3] = {-1, 0, -2}, bmax[3] = {1, 2, 2};
    cam.frame(bmin, bmax);
    Vec3 e = cam.eye();
    float d = length(e - Vec3{0, 1, 0});
    CHECK(near(d, cam.distance, 1e-3f));
    CHECK(cam.distance > 2.0f); // outside the box radius
    float yaw0 = cam.yaw;
    PadState pad;
    pad.buttons = u16(1u << unsigned(PadButton::Right));
    cam.update(pad, 0.1);
    CHECK(cam.yaw > yaw0);
}

TEST(render_test_models_are_valid) {
    for (int v = 0; v < kTestModelVariants; ++v) {
        scene::Model m = make_test_model(v);
        CHECK(!m.meshes.empty());
        for (const scene::Mesh& mesh : m.meshes) {
            CHECK(mesh.indices.size() % 3 == 0);
            for (u32 i : mesh.indices) CHECK(i < mesh.vertices.size());
            u32 covered = 0;
            for (const scene::Primitive& p : mesh.primitives) {
                CHECK(p.first_index + p.index_count <= mesh.indices.size());
                CHECK(p.texture < int(m.textures.size()));
                covered += p.index_count;
            }
            CHECK(covered == mesh.indices.size());
        }
        for (const scene::Texture& t : m.textures) CHECK(t.rgba.size() == size_t(t.width) * t.height);
        CHECK(m.bounds_max[1] > m.bounds_min[1]);
    }
}

TEST(render_model_stats_by_lod) {
    scene::Model m = make_test_model(2); // three LODs
    ModelStats all = model_stats(m, -1), l0 = model_stats(m, 0), l2 = model_stats(m, 2);
    CHECK(all.max_lod == 2);
    CHECK(all.meshes == 3 && l0.meshes == 1 && l2.meshes == 1);
    CHECK(l0.triangles > l2.triangles);
    CHECK(all.triangles == l0.triangles + model_stats(m, 1).triangles + l2.triangles);
}

TEST(render_ground_grid) {
    float bmin[3] = {-2, -0.5f, -1}, bmax[3] = {2, 1, 1};
    scene::Model g = make_ground_grid(bmin, bmax);
    CHECK(g.meshes.size() == 1 && g.textures.size() == 1);
    CHECK(near(g.bounds_min[1], -0.5f) && near(g.bounds_max[1], -0.5f));
    CHECK(g.bounds_min[0] < -2 && g.bounds_max[0] > 2);
}

// The grade pass (presentation §G6) in GT3's 8-bit arithmetic: levels, the curve, the
// saturation mix, each output channel's curve on every input channel.
TEST(render_post_grade_formula) {
    using G = DrawOptions::PostGrade;
    G id; // black 0, white 1, g 0.5, saturation 1: (almost) identity
    for (int v : {0, 1, 64, 128, 200, 255}) {
        std::array<u8, 3> o = post_grade_pixel(id, u8(v), u8(v), u8(v));
        CHECK(std::abs(int(o[0]) - v) <= 1 && o[0] == o[1] && o[1] == o[2]);
    }
    // Levels stretch [black, white] to [0, 1]: white 0.8 lifts 0.8 to full.
    G lv;
    lv.white = 0.8f;
    CHECK(post_grade_pixel(lv, 204, 204, 204)[0] == 255 && post_grade_pixel(lv, 102, 0, 0)[0] == 127);
    // The red preset with saturation 0: a pure red pixel turns grey 0.3 (75), not red.
    G red;
    red.curve[0] = 1, red.curve[1] = 0, red.curve[2] = 0, red.saturation = 0;
    std::array<u8, 3> o = post_grade_pixel(red, 255, 0, 0);
    CHECK(o[0] == 75 && o[1] == 75 && o[2] == 75);
    // A curve of g = 1 brightens the mid-tones: t^2 + 2 t (1 - t) at 0.5 = 0.75.
    G bright;
    bright.curve[0] = bright.curve[1] = bright.curve[2] = 1;
    CHECK(post_grade_pixel(bright, 128, 128, 128)[0] == 191);
    // Swapped levels (black 0.9, white 0.2): an inverted, high-contrast picture.
    G neg;
    neg.black = 0.9f, neg.white = 0.2f;
    CHECK(post_grade_pixel(neg, 30, 30, 30)[0] == 255 && post_grade_pixel(neg, 240, 240, 240)[0] == 0);
    // Entry 0 is unclamped: a channel at 0 with g 0 and close levels comes out bright.
    G q;
    q.black = 0.17f, q.white = 0.2f, q.curve[0] = q.curve[1] = q.curve[2] = 0;
    CHECK(post_grade_pixel(q, 0, 0, 0)[0] == 255 && post_grade_pixel(q, 1, 1, 1)[0] == 0);
    // Super Speedway's Env0 (g 0.87 0.90 0.95, white 0.8): brighter, a cool cast.
    G ss;
    ss.white = 0.8f, ss.curve[0] = 0.87f, ss.curve[1] = 0.90f, ss.curve[2] = 0.95f;
    o = post_grade_pixel(ss, 60, 60, 60);
    CHECK(o[0] > 100 && o[0] < o[1] && o[1] < o[2]);
}

TEST(render_gs_pixel_truncates) {
    auto px = [](float t, float c, u8 fog, float f) {
        const float tt[3] = {t, t, t}, cc[3] = {c, c, c};
        const u8 ff[3] = {fog, fog, fog};
        return int(gs_pixel(tt, cc, ff, f)[0]);
    };
    // MODULATE: 0x80 keeps the texel, other colours truncate (101 x 0x40 >> 7 = 50.5 -> 50), as
    // does the filtered texel (100.7 -> 100); colours past 0x80 clamp at 255.
    CHECK(px(101, 128, 0, 256) == 101 && px(101, 128, 0, 0) == 0);
    const float t3[3] = {101, 100.7f, 200}, c3[3] = {64, 128, 192};
    const u8 none[3] = {0, 0, 0};
    std::array<u8, 3> o = gs_pixel(t3, c3, none, 256);
    CHECK(o[0] == 50 && o[1] == 100 && o[2] == 255);
    // Fog (F C + (256 - F) FOGCOL) >> 8: F = 0 gives the fog colour; F = 255 (no fog) takes a
    // colour past the fog colour down a level and keeps one below it.
    CHECK(px(100, 128, 155, 0) == 155);
    CHECK(px(50, 128, 0, 255) == 49 && px(200, 128, 91, 255) == 199 && px(60, 128, 155, 255) == 60);
    // Super Speedway's road just ahead (a PCSX2 GS dump of the grid, in-car view): the base pass
    // (colour (57, 56, 56), fogged at F = 255 to 0x9B5B5B), then the sheen (colour ~(32.75, 29.91,
    // 26.19) there, fogged to black) added at a frame alpha of 0x80, over a texel (104, 105, 106):
    // 71, 68, 66, where float maths and rounded writes give 73, 71, 68.
    const float tex[3] = {104, 105, 106}, base[3] = {57, 56, 56}, sheen[3] = {32.75f, 29.91f, 26.19f};
    const u8 fog[3] = {0x9B, 0x5B, 0x5B};
    std::array<u8, 3> b = gs_pixel(tex, base, fog, 255), s = gs_pixel(tex, sheen, none, 255);
    CHECK(b[0] == 46 && b[1] == 45 && b[2] == 46 && s[0] == 25 && s[1] == 23 && s[2] == 20);
    CHECK(b[0] + s[0] == 71 && b[1] + s[1] == 68 && b[2] + s[2] == 66);
}

TEST(render_depth_of_field_layers) {
    // presentation §G17: the layers in drawing order, from the lens formula (in double here).
    DrawOptions::DepthOfField d;
    d.f_number = 10, d.focal_mm = 35, d.focus = 94, d.far = 1000;
    const double film = 0.035 / std::sqrt(1.0 + 1.333 * 1.333), c = film / 448.0;
    auto at = [&](double proj_y, double s, double r, int side) { // side +1 near, -1 far
        const double f = 0.5 * film * proj_y, f2 = f * f;
        return f2 * s / (f2 + side * 10.0 * r * c * (s - f));
    };
    auto same = [](float a, double b) { return std::fabs(double(a) - b) <= 1e-3 * std::max(1.0, std::fabs(b)); };
    // A telephoto (zoom 22.8 over GT3's 448-line frame: its projection's y scale 29.66).
    const float ty = 29.66f;
    std::vector<DofLayer> l = dof_layers(d, ty, 0.1f);
    CHECK(l.size() == 7);
    if (l.size() == 7) {
        // Near: 4, 2, 1 lines (levels 2, 1, 0), each from the last one's distance; the first everywhere.
        CHECK(l[0].level == 2 && l[0].alpha == 1 && l[0].distance == 0);
        CHECK(l[1].level == 1 && same(l[1].distance, at(ty, 94, 4, 1)));
        CHECK(l[2].level == 0 && same(l[2].distance, at(ty, 94, 2, 1)));
        // The sharp picture from the 1-line distance on.
        CHECK(l[3].level == -1 && l[3].alpha == 1 && same(l[3].distance, at(ty, 94, 1, 1)));
        // Far: 1, 1.41, 2 lines (levels 0, 0, 1 at 1/2, 1, 1/2); 2.83 lines is past the hyperfocal
        // distance (negative: everything beyond is that sharp).
        CHECK(l[4].level == 0 && l[4].alpha == 0.5f && same(l[4].distance, at(ty, 94, 1, -1)));
        CHECK(l[5].level == 0 && l[5].alpha == 1 && same(l[5].distance, at(ty, 94, std::sqrt(2.0), -1)));
        CHECK(l[6].level == 1 && l[6].alpha == 0.5f && same(l[6].distance, at(ty, 94, 2, -1)));
        CHECK(at(ty, 94, 2 * std::sqrt(2.0), -1) < 0);
        CHECK(l[1].distance < l[2].distance && l[2].distance < l[3].distance && l[3].distance < 94);
    }
    // Focused past the hyperfocal distance (a wide lens): no far layers, the sharp one last.
    l = dof_layers(d, 2.6f, 0.1f);
    CHECK(!l.empty() && l.back().level == -1);
    // Close focus on a long lens with no far plane: every far step to 8 lines, levels 0..3.
    d.focus = 10, d.far = 1e9f;
    l = dof_layers(d, ty, 0.1f);
    const int levels[7] = {0, 0, 1, 1, 2, 2, 3};
    const float alphas[7] = {0.5f, 1, 0.5f, 1, 0.5f, 1, 0.5f};
    CHECK(l.size() == 11);
    for (size_t i = 0; i < 7 && i + 4 < l.size(); ++i)
        CHECK(l[i + 4].level == levels[i] && l[i + 4].alpha == alphas[i] && l[i + 4].distance > l[i + 3].distance);
    // A near plane beyond the 4-line distance: that layer is skipped, the 2-line one covers all.
    l = dof_layers(d, ty, at(ty, 10, 4, 1) + 0.01f);
    CHECK(!l.empty() && l[0].level == 1 && l[0].distance == 0);
    // The far plane cuts the far steps off.
    d.far = float(at(ty, 10, 2, -1)) + 0.01f;
    l = dof_layers(d, ty, 0.1f);
    CHECK(!l.empty() && l.back().level == 1 && l.back().alpha == 0.5f);
}

TEST(render_depth_of_field_levels) {
    // GT3's downsampled pictures at 4:3; a wider picture widens them at the frame's line spacing.
    const int kSize[5][2] = {{320, 224}, {320, 112}, {160, 112}, {160, 56}, {160, 56}};
    for (int k = 0; k < 5; ++k) {
        const std::array<int, 2> s = dof_level_size(k, 4.0f / 3.0f);
        CHECK(s[0] == kSize[k][0] && s[1] == kSize[k][1]);
    }
    CHECK(dof_level_size(0, 16.0f / 9.0f)[0] == 427 && dof_level_size(3, 16.0f / 9.0f)[0] == 213);
    CHECK(dof_level_size(4, 16.0f / 9.0f)[1] == 56);
}

TEST(render_depth_of_field_level_blur) {
    // Each 2-texel mean adds (spacing)^2 / 4 to the variance, the bilinear enlargement its texels'
    // spacing^2 / 6; kept axes move the picture half a texel, the layers half a frame pixel.
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    const DofBlur l0 = dof_level_blur(0), l1 = dof_level_blur(1), l3 = dof_level_blur(3), l4 = dof_level_blur(4);
    CHECK(near(l0.sigma[0], std::sqrt(0.25f + 4.0f / 6.0f)) && near(l0.sigma[1], l0.sigma[0]));
    CHECK(near(l0.shift[0], 0.5f) && near(l0.shift[1], 0.5f));
    CHECK(near(l1.sigma[0], std::sqrt(1.25f + 4.0f / 6.0f)) && near(l1.sigma[1], std::sqrt(1.25f + 16.0f / 6.0f)));
    CHECK(near(l1.shift[0], 1.5f) && near(l1.shift[1], 0.5f));
    CHECK(near(l3.sigma[0], std::sqrt(6.25f + 16.0f / 6.0f)) && near(l3.sigma[1], std::sqrt(9.25f + 64.0f / 6.0f)));
    CHECK(near(l3.shift[0], 3.5f) && near(l3.shift[1], 2.5f));
    CHECK(near(l4.shift[0], 5.5f) && near(l4.shift[1], 6.5f));
    for (int k = 1; k < 5; ++k) // every level blurs more than the one before
        CHECK(dof_level_blur(k).sigma[1] > dof_level_blur(k - 1).sigma[1]);
}

// A modelled rim's turned copies share out the picture's samples (DrawItem::spin_turn): every
// sample of a 4 x 4 pixel tile belongs to one copy, each copy to as many, and a pixel's copies are
// evenly spaced over all of them, so it resolves to the mean of copies spread over the whole turn.
// The filter after the scene weighs every cell alike when its taps cover the pattern's repeat.
TEST(render_spin_copy_samples) {
    for (int cells : {1, 2, 4, 8, 16})
        for (int samples : {1, 2, 4, 8}) {
            const int n = cells * samples;
            std::vector<int> owned(size_t(n), 0);
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    std::vector<int> held;
                    for (int s = 0; s < samples; ++s) {
                        const int c = spin_copy_at(x, y, s, cells);
                        CHECK(c >= 0 && c < n);
                        if (c < 0 || c >= n) return;
                        ++owned[size_t(c)];
                        held.push_back(c);
                        CHECK(spin_copy_at(x + 4, y + 8, s, cells) == c); // the pattern repeats every 4 pixels
                    }
                    std::sort(held.begin(), held.end());
                    for (size_t i = 1; i < held.size(); ++i) CHECK(held[i] - held[i - 1] == cells);
                    // Within the period, the cells differ: the filter's taps take in each once.
                    const int period = spin_copy_period(cells);
                    if (period > 1) CHECK(spin_copy_at(x, y, 0, cells) != spin_copy_at(x + period / 2, y, 0, cells));
                    for (int dy = 0; dy < 4; ++dy)
                        for (int dx = 0; dx < 4; ++dx) // cells repeat on the period
                            if (dx % period == 0 && dy % period == 0)
                                CHECK(spin_copy_at(x + dx, y + dy, 0, cells) == spin_copy_at(x, y, 0, cells));
                }
            for (int k : owned) CHECK(k == 16 / cells); // the tile has 16 x samples samples for cells x samples copies
        }
    // Copies: from the samples up, doubling while more than 1.5 pixels apart, at most 16 cells and
    // as many as fit 1.5 pixels apart over a radian of the rim's edge (64 from 96 pixels).
    CHECK(spin_copy_count(0.0f, 200.0f, 4) == 4);
    CHECK(spin_copy_count(6.0f, 200.0f, 4) == 4 && spin_copy_count(6.1f, 200.0f, 4) == 8);
    CHECK(spin_copy_count(20.0f, 200.0f, 4) == 16 && spin_copy_count(1000.0f, 200.0f, 4) == 64);
    CHECK(spin_copy_count(1000.0f, 96.0f, 4) == 64 && spin_copy_count(1000.0f, 95.0f, 4) == 32);
    CHECK(spin_copy_count(1000.0f, 24.0f, 4) == 16 && spin_copy_count(1000.0f, 6.0f, 4) == 4);
    CHECK(spin_copy_count(1000.0f, 200.0f, 1) == 16 && spin_copy_count(1000.0f, 200.0f, 8) == 64);
    CHECK(spin_copy_period(1) == 1 && spin_copy_period(4) == 2 && spin_copy_period(16) == 4);
}

TEST(render_sheen_vertex_program) {
    // Super Speedway's sheen material and scene light 0's specular.
    scene::Material m;
    m.valid = m.specular_local = true;
    m.specular[0] = 0.6f, m.specular[1] = 0.584f, m.specular[2] = 0.536f;
    m.shininess = 4;
    const float ls[3] = {0.83f, 0.80f, 0.75f};
    // Viewer up and back at 45 degrees, light at 45 degrees ahead: the reflected ray is the light.
    const Vec3 up{0, 1, 0}, at{0, 0, 0}, viewer{-1, 1, 0}, light{1, 1, 0};
    std::array<u8, 3> b = sheen_vertex_bytes(m, light, ls, viewer, at, up);
    CHECK(b[0] == 63 && b[1] == 59 && b[2] == 51); // trunc(128 Ls Ms)
    // The light behind the viewer: nothing.
    b = sheen_vertex_bytes(m, Vec3{-1, 1, 0}, ls, Vec3{1, 1, 0}, at, up);
    CHECK(b[0] == 63); // mirrored geometry still lines up
    b = sheen_vertex_bytes(m, Vec3{-1, 1, 0}, ls, viewer, at, up);
    CHECK(b[0] == 0 && b[1] == 0 && b[2] == 0);
    // c = L.R / |D|, p = 4 for shininess < 6: 60 degrees off the light gives 0.5^4.
    b = sheen_vertex_bytes(m, Vec3{0, 1, 0}, ls, Vec3{-std::sqrt(3.0f), 1, 0}, at, up);
    CHECK(b[0] == u8(std::floor(0.83f * 0.6f * 128.0f * 0.0625f)));
    // Shininess >= 12: p = 16. Scale 255 (chunk flag 0x100) is not clamped: the GS keeps the
    // low 8 bits (1.5 x 255 = 382 -> 126).
    scene::Material h = m;
    h.specular[0] = h.specular[1] = h.specular[2] = 1.0f;
    h.shininess = 20, h.scale = 255.0f / 128.0f;
    const float bright[3] = {1.5f, 1.0f, 0.5f};
    b = sheen_vertex_bytes(h, light, bright, viewer, at, up);
    CHECK(b[0] == 126 && b[1] == 255 && b[2] == 127);
    // Ms = 0 with a shininess of ~0 (Laguna's pavilion eaves): p 4, nothing added, also where
    // the light is behind (c = 0).
    scene::Material z = h;
    z.specular[0] = z.specular[1] = z.specular[2] = 0.0f;
    z.shininess = 6.6e-37f;
    b = sheen_vertex_bytes(z, light, bright, viewer, at, up);
    CHECK(b[0] == 0 && b[1] == 0 && b[2] == 0);
    b = sheen_vertex_bytes(z, Vec3{-1, 1, 0}, bright, viewer, at, up);
    CHECK(b[0] == 0 && b[1] == 0 && b[2] == 0);
}

TEST(render_guard_band_clip_matches_the_original) {
    // Super Speedway's road under the in-car camera (a PCSX2 GS dump of the grid): two strips,
    // (Y, X, A, B) and (A, E, Y, Y2). A, B, E, G 23 m ahead; X, Y, Y2 behind the eye (recovered
    // from the clipped corners). GT3's near plane for zoom sqrt(3): 0.1 x 0.7002 x zoom.
    const ClipPos A{-13.0384f, -2.4863f, 22.9095f}, B{-0.610632f, -2.48871f, 22.9885f};
    const ClipPos E{-18.2423f, -2.47221f, 22.8833f}, G{-37.8906f, -2.44521f, 22.7273f};
    const ClipPos X{-0.17547f, -2.48991f, -0.591788f}, Y{-12.6016f, -2.48993f, -0.64034f};
    const ClipPos Y2{-17.7792f, -2.48799f, -0.708794f};
    const float near_w = 0.1f * 0.7002f * std::sqrt(3.0f);
    struct Case {
        ClipPos v[3];
        int n;
        float screen[4][2]; // the corners as the GS received them (640 x 448, y down)
    };
    const Case cases[] = {
        {{Y, X, A}, 4, {{16.5f, 671.94f}, {-320.0f, 671.94f}, {137.88f, 248.31f}, {-320.0f, 311.56f}}},
        {{X, A, B}, 4, {{16.5f, 671.94f}, {266.19f, 671.94f}, {137.88f, 248.31f}, {311.5f, 248.25f}}},
        {{A, E, Y}, 4, {{137.88f, 248.31f}, {-320.0f, 311.56f}, {64.9f, 248.2f}, {-320.0f, 300.8f}}},
        {{E, Y, Y2}, 3, {{64.9f, 248.2f}, {-320.0f, 285.8f}, {-320.0f, 300.8f}}},
        {{E, G, Y2}, 4, {{64.9f, 248.2f}, {-320.0f, 285.8f}, {-213.5f, 248.1f}, {-320.0f, 258.2f}}},
    };
    for (const Case& c : cases) {
        ClipCorner out[kMaxClipCorners];
        const int n = guard_clip(c.v, near_w, 0, out);
        CHECK(n == c.n);
        for (int i = 0; i < n && i < c.n; ++i) {
            float x = 0, y = 0, w = 0;
            for (int k = 0; k < 3; ++k)
                x += out[i].b[k] * c.v[k].x, y += out[i].b[k] * c.v[k].y, w += out[i].b[k] * c.v[k].w;
            CHECK(near(320.0f + 320.0f * x / w, c.screen[i][0], 0.1f));
            CHECK(near(224.0f - 224.0f * y / w, c.screen[i][1], 0.1f));
        }
    }
    // The sheen's colour at the corners: the vertices' bytes lerped, + 0.5, truncated (A 14,
    // B 35, X 0 behind the eye): 1 and 3 on the bottom edge, as in the dump.
    ClipCorner out[kMaxClipCorners];
    const ClipPos tri[3] = {X, A, B};
    CHECK(guard_clip(tri, near_w, 0, out) == 4);
    const float bytes[3] = {0, 14, 35};
    auto byte_at = [&](int i) {
        float s = 0;
        for (int k = 0; k < 3; ++k) s += out[i].b[k] * bytes[k];
        return int(s + 0.5f);
    };
    CHECK(byte_at(0) == 1 && byte_at(1) == 3 && byte_at(2) == 14 && byte_at(3) == 35);
    // Inside the guard band (on screen or beside it): nothing to clip; past one screen edge: culled.
    CHECK(guard_flags(ClipPos{1.9f, -1.9f, 1.0f}, near_w, 0) == 0);
    CHECK(guard_flags(ClipPos{2.1f, 0.0f, 1.0f}, near_w, 0) == 0x01);
    CHECK(guard_flags(ClipPos{0.0f, 0.0f, 0.1f}, near_w, 0) == 0x20);
    const ClipPos right[3] = {{1.5f, 0, 1}, {3.0f, 0.2f, 1}, {2.5f, -0.3f, 1}};
    CHECK(guard_clip(right, near_w, 0, out) == 0);
}

TEST(render_guard_band_far_plane) {
    // GT3's far plane (Env0 +0x20, e.g. Super Speedway's 1000 m): the VU's +z flag past it, and
    // the plane cut after the near one, before the sides.
    const float near_w = 0.1f, far_w = 1000.0f;
    CHECK(guard_flags(ClipPos{0, 0, 1100}, near_w, far_w) == 0x10);
    CHECK(screen_flags(ClipPos{0, 0, 1100}, near_w, far_w) == 0x10);
    CHECK(guard_flags(ClipPos{0, 0, 1100}, near_w, 0) == 0); // no far plane
    CHECK(guard_flags(ClipPos{0, 0, 1000}, near_w, far_w) == 0); // on the plane: inside
    CHECK(guard_flags(ClipPos{2500, 0, 1100}, near_w, far_w) == (0x10 | 0x01));
    // A triangle reaching past it: cut where the far edges cross w = 1000, halfway.
    const ClipPos tri[3] = {{0, 0, 900}, {100, 0, 1100}, {-100, 0, 1100}};
    ClipCorner out[kMaxClipCorners];
    CHECK(guard_clip(tri, near_w, far_w, out) == 3);
    const float want[3][3] = {{1, 0, 0}, {0.5f, 0, 0.5f}, {0.5f, 0.5f, 0}}; // p0, p(n-1), p1
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) CHECK(near(out[i].b[k], want[i][k], 1e-6f));
    // Wholly beyond it: culled (its vertices share the flag).
    const ClipPos beyond[3] = {{0, 0, 1001}, {100, 0, 1100}, {-100, 0, 1500}};
    CHECK(guard_clip(beyond, near_w, far_w, out) == 0);
    CHECK(guard_clip(beyond, near_w, 0, out) == 3);
    // From behind the eye to past the far plane: both planes cut it, every corner between them.
    const ClipPos l[3] = {{0, -1, -1}, {50, -1, 2000}, {-50, -1, 2000}};
    const int n = guard_clip(l, near_w, far_w, out);
    CHECK(n == 4);
    for (int i = 0; i < n; ++i) {
        float w = 0;
        for (int k = 0; k < 3; ++k) w += out[i].b[k] * l[k].w;
        CHECK(w > near_w - 1e-3f && w < far_w + 1e-2f);
    }
}

TEST(render_gs_bilinear_matches_the_original) {
    // Route 5's wet-road buffer in a PCSX2 GS dump of the grid (software renderer, the targets
    // read back per draw). Blur copies: UV 0 / 1 at the pixel corners, so each pixel is the mean
    // of four texels, ((a + b) >> 1 + (c + d) >> 1) >> 1, where a GPU rounds (17, 17, 12).
    const u8 a[4] = {6, 1, 33, 26}, b[4] = {19, 26, 10, 12}, c[4] = {21, 16, 6, 5};
    CHECK(gs_bilinear_texel(a, 2, 2, 1.0f, 1.0f) == 16);
    CHECK(gs_bilinear_texel(b, 2, 2, 1.0f, 1.0f) == 16);
    CHECK(gs_bilinear_texel(c, 2, 2, 1.0f, 1.0f) == 11);
    // The composite: frame pixel (203, 270) samples texel (20.3, 77.29) of the 64 x 128 buffer
    // with 4-bit weights (12, 12), 31 where the exact filter gives 31.6; times its frame alpha
    // 51 it adds (31 x 51) >> 7 = 12, as the dump shows.
    const u8 d[4] = {29, 32, 30, 32};
    CHECK(gs_bilinear_texel(d, 2, 2, 20.3f - 19.0f, 77.2857f - 76.0f) == 31);
    // On a texel centre: the texel; outside the image: the edge texel.
    CHECK(gs_bilinear_texel(d, 2, 2, 0.5f, 1.5f) == 30);
    CHECK(gs_bilinear_texel(d, 2, 2, -3.0f, -3.0f) == 29);
}

// [video] gamma: a number in range, anything else 1 (GT3's colours, no pass).
TEST(render_gamma_setting) {
    CHECK(parse_gamma("1.25") == 1.25f);
    CHECK(parse_gamma("") == 1.0f);
    CHECK(parse_gamma("bright") == 1.0f);
    CHECK(parse_gamma("nan") == 1.0f);
    CHECK(parse_gamma("0.1") == kGammaMin);
    CHECK(parse_gamma("9") == kGammaMax);
    CHECK(!gamma_changes(parse_gamma("1.00")));
    CHECK(gamma_changes(1.05f) && gamma_changes(0.95f));
}
