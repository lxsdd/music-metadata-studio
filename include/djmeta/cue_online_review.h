#pragma once

// Conservative read-only target mapping from online evidence to textual CUE
// album/track semantics. Never generates REM tags, patches bytes or approves
// a foobar metadata write.
#include "djmeta/cue_metadata.h"
#include "djmeta/online_fields.h"

#include <cstddef>
#include <limits>
#include <map>
#include <tuple>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta::online {

enum class CueReviewTarget { AlbumGlobal, TrackLocal, Unsupported };

struct CueCandidateFieldReview {
    std::size_t evidence_index = 0;
    CueReviewTarget target = CueReviewTarget::Unsupported;
    std::size_t track_ordinal = std::numeric_limits<std::size_t>::max();
    std::string cue_field; // Qualified canonical CUE token name.
    std::vector<std::string> original_values; // Never flatten duplicates.
    FieldEvidence candidate;
    FieldReviewState state = FieldReviewState::Blocked;
    bool original_inherited = false;
    std::size_t affected_inheriting_tracks = 0;
    std::string reason;
};

inline std::string known_cue_target(std::string_view source_field,
                                    CueReviewTarget scope) {
    const auto name = ascii_upper_field(source_field);
    if (scope == CueReviewTarget::AlbumGlobal) {
        if (name == "ALBUM") return "TITLE";
        if (name == "ALBUM ARTIST") return "PERFORMER";
        if (name == "GENRE") return "REM GENRE";
        if (name == "DISCNUMBER") return "REM DISCNUMBER";
        if (name == "TOTALDISCS") return "REM TOTALDISCS";
        if (name == "DATE") return "REM DATE";
        if (name == "COMMENT") return "REM COMMENT";
    } else if (scope == CueReviewTarget::TrackLocal) {
        if (name == "TITLE") return "TITLE";
        if (name == "ARTIST") return "PERFORMER";
        if (name == "SONGWRITER") return "SONGWRITER";
        if (name == "ISRC") return "ISRC";
        if (name == "DATE") return "REM DATE";
        if (name == "COMMENT") return "REM COMMENT";
    }
    return {};
}

