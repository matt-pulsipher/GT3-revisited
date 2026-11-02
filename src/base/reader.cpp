#include "base/reader.h"

namespace gt3r {

Bytes Reader::read_at(u64 offset, size_t len) {
    if (offset > size() || size() - offset < len) throw FormatError("read past end of " + path());
    Bytes b(len);
    read_at(offset, b.data(), len);
    return b;
}

} // namespace gt3r
