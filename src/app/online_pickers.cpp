// The online lobby's car and course pickers (see online_pickers.h).

#include "app/online_pickers.h"

#include "app/app.h"
#include "app/ui.h"
#include "base/log.h"
#include "formats/model/car_model.h"
#include "formats/model/wheel_spec.h"
#include "game/data/car_database.h"
#include "game/gt_mode_menu.h"
#include "menu/flow/menu_set.h"
#include "menu/flow/menu_sound.h"
#include "menu/flow/options.h"
#include "race/backend.h"
#include "race/paint.h"
#include "render/gpu_model.h"
#include "render/showroom.h"
#include "render/view3d.h"
#include "sim/car_spec.h"
#include "video/ipu.h"
#include "vfs/vfs.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace gt3r {

namespace {

std::string ascii(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (c >= 0x20 && c < 0x7F) out += char(c);
    return out;
}

// "ASTON MARTIN" -> "Aston Martin".
std::string title_case(const std::string& s) {
    std::string out = s;
    bool start = true;
    for (char& c : out) {
        c = start ? char(std::toupper(u8(c))) : char(std::tolower(u8(c)));
        start = !std::isalnum(u8(c));
    }
    return out;
}

// The aspect-kept rectangle of an `aspect` (w / h) picture centred in `dst`.
SDL_FRect fit(const SDL_FRect& dst, float aspect) {
    if (aspect <= 0 || dst.h <= 0) return dst;
    SDL_FRect r = dst;
    if (dst.w / dst.h > aspect) r.w = dst.h * aspect;
    else r.h = dst.w / aspect;
    r.x = dst.x + (dst.w - r.w) * 0.5f;
    r.y = dst.y + (dst.h - r.h) * 0.5f;
    return r;
}

// GT3's strip scroll (menus §9.5): centred and still when it fits in 600 units, else
// the focused icon centred within the strip's ends.
float strip_target(int count, int focus, float step, float icon_w) {
    float total = float(count) * step - (step - icon_w);
    if (total <= 600) return 320 - total * 0.5f;
    float x = 320 - (float(focus) * step + icon_w * 0.5f);
    return std::clamp(x, 620 - total, 20.0f);
}

// The focused icon's pulse: 1.0 on a move, falling to 0.5 over 12 frames, repeating.
float pulse(int frames) { return 1.0f - float(frames % 12) / 24.0f; }

void fill_gradient(Ui& ui, float x, float y, float w, float h, Color top, Color bottom, int bands = 24) {
    for (int i = 0; i < bands; ++i) {
        float t = (float(i) + 0.5f) / float(bands);
        auto mix = [&](u8 a, u8 b) { return u8(float(a) + (float(b) - float(a)) * t + 0.5f); };
        float y0 = y + h * float(i) / float(bands), y1 = y + h * float(i + 1) / float(bands);
        ui.fill(x, y0, w, y1 - y0 + 0.5f,
                Color{mix(top.r, bottom.r), mix(top.g, bottom.g), mix(top.b, bottom.b), mix(top.a, bottom.a)});
    }
}

constexpr Color kStripBack{0, 0, 0, 150};

} // namespace

void picker_sound(App& app, const char* name) {
    static menu::flow::MenuSounds* sounds = nullptr; // kept for the app's life (like the menus' own)
    static bool tried = false;
    if (!tried && app.vfs()) {
        tried = true;
        sounds = new menu::flow::MenuSounds;
        if (!sounds->load(*app.vfs())) LOG_WARN("online pickers: no menu sound effects");
    }
    if (!sounds || !sounds->loaded()) return;
    sounds->set_enabled(menu::flow::option_value(&app.config(), "menu_se") != "off");
    sounds->play(app.audio().voices(), name);
}

void draw_paint_chip(Ui& ui, float x, float y, float w, float h, u32 rgb, bool focused) {
    if (focused) ui.fill_round(x - 2, y - 2, w + 4, h + 4, 3, colors::kAccent);
    ui.fill_round(x - 1, y - 1, w + 2, h + 2, 2, Color{0, 0, 0});
    ui.fill_round(x, y, w, h, 2, Color{u8(rgb), u8(rgb >> 8), u8(rgb >> 16)});
}

// --- disc art ---

// "ASTON MARTIN", "aston_martin" -> "astonmartin".
std::string maker_key(const std::string& s) {
    std::string out;
    for (char c : s)
        if (std::isalnum(u8(c))) out.push_back(char(std::tolower(u8(c))));
    return out;
}

// The maker's entry in `refs`: by its whole name, else by its first word ("MERCEDES-BENZ").
template <class Map>
const typename Map::mapped_type* find_maker(const Map& refs, const std::string& maker) {
    auto it = refs.find(maker_key(maker));
    if (it == refs.end()) {
        size_t end = maker.find_first_of(" -");
        if (end != std::string::npos) it = refs.find(maker_key(maker.substr(0, end)));
    }
    return it == refs.end() ? nullptr : &it->second;
}

struct PickerArt::Ref {
    int scene = -1, image = -1;
    SDL_FRect uv{0, 0, 1, 1};
    float aspect = 1;
};

struct PickerArt::Art {
    std::vector<std::unique_ptr<menu::flow::MenuSet>> sets;
    std::vector<const menu::MenuScene*> scenes;
    std::map<std::string, Ref> car_icons, course_icons, course_maps, course_logos;
    std::map<std::string, Ref> makers, tuner_logos; // by maker_key()
    std::map<std::string, int> maker_region;       // by maker_key(): index into region_flags
    std::vector<Ref> region_flags;
    Ref reflection;
    video::CoursePreviews previews;
    bool previews_ok = false;
};

PickerArt::PickerArt() = default;

PickerArt::~PickerArt() {
    for (auto& [k, t] : textures_)
        if (t) SDL_DestroyTexture(t);
    for (auto& [k, t] : stills_)
        if (t) SDL_DestroyTexture(t);
}

