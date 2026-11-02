#pragma once

#include "base/bytes.h"

namespace gt3r {

class Reader {
public:
    virtual ~Reader() = default;
    virtual u64 size() const = 0;
    virtual const std::string& path() const = 0;
    virtual void read_at(u64 offset, void* out, size_t len) = 0;
    Bytes read_at(u64 offset, size_t len);
};

} // namespace gt3r
