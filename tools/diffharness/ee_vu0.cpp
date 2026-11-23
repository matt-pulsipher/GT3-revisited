// COP2: VU0 macro mode.
//
// Implemented: QMFC2/QMTC2/CFC2/CTC2/BC2x and the VU upper/lower instruction
// set that is reachable from macro mode (everything except VCALLMS/VCALLMSR
// and CTC2 to CMSAR1, which would start a micro program and trap as
// unimplemented).
//
// Flag model (documented approximation):
//  - MAC (16 bits): per lane, bits 0-3 overflow (w..x), 4-7 underflow, 8-11
//    sign, 12-15 zero; lane x is the highest bit of each nibble. Overflow and
//    underflow come from the ps2f flags of that lane's operation, sign from
//    the result sign bit, zero when the result magnitude is zero. Lanes outside
//    the dest mask read as clear. Only add/sub/mul/madd/msub/opmula/opmsub
//    families update MAC; MAX/MINI/ABS/MOVE/FTOI/ITOF leave it alone.
//  - Status: low nibble (Z,S,U,O = bits 0-3) mirrors "any lane" of the last
//    MAC update; sticky bits 6-9 accumulate them; DIV/SQRT/RSQRT clear I/D
//    (bits 4,5) and set them (plus sticky bits 10,11) on invalid/divide by zero.
//  - Clipping flag: 24-bit history of 6-bit results of VCLIPw.
//  - TPC/CMSAR0/FBRST are not modelled (CTC2 ignored, CFC2 reads 0; VPU-STAT
//    reads 0; the VU0 busy condition is always false).
//  - VF0/VI0 are forced to their constant values around every instruction.

#include "ee.h"
#include "ps2float.h"

#include <cstring>

