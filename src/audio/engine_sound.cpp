#include "audio/engine_sound.h"

#include "base/file.h"
#include "base/log.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gt3r {

namespace {

constexpr double kTick = 1.0 / 60.0;
constexpr int kMinRpm = 200;

// The game's per-camera channel gain table: {engine bank, exhaust bank}.
constexpr float kViewGain[int(EngineSound::View::Count)][2] = {
    {3.0f, 2.5f}, {0.3f, 1.3f}, {1.0f, 3.0f}, {0.3f, 1.3f}, {0.5f, 3.3f},
};

constexpr u16 kAdsr1 = 0x000F, kAdsr2 = 0x0003;

// Bank level factors (exhaust, engine) the game uses at run time.
constexpr float kK1 = 0.13f, kK2 = 0.13f;

s32 clamp_vol(s32 v) { return std::clamp(v, -0x4000, 0x3FFF); }

} // namespace

void EngineSound::set_position(float x, float z) {
    float d = std::max(std::hypot(x, z), 0.25f);
    pan_ = std::clamp(x / d, -1.0f, 1.0f);
    depth_ = z / d;
}

float EngineSound::pan_for_offset(float x, float z) {
    float d = std::max(std::hypot(x, z), 0.25f);
    return std::clamp(x / d, -1.0f, 1.0f);
}

const char* EngineSound::view_name(View v) {
    switch (v) {
    case View::OnboardOwn: return "onboard (own car)";
    case View::OnboardOther: return "onboard (other car)";
    case View::ChaseOwn: return "chase (own car)";
    case View::ChaseOther: return "chase (other car)";
    case View::Replay: return "replay / TV";
    default: return "?";
    }
}

std::string EngineSound::variant_dir(int variant) {
    variant = std::clamp(variant, 0, 7);
    return (variant < 4 ? "normal" : "turbo") + std::to_string(variant & 3);
}

EngineSound::~EngineSound() = default;

bool EngineSound::load(Vfs& vfs, int sound_id, int variant, std::string* error, const std::string& dir) {
    char id[16];
    std::snprintf(id, sizeof(id), "%05d", sound_id);
    std::shared_ptr<const sound::EngineBank> banks[2];
    std::string paths[2] = {std::string("sound/engine/") + id, "sound/" + variant_dir(variant) + "/" + id};
    for (int i = 0; i < 2; ++i) {
        try { // (an unreadable file is an error, as a bad one is)
            auto bytes = dir.empty() ? vfs.read(paths[i]) : read_host_file(dir + "/" + paths[i].substr(6)); // (past "sound/")
            if (!bytes) continue; // a car may lack one bank (e.g. engine-only ids)
            banks[i] = std::make_shared<const sound::EngineBank>(sound::EngineBank::parse(*bytes));
        } catch (const std::exception& e) {
            if (error) *error = paths[i] + ": " + e.what();
            return false;
        }
    }
    if (!banks[0] && !banks[1]) {
        if (error) *error = "no engine sound " + std::string(id);
        return false;
    }
    set_banks(banks[0], banks[1]);
    return true;
}

void EngineSound::set_banks(std::shared_ptr<const sound::EngineBank> engine,
                            std::shared_ptr<const sound::EngineBank> exhaust) {
    engine_ = std::move(engine);
    exhaust_ = std::move(exhaust);
    players_[0] = Player{engine_, {}};
    players_[1] = Player{exhaust_, {}};
    level_engine_ = level_exhaust_ = 0;
    accum_ = kTick; // tick immediately on the first update
    state_ = State{};
}

void EngineSound::stop(VoiceMixer& mixer) {
    for (Player& p : players_)
        for (Slot& s : p.slot) {
            mixer.key_off(s.voice);
            s = Slot{};
        }
    state_ = State{};
}

void EngineSound::update(VoiceMixer& mixer, float rpm, float throttle, double dt, float doppler, bool drive_engaged) {
    accum_ += dt;
    int ticks = 0;
    while (accum_ >= kTick && ticks < 4) {
        accum_ -= kTick;
        tick(mixer, rpm, throttle, doppler, drive_engaged);
        ++ticks;
    }
    if (accum_ > kTick) accum_ = 0; // drop backlog after a stall
}

