#pragma once

#include "djmeta/normalizer.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {

// Stable proposal-index decisions, scoped to an AnalysisResult fingerprint and
// ruleset revision. No decision is a permission to write tags or files.
enum class ReviewAction { Pending, Accept, Reject, ManualValue };

struct ReviewDecision {
    ReviewAction action = ReviewAction::Pending;
    std::string manual_value;
};

// Shared validation for one or many explicit manual replacements. Never
// implies a write, deletion or automatic rule acceptance.
bool valid_manual_review_value(std::string_view text);

struct ReviewProjection {
    MetadataDocument document;
    std::size_t automatic_safe = 0;
    std::size_t explicitly_accepted = 0;
    std::size_t explicitly_rejected = 0;
    std::size_t manually_replaced = 0;
    std::size_t unresolved_semantic = 0;
};

// Pure staged preview: empty decisions means defaults (SAFE preview only).
// The input must match the captured analysis; conflicts/invalid indices or
// attempts to delete tags implicitly are rejected atomically by exception.
ReviewProjection project_review_decisions(
    const MetadataDocument& original,
    const AnalysisResult& analysis,
    const std::vector<ReviewDecision>& decisions = {});

// For routing and eventual physical postimages, only a user's Accept or
// ManualValue can change the source. Pending SAFE remains a visible proposal
// in the ordinary review view, but it must not silently rename a file.
// This projection is still read-only: no writer or approval is reachable.
ReviewProjection project_explicit_review_decisions(
    const MetadataDocument& original,
    const AnalysisResult& analysis,
    const std::vector<ReviewDecision>& decisions = {});

} // namespace djmeta
