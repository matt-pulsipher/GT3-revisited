#pragma once

// Draws a MenuScene page with SDL_Renderer. Works with any renderer,
// including SDL's software renderer (used by gt3tool to write PNGs).

#include "menu/menu_scene.h"

#include <SDL3/SDL_render.h>

#include <functional>
#include <memory>
#include <map>
#include <vector>

namespace gt3r::menu {

// Text is drawn through a replaceable hook so the game font (text
// workstream) can take over from the debug font without touching the
// renderer. `box` is the element box in target pixels, `scale` the target
// pixels per menu unit (x, y).
struct TextDrawRequest {
    const TextStyle* style = nullptr;
    SDL_FRect box{};
    float scale_x = 1, scale_y = 1;
    SDL_FColor color{};
    bool font_inset = false; // ElementDraw::font_inset
};
using TextDrawer = std::function<void(SDL_Renderer*, const TextDrawRequest&)>;

// The original draws left / right aligned pmb text half its font's size in from the box
// edge (ElementDraw::font_inset, the exact rule); this fixed inset, measured on references,
// stands in for it where it was applied first. apply_text_inset() adds it to a draw list
// (text_dx); used by the race quick menus and the name entry so far.
constexpr float kTextInset = 12.0f;
struct ElementDraw;
void apply_text_inset(std::vector<ElementDraw>& list);

// Default: SDL's 8x8 debug font scaled to the font's pixel size.
void draw_text_debug_font(SDL_Renderer* r, const TextDrawRequest& req);

struct RenderOptions {
    bool outlines = false;       // frame every element (debug)
    bool widget_labels = true;   // frame widgets and print their class name
    bool images = true;
    bool text = true;
};

// One element to draw with an animation transform (used by the interactive
// menu runtime, src/menu/flow). Offsets are in menu units (y up); scale is
// about the element's centre; alpha multiplies every colour's alpha.
struct ElementDraw {
    enum class Kind : u8 {
        Element, // the element's own parts
        AddQuad, // additive grey quad (level `level`) over the box (focus without a highlight)
        Glow,    // additive white strip over the box: centre alpha `level`, edges `level2` (decide effect)
        Fill,    // flat colour `fill` (alpha x `alpha`) over the box (full-screen fades)
        Arc,     // ring segment in `fill`: ellipse = the box (outer radii w/2, h/2), angles
                 // `level`..`level2` degrees (counter-clockwise from +x, screen up), inner
                 // radii = outer x (1 - `arc_thickness`) (the CircleGauge primitive)
        Quad,    // flat colour `fill` (alpha x `alpha`) over the quadrilateral `points` (menu units, y up, in
                 // order round the edge; the gear chart's lines)
    };
    const MenuScene* scene = nullptr; // owner of the element's images; nullptr = set_scene()'s
    const Element* element = nullptr; // may be nullptr for AddQuad/Glow/Fill with has_box
    bool widget = false;
    float alpha = 1, dx = 0, dy = 0, scale = 1;
    float brightness = 1; // multiplies rgb (0.5 = the original's disabled look)
    SDL_FColor tint{1, 1, 1, 1}; // multiplies the parts' rgb per channel (code-coloured icons)
    // Per-corner alpha multipliers (top-left, top-right, bottom-right, bottom-left), e.g.
    // the arcade logos' diagonal cross-fade.
    float corner_alpha[4] = {1, 1, 1, 1};
    // Replaces the element's corner colours (in the layout's order, from the bottom-left; then
    // modulated as usual), e.g. the oil gauge's blend of two elements' colourings.
    bool has_colors = false;
    SDL_FColor colors[4]{};
    bool additive = false; // draw the parts additively (over-bright: a second pass of the same image)
    Kind kind = Kind::Element;
    float level = 0, level2 = 0;
    float arc_thickness = 0.3f;
    SDL_FColor fill{0, 0, 0, 1};
    SDL_FPoint points[4]{}; // Quad
    bool has_box = false; // replaces the element's box (menu units, x/y = bottom-left, y up)
    SDL_FRect box{};
    bool has_clip = false; // clip rectangle (menu units, same convention)
    SDL_FRect clip{};
    float z = 0; // sort key used by the menu runtime
    // Text effects: horizontal offset (menu units) clipped to the element box
    // (marquee), and a byte limit (typewriter; -1 = whole text).
    float text_dx = 0;
    bool text_clip = false;
    int text_bytes = -1;
    // Left / right aligned text sits half its font's size in from the box's side, as the
    // original's menu items anchor their text (menus §5.7).
    bool font_inset = false;
    // Replaces the element's text (game text encoding), e.g. a widget filled
    // by game code (credits, car names). Empty = the element's own text.
    std::shared_ptr<const std::string> text;
    // Skip the element's own image / colour parts (widget callbacks and text still run),
    // e.g. a marker widget whose place game code fills.
    bool hide_parts = false;
    // Replaces the text colour (alpha still follows `alpha`), e.g. the player's row.
    bool has_text_color = false;
    SDL_FColor text_color{1, 1, 1, 1};
    // Call the widget overlay drawer for this element even though it is not a
    // class widget (code-drawn content placed on an item, e.g. a 3D wheel).
    bool overlay = false;
};

class MenuRenderer {
public:
    explicit MenuRenderer(SDL_Renderer* r) : r_(r) {}
    ~MenuRenderer();
    MenuRenderer(const MenuRenderer&) = delete;
    MenuRenderer& operator=(const MenuRenderer&) = delete;

