// GT3's pad frame recorder (AutomobileControlRecord::Recorder and its Manager) against sim::exact::ControlRecorder
// (src/sim/exact/control_record.h): the same frames recorded by both into the same record memory, every byte of the memory
// and the recorder's words compared after every frame and at the close, then the recording played back and sought.

#include "harness.h"
#include "program_map.h"
#include "race_hits.h"

#include "sim/exact/control_record.h"

#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::diff;
namespace ex = gt3r::sim::exact;
namespace rh = gt3r::diff::race_hits;

namespace {

constexpr u32 kManagerCtor = 0x001F4ED0, kBindPool = 0x001F4FB0, kRewind = 0x001F44C8, kWrite = 0x001F4998,
              kClose = 0x001F4C00, kRead = 0x001F4660, kSeek = 0x001F47D8, kRestart = 0x001F52B0;
constexpr u32 kPoolBase = 0x003529B0, kPoolSize = 0x003529B4; // the record memory's pools (0x1F4D80)
constexpr u32 kClassStarts = 0x002D8EE0; // the playback's table of the difference classes' starts

// The recorder's words in the original's memory (recorder r, pool at `pool`) against ours.
std::string recorder_difference(Ee& ee, u32 r, u32 pool, const ex::ControlRecorder& o) {
    auto cmp = [&](const char* what, u32 a, u32 b) { return a == b ? std::string() : rh::fmt("%s 0x%X vs 0x%X", what, b, a); };
    std::string d;
    auto add = [&](std::string s) {
        if (d.empty()) d = std::move(s);
    };
    add(cmp("stream start", ee.r32(r + 0), pool + o.base));
    add(cmp("stream at", ee.r32(r + 4), pool + o.ptr));
    add(cmp("byte", ee.r8(r + 8), o.cur));
    add(cmp("bits", ee.r32(r + 0xC), o.bits));
    add(cmp("steer before", ee.r32(r + 0x14), u32(o.steer_before)));
    for (u32 k = 0; k < 16; ++k) add(cmp(rh::fmt("last +0x%X", k).c_str(), ee.r8(r + 0x18 + k), o.last[k]));
    add(cmp("done", ee.r32(r + 0x28), o.done ? 1 : 0));
    add(cmp("written", ee.r32(r + 0x2C), o.written ? 1 : 0));
    add(cmp("count", ee.r32(r + 0x30), o.count));
    add(cmp("run", ee.r32(r + 0x34), o.run));
    add(cmp("block", ee.r32(r + 0x38), o.bound ? pool + o.block : 0));
    add(cmp("capacity", ee.r32(r + 0x3C), o.capacity));
    return d;
}

std::string memory_difference(Ee& ee, u32 pool, const std::vector<u8>& mem) {
    std::vector<u8> theirs(mem.size());
    ee.read_bytes(pool, theirs.data(), u32(theirs.size()));
    for (size_t k = 0; k < mem.size(); ++k)
        if (theirs[k] != mem[k]) return rh::fmt("memory +0x%zX: 0x%02X vs 0x%02X", k, mem[k], theirs[k]);
    return {};
}

struct Sequence {
    std::string name;
    std::vector<ex::RawPad> frames;
};

ex::RawPad raw_of(const u8* f) {
    ex::RawPad r;
    std::memcpy(r.data(), f, 16);
    return r;
}
void set16(ex::RawPad& r, u32 o, u32 v) { r[o] = u8(v), r[o + 1] = u8(v >> 8); }

std::vector<Sequence> sequences(Rng& rng) {
    std::vector<Sequence> out;
    { // the pad races' scripted frames (race_tick_exact's pad scenarios)
        Sequence s{"pad race frames", {}};
        for (int k = 0; k < 2400; ++k) {
            u8 f[16] = {};
            rh::pad_race_frame(k, f);
            s.frames.push_back(raw_of(f));
        }
        out.push_back(std::move(s));
    }
    { // a stick driver: steering smooth and noisy, analog pedals held and changed, shifts, the kind and flags now and then
        Sequence s{"stick driver", {}};
        double a = 0, v = 0;
        ex::RawPad f{};
        f[0] = 0x1F, f[1] = 2;
        for (int k = 0; k < 24000; ++k) {
            v += rng.uniform(-30.0f, 30.0f), v *= 0.95, a += v;
            if (a > 4096) a = 4096, v = 0;
            if (a < -4096) a = -4096, v = 0;
            set16(f, 4, u32(s32(a)));
            if (rng.chance(0.08)) set16(f, 8, u32(rng.range(0, 256)));
            if (rng.chance(0.05)) set16(f, 0xA, u32(rng.chance(0.5) ? 0 : rng.range(0, 256)));
            if (rng.chance(0.01)) set16(f, 0xE, u32(rng.chance(0.5) ? 0 : 256));
            if (rng.chance(0.01)) set16(f, 0xC, u32(rng.chance(0.7) ? 0 : 256));
            f[2] = rng.chance(0.01) ? u8(rng.chance(0.5) ? 1 : 0xFF) : 0;
            if (rng.chance(0.002)) f[1] = u8(rng.range(0, 4));
            if (rng.chance(0.002)) f[0] = u8(rng.range(0, 0x1F));
            s.frames.push_back(f);
        }
        out.push_back(std::move(s));
    }
    { // a digital pad: steering +-2, buttons on and off
        Sequence s{"digital pad", {}};
        ex::RawPad f{};
        for (int k = 0; k < 4000; ++k) {
            if (rng.chance(0.05)) set16(f, 4, u32(rng.chance(0.4) ? 2 : rng.chance(0.6) ? 0xFFFE : 0));
            if (rng.chance(0.03)) set16(f, 8, rng.chance(0.7) ? 1 : 0);
            if (rng.chance(0.03)) set16(f, 0xA, rng.chance(0.3) ? 1 : 0);
            f[2] = rng.chance(0.01) ? u8(rng.chance(0.5) ? 1 : 0xFF) : 0;
            s.frames.push_back(f);
        }
        out.push_back(std::move(s));
    }
    { // every byte random every frame (the store's recording fills and closes itself)
        Sequence s{"noise", {}};
        for (int k = 0; k < 3000; ++k) {
            ex::RawPad f;
            for (u8& b : f) b = u8(rng.u32_());
            s.frames.push_back(f);
        }
        out.push_back(std::move(s));
    }
    { // the largest changes (classes past the 3-bit field), long holds, the third and sixth bytes
        Sequence s{"extremes", {}};
        ex::RawPad f{};
        for (int k = 0; k < 3000; ++k) {
            const int p = k % 300;
            if (p == 0) set16(f, 4, 0x1000), set16(f, 8, 0xFFFF);
            if (p == 1) set16(f, 4, 0xF000), set16(f, 8, 0), set16(f, 0xA, 0x8000);
            if (p == 2) set16(f, 6, u32(rng.u32_())), f[3] = u8(rng.u32_()), set16(f, 0xC, 0x7FFF), set16(f, 0xE, 0x8001);
            if (p == 150) set16(f, 4, u32(rng.range(-0x8000, 0x7FFF)));
            s.frames.push_back(f);
        }
        out.push_back(std::move(s));
    }
    { // nothing at all
        Sequence s{"empty frames", std::vector<ex::RawPad>(5000)};
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace

DIFF_TEST_ALL(control_record_exact, "AutomobileControlRecord's recording, closing, playback and seek (0x1F4998, 0x1F4C00, "
                                    "0x1F4660, 0x1F47D8 on a Manager 0x1F4ED0 / 0x1F4FB0) vs sim::exact::ControlRecorder") {
    Ee& ee = ctx.ee;
    const std::vector<Sequence> seqs = sequences(ctx.rng);
    long frames = 0, reads = 0, seeks = 0, closed_full = 0, bad = 0;
    std::string first;
    auto fail = [&](const std::string& what) {
        if (++bad == 1) first = what;
    };
    for (const Sequence& s : seqs)
        for (u32 mode : {0u, 2u}) {
            const std::string where = s.name + (mode == 0 ? " (the whole pool)" : " (a store)");
            ee.reset(), ee.clear_hle(), ee.hle_libc_defaults();
            const u32 pool = ee.alloc(ex::kRecordPool, 64), mgr = ee.alloc(0x60, 16), frame = ee.alloc(16, 16), out = ee.alloc(16, 16);
            std::vector<u8> mem(ex::kRecordPool);
            for (u8& b : mem) b = u8(ctx.rng.u32_()); // stale memory
            ee.write_bytes(pool, ByteSpan(mem.data(), mem.size()));
            ee.w32(pa(kPoolBase), pool), ee.w32(pa(kPoolSize), ex::kRecordPool);
            ee.call(pa(kManagerCtor), {Arg::ptr(mgr)});
            ee.w32(mgr + 4, mode);
            ee.call(pa(kBindPool), {Arg::ptr(mgr), Arg::ptr(pool)});
            ee.call(pa(kRestart), {Arg::ptr(mgr)});
            ex::ControlManager m;
            m.mode = mode;
            m.later = ex::later_store(disc_behaviour()); // (Rule::RecordStore: the later builds' store layout)
            m.bind_pool(0);
            m.restart();
            const u32 r = mgr + 8;
            std::string d = recorder_difference(ee, r, pool, m.rec);
            for (size_t k = 0; k < s.frames.size() && d.empty(); ++k) {
                ee.write_bytes(frame, ByteSpan(s.frames[k].data(), 16));
                ee.call(pa(kWrite), {Arg::ptr(r), Arg::ptr(frame)});
                const bool was_done = m.rec.done;
                m.rec.write(mem, s.frames[k]);
                if (!was_done && m.rec.done) ++closed_full;
                ++frames, ctx.sample();
                d = recorder_difference(ee, r, pool, m.rec);
                if (!d.empty()) d = rh::fmt("frame %zu: ", k) + d;
            }
            if (d.empty()) {
                ee.call(pa(kClose), {Arg::ptr(r)});
                m.rec.close(mem);
                d = recorder_difference(ee, r, pool, m.rec);
                if (d.empty()) d = memory_difference(ee, pool, mem);
                if (!d.empty()) d = "close: " + d;
            }
            // played back from the start, past the end
            if (d.empty()) {
                ee.call(pa(kRewind), {Arg::ptr(r)});
                m.rec.rewind();
                for (size_t k = 0; k < s.frames.size() + 3 && d.empty(); ++k) {
                    ee.call(pa(kRead), {Arg::ptr(r), Arg::ptr(out)});
                    const ex::RawPad ours = m.rec.read(mem);
                    u8 theirs[16];
                    ee.read_bytes(out, theirs, 16);
                    ++reads, ctx.sample();
                    if (std::memcmp(theirs, ours.data(), 16) != 0) d = rh::fmt("read %zu: the frame", k);
                    else d = recorder_difference(ee, r, pool, m.rec);
                    if (!d.empty() && d.rfind("read", 0) != 0) d = rh::fmt("read %zu: ", k) + d;
                }
            }
            // sought from the start to random frames, from where it is too, then the next frame
            for (int j = 0; j < 12 && d.empty(); ++j) {
                const u32 n = u32(ctx.rng.range(0, s32(s.frames.size()) + 2));
                const bool keep = j % 3 == 2;
                ee.call(pa(kSeek), {Arg::ptr(r), Arg::integer(n), Arg::integer(keep ? 1 : 0)});
                m.rec.seek(mem, n, keep);
                ee.call(pa(kRead), {Arg::ptr(r), Arg::ptr(out)});
                const ex::RawPad ours = m.rec.read(mem);
                u8 theirs[16];
                ee.read_bytes(out, theirs, 16);
                ++seeks, ctx.sample();
                if (std::memcmp(theirs, ours.data(), 16) != 0) d = rh::fmt("seek %u%s: the next frame", n, keep ? " on" : "");
                else if (std::string dr = recorder_difference(ee, r, pool, m.rec); !dr.empty()) d = rh::fmt("seek %u: ", n) + dr;
            }
            if (!d.empty()) fail(where + ": " + d);
        }
    // the classes' starts the playback adds (0x2D8EE0): the coding's
    {
        s32 start = 0;
        for (u32 c = 0; c < 8; ++c) {
            if (c > 0) start += s32(1u << (2 * c - 1));
            if (ee.r32(pa(kClassStarts) + 4 * c) != u32(start)) fail(rh::fmt("class %u starts at %u, ours %d", c, ee.r32(pa(kClassStarts) + 4 * c), start));
            ctx.sample();
        }
    }
    ctx.note(rh::fmt("%zu sequences in both bindings: %ld frames recorded (%ld recordings closed themselves full), %ld played, "
                     "%ld seeks; %ld differing",
                     seqs.size(), frames, closed_full, reads, seeks, bad));
    ctx.check(bad == 0, first);
}
