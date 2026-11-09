#include "race/custom_cameras.h"

#include "base/config.h"
#include "race/hood_camera.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace gt3r::race {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDeg = kPi / 180.0f;
constexpr std::string_view kSection = "custom_camera";

// [custom_camera<n>]: the prefix and a number.
bool camera_section(std::string_view name) {
    if (name.size() <= kSection.size() || name.substr(0, kSection.size()) != kSection) return false;
    return std::all_of(name.begin() + std::ptrdiff_t(kSection.size()), name.end(), [](char c) { return c >= '0' && c <= '9'; });
}

float number(const IniDocument::Section& s, std::string_view key, float fallback) {
    for (const auto& [k, v] : s.entries)
        if (k == key) {
            char* end = nullptr;
            const float f = std::strtof(v.c_str(), &end);
            return end != v.c_str() && std::isfinite(f) ? f : fallback;
        }
    return fallback;
}

std::string text(const IniDocument::Section& s, std::string_view key) {
    for (const auto& [k, v] : s.entries)
        if (k == key) return v;
    return {};
}

std::string format(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.3f", double(v));
    std::string s = b; // trailing zeros off: 0.500 -> 0.5, 6.000 -> 6
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s == "-0" ? "0" : s;
}

// Degrees wrapped to -180 .. 180.
float wrap_degrees(float a) {
    a = std::remainder(a, 360.0f);
    return a == -180.0f ? 180.0f : a;
}

} // namespace

bool CustomCamera::operator==(const CustomCamera& o) const {
    return id == o.id && name == o.name && enabled == o.enabled && mount == o.mount && offset.x == o.offset.x &&
           offset.y == o.offset.y && offset.z == o.offset.z && yaw == o.yaw && pitch == o.pitch && roll == o.roll &&
           body_roll == o.body_roll && fov == o.fov && car == o.car && incar_hud == o.incar_hud &&
           mirror == o.mirror && mirror_x == o.mirror_x && mirror_y == o.mirror_y && mirror_size == o.mirror_size;
}

void CustomCamera::clamp() {
    // A bonnet mount's offset is a nudge within the mounts' bounds, an in-car mount's a seat
    // nudge from the driver's eye — not positions (hood_camera_eye).
    if (mount == Mount::Hood) {
        offset.x = std::isfinite(offset.x) ? std::clamp(offset.x, -kHoodLateral, kHoodLateral) : 0.0f;
        offset.y = std::isfinite(offset.y) ? std::clamp(offset.y, kHoodHeightMin, kHoodHeightMax) : 0.0f;
        offset.z = std::isfinite(offset.z) ? std::clamp(offset.z, -kHoodForward, kHoodForward) : 0.0f;
    } else if (mount == Mount::Incar) {
        offset.x = std::isfinite(offset.x) ? std::clamp(offset.x, -kSeatLateral, kSeatLateral) : 0.0f;
        offset.y = std::isfinite(offset.y) ? std::clamp(offset.y, kSeatHeightMin, kSeatHeightMax) : 0.0f;
        offset.z = std::isfinite(offset.z) ? std::clamp(offset.z, -kSeatForward, kSeatForward) : 0.0f;
    }
    const float d = render::length(offset);
    if (!(d <= kMaxDistance)) offset = std::isfinite(d) ? offset * (kMaxDistance / d) : render::Vec3{0, 0.7f, 6.0f};
    yaw = wrap_degrees(std::isfinite(yaw) ? yaw : 0.0f);
    pitch = std::clamp(std::isfinite(pitch) ? pitch : 0.0f, -kMaxPitch, kMaxPitch);
    roll = std::clamp(std::isfinite(roll) ? roll : 0.0f, -kMaxRoll, kMaxRoll);
    body_roll = std::clamp(std::isfinite(body_roll) ? body_roll : 1.0f, 0.0f, 1.0f);
    fov = std::clamp(std::isfinite(fov) ? fov : 47.9f, kMinFov, kMaxFov);
    mirror_x = std::clamp(std::isfinite(mirror_x) ? mirror_x : kMirrorX, 0.0f, 1.0f);
    mirror_y = std::clamp(std::isfinite(mirror_y) ? mirror_y : kMirrorY, 0.0f, 1.0f);
    mirror_size = std::clamp(std::isfinite(mirror_size) ? mirror_size : 1.0f, kMinMirror, kMaxMirror);
    if (name.size() > kMaxName) name.resize(kMaxName);
}

