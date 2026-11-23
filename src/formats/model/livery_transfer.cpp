#include "formats/model/livery_transfer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace gt3r::livery_transfer {

namespace {

using V3 = std::array<float, 3>;

V3 sub(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
float dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V3 cross(const V3& a, const V3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }

V3 transformed(const scene::Mesh& m, const float* p) {
    const auto& t = m.transform;
    return {t[0] * p[0] + t[4] * p[1] + t[8] * p[2] + t[12], t[1] * p[0] + t[5] * p[1] + t[9] * p[2] + t[13],
            t[2] * p[0] + t[6] * p[1] + t[10] * p[2] + t[14]};
}

// The point of triangle abc nearest p, as barycentric weights (Ericson, Real-Time Collision Detection 5.1.5).
std::array<float, 3> closest_barycentric(const V3& p, const V3& a, const V3& b, const V3& c) {
    const V3 ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return {1, 0, 0};
    const V3 bp = sub(p, b);
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return {0, 1, 0};
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        const float v = d1 / (d1 - d3);
        return {1 - v, v, 0};
    }
    const V3 cp = sub(p, c);
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return {0, 0, 1};
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        const float w = d2 / (d2 - d6);
        return {1 - w, 0, w};
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return {0, 1 - w, w};
    }
    const float denom = 1 / (va + vb + vc);
    const float v = vb * denom, w = vc * denom;
    return {1 - v - w, v, w};
}

// A full-detail triangle with a livery.
struct RefTri {
    V3 p[3];
    V3 n; // unit face normal, toward its vertex normals
    float uv[3][2];
    const scene::Texture* tex;
};

// The full-detail triangles in cells of kReach, each in every cell its box (grown by kReach) touches.
class Grid {
public:
    explicit Grid(std::vector<RefTri> tris) : tris_(std::move(tris)) {
        for (size_t i = 0; i < tris_.size(); ++i) {
            V3 lo = tris_[i].p[0], hi = tris_[i].p[0];
            for (const V3& q : tris_[i].p)
                for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], q[k]), hi[k] = std::max(hi[k], q[k]);
            const auto c0 = cell(sub(lo, {kReach, kReach, kReach})), c1 = cell({hi[0] + kReach, hi[1] + kReach, hi[2] + kReach});
            for (int x = c0[0]; x <= c1[0]; ++x)
                for (int y = c0[1]; y <= c1[1]; ++y)
                    for (int z = c0[2]; z <= c1[2]; ++z) cells_[key({x, y, z})].push_back(u32(i));
        }
    }
    // The nearest triangle to p within kReach that faces like n, and the barycentric point on it; null when none.
    const RefTri* nearest(const V3& p, const V3& n, std::array<float, 3>& bary) const {
        auto it = cells_.find(key(cell(p)));
        if (it == cells_.end()) return nullptr;
        const RefTri* best = nullptr;
        float best_d = kReach * kReach;
        for (u32 i : it->second) {
            const RefTri& t = tris_[i];
            if (dot(t.n, n) < 0.5f) continue; // (the other side of a thin part, or a face at an angle)
            const std::array<float, 3> b = closest_barycentric(p, t.p[0], t.p[1], t.p[2]);
            V3 q{};
            for (int k = 0; k < 3; ++k) q[k] = b[0] * t.p[0][k] + b[1] * t.p[1][k] + b[2] * t.p[2][k];
            const V3 d = sub(q, p);
            const float dd = dot(d, d);
            if (dd < best_d) best_d = dd, best = &t, bary = b;
        }
        return best;
    }

private:
    static std::array<int, 3> cell(const V3& p) {
        return {int(std::floor(p[0] / kReach)), int(std::floor(p[1] / kReach)), int(std::floor(p[2] / kReach))};
    }
    static u64 key(const std::array<int, 3>& c) {
        return (u64(u32(c[0]) & 0x1FFFFF) << 42) | (u64(u32(c[1]) & 0x1FFFFF) << 21) | u64(u32(c[2]) & 0x1FFFFF);
    }
    std::vector<RefTri> tris_;
    std::unordered_map<u64, std::vector<u32>> cells_;
};

