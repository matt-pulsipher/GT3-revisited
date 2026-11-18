// Gran Turismo Concept's menu bindings (the Concept spec): what the front end (menu/flow/concept_flow.cpp) asks
// the game. The course and car pages are the arcade pickers, so GT3's arcade hooks (gt_mode_menu/arcade.cpp) hand
// over to the concept_* functions here on a Concept disc; the rest goes through ConceptHooks. The rules are in
// concept_state.h, the disc's lists in concept_game.h; the progress lives in the game state and is saved with it.

#include "game/concept_menu.h"

#include "base/file.h"
#include "base/log.h"
#include "game/concept_game.h"
#include "game/gt_save.h"
#include "game/gt_mode_menu/internal.h"
#include "menu/flow/concept_hooks.h"
#include "sim/exact/params_build.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace gt3r::game {

using namespace gt_menu;

namespace {

// The rules' lists of a disc (read with its data).
const ConceptRules& rules_of(const GtData& d) { return d.concept_lists; }

ConceptState& state() { return S()->gt().state().concept_game; }

// The class entry whose picture an entry of the car page is, in `car_class` (or any class: the 3D car).
const GtData::ArcadeCar* entry_car(const GtData& d, const std::string& car_class, const std::string& image, bool dirt) {
    const auto it = d.arcade_classes.find(car_class);
    if (it == d.arcade_classes.end()) return nullptr;
    for (const GtData::ArcadeCar& c : it->second)
        if (concept_dirt_entry(c.label) == dirt && c.image() == image) return &c;
    return nullptr;
}

// The present movies (the programs' full-screen movie table, concept §2.3: row 1 for MOVIE0, row 2 for MOVIE1
// and the Professional milestone): Tokyo-Geneva's names, else the earlier releases'.
std::string movie_row(int row) {
    static const char* const kRows[2][2] = {{"stream/opening_GTCJP.pss", "stream/opening_GT3UK.pss"},
                                            {"stream/ending_GTC.pss", "stream/ending_long_full.pss"}};
    // (The Tokyo-Geneva discs also carry the earlier releases' files; their programs, the ones that load a
    // database/paramdb_gtc_* file, name the first of each pair.)
    const bool geneva = region_data(menu_state().reader).db_suffix().rfind("_gtc", 0) == 0;
    return kRows[row == 2 ? 1 : 0][geneva ? 0 : 1];
}

std::string medal_time(u32 ms) { return ms ? format_time_ms(ms) : std::string("--'--.---"); }

// The presentations of a prize pass's grants, in the milestone sequence's order (concept §8 step 3): the cars one
// after another, then the pages of MOVIE0, MOVIE1 (and its movie), the pod race and the credit transfer.
void queue_grants(const GtData& d, const ConceptRules& rules, const ConceptRun& run, const std::vector<ConceptGrant>& grants) {
    MenuState& ms = menu_state();
    auto simple = [&](const char* page) {
        Show sh{};
        sh.set = sh.page = page;
        sh.layers = {"delay"};
        ms.shows.push_back(std::move(sh));
    };
    int cars = 0;
    for (const ConceptGrant& g : grants) {
        if (g.kind != 0) continue;
        const ConceptRules::Key* k = rules.key(g.name);
        // An "_S" key names the dirt entry of its base key's car (the game configs' lists): the two open
        // together and the car is presented once, by the base key.
        if (k && k->twin && std::any_of(grants.begin(), grants.end(), [&](const ConceptGrant& o) {
                return o.name + "_S" == g.name;
            }))
            continue;
        const GtData::ArcadeCar* ac = k ? concept_key_car(d, *k) : nullptr;
        if (!ac) continue;
        Show sh{};
        sh.set = sh.page = "concept_car_get";
        // The reason line: a license key's by its medal, a race key's by its level; the car's own name layer.
        const char* reason = k->kind == 'L' ? (k->variant == 1 ? "gold_record" : "bronze_record") : k->kind == 'H' ? "pro_clear" : "normal_clear";
        sh.layers = {reason, "car" + std::to_string(std::min(cars, 3))};
        for (const data::CarVariant& v : d.db.arcade_cars())
            if (v.label == ac->label) sh.car = v.car;
        sh.reason = g.name;
        ms.shows.push_back(std::move(sh));
        ++cars;
    }
    (void)run;
    auto has = [&](int kind) { return std::any_of(grants.begin(), grants.end(), [&](const ConceptGrant& g) { return g.kind == kind; }); };
    if (has(3)) simple("concept_moviea");
    if (has(4)) {
        simple("concept_ace");
        ms.movies.push_back(movie_row(2)); // (after the pages: the ending movie)
    }
    if (has(1)) simple("concept_pod");
    if (has(2)) simple("concept_billion");
}

} // namespace

