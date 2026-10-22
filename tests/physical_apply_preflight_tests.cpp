#include "djmeta/physical_apply_preflight.h"
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
    std::cout<<"PASS: atomic physical batch reprobe and exact no-op guards\n";
}