// Bilinear sample of a texture (edges clamped) at u, v in 0..1: colour and coverage.
void sample(const scene::Texture& t, float u, float v, u32& rgb, u32& cover) {
    const float fx = u * float(t.width) - 0.5f, fy = v * float(t.height) - 0.5f;
    const float x0 = std::floor(fx), y0 = std::floor(fy), ax = fx - x0, ay = fy - y0;
    auto at = [&](float x, float y) {
        return size_t(std::clamp(int(y), 0, int(t.height) - 1)) * t.width + size_t(std::clamp(int(x), 0, int(t.width) - 1));
    };
    const size_t i00 = at(x0, y0), i10 = at(x0 + 1, y0), i01 = at(x0, y0 + 1), i11 = at(x0 + 1, y0 + 1);
    const float w00 = (1 - ax) * (1 - ay), w10 = ax * (1 - ay), w01 = (1 - ax) * ay, w11 = ax * ay;
    rgb = 0;
    for (int k = 0; k < 3; ++k) {
        auto ch = [&](size_t i) { return float((t.rgba[i] >> (8 * k)) & 0xFF); };
        rgb |= u32(std::clamp(std::lround(ch(i00) * w00 + ch(i10) * w10 + ch(i01) * w01 + ch(i11) * w11), 0L, 255L)) << (8 * k);
    }
    const std::vector<u8>& c = *t.livery_cover;
    cover = u32(std::clamp(std::lround(c[i00] * w00 + c[i10] * w10 + c[i01] * w01 + c[i11] * w11), 0L, 255L));
}

bool body_mesh(const scene::Mesh& m) { return m.name.rfind("wheel_", 0) != 0; }

} // namespace

