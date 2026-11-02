// gt3tool: inspect and extract data from a user-supplied Gran Turismo 3 disc
// image. Output is for personal use (modding, research); never redistribute
// extracted game data. Commands live in cmd_*.cpp and register themselves.

#include "tool.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>

namespace gt3r::tool {

std::vector<Command>& commands() {
    static std::vector<Command> c;
    return c;
}

bool save(const std::string& path, ByteSpan data) {
    for (size_t i = 1; i < path.size(); ++i)
        if (path[i] == '/') SDL_CreateDirectory(path.substr(0, i).c_str());
    if (!write_host_file(path, data)) {
        std::fprintf(stderr, "failed to write %s\n", path.c_str());
        return false;
    }
    return true;
}

const Volume::Entry& need(Volume& vol, const std::string& path) {
    const Volume::Entry* e = vol.find(path);
    if (!e) throw FormatError("not found in GT3.VOL: " + path);
    return *e;
}

} // namespace gt3r::tool

using namespace gt3r;
using namespace gt3r::tool;

static int usage() {
    std::puts("usage: gt3tool <command> <disc.iso|disc.chd> [args]\n");
    auto cmds = commands();
    std::sort(cmds.begin(), cmds.end(), [](const Command& a, const Command& b) { return std::string(a.name) < b.name; });
    for (const Command& c : cmds) {
        std::string head = std::string(c.name) + " <iso> " + c.usage;
        std::printf("  %-44s %s\n", head.c_str(), c.help);
    }
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    std::string name = argv[1];
    for (const Command& c : commands()) {
        if (name != c.name) continue;
        Args args(argv + 3, argv + argc);
        if (args.size() < c.min_args) return usage();
        try {
            auto disc = Disc::open(argv[2]);
            return c.run(*disc, args);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
    }
    return usage();
}
