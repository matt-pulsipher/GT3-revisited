#include "race/analyzer.h"

#include "base/log.h"
#include "formats/tex1.h"
#include "menu/flow/menu_controller.h"
#include "menu/menu_text.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace gt3r::race {

namespace {

using menu::flow::key_bit;
using menu::flow::MenuKey;

constexpr float kW = 640, kH = 448;
constexpr SDL_Color kRed{205, 73, 73, 255}, kBlue{80, 124, 186, 255}, kGrey{204, 204, 204, 255},
    kDim{110, 110, 110, 255}, kGrid{48, 48, 52, 255};

// Menu space (640 x 448, y up) onto the destination rectangle.
struct Frame {
    SDL_FRect dst;
    float x(float mx) const { return dst.x + mx * dst.w / kW; }
    float y(float my) const { return dst.y + (kH - my) * dst.h / kH; } // y up
    float sx() const { return dst.w / kW; }
    float sy() const { return dst.h / kH; }
    SDL_FRect box(float x0, float y0, float w, float h) const { return {x(x0), y(y0 + h), w * sx(), h * sy()}; }
};

void fill(SDL_Renderer* r, const Frame& f, float x, float y, float w, float h, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect q = f.box(x, y, w, h);
    SDL_RenderFillRect(r, &q);
}

// `game`: `s` is game text (a database name: escapes and JIS pairs, drawn as the game draws them).
constexpr bool kGameText = true;

void text(text::GameFonts* fonts, const Frame& f, float mx, float my, const std::string& s, const char* font,
          float scale, SDL_Color c, text::HAlign a = text::HAlign::Left, bool game = false) {
    if (!fonts) return;
    text::TextStyle st;
    text::GameFont* g = fonts->resolve(font, st);
    if (!g) return;
    st.scale_x *= scale * f.sx();
    st.scale_y *= scale * f.sy();
    st.color = c;
    st.align = a;
    st.valign = text::VAlign::Middle;
    if (game) g->draw_game(f.x(mx), f.y(my), s, st);
    else g->draw(f.x(mx), f.y(my), s, st);
}

std::string fmt_time(double s) {
    if (!(s >= 0)) return "-'--.---";
    long ms = std::lround(s * 1000.0);
    char b[32];
    std::snprintf(b, sizeof(b), "%ld'%02ld.%03ld", ms / 60000, (ms / 1000) % 60, ms % 1000);
    return b;
}

const menu::Element* find(const menu::MenuScene& s, const std::string& name) {
    if (s.file().pages.empty()) return nullptr;
    const menu::Page& p = s.file().pages[0];
    for (const menu::Element& e : p.items)
        if (e.name == name) return &e;
    for (const menu::Layer& l : p.layers) {
        for (const menu::Element& e : l.elements)
            if (e.name == name) return &e;
        for (const menu::Element& e : l.widgets)
            if (e.name == name) return &e;
    }
    return nullptr;
}

} // namespace

size_t AnalyzerTrack::index_at_distance(float d) const {
    auto it = std::lower_bound(trace.dist.begin(), trace.dist.end(), d);
    return std::min(size_t(it - trace.dist.begin()), trace.size() ? trace.size() - 1 : 0);
}

u32 AnalyzerTrack::frame_at_distance(float d, u32 begin) const {
    const size_t i = index_at_distance(d);
    if (frames.empty()) return begin + u32(i);
    return i == 0 ? lap_begin : frames[i - 1]; // (bin i starts where bin i - 1 ends)
}

float AnalyzerTrack::distance_at_frame(u32 f, u32 begin) const {
    if (!valid()) return 0;
    if (frames.empty()) return trace.dist[std::min<size_t>(f > begin ? f - begin : 0, trace.size() - 1)];
    const size_t i = size_t(std::lower_bound(frames.begin(), frames.end(), f) - frames.begin());
    return trace.dist[std::min(i, trace.size() - 1)];
}

