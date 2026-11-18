// Gran Turismo Concept's progress rules (game/concept_state.h, concept §7) on an INVENTED game config:
// three courses, a handful of prize keys. No disc.

#include "test_framework.h"

#include "disc/region.h"
#include "game/concept_game.h"
#include "game/concept_state.h"
#include "game/gt_save.h"
#include "game/save_edit.h"

#include <algorithm>

using namespace gt3r;
using namespace gt3r::game;

namespace {

ConceptRules rules3() {
    ConceptRules r;
    r.courses = {"oval", "hill", "r_oval"};
    r.license_codes = {"LCzz0000", "LCzz0001", "LCzz0002"};
    auto key = [&](const char* name, const char* cls, int index) {
        ConceptRules::Key k = *ConceptRules::parse_key(name);
        k.car_class = cls;
        k.index = index;
        r.keys.push_back(k);
    };
    key("C00H0", "CLASS_A", 3);
    key("C00L0", "CLASS_DIRT", 1);
    key("C00L1", "CLASS_S", 0);
    key("C00N0", "CLASS_B", 2);
    key("C00N0_S", "CLASS_B", 5);
    key("C01L0", "CLASS_A", 0);
    key("C01N0", "CLASS_C", 4);
    key("C02H0", "CLASS_S", 1);
    return r;
}

std::vector<std::string> names(const std::vector<ConceptGrant>& g) {
    std::vector<std::string> out;
    for (const ConceptGrant& x : g) out.push_back(x.name);
    return out;
}

} // namespace

TEST(concept_state_keys_and_codes) {
    const std::optional<ConceptRules::Key> k = ConceptRules::parse_key("C07H0_S");
    CHECK(k && k->course == 7 && k->kind == 'H' && k->variant == 0 && k->twin);
    const std::optional<ConceptRules::Key> l = ConceptRules::parse_key("C02L1");
    CHECK(l && l->course == 2 && l->kind == 'L' && l->variant == 1 && !l->twin);
    for (const char* bad : {"MOVIE0", "PODRACE", "C0L0", "C00X0", "C00N0_T", "", "C00N"}) CHECK(!ConceptRules::parse_key(bad));

    const ConceptRules r = rules3();
    // A license code: mode 0, the test from its last two digits; a race code: the level from its last digit, the
    // course from the menu's choice.
    ConceptRun run = concept_run(r, "LCzz0002", "oval");
    CHECK(run.license() && run.course == 2);
    run = concept_run(r, "A_car_01_0002", "hill");
    CHECK(run.mode == 2 && run.level() == 1 && run.course == 1);
    CHECK(concept_run(r, "A_car_01_0001", "nowhere").mode == -1 && concept_run(r, "ATAF0000", "oval").mode == -1 &&
          concept_run(r, "A2P0009", "oval").mode == -1 && concept_run(r, "", "oval").mode == -1);
    CHECK(concept_race_code("A_car_01_0001", 0) == "A_car_01_0001" && concept_race_code("AD_car_01_0001", 2) == "AD_car_01_0003");
}

TEST(concept_state_records) {
    ConceptState s;
    CHECK(s.empty() && s.medal(0) == kConceptNoMedal && s.place(0, 0) == kConceptNoPlace && !s.has_result(0, 0));
    s.record_license(1, 2, 80.5);
    CHECK(s.license_passed(1) && !s.license_gold(1) && s.licenses[1].best_time == 80.5);
    s.record_license(1, 0, 79.0); // gold: the better medal and time stay
    s.record_license(1, 1, 85.0);
    CHECK(s.license_gold(1) && s.licenses[1].best_time == 79.0);
    s.record_license(1, 7, 1.0); // not a medal
    CHECK(s.license_gold(1) && s.licenses[1].best_time == 79.0);
    s.record_place(0, 2, 3);
    CHECK(s.has_result(0, 2) && !s.won(0, 2) && !s.won_any(2));
    s.record_place(0, 2, 5); // a worse place does not replace the best
    CHECK(s.place(0, 2) == 3);
    s.record_place(2, 2, 0);
    CHECK(s.won(2, 2) && s.won_any(2) && s.won_pro(2) && !s.won(1, 2));
    s.record_place(3, 2, 0); // no such level
    CHECK(s.places.size() == 2);
}

