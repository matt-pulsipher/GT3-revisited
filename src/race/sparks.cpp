#include "race/sparks.h"

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

// The colour keys at P +0x38 .. +0x84: r, g, b, a and the life remaining (s) where the
// key starts.
struct Key {
    float r, g, b, a, t;
};
constexpr Key kKeys[4] = {{1.0f, 0.599999011f, 0.199999005f, 1.0f, 0.800000012f},
                          {0.999997973f, 0.499998003f, 0.0f, 1.0f, 0.399998009f},
                          {0.799997985f, 0.0f, 0.0f, 1.0f, 0.149997994f},
                          {0.199999005f, 0.0f, 0.0f, 1.0f, 0.0f}};

// GS colour (0x80 = 1.0 under MODULATE) to ours (255 = 1.0), alpha 1: the blend ignores it.
u32 vertex_colour(u32 gs) {
    auto ch = [&](int i) { return std::min<u32>(255, ((gs >> (8 * i)) & 0xFF) * 2); };
    return ch(0) | ch(1) << 8 | ch(2) << 16 | 0xFF000000u;
}

render::Vec3 lerp(const render::Vec3& a, const render::Vec3& b, float t) { return a + (b - a) * t; }

} // namespace

u32 spark_colour(float life) {
    for (int i = 0; i < 4; ++i) {
        const Key& k = kKeys[i];
        if (!(k.t < life)) continue;
        float c[4] = {k.r, k.g, k.b, k.a};
        if (i > 0) {
            const Key& p = kKeys[i - 1];
            const float f = (p.t - life) / (p.t - k.t);
            c[0] = p.r * (1 - f) + k.r * f, c[1] = p.g * (1 - f) + k.g * f;
            c[2] = p.b * (1 - f) + k.b * f, c[3] = p.a * (1 - f) + k.a * f;
        }
        u32 out = 0;
        for (int j = 0; j < 4; ++j) out |= (u32(s32(c[j] * 127.5f)) & 0xFFu) << (8 * j);
        return out;
    }
    return 0;
}

SparkDraw::SparkDraw() = default;
SparkDraw::~SparkDraw() = default;

bool SparkDraw::load(render::GpuContext& ctx, const Vfs& vfs) {
    gpu_.reset();
    clear();
    std::optional<Bytes> img = vfs.read("race/particle.img");
    if (!img) return false;
    scene::Model m;
    m.name = "sparks";
    try {
        Tex1 t = Tex1::parse(*img);
        GsMemory gs;
        t.upload(*img, gs);
        Tex1::Image im = t.decode(gs, 0);
        texture_pack::replace_decoded(*img, 0, im);
        scene::Texture st;
        st.name = "particle";
        st.width = im.width, st.height = im.height;
        st.rgba = std::move(im.rgba);
        for (u32& c : st.rgba) c |= 0xFF000000u; // (TCC 0: the texture's alpha is not used)
        st.clamp_u = st.clamp_v = true;
        st.no_mips = true;
        m.textures.push_back(std::move(st));
    } catch (const std::exception& e) {
        LOG_WARN("Sparks: race/particle.img: %s", e.what());
        return false;
    }
    scene::Mesh mesh;
    mesh.name = "streaks";
    verts_.assign(size_t(sim::exact::Sparks::kPool) * 4, scene::Vertex{});
    for (size_t i = 0; i < verts_.size(); ++i) // spread like the smoke's: the bounds never cull the item
        verts_[i].pos[0] = (i & 1) ? 5000.0f : -5000.0f;
    mesh.vertices = verts_;
    for (u32 q = 0; q < u32(sim::exact::Sparks::kPool); ++q)
        for (u32 k : {0u, 1u, 2u, 2u, 1u, 3u}) mesh.indices.push_back(q * 4 + k); // the strip v0 v1 v2 v3
    scene::Primitive p;
    p.index_count = u32(mesh.indices.size());
    p.texture = 0;
    p.flags = scene::kAdditive | scene::kUnlit | scene::kNoDepthWrite | scene::kDoubleSided;
    mesh.primitives.push_back(p);
    m.meshes.push_back(std::move(mesh));
    m.compute_bounds();
    gpu_ = std::make_unique<render::GpuModel>(ctx, m);
    return true;
}

void SparkDraw::clear() {
    for (Slot& s : slots_) s = {};
    last_t_ = -1;
}

