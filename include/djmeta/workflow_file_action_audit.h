#pragma once
// Read-only whole-batch FileOps intention ledger. This is NOT a destination
// resolver, CUE postimage, overwrite grant or executable preflight.
#include "djmeta/file_action_intent.h"
#include "djmeta/workflow_preview_audit.h"
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace djmeta {
struct WorkflowFileActionRow {
    FileAction action=FileAction::None;
    bool skip=false;
    bool blocked=false;
    bool raw_only=false;
    const char* reason="NO_FILE_OPERATION"; // stable public, path-free code
};
struct WorkflowFileActionAudit {
    std::vector<WorkflowFileActionRow> rows;
    std::size_t moves=0,copies=0,renames=0,skips=0;
    std::size_t blocked=0,raw_only=0;
};
inline WorkflowFileActionAudit audit_workflow_file_actions(
    const BatchPreviewTable& table,
    const std::vector<BatchPreviewInputRow>& sources,
    const std::vector<FileAction>& actions,
    const WorkflowPreviewAudit& review) {
    const auto n=table.rows.size();
    if(n>100000 || sources.size()!=n || actions.size()!=n ||
       review.tracks!=n || review.rows.size()!=n)
        throw std::invalid_argument("FILE_ACTION_AUDIT_BATCH_SHAPE");
    WorkflowFileActionAudit result;
    result.rows.reserve(n);
    for(std::size_t i=0;i<n;++i) {
        const auto action=actions[i];
        switch(action) {
        case FileAction::Move: ++result.moves; break;
        case FileAction::Copy: ++result.copies; break;
        case FileAction::Rename: ++result.renames; break;
        case FileAction::None: ++result.skips; break;
        default: throw std::invalid_argument("FILE_ACTION_AUDIT_UNKNOWN_ACTION");
        }
        const auto& raw=table.rows[i];
        const auto& metadata=review.rows[i];
        if(raw.has_raw_target_collision!=metadata.raw_collision ||
           metadata.file_plan_blocked!=!raw.issues.empty())
            throw std::invalid_argument("FILE_ACTION_AUDIT_STALE_ROUTE");
        auto source=sources[i];
        source.semantic_proposals_pending=
            source.semantic_proposals_pending ||
            metadata.pending_safe!=0 || metadata.pending_semantic!=0;
        const auto intent=inspect_file_action_intent(action,source,raw);
        WorkflowFileActionRow row;
        row.action=action;
        row.skip=intent.file_skip;
        row.blocked=intent.blocked;
        row.raw_only=intent.raw_candidate;
        row.reason=intent.reason;
        // A RAW candidate with a known row-specific route finding must not
        // be presented as a clean plan. Universal host/CUE gates are separate.
        if(row.raw_only) {
            for(const auto& code:raw.issues) {
                if(code=="FILESYSTEM_TARGET_UNCHECKED" ||
                   code=="CUE_DEPENDENCIES_UNCHECKED") continue;
                row.blocked=true;
                row.raw_only=false;
                row.reason="RAW_ROUTE_FINDING";
                break;
            }
        }
        if(row.skip && (row.blocked || row.raw_only))
            throw std::invalid_argument("FILE_ACTION_AUDIT_INVALID_SKIP");
        if(!row.skip && !row.blocked && !row.raw_only)
            throw std::invalid_argument("FILE_ACTION_AUDIT_INVALID_STAGE");
        if(row.blocked) ++result.blocked;
        if(row.raw_only) ++result.raw_only;
        result.rows.push_back(row);
    }
    if(result.moves+result.copies+result.renames+result.skips!=n ||
       result.blocked+result.raw_only+result.skips!=n)
        throw std::invalid_argument("FILE_ACTION_AUDIT_COUNTER_MISMATCH");
    return result;
}
// Filter the COMPLETE independently audited file-action ledger by its stage.
// This is display-only: sorted IDs are original captured source identities;
// hidden rows still participated in the whole-batch collision/route audit.
inline std::vector<std::size_t> filter_workflow_file_action_view(
    const WorkflowFileActionAudit& audit,
    const std::vector<std::size_t>& sorted_source_ids,
    BatchLocationQueue queue) {
    if(queue!=BatchLocationQueue::FileActionBlocked &&
       queue!=BatchLocationQueue::FileActionRawOnly &&
       queue!=BatchLocationQueue::FileActionSkipped)
        throw std::invalid_argument("FILE_ACTION_FILTER_UNKNOWN_STAGE");
    const auto n=audit.rows.size();
    if(n>100000 || sorted_source_ids.size()!=n)
        throw std::invalid_argument("FILE_ACTION_FILTER_STALE_BATCH");
    std::size_t moves=0,copies=0,renames=0,skips=0,blocked=0,raw_only=0;
    for(const auto& row:audit.rows) {
        switch(row.action) {
        case FileAction::Move: ++moves; break;
        case FileAction::Copy: ++copies; break;
        case FileAction::Rename: ++renames; break;
        case FileAction::None: ++skips; break;
        default: throw std::invalid_argument("FILE_ACTION_FILTER_UNKNOWN_ACTION");
        }
        if(row.skip!=(row.action==FileAction::None) ||
           (static_cast<int>(row.skip)+static_cast<int>(row.blocked)+
            static_cast<int>(row.raw_only))!=1 ||
           !row.reason || !*row.reason)
            throw std::invalid_argument("FILE_ACTION_FILTER_INVALID_STAGE");
        if(row.blocked) ++blocked;
        if(row.raw_only) ++raw_only;
    }
    if(moves!=audit.moves || copies!=audit.copies ||
       renames!=audit.renames || skips!=audit.skips ||
       blocked!=audit.blocked || raw_only!=audit.raw_only ||
       blocked+raw_only+skips!=n)
        throw std::invalid_argument("FILE_ACTION_FILTER_STALE_COUNTERS");
    std::vector<unsigned char> seen(n,0);
    std::vector<std::size_t> visible;
    visible.reserve(queue==BatchLocationQueue::FileActionBlocked?blocked:
                    queue==BatchLocationQueue::FileActionRawOnly?raw_only:skips);
    for(const auto source:sorted_source_ids) {
        if(source>=n || seen[source]++)
            throw std::invalid_argument("FILE_ACTION_FILTER_INVALID_SOURCE_ORDER");
        const auto& row=audit.rows[source];
        if((queue==BatchLocationQueue::FileActionBlocked && row.blocked) ||
           (queue==BatchLocationQueue::FileActionRawOnly && row.raw_only) ||
           (queue==BatchLocationQueue::FileActionSkipped && row.skip))
            visible.push_back(source);
    }
    return visible;
}

inline std::string format_workflow_file_action_tsv(
    const WorkflowFileActionAudit& actions,
    WorkflowAuditExportScope scope=WorkflowAuditExportScope::AllRows) {
    if(actions.rows.size()>100000 ||
       (scope!=WorkflowAuditExportScope::AllRows &&
        scope!=WorkflowAuditExportScope::AttentionRows))
        throw std::invalid_argument("FILE_ACTION_EXPORT_SCOPE_OR_LIMIT");
    std::size_t moves=0,copies=0,renames=0,skips=0,blocked=0,raw_only=0;
    std::size_t included=0;
    for(const auto& row:actions.rows) {
        switch(row.action) {
        case FileAction::Move: ++moves; break;
        case FileAction::Copy: ++copies; break;
        case FileAction::Rename: ++renames; break;
        case FileAction::None: ++skips; break;
        default: throw std::invalid_argument("FILE_ACTION_EXPORT_UNKNOWN_ACTION");
        }
        if(row.skip!= (row.action==FileAction::None) ||
           (static_cast<int>(row.skip)+static_cast<int>(row.blocked)+
            static_cast<int>(row.raw_only))!=1)
            throw std::invalid_argument("FILE_ACTION_EXPORT_INVALID_STAGE");
        if(row.blocked) ++blocked;
        if(row.raw_only) ++raw_only;
        if(scope==WorkflowAuditExportScope::AllRows || row.blocked) ++included;
    }
    if(moves!=actions.moves || copies!=actions.copies ||
       renames!=actions.renames || skips!=actions.skips ||
       blocked!=actions.blocked || raw_only!=actions.raw_only)
        throw std::invalid_argument("FILE_ACTION_EXPORT_STALE_COUNTERS");
    std::string out="File action ledger\tREAD-ONLY; NOT foobar FileOps approval\r\n";
    out+="Move\t"+std::to_string(moves)+"\tCopy\t"+std::to_string(copies)+
         "\tRename\t"+std::to_string(renames)+"\tNo file action\t"+
         std::to_string(skips)+"\tBlocked\t"+std::to_string(blocked)+
         "\tRAW ONLY\t"+std::to_string(raw_only)+"\r\n";
    out+="Scope\t"+std::string(scope==WorkflowAuditExportScope::AllRows?
         "ALL":"BLOCKED")+
         "\tIncluded rows\t"+std::to_string(included)+"\r\n";
    out+="Source row\tFile action\tStage\tReason\tFinal foobar target\tCUE FILE/sidecar\r\n";
    for(std::size_t i=0;i<actions.rows.size();++i) {
        const auto& row=actions.rows[i];
        // Validate hidden entries too: attention filtering never conceals
        // malformed source evidence or clipboard injection.
        const std::string_view reason(row.reason?row.reason:"");
        if(reason.empty() || reason.size()>80)
            throw std::invalid_argument("FILE_ACTION_EXPORT_INVALID_REASON");
        for(const unsigned char ch:reason)
            if(!((ch>='A'&&ch<='Z') || (ch>='0'&&ch<='9') || ch=='_'))
                throw std::invalid_argument("FILE_ACTION_EXPORT_UNSAFE_REASON");
        if(scope==WorkflowAuditExportScope::AttentionRows && !row.blocked)
            continue;
        out+="#"+std::to_string(i+1)+"\t"+
            std::string(file_action_label(row.action))+"\t"+
            (row.skip?"SKIP":row.blocked?"BLOCKED":"RAW_ONLY")+
            "\t"+std::string(reason)+
            "\tNOT_RESOLVED\tNOT_QUALIFIED\r\n";
        if(out.size()>16u*1024u*1024u)
            throw std::invalid_argument("FILE_ACTION_EXPORT_TOO_LARGE");
    }
    return out;
}
} // namespace djmeta
