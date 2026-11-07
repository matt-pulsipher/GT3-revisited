// The program's data tables from CORE.GT3; see program_data.h.

#include "sim/exact/program_data.h"

#include "base/log.h"
#include "disc/core_image.h"
#include "sim/exact/program_locate.h"
#include "sim/exact/program_tables.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace gt3r::sim {

const char* program_name(Program p) {
    switch (p) {
    case Program::Pal: return "PAL";
    case Program::NtscJ: return "NTSC-J";
    case Program::NtscU: break;
    }
    return "NTSC-U";
}

const char* rule_name(Rule r) {
    switch (r) {
    case Rule::RubberBand: return "the rubber band";
    case Rule::BattleCap: return "the i.LINK battle's catch-up cap";
    case Rule::Oversteer: return "the AI's oversteer indicator";
    case Rule::PadRumble: return "the pad vibration";
    case Rule::SteerForce: return "the steering-wheel force";
    case Rule::LicenceStop: return "the licence test's stop";
    case Rule::LapRecord: return "the lap record";
    case Rule::SpeedEvent: return "the machine test's top speed";
    case Rule::RecordsLocked: return "the records' lock";
    case Rule::LapTime: return "the lap time";
    case Rule::SectorTime: return "the sector time";
    case Rule::SectorDiff: return "the sector difference";
    case Rule::LapDiff: return "the lap difference";
    case Rule::AngleWrap: return "the angle wrap";
    case Rule::RunUp: return "the run-up start";
    case Rule::BattleStart: return "the battle's start";
    case Rule::CatchupAnswer: return "the battle's catch-up setting";
    case Rule::WearAnswer: return "the battle's tyre wear setting";
    case Rule::DrawMode: return "the car's draw mode";
    case Rule::LapStartRestore: return "the replay's lap-start restore";
    case Rule::CollisionCommit: return "the collision commit";
    case Rule::WallFriction: return "the wall friction";
    case Rule::PushSegment: return "the wall push-out";
    case Rule::LapUsable: return "the free run's lap check";
    case Rule::Count: break;
    }
    return "?";
}

} // namespace gt3r::sim

