#pragma once
// Multi-disc external CUE create-only transaction boundary. This is
// deliberately NOT represented as atomic: distinct files can be published
// before an unforeseen later I/O failure. Never auto-delete published CUEs.
#include "djmeta/precision_cue_external_host_review.h"
#include "djmeta/precision_cue_source_consensus.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace djmeta {

enum class PrecisionCueExternalBatchState {
    ReadyForConfirmation,
    RejectedBeforeCreate,
    CreatedAll,
    PartialNeedsReview,
    PublishedNeedsReview
};

struct PrecisionCueExternalBatchResult {
    PrecisionCueExternalBatchState state =
        PrecisionCueExternalBatchState::RejectedBeforeCreate;
    std::size_t created_count = 0;
    std::size_t failed_disc_index = 0; // Zero-based; inspect only on failure.
    std::vector<std::filesystem::path> target_paths;
    std::string diagnostic;
};

namespace precision_cue_batch_detail {
inline std::string album_header(std::string_view cue) {
    // Canonical generated layout already enforces these exact header
    // directives. Their bytes must agree across the whole reviewed album.
    // Comparing just disc counts or file paths could accidentally mix two
    // different album releases in a single batch.
    std::string out;
    std::size_t pos = 0;
    while (pos < cue.size()) {
        const auto end = cue.find('\n', pos);
        if (end == std::string_view::npos) return {};
        const auto line = cue.substr(pos, end - pos);
        if (line.starts_with("FILE ")) break;
        if (line.starts_with("REM GENRE ") ||
            line.starts_with("REM DATE ") ||
            line.starts_with("PERFORMER ") ||
            line.starts_with("TITLE ")) {
            out.append(line);
            out.push_back('\n');
        }
        pos = end + 1;
    }
    return out;
}

inline bool same_target(const std::filesystem::path& a,
                        const std::filesystem::path& b) {
#ifdef _WIN32
    const std::wstring lhs = a.wstring(), rhs = b.wstring();
    const auto compare = CompareStringOrdinal(lhs.c_str(),
        static_cast<int>(lhs.size()), rhs.c_str(),
        static_cast<int>(rhs.size()), TRUE);
    // A failed comparison is conservatively a collision, never permission.
    return compare == 0 || compare == CSTR_EQUAL;
#else
    // Canonical directories with exact sibling bytes; filesystem case
    // aliases remain protected by exclusive atomic publication.
    return a == b;
#endif
}
} // namespace precision_cue_batch_detail

