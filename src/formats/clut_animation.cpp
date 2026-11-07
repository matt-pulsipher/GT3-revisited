#include "formats/clut_animation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt3r {

ClutPlayer::ClutPlayer(std::vector<Tex1::ClutAnimation> animations) : anims_(std::move(animations)) { reset(); }

void ClutPlayer::reset() {
    state_.assign(anims_.size(), State{});
    for (size_t i = 0; i < anims_.size(); ++i) {
        State& s = state_[i];
        s.next = anims_[i].frames.size() > 1 ? 1 : 0;
        s.hold = anims_[i].frames[0].seconds < 0 ? 1 : 0;
    }
}

void ClutPlayer::update(float seconds, int group) {
    for (size_t i = 0; i < anims_.size(); ++i)
        if ((group < 0 || anims_[i].group == group) && state_[i].hold <= 0) advance(i, seconds);
}

void ClutPlayer::release(int group) {
    for (size_t i = 0; i < anims_.size(); ++i)
        if (group < 0 || anims_[i].group == group) --state_[i].hold;
}

void ClutPlayer::start_signal(float seconds) {
    update(100.0f, 1);
    release(1);
    update(-seconds, 1);
    release(1);
}

void ClutPlayer::advance(size_t i, float seconds) {
    const Tex1::ClutAnimation& a = anims_[i];
    State& s = state_[i];
    const u32 n = u32(a.frames.size());
    auto secs = [&](u32 f) { return a.frames[std::min(f, n - 1)].seconds; };
    const float before = s.time;
    float t = s.time + seconds;
    // (Frames of 0 seconds would step forever; the game has none. Bounded here.)
    int steps = 4 * int(n) + 64;
    if (t >= 0) {
        while (steps-- > 0 && std::fabs(secs(s.cur)) <= t) {
            t -= std::fabs(secs(s.cur));
            s.cur = s.next;
            if (a.mode == 0) {
                s.next = s.cur + 1 >= n ? 0 : s.cur + 1;
            } else if (a.mode == 1) {
                if (s.cur == 0) s.dir = 1;
                else if (s.cur + 1 >= n) s.dir = -1;
                s.next = u32(std::clamp(int(s.cur) + s.dir, 0, int(n) - 1));
            } else if (a.mode == 2) {
                if (s.cur + 1 >= n) break; // stays on the last frame (no hold there)
                s.next = s.cur + 1;
            }
            if (secs(s.cur) < 0) ++s.hold;
            if (s.hold > 0) {
                t = 0;
                break;
            }
        }
    } else {
        // Rewinding: back through the frames, each fading to the one it came from. A hold frame
        // left with time in it holds the animation again.
        float prev = before;
        while (steps-- > 0) {
            if (secs(s.cur) < 0 && prev > 0) ++s.hold;
            if (s.hold > 0) {
                t = 0;
                break;
            }
            const u32 from = s.cur;
            s.next = from;
            if (a.mode == 0) {
                s.cur = from == 0 ? n - 1 : from - 1;
            } else if (a.mode == 1) {
                if (from == 0) s.dir = -1;
                else if (from + 1 >= n) s.dir = 1;
                s.cur = u32(std::clamp(int(from) - s.dir, 0, int(n) - 1));
            } else if (a.mode == 2) {
                if (from == 0) {
                    t = 0;
                    break;
                }
                s.cur = from - 1;
            }
            t += std::fabs(secs(s.cur));
            prev = t;
            if (t >= 0) break;
        }
    }
    s.time = t;
    const float d = std::fabs(secs(s.cur));
    s.weight = d > 0 ? 1.0f - std::max(t, 0.0f) / d : 1.0f;
}

void ClutPlayer::colours(size_t i, u32* out) const {
    const Tex1::ClutAnimation& a = anims_[i];
    const State& s = state_[i];
    const u32 n = u32(a.frames.size());
    const u32* c0 = &a.colours[size_t(std::min(s.cur, n - 1)) * a.words];
    const u32* c1 = &a.colours[size_t(std::min(s.next, n - 1)) * a.words];
    const s32 w = s32(s.weight * 4096.0f); // truncated
    for (u32 k = 0; k < a.words; ++k) {
        u32 v = 0;
        for (int b = 0; b < 32; b += 8) {
            s32 mix = s32((c0[k] >> b) & 0xFF) * w + s32((c1[k] >> b) & 0xFF) * (4096 - w);
            v |= ((u32(mix) >> 12) & 0xFF) << b;
        }
        out[k] = v;
    }
}

