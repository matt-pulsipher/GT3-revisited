// Gran Turismo Concept's menus walked item by item on the discs (the Concept spec): every item of the
// title, of its sub-pages and of the main menu is confirmed and backed out of, the level page's hidden level is
// shown, and each release's differences in the front end are checked. On every disc of GT3R_DISC_CONCEPT (disc
// images separated by ';'; skipped without it).

#include "test_framework.h"

#include "concept_discs.h"
#include "disc/disc.h"
#include "game/concept_game.h"
#include "game/concept_menu.h"
#include "game/gt_mode_menu.h"
#include "menu/flow/concept_hooks.h"
#include "menu/flow/game_flow.h"
#include "vfs/vfs.h"

#include <cstdlib>
#include <memory>
#include <set>

using namespace gt3r;
using namespace gt3r::menu;
using namespace gt3r::menu::flow;

namespace {

// A bronze license on the first course, so Single Race opens (with none, the main menu refuses it).
void give_license_medal() {
    game::GtModeSession* session = game::gt_mode_session();
    CHECK(session != nullptr);
    if (session) session->gt().state().concept_game.record_license(0, 2, 100);
}

void set_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unset_env(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

void settle(GameFlow& f) {
    for (int i = 0; i < 400; ++i) {
        if (!f.switching() && f.controller() && !f.controller()->busy()) break;
        f.tick(0);
    }
    f.tick(0);
}

void press(GameFlow& f, MenuKeys k) {
    f.tick(k);
    f.tick(0);
    settle(f);
}

std::string page(const GameFlow& f) { return f.controller() && f.controller()->has_page() ? f.controller()->page_name() : std::string(); }
std::string where(const GameFlow& f) { return f.control() + ":" + page(f); }

std::vector<std::string> item_names(const GameFlow& f) {
    std::vector<std::string> out;
    for (const auto& it : f.controller()->items())
        if (it.flow) out.push_back(it.flow->name);
    return out;
}

bool focus_named(GameFlow& f, const std::string& name) {
    const auto& items = f.controller()->items();
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].flow && items[i].flow->name == name) {
            f.controller()->set_focus(int(i));
            return true;
        }
    return false;
}

// Anything the flow said it could not do since the last call.
int complaints(GameFlow& f, const std::string& at) {
    int n = 0;
    for (const std::string& m : f.take_log())
        if (m.find("not implemented") != std::string::npos || m.find("not available") != std::string::npos) {
            std::printf("    %s: %s\n", at.c_str(), m.c_str());
            ++n;
        }
    return n;
}

// Back to `home` with Cancel (at most a few presses); a request the item made of the game is answered as finished at once.
bool back_to(GameFlow& f, const std::string& home) {
    for (int i = 0; i < 8; ++i) {
        for (int k = 0; k < 120 && f.waiting_for_game(); ++k) {
            f.take_request();
            f.game_finished();
            settle(f);
        }
        if (where(f) == home) return true;
        press(f, key_bit(MenuKey::Cancel));
    }
    return where(f) == home;
}

} // namespace

