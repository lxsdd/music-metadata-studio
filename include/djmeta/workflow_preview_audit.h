#pragma once
// Captured-snapshot, read-only audit of local normalizer decisions and raw
// FileOps candidates. Neither this result nor SAFE preview is write approval.
#include "djmeta/batch_preview.h"
#include "djmeta/review_decisions.h"
#include "djmeta/structural_guard.h"
#include <cstddef>
#include <optional>
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

// Read-only source-identity navigation; never final FileOps authority.
enum class WorkflowNextIssueKind { None, PendingReview, PendingConfident,
    PendingSafe, RawRouteConflict, RawRouteBlocker, FinalFileOpsUnqualified };
struct WorkflowNextIssue {
    WorkflowNextIssueKind kind=WorkflowNextIssueKind::None;
    std::size_t source_index=0,proposal_index=0;
    std::string issue_code;
};
inline WorkflowNextIssue next_workflow_issue(
    const WorkflowPreviewAudit& audit,const BatchPreviewTable& table,
    const std::vector<AnalysisResult>& analyses,
    const std::vector<std::vector<ReviewDecision>>& decisions,
    std::size_t cursor) {
    const auto n=analyses.size();
    if(audit.tracks!=n || audit.rows.size()!=n ||
       table.rows.size()!=n || decisions.size()!=n || n>100000)
        throw std::invalid_argument("WORKFLOW_NEXT_ISSUE_STALE_BATCH");
    if(!n) return {};
    const auto start=cursor<n?cursor:std::size_t{0};
    for(int priority=0;priority<5;++priority)
        for(std::size_t offset=0;offset<n;++offset) {
            const auto i=(start+offset)%n;
            const auto& a=analyses[i];
            const auto& d=decisions[i];
            if(a.proposals.size()!=d.size())
                throw std::invalid_argument("WORKFLOW_NEXT_ISSUE_STALE_DECISIONS");
            if(priority<3) {
                for(std::size_t j=0;j<d.size();++j) {
                    if(d[j].action!=ReviewAction::Pending) continue;
                    const auto safety=a.proposals[j].safety;
                    if(priority==0 && safety==SafetyClass::Review)
                        return {WorkflowNextIssueKind::PendingReview,i,j,{}};
                    if(priority==1 && safety==SafetyClass::Confident)
                        return {WorkflowNextIssueKind::PendingConfident,i,j,{}};
                    if(priority==2 && safety==SafetyClass::Safe)
                        return {WorkflowNextIssueKind::PendingSafe,i,j,{}};
                }
            } else for(const auto& code:table.rows[i].issues) {
                if(code=="FILESYSTEM_TARGET_UNCHECKED" ||
                   code=="CUE_DEPENDENCIES_UNCHECKED" ||
                   code=="UNAPPROVED_METADATA_PROPOSALS") continue;
                const bool collision=code=="DUPLICATE_RAW_TARGET" ||
                    code=="RAW_TARGET_CASE_OR_SEPARATOR_COLLISION" ||
                    code=="RAW_TARGET_ALIASES_SOURCE" ||
                    code=="RAW_TARGET_IS_BATCH_SOURCE" ||
                    code=="RAW_TARGET_SHARED_PHYSICAL_ID" ||
                    code=="RAW_TARGET_SHARED_HOST_CANONICAL";
                if(priority==3 && collision)
                    return {WorkflowNextIssueKind::RawRouteConflict,i,0,code};
                if(priority==4 && !collision)
                    return {WorkflowNextIssueKind::RawRouteBlocker,i,0,code};
            }
        }
    if(audit.file_plan_blocked_tracks)
        return {WorkflowNextIssueKind::FinalFileOpsUnqualified,0,0,{}};
    return {};
}

// The native host attaches this to at most one exact, independently
// re-probed physical Discogs source. These are different units from the
// proposal-count columns below: counts here are DISTINCT physical fields
// in one merged local+official-edition postimage, not individual proposals.
// Host source version evidence is never exported. No approval is captured.
struct WorkflowOnlineSourceCounts {
    std::size_t source_index=0;
    std::size_t combined_changed_fields=0;
    std::size_t local_changed_fields=0;
    std::size_t selected_online_changed_fields=0;
    std::size_t mutually_agreed_fields=0;
    std::size_t selected_online_fields=0;
    std::string verified_source_guard;
};

