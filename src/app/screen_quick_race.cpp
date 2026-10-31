// Quick race: any event of the game with any car, nothing to unlock. The GT Mode
// leagues' races (Beginner, Amateur, Professional, Rally, Endurance), the licence and
// machine tests, the arcade single race classes and the arcade time trials, each
// against its own opponents (the event's entries), with any disc or mod car.
//
// Two pages: the event list (by category, with the course, laps, opponents and the
// event's rules shown as information only - they are not enforced), then the setup
// (car, colour, gearbox; arcade: difficulty and course) and Start. The race is the race
// screen as the menus start it (make_menu_race_screen), with GameRequest::practice set:
// races run as the arcade single race does, tests as GT mode runs them, and nothing is
// written to the GT Mode save. Leaving the race comes back here.
//
// Event names are the game's own: the titles the GT Mode menus show for their items
// (game_menu_titles(): "Sunday Cup", "Practical Cornering I"), else made from the course.
// The last choices are kept in config.ini [quickrace].
//
// Scripted runs (tests, screenshots):
//   GT3R_QUICKRACE="event=Gsunday_e_0001,car=r34_gtr_v_99,paint=1,mt=1,start"
// selects the event (its id or race code) and car; `start` starts the race at once,
// `page=setup` / `page=cars` opens that page; `level=0..3` and `course=<label>` set the
// arcade difficulty and course.

#include "app/app.h"
#include "app/gt_mode_titles.h"
#include "app/online_pickers.h"
#include "app/screen_race.h"
#include "app/screen_registry.h"
#include "app/ui.h"
#include "base/log.h"
#include "game/data/car_database.h"
#include "game/gt_mode.h"
#include "game/language_rules.h"
#include "menu/flow/game_hooks.h"
#include "menu/flow/menu_set.h"
#include "race/field.h"
#include "race/results.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
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

constexpr const char* kLevels[] = {"Easy", "Normal", "Hard", "Professional"};

