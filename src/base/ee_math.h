#pragma once

// GT3's float math (sinf, cosf, sincosf, atanf, atan2f, tanf) in EE arithmetic (ee_float.h), bit for bit: the same
// reduction and polynomial in the same operation order, so a port that calls the routine the program calls gets the
// program's result.
// sincos is a separate routine with its own reduction: its results can differ from sin / cos in the last bit, so each call
// site uses the one the program uses.

#include "base/ee_float.h"

namespace gt3r::ee {

F sin(F x);                       // sinf
F cos(F x);                       // cosf
F sin_uncached(F x);              // the same without the memo of recent results (ee_math.cpp)
F cos_uncached(F x);
void sincos(F x, F& s, F& c);     // sincosf
F atan(F x);                      // atanf
F tan(F x);                       // tanf (the later builds' code, Concept 2001 Tokyo's, is the same arithmetic)
// atan2f: note the program's shortcut: 0 whenever x + y == 0 (so also for y = -x, not only at the origin).
F atan2(F y, F x);
// expf: |x| reduced by whole multiples of ln 2, a rational function of the rest (an odd and an even series on the
// FPU's accumulator), scaled by 2^k, its reciprocal for a negative x. No race routine calls it (the display's do).
F exp(F x);
// The later builds' expf (Gran Turismo Concept 2001 Tokyo): another routine, a series to the eighth power on x less
// its whole multiples of ln 2, scaled by 2^k (0 below the smallest scale, 0x7FFFFFFF above the largest); its constants are
// the program's, 2002 Tokyo-Geneva's folded toward zero (`folded`).
F exp_later(F x, bool folded);

} // namespace gt3r::ee
