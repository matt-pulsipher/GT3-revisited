// The app's name where players and log readers see it (base/app_identity.h), and a volume's name in its errors. No
// game data.

#include "test_framework.h"

#include "base/app_identity.h"
#include "base/log_file.h"
#include "base/reader.h"
#include "disc/volume.h"
#include "gt3r_version.h"
#include "net/session.h"

#include <cstring>
#include <string>

using namespace gt3r;

TEST(app_identity_defaults) {
    CHECK(app_identity().name == "GT3 Revisited");
    CHECK(app_identity().version == GT3R_VERSION);
    CHECK(log_file::file_name(false) == "gt3r.log");
    CHECK(log_file::file_name(true) == "gt3r.prev.log");
    CHECK(net::protocol_mismatch(net::kProtocolVersion - 1).find("Please update GT3 Revisited to play online") !=
          std::string::npos);
}

TEST(app_identity_set_by_another_app) {
    set_app_identity({"Another App", "1.2.3", "another"});
    CHECK(app_identity().name == "Another App");
    CHECK(app_identity().version == "1.2.3");
    CHECK(log_file::file_name(false) == "another.log");
    CHECK(log_file::file_name(true) == "another.prev.log");
    const std::string old = net::protocol_mismatch(net::kProtocolVersion - 1);
    CHECK(old.find("Please update Another App to play online") != std::string::npos);
    CHECK(old.find("GT3") == std::string::npos);

    // Empty fields keep the defaults.
    set_app_identity({"Another App", "", ""});
    CHECK(app_identity().version == GT3R_VERSION);
    CHECK(log_file::file_name(false) == "gt3r.log");

    set_app_identity({});
    CHECK(app_identity().name == "GT3 Revisited");
}

namespace {

// 0x20 zero bytes: no volume's magic.
class ZeroReader : public Reader {
public:
    u64 size() const override { return 0x20; }
    const std::string& path() const override { return path_; }
    void read_at(u64, void* out, size_t len) override { std::memset(out, 0, len); }

private:
    std::string path_ = "zero";
};

std::string volume_error(const char* name) {
    ZeroReader file;
    try {
        if (name) {
            Volume vol(file, 0, file.size(), name);
        } else {
            Volume vol(file, 0, file.size());
        }
    } catch (const FormatError& e) {
        return e.what();
    }
    return "";
}

} // namespace

TEST(volume_named_in_errors) {
    CHECK(volume_error(nullptr) == "GT3.VOL: bad magic");
    CHECK(volume_error("Other.VOL") == "Other.VOL: bad magic");
}
