// Building a run: start() loads the course, car models, sounds and the run's race (race::RaceField on the exact core:
// a race with opponents, a split-screen or online battle, or a solo run); restart() / reset_run_state() build the race
// again from its start; warp, the TV cameras, the saved ghosts.
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"

#include "sim/exact/race_flow.h"
#include "sim/exact/race_record.h"

namespace gt3r::race_screen {

// Loads course, car, track and vehicle for the selected entries. `replay`
// (optional) is played back instead of driving.
void RaceScreen::start(App& app, const Replay* replay) {
    error_.clear();
    reset_session_best(); // a new visit: the runs and retries that follow share it
    qm_items_ = {};       // ... and its quick menus' item switches (menus §11)
    // Speed unit: the game language's (language_units: mph in US English, else km/h, like the
    // menus) unless the player picked one ([race] units, speed_unit()).
    imperial_ = !app.vfs() || language_units(app.vfs()->language()).mph;
    lang_ = app.language();
    if (game::GtModeSession* gs = game::gt_mode_session()) imperial_ = gs->gt().data().units.mph;
    imperial_ = speed_unit().value_or(imperial_);
    env_cube_on_ = app.config().get("video", "car_reflections").value_or("on") != "off";
    if (SDL_getenv("GT3R_NO_ENV")) env_cube_on_ = false;
    shadows_on_ = app.config().get("video", "car_shadows").value_or("on") != "off";
    full_car_detail_ = app.full_car_detail();
    // [video] original_res = on: the 3D scene at the PS2's 640x448 in a 4:3 frame,
    // upscaled with [video] original_filter = nearest (default) | bilinear.
    original_res_ = app.config().get("video", "original_res").value_or("off") == "on";
    original_nearest_ = app.config().get("video", "original_filter").value_or("nearest") != "bilinear";
    if (const char* e = SDL_getenv("GT3R_ORIGINAL_RES")) original_res_ = std::string(e) != "0", original_nearest_ = std::string(e) != "bilinear";
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
        course_bytes_ = *course;
        car_ = car_setup_from_database(db_, ca.label);
        if (replay && !replay->car_model.empty()) launch_model_ = replay->car_model;
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
        car_visual_->set_full_detail(full_car_detail_);
        course_name_ = ce.name;
        // The start title shows the course table's name as it is: licence sections have their
        // own ("Grand Valley Test Section"), and a course without one shows no title.
        title_name_.clear();
        for (const data::Course& c : db_.courses())
            if (c.label == ce.label) title_name_ = c.name;
        course_label_for_dirt_ = ce.label;
        course_dirt_ = dirt_course();
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
        // The pre-race shot's camera move (starting camera: cameras/start.cam, record 0).
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
        hud_.load(*app.vfs(), app.renderer(), lang_);
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
                if (launch_source_ && launch_source_->parts && !attract_) sound_car_.whine = rq.racing_gearbox;
            if constexpr (requires { rq.racing_gearbox2; }) // 2P battle: player 2's garage car
                if (two_player_ && rq.car2_parts) sound_car2_.whine = rq.racing_gearbox2;
        }(launch_.request);
    LOG_INFO("Race: car sounds: C_d %.2f, gearbox whine %s, course ambience %s", sound_car_.cd,
             sound_car_.whine ? "on" : "off", crss_ok_ ? "on" : "none");
    if (!results_loaded_) results_loaded_ = results_.load(*app.vfs(), app.renderer(), lang_) || true;
    engine2_.stop(app.audio().voices());
    if (two_player_ && car_.sound_id) engine2_.load(*app.vfs(), car_.sound_id, car_.sound_variant);
    if (two_player_ && motor_.whine) engine2_.set_banks(motor_.whine, motor_.inverter);
    if (engine_.loaded()) LOG_INFO("Race: engine sound %05u variant %u (%s)", car_.sound_id, car_.sound_variant,
                                   EngineSound::variant_dir(car_.sound_variant).c_str());
    stage_ = Stage::Drive;
    ghost_record_.reset(), best_run_record_.reset(); // (a new visit has no best run and no ghost)
    ghost_time_ = -1;
    best_run_time_ = -1;
    best_run_replay_.reset();
    best_lap_in_run_ = ghost_ask_ = false;
    ghost_shown_ = true;
    // (No ghost to start with: a visit's first comes from its runs or LOAD GHOST, as the original.)
    if (replay) start_playback(*replay);
    else restart();
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
        if (!replay_opts_loaded_) replay_opts_loaded_ = true, replay_opts_.load(*app.vfs(), app.renderer(), hud_.fonts(), lang_);
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
    if (replaying_ && player_.done())
        if (std::optional<bool> m = exact_field() ? exact_playback_matches(*exact_field()) : std::nullopt)
            LOG_INFO("Replay: end, the race %s the recording", *m ? "matches" : "DIFFERS FROM");
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

// LOAD GHOST (ours: replay files stand in for the memory card's ghost files): free runs, the
// best saved lap for this course and car; licence tests, time trials and machine tests, the
// fastest saved run of the same test (course, car, kind and start), the whole run, by the time
// their ghost rules compare (presentation §G11). A whole run's car 0 becomes the ghost car of the
// next run; a free run's best lap the free run's ghost (its race's ghost car, from the next run).
bool RaceScreen::load_saved_ghost() {
    if (!app_ || !preset_.ghost) return false;
    const bool whole = whole_run_ghost();
    if (!whole && !lap_ghost()) return false;
    const Replay& now = recorder_.replay(); // (this run's start)
    const ReplayKind kind = replay_kind();
    const std::string& course = courses_[size_t(course_sel_)].label;
    const std::string& car = cars_[size_t(car_sel_)].label;
    const Replay* best = nullptr;
    double best_t = -1;
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
    for (const Replay& r : loaded) {
        if (!can_play(r)) continue; // (another program's run: not offered)
        double t = r.best_lap;
        if (whole) {
            const sim::Vec3 d = r.start.position - now.start.position;
            const bool same = r.kind == kind && std::fabs(r.start_speed - now.start_speed) < 0.1f && d.dot(d) < 1.0f;
            t = !same ? -1 : kind == ReplayKind::TimeTrial ? r.best_lap : r.total_time;
        } else if (r.exact->kind != sim::exact::Conductor::FreeRun) {
            t = -1; // (a free run's lap ghost comes from a free run)
        }
        if (t > 0 && (!best || t < best_t)) best = &r, best_t = t;
    }
    if (!best) return false;
    if (whole) { // the run's car 0 as the ghost car of the next run
        ghost_record_ = best->exact;
        ghost_time_ = best_t;
        LOG_INFO("Race: ghost from saved replay, run %.3f s (%u steps, a car of the race)", best_t, best->exact->frames);
        return true;
    }
    // A free run's: its run played to its end, its best lap (the store: its snapshot and recording) as the visit's ghost
    // (and the live run's, whose recorders the visit takes as the run ends).
    std::optional<sim::exact::SoloRecorders> rec = solo_recorders_of(*best);
    if (!exact_free_run_) exact_free_run_ = std::make_shared<sim::exact::SoloRecorders>(true);
    if (!rec || !exact_free_run_->ghost_from_best(*rec)) return false;
    if (RaceField* f = exact_field()) exact_ghost_from_best(*f, *rec);
    ghost_time_ = double(rec->best_time()) / 1000.0;
    LOG_INFO("Race: ghost from saved replay, best lap %.3f s (%u frames)", ghost_time_, rec->best.rec.header_frames(rec->pool1));
    return true;
}

std::optional<sim::exact::SoloRecorders> RaceScreen::solo_recorders_of(const Replay& r) {
    if (!app_ || !track_ || !r.exact || !load_program_data(*app_)) return std::nullopt;
    FieldConfig cfg;
    cfg.exact = exact_options(*app_);
    cfg.exact.playback = r.exact;
    cfg.exact.ghost = nullptr, cfg.exact.free_run = nullptr;
    cfg.exact.kind = r.exact->kind;
    std::string err;
    std::unique_ptr<RaceField> f = RaceField::create(*track_, db_, cfg, &err);
    if (!f || !exact_solo_recorders(*f)) {
        LOG_WARN("Race: the saved free run cannot be played (%s)", err.c_str());
        return std::nullopt;
    }
    for (u32 k = 0; k < r.exact->frames; ++k) f->player().step({}, 1.0 / f->tick_hz());
    sim::exact::SoloRecorders rec = *exact_solo_recorders(*f);
    rec.end(); // (the race's end closes its recordings)
    return rec;
}

// The session's best run becomes the ghost (presentation §G11): its record, the ghost car of the next run.
void RaceScreen::take_best_run_ghost() {
    // A free run's best lap becomes the ghost in its recorders (1, 0: "Update Ghost?").
    if (lap_ghost()) {
        if (RaceField* f = exact_field(); f && exact_best_lap_to_ghost(*f)) LOG_INFO("Race: ghost updated, the best lap");
        else if (exact_free_run_) exact_free_run_->best_to_ghost(false);
        return;
    }
    if (!best_run_record_) return;
    ghost_record_ = best_run_record_;
    ghost_time_ = best_run_time_;
    LOG_INFO("Race: ghost updated, the best run %.3f s (%u steps)", ghost_time_, ghost_record_->frames);
}

void RaceScreen::restart() {
    take_live_condition(); // the previous live run hands its engine condition on
    replaying_ = false;
    run_recorded_ = false;
    retry_locked_ = false; // every run starts with RETRY available (presentation §E3)
    result_latch_.reset(); // a new live run
    latched_rows_.reset();
    if (app_) build_vehicle(*app_); // a new race: every car back on its start
    const RaceField* f = exact_field();
    const TrackPose p = f ? f->player_grid() : track_->grid_pose(0);
    // The start speed (the title clock's form, the replay's start): a rolling start's 80 km/h (GT endurance), a demo race's
    // speed, a flying start's (time attack, moving licence tests); the race itself places and moves the cars.
    float v0 = 0;
    if (race_mode_ && field_ && rolling_) v0 = 80.0f / 3.6f;
    else if (race_mode_ && field_ && demo_start_kmh_ > 0) v0 = demo_start_kmh_ / 3.6f;
    else if (!race_mode_ && tt_start_.flying()) v0 = tt_start_.initial_kmh / 3.6f;
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
    if (live_condition_ && has_condition_ && vehicle_ && vehicle_->facts())
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
    return live_condition_ && vehicle_ && vehicle_->facts() ? vehicle_->engine_condition() : run_condition_;
}

void RaceScreen::drop_vehicle() {
    take_live_condition();
    if (track_) track_->set_lap_source(nullptr);
    vehicle_ = nullptr;
    field_.reset();
    solo_field_.reset();
    opp_visuals_.clear();
}

// The disc's program data (the program's tables, the release's rules): every race runs on it. False (error_ set) when the
// disc has no program the exact core can decode.
bool RaceScreen::load_program_data(App& app) {
    if (exact_data_) return true;
    if (!app.vfs()) {
        error_ = "No disc loaded";
        return false;
    }
    std::string err;
    exact_data_ = exact_program_data(*app.vfs(), &err);
    if (!exact_data_) {
        error_ = "Races cannot be set up on this disc: " + err;
        LOG_WARN("Race: %s", error_.c_str());
        return false;
    }
    return true;
}

// What every race takes from the screen (the races' and the solo modes' set-ups add the mode, laps and player 2): the
// program data and whose rules, the course, player 1's car and driving settings, a replay's record and the ghost.
ExactFieldOptions RaceScreen::exact_options(App& app) {
    ExactFieldOptions o;
    o.data = exact_data_;
    // The disc's own release's rules for the player's races; the game's demos (the attract demo, the quick menu's and the
    // theater's demonstrations, the licence preview) and online races (every client shares one physics, docs/dev/ONLINE.md)
    // NTSC-U's on every disc.
    if (attract_ || demo_ || preview_ || !theater_demo_.empty() || online_) o.rules = sim::Program::NtscU;
    o.course_file = course_bytes_;
    o.course = courses_[size_t(course_sel_)].label;
    o.player.car = launch_source_.value_or(ExactCarSource{cars_[size_t(car_sel_)].label});
    o.player.pedal_style = controls_.config().automatic ? 0 : 1;
    o.player.wheel = app.input().wheel_steering(0);
    o.player.asm_on = !aids_options_apply_ || asm_on_;
    o.player.tcs_on = !aids_options_apply_ || tcs_on_;
    o.player.condition = run_condition_;
    o.player.dirt = float(car_dirt_);
    // [race] analog_response = linear (default) | original (Settings > Controls > Analog response; ExactHuman).
    o.player.original_response = to_lower(app.config().get("race", "analog_response").value_or("linear")) == "original";
    o.second.original_response = o.player.original_response;
    o.playback = exact_playback();
    o.ghost = exact_ghost_source();
    // The Analyzer's REPLAY on the exact core: track A's replay as GT3's logger race, track B its ghost car, the pass's tables.
    if (analysis_ && replaying_ && logger_mode_) {
        o.analysis = true, o.analysis_b = analysis_b_, o.logger = true, o.logger_tables = analysis_tables_;
        o.ghost = nullptr, o.free_run = nullptr;
    }
    return o;
}

// The run's race: a race with opponents (or a battle), else a solo run (screen_race_solo.cpp). False with error_ set when
// it cannot be set up from the disc's data.
bool RaceScreen::build_vehicle(App& app) {
    drop_vehicle();
    if (!load_program_data(app)) return false;
    if (race_mode_) {
        FieldConfig cfg;
        cfg.event = race_event_;
        cfg.opponents = race_opponents_;
        // The event's start (standing / rolling), car count and time limit.
        rolling_ = false;
        time_limit_ = 0;
        if (const data::Event* sev = db_.event(race_event_)) {
            EventStart es = event_start(*sev);
            music_start_type_ = es.type;
            // Demo races (start type 0 with a speed): every car starts moving, no countdown.
            demo_start_kmh_ = es.type == 0 && es.initial_kmh > 0 ? es.initial_kmh : 0.0f;
            if (es.cars > 1) cfg.max_opponents = std::min(es.cars - 1, 5);
            if (es.laps == 0 && es.time_limit_s > 0) time_limit_ = es.time_limit_s;
            rolling_ = es.rolling();
            LOG_INFO("Race: event %s start type %d (%s), countdown %.0f s, %d cars, %d laps, time limit %.0f min",
                     race_event_.c_str(), es.type, es.rolling() ? "rolling" : es.flying() ? "flying" : "standing",
                     es.rolling() || demo_start_kmh_ > 0 ? 0.0 : es.countdown, es.cars, es.laps, es.time_limit_s / 60.0);
        }
        // GT mode orders the grid by whole kg per whole power in the language's power unit (lowest on pole, the player last).
        cfg.grid_by_power = from_menu_ && launch_.request.mode.rfind("go_", 0) == 0;
        cfg.grid_hp = app.vfs() && language_units(app.vfs()->language()).power_hp;
        // A race replay's field as its run had it, wherever it is played (the theater has
        // its own launch and today's settings).
        if (replaying_ && replay_.grid_by_power) cfg.grid_by_power = *replay_.grid_by_power;
        field_grid_by_power_ = cfg.grid_by_power;
        const std::string car2 = preset_.car2.empty() ? car_.label : preset_.car2;
        if (two_player_) {
            battle_ = replaying_ && replay_.battle ? *replay_.battle : live_battle(app);
            cfg.battle_catchup = battle_.catchup;
            cfg.handicap_m = battle_.handicap_m;
            cfg.battle_wear = battle_.tyre_damage;
            cfg.opponents.clear();
            cfg.max_opponents = 0; // 2P battle: just the two players
        }
        if (online_) configure_online_field(cfg);
        // The race's laps: the event's (none: 3, or its time limit), the 2P battle's Options laps.
        {
            const data::Event* ev = db_.event(race_event_);
            race_laps_ = ev && ev->laps ? ev->laps : time_limit_ > 0 ? 0 : 3;
            if (two_player_ && replaying_ && replay_.battle) race_laps_ = replay_.battle->laps;
            else if (two_player_ && preset_.laps <= 0) race_laps_ = menu::flow::option_laps(&app.config());
            if (two_player_) battle_.laps = race_laps_;
            if (online_) race_laps_ = std::max(1, preset_.laps);
        }
        // A replay: the race from its record (no grid, car or event decisions made again).
        ExactFieldOptions o = exact_options(app);
        o.kind = online_       ? sim::exact::Conductor::BattleMP // the i.LINK battle
                 : two_player_ ? sim::exact::Conductor::Battle2P
                               : sim::exact::Conductor::SinglePlayer;
        // The attract demo runs as GT3's does: the race's play kind 2 (the race passes it to the conductor's
        // race start as the race-flow byte; the race's exit(2) from the demo runner), so every car, slot 0 too,
        // is a CPU car (control 2) with the event's overrides varied by grid slot, and no records. The test autopilot:
        // configuration 3, the race's AI drives the players' cars (the pads decoded).
        o.config = attract_ ? 2 : preset_.autodrive ? 3 : 0;
        o.laps = race_laps_;
        if (two_player_) {
            o.second.car = launch_source2_.value_or(ExactCarSource{car2});
            o.second.pedal_style = manual2_ == 1 ? 1 : 0;
            o.second.wheel = app.input().wheel_steering(1);
            o.second.asm_on = o.player.asm_on, o.second.tcs_on = o.player.tcs_on;
        }
        cfg.exact = std::move(o);
        std::string err;
        field_ = RaceField::create(*track_, db_, cfg, &err);
        if (!field_) {
            if (replaying_) {
                LOG_ERROR("Replay: the recorded race cannot be built (%s)", err.c_str());
                const bool other = err.find("recorded with") != std::string::npos; // (another program's or other data)
                toast(other ? "This replay cannot be played: " + err.substr(err.find("recorded with")) + "." : "This replay cannot be played.");
            }
            error_ = (online_ ? "Online race: " : "This race cannot be set up: ") + err;
            LOG_WARN("Race: %s", error_.c_str());
            return false;
        }
        {
            vehicle_ = &field_->player();
            track_->set_lap_source(exact_lap_source(*field_)); // the exact core times the player's laps itself
            if (1.0 / step_.step() != field_->tick_hz()) step_ = FixedStep(field_->tick_hz()); // (PAL: 50 a second)
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
                    if (f2 && !f2->empty()) {
                        car2_visual_ = std::make_unique<CarVisual>(*app.gpu(), *f2, m2, ws2, paint2,
                                                                   w2 ? ByteSpan(*w2) : ByteSpan{}, bm2);
                        car2_visual_->set_full_detail(full_car_detail_);
                    }
                }
            }
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
                        if (f && !f->empty()) {
                            v = std::make_shared<CarVisual>(*app.gpu(), *f, model, ws, paint, wf ? ByteSpan(*wf) : ByteSpan{}, bm);
                            v->set_full_detail(full_car_detail_);
                        }
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
    }
    // The solo modes (screen_race_solo.cpp).
    return build_exact_solo(app);
}

