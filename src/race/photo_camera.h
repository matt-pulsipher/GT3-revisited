#pragma once

// Photo mode (ours; GT3's replays have no free camera): from a paused replay's Select list, the
// camera leaves the replay's shots and flies freely round the frozen moment, steered by the pad.
// The pad (the menu pad's DualShock 2 buttons, as the camera editor's, app/camera_editor.h): the
// left stick moves level (forward / back, left / right, as a drone: looking down does not dive),
// the right stick turns, L2 / R2 move down / up, L1 / R1 zoom out / in, Triangle held with L1 / R1
// rolls left / right instead, L3 (the left stick pressed) toggles a faster move. The turn slows as
// the view zooms in, so a long lens can still be aimed. (Square focuses at the crosshair and Select
// shows the camera menu: the race screen's, app/screen_race_photo.cpp.)
//
// The eye stays within kRange of the moment's followed car and kClearance over the road below it
// (when the course has one there). Once the pad has been left alone for kStillTime the camera is
// still(): the picture is then built from kPhotoSamples renders, one a frame, each at its own
// time while the shutter is open (the cars, wheels and sparks drawn where they were then, between
// ticks as between frames) and nudged by its own fraction of a pixel (photo_sample): their mean
// has the motion blur of that shutter, and smooth edges. The shutter closes at the moment shown
// and opens shutter_seconds() before it (1/1000 s to 1/8 s; race/photo_history.h keeps the ticks
// before the paused one). What the camera does meanwhile (PhotoMode, photo_view) follows from the
// camera menu's follow setting (PhotoFollow, photo_follow_mode): Roadside stands by the road, and
// pans with the car under the crosshair (the car sharp, the world streaked) or, with none there,
// stays put (the moving cars blur); Fixed to car moves and turns with a car as if bolted to it.
// Changing either starts the photo again.
//
// SDL-free (tests).

#include "race/procedural_cameras.h"
#include "render/math3d.h"

#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace gt3r::race {

constexpr int kPhotoSamples = 64; // renders in a photo

// What the camera does while the shutter is open: stays put, stays put turning with a car, or
// moves and turns with a car.
enum class PhotoMode : u8 { Free, Pan, Fixed };

// The camera menu's follow setting (Roadside the default), and the mode it gives: Roadside pans
// with the car under the crosshair (`car_hit`) and stays put without one; Fixed to car is Fixed.
enum class PhotoFollow : u8 { Roadside, FixedToCar, Count };
const char* photo_follow_name(PhotoFollow f);
PhotoMode photo_follow_mode(PhotoFollow f, bool car_hit);

// The photo's frame (the camera menu's Aspect): the picture rendered at this shape, centred in the
// screen with black round it; the saved PNG has it too.
enum class PhotoAspect : u8 { Wide, Standard, Count }; // 16:9, 4:3
const char* photo_aspect_name(PhotoAspect a);
float photo_aspect_ratio(PhotoAspect a); // width / height (landscape)
// The frame's orientation (the camera menu's Orientation): landscape, the aspect as it reads, or
// portrait, turned upright (16:9 becomes 9:16, 4:3 becomes 3:4).
enum class PhotoOrientation : u8 { Landscape, Portrait, Count };
const char* photo_orientation_name(PhotoOrientation o);
float photo_frame_ratio(PhotoAspect a, PhotoOrientation o); // the frame's width / height

// The camera menu's grid over the frame (never in the photo): none, the rule of thirds, the
// square diagonals (45 degrees from each corner, the diagonals of the squares at the frame's ends,
// across a portrait frame's width)
// or the golden triangle (a corner-to-corner diagonal, and from the other two corners the
// perpendiculars to it).
enum class PhotoGrid : u8 { Off, Thirds, Diagonals, GoldenTriangle, Count };
const char* photo_grid_name(PhotoGrid g);
// Its lines in the frame: {x0, y0, x1, y1} each, 0..1 across and down (0, 0 the top left), for a
// frame `aspect` wide to 1 high.
std::vector<std::array<float, 4>> photo_grid_lines(PhotoGrid g, float aspect);

// The depth of field (ours): each render looks through its own point of a thin lens (PhotoSample's
// lens_x / lens_y over the aperture's radius) at the same plane of focus, so the mean is sharp at
// the focus distance and blurs before and behind it as a camera's lens does. The lens is a real one
// on a 35 mm (full-frame) camera: its focal length from the field of view (photo_focal_mm: the
// frame's diagonal over the 43.27 mm diagonal of a 36 x 24 mm sensor), its aperture that over the
// f-number (photo_aperture_radius). The camera menu's Aperture picks the f-number.
constexpr float kFStops[] = {1.4f, 2.0f, 2.8f, 4.0f, 5.6f, 8.0f, 11.0f, 16.0f, 22.0f};
constexpr int kFStopCount = int(sizeof kFStops / sizeof kFStops[0]);
constexpr int kDefaultFStop = 4; // f/5.6
// The 35 mm equivalent focal length (mm) of a view `fovy` radians high and `aspect` wide to 1 high.
float photo_focal_mm(float fovy, float aspect);
// The aperture's radius in metres: focal length / f-number / 2.
float photo_aperture_radius(float focal_mm, float f_number);
// A render through the lens point (`dx`, `dy`) metres off its centre (the view's right and up):
// the eye moved there and the view with it, and the projection's shear (`proj` times it) that keeps
// the plane `focus` metres ahead where it was in the picture.
struct PhotoLens {
    render::Vec3 eye;
    render::Mat4 view, shear;
};
PhotoLens photo_lens(const render::Vec3& eye, const render::Mat4& view, float dx, float dy, float focus);