bool concept_active() {
    const GtModeSession* s = S();
    return s && s->gt().data().game == GameKind::Concept;
}

const ConceptRules* concept_rules_now() { return concept_active() ? &rules_of(S()->gt().data()) : nullptr; }

std::string concept_entry_car(const std::string& entry) {
    const GtData& d = S()->gt().data();
    for (const auto& [cls, cars] : d.arcade_classes)
        for (const GtData::ArcadeCar& c : cars)
            if (c.image() == entry)
                for (const data::CarVariant& v : d.db.arcade_cars())
                    if (v.label == c.label) return v.car;
    return d.db.car(entry) ? entry : std::string();
}

void concept_arcade_entries(const std::string& page, const std::string& game, const std::string& car_class,
                            std::vector<std::string>& entries) {
    const GtMode& gt = S()->gt();
    const GtData& d = gt.data();
    const ConceptRules& rules = rules_of(d);
    auto keep_in_order = [&](const std::vector<std::string>& wanted) {
        std::vector<std::string> out;
        for (const std::string& w : wanted)
            if (std::find(entries.begin(), entries.end(), w) != entries.end() && std::find(out.begin(), out.end(), w) == out.end())
                out.push_back(w);
        entries = std::move(out);
    };
    if (page == "ar/select_course") {
        // The game's course list; Single Race offers a course once its license has a medal, the other
        // games all of them.
        const char* list = game == "time_attack" ? "TIME_ATTACK" : game == "two_player_battle" ? "TWO_PLAYER" : "SINGLE_RACE";
        std::vector<std::string> wanted;
        if (auto it = d.arcade_courses.find(list); it != d.arcade_courses.end())
            for (const GtData::ArcadeCourse& c : it->second)
                if (game != "single_race" || concept_course_open(state(), rules.course_index(c.course))) wanted.push_back(c.course);
        keep_in_order(wanted);
    }
    (void)car_class; // (the car page's entries: ConceptHooks::cars, which knows the course)
}

std::vector<std::pair<std::string, std::string>> concept_course_parts(const std::string& entry) {
    std::vector<std::pair<std::string, std::string>> out;
    const GtData& d = S()->gt().data();
    u32 len = 0;
    for (const auto& [list, courses] : d.arcade_courses)
        for (const GtData::ArcadeCourse& c : courses)
            if (c.course == entry) len = c.length_m;
    out.push_back({"total_length", ""});
    char b[32];
    if (!len) std::snprintf(b, sizeof b, "----");
    else if (d.imperial()) std::snprintf(b, sizeof b, "%.3fmiles", double(len) / 1609.344);
    else std::snprintf(b, sizeof b, "%4um", len);
    out.push_back({"length", b});
    return out;
}

namespace {

// The garage cars of a request (concept §10; the race descriptor, the concept control): Single Race, Free Run
// and 2P Battle take each player's car from the garage, as its settings menu left it, on the tires the race gives it;
// player 1's becomes the current car.
void fill_garage_cars(GameRequest& r, GtMode& gt) {
    if (r.mode != "single_race" && r.mode != "free_run" && r.mode != "two_player_battle") return;
    if (r.car_class == "CLASS_PODRACE") return; // (the pods run as they are)
    if (const GarageCar* g = concept_garage_car(gt, r.car, true)) {
        r.car_parts = concept_race_parts(gt, *g, r.course, r.tire_mode == 1);
        r.car_settings = g->settings;
    }
    if (!r.car2.empty())
        if (const GarageCar* g = concept_garage_car(gt, r.car2, false)) {
            r.car2_parts = concept_race_parts(gt, *g, r.course, r.tire_mode2 == 1);
            r.car2_settings = g->settings;
        }
}

} // namespace

