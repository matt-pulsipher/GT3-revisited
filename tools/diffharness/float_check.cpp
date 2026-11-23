// --float-check <library> [millions]: src/base/ee_float's multiply, multiply-add, divide, square root and reciprocal square
// root against an outside float model's results (a shared library with oracle_mul / _madd / _msub / _div / _sqrt / _rsqrt,
// as GT3R_FLOAT_ORACLE loads it, u32 bit patterns in and out): random bit patterns, every exponent pair, zero and denormal
// operands, and every square root bit pattern; prints the first differences of each and their counts.

#include "ps2float.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <functional>
#include <random>
#include <thread>
#include <vector>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace gt3r::diff {

int float_check(const char* library, long millions) {
#if defined(_WIN32)
    std::fprintf(stderr, "--float-check: not available on Windows (%s, %ld)\n", library, millions);
    return 2;
#else
    void* lib = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        std::fprintf(stderr, "--float-check: %s\n", dlerror());
        return 2;
    }
    using Fn1 = u32 (*)(u32);
    using Fn2 = u32 (*)(u32, u32);
    using Fn3 = u32 (*)(u32, u32, u32);
    const auto mul = Fn2(dlsym(lib, "oracle_mul")), div = Fn2(dlsym(lib, "oracle_div")), rsqrt = Fn2(dlsym(lib, "oracle_rsqrt"));
    const auto madd = Fn3(dlsym(lib, "oracle_madd")), msub = Fn3(dlsym(lib, "oracle_msub"));
    const auto sqrt = Fn1(dlsym(lib, "oracle_sqrt"));
    if (!mul || !div || !rsqrt || !madd || !msub || !sqrt) {
        std::fprintf(stderr, "--float-check: %s lacks an oracle_* function\n", library);
        return 2;
    }
    const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    long total = 0;
    // Runs check(i, rng) for i in [0, n) over the threads; check returns false on a difference (printed by the caller).
    auto run = [&](const char* name, u64 n, const std::function<bool(u64, std::mt19937_64&, char*)>& check) {
        std::atomic<long> bad{0};
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; ++t)
            pool.emplace_back([&, t] {
                std::mt19937_64 rng(0x9E3779B97F4A7C15ull * (t + 1));
                char what[160];
                for (u64 i = t; i < n; i += threads)
                    if (!check(i, rng, what) && ++bad <= 4) std::printf("  %s: %s\n", name, what);
            });
        for (std::thread& t : pool) t.join();
        std::printf("%s: %ld of %llu differ\n", name, bad.load(), (unsigned long long)n);
        total += bad;
    };
    auto two = [&](u32 a, u32 b, u32 c, char* what) {
        const u32 m[] = {ps2f::mul(a, b), ps2f::div(a, b), ps2f::rsqrt(a, b), ps2f::madd(c, a, b), ps2f::msub(c, a, b)};
        const u32 o[] = {mul(a, b), div(a, b), rsqrt(a, b), madd(c, a, b), msub(c, a, b)};
        static const char* const op[] = {"mul", "div", "rsqrt", "madd", "msub"};
        for (int k = 0; k < 5; ++k)
            if (m[k] != o[k]) {
                std::snprintf(what, 160, "%s %08x %08x (acc %08x): ours %08x, the model's %08x", op[k], a, b, c, m[k], o[k]);
                return false;
            }
        return true;
    };
    const u64 n = u64(millions) * 1000000;
    run("random bit patterns", n, [&](u64, std::mt19937_64& r, char* w) {
        const u64 v = r();
        return two(u32(v), u32(v >> 32), u32(r()), w);
    });
    run("every exponent pair", 256 * 256 * 64, [&](u64 i, std::mt19937_64& r, char* w) {
        const u64 v = r();
        const u32 a = (u32(v) & 0x807FFFFFu) | u32(i >> 14 & 255) << 23;
        const u32 b = (u32(v >> 32) & 0x807FFFFFu) | u32(i >> 6 & 255) << 23;
        return two(a, b, u32(r()), w);
    });
    run("zero and denormal operands", n / 50 + 1, [&](u64 i, std::mt19937_64& r, char* w) {
        const u64 v = r();
        u32 a = u32(v), b = u32(v >> 32);
        if (i & 1) a &= 0x807FFFFFu;
        if (i & 2) b &= 0x807FFFFFu;
        if (i & 4) a &= 0x80000000u;
        if (i & 8) b &= 0x80000000u;
        return two(a, b, u32(r()), w);
    });
    run("every square root", u64(1) << 32, [&](u64 i, std::mt19937_64&, char* w) {
        const u32 a = u32(i), m = ps2f::sqrt(a), o = sqrt(a);
        if (m == o) return true;
        std::snprintf(w, 160, "sqrt %08x: ours %08x, the model's %08x", a, m, o);
        return false;
    });
    std::printf("%s\n", total ? "DIFFERENCES" : "every result the same");
    return total ? 1 : 0;
#endif
}

} // namespace gt3r::diff
