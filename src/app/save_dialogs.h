#pragma once

namespace gt3r {

class App;

// Registers the platform file dialogs the menus' save page and Settings -> Cameras use
// (menu::flow::set_file_dialogs).
void install_save_dialogs(App& app);

} // namespace gt3r
