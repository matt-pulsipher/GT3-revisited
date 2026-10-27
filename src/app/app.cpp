#include "app/app.h"
#include "base/hitch.h"
#include "app/screen_registry.h"
#include "app/updates.h"

#include "base/log.h"
#include "gt3r_version.h"
#include "formats/png_writer.h"
#include "platform/recent_discs.h"
#include "platform/render_scale.h"
#include "platform/mac_layer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace gt3r {

namespace {

const SDL_DialogFileFilter kDiscFilters[] = {
    {"PS2 disc images", "iso"},
    {"All files", "*"},
};

// Saved disc paths must survive launches from a different working directory
// (e.g. the app icon), so a relative path is made absolute. URIs such as
// Android's content:// are kept as they are.
std::string absolute_disc_path(const std::string& path) {
    if (path.empty() || path.find("://") != std::string::npos) return path;
    std::error_code ec;
    std::filesystem::path p(std::u8string(path.begin(), path.end())); // UTF-8 on every platform
    if (p.is_absolute()) return path;
    std::filesystem::path abs = std::filesystem::absolute(p, ec);
    if (ec) return path;
    const std::u8string out = abs.lexically_normal().u8string();
    return std::string(out.begin(), out.end());
}

void SDLCALL on_dialog(void* user, const char* const* files, int) {
    auto* app = static_cast<App*>(user);
    if (files && files[0]) app->set_dialog_result(files[0]);
}

} // namespace

bool App::init(int argc, char** argv) {
    SDL_SetAppMetadata("GT3 Revisited", GT3R_VERSION, "org.gt3revisited.gt3r");
#if GT3R_XR
    // In a headset the (invisible) window never has keyboard focus, and SDL drops joystick
    // input without it: the controllers' virtual gamepad and Bluetooth pads alike.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_SENSOR)) {
        LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    // Portable mode: a portable.ini next to the executable keeps all user
    // data beside it (handy for USB sticks); otherwise use the OS location.
    const char* base = SDL_GetBasePath();
    if (base && host_path_exists(std::string(base) + "portable.ini")) {
        data_dir_ = base;
    } else if (char* pref = SDL_GetPrefPath("GT3Revisited", "GT3Revisited")) {
        data_dir_ = pref;
        SDL_free(pref);
    }
    SDL_CreateDirectory(mods_dir().c_str());
    config_path_ = data_dir_ + "config.ini";
    if (auto cfg = IniDocument::load(config_path_)) config_ = std::move(*cfg);
    LOG_INFO("User data: %s", data_dir_.c_str());

    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight"); // mobile: racing is landscape
    // macOS: the menu bar (and the title bar's green button) appears at the top of the screen in
    // fullscreen as in other apps; SDL's "auto" hides it when the app went fullscreen itself (our
    // [video] fullscreen = on at start), leaving no way out but F11.
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, "1");
    SDL_WindowFlags win_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    win_flags |= SDL_WINDOW_FULLSCREEN; // immersive: hide system bars
#endif
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    // [video] fullscreen = on | off (desktop; F11 and the Settings screen change it)
    if (to_lower(config_.get("video", "fullscreen").value_or("off")) == "on") win_flags |= SDL_WINDOW_FULLSCREEN;
#endif
    // GT3R_PHONE=1 (desktop testing): a window shaped like a 2340x1080 phone
    // with a display cutout on the left and rounded corners, phone-sized UI.
    if (const char* p = SDL_getenv("GT3R_PHONE"); p && *p == '1' && !platform::is_mobile()) {
        phone_sim_ = true;
        win_flags &= ~SDL_WINDOW_FULLSCREEN;
    }
    touch_ui_ = platform::is_mobile() || phone_sim_;
    window_ = SDL_CreateWindow("GT3 Revisited", phone_sim_ ? 1170 : 1280, phone_sim_ ? 540 : 720, win_flags);
    if (!window_) {
        LOG_ERROR("Window creation failed: %s", SDL_GetError());
        return false;
    }
    if (phone_sim_) { // half the phone's 2340x1080 pixels (fits on laptop screens), whatever the density
        float d = SDL_GetWindowPixelDensity(window_);
        if (d > 0.1f) SDL_SetWindowSize(window_, int(std::lround(1170 / d)), int(std::lround(540 / d)));
    }
    // Preferred: an SDL_GPU device shared by the 3D renderer and the 2D UI
    // renderer. Fallback: a plain SDL_Renderer (native Metal/D3D/Vulkan/GLES
    // backend) and no 3D. [video] gpu=0 or --no-gpu forces the fallback.
    render::GpuOptions gpu_opts;
    bool use_gpu = config_.get("video", "gpu").value_or("1") != "0";
    gpu_opts.msaa = platform::default_msaa(platform::is_mobile());
    if (auto v = config_.get("video", "msaa")) gpu_opts.msaa = std::atoi(v->c_str());
    if (auto v = config_.get("video", "vsync")) gpu_opts.vsync = *v != "0";
    if (auto v = config_.get("video", "gpu_debug")) gpu_opts.debug = *v == "1";
    // [video] render_scale = auto | 0.5..1.0 (auto: native on desktop, capped on mobile)
    std::optional<float> render_scale = platform::parse_render_scale(config_.get("video", "render_scale").value_or("auto"));
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-gpu") use_gpu = false;
        else if (a == "--msaa" && i + 1 < argc) gpu_opts.msaa = std::atoi(argv[++i]);
        else if (a == "--render-scale" && i + 1 < argc)
            render_scale = platform::parse_render_scale(argv[++i]), render_scale_arg_ = true;
    }
    int win_pw = 0, win_ph = 0;
    SDL_GetWindowSizeInPixels(window_, &win_pw, &win_ph);
    gpu_opts.render_scale = render_scale ? *render_scale
                                         : platform::auto_render_scale(win_pw, win_ph, platform::is_mobile());
    configured_scale_ = gpu_opts.render_scale;
    scale_auto_ = !render_scale;
