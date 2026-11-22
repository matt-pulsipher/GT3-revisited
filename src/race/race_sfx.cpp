#include "race/race_sfx.h"

#include "base/log.h"
#include "formats/sound/spu.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>

namespace gt3r::race {

bool RaceSfx::load(const Vfs& vfs) {
    banks_.clear();
    effects_.clear();
    index_.clear();
    for (u8 b = 0; b < 4; ++b) { // (bank 3: the later discs' pod effects)
        std::shared_ptr<sound::InstBank> bank;
        if (const char* path = sound::se_bank_path(b))
            if (std::optional<Bytes> bytes = vfs.read(path)) {
                try {
                    bank = std::make_shared<sound::InstBank>(sound::InstBank::parse(*bytes));
                } catch (const std::exception& e) {
                    LOG_WARN("Race sfx: %s: %s", path, e.what());
                }
            }
        banks_.push_back(bank);
    }
    if (std::optional<Bytes> inf = vfs.read("sound/se.inf")) {
        try {
            index_ = sound::parse_se_index(*inf);
            for (const sound::SoundEffect& e : index_)
                if (e.bank < banks_.size() && banks_[e.bank] && e.last < banks_[e.bank]->regions.size())
                    effects_[e.name] = {e.bank, e.first, e.last};
        } catch (const std::exception& e) {
            LOG_WARN("Race sfx: se.inf: %s", e.what());
        }
    }
    LOG_INFO("Race sfx: %zu named effects", effects_.size());
    return loaded();
}

std::vector<VoiceId> RaceSfx::key_on(VoiceMixer& mixer, const Effect& e, float volume, float pitch, float pan) {
    std::vector<VoiceId> out;
    const sound::InstBank& b = *banks_[size_t(e.bank)];
    for (int r = e.first; r <= e.last; ++r) {
        const sound::InstRegion& reg = b.regions[size_t(r)];
        int l, rr;
        sound::pan_gains(std::clamp(int(reg.pan) + int(pan * 63.0f), 0, 127), l, rr);
        float v = std::clamp(volume, 0.0f, 4.0f); // gains above 1 (in-car loop) up to the 14-bit cap
        VoiceParams p{b.adpcm,
                      reg.address,
                      u16(std::clamp(float(reg.pitch()) * pitch, 1.0f, 16383.0f)),
                      s16(std::min(0x3FFF, int(float((reg.volume * l) >> 14) * v))),
                      s16(std::min(0x3FFF, int(float((reg.volume * rr) >> 14) * v))),
                      reg.adsr1,
                      reg.adsr2,
                      reg.reverb(),
                      reg.priority};
        VoiceId id = mixer.key_on(p);
        if (id.valid()) out.push_back(id);
    }
    return out;
}

void RaceSfx::set(VoiceMixer& mixer, const Effect& e, const std::vector<VoiceId>& voices, float volume, float pitch,
                  float pan) {
    const sound::InstBank& b = *banks_[size_t(e.bank)];
    for (size_t i = 0; i < voices.size(); ++i) {
        int r = std::min(e.first + int(i), e.last);
        const sound::InstRegion& reg = b.regions[size_t(r)];
        int l, rr;
        sound::pan_gains(std::clamp(int(reg.pan) + int(pan * 63.0f), 0, 127), l, rr);
        float v = std::clamp(volume, 0.0f, 4.0f); // gains above 1 (in-car loop) up to the 14-bit cap
        mixer.set_volume(voices[i], s16(std::min(0x3FFF, int(float((reg.volume * l) >> 14) * v))),
                         s16(std::min(0x3FFF, int(float((reg.volume * rr) >> 14) * v))));
        mixer.set_pitch(voices[i], u16(std::clamp(float(reg.pitch()) * pitch, 1.0f, 16383.0f)));
    }
}

void RaceSfx::play(VoiceMixer& mixer, const std::string& name, float volume, float pitch, float pan) {
    auto it = effects_.find(name);
    if (it == effects_.end() || volume <= 0.01f) return;
    std::erase_if(oneshots_, [&](VoiceId v) { return !mixer.playing(v); });
    for (VoiceId v : key_on(mixer, it->second, volume, pitch, pan)) oneshots_.push_back(v);
}

void RaceSfx::hold(VoiceMixer& mixer, const std::string& slot, const std::string& name, float volume, float pitch,
                   float pan) {
    auto it = effects_.find(name);
    Held& h = held_[slot];
    bool alive = !h.voices.empty() && h.name == name &&
                 std::all_of(h.voices.begin(), h.voices.end(), [&](VoiceId v) { return mixer.playing(v); });
    if (volume <= 0.01f || it == effects_.end()) {
        for (VoiceId v : h.voices) mixer.key_off(v);
        h.voices.clear();
        return;
    }
    if (!alive) {
        for (VoiceId v : h.voices) mixer.key_off(v);
        h.voices = key_on(mixer, it->second, volume, pitch, pan);
        h.name = name;
        return;
    }
    set(mixer, it->second, h.voices, volume, pitch, pan);
}

void RaceSfx::stop_all(VoiceMixer& mixer) {
    for (auto& [k, h] : held_)
        for (VoiceId v : h.voices) mixer.key_off(v);
    held_.clear();
    for (VoiceId v : oneshots_) mixer.key_off(v);
    oneshots_.clear();
}

} // namespace gt3r::race
