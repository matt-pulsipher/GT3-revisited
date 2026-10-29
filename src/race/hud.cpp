#include "race/hud.h"

#include "base/log.h"
#include "formats/gs_memory.h"
#include "formats/menu/imgs.h"
#include "formats/tex1.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace gt3r::race {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kMargin = 106.667f; // 16:9 outward shift for alignments with bit 4
constexpr float kSx = 0.75f;          // 853.33 virtual units -> 640
constexpr float kSy = 360.0f / 448.0f;

// Game colours: the GS RGBA value is the drawn colour (the font texels act as 1.0;
// verified against captures of the original: amber text measures (200..208, 120..128, 16..24)).
constexpr SDL_Color kAmber{208, 128, 24, 255};   // 0x801880D0
constexpr SDL_Color kRed{240, 0, 0, 255};        // 0x800000F0
constexpr SDL_Color kGrey{200, 205, 215, 255};

// Target rectangle of the HUD in UI coordinates (the whole screen, or one
// half in split screen); set by Hud::draw.
SDL_FRect g_vp{0, 0, 640, 360};
// Per-block transform (touch-control safe area): the block is scaled by g_s about the
// pivot (UI units, before the shift) and shifted by (g_dx, g_dy). Identity normally.
float g_dx = 0, g_dy = 0, g_s = 1, g_px = 0, g_py = 0;
// 4:3 frame (original resolution option): the 640x448 layout as is, no 16:9 widening.
bool g_43 = false;
float margin() { return g_43 ? 0.0f : kMargin; }
float vsx() { return (g_43 ? 1.0f : kSx) * g_vp.w / 640.0f * g_s; }
float vsy() { return kSy * g_vp.h / 360.0f * g_s; }
float ux(float x) { return g_vp.x + (x + margin()) * vsx() + (1 - g_s) * (g_px - g_vp.x) + g_dx; }
float uy(float y) { return g_vp.y + y * vsy() + (1 - g_s) * (g_py - g_vp.y) + g_dy; }

// Game alignment: 0 left/top, 1 right/bottom, 2 centre; +4 = pushed outward in 16:9.
float anchor(float pos, float size, int align) {
    float margin = (align & 4) ? ::gt3r::race::margin() : 0.0f;
    switch (align & 3) {
    case 0: return pos - margin;
    case 1: return pos + margin - size;
    default: return pos - size / 2;
    }
}

SDL_Texture* make_texture(SDL_Renderer* r, u32 w, u32 h, const std::vector<u32>& rgba) {
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, int(w), int(h));
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, rgba.data(), int(w * 4));
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

