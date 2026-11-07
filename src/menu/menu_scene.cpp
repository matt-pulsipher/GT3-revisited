#include "menu/menu_scene.h"

#include "base/log.h"
#include "formats/texture_pack.h"
#include "vfs/vfs.h"

#include <algorithm>

namespace gt3r::menu {

const char* const kMenuLanguages[] = {"US", "JP", "BR", "FR", "GR", "IT", "SP", nullptr};

MenuScene MenuScene::load(ByteSpan data, std::string path) {
    MenuScene s;
    s.path_ = std::move(path);
    s.file_ = PmbFile::parse(data);
    s.images_.resize(s.file_.images.size());
    for (size_t i = 0; i < s.file_.images.size(); ++i) {
        try {
            Bytes tex = s.file_.image_data(data, i);
            Tex1 set = Tex1::parse(tex);
            if (set.textures.empty()) throw FormatError("empty Tex1 set");
            GsMemory gs;
            set.upload(tex, gs);
            Tex1::Image img = set.decode(gs, 0);
            texture_pack::replace_decoded(tex, 0, img); // the elements' UVs address a pack's picture the same way
            s.images_[i] = {img.width, img.height, std::move(img.rgba)};
        } catch (const std::exception& e) {
            ++s.image_failures_;
            LOG_WARN("%s: image %zu: %s", s.path_.c_str(), i, e.what());
        }
    }
    return s;
}

MenuScene MenuScene::load(const Vfs& vfs, const std::string& path) {
    auto data = vfs.read(path);
    if (!data) throw FormatError("cannot read " + path);
    return load(*data, path);
}

bool MenuScene::default_layer_visible(int page, int layer) const {
    const Page& p = file_.pages.at(page);
    const std::string& name = p.layers.at(layer).name;
    bool has_default = std::any_of(p.layers.begin(), p.layers.end(),
                                   [](const Layer& l) { return l.name == "defaultLayer"; });
    if (has_default) return name == "defaultLayer";
    return name != "hidden";
}

bool MenuScene::layer_visible(int page, int layer, const SceneOptions& o) const {
    if (layer < int(o.layer_visible.size()) && o.layer_visible[layer] >= 0) return o.layer_visible[layer] != 0;
    return default_layer_visible(page, layer);
}

std::vector<DrawItem> MenuScene::draw_list(int page, const SceneOptions& o) const {
    std::vector<DrawItem> out;
    if (page < 0 || page >= int(file_.pages.size())) return out;
    const Page& p = file_.pages[page];
    for (int l = 0; l < int(p.layers.size()); ++l) {
        if (!layer_visible(page, l, o)) continue;
        for (const Element& e : p.layers[l].elements) out.push_back({&e, l, false});
        if (o.show_widgets)
            for (const Element& e : p.layers[l].widgets) out.push_back({&e, l, true});
    }
    if (o.show_items)
        for (int i = 0; i < int(p.items.size()); ++i) {
            const Element* e = &p.items[i];
            if (i == o.focus && !e->alt.empty()) e = &e->alt[0];
            out.push_back({e, -1, false});
        }
    std::stable_sort(out.begin(), out.end(),
                     [](const DrawItem& a, const DrawItem& b) { return a.element->z < b.element->z; });
    return out;
}

std::vector<std::string> list_menu_files(const Vfs& vfs, const std::string& lang, std::string_view ext) {
    std::vector<std::string> out;
    std::string dir = lang.empty() ? "menu/" : "menu/" + lang + "/";
    for (const std::string& name : vfs.list_dir(dir)) {
        if (name.empty() || name.back() == '/') continue;
        if (name.size() >= ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
            out.push_back(dir + name);
    }
    return out;
}

} // namespace gt3r::menu