void CustomCamera::axes(render::Vec3& forward, render::Vec3& up) const {
    // Turned right = about +y by -yaw; up = about the turned x axis; clockwise = about the view axis.
    const render::Mat4 m = render::rotate_y(-yaw * kDeg) * render::rotate_x(pitch * kDeg) * render::rotate_z(-roll * kDeg);
    forward = m.transform_point({0, 0, -1});
    up = m.transform_point({0, 1, 0});
}

std::vector<CustomCamera> load_custom_cameras(const IniDocument& cfg) {
    std::vector<CustomCamera> cams;
    for (const IniDocument::Section& s : cfg.sections()) {
        if (!camera_section(s.name) || int(cams.size()) >= CustomCamera::kMaxCameras) continue;
        CustomCamera c;
        c.id = int(number(s, "id", 0));
        c.name = std::string(trim(text(s, "name")));
        if (c.id <= 0 || c.name.empty() || find_custom_camera(cams, c.id)) continue;
        c.enabled = to_lower(text(s, "enabled")) != "no";
        {
            const std::string m = to_lower(text(s, "mount"));
            c.mount = m == "body" ? CustomCamera::Mount::Body
                                : (m == "bonnet" || m == "hood") ? CustomCamera::Mount::Hood
                                : (m == "incar" || m == "cockpit") ? CustomCamera::Mount::Incar
                                                                   : CustomCamera::Mount::Follow;
        }
        c.offset = {number(s, "x", c.offset.x), number(s, "y", c.offset.y), number(s, "z", c.offset.z)};
        c.yaw = number(s, "yaw", c.yaw);
        c.pitch = number(s, "pitch", c.pitch);
        c.roll = number(s, "roll", c.roll);
        // (roll_lock: an early test build's inverse, the share levelled out)
        c.body_roll = number(s, "body_roll", 100.0f - number(s, "roll_lock", 100.0f - c.body_roll * 100.0f)) / 100.0f;
        c.fov = number(s, "fov", c.fov);
        {
            const std::string car = to_lower(text(s, "car"));
            c.car = car == "hidden" ? CustomCamera::OwnCar::Hidden : CustomCamera::OwnCar::Shown; // ("ahead": shown)
        }
        c.incar_hud = to_lower(text(s, "hud")) == "incar";
        c.mirror = to_lower(text(s, "mirror")) == "on";
        c.mirror_x = number(s, "mirror_x", c.mirror_x);
        c.mirror_y = number(s, "mirror_y", c.mirror_y);
        c.mirror_size = number(s, "mirror_size", c.mirror_size);
        c.clamp();
        cams.push_back(std::move(c));
    }
    return cams;
}

void save_custom_cameras(IniDocument& cfg, const std::vector<CustomCamera>& cams) {
    std::vector<IniDocument::Section>& all = cfg.sections();
    std::erase_if(all, [](const IniDocument::Section& s) { return camera_section(s.name); });
    for (size_t i = 0; i < cams.size(); ++i) {
        const CustomCamera& c = cams[i];
        const std::string sec = std::string(kSection) + std::to_string(i + 1);
        cfg.set(sec, "id", std::to_string(c.id));
        cfg.set(sec, "name", c.name);
        cfg.set(sec, "enabled", c.enabled ? "yes" : "no");
        cfg.set(sec, "mount", c.mount == CustomCamera::Mount::Body ? "body"
                            : c.mount == CustomCamera::Mount::Hood ? "hood"
                            : c.mount == CustomCamera::Mount::Incar ? "incar"
                                                                    : "follow");
        cfg.set(sec, "x", format(c.offset.x));
        cfg.set(sec, "y", format(c.offset.y));
        cfg.set(sec, "z", format(c.offset.z));
        cfg.set(sec, "yaw", format(c.yaw));
        cfg.set(sec, "pitch", format(c.pitch));
        cfg.set(sec, "roll", format(c.roll));
        cfg.set(sec, "body_roll", format(c.body_roll * 100.0f));
        cfg.set(sec, "fov", format(c.fov));
        cfg.set(sec, "car", c.car == CustomCamera::OwnCar::Hidden ? "hidden" : "shown");
        cfg.set(sec, "hud", c.incar_hud ? "incar" : "chase");
        cfg.set(sec, "mirror", c.mirror ? "on" : "off");
        cfg.set(sec, "mirror_x", format(c.mirror_x));
        cfg.set(sec, "mirror_y", format(c.mirror_y));
        cfg.set(sec, "mirror_size", format(c.mirror_size));
    }
}