namespace {

using ee::F;
const F kZero = F::raw(0), kHalf = F::raw(0x3F000000), kOne = F::raw(0x3F800000);
constexpr size_t kMaxBins = 0x4000;

// The area under the line from (d0, v0) to (d1, v1) over [lo, hi]: 0 when the segment misses it, else the segment
// cut to it (the line's values at the cuts) as a trapezoid.
F bin_area(F d0, F d1, F v0, F v1, F lo, F hi) {
    if (d1 < lo || hi < d0) return kZero;
    if (d0 < lo || hi < d1) {
        const F f1 = v1 * d0;
        const F f3 = (v0 * d1) - f1;
        const F inv = kOne / (d1 - d0);
        const F dv = v1 - v0;
        if (d0 < lo) v0 = ((dv * lo) + f3) * inv, d0 = lo;
        if (hi < d1) v1 = ((dv * hi) + f3) * inv, d1 = hi;
    }
    return ((v0 + v1) * (d1 - d0)) * kHalf;
}

// The value at `at` of the line through (d0, x0) and (d1, x1).
F line_at(F d0, F d1, F x0, F x1, F at) {
    const F a = x0 * d1, b = x1 * d0;
    const F inv = kOne / (d1 - d0);
    const F c = a - b;
    return (((x1 - x0) * at) + c) * inv;
}

// At t = 0: a drawn coordinate between the last two steps (the race-flow accumulator's share of the last).
F drawn(F prev, F cur, F flow) {
    const F f = kZero + flow;
    return (prev * f) + (cur * (kOne - f));
}

} // namespace

LoggerBinner::LoggerBinner(F lap) : lap_(lap) {
    w_ = ee::max(lap / F::raw(0x467FFC00), kHalf); // 16383
    inv_w_ = kOne / w_;
}

void LoggerBinner::add(const sim::exact::Car& c, double time, u32 frame) {
    if (bins_.size() >= kMaxBins) return;
    const std::array<F, 3> v{c.speed_abs, c.throttle_pedal, c.brake_pedal};
    const F x = drawn(c.draw_pos_prev.x, c.draw_pos.x, c.flow), z = drawn(c.draw_pos_prev.z, c.draw_pos.z, c.flow);
    F d = c.f3e4 + ee::int_to_f(s32(c.laps) - 1) * lap_;
    auto store = [&] {
        if (!started_ && !(d < kZero)) { // (ours: where the lap starts)
            const double dd = double(d.host()) - double(d_.host());
            start_t_ = first_ || dd <= 0 ? time : t_ + (0.0 - double(d_.host())) / dd * (time - t_);
            start_frame_ = frame, started_ = true;
        }
        first_ = false;
        d_ = d, v_ = v, x_ = x, z_ = z, t_ = time;
    };
    if (first_) {
        if (kZero < d) d = kZero;
        edge_ = d;
        return store();
    }
    if (d <= edge_) {
        d = edge_;
        return store();
    }
    edge_ = d;
    F lo = ee::int_to_f(s32(bins_.size())) * w_;
    F hi = lo + w_;
    for (size_t k = 0; k < 3; ++k) acc_[k] = acc_[k] + bin_area(d_, d, v_[k], v[k], lo, hi);
    const u8 gear = u8(c.gear & 7);
    while (hi <= d) {
        F s = acc_[0], a = acc_[1], b = acc_[2];
        const F k255 = F::raw(0x437F0000);
        a = a * k255, b = b * k255;
        s = s * F::raw(0x42200000); // 40
        a = a * inv_w_, b = b * inv_w_, s = s * inv_w_;
        a = a + kHalf, b = b + kHalf, s = s + kHalf;
        const F px = line_at(d_, d, x_, x, hi), pz = line_at(d_, d, z_, z, hi);
        LoggerBin bin;
        s = ee::min(s, F::raw(0x45FFF800)); // 8191
        bin.word = u16(gear | u32(ee::to_int(s)) << 3);
        bin.throttle = u8(ee::to_int(ee::min(a, k255)));
        bin.brake = u8(ee::to_int(ee::min(b, k255)));
        bin.x = s16(ee::to_int(px)), bin.z = s16(ee::to_int(pz));
        const double dd = double(d.host()) - double(d_.host());
        bin.time = dd > 0 ? t_ + (double(hi.host()) - double(d_.host())) / dd * (time - t_) : time;
        bin.frame = frame;
        bins_.push_back(bin);
        if (bins_.size() >= kMaxBins) return; // (the last step's values are not kept then)
        hi = hi + w_, lo = lo + w_;
        for (size_t k = 0; k < 3; ++k) acc_[k] = bin_area(d_, d, v_[k], v[k], lo, hi);
    }
    store();
}

