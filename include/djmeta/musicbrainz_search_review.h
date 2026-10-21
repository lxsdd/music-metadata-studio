#pragma once

// Search-result evidence mapping (not a tag import or writer).
// An MB recording's artist/title is not the release's album artist/album.
#include "djmeta/online_fields.h"

#include <string>
#include <vector>

namespace djmeta::online {

// Keep the entire local physical field inventory as the comparison source.
// A track TITLE is never a surrogate ALBUM. Missing online values are
// simply omitted: they must not request deletion of a local tag.
// All rows require explicit review and remain evidence, not a write grant.
inline std::vector<FieldReviewRow> review_musicbrainz_search_hit_fields(
    const MetadataDocument& physical_originals,
    const std::string& exact_source_id,
    bool is_release,
    const std::string& provider_title,
    const std::string& provider_artist) {
    const auto scope = is_release ? EvidenceScope::Edition : EvidenceScope::Recording;
    std::vector<FieldEvidence> evidence;
    if (!provider_title.empty())
        evidence.push_back({
            is_release ? "ALBUM" : "TITLE", {provider_title},
            "musicbrainz", exact_source_id, scope, DateMeaning::NotDate});
    if (!provider_artist.empty())
        evidence.push_back({
            is_release ? "ALBUM ARTIST" : "ARTIST", {provider_artist},
            "musicbrainz", exact_source_id, scope, DateMeaning::NotDate});
    auto rows = review_online_fields(physical_originals, evidence);
    for (auto& row : rows)
        row.reason = "musicbrainz_live_search_" + row.reason;
    return rows;
}

} // namespace djmeta::online
