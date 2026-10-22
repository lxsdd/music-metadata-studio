#pragma once

// Read-only comparison of generated CUE boundaries with optional reference
// markers. Never changes audio, tags, or cue files. Frame equality is NOT
// proof of sample-perfect mastering identity.
#include "djmeta/precision_cue.h"
#include "djmeta/precision_cue_match.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace djmeta {

// Signed difference between the nearest 75-fps marker and the catalog's
// cumulative millisecond boundary, rounded to the nearest MICROSECOND.
// This is solely a quantization audit, not an audio/edition comparison.
// The exact unrounded difference is (cue_frame*1000 - elapsed_ms*75)/75 ms.
inline std::int64_t precision_cue_quantization_offset_microseconds(
    const PrecisionCueBoundary& boundary) {
    if (boundary.elapsed_ms < 0 || boundary.cue_frame < 0 ||
        boundary.cue_frame != milliseconds_to_cue_frame(boundary.elapsed_ms))
        throw std::invalid_argument("Invalid quantized CUE boundary");
    const auto numerator = boundary.cue_frame * 1000 - boundary.elapsed_ms * 75;
    const auto absolute = numerator >= 0 ? numerator : -numerator;
    const auto rounded = (absolute * 1000 + 37) / 75;
    return numerator >= 0 ? rounded : -rounded;
}

struct PrecisionCueReferenceBoundary {
    unsigned track_number = 0;
    std::int64_t cue_frame = -1;
};

enum class PrecisionCueAuditState {
    ExactFrames,
    WithinOneFrame,
    Review,
    Rejected,
};

struct PrecisionCueBoundaryDifference {
    unsigned track_number = 0;
    std::int64_t predicted_frame = -1;
    std::int64_t reference_frame = -1;
    std::int64_t signed_delta_frames = 0;
};

struct PrecisionCueAuditResult {
    PrecisionCueAuditState state = PrecisionCueAuditState::Rejected;
    std::size_t exact_count = 0;
    std::size_t within_one_frame_count = 0;
    std::size_t review_count = 0;
    std::vector<PrecisionCueBoundaryDifference> differences;
};

// Edition identity and the actual MP3 duration must be checked separately.
// A malformed reference fails closed, without partial comparison evidence.
inline PrecisionCueAuditResult audit_precision_cue_boundaries(
    const PrecisionCuePlan& generated,
    const std::vector<PrecisionCueReferenceBoundary>& reference) {
    PrecisionCueAuditResult out;
    const auto count = generated.boundaries.size();
    if (count == 0 || count > 99 || reference.size() != count ||
        generated.total_duration_ms <= 0 ||
        generated.total_duration_ms > 24LL * 60 * 60 * 1000)
        return out;

    constexpr std::int64_t max_frame = 24LL * 60 * 60 * 75;
    std::int64_t last_predicted = -1;
    std::int64_t last_elapsed_ms = -1;
    std::int64_t last_reference = -1;
    const auto total_frames = milliseconds_to_cue_frame(generated.total_duration_ms);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& predicted = generated.boundaries[i];
        const auto& expected = reference[i];
        if (predicted.track_number != i + 1 || expected.track_number != i + 1 ||
            predicted.elapsed_ms < 0 ||
            predicted.elapsed_ms >= generated.total_duration_ms ||
            predicted.elapsed_ms <= last_elapsed_ms ||
            predicted.cue_frame < 0 || predicted.cue_frame >= total_frames ||
            predicted.cue_frame > max_frame ||
            predicted.cue_frame != milliseconds_to_cue_frame(predicted.elapsed_ms) ||
            expected.cue_frame < 0 || expected.cue_frame > max_frame ||
            (i == 0 && (predicted.cue_frame != 0 || expected.cue_frame != 0)) ||
            (i != 0 && (predicted.cue_frame <= last_predicted ||
                        expected.cue_frame <= last_reference))) {
            return PrecisionCueAuditResult{};
        }
        const auto delta = predicted.cue_frame - expected.cue_frame;
        out.differences.push_back({predicted.track_number, predicted.cue_frame,
                                   expected.cue_frame, delta});
        if (delta == 0) ++out.exact_count;
        else if (delta == 1 || delta == -1) ++out.within_one_frame_count;
        else ++out.review_count;
        last_predicted = predicted.cue_frame;
        last_elapsed_ms = predicted.elapsed_ms;
        last_reference = expected.cue_frame;
    }
    out.state = out.review_count ? PrecisionCueAuditState::Review :
                out.within_one_frame_count ? PrecisionCueAuditState::WithinOneFrame :
                                             PrecisionCueAuditState::ExactFrames;
    return out;
}

    
// An edition-scoped, read-only CUE diff. Catalog identity is independently
// recomputed here: callers cannot supply a forged "Selected" match report.
// A frame match never proves physical/virtual foobar identity, mastering,
// reference-CUE provenance or permission to create/write a file.
enum class PrecisionCueEditionDiffState {
    ExactFramePreviewOnly,
    FrameReviewRequired,
    EditionReviewRequired,
    Rejected,
};

