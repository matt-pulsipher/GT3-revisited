// ee_double (src/base/ee_double.h), the program's software double: against the host's IEEE double where the two agree (normal
// operands and normal results: exact widening, correctly rounded addition, subtraction, multiplication and narrowing), and the
// rules where they differ (no denormal inputs, exponent 255 singles as Inf / NaN, denormal results truncated).

#include "base/ee_double.h"
#include "test_framework.h"

#include <bit>
#include <cmath>
#include <iterator>
#include <random>

using namespace gt3r;

namespace {

u32 exp_f(u32 f) { return (f >> 23) & 0xFF; }
u64 exp_d(u64 d) { return (d >> 52) & 0x7FF; }
u64 extend(u32 f) { return ee::widen(ee::F::raw(f)).bits; }
u64 add(u64 a, u64 b) { return (ee::D::raw(a) + ee::D::raw(b)).bits; }
u64 sub(u64 a, u64 b) { return (ee::D::raw(a) - ee::D::raw(b)).bits; }
u64 mul(u64 a, u64 b) { return (ee::D::raw(a) * ee::D::raw(b)).bits; }
u32 truncate(u64 d) { return ee::narrow(ee::D::raw(d)).bits; }

} // namespace

TEST(ee_double_matches_ieee_on_normals) {
    std::mt19937_64 rng(5);
    int bad = 0;
    for (int i = 0; i < 300000 && bad < 10; ++i) {
        const u32 f = u32(rng());
        if (exp_f(f) != 0 && exp_f(f) != 0xFF && extend(f) != std::bit_cast<u64>(double(std::bit_cast<float>(f)))) ++bad;
        // a - b over 0..70 binary orders, cancellation included
        const u64 a = (rng() & 0x800FFFFFFFFFFFFFull) | (u64(900 + rng() % 250) << 52);
        u64 b = (rng() & 0x800FFFFFFFFFFFFFull) | ((exp_d(a) - rng() % 71) << 52);
        if (i % 5 == 0) b = (a + rng() % 9 - 4) ^ (u64(rng() & 1) << 63);
        const u64 q = std::bit_cast<u64>(std::bit_cast<double>(a) - std::bit_cast<double>(b));
        if (exp_d(q) != 0 && sub(a, b) != q) ++bad;
        const u64 sum = std::bit_cast<u64>(std::bit_cast<double>(a) + std::bit_cast<double>(b));
        if (exp_d(sum) != 0 && add(a, b) != sum) ++bad;
        // products of operands within 2^+-60 (no overflow, no denormal result)
        const u64 c = (rng() & 0x800FFFFFFFFFFFFFull) | (u64(963 + rng() % 120) << 52);
        const u64 e = (rng() & 0x800FFFFFFFFFFFFFull) | (u64(963 + rng() % 120) << 52);
        if (mul(c, e) != std::bit_cast<u64>(std::bit_cast<double>(c) * std::bit_cast<double>(e))) ++bad;
        // narrowing, halfway cases included
        u64 d = (rng() & 0x800FFFFFFFFFFFFFull) | (u64(1023 - 126 + rng() % 254) << 52);
        if (i % 3 == 0) d = (d & ~u64(0x1FFFFFFF)) | (u64(1) << 28) | (rng() % 3 == 0 ? rng() % 4 : 0);
        const u32 n = std::bit_cast<u32>(float(std::bit_cast<double>(d)));
        if (exp_f(n) != 0 && exp_f(n) != 0xFF && truncate(d) != n) ++bad;
    }
    CHECK(bad == 0);
}

TEST(ee_double_rules) {
    // No denormal inputs: they read as signed zero.
    CHECK(extend(0x00000001) == 0);
    CHECK(extend(0x807FFFFF) == 0x8000000000000000ull);
    CHECK(sub(0x0000000000000001ull, 0) == 0);
    // Exponent 255 singles are Inf / NaN (the EE's largest values).
    CHECK(extend(0x7F800000) == 0x7FF0000000000000ull);
    CHECK(extend(0x7FFFFFFF) == 0x7FFFFFFFC0000000ull);
    CHECK(sub(0x7FF0000000000000ull, 0x7FF0000000000000ull) == 0x7FF8000000000000ull);
    CHECK(mul(0x7FF0000000000000ull, 0) == 0x7FF8000000000000ull);
    // A result below the smallest normal single is truncated (IEEE rounds this one up to 0x00800000).
    CHECK(truncate(0x380FFFFFF0000000ull) == 0x007FFFFF);
    // Round to nearest even, and the program's own use: 1 - 0.05 - 0.25 x 0.25.
    CHECK(truncate(0x3FF0000010000000ull) == 0x3F800000);
    CHECK(truncate(0x3FF0000030000000ull) == 0x3F800002);
    const u64 d = sub(0x3FF0000000000000ull, extend(std::bit_cast<u32>(0.05f)));
    CHECK(truncate(sub(d, extend(std::bit_cast<u32>(0.0625f)))) == std::bit_cast<u32>(float(1.0 - double(0.05f) - 0.0625)));
}

namespace {

// A hash of ee_double results over random and edge operands (every class, widened singles, near-equal pairs, exponent gaps):
// integer-only code, so it is the same on arm64 and x86-64 and with or without FMA contraction; a build that changes the
// program's double results fails here.
u64 ee_double_hash() {
    static const u64 edges[] = {0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000001ull, 0x800FFFFFFFFFFFFFull,
                                0x0010000000000000ull, 0x7FEFFFFFFFFFFFFFull, 0xFFEFFFFFFFFFFFFFull, 0x7FF0000000000000ull,
                                0xFFF0000000000000ull, 0x7FF8000000000000ull, 0x7FF0000000000001ull, 0x3FF0000000000000ull,
                                0xBFF0000000000000ull, 0x3FF0000000000001ull, 0x47EFFFFFE0000000ull, 0x380FFFFFF0000000ull};
    std::mt19937_64 rng(11);
    auto operand = [&](u64 near) -> u64 {
        const u64 r = rng();
        switch (r % 7) {
        case 0: return edges[(r >> 8) % std::size(edges)];
        case 1: return extend(u32(r >> 32));
        case 2: return (r & 0x800FFFFFFFFFFFFFull) | ((((near >> 52) & 0x7FF) + 0x7FF - (r >> 20) % 70) % 0x7FF << 52);
        case 3: return near ^ ((r >> 40) & 0xF) ^ ((r >> 3) & 1 ? 0x8000000000000000ull : 0);
        case 4: return (r & 0x800FFFFFFFFFFFFFull) | (u64(1 + (r >> 30) % 4) << 52);
        default: return r;
        }
    };
    u64 h = 1469598103934665603ull;
    auto mix = [&](u64 v) { h = (h ^ v) * 1099511628211ull; };
    for (int i = 0; i < 200000; ++i) {
        const u64 a = operand(rng()), b = operand(a);
        mix(add(a, b)), mix(sub(a, b)), mix(mul(a, b)), mix(truncate(a)), mix(extend(u32(b)));
    }
    // the AI braking distance's factor 1 + b / 100 (0x1F7088) for every s8 b: widened, x 0.01, + 1, narrowed
    for (int b = -128; b < 128; ++b) {
        const u64 w = extend(ee::int_to_f(b).bits), m = mul(w, 0x3F847AE147AE147Bull), t = add(m, 0x3FF0000000000000ull);
        mix(w), mix(m), mix(t), mix(truncate(t));
    }
    return h;
}

} // namespace

TEST(ee_double_platform_hash) { CHECK(ee_double_hash() == 0x520043706bee31d6ull); }
