#pragma once

// The original's sparks in the interpreter, for the sparks' tests (diff_sparks_exact.cpp) and race_tick_exact's "#sparks"
// races: the particle manager (GT3's made here, a later build's by its own constructor: leaves_probe.h), the emission's VU0
// micro program run from its image, a car's model holder for the emission, and the first difference from sim::exact's pool.

#include "harness.h"
#include "leaves_probe.h"
#include "program_map.h"

#include "sim/exact/sparks.h"

#include <string>

namespace gt3r::diff::sparks_probe {

constexpr Pa kPBlock = 0x002C03B0, kPBuild = 0x0019F898, kReset = 0x0019EFF8, kBurst = 0x0019F450, kUpdate = 0x0019F698,
             kEmit = 0x001AADD0, kVcallSin = 0x00257B24, kMicro = 0x00349600;
constexpr u32 kParticle = 0x54, kMicroSize = 0x398;
// The car record of the model holder the emission is given (its +0x6AC; the later builds' holder is 0x1C bytes longer).
u32 model_car();

// VU0's micro program from byte `start` of its micro memory (the image at kMicro) on the interpreter's VU0 registers, to the
// instruction after the one with the E bit: the upper instructions MUL, MULAi, MADDAi, MADDAbc, MADDi and NOP, the lower
// word a LOI (I bit: the pair's upper instruction still reads the I before it) or an instruction that changes nothing here
// (IOR vi0). False with `err` for anything else.
bool run_micro(Ee& e, u32 start, std::string& err);
// The original's 0x257A44 with its micro program run here (vcallms 0x1D0); the first failure into `micro_err`, which must
// outlive the stand-in.
void install_micro(Ee& e, std::string& micro_err);

// The original's manager in the interpreter's memory: +0 the pool, +4 the active list, +8 the free list (pointers). GT3's
// made here; a later build's by the program's own constructor (`later`), its tuning block by its static constructor's
// routine either way.
struct Manager {
    u32 pm = 0, pool = 0;
    leaves_probe::Manager later;
};
Manager make_manager(Ee& e, std::string& err);
// A later build's manager already made (leaves_probe::make_manager) taken for the sparks: the tuning block built.
Manager adopt_manager(Ee& e, const leaves_probe::Manager& later);
int index_of(const Manager& m, u32 p);

// The first difference between ours and the original's (lists, every record's mirrored words, the random register; a later
// build's: every active particle's, leaves_probe::difference).
std::string difference(Ee& e, Manager& m, const sim::exact::Sparks& s, const Vu0Random& r);

// A car's model holder for the emission (the race entry's +0x11F8 as the loaded model leaves it): +0 the model, whose +4 is
// its GTCI block (the spark points' offset relocated, as 0x227168 does), the car record at model_car().
u32 make_holder(Ee& e, ByteSpan gtci, u32 car);

} // namespace gt3r::diff::sparks_probe
