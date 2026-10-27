#pragma once
// Pure, source-only handoff into the canonical Music Metadata Studio review.
// No second window, no foobar handles, no writer, no filesystem or HTTP.
// All rows are advisory: even exact 75-fps agreement is NOT audio proof.
#include "djmeta/precision_cue_assemble.h"
#include "djmeta/precision_cue_reference_diff.h"
#include "djmeta/precision_cue_source_consensus.h"
#include "djmeta/precision_cue_write_preflight.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace djmeta {

struct PrecisionCueStudioSourceView {
    // Obtained independently from the foobar selection. Never infer a
    // physical source from a virtual CUE subsong or a displayed filename.
    std::uint32_t subsong = 0;
    bool host_readonly_physical_probe_passed = false;
    // Exact canonical identity from the independent physical-file host probe.
    // A basename, virtual subsong path or mutable UI label is insufficient.
    std::string physical_source_identity;
};

enum class PrecisionCueStudioHandoffState {
    Blocked,
    RowsRequireReview,
    ReadOnlyRows
};

enum class PrecisionCueStudioReferenceState {
    NotProvided,
    ExactFrameNotAudioProof,
    WithinOneFrameNotAudioProof,
    ReviewRequired,
    Unqualified
};

struct PrecisionCueStudioReviewRow {
    unsigned disc_number = 0;
    unsigned track_number = 0;
    std::size_t physical_selection_index = 0;
    std::string physical_basename;
    std::string title;
    std::string performer;
    std::string provider;
    std::string provider_release_id;
    // Caller-supplied Deezer catalog evidence, never a transport attestation
    // or an independent frame-resolution clock. Delta = Deezer minus Apple.
    std::string corroborating_provider;
    std::string corroborating_release_id;
    unsigned corroborating_resolution_ms = 0;
    std::optional<std::int64_t> signed_corroborating_track_delta_ms;
    std::optional<std::int64_t> signed_corroborating_cumulative_delta_ms;
    std::int64_t elapsed_ms = 0;
    std::int64_t cue_frame = 0;
    PrecisionCueStudioReferenceState reference_state =
        PrecisionCueStudioReferenceState::NotProvided;
    std::optional<std::int64_t> signed_reference_delta_frames;
};

struct PrecisionCueStudioHandoff {
    PrecisionCueStudioHandoffState state =
        PrecisionCueStudioHandoffState::Blocked;
    const char* diagnostic = "STUDIO_HANDOFF_BLOCKED";
    // Cross-provider catalog consistency is separate from reference CUE
    // comparison and from physical source identity. No write authorization.
    PrecisionCueConsensusState catalog_consensus_state =
        PrecisionCueConsensusState::Rejected;
    const char* catalog_diagnostic = "STUDIO_CATALOG_EVIDENCE_NOT_ATTESTED";
    std::optional<PrecisionCueCoarseDurationConflict> first_coarse_conflict;
    std::vector<PrecisionCueStudioReviewRow> rows;
};

