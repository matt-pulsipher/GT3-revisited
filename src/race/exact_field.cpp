// A race on the exact core; see exact_field.h.

#include "race/exact_field.h"

#include "base/log.h"
#include "game/data/car_database.h"
#include "race/exact_car.h"
#include "race/exact_demo.h"
#include "race/interp.h"
#include "race/exact_setup.h"
#include "race/net_race.h"
#include "sim/exact/pad_read.h"
#include "sim/exact/race.h"
#include "sim/exact/race_record.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <functional>
#include <cmath>
#include <functional>
#include <utility>

namespace gt3r::race {

namespace {

// The race pad (the player's race layout, buttons in GT3's type A meanings) as the device object holds it after its read:
// the analog pad with pressure buttons (pad type 8), or the steering wheel's axes (the wheel on the stick's x, the pedals on
// Cross and Square).
// A race pad button's pressure (held without a pressure report: all the way).
u8 pressure(const PadState& p, PadButton b) {
    if (!p.down(b)) return 0;
    const u8 v = p.press(b);
    return v ? v : 255;
}

ex::PadReading pad_reading(const PadState& p, const ExactHuman& h) {
    const bool wheel = h.wheel;
    ex::PadReading r;
    ex::type_a_channels(r);
    auto press = [&](PadButton b) { return pressure(p, b); };
    u16 bits = 0;
    if (p.down(PadButton::Left)) bits |= 0x100;
    if (p.down(PadButton::Right)) bits |= 0x200;
    if (p.down(PadButton::Cross)) bits |= 0x400;
    if (p.down(PadButton::Square)) bits |= 0x800;
    if (p.down(PadButton::Circle)) bits |= 0x1000;
    if (p.down(PadButton::Triangle)) bits |= 0x2000;
    if (p.down(PadButton::R2)) bits |= 0x4000;
    if (p.down(PadButton::L2)) bits |= 0x8000;
    r.buttons = bits;
    r.channel = {p.axis(PadAxis::RX), p.axis(PadAxis::RY), p.axis(PadAxis::LX), p.axis(PadAxis::LY),
                 press(PadButton::Right), press(PadButton::Left), press(PadButton::Up), press(PadButton::Down),
                 press(PadButton::Triangle), press(PadButton::Circle), press(PadButton::Cross), press(PadButton::Square),
                 press(PadButton::L1), press(PadButton::R1), press(PadButton::L2), press(PadButton::R2)};
    if (wheel) {
        r.type = 0, r.wheel = 1;
        r.wheel_pos = u16(p.axis(PadAxis::LX) * 4);
        r.wheel_throttle = u8(255 - press(PadButton::Cross));
        r.wheel_brake = u8(255 - press(PadButton::Square));
    }
    return r;
}

// The frame value (1..4096) whose value after phase 3's shaping `shape` (of value / 4096) is nearest u in (0, 1]; 0 for 0.
u16 frame_value(float u, const std::function<ex::F(ex::F)>& shape) {
    if (!(u > 0)) return 0;
    auto at = [&](int v) { return shape(ex::F(float(v) / 4096.0f)).host(); };
    int lo = 1, hi = 4096; // the first value whose shaped value reaches u
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (at(mid) < u) lo = mid + 1;
        else hi = mid;
    }
    if (lo > 1 && u - at(lo - 1) < at(lo) - u) --lo;
    return u16(lo);
}

// A player's frame from the race pad: the pad as GT3 reads it (ExactHuman::original_response), or, by default, the same
// frame with its analog axes linear in the race pad's: the stick's (or the wheel's) x straight to the steering, the
// d-pad's pressures to it as GT3 steers with the d-pad (kind 3), and each pressure (Cross, Square, Circle, Triangle)
// to the frame value whose throttle, brake, handbrake or back axis after phase 3's shaping (its analog step x + c (1 - x),
// a wheel's pedal curves) is that pressure's fraction (from c on: the shaping's own least step).
ex::PadFrame pad_frame(const PadState& p, const ExactHuman& h, const ex::ProgramData& d, const ex::PadConstants& k) {
    ex::PadFrame f = ex::pad_read(pad_reading(p, h), d);
    if (h.original_response) return f;
    const ex::F one = ex::F::raw(0x3F800000);
    const std::function<ex::F(ex::F)> plain = [&](ex::F x) { return x + k.analog_jump * (one - x); };
    const std::function<ex::F(ex::F)> wheel_throttle = [&](ex::F x) { return ex::pedal_curve(k.wheel_throttle, plain(x)); };
    const std::function<ex::F(ex::F)> wheel_brake = [&](ex::F x) { return ex::pedal_curve(k.wheel_brake, plain(x)); };
    if (f.kind == 3) {
        const int s = int(pressure(p, PadButton::Left)) - int(pressure(p, PadButton::Right));
        f.steer = s16(std::lround(float(s) * 4096.0f / 255.0f));
    } else if (f.kind == 2 || f.kind == 4) {
        const int lx = p.axis(PadAxis::LX);
        const float v = lx < 128 ? float(128 - lx) / 128.0f : -float(lx - 128) / 127.0f; // left positive
        f.steer = s16(std::lround(v * 4096.0f));
    }
    auto pedal = [&](PadButton b, u16 ex::PadFrame::*field, u8 flag, const std::function<ex::F(ex::F)>& shape) {
        f.flags = u8(f.flags | flag);
        f.*field = frame_value(float(pressure(p, b)) / 255.0f, shape);
    };
    pedal(PadButton::Cross, &ex::PadFrame::throttle, 0x02, h.wheel ? wheel_throttle : plain);
    pedal(PadButton::Square, &ex::PadFrame::brake, 0x04, h.wheel ? wheel_brake : plain);
    pedal(PadButton::Circle, &ex::PadFrame::third, 0x10, plain);
    pedal(PadButton::Triangle, &ex::PadFrame::back, 0x08, h.wheel ? wheel_throttle : plain); // (the reverse throttle)
    return f;
}

// A frame from the screen's inputs where it gives no pad (scripts, the test autopilot, replays of inputs): every axis analog,
// the steering positive left; the reverse request on the back axis.
ex::PadFrame inputs_frame(const sim::ControlInputs& in, bool reverse) {
    ex::PadFrame f;
    f.flags = 0x1F;
    f.kind = in.controller_kind == 3 ? 3 : 2;
    auto axis = [](float v) { return u16(std::lround(std::clamp(v, 0.0f, 1.0f) * 4096.0f)); };
    f.steer = s16(std::lround(std::clamp(-in.steer, -1.0f, 1.0f) * 4096.0f));
    f.throttle = axis(in.throttle);
    f.brake = axis(in.brake);
    f.third = in.handbrake ? 4096 : 0;
    f.shift = in.shift_up ? s8(1) : in.shift_down ? s8(-1) : s8(0);
    if (reverse) f.back = 4096;
    return f;
}

class ExactField;

// Player 1's laps as the race flow times them (RaceTrack::set_lap_source).
class ExactLaps : public LapSource {
public:
    explicit ExactLaps(const ExactField& f) : f_(f) {}
    bool lap_completed() const override;
    int laps_completed() const override;
    bool lap_started() const override;
    double current_lap_time(double now) const override;
    double last_lap_time() const override;
    double best_lap_time() const override;
    float lap_progress() const override;
    bool wrong_way() const override;
    std::vector<LapCrossing> lap_crossings() const override;
    double race_distance() const override;

private:
    const ExactField& f_;
};

class ExactPlayer : public RaceVehicle {
public:
    ExactPlayer(ExactField& f, int car) : f_(f), car_(car) {}
    const char* backend_name() const override { return "sim::exact::ExactRace"; }
    void step(const sim::ControlInputs& in, double dt) override;
    const sim::VehicleState& state() const override;
    const sim::VehicleSetup& setup() const override;
    float boost() const override;
    float brake_pedal() const override;
    const sim::VehicleTelemetry* telemetry() const override;
    std::optional<CarFacts> facts() const override;
    int gear_hint(bool& blink) const override;
    int driver_kind() const override;
    void set_pad(const PadState* pad) override { pad_ = pad; }
    bool ai_take_over() override;
    void set_reverse_request(bool on) override { reverse_ = on; }
    void set_engine_condition(const sim::EngineCondition& c) override;
    sim::EngineCondition engine_condition() const override;
    float wall_impulse() const override;
    float car_impulse() const override;
    float last_impact() const override;

