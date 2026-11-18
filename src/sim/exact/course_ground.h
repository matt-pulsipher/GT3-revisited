#pragma once

// A course's road surface as the vehicle tick's ground query sees it [physics §1.3]: the program's code (the course's
// road block) on the four wheel query records, in EE arithmetic on the course's own floats. Built from our parse of
// the course file (the road block, GTRW): the triangle lookup grid (GTRW +0x48), each segment's vertices (position and the
// two u16 scalars) and triangles (material and flags bytes) and the segment neighbour lists.

#include "formats/course/road.h"
#include "sim/exact/car_record.h"

#include <array>
#include <vector>

namespace gt3r::sim::exact {

// The road block's triangle lookup grid (GTRW +0x48) as both course queries find a point's cell, the cell under
// (x, z) (-1 off the breakpoints), with its breakpoint search.
class RoadCells {
public:
    RoadCells() = default;
    explicit RoadCells(const course::RoadGrid& g);
    int cell(F x, F z) const;

private:
    std::vector<F> xs_, zs_;
};

class CourseGround {
public:
    explicit CourseGround(const course::Road& road);

    // For each record, the highest road triangle that strictly contains its point (x, z) in the grid cell under
    // it, among the segment record 0 holds from the last query (+0x0C), that segment's neighbours, or every segment when it
    // is < 0: its segment, height, interpolated scalars, plane and the triangle's bytes. A record with no triangle keeps
    // the last plane (renormalised) and bytes, with miss = 1 and height -FLT_MAX. `how`: the race's build (Rule::GroundMiss:
    // the later builds' lowest height, the word at Concept 2001 Tokyo's code, is 0xFFFFFFFF, GT3's code 0xFF7FFFFF).
    void query(std::array<Ground, 4>& g, const Behaviour& how = {}) const;

private:
    // Each triangle's terms that depend on its vertices alone, computed once in the query's own operations (so with the
    // same bits): the containment test's (hot) and the rest of a hit's.
    struct TriTest {
        F ax, az;      // the first vertex
        F bx, bz, cx, cz; // the edges from it
        F inv;         // 1 / their cross product
        F ay, by, cy;  // its height, the edges' heights
        float upper;   // the determinant's exact value (by inv's sign) plus its bound, rounded up; infinite where not certain
    };
    struct TriHit {
        F a0, a1, a2;  // attribute A at the first vertex and the edges' differences (x 1 / 65535)
        F b0, b1, b2;  // attribute B
        F dx, dz, plane_d; // the height's slopes along x and z and the plane's d
        u16 attr_a_raw = 0, attr_b_raw = 0; // the first vertex's scalars
        u8 material = 0, flags = 0;
    };
    // A cell's references to one segment in a row (the grid keeps them grouped by segment): its triangles in order.
    struct Run {
        u32 segment = 0, begin = 0, end = 0; // into tris_
    };
    bool passes(u32 seg, int hint) const;
    static unsigned maybe_inside(const TriTest& t, const float (&qx)[4], const float (&qz)[4], unsigned lanes);

    RoadCells cells_;
    std::vector<u32> cell_runs_; // (cells + 1) offsets into runs_
    std::vector<Run> runs_;
    std::vector<u32> tris_;      // the cells' references in order, as triangle indices into test_ / hit_
    std::vector<TriTest> test_;
    std::vector<TriHit> hit_;
    std::vector<std::vector<u16>> neighbours_; // per segment
    bool sides_ = true; // every triangle's terms side_sign operands (ee_float.h)
};

} // namespace gt3r::sim::exact
