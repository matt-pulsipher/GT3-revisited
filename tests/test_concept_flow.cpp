// Gran Turismo Concept's front end on the discs (menu/flow/concept_flow.cpp, game/concept_menu.cpp; the Concept
// spec): the menus walked headlessly from the title to a race request and back, on every disc of
// GT3R_DISC_CONCEPT (disc images separated by ';'; skipped without it). The rules alone are in test_concept_state.cpp.

#include "test_framework.h"

#include "concept_discs.h"
#include "disc/disc.h"
#include "game/concept_game.h"
#include "game/concept_menu.h"
#include "game/gt_mode_menu.h"
#include "menu/flow/concept_hooks.h"
#include "menu/flow/game_flow.h"
#include "menu/flow/gt_mode_hooks.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cstdlib>
#include <memory>

using namespace gt3r;
using namespace gt3r::menu;
using namespace gt3r::menu::flow;

namespace {

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
    for (int i = 0; i < 300; ++i) {
        if (!f.switching() && f.controller() && !f.controller()->busy()) break;
        f.tick(0);
    }
    f.tick(0);
}

void press(GameFlow& f, MenuKey k) {
    f.tick(key_bit(k));
    f.tick(0);
    settle(f);
}

std::string page(const GameFlow& f) { return f.controller() && f.controller()->has_page() ? f.controller()->page_name() : std::string(); }

// Moves the focus of the open page to the item with `action` (or, without one, named `name`).
bool focus_item(GameFlow& f, const std::string& action, const std::string& name = {}) {
    const auto& items = f.controller()->items();
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].flow && ((!action.empty() && items[i].flow->action == action) || (!name.empty() && items[i].flow->name == name))) {
            f.controller()->set_focus(int(i));
            return true;
        }
    return false;
}

std::optional<GameRequest> run_to_request(GameFlow& f) {
    for (int i = 0; i < 120 && !f.waiting_for_game(); ++i) f.tick(0);
    return f.take_request();
}

// The game came back with `r`: the menus resume, and the presentations a result queued are confirmed away.
void come_back(GameFlow& f, GameResult r) {
    post_game_result(std::move(r));
    f.game_finished();
    settle(f);
    for (int i = 0; i < 12 && f.control() != "concept"; ++i) {
        for (int k = 0; k < 240; ++k) f.tick(0); // (a presentation's own delay)
        press(f, MenuKey::Confirm);
        for (int k = 0; k < 120 && f.waiting_for_game(); ++k) f.tick(0);
        if (f.waiting_for_game()) { // a present's movie
            f.take_request();
            f.game_finished();
            settle(f);
        }
    }
}

} // namespace

