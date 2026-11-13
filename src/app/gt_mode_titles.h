#pragma once

// How our own screens (Quick race, Save editor) name GT Mode's races and licence tests: the
// titles the game's menus show for them, the leagues in their menus' order.

#include "game/gt_catalog.h"

#include <map>
#include <string>
#include <vector>

namespace gt3r {

class Vfs;
namespace data {
class CarDatabase;
}

// The titles the GT Mode menus show for their items: every menu/gt_mode.mbl item with a
// focus layer (MblItem::str80) whose layout (the page in menu/<lang>/gt_mode<N>.pmb, that layer)
// holds a title text - the league pages' "event_text" widgets ("Sunday Cup", "Seattle 100
// Miles") and the licence pages' "license_title<n>" ("Practical Cornering I"). Keyed by the
// page the item opens (a series page, "r/beginner/gsunday_e"), else by the item (a test,
// "L0B0001"). ASCII (the UI font's).
std::map<std::string, std::string> game_menu_titles(const Vfs& vfs, const std::string& lang);

// The leagues as the GT Mode menus list them: the catalog's tier and its title.
struct League {
    const char* tier;  // RaceSeries::tier
    const char* title; // "Beginner League"
};
// Custom Events: the league mod events go to when the disc lacks theirs (game/gt_mode_events_menu.cpp).
constexpr League kLeagues[] = {{"beginner", "Beginner League"}, {"amateur", "Amateur League"},
                               {"pro", "Professional League"},  {"dirt", "Rally Events"},
                               {"end", "Endurance Races"},      {"custom", "Custom Events"}};
// A league page's title ("r/beginner/beginner" -> "Beginner League"), by its folder (the tier);
// another tier capitalised.
std::string league_title(const std::string& page);

// The titles of mod events' series (data::ModEventDef::title, keyed by their pages as
// game_menu_titles keys the disc's) added to `titles`.
void add_mod_event_titles(std::map<std::string, std::string>& titles, const game::Catalog& cat, const data::CarDatabase& db);

// The catalog's series in the order of their league pages (gt_mode.mbl), then any the pages do
// not name.
std::vector<const game::RaceSeries*> ordered_series(const Vfs& vfs, const game::Catalog& cat);

// A licence test's short name: "B-1", "IA-8" (the code "LIA0008"); the code when it is not one.
std::string license_test_label(const std::string& code);

} // namespace gt3r
