#include "formats/model/rim_light.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>

namespace gt3r {

namespace {

// Triangles covering less of the image than this (texels squared) are left out: their corners
// all sample one spot (a flat colour for a spoke's side or the barrel) and place no texels.
constexpr double kMinImageArea = 0.01;
// Texels this far (texels) outside a triangle are placed by it too, where no triangle covers
// them: bilinear filtering and the mipmaps blend them into the texels it draws.
constexpr double kNeighbourTexels = 1.0;
// Two triangles covering one texel at places further apart than this (texels) make it an
// image area used twice; it is left alone.
constexpr double kSamePlaceTexels = 1.5;
// Rings closer to the axis than this (texels) are left alone: too few texels to measure.
constexpr double kMinRingRadius = 2.0;
// Each ring's harmonic is measured over the rings this many texels either side as well,
// so that the correction changes smoothly from ring to ring.
constexpr int kRingWindow = 2;
// A ring is measured only when its texels (counted by alpha, 1 = opaque) add up to this many...
constexpr double kMinRingTexels = 8.0;
// ...and they lie round it evenly enough to tell one side from the other: the mean of their
// directions from the axis no longer than this.
constexpr double kMaxRingImbalance = 0.5;
// The least the light divided out may scale a ring's side by (a guard for nearly black rings).
constexpr double kMinGain = 0.25;
// A channel's ring mean below this (of 255) is left alone: no light to measure.
constexpr double kMinRingMean = 1.0;
// Texels measured for one-sidedness: alpha at least this (of 255).
constexpr int kMeasureAlpha = 64;

struct Corner {
    double p[2]; // model (y, z): the wheel turns about x
    double t[2]; // image position, texels
};

double cross(const double* o, const double* a, const double* b) {
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
}

// The lit (non-reflection) triangles drawing each texture, as corners.
std::map<int, std::vector<Corner>> lit_corners(const scene::Model& wheel) {
    std::map<int, std::vector<Corner>> out;
    for (const scene::Mesh& mesh : wheel.meshes)
        for (const scene::Primitive& p : mesh.primitives) {
            if (p.texture < 0 || size_t(p.texture) >= wheel.textures.size() || (p.flags & scene::kEnvironmentMap))
                continue;
            const scene::Texture& tex = wheel.textures[size_t(p.texture)];
            std::vector<Corner>& c = out[p.texture];
            for (u32 i = 0; i + 2 < p.index_count; i += 3)
                for (u32 j = 0; j < 3; ++j) {
                    const scene::Vertex& v = mesh.vertices[mesh.indices[p.first_index + i + j]];
                    c.push_back({{v.pos[1], v.pos[2]}, {double(v.uv[0]) * tex.width, double(v.uv[1]) * tex.height}});
                }
        }
    return out;
}

RimTexelPlaces places_from_corners(u32 w, u32 h, const std::vector<Corner>& corners) {
    RimTexelPlaces out;
    out.width = w, out.height = h;
    out.texels.assign(size_t(w) * h, {});
    const size_t tris = corners.size() / 3;
    // Texels per unit of the face: the image area the triangles cover over their area across the axis.
    double image_area = 0, face_area = 0;
    for (size_t i = 0; i < tris; ++i) {
        const Corner* k = &corners[3 * i];
        const double a = std::fabs(cross(k[0].t, k[1].t, k[2].t)), f = std::fabs(cross(k[0].p, k[1].p, k[2].p));
        if (a >= kMinImageArea && f > 0) image_area += a, face_area += f;
    }
    if (w == 0 || h == 0 || face_area <= 0) return out;
    const double scale = std::sqrt(image_area / face_area);
    struct Slot {
        double outside = std::numeric_limits<double>::infinity(); // texels outside the triangle placing it
        double p[2] = {0, 0};
        bool twice = false;
        bool spot = false; // sampled as one spot's flat colour
    };
    std::vector<Slot> slots(size_t(w) * h);
    for (size_t i = 0; i < tris; ++i) {
        const Corner* k = &corners[3 * i];
        const double area = cross(k[0].t, k[1].t, k[2].t);
        if (std::fabs(area) < kMinImageArea) {
            // A flat colour (a spoke's side, the barrel): its texels keep their colour, as the
            // parts it paints are not laid out round the axis in the image.
            for (int j = 0; j < 3; ++j) {
                const int x0 = int(std::floor(k[j].t[0] - 0.5)), y0 = int(std::floor(k[j].t[1] - 0.5));
                for (int y = y0; y <= y0 + 1; ++y)
                    for (int x = x0; x <= x0 + 1; ++x)
                        if (x >= 0 && y >= 0 && x < int(w) && y < int(h)) slots[size_t(y) * w + size_t(x)].spot = true;
            }
            continue;
        }
        double lo[2] = {k[0].t[0], k[0].t[1]}, hi[2] = {lo[0], lo[1]};
        for (int j = 1; j < 3; ++j)
            for (int a = 0; a < 2; ++a) lo[a] = std::min(lo[a], k[j].t[a]), hi[a] = std::max(hi[a], k[j].t[a]);
        const int x0 = std::max(0, int(std::floor(lo[0] - kNeighbourTexels)));
        const int x1 = std::min(int(w) - 1, int(std::ceil(hi[0] + kNeighbourTexels)));
        const int y0 = std::max(0, int(std::floor(lo[1] - kNeighbourTexels)));
        const int y1 = std::min(int(h) - 1, int(std::ceil(hi[1] + kNeighbourTexels)));
        double edge[3]; // length of the edge opposite each corner
        for (int e = 0; e < 3; ++e)
            edge[e] = std::hypot(k[(e + 2) % 3].t[0] - k[(e + 1) % 3].t[0], k[(e + 2) % 3].t[1] - k[(e + 1) % 3].t[1]);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const double q[2] = {x + 0.5, y + 0.5};
                // Corner e's barycentric weight: the sub-triangle opposite it over the whole. A
                // negative one puts the texel outside the opposite edge, by weight x height.
                double b[3], outside = 0;
                for (int e = 0; e < 3; ++e) {
                    b[e] = cross(q, k[(e + 1) % 3].t, k[(e + 2) % 3].t) / area;
                    if (b[e] < 0 && edge[e] > 0) outside = std::max(outside, -b[e] * std::fabs(area) / edge[e]);
                }
                if (outside > kNeighbourTexels) continue;
                const double p[2] = {b[0] * k[0].p[0] + b[1] * k[1].p[0] + b[2] * k[2].p[0],
                                     b[0] * k[0].p[1] + b[1] * k[1].p[1] + b[2] * k[2].p[1]};
                Slot& s = slots[size_t(y) * w + size_t(x)];
                if (outside == 0 && s.outside == 0) {
                    if (std::hypot(p[0] - s.p[0], p[1] - s.p[1]) * scale > kSamePlaceTexels) s.twice = true;
                } else if (outside < s.outside) {
                    s.outside = outside, s.p[0] = p[0], s.p[1] = p[1];
                }
            }
    }
    for (size_t i = 0; i < slots.size(); ++i) {
        const Slot& s = slots[i];
        if (s.twice || s.spot || !std::isfinite(s.outside)) continue;
        const double r = std::hypot(s.p[0], s.p[1]);
        RimTexelPlaces::Place& pl = out.texels[i];
        pl.radius = float(r * scale);
        if (r > 0) pl.cos = float(s.p[0] / r), pl.sin = float(s.p[1] / r);
    }
    return out;
}

bool fits(const scene::Texture& t, const RimTexelPlaces& places) {
    const size_t n = size_t(t.width) * t.height;
    return n > 0 && t.rgba.size() >= n && places.width == t.width && places.height == t.height &&
           places.texels.size() == n;
}

// Solves the 3x3 system a x = b (Gaussian elimination, partial pivoting); false when singular.
bool solve3(const double a[3][3], const double b[3], double x[3]) {
    double m[3][4];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) m[i][j] = a[i][j];
        m[i][3] = b[i];
    }
    for (int c = 0; c < 3; ++c) {
        int p = c;
        for (int i = c + 1; i < 3; ++i)
            if (std::fabs(m[i][c]) > std::fabs(m[p][c])) p = i;
        if (std::fabs(m[p][c]) < 1e-12) return false;
        for (int j = 0; j < 4; ++j) std::swap(m[c][j], m[p][j]);
        for (int i = 0; i < 3; ++i)
            if (i != c) {
                const double k = m[i][c] / m[c][c];
                for (int j = c; j < 4; ++j) m[i][j] -= k * m[c][j];
            }
    }
    for (int i = 0; i < 3; ++i) x[i] = m[i][3] / m[i][i];
    return true;
}

} // namespace

