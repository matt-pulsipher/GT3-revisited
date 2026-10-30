// Race-side sound models: wind/in-car/whine/turbo maths (and the blow-off on an
// upshift), the looping effect voice and its priority, every wheel's tyre loops and
// their placement, the music / effects trim, the rn_01.es wind unit, CRSS course ambience
// (with GT3R_DISC: every course's CRSS section), the race music cues and request
// levels, and the race playlist.

#include "test_framework.h"
#include "exact_drive.h"

#include "audio/audio_engine.h"
#include "audio/bgm.h"
#include "audio/car_sound_model.h"
#include "audio/course_ambience.h"
#include "audio/engine_sound.h"
#include "audio/loop_effect.h"
#include "audio/surround_cue.h"
#include "base/game_random.h"
#include "base/ps2_float.h"
#include "formats/course/course_file.h"
#include "formats/db/gt_db.h"
#include "formats/sound/crss.h"
#include "formats/sound/inst.h"
#include "formats/sound/mads.h"
#include "menu/flow/game_flow.h"
#include "menu/flow/menu_sound.h"
#include "race/car_sfx.h"
#include "race/license.h"
#include "race/race_audio.h"
#include "race/race_playlist.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

using namespace gt3r;

TEST(sound_car_models) {
    using namespace gt3r::car_sound;
    // Wind: C_d 0.35 at 100 km/h -> 0.001 * 0.35 * 27.78^2 = 0.27; rpm on
    // the 10..450 km/h ramp.
    Wind w = wind(100 / 3.6f, 0, 0.35f);
    CHECK(std::abs(w.gain - 0.27f) < 0.01f);
    CHECK(std::abs(w.rpm - int(2500 + (100 - 10) / 440.0 * 5000)) <= 1);
    CHECK(wind(80, 0, 0.35f).gain == 0.5f);         // capped
    CHECK(wind(30, 1, 0.35f).gain == 0.0f);         // full slipstream silences it
    CHECK(wind(1, 0, 0.35f).rpm == 2500 && wind(200, 0, 0.35f).rpm == 7500);
    Loop ic = in_car(320 / 3.6f, 0, 0.35f);
    CHECK(std::abs(ic.pitch - 1.5f) < 1e-4f && ic.gain == 2.0f);
    // Whine: off with a stock gearbox or the clutch open.
    CHECK(whine(false, 1, 400, 3, 3, 500).gain == 0 && whine(true, 0, 400, 3, 3, 500).gain == 0);
    Loop wh = whine(true, 1, 490, 3, 3, 1047.1976f);
    CHECK(std::abs(wh.gain - 1.0f) < 1e-5f && std::abs(wh.pitch - 3.5f) < 1e-4f);
    // Turbo: level from the load above 0.75, scaled 0.2 per frame; blow-off
    // once when the throttle drops under 75 %, re-armed above 90 %.
    Turbo t;
    t.step(true, 1.0f, 1.75f, 1.75f, 600, 3000, 0);
    t.frame();
    CHECK(t.level() == 25 && std::abs(t.spool().pitch - 3000 / 6144.0f) < 1e-5f && t.blow_off() == 0);
    t.step(true, 0.2f, 1.6f, 0.5f, 100, 2000, 0);
    t.frame();
    CHECK(std::abs(t.blow_off() - (1.6f - 0.8f) / 256 * 2.5f) < 1e-6f);
    t.step(true, 0.2f, 0.5f, 0.5f, 100, 1000, 0);
    t.frame();
    CHECK(t.blow_off() == 0); // not again until re-armed
    t.step(true, 0.95f, 0.5f, 0.6f, 100, 1000, 0);
    t.step(true, 0.5f, 1.2f, 0.6f, 100, 1000, 0);
    t.frame();
    CHECK(t.blow_off() > 0);
    t.step(false, 1, 2, 2, 600, 0, 0);
    t.frame();
    CHECK(t.level() == 0);
}

TEST(sound_course_ambience_gates) {
    // One gated emitter 10 m to the listener's right; a car between the
    // gates fades it in at 1/60 per frame.
    sound::CrssData d;
    sound::CrssEmitter e;
    e.gate_a = {0, 0, 0};
    e.gate_b = {100, 0, 0};
    e.points.push_back({10, 0, 0, 1.0f});
    d.channels[CourseAmbience::Crowds].push_back(e);
    CourseAmbience a;
    a.load(d, [](float x, float, float, float* out) { *out = x; return true; }, 1000.0f);
    CourseAmbience::Listener l;
    l.right = {1, 0, 0};
    for (int i = 0; i < 30; ++i) a.update({l}, {50.0f});
    CHECK(std::abs(a.channel(CourseAmbience::Crowds).r - 0.5f) < 0.01f);
    CHECK(a.channel(CourseAmbience::Crowds).l < 1e-3f);
    for (int i = 0; i < 60; ++i) a.update({l}, {500.0f}); // car left the span
    CHECK(a.channel(CourseAmbience::Crowds).r == 0);
    // Doppler: moving towards the source raises the pitch.
    for (int i = 0; i < 60; ++i) a.update({l}, {50.0f});
    l.velocity = {30, 0, 0};
    a.update({l}, {50.0f});
    CHECK(std::abs(a.channel(CourseAmbience::Crowds).pitch - 331.45f / (331.45f - 30)) < 1e-3f);
    // Tunnels follow the surface flags.
    l.surface_flags = 0x08;
    a.update({l}, {});
    CHECK(a.tunnel() == 1 && a.tunnel2() == 0);
}

TEST(sound_loop_effect) {
    auto bank = std::make_shared<sound::InstBank>();
    Bytes adpcm;
    for (int f = 0; f < 2; ++f) {
        adpcm.push_back(0);
        adpcm.push_back(f ? 0x03 : 0x06);
        for (int i = 0; i < 14; ++i) adpcm.push_back(0x11);
    }
    bank->adpcm = std::make_shared<const Bytes>(adpcm);
    sound::InstRegion r;
    r.volume = 0x4000;
    r.center = 60 << 8;
    r.key_lo = r.key_hi = 60;
    r.adsr1 = 0x000F;
    bank->regions.push_back(r);
    LoopEffect fx;
    fx.set(bank, 0);
    VoiceMixer m;
    fx.update(m, 0.5f, 0.25f, 2.0f);
    CHECK(fx.playing(m));
    std::vector<s16> out(2 * 512);
    m.mix(out.data(), 512);
    CHECK(out[2 * 500] > 0 && std::abs(out[2 * 500] - 2 * out[2 * 500 + 1]) < 8);
    fx.update(m, 0, 0);
    m.mix(out.data(), 512);
    CHECK(!fx.playing(m));
}

