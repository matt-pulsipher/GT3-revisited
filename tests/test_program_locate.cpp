// ProgramData's locator (src/sim/exact/program_data.h, program_locate.h) on the disc's own program (any region's,
// GT3R_DISC): every table found through its routine and every rule's routine recognised (with each table's content hash, to
// compare builds); a routine moved elsewhere, a table
// moved and its address's instructions changed, both still found; a damaged routine's table found by its content; a table
// neither finds reported as missing; a damaged rule routine falling back to NTSC-U's rules. The edits are made on a copy of
// the image in memory.

#include "test_framework.h"

#include "disc/core_image.h"
#include "disc/disc.h"
#include "disc/region.h"
#include "formats/db/gt_db.h"
#include "game/data/car_database.h"
#include "game/gt_mode.h"
#include "race/exact_setup.h"
#include "sim/exact/program_tables.h"
#include "vfs/vfs.h"
#include "sim/exact/program_data.h"
#include "sim/exact/program_locate.h"
#include "sim/exact/special.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>

using namespace gt3r;
namespace ex = gt3r::sim::exact;
using T = ex::ProgramData::Table;

namespace {

std::optional<CoreImage> disc_core() {
    Disc* disc = test::any_region_disc();
    if (!disc) return std::nullopt;
    return CoreImage::decode(disc->read_core_gt3());
}

ex::ProgramData load(const CoreImage& c) { return ex::ProgramData::load(c); }

u32 rd(const CoreImage& c, u32 a) { return rd32(ByteSpan(c.program.data(), c.program.size()), a - c.load_address); }
void wr(CoreImage& c, u32 a, u32 v) {
    for (u32 k = 0; k < 4; ++k) c.program[a - c.load_address + k] = u8(v >> (8 * k));
}

u32 content_hash(const ex::ProgramData& d, T t) {
    u32 h = 2166136261u;
    for (u32 k = 0; k < ex::ProgramData::size(t); ++k) h = (h ^ d.b(t, k)) * 16777619u;
    return h;
}

bool has_issue(const ex::ProgramData& d, const std::string& what) {
    return std::any_of(d.issues().begin(), d.issues().end(), [&](const std::string& s) { return s.find(what) != std::string::npos; });
}

// The code ranges ProgramData recognised (each table's window, each rule's routine start with a margin).
std::vector<std::pair<u32, u32>> sites(const ex::ProgramData& d) {
    std::vector<std::pair<u32, u32>> v;
    for (int i = 0; i < ex::ProgramData::kTables; ++i) {
        const ex::ProgramData::Site s = d.table_site(T(i));
        if (s.at) v.push_back({s.at, s.at + 4 * s.words});
    }
    for (int r = 0; r < ex::kRules; ++r)
        if (const u32 a = d.rule_site(ex::Rule(r))) v.push_back({a, a + 0x800});
    return v;
}
bool clear_of(const std::vector<std::pair<u32, u32>>& v, u32 a, u32 b) {
    return std::none_of(v.begin(), v.end(), [&](const std::pair<u32, u32>& s) { return a < s.second && s.first < b; });
}

} // namespace

TEST(program_locate_disc) {
    const std::optional<CoreImage> c = disc_core();
    if (!c) SKIP("needs GT3R_DISC");
    const ex::ProgramData d = load(*c);
    const Region region = test::any_region_disc()->info().region;
    const ex::Program want = region == Region::Europe ? ex::Program::Pal : region == Region::Japan ? ex::Program::NtscJ : ex::Program::NtscU;
    std::printf("  %s program, %s\n", ex::program_name(d.program()), d.known() ? "known" : "not known");
    for (const std::string& s : d.issues()) std::printf("  issue: %s\n", s.c_str());
    CHECK(d.known() && d.issues().empty() && d.program() == want);
    // every table of the disc equals GT3's values of its release (PAL's HUD messages its own)
    for (int i = 0; i < ex::ProgramData::kTables; ++i) {
        const T t = T(i);
        CHECK(d.table_matched(t) && d.table_release(t) == (t == T::Messages && want == ex::Program::Pal ? want : ex::Program::NtscU));
        const ByteSpan g = ex::gt3_table(t, d.table_release(t));
        for (u32 k = 0; k < ex::ProgramData::size(t); ++k) CHECK(d.b(t, k) == g[k]);
    }
    for (int i = 0; i < ex::ProgramData::kTables; ++i) {
        const T t = T(i);
        std::printf("  tier %d at 0x%06X (content %08X): %s\n", d.tier(t), d.table_start(t), d.tier(t) ? content_hash(d, t) : 0,
                    ex::ProgramData::what(t));
        CHECK(d.tier(t) == 1);
    }
    for (int r = 0; r < ex::kRules; ++r) CHECK(d.recognised(ex::Rule(r)));
    CHECK(d.behaviour() == (ex::Behaviour{want, 60}) && d.behaviour().follows_rules());
}

