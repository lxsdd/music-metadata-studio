#pragma once
// Offline user-reviewed Spotify/Amazon album evidence for Precision CUE.
// No network, provider authorization, writer token or audio accuracy claim.
#include "djmeta/precision_cue_source_consensus.h"
#include <algorithm>

namespace djmeta {
enum class PrecisionCueOptionalFormat {
    SpotifyWebMilliseconds,
    AmazonWebSeconds,
    AmazonDeviceApproximateMilliseconds
};
struct PrecisionCueOptionalSource {
    PrecisionCueRelease release;
    PrecisionCueOptionalFormat format = PrecisionCueOptionalFormat::SpotifyWebMilliseconds;
    bool offline_user_reviewed = false;
};
enum class PrecisionCueOptionalState { Blocked, ReviewOnly };
struct PrecisionCueOptionalAudit {
    PrecisionCueOptionalState state = PrecisionCueOptionalState::Blocked;
    const char* diagnostic = "OPTIONAL_SOURCE_BLOCKED";
    unsigned field_unit_ms = 0; // Reported field unit, NOT accuracy.
    unsigned comparison_resolution_ms = 0;
    PrecisionCueConsensusReport comparison;
};
inline PrecisionCueOptionalAudit audit_precision_cue_optional_source(
    const PrecisionCueRelease& apple, const PrecisionCueOptionalSource& extra) {
    using precision_cue_consensus_detail::normalized;
    PrecisionCueOptionalAudit out;
    if (normalized(apple.provider) != "apple" || !extra.offline_user_reviewed) {
        out.diagnostic = "OPTIONAL_APPLE_OR_OFFLINE_REVIEW_MISSING";
        return out;
    }
    const auto provider = normalized(extra.release.provider);
    switch (extra.format) {
    case PrecisionCueOptionalFormat::SpotifyWebMilliseconds:
        if (provider != "spotify") {
            out.diagnostic = "OPTIONAL_PROVIDER_FORMAT_MISMATCH";
            return out;
        }
        out.field_unit_ms = out.comparison_resolution_ms = 1;
        break;
    case PrecisionCueOptionalFormat::AmazonWebSeconds:
        if (provider != "amazon_music") {
            out.diagnostic = "OPTIONAL_PROVIDER_FORMAT_MISMATCH";
            return out;
        }
        if (std::any_of(extra.release.tracks.begin(), extra.release.tracks.end(),
            [](const PrecisionCueReleaseTrack& t) {
                return t.duration_ms % 1000 != 0;
            })) {
            out.diagnostic = "AMAZON_WEB_DURATION_MUST_BE_WHOLE_SECONDS";
            return out;
        }
        out.field_unit_ms = out.comparison_resolution_ms = 1000;
        break;
    case PrecisionCueOptionalFormat::AmazonDeviceApproximateMilliseconds:
        if (provider != "amazon_music") {
            out.diagnostic = "OPTIONAL_PROVIDER_FORMAT_MISMATCH";
            return out;
        }
        out.field_unit_ms = 1;
        out.comparison_resolution_ms = 1000; // Conservative review gate; not measured accuracy.
        break;
    default:
        out.diagnostic = "OPTIONAL_UNSUPPORTED_FORMAT";
        return out;
    }
    out.comparison = audit_precision_cue_source_consensus({
        {apple, 1}, {extra.release, out.comparison_resolution_ms}});
    if (out.comparison.state == PrecisionCueConsensusState::Rejected ||
        out.comparison.differences.size() != apple.tracks.size()) {
        out.diagnostic = "OPTIONAL_DIFFERENT_EDITION_OR_INVALID_EVIDENCE";
        return out;
    }
    out.state = PrecisionCueOptionalState::ReviewOnly;
    out.diagnostic = out.comparison.state == PrecisionCueConsensusState::Review
        ? "OPTIONAL_DURATION_DISAGREEMENT_REVIEW"
        : "OPTIONAL_OFFLINE_EVIDENCE_NOT_AUDIO_PROOF";
    return out;
}
} // namespace djmeta
