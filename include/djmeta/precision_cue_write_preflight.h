#pragma once
// Precision CUE external export: deterministic, pure, and write-free.
// These results are NOT a filesystem or foobar authorization. The eventual
// writer must independently revalidate source and target handles at commit.
#include "djmeta/external_cue.h"
#include "djmeta/precision_cue.h"
#include "djmeta/precision_cue_filename.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {

enum class PrecisionCueWriteDecision {
    Reject,
    ReviewRequired,
    NoOp,
    ReadyForExplicitWrite
};

struct PrecisionCueExternalExportPlan {
    bool eligible = false;
    std::string target_cue_basename;
    std::string diagnostic;
};

inline std::string precision_cue_ascii_lower(std::string value) {
    for (char& c : value)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return value;
}

// Only a proposed sibling filename; NEVER a qualified destination path.
// Input must be an in-memory CUE produced by make_precision_cue().
inline PrecisionCueExternalExportPlan plan_precision_cue_external_export(
    const PrecisionCuePlan& plan, std::string_view mp3_basename) {
    PrecisionCueExternalExportPlan result;
    if (!precision_cue_safe_mp3_basename(mp3_basename)) {
        result.diagnostic = "MP3_BASENAME_INVALID";
        return result;
    }
    if (plan.boundaries.empty() || plan.boundaries.size() > 99 ||
        plan.total_duration_ms <= 0 || plan.external_cue_utf8.empty()) {
        result.diagnostic = "CUE_PLAN_INVALID";
        return result;
    }
    const auto syntax = inspect_external_cue(plan.external_cue_utf8);
    if (syntax.status != CueSyntaxStatus::Parsed ||
        syntax.encoding != CueTextEncoding::Utf8 ||
        syntax.references.size() != 1 ||
        syntax.references[0].filename != mp3_basename ||
        syntax.references[0].file_type != "WAVE") {
        result.diagnostic = "CUE_FILE_REFERENCE_OR_ENCODING_UNQUALIFIED";
        return result;
    }
    std::int64_t previous_ms = -1, previous_frame = -1;
    std::size_t search_at = 0;
    try {
        for (std::size_t i = 0; i < plan.boundaries.size(); ++i) {
            const auto& boundary = plan.boundaries[i];
            if (boundary.track_number != i + 1 ||
                boundary.elapsed_ms <= previous_ms ||
                boundary.cue_frame <= previous_frame ||
                boundary.cue_frame != milliseconds_to_cue_frame(boundary.elapsed_ms) ||
                (i == 0 && (boundary.elapsed_ms != 0 || boundary.cue_frame != 0))) {
                result.diagnostic = "CUE_BOUNDARIES_INVALID";
                return result;
            }
            const std::string marker = "    INDEX 01 " +
                cue_frame_to_index(boundary.cue_frame) + "\n";
            const auto found = plan.external_cue_utf8.find(marker, search_at);
            if (found == std::string::npos) {
                result.diagnostic = "CUE_INDEX_TEXT_MISMATCH";
                return result;
            }
            search_at = found + marker.size();
            previous_ms = boundary.elapsed_ms;
            previous_frame = boundary.cue_frame;
        }
    } catch (const std::exception&) {
        result.diagnostic = "CUE_BOUNDARIES_INVALID";
        return result;
    }
    if (previous_ms >= plan.total_duration_ms) {
        result.diagnostic = "CUE_TOTAL_DURATION_INVALID";
        return result;
    }
    // The extension is exactly four ASCII bytes, regardless of MP3 case.
    result.target_cue_basename = std::string(mp3_basename.substr(0, mp3_basename.size() - 4));
    result.target_cue_basename += ".cue";
    result.eligible = true;
    return result;
}

// Conservative ASCII collision check. Unicode aliases, reparse points and
// filesystem-specific case equivalence MUST be checked by the host writer.
inline bool precision_cue_export_targets_unique(
    const std::vector<PrecisionCueExternalExportPlan>& plans) {
    for (std::size_t i = 0; i < plans.size(); ++i) {
        if (!plans[i].eligible || plans[i].target_cue_basename.empty()) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (precision_cue_ascii_lower(plans[i].target_cue_basename) ==
                precision_cue_ascii_lower(plans[j].target_cue_basename))
                return false;
    }
    return true;
}