TEST(concept_disc_flow) {
    const std::vector<std::string> discs = test::concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> disc = Disc::open(path);
        std::printf("  %s\n", disc->info().serial.c_str());
        set_env("GT3R_GTMODE_SAVE", (test::temp_dir("concept-flow") / "game.sav").string().c_str());
        set_env("GT3R_GTMODE_NOSAVE", "1");
        game::reset_gt_mode_menu_for_tests();
        game::install_gt_mode_menu();
        Vfs vfs(&disc->volume());
        const std::string lang = text::language_code(vfs.language());
        GameFlow f(vfs_reader(vfs), lang);
        CHECK(f.concept_game());
        f.start();
        settle(f);

        // The title: Concept's own page, on Game Start; its sub-pages come back to it.
        CHECK(f.control() == "concept" && page(f) == "ar/push_start" && f.on_title_page());
        CHECK(f.controller()->focused() && f.controller()->focused()->flow->name == "game_start");
        CHECK(focus_item(f, "", "replay_theater"));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/replay_theater" && !f.on_title_page());
        press(f, MenuKey::Cancel);
        CHECK(page(f) == "ar/push_start" && f.controller()->focused()->flow->name == "replay_theater");
        CHECK(focus_item(f, "go_arcade_menu"));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/top" && f.controller()->items().size() == 6);
        game::GtModeSession* session = game::gt_mode_session();
        CHECK(session != nullptr && game::concept_active());
        if (!session || !game::concept_active()) continue;
        const game::ConceptRules& rules = *game::concept_rules_now();
        game::ConceptState& st = session->gt().state().concept_game;
        CHECK(rules.course_count() == 10 && rules.license_codes.size() == 10 && rules.keys.size() == 50 && st.empty());

        // A new game: Single Race has no course yet (none has a license medal); the main menu refuses it (the
        // original takes no Confirm on the item).
        CHECK(focus_item(f, "single_race"));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/top" && f.controller()->focused() && f.controller()->focused()->flow->action == "single_race");
        CHECK(!f.waiting_for_game());

        // Free Run offers every course from the start.
        CHECK(focus_item(f, "time_attack"));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/select_course" && f.carousel() && f.carousel()->entries == rules.courses);
        {
            // Free Run's strip: the small icons ("_mini1", no place badge), all ten fitting, so none is cut; the
            // description scrolls in its box (the original's page).
            int small = 0, wide = 0, cut = 0, doc_cut = 0;
            for (const ElementDraw& d : f.draw_list()) {
                if (!d.element) continue;
                const std::string& n = d.element->name;
                const bool is_small = n.size() > 6 && n.compare(n.size() - 6, 6, "_mini1") == 0;
                const bool is_wide = n.size() > 5 && n.compare(n.size() - 5, 5, "_mini") == 0;
                small += is_small, wide += is_wide, cut += (is_small || is_wide) && d.has_clip;
                if (n.size() > 4 && n.compare(n.size() - 4, 4, "_doc") == 0 && d.text_clip) ++doc_cut;
            }
            CHECK(small == int(rules.courses.size()) && wide == 0 && cut == 0);
            CHECK(doc_cut > 0);
        }
        press(f, MenuKey::Cancel);
        CHECK(page(f) == "ar/top");

        // With a license on courses 0 and 3 (a road course and a dirt one) Single Race offers those two.
        st.record_license(0, 2, 100);
        st.record_license(3, 0, 90);
        CHECK(focus_item(f, "single_race"));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/level" && f.controller()->items().size() == 2); // Normal, Professional
        // L1 + R1 held on the level page add the Ace button, for the rest of the run.
        f.tick(key_bit(MenuKey::L1) | key_bit(MenuKey::R1));
        settle(f);
        CHECK(page(f) == "ar/level" && f.controller()->items().size() == 3 && f.concept_ace_shown());
        press(f, MenuKey::Confirm); // Normal
        CHECK(page(f) == "ar/select_course" && f.carousel() && f.carousel()->entries.size() == 2);
        CHECK(f.carousel()->entries[0] == rules.courses[0] && f.carousel()->entries[1] == rules.courses[3]);
        press(f, MenuKey::Confirm);
        // The category page of the release; a category whose cars are all locked refuses Confirm.
        const std::string category = page(f);
        CHECK(category == "ar/select_category" || category == "ar/select_category2");
        CHECK(f.controller()->items().size() == 5 && f.controller()->focus() == 0);
        int locked = -1;
        for (int c = 0; c < game::kConceptCategories; ++c)
            if (game::concept_open_cars(session->gt().data(), rules, st, game::concept_category_class(session->gt().data(), c), false).empty())
                locked = c;
        int road_category = 0; // the first category with a car for a road course
        for (int c = game::kConceptCategories - 1; c >= 0; --c)
            if (!game::concept_open_cars(session->gt().data(), rules, st, game::concept_category_class(session->gt().data(), c), false).empty())
                road_category = c;
        if (locked >= 0) {
            f.controller()->set_focus(locked);
            press(f, MenuKey::Confirm);
            CHECK(page(f) == category);
        }
        f.controller()->set_focus(road_category);
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/select_car" && f.carousel() && !f.carousel()->entries.empty());
        const size_t open_cars = f.carousel() ? f.carousel()->entries.size() : 0;
        // The car, then its steps: transmission and driving assist (a Concept page has no tire step); Cancel steps
        // back one.
        press(f, MenuKey::Confirm);
        CHECK(f.carousel() && f.carousel()->step == 2);
        press(f, MenuKey::Confirm);
        CHECK(f.carousel() && f.carousel()->step == 4);
        press(f, MenuKey::Cancel);
        CHECK(f.carousel() && f.carousel()->step == 2 && page(f) == "ar/select_car");
        press(f, MenuKey::Confirm);
        press(f, MenuKey::Right); // TCS only
        press(f, MenuKey::Confirm);
        std::optional<GameRequest> rq = run_to_request(f);
        CHECK(rq.has_value());
        if (!rq) continue;
        std::printf("    single race: %s on %s in %s\n", rq->race_code.c_str(), rq->course.c_str(), rq->car.c_str());
        const data::CarDatabase& db = session->gt().db();
        CHECK(rq->mode == "single_race" && rq->course == rules.courses[0] && rq->difficulty == 0 && rq->assist == 1 && rq->tire_mode == 0);
        CHECK(!rq->race_code.empty() && rq->race_code.back() == '1' && db.event(rq->race_code) != nullptr);
        CHECK(std::any_of(db.arcade_cars().begin(), db.arcade_cars().end(), [&](const data::CarVariant& v) { return v.label == rq->car; }));
        CHECK(game::concept_run(rules, rq->race_code, rq->course).mode == 1);

        // Third place: recorded, nothing granted, back on the game's first page.
        GameResult res;
        res.mode = rq->mode, res.race_code = rq->race_code, res.finished = true, res.position = 3, res.cars = 6;
        come_back(f, res);
        CHECK(f.control() == "concept" && page(f) == "ar/level");
        CHECK(st.place(0, 0) == 2 && st.presents.empty());
        // The same race won: the course's Normal key (and its twin) are granted and presented, the car's slot opens.
        press(f, MenuKey::Confirm);
        press(f, MenuKey::Confirm);
        CHECK(page(f) == category && f.controller()->focus() == road_category); // the category chosen last
        press(f, MenuKey::Confirm);
        for (int i = 0; i < 4; ++i) press(f, MenuKey::Confirm);
        rq = run_to_request(f);
        CHECK(rq.has_value());
        if (!rq) continue;
        res.position = 1;
        post_game_result(res);
        f.game_finished();
        settle(f);
        CHECK(f.control() == "concept_car_get" && page(f) == "concept_car_get");
        for (int k = 0; k < 240; ++k) f.tick(0);
        press(f, MenuKey::Confirm);
        CHECK(f.control() == "concept" && page(f) == "ar/level");
        CHECK(st.won(0, 0) && st.present("C00N0"));
        if (const game::ConceptRules::Key* twin = rules.key("C00N0_S")) CHECK(st.present(twin->name));
        const game::ConceptRules::Key* key = rules.key("C00N0");
        CHECK(key && !game::concept_slot_locked(rules, st, key->car_class, key->index));
        (void)open_cars;

        // Professional on the dirt course: the dirt entries, the code's last digit 2, no tire step.
        press(f, MenuKey::Right);
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/select_course");
        press(f, MenuKey::Right);
        press(f, MenuKey::Confirm);
        int dirt_category = -1;
        for (int c = game::kConceptCategories - 1; c >= 0; --c)
            if (!game::concept_open_cars(session->gt().data(), rules, st, game::concept_category_class(session->gt().data(), c), true).empty())
                dirt_category = c;
        CHECK(dirt_category >= 0);
        f.controller()->set_focus(std::max(dirt_category, 0));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/select_car" && f.carousel() && !f.carousel()->entries.empty());
        // (A dirt course: the transmission step alone.)
        press(f, MenuKey::Confirm);
        CHECK(f.carousel() && f.carousel()->step == 2);
        press(f, MenuKey::Confirm);
        rq = run_to_request(f);
        CHECK(rq.has_value());
        if (!rq) continue;
        std::printf("    single race: %s on %s in %s\n", rq->race_code.c_str(), rq->course.c_str(), rq->car.c_str());
        CHECK(rq->course == rules.courses[3] && rq->race_code.back() == '2' && db.event(rq->race_code) != nullptr);
        CHECK(game::concept_dirt_entry(rq->car) && rq->difficulty == 1);
        come_back(f, GameResult{});
        CHECK(f.control() == "concept" && page(f) == "ar/level");
        press(f, MenuKey::Cancel);
        CHECK(page(f) == "ar/top");

        // Free Run: course, category, car; the free run's event.
        CHECK(focus_item(f, "time_attack"));
        press(f, MenuKey::Confirm);
        CHECK(f.carousel() && f.carousel()->index == 3); // the course chosen last
        for (int i = 0; i < 3; ++i) press(f, MenuKey::Left);
        press(f, MenuKey::Confirm);
        CHECK(page(f) == category);
        f.controller()->set_focus(road_category);
        press(f, MenuKey::Confirm);
        for (int i = 0; i < 4; ++i) press(f, MenuKey::Confirm);
        rq = run_to_request(f);
        CHECK(rq.has_value());
        if (!rq) continue;
        CHECK(rq->mode == "free_run" && rq->race_code == "ATAF0001" && db.event("ATAF0001") != nullptr && !rq->car.empty());
        come_back(f, GameResult{});
        CHECK(page(f) == "ar/select_course");
        press(f, MenuKey::Cancel);

        // Course License: the ten tests, each test's pace car page, the request for the chosen one.
        CHECK(page(f) == "ar/top" && focus_item(f, "license"));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/select_license" && f.controller()->items().size() == 10);
        press(f, MenuKey::Right);
        press(f, MenuKey::Right);
        CHECK(f.controller()->focus() == 2);
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/lead_car" && f.controller()->focus() == 1); // Pace Car NO until YES was chosen
        press(f, MenuKey::Confirm);
        rq = run_to_request(f);
        CHECK(rq.has_value());
        if (!rq) continue;
        std::printf("    license: %s on %s%s\n", rq->race_code.c_str(), rq->course.c_str(), rq->lead_car ? " with the pace car" : "");
        CHECK(rq->mode == "go_license" && rq->race_code == rules.license_codes[2] && !rq->lead_car && db.event(rq->race_code) != nullptr);
        CHECK(db.event(rq->race_code)->course == rules.courses[2] && rq->course == rules.courses[2]);
        // A bronze: the medal is recorded, the course's L0 key granted (its car presented), Single Race gains the course.
        res = GameResult{};
        res.mode = rq->mode, res.race_code = rq->race_code, res.finished = true, res.license_medal = 2, res.total_time = 99.5;
        // In the race, the notice of a prize is queued once for the visit; a run that opens nothing queues none.
        {
            GameResult none = res;
            none.finished = false, none.license_medal = -1;
            CHECK(!game::queue_concept_notice(none));
            CHECK(game::queue_concept_notice(res) && !game::queue_concept_notice(res) && st.medal(2) == game::kConceptNoMedal);
            const std::optional<GtModeHooks::Presentation> p = gt_mode_hooks().presentation ? gt_mode_hooks().presentation() : std::nullopt;
            CHECK(p && p->set == "notice_present" && p->page == "root");
        }
        come_back(f, res);
        CHECK(f.control() == "concept" && page(f) == "ar/select_license" && f.controller()->focus() == 2);
        CHECK(st.medal(2) == 2 && st.present("C02L0") && !st.present("C02L1") && game::concept_course_open(st, 2));
        press(f, MenuKey::Cancel);
        CHECK(page(f) == "ar/top");

        // Game Status opens and its Back button returns; the main menu's Cancel goes back to the title.
        CHECK(focus_item(f, "status"));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/status");
        {
            // It opens on the present granted last (the license's car); every prize key's car has its thumbnail in the
            // page's hidden art; the directions move over the map (not through the list) and the map follows.
            const ConceptHooks& ch = concept_hooks();
            CHECK(f.controller()->focused() && f.controller()->focused()->flow->name == "C02L0");
            const MenuSet::PageRef ref = f.controller()->page();
            const Layer* hidden = nullptr;
            for (const Layer& l : ref.layout().layers)
                if (l.name == "hidden") hidden = &l;
            CHECK(hidden != nullptr);
            int thumbs = 0, keys = 0, reached = 0;
            std::vector<std::string> images;
            for (const game::ConceptRules::Key& k : rules.keys) {
                if (k.twin) continue;
                ++keys;
                const std::string image = ch.key_image ? ch.key_image(k.name) : std::string();
                CHECK(!image.empty());
                bool has = false;
                for (const Element& e : hidden->elements) has = has || e.name == image || e.name == "_" + image;
                thumbs += has;
                if (has && ch.status_reached(k.name)) ++reached, images.push_back(image);
            }
            // (The 2001 Tokyo release's buttons carry their pictures themselves.)
            std::printf("    status: %d keys, %d thumbnails, %d reached\n", keys, thumbs, reached);
            CHECK(keys == 40 && (thumbs == keys || thumbs == 0));
            CHECK(ch.license_medal(2) == 2 && !ch.course_won(2, false) && ch.status_reached("C02L0") && !ch.status_reached("C02L1"));
            std::vector<ElementDraw> draws = f.draw_list();
            int medals = 0, cars = 0, have = 0;
            for (int c = 0; c < 10; ++c) have += ch.license_medal(c) >= 0;
            for (const ElementDraw& d : draws) {
                if (!d.element) continue;
                medals += d.element->name == "license_bronze" || d.element->name == "license_silver" || d.element->name == "license_gold";
                for (const std::string& image : images) cars += d.has_box && (d.element->name == image || d.element->name == "_" + image);
            }
            CHECK(medals == have && have >= 1 && cars == reached);
            const std::string from = f.controller()->focused()->flow->name;
            int moves = 0;
            for (MenuKey k : {MenuKey::Left, MenuKey::Up, MenuKey::Right, MenuKey::Down}) {
                const std::string before = f.controller()->focused()->flow->name;
                press(f, k);
                moves += f.controller()->focused()->flow->name != before;
            }
            CHECK(moves >= 3 && page(f) == "ar/status");
            (void)from;
            // A key that is not reached refuses Confirm; a reached one opens the show room and comes back to its button.
            CHECK(focus_item(f, "", "C02L1"));
            press(f, MenuKey::Confirm);
            CHECK(page(f) == "ar/status");
            CHECK(focus_item(f, "", "C02L0"));
            press(f, MenuKey::Confirm);
            CHECK(page(f) == "ar/show_room");
            press(f, MenuKey::Cancel);
            std::printf("    status: back on %s / %s\n", page(f).c_str(), f.controller()->focused() ? f.controller()->focused()->flow->name.c_str() : "-");
            CHECK(page(f) == "ar/status" && f.controller()->focused()->flow->name == "C02L0");
        }
        CHECK(focus_item(f, "", "game_status"));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/top");
        {
            // Single Race with every license: ten wide icons (with their place badge) run past the strip, each cut at
            // the strip's window between its arrows (as the original's).
            for (int c = 0; c < 10; ++c) st.record_license(c, 0, 100);
            CHECK(focus_item(f, "single_race"));
            press(f, MenuKey::Confirm);
            press(f, MenuKey::Confirm); // Normal
            CHECK(page(f) == "ar/select_course" && f.carousel() && f.carousel()->entries.size() == rules.courses.size());
            int icons = 0, cut = 0;
            for (const ElementDraw& d : f.draw_list()) {
                const std::string n = d.element ? d.element->name : std::string();
                if (n.size() > 5 && n.compare(n.size() - 5, 5, "_mini") == 0) {
                    ++icons;
                    cut += d.has_clip && d.clip.x >= 30 && d.clip.x + d.clip.w <= 610;
                }
            }
            CHECK(icons > 0 && cut == icons);
            press(f, MenuKey::Cancel);
            press(f, MenuKey::Cancel);
            CHECK(page(f) == "ar/top");
        }
        press(f, MenuKey::Cancel);
        CHECK(page(f) == "ar/push_start" && f.on_title_page() && f.controller()->focused()->flow->name == "game_start");
    }
    game::reset_gt_mode_menu_for_tests();
}

