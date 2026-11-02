#include "test_framework.h"

#include "disc/chd_image.h"

#include <algorithm>
#include <future>
#include <limits>

using namespace gt3r;

namespace {

Bytes pattern() {
    Bytes b(512 * 1024);
    for (size_t i = 0; i < b.size(); ++i)
        b[i] = (i / 4096) % 3 == 0 ? 0 : u8((i * 7 + i / 2048 + 1) & 255);
    return b;
}

void check_image(const char* name) {
    const auto path = std::filesystem::path(GT3R_SOURCE_DIR) / "tests" / "data" / name;
    if (!std::filesystem::exists(path)) SKIP("CHD fixture missing; run tests/data/make_chd_fixtures.sh");
    auto file = File::open(path.string());
    CHECK(file != nullptr);
    auto image = ChdImage::open(std::move(file));
    const Bytes expected = pattern();
    CHECK(image->path() == path.string());
    CHECK(image->size() == expected.size());
    CHECK(image->read_at(0, expected.size()) == expected);
    // Boundary reads and cache revisits must not return stale bytes.
    for (u64 offset : {1, 2039, 4093, 130999, 262131, 500003, 51}) {
        const size_t len = 12347;
        Bytes got(len);
        image->read_at(offset, got.data(), got.size());
        CHECK(std::equal(got.begin(), got.end(), expected.begin() + offset));
    }
    CHECK(image->read_at(image->size() - 37, 37) == Bytes(expected.end() - 37, expected.end()));
    CHECK(image->read_at(image->size(), 0).empty());
    std::vector<std::future<bool>> jobs;
    for (size_t thread = 0; thread < 4; ++thread) {
        jobs.push_back(std::async(std::launch::async, [&, thread] {
            for (size_t i = 0; i < 40; ++i) {
                const size_t offset = (thread * 130999 + i * 19937) % (expected.size() - 8193);
                const Bytes got = image->read_at(offset, 8193);
                if (!std::equal(got.begin(), got.end(), expected.begin() + offset)) return false;
            }
            return true;
        }));
    }
    for (auto& job : jobs) CHECK(job.get());
    Reader& reader = *image;
    for (const auto& [offset, len] : std::vector<std::pair<u64, size_t>>{
             {image->size() - 1, 2}, {image->size(), 1}, {image->size() + 1, 0},
             {std::numeric_limits<u64>::max(), 8}, {1, std::numeric_limits<size_t>::max()}}) {
        bool threw = false;
        try {
            reader.read_at(offset, len); // Invalid lengths must fail before allocation.
        } catch (const FormatError&) {
            threw = true;
        }
        CHECK(threw);
        u8 b = 0;
        threw = false;
        try {
            image->read_at(offset, &b, len);
        } catch (const FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }
}

} // namespace

TEST(chd_dvd_byte_exact) { check_image("synthetic.chd"); }
TEST(chd_cd_byte_exact) { check_image("synthetic-cd.chd"); }

TEST(chd_open_error) {
    const auto path = test::temp_path("invalid.chd").string();
    CHECK(write_host_file(path, Bytes(128, 0)));
    bool threw = false;
    try {
        ChdImage::open(File::open(path));
    } catch (const FormatError& e) {
        const std::string message = e.what();
        CHECK(message.find("Could not open CHD") != std::string::npos);
        CHECK(message.find(path) != std::string::npos);
        threw = true;
    }
    CHECK(threw);
}

TEST(chd_disc_extension_case_insensitive) {
    const auto fixture = std::filesystem::path(GT3R_SOURCE_DIR) / "tests/data/synthetic.chd";
    if (!std::filesystem::exists(fixture)) SKIP("CHD fixture missing; run tests/data/make_chd_fixtures.sh");
    const auto path = test::temp_path("synthetic.ChD");
    std::filesystem::copy_file(fixture, path);
    bool threw = false;
    try {
        Disc::open(path.string());
    } catch (const FormatError& e) {
        // ISO probing must be reached even with a mixed-case CHD extension.
        CHECK(std::string(e.what()) == "no ISO9660 primary volume descriptor");
        threw = true;
    }
    CHECK(threw);
}
