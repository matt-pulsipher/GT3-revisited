#include "platform/aspect.h"

#include <algorithm>
#include <cmath>

namespace gt3r::platform {

Aspect parse_aspect(std::string_view value) {
    while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
    if (value == "4:3") return Aspect::Classic;
    if (value == "fill") return Aspect::Fill;
    return Aspect::Wide;
}

const char* aspect_value(Aspect a) {
    switch (a) {
    case Aspect::Classic: return "4:3";
    case Aspect::Fill: return "fill";
    default: return "16:9";
    }
}

int game_width(Aspect a, int pixel_w, int pixel_h) {
    switch (a) {
    case Aspect::Classic: return kClassicWidth;
    case Aspect::Fill:
        if (pixel_w <= 0 || pixel_h <= 0) return kWideWidth;
        return std::clamp(int(std::lround(double(kGameHeight) * pixel_w / pixel_h)), kClassicWidth, kMaxFillWidth);
    default: return kWideWidth;
    }
}

} // namespace gt3r::platform