    const PadState* pad_ = nullptr;
    bool reverse_ = false;

private:
    ExactField& f_;
    int car_;
};

class ExactField : public RaceField {
public:
    // A car as the screen reads it, refreshed after every step.
    struct CarView {
        std::string label, model;
        sim::VehicleSetup setup;
        CarFacts facts;
        sim::VehicleState state, prev;
        sim::VehicleTelemetry tel;
        float wall = 0, car = 0, impact = 0; // this step's wall impulse, strongest car impulse, the stronger's speed change (m/s)
        double finish = -1;                  // race time of the finish (s)
    };

    explicit ExactField(const FieldConfig& cfg) : cfg_(cfg), opts_(cfg.exact), player_(*this, 0) {}
    // The visit's free run recorders as the run leaves them (the race's end closes its recordings).
    ~ExactField() override {
        if (visit_ && race_)
            if (const ex::SoloRecorders* s = race_->recorders()) *visit_ = *s, visit_->end();
    }

    bool build(const data::CarDatabase& db, std::string* error) {
        db_ = &db;
        data_ = opts_.data;
        if (opts_.rules && *opts_.rules != data_->behaviour().rules) { // (another release's rules: a copy that follows them)
            const sim::Behaviour how{*opts_.rules, 60};
            if (!data_->can_behave(how)) {
                if (error) *error = std::string(sim::program_name(how.rules)) + " rules on the " + sim::program_name(data_->program()) + " program";
                return false;
            }
            auto d = std::make_shared<ex::ProgramData>(*data_);
            d->set_behaviour(how);
            data_ = std::move(d);
        }
        const bool battle = opts_.kind == ex::Conductor::Battle2P;
        humans_ = battle ? 2 : 1;
        try {
            course_ = exact_course(opts_.course_file, opts_.course, *data_);
        } catch (const std::exception& e) {
            if (error) *error = std::string("exact course: ") + e.what();
            return false;
        }
        if (opts_.playback) return build_playback(db, error);
        const int slots = std::min(int(course_->query->grid().size()), 8); // (a race has at most 8 cars)
        if (slots < humans_) {
            if (error) *error = "the course has no start grid for the race";
            return false;
        }
        const data::Event* ev = cfg_.event.empty() ? nullptr : db.event(cfg_.event);
        const bool solo = exact_solo_kind(opts_.kind);
        std::vector<std::string> labels = cfg_.opponents;
        if (labels.empty() && ev && !battle) labels = ev->entries;
        if (solo) labels.clear(); // the player alone (and a ghost car)
        if (int(labels.size()) > cfg_.max_opponents) labels.resize(size_t(std::max(0, cfg_.max_opponents)));
        // Never more cars than the course's grid slots, as GT3's menus never make such a field (every event's cars fit its
        // course; the arcade's dirt races are one against one). The race start itself reads a slot past the grid table as the
        // program does (what follows the table in the road block: cars mostly off the road), but no game path asks for one.
        const int max_opp = slots - humans_;
        if (int(labels.size()) > max_opp) {
            LOG_WARN("Race (exact): %zu opponents on a %d-slot grid: %d race", labels.size(), slots, max_opp);
            labels.resize(size_t(max_opp));
        }
        // The opponents' equipment; the grid in the given order, or GT mode's by power-to-weight.
        struct Opp {
            std::string label;
            ex::Equipment eq;
            float kg_per_ps = 0;
        };
        std::vector<Opp> opps;
        for (const std::string& l : labels) {
            std::string err;
            std::optional<ex::Equipment> eq = exact_equipment(db, {l}, &err);
            if (!eq) {
                LOG_WARN("Race (exact): opponent %s: %s", l.c_str(), err.c_str());
                continue;
            }
            Opp o{l, *eq};
            if (cfg_.grid_by_power)
                if (std::optional<sim::CarSpec> sp = sim::build_car_spec(db, l)) o.kg_per_ps = power_to_weight(*sp);
            opps.push_back(std::move(o));
        }
        const int n_opp = int(opps.size());
        // The players behind the opponents (or on the given slot), player 2 next; the opponents on the other slots in their
        // order.
        int p1 = std::clamp(cfg_.player_slot >= 0 ? cfg_.player_slot : n_opp, 0, slots - 1);
        int p2 = p1 + 1 < slots ? p1 + 1 : p1 - 1;
        std::vector<int> free;
        for (int s = 0; s < slots; ++s)
            if (s != p1 && !(humans_ > 1 && s == p2)) free.push_back(s);
        std::vector<size_t> rank(opps.size());
        for (size_t i = 0; i < rank.size(); ++i) rank[i] = i;
        if (cfg_.grid_by_power)
            std::stable_sort(rank.begin(), rank.end(), [&](size_t a, size_t b) { return opps[a].kg_per_ps < opps[b].kg_per_ps; });
        std::vector<int> slot_of(opps.size());
        for (size_t r = 0; r < rank.size(); ++r) slot_of[rank[r]] = free[r];
        // The setup.
        ex::RaceSetup rs;
        rs.data = data_.get();
        rs.db = &db;
        rs.course = course_;
        static const std::vector<u8> kNoEvent(0x200, 0);
        rs.event = ev && ev->bytes.size() >= 0x1F4 ? ev->bytes : ByteSpan(kNoEvent.data(), kNoEvent.size());
        rs.course_category = exact_course_category(db, opts_.course);
        rs.kind = opts_.kind;
        rs.config = opts_.config;
        rs.laps = opts_.laps;
        if (solo) {
            // The licence test's and machine test's laps byte is their type: the event's. The race information's words.
            if (opts_.kind == ex::Conductor::License || opts_.kind == ex::Conductor::MachineTest || opts_.laps <= 0) rs.laps = -1;
            for (size_t k = 0; k < 4; ++k) rs.session.sector[k] = opts_.session[k];
            rs.session.lap = opts_.session[4];
            rs.licence_times = opts_.licence_times;
            rs.unjudged = opts_.unjudged;
            rs.speed_unit = opts_.speed_unit;
            rs.logger = opts_.logger ? 1 : 0, rs.logger_record = opts_.logger_record ? 1 : 0;
        }
        if (ev && ev->bytes.size() >= 0x1F4) {
            rs.start_type = ev->bytes[0x1F3];
            rs.countdown = ev->bytes[0x1F2];
        } else {
            rs.start_type = 2;
        }
        // The solo modes' start (the race's start-up: the time trial's and free run's start type a moving start, the base conductor's
        // none with the configuration's +0 set).
        if (solo)
            rs.start_type = ev && ev->bytes.size() >= 0x1F4 ? demo_start_type(*ev, opts_.kind)
                            : opts_.kind == ex::Conductor::TimeTrial || opts_.kind == ex::Conductor::FreeRun ? 3
                                                                                                                 : 2;
        if (!cfg_.remote.empty()) { // the i.LINK battle: four seconds on the grid, the race's laps, no tyre wear or catch-up
            rs.start_type = 1, rs.countdown = 0;
            rs.battle_laps = opts_.laps;
        }
        if (battle) {
            rs.start_type = -1; // the split-screen battle: no countdown
            rs.battle_laps = opts_.laps;
            rs.battle_wear = cfg_.battle_wear ? 2 : 0;
            rs.handicap = s32(std::lround(cfg_.handicap_m));
            rs.battle_catchup = std::clamp(cfg_.battle_catchup, 0, 2);
        }
        auto human = [&](const ExactHuman& h, int slot, std::string* err) -> std::optional<ex::StartEntry> {
            std::optional<ex::Equipment> eq = exact_equipment(db, h.car, err);
            if (!eq) return std::nullopt;
            ex::StartEntry e;
            e.eq = *eq;
            e.human = 1;
            e.player = true;
            e.grid_slot = slot;
            e.pedal_style = h.pedal_style;
            e.wheel_type = h.wheel ? 1 : 0;
            e.asm_on = h.asm_on, e.tcs_on = h.tcs_on;
            e.condition = exact_condition_words(h.condition, h.dirt);
            e.best_lap = 0x157529FF; // entry + 0x11D8: none (free practice keeps the lower of it and the car's)
            return e;
        };
        std::string err;
        std::optional<ex::StartEntry> e1 = human(opts_.player, p1, &err);
        if (!e1) {
            if (error) *error = "player car " + opts_.player.car.label + ": " + err;
            return false;
        }
        rs.entries.push_back(*e1);
        if (battle) {
            std::optional<ex::StartEntry> e2 = human(opts_.second, p2, &err);
            if (!e2) {
                if (error) *error = "second player car " + opts_.second.car.label + ": " + err;
                return false;
            }
            rs.entries.push_back(*e2);
        }
        bool ghost_entry = false;
        if (opts_.ghost && !opts_.ghost->entries.empty()) { // the whole-run ghost: its run's car 0, on the pad
            ex::StartEntry g = opts_.ghost->entries[0];
            g.human = 1, g.player = true;
            g.car = 0;
            rs.entries.push_back(g);
            rs.ghost_switch = 1; // (drawn translucent: the draw mode 2)
            ghost_entry = true;
        } else if (opts_.kind == ex::Conductor::FreeRun && opts_.free_run_ghost && slots >= 2) {
            // The free run's ghost car: the player's car once more on the next slot with an input device (the ghost's
            // playback), driven from the race's recorders; the start's entry set-up flags car 1 a ghost (F +0x3EA = 2).
            ex::StartEntry g = *e1;
            g.grid_slot = p2;
            rs.entries.push_back(g);
            rs.ghost_switch = opts_.ghost_shown ? 1 : 0;
            ghost_entry = true;
        }
        for (size_t i = 0; i < opps.size(); ++i) {
            ex::StartEntry e;
            e.eq = opps[i].eq;
            e.grid_slot = slot_of[i];
            e.best_lap = 0x157529FF;
            rs.entries.push_back(e);
        }
        // Online: the other players' cars after the opponents, each on its grid slot as the session's grid has it.
        std::vector<std::string> remote_cars; // the car each is drawn as
        for (const FieldConfig::Remote& r : cfg_.remote) {
            ExactHuman h;
            h.car = r.source;
            if (h.car.label.empty()) h.car.label = r.car;
            h.condition = r.condition;
            if (r.slot < 0 || r.slot >= slots) {
                if (error) *error = "the course's grid has " + std::to_string(slots) + " slots";
                return false;
            }
            std::optional<ex::StartEntry> e = human(h, r.slot, &err);
            if (!e) { // a car this disc / mod set lacks (another player's mod car): shown as ours
                LOG_WARN("Race (exact): remote car %s: %s; shown as %s", r.car.c_str(), err.c_str(),
                         opts_.player.car.label.c_str());
                h.car = opts_.player.car;
                e = human(h, r.slot, &err);
            }
            if (!e) {
                if (error) *error = "remote player's car " + r.car + ": " + err;
                return false;
            }
            remote_cars.push_back(h.car.label);
            rs.entries.push_back(*e);
        }
        // The solo modes' recorders (the race object's: the player's device recording the run, the ghost's playing, the free
        // run's lap recording and best lap): a free run's carried from the visit's runs, a whole-run ghost's recording on the
        // ghost's device (the kind 0).
        if (solitaire(opts_.kind)) {
            const bool fr = opts_.kind == ex::Conductor::FreeRun;
            ex::SoloRecorders r = fr && opts_.free_run ? *opts_.free_run : ex::SoloRecorders(fr);
            r.ghost_option = opts_.ghost_option;
            if (opts_.ghost && ghost_entry) r.run_to_ghost(ex::run_recording_of(*opts_.ghost));
            r.start();
            rs.recorders = std::make_shared<const ex::SoloRecorders>(std::move(r));
            rs.ghost_car = ghost_entry ? humans_ : -1;
            if (fr) visit_ = opts_.free_run;
        }
        try {
            race_ = std::make_unique<ex::ExactRace>(rs);
        } catch (const std::exception& e) {
            if (error) *error = std::string("exact race: ") + e.what();
            return false;
        }
        first_remote_ = int(rs.entries.size() - cfg_.remote.size()); // (the remote players' entries come last)
        remote_.assign(cfg_.remote.size(), {});
        for (size_t j = 0; j < cfg_.remote.size(); ++j) race_->set_posed(first_remote_ + int(j), true);
        rs_ = rs;
        rec_ = std::make_shared<ex::RaceRecord>(ex::RaceRecord::of(rs, opts_.course));
        rec_->data = exact_data_fingerprint(db, rs.entries, opts_.course_file);
        change({0, ex::RaceChange::StartCountdown});
        std::vector<std::string> car_labels;
        const int first_opp = humans_ + (ghost_entry ? 1 : 0);
        for (int i = 0; i < race_->size(); ++i)
            car_labels.push_back(i == 0                ? opts_.player.car.label
                                 : i == 1 && battle    ? opts_.second.car.label
                                 : i < first_opp       ? opts_.player.car.label // (the ghost: the player's car)
                                 : i >= first_remote_  ? remote_cars[size_t(i - first_remote_)] // (online: the car raced here)
                                                       : opps[size_t(i - first_opp)].label);
        finish_build(db, rs, car_labels, n_opp, p1);
        return true;
    }

