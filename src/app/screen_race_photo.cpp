// Photo mode (ours, race/photo_camera.h): from a paused replay's Select list, a free camera round
// the frozen moment. Nothing is simulated meanwhile; Circle goes back to the list, the replay's own
// camera coming back as it was. The HUD, the mark and the menus are not drawn: the viewfinder
// (corner brackets and the focus point in the frame's centre), the grid when chosen, the camera menu when shown (Select; touch: MENU) and the controls' legend
// while the camera moves. Once it has been still for a second the picture is built
// (race/photo_camera.h): kPhotoSamples renders, one a frame, each at its sample's time while the
// shutter is open and nudged by its fraction of a pixel, into View3D's running mean
// (DrawOptions::accumulate), which is shown as it builds and then kept; a line along the bottom
// shows how far it has come. Meanwhile the wheels' spin blur (race/wheel_fx.h, the showroom rims'
// copies too) is off, since the renders turn the wheels themselves, and the dynamic resolution
// holds its scale (every render the same size). Any input starts again.
//
// The crosshair: Square held (touch: a finger held on the crosshair) for kFocusHold seconds, as a
// camera's autofocus, checks its ray (focus_photo, photo_hit) once per hold: the depth of field
// focuses where it hits the course or a car, the focus point flashes red (kFocusFlashes times),
// and the car it hit, or none, decides what the camera does while the shutter is open
// (resolve_photo_follow, race::photo_follow_mode: Roadside pans with that car, or with none stays
// put; Fixed to car rides with it, or with none the followed car). Both then stay wherever the
// camera goes until the next focus; nothing is checked otherwise. The camera menu: its follow setting and the shutter (D-pad Up / Down a row, Left
// / Right its value; touch: < and >): Camera (the follow setting), Shutter, Aspect (the frame's
// shape, 16:9 or 4:3: the picture rendered at it, centred, black round it, and saved so),
// Orientation (landscape, or portrait: the frame turned upright, 9:16 or 3:4) and Grid
// (rule of thirds, square diagonals or golden triangle over the frame, never in the photo). Triangle held with L1 / R1 rolls (touch: ROLL L / ROLL R);
// Triangle pressed alone goes back, as Circle (Android's back button).
//
// Cross saves the photo (save_photo), at once when it is finished, else when it is (a move first
// cancels that): the 3D picture alone, at the size it was rendered, as
// screenshots/gt3r-photo-<date>-<time>.png in the save folder (F12's folder); no crosshair, menu,
// legend or line.
//
// Photo mode draws every car at full detail whatever the settings say (photo_car_models); leaving
// it puts back the cars' detail, and the replays' own depth of field, as the settings have them.
//
// The depth of field is the accumulation's too (race::photo_lens): each render looks through its
// own point of a thin lens (the sample's, over the aperture's radius: the 35 mm equivalent focal
// length of the view over the menu's f-number) at the focus distance, so the mean is sharp there
// and blurs before and behind it as a real lens of that focal length and f-number would. The
// replays' own depth of field pass (a post process) is off in photo mode.
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"
#include "base/file.h"
#include "formats/png_writer.h"

