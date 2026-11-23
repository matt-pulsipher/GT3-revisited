// Self tests for COP2 / VU0 macro mode (ee_vu0.cpp). Expectations are hand
// computed from the VU manual; no game code is involved.

#include "asm.h"
#include "harness.h"
#include "ps2float.h"

#include <cstring>

namespace gt3r::diff {
namespace {

using namespace enc;

// ---- COP2 encoders (dest bit masks: x=8 y=4 z=2 w=1) ------------------------
constexpr u32 X = 8, Y = 4, Z = 2, W = 1, XYZW = 15;

constexpr u32 co(u32 dest, u32 ft, u32 fs, u32 fd, u32 funct) {
    return (0x12u << 26) | (1u << 25) | (dest << 21) | (ft << 16) | (fs << 11) | (fd << 6) | funct;
}
// Special2 ops: funct 0x3C-0x3F, sub-op = bits 6-10 (high five) and bits 0-1.
constexpr u32 sp2(u32 dest, u32 ft, u32 fs, u32 sub) { return co(dest, ft, fs, sub >> 2, 0x3C | (sub & 3)); }
// DIV/SQRT/RSQRT style: fsf/ftf live in the dest field.
constexpr u32 sp2f(u32 ftf, u32 fsf, u32 ft, u32 fs, u32 sub) { return sp2((ftf << 2) | fsf, ft, fs, sub); }

constexpr u32 cop2_move(u32 sel, u32 rt, u32 rd) { return (0x12u << 26) | (sel << 21) | (rt << 16) | (rd << 11); }
constexpr u32 cfc2(u32 rt, u32 rd) { return cop2_move(2, rt, rd); }
constexpr u32 ctc2(u32 rt, u32 rd) { return cop2_move(6, rt, rd); }
constexpr u32 bc2(u32 cond, s32 off) { return (0x12u << 26) | (8u << 21) | (cond << 16) | (u32(off) & 0xFFFF); }

// Frequently used ops.
constexpr u32 vmulax(u32 ft, u32 fs) { return sp2(XYZW, ft, fs, 0x18); }
constexpr u32 vmadday(u32 ft, u32 fs) { return sp2(XYZW, ft, fs, 0x09); }
constexpr u32 vmaddaz(u32 ft, u32 fs) { return sp2(XYZW, ft, fs, 0x0A); }
constexpr u32 vmaddw(u32 dest, u32 fd, u32 fs, u32 ft) { return co(dest, ft, fs, fd, 0x0B); }
constexpr u32 vadd(u32 dest, u32 fd, u32 fs, u32 ft) { return co(dest, ft, fs, fd, 0x28); }
constexpr u32 vsub(u32 dest, u32 fd, u32 fs, u32 ft) { return co(dest, ft, fs, fd, 0x2C); }
constexpr u32 vmul(u32 dest, u32 fd, u32 fs, u32 ft) { return co(dest, ft, fs, fd, 0x2A); }
constexpr u32 vmini(u32 dest, u32 fd, u32 fs, u32 ft) { return co(dest, ft, fs, fd, 0x2F); }
constexpr u32 vmax(u32 dest, u32 fd, u32 fs, u32 ft) { return co(dest, ft, fs, fd, 0x2B); }
constexpr u32 vmulq(u32 dest, u32 fd, u32 fs) { return co(dest, 0, fs, fd, 0x1C); }
constexpr u32 vaddi(u32 dest, u32 fd, u32 fs) { return co(dest, 0, fs, fd, 0x22); }


u32 F(float v) { return ps2f::from_host(v); }

void setv(Ee& ee, int r, float x, float y, float z, float w) { ee.vf[size_t(r)] = {F(x), F(y), F(z), F(w)}; }

void expect_v(DiffContext& ctx, const char* what, int r, float x, float y, float z, float w) {
    const std::array<u32, 4> want{F(x), F(y), F(z), F(w)};
    const auto& got = ctx.ee.vf[size_t(r)];
    bool ok = got == want;
    std::string msg = std::string(what) + " vf" + std::to_string(r);
    if (!ok) {
        msg += ": got";
        for (u32 g : got) msg += " " + hex32(g);
        msg += " want";
        for (u32 g : want) msg += " " + hex32(g);
    }
    ctx.check(ok, msg);
}

void expect_bits(DiffContext& ctx, const char* what, int r, u32 x, u32 y, u32 z, u32 w) {
    const std::array<u32, 4> want{x, y, z, w};
    const auto& got = ctx.ee.vf[size_t(r)];
    bool ok = got == want;
    std::string msg = std::string(what) + " vf" + std::to_string(r);
    if (!ok) {
        msg += ": got";
        for (u32 g : got) msg += " " + hex32(g);
    }
    ctx.check(ok, msg);
}

void expect_u(DiffContext& ctx, const char* what, u64 got, u64 want) {
    ctx.check(got == want, std::string(what) + ": got " + std::to_string(got) + " want " + std::to_string(want));
}

u8* quad(Ee& ee, u32 q) { return ee.host_ptr(Ee::kVu0MemBase + q * 16, 16); }
void put_quad(Ee& ee, u32 q, u32 x, u32 y, u32 z, u32 w) {
    u32 v[4] = {x, y, z, w};
    std::memcpy(quad(ee, q), v, 16);
}
std::array<u32, 4> get_quad(Ee& ee, u32 q) {
    std::array<u32, 4> v{};
    std::memcpy(v.data(), quad(ee, q), 16);
    return v;
}

} // namespace

SELF_TEST(vu0_matrix_vector, "VMULAx/VMADDAy/VMADDAz/VMADDw 4x4 matrix * vector") {
    Ee& ee = ctx.ee;
    setv(ee, 1, 1, 0, 0, 0);
    setv(ee, 2, 0, 2, 0, 0);
    setv(ee, 3, 0, 0, 3, 0);
    setv(ee, 4, 10, 20, 30, 1);
    setv(ee, 5, 1, 2, 3, 1);
    run_code(ee, {vmulax(5, 1), vmadday(5, 2), vmaddaz(5, 3), vmaddw(XYZW, 6, 4, 5)});
    expect_v(ctx, "mat*vec", 6, 11, 24, 39, 1);
    expect_v(ctx, "acc via vf0 w", 5, 1, 2, 3, 1);
}

SELF_TEST(vu0_dest_mask, "only dest-enabled lanes are written") {
    Ee& ee = ctx.ee;
    setv(ee, 1, 1, 2, 3, 4);
    setv(ee, 2, 10, 20, 30, 40);
    setv(ee, 3, 9, 9, 9, 9);
    run_code(ee, {vadd(X | Z, 3, 1, 2)});
    expect_v(ctx, "add.xz", 3, 11, 9, 33, 9);
    run_code(ee, {vsub(Y | W, 3, 2, 1)});
    expect_v(ctx, "sub.yw", 3, 11, 18, 33, 36);
}

SELF_TEST(vu0_broadcast, "broadcast, Q and I operand forms") {
    Ee& ee = ctx.ee;
    setv(ee, 1, 1, 2, 3, 4);
    setv(ee, 2, 5, 6, 7, 8);
    run_code(ee, {co(XYZW, 2, 1, 3, 0x18 | 1)});  // VMULy
    expect_v(ctx, "mulby", 3, 6, 12, 18, 24);
    run_code(ee, {co(XYZW, 2, 1, 3, 0x00 | 3)});  // VADDw
    expect_v(ctx, "addw", 3, 9, 10, 11, 12);
    run_code(ee, {co(XYZW, 2, 1, 3, 0x04 | 2)});  // VSUBz
    expect_v(ctx, "subz", 3, -6, -5, -4, -3);
    ee.gpr[a0].lo = F(0.5f);
    run_code(ee, {ctc2(a0, 22), vmulq(XYZW, 3, 1)});
    expect_v(ctx, "mulq", 3, 0.5f, 1, 1.5f, 2);
    ee.gpr[a0].lo = F(2.0f);
    run_code(ee, {ctc2(a0, 21), vaddi(XYZW, 3, 1)});
    expect_v(ctx, "addi", 3, 3, 4, 5, 6);
    // VMADDq: fd = ACC + fs*Q, ACC from VMULAx (1*5 = 5 in x lane ... all lanes: fs * ft.x)
    ee.gpr[a0].lo = F(2.0f);
    run_code(ee, {ctc2(a0, 22), vmulax(2, 1), co(XYZW, 0, 1, 4, 0x21)});  // VMADDq
    expect_v(ctx, "maddq", 4, 7, 14, 21, 28);
}

SELF_TEST(vu0_vf0, "vf0 reads (0,0,0,1) and ignores writes") {
    Ee& ee = ctx.ee;
    setv(ee, 1, 1, 2, 3, 4);
    run_code(ee, {vadd(XYZW, 2, 0, 0), vadd(XYZW, 0, 1, 1)});
    expect_v(ctx, "vf0+vf0", 2, 0, 0, 0, 2);
    expect_v(ctx, "vf0 intact", 0, 0, 0, 0, 1);
}

SELF_TEST(vu0_cross_product, "VOPMULA + VOPMSUB cross product") {
    Ee& ee = ctx.ee;
    setv(ee, 1, 1, 2, 3, 0);
    setv(ee, 2, 4, 5, 6, 0);
    setv(ee, 3, 0, 0, 0, 7);
    run_code(ee, {sp2(XYZW, 2, 1, 0x2E), co(XYZW, 1, 2, 3, 0x2E)});
    expect_v(ctx, "cross", 3, -3, 6, -3, 7);
}

SELF_TEST(vu0_div_sqrt, "VDIV/VSQRT/VRSQRT feeding VMULq") {
    Ee& ee = ctx.ee;
    setv(ee, 1, 1, 8, 0, 0);
    setv(ee, 2, 4, 0, 0, 0);
    setv(ee, 4, 4, 8, 12, 16);
    run_code(ee, {sp2f(0, 0, 2, 1, 0x38), vmulq(XYZW, 3, 4)});  // Q = 1/4
    expect_v(ctx, "div", 3, 1, 2, 3, 4);
    run_code(ee, {sp2f(0, 0, 2, 1, 0x39), vmulq(XYZW, 3, 4)});  // Q = sqrt(4)
    expect_v(ctx, "sqrt", 3, 8, 16, 24, 32);
    run_code(ee, {sp2f(0, 1, 2, 1, 0x3A), vmulq(XYZW, 3, 4)});  // Q = 8 / sqrt(4) = 4
    expect_v(ctx, "rsqrt", 3, 16, 32, 48, 64);
    // fsf/ftf selection: ft.x=4, fs.y=8; 8/4 = 2
    run_code(ee, {sp2f(0, 1, 2, 1, 0x38), vmulq(XYZW, 3, 4)});
    expect_v(ctx, "div fsf=y", 3, 8, 16, 24, 32);
    // divide by zero: +max, D flag set
    setv(ee, 2, 0, 0, 0, 0);
    run_code(ee, {sp2f(0, 0, 2, 1, 0x38)});
    expect_u(ctx, "div0 Q", ee.vq, 0x7FFFFFFF);
    ctx.check((ee.vstatus & 0x20) != 0, "div0 sets D flag");
}

SELF_TEST(vu0_fixed_point, "VFTOI4/VITOF0/VITOF4/VFTOI0/VFTOI12 round trips") {
    Ee& ee = ctx.ee;
    setv(ee, 1, 1.5f, -2.25f, 0.0625f, 100);
    run_code(ee, {sp2(XYZW, 2, 1, 0x15)});  // FTOI4
    expect_bits(ctx, "ftoi4", 2, 24, u32(-36), 1, 1600);
    run_code(ee, {sp2(XYZW, 3, 2, 0x10)});  // ITOF0
    expect_v(ctx, "itof0", 3, 24, -36, 1, 1600);
    run_code(ee, {sp2(XYZW, 3, 2, 0x11)});  // ITOF4
    expect_v(ctx, "itof4", 3, 1.5f, -2.25f, 0.0625f, 100);
    setv(ee, 1, 3.75f, -3.75f, 0.5f, 1e10f);
    run_code(ee, {sp2(XYZW, 2, 1, 0x14)});  // FTOI0 truncates toward zero, saturates
    expect_bits(ctx, "ftoi0", 2, 3, u32(-3), 0, 0x7FFFFFFF);
    setv(ee, 1, 1, 0.5f, -1, 0);
    run_code(ee, {sp2(XYZW, 2, 1, 0x16)});  // FTOI12
    expect_bits(ctx, "ftoi12", 2, 4096, 2048, u32(-4096), 0);
}

SELF_TEST(vu0_minmax, "VMINI/VMAX/VMAXx") {
    Ee& ee = ctx.ee;
    setv(ee, 1, 1, -5, 3, 7);
    setv(ee, 2, 2, -6, 3, 0);
    run_code(ee, {vmini(XYZW, 3, 1, 2), vmax(XYZW, 4, 1, 2)});
    expect_v(ctx, "mini", 3, 1, -6, 3, 0);
    expect_v(ctx, "max", 4, 2, -5, 3, 7);
    run_code(ee, {co(XYZW, 2, 1, 5, 0x10)});  // VMAXx: max(vf1, vf2.x=2)
    expect_v(ctx, "maxx", 5, 2, 2, 3, 7);
}

SELF_TEST(vu0_move_mr32_abs, "VMOVE/VMR32/VABS") {
    Ee& ee = ctx.ee;
    setv(ee, 1, 1, -2, 3, -4);
    setv(ee, 2, 9, 9, 9, 9);
    run_code(ee, {sp2(XYZW, 3, 1, 0x31)});
    expect_v(ctx, "mr32", 3, -2, 3, -4, 1);
    run_code(ee, {sp2(X | Y, 2, 1, 0x30)});
    expect_v(ctx, "move.xy", 2, 1, -2, 9, 9);
    run_code(ee, {sp2(XYZW, 4, 1, 0x1D)});
    expect_v(ctx, "abs", 4, 1, 2, 3, 4);
}

SELF_TEST(vu0_lqi_sqi, "VLQI/VSQI/VLQD/VSQD with VI increment") {
    Ee& ee = ctx.ee;
    put_quad(ee, 2, F(1), F(2), F(3), F(4));
    put_quad(ee, 3, F(5), F(6), F(7), F(8));
    ee.vi[1] = 2;
    ee.vi[2] = 8;
    // VLQI vf1,(vi1++) ; VLQI vf2,(vi1++) ; VSQI vf1,(vi2++) ; VSQI.xy vf2,(vi2++)
    run_code(ee, {sp2(XYZW, 1, 1, 0x34), sp2(XYZW, 2, 1, 0x34), sp2(XYZW, 2, 1, 0x35), sp2(X | Y, 2, 2, 0x35)});
    expect_v(ctx, "lqi 1", 1, 1, 2, 3, 4);
    expect_v(ctx, "lqi 2", 2, 5, 6, 7, 8);
    expect_u(ctx, "vi1", ee.vi[1], 4);
    expect_u(ctx, "vi2", ee.vi[2], 10);
    auto q8 = get_quad(ee, 8), q9 = get_quad(ee, 9);
    ctx.check(q8 == std::array<u32, 4>{F(1), F(2), F(3), F(4)}, "sqi stored quad 8");
    ctx.check(q9[0] == F(5) && q9[1] == F(6), "sqi.xy stored quad 9 xy");
    // VLQD vf3,(--vi1): vi1 4 -> 3, loads quad 3.
    run_code(ee, {sp2(XYZW, 3, 1, 0x36)});
    expect_v(ctx, "lqd", 3, 5, 6, 7, 8);
    expect_u(ctx, "vi1 after lqd", ee.vi[1], 3);
}

SELF_TEST(vu0_sqd_and_zero_vi, "VSQD predecrement and vi0 stays 0") {
    Ee& ee = ctx.ee;
    setv(ee, 7, 1, 2, 3, 4);
    ee.vi[3] = 21;
    run_code(ee, {sp2(XYZW, 3, 7, 0x37)});  // VSQD vf7,(--vi3)
    expect_u(ctx, "vi3", ee.vi[3], 20);
    ctx.check(get_quad(ee, 20) == std::array<u32, 4>{F(1), F(2), F(3), F(4)}, "sqd stored quad 20");
    // VLQI with is = vi0: address 0, no increment of vi0.
    put_quad(ee, 0, F(9), F(8), F(7), F(6));
    run_code(ee, {sp2(XYZW, 8, 0, 0x34)});
    expect_v(ctx, "lqi vi0", 8, 9, 8, 7, 6);
    expect_u(ctx, "vi0", ee.vi[0], 0);
}

SELF_TEST(vu0_moves, "QMTC2/QMFC2/CTC2/CFC2") {
    Ee& ee = ctx.ee;
    ee.gpr[a0].lo = (u64(0x40000000) << 32) | 0x3F800000;
    ee.gpr[a0].hi = (u64(0x41000000) << 32) | 0x40800000;
    run_code(ee, {qmtc2(a0, 5), qmfc2(t0, 5), qmtc2(a0, 0), qmfc2(t1, 0)});
    expect_bits(ctx, "qmtc2", 5, 0x3F800000, 0x40000000, 0x40800000, 0x41000000);
    expect_u(ctx, "qmfc2 lo", ee.gpr[t0].lo, ee.gpr[a0].lo);
    expect_u(ctx, "qmfc2 hi", ee.gpr[t0].hi, ee.gpr[a0].hi);
    expect_u(ctx, "vf0 qmfc2 w", ee.gpr[t1].hi >> 32, 0x3F800000);
    expect_u(ctx, "vf0 qmfc2 xyz", ee.gpr[t1].lo, 0);

    ee.gpr[a1].lo = 0x1234;
    ee.gpr[a2].lo = 0xFFFFFFFF80000001ull;
    run_code(ee, {ctc2(a1, 5), cfc2(v0, 5), ctc2(a1, 0), cfc2(v1, 0), ctc2(a2, 22), cfc2(t2, 22), ctc2(a1, 20), cfc2(t3, 20),
                  ctc2(a2, 21), cfc2(t4, 21)});
    expect_u(ctx, "vi5", ee.gpr[v0].lo, 0x1234);
    expect_u(ctx, "vi0 stays 0", ee.gpr[v1].lo, 0);
    expect_u(ctx, "Q sign-extended", ee.gpr[t2].lo, 0xFFFFFFFF80000001ull);
    expect_u(ctx, "R", ee.gpr[t3].lo, 0x1234);
    expect_u(ctx, "I", ee.gpr[t4].lo, 0xFFFFFFFF80000001ull);
    expect_u(ctx, "ee.vi[5]", ee.vi[5], 0x1234);
    // VPU-STAT reads 0
    run_code(ee, {cfc2(v0, 29)});
    expect_u(ctx, "vpu-stat", ee.gpr[v0].lo, 0);
}

SELF_TEST(vu0_clamp_flags, "PS2 float clamping and MAC/status flags") {
    Ee& ee = ctx.ee;
    setv(ee, 1, 0, 0, 0, 0);
    ee.vf[1] = {0x7F7FFFFF, 0x7F7FFFFF, 0x3F800000, 0x00000000};
    ee.vf[2] = {0x7F7FFFFF, 0xFF7FFFFF, 0xBF800000, 0x3F800000};
    run_code(ee, {vmul(XYZW, 3, 1, 2), cfc2(v0, 17), cfc2(v1, 16)});
    expect_bits(ctx, "huge*huge", 3, 0x7FFFFFFF, 0xFFFFFFFF, 0xBF800000, 0);
    // lane x is the top bit of each nibble: x overflow (bit 3), y overflow (bit 2),
    // y/z sign (bits 10, 9), w zero (bit 12).
    const u32 exp = (1u << 3) | (1u << 2) | (1u << 10) | (1u << 9) | (1u << 12);
    expect_u(ctx, "MAC", ee.gpr[v0].lo, exp);
    expect_u(ctx, "status low nibble (Z,S,O)", ee.gpr[v1].lo & 0xF, 0xB);
}

SELF_TEST(vu0_integer, "VIADD/VISUB/VIADDI/VIAND/VIOR/VMFIR/VMTIR/ILWR/ISWR") {
    Ee& ee = ctx.ee;
    ee.vi[1] = 5;
    ee.vi[2] = 7;
    run_code(ee, {co(0, 2, 1, 3, 0x30), co(0, 2, 1, 4, 0x31), co(0, 5, 1, (u32(-3) & 31), 0x32), co(0, 2, 1, 6, 0x34),
                  co(0, 2, 1, 7, 0x35)});
    expect_u(ctx, "viadd", ee.vi[3], 12);
    expect_u(ctx, "visub", ee.vi[4], 0xFFFE);
    expect_u(ctx, "viaddi", ee.vi[5], 2);
    expect_u(ctx, "viand", ee.vi[6], 5 & 7);
    expect_u(ctx, "vior", ee.vi[7], 5 | 7);
    // VMFIR sign extends; VMTIR takes the low 16 bits of the chosen lane.
    ee.vi[1] = 0xFFFE;
    setv(ee, 9, 0, 0, 0, 0);
    ee.vf[9] = {0x11112222, 0x33334444, 0x55556666, 0x77778888};
    run_code(ee, {sp2(XYZW, 2, 1, 0x3D), sp2f(0, 2, 8, 9, 0x3C)  /* MTIR vi8 = vf9.z */});
    expect_bits(ctx, "mfir", 2, 0xFFFFFFFE, 0xFFFFFFFE, 0xFFFFFFFE, 0xFFFFFFFE);
    expect_u(ctx, "mtir", ee.vi[8], 0x6666);
    // ISWR / ILWR
    ee.vi[1] = 30;
    ee.vi[2] = 0xBEEF;
    put_quad(ee, 30, 1, 2, 3, 4);
    run_code(ee, {sp2(Y | W, 2, 1, 0x3F), sp2(Z, 3, 1, 0x3E)});
    auto q = get_quad(ee, 30);
    ctx.check(q == std::array<u32, 4>{1, 0xBEEF, 3, 0xBEEF}, "iswr.yw");
    expect_u(ctx, "ilwr.z", ee.vi[3], 3);
}

SELF_TEST(vu0_clip, "VCLIPw builds a 6-bit-per-step history") {
    Ee& ee = ctx.ee;
    setv(ee, 1, 2, -2, 0.5f, 0);
    setv(ee, 2, 0, 0, 0, 1);
    run_code(ee, {sp2(W, 2, 1, 0x1F), cfc2(v0, 18)});
    expect_u(ctx, "clip 1", ee.gpr[v0].lo, 1 | 8);
    setv(ee, 1, 0, 0, 5, 0);
    run_code(ee, {sp2(W, 2, 1, 0x1F), cfc2(v0, 18)});
    expect_u(ctx, "clip 2", ee.gpr[v0].lo, (9u << 6) | 0x10);
    setv(ee, 1, 0, 0, -5, 0);
    run_code(ee, {sp2(W, 2, 1, 0x1F), cfc2(v0, 18)});
    expect_u(ctx, "clip 3", ee.gpr[v0].lo, ((((9u << 6) | 0x10) << 6) | 0x20));
}

SELF_TEST(vu0_branch, "BC2F taken / BC2T not taken (VU0 never busy)") {
    Ee& ee = ctx.ee;
    ee.gpr[v0].lo = 0;
    ee.gpr[v1].lo = 0;
    run_code(ee, {bc2(0, 2), nop, addiu(v0, zero, 1), addiu(v1, zero, 2)});
    expect_u(ctx, "bc2f taken skips", ee.gpr[v0].lo, 0);
    expect_u(ctx, "bc2f target runs", ee.gpr[v1].lo, 2);
    ee.gpr[v0].lo = 0;
    run_code(ee, {bc2(1, 2), nop, addiu(v0, zero, 1)});
    expect_u(ctx, "bc2t falls through", ee.gpr[v0].lo, 1);
    // Likely forms: BC2FL taken executes delay slot; BC2TL not taken nullifies it.
    ee.gpr[v0].lo = 0;
    ee.gpr[v1].lo = 0;
    run_code(ee, {bc2(2, 2), addiu(v0, zero, 5), addiu(v1, zero, 9), nop});
    expect_u(ctx, "bc2fl delay slot", ee.gpr[v0].lo, 5);
    expect_u(ctx, "bc2fl skipped", ee.gpr[v1].lo, 0);
    ee.gpr[v0].lo = 0;
    run_code(ee, {bc2(3, 2), addiu(v0, zero, 5)});
    expect_u(ctx, "bc2tl nullified", ee.gpr[v0].lo, 0);
}

SELF_TEST(vu0_random_unit, "VRINIT/VRGET/VRNEXT/VRXOR and trap on VCALLMS") {
    Ee& ee = ctx.ee;
    ee.vf[1] = {0x3F812345, 0, 0, 0};
    run_code(ee, {sp2f(0, 0, 0, 1, 0x42), sp2(XYZW, 2, 0, 0x41), sp2(XYZW, 3, 0, 0x40), cfc2(v0, 20)});
    expect_bits(ctx, "rget", 2, 0x3F812345, 0x3F812345, 0x3F812345, 0x3F812345);
    expect_bits(ctx, "rnext", 3, 0x3F82468A, 0x3F82468A, 0x3F82468A, 0x3F82468A);
    expect_u(ctx, "R after rnext", ee.gpr[v0].lo, 0x3F82468A);
    ee.vf[1] = {0x00000001, 0, 0, 0};
    run_code(ee, {sp2f(0, 0, 0, 1, 0x43), cfc2(v0, 20)});
    expect_u(ctx, "rxor", ee.gpr[v0].lo, 0x3F82468Bu);
    bool trapped = false;
    try {
        run_code(ee, {co(0, 0, 0, 0, 0x38)});  // VCALLMS
    } catch (const EeTrap&) {
        trapped = true;
    }
    ctx.check(trapped, "VCALLMS traps");
}

} // namespace gt3r::diff
