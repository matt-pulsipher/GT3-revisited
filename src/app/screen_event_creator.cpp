// Event creator: the player's own GT Mode events (public issue 275), made in the game and played
// in GT Mode (game/data/mod_events.h, docs/MODDING.md "Adding events").
//
// The events are a mod of the player's, mods/my_events/: one events/<id>.ini each, and its
// picture in pictures/<id>.png. The mod loads over whatever disc is open, like any other, so the
// choices the creator offers come off that disc: its courses, its cars (mod cars too) and the
// leagues of its GT Mode race map. An event saved for a league another disc lacks goes to the
// Custom Events league there.
//
// Pages: the list of the player's events; an event (title, description, picture, league, single
// races or a championship, entry rules, prizes, its opponents and its races); a race (course,
// laps, the event's opponents or a lineup of its own); the opponents of the event (the lineup
// every race without its own runs, and every championship round); the prizes (per place, the
// championship bonus, a prize car). Lineups are Quick race's (app/opponent_grid.h): one to five
// cars, each with a colour and a skill, and the player's place on the grid.
//
// Export .zip writes the event as a mod of its own (<id>/mod.ini, events/, pictures/): the Mods
// screen's Install from .zip takes it on another player's game.
//
// Pointer input is Quick race's (the online lobby's): "<" ">" step a value, a row chooses on a
// second tap (hover focuses it first), the footer's actions are buttons.

#include "app/app.h"
#include "app/gt_mode_titles.h"
#include "app/online_pickers.h"
#include "app/save_dialogs.h"
#include "app/screen_registry.h"
#include "app/text_keyboard.h"
#include "app/ui.h"
#include "base/config.h"
#include "base/file.h"
#include "base/log.h"
#include "formats/course/course_file.h"
#include "formats/course/road.h"
#include "formats/png_reader.h"
#include "formats/png_writer.h"
#include "game/data/car_database.h"
#include "game/data/mod_events.h"
#include "game/gt_mode.h"
#include "game/gt_mode_mods.h"
#include "game/net_car.h"
#include "game/gt_parts.h"
#include "race/pool_draw.h"
#include "sim/car_spec.h"
#include "sim/exact/race_inputs.h"
#include "menu/flow/menu_set.h"
#include "menu/flow/save_page.h"
#include "platform/user_files.h"
#include "vfs/mod_archive.h"
#include "vfs/vfs.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace gt3r {

namespace {

constexpr const char* kModId = "my_events";
constexpr int kPictureW = 640, kPictureH = 448; // the menus' full-screen pictures
constexpr u32 kPrizeStep = 500, kPrizeBigStep = 5000;
constexpr int kMaxLaps = 99;

// Database text may hold non-ASCII bytes; the UI font is ASCII.
std::string ascii(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (c < 0x80) out += char(c);
        else if ((c & 0xC0) == 0xC0) out += '-';
    return out;
}

std::string amount(u32 cr) {
    std::string s = std::to_string(cr);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return s;
}
std::string money(u32 cr) { return amount(cr) + " Cr."; }

// A file name made from a title: lower-case letters, digits and '_'.
std::string id_from(const std::string& title) {
    std::string id;
    for (char c : title) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) id += char(std::tolower(u));
        else if (!id.empty() && id.back() != '_') id += '_';
    }
    while (!id.empty() && id.back() == '_') id.pop_back();
    if (id.size() > 32) id.resize(32);
    return id.empty() ? "event" : id;
}

// `src` scaled to cover w x h (cropped to its aspect, centred), box-filtered.
std::vector<u32> cover(const PngImage& src, int w, int h) {
    std::vector<u32> out(size_t(w) * size_t(h));
    const double sa = double(src.width) / double(src.height), da = double(w) / double(h);
    double cw = src.width, ch = src.height;
    if (sa > da) cw = ch * da;
    else ch = cw / da;
    const double x0 = (src.width - cw) * 0.5, y0 = (src.height - ch) * 0.5, sx = cw / w, sy = ch / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int ax = int(x0 + x * sx), bx = std::max(ax + 1, int(x0 + (x + 1) * sx));
            const int ay = int(y0 + y * sy), by = std::max(ay + 1, int(y0 + (y + 1) * sy));
            u32 sum[4] = {0, 0, 0, 0}, n = 0;
            for (int yy = ay; yy < by && yy < int(src.height); ++yy)
                for (int xx = ax; xx < bx && xx < int(src.width); ++xx) {
                    const u32 p = src.rgba[size_t(yy) * src.width + size_t(xx)];
                    for (int c = 0; c < 4; ++c) sum[c] += (p >> (8 * c)) & 0xFF;
                    ++n;
                }
            u32 p = 0;
            for (int c = 0; c < 4; ++c) p |= (n ? sum[c] / n : 0) << (8 * c);
            out[size_t(y) * size_t(w) + size_t(x)] = p;
        }
    return out;
}

} // namespace

class EventCreatorScreen : public Screen {
public:
    ~EventCreatorScreen() override {
        if (picture_tex_) SDL_DestroyTexture(picture_tex_);
    }

    void update(App& app, double) override {
        update_page(app);
        // Every change is saved as it is made (the event's file; GT Mode reads it at its next visit).
        if (dirty_ && init_ && error_.empty() && (page_ != Page::List || !id_.empty())) persist(app);
    }

    void update_page(App& app) {
        if (!init_) init(app);
        poll_dialog(app);
        if (naming_) {
            if (name_keyboard_.is_open()) {
                const TextKeyboard::Result r = name_keyboard_.update(app.menu(), name_pointer_.take(), name_text_);
                if (text_ == Text::Number) digits_only(name_text_);
                if (r != TextKeyboard::Result::None) end_naming(app, r == TextKeyboard::Result::Done);
            }
            return;
        }
        if (waiting_) return; // (a file dialog is open)
        if (page_ == Page::Cars) {
            update_cars(app);
            return;
        }
        if (page_ == Page::Courses) {
            update_courses(app);
            return;
        }
        pointer_actions(app);
        if (naming_ || page_ == Page::Cars || page_ == Page::Courses) return;
        const MenuInput& m = app.menu();
        if (m.pressed(PadButton::Triangle)) {
            back(app);
            return;
        }
        const std::vector<Row> rows = build_rows(app);
        const int n = int(rows.size());
        if (n == 0) return;
        sel_ = std::clamp(sel_, 0, n - 1);
        if (int v = m.vertical()) {
            sel_ = (sel_ + v + n) % n;
            armed_ = false;
            picker_sound(app, "cursor");
        }
        int h = m.horizontal();
        if (m.pressed(PadButton::L1)) h = -10;
        if (m.pressed(PadButton::R1)) h = 10;
        if (h || m.pressed(PadButton::Cross)) {
            via_pad_ = true;
            act(app, rows[size_t(sel_)], h, m.pressed(PadButton::Cross));
        }
    }