void PickerArt::load(App& app) {
    if (loaded_) return;
    loaded_ = true;
    art_ = std::make_unique<Art>();
    if (!app.vfs()) return;
    Art& a = *art_;
    menu::flow::FileReader read = menu::flow::vfs_reader(*app.vfs());
    std::string lang = app.config().get("gt3menu", "lang").value_or("US");
    if (!app.vfs()->exists("menu/" + lang + "/arcade1.pmb")) lang = "US";
    auto ref_of = [](int scene, const menu::Element& e) {
        Ref r;
        r.scene = scene;
        r.image = int(e.image->image);
        const auto& uv = e.image->uv;
        float u0 = std::min({uv[0], uv[2], uv[4], uv[6]}), u1 = std::max({uv[0], uv[2], uv[4], uv[6]});
        float v0 = std::min({uv[1], uv[3], uv[5], uv[7]}), v1 = std::max({uv[1], uv[3], uv[5], uv[7]});
        r.uv = {u0, v0, u1 - u0, v1 - v0};
        r.aspect = e.h > 0 ? e.w / e.h : 1;
        return r;
    };
    // Every image element of a page (all layers), by name.
    auto page = [&](const std::string& set_name, const char* page_name,
                    const std::function<void(const menu::Element&, const Ref&)>& each) {
        menu::flow::MenuSet* set = nullptr;
        for (auto& s : a.sets)
            if (s->name() == set_name) set = s.get();
        if (!set) {
            try {
                a.sets.push_back(std::make_unique<menu::flow::MenuSet>(read, set_name, lang));
                set = a.sets.back().get();
            } catch (const std::exception& e) {
                LOG_WARN("online pickers: menu set %s: %s", set_name.c_str(), e.what());
                return;
            }
        }
        menu::flow::MenuSet::PageRef ref = set->page(page_name);
        if (!ref) {
            LOG_WARN("online pickers: no menu page %s", page_name);
            return;
        }
        int scene = int(a.scenes.size());
        a.scenes.push_back(ref.scene);
        for (const menu::Layer& l : ref.layout().layers)
            for (const menu::Element& e : l.elements)
                if (e.image && e.image->image < ref.scene->images().size()) each(e, ref_of(scene, e));
    };
    auto ends = [](const std::string& s, const char* suffix) {
        size_t n = std::strlen(suffix);
        return s.size() > n && s.compare(s.size() - n, n, suffix) == 0;
    };
    // Arcade car page: "<car>_mini" strip icons; "car" is the showroom's reflection map.
    page("arcade", "ar/select_car", [&](const menu::Element& e, const Ref& r) {
        if (ends(e.name, "_mini")) a.car_icons[e.name.substr(0, e.name.size() - 5)] = r;
        else if (e.name == "car") a.reflection = r;
    });
    // Arcade course page: "<course>_mini" icons, "<course>_logo" logos, "<course>" maps.
    std::map<std::string, Ref> others;
    page("arcade", "ar/select_course", [&](const menu::Element& e, const Ref& r) {
        if (ends(e.name, "_mini")) a.course_icons[e.name.substr(0, e.name.size() - 5)] = r;
        else if (ends(e.name, "_logo")) a.course_logos[e.name.substr(0, e.name.size() - 5)] = r;
        else others[e.name] = r;
    });
    for (const auto& [name, r] : others)
        if (a.course_icons.count(name)) a.course_maps[name] = r;
    // GT Mode's car dealers (gt_mode.mbl): "cd/cd" lists the regions (Japan, USA, Germany,
    // ...), each region page its dealers: item "dealer<n>" links "cd/cd_<region>_<maker>",
    // and its layer (str80, "s_0<n>") holds the maker's logo "logo<n>" (475 x 190); the
    // region page's "flag" is the country's flag.
    page("gt_mode", "cd/cd", [](const menu::Element&, const Ref&) {});
    for (auto& set : a.sets) {
        if (set->name() != "gt_mode") continue;
        menu::flow::MenuSet::PageRef top = set->page("cd/cd");
        if (!top.flow) break;
        for (const menu::MblItem& region : top.flow->items) {
            menu::flow::MenuSet::PageRef rp = set->page(region.link);
            if (!rp || !rp.flow) continue;
            int scene = int(a.scenes.size());
            a.scenes.push_back(rp.scene);
            int ri = int(a.region_flags.size());
            Ref flag;
            for (const menu::Layer& l : rp.layout().layers)
                for (const menu::Element& e : l.elements)
                    if (e.name == "flag" && e.image) flag = ref_of(scene, e);
            a.region_flags.push_back(flag);
            for (const menu::MblItem& it : rp.flow->items) {
                if (it.link.size() <= region.link.size() + 1) continue;
                std::string maker = maker_key(it.link.substr(region.link.size() + 1));
                for (const menu::Layer& l : rp.layout().layers)
                    if (l.name == it.str80)
                        for (const menu::Element& e : l.elements)
                            if (e.name.rfind("logo", 0) == 0 && e.image &&
                                e.image->image < rp.scene->images().size()) {
                                a.makers[maker] = ref_of(scene, e);
                                a.maker_region[maker] = ri;
                            }
            }
        }
    }
    // Makers without a dealer (tuners such as Mines or Spoon): the tune shop's logos
    // (ts/ts, 300 x 100). Only exact names: the shop shows tuners (Nissan's is Nismo's).
    page("gt_mode", "ts/ts", [&](const menu::Element& e, const Ref& r) {
        if (std::fabs(e.w - 300) < 1 && std::fabs(e.h - 100) < 1) a.tuner_logos.emplace(maker_key(e.name), r);
    });
    a.previews_ok = a.previews.load(*app.vfs());
    LOG_INFO("online pickers: %zu car icons, %zu maker logos, %zu course icons, %zu course stills",
             a.car_icons.size(), a.makers.size(), a.course_icons.size(),
             a.previews_ok ? a.previews.entries().size() : size_t(0));
}

SDL_Texture* PickerArt::texture(App& app, int scene, int image) {
    auto key = std::make_pair(scene, image);
    if (auto it = textures_.find(key); it != textures_.end()) return it->second;
    SDL_Texture* t = nullptr;
    const menu::MenuImage& img = art_->scenes[size_t(scene)]->images()[size_t(image)];
    if (!img.rgba.empty()) {
        t = SDL_CreateTexture(app.renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(img.width),
                              int(img.height));
        if (t) {
            SDL_UpdateTexture(t, nullptr, img.rgba.data(), int(img.width * 4));
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        }
    }
    textures_[key] = t;
    return t;
}

