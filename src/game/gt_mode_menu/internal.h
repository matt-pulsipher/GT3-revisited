#pragma once

// Private to the GT-mode menu bindings (the public API is game/gt_mode_menu.h): the module's
// state and what its files share. The bindings adapt the generic menu runtime (menu/flow/,
// which does not know the game) to the GT Mode rules (gt_mode.h) through GtModeHooks, one file
// per concern (docs/dev/GT_MODE.md, "Menu integration"). The declarations below are grouped by
// the file that defines them.

#include "game/gt_mode_menu.h"
#include "game/prize_count_up.h"
#include "game/save_library.h"
#include "menu/flow/gt_mode_hooks.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gt3r::game::gt_menu {

using menu::Element;
using menu::MblItem;
using menu::flow::GameRequest;
using menu::flow::GameResult;
using menu::flow::MenuController;
using menu::flow::MenuEvent;
using menu::flow::MenuKey;
using menu::flow::key_bit;

// Presentations queued by a result (car_get / license_get pages).
struct Show {
    std::string set, page;
    std::vector<std::string> layers;
    std::string reason;                  // car_get: hidden text element naming why ("championship_win")
    std::string car;                     // car_get: the car received
    int paint = 0;
    std::vector<std::string> candidates; // car_get: the set's cars (roulette)
    std::string record_key;              // name_entry: the ranking and the new entry's rank
    int record_rank = -1;
    std::string model;                   // license_get / trophy_get: the prize model (.prm path)
    s64 money = 0;                       // trophy_get: the championship bonus counted up
    std::string rank;                    // arcade_mini / gtmode_mini / consolation_prize: "1st", "4th", ...
    bool credited = true;                // false: `money` is not in the credits yet (shown from the race)
    // The prize counter (race prizes gtmode_mini / consolation_prize, the championship trophy):
    // the balance counting from `from` to `from + money` (prize_count_up.h).
    struct Counter : PrizeCountUp {
        int done_frame = -1;  // presentation frame the count finished on
        bool started = false;
    } counter;
};

// Presentation timelines (frames at 60 Hz, gt_mode §18): when the model fades in.
constexpr int kLicFadeIn = 120, kLicFadeEnd = 180, kTrophyFadeIn = 180, kTrophyFadeEnd = 270;
constexpr int kLicMess0End = 240, kLicMess1 = 360, kLicOk = 420;
// Trophy (gt_mode §18): mess0 for 180 frames, 90 more, then get_mess;
// after 60 frames it waits for the prize counter (its count starts 360 frames after the page
// opened: the counter's constructor, PrizeCountUp::delay); 15 frames after the count,
// mess1 and OK.
constexpr int kTrophyMess0End = 180, kTrophyMoney = 270, kTrophyCountDelay = 360, kTrophyOkAfter = 15;
// Race prizes (presentation §F): the arcade cup accepts OK after 180 frames; the GT-mode cup and
// the consolation prize once the prize-money counter (presentations.cpp) has finished.
constexpr int kMiniFadeFrames = 30, kArcadeMiniOk = 180;

// GT Mode map pointer (map button list, spec menus): a direction picks the building that
// maximises cos^2 / distance among those within 90 degrees of it; the pointer eases
// there (30 % per frame) and the building takes the focus within 12 px.
struct MapPointer {
    bool active = false;
    float x = 320, y = 224;  // menu units (y up)
    int target = -1;
    bool moving = false;     // a direction chose a target the pointer has not reached yet
    menu::flow::MenuKeys prev = 0;
};

// A code-drawn list held still under a hovering pointer that moved its cursor (the wheel shop's
// tile row, WheelRow; the vertical lists, ListRows).
struct WheelPin {
    std::string page;
    int focus = -1;
    float off = 0;
};
// A code-drawn list's offset on a page, easing towards the focused button's (as GT3's
// button list: 30 % a frame); for another page none yet (the list is placed at once).
struct ShopRow {
    std::string page;
    float off = 0;
};

