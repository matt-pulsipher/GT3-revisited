#pragma once

// Tiny R5900 instruction encoder for SELF_TESTs (hand-written sequences that
// check the interpreter against the documented ISA; no game code involved).

#include "ee.h"

#include <vector>

namespace gt3r::diff::enc {

enum Reg : u32 {
    zero, at, v0, v1, a0, a1, a2, a3, t0, t1, t2, t3, t4, t5, t6, t7,
    s0, s1, s2, s3, s4, s5, s6, s7, t8, t9, k0, k1, gp, sp, fp, ra
};

constexpr u32 R(u32 funct, u32 rs, u32 rt, u32 rd, u32 sa = 0) {
    return (rs << 21) | (rt << 16) | (rd << 11) | ((sa & 31) << 6) | (funct & 63);
}
constexpr u32 I(u32 op, u32 rs, u32 rt, u32 imm) { return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF); }
// MMI: primary 0x1C. For the MMI0..3 groups `funct` is 0x08/0x09/0x28/0x29 and `sub` goes in bits 6..10.
constexpr u32 MMI(u32 funct, u32 rs, u32 rt, u32 rd, u32 sub = 0) {
    return (0x1Cu << 26) | (rs << 21) | (rt << 16) | (rd << 11) | ((sub & 31) << 6) | (funct & 63);
}
// COP1 S-format arithmetic: fd = fs op ft.
constexpr u32 FPU_S(u32 funct, u32 fd, u32 fs, u32 ft) {
    return (0x11u << 26) | (0x10u << 21) | (ft << 16) | (fs << 11) | (fd << 6) | funct;
}

constexpr u32 nop = 0;
constexpr u32 jr_ra = 0x03E00008;
constexpr u32 addiu(u32 rt, u32 rs, s32 imm) { return I(0x09, rs, rt, u32(imm)); }
constexpr u32 daddiu(u32 rt, u32 rs, s32 imm) { return I(0x19, rs, rt, u32(imm)); }
constexpr u32 lui(u32 rt, u32 imm) { return I(0x0F, 0, rt, imm); }
constexpr u32 ori(u32 rt, u32 rs, u32 imm) { return I(0x0D, rs, rt, imm); }
constexpr u32 daddu(u32 rd, u32 rs, u32 rt) { return R(0x2D, rs, rt, rd); }
constexpr u32 lw(u32 rt, s32 off, u32 base) { return I(0x23, base, rt, u32(off)); }
constexpr u32 sw(u32 rt, s32 off, u32 base) { return I(0x2B, base, rt, u32(off)); }
constexpr u32 ld(u32 rt, s32 off, u32 base) { return I(0x37, base, rt, u32(off)); }
constexpr u32 sd(u32 rt, s32 off, u32 base) { return I(0x3F, base, rt, u32(off)); }
constexpr u32 lq(u32 rt, s32 off, u32 base) { return I(0x1E, base, rt, u32(off)); }
constexpr u32 sq(u32 rt, s32 off, u32 base) { return I(0x1F, base, rt, u32(off)); }
constexpr u32 lwc1(u32 ft, s32 off, u32 base) { return I(0x31, base, ft, u32(off)); }
constexpr u32 swc1(u32 ft, s32 off, u32 base) { return I(0x39, base, ft, u32(off)); }
constexpr u32 lqc2(u32 vf, s32 off, u32 base) { return I(0x36, base, vf, u32(off)); }
constexpr u32 sqc2(u32 vf, s32 off, u32 base) { return I(0x3E, base, vf, u32(off)); }
constexpr u32 mtc1(u32 rt, u32 fs) { return (0x11u << 26) | (0x04u << 21) | (rt << 16) | (fs << 11); }
constexpr u32 mfc1(u32 rt, u32 fs) { return (0x11u << 26) | (0x00u << 21) | (rt << 16) | (fs << 11); }
// QMFC2/QMTC2 (interlock bit 0 clear).
constexpr u32 qmfc2(u32 rt, u32 vf) { return (0x12u << 26) | (0x01u << 21) | (rt << 16) | (vf << 11); }
constexpr u32 qmtc2(u32 rt, u32 vf) { return (0x12u << 26) | (0x05u << 21) | (rt << 16) | (vf << 11); }

} // namespace gt3r::diff::enc

namespace gt3r::diff {

// Self-test program: an empty 64 KB image at 0x100000. Code goes at kSelfCode.
constexpr u32 kSelfCode = 0x00100000;
// Prepares `ee` for self tests (call once per test; cheap).
void self_test_setup(Ee& ee);
// Writes `code` + jr $ra + nop at kSelfCode and calls it.
CallResult run_code(Ee& ee, const std::vector<u32>& code, const std::vector<Arg>& args = {});

} // namespace gt3r::diff
