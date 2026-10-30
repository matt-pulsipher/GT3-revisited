#pragma once

// Interactive menu runtime: drives one MenuSet (.mbl flow + .pmb layouts) the
// way the original menus do.
// Frame based: call tick() once per 60 Hz frame with the logical keys held.
// The controller never touches SDL or audio; it publishes
//   - events (item actions, page changes, finish/exit) for game code,
//   - sound effect names (se.inf: "select", "ok", "next", "cancel", ...),
//   - a draw list (elements with alpha/offset/box overrides) for MenuRenderer.
// Behaviour and timings: docs/formats/menu.md.

#include "menu/flow/menu_set.h"
#include "menu/menu_renderer.h"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gt3r::menu::flow {

// Logical menu keys. The original merges the pad into these: Confirm =
// Circle or Cross, Cancel = Triangle or Square (no regional swap), the left
// stick doubles the D-pad.
enum class MenuKey : u8 { Up, Down, Left, Right, Confirm, Cancel, Start, Select, L1, R1, Count };
using MenuKeys = u16; // bit (1 << MenuKey)
constexpr MenuKeys key_bit(MenuKey k) { return MenuKeys(1u << unsigned(k)); }

struct MenuEvent {
    enum class Kind : u8 {
        Action,      // an item with an action string was confirmed (runs before its kind's effect)
        Choice,      // a multi item changed its sub-choice (child = new index, action = child's action)
        PageChanged, // a page was built (page = new page)
        BackAction,  // the page's back_action ran on cancel (action = back_action)
        Finish,      // kind-2 item confirmed: the menu ends with result 1; control code takes over
        Exit,        // cancel on a page without back target: the menu ends with result 0
        Confirm,     // confirm on a page without items (code-driven page); game code decides
        Cancel,      // cancel on a page without items (code-driven page); game code decides
    };
    Kind kind = Kind::Action;
    std::string action;
    std::string page; // page the event belongs to
    std::string item; // item name ("" if none)
    int child = -1;   // Choice: new child index; Action/Finish: item index
};

// A text element's style effects (its marquee, its typewriter) at `age` frames: what the
// controller applies to every text draw from its page's or layer's start; game code restarts
// one from its own start.
void text_effects(ElementDraw& d, const Element& e, u32 age);

// .mbl page flag bits (MblPage::flags42), see docs/formats/menu.md.
namespace page_flag {
constexpr u16 kHorizontal = 0x001; // Left/Right move the focus (else Up/Down)
constexpr u16 kWrap = 0x002;       // focus wraps at the ends
constexpr u16 kFadeIn = 0x004;     // fade from black (30 frames) when built
constexpr u16 kFadeOut = 0x008;    // fade to black (15 frames) when left
constexpr u16 kFeedback = 0x0C0;   // confirm feedback: 0 none, 0x40 decide glow, 0x80 flicker, 0xC0 blink
constexpr u16 kCancel = 0x100;     // Triangle/Square goes back
} // namespace page_flag

// A horizontal button row (button list, menus §4.6): buttons of the given widths `kRowGap`
// apart; centred when the row fits the window, else left / right aligned near the ends and the
// focused button (at `scroll`, eased between buttons) centred in between. Returns the row's x
// offset from the window's left edge; `x` = each button's left edge in the row, `total` = the
// row's width. The controller places its horizontal selectors with it; code-drawn rows too.
constexpr float kRowGap = 4.0f;
float row_layout(const std::vector<float>& widths, float scroll, float window_w, std::vector<float>& x, float& total);
// A vertical button list (button list, menus §4.6): buttons of the given heights `kRowGap` apart;
// at the top of the window while the list fits it (a vertical list that fits is not centred),
// else held at the window's top / bottom edge near the ends and the focused button (at
// `focus`) centred in between. Returns the list's top from the window's top edge, downwards
// (0 or less while it scrolls); `y` = each button's top in the list, `total` = the list's
// height. The code-drawn lists (the garage, Cars Permitted, the records) are placed with it.
float column_layout(const std::vector<float>& heights, float focus, float window_h, std::vector<float>& y,
                    float& total);
// One frame of a list's easing (GT3's button list, 0.3): 30 % of the way from
// `value` to `target`. The selectors ease their scroll position (in items) and a horizontal
// row its offset (row_layout's result for the focused button) this way.
float ease_scroll(float value, float target);

class MenuController {
public:
    explicit MenuController(const MenuSet& set);

    const MenuSet& set() const { return set_; }

