#include "video/field_layout.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt3r::video {

double seam_ratio(const Frame& f) {
    const int h = f.height / 2;
    if (h < 4 || f.y.empty()) return 0.0;
    auto row_diff = [&](int a, int b) {
        const u8* p = &f.y[size_t(a) * f.width];
        const u8* q = &f.y[size_t(b) * f.width];
        double s = 0;
        for (int x = 0; x < f.width; ++x) s += std::abs(int(p[x]) - int(q[x]));
        return s / f.width;
    };
    // A thin horizontal line lying on the seam row differs from the rows on both sides of
    // it, while those rows match each other; a real seam keeps the jump across it however
    // far apart the rows compared are. So take the smallest jump across the seam.
    const double seam = std::min({row_diff(h - 1, h), row_diff(h - 1, h + 1), row_diff(h - 2, h)});
    double around = (row_diff(h - 3, h - 2) + row_diff(h - 2, h - 1) + row_diff(h, h + 1) + row_diff(h + 1, h + 2)) / 4;
    if (seam < 1.0 && around < 1.0) return 0.0;
    return seam / std::max(around, 0.5);
}

void FieldLayoutDetector::add(const Frame& f) {
    if (decided()) return;
    double r = seam_ratio(f);
    if (r == 0.0) return; // flat
    ++total_;
    if (r > kSeamThreshold) ++split_;
}

} // namespace gt3r::video

namespace gt3r::video {

namespace {

// One plane of one field. Field rows are `rows` lines of `w` bytes; `f0` is the field being
// rebuilt (frame parity `parity`: its row k is frame line 2k + parity), `m1`/`p1` the fields
// just before and after it (the other parity), `m2`/`p2` the ones two fields away (the same
// parity). Writes 2 * rows frame lines to `out`.
void deinterlace_plane(const u8* m2, const u8* m1, const u8* f0, const u8* p1, const u8* p2, int w, int rows,
                       int parity, u8* out) {
    auto row = [&](const u8* field, int k) { return field + size_t(std::clamp(k, 0, rows - 1)) * size_t(w); };
    for (int r = 0; r < 2 * rows; ++r) {
        u8* o = out + size_t(r) * size_t(w);
        if ((r & 1) == parity) {
            std::memcpy(o, row(f0, (r - parity) / 2), size_t(w));
            continue;
        }
        // Missing frame line r: the other-parity fields hold it as their row k; the field's own
        // lines just above (c) and below (e) are frame lines r - 1 and r + 1.
        const int k = r >> 1;
        const int kc = (r - 1 - parity) >> 1, ke = (r + 1 - parity) >> 1;
        // At the picture's top or bottom one of them is missing: only the temporal test applies.
        const bool edge = kc < 0 || ke > rows - 1;
        const u8 *C = row(f0, kc), *E = row(f0, ke);
        const u8 *M1 = row(m1, k), *P1 = row(p1, k), *M1b = row(m1, k - 1), *P1b = row(p1, k - 1),
                 *M1f = row(m1, k + 1), *P1f = row(p1, k + 1);
        const u8 *M2c = row(m2, kc), *M2e = row(m2, ke), *P2c = row(p2, kc), *P2e = row(p2, ke);
        for (int x = 0; x < w; ++x) {
            const int c = C[x], e = E[x];
            const int d = (M1[x] + P1[x]) >> 1; // the temporal prediction
            int diff = std::max({std::abs(M1[x] - P1[x]) >> 1,
                                 (std::abs(M2c[x] - c) + std::abs(M2e[x] - e)) >> 1,
                                 (std::abs(P2c[x] - c) + std::abs(P2e[x] - e)) >> 1});
            const int b = (M1b[x] + P1b[x]) >> 1, f = (M1f[x] + P1f[x]) >> 1;
            const int hi = std::max({d - e, d - c, std::min(b - c, f - e)});
            const int lo = std::min({d - e, d - c, std::max(b - c, f - e)});
            if (!edge) diff = std::max({diff, lo, -hi});
            const int spatial = (c + e) >> 1;
            o[x] = u8(std::clamp(spatial, d - diff, d + diff));
        }
    }
}

void deinterlace_planes(const std::vector<u8>& prev, const std::vector<u8>& cur, const std::vector<u8>& next,
                        int w, int h, std::vector<u8>& out) {
    const int rows = h / 2;
    const size_t half = size_t(rows) * size_t(w);
    out.resize(size_t(2 * h) * size_t(w));
    const u8 *pt = prev.data(), *pb = prev.data() + half, *ct = cur.data(), *cb = cur.data() + half,
             *nt = next.data(), *nb = next.data() + half;
    // Fields in time order: prev top, prev bottom, cur top, cur bottom, next top, next bottom.
    deinterlace_plane(pt, pb, ct, cb, nt, w, rows, 0, out.data());
    deinterlace_plane(pb, ct, cb, nt, nb, w, rows, 1, out.data() + size_t(h) * size_t(w));
}

} // namespace

bool can_deinterlace(const Frame& f) {
    return f.width > 0 && f.height >= 8 && f.height % 4 == 0 && f.y.size() == size_t(f.width) * size_t(f.height) &&
           f.u.size() == size_t(f.chroma_width()) * size_t(f.chroma_height()) && f.v.size() == f.u.size();
}

void deinterlace_fields(const Frame& prev, const Frame& cur, const Frame& next, Frame& out) {
    auto same = [&](const Frame& o) { return o.width == cur.width && o.height == cur.height && can_deinterlace(o); };
    const Frame& p = same(prev) ? prev : cur;
    const Frame& n = same(next) ? next : cur;
    deinterlace_planes(p.y, cur.y, n.y, cur.width, cur.height, out.y);
    deinterlace_planes(p.u, cur.u, n.u, cur.chroma_width(), cur.chroma_height(), out.u);
    deinterlace_planes(p.v, cur.v, n.v, cur.chroma_width(), cur.chroma_height(), out.v);
    out.width = cur.width;
    out.height = cur.height * 2;
    out.pts = cur.pts;
    out.type = cur.type;
    out.index = cur.index;
    out.fields_stacked = true;
}

} // namespace gt3r::video
