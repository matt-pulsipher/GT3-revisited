#include "race/replay_cameras.h"

#include "race/music_markers.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt3r::race {

namespace {

float rdf(ByteSpan d, size_t o) {
    u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

GtrcCurve read_curve(ByteSpan d, u32 off) {
    GtrcCurve c;
    if (off == 0 || off + 4 > d.size()) return c;
    u32 n = rd32(d, off);
    if (n > 4096 || off + 4 + size_t(n) * 8 > d.size()) throw FormatError("GTRC curve out of range");
    for (u32 i = 0; i < n; ++i) c.keys.push_back({rdf(d, off + 4 + 8 * i), rdf(d, off + 8 + 8 * i)});
    return c;
}

GtrcRecord read_record(ByteSpan d, size_t o) {
    if (o + 0x80 > d.size()) throw FormatError("GTRC record out of range");
    GtrcRecord r;
    r.kind = rd32(d, o);
    r.start = rdf(d, o + 0x04);
    r.end = rdf(d, o + 0x08);
    r.duration = rdf(d, o + 0x0C);
    r.mount = rd32(d, o + 0x18);
    r.haze = rdf(d, o + 0x1C);
    r.zoom_min = rdf(d, o + 0x28);
    r.zoom_max = rdf(d, o + 0x2C);
    r.dissolve = rdf(d, o + 0x64);
    r.zoom = read_curve(d, rd32(d, o + 0x34));
    for (int i = 0; i < 3; ++i) {
        r.pos[i] = read_curve(d, rd32(d, o + 0x40 + 4 * size_t(i)));
        r.target[i] = read_curve(d, rd32(d, o + 0x58 + 4 * size_t(i)));
    }
    return r;
}

} // namespace

float GtrcCurve::eval(float t) const {
    if (keys.empty()) return 0;
    if (keys.size() == 1) return keys[0].second;
    size_t i = 0;
    while (i + 1 < keys.size() && keys[i + 1].first <= t) ++i;
    i = std::min(i, keys.size() - 2);
    const auto &a = keys[i], &b = keys[i + 1];
    float span = b.first - a.first;
    if (span == 0) return a.second;
    return a.second + (b.second - a.second) * (t - a.first) / span;
}

GtrcScript GtrcScript::parse(ByteSpan d) {
    if (!is_gtrc(d)) throw FormatError("not a GTRC block");
    GtrcScript s;
    s.course_id = rd32(d, 0x14);
    s.lap_length = rdf(d, 0x18);
    s.script_end = rdf(d, 0x1C);
    u32 n = rd32(d, 0x24), off = rd32(d, 0x28);
    u32 n2 = rd32(d, 0x2C), off2 = rd32(d, 0x30);
    if (n > 1024 || n2 > 1024) throw FormatError("GTRC record count");
    for (u32 i = 0; i < n; ++i) s.records.push_back(read_record(d, off + 0x80 * size_t(i)));
    for (u32 i = 0; i < n2; ++i) s.stills.push_back(read_record(d, off2 + 0x80 * size_t(i)));
    return s;
}

int GtrcScript::variants() const {
    if (lap_length <= 0 || records.empty()) return 1;
    return std::max(1, int(script_end / lap_length) - 1 - int(records[0].start / lap_length));
}

std::vector<int> GtrcScript::records_at(float D) const {
    std::vector<int> out;
    const int n = int(records.size());
    if (n == 0 || D < records[0].start) return out;
    // The last record starting at or before D (a binary search on the sorted starts).
    int lo = 0, hi = n;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (D < records[size_t(mid)].start) hi = mid;
        else lo = mid + 1;
    }
    int i = lo - 1;
    const float P = period();
    float d = D;
    while (records[size_t(i)].start <= d && out.size() < 32) {
        if (d < records[size_t(i)].end) out.push_back(i);
        if (++i >= n) { // past the end: on with D - period from the first record
            if (!(d - P < d)) break;
            d -= P, i -= n;
        }
    }
    return out;
}

int GtrcScript::record_at(float D, float u_max) const {
    for (int i : records_at(D)) {
        const GtrcRecord& r = records[size_t(i)];
        const float u = (D - r.start) / (r.end - r.start);
        if (u >= 0 && u <= u_max) return i;
    }
    return -1;
}

