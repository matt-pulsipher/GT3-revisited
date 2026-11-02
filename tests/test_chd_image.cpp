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

std::string fixture(const char* name) {
    const auto path = std::filesystem::path(GT3R_SOURCE_DIR) / "tests" / "data" / name;
    if (!std::filesystem::exists(path)) SKIP("CHD fixture missing; run tests/data/make_chd_fixtures.sh");
    return path.string();
}

// The FormatError message `fn` throws, "" when it does not throw.
std::string error_of(const std::function<void()>& fn) {
    try {
        fn();
    } catch (const FormatError& e) {
        return e.what();
    }
    return "";
}

const char kNotGt3[] = "This does not look like a Gran Turismo 3 disc image (CORE.GT3 / GT3.VOL missing).";

void check_image(const char* name) {
    const std::string path = fixture(name);
    auto file = File::open(path);
    CHECK(file != nullptr);
    auto image = ChdImage::open(std::move(file));
    const Bytes expected = pattern();
    CHECK(image->path() == path);
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

// The small file system of make_chd_fixtures.sh in each layout chdman makes: the sectors are the track's data
// (not chdman's padding frames, not a stored pregap), at each track type's offset, and the dual-layer probe past
// the volume's end (a padding frame, a Form 2 postgap sector) finds no second layer instead of failing.
TEST(chd_file_system_layouts) {
    const std::pair<const char*, u64> cases[] = {{"synthetic-fs-dvd.chd", 37},
                                                 {"synthetic-fs-mode1.chd", 37},
                                                 {"synthetic-fs-mode2.chd", 40},
                                                 {"synthetic-fs-pregap.chd", 40}};
    for (const auto& [name, sectors] : cases) {
        std::unique_ptr<ChdImage> image = ChdImage::open(File::open(fixture(name)));
        CHECK(image->size() == sectors * 2048);
        std::vector<std::unique_ptr<Iso9660>> layers = iso_layers(*image);
        CHECK(layers.size() == 1);
        CHECK(layers[0]->volume_id() == "GT3R_CHD_TEST");
        CHECK(layers[0]->volume_bytes() == 37 * 2048);
        std::optional<Iso9660::Entry> readme = layers[0]->find("README.TXT");
        CHECK(readme.has_value());
        const Bytes text = layers[0]->read(*readme);
        CHECK(std::string(text.begin(), text.end()) == "GT3 Revisited CHD test file\n");
        // Through Disc::open the image gets as far as looking for the game's files.
        CHECK(error_of([&] { Disc::open(fixture(name)); }) == kNotGt3);
    }
}

// Disc::open tells a CHD by its header, whatever the name (an Android content:// URI often has no extension).
TEST(chd_found_by_content) {
    const std::string chd = test::temp_path("disc.iso").string();
    std::filesystem::copy_file(fixture("synthetic-fs-dvd.chd"), chd);
    CHECK(ChdImage::is_chd(*File::open(chd)));
    CHECK(error_of([&] { Disc::open(chd); }) == kNotGt3);

    const std::string iso = test::temp_path("plain.chd").string();
    CHECK(write_host_file(iso, ChdImage::open(File::open(chd))->read_at(0, 37 * 2048)));
    CHECK(!ChdImage::is_chd(*File::open(iso)));
    CHECK(error_of([&] { Disc::open(iso); }) == kNotGt3);
}

TEST(chd_not_a_disc_image) {
    const std::string path = test::temp_path("tiny").string();
    CHECK(write_host_file(path, Bytes{'M', 'C'}));
    CHECK(!ChdImage::is_chd(*File::open(path)));
}