void SparkDraw::add_items(std::vector<render::DrawItem>& items, const sim::exact::Sparks& s, const render::Mat4& view,
                          float alpha, bool cut) {
    if (!gpu_) return;
    // The draw's clock in race frames; a race built again counts from 0 (its particles are all new).
    const double t = double(s.frames()) + double(alpha);
    if (cut || t < last_t_) clear();
    last_t_ = t;
    // Eye space (view rows: right, up, back) and back.
    const render::Vec3 right{view.at(0, 0), view.at(0, 1), view.at(0, 2)}, up{view.at(1, 0), view.at(1, 1), view.at(1, 2)},
        back{view.at(2, 0), view.at(2, 1), view.at(2, 2)};
    const render::Vec3 origin = (right * view.at(0, 3) + up * view.at(1, 3) + back * view.at(2, 3)) * -1.0f;
    auto to_world = [&](float x, float y, float z) { return origin + right * x + up * y + back * z; };
    const auto& pool = s.pool();
    size_t n = 0;
    for (int k = s.first(); k >= 0; k = pool[size_t(k)].next) {
        const sim::exact::Spark& p = pool[size_t(k)];
        Slot& sl = slots_[size_t(k)];
        if (sl.serial != p.serial) sl = {}, sl.serial = p.serial;
        const render::Vec3 w = lerp({p.last.x.host(), p.last.y.host(), p.last.z.host()},
                                    {p.pos.x.host(), p.pos.y.host(), p.pos.z.host()}, alpha);
        const render::Vec3 e1 = view.transform_point(w);
        // Where it was one race frame before: between the drawn positions around that time (the oldest one when it is
        // younger); none yet: not drawn this time.
        bool have = sl.n > 0 && sl.t[0] < t;
        render::Vec3 e0 = e1;
        if (have) {
            const double t0 = t - 1.0;
            if (t0 <= sl.t[0]) {
                e0 = sl.eye[0];
            } else {
                size_t i = 0;
                while (i + 1 < sl.n && sl.t[i + 1] <= t0) ++i;
                const double tb = i + 1 < sl.n ? sl.t[i + 1] : t;
                const render::Vec3 eb = i + 1 < sl.n ? sl.eye[i + 1] : e1;
                e0 = lerp(sl.eye[i], eb, float((t0 - sl.t[i]) / std::max(tb - sl.t[i], 1e-9)));
            }
        }
        if (sl.n > 0 && sl.t[sl.n - 1] >= t) {
            sl.eye[sl.n - 1] = e1; // (the same time again: the camera may have moved)
        } else {
            if (sl.n == kHistory) {
                std::move(sl.t.begin() + 1, sl.t.end(), sl.t.begin());
                std::move(sl.eye.begin() + 1, sl.eye.end(), sl.eye.begin());
                --sl.n;
            }
            sl.t[sl.n] = t, sl.eye[sl.n] = e1, ++sl.n;
        }
        if (!have) continue;
        const u32 col = spark_colour(p.life.host());
        if (col == 0) continue;
        // The streak from e0 to e1, w its half-width (a square of 2w when it has no length on screen).
        const float hw = p.size.host();
        const float dx = e1.x - e0.x, dy = e1.y - e0.y, l2 = dx * dx + dy * dy;
        render::Vec3 c[4];
        if (l2 == 0) {
            c[0] = {e0.x + hw, e0.y + hw, e0.z}, c[1] = {e0.x - hw, e0.y + hw, e0.z};
            c[2] = {e0.x + hw, e0.y - hw, e0.z}, c[3] = {e0.x - hw, e0.y - hw, e0.z};
        } else {
            const float r = hw / std::sqrt(l2), ex = dx * r, ey = dy * r;
            c[0] = {e0.x - ex - ey, e0.y - ey + ex, e0.z}, c[1] = {e0.x - ex + ey, e0.y - ey - ex, e0.z};
            c[2] = {e1.x + ex - ey, e1.y + ey + ex, e1.z}, c[3] = {e1.x + ex + ey, e1.y + ey - ex, e1.z};
        }
        const float uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        const u32 vc = vertex_colour(col);
        for (int j = 0; j < 4; ++j) {
            scene::Vertex& v = verts_[n * 4 + size_t(j)];
            const render::Vec3 q = to_world(c[j].x, c[j].y, c[j].z);
            v.pos[0] = q.x, v.pos[1] = q.y, v.pos[2] = q.z;
            v.normal[0] = back.x, v.normal[1] = back.y, v.normal[2] = back.z;
            v.uv[0] = uv[j][0], v.uv[1] = uv[j][1];
            v.color = vc;
        }
        ++n;
    }
    if (n == 0) return;
    for (size_t i = n * 4; i < verts_.size(); ++i) { // unused: a black point (added: nothing)
        scene::Vertex& v = verts_[i];
        v.pos[0] = verts_[0].pos[0], v.pos[1] = verts_[0].pos[1], v.pos[2] = verts_[0].pos[2];
        v.color = 0;
    }
    gpu_->update_vertices(verts_);
    render::DrawItem it{gpu_.get(), {}, 0};
    it.helper = true; // never culled (positions change every frame); unfogged, as GT3's
    items.push_back(it);
}

} // namespace gt3r::race
