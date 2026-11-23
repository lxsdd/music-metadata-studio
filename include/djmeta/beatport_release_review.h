#pragma once
// Offline, user-initiated Beatport exact-edition review. No HTTP, CUE or
// physical writes, no inferred authorization and no guessed track mapping.
#include "djmeta/beatport_manual_intake.h"
#include "djmeta/cue_online_bridge.h"
#include "djmeta/cue_online_review.h"
#include "djmeta/cue_original_inventory.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta::online {

inline void validate_manual_beatport_edition(
    const ReleaseEdition& edition,std::string_view provider="beatport") {
    if (edition.provider != provider ||
        !valid_evidence_origin(edition.edition_id, 512) ||
        edition.fields.size() > 256 ||
        edition.tracks.empty() || edition.tracks.size() > 200)
        throw std::invalid_argument("Unqualified pasted Beatport release");
    for (const auto& field : edition.fields)
        if (field.provider != provider ||
            field.scope != EvidenceScope::Edition ||
            field.source_id != edition.edition_id)
            throw std::invalid_argument("Beatport edition provenance mismatch");
    for (const auto& track : edition.tracks) {
        if (!valid_evidence_origin(track.source_track_id, 512) ||
            track.fields.empty() || track.fields.size() > 64)
            throw std::invalid_argument("Beatport edition track lacks identity");
        for (const auto& field : track.fields) {
            const auto expected = field.scope == EvidenceScope::Recording
                ? track.source_track_id : edition.edition_id;
            if (field.provider != provider ||
                field.source_id != expected ||
                (field.scope != EvidenceScope::Recording &&
                 field.scope != EvidenceScope::Edition))
                throw std::invalid_argument("Beatport track field provenance mismatch");
        }
    }
}

inline std::optional<LocalReleaseTrack> beatport_physical_identity(
    const MetadataDocument& originals, std::int64_t duration_ms) {
    const auto title = unique_beatport_local_value(originals, "TITLE");
    const auto artist = unique_beatport_local_value(originals, "ARTIST");
    const auto version = unique_beatport_local_value(originals, "VERSION");
    const auto isrc = unique_beatport_local_value(originals, "ISRC");
    const auto present = [&](std::string_view name) {
        return std::any_of(originals.fields.begin(), originals.fields.end(),
            [&](const auto& f) { return ascii_upper_field(f.name) == name; });
    };
    if (!title || !artist ||
        !valid_evidence_origin(*title, 2048) ||
        !valid_evidence_origin(*artist, 2048) ||
        (present("VERSION") && (!version ||
            !valid_evidence_origin(*version, 512))) ||
        (present("ISRC") && (!isrc ||
            !valid_evidence_origin(*isrc, 128))) ||
        duration_ms <= 0 || duration_ms > 36000000)
        return std::nullopt;
    LocalReleaseTrack local;
    local.recording.title = *title;
    local.recording.primary_artist = *artist;
    local.recording.duration_ms = duration_ms;
    if (version) {
        local.recording.mix_name = *version;
        local.recording.mix = classify_mix_annotation(*version);
    }
    if (isrc) local.recording.isrc = *isrc;
    return local;
}

