#include "djmeta/workflow_preview_audit.h"
#include "djmeta/workflow_file_action_audit.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

void check(bool ok,const char* why) {
    if(!ok) { std::cerr<<"FAIL: "<<why<<"\n";std::exit(1); }
}
template<class F> void rejects(F&& f,const char* why) {
    try { f(); } catch(const std::invalid_argument&) { return; }
    check(false,why);
}
int main() {
    using namespace djmeta;
    BatchPreviewTable table;
    table.rows.resize(4);
    table.rows[1].issues={"RAW_TARGET_EXISTS","FILESYSTEM_TARGET_UNCHECKED"};
    table.rows[2].issues={"DUPLICATE_RAW_TARGET"};
    table.rows[2].has_raw_target_collision=true;
    table.unqualified_rows=2;table.raw_collision_items=1;
    std::vector<AnalysisResult> analyses(4);
    std::vector<std::vector<ReviewDecision>> decisions(4);
    for(auto& a:analyses) {
        a.input_fingerprint="SENSITIVE_CAPTURED_FINGERPRINT_1234";
        a.ruleset_revision="rules";
    }
    auto proposal=[](std::size_t idx,SafetyClass safety,
                     const char* original,const char* candidate) {
        Proposal p;
        p.field_index=idx;p.value_index=0;p.safety=safety;
        p.original_value=original;p.proposed_value=candidate;
        return p;
    };
    analyses[0].proposals={proposal(0,SafetyClass::Safe," old ","old"),
                           proposal(1,SafetyClass::Review,"Radio","Extended")};
    decisions[0]={{ReviewAction::Pending,{}},{ReviewAction::Accept,{}}};
    analyses[1].proposals={proposal(0,SafetyClass::Confident,"X","Y"),
                           proposal(1,SafetyClass::Safe,"OK","OK")};
    decisions[1]={{ReviewAction::ManualValue,"X"},{ReviewAction::Accept,{}}};
    analyses[2].proposals={proposal(0,SafetyClass::Review,"A","B")};
    decisions[2]={{ReviewAction::Reject,{}}};
    analyses[3].proposals={proposal(0,SafetyClass::Review,"a","b")};
    decisions[3]={{ReviewAction::Pending,{}}};
    const auto summary=audit_workflow_preview(table,analyses,decisions);
    check(summary.tracks==4 && summary.normalizer_changed_tracks==1 &&
          summary.normalizer_unchanged_tracks==3 &&
          summary.explicit_changed_values==1 && summary.explicit_noop_values==2 &&
          summary.rejected_values==1 && summary.pending_safe_values==1 &&
          summary.pending_semantic_values==1 &&
          summary.tracks_with_pending_semantic==1 &&
          summary.raw_collision_tracks==1 && summary.file_plan_blocked_tracks==2 &&
          summary.rows.size()==4 && summary.tracks_without_proposals==0 &&
          summary.tracks_with_pending==2 &&
          summary.tracks_with_changed_and_pending==1 &&
          summary.tracks_with_only_noops_or_rejections==2 &&
          summary.rows[0].explicit_changes==1 &&
          summary.rows[0].pending_safe==1 &&
          summary.rows[1].exact_noops==2 &&
          summary.rows[2].rejected==1 &&
          summary.rows[2].raw_collision && summary.rows[2].file_plan_blocked,
          "local explicit decisions and raw FileOps blockers remain independent");
    // Full user-facing File locations ledger: all four independent staged
    // intentions, pending review, collision, overwrite risk and no-op.
    std::vector<BatchPreviewInputRow> file_sources(4);
    for(auto& source:file_sources) {
        source.physical_source_qualified=true;
        source.source_physical_key="private-host-id";
        source.destination_root=R"(Z:\Music\Singles)";
        source.raw_relative_path="Test/Artist - Track";
    }
    const std::vector<FileAction> file_actions={
        FileAction::Move,FileAction::Copy,FileAction::Rename,FileAction::None};
    const auto ledger=audit_workflow_file_actions(
        table,file_sources,file_actions,summary);
    check(ledger.moves==1 && ledger.copies==1 && ledger.renames==1 &&
          ledger.skips==1 && ledger.blocked==3 && ledger.raw_only==0 &&
          ledger.rows[0].blocked && ledger.rows[1].blocked &&
          ledger.rows[2].blocked && ledger.rows[3].skip,
          "whole-batch mixed actions remain blocked by pending review, raw findings and rename");
    const auto file_tsv=format_workflow_file_action_tsv(ledger);
    check(file_tsv.find("#1\tMove\tBLOCKED")!=std::string::npos &&
          file_tsv.find("#2\tCopy\tBLOCKED")!=std::string::npos &&
          file_tsv.find("#3\tRename\tBLOCKED")!=std::string::npos &&
          file_tsv.find("#4\tNo file action\tSKIP")!=std::string::npos &&
          file_tsv.find("private-host-id")==std::string::npos &&
          file_tsv.find("Artist")==std::string::npos,
          "per-source action export preserves identity order without paths");
    const auto blocked_tsv=format_workflow_file_action_tsv(
        ledger,WorkflowAuditExportScope::AttentionRows);
    check(blocked_tsv.find("Included rows\t3")!=std::string::npos &&
          blocked_tsv.find("#4\t")==std::string::npos,
          "blocked-only action queue does not invent work for no-action rows");
    auto no_pending=summary;
    no_pending.rows[0].pending_safe=0;
    no_pending.rows[3].pending_semantic=0;
    const auto safe_ledger=audit_workflow_file_actions(
        table,file_sources,file_actions,no_pending);
    check(safe_ledger.raw_only==1 && !safe_ledger.rows[0].blocked &&
          safe_ledger.rows[0].raw_only && safe_ledger.blocked==2,
          "cleared review yields RAW ONLY move, never final host authorization");
    auto unknown_actions=file_actions;
    unknown_actions[1]=static_cast<FileAction>(99);
    rejects([&]{(void)audit_workflow_file_actions(
        table,file_sources,unknown_actions,summary);},
        "unknown action rejects whole ledger");
    auto stale_file_sources=file_sources;
    stale_file_sources.pop_back();
    rejects([&]{(void)audit_workflow_file_actions(
        table,stale_file_sources,file_actions,summary);},
        "truncated physical source batch fails closed");
    auto forged_ledger=ledger;
    forged_ledger.rows[0].reason="INJECT\tFORMULA";
    rejects([&]{(void)format_workflow_file_action_tsv(forged_ledger);},
        "clipboard refuses injected action reason");
    forged_ledger=ledger;forged_ledger.raw_only=1;
    rejects([&]{(void)format_workflow_file_action_tsv(forged_ledger);},
        "clipboard refuses stale aggregate counters");
    check(next_workflow_issue(summary,table,analyses,decisions,0).source_index==3,
          "already accepted semantic proposal is not pending");
    auto navigation=decisions;
    navigation[0][1]={ReviewAction::Pending,{}};
    const auto navigation_audit=audit_workflow_preview(table,analyses,navigation);
    auto next=next_workflow_issue(navigation_audit,table,analyses,navigation,0);
    check(next.kind==WorkflowNextIssueKind::PendingReview &&
          next.source_index==0 && next.proposal_index==1,
          "first semantic REVIEW selected by physical source identity");
    next=next_workflow_issue(navigation_audit,table,analyses,navigation,1);
    check(next.kind==WorkflowNextIssueKind::PendingReview &&
          next.source_index==3 && next.proposal_index==0,
          "next cursor advances in source order regardless of UI sorting");
    auto resolved=navigation;
    resolved[0][1]={ReviewAction::Reject,{}};
    resolved[3][0]={ReviewAction::Reject,{}};
    auto audited=audit_workflow_preview(table,analyses,resolved);
    next=next_workflow_issue(audited,table,analyses,resolved,0);
    check(next.kind==WorkflowNextIssueKind::PendingSafe &&
          next.source_index==0 && next.proposal_index==0,
          "SAFE still requires an explicit decision");
    resolved[0][0]={ReviewAction::Reject,{}};
    audited=audit_workflow_preview(table,analyses,resolved);
    next=next_workflow_issue(audited,table,analyses,resolved,0);
    check(next.kind==WorkflowNextIssueKind::RawRouteConflict &&
          next.source_index==2 && next.issue_code=="DUPLICATE_RAW_TARGET",
          "raw collision is navigable but not an approved FileOps target");
    auto no_collision=table;
    no_collision.rows[2].issues.clear();
    no_collision.rows[2].has_raw_target_collision=false;
    no_collision.raw_collision_items=0;no_collision.unqualified_rows=1;
    audited=audit_workflow_preview(no_collision,analyses,resolved);
    next=next_workflow_issue(audited,no_collision,analyses,resolved,0);
    check(next.kind==WorkflowNextIssueKind::RawRouteBlocker &&
          next.source_index==1 && next.issue_code=="RAW_TARGET_EXISTS",
          "raw existing target is a separate route finding");
    no_collision.rows[1].issues={"FILESYSTEM_TARGET_UNCHECKED"};
    audited=audit_workflow_preview(no_collision,analyses,resolved);
    check(next_workflow_issue(audited,no_collision,analyses,resolved,0).kind==
          WorkflowNextIssueKind::FinalFileOpsUnqualified,
          "global final host check never shown as user-fixable raw path");
    no_collision.rows[1].issues.clear();no_collision.unqualified_rows=0;
    audited=audit_workflow_preview(no_collision,analyses,resolved);
    check(next_workflow_issue(audited,no_collision,analyses,resolved,0).kind==
          WorkflowNextIssueKind::None,"empty queue never approves writes");
    auto broken=resolved;broken[0].pop_back();
    rejects([&]{(void)next_workflow_issue(summary,table,analyses,broken,0);},
            "truncated decision vector rejected");
    auto stale_audit=summary;stale_audit.tracks=3;
    rejects([&]{(void)next_workflow_issue(stale_audit,table,analyses,decisions,0);},
            "stale audit source count rejected");
    next=next_workflow_issue(navigation_audit,table,analyses,navigation,999999);
    check(next.source_index==0 &&
          next.kind==WorkflowNextIssueKind::PendingReview,
          "invalid cursor resets without overflow");
    const auto export_tsv=format_workflow_audit_tsv(table,summary);
    check(export_tsv.find("READ-ONLY batch audit; no write approval")!=std::string::npos &&
          export_tsv.find("#1\t1\t0\t0\t1\t0\tNO\tNO")!=std::string::npos &&
          export_tsv.find("#3\t0\t0\t1\t0\t0\tYES\tYES\tDUPLICATE_RAW_TARGET")!=std::string::npos &&
          export_tsv.find("SENSITIVE_CAPTURED_FINGERPRINT_1234")==std::string::npos,
          "complete TSV includes every row and issue code, no source fingerprint");
    const WorkflowOnlineSourceCounts physical_online{
        0,2,1,1,0,2,"guard:current-dummy-physical-version"};
    const auto joint_export=format_workflow_audit_tsv(
        table,summary,physical_online);
    check(joint_export.find("Verified joint source\t#1")!=std::string::npos &&
          joint_export.find("Local proposal changes")!=std::string::npos &&
          joint_export.find("#1\t1\t0\t0\t1\t0\tNO\tNO\t\t2\t1\t1\t0\tSOURCE_VERIFIED_POSTIMAGE_ONLY")
              !=std::string::npos &&
          joint_export.find("#2\t0\t2\t0\t0\t0\tNO\tYES\tRAW_TARGET_EXISTS;FILESYSTEM_TARGET_UNCHECKED\tN/A")
              !=std::string::npos &&
          joint_export.find("guard:current-dummy") == std::string::npos,
          "merged Discogs+local physical fields are visible ONLY on exact verified row");
    auto forged=physical_online;
    forged.source_index=4;
    rejects([&]{(void)format_workflow_audit_tsv(table,summary,forged);},
        "foreign verified online source row rejected");
    forged=physical_online;forged.verified_source_guard.clear();
    rejects([&]{(void)format_workflow_audit_tsv(table,summary,forged);},
        "missing host physical guard cannot certify online export row");
    forged=physical_online;forged.selected_online_fields=0;
    rejects([&]{(void)format_workflow_audit_tsv(table,summary,forged);},
        "zero selected online fields never has verified online evidence");
    forged=physical_online;forged.selected_online_changed_fields=3;
    rejects([&]{(void)format_workflow_audit_tsv(table,summary,forged);},
        "more changed online fields than selected must fail");
    forged=physical_online;forged.mutually_agreed_fields=3;
    rejects([&]{(void)format_workflow_audit_tsv(table,summary,forged);},
        "unrealistic overlap cannot produce a fabricated field count");
    const auto offline=format_workflow_audit_tsv(table,summary);
    check(offline.find("Verified joint source\tNONE")!=std::string::npos &&
          offline.find("SOURCE_VERIFIED_POSTIMAGE_ONLY")==std::string::npos &&
          offline.find("\tNOT_APPLICABLE")!=std::string::npos,
          "local-only export must never suggest verified Discogs postimage");
    auto injected=table;
    injected.rows[1].issues={"BAD\tFORMULA"};
    rejects([&]{(void)format_workflow_audit_tsv(injected,summary);},
            "export refuses unsafe control/injection in issue code");
    injected=table;
    injected.rows[0].issues={"FILESYSTEM_TARGET_UNCHECKED"};
    rejects([&]{(void)format_workflow_audit_tsv(injected,summary);},
            "export refuses a stale row status instead of forging a report");
    decisions[0][0].action=ReviewAction::Accept;
    check(audit_workflow_preview(table,analyses,decisions).explicit_changed_values==2,
          "pending SAFE must not be counted until explicit Accept");
    decisions[0][0].action=ReviewAction::Pending;
    decisions[3][0]={ReviewAction::ManualValue,"a"};
    check(audit_workflow_preview(table,analyses,decisions).explicit_noop_values==3,
          "manual value equal to original is a no-op");
    decisions[3][0]={ReviewAction::Pending,{}};
    auto bad=decisions;
    bad[0].pop_back();
    rejects([&]{(void)audit_workflow_preview(table,analyses,bad);},
            "missing decisions fail closed");
    bad=decisions;bad[0][0]={ReviewAction::Reject,"unused"};
    rejects([&]{(void)audit_workflow_preview(table,analyses,bad);},
            "unused manual value fails closed");
    bad=decisions;bad[0][0]={ReviewAction::ManualValue,{}};
    rejects([&]{(void)audit_workflow_preview(table,analyses,bad);},
            "empty manual replacement fails closed");
    bad=decisions;bad[0][1]={ReviewAction::ManualValue,"bad\nvalue"};
    rejects([&]{(void)audit_workflow_preview(table,analyses,bad);},
            "manual control characters fail closed");
    auto invalid_accept=analyses;
    invalid_accept[0].proposals[1].proposed_value.clear();
    rejects([&]{(void)audit_workflow_preview(table,invalid_accept,decisions);},
            "implicit deletion is not a valid accepted proposal");
    auto duplicate=analyses;
    duplicate[0].proposals[1].field_index=0;
    rejects([&]{(void)audit_workflow_preview(table,duplicate,decisions);},
            "duplicate field/value targets fail closed");
    auto stale=table;stale.unqualified_rows=0;
    rejects([&]{(void)audit_workflow_preview(stale,analyses,decisions);},
            "stale path counters fail closed");
    stale=table;stale.rows.pop_back();
    rejects([&]{(void)audit_workflow_preview(stale,analyses,decisions);},
            "truncated batch fails closed");
    BatchPreviewTable large;large.rows.resize(500);
    std::vector<AnalysisResult> many(500);
    std::vector<std::vector<ReviewDecision>> choices(500);
    for(std::size_t i=0;i<500;++i) {
        many[i].input_fingerprint="captured";many[i].ruleset_revision="v1";
        many[i].proposals.push_back(proposal(0,SafetyClass::Safe,"  title  ","title"));
        choices[i].push_back({ReviewAction::Pending,{}});
    }
    const auto stress=audit_workflow_preview(large,many,choices);
    check(stress.tracks==500 && stress.pending_safe_values==500 &&
          stress.explicit_changed_values==0 && stress.normalizer_unchanged_tracks==500 &&
          stress.tracks_with_pending==500 && stress.tracks_without_proposals==0,
          "500 pending SAFE rows are not implicit physical writes");
    const auto large_export=format_workflow_audit_tsv(large,stress);
    check(large_export.find("Export scope\tALL\tIncluded rows\t500")!=std::string::npos,
          "full audit declares its exact scope and row count");
    check(large_export.find("#500\t0\t0\t0\t1\t0\tNO\tNO")!=std::string::npos &&
          large_export.find("#1\t")!=std::string::npos &&
          large_export.find("music.mp3")==std::string::npos,
          "all 500 source-order audit rows exported without truncation or paths");
    // Actual DJ library scale, including full-batch and filtered export.
    constexpr std::size_t catalog_size=54129;
    large.rows.resize(catalog_size);
    many.resize(catalog_size);
    choices.resize(catalog_size);
    for(std::size_t i=500;i<catalog_size;++i) {
        many[i].input_fingerprint="sensitive-synthetic-fingerprint";
        many[i].ruleset_revision="v1";
        many[i].proposals.push_back(proposal(0,SafetyClass::Safe," x ","x"));
        choices[i].push_back({ReviewAction::Pending,{}});
    }
    const auto catalog=audit_workflow_preview(large,many,choices);
    // Library-scale FileOps intention ledger: one unresolved action, all
    // remaining sources explicitly skipped, no output truncation or paths.
    std::vector<BatchPreviewInputRow> catalog_sources(catalog_size);
    std::vector<FileAction> catalog_actions(catalog_size,FileAction::None);
    catalog_actions[0]=FileAction::Move;
    catalog_sources[0].physical_source_qualified=true;
    catalog_sources[0].source_physical_key="private-host-identity";
    catalog_sources[0].destination_root=R"(Z:\Music\Singles)";
    catalog_sources[0].raw_relative_path="Test/Artist - Track";
    const auto catalog_ledger=audit_workflow_file_actions(
        large,catalog_sources,catalog_actions,catalog);
    const auto catalog_file_export=format_workflow_file_action_tsv(catalog_ledger);
    const auto catalog_blocked_export=format_workflow_file_action_tsv(
        catalog_ledger,WorkflowAuditExportScope::AttentionRows);
    check(catalog_ledger.blocked==1 && catalog_ledger.skips==catalog_size-1 &&
          catalog_file_export.find("#54129\tNo file action\tSKIP")!=std::string::npos &&
          catalog_file_export.size()<16u*1024u*1024u &&
          catalog_file_export.find("private-host-identity")==std::string::npos &&
          catalog_blocked_export.find("Included rows\t1")!=std::string::npos &&
          catalog_blocked_export.find("#2\t")==std::string::npos,
          "54,129 source file action ledger is complete, private and bounded");
    const auto full_catalog=format_workflow_audit_tsv(large,catalog);
    check(full_catalog.find("Included rows\t54129")!=std::string::npos &&
          full_catalog.find("#54129\t")!=std::string::npos &&
          full_catalog.find("sensitive-synthetic-fingerprint")==std::string::npos &&
          full_catalog.size()<16u*1024u*1024u,
          "54,129 source full audit is complete, bounded and privacy-safe");
    choices[0][0]={ReviewAction::Reject,{}};
    // A universal final FileOps/CUE blocker is not a useful source-specific
    // issue: the focused queue must remain small even when every source
    // is correctly marked globally unqualified.
    large.rows[0].issues={"FILESYSTEM_TARGET_UNCHECKED",
                          "CUE_DEPENDENCIES_UNCHECKED"};
    large.unqualified_rows=1;
    const auto catalog_with_resolved=audit_workflow_preview(large,many,choices);
    const auto attention=format_workflow_audit_tsv(large,catalog_with_resolved,
        std::nullopt,WorkflowAuditExportScope::AttentionRows);
    check(attention.find("Export scope\tATTENTION\tIncluded rows\t54128")!=std::string::npos &&
          attention.find("#1\t")==std::string::npos &&
          attention.find("#2\t")!=std::string::npos &&
          attention.find("#54129\t")!=std::string::npos,
          "attention export omits resolved rows without changing source IDs");
    const WorkflowOnlineSourceCounts online_resolved{
        0,0,0,0,0,1,"guard:synthetic-private"};
    const auto attention_with_online=format_workflow_audit_tsv(
        large,catalog_with_resolved,online_resolved,
        WorkflowAuditExportScope::AttentionRows);
    check(attention_with_online.find("Included rows\t54129")!=std::string::npos &&
          attention_with_online.find("#1\t")!=std::string::npos &&
          attention_with_online.find("guard:synthetic-private")==std::string::npos,
          "verified exact online source remains visible without exporting guard");
    auto hidden_injection=large;
    hidden_injection.rows[0].issues={"BAD\tINJECTION"};
    rejects([&]{(void)format_workflow_audit_tsv(hidden_injection,
        catalog_with_resolved,std::nullopt,WorkflowAuditExportScope::AttentionRows);},
        "attention export rejects unsafe hidden source evidence");
    rejects([&]{(void)format_workflow_audit_tsv(large,catalog_with_resolved,
        std::nullopt,static_cast<WorkflowAuditExportScope>(99));},
        "unknown export scope fails closed");
    auto too_many=stress;too_many.tracks=100001;
    rejects([&]{(void)format_workflow_audit_tsv(large,too_many);},
            "over 100k sources fails explicitly, never silently truncates");
    std::cout<<"PASS: read-only workflow audit 500 rows and negative guards\n";
}
