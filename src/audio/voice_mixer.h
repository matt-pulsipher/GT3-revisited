#pragma once

// Software model of the PS2 SPU2 voice section, used for engine notes, tire
// and collision effects, UI sounds and the sequenced menu music. Voices play
// PS-ADPCM sample data in place (loop/end flags in the data are honoured like
// the hardware does) and take the values the game sends to its voices:
//   pitch   game units, 0x1000 = 44.1 kHz. Converted to SPU2 pitch
//           (0x1000 = 48 kHz) with pitch * 0xEB3 >> 12 exactly like the
//           game's voice-command packer, then limited to 0x4000.
//   volume  0x3FFF = full scale (the SPU2's 15-bit "volume / 2" mode)
//   ADSR1/ADSR2 envelope words
//   reverb  effect send (VMIXEL/VMIXER), from INST region flags bit 0
// and are mixed to 48 kHz stereo s16 with the SPU2's 4-point Gaussian
// interpolation, hardware envelope timing and the reverb unit.
//
// Thread safety: every public method may be called from any thread. The
// game thread issues key-on/off and parameter changes; the audio thread
// calls mix(). One mutex guards the voice table; calls are short. mix()
// renders each block from a copy of the table without the lock (a block can
// take milliseconds on a phone), then takes the rendered voices back and
// replays, in order, the changes made meanwhile: they land after the block,
// as they would have had they waited for it (a key-on meanwhile picks its
// voice from the table as it was before the block). The tick hook runs on the
// mixing thread without the lock held, so it may call any method; it is how
// the sequencer gets sample-exact timing.

#include "audio/spu_reverb.h"
#include "base/bytes.h"
#include "formats/ps_adpcm.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace gt3r {

// The output bus a voice plays on. Each bus has its own gain (the player's
// volume settings): Music carries the sequenced menu music, Effects every other
// voice (engine notes, effects, ambience), so the effects volume never scales
// the music (sound §2.4).
enum class VoiceBus : u8 { Effects, Music };

// Opaque voice handle. Stale handles (voice finished or reused) are ignored.
struct VoiceId {
    u32 value = 0;
    bool valid() const { return value != 0; }
    bool operator==(const VoiceId&) const = default;
};

struct VoiceParams {
    std::shared_ptr<const Bytes> data; // ADPCM bytes; kept alive while playing
    u32 start = 0;                     // byte offset of the first frame
    u16 pitch = 0x1000;                // game units: 0x1000 = 44.1 kHz
    s16 vol_l = 0x3FFF, vol_r = 0x3FFF;
    u16 adsr1 = 0x000F; // instant attack, full sustain
    u16 adsr2 = 0x0003; // fast linear release
    bool reverb = false; // send to the reverb unit
    u8 priority = 0;     // may steal voices whose priority value is >= this
    VoiceBus bus = VoiceBus::Effects;
};

class VoiceMixer {
public:
    // The SPU2 has 2 cores x 24 voices; the game's allocator hands out only
    // the first 46 (what the last two are kept for is not known). key_on takes
    // one of those 46; the last two (kSpareVoices) are keyed only by
    // key_on_spare, for an app that keeps two voices of its own (a stream).
    static constexpr int kVoices = 46;
    static constexpr int kSpareVoices = 2;
    static constexpr int kAllVoices = kVoices + kSpareVoices;
    static constexpr u32 kRate = 48000;
    // Output trim of the mix (headroom: full-level voices sum past 0 dBFS as on the SPU2).
    // The game streams its music through SPU2 voices as well, so the music and the
    // effects meet at equal weight; AudioEngine trims the music stream by the same
    // factor to keep that balance (sound §2.4).
    static constexpr float kOutputTrim = 0.5f;

    enum class Interpolation : u8 { Gaussian, Cubic };

    VoiceMixer();

    VoiceId key_on(const VoiceParams& p);
    // Keys spare voice `spare` (0 or 1: the SPU2's voices 46 and 47), cutting what
    // it played; key_on never hands these out, so nothing else takes them.
    VoiceId key_on_spare(int spare, const VoiceParams& p);
    void key_off(VoiceId v); // enters the release phase
    void stop(VoiceId v);    // silences immediately
    void stop_all();
    void set_pitch(VoiceId v, u16 pitch); // game units
    void set_volume(VoiceId v, s16 l, s16 r);
    // Volume changes are eased to their level, full scale in about 5 ms (256
    // samples), by default. Off: a change lands on the next sample, as the
    // SPU2 takes a voice's level register, for an app whose own rules already
    // ease what they need.
    void set_volume_ramp(bool on) { volume_ramp_ = on; }
    bool volume_ramp() const { return volume_ramp_; }
    // Stealing priority of a playing voice (VoiceParams::priority); the game re-sends a
    // held effect's priority every frame as its loudness changes.
    void set_priority(VoiceId v, u8 priority);
    bool playing(VoiceId v) const;
    bool releasing(VoiceId v) const;
    int active_voices() const;
    // Envelope level 0..1 (0 when not playing), for meters.
    float envelope(VoiceId v) const;