// An oil change in progress on the oil page's gauge (widget_art.cpp): the controller frame it
// started on, the old oil's age (its colour until the new oil is in) and the "oil" effects
// played so far.
struct OilChange {
    bool on = false;
    u32 start = 0;
    float age = 0;
    int sounds = 0;
};

// A car wash in progress (widget_art.cpp): the controller frame it started on, the car's dirt
// before it (the car is shown that dirty until the wash is over), whether its effect played,
// and the spin it gave the car (car_wash_spin: a serial per wash, the frames pushed so far).
struct CarWash {
    bool on = false;
    u32 start = 0;
    float dirt = 0;
    bool sound = false;
    int serial = 0;
    int pushes = 0;
};

// Everything the module keeps between hook calls, in one place. The menu runtime calls the
// hooks as plain functions, so it lives for the whole run; reset_gt_mode_menu_for_tests()
// replaces it with a fresh one (whatever is added here is reset with it).
struct MenuState {
    // The game (session.cpp): the disc data (loaded once), its reader (the PS2 export's icons)
    // and the running session.
    std::shared_ptr<GtData> data;
    GtData::Reader reader;
    std::string lang = "US"; // the menus' language folder (the name entry's characters)
    std::unique_ptr<GtModeSession> session;
    // Races (races.cpp): the last request (the next championship round repeats it), the free
    // run's course, whether the last result was a championship round with more to come, and
    // the last attract demo picked.
    std::optional<GameRequest> last_request;
    std::string free_run_course;
    bool round_pending = false;
    size_t attract_index = 0;
    // Presentations (presentations.cpp): queued, the one being shown, its frame, and a serial
    // bumped for every presentation shown.
    std::vector<Show> shows;
    std::optional<Show> show;
    int show_frames = 0;
    int show_serial = 0;
    // Taken by the flow: effects to play on the next frames (one per frame, on a frame the page
    // itself plays none; they never hold the page's per-frame code), the popup to open
    // after a consumed Finish (load_warning), the ending movies to play after the result screens
    // (in turn: the short one, then the long one).
    std::vector<std::string> sounds;
    std::string popup;
    std::vector<std::string> movies;
    // Code-drawn lists (lists.cpp).
    MapPointer map;
    WheelPin wheel_pin;
    ShopRow shop_row;
    ShopRow list_off; // the vertical list's (ListRows)
    WheelPin list_pin;
    // The menu controller's frame at the last on_tick (actions run after it, on the same frame),
    // the oil gauge's animation and the car wash.
    u32 frame = 0;
    OilChange oil_change;
    CarWash wash;
    // Saves (session.cpp): the save library and the GT3R_GTMODE_SAVE it was made for (made again
    // when that changes), and the state the next save page saves instead of the running game
    // (begin_snapshot_save).
    std::unique_ptr<SaveLibrary> library;
    std::string library_env;
    std::optional<GameState> snapshot;
};

MenuState& menu_state();

// The running session (nullptr until the GT-mode menus opened with disc data).
inline GtModeSession* S() { return menu_state().session.get(); }

inline bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// --- common.cpp: helpers the files share.
std::string cr(s64 v); // log lines
// Menu money texts: the game's "Cr. %s" ("Cr. 14,660", credits_text) on the dealer pages,
// the plain number next to a Cr. icon elsewhere.
std::string cr_pre(s64 v);
std::string cr_num(s64 v);
std::string car_name(const GtMode& gt, const std::string& label);
// Item name -> goods for the price widgets ("price_tbk_1") and shop items.
int goods_of_item(const std::string& item);
// "bbw001_s_link" -> "bbw001_s" (the original cuts the last "_..." part).
std::string wheel_of_item(const std::string& item);
std::string ordinal(int place); // 0-based
bool is_mini_prize(const std::string& page);
// The ranking entry the name entry page edits (nullptr when none).
RankEntry* named_entry();
// Car settings menu: the committed car (the ride car) and the working copy.
GarageCar* setting_real();
GarageCar* setting_work();
bool is_setting_page(const std::string& page);
bool saving_enabled();
void autosave(std::vector<std::string>* log);
// Result lines go to the log file only: the menu screen prints the flow's log
// on screen (a developer overlay).
std::vector<std::string> to_log_file(const std::vector<std::string>& lines);
const Element* find_widget_named(const menu::Page& p, const std::string& name);
const Element* hidden_element(const menu::Page& p, const std::string& name);

