// COP1: the EE's single-precision FPU with PS2 semantics (see ps2float.h).

#include "ee.h"
#include "ps2float.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace gt3r::diff {

// Checks only (GT3R_FLOAT_ORACLE=<shared library>, GT3R_FLOAT_ORACLE_OPS=<add,mul,div,sqrt,rsqrt; default all; "sqrt" is
// also on with "rsqrt">): FPU arithmetic from an outside float model, loaded at run time (oracle_add / _sub / _mul / _madd /
// _msub / _div / _sqrt / _rsqrt, u32 bit patterns in and out), to compare a run on it with one on ours.
namespace {
struct Oracle {
    using Fn2 = u32 (*)(u32, u32);
    using Fn3 = u32 (*)(u32, u32, u32);
    using Fn1 = u32 (*)(u32);
    Fn2 add = nullptr, sub = nullptr, mul = nullptr, div = nullptr, rsqrt = nullptr;
    Fn3 madd = nullptr, msub = nullptr;
    Fn1 sqrt = nullptr;
};
const Oracle& oracle() {
    static const Oracle o = [] {
        Oracle r;
        const char* path = std::getenv("GT3R_FLOAT_ORACLE");
        if (!path) return r;
#if defined(_WIN32)
        std::fprintf(stderr, "GT3R_FLOAT_ORACLE: not available on Windows (%s)\n", path);
        std::exit(2);
#else
        void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!lib) {
            std::fprintf(stderr, "GT3R_FLOAT_ORACLE: %s\n", dlerror());
            std::exit(2);
        }
        const char* list = std::getenv("GT3R_FLOAT_ORACLE_OPS");
        const std::string ops = list ? list : "add,mul,div,sqrt,rsqrt";
        auto want = [&](const char* op) { return ops.find(op) != std::string::npos; };
        auto sym = [&](const char* n) { return dlsym(lib, n); };
        if (want("add")) r.add = Oracle::Fn2(sym("oracle_add")), r.sub = Oracle::Fn2(sym("oracle_sub"));
        if (want("mul")) r.mul = Oracle::Fn2(sym("oracle_mul"));
        if (want("add") && want("mul")) r.madd = Oracle::Fn3(sym("oracle_madd")), r.msub = Oracle::Fn3(sym("oracle_msub"));
        if (want("div")) r.div = Oracle::Fn2(sym("oracle_div"));
        if (want("sqrt")) r.sqrt = Oracle::Fn1(sym("oracle_sqrt"));
        if (want("rsqrt")) r.rsqrt = Oracle::Fn2(sym("oracle_rsqrt"));
        std::fprintf(stderr, "float oracle %s: %s\n", path, ops.c_str());
        return r;
#endif
    }();
    return o;
}
u32 o_add(u32 a, u32 b, ps2f::Flags* fl) { return oracle().add ? oracle().add(a, b) : ps2f::add(a, b, ps2f::Round::Chop, fl); }
u32 o_sub(u32 a, u32 b, ps2f::Flags* fl) { return oracle().sub ? oracle().sub(a, b) : ps2f::sub(a, b, ps2f::Round::Chop, fl); }
u32 o_mul(u32 a, u32 b, ps2f::Flags* fl) { return oracle().mul ? oracle().mul(a, b) : ps2f::mul(a, b, fl); }
u32 o_madd(u32 acc, u32 a, u32 b, ps2f::Flags* fl) {
    return oracle().madd ? oracle().madd(acc, a, b) : ps2f::madd(acc, a, b, fl);
}
u32 o_msub(u32 acc, u32 a, u32 b, ps2f::Flags* fl) {
    return oracle().msub ? oracle().msub(acc, a, b) : ps2f::msub(acc, a, b, fl);
}
} // namespace

void oplog(u32 kind, u32 a, u32 b) {
    static FILE* out = [] { const char* p = std::getenv("GT3R_OPLOG"); return p ? std::fopen(p, "wb") : nullptr; }();
    if (!out) return;
    static const bool unique = std::getenv("GT3R_OPLOG_UNIQUE") != nullptr;
    static std::unordered_set<u64> seen[16];
    if (unique && !seen[kind & 15].insert(u64(a) << 32 | b).second) return;
    const u32 rec[3] = {kind, a, b};
    std::fwrite(rec, 4, 3, out);
}

