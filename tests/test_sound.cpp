// Sound: SPU helpers, the voice mixer, and (with GT3R_DISC) every sound bank.

#include "test_framework.h"

#include "audio/audio_engine.h"
#include "audio/engine_sound.h"
#include "audio/voice_mixer.h"
#include "formats/sound/engn.h"
#include "formats/sound/inst.h"
#include "vfs/vfs.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <utility>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>

using namespace gt3r;
using namespace gt3r::sound;

namespace {

// A PS-ADPCM frame with filter 0 / shift 0 whose 28 nibbles are all `nib`:
// every sample decodes to nib << 12.
void put_frame(Bytes& out, u8 nib, u8 flags) {
    out.push_back(0x00);
    out.push_back(flags);
    for (int i = 0; i < 14; ++i) out.push_back(u8(nib | nib << 4));
}

} // namespace

TEST(sound_note_to_pitch) {
    CHECK(note_to_pitch(60 << 8, 60 << 8) == 0x1000);
    CHECK(note_to_pitch(72 << 8, 60 << 8) == 0x2000);
    CHECK(note_to_pitch(48 << 8, 60 << 8) == 0x0800);
    CHECK(note_to_pitch(84 << 8, 60 << 8) == 0x4000);
    // A fifth up: 2^(7/12) * 0x1000 = 6137.
    CHECK(std::abs(int(note_to_pitch(67 << 8, 60 << 8)) - 6137) <= 1);
    // Fine tune: center 0x3C80 is half a semitone above note 60.
    CHECK(std::abs(int(note_to_pitch(60 << 8, 0x3C80)) - int(4096 / std::exp2(0.5 / 12))) <= 2);
}

TEST(sound_pan_and_crossfade) {
    int l, r;
    pan_gains(0, l, r);
    CHECK(l == 16384 && r == 0);
    pan_gains(127, l, r);
    CHECK(l == 0 && r == 16384);
    pan_gains(64, l, r);
    CHECK(std::abs(l - r) < 200 && std::abs(l - 11585) < 200);
    CHECK(crossfade_gain(0) == 0);
    CHECK(crossfade_gain(1024) == 4096);
    CHECK(std::abs(crossfade_gain(512) - 2896) <= 1);
}

TEST(sound_mixer_loop_and_end) {
    // Looping sample: [loop start][end + repeat], constant 4096.
    Bytes loop;
    put_frame(loop, 1, 0x06);
    put_frame(loop, 1, 0x03);
    // One-shot: two frames, end without repeat.
    Bytes shot;
    put_frame(shot, 1, 0x00);
    put_frame(shot, 1, 0x01);

    VoiceMixer m;
    m.set_master_volume(1.0f);
    VoiceParams p;
    p.data = std::make_shared<const Bytes>(loop);
    p.vol_l = 0x3FFF;
    p.vol_r = 0x2000;
    VoiceId a = m.key_on(p);
    p.data = std::make_shared<const Bytes>(shot);
    p.vol_r = 0x3FFF;
    VoiceId b = m.key_on(p);
    CHECK(a.valid() && b.valid() && !(a == b));
    CHECK(m.active_voices() == 2);

    std::vector<s16> out(2 * 2000);
    m.mix(out.data(), 2000);
    CHECK(m.playing(a));  // still looping
    CHECK(!m.playing(b)); // one-shot finished after 56 samples
    // Steady state: one voice at 4096 * 0x3FFF/0x4000 on the left, half on
    // the right, less the Gaussian filter's DC loss (taps sum to ~0x7F80).
    CHECK(std::abs(out[2 * 1500] - 4076) <= 3);
    CHECK(std::abs(out[2 * 1500 + 1] - 2038) <= 3);

    m.key_off(a);
    m.mix(out.data(), 2000);
    CHECK(!m.playing(a));
    CHECK(out[2 * 1999] == 0);

    // Stale handles are ignored.
    m.set_pitch(a, 0x2000);
    m.stop(b);
    CHECK(m.active_voices() == 0);
}