namespace gt3r::sim::exact {

namespace {

constexpr u8 kAll = 7; // NTSC-U, PAL and NTSC-J (a bit per Program)

// ---- the tables' content checks: what a table's shape and values look like in GT3, for finding one by its content (tier 2;
// no values of any program). A table GT3's code places only has to pass its limit below. ----------------------------------

bool rising(ByteSpan b, size_t o, size_t n, bool strict) {
    for (size_t k = 1; k < n; ++k)
        if (strict ? b[o + k] <= b[o + k - 1] : b[o + k] < b[o + k - 1]) return false;
    return true;
}
u32 word_at(ByteSpan b, size_t o) { return rd32(b, o); }
float float_at(ByteSpan b, size_t o) {
    const u32 w = rd32(b, o);
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}
bool any_set(ByteSpan b) {
    return std::any_of(b.begin(), b.end(), [](u8 c) { return c != 0; });
}
// A curve of 6 rising x bytes from 0 to 255, two zero bytes after them.
bool byte_curve(ByteSpan b, size_t o) { return b[o] == 0 && b[o + 5] == 0xFF && rising(b, o, 6, true) && b[o + 6] == 0 && b[o + 7] == 0; }

// The static physics block: the rolling curve's point count (+0x0B, 2 .. 8), its x bytes (+0x0C) rising, its y bytes
// (+0x14) not falling, the drag shares' divisor (+0) set.
bool check_static(ByteSpan b) {
    const u8 n = b[0x0B];
    return b[0] != 0 && n >= 2 && n <= 8 && rising(b, 0x0C, n, true) && rising(b, 0x14, n, false);
}
// The tyre effect block: 12 records of 0x28 bytes, each three curves' point counts (+1 .. +3, 1 .. 4) and their x bytes
// (+4, +0xC, +0x14) not falling.
bool check_effects(ByteSpan b) {
    for (size_t r = 0; r < 12; ++r) {
        const size_t o = 0x28 * r;
        for (size_t k = 1; k <= 3; ++k)
            if (b[o + k] < 1 || b[o + k] > 4) return false;
        for (size_t g = 0; g < 3; ++g)
            if (!rising(b, o + 4 + 8 * g, 4, false)) return false;
    }
    return true;
}
bool check_controller(ByteSpan b) { return b[0] && b[1] && b[2] && b[3]; } // the first four settings set
bool check_classes(ByteSpan b) {                                            // a class 0 .. 2 per surface
    return any_set(b) && std::all_of(b.begin(), b.end(), [](u8 c) { return c <= 2; });
}
bool check_soft(ByteSpan b) { // a word 0 / 1 per material
    for (size_t o = 0; o < b.size(); o += 4)
        if (word_at(b, o) > 1) return false;
    return any_set(b);
}
bool check_driver_sets(ByteSpan b) { // three 32-byte sets: their first three percentages 1 .. 100, their last 12 bytes clear
    for (size_t s = 0; s < 3; ++s) {
        for (size_t k = 0; k < 3; ++k)
            if (b[32 * s + k] < 1 || b[32 * s + k] > 100) return false;
        if (any_set(b.subspan(32 * s + 0x14, 12))) return false;
    }
    return true;
}
bool check_line_fallback(ByteSpan b) { // a row of 6 lines (0 .. 7, -1 ending it) per line k, starting with k or -1
    for (size_t k = 0; k < 8; ++k) {
        bool end = false;
        for (size_t j = 0; j < 6; ++j) {
            const u8 c = b[6 * k + j];
            if (c > 7 && c != 0xFF) return false;
            if (j < 4 && c == 0xFF) end = true;
        }
        if ((b[6 * k] != k && b[6 * k] != 0xFF) || !end) return false;
    }
    return true;
}
bool check_initial_lines(ByteSpan b) { // three different lines 0 .. 7
    const u32 a = word_at(b, 0), c = word_at(b, 4), d = word_at(b, 8);
    return a <= 7 && c <= 7 && d <= 7 && a != c && a != d && c != d;
}
bool check_line_order(ByteSpan b) { // the lines 0 .. 7 once each
    u32 seen = 0;
    for (u8 c : b)
        if (c <= 7) seen |= 1u << c;
    return seen == 0xFF;
}
bool check_gear_kinds(ByteSpan b) { // a byte-sized kind per gear record
    for (size_t o = 0; o < b.size(); o += 4)
        if (word_at(b, o) > 0xFF) return false;
    return any_set(b);
}
bool check_spec_curves(ByteSpan b) { return byte_curve(b, 0) && byte_curve(b, 0x10); }
bool check_curve_b(ByteSpan b) { return byte_curve(b, 0); }
bool check_lsd_types(ByteSpan b) { // 8 different lower-case letters
    u32 seen = 0;
    for (u8 c : b) {
        if (c < 'a' || c > 'z' || (seen >> (c - 'a') & 1)) return false;
        seen |= 1u << (c - 'a');
    }
    return true;
}
bool check_messages(ByteSpan b) { // 16 records: the message's number, its display steps
    for (u32 k = 0; k < 16; ++k)
        if (word_at(b, 8 * k) != k || word_at(b, 8 * k + 4) > 0xFFFF) return false;
    return true;
}
bool check_machine_sectors(ByteSpan b) { // three rising distances, then a negative end
    const float a = float_at(b, 0), c = float_at(b, 4), d = float_at(b, 8), e = float_at(b, 12);
    return std::isfinite(a) && std::isfinite(c) && std::isfinite(d) && std::isfinite(e) && a > 0 && a < c && c < d && d < 1e6f && e < 0;
}
bool check_stick_curve(ByteSpan b) { // 256 bytes not falling from 0 to 255, symmetric about the centre
    if (b[0] != 0 || b[255] != 0xFF || !rising(b, 0, 256, false)) return false;
    for (size_t k = 0; k < 128; ++k)
        if (b[k] + b[255 - k] != 255 && b[k] + b[255 - k] != 256) return false;
    return true;
}
bool check_effect_kinds(ByteSpan b) { // a kind 0 .. 15 per surface
    for (size_t o = 0; o < b.size(); o += 4)
        if (word_at(b, o) > 15) return false;
    return any_set(b);
}
bool check_effect_scales(ByteSpan b) { // 8 scales 0 .. 4
    for (size_t o = 0; o < b.size(); o += 4) {
        const float f = float_at(b, o);
        if (!std::isfinite(f) || f < 0 || f > 4) return false;
    }
    return any_set(b);
}
bool check_none(ByteSpan) { return true; } // no shape to tell it by (found only through its routine)

// ---- the limits: what the engine needs of any table, whatever its values (GT3's code takes the rest) --------------------
// The rolling-resistance curve's 1 .. 8 points (statics()).
bool limit_static(ByteSpan b) { return b[0x0B] >= 1 && b[0x0B] <= 8; }
// A surface's effect kind: one of the 16 effect records of the course (ExactRace::emit).
bool limit_effect_kinds(ByteSpan b) {
    for (size_t o = 0; o < b.size(); o += 4)
        if (word_at(b, o) > 15) return false;
    return true;
}

using T = ProgramData::Table;
struct TableDef {
    T id;
    const char* what;
    bool (*check)(ByteSpan);
    T base = T::Count; // a table it sits next to in every release (tier 2: found from that one's place) ...
    s32 offset = 0;    // ... at this offset from its start
    bool (*limit)(ByteSpan) = nullptr; // what the engine needs of it (none: any values)
};
// In Table's order.
constexpr size_t kTables_n = size_t(ProgramData::kTables);
const TableDef kTableDefs[] = {
    {T::Static, "the static physics block", check_static, T::Count, 0, limit_static},
    {T::Effects, "the tyre effect block", check_effects},
    {T::Controller, "the controller settings", check_controller},
    {T::SurfaceClass, "the surface classes", check_classes},
    {T::Soft, "the soft materials", check_soft},
    {T::DriverSets, "the AI driver parameter sets", check_driver_sets},
    {T::LineFallback, "the AI line fallback table", check_line_fallback},
    {T::InitialLines, "the AI initial line candidates", check_initial_lines},
    {T::LineOrder, "the AI line loading order", check_line_order},
    {T::GearKinds, "the gear kind table", check_gear_kinds},
    {T::SpecCurves, "the spec lock curve default", check_spec_curves,
     T::SpecCurveB, -0x10},
    {T::SpecCurveB, "the spec tyre curve B default", check_curve_b,
     T::SpecCurves, 0x10},
    {T::SpecSurfaceClass, "the spec surface classes", check_classes},
    {T::LsdTypes, "the differential type letters", check_lsd_types},
    {T::Messages, "the HUD messages", check_messages},
    {T::MachineSectors, "the machine test sectors", check_machine_sectors},
    {T::StickCurve, "the stick response curve", check_stick_curve},
    {T::EffectKinds, "the tyre effect kinds", check_effect_kinds, T::Count, 0, limit_effect_kinds},
    {T::EffectScales, "the tyre effect scales", check_effect_scales},
    {T::EffectParams, "the tyre effect parameters", check_none},
};

// ---- where the tables are addressed and the rules' routines (generated from the discs) ------------------------------
// A table's routine (a window of it, `words` long, around its address's instructions): the hash of its masked code, the
// positions of the two instructions that form the table's address in it (hi = lo: one offset from the global pointer), the
// releases that have it.
struct TableSite {
    T id;
    u64 hash;
    u16 words, hi, lo;
    u8 programs;
};
const TableSite kTableSites[] = {
    {T::Static, 0x2F5FC9DC65E427C4ull, 107, 4, 5, 7},
    {T::Effects, 0xEA919D3FF9B45187ull, 223, 1, 3, 7},
    {T::Controller, 0x6F719E17A0EAA671ull, 183, 1, 5, 5},
    {T::Controller, 0xF0D9ABB118052409ull, 183, 1, 5, 2},
    {T::SurfaceClass, 0x6AB8FA7D5744A5A7ull, 36, 1, 3, 7},
    {T::Soft, 0xDFDCE39D683B4CFFull, 7, 1, 2, 7},
    {T::DriverSets, 0x36A077C5BF8F30F1ull, 30, 25, 27, 7},
    {T::LineFallback, 0xC29B76E1423A89B6ull, 34, 11, 12, 7},
    {T::InitialLines, 0x913B64DE278DFF51ull, 67, 1, 6, 7},
    {T::LineOrder, 0x87BCB3D2F275E8D1ull, 61, 28, 28, 7},
    {T::GearKinds, 0xB2230B51B46932DFull, 16, 8, 9, 7},
    {T::SpecCurves, 0xCFE100974AE9647Aull, 255, 64, 66, 7},
    {T::SpecCurveB, 0xCFE100974AE9647Aull, 255, 213, 213, 7},
    {T::SpecSurfaceClass, 0xCFE100974AE9647Aull, 255, 157, 160, 7},
    {T::LsdTypes, 0xE8F2BBACC96D2A2Aull, 21, 12, 12, 7},
    {T::Messages, 0x45363398E90562FBull, 13, 0, 2, 7},
    {T::MachineSectors, 0xF10B21C7EF1BA8BBull, 24, 14, 16, 3},
    {T::MachineSectors, 0x69F0A21DD8BF04DBull, 24, 14, 16, 4},
    {T::StickCurve, 0x548882043FB7CAAFull, 36, 15, 17, 7},
    {T::EffectKinds, 0x6323B9A9F12330C6ull, 221, 1, 3, 7},
    {T::EffectScales, 0x6323B9A9F12330C6ull, 221, 64, 66, 7},
    {T::EffectParams, 0x32E1110E657A5031ull, 56, 1, 3, 7},
};
// A rule's routine: the hash of its whole masked code, its words (0: these releases have no such routine), the releases.
struct RuleSite {
    Rule rule;
    u64 hash;
    u16 words;
    u8 programs;
};
const RuleSite kRuleSites[] = {
    {Rule::RubberBand, 0xFD846D29752D17C3ull, 67, 5},
    {Rule::RubberBand, 0x6F79A76A49EC1F2Full, 72, 2},
    {Rule::BattleCap, 0xF3C3D0B0111A64E0ull, 27, 1},
    {Rule::BattleCap, 0x0000000000000000ull, 0, 6},
    {Rule::Oversteer, 0xD773594589B94068ull, 215, 5},
    {Rule::Oversteer, 0x37E2D484E2753E6Cull, 220, 2},
    {Rule::PadRumble, 0xF6C9EB3D6F486936ull, 132, 5},
    {Rule::PadRumble, 0x66894A6A76A7825Aull, 142, 2},
    {Rule::SteerForce, 0xF89FC64B74A07980ull, 364, 5},
    {Rule::SteerForce, 0x81770A5E30B858F6ull, 372, 2},
    {Rule::LicenceStop, 0xBB4AAC59101AFBE0ull, 37, 5},
    {Rule::LicenceStop, 0xBD5C4836B1B93B76ull, 37, 2},
    {Rule::LapRecord, 0x3DB1A31C9E527625ull, 38, 1},
    {Rule::LapRecord, 0x9688AA50C84EB6CAull, 37, 2},
    {Rule::LapRecord, 0x785F27924C7EA34Aull, 38, 4},
    {Rule::SpeedEvent, 0x0AB1269B86C82B19ull, 40, 5},
    {Rule::SpeedEvent, 0x8AB3963614329151ull, 46, 2},
    {Rule::RecordsLocked, 0xCA0A2262699E53C4ull, 14, 5},
    {Rule::RecordsLocked, 0x8AA4B93A1049F3FEull, 12, 2},
    {Rule::LapTime, 0xC60C6D01F54306A9ull, 78, 1},
    {Rule::LapTime, 0x2447E896CFF5A4BBull, 81, 2},
    {Rule::LapTime, 0x1BE1B0D370A60EDCull, 67, 4},
    {Rule::SectorTime, 0x6936D79042C6E036ull, 63, 1},
    {Rule::SectorTime, 0x54320AA57B70951Bull, 66, 2},
    {Rule::SectorTime, 0x6C39B94242FD951Full, 38, 4},
    {Rule::SectorDiff, 0x2C7FE65DD10179BDull, 47, 3},
    {Rule::SectorDiff, 0x13B0B9DA022FD914ull, 42, 4},
    {Rule::LapDiff, 0x0636237DE790B58Aull, 42, 3},
    {Rule::LapDiff, 0xEB4B17C187385469ull, 37, 4},
    {Rule::AngleWrap, 0x87E750BF1443DFF6ull, 32, 3},
    {Rule::AngleWrap, 0x03BA289DC405C133ull, 28, 4},
    {Rule::RunUp, 0x734443B8342C6FA4ull, 327, 1},
    {Rule::RunUp, 0x642C317DD650EB96ull, 327, 2},
    {Rule::RunUp, 0xB881DD9D43F3DCA5ull, 325, 4},
    {Rule::BattleStart, 0xAAA604E9E1ED2D71ull, 29, 3},
    {Rule::BattleStart, 0x141BFDCF3CABE724ull, 35, 4},
    {Rule::CatchupAnswer, 0x964CC3048352520Cull, 9, 3},
    {Rule::CatchupAnswer, 0x294A41C42FD83B55ull, 12, 4},
    {Rule::WearAnswer, 0x87E4C90F854D58B9ull, 21, 3},
    {Rule::WearAnswer, 0x285AFE38E4E6EA33ull, 20, 4},
    {Rule::DrawMode, 0x1FCEBB6CF5C5619Aull, 27, 1},
    {Rule::DrawMode, 0xE00BF7904B2AA836ull, 27, 2},
    {Rule::DrawMode, 0xF5969B9D43ECB26Dull, 25, 4},
    {Rule::LapStartRestore, 0x286411A8978EF1FDull, 68, 3},
    {Rule::LapStartRestore, 0xDE3D8DF6D38E2949ull, 64, 4},
    {Rule::CollisionCommit, 0xF8A4EB92A22393F9ull, 26, 3},
    {Rule::CollisionCommit, 0x085BCA824DCC4529ull, 17, 4},
    {Rule::WallFriction, 0xAFB4F2BD51D30E3Cull, 66, 3},
    {Rule::WallFriction, 0x17CDC9658AF3C630ull, 49, 4},
    {Rule::PushSegment, 0x229A875807F26EB2ull, 253, 3},
    {Rule::PushSegment, 0x10B9AD8C45A66040ull, 247, 4},
    {Rule::LapUsable, 0x8C2DD946FD757C17ull, 172, 3},
    {Rule::LapUsable, 0x546A28F3E5D59A2Dull, 169, 4},
};

// A table's address from the window at `at`: its upper half plus the offset added to it (or one offset from the global
// pointer).
bool decode(const CodeIndex& x, u32 at, const TableSite& s, u32& out) {
    const u32 lo = x.word(at + 4 * s.lo), lo_op = lo >> 26, lo_rs = (lo >> 21) & 31;
    if (lo_op != 0x09) return false; // the offset added
    const u32 off = u32(s32(s16(lo & 0xFFFF)));
    if (s.hi == s.lo) {
        if (lo_rs != 28 || !x.gp()) return false;
        out = x.gp() + off;
        return true;
    }
    const u32 hi = x.word(at + 4 * s.hi);
    if (hi >> 26 != 0x0F || ((hi >> 16) & 31) != lo_rs) return false; // the upper half, in the offset's base register
    out = ((hi & 0xFFFF) << 16) + off;
    return true;
}

int lowest(u8 mask) {
    for (int p = 0; p < kPrograms; ++p)
        if (mask >> p & 1) return p;
    return 0;
}

} // namespace

const char* ProgramData::what(Table t) { return size_t(t) < std::size(kTableDefs) ? kTableDefs[size_t(t)].what : "?"; }

ProgramData ProgramData::load(ByteSpan program, u32 load_address, u32 entry_point) {
    static_assert(std::size(kTableDefs) == kTables_n, "a definition per table, in Table's order");
    ProgramData d;
    const CodeIndex x(program, load_address, entry_point);
    std::array<u32, kTables_n> at{};
    // What each recognised piece says the program is (a bit per release), for the program's identity.
    std::vector<u8> votes;
    // A table GT3's code places (by_code: its routine's address, or a fixed offset from such a table) only needs what the
    // engine needs of it; one found by its content must also look like GT3's.
    std::array<bool, kTables_n> by_code{};
    auto placed = [&](const TableDef& t, u32 a) {
        const u32 n = size(t.id);
        return a >= x.text_end() && x.in_image(a, n) && (!t.limit || t.limit(x.bytes(a, n)));
    };
    auto fits = [&](const TableDef& t, u32 a) { return placed(t, a) && t.check(x.bytes(a, size(t.id))); };

    // Tier 1: the table's routine, the address from its instructions.
    for (size_t i = 0; i < kTables_n; ++i) {
        const TableDef& t = kTableDefs[i];
        u32 found = 0;
        u8 mask = 0;
        bool clash = false;
        for (const TableSite& s : kTableSites) {
            if (s.id != t.id) continue;
            for (u32 w : x.find(s.hash, s.words)) {
                u32 a = 0;
                if (!decode(x, w, s, a) || !placed(t, a)) continue;
                if (found && found != a) clash = true;
                found = a, mask |= s.programs;
                d.sites_[i] = {w, s.words, s.hi, s.lo};
            }
        }
        if (found && !clash) at[i] = found, d.tiers_[i] = 1, by_code[i] = true, votes.push_back(mask);
        else d.sites_[i] = {};
    }
    // Tier 2: the table it is part of, else the one place in the program's data with the table's shape.
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < kTables_n; ++i) {
            const TableDef& t = kTableDefs[i];
            if (at[i]) continue;
            if (t.base != T::Count) {
                const size_t j = size_t(t.base);
                const u32 a = at[j] + u32(t.offset);
                if (at[j] && (by_code[j] ? placed(t, a) : fits(t, a))) at[i] = a, d.tiers_[i] = 2, by_code[i] = by_code[j];
                if (at[i] || pass == 0) continue;
            }
            if (pass == 0 || t.check == check_none) continue;
            u32 only = 0;
            int n = 0;
            for (u32 a = (x.text_end() + 3) & ~3u; x.in_image(a, size(t.id)) && n < 2; a += 4)
                if (t.check(x.bytes(a, size(t.id)))) only = a, ++n;
            if (n == 1) at[i] = only, d.tiers_[i] = 2;
        }

