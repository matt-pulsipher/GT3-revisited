#include "race/tyre_smoke.h"

#include "base/log.h"
#include "formats/gs_memory.h"
#include "formats/tex1.h"
#include "formats/texture_pack.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace gt3r::race {

namespace {
float rdf(ByteSpan d, size_t o) {
    u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
constexpr float kT1[8] = {0.9953f, 0.0044f, 0.1186f, 0.5101f, 0.0353f, 0.7672f, 0.2763f, 0.9419f};
constexpr float kT2[8] = {0.0066f, 1.0f, 0.7265f, 0.4041f, 0.9955f, 0.177f, 0.6997f, 0.0529f};
// Surface index -> smoke class: 0, 1, 7..11 tarmac; 2 grass; 3 gravel; 4 sand; 5 dirt; 6, 12+ wet.
int class_of(int s) {
    if (s == 2) return 1;
    if (s == 3) return 2;
    if (s == 4) return 3;
    if (s == 5) return 4;
    if (s == 6 || s >= 12) return 5;
    return 0;
}
} // namespace

float TyreSmoke::rand01() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return float(rng_ >> 8) / 16777216.0f;
}

bool TyreSmoke::load(render::GpuContext& ctx, ByteSpan e, ByteSpan tex) {
    gpu_.reset();
    gpu_blend_.reset();
    clear();
    if (e.size() < 0x920 || std::memcmp(e.data(), "Env0", 4) != 0 || tex.empty() || !Tex1::is_tex1(tex)) return false;
    for (size_t c = 0; c < 6; ++c) {
        size_t o = 0x4D0 + 0x44 * c;
        Class& k = classes_[c];
        for (int i = 0; i < 3; ++i) k.rgb[i] = rdf(e, o + 4 * size_t(i));
        k.opacity = rdf(e, o + 0x0C), k.size = rdf(e, o + 0x10), k.growth = rdf(e, o + 0x14), k.vy = rdf(e, o + 0x18);
        k.spin = rdf(e, o + 0x1C), k.fade = rdf(e, o + 0x20), k.gravity = rdf(e, o + 0x2C), k.alphamix = rdf(e, o + 0x30);
        k.n = rd32(e, o + 0x40);
    }
    shade_floor_ = rdf(e, 0x910);
    // +0x914 / +0x91C: the x / y divisors of GT3's reduced smoke buffer (each 1 when <= 0 or
    // > 64; only route5 / r_route5 have 2 / 2): the puffs are drawn over black into a buffer
    // that is then added to the frame (sum, alpha ignored), without the ground-line split. We
    // add them straight to the frame (the same where puffs do not overlap; full resolution).
    auto divisor = [&](size_t o) {
        s32 v = s32(rd32(e, o));
        return v <= 0 || v > 64 ? 1 : v;
    };
    additive_ = divisor(0x914) != 1 || divisor(0x91C) != 1;
    scene::Model m;
    m.name = "tyre_smoke";
    try {
        Tex1 t = Tex1::parse(tex);
        GsMemory gs;
        t.upload(tex, gs);
        Tex1::Image img = t.decode(gs, 0);
        texture_pack::replace_decoded(tex, 0, img);
        scene::Texture st;
        st.name = "smoke";
        st.width = img.width, st.height = img.height;
        st.rgba = std::move(img.rgba);
        // (decode() already maps the GS alpha 0..0x80 to 0..255: no second doubling.)
        st.clamp_u = st.clamp_v = true;
        m.textures.push_back(std::move(st));
    } catch (const std::exception& ex) {
        LOG_WARN("Tyre smoke: %s", ex.what());
        return false;
    }
    scene::Mesh mesh;
    mesh.name = "puffs";
    mesh.vertices.resize(puffs_.size() * kVerts);
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        // Spread the initial placement so the bounding sphere never culls the item (drawn as a helper).
        mesh.vertices[i].pos[0] = (i & 1) ? 5000.0f : -5000.0f;
        mesh.vertices[i].normal[1] = 1;
    }
    // Per puff two polygon fans of up to 5 vertices (the parts above and below the ground line;
    // unused vertices collapse onto the fan's first one).
    for (u32 q = 0; q < puffs_.size(); ++q)
        for (u32 f = 0; f < 2; ++f) {
            u32 b = q * kVerts + f * 5;
            mesh.indices.insert(mesh.indices.end(), {b, b + 1, b + 2, b, b + 2, b + 3, b, b + 3, b + 4});
        }
    scene::Primitive p;
    p.index_count = u32(mesh.indices.size());
    p.texture = 0;
    p.flags = (additive_ ? scene::kAdditive : scene::kAlphaBlend) | scene::kUnlit | scene::kNoDepthWrite | scene::kDoubleSided;
    mesh.primitives.push_back(p);
    m.meshes.push_back(std::move(mesh));
    m.compute_bounds();
    verts_ = m.meshes[0].vertices;
    gpu_ = std::make_unique<render::GpuModel>(ctx, m);
    if (additive_) { // the mirror draws the puffs straight into its picture with ALPHA 0x44 (Env0 +0x918)
        m.meshes[0].primitives[0].flags =
            scene::kAlphaBlend | scene::kUnlit | scene::kNoDepthWrite | scene::kDoubleSided;
        gpu_blend_ = std::make_unique<render::GpuModel>(ctx, m);
    }
    return true;
}

