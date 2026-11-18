// Sound test: browse and play the game's sound effects through the SPU2-style
// voice mixer, and rev any car's engine with the pressure-sensitive Cross
// button (analog trigger in the default bindings). Effects whose region has
// the effect-send flag go through the reverb (R2 toggles the game's studio C
// setting; L2 switches Gaussian/cubic interpolation).

#include "app/app.h"
#include "app/screen_registry.h"
#include "audio/engine_sound.h"
#include "formats/sound/inst.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdio>
#include <memory>

namespace gt3r {

namespace {

constexpr const char* kInstBanks[] = {"sound/gt3sys.ins", "sound/gt3se01.ins", "sound/gt3se02.ins",
                                      "sound/menu.ins",   "sound/sys.ins",     "sound/se01.ins",
                                      "music/music.ins"};

class SoundScreen : public Screen {
public:
    ~SoundScreen() override = default;

    void update(App& app, double dt) override {
        if (!loaded_) load(app);
        VoiceMixer& mixer = app.audio().voices();
        const MenuInput& m = app.menu();
        const PadState& pad = app.input().pad(0);

        // Effects list.
        int n = int(entries_.size());
        if (n) sel_ = std::clamp(sel_ + m.vertical(), 0, n - 1);
        if (m.pressed(PadButton::Square) && n) play_effect(mixer, entries_[sel_]);
        if (m.pressed(PadButton::Circle)) stop_effects(mixer);
        std::erase_if(fx_voices_, [&](VoiceId v) { return !mixer.playing(v); });

        // Engine selection.
        int ne = int(engine_ids_.size());
        if (ne && (m.horizontal() || m.pressed(PadButton::L1) || m.pressed(PadButton::R1))) {
            int step = m.horizontal() + (m.pressed(PadButton::R1) ? 10 : 0) - (m.pressed(PadButton::L1) ? 10 : 0);
            engine_sel_ = std::clamp(engine_sel_ + step, 0, ne - 1);
            load_engine(app);
        }
        if (m.pressed(PadButton::Start)) {
            variant_ = (variant_ + 1) % 8;
            load_engine(app);
        }
        if (m.pressed(PadButton::Select))
            engine_.set_view(EngineSound::View((int(engine_.view()) + 1) % int(EngineSound::View::Count)));
        if (m.pressed(PadButton::R2))
            mixer.set_reverb(mixer.reverb_mode() == SpuReverb::Off ? SpuReverb::StudioC : SpuReverb::Off, 0x0FFF,
                             0x0FFF);
        if (m.pressed(PadButton::L2))
            mixer.set_interpolation(mixer.interpolation() == VoiceMixer::Interpolation::Gaussian
                                        ? VoiceMixer::Interpolation::Cubic
                                        : VoiceMixer::Interpolation::Gaussian);

        // Engine simulation: throttle from Cross pressure, rpm chases a
        // throttle-dependent target with a rev limiter.
        throttle_ = pad.press(PadButton::Cross) / 255.0f;
        if (engine_.loaded()) {
            float thr = throttle_;
            if (limiter_ > 0) {
                limiter_ -= dt;
                thr = 0;
            }
            float target = idle_ + thr * (redline_ + 400 - idle_);
            float rate = target > rpm_ ? 2500.0f + 6000.0f * thr : 3500.0f;
            float step = rate * float(dt);
            rpm_ = target > rpm_ ? std::min(target, rpm_ + step) : std::max(target, rpm_ - step);
            if (rpm_ >= redline_) {
                rpm_ = redline_;
                limiter_ = 0.06;
            }
            engine_.update(mixer, rpm_, thr, dt);
        }

        if (m.pressed(PadButton::Triangle)) {
            engine_.stop(mixer);
            stop_effects(mixer);
            app.pop();
        }
    }