    // Game pitch to SPU2 pitch (0x1000 = 48 kHz), limited to 0x4000.
    static u16 hardware_pitch(u16 game_pitch);

    void set_master_volume(float v);
    float master_volume() const { return master_; }
    // Gain of one bus, 0..1 (default 1), applied to its voices' dry and reverb
    // sends alike; changes apply from the next rendered sample.
    void set_bus_gain(VoiceBus bus, float gain);
    float bus_gain(VoiceBus bus) const;
    // GT3's surround cue for positional sounds (see surround_cue.h); read by
    // EngineSound / EngineUnit. Off by default.
    void set_surround_cue(bool on) { surround_cue_ = on; }
    bool surround_cue() const { return surround_cue_; }

    void set_interpolation(Interpolation i);
    Interpolation interpolation() const { return interp_; }

    // Reverb: libsd effect mode (SpuReverb::Mode) and return volume EVOL
    // (0x7FFF = 1.0). Off by default; AudioEngine selects the game's
    // boot setting (studio C, 0x0FFF).
    void set_reverb(int mode, s16 depth_l, s16 depth_r);
    void set_reverb_depth(s16 depth_l, s16 depth_r);
    int reverb_mode() const;
    s16 reverb_depth() const;

    // Calls `hook` from mix() every `samples_per_tick` output frames, before
    // rendering them (so changes it makes land exactly on that frame).
    // Pass an empty function to remove it.
    void set_tick_hook(std::function<void()> hook, u32 samples_per_tick);

    // Renders `frames` stereo frames (interleaved L,R) at kRate.
    void mix(s16* out, size_t frames);

private:
    enum class Phase : u8 { Off, Attack, Decay, Sustain, Release };

    struct Voice {
        u32 gen = 0;
        Phase phase = Phase::Off;
        std::shared_ptr<const Bytes> data;
        u32 addr = 0, loop_addr = 0;
        u8 frame_flags = 0;
        bool ended = false; // last frame had the end flag and no repeat
        bool reverb = false;
        u8 priority = 0;
        u8 bus = 0;
        PsAdpcmDecoder dec;
        s16 block[28] = {};
        int block_pos = 28;
        s32 hist[4] = {}; // oldest first, for interpolation
        u32 counter = 0;  // 12-bit fractional position
        u16 hw_pitch = 0x1000;
        s32 vol_l = 0, vol_r = 0;       // current (ramped)
        s32 target_l = 0, target_r = 0; // requested
        u16 adsr1 = 0, adsr2 = 0;
        s32 env = 0;
        u32 env_counter = 0;
        u64 age = 0;
    };
    // A change to the voice table or the reverb, kept while a block renders.
    struct Change {
        enum class Kind : u8 { KeyOn, KeyOff, Stop, StopAll, Pitch, Volume, Priority, Reverb, ReverbDepth };
        Change(Kind k, VoiceId v = {}, int n = 0, s16 left = 0, s16 right = 0) : kind(k), id(v), value(n), l(left), r(right) {}
        Kind kind;
        VoiceId id;
        int value;   // pitch, priority, reverb mode
        s16 l, r;    // volume, reverb depth
        Voice voice; // KeyOn: the voice as keyed on (at id's index)
    };
    // What a block renders with besides the voices, as it was when the block began.
    struct BlockParams {
        s32 master = 0;
        bool gauss = true;
        std::array<s32, 2> bus_gain{};
        bool ramp = true;
    };

    VoiceId key_on_at(int index, const VoiceParams& p); // (mutex_ held)
    Voice* find(VoiceId v);
    const Voice* find(VoiceId v) const;
    void change(Change c); // (mutex_ held)
    void apply(const Change& c);
    void render_block(s16* out, size_t frames);
    void render(s16* out, size_t frames, const BlockParams& p);
    static s32 next_sample(Voice& v);
    static void step_envelope(Voice& v);
    static bool env_tick(Voice& v, int rate, int rate_mask, bool exponential, bool decrease);

    mutable std::mutex mutex_;
    std::array<Voice, kAllVoices> voices_;
    std::array<Voice, kAllVoices> block_;   // the block being rendered (mixing thread, without mutex_)
    bool rendering_ = false;             // a block renders: block_ and reverb_ are the mixing thread's
    std::vector<Change> changes_;        // made while it renders, replayed after it
    std::mutex mix_mutex_;               // one mix() at a time
    u64 clock_ = 0;
    float master_ = kOutputTrim;
    std::array<s32, 2> bus_gain_{1 << 16, 1 << 16}; // per VoiceBus, 16.16 fixed point
    Interpolation interp_ = Interpolation::Gaussian;
    std::atomic<bool> surround_cue_{false};
    std::atomic<bool> volume_ramp_{true};
    SpuReverb reverb_;
    int reverb_mode_ = SpuReverb::Off; // as last set (reverb_ takes it after a rendering block)
    s16 reverb_depth_ = 0;

    std::mutex hook_mutex_; // guards hook_ and the tick counters
    std::function<void()> hook_;
    u32 samples_per_tick_ = 0, until_tick_ = 0;
};

} // namespace gt3r