TEST(sound_mixer_changes_while_rendering) {
    // mix() renders a block from a copy of the voices without the lock. The game's changes
    // made meanwhile must land after the block (as when they waited for it), not be lost when
    // the rendered voices come back, and the block's own results (a one-shot ending) must stay.
    Bytes loop;
    put_frame(loop, 1, 0x06);
    put_frame(loop, 1, 0x03);
    Bytes shot;
    put_frame(shot, 1, 0x00);
    put_frame(shot, 1, 0x01);
    VoiceMixer m;
    VoiceParams p;
    p.data = std::make_shared<const Bytes>(loop);
    std::vector<VoiceId> held;
    for (int i = 0; i < 40; ++i) held.push_back(m.key_on(p)); // many voices: a long block
    p.data = std::make_shared<const Bytes>(shot);
    VoiceId one_shot = m.key_on(p);
    p.data = std::make_shared<const Bytes>(loop);
    // The hook marks the start of the one block (it runs before the block renders).
    std::atomic<bool> started{false};
    m.set_tick_hook([&] { started = true; }, 1u << 30);
    constexpr size_t kFrames = 4 * VoiceMixer::kRate;
    std::vector<s16> out(2 * kFrames);
    std::thread mixing([&] { m.mix(out.data(), kFrames); });
    while (!started) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(5)); // (most likely while it renders)
    m.key_off(held[0]);
    m.stop(held[1]);
    VoiceId fresh = m.key_on(p);
    m.set_reverb(SpuReverb::Hall, 0x1000, 0x1000);
    CHECK(m.reverb_mode() == SpuReverb::Hall); // the game reads back what it set
    CHECK(m.playing(fresh) && !m.playing(held[1]));
    mixing.join();
    m.set_tick_hook({}, 0);
    CHECK(m.releasing(held[0]) || !m.playing(held[0]));
    CHECK(!m.playing(held[1]));
    CHECK(m.playing(fresh));
    CHECK(m.playing(held[2]) && !m.releasing(held[2]));
    CHECK(!m.playing(one_shot));
    CHECK(m.reverb_mode() == SpuReverb::Hall && m.reverb_depth() == 0x1000);
}

TEST(sound_mixer_bus_gains) {
    // The Sound effects volume is the effects bus's gain and never scales the music bus
    // (the sequenced menu music); the Music volume scales only the music bus (sound §2.4).
    // A constant looping sample on each bus: effects hard left, music hard right.
    Bytes loop;
    put_frame(loop, 1, 0x06);
    put_frame(loop, 1, 0x03);
    auto levels = [&](float effects, float music) {
        VoiceMixer m;
        m.set_master_volume(1.0f);
        m.set_bus_gain(VoiceBus::Effects, effects);
        m.set_bus_gain(VoiceBus::Music, music);
        VoiceParams p;
        p.data = std::make_shared<const Bytes>(loop);
        p.vol_l = 0x3FFF;
        p.vol_r = 0;
        m.key_on(p);
        p.vol_l = 0;
        p.vol_r = 0x3FFF;
        p.bus = VoiceBus::Music;
        m.key_on(p);
        std::vector<s16> out(2 * 2000);
        m.mix(out.data(), 2000);
        return std::pair<int, int>{out[2 * 1500], out[2 * 1500 + 1]};
    };
    auto [l1, r1] = levels(1, 1);
    CHECK(std::abs(l1 - 4076) <= 3 && std::abs(r1 - 4076) <= 3); // unity gains change nothing
    auto [l2, r2] = levels(0, 1);
    CHECK(l2 == 0 && r2 == r1);
    auto [l3, r3] = levels(0.25f, 1);
    CHECK(std::abs(l3 - l1 / 4) <= 1 && r3 == r1);
    auto [l4, r4] = levels(1, 0.5f);
    CHECK(l4 == l1 && std::abs(r4 - r1 / 2) <= 1);
    auto [l5, r5] = levels(1, 0);
    CHECK(l5 == l1 && r5 == 0);
    VoiceMixer m;
    m.set_bus_gain(VoiceBus::Music, 2.0f); // clamped to 0..1
    CHECK(m.bus_gain(VoiceBus::Music) == 1.0f && m.bus_gain(VoiceBus::Effects) == 1.0f);
}

