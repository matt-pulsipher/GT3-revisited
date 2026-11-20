#pragma once

// Gran Turismo Concept's menu bindings (concept_menu.cpp; the Concept spec): the game side of the front end in
// menu/flow/concept_flow.cpp. install_concept_menu() registers the ConceptHooks (install_gt_mode_menu calls it); the
// concept_* functions are what GT3's arcade hooks and widget hooks hand over to while a Concept disc's game runs.

#include "game/concept_state.h"
#include "menu/flow/game_hooks.h"
#include "menu/menu_scene.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gt3r::game {

void install_concept_menu();
// The running game is a Gran Turismo Concept disc's (false before the menus opened it).
bool concept_active();
// Its rules' lists (nullptr when not active).
const ConceptRules* concept_rules_now();

// --- While concept_active():
// The car (database label) of a car page entry (its picture's name), "" when unknown.
std::string concept_entry_car(const std::string& entry);
// The course page's entries of a game ("single_race": the courses whose license has a medal), in the list's order.
void concept_arcade_entries(const std::string& page, const std::string& game, const std::string& car_class,
                            std::vector<std::string>& entries);
// The course page's code-filled art (total length).
std::vector<std::pair<std::string, std::string>> concept_course_parts(const std::string& entry);
// A race request's game-side fields (the car's label, a single race's code at its level).
void concept_prepare(menu::flow::GameRequest& r);
// Back from the settings menu, in the race it was opened from: the request's cars as the garage has them now. True
// when a part or a setting changed. False when no Concept game runs or the request has no garage car.
bool concept_refresh_cars(menu::flow::GameRequest& r);
// A race came back: the place or medal is recorded, the prize pass run, its presentations queued, the game saved.
void concept_on_result(const menu::flow::GameResult& r);
// A run of the visit just ended in the race (its result as it stands): when it opens anything the notice was not yet
// shown for in this visit, the page notice_present is queued for the race screen's presentations (concept §8
// step 2) and true is returned. Nothing is recorded: concept_on_result does that when the race screen closes.
bool queue_concept_notice(const menu::flow::GameResult& r);
// The cup of a won single race (page arcade_mini's model), "" when the race has none or no Concept game runs.
std::string concept_trophy_model(const std::string& race_code);
// A license run judged by the race, written to the save at once (as game::save_license_run). False when no Concept
// game runs.
bool concept_save_license_run(const std::string& code, int medal, double seconds);
// The prize car page's car (page concept_car_get), and its name line.
bool concept_car_for_widget(const std::string& page, const std::string& widget_class, std::string& car, int& paint);
std::optional<std::string> concept_widget_text(const std::string& page, const menu::Element& e);

} // namespace gt3r::game
