// render_drive(): the 3D views (1P, 2P split, original 4:3 frame), the draw items
// (course, cars, ghost, smoke, pylons), reflections (env cube, wet road), mirrors, sun and
// lamp glows, the HUD data, and the results / licence / time-trial overlays over the race.
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"

namespace gt3r::race_screen {

// The car reflection map (GT3 renders the course around the focus car into a 256x256
// sphere map every frame; we render a cube): course only, 0.8 m above the player's car,
// scenery within 200 m. [video] car_reflections = off falls back to the sky/scenery gradient.
bool RaceScreen::update_env_cube() {
    if (!env_cube_on_ || !view_ || !course_scene_) return false;
    const sim::Vec3& p = render_state_.position;
    render::Vec3 pos{p.x, p.y + 0.8f, p.z};
    env_items_.clear();
    const SunParams& e = sun_.params();
    constexpr float kDeg = 3.14159265f / 180.0f;
    // GT3 turns the dome by RotX(Env0 +0x48) RotY(+0x28), right-handed and post-multiplied: the
    // yaw first, then the pitch (presentation §I5; its painted glow then lies towards the sun).
    render::Mat4 sky_rot = render::rotate_x(e.sky_pitch * kDeg) * render::rotate_y(e.sky_yaw * kDeg);
    // GT3's map: the dark reflection-only sky dome and slot 5 scenery on black, unfogged and
    // unlit (fallback: the drawn course).
    bool gt3_scene = course_scene_->add_env_items(env_items_, pos, sky_rot);
    env_gt3_scene_ = gt3_scene;
    if (!gt3_scene) course_scene_->add_items(env_items_, pos);
    // Point-light courses: the six strongest lamps' discs over the map (GT3 draws them on the
    // sphere map before the car; we build them in its space as our lookup reads it: axis
    // towards the viewer, tilted 15 degrees up).
    if (gt3_scene && point_lights_.active() && e.has_env && app_ && app_->gpu()) {
        std::vector<ReflectionDisc> discs =
            point_lights_.reflection_discs(e, {p.x, p.y, p.z}, car_lamp_index(render_state_, 0));
        render::Mat4 v = camera_.view();
        render::Vec3 x{v.at(0, 0), v.at(0, 1), v.at(0, 2)}, y{v.at(1, 0), v.at(1, 1), v.at(1, 2)};
        render::Vec3 z = render::cross(x, y);
        const render::Vec3 basis[3] = {x, y * 0.966f - z * 0.259f, y * 0.259f + z * 0.966f};
        reflection_discs_.update(*app_->gpu(), discs, pos, basis);
        reflection_discs_.add_item(env_items_);
    }
    render::DrawOptions o = draw_options();
    o.env_cube = nullptr;
    o.draw_distance = 200;
    o.frustum_cull = true;
    o.fixed_width = o.fixed_height = 0;
    if (gt3_scene) {
        o.fog = false;
        o.quantize_rgb5 = true;
        for (int i = 0; i < 3; ++i) o.clear[i] = 0;
    }
    return view_->render_env_cube(pos, env_items_, o, 128) != nullptr;
}

// Rear-view mirror (onboard cameras; GT3: frame rect (200, 20) 240x64 with a 1 px black
// outline, the camera's view with z flipped (looking back, mirrored), 1.3x zoom, far 120 m).
// `p2` / `half`: player 2's camera / a 4:3 2P half (the original: 240 x 55 at (200, 16) of the
// 640 x 224 half, measured on the captures). On a screen wider than 4:3 the frame keeps its
// shape and stays centred.
void RaceScreen::draw_mirror(App& app, Ui& ui, const SDL_FRect& frame, const render::DrawOptions& main, bool p2,
                             bool half) {
    const CameraRig& cam = p2 ? camera2_ : camera_;
    float fx = 200, fy = 20, fw = 240, fh = 64;
    SDL_FRect mr;
    if (half) { // as wide as on the 16:9 screen at most, like the half's HUD (Hud::draw_split)
        float k = std::min(frame.h / 180.0f, frame.w / 640.0f), ky = frame.h / 224.0f;
        mr = {frame.x + frame.w / 2 + (fx - 320) * k, frame.y + 16 * ky, fw * k, 55 * ky};
    } else if (original_res_) {
        float k = frame.w / 640.0f, ky = frame.h / 448.0f;
        mr = {frame.x + fx * k, frame.y + fy * ky, fw * k, fh * ky};
    } else {
        float k = frame.h / 360.0f * 0.75f, ky = frame.h / 448.0f;
        mr = {frame.x + frame.w / 2 + (fx - 320) * k, frame.y + fy * ky, fw * k, fh * ky};
    }
    std::unique_ptr<render::View3D>& mv = p2 ? mirror_view2_ : mirror_view_;
    if (!mv) mv = std::make_unique<render::View3D>(*app.gpu());
    render::Vec3 eye = cam.eye();
    float t = std::tan(cam.fovy() * 0.5f) / 1.3f;
    TyreSmoke::View sv = smoke_view(cam, mr.w / mr.h, 2.0f * std::atan(t));
    sv.forward = sv.forward * -1.0f; // the mirror looks back
    sv.far_m = 120;
    collect_items(eye, cam_right(cam), p2, !p2, sv, true); // the own car would fill the view
    render::DrawOptions o = main;
    o.mirrored = true;
    o.draw_distance = 120;
    // The mirror's own fog (Env0 +0xA90 RGB, +0xAA0 distance A, +0xAA4 minimum visibility b;
    // A = 0: a constant visibility b), per vertex like the main view's.
    const SunParams& env = sun_.params();
    if (env.has_env && !no_course_fog_) {
        o.fog = env.mirror_fog_b < 0.999f;
        o.fog_gt3 = true;
        o.fog_start = env.mirror_fog_a;
        o.fog_max = std::clamp(1.0f - env.mirror_fog_b, 0.0f, 1.0f);
        for (int i = 0; i < 3; ++i) o.fog_color[i] = std::clamp(env.mirror_fog_rgb[size_t(i)], 0.0f, 1.0f);
    }
    if (original_res_) o.fixed_width = 240, o.fixed_height = 64;
    if (half) o.fixed_width = o.fixed_height = 0;
    render::Mat4 proj = render::perspective(2.0f * std::atan(t), mr.w / mr.h, 0.1f, 120.0f);
    render::Mat4 view = render::scale({1, 1, -1}) * cam.view();
    // An opaque backing: the course passes keep the target's alpha, which is transparent on a
    // VR HUD panel.
    SDL_SetRenderDrawColor(ui.renderer(), 0, 0, 0, 255);
    SDL_RenderFillRect(ui.renderer(), &mr);
    mv->draw(ui.renderer(), mr, view, proj, eye, items_, o);
    SDL_SetRenderDrawColor(ui.renderer(), 0, 0, 0, 255);
    SDL_RenderRect(ui.renderer(), &mr);
}

// Car shadow parameters from the course (Env0): colour, alpha, the light shadow's direction.
CarShadowParams RaceScreen::shadow_params() const {
    CarShadowParams p;
    const SunParams& e = sun_.params();
    if (!e.has_env || !e.has_shadow) return p;
    p.enabled = true;
    for (int i = 0; i < 3; ++i) p.rgb[i] = e.shadow_rgb[size_t(i)];
    p.alpha_scale = e.shadow_alpha;
    if (point_lights_.active()) { // per-lamp shadows: colour of lamp type 0, alpha set per lamp
        for (int i = 0; i < 3; ++i) p.rgb[i] = e.lamps[0].shadow_rgb[size_t(i)];
        p.alpha_scale = 1;
    }
    p.brightness = 1; // F per frame from the road shade (car_look)
    p.headlamp = e.headlamp_pool;
    float el = -e.shadow_elev * 3.14159265f / 180.0f, az = -e.shadow_az * 3.14159265f / 180.0f;
    p.shear = render::Vec3{std::sin(az), 0, std::cos(az)} * (e.shadow_len * std::cos(el));
    return p;
}

// A car picture for the 2P results panel (GT3: the car icon maker renders the car model):
// the car's model from the front left on the panel's blue.
void RaceScreen::draw_car_icon(App& app, int car, const SDL_FRect& rc, size_t slot, bool on_bar) {
    if (rc.w <= 1 || rc.h <= 1 || !app.gpu()) return;
    const CarVisual* v = car == -1 ? car_visual_.get()
                         : car == -2 ? (car2_visual_ ? car2_visual_.get() : car_visual_.get())
                         : car >= 0 && size_t(car) < opp_visuals_.size() ? opp_visuals_[size_t(car)].get()
                                                                          : nullptr;
    const sim::VehicleSetup* su = car == -1 ? &vehicle_->setup()
                                  : car == -2 && field_ ? &field_->second_setup()
                                  : car >= 0 && exact_field() && car < exact_field()->opponents() ? &exact_field()->opponent_setup(car)
                                                                                                  : nullptr;
    if (!v || !su) return;
    while (icon_views_.size() <= slot) icon_views_.push_back(std::make_unique<render::View3D>(*app.gpu()));
    sim::VehicleState st;
    st.orientation = {1, 0, 0, 0};
    for (size_t i = 0; i < 4; ++i) {
        const auto& w = su->wheels[i];
        st.wheels[i].contact = {w.rest_center.x, 0, w.rest_center.z};
        st.wheels[i].suspension = w.static_suspension;
        st.wheels[i].grounded = true;
    }
    std::vector<render::DrawItem> items;
    render::Vec3 eye{-3.6f, 1.4f, -4.4f};
    v->add_items(items, st, *su, eye);
    render::DrawOptions o;
    o.gt3_lighting = true;
    o.light_dir = {-0.4f, 0.8f, -0.5f};
    o.reflection_fresnel = 0.8f; // dark studio-like surroundings: no grey haze on the paint
    for (int i = 0; i < 3; ++i) o.env_sky[i] = o.env_horizon[i] = o.env_ground[i] = 0.08f;
    o.clear[0] = 0.20f, o.clear[1] = 0.20f, o.clear[2] = 0.42f, o.clear[3] = 1.0f;
    if (on_bar) o.clear[3] = 0.0f; // 1P results: the car straight on the bar (the capture)
    render::Mat4 view = render::look_at(eye, {0, 0.55f, 0}, {0, 1, 0});
    render::Mat4 proj = render::perspective(0.42f, rc.w / rc.h, 0.1f, 50.0f);
    icon_views_[slot]->draw(app.renderer(), rc, view, proj, eye, items, o);
}

// The lamp index a car's lights and reflection discs are picked around (point-light courses):
// its front-left wheel's road attribute A as a signed value (-1 on the start-line strip), kept
// while the wheel finds no road (docs/formats/course.md "Point-light courses").
int RaceScreen::car_lamp_index(const sim::VehicleState& s, size_t slot) const {
    if (lamp_index_.size() <= slot) lamp_index_.resize(slot + 1, 0);
    const track::Track* rt = track_ ? track_->runtime() : nullptr;
    const sim::WheelState& w = s.wheels[0];
    RoadShade fl = rt ? road_shade(rt->road(), w.contact.x, w.contact.z, w.contact.y + 1.0f) : RoadShade{};
    if (fl.hit) lamp_index_[slot] = lamp_index(fl.raw_a);
    return lamp_index_[slot];
}

// A car's look this frame: GT3 averages the road shade (vertex attributes A, B) under the four
// wheels and scales the Env0 car light colours and the light shadow by it; the reflection is
// weakened by the car's dirt (the player's garage car; others are clean).
CarLook RaceScreen::car_look(const sim::VehicleState& s, bool player, size_t slot) const {
    CarLook look;
    const SunParams& e = sun_.params();
    const track::Track* rt = track_ ? track_->runtime() : nullptr;
    if (rt && e.has_env && point_lights_.active()) {
        render::Vec3 p{s.position.x, s.position.y, s.position.z};
        std::vector<SelectedLamp> sel = point_lights_.select(e, p, car_lamp_index(s, slot));
        render::ItemLights& il = item_lights_.emplace_back();
        for (int i = 0; i < 3; ++i) il.ambient[i] = e.global_ambient[size_t(i)];
        for (const SelectedLamp& l : sel) {
            const SunParams::LampType& t = e.lamps[size_t(l.type)];
            int k = il.count++;
            for (int i = 0; i < 3; ++i) {
                il.ambient[i] += t.ambient[size_t(i)] * l.w;
                il.dir[k][i] = (&l.dir.x)[i];
                il.diffuse[k][i] = t.diffuse[size_t(i)] * l.w;
                il.specular[k][i] = t.specular[size_t(i)] * l.w;
            }
            look.lamp_shear[k] = render::Vec3{-l.dir.x, 0, -l.dir.z} * e.shadow_len;
            look.lamp_alpha[k] = t.shadow_alpha * l.w;
        }
        look.lamp_shadows = il.count;
        if (il.count > 0) look.lights = &il;
    } else if (rt && e.has_env) {
        float a = 0, b = 0;
        int n = 0;
        for (const sim::WheelState& w : s.wheels) {
            RoadShade rs = road_shade(rt->road(), w.contact.x, w.contact.z, w.contact.y + 1.0f);
            if (!rs.hit) continue;
            a += rs.a, b += rs.b, ++n;
        }
        if (n > 0) {
            a /= float(n), b /= float(n);
            std::array<float, 3> ls = e.light_scale(a, b);
            for (int i = 0; i < 3; ++i) look.light_scale[i] = ls[size_t(i)];
            look.shadow = e.shadow_factor(a, b);
            if (player && debug_) {
                static int log_tick = 0;
                if (++log_tick % 120 == 1) LOG_INFO("road shade a %.2f b %.2f -> light %.2f %.2f %.2f F %.2f", a, b, ls[0], ls[1], ls[2], look.shadow);
            }
        } else {
            look.shadow = e.shadow_f_lit;
        }
    }
    if (player && dirt_dims_) look.reflection = 1.0f - std::clamp(float(car_dirt_), 0.0f, 0.8f);
    return look;
}

// The view a smoke pass is culled for: the camera's direction and projection x scale.
TyreSmoke::View RaceScreen::smoke_view(const CameraRig& c, float aspect, float fovy) {
    sim::Vec3 f = cam_forward(c);
    TyreSmoke::View v;
    v.forward = {f.x, f.y, f.z};
    v.proj_x = 1.0f / (std::tan(fovy * 0.5f) * std::max(aspect, 1e-3f));
    v.near_m = c.near_plane, v.far_m = c.far_plane;
    return v;
}

// Draw items seen from `eye`: course, every car, the ghost.
void RaceScreen::collect_items(const render::Vec3& eye, const sim::Vec3& right, bool player,
                               bool second, const TyreSmoke::View& smoke_v, bool mirror) {
    item_lights_.clear();
    if (shadows_on_) {
        auto apply = [this](CarVisual* v) {
            if (v && !v->shadow_set()) v->set_shadow(shadow_params());
        };
        apply(car_visual_.get());
        apply(car2_visual_.get());
        for (const auto& v : opp_visuals_) apply(v.get());
    }
    items_.clear();
    course_scene_->add_items(items_, eye, mirror);
    scenery_items_ = items_.size();
    billboards_.add_items(items_, {right.x, right.y, right.z});
    {
        const std::array<float, 3>& l = sun_.params().scene_light0_dir;
        pylons_.add_items(items_, eye, {l[0], l[1], l[2]});
    }
    if (smoke_.loaded()) {
        render::Vec3 r = render::normalize(render::Vec3{right.x, right.y, right.z});
        // Camera-facing about the vertical: up is world up made orthogonal to the camera's right.
        render::Vec3 up = render::normalize(render::Vec3{0, 1, 0} - r * r.y);
        smoke_.add_items(items_, eye, r, up, smoke_v);
    }
    player_items_[0] = player_items_[1] = items_.size();
    if (player) {
        CarLook look = car_look(render_state_, true, 0);
        look.lamp = brake_lamps_.factor(0); look.brake_now = brake_lamps_.now(0);
        car_visual_->add_items(items_, render_state_, vehicle_->setup(), eye, look);
        player_items_[1] = items_.size();
    }
    if (second && field_ && field_->has_second()) {
        const CarVisual* v2 = car2_visual_ ? car2_visual_.get() : car_visual_.get();
        CarLook look = car_look(render_state2_, false, 1);
        look.lamp = brake_lamps_.factor(1); look.brake_now = brake_lamps_.now(1);
        v2->add_items(items_, render_state2_, field_->second_setup(), eye, look);
    }
    if (const RaceField* f = exact_field()) // (the opponents on the course: a race's, a free practice's)
        for (int i = 0; i < f->opponents() && size_t(i) < opp_visuals_.size(); ++i)
            if (opp_visuals_[size_t(i)] && i != hidden_opponent_)
            {
                sim::VehicleState os = interpolate(f->opponent_prev_state(i), f->opponent_state(i), step_.alpha());
                CarLook look = car_look(os, false, 2 + size_t(i));
                look.lamp = brake_lamps_.factor(2 + size_t(i)); look.brake_now = brake_lamps_.now(2 + size_t(i));
                size_t first = items_.size();
                opp_visuals_[size_t(i)]->add_items(items_, os, f->opponent_setup(i), eye, look);
                if (f->opponent_ghost(i)) // online: a ghosted remote car (no contact)
                    for (size_t k = first; k < items_.size(); ++k) items_[k].alpha = 0.5f;
            }
    // The ghost (presentation §G11), live only, while Select's switch is on: a car of the race (a whole run's ghost car:
    // licence tests, time trials, machine tests; the free run's ghost car), drawn as the race draws it (not while the
    // computer drives it). The Analyzer's REPLAY: track B's lap in step with track A's.
    sim::VehicleState gs;
    const bool logger_ghost = logger_mode_ && replaying_;
    const bool live_ghost = preset_.ghost && ghost_shown_ && !replaying_ && !demo_ && !preview_;
    const sim::VehicleSetup* ghost_setup = nullptr;
    auto add_ghost = [&](const sim::VehicleSetup& su) {
        size_t first = items_.size();
        CarLook ghost_look;
        ghost_look.instance = 1; // its own morph geometry (the car's is drawn this frame too)
        car_visual_->add_items(items_, gs, su, eye, ghost_look);
        for (size_t i = first; i < items_.size(); ++i) items_[i].alpha = 0.4f;
    };
    if (live_ghost && exact_field() && (whole_run_ghost() || (lap_ghost() && track_->lap_started())) &&
        exact_ghost(*exact_field(), step_.alpha(), gs, &ghost_setup)) {
        add_ghost(*ghost_setup);
    } else if (logger_ghost && analysis_ && exact_field() && exact_ghost(*exact_field(), step_.alpha(), gs, &ghost_setup)) {
        add_ghost(*ghost_setup); // the Analyzer's race: track B's car, car 1 of the race
    } else if (logger_ghost && !logger_ghost_.empty()) {
        const double ghost_t = (double(player_.tick()) - double(logger_begin_) + step_.alpha() - 1.0) * step_.step();
        if (logger_ghost_.state_at(ghost_t, 1.0 / step_.step(), gs)) add_ghost(vehicle_->setup());
    }
}

HudRun RaceScreen::hud_run() const {
    return two_player_                 ? HudRun::Battle
           : machine_                  ? (machine_->timed() ? HudRun::MachineTime : HudRun::MachineSpeed)
           : !license_event_.empty()   ? HudRun::License
           : free_run_                 ? HudRun::FreePractice
           : field_                    ? HudRun::Race
           : solo_free_run()           ? HudRun::FreeRun
                                       : HudRun::TimeTrial;
}

HudData RaceScreen::hud_data(int player) {
    HudData d;
    d.imperial = imperial_;
    bool p2 = player == 1;
    const sim::VehicleState& s = p2 ? render_state2_ : render_state_;
    const sim::VehicleSetup& su = p2 ? field_->second_setup() : vehicle_->setup();
    const CameraRig& cam = p2 ? camera2_ : camera_;
    const sim::ControlInputs& in = p2 ? last_in2_ : last_in_;
    d.speed_kmh = s.speed_kmh;
    d.rpm = s.engine_rpm;
    d.rev_limit_rpm = su.rev_limit_rpm;
    d.shift_rpm = su.upshift_rpm > 0 ? su.upshift_rpm : su.rev_limit_rpm - 500;
    d.onboard = cam.onboard();
    if (!car_.ratios.empty() && car_.final_drive > 0)
        d.top_speed_kmh = car_.limit_rpm / 60.0f * 2 * 3.14159f * car_.wheels.radius[1] /
                          (car_.ratios.back() * car_.final_drive) * 3.6f;
    d.player_x = s.position.x, d.player_z = s.position.z;
    d.has_turbo = su.has_turbo;
    d.boost = p2 ? field_->second_boost() : vehicle_->boost();
    if (const RaceField* f = exact_field())
        for (int i = 0; i < f->opponents(); ++i) d.others.push_back({f->opponent_state(i).position.x, f->opponent_state(i).position.z});
    if (field_) {
        if (field_->has_second()) {
            const sim::VehicleState& o = p2 ? render_state_ : render_state2_;
            d.others.push_back({o.position.x, o.position.z});
        }
    }
    d.gear = s.gear;
    d.automatic = in.automatic;
    if (!p2) d.hint_gear = hint_gear_, d.hint_blink = hint_blink_;
    // Odometer cluster and lamps (player 1's car; the 2P halves show no cluster).
    if (!p2) {
        d.trip_m = odometer_m_;
        // The car's own odometer and oil counter as it drives (physics §10.3): in GT mode the
        // garage car's words the run started with; the oil lamp lights once the oil is worn.
        if (vehicle_->facts()) {
            const sim::EngineCondition c = vehicle_->engine_condition();
            d.odometer_km = 2.0 * double(c.odometer) / 1000.0;
            d.oil_lamp = sim::oil_worn(c.oil_m);
        } else {
            d.odometer_km = odometer_m_ / 1000.0;
        }
        d.handbrake_lamp = in.handbrake;
        if (const sim::VehicleTelemetry* t = vehicle_->telemetry(); t && t->tcs_status >= 0) {
            d.tcs_lamp = t->tcs_status; // the car's own lamp states (F+0x521 / +0x520)
            d.asm_lamp = t->asm_status > 0 ? 1 : 0;
            d.clutch_engaged = t->clutch_locked;
        }
    }
    d.rev_limiter = s.rev_limiter;
    d.throttle = in.throttle;
    d.brake = in.brake;
    d.steer = in.steer;
    if (p2) {
        FieldLap l = field_lap2();
        d.lap = l.started ? l.laps + 1 : 0;
        d.lap_time = l.current, d.last_lap = l.last, d.best_lap = l.best, d.wrong_way = l.wrong_way;
    } else {
        d.lap = track_->lap_started() ? track_->laps_completed() + 1 : 0;
        // A time trial stopped by a course-out / hit is not finished: its lap time runs on with
        // the clock (presentation §D4).
        d.lap_time = track_->current_lap_time(field_ ? field_->race_time() : race_time_);
        // Free practice: 0 until the first line crossing (the warm-up lap; presentation §B1, the conductor's HUD lap time).
        if (free_run_ && !track_->lap_started()) d.lap_time = 0;
        d.last_lap = track_->last_lap_time();
        d.best_lap = track_->best_lap_time();
        d.wrong_way = track_->wrong_way();
        // Machine tests (presentation §D4): "Time" for the time tests, the clock from GO, then
        // the time to the goal once it is reached; the top-speed test the speed lines.
        if (machine_) {
            d.lap_time = finished_ ? machine_time_ : race_time_;
            d.best_kmh = hud_top_kmh_;
        }
        // Best in the solo runs (time trials, free runs, machine tests, licence tests): the
        // visit's best lap (time to the goal, judged time), kept across retries, dashes until
        // one; the session best (best_splits_) is that car's alone (presentation §D4).
        if (whole_run_ghost() || lap_ghost())
            d.best_lap = best_splits_.best_lap() >= 0 ? double(best_splits_.best_lap()) / 1000.0 : -1;
    }
    // The layout (presentation §D4) and what its time lines show.
    HudModeSelect sel;
    sel.run = hud_run();
    sel.event_laps = race_laps_;
    sel.time_limit_min = time_limit_ > 0 ? int(time_limit_ / 60.0) : 0;
    sel.wide = !app_ || !split_horizontal(*app_);
    sel.play = replaying_ ? HudPlay::Replay : HudPlay::Live;
    sel.battle_split_replay = replaying_ && two_player_ && replay_settings_.split;
    d.mode = hud_mode(sel);
    if (preset_.hud_mode >= 0 && !hud_layout(HudMode(preset_.hud_mode)).empty()) d.mode = HudMode(preset_.hud_mode);
    d.live = !replaying_ && !attract_;
    const LapList& laps = lap_lists_[p2 ? 1 : 0];
    d.lap_times.assign(laps.recent.begin(), laps.recent.end());
    d.laps_recorded = laps.count;
    // Total / Time: the recorded laps and the current one, the recorded ones alone once the
    // car has finished (a time trial's FAIL is no finish).
    d.finished = p2 ? finished2_ : finished_ && !tt_failed_;
    d.total_time = d.finished ? laps.sum : laps.sum + std::max(0.0, d.lap_time);
    if (d.lap_time < 0 && !d.finished) d.total_time = laps.count > 0 ? laps.sum : -1;
    // Record Lap / P.P. / Fastest: the session's fastest lap driven (every car's in races, the
    // player's in solo runs; dashes until the first lap, kept across retries).
    // (Free practice: every car's allowed laps, race info + 0x8BC, which the exact core keeps.)
    d.session_best = field_ ? (session_best_ms_ >= 0 ? double(session_best_ms_) / 1000.0 : -1)
                     : free_run_ ? (exact_session_[4] != race::kPracticeNoTime ? double(exact_session_[4]) / 1000.0 : -1)
                                 : (best_splits_.best_lap() >= 0 ? double(best_splits_.best_lap()) / 1000.0 : -1);
    d.pole_icon = d.mode == HudMode::FreePractice && practice_pole_car_ != kNoPoleCar; // the P.P. car's picture
    // The lap count's total: the event's laps in races, the arcade time trial's one lap
    // (presentation §B7; the capture's "Lap 1/1"), none in free runs.
    d.lap_list_lines = lap_list_lines(sel.run == HudRun::Race ? race_laps_ : 0);
    if (sel.run == HudRun::TimeTrial && from_menu_ && launch_.request.mode == "time_trial") d.total_laps = kTimeTrialLaps;
    if (field_) {
        d.total_laps = race_laps_;
        d.cars = field_->opponents() + 1 + (field_->has_second() ? 1 : 0);
        if (p2) d.position = finished2_ ? finish_position2_ : field_->second_position();
        else d.position = finished_ ? finish_position_ : field_->player_position();
        // 3, 2, 1 over the last 3 s of the hold (GT3 holds 10 s), START for 1 s after the
        // signal (rolling start: when the computer hands the car over).
        double rt = field_->race_time(), go = rolling_ ? kRollingAutoDrive : 0.0;
        d.countdown = rt < 0 ? (rt >= -3.0 ? -rt : -1) : (rt >= go && rt < go + 1.0 ? 0 : -1);
    } else if (tt_start_.countdown > 0 && (tt_start_.type == 0 || tt_start_.type == 1)) {
        // Licence / machine test standing start: the same count over the hold.
        d.countdown = tt_hold_ > 0 ? (tt_hold_ <= 3.0 ? tt_hold_ : -1) : (race_time_ < 1.0 ? 0 : -1);
    } else if (flying_go_t_ >= 0) {
        // Flying start: START for 1 s from the signal at the line (presentation §B2).
        d.countdown = race_time_ - flying_go_t_ < 1.0 ? 0 : -1;
    }
    // Free practice: the player's place among the field by best lap (the conductor's ranking, car + 0x1465).
    if (!field_ && free_run_)
        if (const RaceField* f = exact_field(); f && f->opponents() > 0) {
            d.cars = f->opponents() + 1;
            d.position = f->player_position();
        }
    // Pit lane (the race's AI drives the car: driver kinds 2-4) / flying start run-up: AUTO DRIVE; pit-in pop-up; tyre
    // gauge when wear is on.
    const int kind = p2 ? 0 : driver_kind();
    d.auto_drive = !replaying_ && ((two_player_ && (p2 ? finished2_ : finished_)) || (kind >= 2 && kind <= 4) || flying_runup());
    // The event display: lap time, splits, gaps, "+N Laps", "Pitting in..." (presentation §D6).
    {
        const EventDisplay& ev = event_display_[p2 ? 1 : 0];
        for (const EventDisplay::Message* m : ev.visible()) d.events.push_back(*m);
        const EventDisplay::Icon icon = ev.icon();
        d.event_icon = icon.on;
        d.event_icon_dx = icon.dx, d.event_icon_dy = icon.dy;
        event_icon_car_[p2 ? 1 : 0] = icon.car;
    }
    if (!p2)
        if (const sim::VehicleTelemetry* t = vehicle_->telemetry(); t && t->wear_on) {
            d.show_tires = true;
            for (int i = 0; i < 4; ++i) d.tire_levels[size_t(i)] = t->wear_level[size_t(i)];
        }
    if (license_times_.valid() && !p2) {
        for (int i = 0; i < 3; ++i) d.license_targets[size_t(i)] = license_times_.ms[size_t(i)] / 1000.0;
        // (Best: the visit's best judged time, medal or not, above: the licence time is the only
        // "lap" a licence test posts.)
        // The licence clock (the original's HUD: clock - start): standing tests from GO,
        // flying ones from the first line crossing (0 before it); frozen at the result.
        // After the run ends the original's HUD reads the car's recorded time: the finish
        // time, or 359,999,999 after a FAIL, which its formatter shows as the font's dashes
        // (race::kHudNoTimeText).
        if (license_.state() == LicenseRun::State::Finished) d.lap_time = license_.time();
        else if (license_.state() == LicenseRun::State::Failed) d.lap_time = -1;
        else if (!license_.rules().standing && !track_->lap_started()) d.lap_time = 0;
        else d.lap_time = std::max(0.0, race_time_ - license_clock_start_);
    }
    d.car_name = car_.name;
    d.course_name = course_name_;
    d.camera = camera_mode_name(cam.mode());
    d.split = two_player_;
    if (app_) {
        const TouchRect sa = app_->touch().safe_area(); // touch controls: keep the HUD clear of them
        d.safe_area = {sa.x, sa.y, sa.w, sa.h};
        for (const TouchRect& c : app_->touch().control_bounds()) d.touch_controls.push_back({c.x, c.y, c.w, c.h});
    }
    bool failed = tt_failed_ || (!license_event_.empty() && license_.state() == LicenseRun::State::Failed);
    // "@finish" (4 s) on a race / time-trial finish; GT3's licence pass posts no finish
    // event (presentation.md A1), so licences show none, and none on a fail.
    if ((p2 ? finished2_ : finished_) && (p2 || !failed) && license_event_.empty()) d.finish_t = finish_wait_;
    if (two_player_) d.winner_player = p2 ? (p2_won_ ? 2 : 0) : (p1_won_ ? 1 : 0);
    // The course name over the start (presentation §B3), on its own clock (not 2P: [?]). The
    // countdown, START and FINISH are written into the same message in a live run, so once one
    // shows the name is gone: a race's 10 s hold lets it play out first, a licence test's 4 s
    // hold does not. Replays and demonstrations write none of them and keep it.
    if (!replaying_ && !demo_ && (d.countdown >= 0 || d.finish_t >= 0)) title_clock_.stop();
    if (!two_player_ && title_clock_.alpha() > 0) {
        // Capitals as the original makes them (a-z only).
        for (char c : title_name_) d.intro_title += c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c;
        d.title_alpha = title_clock_.alpha();
    }
    return d;
}

// Sun visibility: the original depth-tests an N x N pixel box around the projected sun
// (N = Env0 +0x9E8, at 448 lines); we cast a 3 x 3 grid of rays through that box against
// the course's opaque triangles and use the fraction that gets through.
float RaceScreen::sun_visibility(const render::Vec3& eye, float fovy, float rect_h) {
    const SunParams& sp = sun_.params();
    if (!sp.enabled || !course_scene_ || course_scene_->occluder().empty()) return 1.0f;
    (void)rect_h;
    render::Vec3 d = sp.direction();
    // Angular size of the box: N of 448 lines over the vertical field of view.
    float half = std::max(1.0f, sp.test_box) * 0.5f * fovy / 448.0f;
    render::Vec3 up{0, 1, 0};
    render::Vec3 r = normalize(cross(d, up)), u = cross(r, d);
    int open = 0, n = 0;
    for (int i = -1; i <= 1; ++i)
        for (int j = -1; j <= 1; ++j) {
            render::Vec3 dir = normalize(d + r * (float(i) * half) + u * (float(j) * half));
            ++n;
            open += course_scene_->occluder().blocked(eye, dir, 6000.0f) ? 0 : 1;
        }
    return float(open) / float(n);
}

// `[race] split = auto|horizontal|vertical` (auto = vertical on our 16:9 screen, like GT3's
// 16:9 option); GT3R_RACE split=h|v overrides.
bool RaceScreen::split_horizontal(App& app) const {
    if (!preset_.split.empty()) return preset_.split[0] == 'h';
    std::string v = app.config().get("race", "split").value_or("auto");
    if (v == "auto") return !wide_mode_; // GT3: top / bottom in 4:3, side by side in 16:9 (wide mode)
    return v == "horizontal";
}

// Camera axes from the view matrix (rows of its rotation part).
sim::Vec3 RaceScreen::cam_right(const CameraRig& c) {
    render::Mat4 v = c.view();
    return {v.at(0, 0), v.at(0, 1), v.at(0, 2)};
}

sim::Vec3 RaceScreen::cam_forward(const CameraRig& c) {
    render::Mat4 v = c.view();
    return {-v.at(2, 0), -v.at(2, 1), -v.at(2, 2)};
}

render::DrawOptions RaceScreen::draw_options() const {
    const float* hz = course_scene_->horizon_color();
    render::DrawOptions o;
    o.frustum_cull = true;
    o.sort_blended = true;
    o.draw_distance = 2500;
    o.fog = true;
    o.fog_start = 350;
    o.fog_end = 3000;
    o.fog_max = 0.55f;
    // GT3 clears the race view to black (clear colour 0, colour + depth; docs/formats/course.md).
    // Where no geometry is drawn (below a sky dome that stops at the horizon, ground that is
    // only a dark shadow decal, e.g. Rome's infield) the picture is black-based, not sky-coloured.
    for (int i = 0; i < 3; ++i) o.fog_color[i] = hz[i], o.clear[i] = 0.0f;
    // The course's own fog (Env0): visibility = b + (1 - b) min(1, A / z) towards the fog colour.
    const SunParams& env = sun_.params();
    if (env.has_env && !no_course_fog_) {
        o.fog = env.fog_b < 0.999f && env.fog_a > 0;
        o.fog_gt3 = true;
        o.fog_start = env.fog_a;
        o.fog_max = std::clamp(1.0f - env.fog_b, 0.0f, 1.0f);
        for (int i = 0; i < 3; ++i) o.fog_color[i] = std::clamp(env.fog_rgb[size_t(i)], 0.0f, 1.0f);
    }
    apply_post_pass(o);
    o.ambient = 0.5f;
    o.light_dir = {0.3f, 0.85f, 0.35f};
    o.env_strength = 0.25f;
    // Car lighting from the course's Env0 car light (GT3 lights bodies with it and the
    // paint's material; see scene::Material).
    o.gt3_lighting = true;
    if (env.has_env && env.car_light_mask) env.day_car_lights(o); // Env0 car lights 0..3
    if (env.has_env) o.panorama[0] = env.panorama[0], o.panorama[1] = env.panorama[1];
    // Reflections: the course's sky and horizon colours around the car (stand-in for GT3's
    // per-frame sphere map), GT3's Fresnel-like weight 1 - 0.8 cos.
    const float* sky = course_scene_->sky_color();
    const float* ground = course_scene_->ground_color();
    // (From a car, scenery fills the horizon band; the sky shows only above it.)
    for (int i = 0; i < 3; ++i) o.env_sky[i] = sky[i], o.env_horizon[i] = ground[i], o.env_ground[i] = ground[i];
    o.reflection_fresnel = 0.8f;
    if (env_cube_on_ && view_) o.env_cube = view_->env_cube();
    o.env_gt3_lookup = env_gt3_scene_;
    o.reflection_strength = 1.0f;
    return o;
}

void RaceScreen::render_drive(App& app, Ui& ui) {
    app.note_race_scale();
    upload_course_animations(); // once per frame
    // The game area (App::aspect): 640 x 360 at 16:9, 480 x 360 at 4:3 (GT3's 4:3 picture),
    // the window's shape with fill.
    const float W = ui.width(), H = ui.height();
    const UiRect pic = ui.picture43();
    const SDL_FRect menu_dst{pic.x, pic.y, pic.w, pic.h};
    hud_.set_screen({0, 0, W, H});
    if (quick_) {
        ui.fill(0, 0, W, H, Color{0, 0, 0, 255});
        bool licence_box = preview_ && !license_event_.empty();
        if (panel_ == Panel::None && (backdrop_ || preview_) && (!licence_box || quick_menu_.on_root()) && app.gpu() &&
            track_ && vehicle_) {
            // The race behind the menu (race::menu_backdrop): GT3's licence menu draws it into
            // the preview box (32, 58, 280, 210) of the menu frame, the time trial's, free run's,
            // machine test's and pre-race menus over the whole frame (presentation.md); their
            // layouts are translucent there. The result menus have none (their own background).
            if (!view_) view_ = std::make_unique<render::View3D>(*app.gpu());
            float sx = menu_dst.w / 640.0f, sy = menu_dst.h / 448.0f;
            SDL_FRect box = licence_box ? SDL_FRect{menu_dst.x + 32 * sx, menu_dst.y + 58 * sy, 280 * sx, 210 * sy}
                                        : menu_dst;
            render::Vec3 eye = camera_.eye();
            collect_items(eye, cam_right(camera_), !camera_.onboard(), true, smoke_view(camera_, box.w / box.h, camera_.fovy()));
            view_->draw(ui.renderer(), box, camera_.view(), camera_.projection(box.w / box.h), eye, items_, draw_options());
        }
        if (panel_ != Panel::None) draw_panel(app, menu_dst);
        else quick_menu_.draw(ui.renderer(), menu_dst);
        if (toast_time_ > 0) {
            float w = std::ceil(Ui::text_width(toast_)) + 16;
            ui.fill(W / 2 - w / 2, 80, w, 18, Color{0, 0, 0, 170});
            ui.text(W / 2 - w / 2 + 8, 85, toast_, colors::kText);
        }
        return;
    }
    if (!view_) view_ = std::make_unique<render::View3D>(*app.gpu()), view_->capture_from_env();
    render::DrawOptions o = draw_options();
    // (2P replays: REPLAY OPTIONS SCREEN single shows player 1's view only.)
    bool split = two_player_ && field_ && field_->has_second() && !(replaying_ && !replay_settings_.split);
    // 2P battle. Vertical (GT3's 16:9 option, our default on a 16:9 screen): side by side
    // (player 1 left), each 3D view letterboxed to (x0, H/6, W/2, 8H/11), the HUD on the
    // whole half. Horizontal (the PS2's 4:3 default): player 1 on top, player 2 below.
    SDL_FRect rect{0, 0, W, H};
    SDL_FRect rect2{0, 0, 0, 0}, half1{}, half2{};
    bool horizontal = split_horizontal(app);
    if (split) {
        if (horizontal) {
            half1 = rect = {0, 0, W, H / 2};
            half2 = rect2 = {0, H / 2, W, H / 2};
        } else {
            half1 = {0, 0, W / 2, H};
            half2 = {W / 2, 0, W / 2, H};
            rect = {0, H / 6, W / 2, 8 * H / 11};
            rect2 = {W / 2, H / 6, W / 2, 8 * H / 11};
        }
        ui.fill(0, 0, W, H, Color{0, 0, 0, 255});
    }
    if (original_res_ && !split) {
        // The original's frame: 640x448 internal, 4:3, pillarboxed.
        ui.fill(0, 0, W, H, Color{0, 0, 0, 255});
        rect = menu_dst;
        o.fixed_width = 640, o.fixed_height = 448;
        o.nearest = original_nearest_;
    }
    frame_rect_ = rect;
    Uint64 t0 = SDL_GetPerformanceCounter();
#if GT3R_XR
    if (vr_stereo(app, split)) { // both eyes; the HUD and menus below go onto the panel in front
        if (update_env_cube()) o.env_cube = view_->env_cube(), o.env_gt3_lookup = env_gt3_scene_;
        render_vr_eyes(app, ui, o);
        frame_rect_ = rect = {0, 0, W, H};
    } else
#endif
    {
    Uint64 h_e0 = SDL_GetTicksNS();
    if (update_env_cube()) o.env_cube = view_->env_cube(), o.env_gt3_lookup = env_gt3_scene_;
    Uint64 h_e1 = SDL_GetTicksNS();
    hitch::env_ms = double(h_e1 - h_e0) / 1e6;
    render::Vec3 eye = camera_.eye();
    // In-car view: own car not drawn; a replay's on-board car mount: the followed car not drawn.
    const bool mount_in = camera_.replay_on_board();
    const bool followed_player = !(field_ && replay_focus_ >= 0);
    hidden_opponent_ = mount_in && !followed_player ? replay_focus_ : -1;
    collect_items(eye, cam_right(camera_), !camera_.onboard() && !(mount_in && followed_player), true,
                  smoke_view(camera_, rect.w / rect.h, camera_.fovy()));
    hidden_opponent_ = -1;
    Uint64 h_c1 = SDL_GetTicksNS();
    hitch::collect_ms = double(h_c1 - h_e1) / 1e6;
    const size_t wet_items = wet_.active() ? add_wet_road(eye, rect.w / rect.h, o) : 0;
    // This view's post-process passes (presentation §G16): the dissolve's frozen picture at
    // trunc(128 x^3) / 128, view mode 1's trail at trunc(128 a) / 128; every picture kept.
    render::DrawOptions ov = o;
    ov.keep_last = true;
    ov.snapshot = dissolve_.snapshot;
    dissolve_.snapshot = false;
    ov.dissolve = dissolve_.weight();
    ov.trail = std::floor(128.0f * director_.look().trail_weight()) / 128.0f;
    view_->draw(ui.renderer(), rect, camera_.view(), camera_.projection(rect.w / rect.h), eye, items_, ov);
    remove_wet_road(wet_items); // (the wet road's frame items belong to this view only)
    hitch::draw_ms = double(SDL_GetTicksNS() - h_c1) / 1e6;
    hitch::meshes = view_->stats().meshes_drawn;
    float sun_v = sun_visibility(eye, camera_.fovy(), rect.h);
    if (debug_ && sun_.params().enabled && (++sun_log_ % 60) == 0) LOG_INFO("sun visibility %.2f", sun_v);
    sun_.draw(ui.renderer(), rect, camera_.view(), camera_.fovy(), sun_v);
    auto occl = [this](const render::Vec3& a, const render::Vec3& b) {
        return course_scene_ && course_scene_->occluder().segment_blocked(a, b);
    };
    // Lamp flares: in the original-resolution frame at its 448 lines (the PS2's soft, broken
    // rays); otherwise crisp at the output resolution.
    const float flare_lines = original_res_ && !split ? 448.0f : 0.0f;
    lamps_.draw(ui.renderer(), rect, camera_.view(), eye, camera_.fovy(), occl, flare_lines);
    if (car_lamps_.active())
        car_lamps_.draw(ui.renderer(), rect, camera_.view(), eye, camera_.fovy(), lamp_cars(), occl, flare_lines);
    if (!replaying_ && camera_.onboard() && camera_.mirror() && !camera_.look_back() && (!split || horizontal))
        draw_mirror(app, ui, rect, o, false, split);
    if (split) {
        if (!view2_) view2_ = std::make_unique<render::View3D>(*app.gpu());
        render::Vec3 eye2 = camera2_.eye();
        collect_items(eye2, cam_right(camera2_), true, !camera2_.onboard(),
                      smoke_view(camera2_, rect2.w / rect2.h, camera2_.fovy())); // in-car: own car not drawn
        view2_->draw(ui.renderer(), rect2, camera2_.view(), camera2_.projection(rect2.w / rect2.h), eye2, items_, o);
        sun_.draw(ui.renderer(), rect2, camera2_.view(), camera2_.fovy(), sun_visibility(eye2, camera2_.fovy(), rect2.h));
        if (horizontal && !replaying_ && camera2_.onboard() && camera2_.mirror() && !camera2_.look_back())
            draw_mirror(app, ui, rect2, o, true, true);
    }
    } // (the flat views)
    if (preset_.gpusync) SDL_WaitForGPUIdle(app.gpu()->device()); // measure: CPU + GPU time of the 3D pass
    double draw_ms = double(SDL_GetPerformanceCounter() - t0) * 1000.0 / double(SDL_GetPerformanceFrequency());
    if (!paused_ && frame_ms_ > 0) perf_.add(frame_ms_, draw_ms);
    log_perf(false);

    if (logger_mode_) { // the Analyzer over the replay (or its graph over everything)
        SDL_FRect dst = original_res_ ? frame_rect_ : menu_dst;
        if (analyzer_.view() == Analyzer::View::Graph) {
            ui.fill(0, 0, W, H, Color{0, 0, 0, 255});
        } else { // the bottom band (bg 0..212, indicator_back 0..148) across a wide screen too
            float k = dst.h / 448.0f;
            ui.fill(0, dst.y + dst.h - 212 * k, W, 64 * k, Color{51, 51, 51, 255});
            ui.fill(0, dst.y + dst.h - 148 * k, W, 148 * k, Color{0, 0, 0, 255});
        }
        analyzer_.draw(ui.renderer(), dst);
        return;
    }
    if (replaying_ && split) {
        // 2P replays: both halves, no HUD and no mark (the captures).
    } else if (((replaying_ && preset_.hud) || attract_ || (theater_ && demo_)) && !split) {
        // Replays and the attract demo (presentation §G): the mark on its timer, in replays only
        // while no ITEM is on, in the attract demo always; the REPLAY OPTIONS items and the title.
        if (attract_ || !replay_settings_.items)
            hud_.draw_replay_mark(ui.renderer(), mark_timer_.alpha(), original_res_ ? &rect : nullptr, original_res_);
        if (!attract_) draw_replay_items(app, ui, rect);
    } else if (preset_.hud) {
        HudData d = hud_data(0);
        if (split) {
            hud_.draw_split(ui.renderer(), d, half1, horizontal);
            draw_event_icons(app, 0);
            hud_.draw_split(ui.renderer(), hud_data(1), half2, horizontal);
            draw_event_icons(app, 1);
            if (horizontal) ui.fill(0, half1.h - 1, W, 2, Color{0, 0, 0, 255});
        } else {
            // Under the results panel: all but the lap / time block, drawn over it below.
            if (results_panel_up()) d.layer = 1;
            if (original_res_) { // the HUD in the 4:3 frame
                d.four_three = true;
                hud_.draw(ui.renderer(), d, &rect);
            } else {
                hud_.draw(ui.renderer(), d);
            }
            draw_event_icons(app, 0);
        }
        if (show_perf_) {
            char line[160];
            const render::DrawStats& ds = view_->stats();
            std::snprintf(line, sizeof(line), "%.1f ms  3D %dx%d  %d/%d meshes  %zuk tris  %s / %s", perf_.avg_frame,
                          view_->pixel_width(), view_->pixel_height(), ds.meshes_drawn,
                          ds.meshes_drawn + ds.meshes_culled, ds.triangles / 1000, vehicle_->backend_name(),
                          track_->backend_name());
            ui.text(10, 52, line, Color{200, 205, 215, 200});
        }
    }
    // Results overlay over the running race after the finish.
    if (two_player_ && split && !replaying_ && (finished_ || finished2_)) {
        // 2P (the captures): a finished player's half shows AUTO DRIVE, then the RACE RESULTS
        // panel over that half while the other player keeps racing.
        // The panel at its full-screen size, from the top of the half, clipped to it (the captures).
        std::vector<ResultRow> rows = result_rows(true);
        for (int h = 0; h < 2; ++h) {
            if (!(h == 0 ? finished_ : finished2_)) continue;
            const SDL_FRect& hf = h == 0 ? half1 : half2;
            SDL_FRect dst = menu_dst;
            dst.y = hf.y; // the full-screen panel from the top of the half
            SDL_Rect clip{int(hf.x), int(hf.y), int(hf.w), int(hf.h)};
            SDL_SetRenderClipRect(ui.renderer(), &clip);
            std::vector<SDL_FRect> icons;
            results_.draw(ui.renderer(), hud_.fonts(), rows, finish_wait_, dst, &icons);
            for (size_t i = 0; i < rows.size() && i < icons.size(); ++i) draw_car_icon(app, rows[i].car, icons[i], i);
            SDL_SetRenderClipRect(ui.renderer(), nullptr);
        }
    } else if (finished_ && !replaying_) {
        if (field_) {
            // Car pictures in every row, as in 2P (the 1P capture of the Sunday Cup results).
            std::vector<ResultRow> rows = result_rows(true);
            std::vector<SDL_FRect> icons;
            results_.draw(ui.renderer(), hud_.fonts(), rows, finish_wait_, menu_dst, &icons);
            for (size_t i = 0; i < rows.size() && i < icons.size(); ++i) draw_car_icon(app, rows[i].car, icons[i], i, true);
            // The original draws the HUD's lap / time block over the panel (the capture).
            if (preset_.hud && results_panel_up()) {
                HudData d = hud_data(0);
                d.layer = 2;
                if (original_res_) {
                    d.four_three = true;
                    hud_.draw(ui.renderer(), d, &frame_rect_);
                } else {
                    hud_.draw(ui.renderer(), d);
                }
            }
        } else if (!license_event_.empty()) {
            // Over the live race (the car under the stop driver, the race camera, the HUD):
            // a 1/3 black dim, the medal model, the word and the time; or the bluish wash
            // with TIME EXPIRED / FAIL.
            int f = result_frame(), medal = presented_medal();
            SDL_FRect full{0, 0, W, H};
            if (medal >= 0) {
                license_overlay_.draw_dim(ui.renderer(), f, full);
                float w = LicenseResultOverlay::model_weight(f, medal);
                if (prize_ && w > 0) {
                    const float clear[4] = {0, 0, 0, 0};
                    prize_->set_alpha(w);
                    prize_->draw(ui.renderer(), menu_dst, clear);
                }
                license_overlay_.draw_medal(ui.renderer(), hud_.fonts(), f, medal, license_.time(), menu_dst);
            } else {
                bool failed = license_.state() == LicenseRun::State::Failed;
                license_overlay_.draw_fail(ui.renderer(), hud_.fonts(), f,
                                           race_text(failed ? "RACEDISPLAY_ExamFail" : "RACEDISPLAY_ExamTimeOver",
                                                     failed ? "FAIL" : "TIME EXPIRED"),
                                           failed ? -1.0 : license_.time(), full);
            }
        } else if (!preset_.hud) {
            // Display off (hud=0): no time-trial FAIL / record overlays over the scene.
        } else if (tt_failed_) {
            license_overlay_.draw_fail(ui.renderer(), hud_.fonts(), result_frame(),
                                       race_text("RACEDISPLAY_TimeTrialFail", "FAIL"), -1.0,
                                       SDL_FRect{0, 0, W, H});
        } else {
            // Time trials: the best lap; machine tests: the time to the mark or the top speed.
            TimeTrialOverlay::Data td;
            td.rank = finish_record_rank_;
            td.speed = machine_test_top_speed();
            td.value = td.speed ? double(top_speed_kmh_) : machine_ ? machine_time_ : track_->best_lap_time();
            td.imperial = imperial_;
            // (After the top-speed test's EXIT it shows 0.1 s after the press, race/post_run.h.)
            tt_overlay_.draw(ui.renderer(), hud_.fonts(), finish_wait_ + TimeTrialOverlay::kShowDelay - overlay_delay_, td,
                             menu_dst);
        }
    } else if (finished2_ && two_player_ && !replaying_) {
        results_.draw(ui.renderer(), hud_.fonts(), result_rows(), finish_wait_, menu_dst);
    }
    if (exit_transition_ > kExitHold) { // the accumulated grey wash of the delayed fade-out (presentation §C1)
        const SDL_FColor w = menu::flow::ScreenTransition::delay_fadeout_cover(exit_transition_);
        const u8 g = u8(std::lround(255.0f * w.r));
        ui.fill(0, 0, W, H, Color{g, g, g, u8(255 * w.a)});
    }
    if (toast_time_ > 0) {
        float w = std::ceil(Ui::text_width(toast_)) + 16;
        ui.fill(W / 2 - w / 2, 80, w, 18, Color{0, 0, 0, 170});
        ui.text(W / 2 - w / 2 + 8, 85, toast_, colors::kText);
    }
    if (paused_) render_pause(ui);
}

// GT3's wet-road reflection (docs/dev/RACE.md) for the main view: the buffer (slot 9, mirrored cars, lamp
// streaks) rendered and blurred, then the frame items (alpha clear, slot 13 mask, composite).
size_t RaceScreen::add_wet_road(const render::Vec3& eye, const render::Mat4& view, const render::Mat4& proj,
                                const render::Frustum& fr, const render::DrawOptions& o) {
    wet_items_.clear();
    std::vector<WetRoad::Car> cars;
    auto add = [&](const CarVisual* v, const sim::VehicleState& s, const sim::VehicleSetup& su, size_t idx) {
        if (!v) return;
        render::Mat4 mir;
        if (!CarVisual::road_mirror(s, mir)) return;
        if (wet_.params().cars) v->add_mirror_items(wet_items_, s, su, eye);
        if (!v->lamps().empty()) cars.push_back({&v->lamps(), v->body_matrix(s, su), mir, brake_lamps_.factor(idx), brake_lamps_.now(idx)});
    };
    add(car_visual_.get(), render_state_, vehicle_->setup(), 0);
    if (field_ && field_->has_second())
        add(car2_visual_ ? car2_visual_.get() : car_visual_.get(), render_state2_, field_->second_setup(), 1);
    if (const RaceField* f = exact_field())
        for (int i = 0; i < f->opponents() && size_t(i) < opp_visuals_.size(); ++i)
            if (opp_visuals_[size_t(i)])
                add(opp_visuals_[size_t(i)].get(), interpolate(f->opponent_prev_state(i), f->opponent_state(i), step_.alpha()),
                    f->opponent_setup(i), 2 + size_t(i));
    wet_.add_buffer_items(wet_items_, view, eye, cars);
    SDL_GPUTexture* t = wet_.render(*view_, view, proj, eye, wet_items_, o);
    // GT3 composites it after the scenery, before the cars and their shadows (presentation §K, §I7).
    std::vector<render::DrawItem> frame;
    wet_.add_frame_items(frame, view, fr, t);
    items_.insert(items_.begin() + std::ptrdiff_t(scenery_items_), frame.begin(), frame.end());
    return frame.size();
}

// Every car's lamps for the glow pass: the player, player 2 and the opponents.
std::vector<CarLampGlows::Car> RaceScreen::lamp_cars() const {
    std::vector<CarLampGlows::Car> out;
    auto add = [&](const CarVisual* v, const sim::VehicleState& s, const sim::VehicleSetup& su, size_t idx) {
        if (!v || v->lamps().empty()) return;
        out.push_back({&v->lamps(), v->body_matrix(s, su), brake_lamps_.factor(idx), brake_lamps_.now(idx)});
    };
    add(car_visual_.get(), render_state_, vehicle_->setup(), 0);
    if (field_ && field_->has_second())
        add(car2_visual_ ? car2_visual_.get() : car_visual_.get(), render_state2_, field_->second_setup(), 1);
    if (const RaceField* f = exact_field())
        for (int i = 0; i < f->opponents() && size_t(i) < opp_visuals_.size(); ++i)
            if (opp_visuals_[size_t(i)])
                add(opp_visuals_[size_t(i)].get(), interpolate(f->opponent_prev_state(i), f->opponent_state(i), step_.alpha()),
                    f->opponent_setup(i), 2 + size_t(i));
    return out;
}

} // namespace gt3r::race_screen
