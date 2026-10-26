#pragma once

#include "djmeta/metadata_diff.h"
#include "djmeta/review_decisions.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {

enum class MetadataFocus { Music, Extended, All };
// Presentation-only queue states; never staging or write authorization.
enum class TrackDiscovery { All, Changed, NeedsReview, PendingAny,
                            StagedEdits, Reviewed, Unchanged };

struct TrackReviewSummary {
    std::size_t source_index = 0;
    std::size_t music_changes = 0;
    std::size_t extended_changes = 0;
    std::size_t review_required = 0;
    std::size_t safe_changes = 0;
    std::size_t confident_changes = 0;
    std::size_t pending_proposals = 0;
    std::size_t staged_changes = 0;
};

struct TrackReviewProgress {
    std::size_t pending_proposals = 0;
    std::size_t pending_review = 0;
    std::size_t staged_changes = 0;
};

// Exact captured analysis + explicit in-memory decisions. Accepted or manual
// values equal to the original are no-ops, not staged edits.
TrackReviewProgress summarize_track_review_progress(
    const AnalysisResult& analysis,
    const std::vector<ReviewDecision>& decisions);

// One-time source-to-row index for an immutable, flattened review snapshot.
// Offsets identify contiguous ranges of original row positions, irrespective
// of the order in which sources occurred in the input. Read-only, never a
// sorting, staging, approval or physical write authority.
struct TrackDiffIndex {
    std::vector<std::size_t> offsets;       // track count + 1
    std::vector<std::size_t> row_positions; // original diff-row indices
    std::size_t captured_row_count = 0;
};

TrackDiffIndex index_track_diffs(
    const std::vector<MetadataDiffRow>& proposals, std::size_t track_count);

// A view classification ONLY: every proposal and its provenance are retained.
// Custom/unknown fields remain visible under Extended / All, never deleted.
bool is_music_metadata_field(std::string_view name);
std::vector<TrackReviewSummary> summarize_track_changes(
    std::size_t track_count,
    const std::vector<MetadataDiffRow>& proposals);

// Decision-only review counter: source/field/change summaries remain fixed
// for a captured snapshot. Recount only the tracks touched by an approval.
// Reject mismatched proposal identities rather than showing a false count.
std::size_t count_pending_review_proposals(
    const AnalysisResult& analysis,
    const std::vector<ReviewDecision>& decisions);

std::vector<MetadataDiffRow> selected_track_diffs(
    const std::vector<MetadataDiffRow>& proposals,
    std::size_t source_index, MetadataFocus focus);

// Indexed equivalent: O(proposals for the selected track), not O(all proposals).
// Refuses a structurally stale index and preserves original proposal identity.
std::vector<MetadataDiffRow> selected_track_diffs(
    const std::vector<MetadataDiffRow>& proposals,
    const TrackDiffIndex& index,
    std::size_t source_index, MetadataFocus focus);

// Filter track identities AFTER sorting; no proposal or source is modified.
std::vector<std::size_t> filter_track_view(
    const std::vector<TrackReviewSummary>& summaries,
    const std::vector<std::size_t>& sorted_source_indices,
    TrackDiscovery discovery);

// Sorting changes presentation only; source index identity remains stable.
std::vector<std::size_t> sort_track_summaries(
    const std::vector<TrackReviewSummary>& summaries,
    const std::vector<std::string>& display_labels,
    int column, bool descending);

} // namespace djmeta
