#include "harness.h"

#include "asm.h"
#include "ps2float.h"

#include "disc/region.h"
#include "disc/volume.h"

#include <cstdio>

namespace gt3r::diff {

// The region's course folder, else the first of the other folders a disc keeps its courses in that holds the course (as
// the game's course_file_path: 2002 Tokyo-Geneva's Asian disc keeps them in another folder than its region names).
std::string course_path(const Volume& vol, const std::string& label) {
    const RegionData& r = vol.region_data();
    const std::string own = r.course_path(label, r.default_language());
    if (vol.find(own)) return own;
    for (const char* dir : {"courses/", "courses/US/", "courses/UK/", "courses/EU/", "courses/JP/"})
        if (const std::string p = dir + label; vol.find(p)) return p;
    return own;
}

std::string course_dir(const Volume& vol) {
    const RegionData& r = vol.region_data();
    const std::string x = r.course_path("x", r.default_language()), own = x.substr(0, x.size() - 2);
    if (!vol.list_dir(own).empty()) return own;
    for (const char* dir : {"courses", "courses/US", "courses/UK", "courses/EU", "courses/JP"})
        if (!vol.list_dir(dir).empty()) return dir;
    return own;
}

std::string db_path(const Volume& vol) { return std::string("database/paramdb") + vol.region_data().db_suffix() + ".db"; }

std::vector<DiffTest>& registry() {
    static std::vector<DiffTest> r;
    return r;
}

std::string Rng::label(int lo, int hi) {
    static const char kAlpha[] = "abcdefghijklmnopqrstuvwxyz0123456789_";
    int n = int(range(lo, hi));
    std::string s;
    for (int i = 0; i < n; ++i) s.push_back(kAlpha[range(0, sizeof(kAlpha) - 2)]);
    return s;
}

std::string Rng::bytes_no_nul(int lo, int hi) {
    int n = int(range(lo, hi));
    std::string s;
    for (int i = 0; i < n; ++i) s.push_back(char(range(1, 255)));
    return s;
}

void DiffContext::record(const std::string& line) {
    ++stats.mismatches;
    if (stats.examples.size() < 8) stats.examples.push_back(line);
}

void DiffContext::expect_eq(u64 orig, u64 ours, const std::function<std::string()>& inputs) {
    ++stats.samples;
    if (orig == ours) return;
    char b[96];
    std::snprintf(b, sizeof b, "orig=0x%llX ours=0x%llX", (unsigned long long)orig, (unsigned long long)ours);
    double err = std::fabs(double(s64(orig)) - double(s64(ours)));
    if (err > stats.max_abs) {
        stats.max_abs = err;
        stats.worst = inputs();
    }
    record(inputs() + " -> " + b);
}

void DiffContext::expect_near(double orig, double ours, double abs_tol, double rel_tol,
                              const std::function<std::string()>& inputs) {
    ++stats.samples;
    double err = std::fabs(orig - ours);
    double rel = orig != 0 ? err / std::fabs(orig) : (err > 0 ? INFINITY : 0);
    if (err > stats.max_abs) {
        stats.max_abs = err;
        stats.worst = inputs();
    }
    if (rel > stats.max_rel && std::isfinite(rel)) stats.max_rel = rel;
    if (err <= abs_tol || rel <= rel_tol) return;
    char b[128];
    std::snprintf(b, sizeof b, "orig=%.9g ours=%.9g (abs %.3g, rel %.3g)", orig, ours, err, rel);
    record(inputs() + " -> " + b);
}

void DiffContext::check(bool ok, const std::string& what) {
    ++stats.samples;
    if (!ok) record(what);
}

std::string hex128(const u128& v) {
    char b[40];
    std::snprintf(b, sizeof b, "%016llX_%016llX", (unsigned long long)v.hi, (unsigned long long)v.lo);
    return b;
}

std::string hexf(u32 bits) {
    char b[48];
    std::snprintf(b, sizeof b, "%08X(%.9g)", bits, double(ps2f::to_host(bits)));
    return b;
}

void self_test_setup(Ee& ee) {
    static const Bytes image(0x10000, 0);
    ee.protect_text = false;
    ee.clear_allowed_calls();
    ee.load_program(image, kSelfCode, kSelfCode + 0x10000, 0x00110000, 0x00120000, 0x00118000);
}

CallResult run_code(Ee& ee, const std::vector<u32>& code, const std::vector<Arg>& args) {
    std::vector<u32> all = code;
    all.push_back(enc::jr_ra);
    all.push_back(enc::nop);
    if (all.size() * 4 > 0x10000) throw std::runtime_error("self-test code too long");
    for (size_t i = 0; i < all.size(); ++i) ee.w32(kSelfCode + u32(i * 4), all[i]);
    return ee.call(kSelfCode, args, 100000);
}

} // namespace gt3r::diff
