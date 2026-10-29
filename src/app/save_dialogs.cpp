// The platform file dialogs for the menus' save page (menu::flow::FileDialogs): pick a
// save to import / load as a guest garage (native .sav or any PS2 save container), or
// where to export a save for the PS2. The page chooses the export's format itself (.psu,
// .ps2 card image or raw save data) and names the file after it; the dialog only picks the
// place. SDL's dialogs are asynchronous and may answer on another thread; the answer waits
// in a mailbox the page polls. On macOS they are our own sheets on the game's window
// (platform/mac_dialog.h), which give the window its keyboard focus back when they close; on
// Android the system's document picker (content:// URIs).
// Each dialog has a number (its callback's userdata): the page can give one up
// (FileDialogs::cancel, its waiting window's CANCEL), and an answer it sends later is
// dropped. On macOS giving up also closes the sheet.
//
// Development: GT3R_FILE_DIALOG=<path> answers every dialog with that path at once
// (scripted screenshots, no window), "-" with a cancel.

#include "app/save_dialogs.h"

#include "app/app.h"
#include "base/log.h"
#include "menu/flow/save_page.h"
#include "platform/mac_dialog.h"
#include "platform/user_files.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace gt3r {

namespace {

std::mutex g_mutex;
std::optional<std::string> g_answer;
std::uintptr_t g_ticket = 0; // the open dialog's number; older ones' answers are dropped
bool g_export = false;       // the open dialog picks an export's place
bool g_native = false;       // the open dialog is the system's, over the window
// The folder of the last export (the save dialog starts there; macOS).
std::string g_export_dir;

const SDL_DialogFileFilter kOpenFilters[] = {
    {"Saves (GT3 Revisited, PS2)", "sav;psu;ps2;mcd;raw"},
    {"All files", "*"},
};

void answer(std::uintptr_t ticket, std::string path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (ticket == g_ticket) g_answer = std::move(path);
}

void SDLCALL on_dialog(void* ticket, const char* const* files, int) {
    if (!files) LOG_WARN("file dialog: %s", SDL_GetError()); // (taken as a cancel)
    answer(reinterpret_cast<std::uintptr_t>(ticket), files && files[0] ? std::string(files[0]) : std::string());
}

// macOS: while the dialog's sheet is up the game's window has no keyboard focus, and SDL drops
// a pad's presses without it. They are let through until the dialog ends, so the page's
// CANCEL works from the pad (the sheet takes the keyboard and the mouse itself).
void pad_during_dialog(InputManager& input, bool on) {
#if defined(SDL_PLATFORM_MACOS)
    input.hold_background(on);
#else
    (void)input, (void)on;
#endif
}

// The system's dialog is over (answered, or closed by the page): the pad needs the window's
// focus again (unless Controls -> background says otherwise), and on macOS the window gets it
// back (the sheet leaves no key window).
void dialog_over(SDL_Window* window, InputManager& input) {
    if (!std::exchange(g_native, false)) return;
    pad_during_dialog(input, false);
    platform::refocus_after_dialog(window);
}

} // namespace

void install_save_dialogs(App& app) {
    menu::flow::FileDialogs d;
    SDL_Window* window = app.window();
    InputManager* input = &app.input();
    d.show = [window, input](menu::flow::FileDialogs::Kind kind, const std::string& name) {
        std::uintptr_t ticket = 0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_answer.reset();
            ticket = ++g_ticket;
            g_export = kind == menu::flow::FileDialogs::Kind::ExportPs2;
        }
        if (const char* env = std::getenv("GT3R_FILE_DIALOG"); env && *env) {
            answer(ticket, std::string(env) == "-" ? std::string() : std::string(env));
            return;
        }
        g_native = true;
        pad_during_dialog(*input, true);
        const bool open = kind == menu::flow::FileDialogs::Kind::OpenSave;
#if defined(SDL_PLATFORM_MACOS)
        // Our own sheet (platform/mac_dialog.h: SDL's left the game without keyboard focus).
        // An export starts in the last export's folder, else Documents (a bare name would
        // start in the working directory: "/" for an app opened from the Finder).
        std::string folder = open ? std::string() : g_export_dir;
        if (!open && folder.empty())
            if (const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)) folder = docs;
        if (platform::show_file_sheet(window, !open, folder, name, [ticket](std::string path) { answer(ticket, std::move(path)); }))
            return;
#endif
        void* user = reinterpret_cast<void*>(ticket);
        if (open) // (no file types for an export: the page chose the format and the name's extension)
            SDL_ShowOpenFileDialog(on_dialog, user, window, kOpenFilters, SDL_arraysize(kOpenFilters), nullptr, false);
        else
            SDL_ShowSaveFileDialog(on_dialog, user, window, nullptr, 0, name.c_str());
    };
    d.poll = [window, input]() -> std::optional<std::string> {
        std::optional<std::string> a;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            a = std::exchange(g_answer, std::nullopt);
        }
        if (!a) return a;
        dialog_over(window, *input); // (a frame after SDL's callback: the sheet has gone)
        if (g_export && !a->empty() && a->rfind("content://", 0) != 0)
            if (size_t sl = a->find_last_of("/\\"); sl != std::string::npos) g_export_dir = a->substr(0, sl + 1);
        return a;
    };
    d.cancel = [window, input]() {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            ++g_ticket; // its answer, if one still comes, is dropped
            g_answer.reset();
        }
        platform::cancel_window_sheet(window); // (its cancel answers at once: dropped; the sheet is gone after it)
        dialog_over(window, *input);
    };
    d.name = [](const std::string& path) { return platform::picked_file_name(path); };
    menu::flow::set_file_dialogs(std::move(d));
}

} // namespace gt3r
