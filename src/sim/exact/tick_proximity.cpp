// Phase 11, car-car proximity: the AI's zone cells and contact intensity, relative-position codes and slipstream; see tick.h.

#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

const F kOne = F::raw(0x3F800000);
const F kHalf = F::raw(0x3F000000);
const F kFour = F::raw(0x40800000);
const F kHeightMargin = F::raw(0x40A00000); // 5 m
const F kViewRange = F::raw(0x42C80000);    // 100 m
const F kCodeRange = F::raw(0x42480000);    // 50 m
const F kContactRange = F::raw(0x41A00000); // 20 m
const F kSlipSpeed = F::raw(0x41DE38E4);    // 100 km/h
const F kFastClosing = F::raw(0x41200000);  // 10 m/s
const F kSlowClosing = F::raw(0x3E8E38E4);  // 1 km/h
const F kCloseTime = F::raw(0x40400000);    // 3 s
const F kCloseGap = F::raw(0x40000000);     // 2 m
const F kQuarter = F::raw(0x3E800000);

F at(const Vec3& v, int k) { return k == 0 ? v.x : k == 1 ? v.y : v.z; }

} // namespace

bool pair_view(const Car& a, const Car& b, PairView& out) {
    if (a.ghost || b.ghost) return false;
    if ((a.x535 & 0x10) != (b.x535 & 0x10)) return false;
    F d[3];
    for (int k = 0; k < 3; ++k) d[k] = at(b.pos, k) - at(a.pos, k);
    const F d0 = d[0] * d[0], d1 = d[1] * d[1], d2 = d[2] * d[2];
    out.dist = ee::sqrt(d0 + d1 + d2);
    if (kViewRange < out.dist) return false;
    if (d[2] < -(b.p.body_height + kHeightMargin)) return false;
    if (a.p.body_height + kHeightMargin < d[2]) return false;
    out.ua = out.va = out.ub = out.vb = F();
    for (int k = 0; k < 3; ++k) {
        out.ua = out.ua + at(a.fwd, k) * d[k];
        out.va = out.va + at(a.lat, k) * d[k];
        out.ub = out.ub - at(b.fwd, k) * d[k];
        out.vb = out.vb - at(b.lat, k) * d[k];
    }
    return true;
}

u8 position_code(F dist, F u, F v, F ahead, F behind, F w) {
    if (kCodeRange < dist) return 0;
    u8 c;
    if (ahead * kFour < u) c = 6;
    else if (ahead < u) c = 5;
    else if (F() < u) c = 4;
    else if (-behind <= u) c = 3;
    else if (-behind * kFour <= u) c = 2;
    else c = 1;
    const F nw = -w;
    if (v < nw * kFour) return c | 0x10;
    if (v < nw) return c | 0x20;
    if (v < nw * kHalf) return c | 0x30;
    if (v <= w * kHalf) return c | 0x40;
    if (v <= w) return c | 0x50;
    if (v <= w * kFour) return c | 0x60;
    return c | 0x70;
}

void zone_cells(Car& c, F dist, F u, F v, F ahead, F behind, F w) {
    // Cells (driver block +0xB4 ..): 0 front, 1 front left, 2 front right, 3 left, 4 right, 5 rear left, 6 rear right, 7 rear.
    auto& z = c.ai.zone;
    if (kContactRange < dist) return;
    if (ahead < u) {
        if (v < -w) z[1] = 2;
        else if (w < v) z[2] = 2;
        else {
            z[0] = 2;
            if (F() < v) z[2] = 1;
            else z[1] = 1;
        }
    } else if (u < -behind) {
        if (v < -w) z[5] = 2;
        else if (w < v) z[6] = 2;
        else {
            z[7] = 2;
            if (F() < v) z[6] = 1;
            else z[5] = 1;
        }
    } else if (v < F()) {
        z[3] = 2;
        if (F() < u) z[1] = 1;
        else z[5] = 1;
    } else {
        z[4] = 2;
        if (F() < u) z[2] = 1;
        else z[6] = 1;
    }
}

