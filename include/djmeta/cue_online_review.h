#pragma once

// Conservative read-only target mapping from online evidence to textual CUE
// album/track semantics. Never generates REM tags, patches bytes or approves
// a foobar metadata write.
#include "djmeta/cue_metadata.h"
#include "djmeta/online_fields.h"

#include <cstddef>
#include <limits>
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
    const std::vector<FieldEvidence>& evidence) {
    std::vector<CueCandidateFieldReview> output;
    output.reserve(evidence.size());

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
        if (proposed.provider.empty() || proposed.source_id.empty() ||
            !valid_utf8_metadata_text(proposed.field) ||
            proposed.values.size() != 1 || proposed.values[0].empty() ||
            proposed.values[0].find('\0') != std::string::npos ||
            !valid_utf8_metadata_text(proposed.values[0])) {
            row.reason = "cue_evidence_invalid_or_multivalue";
            output.push_back(std::move(row));
            continue;
        }
        const auto field = ascii_upper_field(proposed.field);
        if (field == "DATE" && proposed.date_meaning == DateMeaning::NotDate) {
            row.reason = "cue_date_role_unverified";
            output.push_back(std::move(row));
            continue;
        }
        row.target = proposed.scope == EvidenceScope::Recording
            ? CueReviewTarget::TrackLocal : CueReviewTarget::AlbumGlobal;
        row.cue_field = known_cue_target(field, row.target);
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

} // namespace djmeta::online
