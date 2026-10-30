#pragma once

// The car settings menu (menu/setting.mbl; gt_mode §14):
// sliders over the car setup record's setting words of a garage car, the part
// change pages (swap between owned parts) and the rules that lock settings
// the fitted parts do not allow.
//
// Sliders work on a working copy of the car; ranges and the initial value
// come from the committed ("real") car, as in the original.

#include "game/gt_mode.h"

#include <optional>
#include <string>

namespace gt3r::game {

struct SliderRange {
    int begin = 0, end = 1; // values begin .. end - 1 (the marker scale)
    int value = 0;          // slider position
    int sub_begin = 0, sub_end = 0; // gear sliders: the clamp range (inclusive .. exclusive); 0/0 = none
    bool fixed() const { return end - begin < 2; }
    int lo() const { return sub_end > sub_begin ? sub_begin : begin; }
    int hi() const { return (sub_end > sub_begin ? sub_end : end) - 1; }
};

// Range and current position of a slider item ("slider_rate_f", ...) for a car.
// `real` gives the ranges, `work` the current value. nullopt = unknown item.
std::optional<SliderRange> slider_range(const GtMode& gt, const GarageCar& real, const GarageCar& work,
                                        const std::string& item);

// Moves a slider of the working copy to `position` (already clamped by the
// caller). Returns false when the change is refused (the car would fail the
// event's entry rules; `event` empty = no event): the old value is kept.
bool slider_set(const GtMode& gt, const GarageCar& real, GarageCar& work, const std::string& item, int position,
                const std::string& event);

// Text of a value widget ("spring_rate_f" -> "7.2kgf/mm", "gear_ratio_3" -> "1.685", "----" when locked).
std::optional<std::string> setting_text(const GtMode& gt, const GarageCar& real, const GarageCar& work,
                                        const std::string& widget_class);

// Text of a part row of the change-parts pages ("parts_eng_muffler" -> "Muffler/Sports",
// GtData::part_texts; gt_mode §14), "----" without a car. `dirt_event`: the menu's event needs
// dirt tyres (the tyre rows then show the dirt tyres when owned, else "----"). nullopt = not a
// part row class.
std::optional<std::string> setting_parts_text(const GtMode& gt, const GarageCar* car, const std::string& widget_class,
                                              bool dirt_event);

// Is the setting behind a slider item unlocked by the car's fitted parts?
bool slider_enabled(const GtMode& gt, const GarageCar& real, const std::string& item);

// The status-box predicates: can item `index` of a set/adjast_* page be entered?
bool setting_page_item_enabled(const GtMode& gt, const GarageCar& real, const std::string& page, int index);
// The predicate behind a "disable_*" overlay class (true = the setting is locked).
bool setting_locked(const GtMode& gt, const GarageCar& real, const std::string& disable_class);

// Part change pages: the goods a "set_*" action fits (-1 = unknown). Rear tyre
// actions map to the rear goods; their ownership is tested on the front goods.
int setting_goods(const std::string& set_action);
// The goods whose ownership enables a set_* item.
int setting_owned_goods(const std::string& set_action);
// "attach_*" marker classes: is that part fitted on the car?
bool setting_attached(const GtMode& gt, const GarageCar& car, const std::string& attach_class);

// Setting files (root page "Save Settings" / "Load Settings").
// Saves the car's committed settings as a new file (at most kSettingFilesPerCar
// per car: the oldest of that car is dropped).
void save_setting_file(GameState& s, const GarageCar& car);
// The save page (Load / Save Settings): writes the car's settings over file `index`
// (Overwrite) or as a new file (index < 0, Save), with the info fields; deletes one.
void write_setting_file(GameState& s, const GarageCar& car, int index, s64 time, int power_ps);
bool delete_setting_file(GameState& s, int index);
// The newest file for a car, or nullptr.
const SettingFile* latest_setting_file(const GameState& s, const std::string& car);
// Applies a file to a car: -1 when it is for another car (nothing applied);
// otherwise each settings group (gear, drivetrain, brake controller, body,
// turbo, suspension, LSD, ASCC, TCSC) is copied only if the file's part for it
// is the fitted part; returns the number of groups skipped ("load_warning").
int apply_setting_file(const SettingFile& f, GarageCar& car);

} // namespace gt3r::game
