// Sound glue: the race music cues, course ambience (CRSS) listeners, every car's tyre
// loops, the inputs of the per-car extra sounds (wind, whine, turbo) and silencing everything.
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"
#include "sim/exact/race.h"

namespace gt3r::race_screen {

// The race's music cues (music §2-3), every frame.
void RaceScreen::music_cues(App& app) {
    RaceMusic::Cues c;
    c.music_on = replaying_ || demo_ || attract_ ? preset_.music && view_settings().bgm_on() : music_on_;
    c.menu_music_on = menu_music_on_;
    c.replay = replaying_;
    c.songs = music_.playlist().has_songs();
    bool lic_replay = !license_event_.empty() && !attract_ && (replaying_ || demo_);
    c.kind = lic_replay                       ? RaceMusic::Kind::LicenseReplay
             : demo_ || attract_              ? RaceMusic::Kind::Demo
             : !license_event_.empty()        ? RaceMusic::Kind::License
             : two_player_                    ? RaceMusic::Kind::TwoPlayer
                                              : RaceMusic::Kind::Race;
    c.paused = paused_;
    c.start_type = field_ ? music_start_type_ : tt_start_.type;
    // Held until the start signal, where the race music starts (music §2.2): a standing start's
    // hold, the computer's 300 ticks of a rolling start, a flying start's run-up to the line.
    // Replays re-run the start sequence (music §2.5): the start jingle over the hold, the race
    // song from the signal.
    c.holding = field_ ? field_->race_time() < 0 || rolling_runup()
                       : tt_hold_ > 0 || (flying_runup() && !demo_ && !attract_);
    c.finished = finished_ || finished2_;
    if (replaying_ && field_ && !two_player_) { // the car the cameras follow, at its finish
        const int n = field_->opponents() + 1;
        std::vector<bool> done(size_t(n), false);
        for (const RaceField::Standing& s : field_->standings())
            if (s.car >= -1 && s.car + 1 < n) done[size_t(s.car + 1)] = s.finished;
        c.finished = replay_finish_.update(done, replay_focus_ + 1);
    }
    c.position = 1; // every finish plays the jingle; in 2P the first one is the winner's
    // The fail jingle: a licence run without a medal (a FAIL, a time slower than bronze: GT3's failed event for both,
    // race::license_fail_jingle), a voided time trial.
    c.failed = tt_failed_ || (!license_event_.empty() && license_.state() != LicenseRun::State::Running &&
                              race::license_fail_jingle(license_.state(), shown_medal_));
    c.medal = license_event_.empty() ? -1 : shown_medal_;
    if (lic_replay) { // the replayed / demo run's own end: as its race ends it (demo) or the live run's end time (replay)
        const sim::exact::ExactRace* er = demo_ && exact_field() ? exact_race_of(*exact_field()) : nullptr;
        const bool demo_end = er && !er->cars().empty() && er->cars()[0].finished != 0;
        const bool replay_end = replaying_ && music_license_end_s_ >= 0 && race_time_ >= music_license_end_s_;
        c.finished = demo_end || replay_end;
        // A demonstration: its run's judged result (2..4 a medal; 0 slower than bronze, -1 out of the course: failed);
        // a replay: what its live run played.
        c.failed = demo_end ? er->kind() == sim::exact::Conductor::License &&
                                  race::license_fail_jingle(race::license_outcome(er->flow().result, er->flow().result_time, true, -1))
                            : music_license_failed_;
    }
    c.since_finish = finish_wait_;
    bool results = (field_ || two_player_) && license_event_.empty() && !machine_;
    c.menu = !quick_ ? RaceMusic::Menu::None
             : prerace_ || !results ? RaceMusic::Menu::Short
                                    : RaceMusic::Menu::Result;
    music_.cues(app.audio(), *app.vfs(), c);
    // The playlist carries over to the next race: saved whenever a song was taken.
    if (std::optional<std::string> pl = music_.take_playlist_change()) {
        app.config().set("options", "music_playlist", *pl);
        app.save_config();
    }
}

// CRSS course ambience: one listener per screen, every car's track distance.
void RaceScreen::update_course_ambience(App& app) {
    if (!crss_ok_) return;
    auto listener = [&](const CameraRig& cam, const sim::VehicleState& car) {
        CourseAmbience::Listener l;
        render::Vec3 e = cam.eye();
        sim::Vec3 r = cam_right(cam);
        l.pos = {e.x, e.y, e.z};
        l.velocity = {car.velocity.x, car.velocity.y, car.velocity.z};
        l.right = {r.x, r.y, r.z};
        sim::SurfaceHit h = track_->query().probe(car.position.x, car.position.z, car.position.y + 2.0f);
        l.surface_flags = h.hit ? (h.surface >> 8) : 0;
        return l;
    };
    std::vector<CourseAmbience::Listener> ls{listener(camera_, render_state_)};
    std::vector<float> dist{track_->distance_of(render_state_.position)};
    if (field_ && field_->has_second()) {
        ls.push_back(listener(camera2_, render_state2_));
        dist.push_back(track_->distance_of(field_->second_state().position));
    }
    if (const RaceField* f = exact_field()) // (the opponents on the course: a race's, a free practice's)
        for (int i = 0; i < f->opponents(); ++i) dist.push_back(track_->distance_of(f->opponent_state(i).position));
    crss_.update(ls, dist);
    crss_player_.update(app.audio().voices(), crss_);
}

// Every car runs the tyre routine (sound §7.6): its wheels' loops placed from the camera with the
// view's gains (the followed car's, or the other cars'), and the followed cars' collision one-shots.
void RaceScreen::update_tyre_sounds(App& app, const sim::VehicleState& s) {
    VoiceMixer& mixer = app.audio().voices();
    auto step = [](const CameraRig& cam, const sim::VehicleState& st, const sim::VehicleSetup& setup,
                   const sim::VehicleTelemetry* t, SoundView view) {
        CarSfx::Step p;
        p.state = &st;
        p.setup = &setup;
        p.telemetry = t;
        p.view = view;
        p.eye = to_sim(cam.eye());
        p.right = cam_right(cam);
        p.forward = cam_forward(cam);
        return p;
    };
    const bool tv = camera_.mode() == CameraMode::Tv || replaying_;
    CarSfx::Step p1 = step(camera_, s, vehicle_->setup(), vehicle_->telemetry(),
                           camera_.onboard() ? SoundView::InCarTarget : tv ? SoundView::Other : SoundView::ChaseTarget);
    // The listener moves with the followed car (as for the other cars' engines, sound §8).
    sim::Vec3 listener_v = s.velocity;
    if ((replaying_ || attract_) && field_ && replay_focus_ >= 0 && replay_focus_ < field_->opponents())
        listener_v = field_->opponent_state(replay_focus_).velocity;
    auto doppler = [&](const sim::VehicleState& st, const sim::Vec3& eye) {
        sim::Vec3 d = st.position - eye;
        float n = d.length();
        return OpponentEngines::doppler(n > 1e-3f ? d * (1.0f / n) : sim::Vec3{0, 0, 1}, st.velocity, listener_v);
    };
    p1.doppler = doppler(s, p1.eye);
    p1.wall_impulse = replaying_ ? 0.0f : vehicle_->wall_impulse();
    p1.car_impulse = replaying_ ? 0.0f : vehicle_->car_impulse();
    car_sfx_.update(sfx_, mixer, p1);
    if (field_ && field_->has_second()) {
        CarSfx::Step p2 = step(camera2_, field_->second_state(), field_->second_setup(), field_->second_telemetry(),
                               camera2_.onboard() ? SoundView::InCarTarget : SoundView::ChaseTarget);
        p2.wall_impulse = field_->second_wall_impulse();
        p2.car_impulse = field_->second_car_impulse();
        car_sfx2_.update(sfx_, mixer, p2);
    }
    // The other cars: the "other car" gains, placed and attenuated (32/d) like the followed car's.
    const RaceField* f = exact_field();
    const int n = f ? f->opponents() : 0;
    if (int(opp_sfx_.size()) != n) {
        for (CarSfx& c : opp_sfx_) c.stop(mixer);
        opp_sfx_.clear();
        opp_sfx_.resize(size_t(n));
    }
    const SoundView other = camera_.onboard() ? SoundView::InCarOther : tv ? SoundView::Other : SoundView::ChaseOther;
    for (int i = 0; i < n; ++i) {
        const sim::VehicleState& os = f->opponent_state(i);
        CarSfx::Step p = step(camera_, os, f->opponent_setup(i), f->opponent_telemetry(i), other);
        p.doppler = doppler(os, p.eye);
        opp_sfx_[size_t(i)].update(sfx_, mixer, p);
    }
}

RaceScreen::SoundCar RaceScreen::car_sound_facts(const std::string& label) const {
    SoundCar c;
    std::string base = db_.car(label) ? label : "";
    if (base.empty())
        if (const data::CarVariant* v = db_.variant(label)) base = v->car;
    const data::Car* car = base.empty() ? nullptr : db_.car(base);
    if (!car) return c;
    for (size_t i = 0; i < data::kCarPartTables.size(); ++i) {
        int t = data::kCarPartTables[i];
        if (t != int(data::TableId::RacingModify) && t != int(data::TableId::Gear)) continue;
        const ByteSpan* rec = db_.record(data::TableId(t), car->parts[i]);
        if (!rec) continue;
        if (t == int(data::TableId::RacingModify) && rec->size() > 0x39 && (*rec)[0x39])
            c.cd = float((*rec)[0x39]) / 100.0f;
        if (t == int(data::TableId::Gear) && rec->size() > 0x10) c.whine = (*rec)[0x10] != 0;
    }
    return c;
}

CarExtraSounds::Frame RaceScreen::extra_frame(const CarFacts& facts, const sim::VehicleTelemetry& t, const sim::VehicleState& s,
                                              const SoundCar& car, bool onboard, float x, float z, SoundView view) {
    CarExtraSounds::Frame f;
    f.u = s.speed_kmh / 3.6f;
    f.slipstream = t.slipstream;
    f.cd = car.cd;
    f.whine = car.whine;
    f.clutch = t.clutch > 0.01f ? 1 : 0;
    f.torque = t.engine_torque;
    const std::vector<float>& r = facts.ratios;
    if (!r.empty()) {
        f.first_ratio = r[0];
        f.ratio = s.gear >= 1 && size_t(s.gear) <= r.size() ? r[size_t(s.gear - 1)] : s.gear < 0 ? facts.reverse_ratio : 0.0f;
    }
    // Driven axle speed: rear, front, or the mean of all four (4WD).
    float front = 0.5f * (std::fabs(s.wheels[0].angular_velocity) + std::fabs(s.wheels[1].angular_velocity));
    float rear = 0.5f * (std::fabs(s.wheels[2].angular_velocity) + std::fabs(s.wheels[3].angular_velocity));
    sim::Drive d = facts.drive;
    f.shaft = d == sim::Drive::FF ? front : d == sim::Drive::AWD ? 0.5f * (front + rear) : rear;
    if (onboard) x = 0, z = 1; // the followed car's own sounds sit in front in the in-car view
    f.x = x, f.z = z;
    f.view = view;
    return f;
}

void RaceScreen::silence(App& app) {
    engine_.stop(app.audio().voices());
    engine2_.stop(app.audio().voices());
    opp_engines_.stop(app.audio().voices());
    car_sfx_.stop(app.audio().voices());
    car_sfx2_.stop(app.audio().voices());
    for (CarSfx& c : opp_sfx_) c.stop(app.audio().voices());
    extra_.stop(sfx_, app.audio().voices());
    extra2_.stop(sfx_, app.audio().voices());
    crss_player_.stop(app.audio().voices());
    sfx_.stop_all(app.audio().voices());
    app.input().set_rumble(1, {});
    app.input().set_trigger_rumble(1, 0, 0);
    app.input().set_rumble(0, {});
    app.input().set_trigger_rumble(0, 0, 0);
}

} // namespace gt3r::race_screen
