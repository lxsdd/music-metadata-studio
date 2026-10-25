#pragma once
// Captured-snapshot, read-only audit of local normalizer decisions and raw
// FileOps candidates. Neither this result nor SAFE preview is write approval.
#include "djmeta/batch_preview.h"
#include "djmeta/review_decisions.h"
#include "djmeta/structural_guard.h"
#include <cstddef>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace djmeta {

struct WorkflowPreviewAudit {
    std::size_t tracks=0;
    std::size_t normalizer_changed_tracks=0;
    std::size_t normalizer_unchanged_tracks=0;
    std::size_t explicit_changed_values=0;
    std::size_t explicit_noop_values=0;
    std::size_t rejected_values=0;
    std::size_t pending_safe_values=0;
    std::size_t pending_semantic_values=0;
    std::size_t tracks_with_pending_semantic=0;
    std::size_t raw_collision_tracks=0;
    std::size_t file_plan_blocked_tracks=0;
    // No ready/approved bit: raw destinations are not foobar FileOps targets.
};

inline WorkflowPreviewAudit audit_workflow_preview(
    const BatchPreviewTable& table,
    const std::vector<AnalysisResult>& analyses,
    const std::vector<std::vector<ReviewDecision>>& decisions) {
    if(table.rows.size()!=analyses.size() ||
       analyses.size()!=decisions.size() || analyses.size()>100000)
        throw std::invalid_argument("WORKFLOW_AUDIT_BATCH_SHAPE_INVALID");
    WorkflowPreviewAudit audit;
    audit.tracks=analyses.size();
    for(std::size_t i=0;i<analyses.size();++i) {
        const auto& analysis=analyses[i];
        const auto& chosen=decisions[i];
        const auto& path=table.rows[i];
        if(analysis.input_fingerprint.empty() ||
           analysis.ruleset_revision.empty() ||
           chosen.size()!=analysis.proposals.size() ||
           analysis.proposals.size()>10000)
            throw std::invalid_argument("WORKFLOW_AUDIT_REVIEW_SHAPE_INVALID");
        std::set<std::pair<std::size_t,std::size_t>> targets;
        bool changed=false,pending_semantic=false;
        for(std::size_t p=0;p<chosen.size();++p) {
            const auto& proposal=analysis.proposals[p];
            const auto& decision=chosen[p];
            if(!targets.emplace(proposal.field_index,proposal.value_index).second)
                throw std::invalid_argument("WORKFLOW_AUDIT_DUPLICATE_PROPOSAL");
            if(proposal.safety!=SafetyClass::Safe &&
               proposal.safety!=SafetyClass::Confident &&
               proposal.safety!=SafetyClass::Review)
                throw std::invalid_argument("WORKFLOW_AUDIT_UNKNOWN_SAFETY");
            switch(decision.action) {
            case ReviewAction::Pending:
                if(!decision.manual_value.empty())
                    throw std::invalid_argument("WORKFLOW_AUDIT_UNUSED_MANUAL_VALUE");
                if(proposal.safety==SafetyClass::Safe)
                    ++audit.pending_safe_values;
                else {
                    ++audit.pending_semantic_values;
                    pending_semantic=true;
                }
                break;
            case ReviewAction::Reject:
                if(!decision.manual_value.empty())
                    throw std::invalid_argument("WORKFLOW_AUDIT_UNUSED_MANUAL_VALUE");
                ++audit.rejected_values;
                break;
            case ReviewAction::Accept:
                if(!decision.manual_value.empty())
                    throw std::invalid_argument("WORKFLOW_AUDIT_UNUSED_MANUAL_VALUE");
                if((proposal.proposed_value.empty() && !proposal.original_value.empty()) ||
                   !valid_utf8_metadata_text(proposal.proposed_value))
                    throw std::invalid_argument("WORKFLOW_AUDIT_INVALID_ACCEPT");
                if(proposal.proposed_value!=proposal.original_value) {
                    ++audit.explicit_changed_values;
                    changed=true;
                } else ++audit.explicit_noop_values;
                break;
            case ReviewAction::ManualValue:
                if(decision.manual_value.empty() ||
                   !valid_utf8_metadata_text(decision.manual_value))
                    throw std::invalid_argument("WORKFLOW_AUDIT_INVALID_MANUAL_VALUE");
                for(unsigned char ch:decision.manual_value)
                    if(ch==0 || ch==0x7f || (ch<0x20 && ch!='\t'))
                        throw std::invalid_argument("WORKFLOW_AUDIT_INVALID_MANUAL_VALUE");
                if(decision.manual_value!=proposal.original_value) {
                    ++audit.explicit_changed_values;
                    changed=true;
                } else ++audit.explicit_noop_values;
                break;
            default:
                throw std::invalid_argument("WORKFLOW_AUDIT_UNKNOWN_DECISION");
            }
        }
        if(changed) ++audit.normalizer_changed_tracks;
        else ++audit.normalizer_unchanged_tracks;
        if(pending_semantic) ++audit.tracks_with_pending_semantic;
        if(path.has_raw_target_collision) ++audit.raw_collision_tracks;
        if(!path.issues.empty()) ++audit.file_plan_blocked_tracks;
    }
    if(audit.raw_collision_tracks!=table.raw_collision_items ||
       audit.file_plan_blocked_tracks!=table.unqualified_rows)
        throw std::invalid_argument("WORKFLOW_AUDIT_PATH_COUNTERS_STALE");
    return audit;
}

} // namespace djmeta
