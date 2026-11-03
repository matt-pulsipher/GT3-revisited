#pragma once

// The online lobby's car and course pickers (docs/dev/ONLINE.md "Pickers"), after
// GT3's arcade pickers with the disc's own art:
//
//   Car: a maker page (GT Mode's dealers: the makers' logos and flags by region, as
//   cd/cd_<region> shows them; the GT Mode garage first) or a class page (the arcade's
//   classes C, B, A, S and rally cars with ar/select_class's logos, then the garage), then
//   the group's cars as the arcade car page shows them: the strip of "<car>_mini" icons
//   (ar/select_car), the car turning in the arcade showroom (menu/showroom_single.fbi, the
//   page's reflection map) and the paint column with each paint's colour and name
//   (carcolor.db / carcolor.sdb). With an `allowed` test (the lobby's rules) the pages can
//   list only the cars allowed.
//   Course: the arcade course page: the course's still behind (stream/course.ipic),
//   its logo and map, the strip of "<course>_mini" icons.
//
// Entries without disc art (courses the arcade lists lack, mod cars outside arcade
// mode) are drawn as text.
//
// Pointer input (mouse, touch, the VR pointer; the owner forwards Screen::on_event to
// event()): the maker / class tiles focus under a hovering pointer and open on a click; a
// click on a strip icon moves the cursor there and a second chooses (as the arcade pickers);
// the wheel and drags move along the strip; the paint chips, the strip's arrows, the tabs
// and the footer's actions are buttons; the right button goes back.