int apply(scene::Model& model, const scene::Model& reference) {
    // The full-detail triangles whose texture the livery covers.
    std::vector<RefTri> ref;
    for (const scene::Mesh& m : reference.meshes) {
        if (m.lod != 0 || !body_mesh(m)) continue;
        for (const scene::Primitive& pr : m.primitives) {
            if (pr.texture < 0 || size_t(pr.texture) >= reference.textures.size()) continue;
            const scene::Texture& tex = reference.textures[size_t(pr.texture)];
            if (!tex.livery_cover || tex.livery_cover->size() != tex.rgba.size()) continue;
            for (u32 k = 0; k + 2 < pr.index_count; k += 3) {
                RefTri t;
                V3 vn{0, 0, 0};
                for (int j = 0; j < 3; ++j) {
                    const scene::Vertex& v = m.vertices[m.indices[pr.first_index + k + u32(j)]];
                    t.p[j] = transformed(m, v.pos);
                    t.uv[j][0] = v.uv[0], t.uv[j][1] = v.uv[1];
                    for (int a = 0; a < 3; ++a) vn[a] += v.normal[a];
                }
                V3 fn = cross(sub(t.p[1], t.p[0]), sub(t.p[2], t.p[0]));
                const float len = std::sqrt(dot(fn, fn));
                if (len < 1e-9f) continue;
                for (float& c : fn) c /= len;
                if (dot(fn, vn) < 0)
                    for (float& c : fn) c = -c;
                t.n = fn;
                t.tex = &tex;
                ref.push_back(t);
            }
        }
    }
    if (ref.empty()) return 0;
    const Grid grid(std::move(ref));
    // The lower-detail textures without a livery, with the triangles that use them.
    std::unordered_map<int, std::vector<std::pair<const scene::Mesh*, u32>>> targets;
    for (const scene::Mesh& m : model.meshes) {
        if (m.lod < 1 || !body_mesh(m)) continue;
        for (const scene::Primitive& pr : m.primitives) {
            if (pr.texture < 0 || size_t(pr.texture) >= model.textures.size() || model.textures[size_t(pr.texture)].livery_cover)
                continue;
            for (u32 k = 0; k + 2 < pr.index_count; k += 3) targets[pr.texture].emplace_back(&m, pr.first_index + k);
        }
    }
    int changed = 0;
    for (auto& [ti, tris] : targets) {
        scene::Texture& t = model.textures[size_t(ti)];
        if (t.width == 0 || t.height == 0 || t.rgba.size() != size_t(t.width) * t.height) continue;
        const u32 w = t.width * kScale, h = t.height * kScale;
        std::vector<u32> colour(size_t(w) * h, 0);
        std::vector<u8> cover(size_t(w) * h, 0);
        bool any = false;
        for (const auto& [mesh, i0] : tris) {
            const scene::Vertex* v[3];
            V3 p[3];
            float px[3][2];
            for (int j = 0; j < 3; ++j) {
                v[j] = &mesh->vertices[mesh->indices[i0 + u32(j)]];
                p[j] = transformed(*mesh, v[j]->pos);
                px[j][0] = v[j]->uv[0] * float(w), px[j][1] = v[j]->uv[1] * float(h);
            }
            const float area = (px[1][0] - px[0][0]) * (px[2][1] - px[0][1]) - (px[2][0] - px[0][0]) * (px[1][1] - px[0][1]);
            if (std::fabs(area) < 1e-6f) continue;
            V3 fn = cross(sub(p[1], p[0]), sub(p[2], p[0]));
            const float len = std::sqrt(dot(fn, fn));
            if (len < 1e-9f) continue;
            V3 vn{0, 0, 0};
            for (int j = 0; j < 3; ++j)
                for (int a = 0; a < 3; ++a) vn[a] += v[j]->normal[a];
            for (float& c : fn) c /= len;
            if (dot(fn, vn) < 0)
                for (float& c : fn) c = -c;
            const int x0 = std::max(0, int(std::floor(std::min({px[0][0], px[1][0], px[2][0]})))),
                      x1 = std::min(int(w) - 1, int(std::ceil(std::max({px[0][0], px[1][0], px[2][0]})))),
                      y0 = std::max(0, int(std::floor(std::min({px[0][1], px[1][1], px[2][1]})))),
                      y1 = std::min(int(h) - 1, int(std::ceil(std::max({px[0][1], px[1][1], px[2][1]}))));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    // Barycentric weights of the texel's centre (a little outside the triangle too, so its edge texels fill).
                    const float cx = float(x) + 0.5f, cy = float(y) + 0.5f;
                    float b[3];
                    for (int j = 0; j < 3; ++j) {
                        const float* a = px[(j + 1) % 3];
                        const float* c = px[(j + 2) % 3];
                        b[j] = ((c[0] - a[0]) * (cy - a[1]) - (c[1] - a[1]) * (cx - a[0])) / area;
                    }
                    const float margin = -1.0f / std::sqrt(std::fabs(area));
                    if (b[0] < margin || b[1] < margin || b[2] < margin) continue;
                    V3 q{};
                    for (int k = 0; k < 3; ++k) q[k] = b[0] * p[0][k] + b[1] * p[1][k] + b[2] * p[2][k];
                    std::array<float, 3> rb{};
                    const RefTri* r = grid.nearest(q, fn, rb);
                    if (!r) continue;
                    const float u = rb[0] * r->uv[0][0] + rb[1] * r->uv[1][0] + rb[2] * r->uv[2][0];
                    const float vv = rb[0] * r->uv[0][1] + rb[1] * r->uv[1][1] + rb[2] * r->uv[2][1];
                    u32 rgb = 0, cov = 0;
                    sample(*r->tex, u, vv, rgb, cov);
                    const size_t o = size_t(y) * w + size_t(x);
                    if (cov > cover[o]) colour[o] = rgb, cover[o] = u8(cov), any = true;
                }
        }
        if (!any) continue;
        // The texture at twice its size (colour filtered unless point-sampled, alpha nearest), the livery over it.
        std::vector<u32> out(size_t(w) * h);
        for (u32 y = 0; y < h; ++y)
            for (u32 x = 0; x < w; ++x) {
                const float fx = (float(x) + 0.5f) / float(kScale) - 0.5f, fy = (float(y) + 0.5f) / float(kScale) - 0.5f;
                const u32 nx = std::min(t.width - 1, x / kScale), ny = std::min(t.height - 1, y / kScale);
                u32 base = t.rgba[size_t(ny) * t.width + nx];
                if (!t.nearest) {
                    const float x0f = std::floor(fx), y0f = std::floor(fy), ax = fx - x0f, ay = fy - y0f;
                    auto at = [&](float xx, float yy) {
                        return t.rgba[size_t(std::clamp(int(yy), 0, int(t.height) - 1)) * t.width +
                                      size_t(std::clamp(int(xx), 0, int(t.width) - 1))];
                    };
                    const u32 c00 = at(x0f, y0f), c10 = at(x0f + 1, y0f), c01 = at(x0f, y0f + 1), c11 = at(x0f + 1, y0f + 1);
                    u32 f = base & 0xFF000000u;
                    for (int k = 0; k < 3; ++k) {
                        auto ch = [&](u32 c) { return float((c >> (8 * k)) & 0xFF); };
                        const float top = ch(c00) * (1 - ax) + ch(c10) * ax, bot = ch(c01) * (1 - ax) + ch(c11) * ax;
                        f |= u32(std::clamp(std::lround(top * (1 - ay) + bot * ay), 0L, 255L)) << (8 * k);
                    }
                    base = f;
                }
                const size_t o = size_t(y) * w + x;
                const u32 a = cover[o];
                u32 px = base & 0xFF000000u;
                for (int k = 0; k < 3; ++k) {
                    const u32 bc = (base >> (8 * k)) & 0xFF, lc = (colour[o] >> (8 * k)) & 0xFF;
                    px |= ((bc * (255 - a) + lc * a + 127) / 255) << (8 * k);
                }
                out[o] = px;
            }
        t.width = w, t.height = h, t.rgba = std::move(out), t.nearest = false;
        t.livery_cover = std::make_shared<const std::vector<u8>>(std::move(cover));
        ++changed;
    }
    return changed;
}

} // namespace gt3r::livery_transfer
