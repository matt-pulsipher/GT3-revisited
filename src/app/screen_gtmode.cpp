// GT Mode in the app: installs the GT-mode menu bindings (game/gt_mode_menu.h)
// so the interactive menus (gt3r --screen gt3menu) run the career, and adds
// a developer overview of the GT-mode save (gt3r --screen gtmode): credits,
// day, garage with condition, license medals and series progress.

#include "app/app.h"
#include "formats/model/wheel_spec.h"
#include "app/screen_registry.h"
#include "game/gt_mode_menu.h"
#include "game/gt_save.h"
#include "base/log.h"
#include "formats/model/car_model.h"
#include "formats/text/charset.h"
#include "menu/flow/gt_mode_hooks.h"
#include "menu/flow/menu_set.h"
#include "platform/render_scale.h"
#include "race/backend.h"
#include "render/prize_view.h"
#include "render/showroom.h"
#include "sim/car_spec.h"
#include "render/view3d.h"
#include "vfs/vfs.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>

namespace gt3r {

namespace {

// 3D models in the GT-mode menus: the "car_render_*" widgets (the car turning
// slowly on a turntable) and the prize models (the wheel shop's tiles are 2D art).
// Each model is drawn through RGBA textures so the page's art shows around it (View3D itself
// composites opaquely).
class MenuModelRenderer {
public:
    void draw(App& app, SDL_Renderer* r, const menu::Element& e, const SDL_FRect& box, float alpha,
              const std::string& page, const menu::MenuImage* image) {
        (void)alpha;
        if (!app.gpu() || !app.vfs()) return;
        frame_ = app.frame();
        // The car renders' own image (car_render_* widgets, the arcade pickers' "car" element) is
        // the showroom's reflection map.
        SDL_GPUTexture* env = env_map(app, image);
        // Presentations with a prize model (license card, championship trophy): the .prm
        // scene with its own camera and animation over the whole page.
        if (e.widget_class && *e.widget_class == "render_model") {
            int serial = 0;
            float fade = 0;
            std::string path = game::presentation_model(page, &serial, &fade);
            if (!path.empty()) {
                const bool mini = page == "arcade_mini" || page == "gtmode_mini" || page == "consolation_prize";
                draw_prize(app, r, e, box, path, serial, fade, page == "trophy_get", mini);
                return;
            }
        }
        std::string car, wheel;
        int paint = 0;
        // Arcade car pickers ("ar/select_car:<entry>"): the entry's car, turning, in the
        // area above the icon strip.
        if (size_t colon = page.find(':'); colon != std::string::npos && page.rfind("ar/", 0) == 0) {
            std::string entry = page.substr(colon + 1);
            // "%<player>": the 2 Player car page (split showroom, player 2 mirrored).
            int player = -1;
            if (size_t pc = entry.find('%'); pc != std::string::npos) {
                player = std::atoi(entry.c_str() + pc + 1);
                entry.resize(pc);
            }
            // "@<frames>": menu frames since the car appeared (drives the intro exactly).
            int frame = -1;
            if (size_t at = entry.find('@'); at != std::string::npos) {
                frame = std::atoi(entry.c_str() + at + 1);
                entry.resize(at);
            }
            if (size_t hash = entry.find('#'); hash != std::string::npos) {
                paint = std::atoi(entry.c_str() + hash + 1);
                entry.resize(hash);
            }
            car = game::arcade_entry_car(entry);
            // Garage classes: the garage car's own paint and wheel.
            if (const game::GarageCar* gc = game::arcade_garage_car(entry)) {
                paint = gc->color;
                wheel = gc->wheel;
            }
            const Loaded* m = car.empty() ? nullptr : this->car(app, car, paint, wheel);
            if (m && player >= 0) {
                // menu/showroom_split.fbi; player 2 mirrored.
                const ShowroomCamera& split = showroom_camera(app, "split");
                showroom(app, r, box, *m, entry, player == 1 ? mirrored(split) : split, frame, player + 1, false, 1,
                         env, 2);
            } else if (m) {
                const bool compulsory = page.rfind("ar/select_compulsory", 0) == 0;
                showroom(app, r, box, *m, entry, showroom_camera(app, compulsory ? "compulsory" : "single"), frame, 0,
                         false, 1, env, compulsory ? 2 : 4);
            }
            return;
        }
        float dirt = 0;
        if (!e.widget_class || !game::car_for_widget(page, *e.widget_class, car, paint, &wheel, &dirt)) return;
        const Loaded* m = this->car(app, car, paint, wheel);
        if (!m) return;
        // Where the car goes, in menu units from the top-left (the widgets cover the
        // whole page or, on the prize screen, only mark a spot).
        SDL_FRect area = box;
        float zoom = 1.2f;
        auto menu_rect = [&](float x, float y, float rw, float rh) {
            float sx = box.w / e.w, sy = box.h / e.h;
            float x0 = box.x - e.x * sx, y0 = box.y - (448 - e.y - e.h) * sy;
            return SDL_FRect{x0 + x * sx, y0 + y * sy, rw * sx, rh * sy};
        };
        // Every car render is a showroom over the whole page with its .fbi camera and lights:
        // car_render_from dealer (every dealer car page), _buy buycar (the "purchased"
        // popup, no floor mirror), _garage garage, _wheel wheel, _wheel_shop wheelshop,
        // _car_wash carwash, _oil_change oilchange.
        if (e.w > 0 && e.h > 0 && *e.widget_class != "render_model") {
            static const std::map<std::string, std::string> kKind = {
                {"car_render_buy", "buycar"},       {"car_render_garage", "garage"},
                {"car_render_wheel", "wheel"},      {"car_render_wheel_shop", "wheelshop"},
                {"car_render_car_wash", "carwash"}, {"car_render_oil_change", "oilchange"},
                {"car_render_from", "dealer"}};
            auto k = kKind.find(*e.widget_class);
            std::string kind = k != kKind.end() ? k->second : page.rfind("cd/", 0) == 0 ? "dealer" : "garage";
            const ShowroomCamera& cam = showroom_camera(app, kind);
            if (cam.valid) {
                // GT-mode kinds: the fast (k = 8) intro fade; a dirty car reflects less (1 - dirt).
                // A wheel change keeps the camera. The car wash spins its car (game::car_wash_spin).
                const game::CarWashSpin spin = game::car_wash_spin();
                showroom(app, r, menu_rect(0, 0, 640, 448), *m, kind + ":" + car + "#" + std::to_string(paint), cam,
                         -1, 0, true, 1.0f - dirt, env, 4, kind == "carwash" ? &spin : nullptr);
                return;
            }
        }
        if (e.w > 0 && e.h > 0) {
            if (*e.widget_class == "render_model") {
                area = menu_rect(40, 150, 560, 180); // between the title and the OK button
                zoom = 0.62f;
            } else if (*e.widget_class == "car_render_from") {
                area = menu_rect(0, 40, 640, 250); // dealer: above the price and paint row
                zoom = 0.9f;
            }
        }
        render(app, r, area, *m, zoom, 0.22f);
    }

private:
    struct Loaded {
        std::unique_ptr<render::GpuModel> model;
        float bmin[3]{}, bmax[3]{};
    };
    struct Targets {
        SDL_Texture* black = nullptr;
        SDL_Texture* white = nullptr;
        int used = 0; // App::frame of the last use
    };
    // Offscreen 2D targets by size. The car's box changes size every frame while it zooms to
    // the next car, so a size unused for 8 frames is freed (otherwise every size of every zoom
    // stayed allocated).
    Targets& targets_for(std::map<std::pair<int, int>, Targets>& m, int w, int h) {
        for (auto it = m.begin(); it != m.end();) {
            if (it->second.used < frame_ - 8) {
                if (it->second.black) SDL_DestroyTexture(it->second.black);
                if (it->second.white) SDL_DestroyTexture(it->second.white);
                it = m.erase(it);
            } else {
                ++it;
            }
        }
        Targets& t = m[{w, h}];
        t.used = frame_;
        return t;
    }

