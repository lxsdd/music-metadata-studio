#pragma once
// Read-only integrity gate: selected Apple search album vs complete lookup.
#include "djmeta/precision_cue_apple.h"
#include <string>
#include <string_view>
namespace djmeta {
namespace precision_cue_apple_selection_detail {
inline std::string identity_text(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    bool pending_space = false;
    for (const unsigned char c : input) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space) result.push_back(' ');
        pending_space = false;
        result.push_back(static_cast<char>(
            c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c));
    }
    return result;
}
}
inline bool apple_precision_cue_selection_matches_lookup(
    const ApplePrecisionCueAlbumHit& selected,
    const PrecisionCueRelease& looked_up) {
    using precision_cue_apple_selection_detail::identity_text;
    if (looked_up.provider != "apple" || selected.collection_id.empty() ||
        selected.collection_id != looked_up.provider_release_id ||
        selected.track_count == 0 ||
        selected.track_count != looked_up.tracks.size() ||
        selected.title.empty() || selected.performer.empty() ||
        looked_up.album_title.empty() || looked_up.album_performer.empty())
        return false;
    return identity_text(selected.title) == identity_text(looked_up.album_title) &&
           identity_text(selected.performer) == identity_text(looked_up.album_performer);
}
} // namespace djmeta
