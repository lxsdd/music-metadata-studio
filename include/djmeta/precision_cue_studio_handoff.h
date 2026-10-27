#pragma once
// Pure, source-only handoff into the canonical Music Metadata Studio review.
// No second window, no foobar handles, no writer, no filesystem or HTTP.
// All rows are advisory: even exact 75-fps agreement is NOT audio proof.
#include "djmeta/precision_cue_assemble.h"
#include "djmeta/precision_cue_reference_diff.h"
#include "djmeta/precision_cue_source_consensus.h"
#include "djmeta/precision_cue_optional_provider_review.h"
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

enum class PrecisionCueStudioOptionalStartState {
    CoarseCatalogOnly,
    ExactFrameAdvisoryNotAudioProof,
    WithinOneFrameReview,
    StartBoundaryDisagreementReview
};

// One selected physical-track row carries each optional catalog source's
// independent, offline-only timing evidence. A millisecond field unit is
// not an authenticated provider request or a frame-accuracy guarantee.
struct PrecisionCueStudioOptionalTrackEvidence {
    std::string provider;
    std::string provider_release_id;
    PrecisionCueOptionalFormat format = PrecisionCueOptionalFormat::SpotifyWebMilliseconds;
    unsigned reported_field_unit_ms = 0;
    unsigned conservative_comparison_resolution_ms = 0;
    std::int64_t signed_track_duration_delta_ms = 0;
    std::int64_t signed_start_cumulative_delta_ms = 0;
    std::int64_t signed_end_cumulative_delta_ms = 0;
    std::int64_t signed_end_frame_delta = 0;
    // The actual CUE INDEX 01 marker. Kept separate from track-end drift.
    // A coarse source still cannot establish frame-accurate timing.
    std::int64_t signed_start_frame_delta = 0;
    // A per-track REVIEW classification for the native read-only detail UI.
    // Coarse catalog durations never become 75-fps evidence even at delta 0.
    PrecisionCueStudioOptionalStartState start_state =
        PrecisionCueStudioOptionalStartState::CoarseCatalogOnly;
    bool disc_tail_duration_disagreement = false;
    // Distinct millisecond durations can quantize to identical start/end
    // frames. Only a high-resolution catalog field can carry this warning.
    bool same_frame_duration_disagreement = false;
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
    // Difference at the actual CUE INDEX 01 track start, not its end.
    // Whole-second Deezer corroboration is never a frame-accurate clock.
    std::optional<std::int64_t> signed_corroborating_start_delta_ms;
    std::int64_t elapsed_ms = 0;
    std::int64_t cue_frame = 0;
    // Quantization error is a signed integer microsecond audit only, not
    // an independent audio clock or a sample-perfect timing assertion.
    std::int64_t signed_quantization_offset_us = 0;
    // A reference INDEX 00/PREGAP/POSTGAP affects the whole disc's
    // structural review even when every INDEX 01 frame is identical.
    bool reference_has_extra_index_or_pregap = false;
    // Track-specific INDEX 00 / PREGAP / POSTGAP audit for the one native
    // Studio review. Absent for no reference or an unqualified reference.
    std::optional<PrecisionCueReferenceTrackStructure> reference_track_structure;
    PrecisionCueStudioReferenceState reference_state =
        PrecisionCueStudioReferenceState::NotProvided;
    std::optional<std::int64_t> signed_reference_delta_frames;
    std::vector<PrecisionCueStudioOptionalTrackEvidence> optional_track_evidence;
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
    // Explicit offline provider evidence only. Never a CUE clock or write token.
    std::vector<PrecisionCueOptionalAudit> optional_provider_evidence;
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
    const std::vector<std::optional<std::string>>& reference_cues,
    const std::vector<PrecisionCueOptionalSource>& optional_sources = {}) {
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
    // Optional providers are not allowed to override the selected exact
    // Apple-edition timing clock, physical identity, or CUE writer gate.
    // Even a matching Spotify millisecond field is REVIEW-only provenance.
    if (optional_sources.size() > 6) {
        out.diagnostic = "STUDIO_TOO_MANY_OPTIONAL_PROVIDERS";
        return out;
    }
    std::set<std::string> optional_provider_ids;
    for (const auto& optional : optional_sources) {
        const auto evidence = audit_precision_cue_optional_source(edition, optional);
        const auto provider = precision_cue_consensus_detail::normalized(
            optional.release.provider);
        if (evidence.state != PrecisionCueOptionalState::ReviewOnly ||
            !optional_provider_ids.insert(provider).second) {
            out.diagnostic = "STUDIO_OPTIONAL_PROVIDER_EVIDENCE_UNQUALIFIED";
            out.optional_provider_evidence.clear();
            return out;
        }
        out.optional_provider_evidence.push_back(evidence);
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
            std::vector<PrecisionCueReferenceTrackStructure> structures;
            bool reference_has_extra_index_or_pregap = false;
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
                structures = diff.track_structure;
                reference_has_extra_index_or_pregap = diff.extra_index_or_pregap;
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
                    row.signed_corroborating_start_delta_ms =
                        evidence.signed_start_cumulative_delta_ms;
                }
                row.elapsed_ms = boundary.elapsed_ms;
                row.cue_frame = boundary.cue_frame;
                row.signed_quantization_offset_us =
                    precision_cue_quantization_offset_microseconds(boundary);
                // Bind optional provider deltas to the actual physical row,
                // after the reviewed disc permutation and per-disc reset.
                // These values remain advisory; no provider may replace the
                // selected Apple timing clock or authorize a writer.
                for (std::size_t source = 0; source < optional_sources.size(); ++source) {
                    const auto& optional = optional_sources[source];
                    const auto& evidence = out.optional_provider_evidence[source];
                    if (evidence.comparison.differences.size() != edition.tracks.size()) {
                        out.diagnostic = "STUDIO_OPTIONAL_TRACK_EVIDENCE_INCOMPLETE";
                        out.rows.clear();
                        return out;
                    }
                    const auto& delta = evidence.comparison.differences[release_index];
                    if (delta.disc_number != item.disc_number ||
                        delta.track_number != item.track_number ||
                        delta.provider != optional.release.provider ||
                        delta.resolution_ms != evidence.comparison_resolution_ms) {
                        out.diagnostic = "STUDIO_OPTIONAL_TRACK_EVIDENCE_CHANGED";
                        out.rows.clear();
                        return out;
                    }
                    const auto start_state =
                        evidence.comparison_resolution_ms > 1000u / 75u
                            ? PrecisionCueStudioOptionalStartState::CoarseCatalogOnly
                            : delta.signed_start_frame_delta == 0
                                ? PrecisionCueStudioOptionalStartState::ExactFrameAdvisoryNotAudioProof
                                : (delta.signed_start_frame_delta == -1 ||
                                   delta.signed_start_frame_delta == 1)
                                    ? PrecisionCueStudioOptionalStartState::WithinOneFrameReview
                                    : PrecisionCueStudioOptionalStartState::StartBoundaryDisagreementReview;
                    row.optional_track_evidence.push_back({
                        optional.release.provider, optional.release.provider_release_id,
                        optional.format, evidence.field_unit_ms,
                        evidence.comparison_resolution_ms, delta.signed_delta_ms,
                        delta.signed_start_cumulative_delta_ms,
                        delta.signed_cumulative_delta_ms, delta.signed_end_frame_delta,
                        delta.signed_start_frame_delta, start_state,
                        delta.disc_terminal_track && delta.signed_delta_ms != 0,
                        evidence.comparison_resolution_ms <= 1000u / 75u &&
                            delta.signed_delta_ms != 0 &&
                            delta.signed_start_frame_delta == 0 &&
                            delta.signed_end_frame_delta == 0});
                }
                row.reference_has_extra_index_or_pregap =
                    reference_has_extra_index_or_pregap;
                row.reference_state = reference_state;
                if (reference_cues[source_index] &&
                    reference_state != PrecisionCueStudioReferenceState::Unqualified) {
                    if (structures.size() != planned.boundaries.size() ||
                        structures[track].track_number != boundary.track_number) {
                        out.diagnostic = "STUDIO_REFERENCE_TRACK_STRUCTURE_CHANGED";
                        out.rows.clear();
                        return out;
                    }
                    row.reference_track_structure = structures[track];
                }
                if (differences.size() == planned.boundaries.size()) {
                    const auto& difference = differences[track];
                    if (difference.track_number != boundary.track_number ||
                        difference.predicted_frame != boundary.cue_frame) {
                        out.diagnostic = "STUDIO_REFERENCE_BOUNDARY_CHANGED";
                        out.rows.clear();
                        return out;
                    }
                    row.signed_reference_delta_frames =
                        difference.signed_delta_frames;
                    // A one-frame discrepancy on track 2 must not label
                    // 29 exact tracks as one-frame mismatches. Conversely,
                    // structural gap/INDEX 00 evidence is disc-wide REVIEW,
                    // even if the individual INDEX 01 positions match.
                    if (!reference_has_extra_index_or_pregap &&
                        reference_state != PrecisionCueStudioReferenceState::Unqualified) {
                        const auto delta = difference.signed_delta_frames;
                        row.reference_state = delta == 0
                            ? PrecisionCueStudioReferenceState::ExactFrameNotAudioProof
                            : (delta == -1 || delta == 1)
                                ? PrecisionCueStudioReferenceState::WithinOneFrameNotAudioProof
                                : PrecisionCueStudioReferenceState::ReviewRequired;
                    }
                }
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
            !deezer_reference || !optional_sources.empty() ||
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
