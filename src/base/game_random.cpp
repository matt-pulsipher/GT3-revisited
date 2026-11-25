#include "base/game_random.h"

#include "base/frame_clock.h"

#include <chrono>
#include <cstring>

namespace gt3r {

u32 GameRandom::next(u32& x) {
    x = x * 17u + 17u;
    return x ^ ((x << 16) | (x >> 16));
}

float GameRandom::rand01(u32& x) {
    u32 bits = (next(x) & 0x7FFFFFu) | 0x3F800000u; // 1.m
    float f;
    std::memcpy(&f, &bits, 4);
    return f - 1.0f; // exact, so no rounding mode matters
}

void GameRandom::stir(u32& x, u32 hardware, GameRandom& shared) {
    int n = (int(hardware) + shared.int31()) % 13;
    skip(x, n > 0 ? n : 1);
}

GameRandom& shared_random() {
    static GameRandom r;
    return r;
}

GameRandom& race_info_random() {
    static GameRandom r;
    return r;
}

void Vu0Random::init(float seed) {
    u32 bits;
    std::memcpy(&bits, &seed, 4);
    r = 0x3F800000u | (bits & 0x7FFFFFu);
}

float Vu0Random::next() {
    const u32 feedback = ((r >> 4) ^ (r >> 22)) & 1u;
    r = ((r << 1 | feedback) & 0x7FFFFFu) | 0x3F800000u;
    float f;
    std::memcpy(&f, &r, 4);
    return f - 1.0f; // exact
}

Vu0Random& vu0_random() {
    static Vu0Random r;
    return r;
}

namespace {
u32 (*g_hardware_source)() = nullptr;
}

u32 hardware_random() {
    if (g_hardware_source) return g_hardware_source();
    if (frame_clock::fixed()) return u32(frame_clock::now_ns() % 48u); // (--shot: the same on every run)
    auto t = std::chrono::high_resolution_clock::now().time_since_epoch();
    return u32(u64(std::chrono::duration_cast<std::chrono::nanoseconds>(t).count()) % 48u);
}

void set_hardware_random_source(u32 (*source)()) { g_hardware_source = source; }

} // namespace gt3r
