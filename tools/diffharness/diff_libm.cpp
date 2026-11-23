// The game's float math library vs the host C library. Our simulation uses
// std:: functions; these tests measure how far the original's polynomial
// approximations are from them (and exercise the COP1 emulation end to end).

#include "sim/exact/build_math.h"
#include "base/ee_double.h"
#include "base/ee_math.h"
#include "harness.h"
#include "ps2float.h"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

using namespace gt3r;
using namespace gt3r::diff;

namespace {
// NB: labels.tsv had sinf/cosf swapped; this harness showed 0x26A1A0 computes
// cos and 0x26A5F0 computes sin (to 1 ulp).
constexpr u32 kCosf = 0x0026A1A0, kSinf = 0x0026A5F0, kAtan2f = 0x00269FC8, kExpf = 0x0026A270, kPowf = 0x0026A4A0;
constexpr u32 kLogf = 0x0026A388; // used by powf (labels.tsv)
constexpr u32 kSincosf = 0x0026A4D0; // (x, float* sin, float* cos)

std::string f2s(float v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.9g", double(v));
    return b;
}

template <typename Gen, typename Ref>
void unary(DiffContext& ctx, u32 fn, Gen gen, Ref ref, double abs_tol, double rel_tol) {
    for (int i = 0; i < ctx.iterations * 4; ++i) {
        float x = gen();
        float orig = ctx.ee.call(fn, {Arg::flt(x)}).f0_host();
        ctx.expect_near(orig, ref(x), abs_tol, rel_tol, [&] { return "x=" + f2s(x); });
    }
}
} // namespace

DIFF_TEST(libm_sinf, "sinf 0x26A5F0 vs std::sin") {
    unary(ctx, kSinf, [&] { return ctx.rng.uniform(-20.0f, 20.0f); }, [](float x) { return std::sin(double(x)); },
          2e-6, 0);
}

DIFF_TEST(libm_cosf, "cosf 0x26A1A0 vs std::cos") {
    unary(ctx, kCosf, [&] { return ctx.rng.uniform(-20.0f, 20.0f); }, [](float x) { return std::cos(double(x)); },
          2e-6, 0);
}

DIFF_TEST(libm_expf, "expf 0x26A270 vs std::exp") {
    unary(ctx, kExpf, [&] { return ctx.rng.uniform(-20.0f, 20.0f); }, [](float x) { return std::exp(double(x)); }, 0,
          4e-6);
}

DIFF_TEST(libm_atan2f, "atan2f 0x269FC8 vs std::atan2") {
    for (int i = 0; i < ctx.iterations * 4; ++i) {
        float y = ctx.rng.uniform(-100.0f, 100.0f), x = ctx.rng.uniform(-100.0f, 100.0f);
        if (i % 16 == 0) y = 0;
        if (i % 16 == 1) x = 0;
        float orig = ctx.ee.call(kAtan2f, {Arg::flt(y), Arg::flt(x)}).f0_host();
        ctx.expect_near(orig, std::atan2(double(y), double(x)), 2e-6, 0, [&] { return "y=" + f2s(y) + " x=" + f2s(x); });
    }
}

DIFF_TEST(libm_powf, "powf 0x26A4A0 vs std::pow") {
    ctx.ee.allow_calls({kExpf, kLogf});
    for (int i = 0; i < ctx.iterations * 2; ++i) {
        float x = ctx.rng.uniform(0.01f, 50.0f), y = ctx.rng.uniform(-3.0f, 3.0f);
        float orig = ctx.ee.call(kPowf, {Arg::flt(x), Arg::flt(y)}).f0_host();
        ctx.expect_near(orig, std::pow(double(x), double(y)), 0, 2e-5, [&] { return "x=" + f2s(x) + " y=" + f2s(y); });
    }
}

