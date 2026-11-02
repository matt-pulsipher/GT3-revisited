#include "platform/music_folder.h"

#include "platform/android_activity.h"
#include "platform/ios_files.h"

#include <SDL3/SDL.h>

#include <memory>
#include <mutex>

namespace gt3r::platform {

namespace {

// The picker's answer, handed from whichever thread gives it (SDL's dialog thread, the main
// thread on iOS) to the game's thread.
struct Pick {
    std::mutex mutex;
    std::optional<FolderAnswer> answer;
};
std::shared_ptr<Pick> g_pick; // the open picker's (game's thread)

#if !defined(SDL_PLATFORM_ANDROID)
void answer(const std::shared_ptr<Pick>& pick, FolderAnswer a) {
    std::lock_guard lock(pick->mutex);
    pick->answer = std::move(a);
}
#endif

#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
void SDLCALL on_folder(void* user, const char* const* files, int) {
    auto* slot = static_cast<std::shared_ptr<Pick>*>(user);
    FolderAnswer a;
    if (files && files[0]) a = {FolderAnswer::Picked, files[0]};
    else if (!files) a.kind = FolderAnswer::Failed;
    answer(*slot, std::move(a));
    delete slot;
}
#endif

#if defined(SDL_PLATFORM_ANDROID)
// GT3Activity.takeFolderPick: null until answered, then "P<path>", "T<tree URI>" (a save
// folder), "C" (cancelled), "A" (no access), "E" (another app's provider), "K" (its access not
// kept) or "F" (no picker).
std::optional<FolderAnswer> android_answer() {
    bool waiting = true;
    std::string s = with_activity_method("takeFolderPick", "()Ljava/lang/String;", [&](JNIEnv* env, jobject act, jmethodID m) {
        auto r = static_cast<jstring>(env->CallObjectMethod(act, m));
        waiting = !r;
        return take_java_string(env, r);
    });
    if (waiting) return std::nullopt;
    FolderAnswer a;
    switch (s.empty() ? 'F' : s[0]) {
    case 'P':
    case 'T': a = {FolderAnswer::Picked, s.substr(1)}; break;
    case 'C': a.kind = FolderAnswer::Cancelled; break;
    case 'A': a.kind = FolderAnswer::NoAccess; break;
    case 'E': a.kind = FolderAnswer::Elsewhere; break;
    case 'K': a.kind = FolderAnswer::NotKept; break;
    default: a.kind = FolderAnswer::Failed; break;
    }
    return a;
}
#endif

} // namespace

bool start_folder_pick(SDL_Window* window, const std::string& start, FolderUse use) {
    if (g_pick) return false;
    auto pick = std::make_shared<Pick>();
#if defined(SDL_PLATFORM_ANDROID)
    (void)window;
    const bool shown =
        use == FolderUse::Saves
            ? with_activity_method("pickSaveFolder", "(Ljava/lang/String;)Z",
                                   [&](JNIEnv* env, jobject act, jmethodID m) {
                                       jstring s = env->NewStringUTF(start.c_str());
                                       const bool ok = env->CallBooleanMethod(act, m, s);
                                       env->DeleteLocalRef(s);
                                       return ok;
                                   })
            : with_activity_method("pickMusicFolder", "()Z",
                                   [](JNIEnv* env, jobject act, jmethodID m) { return bool(env->CallBooleanMethod(act, m)); });
    if (!shown) return false;
#elif defined(SDL_PLATFORM_IOS)
    (void)start, (void)use;
    if (!show_folder_picker(window, [pick](std::string path) {
            answer(pick, path.empty() ? FolderAnswer{} : FolderAnswer{FolderAnswer::Picked, std::move(path)});
        }))
        return false;
#else
    (void)use;
    SDL_ShowOpenFolderDialog(on_folder, new std::shared_ptr<Pick>(pick), window, start.empty() ? nullptr : start.c_str(),
                             false);
#endif
    g_pick = std::move(pick);
    return true;
}

std::optional<FolderAnswer> take_folder_pick() {
    if (!g_pick) return std::nullopt;
    std::optional<FolderAnswer> a;
#if defined(SDL_PLATFORM_ANDROID)
    a = android_answer();
#else
    {
        std::lock_guard lock(g_pick->mutex);
        a = std::move(g_pick->answer);
    }
#endif
    if (a) g_pick.reset();
    return a;
}

bool music_access(const std::string& folder, const std::string& data_dir) {
#if defined(SDL_PLATFORM_ANDROID)
    if (!data_dir.empty() && folder.rfind(data_dir, 0) == 0) return true;
    return with_activity_method("hasMusicAccess", "()Z",
                                [](JNIEnv* env, jobject act, jmethodID m) { return bool(env->CallBooleanMethod(act, m)); });
#else
    (void)folder, (void)data_dir;
    return true;
#endif
}

bool open_app_settings() {
#if defined(SDL_PLATFORM_ANDROID)
    return with_activity_method("openAppSettings", "()Z",
                                [](JNIEnv* env, jobject act, jmethodID m) { return bool(env->CallBooleanMethod(act, m)); });
#else
    return false;
#endif
}

} // namespace gt3r::platform
