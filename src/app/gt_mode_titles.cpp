#include "app/gt_mode_titles.h"

#include "base/log.h"
#include "formats/menu/mbl.h"
#include "formats/menu/pmb.h"
#include "menu/flow/menu_set.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cstdlib>
#include <optional>

namespace gt3r {

namespace {

bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// Menu text may hold non-ASCII bytes; the UI font is ASCII.
std::string ascii(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (c < 0x80) out += char(c);
        else if ((c & 0xC0) == 0xC0) out += '-';
    return out;
}

} // namespace

std::map<std::string, std::string> game_menu_titles(const Vfs& vfs, const std::string& lang) {
    std::map<std::string, std::string> out;
    std::optional<Bytes> mb = vfs.read("menu/gt_mode.mbl");
    if (!mb) return out;
    try {
        menu::MblFile mbl = menu::MblFile::parse(*mb);
        std::map<int, std::optional<menu::PmbFile>> pmbs;
        for (const menu::MblPage& p : mbl.pages) {
            if (!starts(p.name, "r/") && !starts(p.name, "lc/")) continue;
            int n = menu::flow::pmb_number(p);
            if (!pmbs.count(n)) {
                std::optional<Bytes> b = vfs.read("menu/" + lang + "/gt_mode" + std::to_string(n) + ".pmb");
                pmbs[n] = b ? std::optional<menu::PmbFile>(menu::PmbFile::parse(*b)) : std::nullopt;
            }
            const std::optional<menu::PmbFile>& pmb = pmbs[n];
            int pi = pmb ? pmb->find_page(p.name) : -1;
            if (pi < 0) continue;
            const menu::Page& page = pmb->pages[size_t(pi)];
            for (const menu::MblItem& it : p.items) {
                if (it.str80.empty()) continue;
                for (const menu::Layer& l : page.layers) {
                    if (l.name != it.str80) continue;
                    std::string title;
                    for (const menu::Element& w : l.widgets)
                        if (w.text && w.widget_class == "event_text") title = w.text->text;
                    for (const menu::Element& e : l.elements)
                        if (e.text && starts(e.name, "license_title")) title = e.text->text;
                    title = ascii(title);
                    if (!title.empty()) out[it.link.empty() ? it.name : it.link] = title;
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN("GT Mode menu titles: %s", e.what());
    }
    return out;
}

std::vector<const game::RaceSeries*> ordered_series(const Vfs& vfs, const game::Catalog& cat) {
    std::vector<const game::RaceSeries*> ordered;
    try {
        if (std::optional<Bytes> mb = vfs.read("menu/gt_mode.mbl")) {
            menu::MblFile mbl = menu::MblFile::parse(*mb);
            for (const menu::MblPage& p : mbl.pages)
                for (const menu::MblItem& it : p.items)
                    for (const game::RaceSeries& s : cat.series)
                        if (s.page == it.link && std::find(ordered.begin(), ordered.end(), &s) == ordered.end())
                            ordered.push_back(&s);
        }
    } catch (const std::exception&) {
    }
    for (const game::RaceSeries& s : cat.series) // any the league pages do not name
        if (std::find(ordered.begin(), ordered.end(), &s) == ordered.end()) ordered.push_back(&s);
    return ordered;
}

std::string license_test_label(const std::string& code) {
    if (code.size() < 7) return code;
    std::string cls = code.substr(1, 2);
    if (cls[0] == '0') cls = cls.substr(1);
    return cls + "-" + std::to_string(std::atoi(code.substr(3).c_str()));
}

} // namespace gt3r
