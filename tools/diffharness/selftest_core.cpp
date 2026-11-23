// Interpreter self tests for the integer core and COP1 (no disc needed).
// Expected values come from the R5900 ISA definition and PS2 FPU rules.

#include "asm.h"
#include "harness.h"
#include "ps2float.h"

using namespace gt3r;
using namespace gt3r::diff;
using namespace gt3r::diff::enc;

SELF_TEST(core_alu64, "64-bit ALU, shifts, sign extension of 32-bit results") {
    Ee& ee = ctx.ee;
    ee.gpr[a0].lo = 0x7FFFFFFF;
    ee.gpr[a1].lo = 1;
    run_code(ee, {R(0x21, a0, a1, v0),            // addu: 0x80000000 sign-extends
                  R(0x2D, a0, a1, v1),            // daddu: no sign extension
                  R(0x3C, 0, a1, t0, 4),          // dsll32 t0 = 1 << 36
                  R(0x3B, 0, v0, t1, 4),          // dsra t1 = v0 >> 4 (arith)
                  R(0x3E, 0, v0, t2, 0),          // dsrl32 t2 = v0 >> 32
                  I(0x0B, a0, t3, 0xFFFF)});      // sltiu t3 = a0 < 0xFFFF..FF
    ctx.check(ee.gpr[v0].lo == 0xFFFFFFFF80000000ull, "addu sign-extends");
    ctx.check(ee.gpr[v1].lo == 0x80000000ull, "daddu");
    ctx.check(ee.gpr[t0].lo == (1ull << 36), "dsll32");
    ctx.check(ee.gpr[t1].lo == 0xFFFFFFFFF8000000ull, "dsra");
    ctx.check(ee.gpr[t2].lo == 0xFFFFFFFFull, "dsrl32");
    ctx.check(ee.gpr[t3].lo == 1, "sltiu compares against sign-extended immediate");
}

SELF_TEST(core_muldiv, "R5900 MULT rd form, DIV edge cases") {
    Ee& ee = ctx.ee;
    ee.gpr[a0].lo = u64(s64(-7));
    ee.gpr[a1].lo = 3;
    run_code(ee, {R(0x18, a0, a1, v0), R(0x10, 0, 0, v1)}); // mult v0 = lo, mfhi
    ctx.check(ee.gpr[v0].lo == u64(s64(-21)), "mult rd = LO");
    ctx.check(ee.gpr[v1].lo == ~0ull, "mult HI sign");
    run_code(ee, {R(0x1A, a0, a1, 0), R(0x12, 0, 0, v0), R(0x10, 0, 0, v1)});
    ctx.check(s64(ee.gpr[v0].lo) == -2 && s64(ee.gpr[v1].lo) == -1, "div truncates toward zero");
    ee.gpr[a1].lo = 0;
    run_code(ee, {R(0x1A, a0, a1, 0), R(0x12, 0, 0, v0), R(0x10, 0, 0, v1)});
    ctx.check(s64(ee.gpr[v0].lo) == 1 && s64(ee.gpr[v1].lo) == -7, "div by zero (negative dividend)");
}

SELF_TEST(core_unaligned, "LWL/LWR/LDL/LDR/SWL/SWR/SDL/SDR") {
    Ee& ee = ctx.ee;
    u32 buf = ee.alloc(32);
    for (u32 i = 0; i < 32; ++i) ee.w8(buf + i, u8(0x10 + i));
    ee.gpr[a0].lo = buf + 3;
    run_code(ee, {I(0x22, a0, v0, 3), I(0x26, a0, v0, 0),  // lwl/lwr: unaligned word at buf+3
                  I(0x1A, a0, v1, 7), I(0x1B, a0, v1, 0)}); // ldl/ldr: unaligned dword
    ctx.check(ee.gpr[v0].lo == 0x16151413ull, "lwl/lwr");
    ctx.check(ee.gpr[v1].lo == 0x1A19181716151413ull, "ldl/ldr");
    ee.gpr[a0].lo = buf + 17;
    ee.gpr[a1].lo = 0xA1A2A3A4A5A6A7A8ull;
    run_code(ee, {I(0x2A, a0, a1, 3), I(0x2E, a0, a1, 0), I(0x2C, a0, a1, 14), I(0x2D, a0, a1, 8)});
    ctx.check(ee.r32(buf + 16) == 0xA6A7A820u && ee.r8(buf + 20) == 0xA5 && ee.r8(buf + 21) == 0x25, "swl/swr");
    ctx.check(ee.r64(buf + 24) == 0xA2A3A4A5A6A7A8A8ull, "sdl/sdr");
}

