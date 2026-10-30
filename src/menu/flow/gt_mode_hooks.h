#pragma once

// Hooks between the GT-mode menus (GameFlow, control "gt_mode") and the GT
// Mode game logic (src/game/). The menus stay independent of the game state:
// the game side registers these callbacks once (see
// game/gt_mode_menu/) and the flow calls them where the original's
// GT mode menu runs its per-page code: item actions, confirm intercepts
// (no money / no license popups), enable predicates, code-filled text
// widgets (credits, days, car names, prices), race requests and results.
// Every hook is optional; without them GT mode behaves as before.

#include "menu/flow/game_hooks.h"
#include "menu/flow/save_page.h"
#include "menu/flow/menu_controller.h"
#include "menu/flow/menu_set.h"
#include "menu/menu_scene.h"

#include <array>
#include <functional>
#include <optional>
#include <string>

namespace gt3r::menu::flow {

struct GtModeHooks {
    // The GT-mode control is opened (once per GameFlow); `read` loads disc files, `lang` is
    // the flow's language folder ("US", "FR", ...).
    std::function<void(const FileReader& read, const std::string& lang)> attach;
    // Every event of the gt_mode control (item actions, page changes, ...),
    // before the flow's own handling; lines appended to `log` are shown.
    // Returning true for a Finish event (e.g. "go_save_in_home") consumes
    // it: the flow just rebuilds the page instead of its default handling.
    std::function<bool(const MenuEvent& e, std::vector<std::string>& log)> on_event;
    // Confirm intercept: a popup page to open instead of running the item.
    std::function<std::string(const std::string& page, const MblItem& item)> intercept;
    // Enable predicate for items (disabled items refuse confirm).
    std::function<bool(const std::string& page, const MblItem& item)> enabled;
    // Items a page's list leaves out altogether (MenuController::listed).
    std::function<bool(const std::string& page, const MblItem& item)> listed;
    // The item a page opens on (MenuController::initial_item), "" for the flow's rules.
    std::function<std::string(const std::string& page)> initial_item;
    // Text of a code-filled widget (by its class, e.g. "user_money") or
    // element; nullopt = keep the layout's text. `page` is the top page.
    std::function<std::optional<std::string>(const std::string& page, const Element& e)> widget_text;
    // Widgets whose own art is not drawn (their place is filled by code, e.g. a 3D car).
    std::function<bool(const std::string& page, const Element& e)> hide_art;
    // Once per frame after the controller ticked (code-driven lists read
    // controller.fired()). Returns a sound effect name to play, or "".
    std::function<std::string(MenuController& ctl)> on_frame;
    // Every frame, also while the controller is busy (confirm feedback, a page settling), right
    // before on_frame: game code on its own clock (the car wash, the oil change), which may give
    // back the controls a page took away (MenuController::give_control). Returns a sound effect
    // name to play, or "".
    std::function<std::string(MenuController& ctl)> on_tick;
    // A race is about to be requested: fill the event, car spec, course...
    std::function<void(GameRequest& r)> prepare_request;
    // Gameplay came back with a result (prizes, medals, odometer).
    std::function<std::vector<std::string>(const GameResult& r)> on_result;
    // After a result: the next race to run straight away (championship
    // rounds), or nullopt to reopen the menu.
    std::function<std::optional<GameRequest>()> next_request;
    // Extra elements for code-drawn content of the top page (garage list,
    // slider markers, marks), drawn on top of the list.
    std::function<void(const MenuController& ctl, std::vector<ElementDraw>& out)> extra_draws;
    // Image widgets filled by code (license cups, event marks, logos): called
    // for every drawn widget; draws appended to `out` are drawn right after it
    // (with the widget's alpha and offset).
    std::function<void(const MenuController& ctl, const ElementDraw& widget, std::vector<ElementDraw>& out)>
        widget_draws;
    // Presentations after a result (prize car "car_get", license "lc_get"):
    // the next one to show, or nullopt. Shown one after the other before the
    // next championship round / the menu; the hooks above also run for them.
    struct Presentation {
        std::string set, page;           // menu set and page, e.g. "car_get" / "car_get"
        std::vector<std::string> layers; // layers to force on (e.g. "b_mess0")
        // The page takes the controls away when it is built (no key does anything, its buttons
        // are not drawn) until game code gives them back (MenuController::give_control).
        bool take_controls = false;
    };
    std::function<std::optional<Presentation>()> presentation;
    // Touch on code-drawn lists (garage rows): 0 = not handled, 1 = handled
    // (e.g. the cursor moved), 2 = handled and the focused item is confirmed.
    std::function<int(MenuController& ctl, float x, float y)> tap;
    // A hovering pointer over those lists (and the wheel shop's tiles): 0 = not over one, 1 = over
    // the current one, 2 = the cursor moved to the one under it.
    std::function<int(MenuController& ctl, float x, float y)> hover;
    // A popup page to open after a consumed Finish action rebuilt its page
    // (e.g. "load_warning" after loading settings), or "".
    std::function<std::string()> take_popup;
    // Record name entry (presentation page "name_entry"): Back Space (Triangle / Square;
    // TextBox::back_space), true when it plays its sound; and the text box's cursor, the
    // characters before it (-1 = none: the caret after the text).
    std::function<bool()> name_backspace;
    std::function<int()> name_cursor;
    // After a result (and its presentations): a movie to play first (the GT-mode
    // endings: "stream/ending_short_full.pss" / "stream/ending_long_full.pss"), or "".
    std::function<std::string()> take_movie;
    // Attract mode: frames of idling on the title before it starts (0 = never),
    // and the next demo race to run (nullopt = none).
    std::function<int()> attract_idle_frames;
    std::function<std::optional<GameRequest>()> attract_demo;
    // A result of an arcade race (records: time trial rankings, Single Race
    // progression). `r.course` is the course chosen in the arcade menus.
    std::function<void(const GameResult& r)> on_arcade_result;
    // Arcade pickers: the entries the game offers on a code-drawn page
    // ("ar/select_course", "ar/select_car", ...), edited in place (locked
    // courses / cars removed, list order). `game` is the ar/top action
    // ("single_race", "time_attack", ...), `car_class` the class chosen.
    std::function<void(const std::string& page, const std::string& game, const std::string& car_class,
                       std::vector<std::string>& entries)>
        arcade_entries;
    // GT Mode map (page "map", a map button list): the direction keys move a pointer
    // between the buildings instead of the list order; called every tick with the
    // keys held (the controller does not see the directions on that page).
    std::function<void(MenuController& ctl, MenuKeys held)> map_input;
    // Arcade picker entry art filled by the game (moving with the entry): hidden
    // elements to draw and their text ("" = the element's own), e.g. the course's
    // area badge, dirt / tarmac badge and length.
    std::function<std::vector<std::pair<std::string, std::string>>(const std::string& page, const std::string& entry)>
        arcade_parts;
    // Arcade car pickers: the name of the hidden picture element of an entry whose id is not
    // a picture ("<image>" and its strip icon "<image>_mini"), "" = the id itself. Garage
    // class entries ("hg<i>" / "gg<i>") show the picture of their car's label.
    std::function<std::string(const std::string& entry)> arcade_image;
    // Arcade car pickers: the spec ticker's lines (label, value), e.g. {"Length", "4600mm"}.
    std::function<std::vector<std::pair<std::string, std::string>>(const std::string& entry)> arcade_specs;
    // Arcade car pickers: the CircleGauge values 0..1 {handling, speed, acceleration}.
    std::function<std::array<float, 3>(const std::string& entry)> arcade_gauges;
    // Arcade car pickers: the car's paints (name, RGB as bytes R, G, B in the low 24 bits).
    std::function<std::vector<std::pair<std::string, u32>>(const std::string& entry)> arcade_colors;
    // Arcade picker entry that cannot be chosen (ar/select_class: road classes on
    // a dirt course, ...): the hidden doc element explaining why, or "".
    std::function<std::string(const std::string& page, const std::string& course, const std::string& entry)>
        arcade_disabled;
    // Entering GT mode (go_sitemap) with a suspended championship: the round
    // to resume, or nullopt.
    std::function<std::optional<GameRequest>()> resume_request;
    // An arcade race is about to be requested (garage-class cars: the car's spec, colour
    // and the race code picked from its power-to-weight ratio).
    std::function<void(GameRequest& r)> arcade_prepare;
    // The save page (save_page.h: title Load / Save / File Operation, home Save, Trade,
    // arcade Load Garage, Load / Save Settings): its files and operations (SavePage::Backend).
    std::function<std::vector<SavePage::File>(SavePage::Mode m)> save_list;
    std::function<int(SavePage::Mode m, SavePage::Op op, const SavePage::File& f, const std::string& arg)> save_run;
    // The save page closed (drops a snapshot offered for it, game::begin_snapshot_save).
    std::function<void()> save_closed;
};

void set_gt_mode_hooks(GtModeHooks h);
const GtModeHooks& gt_mode_hooks();

// Widgets drawn by the app (3D car renders): called by the menu screen's
// widget drawer for every widget with a class; `box` is in target pixels.
// `image`: the element's own decoded image, or null (a car render's image is the
// showroom's reflection map). Lives apart from GtModeHooks because it needs the App
// (GPU, VFS).
using GtModeWidgetDrawer = std::function<void(App& app, SDL_Renderer* r, const Element& e, const SDL_FRect& box,
                                              float alpha, const std::string& page, const MenuImage* image)>;
// The decoded image of `e` in `scene`, or null.
inline const MenuImage* element_image(const MenuScene* scene, const Element& e) {
    if (!scene || !e.image || e.image->image >= scene->images().size()) return nullptr;
    const MenuImage& img = scene->images()[e.image->image];
    return img.rgba.empty() ? nullptr : &img;
}
void set_gt_mode_widget_drawer(GtModeWidgetDrawer d);
const GtModeWidgetDrawer& gt_mode_widget_drawer();
// Frees what the widget drawer keeps between frames (models, 3D views, targets): called when
// the menus hand over to a race, which needs the memory on 4 GB phones.
using GtModeWidgetRelease = std::function<void(App& app)>;
void set_gt_mode_widget_release(GtModeWidgetRelease f);
const GtModeWidgetRelease& gt_mode_widget_release();

} // namespace gt3r::menu::flow
