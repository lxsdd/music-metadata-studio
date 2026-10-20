#pragma once

#include <cstddef>

namespace djmeta {
// UI-thread admission gate, checked only after finished workers are reaped.
inline constexpr std::size_t kPrecisionCueMaxOutstandingLookups = 4;
[[nodiscard]] constexpr bool precision_cue_lookup_slot_available(
    std::size_t outstanding_jobs) noexcept {
    return outstanding_jobs < kPrecisionCueMaxOutstandingLookups;
}
} // namespace djmeta
