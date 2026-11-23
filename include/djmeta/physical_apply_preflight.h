#pragma once
// All-source physical tag transaction preflight. Pure, no SDK, disk, or writer.
// This verifies a preview; it NEVER grants permission to commit metadata.
#include "djmeta/batch_plan.h"
#include "djmeta/physical_tag_postimage.h"
#include <cstddef>
#include <string>
#include <vector>
namespace djmeta {
struct PhysicalApplyReprobe {
    FilePlanItem observed_item;
    std::string physical_id;
    PhysicalTagSourceSnapshot captured;
    MetadataDocument fresh_tags;
    std::string live_physical_key, live_source_guard;
    std::vector<PhysicalTagValueApproval> approvals;
    bool source_reprobed=false, tags_reread=false, target_reprobed=false;
};
enum class PhysicalApplyReadiness {Blocked, ExactNoop, DetachedCandidate};
struct PhysicalApplyPreflight {
    PhysicalApplyReadiness readiness=PhysicalApplyReadiness::Blocked;
    std::string rejection;
    std::vector<PhysicalTagPostimage> postimages;
    std::size_t changed_sources=0;
};
inline PhysicalApplyPreflight preflight_exact_physical_batch(
    const FrozenBatchPreview& frozen, const BatchApproval& approval,
    const std::vector<PhysicalApplyReprobe>& reprobes) {
    const auto fail=[](const char* why){
        PhysicalApplyPreflight result;result.rejection=why;return result;
    };
    if(frozen.items.empty() || frozen.items.size()>100000 ||
       frozen.items.size()!=frozen.decisions.size() ||
       frozen.items.size()!=reprobes.size() ||
       approval.reviewed_plan_fingerprint.empty() ||
       approval.reviewed_plan_fingerprint!=frozen.plan_fingerprint)
        return fail("PHYSICAL_BATCH_FROZEN_SHAPE_INVALID");
    const auto old=review_batch_plan(frozen.items,&approval);
    if(!old.ready_to_apply || old.plan_fingerprint!=frozen.plan_fingerprint ||
       old.decisions.size()!=frozen.decisions.size())
        return fail("PHYSICAL_BATCH_APPROVAL_STALE");
    for(std::size_t i=0;i<frozen.items.size();++i) {
        const auto& a=old.decisions[i];const auto& b=frozen.decisions[i];
        if(a.status!=b.status || a.reasons!=b.reasons ||
           a.metadata_write_needed!=b.metadata_write_needed ||
           a.cue_bytes_write_needed!=b.cue_bytes_write_needed ||
           a.file_operation_needed!=b.file_operation_needed ||
           a.will_replace_existing_target!=b.will_replace_existing_target)
            return fail("PHYSICAL_BATCH_FORGED_FROZEN_DECISION");
    }
    std::vector<FilePlanItem> observed;
    observed.reserve(reprobes.size());
    for(const auto& row:reprobes){
        if(!row.source_reprobed || !row.tags_reread || !row.target_reprobed)
            return fail("PHYSICAL_BATCH_FRESH_HOST_READ_REQUIRED");
        observed.push_back(row.observed_item);
    }
    const auto fresh=review_batch_plan(observed,&approval);
    if(!fresh.ready_to_apply ||
       fresh.plan_fingerprint!=frozen.plan_fingerprint ||
       fresh.decisions.size()!=old.decisions.size())
        return fail("PHYSICAL_BATCH_FRESH_OBSERVATION_CHANGED");
    PhysicalApplyPreflight out;
    out.postimages.reserve(reprobes.size());
    for(std::size_t i=0;i<reprobes.size();++i){
        const auto& item=frozen.items[i];
        const auto& live=reprobes[i];
        const auto& decision=old.decisions[i];
        // FileOps, sidecars, external CUE and virtual carriers require their
        // OWN SDK-qualified writers and atomic commit/rollback contract.
        if(item.role!=FileRole::Audio || item.action!=FileAction::None ||
           item.cue_links!=CueLinkState::NoExternalCue ||
           !item.associated_audio_id.empty() ||
           !item.associated_audio_ids.empty() ||
           item.cue_references_will_change || decision.file_operation_needed ||
           decision.cue_bytes_write_needed || decision.will_replace_existing_target ||
           !item.target_path.empty() || !item.target_key.empty())
            return fail("PHYSICAL_BATCH_SEPARATE_WRITER_REQUIRED");
        if(live.physical_id!=item.physical_id ||
           live.captured.source_guard!=item.source_guard ||
           live.live_source_guard!=item.source_guard ||
           live.live_physical_key.empty() ||
           live.captured.host_physical_key!=live.live_physical_key)
            return fail("PHYSICAL_BATCH_SOURCE_IDENTITY_STALE");
        if(fingerprint(live.captured.original)!=item.metadata_fingerprint)
            return fail("PHYSICAL_BATCH_ORIGINAL_TAGS_STALE");
        const auto post=preview_explicit_physical_tag_values(
            live.captured,live.fresh_tags,live.live_physical_key,
            live.live_source_guard,live.approvals);
        if(!post.eligible) return fail("PHYSICAL_BATCH_POSTIMAGE_REJECTED");
        if(post.changed!=decision.metadata_write_needed ||
           fingerprint(post.planned)!=item.planned_metadata_fingerprint)
            return fail("PHYSICAL_BATCH_POSTIMAGE_FINGERPRINT_CHANGED");
        if(post.changed) ++out.changed_sources;
        out.postimages.push_back(post);
    }
    out.readiness=out.changed_sources
        ? PhysicalApplyReadiness::DetachedCandidate
        : PhysicalApplyReadiness::ExactNoop;
    return out;
}
} // namespace djmeta