#if GT3R_XR
    // A headset when there is one ([vr] enabled = off or --no-vr: the window, as on a phone).
    gpu_opts.xr = to_lower(config_.get("vr", "enabled").value_or("on")) != "off";
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--no-vr") gpu_opts.xr = false;
#endif
    if (use_gpu) gpu_ = render::GpuContext::create(window_, gpu_opts);
#if GT3R_XR
    if (gpu_ && gpu_->xr()) {
        xr_ = xr::Session::create(gpu_->device(), gpu_->renderer2d(), gpu_->xr_request(), xr::Settings::load(config_));
        if (xr_) {
            gpu_->set_render_scale(1.0f); // eyes are sized by [vr] resolution; the canvas is small
        } else { // a headset runtime but no working session: the window after all
            LOG_WARN("VR: session setup failed; using the window");
            gpu_.reset();
            gpu_opts.xr = false;
            gpu_ = render::GpuContext::create(window_, gpu_opts);
        }
    }
    if (xr_) touch_ui_ = false;
#endif
    if (gpu_) {
        renderer_ = gpu_->renderer2d();
        if (platform::is_mobile()) gpu_->warm_pipelines(); // ~65 ms each on Mali: not mid-race
        // macOS switches a fullscreen window between direct-to-display and composited now and
        // then, and each switch stalls presentation for up to half a second (measured: 0 frames
        // over 40 ms per minute composited vs up to 13 direct). GT3R_DIRECT=1 for comparisons.
        if (!SDL_getenv("GT3R_DIRECT")) platform::keep_composited(window_);
    } else {
        if (use_gpu) LOG_WARN("3D rendering unavailable; continuing with the 2D renderer only");
        renderer_ = SDL_CreateRenderer(window_, nullptr); // nullptr: platform's native backend
        if (!renderer_) {
            LOG_ERROR("Renderer creation failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetRenderVSync(renderer_, gpu_opts.vsync ? 1 : 0);
    }
    layout_ui(false);
    LOG_INFO("Renderer: %s", SDL_GetRendererName(renderer_));
    if (limit_60fps()) apply_frame_limit();
    ui_ = std::make_unique<Ui>(renderer_);

    input_ = std::make_unique<InputManager>();
    input_->load(config_);
    touch_.load(config_);
    audio_ = std::make_unique<AudioEngine>();
    if (auto vol = config_.get("audio", "music_volume")) audio_->set_music_volume(std::strtof(vol->c_str(), nullptr));
    if (auto vol = config_.get("audio", "sfx_volume")) audio_->set_sfx_volume(std::strtof(vol->c_str(), nullptr));
    if (auto v = config_.get("audio", "surround_cue")) audio_->voices().set_surround_cue(*v == "on");
    dialog_mutex_ = SDL_CreateMutex();

    // gt3r [disc.iso] [--browse <path in GT3.VOL>] [--screen <name>] [--shot <out.png>]
    // Platforms without a command line (Android) can put one argument per line
    // in <user data>/args.txt.
    std::vector<std::string> args(argv + 1, argv + argc);
    if (auto extra = read_host_file(data_dir_ + "args.txt")) {
        std::string_view text(reinterpret_cast<const char*>(extra->data()), extra->size());
        for (std::string_view line : split(text, '\n'))
            if (!trim(line).empty()) args.emplace_back(trim(line));
    }
    std::string disc_path, browse, screen;
    bool hub = to_lower(config_.get("app", "start").value_or("")) == "hub";
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        bool has_value = i + 1 < args.size();
        if (a == "--browse" && has_value) browse = args[++i];
        else if (a == "--shot" && has_value) auto_shot_path_ = args[++i];
        else if (a == "--screen" && has_value) screen = args[++i];
        else if ((a == "--msaa" || a == "--render-scale") && has_value) ++i;
        else if (a == "--hub") hub = true;
        else if (a == "--env" && has_value) { // NAME=value: the testing variables on platforms without them (Android)
            const std::string& kv = args[++i];
            size_t eq = kv.find('=');
            if (eq != std::string::npos && eq > 0)
                SDL_SetEnvironmentVariable(SDL_GetEnvironment(), kv.substr(0, eq).c_str(), kv.substr(eq + 1).c_str(), true);
        }
        else if (!a.empty() && a[0] != '-') disc_path = a;
    }
    if (!disc_path.empty()) last_error_ = open_disc(disc_path);
    else open_saved_disc(config_.get("game", "disc").value_or(""));
    push(disc_ ? make_hub_screen() : make_setup_screen());
    // Normal start is the game itself (opening movie, then the title menu);
    // the home menu sits underneath (backing out of the title menu shows it).
    // --hub or [app] start = hub opens Developer options instead.
    if (disc_ && hub && browse.empty() && screen.empty()) push(make_developer_screen());
    else if (disc_ && browse.empty() && screen.empty()) push_game_start();
    if (disc_ && !browse.empty()) push(make_browser_screen(browse));
    if (!screen.empty())
        for (const ScreenEntry& entry : screen_registry())
            if (screen == entry.id && (disc_ || !entry.needs_disc)) push(entry.factory());
    // The update check (README "Online"; [online] update_check): in the background, never waited for.
    // --shot runs use only the cached answer (no traffic to a live server) unless GT3R_UPDATE_CHECK=1.
    const char* uc = SDL_getenv("GT3R_UPDATE_CHECK");
    updates().startup(*this, auto_shot_path_.empty() || (uc && *uc == '1'));
    reset_dynamic_scale();
    last_ticks_ = SDL_GetTicksNS();
    return true;
}

