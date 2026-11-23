#pragma once

// The original's race step set up from the user's disc (shared by diff_race_contacts.cpp and diff_race_walls.cpp).
//
// setup_original_race() builds what the conductor step 0x1F39D8 needs, all with the original's own code: the course container
// (course file blocks relocated by 0x1A8D18; RaceCourse points at it), the paramdb holder with the event record (0x2315D8), one race
// entry per car label (CarEquipments 0x2312B0 / 0x231BC0, then an event's entry (table 31) as the race information builds it,
// 0x231C50 / 0x234150, else 0x231DB0's set-car; the driving-aid switches as the entry
// constructor 0x1771B8 leaves them, ASM and TCS on), a conductor of the race mode (DynamicsConductorSinglePlayer, ctor
// 0x1F2748, unless another is asked for), the event configuration (0x1F35B8) and the conductor's initialisation (its virtual 23,
// 0x1F3628 or the licence conductor's 0x1F0B18: physics globals, AI course object, per-car specs, grid placement, collision
// manager). Every entry is a CPU entry (control source 2). The entries' and the race information's time records and the result
// words are as their constructors and a run's start leave them (no times; the result -2).

#include "ee.h"
#include "program_map.h"

#include "base/bytes.h"

#include <functional>
#include <string>
#include <vector>

namespace gt3r::data {
class CarDatabase;
struct Event;
} // namespace gt3r::data