// The existing Studio workspace owns the only UI, selected physical handles,
// per-field decisions and eventual independently authorized write controller.
// The caller passes its host selection and optional read-only reference CUE
// bytes. This contract deliberately returns NO write/approval token.
// Both sources and reference_cues are indexed by PHYSICAL SELECTION order,
// not by release disc number. The reviewed permutation maps a release disc
// to the matching selected physical source and its sibling reference CUE.
// Recompute the entire provider/edition/disc plan: a mutated or stale UI
// preview cannot be laundered into a trusted review row.
inline PrecisionCueStudioHandoff prepare_precision_cue_studio_review(
    const PrecisionCuePreviewRequest& request,
    const PrecisionCuePreviewResult& displayed,
    const std::vector<PrecisionCueStudioSourceView>& sources,
    const std::vector<std::optional<std::string>>& reference_cues) {
    PrecisionCueStudioHandoff out;
    const auto count = request.physical_discs.size();
    if (count == 0 || count > 8 || sources.size() != count ||
        reference_cues.size() != count ||
        request.reviewed_physical_index_by_disc.size() != count) {
        out.diagnostic = "STUDIO_SELECTION_OR_MAPPING_INCOMPLETE";
        return out;
    }
    // CUE text and durations can be identical for different physical files.
    // Bind the exact host selection to the displayed read-only snapshot.
    if (request.physical_source_identity_by_selection.size() != count ||
        displayed.physical_source_identity_by_selection.size() != count ||
        displayed.physical_source_identity_by_selection !=
            request.physical_source_identity_by_selection) {
        out.diagnostic = "STUDIO_PHYSICAL_SOURCE_SNAPSHOT_CHANGED";
        return out;
    }
    std::set<std::string> seen_physical_sources;
    for (std::size_t index = 0; index < count; ++index) {
        const auto& source = sources[index];
        if (source.subsong != 0 || !source.host_readonly_physical_probe_passed) {
            out.diagnostic = "STUDIO_PHYSICAL_VIRTUAL_ISOLATION_UNQUALIFIED";
            return out;
        }
        if (source.physical_source_identity.empty() ||
            source.physical_source_identity.size() > 4096 ||
            !precision_cue_safe_unicode_text(source.physical_source_identity) ||
            source.physical_source_identity !=
                request.physical_source_identity_by_selection[index] ||
            !seen_physical_sources.insert(source.physical_source_identity).second) {
            out.diagnostic = "STUDIO_PHYSICAL_SOURCE_IDENTITY_CHANGED_OR_DUPLICATE";
            return out;
        }
    }
    const auto matched = match_precision_cue_releases(
        request.reference, request.candidates);
    if (matched.state != PrecisionCueMatchState::Selected ||
        !matched.selected_candidate_index ||
        *matched.selected_candidate_index >= request.candidates.size()) {
        out.diagnostic = "STUDIO_RELEASE_EDITION_NOT_UNIQUE";
        return out;
    }
    const auto& edition = request.candidates[*matched.selected_candidate_index];
    // Deezer whole-second data may corroborate identity but may not become
    // the frame-accurate primary CUE clock.
    if (edition.provider != "apple" || edition.provider_release_id.empty()) {
        out.diagnostic = "STUDIO_HIGH_RESOLUTION_EDITION_REQUIRED";
        return out;
    }
    // Deezer exposes whole-second album track durations. Do not silently
    // promote them to Apple millisecond timing or imply authenticated HTTP.
    // A malformed Deezer catalog claim must not reach the Studio review.
    const bool deezer_reference =
        precision_cue_consensus_detail::normalized(request.reference.provider) == "deezer";
    PrecisionCueConsensusReport catalog;
    if (deezer_reference) {
        if (request.reference.provider_release_id.empty()) {
            out.diagnostic = "STUDIO_DEEZER_RELEASE_ID_REQUIRED";
            return out;
        }
        catalog = audit_precision_cue_source_consensus({
            {edition, 1}, {request.reference, 1000}});
        if (catalog.state == PrecisionCueConsensusState::Rejected ||
            catalog.differences.size() != edition.tracks.size()) {
            out.diagnostic = "STUDIO_DEEZER_CATALOG_EVIDENCE_UNQUALIFIED";
            return out;
        }
        out.catalog_consensus_state = catalog.state;
        out.first_coarse_conflict = catalog.first_coarse_conflict;
        out.catalog_diagnostic =
            catalog.state == PrecisionCueConsensusState::Review
                ? "STUDIO_DEEZER_CUMULATIVE_DURATION_REVIEW"
                : "STUDIO_DEEZER_SECONDS_IDENTITY_ONLY_NOT_FRAME_PROOF";
    } else {
        out.catalog_diagnostic = "STUDIO_NO_QUALIFIED_DEEZER_CORROBORATION";
    }
    try {
        const auto fresh = make_precision_cue_release_preview(request);
        if (fresh.discs.empty() || fresh.discs.size() != count ||
            displayed.discs.size() != count ||
            fresh.state != displayed.state ||
            fresh.preview_qualified != displayed.preview_qualified ||
            // A CUE text/filename match cannot prove that the user kept the
            // same physical selection when equal-length discs are reordered.
            fresh.physical_index_by_disc != displayed.physical_index_by_disc ||
            fresh.physical_index_by_disc != request.reviewed_physical_index_by_disc ||
            fresh.provider != displayed.provider ||
            fresh.provider_release_id != displayed.provider_release_id ||
            fresh.provider != edition.provider ||
            fresh.provider_release_id != edition.provider_release_id) {
            out.diagnostic = "STUDIO_PREVIEW_CHANGED_SINCE_REVIEW";
            return out;
        }
        std::vector<bool> used(count, false);
        std::size_t release_offset = 0;
        bool reference_needs_review = false;
        for (std::size_t disc = 0; disc < count; ++disc) {
            const auto source_index = request.reviewed_physical_index_by_disc[disc];
            if (source_index >= count || used[source_index]) {
                out.diagnostic = "STUDIO_DISC_MAPPING_NOT_A_PERMUTATION";
                out.rows.clear();
                return out;
            }
            used[source_index] = true;
            const auto& planned = displayed.discs[disc];
            const auto& canonical = fresh.discs[disc];
            if (planned.external_cue_utf8 != canonical.external_cue_utf8 ||
                planned.total_duration_ms != canonical.total_duration_ms ||
                planned.boundaries.size() != canonical.boundaries.size() ||
                !plan_precision_cue_external_export(
                    planned, request.physical_discs[source_index].mp3_basename).eligible) {
                out.diagnostic = "STUDIO_CUE_PLAN_CHANGED_SINCE_REVIEW";
                out.rows.clear();
                return out;
            }
            PrecisionCueStudioReferenceState reference_state =
                PrecisionCueStudioReferenceState::NotProvided;
            std::vector<PrecisionCueBoundaryDifference> differences;
            if (reference_cues[source_index]) {
                const auto diff = compare_precision_cue_external_reference(
                    planned, *reference_cues[source_index],
                    request.physical_discs[source_index].mp3_basename);
                switch (diff.state) {
                case PrecisionCueReferenceDiffState::ExactFramesPreviewOnly:
                    reference_state =
                        PrecisionCueStudioReferenceState::ExactFrameNotAudioProof;
                    break;
                case PrecisionCueReferenceDiffState::WithinOneFramePreviewOnly:
                    reference_state =
                        PrecisionCueStudioReferenceState::WithinOneFrameNotAudioProof;
                    break;
                case PrecisionCueReferenceDiffState::ReviewRequired:
                    reference_state = PrecisionCueStudioReferenceState::ReviewRequired;
                    reference_needs_review = true;
                    break;
                case PrecisionCueReferenceDiffState::Unqualified:
                    reference_state = PrecisionCueStudioReferenceState::Unqualified;
                    reference_needs_review = true;
                    break;
                }
                differences = diff.audit.differences;
            }
            std::int64_t elapsed = 0;
            for (std::size_t track = 0; track < planned.boundaries.size(); ++track) {
                if (release_offset >= edition.tracks.size()) {
                    out.diagnostic = "STUDIO_TRACK_COUNT_CHANGED";
                    out.rows.clear();
                    return out;
                }
                const auto release_index = release_offset++;
                const auto& item = edition.tracks[release_index];
                const auto& boundary = planned.boundaries[track];
                const auto& expected = canonical.boundaries[track];
                if (item.disc_number != disc + 1 ||
                    item.track_number != track + 1 || item.duration_ms <= 0 ||
                    boundary.track_number != item.track_number ||
                    boundary.elapsed_ms != elapsed ||
                    boundary.cue_frame != milliseconds_to_cue_frame(elapsed) ||
                    boundary.elapsed_ms != expected.elapsed_ms ||
                    boundary.cue_frame != expected.cue_frame) {
                    out.diagnostic = "STUDIO_TRACK_TIMELINE_CHANGED";
                    out.rows.clear();
                    return out;
                }
                PrecisionCueStudioReviewRow row;
                row.disc_number = item.disc_number;
                row.track_number = item.track_number;
                row.physical_selection_index = source_index;
                row.physical_basename =
                    request.physical_discs[source_index].mp3_basename;
                row.title = item.title;
                row.performer = item.performer;
                row.provider = edition.provider;
                row.provider_release_id = edition.provider_release_id;
                if (deezer_reference) {
                    const auto& evidence = catalog.differences[release_index];
                    if (evidence.disc_number != item.disc_number ||
                        evidence.track_number != item.track_number ||
                        evidence.provider != request.reference.provider ||
                        evidence.resolution_ms != 1000) {
                        out.diagnostic = "STUDIO_DEEZER_TRACK_EVIDENCE_CHANGED";
                        out.rows.clear();
                        return out;
                    }
                    row.corroborating_provider = evidence.provider;
                    row.corroborating_release_id =
                        request.reference.provider_release_id;
                    row.corroborating_resolution_ms = evidence.resolution_ms;
                    row.signed_corroborating_track_delta_ms =
                        evidence.signed_delta_ms;
                    row.signed_corroborating_cumulative_delta_ms =
                        evidence.signed_cumulative_delta_ms;
                }
                row.elapsed_ms = boundary.elapsed_ms;
                row.cue_frame = boundary.cue_frame;
                row.reference_state = reference_state;
                if (differences.size() == planned.boundaries.size())
                    row.signed_reference_delta_frames =
                        differences[track].signed_delta_frames;
                out.rows.push_back(std::move(row));
                elapsed += item.duration_ms;
            }
            if (elapsed != planned.total_duration_ms) {
                out.diagnostic = "STUDIO_DISC_DURATION_CHANGED";
                out.rows.clear();
                return out;
            }
        }
        if (release_offset != edition.tracks.size()) {
            out.diagnostic = "STUDIO_RELEASE_TRACK_COUNT_CHANGED";
            out.rows.clear();
            return out;
        }
        // Multi-disc assignment and physical/edition mastering identity
        // remain an explicit Studio review, even with exact CUE frames.
        if (displayed.state != PrecisionCueMatchState::Selected ||
            !displayed.preview_qualified || reference_needs_review ||
            !deezer_reference ||
            out.catalog_consensus_state == PrecisionCueConsensusState::Review) {
            out.state = PrecisionCueStudioHandoffState::RowsRequireReview;
            out.diagnostic = "STUDIO_ROWS_PREVIEW_ONLY_REVIEW_REQUIRED";
        } else {
            out.state = PrecisionCueStudioHandoffState::ReadOnlyRows;
            out.diagnostic = "STUDIO_ROWS_PREVIEW_ONLY_NO_WRITE_TOKEN";
        }
    } catch (const std::exception&) {
        out.state = PrecisionCueStudioHandoffState::Blocked;
        out.diagnostic = "STUDIO_HANDOFF_RECOMPUTE_FAILED";
        out.rows.clear();
    }
    return out;
}

} // namespace djmeta
