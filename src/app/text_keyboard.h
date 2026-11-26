#pragma once

// The game's own keyboard for text fields (the online start page's session code, name, address
// and relay server; race, camera and event names; event sums). It types where the system's would
// not do: in a VR headset no system keyboard opens over an immersive app, and the Touch
// controllers have no D-pad for letter-by-letter pad typing; on a phone or tablet the system's
// keyboard covers the field, and a controller that calls itself a keyboard keeps it from opening
// at all (issue 471). Elsewhere the fields type as before (a real keyboard, the pad).
//
//   Pad: Up/Down/Left/Right (the left stick) move between the keys, Cross types the key,
//   Square deletes, Start finishes, Triangle / Circle cancel.
//   Pointer (the controller's ray, a mouse): a hover focuses a key, a click types it; the right
//   button cancels.
//
// Shift switches the letters between lower and upper case; Paste adds the clipboard's text.
// The owner keeps the text, and a real keyboard's typing (Screen::on_event).

#include "app/ui.h"

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

#include <string>
#include <vector>

namespace gt3r {

class App;

class TextKeyboard {
public:
    enum class Result : u8 { None, Done, Cancel };
    // Text: letters, digits and a few signs. Number: a keypad of the digits alone (sums, counts).
    enum class Layout : u8 { Text, Number };
    static constexpr size_t kMaxLength = 63;

    // `done` names the finishing key ("OK", "Join"); `upper` starts the letters in upper case.
    void open(std::string title, std::string done, bool upper, Layout layout = Layout::Text);
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    // The pad and the pointer's actions (PointerInput::take()), once per update; types into `text`.
    Result update(const MenuInput& m, const std::vector<PointerInput::Action>& pointer, std::string& text);
    // Over the page, with its footer.
    void draw(Ui& ui, const std::string& text);
    // A key at (x, y) (Screen::clickable).
    bool clickable(float x, float y) const;

private:
    enum Key : u8 { kChar, kShift, kSpace, kDelete, kPaste, kCancel, kDone };
    struct Cap {
        Key key = kChar;
        char c = 0;         // kChar: its character (lower case for letters)
        int row = 0;        // 0 = the digits
        float x = 0, w = 1; // columns from the left, wide keys more than one
        UiRect r;           // where draw() put it
    };
    Result press(int i, std::string& text);
    Result finish(Result r) { return open_ = false, r; }
    int nearest(int row, float x) const; // the key of `row` closest to column `x`

    bool open_ = false, upper_ = false;
    Layout layout_ = Layout::Text;
    int rows_ = 0, cols_ = 0; // the layout's rows, and its width in key columns
    std::string title_, done_;
    std::vector<Cap> caps_;
    int focus_ = 0;
};

// A text field typed without the keyboard above, from what a screen's on_event gets: a real keyboard and the system's screen
// keyboard (Enter finishes, Esc cancels), and with `pad_typing` a controller's: up/down change the last character, right adds
// one, left deletes, A or Start finishes, B cancels.
TextKeyboard::Result text_field_event(const SDL_Event& e, std::string& s, bool pad_typing);

// Whether a text field types on TextKeyboard: in a VR headset, on a phone or tablet (App::touch_ui), or `via_pad`
// (the field was chosen with a controller, where a screen asks for that).
bool use_game_keyboard(App& app, bool via_pad = false);
// SDL text input for a field. With `game_keyboard` the system's screen keyboard stays closed while
// a real keyboard still types into the field (text_field_event).
void start_text_input(SDL_Window* window, bool game_keyboard, SDL_PropertiesID props = 0);
void stop_text_input(SDL_Window* window);

} // namespace gt3r
