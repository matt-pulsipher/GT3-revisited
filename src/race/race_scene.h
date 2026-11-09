#pragma once

// GPU-side scene of a time trial: the course (scenery, the licence course line,
// distant backdrop, camera-centred sky domes) and the player's car (body LODs plus
// four separately transformed wheels).

#include "formats/course/visibility.h"
#include "formats/model/brake_model.h"
#include "formats/model/car_model.h"
#include "race/clut_animations.h"
#include "race/hood_camera.h"
#include "race/occluder.h"
#include "race/procedural_cameras.h"
#include "race/scenery_fade.h"
#include "scene/model.h"
#include "render/gpu_model.h"
#include "race/lamp_glows.h"
#include "render/view3d.h"
#include "race/wheel_fx.h"
#include "sim/sim_types.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace gt3r::race {

struct CourseStats {
    size_t meshes = 0, triangles = 0, textures = 0;
};

class CourseScene {
public:
    // GT3 draws the backdrop (slot 22) with Scale(0.01) x view: its depth and fog distance are a
    // hundredth of the true ones, so its far plane lies 100 times as far as the scenery's.
    static constexpr float kBackdropScale = 0.01f;

    // Builds the models from a decompressed course file.
    // `sheen`: draw the road sheen passes (gtm1::Options::road_sheen).
    CourseScene(render::GpuContext& ctx, ByteSpan course_file, const std::string& name, bool sheen = true);

    // Appends draw items for a camera at `eye`: sky, backdrop, scenery and, on courses that
    // have one, the licence course line right after the scenery. The game draws that line in
    // every view of the course: live runs, demonstrations and replays in any replay mode
    // (course.md "Course line"). The scenery: the nodes of `view`'s visibility list for `eye`
    // (visible_nodes), or with `fade` its nodes drawn whole and, translucent, those fading in
    // or out (SceneryFade, updated with that list). Mirror and SplitMirror: the rear-view
    // mirror's pass (GT3 draws the low-resolution sky, slot 24, then slot 1: no backdrop).
    void add_items(std::vector<render::DrawItem>& items, const render::Vec3& eye,
                   course::SceneryView view = course::SceneryView::Main, const SceneryFade* fade = nullptr) const;
    // The scenery nodes `view` draws from `eye`, in GT3's order (its slot 1 nodes and their
    // billboards): the visibility list of the view point nearest `eye`. Null: every node (no
    // list). Valid while the scene lives.
    const std::vector<u16>* visible_nodes(const render::Vec3& eye, course::SceneryView view) const {
        return vis_[size_t(view)].visible(eye.x, eye.y, eye.z);
    }
    const CourseStats& stats() const { return stats_; }
    // Fog/clear colour picked from the sky (average horizon colour).
    const float* horizon_color() const { return horizon_; }
    // Reflection environment stand-ins: sky textures' upper halves, scenery below the horizon.
    const float* sky_color() const { return sky_rgb_; }
    const float* ground_color() const { return ground_; }
    // The car reflection map's scene (GT3): the slot 23 sky dome centred on `eye`, rotated by
    // `sky_rotation` (RotY(Env0 +0x28) RotX(Env0 +0x48)), then the slot 5 scenery, which is
    // much darker than the drawn scenery and made for reflections: the nodes of slot 6's list
    // for `eye` (the map's camera). False when the course has none.
    bool add_env_items(std::vector<render::DrawItem>& items, const render::Vec3& eye,
                       const render::Mat4& sky_rotation) const;
    // Opaque scenery and backdrop triangles for ray tests (sun visibility).
    const Occluder& occluder() const { return occluder_; }
    // The lights of the course's own lit packets (Env0 scene lights: SunParams::course_lights),
    // given to every course item; none = the draw options' lights.
    void set_lights(const render::ItemLights& lights) { lights_ = lights, has_lights_ = true; }

