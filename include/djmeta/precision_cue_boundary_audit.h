#pragma once

// Read-only comparison of generated CUE boundaries with optional reference
// markers. Never changes audio, tags, or cue files. Frame equality is NOT
// proof of sample-perfect mastering identity.
#include "djmeta/precision_cue.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace djmeta {

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
    std::int64_t last_reference = -1;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& predicted = generated.boundaries[i];
        const auto& expected = reference[i];
        if (predicted.track_number != i + 1 || expected.track_number != i + 1 ||
            predicted.cue_frame < 0 || predicted.cue_frame > max_frame ||
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
        last_reference = expected.cue_frame;
    }
    out.state = out.review_count ? PrecisionCueAuditState::Review :
                out.within_one_frame_count ? PrecisionCueAuditState::WithinOneFrame :
                                             PrecisionCueAuditState::ExactFrames;
    return out;
}

} // namespace djmeta