int new_custom_camera_id(const std::vector<CustomCamera>& cams) {
    int id = 1;
    for (const CustomCamera& c : cams) id = std::max(id, c.id + 1);
    return id;
}

std::string new_custom_camera_name(const std::vector<CustomCamera>& cams) {
    for (int n = int(cams.size()) + 1;; ++n) {
        std::string name = "Camera " + std::to_string(n);
        if (std::none_of(cams.begin(), cams.end(), [&](const CustomCamera& c) { return c.name == name; })) return name;
    }
}

const CustomCamera* find_custom_camera(const std::vector<CustomCamera>& cams, int id) {
    for (const CustomCamera& c : cams)
        if (c.id == id) return &c;
    return nullptr;
}

std::string export_custom_cameras(const std::vector<CustomCamera>& cams) {
    IniDocument doc;
    doc.set("gt3r_cameras", "version", "1");
    save_custom_cameras(doc, cams);
    return "# GT3 Revisited cameras: Settings > Cameras > Import cameras\n" + doc.serialize();
}

std::vector<CustomCamera> import_custom_cameras(std::string_view text) {
    return load_custom_cameras(IniDocument::parse(text));
}

int add_custom_cameras(std::vector<CustomCamera>& cams, const std::vector<CustomCamera>& in) {
    int added = 0;
    for (CustomCamera c : in) {
        if (int(cams.size()) >= CustomCamera::kMaxCameras) break;
        c.id = new_custom_camera_id(cams);
        const std::string base = c.name;
        for (int n = 2; std::any_of(cams.begin(), cams.end(), [&](const CustomCamera& k) { return k.name == c.name; }); ++n) {
            const std::string tail = " " + std::to_string(n);
            c.name = base.substr(0, CustomCamera::kMaxName - tail.size()) + tail;
        }
        cams.push_back(std::move(c));
        ++added;
    }
    return added;
}

std::string camera_file_name(std::string_view name) {
    std::string s;
    for (char ch : name)
        if (u8(ch) >= 32 && std::string_view("/\\:*?\"<>|").find(ch) == std::string_view::npos) s += ch;
    s = std::string(trim(s));
    while (!s.empty() && s.back() == '.') s.pop_back(); // (Windows drops trailing dots)
    return (s.empty() ? std::string("Camera") : s) + ".ini";
}

void orbit_camera(CustomCamera& c, const render::Vec3& pivot, float az, float el) {
    const render::Vec3 v = c.offset - pivot;
    const float d = render::length(v);
    if (d < 0.05f) { // at the pivot: nothing to go round
        look_camera(c, -az, -el);
        return;
    }
    // Azimuth 0 behind the car (+z), growing to its right (+x); elevation up.
    const float a0 = std::atan2(v.x, v.z), e0 = std::asin(std::clamp(v.y / d, -1.0f, 1.0f));
    const float lim = CustomCamera::kMaxPitch * kDeg;
    const float e1 = std::clamp(e0 + el, -lim, lim), a1 = a0 + az;
    c.offset = pivot + render::Vec3{std::cos(e1) * std::sin(a1), std::sin(e1), std::cos(e1) * std::cos(a1)} * d;
    // A camera aimed at the pivot from (a, e) is turned -a and pitched -e: the view goes round with the eye.
    look_camera(c, -az, -(e1 - e0));
    c.clamp();
}

void look_camera(CustomCamera& c, float yaw, float pitch) {
    c.yaw = wrap_degrees(c.yaw + yaw / kDeg);
    c.pitch = std::clamp(c.pitch + pitch / kDeg, -CustomCamera::kMaxPitch, CustomCamera::kMaxPitch);
}

void pan_camera(CustomCamera& c, float right, float up, float forward) {
    render::Vec3 f, u;
    c.axes(f, u);
    const render::Vec3 r = render::normalize(render::cross(f, u));
    c.offset = c.offset + r * right + u * up + f * forward;
    c.clamp();
}

} // namespace gt3r::race
