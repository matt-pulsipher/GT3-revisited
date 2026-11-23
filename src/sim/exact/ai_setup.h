#pragma once

// The AI course object's setup at race initialisation [ai §2]: 0x1F9B98 (before the cars are built, in the conductor's
// virtual 23) with the line loader 0x1F92F8, from our parse of the course file's AI line section (slot 31) and the course's
// road (CourseQuery): each control point's position, heading and radius converted from the file's fixed point, its road
// segment (from the file, or traced along the line by ground probes 0x1F8BA0 / 0x1F8BF8 / 0x1F91E8 when the file has none,
// which also marks each arc's turn direction in its kind byte), its race distance (0x18FA20) and the road's height, slope,
// bank and surface under it (0x1F8F40).

#include "formats/course/ai_lines.h"
#include "sim/exact/ai_course.h"
#include "sim/exact/course_query.h"
#include "sim/exact/program_data.h"

namespace gt3r::sim::exact {

// `lines` null: the course file has no AI line section (K +4 null; no lines). The program's line fallback table and loading
// order come from `d`.
AiCourse ai_course_setup(const CourseQuery& course, const course::AiLines* lines, const ProgramData& d);
// 0x1F8EC8: the road's height under physics (x, y) for query height h from segment `seg` (the hint follows a hit), 0 off the
// road (miss set).
F road_height(const CourseQuery& course, s32& seg, bool& miss, F x, F y, F h);

} // namespace gt3r::sim::exact
