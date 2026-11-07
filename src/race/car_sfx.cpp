#include "race/car_sfx.h"

#include "base/log.h"

#include <algorithm>
#include <cmath>

namespace gt3r::race {

namespace {

constexpr float kGains[5][kChCount] = {
    // engine exhaust turbo b_off whine clash rumble in_car wind road rain squeal unused slide dirt
    {3.00f, 2.50f, 0.50f, 0.20f, 0.80f, 0.30f, 1.40f, 0.60f, 0.00f, 0.90f, 0.90f, 0.60f, 1.0f, 0.80f, 1.40f},
    {0.30f, 1.30f, 0.00f, 0.00f, 0.00f, 0.30f, 0.20f, 0.00f, 0.30f, 0.30f, 0.60f, 0.30f, 1.0f, 0.60f, 1.00f},
    {1.00f, 3.00f, 0.00f, 0.00f, 0.30f, 0.50f, 1.00f, 0.00f, 1.40f, 0.60f, 0.90f, 0.60f, 1.2f, 0.60f, 1.00f},
    {0.30f, 1.30f, 0.00f, 0.00f, 0.00f, 0.00f, 0.20f, 0.00f, 0.30f, 0.30f, 0.60f, 0.30f, 0.6f, 0.60f, 1.00f},
    {0.50f, 3.30f, 0.00f, 0.00f, 0.20f, 0.50f, 0.20f, 0.00f, 0.80f, 0.90f, 1.00f, 0.50f, 1.0f, 0.30f, 1.00f},
};

} // namespace

float sfx_gain(SoundView v, SfxChannel ch) { return kGains[int(v)][int(ch)]; }

TyreSound road_noise(float width, float rim_speed) {
    // Width factor: 0.5 up to 135 mm, 1.0 from 355 mm, linear between.
    float fw = width < 0.135000005f   ? 0.5f
               : 0.355000019f < width ? 1.0f
                                      : 0.5f + (width - 0.135000005f) * (1.0f - 0.5f) / 0.220000014f;
    float v = std::fabs(rim_speed), s = v / 100.0f;
    float vol = std::min(s * s * 2.54999995f * fw, 0.300000012f);
    // Pitch: 0.5 up to 30 km/h, 1.0 from 150 km/h, linear between.
    constexpr float v0 = 8.33333302f, v1 = 41.6666679f;
    float pitch = v < v0 ? 0.5f : v1 < v ? 1.0f : 0.5f + (v - v0) * (1.0f - 0.5f) / (v1 - v0);
    return {vol, pitch};
}

float wall_strength(float j) {
    float s = std::min(std::fabs(j) / 50000.0f, 1.0f);
    return s < 0.2f ? 0.0f : s;
}

float car_strength(float j) {
    float s = std::min(std::fabs(j) / 7500.0f, 1.0f);
    return s < 0.2f ? 0.0f : s;
}

float clash_volume(SoundView view, float strength, float distance) {
    return std::min(1.0f, sfx_gain(view, kChClash) * strength * 0.6f * 32.0f / std::max(distance, 0.25f));
}

const char* tyre_loop_effect(TyreLoop loop) {
    switch (loop) {
    case kLoopSqueal: return "on_skill";
    case kLoopSlide: return "off_skill";
    case kLoopDirt: return "off_roadnoiz";
    case kLoopRumble: return "rumble";
    case kLoopRoad: return "on_road";
    case kLoopRain: return "rain_noiz";
    default: return "";
    }
}

std::array<TyreSound, kTyreLoops> wheel_loops(SoundView v, const WheelSound& w, float doppler) {
    std::array<TyreSound, kTyreLoops> out{};
    // The slip loops: the wheel's slip intensity on the loop's surfaces, pitch from the skid pitch.
    auto slip = [&](TyreLoop l, SfxChannel ch) { out[size_t(l)] = {sfx_gain(v, ch) * w.smoke, doppler * w.pitch}; };
    switch (w.surface) {
    case 0: case 7: case 8: case 9: slip(kLoopSqueal, kChSquealTarmac); break;
    case 2: case 3: case 4: slip(kLoopSlide, kChSlide); break;
    case 5: slip(kLoopDirt, kChDirt); break;
    case 1: case 13: slip(kLoopRumble, kChRumble); break;
    case 6: case 12: case 14: slip(kLoopRain, kChRain); break;
    default: break;
    }
    // Road roar: tarmac, paving, loose and rough while the wheel is (or was just) on the ground.
    if (w.contact && (w.surface == 0 || (w.surface >= 7 && w.surface <= 9))) {
        TyreSound r = road_noise(w.tyre_width, w.rim_speed);
        out[kLoopRoad] = {sfx_gain(v, kChRoad) * r.vol, doppler * r.pitch};
    }
    return out;
}

Placement place_voice(float x, float z, float vol) {
    float d = std::sqrt(x * x + z * z);
    if (d < 0.25f) { // closer than 25 cm: moved back along z first
        z = z - 0.25f + d;
        d = std::sqrt(x * x + z * z);
    }
    d = std::max(d, 1e-6f);
    Placement p;
    p.x_over_d = x / d;
    p.z_over_d = z / d;
    float g = std::min(1.0f, 32.0f / d) * vol;
    float a = std::clamp((p.x_over_d + 1.0f) * 0.785398185f, 0.0f, 1.57079637f);
    p.left = std::cos(a) * g;
    p.right = std::sin(a) * g;
    float m = std::fabs(g);
    p.priority = m >= 1.0f ? 0 : std::min(255, int((1.0f - m) * 255.0f));
    return p;
}

float distance_gain(float x, float z) { return std::min(1.0f, 32.0f / std::max(std::hypot(x, z), 0.25f)); }

bool CarSfx::bind(const std::vector<std::shared_ptr<const sound::InstBank>>& banks,
                  const std::vector<sound::SoundEffect>& index) {
    bound_ = true;
    bool all = true;
    for (int l = 0; l < kTyreLoops; ++l) {
        int bank = -1, region = -1;
        bool ok = LoopEffect::resolve(index, tyre_loop_effect(TyreLoop(l)), 0, bank, region) && bank >= 0 &&
                  size_t(bank) < banks.size() && banks[size_t(bank)];
        all = all && ok;
        for (auto& wheel : loops_) wheel[size_t(l)].set(ok ? banks[size_t(bank)] : nullptr, region);
    }
    return all;
}

void CarSfx::update(RaceSfx& sfx, VoiceMixer& mixer, const Step& st) {
    if (!st.state || !st.setup || !st.telemetry) return;
    const sim::VehicleState& s = *st.state;
    if (!bound_ && sfx.loaded()) bind(sfx.banks(), sfx.index());
    for (size_t w = 0; w < 4; ++w) {
        const sim::WheelState& wh = s.wheels[w];
        // The road roar's grace: sounding on the ground and for 5 steps after leaving it.
        airborne_[w] = wh.grounded || wh.load > 0 ? 6 : std::max(0, airborne_[w] - 1);
        WheelSound ws;
        ws.surface = sim::surface_index(wh.surface);
        ws.smoke = st.telemetry->smoke[w];
        ws.pitch = st.telemetry->skid_pitch[w];
        ws.rim_speed = std::fabs(wh.angular_velocity) * st.setup->wheels[w].radius;
        ws.tyre_width = st.setup->wheels[w].width;
        ws.contact = airborne_[w] > 0;
        wheels_[w] = ws;
        // Every loop of the wheel is its own voice at the wheel, seen from the camera.
        sim::Vec3 o = wh.contact - st.eye;
        float x = o.dot(st.right), z = o.dot(st.forward);
        std::array<TyreSound, kTyreLoops> loops = wheel_loops(st.view, ws, st.doppler);
        for (size_t l = 0; l < size_t(kTyreLoops); ++l) {
            Placement p = place_voice(x, z, loops[l].vol);
            float left = p.left, right = p.right;
            if (mixer.surround_cue()) {
                SurroundCue& c = cues_[w][l];
                c.update(p.x_over_d, p.z_over_d);
                if (c.invert_l) left = -left;
                if (c.invert_r) right = -right;
            }
            out_[w][l] = {left, right, loops[l].pitch, p.priority};
            loops_[w][l].update(mixer, left, right, loops[l].pitch, p.priority);
        }
    }

    // Collisions: the strongest impact of the step, wall `clash` or car `clash2`, keyed again on
    // every step with a hit of strength 0.2 or more (no cooldown), centred. Only the followed car's
    // hits are heard; GT3 reads no other car's collisions.
    float ws = wall_strength(st.wall_impulse), cs = car_strength(st.car_impulse);
    if (ws > 0 || cs > 0) {
        bool car = cs >= ws;
        sim::Vec3 d = s.position - st.eye; // (horizontal in the camera's frame)
        sfx.play(mixer, car ? "clash2" : "clash", clash_volume(st.view, car ? cs : ws, std::hypot(d.dot(st.right), d.dot(st.forward))));
    }
}

void CarSfx::stop(VoiceMixer& mixer) {
    for (auto& wheel : loops_)
        for (LoopEffect& l : wheel) l.stop(mixer);
    for (auto& wheel : cues_)
        for (SurroundCue& c : wheel) c.reset();
    out_ = {};
}

// --- CarExtraSounds ---------------------------------------------------------------------

bool CarExtraSounds::load(Vfs& vfs) {
    std::string err;
    if (!wind_.load(vfs, EngineUnit::kWindBank, &err)) {
        LOG_WARN("Race: wind bank: %s", err.c_str());
        return false;
    }
    return true;
}

CarExtraSounds::Step CarExtraSounds::step_inputs(const sim::VehicleTelemetry& t, bool turbo) {
    Step st;
    st.turbo = turbo;
    st.throttle = t.throttle_processed;
    st.load = t.turbo_load;
    st.torque = t.engine_torque;
    st.lag0 = t.turbo_lag[0];
    st.lag1 = t.turbo_lag[1];
    return st;
}

void CarExtraSounds::step(const Step& s) {
    turbo_.step(s.turbo, s.throttle, prev_load_, s.load, s.torque, s.lag0, s.lag1);
    turbo_.tick();
    prev_load_ = s.load;
}

bool CarExtraSounds::bind(const std::vector<std::shared_ptr<const sound::InstBank>>& banks,
                          const std::vector<sound::SoundEffect>& index) {
    bound_ = true;
    bool all = true;
    const char* names[kLoops] = {car_sound::kWhine, car_sound::kTurboSpool, car_sound::kInCar};
    for (size_t l = 0; l < size_t(kLoops); ++l) {
        int bank = -1, region = -1;
        bool ok = LoopEffect::resolve(index, names[l], 0, bank, region) && bank >= 0 && size_t(bank) < banks.size() &&
                  banks[size_t(bank)];
        all = all && ok;
        loops_[l].set(ok ? banks[size_t(bank)] : nullptr, region);
    }
    return all;
}

void CarExtraSounds::update(RaceSfx& sfx, VoiceMixer& mixer, const Frame& f) {
    if (!bound_ && sfx.loaded()) bind(sfx.banks(), sfx.index());
    if (!f.wind) wind_.stop(mixer);
    else if (wind_.loaded()) {
        car_sound::Wind w = car_sound::wind(f.u, f.slipstream, f.cd);
        wind_.update_at(mixer, float(w.rpm), sfx_gain(f.view, kChWind) * distance_gain(f.x, f.z) * w.gain, f.x, f.z,
                        f.doppler);
    }
    // The loops: the channel gain x the model's, placed at (x, z) with min(1, 32/d); the whine's and the
    // in-car loop's pitch moves with the Doppler factor, the spool's does not.
    const car_sound::Loop wh = car_sound::whine(f.whine, f.clutch, f.torque, f.ratio, f.first_ratio, f.shaft);
    const car_sound::Loop sp = turbo_.spool(), ic = car_sound::in_car(f.u, f.slipstream, f.cd);
    const float vol[kLoops] = {sfx_gain(f.view, kChWhine) * wh.gain, sfx_gain(f.view, kChTurbo) * sp.gain,
                               sfx_gain(f.view, kChInCar) * ic.gain};
    const float pitch[kLoops] = {f.doppler * wh.pitch, sp.pitch, f.doppler * ic.pitch};
    for (size_t l = 0; l < size_t(kLoops); ++l) {
        Placement p = place_voice(f.x, f.z, vol[l]);
        float left = p.left, right = p.right;
        if (mixer.surround_cue()) {
            cues_[l].update(p.x_over_d, p.z_over_d);
            if (cues_[l].invert_l) left = -left;
            if (cues_[l].invert_r) right = -right;
        }
        out_[l] = {left, right, pitch[l], p.priority};
        loops_[l].update(mixer, left, right, pitch[l], p.priority);
    }
    if (const float b = turbo_.take_blow_off(); b > 0) { // at the car itself, 32/d not clamped
        float d = std::max(std::hypot(f.car_x, f.car_z), 0.25f);
        sfx.play(mixer, car_sound::kBlowOff, sfx_gain(f.view, kChBlowOff) * b * 32.0f / d, 1,
                 EngineSound::pan_for_offset(f.car_x, f.car_z));
    }
}

void CarExtraSounds::stop(VoiceMixer& mixer) {
    wind_.stop(mixer);
    for (LoopEffect& l : loops_) l.stop(mixer);
    for (SurroundCue& c : cues_) c.reset();
    out_ = {};
}

} // namespace gt3r::race
