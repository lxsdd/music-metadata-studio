#pragma once
// Exact external-CUE value-only preflight. No SDK, disk I/O or writer.
// All fresh bytes/identities must be supplied by a later host adapter.
#include "djmeta/cue_value_batch_adapter.h"
#include <string>
#include <string_view>
namespace djmeta::online {
enum class CueApplyReadiness { Blocked, ExactNoop, DetachedCandidate };
struct CueApplyPreflight {
    CueApplyReadiness readiness=CueApplyReadiness::Blocked;
    std::string rejection;
    djmeta::CueValuePostimage postimage;
};
inline CueApplyPreflight preflight_exact_external_cue_values(
    const djmeta::FrozenBatchPreview& frozen,
    const djmeta::BatchApproval& approval,
    const djmeta::FilePlanItem& live_item,
    std::string_view current_bytes,
    const ReleaseEdition& verified_edition,
    const std::vector<std::size_t>& selected_indices,
    bool source_reprobed, bool bytes_reread, bool dependencies_reprobed,
    bool target_reprobed) {
    const auto refuse=[](const char* why) {
        CueApplyPreflight result;result.rejection=why;return result;
    };
    if(!source_reprobed || !bytes_reread || !dependencies_reprobed ||
       !target_reprobed)
        return refuse("CUE_APPLY_FRESH_HOST_PROBES_REQUIRED");
    if(frozen.items.size()!=1 || frozen.decisions.size()!=1 ||
       frozen.plan_fingerprint.empty() ||
       approval.reviewed_plan_fingerprint!=frozen.plan_fingerprint)
        return refuse("CUE_APPLY_APPROVAL_OR_BATCH_UNQUALIFIED");
    const auto old=djmeta::review_batch_plan(frozen.items,&approval);
    if(!old.ready_to_apply || old.plan_fingerprint!=frozen.plan_fingerprint ||
       old.decisions.size()!=1 ||
       old.decisions[0].status!=frozen.decisions[0].status ||
       old.decisions[0].reasons!=frozen.decisions[0].reasons ||
       old.decisions[0].cue_bytes_write_needed!=
           frozen.decisions[0].cue_bytes_write_needed ||
       old.decisions[0].metadata_write_needed!=
           frozen.decisions[0].metadata_write_needed ||
       old.decisions[0].file_operation_needed!=
           frozen.decisions[0].file_operation_needed ||
       old.decisions[0].will_replace_existing_target!=
           frozen.decisions[0].will_replace_existing_target)
        return refuse("CUE_APPLY_FROZEN_DECISION_CHANGED");
    const auto fresh=djmeta::review_batch_plan({live_item},&approval);
    if(!fresh.ready_to_apply ||
       fresh.plan_fingerprint!=frozen.plan_fingerprint)
        return refuse("CUE_APPLY_FRESH_PLAN_CHANGED");
    const auto& item=frozen.items.front();
    if(item.role!=djmeta::FileRole::ExternalCue ||
       item.action!=djmeta::FileAction::None ||
       item.cue_links!=djmeta::CueLinkState::Verified ||
       item.cue_references_will_change ||
       item.source_guard.empty() || item.physical_id.empty() ||
       item.cue_source_fingerprint.empty() ||
       item.cue_postimage_fingerprint.empty() ||
       item.source_key.empty() || item.source_path.empty() ||
       (!item.associated_audio_ids.empty() &&
        !item.associated_audio_id.empty()) ||
       (item.associated_audio_id.empty() &&
        item.associated_audio_ids.empty()) ||
       !item.target_path.empty() || !item.target_key.empty() ||
       old.decisions[0].file_operation_needed ||
       old.decisions[0].metadata_write_needed ||
       old.decisions[0].will_replace_existing_target)
        return refuse("CUE_APPLY_SEPARATE_WRITER_REQUIRED");
    if(cue_carrier_content_fingerprint(current_bytes)!=
       item.cue_source_fingerprint)
        return refuse("CUE_APPLY_LIVE_BYTES_CHANGED");
    // Recompute edition/track provenance and the exact user-selected bytes.
    auto unstaged=item;
    unstaged.cue_values_will_change=false; // explicit preflight restaging only
    const auto projected=stage_exact_external_cue_values_for_batch(
        unstaged,current_bytes,verified_edition,selected_indices);
    if(!projected.eligible ||
       projected.planned.cue_source_fingerprint!=
           item.cue_source_fingerprint ||
       projected.planned.cue_postimage_fingerprint!=
           item.cue_postimage_fingerprint ||
       projected.planned.cue_values_will_change!=
           item.cue_values_will_change ||
       projected.cue.changed!=old.decisions[0].cue_bytes_write_needed)
        return refuse("CUE_APPLY_SELECTED_POSTIMAGE_CHANGED");
    CueApplyPreflight result;
    result.postimage=projected.cue;
    result.readiness=projected.cue.changed
        ? CueApplyReadiness::DetachedCandidate
        : CueApplyReadiness::ExactNoop;
    return result;
}
} // namespace djmeta::online
