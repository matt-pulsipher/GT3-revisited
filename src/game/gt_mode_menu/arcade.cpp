// GT-mode menu bindings: the arcade menus' game data (gt_mode §16): the pickers' entries (open
// courses and cars, garage classes), refusals, entry art, gauges, paints and spec ticker, the
// race request of garage cars, and arcade results (Single Race progression, time-trial records).

#include "game/gt_mode_menu/internal.h"

#include "base/log.h"
#include "game/data/weight_class.h"
#include "sim/car_spec.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gt3r::game {

using namespace gt_menu;

const GarageCar* arcade_garage_car(const std::string& entry) {
    GtModeSession* s = S();
    if (!s || entry.size() < 3 || (entry.compare(0, 2, "hg") != 0 && entry.compare(0, 2, "gg") != 0)) return nullptr;
    const GtMode* g = entry[0] == 'h' ? &s->gt() : s->guest.get();
    if (!g) return nullptr;
    int i = std::atoi(entry.c_str() + 2);
    const auto& garage = g->state().garage;
    return i >= 0 && i < int(garage.size()) ? &garage[size_t(i)] : nullptr;
}

std::string arcade_entry_car(const std::string& entry) {
    GtModeSession* s = S();
    if (!s) return {};
    if (const GarageCar* gc = arcade_garage_car(entry)) return gc->car;
    const GtMode& gt = s->gt();
    auto base_of = [&](const std::string& label) {
        for (const data::CarVariant& v : gt.db().arcade_cars())
            if (v.label == label) return v.car;
        return gt.db().car(label) ? label : std::string();
    };
    // Time trials ("tt01"): the challenge's CLASS_COMPULSORY car.
    if (int i = GtMode::time_trial_index(entry); i >= 0)
        if (auto cls = gt.data().arcade_classes.find("CLASS_COMPULSORY");
            cls != gt.data().arcade_classes.end() && i < int(cls->second.size()))
            return base_of(cls->second[size_t(i)].label);
    for (const auto& [cls, cars] : gt.data().arcade_classes)
        for (const GtData::ArcadeCar& c : cars)
            if (c.image() == entry)
                for (const data::CarVariant& v : gt.db().arcade_cars())
                    if (v.label == c.label) return v.car;
    return gt.db().car(entry) ? entry : std::string();
}

int add_time_trial_record(const std::string& code, double seconds) {
    GtModeSession* s = S();
    const int i = GtMode::time_trial_index(code);
    if (!s || i < 0) return -1;
    GtMode& gt = s->gt();
    // The challenge's car (CLASS_COMPULSORY entry i), as its base car for the records.
    std::string car;
    if (auto cls = gt.data().arcade_classes.find("CLASS_COMPULSORY");
        cls != gt.data().arcade_classes.end() && i < int(cls->second.size())) {
        car = cls->second[size_t(i)].label;
        for (const data::CarVariant& v : gt.db().arcade_cars())
            if (v.label == car) car = v.car;
    }
    // A time that makes the ranking asks for a name, as in GT mode.
    const int rank = gt.arcade_time_trial(code, seconds, car);
    if (rank >= 0)
        menu_state().shows.push_back({"name_entry", "name_entry", {}, "", "", 0, {}, "at:" + code, rank, "", 0, "", true, {}, {}});
    return rank;
}

