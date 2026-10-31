// PS2 single-precision arithmetic; see ee_float.h.

#include "base/ee_float.h"

#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace gt3r::ee {

namespace {
inline u64 dbits(double d) {
    u64 b;
    std::memcpy(&b, &d, 8);
    return b;
}
inline double from_dbits(u64 b) {
    double d;
    std::memcpy(&d, &b, 8);
    return d;
}
} // namespace

// Bit manipulation, not libm: these run once or twice per emulated FPU operation.
double to_double(u32 bits) {
    u32 e = (bits >> 23) & 0xFF;
    u64 sign = u64(bits & 0x80000000u) << 32;
    if (e == 0) return from_dbits(sign);
    return from_dbits(sign | (u64(e + (1023 - 127)) << 52) | (u64(bits & 0x7FFFFF) << 29));
}

u32 from_double(double v, Round r, Flags* f) {
    u64 b = dbits(v);
    u32 sign = u32(b >> 32) & 0x80000000u;
    int de = int((b >> 52) & 0x7FF);
    if (de == 0x7FF) {
        if (b & 0xFFFFFFFFFFFFFull) { // NaN
            if (f) f->invalid = true;
            return kMaxPos;
        }
        if (f) f->overflow = true;
        return sign | 0x7FFFFFFFu;
    }
    if (de == 0) { // zero, or a double denormal: far below the smallest PS2 normal
        if ((b << 1) && f) f->underflow = true;
        return sign;
    }
    u64 m = b & 0xFFFFFFFFFFFFFull;
    u32 top = u32(m >> 29);
    if (r == Round::Nearest) { // ties to even
        u64 rest = m & ((1ull << 29) - 1), half = 1ull << 28;
        if (rest > half || (rest == half && (top & 1))) {
            if (++top == 0x800000u) {
                top = 0;
                ++de;
            }
        }
    }
    int field = de - 1023 + 127;
    if (field <= 0) {
        if (f) f->underflow = true;
        return sign;
    }
    if (field > 255) {
        if (f) f->overflow = true;
        return sign | 0x7FFFFFFFu;
    }
    return sign | (u32(field) << 23) | top;
}

u32 from_host(float v) {
    u32 b;
    std::memcpy(&b, &v, 4);
    u32 e = (b >> 23) & 0xFF;
    if (e == 0) return b & 0x80000000u;
    if (e == 255) return (b & 0x7FFFFF) ? kMaxPos : (b & 0x80000000u) | 0x7FFFFFFFu;
    return b;
}

float to_host(u32 bits) {
    u32 e = (bits >> 23) & 0xFF;
    if (e == 0) return (bits & 0x80000000u) ? -0.0f : 0.0f;
    if (e == 255) return (bits & 0x80000000u) ? -FLT_MAX : FLT_MAX;
    float v;
    std::memcpy(&v, &bits, 4);
    return v;
}

// Drops the low bits of the smaller operand like the PS2 adder's alignment
// (one guard bit, no sticky bit). Operands with exponent 0 are already zero.
static void align_operands(u32& a, u32& b) {
    int ea = int((a >> 23) & 0xFF), eb = int((b >> 23) & 0xFF);
    int d = ea - eb;
    if (d >= 25) b &= 0x80000000u;
    else if (d > 1) b &= 0xFFFFFFFFu << (d - 1);
    else if (d <= -25) a &= 0x80000000u;
    else if (d < -1) a &= 0xFFFFFFFFu << (-d - 1);
}

static u32 zero_if_denormal(u32 a) { return ((a >> 23) & 0xFF) ? a : (a & 0x80000000u); }

u32 add(u32 a, u32 b, Round r, Flags* f) {
    a = zero_if_denormal(a);
    b = zero_if_denormal(b);
    align_operands(a, b);
    double s = to_double(a) + to_double(b); // exact after alignment
    if (s == 0.0) {
        // IEEE: x + (-x) = +0 (round toward zero too); -0 + -0 = -0.
        bool neg = (a & b & 0x80000000u) != 0;
        return neg ? 0x80000000u : 0;
    }
    return from_double(s, r, f);
}

u32 sub(u32 a, u32 b, Round r, Flags* f) { return add(a, neg(b), r, f); }

