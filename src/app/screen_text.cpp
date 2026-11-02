// Text & fonts: browses the string tables with the game's own font renderer
// and shows the font atlases and the kanji font.

#include "app/app.h"
#include "app/screen_registry.h"
#include "formats/text/mltx.h"
#include "text/game_font.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace gt3r {

namespace {

using namespace gt3r::text;

enum class Mode { Strings, Fonts, Kanji, Count };
constexpr const char* kModeNames[] = {"STRINGS", "FONTS", "KANJI"};
constexpr const char* kPreviewFonts[] = {"sw_12", "sw_16", "sw_20", "sq_9"};
constexpr const char* kPangram =
    "The quick brown fox jumps over the lazy dog 0123456789\n"
    "Déjà vu! Größe, señor, ça, Œuvre, Łódź, € 1.234,56 ™\n"
    "グランツーリスモ３ 第1戦 ブレーキバランスコントローラ";

class TextScreen : public Screen {
public:
    void update(App& app, double) override {
        if (!loaded_) load(app);
        const MenuInput& m = app.menu();
        if (m.pressed(PadButton::Triangle)) {
            app.pop();
            return;
        }
        if (m.pressed(PadButton::Square)) mode_ = Mode((int(mode_) + 1) % int(Mode::Count));
        int page = m.pressed(PadButton::L1) ? -kRows : m.pressed(PadButton::R1) ? kRows : 0;
        switch (mode_) {
        case Mode::Strings:
            if (table_.size()) sel_ = std::clamp(sel_ + m.vertical() + page, 0, int(table_.size()) - 1);
            if (m.horizontal()) preview_font_ = (preview_font_ + m.horizontal() + 4) % 4;
            if (m.pressed(PadButton::Cross)) lang_ = (lang_ + 1) % kLanguageCount;
            break;
        case Mode::Fonts: {
            int n = int(fonts_.names().size());
            if (n && m.horizontal()) font_sel_ = (font_sel_ + m.horizontal() + n) % n;
            if (m.vertical() || m.pressed(PadButton::Cross)) variant_ ^= 1;
            break;
        }
        case Mode::Kanji: {
            int rows = kanji_codes_.empty() ? 0 : int(kanji_codes_.size() + kKanjiCols - 1) / kKanjiCols;
            kanji_row_ = std::clamp(kanji_row_ + m.vertical() + (page ? page / 2 : 0), 0, std::max(0, rows - 1));
            if (m.pressed(PadButton::Cross)) variant_ ^= 1;
            break;
        }
        default: break;
        }
    }

    void render(App& app, Ui& ui) override {
        ui.header("TEXT & FONTS", kModeNames[int(mode_)]);
        if (!error_.empty()) {
            ui.text_center(150, error_, colors::kBad);
            ui.footer("Triangle: back");
            return;
        }
        switch (mode_) {
        case Mode::Strings: render_strings(ui); break;
        case Mode::Fonts: render_fonts(ui); break;
        case Mode::Kanji: render_kanji(ui); break;
        default: break;
        }
        (void)app;
    }

private:
    static constexpr int kRows = 25;
    static constexpr int kKanjiCols = 24;

    void load(App& app) {
        loaded_ = true;
        if (!fonts_.load(*app.vfs(), app.renderer())) error_ = "Could not load font/system.fnt";
        try {
            if (auto d = app.vfs()->read("text/realtime.dat")) table_ = StringTable::parse(*d);
            else error_ = "text/realtime.dat missing";
        } catch (const std::exception& e) {
            error_ = std::string("realtime.dat: ") + e.what();
        }
        if (const KanjiFont* k = fonts_.kanji()) kanji_codes_ = k->codes();
        lang_ = int(Language::US);
        // Debug aid for screenshots: GT3R_TEXT_MODE=0..2 picks the start page.
        if (const char* m = SDL_getenv("GT3R_TEXT_MODE")) mode_ = Mode(std::clamp(std::atoi(m), 0, int(Mode::Count) - 1));
    }