// Full-library and attention-only exports retain original source row IDs.
// A display filter never weakens global collision or stale-evidence checks.
// No music paths, tag values, source guards, tokens or approval are exported.
enum class WorkflowAuditExportScope { AllRows, AttentionRows };
inline std::string format_workflow_audit_tsv(
    const BatchPreviewTable& table,const WorkflowPreviewAudit& audit,
    const std::optional<WorkflowOnlineSourceCounts>& online=std::nullopt,
    WorkflowAuditExportScope scope=WorkflowAuditExportScope::AllRows) {
    if(table.rows.size()!=audit.tracks || audit.rows.size()!=audit.tracks ||
       audit.tracks>100000)
        throw std::invalid_argument("WORKFLOW_AUDIT_EXPORT_SHAPE_OR_LIMIT");
    if(online &&
       (online->source_index>=audit.tracks ||
        online->verified_source_guard.empty() ||
        online->selected_online_fields==0 ||
        online->selected_online_fields>10000 ||
        online->combined_changed_fields>10000 ||
        online->local_changed_fields>10000 ||
        online->selected_online_changed_fields>online->selected_online_fields ||
        online->mutually_agreed_fields>online->selected_online_fields))
        throw std::invalid_argument("WORKFLOW_AUDIT_ONLINE_SOURCE_EVIDENCE_INVALID");
    if(scope!=WorkflowAuditExportScope::AllRows &&
       scope!=WorkflowAuditExportScope::AttentionRows)
        throw std::invalid_argument("WORKFLOW_AUDIT_EXPORT_SCOPE_INVALID");
    const auto needs_attention=[&](std::size_t i) {
        const auto& row=audit.rows[i];
        return row.explicit_changes!=0 || row.pending_safe!=0 ||
            row.pending_semantic!=0 || row.raw_collision ||
            row.file_plan_blocked || (online && i==online->source_index);
    };
    std::size_t included=0;
    for(std::size_t i=0;i<audit.tracks;++i)
        if(scope==WorkflowAuditExportScope::AllRows || needs_attention(i))
            ++included;
    std::string out="Music Metadata Studio\tREAD-ONLY batch audit; no write approval\r\n";
    out+="Tracks\t"+std::to_string(audit.tracks)+
         "\tLocal staged changed tracks\t"+
         std::to_string(audit.normalizer_changed_tracks)+
         "\tPending tracks\t"+std::to_string(audit.tracks_with_pending)+
         "\tRaw collisions\t"+std::to_string(audit.raw_collision_tracks)+
         "\tFileOps unqualified\t"+std::to_string(audit.file_plan_blocked_tracks)+
         "\tVerified joint source\t"+
         (online ? "#"+std::to_string(online->source_index+1) : "NONE")+
         "\r\n";
    out+="Export scope\t"+
        std::string(scope==WorkflowAuditExportScope::AllRows ? "ALL" : "ATTENTION")+
        "\tIncluded rows\t"+std::to_string(included)+"\r\n";
    out+="Source row\tLocal proposal changes\tExact no-ops\tRejected fields\tPending SAFE\tPending semantic\tRaw collision\tFileOps unqualified\tFindings\tVerified combined changed fields\tSelected Discogs changed fields\tLocal changed fields\tMutually agreed fields\tJoint field evidence\r\n";
    out.reserve(out.size()+included*160);
    for(std::size_t i=0;i<audit.tracks;++i) {
        const auto& row=audit.rows[i];
        const auto& issues=table.rows[i].issues;
        if(issues.size()>64 ||
           row.raw_collision!=table.rows[i].has_raw_target_collision ||
           row.file_plan_blocked!=!issues.empty())
            throw std::invalid_argument("WORKFLOW_AUDIT_EXPORT_ROW_STALE");
        // Hidden rows are validated too; attention filtering is output-only.
        for(const auto& issue:issues) {
            if(issue.empty() || issue.size()>80)
                throw std::invalid_argument("WORKFLOW_AUDIT_EXPORT_UNSAFE_CODE");
            for(const unsigned char ch:issue)
                if(!((ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_'))
                    throw std::invalid_argument("WORKFLOW_AUDIT_EXPORT_UNSAFE_CODE");
        }
        if(scope==WorkflowAuditExportScope::AttentionRows &&
           !needs_attention(i)) continue;
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
            if(!first) out+=';';
            out+=issue;
            first=false;
        }
        if(online && i==online->source_index) {
            out+="\t"+std::to_string(online->combined_changed_fields)+
                 "\t"+std::to_string(online->selected_online_changed_fields)+
                 "\t"+std::to_string(online->local_changed_fields)+
                 "\t"+std::to_string(online->mutually_agreed_fields)+
                 "\tSOURCE_VERIFIED_POSTIMAGE_ONLY";
        } else out+="\tN/A\tN/A\tN/A\tN/A\tNOT_APPLICABLE";
        out+="\r\n";
        if(out.size()>16u*1024u*1024u)
            throw std::invalid_argument("WORKFLOW_AUDIT_EXPORT_TOO_LARGE");
    }
    return out;
}

} // namespace djmeta
