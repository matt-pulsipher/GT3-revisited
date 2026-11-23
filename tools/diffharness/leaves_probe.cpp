// The later builds' particle manager in the interpreter; see leaves_probe.h.

#include "leaves_probe.h"

#include "sim/exact/tick.h"

#include <algorithm>
#include <cstdio>

namespace gt3r::diff::leaves_probe {

namespace {

namespace ex = sim::exact;
using ee::F;

// (the arguments passed as they are: integers to %d / %X, doubles to %f)
template <class... A> std::string fmt(const char* f, A... a) {
    char s[256];
    std::snprintf(s, sizeof s, f, a...);
    return s;
}

// The routines a function calls (jal), in order, up to its return.
std::vector<u32> calls_of(Ee& e, u32 fn) {
    std::vector<u32> out;
    for (u32 a = fn; a < fn + 0x400; a += 4) {
        const u32 w = e.r32(a);
        if (w >> 26 == 3) out.push_back((a & 0xF0000000u) | (w & 0x03FFFFFFu) << 2);
        if (w == 0x03E00008u) break; // jr $ra
    }
    return out;
}

} // namespace

Manager make_manager(Ee& e, std::string& err) {
    Manager m;
    const u32 ctor = kManagerCtor;
    const std::vector<u32> c = calls_of(e, ctor);
    if (c.size() < 13) return err = "the manager's constructor: unexpected calls", m;
    // Its calls 2 .. 4: the pool's allocation, the particle image's load and its texture set-up (none here).
    for (size_t k = 1; k < 4; ++k)
        if (e.has_hle(c[k])) return err = "the manager's constructor: a call already stood in for", m;
    e.hle(c[1], [](Ee& x) { x.set_v0(x.alloc(std::max<u32>(u32(x.a(0)), 16), 16)); }, "the pool's allocation");
    e.hle(c[2], [](Ee& x) { x.set_v0(0); }, "race/particle.img (not loaded)");
    e.hle(c[3], [](Ee&) {}, "the particle image's textures");
    m.pm = e.alloc(0x400, 16);
    e.call(ctor, {Arg::ptr(m.pm)}, 50'000'000);
    for (size_t k = 1; k < 4; ++k) e.unhle(c[k]);
    m.pool = e.r32(m.pm), m.stride = e.r32(m.pool) - m.pool;
    if (e.r32(m.pm + 8) != m.pool || m.stride < 0x50 || m.stride > 0x100) err = "the manager's pool: unexpected lists";
    return m;
}

int index_of(const Manager& m, u32 p) { return p == 0 ? -1 : int((p - m.pool) / m.stride); }

std::string shapes_difference(Ee& e, const Manager& m, const ex::LeafRules& r) {
    for (u32 k = 0; k < 8; ++k) {
        const u32 a = m.pm + kShapes + kShapeSize * k;
        const ex::LeafShape& s = r.shapes[k];
        if (e.r32(a) != k + 1) return fmt("shape %d: id %d", k, e.r32(a));
        const F* f[] = {&s.mass, &s.w, &s.h, &s.inv_mass, &s.diag, &s.ix, &s.iy, &s.iz, &s.lift, &s.drag, &s.damp_x, &s.damp_z};
        for (u32 j = 0; j < std::size(f); ++j)
            if (e.r32(a + 4 + 4 * j) != f[j]->bits)
                return fmt("shape %d +0x%02X", k, 4 + 4 * j) + fmt(": 0x%08X vs ours 0x%08X", e.r32(a + 4 + 4 * j), f[j]->bits);
        const u32 v = e.r32(a + 0x34);
        for (u32 j = 0; j < 4; ++j)
            for (u32 i = 0; i < 3; ++i) {
                const F ours = i == 0 ? s.corners[j].x : i == 1 ? s.corners[j].y : s.corners[j].z;
                if (e.r32(v + 12 * j + 4 * i) != ours.bits) return fmt("shape %d corner %d: lane %d", k, j, i);
            }
    }
    return {};
}

// The first difference between ours and the original's: the register, the lists, every particle's record.
std::string difference(Ee& e, Manager& m, const ex::Sparks& s, const Vu0Random& r) {
    if (e.vr != r.r) return fmt("R 0x%08X vs ours 0x%08X", e.vr, r.r);
    if (index_of(m, e.r32(m.pm + 4)) != s.first()) return fmt("active head %d vs ours %d", index_of(m, e.r32(m.pm + 4)), s.first());
    for (int k = 0; k < ex::Sparks::kPool; ++k) {
        const u32 a = m.pool + m.stride * u32(k);
        if (index_of(m, e.r32(a)) != s.pool()[size_t(k)].next)
            return fmt("particle %d: next %d vs ours %d", k, index_of(m, e.r32(a)), s.pool()[size_t(k)].next);
    }
    for (int k = s.first(); k >= 0; k = s.pool()[size_t(k)].next) {
        const u32 a = m.pool + m.stride * u32(k);
        if (!s.is_leaf(k)) { // a spark: GT3's fields from +0x08 at +0x0C, its first speed at +0x0A
            if (!m.spark_vt) m.spark_vt = e.r32(a + 4);
            if (e.r32(a + 4) != m.spark_vt || m.spark_vt == m.leaf_vt) return fmt("particle %d: not a spark in the original", k);
            const ex::Spark& q = s.pool()[size_t(k)];
            if (s16(e.r16(a + 0xA)) != q.speed0) return fmt("spark %d: +0x0A %d vs ours %d", k, s16(e.r16(a + 0xA)), q.speed0);
            const F* f[] = {&q.pos.x, &q.pos.y, &q.pos.z, &q.vel.x, &q.vel.y, &q.vel.z, &q.inv_mass, &q.size,
                            &q.normal.x, &q.normal.y, &q.normal.z, &q.plane_d, &q.life, &q.total};
            const u32 off[] = {0x0C, 0x10, 0x14, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44};
            for (size_t j = 0; j < std::size(off); ++j)
                if (e.r32(a + off[j]) != f[j]->bits)
                    return fmt("spark %d +0x%02X", k, off[j]) + fmt(": 0x%08X vs ours 0x%08X", e.r32(a + off[j]), f[j]->bits);
            continue;
        }
        if (!m.leaf_vt) m.leaf_vt = e.r32(a + 4);
        if (e.r32(a + 4) != m.leaf_vt || m.spark_vt == m.leaf_vt) return fmt("particle %d: not a leaf in the original", k);
        const ex::Leaf& q = s.leaves()[size_t(k)];
        const F* f[] = {&q.angle.x, &q.angle.y, &q.angle.z, &q.pos.x,    &q.pos.y,    &q.pos.z,    &q.vel.x,   &q.vel.y, &q.vel.z,
                        &q.spin.x,  &q.spin.y,  &q.spin.z,  &q.normal.x, &q.normal.y, &q.normal.z, &q.plane_d, &q.shade};
        for (u32 j = 0; j < std::size(f); ++j)
            if (e.r32(a + 8 + 4 * j) != f[j]->bits)
                return fmt("leaf %d +0x%02X", k, 8 + 4 * j) + fmt(": 0x%08X vs ours 0x%08X", e.r32(a + 8 + 4 * j), f[j]->bits);
        if (e.r32(a + 0x4C) != m.pm + kShapes + kShapeSize * q.shape)
            return fmt("leaf %d: shape 0x%X vs ours %d", k, e.r32(a + 0x4C), q.shape);
    }
    return {};
}

void put_request(Ee& e, u32 a, const ex::TyreEffect& q) {
    e.w32(a, u32(q.wheel)), e.w32(a + 4, u32(q.surface));
    const F f[] = {q.x, q.h, q.z, q.yaw, q.attr_a, q.normal.x, q.normal.y, q.normal.z, q.plane_d, q.smoke_emit, q.slip_ratio, q.hub_vx,
                   q.rim_speed};
    for (u32 j = 0; j < std::size(f); ++j) e.w32(a + 8 + 4 * j, f[j].bits);
}

int alive(const ex::Sparks& s) {
    int n = 0;
    for (int k = s.first(); k >= 0; k = s.pool()[size_t(k)].next) ++n;
    return n;
}

} // namespace gt3r::diff::leaves_probe