bool concept_refresh_cars(GameRequest& r) {
    if (!concept_active() || !r.car_parts) return false;
    const std::optional<std::array<std::uint64_t, 27>> parts = r.car_parts;
    const std::optional<sim::CarSettings> settings = r.car_settings;
    fill_garage_cars(r, S()->gt());
    // (Compared as the race's equipment record holds them.)
    const data::CarDatabase& db = S()->gt().db();
    const auto words = [&](const std::optional<sim::CarSettings>& st) {
        return sim::exact::make_equipment(db, {}, *parts, st.value_or(sim::CarSettings{})).setting;
    };
    const bool changed = parts != r.car_parts || words(settings) != words(r.car_settings);
    if (changed) menu_state().last_request = r;
    return changed;
}

void concept_prepare(GameRequest& r) {
    GtMode& gt = S()->gt();
    const GtData& d = gt.data();
    const bool dirt = gt.arcade_course_dirt(r.course);
    // The car page's entries are pictures: the class entry gives the car's own label and its race.
    auto resolve = [&](std::string& car, const std::string& car_class) -> const GtData::ArcadeCar* {
        const GtData::ArcadeCar* ac = entry_car(d, car_class, car, dirt);
        if (!ac) ac = entry_car(d, car_class, "_" + car, dirt);
        if (ac) car = ac->label;
        return ac;
    };
    const GtData::ArcadeCar* ac = resolve(r.car, r.car_class);
    if (!r.car2.empty()) resolve(r.car2, r.car2_class);
    // Single Race: the class entry's code with its last character raised by the level.
    if (r.mode == "single_race" && ac) r.race_code = concept_race_code(ac->race_code, std::clamp(r.difficulty, 0, kConceptLevels - 1));
    if (r.mode == "go_license") r.event = r.race_code;
    fill_garage_cars(r, gt);
    menu_state().last_request = r;
}

namespace {

// A run's result into the records (§8 step 1): a license test's medal, a single race's place.
void record_result(ConceptState& st, const ConceptRun& run, const GameResult& r) {
    if (run.license()) {
        // The medal the race judged (GameResult::license_medal: 0 gold .. 2 bronze).
        if (r.license_medal >= 0 && r.license_medal <= 2) st.record_license(run.course, r.license_medal, r.total_time);
    } else if (r.finished && r.started && r.position > 0) {
        st.record_place(run.level(), run.course, r.position - 1);
    }
}

// Names the notice was shown for in this visit of the race (the original flags them at once; ours flags them with
// the visit's result, concept_on_result).
// The name of the present granted last, for Game Status to open on (the session record's stored name, §8 step 4).
std::string& last_grant() {
    static std::string name;
    return name;
}

std::set<std::string>& noticed() {
    static std::set<std::string> names;
    return names;
}

} // namespace

bool queue_concept_notice(const GameResult& r) {
    if (!concept_active()) return false;
    const ConceptRules& rules = rules_of(S()->gt().data());
    const ConceptRun run = concept_run(rules, r.race_code, r.course);
    if (run.mode < 0 || (!run.license() && r.mode != "single_race")) return false;
    ConceptState after = state();
    record_result(after, run, r);
    bool any = false;
    for (const ConceptGrant& g : concept_prize_pass(rules, after, run)) any = noticed().insert(g.name).second || any;
    if (!any) return false;
    Show sh{};
    sh.set = "notice_present";
    sh.page = "root";
    menu_state().shows.push_back(std::move(sh));
    return true;
}

std::string concept_trophy_model(const std::string& race_code) {
    if (!concept_active()) return {};
    // The event's class byte (+0x1D3) picks the cup, as GT3's arcade classes number them: 1 easy .. 4 special
    // (Concept's rows: 1 Normal, 2 Professional, 3 Ace).
    const data::Event* ev = S()->gt().data().db.event(race_code);
    static const char* const kClass[5] = {nullptr, "easy", "normal", "hard", "special"};
    const int c = ev && ev->bytes.size() > 0x1D3 ? ev->bytes[0x1D3] : 0;
    return c >= 1 && c <= 4 ? std::string("race/a_") + kClass[c] + "_gold.prm" : std::string();
}

