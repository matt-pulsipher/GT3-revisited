// VU0 macro-mode validation on real game code: the renderer's point
// transform (matrix in vf28-vf31) and clip-code helper (matrix in vf24-vf27,
// VCLIPw) against host math. These have no counterpart in our engine yet;
// they check the interpreter's COP2 implementation end to end.

#include "harness.h"
#include "ps2float.h"

#include <cmath>

using namespace gt3r;
using namespace gt3r::diff;

namespace {
constexpr u32 kTransformPoint = 0x002580EC; // (float4* dst, const float4* src), M = vf28..vf31 (columns)
constexpr u32 kClipCode = 0x0024BF10;       // (x bits, y bits, z bits) -> 6-bit clip code, M = vf24..vf27

void set_matrix(DiffContext& ctx, int first_vf, float m[4][4]) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            m[c][r] = ctx.rng.uniform(-4.0f, 4.0f);
            ctx.ee.vf[first_vf + c][r] = ps2f::from_host(m[c][r]);
        }
}
} // namespace

DIFF_TEST(vu0_transform_point, "point transform 0x2580EC (VMULAx/VMADDAy/VMADDAz/VMADDw) vs host") {
    float m[4][4];
    for (int i = 0; i < ctx.iterations; ++i) {
        set_matrix(ctx, 28, m);
        float v[4];
        u32 src = ctx.ee.alloc(16), dst = ctx.ee.alloc(16);
        for (int k = 0; k < 4; ++k) {
            v[k] = ctx.rng.uniform(-100.0f, 100.0f);
            ctx.ee.wf(src + 4 * k, v[k]);
        }
        ctx.ee.call(kTransformPoint, {Arg::ptr(dst), Arg::ptr(src)});
        for (int r = 0; r < 4; ++r) {
            double ours = 0, mag = 0;
            for (int c = 0; c < 4; ++c) {
                ours += double(m[c][r]) * v[c];
                mag += std::fabs(double(m[c][r]) * v[c]);
            }
            // Four chopped operations: allow a few ulps of the largest term.
            ctx.expect_near(ctx.ee.rf(dst + 4 * r), ours, mag * 4e-7, 0,
                            [&] { return "row " + std::to_string(r) + " v=(" + std::to_string(v[0]) + "," +
                                         std::to_string(v[1]) + "," + std::to_string(v[2]) + "," +
                                         std::to_string(v[3]) + ")"; });
        }
        ctx.ee.free_all();
    }
}

DIFF_TEST(vu0_clip_code, "clip code 0x24BF10 (VCLIPw, CFC2 clip flags) vs host, w > 0") {
    float m[4][4];
    int tested = 0;
    for (int i = 0; i < ctx.iterations * 4 && tested < ctx.iterations; ++i) {
        set_matrix(ctx, 24, m);
        float p[3] = {ctx.rng.uniform(-3, 3), ctx.rng.uniform(-3, 3), ctx.rng.uniform(-3, 3)};
        double t[4];
        for (int r = 0; r < 4; ++r) t[r] = m[0][r] * double(p[0]) + m[1][r] * double(p[1]) + m[2][r] * double(p[2]) + m[3][r];
        if (t[3] <= 0.01) continue; // the w < 0 branch remaps the flags; covered only via the interpreter
        bool near_edge = false;
        u32 ours = 0;
        for (int k = 0; k < 3; ++k) {
            if (std::fabs(std::fabs(t[k]) - t[3]) < 1e-3 * (std::fabs(t[k]) + t[3])) near_edge = true;
            if (t[k] > t[3]) ours |= 1u << (2 * k);
            if (t[k] < -t[3]) ours |= 2u << (2 * k);
        }
        if (near_edge) continue;
        ++tested;
        auto bits = [](float f) { return Arg::integer(s64(ps2f::from_host(f))); };
        u32 orig = u32(ctx.ee.call(kClipCode, {bits(p[0]), bits(p[1]), bits(p[2])}).v0.lo);
        ctx.expect_eq(orig, ours, [&] {
            return "p=(" + std::to_string(p[0]) + "," + std::to_string(p[1]) + "," + std::to_string(p[2]) + ")";
        });
    }
}
