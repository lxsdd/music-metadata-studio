#pragma once
// PRE-COMMIT TRANSACTION CONTRACT ONLY. Does NOT open any physical media for
// writing and never claims a foobar host lock, atomic replace or recovery.
// Requires a SHA-verifiable copy of the current original and a validated
// staged candidate; independently recomputes the candidate's full SHA256
// while streaming untouched source audio bytes. No audio is buffered wholesale.
#include "djmeta/precision_cue_embedded_recovery_inventory.h"
#include "djmeta/precision_cue_embedded_stage_dryrun.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace djmeta {

enum class PrecisionCueEmbeddedCommitReviewState {
    Rejected,
    NeedsReview,
    EvidenceCoherentHostWriteNotAvailable
};
struct PrecisionCueEmbeddedCommitReview {
    PrecisionCueEmbeddedCommitReviewState state =
        PrecisionCueEmbeddedCommitReviewState::Rejected;
    std::string diagnostic = "EMBEDDED_COMMIT_NOT_IMPLEMENTED";
    std::array<std::uint8_t,32> expected_candidate_sha256{};
    std::uintmax_t expected_file_length=0;
};

namespace precision_cue_embedded_commit_review_detail {
inline std::optional<std::array<std::uint8_t,32>>
expected_postimage_full_sha256(
    const PrecisionCuePhysicalFullSnapshot& original,
    std::string_view original_prefix,std::string_view modified_prefix) {
    if (original_prefix.size()!=modified_prefix.size() ||
        original_prefix.empty() || original_prefix.size()>original.physical.bytes ||
        original_prefix.size()>4u*1024u*1024u+10u ||
        precision_cue_sha256(original_prefix)!=
            original.reviewed_prefix_sha256)
        return std::nullopt;
    if (!revalidate_precision_cue_physical_full_snapshot(original,original_prefix))
        return std::nullopt;
    std::ifstream source(original.canonical_path,std::ios::binary);
    if (!source) return std::nullopt;
    PrecisionCueSha256Stream sha;
    sha.update(modified_prefix);
    source.seekg(static_cast<std::streamoff>(original_prefix.size()));
    if (!source) return std::nullopt;
    std::array<char,64*1024> block{};
    std::uintmax_t copied=original_prefix.size();
    while (copied<original.physical.bytes) {
        const auto amount=static_cast<std::size_t>((std::min)(
            std::uintmax_t(block.size()),original.physical.bytes-copied));
        source.read(block.data(),static_cast<std::streamsize>(amount));
        if (source.gcount()!=static_cast<std::streamsize>(amount))
            return std::nullopt;
        sha.update(std::string_view(block.data(),amount));
        copied+=amount;
    }
    char unexpected=0;
    if (source.get(unexpected)) return std::nullopt;
    source.close();
    if (!revalidate_precision_cue_physical_full_snapshot(original,original_prefix))
        return std::nullopt;
    return sha.finalize();
}
inline bool exact_backup_own_inode(
    const std::filesystem::path& backup,
    const PrecisionCuePhysicalFullSnapshot& reviewed,
    std::string_view prefix) {
    if (!precision_cue_recovery_backup_matches_review(backup,reviewed,prefix))
        return false;
    const auto backup_info=capture_precision_cue_physical_full_snapshot(
        backup,prefix);
    return backup_info &&
        !(backup_info->physical.file_id==reviewed.physical.file_id &&
          backup_info->physical.volume_id==reviewed.physical.volume_id);
}
} // namespace precision_cue_embedded_commit_review_detail

inline PrecisionCueEmbeddedCommitReview review_precision_cue_embedded_commit(
    const PrecisionCuePhysicalFullSnapshot& original,
    std::string_view reviewed_prefix,
    const PrecisionCueEmbeddedReviewedPatch& patch,
    const PrecisionCueEmbeddedStageResult& previously_staged,
    const std::filesystem::path& verified_backup_path,
    bool explicit_user_reconfirmation = false) {
    using State=PrecisionCueEmbeddedCommitReviewState;
    PrecisionCueEmbeddedCommitReview result;
    const auto reject=[&](State state,const char* reason) {
        result.state=state;
        result.diagnostic=reason;
        return result;
    };
    if (!explicit_user_reconfirmation)
        return reject(State::Rejected,
                      "EMBEDDED_EXPLICIT_USER_RECONFIRMATION_REQUIRED");
    if (previously_staged.status!=
        PrecisionCueEmbeddedStageStatus::VerifiedAndCleaned)
        return reject(State::NeedsReview,"EMBEDDED_CANDIDATE_NOT_VERIFIED");
    if (verified_backup_path.empty() ||
        verified_backup_path.parent_path()!=original.canonical_path.parent_path())
        return reject(State::NeedsReview,"EMBEDDED_BACKUP_PATH_NOT_QUALIFIED");
    if (!revalidate_precision_cue_physical_full_snapshot(original,reviewed_prefix))
        return reject(State::NeedsReview,"EMBEDDED_SOURCE_STALE_AT_COMMIT_REVIEW");
    if (!precision_cue_embedded_commit_review_detail::exact_backup_own_inode(
            verified_backup_path,original,reviewed_prefix))
        return reject(State::NeedsReview,"EMBEDDED_BACKUP_SHA_OR_ID_UNQUALIFIED");

    std::string_view proposed;
    bool ready=false;
    std::array<std::uint8_t,32> before{},after{};
    std::visit([&](const auto& item) {
        using P=std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<P,PrecisionCueId3Patch>) {
            ready=item.state==PrecisionCueId3PatchState::PreviewReady &&
                original.canonical_path.extension()==".mp3" &&
                reviewed_prefix.starts_with("ID3");
            proposed=item.postimage_tag;
        } else {
            ready=item.state==PrecisionCueFlacPatchState::PreviewReady &&
                original.canonical_path.extension()==".flac" &&
                reviewed_prefix.starts_with("fLaC");
            proposed=item.postimage_metadata;
        }
        before=item.source_sha256;
        after=item.postimage_sha256;
    },patch);
    if (!ready || proposed.size()!=reviewed_prefix.size() ||
        before!=original.reviewed_prefix_sha256 ||
        after!=precision_cue_sha256(proposed))
        return reject(State::NeedsReview,
                      "EMBEDDED_PATCH_HASH_OR_CARRIER_MISMATCH");
    const auto expected=precision_cue_embedded_commit_review_detail::
        expected_postimage_full_sha256(original,reviewed_prefix,proposed);
    if (!expected ||
        previously_staged.total_bytes!=original.physical.bytes ||
        previously_staged.staged_full_sha256!=*expected)
        return reject(State::NeedsReview,
                      "EMBEDDED_STAGED_CANDIDATE_FULL_SHA_MISMATCH");
    result.expected_candidate_sha256=*expected;
    result.expected_file_length=original.physical.bytes;
    // DO NOT return "ready to write". Host file handle/lock, durable backup,
    // crash-safe atomic publication and rollback are NOT implemented here.
    result.state=State::EvidenceCoherentHostWriteNotAvailable;
    result.diagnostic="EMBEDDED_SHA_EVIDENCE_COHERENT_HOST_WRITE_DISABLED";
    return result;
}
} // namespace djmeta
