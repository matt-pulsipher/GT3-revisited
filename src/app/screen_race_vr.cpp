// VR headsets (docs/dev/VR.md): the race in stereo while driving. The headset's seated space
// (origin at the player's head, -z ahead) is placed in the world at the view's eye point: in
// the in-car views at the car's camera point 0, turning with the car (or with its heading
// only, [vr] level_horizon); in the chase views at the chase camera, level, turning with its
// lagged heading. Each eye is that placement times the eye's tracked pose. Replays, the
// pre-race shot, 2P split screen and the debug cameras stay on the virtual screen.
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
    if (split || replaying_ || attract_ || demo_ || logger_mode_ || camera_.intro()) return false;
    const CameraMode m = camera_.mode();
    return m == CameraMode::Bumper || m == CameraMode::Roof || m == CameraMode::Chase || m == CameraMode::ChaseFar;
}

render::Mat4 RaceScreen::vr_base(const xr::Settings& st) const {
    const sim::Quat& q = render_state_.orientation;
    const render::Vec3 eye = camera_.eye();
    float yaw = 0;
    if (camera_.onboard()) {
        if (!st.level_horizon) { // the car's full attitude
            render::Mat4 rot = render::rotate_quat(q.w, q.x, q.y, q.z);
            if (camera_.look_back()) rot = rot * render::rotate_y(kPi);
            return render::translate(eye) * rot;
        }
        sim::Vec3 f = rotate(q, {0, 0, -1});
        yaw = std::atan2(-f.x, -f.z);
        if (camera_.look_back()) yaw += kPi;
    } else { // chase: the rig's lagged heading (look-back included), level
        sim::Vec3 f = cam_forward(camera_);
        yaw = std::atan2(-f.x, -f.z);
    }
    return render::translate(eye) * render::rotate_y(yaw);
}

void RaceScreen::render_vr_eyes(App& app, Ui& ui, const render::DrawOptions& o) {
    xr::Session& x = *app.xr();
    const xr::Settings& st = x.settings();
    SDL_Renderer* r = ui.renderer();
    SDL_Texture* canvas = SDL_GetRenderTarget(r);
    const render::Mat4 base = vr_base(st);

    // The items once for both eyes, from the head: the own car's body around the in-car view
    // unless [vr] car_body = off.
    const render::Mat4 head = base * x.head().matrix();
    const render::Vec3 head_pos = head.transform_point({0, 0, 0});
    const sim::Vec3 right{head.at(0, 0), head.at(1, 0), head.at(2, 0)};
    const xr::Eye& e0 = x.eye(0);
    TyreSmoke::View sv;
    sv.forward = render::normalize(render::Vec3{-head.at(0, 2), -head.at(1, 2), -head.at(2, 2)});
    sv.proj_x = 2.0f / std::max(e0.tan_right - e0.tan_left, 1e-3f);
    sv.near_m = camera_.near_plane, sv.far_m = camera_.far_plane;
    const bool own_car = !camera_.onboard() || st.show_car;
    collect_items(head_pos, right, own_car, true, sv);
    if (camera_.onboard()) // from inside the body: its bonnet, not its roof and windows
        for (size_t i = player_items_[0]; i < player_items_[1]; ++i) items_[i].single_sided = true;
    const float fovy = std::atan(e0.tan_up) - std::atan(e0.tan_down);
    const float sun_v = sun_visibility(head_pos, fovy, float(e0.height));
    auto occl = [this](const render::Vec3& a, const render::Vec3& b) {
        return course_scene_ && course_scene_->occluder().segment_blocked(a, b);
    };
    const std::vector<CarLampGlows::Car> lamp_list = car_lamps_.active() ? lamp_cars() : std::vector<CarLampGlows::Car>{};
    // The sky domes centred on each eye, not on the head between them: they are small (Route
    // 11's is 33-51 m), so a shared centre put the sky at that distance in stereo, nearer than
    // the scenery drawn over it. Centred on the eye it has no parallax: at infinity.
    std::vector<std::pair<size_t, render::Mat4>> sky;
    for (size_t k = 0; k < items_.size(); ++k)
        if (items_[k].background) sky.emplace_back(k, items_[k].transform);

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
        const bool direct = tex && vr_views_[i]->draw_to(tex, e.width, e.height, view, proj, eye, items_, o);
        SDL_SetRenderTarget(r, e.canvas);
        if (!direct) {
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255); // opaque: the course passes keep the target's alpha
            SDL_RenderClear(r);
            vr_views_[i]->draw(r, full, view, proj, eye, items_, o);
        }
        remove_wet_road(wet_items); // (the wet road's frame items belong to this eye only)
        sun_.draw(r, full, view, fr, sun_v);
        lamps_.draw(r, full, view, eye, fr, occl);
        if (car_lamps_.active()) car_lamps_.draw(r, full, view, eye, fr, lamp_list, occl);
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
    if (camera_.onboard() && camera_.mirror() && !camera_.look_back())
        draw_mirror(app, ui, SDL_FRect{0, 0, ui.width(), ui.height()}, o, false, false);
    x.submit_stereo();
}

} // namespace gt3r::race_screen

#endif // GT3R_XR