    void render_strings(Ui& ui) {
        int n = int(table_.size());
        int first = ui.list_window(n, sel_, kRows);
        for (int i = 0; i < kRows && first + i < n; ++i) {
            int idx = first + i;
            float y = 26 + i * 12.0f;
            if (idx == sel_) ui.fill(4, y - 2, 250, 12, colors::kHighlight);
            std::string key = table_.key(idx);
            if (key.size() > 30) key = key.substr(0, 14) + ".." + key.substr(key.size() - 14);
            bool empty = table_.raw(idx, Language(lang_)).empty();
            ui.text(8, y, key, empty ? colors::kDim : colors::kText);
        }
        if (n > kRows)
            ui.text_right(250, Ui::kHeight - 30, std::to_string(sel_ + 1) + "/" + std::to_string(n), colors::kDim);
        if (!n) return;

        // Preview: the selected entry in every language, with the game's font.
        float px = 262, py = 26, pw = Ui::kWidth - px - 6, ph = Ui::kHeight - 50 - py;
        ui.fill(px, py, pw, ph, colors::kPanel);
        GameFont* font = fonts_.get(kPreviewFonts[preview_font_]);
        ui.text(px + 6, py + 4, std::string("Font ") + kPreviewFonts[preview_font_], colors::kAccent);
        if (!font) return;
        TextStyle st;
        st.scale = 18.0f / font->line_height(); // ~18 px per line
        float y = py + 18;
        for (int l = 0; l < table_.language_count() && l < kLanguageCount; ++l) {
            const std::string& raw = table_.raw(sel_, Language(l));
            Color c = l == lang_ ? colors::kAccent : colors::kDim;
            ui.text(px + 6, y + 4, language_code(Language(l)), c);
            if (raw.empty()) {
                ui.text(px + 30, y + 4, "(built-in default)", colors::kDim);
                y += 20;
                continue;
            }
            st.color = l == lang_ ? SDL_Color{255, 220, 120, 255} : SDL_Color{255, 255, 255, 255};
            SDL_FRect r = font->draw_game(px + 30, y, raw, st);
            y += std::max(20.0f, r.h + 4);
        }
        // Selected language rendered large and as UTF-8 via the debug font.
        const std::string& raw = table_.raw(sel_, Language(lang_));
        ui.text(px + 6, py + ph - 26, std::string(language_name(Language(lang_))) + ", " +
                                          std::to_string(table_.filled(Language(lang_))) + "/" +
                                          std::to_string(table_.size()) + " filled", colors::kText);
        TextStyle big;
        big.scale = 0.8f;
        big.align = HAlign::Center;
        big.valign = VAlign::Middle;
        big.color = {120, 200, 255, 255};
        if (!raw.empty() && y < py + ph - 60) font->draw_game(px + pw / 2, (y + py + ph - 30) / 2, raw, big);
        ui.footer("Up/Down: key  Left/Right: font  Cross: language  Square: mode  Triangle: back Smeg");
    }

