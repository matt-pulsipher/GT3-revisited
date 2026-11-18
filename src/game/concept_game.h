#pragma once

// Gran Turismo Concept's lists as the disc has them (concept §2, §5.6, §7.2): the rules' inputs from the
// game config (ConceptRules), the category page's car classes and the cars a category offers on a course. The rules
// themselves are in concept_state.h; the menus' bindings in concept_menu.cpp.

#include "game/concept_state.h"
#include "game/gt_mode.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r::game {

// The courses (gcf tag 8 SINGLE_RACE), the license tests' race codes (tag 14 CLASS_LICENSE) and the prize keys
// (tag 15) of a disc.
ConceptRules concept_rules(const GtData& d);

// The category page's five categories, in its item order C, B, A, S, D: the game config class of each (program
// tables); in British English the class's "_UK" twin when the config has one.
constexpr int kConceptCategories = 5;
std::string concept_category_class(const GtData& d, int category);
// A class without its "_UK" (the prize keys name the plain class; the twin lists the same cars in the same places).
std::string concept_base_class(std::string_view car_class);

// The credit transfer of a release (concept §7.6): the credits it adds (program constant: 10,000,000 in the
// European Tokyo-Geneva program, 1,000,000,000 in the others) and the Gran Turismo 3 release whose saves it takes
// (the save name each program holds: the PAL release's there, the Japanese release's in the others).
struct ConceptTransferRule {
    s64 credits = 0;
    Region gt3 = Region::Japan;
};
ConceptTransferRule concept_transfer_rule(const RegionData& release);
// The Gran Turismo 3 releases whose saves our transfer takes: all three (the saves of every release keep the
// words the transfer changes in the same places), the one the program names first, then NTSC-U, PAL, NTSC-J.
std::vector<Region> concept_transfer_regions(const RegionData& release);

// A release's game save on the memory card (concept §11): the save directory and data file's name (the two parts
// each program's name builder joins, a region code and the release's serial, then "GAMEDATA"), the save's minor
// version (what the status object's version call answers; the major is 100 in all of them) and the icon file of
// menu/icon/ the game file's object names.
struct ConceptSaveRule {
    std::string dir;
    u16 minor = 0;
    std::string icon;
};
ConceptSaveRule concept_save_rule(std::string_view game_config);

// The attract sequence of a release (the programs' main loops, concept §3.3): "" for a demo race, else a movie. The
// Tokyo-Geneva programs: demo, TV spot, demo, TV spot, demo, opening; Tokyo-Seoul: four demos, opening; 2001 Tokyo:
// three demos, opening.
std::vector<std::string> concept_attract_steps(const RegionData& release);

// A class entry's picture on the car page: its label without the "gtcmr_" / "gtcmd_" prefix ("sl500_02").
std::string concept_car_image(std::string_view label);
// A class entry for dirt courses ("gtcmd_<car>"; "gtcmr_<car>" are the road entries).
bool concept_dirt_entry(std::string_view label);

// A car's own single race at a level (0 Normal, 1 Professional, 2 Ace) on a Gran Turismo Concept disc: the race its class
// entry runs at that level (the entry's code with its last character raised by the level, as Single Race does), its entry of
// the course's kind (dirt or road), else its other one. The car is its arcade car (table 35) or its database car (an entry
// whose arcade car is that car); neither given: the first entry. The single-race classes first, their British English twins
// after. A car no class lists: its race under Tokyo-Geneva's names (A_<car>_000n, dirt AD_<car>_000n) where the disc holds
// one. "": no such race.
std::string concept_car_race(const GtData& d, const std::string& arcade, const std::string& car, int level, bool dirt);
// Our CPU cars at a level (the online lobby's, Quick race's custom race; no original mode has a car's skill): the driver
// overrides of the car's own single race at that level (concept_car_race). nullopt: no such race.
std::optional<std::array<u8, 16>> concept_cpu_driver(const GtData& d, const std::string& arcade, const std::string& car, int level,
                                                     bool dirt);

// The entries of a class that can be chosen on a course of the given kind: enabled, of that kind and not locked
// (concept_slot_locked), in list order; `index` is the entry's place in the class list.
struct ConceptEntry {
    const GtData::ArcadeCar* car = nullptr;
    int index = -1;
};
std::vector<ConceptEntry> concept_open_cars(const GtData& d, const ConceptRules& rules, const ConceptState& state,
                                            const std::string& car_class, bool dirt);
// The entry a prize key names (its class and place), or nullptr.
const GtData::ArcadeCar* concept_key_car(const GtData& d, const ConceptRules::Key& key);

// --- The cars' settings (concept §10) ---
//
// The game keeps a garage car for every class entry: the entry's parts with their own settings, owning every part the
// database has a record of for the car, in one of two fixed sets of parts, Normal or Custom. The races take the car
// from there, so what the settings menu changes on a car stays on it (and is saved with the game).

// The two sets (program tables, the same five goods in all four programs: Tokyo-Geneva): the brake
// controller, suspension, gearbox and both tires. Normal fits each; Custom fits each the car has a record of.
constexpr int kConceptSetParts = 5;
extern const int kConceptNormalSet[kConceptSetParts];
extern const int kConceptCustomSet[kConceptSetParts];

// A new garage car of a class entry (an arcade car's label): Normal. nullopt for an unknown entry.
std::optional<GarageCar> concept_new_car(const GtMode& gt, const std::string& entry);
// The entry's garage car (GarageCar::origin is the entry), made when the garage has none yet; it becomes the current
// car when `ride`. nullptr for an unknown entry or a full garage.
GarageCar* concept_garage_car(GtMode& gt, const std::string& entry, bool ride);
// The settings menu opens on its Normal page for this car (the brake controller it wears is the plain one).
bool concept_car_normal(const GtMode& gt, const GarageCar& c);
void concept_fit_normal(const GtMode& gt, GarageCar& c);
void concept_fit_custom(const GtMode& gt, GarageCar& c);
// The Custom page's Reset: every setting back to its part's own, then the Custom set again.
void concept_reset_custom(const GtMode& gt, GarageCar& c);

// The class entries the game keeps a garage car of, in the garage's order (from a new game's): the
// five categories' lists in the game's language (concept_category_class) one after another, 200 at most; an entry the
// database has no car for makes none.
std::vector<std::string> concept_garage_entries(const GtMode& gt);

// A continuously variable gearbox (the gearbox record's gear count is 2): its ratio cannot be set.
bool concept_cvt(const GtMode& gt, const GarageCar& c);

// The Custom page's tires (its two sliders, front and rear on their own): position 0 soft, 1 normal, 2 hard, by the
// tire's stage (program table: stages 7, 0 and 3). -1 for a tire of another stage.
constexpr int kConceptTirePositions = 3;
int concept_tire_position(const GtMode& gt, const GarageCar& c, bool rear);
bool concept_set_tire(const GtMode& gt, GarageCar& c, bool rear, int position);

// The parts a garage car races with (the race's car build, the same rule in GT3): on a dirt course the car's
// dirt tires (stage 12), with the car page's drift tires its stage 11 ones (not on a wet course), else the tires it
// wears.
std::array<u64, 27> concept_race_parts(const GtMode& gt, const GarageCar& c, const std::string& course, bool drift);

} // namespace gt3r::game
