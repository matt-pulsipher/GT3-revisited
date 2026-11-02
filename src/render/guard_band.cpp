#include "render/guard_band.h"

#include <cmath>

namespace gt3r::render {

namespace {

// CLIP of (x, y) against |w|, and the near and far planes: 0x20 near, 0x10 far, 0x08 -y, 0x04 +y,
// 0x02 -x, 0x01 +x.
u32 flags_at(float x, float y, float w, float near, float far) {
    const float aw = std::fabs(w);
    u32 f = 0;
    if (x > aw) f |= 0x01;
    if (x < -aw) f |= 0x02;
    if (y > aw) f |= 0x04;
    if (y < -aw) f |= 0x08;
    if (w < near) f |= 0x20; // the projection's z < -|w|: in front of the near plane or behind the eye
    if (far > 0 && w > far) f |= 0x10; // z > |w|: beyond the far plane
    return f;
}

struct Corner {
    ClipPos p;
    float b[3];
    u32 flags;
};

// The plane's distance of a corner on the scaled coordinates (the VU's x + w, x - w, ...; the
// near plane's z + w and the far plane's z - w, which are w - near and w - far times constants):
// only their ratio is used.
float distance(const ClipPos& p, u32 bit, float near, float far) {
    switch (bit) {
    case 0x20: return p.w - near;
    case 0x10: return p.w - far;
    case 0x08: return 0.5f * p.y + p.w;
    case 0x04: return 0.5f * p.y - p.w;
    case 0x02: return 0.5f * p.x + p.w;
    default: return 0.5f * p.x - p.w;
    }
}

} // namespace

u32 guard_flags(const ClipPos& p, float near, float far) { return flags_at(0.5f * p.x, 0.5f * p.y, p.w, near, far); }

u32 screen_flags(const ClipPos& p, float near, float far) { return flags_at(p.x, p.y, p.w, near, far); }

int guard_clip(const ClipPos v[3], float near, float far, ClipCorner out[kMaxClipCorners]) {
    if (screen_flags(v[0], near, far) & screen_flags(v[1], near, far) & screen_flags(v[2], near, far)) return 0;
    Corner poly[kMaxClipCorners], next[kMaxClipCorners];
    int n = 3;
    u32 any = 0;
    for (int i = 0; i < 3; ++i) {
        poly[i].p = v[i];
        for (int k = 0; k < 3; ++k) poly[i].b[k] = i == k ? 1.0f : 0.0f;
        poly[i].flags = guard_flags(v[i], near, far);
        any |= poly[i].flags;
    }
    for (u32 bit : {0x20u, 0x10u, 0x08u, 0x04u, 0x02u, 0x01u}) {
        if (!(any & bit)) continue; // no corner beyond this plane
        int m = 0;
        any = 0;
        for (int i = 0; i < n; ++i) {
            const Corner& c = poly[i];
            const Corner& d = poly[(i + 1) % n];
            if (m + 2 > kMaxClipCorners) return 0; // (a convex polygon gains one corner a plane)
            if (!(c.flags & bit)) next[m++] = c, any |= c.flags;
            if ((c.flags & bit) == (d.flags & bit)) continue;
            // The crossing: (attr_d dist_c - attr_c dist_d) / (dist_c - dist_d).
            const float dc = distance(c.p, bit, near, far), dd = distance(d.p, bit, near, far);
            const float q = 1.0f / (dc - dd);
            Corner& e = next[m++];
            e.p.x = (d.p.x * dc - c.p.x * dd) * q;
            e.p.y = (d.p.y * dc - c.p.y * dd) * q;
            e.p.w = (d.p.w * dc - c.p.w * dd) * q;
            for (int k = 0; k < 3; ++k) e.b[k] = (d.b[k] * dc - c.b[k] * dd) * q;
            e.flags = guard_flags(e.p, near, far);
            any |= e.flags;
        }
        if (m < 3) return 0;
        n = m;
        for (int i = 0; i < n; ++i) poly[i] = next[i];
    }
    // The strip: from the first corner, then alternately the last ones and the next ones.
    for (int i = 0, lo = 0, hi = n - 1; i < n; ++i) {
        const Corner& c = poly[(i & 1) ? hi-- : lo++];
        for (int k = 0; k < 3; ++k) out[i].b[k] = c.b[k];
    }
    return n;
}

} // namespace gt3r::render
