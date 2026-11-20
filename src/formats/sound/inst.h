#pragma once

// "INST" instrument bank (sound/*.ins, music/music.ins): SPU samples plus
// key-mapped regions grouped into programs.
//
//   0x00 "INST"
//   0x04 u32 0           relocation base (written at load)
//   0x08 u32 header size
//   0x0C u32 0           SPU address (written at load)
//   0x10 u32 data size
//   0x14 u32 data offset
//   0x18 u32 region count,  0x1C u32 region table offset (0x28)
//   0x20 u32 program count, 0x24 u32 program offset table
//   region, 24 bytes:
//     0x00 u32 sample address, 8-byte units from the data start
//     0x04 u16 ?            (0)
//     0x06 s16 volume       0..0x4000
//     0x08 u16 center       center_note << 8 | fine (1/256 semitone); the
//                           sample plays at 44.1 kHz (pitch 0x1000) when
//                           note == center
//     0x0A u16 duration     nominal one-shot length in 1/960 s (0 = looped);
//                           only used for voice bookkeeping by the game
//     0x0C u8  priority     voice-stealing priority (0 in every bank)
//     0x0D u8  pan          0 left, 0x40 centre, 0x7F right
//     0x0E u8  flags        bit 0: send to the reverb (SPU2 VMIXEL/VMIXER)
//     0x0F u8  0
//     0x10 u8  key_lo, 0x11 u8 key_hi  key range (the SE players key at
//                           key_lo)
//     0x12 u8  bend_down, 0x13 u8 bend_up  pitch-bend range in semitones
//     0x14 u16 adsr1, 0x16 u16 adsr2   SPU2 envelope registers
//   program: u32 file offset -> { u8 count; u8 pan; s16 volume (Q14);
//                                 u8 region[count] }

#include "formats/sound/spu.h"

#include <memory>
#include <vector>

namespace gt3r::sound {

struct InstRegion {
    u32 address = 0; // byte offset into InstBank::adpcm
    s16 volume = 0;
    u16 center = 0;
    u16 duration = 0;
    u8 priority = 0;
    u8 pan = 0x40;
    u8 flags = 0;
    u8 key_lo = 0, key_hi = 0;
    u8 bend_down = 0, bend_up = 0;
    u16 adsr1 = 0, adsr2 = 0;

    // Game pitch units (0x1000 = 44.1 kHz), as VoiceParams::pitch expects.
    u16 pitch_for_key(int key) const { return note_to_pitch(key << 8, center); }
    u16 pitch() const { return pitch_for_key(key_lo); }
    bool reverb() const { return flags & 1; }
};

struct InstProgram {
    u8 pan = 0x40;   // added to the channel and region pans (0x40 = neutral)
    s16 volume = 0;  // Q14
    std::vector<u8> regions;
};

struct InstBank {
    std::vector<InstRegion> regions;
    std::vector<InstProgram> programs;
    std::shared_ptr<const Bytes> adpcm;

    static InstBank parse(ByteSpan file);
    SpuSample decode_region(size_t i) const { return decode_spu_sample(*adpcm, regions.at(i).address); }
};

// "GTSE" sound effect index (sound/se.inf): name -> bank + region range.
//   0x00 "GTSE", 0x04 u32 1, 0x08 u32 0, 0x0C u32 count
//   0x10 { u32 name_offset; u32 value }[count], then NUL-terminated names
//   value = bank << 24 | first_region << 12 | last_region
// Banks: 0 sound/gt3sys.ins, 1 sound/gt3se01.ins, 2 sound/gt3se02.ins; later discs of this
// engine add 3 sound/gt3pod.ins (the "pod_*" effects), which GT3's se.inf never names.
// All regions in the range are keyed on together (stereo pairs).
struct SoundEffect {
    std::string name;
    u8 bank = 0;
    u16 first = 0, last = 0;
};

std::vector<SoundEffect> parse_se_index(ByteSpan file);
const char* se_bank_path(u8 bank); // nullptr if unknown

} // namespace gt3r::sound