namespace gt3r::race_screen {

namespace {

constexpr float kDeadZone = 0.15f; // as the camera editor's sticks

float stick(const PadState& p, PadAxis a) {
    const float v = p.axis_f(a);
    return std::fabs(v) < kDeadZone ? 0.0f : (v - std::copysign(kDeadZone, v)) / (1 - kDeadZone);
}

// The pad's part (Triangle held: L1 / R1 roll instead of zooming), and the touch roll buttons'.
race::PhotoInput photo_input(const PadState& p, const TouchControls& touch, bool fast) {
    race::PhotoInput in;
    in.move_x = stick(p, PadAxis::LX);
    in.move_y = -stick(p, PadAxis::LY); // (the stick's up is 0)
    in.turn_x = stick(p, PadAxis::RX);
    in.turn_y = -stick(p, PadAxis::RY);
    in.rise = (p.down(PadButton::R2) ? 1.0f : 0.0f) - (p.down(PadButton::L2) ? 1.0f : 0.0f);
    const float lr = (p.down(PadButton::R1) ? 1.0f : 0.0f) - (p.down(PadButton::L1) ? 1.0f : 0.0f);
    (p.down(PadButton::Triangle) ? in.roll : in.zoom) = lr;
    in.roll += (touch.action_held(TouchAction::RollRight) ? 1.0f : 0.0f) - (touch.action_held(TouchAction::RollLeft) ? 1.0f : 0.0f);
    in.roll = std::clamp(in.roll, -1.0f, 1.0f);
    in.fast = fast;
    return in;
}

std::string shutter_name(int i) { return "1/" + std::to_string(race::kShutterDenominators[i]) + " s"; }

std::string fstop_name(int i) {
    char b[16];
    std::snprintf(b, sizeof b, "f/%g", double(race::kFStops[i]));
    return b;
}

// A line `t` units thick (UI units, the renderer's own).
void thick_line(SDL_Renderer* r, float x0, float y0, float x1, float y1, float t, Color c) {
    const float dx = x1 - x0, dy = y1 - y0, len = std::hypot(dx, dy);
    if (len < 1e-4f) return;
    const float nx = -dy / len * t / 2, ny = dx / len * t / 2, ex = dx / len * t / 2, ey = dy / len * t / 2;
    const SDL_FColor fc{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
    const SDL_Vertex v[4] = {{{x0 + nx - ex, y0 + ny - ey}, fc, {0, 0}},
                             {{x0 - nx - ex, y0 - ny - ey}, fc, {0, 0}},
                             {{x1 - nx + ex, y1 - ny + ey}, fc, {0, 0}},
                             {{x1 + nx + ex, y1 + ny + ey}, fc, {0, 0}}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

// A rounded corner bracket of the viewfinder: arms `arm` long from the corner (`sx`, `sy` the way
// each runs: +1 right / down, -1 left / up), the corner rounded at radius `rad`.
void bracket(SDL_Renderer* r, float x, float y, float sx, float sy, float arm, float rad, float t, Color c) {
    thick_line(r, x + sx * rad, y, x + sx * arm, y, t, c);
    thick_line(r, x, y + sy * rad, x, y + sy * arm, t, c);
    const float cx = x + sx * rad, cy = y + sy * rad; // the arc's centre
    constexpr int kSteps = 6;
    for (int i = 0; i < kSteps; ++i) { // from the vertical arm's end round to the horizontal one's
        const float a0 = 1.5707964f * float(i) / kSteps, a1 = 1.5707964f * float(i + 1) / kSteps;
        thick_line(r, cx - sx * rad * std::cos(a0), cy - sy * rad * std::sin(a0), cx - sx * rad * std::cos(a1),
                   cy - sy * rad * std::sin(a1), t, c);
    }
}

} // namespace

bool RaceScreen::photo_allowed() const { return replaying_ && !two_player_ && !online_ && !logger_mode_; }

render::Vec3 RaceScreen::photo_anchor() const {
    const sim::VehicleState s = field_ && replay_focus_ >= 0 && replay_focus_ < field_->opponents()
                                    ? interpolate(field_->opponent_prev_state(replay_focus_), field_->opponent_state(replay_focus_),
                                                  step_.alpha())
                                    : render_state_;
    return {s.position.x, s.position.y, s.position.z};
}

void RaceScreen::open_photo(App& app) {
    if (!photo_allowed()) return;
#if GT3R_XR
    if (vr_stereo(app, false)) {
        toast("Photo mode is not available in VR");
        return;
    }
#else
    (void)app;
#endif
    photo_car_models(true); // (every car at full detail)
    photo_.emplace();
    photo_alpha_ = step_.alpha(), photo_state_ = render_state_;
    photo_focus_.reset(), photo_fast_ = false;
    photo_focus_hold_ = photo_focus_flash_ = photo_input_idle_ = 0;
    photo_menu_row_ = 0; // (the aspect and grid stay as last chosen)
    photo_buttons_ = app.input().pad(0).buttons; // (a button held on the way in is no press)
    photo_triangle_rolled_ = true;               // (nor its release a back)
    photo_->begin(camera_.eye(), camera_.view(), camera_.fovy(), photo_anchor());
    photo_->set_ground([this](float x, float z, float y) -> std::optional<float> {
        if (!track_) return std::nullopt;
        const sim::SurfaceHit h = track_->query().probe(x, z, y);
        if (!h.hit || h.normal.y < 0.05f) return std::nullopt;
        return h.height;
    });
    apply_photo_view();
    photo_mode_ = race::PhotoMode::Free, photo_target_ = photo_followed(), photo_focus_car_ = -1;
    resolve_photo_follow(); // (no focus yet: Roadside stays put, Fixed to car rides the followed car)
    LOG_INFO("Photo mode: on");
}

void RaceScreen::close_photo() {
    photo_save_ = false;
    if (!photo_) return;
    photo_car_models(false); // (the cars' detail as set again)
    camera_.clear_free_view();
    photo_.reset();
    if (app_) app_->hold_render_scale(false);
    if (view_) view_->reset_accumulation();
    LOG_INFO("Photo mode: off");
}

// Photo mode's cars: on, every car drawn at full detail whatever [video] full_car_detail says (a
// flag on each model: nothing is built again); off, the detail as set.
void RaceScreen::photo_car_models(bool on) {
    const bool full = on || full_car_detail_;
    if (car_visual_) car_visual_->set_full_detail(full);
    for (const std::shared_ptr<CarVisual>& v : opp_visuals_)
        if (v) v->set_full_detail(full);
}

float RaceScreen::photo_focus_distance() const {
    return photo_focus_ ? *photo_focus_ : render::length(photo_anchor() - photo_->eye());
}

void RaceScreen::apply_photo_view() { camera_.set_free_view(photo_->eye(), photo_->view(), photo_->fovy(), photo_focus_distance()); }

RaceScreen::PhotoHit RaceScreen::photo_hit() const {
    PhotoHit hit;
    const render::Vec3 o = photo_->eye(), d = photo_->forward(); // (the crosshair: the view's centre, roll or not)
    float best = course_scene_ && !course_scene_->occluder().empty() ? course_scene_->occluder().nearest(o, d, PhotoHit::kMax)
                                                                       : PhotoHit::kMax;
    // The cars as the picture shows them (their bodies in their own frames), nearer than that.
    auto test = [&](const CarVisual* v, const sim::VehicleState& s, const sim::VehicleSetup& su, int car) {
        if (!v || v->occluder().empty()) return;
        const render::Vec3 p{s.position.x, s.position.y, s.position.z}, op = p - o;
        const float along = render::dot(op, d);
        if (along < 0 || along > best + 6 || render::length(op - d * along) > 6) return; // (a car is shorter than 12 m)
        const render::Mat4 to_body = render::inverse(v->body_matrix(s, su));
        const render::Vec3 a = to_body.transform_point(o), b = to_body.transform_point(o + d * best);
        const float len = render::length(b - a);
        if (len < 1e-3f) return;
        const float t = v->occluder().nearest(a, (b - a) * (1.0f / len), len);
        if (t < len) best = best * t / len, hit.car = car;
    };
    if (vehicle_) test(car_visual_.get(), photo_state_, vehicle_->setup(), 0);
    if (const RaceField* f = exact_field())
        for (int i = 0; i < f->opponents() && size_t(i) < opp_visuals_.size(); ++i)
            test(opp_visuals_[size_t(i)].get(), opponent_view_state(i), f->opponent_setup(i), i + 1);
    if (best < PhotoHit::kMax) hit.distance = best;
    return hit;
}

int RaceScreen::photo_followed() const {
    const RaceField* f = exact_field();
    return f && field_ && replay_focus_ >= 0 && replay_focus_ < f->opponents() ? replay_focus_ + 1 : 0;
}

void RaceScreen::resolve_photo_follow() {
    const int car = photo_focus_car_;
    const race::PhotoMode mode = race::photo_follow_mode(photo_->follow(), car >= 0);
    const int target = car >= 0 ? car : photo_followed();
    if (mode == photo_mode_ && target == photo_target_) return;
    photo_mode_ = mode, photo_target_ = target;
    LOG_INFO("Photo mode: %s, %s car %d", race::photo_follow_name(photo_->follow()),
             photo_mode_ == race::PhotoMode::Pan ? "panning with" : photo_mode_ == race::PhotoMode::Fixed ? "fixed to" : "still, no",
             car);
}

void RaceScreen::focus_photo() {
    const PhotoHit hit = photo_hit();
    if (!hit.distance) {
        toast("Nothing under the crosshair to focus on");
        return;
    }
    photo_focus_ = *hit.distance;
    photo_focus_car_ = hit.car; // (what the camera does follows from it: a car or none)
    resolve_photo_follow();
    photo_->wake(), photo_save_ = false; // (a new photo, focused there)
    apply_photo_view();
    photo_focus_flash_ = kFocusFlashTime; // (the focus point flashes red: focused)
    char t[48];
    std::snprintf(t, sizeof t, "Focus %.1f m%s", double(*hit.distance), hit.car >= 0 ? " (a car)" : "");
    toast(t);
}

void RaceScreen::update_photo(App& app, double dt) {
    const MenuInput& m = app.menu();
    TouchControls& touch = app.touch().controls();
    if (m.pressed(PadButton::Circle)) {
        close_photo(); // back to the Select list it was opened from
        return;
    }
    // The pad's own buttons for the D-pad, L3 and Select, not the menu's: those count the left
    // stick as a D-pad too, and the stick moves the camera here.
    const u16 buttons = app.input().pad(0).buttons, edge = u16(buttons & ~photo_buttons_);
    const u16 let_go = u16(photo_buttons_ & ~buttons);
    photo_buttons_ = buttons;
    auto pressed = [&](PadButton b) { return (edge & (1u << unsigned(b))) != 0; };
    auto down = [&](PadButton b) { return (buttons & (1u << unsigned(b))) != 0; };
    // Triangle: held with L1 / R1 it rolls; pressed and let go alone it goes back as Circle does
    // (Android's back button is Triangle).
    if (pressed(PadButton::Triangle)) photo_triangle_rolled_ = false;
    if (down(PadButton::Triangle) && (down(PadButton::L1) || down(PadButton::R1))) photo_triangle_rolled_ = true;
    if ((let_go & (1u << unsigned(PadButton::Triangle))) && !photo_triangle_rolled_) {
        close_photo();
        return;
    }
    if (pressed(PadButton::L3)) {
        photo_fast_ = !photo_fast_;
        toast(photo_fast_ ? "Fast move on" : "Fast move off");
    }
    // The camera menu: Select / MENU shows or hides it; Up / Down its row, Left / Right the row's
    // value (touch: each row's < and >). Each change is a new photo.
    if (pressed(PadButton::Select) || touch.action_pressed(TouchAction::PhotoMenu)) photo_menu_ = !photo_menu_;
    // Each row's step this frame: touch's < / >, then the pad's Left / Right on the chosen row.
    int step[kPhotoMenuRows] = {};
    for (int i = 0; i < kPhotoMenuRows; ++i)
        step[i] = touch.action_pressed(kPhotoMenuActions[i][1]) ? 1 : touch.action_pressed(kPhotoMenuActions[i][0]) ? -1 : 0;
    if (photo_menu_) {
        if (pressed(PadButton::Down)) photo_menu_row_ = (photo_menu_row_ + 1) % kPhotoMenuRows;
        if (pressed(PadButton::Up)) photo_menu_row_ = (photo_menu_row_ + kPhotoMenuRows - 1) % kPhotoMenuRows;
        step[photo_menu_row_] += pressed(PadButton::Right) ? 1 : pressed(PadButton::Left) ? -1 : 0;
    }
    auto cycle = [](auto v, int dir, int n) { return decltype(v)(((int(v) + dir) % n + n) % n); };
    if (step[0]) photo_->step_follow(step[0]), resolve_photo_follow(); // (from the last focus's car)
    if (step[1]) photo_->step_shutter(step[1]);
    if (step[2]) { // the aperture: a new photo
        photo_fstop_ = std::clamp(photo_fstop_ + step[2], 0, race::kFStopCount - 1);
        photo_->wake();
    }
    if (step[3]) { // a new frame shape: a new photo (its size changes too)
        photo_aspect_ = cycle(photo_aspect_, step[3], int(race::PhotoAspect::Count));
        photo_->wake();
    }
    if (step[4]) { // the frame turned: a new photo too
        photo_orientation_ = cycle(photo_orientation_, step[4], int(race::PhotoOrientation::Count));
        photo_->wake();
    }
    if (step[5]) photo_grid_ = cycle(photo_grid_, step[5], int(race::PhotoGrid::Count)); // (over it only)
    if (step[0] || step[1] || step[2] || step[3] || step[4]) photo_save_ = false;
    touch.set_photo_menu(photo_menu_, photo_menu_values());
    // Touch: a finger on the open screen drags the view along with it (the picture follows the
    // finger: as far as its field of view across the screen's height), two pinch the zoom.
    const TouchGesture g = touch.take_gesture();
    const float per_unit = photo_->fovy() / std::max(1.0f, app.ui().height());
    bool moved = g.any() && photo_->aim(g.drag_x * per_unit, g.drag_y * per_unit, g.pinch);
    moved = photo_->update(photo_input(app.input().pad(0), touch, photo_fast_), dt) || moved;
    if (moved) {
        apply_photo_view();
        photo_save_ = false; // (a new photo: saved only when asked again)
    }
    // Autofocus: Square (touch: the crosshair) held kFocusHold seconds focuses, once per hold.
    if (down(PadButton::Square) || touch.action_held(TouchAction::Focus)) {
        const bool was_short = photo_focus_hold_ < kFocusHold;
        photo_focus_hold_ += dt;
        if (was_short && photo_focus_hold_ >= kFocusHold) focus_photo();
    } else {
        photo_focus_hold_ = 0;
    }
    photo_focus_flash_ = std::max(0.0, photo_focus_flash_ - dt);
    if (m.pressed(PadButton::Cross)) {
        photo_save_ = true;
        if (!view_ || view_->accumulated() < race::kPhotoSamples) toast("The photo is saved when it is finished");
    }
    // Controller: the camera menu hides once the photo is finished and nothing has been pressed or
    // moved for kMenuIdleHide seconds (touch: the controls fade, its rows with them).
    photo_input_idle_ = edge != 0 || moved || g.any() ? 0.0 : photo_input_idle_ + dt;
    const bool touch_shown = touch.visible() && touch.layout() == TouchLayout::Photo;
    const bool finished = photo_->still() && view_ && view_->accumulated() >= race::kPhotoSamples;
    if (photo_menu_ && !touch_shown && finished && photo_input_idle_ >= kMenuIdleHide) photo_menu_ = false;
    app.hold_render_scale(photo_->still());
}

RaceScreen::PhotoRender RaceScreen::photo_render() {
    using A = render::DrawOptions::Accumulate;
    if (!photo_ || !view_) return {};
    if (!photo_->still()) { // (moving: a new photo once it stops)
        view_->reset_accumulation();
        return {};
    }
    const int n = view_->accumulated();
    if (n >= race::kPhotoSamples) return {A::Show, {}};
    const float length = photo_->shutter_seconds() / float(step_.step()); // (in ticks)
    return {A::Add, race::photo_sample(n, race::kPhotoSamples, race::photo_shutter(photo_moment(), length, photo_earliest()))};
}

void RaceScreen::record_photo_history() {
    if (!replaying_ || two_player_ || online_ || logger_mode_ || !vehicle_) return;
    std::vector<sim::VehicleState> cars{vehicle_->state()};
    if (const RaceField* f = exact_field())
        for (int i = 0; i < f->opponents(); ++i) cars.push_back(f->opponent_state(i));
    photo_history_.push(photo_ticks_++, std::move(cars));
}

sim::VehicleState RaceScreen::photo_car_at(int car, float t) const {
    if (photo_history_.span() >= 1)
        if (std::optional<sim::VehicleState> s = photo_history_.at(size_t(car), t)) return *s;
    const float a = std::clamp(t + 1.0f, 0.0f, 1.0f);
    const RaceField* f = exact_field();
    if (car <= 0 || !f || car > f->opponents()) return interpolate(prev_, vehicle_->state(), a);
    return interpolate(f->opponent_prev_state(car - 1), f->opponent_state(car - 1), a);
}

sim::VehicleState RaceScreen::opponent_view_state(int i) const {
    if (photo_time_) return photo_car_at(i + 1, *photo_time_);
    const RaceField* f = exact_field();
    return interpolate(f->opponent_prev_state(i), f->opponent_state(i), frame_alpha());
}

float RaceScreen::photo_focal_mm() const {
    const float aspect = frame_rect_.w > 0 && frame_rect_.h > 0 ? frame_rect_.w / frame_rect_.h
                                                                 : race::photo_frame_ratio(photo_aspect_, photo_orientation_);
    return race::photo_focal_mm(photo_->fovy(), aspect);
}

void RaceScreen::begin_photo_render(const race::PhotoSample& sample) {
    const float t = sample.t;
    photo_time_ = t;
    alpha_at_ = std::clamp(t + 1.0f, 0.0f, 1.0f); // (the sparks and the ghost: within the paused tick only)
    render_state_ = photo_car_at(0, t);
    // The camera at the render's time by the mode, then through the render's point of the lens.
    race::PhotoView v{photo_->eye(), photo_->view()};
    if (photo_mode_ != race::PhotoMode::Free)
        v = race::photo_view(photo_mode_, photo_->eye(), photo_->view(), race::car_matrix(photo_car_at(photo_target_, photo_moment())),
                             race::car_matrix(photo_car_at(photo_target_, t)));
    const float focus = photo_focus_distance();
    const float r = race::photo_aperture_radius(photo_focal_mm(), race::kFStops[photo_fstop_]);
    const race::PhotoLens lens = race::photo_lens(v.eye, v.view, sample.lens_x * r, sample.lens_y * r, focus);
    camera_.set_free_view(lens.eye, lens.view, photo_->fovy(), focus);
    photo_shear_ = lens.shear;
}

void RaceScreen::end_photo_render() {
    photo_time_.reset(), alpha_at_.reset();
    photo_shear_ = {};
    render_state_ = photo_state_;
    apply_photo_view();
}

bool RaceScreen::spin_blur_on() const { return !(photo_ && photo_->still()); }

std::vector<std::string> RaceScreen::photo_menu_values() const {
    return {race::photo_follow_name(photo_->follow()), shutter_name(photo_->shutter()), fstop_name(photo_fstop_),
            race::photo_aspect_name(photo_aspect_), race::photo_orientation_name(photo_orientation_),
            race::photo_grid_name(photo_grid_)};
}

std::string RaceScreen::photo_status() const {
    std::string s = race::photo_follow_name(photo_->follow());
    s += ", " + shutter_name(photo_->shutter()) + ", " + fstop_name(photo_fstop_);
    char mm[16];
    std::snprintf(mm, sizeof mm, " %.0f mm", double(photo_focal_mm()));
    s += mm;
    s += ", " + std::string(race::photo_aspect_name(photo_aspect_));
    if (photo_orientation_ == race::PhotoOrientation::Portrait) s += " portrait";
    if (photo_focus_) {
        char f[24];
        std::snprintf(f, sizeof f, ", focus %.1f m", double(*photo_focus_));
        s += f;
    }
    if (photo_fast_) s += ", fast";
    return s;
}

void RaceScreen::save_photo(App& app) {
    photo_save_ = false;
    std::vector<u32> px;
    int w = 0, h = 0;
    if (!view_ || !view_->read_picture(px, w, h)) {
        toast("Could not save the photo");
        return;
    }
    const std::string dir = app.data_dir() + "screenshots";
    SDL_CreateDirectory(dir.c_str());
    SDL_Time now = 0;
    SDL_DateTime t{};
    char name[64] = "gt3r-photo.png";
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &t, true))
        std::snprintf(name, sizeof name, "gt3r-photo-%04d%02d%02d-%02d%02d%02d-%03d.png", t.year, t.month, t.day, t.hour,
                      t.minute, t.second, t.nanosecond / 1000000);
    const std::string path = dir + "/" + name;
    if (write_host_file(path, encode_png(u32(w), u32(h), px))) {
        LOG_INFO("Photo saved: %s (%dx%d)", path.c_str(), w, h);
        toast("Photo saved");
    } else {
        toast("Could not save the photo");
    }
}

void RaceScreen::render_photo(Ui& ui) {
    const float W = ui.width(), H = ui.height();
    // (On-screen controls: their own buttons, menu and fading, and the touch legend.)
    const TouchControls* tc = app_ ? &app_->touch().controls() : nullptr;
    const bool touch = tc && tc->visible() && tc->layout() == TouchLayout::Photo;
    const bool finished = photo_->still() && view_ && view_->accumulated() >= race::kPhotoSamples;
    // The frame the photo is rendered in (its aspect), as render() last drew it.
    const SDL_FRect fr = frame_rect_.w > 0 ? frame_rect_ : SDL_FRect{0, 0, W, H};
    SDL_Renderer* r = ui.renderer();
    // The viewfinder in single-point focus: rounded brackets round the frame's middle quarter (half
    // its width and half its height) and the focus point, a circle with a cross, in its centre; and
    // the grid. Thin light lines, no outline. Gone once the photo is finished (touch: as the
    // controls fade, the focus point being the focus button); the grid goes with them.
    const float cross_a = touch ? std::max(tc->alpha(), photo_focus_flash_ > 0 ? 1.0f : 0.0f)
                          : finished && photo_focus_flash_ <= 0 ? 0.0f
                                                                : 1.0f;
    if (cross_a > 0 && photo_grid_ != race::PhotoGrid::Off) // (never in the photo)
        for (const auto& l : race::photo_grid_lines(photo_grid_, fr.w / std::max(fr.h, 1.0f))) {
            const float x0 = fr.x + l[0] * fr.w, y0 = fr.y + l[1] * fr.h, x1 = fr.x + l[2] * fr.w, y1 = fr.y + l[3] * fr.h;
            thick_line(r, x0, y0, x1, y1, 0.6f, Color{240, 242, 246, u8(150 * cross_a)});
        }
    if (cross_a > 0) {
        const float cx = fr.x + fr.w / 2, cy = fr.y + fr.h / 2, cr = 10, plus = 4.5f;
        const float bw = fr.w / 2, bh = fr.h / 2, arm = std::min(bw, bh) * 0.22f, rad = arm * 0.35f;
        const float x0 = cx - bw / 2, x1 = cx + bw / 2, y0 = cy - bh / 2, y1 = cy + bh / 2;
        // Focused: the marks red, on and off kFocusFlashes times (the first half of each cycle red).
        const double flashed = kFocusFlashTime - photo_focus_flash_, cycle = kFocusFlashTime / kFocusFlashes;
        const bool red = photo_focus_flash_ > 0 && std::fmod(flashed, cycle) < cycle / 2;
        const Color lite = red ? Color{235, 40, 40, u8(255 * cross_a)} : Color{240, 242, 246, u8(230 * cross_a)};
        const Color point = lite;
        constexpr float kLine = 1.2f;
        bracket(r, x0, y0, 1, 1, arm, rad, kLine, lite);
        bracket(r, x1, y0, -1, 1, arm, rad, kLine, lite);
        bracket(r, x0, y1, 1, -1, arm, rad, kLine, lite);
        bracket(r, x1, y1, -1, -1, arm, rad, kLine, lite);
        constexpr int kSegs = 32;
        for (int i = 0; i < kSegs; ++i) {
            const float a0 = 6.2831853f * float(i) / kSegs, a1 = 6.2831853f * float(i + 1) / kSegs;
            thick_line(r, cx + cr * std::cos(a0), cy + cr * std::sin(a0), cx + cr * std::cos(a1), cy + cr * std::sin(a1), kLine, point);
        }
        thick_line(r, cx - plus, cy, cx + plus, cy, kLine, point);
        thick_line(r, cx, cy - plus, cx, cy + plus, kLine, point);
    }
    // The camera menu (touch: the controls' own rows).
    if (photo_menu_ && !touch) {
        static constexpr const char* kNames[kPhotoMenuRows] = {"Camera", "Shutter", "Aperture", "Aspect", "Orientation", "Grid"};
        const std::vector<std::string> values = photo_menu_values();
        const float mw = 240, x = W / 2 - mw / 2, y = 14;
        ui.fill(x, y, mw, 26 + 18.0f * kPhotoMenuRows, Color{0, 0, 0, 170});
        ui.text(x + 8, y + 6, "CAMERA MENU   (Select hides)", colors::kAccent);
        for (int i = 0; i < kPhotoMenuRows; ++i) {
            const float ry = y + 24 + float(i) * 18;
            if (i == photo_menu_row_) ui.fill(x + 4, ry - 3, mw - 8, 15, colors::kHighlight);
            ui.text(x + 8, ry, kNames[i], colors::kText);
            ui.text(x + 72, ry, "< " + values[size_t(i)] + " >", colors::kText);
        }
    }
    if (photo_->still()) { // the picture alone, and while it builds a line along its bottom
        const int n = view_ ? std::min(view_->accumulated(), race::kPhotoSamples) : race::kPhotoSamples;
        if (n < race::kPhotoSamples) {
            ui.fill(0, H - 3, W * float(n) / float(race::kPhotoSamples), 3, colors::kAccent);
            // What this photo took from the crosshair (above the line, left of the touch buttons).
            std::string what = photo_mode_ == race::PhotoMode::Pan     ? "Panning with the car"
                               : photo_mode_ == race::PhotoMode::Fixed ? "Fixed to the car"
                                                                       : "Still camera";
            if (photo_focus_) {
                char f[32];
                std::snprintf(f, sizeof f, ", focus %.1f m", double(*photo_focus_));
                what += f;
            }
            const float w = std::ceil(Ui::text_width(what)) + 12;
            const float x = touch ? W / 2 - w / 2 : 8;
            ui.fill(x, H - 22, w, 16, Color{0, 0, 0, 120});
            ui.text(x + 6, H - 18, what, colors::kText);
        } else if (!photo_save_ && !touch) { // (not in the saved picture)
            const char* hint = "Cross save photo";
            const float w = std::ceil(Ui::text_width(hint)) + 16;
            ui.fill(W - w - 8, H - 26, w, 18, Color{0, 0, 0, 120});
            ui.text(W - w, H - 21, hint, colors::kText);
        }
        return;
    }
    const std::string status = "PHOTO MODE   " + photo_status();
    const char* l1 = touch ? "Stick move   Drag aim   Pinch zoom   Hold crosshair focus"
                           : "L stick move (L3 fast)   R stick turn   L2/R2 height   L1/R1 zoom   Triangle+L1/R1 roll";
    const char* l2 = touch ? "UP/DOWN height   ROLL   MENU camera, shutter   SAVE   BACK"
                           : "Hold Square focus   Select camera menu   Cross save   Circle / Triangle back";
    const float w = std::ceil(std::max({Ui::text_width(l1), Ui::text_width(l2), Ui::text_width(status)})) + 16;
    const float top = H - (touch ? 128.0f : 66.0f); // (touch: above the stick and the buttons)
    ui.fill(W / 2 - w / 2, top, w, 54, Color{0, 0, 0, 170});
    ui.text(W / 2 - w / 2 + 8, top + 5, status, colors::kAccent);
    ui.text(W / 2 - w / 2 + 8, top + 23, l1, colors::kText);
    ui.text(W / 2 - w / 2 + 8, top + 41, l2, colors::kText);
}

} // namespace gt3r::race_screen
