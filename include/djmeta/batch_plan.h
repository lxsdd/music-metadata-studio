#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace djmeta {

// A pure, read-only preflight model. The foobar adapter supplies canonical
// identity keys and observed destination identities through host filesystem
// services; this core NEVER probes the filesystem or executes operations.
enum class FileAction { None, Rename, Move, Copy };
enum class FileRole { Audio, ExternalCue, Companion };
enum class TargetPresence { Unchecked, Missing, Existing };
enum class CueLinkState { Unchecked, NoExternalCue, Verified, Unresolved };
enum class PlanStatus { Ready, Unchanged, NeedsOverwriteApproval, Blocked };

struct FilePlanItem {
    // Stable canonical physical file identity. Never use a subsong ID here.
    std::string physical_id;
    std::string source_path;
    std::string source_key;
    // Exact host-observed source identity/version evidence from a read-only
    // probe. Not interchangeable with the canonical path or tag fingerprint.
    // A future writer must re-probe it immediately before mutation.
    std::string source_guard;
    std::string target_path;
    std::string target_key;
    FileAction action = FileAction::None;
    FileRole role = FileRole::Audio;
    // Legacy single CUE/companion association.
    std::string associated_audio_id;
    // External CUE: all selected, uniquely identified physical audio
    // participants of its (possibly Multi-FILE) reference set.
    // File reference identity must be qualified separately by the host.
    std::vector<std::string> associated_audio_ids;

    std::string metadata_fingerprint;
    // Approved/selected proposal postimage, not merely raw input fingerprint.
    std::string planned_metadata_fingerprint;
    std::string ruleset_revision;
    std::string routing_profile;
    std::string naming_expression;
    bool manual_override = false;
    // Companion files need a qualified preset policy or explicit manual
    // selection. This is distinct from foobar's legacy moveOtherFiles=yes.
    bool companion_policy_qualified = false;

    // Result of host filesystem preflight, not inferred by this engine.
    TargetPresence target_presence = TargetPresence::Unchecked;
    // Host-observed destination identity/stat guard, required if Existing.
    std::string target_guard;
    // Independent OS/foobar physical identity of an EXISTING destination.
    // Different canonical paths can be hardlinks/aliases of one real file.
    // Never derive this from destination strings or file extensions.
    std::string target_physical_id;
    // An audio file must explicitly report no external cue, or verified cue
    // handling. Unchecked and unresolved dependencies are always blocked.
    CueLinkState cue_links = CueLinkState::Unchecked;
    // For ExternalCue: the exact source bytes and projected FILE-reference
    // postimage must be qualified by a separate read-only cue parser.
    std::string cue_source_fingerprint;
    std::string cue_postimage_fingerprint;
    bool cue_references_will_change = false;
};

struct BatchApproval {
    // Bound to the exact item and filesystem-observation snapshot.
    std::string reviewed_plan_fingerprint;
    bool approve_all_observed_overwrites = false;
    // Optional subset, bound to the SAME plan fingerprint.
    std::vector<std::string> individually_approved_physical_ids{};
};

struct ItemDecision {
    PlanStatus status = PlanStatus::Blocked;
    // Stable diagnostic codes for the future foobar/DJ Library UI.
    std::vector<std::string> reasons;
    bool will_replace_existing_target = false;
    // READ-ONLY mutation intent, never permission to write. Any executor
    // must independently compare the live physical metadata/full CUE bytes
    // and source/target identities immediately before each I/O operation.
    bool metadata_write_needed = false;
    bool cue_bytes_write_needed = false;
    bool file_operation_needed = false;
};

struct BatchPlanReview {
    std::string plan_fingerprint;
    std::vector<ItemDecision> decisions;
    std::size_t requires_overwrite = 0;
    std::size_t blocked = 0;
    bool ready_to_apply = false; // advisory only; executor MUST re-probe
};

// Deterministic, no-IO review. Approval with a nonmatching fingerprint fails
// closed even if only one route, CUE reference, or observed target changed.
BatchPlanReview review_batch_plan(
    const std::vector<FilePlanItem>& items,
    const BatchApproval* approval = nullptr);

// A detached, immutable COPY of the exactly reviewed in-memory proposal.
// This object deliberately exposes no executor, handle, filesystem service
// or tag-writer. It is not permission to perform I/O: a future host executor
// MUST re-probe source/destination/CUE identities and resolve host policy
// immediately before the first write.
struct FrozenBatchPreview {
    const std::string plan_fingerprint;
    const std::vector<FilePlanItem> items;
    const std::vector<ItemDecision> decisions;
};

// Requires explicit approval bound to this exact current plan even when
// there are no pre-existing overwrite targets. Refuses stale, incomplete,
// unsafe or partially reviewed plans; never writes or reads any user files.
FrozenBatchPreview freeze_reviewed_batch_preview(
    const std::vector<FilePlanItem>& items,
    const BatchApproval& approval);

} // namespace djmeta
