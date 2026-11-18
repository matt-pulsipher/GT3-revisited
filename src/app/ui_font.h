#pragma once

#include "base/bytes.h"

#include <SDL3/SDL_render.h>

#include <memory>
#include <string>
#include <string_view>

namespace gt3r {

// Proportional UI text: Barlow (SIL OFL 1.1), compiled into the executable
// and rasterised with stb_truetype into glyph atlases at the *physical* pixel
// size, so text stays sharp at any UI scale (HiDPI, phones). Layout is in UI
// (render) units; glyph origins snap to whole output pixels.
enum class FontFace : u8 {
    Body,   // Barlow Semi Condensed Medium
    Title,  // Barlow Condensed SemiBold
    Prompt, // PromptFont (SIL OFL 1.1): controller and keyboard button glyphs, see prompt_glyph()
};

namespace ui_font {

// Text drawn with Body or Title shows PromptFont's glyph for code point `cp` (glyphs.json in its
// release, e.g. U+21E3 the Cross button) wherever it holds prompt_glyph(cp): code point
// kPromptBase + cp, in the Supplementary Private Use Area, so a prompt never collides with text
// that uses the same code point (PromptFont puts its glyphs on arrows and maths signs).
constexpr u32 kPromptBase = 0xF0000;
std::string prompt_glyph(u32 cp);

// False if the embedded font could not be parsed (callers fall back to SDL's
// debug font).
bool available();
// False if PromptFont could not be parsed (prompt_glyph()s then draw as '?').
bool prompts_available();
// Advance width of `s` (UTF-8) at `em` units per em, including kerning.
float width(FontFace f, float em, std::string_view s);
// Height of a capital letter at `em`.
float cap_height(FontFace f, float em);

} // namespace ui_font

class UiFontRenderer {
public:
    explicit UiFontRenderer(SDL_Renderer* r);
    ~UiFontRenderer();
    UiFontRenderer(const UiFontRenderer&) = delete;
    UiFontRenderer& operator=(const UiFontRenderer&) = delete;

    // Draws `s` with its left edge at x and baseline at `baseline` (render
    // coordinates). Returns false if the font is unavailable.
    bool draw(FontFace f, float em, float x, float baseline, std::string_view s, SDL_FColor c);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gt3r
