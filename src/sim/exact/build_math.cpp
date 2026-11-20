// The library routines that differ between the builds; see build_math.h.

#include "sim/exact/build_math.h"

#include "base/ee_math.h"

namespace gt3r::sim::exact {

using ee::D;
using ee::F;

F atan2(const Behaviour& b, F y, F x) {
    const Program p = b.rule(Rule::Atan2);
    if (gt3_program(p)) return ee::atan2(y, x);
    const F zero = F::raw(0);
    if (x == zero && y == zero) return zero;
    // pi / 2 and pi: GT3's literals 0x3FC90FDB / 0x40490FDB, 2002 Tokyo-Geneva's one lower.
    const bool geneva = p >= Program::ConceptGenevaEu;
    const F half_pi = F::raw(geneva ? 0x3FC90FDA : 0x3FC90FDB), pi = F::raw(geneva ? 0x40490FDA : 0x40490FDB);
    int oct = 0;
    if (y < zero) y = -y, oct = 4;
    if (x < zero) x = -x, oct |= 2;
    F r;
    if (y < x) r = ee::atan(y / x);
    else oct |= 1, r = ee::atan(x / y);
    if (oct & 1) r = half_pi - r;
    if (oct & 2) r = pi - r;
    if (oct & 4) r = -r;
    return r;
}

D widen(const Behaviour& b, F f) {
    if (gt3_program(b.rule(Rule::FloatToDouble)) || ((f.bits >> 23) & 0xFF) != 0xFF) return ee::widen(f);
    // Exponent 255 as an ordinary exponent: 2^128 x 1.fraction.
    return D::raw(u64(f.bits >> 31) << 63 | u64(0x47F) << 52 | u64(f.bits & 0x007FFFFF) << 29);
}

ee::Fp fp_rules(const Behaviour& b) {
    if (gt3_program(b.rule(Rule::FloatToDouble))) return ee::Fp::Gt3;
    return b.rule(Rule::SoftDouble) >= Program::ConceptGenevaEu ? ee::Fp::LaterTowardZero : ee::Fp::Later;
}
D add(const Behaviour& b, D x, D y) { return ee::add(x, y, fp_rules(b)); }
D sub(const Behaviour& b, D x, D y) { return ee::sub(x, y, fp_rules(b)); }
D mul(const Behaviour& b, D x, D y) { return ee::mul(x, y, fp_rules(b)); }
F narrow(const Behaviour& b, D x) { return ee::narrow(x, fp_rules(b)); }

} // namespace gt3r::sim::exact