    // A replay's race: built from the record's setup (no grid, car or event decisions made again).
    bool build_playback(const data::CarDatabase& db, std::string* error) {
        const ex::RaceRecord& r = *opts_.playback;
        if (r.program != data_->fingerprint()) {
            if (error) *error = "the replay was recorded with another version of the game";
            return false;
        }
        // The rules and rate it ran with (a replay of a run on another release's rules cannot play here).
        if (!(data_->behaviour() == r.behaviour)) {
            if (!data_->can_behave(r.behaviour)) {
                if (error) *error = std::string("the replay was recorded with ") + sim::program_name(r.behaviour.rules) + " rules";
                return false;
            }
            auto d = std::make_shared<ex::ProgramData>(*data_);
            d->set_behaviour(r.behaviour);
            data_ = d;
        }
        if (r.data && r.data != exact_data_fingerprint(db, r.entries, opts_.course_file)) {
            if (error) *error = "the replay was recorded with other car or course data";
            return false;
        }
        if (r.course != opts_.course) LOG_WARN("Race (exact): the replay's course %s is played on %s", r.course.c_str(), opts_.course.c_str());
        humans_ = r.kind == ex::Conductor::Battle2P || r.kind == ex::Conductor::BattleMP ? 2 : 1;
        if (int(r.entries.size()) < humans_) {
            if (error) *error = "the replay has no players' cars";
            return false;
        }
        ex::RaceSetup rs = r.setup(*data_, db, course_);
        if (opts_.logger) rs.logger = 1; // (the Analyzer's pass over the replay)
        if (opts_.logger_record) rs.logger_record = 1;
        // The Analyzer's race (GT3's logger race, the solo race's devices both playing): car 0 plays track A's recording,
        // car 1, a ghost car (the solo conductors' entry set-up flags it) with track B's car, plays track B's. Only track A's
        // car takes the record's changes.
        std::shared_ptr<ex::RaceRecord> analysed;
        if (opts_.analysis && solitaire(r.kind) && r.kind != ex::Conductor::FreeRun) {
            analysed = std::make_shared<ex::RaceRecord>(r);
            std::erase_if(analysed->changes, [](const ex::RaceChange& c) { return c.car != 0; });
            rs.entries.resize(1);
            ex::SoloRecorders s(false);
            const ex::RaceRecord* b = opts_.analysis_b.get();
            if (b && !b->entries.empty()) {
                ex::StartEntry g = b->entries[0];
                g.human = 1, g.player = true, g.car = 0, g.grid_slot = r.entries.size() > 1 ? r.entries[1].grid_slot : g.grid_slot;
                rs.entries.push_back(g);
                rs.ghost_switch = 1;
            }
            s.analysis(ex::run_recording_of(r), b ? ex::run_recording_of(*b) : std::vector<u8>{});
            rs.recorders = std::make_shared<const ex::SoloRecorders>(std::move(s));
            rs.ghost_car = rs.entries.size() > 1 ? 1 : -1;
            rs.external_ghost = false;
        }
        try {
            race_ = std::make_unique<ex::ExactRace>(rs);
        } catch (const std::exception& e) {
            if (error) *error = std::string("exact race: ") + e.what();
            return false;
        }
        play_ = opts_.playback;
        if (analysed) play_ = analysed, analysis_ = jumped_ = true; // (not the recorded race: no end check)
        playback_ = ex::RacePlayback(play_.get());
        if (opts_.logger_tables) race_->set_logger_tables(*opts_.logger_tables);
        // The cars' labels: the players' as the screen has them, the opponents' as the replay lists them (else the cars').
        std::vector<std::string> car_labels;
        int first_opp = humans_;
        while (first_opp < race_->size() && race_->cars()[size_t(first_opp)].ghost) ++first_opp;
        const int n_opp = race_->size() - first_opp;
        for (int i = 0; i < race_->size(); ++i) {
            std::string l = i == 0                                 ? opts_.player.car.label
                            : i == 1 && humans_ > 1                ? opts_.second.car.label
                            : i < first_opp                        ? opts_.player.car.label
                            : int(cfg_.opponents.size()) == n_opp ? cfg_.opponents[size_t(i - first_opp)]
                                                                   : std::string();
            if (l.empty()) l = db.label(r.entries[size_t(i)].eq.car);
            car_labels.push_back(l);
        }
        finish_build(db, rs, car_labels, n_opp, r.entries[0].grid_slot);
        LOG_INFO("Race (exact): playing a recorded race of %u frames", r.frames);
        return true;
    }

