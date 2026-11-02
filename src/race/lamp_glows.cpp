#include "race/lamp_glows.h"

#include "base/log.h"
#include "formats/ps2/vif.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace gt3r::race {

namespace {

float rdf(ByteSpan d, size_t o) {
    u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

// The glow's stand-in sprite (race/flare.bin missing): a radial disc (white centre, alpha to 0
// at the rim) with thin vertical and diagonal streaks, as race/flare.bin.
void make_sprite(render::SpriteTexture& out, SDL_Renderer* r, render::GpuContext* gpu) {
    constexpr int N = 128;
    std::vector<u32> px(size_t(N) * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float u = (float(x) + 0.5f) / N * 2 - 1, v = (float(y) + 0.5f) / N * 2 - 1;
            // Disc of radius 0.425 inside a sprite spanning +-1.17 (the streak length).
            float d = std::sqrt(u * u + v * v) * 1.17f / 0.425f;
            float a = std::max(0.0f, 1.0f - d);
            a = a * a;
            float sv = std::max(0.0f, 1.0f - std::fabs(u) * 40.0f) * std::max(0.0f, 1.0f - std::fabs(v)); // vertical
            float dd = std::fabs(u - v) * 0.7071f;
            float sd = std::max(0.0f, 1.0f - dd * 40.0f) * std::max(0.0f, 1.0f - std::sqrt(u * u + v * v) / 1.2f);
            a = std::max(a, (84.0f / 255.0f) * std::max(sv, sd));
            u32 c = u32(std::clamp(a, 0.0f, 1.0f) * 255.0f);
            px[size_t(y) * N + size_t(x)] = 0x00FFFFFFu | (c << 24);
        }
    out.create(r, gpu, N, N, px.data());
}

// A sprite centred on (x, y) in `rect` units, half-sizes (hx, hy), coloured `rgb` (c8 bytes / 255).
void add_sprite(render::ScreenBatch& b, const render::SpriteTexture& t, float x, float y, float hx, float hy,
                const float rgb[3]) {
    const float rgba[4] = {rgb[0], rgb[1], rgb[2], 1.0f};
    b.add_rect(&t, SDL_FRect{x - hx, y - hy, 2 * hx, 2 * hy}, rgba);
}

using Blocked = LampGlows::Blocked;

// View-space x / y / z of a world point (camera looks down -z).
render::Vec3 to_view(const render::Mat4& v, const render::Vec3& p) {
    return {v.at(0, 0) * p.x + v.at(0, 1) * p.y + v.at(0, 2) * p.z + v.at(0, 3),
            v.at(1, 0) * p.x + v.at(1, 1) * p.y + v.at(1, 2) * p.z + v.at(1, 3),
            v.at(2, 0) * p.x + v.at(2, 1) * p.y + v.at(2, 2) * p.z + v.at(2, 3)};
}

// The original's occlusion test for a lamp glow: a depth test of a 10 x 10 pixel box (of the
// PS2's 448-line frame) around the screen point (px, py) at depth z - 0.2 m, worth 2 x the
// passing fraction, capped at 1. Here a 3 x 3 grid of rays to that depth against `blocked`.
float box_visibility(float px, float py, float z, const SDL_FRect& rect, const render::Frustum& fr,
                     const render::Mat4& view, const render::Vec3& eye, const Blocked& blocked) {
    float zt = z - 0.2f;
    if (zt <= 0) return 1; // tested at the near plane: everything is behind it
    const render::Vec3 ax{view.at(0, 0), view.at(0, 1), view.at(0, 2)}, ay{view.at(1, 0), view.at(1, 1), view.at(1, 2)},
        az{view.at(2, 0), view.at(2, 1), view.at(2, 2)};
    const float step = 10.0f / 3.0f * rect.h / 448.0f;
    int open = 0;
    for (int gy = -1; gy <= 1; ++gy)
        for (int gx = -1; gx <= 1; ++gx) {
            float sx = px + float(gx) * step, sy = py + float(gy) * step;
            float rx = fr.left + (sx - rect.x) / rect.w * (fr.right - fr.left);
            float ry = fr.up - (sy - rect.y) / rect.h * (fr.up - fr.down);
            open += blocked(eye, eye + ax * (rx * zt) + ay * (ry * zt) - az * zt) ? 0 : 1;
        }
    return std::min(1.0f, 2.0f * float(open) / 9.0f);
}

// d^n for the directional factors, as the original: n = 0 gives 1, n <= 1 gives d itself.
float directional_power(float d, int n) {
    if (n == 0) return 1;
    float D = d;
    for (int i = 1; i < n; ++i) D *= d;
    return D;
}

} // namespace

// --- Flare mesh ---------------------------------------------------------------------

std::vector<Flare::Mesh> Flare::parse(ByteSpan f) {
    // +0x0A u16 mesh count; at +0x10 per mesh {u32 offset, u16 qwc, u16 texture}. A mesh is a
    // VIF packet as GTM1 chunks (docs/formats/car_model.md "VIF packets"): positions V3-32 at
    // 0x00, the strip table S-8 at 0x40 (count, then (vertices - 2) x 3 per strip), colours
    // V4-8 at 0x80; triangle strips, untextured, alpha-blended.
    std::vector<Mesh> out;
    if (f.empty()) return out;
    try {
        u32 n = rd16(f, 0x0A);
        for (u32 m = 0; m < n; ++m) {
            u32 off = rd32(f, 0x10 + 8 * m), qwc = rd16(f, 0x14 + 8 * m);
            if (off > f.size() || size_t(qwc) * 16 > f.size() - off) throw FormatError("flare mesh out of range");
            std::vector<std::array<float, 4>> pos;
            std::vector<std::array<u32, 4>> col, strip;
            for (const ps2::VifCommand& c : ps2::parse_vif(f.subspan(off, size_t(qwc) * 16))) {
                if (!c.is_unpack()) continue;
                ps2::VifUnpack u = c.unpack();
                if (u.addr == 0x00 && u.vn == 2 && u.vl == 0) pos = ps2::unpack_floats(u);
                else if (u.addr == 0x40 && u.vn == 0 && u.vl == 2) strip = ps2::unpack_words(u);
                else if (u.addr == 0x80 && u.vn == 3 && u.vl == 2) col = ps2::unpack_words(u);
            }
            if (pos.empty() || col.size() != pos.size() || strip.empty()) throw FormatError("flare mesh incomplete");
            Mesh mesh;
            for (size_t i = 0; i < pos.size(); ++i) {
                Vertex v{pos[i][0], pos[i][1], pos[i][2], {}};
                for (int k = 0; k < 4; ++k) v.rgba[k] = u8(col[i][size_t(k)]);
                mesh.verts.push_back(v);
            }
            size_t first = 0;
            for (u32 s = 1; s <= strip[0][0] && s < strip.size(); ++s) {
                size_t len = strip[s][0] / 3 + 2;
                if (first + len > mesh.verts.size()) throw FormatError("flare strip past the vertices");
                for (size_t i = 0; i + 2 < len; ++i)
                    for (size_t k = 0; k < 3; ++k) mesh.tris.push_back(int(first + i + k));
                first += len;
            }
            out.push_back(std::move(mesh));
        }
    } catch (const FormatError& e) {
        LOG_WARN("Lamp glows: race/flare.bin: %s", e.what());
        out.clear();
    }
    return out;
}

std::array<u8, 4> Flare::gs_color(const Vertex& v, const std::array<float, 3>& col, float vis) {
    const float q = std::floor(std::clamp(vis, 0.0f, 1.0f) * 255.0f) / 255.0f;
    return {u8(std::floor(std::min(255.0f, float(v.rgba[0]) * col[0]))),
            u8(std::floor(std::min(255.0f, float(v.rgba[1]) * col[1]))),
            u8(std::floor(std::min(255.0f, float(v.rgba[2]) * col[2]))),
            u8(std::floor(std::min(255.0f, float(v.rgba[3]) * q)))};
}

void Flare::add_mesh(const Mesh& m, render::ScreenBatch& b, const render::Frustum& fr, const SDL_FRect& rect, float x,
                     float y, float d, float sx, float sy, const std::array<float, 3>& col, float vis) {
    std::vector<render::ScreenBatch::Vertex> verts;
    verts.reserve(m.verts.size());
    for (const Vertex& v : m.verts) {
        float vx = x + v.x * sx, vy = y + v.y * sy, vd = d - v.z * sx;
        const std::array<u8, 4> c = gs_color(v, col, vis);
        render::ScreenBatch::Vertex sv;
        sv.x = fr.screen_x(vx, vd, rect.x, rect.w), sv.y = fr.screen_y(vy, vd, rect.y, rect.h);
        for (int i = 0; i < 4; ++i) sv.rgba[i] = float(c[size_t(i)]) / 255.0f;
        verts.push_back(sv);
    }
    std::vector<u32> idx(m.tris.begin(), m.tris.end());
    b.add(nullptr, verts.data(), verts.size(), idx.data(), idx.size(), 2); // Cs As / 128: twice Cs As / 255
}

void Flare::add(render::ScreenBatch& b, const render::Frustum& fr, const SDL_FRect& rect, float x, float y, float d,
                float s, const std::array<float, 3>& col, float vis) const {
    for (const Mesh& m : meshes_) add_mesh(m, b, fr, rect, x, y, d, s, s, col, vis);
}

const Flare::Mesh& Flare::disc_mesh() {
    // The flare system's own mesh, built at start-up: rim j = 2k + i (i = 0..2) of strip
    // k at the angle (4k + 2i) x pi / 14 in single precision (j = 14: 0), cos / sin in double;
    // the strip table 7, 6 x 7 (seven strips of four vertices).
    static const Mesh mesh = [] {
        Mesh m;
        constexpr float kPi = 3.14159274f; // f32 pi
        for (int k = 0; k < 7; ++k) {
            const int first = int(m.verts.size());
            for (int i = 0; i < 3; ++i) {
                const int j = 2 * k + i;
                const float a = j == 14 ? 0.0f : float(4 * k + 2 * i) * kPi / 14.0f;
                Vertex rim{float(std::cos(double(a))), float(std::sin(double(a))), 0, {}};
                m.verts.push_back(rim);
                if (i == 0) m.verts.push_back(Vertex{0, 0, 0, {0xFF, 0xFF, 0xFF, 0xFF}});
            }
            for (int t = 0; t < 2; ++t)
                for (int c = 0; c < 3; ++c) m.tris.push_back(first + t + c);
        }
        return m;
    }();
    return mesh;
}

// --- Course lamps -------------------------------------------------------------------

void LampGlows::load(SDL_Renderer* r, render::GpuContext* gpu, ByteSpan g, ByteSpan env, ByteSpan vls0, ByteSpan flare) {
    lists_.clear();
    count_ = 0;
    if (env.size() < 0xE60 || std::memcmp(env.data(), "Env0", 4) != 0 || !(rd32(env, 0x10) & 0x200000)) return;
    for (int i = 0; i < 8; ++i) {
        size_t o = 0xC60 + 0x40 * size_t(i);
        LampType& t = types_[size_t(i)];
        t.rgb = {rdf(env, o), rdf(env, o + 4), rdf(env, o + 8)};
        t.intensity = rdf(env, o + 0xC);
        t.glow = rdf(env, o + 0x10) * 10.0f;
        t.halo = rdf(env, o + 0x14);
        t.halo_gain = rdf(env, o + 0x18);
        t.exponent = rdf(env, o + 0x1C);
        t.att_c = rdf(env, o + 0x20), t.att_l = rdf(env, o + 0x24), t.att_q = rdf(env, o + 0x28);
        t.max_dist = rdf(env, o + 0x2C);
        t.box = rdf(env, o + 0x30) * 128.0f; // passed to the occlusion test, which uses 10 px instead
        t.blink = rdf(env, o + 0x34);
    }
    if (g.size() < 0x18 || std::memcmp(g.data(), "GTFX", 4) != 0 || !rd32(g, 0x10)) return;
    u32 table = rd32(g, 0x14);
    if (table + 4 > g.size()) return;
    u32 lists = rd32(g, table);
    for (u32 i = 0; i < lists && table + 8 + 4ull * i <= g.size(); ++i) {
        std::vector<Lamp>& list = lists_.emplace_back();
        u32 lp = rd32(g, table + 4 + 4 * i);
        if (!lp || lp + 4 > g.size()) continue;
        u32 n = rd32(g, lp);
        for (u32 k = 0; k < n && lp + 4 + 32ull * (k + 1) <= g.size(); ++k) {
            size_t o = lp + 4 + 32ull * k;
            Lamp l;
            l.type = int(rd32(g, o) & 7);
            l.pos = {rdf(g, o + 4), rdf(g, o + 8), rdf(g, o + 12)};
            l.dir = {rdf(g, o + 16), rdf(g, o + 20), rdf(g, o + 24)};
            l.phase = rdf(g, o + 28);
            list.push_back(l);
        }
        count_ += list.size();
    }
    if (!count_) {
        lists_.clear();
        return;
    }
    vis_ = course::Visibility::parse(vls0);
    flare_.load(flare);
    if (flare_.empty() && !glow_.valid()) make_sprite(glow_, r, gpu);
    LOG_INFO("Lamp glows: %zu lamps in %zu lists, visibility lists %s, flare %s", count_, lists_.size(),
             vis_.empty() ? "absent" : "on", flare_.empty() ? "missing" : "loaded");
}

void LampGlows::draw(SDL_Renderer* r, const SDL_FRect& rect, const render::Mat4& view, const render::Vec3& eye,
                     const render::Frustum& fr, const Blocked& blocked) const {
    render::ScreenBatch b;
    b.rect = rect;
    add(b, rect, view, eye, fr, blocked);
    render::submit_2d(r, b);
}

void LampGlows::add(render::ScreenBatch& b, const SDL_FRect& rect, const render::Mat4& view, const render::Vec3& eye,
                    const render::Frustum& fr, const Blocked& blocked) const {
    if (lists_.empty() || (flare_.empty() && !glow_.valid())) return;
    // The lamps the original keeps this frame: the lists of the nodes visible from the
    // camera, in that order (every list without visibility data), at most 100 that pass the
    // screen test, whether or not they face the camera.
    struct Kept {
        const Lamp* lamp;
        render::Vec3 v; // view space
        float px, py;
    };
    std::vector<Kept> kept;
    auto visit = [&](const std::vector<Lamp>& list) {
        for (const Lamp& l : list) {
            if (kept.size() >= 100) return;
            const LampType& ty = types_[size_t(l.type)];
            render::Vec3 v = to_view(view, l.pos);
            float z = -v.z;
            if (z <= 0 || z > ty.max_dist) continue;
            // On screen: the square of the halo size around the lamp (not the glow size).
            float px = fr.screen_x(v.x, z, rect.x, rect.w), py = fr.screen_y(v.y, z, rect.y, rect.h);
            float hr = ty.halo * fr.per_metre(z, rect.h);
            if (px - hr > rect.x + rect.w || px + hr < rect.x || py - hr > rect.y + rect.h || py + hr < rect.y) continue;
            kept.push_back({&l, v, px, py});
        }
    };
    if (const std::vector<u16>* nodes = vis_.visible(eye.x, eye.y, eye.z)) {
        for (u16 n : *nodes)
            if (n < lists_.size()) visit(lists_[n]);
    } else {
        for (const std::vector<Lamp>& list : lists_) visit(list);
    }

    auto c8 = [](float v) { return float(u8(std::clamp(v, 0.0f, 1.0f) * 255.0f)) / 255.0f; };
    for (const Kept& k : kept) {
        const Lamp& l = *k.lamp;
        const LampType& ty = types_[size_t(l.type)];
        const float z = -k.v.z;
        // Directional factor. n = int(exponent + 0.5) on the EE's round-toward-zero FPU equals
        // lround for exponent >= 0 (Route 11's 0.49999997 gives 0, i.e. no directional factor).
        float D = 1;
        bool facing = true;
        int n = int(std::lround(ty.exponent));
        if (n != 0) {
            render::Vec3 cl = l.pos - eye;
            float len = std::sqrt(dot(cl, cl));
            float d = len > 0 ? dot(l.dir, cl) / len : 0; // the facing as stored, not renormalised
            facing = d >= 0;
            if (facing) D = directional_power(d, n);
        }
        float att = 1.0f / (ty.att_q * z * z + ty.att_l * z + ty.att_c);
        if (ty.blink > 0) att *= 0.5f + 0.5f * float(std::sin(2 * 3.14159265 * (time_ / ty.blink + l.phase)));
        const float zp = z - 0.5f; // pulled 0.5 m towards the camera along the view ray
        if (facing && zp > 0) {
            float vis = box_visibility(k.px, k.py, z, rect, fr, view, eye, blocked);
            const float c = ty.intensity * att * D;
            if (vis > 0 && !flare_.empty()) {
                const float f = zp / z;
                flare_.add(b, fr, rect, k.v.x * f, k.v.y * f, zp, ty.glow, {ty.rgb[0] * c, ty.rgb[1] * c, ty.rgb[2] * c},
                           vis);
            } else if (vis > 0 && glow_.valid()) { // stand-in sprite
                float glow_r = ty.glow * 1.17f * fr.per_metre(zp, rect.h);
                const float rgb[3] = {c8(ty.rgb[0] * c * vis), c8(ty.rgb[1] * c * vis), c8(ty.rgb[2] * c * vis)};
                add_sprite(b, glow_, k.px, k.py, glow_r, glow_r, rgb);
            }
        }
        // Halo: the disc mesh of the halo size at the pulled point, depth-tested there (a ray
        // here), colour x gain x 0.5 at full visibility, not scaled by D.
        if (ty.halo > 0 && ty.halo_gain > 0 && z > 1 && zp > 0) {
            float halo_r = ty.halo * fr.per_metre(zp, rect.h);
            if (halo_r > 0.5f && !blocked(eye, eye + (l.pos - eye) * (zp / z))) {
                const float c = ty.intensity * att * ty.halo_gain * 0.5f, f = zp / z;
                Flare::add_mesh(Flare::disc_mesh(), b, fr, rect, k.v.x * f, k.v.y * f, zp, ty.halo, ty.halo,
                                {ty.rgb[0] * c, ty.rgb[1] * c, ty.rgb[2] * c}, 1.0f);
            }
        }
    }
}

// --- Car lamps --------------------------------------------------------------------

CarLampSet CarLampSet::parse(ByteSpan g) {
    CarLampSet out;
    if (g.size() < 0x58) return out;
    auto vec = [&](size_t o) { return render::Vec3{rdf(g, o), rdf(g, o + 4), rdf(g, o + 8)}; };
    auto list = [&](size_t at, std::vector<CarLampRecord>& dst) {
        u32 n = rd32(g, at), off = rd32(g, at + 4);
        for (u32 i = 0; i < n && i < 32 && off + 0x80ull * (i + 1) <= g.size(); ++i) {
            size_t o = off + 0x80ull * i;
            CarLampRecord l;
            l.xaxis = vec(o), l.view_axis = vec(o + 0x20), l.pos = vec(o + 0x30), l.test = vec(o + 0x4C);
            l.a = rdf(g, o + 0x40), l.b = rdf(g, o + 0x44);
            l.rgb = {rdf(g, o + 0x58), rdf(g, o + 0x5C), rdf(g, o + 0x60)};
            l.exponent = int(rd32(g, o + 0x64));
            l.fade = (rd32(g, o + 0x68) & 1) != 0;
            dst.push_back(l);
        }
    };
    list(0x40, out.head);
    list(0x48, out.tail);
    list(0x50, out.brake);
    return out;
}

void CarLampGlows::load(SDL_Renderer* r, render::GpuContext* gpu, ByteSpan env, ByteSpan flare) {
    active_ = false;
    if (env.size() < 0xA1C || std::memcmp(env.data(), "Env0", 4) != 0 || !(rd32(env, 0x10) & 0x8000)) return;
    LampType& t = type_;
    t.rgb = {rdf(env, 0x9F0), rdf(env, 0x9F4), rdf(env, 0x9F8)};
    t.intensity = rdf(env, 0x9FC);
    t.glow = rdf(env, 0xA00) * 8.0f;
    t.halo = rdf(env, 0xA04);
    t.halo_gain = rdf(env, 0xA08);
    t.att_c = rdf(env, 0xA0C), t.att_l = rdf(env, 0xA10), t.att_q = rdf(env, 0xA14);
    t.max_dist = rdf(env, 0xA18);
    flare_.load(flare);
    if (flare_.empty() && !glow_.valid()) make_sprite(glow_, r, gpu);
    active_ = glow_.valid() || !flare_.empty();
}

void CarLampGlows::draw(SDL_Renderer* r, const SDL_FRect& rect, const render::Mat4& view, const render::Vec3& eye,
                        const render::Frustum& fr, const std::vector<Car>& cars, const Blocked& blocked) const {
    render::ScreenBatch b;
    b.rect = rect;
    add(b, rect, view, eye, fr, cars, blocked);
    render::submit_2d(r, b);
}

void CarLampGlows::add(render::ScreenBatch& b, const SDL_FRect& rect, const render::Mat4& view, const render::Vec3& eye,
                       const render::Frustum& fr, const std::vector<Car>& cars, const Blocked& blocked) const {
    if (!active_) return;
    const LampType& ty = type_;
    auto c8 = [](float v) { return float(u8(std::clamp(v, 0.0f, 1.0f) * 255.0f)) / 255.0f; };
    auto xdir = [](const render::Mat4& m, const render::Vec3& d) {
        return render::Vec3{m.at(0, 0) * d.x + m.at(0, 1) * d.y + m.at(0, 2) * d.z,
                            m.at(1, 0) * d.x + m.at(1, 1) * d.y + m.at(1, 2) * d.z,
                            m.at(2, 0) * d.x + m.at(2, 1) * d.y + m.at(2, 2) * d.z};
    };
    int kept = 0;
    // `glow`: head and brake lamps (tail lamps only ever get the halo); `I` the brake factor.
    auto lamp = [&](const Car& car, const CarLampRecord& l, bool glow, float I) {
        if (kept >= 100) return;
        // Screen test at the test point: in front, within the max distance, the square of
        // halo x max(a, b) on screen.
        render::Vec3 tp = car.model.transform_point(l.test);
        render::Vec3 tv = to_view(view, tp);
        float tz = -tv.z;
        if (tz <= 0 || tz > ty.max_dist) return;
        float tpx = fr.screen_x(tv.x, tz, rect.x, rect.w), tpy = fr.screen_y(tv.y, tz, rect.y, rect.h);
        float hr = ty.halo * std::max(l.a, l.b) * fr.per_metre(tz, rect.h);
        if (tpx - hr > rect.x + rect.w || tpx + hr < rect.x || tpy - hr > rect.y + rect.h || tpy + hr < rect.y) return;
        ++kept;
        const bool bar = std::fabs(l.a - l.b) >= 0.01f;
        render::Vec3 axis = xdir(car.model, l.view_axis);
        // Directional factor at the test point (round head and brake lamps only).
        float D = -1;
        if (glow && !bar && l.exponent == 0) {
            D = 1; // n = 0: no directional factor, whatever the side
        } else if (glow && !bar) {
            float len = std::sqrt(dot(tv, tv));
            render::Vec3 av{dot(render::Vec3{view.at(0, 0), view.at(0, 1), view.at(0, 2)}, axis),
                            dot(render::Vec3{view.at(1, 0), view.at(1, 1), view.at(1, 2)}, axis),
                            dot(render::Vec3{view.at(2, 0), view.at(2, 1), view.at(2, 2)}, axis)};
            float d = len > 0 ? dot(av, tv) / len : 0;
            D = d < 0 ? -1.0f : directional_power(d, l.exponent);
        }
        // The glow and halo sit at the lamp position, pulled along the view ray to depth
        // z + 0.5 x the viewing axis' view-space z.
        render::Vec3 gv = to_view(view, car.model.transform_point(l.pos));
        float z = -gv.z;
        if (z <= 0) return;
        float az = view.at(2, 0) * axis.x + view.at(2, 1) * axis.y + view.at(2, 2) * axis.z;
        float zp = z + 0.5f * az;
        float att = 1.0f / (ty.att_q * z * z + ty.att_l * z + ty.att_c);
        const std::array<float, 3> col{ty.rgb[0] * ty.intensity * l.rgb[0] * att, ty.rgb[1] * ty.intensity * l.rgb[1] * att,
                                       ty.rgb[2] * ty.intensity * l.rgb[2] * att};
        if (zp <= 0) return;
        const float f = zp / z;
        float px = fr.screen_x(gv.x, z, rect.x, rect.w), py = fr.screen_y(gv.y, z, rect.y, rect.h);
        float per_m = fr.per_metre(zp, rect.h);
        if (D >= 0) {
            float vis = box_visibility(tpx, tpy, tz, rect, fr, view, eye, blocked);
            float k = D * I;
            if (vis > 0 && k > 0 && !flare_.empty()) {
                flare_.add(b, fr, rect, gv.x * f, gv.y * f, zp, ty.glow * l.a, {col[0] * k, col[1] * k, col[2] * k}, vis);
            } else if (vis > 0 && k > 0 && glow_.valid()) { // stand-in sprite
                float glow_r = ty.glow * l.a * 1.17f * per_m;
                const float rgb[3] = {c8(col[0] * k * vis), c8(col[1] * k * vis), c8(col[2] * k * vis)};
                add_sprite(b, glow_, px, py, glow_r, glow_r, rgb);
            }
        }
        // Halo: the disc mesh at the pulled point (depth-tested there; a ray here), colour x gain x
        // 0.5 at full visibility, not scaled by D or I. Bar lamps: an ellipse along the lamp's
        // projected x axis.
        float hx = ty.halo * l.a, hy = hx; // metres
        if (bar) {
            render::Vec3 xa = normalize(xdir(car.model, l.xaxis));
            float pxv = view.at(0, 0) * xa.x + view.at(0, 1) * xa.y + view.at(0, 2) * xa.z;
            hx = ty.halo * ((l.a - l.b) * std::fabs(pxv) + l.b);
            hy = ty.halo * l.b;
        }
        if (ty.halo_gain > 0 && z > 1 && (hx * per_m > 0.5f || hy * per_m > 0.5f) &&
            !blocked(eye, eye + (car.model.transform_point(l.pos) - eye) * f)) {
            const float k = ty.halo_gain * 0.5f;
            Flare::add_mesh(Flare::disc_mesh(), b, fr, rect, gv.x * f, gv.y * f, zp, hx, hy,
                            {col[0] * k, col[1] * k, col[2] * k}, 1.0f);
        }
    };
    for (const Car& car : cars) {
        if (!car.lamps) continue;
        for (const CarLampRecord& l : car.lamps->head) lamp(car, l, true, 1.0f);
        if (car.k > 0)
            for (const CarLampRecord& l : car.lamps->brake) {
                float I = l.fade ? car.k : (car.brake_now ? 1.0f : 0.0f);
                if (I > 0) lamp(car, l, true, I);
            }
        for (const CarLampRecord& l : car.lamps->tail) lamp(car, l, false, 1.0f);
    }
}

} // namespace gt3r::race
