#pragma once

// The pit lane and the end of a run as the race step runs them per car after phase 13 [ai §1, §13; physics §8.6]: the pit lane's
// state machine 0x1EDB40 (from 0x1EF060, the race flow, race_flow.h) with the AI driver's line and kind switches, the end of a
// run 0x1EACB8, and the step's per-car loop over the race order (0x1F3E24 .. 0x1F3E44) after the single races' virtual 13.
// In the program's arithmetic on the car record; the line requests go through the AI's plan (AiPlan, tick.h). The race display's
// pit events (into / out of the pit lane) are outputs.

#include "sim/exact/race_flow.h"
#include "sim/exact/tick.h"

#include <span>
#include <vector>

namespace gt3r::sim::exact {

// A race display event of the pit lane (0x1ED9F0 / 0x1EDA40: a RaceDisplayInformationEvent of the car's index).
struct PitEvent {
    s32 type = 0; // 1 the car came into the pit lane, 6 out of it
    s32 car = 0;  // the car's index (F +0x3E8)
};

// What the pit lane's state machine and the end of a run read outside the car record.
struct PitInputs {
    const RaceFlowInputs* flow = nullptr; // the HUD messages' display steps, the race's laps and time limit
    u32 clock = 0;                        // the race clock (conductor + 0xE380) before this step's (0x1EAB80's time limit)
    F lap;                                // the lap length (0x1F2BA8)
    bool pit_lines = false;               // the AI course object has the pit lines 3 and 4 (K + 0x08, 0x1F2E98)
    bool service_always = false;          // the conductor's virtual 42: 1 in free practice (every stop services the car)
    WearParams wear;                      // the conductor's wear curve (fresh tyres: the service test, the tyre change)
    const RaceConfig* rules = nullptr;    // the race configuration (conductor + 0xE340) and its byte (+0x8000 +0x37AC): the
    u8 config = 0;                        // event's overrides at a human's end of a run (0x1F6380)
};

// The AI driver's line and kind switches [ai §1, §13]; each returns whether it switched (the line request found a line).
// 0x1E3BF0: recovery state 0; a pad car back to line 7 (-> 6), a CPU car in a pit kind to line 6, both blended, then driver
// kind 0 (not when the request fails); a CPU car in another kind: kind 0.
bool pit_exit(Car& c, AiPlan& plan);
// 0x1E3D08: into the pit lane: line 3 at once, kind 2, the pit flag F +0x535 bit 3.
bool pit_entry(Car& c, AiPlan& plan);
// 0x1E3D60: to the stop line: line 4 at once, kind 3.
bool pit_stop(Car& c, AiPlan& plan);
// 0x1E3DA8: through or out of the pit lane: line 3 at once, kind 4.
bool pit_leave(Car& c, AiPlan& plan);
// 0x1E3C78: the AI drives on: a car in a pit kind, or a pad car in kind 0, to line 6 (blended) first; then kind 1.
bool drive_on(Car& c, AiPlan& plan);
// 0x1E3E38: the stop driver along line 6 (blended): kind 6.
bool stop_on_line(Car& c, AiPlan& plan);
// 0x1E3DF0 (no caller in GT3; the later builds' pod race at the line, later 0x1EF8A0): the stop driver along line 6 (blended):
// kind 5.
bool stop_lap(Car& c, AiPlan& plan);
// 0x1E3E80: the stop driver with the wheels straight: kind 7.
bool stop_straight(Car& c);
// 0x1F6518: the run-on after a finish: corner speeds x 0.8, the throttle multiplier 0.5.
void slow_down(Driver& d, Program rule = Program::NtscU);
// 0x1EACB8: the end of a run with `mode` [ai §1.1]: end_of_run_head, then end_of_run_drive.
void end_of_run(Car& c, int mode, const PitInputs& in, AiPlan& plan);
// 0x1EACB8's finish bookkeeping: mode 4 is 2 on the pit surface, else 0; the car is marked finished. False when it had not
// finished and is in a pit kind (the pit lane drives it on then): the AI calls after it (end_of_run_drive; the race flow's
// RaceFlowHooks::finish_drive) do not run.
bool end_of_run_head(Car& c, int& mode);
// 0x1EACB8 after the finish bookkeeping: a pad car's driver gets the event's overrides (0x1F6548, empty; 0x1F6380); then by
// mode 1 stop_on_line, 2 stop_straight, 3 slow_down and drive_on, otherwise drive_on.
void end_of_run_drive(Car& c, int mode, const PitInputs& in, AiPlan& plan);
// 0x1EAB80: the race is over for the car (no lap base, and the time limit passed or the race's last lap reached).
bool race_over(const Car& c, const PitInputs& in);
// 0x1EDB40: the pit lane's state machine with the car's new and old lap progress [ai §13; physics §8.6]: the wrong way on the
// pit surface, entering on a pit request (or a CPU car hit on the pit surface), the stop or the drive-through, the stop and the
// tyre change, leaving and the exit, the HUD messages. `hit`: the car's collision body was hit this step (0x1FB838: +0x43C or
// +0x440), `hit_43c` its +0x43C (0x1FB860).
void pit_lane(Car& c, F s, F old, bool hit, bool hit_43c, const PitInputs& in, AiPlan& plan, std::vector<PitEvent>& events);

// The race step's work after phase 13 that changes the cars' records (0x1F3DF0 .. 0x1F3E44): with the step's first entry
// mode the conductor's virtual 13 (race_mode_start), then for each position k of the race order the car order[k]'s race flow
// (race_flow_car), with the pit lane (pit_lane), the end of a run's AI calls (end_of_run_drive), a car handed back to its
// player (pit_exit), the AI's take-over (AiPlan::replan) and the free run's AI drive-on (drive_on) as its hooks. The pit
// lane's display events are also posted to `events` (type 18, in order). The rest of the step (the virtuals 1, 14 and 3, the
// gap matrices, the rubber band) is the caller's.
struct FlowStep {
    RaceFlowInputs in;
    RaceFlow* flow = nullptr;                // the race's clock and end countdown
    std::span<const s32> order;              // the race order (conductor + 0xE388)
    size_t first = 0;                        // the first position the loop runs (1 in the step's second entry mode)
    std::span<const u8> hit, hit_43c;        // per car: its collision body's flags of this step's collision step
    bool mode_start = false;                 // run the conductor's virtual 13 (the step's first entry mode)
    PitInputs pit;                           // its flow is set to &in
    std::vector<RaceEvent>* events = nullptr; // the race's event queue posts
    std::vector<PitEvent>* pit_events = nullptr;
    // The free run ghost's extra step (RaceFlowHooks::ghost_step: the race step in its second entry mode) and the snapshots'
    // collision bodies (RaceFlowHooks::save_body / restore_body), the caller's.
    std::function<void()> ghost_step;
    std::function<void(int, Snapshot&)> save_body;
    std::function<void(int, const Snapshot&)> restore_body;
};
void tick_flow(std::span<Car> cars, FlowStep& f, AiPlan& plan);

} // namespace gt3r::sim::exact