// The presents of a finished game on the discs: the credit transfer's page over the Gran Turismo 3 saves (none in the
// test's folder: the page says so and is left), and the pod race from Game Status to its requests and back.
TEST(concept_disc_presents) {
    const std::vector<std::string> discs = test::concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> disc = Disc::open(path);
        std::printf("  %s\n", disc->info().serial.c_str());
        set_env("GT3R_GTMODE_SAVE", (test::temp_dir("concept-presents") / "game.sav").string().c_str());
        set_env("GT3R_GTMODE_NOSAVE", "1");
        set_env("GT3R_DATA_DIR", (test::temp_dir("concept-presents-data").string() + "/").c_str());
        game::reset_gt_mode_menu_for_tests();
        game::install_gt_mode_menu();
        Vfs vfs(&disc->volume());
        GameFlow f(vfs_reader(vfs), text::language_code(vfs.language()));
        f.start();
        settle(f);
        CHECK(focus_item(f, "go_arcade_menu"));
        press(f, MenuKey::Confirm);
        game::GtModeSession* session = game::gt_mode_session();
        CHECK(session != nullptr && game::concept_active());
        if (!session || !game::concept_active()) continue;
        game::ConceptState& st = session->gt().state().concept_game;
        for (int c = 0; c < 10; ++c) st.record_license(c, 0, 80.0), st.record_place(0, c, 0);
        const game::ConceptState before = st;
        CHECK(focus_item(f, "status"));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/status" && concept_hooks().status_reached("TRANSFER") && concept_hooks().status_reached("PODRACE"));

        // TRANSFER: the save page; with no Gran Turismo 3 save it says so, and OK goes back to Game Status.
        CHECK(focus_item(f, "", "TRANSFER"));
        press(f, MenuKey::Confirm);
        for (int i = 0; i < 200 && f.control() != "memorycard"; ++i) f.tick(0);
        settle(f);
        CHECK(f.control() == "memorycard");
        bool none = true;
        if (const SavePage* sp = f.save_page()) {
            // (The folder is the test's own; were a save there, the list would show and nothing is chosen.)
            none = sp->files().empty();
            CHECK(sp->mode() == SavePage::Mode::Transfer);
            CHECK(none ? sp->phase() == SavePage::Phase::Done && !sp->done_text().empty() : sp->phase() == SavePage::Phase::List);
        } else {
            CHECK(false);
        }
        press(f, none ? MenuKey::Confirm : MenuKey::Cancel);
        for (int i = 0; i < 200 && f.control() != "concept"; ++i) f.tick(0);
        settle(f);
        CHECK(f.control() == "concept" && page(f) == "ar/status" && f.controller()->focused()->flow->name == "TRANSFER");

        // The pod race: its page over Game Status, one player's car page, the request; nothing is recorded.
        const ConceptHooks::Pod pod = concept_hooks().pod();
        CHECK(!pod.course.empty() && pod.car_class == "CLASS_PODRACE" && !pod.entry.empty());
        CHECK(focus_item(f, "", "PODRACE"));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/podrace");
        CHECK(focus_item(f, "go_podrace_1p"));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/select_car" && f.carousel() && f.carousel()->entries.size() == 1);
        for (int i = 0; i < 6 && !f.waiting_for_game(); ++i) press(f, MenuKey::Confirm);
        std::optional<GameRequest> rq = run_to_request(f);
        CHECK(rq.has_value());
        if (!rq) continue;
        std::printf("    pod: %s on %s in %s\n", rq->race_code.c_str(), rq->course.c_str(), rq->car.c_str());
        data::CarDatabase db;
        CHECK(db.load(vfs));
        const data::Event* ev = db.event(rq->race_code);
        CHECK(rq->mode == "single_race" && rq->course == pod.course && ev != nullptr && ev->laps == 3 && ev->cars_in_race == 6);
        GameResult res;
        res.mode = rq->mode, res.race_code = rq->race_code, res.finished = true, res.position = 1, res.cars = 6;
        come_back(f, res);
        CHECK(f.control() == "concept" && page(f) == "ar/status");
        CHECK(st.places == before.places && st.presents == before.presents);
        // Two players: the 2 player car page, each row holding the pod alone (no class to pick); both players through
        // their steps, then the 2 player event, both in the pod.
        CHECK(focus_item(f, "", "PODRACE"));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "ar/podrace" && focus_item(f, "go_podrace_2p"));
        press(f, MenuKey::Confirm);
        CHECK(page(f).rfind("ar/select_car_2p", 0) == 0);
        f.tick(0);
        CHECK(f.two_player(0) && f.two_player(0)->state == 4 && f.two_player(0)->cars.size() == 1 && f.two_player(1)->state == 4);
        // Each player: the car, the transmission, then the driving assist (player 2 moves to "neither"), then ready.
        auto both = [&](MenuKeys k1, MenuKeys k2) {
            f.set_player2_keys(k2);
            f.tick(k1);
            f.set_player2_keys(0);
            f.tick(0);
        };
        both(key_bit(MenuKey::Confirm), key_bit(MenuKey::Confirm));
        CHECK(f.two_player(0)->state == 6 && f.two_player(1)->state == 6);
        both(key_bit(MenuKey::Confirm), key_bit(MenuKey::Confirm));
        CHECK(f.two_player(0)->state == 10 && f.two_player(1)->state == 10);
        both(0, key_bit(MenuKey::Right));
        both(0, key_bit(MenuKey::Right));
        both(0, key_bit(MenuKey::Cancel));
        CHECK(f.two_player(1)->state == 6);
        both(0, key_bit(MenuKey::Confirm));
        CHECK(f.two_player(1)->state == 10 && f.two_player(1)->assist == 2 && f.two_player(0)->assist == 0);
        for (int i = 0; i < 12 && !f.waiting_for_game(); ++i) both(key_bit(MenuKey::Confirm), key_bit(MenuKey::Confirm));
        rq = run_to_request(f);
        CHECK(rq && rq->mode == "two_player_battle" && rq->course == pod.course && rq->car == rq->car2 && !rq->car.empty() &&
              db.event(rq->race_code) != nullptr);
        CHECK(rq && rq->assist == 0 && rq->assist2 == 2);
        if (rq) std::printf("    pod 2P: %s\n", rq->race_code.c_str());
    }
    unset_env("GT3R_DATA_DIR");
    game::reset_gt_mode_menu_for_tests();
}

