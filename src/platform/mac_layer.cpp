#include "platform/mac_layer.h"

#include <SDL3/SDL.h>

#if defined(SDL_PLATFORM_MACOS)
#include <objc/message.h>
#include <objc/runtime.h>

extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void* pool);
#endif

namespace gt3r::platform {

#if defined(SDL_PLATFORM_MACOS)
namespace {

id send(id obj, const char* sel) { return reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(obj, sel_registerName(sel)); }

int mark_layers(id layer, Class metal_layer) {
    if (!layer) return 0;
    int n = 0;
    if (metal_layer && reinterpret_cast<BOOL (*)(id, SEL, Class)>(objc_msgSend)(layer, sel_registerName("isKindOfClass:"), metal_layer)) {
        reinterpret_cast<void (*)(id, SEL, BOOL)>(objc_msgSend)(layer, sel_registerName("setOpaque:"), NO);
        ++n;
    }
    id subs = send(layer, "sublayers");
    unsigned long count = subs ? reinterpret_cast<unsigned long (*)(id, SEL)>(objc_msgSend)(subs, sel_registerName("count")) : 0;
    for (unsigned long i = 0; i < count; ++i)
        n += mark_layers(reinterpret_cast<id (*)(id, SEL, unsigned long)>(objc_msgSend)(subs, sel_registerName("objectAtIndex:"), i), metal_layer);
    return n;
}

int mark_views(id view, Class metal_layer) {
    if (!view) return 0;
    int n = mark_layers(send(view, "layer"), metal_layer);
    id subs = send(view, "subviews");
    unsigned long count = subs ? reinterpret_cast<unsigned long (*)(id, SEL)>(objc_msgSend)(subs, sel_registerName("count")) : 0;
    for (unsigned long i = 0; i < count; ++i)
        n += mark_views(reinterpret_cast<id (*)(id, SEL, unsigned long)>(objc_msgSend)(subs, sel_registerName("objectAtIndex:"), i), metal_layer);
    return n;
}

// [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory]
void make_accessory() {
    id app = send(reinterpret_cast<id>(objc_getClass("NSApplication")), "sharedApplication");
    if (app) reinterpret_cast<BOOL (*)(id, SEL, long)>(objc_msgSend)(app, sel_registerName("setActivationPolicy:"), 1);
}

} // namespace
#endif

void run_without_dock_icon() {
#if defined(SDL_PLATFORM_MACOS)
    // SDL makes NSApp and has it finish launching inside SDL_Init: the policy is set as it does,
    // before the app checks in with the system as a regular one.
    void* pool = objc_autoreleasePoolPush();
    id center = send(reinterpret_cast<id>(objc_getClass("NSNotificationCenter")), "defaultCenter");
    id name = reinterpret_cast<id (*)(id, SEL, const char*)>(objc_msgSend)(
        reinterpret_cast<id>(objc_getClass("NSString")), sel_registerName("stringWithUTF8String:"),
        "NSApplicationWillFinishLaunchingNotification");
    void (^on_launch)(id) = ^(id) { make_accessory(); };
    reinterpret_cast<id (*)(id, SEL, id, id, id, void*)>(objc_msgSend)(
        center, sel_registerName("addObserverForName:object:queue:usingBlock:"), name, nil, nil, (void*)on_launch);
    objc_autoreleasePoolPop(pool);
#endif
}

void keep_composited(SDL_Window* window) {
#if defined(SDL_PLATFORM_MACOS)
    id ns_window = static_cast<id>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr));
    if (!ns_window) return;
    int n = mark_views(send(ns_window, "contentView"), objc_getClass("CAMetalLayer"));
    if (n == 0) SDL_Log("keep_composited: no Metal layer found");
#else
    (void)window;
#endif
}

} // namespace gt3r::platform