TEST(sound_mixer_pitch) {
    // A repeating 4-frame square wave (2 frames high, 2 low): period 112
    // samples at unity pitch. Game pitch 0x2000 is 88.2 kHz, i.e. SPU2 pitch
    // 0x2000 * 0xEB3 >> 12 = 0x1D66: 112 * 4096 / 7526 = 60.95 output samples.
    Bytes sq;
    put_frame(sq, 3, 0x06);
    put_frame(sq, 3, 0x02);
    put_frame(sq, 0xD, 0x02);
    put_frame(sq, 0xD, 0x03);
    VoiceMixer m;
    VoiceParams p;
    p.data = std::make_shared<const Bytes>(sq);
    p.pitch = 0x2000;
    m.key_on(p);
    std::vector<s16> out(2 * 4096);
    m.mix(out.data(), 4096);
    int crossings = 0;
    for (size_t i = 1001; i < 4096; ++i) crossings += (out[2 * i] >= 0) != (out[2 * (i - 1)] >= 0);
    CHECK(VoiceMixer::hardware_pitch(0x2000) == 0x1D66);
    CHECK(VoiceMixer::hardware_pitch(0x1000) == 0x0EB3);
    // 3095 samples / 60.95 per period * 2 crossings per period.
    CHECK(std::abs(crossings - int(3095 * 2 / 60.95)) <= 2);
}

TEST(sound_banks_on_disc) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Volume& vol = disc->volume();
    size_t engn = 0, inst = 0, layers = 0, failures = 0;
    for (const auto& e : vol.entries()) {
        if (e.path.rfind("sound/", 0) != 0 && e.path.rfind("music/", 0) != 0) continue;
        Bytes d = vol.read(e);
        try {
            if (has_magic(d, "ENGN")) {
                EngineBank b = EngineBank::parse(d);
                for (size_t i = 0; i < b.layers.size(); ++i) {
                    const EngineLayer& l = b.layers[i];
                    SpuSample s = b.decode_layer(i);
                    bool ok = s.looped && s.frames * 16 == l.size && l.pitch > 0 && l.pitch < 0x4000;
                    ok = ok && (i == 0 ? l.lo_rpm == 0 : l.lo_rpm == l.base_rpm);
                    ok = ok && (i + 1 == b.layers.size() ? l.hi_rpm == 32767 : l.hi_rpm == l.base_rpm);
                    if (!ok) ++failures;
                }
                layers += b.layers.size();
                ++engn;
            } else if (has_magic(d, "INST")) {
                InstBank b = InstBank::parse(d);
                for (size_t i = 0; i < b.regions.size(); ++i)
                    if (b.decode_region(i).pcm.empty()) ++failures;
                ++inst;
            }
        } catch (const std::exception& ex) {
            std::printf("  %s: %s\n", e.path.c_str(), ex.what());
            ++failures;
        }
    }
    CHECK(engn == 1209);
    CHECK(inst == 7);
    CHECK(layers == 8429);
    CHECK(failures == 0);

    auto se = parse_se_index(vol.read(*vol.find("sound/se.inf")));
    CHECK(se.size() == 42);
    for (const SoundEffect& e : se) {
        const char* bank = se_bank_path(e.bank);
        CHECK(bank != nullptr);
        if (!bank) continue;
        InstBank b = InstBank::parse(vol.read(*vol.find(bank)));
        CHECK(e.last < b.regions.size());
    }
}