namespace {
// The EE's multiplier as measured: radix-4 Booth rows of a's mantissa by b's (a digit of -0 is a zero row; a negative row is
// the one's complement, its +1 placed in the next row at the row's own column), four 4:2 compressors and a 3:2 in three
// levels (rows 0-3, 4-7, 8-11 and 12 alone), then a carry-propagate add of the two vectors above column 16 only: the carry
// out of the low 16 columns is lost, so a product is now and then one below the chopped exact one.
inline void full_add(u64 a, u64 b, u64 c, u64& s, u64& k) { s = a ^ b ^ c, k = ((a & b) | (a & c) | (b & c)) << 1; }
inline void compress42(u64 a, u64 b, u64 c, u64 d, u64& s, u64& k) {
    u64 s1, c1;
    full_add(b, c, d, s1, c1);
    full_add(s1, a, c1, s, k);
}
u64 booth_tree_product(u64 x, u64 y) {
    u64 r[13];
    u64 plus_one = 0;
    for (int i = 0; i < 13; ++i) {
        const int lo = i ? int((y >> (2 * i - 1)) & 1) : 0;
        const int d = -2 * int((y >> (2 * i + 1)) & 1) + int((y >> (2 * i)) & 1) + lo;
        const u64 m = x * u64(d < 0 ? -d : d);
        r[i] = (d < 0 ? ~m : m) << (2 * i);
        if (d < 0) plus_one |= u64(1) << (2 * i);
    }
    for (int i = 0; i < 12; ++i) r[i + 1] |= plus_one & (u64(1) << (2 * i));
    u64 s0, c0, s1, c1, s2, c2, s3, c3, s4, c4, s, c;
    compress42(r[0], r[1], r[2], r[3], s0, c0);
    compress42(r[4], r[5], r[6], r[7], s1, c1);
    compress42(r[8], r[9], r[10], r[11], s2, c2);
    compress42(s0, c0, s1, c1, s3, c3);
    full_add(s2, c2, r[12], s4, c4);
    compress42(s3, c3, s4, c4, s, c);
    constexpr u64 kMask = (u64(1) << 48) - 1;
    return ((((s & kMask) >> 16) + ((c & kMask) >> 16)) << 16) & kMask;
}
} // namespace

u32 mul(u32 a, u32 b, Flags* f) {
    const u32 sign = (a ^ b) & 0x80000000u;
    const u32 ea = (a >> 23) & 0xFF, eb = (b >> 23) & 0xFF;
    if (!ea || !eb) return sign;
    const u64 x = (a & 0x7FFFFFu) | 0x800000u, y = (b & 0x7FFFFFu) | 0x800000u;
    u64 p = x * y;
    const u32 hi = u32(p >> 47) & 1;
    // The lost carry is one unit of column 16: it can only move the result when the bits below the result's last place are
    // under 2^16, so only then does the tree need working out.
    if ((p & ((u64(1) << (23 + hi)) - 1)) < (u64(1) << 16)) p = booth_tree_product(x, y);
    const u32 top = u32(p >> 47) & 1;
    const s32 e = s32(ea + eb) - 127 + s32(top);
    if (e <= 0) {
        if (f) f->underflow = true;
        return sign;
    }
    if (e > 255) {
        if (f) f->overflow = true;
        return sign | 0x7FFFFFFFu;
    }
    return sign | (u32(e) << 23) | (u32(p >> (23 + top)) & 0x7FFFFFu);
}