void App::push_game_start() {
    for (const ScreenEntry& entry : screen_registry())
        if (std::string_view(entry.id) == "boot") {
            push(entry.factory());
            return;
        }
}

void App::open_saved_disc(const std::string& saved) {
    if (saved.empty()) return;
    // Old configs may hold a path relative to wherever gt3r was started:
    // try the working directory, then the executable's directory.
    const char* base = SDL_GetBasePath();
    for (const std::string& candidate : platform::disc_path_candidates(saved, base ? base : "")) {
        bool uri = candidate.find("://") != std::string::npos;
        if (!uri && !host_path_exists(candidate)) continue;
        last_error_ = open_disc(candidate);
        if (disc_) return;
    }
    if (last_error_.empty()) last_error_ = "File not found (moved, renamed or on a drive that is not connected).";
    failed_disc_path_ = saved;
    LOG_WARN("Remembered disc image not available: %s", saved.c_str());
}

std::string App::open_disc(const std::string& path) {
    const std::string full = absolute_disc_path(path);
    try {
        auto disc = Disc::open(full);
        close_disc();
        disc_ = std::move(disc);
        vfs_ = std::make_unique<Vfs>(&disc_->volume());
        vfs_->scan_mods(mods_dir(), &config_);
        config_.set("game", "disc", full);
        platform::save_recent_discs(config_, platform::push_recent_disc(recent_discs(), full));
        save_config();
        last_error_.clear();
        failed_disc_path_.clear();
        return "";
    } catch (const std::exception& e) {
        LOG_ERROR("Could not open disc: %s", e.what());
        failed_disc_path_ = full;
        return e.what();
    }
}

std::vector<std::string> App::recent_discs() const {
    std::vector<std::string> list = platform::load_recent_discs(config_);
    // Configs from before the recent list: the remembered disc is the first entry.
    if (list.empty())
        if (auto d = config_.get("game", "disc"); d && !d->empty()) list.push_back(*d);
    return list;
}

void App::forget_recent_disc(const std::string& path) {
    platform::save_recent_discs(config_, platform::remove_recent_disc(platform::load_recent_discs(config_), path));
    save_config();
}

bool App::fullscreen() const { return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN); }

void App::set_fullscreen(bool on) {
    if (platform::is_mobile()) return; // always fullscreen
    SDL_SetWindowFullscreen(window_, on);
    config_.set("video", "fullscreen", on ? "on" : "off");
    save_config();
}

// [video] limit_60fps: on high-refresh displays, draw every Nth refresh (120 Hz -> 60 fps,
// 144 -> 72, 240 -> 60; 60 and 90 Hz -> every refresh) as SDL's main loop rate (SDL's renderers
// only offer vsync on or off), which with vsync still presents on a refresh.
void App::apply_frame_limit() {
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_));
    const double hz = mode ? mode->refresh_rate : 0;
    if (hz <= 0) return;
    const int interval = limit_60fps() ? std::max(1, int(std::floor(hz / 60.0 + 0.25))) : 1;
    const double rate = interval > 1 ? hz / interval : 0; // 0: as fast as vsync allows
    if (rate == frame_limit_rate_) return;
    frame_limit_rate_ = rate;
    if (rate > 0) SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, std::to_string(rate).c_str());
    else SDL_ResetHint(SDL_HINT_MAIN_CALLBACK_RATE);
    LOG_INFO("Display %.0f Hz: drawing every %s", hz,
             interval == 1 ? "refresh" : (std::to_string(interval) + " refreshes").c_str());
}

bool App::limit_60fps() const { return to_lower(config_.get("video", "limit_60fps").value_or("off")) == "on"; }

void App::set_limit_60fps(bool on) {
    config_.set("video", "limit_60fps", on ? "on" : "off");
    save_config();
    apply_frame_limit();
}

bool App::dynamic_resolution() const {
    return to_lower(config_.get("video", "dynamic_resolution").value_or(platform::is_mobile() ? "on" : "off")) == "on";
}

void App::set_dynamic_resolution(bool on) {
    config_.set("video", "dynamic_resolution", on ? "on" : "off");
    config_.set("video", "dynamic_start", ""), race_scale_ = 0; // a fresh start
    save_config();
    reset_dynamic_scale();
}

void App::apply_render_scale() {
    std::optional<float> v = platform::parse_render_scale(config_.get("video", "render_scale").value_or("auto"));
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(window_, &pw, &ph);
    configured_scale_ = v ? *v : platform::auto_render_scale(pw, ph, platform::is_mobile());
    scale_auto_ = !v;
    render_scale_arg_ = false; // the setting rules from now on
    config_.set("video", "dynamic_start", ""), race_scale_ = 0; // a fresh start
    save_config();
    reset_dynamic_scale();
}

