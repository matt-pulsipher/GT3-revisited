#include "platform/document_tree.h"

#include "platform/android_activity.h"

#include <SDL3/SDL_error.h>

#include <cstdlib>

namespace gt3r::platform {

namespace {

constexpr std::string_view kScheme = "content://";

// Where the tree URI ends in `path` ("content://<authority>/tree/<folder id>"; the id is
// percent-encoded, so it holds no '/'), or npos when `path` is not in a tree.
size_t tree_end(std::string_view path) {
    if (path.substr(0, kScheme.size()) != kScheme) return std::string_view::npos;
    const size_t auth_end = path.find('/', kScheme.size());
    if (auth_end == std::string_view::npos || path.substr(auth_end, 6) != "/tree/") return std::string_view::npos;
    const size_t id = auth_end + 6;
    size_t end = path.find('/', id);
    if (end == std::string_view::npos) end = path.size();
    if (end == id) return std::string_view::npos;
    // A document of the tree (what a picker hands over), not a name in it.
    if (path.substr(end, 10) == "/document/") return std::string_view::npos;
    return end;
}

} // namespace

bool is_document_tree_path(std::string_view path) { return tree_end(path) != std::string_view::npos; }

std::string document_tree_of(std::string_view path) {
    const size_t end = tree_end(path);
    return end == std::string_view::npos ? std::string() : std::string(path.substr(0, end));
}

#if defined(SDL_PLATFORM_ANDROID)

namespace {

// Calls GT3Activity's document method `name`(String...) returning a String (null -> "").
std::string call_string(const char* name, const char* sig, const std::string& a) {
    return with_activity_method(name, sig, [&](JNIEnv* env, jobject act, jmethodID m) {
        jstring s = env->NewStringUTF(a.c_str());
        auto r = static_cast<jstring>(env->CallObjectMethod(act, m, s));
        env->DeleteLocalRef(s);
        return take_java_string(env, r);
    });
}

bool call_bool(const char* name, const char* sig, const std::string& a, const std::string* b = nullptr) {
    return with_activity_method(name, sig, [&](JNIEnv* env, jobject act, jmethodID m) {
        jstring s = env->NewStringUTF(a.c_str());
        jstring t = b ? env->NewStringUTF(b->c_str()) : nullptr;
        const bool ok = b ? env->CallBooleanMethod(act, m, s, t) : env->CallBooleanMethod(act, m, s);
        if (t) env->DeleteLocalRef(t);
        env->DeleteLocalRef(s);
        return ok;
    });
}

// "F" (file) or "D" (folder), the last change in milliseconds, ':', the size; then (lists)
// ':' and the name.
bool parse_entry(const std::string& s, HostDirEntry& e, bool named) {
    if (s.size() < 2 || (s[0] != 'F' && s[0] != 'D')) return false;
    e.dir = s[0] == 'D';
    char* end = nullptr;
    const long long ms = std::strtoll(s.c_str() + 1, &end, 10);
    if (!end || *end != ':') return false;
    e.mtime = s64(ms / 1000);
    const unsigned long long size = std::strtoull(end + 1, &end, 10);
    e.size = u64(size);
    if (!named) return *end == '\0';
    if (*end != ':') return false;
    e.name = end + 1;
    return !e.name.empty();
}

} // namespace

std::string document_uri(const std::string& path) {
    std::string uri = call_string("docUri", "(Ljava/lang/String;)Ljava/lang/String;", path);
    if (uri.empty()) SDL_SetError("%s is not there", path.c_str());
    return uri;
}

std::optional<DocumentInfo> document_info(const std::string& path) {
    const std::string s = call_string("docInfo", "(Ljava/lang/String;)Ljava/lang/String;", path);
    HostDirEntry e;
    if (!parse_entry(s, e, false)) return std::nullopt;
    return DocumentInfo{e.dir, e.mtime, e.size};
}

std::vector<HostDirEntry> document_entries(const std::string& dir) {
    return with_activity_method(
        "docList", "(Ljava/lang/String;)[Ljava/lang/String;", [&](JNIEnv* env, jobject act, jmethodID m) {
            std::vector<HostDirEntry> out;
            jstring s = env->NewStringUTF(dir.c_str());
            auto list = static_cast<jobjectArray>(env->CallObjectMethod(act, m, s));
            env->DeleteLocalRef(s);
            if (!list || env->ExceptionCheck()) return out;
            const jsize n = env->GetArrayLength(list);
            for (jsize i = 0; i < n; ++i) {
                HostDirEntry e;
                if (parse_entry(take_java_string(env, static_cast<jstring>(env->GetObjectArrayElement(list, i))), e, true))
                    out.push_back(std::move(e));
            }
            env->DeleteLocalRef(list);
            return out;
        });
}

std::string document_create(const std::string& path, bool dir) {
    std::string uri = with_activity_method(
        "docCreate", "(Ljava/lang/String;Z)Ljava/lang/String;", [&](JNIEnv* env, jobject act, jmethodID m) {
            jstring s = env->NewStringUTF(path.c_str());
            auto r = static_cast<jstring>(env->CallObjectMethod(act, m, s, jboolean(dir)));
            env->DeleteLocalRef(s);
            return take_java_string(env, r);
        });
    if (uri.empty()) SDL_SetError("cannot make %s", path.c_str());
    return uri;
}

bool document_remove(const std::string& path) {
    if (call_bool("docRemove", "(Ljava/lang/String;)Z", path)) return true;
    SDL_SetError("cannot remove %s", path.c_str());
    return false;
}

bool document_rename(const std::string& from, const std::string& to) {
    if (call_bool("docRename", "(Ljava/lang/String;Ljava/lang/String;)Z", from, &to)) return true;
    SDL_SetError("cannot rename %s to %s", from.c_str(), to.c_str());
    return false;
}

std::string document_tree_name(const std::string& tree) {
    return call_string("docTreeName", "(Ljava/lang/String;)Ljava/lang/String;", tree);
}

bool document_tree_access(const std::string& tree) { return call_bool("docTreeAccess", "(Ljava/lang/String;)Z", tree); }

void forget_document_tree(const std::string& tree) { call_bool("docForgetTree", "(Ljava/lang/String;)Z", tree); }

#endif

} // namespace gt3r::platform
