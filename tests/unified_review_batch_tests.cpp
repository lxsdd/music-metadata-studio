#include "djmeta/unified_review_batch_preflight.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace djmeta;
using namespace djmeta::online;
static void check(bool pass,const char* reason) {
    if(!pass) {std::cerr<<"FAIL: "<<reason<<"\n";std::exit(1);}
}

static UnifiedReviewBatchReprobe fixture(std::string id,bool accept_local,
                                        bool accept_online) {
    UnifiedReviewBatchReprobe row;
    auto& n=row.normalizer;
    auto& live=n.live;
    live.physical_id="source:"+id;
    live.captured={true,false,"disk:"+id,"guard:"+id,{{{
        "TITLE",{" Old "+id+" "}},{"GENRE",{"House","Trance"}},
        {"CUSTOM DJ",{"K1","K2"}},{"DATE",{"1998"}},
        {"CUESHEET",{"FILE mix.flac WAVE\nTRACK 01 AUDIO"}},
        {"REPLAYGAIN_TRACK_GAIN",{"-3 dB"}}
    }}};
    live.fresh_tags=live.captured.original;
    live.live_physical_key=live.captured.host_physical_key;
    live.live_source_guard=live.captured.source_guard;
    live.source_reprobed=live.tags_reread=live.target_reprobed=true;
    auto& item=live.observed_item;
    item.physical_id=live.physical_id;
    item.source_path="Z:/library/"+id+".flac";
    item.source_key="z:/library/"+id+".flac";
    item.source_guard=live.live_source_guard;
    item.role=FileRole::Audio;
    item.action=FileAction::None;
    item.cue_links=CueLinkState::NoExternalCue;
    item.ruleset_revision="rules:unified-v1";
    item.metadata_fingerprint=fingerprint(live.captured.original);
    item.planned_metadata_fingerprint=item.metadata_fingerprint;
    n.active_ruleset_revision=item.ruleset_revision;
    n.captured_analysis.ruleset_revision=item.ruleset_revision;
    n.captured_analysis.input_fingerprint=item.metadata_fingerprint;
    Proposal proposal;
    proposal.field_index=0;
    proposal.field="TITLE";
    proposal.original_value=" Old "+id+" ";
    proposal.proposed_value="Old "+id;
    proposal.safety=SafetyClass::Safe;
    proposal.rule_ids={"fixture.local.trim"};
    n.captured_analysis.proposals.push_back(proposal);
    n.decisions={ReviewDecision{accept_local ? ReviewAction::Accept
                                             : ReviewAction::Pending,""}};
    const std::vector<FieldEvidence> evidence{
        {"GENRE",{"House","Melodic Trance"},"discogs","release:42",
         EvidenceScope::Edition},
        {"DATE",{"2026-10-25"},"discogs","release:42",
         EvidenceScope::Edition,DateMeaning::EditionRelease},
        {"TITLE",{"Online title "+id},"discogs","release:42",
         EvidenceScope::Edition}
    };
    row.online_capture=capture_online_field_review(
        live.captured.original,evidence,true,true);
    if(accept_online) {
        row.selected_online_indices={0,1};
        row.explicit_date_targets={{1,"RELEASEDATE"}};
    }
    return row;
}
static bool rejected(const FrozenBatchPreview& frozen,
                     const BatchApproval& approval,
                     const std::vector<UnifiedReviewBatchReprobe>& rows) {
    const auto result=preflight_unified_review_physical_batch(
        frozen,approval,rows);
    return result.readiness==PhysicalApplyReadiness::Blocked &&
           !result.rejection.empty() && result.postimages.empty() &&
           result.changed_sources==0;
}