    // The cars as the screen reads them, after the race is built.
    void finish_build(const data::CarDatabase& db, const ex::RaceSetup& rs, const std::vector<std::string>& car_labels, int n_opp,
                      int p1) {
        const bool battle = humans_ > 1;
        // A ghost car (flagged by its conductor) right after the players is not one of the opponents.
        opp0_ = humans_;
        ghost_car_ = -1;
        if (opp0_ < race_->size() && race_->cars()[size_t(opp0_)].ghost) ghost_car_ = opp0_++;
        pad_k_ = ex::pad_constants(*data_);
        start_s_ = rs.start_type == 0 ? double(rs.countdown) : rs.start_type == 1 ? 4.0 : rs.start_type == 2 ? 10.0 : 0.0;
        wear_on_ = !(race_->inputs().prep.wear.max == ex::F());
        // The cars.
        cars_.resize(size_t(race_->size()));
        for (int i = 0; i < race_->size(); ++i) {
            CarView& v = cars_[size_t(i)];
            const ex::Car& c = race_->cars()[size_t(i)];
            v.label = car_labels[size_t(i)];
            const std::string base = exact_base_car(db, v.label);
            if (const data::Car* dc = db.car(base)) v.model = db.model_of(*dc);
            if (const data::ModCar* m = db.mod_car(base); m && m->electric)
                LOG_INFO("Race (exact): %s is an electric mod car; it runs on its donor's engine (%s) until the exact core has "
                         "the EV drive", v.label.c_str(), m->donor.c_str());
            v.setup = exact_setup(c);
            v.facts = exact_facts(c);
            exact_state(c, v.setup, v.state);
            exact_telemetry(c, wear_on_, 0, v.tel);
            v.prev = v.state;
        }
        for (int i = opp0_; i < race_->size(); ++i) labels_.push_back(cars_[size_t(i)].label);
        grid_ = {cars_[0].state.position, cars_[0].state.orientation};
        if (battle) grid2_ = {cars_[1].state.position, cars_[1].state.orientation};
        frames_.resize(size_t(race_->size()));
        LOG_INFO("Race (exact): %s, %d cars (%d opponents), event %s, course %s, %d laps, start type %d, player on slot %d; "
                 "%s rules on the %s program, %d steps a second",
                 ex::conductor_name(rs.kind), race_->size(), n_opp, cfg_.event.c_str(), opts_.course.c_str(), int(rs.laps),
                 rs.start_type, p1, sim::program_name(data_->behaviour().rules), sim::program_name(data_->program()), int(tick_hz()));
    }

    // The race modes with the race object's recorders (solo race): the time trial, licence test, machine test, free run.
    static bool solitaire(ex::Conductor k) {
        return k == ex::Conductor::TimeTrial || k == ex::Conductor::License || k == ex::Conductor::MachineTest ||
               k == ex::Conductor::FreeRun;
    }

    // A change the game makes to the race between steps: made, and recorded (a replay makes it again; while one plays, the
    // game's own are not made).
    void change(ex::RaceChange c) {
        if (play_) return;
        ex::apply_change(*race_, c);
        rec_->add_change(c);
    }