    // The rules' routines: the variants found; a release that has no such routine counts when the program is that release.
    std::array<u8, kRules> found{}, absent{};
    for (const RuleSite& s : kRuleSites) {
        if (s.words == 0) {
            absent[size_t(s.rule)] |= s.programs;
        } else if (const std::vector<u32> w = x.find(s.hash, s.words); !w.empty()) {
            found[size_t(s.rule)] |= s.programs;
            if (!d.rule_at_[size_t(s.rule)]) d.rule_at_[size_t(s.rule)] = w[0];
        }
    }
    for (int r = 0; r < kRules; ++r)
        if (found[size_t(r)]) votes.push_back(found[size_t(r)]);

    // The program: the release every recognised piece agrees on, else the one most of them name.
    u8 agree = kAll;
    std::array<int, kPrograms> count{};
    for (u8 m : votes) {
        agree &= m;
        for (int p = 0; p < kPrograms; ++p) count[size_t(p)] += m >> p & 1;
    }
    const int most = int(std::max_element(count.begin(), count.end()) - count.begin());
    d.program_ = Program(agree && !votes.empty() ? lowest(agree) : most);
    const u8 self = u8(1u << int(d.program_));

    // Each rule: the program's own variant, else the one variant found, else the program's release's (an issue: a build's
    // routine of its own, its nearest release's rules the closest guess).
    d.own_ = {d.program_, 60};
    bool all_rules = true;
    for (int r = 0; r < kRules; ++r) {
        const Rule rule = Rule(r);
        Program v = d.program_;
        bool ok = true;
        if ((found[size_t(r)] & self) || (!found[size_t(r)] && (absent[size_t(r)] & self))) v = d.program_;
        else if (found[size_t(r)]) v = Program(lowest(found[size_t(r)]));
        else ok = false;
        d.recognised_[size_t(r)] = ok;
        if (!ok) {
            all_rules = false;
            d.issues_.push_back(std::string(rule_name(rule)) + ": not a known release's code; " + program_name(d.program_) +
                                "'s rules");
        }
        d.own_.variant[size_t(r)] = v == d.program_ ? Behaviour::kFollow : u8(v);
    }
    d.behaviour_ = d.own_;