bool gap_closing(const Car& a, const Car& b, F u, F v, F& gap, F& closing) {
    gap = closing = F();
    if (a.control == 0 && a.driver_kind == 0) return false;
    const F p = (u - b.p.body_rear) - a.p.body_front;
    if (p <= F()) return false;
    const F half = (a.p.body_width + b.p.body_width) * kHalf;
    if (half < v || v < -half) return false;
    F q;
    for (int k = 0; k < 3; ++k) {
        const F rel = b.time_scale * at(b.vel, k) - a.time_scale * at(a.vel, k);
        q = q + at(a.fwd, k) * rel;
    }
    if (gt3_program(a.behaviour.rule(Rule::GapClosing)) && F() <= q) return false; // (the later builds: also drawing apart)
    gap = p;
    closing = -q;
    return true;
}

void contact_intensity(Car& c, F gap, F closing) {
    F x;
    // (Rule::ContactIntensity, the later builds: 5 km/h, 0.1 km/h, 5 s and 4 m; 2002 Tokyo-Geneva's constants its own)
    const Program rule = c.behaviour.rule(Rule::ContactIntensity);
    const bool later = !gt3_program(rule), geneva = rule >= Program::ConceptGenevaEu;
    const F fast = later ? F::raw(geneva ? 0x3FB1C71B : 0x3FB1C71D) : kFastClosing;
    const F slow = later ? F::raw(geneva ? 0x3CE38E37 : 0x3CE38E3A) : kSlowClosing;
    if (fast < closing) x = kOne;
    else if (slow < closing && gap / closing < (later ? F::raw(0x40A00000) : kCloseTime)) x = kHalf;
    if (x == F() && gap < (later ? F::raw(0x40800000) : kCloseGap)) x = kQuarter;
    if (c.ai.contact < x) c.ai.contact = x;
}

F slipstream_factor(const Car& leader, F x, F y) {
    const F v = leader.speed, h = leader.p.body_width;
    // (2002 Tokyo-Geneva's 100 km/h two units lower: Europe)
    if (folded(leader.behaviour, kSlipSpeed.bits, 0x41DE38E2) < v && x < F() && -v < x && -h < y && y < h) return (v + x) / v;
    return F();
}

void proximity(std::span<Car> cars, Proximity& k) {
    const size_t n = cars.size();
    for (Car& c : cars) {
        c.ai.contact = F();
        c.ai.zone = {};
    }
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            Car& a = cars[i];
            Car& b = cars[j];
            PairView pv;
            if (!pair_view(a, b, pv)) {
                k.slip[i][j] = k.slip[j][i] = F();
                k.code[i][j] = k.code[j][i] = 0;
                continue;
            }
            const F w = (a.p.body_width + b.p.body_width) * kHalf;
            const F ahead = a.p.body_front + b.p.body_rear, behind = a.p.body_rear + b.p.body_front;
            zone_cells(a, pv.dist, pv.ua, pv.va, ahead, behind, w);
            zone_cells(b, pv.dist, pv.ub, pv.vb, behind, ahead, w);
            k.code[i][j] = position_code(pv.dist, pv.ua, pv.va, ahead, behind, w);
            k.code[j][i] = position_code(pv.dist, pv.ub, pv.vb, behind, ahead, w);
            if (pv.dist < kContactRange) {
                F gap, closing;
                if (gap_closing(a, b, pv.ua, pv.va, gap, closing)) contact_intensity(a, gap, closing);
                if (gap_closing(b, a, pv.ub, pv.vb, gap, closing)) contact_intensity(b, gap, closing);
            }
            k.slip[i][j] = slipstream_factor(b, pv.ub, pv.vb);
            k.slip[j][i] = slipstream_factor(a, pv.ua, pv.va);
        }
    for (size_t i = 0; i < n; ++i) {
        F s;
        for (size_t j = 0; j < n; ++j) s = s + k.slip[i][j];
        if (kOne < s) s = kOne;
        cars[i].slip_target = s;
    }
}

} // namespace gt3r::sim::exact
