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
    case Program::ConceptTokyo: return "Concept 2001 Tokyo";
    case Program::ConceptSeoul: return "Concept 2002 Tokyo-Seoul v1.02";
    case Program::ConceptSeoul2: return "Concept 2002 Tokyo-Seoul v2.00";
    case Program::ConceptGenevaEu: return "Concept 2002 Tokyo-Geneva (Europe)";
    case Program::ConceptGenevaAsia: return "Concept 2002 Tokyo-Geneva (Asia)";
    case Program::NtscU: break;
    }
    return "NTSC-U";
}

const char* rule_name(Rule r) {
    switch (r) {
    case Rule::RubberBand: return "the rubber band"; // 0x1ECEB8
    case Rule::BattleCap: return "the i.LINK battle's catch-up cap"; // 0x1F0478
    case Rule::Oversteer: return "the AI's oversteer indicator"; // 0x1E3708
    case Rule::PadRumble: return "the pad vibration"; // 0x1DF2B0
    case Rule::SteerForce: return "the steering-wheel force"; // 0x1E0068
    case Rule::LicenceStop: return "the licence test's stop"; // 0x1EF488
    case Rule::LapRecord: return "the lap record"; // 0x1EFDE8
    case Rule::SpeedEvent: return "the machine test's top speed"; // 0x1EC8C0
    case Rule::RecordsLocked: return "the records' lock"; // 0x1EC4E8
    case Rule::LapTime: return "the lap time"; // 0x1EC620
    case Rule::SectorTime: return "the sector time"; // 0x1EC520
    case Rule::SectorDiff: return "the sector difference"; // 0x1EC758
    case Rule::LapDiff: return "the lap difference"; // 0x1EC818
    case Rule::AngleWrap: return "the angle wrap"; // 0x1D8C00
    case Rule::RunUp: return "the run-up start"; // 0x1DDAD0
    case Rule::BattleStart: return "the battle's start"; // 0x1F0010
    case Rule::CatchupAnswer: return "the battle's catch-up setting"; // 0x1F0098
    case Rule::WearAnswer: return "the battle's tyre wear setting"; // 0x1F00C0
    case Rule::DrawMode: return "the car's draw mode"; // 0x1E0668
    case Rule::LapStartRestore: return "the replay's lap-start restore"; // 0x1EE6D8
    case Rule::CollisionCommit: return "the collision commit"; // 0x1FB6D8
    case Rule::WallFriction: return "the wall friction"; // 0x1FE660
    case Rule::PushSegment: return "the wall push-out"; // 0x1FDA10
    case Rule::LapUsable: return "the free run's lap check"; // 0x1F16B8
    case Rule::Atan2: return "atan2f"; // 0x269FC8
    case Rule::FloatToDouble: return "the float-to-double conversion"; // 0x27AFA0
    case Rule::EngineCondition: return "the engine condition factor"; // 0x1DAD28
    case Rule::SlipAngle: return "the slip angle's direction"; // 0x1EA348
    case Rule::SpecBuild: return "the spec builder"; // 0x1DA308
    case Rule::BodyBuild: return "the collision body's outline"; // 0x1FA740
    case Rule::DriverSpread: return "the computer drivers' spread"; // 0x1F6380
    case Rule::MovingStart: return "the moving start"; // 0x1DD8A8
    case Rule::GroundMiss: return "the ground query's height off the road"; // 0x2280C8
    case Rule::GearSpec: return "the gear part's spec"; // 0x232E18
    case Rule::Layout: return "the drive layout"; // 0x1E2700
    case Rule::Driveline: return "the driveline block"; // 0x1E1C98
    case Rule::AwdGain: return "the AWD coupling gain"; // 0x1E5998
    case Rule::DriveClass: return "the AI's driveline class"; // 0x1F5EF0
    case Rule::AxleStep: return "the axle step"; // 0x1E5B98
    case Rule::PadClutch: return "the pad's clutch request"; // 0x1E4180
    case Rule::PadControls: return "the pad's control frame"; // 0x1E4478
    case Rule::DriverInput: return "the driver input phase"; // 0x1E4D38
    case Rule::AiOutputs: return "the AI's outputs"; // 0x1F79F0
    case Rule::AiDecide: return "the AI's decision"; // 0x1F7AE0
    case Rule::AiTraffic: return "the AI's traffic step"; // 0x1F6D70
    case Rule::AiBraking: return "the AI's braking"; // 0x1F7088
    case Rule::Ascc: return "the ASCC"; // 0x1E2D08
    case Rule::TyreEffects: return "the tyre effects"; // 0x1DEBB0
    case Rule::GapClosing: return "a pair's closing speed"; // 0x1EBDA0
    case Rule::ContactIntensity: return "the AI's contact intensity"; // 0x1F6CC0
    case Rule::SlowDown: return "the run-on after a finish"; // 0x1F6518
    case Rule::ParamsBuild: return "the parameter converter"; // 0x1DBFD8
    case Rule::Aids: return "the aids' choice"; // 0x1E3B18
    case Rule::RecordStore: return "the pad recorder's store"; // 0x1F4FF0
    case Rule::StuckNudge: return "the collision step's stuck nudge"; // 0x1FB740
    case Rule::SoftDouble: return "the soft doubles"; // 0x279480
    case Rule::HybridStep: return "the layout 7 car's step"; // later 0x1DA0A8
    case Rule::PodMood: return "special car 1's mood"; // later 0x1DB2F8
    case Rule::PodStop: return "special car 1's stop count"; // later 0x1DBAB0
    case Rule::ActiveWing: return "special car 2's wing"; // later 0x1DBC48
    case Rule::FlyingGrid: return "the single race's flying start"; // later 0x1E73E0
    case Rule::StopZone: return "the stop zone's set-up"; // later 0x1EE280
    case Rule::PodRule: return "the pod race's rule"; // later 0x1EE8C0
    case Rule::PodLine: return "the pod race's line"; // later 0x1EE7F0
    case Rule::PodSpring: return "special car 1's springs"; // later 0x1DBB28
    case Rule::HitReport: return "the collision report"; // 0x1DFEB8
    case Rule::PassBy: return "the pass-by states"; // 0x1F59B0
    case Rule::LicenceJudge: return "the licence test's judge"; // 0x1EF780
    case Rule::Leaves: return "the leaf maker"; // later 0x19C4E8
    case Rule::SparkEmit: return "the sparks' emission"; // 0x1AADD0
    case Rule::Count: break;
    }
    return "?";
}

} // namespace gt3r::sim

