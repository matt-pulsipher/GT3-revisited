#include "base/app_identity.h"

#include "gt3r_version.h"

#include <utility>

namespace gt3r {

namespace {

AppIdentity defaults() { return {"GT3 Revisited", GT3R_VERSION, "gt3r"}; }

AppIdentity& current() {
    static AppIdentity id = defaults();
    return id;
}

} // namespace

const AppIdentity& app_identity() { return current(); }

void set_app_identity(AppIdentity identity) {
    const AppIdentity d = defaults();
    if (identity.name.empty()) identity.name = d.name;
    if (identity.version.empty()) identity.version = d.version;
    if (identity.file_stem.empty()) identity.file_stem = d.file_stem;
    current() = std::move(identity);
}

} // namespace gt3r
