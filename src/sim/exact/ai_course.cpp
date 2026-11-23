// The AI course object's line queries; see ai_course.h.

#include "sim/exact/ai_course.h"

#include "sim/exact/build_math.h"

#include "base/ee_math.h"

namespace gt3r::sim::exact {

namespace {

const F kZero = F::raw(0), kHalf = F::raw(0x3F000000);
const F kTurnA = F::raw(0x40C90FDB), kTurnB = F::raw(0x40C90FDB); // $gp 0x350694 / 0x350698: 2 pi

// The race distance wrapped once into [0, lap) (0x1FA018 / 0x1FA368).
F wrap_lap(F s, F lap) {
    if (s < kZero) return s + lap;
    if (lap <= s) return s - lap;
    return s;
}

} // namespace

int AiCourse::resolve(int line) const {
    if (!table) return -1;
    const std::array<s8, 6>& row = fallback[size_t(u32(line) < 8 ? line : 6)];
    for (s8 k : row) {
        if (k == -1) return -1;
        if (k >= 0 && k < 8 && !lines[size_t(k)].empty()) return k;
    }
    return -1;
}

int AiCourse::span(int line, int hint, F s, int& to, int& from, bool* beyond) const {
    const std::vector<AiPoint>& p = lines[size_t(line)];
    const int n = int(p.size());
    if (s < p[0].dist) return from = n - 1, to = 0, 1;
    if (p[size_t(n - 1)].dist <= s) return from = n - 1, to = 0, 2;
    int a = hint;
    for (int t = 0; t <= n; ++t) {
        if (t & 1) a += t;
        else a -= t;
        int k = a;
        if (k >= 0) {
            if (!(k < n)) k -= n;
        } else {
            k += n;
        }
        if (k == 0) continue;
        int j = k - 1;
        if (!(-1 < j)) j += n;
        if (k < 0 || k >= n || j < 0 || j >= n) {
            if (beyond) *beyond = true;
            continue;
        }
        if (p[size_t(j)].dist <= s && s < p[size_t(k)].dist) return from = j, to = k, 3;
    }
    return 0;
}

bool AiCourse::evaluate(int line, int hint, F s, F& x, F& y, F& h) const {
    const F lap = lap_length;
    s = wrap_lap(s, lap);
    int to = 0, from = 0;
    const int found = span(line, hint, s, to, from);
    const std::vector<AiPoint>& p = lines[size_t(line)];
    F num, den;
    switch (found) {
    case 1: { // before the first point: from the last point across the start line
        const F sl = s + lap, dt = p[size_t(to)].dist + lap, df = p[size_t(from)].dist;
        num = sl - df, den = dt - df;
        break;
    }
    case 2: {
        const F dt = p[size_t(to)].dist + lap, df = p[size_t(from)].dist;
        num = s - df, den = dt - df;
        break;
    }
    case 3: {
        const F df = p[size_t(from)].dist;
        num = s - df, den = p[size_t(to)].dist - df;
        break;
    }
    default: return false;
    }
    const F t = num / den;
    const AiPoint &a = p[size_t(from)], &b = p[size_t(to)];
    if (a.kind == 0) {
        x = (t * (b.x - a.x)) + a.x;
        y = (t * (b.y - a.y)) + a.y;
    } else {
        const F ns = -ee::sin(a.heading), c = ee::cos(a.heading);
        F r = a.radius;
        const F cx = a.x - c * r, cy = a.y + ns * r;
        F a0 = atan2(behaviour, cx - a.x, a.y - cy);
        F a1 = atan2(behaviour, cx - b.x, b.y - cy);
        if (kZero < r) {
            if (a1 < a0) a1 = a1 + folded(behaviour, kTurnA.bits, 0x40C90FDA); // (2002 Tokyo-Geneva's: Europe 0x1F99E8)
        } else {
            r = -r;
            if (a0 < a1) a0 = a0 + folded(behaviour, kTurnB.bits, 0x40C90FDA);
        }
        const F ang = a0 + t * (a1 - a0);
        x = cx - ee::sin(ang) * r;
        y = cy + ee::cos(ang) * r;
    }
    h = (t * (b.height - a.height)) + a.height;
    return true;
}

F ai_line_heading(const AiCourse& k, int line, int hint, F s) {
    F x0{}, y0{}, h0{}, x1{}, y1{}, h1{}; // (the program's stack words where an evaluation finds no span)
    auto at = [&](F d, F& x, F& y, F& h) {
        if (line == 4) k.evaluate_stop(4, d, x, y, h);
        else k.evaluate(line, hint, d, x, y, h);
    };
    at(s, x0, y0, h0);
    F dx{}, dy{};
    for (int i = 0; i < 10; ++i) {
        s = s + kHalf;
        at(s, x1, y1, h1);
        dx = x1 - x0, dy = y1 - y0;
        if (F::raw(0x3E4CCCCC) <= ee::sqrt((dx * dx) + (dy * dy))) break; // 0.2 m (Europe 0x3344FC)
    }
    return atan2(k.behaviour, -dx, dy);
}

int AiCourse::evaluate_stop(int line, F s, F& x, F& y, F& h) const {
    const F lap = lap_length;
    const std::vector<AiPoint>& p = lines[size_t(line)];
    const int n = int(p.size());
    s = wrap_lap(s, lap);
    if (s < lap * kHalf) s = s + lap;
    if (s < p[0].dist) return -1;
    if (p[size_t(n - 1)].dist <= s) return -1;
    int from = 0, to = 0;
    F t = kZero;
    for (int k = 0; k < n - 1; ++k) {
        from = k, to = k + 1;
        const F d0 = p[size_t(k)].dist;
        if (d0 <= s) {
            const F d1 = p[size_t(k + 1)].dist;
            if (s < d1) {
                t = (s - d0) / (d1 - d0);
                break;
            }
        }
    }
    const AiPoint &a = p[size_t(from)], &b = p[size_t(to)];
    x = (t * (b.x - a.x)) + a.x;
    y = (t * (b.y - a.y)) + a.y;
    h = (t * (b.height - a.height)) + a.height;
    return from;
}

bool AiCourse::side_swapped(int hint, F s) const {
    const std::vector<AiPoint>& map = lines[5];
    if (map.empty()) return false;
    int to = 0, from = 0;
    if (!span(5, int(map.size()) < hint ? 0 : hint, s, to, from)) return false;
    return map[size_t(from)].kind != 0;
}

int AiCourse::line_b(int line) const { return (line == 1 || line == 6) && !lines[2].empty() ? 2 : -1; }
int AiCourse::line_a(int line) const { return (line == 2 || line == 6) && !lines[1].empty() ? 1 : -1; }

int AiCourse::lane_left(int line, int hint, F s) const { return side_swapped(hint, s) ? line_a(line) : line_b(line); }
int AiCourse::lane_right(int line, int hint, F s) const { return side_swapped(hint, s) ? line_b(line) : line_a(line); }

} // namespace gt3r::sim::exact
