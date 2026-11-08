#pragma once

#include "base/config.h"
#include "render/math3d.h"
#include "scene/model.h"
#include "sim/sim_types.h"

#include <array>
#include <cstddef>

namespace gt3r::race {

struct HoodCameraSettings {
    float fov_degrees = 55;
    float height_offset = 0;
    float forward_offset = 0;
    float horizon_lock = 0.5f;
    bool mirror = true;
    static HoodCameraSettings load(const IniDocument& ini);
    void save(IniDocument& ini) const;
    void normalize();
};

struct HoodCameraProfile {
    static constexpr size_t kMountCount = 17;
    std::array<render::Vec3, kMountCount> eyes{};
    std::array<float, kMountCount> surface_y{};
    std::array<float, kMountCount> pitch_down{};
    render::Vec3 bounds_min{}, bounds_max{};
    bool valid = false;
};

HoodCameraProfile make_hood_camera_profile(const scene::Model& body, float wheelbase);
render::Vec3 hood_camera_eye(const HoodCameraProfile& profile, const sim::VehicleSetup& setup,
                             const HoodCameraSettings& settings);
float hood_camera_pitch(const HoodCameraProfile& profile, const HoodCameraSettings& settings);

}
