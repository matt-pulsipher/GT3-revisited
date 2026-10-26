// Building a run: start() loads the course, car models, sounds and the vehicle (alone
// or in a RaceField with opponents); restart() / reset_run_state() put the car(s) back
// on the grid or a flying start; warp, the TV cameras, the saved-lap ghost.
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"

namespace gt3r::race_screen {

// GT3's "demonstration": the computer drives the course, watched from the TV cameras.
// Cross / Start / Circle returns to the quick menu; the run before it stays the one
// "replay" and "save" use.
TrackPose RaceScreen::flying_pose(float metres_before) const {
    TrackPose g = track_->grid_pose(0);
    const track::Track* rt = track_->runtime();
    std::vector<sim::Vec3> line = rt ? rt->racing_line(5, 1.0f) : std::vector<sim::Vec3>{};
    if (line.size() < 20) return g;
    float len = track_->length(), target = std::fmod(len - std::max(0.0f, metres_before) + len, len);
    size_t best = 0;
    float bd = 1e30f;
    for (size_t i = 0; i < line.size(); ++i) {
        float d = std::fabs(std::remainder(track_->distance_of(line[i]) - target, len));
        if (d < bd) bd = d, best = i;
    }
    size_t ahead = best;
    float acc = 0;
    while (acc < 10.0f) {
        size_t nx = (ahead + 1) % line.size();
        acc += std::hypot(line[nx].x - line[ahead].x, line[nx].z - line[ahead].z);
        ahead = nx;
        if (ahead == best) break;
    }
    float dx = line[ahead].x - line[best].x, dz = line[ahead].z - line[best].z, yaw = std::atan2(-dx, -dz);
    TrackPose p;
    p.position = line[best];
    sim::SurfaceHit h = track_->query().probe(p.position.x, p.position.z, p.position.y + 5.0f);
    if (h.hit) p.position.y = h.height;
    p.orientation = {std::cos(yaw * 0.5f), 0, std::sin(yaw * 0.5f), 0};
    LOG_INFO("Race: flying start %.0f m before the line on the racing line", metres_before);
    return p;
}

// Loads course, car, track and vehicle for the selected entries. `replay`
// (optional) is played back instead of driving.
void RaceScreen::start(App& app, const Replay* replay) {
    error_.clear();
    reset_session_best(); // a new visit: the runs and retries that follow share it
    qm_items_ = {};       // ... and its quick menus' item switches (menus §11)
    // Speed unit: the game data's rule (GtData::imperial: the NTSC-U data set shows mph,
    // like its menus; without a GT-mode session, mph on NTSC-U discs); [race] units = kmh | mph overrides.
    imperial_ = Disc::current_db_suffix() == "_us";
    if (game::GtModeSession* gs = game::gt_mode_session()) imperial_ = gs->gt().data().imperial;
    if (auto u = app.config().get("race", "units")) imperial_ = *u != "kmh";
    env_cube_on_ = app.config().get("video", "car_reflections").value_or("on") != "off";
    if (SDL_getenv("GT3R_NO_ENV")) env_cube_on_ = false;
    shadows_on_ = app.config().get("video", "car_shadows").value_or("on") != "off";
    // [video] original_res = on: the 3D scene at the PS2's 640x448 in a 4:3 frame,
    // upscaled with [video] original_filter = nearest (default) | bilinear.
    original_res_ = app.config().get("video", "original_res").value_or("off") == "on";
    original_nearest_ = app.config().get("video", "original_filter").value_or("nearest") != "bilinear";
    if (const char* e = SDL_getenv("GT3R_ORIGINAL_RES")) original_res_ = std::string(e) != "0", original_nearest_ = std::string(e) != "bilinear";
    respawn_all_ = respawn_mode(app) == "all";
    // [race] views = all (default: bumper, bumper without mirror, chase, far chase) | gt3
    // (GT3's two: bumper and chase).
    {
        bool gt3_views = to_lower(app.config().get("race", "views").value_or("all")) == "gt3";
        camera_.set_gt3_views(gt3_views);
        camera2_.set_gt3_views(gt3_views);
        bool shake = to_lower(app.config().get("race", "camera_shake").value_or("on")) != "off";
        camera_.set_shake(shake);
        camera2_.set_shake(shake);
    }
    if (!app.gpu()) {
        error_ = "The time trial needs the 3D renderer (GPU unavailable)";
        return;
    }
    const CourseEntry& ce = courses_[size_t(course_sel_)];
    const CarEntry& ca = cars_[size_t(car_sel_)];
    Uint64 t0 = SDL_GetPerformanceCounter();
    try {
        std::string path = course_path(*app.vfs(), ce.label);
        std::optional<Bytes> course = app.vfs()->read(path);
        // The car models' time of day from the course's Env0 flags: bit 19 eve,
        // else bit 15 night, else day.
        car_folder_ = "cars/day/";
        if (course) {
            try {
                course::CourseFile cf0 = course::CourseFile::parse(*course);
                ByteSpan env = cf0.section(*course, course::kSlotEnv);
                if (env.size() > 0x14 && std::memcmp(env.data(), "Env0", 4) == 0) {
                    u32 fl = rd32(env, 0x10);
                    car_folder_ = (fl & 0x80000) ? "cars/eve/" : (fl & 0x8000) ? "cars/night/" : "cars/day/";
                }
            } catch (const std::exception&) {
            }
        }
        if (!course) throw FormatError("missing " + path);
        car_ = car_setup_from_database(db_, ca.label);
        car_.spec = launch_spec_;
        // A saved replay brings the car its run used (parts, settings, tyres, aids).
        const bool replay_spec = replay && replay->spec;
        if (replay_spec) car_.spec = replay->spec;
        if (replay && !replay->car_model.empty()) launch_model_ = replay->car_model;
        // Options ASM / TCS off (arcade single race, free run, 2P battle): the car's own
        // ASCC / TCS are switched off (on keeps what its parts give it).
        if (!replay_spec && aids_options_apply_ && (!asm_on_ || !tcs_on_)) {
            std::optional<sim::CarSpec> sp;
            if (car_.spec) sp = *car_.spec;
            else sp = sim::build_car_spec(db_, ca.label);
            if (sp) {
                sim::apply_aid_options(sp->aids, asm_on_, tcs_on_);
                car_.spec = std::make_shared<const sim::CarSpec>(std::move(*sp));
            }
        }
        std::string model = launch_model_.empty() ? ca.model : launch_model_;
        std::optional<Bytes> car_file = read_car(app, model);
        if (!car_file || car_file->empty()) throw FormatError("car model cars/day/" + model + " is missing or empty");

        course_scene_.reset();
        car_visual_.reset();
        drop_vehicle();
        track_.reset();
        track_ = make_track(*app.vfs(), *course, ce.label);
        // The road under a point, for view mode 1's procedural cameras (none off the road or on a wall).
        camera_.set_ground([this](float x, float z, float y) -> std::optional<float> {
            if (!track_) return std::nullopt;
            const sim::SurfaceHit h = track_->query().probe(x, z, y);
            if (!h.hit || h.normal.y < 0.05f) return std::nullopt;
            return h.height;
        });
        course_scene_ = std::make_unique<CourseScene>(*app.gpu(), *course, ce.label, preset_.sheen);
        // GT mode: the garage car's bought wheels.
        std::optional<Bytes> wheel_file;
        if (from_menu_ || online_)
            [&](const auto& rq) {
                if constexpr (requires { rq.car_wheel; })
                    if (!rq.car_wheel.empty()) wheel_file = app.vfs()->read("wheel/" + rq.car_wheel);
            }(launch_.request);
        {
            render::Vec3 p0{0, -0.8f, 0}, p1{0, -1.5f, -6.0f};
            if (!car_camera_points(*car_file, p0, p1)) LOG_INFO("Race: %s has no camera points (fallbacks)", model.c_str());
            camera_.set_car_points(p0, p1);
            camera2_.set_car_points(p0, p1);
            LOG_INFO("Race: camera points in-car (%.2f %.2f %.2f) chase (%.2f %.2f %.2f)", p0.x, p0.y, p0.z, p1.x, p1.y, p1.z);
        }
        if (event_paint_id_ != 0xFFFFFFFFu)
            if (int k = colors_.index_of(model, event_paint_id_); k >= 0) preset_.paint = k;
        CarBodyMod body_mod = body_mod_for_car(db_, car_.label, preset_.paint);
        if (!wheel_file && !body_mod.wheel.empty()) wheel_file = app.vfs()->read("wheel/" + body_mod.wheel);
        car_visual_ = std::make_unique<CarVisual>(*app.gpu(), *car_file, model, car_.wheels, preset_.paint,
                                                  wheel_file ? ByteSpan(*wheel_file) : ByteSpan{}, body_mod);
        course_name_ = ce.name;
        // The start title shows the course table's name as it is: licence sections have their
        // own ("Grand Valley Test Section"), and a course without one shows no title.
        title_name_.clear();
        for (const data::Course& c : db_.courses())
            if (c.label == ce.label) title_name_ = c.name;
        course_label_for_dirt_ = ce.label;
        if (replay) {
            paint_seed_override_ = replay->paint_seed;
            race_event_ = replay->event, race_opponents_ = replay->opponents;
            if (!replay->car2.empty()) two_player_ = true, preset_.car2 = replay->car2;
        }
        race_mode_ = two_player_ || !race_event_.empty() || !race_opponents_.empty() || online_;
        if (!build_vehicle(app)) throw FormatError(error_);
        build_tv_cameras();
        gtrc_.reset();
        director_ = ReplayDirector(); // (not left pointing at the old script)
        camera_.set_replay_director(nullptr);
        // The pre-race shot's camera move (StartingCamera: cameras/start.cam, record 0).
        camera_.set_start_shot(std::nullopt);
        if (std::optional<Bytes> sc = app.vfs()->read("cameras/start.cam")) try {
                if (GtrcScript::is_gtrc(*sc))
                    if (GtrcScript st = GtrcScript::parse(*sc); !st.records.empty()) camera_.set_start_shot(st.records[0]);
            } catch (const std::exception& e) {
                LOG_WARN("Race: cameras/start.cam: %s", e.what());
            }
        try {
            course::CourseFile cf = course::CourseFile::parse(*course);
            ByteSpan g = cf.section(*course, course::kSlotReplayCameras);
            hud_.set_course_map(app.renderer(), cf.section(*course, course::kSlotCourseMap));
            // Billboard sprites (trees, spectators; slot 30).
            if (app.gpu()) billboards_.load(*app.gpu(), cf.section(*course, 30), ce.label);
            // The sun and its glare (Env0, slot 32).
            sun_.load(*app.vfs(), app.renderer(), cf.section(*course, 32));
            if (sun_.params().has_env && course_scene_) { // the course's own lit packets: its scene lights
                render::ItemLights cl;
                sun_.params().course_lights(cl);
                course_scene_->set_lights(cl);
            }
            point_lights_.load(cf.section(*course, course::kSlotRoad)); // night lamp list (point-light courses)
            if (app.gpu()) smoke_.load(*app.gpu(), cf.section(*course, 32), cf.section(*course, 26)); // tyre smoke / dust
            pylons_.load(app.gpu(), *course); // pylons / cones (GTRW list + slot 36 types)
            if (sun_.params().has_shadow) pylons_.set_shadow(sun_.params().shadow_rgb, sun_.params().shadow_alpha);
            if (app.gpu()) wet_.load(*app.gpu(), *app.vfs(), *course, cf.section(*course, 32)); // wet-road reflection
            if (point_lights_.active()) LOG_INFO("Race: %zu course lamps (point-light mode)", point_lights_.lamps().size());
            // Street / stadium lamps (slot 29, the lists picked by slot 2's visibility lists).
            std::optional<Bytes> flare = app.vfs()->read("race/flare.bin");
            lamps_.load(app.renderer(), cf.section(*course, 29), cf.section(*course, 32), cf.section(*course, 2),
                        flare ? ByteSpan(*flare) : ByteSpan{});
            car_lamps_.load(app.renderer(), cf.section(*course, 32), flare ? ByteSpan(*flare) : ByteSpan{}); // car lamp glows
            LOG_INFO("Race: car models from %s, car lamp glows %s", car_folder_.c_str(), car_lamps_.active() ? "on" : "off");
            if (sun_.params().enabled)
                LOG_INFO("Race: sun at elevation %.1f, azimuth %.1f, glare %.2f, style %d", sun_.params().elevation,
                         sun_.params().azimuth, sun_.params().glare_size, sun_.params().style);
            // Course ambience emitters (slot 35; absent on a few licence layouts).
            crss_ok_ = false;
            ByteSpan cs = cf.section(*course, 35);
            if (!cs.empty()) {
                sound::CrssData crss = sound::CrssData::parse(cs);
                const RaceTrack& tr = *track_;
                crss_.load(crss,
                           [&tr](float x, float y, float z, float* d) {
                               sim::SurfaceHit h = tr.query().probe(x, z, y + 3.0f);
                               if (!h.hit || h.segment < 0) return false;
                               *d = tr.distance_of({x, y, z});
                               return true;
                           },
                           tr.length());
                crss_ok_ = true;
            }
            if (GtrcScript::is_gtrc(g)) {
                gtrc_ = std::make_unique<GtrcScript>(GtrcScript::parse(g));
                director_ = ReplayDirector(gtrc_.get());
                camera_.set_replay_director(&director_);
                LOG_INFO("Race: %zu replay cameras (GTRC), %d lap variants", gtrc_->records.size(), gtrc_->variants());
            }
        } catch (const std::exception& e) {
            LOG_WARN("Race: replay cameras: %s", e.what());
        }
        LOG_INFO("Race: %s with %s (%s); track %s, vehicle %s, %zu scenery meshes, %zu triangles", ce.label.c_str(),
                 ca.label.c_str(), ca.model.c_str(), track_->backend_name(), vehicle_->backend_name(),
                 course_scene_->stats().meshes, course_scene_->stats().triangles);
    } catch (const std::exception& e) {
        error_ = e.what();
        LOG_WARN("Race: could not start: %s", e.what());
        course_scene_.reset();
        car_visual_.reset();
        drop_vehicle();
        track_.reset();
        return;
    }
    if (!hud_loaded_) {
        hud_.load(*app.vfs(), app.renderer());
        hud_loaded_ = true;
    }
    engine_.stop(app.audio().voices());
    std::string err;
    if (car_.sound_id && !engine_.load(*app.vfs(), car_.sound_id, car_.sound_variant, &err))
        LOG_WARN("Race: engine sound %u/%u: %s", car_.sound_id, car_.sound_variant, err.c_str());
    if (const data::ModCar* mc = db_.mod_car(car_.label); mc && mc->electric) {
        motor_ = make_motor_banks({mc->whine_order, mc->whine_level, mc->inverter_level});
        engine_.set_banks(motor_.whine, motor_.inverter);
        LOG_INFO("Race: %s is electric: synthesised motor sound", car_.label.c_str());
    } else {
        motor_ = {};
    }
    if (!sfx_.loaded()) {
        sfx_.load(*app.vfs());
        crss_player_.load(sfx_.banks(), sfx_.index());
    }
    // Wind (rn_01.es), in-car, whine and turbo for the followed cars.
    extra_.stop(sfx_, app.audio().voices());
    extra2_.stop(sfx_, app.audio().voices());
    extra_.reset();
    extra2_.reset();
    extra_.load(*app.vfs());
    if (two_player_) extra2_.load(*app.vfs());
    sound_car_ = car_sound_facts(car_.label);
    sound_car2_ = car_sound_facts(preset_.car2.empty() ? car_.label : preset_.car2);
    if (from_menu_ || online_) // GT mode: the garage car's fitted gearbox
        [&](const auto& rq) {
            if constexpr (requires { rq.racing_gearbox; })
                if (launch_spec_ && !attract_) sound_car_.whine = rq.racing_gearbox;
            if constexpr (requires { rq.racing_gearbox2; }) // 2P battle: player 2's garage car
                if (two_player_ && rq.car2_spec) sound_car2_.whine = rq.racing_gearbox2;
        }(launch_.request);
    LOG_INFO("Race: car sounds: C_d %.2f, gearbox whine %s, course ambience %s", sound_car_.cd,
             sound_car_.whine ? "on" : "off", crss_ok_ ? "on" : "none");
    if (!results_loaded_) results_loaded_ = results_.load(*app.vfs(), app.renderer()) || true;
    engine2_.stop(app.audio().voices());
    if (two_player_ && car_.sound_id) engine2_.load(*app.vfs(), car_.sound_id, car_.sound_variant);
    if (two_player_ && motor_.whine) engine2_.set_banks(motor_.whine, motor_.inverter);
    if (engine_.loaded()) LOG_INFO("Race: engine sound %05u variant %u (%s)", car_.sound_id, car_.sound_variant,
                                   EngineSound::variant_dir(car_.sound_variant).c_str());
    stage_ = Stage::Drive;
    ghost_.clear();
    ghost_time_ = -1;
    if (replay) {
        start_playback(*replay);
    } else {
        if (!race_mode_) load_saved_ghost();
        restart();
    }
    if (attract_) pick_attract_view(); // the demo's own view mode and target (presentation §G13)
    music_.reset(); // the race music starts with the first music_cues()
    // GT races, machine tests, licence tests, time trials and free runs open with their
    // quick menu (quick_menu_layout), as in the original.
    if (!replay && from_menu_ && (!scripted_ || !preset_.qm.empty()) && !attract_ && theater_demo_.empty() &&
        menu_before_run())
        open_prerace_menu(app);
    else if (!replay && preset_.prerace)
        open_prerace_menu(app);
    if (preset_.demo && !replay) start_demo();
    LOG_INFO("Race: loaded in %.0f ms",
             double(SDL_GetPerformanceCounter() - t0) * 1000.0 / double(SDL_GetPerformanceFrequency()));
    if (debug_) {
        for (int i = 0; i < 120; ++i) tick(app, PadState{});
        const sim::VehicleState& s = vehicle_->state();
        sim::SurfaceHit h = track_->query().probe(s.position.x, s.position.z, s.position.y + 2);
        LOG_INFO("Race debug: pos (%.2f %.2f %.2f) ground %.2f q(%.3f %.3f %.3f %.3f)", s.position.x, s.position.y,
                 s.position.z, h.height, s.orientation.w, s.orientation.x, s.orientation.y, s.orientation.z);
    }
    if (preset_.warp > 0) warp(app, preset_.warp);
    if (preset_.watch && !replaying_) {
        watch_replay();
        apply_replay_presets();
        if (preset_.rwarp > 0) warp(app, preset_.rwarp);
    }
    if (preset_.save) save_current_replay();
    if (preset_.quick && (finished_ || finished2_)) open_quick_menu(app);
    render_state_ = vehicle_->state();
    // Place the camera now: a paused start (screenshots) never runs a drive update.
    camera_.snap(render_state_);
    camera_.update(render_state_, vehicle_->setup(), PadState{}, 1.0 / 60.0);
    if (preset_.pause) open_pause(app);
    else paused_ = false;
    if (preset_.pause && preset_.pause_sel > 0) pause_menu_.set_cursor(preset_.pause_sel);
    if (!preset_.watch) apply_replay_presets();
    if (preset_.ritems >= 0 && (replaying_ || demo_)) replay_settings_.items = u8(preset_.ritems); // (not stored)
    if (preset_.rcam >= 0 && replaying_) { // screenshots: a player view in the replay (2: the in-car view)
        camera_.set_mode(CameraMode(std::clamp(preset_.rcam, 0, int(CameraMode::Count) - 1)));
        camera_.snap(render_state_);
        camera_.update(render_state_, vehicle_->setup(), PadState{}, 1.0 / 60.0);
    }
    if (preset_.ropts >= 0 && paused_ && (replaying_ || demo_)) {
        if (!replay_opts_loaded_) replay_opts_loaded_ = true, replay_opts_.load(*app.vfs(), app.renderer(), hud_.fonts());
        replay_opts_.open(replay_settings_, two_player_);
        replay_opts_.tick(0);
        for (int i = 0; i < preset_.ropts; ++i) replay_opts_.tick(menu::flow::key_bit(menu::flow::MenuKey::Down)), replay_opts_.tick(0);
        replay_opts_open_ = true;
    }
}

// Screenshots: the replay's view mode and target (not stored), from its start.
void RaceScreen::apply_replay_presets() {
    if (preset_.rmode < 0 && preset_.rtarget < 0) return;
    if (!replaying_ && !demo_) return;
    if (preset_.rmode >= 0) replay_settings_.mode = std::clamp(preset_.rmode, 0, 3);
    if (preset_.rtarget >= 0) replay_settings_.target[size_t(replay_settings_.mode)] = std::clamp(preset_.rtarget, 0, 2);
    director_.set_view_mode(replay_settings_.mode);
    begin_replay_focus();
}

void RaceScreen::warp(App& app, double seconds) {
    int ticks = int(seconds / step_.step());
    const bool focus = replaying_ && field_ && !two_player_; // the followed car's logic goes on (60 Hz)
    for (int i = 0; i < ticks && !(replaying_ && player_.done()); ++i) {
        tick(app, PadState{});
        if (focus)
            for (focus_acc_ += step_.step() * 60.0; focus_acc_ >= 1.0 - 1e-9; focus_acc_ -= 1.0) step_replay_focus();
    }
    if (replaying_ && player_.done()) {
        const sim::Vec3& a = vehicle_->state().position;
        bool exact = a.x == replay_.final_position.x && a.y == replay_.final_position.y && a.z == replay_.final_position.z;
        LOG_INFO("Replay: end, final position %s the recording", exact ? "matches" : "DIFFERS FROM");
    }
    prev_ = vehicle_->state();
    camera_.snap(vehicle_->state());
}

// Trackside cameras for the replay (TV) view. Placeholder until the
// course's own replay cameras (GTRC) are decoded: one camera every ~90 m,
// alternating sides of the road, 12 m out and 4 m up.
void RaceScreen::build_tv_cameras() {
    std::vector<TvCamera> cams;
    float len = track_->length();
    int n = std::max(4, int(len / 90.0f));
    for (int i = 0; i < n; ++i) {
        float s = len * float(i) / float(n);
        sim::Vec3 p = track_->point_at(s, (i & 1) ? 12.0f : -12.0f);
        sim::SurfaceHit h = track_->query().probe(p.x, p.z, p.y + 30.0f);
        float y = (h.hit ? h.height : p.y) + 4.0f;
        cams.push_back({{p.x, y, p.z}, s, 1.0f});
    }
    camera_.set_tv_cameras(std::move(cams), len);
}

// Best saved lap for this course and car, re-simulated into a ghost.
void RaceScreen::load_saved_ghost() {
    if (!app_ || !preset_.ghost) return;
    const std::string& course = courses_[size_t(course_sel_)].label;
    const std::string& car = cars_[size_t(car_sel_)].label;
    const Replay* best = nullptr;
    std::vector<Replay> loaded;
    int checked = 0;
    for (const ReplayFile& f : list_replays(replay_dir(*app_))) {
        if (f.name.rfind(course + "_" + car + "_", 0) != 0 || ++checked > 10) continue;
        std::optional<Bytes> b = read_host_file(f.path);
        if (!b) continue;
        try {
            loaded.push_back(Replay::parse(*b));
        } catch (const std::exception& e) {
            LOG_WARN("Replay %s: %s", f.name.c_str(), e.what());
        }
    }
    for (const Replay& r : loaded)
        if (r.best_lap > 0 && (!best || r.best_lap < best->best_lap)) best = &r;
    if (!best) return;
    bool exact = false;
    GhostLap g = ghost_from_replay(
        *best,
        [&] {
            std::unique_ptr<RaceVehicle> v = make_vehicle(car_, &db_, *track_);
            if (v) v->set_engine_condition(best->condition);
            return v;
        },
        &exact);
    if (g.empty()) return;
    ghost_ = std::move(g);
    ghost_time_ = best->best_lap;
    LOG_INFO("Race: ghost from saved replay, best lap %.3f s (%zu frames, %s)", best->best_lap, ghost_.size(),
             exact ? "reproduced exactly" : "NOT reproduced exactly");
}

void RaceScreen::restart() {
    take_live_condition(); // the previous live run hands its engine condition on
    replaying_ = false;
    run_recorded_ = false;
    retry_locked_ = false; // every run starts with RETRY available (presentation §E3)
    result_latch_.reset(); // a new live run
    latched_rows_.reset();
    TrackPose p = track_->grid_pose(0);
    if (race_mode_ && app_) {
        build_vehicle(*app_); // fresh field: opponents back on the grid
        if (field_) p = field_->player_grid();
    }
    // Rolling start (GT endurance): moving at 80 km/h from the grid. Flying start (time
    // attack, moving licence tests): the event's speed, its distance before the line.
    float v0 = 0;
    if (race_mode_ && field_ && rolling_) v0 = kRollingKmh / 3.6f;
    else if (race_mode_ && field_ && demo_start_kmh_ > 0) v0 = demo_start_kmh_ / 3.6f;
    else if (!race_mode_ && tt_start_.flying()) {
        v0 = tt_start_.initial_kmh / 3.6f;
        // physics §9: on the racing line (game line 6) at race progress -flying_m,
        // heading along the line over the next 10 m; the grid slot without the line.
        p = flying_pose(tt_start_.flying_m);
    } else if (!race_mode_ && preset_.at >= 0) { // debug: on the line, this far past the start, moving
        v0 = preset_.at_kmh / 3.6f;
        p = flying_pose(track_->length() - std::fmod(preset_.at, track_->length()));
    }
    if (v0 > 0) {
        vehicle_->reset_moving(p, v0);
    } else { // standing start: the slot marks the nose, no spawn hold (physics §9)
        if (const sim::Vehicle* sv = vehicle_->sim_vehicle()) p.position = grid_back(p.position, p.orientation, sv->setup());
        if (preset_.turn != 0) { // debug: q = rot_y(turn) * q
            float h = preset_.turn * 3.14159265f / 360.0f, c = std::cos(h), s = std::sin(h);
            sim::Quat q = p.orientation;
            p.orientation = {c * q.w - s * q.y, c * q.x + s * q.z, c * q.y + s * q.w, c * q.z - s * q.x};
        }
        vehicle_->place(p);
    }
    seed_vehicle_condition(run_condition_, true);
    reset_run_state();
    start_hud_clocks(0, v0);
    recorder_.begin(courses_[size_t(course_sel_)].label, cars_[size_t(car_sel_)].label,
                    controls_.config().automatic, preset_.paint, p, 1.0 / step_.step(), race_event_,
                    field_ ? field_->labels() : std::vector<std::string>{});
    recorder_.set_engine_condition(run_condition_);
    if (two_player_ && field_ && field_->has_second()) recorder_.set_second_car(preset_.car2.empty() ? car_.label : preset_.car2);
    recorder_.set_start_speed(v0);
    recorder_.set_paint_seed(paint_seed_);
    begin_license();
}

void RaceScreen::take_live_condition() {
    if (live_condition_ && has_condition_ && vehicle_ && vehicle_->sim_vehicle())
        run_condition_ = vehicle_->engine_condition();
    live_condition_ = false;
}

// A live run starts with run_condition_ (a mode that carries no condition: a fresh engine
// every run); a replay with the condition its run started with.
void RaceScreen::seed_vehicle_condition(const sim::EngineCondition& c, bool live) {
    if (!vehicle_) return;
    vehicle_->set_engine_condition(c);
    live_condition_ = live;
}

sim::EngineCondition RaceScreen::live_condition() const {
    return live_condition_ && vehicle_ && vehicle_->sim_vehicle() ? vehicle_->engine_condition() : run_condition_;
}

void RaceScreen::drop_vehicle() {
    take_live_condition();
    pit_ = PitLane{};
    vehicle_ = nullptr;
    field_.reset();
    own_vehicle_.reset();
    opp_visuals_.clear();
}

// The player's vehicle: alone (time trial) or in a field with AI opponents.
bool RaceScreen::build_vehicle(App& app) {
    bool ok = build_vehicle_inner(app);
    // Dirt courses (course table 34 +0x08 = 2): the physics' front-brake cut, every car.
    if (ok && vehicle_) {
        bool dirt = false;
        for (const data::Course& c : db_.courses())
            if (c.label == courses_[size_t(course_sel_)].label) dirt = c.surface == 2;
        vehicle_->set_dirt_course(dirt);
        net_.dirt = dirt;
        // The AI aid block while the computer drives the player's car (run-up, pit lane, end of run).
        vehicle_->set_scripted_aids(player_params(false).aids);
    }
    return ok;
}

bool RaceScreen::build_vehicle_inner(App& app) {
    drop_vehicle();
    if (race_mode_) {
        FieldConfig cfg;
        cfg.player = car_.label;
        cfg.player_spec = car_.spec;
        cfg.event = race_event_;
        cfg.opponents = race_opponents_;
        // The event's start (standing / rolling), car count and time limit.
        rolling_ = false;
        time_limit_ = 0;
        if (const data::Event* sev = db_.event(race_event_)) {
            EventStart es = event_start(*sev);
            music_start_type_ = es.type;
            cfg.countdown = es.rolling() ? 0.0 : es.countdown;
            if (es.rolling()) cfg.start_kmh = kRollingKmh;
            // Demo races (start type 0 with a speed): every car starts moving, no countdown.
            demo_start_kmh_ = es.type == 0 && es.initial_kmh > 0 ? es.initial_kmh : 0.0f;
            if (demo_start_kmh_ > 0) cfg.start_kmh = demo_start_kmh_, cfg.countdown = 0;
            // Tyre wear: single player races, not on dirt courses (physics §8.1).
            bool dirt = false;
            for (const data::Course& c : db_.courses())
                if (c.label == courses_[size_t(course_sel_)].label) dirt = c.surface == 2;
            if (sev->bytes.size() >= 0x1C4)
                cfg.wear = sim::model::wear_params(&sev->bytes[0x1BD], !two_player_ && !dirt);
            if (es.cars > 1) cfg.max_opponents = std::min(es.cars - 1, 5);
            if (es.laps == 0 && es.time_limit_s > 0) cfg.time_limit = time_limit_ = es.time_limit_s;
            rolling_ = es.rolling();
            LOG_INFO("Race: event %s start type %d (%s), countdown %.0f s, %d cars, %d laps, time limit %.0f min",
                     race_event_.c_str(), es.type, es.rolling() ? "rolling" : es.flying() ? "flying" : "standing",
                     cfg.countdown, es.cars, es.laps, es.time_limit_s / 60.0);
        }
        cfg.cpu_respawn = respawn_mode(app) != "off";
        cfg.course_category = driver_param_set(db_, course_label_for_dirt_);
        // GT mode orders the grid by power-to-weight (strongest on pole, the player last).
        cfg.grid_by_power = from_menu_ && launch_.request.mode.rfind("go_", 0) == 0;
        // A race replay's field as its run had it, wherever it is played (the theater has
        // its own launch and today's settings).
        if (replaying_ && replay_.grid_by_power) cfg.grid_by_power = *replay_.grid_by_power;
        if (replaying_ && replay_.cpu_respawn) cfg.cpu_respawn = *replay_.cpu_respawn;
        field_grid_by_power_ = cfg.grid_by_power, field_cpu_respawn_ = cfg.cpu_respawn;
        if (two_player_) {
            cfg.countdown = 0; // the original's 2P battle: no 3-2-1, the clock runs from the start
            cfg.second_player = preset_.car2.empty() ? car_.label : preset_.car2;
            battle_ = replaying_ && replay_.battle ? *replay_.battle : live_battle(app, cfg.second_player, cfg.course_category);
            cfg.second_spec = battle_.spec2;
            cfg.boost_amount = battle_.boost_amount, cfg.boost_start = battle_.boost_start, cfg.boost_full = battle_.boost_full;
            cfg.handicap_m = battle_.handicap_m;
            if (battle_.tyre_damage) cfg.wear = sim::model::shipped_wear_params();
            cfg.opponents.clear();
            cfg.max_opponents = 0; // 2P battle: just the two players
        }
        if (online_) configure_online_field(cfg);
        std::string err;
        field_ = RaceField::create(*track_, db_, cfg, &err);
        if (field_) {
            vehicle_ = &field_->player();
            car2_visual_.reset();
            // Player 2's car as the battle has it (a garage car: its own model and bought wheels).
            const int paint2 = battle_.paint2;
            std::string car2 = preset_.car2.empty() ? car_.label : preset_.car2;
            const std::string& model2 = battle_.model2;
            const std::string& wheel2 = battle_.wheel2;
            if (field_->has_second() &&
                (car2 != car_.label || paint2 != preset_.paint || !model2.empty() || !wheel2.empty())) {
                if (const data::Car* c2 = db_.car(car2)) {
                    std::string m2 = model2.empty() ? db_.model_of(*c2) : model2;
                    std::optional<Bytes> f2 = read_car(app, m2);
                    WheelSpec ws2;
                    if (auto w = wheel_spec_for_car(db_, car2)) ws2 = *w;
                    CarBodyMod bm2 = body_mod_for_car(db_, car2, paint2);
                    if (!wheel2.empty()) bm2.wheel = wheel2;
                    std::optional<Bytes> w2 = bm2.wheel.empty() ? std::nullopt : app.vfs()->read("wheel/" + bm2.wheel);
                    if (f2 && !f2->empty())
                        car2_visual_ = std::make_unique<CarVisual>(*app.gpu(), *f2, m2, ws2, paint2,
                                                                   w2 ? ByteSpan(*w2) : ByteSpan{}, bm2);
                }
            }
            // Pit lane (races only; the course needs the pit lines).
            pit_ = PitLane{};
            if (const track::Track* rt = track_->runtime()) pit_.load(*rt, 0);
            const data::Event* ev = db_.event(race_event_);
            race_laps_ = ev && ev->laps ? ev->laps : time_limit_ > 0 ? 0 : 3;
            // 2P battle: the Options menu's lap count ([options] split_laps, default 2), or the
            // replay's.
            if (two_player_ && replaying_ && replay_.battle) race_laps_ = replay_.battle->laps;
            else if (two_player_ && preset_.laps <= 0) race_laps_ = menu::flow::option_laps(&app.config());
            if (two_player_) battle_.laps = race_laps_;
            if (online_) race_laps_ = std::max(1, preset_.laps);
            field_->set_laps(race_laps_);
            race_opponents_ = field_->labels();
            // Paint colours: player first, then the opponents (event slot ids or the game's draw).
            std::vector<PaintEntry> entries;
            std::string player_model = launch_model_.empty() ? cars_[size_t(car_sel_)].model : launch_model_;
            entries.push_back({player_model, colors_.id_at(player_model, preset_.paint)});
            const data::Event* pev = db_.event(race_event_);
            for (int i = 0; i < field_->opponents(); ++i) {
                PaintEntry e{field_->opponent_model(i)};
                if (pev && race_opponents_ == pev->entries && pev->bytes.size() >= 0x110 + 4 * size_t(i + 1))
                    e.id = rd32(pev->bytes, 0x110 + 4 * size_t(i));
                if (online_ && size_t(i) < net_.paint.size()) // the colour that player chose
                    e.id = colors_.id_at(field_->opponent_model(i), net_.paint[size_t(i)]);
                entries.push_back(e);
            }
            // The race module's generator, as left by the opponent draw; a replay re-runs
            // the draw from the state its race had.
            GameRandom replay_rng{paint_seed_override_.value_or(GameRandom::kBootSeed)};
            GameRandom& rng = paint_seed_override_ ? replay_rng : race_info_random();
            paint_seed_ = rng.x;
            paint_seed_override_.reset();
            std::vector<int> paints = assign_paints(colors_, entries, rng);
            std::map<std::pair<std::string, int>, std::shared_ptr<CarVisual>> by_model;
            for (int i = 0; i < field_->opponents(); ++i) {
                const std::string& model = field_->opponent_model(i);
                int paint = std::max(0, paints[size_t(i) + 1]);
                if (online_ && size_t(i) < net_.paint.size())
                    LOG_INFO("Online: %s's %s in paint %d (chosen %d)", net_.names[size_t(i)].c_str(), model.c_str(),
                             paint, net_.paint[size_t(i)]);
                // A mod car's own body (glTF, or the stretched donor shell) and rim, as the player's
                // car gets them; disc cars have none.
                std::string l = field_->opponent_label(i), base = db_.car(l) ? l : "";
                if (base.empty())
                    if (const data::CarVariant* cv = db_.variant(l)) base = cv->car;
                CarBodyMod bm = db_.mod_car(base) ? body_mod_for_car(db_, base, paint) : CarBodyMod{};
                std::shared_ptr<CarVisual>& v = by_model[{bm.active() ? base : model, paint}];
                if (!v) {
                    std::optional<Bytes> f = read_car(app, model);
                    WheelSpec ws;
                    if (auto w = wheel_spec_from_database(db_, model)) ws = *w;
                    std::optional<Bytes> wf = bm.wheel.empty() ? std::nullopt : app.vfs()->read("wheel/" + bm.wheel);
                    try {
                        if (f && !f->empty())
                            v = std::make_shared<CarVisual>(*app.gpu(), *f, model, ws, paint, wf ? ByteSpan(*wf) : ByteSpan{}, bm);
                    } catch (const std::exception& e) {
                        LOG_WARN("Race: opponent model %s: %s", model.c_str(), e.what());
                    }
                }
                opp_visuals_.push_back(v);
            }
            // Engine notes of the opponents (their base car's sound bank).
            std::vector<OpponentEngines::Car> sounds;
            for (int i = 0; i < field_->opponents(); ++i) {
                const std::string& l = field_->opponent_label(i);
                std::string car = db_.car(l) ? l : "";
                if (car.empty())
                    if (const data::CarVariant* v = db_.variant(l)) car = v->car;
                if (car.empty())
                    for (const data::CarVariant& v : db_.arcade_cars())
                        if (v.label == l) car = v.car;
                OpponentEngines::Car c;
                c.cd = car_sound_facts(l).cd;
                if (const data::Car* dc = db_.car(car)) {
                    data::CarDatabase::EngineSound es = db_.engine_sound(*dc);
                    c.sound_id = es.id, c.variant = es.variant;
                }
                sounds.push_back(c);
            }
            opp_engines_.stop(app.audio().voices());
            opp_engines_.load(*app.vfs(), sounds);
            return true;
        }
        LOG_WARN("Race: no opponents (%s); time trial instead", err.c_str());
        toast("Opponents unavailable: time trial");
        race_mode_ = false;
    }
    own_vehicle_ = make_vehicle(car_, &db_, *track_);
    vehicle_ = own_vehicle_.get();
    if (!vehicle_) error_ = "could not create the vehicle";
    return vehicle_ != nullptr;
}

// A 2P battle as launched: player 2's car (its car page's, with the Options ASM / TCS off)
// and the Options menu's catch-up boost, handicap and tyre damage (not on dirt); the laps are
// set with the field.
ReplayBattle RaceScreen::live_battle(App& app, const std::string& car2, int course_category) const {
    ReplayBattle b;
    b.spec2 = launch_spec2_;
    if (aids_options_apply_ && (!asm_on_ || !tcs_on_)) {
        std::optional<sim::CarSpec> s2;
        if (launch_spec2_) s2 = *launch_spec2_;
        else s2 = sim::build_car_spec(db_, car2);
        if (s2) {
            sim::apply_aid_options(s2->aids, asm_on_, tcs_on_);
            b.spec2 = std::make_shared<const sim::CarSpec>(std::move(*s2));
        }
    }
    const IniDocument* cfg = &app.config();
    std::string boost = menu::flow::option_value(cfg, "split_boost");
    if (boost == "low") b.boost_amount = 0.15f, b.boost_start = 20, b.boost_full = 1000;
    else if (boost == "high") b.boost_amount = 0.20f, b.boost_start = 10, b.boost_full = 200;
    b.handicap_m = float(menu::flow::option_handicap(cfg));
    b.tyre_damage = menu::flow::option_on(cfg, "split_tire_damage") && course_category != 2;
    if (car2_paint_ >= 0) b.paint2 = car2_paint_;
    if (from_menu_) b.model2 = launch_.request.car2_model, b.wheel2 = launch_.request.car2_wheel;
    return b;
}

void RaceScreen::reset_run_state() {
    script_time_ = 0;
    demo_drive_tick_ = 0;
    prev_ = vehicle_->state();
    render_state_ = prev_;
    race_time_ = 0;
    machine_time_ = -1, top_speed_kmh_ = 0, hud_top_kmh_ = 0;
    if (machine_) machine_->reset();
    tt_failed_ = false;
    tt_hold_ = !field_ && (tt_start_.type == 0 || tt_start_.type == 1) && !tt_start_.flying() ? tt_start_.countdown : 0;
    // The course's animations restart with every run; the start-signal lights follow the
    // countdown (none for rolling and flying starts).
    restart_course_animations(float(field_ ? std::max(0.0, -field_->race_time()) : tt_hold_));
    vu0_random().init(Vu0Random::kRunSeed); // (as the game's run start: the cars' flap pendulums draw from it)
    // Lap 1 (presentation §B9): a race times it from GO for every car
    // (the HUD's lap time runs from GO), a standing licence test too (shown once past the line),
    // a machine test too (its clock runs from GO, presentation §B7);
    // time trials, free runs and flying tests from the first line crossing.
    const bool standing_licence = !license_event_.empty() && !tt_start_.flying();
    const bool machine = machine_.has_value();
    track_->lap_set_lap1_from_go(field_ || standing_licence || machine, field_ != nullptr || machine);
    track_->lap_reset(0);
    track_->lap_update(vehicle_->state().position, 0);
    controls_.reset();
    rumble_.reset();
    step_.reset();
    camera_.snap(vehicle_->state());
    offroad_time_ = stuck_time_ = 0;
    best_progress_ = -1;
    paused_ = false;
    cur_lap_.clear();
    lap_ticks_ = 0;
    lap_begin_tick_ = 0;
    was_started_ = track_->lap_started();
    flying_runup_ = flying_runup();
    flying_go_t_ = -1;
    run_session_record_ = false;
    finished_ = false;
    finish_position_ = 0;
    lap_times_.clear();
    odometer_m_ = 0;
    smoke_.clear();
    pylons_.reset(track_->query()); // every run (and retry) starts with the cones in place
    pylon_hit_ = false;
    exit_transition_ = -1;
    brake_lamps_.clear();
    hint_driver_.reset(), hint_course_.reset(); // rebuilt for this run
    runup_driver_.reset(), runup_course_.reset();
    auto_driver_.reset(), end_of_run_ = false; // the pit lane / end-of-run driver
    finish_wait_ = 0;
    finish_menu_shown_ = false;
    all_done_at_ = -1;
    exit_finished_ = false, overlay_delay_ = race::kRecordOverlayDelay;
    p1_won_ = p2_won_ = false;
    finish_record_rank_ = -1;
    quick_ = false;
    finished2_ = false;
    finish_position2_ = 0;
    controls2_.reset();
    if (manual2_ >= 0) controls2_.config().automatic = manual2_ == 0;
    rumble2_.reset();
    if (field_ && field_->has_second()) {
        render_state2_ = field_->second_state();
        camera2_.snap(render_state2_);
    }
    reset_lap_events();
}

// [race] respawn = off | cpu | all (default cpu): the non-original stuck fail-safe.
std::string RaceScreen::respawn_mode(App& app) {
    std::string v = app.config().get("race", "respawn").value_or("cpu");
    return v == "off" || v == "all" ? v : "cpu";
}

std::optional<Bytes> RaceScreen::read_car(App& app, const std::string& model) const {
    if (car_folder_ != "cars/day/")
        if (std::optional<Bytes> f = app.vfs()->read(car_folder_ + model); f && !f->empty()) return f;
    return app.vfs()->read("cars/day/" + model);
}

} // namespace gt3r::race_screen