    void render(App& app, Ui& ui) override {
        char hint[96];
        const VoiceMixer& mx = app.audio().voices();
        std::snprintf(hint, sizeof(hint), "%d voices  reverb %s  %s", mx.active_voices(),
                      SpuReverb::mode_name(mx.reverb_mode()),
                      mx.interpolation() == VoiceMixer::Interpolation::Gaussian ? "gauss" : "cubic");
        ui.header("SOUND TEST", hint);
        if (!error_.empty()) ui.text(10, 30, error_, colors::kBad);
        render_effects(ui);
        render_engine(ui);
        ui.footer("{Square} play  {Circle} stop  {Cross} throttle  {Left/Right}/{L1}/{R1} car  {Start} variant  {Select} view  {R2} rev  {L2} interp");
    }

private:
    struct Entry {
        std::string label;
        int bank = 0;
        int first = 0, last = 0;
        bool loops = false;
        bool wet = false; // effect send (region flags bit 0)
    };

    static constexpr int kRows = 24;

    void load(App& app) {
        loaded_ = true;
        Vfs& vfs = *app.vfs();
        for (const char* path : kInstBanks) {
            std::shared_ptr<sound::InstBank> b;
            if (auto bytes = vfs.read(path)) {
                try {
                    b = std::make_shared<sound::InstBank>(sound::InstBank::parse(*bytes));
                } catch (const std::exception& e) {
                    error_ = std::string(path) + ": " + e.what();
                }
            }
            banks_.push_back(b);
        }
        // Named effects first, then every region of every bank.
        if (auto inf = vfs.read("sound/se.inf")) {
            try {
                for (const sound::SoundEffect& e : sound::parse_se_index(*inf))
                    if (e.bank < 3 && banks_[e.bank] && e.last < banks_[e.bank]->regions.size())
                        entries_.push_back({e.name, e.bank, e.first, e.last, loops(e.bank, e.first),
                                            banks_[e.bank]->regions[e.first].reverb()});
            } catch (const std::exception& e) {
                error_ = std::string("se.inf: ") + e.what();
            }
        }
        for (size_t b = 0; b < banks_.size(); ++b) {
            if (!banks_[b]) continue;
            std::string name = kInstBanks[b];
            name = name.substr(name.find('/') + 1);
            for (size_t r = 0; r < banks_[b]->regions.size(); ++r) {
                char label[48];
                std::snprintf(label, sizeof(label), "%s #%zu", name.c_str(), r);
                entries_.push_back({label, int(b), int(r), int(r), loops(int(b), int(r)),
                                    banks_[b]->regions[r].reverb()});
            }
        }
        for (const std::string& f : vfs.list_dir("sound/engine")) {
            if (f.empty() || f.back() == '/') continue;
            if (f.size() == 5) engine_ids_.push_back(std::atoi(f.c_str()));
        }
        std::sort(engine_ids_.begin(), engine_ids_.end());
        load_engine(app);
    }

    bool loops(int bank, int region) const {
        const auto& b = banks_[bank];
        return b && b->decode_region(region).looped;
    }

    void load_engine(App& app) {
        engine_.stop(app.audio().voices());
        if (engine_ids_.empty()) return;
        std::string err;
        if (!engine_.load(*app.vfs(), engine_ids_[engine_sel_], variant_, &err)) {
            error_ = err;
            return;
        }
        const auto* ref = engine_.engine_bank() ? engine_.engine_bank() : engine_.exhaust_bank();
        idle_ = std::max(800.0f, float(ref->layers.front().base_rpm));
        redline_ = float(ref->layers.back().base_rpm) + 500.0f;
        rpm_ = idle_;
    }

