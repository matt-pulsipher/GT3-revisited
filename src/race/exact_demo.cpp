// The disc's demo runs as races on the exact core; see exact_demo.h.

#include "race/exact_demo.h"

#include "game/data/car_database.h"
#include "race/exact_setup.h"
#include "sim/exact/params_build.h"

namespace gt3r::race {

namespace ex = sim::exact;

ex::Conductor demo_conductor(const std::string& event_label) {
    return !event_label.empty() && event_label[0] == 'L' ? ex::Conductor::License : ex::Conductor::TimeTrial;
}

s32 demo_start_type(const data::Event& ev, ex::Conductor kind) {
    const ex::RaceConfig cfg = ex::race_config(ev.bytes);
    s32 t = ev.bytes.size() > 0x1F3 ? s32(ev.bytes[0x1F3]) : 2;
    // The race's start-up from the conductor's start type: 0 and 3 none (-1), 1 the event's, 2 moving (3).
    if (kind == ex::Conductor::TimeTrial || kind == ex::Conductor::FreeRun) t = 3; // their start type: 2
    else if (kind == ex::Conductor::FreePractice) t = -1;                          // its start type: 3
    else if (cfg[0] != 0) t = -1;                                                    // the base's: configuration +0 clear
    return t;
}

std::optional<ex::RaceRecord> demo_record(const DemoReplay& demo, const data::Event& ev, const data::CarDatabase& db, u64 program,
                                          u32 extra, std::string* error) {
    if (demo.car_record.size() < ex::kEquipmentImageSize || ev.bytes.size() < 0x1F4) {
        if (error) *error = "the demo's car record or its event record is short";
        return std::nullopt;
    }
    const ex::Conductor kind = demo_conductor(ev.label);
    ex::RaceSetup s;
    s.kind = kind;
    s.event = ev.bytes;
    s.course_category = exact_course_category(db, ev.course);
    s.start_type = demo_start_type(ev, kind);
    s.countdown = s32(ev.bytes[0x1F2]);
    // The licence test's judging times (the event's +0x1C4: minutes, seconds, milliseconds).
    for (u32 k = 0; k < 3; ++k)
        s.licence_times[k] = u32(ev.bytes[0x1C4 + 4 * k]) * 60000u + u32(ev.bytes[0x1C5 + 4 * k]) * 1000u +
                             u32(rd16(ev.bytes, 0x1C6 + 4 * k));
    // The replay header's car record as GT3's playback loads it into the entry.
    ex::StartEntry e;
    e.eq = ex::equipment_image(demo.car_record);
    e.human = 1;
    e.player = true; // its input device plays the recording
    e.grid_slot = demo.grid_slot;
    e.pedal_style = demo.pedal_style != 0;
    e.spec_flag = demo.spec_flag != 0;
    e.wheel_type = s8(demo.wheel_type);
    e.asm_on = demo.asm_on, e.tcs_on = demo.tcs_on;
    e.condition = demo.condition;
    e.best_lap = ex::kNoTime;
    s.entries.push_back(e);
    ex::RaceRecord r = ex::RaceRecord::of(s, ev.course);
    r.program = program; // (r.data: none, the disc's own recording)
    r.behaviour = {sim::Program::NtscU, 60}; // (every disc carries the NTSC-U recordings: they play with its rules)
    r.add_change({0, ex::RaceChange::StartCountdown});
    // Car 0's frames as the playback hands them over: the recording's, then empty ones (past its end).
    const size_t n = demo.size() + extra;
    for (size_t t = 0; t < n; ++t) {
        const ex::PadFrame f = demo.pad_frame(t);
        r.add_frame(std::span<const ex::PadFrame>(&f, 1));
    }
    return r;
}

} // namespace gt3r::race
