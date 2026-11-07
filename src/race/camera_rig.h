#pragma once

// Race cameras: GT3-style chase camera (lags behind the car's heading),
// bumper and roof cameras (rigid to the body), and a free orbit around the
// car for debugging (right stick / mouse orbit, L2/R2 zoom).

#include "input/virtual_pad.h"
#include "race/procedural_cameras.h"
#include "race/replay_cameras.h"
#include "race/sound_view.h"
#include "render/math3d.h"
#include "sim/sim_types.h"

#include <SDL3/SDL_events.h>

#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace gt3r::race {

enum class CameraMode : u8 { Chase, ChaseFar, Bumper, Roof, Orbit, Tv, Count };

// A trackside replay camera: fixed position, covers the car while it is near
// track distance `s` (metres along the lap).
struct TvCamera {
    render::Vec3 position;
    float s = 0;
    float zoom = 1; // > 1 narrows the field of view
};
const char* camera_mode_name(CameraMode m);

// GT3's pre-race shot (presentation §B3), by C = frames left to the start signal of a
// standing-start hold: shown in every live view while C is 60 or more, at
// t = 1 - (C - 60) / 600, with a 1 s dissolve from the last picture when C - 60 is 120 or 60
// (the shot's own cuts, t 0.8 and 0.9) and 0 (the cut to the player's view).
constexpr bool start_shot_on(int frames_to_signal) { return frames_to_signal >= 60; }
constexpr float start_shot_t(int frames_to_signal) { return 1.0f - float(frames_to_signal - 60) / 600.0f; }
constexpr bool start_shot_dissolve(int frames_to_signal) {
    return frames_to_signal == 180 || frames_to_signal == 120 || frames_to_signal == 60;
}

class CameraRig {
public:
    void set_mode(CameraMode m);
    CameraMode mode() const { return mode_; }
    // R1 (change view). GT3 has two player views, in-car (bumper, with the rear-view mirror) and
    // chase (its camera point 1; points 1 and 2 are the same in every car). We add the bumper
    // view without the mirror and a far chase: bumper -> bumper (no mirror) -> chase -> far chase.
    // With gt3_views only GT3's two alternate. Other modes (debug orbit, roof) go to the chase.
    void next_mode();
    void set_gt3_views(bool on) { gt3_views_ = on; }
    // GT3's in-car camera vibration (on by default; [race] camera_shake = off disables it).
    void set_shake(bool on) { shake_on_ = on; }
    // The player's view (mode + mirror), e.g. restored from the last race.
    void set_view(CameraMode m, bool mirror) { set_mode(m), mirror_ = mirror; }
    // Whether the onboard view shows the rear-view mirror.
    bool mirror() const { return mirror_ || gt3_views_; }
    // The car's camera points 0 (in-car) and 1 (chase) from its GTCI block: the eye sits at -p in
    // the car model's frame (x right, y up, z back; origin at the wheelbase midpoint on the body's
    // lowest point). Defaults are GT3's fallbacks.
    void set_car_points(const render::Vec3& p0, const render::Vec3& p1) { point_[0] = p0, point_[1] = p1; }
    // L1 held: look back (heading + pi, pitch negated; ours, the far chase and roof, turned
    // round too); no mirror while held.
    void set_look_back(bool on) { look_back_ = on; }
    // Ours: looking around (the race pad's right stick, RX / RY; -1..1, y towards the
    // player). The stick's direction is where the view turns (right: 90 degrees right, pulled
    // back: behind, pushed up: ahead), eased there and back to the front when let go. In-car the
    // view turns about the car's up axis with the eye in place; the chase views go round the car.
    // Look back takes over while held.
    void set_look(float x, float y) { look_x_ = x, look_y_ = y; }
    // The look's turn as of the last update(): radians, + right (past pi on the side it turned to).
    float look_yaw() const { return look_; }
    // The onboard view shows the rear-view mirror now: not looking back or turned to the side.
    bool shows_mirror() const { return onboard() && mirror() && !look_back_ && std::fabs(look_) <= kLookMirror; }
    static constexpr float kLookStart = 0.2f, kLookFull = 0.5f; // the stick's reach: turn from, full turn at
    static constexpr float kLookRate = 14.0f;                   // the ease (1/s; 95 % in about 0.2 s)
    static constexpr float kLookMirror = 0.35f;                 // turned past this (radians): no mirror
    // GT3's pre-race shot (starting camera, cameras/start.cam): while `on`, the camera follows the
    // file's record 0 in the car's frame as t goes 0 -> 1 (eye, target and zoom curves).
    void set_intro(bool on, float t) { intro_ = on, intro_t_ = t; }
    // cameras/start.cam's record (without it: a straight move from 30 m ahead to the front corner).
    void set_start_shot(std::optional<GtrcRecord> r) { start_shot_ = std::move(r); }
    bool intro() const { return intro_; }
    bool look_back() const { return look_back_; }
    // Which camera the view is on: its mode, the replay's TV camera, director record and procedural cut, the pre-race shot, the
    // look back. A change is a camera cut (the sparks drop their streaks then, presentation §L7).
    struct Shot {
        CameraMode mode = CameraMode::Bumper;
        int tv = -1, record = -1, serial = -1;
        bool intro = false, back = false;
        bool operator==(const Shot&) const = default;
    };
    Shot shot() const { return {mode_, tv_active_, director_record_, proc_serial_, intro_, look_back_}; }

