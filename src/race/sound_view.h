#pragma once

// The camera views of the race sounds (sound §6.4, §7.1). GT3 gives every car one view for all
// its sounds (engine, exhaust, wind, the extra loops and the tyres) from the current camera's
// kind and whether the car is the camera's target, the car it follows.

#include "base/bytes.h"

namespace gt3r::race {

// The views, in the order of the channel gain table's rows: 0 in-car target, 1 in-car other,
// 2 chase target, 3 chase other, 4 any other camera (replay / TV).
enum class SoundView : u8 { InCarTarget, InCarOther, ChaseTarget, ChaseOther, Other };

// The camera's kind as the race sound code reads it: a car-mounted view (the in-car views, every
// replay car mount inside or outside the car, the course camera turned on board), a chase view,
// or any other camera (trackside and course shots, the replay's other procedural cameras, the
// pre-race shot).
enum class CameraSound : u8 { Cockpit, Chase, Other };

// A car's view. The target's sounds in the cockpit sit at the camera (sound §6.5).
constexpr SoundView sound_view(CameraSound kind, bool target) {
    switch (kind) {
    case CameraSound::Cockpit: return target ? SoundView::InCarTarget : SoundView::InCarOther;
    case CameraSound::Chase: return target ? SoundView::ChaseTarget : SoundView::ChaseOther;
    default: return SoundView::Other;
    }
}

} // namespace gt3r::race
