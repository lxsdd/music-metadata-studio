#pragma once
// Pure, stable, no-I/O visual representation of in-memory Studio decisions.
// Deliberately unrelated to CUE parsing, generation or a metadata writer.
#include "djmeta/review_decisions.h"

#include <string>
#include <string_view>

namespace djmeta {

// "Proposed" column is a preview of the CURRENT user decision. Reject must
// not continue showing a rejected candidate as the selected postimage.
// Reset (Pending) shows the proposal again, but that is not acceptance.
inline std::string_view review_visible_value(
    const std::string& original,
    const std::string& proposed,
    const ReviewDecision* decision) {
    if (!decision) return original;
    switch (decision->action) {
    case ReviewAction::Pending:
    case ReviewAction::Accept:
        return proposed;
    case ReviewAction::Reject:
        return original;
    case ReviewAction::ManualValue:
        return decision->manual_value;
    default:
        return original; // unknown serialized decision cannot display an edit
    }
}

} // namespace djmeta
