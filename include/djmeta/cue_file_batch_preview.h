#pragma once
// Exact external CUE to FilePlan coupling. This is an immutable read-only
// preview; no host handles, filesystem calls, freeze token or write authority.
#include "djmeta/batch_plan.h"
#include "djmeta/cue_association.h"
#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {

struct CueBoundBatchPreview {
    bool eligible=false; // planning only; NOT FileOps approval
    std::string rejection;
    std::vector<FilePlanItem> items;
    CueAssociationPlan association;
    BatchPlanReview review;
};

// The native host supplies independently verified CUE FILE relationships,
// full physical source guards and FINAL (not raw) destination keys. The core
// first reconstructs the byte-exact CUE FILE postimage from that evidence,
// then binds exactly the selected physical audio participants to the existing
// source/target FilePlan items. No guessed paths or CUE writer.
inline CueBoundBatchPreview preview_cue_bound_file_batch(
    const std::vector<FilePlanItem>& captured,
    std::size_t cue_index,
    std::string_view original_cue_bytes,
    const std::vector<CueHostReference>& references,
    const std::vector<CueSelectedAudio>& selected_audios) {
    const auto reject=[](const char* code) {
        CueBoundBatchPreview out;
        out.rejection=code;
        return out;
    };
    if(captured.empty() || captured.size()>100000 ||
       cue_index>=captured.size() || selected_audios.empty() ||
       selected_audios.size()>captured.size() || references.size()>10000)
        return reject("CUE_BATCH_SHAPE_OR_SELECTION_INVALID");
    const auto& cue=captured[cue_index];
    if(cue.role!=FileRole::ExternalCue ||
       cue.cue_links!=CueLinkState::Verified ||
       cue.physical_id.empty() || cue.source_key.empty() ||
       cue.source_guard.empty() || cue.ruleset_revision.empty() ||
       cue.cue_values_will_change)
        return reject("CUE_BATCH_CARRIER_OR_VALUES_NOT_QUALIFIED");

    CueBoundBatchPreview out;
    out.association=qualify_external_cue_association(
        original_cue_bytes,references,selected_audios);
    if(!out.association.qualified) {
        out.rejection=out.association.diagnostics.empty()
            ? "CUE_BATCH_HOST_RELATION_INVALID"
            : out.association.diagnostics.front();
        return out;
    }
    if((!cue.cue_source_fingerprint.empty() &&
        cue.cue_source_fingerprint!=out.association.original_cue_fingerprint) ||
       (!cue.cue_postimage_fingerprint.empty() &&
        cue.cue_postimage_fingerprint!=out.association.proposed_cue_fingerprint) ||
       (cue.cue_references_will_change &&
        !out.association.changes_references))
        return reject("CUE_BATCH_PREVIOUS_CUE_STAGING_STALE");

    std::map<std::string,const FilePlanItem*> audio_by_id;
    for(const auto& row:captured) {
        if(row.role==FileRole::Audio &&
           !row.physical_id.empty() &&
           !audio_by_id.emplace(row.physical_id,&row).second)
            return reject("CUE_BATCH_DUPLICATE_PHYSICAL_AUDIO");
    }
    for(const auto& selected:selected_audios) {
        const auto found=audio_by_id.find(selected.physical_id);
        if(found==audio_by_id.end() ||
           found->second->source_key!=selected.source_key ||
           found->second->target_key!=selected.target_key ||
           found->second->source_guard.empty() ||
           found->second->cue_links!=CueLinkState::Verified)
            return reject("CUE_BATCH_AUDIO_TARGET_OR_GUARD_MISMATCH");
    }
    const auto& expected=out.association.related_selected_audio_ids;
    if((!cue.associated_audio_ids.empty() &&
        cue.associated_audio_ids!=expected) ||
       (!cue.associated_audio_id.empty() &&
        !std::binary_search(expected.begin(),expected.end(),
                            cue.associated_audio_id)))
        return reject("CUE_BATCH_PREVIOUS_ASSOCIATION_CHANGED");

    out.items=captured;
    auto& staged=out.items[cue_index];
    staged.associated_audio_ids=expected;
    staged.cue_source_fingerprint=out.association.original_cue_fingerprint;
    staged.cue_postimage_fingerprint=out.association.proposed_cue_fingerprint;
    staged.cue_references_will_change=out.association.changes_references;
    out.review=review_batch_plan(out.items);
    if(out.review.blocked || out.review.decisions.size()!=captured.size()) {
        out.rejection="CUE_BATCH_CROSS_SOURCE_OR_TARGET_CONFLICT";
        out.items.clear();
        return out;
    }
    // An observed EXISTING target may still need explicit overwrite review.
    // 'eligible' means ONLY a consistent unapproved candidate is available.
    out.eligible=true;
    return out;
}

} // namespace djmeta
