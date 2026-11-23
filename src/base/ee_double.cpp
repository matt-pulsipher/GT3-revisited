// The program's software doubles (fp-bit); see ee_double.h. The unpacked form and each step follow the program's routines:
// unpack_d 0x2790E0, unpack_f 0x27A510, pack_d 0x278FB0, pack_f 0x27A400, _fpadd_parts 0x279180, _fpmul_parts in 0x279480.

#include "base/ee_double.h"

namespace gt3r::ee {

namespace {

enum : u32 { kSnan = 0, kQnan = 1, kClassZero = 2, kNumber = 3, kInf = 4 };

// fp_number_type: a number's fraction keeps 8 guard bits below the 52 (the implicit bit at 1 << 60); a float's keeps 7 (1 << 30).
struct Parts {
    u32 cls = kClassZero, sign = 0;
    s32 exp = 0;
    u64 frac = 0;
};

constexpr u64 kImplicit1 = u64(1) << 60, kImplicit2 = u64(1) << 61, kFracHigh = u64(1) << 63;

// unpack_d (0x2790E0). The later builds' (2002 Tokyo-Geneva Europe 0x27F0E8): an Inf's fraction is the implicit bit, the
// exponent field of either 0x3FF.
Parts unpack_d(u64 v, Fp fp = Fp::Gt3) {
    Parts p;
    p.sign = u32(v >> 63);
    const u32 e = u32(v >> 52) & 0x7FF;
    const u64 f = v & 0xFFFFFFFFFFFFFull;
    if (e == 0) {
        p.cls = kClassZero;
        return p;
    }
    if (e == 0x7FF) {
        p.frac = f;
        p.cls = f == 0 ? kInf : (f & (u64(1) << 51)) ? kQnan : kSnan;
        if (fp != Fp::Gt3) {
            p.exp = 0x3FF;
            if (f == 0) p.frac = kImplicit1;
        }
        return p;
    }
    p.frac = (f << 8) | kImplicit1;
    p.exp = s32(e) - 0x3FF;
    p.cls = kNumber;
    return p;
}

// pack_d (0x278FB0). The later builds' (Concept 2001 Tokyo 0x282DF8 .. , 2002 Tokyo-Geneva Europe 0x27F010): a result below
// the smallest normal is a signed zero; 2002 Tokyo-Geneva's cuts the guard bits off.
u64 pack_d(const Parts& p, Fp fp = Fp::Gt3) {
    u64 f = p.frac;
    u32 e = 0;
    if (p.cls < 2) {
        e = 0x7FF;
        f |= u64(1) << 51;
    } else if (p.cls == kInf) {
        e = 0x7FF;
        f = 0;
    } else if (p.cls == kClassZero || f == 0) {
        f = 0;
    } else if (p.exp < -0x3FE) {
        const s32 shift = -0x3FE - p.exp;
        f = fp != Fp::Gt3 ? 0 : shift < 0x39 ? (f >> shift) >> 8 : 0;
    } else if (p.exp >= 0x400) {
        e = 0x7FF;
        f = 0;
    } else if (fp == Fp::LaterTowardZero) {
        e = u32(p.exp + 0x3FF);
        f >>= 8;
    } else {
        e = u32(p.exp + 0x3FF);
        if ((f & 0xFF) != 0x80) f += 0x7F;
        else if (f & 0x100) f += 0x80;
        if (f > 0x1FFFFFFFFFFFFFFFull) {
            f >>= 1;
            ++e;
        }
        f >>= 8;
    }
    return (u64(p.sign) << 63) | (u64(e & 0x7FF) << 52) | (f & 0xFFFFFFFFFFFFFull);
}

// _fpadd_parts (0x279180): the operand it returns (a, b, the NaN or the sum).
Parts add_parts(const Parts& a, const Parts& b) {
    if (a.cls < 2) return a;
    if (b.cls < 2) return b;
    if (a.cls == kInf) {
        if (b.cls == kInf && a.sign != b.sign) return Parts{kSnan, 0, 0, 0}; // __thenan_df
        return a;
    }
    if (b.cls == kInf) return b;
    if (b.cls == kClassZero) {
        if (a.cls != kClassZero) return a;
        Parts t = a;
        t.sign = a.sign & b.sign;
        return t;
    }
    if (a.cls == kClassZero) return b;
    s32 ae = a.exp, be = b.exp;
    u64 af = a.frac, bf = b.frac;
    const s32 diff = ae - be;
    if ((diff > -1 ? diff : -diff) < 64) {
        while (be < ae) ++be, bf = (bf >> 1) | (bf & 1);
        while (ae < be) ++ae, af = (af >> 1) | (af & 1);
    } else if (be < ae) {
        bf = 0;
    } else {
        ae = be;
        af = 0;
    }
    Parts t;
    t.exp = ae;
    if (a.sign == b.sign) {
        t.sign = a.sign;
        t.frac = af + bf;
    } else {
        const s64 d = s64(a.sign ? bf - af : af - bf);
        t.sign = d < 0;
        t.frac = d < 0 ? u64(-d) : u64(d);
        while (t.frac - 1 <= 0x0FFFFFFFFFFFFFFEull) t.frac <<= 1, --t.exp;
    }
    t.cls = kNumber;
    if (t.frac > 0x1FFFFFFFFFFFFFFFull) {
        t.frac = (t.frac >> 1) | (t.frac & 1);
        ++t.exp;
    }
    return t;
}

// _fpmul_parts inside __muldf3 (0x279480); 2002 Tokyo-Geneva's (Europe 0x27B0D0) has no rounding step.
Parts mul_parts(Parts a, Parts b, Fp fp = Fp::Gt3) {
    const u32 sign = a.sign != b.sign;
    if (a.cls < 2) return a.sign = sign, a;
    if (b.cls < 2) return b.sign = sign, b;
    if (a.cls == kInf) {
        if (b.cls == kClassZero) return Parts{kSnan, 0, 0, 0};
        return a.sign = sign, a;
    }
    if (b.cls == kInf) {
        if (a.cls == kClassZero) return Parts{kSnan, 0, 0, 0};
        return b.sign = sign, b;
    }
    if (a.cls == kClassZero) return a.sign = sign, a;
    if (b.cls == kClassZero) return b.sign = sign, b;
    const u64 nl = a.frac & 0xFFFFFFFFu, nh = a.frac >> 32, ml = b.frac & 0xFFFFFFFFu, mh = b.frac >> 32;
    const u64 pp_ll = ml * nl, pp_hl = mh * nl, pp_lh = ml * nh, pp_hh = mh * nh;
    const u64 ps = pp_hl + pp_lh;
    u64 high = ps < pp_hl ? u64(1) << 32 : 0;
    const u64 low0 = pp_ll + (ps << 32);
    high |= low0 < pp_ll;
    high += (ps >> 32) + pp_hh;
    u64 low = low0;
    Parts t;
    t.sign = sign;
    t.exp = a.exp + b.exp + 4;
    while (high >= kImplicit2) {
        ++t.exp;
        if (high & 1) low = (low >> 1) | kFracHigh;
        high >>= 1;
    }
    while (high < kImplicit1) {
        --t.exp;
        high <<= 1;
        if (low & kFracHigh) high |= 1;
        low <<= 1;
    }
    if (fp != Fp::LaterTowardZero && (high & 0xFF) == 0x80 && ((high & 0x100) || low)) high += 0x80;
    t.frac = high;
    t.cls = kNumber;
    return t;
}

} // namespace

D operator+(D a, D b) { return D::raw(pack_d(add_parts(unpack_d(a.bits), unpack_d(b.bits)))); }

D operator-(D a, D b) {
    Parts nb = unpack_d(b.bits);
    nb.sign ^= 1;
    return D::raw(pack_d(add_parts(unpack_d(a.bits), nb)));
}

D operator*(D a, D b) { return D::raw(pack_d(mul_parts(unpack_d(a.bits), unpack_d(b.bits)))); }

// unpack_f (0x27A510), then make_dp with the fraction moved up by 30 bits.
D widen(F x) {
    Parts p;
    const u32 v = x.bits;
    p.sign = v >> 31;
    const u32 e = (v >> 23) & 0xFF, f = v & 0x7FFFFF;
    if (e == 0) {
        p.cls = kClassZero;
    } else if (e == 0xFF) {
        p.frac = u64(f) << 30;
        p.cls = f == 0 ? kInf : (f & 0x100000) ? kQnan : kSnan;
    } else {
        p.frac = u64((f << 7) | 0x40000000u) << 30;
        p.exp = s32(e) - 0x7F;
        p.cls = kNumber;
    }
    return D::raw(pack_d(p));
}

// unpack_d, the fraction moved down by 30 bits (bit 0 = sticky), then pack_f (0x27A400).
F narrow(D x) {
    const Parts p = unpack_d(x.bits);
    u32 f = u32(s32(s64(p.frac << 2) >> 32));
    if (p.frac & 0x3FFFFFFF) f |= 1;
    u32 e = 0;
    if (p.cls < 2) {
        e = 0xFF;
        f |= 0x100000;
    } else if (p.cls == kInf) {
        e = 0xFF;
        f = 0;
    } else if (p.cls == kClassZero || f == 0) {
        f = 0;
    } else if (p.exp < -0x7E) {
        const s32 shift = -0x7E - p.exp;
        f = shift < 0x1A ? (f >> shift) >> 7 : 0;
    } else if (p.exp >= 0x80) {
        e = 0xFF;
        f = 0;
    } else {
        e = u32(p.exp + 0x7F);
        if ((f & 0x7F) != 0x40) f += 0x3F;
        else if (f & 0x80) f += 0x40;
        if (s32(f) < 0) {
            f >>= 1;
            ++e;
        }
        f >>= 7;
    }
    return F::raw((p.sign << 31) | ((e & 0xFF) << 23) | (f & 0x7FFFFF));
}

D add(D a, D b, Fp fp) { return D::raw(pack_d(add_parts(unpack_d(a.bits, fp), unpack_d(b.bits, fp)), fp)); }

D sub(D a, D b, Fp fp) {
    Parts nb = unpack_d(b.bits, fp);
    nb.sign ^= 1;
    return D::raw(pack_d(add_parts(unpack_d(a.bits, fp), nb), fp));
}

D mul(D a, D b, Fp fp) { return D::raw(pack_d(mul_parts(unpack_d(a.bits, fp), unpack_d(b.bits, fp), fp), fp)); }

// The later builds' __truncdfsf2 (Concept 2001 Tokyo's pack_f 0x283240 .., 2002 Tokyo-Geneva Europe 0x27F428): only the zero
// class is told apart; below the smallest normal a signed zero, exponent 128 an ordinary one, above it Inf; 2002
// Tokyo-Geneva's cuts the guard bits off.
F narrow(D x, Fp fp) {
    if (fp == Fp::Gt3) return narrow(x);
    const Parts p = unpack_d(x.bits, fp);
    u32 f = u32(s32(s64(p.frac << 2) >> 32));
    if (p.frac & 0x3FFFFFFF) f |= 1;
    u32 e = 0;
    if (p.cls == kClassZero || f == 0) {
        f = 0;
    } else if (p.exp < -0x7E) {
        f = 0;
    } else if (p.exp > 0x80) {
        e = 0xFF;
        f = 0;
    } else {
        e = u32(p.exp + 0x7F);
        if (fp == Fp::LaterTowardZero) {
            if (s32(f) < 0) e = 0xFF, f = 0; // (cannot happen: the implicit bit is bit 30)
            f >>= 7;
        } else {
            if ((f & 0x7F) != 0x40) f += 0x3F;
            else if (f & 0x80) f += 0x40;
            if (s32(f) < 0) {
                f >>= 1;
                ++e;
            }
            f >>= 7;
        }
    }
    return F::raw((p.sign << 31) | ((e & 0xFF) << 23) | (f & 0x7FFFFF));
}

} // namespace gt3r::ee