namespace {

const PickerArt::Image kNoImage{};

template <class Refs>
const typename Refs::mapped_type* lookup(const Refs& refs, const std::string& name) {
    auto it = refs.find(name);
    if (it == refs.end() && !name.empty() && name[0] == '_') it = refs.find(name.substr(1));
    if (it == refs.end()) it = refs.find("_" + name);
    return it == refs.end() ? nullptr : &it->second;
}

} // namespace

bool PickerArt::draw_ref(App& app, const Ref* r, const SDL_FRect& dst, float alpha, float bright) {
    if (!r) return false;
    SDL_Texture* t = texture(app, r->scene, r->image);
    if (!t) return false;
    float tw = 0, th = 0;
    SDL_GetTextureSize(t, &tw, &th);
    SDL_FRect src{r->uv.x * tw, r->uv.y * th, r->uv.w * tw, r->uv.h * th};
    SDL_FRect d = fit(dst, r->aspect);
    SDL_SetTextureAlphaModFloat(t, std::clamp(alpha, 0.0f, 1.0f));
    SDL_SetTextureColorModFloat(t, bright, bright, bright);
    SDL_RenderTexture(app.renderer(), t, &src, &d);
    SDL_SetTextureColorModFloat(t, 1, 1, 1);
    return true;
}

bool PickerArt::car_icon(App& app, const std::string& car, const SDL_FRect& dst, float alpha, float bright) {
    load(app);
    return draw_ref(app, lookup(art_->car_icons, car), dst, alpha, bright);
}

bool PickerArt::course_icon(App& app, const std::string& course, const SDL_FRect& dst, float alpha) {
    load(app);
    return draw_ref(app, lookup(art_->course_icons, course), dst, alpha);
}

bool PickerArt::course_map(App& app, const std::string& course, const SDL_FRect& dst, float alpha) {
    load(app);
    return draw_ref(app, lookup(art_->course_maps, course), dst, alpha);
}

bool PickerArt::course_logo(App& app, const std::string& course, const SDL_FRect& dst, float alpha) {
    load(app);
    return draw_ref(app, lookup(art_->course_logos, course), dst, alpha);
}

bool PickerArt::maker_logo(App& app, const std::string& maker, const SDL_FRect& dst, float alpha) {
    load(app);
    const Ref* r = find_maker(art_->makers, maker);
    if (!r)
        if (auto it = art_->tuner_logos.find(maker_key(maker)); it != art_->tuner_logos.end()) r = &it->second;
    return draw_ref(app, r, dst, alpha);
}

int PickerArt::maker_region(App& app, const std::string& maker) {
    load(app);
    const int* r = find_maker(art_->maker_region, maker);
    return r ? *r : -1;
}

bool PickerArt::region_flag(App& app, int region, const SDL_FRect& dst, float alpha) {
    load(app);
    if (region < 0 || size_t(region) >= art_->region_flags.size()) return false;
    const Ref& r = art_->region_flags[size_t(region)];
    return r.scene >= 0 && draw_ref(app, &r, dst, alpha);
}

bool PickerArt::course_still(App& app, const std::string& course, const SDL_FRect& dst, bool cover, float alpha) {
    load(app);
    if (!art_->previews_ok) return false;
    auto it = stills_.find(course);
    if (it == stills_.end()) {
        SDL_Texture* t = nullptr;
        if (const video::RgbaImage* img = art_->previews.image(course)) {
            t = SDL_CreateTexture(app.renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img->width,
                                  img->height);
            if (t) {
                SDL_UpdateTexture(t, nullptr, img->rgba.data(), img->width * 4);
                SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
            }
        }
        it = stills_.emplace(course, t).first;
    }
    SDL_Texture* t = it->second;
    if (!t) return false;
    float tw = 0, th = 0;
    SDL_GetTextureSize(t, &tw, &th);
    // The stills are 640 x 448 frames shown at 4:3.
    const float aspect = 4.0f / 3.0f;
    SDL_FRect src{0, 0, tw, th}, d = dst;
    if (cover) {
        float da = dst.w / std::max(dst.h, 1.0f);
        if (da > aspect) { // wider: crop top and bottom
            float h = th * aspect / da;
            src = {0, (th - h) * 0.5f, tw, h};
        } else {
            float w = tw * da / aspect;
            src = {(tw - w) * 0.5f, 0, w, th};
        }
    } else {
        d = fit(dst, aspect);
    }
    SDL_SetTextureAlphaModFloat(t, std::clamp(alpha, 0.0f, 1.0f));
    SDL_RenderTexture(app.renderer(), t, &src, &d);
    return true;
}

PickerArt::Image PickerArt::reflection_map(App& app) {
    load(app);
    const Ref& r = art_->reflection;
    if (r.scene < 0) return kNoImage;
    const menu::MenuImage& img = art_->scenes[size_t(r.scene)]->images()[size_t(r.image)];
    if (img.rgba.empty()) return kNoImage;
    return {img.width, img.height, &img.rgba};
}

// --- the turning car ---

struct CarTurntable::State {
    render::GpuContext* gpu = nullptr;
    std::unique_ptr<render::View3D> view, mirror;
    std::unique_ptr<render::GpuModel> model;
    float bmin[3]{}, bmax[3]{};
    std::string shown, shown_car; // key of the loaded model, its car label
    std::string want;
    int rest = 0;
    render::ShowroomCamera cam;
    bool cam_tried = false;
    SDL_GPUTexture* env = nullptr;
    bool env_tried = false;
    // The arcade showroom's motion (menus §9.5): yaw spinning up from its start, the
    // distance springing out; `frames` drives the fade-in.
    float yaw = 190, yaw_v = 0, dist = 0.4f, dist_v = 0;
    int frames = 0;
    u64 ticks = 0;
};

CarTurntable::CarTurntable() = default;

CarTurntable::~CarTurntable() {
    if (s_ && s_->env && s_->gpu) SDL_ReleaseGPUTexture(s_->gpu->device(), s_->env);
}

