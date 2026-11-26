#pragma once

// GPU work left for the GPU's thread: a GpuModel made while its thread has a DeferredUploads installed (Scope) does its
// CPU part at once and leaves its upload here, for run() on the GPU's thread, before the model is drawn. A model destroyed
// before run() has its upload skipped.

#include <functional>
#include <utility>
#include <vector>

namespace gt3r::render {

class DeferredUploads {
public:
    void add(std::function<void()> work) { work_.push_back(std::move(work)); }
    void run() {
        for (std::function<void()>& w : std::exchange(work_, {})) w();
    }
    // This thread's, or nullptr (upload at once).
    static DeferredUploads*& current() {
        thread_local DeferredUploads* d = nullptr;
        return d;
    }
    class Scope {
    public:
        explicit Scope(DeferredUploads& d) : prev_(std::exchange(current(), &d)) {}
        ~Scope() { current() = prev_; }

    private:
        DeferredUploads* prev_;
    };

private:
    std::vector<std::function<void()>> work_;
};

} // namespace gt3r::render
