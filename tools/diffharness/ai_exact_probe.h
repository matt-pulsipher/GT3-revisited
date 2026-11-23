#pragma once

// The original's AI course object K (conductor + 0xBBA8, the AI driver object's +0x08) as sim::exact::AiCourse: its lines as
// the course setup 0x1F92F8 left them, the lap length through K +0x00 -> course -> road block +0x18, the pit-lines flag, the
// program's fallback table 0x2DA508. Only addresses and layouts; the code that runs is the user's disc.

#include "ee.h"
#include "program_map.h"

#include "sim/exact/ai_course.h"

#include <string>

namespace gt3r::diff::ai_exact {

namespace ex = sim::exact;

constexpr Co kCourseObject = 0xBBA8; // conductor offset of K
constexpr Pa kFallback = 0x002DA508; // 6 bytes per requested line
constexpr u32 kPointSize = 0x28;

ex::AiPoint read_point(Ee& ee, u32 a);
ex::AiCourse read_course(Ee& ee, u32 k);
// The first difference between two courses ("" when none): "line 6 point 12 dist 0x... vs 0x...".
std::string course_difference(const ex::AiCourse& ours, const ex::AiCourse& theirs);

} // namespace gt3r::diff::ai_exact