void CarTurntable::draw(App& app, const SDL_FRect& dst, const data::CarDatabase& db, const PickerCar& car, int paint,
                        PickerArt& art) {
    if (!app.gpu() || !app.vfs() || dst.w < 8 || dst.h < 8) return;
    if (!s_) s_ = std::make_unique<State>();
    State& s = *s_;
    s.gpu = app.gpu();
    if (!s.cam_tried) {
        s.cam_tried = true;
        if (std::optional<Bytes> f = app.vfs()->read("menu/showroom_single.fbi")) s.cam = render::parse_showroom_camera(*f);
    }
    if (!s.env_tried) {
        s.env_tried = true;
        PickerArt::Image img = art.reflection_map(app);
        if (img.rgba && img.width >= 16 && img.height >= 16) {
            render::Uploader up(app.gpu()->device());
            s.env = up.texture_rgba(img.width, img.height, img.rgba->data(), true);
            if (!up.submit()) s.env = nullptr;
        }
    }
    std::string key = car.label + "#" + std::to_string(paint) + "#" + car.wheel;
    if (key != s.want) {
        s.want = key;
        s.rest = 0;
        if (car.label != s.shown_car) { // a new car: nothing until it has loaded, then the intro
            s.model.reset();
            s.shown.clear();
        }
    }
    // The model loads once the choice has rested (scrolling through the strip stays quick).
    if (s.shown != s.want && ++s.rest >= (s.model ? 3 : 8)) {
        bool new_car = car.label != s.shown_car;
        s.shown = s.want;
        s.shown_car = car.label;
        s.model.reset();
        std::optional<Bytes> file;
        if (!car.model.empty()) {
            // Menu cars load menu/cars/<model> (the showroom's own models), the race files as a fallback.
            file = app.vfs()->read("menu/cars/" + car.model);
            if (!file) file = app.vfs()->read("cars/day/" + car.model);
        }
        if (file) try {
                CarModelOptions opt;
                opt.lod = 0;
                opt.menu_tyres = true;
                opt.paint = paint;
                opt.wheel_spec = race::car_setup_from_database(db, car.label).wheels;
                opt.body_mod = body_mod_for_car(db, car.label, paint);
                std::optional<Bytes> wheel_file;
                if (!car.wheel.empty()) wheel_file = app.vfs()->read("wheel/" + car.wheel);
                else if (!opt.body_mod.wheel.empty()) wheel_file = app.vfs()->read("wheel/" + opt.body_mod.wheel);
                if (wheel_file) opt.wheel_override = *wheel_file;
                scene::Model m = load_car_model(*file, opt, car.model);
                // Showroom pose: the body rides q (ride height) above the floor, the wheels on it.
                if (auto spec = sim::build_car_spec(db, car.label); spec && spec->axle[0].susp.valid) {
                    float q = spec->axle[0].susp.ride_height;
                    for (scene::Mesh& mesh : m.meshes)
                        if (mesh.name.rfind("wheel_", 0) != 0) mesh.transform[13] += q;
                    m.bounds_max[1] += q;
                }
                s.model = std::make_unique<render::GpuModel>(*app.gpu(), m);
                std::copy(m.bounds_min, m.bounds_min + 3, s.bmin);
                std::copy(m.bounds_max, m.bounds_max + 3, s.bmax);
            } catch (const std::exception& e) {
                LOG_WARN("online pickers: car %s: %s", car.label.c_str(), e.what());
            }
        if (new_car) {
            s.yaw = s.cam.valid ? s.cam.key(0x3E) : 190;
            s.yaw_v = 0;
            s.dist = 0.4f, s.dist_v = 0;
            s.frames = 0;
            s.ticks = SDL_GetTicksNS();
        }
    }
    if (!s.model) return;
    // 60 Hz steps of the showroom's integrator: v += a / 60, v -= v c / 60, x += v / 60.
    u64 now = SDL_GetTicksNS();
    int steps = std::clamp(int(double(now - s.ticks) / 1e9 * 60.0 + 0.5), 0, 30);
    if (steps > 0) s.ticks = now;
    const float yaw_a = s.cam.valid ? s.cam.key(0x42) : -10, damp = s.cam.valid ? s.cam.key(0x43) : 0.5f;
    for (int i = 0; i < steps; ++i) {
        s.dist_v += (1.0f - s.dist) * 6.0f / 60.0f; // the 2 -> 6 m spring, as a fraction of the framing
        s.dist_v -= s.dist_v * 6.0f / 60.0f;
        s.dist += s.dist_v / 60.0f;
        s.yaw_v += yaw_a / 60.0f;
        s.yaw_v -= s.yaw_v * damp / 60.0f;
        s.yaw += s.yaw_v / 60.0f;
        ++s.frames;
    }
    // Camera: the showroom's elevation and field of view, framed on the car.
    const float d2r = 3.14159265f / 180.0f;
    const float hfov = (s.cam.valid ? s.cam.key(0x4B) : 53.13f) * d2r;
    const float elev = (s.cam.valid ? -s.cam.key(0x3D) : 10.0f) * d2r;
    const float aspect = dst.w / dst.h;
    const float vfov = 2.0f * std::atan(std::tan(hfov * 0.5f) / aspect);
    const float sx = s.bmax[0] - s.bmin[0], sy = s.bmax[1] - s.bmin[1], sz = s.bmax[2] - s.bmin[2];
    const float radius = 0.5f * std::sqrt(sx * sx + sz * sz);
    const float fit_d = std::max(radius / std::tan(hfov * 0.5f), (0.5f * sy + 0.35f * radius) / std::tan(vfov * 0.5f));
    const float dist = std::max(0.5f, fit_d * 1.05f * s.dist);
    const render::Vec3 target{0.5f * (s.bmin[0] + s.bmax[0]), 0.45f * sy + s.bmin[1], 0.5f * (s.bmin[2] + s.bmax[2])};
    const float yaw = s.yaw * d2r;
    const render::Vec3 eye = target + render::Vec3{std::sin(yaw) * std::cos(elev), std::sin(elev),
                                                   std::cos(yaw) * std::cos(elev)} * dist;
    const render::Mat4 view = render::look_at(eye, target, {0, 1, 0});
    const render::Mat4 proj = render::perspective(vfov, aspect, 0.1f, 1000.0f);
    const float fade = std::clamp(float(s.frames - 5) / 40.0f, 0.0f, 1.0f);
    if (fade <= 0) return;
    if (!s.view) s.view = std::make_unique<render::View3D>(*app.gpu());
    if (!s.mirror) s.mirror = std::make_unique<render::View3D>(*app.gpu());
    SDL_Renderer* r = app.renderer();
    // The floor reflection: the car mirrored about the floor (a rotation, so faces keep their
    // winding), faint, drawn first.
    {
        render::DrawOptions o = s.cam.valid ? render::showroom_options(s.cam, view, true, s.env) : render::DrawOptions{};
        o.clear[0] = o.clear[1] = o.clear[2] = o.clear[3] = 0;
        o.composite_alpha = 0.3f * fade;
        render::DrawItem it{s.model.get(), render::scale({-1, -1, 1}), 0};
        s.mirror->draw(r, dst, view, proj, eye, {it}, o);
        SDL_FlushRenderer(r);
    }
    render::DrawOptions o = s.cam.valid ? render::showroom_options(s.cam, view, false, s.env) : render::DrawOptions{};
    o.clear[0] = o.clear[1] = o.clear[2] = o.clear[3] = 0;
    o.composite_alpha = fade;
    s.view->draw(r, dst, view, proj, eye, {{s.model.get(), {}, 0}}, o);
    SDL_FlushRenderer(r);
}