void EngineSound::tick(VoiceMixer& mixer, float rpm, float throttle, float doppler, bool drive_engaged) {
    // Bank levels (0..127), smoothed by halving the distance each tick.
    float t = std::min(throttle, 1.0f);
    s32 target_exh = std::min(127, int((t * 3.0f + 1.0f) * 32.0f * kK1));
    float e = std::fabs(t - 0.375f) + 0.375f;
    s32 target_eng = std::min(127, int(e * (drive_engaged ? 1.0f : 0.5f) * 127.0f * kK2));
    level_exhaust_ = (level_exhaust_ + target_exh) / 2;
    level_engine_ = (level_engine_ + target_eng) / 2;
    state_.level[0] = level_engine_;
    state_.level[1] = level_exhaust_;

    // Bank volume as the voice layer sees it: level/64 * camera gain, placed
    // with the constant-power pan, as 14-bit SPU volume.
    u16 pitch_scale = u16(std::clamp(doppler * 4096.0f, 0.0f, 16383.0f));
    s32 levels[2] = {level_engine_, level_exhaust_};
    for (int b = 0; b < 2; ++b) {
        Player& p = players_[b];
        if (!p.bank) continue;
        float g = levels[b] / 64.0f * kViewGain[int(view_)][b] * volume_ * (enabled_[b] ? 1.0f : 0.0f);
        s32 l, r;
        unit_volume(g, pan_, l, r);
        if (mixer.surround_cue()) {
            if (b == 0) cue_.update(pan_, depth_);
            if (cue_.invert_l) l = -l;
            if (cue_.invert_r) r = -r;
        }
        run_player(mixer, p, std::max(kMinRpm, int(rpm)), pitch_scale, l, r, state_.bank[b]);
    }
}

