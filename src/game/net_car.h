#pragma once

// The car a player races online, as the lobby sends it (docs/dev/ONLINE.md "Cars").
//
// - Disc car: its database label.
// - Garage car (GT Mode): the base car, its 27 fitted part ids, the user settings, the
//   engine condition and wheel - about 450 bytes. Every machine rebuilds the exact spec
//   from its own disc (sim::build_car_spec), so nothing but these choices travels and
//   every player can check the car.
// - Mod car: its label plus the SHA-256 of its package (game/mod_package.h); a player
//   who lacks the car is offered the package from its owner.
// - Arcade car: a car of the arcade's class lists (database table 35): its label. It races
//   as the arcade does: the base car with the variant's parts and arcade tyres.
//
// Every descriptor also carries the digest of the car's parameter block as its owner's machine
// builds it (online_car_digest), so a machine whose disc builds that car differently (another
// region's data) can tell.

#include "base/bytes.h"
#include "disc/region.h"
#include "sim/car_spec.h"
#include "sim/exact/params_build.h"

#include <array>
#include <functional>
#include <optional>
#include <string>

namespace gt3r::data {
class CarDatabase;
}

namespace gt3r::game {

struct GarageCar;
class GtData;

struct OnlineCar {
    enum class Kind : u8 { Disc = 0, Garage = 1, Mod = 2, Arcade = 3 };
    Kind kind = Kind::Disc;
    std::string car;  // database car label (garage, arcade: the base car)
    std::string name; // display name as the owner's game shows it
    u8 paint = 0;
    // Garage car.
    std::array<u64, 27> parts{};
    sim::CarSettings settings;
    float engine_wear_m = 0, oil_m = 0; // CarCondition (engine output factor)
    std::string wheel;                  // fitted wheel (wheel/<label>), "" = stock
    // Mod car.
    std::string package; // lower-case hex SHA-256 of the car's package
    u32 package_size = 0;
    // Arcade car.
    std::string arcade; // arcade car label (table 35), e.g. "nsx_s_zero_97_290"
    // The owner's online_car_digest (absent from older games' descriptors; a protocol-3 game's held another digest, but
    // those games cannot join a protocol-4 lobby).
    std::optional<std::array<u8, 32>> digest;
};

constexpr size_t kMaxOnlineCarBytes = 1024;

Bytes encode_online_car(const OnlineCar& c);
std::optional<OnlineCar> decode_online_car(ByteSpan data); // nullopt on malformed input

// A disc or mod car from the database (kind Disc or Mod; a mod car's package fields are
// filled by the caller from game/mod_package.h).
OnlineCar online_car_from_db(const data::CarDatabase& db, const std::string& label, u8 paint);
// A GT Mode garage car.
OnlineCar online_car_from_garage(const data::CarDatabase& db, const GarageCar& g);
// An arcade class car (table 35 label); kind Disc with an empty car when the label is unknown.
OnlineCar online_car_from_arcade(const data::CarDatabase& db, const std::string& arcade, u8 paint);

// The car's spec on this machine: the disc car, the garage car rebuilt from its parts and
// settings (with its engine condition), the installed mod car, or the arcade car with its
// arcade tyres for `course` (dirt courses: the dirt tyres, race::arcade_car). nullopt with
// *error when the car (or a part) is not in this database. An arcade car is found by its
// arcade label; its base car may have another label here when this machine builds the same
// spec (the descriptor's digest), as the regional twins of a car do.
std::optional<sim::CarSpec> online_car_spec(const data::CarDatabase& db, const OnlineCar& c, std::string* error = nullptr,
                                            const std::string& course = {});

// The database car this machine draws `c` as: an arcade car's base car on this disc (a regional
// twin may carry another label than the owner's), else its own label; "" when it is not here.
std::string online_car_base(const data::CarDatabase& db, const OnlineCar& c);

// --- The same car on every machine (docs/dev/ONLINE.md "Cars") ---

// The car's parameter block P (car + 0x10, sim::exact::race_params) as an online race builds it on this machine from this
// database: the descriptor's equipment (a garage car's parts and settings, an arcade car's parts with its road tyres, a disc
// or mod car's own parts), NTSC-U's rules at 60 steps a second (every online race's), a road course, a player's entry with
// ASM and TCS on, the descriptor's engine condition. nullopt when the car cannot be built here.
std::optional<sim::exact::ParamImage> online_car_params(const data::CarDatabase& db, const sim::exact::ProgramData& d,
                                                        const OnlineCar& c);
// SHA-256 of online_car_params. The exact core builds P bit for bit alike on every platform (its platform hashes), so
// machines compare the bytes exactly: a different digest means this disc's data for the car differs where it reaches P.
std::optional<std::array<u8, 32>> online_car_digest(const data::CarDatabase& db, const sim::exact::ProgramData& d,
                                                    const OnlineCar& c);

// Another player's car on this machine.
enum class CarHere : u8 {
    Ok,
    ModMissing, // a mod car not installed here: offered for download, else a stand-in
    Unknown,    // the car, a part of it or its arcade car is not in this database
    Differs,    // this machine builds another spec (the descriptor's digest)
    NoModel,    // this disc has no body model for it
    Illegal,    // a garage car GT Mode cannot build: a part or a setting no tune shop or Settings page gives it
};
// `has_model(model)`: this disc has cars/day/<model>. `d`: this machine's program tables (null: no exact core here, the
// digest is not compared; such a machine cannot race online anyway). A descriptor without a digest is not compared. A
// garage car's parts and settings must be ones GT Mode gives it (garage_setup_problem, in this database); `why` gets
// the reason ("front spring rate out of range").
CarHere online_car_here(const data::CarDatabase& db, const sim::exact::ProgramData* d, const OnlineCar& c,
                        const std::function<bool(const std::string& model)>& has_model, std::string* why = nullptr);
// The car cannot race here (the lobby refuses Ready and the start).
inline bool car_here_blocks(CarHere h) {
    return h == CarHere::Unknown || h == CarHere::Differs || h == CarHere::NoModel || h == CarHere::Illegal;
}
// "is not on this disc", "is different on this disc", "has no model on this disc", "is not a legal setup" ("" otherwise).
const char* car_here_text(CarHere h);

// --- Arcade classes ---
// The arcade's road and rally classes (gcf tag 14) in its class page's order: 1 "CLASS_C",
// 2 "CLASS_B", 3 "CLASS_A", 4 "CLASS_S", 5 "CLASS_DIRT" (rally cars); 0 = none.
constexpr int kArcadeClasses = 5;
const char* arcade_class_tag(int cls);  // "CLASS_C" ... ("" for none)
const char* arcade_class_name(int cls); // "Class C" ... "Rally cars"
int arcade_class_index(const std::string& tag);
// The class whose list holds `label` (an arcade car, or a mod car with an [arcade] class).
int arcade_class_of(const GtData& gd, const std::string& label);

// Max power (PS) and its rpm the way the original computes the figure its menus show for a
// built car: the full-throttle torque curve times the engine condition factor `condition`
// (physics §10.3; 1 = a fresh engine) sampled every 100 rpm from the first curve point
// (rounded to 100 rpm) up to the rev limit; power = (N*m / 9.8) * rpm / 716.2.
struct PowerPeak {
    float ps = 0;
    int rpm = 0;
};
PowerPeak spec_power_peak(const sim::CarSpec& spec, float condition = 1);
// Peak power at the crank (PS) of a spec (spec_power_peak().ps).
float spec_power_ps(const sim::CarSpec& spec, float condition = 1);
// The integer a menu shows for a power in PS in the language's units: HP, BHP and CV are
// PS x 0.98619, and the value is truncated, not rounded. power_unit() is the label printed
// after it ("PS", "HP", "BHP", "CV").
int display_power(float ps, const Units& units);
const char* power_unit(const Units& units);

// Highest speed the car's gearing allows at its rev limit (m/s): no driven car goes faster on
// the flat (plausibility checks).
float geared_top_speed(const sim::CarSpec& spec);

// --- Lobby car rules (docs/dev/ONLINE.md "Lobby car rules") ---

// A lobby's rules. Each choice is 0 for "any car".
struct CarRules {
    bool allow_mods = true;  // mod cars may race
    bool stock_only = false; // no tuned garage cars (parts or settings off the stock car's)
    int max_ps = 0;          // power limit at the crank (PS, the car's own spec)
    int car_class = 0;       // 1..5: only that class's cars (car_class(); arcade_class_tag)
    int category = 0;        // 1 production cars, 2 race and rally cars (car +0xF1)
    int drive = 0;           // as GT3's events count it: 1 FF, 2 FR, 3 4WD, 4 MR, 5 RR
    int aspiration = 0;      // 1 NA, 2 turbo
    bool dirt_course = false; // the course is dirt: the arcade's class page refuses the road
                              // classes there, and the rally class on road courses
    bool any() const {
        return allow_mods && !stock_only && !max_ps && !car_class && !category && !drive && !aspiration;
    }
};
constexpr int kDriveRules = 5, kCategoryRules = 2, kAspirationRules = 2;
const char* drive_rule_name(int drive);        // "FF" ... ("any" for 0)
const char* category_rule_name(int category);  // "production cars", "race and rally cars"
const char* aspiration_rule_name(int aspiration); // "NA", "turbo"
// "Class B, FR, NA, at most 300 PS" ("any car" when there is no rule).
std::string car_rules_summary(const CarRules& r);

// What the rules look at, from the car as this machine builds it.
struct CarFacts {
    bool known = false;   // the car is in this machine's database
    bool built = false;   // its spec could be built here (power known)
    float power_ps = 0;   // peak at the crank (a garage car with its engine condition)
    int drive = 0;        // 1 FF, 2 FR, 3 4WD, 4 MR, 5 RR (CarRules::drive); 0 = unknown
    bool turbo = false;   // turbo kit fitted (as GT3's events read it: turbo kit record +0x11)
    int category = 0;     // car +0xF1: 0 production, 1 race, 2 rally
    bool tuned = false;   // a garage car off the stock car (GtMode::tuned)
    int arcade_class = 0; // the class list it is in (arcade cars, mod cars with one); 0 = none
    float kg_per_ps = 0;  // weight per power: the spec's mass less 120 kg over its power (built)
    int weight_class = 0; // the class of that weight per power (data::class_by_weight), 1..4
    std::string setup;    // a garage car GT Mode cannot build: why (garage_setup_problem); "" otherwise
};
// `gd` (may be null) gives the arcade class lists.
CarFacts online_car_facts(const data::CarDatabase& db, const GtData* gd, const OnlineCar& c);

// A car's class online: the class list it is in, else as GT3 classes a garage car in Single
// Race: on a dirt course a rally car, on a road course by its weight per power. 0 = unknown.
int car_class(const CarFacts& f, bool dirt_course);
// "Class A, 4.2 kg/PS", "Class S, arcade car", "Rally cars, dirt course".
std::string car_class_text(const CarFacts& f, bool dirt_course);

// Whether a car may race under a lobby's rules; `why` says what breaks them. A car that is not
// on this machine (a mod car it lacks) passes the checks that need it. A garage car GT Mode cannot
// build (CarFacts::setup) races under no rules.
bool car_meets_rules(const OnlineCar& c, const CarFacts& f, const CarRules& r, std::string* why = nullptr);

// The garage car's gearbox is a racing one (gear part +0x10 != 0): the transmission whine.
bool online_car_racing_gearbox(const data::CarDatabase& db, const OnlineCar& c);

} // namespace gt3r::game
