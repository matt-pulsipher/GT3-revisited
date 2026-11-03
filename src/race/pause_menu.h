#pragma once

// GT3's race pause windows (race menu select window and its PauseMenu /
// pause menu with retry / replay pause menu builders), drawn from the disc's
// menu/<LANG>/pause1..3.pmb: a row of image buttons from the page's hidden
// layer (`m_<name>` in orange, `m_<name>_sel` in light orange over the
// selected one).
//
//   Race   (pause1, races, 2P, free runs, the attract demo): CONTINUE, EXIT
//   Retry  (pause3, time trials, licence tests, machine tests): CONTINUE, EXIT, RETRY
//   Replay (pause2): CONTINUE, EXIT, REPLAY OPTIONS
// EXIT in a race asks "Exit race?" with OK / CANCEL (CANCEL focused); the replay's
// EXIT leaves at once. Left / Right (auto-repeat 30 / 3 frames) move the cursor and
// clamp at the ends (a disabled button cannot be reached from the left); Circle /
// Cross choose; Triangle / Square = CONTINUE in the button row, CANCEL in the
// question. SE "cursor" on a move, "ok" on a choice, "cancel" on Triangle / Square.
// The selected button's light copy fades from alpha 1 to 0 over 30 frames and
// starts again (a 0.5 s sawtooth); a disabled button is drawn at half alpha.

#include "base/bytes.h"
#include "menu/menu_renderer.h"
#include "menu/menu_scene.h"

#include <SDL3/SDL_render.h>

#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <vector>

namespace gt3r {
class Vfs;
namespace text {
class GameFonts;
}
} // namespace gt3r

namespace gt3r::race {

class PauseMenu {
public:
    enum class Kind : u8 { Race, Retry, Replay };
    enum class Result : u8 { None, Continue, Exit, Retry, ReplayOptions };

    bool load(const Vfs& vfs, SDL_Renderer* r, text::GameFonts& fonts, const std::string& lang = "US");
    bool loaded(Kind k) const { return scenes_[int(k)].has_value(); }
    // Opens the window: the button row, cursor on CONTINUE.
    void open(Kind kind);
    Kind kind() const { return kind_; }
    // RETRY greyed out and unselectable (presentation §E3: a run that set a record, until
    // the next run starts).
    void set_retry_enabled(bool on) { retry_enabled_ = on; }
    bool retry_enabled() const { return retry_enabled_; }

    // One 60 Hz frame with the logical menu keys held (menu::flow::MenuKeys bits).
    // Keys held when the window opened count only after they were released.
    Result tick(u16 held_keys);
    Result update(u16 held_keys, double dt);
    std::vector<std::string> take_sounds() { return std::exchange(sounds_, {}); }

    // Pointers (menu units, 640x448 y up): a hover moves the cursor to the button under it; a
    // tap on the cursor's button chooses it, on another moves the cursor there (a finger's first
    // tap; a hovering pointer is already on it).
    bool hover(float x, float y);
    Result tap(float x, float y);

    bool confirming() const { return confirm_; }
    int cursor() const { return cursor_; }
    // Screenshots / scripts: the cursor on a button of the current row.
    void set_cursor(int i);

    void draw(SDL_Renderer* r, const SDL_FRect& dst);

private:
    struct Button {
        std::string name; // "continue", "exit", ...
        int id = 0;       // the original's button ids: 3 continue, 1 exit (ask) / cancel / replay options, 4 exit / ok, 5 retry
        bool enabled = true;
    };
    std::vector<Button> row() const;
    int button_at(float x, float y) const; // -1 = none (a disabled one counts as none)
    Result choose();                       // the cursor's button, as Circle / Cross

    std::optional<menu::MenuScene> scenes_[3];
    std::unique_ptr<menu::MenuRenderer> renderer_;
    Kind kind_ = Kind::Race;
    bool confirm_ = false;
    bool retry_enabled_ = true;
    int cursor_ = 0;
    float blink_ = 1.0f;
    u16 ignore_ = 0xFFFF, held_ = 0;
    int repeat_ = 0;
    double acc_ = 0;
    std::vector<std::string> sounds_;
};

} // namespace gt3r::race