namespace {
// The EE's divider as measured: radix-2 SRT on a carry-save remainder of 25 fraction bits (the divisor's mantissa d in
// [1/2, 1), the dividend's x / 2 to start); the digit from the top three bits of the sum and carry vectors added in a 3-bit
// adder, D, and the OR of their fourth bits: 0 when D = -1/2, or D = 0 and the OR is 0, else 1 with D's sign. After a 0
// digit the selection sees the remainder only shifted while the register takes it through the adder with 0. 25 digits
// (weights 2^0 .. 2^-24), never corrected: the top 24 bits are the result, so a quotient is the chopped one or one above.
// In software every digit's three possible remainders are formed and the digit picks one with masks (no branches: the digits
// are random). No register mask either: bits only move up, and the selection reads fixed places.
u32 srt_quotient(u32 x, u32 y) {
    const u64 d = u64(y) << 1, nd = ~d; // 25 fraction bits; -d as ~d plus a carry-in
    u64 s = x, c = 0, vs = s, vc = 0;   // x / 2
    s64 q = 0;
    for (int j = 0; j < 25; ++j) {
        const u64 s2 = s << 1, c2 = c << 1, x2 = s2 ^ c2, a2 = s2 & c2;
        const u32 top = u32((vs >> 23) + (vc >> 23)) & 7, low = u32((vs | vc) >> 22) & 1;
        const u64 zero = 0 - u64((0xC001u >> (top * 2 + low)) & 1), neg = 0 - u64(top >> 2); // 0: top 0 with low 0, or top 7
        const u64 plus = ~zero & ~neg, minus = ~zero & neg;
        s = (x2 & zero) | ((x2 ^ nd) & plus) | ((x2 ^ d) & minus);
        c = ((a2 << 1) & zero) | ((((a2 | (x2 & nd)) << 1) | 1) & plus) | (((a2 | (x2 & d)) << 1) & minus);
        vs = (s2 & zero) | (s & ~zero), vc = (c2 & zero) | (c & ~zero);
        q = q * 2 + s64(plus & 1) - s64(minus & 1);
    }
    return u32(u64(q) >> ((q >> 24) ? 1 : 0)) & 0xFFFFFFu;
}

// The same unit's square root: the root s built from 0 by 25 digits (weights 2^-1 .. 2^-25) of the same selection, the
// remainder (x' - s^2) 2^j / 2 with x' = X / 4 (X the mantissa in [1, 2), doubled for an odd power) in a register of 26
// fraction bits; the root multiple s + q 2^-(j+2) enters the adder at 25 fraction bits (complemented there, its carry-in at
// that last place). Uncorrected; the top 24 bits of s are the result.
u32 srt_root(u32 x, bool odd) {
    constexpr u64 kAdder = ~u64(1); // the multiple's bits at the adder (not its 26th fraction bit)
    u64 s = u64(x) << int(odd), c = 0, vs = s, vc = 0, root = 0; // x' / 2 and s at 26 fraction bits
    for (int j = 0; j < 25; ++j) {
        const u64 s2 = s << 1, c2 = c << 1, x2 = s2 ^ c2, a2 = s2 & c2;
        const u64 step = u64(1) << (24 - j); // 2^-(j+2)
        const u64 mp = ~(root + step) & kAdder, mn = (root - step) & kAdder; // -(s + step): complement plus a carry-in
        const u32 top = u32((vs >> 24) + (vc >> 24)) & 7, low = u32((vs | vc) >> 23) & 1;
        const u64 zero = 0 - u64((0xC001u >> (top * 2 + low)) & 1), neg = 0 - u64(top >> 2);
        const u64 plus = ~zero & ~neg, minus = ~zero & neg;
        s = (x2 & zero) | ((x2 ^ mp) & plus) | ((x2 ^ mn) & minus);
        c = ((a2 << 1) & zero) | ((((a2 | (x2 & mp)) << 1) | 2) & plus) | (((a2 | (x2 & mn)) << 1) & minus);
        vs = (s2 & zero) | (s & ~zero), vc = (c2 & zero) | (c & ~zero);
        root += (2 * step) & plus, root -= (2 * step) & minus;
    }
    return u32(root >> 2) & 0xFFFFFFu;
}
} // namespace

