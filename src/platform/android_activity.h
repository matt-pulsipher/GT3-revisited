#pragma once

// Android: calling GT3Activity (android/app/src/main/java/.../GT3Activity.java) from native
// code, on the game's thread.

#include <SDL3/SDL.h>

#if defined(SDL_PLATFORM_ANDROID)
#include <jni.h>

#include <string>

namespace gt3r::platform {

// Calls GT3Activity's method `name` with JNI signature `sig`; `call` gets the env, activity and
// method and returns the result (R{} when the method or the activity is missing).
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

// A Java string's UTF-8 ("" for null), releasing the reference.
inline std::string take_java_string(JNIEnv* env, jstring s) {
    std::string out;
    if (s && !env->ExceptionCheck())
        if (const char* c = env->GetStringUTFChars(s, nullptr)) {
            out = c;
            env->ReleaseStringUTFChars(s, c);
        }
    if (s) env->DeleteLocalRef(s);
    return out;
}

} // namespace gt3r::platform
#endif