std::optional<std::pair<int, int>> GtrcScript::other_variant(int k, float s) const {
    const int nvar = variants();
    int v = k + 1;
    for (int i = 0; i < nvar; ++i, ++v) {
        if (v > nvar) v = 1;
        const int r = record_at(float(v) * lap_length + s, 0.6f);
        if (r >= 0) return std::make_pair(r, v);
    }
    return std::nullopt;
}

void ReplayDirector::setup() {
    k_ = 0, zoom_ = 0, offset_ = 0;
    shot_time_ = 0;
    proc_ = {};
    last_record_ = -1, roll_ = 0;
    wide_ = false, swing_ = swing_v_ = 0, swing_acc_ = 0;
    // The course camera starts on the script's last record (the game's set-up, presentation §H8).
    cur_ = s_ && !s_->records.empty() ? int(s_->records.size()) - 1 : -1;
    variant_ = -1, k0_ = 1;
    onboard_ = on_partner_ = turned_ = false;
    onboard_time_ = 0;
    run_out_ = false, last_t_ = 0, last_D_ = 0, backwards_ = false;
    hold_on_ = false, hold_ = 0, manual_ = 0;
    target_set_ = attract_change_ = switched_ = false;
    armed_time_ = 0;
    gate_x_ = 4.0f, gate_request_ = false;
    reset_flag_ = true;
}

void ReplayDirector::reset() {
    reset_flag_ = true;
    if (mode_ == 1) proc_ = {}; // (mode 1 cuts at its next frame)
}

void ReplayDirector::car_changed(double manual_seconds, bool search_first) {
    reset_flag_ = true, target_set_ = true;
    if (manual_seconds > 0) manual_ = manual_seconds;
    attract_change_ = attract_change_ || search_first;
}

void ReplayDirector::set_view_mode(int mode) {
    mode = std::clamp(mode, 0, 3);
    if (mode == mode_) return;
    mode_ = mode;
    reset();
}

float ReplayDirector::shot_t(const GtrcRecord& r, float u, double seconds) {
    // A record aiming at its own target curves with a duration plays over that time; the
    // others (and those under 1 s) follow the car along their range.
    if (!r.looks_at_car() && r.duration >= 1.0f) return std::clamp(float(seconds / double(r.duration)), 0.0f, 1.0f);
    return std::clamp(u, 0.0f, 1.0f);
}

// --- Modes 0, 2, 3: the course camera's shot picker (presentation §H8) ---

int ReplayDirector::shot_state(int mode, bool run_out, bool kind_400, bool on_board, float t, bool backwards) {
    if (mode == 0 || mode == 3) return run_out ? 2 : 0;
    if (!kind_400 || on_board) return run_out ? 1 : 0;
    if (run_out) return 2;
    return (backwards ? t < 0.1f : t > 0.9f) ? 1 : 0;
}

int ReplayDirector::state() const {
    const bool kind_400 = cur_ >= 0 && (s_->records[size_t(cur_)].kind & 0x400) != 0;
    if (hold_on_ && (onboard_ || !kind_400) && hold_ > 1e-9) return 0;
    return shot_state(mode_, run_out_, kind_400, onboard_, last_t_, backwards_);
}

float ReplayDirector::shot_distance(float D, int k, float s) const {
    const float L = s_->lap_length;
    if (variant_ < 0) return k < k0_ ? D + s_->period() : D; // (the car wrapped past its last variant)
    return float(variant_) * L + s + (k != k0_ ? L : 0.0f);   // (the car crossed the line meanwhile)
}

bool ReplayDirector::runs_out(float D, int k, float s, double seconds) const {
    const GtrcRecord& r = s_->records[size_t(cur_)];
    if (!onboard_ && r.mode() != 3 && !r.looks_at_car() && r.duration >= 1.0f) {
        const double t = seconds / double(r.duration);
        return t < 0 || t > 1;
    }
    const float u = (shot_distance(D, k, s) - r.start) / (r.end - r.start);
    return !(u >= 0 && u <= 1);
}

