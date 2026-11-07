// Course viewer: course scenery (GTM1) from the disc region's course file, with the
// AI racing line and the road centre line drawn as ribbons on the road.

#include "app/app.h"
#include "app/screen_model_viewer.h"
#include "app/screen_registry.h"
#include "formats/course/course_model.h"

namespace gt3r {

namespace {

// Race courses first (r_ = reverse layout), then license-test variants (L_).
const char* const kCourses[] = {
    "trialmt", "r_trialmt", "deepforest", "r_deepforest", "grandvalley", "r_grandvalley", "laguna", "midfield",
    "r_midfield", "apricot", "r_apricot", "smtnorth", "r_smtnorth", "smtsouth", "r_smtsouth", "seattle", "r_seattle",
    "rome", "r_rome", "route5", "r_route5", "route5_dry", "r_route5_dry", "route11", "r_route11", "akasaka",
    "r_akasaka", "montecarlo", "tahitidirt", "r_tahitidirt", "tahitimaze", "r_tahitimaze", "slalom80r", "r_slalom80r",
    "super", "testcourse", "L_akasaka1", "L_akasaka2", "L_apricot1", "L_apricot2", "L_circle100_dry1",
    "L_circle100_wet1", "L_circle60_dry1", "L_circle60_wet1", "L_deepforest1", "L_deepforest1a", "L_deepforest1b",
    "L_deepforest2", "L_deepforest2a", "L_deepforest2b", "L_grandvalley1", "L_grandvalley2", "L_grandvalley2a",
    "L_grandvalley2b", "L_grandvalley3", "L_laguna1", "L_laguna2", "L_midfield1", "L_midfield2", "L_midfield3",
    "L_rome1", "L_seattle1", "L_seattle2", "L_seattle3", "L_slalom80r1", "L_slalom80r2", "L_slalom80r3",
    "L_slalom80r4", "L_smtnorth1", "L_smtsouth1", "L_smtsouth2", "L_tahitidirt1", "L_tahitidirt2", "L_tahitimaze1",
    "L_tahitimaze2", "L_testcourse1", "L_testcourse2", "L_testcourse3", "L_trialmt1", "L_trialmt2",
};

// `lines`: the AI racing line and road centre line ribbons (shown, not exported).
scene::Model load_course_model(App& app, size_t i, bool lines) {
    Vfs* vfs = app.vfs();
    if (!vfs) throw FormatError("no disc loaded");
    std::string name = kCourses[i];
    std::string path = course_file_path(*vfs, name);
    if (auto data = path.empty() ? std::nullopt : vfs->read(path)) {
        course::CourseModelOptions opt;
        opt.ai_lines = lines;
        return course::build_course_model(*data, opt, name);
    }
    throw FormatError("course not found: " + name);
}

} // namespace

GT3R_REGISTER_SCREEN(courses, "courses", "Course viewer",
                     "Course scenery with the AI racing line (red) and road centre line (cyan).", 55, [] {
                         std::vector<std::string> names(std::begin(kCourses), std::end(kCourses));
                         return make_model_viewer(
                             "Courses", std::move(names), [](App& app, size_t i) { return load_course_model(app, i, true); },
                             {"courses", [](App& app, size_t i) { return load_course_model(app, i, false); }});
                     });

} // namespace gt3r