// The engine's EE ports (src/base/ee_math.h) must give the original's bits: random arguments of every size, the points next to
// multiples of pi/2 (the reductions' edges), zeros, denormals and the largest values.
DIFF_TEST_ALL(libm_trig_exact, "sinf 0x26A5F0 / cosf 0x26A1A0 / sincosf 0x26A4D0 vs ee::sin / cos / sincos, bit for bit") {
    std::vector<u32> xs = {0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x7F800000u, 0xFF800000u, 0x7FFFFFFFu, 0xFFFFFFFFu};
    for (int k = -40; k <= 40; ++k) {
        u32 c = ps2f::from_host(float(k * (std::numbers::pi / 2)));
        for (int d = -3; d <= 3; ++d) xs.push_back(c + u32(d));
    }
    for (int i = 0; i < ctx.iterations * 20; ++i) {
        switch (i % 4) {
        case 0: xs.push_back(ps2f::from_host(ctx.rng.uniform(-4.0f, 4.0f))); break;
        case 1: xs.push_back(ps2f::from_host(ctx.rng.uniform(-50.0f, 50.0f))); break;
        case 2: xs.push_back(ps2f::from_host(ctx.rng.uniform(-2000.0f, 2000.0f))); break;
        default: {
            u32 b = u32(ctx.rng.next());
            xs.push_back((b & 0x807FFFFFu) | ((100 + b % 52) << 23)); // 2^-27 .. 2^24
        }
        }
    }
    u32 ps = ctx.ee.alloc(16), pc = ctx.ee.alloc(16);
    for (u32 xb : xs) {
        u32 os = ctx.ee.call(pa(kSinf), {Arg::fbits(xb)}).f0, oc = ctx.ee.call(pa(kCosf), {Arg::fbits(xb)}).f0;
        ctx.ee.call(pa(kSincosf), {Arg::fbits(xb), Arg::ptr(ps), Arg::ptr(pc)});
        ee::F x = ee::F::raw(xb), s, c;
        ee::sincos(x, s, c);
        auto at = [&] {
            char b[32];
            std::snprintf(b, sizeof b, "x=%08X", xb);
            return std::string(b);
        };
        ctx.expect_eq(os, ee::sin(x).bits, at);
        ctx.expect_eq(oc, ee::cos(x).bits, at);
        ctx.expect_eq(ctx.ee.r32(ps), s.bits, at);
        ctx.expect_eq(ctx.ee.r32(pc), c.bits, at);
    }
}

// atanf 0x26A088 and atan2f 0x269FC8 against ee::atan / ee::atan2, bit for bit: the range edges tan(pi / 8) and tan(3 pi / 8),
// both octant edges of atan2 (|y| = |x|, y = -x), zeros of both signs, denormals, the largest values and random arguments.
DIFF_TEST_ALL(libm_atan_exact, "atanf 0x26A088 / atan2f 0x269FC8 vs ee::atan / atan2, bit for bit") {
    constexpr u32 kAtanf = 0x0026A088;
    std::vector<u32> xs = {0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x3F800000u, 0xBF800000u,
                           0x7F800000u, 0xFF800000u, 0x7FFFFFFFu, 0xFFFFFFFFu};
    for (u32 edge : {0x3ED413CDu, 0x401A827Au, 0x3F800000u})
        for (int d = -4; d <= 4; ++d) xs.push_back(edge + u32(d)), xs.push_back((edge + u32(d)) | 0x80000000u);
    for (int i = 0; i < ctx.iterations * 20; ++i) {
        switch (i % 4) {
        case 0: xs.push_back(ps2f::from_host(ctx.rng.uniform(-3.0f, 3.0f))); break;
        case 1: xs.push_back(ps2f::from_host(ctx.rng.uniform(-100.0f, 100.0f))); break;
        case 2: xs.push_back(ps2f::from_host(ctx.rng.uniform(-0.5f, 0.5f))); break;
        default: {
            u32 b = u32(ctx.rng.next());
            xs.push_back((b & 0x807FFFFFu) | ((b % 256) << 23)); // any exponent
        }
        }
    }
    for (u32 xb : xs) {
        const u32 o = ctx.ee.call(pa(kAtanf), {Arg::fbits(xb)}).f0;
        ctx.expect_eq(o, ee::atan(ee::F::raw(xb)).bits, [&] {
            char b[32];
            std::snprintf(b, sizeof b, "atan x=%08X", xb);
            return std::string(b);
        });
    }
    std::vector<std::pair<u32, u32>> yx;
    for (u32 a : {0x00000000u, 0x80000000u, 0x00000001u, 0x3F800000u, 0xBF800000u, 0x7FFFFFFFu, 0x40490FDBu})
        for (u32 b : {0x00000000u, 0x80000000u, 0x00000001u, 0x3F800000u, 0xBF800000u, 0x7FFFFFFFu, 0xC0000000u}) yx.push_back({a, b});
    for (int i = 0; i < ctx.iterations * 20; ++i) {
        const float y = ctx.rng.uniform(-50.0f, 50.0f), x = ctx.rng.uniform(-50.0f, 50.0f);
        const u32 yb = ps2f::from_host(y), xbits = ps2f::from_host(x);
        switch (i % 5) {
        case 0: yx.push_back({yb, xbits}); break;
        case 1: yx.push_back({yb, yb ^ 0x80000000u}); break;           // y = -x
        case 2: yx.push_back({yb, yb + u32(int(i % 7) - 3)}); break;    // |y| near |x|
        case 3: yx.push_back({yb, (xbits & 0x807FFFFFu) | (u32(int((yb >> 23) & 0xFF) + int(i % 61) - 30) & 0xFF) << 23}); break;
        default: yx.push_back({yb, ps2f::from_host(x * 1e-3f)}); break;
        }
    }
    for (auto [yb, xb] : yx) {
        const u32 o = ctx.ee.call(pa(kAtan2f), {Arg::fbits(yb), Arg::fbits(xb)}).f0;
        // (the disc's build's atan2f: Rule::Atan2, sim/exact/build_math.h)
        ctx.expect_eq(o, sim::exact::atan2(disc_behaviour(), ee::F::raw(yb), ee::F::raw(xb)).bits, [&] {
            char b[48];
            std::snprintf(b, sizeof b, "atan2 y=%08X x=%08X", yb, xb);
            return std::string(b);
        });
    }
}

