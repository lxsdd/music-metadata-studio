#pragma once

// Read-only, provider-independent release identity gate for precision CUEs.
// A selected candidate is evidence for preview, never authorization to write.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace djmeta {

struct PrecisionCueReleaseTrack {
    unsigned disc_number = 0;
    unsigned track_number = 0;
    std::string title;
    std::string performer;
    std::int64_t duration_ms = 0; // Zero means unknown only for a reference.
};

struct PrecisionCueRelease {
    std::string provider;
    std::string provider_release_id;
    std::string album_title;
    std::string album_performer;
    // Disc and track positions must be consecutive, starting at 1.
    std::vector<PrecisionCueReleaseTrack> tracks;
    // Only populated when official album-level catalog data supplies them.
    std::string genre;
    std::string release_date; // YYYY-MM-DD; never a physical tag write.
};

enum class PrecisionCueMatchState {
    Selected,    // Exactly one unambiguous strong candidate; preview only.
    Review,      // Plausible but incomplete/conflicting evidence.
    Ambiguous,   // Multiple plausible editions or conflicting provider IDs.
    Rejected,    // Structural, title/version or duration mismatch.
    Duplicate,   // Exact duplicate of an already assessed provider ID.
};

struct PrecisionCueCandidateMatch {
    std::size_t candidate_index = 0;
    PrecisionCueMatchState state = PrecisionCueMatchState::Rejected;
    std::vector<std::string> diagnostics;
};

struct PrecisionCueMatchReport {
    PrecisionCueMatchState state = PrecisionCueMatchState::Rejected;
    std::optional<std::size_t> selected_candidate_index;
    std::vector<PrecisionCueCandidateMatch> candidates;
    std::vector<std::string> diagnostics;
};

// No fuzzy substitutions: mixed/original/remix titles remain distinct.
// Normalize only ASCII case and horizontal/vertical ASCII whitespace.
// A single strong match is withheld if another plausible edition exists.
// Provider ID de-duplication is allowed only for identical provider payloads.
// Candidate durations must be >0; unknown reference durations cause REVIEW.
// No network, audio, file or tag access occurs in this function.
PrecisionCueMatchReport match_precision_cue_releases(
    const PrecisionCueRelease& reference,
    const std::vector<PrecisionCueRelease>& candidates);

} // namespace djmeta