namespace {

// An invented bank: one looping region (two ADPCM frames, the second repeating to the first)
// at volume `vol`, and an se.inf index naming every tyre loop's effect after it.
std::shared_ptr<sound::InstBank> loop_bank(s16 vol) {
    auto bank = std::make_shared<sound::InstBank>();
    Bytes adpcm;
    for (int f = 0; f < 2; ++f) {
        adpcm.push_back(0);
        adpcm.push_back(f ? 0x03 : 0x06);
        for (int i = 0; i < 14; ++i) adpcm.push_back(0x11);
    }
    bank->adpcm = std::make_shared<const Bytes>(adpcm);
    sound::InstRegion r;
    r.volume = vol;
    r.center = 60 << 8;
    r.key_lo = r.key_hi = 60;
    r.adsr1 = 0x000F;
    bank->regions.push_back(r);
    return bank;
}

std::vector<sound::SoundEffect> tyre_index() {
    std::vector<sound::SoundEffect> index;
    for (int l = 0; l < race::kTyreLoops; ++l) index.push_back({race::tyre_loop_effect(race::TyreLoop(l)), 0, 0, 0});
    return index;
}

bool close(float a, float b, float eps = 1e-5f) { return std::abs(a - b) <= eps; }

} // namespace

TEST(sound_loop_effect_levels_and_priority) {
    // Side volume = int(gain x 16384) x region volume >> 14, then the 14-bit cap: a gain above 1
    // is louder than the region's own volume.
    auto bank = loop_bank(12900);
    VoiceMixer m;
    // Every voice taken at priority 200: a request may only take a voice whose value is >= its own.
    VoiceParams busy{bank->adpcm, 0, 0x1000, 0x1000, 0x1000, 0x000F, 0x0003, false, 200};
    for (int i = 0; i < VoiceMixer::kVoices; ++i) CHECK(m.key_on(busy).valid());
    LoopEffect loud, quiet, other;
    loud.set(bank, 0);
    quiet.set(bank, 0);
    other.set(bank, 0);
    loud.update(m, 1.2f, 1.2f, 1, 0); // priority 0: takes a busy voice
    CHECK(loud.playing(m));
    quiet.update(m, 0.01f, 0.01f, 1, 252); // nothing has a value >= 252
    CHECK(!quiet.playing(m));
    // The priority follows each update while the voice plays: once quiet (255) it can be taken.
    loud.update(m, 0.001f, 0.001f, 1, 255);
    other.update(m, 1, 1, 1, 220);
    CHECK(other.playing(m) && !loud.playing(m));
    CHECK(m.active_voices() == VoiceMixer::kVoices);
}

TEST(sound_tyre_loops_per_wheel) {
    using namespace race;
    // A wheel sliding on tarmac: squeal = view gain x slip intensity (unclamped), pitch = Doppler x
    // skid pitch; road roar from its own model; every other loop silent.
    WheelSound w;
    w.surface = 0;
    w.smoke = 1.5f;
    w.pitch = 0.9f;
    w.rim_speed = 30;
    w.tyre_width = 0.225f;
    auto l = wheel_loops(SoundView::ChaseTarget, w, 1.1f);
    CHECK(close(l[kLoopSqueal].vol, 0.6f * 1.5f) && close(l[kLoopSqueal].pitch, 0.99f));
    TyreSound road = road_noise(0.225f, 30);
    CHECK(close(l[kLoopRoad].vol, 0.6f * road.vol) && close(l[kLoopRoad].pitch, 1.1f * road.pitch));
    CHECK(l[kLoopSlide].vol == 0 && l[kLoopDirt].vol == 0 && l[kLoopRumble].vol == 0 && l[kLoopRain].vol == 0);
    // Big slides go past 1 (2.4 x 0.6); the views' gains: cockpit 0.6, other cars 0.3, replay/TV 0.5.
    w.smoke = 2.4f;
    CHECK(close(wheel_loops(SoundView::InCarTarget, w, 1)[kLoopSqueal].vol, 1.44f));
    CHECK(close(wheel_loops(SoundView::ChaseOther, w, 1)[kLoopSqueal].vol, 0.72f));
    CHECK(close(wheel_loops(SoundView::Other, w, 1)[kLoopSqueal].vol, 1.2f));
    // Off the ground for good: the road roar stops, the squeal follows the slip intensity.
    w.contact = false;
    CHECK(wheel_loops(SoundView::ChaseTarget, w, 1)[kLoopRoad].vol == 0);
    // Each surface feeds its own loop: grass/gravel/sand slide, dirt, kerbs, wet.
    w.contact = true;
    w.smoke = 1;
    auto on = [&](int surface, TyreLoop loop, SfxChannel ch) {
        w.surface = surface;
        auto o = wheel_loops(SoundView::ChaseTarget, w, 1);
        for (int k = 0; k < kTyreLoops; ++k)
            if (k != loop && o[size_t(k)].vol != 0) return false;
        return close(o[loop].vol, sfx_gain(SoundView::ChaseTarget, ch));
    };
    CHECK(on(3, kLoopSlide, kChSlide) && on(5, kLoopDirt, kChDirt) && on(13, kLoopRumble, kChRumble));
    CHECK(on(1, kLoopRumble, kChRumble) && on(12, kLoopRain, kChRain) && on(6, kLoopRain, kChRain));
    w.surface = 10; // nothing plays on the other surfaces
    for (const TyreSound& t : wheel_loops(SoundView::ChaseTarget, w, 1)) CHECK(t.vol == 0);
}

TEST(sound_tyre_voice_placement) {
    using namespace race;
    // Constant-power pan from x/d; 32/d beyond 32 m; priority 255 (1 - min(1, |gain|)).
    Placement ahead = place_voice(0, 10, 0.6f);
    CHECK(close(ahead.left, 0.6f * 0.70710678f) && close(ahead.right, 0.6f * 0.70710678f));
    CHECK(ahead.priority == int((1.0f - 0.6f) * 255.0f)); // 101: the float product truncates
    Placement right = place_voice(10, 0, 1), left = place_voice(-10, 0, 1);
    CHECK(std::abs(right.left) < 1e-6f && close(right.right, 1) && close(left.left, 1) && std::abs(left.right) < 1e-6f);
    Placement far = place_voice(0, 64, 1);
    CHECK(close(far.left, 0.5f * 0.70710678f) && far.priority == 127);
    Placement loud = place_voice(3, 4, 1.2f); // d = 5: full, and a gain past 1 is never stolen
    CHECK(close(loud.left * loud.left + loud.right * loud.right, 1.44f, 1e-4f) && loud.priority == 0);
    CHECK(place_voice(0, 10, 0).priority == 255);
    // Within 25 cm the source is first moved back along z by 0.25 - d (here: behind, centred).
    Placement near_ = place_voice(0, 0.1f, 1);
    CHECK(close(near_.z_over_d, -1) && close(near_.left, near_.right));
}

