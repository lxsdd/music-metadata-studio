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
    // Independent host-probed canonical physical file identities in selection
    // order. Never use display basenames, CUE virtual subsongs or provider IDs.
    // Optional for core previews; mandatory for a Studio source-bound handoff.
    std::vector<std::string> physical_source_identity_by_selection;
    // Only set after an explicit per-disc UI choice; never inferred from filenames.
    // Still requires release/MP3 identity review and NEVER authorizes writes.
    std::vector<std::size_t> reviewed_physical_index_by_disc;
    std::string genre;
    std::string release_date;
};

// Header REM metadata must retain its catalog-vs-manual provenance.
// Manual values are visible read-only proposals, never catalog attestations.
enum class PrecisionCueHeaderFieldOrigin {
    Absent,
    SelectedCatalog,
    ManualUnverified,
    ConflictsWithSelectedCatalog
};
struct PrecisionCueHeaderProvenance {
    PrecisionCueHeaderFieldOrigin genre = PrecisionCueHeaderFieldOrigin::Absent;
    PrecisionCueHeaderFieldOrigin release_date = PrecisionCueHeaderFieldOrigin::Absent;
};

struct PrecisionCuePreviewResult {
    PrecisionCueMatchState state = PrecisionCueMatchState::Rejected;
    std::vector<std::string> diagnostics;
    std::vector<PrecisionCuePlan> discs;
    std::string provider;
    std::string provider_release_id;
    // Never grants permission to create files or update embedded metadata.
    bool preview_qualified = false;
    PrecisionCueHeaderProvenance header_provenance;
    // Review-snapshot identity: exact release-disc -> physical selection map.
    // Equal basenames/durations are not evidence of unchanged UI selection.
    // Never a writer approval token.
    std::vector<std::size_t> physical_index_by_disc;
    // Frozen alongside the generated CUE plans; not a write approval token.
    std::vector<std::string> physical_source_identity_by_selection;
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

// Revalidate an explicit user-chosen permutation against ALL duration/path
// constraints. Ambiguous durations can become a REVIEWABLE preview, never an
// automatic Selected mapping. The caller must record each actual UI choice.
PrecisionCueDiscMapping review_precision_cue_disc_mapping(
    const std::vector<std::int64_t>& provider_disc_durations_ms,
    const std::vector<PrecisionCuePhysicalDisc>& physical_discs,
    const std::vector<std::size_t>& reviewed_physical_index_by_disc);

// Read-only per-selection validation for the native disc chooser. An incomplete
// selection is reviewable, never a valid complete mapping or write approval.
// The sentinel physical_discs.size() denotes an unassigned disc.
PrecisionCueDiscMapping validate_precision_cue_partial_disc_mapping(
    const std::vector<std::int64_t>& provider_disc_durations_ms,
    const std::vector<PrecisionCuePhysicalDisc>& physical_discs,
    const std::vector<std::size_t>& chosen_physical_index_by_disc);

// All disc plans are created in memory, or none is returned on rejection.
// Unknown physical durations cause REVIEW; >1 s deviation causes REVIEW;
// >3 s is rejected. A correct duration is NOT proof of source mastering.
PrecisionCuePreviewResult make_precision_cue_release_preview(
    const PrecisionCuePreviewRequest& request);

} // namespace djmeta
