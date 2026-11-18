// Gran Turismo Concept's car settings (the Concept spec; game/concept_game.h): the garage car of a class entry,
// its Normal and Custom sets, the Custom page's tires, the tires a race gives the car, and the settings menu itself
// (menu/setting.mbl's Normal and Custom pages) on every disc of GT3R_DISC_CONCEPT (disc images separated by ';';
// skipped without it).

#include "test_framework.h"

#include "concept_discs.h"
#include "disc/disc.h"
#include "game/concept_game.h"
#include "game/concept_menu.h"
#include "game/gt_mode_menu.h"
#include "game/gt_parts.h"
#include "game/gt_setting.h"
#include "menu/flow/concept_hooks.h"
#include "menu/flow/game_flow.h"
#include "race/exact_setup.h"
#include "sim/exact/params_build.h"
#include "vfs/vfs.h"

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

bool focus_item(GameFlow& f, const std::string& name) {
    const auto& items = f.controller()->items();
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].flow && items[i].flow->name == name) {
            f.controller()->set_focus(int(i));
            return true;
        }
    return false;
}

int tire_stage(const game::GtMode& gt, const std::array<u64, 27>& parts, bool rear) {
    const ByteSpan* r = gt.db().record(rear ? data::TableId::RearTire : data::TableId::FrontTire,
                                       parts[size_t(game::part_slot(rear ? 29 : 28))]);
    return r && r->size() > 0x10 ? (*r)[0x10] : -1;
}

} // namespace

// The two sets are the same five kinds of part, tires last (the programs' tables).
TEST(concept_setting_sets) {
    for (int i = 0; i < game::kConceptSetParts; ++i) {
        const game::Goods n = game::goods_info(game::kConceptNormalSet[i]), c = game::goods_info(game::kConceptCustomSet[i]);
        CHECK(n.table == c.table && n.stage == 0 && c.stage >= 0);
    }
    CHECK(game::goods_info(game::kConceptCustomSet[0]).table == 1 && game::goods_info(game::kConceptCustomSet[1]).table == 18 &&
          game::goods_info(game::kConceptCustomSet[2]).table == 17 && game::goods_info(game::kConceptCustomSet[2]).stage == 3);
    CHECK(game::goods_info(game::kConceptNormalSet[3]).table == 28 && game::goods_info(game::kConceptNormalSet[4]).table == 29);
}

