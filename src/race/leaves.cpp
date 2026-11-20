#include "race/leaves.h"

#include "base/log.h"
#include "formats/gs_memory.h"
#include "formats/tex1.h"
#include "formats/texture_pack.h"
#include "render/gpu_model.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>

namespace gt3r::race {

namespace {

constexpr float kTwoPi = 6.28318531f;

float blend_angle(float from, float to, float t) { // across the wrap at 2 pi the short way
    return from + std::remainder(to - from, kTwoPi) * t;
}

// GS color (0x80 = 1.0 under MODULATE) to ours (255 = 1.0).
u32 vertex_color(u32 gs) {
    auto ch = [&](int i) { return std::min<u32>(255, ((gs >> (8 * i)) & 0xFF) * 2); };
    return ch(0) | ch(1) << 8 | ch(2) << 16 | ch(3) << 24;
}

} // namespace

u32 leaf_color(const render::Vec3& n, bool near_side, float a, const LeafLight& l, float base, float gain) {
    const float x = l.dir[0], y = l.dir[1], z = l.dir[2];
    const float len2 = x * x + y * y + z * z;
    const float r = len2 > 0 ? -1.0f / std::sqrt(len2) : 0.0f; // (rsqrt of -1)
    float c = (x * n.x + y * n.y + z * n.z) * r * (a * gain + base);
    if (!near_side) c = -c;
    c = std::max(c, 0.0f);
    u32 out = 0x80000000u;
    for (int i = 0; i < 3; ++i) {
        // trunc(x 128), at most 0x80 (a negative sum, which no course's light 0 gives, would spill into the other
        // channels in the program: ours stops at 0)
        const float v = (l.diffuse[size_t(i)] * c + l.ambient[size_t(i)] * a) * 128.0f;
        const s32 q = v >= 128.0f ? 0x80 : v > 0 ? s32(v) : 0;
        out |= u32(q) << (8 * i);
    }
    return out;
}

LeafDraw::LeafDraw() = default;
LeafDraw::~LeafDraw() = default;

bool LeafDraw::load(render::GpuContext& ctx, const Vfs& vfs) {
    gpu_.reset();
    std::optional<Bytes> img = vfs.read("race/particle.img");
    if (!img) return false;
    scene::Model m;
    m.name = "leaves";
    try {
        Tex1 t = Tex1::parse(*img);
        if (t.textures.size() <= kShapes) return false; // (GT3's particle image: the sparks' glow alone)
        GsMemory gs;
        t.upload(*img, gs);
        for (size_t k = 1; k <= kShapes; ++k) {
            Tex1::Image im = t.decode(gs, k);
            texture_pack::replace_decoded(*img, k, im);
            scene::Texture st;
            st.name = "leaf" + std::to_string(k);
            st.width = im.width, st.height = im.height;
            st.rgba = std::move(im.rgba);
            st.clamp_u = st.clamp_v = true;
            m.textures.push_back(std::move(st));
        }
    } catch (const std::exception& e) {
        LOG_WARN("Leaves: race/particle.img: %s", e.what());
        return false;
    }
    constexpr size_t kPool = size_t(sim::exact::Sparks::kPool);
    scene::Mesh mesh;
    mesh.name = "leaves";
    verts_.assign(kShapes * kPool * 4, scene::Vertex{});
    for (size_t i = 0; i < verts_.size(); ++i) // spread like the sparks': the bounds never cull the item
        verts_[i].pos[0] = (i & 1) ? 5000.0f : -5000.0f;
    mesh.vertices = verts_;
    for (size_t k = 0; k < kShapes; ++k) {
        scene::Primitive p;
        p.first_index = u32(mesh.indices.size());
        for (u32 q = 0; q < u32(kPool); ++q)
            for (u32 c : {0u, 1u, 2u, 2u, 1u, 3u}) mesh.indices.push_back(u32((k * kPool + q) * 4) + c); // the strip v0 v1 v2 v3
        p.index_count = u32(mesh.indices.size()) - p.first_index;
        p.texture = int(k);
        p.flags = scene::kAlphaTest | scene::kUnlit | scene::kDoubleSided;
        mesh.primitives.push_back(p);
    }
    m.meshes.push_back(std::move(mesh));
    m.compute_bounds();
    gpu_ = std::make_unique<render::GpuModel>(ctx, m);
    return true;
}

void LeafDraw::add_items(std::vector<render::DrawItem>& items, const sim::exact::Sparks& s, float alpha,
                         const render::Mat4& view, const LeafLight& light) {
    if (!gpu_ || !s.leaves_on()) return;
    // The point the face test measures from: the view's translation negated (in the view's own axes).
    const render::Vec3 from{-view.at(0, 3), -view.at(1, 3), -view.at(2, 3)};
    const float lb = s.leaf_rules().light_base.host(), lg = s.leaf_rules().light_gain.host();
    constexpr size_t kPool = size_t(sim::exact::Sparks::kPool);
    std::array<size_t, kShapes> used{};
    bool any = false;
    for (int k = s.first(); k >= 0; k = s.pool()[size_t(k)].next) {
        if (!s.is_leaf(k)) continue;
        const sim::exact::Leaf& p = s.leaves()[size_t(k)];
        const size_t shape = std::min<size_t>(p.shape, kShapes - 1);
        const sim::exact::LeafShape& sh = s.leaf_rules().shapes[shape];
        // Its turn and place between the last update and this one (the matrix: rows that are the leaf's axes).
        const float a = blend_angle(p.last_angle.x.host(), p.angle.x.host(), alpha);
        const float b = blend_angle(p.last_angle.y.host(), p.angle.y.host(), alpha);
        const float c = blend_angle(p.last_angle.z.host(), p.angle.z.host(), alpha);
        const float sa = std::sin(a), ca = std::cos(a), sb = std::sin(b), cb = std::cos(b), sc = std::sin(c), cc = std::cos(c);
        const render::Vec3 ax{cb * cc - sa * sc * sb, -ca * sc, sb * cc + sa * sc * cb};
        const render::Vec3 ay{cb * sc + sa * cc * sb, ca * cc, sb * sc - sa * cc * cb};
        const render::Vec3 az{-ca * sb, sa, ca * cb};
        const render::Vec3 at{p.last_pos.x.host() + (p.pos.x.host() - p.last_pos.x.host()) * alpha,
                              p.last_pos.y.host() + (p.pos.y.host() - p.last_pos.y.host()) * alpha,
                              p.last_pos.z.host() + (p.pos.z.host() - p.last_pos.z.host()) * alpha};
        const bool near_side = render::dot(at - from, ay) < 0;
        const u32 col = vertex_color(leaf_color(ay, near_side, p.shade.host(), light, lb, lg));
        const float uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        const size_t base = (shape * kPool + used[shape]++) * 4;
        for (size_t j = 0; j < 4; ++j) {
            const sim::exact::Vec3& q = sh.corners[j];
            const render::Vec3 w = at + ax * q.x.host() + az * q.z.host();
            scene::Vertex& v = verts_[base + j];
            v.pos[0] = w.x, v.pos[1] = w.y, v.pos[2] = w.z;
            v.normal[0] = ay.x, v.normal[1] = ay.y, v.normal[2] = ay.z; // (unlit: the color is the leaf's)
            v.uv[0] = uv[j][0], v.uv[1] = uv[j][1];
            v.color = col;
        }
        any = true;
    }
    if (!any) return;
    for (size_t k = 0; k < kShapes; ++k) // unused: a transparent point
        for (size_t i = (k * kPool + used[k]) * 4; i < (k + 1) * kPool * 4; ++i) {
            scene::Vertex& v = verts_[i];
            v.pos[0] = from.x, v.pos[1] = from.y - 1000.0f, v.pos[2] = from.z;
            v.color = 0;
        }
    gpu_->update_vertices(verts_);
    render::DrawItem it{gpu_.get(), {}, 0};
    it.helper = true; // never culled (positions change every frame)
    items.push_back(it);
}

} // namespace gt3r::race
