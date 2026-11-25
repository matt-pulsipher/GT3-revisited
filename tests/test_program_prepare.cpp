// The open disc's program data, worked out as the disc opens (race::prepare_program_data, src/race/exact_setup.h): taken
// from there by its own disc only, once, and dropped as the disc closes. The disc-free tests use an empty GT3.VOL with a
// program of its own; the disc test (GT3R_DISC, any region) the disc's program.

#include "test_framework.h"

#include "base/reader.h"
#include "disc/disc.h"
#include "disc/volume.h"
#include "race/exact_setup.h"
#include "sim/exact/program_data.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cstring>
#include <string>

using namespace gt3r;

namespace {

// A GT3.VOL with no files: the header and a root directory of one child (its parent link).
class EmptyVolume : public Reader {
public:
    EmptyVolume() : bytes_(0x20, 0) {
        wr(0x00, Volume::kMagic);
        wr(0x08, u32(bytes_.size()));
        wr(0x14, 0x01000000); // a directory
        wr(0x18, 1);
    }
    u64 size() const override { return bytes_.size(); }
    const std::string& path() const override { return path_; }
    void read_at(u64 offset, void* out, size_t len) override {
        std::memset(out, 0, len);
        if (offset < bytes_.size()) std::memcpy(out, bytes_.data() + offset, std::min<size_t>(len, bytes_.size() - size_t(offset)));
    }

private:
    void wr(size_t at, u32 v) {
        for (size_t i = 0; i < 4; ++i) bytes_[at + i] = u8(v >> (8 * i));
    }
    Bytes bytes_;
    std::string path_ = "empty";
};

} // namespace

// A program that does not decode: the error is kept as the result is, and the program is not read again until the disc
// closes.
TEST(program_prepare_keeps_error) {
    EmptyVolume file;
    Volume vol(file, 0, file.size());
    int reads = 0;
    vol.set_program_reader([&reads] {
        ++reads;
        return Bytes(16, 0x5A);
    });
    Vfs vfs(&vol);
    race::prepare_program_data(vfs);
    CHECK(reads == 1);
    std::string first, second;
    CHECK(!race::exact_program_data(vfs, &first));
    CHECK(!race::exact_program_data(vfs, &second));
    CHECK(!first.empty() && first == second);
    CHECK(reads == 1); // both from the prepared result
    race::forget_program_data();
    std::string after;
    CHECK(!race::exact_program_data(vfs, &after));
    CHECK(after == first);
    CHECK(reads == 2); // worked out on use
}

// Another disc's Vfs never gets the prepared disc's result, and preparing a disc without a program prepares nothing.
TEST(program_prepare_is_per_disc) {
    EmptyVolume file;
    Volume open(file, 0, file.size()), other(file, 0, file.size());
    open.set_program_reader([] { return Bytes(16, 0x5A); });
    Vfs open_vfs(&open), other_vfs(&other), no_disc(nullptr);
    race::prepare_program_data(open_vfs);
    std::string of_open, of_other, of_none;
    CHECK(!race::exact_program_data(open_vfs, &of_open));
    CHECK(!race::exact_program_data(other_vfs, &of_other));
    CHECK(!race::exact_program_data(no_disc, &of_none));
    CHECK(!of_open.empty() && !of_other.empty());
    CHECK(of_other != of_open); // "cannot be read", not the open disc's program's error
    CHECK(of_none == of_other);
    race::forget_program_data();
    race::forget_program_data(); // closing with nothing prepared

    race::prepare_program_data(other_vfs);
    std::string unprepared;
    CHECK(!race::exact_program_data(other_vfs, &unprepared));
    CHECK(unprepared == of_other);
    race::forget_program_data();
}

// The disc's program: one result while the disc is open, what working it out on use gives, still valid after the disc
// closes for what holds it; closing while the thread may still run, then opening again.
TEST(program_prepare_disc) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    race::prepare_program_data(vfs);
    std::string err;
    const auto prepared = race::exact_program_data(vfs, &err);
    CHECK(prepared != nullptr);
    if (!prepared) {
        race::forget_program_data();
        return;
    }
    CHECK(race::exact_program_data(vfs) == prepared);
    race::forget_program_data();
    const auto on_use = race::exact_program_data(vfs);
    CHECK(on_use != nullptr && on_use != prepared);
    if (on_use) {
        CHECK(on_use->fingerprint() == prepared->fingerprint());
        CHECK(on_use->program() == prepared->program() && on_use->known() == prepared->known());
        CHECK(on_use->issues() == prepared->issues());
    }
    race::prepare_program_data(vfs);
    race::forget_program_data(); // at once
    race::prepare_program_data(vfs);
    const auto again = race::exact_program_data(vfs);
    CHECK(again != nullptr && again != prepared);
    if (again) CHECK(again->fingerprint() == prepared->fingerprint());
    race::forget_program_data();
}
