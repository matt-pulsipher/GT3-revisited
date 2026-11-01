#pragma once

// The top-level menu flow of GT3 (US): what the original's main loop and
// per-mode menu code (GT mode, arcade, options) do around
// the page manager. Starts at the title menu (gt_mode.mbl page "root"),
// enters arcade / GT mode / options, walks the arcade set-up screens and
// raises a GameRequest where the original starts a race. Code-drawn pages
// (course / class / car selection) are reproduced as simple carousels over
// the pages' own "hidden" art.
//
// SDL-free and frame based like MenuController.

#include "menu/flow/game_hooks.h"
#include "menu/flow/menu_controller.h"
#include "menu/flow/music_list_page.h"
#include "menu/flow/options.h"
#include "menu/flow/save_page.h"
#include "menu/flow/screen_transition.h"
#include "menu/flow/text_box.h"
#include "menu/flow/theater_page.h"
#include "race/custom_music.h"

#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r::menu::flow {

// The title's focus (an item index of gt_mode.mbl page "root") when the title comes back
// after the title command `command` ran (menus §7.1).
// Arcade 0, Replay Theater 2, Load 3, Save 4, File operation 5, Options 6; anything else
// (the theater's demonstrations and Analyzer, races, the attract demos, boot) 0.
int title_focus_after(std::string_view command);

// The music a top-level page asks for (music §4.1, §4.2): a music.inf name, "bgm:<id>"
// (or "bgm:<id>:once") for streamed music, "" for silence, or "\x01" to keep the current
// music. `set` is the menu set (control) the page belongs to.
std::string page_music(const std::string& set, const std::string& page);

// The GT-mode control's focus hooks of the race pages (menus §4.2). The stored race code is
// the game's one "current race" string (set_race_code and its variants, go_license,
// go_machine_test, go_free_run, arcade races); its type is 4 for set_race_code, 2 for
// set_race_code_champ, 3 for set_race_code_spot.
enum class RacePageHook : u8 {
    None,
    License,            // lc/lc_a, b, d, ia, ib, s: the code's last digit - 1
    Series,             // a race series page: the code's last digit
    ChampionshipSeries, // a series page with a championship item: the last two digits (+ 1)
};
// The hook of a page, from its name and items: a licence page (lc/lc_<x> with go_license
// items) or a race series page (r/<class>/<series>, not an _entry page, with set_race_code
// items). Hooked page names are lower case: the nine endurance series pages spelt with a
// capital E (r/end/Eakasaka_n, ...) get no hook (menus §4.2).
RacePageHook race_page_hook(std::string_view page, const std::vector<MblItem>& items);
// The item index a hook gives for the stored race code (0 when the digits it reads are missing
// or give a negative index; a licence index above 7 gives 0; the list clamps a large one).
int race_page_focus(RacePageHook hook, std::string_view code, int race_type);

class GameFlow {
public:
    GameFlow(FileReader reader, std::string lang);

    // Boot: the title menu, focus on the first item.
    void start();
    // Starts directly in another control (e.g. "setting" / "root": the car
    // settings menu opened from a race's quick menu). exited() turns true when
    // that control is left from its top page.
    void start(const std::string& set, const std::string& page);
    // Starts on the save page alone in Save game mode (a race's quick menu "Save game",
    // with game::begin_race_game_save's snapshot); exited() turns true when the page ends.
    void start_save_game();
    bool exited() const { return exited_; }
    // The language page (start("language", "root"), PAL's first start): the chosen language's
    // code ("BR", "FR", "IT", "GR", "SP") once an item is confirmed; the flow has then exited.
    std::optional<std::string> take_language() { return std::exchange(language_choice_, std::nullopt); }
    void tick(MenuKeys held);
    // Touch: a tap at a point in menu layout coordinates (640x448, x right, y up
    // from the bottom-left). Tapping an item focuses it, tapping the focused
    // item confirms it; on code-drawn pickers (arcade course/car carousels) an
    // icon of the strip is chosen the same way, elsewhere the left / right
    // thirds step the entry and the middle confirms; the save page takes its own
    // (SavePage::tap). Returns true when the tap was used (the caller should then
    // not treat it as a key).
    bool tap(float x, float y);
    // A hovering pointer (mouse, VR controller) at a point: focuses the item under it where
    // items stay put (MenuController::hover, the GT Mode garage rows), so a click confirms.
    bool hover(float x, float y);
    // Ours: a drag or the mouse wheel at a point, `dy` layout units (y up): scrolls the music
    // list under it (MusicListPage::scroll_by). False where nothing scrolls.
    bool scroll(float x, float y, float dy);
    // Ours: the pointer's back (a right click), Cancel for one frame like a tap's key.
    void back() {
        if (ctl_ && !switching() && !waiting_game_) injected_ |= key_bit(MenuKey::Cancel);
    }