void App::note_race_scale() {
    if (dyn_on_) race_scale_ = dyn_.scale();
}

void App::remember_race_scale() {
    if (race_scale_ <= 0 || !dyn_on_ || !scale_auto_ || dyn_cycle_s() > 0) return;
    char v[16];
    std::snprintf(v, sizeof v, "%.2f", double(race_scale_));
    if (config_.get("video", "dynamic_start").value_or("") == v) return;
    config_.set("video", "dynamic_start", v);
    save_config();
}

// GT3R_DYN_CYCLE=<seconds> (testing): the dynamic scale sweeps from its ceiling to 50% and back
// every <seconds>, also in --shot runs, to check that resolution changes neither stall nor show.
double App::dyn_cycle_s() {
    static const double s = [] { const char* e = SDL_getenv("GT3R_DYN_CYCLE"); return e ? std::atof(e) : 0.0; }();
    return s;
}

void App::reset_dynamic_scale() {
    dyn_on_ = false;
    if (!gpu_) return;
#if GT3R_XR
    if (xr_) return; // the eyes are sized by [vr] resolution
#endif
    gpu_->set_render_scale(configured_scale_);
    gpu_->set_render_ceiling(0);
    dyn_on_ = (dynamic_resolution() && auto_shot_path_.empty() && !render_scale_arg_) || dyn_cycle_s() > 0;
    // The most the dynamic scale goes up to: the 3D resolution setting, or with Auto the screen's
    // full resolution (platform::dynamic_ceiling); it starts at the setting's size either way.
    float ceiling = configured_scale_;
    if (dyn_on_ && scale_auto_) {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(window_, &pw, &ph);
        ceiling = std::max(ceiling, platform::dynamic_ceiling(pw, ph));
    }
    // Where the last race's scale ended (Auto only: a manual setting starts at its own value).
    float start = configured_scale_;
    if (dyn_on_ && scale_auto_ && dyn_cycle_s() <= 0)
        if (auto v = config_.get("video", "dynamic_start"); v && !v->empty())
            if (float f = std::strtof(v->c_str(), nullptr); std::isfinite(f) && f > 0) {
                start = std::clamp(f, render::DynamicScale::kMin, ceiling);
                LOG_INFO("Dynamic resolution: starting at %d%% (the last race's)", int(std::lround(start * 100)));
            }
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_));
    dyn_.reset(ceiling, mode ? mode->refresh_rate : 0, start);
    if (dyn_on_) gpu_->set_render_scale(dyn_.scale()), gpu_->set_render_ceiling(ceiling); // targets sized once
}

void App::close_disc() {
    if (audio_) audio_->stop_music();
    vfs_.reset();
    disc_.reset();
}

void App::show_open_dialog() {
    SDL_ShowOpenFileDialog(on_dialog, this, window_, kDiscFilters, SDL_arraysize(kDiscFilters), nullptr, false);
}

void App::set_dialog_result(std::string path) {
    SDL_LockMutex(dialog_mutex_);
    dialog_result_ = std::move(path);
    SDL_UnlockMutex(dialog_mutex_);
}

void App::save_config() {
    input_->save(config_);
    touch_.save(config_);
    char vol[16];
    std::snprintf(vol, sizeof(vol), "%.2f", audio_ ? audio_->music_volume() : 0.8f);
    config_.set("audio", "music_volume", vol);
    std::snprintf(vol, sizeof(vol), "%.2f", audio_ ? audio_->sfx_volume() : 0.8f);
    config_.set("audio", "sfx_volume", vol);
    if (!config_.save(config_path_)) LOG_WARN("Could not save %s", config_path_.c_str());
}

void App::apply_screen_changes() {
    for (; pops_ > 0 && !screens_.empty(); --pops_) screens_.pop_back();
    pops_ = 0;
    for (auto& s : pending_) screens_.push_back(std::move(s));
    pending_.clear();
}