// --- session.cpp
// Starts a session (replacing any) on the current save: its game, or a new game when there is
// none ("new game" logged). The disc data is loaded on first use and kept for later sessions;
// false when it cannot be read (`data_error` = why; no session is started). `loaded`: a save
// was loaded.
bool start_session(const GtData::Reader& read, std::string* data_error, bool* loaded = nullptr);

// --- prechecks.cpp: popups instead of an item, enable rules.
std::string intercept(const std::string& page, const MblItem& it);
bool enabled(const std::string& page, const MblItem& it);
// Items a page's list leaves out: the dealer region pages' Opel or Vauxhall button by the game's
// language (docs/dev/GT_MODE.md "Language", dealer_item_listed).
bool listed(const std::string& page, const MblItem& it);

// --- actions.cpp: the GT-mode control's actions.
bool on_event(const MenuEvent& e, std::vector<std::string>& log);

// --- races.cpp: race requests and results, attract demos.
void fill_car(GameRequest& r, const GtMode& gt, const GarageCar* c);
// The engine condition a run starts with (gt_mode §13.1): in the modes that carry it, car c's
// words (null or not a car of gt's garage: an arcade car, a fresh engine; a guest garage car
// lends its words but takes nothing back). apply_condition puts a result's words back.
void fill_condition(GameRequest& r, const GtMode& gt, const GarageCar* c);
void apply_condition(GtMode& gt, const GameResult& r);
void prepare_request(GameRequest& r);
std::vector<std::string> on_result(const GameResult& r);
std::optional<GameRequest> next_request();
std::optional<GameRequest> resume_request();
int attract_idle_frames();
std::optional<GameRequest> attract_demo();

// --- presentations.cpp: race prize pages, license card, trophy, prize car, name entry.
bool counter_done(const Show& sh);
// Trophy: OK (and mess1) 15 frames after the count, and not before the bonus box's 60 frames.
bool trophy_ok();
// The remaining-prize line's opacity at `frame` (in from `shown`, out after the count).
float remain_alpha(const Show& sh, int frame, int shown);
// The race prize page for a GT race result: the event's cup or the consolation prize.
std::optional<Show> race_prize_show(const GtMode& gt, const std::string& code, int place, s64 money);
// on_frame on a presentation page: its timeline; `se` = the effect to play on this frame (the
// prize counter's beep). False when `page` is not one (or not shown).
bool presentation_frame(MenuController& ctl, GtModeSession& s, const std::string& page, std::string& se);
std::optional<menu::flow::GtModeHooks::Presentation> next_presentation();
// Record name entry: Back Space, and the text box's cursor (-1 = no name entry).
bool name_backspace();
int name_cursor();
constexpr size_t kRecordNameMax = 15; // a record's name: characters at most (gt_mode §17.1)

// --- widget_text.cpp
std::optional<std::string> widget_text(const std::string& page, const Element& e);

// --- widget_art.cpp: image widgets filled from the page's hidden art, widget art hidden for code.
void widget_draws(const MenuController& ctl, const Element& w, std::vector<menu::ElementDraw>& out);
bool hide_art(const std::string& page, const Element& el);
// The oil page's gauge `frames` frames into an oil change, step for step as the original's: 61
// frames' wait, the oil drains by 0.01 of the cylinder a frame, 61 frames' wait, the new oil is
// in, it fills back by 0.01 a frame; an "oil" effect as the draining and the filling start.
struct OilCylinder {
    float level = 1;    // the oil's height, 0..1 of the cylinder
    bool clean = false; // the new oil is in
    int sounds = 0;     // "oil" effects started so far
    bool done = false;
};
OilCylinder oil_cylinder_at(u32 frames);
// on_tick: the oil change's effects as it reaches them (returned to play); when it is done it
// gives back the controls its progress popup took away. It also ends when its gauge is no
// longer on screen.
std::string step_oil_change(MenuController& ctl);
// The car wash `frames` frames after its action, step for step as the original's: a 31-frame
// wait, then the "car_wash" effect and 181 frames in which the car's turntable is pushed round
// once a frame; the last push ends the wash (the car is shown clean, the controls come back).
struct WashSpin {
    bool sound = false; // the effect has played
    int pushes = 0;     // frames pushed so far (0..181)
    bool done = false;
};
WashSpin wash_spin_at(u32 frames);
// on_tick: the car wash's effect as it reaches it (returned to play); when it is done it gives
// back the controls its progress popup took away.
std::string step_car_wash(MenuController& ctl);
// The oil gauge, drawn into the (sorted) draw list at its own depth: on the oil page, and on
// that page under its progress popup.
void oil_gauge_draws(const MenuController& ctl, std::vector<menu::ElementDraw>& out);

