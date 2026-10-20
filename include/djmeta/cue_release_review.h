#pragma once
// Exact-edition CUE field comparison, read-only. Never authorizes a write.
#include "djmeta/cue_online_bridge.h"
#include "djmeta/cue_online_review.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace djmeta::online {

struct CueEditionTrackReview {
    std::size_t cue_ordinal = std::numeric_limits<std::size_t>::max();
    std::size_t edition_track_index = no_release_track;
    MatchDecision decision = MatchDecision::Review;
    std::vector<std::string> reasons;
    std::vector<CueCandidateFieldReview> fields;
    // Separate blocked-only display facts from verified candidate fields.
    // These cannot authorize CUE track edits or any carrier writer.
    std::vector<CueCandidateFieldReview> unverified_fields;
};

struct CueEditionReview {
    bool eligible = false; // Input qualified; never permission to modify.
    ReleaseAlignment alignment;
    std::vector<CueCandidateFieldReview> album_fields;
    std::vector<CueEditionTrackReview> tracks;
    std::vector<std::string> reasons;
};

inline CueEditionReview review_cue_release_edition(
    const CueMetadataInventory& cue, const ReleaseEdition& edition) {
    CueEditionReview result;
    // Verify the inspected edition BEFORE offering even blocked album facts.
    // Unqualified CUE track identities must never launder foreign provenance.
    if (!valid_evidence_origin(edition.provider, 128) ||
        !valid_evidence_origin(edition.edition_id, 512)) {
        result.reasons.push_back("cue_release_identity_unqualified");
        return result;
    }
    // A master/release-group or another provider's album fields cannot be
    // silently presented as the exact edition being inspected.
    for (const auto& field : edition.fields)
        if (field.provider != edition.provider ||
            field.source_id != edition.edition_id ||
            field.scope != EvidenceScope::Edition) {
            result.reasons.push_back("cue_release_field_provenance_mismatch");
            return result;
        }
    const auto local = prepare_cue_release_lookup(cue);
    if (!local.eligible) {
        result.reasons = local.reasons;
        if (cue.status != CueSyntaxStatus::Parsed) return result;
        // A readable CUE with missing/ambiguous track identity can still
        // inspect actual album-level facts. They are display-only BLOCKED,
        // not proposed edits, and no recording receives remote credits.
        result.album_fields = review_cue_candidate_fields(
            cue, std::nullopt, edition.fields);
        for (auto& row : result.album_fields) {
            row.state = FieldReviewState::Blocked;
            row.reason = "cue_track_inventory_unqualified_" + row.reason;
        }
        for (const auto& track : cue.tracks) {
            CueEditionTrackReview row;
            row.cue_ordinal = track.ordinal;
            row.reasons = local.reasons;
            result.tracks.push_back(std::move(row));
        }
        return result;
    }
    result.alignment = align_release(local.tracks, edition);
    if (result.alignment.tracks.size() != local.tracks.size()) {
        result.reasons.push_back("cue_release_alignment_unqualified");
        return result;
    }
    result.eligible = true;
    result.album_fields = review_cue_candidate_fields(
        cue, std::nullopt, edition.fields);
    for (const auto& match : result.alignment.tracks) {
        CueEditionTrackReview row;
        row.cue_ordinal = match.source_index;
        row.edition_track_index = match.edition_track_index;
        row.decision = match.decision;
        row.reasons = match.conflicts;
        // Never attribute provider fields without an individual, qualified
        // source-row identity; no guessing from an album-wide release.
        if (match.edition_track_index == no_release_track ||
            match.edition_track_index >= edition.tracks.size()) {
            row.reasons.push_back("cue_track_match_requires_confirmation");
            result.tracks.push_back(std::move(row));
            continue;
        }
        const auto index = match.edition_track_index;
        const auto& remote = edition.tracks[index];
        bool valid = !remote.fields.empty();
        const std::string expected_discogs_id =
            edition.edition_id + "/track/" + std::to_string(index + 1);
        const std::string source_id = remote.fields.empty()
            ? std::string{} : remote.fields.front().source_id;
        if (!valid_evidence_origin(source_id, 512) ||
            (edition.provider == "discogs" && source_id != expected_discogs_id))
            valid = false;
        for (const auto& field : remote.fields)
            if (field.provider != edition.provider ||
                field.scope != EvidenceScope::Edition ||
                field.source_id != source_id) valid = false;
        if (!valid) {
            row.reasons.push_back("cue_edition_track_provenance_mismatch");
            result.tracks.push_back(std::move(row));
            continue;
        }
        if (match.decision != MatchDecision::Suggested ||
            !match.conflicts.empty()) {
            row.reasons.push_back("cue_track_match_requires_confirmation");
            // INDEX positions do not qualify sample-accurate durations.
            // A unique alignment with exact title and artist can still show
            // source-authenticated, explicitly BLOCKED candidate facts.
            const bool weak_only=std::all_of(
                match.conflicts.begin(),match.conflicts.end(),
                [](const std::string& reason) {
                    return reason=="mix_identity_unverified" ||
                           reason=="isrc_missing_on_one_side" ||
                           reason=="named_mix_missing_on_one_side";
                });
            const bool exact_names=std::find(match.evidence.begin(),
                match.evidence.end(),"title_exact")!=match.evidence.end() &&
                std::find(match.evidence.begin(),match.evidence.end(),
                    "primary_artist_exact")!=match.evidence.end();
            if(match.decision==MatchDecision::Review &&
               weak_only && exact_names) {
                row.unverified_fields=review_cue_edition_track_fields(
                    cue,row.cue_ordinal,remote);
                for(auto& field:row.unverified_fields) {
                    field.state=FieldReviewState::Blocked;
                    field.reason="cue_unverified_alignment_evidence_only_"+field.reason;
                }
            }
            result.tracks.push_back(std::move(row));
            continue;
        }
        row.fields=review_cue_edition_track_fields(cue,row.cue_ordinal,remote);
        result.tracks.push_back(std::move(row));
    }
    return result;
}

} // namespace djmeta::online