// The shutter speeds offered: 1 / these seconds.
constexpr int kShutterDenominators[] = {1000, 500, 250, 125, 60, 30, 15, 8};
constexpr int kShutterCount = int(sizeof kShutterDenominators / sizeof kShutterDenominators[0]);
constexpr int kDefaultShutter = 4; // 1/60 s

// The shutter's times (PhotoHistory's: ticks from the newest state): closing at `moment`, the time
// shown, and opening `length` ticks before it, or at `earliest` (the oldest state kept) if that is
// later.
struct PhotoShutter {
    float open = 0, close = 0;
};
PhotoShutter photo_shutter(float moment, float length, float earliest);

// A render's eye and view: the photo's own (`eye`, `view`) for Free; turned about the eye by the
// shortest rotation that takes the followed car's place in the view at the moment shown
// (`car_ref`, its world matrix) to its place at the render's time (`car_at`) for Pan, so the car
// stays where it was in the picture; carried rigidly with the car from `car_ref` to `car_at` for
// Fixed.
struct PhotoView {
    render::Vec3 eye;
    render::Mat4 view;
};
PhotoView photo_view(PhotoMode m, const render::Vec3& eye, const render::Mat4& view, const render::Mat4& car_ref,
                     const render::Mat4& car_at);

// The i-th of n renders: its time (between the shutter's open and close) and its offset within a
// pixel (-0.5..0.5 each way). The times take the shutter's n strata in an order (bit reversal) that
// spreads every first k of them over the whole shutter, so the mean shown while it builds already
// has the whole blur; each lies at its own place in its stratum. The offsets are a Halton sequence
// (bases 3 and 5), unrelated to the times.
struct PhotoSample {
    float t = 0;
    float dx = 0, dy = 0;
    float lens_x = 0, lens_y = 0; // its place on the lens: within the unit disc (right, up)
};
PhotoSample photo_sample(int i, int n, const PhotoShutter& s);

// One frame's pad, read and dead-zoned by the race screen.
struct PhotoInput {
    float move_x = 0, move_y = 0; // the left stick: right / forward (-1..1)
    float turn_x = 0, turn_y = 0; // the right stick: turn right / up (-1..1)
    float rise = 0;               // R2 (+1) up, L2 (-1) down
    float zoom = 0;               // R1 (+1) in, L1 (-1) out
    float roll = 0;               // Triangle with R1 (+1) right, with L1 (-1) left
    bool fast = false;            // L3's toggle
    bool any() const {
        return move_x != 0 || move_y != 0 || turn_x != 0 || turn_y != 0 || rise != 0 || zoom != 0 || roll != 0;
    }
};

class PhotoCamera {
public:
    static constexpr double kStillTime = 1.0; // s without input before the camera counts as still
    static constexpr float kSpeed = 6.0f;      // m/s at full stick
    static constexpr float kRiseSpeed = 3.0f;  // m/s, L2 / R2
    static constexpr float kFastScale = 5.0f;  // L3's toggle
    static constexpr float kRollRate = 0.8f;   // rad/s (about 45 degrees)
    static constexpr float kMaxRoll = 1.5708f; // a quarter turn either way
    static constexpr float kTurnRate = 1.75f;  // rad/s at full stick (about 100 degrees), at kBaseFovy
    static constexpr float kBaseFovy = 0.8f;   // the zoom the turn rate is for (the chase views' 0.78..0.9)
    static constexpr float kZoomRate = 0.9f;   // the field of view shrinks / grows by e^this a second
    static constexpr float kMinFovy = 0.1f, kMaxFovy = 1.75f; // about 6 to 100 degrees
    static constexpr float kMaxPitch = 1.5f;   // about 86 degrees up or down
    static constexpr float kRange = 200.0f;    // m from the anchor (the followed car)
    static constexpr float kClearance = 0.3f;  // m over the road

    // From the view on screen (its eye, view matrix and vertical field of view; a roll is dropped,
    // the camera starts level)
    // round `anchor`.
    void begin(const render::Vec3& eye, const render::Mat4& view, float fovy, const render::Vec3& anchor);
    void set_ground(GroundHeight g) { ground_ = std::move(g); }

    // One frame. True when the view changed.
    bool update(const PhotoInput& in, double dt);
    // Touch (TouchGesture): turned by `yaw` (+ left) and `pitch` (+ up) radians at once and the
    // field of view divided by `zoom` (> 1 nearer), held to their ranges. True when the view changed.
    bool aim(float yaw, float pitch, float zoom);
    // Something else changed the picture (a menu press): the still time starts again.
    void wake() { idle_ = 0; }
    // The follow setting and shutter (each change wakes the camera); `dir` +1 the next, -1 the last
    // (the follow setting goes round, the shutter stops at its ends).
    PhotoFollow follow() const { return follow_; }
    void step_follow(int dir);
    int shutter() const { return shutter_; } // into kShutterDenominators
    void step_shutter(int dir);
    float shutter_seconds() const { return 1.0f / float(kShutterDenominators[shutter_]); }

    render::Vec3 eye() const { return eye_; }
    render::Vec3 forward() const;
    render::Mat4 view() const;
    float fovy() const { return fovy_; }
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
    float roll() const { return roll_; } // + right (clockwise as the camera sees it)
    double idle() const { return idle_; }
    bool still() const { return idle_ >= kStillTime; }

private:
    render::Vec3 eye_{}, anchor_{};
    float yaw_ = 0, pitch_ = 0, roll_ = 0; // yaw 0 looks along -z, positive turns left (about +y)
    float fovy_ = kBaseFovy;
    double idle_ = 0;
    PhotoFollow follow_ = PhotoFollow::Roadside;
    int shutter_ = kDefaultShutter;
    GroundHeight ground_;
};

} // namespace gt3r::race