    void render_fonts(Ui& ui) {
        auto names = fonts_.names();
        if (names.empty()) return;
        font_sel_ = std::clamp(font_sel_, 0, int(names.size()) - 1);
        GameFont* f = fonts_.get(names[font_sel_]);
        const LatinFont& d = f->data();
        char info[160];
        std::snprintf(info, sizeof(info), "%s  codes %X-%X  line %u  ascent %d  kanji %ux%u  variant %d", d.name.c_str(),
                      d.first, d.last, d.line_height, d.ascent, d.kanji_width, d.kanji_height, variant_);
        ui.text(8, 26, info, colors::kAccent);
        // Atlas on the left, rotated into columns if tall.
        SDL_Texture* atlas = f->atlas(FontVariant(variant_));
        float area_h = Ui::kHeight - 50 - 40;
        if (atlas) {
            float tw, th;
            SDL_GetTextureSize(atlas, &tw, &th);
            SDL_SetTextureColorMod(atlas, 255, 255, 255); // the font tints its atlas when drawing
            SDL_SetTextureAlphaMod(atlas, 255);
            ui.fill(6, 38, 4 * 132 > 270 ? 270 : 4 * 132, area_h + 4, colors::kPanel);
            // Split the 128-wide atlas into up to 2 columns.
            int cols = th > area_h ? 2 : 1;
            float slice = std::ceil(th / cols);
            float s = std::min(1.0f, area_h / slice);
            for (int c = 0; c < cols; ++c) {
                SDL_FRect src{0, c * slice, tw, std::min(slice, th - c * slice)};
                SDL_FRect dst{8 + c * (tw * s + 4), 40, tw * s, src.h * s};
                SDL_RenderTexture(ui.renderer(), atlas, &src, &dst);
            }
        }
        TextStyle st;
        st.variant = FontVariant(variant_);
        st.scale = std::min(1.0f, 16.0f / d.line_height);
        f->draw(280, 42, d.first > 0x41 || d.last < 0x7A ? sample_digits(d) : kPangram, st);
        st.color = {255, 200, 40, 255};
        st.scale *= 0.75f;
        f->draw(280, 160, d.first > 0x41 || d.last < 0x7A ? sample_digits(d) : "GT3 A-spec  1'23.456  Lap 3/5", st);
        ui.footer("Left/Right: font  Up/Down: variant  Square: mode  Triangle: back");
    }

    static std::string sample_digits(const LatinFont& d) {
        std::string s;
        for (u32 c = d.first; c <= d.last && s.size() < 40; ++c)
            if (c < 0x80 && d.has(c)) s.push_back(char(c));
        return s;
    }

    void render_kanji(Ui& ui) {
        const KanjiFont* k = fonts_.kanji();
        if (!k) {
            ui.text_center(150, "No kanji font", colors::kBad);
            return;
        }
        char info[128];
        std::snprintf(info, sizeof(info), "%s: %zu glyphs, %zu codes, variant %d", k->name().c_str(), k->glyph_count(),
                      kanji_codes_.size(), variant_);
        ui.text(8, 26, info, colors::kAccent);
        const float cell = 24;
        int vis_rows = int((Ui::kHeight - 50 - 40) / cell);
        for (int r = 0; r < vis_rows; ++r)
            for (int c = 0; c < kKanjiCols; ++c) {
                size_t i = size_t(kanji_row_ + r) * kKanjiCols + c;
                if (i >= kanji_codes_.size()) break;
                SDL_FRect src;
                SDL_Texture* t = fonts_.kanji_texture(kanji_codes_[i], FontVariant(variant_), src);
                if (!t) continue;
                SDL_SetTextureColorMod(t, 255, 255, 255);
                SDL_SetTextureAlphaMod(t, 255);
                SDL_FRect dst{8 + c * cell, 40 + r * cell, 22, 22};
                SDL_RenderTexture(ui.renderer(), t, &src, &dst);
                if (c == 0) {
                    char code[8];
                    std::snprintf(code, sizeof(code), "%04X", kanji_codes_[i]);
                    ui.text(8 + kKanjiCols * cell + 8, 40 + r * cell + 7, code, colors::kDim);
                }
            }
        ui.footer("Up/Down: scroll  L1/R1: page  Cross: variant  Square: mode  Triangle: back");
    }

    bool loaded_ = false;
    std::string error_;
    GameFonts fonts_;
    StringTable table_;
    std::vector<u16> kanji_codes_;
    Mode mode_ = Mode::Strings;
    int sel_ = 0, lang_ = 1, preview_font_ = 1, font_sel_ = 0, variant_ = 0, kanji_row_ = 0;
};

} // namespace

GT3R_REGISTER_SCREEN(text, "text", "Text & fonts",
                     "Browse the string tables with the game's fonts; font atlases and the kanji font.", 70,
                     [] { return std::make_unique<TextScreen>(); });

} // namespace gt3r
