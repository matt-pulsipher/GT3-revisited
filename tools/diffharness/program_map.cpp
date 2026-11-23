// The version table and pa(); see program_map.h.

#include "program_map.h"

#include "sim/exact/race_flow.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <string>

namespace gt3r::diff {

namespace {

struct Row {
    u32 at[sim::exact::kPrograms]; // NTSC-U, PAL, NTSC-J, the later builds in Program's order (0: no counterpart)
};
constexpr Row kRows[] = {
#include "program_map.inc"
};

bool g_set = false;
std::optional<Program> g_program;

const Row* find(u32 a) {
    const Row* b = std::begin(kRows);
    const Row* e = std::end(kRows);
    const Row* r = std::lower_bound(b, e, a, [](const Row& x, u32 v) { return x.at[0] < v; });
    return r != e && r->at[0] == a ? r : nullptr;
}

std::string hex(u32 v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%06X", v);
    return b;
}

} // namespace

void set_program(std::optional<Program> p) { g_set = true, g_program = p; }
std::optional<Program> disc_program() { return g_program; }
Program program() { return g_program.value_or(Program::NtscU); }
bool ntsc_u() { return program() == Program::NtscU; }

namespace {
std::optional<sim::Behaviour> g_behaviour;
} // namespace

sim::Behaviour disc_behaviour() {
    const sim::Behaviour own = sim::release_behaviour(program());
    return sim::concept_program(program()) && g_behaviour && g_behaviour->rules == own.rules ? *g_behaviour : own;
}

void set_disc_behaviour(sim::exact::ProgramData d) {
    g_behaviour.reset();
    if (!sim::concept_program(d.program())) return;
    d.set_behaviour(sim::release_behaviour(d.program()));
    sim::Behaviour b = d.behaviour();
    if (const char* e = std::getenv("GT3R_RULES_AS"))
        for (const char* p = e; *p;) {
            char* end = nullptr;
            const long rule = std::strtol(p, &end, 10);
            if (end == p || *end != ':') break;
            const long as = std::strtol(end + 1, &end, 10);
            if (rule >= 0 && rule < sim::kRules && as >= 0 && as < sim::kPrograms) b.variant[size_t(rule)] = u8(as);
            p = *end == ',' ? end + 1 : end;
        }
    g_behaviour = b;
}

namespace {
u32 g_table_at[sim::exact::ProgramData::kTables] = {};
bool table_place(u32 a, u32& out);
} // namespace

void set_table_addresses(const sim::exact::ProgramData& d) {
    for (int i = 0; i < sim::exact::ProgramData::kTables; ++i) {
        try {
            g_table_at[i] = d.address(sim::exact::ProgramData::Table(i), 0);
        } catch (const std::exception&) {
            g_table_at[i] = 0;
        }
    }
}

namespace {
int g_lenient = 0;
std::set<u32> g_missing;
} // namespace

LenientPlaces::LenientPlaces() {
    if (g_lenient++ == 0) g_missing.clear();
}
LenientPlaces::~LenientPlaces() { --g_lenient; }
std::string LenientPlaces::missing() const {
    std::string s;
    char b[16];
    for (u32 a : g_missing) std::snprintf(b, sizeof b, "%s%X", s.empty() ? "" : " ", a), s += b;
    return s;
}

namespace {
u32 g_later[sim::kRules][3] = {};
constexpr LaterPlace kLaterPlaceDefs[kLaterPlaces] = {
    {0x001E3BF0, 0x001F8980, 2, 8, 0x90},              // kPitExitLine6Return
    {later_fn(sim::Rule::PodLine), 0, 4, 0, 0xD0},     // kPodStopDriverCall (its fourth call: the stop driver)
    {0x001F7AE0, 0x001F2EA8, 1, 8, 0x1000},            // kDecideStopLapReturn
    {0x001F39D8, 0x001F59B0, 1, 8, 0x1400, true},      // kPassByReturn
};
u32 g_later_place[kLaterPlaces] = {};
} // namespace

void set_later_places(const sim::exact::ProgramData& d, ByteSpan program, u32 load) {
    for (auto& r : g_later) r[0] = r[1] = r[2] = 0;
    for (u32& a : g_later_place) a = 0;
    const bool later = sim::concept_program(d.program());
    if (later) {
        const u32 step = pa(0x001F39D8, d.program());
        for (int r = int(sim::Rule::GroundMiss) + 1; r < sim::kRules; ++r) {
            const u32 fn = d.rule_site(sim::Rule(r));
            if (!fn) continue;
            g_later[r][0] = fn;
            for (u32 a = fn, calls = 0; a < fn + 0x100 && a - load + 4 <= program.size(); a += 4)
                if (rd32(program, a - load) >> 26 == 3 && ++calls == 2) {
                    g_later[r][2] = a + 8;
                    break;
                }
            const u32 jal = 0x0C000000u | (fn >> 2 & 0x03FFFFFFu);
            for (u32 a = step; a < step + 0x1000 && a - load + 4 <= program.size(); a += 4)
                if (rd32(program, a - load) == jal) {
                    g_later[r][1] = a;
                    break;
                }
        }
    }
    // (after the rules' routines: a place may be inside one; target 0: any call; GT3's releases only the places of every
    // program)
    auto at = [&](u32 a) -> u32 {
        if (later_key(a)) return (a & 0xFF) == 0 && ((a >> 8) & 0xFF) < u32(sim::kRules) ? g_later[(a >> 8) & 0xFF][0] : 0;
        const Row* r = find(a);
        return r ? r->at[int(d.program())] : 0;
    };
    for (u32 i = 0; i < kLaterPlaces; ++i) {
        const LaterPlace& lp = kLaterPlaceDefs[i];
        if (!later && !lp.every) continue;
        const u32 fn = at(lp.fn), target = lp.target ? at(lp.target) : 0;
        if (!fn || (lp.target && !target)) continue;
        int n = 0;
        for (u32 a = fn; a < fn + lp.span && a - load + 4 <= program.size(); a += 4)
            if (const u32 w = rd32(program, a - load);
                (w >> 26 == 3 || w >> 26 == 2) && (!lp.target || (w & 0x03FFFFFFu) == (target >> 2 & 0x03FFFFFFu)) && ++n == lp.nth) {
                g_later_place[i] = a + u32(lp.add);
                break;
            }
    }
}

u32 pa(u32 a, Program p) {
    if (later_key(a)) {
        if ((a & 0x00FF0000u) == 0x00FF0000u) return p == program() && (a & 0xFFFF) < kLaterPlaces ? g_later_place[a & 0xFFFF] : 0;
        const u32 r = (a >> 8) & 0xFF;
        return p == program() && r < u32(sim::kRules) && (a & 0xFF) < 3 ? g_later[r][a & 0xFF] : 0;
    }
    if (!g_set) throw std::logic_error("program_map: pa(" + hex(a) + ") before the disc's program is known (a static table?)");
    if (p == Program::NtscU) return a;
    if (u32 t = 0; sim::concept_program(p) && p == program() && table_place(a, t)) return t;
    const Row* r = find(a);
    if (g_lenient && sim::concept_program(p) && (!r || !r->at[int(p)])) {
        g_missing.insert(a);
        return 0;
    }
    if (!r) throw std::runtime_error("program_map: " + hex(a) + " has no row in program_map.inc (tools/re/version_map.py)");
    if (!r->at[int(p)])
        throw std::runtime_error("program_map: " + hex(a) + " has no counterpart in the " + sim::exact::program_name(p) + " program");
    return r->at[int(p)];
}

u32 pa(u32 a) { return pa(a, program()); }

bool has_pa(u32 a) {
    if (later_key(a)) return pa(a) != 0;
    if (ntsc_u()) return true;
    if (u32 t = 0; sim::concept_program(program()) && table_place(a, t)) return true;
    const Row* r = find(a);
    return r && r->at[int(program())];
}

int conductor_slot(int s) {
    if (sim::concept_program(program())) return program() >= Program::ConceptSeoul2 && s >= 15 ? s + 2 : s;
    if (program() != Program::NtscJ || s < 4) return s;
    if (s == 4) throw std::runtime_error("program_map: the NTSC-J conductors have no virtual 4");
    return s - 1;
}

namespace {
constexpr u32 kCar0 = 0xC8, kCarSizeU = 0x16DC, kCarsEnd = kCar0 + 8 * kCarSizeU, kCarExtra = 0x1328;
constexpr u32 kVtableU = 0xE3A8, kVtableLater = 0xE47C, kAfterCarsLater = 0x60;
} // namespace

u32 car_stride() { return sim::concept_program(program()) ? kCarSizeU + 4 : kCarSizeU; }
u32 car_extra_word() { return sim::concept_program(program()) ? kCarExtra : 0; }
u32 car_off(u32 o) { return sim::concept_program(program()) && o >= kCarExtra ? o + 4 : o; }

u32 cond_off(u32 o) {
    if (!sim::concept_program(program()) || o < kCar0) return o;
    if (o < kCarsEnd) return kCar0 + (o - kCar0) / kCarSizeU * car_stride() + car_off((o - kCar0) % kCarSizeU);
    if (o >= kVtableU) return o - kVtableU + kVtableLater;
    return o + kAfterCarsLater;
}

u32 entry_off(u32 o) { return sim::concept_program(program()) && o >= 0x20DC ? o + 0x44 : o; }
u32 entry_size() { return sim::concept_program(program()) ? 0x2400 : 0x2200; }
u32 holder_off(u32 o) { return program() >= Program::ConceptSeoul2 && o >= 0x18 ? o + 8 : o; }

int race_step_reg(int r) {
    if (sim::concept_program(program())) throw std::runtime_error("program_map: the race step's registers are not known on a later build");
    if (program() != Program::NtscJ) return r;
    switch (r) {
    case 17: return 19; // the step runs
    case 19: return 17; // the car index
    case 20: return 18; // the conductor
    default: break;
    }
    throw std::runtime_error("program_map: race step register " + std::to_string(r) + " not known on NTSC-J");
}

s32 ntsc_u_event(s32 id) {
    if (sim::concept_program(program())) return id >= 12 ? id - 1 : id == 11 ? sim::exact::kStopsMissedEvent : id; // (their 11: the missed stops')
    return program() == Program::NtscJ && id >= 14 ? id + 1 : id;
}
u32 queue_count_off() { return sim::concept_program(program()) ? 0x81C : 0x818; }

namespace {
using T = sim::exact::ProgramData::Table;
// The tables' NTSC-U starts, in Table's order.
constexpr u32 kTableStarts[sim::exact::ProgramData::kTables] = {
    0x002D28B0, 0x002D35B8, 0x002D4628, 0x002D4948, 0x002D53D8, 0x002D9A08, 0x002DA508, 0x002DA030, 0x003529C0, 0x002EC550,
    0x00352900, 0x00352910, 0x0033C368, 0x00352968, 0x002D32F0, 0x002D75F0, 0x003416F8, 0x003306D0, 0x00330730, 0x002D3510};
} // namespace

u32 ntsc_u_table(T t) { return kTableStarts[size_t(t)]; }

namespace {
bool table_place(u32 a, u32& out) {
    for (int i = 0; i < sim::exact::ProgramData::kTables; ++i)
        if (a >= kTableStarts[i] && a - kTableStarts[i] < sim::exact::ProgramData::size(T(i)) && g_table_at[i]) {
            out = g_table_at[i] + (a - kTableStarts[i]);
            return true;
        }
    return false;
}
} // namespace

TableByte table_byte(u32 a) {
    for (int i = 0; i < sim::exact::ProgramData::kTables; ++i) {
        const T t = T(i);
        if (a >= kTableStarts[i] && a - kTableStarts[i] < sim::exact::ProgramData::size(t)) return {t, a - kTableStarts[i]};
    }
    char b[64];
    std::snprintf(b, sizeof b, "0x%X is not a program table byte", a);
    throw std::out_of_range(b);
}

void poke_table(sim::exact::ProgramData& d, u32 a, u8 v) {
    const TableByte t = table_byte(a);
    d.poke(t.table, t.off, v);
}

u32 ntsc_u_address(u32 x) {
    if (ntsc_u()) return x;
    for (const Row& r : kRows)
        if (r.at[int(program())] == x) return r.at[0];
    return x;
}

} // namespace gt3r::diff
