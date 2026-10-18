#pragma once
// Pure external-CUE write preflight. No filesystem mutation, no host writes.
// The caller must obtain the target's current snapshot and an explicit user action.
#include <cstdint>
#include <string_view>

namespace djmeta {

enum class PrecisionCueWriteDecision {
    Reject,
    ReviewRequired,
    NoOp,
    ReadyForExplicitWrite
};

struct PrecisionCueWritePreflight {
    bool preview_qualified = false;
    bool explicit_user_approval = false;
    bool source_snapshot_current = false;
    bool target_snapshot_current = false;
    bool target_exists = false;
    bool overwrite_approved = false;
    bool path_qualified = false;
    bool cue_content_qualified = false;
    bool existing_content_known = false;
    bool existing_content_equal = false;
};

// Fail closed: missing snapshot, path, content or edition evidence cannot authorize writing.
// Equality is checked before overwrite approval so a true no-op never changes timestamps.
// Caller must revalidate snapshots atomically at the actual write boundary.
inline PrecisionCueWriteDecision preflight_precision_cue_external_write(
    const PrecisionCueWritePreflight& p) noexcept {
    if (!p.path_qualified || !p.cue_content_qualified)
        return PrecisionCueWriteDecision::Reject;
    if (!p.preview_qualified || !p.source_snapshot_current ||
        !p.target_snapshot_current)
        return PrecisionCueWriteDecision::ReviewRequired;
    if (p.target_exists && !p.existing_content_known)
        return PrecisionCueWriteDecision::ReviewRequired;
    if (p.target_exists && p.existing_content_equal)
        return PrecisionCueWriteDecision::NoOp;
    if (!p.explicit_user_approval || (p.target_exists && !p.overwrite_approved))
        return PrecisionCueWriteDecision::ReviewRequired;
    return PrecisionCueWriteDecision::ReadyForExplicitWrite;
}

} // namespace djmeta
