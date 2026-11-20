#pragma once

// The Gran Turismo Concept disc images the concept_disc_* tests run on: GT3R_DISC_CONCEPT, paths separated by ';'
// (none: those tests skip).

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace gt3r::test {

inline std::vector<std::string> concept_discs() {
    std::vector<std::string> out;
    const char* list = std::getenv("GT3R_DISC_CONCEPT");
    if (!list) return out;
    std::istringstream in(list);
    for (std::string path; std::getline(in, path, ';');)
        if (!path.empty()) out.push_back(path);
    return out;
}

} // namespace gt3r::test