namespace {
constexpr u32 kFcrC = 1u << 23;

void apply_flags(u32& fcr31, const ps2f::Flags& f) {
    if (f.overflow) fcr31 |= (1u << 15) | (1u << 4);
    if (f.underflow) fcr31 |= (1u << 14) | (1u << 3);
    if (f.divzero) fcr31 |= (1u << 16) | (1u << 5);
    if (f.invalid) fcr31 |= (1u << 17) | (1u << 6);
}
} // namespace

void Ee::exec_cop1(u32 op) {
    u32 fmt = (op >> 21) & 31, ft = (op >> 16) & 31, fs = (op >> 11) & 31, fd = (op >> 6) & 31;
    u32 funct = op & 63;
    ps2f::Flags fl;
    switch (fmt) {
    case 0x00: set32(ft, fpr[fs]); return;                         // mfc1 (sign-extends)
    case 0x02: set32(ft, fs == 31 ? fcr31 : (fs == 0 ? 0x2E00 : 0)); return; // cfc1
    case 0x04: fpr[fs] = g32(ft); return;                          // mtc1
    case 0x06: if (fs == 31) fcr31 = g32(ft); return;              // ctc1
    case 0x08: {                                                   // bc1f/t/fl/tl
        bool c = (fcr31 & kFcrC) != 0;
        bool want = (ft & 1) != 0, likely = (ft & 2) != 0;
        branch(c == want, s32(s16(op & 0xFFFF)), likely);
        return;
    }
    case 0x14: // W format
        if (funct == 0x20) {
            fpr[fd] = ps2f::from_int(s32(fpr[fs]), ps2f::Round::Chop);
            return;
        }
        unimplemented(op, "COP1.W");
    case 0x10: break; // S format below
    default: unimplemented(op, "COP1");
    }
    u32 a = fpr[fs], b = fpr[ft];
    switch (funct) {
    case 0x00: fpr[fd] = o_add(a, b, &fl); break;
    case 0x01: fpr[fd] = o_sub(a, b, &fl); break;
    case 0x02: oplog(1, a, b), fpr[fd] = o_mul(a, b, &fl); break;
    case 0x03: oplog(3, a, b), fpr[fd] = oracle().div ? oracle().div(a, b) : ps2f::div(a, b, &fl); break;
    case 0x04: // sqrt.s fd, ft
        oplog(4, b, 0), fpr[fd] = oracle().sqrt ? oracle().sqrt(b) : ps2f::sqrt(b, &fl);
        break;
    case 0x05: fpr[fd] = ps2f::abs(a); break;
    case 0x06: fpr[fd] = a; break;
    case 0x07: fpr[fd] = ps2f::neg(a); break;
    case 0x16: oplog(5, a, b), fpr[fd] = oracle().rsqrt ? oracle().rsqrt(a, b) : ps2f::rsqrt(a, b, &fl); break;
    case 0x18: facc = o_add(a, b, &fl); break; // adda
    case 0x19: facc = o_sub(a, b, &fl); break; // suba
    case 0x1A: oplog(2, a, b), facc = o_mul(a, b, &fl); break; // mula
    case 0x1C: oplog(6, a, b), fpr[fd] = o_madd(facc, a, b, &fl); break;
    case 0x1D: oplog(7, a, b), fpr[fd] = o_msub(facc, a, b, &fl); break;
    case 0x1E: oplog(8, a, b), facc = o_madd(facc, a, b, &fl); break;
    case 0x1F: oplog(9, a, b), facc = o_msub(facc, a, b, &fl); break;
    case 0x24: fpr[fd] = u32(ps2f::to_int(a)); break; // cvt.w.s
    case 0x28: fpr[fd] = ps2f::max(a, b); break;
    case 0x29: fpr[fd] = ps2f::min(a, b); break;
    case 0x30: fcr31 &= ~kFcrC; break; // c.f.s
    case 0x32: fcr31 = ps2f::compare(a, b) == 0 ? (fcr31 | kFcrC) : (fcr31 & ~kFcrC); break;
    case 0x34: fcr31 = ps2f::compare(a, b) < 0 ? (fcr31 | kFcrC) : (fcr31 & ~kFcrC); break;
    case 0x36: fcr31 = ps2f::compare(a, b) <= 0 ? (fcr31 | kFcrC) : (fcr31 & ~kFcrC); break;
    default: unimplemented(op, "COP1.S");
    }
    apply_flags(fcr31, fl);
}

} // namespace gt3r::diff
