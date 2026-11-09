#include "race/hood_camera.h"

#include "race/custom_cameras.h"
#include "race/replay_cameras.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>
#include <vector>

namespace gt3r::race {

namespace {

constexpr float kForwardMin = -kHoodForward, kForwardMax = kHoodForward, kForwardStep = 0.05f;
constexpr float kHeightMin = kHoodHeightMin, kHeightMax = kHoodHeightMax;
constexpr float kLateralMax = kHoodLateral;

constexpr float kMountBack = 0.30f;
constexpr float kBodyFront = 0.25f;
constexpr float kBodyRear = 0.55f;
constexpr float kStripHalf = 0.20f;
constexpr float kClearance = 0.40f;

constexpr float kDeg = 3.14159265f / 180.0f;
constexpr float kTiltMargin = 20.0f * kDeg;
constexpr float kTiltMax = 22.0f * kDeg;
constexpr float kTiltDepth = 0.1f;

int mount_index(float forward_offset) {
    return std::clamp(int(std::lround((std::clamp(forward_offset, kForwardMin, kForwardMax) - kForwardMin) / kForwardStep)),
                      0, int(HoodCameraProfile::kMountCount) - 1);
}

struct Tri {
    render::Vec3 a, b, c;
};

std::vector<render::Vec3> clipped(const std::vector<render::Vec3>& poly, const render::Vec3& n, float d) {
    std::vector<render::Vec3> out;
    if (poly.empty()) return out;
    render::Vec3 a = poly.back();
    float da = render::dot(a, n) + d;
    for (const render::Vec3& b : poly) {
        const float db = render::dot(b, n) + d;
        if ((da >= 0) != (db >= 0)) out.push_back(a + (b - a) * (da / (da - db)));
        if (db >= 0) out.push_back(b);
        a = b, da = db;
    }
    return out;
}

bool skip_mesh(const scene::Mesh& m) {
    return m.lod != 0 || m.name.rfind("wheel_", 0) == 0 || m.name.rfind("node", 0) == 0;
}

bool skip_primitive(const scene::Primitive& p) {
    return (p.flags & (scene::kEnvironmentMap | scene::kAdditive | scene::kDepthOnly)) != 0;
}

bool finite(const render::Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

}

HoodCameraProfile make_hood_camera_profile(const scene::Model& body, float wheelbase) {
    HoodCameraProfile profile;
    std::vector<Tri> tris;
    for (const scene::Mesh& m : body.meshes) {
        if (skip_mesh(m)) continue;
        const render::Mat4 at = render::Mat4::from(m.transform);
        for (const scene::Primitive& p : m.primitives) {
            if (skip_primitive(p)) continue;
            const size_t end = std::min(m.indices.size(), size_t(p.first_index) + p.index_count);
            for (size_t i = p.first_index; i + 2 < end; i += 3) {
                render::Vec3 v[3];
                bool ok = true;
                for (int k = 0; k < 3; ++k) {
                    const u32 index = m.indices[i + k];
                    if (index >= m.vertices.size()) {
                        ok = false;
                        break;
                    }
                    const scene::Vertex& sv = m.vertices[index];
                    v[k] = at.transform_point({sv.pos[0], sv.pos[1], sv.pos[2]});
                    if (!finite(v[k])) ok = false;
                }
                if (!ok) continue;
                const render::Vec3 e1 = v[1] - v[0], e2 = v[2] - v[0];
                const render::Vec3 n = render::cross(e1, e2);
                const float area2 = render::dot(n, n);
                if (!std::isfinite(area2) || area2 <= 1e-12f) continue;
                tris.push_back({v[0], v[1], v[2]});
            }
        }
    }
    if (tris.empty()) return profile;
    render::Vec3 lo{FLT_MAX, FLT_MAX, FLT_MAX}, hi{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (const Tri& t : tris)
        for (const render::Vec3& v : {t.a, t.b, t.c}) {
            lo.x = std::min(lo.x, v.x), lo.y = std::min(lo.y, v.y), lo.z = std::min(lo.z, v.z);
            hi.x = std::max(hi.x, v.x), hi.y = std::max(hi.y, v.y), hi.z = std::max(hi.z, v.z);
        }
    const float front_z = lo.z, length = hi.z - lo.z, width = hi.x - lo.x;
    if (!std::isfinite(length) || !std::isfinite(width) || !(length > 0) || !(width > 0)) return profile;
    const float wb = std::isfinite(wheelbase) && wheelbase > 0 ? wheelbase
                                                             : std::clamp(0.6f * length, 0.5f, 4.0f);
    const float z_lo = front_z + kBodyFront * length, z_hi = front_z + kBodyRear * length;
    const float base = std::clamp(-kMountBack * wb, z_lo, z_hi);
    const float strip = kStripHalf * width;
    const float tx = std::tan(55.0f * 0.5f * kDeg) * (16.0f / 9.0f) *
                     ((640.0f / 448.0f) / (ReplayDirector::kYScale * 4.0f / 3.0f));
    for (size_t i = 0; i < HoodCameraProfile::kMountCount; ++i) {
        const float offset = kForwardMin + float(i) * kForwardStep;
        const float z = std::clamp(base - offset, z_lo, z_hi);
        float top = -std::numeric_limits<float>::infinity();
        for (const Tri& t : tris) {
            std::vector<render::Vec3> poly{t.a, t.b, t.c};
            poly = clipped(poly, {1, 0, 0}, strip);
            poly = clipped(poly, {-1, 0, 0}, strip);
            poly = clipped(poly, {0, 0, 1}, -front_z);
            poly = clipped(poly, {0, 0, -1}, z);
            for (const render::Vec3& v : poly) top = std::max(top, v.y);
        }
        profile.surface_y[i] = top;
        profile.eyes[i] = {0, top + kClearance, z};
    }
    for (size_t i = 0; i < HoodCameraProfile::kMountCount; ++i) {
        if (std::isfinite(profile.surface_y[i])) continue;
        float best = hi.y;
        size_t bd = HoodCameraProfile::kMountCount;
        for (size_t j = 0; j < HoodCameraProfile::kMountCount; ++j)
            if (std::isfinite(profile.surface_y[j]))
                if (const size_t d = i > j ? i - j : j - i; d < bd) bd = d, best = profile.surface_y[j];
        profile.surface_y[i] = best;
        profile.eyes[i].y = best + kClearance;
    }
    for (size_t i = 0; i < HoodCameraProfile::kMountCount; ++i) {
        const render::Vec3 eye = profile.eyes[i];
        float alpha = std::numeric_limits<float>::infinity();
        for (const Tri& t : tris) {
            std::vector<render::Vec3> poly{t.a, t.b, t.c};
            poly = clipped(poly, {0, 0, -1}, eye.z - kTiltDepth);
            poly = clipped(poly, {-1, 0, -tx}, tx * eye.z);
            poly = clipped(poly, {1, 0, -tx}, tx * eye.z);
            for (const render::Vec3& v : poly)
                if (const float a = std::atan2(eye.y - v.y, eye.z - v.z); std::isfinite(a) && a >= 0)
                    alpha = std::min(alpha, a);
        }
        profile.pitch_down[i] = std::isfinite(alpha) ? std::clamp(alpha - kTiltMargin, 0.0f, kTiltMax) : 0.0f;
    }
    profile.bounds_min = lo, profile.bounds_max = hi;
    profile.valid = true;
    return profile;
}

render::Vec3 hood_camera_eye(const HoodCameraProfile& profile, const sim::VehicleSetup& setup,
                             const CustomCamera& cam) {
    const float forward = std::isfinite(cam.offset.z) ? cam.offset.z : 0.0f;
    const float height = std::isfinite(cam.offset.y) ? std::clamp(cam.offset.y, kHeightMin, kHeightMax) : 0.0f;
    const float side = std::isfinite(cam.offset.x) ? std::clamp(cam.offset.x, -kLateralMax, kLateralMax) : 0.0f;
    if (profile.valid) {
        render::Vec3 eye = profile.eyes[size_t(mount_index(forward))];
        eye.x += side, eye.y += height;
        return eye;
    }
    const auto sane = [](float v, float fallback, float lo, float hi) {
        return std::isfinite(v) && v > 0 ? std::clamp(v, lo, hi) : fallback;
    };
    const float length = sane(setup.length, 4.5f, 1.0f, 12.0f);
    const float height0 = sane(setup.height, 1.35f, 0.3f, 4.0f);
    const float wb = sane(setup.wheelbase, 0.6f * length, 0.5f, length);
    const float front = -0.55f * length;
    const float z_lo = front + kBodyFront * length, z_hi = front + kBodyRear * length;
    const float base = std::clamp(-kMountBack * wb, z_lo, z_hi);
    const float top = std::clamp(0.55f * height0, 0.30f, 1.20f);
    return {side, top + kClearance + height, std::clamp(base - forward, z_lo, z_hi)};
}

float hood_camera_pitch(const HoodCameraProfile& profile, const CustomCamera& cam) {
    if (!profile.valid) return 0.0f;
    const float forward = std::isfinite(cam.offset.z) ? cam.offset.z : 0.0f;
    return profile.pitch_down[size_t(mount_index(forward))];
}

} // namespace gt3r::race