// tanf 0x26A6C0 against ee::tan, bit for bit (the later builds' tanf is the same arithmetic, rescheduled):
// the reduction's edge pi / 4, odd multiples of pi / 4 (where the nearest quarter turn changes), zeros of both signs,
// denormals, the largest values and random arguments.
DIFF_TEST_ALL(libm_tan_exact, "tanf 0x26A6C0 vs ee::tan, bit for bit") {
    constexpr u32 kTanf = 0x0026A6C0;
    std::vector<u32> xs = {0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x3F800000u, 0xBF800000u,
                           0x7F800000u, 0xFF800000u, 0x7FFFFFFFu, 0xFFFFFFFFu, 0x4F000000u, 0xCF000000u};
    for (int m = 1; m <= 15; m += 2) // m pi / 4
        for (int d = -4; d <= 4; ++d) {
            const u32 e = ps2f::from_host(float(m) * 0.78539816f) + u32(d);
            xs.push_back(e), xs.push_back(e | 0x80000000u);
        }
    for (int i = 0; i < ctx.iterations * 20; ++i) {
        switch (i % 4) {
        case 0: xs.push_back(ps2f::from_host(ctx.rng.uniform(-3.0f, 3.0f))); break;
        case 1: xs.push_back(ps2f::from_host(ctx.rng.uniform(-100.0f, 100.0f))); break;
        case 2: xs.push_back(ps2f::from_host(ctx.rng.uniform(-0.8f, 0.8f))); break;
        default: {
            u32 b = u32(ctx.rng.next());
            xs.push_back((b & 0x807FFFFFu) | ((b % 160) << 23)); // exponents up to 2^32
        }
        }
    }
    for (u32 xb : xs) {
        const u32 o = ctx.ee.call(pa(kTanf), {Arg::fbits(xb)}).f0;
        ctx.expect_eq(o, ee::tan(ee::F::raw(xb)).bits, [&] {
            char b[32];
            std::snprintf(b, sizeof b, "tan x=%08X", xb);
            return std::string(b);
        });
    }
}

