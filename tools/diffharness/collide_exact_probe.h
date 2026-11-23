#pragma once

// The original's collision manager records (0x4F0-byte bodies at manager + 0xC) as sim::collide::exact::Body, a field by
// field comparison, and a function-by-function rerun of a step on both sides, for the bit-exact collision tests
// (diff_collide_exact.cpp; race_tick_exact checks the step in whole races). Only addresses and layouts; the code that runs is
// the user's disc.

#include "ee.h"

#include "sim/collision_exact.h"

#include <string>
#include <vector>

namespace gt3r::diff::collide_exact {

namespace ex = sim::collide::exact;

constexpr u32 kBody0 = 0xC, kBodySize = 0x4F0;
inline u32 body_at(u32 cm, int i) { return cm + kBody0 + kBodySize * u32(i); }

// The course pointer the bodies hold (+0x478) -> the road block's wall point table (the record pointers' base).
u32 wall_table(Ee& ee, u32 course);
// The road block (GTRW, relocated) of a course pointer.
u32 road_block(Ee& ee, u32 course);

// Body record at `b`; wall pointers become indices from `walls`. The car inputs (step length, steering byte, ghost flag) come
// from the car the record points at (+0x450).
// `course` becomes the body's course (the record holds the original's pointer).
ex::Body read_body(Ee& ee, u32 b, u32 walls, const ex::Course* course = nullptr);
std::vector<ex::Body> read_bodies(Ee& ee, u32 cm, u32 walls, const ex::Course* course = nullptr);
// Writes `r` at `b` (wall indices -> pointers from `walls`): every field read_body reads except the car inputs.
void write_body(Ee& ee, u32 b, const ex::Body& r, u32 walls);

// The first field (in record order) whose bits differ, "" when none: "p[3].sx 0x3F800000 vs 0x3F800001".
std::string first_difference(const ex::Body& ours, const ex::Body& theirs);
std::string first_difference(const std::vector<ex::Body>& ours, const std::vector<ex::Body>& theirs);

// Both sides of an all-bodies step (0x1FA8F8) from the manager state `pre` (raw bytes of manager + 0 .. + 0xC + n x 0x4F0),
// function by function; writes `pre` back first and leaves the interpreter's manager in the original's resulting state.
// Returns "" when every function agreed, else the first function and field that differ.
std::string drill_step(Ee& ee, u32 cm, const std::vector<u8>& pre, const ex::Course* course);
// The walls-only step 0x1FACB8 of body i from `pre` on both sides (writes `pre` back first); "" when equal.
std::string check_walls_step(Ee& ee, u32 cm, const std::vector<u8>& pre, int i, const ex::Course* course);

// Raw bytes of the manager with n bodies.
std::vector<u8> snapshot(Ee& ee, u32 cm);
void restore(Ee& ee, u32 cm, const std::vector<u8>& bytes);

} // namespace gt3r::diff::collide_exact
