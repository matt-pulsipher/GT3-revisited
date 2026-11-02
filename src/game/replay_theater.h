#pragma once

// The Replay Theater's files for the menus (menu::flow::TheaterBackend): the saved replays
// in <user data>/replays/ and the disc's demonstration runs (demo/<event>), each with the
// info panel's texts (course, car, power, record, laps, mode, date, size, file name).

#include "menu/flow/theater_page.h"

#include <string>

namespace gt3r {
class Vfs;
}

namespace gt3r::game {

// Installs the backend. `save_dir`: the save folder (App::save_folder(), game/save_folder.h);
// `vfs`: the disc (car / course names and the demonstrations; must outlive the menus).
void install_replay_theater(std::string save_dir, const Vfs* vfs);

// The entries without the menus (tests): kind 0 replays in `replay_dir`, 1 demonstrations.
std::vector<menu::flow::TheaterEntry> replay_theater_entries(int kind, const std::string& replay_dir, const Vfs* vfs);

// "License B-1" / "B1_demo" for L0B0001, "Time Trial 1" / "TT1_demo" for AT010001.
std::string demonstration_mode(const std::string& code);
std::string demonstration_file(const std::string& code);
// The demonstrations in the original's list order: licences B, A, IB, IA, S, R (1..8 each),
// then the arcade time trials 1..10.
std::vector<std::string> demonstration_codes();

} // namespace gt3r::game
