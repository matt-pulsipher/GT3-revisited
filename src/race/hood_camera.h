#pragma once

// The bonnet camera's place on a car, worked out once from the body's LOD-0 mesh
// (CarVisual::hood_profile): HoodCameraProfile::kMountCount fore/aft mounts on a centre strip
// of the bodywork between the cabin and the nose, each at the surface's height plus a
// clearance, with the downward tilt that keeps the bonnet's edge inside the picture's lower
// half. It needs no per-car table: stock cars, regional and 2P variants, followed cars and
// fitted replacement bodies all work from their own mesh. A Mount::Hood custom camera
// (race/custom_cameras.h) rides it; its offset is a nudge from the mounts, not a position.

#include "render/math3d.h"
#include "scene/model.h"
#include "sim/sim_types.h"

#include <array>
#include <cstddef>

namespace gt3r::race {

struct CustomCamera;

struct HoodCameraProfile {
    static constexpr size_t kMountCount = 17;
    std::array<render::Vec3, kMountCount> eyes{};       // the mount in the model frame (x right, y up, z back)
    std::array<float, kMountCount> surface_y{};         // the bodywork's height under it
    std::array<float, kMountCount> pitch_down{};        // radians down: the bonnet's edge in the picture
    render::Vec3 bounds_min{}, bounds_max{};
    bool valid = false;
};

HoodCameraProfile make_hood_camera_profile(const scene::Model& body, float wheelbase);

// A bonnet camera's offset nudges its seat within these bounds (the camera editor's sliders too).
inline constexpr float kHoodForward = 0.40f;                            // ± metres fore/aft along the mounts
inline constexpr float kHoodLateral = 0.40f;                            // ± metres sideways
inline constexpr float kHoodHeightMin = -0.10f, kHoodHeightMax = 0.35f; // metres up/down

// The mount's eye in the car model's frame (the body origin, as CameraRig's custom-camera
// space): mount picked by cam.offset.z (metres fore/aft of the middle mount, ±kHoodForward),
// raised by cam.offset.y (kHoodHeightMin..kHoodHeightMax) and slid sideways by cam.offset.x
// (±kHoodLateral, the centre strip's height kept). An invalid profile falls back to an
// estimate from the setup's length, height and wheelbase.
render::Vec3 hood_camera_eye(const HoodCameraProfile& profile, const sim::VehicleSetup& setup,
                             const CustomCamera& cam);
// The downward tilt for cam's mount (0 when the profile is invalid).
float hood_camera_pitch(const HoodCameraProfile& profile, const CustomCamera& cam);

} // namespace gt3r::race
