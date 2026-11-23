// The original's sparks in the interpreter; see sparks_probe.h.

#include "sparks_probe.h"

#include "base/ee_math.h"

#include <cstdio>
#include <iterator>

namespace gt3r::diff::sparks_probe {

namespace {

namespace ex = sim::exact;
using ee::F;

// (the arguments passed as they are: integers to %d / %X, doubles to %f)
template <class... A> std::string fmt(const char* f, A... a) {
    char s[256];
    std::snprintf(s, sizeof s, f, a...);
    return s;
}

} // namespace

u32 model_car() { return sim::concept_program(program()) ? 0x6C8 : 0x6AC; }

bool run_micro(Ee& e, u32 start, std::string& err) {
    u32 vi = 0; // (the I register)
    bool end = false;
    for (u32 pc = start; pc < kMicroSize; pc += 8) {
        const u32 lo = e.r32(kMicro + pc), up = e.r32(kMicro + pc + 4);
        const u32 dest = (up >> 21) & 0xF, ft = (up >> 16) & 31, fs = (up >> 11) & 31, fd = (up >> 6) & 31, op = up & 63;
        auto lanes = [&](auto f) {
            for (int i = 0; i < 4; ++i)
                if ((dest >> (3 - i)) & 1) f(i);
        };
        const auto S = e.vf[fs], T = e.vf[ft];
        if (op == 0x2A) { // MUL
            lanes([&](int i) { e.vf[fd][size_t(i)] = ee::mul(S[size_t(i)], T[size_t(i)]); });
        } else if (op == 0x23) { // MADDi
            lanes([&](int i) { e.vf[fd][size_t(i)] = ee::madd(e.vacc[size_t(i)], S[size_t(i)], vi); });
        } else if (op >= 0x3C) {
            const u32 op2 = ((up >> 6) & 31) << 2 | (up & 3);
            if (op2 == 0x1E) lanes([&](int i) { e.vacc[size_t(i)] = ee::mul(S[size_t(i)], vi); });                         // MULAi
            else if (op2 == 0x23) lanes([&](int i) { e.vacc[size_t(i)] = ee::madd(e.vacc[size_t(i)], S[size_t(i)], vi); }); // MADDAi
            else if (op2 >= 0x08 && op2 <= 0x0B) // MADDAbc
                lanes([&](int i) { e.vacc[size_t(i)] = ee::madd(e.vacc[size_t(i)], S[size_t(i)], T[op2 & 3]); });
            else if (op2 != 0x2F) return err = fmt("micro 0x%X: upper op 0x%X", pc, op2), false;
        } else {
            return err = fmt("micro 0x%X: upper op 0x%X", pc, op), false;
        }
        e.vf[0] = {0, 0, 0, 0x3F800000};
        if (up >> 31) vi = lo;
        else if (lo != 0x80000035u) return err = fmt("micro 0x%X: lower 0x%X", pc, lo), false;
        if (end) return true;
        end = (up >> 30) & 1;
    }
    return err = "micro: no end", false;
}

void install_micro(Ee& e, std::string& micro_err) {
    e.hle(kVcallSin,
          [&micro_err](Ee& x) {
              std::string err;
              if (!run_micro(x, 0x1D0, err) && micro_err.empty()) micro_err = err;
          },
          "vcallms 0x1D0 (the VU0 micro program, run from its image)");
}

Manager make_manager(Ee& e, std::string& err) {
    Manager m;
    if (sim::concept_program(program())) {
        m.later = leaves_probe::make_manager(e, err);
        m.pm = m.later.pm, m.pool = m.later.pool;
        e.call(kPBuild, {Arg::ptr(kPBlock)});
        return m;
    }
    m.pm = e.alloc(0x20, 16), m.pool = e.alloc(ex::Sparks::kPool * kParticle, 16);
    e.w32(m.pm, m.pool);
    e.call(kPBuild, {Arg::ptr(kPBlock)});
    e.call(kReset, {Arg::ptr(m.pm)});
    return m;
}

Manager adopt_manager(Ee& e, const leaves_probe::Manager& later) {
    Manager m;
    m.later = later, m.pm = later.pm, m.pool = later.pool;
    e.call(kPBuild, {Arg::ptr(kPBlock)});
    return m;
}

int index_of(const Manager& m, u32 p) { return p == 0 ? -1 : int((p - m.pool) / kParticle); }

std::string difference(Ee& e, Manager& m, const ex::Sparks& s, const Vu0Random& r) {
    if (m.later.pm) return leaves_probe::difference(e, m.later, s, r);
    if (e.vr != r.r) return fmt("R 0x%08X vs ours 0x%08X", e.vr, r.r);
    if (index_of(m, e.r32(m.pm + 4)) != s.first()) return fmt("active head %d vs ours %d", index_of(m, e.r32(m.pm + 4)), s.first());
    for (int k = 0; k < ex::Sparks::kPool; ++k) {
        const u32 a = m.pool + kParticle * u32(k);
        const ex::Spark& q = s.pool()[size_t(k)];
        if (index_of(m, e.r32(a)) != q.next) return fmt("particle %d: next %d vs ours %d", k, index_of(m, e.r32(a)), q.next);
        if (s16(e.r16(a + 6)) != q.speed0) return fmt("particle %d: +0x06 %d vs ours %d", k, s16(e.r16(a + 6)), q.speed0);
        const F* f[] = {&q.pos.x, &q.pos.y, &q.pos.z, &q.vel.x, &q.vel.y, &q.vel.z, &q.inv_mass, &q.size,
                        &q.normal.x, &q.normal.y, &q.normal.z, &q.plane_d, &q.life, &q.total};
        const u32 off[] = {0x08, 0x0C, 0x10, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40};
        for (size_t j = 0; j < std::size(off); ++j)
            if (e.r32(a + off[j]) != f[j]->bits)
                return fmt("particle %d: +0x%02X", k, off[j]) + fmt(" 0x%08X vs ours 0x%08X", e.r32(a + off[j]), f[j]->bits);
    }
    return {};
}

u32 make_holder(Ee& e, ByteSpan gtci, u32 car) {
    const u32 rcm = e.alloc(0x700, 16), mdl = e.alloc(0x10, 16);
    const u32 g = e.alloc_bytes(gtci, 16);
    e.w32(g + 0x5C, g + e.r32(g + 0x5C)); // (0x227168 relocates the block's offsets)
    e.w32(rcm, mdl), e.w32(mdl + 4, g), e.w32(rcm + model_car(), car);
    return rcm;
}

} // namespace gt3r::diff::sparks_probe
