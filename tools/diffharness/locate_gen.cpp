// --locate-gen <spec>: ProgramData's locator rows (src/sim/exact/program_data.cpp) from the discs (docs/dev/RE_TOOLKIT.md).
//
// The spec (written by hand or by a script from the RE outputs; it stays outside the repository) names the discs and, per
// program, the functions:
//   iso    <program 0..7> <path>                          the discs, in sim::Program's order (NTSC-U, PAL, NTSC-J, the later builds)
//   table  <name> <p> <fn start> <fn end> <hi> <lo> <at> [<off>]
//                                                          a table (`name`: ProgramData::Table's), the function that addresses
//                                                          it, the lui and addiu (one $gp addiu: hi = lo; a later build's
//                                                          compiler: also a load's or store's own offset), the address they
//                                                          form, and how far into the table that is (default 0: its start)
//   rule   <Rule> <p> <fn start> <fn end> [<as>]           a routine whose rules differ between the releases; `as`: the GT3
//                                                          release (0..2) whose rules a later build's routine has (default
//                                                          `own`: rules of its own)
//   absent <Rule> <p> [<as>]                               program p has no such routine (`as`: as that GT3 release has none)
// (tools/re/locate_spec.py writes it for every build.)
// Prints the rows: the masked-code hash of each function (a table's: a window of it around the instructions), the
// instructions' positions in it and which programs have it; checks that each table decodes to its address and that each
// window is unique in its program.

#include "disc/core_image.h"
#include "disc/disc.h"
#include "sim/exact/program_locate.h"
#include "sim/program.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace gt3r::diff {

namespace {

namespace ex = sim::exact;

struct Prog {
    std::unique_ptr<Disc> disc;
    CoreImage core;
    std::unique_ptr<ex::CodeIndex> index;
};

u32 num(const std::string& s) { return u32(std::stoul(s, nullptr, 16)); }

// The function's end less its trailing zero words (alignment padding another build may not have).
u32 trimmed_end(const ex::CodeIndex& x, u32 start, u32 end) {
    while (end > start + 4 && x.word(end - 4) == 0) end -= 4;
    return end;
}

u32 decode(const ex::CodeIndex& x, u32 hi_at, u32 lo_at) {
    const u32 lo = x.word(lo_at);
    if (hi_at == lo_at) return x.gp() + u32(s32(s16(lo & 0xFFFF)));
    return ((x.word(hi_at) & 0xFFFF) << 16) + u32(s32(s16(lo & 0xFFFF)));
}

} // namespace

