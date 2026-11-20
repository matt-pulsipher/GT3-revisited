// GT3's sinf / cosf / sincosf / atanf / atan2f / tanf; see ee_math.h. Each line is one EE operation in GT3's order (the
// multiply-adds chop the product first), with GT3's constants.

#include "base/ee_math.h"

#include <atomic>

namespace gt3r::ee {

namespace {

const F kHalfPi = F::raw(0x3FC90FDB), kInvPi = F::raw(0x3EA2F983), kPi = F::raw(0x40490FDB), kZero = F::raw(0);
const F kC1 = F::raw(0xBE2AAAA4), kC2 = F::raw(0x3C08873E), kC3 = F::raw(0xB94FB21F), kC4 = F::raw(0x362E9C14);
constexpr u32 kOne = 0x3F800000u;

// Two of the routines' operations by their exact outcome: x + 0 (adda.s with zero) is x, or +0 for a zero x; x * (+-1) is x
// with its sign flipped by the factor's, or that sign's zero for a zero x (ee::add / ee::mul's rules).
F plus_zero(F x) { return ((x.bits >> 23) & 0xFF) ? x : kZero; }
F times_sign(F x, F s) {
    const u32 sign = (x.bits ^ s.bits) & 0x80000000u;
    return F::raw(((x.bits >> 23) & 0xFF) ? sign | (x.bits & 0x7FFFFFFFu) : sign);
}

// The odd polynomial on the reduced argument r with the sign s (+-1).
F kernel(F r, F s) {
    F z = r * r;
    F sr = times_sign(r, s);
    F z2 = z * z;
    F r3 = z * sr;
    F acc = plus_zero(sr); // adda.s
    F r5 = z2 * sr;
    F r7 = z2 * r3;
    acc = madd(acc, kC1, r3);
    F r9 = r5 * z2;
    acc = madd(acc, kC2, r5);
    acc = madd(acc, kC3, r7);
    return madd(acc, kC4, r9);
}

// The last results of sinf and cosf by argument: they are functions of the argument's bits alone, and a race step takes the
// same angles again and again (a car's heading for its body axes and its ground axes, its steering, ...), about half of all
// calls. Direct-mapped, each entry the argument and its result in one word, read and written whole (relaxed atomics: races on
// other threads share it safely). Empty entries hold argument 0 (slot 0: argument 1), which never maps to them.
class Memo {
public:
    bool find(u32 x, u32& r) {
        const u64 e = e_[slot(x)].load(std::memory_order_relaxed);
        r = u32(e);
        return u32(e >> 32) == x;
    }
    void put(u32 x, u32 r) { e_[slot(x)].store(u64(x) << 32 | r, std::memory_order_relaxed); }

private:
    static constexpr int kBits = 8;
    static u32 slot(u32 x) { return (x * 0x9E3779B1u) >> (32 - kBits); }
    alignas(64) std::atomic<u64> e_[1u << kBits] = {u64(1) << 32};
};
constinit Memo g_sin, g_cos;

} // namespace

F sin(F x) {
    u32 m;
    if (g_sin.find(x.bits, m)) return F::raw(m);
    const F r = sin_uncached(x);
    g_sin.put(x.bits, r.bits);
    return r;
}

F cos(F x) {
    u32 m;
    if (g_cos.find(x.bits, m)) return F::raw(m);
    const F r = cos_uncached(x);
    g_cos.put(x.bits, r.bits);
    return r;
}

F sin_uncached(F x) {
    F t = x + kHalfPi;
    F pi = kPi; // kHalfPi + kHalfPi (a doubling: exact)
    s32 neg = s32(t.bits >> 31);
    s32 q = to_int(t * kInvPi);
    F acc = plus_zero(x);
    s32 n = s32(u32(q) - u32(neg));
    F s = F::raw((u32(neg ^ (q & 1)) << 31) | kOne);
    return kernel(msub(acc, int_to_f(n), pi), s);
}

F cos_uncached(F x) {
    s32 q = to_int(x * kInvPi);
    s32 neg = s32(x.bits >> 31);
    F pi = kPi; // kHalfPi + kHalfPi
    F acc = x - kHalfPi;
    s32 n = s32(u32(q) - u32(neg));
    F s = F::raw((u32(neg ^ (~q & 1)) << 31) | kOne);
    return kernel(msub(acc, int_to_f(n), pi), s);
}

void sincos(F x, F& sn, F& cs) {
    F qf = x * kInvPi;
    u32 mask = u32(s32(x.bits) >> 31);
    s32 q = to_int(qf);
    F y = x + F::raw(kPi.bits & mask);
    F qn = int_to_f(q);
    F rs = msub(plus_zero(y), qn, kPi);
    F s = F::raw(((mask ^ u32(q)) << 31) | kOne);
    F rs2 = madd(kPi - y, qn, kPi);
    F rc = msub(y - kHalfPi, qn, kPi);
    rs = min(rs, rs2);
    cs = kernel(rc, -s);
    sn = kernel(rs, s);
}

F atan(F x) {
    // |x| above tan(3 pi / 8) is reduced to 1 / |x| (pi / 2 - atan), above tan(pi / 8) to (1 - |x|) / (1 + |x|) (pi / 4 - atan); a
    // rational function on what is left.
    // (The program computes both reductions and picks one; each is a function of |x| alone, so only the one it picks is.)
    const F one = F::raw(kOne);
    F t = abs(x);
    int range = 0;
    if (F::raw(0x401A827A) < t) t = one / t, range = 1;
    else if (F::raw(0x3ED413CD) < t) t = (one - t) / (one + t), range = -1;
    const F z = t * t;
    F acc = one; // adda.s one + 0
    const F z2 = z * z, t3 = z * t;
    acc = madd(acc, F::raw(0x3F8E38E4), z);
    const F den = madd(acc, F::raw(0x3E73CF3D), z2);
    acc = plus_zero(t); // adda.s
    const F t5 = t3 * z;
    const F inv_den = one / den;
    acc = madd(acc, F::raw(0x3F471C72), t3);
    F r = madd(acc, F::raw(0x3D8AB356), t5) * inv_den;
    if (range < 0) r = F::raw(0x3F490FDB) - r;
    else if (range > 0) r = kHalfPi - r;
    return (x.bits & 0x80000000u) ? -r : r; // the argument's sign bit (also for -0 and negative denormals)
}

F exp(F x) {
    const F a = abs(x);
    const s32 k = to_int(F::raw(0x3FB8AA3B) * a); // 1 / ln 2
    const F r = a - int_to_f(k) * F::raw(0x3F317218); // ln 2
    const F r2 = r * r, r3 = r2 * r, r4 = r2 * r2, r5 = r3 * r2, r7 = r4 * r3, r6 = r3 * r3;
    F acc = F::raw(0x3F000000) * r; // the odd series on the accumulator
    acc = madd(acc, F::raw(0x3C834835), r3);
    acc = madd(acc, F::raw(0x38B75147), r5);
    acc = madd(acc, F::raw(0x33784D94), r7);
    const F odd = madd(acc, F{}, F{});
    acc = madd(acc, F::raw(0x3DEC4EC5), r2); // and the even one on top
    acc = madd(acc, F::raw(0x3ABEF4A9), r4);
    acc = madd(acc, F::raw(0x365943E2), r6);
    const F twice = odd + odd;
    F p = madd(acc, F::raw(0x3F800000), F::raw(0x3F800000));
    const F q = p - twice;
    p = p * F::raw((u32(k) + 0x7F) << 23); // (a 32-bit add: it wraps)
    return s32(x.bits) >= 0 ? p / q : q / p;
}

F exp_later(F x, bool folded) {
    static constexpr u32 kFirst[7] = {0x3F317218, 0x3E2AAAAB, 0x3D2AAAAB, 0x3C088889, 0x3AB60B61, 0x39500D01, 0x37D00D01};
    static constexpr u32 kFolded[7] = {0x3F317217, 0x3E2AAAAA, 0x3D2AAAAA, 0x3C088888, 0x3AB60B60, 0x39500D00, 0x37D00D00};
    const u32* c = folded ? kFolded : kFirst; // ln 2, then 1 / 3! .. 1 / 8!
    const s32 k = to_int(x * F::raw(0x3FB8AA3B));
    const F r = x - int_to_f(k) * F::raw(c[0]);
    const F r2 = r * r, r3 = r * r2, r4 = r2 * r2;
    F s = r + F::raw(0x3F800000);
    s = s + r2 * F::raw(0x3F000000);
    const F t3 = r3 * F::raw(c[1]), t4 = r4 * F::raw(c[2]);
    const F r5 = r2 * r3, r6 = r3 * r3, r7 = r3 * r4;
    s = s + t3;
    const F t5 = r5 * F::raw(c[3]), t6 = r6 * F::raw(c[4]);
    s = s + t4;
    const F t7 = r7 * F::raw(c[5]), r8 = r4 * r4;
    s = s + t5;
    const F t8 = r8 * F::raw(c[6]);
    s = (((s + t6) + t7) + t8);
    const s32 e = s32(u32(k) + 0x7F); // (a 32-bit add: a saturated k wraps below 0)
    if (e < 0) return F{};
    if (e >= 0x100) return F::raw(0x7FFFFFFF);
    return s * F::raw(u32(e) << 23);
}

F tan(F x) {
    // |x| above pi / 4 is reduced by the nearest whole number k of quarter turns (k odd: the cotangent); a rational function
    // on what is left.
    int odd = 0;
    if (F::raw(0x3F490FDB) < abs(x)) {
        const F half = F::raw((x.bits & 0x80000000u) ? 0xBF000000u : 0x3F000000u); // the argument's sign bit
        const s32 k = to_int(x * F::raw(0x3F22F983) + half);
        odd = k & 1;
        x = x - int_to_f(k) * kHalfPi;
    }
    const F z = x * x;
    F acc = F::raw(kOne); // adda.s one + 0
    const F z2 = z * z, x3 = z * x;
    acc = madd(acc, F::raw(0xBEE38E39), z);
    const F x5 = z2 * x;
    const F den = madd(acc, F::raw(0x3C820821), z2);
    acc = plus_zero(x); // adda.s
    acc = madd(acc, F::raw(0xBDE38E39), x3);
    const F num = madd(acc, F::raw(0x3A8AB356), x5);
    return odd ? -den / num : num / den;
}

F atan2(F y, F x) {
    if (x + y == kZero) return kZero;
    int oct = 0;
    if (y < kZero) y = -y, oct = 4;
    if (x < kZero) x = -x, oct |= 2;
    F r;
    if (y < x) r = atan(y / x);
    else oct |= 1, r = atan(x / y);
    if (oct & 1) r = kHalfPi - r;
    if (oct & 2) r = kPi - r;
    if (oct & 4) r = -r;
    return r;
}

} // namespace gt3r::ee