namespace gt3r::sim::exact {

namespace {

constexpr u8 kAll = 0xFF; // every program (a bit per Program)

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
bool check_pod_messages(ByteSpan b) { // the HUD messages 0x10 and 0x11: their numbers, their display steps
    for (u32 k = 0; k < 2; ++k)
        if (word_at(b, 8 * k) != 0x10 + k || word_at(b, 8 * k + 4) > 0xFFFF) return false;
    return true;
}

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
constexpr size_t kTables_n = size_t(ProgramData::kTables), kAllTables_n = size_t(ProgramData::kAllTables);
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
    {T::SpecialCars, "the special cars' labels", check_none},
    {T::Awd3, "the AWD system 3 settings", check_none},
    {T::PodMoods, "special car 1's mood tables", check_none},
    {T::Stepless, "the stepless gearbox settings", check_none},
    {T::RaceSwitches, "the stop zone and ASCC switches", check_none},
    {T::Kind7Ride, "special kind 7's ride settings", check_none},
    {T::FlyingStart, "the single race's flying start grid", check_none},
    {T::Hybrid, "the layout 7 system's settings", check_none},
    {T::PodMessages, "the pod race's HUD messages", check_pod_messages, T::Messages, 0x80},
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
    u16 off = 0; // how far into the table the address formed is (a later build's compiler addresses a field)
};
const TableSite kTableSites[] = {
    {T::Static, 0x2F5FC9DC65E427C4ull, 107, 4, 5, 0x07},
    {T::Effects, 0xEA919D3FF9B45187ull, 223, 1, 3, 0x07},
    {T::Controller, 0x6F719E17A0EAA671ull, 183, 1, 5, 0x05},
    {T::SurfaceClass, 0x6AB8FA7D5744A5A7ull, 36, 1, 3, 0x07},
    {T::Soft, 0xDFDCE39D683B4CFFull, 7, 1, 2, 0x07},
    {T::DriverSets, 0x36A077C5BF8F30F1ull, 30, 25, 27, 0x07},
    {T::LineFallback, 0xC29B76E1423A89B6ull, 34, 11, 12, 0x07},
    {T::InitialLines, 0x913B64DE278DFF51ull, 67, 1, 6, 0x07},
    {T::LineOrder, 0x87BCB3D2F275E8D1ull, 61, 28, 28, 0x07},
    {T::GearKinds, 0xB2230B51B46932DFull, 16, 8, 9, 0x07},
    {T::SpecCurves, 0xCFE100974AE9647Aull, 255, 64, 66, 0x07},
    {T::SpecCurveB, 0xCFE100974AE9647Aull, 255, 213, 213, 0x07},
    {T::SpecSurfaceClass, 0xCFE100974AE9647Aull, 255, 157, 160, 0x07},
    {T::LsdTypes, 0xE8F2BBACC96D2A2Aull, 21, 12, 12, 0x07},
    {T::Messages, 0x45363398E90562FBull, 13, 0, 2, 0x07},
    {T::MachineSectors, 0xF10B21C7EF1BA8BBull, 24, 14, 16, 0x03},
    {T::StickCurve, 0x548882043FB7CAAFull, 36, 15, 17, 0x07},
    {T::EffectKinds, 0x6323B9A9F12330C6ull, 221, 1, 3, 0x07},
    {T::EffectScales, 0x6323B9A9F12330C6ull, 221, 64, 66, 0x07},
    {T::EffectParams, 0x32E1110E657A5031ull, 56, 1, 3, 0x07},
    {T::Controller, 0xF0D9ABB118052409ull, 183, 1, 5, 0x02},
    {T::MachineSectors, 0x69F0A21DD8BF04DBull, 24, 14, 16, 0x04},
    {T::Static, 0xE5740816F0FD6640ull, 70, 0, 1, 0x38},
    {T::Effects, 0x802CD6591CADE1C9ull, 232, 1, 3, 0x38},
    {T::Controller, 0x4A8D78A8BAE9E368ull, 81, 1, 3, 0x38},
    {T::SurfaceClass, 0x0C3F6C8F350DE1E5ull, 36, 1, 3, 0xF8},
    {T::Soft, 0x89911EFA2510DDF0ull, 6, 2, 5, 0x08},
    {T::DriverSets, 0xA3F4E8EB0476C4C9ull, 28, 24, 25, 0xF8},
    {T::LineFallback, 0x03B7304126B62139ull, 37, 14, 15, 0xF8},
    {T::InitialLines, 0xD69A81C5768898A3ull, 67, 1, 8, 0x38},
    {T::LineOrder, 0xCE573F96A21A01DAull, 62, 29, 29, 0x38},
    {T::GearKinds, 0xFE97CD71FC9B3298ull, 15, 9, 11, 0x08},
    {T::SpecCurves, 0x8741FCC996FB4FECull, 68, 32, 35, 0x18},
    {T::SpecCurveB, 0x6C31110955447ADAull, 65, 32, 32, 0xF8},
    {T::SpecSurfaceClass, 0x881A3EA03C89074Cull, 68, 32, 35, 0x18},
    {T::LsdTypes, 0xE578AF5332D6A2FDull, 34, 20, 20, 0xF8},
    {T::Messages, 0xB57E56A36D64A9C6ull, 12, 1, 3, 0x08, 4},
    {T::MachineSectors, 0xB45EF5BBBC9ABECDull, 21, 3, 5, 0x18},
    {T::StickCurve, 0xA4131C09C857EB8Bull, 249, 96, 100, 0x38},
    {T::EffectKinds, 0x8085118D4C68C606ull, 231, 15, 17, 0x08},
    {T::EffectScales, 0x8085118D4C68C606ull, 231, 73, 75, 0x08},
    {T::EffectParams, 0x0B3821F1C5E00C56ull, 80, 0, 6, 0x38},
    {T::SpecialCars, 0xA94A0294554AEC17ull, 67, 32, 34, 0x18, 32},
    {T::Awd3, 0x2F5FEB05A1114E2Bull, 151, 69, 69, 0x08},
    {T::PodMoods, 0x50C9819FF7041577ull, 60, 41, 42, 0x08, 32},
    {T::Stepless, 0x42BE5DBA2D3F159Full, 186, 17, 17, 0x38},
    {T::RaceSwitches, 0xB7209536207DEA08ull, 86, 15, 15, 0x38},
    {T::FlyingStart, 0x051A00C6CBCDF148ull, 83, 26, 26, 0x38, 8},
    {T::Hybrid, 0x097353F166CFF75Bull, 191, 0, 0, 0x38},
    {T::Soft, 0x89911EFA2510E8F0ull, 6, 2, 5, 0x10},
    {T::GearKinds, 0x46E0CDA7F48C8B98ull, 15, 9, 11, 0x10},
    {T::Messages, 0x7C0BBA1BB59414C6ull, 12, 1, 3, 0x10, 4},
    {T::EffectKinds, 0x6B520972D53C8746ull, 231, 15, 17, 0x10},
    {T::EffectScales, 0x6B520972D53C8746ull, 231, 73, 75, 0x10},
    {T::Awd3, 0x5DC278FB8BF3BC2Bull, 151, 69, 69, 0x10},
    {T::PodMoods, 0x3CDC395A59351077ull, 60, 41, 42, 0x10, 32},
    {T::Soft, 0x89911EFA251120D0ull, 6, 2, 5, 0x20},
    {T::GearKinds, 0x656A41CD57D86890ull, 15, 9, 11, 0x20},
    {T::SpecCurves, 0x0F3EAA87F55DD3C4ull, 68, 32, 35, 0xE0},
    {T::SpecSurfaceClass, 0xA4BD08D3B51F7970ull, 68, 32, 35, 0xE0},
    {T::Messages, 0xAE7078A1B3BA0176ull, 12, 1, 3, 0x20, 4},
    {T::MachineSectors, 0xBFF5153DCF3EDF7Dull, 21, 3, 5, 0xE0},
    {T::EffectKinds, 0xB870F344DF32D966ull, 231, 15, 17, 0x20},
    {T::EffectScales, 0xB870F344DF32D966ull, 231, 73, 75, 0x20},
    {T::SpecialCars, 0x777E9DC5174E88A5ull, 67, 32, 34, 0xE0, 32},
    {T::Awd3, 0xF3ED05A35E191FEBull, 151, 69, 69, 0x20},
    {T::PodMoods, 0xC05E35E174B3AE57ull, 60, 41, 42, 0x20, 32},
    {T::Static, 0xF8BD498B7C978419ull, 61, 0, 1, 0xC0},
    {T::Effects, 0x93C11F5A6CEB8A35ull, 218, 1, 3, 0xC0},
    {T::Controller, 0x75910EC07DD95A8Eull, 74, 1, 3, 0xC0},
    {T::Soft, 0x89911EFA2510E6F0ull, 6, 2, 5, 0x40},
    {T::InitialLines, 0x7168ECB5F60A4EB4ull, 66, 1, 8, 0xC0},
    {T::LineOrder, 0x10E0D3646960E0DEull, 256, 29, 29, 0xC0},
    {T::GearKinds, 0x35406D989F90B538ull, 15, 9, 11, 0x40},
    {T::Messages, 0xBCB14D48DAD7CB96ull, 12, 1, 3, 0x40, 4},
    {T::StickCurve, 0xD5FA309E748D041Cull, 236, 96, 100, 0x40},
    {T::EffectKinds, 0xB620CC7D83CCDF0Full, 219, 15, 17, 0x40},
    {T::EffectScales, 0xB620CC7D83CCDF0Full, 219, 70, 72, 0x40},
    {T::EffectParams, 0x9BF6ACF0F554C617ull, 72, 0, 6, 0xC0},
    {T::Awd3, 0xFE222ACD27354A79ull, 141, 67, 67, 0x40},
    {T::PodMoods, 0xAD63DC6F3BF56761ull, 60, 41, 42, 0x40, 32},
    {T::Stepless, 0xF671AB1B8B9BF4E9ull, 170, 17, 17, 0xC0},
    {T::RaceSwitches, 0x845FD6F39EB2BFE0ull, 80, 15, 15, 0xC0},
    {T::Kind7Ride, 0xD638F60E0CC92CD2ull, 67, 6, 9, 0xC0},
    {T::Hybrid, 0x88FC57469C2AF198ull, 166, 0, 0, 0x40},
    {T::Soft, 0x89911EFA2510EC70ull, 6, 2, 5, 0x80},
    {T::GearKinds, 0x33E025B45FC92C70ull, 15, 9, 11, 0x80},
    {T::Messages, 0x1FF7FF04FEEF8116ull, 12, 1, 3, 0x80, 4},
    {T::StickCurve, 0x78E86D89632ACC33ull, 236, 96, 100, 0x80},
    {T::EffectKinds, 0xC771F808EDEBE20Full, 219, 15, 17, 0x80},
    {T::EffectScales, 0xC771F808EDEBE20Full, 219, 70, 72, 0x80},
    {T::Awd3, 0xF0C1F91059C420F9ull, 141, 67, 67, 0x80},
    {T::PodMoods, 0x93D654A2146CD1D7ull, 60, 41, 42, 0x80, 32},
    {T::Hybrid, 0x9F6D122824183198ull, 166, 0, 0, 0x80},
};
// A rule's routine: the hash of its whole masked code, its words (0: these releases have no such routine), the releases;
// and for a later build's routine (another compiler's code) the GT3 release whose rules it has (kOwn: rules of its own, the
// port's branch for that build).
constexpr u8 kOwn = 0xFF;
struct RuleSite {
    Rule rule;
    u64 hash;
    u16 words;
    u8 programs;
    u8 as = kOwn;
};
const RuleSite kRuleSites[] = {
    {Rule::RubberBand, 0xFD846D29752D17C3ull, 67, 0x05},
    {Rule::BattleCap, 0xF3C3D0B0111A64E0ull, 27, 0x01},
    {Rule::Oversteer, 0xD773594589B94068ull, 215, 0x05},
    {Rule::PadRumble, 0xF6C9EB3D6F486936ull, 132, 0x05},
    {Rule::SteerForce, 0xF89FC64B74A07980ull, 364, 0x05},
    {Rule::LicenceStop, 0xBB4AAC59101AFBE0ull, 37, 0x05},
    {Rule::LapRecord, 0x3DB1A31C9E527625ull, 38, 0x01},
    {Rule::SpeedEvent, 0x0AB1269B86C82B19ull, 40, 0x05},
    {Rule::RecordsLocked, 0xCA0A2262699E53C4ull, 14, 0x05},
    {Rule::LapTime, 0xC60C6D01F54306A9ull, 78, 0x01},
    {Rule::SectorTime, 0x6936D79042C6E036ull, 63, 0x01},
    {Rule::SectorDiff, 0x2C7FE65DD10179BDull, 47, 0x03},
    {Rule::LapDiff, 0x0636237DE790B58Aull, 42, 0x03},
    {Rule::AngleWrap, 0x87E750BF1443DFF6ull, 32, 0x03},
    {Rule::RunUp, 0x734443B8342C6FA4ull, 327, 0x01},
    {Rule::BattleStart, 0xAAA604E9E1ED2D71ull, 29, 0x03},
    {Rule::CatchupAnswer, 0x964CC3048352520Cull, 9, 0x03},
    {Rule::WearAnswer, 0x87E4C90F854D58B9ull, 21, 0x03},
    {Rule::DrawMode, 0x1FCEBB6CF5C5619Aull, 27, 0x01},
    {Rule::LapStartRestore, 0x286411A8978EF1FDull, 68, 0x03},
    {Rule::CollisionCommit, 0xF8A4EB92A22393F9ull, 26, 0x03},
    {Rule::WallFriction, 0xAFB4F2BD51D30E3Cull, 66, 0x03},
    {Rule::PushSegment, 0x229A875807F26EB2ull, 253, 0x03},
    {Rule::LapUsable, 0x8C2DD946FD757C17ull, 172, 0x03},
    {Rule::Atan2, 0xF38D3FBFBD0837A8ull, 47, 0x07},
    {Rule::FloatToDouble, 0x9BA0133F04066A9Full, 16, 0x07},
    {Rule::EngineCondition, 0xD8EDB7FA54F1A85Eull, 73, 0x07},
    {Rule::SlipAngle, 0x9779E7B5C4BC03BFull, 36, 0x07},
    {Rule::SpecBuild, 0xCFE100974AE9647Aull, 255, 0x07},
    {Rule::BodyBuild, 0xE51EA22DC1A28FBCull, 75, 0x03},
    {Rule::DriverSpread, 0x99290150F76D2BFAull, 101, 0x07},
    {Rule::MovingStart, 0x2BFEA1855FBE1326ull, 138, 0x07},
    {Rule::GroundMiss, 0xB35FD32BC4777B37ull, 316, 0x07},
    {Rule::GearSpec, 0x588C65A3CBE4922Dull, 19, 0x07},
    {Rule::Layout, 0x44BF2D5363B1086Eull, 106, 0x07},
    {Rule::Driveline, 0xB7290F0FA68A8D09ull, 494, 0x07},
    {Rule::AwdGain, 0x9D1F12D6E1B60D70ull, 102, 0x07},
    {Rule::DriveClass, 0xDEFCCDF279BAE4F7ull, 17, 0x07},
    {Rule::AxleStep, 0x036B974650A8B777ull, 571, 0x07},
    {Rule::PadClutch, 0x14751C31EFD7114Dull, 67, 0x07},
    {Rule::PadControls, 0x082A18D5DDF8B591ull, 526, 0x01},
    {Rule::DriverInput, 0x8647C70C03897C99ull, 79, 0x03},
    {Rule::AiOutputs, 0x9E4083335173DD50ull, 59, 0x07},
    {Rule::AiDecide, 0x8024A432781ED579ull, 725, 0x05},
    {Rule::AiTraffic, 0x57182B9F74A4F2DCull, 193, 0x05},
    {Rule::AiBraking, 0x4CFB830F367E8409ull, 294, 0x07},
    {Rule::Ascc, 0xD47DFE484980E503ull, 215, 0x07},
    {Rule::TyreEffects, 0x9F56CC4E5DE70C3Dull, 358, 0x05},
    {Rule::GapClosing, 0x3824DC2B7E1719B0ull, 62, 0x07},
    {Rule::ContactIntensity, 0xEBF336CB8EAF87BAull, 42, 0x07},
    {Rule::SlowDown, 0xD2ED091B4AE0BB0Eull, 9, 0x07},
    {Rule::ParamsBuild, 0xAED20F71B35E9597ull, 1084, 0x07},
    {Rule::Aids, 0xAD964744EE8064F2ull, 53, 0x07},
    {Rule::RecordStore, 0x30ECD237E2AB813Eull, 14, 0x07},
    {Rule::StuckNudge, 0xFA0C63DD721E81F6ull, 42, 0x07},
    {Rule::SoftDouble, 0x6C7067D2159D1000ull, 170, 0x07},
    {Rule::HybridStep, 0x0000000000000000ull, 0, 0x07},
    {Rule::PodMood, 0x0000000000000000ull, 0, 0x07},
    {Rule::PodStop, 0x0000000000000000ull, 0, 0x07},
    {Rule::ActiveWing, 0x0000000000000000ull, 0, 0x07},
    {Rule::FlyingGrid, 0x0000000000000000ull, 0, 0x07},
    {Rule::StopZone, 0x0000000000000000ull, 0, 0x07},
    {Rule::PodRule, 0x0000000000000000ull, 0, 0x07},
    {Rule::PodLine, 0x0000000000000000ull, 0, 0x07},
    {Rule::PodSpring, 0x0000000000000000ull, 0, 0x07},
    {Rule::HitReport, 0x4E59BF9F8CA5C5D9ull, 33, 0x03},
    {Rule::PassBy, 0xF5A522C2CBC00F50ull, 169, 0x05},
    {Rule::LicenceJudge, 0x7C1AAA07B67FBF2Eull, 78, 0x01},
    {Rule::Leaves, 0x0000000000000000ull, 0, 0x07},
    {Rule::SparkEmit, 0xEE1D9DD4B37339D0ull, 201, 0x07},
    {Rule::RubberBand, 0x6F79A76A49EC1F2Full, 72, 0x02},
    {Rule::BattleCap, 0x0000000000000000ull, 0, 0x06},
    {Rule::Oversteer, 0x37E2D484E2753E6Cull, 220, 0x02},
    {Rule::PadRumble, 0x66894A6A76A7825Aull, 142, 0x02},
    {Rule::SteerForce, 0x81770A5E30B858F6ull, 372, 0x02},
    {Rule::LicenceStop, 0xBD5C4836B1B93B76ull, 37, 0x02},
    {Rule::LapRecord, 0x9688AA50C84EB6CAull, 37, 0x02},
    {Rule::SpeedEvent, 0x8AB3963614329151ull, 46, 0x02},
    {Rule::RecordsLocked, 0x8AA4B93A1049F3FEull, 12, 0x02},
    {Rule::LapTime, 0x2447E896CFF5A4BBull, 81, 0x02},
    {Rule::SectorTime, 0x54320AA57B70951Bull, 66, 0x02},
    {Rule::RunUp, 0x642C317DD650EB96ull, 327, 0x02},
    {Rule::DrawMode, 0xE00BF7904B2AA836ull, 27, 0x02},
    {Rule::PadControls, 0x09EA6A7878024001ull, 526, 0x02},
    {Rule::AiDecide, 0xB66C04B196601FD9ull, 725, 0x02},
    {Rule::AiTraffic, 0x27AD72FECB10511Full, 193, 0x02},
    {Rule::TyreEffects, 0x16A87086F1AD4B8Aull, 358, 0x02},
    {Rule::PassBy, 0x7955DAC500AB4200ull, 169, 0x02},
    {Rule::LicenceJudge, 0xE1B9523410FDAE26ull, 78, 0x02},
    {Rule::LapRecord, 0x785F27924C7EA34Aull, 38, 0x04},
    {Rule::LapTime, 0x1BE1B0D370A60EDCull, 67, 0x04},
    {Rule::SectorTime, 0x6C39B94242FD951Full, 38, 0x04},
    {Rule::SectorDiff, 0x13B0B9DA022FD914ull, 42, 0x04},
    {Rule::LapDiff, 0xEB4B17C187385469ull, 37, 0x04},
    {Rule::AngleWrap, 0x03BA289DC405C133ull, 28, 0x04},
    {Rule::RunUp, 0xB881DD9D43F3DCA5ull, 325, 0x04},
    {Rule::BattleStart, 0x141BFDCF3CABE724ull, 35, 0x04},
    {Rule::CatchupAnswer, 0x294A41C42FD83B55ull, 12, 0x04},
    {Rule::WearAnswer, 0x285AFE38E4E6EA33ull, 20, 0x04},
    {Rule::DrawMode, 0xF5969B9D43ECB26Dull, 25, 0x04},
    {Rule::LapStartRestore, 0xDE3D8DF6D38E2949ull, 64, 0x04},
    {Rule::CollisionCommit, 0x085BCA824DCC4529ull, 17, 0x04},
    {Rule::WallFriction, 0x17CDC9658AF3C630ull, 49, 0x04},
    {Rule::PushSegment, 0x10B9AD8C45A66040ull, 247, 0x04},
    {Rule::LapUsable, 0x546A28F3E5D59A2Dull, 169, 0x04},
    {Rule::BodyBuild, 0x0000000000000000ull, 0, 0x04},
    {Rule::PadControls, 0x70A4BBE4CF36C041ull, 526, 0x04},
    {Rule::DriverInput, 0x66469ED96F843EB9ull, 79, 0x04},
    {Rule::HitReport, 0x33A3E7B74981A939ull, 33, 0x04},
    {Rule::LicenceJudge, 0x664C61137CE6088Eull, 78, 0x04},
    {Rule::RubberBand, 0x231A9ED3CED0A627ull, 73, 0x38, 1},
    {Rule::BattleCap, 0x0000000000000000ull, 0, 0xF8, 1},
    {Rule::Oversteer, 0xC9DA2540B2CF4C60ull, 232, 0x38, 1},
    {Rule::PadRumble, 0xF9598251F9F14BD9ull, 160, 0x38, 1},
    {Rule::SteerForce, 0x52C102DBDA5FB6C2ull, 420, 0x38, 1},
    {Rule::LicenceStop, 0x781FD6BE1A8A3E8Aull, 36, 0x38, 0},
    {Rule::LapRecord, 0x34D221E38ECB6C48ull, 52, 0xF8, 1},
    {Rule::SpeedEvent, 0x7648BED90490DDCFull, 46, 0xF8, 1},
    {Rule::RecordsLocked, 0xE2C45D2D7C0F9DBCull, 11, 0xF8, 1},
    {Rule::LapTime, 0x970EA3537261F4DBull, 79, 0xF8, 1},
    {Rule::SectorTime, 0x43CC2864AE663F64ull, 65, 0xF8, 1},
    {Rule::SectorDiff, 0x3D5F482E64AB6F05ull, 44, 0xF8, 0},
    {Rule::LapDiff, 0x08C7AC2969D1282Bull, 41, 0xF8, 0},
    {Rule::AngleWrap, 0x86A82A34C04FB2D2ull, 35, 0x38, 0},
    {Rule::RunUp, 0x7DC154FB3DDE0C4Bull, 350, 0x18, 0},
    {Rule::BattleStart, 0x6BDE6678F8F5BCC0ull, 31, 0xF8, 0},
    {Rule::CatchupAnswer, 0x1BAC7BC9C28C7AA1ull, 8, 0xF8, 0},
    {Rule::WearAnswer, 0x560424742C6BAEFDull, 24, 0xF8, 0},
    {Rule::DrawMode, 0x9248861EE2325678ull, 23, 0x08, 0},
    {Rule::LapStartRestore, 0x2EE1C5EF1C3ADCA1ull, 72, 0x18, 0},
    {Rule::CollisionCommit, 0x9E990D1F3E9C0B83ull, 26, 0xF8, 0},
    {Rule::WallFriction, 0x133E1B68167D4E46ull, 68, 0x38, 0},
    {Rule::PushSegment, 0x8FF5067050201B02ull, 261, 0x38, 0},
    {Rule::LapUsable, 0x2AE5FFE4CBBE9DE6ull, 169, 0xF8, 0},
    {Rule::Atan2, 0xF8A8A885E4C4C245ull, 56, 0x38},
    {Rule::FloatToDouble, 0x54B1E9A170C3D8EAull, 15, 0xF8},
    {Rule::EngineCondition, 0x0000000000000000ull, 0, 0xF8},
    {Rule::SlipAngle, 0x95EA3D1A3BAD35CDull, 39, 0x38},
    {Rule::SpecBuild, 0x6B348B01EE7C83E1ull, 265, 0x18},
    {Rule::BodyBuild, 0xC4EADBE84875C50Bull, 75, 0x38},
    {Rule::DriverSpread, 0x65AAFFC21D28F10Cull, 106, 0x38},
    {Rule::MovingStart, 0x7DB9F1EEE863FDC5ull, 164, 0x38},
    {Rule::GroundMiss, 0x272DEBED44703460ull, 312, 0x38},
    {Rule::GearSpec, 0xFFC1EDA396B99BE4ull, 21, 0xF8},
    {Rule::Layout, 0x2F5FEB05A1114E2Bull, 151, 0x08},
    {Rule::Driveline, 0xB28D8018A047C57Bull, 594, 0x08},
    {Rule::AwdGain, 0x42CC4E17C1DD0BAAull, 121, 0x38},
    {Rule::DriveClass, 0xD5AE5720FCE3169Aull, 25, 0xF8},
    {Rule::AxleStep, 0xEED3164571C2DE7Full, 670, 0x08},
    {Rule::PadClutch, 0xE7223DA207AF8997ull, 98, 0x38},
    {Rule::PadControls, 0x8AE40DF209FE7F11ull, 627, 0x08},
    {Rule::DriverInput, 0xE21698342CDD8433ull, 102, 0x18},
    {Rule::AiOutputs, 0x70169590444BC9C1ull, 54, 0x38},
    {Rule::AiDecide, 0x41ACD295B1AF8294ull, 852, 0x08},
    {Rule::AiTraffic, 0x15DA5E431475EB20ull, 237, 0x38},
    {Rule::AiBraking, 0x848AC796DB9A3894ull, 483, 0x08},
    {Rule::Ascc, 0x0DFD3BE3A4D1A724ull, 253, 0x38},
    {Rule::TyreEffects, 0x2CE8EE3DE92ADF38ull, 415, 0x08},
    {Rule::GapClosing, 0x5483588C851623B1ull, 63, 0x38},
    {Rule::ContactIntensity, 0x1256D1FC34DC1987ull, 48, 0x38},
    {Rule::SlowDown, 0xF764B7D234D1D4B3ull, 16, 0x38},
    {Rule::ParamsBuild, 0xA3E11B6946241A01ull, 1057, 0x08},
    {Rule::Aids, 0x35A47C6F602E4785ull, 66, 0x18},
    {Rule::RecordStore, 0x17A217A2355C94E6ull, 14, 0xF8},
    {Rule::StuckNudge, 0xE2A0D30BDEDC93A5ull, 87, 0x38},
    {Rule::SoftDouble, 0x7D7A6C64A00BEAA2ull, 171, 0x38},
    {Rule::HybridStep, 0x5FB8B87506B3070Eull, 11, 0xF8},
    {Rule::PodMood, 0x230673D2DEDF07FAull, 187, 0x38},
    {Rule::PodStop, 0x8F179CE98399BA2Full, 25, 0xB8},
    {Rule::ActiveWing, 0x137F98BEEFFCE0C7ull, 47, 0x18},
    {Rule::FlyingGrid, 0x051A00C6CBCDF148ull, 83, 0x38},
    {Rule::StopZone, 0xB7209536207DEA08ull, 86, 0x38},
    {Rule::PodRule, 0x9326E2C03D5BD726ull, 109, 0x38},
    {Rule::PodLine, 0x2C33D2D8A19F2676ull, 51, 0xF8},
    {Rule::PodSpring, 0xC1E30827362A2CF5ull, 54, 0x38},
    {Rule::HitReport, 0xF8D19C6F3B503D3Dull, 58, 0x18},
    {Rule::PassBy, 0x0A93A48DB53224C7ull, 184, 0xB8, 0},
    {Rule::LicenceJudge, 0xE9F96DB72996D5CDull, 89, 0xB8},
    {Rule::Leaves, 0x57C3502ECCDEAB23ull, 201, 0x38},
    {Rule::SparkEmit, 0xF4492F771C6B97CDull, 205, 0x18, 0},
    {Rule::DrawMode, 0x9DDEA5A0F4D67728ull, 23, 0x10, 0},
    {Rule::Layout, 0x5DC278FB8BF3BC2Bull, 151, 0x10},
    {Rule::Driveline, 0x0055E8D82DB9DF7Bull, 594, 0x10},
    {Rule::AxleStep, 0xA9923011FB1D63FFull, 670, 0x10},
    {Rule::PadControls, 0xF0A976C1BC0FE6A0ull, 645, 0x10},
    {Rule::AiDecide, 0xA53BF14759328B14ull, 852, 0x10},
    {Rule::AiBraking, 0x65A057EFCEF6D194ull, 483, 0x10},
    {Rule::TyreEffects, 0xD70FF594F5541FB8ull, 415, 0x10},
    {Rule::ParamsBuild, 0x91D04D3C9F96E19Cull, 1057, 0x10},
    {Rule::RunUp, 0x761AD4EE39564EABull, 350, 0x20, 0},
    {Rule::DrawMode, 0xCD1062B77E31AE50ull, 23, 0xE0, 0},
    {Rule::LapStartRestore, 0xDC6E06CBC0033E8Aull, 74, 0xA0, 0},
    {Rule::SpecBuild, 0x96938492FE6AA93Bull, 301, 0xE0},
    {Rule::Layout, 0xF3ED05A35E191FEBull, 151, 0x20},
    {Rule::Driveline, 0x7880D1475793F4BBull, 594, 0x20},
    {Rule::AxleStep, 0x34535F1B0AD85C2Full, 670, 0x20},
    {Rule::PadControls, 0x81875B2496866850ull, 645, 0x20},
    {Rule::DriverInput, 0x6F5BD6C18D286AC3ull, 102, 0xE0},
    {Rule::AiDecide, 0xFA5F8CE32804FAA4ull, 852, 0x20},
    {Rule::AiBraking, 0x0A78A41E65F32D24ull, 483, 0x20},
    {Rule::TyreEffects, 0x6389CA6C90B506D8ull, 415, 0x20},
    {Rule::ParamsBuild, 0x6605C4FFB675255Cull, 1059, 0x20},
    {Rule::Aids, 0x502B2BF87F954EC2ull, 59, 0xE0},
    {Rule::ActiveWing, 0x1E104A01AE74BCB7ull, 85, 0x20},
    {Rule::HitReport, 0x17253F4236B800EDull, 58, 0xE0},
    {Rule::SparkEmit, 0xB16B81B73B0A0617ull, 224, 0x20},
    {Rule::RubberBand, 0xB59493ADBD945294ull, 66, 0xC0, 1},
    {Rule::Oversteer, 0xD457DF52DF31EA09ull, 206, 0xC0, 1},
    {Rule::PadRumble, 0x42A83898EC227E63ull, 147, 0xC0, 1},
    {Rule::SteerForce, 0x93B723707B51BCCBull, 394, 0x40, 1},
    {Rule::LicenceStop, 0xA33BEA901B15ABE8ull, 35, 0x40},
    {Rule::AngleWrap, 0xA45D5724A2834BD8ull, 31, 0xC0},
    {Rule::RunUp, 0x757F2115ED52ADD5ull, 346, 0x40, 0},
    {Rule::LapStartRestore, 0xCB39CB918718BFD8ull, 74, 0x40, 0},
    {Rule::WallFriction, 0x042ED5AE0A10B324ull, 64, 0xC0, 0},
    {Rule::PushSegment, 0x61695A38549644C0ull, 249, 0xC0, 0},
    {Rule::Atan2, 0xB49C2DDCB3CC94A8ull, 50, 0xC0},
    {Rule::SlipAngle, 0x2294C4346A8C32FBull, 37, 0xC0},
    {Rule::BodyBuild, 0x18B1EEC726EB9312ull, 75, 0xC0},
    {Rule::DriverSpread, 0x97EECFEB15ED4C36ull, 98, 0xC0},
    {Rule::MovingStart, 0x4EB0D95AEE629EFCull, 156, 0xC0},
    {Rule::GroundMiss, 0x312B87FCFE421ECAull, 302, 0xC0},
    {Rule::Layout, 0xFE222ACD27354A79ull, 141, 0x40},
    {Rule::Driveline, 0x31E78789B53F9732ull, 568, 0x40},
    {Rule::AwdGain, 0x5E30169FF36380ACull, 104, 0xC0},
    {Rule::AxleStep, 0x743F06C2558B7B3Eull, 629, 0x40},
    {Rule::PadClutch, 0xDF4F883D738A7917ull, 93, 0xC0},
    {Rule::PadControls, 0xA1FC29448830D357ull, 604, 0x40},
    {Rule::AiOutputs, 0xB555B324775AA7DEull, 50, 0xC0},
    {Rule::AiDecide, 0xD657A39CF6C4FC48ull, 851, 0x40},
    {Rule::AiTraffic, 0x552DE717F840A61Cull, 233, 0x40},
    {Rule::AiBraking, 0x3F623FCCC818B8A8ull, 457, 0x40},
    {Rule::Ascc, 0xF50F39A69CBE7C6Aull, 241, 0xC0},
    {Rule::TyreEffects, 0xEB8821EA936B85FCull, 396, 0x40},
    {Rule::GapClosing, 0x7BA6CDAFBC7114C5ull, 57, 0xC0},
    {Rule::ContactIntensity, 0x3524A2CA6503C786ull, 39, 0xC0},
    {Rule::SlowDown, 0x9F2DC7110C048A5Aull, 14, 0xC0},
    {Rule::ParamsBuild, 0xCDEB0A63AF693FD0ull, 1003, 0xC0},
    {Rule::StuckNudge, 0xA42F2041EAF5A5BFull, 82, 0xC0},
    {Rule::SoftDouble, 0x889387251652E5B8ull, 161, 0xC0},
    {Rule::PodMood, 0xF225E4CBBA59345Aull, 185, 0x40},
    {Rule::PodStop, 0xA541C4BDA734686Dull, 25, 0x40},
    {Rule::ActiveWing, 0x65701348C5EFE1DCull, 80, 0xC0},
    {Rule::FlyingGrid, 0x0000000000000000ull, 0, 0xC0, 0},
    {Rule::StopZone, 0x845FD6F39EB2BFE0ull, 80, 0xC0},
    {Rule::PodRule, 0x95986D40D7A1B178ull, 105, 0xC0},
    {Rule::PodSpring, 0x77E5E8DA273181ACull, 54, 0x40},
    {Rule::PassBy, 0x792C87B10E748DF7ull, 184, 0x40, 1},
    {Rule::LicenceJudge, 0x13BC0277FC9D3CB5ull, 89, 0x40},
    {Rule::Leaves, 0xA6487F5D69E8A1CEull, 195, 0xC0},
    {Rule::SparkEmit, 0x8251843B7EF882B5ull, 221, 0xC0},
    {Rule::SteerForce, 0x5528D80811A12BDEull, 392, 0x80, 1},
    {Rule::LicenceStop, 0x5589F253EED98A75ull, 35, 0x80},
    {Rule::RunUp, 0xEAF6288B3828B05Full, 346, 0x80, 0},
    {Rule::Layout, 0xF0C1F91059C420F9ull, 141, 0x80},
    {Rule::Driveline, 0x42BAA06B169BB032ull, 568, 0x80},
    {Rule::AxleStep, 0x4AC560433BC2A7FEull, 629, 0x80},
    {Rule::PadControls, 0x2210FA8DC7F4C097ull, 604, 0x80},
    {Rule::AiDecide, 0x61B162FE5A3E0EA8ull, 851, 0x80},
    {Rule::AiTraffic, 0x8FE517E0957B67DAull, 233, 0x80},
    {Rule::AiBraking, 0x1694472D4F8D4928ull, 457, 0x80},
    {Rule::TyreEffects, 0x2798ABC8D0FBC075ull, 396, 0x80},
    {Rule::PodMood, 0xC1C1E72610317C32ull, 185, 0x80},
    {Rule::PodSpring, 0x414275F46DFBB6F3ull, 53, 0x80},
};

// A table's address from the window at `at`: its upper half plus the offset added to it (or one offset from the global
// pointer).
bool decode(const CodeIndex& x, u32 at, const TableSite& s, u32& out) {
    const u32 lo = x.word(at + 4 * s.lo), lo_op = lo >> 26, lo_rs = (lo >> 21) & 31;
    // The offset added, or (a later build's compiler) the offset of a load or store through the upper half.
    const bool access = lo_op >= 0x20 && lo_op != 0x2F && lo_op != 0x33; // (through the global pointer too: hi = lo)
    if (lo_op != 0x09 && !access) return false;
    const u32 off = u32(s32(s16(lo & 0xFFFF)));
    if (s.hi == s.lo) {
        if (lo_rs != 28 || !x.gp()) return false;
        out = x.gp() + off - s.off;
        return true;
    }
    const u32 hi = x.word(at + 4 * s.hi);
    if (hi >> 26 != 0x0F || ((hi >> 16) & 31) != lo_rs) return false; // the upper half, in the offset's base register
    out = ((hi & 0xFFFF) << 16) + off - s.off;
    return true;
}

// Whether later build p has a later build's own table: the special kind 7 ride's only 2002 Tokyo-Geneva's two builds, the
// single race's flying start grid only the builds before them; the others every later build.
constexpr bool own_table(Program p, ProgramData::Table t) {
    if (t == ProgramData::Table::Kind7Ride) return p >= Program::ConceptGenevaEu;
    if (t == ProgramData::Table::FlyingStart) return p <= Program::ConceptSeoul2;
    return true;
}

// Whether a later build races on its own rules: its core complete (every routine that differs from GT3's ported and its
// whole race step proven on the disc [physics §14]). The others race on GT3's core until theirs is.
constexpr bool own_core_ready(Program p) {
    return p == Program::ConceptTokyo || p == Program::ConceptSeoul || p == Program::ConceptSeoul2 || p == Program::ConceptGenevaEu ||
           p == Program::ConceptGenevaAsia;
}

int lowest(u8 mask) {
    for (int p = 0; p < kPrograms; ++p)
        if (mask >> p & 1) return p;
    return 0;
}

} // namespace