    void play_effect(VoiceMixer& mixer, const Entry& e) {
        const auto& b = banks_[e.bank];
        for (int r = e.first; r <= e.last; ++r) {
            const sound::InstRegion& reg = b->regions[r];
            int l, rr;
            sound::pan_gains(reg.pan, l, rr);
            VoiceParams p{b->adpcm,
                          reg.address,
                          reg.pitch(),
                          s16(std::min(0x3FFF, (reg.volume * l) >> 14)),
                          s16(std::min(0x3FFF, (reg.volume * rr) >> 14)),
                          reg.adsr1,
                          reg.adsr2,
                          reg.reverb(),
                          reg.priority};
            VoiceId v = mixer.key_on(p);
            if (v.valid()) fx_voices_.push_back(v);
        }
        playing_ = e.label;
    }

    void stop_effects(VoiceMixer& mixer) {
        for (VoiceId v : fx_voices_) mixer.key_off(v);
        fx_voices_.clear();
    }

    void render_effects(Ui& ui) {
        const float x0 = 6, w = 250;
        ui.text(x0, 24, "EFFECTS", colors::kAccent);
        int n = int(entries_.size());
        int first = ui.list_window(n, sel_, kRows - 2);
        for (int i = 0; i < kRows - 2 && first + i < n; ++i) {
            int idx = first + i;
            const Entry& e = entries_[idx];
            float y = 36 + i * 12.0f;
            if (idx == sel_) ui.fill(x0 - 2, y - 2, w, 12, colors::kHighlight);
            ui.text(x0, y, e.label, e.label == playing_ && !fx_voices_.empty() ? colors::kGood : colors::kText);
            char info[32];
            if (e.first == e.last) std::snprintf(info, sizeof(info), "%s%s", e.wet ? "wet " : "", e.loops ? "loop" : "");
            else
                std::snprintf(info, sizeof(info), "%s%dch%s", e.wet ? "wet " : "", e.last - e.first + 1,
                              e.loops ? " loop" : "");
            ui.text_right(x0 + w - 6, y, info, colors::kDim);
        }
        if (n) {
            const Entry& e = entries_[sel_];
            const auto& r = banks_[e.bank]->regions[e.first];
            char line[96];
            std::snprintf(line, sizeof(line), "%u Hz  vol %d  %s", unsigned(std::lround(sound::game_pitch_rate(r.pitch()))),
                          r.volume, r.reverb() ? "reverb send" : "dry");
            ui.text(x0, Ui::kHeight - 40, line, colors::kDim);
        }
    }