    // Builds a page as the base level (what control code does when it
    // sets a page). `from` is the page we came from (focus
    // goes to the item linking to it). Returns false if it cannot be resolved.
    bool open(std::string_view page, std::string_view from = {});
    // Opens a page as a popup level from control code (e.g. the garage sort
    // panel on START); an open popup is replaced (there is one popup level).
    // False while a transition runs or if the page is missing.
    bool popup(std::string_view page);

    void tick(MenuKeys held);

    std::vector<MenuEvent> take_events() { return std::exchange(events_, {}); }
    std::vector<std::string> take_sounds() { return std::exchange(sounds_, {}); }

    // Everything to draw this frame, back to front.
    std::vector<ElementDraw> draw_list() const;

    // --- Hooks (the original's per-page tables filled by control code) -----
    // Initial focus for a page; return -1 for the default rules.
    std::function<int(const std::string& page)> initial_focus;
    // The item a page opens on when initial_focus has none (by name; "" or an item the page does
    // not list: the default rules), e.g. the wheel shop's brand pages on the car's wheel.
    std::function<std::string(const std::string& page)> initial_item;
    // Enable predicate; disabled items stay focusable but refuse confirm.
    std::function<bool(const std::string& page, const MblItem& item)> enabled;
    // Items a page's list leaves out (no button is built; the ones after it close up), e.g. the
    // GT-mode dealer pages by the game's language. Null = every item.
    std::function<bool(const std::string& page, const MblItem& item)> listed;
    // Confirm intercept (a per-page popup hook, menus §3 step 1):
    // return a page name to open it as a popup instead of running the item
    // (e.g. "00cmn/no_money"); "" = run the item normally.
    std::function<std::string(const std::string& page, const MblItem& item)> intercept;

    // --- State -------------------------------------------------------------
    struct ItemState {
        const MblItem* flow = nullptr;
        const Element* element = nullptr;     // page item of the same name (nullptr: not built)
        std::vector<const Element*> children; // multi items: page items of the children
        bool multi = false;
        bool nested_horizontal = false, nested_wrap = false;
    };
    bool has_page() const { return !levels_.empty(); }
    const std::string& page_name() const;
    MenuSet::PageRef page() const;
    const std::vector<ItemState>& items() const;
    int focus() const;
    const ItemState* focused() const;
    int depth() const { return int(levels_.size()) - 1; } // 0 = base page, >0 popups
    bool busy() const { return phase_ != Phase::Idle; }    // input is ignored
    // A page with an "ignore_control" widget takes the controls away when it is built (the
    // car wash's and the oil change's progress popups): no input at all, and the top page's
    // buttons are not drawn, until control code gives control back.
    bool ignoring_control() const { return ignore_control_; }
    void give_control() { ignore_control_ = false; }
    // Game code takes the controls away the same way (the prize car page's roulette).
    void take_control() { ignore_control_ = true; }
    bool finished() const { return finished_; }
    int result() const { return result_; }
    u32 frame() const { return frame_; }
    // Keys that fired this tick (press edges plus auto-repeat for directions
    // and L1/R1), for control code driving pages without items.
    MenuKeys fired() const { return fired_; }
    MenuKeys pressed() const { return pressed_; }

