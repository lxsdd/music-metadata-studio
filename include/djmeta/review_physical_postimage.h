#pragma once
// Bridge explicit local normalizer decisions to a detached physical-tag
// postimage. No foobar SDK, filesystem, writer, approval persistence or I/O.
#include "djmeta/physical_tag_postimage.h"
#include "djmeta/review_decisions.h"
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace djmeta {

// SAFE is an automatic *preview* classification, never implicit approval for
// a physical tag write. Only Accept and ManualValue decisions cross this gate.
// Pending/Reject proposals, including SAFE proposals, retain original values.
inline PhysicalTagPostimage preview_explicit_normalizer_physical_tags(
    const PhysicalTagSourceSnapshot& source,
    const MetadataDocument& fresh_tags,
    const std::string& live_physical_key,
    const std::string& live_source_guard,
    const AnalysisResult& analysis,
    const std::string& active_ruleset_revision,
    const std::vector<ReviewDecision>& decisions) {
    const auto refuse=[](const char* why) {
        PhysicalTagPostimage result;
        result.rejection=why;
        return result;
    };
    if(analysis.ruleset_revision.empty() ||
       active_ruleset_revision!=analysis.ruleset_revision ||
       analysis.input_fingerprint!=fingerprint(source.original))
        return refuse("NORMALIZER_RULESET_OR_SOURCE_STALE");
    if(analysis.proposals.size()>10000 ||
       decisions.size()!=analysis.proposals.size())
        return refuse("NORMALIZER_DECISION_CARDINALITY_INVALID");
    ReviewProjection projected;
    try {
        // Same explicit-only projection as foobar's filename routing. A
        // Pending SAFE suggestion must never appear in either postimage.
        projected=project_explicit_review_decisions(
            source.original,analysis,decisions);
    } catch(const std::invalid_argument&) {
        return refuse("NORMALIZER_PROPOSALS_OR_DECISIONS_UNQUALIFIED");
    }
    // A normalizer decision changes values only. No metadata field deletion,
    // renaming, reordering, or implicit provider omission is possible here.
    if(projected.document.fields.size()!=source.original.fields.size())
        return refuse("NORMALIZER_FIELD_STRUCTURE_CHANGED");
    std::vector<PhysicalTagValueApproval> approvals;
    for(std::size_t i=0;i<projected.document.fields.size();++i) {
        const auto& before=source.original.fields[i];
        const auto& after=projected.document.fields[i];
        if(before.name!=after.name)
            return refuse("NORMALIZER_FIELD_STRUCTURE_CHANGED");
        if(before.values==after.values) continue;
        if(approvals.size()>=1024)
            return refuse("NORMALIZER_PHYSICAL_APPROVAL_LIMIT");
        approvals.push_back({before.name,before.values,after.values});
    }
    // The physical guard independently rechecks identity, freshness, duplicate
    // field names, protected tags, exact multivalues and zero-write no-ops.
    return preview_explicit_physical_tag_values(
        source,fresh_tags,live_physical_key,live_source_guard,approvals);
}

} // namespace djmeta