SELF_TEST(core_branches, "delay slots, likely branches, jal/jr") {
    Ee& ee = ctx.ee;
    ee.gpr[a0].lo = 0;
    // beq taken executes the delay slot; beql not taken skips it.
    run_code(ee, {addiu(v0, zero, 0),
                  I(0x04, a0, zero, 2), addiu(v0, v0, 1), addiu(v0, v0, 100), // taken: skips +100
                  I(0x15, a0, zero, 2), addiu(v0, v0, 10),                    // bnel not taken: skips +10
                  addiu(v0, v0, 1000)});
    ctx.check(ee.gpr[v0].lo == 1001, "beq/bnel delay slot semantics (got " + std::to_string(ee.gpr[v0].lo) + ")");
}

SELF_TEST(fpu_ps2_semantics, "COP1: clamp, flush, chop rounding, cvt.w.s saturation") {
    Ee& ee = ctx.ee;
    auto fop = [&](u32 funct, u32 a, u32 b) {
        ee.fpr[1] = a;
        ee.fpr[2] = b;
        run_code(ee, {FPU_S(funct, 0, 1, 2)});
        return ee.fpr[0];
    };
    ctx.check(fop(2, 0x7F000000, 0x7F000000) == 0x7FFFFFFF, "mul overflow clamps to +max");
    ctx.check(fop(2, 0xFF000000, 0x7F000000) == 0xFFFFFFFF, "mul overflow clamps to -max");
    ctx.check(fop(2, 0x00800000, 0x00800000) == 0, "mul underflow flushes to 0");
    ctx.check(fop(0, 0x00400000, 0x3F800000) == 0x3F800000, "denormal input reads as 0");
    ctx.check(fop(0, 0x7F800000, 0x3F800000) == 0x7F800000, "exponent 255 is a normal number");
    ctx.check(fop(3, 0x3F800000, 0x00000000) == 0x7FFFFFFF, "div by zero -> +max");
    ctx.check(fop(3, 0xBF800000, 0x00000000) == 0xFFFFFFFF, "div by zero sign");
    // 1/3 rounds to nearest in the divider (0x3EAAAAAB), chop would give ...AAA.
    ctx.check(fop(3, 0x3F800000, 0x40400000) == 0x3EAAAAAB, "div rounding");
    // (1 + 2^-23) * (1 + 2^-23) = 1 + 2^-22 + 2^-46 -> chop drops 2^-46
    ctx.check(fop(2, 0x3F800001, 0x3F800001) == 0x3F800002, "mul chop");
    // 1 - 2^-30: operand ignored? exponent difference 30 >= 25 -> result 1.0 (IEEE RTZ would give 0x3F7FFFFF)
    ctx.check(fop(1, 0x3F800000, 0x30800000) == 0x3F800000, "sub of tiny operand keeps the larger");
    ee.fpr[1] = 0x4F000000; // 2^31
    run_code(ee, {FPU_S(0x24, 0, 1, 0)});
    ctx.check(ee.fpr[0] == 0x7FFFFFFF, "cvt.w.s saturates");
    ee.fpr[1] = 0xC0600000; // -3.5
    run_code(ee, {FPU_S(0x24, 0, 1, 0)});
    ctx.check(ee.fpr[0] == u32(-3), "cvt.w.s truncates");
    ctx.check(ps2f::compare(0x00000001, 0x80000000) == 0, "denormal == -0 in compares");
}
