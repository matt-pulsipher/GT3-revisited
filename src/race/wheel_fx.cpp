#include "race/wheel_fx.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt3r::race {

namespace {

float rdf(ByteSpan d, size_t o) {
    u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

// The program's constants: the level thresholds, 1024 / pi (the
// tread's travel per radian in 1/16 texels of a 128-texel repeat) and 3 degrees.
constexpr float kDetail[3] = {0.09f, 0.03f, 0.015f};
constexpr float kTreadPerRadian = 325.94931f;
constexpr float kSpinningImage = 0.052359879f;

} // namespace

WheelEnv WheelEnv::parse(ByteSpan env0) {
    WheelEnv e;
    if (!has_magic(env0, "Env0") || env0.size() < 0xEA8) return e;
    e.glow_gain = rdf(env0, 0x4C);
    e.blur_time = rdf(env0, 0xEA0);
    e.blur_falloff = rdf(env0, 0xEA4);
    return e;
}

int wheel_detail(float proj_x, float bound_radius, float depth) {
    const float s = depth > 0 ? proj_x * bound_radius / depth : 1e9f;
    return s > kDetail[0] ? 0 : s > kDetail[1] ? 1 : s > kDetail[2] ? 2 : 3;
}

float wheel_blur_turn(float spin_step, float moved, const WheelEnv& env) {
    return -spin_step * (env.blur_time * std::exp(env.blur_falloff * moved));
}

bool tread_blur_wanted(u32 tyre_flags, bool outer_face_seen) {
    if (tyre_flags & 2) return (tyre_flags & 1) != 0;
    return outer_face_seen && (tyre_flags & 1) != 0;
}

std::array<float, 3> tread_blur_shifts(float turn, u32 repeats, bool right) {
    float f = turn * 0.5f * (float(repeats) * kTreadPerRadian);
    if (right) f = -f;
    std::array<float, 3> out{};
    for (float& s : out) {
        f *= 0.5f;
        // 1/16 texels, wrapped at 128 texels (the EE's conversion saturates)
        const s32 units = s32(std::trunc(std::clamp(f, -2.0e9f, 2.0e9f))) & 0x7FF;
        s = float(units) * 0.0625f / 128.0f;
    }
    return out;
}

std::array<float, 4> rim_blur_angles(float turn, bool right) {
    float a = turn * 0.5f;
    if (right) a = -a;
    std::array<float, 4> out{};
    for (float& x : out) x = a, a *= 0.5f;
    return out;
}

float rim_blur_lod(u32 texture_width) {
    return texture_width > 64 ? std::log2(float(texture_width) / 64.0f) : 0.0f;
}

bool rim_spinning_image(float turn) { return turn > kSpinningImage || turn < -kSpinningImage; }

} // namespace gt3r::race
