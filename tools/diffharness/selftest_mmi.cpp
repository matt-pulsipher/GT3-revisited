// SELF_TESTs for the R5900 MMI group. Expected values are computed by hand
// from the EE Core instruction definitions (no game code involved).

#include "asm.h"
#include "harness.h"

namespace gt3r::diff {
namespace {

using namespace enc;

constexpr u64 kJunk = 0xDEADBEEFCAFEF00Dull;

u128 X(u64 hi, u64 lo) {
    u128 v;
    v.hi = hi;
    v.lo = lo;
    return v;
}

// Runs `op` with rs = $a0, rt = $a1, rd = $v0 (sentinel-filled beforehand).
u128 run3(DiffContext& ctx, u32 op, u128 rs, u128 rt) {
    Ee& ee = ctx.ee;
    ee.gpr[a0] = rs;
    ee.gpr[a1] = rt;
    ee.gpr[v0] = X(kJunk, kJunk);
    run_code(ee, {op});
    return ee.gpr[v0];
}

void eq128(DiffContext& ctx, const char* what, const u128& got, u64 hi, u64 lo) {
    ctx.check(got.lo == lo && got.hi == hi,
              std::string(what) + ": got " + hex128(got) + " want " + hex128(X(hi, lo)));
}
void eq64(DiffContext& ctx, const char* what, u64 got, u64 want) {
    ctx.check(got == want, std::string(what) + ": got " + hex32(u32(got >> 32)) + hex32(u32(got)) + " want " +
                               hex32(u32(want >> 32)) + hex32(u32(want)));
}

u32 mmi0(u32 sub, u32 rd = v0) { return MMI(0x08, a0, a1, rd, sub); }
u32 mmi1(u32 sub) { return MMI(0x28, a0, a1, v0, sub); }
u32 mmi2(u32 sub) { return MMI(0x09, a0, a1, v0, sub); }
u32 mmi3(u32 sub) { return MMI(0x29, a0, a1, v0, sub); }

const u128 kRT16 = X(0x0008000700060005ull, 0x0004000300020001ull); // halves 1..8
const u128 kRS16 = X(0x0018001700160015ull, 0x0014001300120011ull); // halves 0x11..0x18
const u128 kRTW = X(0x0000000400000003ull, 0x0000000200000001ull); // words 1,2,3,4
const u128 kRSW = X(0x000000DD000000CCull, 0x0000000B0000000Aull); // words A,B,CC,DD

} // namespace

SELF_TEST(mmi_parallel_add_sub, "MMI0 PADDW/PSUBW/PCGTW/PMAXW") {
    u128 s = X(0x80000000FFFFFFFFull, 0x7FFFFFFF00000001ull), t = X(0x8000000000000001ull, 0x0000000100000002ull);
    u128 r = run3(ctx, mmi0(0x00), s, t);
    eq128(ctx, "paddw", r, 0, 0x8000000000000003ull);
    r = run3(ctx, mmi0(0x01), s, t);
    eq128(ctx, "psubw", r, 0x00000000FFFFFFFEull, 0x7FFFFFFEFFFFFFFFull);
    r = run3(ctx, mmi0(0x02), s, t);
    eq128(ctx, "pcgtw", r, 0, 0xFFFFFFFF00000000ull);
    r = run3(ctx, mmi0(0x03), s, t);
    eq128(ctx, "pmaxw", r, 0x8000000000000001ull, 0x7FFFFFFF00000002ull);
    // halves / bytes wrap
    r = run3(ctx, mmi0(0x04), X(0, 0x0001FFFF00007FFFull), X(0, 0x0001000100010001ull));
    eq128(ctx, "paddh", r, 0, 0x0002000000018000ull);
    r = run3(ctx, mmi0(0x08), X(0, 0x00000000000000FFull), X(0, 0x0000000000000002ull));
    eq128(ctx, "paddb", r, 0, 0x0000000000000001ull);
}

SELF_TEST(mmi_saturating, "PADDSB/PSUBSB/PADDUB/PSUBUH/PADDSW/PADDSH") {
    u128 r = run3(ctx, mmi0(0x18), X(0x7F, 0x10FF80017Full), X(0x7F, 0x1080FF7F01ull));
    eq128(ctx, "paddsb", r, 0x7F, 0x0000002080807F7Full);
    r = run3(ctx, mmi0(0x19), X(0, 0x057F80), X(0, 0x03FF01));
    eq128(ctx, "psubsb", r, 0, 0x027F80);
    r = run3(ctx, mmi1(0x18), X(0, 0x10F0), X(0, 0x1020));
    eq128(ctx, "paddub", r, 0, 0x20FF);
    r = run3(ctx, mmi1(0x15), X(0, 0x0000000080000005ull), X(0, 0x00010007ull));
    eq128(ctx, "psubuh", r, 0, 0x7FFF0000ull);
    r = run3(ctx, mmi0(0x10), X(0, 0x000000007FFFFFFFull), X(0, 0x0000000000000001ull));
    eq128(ctx, "paddsw", r, 0, 0x7FFFFFFFull);
    r = run3(ctx, mmi0(0x11), X(0, 0x0000000080000000ull), X(0, 0x0000000000000001ull));
    eq128(ctx, "psubsw", r, 0, 0x80000000ull);
    r = run3(ctx, mmi0(0x14), X(0, 0x8000000000007FFFull), X(0, 0xFFFF000000000001ull));
    eq128(ctx, "paddsh", r, 0, 0x8000000000007FFFull);
    r = run3(ctx, mmi0(0x15), X(0, 0x0000000000008000ull), X(0, 0x0000000000000001ull));
    eq128(ctx, "psubsh", r, 0, 0x8000ull);
    r = run3(ctx, mmi1(0x10), X(0, 0x00000000FFFFFFF0ull), X(0, 0x20ull));
    eq128(ctx, "padduw", r, 0, 0xFFFFFFFFull);
    r = run3(ctx, mmi1(0x11), X(0, 5), X(0, 9));
    eq128(ctx, "psubuw", r, 0, 0);
    r = run3(ctx, mmi1(0x14), X(0, 0xFFF0), X(0, 0x20));
    eq128(ctx, "padduh", r, 0, 0xFFFF);
    r = run3(ctx, mmi1(0x19), X(0, 0x0502), X(0, 0x0301));
    eq128(ctx, "psubub", r, 0, 0x0201);
}

SELF_TEST(mmi_min_cmp_abs, "PMINH/PMINW/PCEQB/PCEQW/PCEQH/PABSW/PABSH/PADSBH/PCGTB/PCGTH") {
    u128 r = run3(ctx, mmi1(0x07), X(0x0028001E0014000Aull, 0x80007FFFFFFB0001ull),
                  X(0x003200000019FFFFull, 0x7FFF800000030002ull));
    eq128(ctx, "pminh", r, 0x0028000000140000ull | 0xFFFF, 0x80008000FFFB0001ull);
    r = run3(ctx, mmi1(0x03), X(0, 0xFFFFFFFF00000005ull), X(0, 0x0000000100000003ull));
    eq128(ctx, "pminw", r, 0, 0xFFFFFFFF00000003ull);
    r = run3(ctx, mmi1(0x0A), X(0x1122334455667788ull, 0x0807060504030201ull),
             X(0x1122334455667788ull, 0x0007000500030001ull));
    eq128(ctx, "pceqb", r, ~0ull, 0x00FF00FF00FF00FFull);
    r = run3(ctx, mmi1(0x02), X(0, 0x0000000200000001ull), X(0, 0x0000000300000001ull));
    eq128(ctx, "pceqw", r, ~0ull, 0x00000000FFFFFFFFull);
    r = run3(ctx, mmi1(0x06), X(0, 0x0003000200010000ull), X(0, 0x0003000000010009ull));
    eq128(ctx, "pceqh", r, ~0ull, 0xFFFF0000FFFF0000ull);
    r = run3(ctx, mmi0(0x0A), X(0, 0x0501), X(0, 0xFE02)); // bytes: 1>2 no, 5>-2 yes
    eq128(ctx, "pcgtb", r, 0, 0xFF00);
    r = run3(ctx, mmi0(0x06), X(0, 0x0000000000020001ull), X(0, 0x0000000000010002ull));
    eq128(ctx, "pcgth", r, 0, 0xFFFF0000ull);
    r = run3(ctx, mmi1(0x01), X(kJunk, kJunk), X(5, 0xFFFFFFFB80000000ull));
    eq128(ctx, "pabsw", r, 0x5, 0x000000057FFFFFFFull);
    r = run3(ctx, mmi1(0x05), X(kJunk, kJunk), X(0, 0xFFFB800000050000ull));
    eq128(ctx, "pabsh", r, 0, 0x00057FFF00050000ull);
    r = run3(ctx, mmi1(0x04), X(0x0028001E0014000Aull, 0x0028001E0014000Aull),
             X(0x0004000300020001ull, 0x0004000300020001ull));
    eq128(ctx, "padsbh", r, 0x002C00210016000Bull, 0x0024001B00120009ull);
}

SELF_TEST(mmi_shifts, "PSLLH/PSRLH/PSRAH/PSLLW/PSRLW/PSRAW and variable word shifts") {
    u128 t = X(0x8000FFFF12340010ull, 0x8000FFFF12340010ull);
    u128 r = run3(ctx, MMI(0x34, 0, a1, v0, 4), X(0, 0), t);
    eq128(ctx, "psllh", r, 0x0000FFF023400100ull, 0x0000FFF023400100ull);
    r = run3(ctx, MMI(0x36, 0, a1, v0, 4), X(0, 0), t);
    eq128(ctx, "psrlh", r, 0x08000FFF01230001ull, 0x08000FFF01230001ull);
    r = run3(ctx, MMI(0x37, 0, a1, v0, 4), X(0, 0), t);
    eq128(ctx, "psrah", r, 0xF800FFFF01230001ull, 0xF800FFFF01230001ull);
    u128 w = X(0x8000000100000100ull, 0x80000001FF000001ull);
    r = run3(ctx, MMI(0x3C, 0, a1, v0, 8), X(0, 0), w);
    eq128(ctx, "psllw", r, 0x0000010000010000ull, 0x0000010000000100ull);
    r = run3(ctx, MMI(0x3E, 0, a1, v0, 4), X(0, 0), w);
    eq128(ctx, "psrlw", r, 0x0800000000000010ull, 0x080000000FF00000ull);
    r = run3(ctx, MMI(0x3F, 0, a1, v0, 4), X(0, 0), w);
    eq128(ctx, "psraw", r, 0xF800000000000010ull, 0xF8000000FFF00000ull);
    // variable word shifts: dword results are sign-extended 32-bit values
    r = run3(ctx, mmi2(0x02), X(1, 33), X(0x40000000, 1));
    eq128(ctx, "psllvw", r, 0xFFFFFFFF80000000ull, 2);
    r = run3(ctx, mmi2(0x03), X(0, 4), X(0, 0x80000000ull));
    eq128(ctx, "psrlvw", r, 0, 0x08000000ull);
    r = run3(ctx, mmi3(0x03), X(0, 4), X(0, 0x80000000ull));
    eq128(ctx, "psravw", r, 0, 0xFFFFFFFFF8000000ull);
}

SELF_TEST(mmi_interleave_pack, "PEXTL/PEXTU/PPAC W,H,B, PEXT5/PPAC5, PINTH/PINTEH") {
    u128 r = run3(ctx, mmi0(0x12), kRSW, kRTW);
    eq128(ctx, "pextlw", r, 0x0000000B00000002ull, 0x0000000A00000001ull);
    r = run3(ctx, mmi1(0x12), kRSW, kRTW);
    eq128(ctx, "pextuw", r, 0x000000DD00000004ull, 0x000000CC00000003ull);
    r = run3(ctx, mmi0(0x13), kRSW, kRTW);
    eq128(ctx, "ppacw", r, 0x000000CC0000000Aull, 0x0000000300000001ull);
    r = run3(ctx, mmi0(0x16), kRS16, kRT16);
    eq128(ctx, "pextlh", r, 0x0014000400130003ull, 0x0012000200110001ull);
    r = run3(ctx, mmi1(0x16), kRS16, kRT16);
    eq128(ctx, "pextuh", r, 0x0018000800170007ull, 0x0016000600150005ull);
    r = run3(ctx, mmi0(0x17), kRS16, kRT16);
    eq128(ctx, "ppach", r, 0x0017001500130011ull, 0x0007000500030001ull);
    r = run3(ctx, mmi0(0x1A), X(0x8887868584838281ull, 0x0807060504030201ull),
             X(0x9897969594939291ull, 0x1817161514131211ull));
    eq128(ctx, "pextlb", r, 0x0818071706160515ull, 0x0414031302120111ull);
    r = run3(ctx, mmi1(0x1A), X(0x8887868584838281ull, 0x0807060504030201ull),
             X(0x9897969594939291ull, 0x1817161514131211ull));
    eq128(ctx, "pextub", r, 0x8898879786968595ull, 0x8494839382928191ull);
    r = run3(ctx, mmi0(0x1B), X(0x2827262524232221ull, 0x1817161514131211ull),
             X(0xFFEEDDCCBBAA9988ull, 0x8877665544332211ull));
    eq128(ctx, "ppacb", r, 0x2725232117151311ull, 0xEECCAA8877553311ull);
    r = run3(ctx, mmi0(0x1E), X(kJunk, kJunk), X(0xFE01, 0xFE01));
    eq128(ctx, "pext5", r, 0x80F88008, 0x80F88008);
    r = run3(ctx, mmi0(0x1F), X(kJunk, kJunk), X(0, 0x80F88008));
    eq128(ctx, "ppac5", r, 0, 0xFE01);
    r = run3(ctx, mmi2(0x0A), kRS16, kRT16);
    eq128(ctx, "pinth", r, 0x0018000400170003ull, 0x0016000200150001ull);
    r = run3(ctx, mmi3(0x0A), kRS16, kRT16);
    eq128(ctx, "pinteh", r, 0x0017000700150005ull, 0x0013000300110001ull);
}

SELF_TEST(mmi_copy_perm_logic, "PCPYLD/PCPYUD/PCPYH/PEXEH/PREVH/PEXCH/PEXEW/PEXCW/PROT3W/PAND/POR/PXOR/PNOR") {
    u128 r = run3(ctx, mmi2(0x0E), X(0xA1, 0xA0), X(0xB1, 0xB0));
    eq128(ctx, "pcpyld", r, 0xA0, 0xB0);
    r = run3(ctx, mmi3(0x0E), X(0xA1, 0xA0), X(0xB1, 0xB0));
    eq128(ctx, "pcpyud", r, 0xB1, 0xA1);
    r = run3(ctx, mmi3(0x1B), X(kJunk, kJunk), kRT16);
    eq128(ctx, "pcpyh", r, 0x0005000500050005ull, 0x0001000100010001ull);
    r = run3(ctx, mmi2(0x1A), X(kJunk, kJunk), kRT16);
    eq128(ctx, "pexeh", r, 0x0008000500060007ull, 0x0004000100020003ull);
    r = run3(ctx, mmi2(0x1B), X(kJunk, kJunk), kRT16);
    eq128(ctx, "prevh", r, 0x0005000600070008ull, 0x0001000200030004ull);
    r = run3(ctx, mmi3(0x1A), X(kJunk, kJunk), kRT16);
    eq128(ctx, "pexch", r, 0x0008000600070005ull, 0x0004000200030001ull);
    r = run3(ctx, mmi2(0x1E), X(kJunk, kJunk), kRTW);
    eq128(ctx, "pexew", r, 0x0000000400000001ull, 0x0000000200000003ull);
    r = run3(ctx, mmi3(0x1E), X(kJunk, kJunk), kRTW);
    eq128(ctx, "pexcw", r, 0x0000000400000002ull, 0x0000000300000001ull);
    r = run3(ctx, mmi2(0x1F), X(kJunk, kJunk), kRTW);
    eq128(ctx, "prot3w", r, 0x0000000400000001ull, 0x0000000300000002ull);
    u128 s = X(0x0123456789ABCDEFull, 0xFEDCBA9876543210ull), t = X(0xFFFF0000FFFF0000ull, 0x00FF00FF00FF00FFull);
    r = run3(ctx, mmi2(0x12), s, t);
    eq128(ctx, "pand", r, s.hi & t.hi, s.lo & t.lo);
    r = run3(ctx, mmi2(0x13), s, t);
    eq128(ctx, "pxor", r, s.hi ^ t.hi, s.lo ^ t.lo);
    r = run3(ctx, mmi3(0x12), s, t);
    eq128(ctx, "por", r, s.hi | t.hi, s.lo | t.lo);
    r = run3(ctx, mmi3(0x13), s, t);
    eq128(ctx, "pnor", r, ~(s.hi | t.hi), ~(s.lo | t.lo));
}

SELF_TEST(mmi_qfsrv, "QFSRV with MTSAB") {
    u128 A = X(0xDDDDDDDDCCCCCCCCull, 0xBBBBBBBBAAAAAAAAull), B = X(0x4444444433333333ull, 0x2222222211111111ull);
    Ee& ee = ctx.ee;
    ee.gpr[a0] = A;
    ee.gpr[a1] = B;
    run_code(ee, {I(1, zero, 0x18, 4), mmi1(0x1B)});
    eq128(ctx, "qfsrv sa=4", ee.gpr[v0], 0xAAAAAAAA44444444ull, 0x3333333322222222ull);
    run_code(ee, {I(1, zero, 0x18, 12), mmi1(0x1B)});
    eq128(ctx, "qfsrv sa=12", ee.gpr[v0], 0xCCCCCCCCBBBBBBBBull, 0xAAAAAAAA44444444ull);
    run_code(ee, {I(1, zero, 0x18, 0), mmi1(0x1B)});
    eq128(ctx, "qfsrv sa=0", ee.gpr[v0], B.hi, B.lo);
}

SELF_TEST(mmi_pipe1_mult_div, "MULT1/MULTU1/DIV1/DIVU1/MFLO1/MFHI1/MTLO1/MTHI1") {
    Ee& ee = ctx.ee;
    ee.hi = X(0x111, 0x222);
    ee.lo = X(0x333, 0x444);
    ee.gpr[a0] = X(0, 0xFFFFFFFEull);
    ee.gpr[a1] = X(0, 3);
    run_code(ee, {MMI(0x18, a0, a1, v0)});
    eq64(ctx, "mult1 lo1", ee.lo.hi, 0xFFFFFFFFFFFFFFFAull);
    eq64(ctx, "mult1 hi1", ee.hi.hi, ~0ull);
    eq64(ctx, "mult1 rd", ee.gpr[v0].lo, 0xFFFFFFFFFFFFFFFAull);
    eq64(ctx, "mult1 lo0 untouched", ee.lo.lo, 0x444);
    eq64(ctx, "mult1 hi0 untouched", ee.hi.lo, 0x222);
    ee.gpr[a0] = X(0, 0xFFFFFFFFull);
    ee.gpr[a1] = X(0, 2);
    run_code(ee, {MMI(0x19, a0, a1, v1)});
    eq64(ctx, "multu1 lo1", ee.lo.hi, 0xFFFFFFFFFFFFFFFEull);
    eq64(ctx, "multu1 hi1", ee.hi.hi, 1);
    eq64(ctx, "multu1 rd", ee.gpr[v1].lo, 0xFFFFFFFFFFFFFFFEull);
    ee.gpr[a0] = X(0, 7);
    ee.gpr[a1] = X(0, 0xFFFFFFFEull);
    run_code(ee, {MMI(0x1A, a0, a1, 0)});
    eq64(ctx, "div1 q", ee.lo.hi, 0xFFFFFFFFFFFFFFFDull);
    eq64(ctx, "div1 r", ee.hi.hi, 1);
    ee.gpr[a0] = X(0, 0xFFFFFFFBull); // -5
    ee.gpr[a1] = X(0, 0);
    run_code(ee, {MMI(0x1A, a0, a1, 0)});
    eq64(ctx, "div1 /0 q", ee.lo.hi, 1);
    eq64(ctx, "div1 /0 r", ee.hi.hi, 0xFFFFFFFFFFFFFFFBull);
    ee.gpr[a0] = X(0, 0x80000000ull);
    ee.gpr[a1] = X(0, 0xFFFFFFFFull);
    run_code(ee, {MMI(0x1A, a0, a1, 0)});
    eq64(ctx, "div1 min/-1 q", ee.lo.hi, 0xFFFFFFFF80000000ull);
    eq64(ctx, "div1 min/-1 r", ee.hi.hi, 0);
    ee.gpr[a0] = X(0, 10);
    ee.gpr[a1] = X(0, 3);
    run_code(ee, {MMI(0x1B, a0, a1, 0)});
    eq64(ctx, "divu1 q", ee.lo.hi, 3);
    eq64(ctx, "divu1 r", ee.hi.hi, 1);
    eq64(ctx, "div1 lo0 untouched", ee.lo.lo, 0x444);
    ee.lo.hi = 0x1234567890ull;
    ee.hi.hi = 0xAABBCCDDEEull;
    ee.gpr[v0] = X(kJunk, kJunk);
    run_code(ee, {MMI(0x12, 0, 0, v0), MMI(0x10, 0, 0, v1)});
    eq64(ctx, "mflo1", ee.gpr[v0].lo, 0x1234567890ull);
    eq64(ctx, "mfhi1", ee.gpr[v1].lo, 0xAABBCCDDEEull);
    eq64(ctx, "mflo1 keeps rd.hi", ee.gpr[v0].hi, kJunk);
    ee.gpr[a0] = X(0, 0x55);
    ee.gpr[a1] = X(0, 0x66);
    run_code(ee, {MMI(0x13, a0, 0, 0), MMI(0x11, a1, 0, 0)});
    eq64(ctx, "mtlo1", ee.lo.hi, 0x55);
    eq64(ctx, "mthi1", ee.hi.hi, 0x66);
}

SELF_TEST(mmi_madd, "MADD/MADDU/MADD1/MADDU1 and PLZCW") {
    Ee& ee = ctx.ee;
    ee.hi = X(0, 0);
    ee.lo = X(0, 100);
    ee.gpr[a0] = X(0, 6);
    ee.gpr[a1] = X(0, 7);
    run_code(ee, {MMI(0x00, a0, a1, v0)});
    eq64(ctx, "madd lo", ee.lo.lo, 142);
    eq64(ctx, "madd hi", ee.hi.lo, 0);
    eq64(ctx, "madd rd", ee.gpr[v0].lo, 142);
    ee.lo = X(0, 5);
    ee.hi = X(0, 0);
    ee.gpr[a0] = X(0, 0xFFFFFFFDull); // -3
    ee.gpr[a1] = X(0, 2);
    run_code(ee, {MMI(0x00, a0, a1, v0)});
    eq64(ctx, "madd neg lo", ee.lo.lo, ~0ull);
    eq64(ctx, "madd neg hi", ee.hi.lo, ~0ull);
    eq64(ctx, "madd neg rd", ee.gpr[v0].lo, ~0ull);
    ee.lo = X(0, 0xFFFFFFFFFFFFFFFFull); // low word 0xFFFFFFFF
    ee.hi = X(0, 0);
    ee.gpr[a0] = X(0, 0xFFFFFFFFull);
    ee.gpr[a1] = X(0, 1);
    run_code(ee, {MMI(0x01, a0, a1, v0)}); // maddu: 0xFFFFFFFF + 0xFFFFFFFF = 0x1FFFFFFFE
    eq64(ctx, "maddu lo", ee.lo.lo, 0xFFFFFFFFFFFFFFFEull);
    eq64(ctx, "maddu hi", ee.hi.lo, 1);
    ee.lo = X(1000, 1);
    ee.hi = X(0, 0);
    ee.gpr[a0] = X(0, 10);
    ee.gpr[a1] = X(0, 20);
    run_code(ee, {MMI(0x20, a0, a1, v1)});
    eq64(ctx, "madd1 lo1", ee.lo.hi, 1200);
    eq64(ctx, "madd1 lo0 untouched", ee.lo.lo, 1);
    eq64(ctx, "madd1 rd", ee.gpr[v1].lo, 1200);
    run_code(ee, {MMI(0x21, a0, a1, 0)});
    eq64(ctx, "maddu1 lo1", ee.lo.hi, 1400);

    u128 r = run3(ctx, MMI(0x04, a0, 0, v0), X(kJunk, 0xFFFFFFF000FF0000ull), X(0, 0));
    eq64(ctx, "plzcw", r.lo, (27ull << 32) | 7);
    eq64(ctx, "plzcw hi untouched", r.hi, kJunk);
    r = run3(ctx, MMI(0x04, a0, 0, v0), X(0, 0xFFFFFFFF00000000ull), X(0, 0));
    eq64(ctx, "plzcw 0/-1", r.lo, (31ull << 32) | 31);
}

SELF_TEST(mmi_pmfhl, "PMFHL.LW/UW/SLW/LH/SH and PMTHL.LW") {
    Ee& ee = ctx.ee;
    ee.lo = X(0xFFFF0004FFFF0003ull, 0xFFFF0002FFFF0001ull);
    ee.hi = X(0xEEEE0014EEEE0013ull, 0xEEEE0012EEEE0011ull);
    ee.gpr[v0] = X(kJunk, kJunk);
    run_code(ee, {MMI(0x30, 0, 0, v0, 0)});
    eq128(ctx, "pmfhl.lw", ee.gpr[v0], 0xEEEE0013FFFF0003ull, 0xEEEE0011FFFF0001ull);
    run_code(ee, {MMI(0x30, 0, 0, v0, 1)});
    eq128(ctx, "pmfhl.uw", ee.gpr[v0], 0xEEEE0014FFFF0004ull, 0xEEEE0012FFFF0002ull);
    ee.lo = X(0x0000000000000000ull, 0x0000000000000000ull);
    ee.hi = X(0xFFFFFFFEull, 1);
    run_code(ee, {MMI(0x30, 0, 0, v0, 2)});
    eq128(ctx, "pmfhl.slw", ee.gpr[v0], 0xFFFFFFFF80000000ull, 0x7FFFFFFFull);
    ee.lo = X(0x0000000000000005ull, 0x0000000000000009ull); // in-range -> sign-extended LO word
    ee.hi = X(0, 0);
    run_code(ee, {MMI(0x30, 0, 0, v0, 2)});
    eq128(ctx, "pmfhl.slw in range", ee.gpr[v0], 5, 9);
    ee.lo = X(0xFFFF0004FFFF0003ull, 0xFFFF0002FFFF0001ull);
    ee.hi = X(0xEEEE0014EEEE0013ull, 0xEEEE0012EEEE0011ull);
    run_code(ee, {MMI(0x30, 0, 0, v0, 3)});
    eq128(ctx, "pmfhl.lh", ee.gpr[v0], 0x0014001300040003ull, 0x0012001100020001ull);
    ee.lo = X(0x00001234FFFF0000ull, 0xFFFFFFF600012345ull);
    ee.hi = X(0xFFFF8000FFFFFFFFull, 0x0000000000000005ull);
    run_code(ee, {MMI(0x30, 0, 0, v0, 4)});
    eq128(ctx, "pmfhl.sh", ee.gpr[v0], 0x800000001234FFF6ull,0xFFFF000580007FFFull);
    ee.gpr[a0] = kRTW;
    run_code(ee, {MMI(0x31, a0, 0, 0, 0)});
    eq64(ctx, "pmthl lo0", ee.lo.lo & 0xFFFFFFFF, 1);
    eq64(ctx, "pmthl hi0", ee.hi.lo & 0xFFFFFFFF, 2);
    eq64(ctx, "pmthl lo1", ee.lo.hi & 0xFFFFFFFF, 3);
    eq64(ctx, "pmthl hi1", ee.hi.hi & 0xFFFFFFFF, 4);
}

SELF_TEST(mmi_parallel_mul, "PMULTW/PMADDW/PMSUBW/PMULTUW/PMADDUW/PDIVW/PDIVUW/PDIVBW/PMULTH/PMADDH/PMFHI/PMTHI") {
    Ee& ee = ctx.ee;
    u128 r = run3(ctx, mmi2(0x0C), X(0x10000, 0xFFFFFFFEull), X(0x10000, 3));
    eq128(ctx, "pmultw rd", r, 0x100000000ull, 0xFFFFFFFFFFFFFFFAull);
    eq64(ctx, "pmultw lo0", ee.lo.lo, 0xFFFFFFFFFFFFFFFAull);
    eq64(ctx, "pmultw hi0", ee.hi.lo, ~0ull);
    eq64(ctx, "pmultw lo1", ee.lo.hi, 0);
    eq64(ctx, "pmultw hi1", ee.hi.hi, 1);
    ee.lo = X(100, 10);
    ee.hi = X(0, 0);
    r = run3(ctx, mmi2(0x00), X(0, 2), X(0, 3));
    eq128(ctx, "pmaddw rd", r, 100, 16);
    eq64(ctx, "pmaddw lo0", ee.lo.lo, 16);
    eq64(ctx, "pmaddw lo1", ee.lo.hi, 100);
    ee.lo = X(0, 10);
    ee.hi = X(0, 0);
    r = run3(ctx, mmi2(0x04), X(0, 2), X(0, 3));
    eq128(ctx, "pmsubw rd", r, 0, 4);
    r = run3(ctx, mmi3(0x0C), X(0, 0xFFFFFFFFull), X(0, 0xFFFFFFFFull));
    eq128(ctx, "pmultuw rd", r, 0, 0xFFFFFFFE00000001ull);
    eq64(ctx, "pmultuw lo0", ee.lo.lo, 1);
    eq64(ctx, "pmultuw hi0", ee.hi.lo, 0xFFFFFFFFFFFFFFFEull);
    ee.lo = X(0, 1);
    ee.hi = X(0, 0);
    r = run3(ctx, mmi3(0x00), X(0, 4), X(0, 5));
    eq128(ctx, "pmadduw rd", r, 0, 21);
    run3(ctx, mmi2(0x0D), X(100, 7), X(7, 0xFFFFFFFEull));
    eq64(ctx, "pdivw q0", ee.lo.lo, 0xFFFFFFFFFFFFFFFDull);
    eq64(ctx, "pdivw r0", ee.hi.lo, 1);
    eq64(ctx, "pdivw q1", ee.lo.hi, 14);
    eq64(ctx, "pdivw r1", ee.hi.hi, 2);
    run3(ctx, mmi3(0x0D), X(100, 0xFFFFFFFFull), X(0, 16));
    eq64(ctx, "pdivuw q0", ee.lo.lo, 0x0FFFFFFFull);
    eq64(ctx, "pdivuw r0", ee.hi.lo, 15);
    eq64(ctx, "pdivuw /0 q1", ee.lo.hi, ~0ull);
    eq64(ctx, "pdivuw /0 r1", ee.hi.hi, 100);
    run3(ctx, mmi2(0x1D), X(0x0000000900000064ull, 0x00000000FFFFFFF9ull), X(0, 0x0007));
    eq128(ctx, "pdivbw lo", ee.lo, 0x000000010000000Eull, 0x00000000FFFFFFFFull);
    eq128(ctx, "pdivbw hi", ee.hi, 0x0000000200000002ull, 0);
    r = run3(ctx, mmi2(0x1C), kRT16, X(0x0001000100010001ull, 0x00030002FFFF000Aull));
    eq128(ctx, "pmulth rd", r, 0x0000000600000005ull, 0xFFFFFFFE0000000Aull);
    eq128(ctx, "pmulth lo", ee.lo, 0x0000000700000005ull, 0x000000060000000Aull);
    eq128(ctx, "pmulth hi", ee.hi, 0x0000000800000006ull, 0x0000000CFFFFFFFEull);
    r = run3(ctx, mmi2(0x10), kRT16, X(0x0001000100010001ull, 0x00030002FFFF000Aull));
    eq128(ctx, "pmaddh rd", r, 0x0000000C0000000Aull, 0xFFFFFFFC00000014ull);
    ee.gpr[a0] = X(0x1111, 0x2222);
    ee.gpr[v0] = X(kJunk, kJunk);
    run_code(ee, {MMI(0x29, a0, 0, 0, 0x08), MMI(0x29, a0, 0, 0, 0x09), MMI(0x09, 0, 0, v0, 0x08)});
    eq128(ctx, "pmthi/pmfhi", ee.gpr[v0], 0x1111, 0x2222);
    eq128(ctx, "pmtlo", ee.lo, 0x1111, 0x2222);
    run_code(ee, {MMI(0x09, 0, 0, v1, 0x09)});
    eq128(ctx, "pmflo", ee.gpr[v1], 0x1111, 0x2222);
}

SELF_TEST(mmi_zero_and_unknown, "writes to $zero are discarded; undefined MMI encodings trap") {
    Ee& ee = ctx.ee;
    ee.gpr[a0] = X(1, 1);
    ee.gpr[a1] = X(2, 2);
    run_code(ee, {mmi0(0x00, zero)});
    ctx.check(ee.gpr[0].lo == 0 && ee.gpr[0].hi == 0, "$zero stays zero");
    bool trapped = false;
    try {
        run_code(ee, {MMI(0x02, a0, a1, v0)});
    } catch (const EeTrap& e) {
        trapped = e.kind == TrapKind::Unimplemented;
    }
    ctx.check(trapped, "unknown MMI funct traps as Unimplemented");
}

} // namespace gt3r::diff