TEST(sound_car_sfx_per_wheel_voices) {
    using namespace race;
    // An invented car in a power drift on tarmac: rear wheels sliding (slip intensity 1.6), fronts
    // gripping (0.2), all rolling at 30 m/s; the camera 6 m behind, looking along +z.
    sim::VehicleState s;
    sim::VehicleSetup setup;
    sim::VehicleTelemetry tel;
    const float xs[4] = {-0.75f, 0.75f, -0.75f, 0.75f}, zs[4] = {1.3f, 1.3f, -1.3f, -1.3f};
    for (size_t w = 0; w < 4; ++w) {
        s.wheels[w].contact = {xs[w], 0, zs[w]};
        s.wheels[w].grounded = true;
        s.wheels[w].load = 3500;
        s.wheels[w].angular_velocity = 100;
        setup.wheels[w].radius = 0.3f;
        setup.wheels[w].width = 0.225f;
        tel.smoke[w] = w < 2 ? 0.2f : 1.6f;
        tel.skid_pitch[w] = w < 2 ? 0.8f : 1.05f;
    }
    CarSfx car;
    CHECK(car.bind({loop_bank(12900)}, tyre_index()));
    CarSfx::Step st;
    st.state = &s;
    st.setup = &setup;
    st.telemetry = &tel;
    st.eye = {0, 1.5f, -6};
    VoiceMixer m;
    RaceSfx sfx; // no effects loaded: the one-shots stay silent
    car.update(sfx, m, st);
    // One squeal and one road voice per wheel: 8 voices.
    CHECK(m.active_voices() == 8);
    for (int w = 0; w < 4; ++w) {
        const CarSfx::Voice& v = car.voice(w, kLoopSqueal);
        float g = 0.6f * tel.smoke[size_t(w)];
        CHECK(close(v.left * v.left + v.right * v.right, g * g, 1e-4f)); // constant power, 32/d = 1 within 32 m
        CHECK(close(v.pitch, tel.skid_pitch[size_t(w)]));
        CHECK(xs[w] < 0 ? v.left > v.right : v.right > v.left); // placed at its wheel
        CHECK(v.priority == std::min(255, int((1 - g) * 255)));
        CHECK(car.voice(w, kLoopRoad).left > 0 && car.voice(w, kLoopSlide).left == 0);
    }
    // The same car as another car, 64 m away: the other-car gain and 32/d.
    CarSfx opp;
    opp.bind({loop_bank(12900)}, tyre_index());
    st.view = SoundView::ChaseOther;
    st.eye = {0, 0, -64};
    st.doppler = 1.05f;
    opp.update(sfx, m, st);
    const CarSfx::Voice& rear = opp.voice(2, kLoopSqueal);
    float d = std::hypot(-0.75f, -1.3f + 64), g = 0.3f * 1.6f * 32 / d;
    CHECK(close(std::hypot(rear.left, rear.right), g, 1e-4f) && close(rear.pitch, 1.05f * 1.05f));
    CHECK(m.active_voices() == 16);
    // Silenced: every voice keyed off.
    car.stop(m);
    opp.stop(m);
    std::vector<s16> out(2 * 4800);
    m.mix(out.data(), 4800);
    CHECK(m.active_voices() == 0);
    // A car without telemetry makes no tyre sound.
    CarSfx none;
    none.bind({loop_bank(12900)}, tyre_index());
    st.telemetry = nullptr;
    none.update(sfx, m, st);
    CHECK(m.active_voices() == 0);
}

TEST(sound_music_and_effects_trim) {
    // The game plays its music streams on SPU2 voices (volume = level x 0x3FFF) beside the
    // effects: a full-level stream and a full-level voice come out at the same level, so the
    // stream takes the voice mix's output trim too.
    CHECK(close(AudioEngine::stream_output_gain(1, 1), VoiceMixer::kOutputTrim));
    CHECK(close(AudioEngine::stream_output_gain(bgm::kRaceSongVolume, 0.8f), 0.5f * 0.8f * VoiceMixer::kOutputTrim));
    VoiceMixer m;
    CHECK(close(m.master_volume(), VoiceMixer::kOutputTrim));
}

TEST(sound_crss_on_disc) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Volume& vol = disc->volume();
    size_t files = 0, with = 0, emitters = 0, gated = 0, failures = 0;
    for (const auto& e : vol.entries()) {
        if (e.path.rfind("courses/US/", 0) != 0) continue;
        Bytes d = vol.read(e);
        if (!course::CourseFile::looks_like_course(d)) continue;
        ++files;
        try {
            auto cf = course::CourseFile::parse(d);
            ByteSpan s = cf.section(d, 35);
            if (s.empty()) continue;
            auto c = sound::CrssData::parse(s);
            ++with;
            for (const auto& ch : c.channels)
                for (const auto& em : ch) {
                    ++emitters;
                    gated += em.gated();
                }
        } catch (const std::exception& ex) {
            std::printf("  %s: %s\n", e.path.c_str(), ex.what());
            ++failures;
        }
    }
    std::printf("  CRSS: %zu/%zu courses, %zu emitters (%zu gated)\n", with, files, emitters, gated);
    CHECK(files == 80 && with == 75 && failures == 0);
    CHECK(emitters > 100 && gated > 50);

    // The wind bank plays through EngineUnit.
    Vfs vfs(&disc->volume());
    EngineUnit u;
    CHECK(u.load(vfs, EngineUnit::kWindBank));
    VoiceMixer m;
    u.update(m, 3750, 0.5f);
    CHECK(u.state().layer[0] == 0 && u.state().layer[1] == 1);
    u.stop(m);
}

TEST(sound_surround_cue) {
    SurroundCue c;
    c.update(0.5f, 0.8f); // in front: channels in phase
    CHECK(c.invert_l == c.invert_r);
    c.update(0.5f, -0.8f); // behind on the right: one side inverted
    CHECK(c.invert_l != c.invert_r);
    c.update(-0.5f, -0.8f); // moving across behind keeps them opposite
    CHECK(c.invert_l != c.invert_r);
    c.update(-0.5f, 0.8f); // back in front: in phase again
    CHECK(c.invert_l == c.invert_r);
    VoiceMixer m;
    CHECK(!m.surround_cue());
}

TEST(sound_menu_effects) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    menu::flow::MenuSounds s;
    CHECK(s.load(vfs));
    VoiceMixer m;
    s.play(m, "cursor"); // one region: keyed hard left and hard right
    CHECK(m.active_voices() == 2);
    s.play(m, "next"); // a pair: first region only
    CHECK(m.active_voices() == 3);
    s.play(m, "cursor"); // earlier instances keep playing
    CHECK(m.active_voices() == 5);
    m.stop_all();
    s.set_enabled(false);
    s.play(m, "ok");
    CHECK(m.active_voices() == 0);
}

