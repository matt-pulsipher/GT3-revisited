#include "menu/flow/save_page.h"

#include <algorithm>
#include <limits>

namespace gt3r::menu::flow {

namespace {

// Icon grid inside an icon view (284 x 214): 64 x 64 cells at a 70 px pitch, 6 px in from
// the top-left (measured from the reference capture, M).
constexpr float kCellPitch = 70, kCellInset = 6;
// The operation menu (layer operation1, a 256 x 140 window for four buttons): buttons
// 128 x 26 at x +116, the first 36 below the window's top, 32 apart; the file's icon face
// (96 x 96) at (+8, top - 104). Longer menus stretch the window downwards.
constexpr float kOpW = 256, kOpMinH = 140, kOpButtonX = 116, kOpFirst = 36, kOpPitch = 32;
// The export's "Exported as" message keeps file names to this many bytes (the window's line).
constexpr size_t kDoneNameMax = 40;

FileDialogs& dialogs_ref() {
    static FileDialogs d;
    return d;
}

const Element* find_any(const Page& p, const std::string& name) {
    for (const Layer& l : p.layers) {
        for (const Element& e : l.elements)
            if (e.name == name) return &e;
        for (const Element& e : l.widgets)
            if (e.name == name) return &e;
    }
    return nullptr;
}

bool game_mode(SavePage::Mode m) {
    return m != SavePage::Mode::LoadSettings && m != SavePage::Mode::SaveSettings;
}

bool inside(const SDL_FRect& r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
bool inside(const Element* e, float x, float y) { return e && inside(SDL_FRect{e->x, e->y, e->w, e->h}, x, y); }

// Icon v (0..11, rows top first) of an icon view.
SDL_FRect cell_box(const Element& view, int v) {
    return {view.x + kCellInset + float(v % SavePage::kCols) * kCellPitch,
            view.y + view.h - kCellInset - 64 - float(v / SavePage::kCols) * kCellPitch, 64, 64};
}

// The operation menu's window for `buttons` buttons, centred on the focused icon `cur` and
// kept inside the panel's view; its button i (128 x 26) at op_button().
SDL_FRect op_window(const Element* view, const SDL_FRect& cur, size_t buttons) {
    const float h = std::max(kOpMinH, kOpFirst + 8 + kOpPitch * float(buttons - 1));
    float x = cur.x + cur.w * 0.5f - kOpW * 0.5f, y = cur.y + cur.h * 0.5f - h * 0.5f;
    if (view) {
        x = std::clamp(x, view->x, view->x + view->w - kOpW);
        y = std::clamp(y, view->y, view->y + view->h - h);
    }
    return {x, y, kOpW, h};
}
SDL_FRect op_button(const SDL_FRect& win, size_t i) {
    return {win.x + kOpButtonX, win.y + win.h - kOpFirst - float(i) * kOpPitch, 128, 26};
}

// A file's name for the page's messages: as the player gave it (FileDialogs::name), cut to
// kDoneNameMax bytes on a character boundary.
std::string shown_name(const std::string& path) {
    const FileDialogs& d = file_dialogs();
    const size_t sl = path.find_last_of("/\\");
    std::string n = d.name ? d.name(path) : path.substr(sl == std::string::npos ? 0 : sl + 1);
    if (n.size() > kDoneNameMax) {
        size_t cut = kDoneNameMax - 3;
        while (cut > 0 && (static_cast<unsigned char>(n[cut]) & 0xC0) == 0x80) --cut;
        n = n.substr(0, cut) + "...";
    }
    return n;
}

} // namespace

void set_file_dialogs(FileDialogs d) { dialogs_ref() = std::move(d); }
const FileDialogs& file_dialogs() { return dialogs_ref(); }

std::string SavePage::export_path(const std::string& chosen, Op format) {
    if (chosen.rfind("content://", 0) == 0) return chosen;
    const size_t sl = chosen.find_last_of("/\\");
    if (chosen.find('.', sl == std::string::npos ? 0 : sl + 1) != std::string::npos) return chosen;
    for (const ExportFormat& e : kExportFormats)
        if (e.op == format) return chosen + e.ext;
    return chosen;
}

void SavePage::open(Mode m, Backend b, std::string title) {
    *this = SavePage{};
    mode_ = m;
    backend_ = std::move(b);
    title_ = std::move(title);
    // The right panel: a file from elsewhere (the platform's file dialog).
    if (file_dialogs().show && (m == Mode::LoadGame || m == Mode::LoadGuest || m == Mode::Files)) {
        File o;
        o.kind = File::Kind::Open;
        o.name = m == Mode::LoadGuest ? "Open a file" : "Import a save";
        extra_.push_back(o);
    }
    reload(true);
    // Start on the save being played (Load / Save), else on the first file.
    for (size_t i = 0; i < files_.size(); ++i)
        if (files_[i].current && (m == Mode::LoadGame || m == Mode::SaveGame)) cursor_ = int(i);
    top_ = std::max(0, cursor_ / kCols - (kRows - 1));
    if (files_.empty() && !extra_.empty()) panel_ = 1;
    prev_ = std::numeric_limits<MenuKeys>::max(); // keys held when the page opens do not count
}

void SavePage::set_transfer_texts(TransferTexts t) {
    transfer_ = std::move(t);
    // No save to take the transfer: the page says so.
    if (mode_ == Mode::Transfer && files_.empty()) {
        done_ = transfer_.no_file;
        phase_ = Phase::Done;
    }
}

void SavePage::reload(bool to_first) {
    files_ = backend_.list ? backend_.list(mode_) : std::vector<File>{};
    if (to_first) {
        panel_ = 0;
        cursor_ = 0;
        while (cursor_ < int(files_.size()) && files_[size_t(cursor_)].kind == File::Kind::New) ++cursor_;
    }
    if (panel_ == 0 && files_.empty() && !extra_.empty()) panel_ = 1;
    const int n = int(files(panel_).size());
    cursor_ = std::clamp(cursor_, 0, std::max(0, n - 1));
    const int rows = (int(files_.size()) + kCols - 1) / kCols;
    if (panel_ == 0) {
        const int r = cursor_ / kCols;
        if (r < top_) top_ = r;
        if (r >= top_ + kRows) top_ = r - kRows + 1;
    }
    top_ = std::clamp(top_, 0, std::max(0, rows - kRows));
}

const SavePage::File* SavePage::focused() const {
    const auto& f = files(panel_);
    return cursor_ >= 0 && cursor_ < int(f.size()) ? &f[size_t(cursor_)] : nullptr;
}

std::vector<SavePage::Op> SavePage::ops() const {
    const File* f = focused();
    if (!f || f->kind == File::Kind::New || f->kind == File::Kind::Open) return {};
    if (f->broken) return {OpDelete};
    std::vector<Op> o;
    if (f->kind == File::Kind::Setting) {
        if (mode_ == Mode::LoadSettings) o.push_back(OpLoad);
        else if (f->own) o.push_back(OpOverwrite);
        o.insert(o.end(), {OpRename, OpCopy, OpDelete});
        return o;
    }
    switch (mode_) {
    case Mode::LoadGuest:
    case Mode::Transfer: return {};
    case Mode::LoadGame: o.push_back(OpLoad); break;
    case Mode::SaveGame: o.push_back(OpOverwrite); break;
    default: break;
    }
    o.insert(o.end(), {OpRename, OpCopy});
    if (!f->current) o.push_back(OpDelete);
    if (file_dialogs().show) o.push_back(OpExport);
    return o;
}

void SavePage::run(Op op, const std::string& arg) {
    chosen_ = op;
    const File* f = focused();
    static const File kNone;
    result_ = backend_.run ? backend_.run(mode_, op, f ? *f : kNone, arg) : int(WriteError);
    frames_ = 0;
    if (op == OpTransfer && (result_ == Ok || result_ == AlreadyDone)) {
        // Made (the file's panel shows the new credits), or refused because it was made before: a window says which.
        done_ = result_ == Ok ? transfer_.complete : transfer_.already;
        result_ = Ok;
        phase_ = Phase::Done;
        reload(false);
        return;
    }
    if (result_ != Ok) {
        phase_ = Phase::Error;
        return;
    }
    phase_ = Phase::List;
    const bool ends = op == OpLoad || op == OpSave || op == OpOverwrite || op == OpNewGame;
    if (ends) {
        result_ = -100; // ended: tick() returns 1
        return;
    }
    // Copies, imports and renamed saves are the newest files: the cursor goes to them.
    reload(op == OpCopy || op == OpImport || (op == OpRename && game_mode(mode_)));
    // An export changes nothing on the page: a window says where it went.
    if (exporting()) {
        done_ = "Exported as " + shown_name(export_path(arg, op));
        phase_ = Phase::Done;
    }
}

void SavePage::show_dialog(FileDialogs::Kind kind, const std::string& name) {
    if (file_dialogs().poll) file_dialogs().poll(); // (an answer still there from a dialog given up on)
    file_dialogs().show(kind, name);
    phase_ = Phase::Dialog;
}

void SavePage::name_done(bool ok, const std::string& name) {
    if (phase_ != Phase::NameEntry) return;
    std::string n = name.substr(0, size_t(kNameMax));
    while (!n.empty() && n.back() == ' ') n.pop_back();
    const File* f = focused();
    const bool is_new = f && f->kind == File::Kind::New;
    if (!ok || n.empty()) {
        phase_ = is_new ? Phase::List : Phase::Operation;
        return;
    }
    if (is_new) run(mode_ == Mode::LoadGame ? OpNewGame : OpSave, n);
    else run(OpRename, n);
}

void SavePage::move(int dc, int dr, std::vector<std::string>& sounds) {
    int panel = panel_, c = cursor_;
    const int n = int(files(panel).size());
    int col = c % kCols + dc, row = c / kCols + dr;
    // Right past the row's last file (or the panel's edge) goes to the other panel.
    if (dc > 0 && panel == 0 && !extra_.empty() && (col >= kCols || row * kCols + col >= n)) {
        panel = 1;
        c = std::min((row - top_) * kCols, int(extra_.size()) - 1);
        c = std::max(c, 0);
    } else if (col < 0 && panel == 1 && !files_.empty()) {
        panel = 0;
        c = std::min((top_ + row) * kCols + kCols - 1, int(files_.size()) - 1);
    } else {
        const int rows = (n + kCols - 1) / kCols;
        if (col >= 0 && col < kCols && row >= 0 && row < rows) c = std::min(row * kCols + col, n - 1);
    }
    if (panel == panel_ && c == cursor_) return;
    panel_ = panel;
    cursor_ = c;
    sounds.push_back("cursor");
    if (panel_ == 0) {
        const int r = cursor_ / kCols;
        if (r < top_) top_ = r;
        if (r >= top_ + kRows) top_ = r - kRows + 1;
    }
}

int SavePage::tick(MenuKeys held, std::vector<std::string>& sounds) {
    const MenuKeys pressed = held & MenuKeys(~prev_);
    // Directions repeat like the menus (the press, 31 frames later, then every 4 frames).
    const MenuKeys dirs = key_bit(MenuKey::Up) | key_bit(MenuKey::Down) | key_bit(MenuKey::Left) | key_bit(MenuKey::Right);
    MenuKeys fired = pressed;
    if (held & dirs) {
        if (pressed & dirs) repeat_ = 0;
        else if (++repeat_ >= 31 && (repeat_ - 31) % 4 == 0) fired |= held & dirs;
    }
    prev_ = held;
    ++frames_;
    auto key = [&](MenuKey k) { return (fired & key_bit(k)) != 0; };
    if (result_ == -100) return 1; // ended by name_done()
    switch (phase_) {
    case Phase::List: {
        int dc = key(MenuKey::Right) ? 1 : key(MenuKey::Left) ? -1 : 0;
        int dr = key(MenuKey::Down) ? 1 : key(MenuKey::Up) ? -1 : 0;
        if (dc || dr) {
            move(dc, dr, sounds);
        } else if (key(MenuKey::Confirm)) {
            const File* f = focused();
            if (!f || (f->broken && ops().empty())) {
                sounds.push_back("disable");
                break;
            }
            sounds.push_back("ok");
            if (f->kind == File::Kind::New) {
                if (mode_ == Mode::SaveSettings) {
                    run(OpSave, {});
                } else {
                    name_ = f->name;
                    phase_ = Phase::NameEntry;
                }
            } else if (f->kind == File::Kind::Open) {
                chosen_ = mode_ == Mode::LoadGuest ? OpLoad : OpImport;
                show_dialog(FileDialogs::Kind::OpenSave, {});
            } else if (mode_ == Mode::LoadGuest && !f->broken) {
                run(OpLoad, {});
            } else if (mode_ == Mode::Transfer) {
                // The one question before anything is written, starting on CANCEL.
                chosen_ = OpTransfer;
                choice_ = 1;
                phase_ = Phase::Confirm;
            } else {
                op_ = 0;
                phase_ = Phase::Operation;
            }
        } else if (key(MenuKey::Cancel)) {
            sounds.push_back("cancel");
            return -1;
        }
        break;
    }
    case Phase::Operation: {
        const std::vector<Op> o = ops();
        int d = key(MenuKey::Down) ? 1 : key(MenuKey::Up) ? -1 : 0;
        if (d) {
            int n = std::clamp(op_ + d, 0, int(o.size()) - 1);
            if (n != op_) {
                op_ = n;
                sounds.push_back("cursor");
            }
        } else if (key(MenuKey::Confirm) && op_ < int(o.size())) {
            sounds.push_back("ok");
            chosen_ = o[size_t(op_)];
            const File* f = focused();
            if (chosen_ == OpRename) {
                name_ = f ? f->name : std::string();
                phase_ = Phase::NameEntry;
            } else if (chosen_ == OpExport) {
                format_ = 0;
                phase_ = Phase::Format;
            } else if (chosen_ == OpDelete || (chosen_ == OpOverwrite && f && !f->current)) {
                // The one question: deleting starts on CANCEL, overwriting on OK.
                choice_ = chosen_ == OpDelete ? 1 : 0;
                phase_ = Phase::Confirm;
            } else {
                run(chosen_, {});
            }
        } else if (key(MenuKey::Cancel)) {
            sounds.push_back("cancel");
            phase_ = Phase::List;
        }
        break;
    }
    case Phase::Format: {
        // Export to PS2: the format, then the save dialog for the place (starting on the
        // file's name with the format's extension).
        int d = key(MenuKey::Down) ? 1 : key(MenuKey::Up) ? -1 : 0;
        if (d) {
            int n = std::clamp(format_ + d, 0, kExportFormatCount - 1);
            if (n != format_) {
                format_ = n;
                sounds.push_back("cursor");
            }
        } else if (key(MenuKey::Confirm)) {
            sounds.push_back("ok");
            const ExportFormat& e = kExportFormats[format_];
            const File* f = focused();
            chosen_ = e.op;
            show_dialog(FileDialogs::Kind::ExportPs2, (f && !f->name.empty() ? f->name : "GT3") + e.ext);
        } else if (key(MenuKey::Cancel)) {
            sounds.push_back("cancel");
            phase_ = Phase::Operation;
        }
        break;
    }
    case Phase::NameEntry: break; // the flow runs the name entry page
    case Phase::Dialog: {
        // The dialog's answer: a file runs the operation; cancelled, back where the dialog was
        // opened (an export's formats, else the list). The waiting window's CANCEL (either
        // key) goes back the same way, for a dialog the player cannot reach or one that never
        // answers: the dialog is given up (FileDialogs::cancel) and its answer never runs.
        const Phase back = exporting() ? Phase::Format : Phase::List;
        std::optional<std::string> path =
            file_dialogs().poll ? file_dialogs().poll() : std::optional<std::string>(std::string());
        if (path) {
            if (path->empty()) phase_ = back;
            else run(chosen_, *path);
        } else if (key(MenuKey::Confirm) || key(MenuKey::Cancel)) {
            sounds.push_back("cancel");
            if (file_dialogs().cancel) file_dialogs().cancel();
            phase_ = back;
        }
        break;
    }
    case Phase::Confirm:
        if (key(MenuKey::Left) || key(MenuKey::Right)) {
            int c = key(MenuKey::Left) ? 0 : 1;
            if (c != choice_) {
                choice_ = c;
                sounds.push_back("cursor");
            }
        } else if (key(MenuKey::Cancel) || (key(MenuKey::Confirm) && choice_ == 1)) {
            sounds.push_back("cancel");
            phase_ = chosen_ == OpTransfer ? Phase::List : Phase::Operation;
        } else if (key(MenuKey::Confirm)) {
            sounds.push_back("ok");
            run(chosen_, {});
        }
        break;
    case Phase::Error:
        if (key(MenuKey::Confirm) || key(MenuKey::Cancel)) {
            sounds.push_back("ok");
            phase_ = Phase::List;
            reload(false);
        }
        break;
    case Phase::Done:
        if (key(MenuKey::Confirm) || key(MenuKey::Cancel)) {
            sounds.push_back("ok");
            phase_ = Phase::List;
            if (mode_ == Mode::Transfer && files_.empty()) return -1; // (no save to take it: the page is left)
        }
        break;
    }
    return result_ == -100 ? 1 : 0;
}

bool SavePage::hover(const MenuSet::PageRef& ref, float x, float y, std::vector<std::string>& sounds) {
    MenuKeys press = 0;
    return pointer(ref, x, y, false, press, sounds);
}

bool SavePage::tap(const MenuSet::PageRef& ref, float x, float y, MenuKeys& press, std::vector<std::string>& sounds) {
    return pointer(ref, x, y, true, press, sounds);
}

bool SavePage::pointer(const MenuSet::PageRef& ref, float x, float y, bool tap, MenuKeys& press,
                       std::vector<std::string>& sounds) {
    if (!ref) return false;
    const Page& p = ref.layout();
    auto el = [&](const std::string& n) { return find_any(p, n); };
    // A file, button or choice under the pointer: when focused already a tap is Confirm,
    // else the focus goes there.
    auto focus = [&](int& at, int i) {
        if (i != at) {
            at = i;
            sounds.push_back("cursor");
        } else if (tap) {
            press = key_bit(MenuKey::Confirm);
        }
        return true;
    };
    // Outside the open window a tap is Cancel (back one level).
    auto outside = [&] {
        if (tap) press = key_bit(MenuKey::Cancel);
        return tap;
    };
    switch (phase_) {
    case Phase::List:
        for (int s = 0; s < 2; ++s) {
            if (s == 1 && extra_.empty()) continue;
            const Element* view = el(s == 0 ? "icon_view_slot1" : "icon_view_slot2");
            if (!view) continue;
            const int first = s == 0 ? top_ * kCols : 0;
            for (int v = 0; v < kCols * kRows; ++v) {
                const int i = first + v;
                if (i >= int(files(s).size()) || !inside(cell_box(*view, v), x, y)) continue;
                if (s != panel_) panel_ = s, cursor_ = -1; // (moved by focus())
                return focus(cursor_, i);
            }
        }
        return false;
    case Phase::Operation:
    case Phase::Format: {
        const bool formats = phase_ == Phase::Format;
        const size_t buttons = formats ? size_t(kExportFormatCount) : ops().size();
        const Element* view = el(panel_ == 0 ? "icon_view_slot1" : "icon_view_slot2");
        const int v = cursor_ - (panel_ == 0 ? top_ * kCols : 0);
        if (!view || buttons == 0 || v < 0 || v >= kCols * kRows) return false;
        const SDL_FRect win = op_window(view, cell_box(*view, v), buttons);
        for (size_t i = 0; i < buttons; ++i)
            if (inside(op_button(win, i), x, y)) return focus(formats ? format_ : op_, int(i));
        return inside(win, x, y) || outside();
    }
    case Phase::Confirm: {
        const std::string w = chosen_ == OpOverwrite ? "confirm2" : "confirm";
        if (inside(el(w + "_m_ok"), x, y)) return focus(choice_, 0);
        if (inside(el(w + "_m_cancel"), x, y)) return focus(choice_, 1);
        return inside(el(w + "_window"), x, y) || outside();
    }
    case Phase::Dialog: // the waiting window's CANCEL
        if (!inside(el("working_m_ok"), x, y)) return false;
        if (tap) press = key_bit(MenuKey::Cancel);
        return true;
    case Phase::Error:
    case Phase::Done: // a tap anywhere closes the window
        if (tap) press = key_bit(MenuKey::Confirm);
        return tap;
    case Phase::NameEntry: break;
    }
    return false;
}

void SavePage::draw(const MenuSet::PageRef& ref, std::vector<ElementDraw>& out) const {
    if (!ref) return;
    const Page& p = ref.layout();
    const bool right = !extra_.empty();
    // The controller's panel widgets: their titles, the bottom bars' texts (the focused
    // file's name), 30 % on the panel without the cursor; no right panel without entries.
    auto bar_text = [&](int panel) -> std::string {
        if (panel != panel_) return {};
        const File* f = focused();
        if (!f) return {};
        if (f->kind == File::Kind::New) return game_mode(mode_) ? (mode_ == Mode::LoadGame ? "New game" : "New save") : "New file";
        return f->current ? f->name + " (playing)" : f->name;
    };
    for (ElementDraw& d : out) {
        if (!d.element) continue;
        const std::string& n = d.element->name;
        const std::string c = d.element->widget_class ? *d.element->widget_class : std::string();
        const int panel = c == "title_bar_slot1" || c == "icon_view_slot1" || c == "pt_free_slot1"   ? 0
                          : c == "title_bar_slot2" || c == "icon_view_slot2" || c == "pt_free_slot2" ? 1
                                                                                                      : -1;
        if (panel < 0) continue;
        if (panel == 1 && !right) d.alpha = 0;
        else if (panel != panel_) d.brightness = 0.3f;
        if (n == "title_bar_slot1")
            d.text = std::make_shared<const std::string>(game_mode(mode_) ? "SAVED GAMES" : "SETTING FILES");
        else if (n == "title_bar_slot2")
            d.text = std::make_shared<const std::string>("FROM A FILE");
    }
    float z = 1000;
    auto put = [&](const Element* e, float dx = 0, float dy = 0, const SDL_FRect* box = nullptr,
                   const std::string* text = nullptr) -> ElementDraw* {
        if (!e) return nullptr;
        ElementDraw d;
        d.scene = ref.scene;
        d.element = e;
        d.widget = e->widget_class.has_value();
        d.dx = dx;
        d.dy = dy;
        d.z = z += 1;
        if (box) d.has_box = true, d.box = *box;
        if (text) d.text = std::make_shared<const std::string>(*text);
        out.push_back(d);
        return &out.back();
    };
    auto el = [&](const std::string& n) { return find_any(p, n); };
    auto layer = [&](const std::string& name) {
        for (const Layer& l : p.layers)
            if (l.name == name) {
                for (const Element& e : l.elements) put(&e);
                for (const Element& e : l.widgets) put(&e);
            }
    };
    auto icon_of = [&](const File* f) {
        return el(!f                                 ? "icon_blank"
                  : f->kind == File::Kind::New        ? "icon_new"
                  : f->kind == File::Kind::Open       ? "icon_unknown"
                  : f->broken                         ? "icon_damage"
                  : f->kind == File::Kind::Setting    ? "icon_setting"
                                                      : "icon_game");
    };
    // Title: the mode's title moved onto the title bar.
    {
        const char* t = mode_ == Mode::LoadSettings   ? "title_load_setting"
                        : mode_ == Mode::SaveSettings ? "title_save_setting"
                                                      : "title_file_operation";
        std::string text = title_;
        if (text.empty())
            text = mode_ == Mode::LoadGame    ? "Load game"
                   : mode_ == Mode::SaveGame  ? "Save game"
                   : mode_ == Mode::LoadGuest ? "Load Garage"
                                              : "";
        if (const Element* te = el(t))
            if (const Element* base = el("title_base"))
                put(te, base->x - te->x, base->y - te->y, nullptr, text.empty() ? nullptr : &text);
    }
    // Bottom bars.
    for (int s = 0; s < 2; ++s)
        if (std::string t = bar_text(s); !t.empty())
            put(el(s == 0 ? "pt_free_slot1" : "pt_free_slot2"), 0, 0, nullptr, &t);
    // The icon grids (the left one scrolled by rows).
    SDL_FRect cur{};
    bool have_cur = false;
    for (int s = 0; s < 2; ++s) {
        if (s == 1 && !right) continue;
        const Element* view = el(s == 0 ? "icon_view_slot1" : "icon_view_slot2");
        if (!view) continue;
        const std::vector<File>& list = files(s);
        const int first = s == 0 ? top_ * kCols : 0;
        for (int v = 0; v < kCols * kRows; ++v) {
            const int i = first + v;
            SDL_FRect box = cell_box(*view, v);
            const File* f = i < int(list.size()) ? &list[size_t(i)] : nullptr;
            if (ElementDraw* d = put(icon_of(f), 0, 0, &box))
                d->brightness = (f && !f->own ? 0.5f : 1.0f) * (s == panel_ ? 1.0f : 0.3f);
            if (f && s == panel_ && i == cursor_) cur = box, have_cur = true;
        }
    }
    if (have_cur) {
        // The cursor: the white quad added over the focused icon with alpha 0.25 x a
        // sawtooth falling from 1 by 1/30 per frame.
        if (ElementDraw* d = put(el("icon_cursor"), 0, 0, &cur)) {
            d->additive = true;
            d->alpha = 0.25f * (1.0f - float(frames_ % 31) / 30.0f);
        }
    }
    // Info panel of the focused file.
    const File* f = focused();
    if (f && !f->broken && (f->kind == File::Kind::Game || f->kind == File::Kind::Setting)) {
        for (const Layer& l : p.layers)
            if (l.name == (f->kind == File::Kind::Game ? "info_game" : "info_setting")) {
                for (const Element& e : l.elements) put(&e);
                for (const Element& e : l.widgets)
                    for (const auto& [n, t] : f->texts)
                        if (n == e.name && !t.empty()) put(&e, 0, 0, nullptr, &t);
            }
    }
    if (phase_ == Phase::Dialog || phase_ == Phase::Done) {
        // Ours, in the original's "working" window (its "Complete" line and OK button): the
        // file dialog being waited for, with CANCEL; where an export went, with OK.
        layer("working");
        if (const Element* face = el("working_icon_face")) {
            SDL_FRect fb{face->x + (face->w - 64) * 0.5f, face->y + (face->h - 64) * 0.5f, 64, 64};
            put(icon_of(f), 0, 0, &fb);
        }
        const std::string text = phase_ == Phase::Dialog ? std::string("Waiting for the file window...") : done_;
        const std::string button = phase_ == Phase::Dialog ? "CANCEL" : "OK";
        put(el("working_text_complete"), 0, 0, nullptr, &text);
        put(el("working_m_ok"), 0, 0, nullptr, &button);
        return;
    }
    if (phase_ == Phase::List || phase_ == Phase::NameEntry) return;
    // Operation menu (operation1's window and button art, the labels by code), centred on
    // the focused icon and kept inside the panel. Export to PS2's format window is drawn the
    // same way in its place (Copy's button art under each format's label).
    const bool formats = phase_ == Phase::Format;
    const std::vector<Op> o = ops();
    const size_t buttons = formats ? size_t(kExportFormatCount) : o.size();
    if (const Element* win = el("op1_window"); win && buttons > 0 && have_cur) {
        const SDL_FRect wb = op_window(el(panel_ == 0 ? "icon_view_slot1" : "icon_view_slot2"), cur, buttons);
        const float x = wb.x, top = wb.y + wb.h;
        put(win, 0, 0, &wb);
        SDL_FRect fb{x + 8 + 16, top - 104 + 16, 64, 64};
        put(icon_of(f), 0, 0, &fb);
        for (size_t i = 0; i < buttons; ++i) {
            const Op op = formats ? OpCopy : o[i];
            const char* art = op == OpLoad        ? "op1_m_load"
                              : op == OpOverwrite ? "op1_m_write"
                              : op == OpRename    ? "op1_m_comment"
                              : op == OpDelete    ? "op1_m_delete"
                                                  : "op1_m_copy";
            std::string name = art;
            const bool on = formats ? int(i) == format_ : int(i) == op_ && phase_ == Phase::Operation;
            if (!on) name += "_sel";
            const Element* b = el(name);
            if (!b) continue;
            std::string label = formats                                ? kExportFormats[i].label
                                : op == OpExport                       ? "Export to PS2"
                                : op == OpOverwrite && f && f->current ? "Save"
                                                                       : std::string();
            const SDL_FRect bb = op_button(wb, i);
            put(b, bb.x - b->x, bb.y - b->y, nullptr, label.empty() ? nullptr : &label);
        }
    }
    switch (phase_) {
    case Phase::Confirm: {
        const bool save = chosen_ == OpOverwrite;
        layer(save ? "confirm2" : "confirm");
        if (chosen_ == OpTransfer) put(el("confirm_text_delete"), 0, 0, nullptr, &transfer_.confirm); // (the delete window's art)
        else put(el(save ? "confirm2_text_overwrite" : "confirm_text_delete"));
        put(el(std::string(save ? "confirm2_m_ok" : "confirm_m_ok") + (choice_ == 0 ? "" : "_sel")));
        put(el(std::string(save ? "confirm2_m_cancel" : "confirm_m_cancel") + (choice_ == 1 ? "" : "_sel")));
        if (const Element* face = el("confirm_icon_face")) {
            SDL_FRect fb{face->x + (face->w - 64) * 0.5f, face->y + (face->h - 64) * 0.5f, 64, 64};
            put(icon_of(f), 0, 0, &fb);
        }
        break;
    }
    case Phase::Error:
        if (result_ == WriteError) {
            layer("writeerror");
            put(el("writeerror_m_ok"));
        } else {
            layer("readerror");
            put(el(result_ == BadData ? "readerror_text_version_error" : "readerror_text_file_damage"));
            put(el("readerror_m_ok"));
        }
        break;
    default: break;
    }
}

} // namespace gt3r::menu::flow