void ReplayDirector::cut_to(int record, int variant, int k) {
    cur_ = record, variant_ = variant, k0_ = k;
    onboard_ = on_partner_ = false;
    hold_on_ = false;
    zoom_ = 0;
    shot_time_ = armed_time_ = 0;
    ++serial_;
}

void ReplayDirector::rearm(int k) {
    k0_ = k;
    if (onboard_) ++serial_;
    onboard_ = on_partner_ = false;
    hold_on_ = false;
    shot_time_ = armed_time_ = 0;
}

void ReplayDirector::turn_on_board(bool partner) {
    onboard_ = true, on_partner_ = partner;
    turned_ = true;
    onboard_time_ = armed_time_ = 0;
    ++serial_;
}

void ReplayDirector::dissolve_out() {
    // The outgoing shot's: its record's own (+0x64), 1 s on board, at most 4 s.
    const GtrcRecord& r = s_->records[size_t(cur_)];
    const float d = std::min(onboard_ || r.mode() == 3 ? 1.0f : r.dissolve, 4.0f);
    if (d > 0) pending_dissolve_ = d;
}

bool ReplayDirector::pick(int k, float s, float D) {
    const bool forced = std::exchange(reset_flag_, false);
    const bool target = std::exchange(target_set_, false) || forced;
    bool attract = std::exchange(attract_change_, false);
    const bool switched = std::exchange(switched_, false);
    const bool manual = manual_ > 1e-9;
    if (cur_ < 0) { // no record yet (no set-up)
        if (const int r = s_->record_at(D, 1.0f); r >= 0) return cut_to(r, -1, k), true;
        if (auto v = s_->other_variant(k, s)) return cut_to(v->first, v->second, k), true;
        cut_to(int(s_->records.size()) - 1, -1, k);
        return turn_on_board(false), true; // (the game: the driver's camera)
    }
    const GtrcRecord& r = s_->records[size_t(cur_)];
    const bool ended = target && (onboard_ || (r.kind & 0x400) || D < r.start || D > r.end);
    if (switched && ended) attract = true; // (mode 2's TOP / AUTO switch)
    if (!attract) {
        if (forced || ended) {
            // The camera re-arms its record; run out at once (D outside it): the record at D
            // whatever its progress, else on board.
            rearm(k);
            run_out_ = runs_out(D, k, s, 1.0 / 60.0);
            const GtrcRecord& a = s_->records[size_t(cur_)];
            last_t_ = shot_t(a, (shot_distance(D, k, s) - a.start) / std::max(a.end - a.start, 1e-3f), 1.0 / 60.0);
            if (run_out_) {
                if (const int n = s_->record_at(D, 1.0f); n >= 0) cut_to(n, -1, k);
                else turn_on_board(false);
                return true;
            }
            if (!gate_open(state(), gate_x_, gate_request_)) return true;
        } else if (!gate_open(state(), gate_x_, gate_request_)) {
            return false;
        }
    }
    // The search: the record at D (unless the current one, outside a manual or attract change),
    // another lap variant's, else on board.
    const int n = s_->record_at(D, manual ? 1.0f : 0.6f);
    if (n >= 0 && (manual || attract || n != cur_)) {
        dissolve_out();
        cut_to(n, -1, k);
        return true;
    }
    if (auto v = s_->other_variant(k, s); v && v->first != cur_) {
        dissolve_out();
        cut_to(v->first, v->second, k);
        hold_on_ = true, hold_ = kVariantHold;
        return true;
    }
    if (attract || !turned_) dissolve_out();
    turn_on_board(false);
    hold_on_ = true, hold_ = kVariantHold;
    return true;
}

float ReplayDirector::pair_angle(const render::Vec3& eye, const Pair& pair, float angle) {
    constexpr float kPi = 3.14159265f;
    const render::Vec3 a = pair.followed - eye, b = pair.partner - eye;
    float apart = std::atan2(a.x, a.z) - std::atan2(b.x, b.z);
    while (apart > kPi) apart -= 2 * kPi;
    while (apart < -kPi) apart += 2 * kPi;
    const float da = render::length(a), db = render::length(b);
    const float x = db < da ? std::max(db - (da - db) * 60.0f, 1.0f) : db;
    const float need = 1.1f * std::fabs(apart) + std::atan2(5.0f, x);
    return angle < need ? std::min(need, 70.0f * kPi / 180.0f) : angle;
}

