#pragma once

// GT3's pad frame recordings (pad recording): what its replays, its ghosts and the free run's lap
// recordings keep of a car's driving, and how they play it back.
//
// A recorder (pad recording's recorder) turns the frames a car's input device reads (PadFrame, the pad read)
// into a bit stream (bit stream) in a block of memory: +0 the frames recorded, +4 the stream's bytes
// + 8 (both written when the recording is closed), +8 the stream. Each frame is coded against the one before (the steering
// against its linear prediction): the fields that changed, each change as a signed variable-length number, and runs of
// unchanged frames. A recording closes itself when its stream comes within 0x40 bytes of the block's capacity; its playback
// gives every frame but the last (the count check comes first), then empty frames.
//
// A manager (pad recording's manager) binds a recorder to a pool of the game's record memory: in mode 0
// the whole pool (0x20000 bytes less the header), in modes 1 and 2 one of its four stores of 0x8000 bytes (a 0x1B00-byte car
// snapshot, then a block of 0x64F8 bytes).

#include "sim/exact/tick.h"

#include <array>
#include <span>
#include <vector>

namespace gt3r::sim::exact {

// A pad frame as the recorder keeps it: the 16 bytes of the device's frame (PadFrame's fields; +0x03 and +0x06 are kept too,
// though nothing the pad read writes sets them).
using RawPad = std::array<u8, 16>;
RawPad raw_pad(const PadFrame& f);
PadFrame pad_of(const RawPad& r);

// The game's record memory: pools of 0x20000 bytes, each four stores of 0x8000.
constexpr u32 kRecordPool = 0x20000, kRecordStore = 0x8000;
constexpr u32 kStoreSnapshot = 0x1B00, kStoreBlock = 0x64F8; // a store: the snapshot, then the recording's block
// (Rule::RecordStore: the later builds' store, Concept 2001 Tokyo, holds a snapshot 4 bytes longer, their car
// record's extra word: the block from 0x1B04, 0x64F4 bytes. ControlManager::later takes it; the races' solo recorders keep
// GT3's layout on every disc, their snapshots being ours: a lap's recording closes itself 4 bytes later than a later
// build's would.)
constexpr u32 store_snapshot(bool later) { return later ? 0x1B04 : kStoreSnapshot; }
constexpr u32 store_block(bool later) { return later ? 0x64F4 : kStoreBlock; }
bool later_store(const Behaviour& b); // the later builds' store layout under behaviour b

// Pad recording's recorder on a block of `mem` (offsets into it; the caller keeps the memory).
struct ControlRecorder {
    // Bit stream (+0x00): the stream's start, the byte it is at, the byte being filled or read and its bits (written: the
    // bits used; read: the bits left).
    u32 base = 0, ptr = 0;
    u8 cur = 0;
    u32 bits = 0;
    s32 steer_before = 0;   // +0x14 the steering of the frame before the last (the prediction's)
    RawPad last{};          // +0x18 the last frame (its steering replaced by the prediction while coding the next)
    bool done = false;      // +0x28 closed (recording) or past its end (playback)
    bool written = false;   // +0x2C a frame was recorded
    u32 count = 0;          // +0x30 frames recorded or played
    u32 run = 0;            // +0x34 recording: frames since the last change; playback: unchanged frames still to give
    bool bound = false;     // +0x38 a block (its offset `block`)
    u32 block = 0;
    u32 capacity = 0;       // +0x3C

    // Bound to the block at `off` (its stream at + 8) of this capacity.
    void bind(u32 off, u32 cap);
    // Back to the stream's start, nothing recorded or played (done when there is no block).
    void rewind();
    // Frame f recorded (nothing once closed).
    void write(std::span<u8> mem, const RawPad& f);
    // The recording closed: the last run and the end, the header (frames, bytes + 8).
    void close(std::span<u8> mem);
    // The next frame played.
    RawPad read(std::span<const u8> mem);
    // n frames read from the start (`keep`: from where it is).
    void seek(std::span<const u8> mem, u32 n, bool keep = false);
    // The block's header (written by close): the frames and the bytes.
    u32 header_frames(std::span<const u8> mem) const;
};

// Pad recording's manager: its mode (+0x04: 0 the whole pool, 1 the stores in turn, 2 one store), the pool (+0x4C)
// and store (+0x50) it is on, and its recorder (+0x08).
struct ControlManager {
    u32 mode = 0;
    bool playing = false; // +0x00 (race input device's play mode: not 0)
    int pool = -1;        // +0x4C (-1 none)
    u32 store = 0;        // +0x50: the store's offset in its pool
    u32 index = 0, used = 0; // +0x54 / +0x58 (mode 1)
    ControlRecorder rec;
    bool later = false; // the later builds' store layout (store_snapshot)

    // On pool p (mode 0: the recorder on the whole pool, else store 0 of it).
    void bind_pool(int p);
    // On store s of its pool (the recorder on the store's block).
    void bind_store(u32 s);
    // Restarted (mode 1 from store 0), its recorder rewound.
    void restart();
    // The store's offset in its pool (its snapshot), none in mode 0.
    bool has_store() const { return mode != 0; }
};

} // namespace gt3r::sim::exact