int main() {
    auto a=fixture("A",true,true);
    auto b=fixture("B",false,false);
    b.online_capture.reset(); // normalizer-only row, no fabricated Discogs evidence
    const auto combined=preview_unified_review_physical_tags(a);
    check(combined.physical.eligible && combined.physical.changed &&
          combined.normalizer_changed_fields==1 &&
          combined.online_changed_fields==2 &&
          combined.mutually_agreed_fields==0 &&
          combined.physical.explicitly_changed==3 &&
          combined.physical.planned.fields.size()==7,
          "three explicit local/online selections combine without implicit consent");
    check(combined.physical.planned.fields[0].values==
              std::vector<std::string>{"Old A"} &&
          combined.physical.planned.fields[1].values==
              std::vector<std::string>({"House","Melodic Trance"}) &&
          combined.physical.planned.fields[2]==a.normalizer.live.captured.original.fields[2] &&
          combined.physical.planned.fields[3]==a.normalizer.live.captured.original.fields[3] &&
          combined.physical.planned.fields[4]==a.normalizer.live.captured.original.fields[4] &&
          combined.physical.planned.fields[5]==a.normalizer.live.captured.original.fields[5] &&
          combined.physical.planned.fields[6]==MetadataField{
              "RELEASEDATE",{"2026-10-25"}},
          "single postimage retains original multivalues, year, CUE and ReplayGain");
    const auto untouched=preview_unified_review_physical_tags(b);
    check(untouched.physical.eligible && !untouched.physical.changed &&
          untouched.physical.planned==b.normalizer.live.fresh_tags &&
          untouched.normalizer_changed_fields==0 &&
          untouched.online_changed_fields==0,
          "pending SAFE and absent online source remain exact no-ops");
    auto only_local=fixture("local",true,false);
    only_local.online_capture.reset();
    const auto local_only_post=preview_unified_review_physical_tags(only_local);
    check(local_only_post.physical.eligible &&
          local_only_post.normalizer_changed_fields==1 &&
          local_only_post.online_changed_fields==0 &&
          local_only_post.physical.explicitly_changed==1 &&
          local_only_post.physical.planned.fields.size()==6,
          "local Accept requires no synthetic provider evidence");
    auto forged_selection=only_local;
    forged_selection.selected_online_indices={0};
    check(!preview_unified_review_physical_tags(forged_selection).physical.eligible &&
          preview_unified_review_physical_tags(forged_selection).rejection==
              "UNIFIED_ONLINE_SELECTION_WITHOUT_EVIDENCE",
          "online index without verified snapshot fails closed");
    forged_selection=only_local;
    forged_selection.explicit_date_targets={{1,"DATE"}};
    check(!preview_unified_review_physical_tags(forged_selection).physical.eligible,
          "orphan provider date targets cannot bypass absent snapshot");
    forged_selection=only_local;
    forged_selection.normalizer.live.fresh_tags.fields[2].values={"raced"};
    check(!preview_unified_review_physical_tags(forged_selection).physical.eligible,
          "local-only row still rejects changed unrelated physical tags");
    const auto local_batch=preview_unified_review_batch_plan({only_local});
    check(local_batch.eligible && local_batch.items.size()==1 &&
          local_batch.postimages[0].physical.explicitly_changed==1,
          "read-only batch accepts local-only explicit edits without Discogs");

    std::vector<UnifiedReviewBatchReprobe> batch{a,b};
    const auto preview=preview_unified_review_batch_plan(batch);
    check(preview.eligible && preview.items.size()==2 &&
          preview.review.blocked==0 &&
          preview.items[0].planned_metadata_fingerprint==
              fingerprint(combined.physical.planned) &&
          preview.items[1].planned_metadata_fingerprint==
              preview.items[1].metadata_fingerprint,
          "whole batch binds mixed postimages and unchanged rows to one plan");
    for(std::size_t i=0;i<batch.size();++i)
        batch[i].normalizer.live.observed_item=preview.items[i];
    const BatchApproval approval{preview.review.plan_fingerprint,false,{}};
    const auto frozen=freeze_reviewed_batch_preview(preview.items,approval);
    const auto final=preflight_unified_review_physical_batch(
        frozen,approval,batch);
    check(final.readiness==PhysicalApplyReadiness::DetachedCandidate &&
          final.changed_sources==1 && final.postimages.size()==2 &&
          final.postimages[0].planned==combined.physical.planned &&
          !final.postimages[1].changed,
          "exact frozen all-source check reproduces selected normalizer and Discogs");
    auto changed=batch;
    changed[0].normalizer.live.fresh_tags.fields[2].values={"raced"};
    check(rejected(frozen,approval,changed),"changed unrelated DJ multivalue blocks batch");
    changed=batch;
    changed[1].normalizer.live.source_reprobed=false;
    check(rejected(frozen,approval,changed),"second no-op source still needs fresh observation");
    changed=batch;
    changed[1].selected_online_indices={0};
    check(rejected(frozen,approval,changed),
          "local-only frozen row cannot acquire a provider selection");
    changed=batch;
    changed[1].online_capture=fixture("B",false,false).online_capture;
    changed[1].selected_online_indices={0};
    check(rejected(frozen,approval,changed),
          "adding provider evidence AND choice cannot reuse original approval");
    changed=batch;
    changed[0].normalizer.active_ruleset_revision="rules:v2";
    check(rejected(frozen,approval,changed),"changed ruleset invalidates whole frozen batch");
    changed=batch;
    changed[0].explicit_date_targets[1]="DATE";
    check(rejected(frozen,approval,changed),"different date target cannot reuse approved plan");
    changed=batch;
    changed[0].online_capture->evidence[0].values={"Forged online genre"};
    check(rejected(frozen,approval,changed),"tampered online provenance rejected");
    changed=batch;
    changed[0].normalizer.decisions[0]={ReviewAction::Pending,""};
    check(rejected(frozen,approval,changed),"rejected local decision changes shared fingerprint");
    changed=batch;
    changed[1].selected_online_indices={0};
    check(rejected(frozen,approval,changed),"new online selection on noop row is stale");
    changed=batch;
    changed[0].normalizer.live.approvals={
        {"CUSTOM DJ",{"K1","K2"},{"Injected"}}};
    check(rejected(frozen,approval,changed),"second physical approval channel never accepted");
    changed=batch;
    changed[0].normalizer.live.captured.virtual_subsong=true;
    check(rejected(frozen,approval,changed),"virtual cue subsong cannot stage physical tags");
    changed=batch;
    changed[0].normalizer.live.observed_item.action=FileAction::Move;
    check(rejected(frozen,approval,changed),"FileOps cannot piggyback on tag-only frozen gate");

    auto conflicting=fixture("C",true,true);
    conflicting.selected_online_indices={0,2};
    conflicting.explicit_date_targets.clear();
    const auto collision=preview_unified_review_physical_tags(conflicting);
    check(!collision.physical.eligible &&
          collision.rejection=="UNIFIED_FIELD_CONFLICT_REQUIRES_EXPLICIT_RESOLUTION" &&
          collision.conflicting_fields==std::vector<std::string>{"title"},
          "opposing accepted local versus online TITLE is an explicit field conflict");
    check(!preview_unified_review_batch_plan({conflicting}).eligible,
          "one field conflict fails the whole batch, never picks a provider by order");
    conflicting.online_capture->evidence[2].values={"Old C"};
    conflicting.online_capture->comparisons=review_online_fields(
        conflicting.online_capture->physical_before,
        conflicting.online_capture->evidence);
    const auto agreeing=preview_unified_review_physical_tags(conflicting);
    check(agreeing.physical.eligible &&
          agreeing.normalizer_changed_fields==1 &&
          agreeing.online_changed_fields==2 &&
          agreeing.mutually_agreed_fields==1 &&
          agreeing.physical.explicitly_changed==2,
          "byte-identical local/provider TITLE choices coalesce to one write intent");

    auto absent=fixture("D",false,false);
    absent.online_capture->physical_before.fields[2].values={"forged"};
    check(!preview_unified_review_physical_tags(absent).physical.eligible,
          "online snapshot must be bound to same immutable original tags");
    absent=fixture("D",false,false);
    absent.selected_online_indices={1};
    check(!preview_unified_review_physical_tags(absent).physical.eligible,
          "date requires explicit field-target decision");
    absent=fixture("D",false,false);
    absent.selected_online_indices={2};
    absent.normalizer.live.captured.source_guard="changed source";
    check(!preview_unified_review_physical_tags(absent).physical.eligible,
          "live physical source guards are mandatory for both branches");

    auto no_op=fixture("N",false,false);
    const auto no_op_preview=preview_unified_review_batch_plan({no_op});
    check(no_op_preview.eligible,"pure pending batch qualifies for no-op plan");
    no_op.normalizer.live.observed_item=no_op_preview.items[0];
    const BatchApproval no_op_approval{
        no_op_preview.review.plan_fingerprint,false,{}};
    const auto no_op_frozen=freeze_reviewed_batch_preview(
        no_op_preview.items,no_op_approval);
    const auto no_op_final=preflight_unified_review_physical_batch(
        no_op_frozen,no_op_approval,{no_op});
    check(no_op_final.readiness==PhysicalApplyReadiness::ExactNoop &&
          no_op_final.changed_sources==0,
          "all-unapproved batch produces zero writes and exact no-op");
    std::cout<<"PASS: unified local and source-aware online selections -> "
                "one safe detached batch postimage and final preflight\n";
}