TEST(sound_bgm_ids) {
    CHECK(std::string(bgm::sequence_name(bgm::kGtMenu)) == "gtmenu01");
    CHECK(std::string(bgm::sequence_name(bgm::kWheelShop)) == "wheelshop01");
    CHECK(bgm::is_stream(bgm::kRace) && !bgm::is_stream(0x23) && !bgm::is_sequence(bgm::kFinish));
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    auto m = sound::MadsIndex::parse(disc->volume().read(*disc->volume().find("bgm/ads.inf")));
    CHECK(int(m.groups.size()) == 17);
    CHECK(m.groups[size_t(bgm::kRace - 0x12)].size() == 25);
    CHECK(m.groups[size_t(bgm::kFinish - 0x12)].at(0).file == "str02a.ads");
    CHECK(m.groups[size_t(bgm::kResult - 0x12)].at(0).title == "result");
    CHECK(m.groups[size_t(bgm::kLicenseBronze - 0x12)].at(0).title == "bronze");
    CHECK(m.groups[size_t(bgm::kFail - 0x12)].at(0).title == "fail");
    CHECK(m.groups[size_t(bgm::kCarGet - 0x12)].at(0).title == "getacar");
    CHECK(m.groups[size_t(bgm::kDemo - 0x12)].at(0).file == "demo_a01.ads");
    CHECK(m.groups[size_t(bgm::kReplayTheater - 0x12)].at(0).file == "str91a.ads");
    // A new race playlist from the disc's race songs: one pass plays each of the 25 songs
    // once, each mapping back to its own track of the group.
    Vfs vfs(&disc->volume());
    bgm::Player player;
    const std::vector<std::string> files = player.files(vfs, bgm::kRace);
    CHECK(files.size() == 25 && files.at(0) == "race_a01.ads");
    std::vector<u64> ids;
    for (const std::string& f : files) ids.push_back(db::id_hash(f));
    race::RacePlaylist p = race::RacePlaylist::with_songs(ids, 0x456);
    std::vector<int> seen(files.size(), 0);
    for (size_t i = 0; i < files.size(); ++i)
        if (std::optional<size_t> k = p.next()) {
            int track = race::RacePlaylist::track_index(p.songs()[*k].id, ids);
            CHECK(track == int(*k));
            ++seen[size_t(track)];
        }
    CHECK(std::all_of(seen.begin(), seen.end(), [](int v) { return v == 1; }));
}

// A licence run's end, as GT3 plays it (music §3.1): its judging posts the failed event for a course-out and for a time
// slower than bronze, live and in the run's replay or demonstration alike, and the fail jingle answers it; a medal plays its
// medal jingle live (nothing at the finish) and the finish jingle in a replay or demonstration.
TEST(sound_race_licence_jingles) {
    using race::RaceMusic;
    using Op = RaceMusic::Action::Op;
    // The jingle at the run's end (-1 none) for an outcome, live (License) or replayed / demonstrated (LicenseReplay).
    auto end_jingle = [](RaceMusic::Kind kind, const race::LicenseOutcome& o) {
        RaceMusic m;
        RaceMusic::Cues c;
        c.kind = kind, c.start_type = 3;
        m.decide(c, false);
        c.finished = true, c.failed = race::license_fail_jingle(o), c.medal = o.medal;
        for (const RaceMusic::Action& a : m.decide(c, false))
            if (a.op == Op::Play) return a.id;
        return -1;
    };
    const race::LicenseOutcome slow = race::license_outcome(0, 41007, true, -1);  // TIME EXPIRED
    const race::LicenseOutcome out = race::license_outcome(-1, 0x157529FF, true, -1); // FAIL
    const race::LicenseOutcome gold = race::license_outcome(2, 34950, false, -1);
    for (RaceMusic::Kind k : {RaceMusic::Kind::License, RaceMusic::Kind::LicenseReplay}) {
        CHECK(end_jingle(k, slow) == bgm::kFail); // live, and its replay's end
        CHECK(end_jingle(k, out) == bgm::kFail);
    }
    CHECK(end_jingle(RaceMusic::Kind::License, gold) == -1); // the medal jingle follows (sound_race_music_cues)
    CHECK(end_jingle(RaceMusic::Kind::LicenseReplay, gold) == bgm::kFinish);
    // A demonstration without a medal (its judged result 0 or -1): the fail jingle; with one, the finish jingle.
    for (int result : {0, -1, 4})
        CHECK(end_jingle(RaceMusic::Kind::LicenseReplay, race::license_outcome(result, 50000, true, -1)) ==
              (result == 4 ? bgm::kFinish : bgm::kFail));
}