// A 2P battle as launched: the Options menu's catch-up, handicap and tyre damage (not on dirt), player 2's model and wheels;
// the laps are set with the field (player 2's car and its Options ASM / TCS are ExactFieldOptions::second's).
ReplayBattle RaceScreen::live_battle(App& app) const {
    ReplayBattle b;
    const IniDocument* cfg = &app.config();
    const std::string boost = menu::flow::option_value(cfg, "split_boost");
    b.catchup = boost == "low" ? 1 : boost == "high" ? 2 : 0;
    b.handicap_m = float(menu::flow::option_handicap(cfg));
    b.tyre_damage = menu::flow::option_on(cfg, "split_tire_damage") && !dirt_course();
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
    tt_hold_ = solo_field_ ? std::max(0.0, -solo_field_->race_time()) : 0; // (the race holds the car itself)
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
    paused_ = false;
    if (!replaying_) best_lap_in_run_ = false;
    lap_ticks_ = 0;
    lap_begin_tick_ = 0;
    was_started_ = track_->lap_started();
    flying_runup_ = flying_runup();
    flying_go_t_ = -1;
    license_clock_start_ = 0;
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
    lamp_index_.clear();
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

std::optional<Bytes> RaceScreen::read_car(App& app, const std::string& model) const {
    if (car_folder_ != "cars/day/")
        if (std::optional<Bytes> f = app.vfs()->read(car_folder_ + model); f && !f->empty()) return f;
    return app.vfs()->read("cars/day/" + model);
}

} // namespace gt3r::race_screen