    // The course's animations: the CLUT animations of the scenery, backdrop and slot 5 texture
    // sets (flashing lights, the start-signal lights) and the nodes the +0x3C animators move
    // (flags, spinning parts, flip-book crowds and screens; gtm1::AnimatedNodes), as the game
    // plays them. A run's start: back to their start, then the start signal for a countdown of
    // `countdown` seconds (0 without one).
    void restart_animations(float countdown);
    // Advances them (seconds of race steps: 1/60 each; not while paused).
    void animate(float seconds);
    // Uploads what changed since the last call (once per drawn frame).
    void upload_animations();

private:
    // Moving nodes, rebuilt every frame into a model sharing `textures`' textures.
    struct Moving {
        std::vector<gtm1::AnimatedNodes> nodes;
        std::unique_ptr<render::GpuModel> gpu;
    };
    void set_moving(Moving& m, render::GpuContext& ctx, const render::GpuModel* textures,
                    std::vector<gtm1::AnimatedNodes>&& nodes);

    ClutAnimations cluts_;
    Moving scenery_moving_, backdrop_moving_;
    bool moving_changed_ = true;
    bool frame_alpha_ = false; // the scenery draws with the PS2's frame alpha (sheen masks)
    Occluder occluder_;
    std::unique_ptr<render::GpuModel> scenery_, line_, backdrop_, sky_, sky_low_;
    CourseStats stats_;
    float horizon_[3] = {0.62f, 0.70f, 0.80f};
    float sky_rgb_[3] = {0.35f, 0.55f, 0.85f};
    std::unique_ptr<render::GpuModel> env_scenery_, env_sky_; // slots 5 and 23
    std::array<course::Visibility, 4> vis_; // slot 1's lists per course::SceneryView
    course::Visibility env_vis_;            // slot 5's (slot 6)
    render::ItemLights lights_;
    bool has_lights_ = false;
    float ground_[3] = {0.18f, 0.17f, 0.16f};
};

// Car shadows (GT3 car model nodes 1 and 6; colour and direction from the course's Env0):
// node 1 is sheared along the light (x' = x + d.x y, y' = 0, z' = z + d.z y in the car's ground
// frame), node 6 is the contact shadow under the car; both are lifted 2 cm, blended over the
// road (no depth writes), RGB = `rgb`, alpha = vertex alpha x alpha_scale (x brightness for node 1).
struct CarShadowParams {
    bool enabled = false;
    float rgb[3] = {0, 0, 0};
    float alpha_scale = 0.5f; // Env0 +0xA6C
    float brightness = 1.0f;  // node 1 factor F (Env0 +0xAB0 / +0xAB8 by road shade)
    render::Vec3 shear{0, 0, 0}; // world-space shift per metre of height (Env0 +0xA70..+0xA78)
    // Node 5, the headlamp pool on the road ahead (night / dusk courses): Env0 +0x58 scales
    // its vertex alpha; drawn additively (Cs As + Cd) in the ground frame. 0 = not drawn.
    float headlamp = 0;
};

// The view drawing the cars, for their wheels (race/wheel_fx.h): GT3 picks each wheel's detail
// from its size on screen, draws the brake parts on the near ones whose outer face is in view,
// and in the main view blurs the spinning wheels. Without one (the mirrors) the wheels are drawn
// plain.
struct WheelView {
    render::Mat4 view, proj;
    render::Vec3 eye;
    bool blur = false; // the main view: the spin blurs (not the 2P halves)
    WheelEnv env;      // the course's blur time and falloff, the rotors' glow gain
};

// Where a car's wheel centres were on screen the last frame the main view drew it (normalised
// device coordinates), for the blur.
struct WheelTrack {
    std::array<std::array<float, 2>, 4> ndc{};
};

// Per-car look for one frame: light colour scales (ambient, diffuse, specular; GT3's road
// shade under the car), the reflection factor (1 - dirt) and the light shadow's factor F.
struct CarLook {
    float light_scale[3] = {1, 1, 1};
    float reflection = 1;
    float shadow = 1;
    float lamp = 0; // brake-lamp factor k/4 (lamp materials' emission)
    bool brake_now = false; // the brake pedal is pressed this frame (the lit-lens geometry)
    // Which drawing of the car this is, 0 the car, 1 its ghost: each has its own morph geometry.
    int instance = 0;
    // Point-light courses: the car's own lights, and one node 1 shadow per lamp (world shear,
    // alpha) instead of the sun shadow.
    const render::ItemLights* lights = nullptr;
    int lamp_shadows = 0;
    render::Vec3 lamp_shear[4];
    float lamp_alpha[4] = {};
    // The wheels' view (null: plain wheels), this car's track of them, and whether it is the
    // camera's car (GT3 blurs its tyres and rims; the other cars' rims show their spinning image).
    const WheelView* wheels = nullptr;
    WheelTrack* wheel_track = nullptr;
    bool focus = false;
};