namespace {
// The last quotients and roots by mantissa: a race step divides the same pairs again and again (about 60% of its divisions
// and square roots repeat one of the last thousand). Direct-mapped, an entry in one word read and written whole (relaxed
// atomics: other threads share it safely), like the sine memo (ee_math.cpp). A quotient's entry holds x's high 13 bits, y's 23
// and the result's 23 (x's low 10 bits are the slot's, given y); a root's the 24-bit key and the result. Bit 63 marks it used.
class Memo {
public:
    bool find(u32 slot, u64 key, u32& r) const {
        const u64 e = e_[slot].load(std::memory_order_relaxed);
        r = u32(e) & 0x7FFFFFu;
        return (e >> 23) == key;
    }
    void put(u32 slot, u64 key, u32 r) { e_[slot].store(key << 23 | (r & 0x7FFFFFu), std::memory_order_relaxed); }

private:
    alignas(64) std::atomic<u64> e_[1024] = {};
};
constinit Memo g_quotients, g_roots;

u32 quotient(u32 x, u32 y) { // (24-bit mantissas)
    const u32 x23 = x & 0x7FFFFFu, y23 = y & 0x7FFFFFu, slot = (x23 ^ (y23 * 0x9E3779B1u >> 22)) & 1023;
    const u64 key = u64(1) << 40 | u64(x23 >> 10) << 23 | y23;
    u32 r;
    if (g_quotients.find(slot, key, r)) return r | 0x800000u;
    // The recurrence is the chopped quotient where it cannot end above it: an exact one, and for x >= y one whose dropped
    // half (the 25th digit, a guard) is under 1/2 (its remainder stays under d, so the 25 digits are within one of q).
    const bool big = x >= y;
    const u64 n = u64(x) << (big ? 23 : 24), chop = n / y, rem = n - chop * y;
    r = rem == 0 || (big && 2 * rem < y) ? u32(chop) : srt_quotient(x, y);
    g_quotients.put(slot, key, r);
    return r;
}
u32 root(u32 x, bool odd) {
    const u32 k = (x & 0x7FFFFFu) | u32(odd) << 23, slot = (k * 0x9E3779B1u) >> 22;
    const u64 key = u64(1) << 40 | k;
    u32 r;
    if (g_roots.find(slot, key, r)) return r | 0x800000u;
    // Likewise the chopped root wherever the exact root's 25th bit (the guard) is 0 (checked on every mantissa and parity).
    const u64 n = u64(x) << (27 + int(odd)); // X 2^50: its root is sqrt(X) 2^25
    u64 g = u64(std::sqrt(double(n)));       // (exact to within one; made the floor below)
    while (g * g > n) --g;
    while ((g + 1) * (g + 1) <= n) ++g;
    r = (g & 2) == 0 ? u32(g >> 2) : srt_root(x, odd);
    g_roots.put(slot, key, r);
    return r;
}
} // namespace

u32 div(u32 a, u32 b, Flags* f) {
    const u32 sign = (a ^ b) & 0x80000000u;
    const u32 ea = (a >> 23) & 0xFF, eb = (b >> 23) & 0xFF;
    if (!eb) {
        if (f) (ea ? f->divzero : f->invalid) = true;
        return sign | 0x7FFFFFFFu;
    }
    if (!ea) return sign;
    const u32 x = (a & 0x7FFFFFu) | 0x800000u, y = (b & 0x7FFFFFu) | 0x800000u;
    const s32 e = s32(ea) - s32(eb) + 126 + s32(x >= y);
    if (e <= 0) {
        if (f) f->underflow = true;
        return sign;
    }
    if (e > 255) {
        if (f) f->overflow = true;
        return sign | 0x7FFFFFFFu;
    }
    return sign | (u32(e) << 23) | (quotient(x, y) & 0x7FFFFFu);
}

u32 sqrt(u32 a, Flags* f) {
    const u32 e = (a >> 23) & 0xFF;
    if (!e) return 0;
    if ((a & 0x80000000u) && f) f->invalid = true;
    return ((e + 127) >> 1) << 23 | (root((a & 0x7FFFFFu) | 0x800000u, !(e & 1)) & 0x7FFFFFu);
}

// The square root first, then the division (the same unit twice).
u32 rsqrt(u32 a, u32 b, Flags* f) { return div(a, sqrt(b, f), f); }

int compare(u32 a, u32 b) {
    double x = to_double(a), y = to_double(b);
    return x < y ? -1 : (x > y ? 1 : 0);
}

u32 max(u32 a, u32 b) { return compare(a, b) >= 0 ? a : b; }
u32 min(u32 a, u32 b) { return compare(a, b) <= 0 ? a : b; }

s32 to_int(u32 a) {
    double v = std::trunc(to_double(a));
    if (v >= 2147483647.0) return 0x7FFFFFFF;
    if (v <= -2147483648.0) return s32(0x80000000u);
    return s32(v);
}

u32 from_int(s32 v, Round r) { return from_double(double(v), r); }

s32 to_fixed(u32 a, int frac_bits) {
    double v = std::trunc(std::ldexp(to_double(a), frac_bits));
    if (v >= 2147483647.0) return 0x7FFFFFFF;
    if (v <= -2147483648.0) return s32(0x80000000u);
    return s32(v);
}

u32 from_fixed(s32 v, int frac_bits) { return from_double(std::ldexp(double(v), -frac_bits), Round::Chop); }

} // namespace gt3r::ee
