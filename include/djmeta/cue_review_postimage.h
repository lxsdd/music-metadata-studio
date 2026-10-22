#pragma once
// Explicit online CUE field selections -> exact in-memory CUE postimage.
// NO filesystem, foobar SDK, virtual-track writer or approval side effects.
#include "djmeta/cue_online_review.h"
#include "djmeta/cue_value_postimage.h"
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
namespace djmeta::online {
struct CuePostimageEvidence {
    std::optional<std::size_t> track;
    FieldEvidence evidence;
    bool edition_track=false;
    bool complete_edition_alignment=false; // advisory, NEVER write authority
};
struct CuePostimageCapturedRow {
    CuePostimageEvidence input;
    CueCandidateFieldReview reviewed;
};
struct CuePostimageReviewSnapshot {
    bool eligible=false;
    djmeta::CueCarrierKind carrier=djmeta::CueCarrierKind::ExternalText;
    std::string original_bytes;
    std::vector<CuePostimageCapturedRow> rows;
};
inline bool same_cue_postimage_review(
    const CueCandidateFieldReview& a,const CueCandidateFieldReview& b) {
    return a.evidence_index==b.evidence_index &&
           a.target==b.target && a.track_ordinal==b.track_ordinal &&
           a.cue_field==b.cue_field && a.original_values==b.original_values &&
           a.candidate==b.candidate && a.state==b.state &&
           a.original_inherited==b.original_inherited &&
           a.affected_inheriting_tracks==b.affected_inheriting_tracks &&
           a.reason==b.reason;
}
// Capture only source and provider evidence. A later explicit selection is
// required; a mere preview must not turn into a write or automatic accept.
inline CuePostimageReviewSnapshot capture_cue_postimage_review(
    std::string_view raw,djmeta::CueCarrierKind carrier,
    const std::vector<CuePostimageEvidence>& inputs) {
    CuePostimageReviewSnapshot out;
    if(raw.empty() || raw.size()>8u*1024u*1024u || inputs.size()>512)
        return out;
    const auto cue=djmeta::inspect_cue_metadata(raw,carrier);
    if(cue.status!=djmeta::CueSyntaxStatus::Parsed ||
       (cue.encoding!=djmeta::CueTextEncoding::Utf8 &&
        cue.encoding!=djmeta::CueTextEncoding::Utf8Bom))
        return out;
    out.carrier=carrier;
    out.original_bytes=std::string(raw);
    for(const auto& input:inputs) {
        const auto reviews=review_cue_candidate_fields(
            cue,input.track,{input.evidence},input.edition_track);
        if(reviews.size()!=1) return {};
        out.rows.push_back({input,reviews.front()});
    }
    out.eligible=true;
    return out;
}
// User-selected indices, never "apply all". Re-parse and re-review every
// selection against the unchanged original BEFORE calling the byte planner.
// No insertion for absent CUE fields, inheritance or ambiguous multivalues.
inline djmeta::CueValuePostimage preview_selected_cue_review_postimage(
    const CuePostimageReviewSnapshot& snapshot,
    std::string_view current_source,
    const std::vector<std::size_t>& selected) {
    const auto refuse=[](const char* why) {
        djmeta::CueValuePostimage r;r.rejection=why;return r;
    };
    if(!snapshot.eligible || snapshot.original_bytes!=current_source)
        return refuse("CUE_REVIEW_STALE_SOURCE");
    if(snapshot.rows.size()>512 || selected.size()>snapshot.rows.size())
        return refuse("CUE_REVIEW_BOUND");
    const auto cue=djmeta::inspect_cue_metadata(current_source,snapshot.carrier);
    if(cue.status!=djmeta::CueSyntaxStatus::Parsed)
        return refuse("CUE_REVIEW_INVALID_SOURCE");
    std::vector<djmeta::CueValueChange> changes;
    std::set<std::size_t> seen;
    for(const auto index:selected) {
        if(index>=snapshot.rows.size() || !seen.insert(index).second)
            return refuse("CUE_REVIEW_DUPLICATE_OR_UNKNOWN_SELECTION");
        const auto& saved=snapshot.rows[index];
        const auto now=review_cue_candidate_fields(cue,saved.input.track,
            {saved.input.evidence},saved.input.edition_track);
        if(now.size()!=1 || !same_cue_postimage_review(saved.reviewed,now[0]))
            return refuse("CUE_REVIEW_MUTATED_EVIDENCE");
        const auto& row=now.front();
        if(row.state==FieldReviewState::Unchanged) continue; // exact no-op
        if(row.state!=FieldReviewState::NeedsReview ||
           row.original_values.size()!=1 || row.original_inherited ||
           row.candidate.values.size()!=1 || row.cue_field.empty())
            return refuse("CUE_REVIEW_UNSAFE_FIELD");
        std::optional<std::size_t> ordinal;
        if(row.target==CueReviewTarget::AlbumGlobal) {
            if(saved.input.track ||
               row.candidate.scope!=EvidenceScope::Edition ||
               saved.input.edition_track ||
               !saved.input.complete_edition_alignment)
                return refuse("CUE_REVIEW_ALBUM_EDITION_UNQUALIFIED");
        } else if(row.target==CueReviewTarget::TrackLocal) {
            if(!saved.input.track || row.track_ordinal!=*saved.input.track)
                return refuse("CUE_REVIEW_TRACK_IDENTITY_UNQUALIFIED");
            ordinal=row.track_ordinal;
        } else return refuse("CUE_REVIEW_TARGET_UNSUPPORTED");
        changes.push_back({ordinal,row.cue_field,row.original_values.front(),
                           row.candidate.values.front()});
    }
    return djmeta::preview_cue_value_postimage(
        current_source,snapshot.carrier,changes);
}
} // namespace djmeta::online
