#include "game/gt_catalog.h"

#include "base/log.h"
#include "game/data/car_database.h"
#include "menu/flow/menu_set.h"

namespace gt3r::game {

namespace {

bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool ends(const std::string& s, const char* p) {
    std::string_view v(p);
    return s.size() >= v.size() && s.compare(s.size() - v.size(), v.size(), v) == 0;
}

// "r/<tier>/<series>": every r/ page that is neither the tier page nor an entry page.
bool split_series(const std::string& page, std::string& tier) {
    if (!starts(page, "r/") || ends(page, "_entry")) return false;
    size_t a = page.find('/', 2);
    if (a == std::string::npos) return false;
    tier = page.substr(2, a - 2);
    std::string rest = page.substr(a + 1);
    return rest != tier && rest != "pro" && rest != "endurance" && rest != "dirt" && rest != "beginner" &&
           rest != "amateur";
}

} // namespace

Catalog Catalog::from_mbl(const menu::MblFile& mbl, const data::CarDatabase* db) {
    Catalog c;
    // Region of each dealer page: the region page ("cd/cd_eng") lists the dealers.
    std::map<std::string, std::string> region_of_page;
    if (int root = mbl.find_page("cd/cd"); root >= 0)
        for (const menu::MblItem& r : mbl.pages[size_t(root)].items)
            if (int rp = mbl.find_page(r.link); rp >= 0)
                for (const menu::MblItem& d : mbl.pages[size_t(rp)].items) region_of_page[d.link] = r.name;
    // Tier page item naming each series page.
    std::map<std::string, std::string> series_id;
    for (const menu::MblPage& p : mbl.pages)
        if (starts(p.name, "r/"))
            for (const menu::MblItem& it : p.items)
                if (it.action.empty() && starts(it.link, "r/")) series_id[it.link] = it.name;

    for (const menu::MblPage& p : mbl.pages) {
        if (starts(p.name, "cd/")) {
            for (const menu::MblItem& it : p.items) {
                if (it.action != "set_car_code") continue;
                DealerCar d;
                d.dealer = p.name;
                d.region = region_of_page.count(p.name) ? region_of_page[p.name] : std::string();
                d.item = it.name;
                d.car = it.name;
                if (db && !db->car(d.car) && !d.car.empty() && d.car[0] == '_' && db->car(d.car.substr(1)))
                    d.car = d.car.substr(1);
                else if (!db && !d.car.empty() && d.car[0] == '_')
                    d.car = d.car.substr(1);
                c.dealers.push_back(std::move(d));
            }
        }
        for (const menu::MblItem& it : p.items)
            if (it.action == "set_focus_parts" && !c.shop_item_of_page.count(p.name)) c.shop_item_of_page[p.name] = it.name;
        std::string tier;
        if (split_series(p.name, tier)) {
            RaceSeries s;
            s.tier = tier;
            s.page = p.name;
            s.id = series_id.count(p.name) ? series_id[p.name] : std::string();
            for (const menu::MblItem& it : p.items) {
                if (it.action == "set_race_code") s.races.push_back(it.name);
                else if (it.action == "set_race_code_champ") s.champ_item = it.name;
            }
            if (s.races.empty()) continue;
            for (const std::string& r : s.races) c.series_of_race[r] = c.series.size();
            c.series.push_back(std::move(s));
        }
        if (starts(p.name, "lc/lc_")) {
            LicenseCourse l;
            l.license = p.name.substr(6);
            for (const menu::MblItem& it : p.items)
                if (it.action == "go_license") l.tests.push_back(it.name);
            if (!l.tests.empty()) c.licenses.push_back(std::move(l));
        }
    }
    return c;
}

const RaceSeries* Catalog::series_for(const std::string& race_code) const {
    auto it = series_of_race.find(race_code);
    return it == series_of_race.end() ? nullptr : &series[it->second];
}

const LicenseCourse* Catalog::license(const std::string& id) const {
    for (const LicenseCourse& l : licenses)
        if (l.license == id) return &l;
    return nullptr;
}

const DealerCar* Catalog::dealer_car(const std::string& s) const {
    for (const DealerCar& d : dealers)
        if (d.item == s || d.car == s) return &d;
    return nullptr;
}

std::map<u16, std::string> maker_countries(const Catalog& c, const data::CarDatabase& db) {
    std::map<u16, std::string> out;
    for (const DealerCar& d : c.dealers) {
        if (d.region.empty()) continue; // (a dealer page no region page lists)
        if (const data::Car* car = db.car(d.car)) out.emplace(car->maker_id, d.region);
    }
    return out;
}

bool load_maker_countries(const std::function<std::optional<Bytes>(const std::string&)>& read, data::CarDatabase& db) {
    const std::optional<Bytes> mbl = read("menu/gt_mode.mbl");
    if (!mbl) return false;
    try {
        menu::MblFile flow = menu::MblFile::parse(*mbl);
        if (const auto& patch = menu::flow::menu_patches().flow) patch("gt_mode", read, flow);
        db.set_maker_countries(maker_countries(Catalog::from_mbl(flow, &db), db));
    } catch (const std::exception& e) {
        LOG_INFO("gt mode: menu/gt_mode.mbl: %s", e.what());
        return false;
    }
    return true;
}

} // namespace gt3r::game