struct PrecisionCueWritePreflight {
    bool preview_available = false;
    bool preview_qualified = false;
    bool explicit_review_resolved = false;
    bool release_identity_confirmed = false;
    bool disc_identity_confirmed = false;
    bool explicit_user_approval = false;
    bool source_snapshot_current = false;
    bool target_snapshot_current = false; // Includes verified nonexistence.
    bool target_exists = false;
    bool overwrite_approved = false;
    bool path_qualified = false; // Full resolved host path, NOT basename alone.
    bool cue_content_qualified = false;
    bool existing_content_known = false;
    bool existing_content_equal = false;
};

// A true NoOp is reported only after identity and fresh source/target evidence.
// NoOp never opens a write handle. A rejected/ambiguous source cannot be
// approved by setting a generic checkbox: preview_available is mandatory.
// This function NEVER performs I/O or grants permission to change audio/tags.
inline PrecisionCueWriteDecision preflight_precision_cue_external_write(
    const PrecisionCueWritePreflight& p) noexcept {
    if (!p.path_qualified || !p.cue_content_qualified || !p.preview_available)
        return PrecisionCueWriteDecision::Reject;
    if ((!p.preview_qualified && !p.explicit_review_resolved) ||
        !p.release_identity_confirmed || !p.disc_identity_confirmed ||
        !p.source_snapshot_current || !p.target_snapshot_current)
        return PrecisionCueWriteDecision::ReviewRequired;
    // Unknown target content cannot be treated as absent or safely overwritten.
    if (!p.target_exists && (p.existing_content_known || p.existing_content_equal))
        return PrecisionCueWriteDecision::Reject;
    if (p.existing_content_equal && !p.existing_content_known)
        return PrecisionCueWriteDecision::Reject;
    if (p.target_exists && !p.existing_content_known)
        return PrecisionCueWriteDecision::ReviewRequired;
    if (p.target_exists && p.existing_content_equal)
        return PrecisionCueWriteDecision::NoOp;
    if (!p.explicit_user_approval || (p.target_exists && !p.overwrite_approved))
        return PrecisionCueWriteDecision::ReviewRequired;
    return PrecisionCueWriteDecision::ReadyForExplicitWrite;
}


// Pure two-phase commit gate. The host must collect the second evidence set
// immediately before opening a write handle, not reuse the preview snapshot.
// A changed target (including appearing/disappearing) always requires review.
// This does NOT perform I/O, establish path safety, or authorize embedded writes.
struct PrecisionCueWriteCommitEvidence {
    PrecisionCueWritePreflight preview;
    PrecisionCueWritePreflight at_commit;
    std::uint64_t source_snapshot_token_preview = 0;
    std::uint64_t source_snapshot_token_commit = 0;
    std::uint64_t target_snapshot_token_preview = 0;
    std::uint64_t target_snapshot_token_commit = 0;
};

inline PrecisionCueWriteDecision revalidate_precision_cue_external_commit(
    const PrecisionCueWriteCommitEvidence& e) noexcept {
    if (preflight_precision_cue_external_write(e.preview) !=
            PrecisionCueWriteDecision::ReadyForExplicitWrite)
        return PrecisionCueWriteDecision::ReviewRequired;
    if (e.source_snapshot_token_preview == 0 ||
        e.target_snapshot_token_preview == 0 ||
        e.source_snapshot_token_preview != e.source_snapshot_token_commit ||
        e.target_snapshot_token_preview != e.target_snapshot_token_commit ||
        e.preview.target_exists != e.at_commit.target_exists ||
        e.preview.existing_content_known != e.at_commit.existing_content_known ||
        e.preview.existing_content_equal != e.at_commit.existing_content_equal)
        return PrecisionCueWriteDecision::ReviewRequired;
    return preflight_precision_cue_external_write(e.at_commit) ==
                   PrecisionCueWriteDecision::ReadyForExplicitWrite
        ? PrecisionCueWriteDecision::ReadyForExplicitWrite
        : PrecisionCueWriteDecision::ReviewRequired;
}

} // namespace djmeta