    std::vector<ElementDraw> draw_list() const;
    // Text width in menu units for a pmb font spec ("sw_20") at its own size: places the
    // name entry's caret after the text. Without it, half the font size per character.
    std::function<float(const std::string& font, const std::string& text)> measure_text;
    std::vector<std::string> take_sounds();
    std::optional<GameRequest> take_request();
    // Menu background music change: a music.inf sequence name, or "" to stop.
    std::optional<std::string> take_music();
    // The Options music list's preview (menus §10.1, after take_music): a race song's track
    // index in its group (bgm::kRace; its intro, looping, level 1.0, not gated by the Menu
    // BGM option), or -1 to stop the music. Ours: or one of the player's songs (`file`, a host
    // path), from its start, looping, at the same level.
    struct MusicPreview {
        int track = -1;
        std::string file;
    };
    std::optional<MusicPreview> take_preview() { return std::exchange(preview_request_, std::nullopt); }
    // Background movie for "mpeg" widgets: a VOL path, or "" to stop.
    std::optional<std::string> take_movie();
    // A full-screen transition the screen should start now over the last
    // frame (the original's loading transition while the next mode loads).
    std::optional<TransitionType> take_transition();
    // True while switching controls: the screen keeps the frozen frame and
    // draws the transition instead of the menu.
    bool switching() const { return switch_frames_ > 0; }
    // Gameplay started by a request has returned: reopen the menu.
    void game_finished();
    bool waiting_for_game() const { return waiting_game_; }
    // Messages for actions this reimplementation does not support yet
    // (memory card, replays, movies...). Unknown actions are logged too.
    std::vector<std::string> take_log();
    // The game options (title Options menu): read from / written to [options] of this
    // document; `changed` runs after every change (persist and apply).
    void set_options(IniDocument* cfg, std::function<void()> changed) {
        options_ = cfg;
        options_changed_ = std::move(changed);
    }
    // Ours: the player's own music (race/custom_music.h), for the Options music list's tabs
    // Original / Custom / Both ([options] music_source, music_mix). Null: the disc's list only.
    void set_music_library(race::MusicLibrary* lib) { music_lib_ = lib; }
    // Ours: the Key Configuration page (option "key/key") shows a "Controller Settings" button in
    // place of the original's DualShock 2 table; it was chosen (Confirm, or a tap on it). The
    // screen opens our controller settings over the menus, which stay on the page and carry on
    // when it closes. True once per choice.
    bool take_controller_settings() { return std::exchange(controller_settings_, false); }

    const std::string& control() const { return control_; }
    // True for the GT-mode control and its prize / license presentations
    // (the GtModeHooks apply).
    // Runs the GT-mode presentations queued so far (name entry, licence card, prize car,
    // trophies) on their own, e.g. from the race screen; exited() once the last one closes.
    // False (and exited) when nothing is queued.
    bool start_presentations();
    bool gt_active() const {
        return control_ == "gt_mode" || control_ == "setting" || control_ == "records" || presenting_;
    }
    // Attract mode (title idle -> demo races -> opening movie) is running.
    bool attract() const { return in_attract_; }
    MenuController* controller() { return ctl_.get(); }
    const MenuController* controller() const { return ctl_.get(); }
    const MenuSet* set() const { return set_; }
    const std::string& lang() const { return lang_; }