RimTexelPlaces rim_texel_places(const scene::Model& wheel, int texture) {
    if (texture < 0 || size_t(texture) >= wheel.textures.size()) return {};
    const scene::Texture& t = wheel.textures[size_t(texture)];
    const auto corners = lit_corners(wheel);
    const auto it = corners.find(texture);
    return places_from_corners(t.width, t.height, it == corners.end() ? std::vector<Corner>{} : it->second);
}

void even_ring_light(scene::Texture& t, const RimTexelPlaces& places) {
    if (!fits(t, places)) return;
    // Per ring: the alpha-weighted sums of the least-squares fit c = m + a cos + b sin of each channel.
    struct Sums {
        double n[3][3] = {};
        double rhs[3][3] = {}; // [channel][1, cos, sin]
    };
    std::vector<Sums> rings;
    for (size_t i = 0; i < places.texels.size(); ++i) {
        const RimTexelPlaces::Place& p = places.texels[i];
        if (p.radius < 0) continue;
        const size_t k = size_t(p.radius);
        if (k >= rings.size()) rings.resize(k + 1);
        const u32 px = t.rgba[i];
        const double wt = double(px >> 24) / 255.0;
        if (wt <= 0) continue;
        const double f[3] = {1, p.cos, p.sin};
        Sums& s = rings[k];
        for (int a = 0; a < 3; ++a) {
            for (int b = 0; b < 3; ++b) s.n[a][b] += wt * f[a] * f[b];
            for (int c = 0; c < 3; ++c) s.rhs[c][a] += wt * f[a] * double((px >> (8 * c)) & 0xFF);
        }
    }
    // A ring can be measured when its texels add up to enough and lie round it evenly enough to
    // tell one side from the other.
    auto measurable = [&](size_t k) {
        const double count = rings[k].n[0][0];
        return count >= kMinRingTexels && std::hypot(rings[k].n[0][1], rings[k].n[0][2]) <= kMaxRingImbalance * count;
    };
    // Each measurable ring's light, 1 + a cos + b sin per channel, fitted over it and the
    // measurable rings in the window round it, so that the light changes evenly from ring to
    // ring. A neighbour weighs no more than the ring itself (by the channel's sum over the
    // ring), so a crowded or bright ring does not set the light of a sparse or dark one.
    struct Light {
        bool on = false;
        double a[3] = {}, b[3] = {};
    };
    std::vector<Light> light(rings.size());
    for (size_t k = 0; k < rings.size(); ++k) {
        if (double(k) + 0.5 < kMinRingRadius || !measurable(k)) continue;
        Light& l = light[k];
        for (int c = 0; c < 3; ++c) {
            const double self = rings[k].rhs[c][0];
            if (self < kMinRingMean * rings[k].n[0][0]) continue;
            double n[3][3] = {}, rhs[3] = {};
            for (int j = int(k) - kRingWindow; j <= int(k) + kRingWindow; ++j) {
                if (j < 0 || size_t(j) >= rings.size() || !measurable(size_t(j))) continue;
                const Sums& r = rings[size_t(j)];
                if (r.rhs[c][0] <= 0) continue;
                const double f = std::min(1.0, self / r.rhs[c][0]);
                for (int u = 0; u < 3; ++u) {
                    for (int v = 0; v < 3; ++v) n[u][v] += f * r.n[u][v];
                    rhs[u] += f * r.rhs[c][u];
                }
            }
            double x[3];
            if (!solve3(n, rhs, x) || x[0] < kMinRingMean) continue;
            l.a[c] = x[1] / x[0], l.b[c] = x[2] / x[0];
            l.on = true;
        }
    }
    // The light divided out, then each ring's channels scaled back to the ring's own mean
    // (alpha-weighted, as fitted).
    auto evened = [&](size_t i) { return places.texels[i].radius >= 0 && light[size_t(places.texels[i].radius)].on; };
    std::vector<std::array<double, 3>> out(places.texels.size());
    std::vector<std::array<double, 6>> means(rings.size()); // [channel] before, [3 + channel] after
    for (size_t i = 0; i < places.texels.size(); ++i) {
        if (!evened(i)) continue;
        const RimTexelPlaces::Place& p = places.texels[i];
        const Light& l = light[size_t(p.radius)];
        const u32 px = t.rgba[i];
        const double wt = double(px >> 24) / 255.0;
        for (int c = 0; c < 3; ++c) {
            const double v = double((px >> (8 * c)) & 0xFF);
            out[i][size_t(c)] = v / std::max(kMinGain, 1.0 + l.a[c] * p.cos + l.b[c] * p.sin);
            means[size_t(p.radius)][size_t(c)] += wt * v;
            means[size_t(p.radius)][size_t(3 + c)] += wt * out[i][size_t(c)];
        }
    }
    for (size_t i = 0; i < places.texels.size(); ++i) {
        if (!evened(i)) continue;
        const std::array<double, 6>& m = means[size_t(places.texels[i].radius)];
        u32 px = t.rgba[i] & 0xFF000000u;
        for (int c = 0; c < 3; ++c) {
            const double v = m[size_t(3 + c)] > 0 ? out[i][size_t(c)] * m[size_t(c)] / m[size_t(3 + c)] : out[i][size_t(c)];
            px |= u32(std::clamp(std::round(v), 0.0, 255.0)) << (8 * c);
        }
        t.rgba[i] = px;
    }
}

