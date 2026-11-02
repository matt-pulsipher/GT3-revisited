#pragma once

// A course's road block (GTRW) as the race's initialisation queries it, in EE arithmetic on the course's own floats: the
// single-point ground query (the course's), the race progress query, the lap length,
// the start grid and the sector markers. Built from our parse of the course file (course::Road). The vehicle tick's batched
// ground query is CourseGround (course_ground.h).

#include "formats/course/road.h"
#include "sim/exact/car_record.h"
#include "sim/exact/course_ground.h"

#include <array>
#include <vector>

namespace gt3r::sim::exact {

class CourseQuery {
public:
    explicit CourseQuery(const course::Road& road);

    // The highest road triangle under record g's point (x, z) that contains it (edges included), among the
    // segment g.seg names, its neighbours, or every segment when it is < 0: the record's segment, height, interpolated
    // scalars (clamped to the corners' range), unit normal and plane, raw scalars and the triangle's bytes; height ties go to
    // the later group of the grid cell. g.h is left as it is. On a miss: miss = 1, height = g.h, scalars 1, bytes 0, segment
    // -1 (kept when the point is off the grid); the program then copies its own uninitialised stack words into the normal,
    // plane and raw scalars, which here keep their values. `material`: the triangle's whole material word (triangle +0x08, as
    // the program reads it; the record keeps its low byte), set on a hit.
    void ground(Ground& g, u16* material = nullptr) const;
    // The race progress (metres along the lap) of course point (x, z) from the checkpoints of segment `seg`, of its
    // neighbours, or of every segment when it is not a segment number; 0 when no checkpoint pair brackets the point.
    F progress(F x, F h, F z, s32 seg) const;
    // The lap length (GTRW +0x18).
    F lap_length() const { return length_; }
    // Start grid slot i (x, y, z, yaw).
    struct Slot {
        F x, y, z, yaw;
    };
    const std::vector<Slot>& grid() const { return grid_; }
    // The same for any slot i below 8 as the program reads it: the road block's words at the grid table + 16 i, past the
    // table too (what follows it there), with the road block at address `road` in memory (the words the course loader makes
    // pointers hold their own value plus `road` less the file's base word; with `road` at that base, as the file holds them).
    Slot slot(int i, u32 road) const;
    // The conductor's sector count (at most 4): the sector count; the sector start: sector i's start
    // distance, -1 past the last, 0 when there is no such marker.
    int sectors() const;
    F sector_start(int i) const;
    // With group 1 (the race info's course): the data logger's marks, their count
    // and mark k's distance (0 when there is no such marker).
    int logger_marks() const;
    F logger_mark(int k) const;

private:
    struct Vertex {
        F x, y, z;
        u16 a, b; // +0x0C / +0x0E
    };
    struct Tri {
        u16 a, b, c;
        u16 material, flags; // +0x08 / +0x0C
    };
    struct Segment {
        std::vector<Vertex> v;
        std::vector<Tri> t;
        std::vector<u16> neighbours, checkpoints;
    };
    struct Checkpoint {
        F x0, z0, x1, z1, dist, dx, dz, d;
    };
    struct Marker {
        u16 type, group;
        F dist;
    };
    bool in_checkpoints(F x, F z, int seg, F& out) const;

    F length_;
    u8 wraps_ = 0; // GTRW +0x20: the checkpoint ring wraps (the last one's next is the first)
    std::vector<Slot> grid_;
    std::array<u32, 32> grid_words_{};
    u32 grid_pointers_ = 0, base_ = 0;
    RoadCells cells_;
    std::vector<u32> cell_group_begin_;
    std::vector<course::RoadGridGroup> groups_;
    std::vector<course::RoadGridRef> refs_;
    std::vector<Segment> segs_;
    std::vector<Checkpoint> cps_;
    std::vector<Marker> markers_;
};

} // namespace gt3r::sim::exact
