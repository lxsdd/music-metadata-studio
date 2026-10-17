#pragma once

// Bounded, user-triggered MusicBrainz release discovery for qualified CUE
// albums. Every attempt uses the official fixed WS/2 release endpoint.
// Broader searches are discovery-only; never evidence of an exact edition.
#include "djmeta/musicbrainz_provider.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace djmeta::online::musicbrainz {

struct ReleaseSearchPlan {
    std::vector<RecordingSearchAttempt> attempts;
};

inline ReleaseSearchPlan release_search_plan(std::string_view album_title,
                                              std::string_view album_artist) {
    ReleaseSearchPlan plan;
    // Validation is identical to the recording provider. No empty title,
    // control characters, oversized terms, or arbitrary HTTP endpoints.
    const auto title_only = make_search_path(SearchKind::Release, album_title, "");
    if (!album_artist.empty()) {
        const auto qualified =
            make_search_path(SearchKind::Release, album_title, album_artist);
        if (qualified != title_only)
            plan.attempts.push_back(
                {"CUE release title + album artist", qualified});
    }
    plan.attempts.push_back({"CUE release title only (edition review required)",
                             title_only});
    return plan;
}

} // namespace djmeta::online::musicbrainz