    // Snap the smoothed chase state to the car (after reset/respawn).
    void snap(const sim::VehicleState& s);
    // Advance smoothing and orbit controls; `s` is the interpolated render state.
    void update(const sim::VehicleState& s, const sim::VehicleSetup& setup, const PadState& pad, double dt);
    bool handle_event(const SDL_Event& e);

    render::Vec3 eye() const { return eye_; }
    render::Mat4 view() const { return view_; }
    // GT3 shows its 640 x 448 frame at 4:3 (its 16:9 option scales x by 0.75), so a picture
    // `aspect` wide per unit of height takes a frustum aspect x kPixelAspect wide: the
    // projection and every overlay placed with it (frustum()).
    static constexpr float kPixelAspect = (640.0f / 448.0f) / (ReplayDirector::kYScale * 4.0f / 3.0f);
    render::Mat4 projection(float aspect) const;
    render::Frustum frustum(float aspect) const { return render::Frustum::symmetric(fovy_, aspect * kPixelAspect); }
    float fovy() const { return fovy_; }
    void set_orbit(float yaw, float pitch, float dist) { orbit_yaw_ = yaw, orbit_pitch_ = pitch, orbit_dist_ = dist; }
    // Replay (TV) cameras and the followed car's track distance (set before update()).
    void set_tv_cameras(std::vector<TvCamera> cams, float track_length) { tv_ = std::move(cams), tv_length_ = track_length; }
    void set_track_distance(float s) { track_s_ = s; }
    // The course's own replay camera script (GTRC); used by the TV mode when set.
    void set_replay_director(ReplayDirector* d) { director_ = d; }
    // Replays, for the next update() only: the point the TV camera aims at (the followed car, or
    // between it and its battle partner, moving from the last target), the pair, and the
    // partner's car (mode 2's on-board cut is mounted on it).
    void set_replay_aim(const render::Vec3& aim, const ReplayDirector::Pair* pair, const sim::VehicleState* partner,
                        float partner_length);
    void set_lap(int lap) { lap_ = lap; } // the followed car's lap counter (race::lap_counter)
    // View mode 1's procedural cameras: the road height (Roadside's placement, the orbit's and
    // the telephoto's ground clearance) and, for the next update() only, the followed car's
    // camera mounts.
    void set_ground(GroundHeight g) { ground_ = std::move(g); }
    void set_replay_mounts(const std::vector<CarMount>* m) { replay_mounts_ = m; }
    // The followed car as the procedural cameras see it (its body origin, extents, attitude).
    static CameraCar camera_car(const sim::VehicleState& s, const sim::VehicleSetup& setup);
    int replay_record() const { return director_record_; }
    // The camera is a course shot whose record has the heat haze flag (ReplayDirector::Shot::haze;
    // never the live views, the procedural cameras or a turn on board).
    bool replay_haze() const { return mode_ == CameraMode::Tv && director_haze_; }
    int tv_index() const { return tv_active_; }
    // The eye is in the car: own car hidden, mirror, in-car HUD and sounds. Not during the
    // pre-race shot, which looks at the car from outside (GT3 draws it with the chase HUD).
    bool onboard() const { return (mode_ == CameraMode::Bumper || mode_ == CameraMode::Roof) && !intro_; }
    // The replay camera is one of view mode 1's on-board car mounts (in-car, a bumper, the
    // driver's head): the followed car is not drawn.
    bool replay_on_board() const { return mode_ == CameraMode::Tv && replay_on_board_; }
    // The camera's kind for the race sounds (GT3's camera manager type over the camera's own
    // kind): the in-car views, the replay's car mounts (on board or outside: view mode 1's mounts,
    // mode 2's on-board cut) are car-mounted; the chase views chase; every other camera, and the
    // pre-race shot, other.
    CameraSound sound_kind() const;
    // The eye's velocity over the last update() (m/s): the listener's motion for every car's
    // Doppler (GT3: the camera's own displacement over the frame). Zero on the update of a cut
    // (shot() or the director's cut changed), where the eye jumps.
    render::Vec3 eye_velocity() const { return eye_velocity_; }
    // The depth of field's focus distance (presentation §G17; GT3's camera +0xD0) as of the last
    // update(): a course shot's distance from its eye to its aim (the car's point 0.4 m up, or
    // the record's target), the procedural cameras' (ProceduralCamera::focus), 100 m for the
    // player's views and car mounts; ours: the fallback TV cameras' distance to their aim.
    float focus() const { return focus_; }

