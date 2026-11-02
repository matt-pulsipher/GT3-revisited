#include "race/replay.h"

#include "base/file.h"
#include "base/log.h"
#include "sim/exact/race_record.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <string_view>

namespace gt3r::race {

namespace {

constexpr char kMagic[8] = {'G', 'T', '3', 'R', 'R', 'P', 'L', '1'};

class Writer {
public:
    template <class T>
    void put(T v) {
        u8 b[sizeof(T)];
        std::memcpy(b, &v, sizeof(T)); // little-endian hosts only (all our targets)
        out.insert(out.end(), b, b + sizeof(T));
    }
    void str(const std::string& s) {
        put<u16>(u16(std::min<size_t>(s.size(), 0xFFFF)));
        out.insert(out.end(), s.begin(), s.begin() + std::min<size_t>(s.size(), 0xFFFF));
    }
    void pose(const TrackPose& p) {
        for (float f : {p.position.x, p.position.y, p.position.z, p.orientation.w, p.orientation.x, p.orientation.y,
                        p.orientation.z})
            put<float>(f);
    }
    Bytes out;
};

class Reader {
public:
    explicit Reader(ByteSpan d) : d_(d) {}
    template <class T>
    T get() {
        if (pos_ + sizeof(T) > d_.size()) throw FormatError("replay truncated");
        T v;
        std::memcpy(&v, d_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return v;
    }
    std::string str() {
        u16 n = get<u16>();
        if (pos_ + n > d_.size()) throw FormatError("replay truncated");
        std::string s(reinterpret_cast<const char*>(d_.data() + pos_), n);
        pos_ += n;
        return s;
    }
    TrackPose pose() {
        TrackPose p;
        p.position.x = get<float>(), p.position.y = get<float>(), p.position.z = get<float>();
        p.orientation.w = get<float>(), p.orientation.x = get<float>(), p.orientation.y = get<float>();
        p.orientation.z = get<float>();
        return p;
    }
    ByteSpan bytes(size_t n) {
        if (pos_ + n > d_.size()) throw FormatError("replay truncated");
        ByteSpan s = d_.subspan(pos_, n);
        pos_ += n;
        return s;
    }
    size_t pos() const { return pos_; }

private:
    ByteSpan d_;
    size_t pos_ = 0;
};

s16 q_steer(float v) { return s16(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); }
u16 q_pedal(float v) { return u16(std::lround(std::clamp(v, 0.0f, 1.0f) * 65535.0f)); }

} // namespace

const char* replay_kind_name(ReplayKind k) {
    switch (k) {
    case ReplayKind::TimeTrial: return "Time Trial";
    case ReplayKind::ArcadeRace: return "Arcade";
    case ReplayKind::TwoPlayer: return "2P Battle";
    case ReplayKind::GtRace: return "GT Mode";
    case ReplayKind::License: return "License";
    case ReplayKind::Practice: return "Practice";
    case ReplayKind::Online: return "Online";
    default: return "-";
    }
}

sim::ControlInputs quantize(const sim::ControlInputs& in) {
    sim::ControlInputs q = in;
    q.steer = float(q_steer(in.steer)) / 32767.0f;
    q.throttle = float(q_pedal(in.throttle)) / 65535.0f;
    q.brake = float(q_pedal(in.brake)) / 65535.0f;
    return q;
}

Bytes Replay::serialize() const {
    Writer w;
    w.out.insert(w.out.end(), kMagic, kMagic + 8);
    w.put<u32>(kVersion);
    w.put<u32>(ticks());
    w.put<double>(tick_hz);
    w.str(course);
    w.str(car);
    w.put<u8>(automatic ? 1 : 0);
    w.put<u8>(u8(std::clamp(paint, 0, 255)));
    w.pose(start);
    w.put<u32>(0); // (no events)
    w.put<double>(best_lap);
    w.put<u32>(best_lap_begin);
    w.put<u32>(best_lap_end);
    w.put<float>(final_position.x), w.put<float>(final_position.y), w.put<float>(final_position.z);
    w.str(event);
    w.put<u16>(u16(std::min<size_t>(opponents.size(), 64)));
    for (size_t i = 0; i < opponents.size() && i < 64; ++i) w.str(opponents[i]);
    w.str(car2);
    w.put<float>(start_speed);
    w.put<u32>(paint_seed);
    w.str(comment);
    w.put<u8>(u8(kind));
    w.put<u16>(u16(std::clamp(laps, 0, 0xFFFF)));
    w.put<double>(total_time);
    w.put<float>(power_ps);
    w.str(car_model);
    w.str(wheel);
    w.put<s64>(saved);
    w.put<u32>(0); // (no car spec)
    w.put<u32>(condition.odometer);
    w.put<float>(condition.wear_m);
    w.put<float>(condition.oil_m);
    auto rule = [](const std::optional<bool>& b) { return u8(!b ? 0 : *b ? 2 : 1); };
    w.put<u8>(rule(grid_by_power));
    if (!car2.empty()) {
        w.put<u8>(battle ? 1 : 0);
        if (battle) {
            const ReplayBattle& b = *battle;
            w.put<u16>(u16(std::clamp(b.laps, 0, 0xFFFF)));
            w.put<float>(b.catchup == 1 ? 0.15f : b.catchup == 2 ? 0.20f : 0.0f), w.put<float>(0), w.put<float>(0);
            w.put<float>(b.handicap_m);
            w.put<u8>(b.tyre_damage ? 1 : 0);
            w.str(b.model2);
            w.str(b.wheel2);
            w.put<u8>(u8(std::clamp(b.paint2, 0, 255)));
            w.put<u32>(0); // (no car spec)
        }
    }
    {
        const Bytes xb = exact ? exact->serialize() : Bytes{};
        w.put<u32>(u32(xb.size()));
        w.out.insert(w.out.end(), xb.begin(), xb.end());
    }
    for (size_t i = 0; i < inputs.size(); ++i) {
        const sim::ControlInputs& in = inputs[i];
        u8 dk = i < driver_kind.size() ? driver_kind[i] : 0;
        w.put<s16>(q_steer(in.steer));
        w.put<u16>(q_pedal(in.throttle));
        w.put<u16>(q_pedal(in.brake));
        w.put<u8>(u8((in.handbrake ? 1 : 0) | (in.shift_up ? 2 : 0) | (in.shift_down ? 4 : 0) | (in.automatic ? 8 : 0) | (in.controller_kind == 2 ? 16 : 0) | (in.controller_kind == 3 ? 32 : 0) |
                     (dk == 1 ? 64 : dk == 5 ? 128 : dk == 8 ? 192 : 0)));
        w.put<u8>(u8(i < reverse.size() && reverse[i] ? 1 : 0));
    }
    if (!car2.empty())
        for (size_t i = 0; i < inputs.size(); ++i) {
            sim::ControlInputs in = i < inputs2.size() ? inputs2[i] : sim::ControlInputs{};
            w.put<s16>(q_steer(in.steer));
            w.put<u16>(q_pedal(in.throttle));
            w.put<u16>(q_pedal(in.brake));
            w.put<u8>(u8((in.handbrake ? 1 : 0) | (in.shift_up ? 2 : 0) | (in.shift_down ? 4 : 0) | (in.automatic ? 8 : 0) | (in.controller_kind == 2 ? 16 : 0) | (in.controller_kind == 3 ? 32 : 0)));
            w.put<u8>(u8(i < reverse2.size() && reverse2[i] ? 1 : 0));
        }
    return std::move(w.out);
}

Replay Replay::parse(ByteSpan data) {
    if (data.size() < 8 || std::memcmp(data.data(), kMagic, 8) != 0) throw FormatError("not a GT3 Revisited replay");
    Reader r(data.subspan(8));
    Replay rp;
    u32 version = r.get<u32>();
    if (version >= 1 && version < kVersion) throw ReplayTooOld(kReplayTooOld);
    if (version != kVersion) throw FormatError("unsupported replay version " + std::to_string(version));
    auto skip_spec = [&] { // (a car spec of a run before the exact core: not used)
        const u32 ns = r.get<u32>();
        if (ns > data.size()) throw FormatError("replay truncated");
        r.bytes(ns);
    };
    u32 n = r.get<u32>();
    rp.tick_hz = r.get<double>();
    if (!(rp.tick_hz > 1 && rp.tick_hz < 10000)) throw FormatError("bad replay tick rate");
    rp.course = r.str();
    rp.car = r.str();
    rp.automatic = r.get<u8>() != 0;
    rp.paint = r.get<u8>();
    rp.start = r.pose();
    u32 ne = r.get<u32>();
    if (ne > n + 1) throw FormatError("bad replay event count");
    for (u32 i = 0; i < ne; ++i) r.get<u32>(), r.get<u8>(), r.pose(); // (respawn events: none in a race on the exact core)
    rp.best_lap = r.get<double>();
    rp.best_lap_begin = r.get<u32>();
    rp.best_lap_end = r.get<u32>();
    rp.final_position.x = r.get<float>(), rp.final_position.y = r.get<float>(), rp.final_position.z = r.get<float>();
    rp.event = r.str();
    u16 no = r.get<u16>();
    if (no > 64) throw FormatError("bad replay opponent count");
    for (u16 i = 0; i < no; ++i) rp.opponents.push_back(r.str());
    rp.car2 = r.str();
    rp.start_speed = r.get<float>();
    rp.paint_seed = r.get<u32>();
    rp.comment = r.str();
    rp.kind = ReplayKind(r.get<u8>());
    rp.laps = r.get<u16>();
    rp.total_time = r.get<double>();
    rp.power_ps = r.get<float>();
    rp.car_model = r.str();
    rp.wheel = r.str();
    rp.saved = r.get<s64>();
    skip_spec();
    rp.condition.odometer = r.get<u32>();
    rp.condition.wear_m = r.get<float>();
    rp.condition.oil_m = r.get<float>();
    {
        auto rule = [](u8 v) { return v == 0 ? std::nullopt : std::optional<bool>(v == 2); };
        u8 f = r.get<u8>();
        rp.grid_by_power = rule(f & 3);
        if (!rp.car2.empty() && r.get<u8>()) {
            ReplayBattle b;
            b.laps = r.get<u16>();
            const float catchup = r.get<float>();
            r.get<float>(), r.get<float>();
            b.catchup = catchup <= 0 ? 0 : catchup < 0.18f ? 1 : 2;
            b.handicap_m = r.get<float>();
            b.tyre_damage = r.get<u8>() != 0;
            b.model2 = r.str();
            b.wheel2 = r.str();
            b.paint2 = r.get<u8>();
            skip_spec();
            rp.battle = std::move(b);
        }
    }
    const u32 nx = r.get<u32>();
    if (!nx) throw ReplayTooOld(kReplayTooOld); // (a run before every race ran on the exact core)
    if (nx > data.size()) throw FormatError("replay truncated");
    rp.exact = std::make_shared<const sim::exact::RaceRecord>(sim::exact::RaceRecord::parse(r.bytes(nx)));
    const bool flags2 = true; // the second flags byte (reverse)
    if (size_t(n) * 8 > data.size()) throw FormatError("replay truncated");
    rp.inputs.reserve(n);
    for (u32 i = 0; i < n; ++i) {
        sim::ControlInputs in;
        in.steer = float(r.get<s16>()) / 32767.0f;
        in.throttle = float(r.get<u16>()) / 65535.0f;
        in.brake = float(r.get<u16>()) / 65535.0f;
        u8 f = r.get<u8>();
        in.handbrake = f & 1, in.shift_up = f & 2, in.shift_down = f & 4, in.automatic = f & 8;
            in.controller_kind = (f & 16) ? 2 : (f & 32) ? 3 : 0;
        rp.inputs.push_back(in);
        rp.driver_kind.push_back(u8((f & 192) == 192 ? 8 : (f & 64) ? 1 : (f & 128) ? 5 : 0));
        if (flags2) rp.reverse.push_back(u8(r.get<u8>() & 1));
    }
    if (!rp.car2.empty())
        for (u32 i = 0; i < n; ++i) {
            sim::ControlInputs in;
            in.steer = float(r.get<s16>()) / 32767.0f;
            in.throttle = float(r.get<u16>()) / 65535.0f;
            in.brake = float(r.get<u16>()) / 65535.0f;
            u8 f = r.get<u8>();
            in.handbrake = f & 1, in.shift_up = f & 2, in.shift_down = f & 4, in.automatic = f & 8;
            in.controller_kind = (f & 16) ? 2 : (f & 32) ? 3 : 0;
            rp.inputs2.push_back(in);
            if (flags2) rp.reverse2.push_back(u8(r.get<u8>() & 1));
        }
    return rp;
}

void ReplayRecorder::begin(std::string course, std::string car, bool automatic, int paint, const TrackPose& start,
                           double hz, std::string event, std::vector<std::string> opponents) {
    replay_ = {};
    replay_.course = std::move(course);
    replay_.car = std::move(car);
    replay_.automatic = automatic;
    replay_.paint = paint;
    replay_.start = start;
    replay_.tick_hz = hz;
    replay_.event = std::move(event);
    replay_.opponents = std::move(opponents);
    active_ = true;
}

sim::ControlInputs ReplayRecorder::record(const sim::ControlInputs& in) {
    sim::ControlInputs q = quantize(in);
    if (active_) replay_.inputs.push_back(q);
    return q;
}

void ReplayRecorder::set_driver_kind(int kind) {
    if (!active_ || replay_.inputs.empty()) return;
    replay_.driver_kind.resize(replay_.inputs.size(), 0);
    // Kinds 5-7 (the stop driver) take the same physics path; 2-4 (the pit lane) are re-run.
    replay_.driver_kind.back() = u8(kind == 1 || kind == 8 ? kind : kind >= 5 && kind <= 7 ? 5 : 0);
}

void ReplayRecorder::set_reverse(bool on) {
    if (!active_ || replay_.inputs.empty()) return;
    replay_.reverse.resize(replay_.inputs.size(), 0);
    replay_.reverse.back() = on ? 1 : 0;
}

void ReplayRecorder::set_second_reverse(bool on) {
    if (!active_ || replay_.inputs2.empty()) return;
    replay_.reverse2.resize(replay_.inputs2.size(), 0);
    replay_.reverse2.back() = on ? 1 : 0;
}

sim::ControlInputs ReplayRecorder::record_second(const sim::ControlInputs& in) {
    sim::ControlInputs q = quantize(in);
    if (active_) replay_.inputs2.push_back(q);
    return q;
}

void ReplayRecorder::lap(double time, u32 begin_tick, u32 end_tick) {
    if (!active_ || time <= 0) return;
    if (replay_.best_lap < 0 || time < replay_.best_lap) {
        replay_.best_lap = time;
        replay_.best_lap_begin = begin_tick;
        replay_.best_lap_end = end_tick;
    }
}

sim::ControlInputs ReplayPlayer::second_inputs() const {
    if (!r_ || tick_ == 0 || tick_ - 1 >= r_->inputs2.size()) return {};
    return r_->inputs2[tick_ - 1];
}

int ReplayPlayer::driver_kind() const {
    if (!r_ || tick_ == 0 || tick_ - 1 >= r_->driver_kind.size()) return 0;
    return r_->driver_kind[tick_ - 1];
}

sim::ControlInputs ReplayPlayer::next_inputs() {
    if (done()) return {};
    return r_->inputs[tick_++];
}

std::optional<std::string> save_replay(const Replay& r, const std::string& dir) {
    host_make_dir(dir);
    std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
    const std::string base = dir + (dir.empty() || dir.back() == '/' ? "" : "/") + r.course + "_" + r.car + "_";
    std::string path = base + stamp + ".gt3replay";
    // Two saves in the same second: the next free second's stamp (names stay sortable).
    for (int i = 1; i < 60 && host_path_exists(path); ++i) {
        std::time_t t = now + i;
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&t));
        path = base + stamp + ".gt3replay";
    }
    Replay copy = r;
    if (copy.saved == 0) copy.saved = s64(now);
    Bytes b = copy.serialize();
    if (!write_host_file(path, b)) return std::nullopt;
    LOG_INFO("Replay saved: %s (%u ticks, %.1f s)", path.c_str(), r.ticks(), r.duration());
    return path;
}

std::vector<ReplayFile> list_replays(const std::string& dir) {
    std::vector<ReplayFile> out;
    constexpr std::string_view kExt = ".gt3replay";
    for (const HostDirEntry& e : host_dir_entries(dir)) {
        if (e.dir || e.name.size() <= kExt.size() || e.name.compare(e.name.size() - kExt.size(), kExt.size(), kExt) != 0)
            continue;
        out.push_back({dir + (dir.empty() || dir.back() == '/' ? "" : "/") + e.name, e.name});
    }
    // File names end in a timestamp: newest first by name order within a course/car, then overall by stamp.
    auto stamp = [](const std::string& n) { return n.size() > 25 ? n.substr(n.size() - 25) : n; };
    std::sort(out.begin(), out.end(), [&](const ReplayFile& a, const ReplayFile& b) { return stamp(a.name) > stamp(b.name); });
    return out;
}

} // namespace gt3r::race