    using ShowroomCamera = render::ShowroomCamera;
    const ShowroomCamera& showroom_camera(App& app, const std::string& kind) {
        auto it = cameras_.find(kind);
        if (it != cameras_.end()) return it->second;
        ShowroomCamera& c = cameras_[kind];
        if (std::optional<Bytes> f = app.vfs()->read("menu/showroom_" + kind + ".fbi")) c = render::parse_showroom_camera(*f);
        return c;
    }
    static ShowroomCamera mirrored(const ShowroomCamera& c) { return render::mirrored(c); }
    static render::DrawOptions showroom_options(const ShowroomCamera& cam, const render::Mat4& view,
                                                bool mirror = false, SDL_GPUTexture* env = nullptr,
                                                float reflect = 1) {
        return render::showroom_options(cam, view, mirror, env, reflect);
    }

    // `frame` >= 0: the menu's frame count since the car appeared (steps the intro by menu
    // frames); else real time.
    // `passes`: the car's accumulation passes (4; 2 for the
    // split and compulsory showrooms).
    // `spin`: the car wash's pushes of the turntable (one kWashSpinPush of yaw acceleration per
    // step until all the pushes so far are applied).
    void showroom(App& app, SDL_Renderer* r, const SDL_FRect& page, const Loaded& m, const std::string& entry,
                  const ShowroomCamera& cam, int frame = -1, int slot = 0, bool fast_fade = false, float reflect = 1,
                  SDL_GPUTexture* env = nullptr, int passes = 4, const game::CarWashSpin* spin = nullptr) {
        Showroom& show_ = shows_[slot];
        if (!cam.valid) return;
        // Phones and tablets: one pass (MSAA still smooths the edges) and so one mirror pass,
        // 3 renders a frame instead of 10. Each pass is its own small 3D render, submit and
        // composite, which a tile-based mobile GPU pays for in full (5 fps on a Galaxy A15).
        if (platform::is_mobile()) passes = 1;
        if (show_.key != entry) {
            show_ = Showroom{};
            show_.key = entry;
            show_.look[0] = cam.key(0x34), show_.look[1] = cam.key(0x35), show_.look[2] = cam.key(0x36);
            show_.ticks = SDL_GetTicksNS();
            show_.yaw = cam.key(0x3E);
            show_.elev = cam.key(0x3D);
            show_.dist = cam.key(0x3F);
            if (spin) show_.spin_serial = spin->serial, show_.spin_pushes = spin->pushes; // (pushes before it: gone)
        }
        if (spin && spin->serial != show_.spin_serial) show_.spin_serial = spin->serial, show_.spin_pushes = 0;
        u64 now = SDL_GetTicksNS();
        double dt = double(now - show_.ticks) / 1e9;
        show_.ticks = now;
        int steps = dt < 0.001 ? 1 : std::min(30, int(dt * 60.0 + 0.5));
        if (frame >= 0) steps = std::clamp(frame - show_.frames - show_.load_wait, 0, 600);
        // The car loads asynchronously first (about 25 frames on a CD, M): nothing is drawn
        // and the camera intro waits until then.
        constexpr int kLoadFrames = 25;
        for (int i = 0; i < steps; ++i) {
            if (show_.load_wait < kLoadFrames) {
                ++show_.load_wait;
                continue;
            }
            // The menu car camera integrator: v += a / 60, v -= v c / 60, x += v / 60.
            auto spring = [](float& x, float& v, float target, float k, float c) {
                v += (target - x) * k / 60.0f;
                v -= v * c / 60.0f;
                x += v / 60.0f;
            };
            spring(show_.dist, show_.dist_v, cam.key(0x44), cam.key(0x45), cam.key(0x46));
            for (int a = 0; a < 3; ++a) spring(show_.look[a], show_.look_v[a], cam.key(0x38 + a), cam.key(0x3B), cam.key(0x3C));
            float push = 0;
            if (spin && show_.spin_pushes < spin->pushes) push = game::kWashSpinPush, ++show_.spin_pushes;
            show_.yaw_v += (cam.key(0x42) + push) / 60.0f;
            show_.yaw_v -= show_.yaw_v * cam.key(0x43) / 60.0f;
            show_.yaw += show_.yaw_v / 60.0f;
            show_.elev_v += cam.key(0x41) / 60.0f;
            show_.elev_v -= show_.elev_v * cam.key(0x43) / 60.0f;
            show_.elev += show_.elev_v / 60.0f;
            ++show_.frames;
        }
        if (!view_black_) view_black_ = std::make_unique<render::View3D>(*app.gpu());
        if (!view_white_) view_white_ = std::make_unique<render::View3D>(*app.gpu());
        // The game's yaw in degrees: 180 = in front of the car (the timed reference shows it
        // head-on ~2 s after entry), 90 = the side, nose to the right (~6 s).
        const float yaw = show_.yaw * 3.14159265f / 180.0f;
        const float pitch = -show_.elev * 3.14159265f / 180.0f;
        const render::Vec3 target{show_.look[0], show_.look[1], show_.look[2]};
        const render::Vec3 eye = target + render::Vec3{std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                                                       std::cos(yaw) * std::cos(pitch)} * show_.dist;
        render::Mat4 view = render::look_at(eye, target, {0, 1, 0});
        if (cam.key(0x47) != 0 || cam.key(0x48) != 0 || cam.key(0x49) != 0 || cam.key(0x4A) != 0) {
            const float d2r = 3.14159265f / 180.0f;
            view = render::translate({cam.key(0x47), cam.key(0x48), 0}) * render::rotate_y(cam.key(0x4A) * d2r) *
                   render::rotate_x(cam.key(0x49) * d2r) * view;
        }
        const float aspect = page.w / std::max(page.h, 1.0f);
        const float vfov = 2.0f * std::atan(std::tan(cam.key(0x4B) * 0.5f * 3.14159265f / 180.0f) / aspect);
        render::Mat4 proj = render::perspective(vfov, aspect, 0.1f, 1000.0f);
        proj.at(1, 1) /= 1.098f; // the game's vertical aspect fix (as in the race)
        if (show_.load_wait < kLoadFrames) return;
        // The floor reflection: the car mirrored about the floor, added at 0.65 x the intro
        // fade (first visible 5 frames after loading, full after 88), drawn first.
        // The intro fade spring: k = 2 (arcade kinds, full after 88 frames) or k = 8 (the GT-mode
        // car_render kinds, full after 38 frames); first visible about 5 frames after loading.
        const float fade_end = fast_fade ? 38.0f : 88.0f;
        const float fade = std::clamp(float(show_.frames - 5) / (fade_end - 5.0f), 0.0f, 1.0f);
        // Accumulation anti-aliasing: every pass is drawn with the projection
        // shifted by the OpenGL jitter table entry x 0.6 game pixels and averaged (pass i at
        // weight 1 / (i + 1)); the floor mirror takes 2 passes (1 for the 2-pass kinds and
        // while the intro fade is under 0.8). (M: the original also re-orders the passes
        // while the car fades in; not reproduced.)
        auto jittered = [&](SDL_Renderer* rr, SDL_Texture* target, int w, int h, int n, auto&& draw_pass) {
            static const float kJ2[2][2] = {{0.25f, 0.75f}, {0.75f, 0.25f}};
            static const float kJ4[4][2] = {{0.375f, 0.25f}, {0.125f, 0.75f}, {0.875f, 0.25f}, {0.625f, 0.75f}};
            SDL_Texture*& tmp = targets_for(pass_tex_, w, h).black;
            if (n > 1 && !tmp) tmp = SDL_CreateTexture(rr, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
            if (!tmp) n = 1;
            const float px = 0.6f * float(w) / 640.0f, py = 0.6f * float(h) / 448.0f;
            for (int i = 0; i < n; ++i) {
                const float* j = n == 4 ? kJ4[i] : n == 2 ? kJ2[i] : nullptr;
                render::Mat4 pj = proj;
                if (j) {
                    pj.at(0, 2) -= 2.0f * (j[0] - 0.5f) * px / float(w);
                    pj.at(1, 2) -= 2.0f * (j[1] - 0.5f) * py / float(h);
                }
                if (i == 0) {
                    draw_pass(pj);
                    continue;
                }
                SDL_SetRenderTarget(rr, tmp);
                draw_pass(pj);
                SDL_SetRenderTarget(rr, target);
                SDL_SetTextureBlendMode(tmp, SDL_BLENDMODE_BLEND);
                SDL_SetTextureAlphaModFloat(tmp, 1.0f / float(i + 1));
                SDL_RenderTexture(rr, tmp, nullptr, nullptr);
            }
        };
        const int mirror_passes = passes <= 2 || fade < 0.8f ? 1 : 2;
        if (entry.rfind("buycar:", 0) != 0) // car_render_buy draws no floor mirror
        add_mirror(r, page, 0.65f * fade * std::clamp(reflect, 0.0f, 1.0f), [&](int w, int h) {
            render::DrawOptions o = showroom_options(cam, view, true, env, reflect);
            o.clear[0] = o.clear[1] = o.clear[2] = 0;
            o.clear[3] = 1;
            // The game mirrors with (1, -1, 1); we use (-1, -1, 1), a rotation, so the faces
            // keep their winding and lighting (the body is left / right symmetric).
            render::DrawItem it{m.model.get(), render::scale({-1, -1, 1}), 0};
            jittered(r, SDL_GetRenderTarget(r), w, h, mirror_passes, [&](const render::Mat4& pj) {
                view_black_->draw(r, SDL_FRect{0, 0, float(w), float(h)}, view, pj, eye, {it}, o);
            });
        });
        composite(r, page, [&](SDL_Texture* tex, int w, int h, float bg) {
            render::DrawOptions o = showroom_options(cam, view, false, env, reflect);
            o.clear[0] = o.clear[1] = o.clear[2] = bg;
            o.clear[3] = 1;
            render::View3D& v = bg > 0.5f ? *view_white_ : *view_black_;
            jittered(r, tex, w, h, passes, [&](const render::Mat4& pj) {
                v.draw(r, SDL_FRect{0, 0, float(w), float(h)}, view, pj, eye, {{m.model.get(), {}, 0}}, o);
            });
        }, fade);
    }

    // The prize model of a presentation (render::PrizeView, shared with the race
    // result medals): restarted for every new presentation, animated in real time.
    void draw_prize(App& app, SDL_Renderer* r, const menu::Element& e, const SDL_FRect& box, const std::string& path,
                    int serial, float fade, bool trophy, bool mini = false) {
        if (!prize_ || prize_path_ != path) {
            prize_.reset();
            prize_path_ = path;
            std::optional<Bytes> file = app.vfs()->read(path);
            if (!file) return;
            auto pv = std::make_unique<render::PrizeView>(*app.gpu());
            if (!pv->load(*file, path)) return;
            prize_ = std::move(pv);
            prize_serial_ = serial;
            prize_ticks_ = SDL_GetTicksNS();
        }
        if (prize_serial_ != serial) {
            prize_->restart();
            prize_serial_ = serial;
            prize_ticks_ = SDL_GetTicksNS();
        }
        // Screenshots: GT3R_GTMODE_PRIZE_FRAME=<n> skips the first n frames of the animation.
        if (prize_skip_ != serial) {
            prize_skip_ = serial;
            if (const char* env = std::getenv("GT3R_GTMODE_PRIZE_FRAME"))
                for (int i = std::atoi(env); i > 0; --i) prize_->tick(1.0 / 60);
        }
        // Real time, at most 0.1 s per call; menu frames stepped faster than real time
        // (scripted screenshots) still advance one 60 Hz frame each.
        u64 now = SDL_GetTicksNS();
        double dt = double(now - prize_ticks_) / 1e9;
        prize_->tick(dt < 0.001 ? 1.0 / 60 : std::min(dt, 0.1));
        prize_ticks_ = now;
        // The scene covers the page (the widget only marks a spot): the whole 640 x 448 frame.
        SDL_FRect area = box;
        if (e.w > 0 && e.h > 0) {
            float sx = box.w / e.w, sy = box.h / e.h;
            area = SDL_FRect{box.x - e.x * sx, box.y - (448 - e.y - e.h) * sy, 640 * sx, 448 * sy};
            // The championship trophy item draws 160 pixels left of centre (gt_mode §18, M).
            if (trophy) area.x -= 160 * sx;
            // Race prizes (mini prize): the cup appears smaller and higher than the .prm camera
            // over the full frame gives (M: fitted to the reference capture of the Sunday Cup
            // cup page: 0.86 x about the frame centre).
            if (mini) {
                const float k = 0.86f, cx = area.x + area.w * 0.5f, cy = area.y + area.h * 0.5f;
                area = SDL_FRect{cx - area.w * k * 0.5f, cy - area.h * k * 0.5f, area.w * k, area.h * k};
            }
        }
        // The game's multi-pass draw (gt_mode §18): license 6 passes, trophy 4, each with an
        // accumulation-AA sub-pixel offset and, when the file's focus distance A > 1, the eye
        // on a circle of the aperture radius around the axis (depth of field); the passes are
        // averaged (pass i weighted 1 / (i + 1)).
        static const float kJ4[4][2] = {{0.375f, 0.25f}, {0.125f, 0.75f}, {0.875f, 0.25f}, {0.625f, 0.75f}};
        static const float kJ6[6][2] = {{0.4646f, 0.4646f}, {0.1313f, 0.7979f}, {0.5353f, 0.8686f},
                                        {0.8686f, 0.5353f}, {0.7979f, 0.1313f}, {0.2020f, 0.2020f}};
        const int passes = trophy ? 4 : 6;
        composite(
            r, area,
            [&](SDL_Texture* tex, int w, int h, float bg) {
                const float clear[4] = {bg, bg, bg, 1};
                SDL_Texture*& tmp = targets_for(pass_tex_, w, h).black;
                if (!tmp) tmp = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
                const float A = prize_->focus(), B = prize_->aperture();
                const float px_scale = float(w) / 640.0f;
                for (int i = 0; i < passes; ++i) {
                    render::PrizeView::Pass ps;
                    const float* j = trophy ? kJ4[i] : kJ6[i];
                    ps.px = (j[0] - 0.5f) * px_scale, ps.py = (j[1] - 0.5f) * px_scale;
                    if (A > 1) {
                        float th = float(i) * 6.2831853f / float(passes);
                        ps.ex = B * std::cos(th), ps.ey = B * std::sin(th), ps.focus = A;
                    }
                    if (i == 0 || !tmp) {
                        if (i > 0) break;
                        prize_->draw(r, SDL_FRect{0, 0, float(w), float(h)}, clear, &ps);
                        continue;
                    }
                    SDL_SetRenderTarget(r, tmp);
                    prize_->draw(r, SDL_FRect{0, 0, float(w), float(h)}, clear, &ps);
                    SDL_SetRenderTarget(r, tex);
                    SDL_SetTextureBlendMode(tmp, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureAlphaModFloat(tmp, 1.0f / float(i + 1));
                    SDL_RenderTexture(r, tmp, nullptr, nullptr);
                }
            },
            fade);
    }

    // Turntable render with exact compositing: the model shader's alpha is not
    // coverage, so render over black (B = a*m) and over white (W = a*m + 1 - a),
    // then page = page * (W - B) + B.
    void render(App& app, SDL_Renderer* r, const SDL_FRect& dst, const Loaded& m, float zoom, float pitch,
                float yaw = -1000, float ambient = -1, float fovy = 0.8f, int lights = 0) {
        if (!view_black_) view_black_ = std::make_unique<render::View3D>(*app.gpu());
        if (!view_white_) view_white_ = std::make_unique<render::View3D>(*app.gpu());
        render::OrbitCamera cam;
        cam.frame(m.bmin, m.bmax);
        cam.distance *= zoom * std::tan(cam.fovy * 0.5f) / std::tan(fovy * 0.5f); // same framing, longer lens
        cam.fovy = fovy;
        cam.pitch = pitch;
        cam.yaw = yaw > -999 ? yaw : 0.6f + float(double(SDL_GetTicks() % 18000) / 18000.0 * 6.283185307); // 1 turn / 18 s
        composite(r, dst, [&](SDL_Texture*, int w, int h, float bg) {
            render::DrawOptions o;
            (void)lights;
            o.clear[0] = o.clear[1] = o.clear[2] = bg;
            o.clear[3] = 1;
            if (ambient >= 0) o.ambient = ambient;
            render::View3D& v = bg > 0.5f ? *view_white_ : *view_black_;
            v.draw(r, SDL_FRect{0, 0, float(w), float(h)}, cam, {{m.model.get(), {}, 0}}, o);
        });
    }

    // Draws a 3D picture once over black and adds it to the page scaled by `factor`
    // (GS ALPHA Cs x FIX + Cd).
    template <class Paint>
    void add_mirror(SDL_Renderer* r, const SDL_FRect& dst, float factor, Paint&& paint) {
        int w = int(dst.w + 0.5f), h = int(dst.h + 0.5f);
        if (w < 8 || h < 8 || factor <= 0) return;
        Targets& t = targets_for(targets_, w, h);
        if (!t.black) t.black = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
        if (!t.black) return;
        SDL_Texture* prev = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, t.black);
        paint(w, h);
        SDL_FlushRenderer(r);
        SDL_SetRenderTarget(r, prev);
        static const SDL_BlendMode kAdd = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO,
            SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
        SDL_SetTextureBlendMode(t.black, kAdd);
        Uint8 k = Uint8(std::clamp(factor, 0.0f, 1.0f) * 255 + 0.5f);
        SDL_SetTextureColorMod(t.black, k, k, k);
        SDL_RenderTexture(r, t.black, nullptr, &dst);
        SDL_SetTextureColorMod(t.black, 255, 255, 255);
        SDL_FlushRenderer(r);
    }

    // Draws a 3D picture twice (`paint(target, w, h, background)` over black, then
    // over white) and composites it onto the page in `dst`.
    template <class Paint>
    void composite(SDL_Renderer* r, const SDL_FRect& dst, Paint&& paint, float opacity = 1.0f) {
        int w = int(dst.w + 0.5f), h = int(dst.h + 0.5f);
        if (w < 8 || h < 8) return;
        Targets& t = targets_for(targets_, w, h);
        if (!t.black) t.black = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
        if (!t.white) t.white = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
        if (!t.black || !t.white) return;
        SDL_Texture* prev = SDL_GetRenderTarget(r);
        auto render_on = [&](SDL_Texture* tex, float bg) {
            SDL_SetRenderTarget(r, tex);
            paint(tex, w, h, bg);
            SDL_FlushRenderer(r);
        };
        render_on(t.black, 0);
        render_on(t.white, 1);
        static const SDL_BlendMode kSub = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT, SDL_BLENDFACTOR_ZERO,
            SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
        static const SDL_BlendMode kAdd = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO,
            SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
        SDL_SetTextureBlendMode(t.black, kSub);
        SDL_RenderTexture(r, t.black, nullptr, nullptr); // white := W - B
        if (opacity < 1.0f) {
            // Fading in: white := lerp(1, W - B, opacity), and B is drawn scaled by it.
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 255, 255, 255, Uint8(std::clamp(1.0f - opacity, 0.0f, 1.0f) * 255 + 0.5f));
            SDL_RenderFillRect(r, nullptr);
        }
        SDL_SetRenderTarget(r, prev);
        SDL_SetTextureBlendMode(t.white, SDL_BLENDMODE_MOD);
        SDL_RenderTexture(r, t.white, nullptr, &dst);
        SDL_SetTextureBlendMode(t.black, kAdd);
        Uint8 o = Uint8(std::clamp(opacity, 0.0f, 1.0f) * 255 + 0.5f);
        SDL_SetTextureColorMod(t.black, o, o, o);
        SDL_RenderTexture(r, t.black, nullptr, &dst);
        SDL_SetTextureColorMod(t.black, 255, 255, 255);
        SDL_FlushRenderer(r); // the targets are reused by the next model of this frame
    }