// A table's routine moved to another place in the code (its window copied over unrelated code, the original cleared), and
// the table itself moved to a clear place in the data with the instructions that form its address changed to the new place.
TEST(program_locate_relocated) {
    std::optional<CoreImage> c = disc_core();
    if (!c) SKIP("needs GT3R_DISC");
    const ex::ProgramData d = load(*c);
    const T name = T::Messages;
    const u32 size = ex::ProgramData::size(name);
    const ex::ProgramData::Site s = d.table_site(name);
    const u32 table = d.table_start(name);
    CHECK(s.at && s.hi == 0 && table);
    if (!s.at) return;
    // The routine: to the first place well clear of every recognised routine.
    const std::vector<std::pair<u32, u32>> busy = sites(d);
    const ex::CodeIndex x(ByteSpan(c->program.data(), c->program.size()), c->load_address, c->entry_point);
    u32 to = 0;
    for (u32 a = c->load_address + 0x10000; a + 0x400 < x.text_end() && !to; a += 0x1000)
        if (clear_of(busy, a - 0x100, a + 4 * s.words + 0x100) && rd(*c, a - 4) == 0x03E00008) to = a + 4; // after a return's delay
    CHECK(to != 0);
    CoreImage moved = *c;
    for (u32 k = 0; k < s.words; ++k) wr(moved, to + 4 * k, rd(*c, s.at + 4 * k)), wr(moved, s.at + 4 * k, 0);
    const ex::ProgramData m = load(moved);
    CHECK(m.tier(name) == 1 && m.table_site(name).at == to && m.table_start(name) == table && m.table_matched(name));
    // The table: to a clear stretch of the data, the routine's address instructions pointing there.
    u32 dest = 0;
    for (u32 a = (x.text_end() + 0x1000) & ~0xFu; x.in_image(a, 2 * size) && !dest; a += 0x10) {
        bool zero = true;
        for (u32 k = 0; k < 2 * size && zero; k += 4) zero = rd(*c, a + k) == 0;
        if (zero) dest = a + size / 2;
    }
    CHECK(dest != 0);
    CoreImage shifted = *c;
    for (u32 k = 0; k < size; ++k) shifted.program[dest - c->load_address + k] = c->program[table - c->load_address + k];
    for (u32 k = 0; k < size; ++k) shifted.program[table - c->load_address + k] = 0;
    const u32 hi_at = s.at + 4 * s.hi, lo_at = s.at + 4 * s.lo;
    wr(shifted, hi_at, (rd(*c, hi_at) & 0xFFFF0000) | ((dest + 0x8000) >> 16));
    wr(shifted, lo_at, (rd(*c, lo_at) & 0xFFFF0000) | (dest & 0xFFFF));
    const ex::ProgramData t = load(shifted);
    CHECK(t.tier(name) == 1 && t.table_start(name) == dest && t.table_matched(name));
    CHECK(t.w(name, 8 * 6 + 4) == d.w(name, 8 * 6 + 4)); // the same bytes from the new place
}

