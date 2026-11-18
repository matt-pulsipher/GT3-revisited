// Button prompts on our screens: "{Cross}" tokens, PromptFont glyphs, the controller families.

#include "test_framework.h"

#include "app/button_prompts.h"
#include "app/ui.h"
#include "app/ui_font.h"

using namespace gt3r;

namespace {

// The table before App sets one: the buttons' names.
PromptTable names() {
    PromptTable t;
    for (size_t i = 0; i < kPromptCount; ++i) t.text[i] = std::string(prompt_token(Prompt(i)));
    return t;
}

std::string g(u32 cp) { return ui_font::prompt_glyph(cp); }

} // namespace

TEST(button_prompts_expand) {
    Ui::set_prompts(names());
    CHECK(Ui::expand_prompts("{Cross}: choose   {Up/Down}: select") == "Cross: choose   Up/Down: select");
    CHECK(Ui::expand_prompts("no prompts") == "no prompts");
    CHECK(Ui::expand_prompts("{video} gamma {Triangle}") == "{video} gamma Triangle"); // other braces stay
    CHECK(Ui::expand_prompts("{L1}/{R1} {Cross") == "L1/R1 {Cross");

    PromptTable t = names();
    t.text[size_t(Prompt::Cross)] = g(0x21E3);
    t.text[size_t(Prompt::Select)] = g(0x21F7);
    Ui::set_prompts(t);
    CHECK(Ui::expand_prompts("({Cross}: car page)") == "(" + g(0x21E3) + ": car page)");
    // A key the screen reads itself: the button's prompt, the key while the prompts show keys.
    CHECK(Ui::expand_prompts("{Select|Delete}: clear") == g(0x21F7) + ": clear");
    CHECK(!Ui::prompts_keyboard());
    t.keyboard = true;
    Ui::set_prompts(t);
    CHECK(Ui::prompts_keyboard());
    CHECK(Ui::expand_prompts("{Select|Delete}: clear") == "Delete: clear"); // no readable keycap: its name
    CHECK(Ui::expand_prompts("{Cross|Return}: OK") == g(0x242E) + ": OK");
    Ui::set_prompts(names());
}

TEST(button_prompts_glyph_text) {
    if (!ui_font::available() || !ui_font::prompts_available()) SKIP("UI fonts not embedded");
    // A glyph is one code point in the Supplementary Private Use Area, never PromptFont's own
    // (U+2212 is its M1 button and the minus sign).
    CHECK(g(0x21E3) == "\xF3\xB2\x87\xA3");
    const float em = Ui::kBodyEm;
    const float with = ui_font::width(FontFace::Body, em, "A" + g(0x21E3) + "B");
    const float without = ui_font::width(FontFace::Body, em, "AB");
    CHECK(with > without + em * 0.9f && with < without + em * 1.3f); // about an em wide
    // Shoulder buttons (flat bars) are drawn larger, so wider than a face button.
    CHECK(ui_font::width(FontFace::Body, em, g(0x2198)) > ui_font::width(FontFace::Body, em, g(0x21D3)) * 1.2f);
    CHECK(ui_font::width(FontFace::Body, em, "\xE2\x88\x92") < em * 0.8f); // U+2212 stays Barlow's minus

    // Fitting keeps a glyph whole.
    PromptTable t = names();
    t.text[size_t(Prompt::Triangle)] = g(0x21E1);
    Ui::set_prompts(t);
    const std::string hint = "{Triangle}: back to the lobby";
    CHECK(Ui::clip(hint, Ui::text_width(hint)) == g(0x21E1) + ": back to the lobby");
    const std::string cut = Ui::clip(hint, Ui::text_width("{Triangle}: back"));
    CHECK(cut.rfind(g(0x21E1), 0) == 0 && cut.size() > 3 && cut.substr(cut.size() - 3) == "...");
    for (const std::string& line : Ui::wrap(hint, Ui::text_width("{Triangle}: back")))
        CHECK(line.find('{') == std::string::npos);
    Ui::set_prompts(names());
}

