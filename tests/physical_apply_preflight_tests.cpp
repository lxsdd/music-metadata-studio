#include "djmeta/normalizer_batch_preflight.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <utility>
using namespace djmeta;
static void check(bool ok,const char* msg) {
    if(!ok){std::cerr<<"FAIL: "<<msg<<"\n";std::exit(1);}
}
static PhysicalApplyReprobe sample(const std::string& id,bool edit){
    PhysicalApplyReprobe r;
    r.physical_id="source:"+id;
    r.captured.qualified_physical=true;
    r.captured.host_physical_key="volume-id:"+id;
    r.captured.source_guard="stat-v1:"+id;
    r.captured.original={{{"TITLE",{"Initial "+id}},
       {"GENRE",{"House","Trance"}},{"CUSTOM",{"Preserve"}},
       {"CUESHEET",{"FILE a.flac WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00"}},
       {"REPLAYGAIN_TRACK_GAIN",{"-2.5 dB"}}}};
    r.fresh_tags=r.captured.original;
    r.live_physical_key=r.captured.host_physical_key;
    r.live_source_guard=r.captured.source_guard;
    r.source_reprobed=r.tags_reread=r.target_reprobed=true;
    if(edit)r.approvals.push_back({"TITLE",{"Initial "+id},{"Updated "+id}});
    const auto checked=preview_explicit_physical_tag_values(
        r.captured,r.fresh_tags,r.live_physical_key,r.live_source_guard,
        r.approvals);
    check(checked.eligible,"qualified read-only sample");
    auto& p=r.observed_item;
    p.physical_id=r.physical_id;
    p.source_path="Z:/Music/"+id+".mp3";
    p.source_key="z:/music/"+id+".mp3";
    p.source_guard=r.live_source_guard;
    p.action=FileAction::None;
    p.role=FileRole::Audio;
    p.target_presence=TargetPresence::Unchecked;
    p.cue_links=CueLinkState::NoExternalCue;
    p.ruleset_revision="2026-10-22.1";
    p.metadata_fingerprint=fingerprint(r.captured.original);
    p.planned_metadata_fingerprint=fingerprint(checked.planned);
    return r;
}
static bool refused(const FrozenBatchPreview& f,const BatchApproval& a,
                    const std::vector<PhysicalApplyReprobe>& p){
    const auto x=preflight_exact_physical_batch(f,a,p);
    return x.readiness==PhysicalApplyReadiness::Blocked &&
           !x.rejection.empty() && x.changed_sources==0 &&
           x.postimages.empty();
}
int main(){
    std::vector<PhysicalApplyReprobe> rows={
        sample("A",true),sample("B",false)};
    std::vector<FilePlanItem> plan={
        rows[0].observed_item,rows[1].observed_item};
    const BatchApproval approval{review_batch_plan(plan).plan_fingerprint,false,{}};
    const auto frozen=freeze_reviewed_batch_preview(plan,approval);
    const auto valid=preflight_exact_physical_batch(frozen,approval,rows);
    check(valid.readiness==PhysicalApplyReadiness::DetachedCandidate &&
          valid.changed_sources==1 && valid.postimages.size()==2 &&
          valid.postimages[0].changed && !valid.postimages[1].changed &&
          valid.postimages[0].planned.fields[1]==rows[0].captured.original.fields[1] &&
          valid.postimages[0].planned.fields[2]==rows[0].captured.original.fields[2] &&
          valid.postimages[0].planned.fields[3]==rows[0].captured.original.fields[3] &&
          valid.postimages[0].planned.fields[4]==rows[0].captured.original.fields[4],
          "one selected TITLE only, no implicit CUE/custom/ReplayGain edits");
    auto changed=rows;
    changed[0].source_reprobed=false;
    check(refused(frozen,approval,changed),"old source observation is insufficient");
    changed=rows;changed[1].tags_reread=false;
    check(refused(frozen,approval,changed),"cached tags rejected, entire batch");
    changed=rows;changed[1].target_reprobed=false;
    check(refused(frozen,approval,changed),"target probe not optional");
    changed=rows;changed[0].live_source_guard="raced";
    check(refused(frozen,approval,changed),"version race invalidates full batch");
    changed=rows;changed[0].live_physical_key="hardlink replacement";
    check(refused(frozen,approval,changed),"physical identity switch refused");
    changed=rows;changed[0].captured.virtual_subsong=true;
    check(refused(frozen,approval,changed),"virtual CUE track rejected");
    changed=rows;changed[0].captured.original.fields[2].values={"corrupt"};
    check(refused(frozen,approval,changed),"captured custom field mutation rejected");
    changed=rows;changed[1].fresh_tags.fields[2].values={"concurrent update"};
    check(refused(frozen,approval,changed),"another source changed, atomic refusal");
    changed=rows;changed[0].approvals[0].replacement_values={"wrong edition"};
    check(refused(frozen,approval,changed),"changed planned value cannot inherit consent");
    changed=rows;changed[0].approvals[0].field_name="CUESHEET";
    check(refused(frozen,approval,changed),"CUESHEET must use separate carrier writer");
    changed=rows;changed[0].observed_item.source_guard="changed";
    check(refused(frozen,approval,changed),"changed source plan fingerprint");
    changed=rows;changed[1].observed_item.target_presence=TargetPresence::Missing;
    check(refused(frozen,approval,changed),"target changed from unchecked to missing");
    changed=rows;changed[1].observed_item.ruleset_revision="changed";
    check(refused(frozen,approval,changed),"changed ruleset revision refused");
    changed=rows;std::swap(changed[0],changed[1]);
    check(refused(frozen,approval,changed),"reordered source records refused");
    BatchApproval bad=approval;
    bad.reviewed_plan_fingerprint=std::string(64,'0');
    check(refused(frozen,bad,rows),"stale consent fingerprint cannot be reused");
    auto forged=frozen.decisions;
    forged[0].metadata_write_needed=false;
    const FrozenBatchPreview invalid{frozen.plan_fingerprint,frozen.items,forged};
    check(refused(invalid,approval,rows),"forged frozen status cannot bypass review");

    auto zero=sample("N",false);
    const std::vector<FilePlanItem> no_op_plan{zero.observed_item};
    const BatchApproval zero_approval{review_batch_plan(no_op_plan).plan_fingerprint,false,{}};
    const auto frozen_noop=freeze_reviewed_batch_preview(no_op_plan,zero_approval);
    const auto noop=preflight_exact_physical_batch(frozen_noop,zero_approval,{zero});
    check(noop.readiness==PhysicalApplyReadiness::ExactNoop &&
          noop.changed_sources==0 && noop.postimages.size()==1 &&
          noop.postimages[0].planned==zero.fresh_tags,
          "untouched file never yields write intent");
    auto linked=zero;linked.observed_item.cue_links=CueLinkState::Verified;
    check(refused(frozen_noop,zero_approval,{linked}),
          "external CUE cannot piggyback on tag-only path");
    auto move=sample("M",true);
    move.observed_item.action=FileAction::Move;
    move.observed_item.target_path="Z:/Music/Singles/M.mp3";
    move.observed_item.target_key="z:/music/singles/m.mp3";
    move.observed_item.target_presence=TargetPresence::Missing;
    const std::vector<FilePlanItem> move_plan{move.observed_item};
    const BatchApproval move_approval{review_batch_plan(move_plan).plan_fingerprint,false,{}};
    const auto frozen_move=freeze_reviewed_batch_preview(move_plan,move_approval);
    check(refused(frozen_move,move_approval,{move}),
          "even approved FileOps cannot enter the physical-only writer gate");

    // Fully detached end-to-end batch: explicit normalizer decisions are
    // re-projected and rebound to the same frozen whole-plan fingerprint.
    auto norm_a=sample("NA",false);
    auto norm_b=sample("NB",false);
    norm_a.observed_item.ruleset_revision="rules:norm1";
    norm_b.observed_item.ruleset_revision="rules:norm1";
    const auto make_analysis=[](const PhysicalApplyReprobe& live){
        AnalysisResult a;
        a.input_fingerprint=fingerprint(live.captured.original);
        a.ruleset_revision="rules:norm1";
        Proposal safe;
        safe.field_index=0;
        safe.field=live.captured.original.fields[0].name;
        safe.original_value=live.captured.original.fields[0].values[0];
        safe.proposed_value="Updated "+live.physical_id.substr(7);
        safe.safety=SafetyClass::Safe;
        safe.rule_ids={"test.safe-title"};
        a.proposals.push_back(safe);
        return a;
    };
    NormalizerBatchReprobe decision_a;
    decision_a.live=norm_a;
    decision_a.captured_analysis=make_analysis(norm_a);
    decision_a.active_ruleset_revision="rules:norm1";
    decision_a.decisions={ReviewDecision{ReviewAction::Accept,""}};
    Proposal genre;
    genre.field_index=1;
    genre.field=norm_a.captured.original.fields[1].name;
    genre.value_index=1;
    genre.original_value="Trance";
    genre.proposed_value="Recommended Genre";
    genre.safety=SafetyClass::Review;
    genre.rule_ids={"test.review-genre"};
    decision_a.captured_analysis.proposals.push_back(genre);
    decision_a.decisions.push_back(
        ReviewDecision{ReviewAction::ManualValue,"Melodic Trance"});
    NormalizerBatchReprobe decision_b;
    decision_b.live=norm_b;
    decision_b.captured_analysis=make_analysis(norm_b);
    decision_b.active_ruleset_revision="rules:norm1";
    decision_b.decisions={ReviewDecision{ReviewAction::Pending,""}};
    auto normalizer_rows=std::vector<NormalizerBatchReprobe>{
        decision_a,decision_b};
    for(auto& row:normalizer_rows){
        const auto projected=preview_explicit_normalizer_physical_tags(
            row.live.captured,row.live.fresh_tags,
            row.live.live_physical_key,row.live.live_source_guard,
            row.captured_analysis,row.active_ruleset_revision,row.decisions);
        check(projected.eligible,"normalizer batch fixture qualifies exact choices");
        row.live.observed_item.planned_metadata_fingerprint=
            fingerprint(projected.planned);
    }
    const std::vector<FilePlanItem> norm_plan{
        normalizer_rows[0].live.observed_item,
        normalizer_rows[1].live.observed_item};
    const BatchApproval norm_approval{
        review_batch_plan(norm_plan).plan_fingerprint,false,{}};
    const auto norm_frozen=freeze_reviewed_batch_preview(
        norm_plan,norm_approval);
    const auto norm_post=preflight_explicit_normalizer_physical_batch(
        norm_frozen,norm_approval,normalizer_rows);
    check(norm_post.readiness==PhysicalApplyReadiness::DetachedCandidate &&
          norm_post.changed_sources==1 &&
          norm_post.postimages.size()==2 &&
          norm_post.postimages[0].eligible &&
          norm_post.postimages[0].planned.fields[0].values==
              std::vector<std::string>{"Updated NA"} &&
          norm_post.postimages[0].planned.fields[1].values==
              std::vector<std::string>({"House","Melodic Trance"}) &&
          norm_post.postimages[0].planned.fields[2]==
              norm_a.captured.original.fields[2] &&
          norm_post.postimages[0].planned.fields[3]==
              norm_a.captured.original.fields[3] &&
          norm_post.postimages[0].planned.fields[4]==
              norm_a.captured.original.fields[4] &&
          !norm_post.postimages[1].changed,
          "accepted SAFE + manual multivalue, pending B, CUE/ReplayGain unchanged");
    const auto normalizer_refuses=[&](
        const std::vector<NormalizerBatchReprobe>& attempt,
        const BatchApproval* alternate_consent=nullptr){
        const auto& consent=alternate_consent
            ? *alternate_consent : norm_approval;
        const auto candidate=preflight_explicit_normalizer_physical_batch(
            norm_frozen,consent,attempt);
        return candidate.readiness==PhysicalApplyReadiness::Blocked &&
               !candidate.rejection.empty() &&
               candidate.postimages.empty() &&
               candidate.changed_sources==0;
    };
    auto altered_normalizer=normalizer_rows;
    altered_normalizer[0].decisions[0].action=ReviewAction::Pending;
    check(normalizer_refuses(altered_normalizer),
          "pending SAFE cannot reuse frozen selected-write consent");
    altered_normalizer=normalizer_rows;
    altered_normalizer[1].decisions[0].action=ReviewAction::Accept;
    check(normalizer_refuses(altered_normalizer),
          "new selection for B invalidates entire previously reviewed batch");
    altered_normalizer=normalizer_rows;
    altered_normalizer[0].decisions[1].manual_value="Different Genre";
    check(normalizer_refuses(altered_normalizer),
          "replaced manual value cannot inherit unchanged batch approval");
    altered_normalizer=normalizer_rows;
    altered_normalizer[0].decisions.pop_back();
    check(normalizer_refuses(altered_normalizer),
          "partial normalizer decision vector invalidates all sources");
    altered_normalizer=normalizer_rows;
    altered_normalizer[0].active_ruleset_revision="rules:norm2";
    check(normalizer_refuses(altered_normalizer),
          "changed active rule revision invalidates normalizer batch");
    altered_normalizer=normalizer_rows;
    altered_normalizer[0].captured_analysis.proposals[0].original_value=
        "forged source";
    check(normalizer_refuses(altered_normalizer),
          "forged original proposal value cannot inherit whole-plan consent");
    altered_normalizer=normalizer_rows;
    altered_normalizer[0].live.approvals.push_back(
        {"TITLE",{"Initial NA"},{"unreviewed injection"}});
    check(normalizer_refuses(altered_normalizer),
          "second field-approval source cannot bypass normalizer selections");
    altered_normalizer=normalizer_rows;
    altered_normalizer[0].live.fresh_tags.fields[2].values={
        "modified in parallel"};
    check(normalizer_refuses(altered_normalizer),
          "external custom-field edit invalidates whole batch atomically");
    altered_normalizer=normalizer_rows;
    altered_normalizer[1].live.tags_reread=false;
    check(normalizer_refuses(altered_normalizer),
          "every selected source needs fresh metadata even if it is a no-op");
    altered_normalizer=normalizer_rows;
    altered_normalizer[0].live.captured.virtual_subsong=true;
    check(normalizer_refuses(altered_normalizer),
          "virtual CUE source cannot use physical normalizer batch path");
    altered_normalizer=normalizer_rows;
    altered_normalizer[0].captured_analysis.proposals[1].field_index=3;
    altered_normalizer[0].captured_analysis.proposals[1].field="CUESHEET";
    altered_normalizer[0].captured_analysis.proposals[1].value_index=0;
    altered_normalizer[0].captured_analysis.proposals[1].original_value=
        norm_a.captured.original.fields[3].values[0];
    check(normalizer_refuses(altered_normalizer),
          "protected CUESHEET cannot become normalizer write via batch");
    auto stale_consent=norm_approval;
    stale_consent.reviewed_plan_fingerprint="old reviewed plan";
    check(normalizer_refuses(normalizer_rows,&stale_consent),
          "old whole-plan approval cannot authorize new normalizer batch");

    // Two untouched sources still yield no physical or timestamp writes.
    auto only_pending=normalizer_rows;
    for(auto& row:only_pending){
        for(auto& decision:row.decisions)
            decision={ReviewAction::Pending,""};
        row.live.observed_item.planned_metadata_fingerprint=
            fingerprint(row.live.captured.original);
    }
    const std::vector<FilePlanItem> pending_plan{
        only_pending[0].live.observed_item,
        only_pending[1].live.observed_item};
    const BatchApproval pending_approval{
        review_batch_plan(pending_plan).plan_fingerprint,false,{}};
    const auto pending_frozen=freeze_reviewed_batch_preview(
        pending_plan,pending_approval);
    const auto pending_post=preflight_explicit_normalizer_physical_batch(
        pending_frozen,pending_approval,only_pending);
    check(pending_post.readiness==PhysicalApplyReadiness::ExactNoop &&
          pending_post.changed_sources==0 &&
          pending_post.postimages.size()==2 &&
          !pending_post.postimages[0].changed &&
          !pending_post.postimages[1].changed,
          "pending SAFE/REVIEW across all sources is a zero-write batch");
    std::cout<<"PASS: atomic physical batch reprobe and exact no-op guards\n";
}