void EngineSound::unit_volume(float gain, float pan, s32& l, s32& r) {
    float angle = std::clamp((std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.78539816f, 0.0f, 1.5707963f);
    l = std::clamp(s32(gain * std::cos(angle) * 16384.0f), 0, 0x3FFF);
    r = std::clamp(s32(gain * std::sin(angle) * 16384.0f), 0, 0x3FFF);
}

void EngineSound::run_player(VoiceMixer& mixer, Player& p, int rpm, u16 pitch_scale, s32 bank_l, s32 bank_r,
                             BankState& st) {
    const auto& layers = p.bank->layers;
    const int n = int(layers.size());

    // Pick the layer (or neighbouring pair) for this rpm.
    int want[2] = {-1, -1};
    for (int i = 0; i < n; ++i) {
        if (rpm >= layers[i].lo_rpm && rpm <= layers[i].hi_rpm) {
            want[0] = i;
            break;
        }
        if (i > 0 && rpm > layers[i - 1].hi_rpm && rpm < layers[i].lo_rpm) {
            want[0] = i - 1;
            want[1] = i;
            break;
        }
    }
    if (want[0] < 0) return; // keep the current voices (rpm outside every range)

    // Keep voices that already play a wanted layer; release the rest.
    Slot next[2];
    bool used[2] = {false, false};
    for (int k = 0; k < 2; ++k) {
        if (want[k] < 0) continue;
        for (int s = 0; s < 2; ++s)
            if (!used[s] && p.slot[s].layer == want[k] && mixer.playing(p.slot[s].voice)) {
                next[k] = p.slot[s];
                used[s] = true;
                break;
            }
    }
    for (int s = 0; s < 2; ++s)
        if (!used[s] && p.slot[s].voice.valid()) mixer.key_off(p.slot[s].voice);

    for (int k = 0; k < 2; ++k) {
        Slot& slot = next[k];
        st.layer[k] = want[k];
        st.gain[k] = 0;
        st.pitch[k] = 0;
        if (want[k] < 0) {
            slot = Slot{};
            continue;
        }
        const sound::EngineLayer& L = layers[want[k]];
        const bool fresh = !slot.voice.valid();

        // Pitch scales linearly with rpm from the recording's base rpm.
        u32 pitch = u32(u64(L.pitch) * u32(rpm) / std::max<u32>(1, L.base_rpm));
        pitch = (pitch * pitch_scale) >> 12;
        if (pitch == 0) pitch = 1;
        if (!fresh) pitch = std::clamp<u32>(pitch, slot.pitch / 2u, slot.pitch * 2u);
        pitch = std::min<u32>(pitch, 0x3FFF);

        // Crossfade position x (0..0x4000) against the other layer.
        s32 x = 0x4000;
        if (want[1] >= 0) {
            const sound::EngineLayer& O = layers[want[k ^ 1]];
            if (rpm < L.lo_rpm) x = ((rpm - O.hi_rpm) << 14) / std::max(1, L.lo_rpm - O.hi_rpm);
            else if (rpm > L.hi_rpm) x = ((O.lo_rpm - rpm) << 14) / std::max(1, O.lo_rpm - L.hi_rpm);
        }
        x = std::max(x, 0);
        s32 g = (sound::crossfade_gain(x >> 4) * 0x3FFF) >> 12;
        s32 lv = (g * L.volume) >> 14;
        s32 vol_l = clamp_vol((lv * bank_l) >> 14);
        s32 vol_r = clamp_vol((lv * bank_r) >> 14);
        if (!fresh) {
            vol_l = std::clamp(vol_l, slot.vol_l - 0x1000, slot.vol_l + 0x1000);
            vol_r = std::clamp(vol_r, slot.vol_r - 0x1000, slot.vol_r + 0x1000);
        }

        if (fresh) {
            VoiceParams vp;
            vp.data = p.bank->adpcm;
            vp.start = L.offset;
            vp.pitch = u16(pitch);
            vp.vol_l = s16(vol_l);
            vp.vol_r = s16(vol_r);
            vp.adsr1 = kAdsr1;
            vp.adsr2 = kAdsr2;
            vp.reverb = true; // the game keys engine voices with the effect send on
            slot.voice = mixer.key_on(vp);
            slot.layer = want[k];
        } else {
            mixer.set_pitch(slot.voice, u16(pitch));
            mixer.set_volume(slot.voice, s16(vol_l), s16(vol_r));
        }
        slot.pitch = u16(pitch);
        slot.vol_l = vol_l;
        slot.vol_r = vol_r;
        st.gain[k] = sound::crossfade_gain(x >> 4) / 4096.0f;
        st.pitch[k] = u16(pitch);
    }
    p.slot[0] = next[0];
    p.slot[1] = next[1];
}

bool EngineUnit::load(Vfs& vfs, const std::string& path, std::string* error) {
    auto bytes = vfs.read(path);
    if (!bytes) {
        if (error) *error = path + " not found";
        return false;
    }
    try {
        set_bank(std::make_shared<const sound::EngineBank>(sound::EngineBank::parse(*bytes)));
    } catch (const std::exception& e) {
        if (error) *error = path + ": " + e.what();
        return false;
    }
    return true;
}

void EngineUnit::set_bank(std::shared_ptr<const sound::EngineBank> bank) {
    player_ = EngineSound::Player{std::move(bank), {}};
    state_ = EngineSound::BankState{};
}

void EngineUnit::update(VoiceMixer& mixer, float rpm, float gain, float pan, float doppler) {
    if (!player_.bank) return;
    s32 l, r;
    EngineSound::unit_volume(gain, pan, l, r);
    u16 pitch_scale = u16(std::clamp(doppler * 4096.0f, 0.0f, 16383.0f));
    EngineSound::run_player(mixer, player_, std::max(kMinRpm, int(rpm)), pitch_scale, l, r, state_);
}

void EngineUnit::update_at(VoiceMixer& mixer, float rpm, float gain, float x, float z, float doppler) {
    if (!player_.bank) return;
    float d = std::max(std::hypot(x, z), 0.25f);
    float pan = std::clamp(x / d, -1.0f, 1.0f);
    s32 l, r;
    EngineSound::unit_volume(gain, pan, l, r);
    if (mixer.surround_cue()) {
        cue_.update(pan, z / d);
        if (cue_.invert_l) l = -l;
        if (cue_.invert_r) r = -r;
    }
    u16 pitch_scale = u16(std::clamp(doppler * 4096.0f, 0.0f, 16383.0f));
    EngineSound::run_player(mixer, player_, std::max(kMinRpm, int(rpm)), pitch_scale, l, r, state_);
}

void EngineUnit::stop(VoiceMixer& mixer) {
    for (EngineSound::Slot& s : player_.slot) {
        mixer.key_off(s.voice);
        s = EngineSound::Slot{};
    }
    state_ = EngineSound::BankState{};
}

} // namespace gt3r