// expf 0x26A270 against ee::exp, bit for bit; the later builds' own expf (Concept 2001 Tokyo 0x26C010: another routine) against
// ee::exp_later with its constants (2002 Tokyo-Geneva's folded toward zero): around the reduction's steps (multiples of
// ln 2), the scale's ends (k + 127 at 0 and 255), zeros, denormals, Inf / NaN patterns, the largest values and random
// arguments over the range the display's callers use and far past it.
DIFF_TEST_ALL(libm_exp_exact, "expf 0x26A270 vs ee::exp (the later builds' vs ee::exp_later), bit for bit") {
    constexpr u32 kExpf = 0x0026A270;
    const bool later = sim::concept_program(program()), folded = program() >= Program::ConceptGenevaEu;
    std::vector<u32> xs = {0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x3F800000u, 0xBF800000u,
                           0x7F800000u, 0xFF800000u, 0x7FFFFFFFu, 0xFFFFFFFFu, 0x7F7FFFFFu, 0xFF7FFFFFu};
    for (int m = -130; m <= 130; ++m) // the steps of k (m ln 2) and the scale's ends
        for (int d = -2; d <= 2; ++d) xs.push_back(ps2f::from_host(float(m) * 0.6931472f) + u32(d));
    for (int i = 0; i < ctx.iterations * 20; ++i) {
        switch (i % 4) {
        case 0: xs.push_back(ps2f::from_host(ctx.rng.uniform(-10.0f, 10.0f))); break;
        case 1: xs.push_back(ps2f::from_host(ctx.rng.uniform(-90.0f, 90.0f))); break;
        case 2: xs.push_back(ps2f::from_host(ctx.rng.uniform(-0.7f, 0.7f))); break;
        default: {
            const u32 b = u32(ctx.rng.next());
            xs.push_back((b & 0x807FFFFFu) | ((b % 140) << 23)); // exponents up to 2^12
        }
        }
    }
    for (u32 xb : xs) {
        const u32 o = ctx.ee.call(pa(kExpf), {Arg::fbits(xb)}).f0;
        const ee::F x = ee::F::raw(xb);
        ctx.expect_eq(o, (later ? ee::exp_later(x, folded) : ee::exp(x)).bits, [&] {
            char b[32];
            std::snprintf(b, sizeof b, "exp x=%08X", xb);
            return std::string(b);
        });
    }
}

// The program's soft doubles (fp-bit, src/base/ee_double.h) against ee::D, bit for bit: __adddf3 0x2793C0, __subdf3 0x279418,
// __muldf3 0x279480, __extendsfdf2 0x27AFA0, __truncdfsf2 0x279C50. First every pair of edge doubles and every edge single, the
// program's own 1 - a - b of the wear curve, cancellation, alignment over 0..70 binary orders, results below the smallest
// normal, overflow, halfway roundings of the narrowing and single exponents around its range; then zeros, denormals, Inf /
// NaN, the largest and smallest normals, widened floats (what the program feeds them), sparse fractions (exact rounding ties),
// random bit patterns, near-equal pairs (cancellation) and exponent gaps up to and past 64 (alignment, sticky bit).
namespace {

const u32 kEdgeSingles[] = {0x00000000, 0x80000000, 0x00000001, 0x007FFFFF, 0x80000001, 0x00800000, 0x80800000,
                            0x3F800000, 0xBF800000, 0x3F7FFFFF, 0x33800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000,
                            0xFF800000, 0x7F800001, 0x7F900000, 0x7FC00000, 0x7FFFFFFF, 0xFFFFFFFF};
const u64 kEdgeDoubles[] = {0x0000000000000000, 0x8000000000000000, 0x0000000000000001, 0x000FFFFFFFFFFFFF,
                            0x800FFFFFFFFFFFFF, 0x0010000000000000, 0x8010000000000000, 0x3FF0000000000000,
                            0xBFF0000000000000, 0x3FF0000000000001, 0x3FEFFFFFFFFFFFFF, 0x7FEFFFFFFFFFFFFF,
                            0xFFEFFFFFFFFFFFFF, 0x7FF0000000000000, 0xFFF0000000000000, 0x7FF0000000000001,
                            0x7FF8000000000000, 0x7FFFFFFFFFFFFFFF, 0x47EFFFFFE0000000, 0x47EFFFFFF0000000,
                            0x3810000000000000, 0x380FFFFFF0000000, 0x36A0000000000000};
constexpr u64 kOneD = 0x3FF0000000000000;

u64 widen_bits(u32 f) { return ee::widen(ee::F::raw(f)).bits; }
u64 make_double(u64 sign, u64 exp, u64 frac) { return (sign << 63) | ((exp & 0x7FF) << 52) | (frac & ((u64(1) << 52) - 1)); }
template <class T, size_t N> T pick(Rng& r, const T (&a)[N]) { return a[size_t(r.range(0, s64(N) - 1))]; }

u32 rand_single(Rng& r) {
    switch (r.range(0, 3)) {
    case 0: return r.u32_();
    case 1: return pick(r, kEdgeSingles);
    case 2: return u32(r.range(0, 1) << 31) | u32(r.range(117, 137) << 23) | (r.u32_() & 0x7FFFFF); // about 2^-10 .. 2^10
    default: return ps2f::from_host(r.uniform(0.0f, 2.0f));
    }
}

u64 rand_double(Rng& r) {
    switch (r.range(0, 4)) {
    case 0: return r.next();
    case 1: return pick(r, kEdgeDoubles);
    case 2: return make_double(u64(r.range(0, 1)), u64(r.range(1023 - 40, 1023 + 40)), r.next());
    case 3: return make_double(u64(r.range(0, 1)), u64(r.range(1023 - 160, 1023 + 160)), r.next()); // single under / overflow
    default: return widen_bits(rand_single(r));
    }
}

std::string hex2(const char* op, u64 a, u64 b) {
    char s[80];
    std::snprintf(s, sizeof s, "%s %016llX %016llX", op, (unsigned long long)a, (unsigned long long)b);
    return std::string(s);
}

} // namespace