    TouchLayout touch_layout() const override {
        return page_ == Page::Cars || page_ == Page::Courses ? TouchLayout::Menu : TouchLayout::None;
    }

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
        if (page_ == Page::Cars) car_picker_.event(e);
        else if (page_ == Page::Courses) course_picker_.event(e);
        else pointer_.event(e);
    }

    void render(App& app, Ui& ui) override {
        if (!init_) init(app);
        ui.clear();
        hits_.clear();
        if (!error_.empty()) {
            ui.header("EVENT CREATOR", "");
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
        const std::vector<Row> rows = build_rows(app);
        sel_ = std::clamp(sel_, 0, std::max(0, int(rows.size()) - 1));
        if (page_ == Page::List || page_ == Page::Event) top_bar(ui); // (as Mods and Settings: "< Back" at the top left)
        else ui.header("EVENT CREATOR", page_title());
        render_side(app, ui, rows);
        for (size_t i = 0; i < rows.size(); ++i) render_row(app, ui, rows[i], int(i), int(i) == sel_);
        std::string hint = rows.empty() ? std::string() : rows[size_t(sel_)].hint;
        if (!note_.empty()) hint = note_;
        if (naming_ && !name_keyboard_.is_open()) hint = "Type, Enter to keep, Esc to cancel";
        footer_actions(ui, hint, {{"Cross: choose", kFootChoose}, {"Triangle: back", kFootBack}}, hits_, kHitFooter, pointer_);
        if (naming_) render_naming(ui);
    }

private:
    enum class Page { List, Event, Race, Pool, Entry, Prizes, Rules, Allowed, Cars, Courses };
    enum Hit { kHitRow, kHitLeft, kHitRight, kHitButton, kHitFooter };
    enum Foot { kFootChoose, kFootBack };
    enum class Pick { None, PrizeCar, PoolAdd, EntryCar, AllowedCar };
    enum class Text { None, Title, Description, Number };

    // A row of a page: a value with arrows, an action, a button, or a row of a lineup.
    struct Row {
        enum class Kind { Value, Action, Button, Field } kind = Kind::Value;
        std::string field;  // Field: what it edits ("title", "prize0" ...), the text typed in place while editing
        std::string suffix; // Field: after its box (" Cr.")
        std::string label, value, hint;
        UiRect r;
        std::function<void(int)> change; // left / right (and L1 / R1: +-10)
        std::function<void()> choose;    // Cross
        bool primary = false, dim = false;
    };

    static constexpr float kLeftX = 8, kLeftW = 310, kRowH = 18, kTopY = 26;
    static constexpr float kRightX = 326, kRightW = 306;
    static constexpr float kButtonY = 318, kButtonH = 22;

    // --- data -----------------------------------------------------------------------------------

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
        data_ = std::make_shared<game::GtData>();
        if (!data_->load(menu::flow::vfs_reader(*app.vfs()))) LOG_WARN("Event creator: GT Mode data: %s", data_->error().c_str());
        for (const data::Car& c : db_.cars()) {
            const std::string model = db_.model_of(c);
            if (model.empty() || !app.vfs()->exists("cars/day/" + model)) continue; // as the race screen lists them
            PickerCar e{c.label, ascii(c.name.empty() ? c.label : c.name), model, -1, db_.mod_car(c.label) != nullptr, c.maker, 0, {}, {}, 0, {}};
            if (e.mod) e.name += " (mod)";
            cars_.push_back(e);
        }
        std::sort(cars_.begin(), cars_.end(), [](const PickerCar& a, const PickerCar& b) { return a.name < b.name; });
        for (const data::Course& c : db_.courses())
            if (c.label.rfind("L_", 0) != 0 && !c.name.empty() && c.name != "-" && !course_file_path(*app.vfs(), c.label).empty())
                courses_.push_back({c.label, ascii(c.name)});
        if (cars_.empty() || courses_.empty()) {
            error_ = "This disc has no cars or courses to race";
            return;
        }
        // The disc's leagues, as its race map lists them, then Custom Events.
        try {
            if (std::optional<Bytes> mb = app.vfs()->read("menu/gt_mode.mbl"))
                leagues_ = game::league_pages(menu::MblFile::parse(*mb));
        } catch (const std::exception& e) {
            LOG_WARN("Event creator: menus: %s", e.what());
        }
        leagues_.push_back(game::kCustomLeaguePage);
        // What the disc's own events pay at most: the limit for an event's prizes.
        for (const data::Event& e : db_.events()) {
            if (e.bytes.size() < 0x1F8 || e.label.empty() || (e.label[0] != 'G' && e.label[0] != 'E' && e.label[0] != 'D')) continue;
            for (size_t p = 0; p < 6; ++p) max_prize_ = std::max(max_prize_, u32(rd16(e.bytes, 0x1D4 + 2 * p)) * 100);
            max_bonus_ = std::max(max_bonus_, u32(rd16(e.bytes, 0x1E2)) * 100);
        }
        if (max_prize_ == 0) max_prize_ = 100000;
        if (max_bonus_ == 0) max_bonus_ = 100000;
        index_versions(app);
        read_events(app);
    }

    std::string mod_dir(App& app) const { return app.mods_dir() + "/" + kModId; }

    // The player's events (mods/my_events/events/*.ini), by title.
    void read_events(App& app) {
        events_.clear();
        const std::string dir = mod_dir(app) + "/events";
        for (const std::string& name : list_host_dir(dir)) {
            if (name.size() < 5 || to_lower(name.substr(name.size() - 4)) != ".ini") continue;
            const std::string id = name.substr(0, name.size() - 4);
            std::optional<Bytes> b = read_host_file(dir + "/" + name);
            if (!b) continue;
            std::string err;
            auto def = data::parse_mod_event(std::string_view(reinterpret_cast<const char*>(b->data()), b->size()), id, &err);
            if (!def) {
                LOG_WARN("Event creator: %s: %s", name.c_str(), err.c_str());
                continue;
            }
            events_.push_back(std::move(*def));
        }
        std::sort(events_.begin(), events_.end(), [](const data::ModEventDef& a, const data::ModEventDef& b) { return a.title < b.title; });
    }

    // How many opponents a course's start grid holds (the player takes a place).
    int grid_limit(App& app, const std::string& course) {
        auto [it, fresh] = grid_limits_.try_emplace(course, data::ModEventDef::kMaxOpponents);
        if (fresh)
            if (std::optional<Bytes> f = app.vfs()->read(course_file_path(*app.vfs(), course))) try {
                const course::CourseFile cf = course::CourseFile::parse(*f);
                const int slots = int(course::Road::parse(cf.section(*f, course::kSlotRoad)).grid.size());
                it->second = std::clamp(slots - 1, 1, data::ModEventDef::kMaxOpponents);
            } catch (const std::exception& e) {
                LOG_WARN("Event creator: %s's start grid: %s", course.c_str(), e.what());
            }
        return it->second;
    }

    std::string course_name(const std::string& label) const {
        for (const PickerCourse& c : courses_)
            if (c.label == label) return c.name;
        return label;
    }
    std::string car_name(const std::string& label) const {
        for (const PickerCar& c : cars_)
            if (c.label == label) return c.name;
        return label;
    }
    int car_index(const std::string& label) const {
        for (size_t i = 0; i < cars_.size(); ++i)
            if (cars_[i].label == label) return int(i);
        return -1;
    }

    // --- pool entries: a car stock, a version of it from the disc (a variant its races field), or a tune of the event's ---

    struct Version {
        std::string label; // the entry's label: the car's, a variant's, or "" for a tune
        std::string name;  // "Stock", "As in Sunday Cup", "Your tune"
    };

    // The disc's variants of each car, named after a race that fields them (the menus' titles).
    void index_versions(App& app) {
        std::map<std::string, std::string> titles = game_menu_titles(*app.vfs(), app.language());
        add_mod_event_titles(titles, data_->catalog, db_);
        std::map<std::string, std::string> where; // variant -> a series title
        for (const game::RaceSeries& sr : data_->catalog.series) {
            auto t = titles.find(sr.page);
            if (t == titles.end() || sr.races.empty() || sr.races.front()[0] == data::kModEventPrefix) continue;
            for (const std::string& code : sr.races)
                if (const data::Event* e = db_.event(code))
                    for (const std::string& l : e->entries) where.try_emplace(l, t->second);
        }
        for (const data::CarVariant& v : db_.variants()) {
            if (v.label.empty() || v.label[0] == data::kModEventPrefix || car_index(v.car) < 0) continue;
            auto w = where.find(v.label);
            if (w == where.end()) continue; // (the ones GT Mode's races field: arcade, licence and demo cars are left out)
            versions_[v.car].push_back({v.label, "As in " + w->second});
        }
    }

    data::ModEventTune* tune(const std::string& name) {
        for (data::ModEventTune& t : ed_.tunes)
            if (t.name == name) return &t;
        return nullptr;
    }
    const data::ModEventTune* tune(const std::string& name) const { return const_cast<EventCreatorScreen*>(this)->tune(name); }
    static bool is_tune(const data::ModEventCar& c) { return c.label.rfind("tune:", 0) == 0; }

    // The car an entry is (its variant's or tune's car).
    std::string entry_car(const data::ModEventCar& c) const {
        if (is_tune(c)) {
            const data::ModEventTune* t = tune(c.label.substr(5));
            return t ? t->car : std::string();
        }
        if (const data::CarVariant* v = db_.variant(c.label)) return v->car;
        return c.label;
    }
    // Stock, the disc's versions, a tune.
    std::vector<Version> versions(const std::string& car) const {
        std::vector<Version> out = {{car, "Stock"}};
        if (auto it = versions_.find(car); it != versions_.end()) out.insert(out.end(), it->second.begin(), it->second.end());
        out.push_back({"", "Your tune"});
        return out;
    }
    std::string version_name(const data::ModEventCar& c) const {
        if (is_tune(c)) return "Your tune";
        for (const Version& v : versions(entry_car(c)))
            if (v.label == c.label) return v.name;
        return "Stock";
    }
    std::string entry_name(const data::ModEventCar& c) const {
        const std::string v = version_name(c);
        return car_name(entry_car(c)) + (v == "Stock" ? "" : ", " + (is_tune(c) ? std::string("tuned") : v.substr(6)));
    }
    // An entry's parts (its variant's, its tune's, its car's own).
    std::array<u64, 27> entry_parts(const data::ModEventCar& c) const {
        if (is_tune(c))
            if (const data::ModEventTune* t = tune(c.label.substr(5)))
                if (auto p = data::mod_event_tune_parts(db_, *t)) return *p;
        if (const data::CarVariant* v = db_.variant(c.label)) return v->parts;
        if (const data::Car* car = db_.car(entry_car(c))) return car->parts;
        return {};
    }
    // "123 PS, 1,040 kg": the entry's car as built.
    std::string entry_facts(const data::ModEventCar& c) {
        game::OnlineCar oc = game::online_car_from_db(db_, entry_car(c), 0);
        oc.kind = game::OnlineCar::Kind::Garage;
        oc.parts = entry_parts(c);
        if (const data::Car* car = db_.car(entry_car(c))) oc.settings = sim::stock_settings(db_, car->label, oc.parts);
        const game::CarFacts f = game::online_car_facts(db_, data_.get(), oc);
        if (!f.built) return {};
        const int kg = int(std::lround(f.kg_per_ps * f.power_ps + 120));
        return std::to_string(int(std::lround(f.power_ps))) + " PS, " + std::to_string(kg) + " kg";
    }

    // The tune shop's categories (its items by their name's first word) and each one's stages for a car.
    struct TuneCategory {
        std::string key, name;
        std::vector<const game::ShopItem*> items;
    };
    static const std::vector<TuneCategory>& tune_categories() {
        static const std::vector<TuneCategory> cats = [] {
            static const std::map<std::string, std::string> kNames = {
                {"bkt", "Brakes"},        {"bbl", "Brake balance"},  {"sus", "Suspension"},     {"muf", "Exhaust"},
                {"nat", "NA tuning"},     {"ptp", "Port polishing"}, {"egb", "Engine balancing"}, {"dsp", "Displacement"},
                {"cpu", "Racing chip"},   {"clt", "Clutch"},         {"fly", "Flywheel"},       {"prp", "Driveshaft"},
                {"tms", "Transmission"},  {"lsd", "Limited slip"},   {"ayc", "Yaw control"},    {"vcd", "Torque split"},
                {"wtr", "Weight reduction"}, {"tbk", "Turbo"},      {"icl", "Intercooler"},     {"tir", "Tires"},
                {"tcs", "Traction control"}, {"sms", "Stability control"}};
            std::vector<TuneCategory> out;
            for (const game::ShopItem& si : game::shop_items()) {
                const std::string item = si.item, key = item.substr(0, item.find('_'));
                auto it = std::find_if(out.begin(), out.end(), [&](const TuneCategory& c) { return c.key == key; });
                if (it == out.end()) {
                    auto n = kNames.find(key);
                    out.push_back({key, n != kNames.end() ? n->second : key, {}});
                    it = out.end() - 1;
                }
                it->items.push_back(&si);
            }
            return out;
        }();
        return cats;
    }
    // "nat_2" -> "Stage 2", "sus_semi_racing" -> "Semi-racing", "lsd_15" -> "1.5-way".
    static std::string stage_name(const std::string& item) {
        const std::string key = item.substr(0, item.find('_')), rest = item.substr(item.find('_') + 1);
        if (key == "lsd" && (rest == "1" || rest == "15" || rest == "2")) return rest == "15" ? "1.5-way" : rest + "-way";
        if (!rest.empty() && std::isdigit(static_cast<unsigned char>(rest[0]))) return "Stage " + rest;
        static const std::map<std::string, std::string> kNames = {{"on", "Fitted"}, {"semi_racing", "Semi-racing"},
                                                                  {"racing_big", "Racing"}, {"cr_mo", "Cr-Mo"},
                                                                  {"light_cr_mo", "Light Cr-Mo"}, {"super_close", "Super close"},
                                                                  {"variable", "Full custom"}, {"super_hard", "Super hard"},
                                                                  {"medium_hard", "Medium hard"}, {"medium_soft", "Medium soft"},
                                                                  {"super_soft", "Super soft"}, {"control", "Control"}};
        if (auto it = kNames.find(rest); it != kNames.end()) return it->second;
        std::string out = rest;
        for (char& c : out)
            if (c == '_') c = ' ';
        if (!out.empty()) out[0] = char(std::toupper(static_cast<unsigned char>(out[0])));
        return out;
    }

    // A pool entry's paint (a carcolor id, the record's) as an index into the car's colours, and back (from_grid).
    int paint_index(const std::string& car, u32 id) const {
        for (int i = 0, n = data_->color_count(car); i < n; ++i)
            if (data_->color_id(car, i) == id) return i;
        return 0;
    }
    // --- pages ----------------------------------------------------------------------------------

    std::string page_title() const {
        switch (page_) {
        case Page::List: return "Your GT Mode events";
        case Page::Event: return ed_.title.empty() ? "New event" : ed_.title;
        case Page::Race: return ed_.title + "  >  Race " + std::to_string(race_ + 1);
        case Page::Pool: return ed_.title + (pool_race_ < 0 ? "  >  Opponents" : "  >  Race " + std::to_string(pool_race_ + 1) + "  >  Its cars");
        case Page::Entry: {
            const std::vector<data::ModEventCar>& p = const_cast<EventCreatorScreen*>(this)->pool();
            return ed_.title + "  >  Opponents  >  " + (entry_ >= 0 && entry_ < int(p.size()) ? car_name(entry_car(p[size_t(entry_)])) : std::string());
        }
        case Page::Prizes: return ed_.title + "  >  Prizes";
        case Page::Rules: return ed_.title + "  >  Car rules";
        case Page::Allowed: return ed_.title + "  >  Car rules  >  Cars allowed";
        default: return "";
        }
    }

    std::vector<Row> build_rows(App& app) {
        switch (page_) {
        case Page::List: return list_rows(app);
        case Page::Event: return event_rows(app);
        case Page::Race: return race_rows(app);
        case Page::Pool: return pool_rows(app);
        case Page::Entry: return entry_rows(app);
        case Page::Prizes: return prize_rows(app);
        case Page::Rules: return rule_rows(app);
        case Page::Allowed: return allowed_rows(app);
        default: return {};
        }
    }

    Row value_row(std::string label, std::string value, std::string hint, std::function<void(int)> change, float x, float& y,
                  float w = kLeftW) {
        Row r;
        r.label = std::move(label), r.value = std::move(value), r.hint = std::move(hint), r.change = std::move(change);
        r.r = {x, y, w, kRowH};
        y += kRowH;
        return r;
    }
    Row action_row(std::string label, std::string value, std::string hint, std::function<void()> choose, float x, float& y,
                   float w = kLeftW) {
        Row r = value_row(std::move(label), std::move(value), std::move(hint), {}, x, y, w);
        r.kind = Row::Kind::Action;
        r.choose = std::move(choose);
        return r;
    }
    Row button(std::string label, UiRect at, std::function<void()> choose, bool primary = false, std::string hint = {}) {
        Row r;
        r.kind = Row::Kind::Button;
        r.label = std::move(label), r.hint = std::move(hint), r.r = at, r.choose = std::move(choose), r.primary = primary;
        return r;
    }
    // A text input field: Cross (or a click, a tap) types into it.
    Row field_row(std::string label, std::string key, std::string value, std::string suffix, std::string hint,
                  std::function<void()> start, float x, float& y, float w = kLeftW) {
        Row r = value_row(std::move(label), std::move(value), std::move(hint), {}, x, y, w);
        r.kind = Row::Kind::Field;
        r.field = std::move(key), r.suffix = std::move(suffix), r.choose = std::move(start);
        return r;
    }

    // The list: a new event, then the player's events.
    std::vector<Row> list_rows(App& app) {
        std::vector<Row> rows;
        float y = kTopY;
        rows.push_back(action_row("New event", "", "Make a new event for GT Mode", [this, &app] { new_event(app); }, kLeftX, y));
        rows.back().primary = true;
        for (size_t i = 0; i < events_.size(); ++i) {
            const data::ModEventDef& d = events_[i];
            const std::string races = std::to_string(d.races.size()) + (d.races.size() == 1 ? " race" : " races");
            rows.push_back(action_row(d.title, d.championship ? "Championship, " + races : races,
                                      "Cross: edit   In GT Mode: " + league_title(league_on_disc(d.league)),
                                      [this, &app, i] { edit_event(app, i); }, kLeftX, y));
        }
        rows.push_back(back_row(app));
        return rows;
    }

    // The league an event shows on with this disc.
    std::string league_on_disc(const std::string& page) const {
        return std::find(leagues_.begin(), leagues_.end(), page) != leagues_.end() ? page : std::string(game::kCustomLeaguePage);
    }

    std::vector<Row> event_rows(App& app) {
        std::vector<Row> rows;
        float y = kTopY;
        const float x = kLeftX;
        rows.push_back(field_row("Title", "title", ed_.title, "", "Its name (GT Mode shows it on its card)",
                                 [this, &app] { begin_naming(app, Text::Title); }, x, y));
        rows.push_back(field_row("Description", "description", ed_.description, "", "A line about it (shown on its card)",
                                 [this, &app] { begin_naming(app, Text::Description); }, x, y));
        rows.push_back(action_row("Picture", picture_state(), "Cross: choose a picture (PNG); it fills the screen behind the event",
                                  [this, &app] { open_dialog(app, menu::flow::FileDialogs::Kind::OpenPicture, ""); }, x, y));
        if (has_picture()) {
            rows.push_back(action_row("", "Remove picture", "Cross: no picture (GT Mode shows the title instead)", [this] {
                picture_.clear(), picture_removed_ = true, dirty_ = true;
                picture_tex_dirty_ = true;
            }, x, y));
        }
        const int li = int(std::find(leagues_.begin(), leagues_.end(), league_on_disc(ed_.league)) - leagues_.begin());
        rows.push_back(value_row("League", league_title(leagues_[size_t(li)]), "Where GT Mode lists it (this disc's leagues)",
                                 [this, li](int h) {
                                     const int n = int(leagues_.size());
                                     ed_.league = leagues_[size_t((li + (h < 0 ? -1 : 1) + n) % n)];
                                     if (ed_.league == game::kCustomLeaguePage) ed_.league.clear();
                                     dirty_ = true;
                                 }, x, y));
        rows.push_back(value_row("Format", ed_.championship ? "Championship" : "Single races",
                                 "Single races: entered one at a time. Championship: every race in turn, for points",
                                 [this](int) { ed_.championship = !ed_.championship, dirty_ = true; }, x, y));
        static const char* const kLicenses[] = {"None", "B", "A", "International B", "International A", "S", "Rally"};
        auto cycle = [this](int& v, int n) { return [this, &v, n](int h) { v = (v + (h < 0 ? -1 : 1) + n) % n, dirty_ = true; }; };
        rows.push_back(value_row("License", kLicenses[ed_.license], "The license a player needs to enter", cycle(ed_.license, 7), x, y));
        rows.push_back(action_row("Car rules", rules_summary(), "Cross: which cars may enter",
                                  [this] { open_sub(Page::Rules); }, x, y));
        rows.push_back(action_row("Prizes", money(ed_.prizes[0]) + " for 1st", "Cross: prize money and a prize car",
                                  [this] { open_sub(Page::Prizes); }, x, y));
        rows.push_back(action_row("Opponents", std::to_string(ed_.field.size()) + (ed_.field.size() == 1 ? " car" : " cars"),
                                  ed_.championship ? "Cross: the cars a championship's field is drawn from"
                                                   : "Cross: the cars every race without its own draws its opponents from",
                                  [this] { open_pool(-1); }, x, y));
        // The races, at the right under the picture.
        float ry = 146;
        for (size_t k = 0; k < ed_.races.size(); ++k) {
            const data::ModEventRace& r = ed_.races[k];
            const std::string laps = std::to_string(r.laps) + (r.laps == 1 ? " lap" : " laps");
            const bool own = !ed_.championship && !r.opponents.empty();
            (void)app;
            rows.push_back(action_row("Race " + std::to_string(k + 1), course_name(r.course) + ", " + laps + (own ? ", own cars" : ""),
                                      "Cross: its course, laps and opponents", [this, &app, k] { open_race(app, int(k)); }, kRightX, ry,
                                      kRightW));
        }
        if (int(ed_.races.size()) < data::ModEventDef::kMaxRaces)
            rows.push_back(action_row("", "Add a race", "Up to 10 races", [this, &app] { add_race(app); }, kRightX, ry, kRightW));
        rows.push_back(button("Export .zip", {kLeftX, kButtonY, 112, kButtonH}, [this, &app] { export_event(app); }, false,
                              "Saves it as a .zip others install from the Mods screen"));
        if (!id_.empty())
            rows.push_back(button(armed_ ? "Press again" : "Delete", {kLeftX + 118, kButtonY, 100, kButtonH},
                                  [this, &app] { delete_event(app); }, false, "Deletes the event (press twice)"));
        rows.push_back(back_row(app));
        return rows;
    }

    std::vector<Row> race_rows(App& app) {
        std::vector<Row> rows;
        data::ModEventRace& r = ed_.races[size_t(race_)];
        float y = 110;
        const float x = kLeftX, w = 232;
        rows.push_back(action_row("Course", course_name(r.course), "Cross: choose a course   Left/Right: the next one",
                                  [this] { open_courses(); }, x, y, w));
        rows.back().change = [this, &app, &r](int h) {
            auto it = std::find_if(courses_.begin(), courses_.end(), [&](const PickerCourse& c) { return c.label == r.course; });
            const int n = int(courses_.size()), i = it == courses_.end() ? 0 : int(it - courses_.begin());
            r.course = courses_[size_t((i + (h < 0 ? -1 : 1) + n) % n)].label;
            course_changed(app);
        };
        rows.push_back(value_row("Laps", std::to_string(r.laps), "Left/Right: laps (L1/R1: 10 at a time)", [this, &r](int h) {
            r.laps = (r.laps - 1 + h + kMaxLaps * 10) % kMaxLaps + 1, dirty_ = true;
        }, x, y, w));
        static const char* const kSkillNames[] = {"League's", "Easy", "Normal", "Hard", "Professional"};
        rows.push_back(value_row("Skill", kSkillNames[std::clamp(r.skill, -1, 3) + 1], skill_facts(r),
                                 [this, &r](int h) { r.skill = (r.skill + 1 + (h < 0 ? -1 : 1) + 5) % 5 - 1, dirty_ = true; }, x, y, w));
        const bool own = !ed_.championship && !r.opponents.empty();
        if (!ed_.championship)
            rows.push_back(value_row("Opponents", own ? "Its own cars" : "The event's", "The event's cars, or cars of its own to draw from",
                                     [this, &r](int) {
                                         if (r.opponents.empty()) r.opponents = ed_.field;
                                         else r.opponents.clear();
                                         dirty_ = true;
                                     }, x, y, w));
        if (own)
            rows.push_back(action_row("Its cars", std::to_string(r.opponents.size()) + (r.opponents.size() == 1 ? " car" : " cars"),
                                      "Cross: the cars this race draws its opponents from", [this] { open_pool(race_); }, x, y, w));
        const int most = std::min({data::ModEventDef::kMaxOpponents, int(race_pool(r).size()), grid_limit(app, r.course)});
        const int n = r.cars > 0 ? std::min(r.cars, most) : most;
        rows.push_back(value_row("Racing you", std::to_string(n) + (n == 1 ? " car" : " cars"),
                                 "How many cars it draws (" + course_name(r.course) + "'s grid has room for " +
                                     std::to_string(grid_limit(app, r.course)) + ")",
                                 [this, &r, n, most](int h) { r.cars = std::clamp(n + (h < 0 ? -1 : 1), 1, std::max(most, 1)), dirty_ = true; },
                                 x, y, w));
        rows.push_back(button(armed_ ? "Press again" : "Remove race", {kLeftX, kButtonY, 120, kButtonH}, [this, &app] { remove_race(app); },
                              false, "Takes the race out of the event (press twice)"));
        editing_buttons(rows);
        return rows;
    }

    // The pool a race draws from: its own cars, else the event's.
    const std::vector<data::ModEventCar>& race_pool(const data::ModEventRace& r) const {
        return !ed_.championship && !r.opponents.empty() ? r.opponents : ed_.field;
    }
    // The pool the Pool page edits: the event's (pool_race_ < 0) or a race's own.
    std::vector<data::ModEventCar>& pool() { return pool_race_ < 0 ? ed_.field : ed_.races[size_t(pool_race_)].opponents; }

    std::vector<Row> pool_rows(App& app) {
        std::vector<Row> rows;
        float y = kTopY;
        std::vector<data::ModEventCar>& p = pool();
        if (p.size() < size_t(data::ModEventDef::kMaxPool))
            rows.push_back(action_row("", "Add a car", "Cross: choose a car (then its version and color)",
                                      [this, &app] { open_cars(app, Pick::PoolAdd, 0, 0); }, kLeftX, y));
        editing_buttons(rows);
        // The cars, at the right: Cross opens one.
        const float colw = kRightW / 2;
        for (size_t i = 0; i < p.size(); ++i) {
            float ly = 0;
            Row r = action_row("", entry_name(p[i]), "Cross: its version, color and parts", [this, i] { open_entry(int(i)); },
                               kRightX + float(i % 2) * colw, ly, colw);
            r.r.y = kTopY + 16 + float(i / 2) * 14, r.r.h = 14;
            r.dim = true;
            rows.push_back(std::move(r));
        }
        return rows;
    }

    std::vector<Row> entry_rows(App& app) {
        std::vector<Row> rows;
        std::vector<data::ModEventCar>& p = pool();
        if (entry_ < 0 || entry_ >= int(p.size())) return rows;
        data::ModEventCar& c = p[size_t(entry_)];
        const std::string car = entry_car(c);
        float y = kTopY;
        rows.push_back(action_row("Car", car_name(car), "Cross: another car", [this, &app, car] {
            open_cars(app, Pick::EntryCar, car_index(car), 0);
        }, kLeftX, y));
        const std::vector<Version> vs = versions(car);
        int vi = int(vs.size()) - 1;
        if (!is_tune(c))
            for (size_t i = 0; i + 1 < vs.size(); ++i)
                if (vs[i].label == c.label) vi = int(i);
        rows.push_back(value_row("Version", vs[size_t(vi)].name,
                                 "Stock, as one of this disc's races fields it, or tuned with parts of your choice",
                                 [this, &c, vs, vi, car](int h) {
                                     const Version& v = vs[size_t((vi + (h < 0 ? -1 : 1) + int(vs.size())) % int(vs.size()))];
                                     c.label = v.label.empty() ? "tune:" + new_tune(car) : v.label;
                                     dirty_ = true;
                                 }, kLeftX, y));
        const int n = data_->color_count(car), pi = c.color == 0xFFFFFFFFu ? -1 : paint_index(car, c.color);
        rows.push_back(value_row("Color", pi < 0 ? "Drawn at the start" : Ui::clip(data_->color_name_latin(c.color), 150),
                                 "Its paint, or one drawn at the race's start", [this, &c, n, pi, car](int h) {
                                     const int k = (pi + 1 + (h < 0 ? -1 : 1) + n + 1) % (n + 1) - 1;
                                     c.color = k < 0 ? 0xFFFFFFFFu : data_->color_id(car, k);
                                     dirty_ = true;
                                 }, kLeftX, y));
        // A tune's parts: every category the shops have parts for this car in (the right column, then the left).
        if (is_tune(c))
            if (data::ModEventTune* t = tune(c.label.substr(5))) {
                float ry = kTopY, ly = y + 24;
                for (const TuneCategory& cat : tune_categories()) {
                    std::vector<const game::ShopItem*> offered;
                    for (const game::ShopItem* si : cat.items)
                        if (game::find_part(db_, t->car, si->goods)) offered.push_back(si);
                    if (offered.empty()) continue;
                    int cur = -1;
                    for (size_t i = 0; i < offered.size(); ++i)
                        if (std::find(t->parts.begin(), t->parts.end(), offered[i]->item) != t->parts.end()) cur = int(i);
                    const bool right = ry + kRowH <= kButtonY - 4;
                    float& yy = right ? ry : ly;
                    rows.push_back(value_row(cat.name, cur < 0 ? "Stock" : stage_name(offered[size_t(cur)]->item),
                                             "The tune shop's parts for this car", [this, t, offered, cur](int h) {
                                                 for (const game::ShopItem* si : offered) std::erase(t->parts, std::string(si->item));
                                                 const int m = int(offered.size());
                                                 const int k = (cur + 1 + (h < 0 ? -1 : 1) + m + 1) % (m + 1) - 1;
                                                 if (k >= 0) t->parts.push_back(offered[size_t(k)]->item);
                                                 dirty_ = true;
                                             }, right ? kRightX : kLeftX, yy, right ? kRightW : kLeftW));
                }
            }
        rows.push_back(button(armed_ ? "Press again" : "Remove", {kLeftX, kButtonY, 120, kButtonH}, [this, &app] {
            if (!armed_) {
                armed_ = true;
                picker_sound(app, "cursor");
                return;
            }
            armed_ = false;
            pool().erase(pool().begin() + entry_);
            dirty_ = true;
            leave_sub(true);
        }, false, "Takes it out of the pool (press twice)"));
        editing_buttons(rows);
        return rows;
    }

    // A new tune of `car` (its name: the car's and a number), stock until parts are chosen.
    std::string new_tune(const std::string& car) {
        std::string name;
        for (int n = 1; name.empty() || tune(name); ++n) name = car + "_" + std::to_string(n);
        ed_.tunes.push_back({name, car, {}});
        return name;
    }

    std::vector<Row> prize_rows(App& app) {
        std::vector<Row> rows;
        float y = kTopY;
        static const char* const kPlaces[] = {"1st", "2nd", "3rd", "4th", "5th", "6th"};
        static const char* const kPlaceNames[] = {"1st", "2nd", "3rd", "4th", "5th", "6th"};
        for (size_t p = 0; p < 6; ++p)
            rows.push_back(field_row(std::string(kPlaces[p]) + " place", "prize" + std::to_string(p), amount(ed_.prizes[p]), " Cr.",
                                     "Up to " + money(max_prize_) + ", what this disc's races pay", [this, &app, p] {
                                         begin_number(app, std::string(kPlaceNames[p]) + " place prize", "prize" + std::to_string(p),
                                                      &ed_.prizes[p], max_prize_);
                                     }, kLeftX, y));
        if (ed_.championship)
            rows.push_back(field_row("Champion's bonus", "bonus", amount(ed_.bonus), " Cr.", "Up to " + money(max_bonus_),
                                     [this, &app] { begin_number(app, "Champion's bonus", "bonus", &ed_.bonus, max_bonus_); }, kLeftX, y));
        y += 8;
        rows.push_back(action_row("Prize car", ed_.prize_car.empty() ? "None" : car_name(ed_.prize_car),
                                  ed_.championship ? "Given once, for winning the championship. Cross: choose   Left/Right: none"
                                                   : "Given once, for winning every race. Cross: choose   Left/Right: none",
                                  [this, &app] { open_cars(app, Pick::PrizeCar, car_index(ed_.prize_car), prize_paint_index()); }, kLeftX, y));
        rows.back().change = [this](int) { ed_.prize_car.clear(), ed_.prize_paint = -1, dirty_ = true; };
        editing_buttons(rows);
        return rows;
    }

    // What a race's skill sets in its opponents' driving, in the disc's numbers (the first driveline class's, ai §9.2).
    std::string skill_facts(const data::ModEventRace& r) const {
        bool dirt = false;
        for (const data::Course& c : db_.courses())
            if (c.label == r.course) dirt = c.surface == 2;
        const std::string code = r.skill < 0 ? base_for(ed_.league) : data::mod_event_skill_event(db_, r.skill, dirt);
        const data::Event* ev = db_.event(code);
        if (!ev || ev->bytes.size() < 0x1F4) return "How the opponents drive: the league's races', or the arcade's difficulties'";
        const sim::exact::DriverOverrides d = sim::exact::driver_overrides(sim::exact::race_config(ev->bytes));
        auto pct = [](u8 v) { return std::to_string(v == 0 || v > 100 ? 100 : int(v)) + "%"; };
        return "Every opponent: throttle " + pct(d[0]) + ", brake " + pct(d[4]) + ", corner speed " + pct(d[8]);
    }

    // --- car rules: GT3's own (the event record's type, drivetrain and engine, and its regulation: the cars that may
    // enter), the list made with the online lobby's filters (app/screen_online.cpp) ---

    static constexpr int kPowerSteps[] = {0, 100, 150, 200, 250, 300, 350, 400, 450, 500, 600, 700, 800};
    static constexpr int kPowerCount = int(sizeof(kPowerSteps) / sizeof(kPowerSteps[0]));

    // A race of the event on a dirt course: the class filter follows the arcade's class page there.
    bool dirt_event() const {
        for (const data::ModEventRace& r : ed_.races)
            for (const data::Course& c : db_.courses())
                if (c.label == r.course && c.surface == 2) return true;
        return false;
    }

    std::string rules_summary() const {
        static const char* const kTypes[] = {"", "race and rally cars", "tuned cars", "stock road cars"};
        static const char* const kDrives[] = {"", "FF", "FR", "4WD", "MR", "RR"};
        static const char* const kAspirations[] = {"", "NA", "turbo"};
        std::string out;
        auto add = [&](const std::string& t) { out += (out.empty() ? "" : ", ") + t; };
        if (ed_.car_class) add(kTypes[ed_.car_class]);
        if (ed_.drivetrain) add(kDrives[ed_.drivetrain]);
        if (ed_.aspiration) add(kAspirations[ed_.aspiration]);
        if (!ed_.allowed.empty()) add(std::to_string(ed_.allowed.size()) + (ed_.allowed.size() == 1 ? " car" : " cars"));
        return out.empty() ? "Any car" : out;
    }

    // What the filters look at for car `i` (as its stock car), built once.
    const game::CarFacts& facts(int i) {
        if (facts_.size() != cars_.size()) facts_.assign(cars_.size(), std::nullopt);
        std::optional<game::CarFacts>& f = facts_[size_t(i)];
        if (!f) f = game::online_car_facts(db_, data_.get(), game::online_car_from_db(db_, cars_[size_t(i)].label, 0));
        return *f;
    }
    // The lobby's rules for the event's own (type, drivetrain, engine) and the list page's filters.
    game::CarRules rules(bool filters) const {
        game::CarRules r;
        r.drive = ed_.drivetrain, r.aspiration = ed_.aspiration;
        if (ed_.car_class == 1) r.category = 2;
        if (ed_.car_class == 3) r.category = 1;
        r.dirt_course = dirt_event();
        if (filters) r.allow_mods = filter_mods_, r.car_class = filter_class_, r.max_ps = filter_ps_;
        return r;
    }
    // Car `i`, as the dealers sell it, under the event's rules (and the list page's filters): GT3's own checks of the type,
    // drivetrain and engine (the lobby's code for them), and its list.
    bool may_enter(int i, bool filters, bool list = true) {
        const game::OnlineCar oc = game::online_car_from_db(db_, cars_[size_t(i)].label, 0);
        if (!game::car_meets_rules(oc, facts(i), rules(filters))) return false;
        if (ed_.car_class == 2) return false; // (tuned cars only: no dealer's car is)
        return !list || ed_.allowed.empty() ||
               std::find(ed_.allowed.begin(), ed_.allowed.end(), cars_[size_t(i)].label) != ed_.allowed.end();
    }
    // How many cars a regulation (the list) holds on this disc.
    size_t list_room() const {
        const db::Table& t = db_.table(data::TableId::Regulation);
        return t.records.empty() ? 0 : (t.records.front().size() - 8) / 8;
    }

    std::vector<Row> rule_rows(App&) {
        std::vector<Row> rows;
        float y = kTopY;
        auto cycle = [this](int& v, int n) { return [this, &v, n](int h) { v = (v + (h < 0 ? -1 : 1) + n + 1) % (n + 1), dirty_ = true; }; };
        static const char* const kTypes[] = {"Any", "Race and rally cars", "Tuned cars", "Stock road cars"};
        static const char* const kDrives[] = {"Any", "FF", "FR", "4WD", "MR", "RR"};
        static const char* const kAspirations[] = {"Any", "NA", "Turbo"};
        rows.push_back(value_row("Car type", kTypes[ed_.car_class],
                                 "Race and rally cars, garage cars with any part or setting changed, or stock road cars",
                                 cycle(ed_.car_class, 3), kLeftX, y));
        rows.push_back(value_row("Drivetrain", kDrives[ed_.drivetrain], "The car's drivetrain", cycle(ed_.drivetrain, 5), kLeftX, y));
        rows.push_back(value_row("Engine", kAspirations[ed_.aspiration], "NA engines, or turbocharged ones", cycle(ed_.aspiration, 2),
                                 kLeftX, y));
        rows.push_back(action_row("Cars allowed",
                                  ed_.allowed.empty() ? "Any" : std::to_string(ed_.allowed.size()) + (ed_.allowed.size() == 1 ? " car" : " cars"),
                                  "Cross: the list of the cars that may enter (by class, power, or one by one)",
                                  [this] { open_sub(Page::Allowed); }, kLeftX, y));
        y += 6;
        rows.push_back(action_row("Any car", "", "Clears every rule above", [this] {
            ed_.car_class = ed_.drivetrain = ed_.aspiration = 0;
            ed_.allowed.clear();
            dirty_ = true;
        }, kLeftX, y));
        editing_buttons(rows);
        return rows;
    }

    // The list of the cars allowed (the event's regulation): made from the lobby's filters, or a car at a time.
    std::vector<Row> allowed_rows(App& app) {
        std::vector<Row> rows;
        float y = kTopY;
        rows.push_back(value_row("Class", filter_class_ ? game::arcade_class_name(filter_class_) : "Any",
                                 "The arcade's classes; a car in no class list goes by its weight per power",
                                 [this](int h) { filter_class_ = (filter_class_ + (h < 0 ? -1 : 1) + game::kArcadeClasses + 1) % (game::kArcadeClasses + 1); },
                                 kLeftX, y));
        rows.push_back(value_row("Power limit", filter_ps_ ? "At most " + std::to_string(filter_ps_) + " PS" : "No limit",
                                 "Peak power at the crank, as the dealers sell the car", [this](int h) {
                                     int k = 0;
                                     for (int j = 0; j < kPowerCount; ++j)
                                         if (kPowerSteps[j] <= filter_ps_) k = j;
                                     filter_ps_ = kPowerSteps[std::clamp(k + (h < 0 ? -1 : 1), 0, kPowerCount - 1)];
                                 }, kLeftX, y));
        rows.push_back(value_row("Mod cars", filter_mods_ ? "Included" : "Left out", "Cars added by mods",
                                 [this](int) { filter_mods_ = !filter_mods_; }, kLeftX, y));
        int passing = 0;
        for (int i = 0; i < int(cars_.size()); ++i) passing += may_enter(i, true, false);
        y += 4;
        rows.push_back(button("Generate list (" + std::to_string(passing) + (passing == 1 ? " car)" : " cars)"), {kLeftX, y, 220, kButtonH},
                              [this] {
                                  ed_.allowed.clear();
                                  for (int i = 0; i < int(cars_.size()); ++i)
                                      if (may_enter(i, true, false)) ed_.allowed.push_back(cars_[size_t(i)].label);
                                  trim_list();
                              },
                              true, "The list becomes the cars that pass the filters above and the event's rules"));
        y += kButtonH + 8;
        if (ed_.allowed.size() < list_room())
            rows.push_back(action_row("", "Add a car", "Cross: choose a car", [this, &app] { open_cars(app, Pick::AllowedCar, 0, 0); },
                                      kLeftX, y));
        if (!ed_.allowed.empty())
            rows.push_back(action_row("", "Clear the list (any car)", "", [this] { ed_.allowed.clear(), dirty_ = true; }, kLeftX, y));
        editing_buttons(rows);
        // The list itself, at the right: Cross takes a car off.
        float ly = kTopY + 16;
        const float colw = kRightW / 2;
        for (size_t i = 0; i < ed_.allowed.size() && ly + float(i / 2) * 14 < kButtonY - 14; ++i) {
            Row r = action_row("", car_name(ed_.allowed[i]), "Cross: take it off the list", [this, i] {
                ed_.allowed.erase(ed_.allowed.begin() + std::ptrdiff_t(i));
                dirty_ = true;
            }, kRightX + float(i % 2) * colw, ly, colw);
            r.r.y = kTopY + 16 + float(i / 2) * 14, r.r.h = 14;
            r.dim = true;
            rows.push_back(std::move(r));
        }
        return rows;
    }

    // A list longer than a regulation holds keeps its first cars.
    void trim_list() {
        dirty_ = true;
        if (ed_.allowed.size() <= list_room()) return;
        note(std::to_string(ed_.allowed.size()) + " cars pass; the list holds " + std::to_string(list_room()) +
             ": narrow the filters, or keep any car");
        ed_.allowed.resize(list_room());
    }

    void step_money(u32& v, int h, u32 cap) {
        const s64 step = std::abs(h) >= 10 ? kPrizeBigStep : kPrizeStep;
        v = u32(std::clamp<s64>(s64(v) + (h < 0 ? -step : step), 0, cap));
        dirty_ = true;
    }

    int prize_paint_index() const {
        if (ed_.prize_car.empty() || ed_.prize_paint < 0) return 0;
        return data_->color_index(ed_.prize_car, ed_.prize_paint);
    }

    // --- actions --------------------------------------------------------------------------------

    void go(Page p) {
        undo_.clear(); // (the list and an event's page: no editing page open)
        page_ = p;
        sel_ = 0;
        armed_ = false;
    }

    void new_event(App& app) {
        ed_ = data::ModEventDef{};
        ed_.title = "My event " + std::to_string(events_.size() + 1);
        ed_.league = leagues_.size() > 1 ? leagues_.front() : std::string();
        ed_.prizes = {5000, 3000, 2000, 1000, 500, 250};
        for (u32& p : ed_.prizes) p = std::min(p, max_prize_);
        ed_.bonus = std::min<u32>(10000, max_bonus_);
        ed_.races.push_back({courses_.front().label, 3, {}, -1});
        for (const PickerCourse& c : courses_)
            if (c.label == "trialmt") ed_.races.front().course = c.label;
        // Its opponents to start with: the pool of the league's first race (the disc's own cars for it).
        if (const data::Event* e = db_.event(base_for(ed_.league)))
            for (const std::string& l : e->entries)
                if (const data::CarVariant* v = db_.variant(l); v && car_index(v->car) >= 0 && ed_.field.size() < size_t(data::ModEventDef::kMaxPool))
                    ed_.field.push_back({l, race::pool_color_id(*e, int(&l - e->entries.data()))});
        if (ed_.field.empty()) ed_.field.push_back({cars_.front().label, 0xFFFFFFFFu});
        id_.clear();
        picture_host_.clear();
        picture_.clear(), picture_removed_ = false, picture_tex_dirty_ = true;
        dirty_ = true;
        go(Page::Event);
        picker_sound(app, "ok");
    }

    void edit_event(App& app, size_t i) {
        ed_ = events_[i];
        id_ = ed_.id;
        picture_host_ = ed_.picture.empty() ? std::string() : mod_dir(app) + "/" + ed_.picture;
        picture_.clear(), picture_removed_ = false, picture_tex_dirty_ = true;
        // Lineups this disc cannot race (cars it lacks) drop those cars.
        auto keep = [this](std::vector<data::ModEventCar>& v) {
            std::erase_if(v, [this](const data::ModEventCar& c) { return car_index(entry_car(c)) < 0; });
        };
        keep(ed_.field);
        for (data::ModEventRace& r : ed_.races) keep(r.opponents);
        std::erase_if(ed_.races, [this](const data::ModEventRace& r) {
            return std::none_of(courses_.begin(), courses_.end(), [&](const PickerCourse& c) { return c.label == r.course; });
        });
        if (ed_.races.empty()) ed_.races.push_back({courses_.front().label, 3, {}, -1});
        dirty_ = false;
        go(Page::Event);
        picker_sound(app, "ok");
    }

    // The editing pages (a race, a pool, a car of it, the prizes, the car rules, the cars allowed): what they change is
    // saved as it changes; Save keeps it and goes up a page, Cancel puts back what the page opened on.
    void open_sub(Page p, bool snapshot = true) {
        if (snapshot) undo_.push_back(ed_);
        page_ = p, sel_ = 0, armed_ = false;
    }
    Page parent_of(Page p) const {
        switch (p) {
        case Page::Pool: return pool_race_ < 0 ? Page::Event : Page::Race;
        case Page::Entry: return Page::Pool;
        case Page::Allowed: return Page::Rules;
        default: return Page::Event;
        }
    }
    void leave_sub(bool keep) {
        if (!undo_.empty()) {
            if (!keep) ed_ = undo_.back(), dirty_ = true;
            undo_.pop_back();
        }
        page_ = parent_of(page_), sel_ = 0, armed_ = false;
    }
    void editing_buttons(std::vector<Row>& rows) {
        rows.push_back(button("Cancel", {632 - 246, kButtonY, 120, kButtonH}, [this] { leave_sub(false); }, false,
                              "Puts back what this page held when it opened"));
        rows.push_back(button("Save", {632 - 120, kButtonY, 120, kButtonH}, [this] { leave_sub(true); }, true));
    }

    void open_pool(int race) {
        pool_race_ = race;
        open_sub(Page::Pool);
    }
    void open_entry(int i, bool snapshot = true) {
        entry_ = i;
        open_sub(Page::Entry, snapshot);
    }

    void open_race(App& app, int k, bool snapshot = true) {
        race_ = k;
        open_sub(Page::Race, snapshot);
        picker_sound(app, "ok");
    }
    void course_changed(App&) { dirty_ = true; }

    void add_race(App& app) {
        data::ModEventRace r;
        r.course = ed_.races.empty() ? courses_.front().label : ed_.races.back().course;
        r.laps = ed_.races.empty() ? 3 : ed_.races.back().laps;
        undo_.push_back(ed_); // (Cancel takes the new race away again)
        ed_.races.push_back(r);
        dirty_ = true;
        open_race(app, int(ed_.races.size()) - 1, false);
    }

    void remove_race(App& app) {
        if (!armed_) {
            armed_ = true;
            picker_sound(app, "cursor");
            return;
        }
        armed_ = false;
        if (ed_.races.size() <= 1) {
            note("An event needs a race: change this one instead");
            return;
        }
        ed_.races.erase(ed_.races.begin() + race_);
        dirty_ = true;
        leave_sub(true);
        picker_sound(app, "ok");
    }

    void open_courses() {
        auto it = std::find_if(courses_.begin(), courses_.end(), [&](const PickerCourse& c) { return c.label == ed_.races[size_t(race_)].course; });
        course_picker_.set_labels("The race's course", "Triangle: back");
        course_picker_.open(courses_, it == courses_.end() ? 0 : int(it - courses_.begin()), [](const std::string&) { return 0u; });
        page_ = Page::Courses;
    }

    void update_courses(App& app) {
        CoursePicker::Result r = course_picker_.update(app);
        if (r == CoursePicker::Result::None) return;
        page_ = Page::Race;
        if (r != CoursePicker::Result::Chosen) return;
        ed_.races[size_t(race_)].course = courses_[size_t(course_picker_.course())].label;
        course_changed(app);
    }

    void open_cars(App& app, Pick who, int car, int paint) {
        picking_ = who;
        car_picker_.open(app, art_, db_, cars_, std::max(car, 0), paint, [](int, int) { return std::string(); });
        page_ = Page::Cars;
        picker_sound(app, "ok");
    }

    void update_cars(App& app) {
        const CarPicker::Result r = car_picker_.update(app);
        if (r == CarPicker::Result::None) return;
        const Page back_to = picking_ == Pick::PrizeCar     ? Page::Prizes
                             : picking_ == Pick::PoolAdd    ? Page::Pool
                             : picking_ == Pick::AllowedCar ? Page::Allowed
                                                            : Page::Entry;
        page_ = back_to;
        if (r != CarPicker::Result::Chosen) return;
        const int car = car_picker_.car(), paint = car_picker_.paint();
        if (picking_ == Pick::AllowedCar) {
            const std::string& l = cars_[size_t(car)].label;
            if (std::find(ed_.allowed.begin(), ed_.allowed.end(), l) == ed_.allowed.end() && ed_.allowed.size() < list_room())
                ed_.allowed.push_back(l);
        } else if (picking_ == Pick::PrizeCar) {
            ed_.prize_car = cars_[size_t(car)].label;
            const u32 id = data_->color_id(ed_.prize_car, paint);
            ed_.prize_paint = id == 0xFFFFFFFFu ? -1 : int(id);
        } else if (picking_ == Pick::PoolAdd) {
            if (pool().size() < size_t(data::ModEventDef::kMaxPool)) {
                undo_.push_back(ed_); // (Cancel on its page takes it out again)
                pool().push_back({cars_[size_t(car)].label, data_->color_id(cars_[size_t(car)].label, paint)});
                open_entry(int(pool().size()) - 1, false);
            }
        } else if (entry_ >= 0 && entry_ < int(pool().size())) { // another car: stock, in the colour chosen
            pool()[size_t(entry_)] = {cars_[size_t(car)].label, data_->color_id(cars_[size_t(car)].label, paint)};
        }
        dirty_ = true;
    }

    void act(App& app, const Row& row, int h, bool cross) {
        note_.clear();
        if (cross && row.choose) {
            row.choose();
            return;
        }
        if (h && row.change) {
            row.change(h);
            picker_sound(app, "cursor");
        } else if (cross && row.change) {
            row.change(1);
            picker_sound(app, "cursor");
        }
    }

    void back(App& app) {
        picker_sound(app, "cancel");
        note_.clear();
        switch (page_) {
        case Page::List:
            flush(app);
            app.pop();
            return;
        case Page::Event:
            flush(app);
            go(Page::List);
            return;
        default: leave_sub(true); return; // (an editing page: as Save)
        }
    }

    void note(std::string s) { note_ = std::move(s); }

    // --- the picture ----------------------------------------------------------------------------

    bool has_picture() const { return !picture_.empty() || (!picture_removed_ && !picture_path().empty() && host_path_exists(picture_path())); }
    std::string picture_path() const { return id_.empty() || ed_.picture.empty() ? std::string() : picture_host_; }
    std::string picture_state() const {
        if (!picture_.empty()) return "New picture";
        return has_picture() ? "Chosen" : "None";
    }

    void open_dialog(App& app, menu::flow::FileDialogs::Kind kind, const std::string& name) {
        install_save_dialogs(app);
        const menu::flow::FileDialogs& d = menu::flow::file_dialogs();
        if (!d.show || !d.poll) {
            note("No file dialog here");
            return;
        }
        d.poll(); // (an answer left from a dialog given up on)
        waiting_ = true;
        dialog_ = kind;
        d.show(kind, name);
    }

    void poll_dialog(App& app) {
        if (!waiting_) return;
        const menu::flow::FileDialogs& d = menu::flow::file_dialogs();
        std::optional<std::string> path = d.poll ? d.poll() : std::optional<std::string>(std::string());
        if (!path) return;
        waiting_ = false;
        app.menu().suppress();
        if (path->empty()) return; // (cancelled)
        const std::string file = d.name ? d.name(*path) : platform::picked_file_name(*path);
        if (dialog_ == menu::flow::FileDialogs::Kind::OpenPicture) load_picture(*path, file);
        else write_export(*path, file);
    }

    void load_picture(const std::string& path, const std::string& file) {
        std::optional<Bytes> b = read_host_file(path);
        if (!b) {
            note("Could not read " + file);
            return;
        }
        try {
            PngImage img = decode_png(*b, 8192);
            if (img.width < 16 || img.height < 16) throw std::runtime_error("too small");
            picture_ = cover(img, kPictureW, kPictureH);
            picture_removed_ = false, picture_tex_dirty_ = true, dirty_ = true;
            note("Picture from " + file + " (shown 640 x 448)");
        } catch (const std::exception& e) {
            note(file + " is not a PNG picture this game can read (" + std::string(e.what()) + ")");
        }
    }

    void draw_picture(App& app, const SDL_FRect& dst) {
        if (picture_tex_dirty_) {
            picture_tex_dirty_ = false;
            if (picture_tex_) SDL_DestroyTexture(picture_tex_), picture_tex_ = nullptr;
            std::vector<u32> px = picture_;
            u32 w = kPictureW, h = kPictureH;
            if (px.empty() && !picture_removed_ && has_picture())
                if (std::optional<Bytes> b = read_host_file(picture_path())) try {
                    PngImage img = decode_png(*b, 4096);
                    w = img.width, h = img.height, px = std::move(img.rgba);
                } catch (const std::exception&) {
                }
            if (!px.empty()) {
                picture_tex_ = SDL_CreateTexture(app.renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(w), int(h));
                if (picture_tex_) {
                    SDL_UpdateTexture(picture_tex_, nullptr, px.data(), int(w * 4));
                    SDL_SetTextureScaleMode(picture_tex_, SDL_SCALEMODE_LINEAR);
                }
            }
        }
        if (picture_tex_) SDL_RenderTexture(app.renderer(), picture_tex_, nullptr, &dst);
    }

    // --- saving ---------------------------------------------------------------------------------

    bool check(App&) {
        if (ed_.field.empty()) return note("Choose its opponents first"), false;
        return true;
    }

    // The rules a race of the league's own runs on (data::ModEventDef::base): its start, its
    // opponents' driving and its tyre wear.
    std::string base_for(const std::string& league) const {
        if (league.empty()) return {};
        const std::string tier = league.substr(2, league.find('/', 2) - 2);
        const game::RaceSeries* any = nullptr;
        for (const game::RaceSeries& s : data_->catalog.series) {
            if (s.tier != tier || s.races.empty()) continue;
            if (s.championship() == ed_.championship) return s.races.front();
            if (!any) any = &s;
        }
        return any ? any->races.front() : std::string();
    }

    // Writes the event as it is now (every change, update()); GT Mode reads it once the mods are scanned again (flush).
    void persist(App& app) {
        dirty_ = false;
        const std::string dir = mod_dir(app);
        host_make_dir(app.mods_dir());
        host_make_dir(dir);
        host_make_dir(dir + "/events");
        if (!host_path_exists(dir + "/mod.ini")) {
            const std::string ini = "[mod]\nname = My events\ndescription = Events made with the Event creator.\n";
            write_host_file(dir + "/mod.ini", ByteSpan(reinterpret_cast<const u8*>(ini.data()), ini.size()));
        }
        if (id_.empty()) { // a new event: a file name of its own
            const std::string base = id_from(ed_.title);
            id_ = base;
            for (int n = 2; host_path_exists(dir + "/events/" + id_ + ".ini"); ++n) id_ = base + "_" + std::to_string(n);
        }
        ed_.id = id_;
        // A race draws no more cars than its course's grid holds.
        for (data::ModEventRace& r : ed_.races) {
            const int pool = int(race_pool(r).size()), room = grid_limit(app, r.course);
            const int n = r.cars > 0 ? std::min(r.cars, pool) : std::min(pool, data::ModEventDef::kMaxOpponents);
            if (n > room) r.cars = room;
        }
        // The tunes a pool still names (a car switched back to stock leaves its tune behind).
        std::erase_if(ed_.tunes, [this](const data::ModEventTune& t) {
            auto named = [&](const std::vector<data::ModEventCar>& v) {
                return std::any_of(v.begin(), v.end(), [&](const data::ModEventCar& c) { return c.label == "tune:" + t.name; });
            };
            if (named(ed_.field)) return false;
            return std::none_of(ed_.races.begin(), ed_.races.end(), [&](const data::ModEventRace& r) { return named(r.opponents); });
        });
        ed_.base = base_for(league_on_disc(ed_.league) == game::kCustomLeaguePage ? std::string() : ed_.league);
        // The picture: a new one written, a removed one deleted.
        const std::string pic_rel = "pictures/" + id_ + ".png";
        if (!picture_.empty()) {
            host_make_dir(dir + "/pictures");
            const Bytes png = encode_png(kPictureW, kPictureH, picture_);
            if (!write_host_file(dir + "/" + pic_rel, png)) return note("Could not save the picture");
            ed_.picture = pic_rel;
            picture_.clear();
            picture_tex_dirty_ = true; // (now from its file)
        } else if (picture_removed_) {
            host_remove(dir + "/" + pic_rel);
            ed_.picture.clear();
        } else if (!has_picture()) {
            ed_.picture.clear();
        }
        picture_removed_ = false;
        picture_host_ = ed_.picture.empty() ? std::string() : dir + "/" + pic_rel;
        const std::string text = data::format_mod_event(ed_);
        if (!write_host_file(dir + "/events/" + id_ + ".ini", ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size())))
            return note("Could not save the event");
        rescan_ = true;
        LOG_INFO("Event creator: saved %s (%s)", ed_.title.c_str(), id_.c_str());
    }

    // The mods scanned again (GT Mode's data reloads at its next visit), once a page of changes is left.
    void flush(App& app) {
        if (!rescan_) return;
        rescan_ = false;
        app.rescan_mods();
        read_events(app);
    }

    void delete_event(App& app) {
        if (!armed_) {
            armed_ = true;
            picker_sound(app, "cursor");
            return;
        }
        armed_ = false;
        const std::string dir = mod_dir(app);
        host_remove(dir + "/events/" + id_ + ".ini");
        host_remove(dir + "/pictures/" + id_ + ".png");
        LOG_INFO("Event creator: deleted %s", id_.c_str());
        rescan_ = false;
        app.rescan_mods();
        read_events(app);
        dirty_ = false;
        id_.clear();
        go(Page::List);
        picker_sound(app, "ok");
    }

    void export_event(App& app) {
        if (!check(app)) return;
        open_dialog(app, menu::flow::FileDialogs::Kind::ExportEvent, (id_.empty() ? id_from(ed_.title) : id_) + ".zip");
    }

    // The event as a mod of its own: <id>/mod.ini, <id>/events/<id>.ini, <id>/pictures/<id>.png.
    void write_export(const std::string& path, const std::string& file) {
        const std::string id = id_.empty() ? id_from(ed_.title) : id_;
        data::ModEventDef d = ed_;
        d.id = id;
        std::vector<std::pair<std::string, Bytes>> files;
        auto text = [](const std::string& s) { return Bytes(s.begin(), s.end()); };
        std::vector<u32> px = picture_;
        if (px.empty() && !picture_removed_ && has_picture())
            if (std::optional<Bytes> b = read_host_file(picture_path())) {
                files.emplace_back(id + "/pictures/" + id + ".png", std::move(*b));
                d.picture = "pictures/" + id + ".png";
            }
        if (!px.empty()) {
            files.emplace_back(id + "/pictures/" + id + ".png", encode_png(kPictureW, kPictureH, px));
            d.picture = "pictures/" + id + ".png";
        } else if (files.empty()) {
            d.picture.clear();
        }
        std::string mod = "[mod]\nname = " + d.title + "\n";
        if (!d.description.empty()) mod += "description = " + d.description + "\n";
        files.emplace_back(id + "/mod.ini", text(mod));
        files.emplace_back(id + "/events/" + id + ".ini", text(data::format_mod_event(d)));
        const Bytes zip = write_zip(files);
        note(write_host_file(path, zip) ? "Exported to " + file + ": install it from the Mods screen" : "Could not save " + file);
    }

    // --- text -----------------------------------------------------------------------------------

    // The game's own keyboard when the field was chosen with a controller (it has no keys to type with) or in a headset
    // (no system keyboard there); else the field types in place: a real keyboard, a phone's own screen keyboard.
    bool game_keyboard(App& app) const {
#if GT3R_XR
        if (app.xr()) return true;
#else
        (void)app;
#endif
        return via_pad_;
    }

    void begin_naming(App& app, Text which) {
        picker_sound(app, "ok");
        naming_ = true;
        text_ = which;
        name_text_ = which == Text::Title ? ed_.title : ed_.description;
        text_label_ = which == Text::Title ? "Title" : "Description";
        edit_key_ = which == Text::Title ? "title" : "description";
        if (game_keyboard(app)) name_keyboard_.open(text_label_, "Done", false);
        else if (SDL_Window* w = app.window()) SDL_StartTextInput(w);
    }

    // A sum typed in (credits): digits only, at most `cap`. A phone shows its number pad; a headset the game's keyboard.
    void begin_number(App& app, std::string label, std::string key, u32* value, u32 cap) {
        picker_sound(app, "ok");
        naming_ = true;
        text_ = Text::Number;
        text_label_ = std::move(label);
        edit_key_ = std::move(key);
        number_ = value, number_cap_ = cap;
        name_text_ = std::to_string(*value);
        if (game_keyboard(app)) {
            name_keyboard_.open(text_label_, "Done", false, TextKeyboard::Layout::Number);
        } else if (SDL_Window* w = app.window()) {
            SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER, SDL_TEXTINPUT_TYPE_NUMBER);
            SDL_StartTextInputWithProperties(w, props);
            SDL_DestroyProperties(props);
        }
    }
    static void digits_only(std::string& s) {
        std::erase_if(s, [](char c) { return !std::isdigit(static_cast<unsigned char>(c)); });
        if (s.size() > 9) s.resize(9);
    }

    void end_naming(App& app, bool keep) {
        naming_ = false;
        edit_key_.clear();
        name_keyboard_.close();
        if (SDL_Window* w = app.window()) SDL_StopTextInput(w);
        app.menu().suppress(); // the keys typed are no menu input
        if (!keep) return;
        const std::string t(trim(name_text_));
        if (text_ == Text::Title && !t.empty()) ed_.title = t;
        if (text_ == Text::Description) ed_.description = t;
        if (text_ == Text::Number && number_ && !t.empty()) {
            const u32 v = u32(std::min<unsigned long long>(std::strtoull(t.c_str(), nullptr, 10), number_cap_));
            if (v < std::strtoull(t.c_str(), nullptr, 10)) note("At most " + money(number_cap_) + ", what this disc's races pay");
            *number_ = v;
        }
        number_ = nullptr;
        dirty_ = true;
    }

    void name_event(App& app, const SDL_Event& e) {
        const bool keyboard = name_keyboard_.is_open();
        float x = 0, y = 0;
        if (keyboard) {
            name_pointer_.event(e);
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key != SDLK_UP && e.key.key != SDLK_DOWN && e.key.key != SDLK_LEFT &&
                e.key.key != SDLK_RIGHT)
                app.menu().suppress();
        } else if (pointer_down(e, x, y)) { // (a click elsewhere finishes it)
            end_naming(app, true);
            return;
        }
        const TextKeyboard::Result r = text_field_event(e, name_text_, !keyboard);
        if (text_ == Text::Number) digits_only(name_text_);
        if (r != TextKeyboard::Result::None) end_naming(app, r == TextKeyboard::Result::Done);
    }

    void render_naming(Ui& ui) {
        if (name_keyboard_.is_open()) name_keyboard_.draw(ui, name_text_);
        return; // (else the field types in place, render_row)
        const float x = 120, y = 70, w = 400, h = 52; // (above a phone's keyboard)
        ui.fill_round(x - 2, y - 2, w + 4, h + 4, 6, colors::kOutline);
        ui.fill_round(x, y, w, h, 5, colors::kPanel);
        ui.text(x + 12, y + 10, text_label_, colors::kAccent);
        ui.text(x + 12, y + 30, Ui::elide(name_text_ + (text_ == Text::Number ? " Cr._" : "_"), w - 24), colors::kText);
        ui.footer(text_ == Text::Number ? "Type an amount, Enter to keep, Esc to cancel (pad: up/down digit, right add, left delete, A done)"
                                        : "Type, Enter to keep, Esc to cancel (pad: up/down letter, right add, left delete, A done)");
    }

    // --- drawing --------------------------------------------------------------------------------

    // The title bar's "< Back": the last row of the list and an event's page, so the pad reaches it (Up from the first row).
    static UiRect back_rect() { return {6, 2, 64, 16}; }
    Row back_row(App& app) {
        return button("< Back", back_rect(), [this, &app] { back(app); }, false,
                      page_ == Page::List ? "Back to the main menu" : "Back to your events");
    }

    // The list's and an event's title bar: "< Back" at its left, as the Mods and Settings screens have it, then the title.
    void top_bar(Ui& ui) {
        const UiRect& S = ui.safe();
        ui.fill(0, 0, ui.width(), 20, colors::kPanel);
        const UiRect back = back_rect(); // (its button is the page's last row: back_row)
        ui.text(back.right() + 12, 6, "EVENT CREATOR", colors::kAccent);
        const float left = back.right() + 12 + Ui::text_width("EVENT CREATOR") + 16, room = S.right() - 8 - left;
        if (room > 16) ui.text_right(S.right() - 8, 6, Ui::elide(page_title(), room), colors::kDim);
    }

    void render_row(App& app, Ui& ui, const Row& r, int i, bool f) {
        if (r.kind == Row::Kind::Button) {
            ui.button(r.r, r.label, f, false, r.primary);
            hits_.add(r.r, kHitButton, i);
            return;
        }
        const UiRect& b = r.r;
        if (f) ui.fill_round(b.x, b.y, b.w, b.h - 2, 4, colors::kHighlight);
        hits_.add({b.x, b.y, b.w, b.h - 2}, kHitRow, i);
        if (r.kind == Row::Kind::Field) { // its label, then a box holding its text (with a caret while it is typed in)
            const float ty = b.y + (b.h - 2 - Ui::kChar) / 2;
            ui.text(b.x + 6, ty, Ui::clip(r.label, 90), f ? colors::kAccent : colors::kText);
            const bool typing = naming_ && !name_keyboard_.is_open() && edit_key_ == r.field;
            const float bx = b.x + 98, bw = b.right() - bx - 4 - (r.suffix.empty() ? 0 : Ui::text_width(r.suffix) + 4);
            ui.fill_round(bx, b.y + 1, bw, b.h - 4, 3, colors::kBackground);
            ui.frame_round(bx, b.y + 1, bw, b.h - 4, 3, 1, typing || f ? colors::kAccent : colors::kOutline);
            const std::string text = typing ? name_text_ : r.value;
            ui.text(bx + 5, ty, typing ? Ui::elide(text + "_", bw - 10) : Ui::clip(text, bw - 10), colors::kText);
            if (!r.suffix.empty()) ui.text(bx + bw + 4, ty, r.suffix, colors::kText);
            return;
        }
        const float ty = b.y + (b.h - 2 - Ui::kChar) / 2;
        const Color lc = r.primary || f ? colors::kAccent : colors::kText;
        if (r.kind == Row::Kind::Action && !r.change) {
            if (r.label.empty()) { // (an action of its own: "+ Add a race"; a list entry: its name)
                ui.text(b.x + 6, ty, Ui::clip((r.dim ? "" : "+ ") + r.value, b.w - 12), f ? colors::kAccent : r.dim ? colors::kText : lc);
                return;
            }
            const float lw = page_ == Page::List ? b.w - 130 : 92;
            ui.text(b.x + 6, ty, Ui::clip(r.label, lw - 8), lc);
            ui.text(b.x + 6 + lw, ty, Ui::clip(r.value, b.w - lw - 12), page_ == Page::List ? colors::kDim : colors::kText);
            return;
        }
        ui.text(b.x + 6, ty, Ui::clip(r.label, 90), lc);
        const float vx = b.x + 98;
        value_arrows(ui, {vx, b.y + 1, b.right() - vx - 2, b.h - 4}, r.value, colors::kText, hits_, kHitLeft, kHitRight, i, pointer_);
    }

    void render_side(App& app, Ui& ui, const std::vector<Row>& rows) {
        if (page_ == Page::List) {
            const float x = kRightX, w = kRightW;
            if (events_.empty()) {
                float y = kTopY + 4;
                for (const std::string& l : Ui::wrap("Make your own GT Mode events: pick the courses, the laps and the cars you race, "
                                                     "single races or a championship, the prize money and a prize car. GT Mode lists "
                                                     "them in the league you choose, ready to enter with your own cars.",
                                                     w))
                    ui.text(x, y, l, colors::kDim), y += 12;
                return;
            }
            if (sel_ > 0 && sel_ <= int(events_.size())) render_summary(app, ui, events_[size_t(sel_ - 1)]);
            return;
        }
        if (page_ == Page::Event) {
            const SDL_FRect pic{kRightX, kTopY, 160, 112};
            ui.fill_round(pic.x - 1, pic.y - 1, pic.w + 2, pic.h + 2, 3, colors::kOutline);
            ui.fill(pic.x, pic.y, pic.w, pic.h, colors::kPanel);
            draw_picture(app, pic);
            if (!picture_tex_) ui.text_center_in(pic.x, pic.w, pic.y + 50, "No picture", colors::kDim);
            float y = kTopY + 2;
            const float tx = pic.x + pic.w + 8, tw = kRightX + kRightW - tx;
            for (const std::string& l : Ui::wrap(ed_.title, tw)) ui.text(tx, y, l, colors::kAccent), y += 12;
            y += 4;
            for (const std::string& l : Ui::wrap("In GT Mode: " + league_title(league_on_disc(ed_.league)), tw))
                ui.text(tx, y, l, colors::kDim), y += 11;
            if (!ed_.prize_car.empty())
                for (const std::string& l : Ui::wrap("Prize car: " + car_name(ed_.prize_car), tw)) ui.text(tx, y, l, colors::kDim), y += 11;
            if (ed_.field.empty()) ui.text(tx, pic.y + pic.h - 10, "No opponents yet", colors::kBad);
            return;
        }
        if (page_ == Page::Race) {
            const data::ModEventRace& r = ed_.races[size_t(race_)];
            art_.course_still(app, r.course, SDL_FRect{kLeftX + 2, 28, 228, 76}, true);
            const float x = 256;
            float y = kTopY + 4;
            const std::vector<data::ModEventCar>& p = race_pool(r);
            ui.text(x, y, ed_.championship || r.opponents.empty() ? "It draws from the event's opponents:" : "It draws from its own cars:",
                    colors::kDim);
            y += 16;
            const float colw = (632 - x) / 2;
            for (size_t i = 0; i < p.size(); ++i)
                ui.text(x + 8 + float(i % 2) * colw, y + float(i / 2) * 12, Ui::clip(entry_name(p[i]), colw - 12), colors::kText);
            return;
        }
        if (page_ == Page::Pool) {
            std::vector<data::ModEventCar>& p = pool();
            ui.text(kRightX, kTopY + 4, std::to_string(p.size()) + " of at most " + std::to_string(data::ModEventDef::kMaxPool) + " cars:",
                    colors::kText);
            return;
        }
        if (page_ == Page::Entry) {
            std::vector<data::ModEventCar>& p = pool();
            if (entry_ < 0 || entry_ >= int(p.size())) return;
            const data::ModEventCar& c = p[size_t(entry_)];
            const int i = car_index(entry_car(c));
            const float y = kTopY + 3 * kRowH + 4;
            if (!is_tune(c) && i >= 0) art_.car_icon(app, car_icon_name(cars_[size_t(i)]), SDL_FRect{kRightX + 40, kTopY + 10, 220, 118});
            ui.text(kLeftX + 6, y, entry_facts(c), colors::kDim);
            return;
        }
        if (page_ == Page::Rules) {
            int allowed = 0;
            for (int i = 0; i < int(cars_.size()); ++i) allowed += may_enter(i, false);
            float y = kTopY + 4;
            ui.text(kRightX, y, std::to_string(allowed) + " of this disc's " + std::to_string(cars_.size()) + " cars may enter,",
                    allowed || ed_.car_class == 2 ? colors::kText : colors::kBad);
            y += 12;
            ui.text(kRightX, y, ed_.car_class == 2 ? "and any of them once tuned:" : "as the dealers sell them:", colors::kText);
            y += 16;
            const float colw = kRightW / 2;
            int n = 0;
            const int fit = int((kButtonY - 8 - y) / 11) * 2;
            for (int i = 0; i < int(cars_.size()) && allowed; ++i) {
                if (!may_enter(i, false)) continue;
                if (n == fit - 1 && allowed > fit) {
                    ui.text(kRightX + colw, y + float(n / 2) * 11, "and " + std::to_string(allowed - n) + " more", colors::kDim);
                    break;
                }
                ui.text(kRightX + float(n % 2) * colw, y + float(n / 2) * 11, Ui::clip(cars_[size_t(i)].name, colw - 8), colors::kDim);
                ++n;
            }
            return;
        }
        if (page_ == Page::Allowed) {
            ui.text(kRightX, kTopY + 4,
                    ed_.allowed.empty() ? "No list: any car may enter"
                                        : std::to_string(ed_.allowed.size()) + " of at most " + std::to_string(list_room()) + " cars:",
                    colors::kText);
            return;
        }
        if (page_ == Page::Prizes && !ed_.prize_car.empty()) {
            const int i = car_index(ed_.prize_car);
            if (i >= 0) art_.car_icon(app, car_icon_name(cars_[size_t(i)]), SDL_FRect{kRightX + 40, 60, 220, 118});
        }
        (void)rows;
    }

    void render_summary(App& app, Ui& ui, const data::ModEventDef& d) {
        const float x = kRightX, w = kRightW;
        float y = kTopY;
        if (!d.races.empty()) art_.course_still(app, d.races.front().course, SDL_FRect{x, y, w, 110}, true);
        y += 116;
        ui.text(x, y, Ui::clip(d.title, w), colors::kAccent), y += 14;
        auto line = [&](std::string label, const std::string& value) {
            for (const std::string& l : Ui::wrap(value, w - 76)) {
                ui.text(x, y, label, colors::kDim);
                ui.text(x + 76, y, l, colors::kText);
                y += 11;
                label.clear();
            }
        };
        if (!d.description.empty()) line("About", d.description);
        line("GT Mode", league_title(league_on_disc(d.league)));
        line("Format", d.championship ? "Championship" : "Single races");
        std::string races;
        for (const data::ModEventRace& r : d.races) races += (races.empty() ? "" : ", ") + course_name(r.course);
        line("Races", races);
        line("1st prize", money(d.prizes[0]));
        if (!d.prize_car.empty()) line("Prize car", car_name(d.prize_car));
    }

    // --- pointer --------------------------------------------------------------------------------

    void pointer_actions(App& app) {
        using K = PointerInput::Action::Kind;
        for (const PointerInput::Action& a : pointer_.take()) {
            const Page page = page_;
            if (a.kind == K::Back) back(app);
            else if (a.kind == K::Hover || a.kind == K::Click) pointer_at(app, a);
            if (page_ != page || naming_ || waiting_) return; // (the rest was for a page no longer on screen)
        }
    }

    void pointer_at(App& app, const PointerInput::Action& a) {
        const bool click = a.kind == PointerInput::Action::Kind::Click;
        via_pad_ = false;
        const HitArea* h = hits_.at(a.x, a.y);
        if (!h) return;
        if (h->id == kHitFooter) {
            if (!click) return;
            if (h->arg == kFootBack) back(app);
            else {
                const std::vector<Row> rows = build_rows(app);
                if (!rows.empty()) act(app, rows[size_t(std::clamp(sel_, 0, int(rows.size()) - 1))], 0, true);
            }
            return;
        }
        const int row = h->arg;
        const bool focused = sel_ == row;
        if (!focused) sel_ = row, armed_ = false;
        const std::vector<Row> rows = build_rows(app);
        if (row < 0 || row >= int(rows.size())) return;
        if (!click) {
            if (!focused) picker_sound(app, "cursor");
        } else if (h->id == kHitLeft || h->id == kHitRight) {
            act(app, rows[size_t(row)], h->id == kHitLeft ? -1 : 1, false);
        } else if (h->id == kHitButton || focused || !a.finger) {
            act(app, rows[size_t(row)], 0, true);
        } else {
            picker_sound(app, "cursor");
        }
    }

    bool init_ = false;
    std::string error_, note_;
    data::CarDatabase db_;
    std::shared_ptr<game::GtData> data_;
    std::vector<PickerCar> cars_;
    std::vector<PickerCourse> courses_;
    std::vector<std::string> leagues_;
    std::map<std::string, int> grid_limits_;
    u32 max_prize_ = 0, max_bonus_ = 0;
    std::vector<data::ModEventDef> events_;
    std::vector<std::optional<game::CarFacts>> facts_; // per car, for the list's filters (stock)
    int filter_class_ = 0, filter_ps_ = 0;             // the list page's filters (they make the list; not saved)
    bool filter_mods_ = true;

    Page page_ = Page::List;
    int sel_ = 0;
    bool armed_ = false;

    data::ModEventDef ed_; // the event being edited
    std::string id_;       // its file name ("" = not saved yet)
    bool dirty_ = false;   // a change not written yet (update() writes it)
    bool rescan_ = false;  // written since the mods were last scanned
    int race_ = 0;         // the race page's race
    int pool_race_ = -1;   // the Pool page's pool: -1 the event's, else that race's own
    int entry_ = -1;       // the Entry page's car in it
    std::vector<data::ModEventDef> undo_; // the event as each open editing page found it (Cancel)
    std::map<std::string, std::vector<Version>> versions_; // per car: the disc's variants of it
    Pick picking_ = Pick::None;

    std::vector<u32> picture_; // a new picture (640 x 448), not saved yet
    bool picture_removed_ = false, picture_tex_dirty_ = true;
    std::string picture_host_;
    SDL_Texture* picture_tex_ = nullptr;

    bool waiting_ = false;
    menu::flow::FileDialogs::Kind dialog_ = menu::flow::FileDialogs::Kind::OpenPicture;

    bool naming_ = false;
    Text text_ = Text::None;
    std::string text_label_;
    std::string edit_key_; // the field typed in (Row::field)
    bool via_pad_ = false; // the last choice came from a controller (game_keyboard)
    u32* number_ = nullptr; // Text::Number: the sum typed into, and its cap
    u32 number_cap_ = 0;
    std::string name_text_;
    TextKeyboard name_keyboard_;
    PointerInput name_pointer_;

    PickerArt art_;
    CarPicker car_picker_;
    CoursePicker course_picker_;
    PointerInput pointer_;
    HitList hits_;
};

GT3R_REGISTER_PLAYER_SCREEN(event_creator, "eventcreator", "Event creator",
                            "Make your own GT Mode events: courses, opponents, prizes and a picture for each.", 7,
                            [] { return std::make_unique<EventCreatorScreen>(); });

} // namespace gt3r
