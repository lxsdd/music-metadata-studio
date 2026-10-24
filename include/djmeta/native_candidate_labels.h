#pragma once
// Source-driven labels for the actual native candidate comparison columns.
// Both the foobar virtual owner-data ListView and portable end-to-end
// review tests use these exact pure presentation rules. No host SDK or writes.
#include "djmeta/online_fields.h"
#include <string>

namespace djmeta::online {

inline bool native_candidate_group(const FieldReviewRow& row) {
    return row.reason.starts_with("musicbrainz_live_candidate_rank_") ||
        row.reason=="musicbrainz_live_release_group" ||
        row.reason=="discogs_live_release_group" ||
        row.reason=="beatport_public_webpage_group" ||
        row.reason=="local_physical_group" ||
        row.reason=="local_cue_group";
}

inline bool native_candidate_cue_inventory(const FieldReviewRow& row) {
    return row.reason=="cue_inventory_read_only" ||
        row.reason=="cue_inventory_unqualified";
}

inline std::string native_candidate_source_label(const FieldReviewRow& row) {
    if(row.reason=="cue_reference_preflight_only")
        return "Local filesystem (read-only)";
    if(row.reason=="local_cue_group" || native_candidate_cue_inventory(row))
        return row.candidate.provider;
    return online_evidence_origin_label(row);
}

inline std::string native_candidate_status_label(const FieldReviewRow& row) {
    if(native_candidate_group(row)) {
        if(row.reason.starts_with("musicbrainz_live_candidate_rank_"))
            return row.candidate.field;
        if(row.reason=="local_physical_group") return "Keep original";
        if(row.reason=="local_cue_group") return "Keep original CUE";
        return row.state==FieldReviewState::Blocked ? "Unmatched" : "Review";
    }
    if(row.reason=="cue_reference_preflight_only")
        return "Reference only";
    if(native_candidate_cue_inventory(row))
        return row.reason=="cue_inventory_unqualified"
            ? "Unqualified" : "Read-only";
    if(row.state==FieldReviewState::Unchanged) return "No change";
    if(row.state==FieldReviewState::NeedsReview) return "Review";
    return "Blocked";
}

} // namespace djmeta::online