// The prize pass: which keys a run opens, the specials, the order and what stays locked.
TEST(concept_state_prize_pass) {
    const ConceptRules r = rules3();
    ConceptState s;
    // Nothing recorded: nothing given, the keyed slots are locked, every other slot is open.
    CHECK(concept_prize_pass(r, s, concept_run(r, "LCzz0000", "")).empty());
    CHECK(concept_slot_locked(r, s, "CLASS_B", 2) && concept_slot_locked(r, s, "CLASS_B", 5) && !concept_slot_locked(r, s, "CLASS_B", 0) &&
          !concept_slot_locked(r, s, "CLASS_DIRT", 2));
    CHECK(!concept_course_open(s, 0));

    // A bronze in test 0: L0 only. Gold: L1 too. Each is given once.
    s.record_license(0, 2, 90);
    std::vector<ConceptGrant> g = concept_prize_pass(r, s, concept_run(r, "LCzz0000", ""));
    CHECK(names(g) == std::vector<std::string>{"C00L0"} && g[0].kind == 0);
    concept_set_presents(s, g);
    CHECK(!concept_slot_locked(r, s, "CLASS_DIRT", 1) && concept_slot_locked(r, s, "CLASS_S", 0) && concept_course_open(s, 0));
    CHECK(concept_prize_pass(r, s, concept_run(r, "LCzz0000", "")).empty());
    s.record_license(0, 0, 80);
    g = concept_prize_pass(r, s, concept_run(r, "LCzz0000", ""));
    CHECK(names(g) == std::vector<std::string>{"C00L1"});
    concept_set_presents(s, g);
    // A license run gives no race key, a race no license key.
    s.record_license(1, 1, 70);
    CHECK(names(concept_prize_pass(r, s, concept_run(r, "A_x_0001", "hill"))).empty());

    // Second at Normal: nothing. A win at Normal: N0 and its twin, not H0.
    s.record_place(0, 0, 1);
    CHECK(concept_prize_pass(r, s, concept_run(r, "A_x_0001", "oval")).empty());
    s.record_place(0, 0, 0);
    g = concept_prize_pass(r, s, concept_run(r, "A_x_0001", "oval"));
    CHECK(names(g) == (std::vector<std::string>{"C00N0", "C00N0_S"}));
    concept_set_presents(s, g);
    // A win at Professional: H0 (N0 is flagged already).
    s.record_place(1, 0, 0);
    g = concept_prize_pass(r, s, concept_run(r, "A_x_0002", "oval"));
    CHECK(names(g) == std::vector<std::string>{"C00H0"});
    concept_set_presents(s, g);
    // A first win at Ace on a course gives its N0 as well (modes 1 to 3).
    s.record_place(2, 1, 0);
    g = concept_prize_pass(r, s, concept_run(r, "A_x_0003", "hill"));
    CHECK(names(g) == std::vector<std::string>{"C01N0"});
    concept_set_presents(s, g);

    // The last course won: its key, then the transfer (kind 2), MOVIE0 (3) and, every course now won at Professional
    // or Ace... not yet: course 0 is, course 1 is (Ace), course 2 at Normal only.
    CHECK(!concept_all_won(r, s));
    s.record_place(0, 2, 0);
    g = concept_prize_pass(r, s, concept_run(r, "A_x_0001", "r_oval"));
    CHECK(names(g) == (std::vector<std::string>{kConceptTransfer, kConceptMovie0}));
    concept_set_presents(s, g);
    CHECK(concept_all_won(r, s) && !concept_all_won_pro(r, s));
    s.record_place(1, 2, 0);
    g = concept_prize_pass(r, s, concept_run(r, "A_x_0002", "r_oval"));
    CHECK(names(g) == (std::vector<std::string>{"C02H0", kConceptMovie1}) && g[1].kind == 4);
    concept_set_presents(s, g);
    CHECK(concept_prize_pass(r, s, concept_run(r, "A_x_0002", "r_oval")).empty());

    // The pod race: the last license medal, on a license run.
    CHECK(!concept_all_licenses(r, s));
    s.record_license(2, 2, 100);
    CHECK(concept_prize_pass(r, s, concept_run(r, "A_x_0001", "oval")).empty());
    g = concept_prize_pass(r, s, concept_run(r, "LCzz0002", ""));
    CHECK(names(g) == std::vector<std::string>{kConceptPodRace} && g[0].kind == 1);
}

TEST(concept_state_completion_and_status) {
    const ConceptRules r = rules3();
    ConceptState s;
    CHECK(concept_completion(r, s) == 0.0f);
    CHECK(!concept_status_reached(r, s, "C00L0") && !concept_status_reached(r, s, "PODRACE") && !concept_status_reached(r, s, "nothing"));
    s.record_license(0, 2, 90); // 1 point
    s.record_license(1, 0, 90); // 2
    s.record_place(0, 0, 0);    // 1
    s.record_place(1, 1, 0);    // 2
    s.record_place(0, 2, 4);    // a result, no point
    CHECK(concept_completion(r, s) == 6.0f / float(kConceptCompletionPoints));
    CHECK(concept_status_reached(r, s, "C00L0") && !concept_status_reached(r, s, "C00L1") && concept_status_reached(r, s, "C01L1"));
    CHECK(concept_status_reached(r, s, "C00N0") && !concept_status_reached(r, s, "C00H0") && concept_status_reached(r, s, "C01H0") &&
          !concept_status_reached(r, s, "C01N0") && !concept_status_reached(r, s, "C02N0"));
    CHECK(!concept_status_reached(r, s, "MOVIE0") && !concept_status_reached(r, s, "TRANSFER") && !concept_status_reached(r, s, "MOVIE1"));
    // Capped at 1 whatever the config's size.
    ConceptRules big = r;
    for (int i = 3; i < 30; ++i) big.courses.push_back("c" + std::to_string(i));
    ConceptState full;
    for (int c = 0; c < 30; ++c) full.record_license(c, 0, 60), full.record_place(1, c, 0);
    CHECK(concept_completion(big, full) == 1.0f && concept_status_reached(big, full, "MOVIE1") && concept_status_reached(big, full, "PODRACE"));
}