    // The tables: GT3's values, the variant of the release the disc's table equals; one GT3's code places but unlike every
    // release's is changed on this disc and its own values serve; one found only by its content and unlike every release's,
    // or not found, an issue: GT3's values of the program's release.
    bool all_tables = true;
    for (size_t i = 0; i < kTables_n; ++i) {
        const TableDef& t = kTableDefs[i];
        d.ranges_[i].where = at[i];
        Program v = d.program_;
        bool matched = false;
        ByteSpan b;
        if (at[i]) {
            b = x.bytes(at[i], size(t.id));
            for (int p = 0; p < kPrograms && !matched; ++p) {
                const ByteSpan g = gt3_table(t.id, Program(p));
                if (std::equal(b.begin(), b.end(), g.begin(), g.end())) v = Program(p), matched = true;
            }
            if (matched && !by_code[i])
                d.issues_.push_back(std::string(t.what) + ": found by its content (its routine is not a known release's)");
        }
        const bool changed = at[i] && !matched && by_code[i];
        if (changed) d.issues_.push_back(std::string(t.what) + ": changed on this disc");
        else if (!matched)
            d.issues_.push_back(std::string(t.what) + (at[i] ? ": unlike every release's on this disc" : ": not found on this disc") +
                                "; GT3's values");
        all_tables = all_tables && (matched || changed) && by_code[i];
        d.releases_[i] = v, d.matched_[i] = matched, d.changed_[i] = changed;
        const ByteSpan g = changed ? b : gt3_table(t.id, v);
        d.ranges_[i].bytes.assign(g.begin(), g.end());
    }
    d.known_ = agree != 0 && !votes.empty() && all_tables && all_rules;
    for (const std::string& s : d.issues_) LOG_WARN("CORE.GT3: %s", s.c_str());
    if (!d.known_) LOG_WARN("CORE.GT3: not a known release's program; nearest %s", program_name(d.program_));
    return d;
}

