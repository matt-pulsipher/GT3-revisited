// Menu asset commands: dump .pmb/.mbl/IMGS structures, render menu pages and
// image sets to PNG, and survey every menu file on the disc.

#include "tool.h"

#include "formats/menu/imgs.h"
#include "formats/menu/mbl.h"
#include "formats/menu/pmb.h"
#include "formats/png_writer.h"
#include "menu/menu_renderer.h"
#include "menu/menu_scene.h"
#include "vfs/vfs.h"

#include <cstdio>
#include <map>

namespace gt3r::tool {

namespace {

using namespace gt3r::menu;

bool ends_with(const std::string& s, std::string_view suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

std::string hex_rgba(const Rgba& c) {
    char b[16];
    std::snprintf(b, sizeof(b), "%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
    return b;
}

void print_element(const Element& e, int indent, const char* tag) {
    std::printf("%*s%s \"%s\" @%zx type=%04x flags=%04x x=%g y=%g z=%g w=%g h=%g", indent, "", tag, e.name.c_str(),
                e.offset, e.type, e.flags, e.x, e.y, e.z, e.w, e.h);
    if (e.widget_class) std::printf(" class=\"%s\"", e.widget_class->c_str());
    std::printf("\n");
    if (e.image) {
        const auto& u = e.image->uv;
        std::printf("%*s  image %u uv (%g,%g) (%g,%g) (%g,%g) (%g,%g)\n", indent, "", e.image->image, u[0], u[1], u[2],
                    u[3], u[4], u[5], u[6], u[7]);
    }
    if (e.colors) {
        const auto& c = *e.colors;
        std::printf("%*s  colors %s %s %s %s\n", indent, "", hex_rgba(c[0]).c_str(), hex_rgba(c[1]).c_str(),
                    hex_rgba(c[2]).c_str(), hex_rgba(c[3]).c_str());
    }
    if (e.text)
        std::printf("%*s  text align=%02x color=%s font=\"%s\" \"%s\"\n", indent, "", e.text->align,
                    hex_rgba(e.text->color).c_str(), e.text->font.c_str(), e.text->text.c_str());
    for (const Element& a : e.alt) print_element(a, indent + 4, "alt");
}

int cmd_pmb(Disc& disc, const Args& a) {
    Bytes data = disc.volume().read(need(disc.volume(), a[0]));
    PmbFile f = PmbFile::parse(data);
    std::printf("%zu page(s), %zu image(s), name size %u\n", f.pages.size(), f.images.size(), f.name_size);
    for (size_t i = 0; i < f.images.size(); ++i)
        std::printf("  image %zu: @%x %u bytes%s\n", i, f.images[i].offset, f.images[i].size,
                    f.images[i].gzip ? " (gzip)" : "");
    for (size_t p = 0; p < f.pages.size(); ++p) {
        const Page& pg = f.pages[p];
        std::printf("page %zu \"%s\": %zu item(s), %zu layer(s)\n", p, pg.name.c_str(), pg.items.size(),
                    pg.layers.size());
        for (const Element& e : pg.items) print_element(e, 2, "item");
        for (const Layer& l : pg.layers) {
            std::printf("  layer \"%s\": %zu element(s), %zu widget(s)\n", l.name.c_str(), l.elements.size(),
                        l.widgets.size());
            for (const Element& e : l.elements) print_element(e, 4, "elem");
            for (const Element& e : l.widgets) print_element(e, 4, "widget");
        }
    }
    return 0;
}

void print_item(const MblItem& it, int indent) {
    std::printf("%*sitem \"%s\" action=\"%s\" link=\"%s\" str40=\"%s\" str80=\"%s\" %d %04x\n", indent, "",
                it.name.c_str(), it.action.c_str(), it.link.c_str(), it.str40.c_str(), it.str80.c_str(), it.unk140,
                it.unk142);
    for (const MblItem& c : it.children) print_item(c, indent + 2);
}

int cmd_mbl(Disc& disc, const Args& a) {
    Bytes data = disc.volume().read(need(disc.volume(), a[0]));
    MblFile f = MblFile::parse(data);
    std::printf("%zu page(s), page header 0x%x%s\n", f.pages.size(), f.page_header, f.legacy ? " (legacy)" : "");
    for (const MblPage& p : f.pages) {
        std::printf("page \"%s\" %u %04x back=\"%s\" back_action=\"%s\" str44=\"%s\"\n", p.name.c_str(), p.unk40,
                    p.flags42, p.back.c_str(), p.back_action.c_str(), p.str44.c_str());
        for (const MblItem& it : p.items) print_item(it, 2);
    }
    return 0;
}

int cmd_imgs(Disc& disc, const Args& a) {
    Bytes data = disc.volume().read(need(disc.volume(), a[0]));
    Imgs set = Imgs::parse(data);
    std::printf("%zu image(s)\n", set.entries.size());
    for (size_t i = 0; i < set.entries.size(); ++i) {
        Tex1::Image img = set.decode(data, i);
        std::printf("  %3zu %-28s %ux%u\n", i, set.entries[i].name.c_str(), img.width, img.height);
        if (a.size() > 1 && img.width && img.height)
            if (!save(a[1] + "/" + set.entries[i].name + ".png", encode_png(img.width, img.height, img.rgba)))
                return 1;
    }
    return 0;
}

// menu-shot <pmb> <outdir> [page-name|index] [options], see the registration below
int cmd_shot(Disc& disc, const Args& a) {
    Bytes data = disc.volume().read(need(disc.volume(), a[0]));
    MenuScene scene = MenuScene::load(data, a[0]);
    SceneOptions so;
    RenderOptions ro;
    std::string want;
    bool all_layers = false, game_font = true;
    for (size_t i = 2; i < a.size(); ++i) {
        if (a[i] == "--all-layers") all_layers = true;
        else if (a[i] == "--outlines") ro.outlines = true;
        else if (a[i] == "--no-labels") ro.widget_labels = false;
        else if (a[i] == "--debug-font") game_font = false;
        else if (a[i] == "--focus" && i + 1 < a.size()) so.focus = std::stoi(a[++i]);
        else want = a[i];
    }
    std::string base = a[0].substr(a[0].find_last_of('/') + 1);
    base = base.substr(0, base.find('.'));
    Vfs vfs(&disc.volume());
    // A page of menu/<LANG>/ draws in that language (its cell font).
    if (a[0].rfind("menu/", 0) == 0 && a[0].find('/', 5) != std::string::npos)
        if (auto l = text::language_from_code(a[0].substr(5, a[0].find('/', 5) - 5))) vfs.set_language(*l);
    int written = 0;
    for (int p = 0; p < int(scene.file().pages.size()); ++p) {
        const Page& pg = scene.file().pages[p];
        if (!want.empty() && want != pg.name && want != std::to_string(p)) continue;
        SceneOptions o = so;
        if (all_layers) o.layer_visible.assign(pg.layers.size(), 1);
        std::vector<u32> px = render_page_rgba(scene, p, o, ro, 640, 448, game_font ? &vfs : nullptr);
        if (px.empty()) {
            std::printf("render failed: %s\n", SDL_GetError());
            return 1;
        }
        std::string name = pg.name;
        for (char& c : name)
            if (c == '/') c = '_';
        char file[64];
        std::snprintf(file, sizeof(file), "%02d_", p);
        if (!save(a[1] + "/" + base + "_" + file + name + ".png", encode_png(640, 448, px))) return 1;
        ++written;
    }
    std::printf("%d page(s) rendered\n", written);
    return 0;
}

int cmd_survey(Disc& disc, const Args&) {
    struct Stat {
        int total = 0, ok = 0, legacy = 0;
        std::vector<std::string> failed;
    };
    std::map<std::string, Stat> st;
    size_t pages = 0, elements = 0, images = 0, image_fail = 0;
    for (const auto& e : disc.volume().entries()) {
        std::string kind = ends_with(e.path, ".pmb") ? "pmb" : ends_with(e.path, ".mbl") ? "mbl"
                           : ends_with(e.path, ".imgs")                                   ? "imgs"
                                                                                          : "";
        if (kind.empty()) continue;
        Stat& s = st[kind];
        ++s.total;
        try {
            Bytes data = disc.volume().read(e);
            if (kind == "pmb") {
                if (data.empty()) { // two empty placeholder files exist
                    ++s.ok;
                    continue;
                }
                MenuScene sc = MenuScene::load(data, e.path);
                if (sc.file().name_size != 64) ++s.legacy;
                pages += sc.file().pages.size();
                for (const Page& p : sc.file().pages) {
                    elements += p.items.size();
                    for (const Layer& l : p.layers) elements += l.elements.size() + l.widgets.size();
                }
                images += sc.images().size();
                image_fail += sc.image_failures();
            } else if (kind == "mbl") {
                if (MblFile::parse(data).legacy) ++s.legacy;
            } else {
                Imgs set = Imgs::parse(data);
                for (size_t i = 0; i < set.entries.size(); ++i) {
                    ++images;
                    if (set.decode(data, i).rgba.empty()) ++image_fail;
                }
            }
            ++s.ok;
        } catch (const std::exception& ex) {
            s.failed.push_back(e.path + ": " + ex.what());
        }
    }
    for (auto& [k, s] : st) {
        std::printf(".%s: %d/%d parsed (%d legacy layout)\n", k.c_str(), s.ok, s.total, s.legacy);
        for (const std::string& f : s.failed) std::printf("  FAIL %s\n", f.c_str());
    }
    std::printf("pmb pages %zu, elements %zu; images decoded %zu, failed %zu\n", pages, elements, images - image_fail,
                image_fail);
    return 0;
}

// One image of a .pmb layout as a PNG (e.g. a car_render_* widget's image, the showroom's
// reflection map).
int cmd_pmb_image(Disc& disc, const Args& a) {
    Bytes data = disc.volume().read(need(disc.volume(), a[0]));
    MenuScene scene = MenuScene::load(data, a[0]);
    size_t i = size_t(std::stoul(a[1]));
    const MenuImage* img = i < scene.images().size() ? &scene.images()[i] : nullptr;
    if (!img || img->rgba.empty()) {
        std::fprintf(stderr, "image %zu not decoded\n", i);
        return 1;
    }
    std::printf("image %zu: %ux%u\n", i, img->width, img->height);
    return save(a[2], encode_png(img->width, img->height, img->rgba)) ? 0 : 1;
}

} // namespace

GT3TOOL_COMMAND(menu_image, "menu-image", "<pmb> <index> <out.png>", "decode one .pmb image to PNG", 3, cmd_pmb_image);
GT3TOOL_COMMAND(menu_pmb, "menu-pmb", "<vol-path>", "dump a .pmb menu layout", 1, cmd_pmb);
GT3TOOL_COMMAND(menu_mbl, "menu-mbl", "<vol-path>", "dump a .mbl menu flow file", 1, cmd_mbl);
GT3TOOL_COMMAND(menu_imgs, "menu-imgs", "<vol-path> [outdir]", "list (and decode to PNGs) an IMGS image set", 1,
                cmd_imgs);
GT3TOOL_COMMAND(menu_shot, "menu-shot", "<pmb> <outdir> [page] [--all-layers] [--outlines] [--no-labels] [--debug-font] [--focus N]",
                "render .pmb pages to 640x448 PNGs", 2, cmd_shot);
GT3TOOL_COMMAND(menu_survey, "menu-survey", "", "parse every .pmb/.mbl/.imgs on the disc", 0, cmd_survey);

} // namespace gt3r::tool
