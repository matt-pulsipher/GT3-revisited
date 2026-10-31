// ee_float (src/base/ee_float.h): the bit-manipulation conversions against a plain frexp/ldexp model of the same rules, on random
// and edge-case operands of every operation; the multiplier and divider within their bounds of the exact result and on known
// PS2 results.

#include "base/ee_float.h"
#include "base/ee_math.h"
#include "test_framework.h"

#include <cmath>
#include <random>
#include <vector>

using namespace gt3r;

namespace {

// The model: exact values through libm.
double m_to_double(u32 bits) {
    u32 e = (bits >> 23) & 0xFF;
    if (e == 0) return (bits & 0x80000000u) ? -0.0 : 0.0;
    double v = std::ldexp(double((bits & 0x7FFFFF) | 0x800000), int(e) - 127 - 23);
    return (bits & 0x80000000u) ? -v : v;
}

u32 m_from_double(double v, ee::Round r, ee::Flags* f) {
    u32 sign = std::signbit(v) ? 0x80000000u : 0;
    if (v == 0.0) return sign;
    if (std::isnan(v)) {
        f->invalid = true;
        return ee::kMaxPos;
    }
    if (std::isinf(v)) {
        f->overflow = true;
        return sign | 0x7FFFFFFFu;
    }
    int e;
    double mm = std::ldexp(std::frexp(std::fabs(v), &e), 24);
    mm = r == ee::Round::Chop ? std::trunc(mm) : std::nearbyint(mm);
    if (mm >= 16777216.0) {
        mm /= 2;
        ++e;
    }
    int field = e + 126;
    if (field <= 0) {
        f->underflow = true;
        return sign;
    }
    if (field > 255) {
        f->overflow = true;
        return sign | 0x7FFFFFFFu;
    }
    return sign | (u32(field) << 23) | (u32(mm) & 0x7FFFFF);
}

bool same(const ee::Flags& a, const ee::Flags& b) {
    return a.overflow == b.overflow && a.underflow == b.underflow && a.divzero == b.divzero && a.invalid == b.invalid;
}

// Random operands: any bit pattern, exponents near each other (the adder's alignment), near the ends of the range, zeros.
u32 operand(std::mt19937& rng, u32 near) {
    u32 r = rng();
    switch (rng() % 6) {
    case 0: return r;
    case 1: return (r & 0x807FFFFFu) | (((near >> 23) & 0xFF) + (rng() % 60) - 30) % 256 << 23;
    case 2: return (r & 0x807FFFFFu) | ((rng() % 8) << 23);
    case 3: return (r & 0x807FFFFFu) | ((248 + rng() % 8) << 23);
    case 4: return r & 0x807FFFFFu; // exponent 0
    default: return (r & 0x80000000u) | (near & 0x7FFFFFFFu) | (rng() & 1);
    }
}

} // namespace

TEST(ee_float_conversions_match_model) {
    std::mt19937 rng(7);
    int bad = 0;
    for (int i = 0; i < 400000 && bad < 10; ++i) {
        u32 a = rng();
        double d = m_to_double(a);
        if (ee::to_double(a) != d || std::signbit(ee::to_double(a)) != std::signbit(d)) ++bad;
        // doubles beyond float precision and range: products, quotients and far exponents
        double v = std::ldexp(double(rng()) * 4294967296.0 + double(rng()), int(rng() % 700) - 380 - 64) * ((rng() & 1) ? -1 : 1);
        if (i % 97 == 0) v = std::ldexp(double(rng() | 1u), int(rng() % 40) - 20); // ties and exact values
        for (ee::Round r : {ee::Round::Chop, ee::Round::Nearest}) {
            ee::Flags f1, f2;
            if (ee::from_double(v, r, &f1) != m_from_double(v, r, &f2) || !same(f1, f2)) ++bad;
        }
    }
    for (double v : {0.0, -0.0, HUGE_VAL, -HUGE_VAL, std::nan(""), 5e-324, -5e-324}) {
        for (ee::Round r : {ee::Round::Chop, ee::Round::Nearest}) {
            ee::Flags f1, f2;
            if (ee::from_double(v, r, &f1) != m_from_double(v, r, &f2) || !same(f1, f2)) ++bad;
        }
    }
    CHECK(bad == 0);
}

