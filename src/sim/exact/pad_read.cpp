// The pad read 0x210740's live path; see pad_read.h.

#include "sim/exact/pad_read.h"

namespace gt3r::sim::exact {

namespace {

// The stick response curve 0x3416F8 (256 bytes): a dead band at both ends and around the centre.
u8 curve(const ProgramData& d, u8 v) { return d.b(ProgramData::Table::StickCurve, v); }

s32 at_most_256(s32 v) { return v < 0x100 ? v : 0x100; }

} // namespace

void type_a_channels(PadReading& r) {
    // The type A entries of 0x2E2CF8: 1 (the left stick's x: channel 2 in +2 and +4), 12 (Cross: +4 channel 10), 14 (Square:
    // 11), 13 (Triangle: 8), 11 (Circle: 9), 16 (R2: 15), 18 (L2: 14); a button's +2 is -1 (no analog channel).
    r.channel_a = {2, -1, -1, -1, -1, -1, -1};
    r.channel_b = {2, 10, 11, 8, 9, 15, 14};
}

void pad_buttons(PadFrame& f, u16 b) {
    if (b & 0x400) f.throttle = 1, f.flags = u8(f.flags & 0xFD);
    if (b & 0x800) f.brake = 1, f.flags = u8(f.flags & 0xFB);
    if (b & 0x1000) f.third = 1, f.flags = u8(f.flags & 0xEF);
    if (b & 0x2000) f.back = 1, f.flags = u8(f.flags & 0xF7);
    f.shift = (b & 0x8000) ? s8(-1) : s8((b >> 14) & 1);
}

void pad_dpad(PadFrame& f, u16 b) {
    f.kind = 0;
    f.flags = u8(f.flags & 0xFE);
    f.steer = (b & 0x100) ? s16(2) : (b & 0x200) ? s16(-2) : s16(0);
}

void pad_dpad_pressure(PadFrame& f, const PadReading& r) {
    s32 left = at_most_256(s32(r.channel[5]) * 2), right = at_most_256(s32(r.channel[4]) * 2);
    left = at_most_256(left * 100 / 60);
    right = at_most_256(right * 100 / 60);
    if (left == 0 && right == 0) {
        f.steer = 0;
        f.flags = u8(f.flags & 0xFE);
        f.kind = 0;
        return;
    }
    f.flags = u8(f.flags | 1);
    f.kind = 3;
    f.steer = s16((left - right) << 4); // (+6 gets the larger pressure; phase 3 does not read it)
}

void pad_stick(PadFrame& f, const PadReading& r, const ProgramData& d) {
    if (f.steer != 0) return;
    const s32 code = r.channel_a[0];
    if (code < 0 || code >= 16) return; // (the program indexes the channels with the whole code: only plain channels)
    const u8 t = curve(d, r.channel[size_t(code)]);
    f.flags = u8(f.flags | 1);
    f.kind = 2;
    f.steer = s16((0x100 - 2 * s32(t)) << 4);
}

s32 pad_channel(s32 code, u8 value, s32 scale, const ProgramData& d) {
    const s32 c = code & 0xFF;
    if (c < 4) {
        const s32 t = curve(d, value);
        const s32 half = code & 0x300;
        if (half == 0x100) {
            const s32 v = (0x80 - t) * 2;
            return v >= 0 ? v : 0;
        }
        if (half == 0x200) {
            const s32 v = (t - 0x80) * 2;
            return v >= 0 ? v : 0;
        }
        return t;
    }
    s32 v = at_most_256(s32(value) * 2);
    v = v * 100 / scale;
    return at_most_256(v);
}

void pad_pedals(PadFrame& f, const PadReading& r, const ProgramData& d, bool pressure) {
    // In the program's order: throttle (function 1), brake (2), third (4), back (3); 0x211050 scales them all by 100,
    // 0x2111C0 (the pressure buttons) by 35, 65, 20 and 35.
    struct Pedal {
        int function;
        u8 flag;
        u16 PadFrame::*field;
        s32 scale, pressure_scale;
    };
    static constexpr Pedal kPedals[] = {{1, 0x02, &PadFrame::throttle, 100, 0x23},
                                        {2, 0x04, &PadFrame::brake, 100, 0x41},
                                        {4, 0x10, &PadFrame::third, 100, 0x14},
                                        {3, 0x08, &PadFrame::back, 100, 0x23}};
    for (const Pedal& p : kPedals) {
        const s32 code = (pressure ? r.channel_b : r.channel_a)[size_t(p.function)];
        if (code < 0) continue;
        f.flags = u8(f.flags | p.flag);
        const u8 v = (code & 0xFF) < 16 ? r.channel[size_t(code & 0xFF)] : 0;
        f.*p.field = u16(pad_channel(code, v, pressure ? p.pressure_scale : p.scale, d) << 4);
    }
}

void pad_shift(PadFrame& f, const PadReading& r, const ProgramData& d) {
    // 0x211330 for functions 5 and 6: the configuration's +2 channel at least half way.
    auto on = [&](int function) {
        const s32 code = r.channel_a[size_t(function)];
        if (code < 0) return 0;
        const u8 v = (code & 0xFF) < 16 ? r.channel[size_t(code & 0xFF)] : 0;
        return (pad_channel(code, v, 100, d) << 4) < 0x800 ? 0 : 1;
    };
    s32 s = on(5) != 0 ? 1 : 0;
    if (on(6)) s = s - 1;
    if (s != 0) f.shift = s8(s);
}

void pad_wheel(PadFrame& f, const PadReading& r) {
    if (r.wheel == 0 || r.wheel >= 4) return;
    pad_buttons(f, r.buttons);
    s32 steer = (0x200 - s32(r.wheel_pos)) * 9;
    if (steer < -0x1000) steer = -0x1000;
    else if (!(steer < 0x1001)) steer = 0x1000;
    f.kind = 4;
    f.flags = u8(f.flags | 1);
    f.steer = s16(steer);
    // 0x2115D8: (0xFF - x - dead travel) x 2, at most 256.
    auto pedal = [](u8 x, s32 dead) { return at_most_256((0xFF - s32(x) - dead) * 2); };
    if (const s32 t = pedal(r.wheel_throttle, 0x4B); t > 0) f.throttle = u16(t << 4), f.flags = u8(f.flags | 2);
    if (const s32 b = pedal(r.wheel_brake, 0x50); b > 0) f.brake = u16(b << 4), f.flags = u8(f.flags | 4);
}

PadFrame pad_read(const PadReading& r, const ProgramData& d) {
    PadFrame f; // 0x1F4330
    switch (r.type) {
    case 4:
        pad_buttons(f, r.buttons);
        pad_dpad(f, r.buttons);
        break;
    case 5:
    case 7:
        pad_buttons(f, r.buttons);
        pad_dpad(f, r.buttons);
        pad_stick(f, r, d);
        pad_pedals(f, r, d, false);
        pad_shift(f, r, d);
        break;
    case 8:
        pad_buttons(f, r.buttons);
        pad_dpad_pressure(f, r);
        pad_stick(f, r, d);
        pad_pedals(f, r, d, true);
        pad_shift(f, r, d);
        break;
    default:
        break;
    }
    pad_wheel(f, r);
    return f;
}

} // namespace gt3r::sim::exact
