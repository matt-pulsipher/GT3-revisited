#pragma once

// What GT mode offers, as laid out by the original's menu flow file
// (menu/gt_mode.mbl): the new-car dealers (cd/ pages, one item per car with
// action set_car_code), the race series (r/<tier>/<series> pages: single
// races with set_race_code, championships with set_race_code_champ) and the
// license tests (lc/lc_<license> pages, action go_license). The original
// has no separate list of these: its menus are the list.

#include "formats/menu/mbl.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gt3r::data {
class CarDatabase;
}

namespace gt3r::game {

struct DealerCar {
    std::string region; // dealer region page item: jp, us, gr, fr, it, uk, bel, aus
    std::string dealer; // dealer page, e.g. "cd/cd_eng_tvr"
    std::string item;   // menu item name ("_206_s16_00")
    std::string car;    // database car label (item without the leading '_' when needed)
};

struct RaceSeries {
    std::string tier;   // "beginner", "amateur", "pro", "dirt", "end"
    std::string page;   // e.g. "r/beginner/gsunday_e"
    std::string id;     // the tier page item naming the series, e.g. "Gsunday_e_0001"
    std::vector<std::string> races; // race codes (= event labels) in order
    std::string champ_item;         // "Galtezza_n_0500" when the series has a championship, else ""
    bool championship() const { return !champ_item.empty(); }
};

struct LicenseCourse {
    std::string license;             // "b", "a", "ib", "ia", "s", "d" (rally; tests L0R*)
    std::vector<std::string> tests;  // event labels, e.g. "L0B0001"
};

struct Catalog {
    std::vector<DealerCar> dealers;
    std::vector<RaceSeries> series;
    std::vector<LicenseCourse> licenses;
    std::map<std::string, size_t> series_of_race; // race code -> index into series
    std::map<std::string, std::string> shop_item_of_page; // ts/ part page -> its set_focus_parts item

    // Builds the catalog from gt_mode.mbl. With a database, dealer item
    // names are resolved to car labels that exist.
    static Catalog from_mbl(const menu::MblFile& mbl, const data::CarDatabase* db = nullptr);

    const RaceSeries* series_for(const std::string& race_code) const;
    const LicenseCourse* license(const std::string& id) const;
    const DealerCar* dealer_car(const std::string& item_or_label) const;
};

// The dealer map's country of each car maker: Car::maker_id -> the region page its dealer is on
// ("jp", "us", "gr", "fr", "it", "uk", "bel", "aus"), from the cars the dealers sell. Makers
// with no dealer on the map (tuners without one, the cars with no maker) have none.
std::map<u16, std::string> maker_countries(const Catalog& c, const data::CarDatabase& db);
// Reads them from menu/gt_mode.mbl as the menus see it (mod makers included) into `db`
// (CarDatabase::set_maker_countries: the driver's side of a car whose body shows none). False
// when the disc has no such file.
bool load_maker_countries(const std::function<std::optional<Bytes>(const std::string&)>& read, data::CarDatabase& db);

} // namespace gt3r::game
