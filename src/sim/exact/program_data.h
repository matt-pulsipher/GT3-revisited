#pragma once

// The game program's own data that the tick's inputs are built from (race_inputs.h): the static physics block, the tyre
// effect block, the controller settings, two surface tables and the race start's, the car spec's and the race flow's
// tables (Table). GT3's values are in the source (program_tables.h: the same in its three releases but for PAL's HUD
// message times).
//
// The disc's program is checked against them, any build the same way, the four known discs and builds no one has mapped:
// each table is found through the routine that addresses it, recognised by a hash of its code with everything that moves
// between builds masked (program_locate.h), its address decoded from that routine's instructions (tier 1; a table at a
// fixed offset from one found so is placed by GT3's code too); a table whose routine is not found is searched for in the
// program's data by its shape (sizes, ranges, sorted breakpoints, the table it is part of: its content check) and taken
// only when exactly one place passes (tier 2). A table GT3's code places is the disc's, with any values the engine can
// take (a modified game's own tuning: changed()); one found only by its shape must equal a release's, and one not found
// or unlike every release's is an issue and GT3's values of the program's release serve. The routines whose rules differ
// between the releases are recognised the same way (Behaviour::variant); one no known release has keeps the program's
// release's rules (an issue). b() / h() / w() read a table by its id and an offset.

#include "base/bytes.h"
#include "sim/exact/program.h"

#include <string>
#include <vector>

namespace gt3r {
struct CoreImage;
}

namespace gt3r::sim::exact {

class ProgramData {
public:
    // The program's tables the exact core reads (what each holds; their sizes: size()).
    enum class Table : u8 {
        Static,           // the static physics block
        Effects,          // the tyre effect and engine sound block
        Controller,       // the controller settings
        SurfaceClass,     // a surface's class for the dirt lock curve, a byte per surface
        Soft,             // a word per material: 1 soft (grass, gravel, sand)
        DriverSets,       // the built-in AI driver parameter sets 0..2, 32 bytes each [ai §9.1]
        LineFallback,     // per requested AI line 0..7 the six lines tried, -1 ending the row [ai §2.1]
        InitialLines,     // the three candidates for a car's initial line in the order they are tried (s32)
        LineOrder,        // the order the line loader goes through the lines
        GearKinds,        // a word per gear record +0x10 byte
        SpecCurves,       // the spec's default steering-wheel lock curve (x 6 bytes at +0, y at +8) and tyre curve B
                          // (x at +0x10, y at +0x18)
        SpecCurveB,       // the tyre curve B's default alone (SpecCurves + 0x10)
        SpecSurfaceClass, // the tyre blocks' surface classes, a byte per surface
        LsdTypes,         // the differential type letters
        Messages,         // the HUD messages, 8 bytes each: +4 the display time in steps (PAL's 100 where NTSC's are 120)
        MachineSectors,   // the machine test's sector starts (4 floats)
        StickCurve,       // the sticks' response curve
        EffectKinds,      // the tyre effect emitter's: a surface's effect kind (16 words)
        EffectScales,     // ... and the effect scales (8 floats)
        EffectParams,     // the tyre effect parameters every race's initialisation writes into the course (11 x 8 bytes)
        Count
    };
    static constexpr int kTables = int(Table::Count);
    static constexpr u32 size(Table t) {
        constexpr u32 sizes[kTables] = {0x120, 0x210, 0x48, 16, 64, 3 * 32, 8 * 6, 3 * 4, 8, 0x58 * 4,
                                        0x1E,  0xE,   16,   8,  16 * 8, 4 * 4, 0x100, 16 * 4, 8 * 4, 11 * 8};
        return sizes[size_t(t)];
    }

    // Checks the program `program` (the image loaded at `load_address`, its start-up code at `entry_point`) and serves the
    // tables of its release. Never throws for a build it does not know: what it cannot find or recognise is in issues().
    static ProgramData load(ByteSpan program, u32 load_address, u32 entry_point = 0);
    static ProgramData load(const CoreImage& core);
    // Every table zero and release p's, nothing located, its rules (the disc-free tests fill the tables with poke()).
    static ProgramData synthetic(Program p = Program::NtscU);