// The race's music cue sequence: start jingle,
// race track at the signal, the finish jingle for every place, licence medal
// and fail jingles, the result / quick menus, pause.
TEST(sound_race_music_cues) {
    using race::RaceMusic;
    using Op = RaceMusic::Action::Op;
    using A = RaceMusic::Action;
    using V = std::vector<A>;
    {   // A single race (start type 2), finishing 4th, then the result menu.
        RaceMusic m;
        RaceMusic::Cues c;
        c.start_type = 2;
        c.holding = true;
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kStartLong, false}}));
        CHECK(m.decide(c, true).empty());
        CHECK(m.decide(c, false).empty()); // jingle over, still held: silence
        c.holding = false;                 // the signal
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kRace, false, bgm::kRaceSongVolume}}));
        CHECK(m.decide(c, true).empty());
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kRace, false, bgm::kRaceSongVolume}})); // the next track
        c.paused = true;
        CHECK((m.decide(c, true) == V{A{Op::Pause}}));
        c.paused = false;
        CHECK((m.decide(c, true) == V{A{Op::Resume}}));
        c.finished = true;
        c.position = 4;
        CHECK((m.decide(c, true) == V{A{Op::Play, bgm::kFinish, false}}));
        CHECK(m.decide(c, false).empty()); // after the jingle: silence until the menu
        c.menu = RaceMusic::Menu::Result;
        CHECK((m.decide(c, false) == V{A{Op::PlayIfDifferent, bgm::kResult, true, bgm::kMenuVolume}}));
        CHECK(m.decide(c, true).empty());
        c.menu = RaceMusic::Menu::None;
        CHECK((m.decide(c, true) == V{A{Op::Stop}}));
    }
    {   // A licence test from rest (start type 1): a pass plays only the medal jingle.
        RaceMusic m;
        RaceMusic::Cues c;
        c.kind = RaceMusic::Kind::License;
        c.start_type = 1;
        c.holding = true;
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kStartShort, false}}));
        c.holding = false;
        CHECK((m.decide(c, true) == V{A{Op::Stop}})); // no race music in licence tests
        CHECK(m.decide(c, false).empty());
        c.finished = true;
        c.position = 1;
        c.medal = 0;
        CHECK(m.decide(c, false).empty()); // no "jg12" on a licence pass
        c.since_finish = RaceMusic::kMedalDelay / 2;
        CHECK(m.decide(c, false).empty());
        c.since_finish = RaceMusic::kMedalDelay;
        CHECK((m.decide(c, true) == V{A{Op::Play, bgm::kLicenseGold, false}}));
        CHECK(m.decide(c, true).empty());
        c.menu = RaceMusic::Menu::Short;
        CHECK((m.decide(c, false) == V{A{Op::PlayIfDifferent, bgm::kQuickMenu, true, bgm::kMenuVolume}}));
    }
    {   // A failed licence test (flying start: no jingle, no hold).
        RaceMusic m;
        RaceMusic::Cues c;
        c.kind = RaceMusic::Kind::License;
        c.start_type = 3;
        CHECK(m.decide(c, false).empty());
        c.finished = c.failed = true;
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kFail, false}}));
        c.since_finish = 5;
        CHECK(m.decide(c, true).empty());
    }
    {   // A licence replay (or disc demo): "jg12" at the finish, no race music, no medal.
        RaceMusic m;
        RaceMusic::Cues c;
        c.kind = RaceMusic::Kind::LicenseReplay;
        c.start_type = 3;
        CHECK(m.decide(c, false).empty());
        c.finished = true;
        c.medal = 0;
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kFinish, false}}));
        c.since_finish = 5;
        CHECK(m.decide(c, false).empty());
    }
    {   // Music switched off: the requests still happen (played at level 0, RaceMusicLevel), so
        // the playlist advances; attract demo track once.
        RaceMusic m;
        RaceMusic::Cues c;
        c.music_on = false;
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kRace, false, bgm::kRaceSongVolume}}));
        CHECK(m.decide(c, true).empty());
        c.music_on = true;
        c.kind = RaceMusic::Kind::Demo;
        c.start_type = 0;
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kDemo, false}}));
        CHECK(m.decide(c, false).empty());
    }
}

// Replays re-run the start sequence (music §2.5): the start jingle over the hold, the race song
// at the replay level from the signal, the finish jingle when the car the cameras follow
// finishes (once).
TEST(sound_race_music_replay_start) {
    using race::RaceMusic;
    using Op = RaceMusic::Action::Op;
    using V = std::vector<RaceMusic::Action>;
    using A = RaceMusic::Action;
    RaceMusic m;
    RaceMusic::Cues c;
    c.replay = true;
    c.start_type = 2;
    c.holding = true;
    CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kStartLong, false}}));
    CHECK(m.decide(c, true).empty());
    c.holding = false; // the replayed signal
    CHECK((m.decide(c, true) == V{A{Op::Play, bgm::kRace, false, bgm::kReplaySongVolume}}));
    c.finished = true;
    CHECK((m.decide(c, true) == V{A{Op::Play, bgm::kFinish, false}}));
    // The finish cue: the followed car's own finish, not one that finished before it was followed.
    race::FinishCue f;
    CHECK(!f.update({false, false, false}, 0));
    CHECK(!f.update({false, true, false}, 0)); // car 1 finishes while car 0 is followed
    CHECK(!f.update({false, true, false}, 1)); // ... followed afterwards: nothing
    CHECK(f.update({false, true, true}, 2));   // car 2 finishes while followed
    CHECK(f.update({true, true, true}, 1));    // heard once, kept
    f.reset();
    CHECK(!f.update({false, false, false}, 0));
}

// The race music's level (music §2.1): its last request's level while the run's switch is on,
// 0 while off; a request made with the switch off stays silent until a stop.
TEST(sound_race_music_level) {
    race::RaceMusicLevel l;
    CHECK(l.base(true) == 1.0f && l.base(false) == 0.0f);
    CHECK(l.request(true, 0.5f) == 0.5f && l.base(true) == 0.5f);
    CHECK(l.base(false) == 0.0f);                              // switched off: silent at once
    CHECK(l.request(false, 0.7f) == 0.0f && l.base(true) == 0.0f); // requested while off
    CHECK(l.request(true, 0.7f) == 0.0f);                      // ... also once switched on again
    l.stop();
    CHECK(l.request(true, 1.0f) == 1.0f);
}

