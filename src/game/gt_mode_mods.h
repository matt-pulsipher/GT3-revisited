#pragma once

// Mod content in the GT3 menus, as menu content patches (menu/flow/menu_set.h MenuPatches):
// mod cars and makers at the dealers and in the arcade (gt_mode_mods.cpp), mod events on the
// league pages (gt_mode_events_menu.cpp). The helpers below are shared by both.

#include "game/data/car_database.h"
#include "menu/flow/menu_set.h"

#include <string>
#include <vector>

namespace gt3r::game {

void install_mod_menu_patches(); // (gt_mode_menu/install.cpp)
void forget_mod_menu_plan();     // (gt_mode_menu/common.cpp: an updated mod can keep its file names)

// Mod events (gt_mode_events_menu.cpp): the flow patch adds each event's series and entry pages
// and its item on a league page; the layout patch gives them the event's picture and texts.
void patch_event_flow(const menu::flow::FileReader& read, menu::MblFile& mbl);
void patch_event_layout(const menu::flow::FileReader& read, menu::MenuScene& scene, const menu::MblFile& mbl);

// The leagues of a disc's GT Mode menus, from the flow itself: the pages the series pages (items
// with set_race_code) go back to, in the race map's order (the map: the page they go back to,
// into *map). The Event creator offers them; an event names one (data::ModEventDef::league).
std::vector<std::string> league_pages(const menu::MblFile& mbl, std::string* map = nullptr);
// The page the Custom Events league gets (an event naming no league of the disc goes there).
extern const char* const kCustomLeaguePage;

namespace mod_menu {

bool starts(const std::string& s, const char* p);
std::string lower(std::string s);
// The database with the enabled mods' cars and events, loaded through the set's reader (cached
// per mod list and region); null without mods.
const data::CarDatabase* database(const menu::flow::FileReader& read);
// A mod picture (PNG at `path`) as a scene image; -1 when the file is missing or broken.
int add_picture(menu::MenuScene& scene, const std::string& path);
// Points an image element (and its focused look) at a picture; -1 leaves it showing nothing.
void set_picture(menu::Element& e, int image);
// Turns an image element into `name` in the menu font, centred in its box.
void set_name_text(menu::Element& e, const std::string& name);
void set_text(menu::Element& e, const std::string& text);
menu::Layer* find_layer(menu::Page& p, const std::string& name);
const menu::Element* find_item(const menu::Page& p, const std::string& name);
menu::Page* layout_page(menu::MenuScene& scene, const std::string& name);

} // namespace mod_menu

} // namespace gt3r::game
