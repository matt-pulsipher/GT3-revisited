#pragma once

// Ours: the player's own race cameras (Settings -> Cameras). Each is a view placed relative to
// the player's car in the camera editor (a photo mode over a frozen race) and, while it is
// enabled, joins change view after the built-in views. The cameras live in config.ini, one
// section per camera in rotation order:
//
//   [custom_camera1]
//   id = 3                  ; stable number (the view a race remembers)
//   name = Hood
//   enabled = yes           ; in the change-view rotation
//   mount = bonnet          ; body: fixed to the car; follow: trails its turns, level;
//                           ; bonnet: on the bonnet, seated by the car's own shape
//   x = 0  y = 0.1  z = -1.2      ; the eye from the driver's eye, metres (x right, y up, z back);
//                           ; a bonnet mount's nudge: sideways, up, fore/aft
//   yaw = 0  pitch = -4  roll = 0 ; degrees: turned right, up, rolled clockwise
//   roll_lock = 0           ; 0..100: how much of the body's roll the view levels out
//                           ; (body and bonnet mounts; follow is always level)
//   fov = 48                ; vertical field of view, degrees
//   car = shown             ; shown / hidden / ahead: the player's car drawn, not drawn,
//                           ; drawn ahead only (hidden while looking back)
//   hud = chase             ; chase / incar: the HUD layout
//   mirror = off            ; on / off: the rear-view mirror
//   mirror_x = 0.5  mirror_y = 0.116  mirror_size = 1 ; its centre (share of the picture) and size
//
// A camera file (Settings -> Cameras: Export / Import) is the same sections with a [gt3r_cameras]
// header (`version = 1`); a whole config.ini imports too. The editor's moves (orbit, look, pan) are
// here as well, free of the race and the screen.

#include "base/bytes.h"
#include "render/math3d.h"

#include <string>
#include <string_view>
#include <vector>

namespace gt3r {
class IniDocument;
}

namespace gt3r::race {

struct CustomCamera {
    // How the camera holds on to the car: fixed to its body (it pitches and rolls with the car,
    // as the in-car view does), following its heading smoothly and level (as the chase view),
    // or seated on the bonnet at the place the body's own shape gives (race/hood_camera.h).
    enum class Mount : u8 { Body, Follow, Hood };

    // Whether the player's car body is drawn: always, never (the in-car look), or only facing
    // forward — hidden while looking back or turned past a right angle, so a bonnet camera's
    // look-back is not the inside of the cabin.
    enum class OwnCar : u8 { Shown, Hidden, Ahead };

    static constexpr float kMinFov = 15, kMaxFov = 100;      // degrees
    static constexpr float kMaxPitch = 89, kMaxRoll = 90;    // degrees
    static constexpr float kMaxDistance = 60;                // metres from the driver's eye
    static constexpr float kMinMirror = 0.4f, kMaxMirror = 3.0f;
    // GT3's mirror (presentation §H2): 240 x 64 at (200, 20) of the 640 x 448 picture.
    static constexpr float kMirrorX = 0.5f, kMirrorY = 52.0f / 448.0f;
    static constexpr int kMaxCameras = 32;
    static constexpr size_t kMaxName = 40;

    int id = 0; // > 0
    std::string name;
    bool enabled = true;
    Mount mount = Mount::Follow;
    // The eye from the driver's eye (the car's in-car camera point), metres in the mount's frame:
    // x right, y up, z back (the car model's axes). A bonnet mount instead reads them as nudges
    // from its own mounts: x sideways, y up, z fore/aft (hood_camera_eye; the editor's Fore/aft,
    // Sideways and Height sliders — clamp() keeps the nudge inside the mounts' bounds).
    render::Vec3 offset{0, 0.7f, 6.0f};
    float yaw = 0, pitch = 0, roll = 0; // degrees: turned right, up, rolled clockwise
    float roll_lock = 0;                // 0..1: the share of the body's roll the view levels out
    float fov = 47.9f;                  // vertical, degrees (GT3's player views: 47.9)
    OwnCar car = OwnCar::Shown;         // the player's car drawn: always, never, or facing forward only
    bool incar_hud = false;             // the in-car view's HUD layout, else the chase views'
    bool mirror = false;
    float mirror_x = kMirrorX, mirror_y = kMirrorY, mirror_size = 1.0f;

    // Every value within its range (a hand-edited config, the editor's moves).
    void clamp();
    // The camera's view direction and up in the mount's frame: turned by yaw, then pitched, then rolled.
    void axes(render::Vec3& forward, render::Vec3& up) const;
    float fovy() const { return fov * 3.14159265f / 180.0f; }
    bool operator==(const CustomCamera& o) const;
};

// The cameras in rotation order ([custom_camera<n>] sections; nameless or broken ones are skipped).
std::vector<CustomCamera> load_custom_cameras(const IniDocument& cfg);
// Writes `cams` as the only [custom_camera<n>] sections.
void save_custom_cameras(IniDocument& cfg, const std::vector<CustomCamera>& cams);
// An id no camera has, and the next "Camera <n>" name not taken.
int new_custom_camera_id(const std::vector<CustomCamera>& cams);
std::string new_custom_camera_name(const std::vector<CustomCamera>& cams);
// The camera with `id` in `cams`, or null.
const CustomCamera* find_custom_camera(const std::vector<CustomCamera>& cams, int id);

// A camera file's text, and the cameras in one (none: not a camera file, or no cameras in it).
std::string export_custom_cameras(const std::vector<CustomCamera>& cams);
std::vector<CustomCamera> import_custom_cameras(std::string_view text);
// `in` added after `cams` with new ids (a name taken gets " 2", " 3", ...), as many as there is room
// for (CustomCamera::kMaxCameras); how many were added.
int add_custom_cameras(std::vector<CustomCamera>& cams, const std::vector<CustomCamera>& in);
// A file name for exporting `name` ("Hood" -> "Hood.ini"): the characters file systems refuse dropped.
std::string camera_file_name(std::string_view name);

// The editor's moves, in the mount's frame (metres and radians):
// orbit: round `pivot` (an offset: the car's middle from the driver's eye), `az` to the right of the
// car and `el` up, the view turning with it so the car stays where it is in the picture;
// look: the view turned right and up in place; pan: the eye moved along the view's right, up and
// forward axes.
void orbit_camera(CustomCamera& c, const render::Vec3& pivot, float az, float el);
void look_camera(CustomCamera& c, float yaw, float pitch);
void pan_camera(CustomCamera& c, float right, float up, float forward);

} // namespace gt3r::race