ReplayDirector::Shot ReplayDirector::evaluate(const GtrcRecord& r, float D, const render::Vec3& car, double dt,
                                              const Pair* pair) {
    Shot shot;
    shot.record = cur_;
    shot.seconds = float(shot_time_);
    shot.haze = r.hazy();
    if (r.mode() == 3) {
        shot.valid = shot.onboard = true;
        return shot;
    }
    const float t = shot_t(r, (D - r.start) / std::max(r.end - r.start, 1e-3f), shot_time_);
    shot.eye = r.position(t);
    shot.target = r.looks_at_car() ? car + render::Vec3{0, 0.4f, 0}
                                   : render::Vec3{r.target[0].eval(t), r.target[1].eval(t), r.target[2].eval(t)};
    float want = r.zoom_by_distance() ? render::length(shot.target - shot.eye) * r.zoom.eval(0) : r.zoom.eval(t);
    if (pair && want > 0) { // a pair: widened to keep both cars in view, then the record's limits
        const float angle = 2.0f * std::atan(1.0f / want);
        const float wide = pair_angle(shot.eye, *pair, angle);
        if (wide > angle) want = 1.0f / std::tan(wide * 0.5f);
    }
    // The record's limits as the game applies them (the designer camera, presentation §G4e):
    // at most max, then at least min, always. min = max fixes the zoom (Grand Valley's start
    // shot: zoom(0) is 0 there, a telephoto from 22.9); min > max gives min.
    want = std::max(std::min(want, r.zoom_max), r.zoom_min);
    want = std::max(want, 0.2f);
    if (mode_ == 1 && wide_) want = 1.0f; // mode 1's wide shot: 90 degrees across
    // Smoothed like the game (exact rate not decoded); snap on a cut.
    zoom_ = zoom_ <= 0 ? want : zoom_ + (want - zoom_) * float(1 - std::exp(-4.0 * dt));
    shot.fovy = zoom_fovy(zoom_);
    shot.valid = true;
    return shot;
}

ReplayDirector::Shot ReplayDirector::update(float s, int lap, const render::Vec3& car, double dt, float aspect,
                                            const Pair* pair) {
    (void)aspect;
    Shot shot;
    if (!s_ || s_->records.empty() || s_->lap_length <= 0) return shot;
    const float L = s_->lap_length;
    if (mode_ == 1) { // the shot of the last avant_garde_cut holds until the next cut
        if (proc_.kind >= 0) {
            shot.valid = true;
            shot.procedural = proc_.kind;
            shot.seconds = float(shot_time_);
        } else if (cur_ >= 0) {
            const GtrcRecord& r = s_->records[size_t(cur_)];
            float D = float(k_) * L + s + offset_;
            if (D < r.start - L * 0.5f) offset_ += L, D += L; // the car crossed the line during the shot
            shot = evaluate(r, D, car, dt, pair);
            // The roll swing, 60 Hz: v += (-30 x - 10 v) / 60 (|v| <= 900), x += v / 60; added
            // for records aiming at the car.
            for (swing_acc_ += dt * 60.0; swing_acc_ >= 1.0; swing_acc_ -= 1.0) {
                swing_v_ = std::clamp(swing_v_ + (-30.0f * swing_ - 10.0f * swing_v_) / 60.0f, -900.0f, 900.0f);
                swing_ += swing_v_ / 60.0f;
            }
            shot.roll = (roll_ + (r.looks_at_car() ? swing_ : 0.0f)) * 3.14159265f / 180.0f;
        }
        shot_time_ += dt;
        return shot;
    }
    // Modes 0, 2, 3: the course camera's shot picker, then the shot.
    const int k = s_->lap_variant(lap);
    const float D = float(k) * L + s;
    if (pick(k, s, D) && mode_ == 2) {
        // Broadcast: at every new shot a draw; with p 0.07, while the followed car has a battle
        // partner, on board the partner's car.
        if (draw() < 0.07f && pair_) turn_on_board(true);
    }
    const GtrcRecord& r = s_->records[size_t(cur_)];
    const float Ds = shot_distance(D, k, s);
    const float u = (Ds - r.start) / std::max(r.end - r.start, 1e-3f);
    backwards_ = r.looks_at_car() && Ds < last_D_;
    last_D_ = Ds;
    proc_ = {};
    if (onboard_ || r.mode() == 3) { // on board: the followed car's in-car view, an on-board record's mount
        shot.valid = true;
        shot.record = cur_;
        shot.seconds = float(shot_time_);
        shot.haze = !onboard_ && r.hazy(); // an on-board record's own flag; a turn on board has none
        last_t_ = std::clamp(u, 0.0f, 1.0f);
        if (on_partner_) {
            shot.onboard = shot.on_partner = true;
        } else {
            proc_.kind = 4;
            proc_.mount = onboard_ ? 0 : int(r.mount);
            shot.procedural = 4;
        }
    } else {
        shot = evaluate(r, Ds, car, dt, pair);
        last_t_ = shot_t(r, u, shot_time_);
    }
    run_out_ = runs_out(D, k, s, shot_time_ + 1.0 / 60.0);
    shot_time_ += dt;
    onboard_time_ += dt;
    armed_time_ += dt;
    hold_ = std::max(0.0, hold_ - dt);
    manual_ = std::max(0.0, manual_ - dt);
    return shot;
}