void fill(SDL_Renderer* r, float x, float y, float w, float h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect rc{x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

SDL_FColor fcol(SDL_Color c, float a = 1.0f) { return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f * a}; }

float rdf(ByteSpan d, size_t o) {
    u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

// How far a block drawn as `parts` (UI units) must move sideways (dir -1 left, +1 right) for
// every part to keep kControlGap from every touch control in `controls`; 0 when none covers
// it. A control that only comes up under the block's bottom line (its top within kUnderlap of
// the block's bottom, about a line of the times) does not count: the block underlaps it. Each
// move takes a part past a control, so the loop ends.
constexpr float kControlGap = 4, kUnderlap = 24;
float clear_sideways(const std::vector<SDL_FRect>& parts, const std::vector<SDL_FRect>& controls, float dir) {
    float bottom = 0;
    for (const SDL_FRect& m : parts) bottom = std::max(bottom, m.y + m.h);
    float s = 0;
    for (bool moved = true; moved;) {
        moved = false;
        for (const SDL_FRect& c : controls) {
            if (c.y >= bottom - kUnderlap) continue;
            for (const SDL_FRect& m : parts) {
                const float x0 = m.x + dir * s - kControlGap, x1 = m.x + m.w + dir * s + kControlGap;
                if (!(x0 < c.x + c.w && c.x < x1 && m.y - kControlGap < c.y + c.h && c.y < m.y + m.h + kControlGap))
                    continue;
                s = dir < 0 ? m.x + m.w + kControlGap - c.x : c.x + c.w + kControlGap - m.x;
                moved = true;
            }
        }
    }
    return s;
}

} // namespace

Hud::Hud() = default;

Hud::~Hud() {
    for (auto& [k, im] : images_)
        if (im.tex) SDL_DestroyTexture(im.tex);
    for (Image& im : tire_body_)
        if (im.tex) SDL_DestroyTexture(im.tex);
    if (tire_outline_.tex) SDL_DestroyTexture(tire_outline_.tex);
    if (map_.tex) SDL_DestroyTexture(map_.tex);
}

std::string Hud::format_time(double s) {
    // GT3's formatter: dashes for negative times (and the 359,999,999 ms "no time" value),
    // otherwise clamped to 999'59.999.
    if (s < 0) return "-'--.---";
    long long ms = std::llround(s * 1000.0);
    if (ms == 359999999) return "-'--.---";
    ms = std::min<long long>(ms, 59999999);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld'%02lld.%03lld", ms / 60000, (ms / 1000) % 60, ms % 1000);
    return buf;
}

std::string Hud::speed_text(float kmh, bool imperial) {
    const float v = imperial ? kmh * 0.62139f : kmh;
    if (!(v > 0)) return "---.-";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f", double(std::min(v, 999.9f)));
    return buf;
}

// Tyre gauge colour of a wear level (-63..127): blue (scrubbing in) to green (fresh),
// yellow, orange and red (worn); black at -64 and below.
SDL_Color Hud::tire_color(int w) {
    if (w <= -64) return {0, 0, 0, 255};
    if (w < 0) return {0, u8(252 - 4 * -w), u8(4 * -w), 255};
    if (w < 32) return {u8(8 * w), 252, 0, 255};
    if (w < 64) return {252, u8(4 * (95 - w)), 0, 255};
    return {252, u8(2 * (127 - std::min(w, 127))), 0, 255};
}

int Hud::tach_scale(float rev_limit) { return int(std::floor((rev_limit + 1500.0f) / 1000.0f)) * 1000; }

Hud::SpeedScale Hud::speed_scale(float top_speed_kmh, bool imperial) {
    SpeedScale s;
    s.n = std::clamp(int(top_speed_kmh / 3.6f * 1.1f / 22.2222f) + 1, 2, 4);
    s.full = float(s.n * (imperial ? 50 : 80));
    static const int kKmh[3] = {180, 240, 320}, kMph[3] = {120, 160, 200};
    static const char* const kKmhFace[3] = {"speed_180km", "speed_240km", "speed_320km"};
    static const char* const kMphFace[3] = {"speed_120mp", "speed_160mp", "speed_200mp"};
    int f = 2;
    for (int i = 0; i < 3; ++i)
        if (float(imperial ? kMph[i] : kKmh[i]) >= s.full) {
            f = i;
            break;
        }
    s.face = imperial ? kMphFace[f] : kKmhFace[f];
    s.step = imperial ? 5.0f : 10.0f;
    return s;
}

const HudRow* Hud::event_row(HudMode m, u8 items, int view) {
    return (items & kItemTimes) ? hud_row(m, HudPart::Events, -1, view) : nullptr;
}

void Hud::load(const Vfs& vfs, SDL_Renderer* r, const std::string& language) {
    fonts_ok_ = fonts_.load(vfs, r);
    for (const std::string& lang : {language, std::string("US"), std::string("UK"), std::string("JP")}) {
        std::optional<Bytes> data = vfs.read("race/" + lang + "/display.imgs");
        if (!data || !menu::Imgs::is_imgs(*data)) continue;
        try {
            menu::Imgs imgs = menu::Imgs::parse(*data);
            for (size_t i = 0; i < imgs.entries.size(); ++i) {
                Tex1::Image img = imgs.decode(*data, i);
                if (imgs.entries[i].name == "tire_gage") {
                    // Alpha codes (x2 after decoding): 10/20/30/40 = tyre bodies 1..4, 255 = outlines.
                    for (int k = 0; k < 5; ++k) {
                        std::vector<u32> px(img.rgba.size(), 0);
                        for (size_t j = 0; j < px.size(); ++j) {
                            u32 a = img.rgba[j] >> 24;
                            bool on = k < 4 ? a == u32(10 * (k + 1)) : a >= 90;
                            if (on) px[j] = 0xFFFFFFFFu;
                        }
                        Image& dst = k < 4 ? tire_body_[size_t(k)] : tire_outline_;
                        dst = {make_texture(r, img.width, img.height, px), float(img.width), float(img.height)};
                    }
                }
                if (SDL_Texture* t = make_texture(r, img.width, img.height, img.rgba))
                    images_[imgs.entries[i].name] = {t, float(img.width), float(img.height)};
            }
        } catch (const std::exception& e) {
            LOG_WARN("HUD: %s", e.what());
        }
        break;
    }
    LOG_INFO("HUD: %zu display images, fonts %s", images_.size(), fonts_ok_ ? "loaded" : "missing");
}

void Hud::set_course_map(SDL_Renderer* r, ByteSpan g) {
    if (map_.tex) SDL_DestroyTexture(map_.tex);
    map_ = {};
    if (g.size() < 0x30 || !has_magic(g, "GTCM")) return;
    try {
        map_ox_ = rdf(g, 0x18);
        map_oz_ = rdf(g, 0x1C);
        map_scale_ = rdf(g, 0x20);
        map_rot_ = rdf(g, 0x24) * kPi / 180.0f;
        ByteSpan t = g.subspan(rd32(g, 0x28));
        Tex1 tex = Tex1::parse(t);
        GsMemory gs;
        tex.upload(t, gs);
        Tex1::Image img = tex.decode(gs, 0);
        // The map art uses PS2 alpha (0x80 = opaque); decode() already maps it to 0..255.
        map_.tex = make_texture(r, img.width, img.height, img.rgba);
        map_.w = float(img.width);
        map_.h = float(img.height);
    } catch (const std::exception& e) {
        LOG_WARN("HUD: course map: %s", e.what());
    }
}

const Hud::Image* Hud::image(const std::string& name) const {
    auto it = images_.find(name);
    return it == images_.end() || !it->second.tex ? nullptr : &it->second;
}

void Hud::text(SDL_Renderer* r, float x, float y, const std::string& s, const char* font, float scale, SDL_Color c,
               text::HAlign a, bool baseline, float spacing) {
    if (!fonts_ok_) {
        float w = float(s.size()) * 8;
        const float x0 = a == text::HAlign::Right ? x - w : a == text::HAlign::Center ? x - w / 2 : x;
        if (measuring_) {
            blit(r, nullptr, {x0, y, w, 8});
            return;
        }
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderDebugText(r, x0, y, s.c_str());
        return;
    }
    text::TextStyle st;
    text::GameFont* f = fonts_.resolve(font, st);
    if (!f) return;
    st.scale = scale;
    st.scale_x *= vsx();
    st.scale_y *= vsy();
    st.color = c;
    st.align = a;
    st.spacing = spacing;
    if (baseline) st.valign = text::VAlign::Baseline;
    if (measuring_) {
        const text::TextLayout lay = f->layout(x, y, s, st);
        blit(r, nullptr, {lay.x, lay.y, lay.width, lay.height});
        return;
    }
    f->draw(x, y, s, st);
}

void Hud::blit(SDL_Renderer* r, SDL_Texture* t, const SDL_FRect& dst) {
    if (!measuring_) SDL_RenderTexture(r, t, nullptr, &dst);
    else if (dst.w > 0 && dst.h > 0) measured_.push_back(dst);
}

std::vector<SDL_FRect> Hud::measure(const std::function<void()>& block) {
    measuring_ = true, measured_.clear();
    block();
    measuring_ = false;
    return measured_;
}

float Hud::text_width(const std::string& s, const char* font, float scale, float spacing) {
    if (!fonts_ok_) return float(s.size()) * 8 * scale;
    text::TextStyle st;
    text::GameFont* f = fonts_.resolve(font, st);
    if (!f) return 0;
    st.scale = scale;
    st.spacing = spacing;
    return f->measure(s, st).x;
}

void Hud::glyph_run(SDL_Renderer* r, float x, float y, const std::vector<std::pair<std::string, SDL_Color>>& run,
                    const char* font) {
    for (const auto& [g, c] : run) {
        text(r, ux(x), uy(y), g, font, 1, c, text::HAlign::Left, true);
        x += text_width(g, font);
    }
}

void Hud::polygon_strip(SDL_Renderer* r, const std::vector<SDL_FPoint>& pts, SDL_Color c) {
    std::vector<SDL_Vertex> v;
    SDL_FColor fc = fcol(c);
    for (size_t i = 0; i + 2 < pts.size(); ++i)
        for (size_t k = 0; k < 3; ++k) {
            SDL_Vertex vx{};
            vx.position = {ux(pts[i + k].x), uy(pts[i + k].y)};
            vx.color = fc;
            v.push_back(vx);
        }
    SDL_RenderGeometry(r, nullptr, v.data(), int(v.size()), nullptr, 0);
}

// Round meter: face stretched to the box, segment band, tapered needle (section "HUD" in RACE.md).
void Hud::draw_meter(SDL_Renderer* r, const Meter& m, float value) {
    float kx = m.face ? m.w / m.face->w : 1, ky = m.face ? m.h / m.face->h : 1;
    if (m.face) {
        SDL_FRect dst{ux(m.x), uy(m.y), m.w * vsx(), m.h * vsy()};
        SDL_RenderTexture(r, m.face->tex, nullptr, &dst);
    }
    float px = m.x + m.w / 2 + m.ox * kx, py = m.y + m.h / 2 + m.oy * ky;
    float rx = m.r * kx, ry = m.r * ky;
    auto at = [&](float deg, float s) {
        float a = deg * kPi / 180.0f;
        return SDL_FPoint{ux(px + std::cos(a) * s * rx), uy(py + std::sin(a) * s * ry)};
    };
    auto angle = [&](float v) { return m.a0 + 90.0f + std::clamp(v, 0.0f, m.scale + 8.0f) / m.scale * (m.a1 - m.a0); };
    // Band: thin segments just inside the dial edge, red from the shift point.
    if (m.step > 0) {
        int n = int(m.scale / m.step);
        int red = m.red_from > 0 ? int(m.red_from / m.step) : n + 1;
        float slot = (m.a1 - m.a0) / float(std::max(n, 1));
        for (int i = 0; i < n; ++i) {
            float s0 = m.a0 + 90.0f + slot * (float(i) + 0.075f), s1 = s0 + slot * 0.85f;
            SDL_FColor c = i >= red ? fcol(kRed) : SDL_FColor{0.81f, 1.0f, 1.0f, 0.9f};
            constexpr int kSteps = 4;
            SDL_Vertex v[kSteps * 6];
            int k = 0;
            for (int j = 0; j < kSteps; ++j) {
                float da = s0 + (s1 - s0) * float(j) / kSteps, db = s0 + (s1 - s0) * float(j + 1) / kSteps;
                SDL_FPoint p0 = at(da, 0.95f), p1 = at(da, 0.98f), p2 = at(db, 0.95f), p3 = at(db, 0.98f);
                for (SDL_FPoint p : {p0, p1, p2, p1, p3, p2}) v[k].position = p, v[k].color = c, ++k;
            }
            SDL_RenderGeometry(r, nullptr, v, k, nullptr, 0);
        }
    }
    // Needle: tapered quad from n0 to n1 of the radius (base 0.04, tip 0.02 of its length).
    float a = angle(value) * kPi / 180.0f;
    float len = m.n1 - m.n0;
    auto np = [&](float along, float across) {
        float lx = (m.n0 + along * len), ly = across * len;
        float wx = lx * std::cos(a) - ly * std::sin(a), wy = lx * std::sin(a) + ly * std::cos(a);
        return SDL_FPoint{ux(px + wx * rx), uy(py + wy * ry)};
    };
    SDL_Vertex v[6];
    SDL_FPoint q[4] = {np(0, 0.02f), np(1, 0.01f), np(0, -0.02f), np(1, -0.01f)};
    int idx[6] = {0, 1, 2, 1, 3, 2};
    for (int i = 0; i < 6; ++i) v[i].position = q[idx[i]], v[i].color = {1.0f, 0.0f, 0.0f, 1.0f};
    SDL_RenderGeometry(r, nullptr, v, 6, nullptr, 0);
}

void Hud::draw_replay_mark(SDL_Renderer* r, float alpha, const SDL_FRect* viewport, bool four_three) {
    g_vp = viewport ? *viewport : SDL_FRect{0, 0, 640, 360};
    g_43 = four_three;
    g_dx = g_dy = 0, g_s = 1;
    if (alpha <= 0) return;
    if (const Image* im = image("gt_logo_mini")) {
        // Right edge 620 (pushed out in 16:9), bottom 420 of the 640x448 frame.
        SDL_FRect rc{ux(anchor(620, im->w, 5)), uy(420 - im->h), im->w * vsx(), im->h * vsy()};
        SDL_SetTextureAlphaModFloat(im->tex, std::min(alpha, 1.0f));
        SDL_RenderTexture(r, im->tex, nullptr, &rc);
        SDL_SetTextureAlphaModFloat(im->tex, 1.0f);
    }
}

void Hud::draw_title(SDL_Renderer*, const HudData& d, const SDL_FRect* viewport) {
    g_vp = viewport ? *viewport : SDL_FRect{0, 0, 640, 360};
    g_43 = d.four_three;
    g_dx = g_dy = 0, g_s = 1;
    // The title (presentation §B3): the course name in capitals, sw_20 at 160 % in light grey,
    // squeezed to the row's width; FINISH at the same place.
    // The name is game text (the course table's bytes): "Côte d'Azur" holds a Latin escape and a
    // JIS quote, both drawn by the game font as GT3 draws them.
    const HudRow* title = hud_row(d.mode, HudPart::Title, 0, d.onboard ? 0 : 1);
    if (!title || d.intro_title.empty() || d.title_alpha <= 0 || !fonts_ok_) return;
    text::TextStyle st;
    text::GameFont* f = fonts_.resolve("sw_20", st);
    if (!f) return;
    const ByteSpan name(reinterpret_cast<const u8*>(d.intro_title.data()), d.intro_title.size());
    st.scale = 1.6f;
    const float w = f->measure_game(name, st).x;
    const float sq = title->w > 0 && w > title->w ? title->w / w : 1.0f;
    st.scale = 1.0f;
    st.scale_x = 1.6f * sq * vsx();
    st.scale_y = 1.6f * vsy();
    st.color = SDL_Color{208, 208, 208, Uint8(std::lround(255.0f * std::min(d.title_alpha, 1.0f)))};
    st.align = text::HAlign::Center;
    st.valign = text::VAlign::Baseline;
    f->draw_game(ux(title->x), uy(title->y), name, st);
}

void Hud::draw_pause(SDL_Renderer* r, int selected, const SDL_FRect* viewport, bool four_three) {
    g_vp = viewport ? *viewport : SDL_FRect{0, 0, 640, 360};
    g_43 = four_three;
    g_dx = g_dy = 0, g_s = 1;
    // Measured on the captures: two buttons 204..318 and 324..436 x 199..224 (frame units),
    // the selected one light orange (253, 168, 112), the other orange (253, 101, 0), dark labels.
    static const char* const kLabel[2] = {"CONTINUE", "EXIT"};
    const float x0[2] = {204, 324}, x1[2] = {318, 436};
    for (int i = 0; i < 2; ++i) {
        SDL_Color c = i == selected ? SDL_Color{253, 168, 112, 255} : SDL_Color{253, 101, 0, 255};
        fill(r, ux(x0[i]), uy(199), (x1[i] - x0[i]) * vsx(), 25 * vsy(), c);
        text(r, ux((x0[i] + x1[i]) / 2), uy(220), kLabel[i], "sw_16#1", 1, SDL_Color{40, 24, 10, 255},
             text::HAlign::Center, true);
    }
}

void Hud::draw_replay_pause(SDL_Renderer* r, int selected, const SDL_FRect* viewport, bool four_three) {
    g_vp = viewport ? *viewport : SDL_FRect{0, 0, 640, 360};
    g_43 = four_three;
    g_dx = g_dy = 0, g_s = 1;
    // Measured on the captures: the frame dimmed, a dark strip 96..536 x 183..240, buttons
    // 124..236 / 244..356 / 364..508 x 192..216 (selected light orange).
    fill(r, ux(-200), uy(0), 2000 * vsx(), 448 * vsy(), SDL_Color{0, 0, 0, 110});
    fill(r, ux(96), uy(183), 440 * vsx(), 57 * vsy(), SDL_Color{0, 0, 0, 90});
    static const char* const kLabel[3] = {"CONTINUE", "EXIT", "REPLAY OPTIONS"};
    const float x0[3] = {124, 244, 364}, x1[3] = {236, 356, 508};
    for (int i = 0; i < 3; ++i) {
        SDL_Color c = i == selected ? SDL_Color{253, 168, 112, 255} : SDL_Color{253, 101, 0, 255};
        fill(r, ux(x0[i]), uy(192), (x1[i] - x0[i]) * vsx(), 24 * vsy(), c);
        text(r, ux((x0[i] + x1[i]) / 2), uy(212), kLabel[i], "sw_16#1", 1, SDL_Color{40, 24, 10, 255},
             text::HAlign::Center, true);
    }
}

// The in-car (view 0) or outside (view 1) meter cluster (presentation §D1-§D3).
void Hud::draw_meters(SDL_Renderer* r, const HudData& d) {
    char name[16];
    char buf[64];
    int scale = tach_scale(d.rev_limit_rpm);
    static const int kFaces[] = {6, 7, 8, 9, 10, 11, 12, 18};
    int face_k = 18;
    for (int k : kFaces)
        if (k * 1000 >= scale) {
            face_k = k;
            break;
        }
    float step = face_k == 18 ? 1000.0f : 500.0f;
    float speed = d.imperial ? d.speed_kmh / 1.609344f : d.speed_kmh;
    const char* unit = d.imperial ? "mph" : "km/h";
    if (d.onboard) {
        // View 0: tachometer and speedometer side by side at the bottom centre.
        std::snprintf(name, sizeof(name), "rev_%02d", face_k);
        Meter tach{image(name), anchor(350, 170, 0), anchor(436, 110, 1), 170, 110, 0, 23, 75, 72.5f, 287.5f,
                   -0.2f, 0.95f, float(scale), d.shift_rpm, step};
        draw_meter(r, tach, d.rpm);
        // Speed scale (presentation §D1): N x 80 km/h or N x 50 mph on the first face that reaches it.
        const SpeedScale ss = speed_scale(d.top_speed_kmh, d.imperial);
        Meter spd{image(ss.face), anchor(290, 170, 1), anchor(436, 110, 1), 170, 110, 0, 23, 75, 72.5f, 287.5f,
                  -0.1f, 0.95f, ss.full, 0, ss.step};
        draw_meter(r, spd, speed);
        text(r, ux(tach.x + 85 - 10), uy(tach.y + 55 - 15), d.automatic ? ">" : "=", "meter#1", 1, kAmber,
             text::HAlign::Left);
        std::snprintf(buf, sizeof(buf), "%3d", int(speed));
        text(r, ux(350), uy(410), buf, "speed", 1, kAmber, text::HAlign::Right, true);
        text(r, ux(350 - 44), uy(430), unit, "sw_12", 1, kAmber, text::HAlign::Left, true);
    } else {
        // View 1: remote tachometer at the bottom right, digital speed and gear inside it.
        std::snprintf(name, sizeof(name), "revb_%02d", face_k);
        Meter tach{image(name), anchor(620, 160, 5), anchor(320, 140, 2), 160, 140, 3, 4, 73, 29.85f, 238.34f,
                   0.38f, 0.9f, float(scale), d.shift_rpm, step};
        draw_meter(r, tach, d.rpm);
        text(r, ux(tach.x + 80 - 2), uy(tach.y + 70 + 28), d.automatic ? ">" : "=", "meter#1", 1, kAmber,
             text::HAlign::Center);
        std::snprintf(buf, sizeof(buf), "%3d", int(speed));
        float sx = anchor(580, 0, 5);
        text(r, ux(sx), uy(385), buf, "speed", 1, kAmber, text::HAlign::Right, true);
        text(r, ux(sx + 5), uy(385), unit, "sw_12", 1, kAmber, text::HAlign::Left, true);
    }
    // Boost meter (turbo cars): face "boost" 64x64, needle 0..2.0 over -45..200.32 degrees.
    if (d.has_turbo) {
        bool on = d.onboard;
        Meter b{image("boost"), anchor(on ? 530.0f : 475.0f, 72, on ? 0 : 5), anchor(436, 64, 1), 72, 64, 0, 0, 32,
                -45.0f, 200.32f, -0.1f, 0.95f, 2.0f, 0, 0};
        // The needle value is a pressure ratio: 1.0 sits at the face's "0 kg/cm2" mark (atmospheric),
        // lower values on the vacuum (mmHg) side; physics' boost is that ratio minus 1.
        draw_meter(r, b, std::clamp(1.0f + d.boost, 0.0f, 2.0f));
    }
    // Back panel (flat polygon, black 50 %), indicator lamps and odometers.
    {
        const SDL_Color panel{0, 0, 0, 128};
        if (d.onboard) {
            // Gear panel between the dials: top corners cut 3, straight to 30 %, tapering to a
            // 15 % inset at the bottom.
            float w = 78, h = 31, X = anchor(320, w, 2), Y = anchor(360, h, 2), R = X + w, B = Y + h;
            polygon_strip(r, {{X + 3, Y}, {R - 3, Y}, {X, Y + 3}, {R, Y + 3}, {X, Y + 0.3f * h}, {R, Y + 0.3f * h},
                              {X + 0.15f * w, B}, {R - 0.15f * w, B}},
                          panel);
        } else {
            draw_panel(r, anchor(620, 136, 5), anchor(430, 30, 1), 136, 30);
        }
        // Lamps: odo-font glyphs '!' ASM, '"' TCS, '#' hand brake, '$' oil, each followed by a space.
        lamp_counter_ = (lamp_counter_ + 21) & 0xFF;
        bool blink_on = s8(u8(lamp_counter_)) > 0;
        const SDL_Color unlit{0, 0, 0, 128};
        auto lamp = [&](int state, SDL_Color on) { return state == 1 || (state == 2 && blink_on) ? on : unlit; };
        std::vector<std::pair<std::string, SDL_Color>> run = {
            {"!", lamp(d.asm_lamp, SDL_Color{73, 204, 43, 255})},  {" ", unlit},
            {"\"", lamp(d.tcs_lamp, SDL_Color{255, 184, 20, 255})}, {" ", unlit},
            {"#", d.handbrake_lamp ? kRed : unlit},                 {" ", unlit},
            {"$", d.oil_lamp ? kRed : unlit}};
        float run_w = 0;
        for (const auto& g : run) run_w += text_width(g.first, "odo#1");
        float lx = d.onboard ? anchor(435, 0, 2) - run_w / 2 : anchor(620, 125, 5), ly = d.onboard ? 430.0f : 410.0f;
        glyph_run(r, lx, ly, run, "odo#1");
        // Odometers: total distance "%06d" (km / miles) and this race's trip "%04d" in tenths.
        const SDL_Color cyan{98, 192, 192, 255};
        double unit_m = d.imperial ? 1609.344 : 1000.0;
        std::snprintf(buf, sizeof(buf), "%06d", int(std::fmod(std::max(0.0, d.odometer_km * 1000.0 / unit_m), 1e6)));
        std::string odo = buf;
        std::snprintf(buf, sizeof(buf), "%04d", int(std::fmod(std::max(0.0, d.trip_m / unit_m * 10.0), 1e4)));
        std::string trip = buf;
        if (d.onboard) {
            text(r, ux(205), uy(428), odo, "odo#1", 1, cyan, text::HAlign::Center, true);
            text(r, ux(205), uy(370), trip, "odo#1", 1, cyan, text::HAlign::Center, true);
        } else {
            text(r, ux(anchor(605, 0, 5)), uy(424), odo, "odo#1", 1, cyan, text::HAlign::Center, true);
            text(r, ux(anchor(525, 0, 5)), uy(424), trip, "odo#1", 1, cyan, text::HAlign::Center, true);
        }
    }
    // Gear with the unlit "8" behind it, and the shift light.
    if (d.onboard) draw_gear(r, d, anchor(320, 73, 2), anchor(360, 31, 2), 73, 31, -35, 0);
    else draw_gear(r, d, anchor(620, 150, 5), anchor(325, 140, 2), 150, 140, -4, -12);
}

// Black 50 % octagon with 3-unit cut corners (the outside cluster's back panel).
void Hud::draw_panel(SDL_Renderer* r, float X, float Y, float w, float h) {
    const float R = X + w, B = Y + h;
    polygon_strip(r, {{X + 3, Y}, {R - 3, Y}, {X, Y + 3}, {R, Y + 3}, {X, B - 3}, {R, B - 3}, {X + 3, B}, {R - 3, B}},
                  SDL_Color{0, 0, 0, 128});
}

// The gear in its box (gx, gy, gw, gh; frame units, already aligned): the digit centred,
// pen at the top of its cell, the unlit "8" behind it, "> n" after it and the shift light at
// (dx, dy) from the cell's centre.
void Hud::draw_gear(SDL_Renderer* r, const HudData& d, float gx, float gy, float gw, float gh, float dx, float dy) {
    float cell_w = 26, cell_h = 26;
    if (fonts_ok_)
        if (text::GameFont* f = fonts_.get("gearr")) cell_w = f->data().line_height * 0.6f, cell_h = f->line_height();
    float cx = gx + gw / 2, cy = gy + gh / 2 + cell_h / 2;
    float penx = cx - cell_w, peny = cy - cell_h; // pen at the top of the cell
    text(r, ux(penx), uy(peny), "8", "gearr#1", 1, SDL_Color{0, 0, 0, 96}, text::HAlign::Left);
    std::string g = d.gear >= 1 ? std::to_string(std::min(d.gear, 9)) : (d.gear < 0 ? "R" : "N");
    SDL_Color gc = d.gear < 0 ? kRed : kAmber;
    if (!d.clutch_engaged) gc.a = 128; // clutch not engaged: half brightness
    if (g == "N") text(r, ux(penx), uy(peny), "N", "sw_20", 1.3f, gc, text::HAlign::Left);
    else text(r, ux(penx), uy(peny), g, "gearr#1", 1, gc, text::HAlign::Left);
    // "> n" after the digit: a ghost "> 8" always, the recommended gear in red over it
    // (blinking red / black in braking zones).
    text(r, ux(penx + cell_w + 2), uy(peny + cell_h * 0.45f), ">8", "gearr#1", 0.5f, SDL_Color{0, 0, 0, 96},
         text::HAlign::Left);
    if (d.hint_gear > 0) {
        bool red = !d.hint_blink || s8(u8(lamp_counter_)) > 0;
        text(r, ux(penx + cell_w + 2), uy(peny + cell_h * 0.45f), ">" + std::to_string(std::min(d.hint_gear, 9)),
             "gearr#1", 0.5f, red ? kRed : SDL_Color{0, 0, 0, 255}, text::HAlign::Left);
    }
    bool shift = d.rpm > d.shift_rpm;
    shift_counter_ = shift ? (shift_counter_ + 42) & 0xFF : 0;
    bool lit = shift && s8(u8(shift_counter_)) >= 1;
    text(r, ux(cx + dx), uy(cy + dy - cell_h), ":", "gearr#1", 1, lit ? kRed : SDL_Color{0, 0, 0, 200},
         text::HAlign::Left);
}

// The lap count and the time lines of the mode (presentation §D4), right-aligned at their
// rows' x (pushed outward in 16:9): values in sw_16 with sw_12 labels, the label's right edge
// max(value width, 90) + 10 left of the value's.
void Hud::draw_times(SDL_Renderer* r, const HudData& d, int view) {
    char buf[64];
    auto width = [&](const std::string& s, const char* font, float fallback) {
        if (!fonts_ok_) return fallback;
        text::GameFont* f = fonts_.get(font);
        return f ? f->measure(s).x : fallback;
    };
    // The lap count is always pushed right in 16:9, whatever its row's alignment.
    if (const HudRow* row = hud_row(d.mode, HudPart::LapCount, -1, view)) {
        const float R = row->x + margin(), y = row->y;
        const int lap = std::min(std::max(d.lap, 1), 9999);
        if (d.total_laps > 0) std::snprintf(buf, sizeof(buf), "%d/%d", std::min(lap, d.total_laps), d.total_laps);
        else std::snprintf(buf, sizeof(buf), "%d", lap);
        text(r, ux(R), uy(y), buf, "sw_20", 1, kAmber, text::HAlign::Right, true);
        text(r, ux(R - width(buf, "sw_20", 30) - 10), uy(y), "Lap", "sw_16", 1, kAmber, text::HAlign::Right, true);
    }
    auto line = [&](float R, float y, const char* label, double t) {
        std::string s = format_time(t);
        text(r, ux(R), uy(y), s, "sw_16", 1, kAmber, text::HAlign::Right, true);
        float tw = std::max(width(s, "sw_16", 90), 90.0f);
        text(r, ux(R - tw - 10), uy(y), label, "sw_12", 1, kAmber, text::HAlign::Right, true);
        return tw;
    };
    // A speed line: the speed in sw_16, the unit in sw_12 after it, the label left of both (at
    // least 90 wide, as the times).
    auto speed_line = [&](float R, float y, const char* label, float kmh) {
        const char* unit = d.imperial ? "mph" : "km/h";
        std::string s = speed_text(kmh, d.imperial);
        float uw = width(unit, "sw_12", 30), vw = width(s, "sw_16", 60);
        text(r, ux(R), uy(y), unit, "sw_12", 1, kAmber, text::HAlign::Right, true);
        text(r, ux(R - uw), uy(y), s, "sw_16", 1, kAmber, text::HAlign::Right, true);
        text(r, ux(R - std::max(vw + uw, 90.0f) - 10), uy(y), label, "sw_12", 1, kAmber, text::HAlign::Right, true);
    };
    // The Lap line goes once the car has finished, where the mode shows the list or Last.
    const bool lap_goes = d.finished && (hud_row(d.mode, HudPart::TimeLine, int(TimeLine::LapTimes), view) ||
                                         hud_row(d.mode, HudPart::TimeLine, int(TimeLine::Last), view));
    for (const HudRow& row : hud_layout(d.mode)) {
        if (row.view != kAnyView && row.view != view) continue;
        const float R = anchor(row.x, 0, row.align), y = row.y;
        if (row.part == HudPart::Target) {
            // Licence targets: gold / silver / bronze with their marks at 75 % left of the time.
            static const char* const kMarks[] = {"mark_1st", "mark_2nd", "mark_3rd"};
            const size_t i = std::min<size_t>(row.sub, 2);
            std::string t = format_time(d.license_targets[i]);
            text(r, ux(R), uy(y), t, "sw_16", 1, kAmber, text::HAlign::Right);
            float tw = std::max(width(t, "sw_16", 90), 90.0f);
            if (const Image* im = image(kMarks[i]))
                blit(r, im->tex,
                     {ux(R - tw - 8 - im->w * 0.75f), uy(y - 4), im->w * 0.75f * vsx(), im->h * 0.75f * vsy()});
            continue;
        }
        if (row.part != HudPart::TimeLine) continue;
        const TimeLine kind = TimeLine(row.sub);
        const char* label = time_line_label(kind);
        switch (kind) {
        case TimeLine::LapTimes: {
            // The lap-time list: the last `lines` recorded laps, oldest at the top, dashes for
            // lines not yet run; their lap numbers right-aligned max(widest, 90) + 10 left of
            // them; lines 0.8 x the sw_16 line height apart.
            const int lines = std::max(d.lap_list_lines, 1), kept = int(d.lap_times.size());
            const int first = std::max(kept - lines, 0);
            float stepy = 24;
            if (fonts_ok_)
                if (text::GameFont* f = fonts_.get("sw_16")) stepy = float(f->data().line_height) * 0.8f;
            float widest = 90;
            for (int i = 0; i < lines; ++i) {
                const int k = first + i;
                std::string s = format_time(k < kept ? d.lap_times[size_t(k)] : -1);
                text(r, ux(R), uy(y + stepy * float(i)), s, "sw_16", 1, kAmber, text::HAlign::Right, true);
                widest = std::max(widest, width(s, "sw_16", 90));
            }
            const int number = std::max(d.laps_recorded - lines + 1, 1);
            for (int i = 0; i < lines; ++i)
                text(r, ux(R - widest - 10), uy(y + stepy * float(i)), std::to_string(number + i), "sw_12", 1, kAmber,
                     text::HAlign::Right, true);
            break;
        }
        case TimeLine::Total:
        case TimeLine::TotalTime: line(R, y, label, d.total_time); break;
        case TimeLine::Best:
        case TimeLine::BestTime: line(R, y, label, d.best_lap); break;
        case TimeLine::Last: line(R, y, label, d.last_lap); break;
        case TimeLine::Lap:
            if (!lap_goes) line(R, y, label, d.lap_time);
            break;
        case TimeLine::Time: line(R, y, label, d.lap_time); break;
        case TimeLine::Record:
        case TimeLine::RecordLap:
        case TimeLine::Fastest: line(R, y, label, d.session_best); break;
        case TimeLine::PolePosition: {
            // P.P.: the session's fastest lap; the picture of the car holding it takes the
            // label's place, right-aligned at the row's offset left of the label's slot and
            // centred on the row's y offset ("P.P." without a picture).
            float tw = line(R, y, d.pole_icon ? "" : label, d.session_best);
            if (d.pole_icon) {
                const float right = R - tw - 10 + row.w, cy = y + row.h;
                pole_icon_rect_ = {ux(right - 64), uy(cy - 16), 64 * vsx(), 32 * vsy()};
                if (measuring_) blit(r, nullptr, pole_icon_rect_); // the race screen draws the picture there
            }
            break;
        }
        case TimeLine::RecordSpeed: speed_line(R, y, label, -1); break;
        case TimeLine::BestSpeed: speed_line(R, y, label, d.best_kmh); break;
        }
    }
}

// The mini-map (presentation §D5).
void Hud::draw_map(SDL_Renderer* r, const HudData& d, int view) {
    const HudRow* map_row = hud_row(d.mode, HudPart::Map, -1, view);
    if (map_row && (d.items & kItemMap) && map_.tex) {
        const float k = map_row->w / 100.0f; // the row's size: the map's scale
        float mx = anchor(map_row->x, map_.w * k, map_row->align), my = anchor(map_row->y, map_.h * k, map_row->valign);
        blit(r, map_.tex, {ux(mx), uy(my), map_.w * k * vsx(), map_.h * k * vsy()});
        auto dot = [&](float wx, float wz, const Image* im) {
            float dx = wx - map_ox_, dz = wz - map_oz_;
            float px = map_scale_ * (std::cos(map_rot_) * dx - std::sin(map_rot_) * dz);
            float pz = map_scale_ * (std::sin(map_rot_) * dx + std::cos(map_rot_) * dz);
            float cx = mx + (map_.w / 2 + px) * k, cy = my + (map_.h / 2 + pz) * k;
            if (im) {
                SDL_FRect rc{ux(cx - 3.5f * k), uy(cy - 3.5f * k), 7 * k * vsx(), 7 * k * vsy()};
                SDL_RenderTexture(r, im->tex, nullptr, &rc);
            } else {
                fill(r, ux(cx) - 2, uy(cy) - 2, 4, 4, kAmber);
            }
        };
        if (!measuring_) { // the dots lie on the map
            for (const auto& [ox, oz] : d.others) dot(ox, oz, image("ecar_dot"));
            dot(d.player_x, d.player_z, image("1pcar_dot"));
        }
    }
}

// The position (presentation §D5).
void Hud::draw_rank(SDL_Renderer* r, const HudData& d, int view) {
    const HudRow* rank_row = hud_row(d.mode, HudPart::Rank, -1, view);
    if (rank_row && d.live && d.cars > 1 && d.position >= 1 && d.position <= 12) {
        // "1st".."12th" in font rank (the suffix is part of the string), 3 px glyph spacing, at
        // the row's position and scale (size / 100); shadow at +4 in black 75 % first. The
        // original draws it with no 16:9 push (presumably an overscan margin); ours pushes it
        // out to the left edge whatever the row's alignment, like the lap count on the right.
        static const char* const kRank[] = {"1st", "2nd", "3rd", "4th", "5th", "6th",
                                            "7th", "8th", "9th", "10th", "11th", "12th"};
        const char* s = kRank[d.position - 1];
        const float k = rank_row->w / 100.0f, x = rank_row->x - margin(), y = rank_row->y;
        text(r, ux(x + 4), uy(y + 4), s, "rank#1", k, SDL_Color{0, 0, 0, 192}, text::HAlign::Left, true, 3);
        text(r, ux(x), uy(y), s, "rank#1", k, kAmber, text::HAlign::Left, true, 3);
    }
}

void Hud::draw(SDL_Renderer* r, const HudData& d, const SDL_FRect* viewport) {
    g_vp = viewport ? *viewport : SDL_FRect{0, 0, 640, 360};
    g_43 = d.four_three;
    ++blink_;
    pole_icon_rect_ = {0, 0, 0, 0};
    // Touch controls: the meters move inside the uncovered area, the top blocks only where a
    // control would cover them (full screen, no controls = no shift).
    const SDL_FRect& sa = d.safe_area;
    float left = std::max(0.0f, sa.x), right = std::min(0.0f, sa.x + sa.w - 640.0f),
          bottom = std::min(0.0f, sa.y + sa.h - 360.0f), top = std::max(0.0f, sa.y);
    // Meter cluster: bottom right of the safe area, smaller when the controls leave a
    // narrow area (16:9 / 16:10 with the right-hand cluster on screen).
    g_dx = right, g_dy = bottom;
    g_s = std::clamp(sa.w / 560.0f, 0.7f, 1.0f), g_px = 640, g_py = 360;
    // d.layer: 0 everything, 1 all but the lap / time block, 2 only that block (the results
    // panel goes between them: the original draws the times over it, the rest under it).
    const bool body = d.layer != 2, times = d.layer != 1 && (d.items & kItemTimes);
    // The mode's rows for this camera view (0 in-car, 1 outside; presentation §D4).
    const int view = d.onboard ? 0 : 1;
    auto row = [&](HudPart p, int sub = -1) { return hud_row(d.mode, p, sub, view); };
    char name[16];
    char buf[64];
    if (body && (d.items & kItemMeters)) {
        if (row(HudPart::Meters, view)) {
            draw_meters(r, d);
        } else if (const HudRow* panel = row(HudPart::Meters, 64)) {
            // The replay's outside view: the speed and the gear on a small back panel (§D9).
            draw_panel(r, anchor(panel->x, panel->w, panel->align), anchor(panel->y, panel->h, panel->valign), panel->w,
                       panel->h);
            if (const HudRow* s = row(HudPart::DigitalSpeed)) {
                const float sx = anchor(s->x, 0, s->align);
                std::snprintf(buf, sizeof(buf), "%3d", int(d.imperial ? d.speed_kmh / 1.609344f : d.speed_kmh));
                text(r, ux(sx), uy(s->y), buf, "speed", 1, kAmber, text::HAlign::Right, true);
                text(r, ux(sx + s->w), uy(float(s->y + s->h)), d.imperial ? "mph" : "km/h", "sw_12", 1, kAmber,
                     text::HAlign::Left, true);
            }
            if (const HudRow* g = row(HudPart::Gear))
                draw_gear(r, d, anchor(g->x, g->w, g->align), anchor(g->y, g->h, g->valign), g->w, g->h,
                          (g->sub & 1) ? -4.0f : -35.0f, (g->sub & 1) ? -12.0f : 0.0f);
        }
    }
    if (times) {
        // --- laps and times (right) --------------------------------------------------------
        // In the corner. Where a touch control would cover it, moved left just clear of the
        // controls, or inside the safe area when that is further than the area's right edge.
        g_dx = 0, g_dy = 0, g_s = 1;
        if (!d.touch_controls.empty()) {
            const float s = clear_sideways(measure([&] { draw_times(r, d, view); }), d.touch_controls, -1);
            if (s <= -right) g_dx = -s;
            else g_dx = right, g_dy = top;
        }
        draw_times(r, d, view);
    }
    if (!body) return;
    // --- mini-map and position (left; presentation §D5) ----------------------------------------
    // Each in its corner; the same rule as the times, mirrored (moved right). The rank's
    // fallback takes the map along (it would come down onto it).
    float rank_dx = 0, rank_dy = 0, map_dx = 0, map_dy = 0;
    g_s = 1;
    if (!d.touch_controls.empty()) {
        g_dx = 0, g_dy = 0;
        const float rs = clear_sideways(measure([&] { draw_rank(r, d, view); }), d.touch_controls, 1);
        const float ms = clear_sideways(measure([&] { draw_map(r, d, view); }), d.touch_controls, 1);
        if (rs > left) {
            rank_dx = map_dx = left, rank_dy = map_dy = top;
        } else {
            rank_dx = rs;
            if (ms <= left) map_dx = ms;
            else map_dx = left, map_dy = top;
        }
    }
    g_dx = map_dx, g_dy = map_dy;
    draw_map(r, d, view);
    g_dx = rank_dx, g_dy = rank_dy;
    draw_rank(r, d, view);

    // --- start countdown, title, wrong way --------------------------------------------------
    g_dx = 0, g_dy = 0, g_s = 1;
    draw_title(r, d, viewport);
    const HudRow* title = row(HudPart::Title, 0);
    if (d.countdown >= 0) {
        if (d.countdown > 0) {
            std::snprintf(name, sizeof(name), "count%d", std::clamp(int(std::ceil(d.countdown)), 1, 3));
            if (const Image* im = image(name)) {
                SDL_FRect rc{ux(320 - im->w / 2), uy(170 - im->h / 2), im->w * vsx(), im->h * vsy()};
                SDL_RenderTexture(r, im->tex, nullptr, &rc);
            } else {
                text(r, 320, 120, std::to_string(int(std::ceil(d.countdown))), "speed", 2.5f, kAmber,
                     text::HAlign::Center);
            }
        } else if (const Image* im = image("start")) {
            SDL_FRect rc{ux(320 - im->w / 2), uy(170 - im->h / 2), im->w * vsx(), im->h * vsy()};
            SDL_RenderTexture(r, im->tex, nullptr, &rc);
        }
    }
    // Finish: "@finish" for 4 s at the title's place (2P: "PLAYER n WIN!" in sw_20 at 150 %,
    // winner only).
    if (d.finish_t >= 0 && d.finish_t < 4.0) {
        if (d.split) {
            if (d.winner_player > 0)
                text(r, ux(320), uy(120 - 15), "PLAYER " + std::to_string(d.winner_player) + " WIN!", "sw_20", 1.5f,
                     kAmber, text::HAlign::Center);
        } else if (const Image* im = image("finish"); im && title) {
            SDL_FRect rc{ux(anchor(title->x, im->w, title->align)), uy(anchor(title->y, im->h, title->valign)),
                         im->w * vsx(), im->h * vsy()};
            SDL_RenderTexture(r, im->tex, nullptr, &rc);
        }
    }
    // The message display (live only, not in every mode): WRONG WAY in red, else AUTO DRIVE,
    // centred on its row (2P half: (320, 200)).
    if (const HudRow* msg = row(HudPart::Messages); msg && d.live) {
        const float my = d.split ? 200.0f : float(msg->y);
        if (d.wrong_way) text(r, ux(msg->x), uy(my) - 6, "WRONG WAY", "sw_16", 1, kRed, text::HAlign::Center);
        else if (d.auto_drive) text(r, ux(msg->x), uy(my) - 6, "AUTO DRIVE", "sw_16", 1, kAmber, text::HAlign::Center);
    }
    // Event display (presentation §D6): centred on its row's anchor, the pen on the baseline,
    // at full alpha, a text wider than the row's width (when set) squeezed to it; the gap's
    // car picture (64 x 32, centred) is drawn by the race screen. With the time item only
    // (replays: REPLAY OPTIONS ITEM time, §D9).
    event_icon_rect_ = {0, 0, 0, 0};
    if (const HudRow* ev = event_row(d.mode, d.items, view)) {
        for (const EventDisplay::Message& m : d.events) {
            const char* font = m.large ? "sw_20" : "sw_16";
            const float w = text_width(m.text, font);
            if (ev->w > 0 && w > ev->w && fonts_ok_) {
                text::TextStyle st;
                if (text::GameFont* f = fonts_.resolve(font, st)) {
                    st.scale_x = float(ev->w) / w * vsx();
                    st.scale_y = vsy();
                    st.color = SDL_Color{m.r, m.g, m.b, 255};
                    st.align = text::HAlign::Center;
                    st.valign = text::VAlign::Baseline;
                    f->draw(ux(ev->x + m.dx), uy(ev->y + m.dy), m.text, st);
                }
            } else {
                text(r, ux(ev->x + m.dx), uy(ev->y + m.dy), m.text, font, 1, SDL_Color{m.r, m.g, m.b, 255},
                     text::HAlign::Center, true);
            }
        }
        if (d.event_icon)
            event_icon_rect_ = {ux(ev->x + d.event_icon_dx - 32), uy(ev->y + d.event_icon_dy - 16), 64 * vsx(),
                                32 * vsy()};
    }
    // Tyre gauge: its row's place (left edge pushed, bottom), each tyre body in its wear colour,
    // outlines dark grey.
    const HudRow* tyres = row(HudPart::Tires);
    if (tyres && (d.items & kItemTires) && d.show_tires && tire_outline_.tex) {
        g_dx = 0, g_dy = std::min(0.0f, d.safe_area.y + d.safe_area.h - 360.0f);
        float x = anchor(tyres->x, 0, tyres->align), y = tyres->y - tire_outline_.h;
        SDL_FRect rc{ux(x), uy(y), tire_outline_.w * vsx(), tire_outline_.h * vsy()};
        for (int i = 0; i < 4; ++i) {
            SDL_Color c = tire_color(d.tire_levels[size_t(i)]);
            SDL_SetTextureColorMod(tire_body_[size_t(i)].tex, c.r, c.g, c.b);
            SDL_RenderTexture(r, tire_body_[size_t(i)].tex, nullptr, &rc);
        }
        SDL_SetTextureColorMod(tire_outline_.tex, 32, 32, 32);
        SDL_RenderTexture(r, tire_outline_.tex, nullptr, &rc);
        g_dx = 0, g_dy = 0;
    }
    // Replay items (presentation §D9): the car's name, light grey sw_16 on its row (squeezed to
    // the row's width); the steering and accel / brake monitors.
    if (const HudRow* cn = row(HudPart::CarName); cn && (d.items & kItemCarName) && !d.car_name.empty()) {
        const float w = text_width(d.car_name, "sw_16"), sq = cn->w > 0 && w > cn->w ? cn->w / w : 1.0f;
        text::TextStyle st;
        if (fonts_ok_)
            if (text::GameFont* f = fonts_.resolve("sw_16", st)) {
                st.scale_x = sq * vsx();
                st.scale_y = vsy();
                st.color = SDL_Color{208, 208, 208, 255};
                st.valign = text::VAlign::Baseline;
                f->draw(ux(anchor(cn->x, w * sq, cn->align)), uy(cn->y), d.car_name, st);
            }
    }
    if (const HudRow* sw = row(HudPart::Steering); sw && (d.items & kItemSteering)) {
        // A light grey tick from 24 to 43 units above the pivot, and the "steering" mark on that
        // circle (radius 43), turned with the steering, up to 90 degrees either side.
        const float px = anchor(sw->x, 0, sw->align), py = sw->y;
        polygon_strip(r, {{px - 0.5f, py - 43}, {px + 0.5f, py - 43}, {px - 0.5f, py - 24}, {px + 0.5f, py - 24}},
                      SDL_Color{208, 208, 208, 255});
        if (const Image* im = image("steering")) {
            const float a = 90.0f * std::clamp(d.steer, -1.0f, 1.0f), rad = a * kPi / 180.0f;
            const float cx = px + 43 * std::sin(rad), cy = py - 43 * std::cos(rad);
            SDL_FRect rc{ux(cx - im->w / 2), uy(cy - im->h / 2), im->w * vsx(), im->h * vsy()};
            SDL_RenderTextureRotated(r, im->tex, nullptr, &rc, a, nullptr, SDL_FLIP_NONE);
        }
    }
    if (const HudRow* ab = row(HudPart::AccelBrake); ab && (d.items & kItemAccelBrake)) {
        // Brake (red) at the row's x, accel (green) at x + its width: dark grey gauges filled
        // from the bottom (the row's y), the gauge art on top.
        auto gauge = [&](float x0, const char* art, float v, SDL_Color c) {
            const Image* im = image(art);
            const float w = im ? im->w : 8, h = im ? im->h : 64, x = anchor(x0, 0, ab->align), y = ab->y - h;
            fill(r, ux(x), uy(y), w * vsx(), h * vsy(), SDL_Color{32, 32, 32, 255});
            const float fh = std::floor(h * std::clamp(v, 0.0f, 1.0f));
            fill(r, ux(x), uy(ab->y - fh), w * vsx(), fh * vsy(), c);
            if (im) {
                SDL_FRect rc{ux(x), uy(y), w * vsx(), h * vsy()};
                SDL_RenderTexture(r, im->tex, nullptr, &rc);
            }
        };
        gauge(ab->x, "brake_gage", d.brake, kRed);
        gauge(float(ab->x + ab->w), "accel_gage", d.throttle, SDL_Color{73, 204, 43, 255});
    }
    (void)kGrey;
}

// 2P layouts (GT3 racedetail rows): racemode 7 = 4:3 horizontal split, 640x224 per
// half; racemode 8 = 16:9 vertical split, 426.667x448 per half.
struct SplitLayout {
    float w, h;
    float rank_x, rank_y;
    float lap_r, lap_y;
    float lines_r, line_y[3];
    float map_x, map_y;
    float speed_r, speed_y;
    float tach_r, tach_y, tach_w, tach_h;
    float gear_x, gear_y;
    float big_x, big_y, warn_y;
};
// 4:3 top / bottom halves, measured on captures of the original's 2P battle: rank (ordinal)
// top left, map below it, laps and times top right, speed then a dark panel (gear, AT/MT, bar
// tachometer, shift light) at the bottom right. The rank is its row's pen (presentation §D8).
constexpr SplitLayout kSplit43{640, 224, 24, 48, 615, 36, 615, {64, 88, 112}, 16, 52,
                               438, 208, 596, 206, 48, 18, 524, 210, 320, 120, 200};
// 16:9 halves: the rows of the side-by-side layout; the lap count is pushed right by the 16:9
// margin like every lap count (row x 310).
constexpr SplitLayout kSplit169{426.667f, 448, 16, 60, 310 + kMargin, 60, 413, {96, 120, 144}, 10, 105,
                                220, 435, 413, 435, 65, 25, 313, 422, 213, 210, 384};

void Hud::draw_split(SDL_Renderer* r, const HudData& d, const SDL_FRect& half, bool horizontal) {
    g_43 = false;
    g_dx = g_dy = 0, g_s = 1;
    pole_icon_rect_ = {0, 0, 0, 0};
    const SplitLayout& L = horizontal ? kSplit43 : kSplit169;
    const float kw = half.w / L.w, kh = half.h / L.h;
    auto X = [&](float x) { return half.x + x * kw; };
    auto Y = [&](float y) { return half.y + y * kh; };
    auto txt = [&](float x, float y, const std::string& s, const char* font, float sc, SDL_Color c, text::HAlign a) {
        if (!fonts_ok_) return;
        text::TextStyle st;
        text::GameFont* f = fonts_.resolve(font, st);
        if (!f) return;
        // The 224-line halves would draw the fonts at double height; they are drawn at 70 %.
        st.scale = sc * (horizontal ? 0.7f : 1.0f);
        st.scale_x *= kw;
        st.scale_y *= kh;
        st.color = c;
        st.align = a;
        f->draw(X(x), Y(y), s, st);
    };
    char buf[64];
    // Mini-map at 70 %, north-up, with both players' dots.
    if (map_.tex) {
        constexpr float k = 0.7f;
        float mx = L.map_x, my = L.map_y;
        SDL_FRect dst{X(mx), Y(my), map_.w * k * kw, map_.h * k * kh};
        SDL_RenderTexture(r, map_.tex, nullptr, &dst);
        auto dot = [&](float wx, float wz, const Image* im) {
            float dx = wx - map_ox_, dz = wz - map_oz_;
            float px = map_scale_ * (std::cos(map_rot_) * dx - std::sin(map_rot_) * dz);
            float pz = map_scale_ * (std::sin(map_rot_) * dx + std::cos(map_rot_) * dz);
            float cx = mx + (map_.w / 2 + px) * k, cy = my + (map_.h / 2 + pz) * k;
            if (!im) return;
            SDL_FRect rc{X(cx - 3.5f * k), Y(cy - 3.5f * k), 7 * k * kw, 7 * k * kh};
            SDL_RenderTexture(r, im->tex, nullptr, &rc);
        };
        for (const auto& [ox, oz] : d.others) dot(ox, oz, image("ecar_dot"));
        dot(d.player_x, d.player_z, image("1pcar_dot"));
    }
    // Position: "1st".."12th" at 80 % (the rows' size), the pen on the baseline, shadow first.
    if (d.cars > 1 && d.position >= 1 && d.position <= 12) {
        static const char* const kRank[] = {"1st", "2nd", "3rd", "4th", "5th", "6th",
                                            "7th", "8th", "9th", "10th", "11th", "12th"};
        if (fonts_ok_)
            if (text::GameFont* f = fonts_.get("rank")) {
                text::TextStyle st;
                fonts_.resolve("rank#1", st);
                st.scale = 1.0f, st.scale_x *= kw * 0.8f, st.scale_y *= kh * 0.8f, st.spacing = 3;
                st.valign = text::VAlign::Baseline;
                st.color = SDL_Color{0, 0, 0, 192};
                f->draw(X(L.rank_x + 4), Y(L.rank_y + 4), kRank[d.position - 1], st);
                st.color = kAmber;
                f->draw(X(L.rank_x), Y(L.rank_y), kRank[d.position - 1], st);
            }
    }
    if (horizontal) { // the dark panel behind gear, AT/MT and the bar tachometer
        float x0 = 484, x1 = 610, y0 = 181, y1 = 210, c = 3;
        std::vector<SDL_FPoint> pts = {{x0 + c, y0}, {x1 - c, y0}, {x0, y0 + c}, {x1, y0 + c},
                                       {x0, y1 - c}, {x1, y1 - c}, {x0 + c, y1}, {x1 - c, y1}};
        std::vector<SDL_Vertex> v;
        for (size_t i = 0; i + 2 < pts.size(); ++i)
            for (size_t k = 0; k < 3; ++k) {
                SDL_Vertex vx{};
                vx.position = {X(pts[i + k].x), Y(pts[i + k].y)};
                vx.color = {0, 0, 0, 0.5f};
                v.push_back(vx);
            }
        SDL_RenderGeometry(r, nullptr, v.data(), int(v.size()), nullptr, 0);
    }
    // The 16:9 halves draw their text as the full screen does (presentation §D8): the pen on
    // the baseline, right-aligned values, the label's right edge max(width, 90) + 10 left of
    // a time's (10 left of the lap count's).
    auto base = [&](float x, float y, const std::string& s, const char* font) {
        if (!fonts_ok_) return 0.0f;
        text::TextStyle st;
        text::GameFont* f = fonts_.resolve(font, st);
        if (!f) return 0.0f;
        st.scale_x *= kw;
        st.scale_y *= kh;
        st.color = kAmber;
        st.align = text::HAlign::Right;
        st.valign = text::VAlign::Baseline;
        f->draw(X(x), Y(y), s, st);
        return f->measure(s).x;
    };
    if (d.lap > 0) {
        const int lap = d.total_laps > 0 ? std::min(d.lap, d.total_laps) : d.lap;
        if (horizontal) {
            if (d.total_laps > 0) std::snprintf(buf, sizeof(buf), "Lap %d/%d", lap, d.total_laps);
            else std::snprintf(buf, sizeof(buf), "Lap %d", lap);
            txt(L.lap_r, L.lap_y, buf, "sw_20", 1, kAmber, text::HAlign::Right);
        } else {
            if (d.total_laps > 0) std::snprintf(buf, sizeof(buf), "%d/%d", lap, d.total_laps);
            else std::snprintf(buf, sizeof(buf), "%d", lap);
            const float w = base(L.lap_r, L.lap_y, buf, "sw_20");
            base(L.lap_r - w - 10, L.lap_y, "Lap", "sw_16");
        }
    }
    // Fastest: the race's fastest lap so far (either player's); the Lap line goes once this
    // half's player has finished (presentation §D8).
    const int lines = d.finished ? 2 : 3;
    const char* labels[3] = {"Fastest", "Last", "Lap"};
    const double times[3] = {d.session_best, d.last_lap, d.lap > 0 ? d.lap_time : -1};
    for (int i = 0; i < lines; ++i) {
        if (horizontal) { // label (small) and time (large), as on the captures
            txt(L.lines_r, L.line_y[i] - 12, format_time(times[i]), "sw_16", 1.45f, kAmber, text::HAlign::Right);
            txt(L.lines_r - 104, L.line_y[i] - 8, labels[i], "sw_12", 1.45f, kAmber, text::HAlign::Right);
        } else {
            const float w = base(L.lines_r, L.line_y[i], format_time(times[i]), "sw_16");
            base(L.lines_r - std::max(w, 90.0f) - 10, L.line_y[i], labels[i], "sw_12");
        }
    }
    // Bar tachometer "meter_mini" (right/bottom aligned): bars light up with rpm.
    if (const Image* im = image("meter_mini")) {
        float w = L.tach_w, h = L.tach_h, x = L.tach_r - w, y = L.tach_y - h;
        int scale = tach_scale(d.rev_limit_rpm);
        float frac = std::clamp(d.rpm / float(scale), 0.0f, 1.0f);
        SDL_FRect dst{X(x), Y(y), w * kw, h * kh};
        SDL_SetTextureAlphaMod(im->tex, 70);
        SDL_RenderTexture(r, im->tex, nullptr, &dst);
        SDL_SetTextureAlphaMod(im->tex, 255);
        SDL_FRect src{0, 0, im->w * frac, im->h};
        SDL_FRect lit{dst.x, dst.y, dst.w * frac, dst.h};
        SDL_RenderTexture(r, im->tex, &src, &lit);
        if (horizontal) {
            txt(x + w / 2, y - 3, d.automatic ? ">" : "=", "meter#1", 1, kAmber, text::HAlign::Center);
            // Shift light: a red dot at the panel's top right, lit above the shift-up rpm.
            bool shift_lit = d.rpm > d.shift_rpm && (blink_ / 3) % 2 == 0;
            fill(r, X(x + w + 3), Y(y - 4), 3 * kw, 3 * kh, shift_lit ? kRed : SDL_Color{60, 0, 0, 200});
        } else {
            txt(x, y + 5, d.automatic ? ">" : "=", "meter#1", 1, kAmber, text::HAlign::Left);
        }
    }
    std::snprintf(buf, sizeof(buf), "%3d", int(d.imperial ? d.speed_kmh / 1.609344f : d.speed_kmh));
    txt(L.speed_r, L.speed_y - (horizontal ? 21 : 28), buf, "speed", horizontal ? 1.13f : 1.0f, kAmber, text::HAlign::Right);
    txt(L.speed_r + 3, L.speed_y - (horizontal ? 9 : 14), d.imperial ? "mph" : "km/h", "sw_12", horizontal ? 1.15f : 1.0f, kAmber, text::HAlign::Left);
    std::string g = d.gear >= 1 ? std::to_string(std::min(d.gear, 9)) : (d.gear < 0 ? "R" : "N");
    float gs = horizontal ? 1.15f : 0.8f;
    txt(L.gear_x - 30, L.gear_y - (horizontal ? 30 : 22), g, g == "N" ? "sw_20" : "gearr#1", g == "N" ? 1.3f : gs,
        d.gear < 0 ? kRed : kAmber, text::HAlign::Left);
    if (horizontal) txt(L.gear_x - 8, L.gear_y - 20, ">8", "gearr#1", 0.55f, SDL_Color{0, 0, 0, 110}, text::HAlign::Left);
    if (d.countdown >= 0 && !horizontal) {
        char name[16];
        if (d.countdown > 0) std::snprintf(name, sizeof(name), "count%d", std::clamp(int(std::ceil(d.countdown)), 1, 3));
        else std::snprintf(name, sizeof(name), "start");
        if (const Image* im = image(name)) {
            SDL_FRect rc{X(L.big_x - im->w / 2), Y(L.big_y - im->h / 2), im->w * kw, im->h * kh};
            SDL_RenderTexture(r, im->tex, nullptr, &rc);
        }
    }
    // (No "PLAYER n WIN!": the original shows AUTO DRIVE, then its results panel over the half.)
    if (d.auto_drive && horizontal) txt(L.speed_r - 78, L.speed_y - 20, "AUTO DRIVE", "sw_16", 1.3f, kAmber, text::HAlign::Right);
    if (d.wrong_way) txt(L.big_x, L.warn_y - 8, "WRONG WAY", "sw_16", 1, kRed, text::HAlign::Center);
    // Event display (presentation §D6): the half's own, centred on (320, 75) of the 640 x 224
    // half (horizontal) or (213, 165) of the 426.667 x 448 half, at most 320 / 384 units wide
    // (wider texts are squeezed), the pen on the baseline.
    event_icon_rect_ = {0, 0, 0, 0};
    const float ex = horizontal ? 320.0f : 213.0f, ey = horizontal ? 75.0f : 165.0f;
    const float max_w = horizontal ? 320.0f : 384.0f;
    if (fonts_ok_)
        for (const EventDisplay::Message& m : d.events) {
            text::TextStyle st;
            text::GameFont* f = fonts_.resolve(m.large ? "sw_20" : "sw_16", st);
            if (!f) continue;
            st.scale = horizontal ? 0.7f : 1.0f;
            const float w = f->measure(m.text, st).x;
            st.scale_x *= kw * (w > max_w ? max_w / w : 1.0f);
            st.scale_y *= kh;
            st.color = SDL_Color{m.r, m.g, m.b, 255};
            st.align = text::HAlign::Center;
            st.valign = text::VAlign::Baseline;
            f->draw(X(ex + m.dx), Y(ey + m.dy), m.text, st);
        }
    if (d.event_icon) {
        const float k = horizontal ? 0.7f : 1.0f; // the picture with the half's text size
        event_icon_rect_ = {X(ex + d.event_icon_dx - 32 * k), Y(ey + d.event_icon_dy - 16 * k), 64 * k * kw, 32 * k * kh};
    }
}

} // namespace gt3r::race
