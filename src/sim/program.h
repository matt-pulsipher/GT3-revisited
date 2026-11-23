#pragma once

// The game program versions the exact core knows (sim/exact/program_data.h tells them apart) and how a race behaves.
// Where a ported routine differs between the releases, the port branches on the race's Behaviour: PAL runs the race step
// 50 times a second where the NTSC programs run it 60 times (its step length and every tick count differ: the rate), and
// PAL and NTSC-J also change some routines' rules (PAL's rubber band cap, NTSC-J's collision commit, ...). The differences
// are listed with the routines that have them.

#include "base/bytes.h"

#include <array>

namespace gt3r::sim {

// GT3's three releases, then the later builds of the same source the core knows, in the order they were made: Gran Turismo
// Concept 2001 Tokyo, 2002 Tokyo-Seoul (v1.02 and v2.00) and 2002 Tokyo-Geneva (the European and the Asian disc). The later
// builds descend from GT3's PAL release and were built with another compiler [physics §14].
enum class Program : u8 { NtscU, Pal, NtscJ, ConceptTokyo, ConceptSeoul, ConceptSeoul2, ConceptGenevaEu, ConceptGenevaAsia };
constexpr int kPrograms = 8;
constexpr int kGt3Programs = 3; // NTSC-U, PAL, NTSC-J: the releases whose tables are in the source (program_tables.h)
// "NTSC-U", "PAL", "NTSC-J", "Concept 2001 Tokyo", ...
const char* program_name(Program p);
constexpr bool gt3_program(Program p) { return int(p) < kGt3Programs; }
constexpr bool concept_program(Program p) { return !gt3_program(p); }
// The GT3 release whose tables a program's tables are compared with first and whose values serve where its own are not
// placed: the program itself, or NTSC-U for a later build (PAL's HUD message times are matched by their values).
constexpr Program gt3_release(Program p) { return gt3_program(p) ? p : Program::NtscU; }

// The routines whose rules (not their rate) differ between the releases, by their NTSC-U address. Each is followed as the
// release the race's rules name has it, or as the disc's program has it where ProgramData recognised another release's
// variant of that one routine (a build of its own: Behaviour::variant).
enum class Rule : u8 {
    RubberBand,      // 0x1ECEB8, the conductor's virtual 4: PAL caps every race mode's time scale at 1.3
    BattleCap,       // 0x1F0478, the i.LINK battle's virtual 4: its own cap (not in PAL's; NTSC-J has no such conductor)
    Oversteer,       // 0x1E3708, the AI's oversteer indicator: PAL's is 0 below 25 km/h
    PadRumble,       // 0x1DF2B0, the pad vibration: PAL leaves a car the AI drives at 0
    SteerForce,      // 0x1E0068, the steering-wheel force: PAL leaves a car the AI drives at 0x80
    LicenceStop,     // 0x1EF488, the licence's stop: PAL's speed 1.5 km/h
    LapRecord,       // 0x1EFDE8, a new record: PAL's needs a lower time
    SpeedEvent,      // 0x1EC8C0, the machine test's top speed: PAL's nothing for a ghost
    RecordsLocked,   // 0x1EC4E8, a car's times kept out of the records: PAL's callers test a ghost first
    LapTime,         // 0x1EC620, a lap time's events and records (PAL: a ghost's; NTSC-J: no general time event, locks)
    SectorTime,      // 0x1EC520, a sector time's (likewise)
    SectorDiff,      // 0x1EC758, a sector's difference to the best: NTSC-J's also with the race-flow byte
    LapDiff,         // 0x1EC818, a lap's difference (likewise)
    AngleWrap,       // 0x1D8C00, the angle wrap: NTSC-J's a turn at a time
    RunUp,           // 0x1DDAD0, a player's run-up start: NTSC-J's has no shift lock-out
    BattleStart,     // 0x1F0010, the split-screen battle's start: NTSC-J's one battle conductor, two cars only
    CatchupAnswer,   // 0x1F0098, the battle's catch-up answer: NTSC-J's setting 2 for any set word
    WearAnswer,      // 0x1F00C0, the battle's wear answer: NTSC-J's the word itself as the setting
    DrawMode,        // 0x1E0668, the car's draw mode: NTSC-J's has no race info + 0x70 test
    LapStartRestore, // 0x1EE6D8, the replay's lap-start restore: NTSC-J's does not rebuild the collision body
    CollisionCommit, // 0x1FB6D8, the collision commit: NTSC-J's no heading wrap, no yaw-rate clamp
    WallFriction,    // 0x1FE660, the wall friction: NTSC-J's without the zero-length tests
    PushSegment,     // 0x1FDA10, the wall push-out: NTSC-J's takes the root of a zero length too
    LapUsable,       // 0x1F16B8, the free run's lap handler: NTSC-J's lap check reads the player's device's own recording
    // The later builds' changes (GT3's three releases have one variant of each):
    Atan2,           // 0x269FC8, atan2f: the later builds' is 0 only when both arguments are; 2002 Tokyo-Geneva's pi literals
    FloatToDouble,   // 0x27AFA0, the soft doubles' float-to-double: the later builds' has no infinity / NaN class
    EngineCondition, // 0x1DAD28, the engine condition factor: the later builds' is 1
    SlipAngle,       // 0x1EA348, a hub's velocity direction: the later builds' atan2f; 2002 Tokyo-Geneva's fade constants
    SpecBuild,       // 0x1DA308, the spec builder: the later builds' default for the spec's byte +0x113 is 90
    BodyBuild,       // 0x1FA740, a car's collision body: 2002 Tokyo-Geneva's chamfer constant
    DriverSpread,    // 0x1F6380, the event's driver overrides: the later builds spread the computer's drivers in every race
    MovingStart,     // 0x1DD8A8, the moving start: the later builds start a car on a straight at full throttle
    GroundMiss,      // 0x2280C8, the wheels' ground query: the later builds' height of a record off the road
    GearSpec,        // 0x232E18, the gear part's spec bytes: the later builds' take the record's +0x30 as the gearbox kind
    Layout,          // 0x1E2700, the drive layout: the later builds' AWD system 3 by a table, system 5 a layout of its own
    Driveline,       // 0x1E1C98, the driveline block: the later builds' gearbox kind and unequal-axle flag
    AwdGain,         // 0x1E5998, the AWD system 3 coupling gain: the later builds' fade by their settings, the torque's sign
                     // in reverse, the front axle's term in the full-transfer test
    DriveClass,      // 0x1F5EF0, the AI's driveline class: the later builds' AWD system 5 is class 0
    AxleStep,        // 0x1E5B98, the axle step: the later builds' stepless gearbox and layout 7 torque, the centre clutch held
                     // by the requested torque, the unequal axles' scale
    PadClutch,       // 0x1E4180, the pad's clutch request: the later builds' open below 1 km/h on the third axis (stepless)
    PadControls,     // 0x1E4478, the pad's control frame: the later builds' steering percentage, the stepless gearbox's creep
    DriverInput,     // 0x1E4D38, phase 3: the later builds filter the steering percentage of a car the AI drives
    AiOutputs,       // 0x1F79F0, the AI's outputs: the later builds' write no steering percentage; the stop's handbrake
    AiDecide,        // 0x1F7AE0, the AI's decision: the later builds' stop point; 2002 Tokyo-Geneva's start look-ahead
    AiTraffic,       // 0x1F6D70, the AI's traffic step: the later builds' cooldown and blend conditions
    AiBraking,       // 0x1F7088, the AI's braking: the later builds' throttle scale by the torques, the stop point
    Ascc,            // 0x1E2D08, the ASCC: the later builds' switches (channel C under throttle, its status)
    TyreEffects,     // 0x1DEBB0, the tyre effects: the later builds' channel 2 silent on surface 8
    GapClosing,      // 0x1EBDA0, a pair's gap and closing speed: the later builds' also for cars drawing apart
    ContactIntensity, // 0x1F6CC0, the AI's contact intensity: the later builds' speeds, time and gap
    SlowDown,        // 0x1F6518, the run-on after a finish: the later builds' throttle multiplier x 0.8, at least 0.5
    ParamsBuild,     // 0x1DBFD8, the parameter converter: the later builds' layout 7 mass; from 2002 Tokyo-Seoul on the rear
                     // ride height's own spec byte
    Aids,            // 0x1E3B18, phase 6's choice of aids: from 2002 Tokyo-Seoul v2.00 on a computer's car's status bytes are
                     // its own aid block's switches, as a pad car's the AI drives
    RecordStore,     // 0x1F4FF0, the pad recorder's store: the later builds' snapshot 4 bytes longer, the block 4 shorter
    StuckNudge,      // 0x1FB740, the collision step's stuck nudge: the later builds' only for a pad car touching something
    SoftDouble,      // 0x279480, __muldf3: the later builds' fp-bit (no denormals, no NaN class in the narrowing); 2002
                     // Tokyo-Geneva's rounds toward zero
    // Routines of the race step only the later builds have (GT3's releases: none), by their Concept 2001 Tokyo address:
    HybridStep,      // later 0x1DA0A8, after phase 1: a layout 7 car's assist byte cleared
    PodMood,         // later 0x1DB2F8, before phase 2: special car 1's mood (its lamps, lean and nod)
    PodStop,         // later 0x1DBAB0, after phase 3: special car 1's count of steps stopped on the brake
    ActiveWing,      // later 0x1DBC48, before phase 12: special car 2's wing, out above one speed and in below another
    FlyingGrid,      // later 0x1E73E0, the single race conductor's virtual 25: an event's flying start spaces the cars by a table
                     // (and its virtual 22 puts each on a line the table names)
    // The pod race (an event of test kind 5: the stop zone, special car 1's sitting):
    StopZone,        // later 0x1EE280, the conductor's initialisation: the stop zone set up from the program's two constants
    PodRule,         // later 0x1EE8C0, the single race and split-screen conductors' virtual 14: a stop counted, HUD messages
                     // 0x10 / 0x11
    PodLine,         // later 0x1EE7F0, the base and split-screen lap handlers: a car that owes two stops stops, disqualified
    PodSpring,       // later 0x1DBB28, the suspension's spring rate: special car 1's wheels lose their spring as it sits
    HitReport,       // 0x1DFEB8, the collision report: the later builds' moods of special car 1 by what it hit
    PassBy,          // 0x1F59B0, the pass-by states at the race step's end (the race's sounds'; the later builds' special car 1
                     // reads them): PAL's counts at 50 steps a second (2002 Tokyo-Geneva Europe's too), the others NTSC-U's
    LicenceJudge,    // 0x1EF780, the licence test's judge: the later builds' clears a hit and goes on, judges no computer's car,
                     // and their licence conductor answers 0 for soft ground and its pit mode (concept §9.2)
    Leaves,          // later 0x19C4E8, the particle manager's leaf maker: the tire effect emitter's call for a wheel on ground
                     // whose triangle flags have 0x20 (leaves.h)
    SparkEmit,       // 0x1AADD0, the sparks' emission: from 2002 Tokyo-Seoul v2.00 on a burst starts on and bounces off the
                     // plane of the car body's drawn ground point and attitude, not the road's (sparks.h)
    Count
};
constexpr int kRules = int(Rule::Count);
constexpr int kGt3Rules = int(Rule::LapUsable) + 1; // the routines that differ among GT3's releases; then the later builds'
// "rubber band", ... (for the messages).
const char* rule_name(Rule r);

// How a race runs on the exact core: whose rules apply (the release whose own changes to the routines are followed) and the
// step rate. Races run at 60 steps a second with the rules of the disc's release (online races: NTSC-U's); a release's own
// recorded streams (PAL's demos) play at its own rate, PAL's 50 steps a second.
struct Behaviour {
    static constexpr u8 kFollow = 0xFF;
    Program rules = Program::NtscU;
    int rate = 60; // steps a second: 60, or 50 with the rules of a release made for 50 (PAL's, Concept Tokyo-Geneva Europe's)
    // Per routine, the release whose variant the disc's program has where it is not `rules`' (ProgramData fills it for the
    // disc's own rules: a build whose routines are not all one release's); kFollow: `rules`'.
    std::array<u8, kRules> variant = follow_all();

    constexpr Program rule(Rule r) const {
        const u8 v = variant[size_t(r)];
        return v == kFollow ? rules : Program(v);
    }
    constexpr bool pal_rate() const { return rate == 50; }
    constexpr bool follows_rules() const {
        for (u8 v : variant)
            if (v != kFollow) return false;
        return true;
    }
    static constexpr std::array<u8, kRules> follow_all() {
        std::array<u8, kRules> a{};
        for (u8& v : a) v = kFollow;
        return a;
    }
    friend constexpr bool operator==(const Behaviour&, const Behaviour&) = default;
};
// How often the caller steps the race: 60 a second, PAL's own release 50.
constexpr int steps_per_second(const Behaviour& b) { return b.rate; }
// The rate of a release's own program: PAL's and Concept 2002 Tokyo-Geneva's European disc's 50 steps a second.
constexpr int release_rate(Program p) { return p == Program::Pal || p == Program::ConceptGenevaEu ? 50 : 60; }
// A release's whole own behaviour (its rules at its rate).
constexpr Behaviour release_behaviour(Program p) { return {p, release_rate(p)}; }

} // namespace gt3r::sim
