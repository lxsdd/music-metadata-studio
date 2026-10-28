#pragma once
// Pure, stable, no-I/O visual representation of in-memory Studio decisions.
// Deliberately unrelated to CUE parsing, generation or a metadata writer.
#include "djmeta/review_decisions.h"
#include "djmeta/metadata_diff.h"

#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

// Presentation projection only: headers never have a proposal identity.
struct ReviewGroupProjection {
    std::vector<std::size_t> row_order;
    std::vector<int> group_at_row; // 0..2 header, -1 actionable proposal
    std::array<std::size_t,3> counts{};
};

inline int review_safety_group(SafetyClass safety) noexcept {
    switch(safety) {
    case SafetyClass::Review: return 0;
    case SafetyClass::Confident: return 1;
    case SafetyClass::Safe: return 2;
    }
    return 0; // unknown classifications stay in the cautious group
}

inline ReviewGroupProjection group_review_rows(
    const std::vector<MetadataDiffRow>& rows,
    const std::vector<std::size_t>& sorted_indices,
    const std::array<bool,3>& expanded) {
    if(sorted_indices.size()!=rows.size())
        throw std::invalid_argument("Review sort identity count changed.");
    ReviewGroupProjection out;
    std::vector<bool> seen(rows.size(),false);
    for(const auto index:sorted_indices) {
        if(index>=rows.size() || seen[index])
            throw std::invalid_argument("Review sort identity is invalid.");
        seen[index]=true;
        ++out.counts[review_safety_group(rows[index].safety)];
    }
    out.row_order.reserve(rows.size()+3);
    out.group_at_row.reserve(rows.size()+3);
    for(int group=0;group<3;++group) {
        if(out.counts[group]==0) continue;
        out.row_order.push_back((std::numeric_limits<std::size_t>::max)());
        out.group_at_row.push_back(group);
        if(!expanded[group]) continue;
        for(const auto index:sorted_indices) {
            if(review_safety_group(rows[index].safety)!=group) continue;
            out.row_order.push_back(index);
            out.group_at_row.push_back(-1);
        }
    }
    return out;
}

} // namespace djmeta
