#pragma once
// Read-only crash recovery inventory for Precision CUE pre-commit backups.
// Unfinished pending clones are UNTRUSTED and must never be restored/deleted
// automatically. Even a byte-verified backup is evidence for explicit review,
// NOT permission to replace source audio or bypass foobar host-file locks.
#include "djmeta/precision_cue_embedded_recovery_backup.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace djmeta {
enum class PrecisionCueRecoveryCandidateState {
    PendingUntrusted,
    VerifiedMatchesReviewedSource,
    VerifiedMismatchOrUnsafe
};
struct PrecisionCueRecoveryCandidate {
    std::filesystem::path path;
    PrecisionCueRecoveryCandidateState state =
        PrecisionCueRecoveryCandidateState::VerifiedMismatchOrUnsafe;
};
enum class PrecisionCueRecoveryInventoryStatus {
    Complete,
    SourceChangedOrMissing,
    TooManyCandidates,
    UnqualifiedDirectory
};
struct PrecisionCueRecoveryInventory {
    PrecisionCueRecoveryInventoryStatus status =
        PrecisionCueRecoveryInventoryStatus::UnqualifiedDirectory;
    std::vector<PrecisionCueRecoveryCandidate> candidates;
    std::size_t inspected = 0;
};

namespace precision_cue_recovery_inventory_detail {
inline bool match_generated_name(std::string_view filename,
                                 std::string_view marker,
                                 std::string_view suffix) {
    if (!filename.starts_with(marker) || !filename.ends_with(suffix) ||
        filename.size()!=marker.size()+32+suffix.size())
        return false;
    const auto id=filename.substr(marker.size(),32);
    for (unsigned char c:id)
        if (!((c>='0' && c<='9') || (c>='a' && c<='f')))
            return false;
    return true;
}
inline bool is_pending(std::string_view filename) {
    return match_generated_name(filename,
        ".precision-cue-backup-pending-",".tmp");
}
inline bool is_published(std::string_view filename) {
    return match_generated_name(filename,
        ".precision-cue-backup-verified-",".bak");
}
} // namespace precision_cue_recovery_inventory_detail

inline PrecisionCueRecoveryInventory inventory_precision_cue_recovery_backups(
    const PrecisionCuePhysicalFullSnapshot& originally_reviewed,
    std::string_view originally_reviewed_prefix,
    std::size_t max_candidates=32) {
    namespace fs=std::filesystem;
    using Status=PrecisionCueRecoveryInventoryStatus;
    using State=PrecisionCueRecoveryCandidateState;
    PrecisionCueRecoveryInventory result;
    const auto parent=originally_reviewed.canonical_path.parent_path();
    std::error_code ec;
    if (!parent.is_absolute() || !originally_reviewed.canonical_path.is_absolute() ||
        originally_reviewed.canonical_path!=
            originally_reviewed.canonical_path.lexically_normal() ||
        fs::canonical(parent,ec)!=parent || ec ||
        max_candidates==0 || max_candidates>128 ||
        originally_reviewed.reviewed_prefix_size!=originally_reviewed_prefix.size() ||
        precision_cue_sha256(originally_reviewed_prefix)!=
            originally_reviewed.reviewed_prefix_sha256)
        return result;

    const bool source_current=revalidate_precision_cue_physical_full_snapshot(
        originally_reviewed,originally_reviewed_prefix);
    result.status=source_current ?
        Status::Complete : Status::SourceChangedOrMissing;
    try {
        fs::directory_iterator iterator(parent,ec), end;
        if (ec) {
            result.status=Status::UnqualifiedDirectory;
            return result;
        }
        for (;iterator!=end;iterator.increment(ec)) {
            if (ec) {
                result.candidates.clear();
                result.status=Status::UnqualifiedDirectory;
                return result;
            }
            const auto filename=iterator->path().filename().string();
            const bool pending=precision_cue_recovery_inventory_detail::
                is_pending(filename);
            const bool published=precision_cue_recovery_inventory_detail::
                is_published(filename);
            if (!pending && !published) continue;
            ++result.inspected;
            if (result.inspected>max_candidates) {
                result.candidates.clear(); // No truncated "safe to use" list.
                result.status=Status::TooManyCandidates;
                return result;
            }
            PrecisionCueRecoveryCandidate candidate;
            candidate.path=iterator->path();
            if (pending) {
                candidate.state=State::PendingUntrusted;
            } else {
                const auto status=fs::symlink_status(candidate.path,ec);
                if (!ec && status.type()==fs::file_type::regular &&
                    precision_cue_recovery_backup_matches_review(
                        candidate.path,originally_reviewed,
                        originally_reviewed_prefix))
                    candidate.state=State::VerifiedMatchesReviewedSource;
                else candidate.state=State::VerifiedMismatchOrUnsafe;
                // A failed symlink_status must not silently convert the
                // whole inventory into a claim of complete qualification.
                if (ec) {
                    result.candidates.clear();
                    result.status=Status::UnqualifiedDirectory;
                    return result;
                }
            }
            result.candidates.push_back(std::move(candidate));
        }
        if (ec) {
            result.candidates.clear();
            result.status=Status::UnqualifiedDirectory;
            return result;
        }
        std::sort(result.candidates.begin(),result.candidates.end(),
            [](const auto& a,const auto& b) {
                return a.path.native()<b.path.native();
            });
        return result;
    } catch (const fs::filesystem_error&) {
        result.candidates.clear();
        result.status=Status::UnqualifiedDirectory;
        return result;
    }
}
} // namespace djmeta
