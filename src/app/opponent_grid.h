#pragma once

// The lineup of a race the player sets up: one to five CPU cars, each any car in any of its colours with a skill, and the
// player's own place among them, pole first. Rows for the page that shows them (Quick race's custom race): the number of
// opponents, the player's place, every opponent's skill, then the lineup. The page keeps the cursor and the car page; action()
// says whose car is to be chosen. The page gives the grid the cars, and the limit its course's start grid sets (0: no room, no rows).
//
// A skill is one of the arcade's difficulties (skill_name): what it does to a car's driver is up to the race (the arcade race
// of that difficulty's driver overrides, GameRequest::opponent_skills).

#include "app/online_pickers.h"
#include "base/config.h"

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace gt3r {

class OpponentGrid {
public:
    static constexpr int kMaxCars = 5; // (a race has six cars, the player's included)
    static constexpr int kPlayer = kMaxCars; // who: the player, as against opponents 0 .. kMaxCars - 1
    static constexpr int kNobody = -1;
    static constexpr int kSkills = 4;
    static constexpr int kDefaultSkill = 1;
    static constexpr float kRowH = 22, kCarH = 37;
    static const char* skill_name(int skill); // "Easy" .. "Professional"
    static std::string ordinal(int n);        // "1st", "2nd", ...

    struct Car {
        std::string label; // the car it races as: a car, or an event's entry with its own parts
        int car = -1;      // its car on the car page (index into the list), -1 = an empty place
        int paint = 0, skill = kDefaultSkill;
    };

    // `cars` (the car page's list), `art` and `picker` (the cars' colours) must outlive the grid. `find`: a car's index in `cars`
    // from its label or an entry's, -1 = none.
    void attach(const std::vector<PickerCar>& cars, PickerArt& art, CarPicker& picker, std::function<int(const std::string&)> find);

    // In config.ini, among a section's keys: opponents = <n>, opponent<k> = <car>:<paint>:<skill>, place = <player's, 0 pole>.
    void read(const IniDocument& cfg, const std::string& section);
    void write(IniDocument& cfg, const std::string& section) const;
    // The labels a section's lineup holds, as list pages show it, and the player's place (no attach needed).
    static std::vector<std::string> labels(const IniDocument& cfg, const std::string& section);
    static int place_in(const IniDocument& cfg, const std::string& section);
    // "<car>:<paint>:<skill>".
    static Car parse(const std::string& text);

    // The places not chosen yet: `entries` in turn (none: the car at `fallback`), each with a colour of its own; `count`
    // opponents when no number was chosen yet.
    void fill(App& app, const std::vector<std::string>& entries, int fallback, int count);
    void set_limit(int limit) { limit_ = std::clamp(limit, 0, kMaxCars); } // 0: no room, no rows
    int limit() const { return limit_; }
    void set_count(int n) { count_ = n; }
    void set_place(int k, Car c); // (finds its car)
    void set_car(int k, int car, int paint); // from the car page
    void set_player(int car, int paint) { player_car_ = car, player_paint_ = paint; } // the car its row shows
    void set_player_place(int place) { place_ = std::clamp(place, 0, kMaxCars); }

    int shown() const; // the opponents racing: those chosen, as many as the limit allows (0 only with no room)
    int player_place() const { return std::min(place_, shown()); } // 0 pole; `shown()` last
    const Car& car(int k) const { return cars_[size_t(k)]; }
    int paint_count(App& app, int car) const;

    // The rows: the number of opponents, the player's place, every opponent's skill, the lineup. (The place is a row of its own:
    // on the player's lineup row its arrows would move away under the finger.)
    static constexpr int kCountRow = 0, kPlaceRow = 1, kSkillRow = 2, kLineup = 3;
    int rows() const { return limit_ > 0 ? shown() + kLineup + 1 : 0; }
    int who(int row) const;     // an opponent's, kPlayer, or kNobody for the rows above the lineup
    float row_h(int row) const { return row >= kLineup ? kCarH : kRowH; }
    // Cross (`cross`) or left / right (`h`) on a row: whose car to choose (who()), else kNobody (the grid changed itself).
    int action(int row, int h, bool cross);
    // The ids a row's parts go to the page's `hits` with, the row's as `row` and its arrows as `left` and `right`, each with `arg`.
    struct Hits {
        HitList& hits;
        const PointerInput& pointer;
        int row, left, right, arg;
    };
    void render_row(App& app, Ui& ui, int row, const UiRect& r, bool focused, const Hits& h);

private:
    std::array<Car, kMaxCars> cars_;
    int count_ = 0, limit_ = kMaxCars, place_ = kMaxCars;
    int player_car_ = 0, player_paint_ = 0;
    const std::vector<PickerCar>* list_ = nullptr;
    PickerArt* art_ = nullptr;
    CarPicker* picker_ = nullptr;
    std::function<int(const std::string&)> find_;
};

} // namespace gt3r
