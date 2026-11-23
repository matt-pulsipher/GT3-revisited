// gt3r_diffharness: developer-only differential tests that run functions of
// the user's own CORE.GT3 in an R5900 interpreter and compare them with our
// reimplementation. Nothing from the disc is stored or shipped.
//
//   gt3r_diffharness <iso> [filter] [--iters N] [--seed S] [--verbose]
//   gt3r_diffharness --selftest [filter]     (interpreter unit tests, no disc)
//   gt3r_diffharness --list
//   gt3r_diffharness <iso> --call ADDR [i:N | f:X | s:TEXT]... [--trace]
//                                            (run one function, print v0/v1/f0)
//   gt3r_diffharness <iso> --probe functions.tsv
//                                            (run every leaf function from an RE functions.tsv
//                                             with pointer/float dummy arguments; tallies traps,
//                                             i.e. which opcodes are still unimplemented)

#include "asm.h"
#include "harness.h"
#include "program_map.h"

#include "disc/core_image.h"
#include "disc/disc.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>

using namespace gt3r;
using namespace gt3r::diff;

namespace {

// Derives segment bounds from the program itself:
//  - .text ends after the last `jr $ra` that is followed by >16 KB without one;
//  - the start-up code at the entry point clears BSS between two lui/addiu
//    constants in $v0/$v1 and moves the third constant (in $a0) into $gp.
ProgramInfo analyse(const CoreImage& core) {
    ProgramInfo p;
    p.load = core.load_address;
    p.entry = core.entry_point;
    p.image_size = u32(core.program.size());
    ByteSpan img(core.program);
    u32 last = 0;
    for (u32 off = 0; off + 4 <= img.size(); off += 4) {
        if (rd32(img, off) != 0x03E00008) continue;
        if (last && off - last > 0x4000) break;
        last = off;
    }
    p.text_end = (p.load + last + 8 + 15) & ~15u;

    u32 regs[32] = {};
    for (u32 pc = p.entry; pc < p.entry + 0x100; pc += 4) {
        u32 op = rd32(img, pc - p.load);
        u32 rs = (op >> 21) & 31, rt = (op >> 16) & 31;
        if ((op >> 26) == 0x0F) regs[rt] = (op & 0xFFFF) << 16;
        else if ((op >> 26) == 0x09) regs[rt] = regs[rs] + u32(s32(s16(op & 0xFFFF)));
        else if ((op & 0xFC1F07FF) == 0x0000002D && ((op >> 11) & 31) == 28) { // daddu $gp, rs, $zero
            p.gp = regs[rs];
            break;
        }
        if (!p.bss_start && regs[2] && regs[3] && (op >> 26) == 0x1F) { // first sq: the BSS clear loop
            p.bss_start = regs[2];
            p.bss_end = regs[3];
        }
    }
    if (!p.gp || !p.bss_start || p.bss_end <= p.bss_start) throw FormatError("could not find start-up code layout");
    return p;
}

// Runs every leaf function listed in an RE functions.tsv (start, end, size,
// name, lib, ncallers, ncalls, tags) with dummy arguments and tallies how it
// ends. Only a coverage aid: most functions are not meaningful with dummies.
int probe(Ee& ee, const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "r");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        return 2;
    }
    std::map<std::string, int> tally;
    std::map<std::string, std::string> example;
    char line[512];
    int total = 0;
    while (std::fgets(line, sizeof line, f)) {
        unsigned start = 0, end = 0, size = 0, ncallers = 0, ncalls = 0;
        char name[256], lib[64];
        if (std::sscanf(line, "%x\t%x\t%u\t%255s\t", &start, &end, &size, name) != 4) continue;
        // columns 5-7: lib (may be empty), ncallers, ncalls
        const char* p = line;
        for (int col = 0; col < 4 && p; ++col) p = std::strchr(p, '\t') ? std::strchr(p, '\t') + 1 : nullptr;
        if (!p) continue;
        lib[0] = 0;
        if (*p != '\t') std::sscanf(p, "%63[^\t]", lib);
        p = std::strchr(p, '\t');
        if (!p || std::sscanf(p + 1, "%u\t%u", &ncallers, &ncalls) != 2 || ncalls != 0 || size > 8192) continue;
        ee.reset();
        u32 buf = ee.alloc(0x10000);
        std::vector<Arg> args;
        for (int i = 0; i < 8; ++i) args.push_back(Arg::ptr(buf + 0x1000 * u32(i)));
        for (int i = 0; i < 8; ++i) args.push_back(Arg::flt(1.0f));
        std::string key;
        try {
            ee.call(start, args, 200000);
            key = "returned";
        } catch (const EeTrap& e) {
            std::string w = e.what();
            if (e.kind == TrapKind::Unimplemented) key = "unimplemented " + w.substr(w.find(": ") + 2, 40);
            else key = w.substr(0, w.find(" at pc"));
            if (!example.count(key)) example[key] = std::string(name) + " " + hex32(e.pc);
        } catch (const std::exception& e) {
            key = std::string("exception ") + e.what();
        }
        ++tally[key];
        ++total;
    }
    std::fclose(f);
    std::printf("%d leaf functions probed\n", total);
    for (auto& [k, n] : tally) std::printf("%6d  %s%s\n", n, k.c_str(), example.count(k) ? ("   e.g. " + example[k]).c_str() : "");
    return 0;
}

