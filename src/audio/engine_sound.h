#pragma once

// Engine note model, reproducing how GT3 drives its ENGN banks (see
// docs/formats/sound.md "Engine sound model"):
//
// A car plays two banks at once:
//   engine  sound/engine/<id>             mechanical/intake note
//   exhaust sound/<variant>/<id>          variant = normal0..3 | turbo0..3
//                                         (muffler level + 4 if a turbo kit)
// Each bank is run by a two-voice "layer player" ticked at the game frame
// rate (60 Hz):
//   - rpm is clamped to >= 200;
//   - if rpm lies within a layer's [lo, hi] that layer plays alone, else the
//     two neighbouring layers play and crossfade with an equal-power (sine)
//     curve from the lower layer's hi to the upper layer's lo;
//   - voice pitch = layer.pitch * rpm / layer.base_rpm * pitch_scale/0x1000
//     (pitch_scale carries Doppler), limited per tick to [prev/2, prev*2];
//   - voice volume = curve * layer.volume * bank volume (L/R), limited per
//     tick to +-0x1000 change;
//   - voices key on at the layer sample start with ADSR 0x000F/0x0003.
// Bank volumes come from per-car 0..127 levels smoothed every tick
// (v = (v + target) / 2):
//   exhaust target = min(127, (1 + 3 * throttle) * 32 * K1)
//   engine  target = min(127, (|throttle - 0.375| + 0.375) * 127 * k * K2)
//                    k = 1, or 0.5 while a car state flag is clear
//                    (presumably "drive engaged"; unconfirmed)
//                    K1 = K2 = 0.13 (the game's run-time values)
// and are scaled by level/64 and a per-camera channel gain table.
// Stereo placement: constant-power pan from the source's lateral offset
// over its horizontal distance in listener space, p = x/d in -1..1:
// angle = clamp((p + 1) * pi/4, 0, pi/2), left = cos, right = sin.
// The game's rear phase inversion (surround_cue.h) is applied when the
// mixer's surround cue option is on and the position is given with
// set_position(). Approximation: the throttle input field is inferred.

#include "audio/surround_cue.h"
#include "audio/voice_mixer.h"
#include "formats/sound/engn.h"

#include <memory>
#include <string>

namespace gt3r {

class Vfs;

class EngineSound {
public:
    // Per-camera channel gains (engine, exhaust) from the game's table.
    enum class View { OnboardOwn = 0, OnboardOther, ChaseOwn, ChaseOther, Replay, Count };
    static const char* view_name(View v);

    struct BankState {
        int layer[2] = {-1, -1};  // layers on the two voices (-1 = idle)
        float gain[2] = {};       // their crossfade gains 0..1
        u16 pitch[2] = {};        // game pitch per voice (0x1000 = 44.1 kHz)
    };
    struct State {
        BankState bank[2]; // 0 engine, 1 exhaust
        int level[2] = {}; // smoothed bank levels (0..kMaxLevel)
    };

    EngineSound() = default;
    ~EngineSound();

    // Loads sound/engine/<id> and sound/<variant>/<id>. Variant 0..7. `dir`: a host folder holding the
    // banks in the same layout (engine/<id>, normal0/<id>...; an exported mod car's own, ModCar::sound_dir)
    // to read them from instead.
    bool load(Vfs& vfs, int sound_id, int variant, std::string* error = nullptr, const std::string& dir = {});
    void set_banks(std::shared_ptr<const sound::EngineBank> engine, std::shared_ptr<const sound::EngineBank> exhaust);
    bool loaded() const { return engine_ || exhaust_; }
    const sound::EngineBank* engine_bank() const { return engine_.get(); }
    const sound::EngineBank* exhaust_bank() const { return exhaust_.get(); }

    void set_view(View v) { view_ = v; }
    View view() const { return view_; }
    void set_volume(float v) { volume_ = v; }
    // Stereo position: the source's lateral offset over its horizontal
    // distance in listener space (-1 = left, 0 = centre, +1 = right).
    void set_pan(float x_over_d) { pan_ = x_over_d; }
    // Full listener-space placement (x right, z forward, metres): sets the
    // pan and the front/behind side used by the surround cue.
    void set_position(float x, float z);
    float pan() const { return pan_; }
    // x/d for a source at (x, z) in listener space (x right, z forward);
    // distances under 0.25 count as 0.25, like the game.
    static float pan_for_offset(float x, float z);
    // Bank level ceiling: 127 * K.
    static constexpr int kMaxLevel = 16;
    void set_bank_enabled(int bank, bool on) { enabled_[bank & 1] = on; }

    // Advances the model by dt seconds (ticks at 60 Hz) and updates voices.
    void update(VoiceMixer& mixer, float rpm, float throttle, double dt, float doppler = 1.0f,
                bool drive_engaged = true);
    void stop(VoiceMixer& mixer);
    const State& state() const { return state_; }

    static std::string variant_dir(int variant); // "normal0".."turbo3"

private:
    friend class EngineUnit;
    struct Slot {
        int layer = -1;
        VoiceId voice;
        u16 pitch = 0;
        s32 vol_l = 0, vol_r = 0;
    };
    struct Player {
        std::shared_ptr<const sound::EngineBank> bank;
        Slot slot[2];
    };
    void tick(VoiceMixer& mixer, float rpm, float throttle, float doppler, bool drive_engaged);
    static void run_player(VoiceMixer& mixer, Player& p, int rpm, u16 pitch_scale, s32 bank_l, s32 bank_r,
                           BankState& st);
    // Unit volume (14-bit L/R) for a gain and the constant-power pan.
    static void unit_volume(float gain, float pan, s32& l, s32& r);

    std::shared_ptr<const sound::EngineBank> engine_, exhaust_;
    Player players_[2];
    View view_ = View::ChaseOwn;
    float volume_ = 1.0f;
    float pan_ = 0.0f;
    float depth_ = 1.0f; // z/d (> 0 = in front)
    SurroundCue cue_;
    bool enabled_[2] = {true, true};
    double accum_ = 0.0;
    s32 level_engine_ = 0, level_exhaust_ = 0;
    State state_;
};

// One extra layer player driven like an engine unit, for the game's third
// per-car unit: the shared wind/road bank sound/rn_01.es. The caller supplies
// the rpm and gain each game frame (car_sound_model.h: wind()); pitch and
// crossfade follow the same rules as EngineSound.
class EngineUnit {
public:
    bool load(Vfs& vfs, const std::string& path, std::string* error = nullptr);
    void set_bank(std::shared_ptr<const sound::EngineBank> bank);
    bool loaded() const { return player_.bank != nullptr; }
    const sound::EngineBank* bank() const { return player_.bank.get(); }

    // gain: unit gain (view gain x model gain; 1 = full scale), pan as
    // EngineSound::set_pan, doppler as EngineSound::update. Call once per
    // game frame.
    void update(VoiceMixer& mixer, float rpm, float gain, float pan = 0.0f, float doppler = 1.0f);
    // Same with the source position in listener space (x right, z forward),
    // which also drives the surround cue when it is enabled.
    void update_at(VoiceMixer& mixer, float rpm, float gain, float x, float z, float doppler = 1.0f);
    void stop(VoiceMixer& mixer);
    const EngineSound::BankState& state() const { return state_; }

    static constexpr const char* kWindBank = "sound/rn_01.es";

private:
    EngineSound::Player player_;
    EngineSound::BankState state_;
    SurroundCue cue_;
};

} // namespace gt3r
