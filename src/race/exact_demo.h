#pragma once

// The disc's demo runs (race/demo_replay.h: GT3's own recordings of the licence tests' and arcade time attacks'
// demonstrations) as races on the exact core: the race GT3's replay playback builds from the save (the event under its
// race mode's conductor, the replay header's car record as the race entry, the start as RaceBase::vf9 sets it) and the
// recording's pad frames for car 0, as a sim::exact::RaceRecord. The game plays it as it plays a replay
// (ExactFieldOptions::playback); every frame is the one GT3's playback 0x1F4660 hands the race.

#include "race/demo_replay.h"
#include "sim/exact/race_record.h"

#include <optional>
#include <string>

namespace gt3r::data {
class CarDatabase;
struct Event;
} // namespace gt3r::data

namespace gt3r::race {

// The race mode of a demo's event: licence tests (L...) the licence conductor, the arcade time attacks (AT...) the time
// trial's.
sim::exact::Conductor demo_conductor(const std::string& event_label);

// The race information's start type a demo's race has (RaceBase::vf9: the event's +0x1F3, then the conductor's virtual 9:
// the time trial's moving start 3; the base conductor's: the event's when the configuration's +0 is 0, else none).
s32 demo_start_type(const data::Event& ev, sim::exact::Conductor kind);

// The demo as a race record: the setup (the event's configuration and start, the licence's judging times, the car record's
// entry) and car 0's frames, the recording's T - 1 then empty ones (`extra` steps more, the run's end). `program` is the
// fingerprint of the tables it will run on (ProgramData::fingerprint).
std::optional<sim::exact::RaceRecord> demo_record(const DemoReplay& demo, const data::Event& ev, const data::CarDatabase& db,
                                                  u64 program, u32 extra = 600, std::string* error = nullptr);

} // namespace gt3r::race