TEST(ee_float_operations_match_model) {
    std::mt19937 rng(11);
    int bad = 0;
    auto add_model = [](u32 a, u32 b, ee::Round r, ee::Flags* f) {
        auto dz = [](u32 x) { return ((x >> 23) & 0xFF) ? x : (x & 0x80000000u); };
        a = dz(a);
        b = dz(b);
        int d = int((a >> 23) & 0xFF) - int((b >> 23) & 0xFF);
        if (d >= 25) b &= 0x80000000u;
        else if (d > 1) b &= 0xFFFFFFFFu << (d - 1);
        else if (d <= -25) a &= 0x80000000u;
        else if (d < -1) a &= 0xFFFFFFFFu << (-d - 1);
        double s = m_to_double(a) + m_to_double(b);
        if (s == 0.0) return (a & b & 0x80000000u) ? 0x80000000u : 0u;
        return m_from_double(s, r, f);
    };
    for (int i = 0; i < 400000 && bad < 10; ++i) {
        u32 a = operand(rng, rng()), b = operand(rng, a);
        ee::Flags f1, f2;
        if (ee::add(a, b, ee::Round::Chop, &f1) != add_model(a, b, ee::Round::Chop, &f2) || !same(f1, f2)) ++bad;
        if (ee::add(a, b, ee::Round::Nearest) != add_model(a, b, ee::Round::Nearest, &f2)) ++bad;
        if (ee::sub(a, b) != add_model(a, b ^ 0x80000000u, ee::Round::Chop, &f2)) ++bad;
        // The multiplier: the chopped product, or one below it only when the bits below its last place are under 2^16 (the
        // carry it loses); the divider: the chopped quotient or root, or one above it when not exact. (Away from the
        // exponent's ends, where a step changes the exponent.)
        double p = m_to_double(a) * m_to_double(b);
        u32 pm = p == 0.0 ? (a ^ b) & 0x80000000u : m_from_double(p, ee::Round::Chop, &f2);
        const u64 p48 = u64((a & 0x7FFFFFu) | 0x800000u) * u64((b & 0x7FFFFFu) | 0x800000u);
        const u64 below = p48 & ((u64(1) << (23 + (p48 >> 47))) - 1); // the exact product's bits below pm's last place
        const u32 r = ee::mul(a, b);
        if (r != pm && !(r == pm - 1 && below < 65536 && (pm & 0x7FFFFFFFu) > 0x800000u)) ++bad;
        if (((b >> 23) & 0xFF) != 0) {
            double q = m_to_double(a) / m_to_double(b);
            u32 qm = q == 0.0 ? (a ^ b) & 0x80000000u : m_from_double(q, ee::Round::Chop, &f2);
            const u32 e = (qm >> 23) & 0xFF, r = ee::div(a, b);
            const bool exact = q == m_to_double(qm);
            const bool inside = e > 0 && e < 255 && (qm & 0x7FFFFFu) != 0x7FFFFFu;
            if (inside ? !(r == qm || (!exact && r == qm + 1)) : (e == 0 && r != qm)) ++bad;
        }
        double sq = std::sqrt(std::fabs(m_to_double(a)));
        const u32 sm = sq == 0.0 ? 0u : m_from_double(sq, ee::Round::Chop, &f2), rs = ee::sqrt(a);
        if ((sm & 0x7FFFFFu) != 0x7FFFFFu && !(rs == sm || (sq != m_to_double(sm) && rs == sm + 1))) ++bad;
        double ma = m_to_double(a), mb = m_to_double(b);
        if (ee::compare(a, b) != (ma < mb ? -1 : ma > mb ? 1 : 0)) ++bad;
        double t = std::trunc(ma);
        s32 ti = t >= 2147483647.0 ? 0x7FFFFFFF : t <= -2147483648.0 ? s32(0x80000000u) : s32(t);
        if (ee::to_int(a) != ti) ++bad;
        s32 iv = s32(rng());
        if (ee::from_int(iv) != m_from_double(double(iv), ee::Round::Chop, &f2)) ++bad;
    }
    CHECK(bad == 0);
}