// The game's progress saves itself after a run; on a new game nothing has made the disc's save folder yet.
static void save_progress(GtModeSession* s, const GameState* state = nullptr) {
    const std::string path = s->save_path();
    if (const size_t slash = path.find_last_of('/'); slash != std::string::npos && slash > 0) host_make_dir(path.substr(0, slash));
    std::string err;
    if (!(state ? write_save(path, *state, &err) : s->save(&err))) LOG_WARN("concept: save failed: %s", err.c_str());
}

void concept_on_result(const GameResult& r) {
    GtModeSession* s = S();
    GtMode& gt = s->gt();
    const GtData& d = gt.data();
    const ConceptRules& rules = rules_of(d);
    ConceptState& st = state();
    const ConceptRun run = concept_run(rules, r.race_code, r.course);
    if (run.mode < 0 || (!run.license() && r.mode != "single_race")) noticed().clear();
    // The pod races, 2 player races and free runs record nothing and grant nothing (concept §8, §9.4).
    if (run.mode < 0 || (!run.license() && r.mode != "single_race")) {
        if (r.records_done && saving_enabled()) save_progress(s);
        return;
    }
    LOG_INFO("concept: %s on course %d: place %d, medal %d", r.race_code.c_str(), run.course, r.position, r.license_medal);
    record_result(st, run, r);
    noticed().clear();
    // The prize pass; what it grants is presented, then flagged (§8 steps 2 to 4).
    const std::vector<ConceptGrant> grants = concept_prize_pass(rules, st, run);
    for (const ConceptGrant& g : grants) LOG_INFO("concept: %s granted", g.name.c_str());
    if (!grants.empty()) last_grant() = grants.back().name;
    queue_grants(d, rules, run, grants);
    concept_set_presents(st, grants);
    if (saving_enabled()) save_progress(s);
}

bool concept_save_license_run(const std::string& code, int medal, double seconds) {
    if (!concept_active()) return false;
    GtModeSession* s = S();
    const ConceptRun run = concept_run(rules_of(s->gt().data()), code, {});
    if (!run.license() || medal < 0 || !saving_enabled()) return true;
    // A copy: the live state is recorded when the result reaches the menus (with the prize pass).
    GameState copy = s->gt().state();
    copy.concept_game.record_license(run.course, std::min(medal, 2), seconds);
    save_progress(s, &copy);
    return true;
}

bool concept_car_for_widget(const std::string& page, const std::string& cls, std::string& car, int& paint) {
    const MenuState& ms = menu_state();
    if (page == "concept_car_get" && cls == "render_model" && ms.show && ms.show->page == page && !ms.show->car.empty()) {
        car = ms.show->car;
        paint = ms.show->paint;
        return true;
    }
    return false;
}

std::optional<std::string> concept_widget_text(const std::string& page, const Element& e) {
    const MenuState& ms = menu_state();
    if (page != "concept_car_get" || !e.widget_class || !starts(*e.widget_class, "car_name_") || !ms.show || !e.text) return std::nullopt;
    // "You have won this car!" with the car's name (M: how the original joins the two was not read).
    return e.text->text + " " + car_name(S()->gt(), ms.show->car);
}