class CarVisual {
public:
    // `car_file` is cars/day/<model>; wheels are placed from `wheels`.
    // `wheel_file` (optional): a bought wheel (wheel/<label>, GTTW) replacing the car's own.
    // `showroom_car`, `showroom_wheel` (optional): LOD 0's wheels as the menus draw them
    // ([video] showroom_wheels; CarModelOptions::showroom_car, showroom_wheel). `stand_in_rim`
    // (optional): the menus' rim for a race rim with nothing to draw (CarModelOptions::stand_in_rim).
    CarVisual(render::GpuContext& ctx, ByteSpan car_file, const std::string& model, const WheelSpec& wheels,
              int paint = 0, ByteSpan wheel_file = {}, const CarBodyMod& body_mod = {}, ByteSpan showroom_car = {},
              ByteSpan showroom_wheel = {}, std::function<std::optional<Bytes>()> stand_in_rim = {});

    // Car draw items for the (interpolated) state. LOD is picked from the
    // camera distance (car_lod).
    void add_items(std::vector<render::DrawItem>& items, const sim::VehicleState& s, const sim::VehicleSetup& setup,
                   const render::Vec3& eye, const CarLook& look = {}) const;
    // The wet-road reflection's mirrored car (docs/dev/RACE.md "Wet-road reflection"): body and wheels only, reflected about
    // the road plane under the car (the four wheel contacts). False when the plane is unusable.
    bool add_mirror_items(std::vector<render::DrawItem>& items, const sim::VehicleState& s,
                          const sim::VehicleSetup& setup, const render::Vec3& eye, const CarLook& look = {}) const;
    // Reflection about the road plane under the car (I - 2 n n^T, through the contacts' centre).
    static bool road_mirror(const sim::VehicleState& s, render::Mat4& out);
    int lod_count() const { return lods_; }
    // [video] full_car_detail = on: LOD 0 (the full-detail body, wheels and textures) at any distance.
    void set_full_detail(bool on) { full_detail_ = on; }
    // The GTCI lamp lists (headlamps, tail and brake lamps) in the model frame, and the
    // model frame -> world for a state (the body as drawn).
    const CarLampSet& lamps() const { return lamps_; }
    // The car's camera mounts (GTCI): view mode 1's car-mounted shots.
    const std::vector<CarMount>& mounts() const { return mounts_; }
    render::Mat4 body_matrix(const sim::VehicleState& s, const sim::VehicleSetup& setup) const;
    // The body's opaque LOD-0 triangles in the model frame: what the lamp glows' ray tests see of
    // the car (GT3's depth test sees the drawn car).
    const Occluder& occluder() const { return occluder_; }
    // The bonnet camera's mounts on the body (race/hood_camera.h), from its LOD-0 mesh.
    const HoodCameraProfile& hood_profile() const { return hood_profile_; }
    void set_shadow(const CarShadowParams& p);
    bool shadow_set() const { return shadow_set_; }
    // The brake parts inside the wheels (race/brake.bin; the car file's GTCI records which).
    void set_brake_parts(const BrakeParts& bin);

private:
    // `wheel_fx`: the wheels' brake parts and spin blurs (not for the wet road's mirrored car,
    // whose wheels GT3 draws as its farthest level).
    void add_body_items(std::vector<render::DrawItem>& items, const sim::VehicleState& s, const sim::VehicleSetup& setup,
                        const CarLook& look, const render::Mat4& pre, int lod, bool wheel_fx) const;
    // What one wheel shows this frame (race/wheel_fx.h).
    struct WheelLook {
        bool parts = false;     // the rotor and caliper
        float glow = 0;         // the rotor glow's weight (0: none)
        bool tread = false;     // the tread blur, by tread_shifts
        std::array<float, 3> tread_shifts{};
        int rim = 0;            // 1: the rim blur by rim_angles, 2: the wheel's spinning image
        std::array<float, 4> rim_angles{};
        float turn = 0;         // the blur's turn (radians; 0: none), for showroom rims' copies
    };
    WheelLook wheel_look(int i, const render::Mat4& at, const render::Mat4& hub, const sim::WheelState& w,
                         const CarLook& look) const;
    std::unique_ptr<render::GpuModel> body_, body_brake_;
    // The draws the car's callback morphs (mud flaps, aerials, steering), per LOD, drawn from
    // models sharing morph_base_'s textures, one per CarLook::instance, rebuilt when the values
    // change.
    std::vector<gtm1::MorphDraws> morphs_;
    std::unique_ptr<render::GpuModel> morph_base_;
    struct Morph {
        std::unique_ptr<render::GpuModel> gpu;
        std::array<float, 5> values{};
    };
    mutable std::array<Morph, 2> morph_;
    CarLampSet lamps_;
    Occluder occluder_;
    HoodCameraProfile hood_profile_;
    std::vector<CarMount> mounts_;
    // Each wheel: the tyre (with its black faces) and the rim, drawn spinning; the band, the tyres'
    // arch shade, turned by the camber only; the brake parts (rotor and caliper, steered, not
    // spinning) and the rotor's glow (spinning).
    std::array<std::unique_ptr<render::GpuModel>, 4> tyres_, rims_;
    std::array<std::unique_ptr<render::GpuModel>, 4> bands_;
    std::array<std::unique_ptr<render::GpuModel>, 4> brakes_, glows_;
    std::array<render::Vec3, 4> wheel_center_{}; // where the decoder placed each wheel (car frame)
    std::array<BrakeRecord, 2> brake_records_{}; // GTCI +0x14 / +0x28
    // The wheels' sizes (WheelSpec, per axle): the tyre width and the bounding radius sqrt(R^2 + W^2 / 4).
    std::array<float, 2> wheel_width_{}, wheel_bound_{};
    u32 tyre_flags_ = 0, tyre_repeats_ = 1; // GTTR +0x14 / +0x10
    float tyre_shininess_ = 8;              // GTTR +0x18
    // The rim image's width (the blur's mip level) and the wheel's spinning image: the texture
    // index in rims_ (-1: the wheel file has none).
    u32 rim_width_ = 0;
    int rim_spin_texture_ = -1;
    // Showroom wheels: LOD 0's rims are the menus' modelled ones (CarModelOptions::showroom_car),
    // blurred by turned copies of their geometry (add_body_items).
    bool showroom_rims_ = false;
    // Shadow nodes 1 (light) and 6 (contact), and node 5 (headlamp pool); all LODs; CPU copies
    // to recolour.
    std::array<std::unique_ptr<render::GpuModel>, 3> shadow_;
    std::array<scene::Model, 3> shadow_src_;
    CarShadowParams shadow_params_;
    render::GpuContext* ctx_ = nullptr;
    bool shadow_set_ = false;
    bool full_detail_ = false;
    int lods_ = 1;
};

// The car's camera points 0 (in-car) and 1 (chase) from its GTCI block (+0x68 count, +0x6C
// offset of 0x40-byte records, translate p at +0x00); false when it has fewer than two.
bool car_camera_points(ByteSpan car_file, render::Vec3& p0, render::Vec3& p1);

// Car frame -> world matrix for a vehicle state.
render::Mat4 car_matrix(const sim::VehicleState& s);

// The car LOD at `dist` metres from the camera for a model with `lods` levels: 0 under 30 m, 1 under
// 80 m, else 2, at most lods - 1; 0 at any distance with `full_detail` ([video] full_car_detail = on).
int car_lod(float dist, int lods, bool full_detail = false);

// The car callback's morph values for a state (gtm1::RunParams::callback): ids 1-4 the flaps,
// 0.5 + 0.5 x (physics §8.8); id 5 the steering, 0.5 + 0.5 steer / lock (0.5 straight ahead).
std::array<float, 5> car_callback_values(const sim::VehicleState& s, const sim::VehicleSetup& setup);

} // namespace gt3r::race
