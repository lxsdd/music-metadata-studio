#pragma once
// Explicit online-field selection produces a detached preview, NOT a write.
// No foobar handles, network calls or filesystem I/O are available here.
#include "djmeta/online_fields.h"
#include <cstddef>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace djmeta::online {

struct OnlineFieldReviewSnapshot {
    MetadataDocument physical_before;
    std::vector<FieldEvidence> evidence;
    std::vector<FieldReviewRow> comparisons;
};

struct OnlineFieldStagedPreview {
    MetadataDocument planned;
    std::size_t explicitly_changed=0;
    std::size_t exact_noops=0;
};

// Both flags are independent, host/provider-supplied qualifications. In
// particular, a virtual CUE subsong and a mere search snippet may not stage
// physical tags. Actual writers would still require their own stale guards.
inline OnlineFieldReviewSnapshot capture_online_field_review(
    const MetadataDocument& originals,
    const std::vector<FieldEvidence>& evidence,
    bool physical_source_qualified, bool source_evidence_verified) {
    if (!physical_source_qualified || !source_evidence_verified)
        throw std::invalid_argument("Physical and provider qualifications required");
    if (originals.fields.size()>10000 || evidence.size()>1024)
        throw std::invalid_argument("Online field snapshot exceeds bounds");
    return {originals,evidence,review_online_fields(originals,evidence)};
}

inline bool same_online_field_review_row(const FieldReviewRow& a,
                                         const FieldReviewRow& b) {
    return a.evidence_index==b.evidence_index &&
           a.local_field_index==b.local_field_index &&
           a.field==b.field &&
           a.original_values==b.original_values &&
           a.candidate==b.candidate &&
           a.state==b.state && a.reason==b.reason;
}

// Explicit indices only: no accept-all default. Recompute the review from
// immutable evidence and the exact current physical document before staging.
// Conflicting providers targeting one tag, ambiguous local names, protected
// fields, duplicates and stale snapshots fail closed as a single operation.
// These values are not permission to invoke any physical tag/CUE/file writer.
inline OnlineFieldStagedPreview stage_selected_online_fields(
    const OnlineFieldReviewSnapshot& capture,
    const MetadataDocument& current_physical,
    const std::vector<std::size_t>& selected_indices) {
    if (current_physical!=capture.physical_before)
        throw std::invalid_argument("Stale physical original metadata");
    if (capture.evidence.size()>1024 ||
        capture.comparisons.size()!=capture.evidence.size() ||
        selected_indices.size()>capture.evidence.size())
        throw std::invalid_argument("Invalid online field review shape");
    const auto verified=review_online_fields(current_physical,capture.evidence);
    for(std::size_t i=0;i<verified.size();++i)
        if (!same_online_field_review_row(verified[i],capture.comparisons[i]))
            throw std::invalid_argument("Mutated online review snapshot");
    OnlineFieldStagedPreview result{current_physical};
    std::set<std::size_t> chosen_rows;
    std::set<std::string> chosen_tags;
    for(const auto index:selected_indices) {
        if (index>=verified.size() || !chosen_rows.insert(index).second)
            throw std::invalid_argument("Unknown or duplicate online field index");
        const auto& row=verified[index];
        if(row.state==FieldReviewState::Blocked)
            throw std::invalid_argument("Unqualified online field");
        const auto canonical=ascii_upper_field(row.candidate.field);
        if(canonical.empty() || !chosen_tags.insert(canonical).second)
            throw std::invalid_argument("Multiple providers target the same physical tag");
        if(row.state==FieldReviewState::Unchanged) {
            ++result.exact_noops;
            continue;
        }
        if(row.state!=FieldReviewState::NeedsReview)
            throw std::invalid_argument("Unknown online review status");
        if(row.local_field_index!=no_local_field) {
            if(row.local_field_index>=result.planned.fields.size())
                throw std::invalid_argument("Missing physical field identity");
            result.planned.fields[row.local_field_index].values=row.candidate.values;
        } else {
            result.planned.fields.push_back({row.candidate.field,row.candidate.values});
        }
        ++result.explicitly_changed;
    }
    return result;
}

} // namespace djmeta::online
