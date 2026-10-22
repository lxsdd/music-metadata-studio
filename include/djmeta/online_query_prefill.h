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


// One shared edit box serves title discovery and canonical Beatport URLs.
// Never carry the previous provider's query into a different transport.
// A title suggestion is read-only; it never authorizes an online request.
enum class ProviderQueryTransition { Preserve, Clear, ClearAndSuggest };
inline constexpr ProviderQueryTransition provider_query_transition(
    bool was_beatport, bool now_beatport) noexcept {
    if(now_beatport) return ProviderQueryTransition::Clear;
    return was_beatport ? ProviderQueryTransition::ClearAndSuggest
                        : ProviderQueryTransition::Preserve;
}

} // namespace djmeta