// --- The cut clock (presentation §G14) ---

bool CutClock::event(const MusicMarkers* m, int track) const { return m && m->event(track); }
float CutClock::interval(const MusicMarkers* m, int track) const { return m ? m->interval(track) : MusicMarkers::kNone; }

void CutClock::setup(float level, float music_cuts) {
    request_ = 1;
    x_ = 4.0f;
    beats_ = beat_cuts_ = phrase_skip_ = false;
    phrases_ = outside_ = true;
    reset(level, music_cuts);
}

void CutClock::reset(float level, float music_cuts) {
    countdown_ = 60;
    level_ = std::clamp(level, 0.0f, 1.0f);
    music_cuts_ = std::clamp(music_cuts, 0.0f, 1.0f);
}

float CutClock::update_shot_length(const MusicMarkers* m, int mode) {
    float x;
    if (mode == 1) {
        x = interval(m, music_cuts_on(mode) ? 4 : 3);
    } else {
        x = interval(m, 3);
        if (phrases_) x = std::min(x, interval(m, 2));
        if (beats_) x = std::min(x, interval(m, 1));
    }
    if (x > 80.0f) x = float(countdown_) / 60.0f; // no marked song: the countdown
    return x_ = x;
}

void CutClock::update_request(const MusicMarkers* m, int mode, bool hold, GameRandom& rng) {
    const bool on = music_cuts_on(mode);
    rng.next(); // the director's generator steps every frame
    auto draw = [&rng] {
        rng.next();
        return rng.rand01();
    };
    request_ = 0;
    if (hold) countdown_ = 120;
    bool found = false;
    for (int i = 1; i <= 6; ++i)
        if ((on ? i == 4 : (i == 2 || i == 3)) && interval(m, i) < 80.0f) found = true;
    if (!found && --countdown_ < 0) request_ = 1, countdown_ = 120;
    // The music's tracks 4..9 (on the disc only track 4 has markers).
    if (event(m, 4)) phrase_skip_ = false, beat_cuts_ = false;
    if (event(m, 5)) phrase_skip_ = true, beat_cuts_ = false;
    if (event(m, 6)) phrase_skip_ = false, draw(), beat_cuts_ = true;
    if (event(m, 7)) phrase_skip_ = true, draw(), beat_cuts_ = true;
    if (event(m, 8)) outside_ = true;
    if (event(m, 9)) outside_ = false;
    if (mode == 1) {
        if (on ? event(m, 4) : event(m, 3)) request_ = on ? 4 : 3;
        return;
    }
    if (event(m, 3)) request_ = 3;
    if (event(m, 1)) {
        beats_ = false;
        if (beat_cuts_) request_ = 1, beats_ = true;
    }
    if (event(m, 2)) {
        if (phrases_) request_ = 2;
        phrases_ = true;
        if (phrase_skip_ && draw() < 0.5f) phrases_ = false;
    }
}