void TyreSmoke::clear() {
    for (Puff& p : puffs_) p.live = false;
}

size_t TyreSmoke::live() const {
    return size_t(std::count_if(puffs_.begin(), puffs_.end(), [](const Puff& p) { return p.live; }));
}

void TyreSmoke::emit(const render::Vec3& contact, int surface_index, float intensity, float sun, bool right_side) {
    if (!gpu_ || intensity <= 0.001f) return;
    int c = class_of(surface_index);
    const Class& k = classes_[size_t(c)];
    if (k.opacity < 0.01f) return;
    Puff* slot = nullptr;
    for (Puff& p : puffs_)
        if (!p.live) {
            slot = &p;
            break;
        }
    if (!slot) { // steal the weakest
        float best = 1e30f;
        for (Puff& p : puffs_)
            if (p.strength * p.weight < best) best = p.strength * p.weight, slot = &p;
    }
    Puff& p = *slot;
    p = {};
    p.live = true;
    p.cls = c;
    p.right = right_side;
    p.k = std::min(7, int(rand01() * 8));
    p.strength = std::min(1.0f, intensity) * kT1[p.k];
    float shade = shade_floor_ + (1 - shade_floor_) * std::clamp(sun, 0.0f, 1.0f);
    for (int i = 0; i < 3; ++i) p.rgb[i] = k.rgb[i] * shade;
    p.size = k.size * p.strength;
    p.vy = k.vy * (1 + (rand01() - 0.2f) * 0.3f);
    p.rot = rand01() * 360.0f;
    p.weight = p.strength + (1 - p.strength) * k.alphamix;
    p.fade_m = std::min(255.0f, 1 + rand01() * (float(k.n) + 0.2f));
    p.grow_m = 1 + (rand01() - 0.5f) * 0.2f;
    p.pos = contact;
    p.ground = contact;
}

void TyreSmoke::update(double dt) {
    float h = float(dt);
    for (Puff& p : puffs_) {
        if (!p.live) continue;
        const Class& k = classes_[size_t(p.cls)];
        p.rot += (p.right ? 1.0f : -1.0f) * k.spin * kT2[p.k] / std::min(std::max(p.size, 1e-3f), 1.0f) * h;
        p.rot = std::fmod(p.rot + 360.0f, 360.0f);
        p.size += k.growth * p.grow_m * h;
        p.strength -= k.fade * p.fade_m * h;
        p.vy -= 9.81f * k.gravity * h;
        p.pos.y += p.vy * h;
        if (p.strength <= 0 || p.pos.y + 1.2071f * p.size < p.ground.y) p.live = false;
    }
}

