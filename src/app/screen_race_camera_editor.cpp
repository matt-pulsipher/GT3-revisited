// The camera editor (Settings -> Cameras, app/camera_editor.h) on the race screen: the arcade's
// Normal race on the course of the player's last race with its car ([cameras] preview_*; Trial
// Mountain and the event's car before any race), driven by the computer for a few seconds past
// the start signal, then frozen: nothing more is simulated, heard or kept (a practice race,
// left without a result). The picture is drawn through the camera being edited, HUD and mirror
// included, with the editor over it; Save writes the camera to config.ini.
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"

namespace gt3r::race_screen {

namespace {

// The editor's presets (CameraEditor::kPresetNames), as built-in views — all but the bonnet,
// which is a mount of its own seated by the car's shape (race/hood_camera.h).
constexpr CameraMode kPresetModes[] = {CameraMode::Chase, CameraMode::ChaseFar, CameraMode::Bumper, CameraMode::Roof};

} // namespace

RaceScreen::RaceScreen(MenuLaunch l, CustomCamera edit, bool is_new) : RaceScreen(std::move(l)) {
    editor_ = std::make_unique<CameraEditor>(std::move(edit), is_new, [this](int i) {
        i = std::clamp(i, 0, CameraEditor::kPresets - 1);
        if (i == CameraEditor::kPresetBonnet) { // on the bonnet, the in-car look over the bodywork
            CustomCamera c;
            c.mount = CustomCamera::Mount::Hood;
            c.offset = {};
            c.fov = 55;
            c.car = CustomCamera::OwnCar::Ahead;
            c.incar_hud = c.mirror = true;
            return c;
        }
        const sim::VehicleSetup setup = vehicle_ ? vehicle_->setup() : sim::VehicleSetup{};
        return camera_.preset(kPresetModes[i], setup);
    });
    preset_.autodrive = true; // (the computer drives to the moment shown)
    preset_.music = preset_.ghost = false;
    preset_.hud = true;
    preset_.warp = 0;
}

void RaceScreen::start_camera_editor(App& app) {
    // The moment: kEditorSceneTime past the start signal (after a standing start's hold).
    const double hold = field_ ? std::max(0.0, -field_->race_time()) : std::max(0.0, tt_hold_);
    warp(app, hold + kEditorSceneTime);
    render_state_ = prev_ = vehicle_->state();
    camera_.set_intro(false, 1.0f);
    camera_.set_look_back(false);
    camera_.set_look(0, 0);
    editor_->begin();
    apply_editor_camera();
    bind_hood(camera_, hood_bound_, car_visual_.get());
    camera_.snap(render_state_);
    camera_.update(render_state_, vehicle_->setup(), PadState{}, 1.0 / 60.0);
    silence(app);
    LOG_INFO("Camera editor: %s, the race at %.1f s", editor_->camera().name.c_str(), field_ ? field_->race_time() : race_time_);
}

void RaceScreen::apply_editor_camera() {
    const CustomCamera& c = editor_->camera();
    camera_.set_custom_cameras({c});
    camera_.set_custom_id(c.id);
    if (camera_.mode() != CameraMode::Custom) camera_.set_mode(CameraMode::Custom);
}

void RaceScreen::update_camera_editor(App& app, double dt) {
    switch (editor_->update(app, editor_scene(), dt)) {
    case CameraEditor::Result::Save: {
        std::vector<CustomCamera> cams = load_custom_cameras(app.config());
        const CustomCamera& c = editor_->camera();
        auto it = std::find_if(cams.begin(), cams.end(), [&](const CustomCamera& k) { return k.id == c.id; });
        if (it != cams.end()) *it = c;
        else cams.push_back(c);
        save_custom_cameras(app.config(), cams);
        app.save_config();
        LOG_INFO("Camera editor: saved %s", c.name.c_str());
        app.pop();
        return;
    }
    case CameraEditor::Result::Cancel: app.pop(); return;
    case CameraEditor::Result::None: break;
    }
    // The frozen race through the camera as it is now.
    apply_editor_camera();
    camera_.set_track_distance(track_->distance_of(render_state_.position));
    camera_.set_lap(lap_count());
    bind_hood(camera_, hood_bound_, car_visual_.get());
    camera_.update(render_state_, vehicle_->setup(), PadState{}, dt);
}

CameraEditor::Scene RaceScreen::editor_scene() const {
    CameraEditor::Scene s;
    s.frame = {frame_rect_.x, frame_rect_.y, frame_rect_.w, frame_rect_.h};
    const SDL_FRect m = mirror_rect(frame_rect_, false, &editor_->camera());
    s.mirror = {m.x, m.y, m.w, m.h};
    // The car's middle: 0.4 of its height over the body origin, from the driver's eye; the road under
    // the car at the ride height below the body origin, the eye 0.15 m above it at least.
    const sim::VehicleSetup setup = vehicle_ ? vehicle_->setup() : sim::VehicleSetup{};
    const float h = vehicle_ ? setup.height : 1.3f, q = setup.wheels[0].ride_height > 0 ? setup.wheels[0].ride_height : 0.13f;
    s.pivot = render::Vec3{0, h * 0.4f, 0} - camera_.driver_eye();
    s.min_y = 0.15f - q - camera_.driver_eye().y;
    return s;
}

void RaceScreen::remember_editor_scene(App& app) const {
    IniDocument& cfg = app.config();
    const std::string paint = std::to_string(preset_.paint);
    if (cfg.get("cameras", "preview_course") == preset_.course && cfg.get("cameras", "preview_car") == preset_.car &&
        cfg.get("cameras", "preview_paint") == paint)
        return;
    cfg.set("cameras", "preview_course", preset_.course);
    cfg.set("cameras", "preview_car", preset_.car);
    cfg.set("cameras", "preview_paint", paint);
    app.save_config();
}

} // namespace gt3r::race_screen

namespace gt3r {

std::unique_ptr<Screen> make_camera_editor_screen(App& app, race::CustomCamera camera, bool is_new) {
    const IniDocument& cfg = app.config();
    menu::flow::GameRequest rq;
    rq.kind = menu::flow::GameRequest::Kind::StartRace;
    rq.mode = "single_race";
    rq.page = "camera_editor";
    rq.practice = true; // (nothing goes into the GT Mode save)
    rq.difficulty = 1;
    rq.course = cfg.get("cameras", "preview_course").value_or("trialmt");
    rq.car = cfg.get("cameras", "preview_car").value_or("");
    rq.car_color = std::max(0, std::atoi(cfg.get("cameras", "preview_paint").value_or("0").c_str()));
    return std::make_unique<race_screen::RaceScreen>(race_screen::MenuLaunch{rq}, std::move(camera), is_new);
}

} // namespace gt3r
