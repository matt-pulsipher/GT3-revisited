// Quick race: any event of the game with any car, nothing to unlock. The GT Mode
// leagues' races (Beginner, Amateur, Professional, Rally, Endurance), the licence and
// machine tests, the arcade single race classes and the arcade time trials, each
// against its own opponents (the event's entries), with any disc or mod car.
//
// Two pages: the event list (by category, with the course, laps, opponents and the
// event's rules shown as information only - they are not enforced), then the setup
// (car, colour, gearbox; arcade: difficulty and course) and Start. The car page lists the cars
// by maker or, as online, by the arcade's classes (a car in no list by its weight per power;
// a class list's cars race as the arcade's own). The race is the race
// screen as the menus start it (make_menu_race_screen), with GameRequest::practice set:
// races run as the arcade single race does, tests as GT mode runs them, and nothing is
// written to the GT Mode save. Leaving the race comes back here.
//
// Event names are the game's own: the titles the GT Mode menus show for their items
// (game_menu_titles(): "Sunday Cup", "Practical Cornering I"), else made from the course.
// The last choices are kept in config.ini [quickrace].
//
// The list's first item is a custom race, the player's own, on one page: any course, the laps
// and the lineup (app/opponent_grid.h): one to five opponents, as many as the course's start
// grid holds, and the player's car and place among them. It runs on the arcade's Normal race (Class S's, on dirt the dirt race's):
// that race's start and tyre wear, and its rubber band if the player turns catch-up on, which
// would otherwise hold fast opponents back. Each opponent's skill is the driver overrides of
// the arcade race of that difficulty; the values are the disc's. Custom races can be saved
// under a name (the same name replaces one) and follow it in the list. The last one is kept in
// [quickrace_custom], saved ones in [quickrace_race<n>], with the same keys.
//
// Pointer input is the online lobby's: "<" ">" buttons step a value at one tap, a row chooses on a
// second tap (a mouse's hover focuses it first), the footer's actions are buttons, the list drags.
// The pages are tapped directly, so they show no on-screen pad.
//
// Scripted runs (tests, screenshots):
//   GT3R_QUICKRACE="event=Gsunday_e_0001,car=r34_gtr_v_99,paint=1,mt=1,start"
// selects the event (its id or race code) and car; `start` starts the race at once,
// `page=setup` / `page=cars` opens that page; `level=0..3` and `course=<label>` set the
// arcade difficulty and course. A custom race: `page=custom` or `race=<saved name>` opens it,
// with `course`, `car`, `paint`, `mt`, `laps=<n>`, `place=<n>` (1 pole), `catchup=1`,
// `save=<name>`, `start` and `grid=<car>:<paint>:<skill>/...` (skill 0..3 easy .. professional).

#include "app/app.h"
#include "app/gt_mode_titles.h"
#include "app/online_pickers.h"
#include "app/opponent_grid.h"
#include "app/screen_race.h"
#include "app/screen_registry.h"
#include "app/text_keyboard.h"
#include "app/ui.h"
#include "base/config.h"
#include "base/log.h"
#include "formats/course/course_file.h"
#include "formats/course/road.h"
#include "game/data/car_database.h"
#include "game/gt_mode.h"
#include "game/language_rules.h"
#include "game/net_car.h"
#include "menu/flow/game_hooks.h"
#include "menu/flow/menu_set.h"
#include "race/field.h"
#include "race/results.h"
#include "sim/exact/race_inputs.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace gt3r {

namespace {

// Database and menu text may hold non-ASCII bytes; the UI font is ASCII.
std::string ascii(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (c < 0x80) out += char(c);
        else if ((c & 0xC0) == 0xC0) out += '-';
    return out;
}

bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

enum class Kind { Race, Arcade, License, Machine, TimeAttack };

struct QuickEvent {
    std::string id;    // [quickrace] event, GT3R_QUICKRACE event=: the race code, "arcade_<class>"
    std::string code;  // event label (arcade: of the Normal level; the difficulty picks the letter)
    std::string name;  // as listed
    Kind kind = Kind::Race;
    std::string arcade_class; // arcade: "C", "B", "A", "S", "D" (dirt)
    std::string test_car;     // licence tests, time trials: the event's own car (menu car id)
};

struct Category {
    std::string title;
    std::vector<int> events; // indices into events_
};

constexpr const char* kCustomSection = "quickrace_custom"; // the last custom race
constexpr const char* kSavedSection = "quickrace_race";    // + a number: a saved one
constexpr int kMaxLaps = 99;

// A custom race as the list shows it (its section of config.ini).
struct CustomSummary {
    std::string section, name, course, car;
    int laps = 3, place = 0;
    std::vector<std::string> opponents;
};

struct Script {
    bool have = false, start = false;
    std::string event, car, page, course, race, save;
    int paint = -1, mt = -1, level = -1, laps = -1, place = -1, catchup = -1;
    std::vector<OpponentGrid::Car> grid;
};

Script read_script() {
    Script s;
    const char* env = SDL_getenv("GT3R_QUICKRACE");
    if (!env) return s;
    s.have = true;
    std::string all = env;
    size_t pos = 0;
    while (pos <= all.size()) {
        size_t end = all.find(',', pos);
        if (end == std::string::npos) end = all.size();
        std::string kv = all.substr(pos, end - pos);
        pos = end + 1;
        size_t eq = kv.find('=');
        std::string k = kv.substr(0, eq), v = eq == std::string::npos ? "" : kv.substr(eq + 1);
        if (k == "event") s.event = v;
        else if (k == "car") s.car = v;
        else if (k == "paint") s.paint = std::atoi(v.c_str());
        else if (k == "mt") s.mt = std::atoi(v.c_str());
        else if (k == "level") s.level = std::clamp(std::atoi(v.c_str()), 0, 3);
        else if (k == "course") s.course = v;
        else if (k == "page") s.page = v;
        else if (k == "laps") s.laps = std::atoi(v.c_str());
        else if (k == "place") s.place = std::atoi(v.c_str());
        else if (k == "catchup") s.catchup = std::atoi(v.c_str());
        else if (k == "race") s.race = v;
        else if (k == "save") s.save = v;
        else if (k == "grid") {
            for (size_t at = 0; at <= v.size() && int(s.grid.size()) < OpponentGrid::kMaxCars;) {
                size_t slash = std::min(v.find('/', at), v.size());
                s.grid.push_back(OpponentGrid::parse(v.substr(at, slash - at)));
                at = slash + 1;
            }
        } else if (k == "start") s.start = true;
    }
    return s;
}

// "1'23.456" (or "--" for none).
std::string lap_time(double s) {
    if (s < 0) return "--";
    int ms = int(s * 1000.0 + 0.5);
    char b[32];
    if (ms >= 3600000) std::snprintf(b, sizeof(b), "%d:%02d'%02d.%03d", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000);
    else if (ms >= 60000) std::snprintf(b, sizeof(b), "%d'%02d.%03d", ms / 60000, ms / 1000 % 60, ms % 1000);
    else std::snprintf(b, sizeof(b), "%d.%03d", ms / 1000, ms % 1000);
    return b;
}

class QuickRaceScreen : public Screen {
public:
    QuickRaceScreen() : script_(read_script()) {}

    void update(App& app, double) override {
        if (!init_) init(app);
        if (in_race_) { // back from the race
            in_race_ = false;
            app.menu().suppress();
            took_result(menu::flow::take_game_result());
        }
        if (script_.have) run_script(app);
        if (naming_) {
            if (name_keyboard_.is_open())
                if (TextKeyboard::Result r = name_keyboard_.update(app.menu(), name_pointer_.take(), name_text_);
                    r != TextKeyboard::Result::None)
                    end_naming(app, r == TextKeyboard::Result::Done);
            return;
        }
        if (page_ == Page::Events || page_ == Page::Setup || page_ == Page::Custom) pointer_actions(app);
        if (naming_) return;
        if (page_ == Page::Cars) update_cars(app);
        else if (page_ == Page::Courses) update_courses(app);
        else if (page_ == Page::Custom) update_custom(app);
        else if (page_ == Page::Setup) update_setup(app);
        else update_events(app);
    }

    // (the pages are tapped directly; the pickers keep the pad)
    TouchLayout touch_layout() const override { return page_ == Page::Cars || page_ == Page::Courses ? TouchLayout::Menu : TouchLayout::None; }

    bool clickable(float x, float y) const override {
        if (naming_) return name_keyboard_.is_open() && name_keyboard_.clickable(x, y);
        if (page_ == Page::Cars) return car_picker_.clickable(x, y);
        if (page_ == Page::Courses) return course_picker_.clickable(x, y);
        return hits_.at(x, y) != nullptr;
    }

    void on_event(App& app, const SDL_Event& e) override {
        if (naming_) {
            name_event(app, e);
            return;
        }
        if (page_ == Page::Cars) car_picker_.event(e); // the pickers' pointer (online_pickers.h)
        else if (page_ == Page::Courses) course_picker_.event(e);
        else pointer_.event(e);
    }

    void render(App& app, Ui& ui) override {
        // A screen pushed from another's update renders before its own first update.
        if (!init_) init(app);
        ui.clear();
        hits_.clear();
        if (!error_.empty()) {
            ui.header("QUICK RACE", "");
            ui.text_center(150, error_, colors::kBad);
            footer_actions(ui, "", {{"Triangle: back", kFootBack}}, hits_, kHitFooter, pointer_);
            return;
        }
        if (page_ == Page::Cars) {
            car_picker_.render(app, ui);
            return;
        }
        if (page_ == Page::Courses) {
            course_picker_.render(app, ui, art_);
            return;
        }
        if (page_ == Page::Custom) render_custom(app, ui);
        else if (page_ == Page::Setup) render_setup(app, ui);
        else render_events(app, ui);
        if (naming_) render_naming(ui);
    }

private:
    enum class Page { Events, Setup, Cars, Courses, Custom };
    enum class Row { Car, Colour, Gearbox, Level, Course, Start };