    void render_engine(Ui& ui) {
        const float x0 = 268;
        ui.fill(x0 - 6, 22, Ui::kWidth - x0, Ui::kHeight - 50, colors::kPanel);
        char line[96];
        int id = engine_ids_.empty() ? 0 : engine_ids_[engine_sel_];
        std::snprintf(line, sizeof(line), "ENGINE %05d  %s", id, EngineSound::variant_dir(variant_).c_str());
        ui.text(x0, 26, line, colors::kAccent);
        std::snprintf(line, sizeof(line), "view: %s", EngineSound::view_name(engine_.view()));
        ui.text(x0, 38, line, colors::kDim);
        if (!engine_.loaded()) return;

        // Tachometer: 240-degree arc from 0 to redline + 1000.
        const float cx = x0 + 90, cy = 140, rad = 74;
        const float max_rpm = std::ceil((redline_ + 1000) / 1000) * 1000;
        SDL_Renderer* r = ui.renderer();
        auto angle = [&](float rpm) { return float(std::numbers::pi) * (1.25f - 1.5f * rpm / max_rpm); };
        for (int k = 0; k <= int(max_rpm / 500); ++k) {
            float rpm = k * 500.0f, a = angle(rpm);
            bool major = k % 2 == 0;
            Color c = rpm >= redline_ ? colors::kBad : colors::kText;
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
            float r0 = major ? rad - 10 : rad - 5;
            SDL_RenderLine(r, cx + r0 * std::cos(a), cy - r0 * std::sin(a), cx + rad * std::cos(a),
                           cy - rad * std::sin(a));
            if (major) {
                char num[12];
                std::snprintf(num, sizeof(num), "%d", k / 2);
                ui.text(cx + (rad - 20) * std::cos(a) - 4, cy - (rad - 20) * std::sin(a) - 4, num, c);
            }
        }
        for (int s = 0; s < 60; ++s) {
            float a0 = angle(max_rpm * s / 60), a1 = angle(max_rpm * (s + 1) / 60);
            SDL_RenderLine(r, cx + rad * std::cos(a0), cy - rad * std::sin(a0), cx + rad * std::cos(a1),
                           cy - rad * std::sin(a1));
        }
        float a = angle(rpm_);
        SDL_SetRenderDrawColor(r, colors::kAccent.r, colors::kAccent.g, colors::kAccent.b, 255);
        for (float d = -1; d <= 1; d += 0.5f)
            SDL_RenderLine(r, cx + d, cy + d, cx + (rad - 6) * std::cos(a) + d, cy - (rad - 6) * std::sin(a));
        ui.text(cx - 20, cy + 22, "x1000", colors::kDim);

        // Throttle and bank levels.
        const float bx = x0 + 196, bw = Ui::kWidth - bx - 12;
        const auto& st = engine_.state();
        ui.text(bx, 64, "throttle ({Cross})", colors::kDim);
        ui.bar(bx, 76, bw, 6, throttle_, colors::kAccent);
        ui.text(bx, 90, "engine bank", colors::kDim);
        ui.bar(bx, 102, bw, 6, st.level[0] / float(EngineSound::kMaxLevel), colors::kGood);
        ui.text(bx, 116, "exhaust bank", colors::kDim);
        ui.bar(bx, 128, bw, 6, st.level[1] / float(EngineSound::kMaxLevel), colors::kGood);
        std::snprintf(line, sizeof(line), "%.0f rpm", rpm_);
        ui.text(bx, 150, line, colors::kText);
        std::snprintf(line, sizeof(line), "redline %.0f", redline_);
        ui.text(bx, 164, line, colors::kDim);

        // Layers of each bank with their crossfade gains.
        const sound::EngineBank* banks[2] = {engine_.engine_bank(), engine_.exhaust_bank()};
        const char* names[2] = {"engine/", "exhaust/"};
        float y = 232;
        for (int b = 0; b < 2; ++b, y += 26) {
            ui.text(x0, y, names[b], colors::kDim);
            if (!banks[b]) continue;
            const auto& layers = banks[b]->layers;
            float lw = (Ui::kWidth - x0 - 80) / float(layers.size());
            for (size_t i = 0; i < layers.size(); ++i) {
                float lx = x0 + 70 + i * lw;
                float g = 0;
                for (int k = 0; k < 2; ++k)
                    if (st.bank[b].layer[k] == int(i)) g = st.bank[b].gain[k];
                ui.frame(lx, y, lw - 3, 10, colors::kDim);
                if (g > 0) ui.fill(lx + 1, y + 1, (lw - 5) * g, 8, colors::kGood);
                char rl[8];
                std::snprintf(rl, sizeof(rl), "%.1f", layers[i].base_rpm / 1000.0f);
                ui.text(lx, y + 12, rl, g > 0 ? colors::kText : colors::kDim);
            }
        }
    }

    bool loaded_ = false;
    std::string error_;
    std::vector<std::shared_ptr<sound::InstBank>> banks_;
    std::vector<Entry> entries_;
    int sel_ = 0;
    std::vector<VoiceId> fx_voices_;
    std::string playing_;

    std::vector<int> engine_ids_;
    int engine_sel_ = 0;
    int variant_ = 0;
    EngineSound engine_;
    float idle_ = 800, redline_ = 7000, rpm_ = 800, throttle_ = 0;
    double limiter_ = 0;
};

} // namespace

GT3R_REGISTER_SCREEN(sound, "sound", "Sound test", "Play sound effects and rev engines with the Cross button.", 25,
                     [] { return std::make_unique<SoundScreen>(); });

} // namespace gt3r