void LoggerBinner::finish() {
    if (bins_.empty()) return;
    u32 ps = bins_[0].word >> 3, pt = bins_[0].throttle, pb = bins_[0].brake; // the bin before's, as they were
    for (size_t i = 1; i + 1 < bins_.size(); ++i) {
        LoggerBin& b = bins_[i];
        const LoggerBin& n = bins_[i + 1];
        const u32 s = b.word >> 3, t = b.throttle, r = b.brake;
        b.word = u16((((ps + 2 * s + (n.word >> 3)) >> 2) << 3) | (b.word & 7));
        b.throttle = u8((pt + 2 * t + n.throttle) >> 2);
        b.brake = u8((pb + 2 * r + n.brake) >> 2);
        ps = s, pt = t, pb = r;
    }
}

AnalyzerTrack logger_track(const LoggerBinner& bins, const std::map<s32, sim::exact::Snapshot>* table, double hz) {
    AnalyzerTrack t;
    t.hz = hz;
    const float w = bins.width().host();
    LapTrace& tr = t.trace;
    for (size_t k = 0; k < bins.bins().size(); ++k) {
        const LoggerBin& b = bins.bins()[k];
        tr.dist.push_back(float(k) * w);
        tr.speed_kmh.push_back(b.speed() * 3.6f);
        tr.throttle.push_back(float(b.throttle) / 255.0f);
        tr.brake.push_back(float(b.brake) / 255.0f);
        tr.x.push_back(float(b.x)), tr.z.push_back(float(b.z));
        t.times.push_back(std::max(0.0, b.time - bins.start_time()));
        t.frames.push_back(b.frame);
    }
    t.lap_begin = bins.start_frame();
    t.lap_end = t.frames.empty() ? t.lap_begin : t.frames.back();
    // The logger's entries end at its marks and at the lap: their times count from the lap's start (ms).
    double last = 0;
    if (table)
        for (const auto& [k, e] : *table) {
            if (k < 0 || e.time == 0 || e.time == sim::exact::kNoTime) continue; // (an entry not ended)
            const double at = double(e.time) / 1000.0;
            t.sector_times.push_back(at - last);
            last = at;
        }
    tr.time = !t.sector_times.empty() ? last : t.times.empty() ? -1.0 : t.times.back();
    return t;
}

std::optional<LoggerMap> LoggerMap::parse(ByteSpan d) {
    if (d.size() < 0x38 || std::memcmp(d.data(), "LGM0", 4) != 0) return std::nullopt;
    auto u32at = [&](size_t o) { u32 v; std::memcpy(&v, d.data() + o, 4); return v; };
    auto f32at = [&](size_t o) { float v; std::memcpy(&v, d.data() + o, 4); return v; };
    LoggerMap m;
    m.ox = f32at(0x18), m.oz = f32at(0x1C), m.scale = f32at(0x20);
    m.rot = f32at(0x24) * 3.14159265f / 180.0f;
    const u32 tex = u32at(0x30), n = u32at(0x34);
    if (n > 64 || 0x38 + size_t(n) * 16 > d.size()) return std::nullopt;
    for (u32 i = 0; i < n; ++i) m.points.push_back({f32at(0x38 + 16 * i), f32at(0x3C + 16 * i), f32at(0x40 + 16 * i)});
    if (tex > 0 && tex < d.size()) {
        try {
            ByteSpan t = d.subspan(tex);
            Tex1 t1 = Tex1::parse(t);
            GsMemory gs;
            t1.upload(t, gs);
            Tex1::Image img = t1.decode(gs, 0);
            m.width = int(img.width), m.height = int(img.height);
            m.rgba = std::move(img.rgba);
        } catch (const std::exception& e) {
            LOG_WARN("Analyzer: logger map picture: %s", e.what());
        }
    }
    return m;
}

std::pair<float, float> LoggerMap::project(float x, float z) const {
    // The origin is taken off after the rotation (unlike the HUD map's GTCM, which rotates
    // the offset from it): every map's sector points land on its outline this way.
    float px = scale * (std::cos(rot) * x - std::sin(rot) * z - ox);
    float pz = scale * (std::sin(rot) * x + std::cos(rot) * z - oz);
    return {float(width) / 2 + px, float(height) / 2 + pz};
}

