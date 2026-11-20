#pragma once

// The C library and soft-double routines whose results differ between the builds of the engine (Rule::Atan2,
// Rule::FloatToDouble [physics §14]). GT3's own are ee::atan2 (base/ee_math.h) and ee::widen
// (base/ee_double.h); the exact core's routines call these with the race's behaviour.

#include "base/ee_double.h"
#include "base/ee_float.h"
#include "sim/exact/program.h"

namespace gt3r::sim::exact {

// atan2f (GT3). GT3's is 0 whenever x + y == 0; the later builds' (Concept 2001 Tokyo) only when both
// are 0. 2002 Tokyo-Geneva's (Europe, Asia) pi / 2 and pi are one unit in the last place lower.
ee::F atan2(const Behaviour& b, ee::F y, ee::F x);

// __extendsfdf2 (GT3). GT3's reads a float's exponent 255 as an infinity or a NaN; the later builds' (Concept 2001
// Tokyo) has no such class: the exponent is an ordinary one (2^128 and above, as the EE's FPU has it).
ee::D widen(const Behaviour& b, ee::F f);

// The soft doubles' arithmetic and narrowing (GT3 __adddf3, __subdf3, __muldf3, __truncdfsf2):
// the later builds' fp-bit (Rule::FloatToDouble's) has no denormals and no NaN class in the narrowing; 2002
// Tokyo-Geneva's (Rule::SoftDouble) rounds toward zero [physics §14.2].
ee::Fp fp_rules(const Behaviour& b);
// 2002 Tokyo-Geneva's compiler folded its decimal constants rounding toward zero, as its soft doubles round
// (Rule::SoftDouble): a routine's constant can be one or a few units in the last place off GT3's. Where a port's constant
// differs so, it keeps the build's (each read from the build's routine) [physics §14.2].
inline bool toward_zero(const Behaviour& b) { return b.rule(Rule::SoftDouble) >= Program::ConceptGenevaEu; }
// x, or its 2002 Tokyo-Geneva value z (toward_zero).
inline ee::F folded(const Behaviour& b, u32 x, u32 z) { return ee::F::raw(toward_zero(b) ? z : x); }
ee::D add(const Behaviour& b, ee::D x, ee::D y);
ee::D sub(const Behaviour& b, ee::D x, ee::D y);
ee::D mul(const Behaviour& b, ee::D x, ee::D y);
ee::F narrow(const Behaviour& b, ee::D x);

} // namespace gt3r::sim::exact