int locate_gen(const char* spec_path) {
    std::ifstream in(spec_path);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", spec_path);
        return 2;
    }
    constexpr int kProgs = sim::kPrograms;
    Prog progs[kProgs];
    struct TableKey {
        std::string name;
        u64 h;
        u32 n, hi, lo, off;
        bool operator<(const TableKey& o) const {
            return std::tie(name, h, n, hi, lo, off) < std::tie(o.name, o.h, o.n, o.hi, o.lo, o.off);
        }
    };
    std::map<TableKey, int> tables;
    std::map<std::pair<std::string, int>, u32> table_at; // a table's start in each program
    std::vector<TableKey> table_order;
    struct RuleKey {
        std::string rule;
        u64 h;
        u32 n;
        std::string as;
        bool operator<(const RuleKey& o) const { return std::tie(rule, h, n, as) < std::tie(o.rule, o.h, o.n, o.as); }
    };
    std::map<RuleKey, int> rules;
    std::vector<RuleKey> rule_order;
    int errors = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::vector<std::string> f;
        for (std::string t; std::getline(ls, t, '\t');) {
            if (!t.empty() && t[0] == '#') break; // (a comment ends the line)
            f.push_back(t);
        }
        if (f[0] == "iso") {
            Prog& p = progs[std::stoi(f[1])];
            p.disc = Disc::open(f[2]);
            p.core = CoreImage::decode(p.disc->read_core_gt3());
            p.index = std::make_unique<ex::CodeIndex>(ByteSpan(p.core.program.data(), p.core.program.size()), p.core.load_address,
                                                      p.core.entry_point);
            std::fprintf(stderr, "p%s: load 0x%X, %zu bytes, entry 0x%X, code to 0x%X, gp 0x%X\n", f[1].c_str(), p.core.load_address,
                         p.core.program.size(), p.core.entry_point, p.index->text_end(), p.index->gp());
            continue;
        }
        const int pi = std::stoi(f[2]);
        const ex::CodeIndex& x = *progs[pi].index;
        if (f[0] == "table") {
            const u32 fs = num(f[3]), fe = trimmed_end(x, fs, num(f[4])), hi = num(f[5]), lo = num(f[6]), at = num(f[7]);
            u32 ws = fs, we = fe;
            if ((fe - fs) / 4 > 256) ws = std::max(fs, std::min(hi, lo) - 128), we = std::min(fe, std::max(hi, lo) + 132);
            const u32 n = (we - ws) / 4;
            const u64 h = x.hash_range(ws, we);
            const u32 got = decode(x, hi, lo);
            if (const u32 lo_op = x.word(lo) >> 26; lo_op != 0x09 && lo_op < 0x20)
                std::fprintf(stderr, "table %s p%d: the offset's instruction is neither an add nor a load / store\n", f[1].c_str(), pi),
                    ++errors;
            if (got != at) std::fprintf(stderr, "table %s p%d: decodes to 0x%X, not 0x%X\n", f[1].c_str(), pi, got, at), ++errors;
            const size_t hits = x.find(h, n).size();
            if (hits != 1) std::fprintf(stderr, "table %s p%d: window found %zu times\n", f[1].c_str(), pi, hits), ++errors;
            const TableKey k{f[1], h, n, (hi - ws) / 4, (lo - ws) / 4, f.size() > 8 ? u32(std::stoul(f[8])) : 0};
            table_at[{f[1], pi}] = at - k.off;
            if (!tables.count(k)) table_order.push_back(k);
            tables[k] |= 1 << pi;
        } else if (f[0] == "rule" || f[0] == "absent") {
            RuleKey k{f[1], 0, 0, f[0] == "rule" && f.size() > 5 && f[5] != "own" ? f[5] : f[0] == "absent" && f.size() > 3 ? f[3] : ""};
            if (f[0] == "rule") {
                const u32 fs = num(f[3]), fe = trimmed_end(x, fs, num(f[4]));
                k.n = (fe - fs) / 4, k.h = x.hash_range(fs, fe);
                if (const size_t hits = x.find(k.h, k.n).size(); hits != 1)
                    std::fprintf(stderr, "rule %s p%d: routine found %zu times\n", f[1].c_str(), pi, hits);
            }
            if (!rules.count(k)) rule_order.push_back(k);
            rules[k] |= 1 << pi;
        }
    }
    auto same_place = [&](const TableKey& k, int p) {
        const ex::CodeIndex& x = *progs[p].index;
        const u32 w = x.find(k.h, k.n)[0];
        const auto it = table_at.find({k.name, p});
        return it != table_at.end() && decode(x, w + 4 * k.hi, w + 4 * k.lo) - k.off == it->second;
    };
    // Where else each row is found: a table window or a routine found in a program its row does not name would be misread.
    for (const TableKey& k : table_order)
        for (int p = 0; p < kProgs; ++p)
            if (progs[p].index && !(tables[k] >> p & 1) && !progs[p].index->find(k.h, k.n).empty() &&
                !same_place(k, p)) // (found in a program another row serves: fine when both decode to the same table)
                std::fprintf(stderr, "table %s: a window of p%d's row %d also in p%d\n", k.name.c_str(), tables[k], k.n, p), ++errors;
    for (const RuleKey& k : rule_order)
        for (int p = 0; p < kProgs; ++p)
            if (k.n && progs[p].index && !(rules[k] >> p & 1) && !progs[p].index->find(k.h, k.n).empty())
                std::fprintf(stderr, "rule %s: mask %d's routine also in p%d\n", k.rule.c_str(), rules[k], p), ++errors;
    std::printf("// tables: name, hash, words, the address's instructions (hi, lo; one $gp addiu: hi = lo), programs\n");
    for (const TableKey& k : table_order)
        std::printf("    {T::%s, 0x%016llXull, %u, %u, %u, 0x%02X%s},\n", k.name.c_str(), (unsigned long long)k.h, k.n, k.hi, k.lo,
                    unsigned(tables[k]), k.off ? (", " + std::to_string(k.off)).c_str() : "");
    std::printf("// rules: rule, hash, words (0: the program has no such routine), programs\n");
    for (const RuleKey& k : rule_order)
        std::printf("    {Rule::%s, 0x%016llXull, %u, 0x%02X%s},\n", k.rule.c_str(), (unsigned long long)k.h, k.n, unsigned(rules[k]),
                    k.as.empty() ? "" : (", " + k.as).c_str());
    // Which programs the rows tell apart: two that every row names together would be taken for the first.
    for (int a = 0; a < kProgs; ++a)
        for (int b = a + 1; b < kProgs; ++b) {
            if (!progs[a].index || !progs[b].index) continue;
            int apart = 0;
            for (const TableKey& k : table_order) apart += (tables[k] >> a & 1) != (tables[k] >> b & 1);
            for (const RuleKey& k : rule_order) apart += k.n && (rules[k] >> a & 1) != (rules[k] >> b & 1);
            std::fprintf(stderr, "p%d / p%d: %d rows tell them apart\n", a, b, apart);
            if (!apart) ++errors;
        }
    std::fprintf(stderr, "%d problems\n", errors);
    return errors ? 1 : 0;
}