    // Code-drawn selection page state (arcade course/class/car pickers).
    // The original's layout: a strip of
    // `<entry>_mini` icons (every available entry, centred or scrolled so the
    // focused one is centred, the focused icon pulsing) and a sliding "cross"
    // (`cross_bar`) that carries the entry's art 680 px sideways on each move.
    struct Carousel {
        std::string page;
        std::vector<std::string> entries;
        int index = 0;
        bool wrap = false;       // class page wraps; course / car / time trial clamp
        float scroll = 0;        // strip scroll (screen x of the first icon's left edge, minus the strip x)
        float pulse = 1;         // focused icon brightness (PS2 units, 0.502 = normal)
        float cross_x = 0, cross_v = 0, cross_target = 0; // cross position (screen x), spring
        int age = 0;             // frames since the current entry's art appeared (24-frame fade-in)
        int rest = 0;            // frames since the last move (background movie after 6)
        int frames = 0;          // frames since the page opened (breadcrumb slide-in)
        // Car page steps: 1 car and paint, 2 AUTOMATIC / MANUAL, 3 RACING / DRIFT. The page
        // springs up (oy, screen px) so the step's button row sits in the cross band.
        int step = 1;
        float oy = 0, oy_v = 0;
        float strip_alpha = 1;
        bool has_cross = false;  // the page has a cross_bar (the 2 player car page has none)
        float paint_pos = 0;     // colour column position (eases towards the paint index)
        float blur = 0;          // background blur radius (px, the car page steps)
        float art_offset = 0;    // the current art's offset from its layout place at cross_target
        struct Old {
            std::string id;
            float offset;        // as art_offset
            float alpha;
            int frames;          // frames left (16)
        };
        std::vector<Old> old;
    };
    const Carousel* carousel() const { return carousel_.entries.empty() ? nullptr : &carousel_; }
    // The Options music list page (option page "music/music"), while it is open.
    const MusicListPage* music_list() const { return music_page_.is_open() ? &music_page_ : nullptr; }
    int car_paint() const { return car_paint_; }
    // Player 2's pad (2 Player Battle car page), set before tick().
    void set_player2_keys(MenuKeys held) { held2_ = held; }
    // 2 Player car page (ar/select_car_2p): each player's selection, run on its own pad.
    // States: 2 class row, 4 car + paint, 6 AT / MT,
    // 8 RACING / DRIFT, 9 ready.
    struct TwoPlayer {
        int state = 2;
        int cls = 0;                      // index into `classes`
        std::vector<std::string> cars;    // the chosen class's open cars
        int car = 0, paint = 0, transmission = 0, tire_mode = 0;
        int age = 0;                      // frames since the current car / class appeared
        float scroll = 0, pulse = 1;      // the row's mini cursor
        float paint_pos = 0;
        MenuKeys prev = 0;                // key repeat
        int repeat = 0;
    };
    const TwoPlayer* two_player(int p) const { return two_page_ ? &two_[p] : nullptr; }
    static const std::vector<std::string>& two_player_classes();

    // Debugging / screenshots: opens a page of a menu set directly (GT3R_GT3MENU_OPEN).
    void debug_open(const std::string& set, const std::string& page) { open_control(set, page, -1); }

private:
    std::vector<ElementDraw> draw_list_impl() const;
    MenuSet& load_set(const std::string& name);
    void switch_control(const std::string& set, const std::string& page, int focus);
    void open_control(const std::string& set, const std::string& page, int focus);
    void handle(const MenuEvent& e);
    void handle_gt(const MenuEvent& e);
    void handle_arcade(const MenuEvent& e);
    void handle_option(const MenuEvent& e);
    void handle_language(const MenuEvent& e);
    void arcade_step(int step);
    void arcade_back();
    void arcade_start_race();
    void build_carousel(const std::string& page);
    // One frame; move = -1 / 0 / +1 (an accepted move), `prev` = the entry moved from (-1: the
    // neighbour behind the move).
    void animate_carousel(int move, int prev = -1);
    int carousel_icon_at(float x, float y) const; // the strip icon under a point, or -1
    std::vector<std::string> carousel_parts(const std::string& id) const; // the entry's art (moves with the cross)
    // The hidden picture of a car page entry (GtModeHooks::arcade_image; a label that
    // starts with a digit is named "_<label>" in the layout), else the id itself.
    std::string entry_image(const std::string& id) const;
    std::string carousel_disabled() const; // why the shown carousel entry cannot be chosen, or ""
    void request_race(GameRequest r);
    // The save page (control "memorycard", page file_operation; save_page.h): when it
    // ends the flow switches to `ok` after a load / save, else to `back`.
    struct Target {
        std::string set, page;
        int focus = -1;
    };
    void open_save_page(SavePage::Mode m, Target back, Target ok, bool transition, std::string title = {});
    void switch_control_now(const Target& t);
    void log(std::string msg);