// The three tables survive the save file; a Gran Turismo 3 save has none and stays as it was.
TEST(concept_state_save_round_trip) {
    GameState g;
    const std::string plain = save_to_text(g);
    CHECK(plain.find("clicense") == std::string::npos && plain.find("cplace") == std::string::npos && plain.find("cpresent") == std::string::npos);
    g.concept_game.record_license(0, 2, 91.234);
    g.concept_game.record_license(9, 0, 60.5);
    g.concept_game.licenses[4]; // (a record without a medal)
    g.concept_game.record_place(0, 3, 0);
    g.concept_game.record_place(2, 9, 5);
    g.concept_game.presents = {"C00L0", "C09N0_S", kConceptPodRace};
    const GameState back = load_from_text(save_to_text(g));
    const ConceptState& c = back.concept_game;
    CHECK(c.medal(0) == 2 && c.licenses.at(0).best_time == 91.234 && c.license_gold(9) && c.medal(4) == kConceptNoMedal && c.licenses.size() == 3);
    CHECK(c.won(0, 3) && c.place(2, 9) == 5 && c.places.size() == 2);
    CHECK(c.presents == g.concept_game.presents);
    CHECK(save_to_text(back) == save_to_text(g));
    // A damaged record is refused, not half read.
    bool threw = false;
    try {
        std::string t = save_to_text(g);
        t.replace(t.find("cplace 0 3 0"), 12, "cplace 9 3 0");
        load_from_text(t);
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

// The credit transfer into a Gran Turismo 3 game (concept §7.6; game/save_edit.h): the mark, the credits, the
// license cards, the bronze award bit and a bronze on every test without a medal; a second transfer changes nothing;
// the mark survives our save format.
TEST(concept_transfer_save_edit) {
    GameState gt3;
    gt3.credits = 12'345;
    gt3.trophy_flags = 0x41; // the B license card and a championship trophy
    gt3.arcade.awards = 0x1000;
    gt3.license_tests[0] = {Medal::Gold, 21.5};
    gt3.license_tests[9] = {Medal::Silver, 40.25};
    gt3.license_tests[47] = {Medal::None, 55.0}; // a run slower than bronze keeps its time
    CHECK(!gt3.concept_transfer);
    CHECK(apply_concept_transfer(gt3, 10'000'000));
    CHECK(gt3.concept_transfer && gt3.credits == 10'012'345);
    CHECK(gt3.trophy_flags == 0x7Fu && gt3.arcade.awards == (0x1000u | 0x80000u));
    int gold = 0, silver = 0, bronze = 0;
    for (const LicenseTestRecord& t : gt3.license_tests) gold += t.medal == Medal::Gold, silver += t.medal == Medal::Silver, bronze += t.medal == Medal::Bronze;
    CHECK(gold == 1 && silver == 1 && bronze == 46);
    CHECK(gt3.license_tests[0].best_time == 21.5 && gt3.license_tests[47].best_time == 55.0 && gt3.license_tests[5].best_time < 0);
    // Again: refused, nothing moves.
    const GameState before = gt3;
    CHECK(!apply_concept_transfer(gt3, 10'000'000));
    CHECK(gt3.credits == before.credits && gt3.trophy_flags == before.trophy_flags && gt3.arcade.awards == before.arcade.awards);
    // The mark is kept by the save text, and a save without it reads as not transferred.
    const GameState back = load_from_text(save_to_text(gt3));
    CHECK(back.concept_transfer && back.credits == gt3.credits && back.license_tests[47].medal == Medal::Bronze);
    CHECK(!load_from_text(save_to_text(GameState{})).concept_transfer);
    // Each release's rule: the European Tokyo-Geneva program's 10,000,000 to the PAL release's saves, the others'
    // 1,000,000,000 to the Japanese release's.
    RegionData eu{Region::Europe}, jp{Region::Japan};
    eu.game = jp.game = GameKind::Concept;
    eu.program_gcf = "gameconf/concept_eu.gcf";
    CHECK(concept_transfer_rule(eu).credits == 10'000'000 && concept_transfer_rule(eu).gt3 == Region::Europe);
    for (const char* gcf : {"gameconf/concept.gcf", "gameconf/concept_kr.gcf", "gameconf/concept_tw.gcf"}) {
        jp.program_gcf = gcf;
        CHECK(concept_transfer_rule(jp).credits == 1'000'000'000 && concept_transfer_rule(jp).gt3 == Region::Japan);
    }
    // Ours lists every GT3 release's saves, the named release's first.
    CHECK((concept_transfer_regions(eu) == std::vector<Region>{Region::Europe, Region::America, Region::Japan}));
    CHECK((concept_transfer_regions(jp) == std::vector<Region>{Region::Japan, Region::America, Region::Europe}));
}
