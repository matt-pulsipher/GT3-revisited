// Renderer workstream tests (CPU only: math, camera, procedural models).

#include "render/camera.h"
#include "render/dynamic_scale.h"
#include "render/gpu_context.h"
#include "render/gpu_model.h"
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
