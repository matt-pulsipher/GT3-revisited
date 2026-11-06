// Menu formats (.pmb, .mbl, IMGS) and the static menu scene.

#include "test_framework.h"

#include "formats/menu/imgs.h"
#include "formats/menu/mbl.h"
#include "formats/menu/pmb.h"
#include "menu/menu_canvas.h"
#include "menu/menu_renderer.h"
#include "menu/menu_scene.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstring>

using namespace gt3r;
using namespace gt3r::menu;

namespace {

void put32(Bytes& b, size_t o, u32 v) {
    if (b.size() < o + 4) b.resize(o + 4);
    std::memcpy(&b[o], &v, 4);
}
void putf(Bytes& b, size_t o, float f) {
    u32 v;
    std::memcpy(&v, &f, 4);
    put32(b, o, v);
}
void putstr(Bytes& b, size_t o, const char* s) {
    size_t n = std::strlen(s) + 1;
    if (b.size() < o + n) b.resize(o + n);
    std::memcpy(&b[o], s, n);
}

// Builds a minimal synthetic pmb: one page, no items, one layer with one
// element (colored rectangle with text). Not game data.
Bytes synthetic_pmb() {
    Bytes b(0x400, 0);
    put32(b, 0x00, 1);    // pages
    put32(b, 0x04, 0x10); // page table
    put32(b, 0x08, 0);    // images
    put32(b, 0x0C, 0x400);
    put32(b, 0x10, 0x20); // -> page
    // page @0x20: name[64], 0 items, 1 layer
    putstr(b, 0x20, "root");
    put32(b, 0x60, 0);
    put32(b, 0x64, 0x70);
    put32(b, 0x68, 1);
    put32(b, 0x6C, 0x70);
    put32(b, 0x70, 0x80); // -> layer
    // layer @0x80
    putstr(b, 0x80, "defaultLayer");
    put32(b, 0xC0, 1);
    put32(b, 0xC4, 0xD0);
    put32(b, 0xC8, 0);
    put32(b, 0xCC, 0x3FC);
    put32(b, 0xD0, 0x100); // -> element
    // element @0x100
    putstr(b, 0x100, "box");
    put32(b, 0x140, Element::kColor | Element::kText | (0x0C << 16));
    putf(b, 0x144, 0);
    putf(b, 0x148, 0);
    putf(b, 0x14C, 5);
    putf(b, 0x150, 640);
    putf(b, 0x154, 224); // bottom half of the screen (y grows upwards)
    put32(b, 0x164, 0x200); // colors
    put32(b, 0x168, 0x220); // text
    for (int i = 0; i < 4; ++i) put32(b, 0x200 + i * 4, 0xFF0000FF); // opaque red
    put32(b, 0x220, 1 | (4 << 16));
    put32(b, 0x224, 0x7FFFFFFF);
    put32(b, 0x228, 0x240);
    put32(b, 0x22C, 0x250);
    putstr(b, 0x240, "HELLO");
    putstr(b, 0x250, "sw_16#1*90");
    return b;
}

} // namespace

TEST(menu_pmb_synthetic) {
    Bytes b = synthetic_pmb();
    PmbFile f = PmbFile::parse(b);
    CHECK(f.name_size == 64);
    CHECK(f.pages.size() == 1);
    CHECK(f.pages[0].layers.size() == 1);
    const Element& e = f.pages[0].layers[0].elements.at(0);
    CHECK(e.name == "box");
    CHECK(e.colors && (*e.colors)[0].r == 0xFF && (*e.colors)[0].a == 0xFF);
    CHECK(e.text && e.text->text == "HELLO" && e.text->align == 4);
    CHECK(!e.image && e.alt.empty() && !e.widget_class);
    CHECK(e.h == 224.0f);
}

TEST(menu_font_spec) {
    FontSpec a = FontSpec::parse("sw_16#1*90");
    CHECK(a.name == "sw_16" && a.size == 16 && a.variant == 1);
    CHECK(std::fabs(a.sx - 0.9f) < 1e-6f && std::fabs(a.sy - 0.9f) < 1e-6f); // one value scales both axes
    CHECK(FontSpec::parse("sw_16#!1").variant == 0); // the game reads '!' (< '0') as look 0
    FontSpec b = FontSpec::parse("sw_16#1*65,75");
    CHECK(std::fabs(b.sy - 0.75f) < 1e-6f);
    FontSpec c = FontSpec::parse("sq_9");
    CHECK(c.size == 9 && c.sx == 1.0f);
}

TEST(menu_render_synthetic) {
    Bytes b = synthetic_pmb();
    MenuScene s = MenuScene::load(b, "synthetic");
    SceneOptions so;
    RenderOptions ro;
    ro.text = false;
    std::vector<u32> px = render_page_rgba(s, 0, so, ro, 64, 44);
    CHECK(px.size() == 64 * 44);
    if (px.size() == 64 * 44) {
        // Bottom half red, top half black (y up in menu space).
        const u8* top = reinterpret_cast<const u8*>(&px[5 * 64 + 10]);
        const u8* bottom = reinterpret_cast<const u8*>(&px[40 * 64 + 10]);
        CHECK(top[0] < 16);
        CHECK(bottom[0] > 240 && bottom[1] < 16);
    }
}

