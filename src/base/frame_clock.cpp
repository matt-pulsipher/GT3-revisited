#include "base/frame_clock.h"

#include <SDL3/SDL_timer.h>

#include <atomic>
#include <random>

namespace gt3r::frame_clock {

namespace {
std::atomic<bool> g_fixed{false};
std::atomic<u64> g_ns{1'000'000'000};
} // namespace

u64 now_ns() { return g_fixed.load(std::memory_order_relaxed) ? g_ns.load(std::memory_order_relaxed) : SDL_GetTicksNS(); }

u64 counter() { return g_fixed.load(std::memory_order_relaxed) ? now_ns() : SDL_GetPerformanceCounter(); }

u32 random_seed() { return g_fixed.load(std::memory_order_relaxed) ? 0x6A09E667u : std::random_device{}(); }

bool fixed() { return g_fixed.load(std::memory_order_relaxed); }

void set_fixed() { g_fixed.store(true, std::memory_order_relaxed); }

void step(u64 ns) {
    if (fixed()) g_ns.fetch_add(ns, std::memory_order_relaxed);
}

} // namespace gt3r::frame_clock