// Known PS2 results where the multiplier gives one below the chopped product and the divider one above the chopped quotient
// or root (with a few plain ones), through the functions and the ee::F operators.
TEST(ee_float_ps2_known_results) {
    const u32 mul[][3] = {
        {0x42867EF9u, 0x44E2CAB1u, 0x47EE4D29u}, {0xC23EADDCu, 0x390B27B6u, 0xBBCF4BF4u}, {0xC67EABFEu, 0xC7199C76u, 0x4E18D070u},
        {0x43574BC8u, 0xC41DC545u, 0xC804AF60u}, {0x4AFD0AAFu, 0xB383B509u, 0xBF022F68u}, {0x3C0F1FDBu, 0x3F7C1EA4u, 0x3C0CF47Cu},
        {0x3F800000u, 0x3F7FFFFFu, 0x3F7FFFFEu}, {0x3F7FFFFFu, 0x3F800000u, 0x3F7FFFFFu}, // 1.0 * m low, m * 1.0 exact
    };
    const u32 div[][3] = {
        {0xBF6F746Eu, 0xBBD8AF44u, 0x430D7368u}, {0xC2F683A1u, 0x453D2682u, 0xBD26D192u}, {0xBD357760u, 0xB79071B7u, 0x4520CEAEu},
        {0xB7992194u, 0xC4AB79DFu, 0x32649CD0u}, {0x3D83815Eu, 0x456DF726u, 0x378D78C0u}, {0xB7DE9689u, 0x3A5574FFu, 0xBD0579B6u},
        {0x3F800000u, 0x40400000u, 0x3EAAAAABu}, {0x40A00000u, 0x41200000u, 0x3F000000u}, // 1 / 3, 5 / 10
    };
    const u32 sqrt[][2] = {
        {0x36CC3609u, 0x3B21ACF8u}, {0x3F5C248Cu, 0x3F6D6535u}, {0x495F7BE1u, 0x446F30A7u}, {0x371B6734u, 0x3B477510u},
        {0x3FF73693u, 0x3FB1E2B7u}, {0x40ADA8C8u, 0x40151786u}, {0x40000000u, 0x3FB504F3u}, {0x40400000u, 0x3FDDB3D7u},
    };
    for (const auto& t : mul) {
        CHECK(ee::mul(t[0], t[1]) == t[2]);
        CHECK((ee::F::raw(t[0]) * ee::F::raw(t[1])).bits == t[2]);
    }
    for (const auto& t : div) {
        CHECK(ee::div(t[0], t[1]) == t[2]);
        CHECK((ee::F::raw(t[0]) / ee::F::raw(t[1])).bits == t[2]);
    }
    for (const auto& t : sqrt) {
        CHECK(ee::sqrt(t[0]) == t[1]);
        CHECK(ee::sqrt(t[0] | 0x80000000u) == t[1]); // sqrt(|a|)
        CHECK(ee::rsqrt(0x3F800000u, t[0]) == ee::div(0x3F800000u, t[1]));
    }
    // Division by zero: +-max, and 0 / 0 flags invalid; a zero dividend gives the signed zero.
    ee::Flags f;
    CHECK(ee::div(0xC0000000u, 0x00000000u, &f) == 0xFFFFFFFFu && f.divzero);
    ee::Flags g;
    CHECK(ee::div(0x00000000u, 0x80000000u, &g) == 0xFFFFFFFFu && g.invalid);
    CHECK(ee::div(0x80000000u, 0x3F800000u) == 0x80000000u && ee::sqrt(0x80000000u) == 0);
}

