// race_tick_exact's built inputs and their checks against the original's memory; see tick_inputs.h.

#include "tick_inputs.h"

#include "car_exact_probe.h"
#include "race_hits.h"

#include "game/data/car_database.h"
#include "sim/exact/build_math.h"
#include "sim/exact/car_fields.h"

#include <cstring>
#include <vector>

namespace gt3r::diff::tick_inputs {

namespace {

using ee::F;
using race_hits::fmt;

// A field visitor's view of an input: (name, index (-1: none), bits). differ() runs the visitor over both values.
struct Flat {
    std::vector<u32> bits;
    void operator()(const char*, int, u32 b) { bits.push_back(b); }
};
struct Name {
    size_t want, at = 0;
    std::string name;
    void operator()(const char* n, int i, u32) {
        if (at++ == want) name = i < 0 ? std::string(n) : fmt("%s[%d]", n, i);
    }
};
template <class V> void put(V& v, const char* n, int i, F f) { v(n, i, f.bits); }
template <class V> void put(V& v, const char* n, int i, u32 x) { v(n, i, x); }
template <class V, class T, size_t N> void put_all(V& v, const char* n, const std::array<T, N>& a) {
    for (size_t i = 0; i < N; ++i) {
        if constexpr (std::is_same_v<T, F>) put(v, n, int(i), a[i]);
        else put(v, n, int(i), u32(a[i]));
    }
}
template <class V, size_t N> void put_table(V& v, const char* n, const ex::Table<N>& t) {
    put(v, n, -1, u32(t.n));
    for (s32 k = 0; k < t.n && size_t(k) < N; ++k) put(v, "x", k, t.x[size_t(k)]), put(v, "y", k, t.y[size_t(k)]);
}
template <class V> void put_curve(V& v, const char* n, const ex::PedalCurve& c) {
    put(v, n, -1, u32(c.type)), put(v, "a", -1, c.a), put(v, "b", -1, c.b), put(v, "c", -1, c.c);
}

template <class V> void visit(V& v, const ex::Statics& s) {
    put_all(v, "slip_fade", s.slip_fade), put_all(v, "slip_gain_on", s.slip_gain_on), put_all(v, "slip_gain_base", s.slip_gain_base);
    put_all(v, "slip_gain_knee", s.slip_gain_knee), put_table(v, "rolling_curve", s.rolling_curve), put_all(v, "rolling_mu", s.rolling_mu);
}
template <class V> void visit(V& v, const ex::SurfaceBumps& s) {
    put_all(v, "amp", s.amp), put_all(v, "freq", s.freq), put_all(v, "scaled", s.scaled), put_all(v, "mode", s.mode), put_all(v, "soft", s.soft);
}
template <class V> void visit(V& v, const ex::Slipstream& s) {
    put(v, "drag_share", -1, s.drag_share), put(v, "downforce_share", -1, s.downforce_share), put(v, "k_up", -1, s.k_up),
        put(v, "k_down", -1, s.k_down);
}
template <class V> void visit(V& v, const ex::Condition& k) {
    put(v, "e1", -1, k.e1), put(v, "c1", -1, k.c1), put(v, "e2", -1, k.e2), put(v, "e3", -1, k.e3), put(v, "c2", -1, k.c2);
    put(v, "o1", -1, k.o1), put(v, "co", -1, k.co), put(v, "omax", -1, k.omax), put(v, "x20", -1, k.x20);
}
template <class V> void visit(V& v, const ex::AidInputs& a) {
    put(v, "course_category", -1, u32(a.course_category)), put_all(v, "over_angle", a.over_angle), put_all(v, "over_gain", a.over_gain);
    put(v, "dirt_steer", -1, u32(a.dirt_steer)), put_all(v, "dirt_cut", a.dirt_cut);
}
template <class V> void visit(V& v, const ex::BrakeHeatConsts& k) {
    put(v, "c0", -1, k.c0), put(v, "c1", -1, k.c1), put(v, "c2", -1, k.c2), put(v, "c3", -1, k.c3);
}
template <class V> void visit(V& v, const ex::SteerFeel& s) {
    put(v, "align_gain", -1, s.align_gain), put(v, "lat_gain", -1, s.lat_gain), put(v, "pad_gain", -1, s.pad_gain);
    put(v, "steer_gain", -1, s.steer_gain), put(v, "steer_max", -1, s.steer_max), put(v, "slip_gain", -1, s.slip_gain);
    put(v, "slip_max", -1, s.slip_max), put(v, "use_x420", -1, u32(s.use_x420)), put(v, "mode", -1, u32(s.mode));
    put_curve(v, "curve", s.curve);
}
template <class V> void visit(V& v, const ex::EffectTables& t) {
    for (size_t ch = 0; ch < 3; ++ch)
        for (size_t cl = 0; cl < 3; ++cl) {
            const ex::EffectCurves& e = t.set[ch][cl];
            put(v, "channel", int(ch), u32(cl)); // (a marker: never differs)
            put_table(v, "a", e.a), put_table(v, "b", e.b), put(v, "base", -1, e.base), put(v, "r0", -1, e.r0), put(v, "g", -1, e.g);
            put(v, "by_lat", -1, u32(t.by_lat[ch][cl])), put(v, "by_slip", -1, u32(t.by_slip[ch][cl]));
        }
    put(v, "kerb_scale", -1, t.kerb_scale), put(v, "kerb_x0", -1, t.kerb_x0), put(v, "kerb_y0", -1, t.kerb_y0);
    put(v, "kerb_x1", -1, t.kerb_x1), put(v, "kerb_y1", -1, t.kerb_y1);
}
template <class V> void visit(V& v, const std::array<F, 4>& s) { put_all(v, "sound_scales", s); }
template <class V> void visit(V& v, const ex::GearSettings& g) {
    put(v, "mode", -1, u32(g.mode)), put(v, "down_throttle", -1, u32(g.down_throttle)), put(v, "down_lift", -1, u32(g.down_lift)),
        put(v, "down_brake", -1, u32(g.down_brake));
}
template <class V> void visit(V& v, const ex::PadConstants& k) {
    put(v, "throttle_up", -1, k.throttle_up), put(v, "throttle_down", -1, k.throttle_down), put(v, "brake_up", -1, k.brake_up);
    put(v, "brake_down", -1, k.brake_down), put(v, "third_up", -1, k.third_up), put(v, "third_down", -1, k.third_down);
    put(v, "k1", -1, k.k1), put(v, "k2", -1, k.k2), put(v, "k3", -1, k.k3), put(v, "wheel_slew", -1, k.wheel_slew);
    put_all(v, "wheel_lock", k.wheel_lock), put(v, "analog_jump", -1, k.analog_jump);
    put_all(v, "rate_out", k.rate_out), put_all(v, "rate_back", k.rate_back), put_all(v, "rate", k.rate);
    put_curve(v, "wheel_throttle", k.wheel_throttle), put_curve(v, "wheel_brake", k.wheel_brake), put_all(v, "surface_class", k.surface_class);
}
template <class V> void visit(V& v, const ex::WearParams& w) {
    put(v, "fresh", -1, w.fresh), put(v, "fresh_loss", -1, w.fresh_loss), put(v, "knee1", -1, w.knee1), put(v, "loss1", -1, w.loss1);
    put(v, "knee2", -1, w.knee2), put(v, "loss2", -1, w.loss2), put(v, "max", -1, w.max), put(v, "max_loss", -1, w.max_loss);
    put(v, "worn", -1, w.worn), put(v, "slip_cap", -1, w.slip_cap);
}
template <class V> void visit(V& v, const ex::RubberBand& b) {
    put(v, "slow", -1, b.slow), put(v, "slow_start", -1, b.slow_start), put(v, "slow_full", -1, b.slow_full);
    put(v, "fast", -1, b.fast), put(v, "fast_start", -1, b.fast_start), put(v, "fast_full", -1, b.fast_full), put(v, "global", -1, b.global);
}
template <class V> void visit(V& v, const ex::CourseOut& r) {
    put(v, "on", -1, u32(r.on)), put(v, "pit", -1, u32(r.pit)), put(v, "soft", -1, u32(r.soft)), put(v, "stretch", -1, u32(r.stretch));
    put(v, "lap", -1, r.lap), put(v, "lo", -1, r.lo), put(v, "hi", -1, r.hi);
}

template <class T> std::string diff_of(const char* what, const T& ours, const T& theirs) {
    Flat a, b;
    visit(a, ours), visit(b, theirs);
    for (size_t k = 0; k < a.bits.size() && k < b.bits.size(); ++k)
        if (a.bits[k] != b.bits[k]) {
            Name n{k, 0, {}};
            visit(n, ours);
            return fmt("%s %s: 0x%08X vs 0x%08X", what, n.name.c_str(), a.bits[k], b.bits[k]);
        }
    if (a.bits.size() != b.bits.size()) return fmt("%s: %zu vs %zu values", what, a.bits.size(), b.bits.size());
    return {};
}

F f32(Ee& ee, u32 a) { return F::raw(ee.r32(a)); }

ex::Table<8> mem_table8(Ee& ee, u32 a) {
    ex::Table<8> t{};
    t.n = s16(ee.r16(a));
    const u32 xs = ee.r32(a + 4), ys = ee.r32(a + 8);
    for (u32 k = 0; k < 8 && s32(k) < t.n; ++k) t.x[k] = f32(ee, xs + 4 * k), t.y[k] = f32(ee, ys + 4 * k);
    return t;
}

ex::PedalCurve mem_curve(Ee& ee, u32 a) { return {s32(ee.r32(a)), f32(ee, a + 4), f32(ee, a + 8), f32(ee, a + 12)}; }

// The conductors' vtables (conductor + 0x8000 + 0x63A8).
u32 vtable_of(ex::Conductor k) {
    switch (k) {
    case ex::Conductor::SinglePlayer: return pa(0x0033D920);
    case ex::Conductor::TimeTrial: return pa(0x0033D548);
    case ex::Conductor::License: return pa(0x0033D1F0);
    case ex::Conductor::FreeRun: return pa(0x0033D390);
    case ex::Conductor::Battle2P: return pa(0x0033CD10);
    case ex::Conductor::BattleMP: return pa(0x0033CEB0);
    case ex::Conductor::MachineTest: return pa(0x0033D050);
    case ex::Conductor::FreePractice: return pa(0x0033D6E8);
    case ex::Conductor::Training: return 0; // the later builds' pace-car conductor: no GT3 address
    }
    return 0;
}

} // namespace

Built& built() {
    static Built b;
    return b;
}

void Built::reset(const ex::ProgramData& pristine, ex::Conductor k, const data::Event& ev, u8 category, s32 battle_wear,
                  s32 battle_catchup) {
    data = pristine;
    kind = k;
    course_category = category;
    cfg = ex::race_config(ByteSpan(ev.bytes.data(), ev.bytes.size()));
    flow = 0, flow_scale = F::raw(0x3F800000);
    band_on = ex::rubber_band_answer(k, cfg, battle_catchup, program()); // virtual 21 asks 11, then 10
    wear_on = ex::wear_answer(k, cfg, category, battle_wear, program());
    ok = true;
    rebuild();
}

void Built::poke(Ee& ee, u32 addr, u8 v) {
    const TableByte t = table_byte(addr); // addr: the NTSC-U address of a table byte
    ee.w8(data.address(t.table, t.off), v);
    data.poke(t.table, t.off, v);
    rebuild();
}

void Built::rebuild() {
    race = ex::race_inputs(data, kind, cfg, wear_on, course_category);
    band = ex::rubber_band_params(cfg, band_on);
}

u8 course_category(const data::CarDatabase& db, const data::Event& ev) {
    for (const data::Course& c : db.courses())
        if (c.label == ev.course) return c.surface;
    return 0;
}

ex::Statics mem_statics(Ee& ee) {
    ex::Statics s;
    constexpr Pa kCurve = 0x002D2CE0, kMu = 0x002D2CF0;
    s.rolling_curve.n = s16(ee.r16(kCurve));
    if (s.rolling_curve.n < 1 || size_t(s.rolling_curve.n) > s.rolling_curve.x.size()) throw std::runtime_error("rolling-resistance table: bad size");
    const u32 x = ee.r32(kCurve + 4), y = ee.r32(kCurve + 8);
    for (u32 k = 0; k < u32(s.rolling_curve.n); ++k) s.rolling_curve.x[k] = f32(ee, x + 4 * k), s.rolling_curve.y[k] = f32(ee, y + 4 * k);
    for (u32 k = 0; k < 16; ++k) s.rolling_mu[k] = f32(ee, kMu + 4 * k);
    for (u32 k = 0; k < 2; ++k)
        s.slip_fade[k] = ee.r8(pa(0x002D2934) + k), s.slip_gain_on[k] = ee.r8(pa(0x002D2936) + k), s.slip_gain_base[k] = ee.r8(pa(0x002D2938) + k),
        s.slip_gain_knee[k] = ee.r8(pa(0x002D293A) + k);
    return s;
}

ex::SurfaceBumps mem_bumps(Ee& ee) {
    ex::SurfaceBumps sb{};
    for (u32 s = 0; s < 16; ++s) {
        sb.amp[s] = f32(ee, pa(0x002D2D30) + 4 * s), sb.freq[s] = f32(ee, pa(0x002D2D70) + 4 * s);
        sb.scaled[s] = s8(ee.r8(pa(0x002D2DB0) + s)), sb.mode[s] = s8(ee.r8(pa(0x002D2DC0) + s)), sb.soft[s] = ee.r32(pa(0x002D53D8) + 4 * s);
    }
    return sb;
}

ex::Slipstream mem_slipstream(Ee& ee) { return {f32(ee, pa(0x00352924)), f32(ee, pa(0x00352928)), f32(ee, pa(0x0035292C)), f32(ee, pa(0x00352930))}; }

ex::Condition mem_condition(Ee& ee) {
    if (ee.r8(pa(0x002D294C)) != 0) {
        const u32 b = pa(0x002D2B00);
        return {f32(ee, b), f32(ee, b + 4), f32(ee, b + 8), f32(ee, b + 12), f32(ee, b + 16), f32(ee, b + 20), f32(ee, b + 24),
                f32(ee, b + 28), f32(ee, b + 32)};
    }
    std::array<u8, 9> bytes{};
    for (u32 k = 0; k < 9; ++k) bytes[k] = ee.r8(pa(0x002D2950) + k);
    return ex::condition_params(bytes, ex::toward_zero(disc_behaviour()));
}

ex::AidInputs mem_aids(Ee& ee, u32 car) {
    ex::AidInputs in;
    in.course_category = ee.r8(ee.r32(car + 4) + 0x8000 + k_off(0x37AD));
    in.over_angle = {ee.r8(pa(0x2D295D)), ee.r8(pa(0x2D295E))};
    in.over_gain = {ee.r8(pa(0x2D295F)), ee.r8(pa(0x2D2960))};
    in.dirt_steer = ee.r8(pa(0x2D2965));
    in.dirt_cut = {ee.r8(pa(0x2D2966)), ee.r8(pa(0x2D2967))};
    // (the later builds' ASCC switches, where ProgramData located them)
    using T = ex::ProgramData::Table;
    if (const ex::ProgramData& d = built().data; d.has(T::RaceSwitches) && !sim::gt3_program(d.behaviour().rule(sim::Rule::Ascc))) {
        const u32 at = d.address(T::RaceSwitches, 0);
        in.ascc = {1, ee.r8(at + 3), ee.r8(at + 4), ee.r8(at + 5)};
    }
    return in;
}

ex::BrakeHeatConsts mem_brake_heat(Ee& ee) { return {f32(ee, pa(0x0049D0B0)), f32(ee, pa(0x0049D0B4)), f32(ee, pa(0x0049D0B8)), f32(ee, pa(0x0049D0BC))}; }

ex::SteerFeel mem_steer_feel(Ee& ee) {
    ex::SteerFeel s{};
    s.align_gain = f32(ee, pa(0x00352934)), s.lat_gain = f32(ee, pa(0x00352938)), s.pad_gain = f32(ee, pa(0x0035293C)), s.steer_gain = f32(ee, pa(0x00352940));
    s.steer_max = f32(ee, pa(0x00352944)), s.slip_gain = f32(ee, pa(0x00352948)), s.slip_max = f32(ee, pa(0x0035294C));
    s.use_x420 = ee.r8(pa(0x002D465F)), s.mode = ee.r8(pa(0x002D4661));
    s.curve = mem_curve(ee, pa(0x0049D050));
    return s;
}

ex::EffectTables mem_effects(Ee& ee) {
    ex::EffectTables t{};
    const u32 sets[3] = {pa(0x0049C9B0), pa(0x0049CBC0), pa(0x0049CDD0)};
    for (u32 ch = 0; ch < 3; ++ch)
        for (u32 cl = 0; cl < 3; ++cl) {
            const u32 a = sets[ch] + 0x84 * cl;
            ex::EffectCurves& e = t.set[ch][cl];
            e.a = mem_table8(ee, a), e.b = mem_table8(ee, a + 0x4C);
            e.base = f32(ee, a + 0x58), e.r0 = f32(ee, a + 0x5C), e.g = f32(ee, a + 0x60);
            const u32 sel = pa(0x002D35B8) + 0xA0 * ch + 0x28 * cl;
            t.by_lat[ch][cl] = ee.r8(sel), t.by_slip[ch][cl] = ee.r8(sel + 0x1F);
        }
    t.kerb_scale = f32(ee, pa(0x0049CFC0)), t.kerb_x0 = f32(ee, pa(0x0049CFC4)), t.kerb_y0 = f32(ee, pa(0x0049CFC8)), t.kerb_x1 = f32(ee, pa(0x0049CFCC)),
    t.kerb_y1 = f32(ee, pa(0x0049CFD0));
    return t;
}

std::array<F, 4> mem_sound(Ee& ee) { return {f32(ee, pa(0x0049D03C)), f32(ee, pa(0x0049D040)), f32(ee, pa(0x0049D044)), f32(ee, pa(0x0049D048))}; }

ex::GearSettings mem_gears(Ee& ee) {
    const u32 b = pa(0x002D4628);
    return {ee.r8(b + 0x14), ee.r8(b + 0x15), ee.r8(b + 0x16), ee.r8(b + 0x17)};
}

ex::PadConstants mem_pad(Ee& ee) {
    ex::PadConstants k{};
    k.throttle_up = f32(ee, pa(0x2D4930)), k.throttle_down = f32(ee, pa(0x2D4934)), k.brake_up = f32(ee, pa(0x2D4938)), k.brake_down = f32(ee, pa(0x2D493C));
    k.third_up = f32(ee, pa(0x2D4940)), k.third_down = f32(ee, pa(0x2D4944));
    k.k1 = f32(ee, pa(0x352970)), k.k2 = f32(ee, pa(0x352974)), k.k3 = f32(ee, pa(0x352978)), k.wheel_slew = f32(ee, pa(0x35297C));
    for (u32 i = 0; i < 4; ++i) k.wheel_lock[i] = f32(ee, pa(0x352980) + 4 * i);
    k.analog_jump = f32(ee, pa(0x352990));
    for (u32 i = 0; i < 3; ++i) k.rate_out[i] = f32(ee, pa(0x49D080) + 4 * i), k.rate_back[i] = f32(ee, pa(0x49D090) + 4 * i), k.rate[i] = f32(ee, pa(0x49D0A0) + 4 * i);
    k.wheel_throttle = mem_curve(ee, pa(0x49D060)), k.wheel_brake = mem_curve(ee, pa(0x49D070));
    for (u32 i = 0; i < 16; ++i) k.surface_class[i] = ee.r8(pa(0x2D4948) + i);
    return k;
}

ex::WearParams mem_wear(Ee& ee, u32 car) {
    const u32 cp = ee.r32(car + 4) + 0x84;
    auto f = [&](u32 o) { return f32(ee, cp + o); };
    return {f(0x00), f(0x04), f(0x08), f(0x0C), f(0x10), f(0x14), f(0x18), f(0x1C), f(0x20), f(0x24)};
}

ex::RubberBand mem_band(Ee& ee, u32 cond) {
    auto f = [&](u32 o) { return f32(ee, cond + o); };
    return {f(0xAC), f(0xB0), f(0xB4), f(0xB8), f(0xBC), f(0xC0), f(0xC4)};
}

std::string differ(const ex::Statics& a, const ex::Statics& b) { return diff_of("statics", a, b); }
std::string differ(const ex::SurfaceBumps& a, const ex::SurfaceBumps& b) { return diff_of("surface tables", a, b); }
std::string differ(const ex::Slipstream& a, const ex::Slipstream& b) { return diff_of("slipstream", a, b); }
std::string differ(const ex::Condition& a, const ex::Condition& b) { return diff_of("condition", a, b); }
std::string differ(const ex::AidInputs& a, const ex::AidInputs& b) { return diff_of("aid inputs", a, b); }
std::string differ(const ex::BrakeHeatConsts& a, const ex::BrakeHeatConsts& b) { return diff_of("brake heat", a, b); }
std::string differ(const ex::SteerFeel& a, const ex::SteerFeel& b) { return diff_of("steer feel", a, b); }
std::string differ(const ex::EffectTables& a, const ex::EffectTables& b) { return diff_of("effect tables", a, b); }
std::string differ(const std::array<F, 4>& a, const std::array<F, 4>& b) { return diff_of("engine sound", a, b); }
std::string differ(const ex::GearSettings& a, const ex::GearSettings& b) { return diff_of("gear settings", a, b); }
std::string differ(const ex::PadConstants& a, const ex::PadConstants& b) { return diff_of("pad constants", a, b); }
std::string differ(const ex::WearParams& a, const ex::WearParams& b) { return diff_of("wear curve", a, b); }
std::string differ(const ex::RubberBand& a, const ex::RubberBand& b) { return diff_of("rubber band", a, b); }
std::string differ(const ex::CourseOut& a, const ex::CourseOut& b) { return diff_of("course-out rule", a, b); }

ex::CourseOut mem_course_out(Ee& ee, u32 cond, ex::CourseOut r) {
    const u32 table = ee.r32(ee.r32(cond + cond_off(0xBBA8) + 4) + 0x18);
    r.lap = f32(ee, ee.r32(ee.r32(ee.r32(cond + 0x8000 + k_off(0x37A8))) + 0x70) + 0x18);
    r.stretch = table != 0 && s32(ee.r32(table)) >= 6;
    r.lo = r.hi = F();
    if (r.stretch) {
        const s32 n = s32(ee.r32(table));
        r.lo = f32(ee, table + 4 + 0x28 * 1 + 0x14);
        r.hi = f32(ee, table + 4 + 0x28 * u32(n - 2) + 0x14);
    }
    return r;
}

ex::GapMatrix mem_gaps(Ee& ee, u32 cond) {
    ex::GapMatrix m;
    for (u32 i = 0; i < 8; ++i)
        for (u32 j = 0; j < 8; ++j) m.laps[i][j] = s8(ee.r8(cond + cond_off(0xB8F4) + 8 * i + j)), m.gap[i][j] = f32(ee, cond + cond_off(0xB934) + 32 * i + 4 * j);
    return m;
}

F mem_pit_box_heading(Ee& ee, u32 cond, int box) {
    const u32 table = ee.r32(ee.r32(cond + cond_off(0xBBA8) + 4) + 0x18);
    return f32(ee, table + u32(box) * 0x28 + 0x28);
}

namespace {
// A later build's globals whose places the version table does not have right yet stop a read (a trap): named, not fatal.
template <class Fn> std::string readable(Fn fn) {
    if (!sim::concept_program(program())) return fn();
    try {
        return fn();
    } catch (const EeTrap& t) {
        return std::string("not readable on this build (a global's place in the version table): ") + t.what();
    }
}
std::string race_inputs_differ_(Ee& ee, u32 car);
std::string check_race_start_(Ee& ee, u32 cond, int ncars);
} // namespace

std::string race_inputs_differ(Ee& ee, u32 car) {
    return readable([&] { return race_inputs_differ_(ee, car); });
}
std::string check_race_start(Ee& ee, u32 cond, int ncars) {
    return readable([&] { return check_race_start_(ee, cond, ncars); });
}

namespace {

std::string race_inputs_differ_(Ee& ee, u32 car) {
    const ex::RaceInputs& r = built().race;
    for (std::string d : {differ(r.statics, mem_statics(ee)), differ(r.bumps, mem_bumps(ee)), differ(r.prep.slip, mem_slipstream(ee)),
                          differ(r.prep.condition, mem_condition(ee)), differ(r.aids, mem_aids(ee, car)),
                          differ(r.brake_heat, mem_brake_heat(ee)), differ(r.feel, mem_steer_feel(ee)), differ(r.effects, mem_effects(ee)),
                          differ(r.sound_scales, mem_sound(ee)), differ(r.gears, mem_gears(ee)), differ(r.pad, mem_pad(ee)),
                          differ(r.prep.wear, mem_wear(ee, car))})
        if (!d.empty()) return d;
    return {};
}

std::string check_race_start_(Ee& ee, u32 cond, int ncars) {
    const Built& b = built();
    if (!b.ok) return "the built inputs were not set up for the race";
    const u32 K = cond + 0x8000, vt = ee.r32(K + k_off(0x63A8));
    if (vt != vtable_of(b.kind)) return fmt("conductor vtable 0x%X, not the %s conductor's", vt, ex::conductor_name(b.kind));
    const u32 car0 = cond + 0xC8;
    if (std::string d = race_inputs_differ(ee, car0); !d.empty()) return d;
    for (u32 i = 0; i < 0x40; ++i) {
        if (i == 1) continue; // the lap count (the harness's)
        if (ee.r8(cond + cond_off(0xE340) + i) != b.cfg[i]) return fmt("race configuration +0x%X: 0x%02X vs 0x%02X", i, b.cfg[i], ee.r8(cond + cond_off(0xE340) + i));
    }
    if (std::string d = differ(b.band, mem_band(ee, cond)); !d.empty()) return d;
    // A later build's stop zone (its initialisation's, later 0x1EE280: Rule::StopZone) from our lap length.
    if (sim::concept_program(program())) {
        const ex::StopZone z = ex::stop_zone_setup(b.data, b.data.behaviour(), b.cfg[2], b.ai.lap_length, ncars);
        const u32 a = cond + 0xE408;
        if (ee.r8(a) != z.active) return fmt("stop zone %u vs %u", z.active, ee.r8(a));
        if (z.active == 1) {
            if (ee.r32(a + 0xC) != z.base.bits || ee.r32(a + 0x10) != z.spacing.bits)
                return fmt("stop zone 0x%08X + 0x%08X vs 0x%08X + 0x%08X", z.base.bits, z.spacing.bits, ee.r32(a + 0xC), ee.r32(a + 0x10));
            for (u32 o = 2; o < 0x74; ++o)
                if (o < 0xA || o >= 0x14) // (the counts and the points)
                    if (ee.r8(a + o) != 0) return fmt("stop zone +0x%X: 0x%02X (cleared at the set-up)", o, ee.r8(a + o));
        }
    }
    if (ee.r8(K + k_off(0x37AC)) != 0) return fmt("race-flow byte %u", ee.r8(K + k_off(0x37AC)));
    if (ee.r32(K + k_off(0x37B0)) != 0x3F800000) return fmt("race-flow scale 0x%08X", ee.r32(K + k_off(0x37B0)));
    for (u32 o = cond_off(0xB7B4); o < cond_off(0xB7B4) + 0x140; o += 4)
        if (ee.r32(cond + o) != 0) return fmt("proximity matrices: conductor +0x%X 0x%08X", o, ee.r32(cond + o));
    // The conductor's answers, its virtuals called directly (with the vtable's this adjustment).
    auto vf = [&](int n, std::vector<Arg> extra = {}) {
        const u32 e = vt + 8 * u32(conductor_slot(n));
        std::vector<Arg> args{Arg::ptr(cond + u32(s32(s16(ee.r16(e)))))};
        args.insert(args.end(), extra.begin(), extra.end());
        return s32(ee.call(ee.r32(e + 4), args, 10'000'000).v0_s32());
    };
    const u8 cat = b.course_category;
    const ex::CourseOut rule = ex::course_out_rule(b.kind, cat, b.data.behaviour().rule(sim::Rule::LicenceJudge));
    struct Answer {
        int n;
        s32 ours, theirs;
    };
    const s32 v10 = vf(10, {Arg::ptr(cond + cond_off(0xE340))}), v11 = vf(11, {Arg::ptr(cond + cond_off(0xE340))});
    const Answer answers[] = {
        {10, s32(b.wear_on), s32(v10 != 0 && !ex::dirt_course(cat))},
        {11, s32(b.band_on), s32(v11 != 0)},
        // (a later build's base conductor answers 255 for no pass mode, GT3's -1: the step reads a byte there)
        {32, ex::rubber_band_mode(b.kind, b.cfg), [&] { const s32 v = vf(32); return sim::concept_program(program()) && v == 255 ? -1 : v; }()},
        {35, ex::pit_mode(b.kind, cat, b.data.behaviour().rule(sim::Rule::LicenceJudge)), vf(35)},
        {37, rule.pit, vf(37)},
        {38, rule.soft, vf(38)},
        {39, s32(ex::has_pits(b.kind)), vf(39)},
        {40, s32(rule.on), s32(ee.r32(vt + 8 * u32(conductor_slot(40)) + 4) == pa(0x001DA020))},
    };
    for (const Answer& a : answers)
        if (a.ours != a.theirs) return fmt("conductor virtual %d: %d vs %d", a.n, a.ours, a.theirs);
    if (std::string d = differ(b.course_out(), mem_course_out(ee, cond, b.course_out())); !d.empty()) return d;
    // The pit box headings where the stop line has the car's pit box (the program reads past the line for the others).
    const std::vector<ex::AiPoint>& stop = b.ai.lines[4];
    for (int i = 0; i < ncars; ++i) {
        const int box = int(s8(ee.r8(race_setup::car_addr(cond, i) + ex::kF + f_off(0x4DE))));
        if (box < 0 || size_t(box) >= stop.size()) continue;
        const F ours = ex::pit_box_heading(b.ai, box), theirs = mem_pit_box_heading(ee, cond, box);
        if (ours.bits != theirs.bits) return fmt("car %d: pit box %d heading 0x%08X vs 0x%08X", i, box, ours.bits, theirs.bits);
    }
    // The gap matrices of the grid (with our lap length; the diagonal stays as 0x1ED410 cleared it).
    std::vector<ex::Car> cars;
    for (int i = 0; i < ncars; ++i) cars.push_back(car_exact::read_car(ee, race_setup::car_addr(cond, i)));
    ex::GapMatrix ours;
    ex::gap_matrix(cars, b.ai.lap_length, ours);
    const ex::GapMatrix theirs = mem_gaps(ee, cond);
    for (size_t i = 0; i < 8; ++i)
        for (size_t j = 0; j < 8; ++j)
            if (ours.laps[i][j] != theirs.laps[i][j] || ours.gap[i][j].bits != theirs.gap[i][j].bits)
                return fmt("gap matrix [%zu][%zu]: %d 0x%08X vs %d 0x%08X", i, j, ours.laps[i][j], ours.gap[i][j].bits, theirs.laps[i][j],
                           theirs.gap[i][j].bits);
    // The ranking the initialisation ends with: a sorted order (ranking it again changes nothing) and the ranks it gives.
    ex::RaceOrder order;
    order.fill(-1);
    for (int k = 0; k < ncars; ++k) order[size_t(k)] = s32(ee.r32(cond + cond_off(0xE388) + 4 * u32(k)));
    const ex::RaceOrder before = order;
    std::vector<ex::Car> ranked = cars;
    ex::race_ranking(ranked, order, b.kind);
    if (order != before) return "race order: not as the ranking leaves it";
    for (int i = 0; i < ncars; ++i)
        if (ranked[size_t(i)].rank != cars[size_t(i)].rank) return fmt("car %d: rank %d vs %d", i, ranked[size_t(i)].rank, cars[size_t(i)].rank);
    return {};
}

} // namespace

} // namespace gt3r::diff::tick_inputs