ProgramData ProgramData::load(const CoreImage& core) {
    return load(ByteSpan(core.program.data(), core.program.size()), core.load_address, core.entry_point);
}

ProgramData ProgramData::synthetic(Program p) {
    ProgramData d;
    d.program_ = p, d.known_ = true;
    d.matched_.fill(true), d.releases_.fill(p);
    d.own_ = d.behaviour_ = {p, 60};
    d.recognised_.fill(true);
    for (size_t i = 0; i < kTables_n; ++i) d.ranges_[i].bytes.assign(size(T(i)), 0);
    return d;
}

bool ProgramData::changed() const { return std::find(changed_.begin(), changed_.end(), true) != changed_.end(); }

ProgramData ProgramData::gt3_tables() const {
    ProgramData d = *this;
    for (size_t i = 0; i < kTables_n; ++i)
        if (d.changed_[i]) {
            const ByteSpan g = gt3_table(T(i), d.releases_[i]);
            d.ranges_[i].bytes.assign(g.begin(), g.end());
            d.changed_[i] = false;
        }
    return d;
}

bool ProgramData::can_behave(const Behaviour& b) const {
    if (b.rules != Program::NtscU && b.rules != program_) return false;
    return b.rate == 60 || (b.rate == 50 && b.rules == Program::Pal);
}

