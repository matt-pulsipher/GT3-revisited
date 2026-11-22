#pragma once

// Race sound effects (volume 0..4 x the region's own volume, 14-bit cap): the named effects of sound/se.inf (banks gt3sys,
// gt3se01, gt3se02; the later discs' gt3pod), played through the VoiceMixer. One-shots (collisions,
// shifts) and held loops (tyre skids) whose volume and pitch follow the car
// each frame.

#include "audio/voice_mixer.h"
#include "formats/sound/inst.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gt3r {
class Vfs;
}

namespace gt3r::race {

class RaceSfx {
public:
    bool load(const Vfs& vfs);
    bool loaded() const { return !effects_.empty(); }
    bool has(const std::string& name) const { return effects_.count(name) != 0; }

    // One-shot: volume 0..1 (scales the regions' own volume), pitch multiplier, pan -1..1.
    void play(VoiceMixer& mixer, const std::string& name, float volume = 1, float pitch = 1, float pan = 0);

    // A held, looping effect identified by `slot` (e.g. "skid0"): keyed on while
    // volume > 0, updated every call, keyed off when volume reaches 0.
    void hold(VoiceMixer& mixer, const std::string& slot, const std::string& name, float volume, float pitch = 1,
              float pan = 0);
    void stop_all(VoiceMixer& mixer);

    // The se.inf banks (index = bank number; null when missing) and the index,
    // for players that drive effects themselves (course ambience).
    std::vector<std::shared_ptr<const sound::InstBank>> banks() const {
        return {banks_.begin(), banks_.end()};
    }
    const std::vector<sound::SoundEffect>& index() const { return index_; }

private:
    struct Effect {
        int bank = 0;
        int first = 0, last = 0;
    };
    struct Held {
        std::string name;
        std::vector<VoiceId> voices;
    };
    std::vector<VoiceId> key_on(VoiceMixer& mixer, const Effect& e, float volume, float pitch, float pan);
    void set(VoiceMixer& mixer, const Effect& e, const std::vector<VoiceId>& voices, float volume, float pitch, float pan);

    std::vector<std::shared_ptr<sound::InstBank>> banks_;
    std::map<std::string, Effect> effects_;
    std::vector<sound::SoundEffect> index_;
    std::map<std::string, Held> held_;
    std::vector<VoiceId> oneshots_;
};

} // namespace gt3r::race