const char* ProgramData::what(Table t) { return size_t(t) < std::size(kTableDefs) ? kTableDefs[size_t(t)].what : "?"; }

ProgramData ProgramData::load(ByteSpan program, u32 load_address, u32 entry_point) {
    static_assert(std::size(kTableDefs) == kAllTables_n, "a definition per table, in Table's order");
    ProgramData d;
    const CodeIndex x(program, load_address, entry_point);
    std::array<u32, kAllTables_n> at{};
    // What each recognised piece says the program is (a bit per release), for the program's identity.
    std::vector<u8> votes;
    // A table GT3's code places (by_code: its routine's address, or a fixed offset from such a table) only needs what the
    // engine needs of it; one found by its content must also look like GT3's.
    std::array<bool, kAllTables_n> by_code{};
    auto placed = [&](const TableDef& t, u32 a) {
        const u32 n = size(t.id);
        return a >= x.text_end() && x.in_image(a, n) && (!t.limit || t.limit(x.bytes(a, n)));
    };
    auto fits = [&](const TableDef& t, u32 a) { return placed(t, a) && t.check(x.bytes(a, size(t.id))); };

    // Tier 1: the table's routine, the address from its instructions (the later builds' own tables too).
    for (size_t i = 0; i < kAllTables_n; ++i) {
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
    std::array<std::array<u8, kPrograms>, kRules> as{};
    for (auto& a : as) a.fill(kOwn);
    for (const RuleSite& s : kRuleSites) {
        if (s.words == 0) {
            absent[size_t(s.rule)] |= s.programs;
            for (int p = 0; p < kPrograms; ++p) // (a later build without the routine, as a GT3 release without it)
                if ((s.programs >> p & 1) && s.as != kOwn) as[size_t(s.rule)][size_t(p)] = s.as;
        } else if (const std::vector<u32> w = x.find(s.hash, s.words); !w.empty()) {
            found[size_t(s.rule)] |= s.programs;
            for (int p = 0; p < kPrograms; ++p)
                if (s.programs >> p & 1) as[size_t(s.rule)][size_t(p)] = s.as;
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
    // A later build races on GT3's core until its own is complete (own_core_ready): NTSC-U's rules and GT3's tables where
    // its own are not placed, as any build no one has mapped. Its own rules (rules_) are what set_behaviour() gives for them.
    const bool own_core = gt3_program(d.program_) || own_core_ready(d.program_);
    const Program base = gt3_release(d.program_);

    // Each rule: the program's own variant, else the one variant found, else the program's release's (an issue: a build's
    // routine of its own, its nearest release's rules the closest guess).
    d.rules_ = {d.program_, 60};
    bool all_rules = true;
    for (int r = 0; r < kRules; ++r) {
        const Rule rule = Rule(r);
        Program v = d.program_;
        bool ok = true;
        // (a release's known lack of a routine tells only a program something else was recognised in)
        if ((found[size_t(r)] & self) || (!found[size_t(r)] && (absent[size_t(r)] & self) && !votes.empty())) v = d.program_;
        else if (found[size_t(r)]) v = Program(lowest(found[size_t(r)]));
        else ok = false, v = base;
        // A later build's routine with a GT3 release's rules.
        if (ok && as[size_t(r)][size_t(v)] != kOwn) v = Program(as[size_t(r)][size_t(v)]);
        d.recognised_[size_t(r)] = ok;
        if (!ok) {
            all_rules = false;
            d.issues_.push_back(std::string(rule_name(rule)) + ": not a known release's code; " + program_name(base) + "'s rules");
        }
        d.rules_.variant[size_t(r)] = v == d.program_ ? Behaviour::kFollow : u8(v);
    }
    d.own_ = own_core ? d.rules_ : Behaviour{base, 60};
    d.behaviour_ = d.own_;

    // The tables: GT3's values, the variant of the release the disc's table equals; one GT3's code places but unlike every
    // release's is changed on this disc and its own values serve; one found only by its content and unlike every release's,
    // or not found, an issue: GT3's values of the program's release.
    bool all_tables = true;
    for (size_t i = 0; i < kTables_n; ++i) {
        const TableDef& t = kTableDefs[i];
        d.ranges_[i].where = at[i];
        Program v = base;
        bool matched = false;
        ByteSpan b;
        if (at[i]) {
            b = x.bytes(at[i], size(t.id));
            for (int p = 0; p < kGt3Programs && !matched; ++p) {
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
    // The later builds' own tables: the disc's bytes where its routine places them (GT3's releases have none); one that sits
    // beside one of GT3's tables in every later build, at that one's place (tier 2), on a later build only.
    for (size_t i = kTables_n; i < kAllTables_n; ++i) {
        if (const TableDef& t = kTableDefs[i]; !at[i] && t.base != T::Count && concept_program(d.program_) && own_table(d.program_, T(i))) {
            const size_t j = size_t(t.base);
            if (const u32 a = at[j] + u32(t.offset); at[j] && by_code[j] && fits(t, a)) at[i] = a, d.tiers_[i] = 2;
        }
        d.ranges_[i].where = at[i];
        if (at[i]) {
            const ByteSpan b = x.bytes(at[i], size(T(i)));
            d.ranges_[i].bytes.assign(b.begin(), b.end());
        } else if (concept_program(d.program_) && own_table(d.program_, T(i)) && agree != 0 && !votes.empty()) {
            d.issues_.push_back(std::string(kTableDefs[i].what) + ": not found on this disc");
            all_tables = false;
        }
    }
    d.known_ = agree != 0 && !votes.empty() && all_tables && all_rules && own_core;
    for (const std::string& s : d.issues_) LOG_WARN("CORE.GT3: %s", s.c_str());
    if (!d.known_ && concept_program(d.program_) && agree != 0 && !votes.empty())
        LOG_INFO("CORE.GT3: %s's program; it races on GT3's core", program_name(d.program_));
    else if (!d.known_) LOG_WARN("CORE.GT3: not a known release's program; nearest %s", program_name(d.program_));
    return d;
}

ProgramData ProgramData::load(const CoreImage& core) {
    return load(ByteSpan(core.program.data(), core.program.size()), core.load_address, core.entry_point);
}

ProgramData ProgramData::synthetic(Program p) {
    ProgramData d;
    d.program_ = p, d.known_ = true;
    d.matched_.fill(true), d.releases_.fill(gt3_release(p));
    d.rules_ = d.own_ = d.behaviour_ = {p, 60};
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
    return b.rate == 60 || (b.rate == 50 && release_rate(b.rules) == 50);
}

void ProgramData::set_behaviour(const Behaviour& b) {
    if (!can_behave(b))
        throw std::invalid_argument(std::string("ProgramData: ") + program_name(b.rules) + " rules at " + std::to_string(b.rate) +
                                    " steps a second on the " + program_name(program_) +
                                    " program (the rules of NTSC-U or the disc's release; 50 only with a 50 Hz release's)");
    behaviour_ = b;
    // The disc's own rules: with the routines its program has of another release's.
    if (b.rules == program_ && b.follows_rules()) behaviour_.variant = rules_.variant;
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
    for (int r = 0; r < kGt3Rules; ++r) mix(rules_.variant[size_t(r)]);
    for (int r = kGt3Rules; r < kRules; ++r) // (the later builds' rules: only where one is not the program's own)
        if (rules_.variant[size_t(r)] != Behaviour::kFollow) mix(u8(r)), mix(rules_.variant[size_t(r)]);
    // (a later build on its own core: a race recorded on GT3's core is another program's)
    if (concept_program(program_) && own_.rules == program_) mix(0xC0), mix(0x5E);
    // (and on its own store layout for the races' solo recorders, Rule::RecordStore: a record made when they had GT3's is
    // another program's)
    if (!gt3_program(own_.rule(Rule::RecordStore))) mix(0x1B), mix(0x04);
    for (size_t i = 0; i < kTables_n; ++i) {
        mix(u8(i));
        for (u8 c : ranges_[i].bytes) mix(c);
    }
    for (size_t i = kTables_n; i < kAllTables_n; ++i) { // (the later builds' own tables: only where the disc has one)
        if (ranges_[i].bytes.empty()) continue;
        mix(u8(i));
        for (u8 c : ranges_[i].bytes) mix(c);
    }
    return h;
}

} // namespace gt3r::sim::exact