    // Control code took over a cancel that ended the page (Exit) and stays on it: the page
    // takes input again (e.g. the arcade car page stepping back from AUTOMATIC / MANUAL).
    void resume() { finished_ = false, result_ = 0; }
    void set_focus(int index);
    // Touch: the item of the top page under a point in layout coordinates
    // (640x448 menu units, x right, y up from the bottom-left), or -1. Uses
    // the boxes the items are drawn in (scrolling selectors included).
    int hit_test(float x, float y) const;
    // The box item `index` of the top page is drawn in (layout coordinates, as
    // the element boxes: x, y = bottom-left), following scrolling selectors;
    // `clip` gets the selector window (or the whole page). False if no such item.
    bool item_box(int index, SDL_FRect& box, SDL_FRect* clip = nullptr) const;
    // The fade-in alpha of an element created with a layer of the top page (code-drawn hidden
    // art takes the same 24-frame fade as the layer's own elements): since the layer was shown
    // (a focus layer, each time it appears), else since the page was built; 1 without a fade.
    float layer_fade_alpha(int layer, const Element& e) const;
    // A tap at a point: on the focused item it confirms it (like Circle/Cross);
    // on another item it moves the focus there (SE "select"). Returns false when
    // nothing was hit or the menu is busy.
    bool tap(float x, float y);
    // A pointer that hovers (mouse, a VR controller): the item under it is focused, so one click
    // then confirms. On a horizontal scrolling row the row then holds still (a still pointer
    // stays over its item) until the focus moves by key; vertical scrolling selectors, which
    // centre the focused item by design, are left to taps.
    bool hover(float x, float y);
    bool set_focus(std::string_view item);
    int choice(std::string_view item) const;
    void set_choice(std::string_view item, int child);
    // Same for a multi item of another page (e.g. before it is opened).
    void set_choice(const std::string& page, std::string_view item, int child) { choices_[choice_key(page, item)] = child; }
    bool item_enabled(int index) const;
    // Whether a layer of the top page is currently shown (default layer or a
    // focused item's layer); forced on/off with set_layer.
    bool layer_visible(int layer) const;
    void set_layer(std::string_view layer, int visible); // -1 = rules

private:
    // An instance of a .pmb layer shown for the focused item (str80: plain,
    // str40: slides in/out with a spring), or forced on by set_layer.
    struct LayerInst {
        int layer = -1;
        u32 created = 0;
        u32 dying_since = 0;
        bool dying = false;
        bool slide = false;
        float x = 0, y = 0, vx = 0, vy = 0, tx = 0, ty = 0;
        int spring_frames = 0;
    };
    struct Level {
        std::string name;
        MenuSet::PageRef page, import;
        u16 flags = 0;
        std::vector<ItemState> items;
        int focus = 0;
        u32 built = 0; // frame the page was built (element fade-ins)
        float pulse = 0.25f;
        float scroll = 0; // list scroll position (in items), eases towards focus
        bool pinned = false; // a hovering pointer moved the focus: the scroll stays put (hover())
        // A horizontal selector's row offset in its window (row_layout), easing towards the
        // focused button's (GT3 eases the offset, not the focus); not yet placed: none.
        float row_off = 0;
        bool row_off_set = false;
        std::vector<LayerInst> layers;
        std::map<std::string, int> layer_override;
    };
    enum class Phase : u8 { Idle, Feedback, FadeOut, Gap, Settle };
    enum class Op : u8 { None, Goto, Popup, Finish, Back, Exit };

    Level& top() { return levels_.back(); }
    const Level& top() const { return levels_.back(); }
    std::string choice_key(const std::string& page, std::string_view item) const {
        return page + "\x1f" + std::string(item);
    }
    void build(const std::string& name, const std::string& from, bool popup);
    void refresh_layers(Level& lv, int dir_code);
    void handle_input(MenuKeys pressed, MenuKeys fired);
    bool move_focus(int delta, int dir_code);
    void confirm();
    void run_item(const MblItem& it, int index);
    void start_feedback(Op op, std::string target);
    void perform(Op op, const std::string& target);
    void cancel(bool via_ok);
    void emit(MenuEvent::Kind k, std::string action, std::string item = {}, int child = -1);
    void sound(const char* name) { sounds_.push_back(name); }
    void step_layers(Level& lv);
    // A horizontal selector's row (its window, each item's left edge, the row's width) and the
    // offset it eases towards (the focused item's); false for other pages.
    bool horizontal_row(const Level& lv, const Element*& window, std::vector<float>& x, float& total,
                        float& target) const;
    // The row's offset as drawn now (row_off, or the target before the first frame).
    float row_offset(const Level& lv, float target) const { return lv.row_off_set ? lv.row_off : target; }
    void add_level_draws(std::vector<ElementDraw>& out, const Level& lv, bool is_top) const;
    float fade_alpha(const Element& e, u32 since) const;
    float dying_alpha(const Element& e, const LayerInst& li) const;

    const MenuSet& set_;
    std::vector<Level> levels_;
    std::string return_page_;                 // the original's "return" target
    std::map<std::string, int> choices_;      // page\x1fitem -> child (kept across visits)
    std::vector<MenuEvent> events_;
    std::vector<std::string> sounds_;
    bool finished_ = false;
    int result_ = 0;

    // Input: edges and the shared auto-repeat counter.
    MenuKeys held_ = 0, fired_ = 0, pressed_ = 0;
    int repeat_counter_ = 0;
    bool repeating_ = false;

    // Transition state.
    Phase phase_ = Phase::Idle;
    int phase_frames_ = 0;
    Op pending_op_ = Op::None;
    std::string pending_target_;
    std::string pending_from_; // page whose item gets the focus on the next page
    u16 feedback_ = 0; // page flags & 0xC0 of the running feedback
    int feedback_item_ = -1;
    u32 black_in_since_ = 0; // fade from black (page flag 4)
    bool black_in_ = false;
    int black_out_ = -1; // fade to black progress in frames (page flag 8), -1 = off
    u32 frame_ = 0;
    bool free_moved_ = false; // Left / Right on a free (name-entry) row since the last row change
    bool ignore_control_ = false; // ignoring_control()
};

} // namespace gt3r::menu::flow
