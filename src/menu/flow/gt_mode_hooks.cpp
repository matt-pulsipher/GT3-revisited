#include "menu/flow/gt_mode_hooks.h"

namespace gt3r::menu::flow {

namespace {
GtModeHooks& hooks() {
    static GtModeHooks h;
    return h;
}
GtModeWidgetDrawer& drawer() {
    static GtModeWidgetDrawer d;
    return d;
}
GtModeWidgetRelease& release() {
    static GtModeWidgetRelease r;
    return r;
}
GtModeWidgetRelease& trim() {
    static GtModeWidgetRelease t;
    return t;
}
} // namespace

void set_gt_mode_hooks(GtModeHooks h) { hooks() = std::move(h); }
const GtModeHooks& gt_mode_hooks() { return hooks(); }

void set_gt_mode_widget_drawer(GtModeWidgetDrawer d) { drawer() = std::move(d); }
const GtModeWidgetDrawer& gt_mode_widget_drawer() { return drawer(); }

void set_gt_mode_widget_release(GtModeWidgetRelease f) { release() = std::move(f); }
const GtModeWidgetRelease& gt_mode_widget_release() { return release(); }

void set_gt_mode_widget_trim(GtModeWidgetRelease f) { trim() = std::move(f); }
const GtModeWidgetRelease& gt_mode_widget_trim() { return trim(); }

} // namespace gt3r::menu::flow
