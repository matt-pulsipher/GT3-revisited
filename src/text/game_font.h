#pragma once

// Game font rendering with SDL_Renderer. Public, stable API for screens.
//
//   text::GameFonts fonts;
//   fonts.load(*app.vfs(), app.renderer());          // once (cheap to keep around)
//   text::GameFont* f = fonts.get("sw_16");           // or fonts.default_font()
//   text::TextStyle st;
//   st.color = {255, 200, 40, 255};
//   st.align = text::HAlign::Center;
//   f->draw(320, 100, "Gran Turismo 3 — A-spec", st);  // UTF-8
//   f->draw_game(320, 130, raw_pmb_bytes, st);         // game-encoded bytes
//   SDL_FPoint size = f->measure("Lap 1/5", st);
//
// Coordinates are in renderer (logical) pixels; font pixels are multiplied
// by `scale`. Fonts in font/system.fnt: "sw_12" (the game's default),
// "sw_16", "sw_20", "sq_9"; in font/menu.fnt (digits/symbols only): "speed",
// "meter", "odo", "rank", "gearr". Japanese (and anything else outside the
// Latin fonts that has a JIS code) is drawn with the kanji font
// font/system.kf, scaled to the Latin font's kanji cell, like the game.
// Unknown characters draw as '?'.

#include "base/bytes.h"
#include "formats/text/charset.h"
#include "formats/text/fnt.h"
#include "formats/text/text_layout.h"

#include <SDL3/SDL_render.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r {
class Vfs;
}

namespace gt3r::text {

struct TextStyle {
    float scale = 1.0f;   // uniform scale; scale_x/scale_y multiply it
    float scale_x = 1.0f; // e.g. 0.5 to squeeze, like the game's per-axis scale
    float scale_y = 1.0f;
    SDL_Color color{255, 255, 255, 255}; // multiplies the glyph colours
    HAlign align = HAlign::Left;
    VAlign valign = VAlign::Top; // y is the top of the first line
    float spacing = 0.0f;        // extra pixels after each glyph (unscaled)
    float line_spacing = 0.0f;   // extra pixels between lines (unscaled)
    FontVariant variant = FontVariant::Outlined;
};

class GameFonts;

class GameFont {
public:
    const std::string& name() const { return data_->name; }
    // Unscaled metrics in font pixels.
    float line_height() const { return data_->line_height; }
    float ascent() const { return data_->ascent; }
    const LatinFont& data() const { return *data_; }

    // Size of the text block (widest line x total height), scaled.
    SDL_FPoint measure(std::string_view utf8, const TextStyle& style = {}) const;
    SDL_FPoint measure_game(ByteSpan game_text, const TextStyle& style = {}) const;
    SDL_FPoint measure_game(std::string_view game_text, const TextStyle& style = {}) const {
        return measure_game(ByteSpan(reinterpret_cast<const u8*>(game_text.data()), game_text.size()), style);
    }
    // Draws and returns the block's bounds.
    SDL_FRect draw(float x, float y, std::string_view utf8, const TextStyle& style = {});
    SDL_FRect draw_game(float x, float y, ByteSpan game_text, const TextStyle& style = {});
    SDL_FRect draw_game(float x, float y, std::string_view game_text, const TextStyle& style = {}) {
        return draw_game(x, y, ByteSpan(reinterpret_cast<const u8*>(game_text.data()), game_text.size()), style);
    }
    // Pure layout (no drawing), e.g. for caret positions or custom effects.
    TextLayout layout(float x, float y, std::string_view utf8, const TextStyle& style = {}) const;
    TextLayout layout_game(float x, float y, ByteSpan game_text, const TextStyle& style = {}) const;
    TextLayout layout_game(float x, float y, std::string_view game_text, const TextStyle& style = {}) const {
        return layout_game(x, y, ByteSpan(reinterpret_cast<const u8*>(game_text.data()), game_text.size()), style);
    }

    // The Latin glyph atlas texture (owned by the font).
    SDL_Texture* atlas(FontVariant v) const { return atlas_[size_t(v)]; }

private:
    friend class GameFonts;
    GameFont(GameFonts* owner, const LatinFont* data) : owner_(owner), data_(data) {}
    SDL_FRect render(const TextLayout& lay, const TextStyle& style);

    GameFonts* owner_;
    const LatinFont* data_;
    SDL_Texture* atlas_[kFontVariants] = {};
};

class GameFonts {
public:
    GameFonts();
    ~GameFonts();
    GameFonts(const GameFonts&) = delete;
    GameFonts& operator=(const GameFonts&) = delete;

    // Loads font/system.fnt, font/menu.fnt, font/system.kf and
    // font/jis2uni.dat. Returns false if no font could be loaded; missing
    // optional parts (kanji, JIS table) are logged and tolerated.
    bool load(const Vfs& vfs, SDL_Renderer* renderer);
    void unload();
    bool loaded() const { return !fonts_.empty(); }

    GameFont* get(std::string_view name);
    // Text styles without a font name ("") use sw_16 (matches the original's menus, M).
    GameFont* default_font() { return get("sw_16"); }
    // Resolves a game font spec "name[#V][*X[,Y]]" as used by menus and the
    // program: #V selects the palette variant (0 outlined, 1 plain), *X,Y
    // scales by X% horizontally and Y% vertically (Y defaults to X). Updates
    // style.variant / scale_x / scale_y; unknown names fall back to the
    // default font like the game. Returns nullptr only if nothing is loaded.
    GameFont* resolve(std::string_view spec, TextStyle& style);
    std::vector<std::string> names() const;

    const JisTable* jis() const { return jis_.empty() ? nullptr : &jis_; }
    const KanjiFont* kanji() const { return kanji_.get(); }
    // Game-encoded bytes -> UTF-8 using the disc's JIS table.
    std::string to_utf8(ByteSpan game_text) const { return game_text_to_utf8(game_text, jis()); }

    SDL_Renderer* renderer() const { return renderer_; }
    // Kanji glyph cache (one cell per JIS code); `out` receives the source rect.
    SDL_Texture* kanji_texture(u16 jis, FontVariant v, SDL_FRect& out);

private:
    struct KanjiPage {
        SDL_Texture* tex = nullptr;
        int used = 0;
    };
    SDL_Renderer* renderer_ = nullptr;
    std::vector<FontFile> files_;
    std::vector<std::unique_ptr<GameFont>> fonts_;
    JisTable jis_;
    std::unique_ptr<KanjiFont> kanji_;
    std::vector<KanjiPage> kanji_pages_[kFontVariants];
    std::map<u16, std::pair<int, int>> kanji_slots_[kFontVariants]; // jis -> (page, slot)
};

} // namespace gt3r::text