    // The GPU copy of a showroom reflection map (a menu image), uploaded once per distinct
    // image (keyed by size and a sample of its pixels: the pages share two maps).
    SDL_GPUTexture* env_map(App& app, const menu::MenuImage* image) {
        if (!image || image->rgba.empty() || image->width < 16 || image->height < 16) return nullptr;
        u64 key = (u64(image->width) << 48) ^ (u64(image->height) << 32);
        for (size_t i = 0; i < image->rgba.size(); i += 997) key = key * 1099511628211ull ^ image->rgba[i];
        auto it = env_maps_.find(key);
        if (it != env_maps_.end()) return it->second;
        render::Uploader up(app.gpu()->device());
        SDL_GPUTexture* t = up.texture_rgba(image->width, image->height, image->rgba.data(), true);
        if (!up.submit()) t = nullptr;
        env_maps_[key] = t;
        return t;
    }

    const Loaded* car(App& app, const std::string& car, int paint, const std::string& wheel) {
        std::string key = car + "#" + std::to_string(paint) + "#" + wheel;
        if (auto it = cars_.find(key); it != cars_.end()) return it->second.model ? &it->second : nullptr;
        // Phones keep fewer of the detailed menu cars (Mali drivers spend ~185 KB per texture).
        if (cars_.size() >= (platform::is_mobile() ? 3u : 13u)) cars_.clear();
        Loaded& l = cars_[key];
        const game::GtModeSession* s = game::gt_mode_session();
        if (!s) return nullptr;
        std::string model = s->gt().data().model(car);
        // Menu cars load menu/cars/<model> (e.g. the Miata there is the open roadster;
        // cars/day/ has the hardtop the race uses), the race files as a fallback.
        std::optional<Bytes> file = model.empty() ? std::nullopt : app.vfs()->read("menu/cars/" + model);
        if (!file && !model.empty()) file = app.vfs()->read("cars/day/" + model);
        if (!file) return nullptr;
        try {
            CarModelOptions opt;
            opt.lod = 0;
            opt.menu_tyres = true;
            opt.paint = paint;
            race::CarSetup setup = race::car_setup_from_database(s->gt().db(), car);
            opt.wheel_spec = setup.wheels;
            opt.body_mod = body_mod_for_car(s->gt().db(), car, paint);
            std::optional<Bytes> wheel_file;
            // A fitted wheel: the menus load the detailed menu/wheel/<code> model,
            // the race wheel/<code>.
            if (!wheel.empty()) wheel_file = app.vfs()->read("menu/wheel/" + wheel);
            if (!wheel.empty() && !wheel_file) wheel_file = app.vfs()->read("wheel/" + wheel);
            else if (!opt.body_mod.wheel.empty()) wheel_file = app.vfs()->read("wheel/" + opt.body_mod.wheel);
            if (wheel_file) opt.wheel_override = *wheel_file;
            scene::Model m = load_car_model(*file, opt, model);
            // Showroom pose: the body rides q (ride height, spec 0x5C) above the floor, the
            // wheels stay on it (wheel centres R - q below the body origin).
            if (auto spec = sim::build_car_spec(s->gt().db(), car)) {
                float q = spec->axle[0].ride_height;
                for (scene::Mesh& mesh : m.meshes)
                    if (mesh.name.rfind("wheel_", 0) != 0) mesh.transform[13] += q;
                m.bounds_max[1] += q;
            }
            l.model = std::make_unique<render::GpuModel>(*app.gpu(), m);
            std::copy(m.bounds_min, m.bounds_min + 3, l.bmin);
            std::copy(m.bounds_max, m.bounds_max + 3, l.bmax);
        } catch (const std::exception& ex) {
            LOG_WARN("gt mode: car %s: %s", car.c_str(), ex.what());
            l.model.reset();
        }
        return l.model ? &l : nullptr;
    }

public:
    // Before a race: the models, views and targets kept between frames (the pages load them
    // again when drawn).
    void release(App& app) {
        cars_.clear();
        prize_.reset();
        prize_path_.clear();
        view_black_.reset();
        view_white_.reset();
        for (auto* m : {&targets_, &pass_tex_}) {
            for (auto& [size, t] : *m) {
                if (t.black) SDL_DestroyTexture(t.black);
                if (t.white) SDL_DestroyTexture(t.white);
            }
            m->clear();
        }
        if (app.gpu())
            for (auto& [key, t] : env_maps_)
                if (t) SDL_ReleaseGPUTexture(app.gpu()->device(), t);
        env_maps_.clear();
    }

private:
    std::map<std::string, Loaded> cars_;
    struct Showroom {
        std::string key;
        float yaw = 190, yaw_v = 0, elev = 0, elev_v = 0, dist = 2, dist_v = 0;
        float look[3] = {}, look_v[3] = {};
        int frames = 0;
        int load_wait = 0; // frames spent "loading" (the original's async car load)
        u64 ticks = 0;
        int spin_serial = 0, spin_pushes = 0; // the car wash whose pushes are applied, and how many
    };
    std::map<int, Showroom> shows_; // 0 single / dealer, 1 / 2 the 2 Player cars
    std::map<std::string, ShowroomCamera> cameras_;
    std::unique_ptr<render::PrizeView> prize_;
    std::map<std::pair<int, int>, Targets> pass_tex_; // multi-pass scratch targets (in .black)
    int frame_ = 0; // App::frame of this draw
    std::string prize_path_;
    int prize_serial_ = -1, prize_skip_ = -1;
    u64 prize_ticks_ = 0;
    std::map<std::pair<int, int>, Targets> targets_;
    std::unique_ptr<render::View3D> view_black_, view_white_;
    std::map<u64, SDL_GPUTexture*> env_maps_; // showroom reflection maps (env_map)
};

MenuModelRenderer& menu_models() {
    // Never destroyed: GPU objects must not outlive the app's GPU context at exit.
    static MenuModelRenderer* renderer = new MenuModelRenderer;
    return *renderer;
}

const bool gt_mode_hooks_installed = [] {
    game::install_gt_mode_menu();
    menu::flow::set_gt_mode_widget_drawer([](App& app, SDL_Renderer* r, const menu::Element& e, const SDL_FRect& box,
                                             float alpha, const std::string& page, const menu::MenuImage* image) {
        menu_models().draw(app, r, e, box, alpha, page, image);
    });
    menu::flow::set_gt_mode_widget_release([](App& app) { menu_models().release(app); });
    return true;
}();

class GtModeScreen : public Screen {
public:
    void update(App& app, double) override {
        if (!init_) init(app);
        if (app.menu().pressed(PadButton::Triangle)) app.pop();
        if (app.menu().pressed(PadButton::Down)) ++scroll_;
        if (app.menu().pressed(PadButton::Up) && scroll_ > 0) --scroll_;
    }

