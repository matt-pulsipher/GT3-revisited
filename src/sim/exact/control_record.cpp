// Pad recording (control_record.h): the recorder's bit stream and frame coding, in the program's integer
// arithmetic.

#include "sim/exact/control_record.h"

namespace gt3r::sim::exact {

RawPad raw_pad(const PadFrame& f) {
    RawPad r{};
    auto h = [&](u32 o, u16 v) { r[o] = u8(v), r[o + 1] = u8(v >> 8); };
    r[0] = f.flags, r[1] = f.kind, r[2] = u8(f.shift);
    h(4, u16(f.steer)), h(8, f.throttle), h(0xA, f.brake), h(0xC, f.back), h(0xE, f.third);
    return r;
}

PadFrame pad_of(const RawPad& r) {
    auto h = [&](u32 o) { return u16(r[o] | r[o + 1] << 8); };
    PadFrame f;
    f.flags = r[0], f.kind = r[1], f.shift = s8(r[2]);
    f.steer = s16(h(4)), f.throttle = h(8), f.brake = h(0xA), f.back = h(0xC), f.third = h(0xE);
    return f;
}

namespace {

// The n low bits of v, highest first, into the stream (the byte being filled is stored after every call).
void put(std::span<u8> mem, ControlRecorder& r, u64 v, u32 n) {
    u64 a1 = v << ((64 - n) & 63);
    u32 t1 = r.ptr, t0 = r.cur, a3 = r.bits;
    auto store = [&](u32 at, u8 b) {
        if (at < mem.size()) mem[at] = b; // (the stream stays inside its block: it closes 0x40 bytes before the end)
    };
    for (u32 a2 = n; a2 != 0;) {
        const u32 free = 8 - a3;
        const u64 top = a1 >> ((a3 + 0x38) & 63);
        const u32 take = free < a2 ? free : a2;
        a3 += take;
        t0 |= u32(top);
        if (a3 == 8) store(t1, u8(t0)), a3 = 0, t0 = 0, ++t1;
        a2 -= take;
        a1 <<= take;
    }
    if (a3 != 0) store(t1, u8(t0));
    r.bits = a3, r.ptr = t1, r.cur = u8(t0);
}

// n bits of the stream, highest first.
u64 get(std::span<const u8> mem, ControlRecorder& r, u32 n) {
    u32 t1 = r.ptr, a2 = r.bits;
    u64 t0 = r.cur, a1 = 0;
    for (u32 a3 = n; a3 != 0;) {
        if (a2 == 0) t0 = t1 < mem.size() ? mem[t1] : 0, a2 = 8, ++t1;
        const u32 take = a2 < a3 ? a2 : a3;
        a1 = (a1 << take) | (t0 >> ((8 - take) & 63));
        t0 = (t0 << take) & 0xFF;
        a3 -= take, a2 -= take;
    }
    r.bits = a2, r.ptr = t1, r.cur = u8(t0);
    return a1;
}

s32 low32(u64 v) { return s32(u32(v)); }

// A count (1 or more) as its gamma code: n = count - 1 (0 for 0) in s ones, a zero and s bits, s the code's class.
void put_count(std::span<u8> mem, ControlRecorder& r, u32 count) {
    u64 s0 = count != 0 ? u32(count - 1) : 0u;
    u32 s1 = 0;
    if (s0 != 0)
        do {
            s0 -= u64(1) << s1;
            ++s1;
        } while (s0 >= (u64(1) << s1));
    put(mem, r, ~u64(0), s1);
    put(mem, r, 0, 1);
    put(mem, r, s0, s1);
}

// A gamma code's count (n + 1).
s32 get_count(std::span<const u8> mem, ControlRecorder& r) {
    u32 s1 = 0;
    while (get(mem, r, 1) != 0) ++s1;
    const s32 v = low32(get(mem, r, s1));
    return v + s32(u32(1) << (s1 & 31));
}

// A signed difference: its class (3 bits), sign, and the magnitude past the class's start in 2 class + 1 bits.
void put_diff(std::span<u8> mem, ControlRecorder& r, s32 v) {
    u32 sign = 0;
    s32 m = v;
    if (m < 0) m = -m, sign = 1;
    s32 s1 = 1;
    if (m >= 2) {
        s32 step = 2;
        do {
            m -= step;
            s1 += 2;
            step = s32(u32(1) << (u32(s1) & 31));
        } while (m >= step);
    }
    put(mem, r, u64(s64(s1 >> 1)), 3);
    put(mem, r, sign, 1);
    put(mem, r, u64(s64(m)), u32(s1));
}

// A signed difference. The class's start is what the coding's classes before it hold: 2, 8, 32, ...
s32 get_diff(std::span<const u8> mem, ControlRecorder& r) {
    const s32 c = low32(get(mem, r, 3));
    const s32 sign = low32(get(mem, r, 1));
    const s32 v = low32(get(mem, r, u32(2 * c + 1)));
    s32 start = 0;
    for (s32 k = 1; k <= (c & 7); ++k) start += s32(u32(1) << (2 * k - 1));
    const s32 m = v + start;
    return sign != 0 ? -m : m;
}

s16 field16(const RawPad& f, u32 o) { return s16(u16(f[o] | f[o + 1] << 8)); }
void set16(RawPad& f, u32 o, u16 v) { f[o] = u8(v), f[o + 1] = u8(v >> 8); }

// The last frame's steering replaced by its linear prediction (when it is 16 or more either way).
void predict(ControlRecorder& r) {
    const s32 a1 = field16(r.last, 4);
    const s32 v1 = r.steer_before;
    r.steer_before = a1;
    if ((a1 < 0 ? -a1 : a1) < 0x10) return;
    s32 p = a1 * 2 - v1;
    if (!(p < 0x1001)) p = 0x1000;
    else if (!(-0x1001 < p)) p = -0x1000;
    set16(r.last, 4, u16(p));
}

// The fields by their bit in the change mask, in the order they are coded: offset in the frame, size.
struct Field {
    u32 bit, off, size; // size 1 unsigned byte, 0 signed byte, 2 halfword
};
constexpr Field kFields[] = {{0x100, 0, 1}, {0x80, 1, 1}, {0x10, 2, 0}, {0x02, 4, 2}, {0x01, 6, 2},
                             {0x04, 8, 2},  {0x08, 0xA, 2}, {0x40, 0xC, 2}, {0x20, 0xE, 2}};

} // namespace

void ControlRecorder::bind(u32 off, u32 cap) { capacity = cap, base = off + 8, bound = true, block = off; }

void ControlRecorder::rewind() {
    ptr = base, bits = 0, cur = 0;
    written = false, done = false, count = 0, run = 0;
    last = {};
    steer_before = 0;
    if (!bound) done = true;
}

void ControlRecorder::write(std::span<u8> mem, const RawPad& f) {
    if (!done) {
        predict(*this);
        written = true;
        u32 mask = 0;
        for (const Field& k : kFields) {
            const bool changed = k.size == 2 ? field16(last, k.off) != field16(f, k.off)
                                 : k.size == 0 ? s8(last[k.off]) != s8(f[k.off])
                                               : last[k.off] != f[k.off];
            if (changed) mask |= k.bit;
        }
        if (++count != 1 && mask == 0) {
            ++run;
        } else {
            if (run != 0) put_count(mem, *this, run);
            put_count(mem, *this, mask + 1);
            for (const Field& k : kFields) {
                if (!(mask & k.bit)) continue;
                s32 d;
                if (k.size == 2) d = s32(field16(f, k.off)) - s32(field16(last, k.off));
                else if (k.size == 0) d = s32(s8(f[k.off])) - s32(s8(last[k.off]));
                else d = s32(f[k.off]) - s32(last[k.off]);
                put_diff(mem, *this, d);
                last[k.off] = f[k.off];
                if (k.size == 2) last[k.off + 1] = f[k.off + 1];
            }
            if (!(ptr - base + 0x40 < capacity)) close(mem);
            run = 1;
        }
    }
    last = f;
}

void ControlRecorder::close(std::span<u8> mem) {
    if (!done && written) {
        put_count(mem, *this, run);
        put_count(mem, *this, 1);
        auto w32 = [&](u32 o, u32 v) {
            for (u32 k = 0; k < 4; ++k)
                if (o + k < mem.size()) mem[o + k] = u8(v >> (8 * k));
        };
        w32(block, count);
        w32(block + 4, ptr - base + 8);
    }
    done = true;
}

u32 ControlRecorder::header_frames(std::span<const u8> mem) const {
    u32 v = 0;
    for (u32 k = 0; k < 4; ++k) v |= u32(block + k < mem.size() ? mem[block + k] : 0) << (8 * k);
    return v;
}

RawPad ControlRecorder::read(std::span<const u8> mem) {
    if (!done) {
        if (!(++count < header_frames(mem))) {
            last = {}, done = true;
        } else {
            predict(*this);
            if (run != 0) {
                --run;
            } else {
                const u32 mask = u32(get_count(mem, *this) - 1);
                for (const Field& k : kFields) {
                    if (!(mask & k.bit)) continue;
                    const s32 d = get_diff(mem, *this);
                    if (k.size == 2) set16(last, k.off, u16(u32(field16(last, k.off) & 0xFFFF) + u32(d)));
                    else last[k.off] = u8(u32(last[k.off]) + u32(d));
                }
                run = u32(get_count(mem, *this) - 1);
            }
        }
    }
    return last;
}

void ControlRecorder::seek(std::span<const u8> mem, u32 n, bool keep) {
    if (!keep) rewind();
    for (; n > 0; --n) read(mem);
}

void ControlManager::bind_pool(int p) {
    pool = p;
    if (mode == 0) rec.bind(0, kRecordPool - 8);
    else bind_store(0);
}

bool later_store(const Behaviour& b) { return !gt3_program(b.rule(Rule::RecordStore)); }

void ControlManager::bind_store(u32 s) {
    store = s * kRecordStore;
    rec.bind(store + store_snapshot(later), store_block(later));
}

void ControlManager::restart() {
    if (mode == 1) index = used = 0, bind_store(0);
    rec.rewind();
}

} // namespace gt3r::sim::exact
