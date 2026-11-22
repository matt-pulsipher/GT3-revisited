#pragma once

// A looping sound-effect voice driven every game frame, like the game's SE
// handles (sound/se.inf regions): keyed on at the region's sample with its
// ADSR when it becomes audible, its volume and pitch updated while it plays,
// keyed off when both sides reach 0. Volume per side = int(gain * 0x4000) *
// region volume >> 14, clamped to 14 bits (negative = phase inverted); pitch =
// the region pitch at key_lo times `pitch`; a stealing priority given with an
// update is re-sent while the voice plays. Also hosts the matching one-shot player.

#include "audio/voice_mixer.h"
#include "formats/sound/inst.h"

#include <memory>
#include <string>
#include <vector>

namespace gt3r {

class LoopEffect {
public:
    void set(std::shared_ptr<const sound::InstBank> bank, int region);
    bool valid() const { return bank_ && region_ >= 0; }

    // l, r: side gains (1 = the region's full volume, negative inverts); pitch: multiplier;
    // priority: stealing priority 0..255 (-1 = the region's own, kept while playing).
    void update(VoiceMixer& mixer, float l, float r, float pitch = 1.0f, int priority = -1);
    // A one-shot on the handle (the game's SE channel given a new effect): restart() stops the voice and keys the region
    // on again; follow() only updates it while it plays (keyed off at 0, never keyed again).
    void restart(VoiceMixer& mixer, float l, float r, float pitch = 1.0f, int priority = -1);
    void follow(VoiceMixer& mixer, float l, float r, float pitch = 1.0f, int priority = -1);
    void stop(VoiceMixer& mixer);
    bool playing(const VoiceMixer& mixer) const { return mixer.playing(voice_); }

    static VoiceId play_once(VoiceMixer& mixer, const std::shared_ptr<const sound::InstBank>& bank, int region,
                             float l, float r, float pitch = 1.0f, int priority = -1);

    // Region of a named se.inf effect: sel 0/1 = its first region, 2 = its
    // last (the right half of the stereo pairs), as the game selects them.
    static bool resolve(const std::vector<sound::SoundEffect>& effects, const std::string& name, int sel, int& bank,
                        int& region);

private:
    void send(VoiceMixer& mixer, float l, float r, float pitch, int priority, bool key);
    static VoiceParams params(const sound::InstBank& b, int region, s16 l, s16 r, float pitch, int priority,
                              std::shared_ptr<const Bytes> data);
    std::shared_ptr<const sound::InstBank> bank_;
    int region_ = -1;
    VoiceId voice_;
};

} // namespace gt3r
