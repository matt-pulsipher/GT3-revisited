// MMI (opcode 0x1C): R5900 multimedia and pipeline-1 instructions.
//
// Implemented from the EE Core Instruction Set Manual. Lane layout: a u128 is
// little-endian, so byte/half/word lane i is the i-th element counted from
// bit 0 (u128::lo holds lanes 0..half of them, u128::hi the rest).
// HI/LO: `hi.lo`/`lo.lo` are the pipeline-0 registers, `hi.hi`/`lo.hi` are
// HI1/LO1. Parallel multiplies use all four 32-bit slots of HI and LO.

#include "ee.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <limits>

namespace gt3r::diff {

namespace {

template <class T> using Lanes = std::array<T, 16 / sizeof(T)>;

template <class T> Lanes<T> L(const u128& v) {
    Lanes<T> a;
    std::memcpy(&a, &v, 16);
    return a;
}
template <class T> u128 P(const Lanes<T>& a) {
    u128 v;
    std::memcpy(static_cast<void*>(&v), &a, 16);
    return v;
}

// Lane-wise binary op on type T (use signed T for signed semantics).
template <class T, class F> u128 bin(const u128& x, const u128& y, F f) {
    auto a = L<T>(x);
    auto b = L<T>(y);
    Lanes<T> r;
    for (size_t i = 0; i < r.size(); ++i) r[i] = T(f(a[i], b[i]));
    return P<T>(r);
}
template <class T, class F> u128 un(const u128& x, F f) {
    auto a = L<T>(x);
    Lanes<T> r;
    for (size_t i = 0; i < r.size(); ++i) r[i] = T(f(a[i]));
    return P<T>(r);
}

template <class S> S sat_s(s64 v) {
    return S(std::clamp<s64>(v, std::numeric_limits<S>::min(), std::numeric_limits<S>::max()));
}
template <class U> U sat_u(s64 v) {
    return U(std::clamp<s64>(v, 0, s64(std::numeric_limits<U>::max())));
}

u128 from_w(u32 a, u32 b, u32 c, u32 d) {
    u128 r;
    r.set_w(0, a);
    r.set_w(1, b);
    r.set_w(2, c);
    r.set_w(3, d);
    return r;
}

constexpr u64 sx(u32 v) { return u64(s64(s32(v))); }

// R5900 DIV semantics for one signed 32-bit pair; writes sign-extended lo/hi.
void div_s(s32 n, s32 d, u64& lo, u64& hi) {
    if (d == 0) {
        lo = u64(s64(n < 0 ? 1 : -1));
        hi = u64(s64(n));
    } else if (n == INT32_MIN && d == -1) {
        lo = u64(s64(INT32_MIN));
        hi = 0;
    } else {
        lo = u64(s64(n / d));
        hi = u64(s64(n % d));
    }
}
void div_u(u32 n, u32 d, u64& lo, u64& hi) {
    if (d == 0) {
        lo = ~u64(0);
        hi = sx(n);
    } else {
        lo = sx(n / d);
        hi = sx(n % d);
    }
}

int lzc_word(u32 v) {
    u32 x = (v & 0x80000000u) ? ~v : v;
    int n = 0;
    while (n < 32 && !(x & (0x80000000u >> n))) ++n;
    return n - 1; // number of leading bits equal to the sign bit, excluding the sign bit
}

} // namespace

void Ee::exec_mmi(u32 op) {
    const int rs = (op >> 21) & 31, rt = (op >> 16) & 31, rd = (op >> 11) & 31;
    const u32 sub = (op >> 6) & 31;
    const u128 S = gpr[rs], T = gpr[rt];
    auto wr = [&](u128 v) { if (rd) gpr[rd] = v; };
    // Pipeline-0 / pipeline-1 three-operand multiply-accumulate on 32-bit operands.
    auto madd = [&](bool pipe1, bool is_unsigned, bool accumulate) {
        u64& L_ = pipe1 ? lo.hi : lo.lo;
        u64& H_ = pipe1 ? hi.hi : hi.lo;
        u64 acc = accumulate ? ((H_ << 32) | (L_ & 0xFFFFFFFFu)) : 0;
        u64 p = is_unsigned ? u64(g32(rs)) * u64(g32(rt)) : u64(s64(s32(g32(rs))) * s64(s32(g32(rt))));
        u64 t = acc + p;
        L_ = sx(u32(t));
        H_ = sx(u32(t >> 32));
        if (rd) gpr[rd].lo = L_;
    };

    switch (op & 63) {
    case 0x00: madd(false, false, true); return;  // MADD
    case 0x01: madd(false, true, true); return;   // MADDU
    case 0x20: madd(true, false, true); return;   // MADD1
    case 0x21: madd(true, true, true); return;    // MADDU1
    case 0x18: madd(true, false, false); return;  // MULT1
    case 0x19: madd(true, true, false); return;   // MULTU1
    case 0x04: {                                   // PLZCW
        u64 r = (u64(u32(lzc_word(S.w(1)))) << 32) | u32(lzc_word(S.w(0)));
        if (rd) gpr[rd].lo = r;
        return;
    }
    case 0x10: if (rd) gpr[rd].lo = hi.hi; return; // MFHI1
    case 0x11: hi.hi = S.lo; return;               // MTHI1
    case 0x12: if (rd) gpr[rd].lo = lo.hi; return; // MFLO1
    case 0x13: lo.hi = S.lo; return;               // MTLO1
    case 0x1A:                                     // DIV1
        div_s(s32(g32(rs)), s32(g32(rt)), lo.hi, hi.hi);
        return;
    case 0x1B:                                     // DIVU1
        div_u(g32(rs), g32(rt), lo.hi, hi.hi);
        return;
    case 0x30: {                                   // PMFHL
        u128 r;
        switch (sub) {
        case 0: r = from_w(lo.w(0), hi.w(0), lo.w(2), hi.w(2)); break; // LW
        case 1: r = from_w(lo.w(1), hi.w(1), lo.w(3), hi.w(3)); break; // UW
        case 2: {                                                       // SLW
            auto slw = [](u32 h, u32 l) -> u64 {
                s64 t = s64((u64(h) << 32) | l);
                if (t >= 0x7FFFFFFF) return 0x7FFFFFFF;
                if (t <= -0x80000000LL) return 0xFFFFFFFF80000000ull;
                return sx(l);
            };
            r.lo = slw(hi.w(0), lo.w(0));
            r.hi = slw(hi.w(2), lo.w(2));
            break;
        }
        case 3: {                                                       // LH
            Lanes<u16> a = L<u16>(lo), b = L<u16>(hi), o;
            o[0] = a[0]; o[1] = a[2]; o[2] = b[0]; o[3] = b[2];
            o[4] = a[4]; o[5] = a[6]; o[6] = b[4]; o[7] = b[6];
            r = P<u16>(o);
            break;
        }
        case 4: {                                                       // SH
            Lanes<u16> o;
            o[0] = u16(sat_s<s16>(s32(lo.w(0)))); o[1] = u16(sat_s<s16>(s32(lo.w(2))));
            o[2] = u16(sat_s<s16>(s32(hi.w(0)))); o[3] = u16(sat_s<s16>(s32(hi.w(2))));
            o[4] = u16(sat_s<s16>(s32(lo.w(1)))); o[5] = u16(sat_s<s16>(s32(lo.w(3))));
            o[6] = u16(sat_s<s16>(s32(hi.w(1)))); o[7] = u16(sat_s<s16>(s32(hi.w(3))));
            r = P<u16>(o);
            break;
        }
        default: unimplemented(op, "MMI");
        }
        wr(r);
        return;
    }
    case 0x31:                                     // PMTHL.LW
        if (sub != 0) unimplemented(op, "MMI");
        lo.set_w(0, S.w(0));
        hi.set_w(0, S.w(1));
        lo.set_w(2, S.w(2));
        hi.set_w(2, S.w(3));
        return;
    case 0x34: wr(un<u16>(T, [&](u16 v) { return u16(v << (sub & 15)); })); return;            // PSLLH
    case 0x36: wr(un<u16>(T, [&](u16 v) { return u16(v >> (sub & 15)); })); return;            // PSRLH
    case 0x37: wr(un<s16>(T, [&](s16 v) { return s16(v >> (sub & 15)); })); return;            // PSRAH
    case 0x3C: wr(un<u32>(T, [&](u32 v) { return v << sub; })); return;                        // PSLLW
    case 0x3E: wr(un<u32>(T, [&](u32 v) { return v >> sub; })); return;                        // PSRLW
    case 0x3F: wr(un<s32>(T, [&](s32 v) { return v >> sub; })); return;                        // PSRAW

    case 0x08: { // MMI0
        switch (sub) {
        case 0x00: wr(bin<u32>(S, T, [](u32 a, u32 b) { return a + b; })); return;           // PADDW
        case 0x01: wr(bin<u32>(S, T, [](u32 a, u32 b) { return a - b; })); return;           // PSUBW
        case 0x02: wr(bin<s32>(S, T, [](s32 a, s32 b) { return a > b ? -1 : 0; })); return;  // PCGTW
        case 0x03: wr(bin<s32>(S, T, [](s32 a, s32 b) { return std::max(a, b); })); return;  // PMAXW
        case 0x04: wr(bin<u16>(S, T, [](u16 a, u16 b) { return a + b; })); return;           // PADDH
        case 0x05: wr(bin<u16>(S, T, [](u16 a, u16 b) { return a - b; })); return;           // PSUBH
        case 0x06: wr(bin<s16>(S, T, [](s16 a, s16 b) { return a > b ? -1 : 0; })); return;  // PCGTH
        case 0x07: wr(bin<s16>(S, T, [](s16 a, s16 b) { return std::max(a, b); })); return;  // PMAXH
        case 0x08: wr(bin<u8>(S, T, [](u8 a, u8 b) { return a + b; })); return;              // PADDB
        case 0x09: wr(bin<u8>(S, T, [](u8 a, u8 b) { return a - b; })); return;              // PSUBB
        case 0x0A: wr(bin<std::int8_t>(S, T, [](std::int8_t a, std::int8_t b) { return a > b ? -1 : 0; })); return;     // PCGTB
        case 0x10: wr(bin<s32>(S, T, [](s32 a, s32 b) { return sat_s<s32>(s64(a) + b); })); return; // PADDSW
        case 0x11: wr(bin<s32>(S, T, [](s32 a, s32 b) { return sat_s<s32>(s64(a) - b); })); return; // PSUBSW
        case 0x12: { // PEXTLW
            wr(from_w(T.w(0), S.w(0), T.w(1), S.w(1)));
            return;
        }
        case 0x13: wr(from_w(T.w(0), T.w(2), S.w(0), S.w(2))); return; // PPACW
        case 0x14: wr(bin<s16>(S, T, [](s16 a, s16 b) { return sat_s<s16>(s32(a) + b); })); return; // PADDSH
        case 0x15: wr(bin<s16>(S, T, [](s16 a, s16 b) { return sat_s<s16>(s32(a) - b); })); return; // PSUBSH
        case 0x16: { // PEXTLH
            auto a = L<u16>(S), b = L<u16>(T);
            Lanes<u16> o;
            for (int i = 0; i < 4; ++i) { o[2 * i] = b[i]; o[2 * i + 1] = a[i]; }
            wr(P<u16>(o));
            return;
        }
        case 0x17: { // PPACH
            auto a = L<u16>(S), b = L<u16>(T);
            Lanes<u16> o;
            for (int i = 0; i < 4; ++i) { o[i] = b[2 * i]; o[4 + i] = a[2 * i]; }
            wr(P<u16>(o));
            return;
        }
        case 0x18: wr(bin<std::int8_t>(S, T, [](std::int8_t a, std::int8_t b) { return sat_s<std::int8_t>(s32(a) + b); })); return; // PADDSB
        case 0x19: wr(bin<std::int8_t>(S, T, [](std::int8_t a, std::int8_t b) { return sat_s<std::int8_t>(s32(a) - b); })); return; // PSUBSB
        case 0x1A: { // PEXTLB
            auto a = L<u8>(S), b = L<u8>(T);
            Lanes<u8> o;
            for (int i = 0; i < 8; ++i) { o[2 * i] = b[i]; o[2 * i + 1] = a[i]; }
            wr(P<u8>(o));
            return;
        }
        case 0x1B: { // PPACB
            auto a = L<u8>(S), b = L<u8>(T);
            Lanes<u8> o;
            for (int i = 0; i < 8; ++i) { o[i] = b[2 * i]; o[8 + i] = a[2 * i]; }
            wr(P<u8>(o));
            return;
        }
        case 0x1E: // PEXT5
            wr(un<u32>(T, [](u32 w) {
                return ((w & 0x001F) << 3) | ((w & 0x03E0) << 6) | ((w & 0x7C00) << 9) | ((w & 0x8000) << 16);
            }));
            return;
        case 0x1F: // PPAC5
            wr(un<u32>(T, [](u32 w) {
                return ((w & 0x000000F8) >> 3) | ((w & 0x0000F800) >> 6) | ((w & 0x00F80000) >> 9) |
                       ((w & 0x80000000u) >> 16);
            }));
            return;
        default: unimplemented(op, "MMI");
        }
    }

    case 0x28: { // MMI1
        switch (sub) {
        case 0x01: wr(un<s32>(T, [](s32 v) { return v == INT32_MIN ? INT32_MAX : (v < 0 ? -v : v); })); return; // PABSW
        case 0x02: wr(bin<u32>(S, T, [](u32 a, u32 b) { return a == b ? ~0u : 0u; })); return; // PCEQW
        case 0x03: wr(bin<s32>(S, T, [](s32 a, s32 b) { return std::min(a, b); })); return;     // PMINW
        case 0x04: { // PADSBH: low four halves subtract, high four add
            auto a = L<u16>(S), b = L<u16>(T);
            Lanes<u16> o;
            for (int i = 0; i < 4; ++i) { o[i] = u16(a[i] - b[i]); o[4 + i] = u16(a[4 + i] + b[4 + i]); }
            wr(P<u16>(o));
            return;
        }
        case 0x05: wr(un<s16>(T, [](s16 v) { return v == INT16_MIN ? INT16_MAX : (v < 0 ? -v : v); })); return; // PABSH
        case 0x06: wr(bin<u16>(S, T, [](u16 a, u16 b) { return a == b ? 0xFFFF : 0; })); return; // PCEQH
        case 0x07: wr(bin<s16>(S, T, [](s16 a, s16 b) { return std::min(a, b); })); return;       // PMINH
        case 0x0A: wr(bin<u8>(S, T, [](u8 a, u8 b) { return a == b ? 0xFF : 0; })); return;       // PCEQB
        case 0x10: wr(bin<u32>(S, T, [](u32 a, u32 b) { return sat_u<u32>(s64(a) + s64(b)); })); return; // PADDUW
        case 0x11: wr(bin<u32>(S, T, [](u32 a, u32 b) { return sat_u<u32>(s64(a) - s64(b)); })); return; // PSUBUW
        case 0x12: wr(from_w(T.w(2), S.w(2), T.w(3), S.w(3))); return;                              // PEXTUW
        case 0x14: wr(bin<u16>(S, T, [](u16 a, u16 b) { return sat_u<u16>(s32(a) + b); })); return;  // PADDUH
        case 0x15: wr(bin<u16>(S, T, [](u16 a, u16 b) { return sat_u<u16>(s32(a) - b); })); return;  // PSUBUH
        case 0x16: { // PEXTUH
            auto a = L<u16>(S), b = L<u16>(T);
            Lanes<u16> o;
            for (int i = 0; i < 4; ++i) { o[2 * i] = b[4 + i]; o[2 * i + 1] = a[4 + i]; }
            wr(P<u16>(o));
            return;
        }
        case 0x18: wr(bin<u8>(S, T, [](u8 a, u8 b) { return sat_u<u8>(s32(a) + b); })); return;  // PADDUB
        case 0x19: wr(bin<u8>(S, T, [](u8 a, u8 b) { return sat_u<u8>(s32(a) - b); })); return;  // PSUBUB
        case 0x1A: { // PEXTUB
            auto a = L<u8>(S), b = L<u8>(T);
            Lanes<u8> o;
            for (int i = 0; i < 8; ++i) { o[2 * i] = b[8 + i]; o[2 * i + 1] = a[8 + i]; }
            wr(P<u8>(o));
            return;
        }
        case 0x1B: { // QFSRV: ({rs, rt} >> (sa * 8)) low 128 bits
            u8 buf[32];
            std::memcpy(buf, &T, 16);
            std::memcpy(buf + 16, &S, 16);
            u128 r;
            std::memcpy(&r, buf + (sa & 0xF), 16);
            wr(r);
            return;
        }
        default: unimplemented(op, "MMI");
        }
    }

    case 0x09: { // MMI2
        switch (sub) {
        case 0x00: case 0x04: case 0x0C: { // PMADDW, PMSUBW, PMULTW
            for (int k = 0; k < 2; ++k) {
                u64& L_ = k ? lo.hi : lo.lo;
                u64& H_ = k ? hi.hi : hi.lo;
                s64 p = s64(s32(S.w(2 * k))) * s64(s32(T.w(2 * k)));
                s64 acc = sub == 0x0C ? 0 : s64((H_ << 32) | (L_ & 0xFFFFFFFFu));
                u64 t = u64(sub == 0x04 ? acc - p : acc + p);
                if (sub == 0x0C) t = u64(p);
                L_ = sx(u32(t));
                H_ = sx(u32(t >> 32));
                if (rd) (k ? gpr[rd].hi : gpr[rd].lo) = t;
            }
            return;
        }
        case 0x02: wr([&] { // PSLLVW
            u128 r;
            r.lo = sx(T.w(0) << (S.w(0) & 31));
            r.hi = sx(T.w(2) << (S.w(2) & 31));
            return r;
        }()); return;
        case 0x03: wr([&] { // PSRLVW
            u128 r;
            r.lo = sx(T.w(0) >> (S.w(0) & 31));
            r.hi = sx(T.w(2) >> (S.w(2) & 31));
            return r;
        }()); return;
        case 0x08: wr(hi); return; // PMFHI
        case 0x09: wr(lo); return; // PMFLO
        case 0x0A: { // PINTH
            auto a = L<u16>(S), b = L<u16>(T);
            Lanes<u16> o;
            for (int i = 0; i < 4; ++i) { o[2 * i] = b[i]; o[2 * i + 1] = a[4 + i]; }
            wr(P<u16>(o));
            return;
        }
        case 0x0D: { // PDIVW
            div_s(s32(S.w(0)), s32(T.w(0)), lo.lo, hi.lo);
            div_s(s32(S.w(2)), s32(T.w(2)), lo.hi, hi.hi);
            return;
        }
        case 0x0E: { u128 r; r.lo = T.lo; r.hi = S.lo; wr(r); return; } // PCPYLD
        case 0x10: case 0x14: case 0x1C: { // PMADDH, PMSUBH, PMULTH
            auto a = L<s16>(S), b = L<s16>(T);
            u128* lw[2] = {&lo, &hi};
            // Product i goes to LO (even i) / HI (odd i); slot order 0,1 of each 64-bit half pair.
            static const int slot[8][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {0, 2}, {1, 2}, {0, 3}, {1, 3}};
            for (int i = 0; i < 8; ++i) {
                u128& r = *lw[slot[i][0]];
                int w = slot[i][1];
                s32 p = s32(a[i]) * s32(b[i]);
                u32 cur = r.w(w);
                r.set_w(w, sub == 0x1C ? u32(p) : sub == 0x14 ? cur - u32(p) : cur + u32(p));
            }
            wr(from_w(lo.w(0), hi.w(0), lo.w(2), hi.w(2)));
            return;
        }
        case 0x11: case 0x15: { // PHMADH, PHMSBH
            auto a = L<s16>(S), b = L<s16>(T);
            auto p = [&](int i) { return s32(a[i]) * s32(b[i]); };
            auto comb = [&](int i) { return u32(sub == 0x11 ? p(i + 1) + p(i) : p(i + 1) - p(i)); };
            lo.set_w(0, comb(0)); lo.set_w(1, u32(p(1)));
            hi.set_w(0, comb(2)); hi.set_w(1, u32(p(3)));
            lo.set_w(2, comb(4)); lo.set_w(3, u32(p(5)));
            hi.set_w(2, comb(6)); hi.set_w(3, u32(p(7)));
            wr(from_w(lo.w(0), hi.w(0), lo.w(2), hi.w(2)));
            return;
        }
        case 0x12: wr(bin<u64>(S, T, [](u64 a, u64 b) { return a & b; })); return; // PAND
        case 0x13: wr(bin<u64>(S, T, [](u64 a, u64 b) { return a ^ b; })); return; // PXOR
        case 0x1A: { // PEXEH
            auto b = L<u16>(T);
            Lanes<u16> o = {b[2], b[1], b[0], b[3], b[6], b[5], b[4], b[7]};
            wr(P<u16>(o));
            return;
        }
        case 0x1B: { // PREVH
            auto b = L<u16>(T);
            Lanes<u16> o = {b[3], b[2], b[1], b[0], b[7], b[6], b[5], b[4]};
            wr(P<u16>(o));
            return;
        }
        case 0x1D: { // PDIVBW: each word of rs divided by the low halfword of rt
            s32 d = s16(T.w(0) & 0xFFFF);
            u128 l, h;
            for (int i = 0; i < 4; ++i) {
                u64 q, r;
                div_s(s32(S.w(i)), d, q, r);
                l.set_w(i, u32(q));
                h.set_w(i, u32(r));
            }
            lo = l;
            hi = h;
            return;
        }
        case 0x1E: wr(from_w(T.w(2), T.w(1), T.w(0), T.w(3))); return; // PEXEW
        case 0x1F: wr(from_w(T.w(1), T.w(2), T.w(0), T.w(3))); return; // PROT3W
        default: unimplemented(op, "MMI");
        }
    }

    case 0x29: { // MMI3
        switch (sub) {
        case 0x00: case 0x0C: { // PMADDUW, PMULTUW
            for (int k = 0; k < 2; ++k) {
                u64& L_ = k ? lo.hi : lo.lo;
                u64& H_ = k ? hi.hi : hi.lo;
                u64 p = u64(S.w(2 * k)) * u64(T.w(2 * k));
                u64 t = (sub == 0x0C ? 0 : ((H_ << 32) | (L_ & 0xFFFFFFFFu))) + p;
                L_ = sx(u32(t));
                H_ = sx(u32(t >> 32));
                if (rd) (k ? gpr[rd].hi : gpr[rd].lo) = t;
            }
            return;
        }
        case 0x03: wr([&] { // PSRAVW
            u128 r;
            r.lo = u64(s64(s32(T.w(0)) >> (S.w(0) & 31)));
            r.hi = u64(s64(s32(T.w(2)) >> (S.w(2) & 31)));
            return r;
        }()); return;
        case 0x08: hi = S; return; // PMTHI
        case 0x09: lo = S; return; // PMTLO
        case 0x0A: { // PINTEH
            auto a = L<u16>(S), b = L<u16>(T);
            Lanes<u16> o;
            for (int i = 0; i < 4; ++i) { o[2 * i] = b[2 * i]; o[2 * i + 1] = a[2 * i]; }
            wr(P<u16>(o));
            return;
        }
        case 0x0D: { // PDIVUW
            div_u(S.w(0), T.w(0), lo.lo, hi.lo);
            div_u(S.w(2), T.w(2), lo.hi, hi.hi);
            return;
        }
        case 0x0E: { u128 r; r.lo = S.hi; r.hi = T.hi; wr(r); return; } // PCPYUD
        case 0x12: wr(bin<u64>(S, T, [](u64 a, u64 b) { return a | b; })); return;  // POR
        case 0x13: wr(bin<u64>(S, T, [](u64 a, u64 b) { return ~(a | b); })); return; // PNOR
        case 0x1A: { // PEXCH
            auto b = L<u16>(T);
            Lanes<u16> o = {b[0], b[2], b[1], b[3], b[4], b[6], b[5], b[7]};
            wr(P<u16>(o));
            return;
        }
        case 0x1B: { // PCPYH
            auto b = L<u16>(T);
            Lanes<u16> o = {b[0], b[0], b[0], b[0], b[4], b[4], b[4], b[4]};
            wr(P<u16>(o));
            return;
        }
        case 0x1E: wr(from_w(T.w(0), T.w(2), T.w(1), T.w(3))); return; // PEXCW
        default: unimplemented(op, "MMI");
        }
    }
    default: unimplemented(op, "MMI");
    }
}

} // namespace gt3r::diff
