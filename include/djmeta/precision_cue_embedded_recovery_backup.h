#pragma once
// PRE-COMMIT SAFETY FOUNDATION, NOT A PRODUCTION EMBEDDED WRITER.
// Explicitly opt-in to creating a NEW verified byte-for-byte physical backup.
// Never modifies/renames/overwrites the source audio file or any existing
// backup. No recovery/publish-to-source operation exists in this header.
// Staged incomplete clones are removed on ordinary failures; a process crash
// can leave a "pending" orphan which MUST NOT be assumed to be a valid backup.
#include "djmeta/precision_cue_embedded_stage_dryrun.h"
#include "djmeta/precision_cue_external_create.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace djmeta {

enum class PrecisionCueRecoveryBackupStatus {
    Rejected,
    SourceStale,
    VerifiedBackupCreated,
    DestinationCollision,
    IoError,
    PublishedNeedsReview
};
struct PrecisionCueRecoveryBackupResult {
    PrecisionCueRecoveryBackupStatus status =
        PrecisionCueRecoveryBackupStatus::Rejected;
    std::string diagnostic = "EMBEDDED_BACKUP_NOT_REQUESTED";
    std::filesystem::path verified_backup_path;
    std::array<std::uint8_t,32> verified_sha256{};
    std::uintmax_t bytes = 0;
};
enum class PrecisionCueRecoveryBackupCheckpoint {
    AfterPendingClone,
    AfterPendingVerified,
    AfterBackupPublished
};
namespace precision_cue_recovery_backup_detail {
inline bool is_qualified_parent(const std::filesystem::path& source) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!source.is_absolute() || source != source.lexically_normal())
        return false;
    const auto parent=source.parent_path();
    return !parent.empty() && parent.is_absolute() &&
        fs::canonical(parent,ec)==parent && !ec;
}
inline std::optional<PrecisionCuePhysicalFullSnapshot>
verify_byte_identical_copy(
    const std::filesystem::path& clone,
    const PrecisionCuePhysicalFullSnapshot& original,
    std::string_view reviewed_metadata_prefix) {
    const auto copied=capture_precision_cue_physical_full_snapshot(
        clone,reviewed_metadata_prefix);
    if (!copied || copied->physical.bytes!=original.physical.bytes ||
        copied->full_file_sha256!=original.full_file_sha256 ||
        copied->reviewed_prefix_sha256!=original.reviewed_prefix_sha256)
        return std::nullopt;
    return copied;
}
inline std::filesystem::path backup_candidate_from_pending(
    const std::filesystem::path& pending) {
    const auto filename=pending.filename().string();
    constexpr std::string_view lead=".precision-cue-backup-pending-";
    constexpr std::string_view end=".tmp";
    if (!std::string_view(filename).starts_with(lead) ||
        !std::string_view(filename).ends_with(end))
        return {};
    const auto id=filename.substr(lead.size(),
                                  filename.size()-lead.size()-end.size());
    if (id.size()!=32) return {};
    for (unsigned char c:id)
        if (!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return {};
    return pending.parent_path() /
        (".precision-cue-backup-verified-"+id+".bak");
}
} // namespace precision_cue_recovery_backup_detail