    static constexpr float kListY = 26, kRowH = 16, kListW = 318;
    static constexpr int kListRows = 19;
    static constexpr float kSetupX = 12, kSetupR = 318, kSetupY = 234, kSetupH = 18; // the setup rows (under the car)
    // The custom race: the course, its rows and the car at the left, the lineup at the right, the buttons along the bottom.
    static constexpr float kLeftX = 8, kLeftW = 232, kLeftY = 110, kLeftH = 24, kGridX = 248, kGridR = 632, kGridY = 26;
    static constexpr float kButtonY = 316, kButtonH = 24;
    static constexpr int kSetupCar = -1, kCustomCar = -2; // what the car page chooses, or an opponent's (0 ..)
    enum Hit { kHitRow, kHitLeft, kHitRight, kHitButton, kHitFooter };
    enum Foot { kFootChoose, kFootBack };

    // --- pointer ------------------------------------------------------------------------

    // The pages' pointer, as the online lobby's: hover focuses a row; a click on an arrow steps its value, on a row chooses it
    // (a finger's first tap only focuses), on a button acts. A drag or the wheel scrolls the list.
    void pointer_actions(App& app) {
        using K = PointerInput::Action::Kind;
        for (const PointerInput::Action& a : pointer_.take()) {
            const Page page = page_;
            if (a.kind == K::Back) go_back(app);
            else if (a.kind == K::Hover || a.kind == K::Click) pointer_at(app, a);
            else if (page_ == Page::Events) scroll_list(a);
            if (page_ != page || naming_) return; // (the rest was for a page no longer on screen)
        }
    }

    void pointer_at(App& app, const PointerInput::Action& a) {
        const bool click = a.kind == PointerInput::Action::Kind::Click;
        const HitArea* h = hits_.at(a.x, a.y);
        if (!h) return;
        if (h->id == kHitFooter) {
            if (click && h->arg == kFootBack) go_back(app);
            else if (click) confirm(app);
            return;
        }
        int& sel = page_ == Page::Events ? row_ : page_ == Page::Setup ? setup_sel_ : custom_sel_;
        const int row = h->arg, id = h->id;
        const bool focused = sel == row;
        if (!focused) sel = row, delete_armed_ = false;
        if (!click) {
            if (!focused) picker_sound(app, "cursor");
        } else if (id == kHitLeft || id == kHitRight) {
            act(app, row, id == kHitLeft ? -1 : 1, false);
        } else if (id == kHitButton || focused || !a.finger) {
            act(app, row, 0, true);
        } else {
            picker_sound(app, "cursor");
        }
    }

    void act(App& app, int row, int h, bool cross) {
        if (page_ == Page::Events) choose_row(app);
        else if (page_ == Page::Setup) setup_action(app, setup_rows()[size_t(row)], h, cross);
        else custom_action(app, custom_rows()[size_t(row)], h, cross);
    }

    void confirm(App& app) {
        if (page_ == Page::Events) choose_row(app);
        else act(app, page_ == Page::Setup ? setup_sel_ : custom_sel_, 0, true);
    }

    void go_back(App& app) {
        if (page_ == Page::Events) {
            app.save_config();
            app.pop();
            return;
        }
        picker_sound(app, "cancel");
        if (page_ == Page::Custom) {
            leave_custom(app);
        } else {
            page_ = Page::Events;
            center_row();
        }
    }

    int last_first() const { return std::max(0, int(rows_.size()) - kListRows); }
    void center_row() { list_first_ = std::clamp(row_ - kListRows / 2, 0, last_first()); }

    void scroll_list(const PointerInput::Action& a) {
        using K = PointerInput::Action::Kind;
        if (a.kind == K::DragEnd) {
            list_drag_ = false;
        } else if (a.kind == K::Drag && a.sx < kListW) {
            if (!list_drag_) list_drag_ = true, drag_first_ = list_first_;
            list_first_ = std::clamp(drag_first_ - int(std::lround((a.y - a.sy) / kRowH)), 0, last_first());
        } else if (a.kind == K::Wheel && a.x < kListW) {
            list_first_ = std::clamp(list_first_ - int(std::lround(a.wheel * 3)), 0, last_first());
        }
    }

    // --- data -------------------------------------------------------------------

    void init(App& app) {
        init_ = true;
        if (!app.vfs()) {
            error_ = "No disc loaded";
            return;
        }
        if (!db_.load(*app.vfs())) {
            error_ = db_.player_error();
            return;
        }
        auto data = std::make_shared<game::GtData>();
        if (!data->load(menu::flow::vfs_reader(*app.vfs()))) LOG_WARN("Quick race: GT mode data: %s", data->error().c_str());
        gt_ = std::make_unique<game::GtMode>(data);
        for (const data::Car& c : db_.cars()) {
            std::string model = db_.model_of(c);
            if (model.empty() || !app.vfs()->exists("cars/day/" + model)) continue; // as the race screen lists them
            PickerCar e{c.label, ascii(c.name.empty() ? c.label : c.name), model, -1, db_.mod_car(c.label) != nullptr, c.maker, 0, {}, {}, 0, {}};
            if (e.mod) e.name += " (mod)";
            cars_.push_back(e);
        }
        std::sort(cars_.begin(), cars_.end(), [](const PickerCar& a, const PickerCar& b) { return a.name < b.name; });
        disc_cars_ = int(cars_.size());
        add_arcade_cars(app, db_, *data, cars_);
        facts_.assign(cars_.size(), std::nullopt);
        try {
            build_events(app, *data);
        } catch (const std::exception& e) { // (a disc whose events or menus cannot be read)
            LOG_WARN("Quick race: events: %s", e.what());
            categories_.clear(), events_.clear();
        }
        if (events_.empty()) {
            error_ = "No events found on the disc";
            return;
        }
        if (cars_.empty()) {
            error_ = "No cars found on the disc";
            return;
        }
        for (const data::Course& c : db_.courses())
            if (c.label.rfind("L_", 0) != 0 && has_course(app, c.label))
                courses_.push_back({c.label, ascii(c.name.empty() ? c.label : c.name)});
        grid_.attach(cars_, art_, car_picker_, [this](const std::string& l) { return grid_car(l); });
        if (db_.event(arcade_code("S", 1)) && !courses_.empty()) {
            has_custom_ = true;
            read_custom(app.config(), kCustomSection);
            custom_defaults(app);
            write_custom(app.config(), kCustomSection);
            read_saved(app);
        }
        build_rows();
        // The last choices.
        const IniDocument& cfg = app.config();
        car_picker_.set_by_class(cfg.get("quickrace", "car_page").value_or("makers") == "classes");
        car_ = find_car(cfg.get("quickrace", "car").value_or(""));
        paint_ = std::max(0, std::atoi(cfg.get("quickrace", "paint").value_or("0").c_str()));
        manual_ = cfg.get("quickrace", "gearbox").value_or("at") == "mt";
        level_ = std::clamp(std::atoi(cfg.get("quickrace", "level").value_or("1").c_str()), 0, 3);
        arcade_course_ = cfg.get("quickrace", "course").value_or("");
        select_event(cfg.get("quickrace", "event").value_or(""));
        center_row();
    }

    // The list: the custom races, then the events by category.
    void build_rows() {
        rows_.clear();
        if (has_custom_) {
            rows_.push_back({-1, -1, -1});
            rows_.push_back({-1, -1, 0});
            for (size_t k = 0; k < saved_.size(); ++k) rows_.push_back({-1, -1, int(k) + 1});
        }
        for (const Category& c : categories_) {
            rows_.push_back({&c - categories_.data(), -1, -1});
            for (int e : c.events) rows_.push_back({&c - categories_.data(), e, -1});
        }
    }

    bool selectable(int r) const { return rows_[size_t(r)].event >= 0 || rows_[size_t(r)].custom >= 0; }

