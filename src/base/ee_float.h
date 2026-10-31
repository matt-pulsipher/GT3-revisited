#pragma once

// PS2 single-precision arithmetic (the EE's FPU and VU0), for code that must reproduce the original
// game's float results bit for bit.
//
// The PS2 FPUs are not IEEE 754:
//  - no denormals: an input with exponent 0 reads as (signed) zero, a result
//    below the smallest normal flushes to signed zero (underflow flag);
//  - no Inf/NaN: exponent 255 is an ordinary exponent, so the largest value
//    is 0x7FFFFFFF ~ 3.4e38 * 2; a result above it clamps to +-max (overflow);
//  - add/sub round toward zero ("chop"). The adder aligns the smaller
//    operand without a sticky bit: bits below one guard bit are dropped and an
//    operand 25+ binary orders smaller is ignored (PCSX2's accurate model);
//  - mul is the EE's multiplier: radix-4 Booth partial products summed by a
//    tree of 4:2 compressors whose carry out of the low 16 columns is lost, so
//    a product is the chopped one or, now and then, one below it;
//  - div and sqrt are the EE's divider: a radix-2 SRT recurrence on a
//    carry-save remainder, 25 digits and no correction, so a result is the
//    chopped one or one above it (never above an exact one); rsqrt divides by
//    the square root;
//  - division by zero gives +-max (sign = xor of signs), sqrt/rsqrt use |x|.
// The multiplier and the divider were worked out from their results alone; with them the disc's
// demonstration runs (pad frames recorded on a PS2) all end on the times their saves record.
//
// Two layers: functions on raw bit patterns, and ee::F, a value with those rules as operators,
// for ports that keep GT3's order of operations:
//   ee::F d = (b - a) * ee::F(0.5f);  // a subtraction then a multiplication, each chopped
// C++ keeps the written order of these operators, so each operator is one EE operation. Construct
// values with ee::F(host float) or ee::F::raw(bits); there is no implicit conversion from host floats.

#include "base/bytes.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace gt3r::ee {

enum class Round : u8 { Chop, Nearest };

struct Flags {
    bool overflow = false, underflow = false, divzero = false, invalid = false;
};

constexpr u32 kMaxPos = 0x7FFFFFFF;
constexpr u32 kMaxNeg = 0xFFFFFFFF;

// Exact value of a PS2 float (exponent 0 -> +-0, exponent 255 is normal).
double to_double(u32 bits);
// Rounds and clamps a double into a PS2 float.
u32 from_double(double v, Round r, Flags* f = nullptr);
// Host float -> PS2 float: denormals flush to zero, Inf -> +-max, NaN -> +max.
u32 from_host(float v);
// PS2 float -> host float for reporting (exponent 255 clamps to +-FLT_MAX).
float to_host(u32 bits);

u32 add(u32 a, u32 b, Round r = Round::Chop, Flags* f = nullptr);
u32 sub(u32 a, u32 b, Round r = Round::Chop, Flags* f = nullptr);
// The EE's multiplier: the chopped product, now and then one below it (see the top of this file).
u32 mul(u32 a, u32 b, Flags* f = nullptr);
// The EE's divider: the chopped quotient or one above it, never above when exact.
u32 div(u32 a, u32 b, Flags* f = nullptr);
u32 sqrt(u32 a, Flags* f = nullptr);        // sqrt(|a|): the chopped root or one above
u32 rsqrt(u32 a, u32 b, Flags* f = nullptr); // a / sqrt(|b|): the square root, then the division
// a + b * c with the product rounded first (the PS2's multiply-add); a product that overflows is the result (+-max, negated
// for msub) whatever the accumulator.
inline u32 madd(u32 acc, u32 b, u32 c, Flags* f = nullptr) {
    Flags m;
    const u32 p = mul(b, c, &m);
    if (f) f->overflow |= m.overflow, f->underflow |= m.underflow;
    return m.overflow ? p : add(acc, p, Round::Chop, f);
}
inline u32 msub(u32 acc, u32 b, u32 c, Flags* f = nullptr) {
    Flags m;
    const u32 p = mul(b, c, &m);
    if (f) f->overflow |= m.overflow, f->underflow |= m.underflow;
    return m.overflow ? (p ^ 0x80000000u) : sub(acc, p, Round::Chop, f);
}

