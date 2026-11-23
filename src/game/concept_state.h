#pragma once

// Gran Turismo Concept's progress and the rules that read it (concept §7): the course license records,
// the single-race results and the present flags, the prize pass run after a race, which cars and courses are open, and
// the completion figure. Pure rules over plain data: the lists come from the disc's game config (concept_game.h), the
// state is saved with the game (gt_save.h). Addresses are the Tokyo-Geneva program's.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gt3r::game {

// Levels of a single race: 0 Normal, 1 Professional, 2 Ace (the race code's last digit - 1, 0x217BC8).
constexpr int kConceptLevels = 3;
// A license medal: -1 none, 0 gold, 1 silver, 2 bronze (0x2169D8).
constexpr int kConceptNoMedal = -1;
// A finishing place: 0 = won; "has a result" = under 6 (0x119958); none recorded: 8, as GT3's arcade results.
constexpr int kConceptNoPlace = 8;
constexpr int kConceptPlaces = 6;

// The special presents' names (program constants 0x3368C0, 0x3368D0, 0x322900, 0x3368E0).
constexpr const char* kConceptMovie0 = "MOVIE0";
constexpr const char* kConceptMovie1 = "MOVIE1";
constexpr const char* kConceptTransfer = "TRANSFER";
constexpr const char* kConceptPodRace = "PODRACE";

// What the game keeps (the three tables of §7.1), by course index (the course's place in the SINGLE_RACE list).
struct ConceptState {
    struct License {
        int medal = kConceptNoMedal;
        double best_time = -1; // seconds, < 0 = none
    };
    std::map<int, License> licenses;             // course -> record
    std::map<std::pair<int, int>, int> places;   // (level, course) -> best place
    std::set<std::string> presents;              // prize keys and specials whose flag is set

    int medal(int course) const;
    bool license_passed(int course) const { return medal(course) != kConceptNoMedal; }
    bool license_gold(int course) const { return medal(course) == 0; }
    int place(int level, int course) const;
    bool has_result(int level, int course) const { return place(level, course) < kConceptPlaces; }
    bool won(int level, int course) const { return place(level, course) == 0; }
    bool won_any(int course) const;       // at some level
    bool won_pro(int course) const;       // at Professional or Ace
    bool present(const std::string& key) const { return presents.count(key) != 0; }

    // A license run with a medal (0 gold .. 2 bronze): the better medal and the faster run are kept, as GT3's license
    // record does (M: the record is GT3's; the medal store 0x216A08 was not read for the comparison).
    void record_license(int course, int medal, double seconds);
    // A single race's finishing place (0-based): the best one is kept (0x217D78).
    void record_place(int level, int course, int place);
    bool empty() const { return licenses.empty() && places.empty() && presents.empty(); }
};

// The lists of the disc's game config the rules read.
struct ConceptRules {
    std::vector<std::string> courses;        // gcf tag 8 SINGLE_RACE, in order: course n
    std::vector<std::string> license_codes;  // gcf tag 14 CLASS_LICENSE: test n's race code ("LCeu0003")
    // A prize key (gcf tag 15): "C<course 00-09><L|N|H><variant>" with an optional "_S" (0x119800 reads the
    // name), naming one car by its class and place.
    struct Key {
        std::string name;
        int course = -1;
        char kind = 0;    // 'L' license, 'N' Normal, 'H' Professional
        int variant = 0;  // L0 a medal, L1 gold
        bool twin = false; // the "_S" key
        std::string car_class; // "CLASS_S"
        int index = -1;        // place in that class list
    };
    std::vector<Key> keys; // in the game config's order

    int course_count() const { return int(courses.size()); }
    int course_index(std::string_view course) const;
    const Key* key(std::string_view name) const;
    // Reads a key's name; nullopt when it is not one ("MOVIE0", ...).
    static std::optional<Key> parse_key(std::string_view name);
};

// What a race code says about a run (0x215240): a course license test (mode 0; the test from the code's last two
// digits) or a single race at a level (mode 1 Normal, 2 Professional, 3 Ace, the code's last digit; the course's
// index in SINGLE_RACE). mode -1: neither.
struct ConceptRun {
    int mode = -1;
    int course = -1;
    bool license() const { return mode == 0; }
    int level() const { return mode - 1; }
};
ConceptRun concept_run(const ConceptRules& rules, std::string_view race_code, std::string_view course);
// A single race's code at a level: the class entry's code with its last character raised by the level (0x114178).
std::string concept_race_code(std::string_view entry_code, int level);

// The prize pass (0x2153C0), run after a single race or a license test once its result is recorded: the keys and
// specials the run opens that are not flagged yet, each with its kind (0 a car, 1 the pod race, 2 the credit
// transfer, 3 MOVIE0, 4 MOVIE1), sorted by kind, six at most. Nothing is flagged: concept_set_presents does that once
// the presentations have been shown.
struct ConceptGrant {
    std::string name;
    int kind = 0;
};
constexpr size_t kConceptMaxGrants = 6;
std::vector<ConceptGrant> concept_prize_pass(const ConceptRules& rules, const ConceptState& state, const ConceptRun& run);
void concept_set_presents(ConceptState& state, const std::vector<ConceptGrant>& grants);

// Every course won at some level / at Professional or Ace (0x2180B0, 0x2180F8); every license with a medal (0x216A40).
bool concept_all_won(const ConceptRules& rules, const ConceptState& state);
bool concept_all_won_pro(const ConceptRules& rules, const ConceptState& state);
bool concept_all_licenses(const ConceptRules& rules, const ConceptState& state);

// A class slot is locked while some prize key names it and that key's flag is clear (0x211058); every other slot is
// open from the start.
bool concept_slot_locked(const ConceptRules& rules, const ConceptState& state, std::string_view car_class, int index);
// A course is offered in Single Race once its license has a medal (0x103FC0).
bool concept_course_open(const ConceptState& state, int course);

// The completion figure 0..1 (0x21B508): per course a point for a win at any level and one more for a win at
// Professional or Ace, per license a point for a medal and one more for gold, over 40 (program constant), capped at 1.
constexpr int kConceptCompletionPoints = 40;
float concept_completion(const ConceptRules& rules, const ConceptState& state);

// Game Status (0x119958): whether the thing an item or node of the page stands for is reached. `item` is the item's
// name: "C<nn>L0", "C<nn>L1", "C<nn>N0", "C<nn>H0", "MOVIE0", "MOVIE1", "TRANSFER", "PODRACE".
bool concept_status_reached(const ConceptRules& rules, const ConceptState& state, std::string_view item);

} // namespace gt3r::game