    // Every event, by category: the GT Mode leagues in their menus' order, the licence tests,
    // the machine tests, then arcade mode's single race classes and time trials.
    void build_events(App& app, const game::GtData& data) {
        std::map<std::string, std::string> titles = game_menu_titles(*app.vfs(), app.language());
        const game::Catalog& cat = data.catalog;
        auto playable = [&](const std::string& code) {
            const data::Event* ev = db_.event(code);
            return ev && has_course(app, ev->course);
        };
        auto add = [&](Category& c, QuickEvent e) {
            c.events.push_back(int(events_.size()));
            events_.push_back(std::move(e));
        };
        // GT Mode leagues: the series in the order of their league page (gt_mode.mbl), each
        // race as "<series> - Race n" ("Round n" in a championship).
        const std::vector<const game::RaceSeries*> ordered = ordered_series(*app.vfs(), cat);
        for (const auto& [tier, title] : kLeagues) {
            Category c{title, {}};
            for (const game::RaceSeries* s : ordered) {
                if (s->tier != tier) continue;
                std::string series = titles.count(s->page) ? titles[s->page] : std::string();
                for (size_t i = 0; i < s->races.size(); ++i) {
                    const std::string& code = s->races[i];
                    if (!playable(code)) continue;
                    std::string name = series.empty() ? course_name(db_.event(code)->course) : series;
                    if (s->races.size() > 1)
                        name += (s->championship() ? " - Round " : " - Race ") + std::to_string(i + 1);
                    if (series.empty() && s->races.size() == 1) name = course_name(db_.event(code)->course);
                    add(c, {code, code, name, Kind::Race, {}, {}});
                }
            }
            if (!c.events.empty()) categories_.push_back(std::move(c));
        }
        // Licence tests: "B-1 Practical Cornering I", in the licence order.
        {
            Category c{"Licence Tests", {}};
            for (const char* id : {"b", "a", "ib", "ia", "s", "d"})
                if (const game::LicenseCourse* l = cat.license(id))
                    for (const std::string& code : l->tests) {
                        if (!playable(code)) continue;
                        const data::Event* ev = db_.event(code);
                        std::string name = license_test_label(code);
                        if (titles.count(code)) name += "  " + titles[code];
                        // The test's car in the game's language (gt_mode §21, docs/dev/GT_MODE.md "Language").
                        add(c, {code, code, name, Kind::License, {},
                                ev->entries.empty() ? "" : game::license_car(db_, *ev, db_.language()).entry});
                    }
            if (!c.events.empty()) categories_.push_back(std::move(c));
        }
        // Machine tests (GT Mode's: the player's own car).
        {
            Category c{"Machine Tests", {}};
            static const std::pair<const char*, const char*> kTests[] = {
                {"M400", "0-400 m"}, {"M1000", "0-1000 m"}, {"MMAX", "Top Speed"}};
            for (const auto& [code, name] : kTests)
                if (playable(code)) add(c, {code, code, name, Kind::Machine, {}, {}});
            if (!c.events.empty()) categories_.push_back(std::move(c));
        }
        // Arcade single race: one entry per car class; the difficulty and course are chosen
        // in the setup, as on the arcade pages. Race codes A{E,N,H,P}{C,B,A,S}T0001, dirt A?AD0001.
        {
            Category c{"Arcade Single Race", {}};
            static const std::pair<const char*, const char*> kClasses[] = {
                {"C", "Class C"}, {"B", "Class B"}, {"A", "Class A"}, {"S", "Class S"}, {"D", "Dirt"}};
            for (const auto& [cls, name] : kClasses) {
                QuickEvent e{std::string("arcade_") + cls, arcade_code(cls, 1), std::string("Single Race - ") + name,
                             Kind::Arcade, cls, {}};
                if (db_.event(e.code)) add(c, e);
            }
            if (!c.events.empty()) categories_.push_back(std::move(c));
            for (const auto& [game, list] : data.arcade_courses)
                if (game == "SINGLE_RACE")
                    for (const game::GtData::ArcadeCourse& ac : list)
                        if (has_course(app, ac.course)) arcade_courses_.push_back(ac.course);
            if (arcade_courses_.empty())
                for (const data::Course& co : db_.courses())
                    if (!starts(co.label, "L_") && !co.name.empty() && has_course(app, co.label)) arcade_courses_.push_back(co.label);
        }
        // Arcade time trials (compulsory): the course with its fixed car.
        {
            Category c{"Arcade Time Trial", {}};
            std::vector<QuickEvent> tt;
            if (auto it = data.arcade_classes.find("CLASS_COMPULSORY"); it != data.arcade_classes.end())
                for (const game::GtData::ArcadeCar& a : it->second) {
                    if (a.race_code.empty() || !playable(a.race_code)) continue;
                    std::string car = car_name(a.label);
                    tt.push_back({a.race_code, a.race_code,
                                  "Time Trial - " + course_name(db_.event(a.race_code)->course) + (car.empty() ? "" : " (" + car + ")"),
                                  Kind::TimeAttack, {}, a.label});
                }
            std::sort(tt.begin(), tt.end(), [](const QuickEvent& a, const QuickEvent& b) { return a.code < b.code; });
            for (QuickEvent& e : tt) add(c, std::move(e));
            if (!c.events.empty()) categories_.push_back(std::move(c));
        }
        LOG_INFO("Quick race: %zu events in %zu categories, %zu cars", events_.size(), categories_.size(), cars_.size());
    }

    // "L0B0003" -> "B-3", "LIA0007" -> "IA-7", "L0R0001" -> "R-1".
    static std::string arcade_code(const std::string& cls, int level) {
        static const char kLevel[] = "ENHP";
        std::string code = std::string("A") + kLevel[std::clamp(level, 0, 3)];
        return code + (cls == "D" ? "AD" : cls + "T") + "0001";
    }

    bool has_course(App& app, const std::string& label) { return !course_file_path(*app.vfs(), label).empty(); }

    std::string course_name(const std::string& label) const {
        for (const data::Course& c : db_.courses())
            if (c.label == label && !c.name.empty()) return ascii(c.name);
        return label;
    }

    // A car id the menus use (base car label, or a variant / arcade car label) -> its base car.
    std::string base_car(const std::string& id) const {
        for (const std::string& l : {id, "_" + id}) {
            if (db_.car(l)) return l;
            if (const data::CarVariant* v = db_.variant(l)) return v->car;
            for (const data::CarVariant& v : db_.arcade_cars())
                if (v.label == l && db_.car(v.car)) return v.car;
        }
        return {};
    }

    std::string car_name(const std::string& id) const {
        const data::Car* c = db_.car(base_car(id));
        return c ? ascii(c->latin_name.empty() ? c->label : c->latin_name) : std::string();
    }

    int find_car(const std::string& label) const {
        for (size_t i = 0; i < cars_.size(); ++i)
            if (car_key(cars_[i]) == label) return int(i);
        return -1;
    }

    // A label's place on the list: the class pages' arcade car or the car itself, else an event entry's car.
    int grid_car(const std::string& label) const {
        const int k = find_car(label);
        return k >= 0 ? k : find_car(base_car(label));
    }

    // The car page's classes for a race on this kind of course.
    void assign_classes(bool dirt) {
        assign_car_classes(cars_, dirt, [this](int i) -> const game::CarFacts& {
            std::optional<game::CarFacts>& f = facts_[size_t(i)];
            if (!f) {
                const PickerCar& c = cars_[size_t(i)];
                f = game::online_car_facts(db_, &gt_->data(), c.arcade.empty() ? game::online_car_from_db(db_, c.label, 0)
                                                                                : game::online_car_from_arcade(db_, c.arcade, 0));
            }
            return *f;
        });
    }

    const QuickEvent& event() const { return events_[size_t(rows_[size_t(row_)].event)]; }

    // The race code to run (arcade: of the chosen difficulty).
    std::string event_code() const {
        const QuickEvent& e = event();
        return e.kind == Kind::Arcade ? arcade_code(e.arcade_class, level_) : e.code;
    }

    // The courses an arcade class races on: dirt courses for the dirt class, else the others.
    std::vector<std::string> arcade_courses() const {
        std::vector<std::string> out;
        bool dirt = event().arcade_class == "D";
        for (const std::string& l : arcade_courses_)
            if (dirt_course(l) == dirt) out.push_back(l);
        return out.empty() ? arcade_courses_ : out;
    }

    bool dirt_course(const std::string& label) const {
        bool dirt = false;
        for (const data::Course& c : db_.courses())
            if (c.label == label) dirt = c.surface == 2;
        return dirt;
    }

    std::string event_course() const {
        const QuickEvent& e = event();
        if (e.kind == Kind::Arcade) {
            std::vector<std::string> list = arcade_courses();
            if (std::find(list.begin(), list.end(), arcade_course_) != list.end()) return arcade_course_;
            return list.empty() ? std::string() : list.front();
        }
        const data::Event* ev = db_.event(e.code);
        return ev ? ev->course : std::string();
    }

    void select_event(const std::string& id) {
        row_ = -1;
        for (size_t r = 0; r < rows_.size(); ++r)
            if (selectable(int(r)) && row_ < 0) row_ = int(r); // the list's first item by default
        for (size_t r = 0; r < rows_.size(); ++r)
            if (rows_[r].event >= 0 && (events_[size_t(rows_[r].event)].id == id || events_[size_t(rows_[r].event)].code == id))
                row_ = int(r);
    }

    // The car a race starts with when none was chosen yet: the event's first opponent's.
    void default_car() {
        const QuickEvent& e = event();
        own_car_ = e.test_car.empty();
        if (car_ >= 0) return;
        if (const data::Event* ev = db_.event(event_code()); ev && !ev->entries.empty())
            car_ = find_car(base_car(e.kind == Kind::License && !e.test_car.empty() ? e.test_car : ev->entries[0]));
        if (car_ < 0) car_ = find_car("r34_gtr_v_99");
        if (car_ < 0) car_ = 0;
        paint_ = 0;
    }

    // --- event page -------------------------------------------------------------------

    struct ListRow {
        ptrdiff_t category;
        int event;  // -1 = the category's heading, or a custom race
        int custom; // 0 the custom race, n a saved one (saved_[n - 1]), -1 = none
    };

    void step_row(int d) {
        int r = row_;
        do r += d;
        while (r >= 0 && r < int(rows_.size()) && !selectable(r));
        if (r >= 0 && r < int(rows_.size())) row_ = r;
    }

    void update_events(App& app) {
        const MenuInput& m = app.menu();
        if (m.pressed(PadButton::Triangle)) {
            go_back(app);
            return;
        }
        if (!error_.empty()) return;
        int before = row_;
        if (int v = m.vertical()) step_row(v > 0 ? 1 : -1);
        int h = m.horizontal();
        if (m.pressed(PadButton::L1)) h = -1;
        if (m.pressed(PadButton::R1)) h = 1;
        if (h) { // the previous / next category
            ptrdiff_t cat = rows_[size_t(row_)].category + (h > 0 ? 1 : -1);
            for (size_t r = 0; r < rows_.size(); ++r)
                if (rows_[r].category == cat && selectable(int(r))) {
                    row_ = int(r);
                    break;
                }
        }
        if (row_ != before) {
            center_row();
            picker_sound(app, "cursor");
        }
        if (m.pressed(PadButton::Cross)) choose_row(app);
    }