// --locate-survey <spec>: how the masking behaves on a corpus of builds. The spec's `iso` lines name the builds (0: the
// reference, NTSC-U), `funcs <functions.tsv>` the reference's functions; its `table` / `rule` lines for program 0 give the
// locator's windows. For each masking (ProgramData's; without the loaded values; also without the load / store offsets),
// prints how many of the reference's functions (8 instructions or more) each build has (found / found once), and in how
// many places of each build each window is found.
int locate_survey(const char* spec_path) {
    std::ifstream in(spec_path);
    if (!in) return 2;
    std::vector<std::string> isos;
    std::vector<std::pair<u32, u32>> funcs;
    struct Win {
        std::string name;
        u32 start, end;
    };
    std::vector<Win> wins;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::vector<std::string> f;
        for (std::string t; std::getline(ls, t, '\t');) f.push_back(t);
        if (f.empty()) continue;
        if (f[0] == "iso") {
            const size_t b = size_t(std::stoi(f[1]));
            if (b >= isos.size()) isos.resize(b + 1);
            isos[b] = f[2];
        } else if (f[0] == "funcs" && f[1] == "auto") {
            funcs.push_back({0, 0}); // the reference's function starts from its code (below)
        } else if (f[0] == "funcs") {
            std::ifstream ft(f[1]);
            for (std::string l; std::getline(ft, l);) {
                std::istringstream fl(l);
                std::string a, b;
                std::getline(fl, a, '\t'), std::getline(fl, b, '\t');
                if (a == "start" || a.empty()) continue;
                funcs.push_back({num(a), num(b)});
            }
        } else if ((f[0] == "table" || f[0] == "rule") && f[2] == "0") {
            u32 fs = num(f[3]), fe = num(f[4]);
            if (f[0] == "table") {
                const u32 hi = num(f[5]), lo = num(f[6]);
                if ((fe - fs) / 4 > 256) fs = std::max(fs, std::min(hi, lo) - 128), fe = std::min(fe, std::max(hi, lo) + 132);
            }
            wins.push_back({f[0] + " " + f[1], fs, fe});
        }
    }
    std::vector<CoreImage> cores;
    for (const std::string& p : isos) {
        if (p.empty()) break;
        cores.push_back(CoreImage::decode(Disc::open(p)->read_core_gt3()));
    }
    // `funcs auto`: a function starts after a `jr $ra` and its delay slot (padding skipped) and ends where the next starts.
    if (funcs.size() == 1 && funcs[0].first == 0) {
        funcs.clear();
        const ex::CodeIndex x(ByteSpan(cores[0].program.data(), cores[0].program.size()), cores[0].load_address, cores[0].entry_point);
        u32 start = 0;
        for (u32 a = cores[0].load_address; a + 8 <= x.text_end(); a += 4) {
            if (x.word(a) != 0x03E00008) continue;
            u32 n = a + 8;
            while (n < x.text_end() && x.word(n) == 0) n += 4;
            if (start) funcs.push_back({start, a + 8});
            start = n;
        }
    }
    const char* modes[] = {"ProgramData's masking", "without loaded values", "without values or load/store offsets",
                           "without values, offsets or registers"};
    for (int mode = 0; mode < 4; ++mode) {
        ex::CodeIndex::Options o;
        o.load_values = mode == 0, o.mask_offsets = mode >= 2, o.mask_registers = mode == 3;
        std::vector<std::unique_ptr<ex::CodeIndex>> xs;
        for (const CoreImage& c : cores)
            xs.push_back(std::make_unique<ex::CodeIndex>(ByteSpan(c.program.data(), c.program.size()), c.load_address, c.entry_point, o));
        std::printf("%s:\n  functions:", modes[mode]);
        for (size_t b = 1; b < xs.size(); ++b) {
            int n = 0, found = 0, once = 0;
            for (auto [s, e] : funcs) {
                e = trimmed_end(*xs[0], s, e);
                if (e < s + 32 || e > xs[0]->text_end()) continue;
                ++n;
                const size_t k = xs[b]->find(xs[0]->hash_range(s, e), (e - s) / 4).size();
                found += k > 0, once += k == 1;
            }
            std::printf("  b%zu %d/%d (%d once)", b, found, n, once);
        }
        std::printf("\n");
        for (const Win& w : wins) {
            const u32 e = w.name[0] == 'r' ? trimmed_end(*xs[0], w.start, w.end) : w.end;
            const u64 h = xs[0]->hash_range(w.start, e);
            std::printf("  %-34s", w.name.c_str());
            for (size_t b = 0; b < xs.size(); ++b) std::printf(" %zu", xs[b]->find(h, (e - w.start) / 4).size());
            std::printf("\n");
        }
    }
    return 0;
}

} // namespace gt3r::diff
