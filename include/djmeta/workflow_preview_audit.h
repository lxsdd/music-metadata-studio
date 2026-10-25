#pragma once
// Captured-snapshot, read-only audit of local normalizer decisions and raw
// FileOps candidates. Neither this result nor SAFE preview is write approval.
#include "djmeta/batch_preview.h"
#include "djmeta/review_decisions.h"
#include "djmeta/structural_guard.h"
#include <cstddef>
#include <string>
#include <string_view>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace djmeta {

// Per-source-order evidence: in-memory decisions, never a write authorization.
struct WorkflowAuditRow {
    std::size_t explicit_changes=0;
    std::size_t exact_noops=0;
    std::size_t rejected=0;
    std::size_t pending_safe=0;
    std::size_t pending_semantic=0;
    bool raw_collision=false;
    bool file_plan_blocked=false;
};

struct WorkflowPreviewAudit {
    std::vector<WorkflowAuditRow> rows;
    std::size_t tracks_without_proposals=0;
    std::size_t tracks_with_pending=0;
    std::size_t tracks_with_changed_and_pending=0;
    std::size_t tracks_with_only_noops_or_rejections=0;
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
    audit.rows.reserve(analyses.size());
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
        WorkflowAuditRow detail;
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
                if(proposal.safety==SafetyClass::Safe) {
                    ++audit.pending_safe_values;
                    ++detail.pending_safe;
                } else {
                    ++audit.pending_semantic_values;
                    ++detail.pending_semantic;
                    pending_semantic=true;
                }
                break;
            case ReviewAction::Reject:
                if(!decision.manual_value.empty())
                    throw std::invalid_argument("WORKFLOW_AUDIT_UNUSED_MANUAL_VALUE");
                ++audit.rejected_values;
                ++detail.rejected;
                break;
            case ReviewAction::Accept:
                if(!decision.manual_value.empty())
                    throw std::invalid_argument("WORKFLOW_AUDIT_UNUSED_MANUAL_VALUE");
                if((proposal.proposed_value.empty() && !proposal.original_value.empty()) ||
                   !valid_utf8_metadata_text(proposal.proposed_value))
                    throw std::invalid_argument("WORKFLOW_AUDIT_INVALID_ACCEPT");
                if(proposal.proposed_value!=proposal.original_value) {
                    ++audit.explicit_changed_values;
                    ++detail.explicit_changes;
                    changed=true;
                } else { ++audit.explicit_noop_values; ++detail.exact_noops; }
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
                    ++detail.explicit_changes;
                    changed=true;
                } else { ++audit.explicit_noop_values; ++detail.exact_noops; }
                break;
            default:
                throw std::invalid_argument("WORKFLOW_AUDIT_UNKNOWN_DECISION");
            }
        }
        if(changed) ++audit.normalizer_changed_tracks;
        else ++audit.normalizer_unchanged_tracks;
        if(pending_semantic) ++audit.tracks_with_pending_semantic;
        if(analysis.proposals.empty()) ++audit.tracks_without_proposals;
        if(detail.pending_safe || detail.pending_semantic) {
            ++audit.tracks_with_pending;
            if(changed) ++audit.tracks_with_changed_and_pending;
        } else if(!changed && !analysis.proposals.empty()) {
            ++audit.tracks_with_only_noops_or_rejections;
        }
        detail.raw_collision=path.has_raw_target_collision;
        detail.file_plan_blocked=!path.issues.empty();
        if(detail.raw_collision) ++audit.raw_collision_tracks;
        if(detail.file_plan_blocked) ++audit.file_plan_blocked_tracks;
        audit.rows.push_back(detail);
    }
    if(audit.raw_collision_tracks!=table.raw_collision_items ||
       audit.file_plan_blocked_tracks!=table.unqualified_rows)
        throw std::invalid_argument("WORKFLOW_AUDIT_PATH_COUNTERS_STALE");
    return audit;
}

// Full, bounded per-track export: no music paths, tag values, tokens or
// persisted approval. Row numbers are original source-selection order.
// Explicit errors replace silent truncation when a batch is too large.
inline std::string format_workflow_audit_tsv(
    const BatchPreviewTable& table,const WorkflowPreviewAudit& audit) {
    if(table.rows.size()!=audit.tracks || audit.rows.size()!=audit.tracks ||
       audit.tracks>5000)
        throw std::invalid_argument("WORKFLOW_AUDIT_EXPORT_SHAPE_OR_LIMIT");
    std::string out="Music Metadata Studio\tREAD-ONLY batch audit; no write approval\r\n";
    out+="Tracks\t"+std::to_string(audit.tracks)+
         "\tStaged changed tracks\t"+
         std::to_string(audit.normalizer_changed_tracks)+
         "\tPending tracks\t"+std::to_string(audit.tracks_with_pending)+
         "\tRaw collisions\t"+std::to_string(audit.raw_collision_tracks)+
         "\tFileOps unqualified\t"+std::to_string(audit.file_plan_blocked_tracks)+
         "\r\n";
    out+="Source row\tChanged fields\tExact no-ops\tRejected fields\tPending SAFE\tPending semantic\tRaw collision\tFileOps unqualified\tFindings\r\n";
    out.reserve(out.size()+audit.tracks*160);
    for(std::size_t i=0;i<audit.tracks;++i) {
        const auto& row=audit.rows[i];
        const auto& issues=table.rows[i].issues;
        if(issues.size()>64 ||
           row.raw_collision!=table.rows[i].has_raw_target_collision ||
           row.file_plan_blocked!=!issues.empty())
            throw std::invalid_argument("WORKFLOW_AUDIT_EXPORT_ROW_STALE");
        out+="#"+std::to_string(i+1)+"\t"+
             std::to_string(row.explicit_changes)+"\t"+
             std::to_string(row.exact_noops)+"\t"+
             std::to_string(row.rejected)+"\t"+
             std::to_string(row.pending_safe)+"\t"+
             std::to_string(row.pending_semantic)+"\t"+
             (row.raw_collision?"YES":"NO")+"\t"+
             (row.file_plan_blocked?"YES":"NO")+"\t";
        bool first=true;
        for(const auto& issue:issues) {
            if(issue.empty() || issue.size()>80)
                throw std::invalid_argument("WORKFLOW_AUDIT_EXPORT_UNSAFE_CODE");
            for(const unsigned char ch:issue)
                if(!((ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_'))
                    throw std::invalid_argument("WORKFLOW_AUDIT_EXPORT_UNSAFE_CODE");
            if(!first) out+=';';
            out+=issue;
            first=false;
        }
        out+="\r\n";
        if(out.size()>2u*1024u*1024u)
            throw std::invalid_argument("WORKFLOW_AUDIT_EXPORT_TOO_LARGE");
    }
    return out;
}

} // namespace djmeta