    void choose_row(App& app) {
        if (rows_[size_t(row_)].custom >= 0) open_custom(app);
        else choose_event(app);
    }

    void choose_event(App& app) {
        picker_sound(app, "ok");
        default_car();
        page_ = Page::Setup;
        setup_sel_ = int(setup_rows().size()) - 1; // on Start
        remember(app);
    }

    void render_events(App& app, Ui& ui) {
        ui.header("QUICK RACE", "Any event, any car - practice: nothing goes into the GT Mode save");
        for (int i = 0; i < kListRows && list_first_ + i < int(rows_.size()); ++i) {
            const ListRow& r = rows_[size_t(list_first_ + i)];
            float y = kListY + float(i) * kRowH;
            if (!selectable(list_first_ + i)) {
                ui.text(12, y + 4, r.category < 0 ? "Custom Races" : categories_[size_t(r.category)].title, colors::kAccent);
                continue;
            }
            bool sel = list_first_ + i == row_;
            if (sel) ui.fill_round(8, y, kListW - 8, kRowH - 1, 3, colors::kHighlight);
            hits_.add({8, y, kListW - 8, kRowH}, kHitRow, list_first_ + i);
            const std::string name = r.custom == 0 ? "New custom race" : r.custom > 0 ? saved_[size_t(r.custom - 1)].name : events_[size_t(r.event)].name;
            ui.text(24, y + 4, Ui::clip(name, kListW - 30), sel ? colors::kAccent : colors::kText);
        }
        ui.scrollbar(UiRect{8, kListY, kListW - 6, kListRows * kRowH}, float(rows_.size()) * kRowH, float(list_first_) * kRowH);
        if (const int c = rows_[size_t(row_)].custom; c >= 0) render_custom_info(app, ui, c ? saved_[size_t(c - 1)] : last_custom_);
        else render_info(app, ui, true);
        footer_actions(ui, "Up/Down: event   L1/R1 or Left/Right: category", {{"Cross: choose", kFootChoose}, {"Triangle: back", kFootBack}},
                       hits_, kHitFooter, pointer_);
    }

    // A course's still with its logo in `still`, framed; its map or name without one.
    void course_art(App& app, Ui& ui, const std::string& course, const SDL_FRect& still) {
        // Licence sections ("L_deepforest1a") show their course's art ("deepforest").
        std::string art = course;
        if (starts(art, "L_")) {
            art = art.substr(2);
            while (!art.empty() && (std::isdigit(u8(art.back())) || (art.size() > 1 && std::isdigit(u8(art[art.size() - 2])))))
                art.pop_back();
        }
        ui.fill_round(still.x - 2, still.y - 2, still.w + 4, still.h + 4, 4, colors::kOutline);
        if (!art_.course_still(app, art, still, true)) {
            ui.fill(still.x, still.y, still.w, still.h, colors::kPanel);
            if (!art_.course_map(app, art, SDL_FRect{still.x + 90, still.y + 10, still.w - 180, still.h - 20}))
                ui.text_center_in(still.x, still.w, still.y + still.h / 2 - 4, course_name(course), colors::kDim);
        } else {
            art_.course_logo(app, art, SDL_FRect{still.x + 4, still.y + still.h - 34, 96, 30});
        }
    }

