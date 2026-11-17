#pragma once
// Early read-only release identity check for accumulated catalog duration
// drift. A one-track match cannot validate a continuous-mix CUE boundary.
// Inputs must already have structurally validated ordered disc/track lists.
#include "djmeta/precision_cue_match.h"
#include <cstdint>
#include <optional>

namespace djmeta {

struct PrecisionCueCumulativeEvidenceConflict {
    unsigned disc_number = 0;
    unsigned track_number = 0;
    // Candidate minus reference duration up to this boundary, per disc.
    std::int64_t signed_cumulative_delta_ms = 0;
    std::int64_t allowed_cumulative_delta_ms = 0;
};

// Without verified per-provider precision, allow 500ms rounding uncertainty
// per known reference track plus 1000ms baseline. Exceeding this budget is
// REVIEW (never a silent edition selection), not proof of a different master.
// A new disc resets the boundary clock. Missing reference durations already
// require review by the matcher and cannot establish cumulative evidence.
inline std::optional<PrecisionCueCumulativeEvidenceConflict>
first_precision_cue_cumulative_evidence_conflict(
    const PrecisionCueRelease& reference, const PrecisionCueRelease& candidate) {
    if (reference.tracks.size() != candidate.tracks.size())
        return std::nullopt;
    unsigned disc = 0;
    std::int64_t cumulative_delta = 0;
    std::int64_t known_tracks = 0;
    bool reference_known = true;
    for (std::size_t i = 0; i < reference.tracks.size(); ++i) {
        const auto& a = reference.tracks[i];
        const auto& b = candidate.tracks[i];
        if (a.disc_number != b.disc_number ||
            a.track_number != b.track_number)
            return std::nullopt;
        if (a.disc_number != disc) {
            disc = a.disc_number;
            cumulative_delta = 0;
            known_tracks = 0;
            reference_known = true;
        }
        if (a.duration_ms <= 0 || b.duration_ms <= 0)
            reference_known = false;
        if (!reference_known) continue;
        cumulative_delta += b.duration_ms - a.duration_ms;
        ++known_tracks;
        const auto allowed = 1000 + 500 * known_tracks;
        if (cumulative_delta > allowed || cumulative_delta < -allowed)
            return PrecisionCueCumulativeEvidenceConflict{
                disc, a.track_number, cumulative_delta, allowed
            };
    }
    return std::nullopt;
}

} // namespace djmeta