namespace gt_menu {

namespace {

// A won arcade race's cup page (presentation §F): the difficulty's gold cup
// (race/a_<difficulty>_gold.prm), OK after 3 s; none for a code without a difficulty.
std::optional<Show> arcade_trophy_show(const std::string& race_code) {
    if (race_code.size() < 2) return std::nullopt;
    const char c = race_code[1];
    const char* cls = c == 'E' ? "easy" : c == 'N' ? "normal" : c == 'H' ? "hard" : c == 'P' ? "special" : nullptr;
    if (!cls) return std::nullopt;
    Show sh{};
    sh.set = sh.page = "arcade_mini";
    sh.model = std::string("race/a_") + cls + "_gold.prm";
    sh.rank = ordinal(0);
    return sh;
}

} // namespace

void on_arcade_result(const GameResult& r) {
    MenuState& ms = menu_state();
    GtModeSession* s = S();
    if (!s) return;
    GtMode& gt = s->gt();
    // A garage car's engine condition back, the run's distance into the total (physics §10.3),
    // finished or not. The time trials' and free runs' records went in from the race screen
    // after each run (gt_mode §17), never from here: saved with the rest.
    apply_condition(gt, r);
    if (!r.finished) {
        if ((r.car_condition || r.records_done) && saving_enabled()) s->save();
        return;
    }
    // Single Race: the best place per difficulty and course, and the unlocks.
    if (r.mode == "single_race" && r.position > 0) {
        GtMode::ArcadeOutcome o = gt.arcade_single_race(r.race_code, r.course, r.position - 1);
        // A win: the difficulty's cup (the original shows it for a win only, right after the
        // race; a race screen that queued it itself before the replay says prize_shown).
        if (r.position == 1 && !r.prize_shown)
            if (std::optional<Show> sh = arcade_trophy_show(r.race_code)) ms.shows.push_back(std::move(*sh));
        if (o.courses_opened > 0) LOG_INFO("arcade: course level %d", gt.state().arcade.course_level);
        for (int l : o.car_levels) LOG_INFO("arcade: cars of level %d unlocked", l);
    }
    if (saving_enabled()) s->save();
}

// Arcade picker art filled by code: the course's area (group) badge, dirt / tarmac
// badge and length; the car's drive badge and power / torque.
std::vector<std::pair<std::string, std::string>> arcade_parts(const std::string& page, const std::string& entry) {
    std::vector<std::pair<std::string, std::string>> out;
    GtModeSession* s = S();
    if (!s) return out;
    const GtMode& gt = s->gt();
    if (page == "ar/select_course") {
        if (int l = gt.arcade_course_level(entry); l >= 0 && l < 6) out.push_back({std::string("area_") + char('a' + l), ""});
        out.push_back({gt.arcade_course_dirt(entry) ? "dirt" : "tarmac", ""});
        u32 len = 0;
        for (const auto& [list, courses] : gt.data().arcade_courses)
            for (const GtData::ArcadeCourse& c : courses)
                if (c.course == entry) len = c.length_m;
        out.push_back({"total_length", ""});
        char b[32];
        // NTSC-U: miles with three decimals ("1.494miles"); else metres.
        if (!len) std::snprintf(b, sizeof b, "----");
        else if (gt.data().imperial()) std::snprintf(b, sizeof b, "%.3fmiles", double(len) / 1609.344);
        else std::snprintf(b, sizeof b, "%4um", len);
        out.push_back({"length", b});
    } else if (page == "ar/select_compulsory") {
        // Time trial: the records (top three: the ranking, else the race record's
        // defaults), the car's power and weight.
        int i = GtMode::time_trial_index(entry);
        if (i < 0) return out;
        char code[24];
        std::snprintf(code, sizeof code, "AT%02d0001", i + 1);
        std::vector<double> t;
        if (auto it = gt.state().rankings.find(std::string("at:") + code); it != gt.state().rankings.end())
            for (const RankEntry& r : it->second) t.push_back(r.value);
        if (t.empty()) t = gt.time_trial_defaults(code);
        static const char* const kSlot[3] = {"first_time", "second_time", "third_time"};
        for (int k = 0; k < 3; ++k)
            out.push_back({kSlot[k], k < int(t.size()) ? format_time_ms(u32(std::llround(t[size_t(k)] * 1000)))
                                                        : std::string("--:--:--")});
        if (const data::Car* car = gt.db().car(arcade_entry_car(entry))) {
            data::CarDatabase::Summary sum = gt.db().summarize(*car);
            float ps = sum.power_ps;
            if (auto spec = sim::build_car_spec(gt.db(), arcade_entry_car(entry)); spec && ps <= 0)
                ps = spec_power_ps(*spec);
            char b[32];
            out.push_back({"power_value", gt.power_text(ps, " ")});
            std::snprintf(b, sizeof b, "%d KG", int(sum.weight_kg));
            out.push_back({"weight_value", b});
        }
    } else if (page == "ar/select_car" || page == "ar/select_car_2p") {
        // The picture names the car ("nsx_s_zero_97"); its arcade car gives the base car.
        const data::Car* car = gt.db().car(arcade_entry_car(entry));
        if (!car) return out;
        data::CarDatabase::Summary sum = gt.db().summarize(*car);
        // A garage car shows its own computed power (parts and condition); cars without a
        // catalogue power (rally prototypes): the built spec's peak (M).
        std::string power;
        if (const GarageCar* gc = arcade_garage_car(entry)) {
            PowerPeak p = gt.power_peak(*gc);
            power = gt.power_text(p.ps) + "/" + std::to_string(p.rpm) + "rpm";
        }
        if (sum.power_ps == 0)
            if (auto spec = sim::build_car_spec(gt.db(), car->label)) {
                PowerPeak p = spec_power_peak(*spec);
                sum.power_ps = u16(p.ps);
                sum.power_rpm = std::to_string(p.rpm) + "rpm";
                sum.torque_kgfm = spec->peak_torque() / 9.80665f;
            }
        if (power.empty()) power = gt.power_text(float(sum.power_ps)) + "/" + sum.power_rpm;
        std::string drive = sum.drive;
        for (char& ch : drive) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        out.push_back({drive == "4wd" ? "four_wd" : drive, ""});
        // Power and torque with their rpm: "276HP/7300rpm", "224.21ft.lb/5300rpm" (US English),
        // "31.0kgm/5300rpm" in the other languages.
        char b[64];
        if (gt.data().units.torque_ftlb) {
            out.push_back({"ps", power});
            std::snprintf(b, sizeof b, "%.2fft.lb/%s", double(sum.torque_kgfm) * 7.2327, sum.torque_rpm.c_str());
            out.push_back({"kgm", b});
        } else {
            out.push_back({"ps", power});
            std::snprintf(b, sizeof b, "%.1fkgm/%s", double(sum.torque_kgfm), sum.torque_rpm.c_str());
            out.push_back({"kgm", b});
        }
    }
    return out;
}

// Arcade car pickers: CircleGauge values. Handling and acceleration come from spec
// bytes the builder never sets (0 -> 7 x 0.1 = 0.7 for every car, as in the
// original); speed is the showroom top speed in km/h (truncated) / 350, at most 1
// (menus §9.3).
std::array<float, 3> arcade_gauges(const std::string& entry) {
    std::array<float, 3> v{0.7f, 0.7f, 0.7f};
    GtModeSession* s = S();
    if (!s) return v;
    auto spec = sim::build_car_spec(s->gt().db(), arcade_entry_car(entry));
    if (!spec) return v;
    int kmh = int(sim::showroom_top_speed(*spec) * 3.6f);
    v[1] = std::min(1.0f, float(kmh > 0 ? kmh : 7) / 350.0f);
    return v;
}

// Arcade car pickers: the paints of the car's model (carcolor.db / .sdb).
std::vector<std::pair<std::string, u32>> arcade_colors(const std::string& entry) {
    std::vector<std::pair<std::string, u32>> out;
    GtModeSession* s = S();
    if (!s) return out;
    const GtData& d = s->gt().data();
    std::string car = arcade_entry_car(entry);
    if (const GarageCar* gc = arcade_garage_car(entry)) {
        // A garage car keeps its own colour (one entry, no arrows).
        u32 id = d.color_id(car, gc->color);
        out.push_back({d.color_name(id), d.color_rgb(id)});
        return out;
    }
    for (int i = 0; i < d.color_count(car); ++i) {
        u32 id = d.color_id(car, i);
        out.push_back({d.color_name(id), d.color_rgb(id)});
    }
    return out;
}

// Arcade garage classes: the car page shows the garage car's picture and strip icon by
// its label (the arcade layout holds art for every car label).
std::string arcade_image(const std::string& entry) {
    const GarageCar* gc = arcade_garage_car(entry);
    return gc ? gc->car : std::string();
}

// Arcade car pickers: the spec ticker (always millimetres).
std::vector<std::pair<std::string, std::string>> arcade_specs(const std::string& entry) {
    std::vector<std::pair<std::string, std::string>> out;
    GtModeSession* s = S();
    if (!s) return out;
    const data::Car* car = s->gt().db().car(arcade_entry_car(entry));
    if (!car) return out;
    data::CarDatabase::Summary sum = s->gt().db().summarize(*car);
    auto mm = [](u16 v) { return std::to_string(v) + "mm"; };
    out = {{"Length", mm(sum.length_mm)}, {"Height", mm(sum.height_mm)}, {"Width", mm(sum.width_mm)},
           {"WheelBase", mm(sum.wheelbase_mm)}};
    return out;
}

// Arcade races with garage cars: the car as it is in the garage (parts, settings,
// colour); Single Race picks the race code from its weight / power ratio
// (dirt course: A?AD0001; road: data::class_by_weight's class letter and race).
void arcade_prepare(GameRequest& r) {
    GtModeSession* s = S();
    if (!s) return;
    const GtMode& gt = s->gt();
    const GarageCar* g1 = arcade_garage_car(r.car);
    const GarageCar* g2 = arcade_garage_car(r.car2);
    if (g1 && r.mode == "single_race" && r.race_code.size() == 8) {
        if (gt.arcade_course_dirt(r.course)) {
            r.race_code[2] = 'A', r.race_code[3] = 'D', r.race_code[7] = '1';
        } else {
            float ps = std::max(1.0f, gt.power_ps(*g1));
            float kg = 1000;
            if (auto sp = gt.car_spec(*g1)) kg = std::max(1.0f, sp->mass - 120.0f);
            const data::WeightClass w = data::class_by_weight(kg / ps); // M: weight / power
            r.race_code[2] = "?CBAS"[w.cls];
            r.race_code[3] = 'T';
            r.race_code[7] = char('1' + w.next);
        }
    }
    if (g1) fill_car(r, gt, g1);
    fill_condition(r, gt, g1); // single race / free run: the garage car's engine, or a fresh one
    // Mod cars: their class's race variant ([arcade] race_variant), as for garage cars.
    if (const data::ModCar* mc = gt.db().mod_car(r.car); mc && !g1 && r.mode == "single_race" &&
                                                             r.race_code.size() == 8 && r.race_code[3] == 'T')
        r.race_code[7] = char('1' + mc->arcade_variant);
    if (g2) { // player 2: the garage car as tuned, like player 1's
        GameRequest p2;
        fill_car(p2, gt, g2);
        r.car2 = p2.car;
        r.car2_color = p2.car_color;
        r.car2_parts = p2.car_parts;
        r.car2_settings = p2.car_settings;
        r.car2_model = p2.car_model;
        r.car2_wheel = p2.car_wheel;
        r.racing_gearbox2 = p2.racing_gearbox;
    }
}

// Arcade class page: road classes on a dirt course, the dirt class on a road
// course and a garage class without cars cannot be chosen.
std::string arcade_disabled(const std::string& page, const std::string& course, const std::string& entry) {
    GtModeSession* s = S();
    if (!s) return std::string();
    const GtMode& gt = s->gt();
    // A garage car on a dirt course needs dirt tires (owned-part bit 0x42).
    if (const GarageCar* gc = arcade_garage_car(entry))
        return gt.arcade_course_dirt(course) && !gc->owned.test(66) ? std::string("disable_dirt_doc") : std::string();
    if (page != "ar/select_class") return std::string();
    if (entry == "home_garage") return gt.state().garage.empty() ? std::string("disable_no_car_doc") : std::string();
    if (entry == "guest_garage") // until a guest garage with cars is loaded (Load Garage / Trade)
        return s->guest && !s->guest->state().garage.empty() ? std::string() : std::string("disable_no_car_doc");
    bool dirt = gt.arcade_course_dirt(course);
    if (entry == "class_dirt") return dirt ? std::string() : std::string("disable_dirt_doc");
    if (starts(entry, "class_")) return dirt ? std::string("disable_road_doc") : std::string();
    return std::string();
}

// Arcade pickers: only the open courses (gcf list order) and the class's open cars.
void arcade_entries(const std::string& page, const std::string& game, const std::string& car_class,
                    std::vector<std::string>& entries) {
    GtModeSession* s = S();
    if (!s) return;
    const GtMode& gt = s->gt();
    auto keep_in_order = [&](const std::vector<std::string>& wanted) {
        std::vector<std::string> out;
        for (const std::string& w : wanted)
            if (std::find(entries.begin(), entries.end(), w) != entries.end() &&
                std::find(out.begin(), out.end(), w) == out.end())
                out.push_back(w);
        if (!out.empty()) entries = std::move(out);
    };
    if (page == "ar/select_course") {
        const char* list = game == "time_attack" ? "TIME_ATTACK" : game == "two_player_battle" ? "TWO_PLAYER"
                                                                                               : "SINGLE_RACE";
        keep_in_order(gt.arcade_courses(list));
    } else if (page == "ar/select_class") {
        // The game's class order; Single Race offers no guest garage.
        std::vector<std::string> order = {"class_c", "class_b", "class_a", "class_s", "class_dirt", "home_garage"};
        if (game != "single_race") order.push_back("guest_garage");
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const std::string& e) {
                                         return std::find(order.begin(), order.end(), e) == order.end();
                                     }),
                      entries.end());
        keep_in_order(order);
    } else if ((page == "ar/select_car" || page == "ar/select_car_2p") &&
               (car_class == "home_garage" || car_class == "guest_garage")) {
        // Garage classes: the garage's cars in list order ("hg<i>" / "gg<i>").
        const GtMode* g = car_class == "home_garage" ? &gt : s->guest.get();
        entries.clear();
        if (g)
            for (size_t i = 0; i < g->state().garage.size(); ++i)
                entries.push_back((car_class == "home_garage" ? "hg" : "gg") + std::to_string(i));
    } else if (page == "ar/select_car" && starts(car_class, "class_")) {
        std::vector<std::string> imgs;
        for (const GtData::ArcadeCar* c : gt.arcade_cars(car_class)) imgs.push_back(c->image());
        keep_in_order(imgs);
        // Mod cars have no picture on the page (the showroom draws them): listed by label.
        for (const GtData::ArcadeCar* c : gt.arcade_cars(car_class))
            if (gt.db().mod_car(c->label)) entries.push_back(c->label);
    }
    // (ar/select_car_2p: each player's class row asks for that class's cars as
    // "ar/select_car", GameFlow::two_player_step.)
}

} // namespace gt_menu

bool queue_arcade_trophy(const std::string& race_code) {
    if (!S()) return false;
    std::optional<Show> sh = gt_menu::arcade_trophy_show(race_code);
    if (!sh) return false;
    menu_state().shows.push_back(std::move(*sh));
    return true;
}

} // namespace gt3r::game