// Single shared fail-closed review over the COMPLETE numbered disc set.
// This does not grant write permission, does not create files, and requires
// fresh physical source content SHA + absent target evidence for every disc.
inline PrecisionCueExternalBatchResult
preflight_precision_cue_external_batch(
    const std::vector<PrecisionCueReviewedExternalCreate>& reviews,
    const PrecisionCueRelease& selected_edition) {
    using State = PrecisionCueExternalBatchState;
    PrecisionCueExternalBatchResult out;
    if (reviews.empty() || reviews.size() > 8) {
        out.diagnostic = "CUE_BATCH_DISC_COUNT_INVALID";
        return out;
    }
    // The full selected high-resolution Apple edition, not just a common
    // album TITLE/PERFORMER, binds all discs and all TRACK metadata/durations.
    // The native window owns this release; no network or writes are involved.
    if (selected_edition.provider != "apple" ||
        !precision_cue_consensus_detail::valid_release_structure(
            selected_edition) ||
        selected_edition.tracks.back().disc_number != reviews.size()) {
        out.diagnostic = "CUE_BATCH_SELECTED_EDITION_INVALID";
        return out;
    }
    std::string shared_album_header;
    for (std::size_t index = 0; index < reviews.size(); ++index) {
        out.failed_disc_index = index;
        const auto& review = reviews[index];
        const auto& request = review.request;
        // Plans must represent exactly one and the same reviewed album
        // per batch, with consecutive disc indices and shared disc count.
        const auto& plan = request.plan;
        const auto proposed = plan_precision_cue_external_export(
            plan, request.mp3_basename);
        if (!proposed.eligible ||
            request.reviewed_preflight.target_exists ||
            request.reviewed_preflight.overwrite_approved ||
            !request.reviewed_preflight.preview_available ||
            !request.reviewed_preflight.preview_qualified ||
            !request.reviewed_preflight.explicit_review_resolved ||
            !request.reviewed_preflight.release_identity_confirmed ||
            !request.reviewed_preflight.disc_identity_confirmed ||
            request.reviewed_cue_sha256 ==
                std::array<std::uint8_t, 32>{} ||
            request.reviewed_cue_sha256 !=
                precision_cue_sha256(plan.external_cue_utf8)) {
            out.diagnostic = "CUE_BATCH_REVIEW_INVALID";
            return out;
        }
        const auto actual_album_header =
            precision_cue_batch_detail::album_header(plan.external_cue_utf8);
        if (actual_album_header.empty() ||
            (index != 0 && actual_album_header != shared_album_header)) {
            out.diagnostic = "CUE_BATCH_ALBUM_EDITION_CONFLICT";
            return out;
        }
        if (index == 0) shared_album_header = actual_album_header;
        // The validated generator records consecutive disc numbers in its
        // canonical REM fields; not merely a vector index or filename.
        const std::string disc_line = "REM DISCNUMBER " +
            std::to_string(index + 1) + "\n";
        const std::string count_line = "REM TOTALDISCS " +
            std::to_string(reviews.size()) + "\n";
        if (plan.external_cue_utf8.find(disc_line) ==
                std::string::npos ||
            plan.external_cue_utf8.find(count_line) ==
                std::string::npos) {
            out.diagnostic = "CUE_BATCH_DISC_IDENTITY_INVALID";
            return out;
        }
        // Reconstruct the EXACT canonical CUE for each disc using one
        // qualified edition. Prevent interleaving tracks from different
        // releases even when album titles, total time and disc counts agree.
        PrecisionCueDisc canonical_disc;
        canonical_disc.album_title = selected_edition.album_title;
        canonical_disc.album_performer = selected_edition.album_performer;
        canonical_disc.genre = selected_edition.genre;
        canonical_disc.release_date = selected_edition.release_date;
        canonical_disc.audio_file_basename = request.mp3_basename;
        canonical_disc.disc_number = static_cast<unsigned>(index + 1);
        canonical_disc.total_discs = static_cast<unsigned>(reviews.size());
        for (const auto& track : selected_edition.tracks) {
            if (track.disc_number == index + 1)
                canonical_disc.tracks.push_back({
                    track.title, track.performer, track.duration_ms
                });
        }
        if (canonical_disc.tracks.empty()) {
            out.diagnostic = "CUE_BATCH_SELECTED_DISC_MISSING";
            return out;
        }
        try {
            const auto expected = make_precision_cue(canonical_disc);
            if (plan.external_cue_utf8 != expected.external_cue_utf8 ||
                plan.total_duration_ms != expected.total_duration_ms ||
                plan.boundaries.size() != expected.boundaries.size()) {
                out.diagnostic = "CUE_BATCH_EDITION_TRACKLIST_CONFLICT";
                return out;
            }
            for (std::size_t pos = 0; pos < plan.boundaries.size(); ++pos) {
                if (plan.boundaries[pos].track_number !=
                        expected.boundaries[pos].track_number ||
                    plan.boundaries[pos].elapsed_ms !=
                        expected.boundaries[pos].elapsed_ms ||
                    plan.boundaries[pos].cue_frame !=
                        expected.boundaries[pos].cue_frame) {
                    out.diagnostic = "CUE_BATCH_EDITION_TRACKLIST_CONFLICT";
                    return out;
                }
            }
        } catch (const std::exception&) {
            out.diagnostic = "CUE_BATCH_EDITION_TRACKLIST_CONFLICT";
            return out;
        }
        const auto destination = request.qualified_sibling_directory /
            precision_cue_external_detail::utf8_path(
                proposed.target_cue_basename);
        if (!destination.is_absolute() ||
            destination != destination.lexically_normal() ||
            destination.parent_path() != request.qualified_sibling_directory ||
            !precision_cue_external_detail::physical_source_unchanged(
                review.reviewed_source,
                request.physical_mp3_absolute_path,
                request.qualified_sibling_directory, request.mp3_basename)) {
            out.diagnostic = "CUE_BATCH_PHYSICAL_SOURCE_STALE";
            return out;
        }
        // Distinct basenames are insufficient: two hardlinks can reference
        // the very same physical MP3. A multi-disc release must never
        // generate distinct disc CUEs for one underlying file identity.
        for (std::size_t previous = 0; previous < index; ++previous) {
            const auto& prior = reviews[previous].reviewed_source;
            if (prior.volume_id == review.reviewed_source.volume_id &&
                prior.file_id == review.reviewed_source.file_id) {
                out.diagnostic = "CUE_BATCH_DUPLICATE_PHYSICAL_SOURCE";
                return out;
            }
        }
        for (const auto& previous : out.target_paths) {
            if (precision_cue_batch_detail::same_target(
                    previous, destination)) {
                out.diagnostic = "CUE_BATCH_TARGET_COLLISION";
                return out;
            }
        }
        const auto fresh = observe_precision_cue_external_review(review);
        if (!fresh.preflight.source_snapshot_current ||
            !fresh.preflight.target_snapshot_current ||
            fresh.preflight.target_exists ||
            !fresh.preflight.cue_content_qualified ||
            fresh.source_snapshot_token == 0 ||
            fresh.target_snapshot_token == 0 ||
            fresh.source_snapshot_token !=
                request.reviewed_source_snapshot_token ||
            fresh.target_snapshot_token !=
                request.reviewed_target_snapshot_token) {
            out.diagnostic = "CUE_BATCH_SNAPSHOT_OR_TARGET_CHANGED";
            return out;
        }
        out.target_paths.push_back(destination);
    }
    out.state = State::ReadyForConfirmation;
    out.diagnostic = "CUE_BATCH_ALL_DISCS_FRESH_NO_WRITES";
    out.failed_disc_index = 0;
    return out;
}

