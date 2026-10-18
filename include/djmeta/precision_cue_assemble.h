#pragma once

// Pure release-to-disc precision CUE preview: no I/O and no tag/audio writes.
// Physical durations must come from an independent local read-only host probe.
#include "djmeta/precision_cue.h"
#include "djmeta/precision_cue_match.h"

#include <cstdint>
#include <string>
#include <vector>

namespace djmeta {

struct PrecisionCuePhysicalDisc {
    std::string mp3_basename;
    std::int64_t measured_duration_ms = 0; // 0 means unknown
};

struct PrecisionCuePreviewRequest {
    PrecisionCueRelease reference;
    std::vector<PrecisionCueRelease> candidates;
    std::vector<PrecisionCuePhysicalDisc> physical_discs;
    std::string genre;
    std::string release_date;
};

struct PrecisionCuePreviewResult {
    PrecisionCueMatchState state = PrecisionCueMatchState::Rejected;
    std::vector<std::string> diagnostics;
    std::vector<PrecisionCuePlan> discs;
    std::string provider;
    std::string provider_release_id;
    // Never grants permission to create files or update embedded metadata.
    bool preview_qualified = false;
};


struct PrecisionCueDiscMapping {
    PrecisionCueMatchState state = PrecisionCueMatchState::Rejected;
    std::vector<std::size_t> physical_index_by_disc;
    std::vector<std::string> diagnostics;
};

// Duration-only assignment is a read-only suggestion, never proof of
// disc identity or permission to write. Multiple valid permutations return
// Ambiguous with no mapping. Multi-disc results always require review.
PrecisionCueDiscMapping resolve_precision_cue_disc_mapping(
    const std::vector<std::int64_t>& provider_disc_durations_ms,
    const std::vector<PrecisionCuePhysicalDisc>& physical_discs);

// All disc plans are created in memory, or none is returned on rejection.
// Unknown physical durations cause REVIEW; >1 s deviation causes REVIEW;
// >3 s is rejected. A correct duration is NOT proof of source mastering.
PrecisionCuePreviewResult make_precision_cue_release_preview(
    const PrecisionCuePreviewRequest& request);

} // namespace djmeta