// The ads.inf info block (FORMATS.md, music §8): rate, length and 32 marker tracks, offsets
// from the block.
TEST(sound_mads_markers_synthetic) {
    Bytes d(0x400, 0);
    auto w32 = [&d](size_t o, u32 v) { std::memcpy(&d[o], &v, 4); };
    std::memcpy(d.data(), "MADS", 4);
    w32(4, 1);
    w32(0x0C, 1);
    w32(0x10, 0x100); // group 0x12: one record at 0x100
    w32(0x14, 1);
    const char* names[4] = {"race_a01", "race_a01.ads", "title", "artist"};
    u32 str = 0x380;
    for (int k = 0; k < 4; ++k) {
        w32(0x100 + 4 * size_t(k), str);
        std::memcpy(&d[str], names[k], std::strlen(names[k]));
        str += 0x10;
    }
    const u32 info = 0x140;
    w32(0x110, info);
    w32(info + 4, 44100);
    w32(info + 8, 9000000);
    const u32 marks[3] = {1791, 162047, 322560};
    w32(info + 0x0C + 4 * 3, 3);    // track 3: three markers
    w32(info + 0x8C + 4 * 3, 0x110); // at info + 0x110
    for (int i = 0; i < 3; ++i) w32(info + 0x110 + 4 * size_t(i), marks[i]);
    sound::MadsIndex m = sound::MadsIndex::parse(d);
    const sound::MadsMarkers& k = m.groups.at(0).at(0).markers;
    CHECK(k.rate == 44100 && k.length == 9000000 && k.any());
    CHECK((k.tracks[3] == std::vector<u32>{1791, 162047, 322560}) && k.tracks[4].empty());
    CHECK(std::fabs(k.seconds(3, 1) - 162047.0 / 44100.0) < 1e-9);
    w32(info + 0x0C + 4 * 3, 0x200); // a track past the file
    bool threw = false;
    try {
        sound::MadsIndex::parse(d);
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

// On the disc: only the race songs and demo_a01 carry markers, tracks 1..4 only, tracks 3 and 4
// the same cut list; race_a01's starts 1791, 162047, 322560 (0.041, 3.675, 7.314 s).
TEST(sound_mads_markers_disc) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    auto m = sound::MadsIndex::parse(disc->volume().read(*disc->volume().find("bgm/ads.inf")));
    const sound::MadsTrack& a01 = m.groups.at(size_t(bgm::kRace - 0x12)).at(0);
    CHECK(a01.file == "race_a01.ads" && a01.markers.rate == 44100);
    const std::vector<u32>& t3 = a01.markers.tracks[3];
    CHECK(t3.size() == 205 && t3.at(0) == 1791 && t3.at(1) == 162047 && t3.at(2) == 322560);
    CHECK(a01.markers.tracks[1].size() == 612 && a01.markers.tracks[2].size() == 126);
    int marked = 0;
    for (size_t g = 0; g < m.groups.size(); ++g)
        for (const sound::MadsTrack& t : m.groups[g]) {
            const bool songs = int(g) + 0x12 == bgm::kRace || int(g) + 0x12 == bgm::kDemo;
            CHECK(t.markers.any() == songs);
            if (!t.markers.any()) continue;
            ++marked;
            CHECK(t.markers.tracks[3] == t.markers.tracks[4] && !t.markers.tracks[3].empty());
            for (int k = 0; k < sound::MadsMarkers::kTracks; ++k)
                if (k == 0 || k > 4) CHECK(t.markers.tracks[size_t(k)].empty());
        }
    CHECK(marked == 26);
    const sound::MadsTrack& demo = m.groups.at(size_t(bgm::kDemo - 0x12)).at(0);
    CHECK(demo.markers.tracks[3].size() == 88);
}

// The cues as the race screen produces them for a run entered through its quick menu
// (music §2.2, §3.3): the run is set up behind the pre-race menu with the cars already
// held, the menu closes on the hold's first frame, then the signal. The start jingle
// plays when the menu closes, as at a direct start; Retry from the result menu likewise.
TEST(sound_race_music_start_after_quick_menu) {
    using race::RaceMusic;
    using Op = RaceMusic::Action::Op;
    using A = RaceMusic::Action;
    using V = std::vector<A>;
    {   // A GT race (start type 2): pre-race menu, Start, the 10 s hold, the signal.
        RaceMusic m;
        RaceMusic::Cues c;
        c.start_type = 2;
        c.holding = true;
        c.menu = RaceMusic::Menu::Short;
        CHECK((m.decide(c, false) == V{A{Op::PlayIfDifferent, bgm::kQuickMenu, true, bgm::kMenuVolume}}));
        for (int i = 0; i < 30; ++i) CHECK(m.decide(c, true).empty()); // the menu, the run held behind it
        c.menu = RaceMusic::Menu::None;                                  // Start: the hold begins
        CHECK((m.decide(c, true) == V{A{Op::Stop}, A{Op::Play, bgm::kStartLong, false}}));
        for (int i = 0; i < 599; ++i) CHECK(m.decide(c, i < 300).empty()); // the jingle, then silence
        c.holding = false;                                                  // the signal
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kRace, false, bgm::kRaceSongVolume}}));
        // Finish, result menu, Retry: the run is held again when the menu closes.
        c.finished = true;
        CHECK((m.decide(c, true) == V{A{Op::Play, bgm::kFinish, false}}));
        c.menu = RaceMusic::Menu::Result;
        CHECK((m.decide(c, false) == V{A{Op::PlayIfDifferent, bgm::kResult, true, bgm::kMenuVolume}}));
        c.finished = false;
        c.holding = true;
        c.menu = RaceMusic::Menu::None;
        CHECK((m.decide(c, true) == V{A{Op::Stop}, A{Op::Play, bgm::kStartLong, false}}));
    }
    {   // GT mode's SETTING from the pre-race menu: the menu closes (the stop: RaceMusic::stop starts the next
        // cues afresh) and opens again after the settings, its music from the beginning; Start as usual.
        RaceMusic m;
        RaceMusic::Cues c;
        c.start_type = 2;
        c.holding = true;
        c.menu = RaceMusic::Menu::Short;
        CHECK((m.decide(c, false) == V{A{Op::PlayIfDifferent, bgm::kQuickMenu, true, bgm::kMenuVolume}}));
        CHECK(m.decide(c, true).empty());
        m.reset(); // SETTING
        CHECK((m.decide(c, false) == V{A{Op::PlayIfDifferent, bgm::kQuickMenu, true, bgm::kMenuVolume}}));
        CHECK(m.decide(c, true).empty());
        c.menu = RaceMusic::Menu::None; // Start
        CHECK((m.decide(c, true) == V{A{Op::Stop}, A{Op::Play, bgm::kStartLong, false}}));
    }
    {   // A licence test from rest (start type 1): its menu, Start, the 4 s hold, the signal.
        RaceMusic m;
        RaceMusic::Cues c;
        c.kind = RaceMusic::Kind::License;
        c.start_type = 1;
        c.menu = RaceMusic::Menu::Short;
        CHECK((m.decide(c, false) == V{A{Op::PlayIfDifferent, bgm::kQuickMenu, true, bgm::kMenuVolume}}));
        c.holding = true;
        c.menu = RaceMusic::Menu::None;
        CHECK((m.decide(c, true) == V{A{Op::Stop}, A{Op::Play, bgm::kStartShort, false}}));
        c.holding = false;
        CHECK((m.decide(c, true) == V{A{Op::Stop}}));
    }
    {   // Closing the menu into a run with no hold: the stop, then the race songs.
        RaceMusic m;
        RaceMusic::Cues c;
        c.start_type = 3;
        c.menu = RaceMusic::Menu::Short;
        CHECK((m.decide(c, false) == V{A{Op::PlayIfDifferent, bgm::kQuickMenu, true, bgm::kMenuVolume}}));
        c.menu = RaceMusic::Menu::None;
        CHECK((m.decide(c, true) == V{A{Op::Stop}}));
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kRace, false, bgm::kRaceSongVolume}}));
    }
    {   // A time trial's flying start (start type 3): its menu, Start, the computer's run-up to
        // the line held in silence, the race songs from the start signal at the line.
        RaceMusic m;
        RaceMusic::Cues c;
        c.start_type = 3;
        c.menu = RaceMusic::Menu::Short;
        CHECK((m.decide(c, false) == V{A{Op::PlayIfDifferent, bgm::kQuickMenu, true, bgm::kMenuVolume}}));
        c.holding = true; // the run-up
        c.menu = RaceMusic::Menu::None;
        CHECK((m.decide(c, true) == V{A{Op::Stop}}));
        for (int i = 0; i < 120; ++i) CHECK(m.decide(c, false).empty());
        c.holding = false; // the line
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kRace, false, bgm::kRaceSongVolume}}));
    }
    {   // Closing the menu during a hold with no start jingle (start type 0): only the stop.
        RaceMusic m;
        RaceMusic::Cues c;
        c.start_type = 0;
        c.holding = true;
        c.menu = RaceMusic::Menu::Short;
        CHECK((m.decide(c, false) == V{A{Op::PlayIfDifferent, bgm::kQuickMenu, true, bgm::kMenuVolume}}));
        c.menu = RaceMusic::Menu::None;
        CHECK((m.decide(c, true) == V{A{Op::Stop}}));
        CHECK(m.decide(c, false).empty()); // held: silence
    }
}

