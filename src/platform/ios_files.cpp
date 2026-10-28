#include "platform/ios_files.h"

#include "platform/recent_discs.h"

#include <SDL3/SDL.h>

#if defined(SDL_PLATFORM_IOS)
#include <objc/message.h>
#include <objc/runtime.h>
#endif

#include <map>
#include <utility>
#include <vector>

namespace gt3r::platform {

#if defined(SDL_PLATFORM_IOS)
namespace {

id send(id obj, const char* sel) { return reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(obj, sel_registerName(sel)); }
id send(id obj, const char* sel, id arg) {
    return reinterpret_cast<id (*)(id, SEL, id)>(objc_msgSend)(obj, sel_registerName(sel), arg);
}
void call(id obj, const char* sel) { reinterpret_cast<void (*)(id, SEL)>(objc_msgSend)(obj, sel_registerName(sel)); }
void call(id obj, const char* sel, id arg) {
    reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(obj, sel_registerName(sel), arg);
}
void call(id obj, const char* sel, id a, id b) {
    reinterpret_cast<void (*)(id, SEL, id, id)>(objc_msgSend)(obj, sel_registerName(sel), a, b);
}
void call(id obj, const char* sel, BOOL arg) {
    reinterpret_cast<void (*)(id, SEL, BOOL)>(objc_msgSend)(obj, sel_registerName(sel), arg);
}
bool ask(id obj, const char* sel) { return reinterpret_cast<BOOL (*)(id, SEL)>(objc_msgSend)(obj, sel_registerName(sel)); }
id cls(const char* name) { return reinterpret_cast<id>(objc_getClass(name)); }
id ns_string(const std::string& s) {
    return reinterpret_cast<id (*)(id, SEL, const char*)>(objc_msgSend)(cls("NSString"), sel_registerName("stringWithUTF8String:"),
                                                                       s.c_str());
}
std::string utf8(id ns_str) {
    const char* s = ns_str ? reinterpret_cast<const char* (*)(id, SEL)>(objc_msgSend)(ns_str, sel_registerName("UTF8String"))
                           : nullptr;
    return s ? s : "";
}

// NSUserDefaults key of the bookmarks: a dictionary from a picked file's path to its bookmark.
constexpr const char* kBookmarksKey = "GT3RFileBookmarks";

id bookmarks() { return send(send(cls("NSUserDefaults"), "standardUserDefaults"), "dictionaryForKey:", ns_string(kBookmarksKey)); }

// Remembers `url` (already accessed) as `path`'s bookmark, replacing `old_path`'s.
void store_bookmark(id url, const std::string& path, const std::string& old_path = {}) {
    id data = reinterpret_cast<id (*)(id, SEL, unsigned long, id, id, id*)>(objc_msgSend)(
        url, sel_registerName("bookmarkDataWithOptions:includingResourceValuesForKeys:relativeToURL:error:"), 0UL,
        nullptr, nullptr, nullptr);
    if (!data) return;
    id defaults = send(cls("NSUserDefaults"), "standardUserDefaults");
    id old = bookmarks();
    id dict = old ? send(old, "mutableCopy") : send(send(cls("NSMutableDictionary"), "alloc"), "init");
    if (!old_path.empty()) call(dict, "removeObjectForKey:", ns_string(old_path));
    call(dict, "setObject:forKey:", data, ns_string(path));
    call(defaults, "setObject:forKey:", dict, ns_string(kBookmarksKey));
    call(dict, "release");
}

// Gives back the access to a file picked in an earlier run: its path now (it may have moved or
// been renamed), or "" without a bookmark that still resolves.
std::string resume_bookmark(const std::string& path) {
    id dict = bookmarks();
    id data = dict ? send(dict, "objectForKey:", ns_string(path)) : nullptr;
    if (!data) return "";
    BOOL stale = NO;
    id url = reinterpret_cast<id (*)(id, SEL, id, unsigned long, id, BOOL*, id*)>(objc_msgSend)(
        cls("NSURL"), sel_registerName("URLByResolvingBookmarkData:options:relativeToURL:bookmarkDataIsStale:error:"), data,
        0UL, nullptr, &stale, nullptr);
    if (!url) return "";
    // Kept for the rest of the run: the disc image is read while the game runs.
    ask(url, "startAccessingSecurityScopedResource");
    std::string now = utf8(send(url, "path"));
    if (now.empty()) return "";
    if (stale || now != path) store_bookmark(url, now, path);
    return now;
}

// The picker on screen (one at a time) and who gets its answer. The delegate lives for the whole
// run: the picker only holds it weakly.
std::function<void(std::string)> g_done;
bool g_picking = false;

void picked(std::string path) {
    g_picking = false;
    auto done = std::exchange(g_done, nullptr);
    if (done) done(std::move(path));
}

// UIDocumentPickerDelegate
void did_pick(id, SEL, id, id urls) {
    std::string path;
    if (id url = urls ? send(urls, "firstObject") : nullptr) {
        // Readable only from here on, for the rest of the run (as in resume_bookmark).
        ask(url, "startAccessingSecurityScopedResource");
        path = utf8(send(url, "path"));
        // The app's own folder needs no bookmark (rebase_app_container_path finds it again).
        if (!path.empty() && path.find("/Containers/Data/Application/") == std::string::npos) store_bookmark(url, path);
    }
    picked(std::move(path));
}
void was_cancelled(id, SEL, id) { picked(""); }

id picker_delegate() {
    static id delegate = [] {
        Class c = objc_allocateClassPair(objc_getClass("NSObject"), "GT3RDocumentPickerDelegate", 0);
        class_addMethod(c, sel_registerName("documentPicker:didPickDocumentsAtURLs:"), reinterpret_cast<IMP>(did_pick), "v@:@@");
        class_addMethod(c, sel_registerName("documentPickerWasCancelled:"), reinterpret_cast<IMP>(was_cancelled), "v@:@");
        if (Protocol* p = objc_getProtocol("UIDocumentPickerDelegate")) class_addProtocol(c, p);
        objc_registerClassPair(c);
        return send(send(reinterpret_cast<id>(c), "alloc"), "init");
    }();
    return delegate;
}

// The file types for the picker: one per extension in the filters' patterns ("iso;bin"), any
// file for "*".
id content_types(const SDL_DialogFileFilter* filters, int count) {
    id types = send(cls("NSMutableArray"), "array");
    for (int i = 0; i < count; ++i) {
        std::string pattern = filters[i].pattern ? filters[i].pattern : "";
        for (size_t at = 0; at <= pattern.size();) {
            size_t end = pattern.find(';', at);
            if (end == std::string::npos) end = pattern.size();
            std::string ext = pattern.substr(at, end - at);
            at = end + 1;
            if (ext.empty()) continue;
            id type = ext == "*" ? send(cls("UTType"), "typeWithIdentifier:", ns_string("public.data"))
                                 : send(cls("UTType"), "typeWithFilenameExtension:", ns_string(ext));
            if (type) call(types, "addObject:", type);
        }
    }
    return types;
}

} // namespace
#endif

bool show_document_picker(SDL_Window* window, const SDL_DialogFileFilter* filters, int count,
                          std::function<void(std::string path)> done) {
#if defined(SDL_PLATFORM_IOS)
    if (g_picking || !window) return false;
    id ui_window = static_cast<id>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr));
    id vc = ui_window ? send(ui_window, "rootViewController") : nullptr;
    while (vc && send(vc, "presentedViewController")) vc = send(vc, "presentedViewController");
    if (!vc) return false;
    // asCopy NO: the file is opened where it is (a copy would be another 3.9 GB in the app).
    id picker = reinterpret_cast<id (*)(id, SEL, id, BOOL)>(objc_msgSend)(
        send(cls("UIDocumentPickerViewController"), "alloc"), sel_registerName("initForOpeningContentTypes:asCopy:"),
        content_types(filters, count), NO);
    if (!picker) return false;
    call(picker, "setDelegate:", picker_delegate());
    call(picker, "setAllowsMultipleSelection:", NO);
    g_done = std::move(done);
    g_picking = true;
    reinterpret_cast<void (*)(id, SEL, id, BOOL, id)>(objc_msgSend)(
        vc, sel_registerName("presentViewController:animated:completion:"), picker, YES, nullptr);
    call(picker, "release"); // the presenting controller keeps it while it is shown
    return true;
#else
    (void)window, (void)filters, (void)count, (void)done;
    return false;
#endif
}

bool restore_disc_access(IniDocument& cfg, const std::string& data_dir) {
#if defined(SDL_PLATFORM_IOS)
    std::map<std::string, std::string> resumed; // [game] disc is also recent1: one access per file
    auto restore = [&](const std::string& saved) {
        const std::string path = rebase_app_container_path(saved, data_dir);
        auto [it, added] = resumed.try_emplace(path, path);
        if (added)
            if (std::string now = resume_bookmark(path); !now.empty()) it->second = now;
        return it->second;
    };
    bool changed = false;
    if (auto disc = cfg.get("game", "disc"); disc && !disc->empty())
        if (std::string path = restore(*disc); path != *disc) cfg.set("game", "disc", path), changed = true;
    const std::vector<std::string> list = load_recent_discs(cfg);
    std::vector<std::string> now;
    for (const std::string& p : list) now.push_back(restore(p));
    if (now != list) save_recent_discs(cfg, now), changed = true;
    return changed;
#else
    (void)cfg, (void)data_dir;
    return false;
#endif
}

} // namespace gt3r::platform