TEST(concept_disc_walk) {
    const std::vector<std::string> discs = test::concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> disc = Disc::open(path);
        std::printf("  %s\n", disc->info().serial.c_str());
        set_env("GT3R_GTMODE_SAVE", (test::temp_dir("concept-walk") / "game.sav").string().c_str());
        set_env("GT3R_GTMODE_NOSAVE", "1");
        set_env("GT3R_DATA_DIR", (test::temp_dir("concept-walk-data").string() + "/").c_str());
        game::reset_gt_mode_menu_for_tests();
        game::install_gt_mode_menu();
        Vfs vfs(&disc->volume());
        const RegionData& rd = disc->info().data;
        const std::string gcf = rd.program_gcf.value_or(std::string());
        const bool geneva = gcf == "gameconf/concept_eu.gcf" || gcf == "gameconf/concept_tw.gcf";
        GameFlow f(vfs_reader(vfs), text::language_code(vfs.language()));
        f.start();
        settle(f);
        const std::string title = where(f);
        CHECK(title == "concept:ar/push_start");

        // The title's items: seven on Tokyo-Geneva, eight with the A-spec demo on the earlier releases (concept §5.2, §12).
        const std::vector<std::string> title_items = item_names(f);
        const bool aspec = std::find(title_items.begin(), title_items.end(), "aspec_demo") != title_items.end();
        CHECK(title_items.size() == (geneva ? 7u : 8u) && aspec == !geneva);
        int bad = 0;
        for (const std::string& name : title_items) {
            if (name == "game_start") continue;
            CHECK(focus_named(f, name));
            press(f, key_bit(MenuKey::Confirm));
            for (int k = 0; k < 200 && f.switching(); ++k) f.tick(0);
            settle(f);
            const std::string at = f.waiting_for_game() ? std::string("(the game)") : where(f);
            std::printf("    title %-16s -> %s\n", name.c_str(), at.c_str());
            bad += complaints(f, "title " + name);
            if (name == "aspec_demo") { // the A-spec demo movie, then the title again
                const std::optional<GameRequest> rq = f.take_request();
                CHECK(rq && rq->kind == GameRequest::Kind::PlayMovie && vfs.exists(rq->course));
                if (rq) f.game_finished();
                settle(f);
            } else {
                CHECK(at != title);
                // The options: every item of the page opens and comes back (the pages are GT3's, concept §10).
                if (at == "option:option") {
                    const std::vector<std::string> opts = item_names(f);
                    std::printf("      %zu option items:", opts.size());
                    for (const std::string& o : opts) {
                        CHECK(focus_named(f, o));
                        press(f, key_bit(MenuKey::Confirm));
                        for (int k = 0; k < 200 && f.switching(); ++k) f.tick(0);
                        settle(f);
                        std::printf(" %s>%s", o.c_str(), where(f).c_str());
                        bad += complaints(f, "option " + o);
                        if (where(f) == title) break; // (its Exit)
                        CHECK(back_to(f, at));
                    }
                    std::printf("\n");
                }
                // The sub-pages' own items lead somewhere too.
                if (at == "concept:ar/replay_theater" || at == "concept:ar/credit") {
                    for (const std::string& sub : item_names(f)) {
                        CHECK(focus_named(f, sub));
                        press(f, key_bit(MenuKey::Confirm));
                        for (int k = 0; k < 200 && f.switching(); ++k) f.tick(0);
                        settle(f);
                        bad += complaints(f, name + " " + sub);
                        if (at == "concept:ar/replay_theater") {
                            std::printf("      %-14s -> %s\n", sub.c_str(), where(f).c_str());
                            CHECK(where(f) != at);
                        }
                        CHECK(back_to(f, at));
                    }
                }
            }
            CHECK(back_to(f, title));
        }
        CHECK(bad == 0);

        // The main menu: six items in the program's order; each opens its first page and Cancel comes back.
        CHECK(focus_named(f, "game_start"));
        press(f, key_bit(MenuKey::Confirm));
        const std::string top = where(f);
        CHECK(top == "concept:ar/top");
        // Single Race needs a course with a license medal (concept §5.5): a bronze on the first course.
        give_license_medal();
        const std::vector<std::string> games = item_names(f);
        CHECK((games == std::vector<std::string>{"license", "single_race", "two_player", "time_attack", "game_status", "ilink_battle"}));
        const std::string category = geneva ? "ar/select_category2" : "ar/select_category";
        for (const std::string& name : games) {
            CHECK(focus_named(f, name));
            press(f, key_bit(MenuKey::Confirm));
            for (int k = 0; k < 200 && f.switching(); ++k) f.tick(0);
            settle(f);
            if (name == "ilink_battle") {
                // i.LINK Battle: handed to the game (our online lobby), then the main menu again.
                const std::optional<GameRequest> rq = f.take_request();
                std::printf("    main  %-16s -> the game (%s)\n", name.c_str(), rq ? rq->mode.c_str() : "nothing");
                CHECK(rq.has_value());
                if (rq) f.game_finished();
                settle(f);
                bad += complaints(f, "main " + name);
                CHECK(back_to(f, top));
                continue;
            }
            std::printf("    main  %-16s -> %s\n", name.c_str(), where(f).c_str());
            bad += complaints(f, "main " + name);
            CHECK(where(f) != top);
            if (name == "single_race") {
                // The level page: two levels, three once L1 and R1 are held (concept §5.4); not kept by a new start.
                CHECK(page(f) == "ar/level" && f.controller()->items().size() == 2);
                press(f, MenuKeys(key_bit(MenuKey::L1) | key_bit(MenuKey::R1)));
                CHECK(f.controller()->items().size() == 3);
            }
            if (name == "time_attack") {
                // Free Run: course, then the release's category page, then the car page.
                CHECK(page(f) == "ar/select_course");
                press(f, key_bit(MenuKey::Confirm));
                CHECK(page(f) == category);
                // (A category whose cars are all prizes still to be won cannot be chosen: the first one that can.)
                const ConceptHooks& h = concept_hooks();
                int open = -1;
                for (int c = 0; c < game::kConceptCategories && open < 0; ++c)
                    if (h.category_cars && h.category_cars(c, f.chosen_course()) > 0) open = c;
                CHECK(open >= 0);
                if (open >= 0) f.controller()->set_focus(open);
                press(f, key_bit(MenuKey::Confirm));
                CHECK(page(f) == "ar/select_car");
            }
            if (name == "two_player") {
                CHECK(page(f) == "ar/select_course");
                press(f, key_bit(MenuKey::Confirm));
                CHECK(page(f) == (geneva ? "ar/select_car_2p2" : "ar/select_car_2p"));
            }
            if (name == "license") CHECK(page(f) == "ar/select_license" && f.controller()->items().size() == 10);
            if (name == "game_status") CHECK(page(f) == "ar/status");
            CHECK(back_to(f, top));
        }
        CHECK(bad == 0);
        // The releases' differences in what the menus hand on (concept §12): the license tests' codes, the present
        // movies (on the disc), the game config's class lists (the "_UK" twins on the European Tokyo-Geneva disc only), the credit transfer.
        if (const game::ConceptRules* rules = game::concept_rules_now()) {
            const std::string want = gcf == "gameconf/concept_eu.gcf" ? "LCeu000" : gcf == "gameconf/concept_tw.gcf" ? "LCtw000" : "LC00000";
            CHECK(rules->license_codes.size() == 10);
            for (size_t i = 0; i < rules->license_codes.size(); ++i) CHECK(rules->license_codes[i] == want + std::to_string(i));
        }
        CHECK(vfs.exists(geneva ? "stream/opening_GTCJP.pss" : "stream/opening_GT3UK.pss"));
        CHECK(vfs.exists(geneva ? "stream/ending_GTC.pss" : "stream/ending_long_full.pss"));
        if (game::GtModeSession* session = game::gt_mode_session()) {
            const game::GtData& d = session->gt().data();
            int car_lists = 0, uk = 0;
            for (const auto& [name, list] : d.arcade_classes) {
                if (name.rfind("CLASS_", 0) != 0) continue;
                ++car_lists;
                if (name.size() > 3 && name.compare(name.size() - 3, 3, "_UK") == 0) ++uk;
            }
            std::printf("    %d class lists, %d of them UK twins\n", car_lists, uk);
            // (Nine class lists on every disc; the European Tokyo-Geneva config adds the four road classes' UK twins.)
            CHECK(car_lists - uk == 9 && uk == (gcf == "gameconf/concept_eu.gcf" ? 4 : 0));
        }
        const game::ConceptTransferRule tr = game::concept_transfer_rule(rd);
        CHECK(tr.credits == (gcf == "gameconf/concept_eu.gcf" ? 10'000'000 : 1'000'000'000));
        CHECK(tr.gt3 == (gcf == "gameconf/concept_eu.gcf" ? Region::Europe : Region::Japan));
        // Cancel on the main menu: the title, on Game Start.
        press(f, key_bit(MenuKey::Cancel));
        CHECK(where(f) == title && f.controller()->focused() && f.controller()->focused()->flow->name == "game_start");
    }
    unset_env("GT3R_DATA_DIR");
    game::reset_gt_mode_menu_for_tests();
}

// The language page of the releases that have one (the European Tokyo-Geneva disc's six languages, the Asian one's
// two; concept §3.1, §15): each item gives its language, and the menus open in every language offered: the title,
// the main menu with its six games, the level page.
TEST(concept_disc_languages_walk) {
    const std::vector<std::string> discs = test::concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> disc = Disc::open(path);
        const RegionData& rd = disc->info().data;
        Vfs vfs(&disc->volume());
        std::set<std::string> chosen;
        if (rd.languages().size() > 1) {
            for (size_t i = 0; i < rd.languages().size(); ++i) {
                game::reset_gt_mode_menu_for_tests();
                game::install_gt_mode_menu();
                GameFlow f(vfs_reader(vfs), text::language_code(rd.default_language()));
                f.start("language", rd.language_page());
                settle(f);
                CHECK(f.control() == "language" && f.controller()->items().size() == rd.languages().size());
                f.controller()->set_focus(int(i));
                press(f, key_bit(MenuKey::Confirm));
                for (int k = 0; k < 300 && !f.exited(); ++k) f.tick(0);
                const std::optional<std::string> lang = f.take_language();
                CHECK(lang.has_value());
                if (lang) chosen.insert(*lang);
            }
            CHECK(chosen.size() == rd.languages().size());
        }
        std::printf("  %s:", disc->info().serial.c_str());
        for (Language l : rd.languages()) {
            const std::string code = text::language_code(l);
            if (!chosen.empty()) CHECK(chosen.count(code) == 1);
            set_env("GT3R_GTMODE_SAVE", (test::temp_dir("concept-lang") / "game.sav").string().c_str());
            set_env("GT3R_GTMODE_NOSAVE", "1");
            game::reset_gt_mode_menu_for_tests();
            game::install_gt_mode_menu();
            GameFlow f(vfs_reader(vfs), code);
            f.start();
            settle(f);
            CHECK(where(f) == "concept:ar/push_start");
            CHECK(focus_named(f, "game_start"));
            press(f, key_bit(MenuKey::Confirm));
            CHECK(where(f) == "concept:ar/top" && f.controller()->items().size() == 6);
            give_license_medal();
            CHECK(focus_named(f, "single_race"));
            press(f, key_bit(MenuKey::Confirm));
            CHECK(page(f) == "ar/level");
            std::printf(" %s", code.c_str());
        }
        std::printf("\n");
    }
    game::reset_gt_mode_menu_for_tests();
}
