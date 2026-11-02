// A race's record for replays; see race_record.h.

#include "sim/exact/race_record.h"

#include "sim/exact/car_fields.h"

#include <algorithm>
#include <cstring>

namespace gt3r::sim::exact {

namespace {

class Writer {
public:
    template <class T>
    void put(T v) {
        u8 b[sizeof(T)];
        std::memcpy(b, &v, sizeof(T)); // little-endian hosts only (all our targets)
        out.insert(out.end(), b, b + sizeof(T));
    }
    void var(u32 v) { // LEB128
        while (v >= 0x80) out.push_back(u8(v | 0x80)), v >>= 7;
        out.push_back(u8(v));
    }
    void str(const std::string& s) {
        var(u32(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
    void bytes(ByteSpan b) { out.insert(out.end(), b.begin(), b.end()); }
    Bytes out;
};

class Reader {
public:
    explicit Reader(ByteSpan d) : d_(d) {}
    template <class T>
    T get() {
        if (d_.size() - pos_ < sizeof(T)) throw FormatError("race record truncated");
        T v;
        std::memcpy(&v, d_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return v;
    }
    u32 var() {
        u32 v = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            const u8 b = get<u8>();
            v |= u32(b & 0x7F) << shift;
            if (!(b & 0x80)) return v;
        }
        throw FormatError("race record: bad number");
    }
    ByteSpan bytes(size_t n) {
        if (d_.size() - pos_ < n) throw FormatError("race record truncated");
        ByteSpan s = d_.subspan(pos_, n);
        pos_ += n;
        return s;
    }
    std::string str() {
        const ByteSpan b = bytes(var());
        return std::string(reinterpret_cast<const char*>(b.data()), b.size());
    }
    bool at_end() const { return pos_ == d_.size(); }

private:
    ByteSpan d_;
    size_t pos_ = 0;
};

void put_entry(Writer& w, const StartEntry& e) {
    w.put<u64>(e.eq.car);
    for (u64 p : e.eq.part) w.put<u64>(p);
    for (s32 s : e.eq.setting) w.put<s32>(s);
    w.put<u32>(e.human);
    w.put<u8>(e.player ? 1 : 0);
    w.put<s32>(e.grid_slot);
    w.put<s32>(e.pedal_style);
    w.put<s32>(e.spec_flag);
    w.put<s32>(e.wheel_type);
    w.put<u8>(u8((e.asm_on ? 1 : 0) | (e.tcs_on ? 2 : 0)));
    for (u32 c : e.condition) w.put<u32>(c);
    w.put<u32>(e.best_lap);
    w.put<u32>(e.car);
}

void put_splits(Writer& w, const Splits& s) {
    for (u32 t : s.sector) w.put<u32>(t);
    w.put<u32>(s.lap);
}

Splits get_splits(Reader& r) {
    Splits s;
    for (u32& t : s.sector) t = r.get<u32>();
    s.lap = r.get<u32>();
    return s;
}

void put_records(Writer& w, const EntryRecords& e) {
    w.put<u32>(e.laps.counted_only), w.put<u32>(e.laps.count), w.put<u32>(e.laps.dropped), w.put<u32>(e.laps.total);
    w.var(u32(e.laps.laps.size()));
    for (u32 t : e.laps.laps) w.put<u32>(t);
    put_splits(w, e.best), put_splits(w, e.current);
    w.put<u32>(e.best_lap);
    w.put<u32>(e.top_speed.bits);
    w.var(u32(e.words.size()));
    for (const auto& [off, v] : e.words) w.put<u32>(off), w.put<u32>(v);
}

EntryRecords get_records(Reader& r, u32 version) {
    EntryRecords e;
    e.laps.counted_only = r.get<u32>(), e.laps.count = r.get<u32>(), e.laps.dropped = r.get<u32>(), e.laps.total = r.get<u32>();
    const u32 n = r.var();
    if (n > 100000) throw FormatError("race record: bad lap list");
    for (u32 k = 0; k < n; ++k) e.laps.laps.push_back(r.get<u32>());
    e.best = get_splits(r), e.current = get_splits(r);
    e.best_lap = r.get<u32>();
    e.top_speed = F::raw(r.get<u32>());
    if (version >= 2) {
        const u32 n = r.var();
        if (n > 100000) throw FormatError("race record: bad entry words");
        for (u32 k = 0; k < n; ++k) {
            const u32 off = r.get<u32>();
            e.words[off] = r.get<u32>();
        }
    }
    return e;
}

StartEntry get_entry(Reader& r) {
    StartEntry e;
    e.eq.car = r.get<u64>();
    for (u64& p : e.eq.part) p = r.get<u64>();
    for (s32& s : e.eq.setting) s = r.get<s32>();
    e.human = r.get<u32>();
    e.player = r.get<u8>() != 0;
    e.grid_slot = r.get<s32>();
    e.pedal_style = r.get<s32>();
    e.spec_flag = r.get<s32>();
    e.wheel_type = r.get<s32>();
    const u8 aids = r.get<u8>();
    e.asm_on = (aids & 1) != 0, e.tcs_on = (aids & 2) != 0;
    for (u32& c : e.condition) c = r.get<u32>();
    e.best_lap = r.get<u32>();
    e.car = r.get<u32>();
    return e;
}

// A car's frames: per frame a mask of the fields that changed from the frame before (bit 0 flags, 1 kind, 2 shift, 3 steer,
// 4 throttle, 5 brake, 6 back, 7 third) and those fields; a run of unchanged frames as mask 0 and its length.
bool same(const PadFrame& a, const PadFrame& b) {
    return a.flags == b.flags && a.kind == b.kind && a.shift == b.shift && a.steer == b.steer && a.throttle == b.throttle &&
           a.brake == b.brake && a.back == b.back && a.third == b.third;
}

Bytes encode_pads(const std::vector<PadFrame>& pads, u32 frames) {
    Writer w;
    PadFrame prev;
    u32 run = 0;
    auto flush = [&] {
        if (run) w.put<u8>(0), w.var(run), run = 0;
    };
    for (u32 t = 0; t < frames; ++t) {
        const PadFrame f = t < pads.size() ? pads[t] : PadFrame{};
        if (same(f, prev)) {
            ++run;
            continue;
        }
        flush();
        const u8 mask = u8((f.flags != prev.flags ? 1 : 0) | (f.kind != prev.kind ? 2 : 0) | (f.shift != prev.shift ? 4 : 0) |
                           (f.steer != prev.steer ? 8 : 0) | (f.throttle != prev.throttle ? 16 : 0) |
                           (f.brake != prev.brake ? 32 : 0) | (f.back != prev.back ? 64 : 0) | (f.third != prev.third ? 128 : 0));
        w.put<u8>(mask);
        if (mask & 1) w.put<u8>(f.flags);
        if (mask & 2) w.put<u8>(f.kind);
        if (mask & 4) w.put<s8>(f.shift);
        if (mask & 8) w.put<s16>(f.steer);
        if (mask & 16) w.put<u16>(f.throttle);
        if (mask & 32) w.put<u16>(f.brake);
        if (mask & 64) w.put<u16>(f.back);
        if (mask & 128) w.put<u16>(f.third);
        prev = f;
    }
    flush();
    return std::move(w.out);
}

std::vector<PadFrame> decode_pads(ByteSpan b, u32 frames) {
    Reader r(b);
    std::vector<PadFrame> out;
    out.reserve(frames);
    PadFrame f;
    while (out.size() < frames) {
        const u8 mask = r.get<u8>();
        if (!mask) {
            const u32 run = r.var();
            if (run == 0 || run > frames - out.size()) throw FormatError("race record: bad frame run");
            out.insert(out.end(), run, f);
            continue;
        }
        if (mask & 1) f.flags = r.get<u8>();
        if (mask & 2) f.kind = r.get<u8>();
        if (mask & 4) f.shift = r.get<s8>();
        if (mask & 8) f.steer = r.get<s16>();
        if (mask & 16) f.throttle = r.get<u16>();
        if (mask & 32) f.brake = r.get<u16>();
        if (mask & 64) f.back = r.get<u16>();
        if (mask & 128) f.third = r.get<u16>();
        out.push_back(f);
    }
    if (!r.at_end()) throw FormatError("race record: frames past the end");
    return out;
}

std::vector<PadFrame> get_frames(Reader& r) {
    const u32 n = r.var();
    if (n > 60u * 60 * 60 * 24) throw FormatError("race record: bad frame count");
    const u32 len = r.var();
    return decode_pads(r.bytes(len), n);
}

// A snapshot: its words, the car's record image and the collision body's points.
void put_snapshot(Writer& w, const Snapshot& s) {
    w.put<u8>(s.sub), w.put<u8>(s.phase), w.put<u32>(s.frame), w.put<u32>(s.frames), w.put<u32>(s.frac.bits), w.put<u32>(s.time);
    std::vector<u8> image(kCarSize, 0);
    store_image(s.car, image.data());
    w.bytes(ByteSpan(image.data(), image.size()));
    w.put<u8>(s.has_body ? 1 : 0);
    if (!s.has_body) return;
    for (const collide::exact::Point& p : s.body) {
        for (F v : {p.lx, p.ly, p.ux, p.uy, p.sx, p.sy, p.ex, p.ey}) w.put<u32>(v.bits);
        w.put<u32>(p.flags), w.put<u8>(p.hit ? 1 : 0), w.put<u32>(p.t.bits), w.put<s32>(p.wall);
        w.put<u32>(p.tx.bits), w.put<u32>(p.ty.bits), w.put<s32>(p.other), w.put<s32>(p.edge);
    }
}

Snapshot get_snapshot(Reader& r, const Behaviour& how) {
    Snapshot s;
    s.sub = r.get<u8>(), s.phase = r.get<u8>(), s.frame = r.get<u32>(), s.frames = r.get<u32>();
    s.frac = F::raw(r.get<u32>()), s.time = r.get<u32>();
    s.car.behaviour = how;
    load_image(s.car, r.bytes(kCarSize).data());
    s.has_body = r.get<u8>() != 0;
    if (!s.has_body) return s;
    for (collide::exact::Point& p : s.body) {
        for (F* v : {&p.lx, &p.ly, &p.ux, &p.uy, &p.sx, &p.sy, &p.ex, &p.ey}) *v = F::raw(r.get<u32>());
        p.flags = r.get<u32>(), p.hit = r.get<u8>() != 0, p.t = F::raw(r.get<u32>()), p.wall = r.get<s32>();
        p.tx = F::raw(r.get<u32>()), p.ty = F::raw(r.get<u32>()), p.other = r.get<s32>(), p.edge = r.get<s32>();
    }
    return s;
}

// The record memory's pool as its runs of bytes that are not zero (gaps of 64 zero bytes or more left out).
void put_pool(Writer& w, const std::vector<u8>& pool) {
    std::vector<std::pair<u32, u32>> runs;
    for (u32 k = 0; k < pool.size();) {
        if (pool[k] == 0) {
            ++k;
            continue;
        }
        u32 end = k, zeros = 0;
        for (u32 j = k; j < pool.size() && zeros < 64; ++j) {
            if (pool[j] != 0) end = j + 1, zeros = 0;
            else ++zeros;
        }
        runs.push_back({k, end - k});
        k = end;
    }
    w.var(u32(runs.size()));
    for (const auto& [at, len] : runs) w.var(at), w.var(len), w.bytes(ByteSpan(pool.data() + at, len));
}

void get_pool(Reader& r, std::vector<u8>& pool) {
    std::fill(pool.begin(), pool.end(), u8(0));
    const u32 n = r.var();
    if (n > pool.size()) throw FormatError("race record: bad record memory");
    for (u32 k = 0; k < n; ++k) {
        const u32 at = r.var(), len = r.var();
        if (at > pool.size() || len > pool.size() - at) throw FormatError("race record: bad record memory");
        const ByteSpan b = r.bytes(len);
        std::copy(b.begin(), b.end(), pool.begin() + at);
    }
}

// The recorders at a run's start: the race mode's kind of them and their switches, pool 1's stores and snapshots (pool 0, the
// player's, is recorded anew at every run's start; the managers' places follow from the kind: SoloRecorders' constructor).
void put_recorders(Writer& w, const SoloRecorders& s) {
    w.put<u8>(u8((s.free_run ? 1 : 0) | (s.ghost_option ? 2 : 0) | (s.ghost_empty ? 4 : 0)));
    for (const Snapshot& h : s.snapshot) put_snapshot(w, h);
    put_pool(w, s.pool1);
}

std::shared_ptr<const SoloRecorders> get_recorders(Reader& r, const Behaviour& how) {
    const u8 f = r.get<u8>();
    auto s = std::make_shared<SoloRecorders>((f & 1) != 0);
    s->ghost_option = (f & 2) != 0, s->ghost_empty = (f & 4) != 0;
    for (Snapshot& h : s->snapshot) h = get_snapshot(r, how);
    get_pool(r, s->pool1);
    s->start();
    return s;
}

// Version 2's free run recorders (their frames as lists): recorded on the stores as the program records them.
std::shared_ptr<const SoloRecorders> get_free_run_v2(Reader& r, const Behaviour& how) {
    auto s = std::make_shared<SoloRecorders>(true);
    s->snapshot[1] = get_snapshot(r, how), s->snapshot[2] = get_snapshot(r, how), s->snapshot[0] = get_snapshot(r, how);
    s->ghost_empty = r.get<u8>() == 0;
    r.get<u32>(), r.get<u32>(); // (the lap's frames, the playback's place)
    for (u32 store : {1u, 2u, 0u}) {
        const std::vector<PadFrame> frames = get_frames(r);
        ControlRecorder rec;
        rec.bind(store * kRecordStore + kStoreSnapshot, kStoreBlock), rec.rewind();
        for (const PadFrame& p : frames) rec.write(s->pool1, raw_pad(p));
        rec.close(s->pool1);
    }
    s->start();
    return s;
}

constexpr char kMagic[4] = {'X', 'R', 'C', 'D'};

} // namespace

void apply_change(ExactRace& race, const RaceChange& c) {
    const bool has_car = c.car < race.size();
    switch (c.kind) {
    case RaceChange::PedalStyle:
        if (has_car) race.cars()[c.car].pedal_style = u8(c.a);
        break;
    case RaceChange::Condition:
        if (has_car) {
            Car& r = race.cars()[c.car];
            r.odometer = c.a, r.engine_wear = F::raw(c.b), r.oil = F::raw(c.c);
        }
        break;
    case RaceChange::DriveOn:
        if (has_car) race.drive_on(c.car);
        break;
    case RaceChange::HandBack:
        if (has_car) race.hand_back(c.car);
        break;
    case RaceChange::Hit:
        if (has_car) hit_course_out(race.cars()[c.car], race.kind(), s32(c.a), F::raw(c.b));
        break;
    case RaceChange::StartCountdown: race.start_countdown(); break;
    case RaceChange::StartCount: race.set_start_count(s32(c.a)); break;
    case RaceChange::StartType: race.set_start_type(s32(c.a)); break;
    case RaceChange::TimeScale: race.set_time_scale(F::raw(c.a)); break;
    case RaceChange::FlowByte: race.set_flow_byte(u8(c.a)); break;
    case RaceChange::Timer: race.set_timer(s32(c.a)); break;
    }
}

RaceSetup RaceRecord::setup(const ProgramData& d, const data::CarDatabase& db, std::shared_ptr<const RaceCourse> c) const {
    RaceSetup s;
    s.data = &d, s.db = &db, s.course = std::move(c);
    s.course_category = course_category;
    s.kind = kind;
    s.config = config;
    s.laps = laps;
    s.start_type = start_type, s.countdown = countdown;
    s.battle_laps = battle_laps, s.battle_wear = battle_wear, s.handicap = handicap, s.battle_catchup = battle_catchup;
    s.replay = replay, s.unjudged = unjudged, s.locked = locked;
    s.logger = logger, s.logger_record = logger_record;
    s.session = session;
    s.records = records;
    s.licence_times = licence_times;
    s.speed_unit = speed_unit;
    s.ghost_switch = ghost_switch;
    s.rules = &rules;
    s.entries = entries;
    s.effects = effects;
    s.info = info, s.conductor = conductor, s.course_addr = course_addr, s.road_addr = road_addr;
    s.recorders = recorders, s.ghost_car = ghost_car, s.external_ghost = external_ghost;
    s.sparks = sparks;
    return s;
}

RaceRecord RaceRecord::of(const RaceSetup& s, std::string course_label) {
    RaceRecord r;
    if (s.data) r.program = s.data->fingerprint(), r.behaviour = s.data->behaviour();
    r.course = std::move(course_label);
    r.course_category = s.course_category;
    r.kind = s.kind;
    r.config = s.config;
    r.laps = s.laps;
    r.start_type = s.start_type, r.countdown = s.countdown;
    r.battle_laps = s.battle_laps, r.battle_wear = s.battle_wear, r.handicap = s.handicap, r.battle_catchup = s.battle_catchup;
    r.replay = s.replay, r.unjudged = s.unjudged, r.locked = s.locked;
    r.logger = s.logger, r.logger_record = s.logger_record;
    r.session = s.session;
    r.records = s.records;
    r.licence_times = s.licence_times;
    r.speed_unit = s.speed_unit;
    r.ghost_switch = s.ghost_switch;
    r.rules = s.rules ? *s.rules : race_config(s.event);
    r.effects = s.effects;
    r.info = s.info, r.conductor = s.conductor, r.course_addr = s.course_addr, r.road_addr = s.road_addr;
    r.entries = s.entries;
    r.pads.assign(s.entries.size(), {});
    r.recorders = s.recorders, r.ghost_car = s.ghost_car, r.external_ghost = s.external_ghost;
    r.sparks = s.sparks;
    return r;
}

std::vector<u8> run_recording_of(const RaceRecord& r) {
    if (!r.run_recording.empty()) return r.run_recording;
    SoloRecorders s(false);
    for (u32 t = 0; t < r.frames; ++t) s.player_frame(r.pad(0, t));
    s.end();
    return s.run_recording();
}

void RaceRecord::add_change(RaceChange c) {
    c.frame = frames;
    changes.push_back(c);
}

void RaceRecord::add_frame(std::span<const PadFrame> frame_pads) {
    pads.resize(entries.size());
    // (a car's stream is kept only from its first frame that is not empty: a CPU car's stays empty)
    for (size_t i = 0; i < pads.size() && i < frame_pads.size(); ++i)
        if (!same(frame_pads[i], PadFrame{}) || !pads[i].empty()) {
            pads[i].resize(frames, PadFrame{});
            pads[i].push_back(frame_pads[i]);
        }
    ++frames;
}

void RaceRecord::add_nested(std::span<const PadFrame> p) {
    Nested n;
    n.frame = frames ? frames - 1 : 0;
    n.pads.assign(p.begin(), p.end());
    nested.push_back(std::move(n));
}

PadFrame RaceRecord::pad(int i, u32 t) const {
    if (i < 0 || size_t(i) >= pads.size() || t >= pads[size_t(i)].size()) return {};
    return pads[size_t(i)][t];
}

Bytes RaceRecord::serialize() const {
    Writer w;
    w.bytes(ByteSpan(reinterpret_cast<const u8*>(kMagic), 4));
    w.put<u32>(kVersion);
    w.put<u64>(program);
    w.put<u64>(data);
    w.put<u8>(u8(behaviour.rules));
    w.put<u8>(u8(behaviour.rate));
    w.str(course);
    w.put<u8>(course_category);
    w.put<u8>(u8(kind));
    w.put<u8>(config);
    w.put<s32>(laps);
    for (s32 v : {start_type, countdown, battle_laps, battle_wear, handicap, battle_catchup}) w.put<s32>(v);
    w.put<u8>(u8((replay ? 1 : 0) | (unjudged ? 2 : 0) | (effects ? 4 : 0) | (locked ? 8 : 0)));
    w.put<u32>(logger), w.put<u32>(logger_record);
    put_splits(w, session);
    w.var(u32(records.size()));
    for (const EntryRecords& e : records) put_records(w, e);
    for (u32 t : licence_times) w.put<u32>(t);
    w.put<u8>(speed_unit);
    w.put<u32>(ghost_switch);
    w.bytes(ByteSpan(rules.data(), rules.size()));
    w.put<u32>(info), w.put<u32>(conductor), w.put<u32>(course_addr);
    w.put<u32>(road_addr);
    w.put<u8>(u8(entries.size()));
    for (const StartEntry& e : entries) put_entry(w, e);
    w.put<u32>(frames);
    for (size_t i = 0; i < entries.size(); ++i) {
        const Bytes b = encode_pads(i < pads.size() ? pads[i] : std::vector<PadFrame>{}, frames);
        w.var(u32(b.size()));
        w.bytes(b);
    }
    w.var(u32(changes.size()));
    u32 last = 0;
    for (const RaceChange& c : changes) {
        w.var(c.frame - std::min(last, c.frame)); // (in frame order)
        last = c.frame;
        w.put<u8>(c.kind);
        w.put<u8>(c.car);
        w.put<u32>(c.a), w.put<u32>(c.b), w.put<u32>(c.c);
    }
    w.var(u32(nested.size()));
    last = 0;
    for (const Nested& n : nested) {
        w.var(n.frame - std::min(last, n.frame));
        last = n.frame;
        w.put<u8>(u8(n.pads.size()));
        for (const PadFrame& f : n.pads) {
            w.put<u8>(f.flags), w.put<u8>(f.kind), w.put<s8>(f.shift), w.put<s16>(f.steer);
            w.put<u16>(f.throttle), w.put<u16>(f.brake), w.put<u16>(f.back), w.put<u16>(f.third);
        }
    }
    w.put<u8>(recorders ? 1 : 0);
    if (recorders) put_recorders(w, *recorders);
    w.put<s8>(s8(ghost_car));
    w.put<u8>(external_ghost ? 1 : 0);
    w.var(u32(run_recording.size()));
    w.bytes(ByteSpan(run_recording.data(), run_recording.size()));
    w.var(u32(sparks.size()));
    for (const std::vector<SparkPoint>& e : sparks) {
        w.var(u32(e.size()));
        for (const SparkPoint& p : e) w.put<u32>(p.x.bits), w.put<u32>(p.y.bits), w.put<u32>(p.z.bits);
    }
    w.put<u64>(final_hash);
    return std::move(w.out);
}

RaceRecord RaceRecord::parse(ByteSpan data) {
    Reader rd(data);
    if (std::memcmp(rd.bytes(4).data(), kMagic, 4) != 0) throw FormatError("not a race record");
    const u32 version = rd.get<u32>();
    if (version < 1 || version > kVersion) throw FormatError("race record version " + std::to_string(version) + " is not supported");
    RaceRecord r;
    r.program = rd.get<u64>();
    r.data = rd.get<u64>();
    const u8 rules_of = rd.get<u8>(), rate = rd.get<u8>();
    if (rules_of >= u8(sim::kPrograms) || (rate != 60 && rate != 50)) throw FormatError("race record: unknown behaviour");
    r.behaviour = {Program(rules_of), int(rate)};
    r.course = rd.str();
    r.course_category = rd.get<u8>();
    const u8 kind = rd.get<u8>();
    if (kind > u8(Conductor::FreePractice)) throw FormatError("race record: unknown race mode");
    r.kind = Conductor(kind);
    r.config = rd.get<u8>();
    r.laps = rd.get<s32>();
    for (s32* v : {&r.start_type, &r.countdown, &r.battle_laps, &r.battle_wear, &r.handicap, &r.battle_catchup}) *v = rd.get<s32>();
    const u8 flags = rd.get<u8>();
    r.replay = (flags & 1) != 0, r.unjudged = (flags & 2) != 0, r.effects = (flags & 4) != 0, r.locked = (flags & 8) != 0;
    r.logger = rd.get<u32>(), r.logger_record = rd.get<u32>();
    r.session = get_splits(rd);
    const u32 nr = rd.var();
    if (nr > 8) throw FormatError("race record: bad records count");
    for (u32 k = 0; k < nr; ++k) r.records.push_back(get_records(rd, version));
    for (u32& t : r.licence_times) t = rd.get<u32>();
    r.speed_unit = rd.get<u8>();
    r.ghost_switch = rd.get<u32>();
    const ByteSpan rules = rd.bytes(r.rules.size());
    std::copy(rules.begin(), rules.end(), r.rules.begin());
    r.info = rd.get<u32>(), r.conductor = rd.get<u32>(), r.course_addr = rd.get<u32>();
    if (version >= 2) r.road_addr = rd.get<u32>();
    const u8 n = rd.get<u8>();
    if (n == 0 || n > 8) throw FormatError("race record: bad car count");
    for (u8 i = 0; i < n; ++i) {
        r.entries.push_back(get_entry(rd));
        if (u32(r.entries.back().grid_slot) >= 8) throw FormatError("race record: bad grid slot");
    }
    r.frames = rd.get<u32>();
    if (r.frames > 60u * 60 * 60 * 24) throw FormatError("race record: bad frame count");
    for (u8 i = 0; i < n; ++i) {
        const u32 len = rd.var();
        std::vector<PadFrame> p = decode_pads(rd.bytes(len), r.frames);
        while (!p.empty() && same(p.back(), PadFrame{})) p.pop_back(); // (empty frames past a stream's end)
        r.pads.push_back(std::move(p));
    }
    const u32 nc = rd.var();
    if (nc > data.size()) throw FormatError("race record: bad change count");
    u32 frame = 0;
    for (u32 k = 0; k < nc; ++k) {
        RaceChange c;
        frame += rd.var();
        c.frame = frame;
        c.kind = RaceChange::Kind(rd.get<u8>());
        c.car = rd.get<u8>();
        c.a = rd.get<u32>(), c.b = rd.get<u32>(), c.c = rd.get<u32>();
        r.changes.push_back(c);
    }
    const u32 nn = rd.var();
    if (nn > r.frames) throw FormatError("race record: bad extra step count");
    frame = 0;
    for (u32 k = 0; k < nn; ++k) {
        Nested n;
        frame += rd.var();
        n.frame = frame;
        const u8 np = rd.get<u8>();
        if (np > 8) throw FormatError("race record: bad extra step");
        for (u8 i = 0; i < np; ++i) {
            PadFrame f;
            f.flags = rd.get<u8>(), f.kind = rd.get<u8>(), f.shift = rd.get<s8>(), f.steer = rd.get<s16>();
            f.throttle = rd.get<u16>(), f.brake = rd.get<u16>(), f.back = rd.get<u16>(), f.third = rd.get<u16>();
            n.pads.push_back(f);
        }
        r.nested.push_back(std::move(n));
    }
    if (version >= 3) {
        if (rd.get<u8>() != 0) r.recorders = get_recorders(rd, r.behaviour);
        r.ghost_car = rd.get<s8>();
        r.external_ghost = rd.get<u8>() != 0;
        const u32 len = rd.var();
        if (len > kRecordPool) throw FormatError("race record: bad recording");
        const ByteSpan b = rd.bytes(len);
        r.run_recording.assign(b.begin(), b.end());
    } else {
        // Before version 3: the free run's recorders (version 2's, or fresh), answering the lap handlers; the ghost car's
        // frames are the record's.
        if (version >= 2 && rd.get<u8>() != 0) r.recorders = get_free_run_v2(rd, r.behaviour);
        else if (r.kind == Conductor::FreeRun) r.recorders = std::make_shared<const SoloRecorders>(true);
        if (r.recorders) {
            r.external_ghost = true;
            for (size_t i = 1; i < r.entries.size(); ++i)
                if (r.entries[i].human != 0 && r.entries[i].player) r.ghost_car = s32(i); // (the ghost car: a pad car after the players)
        }
    }
    if (version >= 4) {
        const u32 ne = rd.var();
        if (ne > 8) throw FormatError("race record: bad spark point lists");
        for (u32 i = 0; i < ne; ++i) {
            const u32 np = rd.var();
            if (np > 64) throw FormatError("race record: bad spark points");
            std::vector<SparkPoint> e;
            for (u32 k = 0; k < np; ++k) {
                SparkPoint p;
                p.x = F::raw(rd.get<u32>()), p.y = F::raw(rd.get<u32>()), p.z = F::raw(rd.get<u32>());
                e.push_back(p);
            }
            r.sparks.push_back(std::move(e));
        }
    }
    r.final_hash = rd.get<u64>();
    if (!rd.at_end()) throw FormatError("race record: bytes past the end");
    return r;
}

u64 race_hash(const ExactRace& race) {
    u64 h = 1469598103934665603ull; // FNV-1a
    auto mix = [&](const void* p, size_t n) {
        const u8* b = static_cast<const u8*>(p);
        for (size_t k = 0; k < n; ++k) h = (h ^ b[k]) * 1099511628211ull;
    };
    auto word = [&](u32 v) { mix(&v, 4); };
    std::vector<u8> image(kCarSize);
    for (const Car& c : race.cars()) {
        std::fill(image.begin(), image.end(), u8(0));
        store_image(c, image.data());
        mix(image.data(), image.size());
    }
    for (s32 k : race.order()) word(u32(k));
    const RaceFlow& f = race.flow();
    word(f.clock), word(f.end_count), word(u32(f.result)), word(f.result_time), word(f.record);
    word(race.random().r);
    const RaceInfoStart& i = race.info();
    for (s32 v : {i.start, i.start_type, i.seconds, i.start_count, i.timer_on, i.timer}) word(u32(v));
    if (race.has_sparks()) race.sparks().mix(word);
    return h;
}

bool RacePlayback::step(ExactRace& race) {
    if (done()) return false;
    const std::span<const PadFrame> p = next(race);
    race.step(p);
    return true;
}

void RacePlayback::seek(u32 t) {
    t_ = t, change_ = 0, nested_ = 0;
    if (!r_) return;
    while (change_ < r_->changes.size() && r_->changes[change_].frame < t) ++change_;
    while (nested_ < r_->nested.size() && r_->nested[nested_].frame < t) ++nested_;
}

std::span<const PadFrame> RacePlayback::next(ExactRace& race) {
    pads_.assign(size_t(race.size()), PadFrame{});
    if (!r_) return pads_;
    while (change_ < r_->changes.size() && r_->changes[change_].frame <= t_) apply_change(race, r_->changes[change_++]);
    for (int i = 0; i < race.size(); ++i) pads_[size_t(i)] = r_->pad(i, t_);
    // This frame's extra steps read their recorded pads (none recorded: the frame's own, as the race does by default).
    while (nested_ < r_->nested.size() && r_->nested[nested_].frame < t_) ++nested_;
    race.set_nested_pads([this, t = t_](std::span<PadFrame> f) {
        if (nested_ >= r_->nested.size() || r_->nested[nested_].frame != t) return;
        const std::vector<PadFrame>& p = r_->nested[nested_++].pads;
        for (size_t i = 0; i < f.size() && i < p.size(); ++i) f[i] = p[i];
    });
    ++t_;
    return pads_;
}

} // namespace gt3r::sim::exact
