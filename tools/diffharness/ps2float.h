#pragma once

// PS2 single-precision arithmetic (EE COP1 and VU0) on raw bit patterns: the engine's
// src/base/ee_float.h, which the interpreter and the bit-exact ports share.

#include "base/ee_float.h"

namespace gt3r::diff {
namespace ps2f = gt3r::ee;
// GT3R_OPLOG=<file> (GT3R_OPLOG_UNIQUE: each operand pair once per kind): every FPU / VU0 multiply (the multiply-adds'
// products too), divide, square root and reciprocal square root the interpreter runs, as 12-byte records {kind, a, b} (kind:
// 1 FPU mul, 2 mula, 6 madd, 7 msub, 8 madda, 9 msuba, 3 FPU div, 4 FPU sqrt, 5 FPU rsqrt, 11 VU0 mul, 13 VU0 div, 14 VU0
// sqrt, 15 VU0 rsqrt).
void oplog(u32 kind, u32 a, u32 b);
} // namespace gt3r::diff
