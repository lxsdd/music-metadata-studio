#pragma once

// Read-only presentation adapter: one explicitly scoped clipboard candidate
// compared against ONE immutable, already qualified text-CUE carrier.
// Never constructs a postimage or invokes foobar SDK tag/file writing.
#include "djmeta/cue_online_review.h"
#include "djmeta/cue_original_inventory.h"
#include "djmeta/online_intake.h"

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

namespace djmeta::online {

inline std::vector<FieldReviewRow> review_manual_cue_candidate(
    const CueMetadataInventory& cue, const ManualCandidate& manual) {
    if (cue.status != CueSyntaxStatus::Parsed)
        throw std::invalid_argument(
            "CUE has unqualified syntax or metadata; comparison refused.");
    if (manual.provider.empty() || manual.source_id.empty() ||
        manual.fields.empty())
        throw std::invalid_argument("CUE candidate identity or fields missing.");
    if (manual.scope == EvidenceScope::Recording) {
        if (!manual.cue_track_ordinal ||
            *manual.cue_track_ordinal >= cue.tracks.size())
            throw std::invalid_argument(
                "Recording comparison requires @cue_track_ordinal=1..N, "
                "the absolute CUE track order (not TRACK number).");
        if (cue.tracks[*manual.cue_track_ordinal].track_type != "AUDIO")
            throw std::invalid_argument(
                "CUE candidate recording target is not an audio track.");
    } else if (manual.cue_track_ordinal) {
        throw std::invalid_argument(
            "Album/release comparison must not include @cue_track_ordinal.");
    }
    const auto proposals = review_cue_candidate_fields(
        cue, manual.cue_track_ordinal, manual.fields);
    std::vector<FieldReviewRow> rows;
    rows.reserve(proposals.size());
    for (const auto& item : proposals) {
        FieldReviewRow row;
        row.evidence_index = item.evidence_index;
        row.candidate = item.candidate;
        row.original_values = item.original_values;
        row.state = item.state;
        const std::string field = item.cue_field.empty()
            ? item.candidate.field : item.cue_field;
        if (item.target == CueReviewTarget::AlbumGlobal) {
            row.field = "Album / " + field;
        } else if (item.target == CueReviewTarget::TrackLocal) {
            row.field = "Track " +
                std::to_string(*manual.cue_track_ordinal + 1) + " / " + field;
        } else {
            row.field = "Unsupported / " + field;
        }
        row.reason = item.reason;
        if (item.original_inherited) row.reason += " [inherited source]";
        if (item.affected_inheriting_tracks)
            row.reason += " [affected inherited tracks: " +
                std::to_string(item.affected_inheriting_tracks) + "]";
        rows.push_back(std::move(row));
    }
    return rows;
}


// A manual CUE clipboard import is one scoped proposal group per album,
// target track or unsupported field category. The complete parsed CUE
// metadata and FILE references stay in separate album/track/FILE groups.
// Never promote the user-declared provider to an authenticated API origin.
inline void append_manual_cue_comparison_source(
    std::vector<FieldReviewRow>& rows,
    const CueMetadataInventory& cue,
    std::string_view declared_provider) {
    if(cue.status != CueSyntaxStatus::Parsed ||
       !valid_evidence_origin(declared_provider,128) ||
       rows.empty() || rows.size()>96)
        throw std::invalid_argument("Manual CUE comparison group not qualified.");
    if(std::any_of(rows.begin(),rows.end(),[&](const FieldReviewRow& row) {
           return row.candidate.provider!=declared_provider ||
                  row.candidate.source_id.empty() ||
                  row.candidate.field.empty() ||
                  is_local_cue_inventory(row) ||
                  is_local_physical_original(row);
       }))
        throw std::invalid_argument("Manual CUE comparison provenance mismatch.");
    auto originals=cue_original_inventory_rows(cue,"CUE Original / ");
    if(originals.empty())
        throw std::invalid_argument("Manual CUE has no qualified source inventory.");
    rows.reserve(rows.size()+originals.size());
    const std::string prefix="Pasted "+std::string(declared_provider)+" / ";
    for(auto& row:rows) row.field=prefix+row.field;
    for(auto& row:originals) rows.push_back(std::move(row));
}

} // namespace djmeta::online
