#include "app/opponent_grid.h"

#include "game/data/car_database.h"
#include "sim/exact/race_inputs.h"

#include <cstdlib>

namespace gt3r {

const char* OpponentGrid::skill_name(int skill, bool concept_levels) {
    static const char* const kNames[kSkills] = {"Easy", "Normal", "Hard", "Professional"};
    static const char* const kConcept[kConceptSkills] = {"Normal", "Professional", "Ace"};
    if (concept_levels) return kConcept[std::clamp(skill, 0, kConceptSkills - 1)];
    return kNames[std::clamp(skill, 0, kSkills - 1)];
}

void OpponentGrid::set_concept_levels(bool on) {
    concept_ = on;
    for (Car& c : cars_) c.skill = std::clamp(c.skill, 0, skills() - 1);
}

std::string OpponentGrid::skill_race(int skill, bool dirt) {
    static const char kLevel[] = "ENHP";
    return std::string("A") + kLevel[std::clamp(skill, 0, kSkills - 1)] + (dirt ? "AD" : "ST") + "0001";
}

std::optional<std::array<u8, 16>> OpponentGrid::skill_driver(const data::CarDatabase& db, int skill, bool dirt) {
    const data::Event* ev = db.event(skill_race(skill, dirt));
    if (!ev || ev->bytes.size() < 0x1F4) return std::nullopt;
    return sim::exact::driver_overrides(sim::exact::race_config(ev->bytes));
}

std::string OpponentGrid::ordinal(int n) {
    const char* suf = (n % 100 >= 11 && n % 100 <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    return std::to_string(n) + suf;
}

void OpponentGrid::attach(const std::vector<PickerCar>& cars, PickerArt& art, CarPicker& picker,
                          std::function<int(const std::string&)> find) {
    list_ = &cars, art_ = &art, picker_ = &picker, find_ = std::move(find);
}

OpponentGrid::Car OpponentGrid::parse(const std::string& text) {
    Car c;
    const size_t a = text.find(':'), b = a == std::string::npos ? a : text.find(':', a + 1);
    c.label = text.substr(0, a);
    if (a != std::string::npos) c.paint = std::max(0, std::atoi(text.c_str() + a + 1));
    if (b != std::string::npos) c.skill = std::clamp(std::atoi(text.c_str() + b + 1), 0, kSkills - 1);
    return c;
}

void OpponentGrid::read(const IniDocument& cfg, const std::string& section) {
    count_ = std::clamp(std::atoi(cfg.get(section, "opponents").value_or("0").c_str()), 0, kMaxCars);
    place_ = place_in(cfg, section);
    for (int k = 0; k < kMaxCars; ++k)
        set_place(k, parse(cfg.get(section, "opponent" + std::to_string(k + 1)).value_or("")));
}

void OpponentGrid::write(IniDocument& cfg, const std::string& section) const {
    cfg.set(section, "opponents", std::to_string(count_));
    cfg.set(section, "place", std::to_string(place_));
    for (int k = 0; k < kMaxCars; ++k)
        if (const Car& c = cars_[size_t(k)]; c.car >= 0)
            cfg.set(section, "opponent" + std::to_string(k + 1),
                    c.label + ":" + std::to_string(c.paint) + ":" + std::to_string(c.skill));
}

std::vector<std::string> OpponentGrid::labels(const IniDocument& cfg, const std::string& section) {
    std::vector<std::string> out;
    const int n = std::clamp(std::atoi(cfg.get(section, "opponents").value_or("0").c_str()), 0, kMaxCars);
    for (int k = 0; k < n; ++k) out.push_back(parse(cfg.get(section, "opponent" + std::to_string(k + 1)).value_or("")).label);
    return out;
}

int OpponentGrid::place_in(const IniDocument& cfg, const std::string& section) {
    return std::clamp(std::atoi(cfg.get(section, "place").value_or("5").c_str()), 0, kMaxCars);
}

void OpponentGrid::fill(App& app, const std::vector<std::string>& entries, int fallback, int count) {
    if (count_ <= 0) count_ = std::clamp(count, min_, kMaxCars);
    for (int k = 0; k < kMaxCars; ++k) {
        if (cars_[size_t(k)].car >= 0) continue;
        Car c;
        if (!entries.empty()) c.label = entries[size_t(k) % entries.size()];
        set_place(k, c);
        if (cars_[size_t(k)].car < 0) { // (an entry not among the cars)
            c.label = car_key((*list_)[size_t(fallback)]);
            set_place(k, c);
        }
        cars_[size_t(k)].paint = k % paint_count(app, cars_[size_t(k)].car);
    }
}

void OpponentGrid::set_place(int k, Car c) {
    c.car = c.label.empty() ? -1 : find_(c.label);
    c.skill = std::clamp(c.skill, 0, skills() - 1);
    cars_[size_t(k)] = std::move(c);
}

void OpponentGrid::set_car(int k, int car, int paint) {
    Car& c = cars_[size_t(k)];
    c.car = car;
    c.label = car_key((*list_)[size_t(car)]);
    c.paint = paint;
}

int OpponentGrid::shown() const { return limit_ > 0 ? std::clamp(count_, min_, limit_) : 0; }

int OpponentGrid::paint_count(App& app, int car) const {
    if (car < 0 || size_t(car) >= list_->size()) return 1;
    return std::max(1, int(picker_->paints(app, (*list_)[size_t(car)]).size()));
}

int OpponentGrid::who(int row) const {
    if (row < kLineup) return kNobody;
    const int at = row - kLineup, place = player_place();
    return at == place ? kPlayer : at < place ? at : at - 1;
}

int OpponentGrid::action(int row, int h, bool cross) {
    if (limit_ <= 0 || read_only_) return kNobody;
    const int n = shown(), step = h < 0 ? -1 : 1;
    if (row == kCountRow) {
        const int span = limit_ - min_ + 1;
        if (h) count_ = (n - min_ + step + span) % span + min_;
        return kNobody;
    }
    if (row == kPlaceRow) {
        if (h) place_ = (player_place() + step + n + 1) % (n + 1);
        return kNobody;
    }
    if (row == kSkillRow) {
        const int skill = (cars_[0].skill + step + skills()) % skills();
        for (Car& c : cars_) c.skill = skill;
        return kNobody;
    }
    const int k = who(row);
    if (cross) return k;
    if (k != kPlayer) cars_[size_t(k)].skill = (cars_[size_t(k)].skill + step + skills()) % skills();
    return kNobody;
}

void OpponentGrid::render_row(App& app, Ui& ui, int row, const UiRect& r, bool focused, const Hits& h) {
    const int n = shown();
    if (focused) ui.fill_round(r.x, r.y, r.w, r.h - 2, 4, colors::kHighlight);
    h.hits.add({r.x, r.y, r.w, r.h - 2}, h.row, h.arg);
    if (row < kLineup) {
        std::string label = count_text_, value = n == 0 ? "None" : std::to_string(n);
        if (row == kPlaceRow) {
            label = place_text_, value = ordinal(player_place() + 1) + " of " + std::to_string(n + 1);
            if (!players_text_.empty()) { // (a block of players: where it starts among the CPU cars)
                const int ahead = player_place();
                value = ahead == 0 ? "On pole" : "Behind " + std::to_string(ahead) + (ahead == 1 ? " car" : " cars");
            }
        }
        if (row == kSkillRow) {
            const int skill = cars_[0].skill;
            const bool same = std::all_of(cars_.begin(), cars_.begin() + n, [&](const Car& c) { return c.skill == skill; });
            label = "Skill for all", value = same ? skill_name(skill, concept_) : "Mixed";
        }
        ui.text(r.x + 6, r.y + (r.h - 2 - Ui::kChar) / 2, label, focused ? colors::kAccent : colors::kText);
        if (read_only_) ui.text_right(r.right() - 10, r.y + (r.h - 2 - Ui::kChar) / 2, value, colors::kText);
        else value_arrows(ui, {r.right() - 156, r.y + 1, 152, r.h - 4}, value, colors::kText, h.hits, h.left, h.right, h.arg, h.pointer);
        return;
    }
    const int k = who(row);
    const bool you = k == kPlayer;
    if (you && !players_text_.empty()) { // (the players' place: no car of its own)
        if (!focused) ui.fill_round(r.x, r.y, r.w, r.h - 2, 4, colors::kPanel);
        ui.text(r.x + 6, r.y + 12, std::to_string(row - kLineup + 1), focused ? colors::kAccent : colors::kDim);
        ui.text(r.x + 84, r.y + 12, Ui::clip(players_text_, r.right() - r.x - 90), colors::kAccent);
        return;
    }
    const int car = you ? player_car_ : cars_[size_t(k)].car, paint = you ? player_paint_ : cars_[size_t(k)].paint;
    auto skill_value = [&] {
        if (you) return;
        const char* v = skill_name(cars_[size_t(k)].skill, concept_);
        if (read_only_) ui.text_right(r.right() - 10, r.y + 12, v, colors::kText);
        else value_arrows(ui, {r.right() - 156, r.y + 4, 152, r.h - 10}, v, colors::kText, h.hits, h.left, h.right, h.arg, h.pointer);
    };
    if (car < 0 || size_t(car) >= list_->size()) { // (a car this machine's list lacks: its label)
        ui.text(r.x + 6, r.y + 12, std::to_string(row - kLineup + 1), focused ? colors::kAccent : colors::kDim);
        ui.text(r.x + 84, r.y + 12, Ui::clip(you ? "You" : cars_[size_t(k)].label, r.right() - r.x - 250), colors::kDim);
        skill_value();
        return;
    }
    const PickerCar& c = (*list_)[size_t(car)];
    if (you && !focused) ui.fill_round(r.x, r.y, r.w, r.h - 2, 4, colors::kPanel);
    ui.text(r.x + 6, r.y + 12, std::to_string(row - kLineup + 1), focused ? colors::kAccent : colors::kDim);
    art_->car_icon(app, car_icon_name(c), SDL_FRect{r.x + 20, r.y + 2, 58, 31});
    const float tx = r.x + 84, room = (you ? r.right() - 6 : r.right() - 160) - tx;
    ui.text(tx, r.y + 4, Ui::clip(you ? "You: " + c.name : c.name, room), focused || you ? colors::kAccent : colors::kText);
    std::vector<PaintInfo> pal = picker_->paints(app, c);
    if (pal.size() > 1) { // (a race car has its one livery)
        const PaintInfo& p = pal[size_t(std::clamp(paint, 0, int(pal.size()) - 1))];
        draw_paint_chip(ui, tx, r.y + 18, 18, 9, p.rgb, false);
        ui.text(tx + 23, r.y + 18, Ui::clip(p.name, room - 23), colors::kDim);
    }
    skill_value();
}

} // namespace gt3r
