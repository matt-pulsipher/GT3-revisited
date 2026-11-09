#include "formats/course/visibility.h"

#include <algorithm>
#include <cstring>
#include <map>

namespace gt3r::course {

namespace {

float rdf(ByteSpan d, size_t o) {
    u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

} // namespace

Visibility Visibility::parse(ByteSpan d) {
    Visibility v;
    if (d.size() < 0x30 || std::memcmp(d.data(), "Vls0", 4) != 0) return v;
    try {
        u32 nx = rd16(d, 0x10), nz = rd16(d, 0x12), grid = rd32(d, 0x14);
        if (!nx || !nz) return v;
        v.nx_ = nx, v.nz_ = nz;
        v.x0_ = rdf(d, 0x18), v.z0_ = rdf(d, 0x1C), v.sx_ = rdf(d, 0x28), v.sz_ = rdf(d, 0x2C);
        std::map<u32, u32> point_at, list_at; // block offset -> index (points are shared by cells)
        v.cells_.resize(size_t(nx) * nz);
        for (size_t c = 0; c < v.cells_.size(); ++c) {
            u32 lp = rd32(d, grid + 4 * c);
            for (u32 k = 0;; ++k) {
                u32 r = rd32(d, lp + 4 * k);
                if (!r) break;
                auto [pit, new_point] = point_at.try_emplace(r, u32(v.points_.size()));
                if (new_point) {
                    Point p{rdf(d, r + 4), rdf(d, r + 8), rdf(d, r + 12), 0};
                    u32 lo = rd32(d, r);
                    auto [lit, new_list] = list_at.try_emplace(lo, u32(v.lists_.size()));
                    if (new_list) {
                        std::vector<u16> nodes(rd16(d, lo));
                        for (size_t i = 0; i < nodes.size(); ++i) nodes[i] = rd16(d, lo + 2 + 2 * i);
                        v.lists_.push_back(std::move(nodes));
                    }
                    p.list = lit->second;
                    v.points_.push_back(p);
                }
                v.cells_[c].push_back(pit->second);
            }
        }
    } catch (const FormatError&) {
        return Visibility{};
    }
    return v;
}

const std::vector<u16>* Visibility::visible(float x, float y, float z) const {
    if (cells_.empty()) return nullptr;
    // The cell under the camera; the float -> int conversion truncates, then clamps.
    int ix = int((x - x0_) * float(nx_) * sx_), iz = int((z - z0_) * float(nz_) * sz_);
    ix = std::clamp(ix, 0, int(nx_) - 1), iz = std::clamp(iz, 0, int(nz_) - 1);
    const std::vector<u32>& cell = cells_[size_t(iz) * nx_ + size_t(ix)];
    if (cell.empty()) return nullptr;
    // The nearest view point; a later point replaces the best only when strictly nearer.
    const Point* best = &points_[cell[0]];
    float best_d = 3.4028235e38f;
    for (u32 i : cell) {
        const Point& p = points_[i];
        float dx = p.x - x, dy = p.y - y, dz = p.z - z;
        float dd = dx * dx + dy * dy + dz * dz;
        if (dd < best_d) best_d = dd, best = &p;
    }
    return &lists_[best->list];
}

int scenery_list_slot(const CourseFile& file, SceneryView view) {
    int slot = kSlotSceneryVisMain;
    switch (view) {
    case SceneryView::Main: slot = kSlotSceneryVisMain; break;
    case SceneryView::Split: slot = kSlotSceneryVisSplit; break;
    case SceneryView::Mirror: slot = kSlotSceneryVisMirror; break;
    case SceneryView::SplitMirror:
        slot = file.find(kSlotSceneryVisSplitMirror) ? kSlotSceneryVisSplitMirror : kSlotSceneryVisMirror;
        break;
    }
    return file.find(slot) ? slot : 0;
}

} // namespace gt3r::course