std::vector<float> logger_sectors(const LoggerMap& m, const std::function<float(float, float, float)>& distance_of,
                                  float lap_length) {
    std::vector<float> s{0.0f};
    for (const auto& p : m.points) {
        float d = distance_of(p[0], p[1], p[2]);
        if (d > 1.0f && d < lap_length - 1.0f) s.push_back(d);
    }
    std::sort(s.begin(), s.end());
    s.erase(std::unique(s.begin(), s.end(), [](float a, float b) { return std::fabs(a - b) < 5.0f; }), s.end());
    if (s.size() > 40) s.resize(40); // the page has 40 sector labels
    return s;
}

Analyzer::~Analyzer() {
    if (map_tex_) SDL_DestroyTexture(map_tex_);
}

bool Analyzer::load(const Vfs& vfs, SDL_Renderer* r, text::GameFonts& fonts, const std::string& lang) {
    fonts_ = &fonts;
    try {
        scene_ = menu::MenuScene::load(vfs, "menu/" + lang + "/logger1.pmb");
    } catch (const std::exception& e) {
        LOG_WARN("Analyzer: %s", e.what());
        scene_.reset();
    }
    renderer_ = std::make_unique<menu::MenuRenderer>(r);
    if (fonts.loaded()) renderer_->set_text_drawer(menu::make_game_font_drawer(fonts));
    return scene_.has_value();
}

void Analyzer::set_course(SDL_Renderer* r, std::optional<LoggerMap> map, std::vector<float> sectors, float lap_length) {
    if (map_tex_) SDL_DestroyTexture(map_tex_);
    map_tex_ = nullptr;
    map_ = std::move(map);
    if (map_ && map_->width > 0 && r) {
        map_tex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, map_->width, map_->height);
        if (map_tex_) {
            SDL_UpdateTexture(map_tex_, nullptr, map_->rgba.data(), map_->width * 4);
            SDL_SetTextureBlendMode(map_tex_, SDL_BLENDMODE_BLEND);
        }
    }
    sectors_ = std::move(sectors);
    if (sectors_.empty()) sectors_.push_back(0);
    lap_length_ = lap_length;
}

// A lap's distances must rise from the line: samples taken just before the line (distance
// near the lap length) at its start and just after it at its end are dropped.
static void trim_lap(AnalyzerTrack& t, float L) {
    LapTrace& tr = t.trace;
    if (L <= 0 || tr.size() < 2) return;
    size_t a = 0, b = tr.size();
    while (a < b && tr.dist[a] > L * 0.5f && a < tr.size() / 2) ++a;
    while (b > a && tr.dist[b - 1] < L * 0.5f && b > tr.size() / 2) --b;
    if (a == 0 && b == tr.size()) return;
    auto cut = [&](std::vector<float>& v) {
        if (v.size() == tr.dist.size()) v = std::vector<float>(v.begin() + long(a), v.begin() + long(b));
    };
    for (std::vector<float>* v : {&tr.speed_kmh, &tr.throttle, &tr.brake, &tr.x, &tr.z}) cut(*v);
    cut(tr.dist);
    if (!t.ghost.empty() && t.ghost.size() >= b) {
        std::vector<sim::VehicleState>& g = t.ghost.frames();
        g = std::vector<sim::VehicleState>(g.begin() + long(a), g.begin() + long(b));
    }
}

void Analyzer::set_tracks(AnalyzerTrack a, AnalyzerTrack b) {
    a_ = std::move(a);
    b_ = std::move(b);
    trim_lap(a_, lap_length_);
    trim_lap(b_, lap_length_);
    if (!a_.valid() && b_.valid()) display_ = 1;
    else if (a_.valid() && !b_.valid()) display_ = 0;
}

void Analyzer::open(View v) {
    view_ = v;
    panel_ = false;
    cursor_ = 0;
    control_ = Start;
    playing_ = true, slow_ = false;
    camera_presses_ = 0;
    seek_.reset();
    held_ = 0, ignore_ = 0xFFFF;
    repeat_ = 0;
}

void Analyzer::set_cursor(float d) { cursor_ = std::clamp(d, 0.0f, std::max(lap_length_, 0.0f)); }

std::vector<std::string> Analyzer::take_sounds() { return std::exchange(sounds_, {}); }

int Analyzer::sector_at(float d) const {
    int s = 0;
    for (size_t i = 0; i < sectors_.size(); ++i)
        if (d >= sectors_[i]) s = int(i);
    return s;
}

