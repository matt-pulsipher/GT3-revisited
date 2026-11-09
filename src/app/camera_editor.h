#pragma once

// Settings -> Cameras: the camera editor, a photo mode for the player's own race cameras
// (race/custom_cameras.h). The race screen freezes a moment of a race and draws it through the
// camera being edited (screen_race_camera_editor.cpp); this is the overlay and its controls,
// on two pages:
//   Camera: place the view by rotating it round the car or in place, moving it and moving it
//     nearer or farther; field of view, roll, how it holds on to the car, the HUD layout and
//     whether the car is drawn; a built-in view of the car to start from.
//   Mirror: show the rear-view mirror, move it on the picture and size it.
//
// Pointer: one finger or the left button rotates (round the car or in place, as the panel
// says; the right button the other way); two fingers, the middle button or Shift + the left
// button move the camera; a pinch or the wheel moves it nearer or farther (Ctrl + wheel: the
// field of view). On the Mirror page a drag moves the mirror, a pinch or the wheel sizes it.
// Pad: the left stick moves the camera (the mirror), the right stick rotates it, L2 / R2 move
// it down / up (size the mirror); the D-pad picks the panel's rows and values, L1 / R1 the
// page, Select hides the panel, Start saves, Triangle leaves (asking first when changed).

#include "app/ui.h"
#include "race/custom_cameras.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace gt3r {

class App;

class CameraEditor {
public:
    enum class Result : u8 { None, Save, Cancel };
    // The views a camera can start from (CameraRig::preset; kPresetBonnet is a Mount::Hood
    // camera, built in screen_race_camera_editor.cpp), in the panel's order.
    static constexpr const char* kPresetNames[] = {"Chase", "Far chase", "Bumper", "Roof", "Bonnet"};
    static constexpr int kPresets = 5;
    static constexpr int kPresetBonnet = 4;

    // What the race screen tells the editor every frame.
    struct Scene {
        UiRect frame;       // the 3D picture
        UiRect mirror;      // where the mirror is (or would be) drawn
        render::Vec3 pivot; // the car's middle, as an offset (race::orbit_camera)
        float min_y = -1e9f; // the lowest eye (an offset's y): a little above the road under the car
    };

    // `preset(i)`: kPresetNames[i] of the car in the scene, as a camera.
    CameraEditor(race::CustomCamera cam, bool is_new, std::function<race::CustomCamera(int)> preset);

    const race::CustomCamera& camera() const { return cam_; }
    // A new camera starts as the car's chase view (preset 0), once the scene's car is there.
    void begin();
    void event(const SDL_Event& e, const Scene& scene);
    Result update(App& app, const Scene& scene, double dt);
    void draw(Ui& ui, const Scene& scene);
    bool clickable(float x, float y) const;
    // The picture without the editor (Hide): the race's own look.
    bool hidden() const { return hidden_; }

private:
    enum class Page : u8 { Camera, Mirror };
    enum class Drag : u8 { None, Ui, Scene, Two, Ignore };
    enum Hit : int { kNone = 0, kTabCamera, kTabMirror, kHide, kShow, kCancel, kSave, kRow, kKeep, kLeave };
    struct Row {
        enum class Kind : u8 { Choice, Slider, Action } kind = Kind::Choice;
        std::string label, value;
        float frac = 0;
        std::function<void(int)> change;     // Left / Right, the arrows
        std::function<void(float)> set_frac; // a slider's pointer position, 0..1
        std::function<void()> activate;      // Cross, a tap on the label
    };
    struct Pointer {
        u64 id = 0;
        float x = 0, y = 0;
    };

    void build_rows();
    void layout(float w, float h, bool touch);
    Hit hit(float x, float y, int* row = nullptr) const;
    void tap(Hit h, int row, float x);
    void leave_request();
    // The scene's moves: a one-finger / mouse drag by (dx, dy) units, `around` round the car (else
    // in place); two fingers' pan and pinch; the wheel.
    void rotate(const Scene& s, float dx, float dy, bool around);
    void pan(const Scene& s, float dx, float dy);
    void dolly(const Scene& s, float amount); // + nearer (towards the view), a share of the distance
    void move_mirror(const Scene& s, float dx, float dy);
    void keep_above_road(const Scene& s) { cam_.offset.y = std::max(cam_.offset.y, s.min_y); }
    float pivot_distance(const Scene& s) const;
    void pad_input(App& app, const Scene& s, double dt);

    race::CustomCamera cam_, start_;
    // The eye and aim kept for the position mounts (body/follow: metres from the driver's eye,
    // degrees of turn) while the camera sits on the bonnet — a position and a bonnet nudge don't
    // translate, and the bonnet always starts at its default centred seat aimed ahead.
    render::Vec3 pos_offset_{0, 0.7f, 6.0f};
    float pos_yaw_ = 0, pos_pitch_ = 0, pos_roll_ = 0;
    void keep_mount_offset() {
        if (cam_.mount == race::CustomCamera::Mount::Hood) return;
        pos_offset_ = cam_.offset, pos_yaw_ = cam_.yaw, pos_pitch_ = cam_.pitch, pos_roll_ = cam_.roll;
    }
    bool is_new_ = false;
    std::function<race::CustomCamera(int)> preset_;
    Page page_ = Page::Camera;
    bool around_ = true; // the main drag rotates round the car (else in place)
    bool hidden_ = false;
    int preset_sel_ = 0;
    std::vector<Row> rows_;
    int sel_ = 0;
    bool confirm_ = false;
    int confirm_sel_ = 0; // 0 keep editing, 1 leave
    Result pending_ = Result::None;

    // Pointer state.
    std::vector<Pointer> ptrs_;
    Drag drag_ = Drag::None;
    Hit press_hit_ = kNone;
    int press_row_ = -1;
    bool slider_ = false;
    bool pan_drag_ = false; // the left button with Shift: move
    float spread_ = 1, cx_ = 0, cy_ = 0;
    int mouse_button_ = 0; // right / middle held over the picture
    float mouse_x_ = 0, mouse_y_ = 0;

    // Pad state: D-pad edges and repeats (the left stick is not the D-pad here).
    u16 dpad_prev_ = 0;
    double repeat_[4] = {};

    // Layout (UI units), from the last draw.
    struct Button {
        UiRect r;
        Hit id;
        std::string label;
    };
    std::vector<Button> buttons_;
    UiRect bar_, panel_, keep_, leave_;
    float row_h_ = 20, rows_y_ = 0, val_x_ = 0, val_w_ = 0, slider_w_ = 0, label_x_ = 0;
    bool touch_ = false;
};

} // namespace gt3r
