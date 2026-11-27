#include "race/wheel_fx.h"

#include "scene/model.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace gt3r::race {

namespace {

float rdf(ByteSpan d, size_t o) {
    u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

// The program's constants: the level thresholds (0x350990..0x350998), 1024 / pi (0x3509A4: the
// tread's travel per radian in 1/16 texels of a 128-texel repeat) and 3 degrees (0x3509A8).
constexpr float kDetail[3] = {0.09f, 0.03f, 0.015f};
constexpr float kTreadPerRadian = 325.94931f;
constexpr float kSpinningImage = 0.052359879f;

} // namespace

WheelEnv WheelEnv::parse(ByteSpan env0) {
    WheelEnv e;
    if (!has_magic(env0, "Env0") || env0.size() < 0xEA8) return e;
    e.glow_gain = rdf(env0, 0x4C);
    e.blur_time = rdf(env0, 0xEA0);
    e.blur_falloff = rdf(env0, 0xEA4);
    return e;
}

int wheel_detail(float proj_x, float bound_radius, float depth) {
    const float s = depth > 0 ? proj_x * bound_radius / depth : 1e9f;
    return s > kDetail[0] ? 0 : s > kDetail[1] ? 1 : s > kDetail[2] ? 2 : 3;
}

float wheel_blur_turn(float spin_step, float moved, const WheelEnv& env) {
    return -spin_step * (env.blur_time * std::exp(env.blur_falloff * moved));
}

bool tread_blur_wanted(u32 tyre_flags, bool outer_face_seen) {
    if (tyre_flags & 2) return (tyre_flags & 1) != 0;
    return outer_face_seen && (tyre_flags & 1) != 0;
}

std::array<float, 3> tread_blur_shifts(float turn, u32 repeats, bool right) {
    float f = turn * 0.5f * (float(repeats) * kTreadPerRadian);
    if (right) f = -f;
    std::array<float, 3> out{};
    for (float& s : out) {
        f *= 0.5f;
        // 1/16 texels, wrapped at 128 texels (the EE's conversion saturates)
        const s32 units = s32(std::trunc(std::clamp(f, -2.0e9f, 2.0e9f))) & 0x7FF;
        s = float(units) * 0.0625f / 128.0f;
    }
    return out;
}

std::array<float, 4> rim_blur_angles(float turn, bool right) {
    float a = turn * 0.5f;
    if (right) a = -a;
    std::array<float, 4> out{};
    for (float& x : out) x = a, a *= 0.5f;
    return out;
}

float rim_blur_lod(u32 texture_width) {
    return texture_width > 64 ? std::log2(float(texture_width) / 64.0f) : 0.0f;
}

bool rim_spinning_image(float turn) { return turn > kSpinningImage || turn < -kSpinningImage; }

namespace {

// Each vertex of `m` in the hub frame (its mesh's transform applied, column-major).
template <class F> void each_hub_point(const scene::Model& m, F f) {
    for (const scene::Mesh& mesh : m.meshes) {
        const std::array<float, 16>& t = mesh.transform;
        for (const scene::Vertex& v : mesh.vertices) {
            const float* p = v.pos;
            f(t[0] * p[0] + t[4] * p[1] + t[8] * p[2] + t[12], t[1] * p[0] + t[5] * p[1] + t[9] * p[2] + t[13],
              t[2] * p[0] + t[6] * p[1] + t[10] * p[2] + t[14]);
        }
    }
}

} // namespace

RimDisc rim_disc(const scene::Model& rim) {
    RimDisc d;
    bool any = false;
    float r2 = 0;
    each_hub_point(rim, [&](float x, float y, float z) {
        d.min_x = any ? std::min(d.min_x, x) : x;
        d.max_x = any ? std::max(d.max_x, x) : x;
        r2 = std::max(r2, y * y + z * z);
        any = true;
    });
    d.radius = std::sqrt(r2);
    return d;
}

TyreRing tyre_inner_ring(const scene::Model& tyre) {
    // Only the lit pieces (tread, shoulder, sidewalls): the black unlit faces close the inner side
    // with a disc down to the axle (tyre_model.h).
    struct P {
        float x, r;
    };
    std::vector<P> pts;
    for (const scene::Mesh& mesh : tyre.meshes) {
        const std::array<float, 16>& t = mesh.transform; // (column-major)
        for (const scene::Primitive& prim : mesh.primitives) {
            if (prim.flags & scene::kUnlit) continue;
            for (u32 j = prim.first_index; j < prim.first_index + prim.index_count && j < mesh.indices.size(); ++j) {
                const u32 vi = mesh.indices[j];
                if (vi >= mesh.vertices.size()) continue;
                const float* p = mesh.vertices[vi].pos;
                const float x = t[0] * p[0] + t[4] * p[1] + t[8] * p[2] + t[12];
                const float y = t[1] * p[0] + t[5] * p[1] + t[9] * p[2] + t[13];
                const float z = t[2] * p[0] + t[6] * p[1] + t[10] * p[2] + t[14];
                pts.push_back({x, std::sqrt(y * y + z * z)});
            }
        }
    }
    TyreRing ring;
    if (pts.empty()) return ring;
    float r_min = pts[0].r;
    for (const P& p : pts) r_min = std::min(r_min, p.r);
    bool any = false;
    for (const P& p : pts) {
        if (p.r > r_min + kTyreRingBand) continue;
        ring.min_x = any ? std::min(ring.min_x, p.x) : p.x;
        ring.max_x = any ? std::max(ring.max_x, p.x) : p.x;
        any = true;
    }
    ring.radius = r_min;
    return ring;
}

scene::Model spin_mask_shape(const RimDisc& rim, const TyreRing& tyre, bool minus_x, float out, int segments) {
    scene::Mesh m;
    m.name = "spin_mask_shape";
    const float face = minus_x ? rim.min_x - out : rim.max_x + out;
    auto vertex = [&](float x, float y, float z) {
        scene::Vertex v;
        v.pos[0] = x, v.pos[1] = y, v.pos[2] = z;
        v.normal[0] = minus_x ? -1.0f : 1.0f;
        m.vertices.push_back(v);
        return u32(m.vertices.size() - 1);
    };
    // The disc: a fan round its centre, vertices 1..segments on its edge.
    const u32 centre = vertex(face, 0, 0);
    for (int k = 0; k < segments; ++k) {
        const float a = 6.2831853f * float(k) / float(segments);
        vertex(face, rim.radius * std::cos(a), rim.radius * std::sin(a));
    }
    auto edge = [&](int k) { return u32(1 + k % segments); };
    for (int k = 0; k < segments; ++k) m.indices.insert(m.indices.end(), {centre, edge(k), edge(k + 1)});
    // The sidewall: from the disc's edge to the tyre's inner ring on the same side.
    // (Not where the ring lies across the rim's middle from the face: a tyre that has its ring only
    // on the outer side, its inner side closed by a black disc in front of the rim.)
    const float ring_x = minus_x ? tyre.min_x : tyre.max_x;
    const float middle = 0.5f * (rim.min_x + rim.max_x);
    if (tyre.radius > 0 && (minus_x ? ring_x < middle : ring_x > middle)) {
        const u32 ring0 = u32(m.vertices.size());
        for (int k = 0; k < segments; ++k) {
            const float a = 6.2831853f * float(k) / float(segments);
            vertex(ring_x, tyre.radius * std::cos(a), tyre.radius * std::sin(a));
        }
        auto ring = [&](int k) { return ring0 + u32(k % segments); };
        for (int k = 0; k < segments; ++k)
            m.indices.insert(m.indices.end(), {edge(k), ring(k), ring(k + 1), edge(k), ring(k + 1), edge(k + 1)});
    }
    scene::Primitive p;
    p.index_count = u32(m.indices.size());
    p.flags = scene::kDoubleSided | scene::kUnlit;
    m.primitives.push_back(p);
    scene::Model model;
    model.name = m.name;
    model.meshes.push_back(std::move(m));
    model.compute_bounds();
    return model;
}

} // namespace gt3r::race
