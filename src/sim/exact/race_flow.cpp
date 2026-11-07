// The race flow after the vehicle phases; see race_flow.h.

#include "sim/exact/race_flow.h"

#include "sim/exact/pit_lane.h"

#include <algorithm>
#include <stdexcept>

namespace gt3r::sim::exact {

namespace {

const F kZero{}, kHalf = F::raw(0x3F000000), kOne = F::raw(0x3F800000), kTen = F::raw(0x41200000), kFifty = F::raw(0x42480000);
const F kFast = F::raw(0x3FB1C71D); // 5 km/h
const F kSlow = F::raw(0x3F555556); // 3 km/h
const F kStop = F::raw(0x3E8E38E4); // 1 km/h: the licence's stop speed
// The unit factors: m/s to km/h, x m/s to mph.
const F kKmh = F::raw(0x40666666), kMph1 = F::raw(0x40666666), kMph2 = F::raw(0x3F1F136A);

// The race clock's units in a step (the clock counts 3000 a second): 50, at PAL's rate 60; as a float and as the integer
// the crossings' remainders and the snapshots' clock phase use.
F clock_units(const RaceFlowInputs& in) { return in.behaviour.pal_rate() ? F::raw(0x42700000) : kFifty; }
s32 clock_step(const RaceFlowInputs& in) { return in.behaviour.pal_rate() ? 60 : 50; }
// The rules' own branches (RaceFlowInputs::behaviour): PAL's, NTSC-J's.
bool pal_rules(const RaceFlowInputs& in, Rule r) { return in.behaviour.rule(r) == Program::Pal; }
bool ntsc_j_rules(const RaceFlowInputs& in, Rule r) { return in.behaviour.rule(r) == Program::NtscJ; }

RaceEvent race_event(s32 type, s32 word, s32 prio) {
    RaceEvent e;
    e.type = type, e.car = word, e.arg = prio;
    return e;
}
RecorderEvent recorder_event(RecorderEvent::Kind kind, int car, u32 time = 0) {
    RecorderEvent e;
    e.kind = kind, e.car = car, e.time = time;
    return e;
}

// The conductor's player-record test: the mode records the player (time trial, free run, licence, machine test): a pad
// car (control source 0) of driver kind 0 is recorded.
bool records_player(Conductor k) {
    return k == Conductor::TimeTrial || k == Conductor::FreeRun || k == Conductor::License || k == Conductor::MachineTest;
}
bool recorded(const Car& c, const RaceFlowInputs& in) { return records_player(in.kind) && c.driver_kind == 0 && c.control == 0; }
// Virtual 6: 1 in the machine test, the licence's when the configuration's +0 is 0 (a standing start).
bool logs_from_start(const RaceFlowInputs& in) {
    return in.kind == Conductor::MachineTest || (in.kind == Conductor::License && in.config[0] == 0);
}
// Virtual 7: the licence's type 3 test of more than one lap.
bool multi_lap_licence(const RaceFlowInputs& in) { return in.kind == Conductor::License && in.config[2] == 3 && in.config[1] != 1; }
// The logger mark: the data logger entry of car's mark (end: the one ending there): the mark count F +0x493; the licence's laps test
// counts on over its laps (the logger's marks + 1 a lap), the ending one 1 lower.
s32 logger_entry(const Car& c, bool end, const RaceFlowInputs& in) {
    if (!multi_lap_licence(in)) return c.x493;
    const s32 k = (s32(c.laps) - 1) * (in.course->logger_marks() + 1) + s32(c.x493);
    return end ? k - 1 : k;
}

// The machine test's type (the configuration's laps byte below 4, else 0) and sector count (its sector count).
int machine_type(u8 laps_byte) { return laps_byte < 4 ? laps_byte : 0; }
int machine_sectors_count(u8 laps_byte) {
    const int t = machine_type(laps_byte);
    return t == 1 ? 1 : t == 2 ? 2 : t == 0 || t == 3 ? 3 : 1;
}
// Virtual 31: the split line.
F split_line(const RaceFlowInputs& in) {
    if (in.kind != Conductor::MachineTest) return in.split;
    return in.machine_sectors[size_t(machine_sectors_count(in.config[1]) - 1)];
}
bool sector_order_mode(Conductor k) { return k == Conductor::SinglePlayer || k == Conductor::Battle2P || k == Conductor::BattleMP; }

// Car i's run is over (end_of_run_head, pit_lane.h), then the AI calls after it (the hook).
void finish_car(Car& c, int i, int mode, const RaceFlowHooks& hooks) {
    if (end_of_run_head(c, mode) && hooks.finish_drive) hooks.finish_drive(c, i, mode);
}

// With 3 (the race-flow byte 2): the race ends 3 s on (3 x 60 + 1 steps; PAL 3 x 50 + 1).
void end_countdown(const RaceFlowInputs& in, RaceFlow& flow) {
    if (in.flow == 2) flow.end_count = u16(3 * steps_per_second(in.behaviour) + 1); // steps a second
}

// A time `t` of the car with index byte `index` (F +0x3E8) not above the session's best (race info + 0x8BC) marks a
// new record (race info + 0x4C) and, with a result (+0x40), posts 19. Only for the first car, outside replays, with the
// race-flow byte clear and the data logger off.
bool new_record(u8 index, u32 t, const RaceFlowInputs& in, RaceFlow& flow, std::vector<RaceEvent>& events) {
    if (index != 0 || in.replay || in.flow != 0 || in.logger || t == kNoTime) return false;
    // NTSC: a time equal to the session's best is a new record; the PAL program asks for a lower one.
    if (pal_rules(in, Rule::LapRecord) ? !(t < flow.session.lap) : flow.session.lap < t) return false;
    flow.record = 1;
    if (flow.result != 0) events.push_back(race_event(0x13, 0, 0));
    return true;
}

// ---- the race display's events and the records ----------------------------------------------------------------------------

RaceEvent display(s32 type, s32 car) { return race_event(type, car, 2); }
EntryRecords& entry(RaceFlow& flow, int i) {
    if (flow.entries.size() <= size_t(i)) flow.entries.resize(size_t(i) + 1);
    return flow.entries[size_t(i)];
}
// A time event's time, at most 0x0FFFFFFF.
u32 clamp_time(u32 t) { return 0x0FFFFFFEu < t ? 0x0FFFFFFFu : t; }
// The store, entry + 0x11DC + 4 sector, for any sector: the current lap's four sector times, and past them on into
// the entry (a licence test counts its sectors on over its laps): its current lap's time (+0x11EC), its best lap (+0x11F0),
// the machine test's top speed (+0x2100), any other word in EntryRecords::words.
void put_split(EntryRecords& e, s32 sector, u32 t) {
    const u32 off = 0x11DCu + 4u * u32(sector);
    if (u32(sector) < 4) e.current.sector[size_t(sector)] = t;
    else if (off == 0x11EC) e.current.lap = t;
    else if (off == 0x11F0) e.best_lap = t;
    else if (off == 0x2100) e.top_speed = F::raw(t);
    else e.words[off] = t;
}
// A lap into the list.
void add_lap(LapList& l, u32 lap) {
    ++l.count;
    if (l.counted_only != 0) return;
    l.total += lap;
    if (l.laps.size() == 1000) ++l.dropped, l.laps.erase(l.laps.begin());
    l.laps.push_back(lap);
}
// The lower time (no time counts as none).
void best_time(u32& best, u32 lap) {
    if (best != kNoTime && !(lap < best)) return;
    best = kNoTime < lap ? kNoTime : lap;
}
// src's splits into dst when its lap is the better (dst none counts as worse).
void better_splits(Splits& dst, const Splits& src) {
    if (dst.lap != kNoTime && !(src.lap < dst.lap)) return;
    dst.sector = src.sector;
    dst.lap = src.lap;
}
// The time trial's and free run's difference of a sector time to the session's best (event 15); not with the
// race-flow byte set (NTSC-J: also then).
void sector_diff(std::span<const Car> cars, int i, s32 sector, u32 split, const RaceFlowInputs& in, const RaceFlow& flow,
                 std::vector<RaceEvent>& events) {
    if ((in.flow != 0 && !ntsc_j_rules(in, Rule::SectorDiff)) || cars[size_t(i)].course_out != 0) return;
    if (u32(sector) >= 4) return; // (cannot happen: the time trial's and free run's sector is the course's, below 4)
    const u32 best = flow.session.sector[size_t(sector)];
    if (best == kNoTime) return;
    RaceEvent e = display(15, i);
    e.value = s32(split - best);
    events.push_back(e);
}
// The same for a lap time and the session's best lap.
void lap_diff(std::span<const Car> cars, int i, u32 lap, const RaceFlowInputs& in, const RaceFlow& flow, std::vector<RaceEvent>& events) {
    if ((in.flow != 0 && !ntsc_j_rules(in, Rule::LapDiff)) || cars[size_t(i)].course_out != 0 || flow.session.lap == kNoTime) return;
    RaceEvent e = display(15, i);
    e.value = s32(lap - flow.session.lap);
    events.push_back(e);
}
// The machine test's top speed: unless locked into the entry; event 17 in the display's unit. PAL:
// nothing for a ghost.
void speed_event(std::span<const Car> cars, int i, F speed, const RaceFlowInputs& in, RaceFlow& flow, std::vector<RaceEvent>& events) {
    if (pal_rules(in, Rule::SpeedEvent) && cars[size_t(i)].ghost != 0) return;
    if (!records_locked(cars[size_t(i)], in)) entry(flow, i).top_speed = speed;
    RaceEvent e = display(17, i);
    e.speed = in.speed_unit == 0 ? speed * kKmh : in.speed_unit == 1 ? speed * kMph1 * kMph2 : speed;
    events.push_back(e);
}

// ---- the data logger and the free run's recordings ------------------------------------------------------------------------

std::map<s32, Snapshot>& logger_table(RaceFlow& flow, int i) {
    if (flow.logger.size() <= size_t(i)) flow.logger.resize(size_t(i) + 1);
    return flow.logger[size_t(i)];
}
// The car: the car of index F +0x3E8 (itself in a race).
const Car& snapshot_car(std::span<const Car> cars, int i) {
    const u8 k = cars[size_t(i)].index;
    return k < cars.size() ? cars[k] : cars[size_t(i)];
}
// The end of car i's logger entry k (a mark, the split, a lap, the licence's stop): posts 13 (the car's index);
// recording, the entry's time and frames.
void logger_end(std::span<Car> cars, int i, s32 k, u32 time, const RaceFlowInputs& in, RaceFlow& flow, std::vector<RaceEvent>& events) {
    events.push_back(race_event(13, cars[size_t(i)].index, 0));
    if (!in.logger_record) return;
    Snapshot& e = logger_table(flow, i)[k];
    e.time = time;
    e.frames = in.logger_frame[size_t(i) & 7] - e.frame;
}
// The start of car i's logger entry k: the car, the crossing's remainder and fraction, the clock's phase
// and the car's input frame (less the table's first entry's when its input object counts so).
void logger_start(std::span<Car> cars, int i, s32 k, u32 sub, F frac, const RaceFlowInputs& in, RaceFlow& flow,
                  const RaceFlowHooks& hooks) {
    std::map<s32, Snapshot>& table = logger_table(flow, i);
    u32 frame = in.logger_frame[size_t(i) & 7];
    if (in.logger_relative[size_t(i) & 7] && k != 0)
        if (const auto first = table.find(0); first != table.end()) frame -= first->second.frame;
    Snapshot& e = table[k];
    e.car = snapshot_car(cars, i);
    if (hooks.save_body) hooks.save_body(cars[size_t(i)].index, e);
    e.sub = u8(sub);
    e.frame = frame;
    e.frac = frac;
    e.phase = u8((flow.clock + u32(clock_step(in))) % 3);
}
// Car i's lap start into the free run's lap recording (its header) and the recording restarted.
void lap_start(std::span<Car> cars, int i, s32 sub, F frac, const RaceFlowInputs& in, RaceFlow& flow, const RaceFlowHooks& hooks) {
    RecorderEvent e;
    e.kind = RecorderEvent::lap_start, e.car = i;
    e.snapshot.car = snapshot_car(cars, i);
    if (hooks.save_body) hooks.save_body(cars[size_t(i)].index, e.snapshot);
    e.snapshot.sub = u8(sub);
    e.snapshot.frac = frac;
    e.snapshot.phase = u8((flow.clock + u32(clock_step(in))) % 3);
    flow.recorder.push_back(std::move(e));
}
// The ghost's playback's lap start (of its +0xC4), none while it is empty (+0x140).
const Snapshot* ghost_start(const RaceFlowInputs& in) { return in.recorder.ghost ? in.recorder.ghost_start : nullptr; }
// Car c restored from a snapshot, but its ghost flag, index and control source; its last crossing's time from the
// snapshot's remainder and the race clock. (Its collision body, reset and restored from the snapshot's copy, is the caller's.)
void restore(Car& c, const Snapshot& s, u32 clock) {
    const u8 ghost = c.ghost, index = c.index;
    const s8 control = c.control;
    c = s.car;
    c.ghost = ghost, c.index = index, c.control = control;
    c.x530 = (clock + s.sub) / 3;
}
// The free run's ghost car (car 1) put back on its recording's lap start with `laps` laps and its playback
// restarted; its step accumulator takes the crossing's fraction less the recording's, a step more for it when that is below 0.
void ghost_restart(std::span<Car> cars, s32 laps, F frac, const Snapshot* ghost, const RaceFlowInputs& in, RaceFlow& flow,
                   const RaceFlowHooks& hooks) {
    if (!ghost) return; // the playback is empty (+0x140)
    Car& g = cars[1];
    const Snapshot& s = *ghost;
    restore(g.index < cars.size() ? cars[g.index] : g, s, flow.clock);
    if (hooks.restore_body) hooks.restore_body(g.index, s);
    g.laps = s16(laps);
    RecorderEvent e;
    e.kind = RecorderEvent::ghost_restart, e.car = 1, e.time = u32(laps);
    F f = frac - s.frac;
    const bool behind = f < kZero;
    e.extra = behind && !in.logger && g.index == 1;
    flow.recorder.push_back(std::move(e));
    if (behind) {
        if (flow.recorder.back().extra && hooks.ghost_step) hooks.ghost_step();
        f = f + kOne;
    }
    g.flow = f;
}

// The licence test's end for car i (lap time t): finished (mode 1 in a type 1 test, else 3), then outside replays
// for a pad car the result: unjudged tests post 5; judged, the first judging time (0, gold, silver, bronze, 0) not below t
// gives the result 1 + its index (passed; with the race-flow byte set posts 5 and keeps no result) or 0 (failed: posts 9);
// the time's display event for the car of index F +0x3E8 (car i in a race).
void licence_end(std::span<Car> cars, int i, u32 t, const RaceFlowInputs& in, RaceFlow& flow, std::vector<RaceEvent>& events,
                 const RaceFlowHooks& hooks) {
    Car& c = cars[size_t(i)];
    finish_car(c, i, in.config[2] == 1 ? 1 : 3, hooks);
    auto shown = [&] {
        if (c.index >= cars.size()) return; // (cannot happen: a car's index is its place in the race)
        lap_time(cars, c.index, 1, t, c.course_out, in, flow, events);
    };
    if (c.ghost != 0) return shown();
    if (in.replay) return;
    if (in.unjudged) {
        events.push_back(race_event(5, c.index, 2));
        return shown();
    }
    s32 result = 0;
    bool passed = false;
    for (u32 k = 0; k < 5; ++k) {
        const u32 limit = k >= 1 && k <= 3 ? in.licence_times[k - 1] : 0;
        if (limit < t) continue;
        result = s32(k + 1), passed = true;
        break;
    }
    if (passed) {
        if (in.flow != 0) {
            events.push_back(race_event(5, c.index, 2));
            return shown();
        }
    } else {
        events.push_back(race_event(9, c.index, 0));
        if (in.flow != 0) return shown();
    }
    flow.result = result, flow.result_time = t;
    new_record(c.index, t, in, flow, events);
    shown();
}

// The licence test's car i went out of the course: finished (mode 2), then for a pad car outside replays of a judged
// test posts 9 and, with the race-flow byte clear, the result -1.
void licence_out(Car& c, int i, const RaceFlowInputs& in, RaceFlow& flow, std::vector<RaceEvent>& events, const RaceFlowHooks& hooks) {
    if (in.replay) return;
    finish_car(c, i, 2, hooks);
    if (c.ghost != 0 || in.unjudged) return;
    events.push_back(race_event(9, c.index, 0));
    if (in.flow == 0) flow.result = -1, flow.result_time = kNoTime;
}

// All four wheels on stop-zone ground (G +0x33 bit 2) below the stop speed: the speed's fraction of it in `ratio`.
bool stopped(const Car& c, F& ratio) {
    ratio = kZero;
    int n = 0;
    for (const Ground& g : c.g) n += (g.flags & 4) != 0;
    if (n < 4) return false;
    F s = c.vel.x * c.vel.x;
    s = s + c.vel.y * c.vel.y;
    s = s + c.vel.z * c.vel.z;
    s = sqrt(s);
    const F stop = pal_rules(c, Rule::LicenceStop) ? F::raw(0x3ED55556) : kStop; // PAL: 1.5 km/h
    if (stop < s) return false;
    ratio = s / stop;
    return true;
}

} // namespace

void new_run(EntryRecords& e) { e.laps = LapList{}; }

void restore_car(Car& c, const Snapshot& s, u32 clock) { restore(c, s, clock); }

SoloRecorders::SoloRecorders(bool fr) : pool0(kRecordPool), pool1(kRecordPool), free_run(fr) {
    // (each recorder's constructor rewinds it without a block: closed until a restart)
    for (ControlManager* m : {&player, &ghost, &lap, &best}) m->rec.rewind();
    player.mode = 0, player.bind_pool(0);
    lap.mode = 2, lap.bind_pool(1), lap.bind_store(1);
    best.mode = 2, best.bind_pool(1), best.bind_store(2);
    snapshot[2] = Snapshot{}, snapshot[2].time = kNoTime; // (cleared, "no time": in the memory too)
    for (u32 k = 0; k < 4; ++k) pool1[best.store + 0x10 + k] = u8(kNoTime >> (8 * k));
    ghost.mode = 0, ghost.bind_pool(1);
    if (free_run) ghost.mode = 2; // Race free-run's devices
    start();
}

void SoloRecorders::start() {
    lap.restart(), best.restart();
    if (free_run && ghost_empty) ghost.bind_store(0);
    player.restart(), ghost.restart();
}

PadFrame SoloRecorders::player_frame(const PadFrame& live) {
    if (player_plays) return pad_of(player.rec.read(pool0)); // (mode 1)
    player.rec.write(pool0, raw_pad(live));
    return live;
}

PadFrame SoloRecorders::ghost_frame() {
    if (ghost_empty) return PadFrame{};
    return pad_of(ghost.rec.read(pool1));
}

void SoloRecorders::lap_frame(const PadFrame& f) { lap.rec.write(pool1, raw_pad(f)); }

FreeRunRecorder SoloRecorders::inputs(const Behaviour& how) const {
    FreeRunRecorder r;
    // The lap recording not closed (it closes itself when its store is full) and under 36,000 frames; NTSC-J's
    // lap handler reads the player's device's own recording's flag instead.
    const bool open = how.rule(Rule::LapUsable) == Program::NtscJ ? !player.rec.done : !lap.rec.done;
    r.lap_ok = open && s32(lap.rec.count) < 36000;
    r.best = snapshot[best.store / kRecordStore].time;
    r.ghost = !ghost_empty;
    if (ghost.has_store()) r.ghost_time = snapshot[ghost.store / kRecordStore].time, r.ghost_start = &snapshot[ghost.store / kRecordStore];
    r.lap_start = &snapshot[lap.store / kRecordStore];
    r.ghost_option = ghost_option;
    return r;
}

namespace {
// / the copy: store `from` of pool 1 (its snapshot and recording) over store `to`.
void copy_store(SoloRecorders& r, u32 from, u32 to) {
    std::copy_n(r.pool1.begin() + from, kRecordStore, r.pool1.begin() + to);
    r.snapshot[to / kRecordStore] = r.snapshot[from / kRecordStore];
}
} // namespace

void SoloRecorders::best_to_ghost(bool now) {
    if (now && !ghost_option) return; // (the quick menu's "Update Ghost?" makes it later: not at once)
    copy_store(*this, 2 * kRecordStore, 0);
    ghost_empty = false;
    if (now) ghost.restart();
}

bool SoloRecorders::ghost_from_best(const SoloRecorders& from) {
    if (from.best_time() == kNoTime || from.best.rec.header_frames(from.pool1) == 0) return false;
    std::copy_n(from.pool1.begin() + from.best.store, kRecordStore, pool1.begin()); // (over store 0, as best_to_ghost)
    snapshot[0] = from.snapshot[from.best.store / kRecordStore];
    ghost_empty = false;
    return true;
}

void SoloRecorders::run_to_ghost(std::span<const u8> recording) {
    // (the program copies the whole of pool 0; past the recording its bytes are never read: cleared here)
    std::fill(pool1.begin(), pool1.end(), u8(0));
    std::copy_n(recording.begin(), std::min(recording.size(), pool1.size()), pool1.begin());
    ghost_empty = false;
}

void SoloRecorders::apply(const RecorderEvent& e) {
    switch (e.kind) {
    case RecorderEvent::close: lap.rec.close(pool1); break;
    case RecorderEvent::lap: snapshot[lap.store / kRecordStore].time = e.time; break;
    case RecorderEvent::best: copy_store(*this, lap.store, best.store); break;
    case RecorderEvent::ghost: best_to_ghost(true); break;       // (1, 1)
    case RecorderEvent::ghost_restart: ghost.restart(); break;   // the ghost car's restart: its playback
    case RecorderEvent::lap_start: {                              // the snapshot
        Snapshot& h = snapshot[lap.store / kRecordStore];
        const u32 time = h.time, frame = h.frame, frames = h.frames; // (the header's other words stay)
        h = e.snapshot, h.time = time, h.frame = frame, h.frames = frames;
        lap.restart();
        break;
    }
    }
}

void SoloRecorders::end() {
    player.rec.close(pool0), ghost.rec.close(pool1);
    lap.rec.close(pool1), best.rec.close(pool1);
}

void SoloRecorders::analysis(std::span<const u8> a, std::span<const u8> b) {
    std::fill(pool0.begin(), pool0.end(), u8(0));
    std::copy_n(a.begin(), std::min(a.size(), pool0.size()), pool0.begin());
    player_plays = true;
    ghost.mode = 0, ghost.bind_pool(1);
    std::fill(pool1.begin(), pool1.end(), u8(0));
    std::copy_n(b.begin(), std::min(b.size(), pool1.size()), pool1.begin());
    ghost_empty = b.empty();
    start();
}

std::vector<u8> SoloRecorders::run_recording() const {
    if (!player.rec.done || !player.rec.written) return {};
    u32 bytes = 0;
    for (u32 k = 0; k < 4; ++k) bytes |= u32(pool0[4 + k]) << (8 * k);
    bytes = std::min<u32>(bytes + 1, kRecordPool); // (and the stream's last byte, filled in part)
    return std::vector<u8>(pool0.begin(), pool0.begin() + bytes);
}

u32 display_word(const RaceEvent& e) {
    if (!display_event(e.type)) return u32(e.car);
    auto clamp = [](s32 v, s32 hi) { return v < 0 ? 0u : u32(hi < v ? hi : v); };
    const u32 car = clamp(e.car, 15);
    switch (e.type) {
    case 11: return car | clamp(e.lap, 999) << 8;
    case 12: return car | clamp(e.sector, 15) << 4 | clamp(e.lap, 999) << 8;
    case 17: return car | clamp(ee::to_int(e.speed * kTen), 9999) << 4;
    case 18: return car | (u32(e.value) & 15) << 4;
    default: { // 14-16 (and the unused 13-type slot): the value, signed, within +-0x7FFFFFF
        const s32 v = e.value < -0x7FFFFFF ? -0x7FFFFFF : 0x7FFFFFF < e.value ? 0x7FFFFFF : e.value;
        return car | u32(v) << 4;
    }
    }
}

u32 display_bits(s32 type) {
    switch (type) {
    case 11: return 0x3FF0F;
    case 12: return 0x3FFFF;
    case 17: return 0x3FFFF;
    case 18: return 0xFF;
    default: return 0xFFFFFFFFu;
    }
}

bool records_locked(const Car& c, const RaceFlowInputs& in) {
    return (c.ghost != 0 && !pal_rules(in, Rule::RecordsLocked)) || in.flow != 0 || in.locked || in.replay; // PAL's callers leave a ghost out before
}

void lap_time(std::span<Car> cars, int n, s32 laps, u32 lap, u8 failed, const RaceFlowInputs& in, RaceFlow& flow,
              std::vector<RaceEvent>& events) {
    Car& c = cars[size_t(n)];
    keep_best_lap(c, lap);
    if (pal_rules(in, Rule::LapTime) && c.ghost != 0) return; // PAL: nothing more for a ghost
    const bool locked = records_locked(c, in);
    RaceEvent a = display(11, n);
    a.lap = laps;
    events.push_back(a);
    if (!ntsc_j_rules(in, Rule::LapTime)) { // (NTSC-J has no general time event)
        RaceEvent b = display(14, n);
        b.value = s32(clamp_time(lap));
        events.push_back(b);
    }
    if (locked && !ntsc_j_rules(in, Rule::LapTime)) return;
    EntryRecords& e = entry(flow, n);
    if (!locked) add_lap(e.laps, lap);
    if (failed != 0) return;
    if (!locked) best_time(e.best_lap, lap);
    // The entry's best lap's splits, then (not locked) the session's. NTSC-J: also for a locked
    // car's entry.
    e.current.lap = lap;
    better_splits(e.best, e.current);
    if (!locked) better_splits(flow.session, e.current);
}

void sector_time(std::span<Car> cars, int i, s32 lap, s32 sector, u32 split, u8 failed, const RaceFlowInputs& in, RaceFlow& flow,
                 std::vector<RaceEvent>& events) {
    if (pal_rules(in, Rule::SectorTime) && cars[size_t(i)].ghost != 0) return; // PAL: nothing for a ghost
    const bool locked = records_locked(cars[size_t(i)], in) && !ntsc_j_rules(in, Rule::SectorTime); // (NTSC-J: never locked)
    RaceEvent a = display(12, i);
    a.lap = lap, a.sector = sector;
    events.push_back(a);
    if (!ntsc_j_rules(in, Rule::SectorTime)) {
        RaceEvent b = display(14, i);
        b.value = s32(clamp_time(split));
        events.push_back(b);
    }
    if (failed != 0 || locked) return;
    put_split(entry(flow, i), sector, split);
}

void race_mode_sector_order(std::span<Car> cars, int i, s32 lap, s32 sector, u32 t, const RaceFlowInputs& in, RaceFlow& flow,
                            std::vector<RaceEvent>& events) {
    (void)cars;
    if (!sector_order_mode(in.kind)) return; // the others' sector handler is empty
    if (u32(sector) >= 4) return; // (cannot happen: a race's sector is the course's, below 4)
    SectorOrder& o = flow.order;
    const size_t s = size_t(sector);
    if (s32(o.lap[s]) < lap) { // the first car on a new lead lap
        o.lap[s] = s16(lap);
        o.count[s] = 1, o.shown[s] = 0;
        o.time[s][0] = t;
        o.car[s][0] = s8(i);
        return;
    }
    if (lap != s32(o.lap[s])) { // laps behind
        RaceEvent e = display(16, i);
        e.value = s32(o.lap[s]) - lap;
        events.push_back(e);
        return;
    }
    const s32 n = o.count[s], shown = o.shown[s];
    if (n < 0 || n >= 8) return; // (cannot happen: each car reaches a sector once a lap, and a race has at most 8)
    s32 p = n; // the place by its time, not before the cars already posted
    if (n != shown && shown < n && t < o.time[s][size_t(n - 1)]) {
        p = n - 1;
        while (shown < p && t < o.time[s][size_t(p - 1)]) --p;
    }
    for (s32 k = n; p < k; --k) o.time[s][size_t(k)] = o.time[s][size_t(k - 1)];
    o.time[s][size_t(p)] = t;
    o.car[s][size_t(p)] = s8(i);
    o.count[s] = s8(n + 1);
}

void race_mode_sector(std::span<Car> cars, int i, s32 laps, s32 sector, u32 split, const RaceFlowInputs& in, RaceFlow& flow,
                      std::vector<RaceEvent>& events) {
    const Car& c = cars[size_t(i)];
    switch (in.kind) {
    case Conductor::TimeTrial:
    case Conductor::FreeRun:
        sector_diff(cars, i, sector, split, in, flow, events);
        sector_time(cars, i, laps - 1, sector, split, c.course_out, in, flow, events);
        return;
    case Conductor::License: // the sectors counted on over the laps, as lap 1's
        sector_time(cars, i, 1, (laps - 1) * sector_count(in.kind, in.config[1], *in.course) + sector, split, c.course_out, in, flow,
                    events);
        return;
    case Conductor::MachineTest:
        if (!in.replay) sector_time(cars, i, laps, sector, split, c.course_out, in, flow, events);
        return;
    case Conductor::FreePractice: // the difference to the session's best without the race-flow byte's test
        if (c.course_out == 0) {
            if (u32(sector) >= 4) return; // (cannot happen: free practice's sector is the course's, below 4)
            if (const u32 best = flow.session.sector[size_t(sector)]; best != kNoTime) {
                RaceEvent e = display(15, i);
                e.value = s32(split - best);
                events.push_back(e);
            }
        }
        sector_time(cars, i, laps, sector, split, c.course_out, in, flow, events);
        return;
    default: // the base conductor's
        sector_time(cars, i, laps - 1, sector, split, c.course_out, in, flow, events);
        return;
    }
}

void race_mode_display(std::span<const Car> cars, const RaceFlowInputs& in, RaceFlow& flow, std::vector<RaceEvent>& events) {
    (void)cars;
    if (!sector_order_mode(in.kind)) return; // the others' time-difference hook is empty
    SectorOrder& o = flow.order;
    const int sectors = sector_count(in.kind, in.config[1], *in.course);
    for (int s = 0; s < sectors && s < 4; ++s) { // (sector_count is at most 4)
        const size_t k = size_t(s);
        if (o.shown[k] == o.count[k]) continue;
        for (s32 p = o.shown[k]; p < o.count[k]; ++p) {
            if (p == 0) continue; // the lead car: (empty)
            const u32 tp = o.time[k][size_t(p)];
            RaceEvent e = display(15, o.car[k][size_t(p)]); // behind the lead car
            e.value = s32(tp - o.time[k][0]);
            events.push_back(e);
            if (p == 1) { // the lead car ahead of the second
                RaceEvent f = display(15, o.car[k][0]);
                f.value = s32(o.time[k][0] - tp);
                events.push_back(f);
            }
        }
        o.shown[k] = o.count[k];
    }
}

// The conductors' first-crossing handler, the race's first crossing of car i.
void race_mode_first_crossing(std::span<Car> cars, int i, u32 t, s32 sub, F frac, const RaceFlowInputs& in, RaceFlow& flow,
                              std::vector<RaceEvent>& events, const RaceFlowHooks& hooks) {
    Car& c = cars[size_t(i)];
    switch (in.kind) {
    case Conductor::TimeTrial:
        if (c.control == 0) {
            if (hooks.rolling_handback) hooks.rolling_handback(c);
            events.push_back(race_event(4, i, 0));
        }
        c.x530 = t;
        break;
    case Conductor::License:
        if (in.config[0] == 0) break;
        c.x530 = t;
        if (hooks.rolling_handback) hooks.rolling_handback(c);
        if (i == 0) events.push_back(race_event(4, 0, 0));
        break;
    case Conductor::FreePractice: // car 0's note 3
        if (i == 0) {
            RaceEvent e = display(18, 0);
            e.value = 3;
            events.push_back(e);
        }
        c.x530 = t;
        break;
    case Conductor::FreeRun:
        if (i == 0) {
            if (c.driver_kind != 0) { // the AI drove the run-up
                if (hooks.rolling_handback) hooks.rolling_handback(c);
                c.ai_hold = 0;
                events.push_back(race_event(4, 0, 0));
            }
            if (!in.logger && cars.size() == 2) ghost_restart(cars, 0, frac, ghost_start(in), in, flow, hooks);
            if (in.flow == 0) lap_start(cars, 0, sub, frac, in, flow, hooks);
        }
        if ((c.ghost != 0 || in.logger_record) && hooks.drive_on) hooks.drive_on(c, i);
        c.x530 = t;
        break;
    default: break; // the base conductor's code, the single races' code: nothing
    }
}

// The conductors' lap handler, a completed lap of car i.
void race_mode_lap(std::span<Car> cars, std::span<const s32> order, int i, s32 laps, u32 t, u32 lap, s32 sub, F frac,
                   const RaceFlowInputs& in, RaceFlow& flow, std::vector<RaceEvent>& events, const RaceFlowHooks& hooks) {
    Car& c = cars[size_t(i)];
    switch (in.kind) {
    case Conductor::TimeTrial: // every lap ends the run
        if (in.replay) return;
        finish_car(c, i, 3, hooks);
        if (c.ghost == 0) {
            if (i == 0) end_countdown(in, flow);
            events.push_back(race_event(5, i, 2));
            if (in.flow == 0) new_record(c.index, lap, in, flow, events);
        }
        lap_diff(cars, i, lap, in, flow, events);
        lap_time(cars, i, laps, lap, c.course_out, in, flow, events);
        c.x530 = t;
        return;
    case Conductor::FreePractice: { // the difference to the session's best lap and a new best's note
        const u32 best = flow.session.lap;
        if (c.course_out == 0) {
            if (best != kNoTime) {
                RaceEvent e = display(15, i);
                e.value = s32(lap - best);
                events.push_back(e);
            }
            if (lap < best) {
                RaceEvent e = display(18, i);
                e.value = 2;
                events.push_back(e);
            }
        }
        lap_time(cars, i, laps, lap, c.course_out, in, flow, events);
        clear_course_out(c);
        c.x530 = t;
        return;
    }
    case Conductor::FreeRun: // the lap's recording, a best lap, the ghost
        lap_diff(cars, i, lap, in, flow, events);
        lap_time(cars, i, laps, lap, c.course_out, in, flow, events);
        if (i == 0) {
            const Snapshot* ghost = ghost_start(in);
            if (in.flow == 0) {
                flow.recorder.push_back(recorder_event(RecorderEvent::close, 0));
                if (in.recorder.lap_ok) {
                    flow.recorder.push_back(recorder_event(RecorderEvent::lap, 0, lap));
                    if (lap < in.recorder.best) {
                        flow.recorder.push_back(recorder_event(RecorderEvent::best, 0, lap));
                        flow.record = 1;
                        if (!in.recorder.ghost || lap < in.recorder.ghost_time) {
                            flow.ghost = 1;
                            flow.recorder.push_back(recorder_event(RecorderEvent::ghost, 0, lap));
                            if (in.recorder.ghost_option) ghost = in.recorder.lap_start; // the lap just kept, at once
                        }
                    }
                }
            }
            if (!in.logger && cars.size() == 2) ghost_restart(cars, laps, frac, ghost, in, flow, hooks);
            if (in.flow == 0) lap_start(cars, 0, sub, frac, in, flow, hooks);
        }
        if ((c.ghost != 0 || in.logger_record) && hooks.drive_on) hooks.drive_on(c, i);
        clear_course_out(c);
        c.x530 = t;
        return;
    case Conductor::MachineTest: return; // nothing
    case Conductor::License:             // a type 3 test ends with its laps; else the lap as a sector time
        if (in.config[2] == 3 && in.laps == laps) return licence_end(cars, i, lap, in, flow, events, hooks);
        sector_time(cars, i, laps, laps * sector_count(in.kind, in.config[1], *in.course) - 1, lap, c.course_out, in, flow, events);
        return;
    case Conductor::Battle2P: { // the leader finished or the laps done; the AI takes the other pad cars over
        lap_time(cars, i, laps, lap, c.course_out, in, flow, events);
        bool end = cars[size_t(order[0])].finished != 0;
        if (in.laps != 0 && laps == in.laps) end = true;
        if (end) {
            events.push_back(race_event(5, i, 2));
            finish_car(c, i, 3, hooks);
            for (int j = 0; j < int(cars.size()); ++j) {
                if (j == i || cars[size_t(j)].control != 0) continue;
                cars[size_t(j)].control = 2;
                if (hooks.ai_takeover) hooks.ai_takeover(cars[size_t(j)], j);
            }
        }
        clear_course_out(c);
        c.x530 = t;
        return;
    }
    default: break;
    }
    // The base conductor's code (single races, the i.LINK battle): the leader finished, the race's laps done, or the time
    // limit passed.
    lap_time(cars, i, laps, lap, c.course_out, in, flow, events);
    bool end = cars[size_t(order[0])].finished != 0;
    if (in.laps != 0 && laps == in.laps) end = true;
    if (const u32 limit = u32(in.time_limit) * 60000u; limit != 0 && !(t < limit)) end = true;
    if (end) {
        events.push_back(race_event(5, i, 2));
        finish_car(c, i, 3, hooks);
        if (i == 0) end_countdown(in, flow);
    }
    clear_course_out(c);
    c.x530 = t;
}

// The conductors' split handler, car i over the split line. True when the run ended.
bool race_mode_split(std::span<Car> cars, int i, s32 laps, u32 t, u32 lap, const RaceFlowInputs& in, RaceFlow& flow,
                     std::vector<RaceEvent>& events, const RaceFlowHooks& hooks) {
    (void)t;
    Car& c = cars[size_t(i)];
    if (in.kind == Conductor::MachineTest) {
        if (c.max_speed < c.speed_abs) c.max_speed = c.speed_abs;
        if (in.replay) return false;
        finish_car(c, i, 3, hooks);
        const int type = machine_type(in.config[1]);
        if (c.ghost == 0 && in.flow == 0 && type > 0 && type < 3) new_record(u8(i), lap, in, flow, events);
        if (type == 1 || type == 2) lap_time(cars, i, laps, lap, c.course_out, in, flow, events); // the time to the goal
        else speed_event(cars, i, c.max_speed, in, flow, events);                              // the top speed
        if (i == 0) events.push_back(race_event(5, 0, 2));
        return true;
    }
    if (in.kind == Conductor::License && in.config[2] == 2) {
        licence_end(cars, i, lap, in, flow, events, hooks);
        return true;
    }
    return false;
}


std::array<u32, 16> hud_messages(const ProgramData& d) {
    std::array<u32, 16> m{};
    for (u32 k = 0; k < 16; ++k) m[k] = d.w(ProgramData::Table::Messages, 8 * k + 4);
    // PAL's table counts 50 steps a second (100 where NTSC-U's has 120): at 60 steps a second (PAL's rules or NTSC-U's on a
    // PAL disc) its times are n x 6 / 5, which gives NTSC-U's table (0xFF stays).
    if (d.table_release(ProgramData::Table::Messages) == Program::Pal && !d.behaviour().pal_rate())
        for (u32& t : m)
            if (t != 0xFF) t = t * 6 / 5;
    return m;
}

std::array<F, 4> machine_sectors(const ProgramData& d) {
    std::array<F, 4> s{};
    for (u32 k = 0; k < 4; ++k) s[k] = F::raw(d.w(ProgramData::Table::MachineSectors, 4 * k));
    return s;
}

s32 race_laps(Conductor k, const RaceConfig& cfg, s32 info_laps) {
    switch (k) {
    case Conductor::Battle2P:
    case Conductor::BattleMP: return info_laps;
    case Conductor::TimeTrial: return 1;
    case Conductor::FreeRun:
    case Conductor::FreePractice:
    case Conductor::MachineTest: return 0;
    default: return cfg[1];
    }
}

F car_progress(const Car& c, const CourseQuery& course) { return course.progress(c.pos.x, c.pos.z, -c.pos.y, c.road_seg); }

F race_progress(const Car& c, Conductor k, const CourseQuery& course) {
    return k == Conductor::MachineTest ? nose_progress(c, course) : car_progress(c, course);
}

int sector_count(Conductor k, u8 laps_byte, const CourseQuery& course) {
    return k == Conductor::MachineTest ? machine_sectors_count(laps_byte) : course.sectors();
}

F sector_line(Conductor k, u8 laps_byte, const std::array<F, 4>& machine, const CourseQuery& course, int i) {
    if (k != Conductor::MachineTest) return course.sector_start(i);
    return machine[size_t(i < machine_sectors_count(laps_byte) - 1 ? i : 3)];
}

void keep_best_lap(Car& c, u32 lap) {
    if (lap < c.best_lap) c.best_lap = lap;
}

void hud_message(Car& c, const RaceFlowInputs& in, u8 m) {
    const u32 steps = in.messages[m & 15];
    if (steps == 0 && c.x51d != 0) return;
    c.x51d = u8(steps);
    c.x51c = m;
}

int line_crossings(std::span<Car> cars, std::span<const s32> order, int i, F old, const RaceFlowInputs& in, RaceFlow& flow,
                   std::vector<RaceEvent>& events, const RaceFlowHooks& hooks) {
    Car& c = cars[size_t(i)];
    const F lap = in.course->lap_length(), now = c.f3e4;
    if (old <= now) {
        if (old + lap * kHalf < now) { // the start line backwards
            c.prev.f3e4 = c.prev.f3e4 + lap;
            const u8 b = u8(c.lap_base + 1);
            c.lap_base = b < 0xFB ? b : 0xFA;
            return 1;
        }
        if (c.lap_base != 0 || c.laps == 0 || c.finished != 0) return 0;
        // A sector line (the sector start): the sector handler (unless the player-record test) and 16, then the next sector.
        const F line = sector_line(in.kind, in.config[1], in.machine_sectors, *in.course, c.sector);
        if (old < line && line <= now) {
            const F frac = (line - old) / (now - old);
            const s32 v = ee::to_int(frac * clock_units(in));
            const u32 t = (flow.clock + u32(v)) / 3, split = t - c.x530;
            if (!records_player(in.kind)) race_mode_sector_order(cars, i, s32(c.laps) - 1, c.sector, t, in, flow, events);
            race_mode_sector(cars, i, c.laps, c.sector, split, in, flow, events);
            c.sector = u8(c.sector + 1);
        }
        // The data logger's next mark for a recorded car: its entry's end, the next one's start.
        if (recorded(c, in) && in.logger) {
            const F mark = in.course->logger_mark(c.x493);
            if (old < mark && mark <= now) {
                const F frac = (mark - old) / (now - old);
                const s32 v = ee::to_int(frac * clock_units(in));
                const u32 t = (flow.clock + u32(v)) / 3 - c.x530;
                logger_end(cars, i, logger_entry(c, true, in), t, in, flow, events);
                c.x493 = u8(c.x493 + 1);
                const s32 k = logger_entry(c, false, in);
                if (in.logger_record) logger_start(cars, i, k, u32(clock_step(in) - v), frac, in, flow, hooks);
            }
        }
        // The split line (none at 0): the split handler.
        const F split = split_line(in);
        if (split == kZero || !(old < split) || !(split <= now)) return 0;
        const F frac = (split - old) / (now - old);
        const s32 v = ee::to_int(frac * clock_units(in));
        const u32 t = (flow.clock + u32(v)) / 3, run = t - c.x530;
        const bool rec = recorded(c, in);
        const bool ended = race_mode_split(cars, i, c.laps, t, run, in, flow, events, hooks);
        if (rec && ended && in.logger) logger_end(cars, i, logger_entry(c, true, in), run, in, flow, events);
        return 0;
    }
    if (!(now + lap * kHalf < old)) return 1; // going backwards
    // the start line forwards
    c.prev.f3e4 = c.prev.f3e4 - lap;
    if (c.lap_base != 0) {
        c.lap_base = u8(c.lap_base - 1);
        return 0;
    }
    if (c.finished != 0) return 0;
    const F rest = lap - old;
    const s32 laps = c.laps;
    const u8 sector = c.sector;
    c.sector = 0;
    const F frac = rest / (rest + now);
    c.laps = s16(u16(c.laps) + 1);
    const s32 v = ee::to_int(frac * clock_units(in));
    const u32 t = (flow.clock + u32(v)) / 3, run = t - c.x530;
    const s32 sub = clock_step(in) - v;
    if (laps == 0) { // the race's first crossing: the first-crossing handler (after it: a hand-back makes the car recorded)
        race_mode_first_crossing(cars, i, t, sub, frac, in, flow, events, hooks);
        if (recorded(c, in) && !logs_from_start(in) && in.logger_record) logger_start(cars, i, 0, u32(sub), frac, in, flow, hooks);
    } else { // the sector handler (unless the player-record test) and the lap handler; the data logger's lap
        if (!records_player(in.kind)) race_mode_sector_order(cars, i, laps - 1, sector, t, in, flow, events);
        const bool rec = recorded(c, in);
        race_mode_lap(cars, order, i, laps, t, run, sub, frac, in, flow, events, hooks);
        if (rec && in.logger) {
            const s32 k = logger_entry(c, true, in);
            logger_end(cars, i, k, run, in, flow, events);
            if (multi_lap_licence(in) && c.finished == 0) logger_start(cars, i, k + 1, u32(sub), frac, in, flow, hooks);
        }
    }
    c.x493 = 0;
    return 0;
}

void race_flow_car(std::span<Car> cars, std::span<const s32> order, int i, bool hit, const RaceFlowInputs& in, RaceFlow& flow,
                   std::vector<RaceEvent>& events, const RaceFlowHooks& hooks) {
    Car& c = cars[size_t(i)];
    const F old = c.f3e4;
    c.f3e4 = race_progress(c, in.kind, *in.course);
    if (in.start_count > 0) return;
    if (line_crossings(cars, order, i, old, in, flow, events, hooks) == 1) { // wrong way
        F s = c.speed * c.speed;
        s = s + c.lat_speed * c.lat_speed;
        c.x535 = u8(c.x535 | 1);
        s = sqrt(s);
        if (kFast < s) {
            if (!hit && !(u32(u8(c.control)) - 1 < 2) && c.driver_kind == 0) hud_message(c, in, 1);
        } else if (s < kSlow) {
            hud_message(c, in, 0);
        }
    } else {
        c.x535 = u8(c.x535 & 0xFE);
        hud_message(c, in, 0);
    }
    if (c.driver_kind == 1 && c.finished == 0) hud_message(c, in, c.full_throttle != 0 ? 5 : 4);
    if (hooks.pit_lane) hooks.pit_lane(c, i, c.f3e4, old);
}

void rolling_start(Car& car0, const RaceFlowInputs& in, std::vector<RaceEvent>& events, const RaceFlowHooks& hooks) {
    if (in.start_type != 3) return;
    if (!(s16(car0.ai_hold) > 0)) return;
    car0.ai_hold = u16(car0.ai_hold - 1);
    if (car0.ai_hold != 0) return;
    if (hooks.rolling_handback) hooks.rolling_handback(car0);
    events.push_back(race_event(4, 0, 0));
}

void race_mode_start(std::span<Car> cars, const RaceFlowInputs& in, std::vector<RaceEvent>& events, const RaceFlowHooks& hooks) {
    if (cars.empty()) return;
    Car& c = cars[0];
    switch (in.kind) {
    case Conductor::SinglePlayer: rolling_start(c, in, events, hooks); return;
    case Conductor::FreeRun:
        if (!(s16(c.ai_hold) > 0)) return;
        c.ai_hold = u16(c.ai_hold - 1);
        if (c.ai_hold != 0) return;
        if (hooks.rolling_handback) hooks.rolling_handback(c);
        events.push_back(race_event(4, 0, 0));
        return;
    case Conductor::TimeTrial: // out of the course (either flag)
        if ((c.course_out & 3) == 0 || in.replay || c.finished != 0) return;
        if (c.index == 0) events.push_back(race_event(10, 0, 0));
        finish_car(c, 0, 2, hooks);
        return;
    default: return; // the base conductor's code, the licence's
    }
}

bool race_flow_ported(Conductor) { return true; }

void race_clock(RaceFlow& flow, const RaceFlowInputs& in) {
    if (in.start_count > 0) return;
    const u32 t = flow.clock + u32(3000 / steps_per_second(in.behaviour)); // 3000 a second: 50 a step (PAL rate 60)
    flow.clock = 0x405F7DFDu < t ? 0x405F7DFDu : t;
}

void race_mode_clock(std::span<Car> cars, const RaceFlowInputs& in, RaceFlow& flow, std::vector<RaceEvent>& events,
                     const RaceFlowHooks& hooks) {
    if (in.kind == Conductor::License) // for every car
        for (int i = 0; i < int(cars.size()); ++i) {
            Car& c = cars[size_t(i)];
            if (c.finished != 0) continue;
            if (c.course_out & 2) {
                licence_out(c, i, in, flow, events, hooks);
                continue;
            }
            if (c.course_out & 1) {
                licence_out(c, i, in, flow, events, hooks);
                continue;
            }
            if (in.config[2] != 1) continue;
            F ratio;
            if (!stopped(c, ratio)) continue;
            const s32 v = ee::to_int(ratio * clock_units(in));
            const u32 t = (flow.clock + u32(v)) / 3 - c.x530;
            if (in.logger) logger_end(cars, i, logger_entry(c, true, in), t, in, flow, events);
            licence_end(cars, i, t, in, flow, events, hooks);
        }
    // The base conductor's code: at the countdown's last step the data logger's first entries; the clock.
    if (in.start_count == 1 && logs_from_start(in) && in.logger_record)
        for (int i = 0; i < int(cars.size()); ++i)
            if (recorded(cars[size_t(i)], in)) logger_start(cars, i, 0, 0, kOne, in, flow, hooks);
    race_clock(flow, in);
}

void gap_matrix(std::span<const Car> cars, F lap, GapMatrix& m) {
    const size_t n = cars.size();
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            s32 laps = 0;
            F gap;
            race_gap(cars[j], cars[i], lap, laps, gap);
            m.laps[i][j] = s8(laps >= 0x80 ? 0x7F : laps < -0x7F ? -0x7F : laps);
            m.gap[i][j] = gap;
        }
}

void race_ranking(std::span<Car> cars, RaceOrder& order, Conductor k) {
    if (k != Conductor::FreePractice) return rank(cars, order);
    const int n = int(cars.size());
    for (int p = 1; p < n; ++p) {
        const s32 car = order[size_t(p)];
        int j = p - 1;
        for (; j >= 0 && cars[size_t(car)].best_lap < cars[size_t(order[size_t(j)])].best_lap; --j) order[size_t(j + 1)] = order[size_t(j)];
        order[size_t(j + 1)] = car;
    }
    for (int p = 0; p < n; ++p) cars[size_t(order[size_t(p)])].rank = s8(p + 1);
}

} // namespace gt3r::sim::exact
