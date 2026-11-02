#include "race/sun_glare.h"

#include "base/log.h"
#include "formats/gs_memory.h"
#include "formats/tex1.h"
#include "render/view3d.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>

namespace gt3r::race {

namespace {
float rdf(ByteSpan d, size_t o) {
    u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
} // namespace

std::array<float, 3> SunParams::light_scale(float a, float b) const {
    auto sc = [&](const std::array<float, 2>& p) { return p[1] + (1 - p[1]) * b * (1 - (1 - p[0]) * (1 - a)); };
    return {sc(shade_ambient), sc(shade_diffuse), sc(shade_specular)};
}

void SunParams::day_car_lights(render::DrawOptions& o) const {
    o.gt3_lighting = true;
    for (int i = 0; i < 3; ++i) o.light_ambient[i] = global_ambient[size_t(i)];
    bool main = false;
    o.fill_count = 0;
    for (size_t n = 0; n < 4; ++n) {
        if (!((car_light_mask >> n) & 1)) continue;
        const LampType& t = lamps[n];
        for (int i = 0; i < 3; ++i) o.light_ambient[i] += t.ambient[size_t(i)];
        render::Vec3 d{car_light_dir[n][0], car_light_dir[n][1], car_light_dir[n][2]};
        if (render::length(d) < 1e-6f) continue; // no direction: ambient only
        d = render::normalize(d);
        if (!main) {
            main = true;
            o.light_dir = d;
            for (int i = 0; i < 3; ++i) o.light_diffuse[i] = t.diffuse[size_t(i)], o.light_specular[i] = t.specular[size_t(i)];
        } else if (o.fill_count < 3) {
            int k = o.fill_count++;
            o.fill_dir[k][0] = d.x, o.fill_dir[k][1] = d.y, o.fill_dir[k][2] = d.z;
            for (int i = 0; i < 3; ++i) o.fill_diffuse[k][i] = t.diffuse[size_t(i)], o.fill_specular[k][i] = t.specular[size_t(i)];
        }
    }
    if (!main)
        for (int i = 0; i < 3; ++i) o.light_diffuse[i] = o.light_specular[i] = 0;
}

void SunParams::course_lights(render::ItemLights& out) const {
    // GT3 lights the course with the enabled scene lights and a global ambient of 0; the light
    // slots go, in light order, to the enabled lights with a non-zero diffuse or specular.
    out = {};
    for (size_t n = 0; n < 4; ++n) {
        if (!((scene_light_mask >> n) & 1)) continue;
        const SceneLight& l = scene_lights[n];
        for (int i = 0; i < 3; ++i) out.ambient[i] += l.ambient[size_t(i)];
        bool lit = false;
        for (int i = 0; i < 3; ++i) lit = lit || l.diffuse[size_t(i)] != 0 || l.specular[size_t(i)] != 0;
        render::Vec3 d{l.dir[0], l.dir[1], l.dir[2]};
        if (!lit || render::length(d) < 1e-6f) continue;
        d = render::normalize(d);
        int k = out.count++;
        out.dir[k][0] = d.x, out.dir[k][1] = d.y, out.dir[k][2] = d.z;
        for (int i = 0; i < 3; ++i) out.diffuse[k][i] = l.diffuse[size_t(i)], out.specular[k][i] = l.specular[size_t(i)];
    }
}

render::Vec3 SunParams::direction() const {
    float e = -elevation * 3.14159265f / 180.0f, a = -azimuth * 3.14159265f / 180.0f;
    return {std::sin(a) * std::cos(e), -std::sin(e), std::cos(a) * std::cos(e)};
}

SunParams SunParams::from_env0(ByteSpan b) {
    SunParams p;
    if (b.size() < 0x9EC || b[0] != 'E' || b[1] != 'n' || b[2] != 'v' || b[3] != '0') return p;
    auto f = [&](size_t o) { return rdf(b, o); };
    p.enabled = (rd32(b, 0x10) & 0x400) != 0;
    p.rgb = {f(0x9C0), f(0x9C4), f(0x9C8)};
    p.weight = f(0x9CC);
    p.glare_size = f(0x9D0);
    p.disc_size = f(0x9D4);
    p.disc_gain = f(0x9D8);
    p.elevation = f(0x9DC);
    p.azimuth = f(0x9E0);
    p.style = s32(rd32(b, 0x9E4));
    p.test_box = f(0x9E8);
    p.has_env = true;
    p.fog_rgb = {f(0x60), f(0x64), f(0x68)};
    p.sky_yaw = f(0x28), p.sky_pitch = f(0x48);
    p.fog_a = f(0x70);
    p.fog_b = f(0x74);
    if (b.size() >= 0xAA8) {
        p.mirror_fog_rgb = {f(0xA90), f(0xA94), f(0xA98)};
        p.mirror_fog_a = f(0xAA0);
        p.mirror_fog_b = f(0xAA4);
    }
    p.dof = (rd32(b, 0x10) & 0x100) != 0;
    p.dof_f_number = f(0x930), p.dof_focal_mm = f(0x934), p.dof_min_focus = f(0x938);
    p.far_clip = f(0x20);
    p.grade = (rd32(b, 0x10) & 0x80) != 0;
    p.grade_g = {f(0x980), f(0x984), f(0x988)};
    p.grade_black = f(0x98C), p.grade_white = f(0x990);
    p.saturation = f(0x994);
    // Car light 0 (Env0 +0xD0: direction towards the light, w 0; +0xE0 ambient, +0xF0 diffuse,
    // +0x100 specular; enabled by flag bit 0) and the global ambient (+0xEF0).
    p.car_light = (rd32(b, 0x10) & 1) != 0;
    p.light_dir = {f(0xD0), f(0xD4), f(0xD8)};
    p.light_ambient = {f(0xE0), f(0xE4), f(0xE8)};
    p.light_diffuse = {f(0xF0), f(0xF4), f(0xF8)};
    p.light_specular = {f(0x100), f(0x104), f(0x108)};
    if (b.size() >= 0xEFC) p.global_ambient = {f(0xEF0), f(0xEF4), f(0xEF8)};
    if (b.size() >= 0xABC) {
        p.has_shadow = true;
        p.shadow_rgb = {f(0xA60), f(0xA64), f(0xA68)};
        p.shadow_alpha = f(0xA6C);
        p.shadow_elev = f(0xA70);
        p.shadow_az = f(0xA74);
        p.shadow_len = f(0xA78);
        p.shadow_f_lit = f(0xAB0);
        p.shadow_f_shade = f(0xAB8);
        p.headlamp_pool = f(0x58);
    }
    for (size_t n = 0; n < 4; ++n) {
        size_t o = 0xD0 + 0x80 * n;
        if (o + 0x80 > b.size()) break;
        SunParams::LampType& t = p.lamps[n];
        t.ambient = {f(o + 0x10), f(o + 0x14), f(o + 0x18)};
        t.diffuse = {f(o + 0x20), f(o + 0x24), f(o + 0x28)};
        t.specular = {f(o + 0x30), f(o + 0x34), f(o + 0x38)};
        t.disc_size = f(o + 0x40);
        t.spot_exp = f(o + 0x4C);
        t.cutoff = f(o + 0x50);
        t.c0 = f(o + 0x54), t.c1 = f(o + 0x58), t.c2 = f(o + 0x5C);
        t.shadow_rgb = {f(o + 0x60), f(o + 0x64), f(o + 0x68)};
        t.shadow_alpha = f(o + 0x6C);
        t.disc_rgba = {f(o + 0x70), f(o + 0x74), f(o + 0x78), f(o + 0x7C)};
        p.car_light_dir[n] = {f(o), f(o + 4), f(o + 8)};
    }
    p.car_light_mask = rd32(b, 0x10) & 0xF;
    if (b.size() >= 0xADC) p.scene_light0_dir = {f(0xAD0), f(0xAD4), f(0xAD8)};
    p.scene_light_mask = (rd32(b, 0x10) >> 11) & 0xF;
    if (b.size() >= 0xEC8) p.panorama = {f(0xEC0), f(0xEC4)};
    if (b.size() >= 0xF3C) p.view_flags = {rd32(b, 0xF30), rd32(b, 0xF34), rd32(b, 0xF38)};
    for (size_t n = 0; n < 4; ++n) {
        size_t o = 0xAD0 + 0x50 * n;
        if (o + 0x40 > b.size()) break;
        SceneLight& l = p.scene_lights[n];
        l.dir = {f(o), f(o + 4), f(o + 8)};
        l.ambient = {f(o + 0x10), f(o + 0x14), f(o + 0x18)};
        l.diffuse = {f(o + 0x20), f(o + 0x24), f(o + 0x28)};
        l.specular = {f(o + 0x30), f(o + 0x34), f(o + 0x38)};
    }
    // The replay passes after the 3D picture (presentation §L6).
    const u32 flags = rd32(b, 0x10);
    p.haze = (flags & 0x20) != 0;
    p.haze_count = rd32(b, 0xB0);
    p.haze_size = f(0xB4), p.haze_box = f(0xB8), p.haze_cap = f(0xBC), p.haze_ahead = f(0xC0);
    p.haze_drift_xz = f(0xC4), p.haze_drift_y = f(0xC8);
    p.vignette = (flags & 0x40) != 0;
    p.vignette_outer = f(0x950), p.vignette_inner = f(0x954);
    for (size_t i = 0; i < 4; ++i) {
        p.vignette_outer_rgba[i] = f(0x958 + 4 * i);
        p.vignette_inner_rgba[i] = f(0x968 + 4 * i);
    }
    if (b.size() >= 0xAD0) {
        p.shade_ambient = {f(0xAC8), f(0xACC)};
        p.shade_diffuse = {f(0xAB4), f(0xABC)};
        p.shade_specular = {f(0xAC0), f(0xAC4)};
    }
    return p;
}

void SunGlare::load(const Vfs& vfs, SDL_Renderer* r, render::GpuContext* gpu, ByteSpan env0) {
    p_ = SunParams::from_env0(env0);
    if (tex_.valid() || !p_.enabled) return;
    std::optional<Bytes> data = vfs.read("race/glare.img");
    if (!data) return;
    try {
        Tex1 tex = Tex1::parse(*data);
        GsMemory gs;
        tex.upload(*data, gs);
        Tex1::Image img = tex.decode(gs, 0);
        // (decode() already maps the GS alpha 0..0x80 to 0..255.)
        if (img.rgba.size() >= size_t(img.width) * img.height)
            tex_.create(r, gpu, int(img.width), int(img.height), img.rgba.data());
    } catch (const std::exception& e) {
        LOG_WARN("Sun glare: %s", e.what());
    }
}

void SunGlare::draw(SDL_Renderer* r, const SDL_FRect& rect, const render::Mat4& view, const render::Frustum& fr,
                    float visibility) const {
    render::ScreenBatch b;
    b.rect = rect;
    add(b, rect, view, fr, visibility);
    render::submit_2d(r, b);
}

void SunGlare::add(render::ScreenBatch& b, const SDL_FRect& rect, const render::Mat4& view, const render::Frustum& fr,
                   float visibility) const {
    if (!p_.enabled || !tex_.valid() || visibility <= 0 || p_.glare_size <= 0) return;
    // The sun direction in camera space (view rotation only).
    render::Vec3 d = p_.direction();
    float cx = view.at(0, 0) * d.x + view.at(0, 1) * d.y + view.at(0, 2) * d.z;
    float cy = view.at(1, 0) * d.x + view.at(1, 1) * d.y + view.at(1, 2) * d.z;
    float cz = view.at(2, 0) * d.x + view.at(2, 1) * d.y + view.at(2, 2) * d.z;
    if (cz >= -1e-3f) return; // behind the camera (it looks down -z)
    float px = fr.screen_x(cx, -cz, rect.x, rect.w), py = fr.screen_y(cy, -cz, rect.y, rect.h);
    float half = p_.glare_size * fr.per_metre(1.0f, rect.h); // angular half-size atan(k)
    if (px + half < rect.x || px - half > rect.x + rect.w || py + half < rect.y || py - half > rect.y + rect.h) return;
    // Colour: sun RGB x visibility, with the original's 0.5 = full weight convention.
    float k = std::clamp(p_.weight, 0.0f, 1.0f) * visibility;
    auto c8 = [&](float v) { return float(u8(std::clamp(v * k, 0.0f, 1.0f) * 255.0f)) / 255.0f; };
    const float rgba[4] = {c8(p_.rgb[0]), c8(p_.rgb[1]), c8(p_.rgb[2]), 1.0f};
    b.add_rect(&tex_, SDL_FRect{px - half, py - half, 2 * half, 2 * half}, rgba);
}

} // namespace gt3r::race