    // --- the step ---
    void step(const sim::ControlInputs& in, double dt) {
        (void)dt;
        const Uint64 t0 = SDL_GetPerformanceCounter();
        for (ex::PadFrame& f : frames_) f = {};
        if (play_) { // a replay: the recorded changes and frames
            if (!failed_) {
                const std::span<const ex::PadFrame> p = playback_.next(*race_);
                std::copy(p.begin(), p.end(), frames_.begin());
            }
        } else {
            frames_[0] = player_.pad_ ? pad_frame(*player_.pad_, opts_.player, *data_, pad_k_) : inputs_frame(in, player_.reverse_);
            // The pause menu's AT / MT while the player drives: the entry's pedal style (0 / 1) on the record.
            if (const ex::Car& c = race_->cars()[0];
                player_.pad_ && c.control == 0 && c.pedal_style <= 1 && c.pedal_style != (in.automatic ? 0 : 1))
                change({0, ex::RaceChange::PedalStyle, 0, in.automatic ? 0u : 1u});
            if (humans_ > 1) frames_[1] = pad2_ ? pad_frame(*pad2_, opts_.second, *data_, pad_k_) : inputs_frame(in2_, reverse2_);
            if (!failed_) rec_->add_frame(frames_);
        }
        player_.pad_ = nullptr, pad2_ = nullptr;
        in2_.shift_up = in2_.shift_down = false;
        const ex::RaceOrder order_before = race_->order();
        struct Before {
            s16 laps;
            u8 sector, finished;
            u32 x530;
        };
        std::vector<Before> before(cars_.size());
        for (size_t i = 0; i < cars_.size(); ++i) {
            const ex::Car& c = race_->cars()[i];
            before[i] = {s16(c.laps - c.lap_base), c.sector, c.finished, c.x530};
            cars_[i].prev = cars_[i].state;
        }
        if (failed_) return;
        online_before_step();
        // The data logger's input objects: every car's frame count with this step's frame read (a table entry's frame; a
        // jump restores the entry's car and continues from its next frame).
        if (race_->flow_inputs().logger)
            for (int i = 0; i < race_->size(); ++i) race_->set_logger_input(i, play_ ? playback_.frame() : rec_->frames, false);
        try {
            race_->step(frames_);
        } catch (const std::exception& e) { // (the step itself never throws; out of memory, the race stops where it is)
            LOG_ERROR("Race (exact): the race step stopped: %s", e.what());
            failed_ = true;
            return;
        }
        stepped_ = true;
        if (play_ && playback_.done() && !play_end_ && !jumped_) { // the replay's last frame: the recorded race again?
            play_end_ = ex::race_hash(*race_) == play_->final_hash;
            LOG_INFO("Race (exact): the replay's %u frames played; the race %s the recording", play_->frames,
                     *play_end_ ? "matches" : "DIFFERS FROM");
        }
        // The step's frame cost (GT3R_RACE debug=1 logs it; the perf overlay's ticks include it).
        step_ms_ += double(SDL_GetPerformanceCounter() - t0) * 1000.0 / double(SDL_GetPerformanceFrequency());
        if (++steps_ % 600 == 0) {
            LOG_INFO("Race (exact): %.3f ms per race step (%d cars, last 600 steps)", step_ms_ / 600.0, race_->size());
            step_ms_ = 0;
        }
        static const bool debug = SDL_getenv("GT3R_EXACT_DEBUG") != nullptr;
        if (debug && steps_ % 120 == 0)
            for (int i = 0; i < race_->size(); ++i) {
                const ex::Car& c = race_->cars()[size_t(i)];
                LOG_INFO("exact car %d: pos (%.1f %.1f %.1f) %.1f km/h gear %d laps %d base %d sector %d progress %.1f rank %d "
                         "control %d kind %d finished %d start %d clock %u",
                         i, c.pos.x.host(), c.pos.y.host(), c.pos.z.host(), c.speed.host() * 3.6f, int(c.gear), int(c.laps),
                         int(c.lap_base), int(c.sector), c.f3e4.host(), int(c.rank), int(c.control), int(c.driver_kind),
                         int(c.finished), int(race_->start_count()), race_->flow().clock);
            }
        // Hits: the walls' and the cars' impulses on each car this step.
        for (CarView& v : cars_) v.wall = v.car = 0;
        contacts_ = 0;
        for (const ex::CollisionHit& h : race_->hits()) {
            if (h.car < 0 || size_t(h.car) >= cars_.size()) continue;
            CarView& v = cars_[size_t(h.car)];
            if (h.kind == 0) v.wall += h.strength.host();
            else v.car = std::max(v.car, h.strength.host() * 15000.0f), ++contacts_;
        }
        for (size_t i = 0; i < cars_.size(); ++i) {
            CarView& v = cars_[i];
            const ex::Car& c = race_->cars()[i];
            exact_state(c, v.setup, v.state);
            exact_telemetry(c, wear_on_, v.wall, v.tel);
            v.impact = std::max(v.wall, v.car) * c.p.inv_mass.host();
        }
        online_after_step();
        // This step's line and sector crossings, in the race order before the step (the leader first).
        crossings_.clear();
        lap_done_ = false;
        const long long now_ms = (long long)(race_->flow().clock / 3);
        // The race display's sector events of the step (12 the car's lap and sector, then 14 its time since its last
        // crossing at the sector line, within the step; NTSC-J posts no 14).
        struct Sector {
            s32 lap = 0, sector = 0;
            long long split = -1;
        };
        std::vector<std::optional<Sector>> sector_of(cars_.size());
        for (size_t e = 0; e < race_->events().size(); ++e) {
            const ex::RaceEvent& ev = race_->events()[e];
            if (ev.type != 12 || ev.car < 0 || size_t(ev.car) >= cars_.size()) continue;
            Sector s{ev.lap, ev.sector, -1};
            if (e + 1 < race_->events().size() && race_->events()[e + 1].type == 14 && race_->events()[e + 1].car == ev.car)
                s.split = race_->events()[e + 1].value;
            sector_of[size_t(ev.car)] = s;
        }
        for (s32 k : order_before) {
            if (k < 0 || size_t(k) >= cars_.size() || k == ghost_car_) continue;
            const ex::Car& c = race_->cars()[size_t(k)];
            const Before& b = before[size_t(k)];
            const s16 laps = s16(c.laps - c.lap_base);
            if (laps > b.laps && b.laps >= 1 && !b.finished) {
                LapCrossing x;
                x.line = true;
                x.point = sectors(); // every sector of the lap passed
                x.lap = b.laps - 1;
                x.ms = c.x530;
                x.split_ms = (long long)c.x530 - (long long)b.x530;
                crossings_.push_back({id_of(k), x});
                if (k == 0) lap_done_ = true, last_lap_s_ = double(x.split_ms) / 1000.0;
                if (k < humans_)
                    LOG_INFO("Race (exact): car %d lap %d %.3f s (at %.3f s)", int(k), x.lap + 1, double(x.split_ms) / 1000.0,
                             double(x.ms) / 1000.0);
            } else if (const std::optional<Sector>& sx = sector_of[size_t(k)]; sx && laps == b.laps && laps >= 1) {
                // (the point and lap as the screen counts them: the sector passed in this lap, the lap from 0; the event's own
                // numbers are its conductor's: the licence's count on over the laps)
                LapCrossing x;
                x.point = std::max(0, int(c.sector) - 1);
                x.lap = laps - 1;
                x.split_ms = sx->split >= 0 ? sx->split : now_ms - (long long)c.x530;
                x.ms = (long long)c.x530 + x.split_ms;
                crossings_.push_back({id_of(k), x});
            }
            if (c.finished && !b.finished) {
                CarView& v = cars_[size_t(k)];
                v.finish = double(c.x530) / 1000.0;
                if (race_->kind() == ex::Conductor::License) { // (the test's time: its lap time event keeps it as the best lap)
                    if (c.best_lap == ex::kNoTime)
                        LOG_INFO("Race (exact): car %d (%s) ended the licence test without a time", int(k), v.label.c_str());
                    else
                        LOG_INFO("Race (exact): car %d (%s) finished the licence test in %.3f s", int(k), v.label.c_str(),
                                 double(c.best_lap) / 1000.0);
                } else {
                    LOG_INFO("Race (exact): car %d (%s) finished at %.3f s, position %d", int(k), v.label.c_str(), v.finish,
                             int(c.rank));
                }
            }
        }
        for (const ex::PitEvent& p : race_->pit_events())
            if (p.type == 1 && p.car >= 0 && p.car < race_->size() && p.car != ghost_car_) pit_entries_.push_back(id_of(p.car));
    }

    // --- RaceField ---
    RaceVehicle& player() override { return player_; }
    TrackPose player_grid() const override { return grid_; }
    int opponents() const override { return int(cars_.size()) - opp0_; }
    const std::string& opponent_label(int i) const override { return opp(i).label; }
    const std::string& opponent_model(int i) const override { return opp(i).model; }
    const sim::VehicleState& opponent_state(int i) const override { return opp(i).state; }
    const sim::VehicleState& opponent_prev_state(int i) const override { return opp(i).prev; }
    const sim::VehicleSetup& opponent_setup(int i) const override { return opp(i).setup; }
    int opponent_laps(int i) const override { return completed(opp0_ + i); }
    int opponent_lap_count(int i) const override { return lap_count(opp0_ + i); }
    double race_time() const override {
        if (!stepped_) return -start_s_;
        if (race_->start_count() > 0) return -double(race_->start_count()) / tick_hz(); // (frames of the race step)
        return double(race_->flow().clock) / 3000.0;
    }
    int player_position() const override {
        if (!online()) return int(race_->cars()[0].rank);
        const std::vector<Standing> st = standings();
        for (size_t i = 0; i < st.size(); ++i)
            if (st[i].car == -1) return int(i) + 1;
        return 1;
    }
    double player_finish() const override { return cars_[0].finish; }
    double second_finish() const override { return humans_ > 1 ? cars_[1].finish : -1; }
    int contacts_last_tick() const override { return contacts_; }
    const std::vector<std::string>& labels() const override { return labels_; }

