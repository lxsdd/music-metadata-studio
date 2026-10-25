#pragma once
// One detached, read-only local+online physical audio / external CUE FILE
// reference / qualified host FilePlan preflight. No SDK, I/O, approval,
// frozen batch, CUE value editor or writer is accessible from this contract.
#include "djmeta/unified_review_batch_preflight.h"
#include "djmeta/cue_file_batch_preview.h"
#include "djmeta/cue_physical_selection_audit.h"
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {

struct UnifiedCueHostObservation {
    // Actual OS+foobar-observed CUE carrier and FILE source identities.
    // Values are NOT inferable from FILE basenames, Title Formatting output
    // or paths; final destination keys come from independent host policy.
    std::string cue_carrier_physical_key;
    std::string cue_carrier_source_guard;
    bool cue_carrier_reprobed=false;
    bool cue_bytes_reread=false;
    bool cue_target_reprobed=false;
    std::vector<CueObservedFileSource> file_sources;
    std::vector<CueHostReference> file_destinations;
};

struct UnifiedCueFilePlanPreview {
    bool eligible=false; // advisory only, NO write approval
    std::string rejection;
    std::vector<FilePlanItem> items;
    // Exactly one per AUDIO row in source order; never indexed by subsong.
    std::vector<UnifiedReviewPostimage> audio_postimages;
    CueAssociationPlan cue;
    BatchPlanReview review;
    std::size_t changed_audio_sources=0;
    bool cue_bytes_change=false;
};

// All selected AUDIO rows and exactly one external CUE must be present.
// Audio physical reprobes are in captured AUDIO order (skip the CUE row).
// Unlike separate local/online-only or CUE-only previews, this method
// compares the exact full FilePlan host observation BEFORE projecting even
// one accepted physical field, and independently matches FILE physical ID,
// FILE canonical source key, proposed host destination key and guard for
// every selected audio. Never treat an empty approval as final consent.
inline UnifiedCueFilePlanPreview preview_unified_cue_file_plan(
    const std::vector<FilePlanItem>& captured,
    std::size_t cue_index,
    const std::vector<UnifiedReviewBatchReprobe>& audio_reprobes,
    std::string_view cue_source_bytes,
    const UnifiedCueHostObservation& host) {
    const auto refuse=[](const char* why) {
        UnifiedCueFilePlanPreview out;
        out.rejection=why;
        return out;
    };
    if(captured.size()<2 || captured.size()>100000 ||
       cue_index>=captured.size() || audio_reprobes.size()!=captured.size()-1 ||
       host.file_sources.empty() || host.file_sources.size()>128 ||
       host.file_destinations.size()!=host.file_sources.size() ||
       cue_source_bytes.empty() || cue_source_bytes.size()>2u*1024u*1024u)
        return refuse("UNIFIED_CUE_BATCH_SHAPE_INVALID");
    const auto& carrier=captured[cue_index];
    if(carrier.role!=FileRole::ExternalCue ||
       carrier.physical_id.empty() || carrier.source_guard.empty() ||
       !host.cue_carrier_reprobed || !host.cue_bytes_reread ||
       !host.cue_target_reprobed ||
       host.cue_carrier_physical_key!=carrier.physical_id ||
       host.cue_carrier_source_guard!=carrier.source_guard)
        return refuse("UNIFIED_CUE_CARRIER_HOST_EVIDENCE_STALE");

    UnifiedCueFilePlanPreview out;
    out.items=captured;
    out.audio_postimages.reserve(audio_reprobes.size());
    std::vector<CueSelectedAudio> selected_audio;
    std::vector<CueSelectedPhysicalSource> physical_selected;
    selected_audio.reserve(audio_reprobes.size());
    physical_selected.reserve(audio_reprobes.size());
    std::size_t audio_index=0;
    for(std::size_t i=0;i<captured.size();++i) {
        if(i==cue_index) continue;
        const auto& row=captured[i];
        const auto& n=audio_reprobes[audio_index++].normalizer;
        const auto& live=n.live;
        if(row.role!=FileRole::Audio ||
           row.cue_links!=CueLinkState::Verified ||
           !live.source_reprobed || !live.tags_reread || !live.target_reprobed ||
           !live.captured.qualified_physical || live.captured.virtual_subsong ||
           live.physical_id.empty() || live.physical_id!=row.physical_id ||
           live.live_physical_key.empty() ||
           live.live_physical_key!=live.captured.host_physical_key ||
           live.live_source_guard.empty() ||
           live.live_source_guard!=live.captured.source_guard ||
           row.source_guard!=live.live_source_guard ||
           row.metadata_fingerprint!=fingerprint(live.captured.original) ||
           n.active_ruleset_revision.empty() ||
           n.active_ruleset_revision!=row.ruleset_revision ||
           n.active_ruleset_revision!=n.captured_analysis.ruleset_revision ||
           !live.approvals.empty())
            return refuse("UNIFIED_CUE_AUDIO_HOST_EVIDENCE_STALE");
        // Do NOT compare only a source string or routing profile: all target
        // guards, CUE policy bits and naming expressions belong to one exact
        // host FilePlan snapshot and may not be replaced between probes.
        out.items[i]=live.observed_item;
        selected_audio.push_back({row.physical_id,row.source_key,row.target_key});
        physical_selected.push_back({
            i,live.live_physical_key,live.live_source_guard,true});
    }
    if(review_batch_plan(captured).plan_fingerprint!=
       review_batch_plan(out.items).plan_fingerprint)
        return refuse("UNIFIED_CUE_WHOLE_HOST_PLAN_CHANGED");

    const auto physical=audit_cue_physical_selection(
        host.cue_carrier_physical_key,host.file_sources,physical_selected);
    if(!physical.eligible)
        return refuse("UNIFIED_CUE_FILE_PHYSICAL_COVERAGE_UNQUALIFIED");

    for(std::size_t i=0;i<host.file_destinations.size();++i) {
        const auto selected=physical.file_to_selected_row[i];
        if(selected==cue_unselected_source_row) continue;
        const auto& reference=host.file_destinations[i];
        // Same original audio for the FILE physical handle and for the
        // captured host canonical path and future final target. Reject path
        // substitutions even when both sources still exist on disk.
        if(reference.resolved_source_key!=captured[selected].source_key ||
           reference.verified_destination_key!=captured[selected].target_key)
            return refuse("UNIFIED_CUE_FILE_TO_AUDIO_TARGET_MISMATCH");
    }

    audio_index=0;
    for(std::size_t i=0;i<captured.size();++i) {
        if(i==cue_index) continue;
        const auto& reprobe=audio_reprobes[audio_index++];
        const auto post=preview_unified_review_physical_tags(reprobe);
        if(!post.physical.eligible)
            return refuse("UNIFIED_CUE_AUDIO_TAG_POSTIMAGE_UNQUALIFIED");
        const auto post_fp=fingerprint(post.physical.planned);
        const auto& previous=captured[i];
        if(!previous.planned_metadata_fingerprint.empty() &&
           previous.planned_metadata_fingerprint!=previous.metadata_fingerprint &&
           previous.planned_metadata_fingerprint!=post_fp)
            return refuse("UNIFIED_CUE_PREVIOUS_AUDIO_STAGING_STALE");
        out.items[i].planned_metadata_fingerprint=post_fp;
        if(post.physical.changed) ++out.changed_audio_sources;
        out.audio_postimages.push_back(post);
    }

    const auto cue=preview_cue_bound_file_batch(
        out.items,cue_index,cue_source_bytes,host.file_destinations,
        selected_audio);
    if(!cue.eligible)
        return refuse("UNIFIED_CUE_FILEPLAN_OR_CUE_CONFLICT");
    out.items=cue.items;
    out.cue=cue.association;
    out.review=cue.review;
    out.cue_bytes_change=out.review.decisions[cue_index].cue_bytes_write_needed;
    out.eligible=true;
    return out;
}
} // namespace djmeta