double ring_light_one_sidedness(const scene::Texture& t, const RimTexelPlaces& places) {
    if (!fits(t, places)) return 0;
    auto measured = [&](size_t i) {
        return places.texels[i].radius >= kMinRingRadius && int(t.rgba[i] >> 24) >= kMeasureAlpha;
    };
    auto luminance = [](u32 px) { return 0.299 * (px & 0xFF) + 0.587 * ((px >> 8) & 0xFF) + 0.114 * ((px >> 16) & 0xFF); };
    // Rings as even_ring_light measures them: enough texels, round the ring evenly enough.
    struct Ring {
        double n = 0, sum = 0, cos = 0, sin = 0, dx = 0, dy = 0;
    };
    std::vector<Ring> rings;
    for (size_t i = 0; i < places.texels.size(); ++i) {
        if (!measured(i)) continue;
        const RimTexelPlaces::Place& p = places.texels[i];
        const size_t k = size_t(p.radius);
        if (k >= rings.size()) rings.resize(k + 1);
        rings[k].n += 1, rings[k].sum += luminance(t.rgba[i]), rings[k].cos += p.cos, rings[k].sin += p.sin;
    }
    auto measurable = [](const Ring& r) {
        return r.n >= kMinRingTexels && std::hypot(r.cos, r.sin) <= kMaxRingImbalance * r.n;
    };
    double n = 0, all = 0, dx = 0, dy = 0;
    for (size_t i = 0; i < places.texels.size(); ++i) {
        if (!measured(i)) continue;
        const RimTexelPlaces::Place& p = places.texels[i];
        const Ring& r = rings[size_t(p.radius)];
        if (!measurable(r)) continue;
        const double l = luminance(t.rgba[i]), d = l - r.sum / r.n;
        n += 1, all += l, dx += d * p.cos, dy += d * p.sin;
    }
    return all > 0 ? std::hypot(dx / n, dy / n) / (all / n) : 0;
}

int even_rim_light(scene::Model& wheel) {
    int n = 0;
    for (const auto& [tex, corners] : lit_corners(wheel)) {
        scene::Texture& t = wheel.textures[size_t(tex)];
        const std::vector<u32> before = t.rgba;
        even_ring_light(t, places_from_corners(t.width, t.height, corners));
        n += t.rgba != before;
    }
    return n;
}

std::optional<double> rim_light_one_sidedness(const scene::Model& wheel) {
    const auto corners = lit_corners(wheel);
    int most = -1;
    size_t most_n = 0;
    for (const auto& [tex, c] : corners)
        if (c.size() > most_n) most = tex, most_n = c.size();
    if (most < 0) return std::nullopt;
    const scene::Texture& t = wheel.textures[size_t(most)];
    return ring_light_one_sidedness(t, places_from_corners(t.width, t.height, corners.at(most)));
}

} // namespace gt3r