// --- car picker ---

CarPicker::CarPicker() = default;
CarPicker::~CarPicker() = default;

std::vector<PaintInfo> CarPicker::paints(App& app, const PickerCar& c) const {
    std::vector<PaintInfo> out;
    const game::GtData* gd = nullptr;
    if (game::GtModeSession* gs = game::gt_mode_session()) gd = &gs->gt().data();
    auto info = [&](u32 id) {
        PaintInfo p;
        p.rgb = gd ? gd->color_rgb(id) : 0x808080;
        std::string n = gd ? ascii(gd->color_name(id)) : "";
        if (!n.empty() && n[0] != '-') p.name = n; // names starting with '-' show nothing (menus §9.5)
        return p;
    };
    if (gd) {
        if (c.garage >= 0) {
            u32 id = gd->color_id(c.label, c.paint);
            if (id != 0xFFFFFFFFu) out.push_back(info(id));
        } else {
            int n = gd->color_count(c.label);
            for (int i = 0; i < n; ++i) out.push_back(info(gd->color_id(c.label, i)));
        }
    } else if (app.vfs()) {
        static race::CarColors colors; // the disc's paint table, when GT Mode data is not loaded
        static bool loaded = colors.load(*app.vfs());
        if (loaded)
            if (const std::vector<u32>* ids = colors.ids(c.model)) {
                if (c.garage >= 0) {
                    if (c.paint >= 0 && size_t(c.paint) < ids->size()) out.push_back({colors.rgb((*ids)[size_t(c.paint)]), ""});
                } else {
                    for (u32 id : *ids) out.push_back({colors.rgb(id), ""});
                }
            }
    }
    if (out.empty()) out.push_back({0x808080, ""});
    return out;
}

// The garage first, then the makers by dealer region in GT Mode's order (Japan, USA,
// Germany, ...) and name; makers without a dealer last.
void CarPicker::build_groups(App& app) {
    groups_.clear();
    Group garage{"", "GARAGE", {}, -1};
    // By maker_key(): the database spells some makers two ways ("CHEVROLET" with and without
    // a trailing space).
    std::map<std::string, std::pair<std::string, std::vector<int>>> by_maker;
    for (size_t i = 0; i < cars_->size(); ++i) {
        const PickerCar& c = (*cars_)[i];
        if (c.garage >= 0) {
            garage.cars.push_back(int(i));
            continue;
        }
        std::string maker = ascii(c.maker);
        while (!maker.empty() && maker.back() == ' ') maker.pop_back();
        if (maker.empty()) maker = "OTHER";
        auto& g = by_maker[maker_key(maker)];
        if (g.first.empty()) g.first = maker;
        g.second.push_back(int(i));
    }
    if (!garage.cars.empty()) groups_.push_back(std::move(garage));
    size_t first = groups_.size();
    for (auto& [key, g] : by_maker)
        groups_.push_back({g.first, title_case(g.first), std::move(g.second), art_->maker_region(app, g.first)});
    std::stable_sort(groups_.begin() + ptrdiff_t(first), groups_.end(), [](const Group& a, const Group& b) {
        return unsigned(a.region) < unsigned(b.region); // -1 (no dealer) last
    });
}

void CarPicker::open(App& app, PickerArt& art, const data::CarDatabase& db, const std::vector<PickerCar>& cars, int selected,
                     int paint, std::function<std::string(int, int)> note) {
    art_ = &art;
    db_ = &db;
    cars_ = &cars;
    note_ = std::move(note);
    build_groups(app);
    car_ = std::clamp(selected, 0, std::max(0, int(cars.size()) - 1));
    paint_ = paint;
    group_ = 0, pos_ = 0;
    for (size_t g = 0; g < groups_.size(); ++g)
        for (size_t k = 0; k < groups_[g].cars.size(); ++k)
            if (groups_[g].cars[k] == car_) group_ = int(g), pos_ = int(k);
    in_group_ = !groups_.empty();
    maker_scroll_ = 0;
    strip_x_ = in_group_ ? strip_target(int(groups_[size_t(group_)].cars.size()), pos_, 55, 51) : 0;
    paint_y_ = float(paint_);
    pulse_ = 0;
}

void CarPicker::enter_group(int g, int pos) {
    group_ = g;
    pos_ = pos;
    in_group_ = true;
    car_ = groups_[size_t(g)].cars[size_t(pos)];
    paint_ = 0;
    paint_y_ = 0;
    strip_x_ = strip_target(int(groups_[size_t(g)].cars.size()), pos, 55, 51);
    pulse_ = 0;
}

