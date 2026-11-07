#pragma once

// Tex1 CLUT animations at run time (docs/FORMATS.md "Tex1", "CLUT animation"): the timing and
// colour mixing the game applies to them, and the textures they recolour. Course sets use them
// for flashing lights (the camera flashes in Route 11's grandstands), signs and the start-signal
// lights; a set's textures show the animations' state, not the colours stored in its CLUT data.

#include "formats/tex1.h"

#include <vector>

namespace gt3r {

// Plays a set's CLUT animations the way the game does. Per animation: frame `cur`, the frame
// it fades to (`next`), the seconds spent in `cur` and the fade weight of `cur`,
// 1 - seconds / |frame seconds|. Advancing past a frame steps to the next one (loop, ping-pong or
// once per the animation's mode); a frame with negative seconds holds the animation there until
// it is released (a release ahead of time lets it pass the next hold).
class ClutPlayer {
public:
    ClutPlayer() = default;
    explicit ClutPlayer(std::vector<Tex1::ClutAnimation> animations); // starts reset()

    const std::vector<Tex1::ClutAnimation>& animations() const { return anims_; }

    // Every animation on its first frame, fading to the second, weight 1; held when the first
    // frame holds.
    void reset();
    // Advances (seconds > 0) or rewinds (seconds < 0) the animations of `group` (every group when
    // < 0) that are not held.
    void update(float seconds, int group = -1);
    // One hold less for the animations of `group` (every group when < 0).
    void release(int group);
    // The race start: the start-signal group (1) runs on to its hold, is released, is set back
    // `seconds` (the countdown's length, 0 without one) and released once more, so it reaches its
    // hold as the countdown ends and plays on through it.
    void start_signal(float seconds);

    // Animation i's colours now (CT32 words as stored): per byte, frame cur x w + frame next x
    // (4096 - w), >> 12, with w = the weight x 4096 truncated.
    void colours(size_t i, u32* out) const;

private:
    struct State {
        u32 cur = 0, next = 0;
        float time = 0;   // seconds into frame cur
        float weight = 1; // fade weight of frame cur
        int hold = 0;     // > 0: held
        int dir = 1;      // ping-pong direction
    };
    void advance(size_t i, float seconds);

    std::vector<Tex1::ClutAnimation> anims_;
    std::vector<State> state_;
};

// The textures of one set whose CLUT entries animations rewrite, kept as texel indices so new
// colours only need a palette lookup. Textures are the full power-of-two size with TEX0.CSA
// honoured, as gtm1::decode_texture_set decodes them, or cropped to their REGION_CLAMP rectangle
// as Tex1::decode does (`region_crop`). Paint variants are not applied (no painted set has CLUT
// animations).
class ClutTextures {
public:
    ClutTextures() = default;
    // `tex1` begins with the set's "Tex1" header. Starts at the animations' reset state.
    explicit ClutTextures(ByteSpan tex1, bool region_crop = false);

    bool empty() const { return records_.empty(); }
    ClutPlayer& player() { return player_; }
    const ClutPlayer& player() const { return player_; }

    // Texture records fed by animations, in record order.
    std::vector<u32> records() const;
    // Record r's texels with the animations' current colours (nullptr: not animated).
    const std::vector<u32>* rgba(u32 r) const;
    // Smallest texel alpha of record r over every frame of its animations (0..255).
    u32 min_alpha(u32 r) const;
    // Recolours the records whose animations changed colour since the last call; returns them.
    std::vector<u32> refresh();
    // Stops recolouring texture record `index` (a texture pack replaced it).
    void erase(u32 index);

private:
    struct Record {
        u32 index = 0;          // texture record
        u32 width = 0, height = 0;
        bool tcc = true;
        u32 cpsm = 0;
        std::vector<u8> texels; // palette index per texel
        u32 palette[256] = {};  // RGBA8 of the static entries
        s32 source[256];        // animated entries: (animation << 16 | word), else -1
        std::vector<u32> rgba;
    };
    void recolour(Record& r) const;

    ClutPlayer player_;
    std::vector<Record> records_;
    std::vector<std::vector<u32>> colours_; // per animation: the colours last applied
};

} // namespace gt3r