TEST(menu_disc_files) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Volume& vol = disc->volume();
    int pmb = 0, pmb_ok = 0, mbl = 0, mbl_ok = 0, imgs = 0, imgs_ok = 0;
    for (const auto& e : vol.entries()) {
        auto ends = [&](const char* s) {
            size_t n = std::strlen(s);
            return e.path.size() > n && e.path.compare(e.path.size() - n, n, s) == 0;
        };
        try {
            if (ends(".pmb")) {
                ++pmb;
                Bytes d = vol.read(e);
                if (d.empty() || PmbFile::is_pmb(d)) {
                    if (!d.empty()) PmbFile::parse(d);
                    ++pmb_ok;
                }
            } else if (ends(".mbl")) {
                ++mbl;
                MblFile::parse(vol.read(e));
                ++mbl_ok;
            } else if (ends(".imgs")) {
                ++imgs;
                Imgs::parse(vol.read(e));
                ++imgs_ok;
            }
        } catch (const std::exception& ex) {
            std::printf("  %s: %s\n", e.path.c_str(), ex.what());
        }
    }
    // 3 Italian .pmb files are headerless fragments and are not counted as pmb.
    CHECK(pmb == 963);
    CHECK(pmb_ok == 960);
    CHECK(mbl == 51 && mbl_ok == 51);
    CHECK(imgs == 23 && imgs_ok == 23);

    // Spot check: the US home screen.
    Bytes d = vol.read(*vol.find("menu/US/gt_mode2.pmb"));
    MenuScene s = MenuScene::load(d, "menu/US/gt_mode2.pmb");
    int hm = s.file().find_page("hm/hm");
    CHECK(hm >= 0);
    CHECK(s.image_failures() == 0);
    if (hm >= 0) CHECK(!s.draw_list(hm, SceneOptions{}).empty());

    MblFile flow = MblFile::parse(vol.read(*vol.find("menu/option.mbl")));
    CHECK(!flow.legacy);
    CHECK(flow.find_page("ghost/ghost") >= 0);
}

namespace {

// The target's pixel at (x, y) as 0xAABBGGRR.
u32 target_pixel(SDL_Renderer* r, int x, int y) {
    SDL_Surface* s = SDL_RenderReadPixels(r, nullptr);
    if (!s) return 0;
    SDL_Surface* rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!rgba) return 0;
    u32 v = 0;
    std::memcpy(&v, static_cast<const u8*>(rgba->pixels) + size_t(y) * rgba->pitch + size_t(x) * 4, 4);
    SDL_DestroySurface(rgba);
    return v;
}

bool close_rgb(u32 a, u32 b, int tolerance) {
    for (int shift = 0; shift < 24; shift += 8)
        if (std::abs(int(a >> shift & 255) - int(b >> shift & 255)) > tolerance) return false;
    return true;
}

void fill(SDL_Renderer* r, SDL_BlendMode mode, const SDL_FRect& box, u8 cr, u8 cg, u8 cb, u8 ca) {
    SDL_SetRenderDrawBlendMode(r, mode);
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_RenderFillRect(r, &box);
}

// The art before the model, the model's stand-in, the art after it (elements blend or add).
void art_before(SDL_Renderer* r) {
    SDL_SetRenderDrawColor(r, 30, 60, 90, 255);
    SDL_RenderClear(r);
    fill(r, SDL_BLENDMODE_BLEND, {200, 0, 200, 448}, 200, 40, 40, 128);
}
void art_after(SDL_Renderer* r) {
    fill(r, SDL_BLENDMODE_BLEND, {0, 100, 640, 100}, 20, 200, 60, 96);
    fill(r, SDL_BLENDMODE_ADD, {0, 300, 640, 100}, 40, 50, 60, 128);
}

} // namespace

// The page around a model: art over the larger picture comes out as on one flat 640 x 448 page.
TEST(menu_canvas_art_matches_a_flat_page) {
    SDL_Surface* surface = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* r = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    CHECK(r != nullptr);
    if (!r) {
        SDL_DestroySurface(surface);
        return;
    }
    const auto target = [&] {
        return SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 640, 448);
    };
    SDL_Texture* flat = target();
    SDL_Texture* page = target();
    CHECK(flat && page);
    const SDL_FRect model{100, 50, 400, 300};

    SDL_SetRenderTarget(r, flat);
    art_before(r);
    fill(r, SDL_BLENDMODE_NONE, model, 250, 220, 10, 255);
    art_after(r);

    {
        MenuCanvas canvas;
        SDL_SetRenderTarget(r, page);
        canvas.begin(r, page, 1280, 896);
        CHECK(canvas.on());
        CHECK(canvas.scale_x() == 2.0f && canvas.scale_y() == 2.0f);
        art_before(r);
        canvas.model_begin(r);
        fill(r, SDL_BLENDMODE_NONE, canvas.to_picture(model), 250, 220, 10, 255);
        canvas.model_end(r);
        art_after(r);
        canvas.end(r);
        SDL_Texture* shown = canvas.shown(page);
        CHECK(shown != page);

        // Inside each overlap of art and model, away from the edges the scaling softens.
        const int spots[][2] = {{50, 20}, {50, 150}, {50, 350}, {150, 20}, {150, 150}, {150, 250}, {150, 350},
                                {300, 150}, {300, 250}, {300, 350}, {450, 20}, {450, 150}, {450, 350}, {600, 420}};
        for (const auto& [x, y] : spots) {
            SDL_SetRenderTarget(r, flat);
            const u32 want = target_pixel(r, x, y);
            SDL_SetRenderTarget(r, shown);
            CHECK(close_rgb(target_pixel(r, x * 2, y * 2), want, 2));
        }

        // No model: the page itself, as on a screen no larger than the page.
        SDL_SetRenderTarget(r, page);
        canvas.begin(r, page, 1280, 896);
        canvas.end(r);
        CHECK(canvas.shown(page) == page);
        canvas.begin(r, page, 640, 448);
        CHECK(!canvas.on());
        canvas.end(r);
        CHECK(canvas.shown(page) == page);
    }
    SDL_DestroyTexture(flat);
    SDL_DestroyTexture(page);
    SDL_DestroyRenderer(r);
    SDL_DestroySurface(surface);
}
