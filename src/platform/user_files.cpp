#include "platform/user_files.h"

#include "platform/recent_discs.h"

#include <SDL3/SDL.h>

#if defined(SDL_PLATFORM_ANDROID)
#include <jni.h>
#endif

namespace gt3r::platform {

#if defined(SDL_PLATFORM_ANDROID)
namespace {

// Calls a GT3Activity method taking one String; `call` gets the env, activity and method.
template <typename F>
auto with_activity_method(const char* name, const char* sig, F&& call) -> decltype(call(nullptr, nullptr, nullptr)) {
    using R = decltype(call(nullptr, nullptr, nullptr));
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return R{};
    R out{};
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = cls ? env->GetMethodID(cls, name, sig) : nullptr;
    if (m) out = call(env, activity, m);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return out;
}

} // namespace
#endif

bool open_user_folder(const std::string& dir, const std::string& data_dir) {
#if defined(SDL_PLATFORM_ANDROID)
    // GT3Activity.openUserFolder(relative path inside the user data folder).
    std::string rel = dir.rfind(data_dir, 0) == 0 ? dir.substr(data_dir.size()) : std::string();
    while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
    while (!rel.empty() && rel.back() == '/') rel.pop_back();
    return with_activity_method("openUserFolder", "(Ljava/lang/String;)Z", [&](JNIEnv* env, jobject act, jmethodID m) {
        jstring s = env->NewStringUTF(rel.c_str());
        const bool ok = env->CallBooleanMethod(act, m, s);
        env->DeleteLocalRef(s);
        return ok;
    });
#elif defined(SDL_PLATFORM_IOS)
    // The Files app at that folder: its shareddocuments:// scheme takes the folder's path, which
    // it shows because the app shares its Documents (the user data folder, UIFileSharingEnabled).
    (void)data_dir;
    return SDL_OpenURL(("shareddocuments://" + dir).c_str());
#else
    (void)data_dir;
    std::string url = "file://";
    if (!dir.empty() && dir[0] != '/') url += '/'; // Windows: file:///C:/...
    for (char c : dir) url += c == '\\' ? '/' : c;
    return SDL_OpenURL(url.c_str());
#endif
}

std::string picked_file_name(const std::string& path) {
#if defined(SDL_PLATFORM_ANDROID)
    if (path.rfind("content://", 0) == 0) {
        std::string name = with_activity_method("uriDisplayName", "(Ljava/lang/String;)Ljava/lang/String;",
                                                [&](JNIEnv* env, jobject act, jmethodID m) {
                                                    jstring s = env->NewStringUTF(path.c_str());
                                                    auto r = static_cast<jstring>(env->CallObjectMethod(act, m, s));
                                                    env->DeleteLocalRef(s);
                                                    std::string out;
                                                    if (r && !env->ExceptionCheck()) {
                                                        if (const char* c = env->GetStringUTFChars(r, nullptr)) {
                                                            out = c;
                                                            env->ReleaseStringUTFChars(r, c);
                                                        }
                                                    }
                                                    if (r) env->DeleteLocalRef(r);
                                                    return out;
                                                });
        if (!name.empty()) return name;
    }
#endif
    return disc_file_name(path);
}

} // namespace gt3r::platform