void Analyzer::jump(int dir) {
    int s = sector_at(cursor_);
    // Back: to this sector's start, or the previous one's when already there (M).
    int t = dir < 0 ? (cursor_ - sectors_[size_t(s)] > 5.0f ? s : s - 1) : s + 1;
    if (t < 0 || t >= int(sectors_.size())) {
        sounds_.push_back("disable");
        return;
    }
    cursor_ = sectors_[size_t(t)];
    if (view_ == View::Replay) seek_ = cursor_;
    sounds_.push_back("cursor");
}

bool Analyzer::tick(u16 held) {
    ++frames_;
    ignore_ &= held;
    held &= u16(~ignore_);
    const u16 pressed = held & u16(~held_);
    held_ = held;
    auto down = [&](MenuKey k) { return (held & key_bit(k)) != 0; };
    auto key = [&](MenuKey k) { return (pressed & key_bit(k)) != 0; };
    if (panel_) {
        if (key(MenuKey::Up) || key(MenuKey::Down)) {
            int r = std::clamp(panel_row_ + (key(MenuKey::Down) ? 1 : -1), 0, 3);
            if (r != panel_row_) panel_row_ = r, sounds_.push_back("cursor");
        } else if (key(MenuKey::Left) || key(MenuKey::Right)) {
            const int d = key(MenuKey::Right) ? 1 : -1;
            bool* sw = panel_row_ == 0 ? &show_speed_ : panel_row_ == 1 ? &show_accel_ : panel_row_ == 2 ? &show_brake_ : nullptr;
            if (sw) {
                bool v = d < 0; // ON left, OFF right
                if (*sw != v) *sw = v, sounds_.push_back("cursor");
            } else {
                int n = std::clamp(display_ + d, 0, 2);
                if (n != display_) display_ = n, sounds_.push_back("cursor");
            }
        } else if (key(MenuKey::Confirm) || key(MenuKey::Cancel)) {
            panel_ = false;
            sounds_.push_back(key(MenuKey::Confirm) ? "ok" : "cancel");
        }
        return true;
    }
    if (key(MenuKey::Cancel)) {
        sounds_.push_back("cancel");
        return false;
    }
    if (key(MenuKey::L1)) jump(-1);
    if (key(MenuKey::R1)) jump(1);
    if (view_ == View::Graph) {
        // The cursor runs along the lap: 2 m per frame, faster while held (M).
        if (down(MenuKey::Left) || down(MenuKey::Right)) {
            repeat_ = (key(MenuKey::Left) || key(MenuKey::Right)) ? 0 : repeat_ + 1;
            float step = repeat_ < 30 ? 2.0f : repeat_ < 90 ? 6.0f : 15.0f;
            set_cursor(cursor_ + (down(MenuKey::Right) ? step : -step));
        }
        if (key(MenuKey::Confirm)) {
            panel_ = true;
            sounds_.push_back("ok");
        }
    } else {
        if (key(MenuKey::Left) || key(MenuKey::Right)) {
            int c = std::clamp(control_ + (key(MenuKey::Right) ? 1 : -1), 0, 3);
            if (c != control_) control_ = c, sounds_.push_back("cursor");
        }
        if (key(MenuKey::Confirm)) {
            sounds_.push_back("ok");
            switch (control_) {
            case Start: playing_ = true; break;
            case Stop: playing_ = false; break;
            case Slow: slow_ = !slow_, playing_ = true; break;
            case Camera: ++camera_presses_; break;
            }
        }
    }
    return true;
}

