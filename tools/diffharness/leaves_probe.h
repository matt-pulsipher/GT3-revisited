#pragma once

// The later builds' particle manager in the interpreter's memory, for the leaves' tests (diff_leaves_exact.cpp) and
// race_tick_exact's leaf checks: the manager built by the program's own constructor (GT3's 0x19EEF8's counterpart), the
// first difference between it and sim::exact's pool (sparks.h with its leaves, leaves.h).

#include "harness.h"
#include "program_map.h"

#include "sim/exact/sparks.h"

#include <string>
#include <vector>

namespace gt3r::diff::leaves_probe {

constexpr Pa kManagerCtor = 0x0019EEF8, kUpdate = 0x0019F698, kEmitter = 0x001DF150;
constexpr u32 kShapes = 0xC, kShapeSize = 0x74, kReq = 0x40;

// The original's manager: +0 the pool, +4 the active list, +8 the free list (pointers); the leaves' shapes from +0xC.
struct Manager {
    u32 pm = 0, pool = 0, stride = 0, leaf_vt = 0, spark_vt = 0;
};
// The constructor run with its allocation, its load of race/particle.img and its texture set-up stood in for (the image not
// loaded: the draw is not run); `err` set when the program's constructor is not as expected.
Manager make_manager(Ee& e, std::string& err);
int index_of(const Manager& m, u32 p);
// The first difference of the eight shapes from ours (none: empty).
std::string shapes_difference(Ee& e, const Manager& m, const sim::exact::LeafRules& r);
// The first difference of the register, the lists and every active particle's record from ours: a leaf's +0x08 .. +0x4C, a
// spark's (GT3's record 4 bytes on: the particle kind's table at +0x04) +0x0A and +0x0C .. +0x44. The two kinds are told by
// the table word at +0x04 (each kind's learned from its first particle).
std::string difference(Ee& e, Manager& m, const sim::exact::Sparks& s, const Vu0Random& r);
// The active particles of ours.
int alive(const sim::exact::Sparks& s);
// A tire effect request as the emitter builds it on its stack (0x40 bytes).
void put_request(Ee& e, u32 a, const sim::exact::TyreEffect& q);

} // namespace gt3r::diff::leaves_probe