ClutTextures::ClutTextures(ByteSpan tex1, bool region_crop) {
    Tex1 t = Tex1::parse(tex1);
    if (t.clut_animations.empty()) return;
    player_ = ClutPlayer(t.clut_animations);
    const std::vector<Tex1::ClutAnimation>& anims = player_.animations();
    // Which CLUT entries the animations feed: their colours replaced by markers, (animation <<
    // 16 | word) + 1, in an otherwise empty GS memory, then read back through each texture's CLUT.
    GsMemory marks;
    for (const Tex1::Buffer& b : t.buffers) {
        if (b.dpsm != u32(Psm::CT32)) continue; // (every animation targets a CT32 CLUT buffer)
        const size_t bytes = size_t(b.width) * b.height * 4;
        std::vector<u8> m(bytes, 0);
        bool any = false;
        for (size_t ai = 0; ai < anims.size(); ++ai) {
            const Tex1::ClutAnimation& a = anims[ai];
            if (a.target < b.offset || a.target - b.offset + 4ull * a.words > bytes) continue;
            for (u32 w = 0; w < a.words; ++w) {
                u32 v = ((u32(ai) << 16) | w) + 1;
                std::memcpy(&m[a.target - b.offset + 4 * w], &v, 4);
            }
            any = true;
        }
        if (any) marks.upload(b.dbp, b.dbw, b.dpsm, b.width, b.height, m);
    }
    GsMemory gs;
    t.upload(tex1, gs);
    for (u32 ti = 0; ti < t.textures.size(); ++ti) {
        const Tex0& t0 = t.textures[ti].tex0;
        if (!indexed_ct32(t0)) continue;
        const bool is4 = t0.psm == u32(Psm::T4) || t0.psm == u32(Psm::T4HL) || t0.psm == u32(Psm::T4HH);
        const u32 n = is4 ? 16 : 256;
        Record r;
        bool animated = false;
        std::fill(std::begin(r.source), std::end(r.source), -1);
        for (u32 i = 0; i < n; ++i) {
            u32 x, y;
            csm1_position((is4 ? t0.csa * 16 : 0) + i, x, y);
            u32 mark = marks.read_texel(t0.cbp, 1, u32(Psm::CT32), x, y);
            if (mark) r.source[i] = s32(mark - 1), animated = true;
            r.palette[i] = clut_rgba(gs.read_texel(t0.cbp, 1, u32(Psm::CT32), x, y), t0.cpsm);
        }
        if (!animated) continue;
        r.index = ti;
        r.width = t0.width(), r.height = t0.height();
        u32 x0 = 0, y0 = 0;
        const Clamp& c = t.textures[ti].clamp;
        if (region_crop && c.wms == 2 && c.maxu >= c.minu) x0 = c.minu, r.width = c.maxu - c.minu + 1;
        if (region_crop && c.wmt == 2 && c.maxv >= c.minv) y0 = c.minv, r.height = c.maxv - c.minv + 1;
        r.tcc = t0.tcc, r.cpsm = t0.cpsm;
        r.texels.resize(size_t(r.width) * r.height);
        for (u32 y = 0; y < r.height; ++y)
            for (u32 x = 0; x < r.width; ++x)
                r.texels[size_t(y) * r.width + x] = u8(gs.read_texel(t0.tbp, t0.tbw, t0.psm, x0 + x, y0 + y) & (n - 1));
        records_.push_back(std::move(r));
    }
    colours_.assign(anims.size(), {});
    refresh();
}

std::vector<u32> ClutTextures::records() const {
    std::vector<u32> out;
    for (const Record& r : records_) out.push_back(r.index);
    return out;
}

const std::vector<u32>* ClutTextures::rgba(u32 index) const {
    for (const Record& r : records_)
        if (r.index == index) return &r.rgba;
    return nullptr;
}

void ClutTextures::erase(u32 index) {
    records_.erase(std::remove_if(records_.begin(), records_.end(), [&](const Record& r) { return r.index == index; }),
                   records_.end());
}

u32 ClutTextures::min_alpha(u32 index) const {
    const std::vector<Tex1::ClutAnimation>& anims = player_.animations();
    for (const Record& r : records_) {
        if (r.index != index) continue;
        bool used[256] = {};
        for (u8 t : r.texels) used[t] = true;
        u32 lo = 255;
        for (u32 i = 0; i < 256; ++i) {
            if (!used[i]) continue;
            if (!r.tcc) return 255;
            if (r.source[i] < 0) {
                lo = std::min(lo, r.palette[i] >> 24);
                continue;
            }
            const Tex1::ClutAnimation& a = anims[size_t(r.source[i] >> 16)];
            const u32 w = u32(r.source[i] & 0xFFFF);
            for (size_t f = 0; f < a.frames.size(); ++f)
                lo = std::min(lo, clut_rgba(a.colours[f * a.words + w], r.cpsm) >> 24);
        }
        return lo;
    }
    return 255;
}

void ClutTextures::recolour(Record& r) const {
    u32 pal[256];
    for (u32 i = 0; i < 256; ++i) {
        u32 c = r.palette[i];
        if (r.source[i] >= 0) c = clut_rgba(colours_[size_t(r.source[i] >> 16)][size_t(r.source[i] & 0xFFFF)], r.cpsm);
        pal[i] = r.tcc ? c : (c | 0xFF000000u);
    }
    r.rgba.resize(r.texels.size());
    for (size_t k = 0; k < r.texels.size(); ++k) r.rgba[k] = pal[r.texels[k]];
}

std::vector<u32> ClutTextures::refresh() {
    const std::vector<Tex1::ClutAnimation>& anims = player_.animations();
    std::vector<bool> changed(anims.size(), false);
    std::vector<u32> now;
    for (size_t ai = 0; ai < anims.size(); ++ai) {
        now.resize(anims[ai].words);
        player_.colours(ai, now.data());
        if (now != colours_[ai]) colours_[ai] = now, changed[ai] = true;
    }
    std::vector<u32> out;
    for (Record& r : records_) {
        bool dirty = false;
        for (u32 i = 0; i < 256 && !dirty; ++i) dirty = r.source[i] >= 0 && changed[size_t(r.source[i] >> 16)];
        if (!dirty) continue;
        recolour(r);
        out.push_back(r.index);
    }
    return out;
}

} // namespace gt3r