void Analyzer::draw(SDL_Renderer* r, const SDL_FRect& dst) {
    if (!scene_ || !renderer_) return;
    const Frame f{dst};
    if (view_ == View::Graph) draw_graph(r, dst);
    renderer_->set_scene(&*scene_);
    std::vector<menu::ElementDraw> list;
    auto add = [&](const menu::Element* e, float dx = 0, float dy = 0, float brightness = 1,
                   const std::string* txt = nullptr) {
        if (!e) return;
        menu::ElementDraw d;
        d.element = e;
        d.dx = dx, d.dy = dy;
        d.brightness = brightness;
        if (txt) d.text = std::make_shared<const std::string>(*txt);
        list.push_back(d);
    };
    // The bottom band.
    add(find(*scene_, "bg"));
    add(find(*scene_, "indicator_back"));
    if (view_ == View::Graph) add(find(*scene_, "jump"));
    // REPLAY controls: the focused one lit, the active state (playing / stopped / slow) bright.
    if (view_ == View::Replay) {
        static const char* const kCtl[4] = {"RawListItemFixed_ReplayStart", "RawListItemFixed_ReplayStop",
                                            "RawListItemFixed_ReplaySlow", "RawListItemFixed_ReplayCamera"};
        for (int i = 0; i < 4; ++i) {
            bool active = (i == Start && playing_ && !slow_) || (i == Stop && !playing_) || (i == Slow && slow_);
            add(find(*scene_, kCtl[i]), 0, 0, i == control_ ? 1.0f : active ? 0.8f : 0.45f);
        }
    }
    // Status panels: the LoggerReplayStatus template at TRACK A / TRACK B.
    const menu::Element* tmpl = find(*scene_, "LoggerReplayStatus");
    for (int t = 0; t < 2 && tmpl; ++t) {
        const AnalyzerTrack& tr = t == 0 ? a_ : b_;
        const menu::Element* inst = find(*scene_, std::string(view_ == View::Graph ? "LoggerReplayStatus_Graph"
                                                                                   : "LoggerReplayStatus_Replay") +
                                                       (t == 0 ? "TrackA" : "TrackB"));
        if (!inst) continue;
        const float dx = inst->x - tmpl->x, dy = inst->y - tmpl->y;
        add(tmpl, dx, dy, tr.valid() ? 1.0f : 0.4f);
        for (const char* n : {"r_speed", "r_total", "r_sector", "r_average"}) add(find(*scene_, n), dx, dy);
    }
    menu::RenderOptions ro{false, false, true, true};
    renderer_->draw_elements(list, ro, dst);
    list.clear();

    // Status values and titles (code-filled; the info fields have no font of their own).
    const int sec = sector_at(cursor_);
    const float s0 = sectors_[size_t(sec)];
    const float s1 = size_t(sec) + 1 < sectors_.size() ? sectors_[size_t(sec) + 1] : lap_length_;
    for (int t = 0; t < 2 && tmpl; ++t) {
        const AnalyzerTrack& tr = t == 0 ? a_ : b_;
        const menu::Element* inst = find(*scene_, std::string(view_ == View::Graph ? "LoggerReplayStatus_Graph"
                                                                                   : "LoggerReplayStatus_Replay") +
                                                       (t == 0 ? "TrackA" : "TrackB"));
        if (!inst) continue;
        const float dx = inst->x - tmpl->x, dy = inst->y - tmpl->y;
        const SDL_Color c = t == 0 ? kRed : kBlue;
        // The panel's title: TRACK A / TRACK B and the car.
        // TRACK A / TRACK B where the template's icon goes, the car's name at its title.
        if (const menu::Element* ic = find(*scene_, "r_icon"))
            text(fonts_, f, ic->x + dx - 32, ic->y + dy + ic->h / 2, t == 0 ? "TRACK A" : "TRACK B", "sw_16#!1", 1, c);
        if (!tr.valid()) continue;
        const size_t i = tr.index_at_distance(cursor_);
        const size_t i0 = tr.index_at_distance(s0), i1 = tr.index_at_distance(std::min(cursor_, s1));
        const float unit = imperial ? 1.0f / 1.609344f : 1.0f;
        char buf[32];
        std::snprintf(buf, sizeof buf, "%d %s", int(std::lround(tr.trace.speed_kmh[i] * unit)), imperial ? "mph" : "km/h");
        // The sector's time: the data logger's for the whole sector where the track has its table, else the samples' to the
        // cursor.
        const bool logged = size_t(sec) < tr.sector_times.size();
        const double st = logged ? tr.sector_times[size_t(sec)] : tr.time_at(i1) - tr.time_at(i0);
        const float sd = logged ? s1 - s0 : tr.trace.dist[i1] - tr.trace.dist[i0];
        std::string avg = "---";
        if (st > 0.05) {
            std::snprintf(buf + 16, 16, "%d", int(std::lround(sd / st * 3.6 * unit)));
            avg = std::string(buf + 16) + (imperial ? " mph" : " km/h");
        }
        const std::string vals[4] = {std::string(buf, std::strlen(buf)), fmt_time(tr.time_at(i)), fmt_time(st), avg};
        static const char* const kInfo[4] = {"r_speed_info", "r_total_info", "r_sector_info", "r_average_info"};
        for (int k = 0; k < 4; ++k)
            if (const menu::Element* e = find(*scene_, kInfo[k]))
                text(fonts_, f, e->x + dx + e->w, e->y + dy + e->h / 2, vals[k], "sw_16#1", 0.9f, kGrey, text::HAlign::Right);
        if (const menu::Element* ti = find(*scene_, "r_title")) {
            // (Cut by characters: a JIS pair is one.)
            std::string name = text::game_text_length(tr.title) > 20 ? text::game_text_prefix(tr.title, 19) + "." : tr.title;
            text(fonts_, f, ti->x + dx + 4, ti->y + dy + ti->h / 2, name, "sw_12#1", 1, kGrey, text::HAlign::Left, kGameText);
        }
    }

    // The sector bar: up to ten labels in view, the cursor's sector lit (M).
    if (const menu::Element* bar = find(*scene_, "LoggerSector_Sectors")) {
        const int n = int(sectors_.size());
        const int vis = 10;
        int first = std::clamp(sec - vis / 2, 0, std::max(0, n - vis));
        const float w = bar->w / float(vis);
        for (int k = first; k < std::min(n, first + vis); ++k) {
            float x = bar->x + float(k - first) * w;
            if (const menu::Element* lab = find(*scene_, "icon_sector" + std::to_string(k + 1))) {
                menu::ElementDraw d;
                d.element = lab;
                d.has_box = true;
                d.box = {x + (w - lab->w) / 2, bar->y, lab->w, lab->h};
                d.brightness = k == sec ? 1.0f : 0.45f;
                list.push_back(d);
            }
        }
        renderer_->draw_elements(list, ro, dst);
        list.clear();
        // Progress through the lap under the bar.
        if (lap_length_ > 0) {
            fill(r, f, bar->x, bar->y - 3, bar->w, 2, kGrid);
            fill(r, f, bar->x, bar->y - 3, bar->w * std::clamp(cursor_ / lap_length_, 0.0f, 1.0f), 2, SDL_Color{233, 112, 20, 255});
        }
    }

    // EXPRESSION panel.
    if (view_ == View::Graph && panel_) {
        for (const char* n : {"LoggerGraphOperation", "RawListV_GraphOperation", "GraphOperationTitle", "GraphVelocityTitle",
                              "GraphAccellTitle", "GraphBrakeTitle", "GraphDisplayTitle"})
            add(find(*scene_, n));
        static const char* const kRows[4] = {"RawListHFixed_GraphVelocity", "RawListHFixed_GraphAccell",
                                             "RawListHFixed_GraphBrake", "RawListHFixed_GraphDisplay"};
        for (int k = 0; k < 4; ++k) add(find(*scene_, kRows[k]), 0, 0, k == panel_row_ ? 2.2f : 1.0f);
        auto opt = [&](const char* n, bool on) { add(find(*scene_, n), 0, 0, on ? 1.6f : 0.55f); };
        opt("RawListItemFixed_GraphVelocityN", show_speed_);
        opt("RawListItemFixed_GraphVelocityF", !show_speed_);
        opt("RawListItemFixed_GraphAccellON", show_accel_);
        opt("RawListItemFixed_GraphAccellOFF", !show_accel_);
        opt("RawListItemFixed_GraphBraleON", show_brake_);
        opt("RawListItemFixed_GraphBraleOFF", !show_brake_);
        opt("RawListItemFixed_GraphDisplayA", display_ == 0);
        opt("RawListItemFixed_GraphDisplayB", display_ == 1);
        opt("RawListItemFixed_GraphDisplayAB", display_ == 2);
        std::stable_sort(list.begin(), list.end(),
                         [](const menu::ElementDraw& x, const menu::ElementDraw& y) { return x.element->z < y.element->z; });
        renderer_->draw_elements(list, ro, dst);
    }
}