// --- lists.cpp: code-driven lists (garage, permitted cars, wheel shop tiles, map pointer) and
// the per-frame input.
// The code-drawn vertical lists (shared by drawing and touch): the garage and trade lists, Cars
// Permitted and the records, each GT3's vertical button list (menu::flow::column_layout): the
// page's item template once per entry, kRowGap = 4 apart from the top of the list's window
// widget (so 28 units apart for the garage's 24-unit rows, 24 for Cars Permitted's 20, 60 for
// the records' 56); while the list is longer than the window the cursor row is centred and the
// ends are held at the window's edges. The offset eases towards the cursor row's
// (MenuState::list_off, 30 % a frame; placed at once when the page opens) and the rows are
// clipped to the window. While a hovering pointer moves the cursor the list holds still
// (MenuState::list_pin), so the row stays under it; a cursor key releases it.
struct ListRows {
    const Element* tmpl = nullptr;   // the page's item (its alt: the cursor row's look)
    const Element* win = nullptr;    // the window widget
    int n = 0, cursor = 0;
    std::vector<float> y;            // each row's top in the list, downwards from its top
    float total = 0;                 // the list's height
    float target = 0, off = 0;       // the list's top from the window's top (downwards): wanted, shown
    // Row i in layout coordinates (y up), at the window's left edge.
    SDL_FRect box(int i) const {
        return {win->x, win->y + win->h - off - y[size_t(i)] - tmpl->h, tmpl->w, tmpl->h};
    }
    // Whether any of row i is inside the window.
    bool shown(int i) const {
        const SDL_FRect b = box(i);
        return b.y < win->y + win->h && b.y + b.h > win->y;
    }
    SDL_FRect clip() const { return {win->x, win->y, win->w, win->h}; }
};
bool garage_rows(const MenuController& ctl, GtModeSession& s, ListRows& g);
// The race information's Cars Permitted popup.
bool regulated_rows(const MenuController& ctl, GtModeSession& s, int n, ListRows& g);
// The records page's places (`n`: 12, a licence test's 6).
bool records_rows(const MenuController& ctl, GtModeSession& s, int n, ListRows& g);
// The places of the records page's table.
int records_places(const GtModeSession& s);
// A vertical list's scroll bar (the page's "scroll_bar" widget, GT3's ScrollBar): nothing, track
// included, while the list is no longer than the bar; else the track and the hidden "pinch"
// over the visible share of the list at its offset: top = bar top - offset x bar h / list h,
// window h x bar h / list h high (top-down).
void list_scroll_bar(const ListRows& g, const menu::flow::MenuSet::PageRef& ref, std::vector<menu::ElementDraw>& out);
// The wheel shop's tiles on a brand page: the brand's wheels in a row in the "menu_wheel_shop"
// window (GT3's wheel shop button list: a horizontal button row, menu::flow::row_layout, of the
// page's 52 x 52 items kRowGap = 4 apart, so 56 units from tile to tile; its offset eases
// towards the focused tile's, ShopRow). While a hovering pointer moves the cursor the row holds
// still (WheelPin), so the tile stays under it; a cursor key releases it.
struct WheelRow {
    const Element* win = nullptr;
    int n = 0;
    float tile = 52, step = 56, off = 0, target = 0;
    SDL_FRect box(int i) const { return {win->x + off + float(i) * step, win->y + (win->h - tile) * 0.5f, tile, tile}; }
};
bool wheel_row(const MenuController& ctl, WheelRow& r);
// The garage's wheel list (hm/hm_wheel, GT3's WheelButtonList, gt_mode §7): one button per
// entry of the wheel inventory, the stock-wheel placeholder first and then the bought wheels in
// the order they were bought, as a horizontal button row (menu::flow::row_layout) centred in the
// "menu_wheel" window and scrolling with the cursor (GtModeSession::wheel_cursor /
// wheel_row_off). Each button is the page's "wheel" item (its 60 x 60 frame, the focused one in
// the item's focus look) with the entry's 52 x 52 tile in the middle: the page's hidden
// "default_wheel" for the placeholder, the hidden image named after the wheel otherwise; the
// hidden "mark" frame is drawn over the entry the car wears. While a hovering pointer moves the
// cursor the row holds still (WheelPin), as the wheel shop's.
struct WheelList {
    const Element* win = nullptr;    // "menu_wheel"
    const Element* button = nullptr; // the page's "wheel" item
    int n = 0;                       // entries: the placeholder + the bought wheels
    std::vector<float> x;            // each button's left edge in the row
    float off = 0, total = 0;        // the row's offset in the window (as it eases), its width
    float target = 0;                // the offset it eases towards (the cursor's button's)
    SDL_FRect box(int i) const {
        return {win->x + off + x[size_t(i)], win->y + (win->h - button->h) * 0.5f, button->w, button->h};
    }
};
bool wheel_list(const MenuController& ctl, const GtModeSession& s, WheelList& r);
// The item a page opens on (GtModeHooks::initial_item): a wheel shop brand page on the wheel the
// ride car wears, when the page sells it (wheel shop button list).
std::string initial_item(const std::string& page);
// The list's entry `i`: "" (the stock wheel) for the placeholder, else the bought wheel.
std::string wheel_list_entry(const GameState& st, int i);
// The entry a car wears: 0 on its own wheels, k for GameState::wheels[k - 1], -1 for a wheel the
// inventory does not hold (no button is marked, gt_mode §7).
int worn_wheel_entry(const GameState& st, const GarageCar& c);
// The garage car the wheel page is for (the one chosen in the garage), or nullptr.
const GarageCar* wheel_page_car(const GtModeSession& s);
void map_input(MenuController& ctl, menu::flow::MenuKeys held);
std::string on_frame(MenuController& ctl);
std::string on_tick(MenuController& ctl);
int garage_hover(MenuController& ctl, float x, float y);
int garage_tap(MenuController& ctl, float x, float y);

// --- extra_draws.cpp
void extra_draws(const MenuController& ctl, std::vector<menu::ElementDraw>& out);

// --- arcade.cpp: the arcade menus' game data and results.
void on_arcade_result(const GameResult& r);
std::vector<std::pair<std::string, std::string>> arcade_parts(const std::string& page, const std::string& entry);
std::array<float, 3> arcade_gauges(const std::string& entry);
std::vector<std::pair<std::string, u32>> arcade_colors(const std::string& entry);
std::string arcade_image(const std::string& entry);
std::vector<std::pair<std::string, std::string>> arcade_specs(const std::string& entry);
void arcade_prepare(GameRequest& r);
std::string arcade_disabled(const std::string& page, const std::string& course, const std::string& entry);
void arcade_entries(const std::string& page, const std::string& game, const std::string& car_class,
                    std::vector<std::string>& entries);

// --- save_backend.cpp: the save page's files and operations (menu/flow/save_page.h).
std::vector<menu::flow::SavePage::File> save_page_files(menu::flow::SavePage::Mode m);
int save_run(menu::flow::SavePage::Mode m, menu::flow::SavePage::Op op, const menu::flow::SavePage::File& f,
             const std::string& arg);
void save_closed();

} // namespace gt3r::game::gt_menu