// RETRY from the pause window over a failed run: the run's music stops (RaceMusic::stop, which
// starts the next cues afresh) and the new run plays its own start, never the paused piece.
TEST(sound_race_music_pause_retry) {
    using race::RaceMusic;
    using Op = RaceMusic::Action::Op;
    using A = RaceMusic::Action;
    using V = std::vector<A>;
    for (int start_type : {3, 1}) { // a flying licence test, one from rest
        RaceMusic m;
        RaceMusic::Cues c;
        c.kind = RaceMusic::Kind::License;
        c.start_type = start_type;
        CHECK(m.decide(c, false).empty());
        c.finished = c.failed = true;
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kFail, false}}));
        c.paused = true; // the fail screen, paused
        CHECK((m.decide(c, true) == V{A{Op::Pause}}));
        m.reset(); // RETRY: the stop
        c = RaceMusic::Cues{};
        c.kind = RaceMusic::Kind::License;
        c.start_type = start_type;
        c.holding = true; // the new run's hold (a flying test's run-up)
        V start = m.decide(c, false);
        CHECK((start == (start_type == 1 ? V{A{Op::Play, bgm::kStartShort, false}} : V{})));
        for (int i = 0; i < 120; ++i) CHECK(m.decide(c, start_type == 1).empty());
        c.holding = false; // the signal: no race music in licence tests
        CHECK((m.decide(c, false) == (start_type == 1 ? V{A{Op::Stop}} : V{})));
        CHECK(m.decide(c, false).empty());
    }
    {   // A time trial voided, paused, retried: silence over the run-up, a race song from the line.
        RaceMusic m;
        RaceMusic::Cues c;
        c.start_type = 3;
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kRace, false, bgm::kRaceSongVolume}}));
        c.finished = c.failed = true;
        CHECK((m.decide(c, true) == V{A{Op::Play, bgm::kFail, false}}));
        c.paused = true;
        CHECK((m.decide(c, true) == V{A{Op::Pause}}));
        m.reset();
        c = RaceMusic::Cues{};
        c.start_type = 3;
        c.holding = true;
        CHECK(m.decide(c, false).empty());
        c.holding = false;
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kRace, false, bgm::kRaceSongVolume}}));
    }
}

// Each music request's level (music §6): race songs 0.5 live and 0.7 in replays,
// jingles and the demo track 1.0, the quick menus the menu level 0.8; the quick menus
// follow the Menu BGM switch, not the race's.
TEST(sound_music_request_levels) {
    using race::RaceMusic;
    using Op = RaceMusic::Action::Op;
    using A = RaceMusic::Action;
    using V = std::vector<A>;
    // A sequence takes its level as the sequencer's music volume (Q14, truncated); a
    // stream plays at level x the Music volume setting.
    CHECK(bgm::sequence_volume(bgm::kMenuVolume) == 13107);
    CHECK(bgm::sequence_volume(bgm::kJingleVolume) == 0x3FFF && bgm::sequence_volume(0) == 0);
    CHECK(std::abs(bgm::stream_gain(bgm::kRaceSongVolume, 0.8f) - 0.4f) < 1e-6f);
    CHECK(std::abs(bgm::stream_gain(bgm::kReplaySongVolume, 1.0f) - 0.7f) < 1e-6f);
    CHECK(std::abs(bgm::stream_gain(bgm::kMenuVolume, 0.5f) - 0.4f) < 1e-6f);
    {   // A live race: start jingle 1.0, race songs 0.5, finish jingle 1.0, result menu 0.8.
        RaceMusic m;
        RaceMusic::Cues c;
        c.holding = true;
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kStartLong, false, 1.0f}}));
        c.holding = false;
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kRace, false, 0.5f}}));
        c.finished = true;
        CHECK((m.decide(c, true) == V{A{Op::Play, bgm::kFinish, false, 1.0f}}));
        c.menu = RaceMusic::Menu::Result;
        CHECK((m.decide(c, false) == V{A{Op::PlayIfDifferent, bgm::kResult, true, 0.8f}}));
    }
    {   // A replay of the run (rolling start): race songs at 0.7.
        RaceMusic m;
        RaceMusic::Cues c;
        c.replay = true;
        c.start_type = 3;
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kRace, false, 0.7f}}));
    }
    {   // The attract demo's track at 1.0.
        RaceMusic m;
        RaceMusic::Cues c;
        c.kind = RaceMusic::Kind::Demo;
        c.start_type = 0;
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kDemo, false, 1.0f}}));
    }
    {   // Race BGM off, Menu BGM on: the quick menu still plays its music; closing it stops.
        RaceMusic m;
        RaceMusic::Cues c;
        c.music_on = false;
        c.menu = RaceMusic::Menu::Short;
        CHECK((m.decide(c, false) == V{A{Op::PlayIfDifferent, bgm::kQuickMenu, true, 0.8f}}));
        CHECK(m.decide(c, true).empty());
        c.menu = RaceMusic::Menu::None;
        CHECK((m.decide(c, true) == V{A{Op::Stop}}));
        // The race's own song is requested, silently (RaceMusicLevel).
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kRace, false, 0.5f}}));
    }
    {   // Menu BGM off, race BGM on: no quick-menu music, the race music plays.
        RaceMusic m;
        RaceMusic::Cues c;
        c.menu_music_on = false;
        c.menu = RaceMusic::Menu::Short;
        CHECK((m.decide(c, false) == V{A{Op::Stop}}));
        c.menu = RaceMusic::Menu::None;
        c.start_type = 3;
        CHECK((m.decide(c, false) == V{A{Op::Stop}})); // the menu closes
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kRace, false, 0.5f}}));
    }
    {   // No song in the playlist: the start jingle stops at the signal and no race song plays.
        RaceMusic m;
        RaceMusic::Cues c;
        c.songs = false;
        c.holding = true;
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kStartLong, false}}));
        c.holding = false;
        CHECK((m.decide(c, true) == V{A{Op::Stop}}));
        CHECK(m.decide(c, false).empty());
        c.finished = true;
        CHECK((m.decide(c, false) == V{A{Op::Play, bgm::kFinish, false}})); // the jingles still play
    }
}