namespace gt3r::diff::race_setup {

// ---- original program addresses ---------------------------------------------------------------------------------------------
// NTSC-U addresses (Pa: the disc's program's where they are used, program_map.h).
constexpr Pa kEqCtor = 0x002312B0, kEqInit = 0x00231BC0, kEqSetCar = 0x00231DB0, kClassCheck = 0x00235F88;
// CarEquipments' part setters, run after a part is replaced (model_car_params_exact's part overrides).
constexpr Pa kSetParts[] = {0x231FC8, 0x232080, 0x2320C8, 0x232138, 0x2321A8, 0x232208, 0x232400, 0x2324D0, 0x232570, 0x2369A8};
constexpr Pa kDbRecord = 0x002315D8;          // (db holder, table, u64 key) -> record
constexpr Pa kVariantParts = 0x00231C50;      // (EQ, variant record): its parts and the part setters
constexpr Pa kVariantSettings = 0x00234150;   // (EQ, variant record): its settings bytes over the setters'
constexpr Pa kCourseRelocate = 0x001A8D18;    // course container relocator
constexpr Pa kCondCtor = 0x001F2748;          // DynamicsConductorSinglePlayer ctor
constexpr Pa kRaceSetup = 0x001F35B8;         // (conductor, race info): event config block
constexpr Pa kCondInit = 0x001F3628;          // DynamicsConductor::vf23 (conductor, config byte)
constexpr Pa kCondStep = 0x001F39D8;          // per-tick race step (conductor, first car)
constexpr Pa kAirDensityGlobal = 0x00352920;
constexpr Pa kSinglePlayerVtable = 0x0033D920;
// The race modes' conductors: constructor and vtable (conductor + 0x8000 + 0x63A8).
struct ConductorType {
    const char* name;
    Pa ctor, vtable;
};
constexpr ConductorType kConductors[] = {
    {"single player", 0x001F2748, 0x0033D920}, {"time trial", 0x001F1AD8, 0x0033D548},   {"licence", 0x001F0AD8, 0x0033D1F0},
    {"free run", 0x001F1190, 0x0033D390},      {"battle 2P", 0x001EFFD0, 0x0033CD10},    {"battle MP", 0x001F03A8, 0x0033CEB0},
    {"machine test", 0x001F05F8, 0x0033D050},  {"free practice", 0x001F2010, 0x0033D6E8},
};
// Whether the disc's program has conductor kConductors[i]: NTSC-J has one battle conductor (DynamicsConductorBattle, the
// 2-player one's virtuals), no i.LINK battle's.
// A later build (Gran Turismo Concept): the conductors whose constructor and vtable the version table has, less the free
// run's (its initialisation calls into the race object, whose stand-in here is laid out as GT3's).
inline bool has_conductor(size_t i) {
    if (sim::concept_program(program())) return i != 3 && has_pa(kConductors[i].ctor.ntsc_u) && has_pa(kConductors[i].vtable.ntsc_u);
    return !(program() == Program::NtscJ && i == 5);
}

// ---- layout -----------------------------------------------------------------------------------------------------------------
constexpr u32 kCondSize = 0x10000, kK8 = 0x8000;
constexpr u32 kCarBase = 0xC8, kF = 0xF84, kDrv = 0xEC8; // a car slot's size: car_stride() (program_map.h)
constexpr Co kCm = 0xBBB4;
constexpr u32 kBody0 = 0xC, kBodyStride = 0x4F0;
constexpr u32 kEntryEq = 0x20;
constexpr struct EntrySize {
    operator u32() const { return entry_size(); } // 0x2200; the later builds' entries are longer (program_map.h)
} kEntrySize;
// The database's tables are asked for by their place in the disc's file (CarDatabase::place_of: Gran Turismo Concept's
// last six are in another order).

inline u32 car_addr(u32 cond, int i) { return cond + kCarBase + car_stride() * u32(i); }
inline u32 body_addr(u32 cond, int i) { return cond + kCm + kBody0 + kBodyStride * u32(i); }

// CarEquipments slot of database part index i (0..26).
u32 eq_slot_for(size_t part_index);

// Course container with the blocks the race code reads (slots 28 GTRW, 31 AI lines, 32 Env0, 35 CRSS, 37-40 the AI driver sets per
// driveline class), relocated. 0 on failure.
u32 load_course(Ee& ee, ByteSpan file, std::string& err);

// A database holder's database (CarDataBase: +0 the database's bytes in memory). From 2002 Tokyo-Seoul v2.00 on the holder
// also keeps, at +4, an index its loader builds over each of the 30 tables: 256 chain heads (u16, 0xFFFF: none) by the
// sum of the bytes of a record's id plus its byte +0x10, then a next-record word per record; the part lookup of a car's
// equipment walks it. Built here as the loader leaves it (the loader itself reads the file).
void set_database(Ee& ee, u32 holder, u32 gtar);

// Race entry for a car label (table 30 car or table 31 variant) at `entry` (kEntrySize bytes).
bool build_entry(Ee& ee, u32 gtar, const data::CarDatabase& db, const std::string& label, u32 entry, std::string& err);

struct OrigRace {
    u32 cond = 0, info = 0, holder = 0, gtar = 0, course = 0, container = 0;
    int ncars = 0;
};

// The race entries' driving-aid switches, ASM (entry +0x20F0) and TCS (+0x20F4); setup_original_race() leaves both on, as the entry
// constructor does. The car initialisation 0x1DDAD0 reads them: ASM 0 clears the spec's ASCC channel flags (0x109), TCS 0 its TCS gain
// (0x110), so a player-controlled car (control source 0) runs without them; CPU cars use the driver's aid block either way. Call it
// from setup_original_race()'s `before_init`.
void set_entry_aids(Ee& ee, const OrigRace& o, bool asm_on, bool tcs_on);

// Resets the interpreter (RAM, HLE stubs, probes), installs the libc and class-check stubs and sets everything up as described
// above, with `labels` in grid order, `laps` laps and a start hold of `hold` frames (race info +0x5C). `before_init` runs after
// the event configuration and before the conductor's initialisation (probes that must see the initialisation). `config`: the
// configuration byte the initialisation is given (virtual 23's a1, kept at conductor + 0x8000 + 0x37AC: 3 / 2 put a player's
// entry under the AI, 2 also varies the CPU drivers by grid position).
bool setup_original_race(Ee& ee, const data::CarDatabase& db, const Bytes& gtar_bytes, const data::Event& ev, ByteSpan course_file,
                         const std::vector<std::string>& labels, int laps, int hold, OrigRace& o, std::string& err,
                         const std::function<void(Ee&, const OrigRace&)>& before_init = {}, const ConductorType& conductor = kConductors[0],
                         int config = 0);

} // namespace gt3r::diff::race_setup