    bool has_second() const override { return humans_ > 1; }
    void set_second_inputs(const sim::ControlInputs& in) override { in2_ = in; }
    void set_second_reverse(bool on) override { reverse2_ = on; }
    void set_second_pad(const PadState* pad) override { pad2_ = pad; }
    // Every race steps 60 times a second, on every disc (whose rules: ExactFieldOptions::rules, a replay's record).
    double tick_hz() const override { return sim::kDefaultTickHz; }
    const sim::VehicleState& second_state() const override { return cars_[1].state; }
    const sim::VehicleState& second_prev_state() const override { return cars_[1].prev; }
    const sim::VehicleSetup& second_setup() const override { return cars_[1].setup; }
    TrackPose second_grid() const override { return grid2_; }
    float second_boost() const override { return cars_[1].tel.boost; }
    const sim::VehicleTelemetry* second_telemetry() const override { return humans_ > 1 ? &cars_[1].tel : nullptr; }
    const sim::VehicleTelemetry* opponent_telemetry(int i) const override { return &opp(i).tel; }
    std::optional<CarFacts> second_facts() const override {
        return humans_ > 1 ? std::optional<CarFacts>(cars_[1].facts) : std::nullopt;
    }
    std::optional<CarFacts> opponent_facts(int i) const override { return opp(i).facts; }
    float opponent_brake(int i) const override { return race_->cars()[size_t(opp0_ + i)].brake_pedal.host(); }
    float opponent_car_impulse(int i) const override { return opp(i).car; }
    float second_brake() const override { return humans_ > 1 ? race_->cars()[1].brake_pedal.host() : 0.0f; }
    float second_wall_impulse() const override { return humans_ > 1 ? cars_[1].wall : 0.0f; }
    float second_car_impulse() const override { return humans_ > 1 ? cars_[1].car : 0.0f; }
    float second_impact() const override { return humans_ > 1 ? cars_[1].impact : 0.0f; }
    LapInfo second_laps() const override {
        LapInfo l;
        if (humans_ < 2) return l;
        const ex::Car& c = race_->cars()[1];
        l.laps = completed(1);
        l.started = lap_count(1) > 0;
        l.wrong_way = (c.x535 & 1) != 0;
        l.current = std::max(0.0, race_time() - double(c.x530) / 1000.0);
        l.best = c.best_lap == 0x157529FF ? -1 : double(c.best_lap) / 1000.0;
        l.race_distance = distance(1);
        return l;
    }
    int second_position() const override { return humans_ > 1 ? int(race_->cars()[1].rank) : 0; }
    void set_laps(int laps) override { (void)laps; } // set at the start (ExactFieldOptions::laps)
    const std::vector<CarCrossing>& last_crossings() const override { return crossings_; }
    std::vector<int> take_pit_entries() override { return std::exchange(pit_entries_, {}); }
    bool opponent_in_pits(int i) const override {
        const u8 k = race_->cars()[size_t(opp0_ + i)].driver_kind;
        return k >= 2 && k <= 4;
    }
    std::vector<Standing> standings() const override {
        std::vector<Standing> out;
        for (s32 k : race_->order()) {
            if (k < 0 || size_t(k) >= cars_.size() || k == ghost_car_) continue;
            const ex::Car& c = race_->cars()[size_t(k)];
            Standing s;
            s.car = id_of(k);
            s.finished = c.finished != 0;
            s.time = s.finished ? cars_[size_t(k)].finish : 0;
            s.laps = completed(k);
            s.distance = distance(k);
            if (const Remote* rm = remote_of(k); rm && rm->claimed) s.finished = true, s.time = rm->claim; // its owner's
            out.push_back(s);
        }
        if (online()) std::stable_sort(out.begin(), out.end(), ranks_ahead); // the owners' finish times decide the finished
        return out;
    }

    // --- online (docs/dev/ONLINE.md): the other players' cars, posed from their owners' states ---
    bool opponent_remote(int i) const override { return remote_of(opp0_ + i) != nullptr; }
    void set_remote_pose(int i, const RemoteCar::Pose& p) override {
        Remote* rm = remote_of(opp0_ + i);
        if (!rm || rm->retired || !p.car) return;
        race_->pose(opp0_ + i, *p.car);
        rm->shown = p.state;
        exact_telemetry(*p.car, wear_on_, 0, rm->tel);
        rm->have_pose = true;
    }
    void set_remote_finish(int i, double t) override {
        if (Remote* rm = remote_of(opp0_ + i); rm && !rm->claimed) rm->claimed = true, rm->claim = t;
    }
    void set_remote_retired(int i) override {
        if (Remote* rm = remote_of(opp0_ + i)) rm->retired = true;
    }
    bool opponent_retired(int i) const override {
        const Remote* rm = remote_of(opp0_ + i);
        return rm && rm->retired;
    }
    std::vector<RemoteHit> take_remote_hits() override { return std::exchange(remote_hits_, {}); }
    double last_contact_with(int i) const override {
        const Remote* rm = remote_of(opp0_ + i);
        return rm ? rm->last_contact : -1e18;
    }
    double remote_observed_finish(int i) const override { return remote_of(opp0_ + i) ? opp(i).finish : -1; }
    // A hit on our car only the other machine saw (course frame: x, z; the physics frame's y is -z).
    void apply_player_impulse(float px, float pz, float jx, float jz) override {
        if (!(std::isfinite(px) && std::isfinite(pz) && std::isfinite(jx) && std::isfinite(jz))) return;
        race_->apply_impulse(0, ex::F(px), ex::F(-pz), ex::F(jx), ex::F(-jz));
        cars_[0].car = std::max(cars_[0].car, std::hypot(jx, jz));
    }
    void set_remote_ghost(int i, bool on) override {
        if (Remote* rm = remote_of(opp0_ + i)) rm->ghost = on;
    }
    bool opponent_ghost(int i) const override {
        const Remote* rm = remote_of(opp0_ + i);
        return rm && (rm->ghost || rm->claimed);
    }
    std::unique_ptr<ex::ExactRace> make_shadow(int i, const net::CarEntry& entry) const {
        const int k = opp0_ + i;
        if (!remote_of(k)) return nullptr;
        ex::RaceSetup s = rs_;
        ex::StartEntry e = rs_.entries[size_t(k)];
        e.pedal_style = entry.pedal_style, e.wheel_type = entry.wheel_type;
        e.asm_on = entry.asm_on, e.tcs_on = entry.tcs_on;
        s.entries = {e};
        s.config = entry.control == 1 ? 3 : 0; // the AI drives the owner's car with the pad decoded (its test autopilot)
        s.battle_laps = 0;                     // no laps to finish: it only predicts
        s.effects = false;
        try {
            return std::make_unique<ex::ExactRace>(s);
        } catch (const std::exception& ex) {
            LOG_WARN("Online: no prediction for %s (%s)", opp(i).label.c_str(), ex.what());
            return nullptr;
        }
    }
    net::CarEntry player_entry() const { return car_entry(race_->cars()[0], opts_.player.asm_on, opts_.player.tcs_on); }

    const ex::ExactRace& race() const { return *race_; }
    const std::vector<ex::PadFrame>& frames() const { return frames_; }
    const LapSource& laps() const { return laps_; }

    // Player 1's laps (ExactLaps).
    bool lap_done() const { return lap_done_; }
    double last_lap() const { return last_lap_s_; }
    int player_laps() const { return completed(0); }
    bool player_started() const { return lap_count(0) > 0; }
    double player_distance() const { return distance(0); }
    float player_progress() const {
        const float len = course_->query->lap_length().host();
        return len > 0 ? std::clamp(race_->cars()[0].f3e4.host() / len, 0.0f, 1.0f) : 0.0f;
    }
    std::vector<LapCrossing> player_crossings() const {
        std::vector<LapCrossing> out;
        for (const CarCrossing& c : crossings_)
            if (c.car == -1) out.push_back(c.crossing);
        return out;
    }