void install_concept_menu() {
    menu::flow::ConceptHooks h;
    h.attract_steps = [] {
        return concept_active() ? concept_attract_steps(region_data(menu_state().reader)) : std::vector<std::string>();
    };
    h.category_cars = [](int category, const std::string& course) {
        if (!concept_active()) return 0;
        const GtMode& gt = S()->gt();
        return int(concept_open_cars(gt.data(), rules_of(gt.data()), state(), concept_category_class(gt.data(), category),
                                     gt.arcade_course_dirt(course))
                       .size());
    };
    h.cars = [](const std::string& car_class, const std::string& course) {
        // The category's cars of the course's kind that are not locked, in list order, by picture.
        std::vector<std::string> out;
        if (!concept_active()) return out;
        const GtMode& gt = S()->gt();
        for (const ConceptEntry& e : concept_open_cars(gt.data(), rules_of(gt.data()), state(), car_class, gt.arcade_course_dirt(course)))
            out.push_back(e.car->image());
        return out;
    };
    h.category_class = [](int category) { return concept_active() ? concept_category_class(S()->gt().data(), category) : std::string(); };
    h.course_dirt = [](const std::string& course) { return concept_active() && S()->gt().arcade_course_dirt(course); };
    h.course_place = [](int level, const std::string& course) {
        if (!concept_active()) return kConceptNoPlace;
        return state().place(level, rules_of(S()->gt().data()).course_index(course));
    };
    h.license_tests = [] {
        std::vector<menu::flow::ConceptHooks::Test> out;
        if (!concept_active()) return out;
        const GtMode& gt = S()->gt();
        const GtData& d = gt.data();
        const auto cls = d.arcade_classes.find("CLASS_LICENSE");
        if (cls == d.arcade_classes.end()) return out;
        for (size_t i = 0; i < cls->second.size(); ++i) {
            menu::flow::ConceptHooks::Test t;
            t.code = cls->second[i].race_code;
            t.medal = state().medal(int(i));
            if (const data::Event* ev = d.db.event(t.code)) {
                t.course = ev->course;
                if (ev->bytes.size() >= 0x1D0)
                    for (int m = 0; m < 3; ++m) {
                        // Each {u8 minutes, u8 seconds, u16 milliseconds}, as GT3's (race/license.h).
                        const size_t o = 0x1C4 + 4 * size_t(m);
                        t.times[size_t(m)] = medal_time(u32(ev->bytes[o]) * 60000 + u32(ev->bytes[o + 1]) * 1000 + rd16(ev->bytes, o + 2));
                    }
            }
            for (const data::CarVariant& v : d.db.arcade_cars())
                if (v.label == cls->second[i].label) t.car = car_name(gt, v.car);
            out.push_back(std::move(t));
        }
        return out;
    };
    h.status_reached = [](const std::string& item) {
        return concept_active() && concept_status_reached(rules_of(S()->gt().data()), state(), item);
    };
    h.completion_text = [] {
        if (!concept_active()) return std::string();
        char b[16];
        std::snprintf(b, sizeof b, "%d%%", int(concept_completion(rules_of(S()->gt().data()), state()) * 100.0f));
        return std::string(b);
    };
    h.license_medal = [](int course) { return concept_active() ? state().medal(course) : kConceptNoMedal; };
    h.pod = [] {
        menu::flow::ConceptHooks::Pod p;
        if (!concept_active()) return p;
        const GtData& d = S()->gt().data();
        const auto course = d.arcade_courses.find("POD_RACE");
        const auto cls = d.arcade_classes.find("CLASS_PODRACE");
        if (course == d.arcade_courses.end() || course->second.empty() || cls == d.arcade_classes.end() || cls->second.empty()) return p;
        p.course = course->second[0].course;
        p.car_class = "CLASS_PODRACE";
        p.entry = cls->second[0].image();
        return p;
    };
    h.last_grant = [] { return std::exchange(last_grant(), std::string()); };
    h.course_won = [](int course, bool pro) {
        return concept_active() && (pro ? state().won_pro(course) : state().won_any(course));
    };
    h.key_image = [](const std::string& key) {
        if (!concept_active()) return std::string();
        const GtData& d = S()->gt().data();
        const ConceptRules::Key* k = rules_of(d).key(key);
        const GtData::ArcadeCar* ac = k ? concept_key_car(d, *k) : nullptr;
        return ac ? ac->image() : std::string();
    };
    h.present_movie = [](const std::string& item) {
        if (!concept_active() || !concept_status_reached(rules_of(S()->gt().data()), state(), item)) return std::string();
        return item == kConceptMovie0 ? movie_row(1) : item == kConceptMovie1 ? movie_row(2) : std::string();
    };
    h.set_car = [](const std::string& key) {
        if (!concept_active()) return;
        GtModeSession* s = S();
        const GtData& d = s->gt().data();
        const ConceptRules::Key* k = rules_of(d).key(key);
        const GtData::ArcadeCar* ac = k ? concept_key_car(d, *k) : nullptr;
        s->focus_car.clear();
        if (ac)
            for (const data::CarVariant& v : d.db.arcade_cars())
                if (v.label == ac->label) s->focus_car = v.car;
        s->color_cursor = 0;
    };
    h.prepare = [](GameRequest& r) {
        if (concept_active()) concept_prepare(r);
    };
    menu::flow::set_concept_hooks(std::move(h));
}

} // namespace gt3r::game
