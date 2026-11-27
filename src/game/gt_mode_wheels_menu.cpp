// Mod wheels at GT Mode's wheel shop (docs/MODDING.md "Adding wheels"), as menu content
// patches (menu/flow/menu_set.h MenuPatches), applied while the gt_mode set loads:
//
//   - each mod wheel for sale (vfs/mod_wheels.h) joins its maker's page (at/ws/ws_<maker>): the
//     page whose name, or the first word of whose maker, is its brand ("OZ Racing" -> ws_oz).
//     Its item ("<label>_link", set_focus_wheel_code like the disc's) is cloned from the page's
//     first wheel, its 52 x 52 tile (the page's hidden image named after the wheel) from that
//     wheel's tile, with the mod's picture;
//   - a brand the shop has no page for gets one (at/ws/ws_mod_<key>, cloned from the shop's first
//     maker's page without its wheels, the maker's name in its title) and a button on the list of
//     makers (at/ws/ws: the button, the maker's logo and name layers cloned from the last maker's,
//     the name in the menu font, the mod's logo);
//   - the garage's wheel page (hm/hm_wheel) gets every mod wheel's tile, so a bought one shows.
//
// Every picture is the mod's own; nothing from the disc is copied into mods.

#include "base/log.h"
#include "game/gt_mode_mods.h"
#include "menu/flow/menu_set.h"
#include "vfs/mod_wheels.h"

#include <algorithm>
#include <map>

