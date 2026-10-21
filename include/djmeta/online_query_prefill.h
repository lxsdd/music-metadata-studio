#pragma once

#include <string_view>

namespace djmeta {

// A newly selected track without a searchable TITLE/ALBUM (or an external
// CUE requiring explicit Inspect CUE) must not inherit the previous track's
// automatically generated provider query. Preserve deliberate manual input
// during ordinary navigation; explicit track activation may replace it.
inline bool should_replace_auto_online_query(
    std::wstring_view current,
    std::wstring_view previous_automatic,
    bool explicit_track_activation) noexcept {
    return explicit_track_activation ||
        current.empty() || current == previous_automatic;
}

} // namespace djmeta