// Never delete a previously published backup in any status branch.
// If power fails after publish, the verified backup can be found by its name,
// but may NOT be restored automatically or without a new explicit review.
// The fault callback is only for tests; never use it for production actions.
inline PrecisionCueRecoveryBackupResult prepare_precision_cue_recovery_backup(
    const PrecisionCuePhysicalFullSnapshot& original,
    std::string_view reviewed_metadata_prefix,
    bool explicitly_request_new_backup = false,
    const std::function<void(PrecisionCueRecoveryBackupCheckpoint,
                             const std::filesystem::path&)>& test_fault = {}) {
    using Status=PrecisionCueRecoveryBackupStatus;
    using namespace precision_cue_recovery_backup_detail;
    if (!explicitly_request_new_backup)
        return {Status::Rejected,"EMBEDDED_BACKUP_REQUIRES_EXPLICIT_OPT_IN"};
    if (!is_qualified_parent(original.canonical_path) ||
        !revalidate_precision_cue_physical_full_snapshot(
            original,reviewed_metadata_prefix))
        return {Status::SourceStale,"EMBEDDED_BACKUP_SOURCE_SNAPSHOT_STALE"};
    try {
        const auto parent=original.canonical_path.parent_path();
        const auto id=precision_cue_embedded_stage_detail::random_suffix();
        precision_cue_embedded_stage_detail::ScopedTemporaryFile pending(
            parent/(".precision-cue-backup-pending-"+id+".tmp"));
        const auto target=backup_candidate_from_pending(pending.name);
        if (target.empty() || target.parent_path()!=parent)
            return {Status::Rejected,"EMBEDDED_BACKUP_TARGET_INVALID"};
        if (!precision_cue_embedded_stage_detail::clone_to_owned_new_stage(
                original.canonical_path,pending,original.physical.bytes))
            return {Status::IoError,"EMBEDDED_BACKUP_EXCLUSIVE_CLONE_FAILED"};
        if (test_fault)
            test_fault(PrecisionCueRecoveryBackupCheckpoint::AfterPendingClone,
                       pending.name);
        if (!verify_byte_identical_copy(
                pending.name,original,reviewed_metadata_prefix))
            return {Status::IoError,"EMBEDDED_BACKUP_COPY_HASH_MISMATCH"};
        if (!revalidate_precision_cue_physical_full_snapshot(
                original,reviewed_metadata_prefix))
            return {Status::SourceStale,"EMBEDDED_BACKUP_SOURCE_CHANGED_DURING_COPY"};
        if (test_fault)
            test_fault(PrecisionCueRecoveryBackupCheckpoint::AfterPendingVerified,
                       pending.name);
        // The second hash after fault injection cannot be skipped.
        if (!verify_byte_identical_copy(
                pending.name,original,reviewed_metadata_prefix))
            return {Status::IoError,"EMBEDDED_BACKUP_POSTCHECK_HASH_MISMATCH"};
        if (!revalidate_precision_cue_physical_full_snapshot(
                original,reviewed_metadata_prefix))
            return {Status::SourceStale,"EMBEDDED_BACKUP_SOURCE_CHANGED_BEFORE_PUBLISH"};
        // MoveFileExW without REPLACE_EXISTING / POSIX hard link: collision
        // fails without replacing an existing backup file or symlink.
        if (!precision_cue_external_detail::publish_without_replace(
                pending.name,target)) {
            std::error_code ec;
            const auto state=std::filesystem::symlink_status(target,ec);
            if (!ec && state.type()!=std::filesystem::file_type::not_found)
                return {Status::DestinationCollision,
                        "EMBEDDED_BACKUP_DESTINATION_ALREADY_EXISTS"};
            return {Status::IoError,"EMBEDDED_BACKUP_PUBLISH_FAILED"};
        }
        // On Windows the pending entry has been renamed; on POSIX the
        // pending hard link is removed by its owned RAII guard.
        PrecisionCueRecoveryBackupResult result;
        result.verified_backup_path=target;
        result.bytes=original.physical.bytes;
        result.verified_sha256=original.full_file_sha256;
        if (test_fault)
            test_fault(PrecisionCueRecoveryBackupCheckpoint::AfterBackupPublished,
                       target);
        if (!verify_byte_identical_copy(target,original,reviewed_metadata_prefix) ||
            !revalidate_precision_cue_physical_full_snapshot(
                original,reviewed_metadata_prefix)) {
            result.status=Status::PublishedNeedsReview;
            result.diagnostic="EMBEDDED_BACKUP_PUBLISHED_BUT_NOT_QUALIFIED";
            return result; // Preserve published file for explicit recovery.
        }
        result.status=Status::VerifiedBackupCreated;
        result.diagnostic="EMBEDDED_VERIFIED_BACKUP_CREATED_SOURCE_UNCHANGED";
        return result;
    } catch (const std::exception&) {
        return {Status::IoError,
                "EMBEDDED_BACKUP_FAILED_RETAIN_EXISTING_PUBLISHED_COPIES"};
    }
}

// Read-only evidence check, no automatic rollback or destructive cleanup.
// Caller must supply expected snapshot; any unfamiliar file is NOT trusted.
inline bool precision_cue_recovery_backup_matches_review(
    const std::filesystem::path& backup,
    const PrecisionCuePhysicalFullSnapshot& reviewed_source,
    std::string_view reviewed_prefix) {
    namespace fs=std::filesystem;
    std::error_code ec;
    const auto name=backup.filename().string();
    constexpr std::string_view prefix=".precision-cue-backup-verified-";
    if (!std::string_view(name).starts_with(prefix) ||
        !std::string_view(name).ends_with(".bak") ||
        !fs::is_regular_file(backup,ec) || ec ||
        fs::is_symlink(backup,ec) || ec ||
        backup.parent_path()!=reviewed_source.canonical_path.parent_path())
        return false;
    return precision_cue_recovery_backup_detail::verify_byte_identical_copy(
        backup,reviewed_source,reviewed_prefix).has_value();
}
} // namespace djmeta