SDL_AppResult App::iterate() {
#if GT3R_XR
    // A headset paces the frames: wait for its next one (nothing to draw while its session is
    // not running, e.g. the headset is off).
    if (xr_) {
        xr_->poll();
        if (xr_->exit_requested()) return SDL_APP_SUCCESS;
        if (xr_->take_focus_lost() && !screens_.empty()) { // the system menu: races pause
            SDL_Event bg{};
            bg.type = SDL_EVENT_DID_ENTER_BACKGROUND;
            screens_.back()->on_event(*this, bg);
        }
        if (!xr_->begin_frame()) {
            SDL_Delay(10);
            last_ticks_ = SDL_GetTicksNS();
            xr_last_display_ = 0;
            return quit_ ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
        }
    }
#endif
    Uint64 now = SDL_GetTicksNS();
    double frame_s = double(now - last_ticks_) / 1e9;
#if GT3R_XR
    // Step by the headset's display times: even steps however the frame's work varies.
    if (xr_) {
        const s64 t = xr_->display_time();
        if (xr_last_display_ > 0 && t > xr_last_display_) frame_s = double(t - xr_last_display_) / 1e9;
        xr_last_display_ = t;
    }
#endif
    double dt = frame_s;
    last_ticks_ = now;
    if (dt > 0.1) dt = 0.1;
    // --shot: step exactly one 60 Hz frame per iteration, so a shot of frame N shows the same
    // simulation time on every run (comparison screenshots are repeatable).
    if (!auto_shot_path_.empty()) dt = 1.0 / 60.0;

    // Pick up a file chosen in the (possibly asynchronous) dialog.
    std::string chosen;
    SDL_LockMutex(dialog_mutex_);
    chosen.swap(dialog_result_);
    SDL_UnlockMutex(dialog_mutex_);
    if (!chosen.empty()) {
        last_error_ = open_disc(chosen);
        if (disc_) {
            screens_.clear();
            push(make_hub_screen());
            push_game_start();
        }
    }

    audio_->update();
    updates().poll(*this);
    apply_screen_changes();
    Screen* top = screens_.empty() ? nullptr : screens_.back().get();
    updates().frame(*this, top && top->update_prompts());
    const bool modal = updates().modal(); // the update dialog takes the input
    TouchLayout touch_layout = modal ? TouchLayout::None : top ? top->touch_layout() : TouchLayout::None;
#if GT3R_XR
    if (xr_) {
        // The menus' pointer, except while driving (the right trigger is the throttle there).
        const bool pointer = modal || (top && top->touch_layout() != TouchLayout::Driving);
        touch_layout = TouchLayout::None; // the controllers, never on-screen controls
        xr_->update_input(dt, pointer);
        SDL_UpdateJoysticks(); // the virtual gamepad's new state, this frame
        xr_pointer();
    }
#endif
    touch_.update(dt, touch_layout, *input_);
    input_->update(dt);
    if (modal || (top && top->wants_menu_input())) menu_.update(input_->pad(0), dt);
    else menu_.update(PadState{}, dt);

    Uint64 h_u0 = SDL_GetTicksNS();
    if (modal) updates().dialog_update(*this);
    else if (top) top->update(*this, dt);
    Uint64 h_u1 = SDL_GetTicksNS();
    apply_screen_changes();
    if (screens_.empty()) quit_ = true;

    // Phones: the display cutout and corner radius (retried until the
    // window has insets; refreshed when the safe area changes).
    if (platform::is_mobile() && !display_insets_ && --insets_retry_ <= 0) {
        display_insets_ = platform::query_display_insets();
        insets_retry_ = 30;
        if (display_insets_)
            LOG_INFO("Display cutout %d,%d,%d,%d px, corner radius %d px", display_insets_->left, display_insets_->top,
                     display_insets_->right, display_insets_->bottom, display_insets_->corner_radius);
    }
#if GT3R_XR
    if (xr_) SDL_SetRenderTarget(renderer_, xr_->canvas()); // the frame goes into the canvas
#endif
    layout_ui(!screens_.empty() && screens_.back()->adaptive_ui());
    ui_->clear();
    Uint64 h_r0 = SDL_GetTicksNS();
    if (!screens_.empty()) screens_.back()->render(*this, *ui_);
    updates().dialog_render(*ui_);
    Uint64 h_r1 = SDL_GetTicksNS();
    touch_.set_safe_area(safe_area_window());
    touch_.render(*ui_);
    if (phone_sim_) draw_phone_mask();
#if GT3R_XR
    if (xr_ && xr_pointer_on_) { // the pointer's dot on the virtual screen
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 230);
        SDL_FRect dot{xr_pointer_x_ - 2, xr_pointer_y_ - 2, 4, 4};
        SDL_RenderFillRect(renderer_, &dot);
        SDL_SetRenderDrawColor(renderer_, 20, 90, 200, 255);
        SDL_RenderRect(renderer_, &dot);
    }
#endif
    ++frame_;
    // --shot: the 30th frame, or GT3R_SHOT_FRAME=<n> (e.g. into a race started from the menus).
    static const int shot_frame = [] {
        const char* f = SDL_getenv("GT3R_SHOT_FRAME");
        return f && *f ? std::atoi(f) : 30;
    }();
    // GT3R_SHOT_COUNT=<n>: n consecutive frames from there, as <path>-<k>.png (flicker checks).
    static const int shot_count = [] {
        const char* f = SDL_getenv("GT3R_SHOT_COUNT");
        return f && *f ? std::max(1, std::atoi(f)) : 1;
    }();
    if (!auto_shot_path_.empty() && frame_ >= shot_frame && frame_ < shot_frame + shot_count) {
        std::string path = auto_shot_path_;
        if (shot_count > 1) {
            size_t dot = path.rfind('.');
            std::string k = "-" + std::to_string(frame_ - shot_frame);
            path = dot == std::string::npos ? path + k : path.substr(0, dot) + k + path.substr(dot);
        }
        save_screenshot(path);
        if (frame_ == shot_frame + shot_count - 1) quit_ = true;
    }
    if (screenshot_requested_) {
        screenshot_requested_ = false;
        std::string dir = data_dir_ + "screenshots";
        SDL_CreateDirectory(dir.c_str());
        save_screenshot(dir + "/gt3r-" + std::to_string(SDL_GetTicks()) + ".png");
    }
    const Uint64 present_start = SDL_GetTicksNS();
    SDL_RenderPresent(renderer_);
    const double present_ms = double(SDL_GetTicksNS() - present_start) / 1e6;
    // A lost GPU (Vulkan's VK_ERROR_DEVICE_LOST, which SDL only reports in its error text) never
    // comes back, and the frames that never finish grow the memory by hundreds of MB a second
    // until the system kills us: close with a message instead.
    if (gpu_ && std::strstr(SDL_GetError(), "DEVICE_LOST")) {
        LOG_ERROR("GPU device lost: %s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GT3 Revisited",
                                 "The graphics driver stopped responding (GPU device lost), so the game has to close.",
                                 window_);
        return SDL_APP_FAILURE;
    }
    if (dyn_on_) {
        float s = dyn_.frame(frame_s * 1000.0, present_ms);
        if (const double c = dyn_cycle_s(); c > 0) { // testing: down to 50% and back up every c seconds
            const double t = std::fmod(double(SDL_GetTicksNS()) / 1e9 / c, 1.0);
            const float top = dyn_.max_scale(), k = render::DynamicScale::kStep;
            s = std::round((0.5f + (top - 0.5f) * float(std::fabs(2.0 * t - 1.0))) / k) * k;
        }
        if (s != gpu_->render_scale()) {
            LOG_INFO("Dynamic resolution: %d%%", int(std::lround(s * 100)));
            gpu_->set_render_scale(s);
        }
    }