struct Script {
    bool have = false, start = false;
    std::string event, car, page, course;
    int paint = -1, mt = -1, level = -1;
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
        else if (k == "start") s.start = true;
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

std::string ordinal(int n) {
    const char* suf = (n % 100 >= 11 && n % 100 <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    return std::to_string(n) + suf;
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
        if (page_ == Page::Cars) update_cars(app);
        else if (page_ == Page::Setup) update_setup(app);
        else update_events(app);
    }

    void on_event(App& app, const SDL_Event& e) override {
        if (page_ == Page::Cars) car_picker_.event(e); // the car page's pointer (online_pickers.h)
        float x = 0, y = 0;
        if (page_ == Page::Cars || !pointer_down(e, x, y)) return;
        // Taps: a row is selected; the selected row is chosen (as Cross).
        if (page_ == Page::Events) {
            if (x >= kListW || y < kListY) return;
            int r = list_first_ + int((y - kListY + 2) / kRowH);
            if (r < 0 || r >= int(rows_.size()) || r >= list_first_ + kListRows || rows_[size_t(r)].event < 0) return;
            if (r == row_) choose_event(app);
            else row_ = r, picker_sound(app, "cursor");
        } else {
            std::vector<Row> rows = setup_rows();
            int r = int((y - kSetupY + 3) / kSetupH);
            if (x >= kSetupR || r < 0 || r >= int(rows.size())) return;
            if (r == setup_sel_) setup_action(app, rows[size_t(r)], 1, true);
            else setup_sel_ = r, picker_sound(app, "cursor");
        }
    }

    void render(App& app, Ui& ui) override {
        // A screen pushed from another's update renders before its own first update.
        if (!init_) init(app);
        ui.clear();
        if (!error_.empty()) {
            ui.header("QUICK RACE", "");
            ui.text_center(150, error_, colors::kBad);
            ui.footer("Triangle: back");
            return;
        }
        if (page_ == Page::Cars) {
            car_picker_.render(app, ui);
            return;
        }
        if (page_ == Page::Setup) render_setup(app, ui);
        else render_events(app, ui);
    }

private:
    enum class Page { Events, Setup, Cars };
    enum class Row { Car, Colour, Gearbox, Level, Course, Start };

    static constexpr float kListY = 28, kRowH = 12, kListW = 318;
    static constexpr int kListRows = 25;
    static constexpr float kSetupX = 12, kSetupR = 318, kSetupY = 236, kSetupH = 15; // the setup rows (under the car)

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
        try {
            build_events(app, *data);
        } catch (const std::exception& e) { // (a disc whose events or menus cannot be read)
            LOG_WARN("Quick race: events: %s", e.what());
            categories_.clear(), events_.clear();
        }
        for (const Category& c : categories_) {
            rows_.push_back({&c - categories_.data(), -1});
            for (int e : c.events) rows_.push_back({&c - categories_.data(), e});
        }
        if (events_.empty()) {
            error_ = "No events found on the disc";
            return;
        }
        if (cars_.empty()) {
            error_ = "No cars found on the disc";
            return;
        }
        // The last choices.
        const IniDocument& cfg = app.config();
        car_ = find_car(cfg.get("quickrace", "car").value_or(""));
        paint_ = std::max(0, std::atoi(cfg.get("quickrace", "paint").value_or("0").c_str()));
        manual_ = cfg.get("quickrace", "gearbox").value_or("at") == "mt";
        level_ = std::clamp(std::atoi(cfg.get("quickrace", "level").value_or("1").c_str()), 0, 3);
        arcade_course_ = cfg.get("quickrace", "course").value_or("");
        select_event(cfg.get("quickrace", "event").value_or(""));
    }

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
            if (cars_[i].label == label) return int(i);
        return -1;
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
        for (const std::string& l : arcade_courses_) {
            bool d = false;
            for (const data::Course& c : db_.courses())
                if (c.label == l) d = c.surface == 2;
            if (d == dirt) out.push_back(l);
        }
        return out.empty() ? arcade_courses_ : out;
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
            if (rows_[r].event >= 0 && row_ < 0) row_ = int(r); // the first event by default
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
        int event; // -1 = the category's heading
    };

    void step_row(int d) {
        int r = row_;
        do r += d;
        while (r >= 0 && r < int(rows_.size()) && rows_[size_t(r)].event < 0);
        if (r >= 0 && r < int(rows_.size())) row_ = r;
    }

    void update_events(App& app) {
        const MenuInput& m = app.menu();
        if (m.pressed(PadButton::Triangle)) {
            app.save_config();
            app.pop();
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
                if (rows_[r].category == cat && rows_[r].event >= 0) {
                    row_ = int(r);
                    break;
                }
        }
        if (row_ != before) picker_sound(app, "cursor");
        if (m.pressed(PadButton::Cross)) choose_event(app);
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
        list_first_ = 0;
        if (row_ > kListRows / 2) list_first_ = std::min(row_ - kListRows / 2, std::max(0, int(rows_.size()) - kListRows));
        for (int i = 0; i < kListRows && list_first_ + i < int(rows_.size()); ++i) {
            const ListRow& r = rows_[size_t(list_first_ + i)];
            float y = kListY + float(i) * kRowH;
            if (r.event < 0) {
                ui.text(12, y, categories_[size_t(r.category)].title, colors::kAccent);
                continue;
            }
            bool sel = list_first_ + i == row_;
            if (sel) ui.fill_round(8, y - 2, kListW - 8, kRowH, 3, colors::kHighlight);
            ui.text(24, y, Ui::clip(events_[size_t(r.event)].name, kListW - 30), sel ? colors::kAccent : colors::kText);
        }
        ui.scrollbar(UiRect{8, kListY - 2, kListW - 6, kListRows * kRowH}, float(rows_.size()) * kRowH, float(list_first_) * kRowH);
        render_info(app, ui, true);
        ui.footer("Up/Down: event   L1/R1 or Left/Right: category   Cross: choose   Triangle: back");
    }

    // The event at the right: the course's still, the name, course, laps, field, rules and
    // opponents (the setup page shows it without the opponents, its rows below).
    void render_info(App& app, Ui& ui, bool full) {
        const float x = 330, w = 302;
        const QuickEvent& e = event();
        std::string code = event_code(), course = event_course();
        const data::Event* ev = db_.event(code);
        const SDL_FRect still{x, 28, w, 128};
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
            picker_sound(app, "cancel");
            page_ = Page::Events;
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
                car_picker_.open(app, art_, db_, cars_, car_, own_car_ ? paint_ : 0, [this](int i, int) { return car_note(i); });
                page_ = Page::Cars;
                picker_sound(app, "ok");
                return;
            }
            if (!e.test_car.empty() && ((own_car_ && car_ == 0 && h < 0) || (!own_car_ && h > 0))) {
                own_car_ = !own_car_; // the test's car sits before the first car
            } else if (own_car_) {
                int n = int(cars_.size());
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
            float y = kSetupY + float(i) * kSetupH;
            bool f = int(i) == setup_sel_;
            if (f) ui.fill_round(kSetupX - 4, y - 3, kSetupR - kSetupX + 4, 14, 4, colors::kHighlight);
            std::string label, value;
            float indent = 0;
            switch (rows[i]) {
            case Row::Car:
                label = "Car";
                if (own_car_) {
                    if (art_.car_icon(app, c.label, SDL_FRect{kSetupX + 84, y - 3, 26, 14})) indent = 30;
                    value = "< " + c.name + " >";
                } else {
                    value = "< The test's car: " + car_name(e.test_car) + " >";
                }
                break;
            case Row::Colour: {
                label = "Colour";
                int cur = std::clamp(paint_, 0, std::max(0, int(pal.size()) - 1));
                if (!pal.empty()) draw_paint_chip(ui, kSetupX + 84, y - 1, 18, 9, pal[size_t(cur)].rgb, false), indent = 24;
                std::string pname = pal.empty() ? "" : pal[size_t(cur)].name;
                value = "< " + (pname.empty() ? "" : pname + "  ") + std::to_string(cur + 1) + " of " +
                        std::to_string(std::max<size_t>(1, pal.size())) + " >";
                break;
            }
            case Row::Gearbox: label = "Transmission"; value = manual_ ? "< Manual >" : "< Automatic >"; break;
            case Row::Level: label = "Difficulty"; value = std::string("< ") + kLevels[level_] + " >"; break;
            case Row::Course: label = "Course"; value = "< " + course_name(event_course()) + " >"; break;
            case Row::Start: label = "Start race"; break;
            }
            Color lc = f ? colors::kAccent : rows[i] == Row::Start ? colors::kGood : colors::kText;
            ui.text(kSetupX + 4, y, label, lc);
            ui.text(kSetupX + 84 + indent, y, Ui::clip(value, kSetupR - kSetupX - 88 - indent), colors::kText);
        }
        if (!last_result_.empty()) ui.text(330, Ui::kHeight - 30, Ui::clip(last_result_, 302), colors::kGood);
        ui.footer("Up/Down: choose   Left/Right: change   Cross: car page / start   Triangle: events");
    }

    void update_cars(App& app) {
        CarPicker::Result r = car_picker_.update(app);
        if (r == CarPicker::Result::None) return;
        page_ = Page::Setup;
        if (r != CarPicker::Result::Chosen) return;
        car_ = car_picker_.car();
        paint_ = car_picker_.paint();
        own_car_ = true;
        remember(app);
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
        if (own_car_) rq.car = cars_[size_t(car_)].label;
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
            last_result_ = r->finished ? "Last race: " + ordinal(r->position) + " of " + std::to_string(r->cars) +
                                             "   best lap " + lap_time(r->best_lap)
                                       : "Last race: retired";
        } else if (r->best_lap >= 0) {
            last_result_ = "Last run: best lap " + lap_time(r->best_lap);
        }
    }

    void remember(App& app) {
        IniDocument& cfg = app.config();
        cfg.set("quickrace", "event", event().id);
        if (own_car_ && car_ >= 0) {
            cfg.set("quickrace", "car", cars_[size_t(car_)].label);
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

    Script script_;
    bool init_ = false, in_race_ = false;
    std::string error_, last_result_;
    data::CarDatabase db_;
    std::unique_ptr<game::GtMode> gt_; // the event rules (a fresh game: never saved)
    std::vector<QuickEvent> events_;
    std::vector<Category> categories_;
    std::vector<ListRow> rows_;
    std::vector<PickerCar> cars_;
    std::vector<std::string> arcade_courses_;
    Page page_ = Page::Events;
    int row_ = 0, list_first_ = 0, setup_sel_ = 0;
    int car_ = -1, paint_ = 0, level_ = 1;
    bool own_car_ = true, manual_ = false;
    std::string arcade_course_;
    PickerArt art_;
    CarPicker car_picker_;
    CarTurntable turntable_;
};

} // namespace

GT3R_REGISTER_PLAYER_SCREEN(quick_race, "quickrace", "Quick race",
                            "Race any event of the game with any car: GT Mode races, licence tests, arcade - nothing to unlock.",
                            7, [] { return std::make_unique<QuickRaceScreen>(); });

} // namespace gt3r
