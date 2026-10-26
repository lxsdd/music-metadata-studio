#pragma once

// Read-only adapter for a user-requested Apple iTunes Search API lookup response.
// No HTTP, filesystem, foobar or metadata writer is invoked by this adapter.

#include "djmeta/precision_cue_match.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {

// Only a decimal collection ID is accepted, so the caller cannot inject a URL.
// Returns an official public HTTPS iTunes lookup URL. Network transport is
// intentionally outside the pure core and must enforce timeouts/rate limits.
std::string apple_precision_cue_lookup_url(std::string_view collection_id);


// Limited to an ASCII ISO alpha-2 storefront. HTTP is never performed here.
std::string apple_precision_cue_lookup_url(
    std::string_view collection_id, std::string_view storefront);

struct ApplePrecisionCueAlbumHit {
    std::string collection_id;
    std::string title;
    std::string performer;
    unsigned track_count = 0;
};

// Build a bounded official Apple/iTunes album-search URL. Results are
// candidates only, never authorization for an exact-release CUE preview.
std::string apple_precision_cue_album_search_url(
    std::string_view album_artist_query, std::string_view storefront);
std::vector<ApplePrecisionCueAlbumHit> parse_apple_precision_cue_album_search(
    std::string_view json);

// Parses an official lookup?id=<collectionId>&entity=song response. Requires
// one complete, single-release, ordered tracklist with positive integer-ms
// durations. Missing/ambiguous/malformed data is rejected, never fabricated.
// The resulting release must still pass match_precision_cue_releases against
// independent reference metadata before a CUE can be previewed.
// Throws std::invalid_argument for malformed or incomplete provider data.
PrecisionCueRelease parse_apple_precision_cue_lookup(
    std::string_view json, std::string_view expected_collection_id);

} // namespace djmeta
