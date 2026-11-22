#include "audio/loop_effect.h"

#include <algorithm>

namespace gt3r {

namespace {

// int(gain x 16384) x region volume >> 14, then clamped: a gain above 1 is louder than the
// region's own volume up to the 14-bit cap. The truncation saturates like the PS2's.
s16 side_volume(float gain, const sound::InstRegion& r) {
    s64 g = s64(std::clamp(gain * 16384.0f, -2147483648.0f, 2147483520.0f));
    return s16(std::clamp<s64>((g * r.volume) >> 14, -0x4000, 0x3FFF));
}

u16 scaled_pitch(const sound::InstRegion& r, float pitch) {
    u32 p = (u32(r.pitch()) * u32(std::clamp(int(pitch * 4096.0f), 0, 0xFFFF))) >> 12;
    return u16(std::min<u32>(p, 0xFFFF));
}

} // namespace

void LoopEffect::set(std::shared_ptr<const sound::InstBank> bank, int region) {
    bank_ = std::move(bank);
    region_ = bank_ && region >= 0 && size_t(region) < bank_->regions.size() ? region : -1;
    voice_ = {};
}

VoiceParams LoopEffect::params(const sound::InstBank& b, int region, s16 l, s16 r, float pitch, int priority,
                               std::shared_ptr<const Bytes> data) {
    const sound::InstRegion& reg = b.regions[size_t(region)];
    VoiceParams p;
    p.data = std::move(data);
    p.start = reg.address;
    p.pitch = scaled_pitch(reg, pitch);
    p.vol_l = l; // negative inverts the phase (the surround cue)
    p.vol_r = r;
    p.adsr1 = reg.adsr1;
    p.adsr2 = reg.adsr2;
    p.reverb = reg.reverb();
    p.priority = u8(priority < 0 ? reg.priority : std::min(priority, 255));
    return p;
}

void LoopEffect::update(VoiceMixer& mixer, float l, float r, float pitch, int priority) { send(mixer, l, r, pitch, priority, true); }

void LoopEffect::restart(VoiceMixer& mixer, float l, float r, float pitch, int priority) {
    stop(mixer);
    send(mixer, l, r, pitch, priority, true);
}

void LoopEffect::follow(VoiceMixer& mixer, float l, float r, float pitch, int priority) {
    if (voice_.valid()) send(mixer, l, r, pitch, priority, false);
}

void LoopEffect::send(VoiceMixer& mixer, float l, float r, float pitch, int priority, bool key) {
    if (!valid()) return;
    const sound::InstRegion& reg = bank_->regions[size_t(region_)];
    s16 vl = side_volume(l, reg), vr = side_volume(r, reg);
    if (vl == 0 && vr == 0) {
        stop(mixer);
        return;
    }
    if (mixer.playing(voice_) && !mixer.releasing(voice_)) {
        if (priority >= 0) mixer.set_priority(voice_, u8(std::min(priority, 255)));
        mixer.set_pitch(voice_, scaled_pitch(reg, pitch));
        mixer.set_volume(voice_, vl, vr);
        return;
    }
    if (!key) {
        voice_ = {};
        return;
    }
    voice_ = mixer.key_on(params(*bank_, region_, vl, vr, pitch, priority, bank_->adpcm));
}

void LoopEffect::stop(VoiceMixer& mixer) {
    if (voice_.valid()) mixer.key_off(voice_);
    voice_ = {};
}

VoiceId LoopEffect::play_once(VoiceMixer& mixer, const std::shared_ptr<const sound::InstBank>& bank, int region,
                              float l, float r, float pitch, int priority) {
    if (!bank || region < 0 || size_t(region) >= bank->regions.size()) return {};
    const sound::InstRegion& reg = bank->regions[size_t(region)];
    return mixer.key_on(params(*bank, region, side_volume(l, reg), side_volume(r, reg), pitch, priority, bank->adpcm));
}

bool LoopEffect::resolve(const std::vector<sound::SoundEffect>& effects, const std::string& name, int sel, int& bank,
                         int& region) {
    for (const sound::SoundEffect& e : effects)
        if (e.name == name) {
            bank = e.bank;
            region = sel == 2 ? e.last : e.first;
            return true;
        }
    return false;
}

} // namespace gt3r