#if GT3R_XR
    if (xr_) xr_->end_frame();
#endif
    {
        static const double thr = [] { const char* e = SDL_getenv("GT3R_HITCH"); return e ? std::atof(e) : 0.0; }();
        static Uint64 h_prev_end = 0;
        Uint64 h_end = SDL_GetTicksNS();
        if (thr > 0 && h_prev_end) {
            double wall = double(h_end - h_prev_end) / 1e6;
            if (wall > thr)
                LOG_INFO("HITCH frame %llu: %.1f ms (pre %.1f, update %.1f [%d ticks %.1f], render %.1f [env %.1f collect %.1f draw %.1f], present %.1f) t=%.1f pos=(%.0f,%.0f) smoke=%d meshes=%d",
                         (unsigned long long)frame_, wall, double(h_u0 - h_prev_end) / 1e6, double(h_u1 - h_u0) / 1e6,
                         hitch::ticks, hitch::ticks_ms, double(h_r1 - h_r0) / 1e6, hitch::env_ms, hitch::collect_ms,
                         hitch::draw_ms, double(h_end - h_r1) / 1e6, hitch::race_t, hitch::x, hitch::z, hitch::smoke, hitch::meshes);
        }
        h_prev_end = h_end;
    }
    return quit_ ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

SDL_AppResult App::event(const SDL_Event& e) {
    if (e.type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    // Android may end a backgrounded app without a quit.
    if (e.type == SDL_EVENT_WILL_ENTER_BACKGROUND || e.type == SDL_EVENT_TERMINATING) remember_race_scale();
    input_->handle_event(e);
    if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) set_dialog_result(e.drop.data);
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F12 && !e.key.repeat) screenshot_requested_ = true;
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F11 && !e.key.repeat) set_fullscreen(!fullscreen());
    // Fullscreen entered or left another way (macOS green button, window manager).
    if ((e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN || e.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) && !platform::is_mobile()) {
        const char* v = e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ? "on" : "off";
        if (config_.get("video", "fullscreen").value_or("off") != v) {
            config_.set("video", "fullscreen", v);
            save_config();
        }
    }
    if (e.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED && limit_60fps()) apply_frame_limit(); // another refresh rate
    if (e.type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
        e.type == SDL_EVENT_DID_ENTER_FOREGROUND)
        display_insets_.reset(), insets_retry_ = 0; // re-query (rotation, foldables)
    SDL_Event local = e; // pointer coordinates in UI space
    SDL_ConvertEventToRenderCoordinates(renderer_, &local);
    if (updates().dialog_open()) { // modal: pointers go to the update dialog only
        updates().dialog_event(*this, local);
        return SDL_APP_CONTINUE;
    }
    if (touch_.event(local)) return SDL_APP_CONTINUE; // a touch on the on-screen controls
    if (!screens_.empty()) screens_.back()->on_event(*this, local);
    return SDL_APP_CONTINUE;
}

SDL_FRect App::safe_area_window() const {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window_, &ww, &wh);
#if GT3R_XR
    if (xr_) return {0, 0, float(xr_->canvas_width()), float(xr_->canvas_height())};
#endif
    SDL_FRect r{0, 0, float(ww), float(wh)};
    SDL_Rect sa{};
    float corner = 0; // rounded-corner radius, window units
    if (phone_sim_) {
        r.x = 82.0f * float(ww) / 2340.0f; // the S25 FE's cutout inset, landscape
        r.w -= r.x;
        corner = 113.0f * float(wh) / 1080.0f;
    } else if (display_insets_ && ww > 0) {
        // Cutout only: SDL's safe area also counts the system-gesture zones.
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(window_, &pw, &ph);
        const float k = pw > 0 ? float(ww) / float(pw) : 1.0f;
        const platform::DisplayInsets& d = *display_insets_;
        r = {d.left * k, d.top * k, float(ww) - (d.left + d.right) * k, float(wh) - (d.top + d.bottom) * k};
        corner = float(d.corner_radius) * k;
    } else if (SDL_GetWindowSafeArea(window_, &sa) && sa.w > 0 && sa.h > 0) {
        r = {float(sa.x), float(sa.y), float(sa.w), float(sa.h)};
    }
    if (touch_ui_) {
        // Rounded corners are not part of the safe area. A rectangle inset by
        // 0.3 x the radius from both edges keeps its corner on the screen;
        // unknown radius: assume ~10 % of the short side.
        float c = corner > 0 ? 0.3f * corner : 0.03f * float(std::min(ww, wh));
        float x0 = std::max(r.x, c), y0 = std::max(r.y, c);
        float x1 = std::min(r.x + r.w, float(ww) - c), y1 = std::min(r.y + r.h, float(wh) - c);
        r = {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
    }
    return r;
}