DIFF_TEST_ALL(libgcc_double_exact, "fp-bit __adddf3 / __subdf3 / __muldf3 / __extendsfdf2 / __truncdfsf2 vs ee::D, bit for bit") {
    constexpr u32 kAdd = 0x002793C0, kSub = 0x00279418, kMul = 0x00279480, kWiden = 0x0027AFA0, kNarrow = 0x00279C50;
    Rng& r = ctx.rng;
    auto widen = [&](u32 f) {
        const u64 orig = ctx.ee.call(pa(kWiden), {Arg::fbits(f)}).v0.lo;
        // (the disc's build's conversion: Rule::FloatToDouble)
        ctx.expect_eq(orig, sim::exact::widen(disc_behaviour(), ee::F::raw(f)).bits, [&] { return hex2("widen", f, 0); });
    };
    // (the disc's build's soft doubles: Rule::FloatToDouble's no denormals, Rule::SoftDouble's rounding)
    const sim::Behaviour how = disc_behaviour();
    auto pair = [&](u64 a, u64 b) {
        const u64 os = ctx.ee.call(pa(kAdd), {Arg::integer(s64(a)), Arg::integer(s64(b))}).v0.lo;
        ctx.expect_eq(os, sim::exact::add(how, ee::D::raw(a), ee::D::raw(b)).bits, [&] { return hex2("add", a, b); });
        const u64 od = ctx.ee.call(pa(kSub), {Arg::integer(s64(a)), Arg::integer(s64(b))}).v0.lo;
        ctx.expect_eq(od, sim::exact::sub(how, ee::D::raw(a), ee::D::raw(b)).bits, [&] { return hex2("sub", a, b); });
        const u64 om = ctx.ee.call(pa(kMul), {Arg::integer(s64(a)), Arg::integer(s64(b))}).v0.lo;
        ctx.expect_eq(om, sim::exact::mul(how, ee::D::raw(a), ee::D::raw(b)).bits, [&] { return hex2("mul", a, b); });
    };
    // (2001 Tokyo's and Tokyo-Seoul's unpack_d leaves an exponent-2047 double's exponent field unset, so their narrowing of
    // one reads whatever the stack holds there: not compared, counted)
    int stack_read = 0;
    auto narrow = [&](u64 d) {
        if (sim::exact::fp_rules(how) == ee::Fp::Later && ((d >> 52) & 0x7FF) == 0x7FF) {
            ++stack_read;
            return;
        }
        const u32 orig = ctx.ee.call(pa(kNarrow), {Arg::integer(s64(d))}).f0;
        ctx.expect_eq(orig, sim::exact::narrow(how, ee::D::raw(d)).bits, [&] { return hex2("narrow", d, 0); });
    };
    for (u32 f : kEdgeSingles) widen(f);
    for (int i = 0; i < ctx.iterations * 4; ++i) widen(rand_single(r));
    // the AI braking distance's factor 1 + b / 100 (0x1F7088): every s8 b as the program converts it, x 0.01, + 1, narrowed
    for (int b = -128; b < 128; ++b) {
        const u32 f = ee::int_to_f(b).bits;
        widen(f);
        const u64 w = widen_bits(f), c = 0x3F847AE147AE147Bull;
        const u64 om = ctx.ee.call(pa(kMul), {Arg::integer(s64(w)), Arg::integer(s64(c))}).v0.lo;
        ctx.expect_eq(om, sim::exact::mul(how, ee::D::raw(w), ee::D::raw(c)).bits, [&] { return hex2("mul", w, c); });
        const u64 os = ctx.ee.call(pa(kAdd), {Arg::integer(s64(om)), Arg::integer(s64(kOneD))}).v0.lo;
        ctx.expect_eq(os, sim::exact::add(how, ee::D::raw(om), ee::D::raw(kOneD)).bits, [&] { return hex2("add", om, kOneD); });
        narrow(os);
    }
    for (u64 a : kEdgeDoubles)
        for (u64 b : kEdgeDoubles) pair(a, b);
    for (int i = 0; i < ctx.iterations * 8; ++i) {
        u64 a = rand_double(r), b = rand_double(r);
        switch (i % 7) {
        case 0: // the program's (double)1 - (double)loss, then minus (double)(t x d)
            a = kOneD;
            b = widen_bits(rand_single(r));
            break;
        case 1:
            a = (ee::D::raw(kOneD) - ee::widen(ee::F::raw(rand_single(r)))).bits;
            b = widen_bits(rand_single(r));
            break;
        case 2: // cancellation
            b = (a + u64(r.range(-4, 4))) ^ (r.chance(0.5) ? u64(1) << 63 : 0);
            break;
        case 3: // alignment over 0..70 binary orders
            b = make_double(u64(r.range(0, 1)), ((a >> 52) & 0x7FF) - u64(r.range(0, 70)), r.next());
            break;
        case 4: // results below the smallest normal
            a = make_double(u64(r.range(0, 1)), u64(r.range(1, 4)), r.next());
            b = make_double(u64(r.range(0, 1)), u64(r.range(1, 4)), r.next());
            break;
        case 5: { // overflow
            const u64 s = u64(r.range(0, 1));
            a = make_double(s, 2046, r.next());
            b = make_double(s ^ 1, u64(r.range(2043, 2046)), r.next());
            break;
        }
        default: break;
        }
        pair(a, b);
    }
    for (u64 d : kEdgeDoubles) narrow(d);
    for (int i = 0; i < ctx.iterations * 4; ++i) {
        u64 d = rand_double(r);
        switch (i % 4) {
        case 0: { // half an ulp of the single (and its neighbours) above a normal single
            const u32 f = u32(r.range(0, 1) << 31) | u32(r.range(1, 254) << 23) | (r.u32_() & 0x7FFFFF);
            d = widen_bits(f) + (u64(1) << 28) + u64(r.range(-2, 2));
            break;
        }
        case 1: // single exponents -152 .. -120 and 124 .. 130
            d = make_double(u64(r.range(0, 1)), u64(r.chance(0.5) ? r.range(1023 - 152, 1023 - 120) : r.range(1023 + 124, 1023 + 130)), r.next());
            break;
        default: break;
        }
        narrow(d);
    }
    auto gen = [&](int k) -> u64 {
        const u64 x = r.next();
        switch (k % 6) {
        case 0: return pick(r, kEdgeDoubles);
        case 1: return widen_bits(ps2f::from_host(r.uniform(-3000.0f, 3000.0f)));
        case 2: { // sparse fraction
            u64 f = 0;
            for (int i = 0; i < 3; ++i) f |= u64(1) << (x >> (8 * i) & 63) % 52;
            return (x & 0x8000000000000000ull) | (u64(0x3F0 + (x >> 40) % 32) << 52) | f;
        }
        case 3: return x;
        case 4: return (x & 0x800FFFFFFFFFFFFFull) | (u64(0x3C0 + (x >> 52) % 128) << 52);
        default: return (x & 0x800FFFFFFFFFFFFFull) | (u64(1 + (x >> 52) % 2046) << 52);
        }
    };
    for (int i = 0; i < ctx.iterations * 20; ++i) {
        const u64 a = gen(i);
        u64 b = gen(i / 6 + i);
        switch (i % 5) {
        case 1: b = a ^ (r.next() & 0xF) ^ (i & 8 ? 0x8000000000000000ull : 0); break;                         // cancellation
        case 2: b = (a & 0x800FFFFFFFFFFFFFull) | (((a >> 52 & 0x7FF) + 0x7FF - u64(i % 70)) % 0x7FF << 52); break; // gap
        default: break;
        }
        pair(a, b);
        narrow(a);
        widen(i % 3 ? u32(r.next()) : ee::narrow(ee::D::raw(a)).bits);
    }
    if (stack_read) ctx.note(std::to_string(stack_read) + " narrowings of an exponent-2047 double not compared (this build's result reads the stack)");
}