    // The event at the right: the course's still, the name, course, laps, field, rules and
    // opponents (the setup page shows it without the opponents, its rows below).
    void render_info(App& app, Ui& ui, bool full) {
        const float x = 330, w = 302;
        const QuickEvent& e = event();
        std::string code = event_code(), course = event_course();
        const data::Event* ev = db_.event(code);
        course_art(app, ui, course, SDL_FRect{x, 28, w, 128});
        float y = 162;
        ui.text(x, y, Ui::clip(e.name, w), colors::kAccent);
        y += 14;
        auto line = [&](std::string label, const std::string& value, Color c = colors::kText) {
            for (const std::string& l : Ui::wrap(value, w - 70)) {
                ui.text(x, y, label, colors::kDim);
                ui.text(x + 70, y, l, c);
                y += 11;
                label.clear(); // on the first line only
            }
        };
        std::string lc = course_name(course);
        line("Course", e.kind == Kind::Arcade && full ? lc + "  (chosen in the setup)" : lc);
        if (ev) {
            std::string laps;
            race::EventStart st = race::event_start(*ev);
            if (e.kind == Kind::Machine) laps = code == "MMAX" ? "top speed run" : "standing start to the mark";
            else if (e.kind == Kind::TimeAttack) laps = std::to_string(race::kTimeTrialLaps) + " lap"; // (presentation §B7)
            else if (ev->laps == 0 && st.time_limit_s > 0) laps = std::to_string(int(st.time_limit_s / 60 + 0.5)) + " minutes";
            else if (ev->laps) laps = std::to_string(ev->laps) + (ev->laps == 1 ? " lap" : " laps");
            if (!laps.empty()) line(e.kind == Kind::Race || e.kind == Kind::Arcade ? "Race" : "Run", laps);
            std::optional<game::EventRules> r = gt_ ? gt_->rules(code) : std::nullopt;
            if (e.kind == Kind::Race || e.kind == Kind::Arcade)
                line("Field", std::to_string(r && r->cars ? r->cars : int(ev->cars_in_race)) + " cars");
            if (r && e.kind == Kind::License && r->license_ms[0]) {
                std::string t = "gold " + lap_time(r->license_ms[0] / 1000.0);
                if (r->license_ms[1]) t += "  silver " + lap_time(r->license_ms[1] / 1000.0);
                if (r->license_ms[2]) t += "  bronze " + lap_time(r->license_ms[2] / 1000.0);
                line("Targets", t);
            }
            if (!e.test_car.empty()) line("Car", car_name(e.test_car) + " (the test's car, or your own)");
            std::string rules = rules_text(r);
            if (!rules.empty()) line("Rules", rules + " (not enforced)", colors::kDim);
            if (full && (e.kind == Kind::Race || e.kind == Kind::Arcade)) {
                std::vector<std::string> names;
                for (const std::string& v : ev->entries) {
                    std::string n = car_name(v);
                    if (!n.empty() && std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
                }
                std::string all;
                for (size_t i = 0; i < names.size(); ++i) all += (i ? ", " : "") + names[i];
                if (!all.empty()) {
                    // The opponents fill the rest of the panel.
                    std::vector<std::string> lines = Ui::wrap(all, w - 70);
                    for (size_t i = 0; i < lines.size() && y < Ui::kHeight - 40; ++i) {
                        if (i == 0) ui.text(x, y, "Opponents", colors::kDim);
                        ui.text(x + 70, y, y + 11 >= Ui::kHeight - 40 && i + 1 < lines.size() ? Ui::clip(lines[i] + " ...", w - 70) : lines[i],
                                colors::kText);
                        y += 11;
                    }
                }
            }
        }
        if (full && !last_result_.empty()) ui.text(x, Ui::kHeight - 30, Ui::clip(last_result_, w), colors::kGood);
    }

    // The event's entry rules, as GT mode checks them (info only here).
    std::string rules_text(const std::optional<game::EventRules>& r) const {
        if (!r) return {};
        std::vector<std::string> parts;
        static const char* const kLic[] = {"", "B", "A", "IB", "IA", "S", "R"};
        if (r->required_license > 0 && r->required_license < 7) parts.push_back(std::string(kLic[r->required_license]) + " licence");
        static const char* const kDrive[] = {"", "FF", "FR", "4WD", "MR", "RR"};
        if (r->drivetrain > 0 && r->drivetrain < 6) parts.push_back(std::string(kDrive[r->drivetrain]) + " only");
        if (r->aspiration == 1) parts.push_back("NA only");
        if (r->aspiration == 2) parts.push_back("turbo only");
        if (r->car_class == 1) parts.push_back("race cars");
        if (r->car_class == 2) parts.push_back("tuned cars");
        if (r->car_class == 3) parts.push_back("stock production cars");
        if (r->dirt_tires) parts.push_back("dirt tyres");
        if (!r->regulation.empty())
            for (const data::Regulation& g : db_.regulations())
                if (g.label == r->regulation) parts.push_back("listed cars only (" + std::to_string(g.cars.size()) + ")");
        std::string out;
        for (size_t i = 0; i < parts.size(); ++i) out += (i ? ", " : "") + parts[i];
        return out;
    }

    // Why a car could not enter the event in GT mode (stock car; "" = it could).
    std::string car_note(int i) const {
        if (!gt_ || i < 0 || size_t(i) >= cars_.size()) return {};
        const QuickEvent& e = event();
        if (e.kind != Kind::Race) return {};
        std::optional<game::GarageCar> gc = gt_->make_car(cars_[size_t(i)].label);
        if (!gc) return {};
        game::EntryResult r = gt_->check_entry(e.code, *gc);
        std::vector<std::string> why;
        if (r.regulation) why.push_back("not on the event's list");
        if (r.drivetrain) why.push_back("drivetrain");
        if (r.aspiration) why.push_back("aspiration");
        if (r.car_class) why.push_back("car class");
        if (why.empty()) return {};
        std::string out = "GT Mode would refuse it (";
        for (size_t k = 0; k < why.size(); ++k) out += (k ? ", " : "") + why[k];
        return out + ") - allowed here";
    }

    // --- setup page -------------------------------------------------------------------

    std::vector<Row> setup_rows() const {
        std::vector<Row> rows = {Row::Car};
        if (own_car_) rows.push_back(Row::Colour);
        rows.push_back(Row::Gearbox);
        if (event().kind == Kind::Arcade) rows.push_back(Row::Level), rows.push_back(Row::Course);
        rows.push_back(Row::Start);
        return rows;
    }

    int paint_count(App& app) { return std::max(1, int(car_picker_.paints(app, cars_[size_t(car_)]).size())); }

    void update_setup(App& app) {
        const MenuInput& m = app.menu();
        if (m.pressed(PadButton::Triangle)) {
            go_back(app);
            return;
        }
        std::vector<Row> rows = setup_rows();
        int n = int(rows.size());
        if (int v = m.vertical()) {
            setup_sel_ = (setup_sel_ + v + n) % n;
            picker_sound(app, "cursor");
        }
        setup_sel_ = std::clamp(setup_sel_, 0, n - 1);
        int h = m.horizontal();
        if (m.pressed(PadButton::L1)) h = -10;
        if (m.pressed(PadButton::R1)) h = 10;
        if (h || m.pressed(PadButton::Cross)) setup_action(app, rows[size_t(setup_sel_)], h, m.pressed(PadButton::Cross));
    }

    void setup_action(App& app, Row row, int h, bool cross) {
        const QuickEvent& e = event();
        switch (row) {
        case Row::Car:
            if (cross) {
                picking_ = kSetupCar;
                assign_classes(dirt_course(event_course()));
                car_picker_.open(app, art_, db_, cars_, car_, own_car_ ? paint_ : 0, [this](int i, int) { return car_note(i); });
                page_ = Page::Cars;
                picker_sound(app, "ok");
                return;
            }
            if (!e.test_car.empty() && ((own_car_ && car_ == 0 && h < 0) || (!own_car_ && h > 0))) {
                own_car_ = !own_car_; // the test's car sits before the first car
            } else if (own_car_) {
                int n = disc_cars_; // (the class pages' arcade cars follow them: the same cars again)
                if (car_ >= n) car_ = std::max(find_car(cars_[size_t(car_)].label), 0);
                car_ = std::clamp(car_ + h, 0, n - 1);
                if (std::abs(h) == 1) car_ = (car_ + n) % n;
                paint_ = 0;
            } else {
                own_car_ = true;
                car_ = h > 0 ? 0 : int(cars_.size()) - 1;
            }
            break;
        case Row::Colour: paint_ = (paint_ + (h < 0 ? -1 : 1) + paint_count(app)) % paint_count(app); break;
        case Row::Gearbox: manual_ = !manual_; break;
        case Row::Level: level_ = (level_ + (h < 0 ? -1 : 1) + 4) % 4; break;
        case Row::Course: {
            std::vector<std::string> list = arcade_courses();
            if (list.empty()) break;
            auto it = std::find(list.begin(), list.end(), event_course());
            int i = it == list.end() ? 0 : int(it - list.begin());
            arcade_course_ = list[size_t((i + (h < 0 ? -1 : 1) + int(list.size())) % int(list.size()))];
            break;
        }
        case Row::Start:
            if (cross) start_race(app);
            return;
        }
        picker_sound(app, "cursor");
        remember(app);
    }

    void render_setup(App& app, Ui& ui) {
        const QuickEvent& e = event();
        ui.header("QUICK RACE", e.name);
        render_info(app, ui, false);
        // The car at the left: turning in the arcade showroom, with its paint.
        const PickerCar& c = cars_[size_t(car_)];
        PickerCar shown = c;
        int paint = paint_;
        if (!own_car_) {
            int k = find_car(base_car(e.test_car));
            if (k >= 0) shown = cars_[size_t(k)];
            paint = 0;
        }
        const SDL_FRect stage{8, 28, 310, 170};
        ui.fill_round(stage.x, stage.y, stage.w, stage.h, 6, colors::kPanel);
        turntable_.draw(app, stage, db_, shown, paint, art_);
        ui.text(16, 204, Ui::clip(own_car_ ? c.name : car_name(e.test_car), 300), colors::kText);
        if (own_car_)
            if (std::string note = car_note(car_); !note.empty())
                ui.text(16, 217, Ui::clip(note, 300), colors::kDim);
        // The rows.
        std::vector<Row> rows = setup_rows();
        std::vector<PaintInfo> pal = car_picker_.paints(app, c);
        for (size_t i = 0; i < rows.size(); ++i) {
            const float y = kSetupY + float(i) * kSetupH;
            const bool f = int(i) == setup_sel_;
            const UiRect row{kSetupX - 4, y, kSetupR - kSetupX + 4, kSetupH - 1};
            if (f) ui.fill_round(row.x, row.y, row.w, row.h, 4, colors::kHighlight);
            hits_.add(row, kHitRow, int(i));
            const UiRect arrows{kSetupX + 80, y + 1, kSetupR - kSetupX - 84, kSetupH - 3};
            const float vx = arrows.x + 30; // (where value_arrows puts the value)
            std::string label, value;
            float indent = 0;
            switch (rows[i]) {
            case Row::Car:
                label = "Car";
                if (own_car_) {
                    if (art_.car_icon(app, car_icon_name(c), SDL_FRect{vx, y + 1, 26, 14})) indent = 30;
                    value = c.name;
                } else {
                    value = "The test's car: " + car_name(e.test_car);
                }
                break;
            case Row::Colour: {
                label = "Colour";
                int cur = std::clamp(paint_, 0, std::max(0, int(pal.size()) - 1));
                if (!pal.empty()) draw_paint_chip(ui, vx, y + 4, 18, 9, pal[size_t(cur)].rgb, false), indent = 24;
                std::string pname = pal.empty() ? "" : pal[size_t(cur)].name;
                value = (pname.empty() ? "" : pname + "  ") + std::to_string(cur + 1) + " of " + std::to_string(std::max<size_t>(1, pal.size()));
                break;
            }
            case Row::Gearbox: label = "Transmission"; value = manual_ ? "Manual" : "Automatic"; break;
            case Row::Level: label = "Difficulty"; value = OpponentGrid::skill_name(level_); break;
            case Row::Course: label = "Course"; value = course_name(event_course()); break;
            case Row::Start: label = "Start race"; break;
            }
            ui.text(kSetupX + 4, y + 4, label, f ? colors::kAccent : rows[i] == Row::Start ? colors::kGood : colors::kText);
            if (rows[i] != Row::Start) value_arrows(ui, arrows, value, colors::kText, hits_, kHitLeft, kHitRight, int(i), pointer_, indent);
        }
        if (!last_result_.empty()) ui.text(330, Ui::kHeight - 30, Ui::clip(last_result_, 302), colors::kGood);
        footer_actions(ui, "Up/Down: choose   Left/Right: change", {{"Cross: choose", kFootChoose}, {"Triangle: events", kFootBack}}, hits_,
                       kHitFooter, pointer_);
    }

    void update_cars(App& app) {
        CarPicker::Result r = car_picker_.update(app);
        if (r == CarPicker::Result::None) return;
        page_ = picking_ == kSetupCar ? Page::Setup : Page::Custom;
        if (r == CarPicker::Result::Chosen) {
            if (picking_ == kSetupCar) {
                car_ = car_picker_.car();
                paint_ = car_picker_.paint();
                own_car_ = true;
            } else if (picking_ == kCustomCar) {
                custom_.car = car_picker_.car();
                custom_.paint = car_picker_.paint();
            } else {
                grid_.set_car(picking_, car_picker_.car(), car_picker_.paint());
            }
            remember(app);
        }
        picking_ = kSetupCar;
    }

    // --- custom race ------------------------------------------------------------------

    void read_custom(const IniDocument& cfg, const std::string& section) {
        custom_ = {};
        custom_.course = cfg.get(section, "course").value_or("");
        custom_.laps = std::clamp(std::atoi(cfg.get(section, "laps").value_or("3").c_str()), 1, kMaxLaps);
        custom_.car = find_car(cfg.get(section, "car").value_or(""));
        custom_.paint = std::max(0, std::atoi(cfg.get(section, "paint").value_or("0").c_str()));
        custom_.manual = cfg.get(section, "gearbox").value_or("at") == "mt";
        custom_.catch_up = cfg.get(section, "catch_up").value_or("off") == "on";
        grid_.read(cfg, section);
    }

    void write_custom(IniDocument& cfg, const std::string& section) const {
        cfg.set(section, "course", custom_.course);
        cfg.set(section, "laps", std::to_string(custom_.laps));
        cfg.set(section, "car", car_key(cars_[size_t(custom_.car)]));
        cfg.set(section, "paint", std::to_string(custom_.paint));
        cfg.set(section, "gearbox", custom_.manual ? "mt" : "at");
        cfg.set(section, "catch_up", custom_.catch_up ? "on" : "off");
        grid_.write(cfg, section);
    }

    // What the first custom race starts from, and what another disc or removed mods left out.
    void custom_defaults(App& app) {
        auto listed = [&](const std::string& label) {
            return std::any_of(courses_.begin(), courses_.end(), [&](const PickerCourse& c) { return c.label == label; });
        };
        if (!listed(custom_.course)) custom_.course = listed("trialmt") ? "trialmt" : courses_.front().label;
        if (custom_.car < 0) {
            custom_.car = std::max(find_car("r34_gtr_v_99"), 0);
            custom_.paint = 0;
        }
        const data::Event* ev = db_.event(custom_event());
        grid_.fill(app, ev ? ev->entries : std::vector<std::string>{}, custom_.car, OpponentGrid::kMaxCars);
        grid_.set_limit(course_grid_limit(app, custom_.course));
        grid_.set_player(custom_.car, custom_.paint);
    }

    // The arcade race a custom race runs on.
    std::string custom_event() const { return arcade_code(dirt_course(custom_.course) ? "D" : "S", 1); }

    // The event a skill takes its driver overrides from; "" = none on this disc.
    std::string skill_event(int skill) const {
        std::string code = arcade_code(dirt_course(custom_.course) ? "D" : "S", skill);
        return db_.event(code) ? code : std::string();
    }

    // What a skill sets in a car's driver, in the disc's numbers (the first driveline class's, ai §9.2).
    std::string skill_facts(int skill) const {
        const data::Event* ev = db_.event(skill_event(skill));
        if (!ev || ev->bytes.size() < 0x1F4) return {};
        const sim::exact::DriverOverrides d = sim::exact::driver_overrides(sim::exact::race_config(ev->bytes));
        auto pct = [](u8 v) { return std::to_string(v == 0 || v > 100 ? 100 : int(v)) + "%"; };
        return "Throttle " + pct(d[0]) + ", brake " + pct(d[4]) + ", corner speed " + pct(d[8]);
    }

    // How many opponents a course's start grid holds (the player takes a place).
    int course_grid_limit(App& app, const std::string& course) {
        auto [it, fresh] = grid_limits_.try_emplace(course, OpponentGrid::kMaxCars);
        if (fresh)
            if (std::optional<Bytes> f = app.vfs()->read(course_file_path(*app.vfs(), course))) try {
                const course::CourseFile cf = course::CourseFile::parse(*f);
                const int slots = int(course::Road::parse(cf.section(*f, course::kSlotRoad)).grid.size());
                it->second = std::clamp(slots - 1, 1, OpponentGrid::kMaxCars);
            } catch (const std::exception& e) {
                LOG_WARN("Quick race: %s's start grid: %s", course.c_str(), e.what());
            }
        return it->second;
    }

    // A course's length in metres from the arcade course lists (0 = unknown).
    u32 course_length(const std::string& label) const {
        if (gt_)
            for (const auto& [game_name, list] : gt_->data().arcade_courses)
                for (const game::GtData::ArcadeCourse& c : list)
                    if (c.course == label && c.length_m) return c.length_m;
        return 0;
    }

    void open_custom(App& app) {
        picker_sound(app, "ok");
        const int c = rows_[size_t(row_)].custom;
        saved_section_ = c > 0 ? saved_[size_t(c - 1)].section : std::string();
        saved_name_ = c > 0 ? saved_[size_t(c - 1)].name : std::string();
        read_custom(app.config(), c > 0 ? saved_section_ : kCustomSection);
        custom_defaults(app);
        page_ = Page::Custom;
        custom_sel_ = int(custom_rows().size()) - 1; // on Start
        remember(app);
    }

    void leave_custom(App& app) {
        page_ = Page::Events;
        delete_armed_ = false;
        remember(app);
        const std::string section = std::exchange(saved_section_, std::string());
        saved_name_.clear();
        refresh_list(app, section);
    }

    // The custom races changed: the list again, the cursor on that saved race ("" = on the custom race).
    void refresh_list(App& app, const std::string& section) {
        read_saved(app);
        build_rows();
        select_custom(section);
        center_row();
    }

    void select_custom(const std::string& section) {
        for (size_t r = 0; r < rows_.size(); ++r)
            if (rows_[r].custom == 0 || (rows_[r].custom > 0 && saved_[size_t(rows_[r].custom - 1)].section == section)) row_ = int(r);
    }

    CustomSummary summary(App& app, const std::string& section) const {
        const IniDocument& cfg = app.config();
        CustomSummary r;
        r.section = section;
        r.name = ascii(cfg.get(section, "name").value_or(""));
        r.course = cfg.get(section, "course").value_or("");
        r.car = cfg.get(section, "car").value_or("");
        r.laps = std::clamp(std::atoi(cfg.get(section, "laps").value_or("3").c_str()), 1, kMaxLaps);
        r.opponents = OpponentGrid::labels(cfg, section);
        r.place = std::min(OpponentGrid::place_in(cfg, section), int(r.opponents.size()));
        return r;
    }

    // The last custom race, and the saved races on courses this disc has.
    void read_saved(App& app) {
        last_custom_ = summary(app, kCustomSection);
        saved_.clear();
        for (const IniDocument::Section& sec : app.config().sections())
            if (sec.name.rfind(kSavedSection, 0) == 0) {
                CustomSummary r = summary(app, sec.name);
                if (!r.name.empty() && has_course(app, r.course)) saved_.push_back(std::move(r));
            }
    }

    // The race as it is now, under this name (a race of that name is replaced).
    void save_race(App& app, const std::string& name) {
        IniDocument& cfg = app.config();
        std::string section;
        int last = 0;
        for (const IniDocument::Section& sec : cfg.sections())
            if (sec.name.rfind(kSavedSection, 0) == 0) {
                last = std::max(last, std::atoi(sec.name.c_str() + std::string(kSavedSection).size()));
                if (cfg.get(sec.name, "name").value_or("") == name) section = sec.name;
            }
        if (section.empty()) section = kSavedSection + std::to_string(last + 1);
        write_custom(cfg, section);
        cfg.set(section, "name", name);
        app.save_config();
        saved_section_ = section, saved_name_ = name;
        refresh_list(app, section);
        LOG_INFO("Quick race: saved \"%s\" (%s)", name.c_str(), section.c_str());
    }

    void delete_race(App& app) {
        std::erase_if(app.config().sections(), [&](const IniDocument::Section& sec) { return sec.name == saved_section_; });
        app.save_config();
        LOG_INFO("Quick race: deleted \"%s\" (%s)", saved_name_.c_str(), saved_section_.c_str());
        saved_section_.clear(), saved_name_.clear();
        refresh_list(app, "");
    }

    // What a new saved race is called until the player types a name.
    std::string race_name() const {
        if (!saved_name_.empty()) return saved_name_;
        std::string name = cars_[size_t(grid_.car(0).car)].name;
        if (grid_.shown() > 1) name += " +" + std::to_string(grid_.shown() - 1);
        return (name + " at " + course_name(custom_.course)).substr(0, TextKeyboard::kMaxLength);
    }

    void begin_naming(App& app) {
        picker_sound(app, "ok");
        naming_ = true;
        name_text_ = race_name();
        if (SDL_Window* w = app.window()) SDL_StartTextInput(w);
#if GT3R_XR
        if (app.xr()) name_keyboard_.open("Save race as", "Save", false); // (no system keyboard in a headset)
#endif
    }

    void end_naming(App& app, bool keep) {
        naming_ = false;
        name_keyboard_.close();
        if (SDL_Window* w = app.window()) SDL_StopTextInput(w);
        app.menu().suppress(); // the keys typed are no menu input
        const std::string name(trim(name_text_));
        if (keep && !name.empty()) save_race(app, name);
    }

    void name_event(App& app, const SDL_Event& e) {
        const bool keyboard = name_keyboard_.is_open();
        float x = 0, y = 0;
        if (keyboard) {
            name_pointer_.event(e);
            // A real keyboard's letters only type (W is also Cross, Space Circle, ...); the arrows move between the keys.
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key != SDLK_UP && e.key.key != SDLK_DOWN && e.key.key != SDLK_LEFT &&
                e.key.key != SDLK_RIGHT)
                app.menu().suppress();
        } else if (pointer_down(e, x, y)) { // (a click elsewhere finishes it)
            end_naming(app, true);
            return;
        }
        if (const TextKeyboard::Result r = text_field_event(e, name_text_, !keyboard); r != TextKeyboard::Result::None)
            end_naming(app, r == TextKeyboard::Result::Done);
    }

