#include "race/driver_seat.h"

#include "render/math3d.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace gt3r::race {

namespace {

constexpr float kCell = 0.02f;        // mirror images match within a cell or its neighbours (2 to 4 cm)
constexpr float kLaneInner = 0.08f;   // a seat's lane: from this far off the centre plane (metres) ...
constexpr float kLaneOuter = 0.34f;   // ... to this share of the body's width
constexpr float kCabinFront = 0.30f;  // the cabin's part of the body's length, from the nose
constexpr float kCabinRear = 0.80f;
constexpr float kWaist = 0.50f;       // above this share of the body's height
constexpr float kPanel = 0.12f;       // larger triangles (m2) are bodywork and glass, not a figure
constexpr float kFigure = 0.20f;      // a lane holds a driver from this much unmirrored area (m2) ...
constexpr float kClear = 3.0f;        // ... and this many times the other lane's

using Tri = std::array<render::Vec3, 3>;

u64 cell_key(int x, int y, int z) {
    return (u64(u32(x + (1 << 20))) << 42) | (u64(u32(y + (1 << 20))) << 21) | u64(u32(z + (1 << 20)));
}

int cell_of(float v) {
    return int(std::floor(v / kCell));
}

} // namespace

DriverSeat find_driver_seat(const scene::Model& body) {
    std::vector<Tri> tris;
    for (const scene::Mesh& mesh : body.meshes) {
        if (mesh.lod != 0 || mesh.name.rfind("wheel_", 0) == 0 || mesh.name.rfind("node", 0) == 0) continue;
        const render::Mat4 at = render::Mat4::from(mesh.transform);
        for (const scene::Primitive& p : mesh.primitives) {
            if (p.flags & (scene::kEnvironmentMap | scene::kAdditive | scene::kDepthOnly)) continue;
            const size_t end = std::min(mesh.indices.size(), size_t(p.first_index) + p.index_count);
            for (size_t i = p.first_index; i + 2 < end; i += 3) {
                Tri t;
                bool ok = true;
                for (size_t k = 0; k < 3; ++k) {
                    const u32 index = mesh.indices[i + k];
                    if (index >= mesh.vertices.size()) {
                        ok = false;
                        break;
                    }
                    const auto& v = mesh.vertices[index].pos;
                    t[k] = at.transform_point({v[0], v[1], v[2]});
                    ok = ok && std::isfinite(t[k].x) && std::isfinite(t[k].y) && std::isfinite(t[k].z);
                }
                if (ok) tris.push_back(t);
            }
        }
    }
    DriverSeat seat;
    if (tris.empty()) return seat;

    render::Vec3 lo = tris[0][0], hi = tris[0][0];
    std::unordered_set<u64> cells;
    for (const Tri& t : tris)
        for (const render::Vec3& p : t) {
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
            cells.insert(cell_key(cell_of(p.x), cell_of(p.y), cell_of(p.z)));
        }
    const float width = hi.x - lo.x, height = hi.y - lo.y, length = hi.z - lo.z;
    if (!(width > 0.5f && height > 0.3f && length > 1.0f) || width > 20 || height > 20 || length > 40) return seat; // not a car
    const float centre = 0.5f * (lo.x + hi.x);
    const auto mirrored = [&](const render::Vec3& p) {
        const int x = cell_of(2 * centre - p.x), y = cell_of(p.y), z = cell_of(p.z);
        for (int i = -1; i <= 1; ++i)
            for (int j = -1; j <= 1; ++j)
                for (int k = -1; k <= 1; ++k)
                    if (cells.count(cell_key(x + i, y + j, z + k))) return true;
        return false;
    };

    double area[2] = {0, 0}, across[2] = {0, 0};
    for (const Tri& t : tris) {
        const render::Vec3 m = (t[0] + t[1] + t[2]) * (1.0f / 3.0f);
        const float off = m.x - centre;
        if (std::fabs(off) <= kLaneInner || std::fabs(off) >= kLaneOuter * width) continue;
        if (m.z <= lo.z + kCabinFront * length || m.z >= lo.z + kCabinRear * length || m.y <= lo.y + kWaist * height) continue;
        const double a = 0.5 * double(render::length(render::cross(t[1] - t[0], t[2] - t[0])));
        if (a >= double(kPanel) || (mirrored(t[0]) && mirrored(t[1]) && mirrored(t[2]))) continue;
        const size_t lane = off > 0 ? 1 : 0;
        area[lane] += a, across[lane] += a * double(off);
    }
    seat.left = float(area[0]), seat.right = float(area[1]);
    const size_t more = area[1] > area[0] ? 1 : 0;
    if (area[more] >= double(kFigure) && area[more] >= double(kClear) * area[1 - more])
        seat.side = more ? 1 : -1, seat.x = float(across[more] / area[more]);
    return seat;
}

} // namespace gt3r::race