struct PrecisionCueEditionDiffReview {
    PrecisionCueEditionDiffState state = PrecisionCueEditionDiffState::Rejected;
    PrecisionCueAuditResult boundaries;
};

// The candidate must be the uniquely selected release edition, and EVERY
// generated millisecond boundary must derive from that edition's exact
// per-disc catalog durations. Physical-file identity remains a separate
// mandatory host gate; no audio, tag, CUE or filesystem operation occurs.
inline PrecisionCueEditionDiffReview review_precision_cue_edition_boundaries(
    const PrecisionCueRelease& reference_release,
    const std::vector<PrecisionCueRelease>& candidate_releases,
    std::size_t requested_candidate_index,
    unsigned disc_number,
    const PrecisionCuePlan& generated,
    const std::vector<PrecisionCueReferenceBoundary>& reference_boundaries) {
    PrecisionCueEditionDiffReview out;
    if (disc_number == 0 || disc_number > 8 ||
        requested_candidate_index >= candidate_releases.size())
        return out;
    const auto identity =
        match_precision_cue_releases(reference_release, candidate_releases);
    if (identity.state != PrecisionCueMatchState::Selected ||
        !identity.selected_candidate_index ||
        *identity.selected_candidate_index != requested_candidate_index) {
        out.state = PrecisionCueEditionDiffState::EditionReviewRequired;
        return out;
    }

    const auto& edition = candidate_releases[requested_candidate_index];
    constexpr std::int64_t kMaximumDiscMs = 24LL * 60 * 60 * 1000;
    std::size_t track_index = 0;
    std::int64_t elapsed_ms = 0;
    for (const auto& track : edition.tracks) {
        if (track.disc_number != disc_number) continue;
        if (track_index >= generated.boundaries.size() ||
            track.track_number != track_index + 1 ||
            track.duration_ms <= 0 ||
            track.duration_ms > kMaximumDiscMs - elapsed_ms)
            return out;
        const auto& boundary = generated.boundaries[track_index];
        if (boundary.track_number != track.track_number ||
            boundary.elapsed_ms != elapsed_ms ||
            boundary.cue_frame != milliseconds_to_cue_frame(elapsed_ms))
            return out;
        elapsed_ms += track.duration_ms;
        ++track_index;
    }
    if (track_index == 0 || track_index != generated.boundaries.size() ||
        generated.total_duration_ms != elapsed_ms)
        return out;

    out.boundaries = audit_precision_cue_boundaries(generated, reference_boundaries);
    if (out.boundaries.state == PrecisionCueAuditState::Rejected) return out;
    out.state = out.boundaries.state == PrecisionCueAuditState::ExactFrames
        ? PrecisionCueEditionDiffState::ExactFramePreviewOnly
        : PrecisionCueEditionDiffState::FrameReviewRequired;
    return out;
}

} // namespace djmeta