void CutClock::step_level(int dir) { level_ = std::clamp(level_ + (dir > 0 ? 0.1f : -0.1f), 0.0f, 1.0f); }

bool CutClock::step_music_cuts(int dir) {
    const bool was = music_cuts_ >= 0.5f;
    music_cuts_ = std::clamp(music_cuts_ + (dir > 0 ? 1.1f : -1.1f), 0.0f, 1.0f);
    return (music_cuts_ >= 0.5f) != was;
}

// --- The shot's look ---

float ShotLook::trail_weight() const { return trail ? std::min(std::clamp(trail_timer, 0.0f, 1.0f), 0.8f) : 0.0f; }

void ShotLook::step() {
    if (!trail || !trail_fade) return;
    trail_timer -= 0.02f;
    if (trail_timer < 0) trail_fade = false;
}

void DissolveClock::start(float seconds) {
    d = std::max(seconds, 1.0f / 60.0f);
    x = std::max(1.0f - (1.0f / 60.0f) / d, 0.0f);
    snapshot = true;
}

void DissolveClock::step() {
    if (snapshot) return; // (from the frame after the snapshot)
    x = std::max(x - (1.0f / 60.0f) / d, 0.0f);
}

// --- Mode 1 ---

float ReplayDirector::draw() {
    rng_.next();
    return rng_.rand01();
}

bool ReplayDirector::avant_garde_due(int request, bool hold, bool pad) {
    const bool reset = std::exchange(reset_flag_, false);
    look_.step();
    if (hold) request = 0;
    return request > 0 || reset || pad;
}

std::optional<std::pair<int, int>> ReplayDirector::find_free_shot(const FieldCar& c, float min_seconds) const {
    if (!s_ || s_->records.empty() || s_->lap_length <= 0) return std::nullopt;
    const float L = s_->lap_length;
    const int nvar = s_->variants();
    const GtrcRecord* last = last_record_ >= 0 && last_record_ < int(s_->records.size()) ? &s_->records[size_t(last_record_)]
                                                                                       : nullptr;
    const float lx = last ? last->pos[0].eval(0) : 0, lz = last ? last->pos[2].eval(0) : 0;
    int k = std::max(s_->lap_variant(c.lap), 1);
    for (int v = 0; v < nvar; ++v, ++k) {
        if (k > nvar) k = 1;
        const float D = float(k) * L + c.s;
        for (size_t i = 0; i < s_->records.size(); ++i) {
            const GtrcRecord& r = s_->records[i];
            if (int(i) == last_record_ || r.end <= r.start || D < r.start || D >= r.end) continue;
            const float u = (D - r.start) / (r.end - r.start);
            if (u < 0 || u > 0.8f) continue;
            // Time left in the record at the car's speed (at least 3 m/s either way).
            const float left = c.speed < 0 ? (r.start - D) / std::min(c.speed, -3.0f) : (r.end - D) / std::max(c.speed, 3.0f);
            if (left < min_seconds) continue;
            if (last) { // a visible cut: the camera starts 15 m or more from the last shot's
                const float dx = r.pos[0].eval(0) - lx, dz = r.pos[2].eval(0) - lz;
                if (dx * dx + dz * dz < 225.0f) continue;
            }
            return std::make_pair(int(i), k);
        }
    }
    return std::nullopt;
}

int ReplayDirector::procedural_kind(float r, float shot_seconds, bool hold) {
    static const float kShort[5] = {0.0f, 0.3f, 0.5f, 0.7f, 1.0f};  // cumulative, shots under 3 s
    static const float kLong[5] = {0.35f, 0.6f, 0.7f, 0.85f, 1.0f}; // 3 s or more
    static const float kHold[5] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f};   // the standing-start hold
    const float* t = hold ? kHold : shot_seconds >= 3.0f ? kLong : kShort;
    for (int i = 0; i < 5; ++i)
        if (r < t[i]) return i;
    return 4;
}

