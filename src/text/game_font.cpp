#include "text/game_font.h"

#include "base/log.h"
#include "vfs/vfs.h"

#include <algorithm>

namespace gt3r::text {

namespace {

constexpr int kKanjiPageSize = 512; // cells per page = (512 / cell)^2

LayoutOptions options(const TextStyle& s) {
    LayoutOptions o;
    o.scale_x = s.scale * s.scale_x;
    o.scale_y = s.scale * s.scale_y;
    o.spacing = s.spacing;
    o.line_spacing = s.line_spacing;
    o.align = s.align;
    o.valign = s.valign;
    return o;
}

SDL_Texture* make_texture(SDL_Renderer* r, int w, int h, const u32* rgba) {
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) {
        LOG_WARN("font texture %dx%d: %s", w, h, SDL_GetError());
        return nullptr;
    }
    if (rgba) SDL_UpdateTexture(t, nullptr, rgba, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

} // namespace

// --- GameFont ----------------------------------------------------------------

TextLayout GameFont::layout(float x, float y, std::string_view utf8, const TextStyle& style) const {
    auto refs = map_unicode(utf8_decode(utf8), *data_, owner_->kanji(), owner_->jis());
    return layout_text(refs, *data_, owner_->kanji(), x, y, options(style));
}

SDL_FPoint GameFont::measure(std::string_view utf8, const TextStyle& style) const {
    TextLayout lay = layout(0, 0, utf8, style);
    return {lay.width, lay.height};
}

TextLayout GameFont::layout_game(float x, float y, ByteSpan game_text, const TextStyle& style) const {
    return layout_text(map_game_text(game_text), *data_, owner_->kanji(), x, y, options(style));
}

SDL_FPoint GameFont::measure_game(ByteSpan game_text, const TextStyle& style) const {
    TextLayout lay = layout_game(0, 0, game_text, style);
    return {lay.width, lay.height};
}

SDL_FRect GameFont::draw(float x, float y, std::string_view utf8, const TextStyle& style) {
    return render(layout(x, y, utf8, style), style);
}

SDL_FRect GameFont::draw_game(float x, float y, ByteSpan game_text, const TextStyle& style) {
    return render(layout_game(x, y, game_text, style), style);
}

SDL_FRect GameFont::render(const TextLayout& lay, const TextStyle& style) {
    SDL_Renderer* r = owner_->renderer();
    size_t v = size_t(style.variant) % kFontVariants;
    SDL_Texture* atlas = atlas_[v];
    if (atlas) {
        SDL_SetTextureColorMod(atlas, style.color.r, style.color.g, style.color.b);
        SDL_SetTextureAlphaMod(atlas, style.color.a);
    }
    for (const GlyphQuad& q : lay.quads) {
        SDL_FRect dst{q.x, q.y, q.w, q.h};
        if (q.kanji) {
            SDL_FRect src;
            SDL_Texture* t = owner_->kanji_texture(q.code, style.variant, src);
            if (!t) continue;
            SDL_SetTextureColorMod(t, style.color.r, style.color.g, style.color.b);
            SDL_SetTextureAlphaMod(t, style.color.a);
            SDL_RenderTexture(r, t, &src, &dst);
        } else if (atlas) {
            SDL_FRect src{float(q.sx), float(q.sy), float(q.sw), float(q.sh)};
            SDL_RenderTexture(r, atlas, &src, &dst);
        }
    }
    return {lay.x, lay.y, lay.width, lay.height};
}

// --- GameFonts ---------------------------------------------------------------

GameFonts::GameFonts() = default;
GameFonts::~GameFonts() { unload(); }

void GameFonts::unload() {
    for (auto& f : fonts_)
        for (SDL_Texture*& t : f->atlas_)
            if (t) SDL_DestroyTexture(t);
    fonts_.clear();
    files_.clear();
    for (int v = 0; v < kFontVariants; ++v) {
        for (KanjiPage& p : kanji_pages_[v])
            if (p.tex) SDL_DestroyTexture(p.tex);
        kanji_pages_[v].clear();
        kanji_slots_[v].clear();
    }
    kanji_.reset();
    jis_ = JisTable();
    renderer_ = nullptr;
}

bool GameFonts::load(const Vfs& vfs, SDL_Renderer* renderer) {
    unload();
    renderer_ = renderer;
    for (const char* path : {"font/system.fnt", "font/menu.fnt"}) {
        auto data = vfs.read(path);
        if (!data) {
            LOG_WARN("fonts: %s missing", path);
            continue;
        }
        try {
            files_.push_back(FontFile::parse(*data));
        } catch (const std::exception& e) {
            LOG_WARN("fonts: %s: %s", path, e.what());
        }
    }
    for (FontFile& file : files_)
        for (const LatinFont& lf : file.fonts) {
            auto f = std::unique_ptr<GameFont>(new GameFont(this, &lf));
            for (int v = 0; v < kFontVariants; ++v) {
                const Tex1::Image& img = lf.atlas[v];
                if (img.width && img.height)
                    f->atlas_[v] = make_texture(renderer, int(img.width), int(img.height), img.rgba.data());
            }
            fonts_.push_back(std::move(f));
        }
    try {
        if (auto d = vfs.read("font/jis2uni.dat")) jis_ = JisTable::parse(*d);
        if (auto d = vfs.read("font/system.kf")) kanji_ = std::make_unique<KanjiFont>(KanjiFont::parse(*d));
    } catch (const std::exception& e) {
        LOG_WARN("fonts: kanji support unavailable: %s", e.what());
    }
    LOG_INFO("fonts: %zu fonts, %zu kanji, %zu JIS codes", fonts_.size(), kanji_ ? kanji_->glyph_count() : 0,
             jis_.size());
    return loaded();
}

GameFont* GameFonts::get(std::string_view name) {
    for (auto& f : fonts_)
        if (f->name() == name) return f.get();
    return nullptr;
}

GameFont* GameFonts::resolve(std::string_view spec, TextStyle& style) {
    size_t end = spec.find_first_of("#*,");
    GameFont* f = get(spec.substr(0, end));
    if (!f) f = default_font();
    if (!f && !fonts_.empty()) f = fonts_.front().get();
    auto number = [&](size_t& i) {
        int v = 0;
        while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') v = v * 10 + (spec[i++] - '0');
        return v;
    };
    for (size_t i = end; i < spec.size();) {
        char c = spec[i++];
        if (c == '#' && i < spec.size()) {
            int v = spec[i++] - '0';
            style.variant = FontVariant(v >= 0 && v < kFontVariants ? v : 0);
        } else if (c == '*') {
            int x = number(i), y = x;
            if (i < spec.size() && spec[i] == ',') {
                ++i;
                y = number(i);
            }
            style.scale_x = float(x) * 0.01f;
            style.scale_y = float(y) * 0.01f;
        }
    }
    return f;
}

std::vector<std::string> GameFonts::names() const {
    std::vector<std::string> out;
    for (auto& f : fonts_) out.push_back(f->name());
    return out;
}

SDL_Texture* GameFonts::kanji_texture(u16 jis, FontVariant variant, SDL_FRect& out) {
    if (!kanji_ || !renderer_) return nullptr;
    int v = int(variant) % kFontVariants;
    int cw = int(kanji_->cell_width()), ch = int(kanji_->cell_height());
    // One pixel of padding around each cell keeps linear filtering clean.
    int pw = cw + 2, ph = ch + 2;
    int cols = kKanjiPageSize / pw, per_page = cols * (kKanjiPageSize / ph);
    auto it = kanji_slots_[v].find(jis);
    int page, slot;
    if (it != kanji_slots_[v].end()) {
        std::tie(page, slot) = it->second;
    } else {
        std::vector<u32> cell(size_t(cw * ch));
        if (!kanji_->render(jis, variant, cell.data())) return nullptr;
        auto& pages = kanji_pages_[v];
        if (pages.empty() || pages.back().used >= per_page) {
            KanjiPage p;
            std::vector<u32> clear(size_t(kKanjiPageSize * kKanjiPageSize), 0);
            p.tex = make_texture(renderer_, kKanjiPageSize, kKanjiPageSize, clear.data());
            if (!p.tex) return nullptr;
            pages.push_back(p);
        }
        page = int(pages.size()) - 1;
        slot = pages.back().used++;
        SDL_Rect dst{(slot % cols) * pw + 1, (slot / cols) * ph + 1, cw, ch};
        SDL_UpdateTexture(pages.back().tex, &dst, cell.data(), cw * 4);
        kanji_slots_[v][jis] = {page, slot};
    }
    out = {float((slot % cols) * pw + 1), float((slot / cols) * ph + 1), float(cw), float(ch)};
    return kanji_pages_[v][size_t(page)].tex;
}

} // namespace gt3r::text
