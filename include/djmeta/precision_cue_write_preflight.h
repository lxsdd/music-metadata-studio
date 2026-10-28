#pragma once
// Precision CUE external export: deterministic, pure, and write-free.
// These results are NOT a filesystem or foobar authorization. The eventual
// writer must independently revalidate source and target handles at commit.
#include "djmeta/external_cue.h"
#include "djmeta/precision_cue.h"
#include "djmeta/precision_cue_filename.h"
#include "djmeta/precision_cue_sha256.h"
#include "djmeta/precision_cue_unicode_text.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {

// Product release gate, independent of the per-click confirmation. The
// unmerged Precision CUE Draft is REVIEW/COPY ONLY until the owner separately
// approves external .cue publication and original-host writer qualification.
// The synthetic create-only backend remains testable, but a native button,
// synthetic WM_COMMAND or restored profile cannot enable a production writer.
// There is intentionally no runtime preference or approval checkbox for this.
enum class PrecisionCueNativeOperation {
    Review,
    CopyCueText,
    CreateExternalSingle,
    CreateExternalBatch,
    ModifyEmbeddedCue,
    ModifyTags,
    ModifyAudio
};

inline constexpr bool precision_cue_native_operation_allowed(
    PrecisionCueNativeOperation operation) noexcept {
    switch (operation) {
    case PrecisionCueNativeOperation::Review:
    case PrecisionCueNativeOperation::CopyCueText:
        return true;
    default:
        return false; // Owner release NOT GRANTED; fail closed.
    }
}

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

// Canonical *generated* text validation, intentionally stricter than the
// general external-CUE parser used to inspect pre-existing files. No I/O.
inline bool precision_cue_strict_generated_layout(
    const PrecisionCuePlan& plan, std::string_view mp3_basename) {
    if (plan.external_cue_utf8.empty() ||
        plan.external_cue_utf8.size() > 1024 * 1024) return false;
    const std::string_view text(plan.external_cue_utf8);
    std::vector<std::string_view> lines;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const auto end = text.find('\n', cursor);
        if (end == std::string_view::npos || lines.size() >= 410) return false;
        lines.push_back(text.substr(cursor, end - cursor));
        cursor = end + 1;
    }
    std::size_t pos = 0;
    const auto exact = [&](std::string_view expected) {
        if (pos == lines.size() || lines[pos] != expected) return false;
        ++pos;
        return true;
    };
    const auto quoted = [&](std::string_view prefix) {
        if (pos == lines.size()) return false;
        const auto line = lines[pos];
        if (!line.starts_with(prefix) || line.size() <= prefix.size() + 2 ||
            line[prefix.size()] != '"' || line.back() != '"') return false;
        const auto value = line.substr(prefix.size() + 1,
                                       line.size() - prefix.size() - 2);
        if (value.find('"') != std::string_view::npos ||
            !precision_cue_safe_unicode_text(value))
            return false;
        ++pos;
        return true;
    };
    const auto number = [&](std::string_view prefix, unsigned& parsed) {
        if (pos == lines.size() || !lines[pos].starts_with(prefix)) return false;
        const auto digits = lines[pos].substr(prefix.size());
        if (digits.empty() || digits.size() > 2 || digits.front() == '0') return false;
        unsigned value = 0;
        for (const char c : digits) {
            if (c < '0' || c > '9') return false;
            value = value * 10 + static_cast<unsigned>(c - '0');
        }
        if (value == 0 || value > 99) return false;
        parsed = value;
        ++pos;
        return true;
    };
    if (pos < lines.size() && lines[pos].starts_with("REM GENRE ") &&
        !quoted("REM GENRE ")) return false;
    if (pos < lines.size() && lines[pos].starts_with("REM DATE ") &&
        !quoted("REM DATE ")) return false;
    unsigned disc = 0, total_discs = 0;
    if (!number("REM DISCNUMBER ", disc) ||
        !number("REM TOTALDISCS ", total_discs) ||
        disc > total_discs ||
        !quoted("PERFORMER ") || !quoted("TITLE ") ||
        !exact("FILE \"" + std::string(mp3_basename) + "\" WAVE")) return false;

    for (std::size_t i = 0; i < plan.boundaries.size(); ++i) {
        const unsigned track = static_cast<unsigned>(i + 1);
        const std::string track_line = "  TRACK " +
            std::string(track < 10 ? "0" : "") + std::to_string(track) + " AUDIO";
        if (!exact(track_line) || !quoted("    TITLE ") ||
            !quoted("    PERFORMER ") ||
            !exact("    INDEX 01 " +
                   cue_frame_to_index(plan.boundaries[i].cue_frame)))
            return false;
    }
    return pos == lines.size();
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
    // Validate the numeric boundary model before rendering expected INDEX lines.
    std::int64_t previous_ms = -1, previous_frame = -1;
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
            previous_ms = boundary.elapsed_ms;
            previous_frame = boundary.cue_frame;
        }
    } catch (const std::exception&) {
        result.diagnostic = "CUE_BOUNDARIES_INVALID";
        return result;
    }
    // The generic CUE inventory validates FILE syntax, not the generated
    // grammar. Require exactly the generator's header/track/index sequence:
    // no extra instructions, duplicate metadata, altered TRACK order, or
    // displaced INDEX lines may be exported as if reviewed.
    if (!precision_cue_strict_generated_layout(plan, mp3_basename)) {
        result.diagnostic = "CUE_GENERATED_LAYOUT_MISMATCH";
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
    bool overwrite_approved = false; // Legacy UI flag: NEVER grants create-only overwrite.
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
    // Precision CUE is CREATE-ONLY. A legacy overwrite checkbox or an
    // unequal existing target can never turn a collision into permission
    // to replace bytes. Re-review a newly absent target from fresh handles.
    if (p.target_exists || p.overwrite_approved || !p.explicit_user_approval)
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
    // Host MUST hash the exact reviewed bytes and the exact bytes handed to
    // its writer independently. Zero/changed digests fail closed.
    std::array<std::uint8_t, 32> reviewed_cue_sha256{};
    std::array<std::uint8_t, 32> at_commit_cue_sha256{};
};

inline PrecisionCueWriteDecision revalidate_precision_cue_external_commit(
    const PrecisionCueWriteCommitEvidence& e) noexcept {
    if (preflight_precision_cue_external_write(e.preview) !=
            PrecisionCueWriteDecision::ReadyForExplicitWrite)
        return PrecisionCueWriteDecision::ReviewRequired;
    if (e.reviewed_cue_sha256 == std::array<std::uint8_t, 32>{} ||
        e.reviewed_cue_sha256 != e.at_commit_cue_sha256)
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