void ProgramData::set_behaviour(const Behaviour& b) {
    if (!can_behave(b))
        throw std::invalid_argument(std::string("ProgramData: ") + program_name(b.rules) + " rules at " + std::to_string(b.rate) +
                                    " steps a second on the " + program_name(program_) +
                                    " program (the rules of NTSC-U or the disc's release; 50 only with PAL's)");
    behaviour_ = b;
    // The disc's own rules: with the routines its program has of another release's.
    if (b.rules == program_ && b.follows_rules()) behaviour_.variant = own_.variant;
}

const u8* ProgramData::at(Table t, u32 off, u32 n) const {
    const Range& r = ranges_[size_t(t)];
    if (r.bytes.empty() || off + u64(n) > r.bytes.size())
        throw std::out_of_range(std::string("ProgramData: ") + what(t) + (r.bytes.empty() ? ": not found" : ": past its end"));
    return r.bytes.data() + off;
}

u32 ProgramData::address(Table t, u32 off) const {
    (void)at(t, off, 1);
    return ranges_[size_t(t)].where + off;
}

u8 ProgramData::b(Table t, u32 off) const { return *at(t, off, 1); }
u16 ProgramData::h(Table t, u32 off) const {
    const u8* p = at(t, off, 2);
    return u16(p[0] | p[1] << 8);
}
u32 ProgramData::w(Table t, u32 off) const {
    const u8* p = at(t, off, 4);
    return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24;
}
void ProgramData::poke(Table t, u32 off, u8 v) { *const_cast<u8*>(at(t, off, 1)) = v; }

u64 ProgramData::fingerprint() const {
    u64 h = 1469598103934665603ull; // FNV-1a
    auto mix = [&](u8 c) { h = (h ^ c) * 1099511628211ull; };
    mix(u8(program_)); // (the versions' routines differ where some of their tables do not)
    for (u8 v : own_.variant) mix(v);
    for (size_t i = 0; i < kTables_n; ++i) {
        mix(u8(i));
        for (u8 c : ranges_[i].bytes) mix(c);
    }
    return h;
}

} // namespace gt3r::sim::exact