    // Uploads the scene's images. The scene must outlive draw calls.
    void set_scene(const MenuScene* scene);
    const MenuScene* scene() const { return scene_; }
    // While a widget drawer runs: the scene that owns the element being drawn (its images).
    const MenuScene* drawing_scene() const { return drawing_ ? drawing_ : scene_; }
    void set_text_drawer(TextDrawer d) { text_ = std::move(d); }
    // Called for widgets bound to game code (e.g. class "mpeg": a movie)
    // at their place in the draw order; `box` is in target pixels.
    using WidgetDrawer = std::function<void(SDL_Renderer*, const Element&, const SDL_FRect& box, float alpha)>;
    void set_widget_drawer(WidgetDrawer d) { widget_ = std::move(d); }
    // Same, called after the widget's own image/colour parts (before its text):
    // for content that goes on top of the widget's art (GT mode car renders).
    void set_widget_overlay_drawer(WidgetDrawer d) { widget_overlay_ = std::move(d); }

    // Draws `page` so that the 640x448 menu space maps onto `dst`.
    void draw(int page, const SceneOptions& so, const RenderOptions& ro, const SDL_FRect& dst);

    // Draws elements in the given order (back to front). Images of scenes
    // other than set_scene()'s are uploaded on first use and kept until
    // clear_cache() or destruction; the scenes must outlive that.
    void draw_elements(const std::vector<ElementDraw>& list, const RenderOptions& ro, const SDL_FRect& dst);
    void clear_cache();

private:
    void draw_element(const Element& e, bool widget, const RenderOptions& ro, const SDL_FRect& dst);
    void draw_element(const Element& e, const std::vector<SDL_Texture*>& textures, const ElementDraw& t,
                      const RenderOptions& ro, const SDL_FRect& dst);
    void draw_effect(const ElementDraw& t, const SDL_FRect& dst);
    const std::vector<SDL_Texture*>& textures_for(const MenuScene* scene);
    std::vector<SDL_Texture*> upload(const MenuScene* scene);
    void release();

    SDL_Renderer* r_;
    const MenuScene* scene_ = nullptr;
    const MenuScene* drawing_ = nullptr; // drawing_scene()
    std::vector<SDL_Texture*> textures_;
    std::map<const MenuScene*, std::vector<SDL_Texture*>> cache_;
    TextDrawer text_ = draw_text_debug_font;
    WidgetDrawer widget_;
    WidgetDrawer widget_overlay_;
};

// Renders a page into an RGBA8 image of the given size using a software
// renderer (no window needed). With `fonts_vfs`, text uses the game fonts.
std::vector<u32> render_page_rgba(const MenuScene& scene, int page, const SceneOptions& so, const RenderOptions& ro,
                                  int width, int height, const Vfs* fonts_vfs = nullptr);

} // namespace gt3r::menu