// Called only AFTER the user has confirmed ALL listed full target paths in
// the native UI. The batch is revalidated again immediately before any write.
// Every file still executes the existing independent, atomic no-replace and
// postimage gate. Once any file is created, no implicit rollback is attempted.
inline PrecisionCueExternalBatchResult
create_confirmed_precision_cue_external_batch(
    std::vector<PrecisionCueReviewedExternalCreate> reviews,
    const PrecisionCueRelease& selected_edition,
    bool explicit_batch_confirmation) {
    using State = PrecisionCueExternalBatchState;
    PrecisionCueExternalBatchResult result;
    if (!explicit_batch_confirmation) {
        result.diagnostic = "CUE_BATCH_REQUIRES_EXPLICIT_CONFIRMATION";
        return result;
    }
    result = preflight_precision_cue_external_batch(reviews, selected_edition);
    if (result.state != State::ReadyForConfirmation)
        return result;
    for (std::size_t index = 0; index < reviews.size(); ++index) {
        auto& reviewed = reviews[index];
        // A single batch consent is materialized only on this local copy.
        reviewed.request.explicitly_approved_for_external_create = true;
        reviewed.request.reviewed_preflight.explicit_user_approval = true;
        const auto created = create_confirmed_precision_cue_external(
            std::move(reviewed));
        if (created.status != PrecisionCueExternalCreateStatus::Created) {
            result.failed_disc_index = index;
            result.diagnostic = "CUE_BATCH_DISC_" +
                std::to_string(index + 1) + "_" + created.diagnostic;
            // Never assume filesystem rollback after successful publication.
            result.state = created.status ==
                    PrecisionCueExternalCreateStatus::PublishedNeedsReview
                ? State::PublishedNeedsReview
                : result.created_count > 0 ? State::PartialNeedsReview
                                           : State::RejectedBeforeCreate;
            return result;
        }
        ++result.created_count;
    }
    result.state = State::CreatedAll;
    result.diagnostic = "CUE_BATCH_ALL_CREATED_AND_VERIFIED";
    return result;
}

} // namespace djmeta
