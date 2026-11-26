// An app's own controls (input/extra_controls.h): binding targets beyond the DualShock 2's
// buttons, named by the app, with defaults in the built-in layouts, read from the player's pads.

#include "test_framework.h"

#include "base/config.h"
#include "input/bindings.h"
#include "input/extra_controls.h"
#include "input/input_manager.h"

#include <SDL3/SDL.h>

#include <algorithm>

using namespace gt3r;

namespace {

bool has(const Profile& p, const PadTarget& t, std::string_view source) {
    return std::any_of(p.bindings.begin(), p.bindings.end(), [&](const Binding& b) { return b.target == t && b.source.to_string() == source; });
}

} // namespace

TEST(input_extra_controls) {
    clear_extra_controls();
    // The targets by name, as config lines write them.
    PadTarget t;
    CHECK(PadTarget::parse("Extra2", t) && t == extra_control(1) && t.to_string() == "Extra2");
    CHECK(!PadTarget::parse("Extra5", t) && !PadTarget::parse("Extra0", t));
    CHECK(extra_control_name(0).empty());
    name_extra_control(0, "Boost");
    CHECK(extra_control_name(0) == "Boost");

    // None by default: GT3 Revisited's layouts are as they were.
    const size_t pad_bindings = make_race_preset("modern", "pad").bindings.size();
    const size_t wheel_bindings = make_preset("wheel").bindings.size();

    // An app's defaults: in their layout only, keys in every layout.
    add_extra_control_default(0, "wheel", true, "joy.button6");
    add_extra_control_default(0, "keyboard", true, "key.B");
    const Profile wheel_race = make_race_preset("modern", "wheel");
    CHECK(has(wheel_race, extra_control(0), "joy.button6") && has(wheel_race, extra_control(0), "key.B"));
    const Profile pad_race = make_race_preset("modern", "pad");
    CHECK(!has(pad_race, extra_control(0), "joy.button6") && has(pad_race, extra_control(0), "key.B"));
    CHECK(pad_race.bindings.size() == pad_bindings + 1 && make_preset("wheel").bindings.size() == wheel_bindings);
    CHECK(make_race_preset("gt3", "wheel").bindings.empty());

    // Read from the race pad while its input is down, like a button.
    auto key = [](SDL_Scancode sc) {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.scancode = sc;
        return e;
    };
    {
        InputManager in;
        in.set_device_filter(0, InputManager::kKeyboardOnly); // (whatever else is connected, or attached by other tests)
        in.update(1.0 / 60);
        CHECK(!in.race_pad(0).extra_down(0));
        in.handle_event(key(SDL_SCANCODE_B));
        in.update(1.0 / 60);
        CHECK(in.race_pad(0).extra_down(0) && !in.race_pad(0).extra_down(1) && !in.pad(0).extra_down(0));
        in.update(1.0 / 60);
        CHECK(!in.race_pad(0).extra_down(0));
        // Bound by hand like a button, and saved with the player's bindings.
        InputSource n;
        CHECK(InputSource::parse("key.N", n));
        in.bind_race(0, extra_control(1), n, true);
        IniDocument cfg;
        in.save(cfg);
        InputManager again;
        again.load(cfg);
        again.set_device_filter(0, InputManager::kKeyboardOnly);
        again.handle_event(key(SDL_SCANCODE_N));
        again.update(1.0 / 60);
        CHECK(again.race_pad(0).extra_down(1));
    }
    clear_extra_controls();
    CHECK(make_race_preset("modern", "pad").bindings.size() == pad_bindings && extra_control_name(0).empty());
}