TEST(sound_engine_model) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    EngineSound es;
    CHECK(es.load(vfs, 1, 0));
    CHECK(es.engine_bank() && es.exhaust_bank());
    VoiceMixer m;
    std::vector<s16> buf(2 * 800);
    // At a layer's exact base rpm only that layer plays; between layers two.
    const auto& L = es.engine_bank()->layers;
    for (int i = 0; i < 10; ++i) es.update(m, float(L[2].base_rpm), 1.0f, 1.0 / 60);
    m.mix(buf.data(), 800);
    CHECK(es.state().bank[0].layer[0] == 2 && es.state().bank[0].layer[1] == -1);
    CHECK(es.state().level[1] >= 15); // full throttle: exhaust target 128 * 0.13 = 16
    float mid = (L[2].base_rpm + L[3].base_rpm) / 2.0f;
    for (int i = 0; i < 10; ++i) es.update(m, mid, 0.0f, 1.0 / 60);
    m.mix(buf.data(), 800);
    CHECK(es.state().bank[0].layer[0] == 2 && es.state().bank[0].layer[1] == 3);
    CHECK(std::abs(es.state().bank[0].gain[0] - es.state().bank[0].gain[1]) < 0.01f);
    CHECK(es.state().level[1] <= 4); // off throttle: exhaust target 32 * 0.13 = 4
    CHECK(m.active_voices() >= 2 && m.active_voices() <= 6);
    // Constant-power pan: hard right silences the left channel.
    CHECK(EngineSound::pan_for_offset(3, 4) == 0.6f && EngineSound::pan_for_offset(0.1f, 0) == 0.4f);
    es.set_pan(1.0f);
    for (int i = 0; i < 20; ++i) es.update(m, mid, 0.0f, 1.0 / 60);
    std::vector<s16> big(2 * 4800);
    for (int i = 0; i < 6; ++i) m.mix(big.data(), 4800);
    double el = 0, er = 0;
    for (size_t i = 0; i < 4800; ++i) el += std::abs(big[2 * i]), er += std::abs(big[2 * i + 1]);
    CHECK(er > 0 && el < er * 0.01);
    es.stop(m);
    m.mix(buf.data(), 800);
    CHECK(m.active_voices() == 0);
}

// The output device setting on SDL's dummy driver (one device): a device that isn't connected
// plays on the default; a named one opens explicitly; choosing the default again moves back.
TEST(sound_output_device_choice) {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    const bool init = SDL_InitSubSystem(SDL_INIT_AUDIO);
    SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
    if (!init) SKIP("no SDL dummy audio driver");
    {
        AudioEngine missing("No Such Headphones");
        CHECK(missing.ok());
        CHECK(missing.output_device() == "No Such Headphones");
        CHECK(missing.output_device_id() == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);

        const std::vector<std::string> devs = AudioEngine::playback_devices();
        CHECK(devs.size() == 1);
        AudioEngine named(devs.empty() ? std::string() : devs[0]);
        CHECK(named.ok());
        CHECK(named.output_device_id() != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
        // The switch runs on the engine's own thread.
        named.set_output_device("");
        for (int i = 0; i < 200 && named.output_device_id() != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(named.output_device_id() == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
        named.set_output_device(devs.empty() ? std::string() : devs[0]);
        for (int i = 0; i < 200 && named.output_device_id() == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(named.output_device_id() != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// A refused first open (seen on Android early in the app's life) is retried: an engine made
// while no device can open stays without one, then opens it once one can, on retry().
TEST(sound_output_open_retry) {
    if (SDL_WasInit(SDL_INIT_AUDIO)) SKIP("audio already initialized");
    bool init = false;
    {
        AudioEngine audio; // SDL audio is down: the open fails, the streams stay
        CHECK(!audio.ok());
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        init = SDL_InitSubSystem(SDL_INIT_AUDIO);
        SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
        if (init) {
            audio.retry();
            for (int i = 0; i < 200 && !audio.ok(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            CHECK(audio.ok());
            CHECK(audio.output_device_id() == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
        }
    }
    if (!init) SKIP("no SDL dummy audio driver");
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