void Analyzer::draw_graph(SDL_Renderer* r, const SDL_FRect& dst) {
    const Frame f{dst};
    // The graph area (LoggerRaceGraph_Graph, black): speed above, accel and brake strips below.
    const float gx = 16, gw = 608; // the traces: y 218..436 (y up)
    fill(r, f, 0, 180, kW, 268, SDL_Color{0, 0, 0, 255});
    const float L = std::max(lap_length_, 1.0f);
    // The window: a quarter of the lap (at least 400 m) with the cursor at its first third (M).
    const float span = std::min(L, std::max(400.0f, L / 4));
    const float w0 = std::clamp(cursor_ - span / 3, 0.0f, std::max(0.0f, L - span));
    auto gxof = [&](float d) { return gx + gw * (d - w0) / span; };
    const float unit = imperial ? 1.0f / 1.609344f : 1.0f;
    float vmax = 100;
    for (const AnalyzerTrack* t : {&a_, &b_})
        for (float v : t->trace.speed_kmh) vmax = std::max(vmax, v * unit);
    vmax = std::ceil(vmax / 50.0f) * 50.0f;
    const float sy0 = 300, sh = 136, ay0 = 258, ah = 36, by0 = 218, bh = 36;
    for (float v = 50; v < vmax; v += 50) {
        float y = sy0 + sh * v / vmax;
        fill(r, f, gx, y, gw, 1, kGrid);
        text(fonts_, f, gx + gw - 2, y + 6, std::to_string(int(v)), "sw_12#1", 0.8f, kDim, text::HAlign::Right);
    }
    fill(r, f, gx, ay0, gw, ah, SDL_Color{16, 16, 18, 255});
    fill(r, f, gx, by0, gw, bh, SDL_Color{16, 16, 18, 255});
    text(fonts_, f, gx + gw - 2, ay0 + ah - 7, "ACCEL", "sw_12#1", 0.8f, kDim, text::HAlign::Right);
    text(fonts_, f, gx + gw - 2, by0 + bh - 7, "BRAKE", "sw_12#1", 0.8f, kDim, text::HAlign::Right);
    // Sector boundaries.
    for (float s : sectors_)
        if (s >= w0 && s <= w0 + span) fill(r, f, gxof(s), by0, 1, sy0 + sh - by0, SDL_Color{70, 60, 40, 255});
    auto plot = [&](const AnalyzerTrack& t, const std::vector<float>& vals, float y0, float h, float scale, SDL_Color c) {
        std::vector<SDL_FPoint> pts;
        for (size_t i = 0; i < t.trace.size(); ++i) {
            float d = t.trace.dist[i];
            if (d < w0 || d > w0 + span) continue;
            pts.push_back({f.x(gxof(d)), f.y(y0 + h * std::clamp(vals[i] / scale, 0.0f, 1.0f))});
        }
        if (pts.size() < 2) return;
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderLines(r, pts.data(), int(pts.size()));
    };
    for (int k = 0; k < 2; ++k) {
        const AnalyzerTrack& t = k == 0 ? a_ : b_;
        if (!t.valid() || (display_ != 2 && display_ != k)) continue;
        const SDL_Color c = k == 0 ? kRed : kBlue;
        if (show_speed_) plot(t, t.trace.speed_kmh, sy0, sh, vmax / unit, c);
        if (show_accel_) plot(t, t.trace.throttle, ay0, ah, 1.0f, c);
        if (show_brake_) plot(t, t.trace.brake, by0, bh, 1.0f, c);
    }
    // The cursor line.
    fill(r, f, gxof(cursor_), by0, 1, sy0 + sh - by0, SDL_Color{255, 255, 255, 160});
    // The course map (LoggerMapCourse_GraphMode) with both cars at the cursor.
    if (scene_)
        if (const menu::Element* mb = find(*scene_, "LoggerMapCourse_GraphMode")) {
            fill(r, f, mb->x - 4, mb->y - 4, mb->w + 8, mb->h + 8, SDL_Color{0, 0, 0, 200});
            if (map_tex_) {
                SDL_FRect q = f.box(mb->x, mb->y, mb->w, mb->h);
                SDL_RenderTexture(r, map_tex_, nullptr, &q);
            }
            if (map_)
                for (int k = 1; k >= 0; --k) {
                    const AnalyzerTrack& t = k == 0 ? a_ : b_;
                    if (!t.valid() || t.trace.x.size() != t.trace.size()) continue;
                    size_t i = t.index_at_distance(cursor_);
                    auto [px, py] = map_->project(t.trace.x[i], t.trace.z[i]);
                    float mx = mb->x + px * mb->w / float(std::max(1, map_->width));
                    float my = mb->y + mb->h - py * mb->h / float(std::max(1, map_->height));
                    fill(r, f, mx - 3, my - 3, 6, 6, k == 0 ? kRed : kBlue);
                }
        }
}

} // namespace gt3r::race
