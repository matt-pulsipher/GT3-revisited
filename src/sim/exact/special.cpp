// The later builds' special cars; see special.h.

#include "sim/exact/special.h"

#include "base/ee_math.h"
#include "formats/db/gt_db.h"
#include "sim/exact/race_flow.h"

#include <cstring>

namespace gt3r::sim::exact {

namespace {

using T = ProgramData::Table;

// The moods' step counts: at 60 steps a second, and at 50 (2002 Tokyo-Geneva's European build: five sixths of each).
struct MoodSteps {
    int alongside;    // the time of the moods a rival alongside and the slip filter set
    int fade, fade_x; // the lamps' fade from class 0, from another class
    int lean;         // the lean's limit
    int period;       // the nod's period; its position turns at a quarter and three quarters
};
constexpr MoodSteps kSteps60{0x78, 0x18, 0x24, 0x1E, 0x30}, kSteps50{0x64, 0x14, 0x1E, 0x19, 0x28};
const MoodSteps& mood_steps(const Car& c) { return pal_rate(c) ? kSteps50 : kSteps60; }

// The nod's position from its phase (later): up over a quarter of the period, down to minus that, back to 0.
int nod(const MoodSteps& k, u8 phase) {
    const int half = k.period / 2, q = half / 2;
    return phase > half + q ? int(phase) - k.period : phase <= q ? int(phase) : half - int(phase);
}

// Car i's whole laps behind car j with the gap folded into half a lap.
int laps_apart(int i, int j, const GapMatrix& g, F lap) {
    int laps = g.laps[size_t(i)][size_t(j)];
    const F gap = g.gap[size_t(i)][size_t(j)], half = lap * F::raw(0x3F000000);
    if (half < gap) ++laps;
    else if (gap < -half) --laps;
    return laps;
}

} // namespace

MoodTables mood_tables(const ProgramData& d) {
    MoodTables k;
    if (!d.has(T::PodMoods)) return k;
    for (u32 i = 0; i < 16; ++i) k.time[i] = d.b(T::PodMoods, i), k.lean[i] = s8(d.b(T::PodMoods, 0x10 + i)), k.lamp[i] = d.b(T::PodMoods, 0x20 + i);
    return k;
}

u8 special_kind(const ProgramData& d, u64 label_id) {
    if (!d.has(T::SpecialCars)) return 0;
    // Each build's labels in the table and the kinds its spec builder gives them (the builder's own constants).
    struct Place {
        u8 off, kind;
    };
    static constexpr Place kFirst[] = {{0x20, 1}, {0x30, 2}}; // 2001 Tokyo, Tokyo-Seoul v1.02
    static constexpr Place kLater[] = {{0x00, 7}, {0x10, 7}, {0x20, 1}, {0x30, 2}, {0x40, 8}, {0x58, 9}, {0x68, 10}}; // v2.00, Tokyo-Geneva
    const Program p = d.behaviour().rule(Rule::SpecBuild);
    const std::span<const Place> places = p == Program::ConceptTokyo || p == Program::ConceptSeoul ? std::span<const Place>(kFirst)
                                                                                                      : std::span<const Place>(kLater);
    u8 kind = 0;
    for (const Place& k : places) {
        char label[0x21] = {};
        for (u32 i = 0; i < 0x20 && k.off + i < ProgramData::size(T::SpecialCars); ++i) label[i] = char(d.b(T::SpecialCars, k.off + i));
        if (label[0] && db::id_hash(label) == label_id) kind = k.kind;
    }
    return kind;
}

void set_mood(Car& c, const MoodTables& k, int mood, int time) {
    if (c.p.special != 1) return;
    auto& s = c.later.special;
    if (s8(s[3]) > 0 && time != 0xFF) return;
    if (s[2] == 0xFF) return;
    if (time == 0 && s[2] != 0) return;
    const u8 cur = s[1] & 0xF;
    const size_t m = size_t(mood) & 0xF;
    if (cur != mood) s[6] = u8(mood), s[1] = u8(cur << 4 | mood);
    s[2] = time == -1 ? k.time[m] : u8(time);
    if (k.lamp[cur] != k.lamp[m]) s[3] = u8(k.lamp[cur] != 0 ? mood_steps(c).fade_x : mood_steps(c).fade);
}

void pass_by_step(PassByState& p, const Proximity& k, int n, bool pal_rate) {
    const s16 cap = pal_rate ? 250 : 300, held = pal_rate ? 51 : 61; // (a second and a step)
    for (size_t i = 0; i < size_t(n) && i < 8; ++i)
        for (size_t j = 0; j < size_t(n) && j < 8; ++j) {
            if (i == j) continue;
            s16& t = p.timer[i][j];
            if (F{} < k.slip[i][j]) {
                if (t < cap) ++t;
            } else if (t > 0) {
                --t;
            }
            const int code = k.code[i][j] & 0xF;
            u8& s = p.state[i][j];
            switch (s) {
            case 0:
                if (p.state[j][i] != 0) break;
                if (code == 4) s = 3;
                else if (code == 5) s = 2;
                else if (t >= held) s = 1;
                break;
            case 1:
                if (p.state[j][i] == 0 && code == 4) s = 3;
                else if (p.state[j][i] == 0 && code == 5) s = 2;
                else if (t == 0) s = 0;
                break;
            case 2:
                if (code == 3 || code == 4) s = 3;
                else if (code == 0 || code == 6) s = 1;
                else if (code == 1 || code == 2) p.state[j][i] = 2, s = 0;
                break;
            case 3:
                if (code == 1 || code == 2) p.state[j][i] = 2, s = 0;
                else if (code == 5 || code == 6) s = 2;
                break;
            default: break;
            }
        }
}

int rival_level(int i, int count, const PassBy& m, const GapMatrix& gaps, F lap_length) {
    int best = 0;
    for (int j = 0; j < count; ++j) {
        if (j == i) continue;
        const u8 a = m[size_t(i)][size_t(j)], b = m[size_t(j)][size_t(i)];
        if (a < 2 && b < 2) continue;
        int level = 0;
        if (const int laps = laps_apart(i, j, gaps, lap_length); laps != 0) level = laps < 0 ? 2 : 1;
        else if (a == 3 || b == 3) level = 5;
        else if (a == 2) level = 4;
        else if (b == 2) level = 3;
        if (best < level) best = level;
    }
    return best;
}

int rival_level(int i, int count, const LaterInputs& in) {
    return in.pass_by && in.gaps ? rival_level(i, count, *in.pass_by, *in.gaps, in.lap_length) : 0;
}

void hybrid_step(Car& c) {
    if (c.p.d.layout == 7) c.later.special[5] = 0;
}

void mood_step(Car& c, const MoodTables& k, int rival, bool all_soft) {
    if (c.p.special != 1) return;
    auto& s = c.later.special;
    const MoodSteps& n = mood_steps(c);
    s[6] = 0;
    bool rest_first = false; // (only the last case asks for the rest mood before the time's step)
    if (c.finished) set_mood(c, k, s8(c.rank) == 1 ? kMoodHappy : kMoodSad, 0);
    else if (c.x51c == 1) set_mood(c, k, kMoodWrongWay, 0);
    else if (c.x535 & 0x10) set_mood(c, k, kMoodPit, 0);
    else if (rival == 5) set_mood(c, k, kMoodPassed, n.alongside);
    else if (c.slip_filter == F::raw(0x3F800000)) set_mood(c, k, kMoodHappy, n.alongside);
    else if (all_soft) set_mood(c, k, kMoodDirt, 0);
    else rest_first = true;
    if (rest_first && s[2] == 0) set_mood(c, k, kMoodRest, -1);
    // the mood's time
    if (s[2] != 0 && s[2] != 0xFF && --s[2] == 0) set_mood(c, k, kMoodRest, -1);
    // the lamps' fade: one whose mood's class is 0 ends at the shorter fade's length
    if (s8(s[3]) > 0) {
        --s[3];
        if (k.lamp[s[1] & 0xF] == 0 && s8(s[3]) <= n.fade) s[3] = 0;
    }
    // the lean, moved only while the nod is level
    const s8 want = k.lean[s[1] & 0xF];
    const bool level = nod(n, s[4]) == 0;
    if (level) {
        const s8 v = s8(s[5]);
        if (want == 1) s[5] = s8(u8(s[5] + 1)) <= n.lean ? u8(s[5] + 1) : u8(n.lean);
        else if (want == -1) s[5] = s8(u8(s[5] - 1)) < -n.lean ? u8(-n.lean) : u8(s[5] - 1);
        else if (v > 0) s[5] = u8(v - 1);
        else if (v < 0) s[5] = u8(v + 1);
    }
    if (s[5] != 0) return;
    // the nod: runs on in a mood whose lean entry is 2, else until it is level again
    if (want != 2 && nod(n, s[4]) == 0) return;
    if (++s[4] >= n.period) s[4] = 0;
}

sim::VehicleState::Special special_look(const Car& c, const MoodTables& k) {
    sim::VehicleState::Special out;
    const u8 kind = c.p.special;
    const auto& s = c.later.special;
    if (kind == 2 || (kind >= 8 && kind <= 10)) {
        u32 bits;
        std::memcpy(&bits, s.data(), 4);
        out.wing = F::raw(bits).host();
    }
    if (kind != 1) return out;
    const MoodSteps& n = mood_steps(c);
    const int fade = s8(s[3]);
    if (fade <= n.fade) { // the new class fading in
        out.lamp = k.lamp[s[1] & 0xF];
        out.lamp_weight = float(n.fade - fade) / float(n.fade);
    } else { // the class before fading out
        out.lamp = k.lamp[s[1] >> 4];
        out.lamp_weight = float(fade - n.fade) / float(n.fade_x - n.fade);
    }
    out.lean = 0.5f - float(s8(s[5])) / float(2 * n.lean);
    out.nod = 0.5f + float(nod(n, s[4])) / (float(n.period) * 0.5f);
    out.mood = s[1] & 0xF;
    out.mood_new = s[6];
    return out;
}

bool sitting(const Car& c) { return c.later.special[0] == (pal_rate(c) ? 0x1D : 0x22); }

F pod_spring(const Car& c, int i, F k) {
    if (c.p.special != 1 || gt3_program(c.behaviour.rule(Rule::PodSpring))) return k;
    const int n = c.later.special[0];
    if (n == 0) return k;
    const int index = c.index; // (F +0x3E8)
    int r = (i + index) % 4;
    if ((index & 7) >= 4) r = 3 - r;
    const int t = (pal_rate(c) ? 6 : 7) * r;
    if (n < t) return k;
    const int m = 1 - (n - t);
    return k * ee::int_to_f(m < 0 ? 0 : m);
}

void hit_mood(Car& c, const MoodTables& k, s32 kind, s32 part) {
    if (c.p.special != 1 || gt3_program(c.behaviour.rule(Rule::HitReport))) return;
    const int hold = pal_rate(c) ? 0xAF : 0xD2; // (a part of the body: its own time; 50 steps a second: five sixths)
    switch (kind) {
    case 0: return set_mood(c, k, kMoodHit, -1);   // a wall (later)
    case 2: return set_mood(c, k, kMoodAngry, -1);
    case 1: break;                                 // a car: by the part
    default: return;
    }
    if (u32(part - 1) < 2) return set_mood(c, k, kMoodAngry, -1);
    if (u32(part - 5) < 2) return set_mood(c, k, kMoodScared, -1);
    if (part < 8) return set_mood(c, k, kMoodHurt, -1);
    if (part < 0xB) return set_mood(c, k, kMoodAngry, hold);
    if (part == 0xB || part == 0xF) return set_mood(c, k, kMoodHurt, hold);
    set_mood(c, k, kMoodScared, hold);
}

void stop_count_step(Car& c) {
    if (c.p.special != 1) return;
    u8& n = c.later.special[0];
    bool stopped = false;
    if (!(c.handbrake < F::raw(0x3F800000))) {
        const F a = c.lat_speed * c.lat_speed, b = c.speed * c.speed;
        // 1 km/h (Tokyo-Geneva Europe's constant is its own)
        stopped = !(F::raw(c.behaviour.rule(Rule::PodStop) == Program::ConceptGenevaEu ? 0x3E8E38E3 : 0x3E8E38E4) < sqrt(b + a));
    }
    const int cap = pal_rate(c) ? 0x1D : 0x22;
    if (stopped) n = u8(n + 1 <= cap ? n + 1 : cap);
    else if (n != 0) --n;
}

namespace {

// The wings' speeds (m/s) as each build's routine holds them: kind 2's in / out, kind 8's, kind 10's, kind 9's range and span.
struct WingSpeeds {
    u32 in, out, in8, out8, in10, out10, lo9, hi9, span9;
};
constexpr WingSpeeds kWingSeoul2{0x41855556, 0x41B1C71D, 0x41E87457, 0x41FA55E8, 0x41B1C71D, 0x42055556, 0x417A55E8, 0x41B2CFA6, 0x40D692C8};
constexpr WingSpeeds kWingGeneva{0x41855554, 0x41B1C71B, 0x41E87455, 0x41FA55E5, 0x41B1C71B, 0x42055554, 0x417A55E5, 0x41B2CFA4, 0x40D692C6};

// The 2002 builds' routine from Tokyo-Seoul v2.00 on (there).
void wing_step_later(Car& c, const WingSpeeds& k) {
    const u8 kind = c.p.special;
    const int type = kind == 2 || kind == 8 || kind == 10 ? 1 : kind == 9 ? 2 : 0;
    if (type == 0) return;
    auto& s = c.later.special;
    const F one = F::raw(0x3F800000);
    F target;
    if (type == 1) {
        const F in = F::raw(kind == 8 ? k.in8 : kind == 10 ? k.in10 : k.in), out = F::raw(kind == 8 ? k.out8 : kind == 10 ? k.out10 : k.out);
        if (s[4] == 0) {
            if (out <= c.speed) s[4] = 1;
        } else if (c.speed < in) {
            s[4] = 0;
        }
        target = s[4] == 1 ? one : F{};
    } else {
        target = c.speed <= F::raw(k.lo9) ? F{} : F::raw(k.hi9) <= c.speed ? one : (c.speed - F::raw(k.lo9)) / F::raw(k.span9);
    }
    const F rate = F::raw(kind == 9 ? 0x40000000 : 0x3F000000);
    u32 bits;
    std::memcpy(&bits, s.data(), 4);
    F v = F::raw(bits);
    if (v < target) {
        v = v + rate * c.dt;
        if (target < v) v = target;
    } else if (target < v) {
        v = v - rate * c.dt;
        if (v < target) v = target;
    } else {
        return;
    }
    std::memcpy(s.data(), &v.bits, 4);
}

} // namespace

void wing_step(Car& c) {
    if (const Program p = c.behaviour.rule(Rule::ActiveWing); p != Program::ConceptTokyo && p != Program::ConceptSeoul)
        return wing_step_later(c, p == Program::ConceptSeoul2 ? kWingSeoul2 : kWingGeneva);
    if (c.p.special != 2) return;
    auto& s = c.later.special;
    if (s[4] == 0) {
        if (F::raw(0x41B1C71D) <= c.speed) s[4] = 1; // 80 km/h
    } else if (c.speed < F::raw(0x41855556)) { // 60 km/h
        s[4] = 0;
    }
    u32 bits;
    std::memcpy(&bits, s.data(), 4);
    F v = F::raw(bits);
    const F step = c.dt * F::raw(0x3F000000);
    if (s[4] == 1) {
        v = v + step;
        if (F::raw(0x3F800000) < v) v = F::raw(0x3F800000);
    } else {
        v = v - step;
        if (v < F{}) v = F{};
    }
    std::memcpy(s.data(), &v.bits, 4);
}

} // namespace gt3r::sim::exact