    FileReader reader_;
    std::string lang_;
    std::map<std::string, std::unique_ptr<MenuSet>> sets_;
    std::string control_;
    MenuSet* set_ = nullptr;
    std::unique_ptr<MenuController> ctl_;
    std::vector<std::string> sounds_;
    std::vector<std::string> log_;
    std::optional<GameRequest> request_;
    std::optional<TransitionType> transition_;
    std::string music_ = "-"; // current menu BGM ("-" = none chosen yet)
    std::optional<std::string> music_request_;
    std::string movie_;
    std::optional<std::string> movie_request_;

    // Pending control switch (runs after the transition).
    int switch_frames_ = 0;
    std::string next_set_, next_page_;
    int next_focus_ = -1;
    std::optional<GameRequest> pending_request_;
    bool waiting_game_ = false;
    std::string resume_set_, resume_page_;
    int resume_focus_ = -1;

    // Title / GT mode. The control's one title focus: page "root" always opens on it
    // (title_focus_after on coming back from a title command, go_top, set_focus_replay_theater).
    int title_focus_ = 0;
    // The stored race code and its type (race_page_hook); a race page's hook reads and clears it.
    std::string race_code_;
    int race_type_ = 0;
    int race_page_focus_now(const MenuSet& set, const std::string& page);
    // Arcade.
    int arcade_game_ = 0;           // index on ar/top
    std::string arcade_action_;     // its action
    std::vector<std::string> arcade_steps_;
    int arcade_pos_ = -1;
    int difficulty_ = 0;
    std::string course_, car_class_, car_;
    int car_paint_ = 0; // arcade car pickers: colour index of the focused car (Up / Down)
    int transmission_ = 0, tire_mode_ = 0; // car page steps 2 / 3 (kept while the arcade runs)
    Carousel carousel_;
    IniDocument* options_ = nullptr;
    // Options music list (menus §10.1): the race songs on the disc, the page and the top page's view.
    std::vector<MusicTrack> race_tracks_;
    bool race_tracks_loaded_ = false;
    MusicListPage music_page_, music_view_;
    std::optional<MusicPreview> preview_request_;
    std::optional<std::string> language_choice_;
    const std::vector<MusicTrack>& race_tracks();
    // Ours (the tabs): the source the list shows now (the chosen one, Original without custom
    // songs), the player's songs as tracks, the edited list into its playlists, a page's preview.
    race::MusicSource music_source() const;
    std::vector<MusicTrack> custom_tracks() const;
    void commit_music_list();
    void take_page_preview(MusicListPage& page);
    race::MusicLibrary* music_lib_ = nullptr;
    race::MusicSource music_shown_ = race::MusicSource::Original; // the editable page's list
    std::vector<MusicTrack> music_tracks_;                         // ... and its tracks
    race::RacePlaylist music_playlist(); // [options] music_playlist, or a new list (stored)
    // `tabs_focused`: the cursor on the tabs (a tab was just chosen).
    void open_music_list(bool editable, bool tabs_focused = false);
    bool music_list_active() const;      // the page is up and takes input
    bool music_list_area(SDL_FRect& area) const;
    void option_draws(std::vector<ElementDraw>& out) const;
    std::function<void()> options_changed_;
    bool key_config_page() const; // the Key Configuration page is up and takes input
    bool controller_settings_ = false;
    SavePage save_page_;
    bool save_mode_ = false; // the "memorycard" control shows the save page
    bool name_mode_ = false; // the save page's name entry (Edit file name, a new save)
    TextBox name_box_;       // its text and cursor
    MenuKeys held_name_ = 0;
    Target save_back_, save_ok_;
public:
    const SavePage* save_page() const {
        return control_ == "memorycard" && save_mode_ && !theater_mode_ ? &save_page_ : nullptr;
    }
    // The save page on its own (title Save game): saves the running game, or the state the
    // game side offered with game::begin_snapshot_save() (e.g. a championship between
    // rounds, without changing the running game), as a new save or over one (one
    // confirm), then reopens <set>/<page> (focus `focus`) either way.
    void open_save_game(const std::string& set, const std::string& page, int focus = -1) {
        open_save_page(SavePage::Mode::SaveGame, Target{set, page, focus}, Target{set, page, focus}, false);
    }
    // Replay Theater: the file list (Load Replay, Demonstration, the Analyzer's LOAD A / B).
    const TheaterPage* theater() const { return control_ == "memorycard" && theater_mode_ ? &theater_ : nullptr; }
    // The Analyzer (menu/logger.mbl page "top"): the loaded tracks and the focused button
    // (0 REPLAY, 1 GRAPH, 2 LOAD A, 3 LOAD B, 4 EXIT).
    struct LoggerTrack {
        std::string path; // "" = none loaded
        std::vector<std::pair<std::string, std::string>> texts; // TheaterEntry texts
    };
    const LoggerTrack& logger_track(int i) const { return logger_tracks_[i & 1]; }
    int logger_focus() const { return logger_focus_; }
private:
    // Replay Theater (docs/dev/RACE.md "Replay Theater").
    enum class TheaterFrom { Theater, LoggerA, LoggerB };
    void open_theater(TheaterPage::Mode m, TheaterFrom from);
    void theater_tick(MenuKeys held);
    void logger_tick(MenuKeys held);
    void logger_draws(std::vector<ElementDraw>& out) const;
    TheaterPage theater_;
    bool theater_mode_ = false;
    TheaterFrom theater_from_ = TheaterFrom::Theater;
    bool theater_resume_ = false; // gameplay started by the theater: resume without results
    LoggerTrack logger_tracks_[2];
    int logger_focus_ = 0;
    int logger_frames_ = 0;
    MenuKeys logger_prev_ = 0;
    u32 generation_ = 0; // bumped whenever a page is opened
    bool gt_attached_ = false; // GtModeHooks::attach ran
    MenuKeys held_ = 0;
    MenuKeys held2_ = 0;
    bool two_page_ = false;           // ar/select_car_2p is open
    TwoPlayer two_[2];
    std::vector<std::string> raw_cars_; // every car picture of the 2 Player car page
    void two_player_open();
    void two_player_frame();
    bool two_player_step(int p, MenuKeys fired);
    void two_player_draw(std::vector<ElementDraw>& out) const;        // keys held this tick (arcade Professional: L1 + R1 on Hard)
    bool presenting_ = false;  // a GT-mode presentation control is open
    bool standalone_shows_ = false; // start_presentations(): exit when the queue is empty
    MenuKeys injected_ = 0;    // keys pressed by a tap, applied on the next tick
    int carousel_jump_ = -1;   // a tapped strip icon, moved to on the next tick
    bool exited_ = false;      // a control started with start(set, page) was left
    bool standalone_save_ = false; // started with start_save_game()
    // Attract mode: idle frames on the title, demos run.
    int idle_frames_ = 0;
    bool in_attract_ = false;
    int attract_demos_ = 0;
    bool attract_movie_ = false;
    void start_attract_demo();
    bool show_presentation();  // opens the next one; false when none
    void after_game();         // presentations, then the next round or the menu
};

} // namespace gt3r::menu::flow