    // What could not be found or recognised and what the disc changes, one line each ("the static physics block: not found
    // on this disc; GT3's values", "the rubber band: not a known release's code; PAL's rules", "the static physics block:
    // changed on this disc"). Empty for the known discs.
    const std::vector<std::string>& issues() const { return issues_; }
    // How each table was found on the disc: 1 its routine, 2 its content, 0 not found.
    int tier(Table t) const { return tiers_[size_t(t)]; }
    // The table's start in this program (0: not found).
    u32 table_start(Table t) const { return ranges_[size_t(t)].where; }
    // The release whose values a table has (the disc's table equals them; else the program's release, whose variant serves
    // and whose step counts the disc's own values are in), and whether the disc's table matched one at all.
    Program table_release(Table t) const { return releases_[size_t(t)]; }
    bool table_matched(Table t) const { return matched_[size_t(t)]; }
    // A table GT3's code places whose values are unlike every release's: the disc's own values serve (a modified game).
    bool table_changed(Table t) const { return changed_[size_t(t)]; }
    bool changed() const;
    // This program data with GT3's values in every changed table (the program's release's): what races every machine and
    // disc must run the same take, online races and the game's own recorded demos. A copy equal to this one (the same
    // fingerprint) when nothing is changed.
    ProgramData gt3_tables() const;
    // What a table holds, for the messages ("the static physics block").
    static const char* what(Table t);
    // Whether routine r was recognised (one of the known releases' variants, or a release's known lack of it).
    bool recognised(Rule r) const { return recognised_[size_t(r)]; }
    // Where the program's code was recognised (0: not): a table's routine window (`words` long, the two halves of the
    // table's address at instructions hi and lo) and a rule's routine.
    struct Site {
        u32 at = 0, words = 0, hi = 0, lo = 0;
    };
    Site table_site(Table t) const { return sites_[size_t(t)]; }
    u32 rule_site(Rule r) const { return rule_at_[size_t(r)]; }

    // The disc's program: the known release whose code it has (an unknown build: the release most of its recognised
    // routines and tables match; known() false: then GT3's tables where its own are not placed by GT3's code and, for each
    // routine not recognised, that release's rules). A known program may still have changed tables (changed()).
    Program program() const { return program_; }
    bool known() const { return known_; }
    // How the races behave (sim/program.h; the builders here follow it, the caller hands it to Car::behaviour,
    // collide::exact::Body::behaviour and RaceFlowInputs::behaviour and steps the race at steps_per_second()). By default the
    // disc's own release's rules at 60 steps a second (with the routines it has of another release's: Behaviour::variant);
    // online races NTSC-U's rules; a release's own demo streams its whole behaviour (release_behaviour: PAL at 50 steps a
    // second). At 60 steps a second the tables whose values follow the rate read at that rate (PAL's HUD message times, 100
    // steps, as 120). Allowed: the rules of NTSC-U or of the disc's own release, the rate 50 only with PAL's rules (else
    // std::invalid_argument).
    Behaviour behaviour() const { return behaviour_; }
    bool can_behave(const Behaviour& b) const;
    void set_behaviour(const Behaviour& b);

    // Bytes of table t at offset `off` (std::out_of_range past its end or for a table not found).
    u8 b(Table t, u32 off = 0) const;
    u16 h(Table t, u32 off = 0) const;
    u32 w(Table t, u32 off = 0) const;
    // Changes a byte of the copy.
    void poke(Table t, u32 off, u8 v);
    // Where a table byte is in this program.
    u32 address(Table t, u32 off) const;
    // A hash of every copied table (their addresses and bytes) and of the routines' variants: two programs with the same
    // fingerprint build and run every race the same (a replay keeps it and refuses another program's).
    u64 fingerprint() const;

private:
    struct Range {
        u32 where = 0; // the table's start in this program (0: not found)
        std::vector<u8> bytes;
    };
    Program program_ = Program::NtscU;
    bool known_ = false;
    Behaviour behaviour_, own_; // own_: the disc's own rules with its routines' variants
    std::array<Range, kTables> ranges_{};
    std::array<int, kTables> tiers_{};
    std::array<Program, kTables> releases_{};
    std::array<bool, kTables> matched_{};
    std::array<bool, kTables> changed_{};
    std::array<Site, kTables> sites_{};
    std::array<bool, kRules> recognised_{};
    std::array<u32, kRules> rule_at_{};
    std::vector<std::string> issues_;
    const u8* at(Table t, u32 off, u32 n) const;
};

} // namespace gt3r::sim::exact
