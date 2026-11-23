#pragma once

// Differential test registry and helpers. A test runs a function of the
// user's CORE.GT3 in the interpreter and our reimplementation on the same
// generated inputs, and records mismatches and the maximum error.
//
//   DIFF_TEST(id_hash, "string_hash 0x231190 vs db::id_hash") {
//       for (int i = 0; i < ctx.iterations; ++i) {
//           std::string s = ctx.rng.label(1, 40);
//           ctx.ee.reset();
//           u64 orig = ctx.ee.call(0x231190, {Arg::ptr(ctx.ee.alloc_cstr(s))}).v0.lo;
//           ctx.expect_eq(orig, db::id_hash(s), [&] { return "label=\"" + s + "\""; });
//       }
//   }
//
// SELF_TEST(...) registers an interpreter unit test that needs no disc: it
// runs hand-encoded instructions (see asm.h) in an empty program.

#include "ee.h"
#include "program_map.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace gt3r {
class Disc;
class Volume;
}

namespace gt3r::diff {

// Layout of the loaded program, derived from the image itself.
struct ProgramInfo {
    u32 load = 0, entry = 0, text_end = 0, bss_start = 0, bss_end = 0, gp = 0;
    u32 image_size = 0;
};

// xorshift64* generator with helpers for typical inputs.
class Rng {
public:
    explicit Rng(u64 seed = 0x9E3779B97F4A7C15ull) : s_(seed ? seed : 1) {}
    u64 next() {
        s_ ^= s_ >> 12;
        s_ ^= s_ << 25;
        s_ ^= s_ >> 27;
        return s_ * 0x2545F4914F6CDD1Dull;
    }
    u32 u32_() { return u32(next() >> 32); }
    // Uniform integer in [lo, hi].
    s64 range(s64 lo, s64 hi) { return lo + s64(next() % u64(hi - lo + 1)); }
    float uniform(float lo, float hi) { return lo + (hi - lo) * float(next() >> 40) / float(1ull << 24); }
    bool chance(double p) { return double(next() >> 11) / double(1ull << 53) < p; }
    // Random printable label of length [lo, hi] (database-label alphabet).
    std::string label(int lo, int hi);
    // Random bytes 1..255 (no NUL) of length [lo, hi].
    std::string bytes_no_nul(int lo, int hi);

private:
    u64 s_;
};

struct DiffStats {
    u64 samples = 0, mismatches = 0;
    double max_abs = 0, max_rel = 0;
    std::string worst; // inputs of the largest error
    std::vector<std::string> examples;
};

class DiffContext {
public:
    Ee& ee;
    Disc* disc = nullptr;          // null for self tests
    const ProgramInfo* program = nullptr;
    Rng rng;
    int iterations = 1000;          // suggested sample count (scaled by --iters)
    bool verbose = false;
    DiffStats stats;
    std::vector<std::string> notes;

    DiffContext(Ee& e, u64 seed) : ee(e), rng(seed) {}

    // Exact comparison.
    void expect_eq(u64 orig, u64 ours, const std::function<std::string()>& inputs);
    // Floating comparison: passes when |orig - ours| <= abs_tol or relative error <= rel_tol.
    void expect_near(double orig, double ours, double abs_tol, double rel_tol, const std::function<std::string()>& inputs);
    // Self-test assertion (counts as one sample).
    void check(bool ok, const std::string& what);
    void note(std::string s) { notes.push_back(std::move(s)); }
    void sample() { ++stats.samples; }

private:
    void record(const std::string& line);
};

struct DiffTest {
    const char* name;
    const char* description;
    void (*fn)(DiffContext&);
    bool needs_disc;
    bool every_program = false; // runs on the PAL and NTSC-J programs too (its addresses go through pa(), program_map.h)
};

std::vector<DiffTest>& registry();

struct DiffRegistrar {
    DiffRegistrar(const char* n, const char* d, void (*fn)(DiffContext&), bool disc, bool every_program = false) {
        registry().push_back({n, d, fn, disc, every_program});
    }
};

// A course file on the disc as its program reads it in its region's first language (disc/region.h): courses/US/<label> on
// NTSC-U, courses/UK/<label> on PAL, courses/<label> on NTSC-J; and the folder.
std::string course_path(const Volume& vol, const std::string& label);
std::string course_dir(const Volume& vol);
// The car database the disc's program loads (database/paramdb_us.db, _eu, or database/paramdb.db on NTSC-J).
std::string db_path(const Volume& vol);

// The first 128 bits of a u128 as hex, for messages.
std::string hex128(const u128& v);
std::string hexf(u32 ps2_bits);

} // namespace gt3r::diff

#define DIFF_TEST(id, desc)                                                                                            \
    static void diff_test_##id(::gt3r::diff::DiffContext& ctx);                                                        \
    static ::gt3r::diff::DiffRegistrar diff_reg_##id(#id, desc, diff_test_##id, true);                                 \
    static void diff_test_##id([[maybe_unused]] ::gt3r::diff::DiffContext& ctx)

// A test that runs on every known program version (NTSC-U, PAL, NTSC-J): the original's addresses it uses are NTSC-U
// addresses translated with pa() (program_map.h). Plain DIFF_TEST tests run on NTSC-U only and are skipped elsewhere.
#define DIFF_TEST_ALL(id, desc)                                                                                        \
    static void diff_test_##id(::gt3r::diff::DiffContext& ctx);                                                        \
    static ::gt3r::diff::DiffRegistrar diff_reg_##id(#id, desc, diff_test_##id, true, true);                           \
    static void diff_test_##id([[maybe_unused]] ::gt3r::diff::DiffContext& ctx)

#define SELF_TEST(id, desc)                                                                                            \
    static void self_test_##id(::gt3r::diff::DiffContext& ctx);                                                        \
    static ::gt3r::diff::DiffRegistrar self_reg_##id("self." #id, desc, self_test_##id, false);                        \
    static void self_test_##id([[maybe_unused]] ::gt3r::diff::DiffContext& ctx)