// A damaged routine: its table found by its content (tier 2) and reported; a table neither finds: reported, GT3's values
// serve it.
TEST(program_locate_damaged) {
    std::optional<CoreImage> c = disc_core();
    if (!c) SKIP("needs GT3R_DISC");
    const ex::ProgramData d = load(*c);
    const ex::ProgramData::Site s = d.table_site(T::Messages);
    CHECK(s.at && s.words > 4);
    if (!s.at) return;
    CoreImage bad = *c;
    const u32 at = s.at + 4 * (s.lo + 1);
    wr(bad, at, rd(*c, at) ^ 0x00010000); // another register in the instruction after the address
    const ex::ProgramData b = load(bad);
    CHECK(b.tier(T::Messages) == 2 && b.table_start(T::Messages) == d.table_start(T::Messages));
    CHECK(b.table_matched(T::Messages) && has_issue(b, "the HUD messages") && has_issue(b, "found by its content"));
    // The static block: its routine damaged and its rolling curve's count cleared.
    const ex::ProgramData::Site st = d.table_site(T::Static);
    CHECK(st.at != 0);
    if (!st.at) return;
    const u32 sat = st.at + 4 * (st.lo + 1);
    wr(bad, sat, rd(*c, sat) ^ 0x00010000);
    bad.program[d.table_start(T::Static) + 0x0B - c->load_address] = 0;
    bool threw = false;
    ex::ProgramData m;
    try {
        m = load(bad);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(!threw && m.tier(T::Static) == 0 && !m.table_matched(T::Static));
    CHECK(has_issue(m, "the static physics block: not found on this disc; GT3's values"));
    CHECK(m.b(T::Static, 0x0B) == d.b(T::Static, 0x0B) && m.b(T::StickCurve, 255) == d.b(T::StickCurve, 255)); // GT3's values
}

// A table GT3's code places with values of its own (a modified game's tuning): the disc's values serve, values only the
// shape check refuses included (no slipstream: the divisor 0; a one-point rolling curve), and the program stays known with
// the table changed (an issue). gt3_tables() gives GT3's values back (the stock fingerprint), which the races every machine
// shares play on (record_program_data). Past what the engine takes (nine rolling points) the table is not found and GT3's
// values serve.
TEST(program_locate_changed) {
    std::optional<CoreImage> c = disc_core();
    if (!c) SKIP("needs GT3R_DISC");
    const ex::ProgramData d = load(*c);
    const u32 at = d.table_start(T::Static);
    CHECK(at != 0 && d.tier(T::Static) == 1 && !d.changed());
    if (!at) return;
    CoreImage mod = *c;
    mod.program[at - c->load_address] = 0;        // +0x00 the slipstream shares' divisor: none
    mod.program[at + 0x0B - c->load_address] = 1; // one rolling-resistance point
    mod.program[at + 0x1D - c->load_address] = 1; // the kerb's bump amplitude: 1 mm
    const ex::ProgramData m = load(mod);
    CHECK(m.tier(T::Static) == 1 && m.table_changed(T::Static) && !m.table_matched(T::Static) && m.changed());
    CHECK(m.known() && m.program() == d.program() && has_issue(m, "the static physics block: changed on this disc"));
    CHECK(m.b(T::Static, 0) == 0 && m.b(T::Static, 0x0B) == 1 && m.b(T::Static, 0x1D) == 1);
    CHECK(m.b(T::Static, 0x1C) == d.b(T::Static, 0x1C) && m.fingerprint() != d.fingerprint());
    const ex::ProgramData g = m.gt3_tables();
    CHECK(!g.changed() && g.fingerprint() == d.fingerprint() && g.b(T::Static, 0x1D) == d.b(T::Static, 0x1D));
    CHECK(d.gt3_tables().fingerprint() == d.fingerprint());
    const auto own = std::make_shared<const ex::ProgramData>(m), stock = std::make_shared<const ex::ProgramData>(d);
    CHECK(race::record_program_data(own, m.fingerprint()) == own);
    const auto shared = race::record_program_data(own, d.fingerprint());
    CHECK(shared && !shared->changed() && shared->fingerprint() == d.fingerprint());
    CHECK(!race::record_program_data(stock, m.fingerprint()) && race::record_program_data(stock, d.fingerprint()) == stock);
    mod.program[at + 0x0B - c->load_address] = 9;
    const ex::ProgramData n = load(mod);
    CHECK(n.tier(T::Static) == 0 && !n.table_changed(T::Static) && !n.changed() && !n.known());
    CHECK(has_issue(n, "the static physics block: not found on this disc; GT3's values") && n.b(T::Static, 0x0B) == d.b(T::Static, 0x0B));
}

// A rule's routine not any known release's: that routine keeps its program's release's rules, reported; the rest stay the
// disc's.
TEST(program_locate_unknown_rule) {
    std::optional<CoreImage> c = disc_core();
    if (!c) SKIP("needs GT3R_DISC");
    const ex::ProgramData d = load(*c);
    const u32 a = d.rule_site(ex::Rule::LicenceStop);
    CHECK(a != 0);
    if (!a) return;
    CoreImage bad = *c;
    wr(bad, a + 8, rd(*c, a + 8) ^ 0x00010000);
    const ex::ProgramData b = load(bad);
    CHECK(!b.recognised(ex::Rule::LicenceStop) && b.behaviour().rule(ex::Rule::LicenceStop) == d.program());
    CHECK(has_issue(b, std::string("the licence test's stop: not a known release's code; ") + ex::program_name(d.program()) + "'s rules"));
    // Online races follow NTSC-U's rules, that routine too.
    ex::ProgramData online = b;
    online.set_behaviour({ex::Program::NtscU, 60});
    CHECK(online.behaviour().rule(ex::Rule::LicenceStop) == ex::Program::NtscU);
    CHECK(b.program() == d.program() && b.behaviour().rule(ex::Rule::LapRecord) == d.program());
}

// Other builds of the engine (GT3R_DISC_CONCEPT: disc images separated by ';', e.g. GT Concept): the disc opens, the program
// decodes and ProgramData loads without an exception; each table's tier and each rule are reported; a later build of the
// engine (Gran Turismo Concept) is known and races on its own physics (race_physics), its changed tables the disc's. Whether the
// disc's car database and course files can be read is reported (a disc whose data cannot build a race is reported, not
// chased).
TEST(program_locate_other_builds) {
    const char* list = std::getenv("GT3R_DISC_CONCEPT");
    if (!list || !*list) SKIP("needs GT3R_DISC_CONCEPT");
    std::istringstream in(list);
    for (std::string path; std::getline(in, path, ';');) {
        if (path.empty()) continue;
        std::printf("  %s\n", path.c_str());
        bool threw = false;
        try {
            std::unique_ptr<Disc> disc = Disc::open(path);
            const ex::ProgramData d = load(CoreImage::decode(disc->read_core_gt3()));
            std::printf("    %s program, %s\n", ex::program_name(d.program()), d.known() ? "known" : "not known (GT3's core)");
            // A later build of the engine is told by its own code: every table through the routine that addresses it, the
            // routines whose changes are ported recognised; it races on GT3's core (NTSC-U's rules) until its own is
            // complete, and its own rules are there for the asking.
            if (ex::concept_program(d.program())) {
                for (int i = 0; i < ex::ProgramData::kTables; ++i) CHECK(d.tier(T(i)) == 1);
                for (const ex::Rule r : {ex::Rule::Atan2, ex::Rule::FloatToDouble, ex::Rule::EngineCondition, ex::Rule::SlipAngle,
                                         ex::Rule::SpecBuild})
                    CHECK(d.recognised(r));
                // Every later build races on its own physics (its whole race step is proven on the disc): the disc reports
                // its own rules, every routine recognized (at 60 steps a second, as a PAL disc of GT3; 2002 Tokyo-Geneva
                // Europe's own 50 is its release behaviour's).
                if (ex::concept_program(d.program())) {
                    CHECK(d.known());
                    for (const std::string& issue : d.issues()) CHECK(issue.find("changed on this disc") != std::string::npos);
                    CHECK(d.behaviour().rules == d.program() && d.behaviour().rate == 60);
                    CHECK(ex::release_behaviour(d.program()).rate == (d.program() == ex::Program::ConceptGenevaEu ? 50 : 60));
                    CHECK(!ex::gt3_program(d.behaviour().rule(ex::Rule::AxleStep)) && d.behaviour().rule(ex::Rule::LapTime) == ex::Program::Pal);
                    for (int r = 0; r < ex::kRules; ++r) CHECK(d.recognised(ex::Rule(r)));
                    CHECK(race::race_physics(d).own);
                }
                ex::ProgramData own = d;
                const ex::Behaviour release = ex::release_behaviour(d.program());
                CHECK(own.can_behave(release) && own.can_behave({d.program(), 60}));
                own.set_behaviour({d.program(), 60});
                CHECK(own.behaviour().rule(ex::Rule::Atan2) == d.program() && own.behaviour().rule(ex::Rule::SpecBuild) == d.program());
                // The later builds' own routines and tables (special.h, the AWD system 3 settings): found on the disc.
                for (const ex::Rule r : {ex::Rule::GearSpec, ex::Rule::Layout, ex::Rule::Driveline, ex::Rule::AwdGain, ex::Rule::DriveClass,
                                         ex::Rule::HybridStep, ex::Rule::PodMood, ex::Rule::PodStop, ex::Rule::ActiveWing,
                                         ex::Rule::StopZone, ex::Rule::PodRule, ex::Rule::PodLine, ex::Rule::PodSpring,
                                         ex::Rule::HitReport, ex::Rule::LicenceJudge})
                    CHECK(d.recognised(r) && own.behaviour().rule(r) == d.program());
                // (the pass-by states: NTSC-U's routine, 2002 Tokyo-Geneva Europe's PAL's 50 Hz counts; the pod race's HUD
                // messages beside GT3's, display time 0)
                CHECK(d.recognised(ex::Rule::PassBy) &&
                      own.behaviour().rule(ex::Rule::PassBy) == (d.program() == ex::Program::ConceptGenevaEu ? ex::Program::Pal : ex::Program::NtscU));
                CHECK(d.has(T::PodMessages) && d.tier(T::PodMessages) == 2 && d.table_start(T::PodMessages) == d.table_start(T::Messages) + 0x80);
                CHECK(d.w(T::PodMessages, 0) == 0x10 && d.w(T::PodMessages, 8) == 0x11 && d.w(T::PodMessages, 4) == 0 && d.w(T::PodMessages, 12) == 0);
                for (const T t : {T::SpecialCars, T::Awd3, T::PodMoods, T::Stepless, T::RaceSwitches}) CHECK(d.has(t) && d.tier(t) == 1);
                // (2002 Tokyo-Geneva's special kind 7 ride settings: only those two builds have them, switched on)
                if (d.program() >= ex::Program::ConceptGenevaEu) CHECK(d.has(T::Kind7Ride) && d.tier(T::Kind7Ride) == 1 && d.b(T::Kind7Ride, 0) == 1);
                else CHECK(!d.has(T::Kind7Ride));
                // (the single race's flying start grid: only the builds before 2002 Tokyo-Geneva, whose conductor is GT3's again;
                // its lines 0 .. 7, its steps and speed steps set; the layout 7 system's settings on every build, both ratings set)
                const bool grid = d.program() <= ex::Program::ConceptSeoul2;
                CHECK(d.has(T::FlyingStart) == grid);
                CHECK(ex::gt3_program(own.behaviour().rule(ex::Rule::FlyingGrid)) == !grid && d.recognised(ex::Rule::FlyingGrid));
                if (grid) {
                    CHECK(d.tier(T::FlyingStart) == 1);
                    for (u32 k = 0; k < 6; ++k) CHECK(d.b(T::FlyingStart, k) <= 7);
                    for (u32 k = 0; k < 5; ++k) CHECK(d.b(T::FlyingStart, 8 + k) != 0 && d.b(T::FlyingStart, 0x10 + k) != 0);
                }
                CHECK(d.has(T::Hybrid) && d.tier(T::Hybrid) == 1 && d.b(T::Hybrid, 0) != 0 && d.b(T::Hybrid, 1) != 0);
                CHECK(d.recognised(ex::Rule::StuckNudge) && own.behaviour().rule(ex::Rule::StuckNudge) == d.program());
                // (the routines of the race step ported in the later passes, each by its own code on every build)
                for (const ex::Rule r : {ex::Rule::AxleStep, ex::Rule::PadClutch, ex::Rule::PadControls, ex::Rule::DriverInput,
                                         ex::Rule::AiOutputs, ex::Rule::AiDecide, ex::Rule::AiTraffic, ex::Rule::AiBraking, ex::Rule::Ascc,
                                         ex::Rule::TyreEffects, ex::Rule::GapClosing, ex::Rule::ContactIntensity, ex::Rule::SlowDown,
                                         ex::Rule::ParamsBuild})
                    CHECK(d.recognised(r) && own.behaviour().rule(r) == d.program());
                // (the stop zone: 608 m on, 33 m long; the ASCC's three switches set; the stepless rates)
                CHECK(d.h(T::RaceSwitches, 0) == 608 && d.b(T::RaceSwitches, 2) == 33);
                CHECK(d.b(T::RaceSwitches, 3) == 1 && d.b(T::RaceSwitches, 4) == 1 && d.b(T::RaceSwitches, 5) == 1);
                CHECK(d.b(T::Stepless, 0) > d.b(T::Stepless, 1) && d.b(T::Stepless, 0xC) != 0 && d.b(T::Stepless, 0xD) != 0);
                // (the special cars: kind 1 and kind 2 by their labels in the program, no other car)
                {
                    const sim::exact::MoodTables moods = sim::exact::mood_tables(d);
                    CHECK(moods.time[1] != 0 && moods.lamp[1] != moods.lamp[0]);
                    int kinds[3] = {};
                    data::CarDatabase cars;
                    if (cars.load(disc->volume()))
                        for (const data::Car& c : cars.cars())
                            if (const u8 k = sim::exact::special_kind(own, db::id_hash(c.label)); k == 1 || k == 2) ++kinds[k];
                    CHECK(kinds[1] == 1 && kinds[2] == 1);
                }
                // (the routines of GT3's releases' rules, each as the build has it: PAL's rubber band cap and ghost rules,
                // NTSC-U's licence stop but for 2002 Tokyo-Geneva's own constants)
                CHECK(own.behaviour().rule(ex::Rule::RubberBand) == ex::Program::Pal && own.behaviour().rule(ex::Rule::BattleCap) == ex::Program::Pal);
                CHECK(own.behaviour().rule(ex::Rule::SectorTime) == ex::Program::Pal && own.behaviour().rule(ex::Rule::LapUsable) == ex::Program::NtscU);
                CHECK(own.behaviour().rule(ex::Rule::LicenceStop) ==
                      (d.program() >= ex::Program::ConceptGenevaEu ? d.program() : ex::Program::NtscU));
                // Online races and GT3's demos: NTSC-U's rules on GT3's tables.
                ex::ProgramData online = d.gt3_tables();
                online.set_behaviour({ex::Program::NtscU, 60});
                CHECK(!online.changed() && online.behaviour().rule(ex::Rule::Atan2) == ex::Program::NtscU);
            }
            // GT3's releases have none of the later builds' tables or routines (their lack is how they are told).
            if (ex::gt3_program(d.program())) {
                for (const T t : {T::SpecialCars, T::Awd3, T::PodMoods, T::Stepless, T::RaceSwitches, T::Kind7Ride, T::FlyingStart, T::Hybrid,
                                  T::PodMessages})
                    CHECK(!d.has(t));
                for (const ex::Rule r : {ex::Rule::HitReport, ex::Rule::PassBy, ex::Rule::LicenceJudge})
                    CHECK(d.recognised(r) && d.behaviour().rule(r) == d.program());
                CHECK(d.recognised(ex::Rule::StuckNudge) && ex::gt3_program(d.behaviour().rule(ex::Rule::StuckNudge)));
                for (const ex::Rule r : {ex::Rule::AxleStep, ex::Rule::PadControls, ex::Rule::AiDecide, ex::Rule::AiTraffic, ex::Rule::Ascc,
                                         ex::Rule::SlowDown, ex::Rule::ParamsBuild})
                    CHECK(d.recognised(r) && ex::gt3_program(d.behaviour().rule(r)));
                for (const ex::Rule r : {ex::Rule::HybridStep, ex::Rule::PodMood, ex::Rule::PodStop, ex::Rule::ActiveWing, ex::Rule::FlyingGrid,
                                         ex::Rule::StopZone, ex::Rule::PodRule, ex::Rule::PodLine, ex::Rule::PodSpring})
                    CHECK(d.rule_site(r) == 0 && d.behaviour().rule(r) == d.program());
            }
            int tiers[3] = {}, matched = 0;
            for (int i = 0; i < ex::ProgramData::kTables; ++i) {
                const T t = T(i);
                ++tiers[d.tier(t)];
                matched += d.table_matched(t);
                std::string values = d.table_matched(t) ? ", GT3's values" : d.table_changed(t) ? ", changed" : "";
                if (d.table_matched(t) && ex::gt3_table_differs(t, ex::Program::Pal)) // which release's
                    values += std::string(" (") + ex::program_name(d.table_release(t)) + "'s)";
                std::printf("    table tier %d%s: %s\n", d.tier(t), values.c_str(), ex::ProgramData::what(t));
                CHECK(d.table_matched(t) || has_issue(d, std::string(ex::ProgramData::what(t)) + ":"));
                const ByteSpan g = ex::gt3_table(t, d.table_release(t));
                CHECK(d.table_changed(t) || d.b(t, 0) == g[0]); // GT3's values serve (but where GT3's code places a changed one)
            }
            int rules = 0;
            for (int r = 0; r < ex::kRules; ++r) rules += d.recognised(ex::Rule(r));
            std::printf("    tables: %d by their routine, %d by content, %d not found; %d with GT3's values; rules recognised: %d of %d\n",
                        tiers[1], tiers[2], tiers[0], matched, rules, ex::kRules);
            Vfs vfs(&disc->volume());
            std::string err;
            const auto pd = race::exact_program_data(vfs, &err);
            CHECK(pd != nullptr);
            if (pd) {
                const race::RacePhysics how = race::race_physics(*pd);
                std::printf("    the game: races on %s physics%s\n", race::race_core_name(how.core), how.own ? "" : " (not its own)");
                CHECK(how.own == d.known());
            }
            // The disc's data a race is built from.
            data::CarDatabase db;
            const bool db_ok = db.load(disc->volume());
            std::printf("    car database: %s\n", db_ok ? "read" : ("not readable: " + db.error()).c_str());
            if (db_ok && pd && !db.cars().empty()) {
                std::string why;
                const std::optional<ex::Equipment> eq = race::exact_equipment(db, race::ExactCarSource(db.cars()[0].label), &why);
                std::printf("    a car's equipment (%s): %s\n", db.cars()[0].label.c_str(), eq ? "built" : why.c_str());
            }
            int courses = 0, read = 0;
            std::string course_error;
            for (const Volume::Entry& e : disc->volume().entries()) {
                if (e.path.rfind("courses/", 0) != 0 || !pd) continue;
                if (e.path.find('.') != std::string::npos) continue; // course files have no extension
                ++courses;
                try {
                    const Bytes file = disc->volume().read(e);
                    (void)ex::RaceCourse::load(ByteSpan(file.data(), file.size()), *pd);
                    ++read;
                } catch (const std::exception& x) {
                    if (course_error.empty()) course_error = e.path + ": " + x.what();
                }
            }
            std::printf("    course files: %d of %d read by the exact core%s%s\n", read, courses, course_error.empty() ? "" : "; ",
                        course_error.c_str());
            // The data GT mode needs (its database, game config, colours, menus).
            game::GtData gt;
            const bool gt_ok = gt.load([&](const std::string& p) { return vfs.read(p); });
            std::printf("    GT mode data: %s\n", gt_ok ? "read" : ("not readable: " + gt.error()).c_str());
        } catch (const std::exception& e) {
            threw = true;
            std::printf("    exception: %s\n", e.what());
        }
        CHECK(!threw);
    }
}
