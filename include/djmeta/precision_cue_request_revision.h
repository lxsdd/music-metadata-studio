#pragma once
// Pure, host-independent stale-request guard. UI text equality alone is not
// enough: a user may change a release selection and then change it back while
// a network request is still running. Such responses must be discarded.
#include <cstdint>
#include <limits>

namespace djmeta {
class PrecisionCueRequestRevision {
    std::uint64_t revision_ = 1;
    std::uint64_t requested_ = 0;
    bool exhausted_ = false;
public:
    void input_changed() noexcept {
        if (revision_ == (std::numeric_limits<std::uint64_t>::max)())
            exhausted_ = true; // Never wrap back to a previously approved value.
        else
            ++revision_;
    }
    // Dismissal is stronger than a text change: even identical later input
    // cannot revive a previously completed worker's result.
    void cancel_request() noexcept {
        input_changed();
        requested_ = 0;
    }
    void begin_request() noexcept {
        requested_ = exhausted_ ? 0 : revision_;
    }
    bool request_is_current() const noexcept {
        return !exhausted_ && requested_ != 0 && requested_ == revision_;
    }
};
} // namespace djmeta