    // --- the player's car ---
    const CarView& car(int i) const { return cars_[size_t(i)]; }
    const ex::Car& record(int i) const { return race_->cars()[size_t(i)]; }
    ex::Car& record(int i) { return race_->cars()[size_t(i)]; }
    void player_step(const sim::ControlInputs& in, double dt) { step(in, dt); }
    // On the race's AI course: the AI drives car i on.
    bool drive_on(int i) {
        if (play_) return true; // (a replay has it among its changes)
        const bool ok = race_->drive_on(i);
        if (ok) rec_->add_change({0, ex::RaceChange::DriveOn, u8(i)});
        return ok;
    }
    void set_condition(int i, const sim::EngineCondition& c) {
        if (exact_condition(record(i)) == c) return;
        // (the parameter block read the entry's words at the start; only the counters change here)
        change({0, ex::RaceChange::Condition, u8(i), c.odometer, ex::F(c.wear_m).bits, ex::F(c.oil_m).bits});
    }
    // The record so far (a copy with the race's hash), or the record played.
    std::shared_ptr<const ex::RaceRecord> recording() const {
        if (play_) return play_;
        if (closed_) return closed_;
        if (!rec_) return nullptr;
        auto r = std::make_shared<ex::RaceRecord>(*rec_);
        r->final_hash = ex::race_hash(*race_);
        // The player's recording as the race's end closes it: a whole-run ghost plays it.
        if (const ex::SoloRecorders* s = race_->recorders()) {
            ex::SoloRecorders done = *s;
            done.end();
            r->run_recording = done.run_recording();
        }
        return r;
    }
    bool close_recording() {
        if (play_ || !rec_) return false;
        if (!closed_) closed_ = recording();
        return true;
    }
    // Pause EXIT counted as the run's finish, as the race object's EXIT makes it (the free run's, the top-speed test's): the
    // record closed, the race's recordings closed and car 0's end of run (mode 4; the top-speed test's 3).
    bool exit_finish(bool top_speed) {
        if (!close_recording()) return false;
        race_->close_recordings(!top_speed && race_->kind() == ex::Conductor::FreeRun);
        race_->end_run(0, top_speed ? 3 : 4);
        return true;
    }
    std::optional<bool> playback_matches() const { return play_end_; }
    ex::SoloRecorders* solo_recorders() { return play_ ? nullptr : race_ ? race_->recorders() : nullptr; }
    bool analysis_built() const { return analysis_; }
    void set_logger_record(bool on) {
        if (race_) race_->set_logger_record(on);
    }
    bool logger_jump(int k, u32 step) {
        if (!race_ || !race_->logger_jump(k)) return false;
        playback_.seek(step);
        jumped_ = true; // (the replay's own end no longer checks the race)
        return true;
    }
    // A hit of car i that is not one of the collision step's (kind 2: the course's rigid bodies, the game's pylons): the
    // conductor's hit rule before the next step, recorded.
    void report_hit(int i, int kind) {
        if (i >= 0 && i < race_->size()) change({0, ex::RaceChange::Hit, u8(i), u32(kind), 0});
    }
    bool ghost(float alpha, sim::VehicleState& out, const sim::VehicleSetup** setup) const {
        if (ghost_car_ < 0) return false;
        const ex::Car& c = race_->cars()[size_t(ghost_car_)];
        if (ex::draw_mode(c, 0, race_->ghost_switch()) == 0) return false; // (the computer drives it: not drawn)
        const CarView& v = cars_[size_t(ghost_car_)];
        out = interpolate(v.prev, v.state, alpha);
        if (setup) *setup = &v.setup;
        return true;
    }

private:
    // A remote player's car (online).
    struct Remote {
        bool retired = false, ghost = false, claimed = false, have_pose = false;
        double claim = -1;           // its owner's finish time
        double last_contact = -1e18; // race time our car last touched it
        sim::VehicleState shown;     // the pose's drawn state (the prediction, blended)
        sim::VehicleTelemetry tel;
    };
    bool online() const { return !remote_.empty(); }
    Remote* remote_of(int k) {
        const int j = k - first_remote_;
        return online() && j >= 0 && size_t(j) < remote_.size() ? &remote_[size_t(j)] : nullptr;
    }
    const Remote* remote_of(int k) const { return const_cast<ExactField*>(this)->remote_of(k); }
    // Before the step: a ghost, a finished or a retired car has no contact (the record's ghost flag).
    void online_before_step() {
        for (size_t j = 0; j < remote_.size(); ++j) {
            const Remote& rm = remote_[j];
            race_->cars()[size_t(first_remote_) + j].ghost = rm.ghost || rm.claimed || rm.retired ? 1 : 0;
        }
    }
    // After the step: a remote car is drawn as its pose shows it; our car's contacts with remote cars go to their owners.
    void online_after_step() {
        if (!online()) return;
        for (size_t j = 0; j < remote_.size(); ++j) {
            const Remote& rm = remote_[j];
            CarView& v = cars_[size_t(first_remote_) + j];
            if (!rm.have_pose) continue;
            v.state = rm.shown;
            v.tel = rm.tel;
        }
        const double now = race_time();
        for (const ex::ExactRace::CarContact& c : race_->car_contacts()) {
            const int other = c.struck == 0 ? c.striker : c.striker == 0 ? c.struck : -1;
            Remote* rm = remote_of(other);
            if (!rm) continue;
            // The impulse on the remote car, in the course frame (x, z = -y).
            const float sign = c.struck == other ? 1.0f : -1.0f;
            remote_hits_.push_back({other - opp0_, c.x, -c.y, c.jx * sign, -c.jy * sign});
            rm->last_contact = now;
        }
    }
    const CarView& opp(int i) const { return cars_.at(size_t(opp0_ + i)); }
    int id_of(int k) const { return k == 0 ? -1 : k == 1 && humans_ > 1 ? -2 : k == ghost_car_ ? -3 : k - opp0_; }
    int lap_count(int k) const {
        const ex::Car& c = race_->cars()[size_t(k)];
        return std::max(0, int(c.laps) - int(c.lap_base));
    }
    int completed(int k) const { return std::max(0, lap_count(k) - 1); }
    double distance(int k) const {
        const ex::Car& c = race_->cars()[size_t(k)];
        return double(completed(k)) * double(course_->query->lap_length().host()) + double(c.f3e4.host());
    }
    int sectors() const { return course_->query->sectors(); }

