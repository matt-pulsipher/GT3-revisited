#include "race/backend.h"

#include "base/log.h"
#include "formats/course/course_file.h"
#include "formats/model/wheel_spec.h"
#include "game/data/car_database.h"
#include "track/track.h"

#include <algorithm>
#include <cmath>

namespace gt3r::race {

namespace {

u64 part_id(const data::Car& car, data::TableId table) {
    for (size_t i = 0; i < data::kCarPartTables.size(); ++i)
        if (data::kCarPartTables[i] == int(table)) return car.parts[i];
    return 0;
}

// The surface under a point: the road mesh, else the ground estimate (the screen's probes for kerbs, the off-road rumble,
// pylons and smoke).
class GroundQuery : public sim::TrackQuery {
public:
    explicit GroundQuery(const track::Track& t) : t_(t) {}
    sim::SurfaceHit probe(float x, float z, float y_max) const override {
        sim::SurfaceHit h = t_.probe_or_ground(x, z, y_max);
        h.hit = true; // probe_or_ground reports misses with hit = false but a usable height
        return h;
    }

private:
    const track::Track& t_;
};

class TrackAdapter : public RaceTrack {
public:
    explicit TrackAdapter(std::unique_ptr<track::Track> t) : t_(std::move(t)), ground_(*t_), lap_(*t_) {}
    const char* backend_name() const override { return "track::Track"; }
    const sim::TrackQuery& query() const override { return ground_; }
    float length() const override { return t_->length(); }
    int grid_count() const override { return std::max(1, t_->grid_count()); }
    TrackPose grid_pose(int slot) const override {
        track::Pose p = t_->grid_pose(slot);
        return {p.position, p.orientation};
    }
    sim::Vec3 point_at(float s, float lateral) const override { return t_->point_at(s, lateral); }
    float distance_of(const sim::Vec3& p) const override { return t_->locate(p).distance; }
    bool is_kerb(u32 surface) const override { return track::surface_type(surface) == track::Surface::Kerb; }
    bool is_offroad(const sim::SurfaceHit& hit) const override {
        track::Surface s = track::surface_type(hit.surface);
        return hit.segment < 0 || s == track::Surface::Grass || s == track::Surface::Gravel ||
               s == track::Surface::OffRoad;
    }
    void set_lap_source(const LapSource* src) override { src_ = src; }
    // (The track's lap timer runs on with a lap source: its distance still places the cameras.)
    void lap_reset(double t) override { lap_.reset(t); }
    bool lap_update(const sim::Vec3& p, double t) override {
        const bool done = lap_.update(p, t);
        return src_ ? src_->lap_completed() : done;
    }
    int laps_completed() const override { return src_ ? src_->laps_completed() : lap_.laps_completed(); }
    bool lap_started() const override { return src_ ? src_->lap_started() : lap_.lap_started(); }
    double current_lap_time(double now) const override { return src_ ? src_->current_lap_time(now) : lap_.current_lap_time(now); }
    double last_lap_time() const override { return src_ ? src_->last_lap_time() : lap_.last_lap_time(); }
    double best_lap_time() const override { return src_ ? src_->best_lap_time() : lap_.best_lap_time(); }
    float lap_progress() const override { return src_ ? src_->lap_progress() : lap_.lap_progress(); }
    bool wrong_way() const override { return src_ ? src_->wrong_way() : lap_.wrong_way(); }
    void lap_set_lap1_from_go(bool from_go, bool before_line) override { lap_.set_lap1_from_go(from_go, before_line); }
    void lap_freeze() override { lap_.freeze(); }
    std::vector<LapCrossing> lap_crossings() const override {
        if (src_) return src_->lap_crossings();
        std::vector<LapCrossing> out;
        for (const track::LapTimer::Crossing& c : lap_.crossings()) out.push_back({c.point, c.line, c.lap, c.ms, c.split_ms});
        return out;
    }
    std::vector<float> split_points() const override { return lap_.checkpoints(); }
    const track::Track& track() const { return *t_; }
    const track::Track* runtime() const override { return t_.get(); }
    double race_distance() const override { return src_ ? src_->race_distance() : lap_.race_distance(); }

private:
    const LapSource* src_ = nullptr;
    std::unique_ptr<track::Track> t_;
    GroundQuery ground_;
    track::LapTimer lap_;
};

} // namespace

CarSetup car_setup_from_database(const data::CarDatabase& db, const std::string& label) {
    CarSetup s;
    s.label = label;
    const data::Car* car = db.car(label);
    if (!car) return s;
    s.name = car->latin_name; // the app's own screens draw it (Car::latin_name)
    s.model = db.model_of(*car);
    if (auto en = db.engine(part_id(*car, data::TableId::Engine))) {
        if (en->rpm_limit_x100) s.limit_rpm = en->rpm_limit_x100 * 100.0f;
        else if (!en->rpm.empty()) s.limit_rpm = float(en->rpm.back());
    }
    if (auto gb = db.gearbox(part_id(*car, data::TableId::Gear))) {
        for (size_t i = 0; i < gb->gears && i < gb->ratios.size(); ++i)
            if (gb->ratios[i] > 0) s.ratios.push_back(gb->ratios[i]);
        if (gb->final_drive > 0) s.final_drive = gb->final_drive;
    }
    if (auto ws = wheel_spec_for_car(db, label)) s.wheels = *ws;
    s.body_mod = body_mod_for_car(db, label);
    data::CarDatabase::EngineSound es = db.engine_sound(*car);
    s.sound_id = es.id;
    s.sound_variant = es.variant;
    return s;
}

std::unique_ptr<RaceTrack> make_track(const Vfs& vfs, ByteSpan course_file, const std::string& label) {
    (void)vfs;
    return std::make_unique<TrackAdapter>(track::Track::from_course_file(course_file, label));
}

} // namespace gt3r::race
