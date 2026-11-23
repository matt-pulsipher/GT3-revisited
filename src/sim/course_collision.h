#pragma once

// A course as the collision step sees it [physics §5.3]: the wall point records, the program's walk of the wall grid
// (GTRW +0x4C) for a body's box and road segment, the segment links, and the road segment under a point by the
// program's own ground query (each car's segment hint). All in EE arithmetic on the course's own floats.

#include "formats/course/road.h"
#include "sim/collision_exact.h"

#include <vector>

namespace gt3r::sim {

class CourseCollision : public collide::exact::Course {
public:
    using F = collide::exact::F;
    explicit CourseCollision(const course::Road& road);

    const collide::exact::WallPt* walls() const override { return walls_.data(); }
    void wall_candidates(F x0, F z0, F x1, F z1, int hint, std::vector<int>& out) const override;
    bool linked(int a, int b) const override;
    // 0x2280C8 for one point (world x, z): the segment of the highest road triangle that strictly contains it, among
    // the segment `hint` and its neighbours (every segment when hint < 0); -1 when there is none.
    int road_segment(F x, F z, int hint) const override;

    size_t wall_count() const { return walls_.size(); }

private:
    struct Grid {
        std::vector<F> xs, zs;
        std::vector<u32> cell_begin;
        std::vector<course::RoadGridRef> refs;
        bool empty() const { return xs.size() < 2 || zs.size() < 2; }
    };
    struct Tri {
        F ax, ay, az, bx, by, bz, cx, cy, cz;
    };
    bool passes(int seg, int hint) const;

    std::vector<collide::exact::WallPt> walls_;
    Grid wall_grid_, road_grid_;
    std::vector<std::vector<u16>> neighbours_;
    std::vector<u16> before_;              // segment -1's list (Road::before_segments)
    std::vector<std::vector<Tri>> tris_;   // per segment, per triangle
    std::vector<int> every_;               // a course without a wall grid (built in code): every segment start
};

} // namespace gt3r::sim