    void render_naming(Ui& ui) {
        if (name_keyboard_.is_open()) {
            name_keyboard_.draw(ui, name_text_);
            return;
        }
        const float x = 120, y = 70, w = 400, h = 52; // (above a phone's keyboard)
        ui.fill_round(x - 2, y - 2, w + 4, h + 4, 6, colors::kOutline);
        ui.fill_round(x, y, w, h, 5, colors::kPanel);
        ui.text(x + 12, y + 10, "Save race as", colors::kAccent);
        ui.text(x + 12, y + 30, Ui::elide(name_text_ + "_", w - 24), colors::kText);
        ui.footer("Type a name, Enter to save, Esc to cancel (pad: up/down letter, right add, left delete, A save)");
    }

    // A custom race on the list's panel: the course's still, then the facts.
    void render_custom_info(App& app, Ui& ui, const CustomSummary& s) {
        const float x = 330, w = 302;
        course_art(app, ui, s.course, SDL_FRect{x, 28, w, 128});
        float y = 162;
        ui.text(x, y, Ui::clip(s.name.empty() ? "New custom race" : s.name, w), colors::kAccent);
        y += 14;
        auto line = [&](std::string label, const std::string& value) {
            for (const std::string& l : Ui::wrap(value, w - 70)) {
                ui.text(x, y, label, colors::kDim);
                ui.text(x + 70, y, l, colors::kText);
                y += 11;
                label.clear(); // on the first line only
            }
        };
        line("Course", course_name(s.course));
        line("Race", std::to_string(s.laps) + (s.laps == 1 ? " lap" : " laps"));
        line("Car", car_name(s.car));
        line("Field", std::to_string(s.opponents.size() + 1) + " cars");
        line("You start", OpponentGrid::ordinal(s.place + 1));
        std::string all;
        for (const std::string& o : s.opponents) all += (all.empty() ? "" : ", ") + car_name(o);
        line("Opponents", all);
    }