CarPicker::Result CarPicker::update(App& app) {
    const MenuInput& m = app.menu();
    ++pulse_;
    if (groups_.empty()) return m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle) ? Result::Cancelled
                                                                                                : Result::None;
    if (!in_group_) {
        constexpr int kCols = 4;
        int n = int(groups_.size());
        int h = m.horizontal(), v = m.vertical();
        if (h || v) {
            int to = std::clamp(group_ + h + v * kCols, 0, n - 1);
            if (to != group_) group_ = to, picker_sound(app, "cursor");
            else picker_sound(app, "disable");
        }
        if (m.pressed(PadButton::Cross)) {
            picker_sound(app, "ok");
            // Back into the group of the lobby's car at that car, else at its first.
            int pos = 0;
            for (size_t k = 0; k < groups_[size_t(group_)].cars.size(); ++k)
                if (groups_[size_t(group_)].cars[k] == car_) pos = int(k);
            int keep_paint = groups_[size_t(group_)].cars[size_t(pos)] == car_ ? paint_ : 0;
            enter_group(group_, pos);
            paint_ = keep_paint, paint_y_ = float(paint_);
        } else if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) {
            picker_sound(app, "cancel");
            return Result::Cancelled;
        }
        return Result::None;
    }
    const Group& g = groups_[size_t(group_)];
    int n = int(g.cars.size());
    int h = m.horizontal();
    if (m.pressed(PadButton::L1)) h = -10;
    if (m.pressed(PadButton::R1)) h = 10;
    if (h) {
        int to = std::clamp(pos_ + h, 0, n - 1);
        if (to != pos_) {
            pos_ = to;
            car_ = g.cars[size_t(pos_)];
            paint_ = 0; // each new car starts at its first paint
            pulse_ = 0;
            picker_sound(app, "cursor");
        } else {
            picker_sound(app, "disable"); // the strip clamps at its ends
        }
    }
    if (int v = m.vertical()) {
        int count = (*cars_)[size_t(car_)].garage >= 0 ? 1 : int(paints(app, (*cars_)[size_t(car_)]).size());
        int to = std::clamp(paint_ + v, 0, count - 1);
        if (to != paint_) paint_ = to, picker_sound(app, "cursor");
        else picker_sound(app, "disable");
    }
    if (m.pressed(PadButton::Cross)) {
        picker_sound(app, "ok");
        return Result::Chosen;
    }
    if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) {
        picker_sound(app, "cancel");
        in_group_ = false;
    }
    return Result::None;
}

void CarPicker::render(App& app, Ui& ui) {
    if (!cars_ || groups_.empty()) {
        ui.clear();
        ui.header("CAR SELECT", "");
        ui.text_center(150, "No cars", colors::kBad);
        ui.footer("Triangle: back");
        return;
    }
    if (in_group_) render_cars(app, ui);
    else render_makers(app, ui);
}

void CarPicker::render_makers(App& app, Ui& ui) {
    ui.clear();
    fill_gradient(ui, 0, 20, Ui::kWidth, Ui::kHeight - 36, Color{24, 28, 40}, Color{8, 9, 12});
    ui.header("CAR SELECT", "Choose a maker");
    constexpr int kCols = 4, kRows = 4;
    constexpr float kW = 148, kH = 60, kGap = 6, kX0 = (Ui::kWidth - (kCols * kW + (kCols - 1) * kGap)) / 2,
                    kY0 = 30;
    int row = group_ / kCols;
    if (row < maker_scroll_) maker_scroll_ = row;
    if (row >= maker_scroll_ + kRows) maker_scroll_ = row - kRows + 1;
    for (int i = maker_scroll_ * kCols; i < int(groups_.size()) && i < (maker_scroll_ + kRows) * kCols; ++i) {
        const Group& g = groups_[size_t(i)];
        float x = kX0 + float(i % kCols) * (kW + kGap), y = kY0 + float(i / kCols - maker_scroll_) * (kH + kGap);
        bool f = i == group_;
        ui.fill_round(x, y, kW, kH, 6, f ? Color{58, 64, 92} : Color{30, 33, 44, 230});
        if (f) ui.frame_round(x, y, kW, kH, 6, 2, colors::kAccent);
        if (!g.maker.empty()) art_->region_flag(app, g.region, SDL_FRect{x + 5, y + 4, 15, 12}, f ? 1.0f : 0.7f);
        SDL_FRect logo{x + 22, y + 4, kW - 44, kH - 19};
        bool drawn = !g.maker.empty() && art_->maker_logo(app, g.maker, logo, f ? 1.0f : 0.8f);
        if (!drawn) {
            std::string t = Ui::clip(g.title, kW - 12);
            float tw = Ui::text_width(t, 2);
            if (tw <= kW - 12) ui.text_big(x + (kW - tw) / 2, y + 14, t, 2, f ? colors::kAccent : colors::kText);
            else ui.text_center_in(x, kW, y + 18, t, f ? colors::kAccent : colors::kText);
        }
        std::string count = std::to_string(g.cars.size()) + (g.cars.size() == 1 ? " car" : " cars");
        if (g.maker.empty()) count = "GT Mode, " + count;
        else if (drawn) count = g.title + "  -  " + count; // makers may share a logo (two "Chevrolet")
        ui.text_center_in(x, kW, y + kH - 13, count, colors::kDim);
    }
    int rows = (int(groups_.size()) + kCols - 1) / kCols;
    if (rows > kRows) {
        UiRect view{Ui::kWidth - 8, kY0, 4, kRows * (kH + kGap) - kGap};
        ui.scrollbar(view, float(rows) * (kH + kGap), float(maker_scroll_) * (kH + kGap));
    }
    ui.text_center(kY0 + kRows * (kH + kGap) + 4, groups_[size_t(group_)].title, colors::kAccent);
    ui.footer("Arrows: maker   Cross: its cars   Triangle: back to the lobby");
}