inline u32 neg(u32 a) { return a ^ 0x80000000u; }
inline u32 abs(u32 a) { return a & 0x7FFFFFFFu; }
// -1 / 0 / 1; +0 == -0 and denormals compare as zero.
int compare(u32 a, u32 b);
u32 max(u32 a, u32 b);
u32 min(u32 a, u32 b);

// Float to int: truncates toward zero, saturates to INT32_MIN/MAX.
s32 to_int(u32 a);
// Int to float (the FPU's and VU0's conversion).
u32 from_int(s32 v, Round r = Round::Chop);
// VU0's fixed-point conversions with n fractional bits (0, 4, 12, 15).
s32 to_fixed(u32 a, int frac_bits);
u32 from_fixed(s32 v, int frac_bits);

// An EE single: the EE's addition, subtraction, multiplication, division and square root (the rules above),
// comparisons as the FPU compares (+0 == -0).
struct F {
    u32 bits = 0;
    constexpr F() = default;
    explicit F(float host) : bits(from_host(host)) {}
    static constexpr F raw(u32 b) {
        F f;
        f.bits = b;
        return f;
    }
    float host() const { return to_host(bits); }
    double value() const { return to_double(bits); }
};

// Inline fast paths of add / sub / mul (no flags), divide, square root, compare and the int conversions, for ports that
// run every frame: the same results as the functions above (tests/test_ee_float.cpp checks them against each other).
#if defined(_MSC_VER)
#define GT3R_EE_INLINE __forceinline
#else
#define GT3R_EE_INLINE inline __attribute__((always_inline))
#endif
namespace fast {
GT3R_EE_INLINE float host(u32 bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
GT3R_EE_INLINE u32 bits(float f) {
    u32 b;
    std::memcpy(&b, &f, 4);
    return b;
}
// Add and multiply in integer registers only: on phone cores moving values between the integer and float register files
// costs more than the arithmetic (Hiby R4, Cortex-A73: 9 / 4 ns per independent add / mul against 17 / 9 ns through
// doubles; the same bits).
GT3R_EE_INLINE u32 add(u32 a, u32 b) {
    // An exponent-0 operand reads as zero. The smaller magnitude keeps one guard bit below the larger's last bit (the
    // adder's alignment); the exact sum of the aligned values is chopped.
    const u32 ea = (a >> 23) & 0xFF, eb = (b >> 23) & 0xFF;
    a = ea ? a : (a & 0x80000000u);
    b = eb ? b : (b & 0x80000000u);
    const bool swap = (b & 0x7FFFFFFFu) > (a & 0x7FFFFFFFu);
    const u32 x = swap ? b : a, y = swap ? a : b; // |x| >= |y|
    const u32 ex = swap ? eb : ea, ey = swap ? ea : eb;
    if (!ey) return ex ? x : (a & b & 0x80000000u);
    const u32 big = ((x & 0x7FFFFFu) | 0x800000u) << 1; // in half ulps of x
    const u32 d = ex - ey;
    const u32 small = d > 25 ? 0 : (((y & 0x7FFFFFu) | 0x800000u) << 1) >> d;
    const u32 sum = ((x ^ y) >> 31) ? big - small : big + small;
    if (!sum) return 0;
    const int top = 31 - std::countl_zero(sum); // 0..25
    const int e = int(ex) + top - 24;
    const u32 m = top >= 23 ? (sum >> (top - 23)) : (sum << (23 - top));
    const u32 sign = x & 0x80000000u;
    if (e <= 0) return sign;
    if (e > 255) return sign | 0x7FFFFFFFu;
    return sign | (u32(e) << 23) | (m & 0x7FFFFFu);
}
GT3R_EE_INLINE u32 mul(u32 a, u32 b) {
    const u32 sign = (a ^ b) & 0x80000000u;
    const u32 ea = (a >> 23) & 0xFF, eb = (b >> 23) & 0xFF;
    if (!ea || !eb) return sign;
    const u64 p = u64((a & 0x7FFFFFu) | 0x800000u) * u64((b & 0x7FFFFFu) | 0x800000u); // [2^46, 2^48), exact
    const u32 hi = u32(p >> 47);
    // The multiplier's lost carry can only move a product whose bits below its last place are under 2^16.
    if ((p & ((u64(1) << (23 + hi)) - 1)) < (u64(1) << 16)) return ee::mul(a, b);
    const s32 e = s32(ea + eb) - 127 + s32(hi);
    const u32 m = u32(p >> (23 + hi)) & 0x7FFFFFu; // chopped
    if (e <= 0) return sign;
    if (e > 255) return sign | 0x7FFFFFFFu;
    return sign | (u32(e) << 23) | m;
}
// Divide and square root: the divider's recurrence, out of line.
GT3R_EE_INLINE u32 div(u32 a, u32 b) { return ee::div(a, b); }
GT3R_EE_INLINE u32 sqrt(u32 a) { return ee::sqrt(a); }
// Float to int (truncates toward zero, saturates) and int to float (chopped to 24 bits) in integer arithmetic.
GT3R_EE_INLINE s32 to_int(u32 a) {
    const u32 e = (a >> 23) & 0xFF;
    if (e < 127) return 0; // below 1 (exponent 0 included)
    if (e >= 127 + 31) return (a & 0x80000000u) ? s32(0x80000000u) : s32(0x7FFFFFFF);
    const u32 m = (a & 0x7FFFFFu) | 0x800000u;
    const u32 mag = e >= 150 ? m << (e - 150) : m >> (150 - e);
    return (a & 0x80000000u) ? s32(0u - mag) : s32(mag);
}
GT3R_EE_INLINE u32 from_int(s32 v) {
    if (v == 0) return 0;
    const u32 sign = v < 0 ? 0x80000000u : 0u;
    const u32 mag = v < 0 ? 0u - u32(v) : u32(v);
    const int top = 31 - std::countl_zero(mag); // 0..31
    const u32 m = top > 23 ? mag >> (top - 23) : mag << (23 - top);
    return sign | (u32(127 + top) << 23) | (m & 0x7FFFFFu);
}
// The order of sign-magnitude values with exponent-0 values as zero.
GT3R_EE_INLINE s32 key(u32 x) {
    if (!((x >> 23) & 0xFF)) return 0;
    const s32 m = s32(x & 0x7FFFFFFFu);
    return (x & 0x80000000u) ? -m : m;
}
} // namespace fast

GT3R_EE_INLINE F operator+(F a, F b) { return F::raw(fast::add(a.bits, b.bits)); }
GT3R_EE_INLINE F operator-(F a, F b) { return F::raw(fast::add(a.bits, neg(b.bits))); }
GT3R_EE_INLINE F operator*(F a, F b) { return F::raw(fast::mul(a.bits, b.bits)); }
GT3R_EE_INLINE F operator/(F a, F b) { return F::raw(fast::div(a.bits, b.bits)); }
inline F operator-(F a) { return F::raw(neg(a.bits)); }
inline F& operator+=(F& a, F b) { return a = a + b; }
inline F& operator-=(F& a, F b) { return a = a - b; }
inline F& operator*=(F& a, F b) { return a = a * b; }
GT3R_EE_INLINE bool operator==(F a, F b) { return fast::key(a.bits) == fast::key(b.bits); }
GT3R_EE_INLINE bool operator!=(F a, F b) { return fast::key(a.bits) != fast::key(b.bits); }
GT3R_EE_INLINE bool operator<(F a, F b) { return fast::key(a.bits) < fast::key(b.bits); }
GT3R_EE_INLINE bool operator<=(F a, F b) { return fast::key(a.bits) <= fast::key(b.bits); }
GT3R_EE_INLINE bool operator>(F a, F b) { return fast::key(a.bits) > fast::key(b.bits); }
GT3R_EE_INLINE bool operator>=(F a, F b) { return fast::key(a.bits) >= fast::key(b.bits); }
inline F abs(F a) { return F::raw(abs(a.bits)); }
GT3R_EE_INLINE F sqrt(F a) { return F::raw(fast::sqrt(a.bits)); } // sqrt(|a|)
inline F rsqrt(F a, F b) { return F::raw(rsqrt(a.bits, b.bits)); } // a / sqrt(|b|)
// The PS2's multiply-add: the product rounded first (an overflowed one, +-max, is the result).
GT3R_EE_INLINE F madd(F acc, F b, F c) {
    const F p = b * c;
    return (p.bits & 0x7FFFFFFFu) == 0x7FFFFFFFu ? F::raw(ee::madd(acc.bits, b.bits, c.bits)) : acc + p;
}
GT3R_EE_INLINE F msub(F acc, F b, F c) {
    const F p = b * c;
    return (p.bits & 0x7FFFFFFFu) == 0x7FFFFFFFu ? F::raw(ee::msub(acc.bits, b.bits, c.bits)) : acc - p;
}
inline F max(F a, F b) { return a >= b ? a : b; }
inline F min(F a, F b) { return a <= b ? a : b; }
GT3R_EE_INLINE s32 to_int(F a) { return fast::to_int(a.bits); }        // truncates, saturates
GT3R_EE_INLINE F int_to_f(s32 v) { return F::raw(fast::from_int(v)); } // chopped to 24 bits

// The sign of a side test (a (b - c)) - (d (e - f)), computed in this order (subtract, multiply, multiply, subtract),
// told without computing it where it is certain, so a port can leave out a test that cannot pass (the outcome is the same;
// where the sign is not certain the caller computes the test as before):
//   +1: the chopped result is >= 0 (positive, or a zero), -1: it is <= 0 (negative, or a zero), 0: not certain.
// Chopping keeps every operation within a bound of its exact result: a subtraction is off by less than 2.5 ulp of its
// larger operand (the adder drops the smaller one's bits below one guard bit, then chops), a multiplication by less than
// 1 ulp, so the result is within 2^-19 (|a| max(|b|, |c|) + |d| max(|e|, |f|)) of the exact value (plus underflows
// below 2^-126). The test evaluates the exact value in host floats (off by less than 2^-21 of the same terms) and counts
// the sign as certain only beyond 2^-14 of them (32 times the bound). The operands must be below 2^60 (no overflow
// anywhere) and exponent-0 values are zeros, as the EE reads them: side_operand gives an operand's value or false.
GT3R_EE_INLINE bool side_range(u32 x) { return ((x >> 23) & 0xFF) < 127 + 60; }
GT3R_EE_INLINE double exact_value(u32 x) { return ((x >> 23) & 0xFF) ? double(fast::host(x)) : 0.0; }
GT3R_EE_INLINE bool side_operand(F x, float& v) {
    v = ((x.bits >> 23) & 0xFF) ? fast::host(x.bits) : 0.0f;
    return side_range(x.bits);
}
GT3R_EE_INLINE int side_sign(float a, float b, float c, float d, float e, float f) {
    const float v = (a * (b - c)) - (d * (e - f));
    const float m = std::fabs(a) * std::fmax(std::fabs(b), std::fabs(c)) + std::fabs(d) * std::fmax(std::fabs(e), std::fabs(f));
    const float tol = m * 0x1p-14f + 0x1p-100f;
    return (v > tol) - (v < -tol);
}
GT3R_EE_INLINE int side_sign(F a, F b, F c, F d, F e, F f) {
    float va, vb, vc, vd, ve, vf;
    if (!side_operand(a, va) || !side_operand(b, vb) || !side_operand(c, vc) || !side_operand(d, vd) || !side_operand(e, ve) ||
        !side_operand(f, vf))
        return 0;
    return side_sign(va, vb, vc, vd, ve, vf);
}

} // namespace gt3r::ee