    // The page's rows: the race at the left, the lineup (OpponentGrid's rows) at the right, then what to do with it.
    enum class CRow { Course, Laps, Gearbox, CatchUp, Grid, Save, Delete, Start };
    struct CustomRow {
        CRow kind;
        int grid; // Grid: the grid's row
        UiRect r;
    };
    std::vector<CustomRow> custom_rows() const {
        std::vector<CustomRow> rows;
        float y = kLeftY;
        for (CRow k : {CRow::Course, CRow::Laps, CRow::Gearbox, CRow::CatchUp}) {
            rows.push_back({k, -1, {kLeftX, y, kLeftW, kLeftH}});
            y += kLeftH;
        }
        y = kGridY;
        for (int r = 0; r < grid_.rows(); ++r) {
            rows.push_back({CRow::Grid, r, {kGridX, y, kGridR - kGridX, grid_.row_h(r)}});
            y += grid_.row_h(r);
        }
        rows.push_back({CRow::Save, -1, {kLeftX, kButtonY, 112, kButtonH}});
        if (!saved_section_.empty()) rows.push_back({CRow::Delete, -1, {kLeftX + 118, kButtonY, 114, kButtonH}});
        rows.push_back({CRow::Start, -1, {kGridR - 200, kButtonY, 200, kButtonH}});
        return rows;
    }

    void update_custom(App& app) {
        const MenuInput& m = app.menu();
        if (m.pressed(PadButton::Triangle)) {
            go_back(app);
            return;
        }
        const std::vector<CustomRow> rows = custom_rows();
        const int n = int(rows.size());
        if (int v = m.vertical()) {
            custom_sel_ = (custom_sel_ + v + n) % n;
            delete_armed_ = false;
            picker_sound(app, "cursor");
        }
        custom_sel_ = std::clamp(custom_sel_, 0, n - 1);
        int h = m.horizontal();
        if (m.pressed(PadButton::L1)) h = -10;
        if (m.pressed(PadButton::R1)) h = 10;
        if (h || m.pressed(PadButton::Cross)) custom_action(app, rows[size_t(custom_sel_)], h, m.pressed(PadButton::Cross));
    }

    void open_car_page(App& app, int who, int car, int paint) {
        picking_ = who;
        assign_classes(dirt_course(custom_.course));
        car_picker_.open(app, art_, db_, cars_, car, paint, [](int, int) { return std::string(); });
        page_ = Page::Cars;
        picker_sound(app, "ok");
    }

    void custom_action(App& app, const CustomRow& row, int h, bool cross) {
        switch (row.kind) {
        case CRow::Course: {
            auto it = std::find_if(courses_.begin(), courses_.end(), [&](const PickerCourse& c) { return c.label == custom_.course; });
            const int n = int(courses_.size()), i = it == courses_.end() ? 0 : int(it - courses_.begin());
            if (cross) {
                course_picker_.set_labels("Custom race", "Triangle: back");
                course_picker_.open(courses_, i, [this](const std::string& c) { return course_length(c); });
                page_ = Page::Courses;
                picker_sound(app, "ok");
                return;
            }
            custom_.course = courses_[size_t((i + (h < 0 ? -1 : 1) + n) % n)].label;
            grid_.set_limit(course_grid_limit(app, custom_.course));
            break;
        }
        case CRow::Laps: custom_.laps = (custom_.laps - 1 + h + kMaxLaps) % kMaxLaps + 1; break;
        case CRow::Gearbox: custom_.manual = !custom_.manual; break;
        case CRow::CatchUp: custom_.catch_up = !custom_.catch_up; break;
        case CRow::Grid:
            if (const int who = grid_.action(row.grid, h, cross); who == OpponentGrid::kPlayer) {
                open_car_page(app, kCustomCar, custom_.car, custom_.paint);
                return;
            } else if (who != OpponentGrid::kNobody) {
                open_car_page(app, who, grid_.car(who).car, grid_.car(who).paint);
                return;
            }
            break;
        case CRow::Save:
            if (cross) begin_naming(app);
            return;
        case CRow::Delete: // (twice: it cannot be undone)
            if (!cross) return;
            if (delete_armed_) {
                delete_armed_ = false;
                delete_race(app);
                picker_sound(app, "ok");
            } else {
                delete_armed_ = true;
                picker_sound(app, "cursor");
            }
            return;
        case CRow::Start:
            if (cross) start_custom(app);
            return;
        }
        picker_sound(app, "cursor");
        remember(app);
    }

    // What the row under the cursor does, for the footer.
    std::string custom_hint(const CustomRow& row) const {
        if (row.kind == CRow::CatchUp) return "Catch-up: cars far ahead of you slow down, cars far behind you speed up";
        if (row.kind != CRow::Grid) return "Up/Down: choose   Left/Right: change";
        if (row.grid == OpponentGrid::kSkillRow) return "Sets every opponent's skill at once";
        if (row.grid == OpponentGrid::kPlaceRow) return "Where you start, 1st on pole";
        if (row.grid == OpponentGrid::kCountRow)
            return grid_.limit() < OpponentGrid::kMaxCars ? "This course's grid has room for " + std::to_string(grid_.limit()) + " opponents"
                                                          : "Up/Down: choose   Left/Right: change";
        const int who = grid_.who(row.grid);
        if (who == OpponentGrid::kPlayer) return "Cross: your car and colour";
        const int skill = grid_.car(who).skill;
        return std::string(OpponentGrid::skill_name(skill)) + ": " + skill_facts(skill);
    }

    void render_custom(App& app, Ui& ui) {
        ui.header("QUICK RACE", saved_name_.empty() ? "Custom race" : "Custom race: " + saved_name_);
        grid_.set_player(custom_.car, custom_.paint);
        const std::vector<CustomRow> rows = custom_rows();
        for (size_t i = 0; i < rows.size(); ++i) {
            const CustomRow& r = rows[i];
            const UiRect& b = r.r;
            const bool f = int(i) == custom_sel_;
            if (r.kind == CRow::Grid) {
                grid_.render_row(app, ui, r.grid, b, f, {hits_, pointer_, kHitRow, kHitLeft, kHitRight, int(i)});
                continue;
            }
            if (r.kind == CRow::Save || r.kind == CRow::Delete || r.kind == CRow::Start) {
                std::string label = r.kind == CRow::Save ? "Save race" : r.kind == CRow::Start ? "Start race" : "Delete race";
                if (r.kind == CRow::Delete && delete_armed_) label = "Press again";
                ui.button(b, label, f, false, r.kind == CRow::Start);
                hits_.add(b, kHitButton, int(i));
                continue;
            }
            if (f) ui.fill_round(b.x, b.y, b.w, b.h - 2, 4, colors::kHighlight);
            hits_.add({b.x, b.y, b.w, b.h - 2}, kHitRow, int(i));
            std::string label, value;
            switch (r.kind) {
            case CRow::Course: value = course_name(custom_.course); break;
            case CRow::Laps: label = "Laps", value = std::to_string(custom_.laps); break;
            case CRow::Gearbox: label = "Transmission", value = custom_.manual ? "Manual" : "Automatic"; break;
            default: label = "Catch-up", value = custom_.catch_up ? "On" : "Off"; break;
            }
            if (!label.empty()) ui.text(b.x + 6, b.y + (b.h - 2 - Ui::kChar) / 2, label, f ? colors::kAccent : colors::kText);
            const float vx = label.empty() ? b.x + 2 : b.x + 78;
            value_arrows(ui, {vx, b.y + 1, b.right() - vx - 2, b.h - 4}, value, colors::kText, hits_, kHitLeft, kHitRight, int(i), pointer_);
        }
        // The course, and the car under the cursor (an opponent's, else the player's).
        course_art(app, ui, custom_.course, SDL_FRect{kLeftX + 2, 28, kLeftW - 4, 76});
        const CustomRow& at = rows[size_t(std::clamp(custom_sel_, 0, int(rows.size()) - 1))];
        const int who = at.kind == CRow::Grid ? grid_.who(at.grid) : OpponentGrid::kNobody;
        const bool opponent = who >= 0 && who < OpponentGrid::kPlayer;
        const PickerCar& shown = cars_[size_t(opponent ? grid_.car(who).car : custom_.car)];
        const SDL_FRect stage{kLeftX, 210, kLeftW, 102};
        ui.fill_round(stage.x, stage.y, stage.w, stage.h, 6, colors::kPanel);
        turntable_.draw(app, stage, db_, shown, opponent ? grid_.car(who).paint : custom_.paint, art_);
        ui.text(stage.x + 8, stage.y + 6, Ui::clip(shown.name, stage.w - 16), colors::kText);
        if (!last_result_.empty()) ui.text(stage.x + 8, stage.y + stage.h - 12, Ui::clip(last_result_, stage.w - 16), colors::kGood);
        footer_actions(ui, custom_hint(at), {{"Cross: choose", kFootChoose}, {"Triangle: back", kFootBack}}, hits_, kHitFooter, pointer_);
    }

    void update_courses(App& app) {
        CoursePicker::Result r = course_picker_.update(app);
        if (r == CoursePicker::Result::None) return;
        page_ = Page::Custom;
        if (r != CoursePicker::Result::Chosen) return;
        custom_.course = courses_[size_t(course_picker_.course())].label;
        grid_.set_limit(course_grid_limit(app, custom_.course));
        remember(app);
    }