namespace gt3r::diff {

namespace {

namespace f32 = ps2f;
using Fv = std::array<u32, 4>;

enum class K { Add, Sub, Mul, Madd, Msub, Max, Mini };

constexpr u32 kOne = 0x3F800000u;

void set_mac(Ee& e, u32 mac) {
    e.vmac = mac & 0xFFFF;
    u32 cur = 0;
    if (mac & 0xF000) cur |= 1;
    if (mac & 0x0F00) cur |= 2;
    if (mac & 0x00F0) cur |= 4;
    if (mac & 0x000F) cur |= 8;
    e.vstatus = (e.vstatus & ~0xFu) | cur | (cur << 6);
}

// dest-masked lane arithmetic. Writes ACC when to_acc, else vf[fd].
void arith(Ee& e, K k, u32 dest, const Fv& a, const Fv& b, bool to_acc, u32 fd) {
    Fv res{};
    u32 mac = 0;
    for (int i = 0; i < 4; ++i) {
        if (!((dest >> (3 - i)) & 1)) continue;
        f32::Flags f;
        u32 r = 0;
        switch (k) {
        case K::Add: r = f32::add(a[i], b[i], f32::Round::Chop, &f); break;
        case K::Sub: r = f32::sub(a[i], b[i], f32::Round::Chop, &f); break;
        case K::Mul: oplog(11, a[i], b[i]), r = f32::mul(a[i], b[i], &f); break;
        case K::Madd: oplog(11, a[i], b[i]), r = f32::madd(e.vacc[i], a[i], b[i], &f); break;
        case K::Msub: oplog(11, a[i], b[i]), r = f32::msub(e.vacc[i], a[i], b[i], &f); break;
        case K::Max: r = f32::max(a[i], b[i]); break;
        case K::Mini: r = f32::min(a[i], b[i]); break;
        }
        res[i] = r;
        int bit = 3 - i;
        if (f.overflow) mac |= 1u << bit;
        if (f.underflow) mac |= 1u << (4 + bit);
        if (r & 0x80000000u) mac |= 1u << (8 + bit);
        if ((r & 0x7FFFFFFFu) == 0) mac |= 1u << (12 + bit);
    }
    if (k != K::Max && k != K::Mini) set_mac(e, mac);
    for (int i = 0; i < 4; ++i) {
        if (!((dest >> (3 - i)) & 1)) continue;
        if (to_acc) e.vacc[i] = res[i];
        else if (fd) e.vf[fd][i] = res[i];
    }
}

u8* vu0_quad(Ee& e, u32 vi_value) { return e.host_ptr(Ee::kVu0MemBase + ((vi_value << 4) & (Ee::kVu0MemSize - 1)), 16); }

u32 rd_lane(const u8* p, int i) {
    u32 v;
    std::memcpy(&v, p + 4 * i, 4);
    return v;
}
void wr_lane(u8* p, int i, u32 v) { std::memcpy(p + 4 * i, &v, 4); }

void set_div_flags(Ee& e, const f32::Flags& f) {
    e.vstatus &= ~0x30u;
    if (f.invalid) e.vstatus |= 0x10u | 0x400u;
    if (f.divzero) e.vstatus |= 0x20u | 0x800u;
}

void exec_co(Ee& e, u32 op) {
    const u32 dest = (op >> 21) & 15;
    const u32 ft = (op >> 16) & 31, fs = (op >> 11) & 31, fd = (op >> 6) & 31;
    const u32 fsf = (op >> 21) & 3, ftf = (op >> 23) & 3;
    const Fv S = e.vf[fs], T = e.vf[ft];
    auto bcast = [](u32 v) { return Fv{v, v, v, v}; };
    const u32 funct = op & 63;

    auto do_op = [&](K k, const Fv& b, bool acc) { arith(e, k, dest, S, b, acc, fd); };
    static const K kGroup[7] = {K::Add, K::Sub, K::Madd, K::Msub, K::Max, K::Mini, K::Mul};

    if (funct < 0x1C) { // <op>bc, full set
        do_op(kGroup[funct >> 2], bcast(T[funct & 3]), false);
        return;
    }
    switch (funct) {
    case 0x1C: do_op(K::Mul, bcast(e.vq), false); return;
    case 0x1D: do_op(K::Max, bcast(e.vi_reg), false); return;
    case 0x1E: do_op(K::Mul, bcast(e.vi_reg), false); return;
    case 0x1F: do_op(K::Mini, bcast(e.vi_reg), false); return;
    case 0x20: do_op(K::Add, bcast(e.vq), false); return;
    case 0x21: do_op(K::Madd, bcast(e.vq), false); return;
    case 0x22: do_op(K::Add, bcast(e.vi_reg), false); return;
    case 0x23: do_op(K::Madd, bcast(e.vi_reg), false); return;
    case 0x24: do_op(K::Sub, bcast(e.vq), false); return;
    case 0x25: do_op(K::Msub, bcast(e.vq), false); return;
    case 0x26: do_op(K::Sub, bcast(e.vi_reg), false); return;
    case 0x27: do_op(K::Msub, bcast(e.vi_reg), false); return;
    case 0x28: do_op(K::Add, T, false); return;
    case 0x29: do_op(K::Madd, T, false); return;
    case 0x2A: do_op(K::Mul, T, false); return;
    case 0x2B: do_op(K::Max, T, false); return;
    case 0x2C: do_op(K::Sub, T, false); return;
    case 0x2D: do_op(K::Msub, T, false); return;
    case 0x2E: { // OPMSUB: fd.xyz = ACC - fs.yzx * ft.zxy
        arith(e, K::Msub, 0xE, Fv{S[1], S[2], S[0], 0}, Fv{T[2], T[0], T[1], 0}, false, fd);
        return;
    }
    case 0x2F: do_op(K::Mini, T, false); return;
    case 0x30: case 0x31: { // VIADD / VISUB
        u32 a = e.vi[fs & 15], b = e.vi[ft & 15];
        u16 r = u16(funct == 0x30 ? a + b : a - b);
        if (fd & 15) e.vi[fd & 15] = r;
        return;
    }
    case 0x32: { // VIADDI: it = is + imm5
        s32 imm = s32(fd << 27) >> 27;
        if (ft & 15) e.vi[ft & 15] = u16(e.vi[fs & 15] + imm);
        return;
    }
    case 0x34: if (fd & 15) e.vi[fd & 15] = e.vi[fs & 15] & e.vi[ft & 15]; return;
    case 0x35: if (fd & 15) e.vi[fd & 15] = e.vi[fs & 15] | e.vi[ft & 15]; return;
    case 0x38: case 0x39: e.unimplemented(op, "COP2 VCALLMS"); 
    default: break;
    }
    if (funct < 0x3C) e.unimplemented(op, "COP2");

    // Special2
    const u32 sub = ((op >> 4) & 0x7C) | (op & 3);
    if (sub < 0x10) { // ADDA/SUBA/MADDA/MSUBA bc
        do_op(kGroup[sub >> 2], bcast(T[sub & 3]), true);
        return;
    }
    if (sub >= 0x18 && sub <= 0x1B) { do_op(K::Mul, bcast(T[sub & 3]), true); return; }
    switch (sub) {
    case 0x10: case 0x11: case 0x12: case 0x13: { // ITOF0/4/12/15
        static const int fb[4] = {0, 4, 12, 15};
        for (int i = 0; i < 4; ++i)
            if ((dest >> (3 - i)) & 1 && ft) e.vf[ft][i] = f32::from_fixed(s32(S[i]), fb[sub & 3]);
        return;
    }
    case 0x14: case 0x15: case 0x16: case 0x17: { // FTOI0/4/12/15
        static const int fb[4] = {0, 4, 12, 15};
        for (int i = 0; i < 4; ++i)
            if ((dest >> (3 - i)) & 1 && ft) e.vf[ft][i] = u32(f32::to_fixed(S[i], fb[sub & 3]));
        return;
    }
    case 0x1C: do_op(K::Mul, bcast(e.vq), true); return;
    case 0x1D: // ABS
        for (int i = 0; i < 4; ++i)
            if ((dest >> (3 - i)) & 1 && ft) e.vf[ft][i] = f32::abs(S[i]);
        return;
    case 0x1E: do_op(K::Mul, bcast(e.vi_reg), true); return;
    case 0x1F: { // CLIPw: compare fs.xyz against |ft.w|
        u32 w = f32::abs(T[3]), nw = f32::neg(w);
        u32 bits = 0;
        for (int i = 0; i < 3; ++i) {
            if (f32::compare(S[i], w) > 0) bits |= 1u << (2 * i);
            if (f32::compare(S[i], nw) < 0) bits |= 2u << (2 * i);
        }
        e.vclip = ((e.vclip << 6) | bits) & 0xFFFFFF;
        return;
    }
    case 0x20: do_op(K::Add, bcast(e.vq), true); return;
    case 0x21: do_op(K::Madd, bcast(e.vq), true); return;
    case 0x22: do_op(K::Add, bcast(e.vi_reg), true); return;
    case 0x23: do_op(K::Madd, bcast(e.vi_reg), true); return;
    case 0x24: do_op(K::Sub, bcast(e.vq), true); return;
    case 0x25: do_op(K::Msub, bcast(e.vq), true); return;
    case 0x26: do_op(K::Sub, bcast(e.vi_reg), true); return;
    case 0x27: do_op(K::Msub, bcast(e.vi_reg), true); return;
    case 0x28: do_op(K::Add, T, true); return;
    case 0x29: do_op(K::Madd, T, true); return;
    case 0x2A: do_op(K::Mul, T, true); return;
    case 0x2C: do_op(K::Sub, T, true); return;
    case 0x2D: do_op(K::Msub, T, true); return;
    case 0x2E: // OPMULA: ACC.xyz = fs.yzx * ft.zxy
        arith(e, K::Mul, 0xE, Fv{S[1], S[2], S[0], 0}, Fv{T[2], T[0], T[1], 0}, true, 0);
        return;
    case 0x2F: return; // VNOP
    case 0x30: // VMOVE
        for (int i = 0; i < 4; ++i)
            if ((dest >> (3 - i)) & 1 && ft) e.vf[ft][i] = S[i];
        return;
    case 0x31: // VMR32
        for (int i = 0; i < 4; ++i)
            if ((dest >> (3 - i)) & 1 && ft) e.vf[ft][i] = S[(i + 1) & 3];
        return;
    case 0x34: case 0x36: { // VLQI / VLQD: ft = mem[is]
        u32 is = fs & 15;
        if (sub == 0x36 && is) --e.vi[is];
        const u8* p = vu0_quad(e, e.vi[is]);
        for (int i = 0; i < 4; ++i)
            if ((dest >> (3 - i)) & 1 && ft) e.vf[ft][i] = rd_lane(p, i);
        if (sub == 0x34 && is) ++e.vi[is];
        return;
    }
    case 0x35: case 0x37: { // VSQI / VSQD: mem[it] = fs
        u32 it = ft & 15;
        if (sub == 0x37 && it) --e.vi[it];
        u8* p = vu0_quad(e, e.vi[it]);
        for (int i = 0; i < 4; ++i)
            if ((dest >> (3 - i)) & 1) wr_lane(p, i, S[i]);
        if (sub == 0x35 && it) ++e.vi[it];
        return;
    }
    case 0x38: { // VDIV
        f32::Flags f;
        oplog(13, S[fsf], T[ftf]), e.vq = f32::div(S[fsf], T[ftf], &f);
        set_div_flags(e, f);
        return;
    }
    case 0x39: { // VSQRT
        f32::Flags f;
        oplog(14, T[ftf], 0), e.vq = f32::sqrt(T[ftf], &f);
        set_div_flags(e, f);
        return;
    }
    case 0x3A: { // VRSQRT
        f32::Flags f;
        oplog(15, S[fsf], T[ftf]), e.vq = f32::rsqrt(S[fsf], T[ftf], &f);
        set_div_flags(e, f);
        return;
    }
    case 0x3B: return; // VWAITQ
    case 0x3C: if (ft & 15) e.vi[ft & 15] = u16(S[fsf]); return; // VMTIR
    case 0x3D: // VMFIR
        for (int i = 0; i < 4; ++i)
            if ((dest >> (3 - i)) & 1 && ft) e.vf[ft][i] = u32(s32(s16(e.vi[fs & 15])));
        return;
    case 0x3E: { // VILWR: it = mem[is].(first dest lane), low 16 bits
        const u8* p = vu0_quad(e, e.vi[fs & 15]);
        for (int i = 0; i < 4; ++i) {
            if ((dest >> (3 - i)) & 1) {
                if (ft & 15) e.vi[ft & 15] = u16(rd_lane(p, i));
                break;
            }
        }
        return;
    }
    case 0x3F: { // VISWR: mem[is].dest = it (zero-extended)
        u8* p = vu0_quad(e, e.vi[fs & 15]);
        for (int i = 0; i < 4; ++i)
            if ((dest >> (3 - i)) & 1) wr_lane(p, i, e.vi[ft & 15]);
        return;
    }
    case 0x40: { // VRNEXT: standard 23-bit LFSR step
        u32 r = e.vr;
        u32 x = (r >> 4) & 1, y = (r >> 22) & 1;
        r <<= 1;
        r ^= x ^ y;
        e.vr = (r & 0x7FFFFF) | kOne;
        for (int i = 0; i < 4; ++i)
            if ((dest >> (3 - i)) & 1 && ft) e.vf[ft][i] = e.vr;
        return;
    }
    case 0x41: // VRGET
        for (int i = 0; i < 4; ++i)
            if ((dest >> (3 - i)) & 1 && ft) e.vf[ft][i] = e.vr;
        return;
    case 0x42: e.vr = (S[fsf] & 0x7FFFFF) | kOne; return;                 // VRINIT
    case 0x43: e.vr = ((e.vr ^ S[fsf]) & 0x7FFFFF) | kOne; return;        // VRXOR
    default: e.unimplemented(op, "COP2");
    }
}

} // namespace

void Ee::exec_cop2(u32 op) {
    const u32 rs = (op >> 21) & 31, rt = (op >> 16) & 31, rd = (op >> 11) & 31;
    vf[0] = {0, 0, 0, kOne};
    vi[0] = 0;
    if (rs >= 0x10) {
        exec_co(*this, op);
        vf[0] = {0, 0, 0, kOne};
        vi[0] = 0;
        return;
    }
    switch (rs) {
    case 0x01: // QMFC2
        if (rt) {
            u128 v;
            for (int i = 0; i < 4; ++i) v.set_w(i, vf[rd][i]);
            gpr[rt] = v;
        }
        break;
    case 0x02: { // CFC2
        u32 v = 0;
        if (rd < 16) v = vi[rd];
        else switch (rd) {
            case 16: v = vstatus; break;
            case 17: v = vmac; break;
            case 18: v = vclip; break;
            case 20: v = vr; break;
            case 21: v = vi_reg; break;
            case 22: v = vq; break;
            default: v = 0; break; // TPC, CMSAR0, FBRST, VPU-STAT: not modelled
            }
        set32(int(rt), v);
        break;
    }
    case 0x05: // QMTC2
        if (rd) {
            for (int i = 0; i < 4; ++i) vf[rd][i] = gpr[rt].w(i);
        }
        break;
    case 0x06: { // CTC2
        u32 v = g32(int(rt));
        if (rd < 16) {
            if (rd) vi[rd] = u16(v);
        } else switch (rd) {
            case 16: vstatus = v & 0xFFF; break;
            case 17: vmac = v & 0xFFFF; break;
            case 18: vclip = v & 0xFFFFFF; break;
            case 20: vr = v; break;
            case 21: vi_reg = v; break;
            case 22: vq = v; break;
            case 31: unimplemented(op, "COP2 CTC2 CMSAR1 (starts micro program)");
            default: break; // TPC, CMSAR0, FBRST, VPU-STAT: ignored
            }
        break;
    }
    case 0x08: { // BC2F/BC2T/BC2FL/BC2TL; VU0 never busy => condition "false"
        bool on_true = rt & 1, likely = rt & 2;
        if (rt > 3) unimplemented(op, "COP2 BC2");
        branch(!on_true, s32(s16(op & 0xFFFF)), likely);
        break;
    }
    default: unimplemented(op, "COP2");
    }
    vf[0] = {0, 0, 0, kOne};
    vi[0] = 0;
}

} // namespace gt3r::diff
