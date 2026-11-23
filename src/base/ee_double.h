#pragma once

// GT3's double precision: the EE has no double FPU, so a `double` in the game's C is libgcc's software
// floating point (GCC 2.95 fp-bit, linked into the program), bit for bit: __adddf3 (0x2793C0), __subdf3 (0x279418), __muldf3
// (0x279480), __extendsfdf2 (0x27AFA0) and __truncdfsf2 (0x279C50). Its rules differ from IEEE hardware and from the EE's
// single FPU (ee_float.h):
//  - an exponent-0 input reads as signed zero (no denormals), also from a single;
//  - exponent 255 (single) / 2047 (double) is Inf or NaN, as in IEEE, so the EE's largest values (0x7F800000 and up) are
//    Inf / NaN here;
//  - results round to nearest even on the guard bits (8 for double, 7 for single), with the bits shifted out on alignment
//    folded into a sticky bit (the multiply's partial sticky bit as fp-bit has it); a result below the smallest normal is
//    truncated into a denormal (or 0), not rounded;
//  - a NaN result is whichever operand was a NaN (with b's sign flipped in a - b), Inf - Inf and Inf x 0 the NaN __thenan_df.
// Integer arithmetic only, so the same on every host.

#include "base/ee_float.h"

namespace gt3r::ee {

// A double as its IEEE bit pattern.
struct D {
    u64 bits = 0;
    static constexpr D raw(u64 b) {
        D d;
        d.bits = b;
        return d;
    }
};
D operator+(D a, D b); // __adddf3
D operator-(D a, D b); // __subdf3
D operator*(D a, D b); // __muldf3
D widen(F x);          // __extendsfdf2: single -> double
F narrow(D x);         // __truncdfsf2: double -> single

// The later builds of the engine (Gran Turismo Concept) link another fp-bit [physics §14.2]: a result below the smallest
// normal is a signed zero (no denormals), a double's exponent 2047 reads as Inf (fraction 0) or NaN with its exponent field
// at 0x3FF, and the narrowing has no NaN / Inf class (its largest exponent, 128, is an ordinary one; above it Inf).
// 2002 Tokyo-Geneva's also rounds toward zero: the guard bits are cut off, the multiply has no rounding step.
enum class Fp : u8 { Gt3, Later, LaterTowardZero };
D add(D a, D b, Fp fp);
D sub(D a, D b, Fp fp);
D mul(D a, D b, Fp fp);
F narrow(D x, Fp fp);

} // namespace gt3r::ee