void CarPicker::render_cars(App& app, Ui& ui) {
    const Group& g = groups_[size_t(group_)];
    const PickerCar& c = (*cars_)[size_t(car_)];
    ui.clear();
    // The showroom: a dark backdrop with a lit floor.
    fill_gradient(ui, 0, 20, Ui::kWidth, 200, Color{30, 34, 48}, Color{58, 62, 76});
    fill_gradient(ui, 0, 220, Ui::kWidth, Ui::kHeight - 236, Color{40, 42, 52}, Color{10, 10, 14});
    ui.header("CAR SELECT", g.title);

    std::vector<PaintInfo> pal = paints(app, c);
    int paint = c.garage >= 0 ? c.paint : std::clamp(paint_, 0, int(pal.size()) - 1);
    turntable_.draw(app, SDL_FRect{70, 40, 430, 222}, *db_, c, paint, *art_);

    // Name and maker.
    const data::Car* dc = db_->car(c.label);
    std::string name = c.name.rfind("Garage: ", 0) == 0 ? c.name.substr(8) : c.name;
    if (!art_->maker_logo(app, c.maker, SDL_FRect{12, 24, 96, 32}))
        ui.text(16, 34, title_case(c.maker), colors::kDim);
    ui.text_big(118, 28, Ui::clip(name, 500), 2, colors::kText);

    // Paint column (menus §9.5: the current paint, up to two slots above and four below), eased.
    paint_y_ += (float(c.garage >= 0 ? 0 : paint) - paint_y_) * 0.3f;
    {
        const float cx = 34, cy = 120, pitch = 20;
        int n = c.garage >= 0 ? 1 : int(pal.size());
        int cur = c.garage >= 0 ? 0 : paint;
        for (int i = 0; i < n; ++i) {
            float off = float(i) - paint_y_;
            if (off < -2.5f || off > 4.5f) continue;
            float y = cy + off * pitch;
            float a = off < -1.5f || off > 3.5f ? 0.4f : 1.0f;
            u32 rgb = c.garage >= 0 ? pal[0].rgb : pal[size_t(i)].rgb;
            float sz = i == cur ? 16.0f : 12.0f;
            Color k{u8(rgb), u8(rgb >> 8), u8(rgb >> 16), u8(255 * a)};
            if (i == cur) ui.fill_round(cx - sz / 2 - 2, y - sz / 2 - 2, sz + 4, sz + 4, 3, colors::kAccent);
            ui.fill_round(cx - sz / 2 - 1, y - sz / 2 - 1, sz + 2, sz + 2, 2, Color{0, 0, 0, u8(255 * a)});
            ui.fill_round(cx - sz / 2, y - sz / 2, sz, sz, 2, k);
        }
        if (c.garage < 0 && cur > 0) ui.text_center_in(cx - 10, 20, cy - pitch * 1.0f - 20, "^", colors::kDim);
        if (c.garage < 0 && cur + 1 < n) ui.text_center_in(cx - 10, 20, cy + pitch * 4.6f, "v", colors::kDim);
        std::string pname = pal[size_t(c.garage >= 0 ? 0 : cur)].name;
        std::string line = c.garage >= 0 ? "the garage car's own paint"
                                         : std::to_string(cur + 1) + " / " + std::to_string(n);
        if (!pname.empty()) ui.text(14, 238, Ui::clip(pname, 220), colors::kText);
        ui.text(14, 250, line, colors::kDim);
    }

    // Specs.
    if (dc) {
        data::CarDatabase::Summary sm = db_->summarize(*dc);
        float x = 512, y = 70;
        auto spec = [&](const char* label, const std::string& value) {
            if (value.empty()) return;
            ui.text(x, y, label, colors::kDim);
            ui.text(x, y + 11, Ui::clip(value, 120), colors::kText);
            y += 28;
        };
        char buf[64];
        if (sm.year) spec("Year", std::to_string(sm.year));
        spec("Drive", ascii(sm.drive));
        // Power as the game's menus show it: a garage car's computed peak (parts and
        // condition), else the catalogue figure; truncated, HP on NTSC-U data.
        game::GtModeSession* gs = game::gt_mode_session();
        const bool imperial = gs ? gs->gt().data().imperial : Disc::current_db_suffix() == "_us";
        const std::string unit = std::string(" ") + game::power_unit(imperial);
        if (c.garage >= 0 && gs && c.garage < int(gs->gt().state().garage.size())) {
            game::PowerPeak p = gs->gt().power_peak(gs->gt().state().garage[size_t(c.garage)]);
            spec("Power", std::to_string(game::display_power(p.ps, imperial)) + unit + " / " + std::to_string(p.rpm) + "rpm");
        } else if (sm.power_ps) {
            spec("Power", std::to_string(game::display_power(float(sm.power_ps), imperial)) + unit +
                              (sm.power_rpm.empty() ? "" : " / " + ascii(sm.power_rpm)));
        }
        if (sm.torque_kgfm > 0) {
            std::snprintf(buf, sizeof buf, "%.1f kgfm", double(sm.torque_kgfm));
            spec("Torque", buf + (sm.torque_rpm.empty() ? "" : " / " + ascii(sm.torque_rpm)));
        }
        if (sm.weight_kg) spec("Weight", std::to_string(sm.weight_kg) + " kg");
        if (c.garage >= 0) spec("Setup", "garage parts and settings");
        else if (c.mod) spec("Mod car", "others are offered it");
    }

    // The icon strip.
    int n = int(g.cars.size());
    constexpr float kStep = 55, kIconW = 51, kIconH = 27, kStripY = 272;
    strip_x_ += (strip_target(n, pos_, kStep, kIconW) - strip_x_) * 0.3f;
    ui.fill(0, kStripY - 5, Ui::kWidth, kIconH + 10, kStripBack);
    SDL_Rect clip{20, int(kStripY - 4), 600, int(kIconH + 8)};
    SDL_SetRenderClipRect(app.renderer(), &clip);
    for (int i = 0; i < n; ++i) {
        float x = strip_x_ + float(i) * kStep;
        if (x + kIconW < 20 || x > 620) continue;
        const PickerCar& e = (*cars_)[size_t(g.cars[size_t(i)])];
        bool f = i == pos_;
        float bright = f ? pulse(pulse_) * 0.4f + 0.6f : 0.7f;
        SDL_FRect d{x, kStripY, kIconW, kIconH};
        if (!art_->car_icon(app, e.label, d, 1, bright)) {
            ui.fill_round(x, kStripY, kIconW, kIconH, 3, Color{50, 54, 70});
            std::string nm = e.name.rfind("Garage: ", 0) == 0 ? e.name.substr(8) : e.name;
            ui.text_center_in(x, kIconW, kStripY + 9, Ui::clip(nm, kIconW - 4), f ? colors::kAccent : colors::kDim);
        }
        if (f) ui.frame_round(x - 2, kStripY - 2, kIconW + 4, kIconH + 4, 3, 1, colors::kAccent);
    }
    SDL_SetRenderClipRect(app.renderer(), nullptr);
    if (pos_ > 0) ui.text(6, kStripY + 9, "<", colors::kAccent);
    if (pos_ + 1 < n) ui.text_right(Ui::kWidth - 6, kStripY + 9, ">", colors::kAccent);
    std::string note = note_ ? note_(car_, paint) : "";
    if (!note.empty()) ui.text_center(kStripY + 38, Ui::clip(note, 600), colors::kBad);
    else ui.text_center(kStripY + 38, std::to_string(pos_ + 1) + " / " + std::to_string(n), colors::kDim);
    ui.footer("Left/Right: car (L1/R1: x10)   Up/Down: paint   Cross: choose   Triangle: makers");
}