    static constexpr float kFarPlane = 6000.0f; // ours, where no GT3 far plane is known
    float near_plane = 0.1f, far_plane = kFarPlane;
    // GT3's far plane (presentation §G: Env0 +0x20, 610..3010 m; 0 = none): its VU clips and culls
    // the race view there (render::DrawOptions::far_clip), and drops tyre smoke beyond it.
    // set_gt3_far also moves far_plane out to the backdrop's (CourseScene::kBackdropScale).
    float gt3_far = 0;
    void set_gt3_far(float far, float backdrop_scale) {
        gt3_far = far;
        far_plane = far > 0 ? far / backdrop_scale : kFarPlane;
    }
    // GT3's near plane at this view's zoom (presentation §G: 0.1 x 0.7002 x the zoom),
    // where its VU clips at the guard band (render::DrawOptions::guard_near).
    float gt3_near() const { return 0.1f * 0.7002f * 0.7f * ReplayDirector::kYScale / std::tan(fovy_ * 0.5f); }

private:
    void place(const sim::VehicleState& s, const sim::VehicleSetup& setup, const PadState& pad, double dt);

    CameraMode mode_ = CameraMode::Bumper; // GT3's default view: in-car (both halves in 2P)
    render::Vec3 eye_{0, 2, 5};
    render::Mat4 view_;
    float fovy_ = 0.8f;
    bool snapped_ = false;
    float chase_yaw_ = 0;    // smoothed heading (radians, same convention as the car: nose = (-sin, 0, -cos))
    float chase_height_ = 0; // smoothed car height
    // GT3 chase lags (spec: physics step at 60 Hz): yaw spring 30 e - 10 w (trapezoid), pitch
    // spring 20 e - 10 w (semi-implicit Euler).
    float lag_yaw_ = 0, lag_yaw_w_ = 0, lag_yaw_acc_ = 0, lag_pitch_ = 0, lag_pitch_w_ = 0;
    double lag_time_ = 0;
    render::Vec3 point_[2] = {{0, -0.8f, 0}, {0, -1.5f, -6.0f}};
    bool look_back_ = false;
    float look_x_ = 0, look_y_ = 0; // the look's stick (set_look)
    float look_ = 0;                // its eased turn (radians, + right)
    bool mirror_ = true;
    // In-car vibration (GT3's on-board camera shakers): random kicks scaled by the tyres' slip,
    // through a lag, stepped at 60 Hz; see update().
    struct Shaker {
        float v = 0, x = 0, u_prev = 0;
        void step(float u) { v += ((u - u_prev) - 12.0f * v) / 60.0f, x += v / 60.0f, u_prev = u; }
    };
    float shake_random(); // GT3's uniform [0, 1) draw (two RNG steps)
    bool shake_on_ = true;
    Shaker shake_yaw_, shake_pitch_;
    float shake_speed_ = 1.0f / 12.0f;
    u32 shake_rng_ = 0x87654321u;
    double shake_time_ = 0;
    bool gt3_views_ = false;
    bool intro_ = false;
    float intro_t_ = 0;
    std::optional<GtrcRecord> start_shot_;
    float orbit_yaw_ = 0.6f, orbit_pitch_ = 0.25f, orbit_dist_ = 7.0f;
    bool dragging_ = false;
    std::vector<TvCamera> tv_;
    float tv_length_ = 0, track_s_ = 0;
    int tv_active_ = -1;
    ReplayDirector* director_ = nullptr;
    int lap_ = 0, director_record_ = -1;
    bool director_haze_ = false;
    std::optional<render::Vec3> replay_aim_;
    std::optional<ReplayDirector::Pair> replay_pair_;
    std::optional<sim::VehicleState> replay_partner_;
    float replay_partner_length_ = 4.5f;
    // Mode 1's procedural camera of the current cut (the director's cut serial), stepped at 60 Hz.
    ProceduralCamera proc_;
    bool replay_on_board_ = false;
    bool replay_car_mount_ = false; // a car mount (procedural, inside or out) or mode 2's on-board cut
    // The eye's motion: the last update's eye and cut (eye_velocity).
    render::Vec3 eye_velocity_{}, last_eye_{};
    std::optional<std::pair<Shot, int>> last_cut_;
    float focus_ = 100.0f;
    int proc_serial_ = -1;
    double proc_acc_ = 0;
    GroundHeight ground_;
    const std::vector<CarMount>* replay_mounts_ = nullptr;
};

} // namespace gt3r::race