    void render(App&, Ui& ui) override {
        ui.header("GT MODE", path_);
        if (!gt_) {
            ui.text(8, 40, error_.empty() ? "loading..." : error_, colors::kBad);
            ui.footer("Triangle: back");
            return;
        }
        const game::GameState& s = gt_->state();
        float y = 34;
        auto line = [&](const std::string& t, Color c = colors::kText) {
            ui.text(8, y, t, c);
            y += 11;
        };
        char b[256];
        std::snprintf(b, sizeof b, "Credits %s Cr.   Day %u   Races %u   Wins %u   Prize money %s Cr.   Completion %s",
                      game::format_credits(s.credits).c_str(), s.day, s.races, s.wins,
                      game::format_credits(s.prize_money).c_str(), gt_->completion_text().c_str());
        line(b, colors::kAccent);
        std::string lic = "Licenses:";
        for (int l = 0; l < game::kLicenseCount; ++l) {
            lic += std::string(" ") + game::license_name(game::License(l)) + "[";
            for (int t = 0; t < 8; ++t) {
                game::Medal m = s.license_tests[size_t(l * 8 + t)].medal;
                lic += m == game::Medal::Gold ? 'G' : m == game::Medal::Silver ? 'S' : m == game::Medal::Bronze ? 'B' : '.';
            }
            lic += "]";
        }
        line(lic);
        int won = 0;
        for (const auto& [code, pos] : s.race_best) won += pos == 0;
        std::snprintf(b, sizeof b, "Races won %d / %zu   Prize cars %u   Wheels %zu", won, gt_->data().race_codes.size(),
                      s.prize_cars, s.wheels.size());
        line(b);
        y += 4;
        std::snprintf(b, sizeof b, "Garage (%zu cars)", s.garage.size());
        line(b, colors::kAccent);
        for (size_t i = size_t(scroll_); i < s.garage.size() && y < Ui::kHeight - 20; ++i) {
            const game::GarageCar& c = s.garage[i];
            const data::Car* car = gt_->db().car(c.car);
            float ps = gt_->power_ps(c);
            // Columns: name, then the figures (proportional text, so not space-padded).
            const Color col = int(i) == s.ride ? colors::kAccent : colors::kText;
            if (int(i) == s.ride) ui.text(8, y, "*", col);
            // (The name is game text: UTF-8 for this font.)
            const std::string name =
                car && !car->latin_name.empty() ? text::game_text_to_utf8(car->latin_name, nullptr) : c.car;
            ui.text(16, y, Ui::clip(name, 216), col);
            ui.text_right(280, y, gt_->power_text(ps, " "), col);
            std::snprintf(b, sizeof b, "%.1f km", c.cond.odometer_m / 1000.0);
            ui.text_right(350, y, b, col);
            std::snprintf(b, sizeof b, "power x%.3f   sell %s Cr.%s", double(game::GtMode::engine_factor(c.cond)),
                          game::format_credits(gt_->sell_price(c)).c_str(), c.origin.empty() ? "" : "  (prize)");
            ui.text(364, y, b, col);
            y += 11;
        }
        ui.footer("Up/Down: scroll   Triangle: back   (play GT mode in the 'gt3menu' screen)");
    }

private:
    void init(App& app) {
        init_ = true;
        if (!app.vfs()) {
            error_ = "No disc loaded";
            return;
        }
        auto data = std::make_shared<game::GtData>();
        if (!data->load(menu::flow::vfs_reader(*app.vfs()))) {
            error_ = data->error();
            return;
        }
        gt_ = std::make_unique<game::GtMode>(data);
        game::ensure_gt_mode_session(menu::flow::vfs_reader(*app.vfs())); // this disc's region's saves
        path_ = game::current_save_path();
        std::string err;
        if (auto st = game::read_save(path_, &err)) gt_->state() = std::move(*st);
        else path_ += " (none: new game)";
    }

    bool init_ = false;
    std::unique_ptr<game::GtMode> gt_;
    std::string path_, error_;
    int scroll_ = 0;
};

} // namespace

GT3R_REGISTER_SCREEN(gtmode, "gtmode", "GT Mode save", "Overview of the GT-mode career save.", 16,
                     [] { return std::make_unique<GtModeScreen>(); });

} // namespace gt3r