ReplayDirector::Cut ReplayDirector::avant_garde_cut(const std::vector<FieldCar>& cars, int followed, bool pick_other,
                                                    float x, int request, bool hold, u32 camera_seed) {
    Cut cut;
    const int n = int(cars.size());
    if (n == 0 || !s_) return cut;
    followed = std::clamp(followed, 0, n - 1);
    if (pick_other && n > 1) { // a random car other than the one followed
        int i = int(draw() * float(n));
        if (i == followed) ++i;
        if (i >= n) i = 0;
        followed = i;
    }
    // The first car from there with a free course shot lasting x (none: the car picked first,
    // aimed with its partner again).
    std::optional<std::pair<int, int>> found;
    int tries = 0;
    for (; tries < n && !found; ++tries) {
        const int c = (followed + tries) % n;
        if ((found = find_free_shot(cars[size_t(c)], x))) followed = c;
    }
    cut.car = followed;
    cut.pair = !(request == 1 && found && tries == 1);
    proc_ = {};
    onboard_ = on_partner_ = false; // (the course camera keeps its record for a procedural camera)
    wide_ = false, swing_ = swing_v_ = 0, swing_acc_ = 0;
    if (found) {
        roll_ = draw() * 20.0f - 10.0f;
        cur_ = found->first, k_ = found->second, offset_ = 0;
        variant_ = found->second, k0_ = s_->lap_variant(cars[size_t(followed)].lap);
        last_record_ = found->first;
        wide_ = draw() < 0.17f;
        if (draw() < 0.3f) rng_.rand01(); // a yaw seed for the aim's damper (no visible effect)
        if (draw() < 0.1f) swing_ = draw() * 200.0f - 100.0f;
    }
    // With p 0.25 (and always without a course shot) one of the procedural cameras instead.
    float trail_bias = 0;
    if (draw() < 0.25f || !found) {
        proc_.kind = procedural_kind(draw(), x, hold);
        proc_.x = x;
        proc_.seed = camera_seed;
        if (proc_.kind == 2) trail_bias = 0.5f; // the orbit camera: the trail at p 0.6
        if (proc_.kind == 4) { // a mount 3 .. m - 1 of the car's camera table (0 with three or fewer)
            const int m = cars[size_t(followed)].mounts;
            proc_.mount = m - 3 > 0 ? std::clamp(int(draw() * float(m - 3)), 0, m - 4) + 3 : 0;
        }
    }
    cut.dissolve = roll_look(trail_bias, request == 3 ? -0.3f : 0.0f) ? 0.5f : 0.0f;
    zoom_ = 0;
    shot_time_ = 0;
    ++serial_;
    return cut;
}

bool ReplayDirector::roll_look(float trail_bias, float dissolve_bias) {
    ShotLook& f = look_;
    // Levels (p 0.25), swapped (a negative) when the first draw is under 0.0125.
    const float lv = draw();
    f.levels = lv < 0.25f;
    if (f.levels) {
        f.black = std::min(draw() * 0.175f, 0.175f);
        f.white = 0.2f + 0.8f * draw();
        if (lv < 0.0125f) std::swap(f.black, f.white);
    } else {
        f.black = 0, f.white = 1;
    }
    // The trail.
    f.trail = draw() - trail_bias < 0.1f;
    if (f.trail) {
        f.trail_timer = (150.0f * draw() + 50.0f) / 100.0f;
        f.trail_fade = draw() < 0.5f;
    }
    // The dissolve.
    const bool dissolve = draw() - dissolve_bias < 0.4f;
    // The colour filter: kept with p 0.8; off, turned on with p 0.1 (saturation, then the preset).
    if (f.filter) {
        f.filter = draw() < 0.8f;
    } else if (draw() < 0.1f) {
        const float q = draw();
        f.saturation = q < 0.7f ? 1.0f : q < 0.85f ? draw() : 0.0f;
        static const float kPreset[5][3] = {{1, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0.3f, 0.3f, 0.3f}, {1, 1, 1}};
        const int p = int(5 * draw()) % 5;
        for (int i = 0; i < 3; ++i) f.curve[i] = kPreset[p][i];
        f.filter = true;
    }
    return dissolve;
}

} // namespace gt3r::race
