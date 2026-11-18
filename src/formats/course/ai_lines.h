#pragma once

// AI driving lines (course file slot 31, no magic).
//
//   0x00 u32 section size (excluding trailing padding)
//   0x04 u32 version: 8 (7 in one leftover file of a later disc, see below)
//   0x08 u32 0
//   0x0C u32 line_offsets[7]   (0 = line absent; may point at an empty list)
//   0x28 "build MM/DD/YY hh:mm:ss\n" (tool build stamp), zero padded to 0x48
//        (version 7 has no stamp: the first line starts at 0x28)
//   line: u32 count, then count x 40-byte control points:
//     s32 flags   low byte 0/1; upper bytes carry small packed values
//     s32 x, z    fixed point 1/4096 m, in the simulation frame (X mirrored
//                 relative to the scenery: world_x = -x / 4096)
//     s32 0
//     s32 segment index into Road::segments (GTRW)
//     s32 0
//     s32 radius   fixed point 1/4096: signed radius of the arc that starts at
//                  this point when flags bit 0 is set (> 0 turns left in the
//                  world frame); 0 on straight segments
//     s32 0, s32 0
//     s32 packed   low 16 bits: heading, 4096 = full turn (sim frame);
//                  high 16 bits: small flags
//
// Lines 0-2 run the whole lap and are near-identical (variants of the racing
// line), 3 and 4 cover part of the lap (likely pit in/out), 5 is another full
// lap variant and 6 is optional. Control points are sparse (one every
// 50-150 m, denser in corners). The line between them is a chain of straights
// and circular arcs (see `radius`); `AiLine::sample_arcs` rebuilds it.

#include "formats/course/road.h"

#include <string>
#include <vector>

namespace gt3r::course {

struct AiPoint {
    Vec3 pos;           // scenery/world frame; y = 0 until projected onto the road
    u32 flags = 0;
    u32 segment = 0;
    float lateral = 0;  // raw / 4096: signed arc radius (m) when flags & 1, else 0
    float radius() const { return (flags & 1) ? lateral : 0.0f; }
    float heading = 0;  // radians, world frame (converted from the sim frame)
    u32 packed = 0;
    s32 raw_x = 0, raw_z = 0, raw_radius = 0; // +0x04 / +0x08 / +0x18 as stored (fixed point 1/4096)
};

struct AiLine {
    std::vector<AiPoint> points;
    bool closed = false; // full lap

    // Catmull-Rom through the control points, `step` metres apart. Only
    // suitable for dense lines; prefer sample_arcs().
    std::vector<Vec3> sample(float step = 2.0f) const;
    // The line as the game defines it: from each control point a straight
    // (flags bit 0 clear) or a circular arc of radius() (bit 0 set) to the
    // next point, sampled about `step` metres apart. Of the two arcs of that
    // radius joining the points, the one continuing the previous segment's
    // direction is used (hairpins can exceed 180 degrees). y is left 0.
    // `loop` adds the segment from the last point back to the first.
    std::vector<Vec3> sample_arcs(float step, bool loop) const;
    // sample_arcs() with heights from the road (following the previous
    // sample's height where the road passes over itself). Loops when the line
    // covers most of the lap.
    std::vector<Vec3> sample(const Road& road, float step = 2.0f) const;
};

// Centre line with arc length, used to express positions as (s, d).
struct TrackFrame {
    std::vector<Vec3> points; // closed loop
    std::vector<float> s;     // cumulative distance at each point (s[0] = 0)
    float length = 0;

    explicit TrackFrame(const Road& road);
    // Projects (x, z) onto the loop: distance along it and signed lateral
    // offset (positive to the left of the travel direction, Y up).
    void project(float x, float z, float& s_out, float& d_out) const;
    Vec3 at(float s, float d) const; // y left 0
};

struct AiLines {
    std::vector<AiLine> lines; // always 7 entries; absent lines are empty
    std::string build;

    static AiLines parse(ByteSpan data);
    // Fills in point heights from the road surface.
    void project(const Road& road);
};

} // namespace gt3r::course