TEST(button_prompts_families) {
    CHECK(pad_family(SDL_GAMEPAD_TYPE_PS5) == PadFamily::PlayStation);
    CHECK(pad_family(SDL_GAMEPAD_TYPE_XBOXONE) == PadFamily::Xbox);
    CHECK(pad_family(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO) == PadFamily::Nintendo);
    CHECK(pad_family(SDL_GAMEPAD_TYPE_STANDARD) == PadFamily::Generic);

    // Face buttons by the label the pad reports; unlabelled ones by position.
    const auto face = [](PadFamily f, SDL_GamepadButton b, SDL_GamepadButtonLabel l) {
        return pad_button_glyph(f, SDL_GAMEPAD_TYPE_UNKNOWN, b, l);
    };
    CHECK(face(PadFamily::PlayStation, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_LABEL_CROSS) == g(0x21E3));
    CHECK(face(PadFamily::Xbox, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_LABEL_A) == g(0x21D3));
    CHECK(face(PadFamily::Nintendo, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_LABEL_B) == g(0x21D2));
    CHECK(face(PadFamily::Generic, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN) == g(0x21A6));

    // Shoulders, triggers, Start and Select by the family's names.
    const auto btn = [](PadFamily f, SDL_GamepadType t, SDL_GamepadButton b) {
        return pad_button_glyph(f, t, b, SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN);
    };
    CHECK(btn(PadFamily::PlayStation, SDL_GAMEPAD_TYPE_PS3, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) == g(0x21B0));
    CHECK(btn(PadFamily::Xbox, SDL_GAMEPAD_TYPE_XBOXONE, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) == g(0x2198));
    CHECK(btn(PadFamily::Nintendo, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) == g(0x219D));
    CHECK(pad_axis_glyph(PadFamily::PlayStation, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, InputSource::Half::Full) == g(0x21B3));
    CHECK(pad_axis_glyph(PadFamily::Nintendo, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, InputSource::Half::Full) == g(0x219A));
    CHECK(pad_axis_glyph(PadFamily::Xbox, SDL_GAMEPAD_AXIS_LEFTY, InputSource::Half::Negative) == g(0x21BE));
    CHECK(btn(PadFamily::PlayStation, SDL_GAMEPAD_TYPE_PS5, SDL_GAMEPAD_BUTTON_START) == "Options");
    CHECK(btn(PadFamily::PlayStation, SDL_GAMEPAD_TYPE_PS4, SDL_GAMEPAD_BUTTON_BACK) == "Share");
    CHECK(btn(PadFamily::Xbox, SDL_GAMEPAD_TYPE_XBOXONE, SDL_GAMEPAD_BUTTON_START) == g(0x21FB));
    CHECK(btn(PadFamily::Nintendo, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, SDL_GAMEPAD_BUTTON_BACK) == g(0x21FD));
    CHECK(btn(PadFamily::Xbox, SDL_GAMEPAD_TYPE_XBOXONE, SDL_GAMEPAD_BUTTON_DPAD_UP) == g(0x227B));
    CHECK(btn(PadFamily::Generic, SDL_GAMEPAD_TYPE_STANDARD, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1).empty()); // by name

    // Keys: arrows, Enter and Esc as keycaps; keys with a word on them by name.
    CHECK(key_glyph(SDL_SCANCODE_RETURN) == g(0x242E));
    CHECK(key_glyph(SDL_SCANCODE_ESCAPE) == g(0x242F));
    CHECK(key_glyph(SDL_SCANCODE_UP) == g(0x23F6));
    CHECK(key_glyph(SDL_SCANCODE_SPACE).empty());
    CHECK(key_glyph(SDL_SCANCODE_TAB).empty());

    CHECK(ps2_glyph(PadButton::Triangle) == g(0x21E1));
    CHECK(ps2_glyph(PadButton::Start) == g(0x21F8));
}
