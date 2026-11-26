#pragma once

// work(k) for every k < n on this thread and up to `threads` - 1 more (0 or 1: this thread alone), each taking the next
// index in turn; returns when all are done. A thread that cannot start leaves its share to the others. `work` must
// not throw.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <thread>
#include <vector>

namespace gt3r {

template <class Work>
void parallel_for(size_t n, unsigned threads, const Work& work) {
    std::atomic<size_t> next{0};
    auto run = [&] {
        for (size_t k; (k = next++) < n;) work(k);
    };
    std::vector<std::thread> helpers;
    for (size_t t = 1; t < std::min<size_t>(n, threads); ++t) {
        try {
            helpers.emplace_back(run);
        } catch (const std::exception&) {
            break;
        }
    }
    run();
    for (std::thread& t : helpers) t.join();
}

} // namespace gt3r
