#include "djmeta/workflow_preview_audit.h"
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
    for(auto& a:analyses) {a.input_fingerprint="source";a.ruleset_revision="rules";}
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
    const auto export_tsv=format_workflow_audit_tsv(table,summary);
    check(export_tsv.find("READ-ONLY batch audit; no write approval")!=std::string::npos &&
          export_tsv.find("#1\t1\t0\t0\t1\t0\tNO\tNO")!=std::string::npos &&
          export_tsv.find("#3\t0\t0\t1\t0\t0\tYES\tYES\tDUPLICATE_RAW_TARGET")!=std::string::npos &&
          export_tsv.find("source")==std::string::npos,
          "complete TSV includes every row and issue code, no source fingerprint");
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
    check(large_export.find("#500\t0\t0\t0\t1\t0\tNO\tNO")!=std::string::npos &&
          large_export.find("#1\t")!=std::string::npos &&
          large_export.find("music.mp3")==std::string::npos,
          "all 500 source-order audit rows exported without truncation or paths");
    auto too_many=stress;too_many.tracks=5001;
    rejects([&]{(void)format_workflow_audit_tsv(large,too_many);},
            "oversized export fails explicitly, never silently truncates");
    std::cout<<"PASS: read-only workflow audit 500 rows and negative guards\n";
}
