#include "race/race_scene.h"

#include "base/log.h"
#include "formats/course/course_model.h"
#include "formats/model/wheel_model.h"
#include "race/interp.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <optional>

namespace gt3r::race {

namespace {

const char* const kWheelNames[4] = {"wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"};

void log_bounds(const char* what, const scene::Model& m) {
    LOG_INFO("Race: %s bounds (%.0f %.0f %.0f) .. (%.0f %.0f %.0f), %zu meshes", what, m.bounds_min[0], m.bounds_min[1],
             m.bounds_min[2], m.bounds_max[0], m.bounds_max[1], m.bounds_max[2], m.meshes.size());
}

} // namespace

// The car model's origin (y = 0, the body's lowest point) sits at the static body height, which
// is the ride height q above the ground (GT3 places the body at ground + static CoG height, equal
// to q); the sim's car frame has its origin on the ground.
static float body_lift(const sim::VehicleSetup& setup) {
    float q = setup.wheels[0].ride_height;
    return q > 0 ? q : 0.13f; // placeholder suspension: a typical road car
}

bool car_camera_points(ByteSpan car_file, render::Vec3& p0, render::Vec3& p1) {
    auto rdf = [](ByteSpan d, size_t o) {
        u32 v = rd32(d, o);
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    };
    try {
        CarFile f = CarFile::parse(car_file);
        ByteSpan g = f.info;
        if (g.size() < 0x70) return false;
        u32 n = rd32(g, 0x68), off = rd32(g, 0x6C);
        if (n < 2 || off + 0x80 > g.size()) return false;
        auto pt = [&](u32 i) { return render::Vec3{rdf(g, off + 0x40 * i), rdf(g, off + 0x40 * i + 4), rdf(g, off + 0x40 * i + 8)}; };
        p0 = pt(0), p1 = pt(1);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

render::Mat4 car_matrix(const sim::VehicleState& s) {
    const sim::Quat& q = s.orientation;
    return render::translate({s.position.x, s.position.y, s.position.z}) * render::rotate_quat(q.w, q.x, q.y, q.z);
}

int car_lod(float dist, int lods, bool full_detail) {
    if (full_detail) return 0;
    return std::max(0, std::min(dist < 30 ? 0 : dist < 80 ? 1 : 2, lods - 1));
}

CourseScene::CourseScene(render::GpuContext& ctx, ByteSpan data, const std::string& name, bool sheen)
    : frame_alpha_(sheen) {
    course::CourseModelOptions o;
    o.reflections = sheen; // road sheen (masked second pass)
    o.frame_alpha = frame_alpha_; // its mask: the frame alpha the passes leave (shadow masks included)
    o.backdrop = false;
    o.sky = false;
    o.course_line = false; // its own model, below
    std::vector<gtm1::ClutSet> scenery_cluts, backdrop_cluts, env_cluts;
    std::vector<gtm1::AnimatedNodes> scenery_nodes, backdrop_nodes;
    scene::Model scenery = course::build_course_model(data, o, name, &scenery_cluts, &scenery_nodes);
    log_bounds("scenery", scenery);
    for (const scene::Mesh& m : scenery.meshes)
        for (const scene::Primitive& p : m.primitives) stats_.triangles += p.index_count / 3;
    stats_.meshes = scenery.meshes.size();
    stats_.textures = scenery.textures.size();
    // (The course models keep their geometry: GT3 clips at the guard band, DrawItem::guard_band.)
    scenery_ = std::make_unique<render::GpuModel>(ctx, scenery, true);
    cluts_.add(scenery_.get(), std::move(scenery_cluts));
    set_moving(scenery_moving_, ctx, scenery_.get(), std::move(scenery_nodes));
    {
        // What a car's sides reflect: the scenery's mean colour (texel x pre-lit vertex colour).
        double acc[3] = {}, n = 0;
        for (const scene::Mesh& m : scenery.meshes)
            for (const scene::Primitive& p : m.primitives) {
                if (p.texture < 0 || p.index_count == 0) continue;
                const scene::Texture& t = scenery.textures[size_t(p.texture)];
                if (t.rgba.empty()) continue;
                const scene::Vertex& v = m.vertices[m.indices[p.first_index]];
                u32 tc = t.rgba[t.rgba.size() / 2];
                const double vs = (p.flags & scene::kVertexColor2x) ? 1.0 / 128.0 : 1.0 / 255.0;
                for (int k = 0; k < 3; ++k)
                    acc[k] += double(p.index_count) * (double((tc >> (8 * k)) & 0xFF) / 255.0) *
                              (double((v.color >> (8 * k)) & 0xFF) * vs);
                n += double(p.index_count);
            }
        if (n > 0)
            for (int k = 0; k < 3; ++k) ground_[k] = float(acc[k] / n);
    }
    occluder_.add(scenery);

    // The licence course line (course.md "Course line"): the game draws it opaque after the
    // whole scenery, blended packets included. Flagged blended (its vertex alpha is 1.0, so it
    // covers like an opaque pass) so that it follows the scenery's blended passes in model order.
    scene::Model line = course::course_line_model(data, name + "/course_line");
    if (!line.meshes.empty()) {
        for (scene::Mesh& m : line.meshes)
            for (scene::Primitive& p : m.primitives) p.flags |= scene::kAlphaBlend;
        line_ = std::make_unique<render::GpuModel>(ctx, line, true);
    }

    course::CourseModelOptions bo;
    bo.scenery = bo.course_line = bo.sky = false;
    bo.backdrop = true;
    scene::Model backdrop = course::build_course_model(data, bo, name + "/backdrop", &backdrop_cluts, &backdrop_nodes);
    occluder_.add(backdrop);
    if (!backdrop.meshes.empty()) {
        log_bounds("backdrop", backdrop);
        backdrop_ = std::make_unique<render::GpuModel>(ctx, backdrop, true);
        cluts_.add(backdrop_.get(), std::move(backdrop_cluts));
        set_moving(backdrop_moving_, ctx, backdrop_.get(), std::move(backdrop_nodes));
    }

    course::CourseModelOptions so;
    so.scenery = so.course_line = so.backdrop = false;
    so.sky = true;
    scene::Model sky = course::build_course_model(data, so, name + "/sky");
    if (!sky.meshes.empty()) {
        log_bounds("sky", sky);
        // Horizon colour: average of the sky textures' lower halves (where the horizon usually is).
        auto mean_rows = [&](float out[3], bool lower) {
            double acc[3] = {}, n = 0;
            for (const scene::Texture& t : sky.textures)
                for (u32 y = lower ? t.height / 2 : 0; y < (lower ? t.height : t.height / 2); ++y)
                    for (u32 x = 0; x < t.width; ++x) {
                        u32 c = t.rgba[size_t(y) * t.width + x];
                        for (int k = 0; k < 3; ++k) acc[k] += double((c >> (8 * k)) & 0xFF) / 255.0;
                        n += 1;
                    }
            if (n > 0)
                for (int k = 0; k < 3; ++k) out[k] = float(acc[k] / n);
        };
        mean_rows(horizon_, true);
        mean_rows(sky_rgb_, false);
        sky_ = std::make_unique<render::GpuModel>(ctx, sky);
    }
    // The rear-view mirror's sky: the low-resolution dome (slot 24).
    course::CourseModelOptions mo;
    mo.scenery = mo.course_line = mo.backdrop = mo.sky = false;
    mo.sky_low = true;
    scene::Model sky_low = course::build_course_model(data, mo, name + "/sky2");
    if (!sky_low.meshes.empty()) sky_low_ = std::make_unique<render::GpuModel>(ctx, sky_low);
    occluder_.build();

    // The reflection map's scene: slot 5 scenery and the slot 23 sky dome.
    course::CourseModelOptions eo;
    eo.scenery = eo.course_line = eo.backdrop = eo.sky = false;
    eo.scenery2 = true;
    scene::Model env_scenery = course::build_course_model(data, eo, name + "/env", &env_cluts);
    if (!env_scenery.meshes.empty()) {
        env_scenery_ = std::make_unique<render::GpuModel>(ctx, env_scenery);
        cluts_.add(env_scenery_.get(), std::move(env_cluts));
    }
    eo.scenery2 = false;
    eo.sky_object = true;
    scene::Model env_sky = course::build_course_model(data, eo, name + "/env_sky");
    if (!env_sky.meshes.empty()) env_sky_ = std::make_unique<render::GpuModel>(ctx, env_sky);

    // The visibility lists: GT3 draws only the nodes listed for the view point nearest the camera,
    // so parts of the scenery a hill hides are not drawn over the backdrop.
    const course::CourseFile file = course::CourseFile::parse(data);
    for (size_t v = 0; v < vis_.size(); ++v)
        if (int slot = course::scenery_list_slot(file, course::SceneryView(v)))
            vis_[v] = course::Visibility::parse(file.section(data, slot));
    env_vis_ = course::Visibility::parse(file.section(data, course::kSlotScenery2Vis));
}

void CourseScene::set_moving(Moving& m, render::GpuContext& ctx, const render::GpuModel* textures,
                             std::vector<gtm1::AnimatedNodes>&& nodes) {
    if (!textures || nodes.empty()) return;
    m.nodes = std::move(nodes);
    m.gpu = std::make_unique<render::GpuModel>(ctx, *textures);
    m.gpu->keep_geometry();
    size_t n = 0, parts = 0;
    for (const gtm1::AnimatedNodes& a : m.nodes) n += a.nodes().size(), parts += a.part_count();
    LOG_INFO("Course: %zu moving nodes, %zu parts", n, parts);
}

void CourseScene::restart_animations(float countdown) {
    cluts_.restart(countdown);
    for (Moving* m : {&scenery_moving_, &backdrop_moving_})
        for (gtm1::AnimatedNodes& a : m->nodes) a.reset();
    moving_changed_ = true;
}

void CourseScene::animate(float seconds) {
    cluts_.animate(seconds);
    for (Moving* m : {&scenery_moving_, &backdrop_moving_})
        for (gtm1::AnimatedNodes& a : m->nodes) a.update(seconds);
    moving_changed_ = moving_changed_ || seconds != 0;
}

void CourseScene::upload_animations() {
    cluts_.upload();
    if (!moving_changed_) return;
    moving_changed_ = false;
    for (Moving* m : {&scenery_moving_, &backdrop_moving_}) {
        if (!m->gpu) continue;
        std::vector<scene::Mesh> meshes;
        for (const gtm1::AnimatedNodes& a : m->nodes) {
            std::vector<scene::Mesh> part = a.meshes();
            for (scene::Mesh& mesh : part) meshes.push_back(std::move(mesh));
        }
        m->gpu->update_geometry(meshes);
    }
}

bool CourseScene::add_env_items(std::vector<render::DrawItem>& items, const render::Vec3& eye,
                                const render::Mat4& sky_rotation) const {
    if (!env_scenery_ && !env_sky_) return false;
    const render::ItemLights* lights = has_lights_ ? &lights_ : nullptr;
    if (env_sky_) {
        render::DrawItem it{env_sky_.get(), render::translate(eye) * sky_rotation, 0};
        it.background = true;
        it.lights = lights;
        items.push_back(it);
    }
    if (env_scenery_) {
        render::DrawItem it{env_scenery_.get(), {}, 0};
        it.lights = lights;
        it.sphere_map = true;
        it.nodes = env_vis_.visible(eye.x, eye.y, eye.z);
        items.push_back(it);
    }
    return true;
}

void CourseScene::add_items(std::vector<render::DrawItem>& items, const render::Vec3& eye,
                            course::SceneryView view, const SceneryFade* fade) const {
    const render::ItemLights* lights = has_lights_ ? &lights_ : nullptr;
    const bool mirror = view == course::SceneryView::Mirror || view == course::SceneryView::SplitMirror;
    // The mirror draws the low-resolution sky (slot 24) and no backdrop.
    const render::GpuModel* sky = mirror && sky_low_ ? sky_low_.get() : sky_.get();
    if (sky) {
        render::DrawItem it{sky, render::translate(eye), 0};
        it.background = true;
        it.lights = lights;
        items.push_back(it);
    }
    for (const render::GpuModel* m : {backdrop_.get(), backdrop_moving_.gpu.get()}) {
        if (!m || mirror) continue;
        render::DrawItem it{m, {}, 0};
        it.draw_distance = 0; // distant scenery: never distance-culled
        it.lights = lights;
        it.sphere_map = true; // chunk kind 2: the packet's own texture as a sphere map
        it.modelview_scale = kBackdropScale; // (the sheen's viewer, the guard band's planes)
        it.guard_band = true;
        // Drawn whole before the course, then GT3 resets the depth: the scenery always covers
        // it, even where the ground lies below its sea (Tahiti Maze's lagoon shores).
        it.in_order = true;
        it.in_order_join = m == backdrop_moving_.gpu.get();
        it.depth_reset = true;
        items.push_back(it);
    }
    const std::vector<u16>* nodes = fade ? fade->solid() : visible_nodes(eye, view);
    for (const render::GpuModel* m : {scenery_.get(), scenery_moving_.gpu.get()}) {
        if (!m) continue;
        render::DrawItem it{m, {}, 0};
        it.nodes = nodes;   // the list's nodes in its order, as the PS2 draws them
        it.in_order = true; // (in file order without a list)
        // The moving nodes with the scenery: their opaque passes before its blended ones.
        it.in_order_join = m == scenery_moving_.gpu.get();
        it.frame_alpha = frame_alpha_;
        it.lights = lights;
        it.sphere_map = true; // chunk kind 2: the packet's own texture as a sphere map
        it.guard_band = true;
        items.push_back(it);
    }
    // Nodes fading in or out: dithered, drawn with the scenery (joined to its in-order run).
    if (fade)
        for (const SceneryFade::Group& g : fade->faded()) {
            if (g.nodes.empty()) continue;
            for (const render::GpuModel* m : {scenery_.get(), scenery_moving_.gpu.get()}) {
                if (!m) continue;
                render::DrawItem it{m, {}, 0};
                it.nodes = &g.nodes;
                it.fade = g.weight;
                it.in_order = it.in_order_join = true;
                it.frame_alpha = frame_alpha_;
                it.lights = lights;
                it.sphere_map = true;
                it.guard_band = true;
                items.push_back(it);
            }
        }
    if (line_) {
        render::DrawItem it{line_.get(), {}, 0};
        it.in_order = true;           // after the scenery's in-order blended passes
        it.frame_alpha = frame_alpha_; // colour only: the frame alpha stays (kKeepAlpha)
        it.draw_distance = 0;          // one mesh along the whole section: never distance-culled
        it.guard_band = true;
        items.push_back(it);
    }
}

CarVisual::CarVisual(render::GpuContext& ctx, ByteSpan car_file, const std::string& model, const WheelSpec& ws,
                     int paint, ByteSpan wheel_file, const CarBodyMod& body_mod, ByteSpan showroom_car,
                     ByteSpan showroom_wheel, std::function<std::optional<Bytes>()> stand_in_rim)
    : ctx_(&ctx) {
    mounts_ = car_camera_mounts(car_file);
    CarModelOptions opt;
    opt.body_mod = body_mod;
    opt.wheel_override = wheel_file;
    opt.showroom_car = showroom_car;
    opt.showroom_wheel = showroom_wheel;
    opt.stand_in_rim = std::move(stand_in_rim);
    showroom_rims_ = CarFile::is_car(showroom_car); // (load_car_model's test for LOD 0's menu wheels)
    opt.lod = -1;
    opt.wheels = true;
    opt.effects = true; // shadow nodes
    opt.wheel_spec = ws;
    opt.paint = paint;
    opt.morphs = &morphs_;
    scene::Model car = load_car_model(car_file, opt, model);
    for (const scene::Mesh& m : car.meshes) lods_ = std::max(lods_, m.lod + 1);
    scene::Model body = scene::extract(car, [](const scene::Mesh& m) {
        return m.name.rfind("wheel_", 0) != 0 && m.name.rfind("node", 0) != 0;
    });
    for (int k = 0; k < 3; ++k) {
        std::string n = k == 0 ? "node1" : k == 1 ? "node6" : "node5";
        shadow_src_[size_t(k)] = scene::extract(car, [&](const scene::Mesh& m) { return m.name == n; });
        for (scene::Mesh& m : shadow_src_[size_t(k)].meshes)
            for (scene::Primitive& p : m.primitives) {
                p.material = {};
                if (k < 2) {
                    p.flags = scene::kAlphaBlend | scene::kUnlit | scene::kNoDepthWrite | scene::kDoubleSided, p.texture = -1;
                } else { // headlamp pool: textured, Cs As + Cd
                    p.flags = scene::kAdditive | scene::kUnlit | scene::kNoDepthWrite | scene::kDoubleSided;
                }
            }
    }
    hood_profile_ = make_hood_camera_profile(body, ws.wheelbase);
    body_ = std::make_unique<render::GpuModel>(ctx, body);
    occluder_.add(body);
    occluder_.build();
    if (!morphs_.empty()) {
        scene::Model mm; // the textures the morph parts use, then their meshes at the defaults
        mm.name = model + "_morph";
        std::vector<int> map(car.textures.size(), -1);
        for (const gtm1::MorphDraws& md : morphs_)
            for (int t : md.textures())
                if (size_t(t) < map.size() && map[size_t(t)] < 0) {
                    map[size_t(t)] = int(mm.textures.size());
                    mm.textures.push_back(car.textures[size_t(t)]);
                }
        size_t draws = 0;
        for (gtm1::MorphDraws& md : morphs_) {
            md.map_textures(map);
            draws += md.size();
            for (scene::Mesh& m : md.meshes(gtm1::kCarCallbackDefaults)) mm.meshes.push_back(std::move(m));
        }
        morph_base_ = std::make_unique<render::GpuModel>(ctx, mm);
        LOG_INFO("Car %s: %zu morph draws (%zu LODs)", model.c_str(), draws, morphs_.size());
    }
    try {
        lamps_ = CarLampSet::parse(CarFile::parse(car_file).info);
        LOG_INFO("Car %s: lamps %zu head, %zu tail, %zu brake", model.c_str(), lamps_.head.size(), lamps_.tail.size(), lamps_.brake.size());
    } catch (const std::exception&) {
    }
    {
        // The brake-lamp variant: the node programs' brake switch (selector id 0) takes its
        // "pressed" branch (lit lens chunks with lamp materials).
        CarModelOptions bo = opt;
        bo.brake = true;
        bo.wheels = false;
        bo.effects = false;
        std::vector<gtm1::MorphDraws> lit_morphs; // (left out as in body_; drawn from morphs_)
        bo.morphs = &lit_morphs;
        scene::Model lit = load_car_model(car_file, bo, model);
        scene::Model lit_body = scene::extract(lit, [](const scene::Mesh& m) { return m.name.rfind("node", 0) != 0; });
        if (!lit_body.meshes.empty()) body_brake_ = std::make_unique<render::GpuModel>(ctx, lit_body);
    }
    // The wheels' effects (race/wheel_fx.h): the tyre's GTTR fields, the brake records, and the
    // rim's images in the wheel file's texture table (GTM1 +0x34, table 0: entry 1 the rim,
    // entry 2 the spinning rim GT3 shows on the other cars' fast wheels).
    std::optional<scene::Texture> rim_spin;
    try {
        CarFile f = CarFile::parse(car_file);
        if (f.tire.size() >= 0x1C) {
            tyre_repeats_ = rd32(f.tire, 0x10), tyre_flags_ = rd32(f.tire, 0x14);
            u32 sh = rd32(f.tire, 0x18);
            std::memcpy(&tyre_shininess_, &sh, 4);
        }
        brake_records_ = parse_brake_records(f.info);
        gtm1::Gtm1 g = gtm1::Gtm1::parse(WheelFile::parse(wheel_file.empty() ? f.wheel : wheel_file).gtm1);
        auto set_image = [&](size_t entry) -> std::optional<scene::Texture> {
            if (g.texset_tables.empty() || g.texset_tables[0].size() <= entry) return std::nullopt;
            const u32 k = g.texset_tables[0][entry];
            if (k >= g.texsets.size() || g.texsets[k] >= g.data.size()) return std::nullopt;
            std::vector<scene::Texture> t;
            gtm1::decode_texture_set(g.data.subspan(g.texsets[k]), t, "rim_");
            if (t.empty()) return std::nullopt;
            return t[0];
        };
        if (std::optional<scene::Texture> rim = set_image(1)) rim_width_ = rim->width;
        rim_spin = set_image(2);
    } catch (const std::exception& e) {
        LOG_WARN("Car %s: wheel effects: %s", model.c_str(), e.what());
    }
    for (int i = 0; i < 4; ++i) {
        std::string n = kWheelNames[i];
        int axle = i >> 1;
        render::Vec3 c{(i & 1 ? 0.5f : -0.5f) * ws.track[axle], ws.radius[axle], (axle ? 0.5f : -0.5f) * ws.wheelbase};
        wheel_center_[size_t(i)] = c;
        wheel_width_[size_t(axle)] = ws.width[axle];
        wheel_bound_[size_t(axle)] =
            std::sqrt(ws.radius[axle] * ws.radius[axle] + 0.25f * ws.width[axle] * ws.width[axle]);
        auto hub_model = [&](const std::string& name) {
            scene::Model w = scene::extract(car, [&](const scene::Mesh& m) { return m.name == name; });
            for (scene::Mesh& m : w.meshes)
                for (scene::Vertex& v : m.vertices) v.pos[0] -= c.x, v.pos[1] -= c.y, v.pos[2] -= c.z;
            return w;
        };
        scene::Model rim = hub_model(n);
        if (!rim.meshes.empty()) {
            if (rim_spin) {
                rim_spin_texture_ = int(rim.textures.size()); // (the same index in every wheel's model)
                rim.textures.push_back(*rim_spin);
            }
            rims_[size_t(i)] = std::make_unique<render::GpuModel>(ctx, rim);
        }
        scene::Model tyre = hub_model(n + "_tyre");
        if (!tyre.meshes.empty()) tyres_[size_t(i)] = std::make_unique<render::GpuModel>(ctx, tyre);
        scene::Model band = hub_model(n + "_band");
        if (!band.meshes.empty()) bands_[size_t(i)] = std::make_unique<render::GpuModel>(ctx, band);
    }
}

void CarVisual::set_brake_parts(const BrakeParts& bin) {
    for (int i = 0; i < 4; ++i) {
        const BrakeRecord& rec = brake_records_[size_t(i >> 1)];
        const bool right = (i & 1) != 0;
        brakes_[size_t(i)].reset(), glows_[size_t(i)].reset();
        scene::Model parts = build_brake_parts(bin, rec, right, tyre_shininess_);
        if (!parts.meshes.empty()) brakes_[size_t(i)] = std::make_unique<render::GpuModel>(*ctx_, parts);
        scene::Model glow = build_brake_glow(bin, rec, right);
        if (!glow.meshes.empty()) glows_[size_t(i)] = std::make_unique<render::GpuModel>(*ctx_, glow);
    }
}

std::array<float, 5> car_callback_values(const sim::VehicleState& s, const sim::VehicleSetup& setup) {
    std::array<float, 5> v;
    for (size_t i = 0; i < 4; ++i) v[i] = 0.5f + 0.5f * s.wheels[i].flap;
    v[4] = setup.steer_lock > 0 ? 0.5f + 0.5f * s.steer / setup.steer_lock : 0.5f;
    return v;
}

render::Mat4 CarVisual::body_matrix(const sim::VehicleState& s, const sim::VehicleSetup& setup) const {
    return car_matrix(s) * render::translate({0, body_lift(setup), 0});
}

void CarVisual::set_shadow(const CarShadowParams& p) {
    shadow_params_ = p;
    shadow_set_ = true;
    for (int k = 0; k < 3; ++k) {
        shadow_[size_t(k)].reset();
        if (!p.enabled || shadow_src_[size_t(k)].meshes.empty()) continue;
        scene::Model m = shadow_src_[size_t(k)];
        if (k == 2) { // headlamp pool: vertex colour kept, alpha x Env0 +0x58
            if (p.headlamp <= 0) continue;
            for (scene::Mesh& mesh : m.meshes)
                for (scene::Vertex& v : mesh.vertices) {
                    float a = std::min(1.0f, float(v.color >> 24) / 255.0f * p.headlamp);
                    v.color = (v.color & 0x00FFFFFFu) | u32(a * 255 + 0.5f) << 24;
                }
            shadow_[2] = std::make_unique<render::GpuModel>(*ctx_, m);
            continue;
        }
        float f = p.alpha_scale; // node 1's road-shade factor F is applied per frame (CarLook::shadow)
        for (scene::Mesh& mesh : m.meshes)
            for (scene::Vertex& v : mesh.vertices) {
                float a = std::min(1.0f, float(v.color >> 24) / 255.0f * f);
                u32 c = u32(std::clamp(p.rgb[0], 0.0f, 1.0f) * 255) | u32(std::clamp(p.rgb[1], 0.0f, 1.0f) * 255) << 8 |
                        u32(std::clamp(p.rgb[2], 0.0f, 1.0f) * 255) << 16 | u32(a * 255 + 0.5f) << 24;
                v.color = c;
            }
        shadow_[size_t(k)] = std::make_unique<render::GpuModel>(*ctx_, m);
    }
}

void CarVisual::add_items(std::vector<render::DrawItem>& items, const sim::VehicleState& s,
                          const sim::VehicleSetup& setup, const render::Vec3& eye, const CarLook& look) const {
    float dist = render::length(render::Vec3{s.position.x, s.position.y, s.position.z} - eye);
    const int lod = car_lod(dist, lods_, full_detail_);
    if (shadow_params_.enabled && (shadow_[0] || shadow_[1])) {
        // Ground frame: the plane through the four wheel contacts, the car's heading.
        sim::Vec3 c{0, 0, 0};
        for (const sim::WheelState& w : s.wheels) c = c + w.contact * 0.25;
        sim::Vec3 fwd = (s.wheels[0].contact + s.wheels[1].contact) - (s.wheels[2].contact + s.wheels[3].contact);
        sim::Vec3 rgt = (s.wheels[1].contact + s.wheels[3].contact) - (s.wheels[0].contact + s.wheels[2].contact);
        render::Vec3 f = render::normalize(render::Vec3{-float(fwd.x), -float(fwd.y), -float(fwd.z)}); // car +Z = back
        render::Vec3 rr = render::normalize(render::Vec3{float(rgt.x), float(rgt.y), float(rgt.z)});
        render::Vec3 up = render::normalize(render::cross(f, rr));
        rr = render::normalize(render::cross(up, f));
        if (render::length(f) > 0.5f && up.y > 0.3f) {
            render::Mat4 g; // columns: right (x), up (y), back (z), origin 2 cm above the plane
            render::Vec3 o = render::Vec3{float(c.x), float(c.y), float(c.z)} + up * 0.02f;
            for (int i = 0; i < 3; ++i) {
                g.at(i, 0) = (&rr.x)[i], g.at(i, 1) = (&up.x)[i], g.at(i, 2) = (&f.x)[i], g.at(i, 3) = (&o.x)[i];
            }
            // Light shear in the ground frame: x' = x + dx y, y' = 0, z' = z + dz y.
            auto shear = [&](const render::Vec3& d) {
                render::Mat4 sh;
                sh.at(0, 1) = render::dot(d, rr);
                sh.at(1, 1) = 1e-3f; // flattened (kept invertible for the normal matrix)
                sh.at(2, 1) = render::dot(d, f);
                return sh;
            };
            render::Mat4 sh = shear(shadow_params_.shear);
            render::Mat4 flat;
            flat.at(1, 1) = 1e-3f;
            // Depth bias (GT3 nudges the shadow's near/far planes): pull it 2 % towards the eye
            // along the view rays, which leaves its screen position unchanged.
            render::Mat4 bias = render::translate(eye) * render::scale({0.98f, 0.98f, 0.98f}) *
                                render::translate(render::Vec3{0, 0, 0} - eye);
            // GT3 draws them before the body (node 1, then 5, then 6), so the body and wheels
            // cover them: each is drawn whole at its place (in_order), ahead of the body items
            // (presentation §I7).
            auto shadow = [&](const render::GpuModel* m, const render::Mat4& t, float alpha) {
                render::DrawItem it{m, t, lod};
                it.alpha = alpha;
                it.in_order = true;
                it.far_whole = true;
                items.push_back(it);
            };
            for (int k = 0; shadow_[0] && k < look.lamp_shadows && k < 4; ++k)
                if (look.lamp_alpha[k] > 0.01f)
                    shadow(shadow_[0].get(), bias * g * shear(look.lamp_shear[k]), look.lamp_alpha[k]);
            if (shadow_[0] && look.lamp_shadows == 0 && look.shadow * shadow_params_.brightness > 0.01f)
                shadow(shadow_[0].get(), bias * g * sh, look.shadow * shadow_params_.brightness); // > 1: blend clamps
            if (shadow_[2]) shadow(shadow_[2].get(), bias * g * flat, 1);
            if (shadow_[1]) shadow(shadow_[1].get(), bias * g * flat, 1);
        }
    }
    add_body_items(items, s, setup, look, {}, lod, true);
}

bool CarVisual::road_mirror(const sim::VehicleState& s, render::Mat4& out) {
    sim::Vec3 c{0, 0, 0};
    for (const sim::WheelState& w : s.wheels) c = c + w.contact * 0.25;
    sim::Vec3 fwd = (s.wheels[0].contact + s.wheels[1].contact) - (s.wheels[2].contact + s.wheels[3].contact);
    sim::Vec3 rgt = (s.wheels[1].contact + s.wheels[3].contact) - (s.wheels[0].contact + s.wheels[2].contact);
    // Up = back x right (as the shadows' ground frame).
    render::Vec3 n = render::normalize(render::cross(render::Vec3{-float(fwd.x), -float(fwd.y), -float(fwd.z)},
                                                     render::Vec3{float(rgt.x), float(rgt.y), float(rgt.z)}));
    if (!(n.y > 0.3f)) return false;
    render::Vec3 p{float(c.x), float(c.y), float(c.z)};
    float pn = render::dot(p, n);
    out = render::Mat4{};
    const float N[3] = {n.x, n.y, n.z};
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) out.at(r, k) = (r == k ? 1.0f : 0.0f) - 2.0f * N[r] * N[k];
        out.at(r, 3) = 2.0f * pn * N[r];
    }
    return true;
}

bool CarVisual::add_mirror_items(std::vector<render::DrawItem>& items, const sim::VehicleState& s,
                                 const sim::VehicleSetup& setup, const render::Vec3& eye, const CarLook& look) const {
    render::Mat4 m;
    if (!road_mirror(s, m)) return false;
    float dist = render::length(render::Vec3{s.position.x, s.position.y, s.position.z} - eye);
    add_body_items(items, s, setup, look, m, car_lod(dist, lods_, full_detail_), false);
    return true;
}

CarVisual::WheelLook CarVisual::wheel_look(int i, const render::Mat4& at, const render::Mat4& hub,
                                           const sim::WheelState& w, const CarLook& look) const {
    WheelLook out;
    if (!look.wheels) return out;
    const WheelView& v = *look.wheels;
    const size_t axle = size_t(i >> 1);
    const bool right = (i & 1) != 0;
    // The wheel's level from its size on screen; its outer face in view when the eye is beyond the
    // face's plane in the wheel's frame (steered and cambered, before the spin).
    const render::Vec3 centre = at.transform_point({0, 0, 0});
    const int detail = wheel_detail(v.proj.at(0, 0), wheel_bound_[axle], -v.view.transform_point(centre).z);
    const render::Vec3 eye = render::inverse(hub).transform_point(v.eye);
    const float half_width = 0.5f * wheel_width_[axle];
    const bool outer = right ? eye.x > half_width : eye.x < -half_width;
    out.parts = detail < 2 && outer;
    if (detail < 2) out.glow = brake_glow_fix(w.brake_heat, v.env.glow_gain);
    // The blur's turn, from the wheel centre's movement on screen since the last frame.
    float turn = 0;
    if (v.blur && look.wheel_track) {
        const render::Mat4 vp = v.proj * v.view;
        const render::Vec3 c = vp.transform_point(centre);
        const float cw = vp.at(3, 0) * centre.x + vp.at(3, 1) * centre.y + vp.at(3, 2) * centre.z + vp.at(3, 3);
        std::array<float, 2>& last = look.wheel_track->ndc[size_t(i)];
        const std::array<float, 2> ndc = std::fabs(cw) > 1e-6f ? std::array<float, 2>{c.x / cw, c.y / cw} : last;
        const float moved = std::hypot(ndc[0] - last[0], ndc[1] - last[1]);
        last = ndc;
        turn = wheel_blur_turn(w.spin_step, moved, v.env);
    }
    if (turn == 0) return out;
    if (look.focus && detail < 2 && tread_blur_wanted(tyre_flags_, outer)) {
        out.tread = true;
        out.tread_shifts = tread_blur_shifts(turn, tyre_repeats_, right);
    }
    if (detail < 3) out.turn = turn;
    if (outer && detail < 3) {
        if (look.focus) out.rim = 1, out.rim_angles = rim_blur_angles(turn, right);
        else if (rim_spin_texture_ >= 0 && rim_spinning_image(turn)) out.rim = 2;
    }
    return out;
}

void CarVisual::add_body_items(std::vector<render::DrawItem>& items, const sim::VehicleState& s,
                               const sim::VehicleSetup& setup, const CarLook& look, const render::Mat4& pre,
                               int lod, bool wheel_fx) const {
    render::Mat4 world = pre * car_matrix(s);
    auto styled = [&](render::DrawItem it) {
        it.far_whole = true; // (GT3's cars pass: its node box test drops a model reaching past the far plane)
        for (int k = 0; k < 3; ++k) it.light_scale[k] = look.light_scale[k];
        it.reflection_scale = look.reflection;
        it.lamp = look.lamp;
        it.lights = look.lights;
        return it;
    };
    const render::GpuModel* body = look.brake_now && body_brake_ ? body_brake_.get() : body_.get();
    items.push_back(styled({body, world * render::translate({0, body_lift(setup), 0}), lod}));
    if (morph_base_) {
        Morph& mo = morph_[size_t(std::clamp(look.instance, 0, int(morph_.size()) - 1))];
        const std::array<float, 5> v = car_callback_values(s, setup);
        if (!mo.gpu) {
            mo.gpu = std::make_unique<render::GpuModel>(*ctx_, *morph_base_);
            mo.values.fill(-1);
        }
        if (v != mo.values) {
            std::vector<scene::Mesh> meshes;
            for (const gtm1::MorphDraws& md : morphs_)
                for (scene::Mesh& m : md.meshes(v)) meshes.push_back(std::move(m));
            mo.gpu->update_geometry(meshes);
            mo.values = v;
        }
        items.push_back(styled({mo.gpu.get(), world * render::translate({0, body_lift(setup), 0}), lod}));
    }
    for (int i = 0; i < 4; ++i) {
        const size_t k = size_t(i);
        if (!tyres_[k] && !rims_[k]) continue;
        const sim::WheelState& w = s.wheels[k];
        const sim::VehicleSetup::Wheel& sw = setup.wheels[k];
        // Hub in the car frame: the sim's rest hub (y = tyre radius over the ground-level origin)
        // plus the suspension travel from the static pose (+ = compressed: up into the arch;
        // airborne = max droop). The body mesh is lifted by the ride height (see body_lift).
        render::Vec3 c = wheel_center_[k];
        c.y = sw.rest_center.y + (w.suspension - sw.static_suspension);
        // GT3 turns a wheel by its steering (and half a turn for a right wheel: ours are built
        // turned), then by its camber about the z axis of that frame (so the other way round in
        // ours for a right wheel), then by its spin.
        const float camber = (i & 1) ? -w.camber : w.camber;
        const render::Mat4 at = world * render::translate(c);
        const render::Mat4 hub = at * render::rotate_y(-w.steer_angle) * render::rotate_z(camber);
        const render::Mat4 spun = hub * render::rotate_x(-w.spin_angle);
        const WheelLook wl = wheel_fx ? wheel_look(i, at, hub, w, look) : WheelLook{};
        // GT3's order: the tyre, the rotor and caliper, the tyre's black faces with the rotor's
        // glow, then the rim. The parts are drawn whole before the tyre's opaque faces (in_order),
        // which then cover the caliper's soft shadow where it wrote no depth, as in GT3.
        if (wl.parts && brakes_[k]) {
            render::DrawItem it = styled({brakes_[k].get(), hub, 0});
            it.in_order = true;
            items.push_back(it);
        }
        if (tyres_[k]) {
            render::DrawItem it = styled({tyres_[k].get(), spun, lod});
            if (wl.tread) {
                it.wheel_blur = 1;
                for (size_t j = 0; j < 3; ++j) it.wheel_steps[j] = wl.tread_shifts[j];
            }
            items.push_back(it);
        }
        if (wl.glow > 0 && glows_[k]) {
            render::DrawItem it = styled({glows_[k].get(), spun, 0});
            it.tint = wl.glow; // GS ALPHA 0x68: Cs x FIX + Cd (the glow texture's alpha is 0x80)
            it.no_fog = true;
            items.push_back(it);
        }
        // Showroom wheels at LOD 0: the modelled rim, blurred by copies of it turned back over the
        // blur's turn (GT3's copies of a flat rim's image as geometry: DrawItem::spin_turn).
        const bool modelled = lod == 0 && showroom_rims_;
        if (const render::GpuModel* rim = rims_[k].get()) {
            const int rim_fx = modelled ? 0 : wl.rim;
            render::DrawItem it = styled({rim, spun, lod});
            if (modelled) it.spin_turn = wl.turn;
            if (rim_fx == 1) {
                it.wheel_blur = 2;
                it.wheel_blur_lod = rim_blur_lod(rim_width_);
                // GT3 tests the blurred image's own alpha (the spokes' coverage x 0x80) at the rim
                // programs' AREF 0x20; our rims' cut-out copies (alpha 0 or 1) average to the coverage.
                it.wheel_blur_alpha_ref = 0x20 / 128.0f;
                it.wheel_steps = wl.rim_angles;
            } else if (rim_fx == 2) {
                it.texture = rim->textures()[size_t(rim_spin_texture_)].texture;
            }
            items.push_back(it);
        }
        // The arch shade: turned by the camber, not by the steering (GT3).
        if (bands_[k]) items.push_back(styled({bands_[k].get(), at * render::rotate_z(camber), lod}));
    }
}

} // namespace gt3r::race
