#pragma once
// Read-only physical EOF audit. Never proves edition or sample accuracy.
#include "djmeta/precision_cue.h"
#include <cstdint>
#include <string_view>
namespace djmeta {
enum class PrecisionCuePhysicalBoundsState { QualifiedForExplicitReview, ReviewOnly, Rejected };
struct PrecisionCuePhysicalBoundsAudit {
    PrecisionCuePhysicalBoundsState state=PrecisionCuePhysicalBoundsState::Rejected;
    std::string_view diagnostic="CUE_PHYSICAL_BOUNDS_INVALID";
    std::int64_t final_index_frame=0, physical_complete_frames=0;
    std::int64_t final_track_frames_available=0, signed_total_delta_ms=0;
};
inline PrecisionCuePhysicalBoundsAudit audit_precision_cue_physical_bounds(
    const PrecisionCuePlan& plan, std::int64_t physical_duration_ms) noexcept {
    using State=PrecisionCuePhysicalBoundsState;
    PrecisionCuePhysicalBoundsAudit r;
    constexpr std::int64_t max_ms=24LL*60*60*1000;
    if (plan.boundaries.empty() || plan.boundaries.size()>99 ||
        plan.total_duration_ms<=0 || plan.total_duration_ms>max_ms ||
        physical_duration_ms<0 || physical_duration_ms>max_ms) return r;
    std::int64_t last_ms=-1, last_frame=-1;
    for (std::size_t i=0;i<plan.boundaries.size();++i) {
        const auto& b=plan.boundaries[i];
        if (b.track_number!=i+1 || b.elapsed_ms<=last_ms ||
            b.elapsed_ms>=plan.total_duration_ms || b.cue_frame<=last_frame ||
            b.cue_frame!=(b.elapsed_ms*75+500)/1000 ||
            (i==0 && (b.elapsed_ms!=0 || b.cue_frame!=0))) return r;
        last_ms=b.elapsed_ms; last_frame=b.cue_frame;
    }
    r.final_index_frame=last_frame;
    if (physical_duration_ms==0) {
        r.state=State::ReviewOnly;
        r.diagnostic="CUE_PHYSICAL_DURATION_UNKNOWN_NO_CREATE";
        return r;
    }
    r.physical_complete_frames=physical_duration_ms*75/1000;
    r.final_track_frames_available=r.physical_complete_frames-last_frame;
    r.signed_total_delta_ms=plan.total_duration_ms-physical_duration_ms;
    if (r.final_track_frames_available<1) {
        r.diagnostic="CUE_FINAL_INDEX_AT_OR_BEYOND_PHYSICAL_EOF";
        return r;
    }
    if (r.signed_total_delta_ms>3000 || r.signed_total_delta_ms< -3000) {
        r.diagnostic="CUE_TOTAL_DURATION_INCOMPATIBLE_WITH_PHYSICAL";
        return r;
    }
    if (r.final_track_frames_available<=2) {
        r.state=State::ReviewOnly;
        r.diagnostic="CUE_FINAL_TRACK_TOO_CLOSE_TO_PHYSICAL_EOF";
        return r;
    }
    if (r.signed_total_delta_ms>1000 || r.signed_total_delta_ms< -1000) {
        r.state=State::ReviewOnly;
        r.diagnostic="CUE_TOTAL_DURATION_REQUIRES_PHYSICAL_REVIEW";
        return r;
    }
    r.state=State::QualifiedForExplicitReview;
    r.diagnostic="CUE_INDEXES_WITHIN_PHYSICAL_BOUNDS_NOT_SAMPLE_PROOF";
    return r;
}
} // namespace djmeta