// --- course picker ---

void CoursePicker::open(const std::vector<PickerCourse>& courses, int selected,
                        std::function<u32(const std::string&)> length_m) {
    courses_ = &courses;
    length_m_ = std::move(length_m);
    sel_ = std::clamp(selected, 0, std::max(0, int(courses.size()) - 1));
    prev_ = -1;
    strip_x_ = strip_target(int(courses.size()), sel_, 59, 55);
    pulse_ = 0;
    fade_ = 100;
}

CoursePicker::Result CoursePicker::update(App& app) {
    const MenuInput& m = app.menu();
    ++pulse_, ++fade_;
    int n = courses_ ? int(courses_->size()) : 0;
    int h = m.horizontal();
    if (m.pressed(PadButton::L1)) h = -10;
    if (m.pressed(PadButton::R1)) h = 10;
    if (h && n) {
        int to = std::clamp(sel_ + h, 0, n - 1);
        if (to != sel_) {
            prev_ = sel_;
            sel_ = to;
            pulse_ = fade_ = 0;
            picker_sound(app, "cursor");
        } else {
            picker_sound(app, "disable");
        }
    }
    if (m.pressed(PadButton::Cross) && n) {
        picker_sound(app, "ok");
        return Result::Chosen;
    }
    if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) {
        picker_sound(app, "cancel");
        return Result::Cancelled;
    }
    return Result::None;
}

void CoursePicker::render(App& app, Ui& ui, PickerArt& art) {
    ui.clear();
    int n = courses_ ? int(courses_->size()) : 0;
    if (!n) {
        ui.header("COURSE SELECT", "");
        ui.text_center(150, "No courses", colors::kBad);
        ui.footer("Triangle: back");
        return;
    }
    const PickerCourse& c = (*courses_)[size_t(sel_)];
    // The course's still behind everything; the previous one fades out over a few frames.
    const SDL_FRect screen{0, 0, Ui::kWidth, Ui::kHeight};
    float t = std::clamp(float(fade_) / 12.0f, 0.0f, 1.0f);
    if (prev_ >= 0 && t < 1) art.course_still(app, (*courses_)[size_t(prev_)].label, screen, true, 1);
    if (!art.course_still(app, c.label, screen, true, t))
        fill_gradient(ui, 0, 0, Ui::kWidth, Ui::kHeight, Color{24, 28, 40, u8(255 * t)}, Color{8, 9, 12, u8(255 * t)});
    fill_gradient(ui, 0, 20, Ui::kWidth, 90, Color{0, 0, 0, 150}, Color{0, 0, 0, 0});
    fill_gradient(ui, 0, 200, Ui::kWidth, 70, Color{0, 0, 0, 0}, Color{0, 0, 0, 170});
    ui.header("COURSE SELECT", "The host chooses the course");

    // Logo (or the name) at the top left, the map at the right.
    float slide = (1 - t) * 24;
    if (!art.course_logo(app, c.label, SDL_FRect{14 - slide, 26, 240, 80}, t))
        ui.text_big(20 - slide, 44, Ui::clip(c.name, 400), 3, colors::kText);
    {
        SDL_FRect box{456 + slide, 28, 168, 126};
        ui.fill_round(box.x, box.y, box.w, box.h, 6, Color{0, 0, 0, u8(140 * t)});
        if (!art.course_map(app, c.label, SDL_FRect{box.x + 6, box.y + 6, box.w - 12, box.h - 12}, t))
            ui.text_center_in(box.x, box.w, box.y + box.h / 2 - 4, "no map", colors::kDim);
    }
    // Name and length.
    {
        Color k = colors::kText;
        k.a = u8(255 * t);
        ui.fill_round(14, 214, 380, 40, 6, Color{0, 0, 0, u8(150 * t)});
        ui.text(24, 222, Ui::clip(c.name, 360), k);
        u32 m = length_m_ ? length_m_(c.label) : 0;
        if (m) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "Length %.3f km  (%.3f miles)", double(m) / 1000.0, double(m) / 1609.344);
            ui.text(24, 238, buf, colors::kDim);
        } else {
            ui.text(24, 238, c.label.rfind("r_", 0) == 0 ? "Reverse layout" : " ", colors::kDim);
        }
    }

    // The icon strip.
    constexpr float kStep = 59, kIconW = 55, kIconH = 42, kStripY = 266;
    strip_x_ += (strip_target(n, sel_, kStep, kIconW) - strip_x_) * 0.3f;
    ui.fill(0, kStripY - 5, Ui::kWidth, kIconH + 10, kStripBack);
    SDL_Rect clip{20, int(kStripY - 4), 600, int(kIconH + 8)};
    SDL_SetRenderClipRect(app.renderer(), &clip);
    for (int i = 0; i < n; ++i) {
        float x = strip_x_ + float(i) * kStep;
        if (x + kIconW < 20 || x > 620) continue;
        const PickerCourse& e = (*courses_)[size_t(i)];
        bool f = i == sel_;
        if (!art.course_icon(app, e.label, SDL_FRect{x, kStripY, kIconW, kIconH}, f ? 1.0f : 0.7f)) {
            ui.fill_round(x, kStripY, kIconW, kIconH, 3, Color{50, 54, 70});
            std::vector<std::string> lines = Ui::wrap(e.name, kIconW - 4);
            for (size_t l = 0; l < lines.size() && l < 3; ++l)
                ui.text_center_in(x, kIconW, kStripY + 4 + float(l) * 12, lines[l], f ? colors::kAccent : colors::kDim);
        }
        if (f) {
            ui.frame_round(x - 2, kStripY - 2, kIconW + 4, kIconH + 4, 3, 1, colors::kAccent);
            Color glow{255, 255, 255, u8(70 * (pulse(pulse_) - 0.5f) * 2)};
            ui.fill(x, kStripY, kIconW, kIconH, glow);
        }
    }
    SDL_SetRenderClipRect(app.renderer(), nullptr);
    if (sel_ > 0) ui.text(6, kStripY + 17, "<", colors::kAccent);
    if (sel_ + 1 < n) ui.text_right(Ui::kWidth - 6, kStripY + 17, ">", colors::kAccent);
    ui.footer("Left/Right: course (L1/R1: x10)   Cross: choose   Triangle: back to the lobby");
}

} // namespace gt3r
