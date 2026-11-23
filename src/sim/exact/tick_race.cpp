// The race step's own parts around the vehicle phases: the race-flow gate and the rubber band; see tick.h.

#include "sim/exact/tick.h"

namespace gt3r::sim::exact {

namespace {

const F kZero{}, kOne = F::raw(0x3F800000);

} // namespace

bool race_flow_gate(std::span<Car> cars, u8 flow, F scale) {
    if (flow == 0) return true;
    bool run = false;
    for (Car& c : cars) {
        F a = c.flow - scale;
        if (a < kZero) {
            run = true;
            a = a + kOne;
        }
        c.flow = a;
    }
    return run;
}

s32 lap_count(const Car& c) { return s32(c.laps) - s32(c.lap_base); }

void race_gap(const Car& ref, const Car& car, F lap, s32& laps, F& gap) {
    s32 n = lap_count(ref) - lap_count(car);
    F d = ref.f3e4 - car.f3e4;
    if (n > 0) {
        if (d < kZero) --n, d = d + lap;
    } else if (n < 0) {
        if (kZero < d) ++n, d = d - lap;
    }
    laps = n;
    gap = d;
}

F speed_up(const RubberBand& k, const Car& car, s32 laps, F gap) {
    if (car.x535 & 0x10) return kOne;
    if (laps > 0 || k.fast_full <= gap) return k.fast + kOne;
    if (!(k.fast_start < gap)) return kOne;
    const F span = k.fast_full - k.fast_start;
    const F t = (gap - k.fast_start) / span;
    return k.fast * t + kOne;
}

F slow_down(const RubberBand& k, s32 laps, F gap) {
    if (laps > 0 || k.slow_full <= gap) return kOne - k.slow;
    if (!(k.slow_start < gap)) return kOne;
    const F span = k.slow_full - k.slow_start;
    const F t = (gap - k.slow_start) / span;
    return kOne - k.slow * t;
}

F band_scale(const RubberBand& k, const Car& ref, Car& car, F lap, F scale, bool cap) {
    F s = kOne;
    if (car.finished != 0) s = s * scale;
    else {
        s32 laps = 0;
        F gap;
        race_gap(ref, car, lap, laps, gap);
        const bool behind = laps > 0 || (laps == 0 && kZero < gap);
        if (behind) s = (k.fast == kZero ? s : speed_up(k, car, laps, gap)) * scale;
        else s = (k.slow == kZero ? s : slow_down(k, -laps, -gap)) * scale;
    }
    // PAL's virtual 4 caps every mode's scale at 1.3 (0x351F18); its i.LINK battle (0x1F13F0) adds no cap of its own.
    if (pal_rules(car, Rule::RubberBand) && F::raw(0x3FA66666) < s) s = F::raw(0x3FA66666);
    car.time_scale = s * k.global;
    if (cap && !pal_rules(car, Rule::BattleCap)) {
        const F top = k.fast + kOne;
        if (!(s <= top)) {
            car.time_scale = top * k.global;
            return top;
        }
    }
    return s;
}

void rubber_band(std::span<Car> cars, std::span<const s32> order, s32 mode, const RubberBand& k, F lap, bool cap) {
    const s32 n = s32(cars.size());
    s32 last = -1, ref = -1; // the last and the first car of the order the AI does not drive
    for (s32 i = 0; i < n; ++i) {
        const s32 idx = order[size_t(i)];
        if (cars[size_t(idx)].control != 2) {
            last = idx;
            if (ref == -1) ref = idx;
        }
    }
    if (last == -1) ref = 0;
    // (the program reads the words before the order for a reference ranked 0: cannot happen, the start and the ranking number
    // the cars 1 .. n; with the reference's rank in 1 .. n every position below is inside the order)
    if ((mode == 0 || mode == 2) && (cars[size_t(ref)].rank < 1 || cars[size_t(ref)].rank > n)) return;
    auto at = [&](s32 pos) -> Car& { return cars[size_t(order[size_t(pos)])]; };
    if (mode == 1) {
        for (s32 i = 0; i < n; ++i)
            if (i != ref) band_scale(k, cars[size_t(ref)], cars[size_t(i)], lap, kOne, cap);
    } else if (mode == 2) {
        const s32 r = cars[size_t(ref)].rank;
        for (s32 i = 0; i < r - 1; ++i) band_scale(k, at(i + 1), at(i), lap, kOne, cap);
        for (s32 i = r; i < n; ++i) band_scale(k, at(i - 1), at(i), lap, kOne, cap);
    } else if (mode == 0) {
        const s32 j = s32(cars[size_t(ref)].rank) - 1;
        F s = kOne;
        for (s32 i = j; i > 0; --i) s = band_scale(k, at(i), at(i - 1), lap, s, cap);
        s = kOne;
        for (s32 i = j; i < n - 1; ++i) s = band_scale(k, at(i), at(i + 1), lap, s, cap);
    }
}

} // namespace gt3r::sim::exact