void usage() {
    std::fprintf(stderr,
                 "usage: gt3r_diffharness <iso> [filter] [--iters N] [--seed S] [--verbose]\n"
                 "       gt3r_diffharness --selftest [filter]\n"
                 "       gt3r_diffharness --list\n"
                 "       gt3r_diffharness --locate-gen <spec>   (ProgramData's locator rows; docs/dev/RE_TOOLKIT.md)\n");
}

} // namespace

namespace gt3r::diff {
int locate_gen(const char* spec_path);    // locate_gen.cpp
int locate_survey(const char* spec_path); // locate_gen.cpp
int float_check(const char* library, long millions); // float_check.cpp
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--locate-gen") return gt3r::diff::locate_gen(argv[2]);
    if (argc == 3 && std::string(argv[1]) == "--locate-survey") return gt3r::diff::locate_survey(argv[2]);
    if ((argc == 3 || argc == 4) && std::string(argv[1]) == "--float-check")
        return gt3r::diff::float_check(argv[2], argc == 4 ? std::atol(argv[3]) : 100);
    std::string iso, filter;
    double iter_scale = 1.0;
    u64 seed = 12345;
    bool selftest = false, list = false, verbose = false, trace = false, locate_sites = false;
    std::string call_addr, probe_file;
    std::vector<std::string> call_args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--iters" && i + 1 < argc) iter_scale = std::atof(argv[++i]);
        else if (a == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 0);
        else if (a == "--selftest") selftest = true;
        else if (a == "--list") list = true;
        else if (a == "--verbose" || a == "-v") verbose = true;
        else if (a == "--trace") trace = true;
        else if (a == "--locate-sites") locate_sites = true;
        else if (a == "--call" && i + 1 < argc) call_addr = argv[++i];
        else if (a == "--probe" && i + 1 < argc) probe_file = argv[++i];
        else if (!call_addr.empty()) call_args.push_back(a);
        else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (iso.empty() && !selftest) iso = a;
        else filter = a;
    }
    if (list) {
        for (const DiffTest& t : registry())
            std::printf("%-28s %s%s\n", t.name, t.description, t.needs_disc ? "" : " [no disc]");
        return 0;
    }
    if (iso.empty() && !selftest) {
        usage();
        return 2;
    }

    std::unique_ptr<Disc> disc;
    CoreImage core;
    ProgramInfo prog;
    if (!selftest) {
        try {
            disc = Disc::open(iso);
            Bytes raw = disc->read_core_gt3();
            core = CoreImage::decode(raw);
            prog = analyse(core);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 2;
        }
        std::printf("program: load 0x%06X, %u bytes, .text to 0x%06X, bss 0x%06X-0x%06X, gp 0x%06X\n", prog.load,
                    prog.image_size, prog.text_end, prog.bss_start, prog.bss_end, prog.gp);
        // The program version (ProgramData's check); tests not registered for every version run on NTSC-U only.
        try {
            const sim::exact::ProgramData d = sim::exact::ProgramData::load(core);
            set_program(d.program());
            set_table_addresses(d);
            set_disc_behaviour(d);
            set_later_places(d, ByteSpan(core.program.data(), core.program.size()), core.load_address);
            // GT3R_PROGRAM=<0..7>: take the disc's program for that one (a build ProgramData does not identify yet).
            if (const char* e = std::getenv("GT3R_PROGRAM")) set_program(sim::exact::Program(std::atoi(e)));
            std::printf("program version: %s%s\n", sim::exact::program_name(program()),
                        d.known() ? "" : sim::exact::concept_program(d.program()) ? " (races on GT3's core)" : " (nearest; not known)");
            if (locate_sites) {
                // Where ProgramData recognised its tables' routines and the rules' (tools/re/locate_spec.py reads this).
                using PD = sim::exact::ProgramData;
                for (int t = 0; t < PD::kTables; ++t) {
                    const PD::Site s = d.table_site(PD::Table(t));
                    std::printf("table\t%d\t%X\t%u\t%u\t%u\t%X\t%d\n", t, s.at, s.words, s.hi, s.lo, d.table_start(PD::Table(t)),
                                d.tier(PD::Table(t)));
                }
                for (int r = 0; r < sim::exact::kRules; ++r)
                    std::printf("rule\t%d\t%X\t%d\n", r, d.rule_site(sim::exact::Rule(r)), int(d.recognised(sim::exact::Rule(r))));
                return 0;
            }
        } catch (const std::exception& e) {
            set_program(std::nullopt);
            std::printf("program version: unknown (%s)\n", e.what());
        }
    } else {
        set_program(Program::NtscU);
    }

    auto ee = std::make_unique<Ee>();
    if (!probe_file.empty()) {
        ee->load_program(core.program, prog.load, prog.text_end, prog.bss_start, prog.bss_end, prog.gp);
        return probe(*ee, probe_file);
    }
    if (!call_addr.empty()) {
        ee->load_program(core.program, prog.load, prog.text_end, prog.bss_start, prog.bss_end, prog.gp);
        ee->trace = trace;
        std::vector<Arg> args;
        for (const std::string& a : call_args) {
            if (a.rfind("i:", 0) == 0) args.push_back(Arg::integer(std::strtoll(a.c_str() + 2, nullptr, 0)));
            else if (a.rfind("f:", 0) == 0) args.push_back(Arg::flt(std::strtof(a.c_str() + 2, nullptr)));
            else if (a.rfind("s:", 0) == 0) args.push_back(Arg::ptr(ee->alloc_cstr(a.substr(2))));
            else {
                std::fprintf(stderr, "bad argument %s (use i:N, f:X or s:TEXT)\n", a.c_str());
                return 2;
            }
        }
        try {
            CallResult r = ee->call(u32(std::strtoul(call_addr.c_str(), nullptr, 16)), args);
            std::printf("v0=%s v1=%s f0=%s (%llu instructions)\n", hex128(r.v0).c_str(), hex128(r.v1).c_str(),
                        hexf(r.f0).c_str(), (unsigned long long)r.instructions);
        } catch (const std::exception& e) {
            std::printf("%s\n", e.what());
            return 1;
        }
        return 0;
    }
    bool program_loaded = false;
    int failed = 0, passed = 0, skipped = 0;
    for (const DiffTest& t : registry()) {
        if (selftest == t.needs_disc) continue; // --selftest runs only self tests, disc runs only diff tests
        if (!filter.empty() && std::string(t.name).find(filter) == std::string::npos) continue;
        if (t.needs_disc && !t.every_program && !ntsc_u()) {
            ++skipped;
            if (!filter.empty())
                std::printf("SKIP %-30s (NTSC-U only; the disc's program is %s)\n", t.name, sim::exact::program_name(program()));
            continue;
        }
        DiffContext ctx(*ee, seed);
        ctx.iterations = std::max(1, int(1000 * iter_scale));
        ctx.verbose = verbose;
        ctx.disc = disc.get();
        ctx.program = &prog;
        if (t.needs_disc) {
            if (!program_loaded) {
                ee->load_program(core.program, prog.load, prog.text_end, prog.bss_start, prog.bss_end, prog.gp);
                program_loaded = true;
            }
            ee->protect_text = true;
            ee->clear_allowed_calls();
            ee->reset();
        } else {
            self_test_setup(*ee);
            program_loaded = false;
        }
        auto t0 = std::chrono::steady_clock::now();
        std::string trap;
        try {
            t.fn(ctx);
        } catch (const EeTrap& e) {
            trap = e.what();
        } catch (const std::exception& e) {
            trap = std::string("exception: ") + e.what();
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        bool ok = trap.empty() && ctx.stats.mismatches == 0;
        ok ? ++passed : ++failed;
        std::printf("%-4s %-30s %7llu samples %5llu mismatches  max_abs %-10.4g max_rel %-10.4g %7.1f ms\n",
                    ok ? "PASS" : "FAIL", t.name, (unsigned long long)ctx.stats.samples,
                    (unsigned long long)ctx.stats.mismatches, ctx.stats.max_abs, ctx.stats.max_rel, ms);
        if (!trap.empty()) std::printf("     TRAP: %s\n", trap.c_str());
        for (const std::string& n : ctx.notes) std::printf("     note: %s\n", n.c_str());
        for (const std::string& ex : ctx.stats.examples) std::printf("     %s\n", ex.c_str());
        if (!ctx.stats.worst.empty() && (verbose || !ok)) std::printf("     worst: %s\n", ctx.stats.worst.c_str());
    }
    if (skipped) std::printf("%d passed, %d failed, %d skipped (NTSC-U only)\n", passed, failed, skipped);
    else std::printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