    FieldConfig cfg_;
    ExactFieldOptions opts_;
    const data::CarDatabase* db_ = nullptr;
    std::shared_ptr<const ex::ProgramData> data_;
    ex::PadConstants pad_k_; // the driver input's constants (the linear response's shaping)
    std::shared_ptr<const ex::RaceCourse> course_;
    std::unique_ptr<ex::ExactRace> race_;
    std::vector<CarView> cars_;
    std::vector<ex::PadFrame> frames_;
    std::vector<std::string> labels_;
    std::vector<CarCrossing> crossings_;
    std::vector<int> pit_entries_;
    int humans_ = 1, contacts_ = 0;
    int opp0_ = 1, ghost_car_ = -1; // the first opponent's car; the ghost car (-1 none)
    // The free run's recorders: the visit's (ExactFieldOptions::free_run), else the field's own (a replay's: its record's).
    std::shared_ptr<ex::SoloRecorders> visit_; // the visit's free run recorders: left as the run leaves them
    bool jumped_ = false;                      // the Analyzer's REPLAY jumped (exact_logger_jump)
    bool analysis_ = false;                    // an Analyzer race (ExactFieldOptions::analysis)
    bool wear_on_ = false, stepped_ = false, failed_ = false;
    double start_s_ = 0;
    TrackPose grid_, grid2_;
    sim::ControlInputs in2_;
    bool reverse2_ = false;
    const PadState* pad2_ = nullptr;
    ExactPlayer player_;
    ExactLaps laps_{*this};
    // Online.
    int first_remote_ = 1 << 20; // the first remote car's index
    std::vector<Remote> remote_;
    std::vector<RemoteHit> remote_hits_;
    ex::RaceSetup rs_; // the race's setup (the shadows' from it)
    bool lap_done_ = false;
    double last_lap_s_ = -1;
    double step_ms_ = 0;
    long long steps_ = 0;
    std::shared_ptr<ex::RaceRecord> rec_;         // the live race's record
    std::shared_ptr<const ex::RaceRecord> closed_; // the record as it was closed (close_recording), else none
    std::shared_ptr<const ex::RaceRecord> play_;  // the record a replay plays
    ex::RacePlayback playback_;
    std::optional<bool> play_end_;
};

bool ExactLaps::lap_completed() const { return f_.lap_done(); }
int ExactLaps::laps_completed() const { return f_.player_laps(); }
bool ExactLaps::lap_started() const { return f_.player_started(); }
double ExactLaps::current_lap_time(double now) const {
    return std::max(0.0, now - double(f_.record(0).x530) / 1000.0); // since the last crossing (lap 1: since GO)
}
double ExactLaps::last_lap_time() const { return f_.last_lap(); }
double ExactLaps::best_lap_time() const {
    const u32 b = f_.record(0).best_lap; // car + 0x16C4 (ms; 0x157529FF none)
    return b == 0x157529FF ? -1.0 : double(b) / 1000.0;
}
float ExactLaps::lap_progress() const { return f_.player_progress(); }
bool ExactLaps::wrong_way() const { return (f_.record(0).x535 & 1) != 0; }
std::vector<LapCrossing> ExactLaps::lap_crossings() const { return f_.player_crossings(); }
double ExactLaps::race_distance() const { return f_.player_distance(); }

void ExactPlayer::step(const sim::ControlInputs& in, double dt) { f_.player_step(in, dt); }
const sim::VehicleState& ExactPlayer::state() const { return f_.car(car_).state; }
const sim::VehicleSetup& ExactPlayer::setup() const { return f_.car(car_).setup; }
float ExactPlayer::boost() const { return f_.car(car_).tel.boost; }
float ExactPlayer::brake_pedal() const { return f_.record(car_).brake_pedal.host(); }
const sim::VehicleTelemetry* ExactPlayer::telemetry() const { return &f_.car(car_).tel; }
std::optional<CarFacts> ExactPlayer::facts() const { return f_.car(car_).facts; }
int ExactPlayer::gear_hint(bool& blink) const {
    const ex::Driver& d = f_.record(car_).ai;
    blink = d.gear_braking != 0;
    return d.gear_hint;
}
int ExactPlayer::driver_kind() const { return f_.record(car_).driver_kind; }
bool ExactPlayer::ai_take_over() { return f_.drive_on(car_); }
void ExactPlayer::set_engine_condition(const sim::EngineCondition& c) { f_.set_condition(car_, c); }
sim::EngineCondition ExactPlayer::engine_condition() const { return exact_condition(f_.record(car_)); }
float ExactPlayer::wall_impulse() const { return f_.car(car_).wall; }
float ExactPlayer::car_impulse() const { return f_.car(car_).car; }
float ExactPlayer::last_impact() const { return f_.car(car_).impact; }

} // namespace

std::unique_ptr<RaceField> make_exact_field(const RaceTrack&, const data::CarDatabase& db, const FieldConfig& cfg,
                                            std::string* error) {
    if (!cfg.exact.data) {
        if (error) *error = "the exact core needs the disc's program tables";
        return nullptr;
    }
    auto f = std::make_unique<ExactField>(cfg);
    if (!f->build(db, error)) return nullptr;
    return f;
}

const sim::exact::ExactRace* exact_race_of(const RaceField& f) {
    const auto* e = dynamic_cast<const ExactField*>(&f);
    return e ? &e->race() : nullptr;
}

const std::vector<sim::exact::PadFrame>* exact_frames_of(const RaceField& f) {
    const auto* e = dynamic_cast<const ExactField*>(&f);
    return e ? &e->frames() : nullptr;
}

const LapSource* exact_lap_source(const RaceField& f) {
    const auto* e = dynamic_cast<const ExactField*>(&f);
    return e ? &e->laps() : nullptr;
}

sim::exact::PadFrame exact_inputs_frame(const sim::ControlInputs& in, bool reverse) { return inputs_frame(in, reverse); }

std::unique_ptr<sim::exact::ExactRace> exact_shadow(const RaceField& f, int opponent, const net::CarEntry& entry) {
    const auto* e = dynamic_cast<const ExactField*>(&f);
    return e ? e->make_shadow(opponent, entry) : nullptr;
}

std::optional<net::CarEntry> exact_player_entry(const RaceField& f) {
    const auto* e = dynamic_cast<const ExactField*>(&f);
    if (!e) return std::nullopt;
    return e->player_entry();
}

sim::exact::PadFrame exact_pad_frame(const PadState& pad, const ExactHuman& h, const sim::exact::ProgramData& d) {
    return pad_frame(pad, h, d, ex::pad_constants(d));
}

std::shared_ptr<const sim::exact::RaceRecord> exact_recording(const RaceField& f) {
    const auto* e = dynamic_cast<const ExactField*>(&f);
    return e ? e->recording() : nullptr;
}

bool exact_close_recording(RaceField& f) {
    auto* e = dynamic_cast<ExactField*>(&f);
    return e && e->close_recording();
}

bool exact_exit_finish(RaceField& f, bool top_speed_test) {
    auto* e = dynamic_cast<ExactField*>(&f);
    return e && e->exit_finish(top_speed_test);
}

bool exact_ghost(const RaceField& f, float alpha, sim::VehicleState& state, const sim::VehicleSetup** setup) {
    const auto* e = dynamic_cast<const ExactField*>(&f);
    return e && e->ghost(alpha, state, setup);
}

std::optional<bool> exact_playback_matches(const RaceField& f) {
    const auto* e = dynamic_cast<const ExactField*>(&f);
    return e ? e->playback_matches() : std::nullopt;
}

bool exact_solo_kind(sim::exact::Conductor k) {
    return k == ex::Conductor::TimeTrial || k == ex::Conductor::License || k == ex::Conductor::MachineTest ||
           k == ex::Conductor::FreeRun || k == ex::Conductor::FreePractice;
}

bool exact_report_hit(RaceField& f, int car, int kind) {
    auto* e = dynamic_cast<ExactField*>(&f);
    if (e) e->report_hit(car, kind);
    return e != nullptr;
}

const std::vector<std::map<s32, sim::exact::Snapshot>>* exact_logger_tables(const RaceField& f) {
    const sim::exact::ExactRace* r = exact_race_of(f);
    return r ? &r->flow().logger : nullptr;
}

const sim::exact::SoloRecorders* exact_solo_recorders(const RaceField& f) {
    const sim::exact::ExactRace* r = exact_race_of(f);
    return r ? r->recorders() : nullptr;
}

bool exact_logger_jump(RaceField& f, int k, u32 step) {
    auto* e = dynamic_cast<ExactField*>(&f);
    return e && e->logger_jump(k, step);
}

bool exact_analysis_field(const RaceField& f) {
    const auto* e = dynamic_cast<const ExactField*>(&f);
    return e && e->analysis_built();
}

u32 exact_device_frame(const RaceField& f, int i) {
    const sim::exact::ExactRace* r = exact_race_of(f);
    const sim::exact::SoloRecorders* s = r ? r->recorders() : nullptr;
    if (!s) return 0;
    if (i == 0) return s->player.rec.count;
    return i == r->ghost_car() ? s->ghost.rec.count : 0;
}

void exact_logger_record(RaceField& f, bool on) {
    if (auto* e = dynamic_cast<ExactField*>(&f)) e->set_logger_record(on);
}

bool exact_best_lap_to_ghost(RaceField& f) {
    auto* e = dynamic_cast<ExactField*>(&f);
    sim::exact::SoloRecorders* s = e ? e->solo_recorders() : nullptr;
    if (!s || !s->free_run) return false;
    s->best_to_ghost(false);
    return true;
}

bool exact_ghost_from_best(RaceField& f, const sim::exact::SoloRecorders& from) {
    auto* e = dynamic_cast<ExactField*>(&f);
    sim::exact::SoloRecorders* s = e ? e->solo_recorders() : nullptr;
    return s && s->free_run && s->ghost_from_best(from);
}

} // namespace gt3r::race
