#pragma once

// Hooks between Gran Turismo Concept's front end (GameFlow's control "concept", concept_flow.cpp) and the game's
// rules and data (game/concept_menu.cpp registers them), as GtModeHooks are for GT Mode: the flow knows the pages,
// the game knows the disc's lists and the player's progress. The pickers shared with GT3's arcade (course and car
// pages) go through GtModeHooks' arcade_* hooks. Every hook is optional.

#include "menu/flow/game_hooks.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace gt3r::menu::flow {

// The title's idle time before the attract sequence: 20 seconds (the session's constant: 1000 frames in the 50 Hz
// European Tokyo-Geneva program, 1200 in the 60 Hz 2001 Tokyo program), in menu steps.
constexpr int kConceptTitleIdleFrames = 20 * 60;

struct ConceptHooks {
    // The attract sequence of the release (concept §3.3), run when the title was left alone for kConceptTitleIdle:
    // "" for a demo race, else a movie's path, in order.
    std::function<std::vector<std::string>()> attract_steps;
    // The category page (concept §5.6): how many cars of category `category` (the page's item index: C, B, A, S,
    // D) can be chosen on `course` (its kind: road or dirt); 0 = the category cannot be chosen. And the car class
    // the category stands for (a game config class list, "CLASS_C").
    std::function<int(int category, const std::string& course)> category_cars;
    std::function<std::string(int category)> category_class;
    // The car page's entries (their pictures' names) for a class on a course: the class's cars of the course's kind
    // that are open, in list order.
    std::function<std::vector<std::string>(const std::string& car_class, const std::string& course)> cars;
    // A dirt course (the category page's rally title, the car page's tire step).
    std::function<bool(const std::string& course)> course_dirt;
    // The course page's place badges: the best finishing place (0 = 1st) on `course` at `level`, 6 or more = none.
    std::function<int(int level, const std::string& course)> course_place;

    // The Course License page (§5.8): its tests in order.
    struct Test {
        std::string code;                 // the race code ("LCeu0003")
        std::string course;               // the course label
        std::string car;                  // the test car's name
        int medal = -1;                   // won so far: -1 none, 0 gold, 1 silver, 2 bronze
        std::array<std::string, 3> times; // gold, silver, bronze target times as text
    };
    std::function<std::vector<Test>()> license_tests;

    // Game Status (§5.9): whether the thing an item stands for is reached ("C03N0", "MOVIE0", ...), the completion
    // figure's text, a node's state (the course's license medal, -1 none; its best place at Normal / Professional).
    std::function<bool(const std::string& item)> status_reached;
    std::function<std::string()> completion_text;
    std::function<int(int course)> license_medal;
    // A course won at some level (`pro` false) / at Professional or Ace (`pro` true): the map's N and H nodes.
    std::function<bool(int course, bool pro)> course_won;
    // The picture of the car a prize key names (the page's hidden thumbnail, the show room's description), "" = none.
    std::function<std::string(const std::string& key)> key_image;
    // The present an item of the page starts: MOVIE0 / MOVIE1 give the movie's path, anything else "".
    std::function<std::string(const std::string& item)> present_movie;
    // The pod race (§4.4, §9.1): its course (the game config's POD_RACE list), its car class (CLASS_PODRACE) and the
    // class's entry (its picture's name); empty fields when the disc has none.
    struct Pod {
        std::string course, car_class, entry;
    };
    std::function<Pod()> pod;
    // The present granted last since this was asked ("" none): Game Status opens on its button.
    std::function<std::string()> last_grant;
    // The Game Status page shows a prize car (set_concept_car: the item's name is its prize key).
    std::function<void(const std::string& key)> set_car;

    // A race is about to be asked for: the game fills what the menus do not know (the race code of the chosen car
    // and level, the license test's event, the car's own label).
    std::function<void(GameRequest& r)> prepare;
};

void set_concept_hooks(ConceptHooks h);
const ConceptHooks& concept_hooks();

} // namespace gt3r::menu::flow
