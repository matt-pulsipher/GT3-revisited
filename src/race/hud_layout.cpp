#include "race/hud_layout.h"

#include "formats/db/gt_db.h"

#include <exception>
#include <map>
#include <vector>

namespace gt3r::race {

namespace {

using P = HudPart;
constexpr u8 kAny = kAnyView;
constexpr u8 tl(TimeLine t) { return u8(t); }

// The rows of each mode, in the tables' order (their empty rows left out).
constexpr HudRow kRace[] = {
    {P::Rank, 0, kAny, 40, 64, 100, 100, 4, 0},
    {P::LapCount, 0, kAny, 615, 48, 0, 0, 5, 0},
    {P::Messages, 0, kAny, 320, 305, 0, 0, 2, 2},
    {P::Events, 0, kAny, 320, 130, 0, 0, 2, 2},
    {P::Map, 100, kAny, 16, 80, 100, 100, 4, 0},
    {P::TimeLine, tl(TimeLine::Best), kAny, 615, 84, 0, 0, 5, 0},
    {P::Meters, 0, 0, 0, 0, 0, 0, 0, 0},
    {P::Meters, 1, 1, 0, 0, 0, 0, 0, 0},
    {P::Title, 0, kAny, 320, 160, 500, 0, 2, 0},
    {P::Tires, 0, kAny, 20, 435, 0, 0, 4, 1},
    {P::TimeLine, tl(TimeLine::Last), kAny, 615, 112, 0, 0, 5, 0},
    {P::TimeLine, tl(TimeLine::Lap), kAny, 615, 140, 0, 0, 5, 0},
};
constexpr HudRow kEndurance[] = {
    {P::TimeLine, tl(TimeLine::Total), kAny, 615, 84, 0, 0, 5, 0},
    {P::TimeLine, tl(TimeLine::Best), kAny, 615, 112, 0, 0, 5, 0},
    {P::TimeLine, tl(TimeLine::LapTimes), kAny, 615, 140, 0, 0, 5, 0},
    {P::TimeLine, tl(TimeLine::Lap), kAny, 615, 216, 0, 0, 5, 0},
    {P::Meters, 0, 0, 0, 0, 0, 0, 0, 0},
    {P::Meters, 1, 1, 0, 0, 0, 0, 0, 0},
    {P::Map, 0, kAny, 16, 96, 100, 100, 4, 0},
    {P::Rank, 0, kAny, 40, 60, 100, 100, 0, 0},
    {P::LapCount, 0, kAny, 615, 48, 0, 0, 0, 0},
    {P::Messages, 0, kAny, 320, 305, 0, 0, 2, 2},
    {P::Events, 0, kAny, 320, 130, 320, 0, 2, 2},
    {P::Title, 0, kAny, 320, 160, 400, 0, 2, 0},
    {P::Tires, 0, kAny, 20, 435, 100, 100, 4, 1},
};
constexpr HudRow kLicense[] = {
    {P::TimeLine, tl(TimeLine::Best), kAny, 615, 84, 0, 0, 5, 0},
    {P::TimeLine, tl(TimeLine::Time), kAny, 615, 112, 0, 0, 5, 0},
    {P::Meters, 0, 0, 0, 0, 0, 0, 0, 0},
    {P::Meters, 1, 1, 0, 0, 0, 0, 0, 0},
    {P::Map, 0, kAny, 16, 32, 100, 100, 4, 0},
    {P::Target, 0, kAny, 615, 140, 8, -7, 5, 0},
    {P::Target, 1, kAny, 615, 168, 8, -7, 5, 0},
    {P::Target, 2, kAny, 615, 196, 8, -7, 5, 0},
    {P::Title, 0, kAny, 320, 160, 500, 0, 2, 0},
    {P::Events, 0, kAny, 320, 130, 0, 0, 2, 0},
};
constexpr HudRow kMachineTime[] = {
    {P::Meters, 0, 0, 0, 0, 0, 0, 0, 0},
    {P::Meters, 1, 1, 0, 0, 0, 0, 0, 0},
    {P::Map, 0, kAny, 16, 16, 100, 100, 4, 0},
    {P::TimeLine, tl(TimeLine::BestTime), kAny, 615, 84, 0, 0, 5, 0},
    {P::TimeLine, tl(TimeLine::Time), kAny, 615, 112, 0, 0, 5, 0},
    {P::Messages, 0, kAny, 320, 305, 0, 0, 2, 2},
    {P::Events, 0, kAny, 320, 130, 0, 0, 2, 2},
    {P::Title, 0, kAny, 320, 160, 480, 0, 2, 0},
};
constexpr HudRow kFreeRun[] = {
    {P::Meters, 0, 0, 0, 0, 0, 0, 0, 0},
    {P::Meters, 1, 1, 0, 0, 0, 0, 0, 0},
    {P::Map, 0, kAny, 16, 16, 100, 100, 4, 0},
    {P::LapCount, 0, kAny, 615, 48, 0, 0, 5, 0},
    {P::TimeLine, tl(TimeLine::RecordLap), kAny, 615, 84, 0, 0, 5, 2},
    {P::TimeLine, tl(TimeLine::Best), kAny, 615, 112, 0, 0, 5, 2},
    {P::TimeLine, tl(TimeLine::LapTimes), kAny, 615, 140, 0, 0, 5, 2},
    {P::TimeLine, tl(TimeLine::Lap), kAny, 615, 216, 0, 0, 5, 2},
    {P::Messages, 0, kAny, 320, 305, 0, 0, 2, 2},
    {P::Events, 0, kAny, 320, 130, 400, 0, 2, 2},
    {P::Title, 0, kAny, 320, 160, 500, 0, 2, 0},
    {P::Tires, 0, kAny, 20, 435, 0, 0, 4, 1},
};
constexpr HudRow kBattle[] = {
    {P::Rank, 0, kAny, 24, 48, 80, 80, 0, 0},
    {P::LapCount, 0, kAny, 615, 36, 0, 0, 1, 0},
    {P::Messages, 0, kAny, 320, 200, 0, 0, 2, 2},
    {P::Events, 0, kAny, 320, 75, 320, 0, 2, 2},
    {P::Map, 0, kAny, 16, 80, 70, 70, 0, 0},
    {P::DigitalSpeed, 0, kAny, 440, 210, 3, 0, 1, 1},
    {P::BarTach, 0, kAny, 605, 208, 48, 25, 1, 1},
    {P::Gear, 0, kAny, 520, 195, 0, 0, 1, 2},
    {P::Meters, 64, kAny, 610, 211, 128, 31, 1, 1},
    {P::TimeLine, tl(TimeLine::Fastest), kAny, 615, 64, 0, 0, 1, 2},
    {P::TimeLine, tl(TimeLine::Last), kAny, 615, 88, 0, 0, 1, 2},
    {P::TimeLine, tl(TimeLine::Lap), kAny, 615, 112, 0, 0, 1, 2},
    {P::Title, 0, kAny, 320, 120, 0, 0, 2, 2},
    {P::Steering, 0, kAny, 150, 188, 0, 0, 0, 1},
    {P::Tires, 0, kAny, 20, 212, 0, 0, 0, 1},
    {P::AccelBrake, 0, kAny, 68, 212, 20, 0, 0, 1},
};
constexpr HudRow kBattleWide[] = {
    {P::Rank, 0, kAny, 16, 60, 80, 80, 0, 0},
    {P::LapCount, 0, kAny, 310, 60, 0, 0, 1, 0},
    {P::Messages, 0, kAny, 213, 384, 0, 0, 2, 2},
    {P::Events, 0, kAny, 213, 165, 384, 0, 2, 2},
    {P::Map, 0, kAny, 10, 105, 70, 70, 0, 0},
    {P::DigitalSpeed, 0, kAny, 220, 435, 3, 0, 1, 1},
    {P::BarTach, 0, kAny, 413, 435, 65, 25, 1, 1},
    {P::Gear, 0, kAny, 313, 422, 0, 0, 1, 2},
    {P::TimeLine, tl(TimeLine::Fastest), kAny, 413, 96, 0, 0, 1, 2},
    {P::TimeLine, tl(TimeLine::Last), kAny, 413, 120, 0, 0, 1, 2},
    {P::TimeLine, tl(TimeLine::Lap), kAny, 413, 144, 0, 0, 1, 2},
    {P::Title, 0, kAny, 213, 210, 360, 0, 2, 2},
    {P::Steering, 0, kAny, 40, 307, 0, 0, 0, 1},
    {P::Tires, 0, kAny, 20, 392, 0, 0, 0, 1},
    {P::AccelBrake, 0, kAny, 68, 392, 20, 0, 0, 1},
};
constexpr HudRow kMachineSpeed[] = {
    {P::Meters, 0, 0, 0, 0, 0, 0, 0, 0},
    {P::Meters, 1, 1, 0, 0, 0, 0, 0, 0},
    {P::Map, 100, kAny, 16, 16, 100, 100, 4, 0},
    {P::Tires, 0, kAny, 20, 435, 0, 0, 4, 1},
    {P::TimeLine, tl(TimeLine::RecordSpeed), kAny, 615, 84, 0, 0, 5, 0},
    {P::TimeLine, tl(TimeLine::BestSpeed), kAny, 615, 112, 0, 0, 5, 0},
    {P::Title, 0, kAny, 320, 160, 400, 0, 2, 0},
};
constexpr HudRow kReplay[] = {
    {P::Map, 0, kAny, 16, 16, 100, 100, 4, 0},
    {P::LapCount, 0, kAny, 615, 48, 0, 0, 0, 0},
    {P::ViewMode, 0, kAny, 20, 32, 100, 100, 4, 0},
    {P::CarName, 0, 0, 16, 290, 300, 100, 4, 2},
    {P::CarName, 0, 1, 615, 433, 320, 100, 5, 2},
    {P::TimeLine, tl(TimeLine::Lap), kAny, 615, 84, 0, 0, 5, 0},
    {P::Meters, 0, 0, 0, 0, 0, 0, 0, 0},
    {P::Steering, 0, kAny, 40, 350, 100, 100, 4, 2},
    {P::Tires, 0, kAny, 20, 435, 0, 0, 4, 1},
    {P::AccelBrake, 0, kAny, 68, 435, 20, 0, 4, 1},
    {P::Title, 0, kAny, 320, 160, 500, 0, 2, 0},
    {P::Events2, 0, 1, 615, 400, 320, 100, 5, 2},
    {P::Events, 0, kAny, 320, 130, 400, 0, 2, 0},
    {P::Title, 1, kAny, 320, 220, 400, 0, 2, 0},
    {P::DigitalSpeed, 0, 1, 170, 433, 5, 0, 4, 1},
    {P::Gear, 0, 1, 250, 420, 0, 0, 4, 1},
    {P::Meters, 64, 1, 210, 436, 70, 30, 4, 1},
};
constexpr HudRow kDemo[] = {
    {P::ViewMode, 0, kAny, 20, 32, 100, 100, 4, 0},
    {P::Events2, 0, kAny, 610, 425, 570, 100, 5, 0},
    {P::Events, 0, 0, 320, 130, 0, 0, 2, 0},
};
constexpr HudRow kFreePractice[] = {
    {P::Meters, 0, 0, 0, 0, 0, 0, 0, 0},
    {P::Meters, 1, 1, 0, 0, 0, 0, 0, 0},
    {P::Rank, 0, kAny, 40, 60, 100, 100, 4, 0},
    {P::LapCount, 0, kAny, 615, 48, 0, 0, 5, 0},
    {P::TimeLine, tl(TimeLine::PolePosition), kAny, 615, 84, -99, -4, 5, 2},
    {P::TimeLine, tl(TimeLine::Best), kAny, 615, 112, 0, 0, 5, 0},
    {P::TimeLine, tl(TimeLine::LapTimes), kAny, 615, 140, 0, 0, 5, 0},
    {P::TimeLine, tl(TimeLine::Lap), kAny, 615, 216, 0, 0, 5, 0},
    {P::Map, 0, kAny, 16, 96, 100, 100, 4, 0},
    {P::Messages, 0, kAny, 320, 305, 0, 0, 2, 2},
    {P::Events, 0, kAny, 320, 130, 0, 0, 2, 2},
    {P::Title, 0, kAny, 320, 150, 480, 0, 2, 2},
    {P::Tires, 0, kAny, 20, 435, 0, 0, 4, 1},
    {P::Title, 1, kAny, 320, 220, 400, 0, 2, 2},
};
constexpr HudRow kTimeTrial[] = {
    {P::Meters, 0, 0, 0, 0, 0, 0, 0, 0},
    {P::Meters, 1, 1, 0, 0, 0, 0, 0, 0},
    {P::Map, 0, kAny, 16, 32, 100, 100, 4, 0},
    {P::LapCount, 0, kAny, 615, 48, 0, 0, 5, 0},
    {P::TimeLine, tl(TimeLine::Best), kAny, 615, 84, 0, 0, 5, 0},
    {P::TimeLine, tl(TimeLine::TotalTime), kAny, 615, 112, 0, 0, 5, 0},
    {P::Messages, 0, kAny, 320, 305, 0, 0, 2, 2},
    {P::Events, 0, kAny, 320, 130, 0, 0, 2, 2},
    {P::Title, 0, kAny, 320, 160, 500, 0, 2, 0},
    {P::Tires, 0, kAny, 20, 435, 0, 0, 4, 1},
};
// The Analyzer's REPLAY (solo race's logger display): drawn once per track's car (Hud::draw_logger).
constexpr HudRow kLogger[] = {
    {P::Steering, 0, kAny, 42, 70, 100, 100, 0, 1},
    {P::BarTach, 0, kAny, 24, 200, 48, 25, 0, 0},
    {P::AccelBrake, 0, kAny, 24, 150, 20, 100, 0, 1},
    {P::Gear, 1, kAny, 44, 180, 0, 0, 0, 0},
};

} // namespace

namespace {

// set_hud_tables' rows by mode number.
std::map<u8, std::vector<HudRow>>& disc_rows() {
    static std::map<u8, std::vector<HudRow>> rows;
    return rows;
}

} // namespace

bool set_hud_tables(ByteSpan racemode, ByteSpan racedetail) {
    std::map<u8, std::vector<HudRow>> out;
    try {
        const db::Table modes = db::Table::parse(racemode), details = db::Table::parse(racedetail);
        std::map<u64, ByteSpan> by_id;
        for (size_t i = 0; i < details.size(); ++i) by_id[details.id_of(i)] = details.records[i];
        const u64 none = db::id_hash("(Null)");
        for (size_t m = 0; m < modes.size(); ++m) {
            const ByteSpan rec = modes.records[m];
            if (rec.size() < 0x10 + 8 * 32) return false;
            std::vector<HudRow>& rows = out[rec[8]];
            for (size_t k = 0; k < 32; ++k) {
                const u64 id = rd64(rec, 0x10 + 8 * k);
                if (id == none) break;
                const auto it = by_id.find(id);
                if (it == by_id.end() || it->second.size() < 0x22) continue;
                const ByteSpan r = it->second;
                const u16 part = rd16(r, 8);
                if (part == 0) continue;
                rows.push_back({HudPart(part), u8(rd16(r, 10)), u8(s16(rd16(r, 0x18))), s16(rd16(r, 0x1A)), s16(rd16(r, 0x1C)),
                                s16(rd16(r, 0x1E)), s16(rd16(r, 0x20)), r[0x12], r[0x0C]});
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    disc_rows() = std::move(out);
    return true;
}

void clear_hud_tables() { disc_rows().clear(); }

namespace {

std::span<const HudRow> builtin_layout(HudMode m) {
    switch (m) {
    case HudMode::Race: return kRace;
    case HudMode::Endurance: return kEndurance;
    case HudMode::License: return kLicense;
    case HudMode::MachineTime: return kMachineTime;
    case HudMode::FreeRun: return kFreeRun;
    case HudMode::Battle: return kBattle;
    case HudMode::BattleWide: return kBattleWide;
    case HudMode::MachineSpeed: return kMachineSpeed;
    case HudMode::Replay: return kReplay;
    case HudMode::Demo: return kDemo;
    case HudMode::FreePractice: return kFreePractice;
    case HudMode::TimeTrial: return kTimeTrial;
    case HudMode::Logger: return kLogger;
    }
    return {};
}

const HudRow* find_row(std::span<const HudRow> rows, HudPart part, int sub, int view) {
    for (const HudRow& r : rows)
        if (r.part == part && (sub < 0 || r.sub == sub) && (r.view == kAnyView || r.view == view)) return &r;
    return nullptr;
}

} // namespace

std::span<const HudRow> hud_layout(HudMode m) {
    if (!disc_rows().empty())
        if (const auto it = disc_rows().find(u8(m)); it != disc_rows().end()) return it->second;
    return builtin_layout(m);
}

const HudRow* hud_row(HudMode m, HudPart part, int sub, int view) { return find_row(hud_layout(m), part, sub, view); }

const HudRow* hud_builtin_row(HudMode m, HudPart part, int sub, int view) { return find_row(builtin_layout(m), part, sub, view); }

const char* time_line_label(TimeLine t) {
    switch (t) {
    case TimeLine::LapTimes: return "";
    case TimeLine::Total: return "Total";
    case TimeLine::Best:
    case TimeLine::BestTime:
    case TimeLine::BestSpeed: return "Best";
    case TimeLine::Last: return "Last";
    case TimeLine::Lap: return "Lap";
    case TimeLine::Record:
    case TimeLine::RecordSpeed: return "Record";
    case TimeLine::RecordLap: return "Record Lap";
    case TimeLine::PolePosition: return "P.P.";
    case TimeLine::Time:
    case TimeLine::TotalTime: return "Time";
    case TimeLine::Fastest: return "Fastest";
    }
    return "";
}

// The race classes pick their mode when the run is set up; the display then takes, for every
// run but a free practice and 2P, the endurance layout when the event has 10 laps or more or
// a time limit. Replays and demonstrations use their own layouts; 2P keeps its own in a
// demonstration and in a replay drawn as both halves.
HudMode hud_mode(const HudModeSelect& s) {
    if (s.run == HudRun::Battle) {
        if (s.play == HudPlay::Replay && !s.battle_split_replay) return HudMode::Replay;
        return s.wide ? HudMode::BattleWide : HudMode::Battle;
    }
    if (s.play == HudPlay::Replay) return HudMode::Replay;
    if (s.play == HudPlay::Demo) return HudMode::Demo;
    if (s.run == HudRun::FreePractice) return HudMode::FreePractice;
    if (s.event_laps >= 10 || s.time_limit_min != 0) return HudMode::Endurance;
    switch (s.run) {
    case HudRun::TimeTrial: return HudMode::TimeTrial;
    case HudRun::FreeRun: return HudMode::FreeRun;
    case HudRun::License: return HudMode::License;
    case HudRun::MachineTime: return HudMode::MachineTime;
    case HudRun::MachineSpeed: return HudMode::MachineSpeed;
    default: return HudMode::Race;
    }
}

int lap_list_lines(int laps) { return laps >= 1 && laps <= 3 ? laps : 3; }

} // namespace gt3r::race