void App::layout_ui(bool adaptive) {
    int pw = 0, ph = 0, ww = 0, wh = 0;
    SDL_GetRenderOutputSize(renderer_, &pw, &ph);
    SDL_GetWindowSize(window_, &ww, &wh);
#if GT3R_XR
    if (xr_) { // the canvas stands in for the window
        SDL_GetCurrentRenderOutputSize(renderer_, &pw, &ph);
        ww = pw, wh = ph;
    }
#endif
    // (Compared with the current target's own presentation: a VR canvas keeps its own.)
    int cur_w = 0, cur_h = 0;
    SDL_RendererLogicalPresentation cur_mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_GetRenderLogicalPresentation(renderer_, &cur_w, &cur_h, &cur_mode);
    if (!adaptive || pw < 160 || ph < 90 || ww <= 0 || wh <= 0) {
        if (cur_w != Ui::kWidth || cur_h != Ui::kHeight || cur_mode != SDL_LOGICAL_PRESENTATION_LETTERBOX)
            SDL_SetRenderLogicalPresentation(renderer_, Ui::kWidth, Ui::kHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        if (ui_) ui_->set_layout(Ui::kWidth, Ui::kHeight, {0, 0, Ui::kWidth, Ui::kHeight}, touch_ui_);
        return;
    }
    // Integer pixel scale: about the 640x360 look on desktop; on phones the
    // 8 px font at ~12 dp (readable at arm's length, rows of ~9 mm).
    int s = std::max(1, int(std::lround(std::min(pw / 640.0f, ph / 360.0f))));
    if (touch_ui_) {
        // GT3R_PHONE: the S25 FE's density, scaled to the half-size window.
        float dp = phone_sim_ ? 2.625f * float(ph) / 1080.0f : SDL_GetWindowDisplayScale(window_);
        s = std::max(s, int(std::lround(1.5f * std::max(dp, 1.0f))));
    }
    // Never so large that the safe area gets smaller than ~480x250 units.
    const SDL_FRect sa = safe_area_window();
    const float px_per_window = float(pw) / float(ww);
    const float safe_w = sa.w * px_per_window, safe_h = sa.h * px_per_window;
    while (s > 1 && (safe_w / float(s) < 480 || safe_h / float(s) < 250)) --s;
    const int lw = pw / s, lh = ph / s;
    if (lw != cur_w || lh != cur_h || cur_mode != SDL_LOGICAL_PRESENTATION_LETTERBOX) {
        SDL_SetRenderLogicalPresentation(renderer_, lw, lh, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        LOG_INFO("UI layout %dx%d (x%d), safe area %.0f,%.0f %.0fx%.0f px", lw, lh, s, sa.x * px_per_window,
                 sa.y * px_per_window, safe_w, safe_h);
    }
    const float k = px_per_window / float(s); // window units -> UI units
    float x0 = std::ceil(sa.x * k), y0 = std::ceil(sa.y * k);
    float x1 = std::floor((sa.x + sa.w) * k), y1 = std::floor((sa.y + sa.h) * k);
    x0 = std::clamp(x0, 0.0f, float(lw) / 4), y0 = std::clamp(y0, 0.0f, float(lh) / 4);
    x1 = std::clamp(x1, float(lw) * 3 / 4, float(lw)), y1 = std::clamp(y1, float(lh) * 3 / 4, float(lh));
    ui_->set_layout(float(lw), float(lh), {x0, y0, x1 - x0, y1 - y0}, touch_ui_);
}

void App::draw_phone_mask() {
    int pw = 0, ph = 0;
    SDL_GetRenderOutputSize(renderer_, &pw, &ph);
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode;
    SDL_GetRenderLogicalPresentation(renderer_, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    // Rounded corners (radius 113 px at 1080 px) and the punch-hole camera.
    const float k = float(ph) / 1080.0f, rad = 113 * k;
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    for (int y = 0; y < int(rad); ++y) {
        float dy = rad - float(y) - 0.5f;
        float w = rad - std::sqrt(std::max(0.0f, rad * rad - dy * dy));
        SDL_FRect rs[4] = {{0, float(y), w, 1}, {float(pw) - w, float(y), w, 1},
                           {0, float(ph - 1 - y), w, 1}, {float(pw) - w, float(ph - 1 - y), w, 1}};
        SDL_RenderFillRects(renderer_, rs, 4);
    }
    const float cx = 40 * k, cy = float(ph) / 2, cr = 16 * k;
    for (float y = -cr; y <= cr; y += 1) {
        float w = std::sqrt(std::max(0.0f, cr * cr - y * y));
        SDL_RenderLine(renderer_, cx - w, cy + y, cx + w, cy + y);
    }
    SDL_SetRenderLogicalPresentation(renderer_, lw, lh, mode);
}

void App::save_screenshot(const std::string& path) {
    // Normally the game area only (comparison shots); GT3R_PHONE: the whole
    // window, side bars and touch controls included.
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    if (phone_sim_) {
        SDL_GetRenderLogicalPresentation(renderer_, &lw, &lh, &mode);
        SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    }
    SDL_Surface* shot = SDL_RenderReadPixels(renderer_, nullptr);
    if (phone_sim_) SDL_SetRenderLogicalPresentation(renderer_, lw, lh, mode);
    if (!shot) return;
    SDL_Surface* rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(shot);
    if (!rgba) return;
    std::vector<u32> px(size_t(rgba->w) * rgba->h);
    for (int y = 0; y < rgba->h; ++y)
        std::memcpy(&px[size_t(y) * rgba->w], static_cast<u8*>(rgba->pixels) + size_t(y) * rgba->pitch, size_t(rgba->w) * 4);
    Bytes png = encode_png(u32(rgba->w), u32(rgba->h), px);
    SDL_DestroySurface(rgba);
    if (write_host_file(path, png)) LOG_INFO("Screenshot saved: %s", path.c_str());
#if GT3R_XR
    // A stereo frame: the two eyes side by side as <path>-eyes.png (the shot above is the HUD).
    if (xr_ && xr_->stereo_submitted()) {
        SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
        const xr::Eye& e0 = xr_->eye(0);
        const int w = e0.width, h = e0.height;
        std::vector<u32> both(size_t(w) * 2 * size_t(h));
        for (int i = 0; i < 2; ++i) {
            SDL_SetRenderTarget(renderer_, xr_->eye(i).canvas);
            SDL_Surface* s = SDL_RenderReadPixels(renderer_, nullptr);
            SDL_Surface* c = s ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32) : nullptr;
            if (s) SDL_DestroySurface(s);
            if (!c) continue;
            for (int y = 0; y < std::min(h, c->h); ++y)
                std::memcpy(&both[size_t(y) * size_t(w) * 2 + size_t(i) * size_t(w)],
                            static_cast<u8*>(c->pixels) + size_t(y) * size_t(c->pitch), size_t(std::min(w, c->w)) * 4);
            SDL_DestroySurface(c);
        }
        SDL_SetRenderTarget(renderer_, prev);
        size_t dot = path.rfind('.');
        std::string eyes = (dot == std::string::npos ? path : path.substr(0, dot)) + "-eyes.png";
        if (write_host_file(eyes, encode_png(u32(w) * 2, u32(h), both))) LOG_INFO("Eyes saved: %s", eyes.c_str());
    }
#endif
}

#if GT3R_XR
void App::set_xr_settings(const xr::Settings& s) {
    if (!xr_) return;
    xr_->set_settings(s);
    s.save(config_);
    save_config();
}

// The right controller points at the virtual screen (menus, movies; not while racing in 3D):
// a dot follows it and the trigger clicks, as mouse events in UI coordinates.
void App::xr_pointer() {
    // Already in UI coordinates: routed like App::event's pointer events after their conversion.
    auto dispatch = [this](const SDL_Event& e) {
        if (updates().dialog_open()) updates().dialog_event(*this, e);
        else if (!screens_.empty()) screens_.back()->on_event(*this, e);
    };
    float u = 0, v = 0;
    bool pressed = false;
    const bool on = xr_->pointer(u, v, pressed);
    if (!on) {
        if (xr_pointer_down_) { // released off the screen
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.x = xr_pointer_x_, e.button.y = xr_pointer_y_;
            dispatch(e);
        }
        xr_pointer_on_ = xr_pointer_down_ = false;
        return;
    }
    // Canvas pixels -> UI units through the canvas's logical presentation (layout_ui).
    float x = u * float(xr_->canvas_width()), y = v * float(xr_->canvas_height());
    SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, xr_->canvas());
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_FRect lr{};
    if (SDL_GetRenderLogicalPresentation(renderer_, &lw, &lh, &mode) && mode != SDL_LOGICAL_PRESENTATION_DISABLED &&
        lw > 0 && lh > 0 && SDL_GetRenderLogicalPresentationRect(renderer_, &lr) && lr.w > 0 && lr.h > 0) {
        x = (x - lr.x) * float(lw) / lr.w;
        y = (y - lr.y) * float(lh) / lr.h;
    }
    SDL_SetRenderTarget(renderer_, prev);
    if (!xr_pointer_on_ || x != xr_pointer_x_ || y != xr_pointer_y_) {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.x = x, e.motion.y = y;
        e.motion.xrel = xr_pointer_on_ ? x - xr_pointer_x_ : 0;
        e.motion.yrel = xr_pointer_on_ ? y - xr_pointer_y_ : 0;
        e.motion.state = xr_pointer_down_ ? SDL_BUTTON_LMASK : 0;
        dispatch(e);
    }
    xr_pointer_on_ = true;
    xr_pointer_x_ = x, xr_pointer_y_ = y;
    if (pressed != xr_pointer_down_) {
        SDL_Event e{};
        e.type = pressed ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = pressed;
        e.button.clicks = 1;
        e.button.x = x, e.button.y = y;
        dispatch(e);
        xr_pointer_down_ = pressed;
    }
}
#endif

void App::shutdown() {
    remember_race_scale();
    if (input_) save_config();
    screens_.clear();
    pending_.clear();
    close_disc();
    audio_.reset();
    input_.reset();
    ui_.reset();
    if (dialog_mutex_) SDL_DestroyMutex(dialog_mutex_);
#if GT3R_XR
    xr_.reset(); // its canvases and swapchains before the renderer and the device
#endif
    if (gpu_) gpu_.reset(); // destroys the 2D renderer too
    else if (renderer_) SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

} // namespace gt3r