TEST(ee_float_fast_paths_match) {
    // The inline operators of ee::F against the functions they stand for.
    std::mt19937 rng(23);
    int bad = 0;
    for (int i = 0; i < 400000 && bad < 10; ++i) {
        u32 a = operand(rng, rng()), b = operand(rng, a);
        ee::F x = ee::F::raw(a), y = ee::F::raw(b);
        if ((x + y).bits != ee::add(a, b) || (x - y).bits != ee::sub(a, b) || (x * y).bits != ee::mul(a, b)) ++bad;
        int c = ee::compare(a, b);
        if ((x < y) != (c < 0) || (x <= y) != (c <= 0) || (x == y) != (c == 0) || (x > y) != (c > 0) || (x >= y) != (c >= 0)) ++bad;
        if (ee::max(x, y).bits != ee::max(a, b) || ee::min(x, y).bits != ee::min(a, b)) ++bad;
        if ((x / y).bits != ee::div(a, b) || ee::sqrt(x).bits != ee::sqrt(a)) ++bad;
        u32 z = rng();
        if (ee::madd(ee::F::raw(z), x, y).bits != ee::madd(z, a, b) || ee::msub(ee::F::raw(z), x, y).bits != ee::msub(z, a, b)) ++bad;
        // float to int on any operand, int to float on any int and on small ones
        const s32 n = s32(rng()) >> (rng() % 32);
        if (ee::to_int(x) != ee::to_int(a) || ee::int_to_f(n).bits != ee::from_int(n, ee::Round::Chop)) ++bad;
    }
    for (s32 n : {0, 1, -1, 0x7FFFFFFF, s32(0x80000000u), 0xFFFFFF, 0x1000000, 0x1000001, -0x1000001})
        if (ee::int_to_f(n).bits != ee::from_int(n, ee::Round::Chop)) ++bad;
    for (u32 a : {0x4EFFFFFFu, 0x4F000000u, 0xCF000000u, 0xCEFFFFFFu, 0x3F7FFFFFu, 0xBF800000u, 0x7FFFFFFFu, 0xFFFFFFFFu, 0x80000000u})
        if (ee::to_int(ee::F::raw(a)) != ee::to_int(a)) ++bad;
    CHECK(bad == 0);
}

// ee::side_sign never claims a sign the chopped arithmetic does not give: random side tests at every scale, and tests made to
// land next to zero (a point by a line through two others, the products' exact values a few ulps apart or equal).
TEST(ee_float_side_sign_is_certain) {
    std::mt19937 rng(5);
    int wrong = 0;
    long decided = 0, random = 0;
    auto value = [&](int scale) { // a course-like coordinate or difference: |x| around 2^scale, either sign, sometimes zero
        if (rng() % 16 == 0) return ee::F::raw(0);
        const u32 e = u32(127 + scale + int(rng() % 9) - 4);
        return ee::F::raw((rng() & 0x807FFFFFu) | (e << 23));
    };
    auto check = [&](ee::F a, ee::F b, ee::F c, ee::F d, ee::F e, ee::F f) {
        const ee::F s = (a * (b - c)) - (d * (e - f));
        const int k = ee::side_sign(a, b, c, d, e, f);
        if ((k > 0 && !(ee::F::raw(0) <= s)) || (k < 0 && !(s <= ee::F::raw(0)))) ++wrong;
        return k != 0;
    };
    for (int i = 0; i < 400000; ++i) {
        const int sa = int(rng() % 24) - 12, sb = int(rng() % 24) - 4;
        ee::F a = value(sa), b = value(sb), c = value(sb), d = value(sa), e = value(sb), f = value(sb);
        decided += check(a, b, c, d, e, f), ++random;
        // next to zero: e - f chosen so d (e - f) is a (b - c) give or take a few ulps (f = e - a (b - c) / d)
        if (!(d == ee::F::raw(0))) {
            const ee::F t = (a * (b - c)) / d;
            ee::F f2 = e - t;
            for (int k = -3; k <= 3; ++k) check(a, b, c, d, e, ee::F::raw(f2.bits + u32(k)));
        }
        // collinear: b - c = (e - f) d / a exactly in the reals for small integers
        const s32 m = s32(rng() % 2001) - 1000, n = s32(rng() % 2001) - 1000, p = s32(rng() % 41) - 20;
        check(ee::int_to_f(p), ee::int_to_f(m), ee::int_to_f(n), ee::int_to_f(p), ee::int_to_f(m), ee::int_to_f(n));
    }
    CHECK(wrong == 0);
    CHECK(decided > random * 9 / 10); // (random tests are nearly all decided: the skip is worth having)
}

