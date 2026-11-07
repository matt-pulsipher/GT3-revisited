#include "race/wet_road.h"

#include "base/log.h"
#include "formats/course/course_model.h"
#include "formats/gs_memory.h"
#include "formats/tex1.h"
#include "formats/texture_pack.h"
#include "render/gpu_context.h"
#include "render/gpu_model.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt3r::race {

namespace {

float rdf(ByteSpan d, size_t o) {
    u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

// A quad model: corners in (x, y, z) with UVs, one primitive with `flags`, the given colour.
std::unique_ptr<render::GpuModel> make_quad(render::GpuContext& ctx, const std::array<render::Vec3, 4>& pos,
                                            const std::array<std::array<float, 2>, 4>& uv, u32 color, u32 flags,
                                            int texture = -1, scene::Texture* tex = nullptr) {
    scene::Model m;
    if (tex) m.textures.push_back(*tex);
    scene::Mesh mesh;
    mesh.name = "quad";
    for (int i = 0; i < 4; ++i) {
        scene::Vertex v;
        v.pos[0] = pos[size_t(i)].x, v.pos[1] = pos[size_t(i)].y, v.pos[2] = pos[size_t(i)].z;
        v.normal[2] = 1;
        v.uv[0] = uv[size_t(i)][0], v.uv[1] = uv[size_t(i)][1];
        v.color = color;
        mesh.vertices.push_back(v);
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    scene::Primitive p;
    p.index_count = 6;
    p.texture = texture;
    p.flags = flags;
    mesh.primitives.push_back(p);
    m.meshes.push_back(std::move(mesh));
    m.compute_bounds();
    return std::make_unique<render::GpuModel>(ctx, m);
}

constexpr u32 kQuadFlags = scene::kUnlit | scene::kNoDepthWrite | scene::kDoubleSided;

} // namespace

WetRoad::WetRoad() = default;
WetRoad::~WetRoad() = default;

bool WetRoad::load(render::GpuContext& ctx, const Vfs& vfs, ByteSpan course, ByteSpan env) {
    ctx_ = &ctx;
    reflection_.reset(), mask_.reset(), streaks_.reset();
    for (FrameQuads& q : quads_) q = FrameQuads{};
    for (auto& q : blur_quad_) q.reset();
    p_ = {};
    if (env.size() < 0xE80 || std::memcmp(env.data(), "Env0", 4) != 0) return false;
    try {
        course::CourseModelOptions o;
        o.scenery = o.course_line = o.backdrop = o.sky = false;
        o.road_reflection = true;
        std::vector<gtm1::ClutSet> cluts;
        scene::Model refl = course::build_course_model(course, o, "reflection", &cluts);
        if (refl.meshes.empty()) return false;
        reflection_ = std::make_unique<render::GpuModel>(ctx, refl);
        cluts_ = {};
        cluts_.add(reflection_.get(), std::move(cluts));
        o.road_reflection = false;
        o.road_reflection_mask = true;
        o.frame_alpha = true; // its passes write only the frame alpha
        scene::Model mask = course::build_course_model(course, o, "reflection_mask");
        // (Its vertex alpha is the weight: GT3 clips it at the guard band, DrawItem::guard_band.)
        if (!mask.meshes.empty()) mask_ = std::make_unique<render::GpuModel>(ctx, mask, true);
    } catch (const std::exception& e) {
        LOG_WARN("Wet road: %s", e.what());
        reflection_.reset();
        return false;
    }
    u32 flags = rd32(env, 0x10);
    p_.mode = int(rd32(env, 0xC20) & 0xFF);
    p_.width = std::clamp(int(rd32(env, 0xC24)), 8, 1024);
    p_.height = std::clamp(int(rd32(env, 0xC28)), 8, 1024);
    p_.blur = std::clamp(int(rd32(env, 0xC2C)), 0, 16);
    p_.fog_rgb = {rdf(env, 0xC40), rdf(env, 0xC44), rdf(env, 0xC48)};
    p_.fog_a = rdf(env, 0xC50), p_.fog_b = rdf(env, 0xC54);
    p_.cars = (flags & 0x10000) != 0;
    p_.streaks = (flags & 0x20000) != 0;
    p_.streak_rgb = {rdf(env, 0xE60), rdf(env, 0xE64), rdf(env, 0xE68)};
    p_.streak_gain = rdf(env, 0xE6C);
    p_.width_head_on = rdf(env, 0xE70), p_.width_side = rdf(env, 0xE74);
    p_.cap = rdf(env, 0xE78), p_.length = rdf(env, 0xE7C);

    // Blur passes: the buffer onto itself through an NDC quad (identity view and projection),
    // source shifted by -0.5 / +0.5 texel (GT3's sprites: UV 0 / 1 at the GS's pixels).
    for (int k = 0; k < 2; ++k) {
        float du = (k == 0 ? -0.5f : 0.5f) / float(p_.width), dv = (k == 0 ? -0.5f : 0.5f) / float(p_.height);
        blur_quad_[k] = make_quad(ctx, {render::Vec3{-1, 1, 0}, {1, 1, 0}, {1, -1, 0}, {-1, -1, 0}},
                                  {{{du, dv}, {1 + du, dv}, {1 + du, 1 + dv}, {du, 1 + dv}}}, 0xFFFFFFFFu, kQuadFlags);
    }
    // Streaks: race/reflection.img; the blend ignores the alpha (Cs + Cd), so the texels' is 1.
    if (p_.streaks)
        if (std::optional<Bytes> img = vfs.read("race/reflection.img")) try {
                Tex1 t = Tex1::parse(*img);
                GsMemory gs;
                t.upload(*img, gs);
                Tex1::Image im = t.decode(gs, 0);
                texture_pack::replace_decoded(*img, 0, im);
                scene::Model m;
                scene::Texture st;
                st.name = "reflection";
                st.width = im.width, st.height = im.height;
                st.rgba = std::move(im.rgba);
                for (u32& c : st.rgba) c |= 0xFF000000u;
                st.clamp_u = st.clamp_v = true;
                m.textures.push_back(std::move(st));
                scene::Mesh mesh;
                mesh.name = "streaks";
                streak_verts_.assign(size_t(kMaxStreaks) * 4, scene::Vertex{});
                for (size_t i = 0; i < streak_verts_.size(); ++i) // spread: the bounds never cull the item
                    streak_verts_[i].pos[0] = (i & 1) ? 5000.0f : -5000.0f, streak_verts_[i].color = 0;
                mesh.vertices = streak_verts_;
                for (u32 q = 0; q < u32(kMaxStreaks); ++q)
                    for (u32 k : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(q * 4 + k);
                scene::Primitive pr;
                pr.index_count = u32(mesh.indices.size());
                pr.texture = 0;
                pr.flags = scene::kAdditive | kQuadFlags;
                mesh.primitives.push_back(pr);
                m.meshes.push_back(std::move(mesh));
                m.compute_bounds();
                streaks_ = std::make_unique<render::GpuModel>(ctx, m);
            } catch (const std::exception& e) {
                LOG_WARN("Wet road: race/reflection.img: %s", e.what());
            }
    LOG_INFO("Wet road: reflection pass, mode 0x%02X, %dx%d buffer, %d blur pass(es), mask %s, cars %s, streaks %s", p_.mode,
             p_.width, p_.height, p_.blur, mask_ ? "yes" : "no", p_.cars ? "yes" : "no", streaks_ ? "yes" : "no");
    return true;
}

void WetRoad::add_buffer_items(std::vector<render::DrawItem>& items, const render::Mat4& view, const render::Vec3& eye,
                               const std::vector<Car>& cars) {
    if (!reflection_) return;
    items.push_back({reflection_.get(), {}, 0});
    if (!streaks_) return;
    // Eye-space basis (view rows: right, up, back).
    render::Vec3 right{view.at(0, 0), view.at(0, 1), view.at(0, 2)}, up{view.at(1, 0), view.at(1, 1), view.at(1, 2)},
        back{view.at(2, 0), view.at(2, 1), view.at(2, 2)};
    auto to_eye = [&](const render::Vec3& p) { return view.transform_point(p); };
    auto to_world = [&](const render::Vec3& q) { return eye + right * q.x + up * q.y + back * q.z; };
    auto xdir = [](const render::Mat4& m, const render::Vec3& d) {
        return render::Vec3{m.at(0, 0) * d.x + m.at(0, 1) * d.y + m.at(0, 2) * d.z,
                            m.at(1, 0) * d.x + m.at(1, 1) * d.y + m.at(1, 2) * d.z,
                            m.at(2, 0) * d.x + m.at(2, 1) * d.y + m.at(2, 2) * d.z};
    };
    auto byte = [](float x) { return u32(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
    size_t n = 0;
    auto streak = [&](const Car& car, const CarLampRecord& l, float I) {
        if (n >= size_t(kMaxStreaks) || I <= 0) return;
        render::Mat4 m = car.mirror * car.model;
        render::Vec3 img = m.transform_point(l.pos);
        render::Vec3 p = to_eye(img);
        if (p.z + 0.5f > 0) return; // behind (or at) the eye: the camera looks down -z
        float len = render::length(p);
        if (len < 1e-3f) return;
        render::Vec3 axis = render::normalize(xdir(m, l.view_axis));
        render::Vec3 ae = {render::dot(right, axis), render::dot(up, axis), render::dot(back, axis)};
        float c = render::dot(ae, p) / len;
        if (c < 0) return;
        render::Vec3 q = p * ((p.z + 0.5f) / p.z); // 0.5 m towards the eye along the view ray
        float w = (p_.width_side + (p_.width_head_on - p_.width_side) * c) * l.a;
        float top = q.y + std::min(p_.cap, l.pos.y), bottom = q.y - p_.length;
        const float f = I * c * p_.streak_gain;
        u32 col = byte(l.rgb[0] * p_.streak_rgb[0] * f) | byte(l.rgb[1] * p_.streak_rgb[1] * f) << 8 |
                  byte(l.rgb[2] * p_.streak_rgb[2] * f) << 16 | 0xFF000000u;
        const render::Vec3 corners[4] = {{q.x + w, top, q.z}, {q.x - w, top, q.z}, {q.x - w, bottom, q.z}, {q.x + w, bottom, q.z}};
        const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}}; // u = 0 on the +x side, v = 0 at the top
        for (int k = 0; k < 4; ++k) {
            scene::Vertex& v = streak_verts_[n * 4 + size_t(k)];
            render::Vec3 wp = to_world(corners[k]);
            v.pos[0] = wp.x, v.pos[1] = wp.y, v.pos[2] = wp.z;
            v.normal[0] = back.x, v.normal[1] = back.y, v.normal[2] = back.z;
            v.uv[0] = uv[k][0], v.uv[1] = uv[k][1];
            v.color = col;
        }
        ++n;
    };
    for (const Car& car : cars) {
        if (!car.lamps) continue;
        for (const CarLampRecord& l : car.lamps->head) streak(car, l, 1.0f);
        if (car.k > 0)
            for (const CarLampRecord& l : car.lamps->brake) streak(car, l, l.fade ? car.k : (car.brake_now ? 1.0f : 0.0f));
        for (const CarLampRecord& l : car.lamps->tail) streak(car, l, 0.3f);
    }
    for (size_t i = n * 4; i < streak_verts_.size(); ++i) streak_verts_[i].color = 0; // unused: black (added: nothing)
    if (n == 0) return;
    streaks_->update_vertices(streak_verts_);
    items.push_back({streaks_.get(), {}, 0});
}

render::DrawOptions WetRoad::buffer_options(const render::DrawOptions& main) const {
    render::DrawOptions o = main;
    for (float& c : o.clear) c = 0;
    o.fixed_width = o.fixed_height = 0;
    o.cull = render::CullMode::None; // pre-mirrored scenery and mirrored cars
    o.quantize_rgb5 = false;
    // The buffer's fog: Env0 +0xC40 (the +0x60 layout; b >= 1 = none).
    o.fog = p_.fog_b < 1.0f;
    o.fog_gt3 = true;
    for (int i = 0; i < 3; ++i) o.fog_color[i] = p_.fog_rgb[size_t(i)];
    o.fog_start = p_.fog_a, o.fog_max = 1.0f - p_.fog_b;
    return o;
}

SDL_GPUTexture* WetRoad::render(render::View3D& v, const render::Mat4& view, const render::Mat4& proj,
                                const render::Vec3& eye, const std::vector<render::DrawItem>& items,
                                const render::DrawOptions& main) {
    if (!reflection_) return nullptr;
    SDL_GPUTexture* t = v.render_offscreen(0, p_.width, p_.height, view, proj, eye, items, buffer_options(main));
    if (!t) return nullptr;
    render::DrawOptions bo;
    for (float& c : bo.clear) c = 0;
    bo.fog = false;
    bo.frustum_cull = false;
    bo.sort_blended = false;
    for (int k = 0; k < p_.blur && blur_quad_[0]; ++k) {
        render::DrawItem it{blur_quad_[k & 1].get(), {}, 0};
        it.texture = t;
        it.gs_bilinear = true;
        it.wheel_steps = {float(p_.width), float(p_.height), 0, 0};
        t = v.render_offscreen(1 + (k & 1), p_.width, p_.height, {}, {}, {0, 0, 0}, {it}, bo);
        if (!t) return nullptr;
    }
    return t;
}

WetRoad::FrameQuads& WetRoad::frame_quads(const render::Frustum& fr) {
    auto same = [&](const render::Frustum& a) {
        return std::fabs(a.left - fr.left) < 1e-5f && std::fabs(a.right - fr.right) < 1e-5f &&
               std::fabs(a.down - fr.down) < 1e-5f && std::fabs(a.up - fr.up) < 1e-5f;
    };
    FrameQuads* q = nullptr;
    for (FrameQuads& c : quads_)
        if (c.clear && same(c.fr)) q = &c;
    if (!q) { // the least recently used pair
        q = quads_[0].used <= quads_[1].used ? &quads_[0] : &quads_[1];
        q->fr = fr;
        // Camera space (x right, y up, looking down -z), just past the 0.1 m near plane.
        const float d = 0.105f * 1.01f;
        const float l = fr.left * d, r = fr.right * d, b = fr.down * d, t = fr.up * d;
        const std::array<render::Vec3, 4> pos = {render::Vec3{l, t, -0.105f}, {r, t, -0.105f}, {r, b, -0.105f},
                                                 {l, b, -0.105f}};
        // The composite's net -0.5 texel shift: a GS pixel samples the scene at its integer position
        // (the buffer's XYOFFSET is whole pixels), so GT3's texels hold it half a texel up and left
        // of ours.
        float du = -0.5f / float(p_.width), dv = -0.5f / float(p_.height);
        q->clear = make_quad(*ctx_, pos, {{{0, 0}, {1, 0}, {1, 1}, {0, 1}}}, 0x00FFFFFFu, scene::kAlphaOnly | kQuadFlags);
        q->frame = make_quad(*ctx_, pos, {{{du, dv}, {1 + du, dv}, {1 + du, 1 + dv}, {du, 1 + dv}}}, 0xFFFFFFFFu,
                             scene::kAlphaBlend | kQuadFlags);
    }
    q->used = ++quad_uses_;
    return *q;
}

void WetRoad::add_frame_items(std::vector<render::DrawItem>& items, const render::Mat4& view, const render::Frustum& fr,
                              SDL_GPUTexture* buffer) {
    if (!reflection_ || !buffer || !ctx_) return;
    FrameQuads& q = frame_quads(fr);
    if (!q.clear || !q.frame) return;
    render::Mat4 cam = render::inverse(view); // camera -> world
    if (mask_) { // (without slot 13 the frame alpha the scenery left is the weight)
        render::DrawItem clr{q.clear.get(), cam, 0};
        clr.in_order = clr.frame_alpha = true;
        items.push_back(clr);
        render::DrawItem mk{mask_.get(), {}, 0};
        mk.in_order = mk.frame_alpha = mk.guard_band = true;
        items.push_back(mk);
    }
    render::DrawItem comp{q.frame.get(), cam, 0};
    comp.in_order = true;
    comp.texture = buffer;
    comp.gs_bilinear = true;
    comp.wheel_steps = {float(p_.width), float(p_.height), 0, 0};
    comp.no_fog = true;
    comp.dest_alpha_blend = p_.mode == 0x54 ? 1 : 2;
    items.push_back(comp);
}

} // namespace gt3r::race