inline std::vector<FieldReviewRow> review_beatport_user_edition_physical(
    const MetadataDocument& originals, std::int64_t duration_ms,
    const ReleaseEdition& edition,
    std::string_view provider="beatport") {
    validate_manual_beatport_edition(edition,provider);
    if (originals.fields.size() > 10000)
        throw std::invalid_argument("Unbounded physical tag inventory");
    const auto local = beatport_physical_identity(originals, duration_ms);
    std::optional<TrackAssociation> chosen;
    std::size_t verified = no_release_track;
    if (local) {
        const auto alignment = align_release({*local}, edition);
        if (alignment.tracks.size() == 1) {
            chosen = alignment.tracks.front();
            if (chosen->decision == MatchDecision::Suggested &&
                chosen->conflicts.empty() &&
                chosen->edition_track_index < edition.tracks.size())
                verified = chosen->edition_track_index;
        }
    }
    std::vector<FieldReviewRow> rows = review_online_fields(
        originals, edition.fields);
    for (auto& row : rows) {
        row.field = "Album / " + row.field;
        row.reason = "manual_beatport_edition_" + row.reason;
        if (verified == no_release_track)
            row.state = FieldReviewState::Blocked;
    }
    for (std::size_t i = 0; i < edition.tracks.size(); ++i) {
        const auto& track = edition.tracks[i];
        const auto prefix = "Release Track " + std::to_string(i + 1) + " / ";
        FieldReviewRow diagnostic;
        diagnostic.field = prefix + "MATCH";
        diagnostic.candidate = {"MATCH_DIAGNOSTIC", {}, std::string(provider),
            track.source_track_id, EvidenceScope::Recording,
            DateMeaning::NotDate};
        diagnostic.state = i == verified
            ? FieldReviewState::Unchanged : FieldReviewState::Blocked;
        diagnostic.reason = i == verified
            ? "manual_beatport_edition_track_suggested_readonly"
            : "manual_beatport_edition_track_not_safely_matched";
        if (chosen && chosen->edition_track_index == i)
            diagnostic.candidate.values = i == verified
                ? chosen->evidence : chosen->conflicts;
        if (diagnostic.candidate.values.empty())
            diagnostic.candidate.values = {
                i == verified ? "Suggested only; no write approval"
                              : "Track identity not independently verified"};
        rows.push_back(std::move(diagnostic));
        auto compared = review_online_fields(originals, track.fields);
        for (auto& row : compared) {
            row.field = prefix + row.field;
            row.reason = "manual_beatport_edition_track_" + row.reason;
            if (i != verified) row.state = FieldReviewState::Blocked;
            rows.push_back(std::move(row));
        }
    }
    // The provider's absent values are never deletion instructions.
    for (const auto& field : originals.fields) {
        const auto supplied = [&](const FieldEvidence& evidence) {
            return ascii_upper_field(evidence.field) ==
                ascii_upper_field(field.name);
        };
        bool present = std::any_of(edition.fields.begin(),
            edition.fields.end(), supplied);
        if (verified != no_release_track)
            present = present || std::any_of(
                edition.tracks[verified].fields.begin(),
                edition.tracks[verified].fields.end(), supplied);
        if (!present) {
            auto row = make_local_physical_original(
                field, "manual_beatport_local_only_no_provider_value");
            row.field = "Physical / " + field.name;
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

inline std::vector<FieldReviewRow> review_beatport_user_edition_cue(
    const CueMetadataInventory& cue, const ReleaseEdition& edition) {
    validate_manual_beatport_edition(edition);
    const auto local = prepare_cue_release_lookup(cue);
    if (cue.status != CueSyntaxStatus::Parsed)
        throw std::invalid_argument("Beatport CUE carrier is not fully parsed");
    ReleaseAlignment alignment;
    if (local.eligible) alignment = align_release(local.tracks, edition);
    // Reading a valid CUE is not proof that the pasted edition belongs to
    // that album. Do not make album-global changes reviewable when any
    // local track or release track is missing, uncertain or in conflict.
    bool complete_edition_alignment = local.eligible &&
        local.tracks.size() == edition.tracks.size() &&
        alignment.tracks.size() == local.tracks.size();
    std::vector<bool> matched_remote(edition.tracks.size(), false);
    for (const auto& match : alignment.tracks) {
        if (match.decision != MatchDecision::Suggested ||
            !match.conflicts.empty() ||
            match.edition_track_index >= edition.tracks.size() ||
            matched_remote[match.edition_track_index]) {
            complete_edition_alignment = false;
        } else {
            matched_remote[match.edition_track_index] = true;
        }
    }
    // Evaluate the whole edition field set at once: duplicate canonical
    // aliases and conflicting dates must not bypass the CUE token guard.
    std::vector<FieldReviewRow> rows;
    const auto album_review = review_cue_candidate_fields(
        cue, std::nullopt, edition.fields);
    for (const auto& item : album_review) {
        FieldReviewRow row;
        row.field = "Album / " + (item.cue_field.empty()
            ? item.candidate.field : item.cue_field);
        row.original_values = item.original_values;
        row.candidate = item.candidate;
        row.state = complete_edition_alignment
            ? item.state : FieldReviewState::Blocked;
        row.reason = complete_edition_alignment
            ? "manual_beatport_cue_album_" + item.reason
            : "manual_beatport_cue_unverified_edition_album_evidence_only_"
                + item.reason;
        rows.push_back(std::move(row));
    }
    for (std::size_t i = 0; i < edition.tracks.size(); ++i) {
        const auto& track = edition.tracks[i];
        const TrackAssociation* link = nullptr;
        for (const auto& association : alignment.tracks)
            if (association.edition_track_index == i) {
                link = &association;
                break;
            }
        const bool assigned = link && link->decision != MatchDecision::Rejected &&
            link->source_index < cue.tracks.size();
        const bool suggested = assigned &&
            link->decision == MatchDecision::Suggested &&
            link->conflicts.empty();
        const bool weak = assigned &&
            link->decision == MatchDecision::Review &&
            std::all_of(link->conflicts.begin(), link->conflicts.end(),
                [](const auto& reason) {
                    return reason == "mix_identity_unverified" ||
                           reason == "isrc_missing_on_one_side" ||
                           reason == "named_mix_missing_on_one_side";
                }) &&
            std::find(link->evidence.begin(), link->evidence.end(),
                "title_exact") != link->evidence.end() &&
            std::find(link->evidence.begin(), link->evidence.end(),
                "primary_artist_exact") != link->evidence.end();
        const auto prefix = assigned
            ? "CUE Track " + std::to_string(link->source_index + 1) + " / "
            : "Edition Track " + std::to_string(i + 1) + " / ";
        FieldReviewRow diagnostic;
        diagnostic.field = prefix + "MATCH";
        diagnostic.candidate = {"MATCH_DIAGNOSTIC", {}, "beatport",
            track.source_track_id, EvidenceScope::Recording,
            DateMeaning::NotDate};
        diagnostic.state = suggested
            ? FieldReviewState::Unchanged : FieldReviewState::Blocked;
        diagnostic.reason = suggested
            ? "manual_beatport_cue_suggested_readonly"
            : "manual_beatport_cue_track_unverified";
        diagnostic.candidate.values = link
            ? (suggested ? link->evidence : link->conflicts)
            : std::vector<std::string>{};
        if (diagnostic.candidate.values.empty())
            diagnostic.candidate.values = {"No verified CUE-to-edition track"};
        rows.push_back(std::move(diagnostic));
        // One batch per remote recording: otherwise TITLE/title aliases,
        // different DATE candidates or other duplicate CUE targets each
        // appear independently eligible after a one-field-at-a-time check.
        std::vector<FieldEvidence> recording_evidence;
        for (const auto& field : track.fields)
            if (field.scope == EvidenceScope::Recording)
                recording_evidence.push_back(field);
        std::vector<CueCandidateFieldReview> compared;
        if (suggested || weak)
            compared = review_cue_candidate_fields(
                cue, link->source_index, recording_evidence);
        std::size_t recording_index = 0;
        for (const auto& field : track.fields) {
            FieldReviewRow row;
            row.field = prefix + field.field;
            row.candidate = field;
            row.reason = "manual_beatport_cue_track_not_qualified";
            if (field.scope == EvidenceScope::Recording) {
                if (suggested || weak) {
                    const auto& checked = compared.at(recording_index);
                    row.original_values = checked.original_values;
                    row.state = suggested ? checked.state : FieldReviewState::Blocked;
                    row.reason = suggested
                        ? "manual_beatport_cue_track_" + checked.reason
                        : "manual_beatport_cue_weak_alignment_evidence_only_"
                            + checked.reason;
                }
                ++recording_index;
            } else if (field.scope == EvidenceScope::Edition) {
                row.reason = "manual_beatport_cue_edition_field_not_track_local";
            }
            rows.push_back(std::move(row));
        }
    }
    // Never hide the original CUE carrier behind pasted provider evidence.
    // Duplicates, unknown REM, local track fields and FILE remain separate.
    const auto original = cue_original_inventory_rows(cue,"CUE Original / ");
    constexpr std::size_t maximum_rows = 12000;
    if (original.size() > maximum_rows ||
        rows.size() > maximum_rows - original.size())
        throw std::invalid_argument("Beatport CUE comparison exceeds row limit");
    rows.insert(rows.end(),original.begin(),original.end());
    return rows;
}

} // namespace djmeta::online