    void start_custom(App& app) {
        menu::flow::GameRequest rq;
        rq.kind = menu::flow::GameRequest::Kind::StartRace;
        rq.practice = true;
        rq.page = "quickrace";
        rq.mode = "single_race";
        rq.event = rq.race_code = custom_event();
        rq.course = custom_.course;
        rq.difficulty = 1;
        rq.laps = custom_.laps;
        rq.car = car_key(cars_[size_t(custom_.car)]);
        rq.car_color = custom_.paint;
        rq.transmission = custom_.manual ? 1 : 0;
        for (int k = 0; k < grid_.shown(); ++k) {
            const OpponentGrid::Car& g = grid_.car(k);
            rq.opponents.push_back(g.label);
            rq.opponent_paints.push_back(g.paint);
            rq.opponent_skills.push_back(skill_event(g.skill));
            LOG_INFO("Quick race: opponent %d: %s, paint %d, %s", k + 1, g.label.c_str(), g.paint, OpponentGrid::skill_name(g.skill));
        }
        rq.no_rubber_band = !custom_.catch_up;
        rq.player_slot = grid_.player_place();
        LOG_INFO("Quick race: custom race at %s, %d laps, with %s", custom_.course.c_str(), custom_.laps, rq.car.c_str());
        remember(app);
        (void)menu::flow::take_game_result(); // nothing stale from earlier races
        last_result_.clear();
        in_race_ = true;
        app.push(make_menu_race_screen(rq));
    }

    // --- race ---------------------------------------------------------------------

    void start_race(App& app) {
        const QuickEvent& e = event();
        menu::flow::GameRequest rq;
        rq.kind = menu::flow::GameRequest::Kind::StartRace;
        rq.practice = true;
        rq.page = "quickrace";
        rq.event = rq.race_code = event_code();
        switch (e.kind) {
        case Kind::Race: rq.mode = "single_race"; break;
        case Kind::Arcade:
            rq.mode = "single_race";
            rq.difficulty = level_;
            rq.course = event_course();
            rq.car_class = e.arcade_class == "D" ? "class_dirt" : "class_" + std::string(1, char(std::tolower(e.arcade_class[0])));
            break;
        case Kind::License: rq.mode = "go_license"; break;
        case Kind::Machine: rq.mode = "go_machine_test"; break;
        case Kind::TimeAttack: rq.mode = "time_trial"; break;
        }
        // The test's own car: the licence test's entry 0 (empty: the race screen takes it with
        // its parts and colour), the time trial's arcade car.
        if (own_car_) rq.car = car_key(cars_[size_t(car_)]);
        else if (e.kind == Kind::TimeAttack) rq.car = e.test_car;
        rq.car_color = own_car_ ? paint_ : 0;
        rq.transmission = manual_ ? 1 : 0;
        LOG_INFO("Quick race: %s (%s) with %s, paint %d, %s", e.name.c_str(), rq.event.c_str(),
                 rq.car.empty() ? "the test's car" : rq.car.c_str(), rq.car_color, manual_ ? "MT" : "AT");
        remember(app);
        (void)menu::flow::take_game_result(); // nothing stale from earlier races
        last_result_.clear();
        in_race_ = true;
        app.push(make_menu_race_screen(rq));
    }

    // The race's result (posted when the race screen was left).
    void took_result(const std::optional<menu::flow::GameResult>& r) {
        last_result_.clear();
        if (!r) return;
        if (r->license_medal >= 0) {
            static const char* const kMedal[] = {"Gold", "Silver", "Bronze"};
            last_result_ = std::string("Last run: ") + kMedal[std::min(r->license_medal, 2)] + "  " + lap_time(r->total_time);
        } else if (r->mode == "go_license" || r->mode == "go_machine_test") {
            last_result_ = r->finished ? "Last run: " + lap_time(r->total_time) : "Last run: not completed";
        } else if (r->cars > 1) {
            last_result_ = r->finished ? "Last race: " + OpponentGrid::ordinal(r->position) + " of " + std::to_string(r->cars) +
                                             "   best lap " + lap_time(r->best_lap)
                                       : "Last race: retired";
        } else if (r->best_lap >= 0) {
            last_result_ = "Last run: best lap " + lap_time(r->best_lap);
        }
    }

    void remember(App& app) {
        IniDocument& cfg = app.config();
        cfg.set("quickrace", "event", rows_[size_t(row_)].custom >= 0 ? "custom" : event().id);
        if (has_custom_) write_custom(cfg, kCustomSection);
        cfg.set("quickrace", "car_page", car_picker_.by_class() ? "classes" : "makers");
        if (own_car_ && car_ >= 0) {
            cfg.set("quickrace", "car", car_key(cars_[size_t(car_)]));
            cfg.set("quickrace", "paint", std::to_string(paint_));
        }
        cfg.set("quickrace", "gearbox", manual_ ? "mt" : "at");
        cfg.set("quickrace", "level", std::to_string(level_));
        if (!arcade_course_.empty()) cfg.set("quickrace", "course", arcade_course_);
    }

    // GT3R_QUICKRACE: once, on the first frame.
    void run_script(App& app) {
        Script s = script_;
        script_.have = false;
        if (!error_.empty()) return;
        if (has_custom_ && (s.page == "custom" || !s.race.empty() || !s.grid.empty())) {
            run_custom_script(app, s);
            return;
        }
        if (!s.event.empty()) select_event(s.event);
        if (!s.car.empty()) {
            int k = find_car(s.car);
            if (k < 0) k = find_car(base_car(s.car));
            if (k >= 0) car_ = k;
        }
        if (s.level >= 0) level_ = s.level;
        if (!s.course.empty()) arcade_course_ = s.course;
        if (s.mt >= 0) manual_ = s.mt != 0;
        if (s.page.empty() && !s.start) return;
        default_car();
        if (!s.car.empty()) own_car_ = true;
        if (s.paint >= 0) paint_ = s.paint;
        page_ = Page::Setup;
        setup_sel_ = int(setup_rows().size()) - 1;
        if (s.page == "cars") setup_action(app, Row::Car, 0, true);
        if (s.start) start_race(app);
    }

    void run_custom_script(App& app, const Script& s) {
        select_custom("");
        for (const CustomSummary& r : saved_)
            if (r.name == s.race) select_custom(r.section);
        open_custom(app);
        if (!s.course.empty()) custom_.course = s.course;
        if (s.laps > 0) custom_.laps = std::min(s.laps, kMaxLaps);
        if (s.place > 0) grid_.set_player_place(s.place - 1);
        if (int k = s.car.empty() ? -1 : grid_car(s.car); k >= 0) custom_.car = k, custom_.paint = 0;
        if (s.paint >= 0) custom_.paint = s.paint;
        if (s.mt >= 0) custom_.manual = s.mt != 0;
        if (s.catchup >= 0) custom_.catch_up = s.catchup != 0;
        if (!s.grid.empty()) {
            grid_.set_count(int(s.grid.size()));
            for (size_t k = 0; k < s.grid.size(); ++k) grid_.set_place(int(k), s.grid[k]);
        }
        custom_defaults(app);
        if (!s.save.empty()) save_race(app, s.save);
        if (s.start) start_custom(app);
    }

    Script script_;
    bool init_ = false, in_race_ = false;
    std::string error_, last_result_;
    data::CarDatabase db_;
    std::unique_ptr<game::GtMode> gt_; // the event rules (a fresh game: never saved)
    std::vector<QuickEvent> events_;
    std::vector<Category> categories_;
    std::vector<ListRow> rows_;
    std::vector<PickerCar> cars_; // every car of the disc and the mods, then the class pages' arcade cars
    int disc_cars_ = 0;
    std::vector<std::optional<game::CarFacts>> facts_; // by car, as needed (assign_classes)
    std::vector<std::string> arcade_courses_;
    Page page_ = Page::Events;
    int row_ = 0, list_first_ = 0, setup_sel_ = 0;
    int car_ = -1, paint_ = 0, level_ = 1;
    bool own_car_ = true, manual_ = false;
    std::string arcade_course_;
    // The custom race on the page (the last one or a saved one, in config.ini).
    struct CustomRace {
        std::string course;
        int laps = 3, car = -1, paint = 0;
        bool manual = false, catch_up = false;
    };
    bool has_custom_ = false; // (a disc without the arcade's races has none)
    CustomRace custom_;
    OpponentGrid grid_;
    std::vector<PickerCourse> courses_;
    std::map<std::string, int> grid_limits_; // by course (course_grid_limit)
    int custom_sel_ = 0;
    int picking_ = kSetupCar;                // what the car page is choosing
    CustomSummary last_custom_;
    std::vector<CustomSummary> saved_;
    std::string saved_section_, saved_name_; // the saved race on the page ("" = none)
    bool delete_armed_ = false;
    bool naming_ = false;                    // typing a saved race's name
    std::string name_text_;
    TextKeyboard name_keyboard_;             // in VR
    PointerInput name_pointer_;
    PointerInput pointer_; // the list, the setup and the custom race (the pickers have their own)
    HitList hits_;
    bool list_drag_ = false;
    int drag_first_ = 0;   // the list's first row when a drag began
    PickerArt art_;
    CarPicker car_picker_;
    CoursePicker course_picker_;
    CarTurntable turntable_;
};

} // namespace

GT3R_REGISTER_PLAYER_SCREEN(quick_race, "quickrace", "Quick race",
                            "Your own custom race, or any event of the game with any car: nothing to unlock.",
                            1, [] { return std::make_unique<QuickRaceScreen>(); });

} // namespace gt3r