// The race playlist (music §2.3). Invented id codes.
TEST(sound_race_playlist) {
    using race::RacePlaylist;
    const std::vector<u64> ids = {0x1111, 0x2222, 0x3333, 0x4444, 0x5555};
    const size_t n = ids.size();
    {   // List order: every song once per pass, in list order; the next pass starts again.
        RacePlaylist p = RacePlaylist::with_songs(ids, 0x1234);
        p.set_shuffle(false);
        for (int pass = 0; pass < 2; ++pass)
            for (size_t k = 0; k < n; ++k) CHECK(p.next() == k);
        // Songs out of the list are skipped (the options' commit rebuilds the order).
        p.set_listed(1, false);
        p.set_listed(3, false);
        p.set_shuffle(false);
        CHECK(p.next() == 0u && p.next() == 2u && p.next() == 4u && p.next() == 0u);
    }
    {   // Shuffle: the order comes from the generator seeded with the stored seed: draw i
        // puts song trunc(unit x n) (or the next free one) at place i; one more draw is
        // the next seed. A pass plays every song once; the pass after it is a new order.
        RacePlaylist p = RacePlaylist::with_songs(ids, 0x1234);
        CHECK(p.shuffle() && p.has_songs() && p.cursor() == 0);
        u32 st = 0x1234;
        const size_t first = size_t(ps2::to_int(ps2::mul(GameRandom::rand01(st), float(n))));
        GameRandom::skip(st, int(n) - 1);
        const u16 seed = u16(ps2::to_int(GameRandom::rand01(st) * 32768.0f));
        CHECK(p.seed() == seed);
        std::vector<int> seen(n, 0);
        std::optional<size_t> k0 = p.next();
        CHECK(k0 == first);
        if (k0) ++seen[*k0];
        for (size_t i = 1; i < n; ++i)
            if (std::optional<size_t> k = p.next()) ++seen[*k];
        CHECK(std::all_of(seen.begin(), seen.end(), [](int v) { return v == 1; }));
        CHECK(p.cursor() == 0 && p.seed() != seed); // rebuilt right after the pass's last song
        st = seed;
        CHECK(p.next() == size_t(ps2::to_int(ps2::mul(GameRandom::rand01(st), float(n)))));
        // The same seed gives the same order.
        RacePlaylist q = RacePlaylist::with_songs(ids, 0x1234);
        CHECK(q.next() == k0);
    }
    {   // No song in the list: nothing to play.
        RacePlaylist p = RacePlaylist::with_songs(ids, 7);
        for (size_t k = 0; k < n; ++k) p.set_listed(k, false);
        p.set_shuffle(true);
        CHECK(!p.has_songs() && !p.next());
        CHECK(!RacePlaylist::with_songs({}, 7).has_songs());
    }
    {   // Saved and loaded mid-pass, it carries on identically (the next race continues it).
        RacePlaylist p = RacePlaylist::with_songs(ids, 0xBEEF);
        p.next(), p.next();
        std::optional<RacePlaylist> q = RacePlaylist::from_text(p.to_text());
        CHECK(q && *q == p);
        if (q)
            for (int i = 0; i < 12; ++i) CHECK(p.next() == q->next());
        CHECK(!RacePlaylist::from_text("") && !RacePlaylist::from_text("2 0 1 0 5"));
        CHECK(!RacePlaylist::from_text("1 0 1 0 5 nonsense"));
        // A damaged order (no song at the cursor's place) starts a new pass instead of waiting.
        std::optional<RacePlaylist> d = RacePlaylist::from_text("1 0 0 0 9 0000000000001111:1:0:5 0000000000002222:1:0:6");
        CHECK(d && d->next() == 0u && d->next() == 1u);
    }
    // The picked song's BGM track: its file's place in the race-song group, 0 if absent.
    CHECK(RacePlaylist::track_index(0x3333, ids) == 2 && RacePlaylist::track_index(0x9999, ids) == 0);
}

// The Replay Theater page plays stream 0x1A ("Are You Gonna Go My Way", looping) at the
// menu level (music §4.1); the title page is silent.
TEST(sound_replay_theater_music) {
    CHECK(menu::flow::page_music("gt_mode", "replay_theater") == "bgm:" + std::to_string(bgm::kReplayTheater));
    CHECK(bgm::kReplayTheater == 0x1A && bgm::is_stream(bgm::kReplayTheater));
    CHECK(menu::flow::page_music("gt_mode", "root").empty());
    CHECK(menu::flow::page_music("gt_mode", "lc/lc_b") == "license01");
}

// Full throttle through the gears in a turbo car (the R34 on the machine tests' straight, the game's own physics): the
// processed throttle drops to 0 in every upshift, so every upshift with the load above 0.8 fires the blow-off (sound §7.5).
// The pad's throttle, held at 1, never would. Needs the disc.
TEST(sound_turbo_blow_off_on_upshift) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    data::CarDatabase db;
    CHECK(db.load(disc->volume()));
    test::StraightRun run = test::straight_run(*disc, db, "r34_gtr_v_99");
    CHECK(run.field != nullptr);
    if (!run.field) return;
    race::RaceVehicle& car = run.field->player();
    CHECK(car.facts() && car.facts()->turbo);
    sim::ControlInputs in;
    in.throttle = 1;
    in.controller_kind = 2;
    car_sound::Turbo processed, pad;
    float prev_load = 0;
    int gear = car.state().gear, upshifts = 0, fired = 0, fired_pad = 0, late = 0, last_shift = -100;
    float loudest = 0;
    for (int i = 0; i < 60 * 20; ++i) {
        run.step(in);
        if (run.field->race_time() <= 0) continue; // (the start's hold)
        race::CarExtraSounds::Step st = race::CarExtraSounds::step_inputs(*car.telemetry(), car.facts()->turbo);
        processed.step(st.turbo, st.throttle, prev_load, st.load, st.torque, st.lag0, st.lag1);
        pad.step(st.turbo, in.throttle, prev_load, st.load, st.torque, st.lag0, st.lag1);
        prev_load = st.load;
        processed.frame();
        pad.frame();
        if (car.state().gear > gear && gear >= 1) ++upshifts, last_shift = i;
        gear = car.state().gear;
        if (processed.blow_off() > 0) {
            ++fired;
            late += i - last_shift > 2; // fires on the shift itself
            loudest = std::max(loudest, processed.blow_off());
        }
        fired_pad += pad.blow_off() > 0;
    }
    // In the cockpit (channel gain 0.2, 32 / d with d = 1 m) the one-shot plays above the
    // effects' 0.01 floor.
    const float cockpit = race::sfx_gain(race::SoundView::InCarTarget, race::kChBlowOff) * loudest * 32.0f;
    std::printf("  turbo: %d upshifts, %d blow-offs (pad throttle: %d), cockpit volume up to %.3f\n", upshifts,
                fired, fired_pad, cockpit);
    CHECK(upshifts >= 3 && fired == upshifts && late == 0);
    CHECK(fired_pad == 0);
    CHECK(cockpit > 0.01f);
}
