#pragma once
// Explicit online CUE field selections -> exact in-memory CUE postimage.
// NO filesystem, foobar SDK, virtual-track writer or approval side effects.
#include "djmeta/cue_online_review.h"
#include "djmeta/cue_value_postimage.h"
#include "djmeta/cue_release_review.h"
#include "djmeta/cue_musicbrainz_release_gate.h"
#include <cstddef>
#include <optional>
#include <set>
#include <stdexcept>
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
// Map selected HWND detail row indices to immutable CUE evidence rows.
// Never use the sorted view-row index as a CUE track/edition identity.
// A multi-select is atomic: one ambiguous/missing/duplicate row rejects all.
inline std::vector<std::size_t> resolve_cue_detail_selection(
    const CuePostimageReviewSnapshot& capture,
    const std::vector<FieldReviewRow>& displayed,
    const std::vector<std::size_t>& selected_detail_indices) {
    if(!capture.eligible || selected_detail_indices.empty() ||
       selected_detail_indices.size()>512 ||
       selected_detail_indices.size()>displayed.size())
        throw std::invalid_argument("CUE_DETAIL_SELECTION_SHAPE_INVALID");
    std::set<std::size_t> selected_rows,selected_evidence;
    std::vector<std::size_t> result;
    for(const auto view_index:selected_detail_indices) {
        if(view_index>=displayed.size() ||
           !selected_rows.insert(view_index).second)
            throw std::invalid_argument("CUE_DETAIL_SELECTION_DUPLICATE_OR_UNKNOWN");
        const auto& detail=displayed[view_index];
        std::optional<std::size_t> resolved;
        for(std::size_t j=0;j<capture.rows.size();++j) {
            const auto& source=capture.rows[j].reviewed;
            if(source.candidate==detail.candidate &&
               source.original_values==detail.original_values &&
               source.state==detail.state) {
                if(resolved)
                    throw std::invalid_argument("CUE_DETAIL_SOURCE_AMBIGUOUS");
                resolved=j;
            }
        }
        if(!resolved || !selected_evidence.insert(*resolved).second)
            throw std::invalid_argument("CUE_DETAIL_SELECTION_UNQUALIFIED");
        result.push_back(*resolved);
    }
    return result;
}

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

 // Source-verified release adapter. A caller cannot supply an arbitrary
 // "complete_edition_alignment" boolean: this path recomputes exact edition
 // alignment, track identities and provider-specific provenance from source.
 // Still ONLY an in-memory preview, never a CUE/audio/foobar writer.
inline CuePostimageReviewSnapshot capture_qualified_cue_edition_postimage(
    std::string_view raw,djmeta::CueCarrierKind carrier,
    const ReleaseEdition& edition) {
    if(edition.provider!="musicbrainz" && edition.provider!="discogs")
        return {};
    const auto cue=djmeta::inspect_cue_metadata(raw,carrier);
    if(cue.status!=djmeta::CueSyntaxStatus::Parsed)
        return {};
    const auto reviewed=review_cue_release_edition(cue,edition);
    if(!reviewed.eligible ||
       reviewed.alignment.provider!=edition.provider ||
       reviewed.alignment.edition_id!=edition.edition_id)
        return {};
    const auto& matches=reviewed.alignment.tracks;
    bool full=matches.size()==cue.tracks.size() &&
              matches.size()==edition.tracks.size() && !matches.empty();
    std::set<std::size_t> seen_local,seen_remote;
    for(const auto& match:matches) {
        if(match.decision!=MatchDecision::Suggested ||
           !match.conflicts.empty() ||
           match.source_index>=cue.tracks.size() ||
           match.edition_track_index>=edition.tracks.size() ||
           !seen_local.insert(match.source_index).second ||
           !seen_remote.insert(match.edition_track_index).second ||
           !qualified_cue_edition_track_provenance(
               edition,match.edition_track_index))
            full=false;
    }
    full=full && seen_local.size()==cue.tracks.size() &&
         seen_remote.size()==edition.tracks.size();
    if(edition.provider=="musicbrainz")
        full=full && qualified_musicbrainz_cue_album_alignment(
            cue,edition,reviewed.alignment);
    std::vector<CuePostimageEvidence> inputs;
    std::vector<CueCandidateFieldReview> expected;
    const auto append=[&](const CueCandidateFieldReview& field,
                          std::optional<std::size_t> track,bool track_field) {
        if(field.state!=FieldReviewState::NeedsReview &&
           field.state!=FieldReviewState::Unchanged)
            return;
        if(inputs.size()>=512) return;
        inputs.push_back({track,field.candidate,track_field,full});
        expected.push_back(field);
    };
    // Album changes require whole-edition identity; equal values may still
    // be displayed as no-ops, never interpreted as a write permission.
    for(const auto& field:reviewed.album_fields)
        if(full || field.state==FieldReviewState::Unchanged)
            append(field,std::nullopt,false);
    for(const auto& track:reviewed.tracks) {
        if(track.decision!=MatchDecision::Suggested ||
           !track.reasons.empty() ||
           track.cue_ordinal>=cue.tracks.size() ||
           track.edition_track_index>=edition.tracks.size())
            continue;
        const auto& remote=edition.tracks[track.edition_track_index];
        if(!qualified_cue_edition_track_provenance(
                edition,track.edition_track_index))
            continue;
        if(edition.provider=="musicbrainz") {
            if(!musicbrainz::valid_mbid(remote.source_track_id))
                continue;
            bool provenance=true;
            for(const auto& field:remote.fields)
                if(field.source_id!=remote.source_track_id)
                    provenance=false;
            if(!provenance) continue;
        }
        for(const auto& field:track.fields)
            append(field,track.cue_ordinal,true);
    }
    // A release with no independently verified reviewable rows cannot
    // yield an eligible edition snapshot.
    if(inputs.empty() || inputs.size()!=expected.size()) return {};
    auto snapshot=capture_cue_postimage_review(raw,carrier,inputs);
    if(!snapshot.eligible || snapshot.rows.size()!=expected.size())
        return {};
    for(std::size_t i=0;i<expected.size();++i) {
        auto original=expected[i];
        original.evidence_index=0; // single-field immutable captured row
        if(!same_cue_postimage_review(snapshot.rows[i].reviewed,original))
            return {}; // includes aliases, conflicting or duplicated fields
    }
    return snapshot;
}

} // namespace djmeta::online
