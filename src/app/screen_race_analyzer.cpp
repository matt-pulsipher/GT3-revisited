// The Analyzer (data logger, race/analyzer.h): the quick menu's DATA LOGGER over the
// session's laps, and the Replay Theater's logger mode (two replays' races played with the
// data logger, REPLAY with the second's lap as the ghost).
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"

namespace gt3r::race_screen {

void RaceScreen::load_analyzer(App& app) {
    if (!analyzer_loaded_) analyzer_loaded_ = true, analyzer_.load(*app.vfs(), app.renderer(), hud_.fonts(), lang_);
    analyzer_.imperial = imperial_;
    // The course's data-logger map (picture, sector points).
    std::optional<LoggerMap> map;
    const std::string label = courses_[size_t(course_sel_)].label;
    if (std::optional<Bytes> b = app.vfs()->read("logger/" + label)) map = LoggerMap::parse(*b);
    float len = track_ ? track_->length() : 0.0f;
    std::vector<float> sectors;
    if (map && track_) sectors = logger_sectors(*map, [this](float x, float y, float z) { return track_->distance_of({x, y, z}); }, len);
    // A replay: the data logger's sectors, the course's logger marks (the road's markers of group 1), on the race's own
    // distance.
    if (replaying_ && replay_.exact && load_program_data(app)) {
        try {
            const std::shared_ptr<const sim::exact::RaceCourse> c = exact_course(course_bytes_, label, *exact_data_);
            const sim::exact::CourseQuery& q = *c->query;
            len = q.lap_length().host();
            sectors = {0.0f};
            for (int k = 0; k < q.logger_marks() && sectors.size() < 40; ++k) sectors.push_back(q.logger_mark(k).host());
        } catch (const std::exception& e) {
            LOG_WARN("Analyzer: %s", e.what());
        }
    }
    analyzer_.set_course(app.renderer(), std::move(map), std::move(sectors), len);
}

// GT3's Analyzer pass over a replay: its race played with the data logger recording (race info
// + 0x70 / + 0x74), car 0 binned after every step (LoggerBinner), the logger table's sector times; its states from the
// lap's start for the ghost.
AnalyzerTrack RaceScreen::exact_logger_track(App& app, const Replay& r) {
    if (!r.exact || !track_ || !load_program_data(app)) return {};
    FieldConfig cfg;
    cfg.event = r.event;
    cfg.opponents = r.opponents;
    ExactFieldOptions o = exact_options(app);
    o.playback = r.exact;
    o.ghost = nullptr, o.free_run = nullptr;
    o.kind = r.exact->kind;
    o.logger = o.logger_record = true;
    cfg.exact = std::move(o);
    std::string err;
    std::unique_ptr<RaceField> f = RaceField::create(*track_, db_, cfg, &err);
    const sim::exact::ExactRace* race = f ? exact_race_of(*f) : nullptr;
    if (!race) {
        LOG_WARN("Analyzer: the replay's race cannot be built (%s)", err.c_str());
        return {};
    }
    LoggerBinner bins(race->course().query->lap_length());
    std::vector<sim::VehicleState> states;
    for (u32 k = 0; k < r.exact->frames; ++k) {
        f->player().step({}, 1.0 / f->tick_hz());
        bins.add(race->cars()[0], double(race->flow().clock) / 3000.0, k + 1);
        states.push_back(f->player().state());
    }
    bins.finish();
    const auto* tables = exact_logger_tables(*f);
    AnalyzerTrack t = logger_track(bins, tables && !tables->empty() ? &(*tables)[0] : nullptr, f->tick_hz());
    for (size_t k = t.lap_begin ? t.lap_begin - 1 : 0; k < states.size() && k < t.lap_end; ++k) t.ghost.add(states[k]);
    LOG_INFO("Analyzer: the logger's pass over %u frames: %zu bins of %.3f m, %zu sectors timed, lap %.3f s", r.exact->frames,
             bins.bins().size(), double(bins.width().host()), t.sector_times.size(), t.trace.time);
    return t;
}

// GT3's Analyzer race on the exact core (over the logger race, the solo race's devices both playing): track A's
// replay with track B's car as its ghost car (car 1) playing track B's recording, the time trial, licence test and machine test;
// the race played with the data logger recording (race info + 0x70 / + 0x74) until track A's recording ends, car 0 binned for
// track A and car 1 for track B after every step (LoggerBinner, frames by the cars' input devices), the logger's tables' sector
// times; the tables kept for the REPLAY view's jumps (exact_analysis_jump).
bool RaceScreen::exact_analysis_pass(App& app, const Replay& a, std::shared_ptr<const sim::exact::RaceRecord> b, AnalyzerTrack& ta,
                                     AnalyzerTrack& tb) {
    using K = sim::exact::Conductor;
    if (!a.exact || !track_ || !load_program_data(app)) return false;
    const K kind = a.exact->kind;
    if (kind != K::TimeTrial && kind != K::License && kind != K::MachineTest) return false; // (the free run: separate passes)
    FieldConfig cfg;
    cfg.event = a.event;
    cfg.opponents = a.opponents;
    ExactFieldOptions o = exact_options(app);
    o.playback = a.exact;
    o.ghost = nullptr, o.free_run = nullptr;
    o.kind = kind;
    o.logger = o.logger_record = true;
    o.analysis = true, o.analysis_b = b, o.logger_tables = nullptr;
    cfg.exact = std::move(o);
    std::string err;
    std::unique_ptr<RaceField> f = RaceField::create(*track_, db_, cfg, &err);
    const sim::exact::ExactRace* race = f ? exact_race_of(*f) : nullptr;
    if (!race || !race->recorders()) {
        LOG_WARN("Analyzer: the logger's race cannot be built (%s)", err.c_str());
        return false;
    }
    const bool two = b && race->size() > 1 && race->ghost_car() == 1;
    LoggerBinner ba(race->course().query->lap_length()), bb(race->course().query->lap_length());
    analysis_steps_.clear();
    const u32 most = a.exact->frames + 600; // (past the recording: its playback ends it)
    for (u32 k = 0; k < most && !race->recorders()->player.rec.done; ++k) {
        f->player().step({}, 1.0 / f->tick_hz());
        const double t = double(race->flow().clock) / 3000.0;
        ba.add(race->cars()[0], t, exact_device_frame(*f, 0));
        if (two) bb.add(race->cars()[1], t, exact_device_frame(*f, 1));
        analysis_steps_.push_back(exact_device_frame(*f, 0));
    }
    ba.finish(), bb.finish();
    const auto* tables = exact_logger_tables(*f);
    ta = logger_track(ba, tables && !tables->empty() ? &(*tables)[0] : nullptr, f->tick_hz());
    if (two) tb = logger_track(bb, tables && tables->size() > 1 ? &(*tables)[1] : nullptr, f->tick_hz());
    analysis_tables_ = tables ? std::make_shared<const std::vector<std::map<s32, sim::exact::Snapshot>>>(*tables) : nullptr;
    LOG_INFO("Analyzer: the logger's race over %zu steps: track A %zu bins (lap %.3f s), track B %zu bins (lap %.3f s)",
             analysis_steps_.size(), ba.bins().size(), ta.trace.time, two ? bb.bins().size() : size_t(0), two ? tb.trace.time : 0.0);
    return true;
}

// The REPLAY view's jump to the logger's entry k (the sector starting at its mark), on from the step of the pass
// that read the entry's frame.
void RaceScreen::exact_analysis_jump(App& app, int k) {
    (void)app;
    RaceField* f = exact_field();
    if (!f || !analysis_tables_ || analysis_tables_->empty() || (*analysis_tables_)[0].empty()) {
        LOG_WARN("Analyzer: REPLAY: no logger's race or table to jump in (field %d, tables %d, %zu, entries %zu)", f ? 1 : 0,
                 analysis_tables_ ? 1 : 0, analysis_tables_ ? analysis_tables_->size() : size_t(0),
                 analysis_tables_ && !analysis_tables_->empty() ? (*analysis_tables_)[0].size() : size_t(0));
        return;
    }
    const std::map<s32, sim::exact::Snapshot>& table = (*analysis_tables_)[0];
    auto it = table.lower_bound(k);
    if (it == table.end()) it = std::prev(table.end());
    k = it->first;
    const u32 frame = it->second.frame;
    u32 step = 0;
    while (step < analysis_steps_.size() && analysis_steps_[step] < frame) ++step;
    if (!exact_logger_jump(*f, k, step + 1)) {
        LOG_WARN("Analyzer: REPLAY: the jump to the logger's entry %d failed", k);
        return;
    }
    player_.seek(step + 1);
    LOG_INFO("Analyzer: REPLAY from the logger's entry %d (track A's frame %u, the pass's step %u)", k, frame, step + 1);
    render_state_ = prev_ = vehicle_->state();
    camera_.snap(render_state_);
}

// Quick menu DATA LOGGER: A the session's last lap, B its best (GRAPH).
void RaceScreen::open_session_analyzer(App& app) {
    load_analyzer(app);
    AnalyzerTrack a, b;
    a.trace = logger_.last(), b.trace = logger_.best();
    a.hz = b.hz = 1.0 / step_.step();
    a.title = b.title = car_.name;
    analyzer_.set_tracks(std::move(a), std::move(b));
    analyzer_.open(Analyzer::View::Graph);
}

// The theater's Analyzer: track A (the replay being played) and track B (the second file,
// same course) played to get their laps; REPLAY plays A's lap with B as the ghost.
void RaceScreen::setup_logger(App& app) {
    load_analyzer(app);
    const Replay ra = replay_; // (playing B replaces it)
    AnalyzerTrack a, b;
    std::optional<Replay> rb;
    if (!launch_.request.replay_file2.empty())
        if (rb = read_replay_file(launch_.request.replay_file2); rb && rb->course != ra.course) rb.reset();
    // Tracks A and B in one race as GT3's Analyzer runs them (time trials, licence tests, machine tests); otherwise a pass
    // over each replay.
    analysis_ = false, analysis_b_ = nullptr, analysis_tables_ = nullptr;
    if ((!rb || rb->exact) && exact_analysis_pass(app, ra, rb ? rb->exact : nullptr, a, b)) {
        analysis_ = true, analysis_b_ = rb ? rb->exact : nullptr;
        if (rb) b.title = car_display_name(rb->car);
    } else {
        if (rb) {
            b = exact_logger_track(app, *rb);
            b.title = car_display_name(rb->car);
        }
        a = exact_logger_track(app, ra);
    }
    a.title = car_.name;
    logger_begin_ = a.lap_begin, logger_end_ = a.lap_end;
    logger_ghost_ = analysis_ ? GhostLap{} : b.ghost; // (the Analyzer's race draws track B's car itself)
    LOG_INFO("Analyzer: track A %zu samples (%.3f s, %.0f-%.0f m), track B %zu samples", a.trace.size(), a.trace.time,
             a.valid() ? a.trace.dist.front() : 0.0f, a.valid() ? a.trace.dist.back() : 0.0f, b.trace.size());
    analyzer_.set_tracks(std::move(a), std::move(b));
    analyzer_.open(launch_.request.logger_view == 1 ? Analyzer::View::Graph : Analyzer::View::Replay);
    logger_seek(app, std::max(0.0f, preset_.lcursor));
    if (preset_.lcursor >= 0) analyzer_.set_cursor(preset_.lcursor);
}

// REPLAY: track A's replay from its lap's start to the sample at distance `d`; GT3's Analyzer race (analysis_) jumps to the
// sector the distance is in (its logger entry: the sector from its mark).
void RaceScreen::logger_seek(App& app, float d) {
    if (analysis_) {
        if (!replaying_ || !exact_field() || !exact_analysis_field(*exact_field()))
            start_playback(Replay(replay_)); // (the REPLAY view's race: track A's replay as the logger's race)
        const std::vector<float>& s = analyzer_.sector_starts();
        int k = 0;
        while (size_t(k + 1) < s.size() && s[size_t(k + 1)] <= d) ++k;
        exact_analysis_jump(app, k);
        silence(app);
        return;
    }
    const AnalyzerTrack& a = analyzer_.track(0);
    const u32 target = a.valid() ? a.frame_at_distance(d, logger_begin_) : logger_begin_;
    start_playback(Replay(replay_));
    while (player_.tick() < target && !player_.done()) tick(app, PadState{});
    silence(app);
    render_state_ = prev_ = vehicle_->state();
    camera_.snap(render_state_);
}

// The theater's Analyzer, every frame: true while it runs (the race loop does nothing else).
bool RaceScreen::update_logger(App& app, double dt) {
    logger_acc_ += dt;
    bool open = true;
    while (open && logger_acc_ >= 1.0 / 60.0) {
        logger_acc_ -= 1.0 / 60.0;
        open = analyzer_.tick(menu_keys(app.input().pad(0)));
        if (!quick_sounds_tried_) {
            quick_sounds_tried_ = true;
            quick_sounds_.load(*app.vfs());
        }
        for (const std::string& se : analyzer_.take_sounds()) quick_sounds_.play(app.audio().voices(), se);
    }
    if (!open) {
        leave(app); // back to the Analyzer's top page
        return true;
    }
    if (analyzer_.view() == Analyzer::View::Graph) {
        silence(app);
        return true;
    }
    if (std::optional<float> s = analyzer_.take_seek()) logger_seek(app, *s);
    for (int i = analyzer_.take_camera_presses(); i > 0; --i) next_view(camera_, 0);
    const double rate = analyzer_.playing() ? (analyzer_.slow() ? 0.25 : 1.0) : 0.0;
    int n = rate > 0 ? step_.advance(dt * rate) : 0;
    for (int i = 0; i < n; ++i) {
        const u32 at = analysis_ && exact_field() ? exact_device_frame(*exact_field(), 0) : player_.tick() + 1;
        if (at >= logger_end_ || player_.done()) { // loop the lap
            logger_seek(app, 0);
            break;
        }
        tick(app, PadState{});
    }
    render_state_ = interpolate(prev_, vehicle_->state(), step_.alpha());
    camera_.set_track_distance(track_->distance_of(render_state_.position));
    camera_.set_lap(lap_count());
    camera_.update(render_state_, vehicle_->setup(), PadState{}, dt);
    const AnalyzerTrack& a = analyzer_.track(0);
    if (a.valid())
        analyzer_.set_cursor(a.distance_at_frame(analysis_ && exact_field() ? exact_device_frame(*exact_field(), 0) : player_.tick(),
                                                 logger_begin_));
    if (!analyzer_.playing()) silence(app);
    return true;
}

} // namespace gt3r::race_screen
