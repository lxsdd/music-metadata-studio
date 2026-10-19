#pragma once
// Strict, read-only parser for a COMPLETE official Deezer album API response.
// No network, audio, scraping, filesystem or tag operations occur here.
// Public Deezer durations have WHOLE-SECOND resolution; they are identity
// evidence, never an independent source of frame-accurate CUE boundaries.
#include "djmeta/precision_cue_match.h"
#include <string>
#include <string_view>

namespace djmeta {
// Fixed official public catalog endpoint. A URL cannot be injected.
std::string deezer_precision_cue_album_api_url(std::string_view album_id);
// Requires a complete, unpaginated, internally consistent album tracklist.
// A paginated/incomplete response MUST be collected and revalidated by a
// separately authorized transport before use. Never silently truncate.
PrecisionCueRelease parse_deezer_precision_cue_album(
    std::string_view json, std::string_view expected_album_id);
} // namespace djmeta
