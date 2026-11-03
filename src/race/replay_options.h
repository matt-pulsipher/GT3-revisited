#pragma once

// GT3's REPLAY OPTIONS (replay option, menu/<LANG>/replay_panel1.pmb page replay_option,
// replay_panel2.pmb replay_option_2p for 2P replays), opened from the replay pause's
// REPLAY OPTIONS button.
//
// Rows (top to bottom): MODE (traditional, avant-garde, broadcast, course line: the replay
// camera director's view modes 0..3), TARGET (MANUAL / TOP / AUTO: the car the cameras
// follow), ITEM (two rows of HUD parts shown during replays: easy meter, car name, time;
// map, steering, tyres, accel / brake), SOUND (BGM, SE) and, in 2P, SCREEN (single / split).
// Up / Down choose the row, Left / Right move the cursor along it (TARGET skips the targets
// the mode does not allow), Circle / Cross apply the focused value (MODE, TARGET, SCREEN) or
// toggle it (ITEM, SOUND); Triangle / Square close the panel. SE "cursor" on a move, "ok" on
// a change. The panel is the only way to change the view mode (the replay pad has no mode
// buttons); applying the avant-garde mode also stores BGM on. The mode table (per mode:
// allowed targets, the target taken when the stored one is not allowed, and whether BGM is
// forced on) is the original's:
//   0 traditional: MANUAL         (default MANUAL)
//   1 avant-garde: AUTO           (default AUTO, BGM forced on, the BGM button disabled)
//   2 broadcast:   MANUAL TOP AUTO (default AUTO)
//   3 course line: MANUAL         (default MANUAL)
// Cells: the icon art (white) tinted by the page's colour quads: color_on (yellow) for the
// values in force, color_off (dark yellow) for the others, color_disable (darker) for a
// value the mode does not allow; the focused cell is lightened by a pulsing white (M,
// matched to the captures: on 251,197,0 -> 255,255,89; off 104,84,0 -> 197,177,93).
//
// The state logic is SDL-free (tests); the panel draws with a menu::MenuRenderer.

#include "base/bytes.h"
#include "menu/menu_renderer.h"
#include "menu/menu_scene.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gt3r {
class Vfs;
class IniDocument;
namespace text {
class GameFonts;
}
} // namespace gt3r

namespace gt3r::race {

struct ReplaySettings {
    enum Item : u8 { EasyMeter = 1, CarName = 2, Time = 4, Map = 8, Steering = 16, Tire = 32, Accel = 64 };
    int mode = 0;                       // 0 traditional .. 3 course line
    std::array<int, 4> target{0, 2, 2, 0}; // per mode: 0 MANUAL, 1 TOP, 2 AUTO
    u8 items = 0;                       // Item bits shown in replays (the original's default: none)
    bool bgm = true, se = true;
    bool split = true;                  // 2P replays: split screen (false = player 1 only)
    // View mode 1's pad (presentation §G3; stored in the PS2 save's options block): Up / Down
    // the race-sound level in the music's marked sections (0..1, a new game 0.1), Right / Left
    // the music-timed cuts (0 off / 1 on, a new game off).
    float level = 0.1f, music_cuts = 0.0f;

    static bool target_allowed(int mode, int target);
    static int default_target(int mode);
    static bool bgm_forced(int mode) { return mode == 1; }
    int current_target() const; // the mode's stored target, or its default when not allowed
    bool bgm_on() const { return bgm || bgm_forced(mode); }

    // [options] replay_mode, replay_targets ("0,2,2,0"), replay_items, replay_bgm, replay_se,
    // replay_screen, replay_level ("0.1"), replay_music_cuts ("on" / "off").
    static ReplaySettings load(const IniDocument& cfg);
    void save(IniDocument& cfg) const;
};

// The title attract demo's view (presentation §G13): not the stored REPLAY OPTIONS but a pick
// from r in [0, 1): r < 0.33 mode 0 (MANUAL), r < 0.66 mode 2 with the target
// int(100 r) mod 3, else mode 1 (AUTO). `since_avant_garde` counts the demos since the last
// mode-1 one (0 at start-up): mode 1 is forced once it reaches 3, i.e. after two demos
// without it (three after start-up). Updates the count.
struct AttractView {
    int mode = 0, target = 0;
};
AttractView pick_attract_view(float r, int& since_avant_garde);

class ReplayOptions {
public:
    enum Row { RowMode, RowTarget, RowItem1, RowItem2, RowSound, RowScreen };

    void open(const ReplaySettings& s, bool two_player);
    // One 60 Hz frame with the menu keys held (menu::flow::MenuKeys). Returns false when the
    // panel closed (Triangle / Square).
    bool tick(u16 held);
    const ReplaySettings& settings() const { return s_; }
    bool changed() const { return changed_; } // something was applied since open()
    std::vector<std::string> take_sounds();
    // Pointers (menu units, 640x448 y up; `vertical_split` as draw()): a hover moves the cursor
    // to the cell under it; a tap on the cursor's cell applies it as Circle / Cross, on another
    // moves the cursor there. Disabled cells take neither.
    void hover(float x, float y, bool vertical_split = true);
    void tap(float x, float y, bool vertical_split = true);

    int row() const { return row_; }
    int column() const { return col_[size_t(row_)]; }
    int rows() const { return two_player_ ? 6 : 5; }
    static int columns(int row) { return row == RowMode ? 4 : row == RowTarget ? 3 : row == RowItem1 ? 3 : row == RowItem2 ? 4 : 2; }
    // Cell state for drawing: 0 off, 1 on (in force), 2 disabled.
    int cell(int row, int col) const;

    // Drawing (replay_panel1 / replay_panel2).
    bool load(const Vfs& vfs, SDL_Renderer* r, text::GameFonts& fonts, const std::string& lang = "US");
    void draw(const SDL_FRect& dst, bool vertical_split = true);

private:
    bool cell_at(float x, float y, bool vertical_split, int& row, int& col) const;
    void apply(); // the cursor's cell, as Circle / Cross

    ReplaySettings s_;
    bool two_player_ = false;
    bool changed_ = false;
    int row_ = 0;
    std::array<int, 6> col_{};
    u16 held_ = 0, ignore_ = 0xFFFF;
    int repeat_ = 0;
    int frames_ = 0;
    std::vector<std::string> sounds_;
    std::optional<menu::MenuScene> scenes_[2];
    std::unique_ptr<menu::MenuRenderer> renderer_;
};

} // namespace gt3r::race