namespace gt3r::game {

namespace {

using namespace mod_menu;
using menu::Element;
using menu::Layer;
using menu::MblFile;
using menu::MblItem;
using menu::MblPage;
using menu::MenuScene;
using menu::Page;

constexpr const char* kShopPage = "at/ws/ws";
constexpr const char* kBrandPrefix = "at/ws/ws_";
constexpr const char* kGarageWheels = "hm/hm_wheel";

struct BrandAdd {
    std::string page;          // at/ws/ws_<maker>
    std::string template_page; // a disc maker's page to clone (a new maker), else = page
    std::string key;           // a new maker's: wheel_brand_key
    const ModWheelDef* first = nullptr; // a new maker's: the wheel naming it first (name, logo)
    std::vector<const ModWheelDef*> wheels;
};

struct WheelPlan {
    std::vector<ModWheelDef> wheels; // the enabled mods' wheels that load
    std::vector<BrandAdd> brands;
};

WheelPlan& plan() {
    static WheelPlan p;
    return p;
}

// Turns an element (and its focused look) into `text` in the menu font, centred in its box.
void set_label(Element& e, const std::string& text, const char* font) {
    auto apply = [&](Element& x, menu::Rgba color) {
        x.image.reset();
        x.type = u16((x.type & ~Element::kImage) | Element::kText);
        menu::TextStyle t;
        t.unk0 = 1;
        t.align = 4; // centred
        t.color = color;
        t.font = font;
        t.text = text;
        x.text = t;
    };
    apply(e, {0xFF, 0xFF, 0xFF, 0x80});
    for (Element& a : e.alt) apply(a, {0x7E, 0x7E, 0x7E, 0x80});
}

Element* find_element(Layer& l, const std::string& name) {
    for (Element& e : l.elements)
        if (e.name == name) return &e;
    return nullptr;
}

} // namespace

void patch_wheel_flow(const menu::flow::FileReader& read, MblFile& mbl) {
    WheelPlan& p = plan();
    p = WheelPlan{};
    for (ModWheelDef& d : mod_wheels(read)) {
        if (!d.error.empty()) {
            LOG_WARN("mod wheels: %s: %s", d.file.c_str(), d.error.c_str());
            continue;
        }
        p.wheels.push_back(std::move(d));
    }
    const int shop = mbl.find_page(kShopPage);
    if (p.wheels.empty() || shop < 0 || mbl.pages[size_t(shop)].items.empty()) return;
    // The disc's makers: their pages' names after "ws_".
    std::map<std::string, std::string> disc; // key -> page
    std::string first_page;
    for (const MblItem& it : mbl.pages[size_t(shop)].items) {
        if (!starts(it.link, kBrandPrefix) || mbl.find_page(it.link) < 0) continue;
        disc[it.link.substr(std::string(kBrandPrefix).size())] = it.link;
        if (first_page.empty()) first_page = it.link;
    }
    if (first_page.empty() || mbl.pages[size_t(mbl.find_page(first_page))].items.empty()) return;
    auto brand_for = [&](const std::string& page) -> BrandAdd& {
        for (BrandAdd& b : p.brands)
            if (b.page == page) return b;
        p.brands.push_back({page, page, {}, nullptr, {}});
        return p.brands.back();
    };
    for (const ModWheelDef& w : p.wheels) {
        if (!w.for_sale) continue;
        const std::string key = wheel_brand_key(w.brand), word = wheel_brand_key(w.brand, true);
        auto it = disc.find(key);
        if (it == disc.end()) it = disc.find(word);
        if (it != disc.end()) {
            brand_for(it->second).wheels.push_back(&w);
            continue;
        }
        BrandAdd& b = brand_for(std::string(kBrandPrefix) + "mod_" + key);
        if (!b.first) b.template_page = first_page, b.key = key, b.first = &w;
        b.wheels.push_back(&w);
    }
    // New makers: a button on the shop's list and a page of their own.
    for (const BrandAdd& b : p.brands) {
        if (!b.first) continue;
        MblPage& sp = mbl.pages[size_t(mbl.find_page(kShopPage))];
        MblItem it = sp.items.back();
        it.name = "mod_" + b.key;
        it.link = b.page;
        it.str40 = "m_mod_" + b.key;
        it.str80 = "s_mod_" + b.key;
        sp.items.push_back(std::move(it));
        MblPage page = mbl.pages[size_t(mbl.find_page(b.template_page))];
        page.name = b.page;
        page.back = kShopPage;
        page.items.clear();
        mbl.pages.push_back(std::move(page));
    }
    // The wheels on their makers' pages, as the page's first wheel.
    const MblItem tmpl = mbl.pages[size_t(mbl.find_page(first_page))].items.front();
    for (const BrandAdd& b : p.brands) {
        MblPage& page = mbl.pages[size_t(mbl.find_page(b.page))];
        const MblItem own = page.items.empty() ? tmpl : page.items.front();
        for (const ModWheelDef* w : b.wheels) {
            MblItem it = own;
            it.name = w->label + "_link";
            page.items.push_back(std::move(it));
        }
        LOG_INFO("mod wheels: %zu at %s", b.wheels.size(), b.page.c_str());
    }
}

void patch_wheel_layout(const menu::flow::FileReader&, MenuScene& scene, const MblFile& mbl) {
    const WheelPlan& p = plan();
    if (p.wheels.empty()) return;
    // A page's first wheel: its item and its tile in the hidden layer.
    struct Template {
        std::optional<Element> item, tile;
        bool ok() const { return item && tile; }
    };
    auto first_wheel = [&](Page& pg) {
        Template t;
        Layer* hidden = find_layer(pg, "hidden");
        for (const Element& it : pg.items) {
            if (it.name.size() <= 5 || it.name.compare(it.name.size() - 5, 5, "_link") != 0) continue;
            Element* tile = hidden ? find_element(*hidden, it.name.substr(0, it.name.size() - 5)) : nullptr;
            if (!tile) continue;
            t.item = it, t.tile = *tile;
            break;
        }
        return t;
    };
    // New makers' pages, cloned from their template without its wheels.
    for (const BrandAdd& b : p.brands) {
        if (!b.first || scene.file().find_page(b.page) >= 0) continue;
        const int t = scene.file().find_page(b.template_page);
        if (t < 0) continue;
        Page copy = scene.file().pages[size_t(t)];
        if (!first_wheel(copy).ok()) continue;
        copy.name = b.page;
        if (Layer* hidden = find_layer(copy, "hidden"))
            hidden->elements.erase(std::remove_if(hidden->elements.begin(), hidden->elements.end(),
                                                  [&](const Element& e) {
                                                      for (const Element& it : copy.items)
                                                          if (it.name == e.name + "_link") return true;
                                                      return false;
                                                  }),
                                   hidden->elements.end());
        copy.items.clear();
        for (Layer& l : copy.layers)
            for (Element& e : l.elements)
                if (e.name == "maker_name") set_text(e, b.first->brand);
        scene.mutable_file().pages.push_back(std::move(copy));
    }
    // The wheels on their makers' pages.
    for (const BrandAdd& b : p.brands) {
        Page* pg = layout_page(scene, b.page);
        if (!pg) continue;
        Template t = first_wheel(*pg);
        if (!t.ok()) // (a new maker's page: its template's first wheel)
            if (Page* tp = layout_page(scene, b.template_page)) t = first_wheel(*tp);
        Layer* hidden = find_layer(*pg, "hidden");
        if (!t.ok() || !hidden) continue;
        for (const ModWheelDef* w : b.wheels) {
            if (find_item(*pg, w->label + "_link")) continue;
            Element it = *t.item;
            it.name = w->label + "_link";
            for (Element& a : it.alt) a.name = it.name + "_a";
            pg->items.push_back(std::move(it));
            Element tile = *t.tile;
            tile.name = w->label;
            set_picture(tile, add_picture(scene, w->picture));
            hidden->elements.push_back(std::move(tile));
        }
    }
    // New makers on the shop's list: the last maker's button, logo and name layers.
    if (Page* shop = layout_page(scene, kShopPage)) {
        const int sf = mbl.find_page(kShopPage);
        for (const BrandAdd& b : p.brands) {
            if (!b.first || sf < 0 || find_item(*shop, "mod_" + b.key)) continue;
            // The template: the disc's last maker (the list's items before the mods' own).
            const MblItem* last = nullptr;
            for (const MblItem& it : mbl.pages[size_t(sf)].items)
                if (!starts(it.name, "mod_")) last = &it;
            if (!last) continue;
            const Element* item = find_item(*shop, last->name);
            const Layer* m = find_layer(*shop, last->str40);
            const Layer* s = find_layer(*shop, last->str80);
            if (item) {
                Element e = *item;
                e.name = "mod_" + b.key;
                for (Element& a : e.alt) a.name = e.name + "_a";
                // Narrowed to the button (sw_16 is about 9 units a character).
                const int pct = std::clamp(int((e.w - 8) * 100 / std::max<size_t>(1, 9 * b.first->brand.size())), 40, 100);
                set_label(e, b.first->brand, ("sw_16#1*" + std::to_string(pct) + ",100").c_str());
                shop->items.push_back(std::move(e));
            }
            if (m) {
                Layer l = *m;
                l.name = "m_mod_" + b.key;
                for (Element& e : l.elements)
                    if (e.image) {
                        const int img = add_picture(scene, b.first->brand_logo);
                        if (img >= 0) set_picture(e, img);
                        else set_name_text(e, b.first->brand);
                    }
                shop->layers.push_back(std::move(l));
            }
            if (s) {
                Layer l = *s;
                l.name = "s_mod_" + b.key;
                for (Element& e : l.elements) set_text(e, b.first->brand);
                shop->layers.push_back(std::move(l));
            }
        }
    }
    // The garage's wheel page: every mod wheel's tile, as the page's stock-wheel tile.
    if (Page* garage = layout_page(scene, kGarageWheels))
        if (Layer* hidden = find_layer(*garage, "hidden"))
            if (const Element* def = find_element(*hidden, "default_wheel")) {
                const Element tmpl = *def;
                for (const ModWheelDef& w : p.wheels) {
                    if (find_element(*hidden, w.label)) continue;
                    Element tile = tmpl;
                    tile.name = w.label;
                    set_picture(tile, add_picture(scene, w.picture));
                    hidden->elements.push_back(std::move(tile));
                }
            }
}

} // namespace gt3r::game