// The memo of sinf / cosf results (ee_math.cpp) gives what the routines give: arguments that share its entries, again and in
// turn, and every one the platform hash below takes.
TEST(ee_math_memo_matches) {
    std::mt19937 rng(11);
    int bad = 0;
    std::vector<ee::F> args;
    for (int i = 0; i < 4000; ++i) args.push_back(ee::F::raw((rng() & 0x807FFFFFu) | ((100 + rng() % 40) << 23)));
    args.push_back(ee::F::raw(0)), args.push_back(ee::F::raw(1)), args.push_back(ee::F::raw(0x80000000u));
    for (int pass = 0; pass < 3; ++pass)
        for (size_t i = 0; i < args.size(); ++i) {
            const ee::F t = args[pass == 1 ? args.size() - 1 - i : (i * 7919) % args.size()];
            if (ee::sin(t).bits != ee::sin_uncached(t).bits || ee::cos(t).bits != ee::cos_uncached(t).bits) ++bad;
        }
    CHECK(bad == 0);
}

// The same bits on every platform and compiler setting: a hash of ee::F and ee_math results over fixed random operands
// (checked equal on arm64 and x86-64, with and without FMA contraction). A failure here means a platform's build changes PS2
// float results, which would make replays and online races differ between platforms.
TEST(ee_float_platform_hash) {
    std::mt19937 rng(7);
    u64 h[4] = {1469598103934665603ull, 1469598103934665603ull, 1469598103934665603ull, 1469598103934665603ull};
    auto mix = [&](int k, u32 v) { h[k] = (h[k] ^ v) * 1099511628211ull; };
    for (int i = 0; i < 200000; ++i) {
        const u32 a = operand(rng, rng()), b = operand(rng, a), c = operand(rng, b);
        const ee::F x = ee::F::raw(a), y = ee::F::raw(b), z = ee::F::raw(c);
        mix(0, (x + y).bits ^ (x - z).bits);
        mix(1, (x * y).bits ^ ee::madd(z, x, y).bits);
        mix(2, (x / y).bits ^ ee::sqrt(x).bits);
        if (i % 8 == 0) {
            ee::F s, co;
            const ee::F t = ee::F::raw((a & 0x807FFFFFu) | ((110 + (a >> 9) % 30) << 23)); // |t| up to 2^12
            ee::sincos(t, s, co);
            mix(3, ee::sin(t).bits ^ ee::cos(t).bits ^ s.bits ^ co.bits ^ ee::atan2(x, y).bits ^ ee::atan(z).bits);
        }
    }
    CHECK(h[0] == 0x534ba27d4f41d126ull);
    CHECK(h[1] == 0xd1182518a327deccull); // (h[1] and h[2] as a reference model of the EE's FPU gives them too)
    CHECK(h[2] == 0x8b0a9ed8cf1ef085ull);
    CHECK(h[3] == 0x8692188a8ed84c56ull);
}

