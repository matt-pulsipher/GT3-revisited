#include "race/sun_disc.h"

#include "race/lamp_glows.h"
#include "race/sun_glare.h"

namespace gt3r::race {

namespace {
// The disc's point is 10 x the sun direction from the camera and its size 10 x Env0 +0x9D4
// (f32 10.0); its colour is RGB x gain x 0.5.
constexpr float kDiscDistance = 10.0f;
} // namespace

scene::Model SunDisc::build(const SunParams& p) {
    scene::Model m;
    m.name = "sun_disc";
    // The program draws nothing for a size x 10 <= 0 (Super Speedway's 0, the sunless courses).
    if (!p.enabled || !(p.disc_size * kDiscDistance > 0)) return m;
    const float g = p.disc_gain * 0.5f;
    const std::array<float, 3> col{p.rgb[0] * g, p.rgb[1] * g, p.rgb[2] * g};
    const Flare::Mesh& disc = Flare::disc_mesh();
    scene::Mesh mesh;
    mesh.name = "sun_disc";
    for (const Flare::Vertex& v : disc.verts) {
        scene::Vertex sv;
        sv.pos[0] = v.x, sv.pos[1] = v.y, sv.pos[2] = v.z;
        sv.normal[2] = 1;
        // Full visibility: the program passes the 0xFF words, not an occlusion result.
        const std::array<u8, 4> c = Flare::gs_color(v, col, 1.0f);
        sv.color = u32(c[0]) | u32(c[1]) << 8 | u32(c[2]) << 16 | u32(c[3]) << 24;
        mesh.vertices.push_back(sv);
    }
    for (int t : disc.tris) mesh.indices.push_back(u32(t));
    scene::Primitive prim;
    prim.index_count = u32(mesh.indices.size());
    // ALPHA 0x48 (Cs As + Cd), depth test and write off, alpha test off, fog off.
    prim.flags = scene::kAdditive | scene::kUnlit | scene::kNoDepthWrite | scene::kDoubleSided;
    mesh.primitives.push_back(prim);
    m.meshes.push_back(std::move(mesh));
    m.compute_bounds();
    return m;
}

void SunDisc::load(render::GpuContext& ctx, const SunParams& p) {
    gpu_.reset();
    scene::Model m = build(p);
    if (m.meshes.empty()) return;
    dir_ = p.direction();
    size_ = p.disc_size;
    gpu_ = std::make_unique<render::GpuModel>(ctx, m);
}

render::Mat4 SunDisc::transform(const render::Vec3& eye, const render::Vec3& sun_dir, float disc_size,
                                const render::Vec3& right, const render::Vec3& up) {
    // The camera's rotation, translation cleared, Translate(10 d), the rotation
    // reset to identity (screen-aligned in eye space), Scale(10 x size).
    const float s = kDiscDistance * disc_size;
    const render::Vec3 back = render::cross(right, up);
    const render::Vec3 c = eye + sun_dir * kDiscDistance;
    render::Mat4 t;
    for (int i = 0; i < 3; ++i) {
        t.at(i, 0) = (&right.x)[i] * s;
        t.at(i, 1) = (&up.x)[i] * s;
        t.at(i, 2) = (&back.x)[i] * s;
        t.at(i, 3) = (&c.x)[i];
    }
    return t;
}

void SunDisc::add_items(std::vector<render::DrawItem>& items, const render::Vec3& eye, const render::Vec3& right,
                        const render::Vec3& up) const {
    if (!gpu_) return;
    render::DrawItem it{gpu_.get(), transform(eye, dir_, size_, right, up), 0};
    it.background = true; // after the sky dome (the item before it), before everything else
    items.push_back(it);
    items.push_back(it); // the second half of As / 128
}

} // namespace gt3r::race