void TyreSmoke::add_items(std::vector<render::DrawItem>& items, const render::Vec3& eye, const render::Vec3& right,
                          const render::Vec3& up, const View& view, bool mirror) {
    if (!gpu_) return;
    std::vector<u32> order;
    for (u32 i = 0; i < puffs_.size(); ++i)
        if (puffs_[i].live && visible(view, render::dot(puffs_[i].pos - eye, view.forward), puffs_[i].size))
            order.push_back(i);
    if (order.empty()) return;
    std::sort(order.begin(), order.end(), [&](u32 a, u32 b) {
        return render::length(puffs_[a].pos - eye) > render::length(puffs_[b].pos - eye);
    });
    std::fill(verts_.begin(), verts_.end(), scene::Vertex{});
    for (size_t q = 0; q < order.size(); ++q) {
        const Puff& p = puffs_[order[q]];
        const Class& k = classes_[size_t(p.cls)];
        float a = std::min(1.0f, 1.99f * p.strength * p.weight * k.opacity);
        u32 rgb = u32(std::clamp(p.rgb[0], 0.0f, 1.0f) * 255) | u32(std::clamp(p.rgb[1], 0.0f, 1.0f) * 255) << 8 |
                  u32(std::clamp(p.rgb[2], 0.0f, 1.0f) * 255) << 16;
        float r = p.rot * 3.14159265f / 180.0f, cs = std::cos(r), sn = std::sin(r);
        render::Vec3 ax = (right * cs + up * sn) * p.size, ay = (up * cs - right * sn) * p.size;
        float u0 = 0.0039f + 0.25f * float(p.k & 3), u1 = 0.2461f + 0.25f * float(p.k & 3);
        struct V {
            render::Vec3 pos;
            float u, v, a;
        };
        // Corners in fan order.
        V quad[4];
        const float cx[4] = {-1, 1, 1, -1}, cy[4] = {-1, -1, 1, 1};
        for (int c = 0; c < 4; ++c)
            quad[c] = {p.pos + ax * cx[c] + ay * cy[c], cx[c] < 0 ? u0 : u1, cy[c] < 0 ? 0.9922f : 0.0078f, a};
        auto put = [&](int fan, const std::vector<V>& poly) {
            for (int i = 0; i < 5; ++i) {
                const V& s = poly.empty() ? quad[0] : poly[size_t(std::min(i, int(poly.size()) - 1))];
                scene::Vertex& v = verts_[q * kVerts + size_t(fan * 5 + i)];
                v.pos[0] = s.pos.x, v.pos[1] = s.pos.y, v.pos[2] = s.pos.z;
                v.normal[1] = 1;
                v.uv[0] = s.u, v.uv[1] = s.v;
                v.color = poly.empty() ? 0u : rgb | u32(std::clamp(s.a, 0.0f, 1.0f) * 255) << 24;
            }
        };
        // GT3: puffs up to 3 m that cross the ground plane are split along it; the part below
        // fades to alpha 0 away from the ground line (a soft edge where the cloud meets the road).
        float g = p.ground.y - 0.05f;
        bool crosses = false, above = false, below = false;
        for (const V& c : quad) (c.pos.y >= g ? above : below) = true;
        crosses = above && below;
        if (p.size > 3.0f || !crosses || additive_ || mirror) {
            put(0, {quad[0], quad[1], quad[2], quad[3]});
            put(1, {});
            continue;
        }
        std::vector<V> up_poly, down_poly;
        for (int i = 0; i < 4; ++i) {
            const V& A = quad[i];
            const V& B = quad[(i + 1) % 4];
            float da = A.pos.y - g, db = B.pos.y - g;
            (da >= 0 ? up_poly : down_poly).push_back(A);
            if ((da >= 0) != (db >= 0)) {
                float t = da / (da - db);
                V m{A.pos + (B.pos - A.pos) * t, A.u + (B.u - A.u) * t, A.v + (B.v - A.v) * t, a};
                up_poly.push_back(m);
                down_poly.push_back(m);
            }
        }
        for (V& d : down_poly) // fade from the line (alpha a) to 0 one puff-size below it
            d.a = a * std::clamp(1.0f + (d.pos.y - g) / std::max(p.size, 1e-3f), 0.0f, 1.0f);
        put(0, up_poly);
        put(1, down_poly);
    }
    render::GpuModel* gpu = mirror && gpu_blend_ ? gpu_blend_.get() : gpu_.get();
    gpu->update_vertices(verts_);
    render::DrawItem it{gpu, {}, 0};
    it.helper = true; // never culled (positions change every frame)
    it.late = true;   // after the whole scene and the heat haze (presentation §L6)
    items.push_back(it);
}

} // namespace gt3r::race
