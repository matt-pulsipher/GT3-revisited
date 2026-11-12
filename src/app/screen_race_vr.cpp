// VR headsets (docs/dev/VR.md): the race in stereo while driving. The headset's seated space
// (origin at the player's head, -z ahead) is placed in the world at the view's eye point: in
// the in-car views at the car's camera point 0 ([vr] seat_side, seat_forward, seat_height from there),
// turning with the car (or with its heading only, [vr] level_horizon); in the chase views at
// the chase camera, level, turning with its lagged heading; in the player's own cameras at
// the camera's eye, facing its heading (CameraRig::custom_seat). Each eye is that placement
// times the eye's tracked pose. Replays, the pre-race shot, 2P split screen, the camera editor
// and the debug cameras stay on the virtual screen.
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"

#if GT3R_XR

namespace gt3r::race_screen {

namespace {
constexpr float kPi = 3.14159265f;
}

bool RaceScreen::vr_stereo(App& app, bool split) const {
    const xr::Session* x = app.xr();
    if (!x || !x->settings().stereo || !x->eyes_valid() || !view_ || !course_scene_) return false;
    if (split || replaying_ || attract_ || demo_ || logger_mode_ || editor_ || camera_.intro()) return false;
    const CameraMode m = camera_.mode();
    return m == CameraMode::Bumper || m == CameraMode::Roof || m == CameraMode::Chase || m == CameraMode::ChaseFar ||
           m == CameraMode::Custom;
}

render::Mat4 RaceScreen::vr_base(const xr::Settings& st) const {
    const sim::Quat& q = render_state_.orientation;
    const render::Vec3 eye = camera_.eye();
    // One of the player's own cameras: its eye and heading, the head doing its pitch and tilt. The
    // seat's height and reach move an in-car mount's eye as they do the in-car views' (its side is
    // the camera's own Sideways); the other mounts are where the camera puts them.
    if (const CustomCamera* c = camera_.custom()) {
        const bool seat = c->mount == CustomCamera::Mount::Incar;
        return camera_.custom_seat(st.level_horizon, seat ? render::Vec3{0, st.seat_height, -st.seat_forward} : render::Vec3{});
    }
    if (camera_.car_mounted()) { // the car's full attitude or its heading, the seat moved within the car
        const sim::Vec3 f = rotate(q, {0, 0, -1});
        const render::Mat4 car = st.level_horizon ? render::rotate_y(std::atan2(-f.x, -f.z))
                                                  : render::rotate_quat(q.w, q.x, q.y, q.z);
        const render::Mat4 seat = render::translate(eye) * car * render::translate(st.seat(driver_side_));
        return camera_.look_back() ? seat * render::rotate_y(kPi) : seat;
    }
    // Chase: the rig's lagged heading (look-back included), level.
    const sim::Vec3 f = cam_forward(camera_);
    return render::translate(eye) * render::rotate_y(std::atan2(-f.x, -f.z));
}

void RaceScreen::render_vr_eyes(App& app, Ui& ui, const render::DrawOptions& o) {
    xr::Session& x = *app.xr();
    const xr::Settings& st = x.settings();
    SDL_Renderer* r = ui.renderer();
    SDL_Texture* canvas = SDL_GetRenderTarget(r);
    const render::Mat4 base = vr_base(st);

    // The items once for both eyes, from the head. The own car as the flat view has it (a custom
    // camera's fades out while it would block the view); [vr] car_body = on draws its body round
    // a view on the car that leaves it out. Round the driver's seat (that body, and a custom
    // in-car camera's own) it is drawn from inside: its bonnet, not its roof and windows.
    const render::Mat4 head = base * x.head().matrix();
    const render::Vec3 head_pos = head.transform_point({0, 0, 0});
    const sim::Vec3 right{head.at(0, 0), head.at(1, 0), head.at(2, 0)};
    const xr::Eye& e0 = x.eye(0);
    TyreSmoke::View sv;
    sv.forward = render::normalize(render::Vec3{-head.at(0, 2), -head.at(1, 2), -head.at(2, 2)});
    sv.proj_x = 2.0f / std::max(e0.tan_right - e0.tan_left, 1e-3f);
    sv.near_m = camera_.near_plane, sv.far_m = camera_.gt3_far > 0 ? camera_.gt3_far : camera_.far_plane;
    const float car_fade = own_car_fade(camera_.body_blocks_view());
    const bool shown = !camera_.hides_car() && car_fade > 0;
    const bool cabin = camera_.hides_car() && st.show_car && camera_.car_mounted();
    spark_pass_ = true, spark_view_ = render::inverse(head); // (the sparks' streaks from the head)
    const CustomCamera* custom = camera_.custom();
    const bool inside = cabin || (custom && custom->mount == CustomCamera::Mount::Incar);
    collect_items(head_pos, right, shown || cabin, true, sv);
    spark_pass_ = false;
    for (size_t i = player_items_[0]; i < player_items_[1]; ++i) {
        if (inside) items_[i].single_sided = true;
        if (!cabin && car_fade < 1) items_[i].alpha = std::min(items_[i].alpha, car_fade); // (translucent, as the ghost car is)
    }
    const float fovy = std::atan(e0.tan_up) - std::atan(e0.tan_down);
    const float sun_v = sun_visibility(head_pos, fovy, float(e0.height));
    // The glows of the cars as the flat view has them: on board, not the own car's (its body here is
    // seen from inside, so it hides nothing either).
    const GlowCars glow = has_glows() ? glow_cars(!camera_.hides_car(), true, -1) : GlowCars{};
    auto occl = [&](const render::Vec3& a, const render::Vec3& b) { return glow_blocked(glow, a, b); };
    // The sky domes centred on each eye, not on the head between them: they are small (Route
    // 11's is 33-51 m), so a shared centre put the sky at that distance in stereo, nearer than
    // the scenery drawn over it. Centred on the eye it has no parallax: at infinity.
    std::vector<std::pair<size_t, render::Mat4>> sky;
    for (size_t k = 0; k < items_.size(); ++k)
        if (items_[k].background) sky.emplace_back(k, items_[k].transform);
    // GT3's reflections are looked up in the camera's frame (sphere maps, the course's environment
    // programs): oriented by the race camera, as on a flat screen, not by the head turning in it.
    render::DrawOptions eo = o;
    eo.reflection_view = camera_.view();

    for (int i = 0; i < 2; ++i) {
        const xr::Eye& e = x.eye(i);
        const render::Mat4 world = base * e.pose.matrix();
        const render::Mat4 view = render::inverse(world);
        const render::Vec3 eye = world.transform_point({0, 0, 0});
        const render::Mat4 proj = e.projection(camera_.near_plane, camera_.far_plane);
        const render::Frustum fr{e.tan_left, e.tan_right, e.tan_down, e.tan_up};
        for (const auto& [k, t] : sky) items_[k].transform = render::translate(eye - head_pos) * t;
        const SDL_FRect full{0, 0, float(e.width), float(e.height)};
        if (!vr_views_[i]) vr_views_[i] = std::make_unique<render::View3D>(*app.gpu());
        const size_t wet_items = wet_.active() ? add_wet_road(eye, view, proj, fr, o) : 0;
        // Straight into the eye's canvas (no composite pass); the glows go on top with the 2D
        // renderer, whose work runs after this.
        auto* tex = static_cast<SDL_GPUTexture*>(
            SDL_GetPointerProperty(SDL_GetTextureProperties(e.canvas), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
        const bool direct = tex && vr_views_[i]->draw_to(tex, e.width, e.height, view, proj, eye, items_, eo);
        SDL_SetRenderTarget(r, e.canvas);
        if (!direct) {
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255); // opaque: the course passes keep the target's alpha
            SDL_RenderClear(r);
            vr_views_[i]->draw(r, full, view, proj, eye, items_, eo);
        }
        remove_wet_road(wet_items); // (the wet road's frame items belong to this eye only)
        sun_.draw(r, full, view, fr, sun_v);
        lamps_.draw(r, full, view, eye, fr, occl);
        if (car_lamps_.active()) car_lamps_.draw(r, full, view, eye, fr, glow.lamps, occl);
        if (exit_transition_ > kExitHold) { // the race -> menu grey wash, over the whole view (presentation §C1)
            const SDL_FColor w = menu::flow::ScreenTransition::delay_fadeout_cover(exit_transition_);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColorFloat(r, w.r, w.g, w.b, w.a);
            SDL_RenderFillRect(r, &full);
        }
    }
    for (const auto& [k, t] : sky) items_[k].transform = t;
    hitch::meshes = vr_views_[0]->stats().meshes_drawn;

    // Back on the canvas: transparent, for the HUD panel.
    SDL_SetRenderTarget(r, canvas);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    // The rear-view mirror where GT3 draws it, at the top of the panel.
    if (camera_.shows_mirror())
        draw_mirror(app, ui, SDL_FRect{0, 0, ui.width(), ui.height()}, o, false, false);
    x.submit_stereo();
}

} // namespace gt3r::race_screen

#endif // GT3R_XR