inline std::vector<CueCandidateFieldReview> review_cue_candidate_fields(
    const CueMetadataInventory& cue,
    std::optional<std::size_t> track_ordinal,
    const std::vector<FieldEvidence>& evidence,
    bool edition_track_evidence = false) {
    std::vector<CueCandidateFieldReview> output;
    output.reserve(evidence.size());
    // An explicitly selected release-edition TRACK may carry Edition-scoped
    // provenance. Only this opt-in review maps such evidence to a CUE track;
    // ordinary edition fields continue to target the album global scope.
    const auto target_for = [&](const FieldEvidence& proposed) {
        return proposed.scope == EvidenceScope::Recording ||
               (edition_track_evidence && proposed.scope == EvidenceScope::Edition)
            ? CueReviewTarget::TrackLocal : CueReviewTarget::AlbumGlobal;
    };
    // Duplicate canonical CUE targets from one source are ambiguous, even
    // when the input uses different case or equivalent field aliases.
    using EvidenceKey = std::tuple<std::string, std::string, std::string,
                                   EvidenceScope>;
    std::map<EvidenceKey, std::size_t> occurrence_count;
    for (const auto& proposed : evidence) {
        const auto target = target_for(proposed);
        const auto canonical = known_cue_target(proposed.field, target);
        if (!canonical.empty())
            ++occurrence_count[{canonical, proposed.provider,
                                proposed.source_id, proposed.scope}];
    }

    for (std::size_t i = 0; i < evidence.size(); ++i) {
        const auto& proposed = evidence[i];
        CueCandidateFieldReview row;
        row.evidence_index = i;
        row.candidate = proposed;
        if (cue.status != CueSyntaxStatus::Parsed) {
            row.reason = "cue_inventory_unqualified";
            output.push_back(std::move(row));
            continue;
        }
        if (proposed.field.empty() || proposed.field.size() > 256 ||
            !valid_utf8_metadata_text(proposed.field) ||
            !valid_evidence_origin(proposed.provider, 128) ||
            !valid_evidence_origin(proposed.source_id, 512) ||
            proposed.values.size() != 1 ||
            !valid_evidence_origin(proposed.values[0], 8192)) {
            row.reason = "cue_evidence_invalid_or_multivalue";
            output.push_back(std::move(row));
            continue;
        }
        const auto field = ascii_upper_field(proposed.field);
        if (edition_track_evidence && proposed.scope != EvidenceScope::Edition) {
            row.reason = "cue_edition_track_scope_mismatch";
            output.push_back(std::move(row));
            continue;
        }
        // An edition publication date is album evidence, never a local
        // recording date, even when carried beside track-specific credits.
        if (edition_track_evidence && field == "DATE") {
            row.reason = "cue_edition_date_not_track_local";
            output.push_back(std::move(row));
            continue;
        }
        if (field == "DATE" && proposed.date_meaning == DateMeaning::NotDate) {
            row.reason = "cue_date_role_unverified";
            output.push_back(std::move(row));
            continue;
        }
        row.target = target_for(proposed);
        row.cue_field = known_cue_target(field, row.target);
        if (!row.cue_field.empty() &&
            occurrence_count[{row.cue_field, proposed.provider,
                              proposed.source_id, proposed.scope}] > 1) {
            row.reason = "cue_duplicate_provider_field_evidence";
            output.push_back(std::move(row));
            continue;
        }
        if (field == "DATE" && row.target == CueReviewTarget::AlbumGlobal &&
            proposed.date_meaning != DateMeaning::EditionRelease) {
            row.reason = "cue_date_scope_mismatch";
            output.push_back(std::move(row));
            continue;
        }
        if (row.cue_field.empty()) {
            row.target = CueReviewTarget::Unsupported;
            row.reason = "cue_field_not_representable";
            output.push_back(std::move(row));
            continue;
        }
        std::size_t count = 0;
        if (row.target == CueReviewTarget::TrackLocal) {
            if (!track_ordinal || *track_ordinal >= cue.tracks.size()) {
                row.reason = "cue_track_identity_missing";
                output.push_back(std::move(row));
                continue;
            }
            row.track_ordinal = *track_ordinal;
            const auto& target = cue.tracks[*track_ordinal];
            for (const auto& existing : target.local_fields) {
                if (existing.name == row.cue_field) {
                    row.original_values.push_back(existing.value);
                    ++count;
                }
            }
            if (count == 0) {
                const auto inherited =
                    effective_cue_field(cue, *track_ordinal, row.cue_field);
                if (inherited.ambiguous) {
                    row.reason = "cue_inheritance_ambiguous";
                    output.push_back(std::move(row));
                    continue;
                }
                if (inherited.present && inherited.inherited) {
                    row.original_values.push_back(inherited.value);
                    row.original_inherited = true;
                }
            }
        } else {
            for (const auto& existing : cue.globals) {
                if (existing.name == row.cue_field) {
                    row.original_values.push_back(existing.value);
                    ++count;
                }
            }
            if (row.cue_field != "TITLE" && row.cue_field != "ISRC") {
                for (std::size_t track = 0; track < cue.tracks.size(); ++track) {
                    const auto inherited = effective_cue_field(
                        cue, track, row.cue_field);
                    if (inherited.inherited ||
                        (!inherited.present && !inherited.ambiguous))
                        ++row.affected_inheriting_tracks;
                }
            }
        }
        if (count > 1) {
            row.reason = "cue_duplicated_target_token";
        } else if (row.original_values.size() == 1 &&
                   row.original_values[0] == proposed.values[0]) {
            row.state = FieldReviewState::Unchanged;
            row.reason = "exact_noop";
            row.affected_inheriting_tracks = 0;
        } else {
            row.state = FieldReviewState::NeedsReview;
            row.reason = row.original_inherited
                ? "track_override_would_be_required"
                : "cue_field_value_differs";
        }
        output.push_back(std::move(row));
    }
    return output;
}

// The caller must already have selected and verified the one-to-one CUE to
// release-track association. This function is read-only: it never changes
// provenance scope, invents a CUE token, or authorizes an edit.
inline std::vector<CueCandidateFieldReview> review_cue_edition_track_fields(
    const CueMetadataInventory& cue,
    std::optional<std::size_t> track_ordinal,
    const EditionTrack& edition_track) {
    auto rows = review_cue_candidate_fields(
        cue, track_ordinal, edition_track.fields, true);
    if (edition_track.fields.empty()) return rows;
    const auto& identity = edition_track.fields.front().source_id;
    const auto& provider = edition_track.fields.front().provider;
    // Equal IDs across different providers do not establish provenance.
    bool consistent = valid_evidence_origin(identity, 512) &&
                      valid_evidence_origin(provider, 128);
    for (const auto& field : edition_track.fields)
        if (field.source_id != identity || field.provider != provider ||
            field.scope != EvidenceScope::Edition) consistent = false;
    if (!consistent)
        for (auto& row : rows) {
            row.state = FieldReviewState::Blocked;
            row.reason = "cue_edition_track_identity_unqualified";
        }
    return rows;
}

} // namespace djmeta::online
