#pragma once

// VR headsets through OpenXR (Meta Quest; docs/dev/VR.md). Without GT3R_XR none of this is
// compiled.
//
// The game keeps drawing with its 2D renderer, into a canvas (a render target) instead of a
// window: menus, movies and replays show on a large virtual screen in front of the player.
// While driving, the race screen draws a stereo frame instead: each eye's view of the course
// from the head pose the headset reports, into that eye's canvas, and the HUD into the canvas,
// which then floats in front of the car as a transparent panel.
//
// Per frame (App::iterate):
//   begin_frame()           waits for the headset's next frame and locates the eyes
//   ... update, then draw into canvas() and, for a stereo frame, eye(i).canvas ...
//   SDL_RenderPresent()     submits the 2D renderer's work
//   end_frame()             copies the canvases into the headset's swapchains and submits
//                           the layers (the screen, or the eyes plus the HUD panel)

#if GT3R_XR

#include "base/bytes.h"
#include "render/math3d.h"

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <vector>

namespace gt3r {
class IniDocument;
}

namespace gt3r::xr {

// A rigid transform: position (metres) and a unit quaternion.
struct Pose {
    render::Vec3 pos;
    float qw = 1, qx = 0, qy = 0, qz = 0;
    render::Mat4 matrix() const { return render::translate(pos) * render::rotate_quat(qw, qx, qy, qz); }
};

// One eye of the current frame.
struct Eye {
    Pose pose; // in the seated space: origin at the player's head when centred, -z ahead, y up
    // Field of view as the tangents of the half angles (left and down negative).
    float tan_left = -1, tan_right = 1, tan_down = -1, tan_up = 1;
    int width = 0, height = 0;     // canvas size in pixels
    SDL_Texture* canvas = nullptr; // render target of the 2D renderer, copied to the headset
    render::Mat4 projection(float znear, float zfar) const {
        return render::frustum(tan_left, tan_right, tan_down, tan_up, znear, zfar);
    }
};

// [vr] in config.ini (Settings > VR).
struct Settings {
    bool stereo = true;          // stereo = on: races in 3D; off: on the virtual screen
    bool level_horizon = false;  // level_horizon = on: the in-car view ignores the car's pitch and roll
    bool show_car = false;       // car_body = on: the own car's body around the in-car view
    bool wheel = false;          // steering = wheel: both grips held, turn the controllers like a wheel
    int wheel_degrees = 180;     // wheel_degrees: rotation from full left to full right
    float screen_distance = 2.4f, screen_width = 2.6f; // menus' virtual screen (metres)
    float hud_distance = 1.3f, hud_width = 1.5f;       // the race HUD panel in front of the car
    float hud_drop = 0.12f;                            // HUD panel centre below eye level
    int refresh_hz = 90;      // refresh = 72 | 80 | 90 | 120 (what the headset offers)
    float eye_scale = 1.0f;   // resolution = 0.7 .. 1.3 of the headset's recommended size
    static Settings load(const IniDocument& cfg);
    void save(IniDocument& cfg) const;
};

class Session {
public:
    // Properties for SDL_CreateGPUDeviceWithProperties that create the device through the
    // headset's OpenXR runtime (instance and system written back on success), with the
    // optional extensions the runtime supports. False when no OpenXR loader or runtime exists.
    struct DeviceRequest {
        u64 instance = 0, system = 0;
        std::vector<std::string> extensions;
        std::vector<const char*> names;
    };
    static bool prepare_device(SDL_PropertiesID props, DeviceRequest& req);

    // The session for a device created with prepare_device's properties. `renderer` is the
    // device's offscreen 2D renderer (canvases are its render targets).
    static std::unique_ptr<Session> create(SDL_GPUDevice* device, SDL_Renderer* renderer, const DeviceRequest& req,
                                           const Settings& settings);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Events: session state changes, the runtime asking the app to quit, lost focus.
    void poll();
    bool running() const;       // frames are being produced (the session has begun)
    bool focused() const;       // input reaches the app (not the system menu)
    bool exit_requested() const;
    // True once after the session lost focus (the system menu opened): races pause.
    bool take_focus_lost();

    // Waits for the next display frame and locates the eyes. False when no frame should be
    // made now (session not running): sleep briefly and skip rendering.
    bool begin_frame();
    // After SDL_RenderPresent: hands this frame's canvases to the compositor.
    void end_frame();
    // Seconds between the headset's frames (the display refresh).
    double frame_interval() const;
    // Predicted display time of the current frame (XrTime, nanoseconds).
    s64 display_time() const;

    // The 2D canvas: the game's 640x360 UI space at 2x (render target of the 2D renderer).
    SDL_Texture* canvas() const { return canvas_; }
    int canvas_width() const { return canvas_w_; }
    int canvas_height() const { return canvas_h_; }

    // A stereo frame this frame: eyes drawn into eye(i).canvas, the canvas holds the HUD over a
    // transparent background. Call before end_frame(); otherwise the canvas is the screen.
    void submit_stereo() { stereo_ = true; }
    bool stereo_submitted() const { return stereo_; }
    bool eyes_valid() const { return eyes_valid_; } // the eye poses of this frame are tracked
    const Eye& eye(int i) const { return eyes_[i]; }
    // The head (between the eyes) in the seated space, this frame.
    Pose head() const;

    // Re-centres the seated space on the head (its position and heading) at the next frame.
    void recenter() { recenter_pending_ = true; }

    const Settings& settings() const { return settings_; }
    void set_settings(const Settings& s);
    // Display refresh rates the headset offers (empty: fixed rate).
    const std::vector<float>& refresh_rates() const;

    // Controllers (xr_input.cpp): reads the actions into the virtual gamepad. With
    // `pointer_allowed` (menus, not driving) the right controller also points at the virtual
    // screen: while it does, its trigger clicks there instead of acting as the pad's trigger.
    void update_input(double dt, bool pointer_allowed);
    void rumble(float low, float high, Uint32 ms);
    // Where the pointer hits the screen this frame, (u, v) in 0..1 from the top left.
    bool pointer(float& u, float& v, bool& pressed) const;
    // The pointer is over something the page takes clicks on (App::xr_pointer, each frame): A
    // pressed there clicks as the trigger does instead of being the pad's A.
    void set_a_clicks(bool on) { a_clicks_ = on; }

    struct Impl;

private:
    Session() = default;
    std::unique_ptr<Impl> impl_;
    Settings settings_;
    SDL_Texture* canvas_ = nullptr;
    int canvas_w_ = 1280, canvas_h_ = 720;
    Eye eyes_[2];
    bool eyes_valid_ = false;
    bool stereo_ = false;
    bool hud_panel_ = false; // the last frame showed the canvas as the race's HUD panel (not the screen)
    bool pointer_on_ = false, pointer_pressed_ = false;
    float pointer_u_ = 0, pointer_v_ = 0;
    bool pull_ = false, pull_free_ = false; // the trigger pulled; the ray left where the pull began
    float pull_u_ = 0, pull_v_ = 0;
    bool trigger_down_ = false;
    bool a_clicks_ = false, a_held_ = false, a_click_ = false; // a_click_: this A press clicks
    bool recenter_pending_ = false;
};

} // namespace gt3r::xr

#endif // GT3R_XR