#include "app/ui.h"
#include "base/bytes.h"

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_render.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gt3r {

class App;
namespace data {
class CarDatabase;
}

struct PickerCar {
    std::string label, name, model;
    int garage = -1; // GT Mode garage index (the car with its parts and settings)
    bool mod = false;
    std::string maker; // database maker ("NISSAN"); garage cars are listed under the garage
    int paint = 0;     // garage cars: their own paint
    std::string wheel; // garage cars: the fitted wheel ("" = stock)
    // Arcade class cars: the arcade car (table 35 label; `label` is its base car), listed
    // only on the class page.
    std::string arcade;
    int arcade_class = 0;   // the class page it is on, 1..5 (game::arcade_class_tag); 0 = none
    std::string class_text; // its class on the car page ("Class A, 4.6 kg/PS"); "" = none shown
};

struct PickerCourse {
    std::string label, name;
};

struct PaintInfo {
    u32 rgb = 0; // bytes R, G, B
    std::string name;
};

// The disc art the lobby and the pickers draw, uploaded on first use.
class PickerArt {
public:
    PickerArt();
    ~PickerArt();
    PickerArt(const PickerArt&) = delete;
    PickerArt& operator=(const PickerArt&) = delete;
    // Rectangles in UI units; the art is fitted (aspect kept) and centred. False when
    // there is no art for the name.
    bool car_icon(App& app, const std::string& car, const SDL_FRect& dst, float alpha = 1, float bright = 1);
    bool has_car_icon(App& app, const std::string& car);
    // The maker's logo from GT Mode's dealers (a tuner's from the tune shop).
    bool maker_logo(App& app, const std::string& maker, const SDL_FRect& dst, float alpha = 1);
    // The dealer region of a maker (0 Japan, 1 USA, 2 Germany, ... as GT Mode lists them;
    // -1 = none) and the region's flag.
    int maker_region(App& app, const std::string& maker);
    bool region_flag(App& app, int region, const SDL_FRect& dst, float alpha = 1);
    bool course_icon(App& app, const std::string& course, const SDL_FRect& dst, float alpha = 1);
    bool course_map(App& app, const std::string& course, const SDL_FRect& dst, float alpha = 1);
    bool course_logo(App& app, const std::string& course, const SDL_FRect& dst, float alpha = 1);
    // The course's still (640 x 448) filling `dst`, cropped to its aspect when `cover`.
    bool course_still(App& app, const std::string& course, const SDL_FRect& dst, bool cover, float alpha = 1);
    // The arcade class page's logo of class 1..5 (game::arcade_class_tag), 0 = the home garage.
    bool class_logo(App& app, int cls, const SDL_FRect& dst, float alpha = 1);
    // The arcade car page's showroom reflection map (its "car" element image), or null.
    struct Image {
        u32 width = 0, height = 0;
        const std::vector<u32>* rgba = nullptr;
    };
    Image reflection_map(App& app);

private:
    struct Art;
    struct Ref;
    void load(App& app);
    bool draw_ref(App& app, const Ref* r, const SDL_FRect& dst, float alpha, float bright = 1);
    SDL_Texture* texture(App& app, int scene, int image);

    bool loaded_ = false;
    std::unique_ptr<Art> art_;
    std::map<std::pair<int, int>, SDL_Texture*> textures_;
    std::map<std::string, SDL_Texture*> stills_;
};

// A car turning in the arcade showroom, drawn into a rectangle of the UI.
class CarTurntable {
public:
    CarTurntable();
    ~CarTurntable();
    CarTurntable(const CarTurntable&) = delete;
    CarTurntable& operator=(const CarTurntable&) = delete;
    // Shows `car` (database label) with paint index `paint`; the model loads once the
    // choice has rested a few frames (as the original's showroom waits for its load).
    void draw(App& app, const SDL_FRect& dst, const data::CarDatabase& db, const PickerCar& car, int paint,
              PickerArt& art);

private:
    struct State;
    std::unique_ptr<State> s_;
};

class CarPicker {
public:
    CarPicker();
    ~CarPicker();
    enum class Result { None, Chosen, Cancelled };
    // `art`, `cars` and `db` must outlive the picker while it is open. `note(i, paint)` gives a
    // warning for a car (the lobby's rules), "" when fine. `allowed(i)` (optional): whether the
    // car may be chosen; the pages then offer to list only those.
    void open(App& app, PickerArt& art, const data::CarDatabase& db, const std::vector<PickerCar>& cars, int selected,
              int paint, std::function<std::string(int, int)> note, std::function<bool(int)> allowed = {});
    Result update(App& app);
    void render(App& app, Ui& ui);
    void event(const SDL_Event& e) { pointer_.event(e); }
    bool clickable(float x, float y) const { return hits_.at(x, y) != nullptr; } // Screen::clickable
    // The cars allowed changed (the lobby's rules or course): lists them again.
    void refresh(App& app) {
        if (cars_) regroup(app);
    }
    void show_groups() { in_group_ = false; } // the maker / class page instead of the car's own
    int car() const { return car_; }
    int paint() const { return paint_; }
    // The class page instead of the maker page; only the allowed cars. Kept between opens.
    bool by_class() const { return by_class_; }
    void set_by_class(bool on) { by_class_ = on; }
    bool allowed_only() const { return allowed_only_; }
    void set_allowed_only(bool on) { allowed_only_ = on; }

    // The paints of a car: its own for a garage car, else its model's list (one unnamed
    // grey paint when unknown).
    std::vector<PaintInfo> paints(App& app, const PickerCar& c) const;

private:
    struct Group {
        std::string maker, title; // maker "" = the garage (or a class)
        std::vector<int> cars;    // indices into cars_
        int region = -1;          // dealer region (PickerArt::maker_region)
        int cls = -1;             // class page: 1..5, 0 = the garage; -1 on the maker page
    };
    enum Hit { kTile, kTab, kFilter, kStrip, kStripArrow, kPaint, kFooter };
    enum Footer { kChoose, kBack, kToggleFilter, kSwitchView };
    void build_groups(App& app);
    // Rebuilds the groups (view or filter changed) and finds the focused car again.
    void regroup(App& app);
    void enter_group(int g, int pos);
    bool filtering() const { return allowed_ && allowed_only_; }
    void set_pos(App& app, int pos);
    void set_paint(App& app, int paint);
    Result pointer_actions(App& app);
    Result footer_action(App& app, int which);
    void render_groups(App& app, Ui& ui);
    void render_cars(App& app, Ui& ui);
    void render_tabs(App& app, Ui& ui, float y);

    const data::CarDatabase* db_ = nullptr;
    const std::vector<PickerCar>* cars_ = nullptr;
    std::function<std::string(int, int)> note_;
    std::function<bool(int)> allowed_;
    std::vector<Group> groups_;
    bool by_class_ = false, allowed_only_ = true;
    bool has_classes_ = false; // a car in a class list: the class page is offered
    bool in_group_ = false;
    int group_ = 0, pos_ = 0; // focused group, focused car within it
    int car_ = 0, paint_ = 0;
    int group_scroll_ = 0;  // first row of tiles shown
    float strip_x_ = 0;     // eased strip scroll (UI units)
    float paint_y_ = 0;     // eased paint column scroll (slots)
    int pulse_ = 0;         // frames since the last move (the focused icon's pulse)
    int drag_pos_ = -1;     // the strip's position when a drag along it began (-1: no drag)
    float wheel_ = 0;       // mouse wheel steps not yet used (trackpads scroll in fractions)
    PickerArt* art_ = nullptr;
    CarTurntable turntable_;
    PointerInput pointer_;
    HitList hits_;
};

class CoursePicker {
public:
    enum class Result { None, Chosen, Cancelled };
    // `length_m(label)`: the course length in metres (0 = unknown).
    void open(const std::vector<PickerCourse>& courses, int selected, std::function<u32(const std::string&)> length_m);
    Result update(App& app);
    void render(App& app, Ui& ui, PickerArt& art);
    void event(const SDL_Event& e) { pointer_.event(e); }
    bool clickable(float x, float y) const { return hits_.at(x, y) != nullptr; } // Screen::clickable
    int course() const { return sel_; }

private:
    enum Hit { kStrip, kStripArrow, kFooter };
    void select(App& app, int to);
    const std::vector<PickerCourse>* courses_ = nullptr;
    std::function<u32(const std::string&)> length_m_;
    int sel_ = 0, prev_ = -1;
    float strip_x_ = 0;
    int pulse_ = 0, fade_ = 0; // frames since the last move
    int drag_sel_ = -1;        // the selection when a drag along the strip began
    float wheel_ = 0;
    PointerInput pointer_;
    HitList hits_;
};

// A footer whose actions are buttons: `hint` (the movement keys) at the left, then each action
// ("Cross: choose") as a button from the right edge, highlighted under the pointer. Their
// rectangles go to `hits` as `id` with the action's `arg`.
struct FooterAction {
    std::string label;
    int arg = 0;
};
void footer_actions(Ui& ui, std::string_view hint, const std::vector<FooterAction>& actions, HitList& hits, int id,
                    const PointerInput& pointer);

// The pointer's last position is over `r` (a mouse / VR pointer; hover highlights).
bool hovering(const PointerInput& pointer, const UiRect& r);

// The name of a car's strip icon ("<name>_mini" on ar/select_car): an arcade car's picture
// (GtData::ArcadeCar::image), else its label.
std::string car_icon_name(const PickerCar& c);

// Plays a named menu sound effect (sound/se.inf: "cursor", "ok", "cancel", "disable")
// when the menu SE option is on.
void picker_sound(App& app, const char* name);

// A paint chip: the colour filled, outlined.
void draw_paint_chip(Ui& ui, float x, float y, float w, float h, u32 rgb, bool focused);

} // namespace gt3r