TEST(concept_disc_settings) {
    const std::vector<std::string> discs = test::concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> disc = Disc::open(path);
        std::printf("  %s\n", disc->info().serial.c_str());
        set_env("GT3R_GTMODE_SAVE", (test::temp_dir("concept-setting") / "game.sav").string().c_str());
        set_env("GT3R_GTMODE_NOSAVE", "1");
        game::reset_gt_mode_menu_for_tests();
        game::install_gt_mode_menu();
        Vfs vfs(&disc->volume());
        const std::string lang = text::language_code(vfs.language());
        {
            GameFlow title(vfs_reader(vfs), lang);
            title.start();
            settle(title);
        }
        game::GtModeSession* session = game::gt_mode_session();
        CHECK(session != nullptr && game::concept_active());
        if (!session || !game::concept_active()) continue;
        game::GtMode& gt = session->gt();
        const game::GtData& d = gt.data();
        CHECK(gt.state().garage.empty());

        // Every road entry of the first category makes a garage car in the Normal set, on its plain tires.
        const std::string cls = game::concept_category_class(d, 0);
        const auto list = d.arcade_classes.find(cls);
        CHECK(list != d.arcade_classes.end() && !list->second.empty());
        if (list == d.arcade_classes.end()) continue;
        std::string entry, course, dirt_course;
        int adjustable = 0, made = 0, as_row = 0;
        for (const game::GtData::ArcadeCar& ac : list->second) {
            const std::optional<game::GarageCar> g = game::concept_new_car(gt, ac.label);
            CHECK(g.has_value());
            if (!g) continue;
            ++made;
            // A new garage car races with its arcade row's engine output factor; with the row's whole tune too unless
            // fitting the Normal set put a part's settings back to its own.
            for (const data::CarVariant& v : d.db.arcade_cars()) {
                if (v.label != ac.label) continue;
                const std::optional<sim::exact::Equipment> eq = race::exact_equipment(d.db, race::ExactCarSource{ac.label, g->parts, g->settings});
                CHECK(eq.has_value());
                if (!eq) continue;
                CHECK(eq->word(0x1B8) == rd16(v.bytes, 0x10A) && eq->word(0x1B8) > 0);
                if (eq->setting == sim::exact::variant_equipment(d.db, v).setting) ++as_row;
            }
            CHECK(g->origin == ac.label && game::concept_car_normal(gt, *g));
            CHECK(tire_stage(gt, g->parts, false) == 0 && tire_stage(gt, g->parts, true) == 0);
            CHECK(game::concept_tire_position(gt, *g, false) == 1 && game::concept_tire_position(gt, *g, true) == 1);
            game::GarageCar c = *g;
            game::concept_fit_custom(gt, c);
            if (!game::concept_car_normal(gt, c)) ++adjustable;
            if (entry.empty() && !game::concept_dirt_entry(ac.label) && !game::concept_car_normal(gt, c)) entry = ac.label;
        }
        std::printf("    %s: %d cars, %d with a Custom set, %d as their rows are tuned\n", cls.c_str(), made, adjustable, as_row);
        CHECK(made > 0 && adjustable > 0 && !entry.empty());
        if (entry.empty()) continue;
        for (const auto& [name, courses] : d.arcade_courses)
            for (const game::GtData::ArcadeCourse& c : courses) (gt.arcade_course_dirt(c.course) ? dirt_course : course) = c.course;
        CHECK(!course.empty());

        // The garage keeps one car per entry; it becomes the current car.
        game::GarageCar* car = game::concept_garage_car(gt, entry, true);
        CHECK(car != nullptr && gt.state().garage.size() == 1 && gt.state().ride_car() == car);
        if (!car) continue;
        CHECK(game::concept_garage_car(gt, entry, false) == car && gt.state().garage.size() == 1);
        CHECK(game::setting_start_page() == "root");
        const sim::CarSettings normal = car->settings;

        // Custom: its own parts; the tires stay; each axle's tire has three positions of its own.
        game::concept_fit_custom(gt, *car);
        CHECK(!game::concept_car_normal(gt, *car) && game::setting_start_page() == "custom");
        CHECK(tire_stage(gt, car->parts, false) == 0);
        const bool soft = game::concept_set_tire(gt, *car, false, 0);
        if (soft) {
            CHECK(game::concept_tire_position(gt, *car, false) == 0 && game::concept_tire_position(gt, *car, true) == 1);
            CHECK(tire_stage(gt, car->parts, false) == 7 && tire_stage(gt, car->parts, true) == 0);
            CHECK(game::concept_set_tire(gt, *car, true, 2) && tire_stage(gt, car->parts, true) == 3);
        }
        std::printf("    %s: soft and hard tires %s\n", entry.c_str(), soft ? "on the disc" : "not on the disc");
        // The race keeps the car's tires on a road course, takes the drift tires when they were chosen and the dirt tires
        // on a dirt course.
        CHECK(game::concept_race_parts(gt, *car, course, false) == car->parts);
        const std::array<u64, 27> drift = game::concept_race_parts(gt, *car, course, true);
        CHECK(tire_stage(gt, drift, false) == 11 && tire_stage(gt, drift, true) == 11);
        if (!dirt_course.empty()) {
            const std::array<u64, 27> dirt = game::concept_race_parts(gt, *car, dirt_course, true);
            CHECK(tire_stage(gt, dirt, false) == 12 && tire_stage(gt, dirt, true) == 12);
        }
        // Reset: the Custom set again, every setting its part's own; Normal: back as it was made.
        car->settings.final_drive += 1;
        game::concept_reset_custom(gt, *car);
        CHECK(!game::concept_car_normal(gt, *car) && game::concept_tire_position(gt, *car, false) == 1 &&
              game::concept_tire_position(gt, *car, true) == 1);
        game::concept_fit_normal(gt, *car);
        CHECK(game::concept_car_normal(gt, *car) && car->settings.final_drive == normal.final_drive &&
              car->settings.spring == normal.spring && car->settings.ride_height == normal.ride_height);

        // The menu: the Normal page; Custom fits the set and opens the Custom page, whose tire page moves the front tire
        // and whose Normal button goes back; Return ends the menu.
        CHECK(game::begin_setting("A_test_0001"));
        GameFlow f(vfs_reader(vfs), lang);
        f.start("setting", game::setting_start_page());
        settle(f);
        CHECK(f.control() == "setting" && page(f) == "root");
        CHECK(focus_item(f, "item12"));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "custom" && !game::concept_car_normal(gt, *gt.state().ride_car()));
        // (The pages' buttons are picked by direction: from Custom, Normal lies to the left.)
        CHECK(focus_item(f, "custom"));
        press(f, MenuKey::Left);
        CHECK(f.controller()->focused() && f.controller()->focused()->flow->name == "normal");
        CHECK(focus_item(f, "tire"));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "set/adjast_tir");
        if (soft && page(f) == "set/adjast_tir") {
            CHECK(focus_item(f, "slider_tire_f"));
            press(f, MenuKey::Left);
            CHECK(tire_stage(gt, gt.state().ride_car()->parts, false) == 0); // (not before OK)
            CHECK(focus_item(f, "ok2"));
            press(f, MenuKey::Confirm);
            for (int i = 0; i < 200 && page(f) != "custom"; ++i) f.tick(0);
            settle(f);
            CHECK(page(f) == "custom" && tire_stage(gt, gt.state().ride_car()->parts, false) == 7);
        } else {
            press(f, MenuKey::Cancel);
        }
        CHECK(page(f) == "custom" && focus_item(f, "normal"));
        press(f, MenuKey::Confirm);
        CHECK(page(f) == "root" && game::concept_car_normal(gt, *gt.state().ride_car()));
        CHECK(focus_item(f, "exit"));
        press(f, MenuKey::Confirm);
        for (int i = 0; i < 200 && !f.exited(); ++i) f.tick(0);
        CHECK(f.exited());
        // Cancel on the Normal page leaves too (the page's back name "exit": the menu ends).
        {
            CHECK(game::begin_setting("A_test_0001"));
            GameFlow g(vfs_reader(vfs), lang);
            g.start("setting", game::setting_start_page());
            settle(g);
            CHECK(page(g) == "root");
            press(g, MenuKey::Cancel);
            for (int i = 0; i < 200 && !g.exited(); ++i) g.tick(0);
            CHECK(g.exited());
        }

        // A race request takes the garage car: its parts and settings, and they follow the settings menu.
        if (const ConceptHooks& h = concept_hooks(); h.prepare) {
            GameRequest r;
            r.mode = "single_race", r.course = course, r.car_class = cls, r.car = game::concept_car_image(entry);
            h.prepare(r);
            CHECK(r.car == entry && r.car_parts && r.car_settings && *r.car_parts == gt.state().ride_car()->parts);
            CHECK(!game::concept_refresh_cars(r));
            game::concept_fit_custom(gt, *gt.state().ride_car());
            CHECK(game::concept_refresh_cars(r) && r.car_parts && *r.car_parts == gt.state().ride_car()->parts);
        }
    }
    game::reset_gt_mode_menu_for_tests();
}
