#pragma once
// One detached, read-only local+online physical audio / external CUE FILE
// reference / qualified host FilePlan preflight. No SDK, I/O, approval,
// frozen batch, CUE value editor or writer is accessible from this contract.
#include "djmeta/unified_review_batch_preflight.h"
#include "djmeta/cue_file_batch_preview.h"
#include "djmeta/cue_physical_selection_audit.h"
#include <cstddef>
#include <algorithm>
#include <map>
#include <utility>
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
    // Unapproved source-selection evidence retained solely for detached
    // second-pass comparison; no SDK write handle, token or consent.
    std::vector<std::string> exact_selection_fingerprints;
    std::vector<CueObservedFileSource> observed_file_sources;
    std::vector<CueHostReference> observed_file_references;
};

// Include choices and provider provenance, not merely the resulting
// metadata postimage: Accept and Reject can produce identical bytes, but
// a changed user decision must invalidate a previous exact review.
inline std::string unified_cue_review_selection_fingerprint(
    const UnifiedReviewBatchReprobe& row) {
    MetadataDocument proof;
    const auto add=[&](std::string name,std::vector<std::string> values) {
        proof.fields.push_back({std::move(name),std::move(values)});
    };
    add("SOURCE",{row.normalizer.live.physical_id,
        row.normalizer.live.live_physical_key,
        row.normalizer.live.live_source_guard});
    add("RULESET",{row.normalizer.active_ruleset_revision});
    const auto& analysis=row.normalizer.captured_analysis;
    add("CAPTURE",{analysis.input_fingerprint,analysis.ruleset_revision});
    // A decision index is only meaningful against the *same* immutable
    // proposal/rule explanation. An updated rule could yield byte-identical
    // tags yet materially different user intent; the second pass must refuse
    // that silent substitution rather than comparing just postimages.
    add("CANONICAL_PREVIEW",{fingerprint(analysis.canonical_preview)});
    for(std::size_t i=0;i<analysis.changes.size();++i) {
        const auto& c=analysis.changes[i];
        add("LOCAL_CHANGE",{std::to_string(i),
            std::to_string(c.field_index),c.field,
            std::to_string(c.value_index),c.original_value,
            c.before_value,c.proposed_value,c.rule_id,
            std::to_string(static_cast<int>(c.safety)),c.rationale});
    }
    for(std::size_t i=0;i<analysis.proposals.size();++i) {
        const auto& p=analysis.proposals[i];
        add("LOCAL_PROPOSAL",{std::to_string(i),
            std::to_string(p.field_index),p.field,
            std::to_string(p.value_index),p.original_value,
            p.proposed_value,
            std::to_string(static_cast<int>(p.safety))});
        add("LOCAL_PROPOSAL_RULE_IDS",p.rule_ids);
        add("LOCAL_PROPOSAL_RATIONALES",p.rationales);
    }
    for(std::size_t i=0;i<row.normalizer.decisions.size();++i) {
        const auto& decision=row.normalizer.decisions[i];
        add("LOCAL_DECISION",{std::to_string(i),
            std::to_string(static_cast<int>(decision.action)),
            decision.manual_value});
    }
    if(row.online_capture) {
        add("ONLINE_PRESENT",{"1",
            fingerprint(row.online_capture->physical_before)});
        for(std::size_t i=0;i<row.online_capture->evidence.size();++i) {
            const auto& ev=row.online_capture->evidence[i];
            add("ONLINE_PROVENANCE",{std::to_string(i),ev.field,
                ev.provider,ev.source_id,
                std::to_string(static_cast<int>(ev.scope)),
                std::to_string(static_cast<int>(ev.date_meaning))});
            add("ONLINE_VALUES",ev.values);
        }
    } else add("ONLINE_PRESENT",{"0"});
    for(const auto i:row.selected_online_indices)
        add("ONLINE_SELECTED",{std::to_string(i)});
    for(const auto& [i,target]:row.explicit_date_targets)
        add("ONLINE_DATE_TARGET",{std::to_string(i),target});
    return fingerprint(proof);
}

struct UnifiedCueSecondPassCheck {
    bool eligible=false; // advisory recheck only, NEVER write permission
    std::string rejection;
    bool exact_noop=false;
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
        out.exact_selection_fingerprints.push_back(
            unified_cue_review_selection_fingerprint(
                audio_reprobes[audio_index-1]));
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
        const auto& reference=host.file_destinations[i];
        const auto& physical_file=host.file_sources[i];
        // A physical ID + file version is NOT proof that its separately
        // supplied canonical source name matches this FILE reference.
        // This check is mandatory for unselected sources too: otherwise a
        // moved CUE could silently relink an unrelated audio file.
        if(physical_file.canonical_source_key.empty() ||
           physical_file.canonical_source_key!=reference.resolved_source_key)
            return refuse("UNIFIED_CUE_FILE_CANONICAL_SOURCE_MISMATCH");
        if(selected==cue_unselected_source_row) continue;
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
    out.observed_file_sources=host.file_sources;
    out.observed_file_references=host.file_destinations;
    out.eligible=true;
    return out;
}

// Reconstruct the whole preview from independently RE-READ sources, rather
// than reusing stored booleans or postimages as future authority. A stable
// output plan is NOT enough: selected-field provenance and every selected or
// unselected physical FILE source identity/version must match byte-for-byte.
// There is deliberately no BatchApproval, freeze or executor here.
inline UnifiedCueSecondPassCheck recheck_unified_cue_file_plan(
    const UnifiedCueFilePlanPreview& previous,
    const std::vector<FilePlanItem>& captured,
    std::size_t cue_index,
    const std::vector<UnifiedReviewBatchReprobe>& refreshed_audio,
    std::string_view reread_cue_bytes,
    const UnifiedCueHostObservation& refreshed_host) {
    const auto refuse=[](const char* reason) {
        UnifiedCueSecondPassCheck out;
        out.rejection=reason;
        return out;
    };
    if(!previous.eligible || previous.review.plan_fingerprint.empty() ||
       previous.review.blocked!=0 ||
       previous.audio_postimages.size()!=refreshed_audio.size() ||
       previous.items.size()!=captured.size() ||
       previous.exact_selection_fingerprints.size()!=refreshed_audio.size() ||
       previous.observed_file_sources.size()!=refreshed_host.file_sources.size() ||
       previous.observed_file_references.size()!=refreshed_host.file_destinations.size())
        return refuse("UNIFIED_CUE_SECOND_PASS_SHAPE_OR_ORIGINAL_UNQUALIFIED");
    for(std::size_t i=0;i<refreshed_audio.size();++i)
        if(previous.exact_selection_fingerprints[i]!=
           unified_cue_review_selection_fingerprint(refreshed_audio[i]))
            return refuse("UNIFIED_CUE_SECOND_PASS_SELECTION_OR_PROVENANCE_CHANGED");
    for(std::size_t i=0;i<refreshed_host.file_sources.size();++i) {
        const auto& before=previous.observed_file_sources[i];
        const auto& now=refreshed_host.file_sources[i];
        if(before.file_index!=now.file_index ||
           before.host_physical_key!=now.host_physical_key ||
           before.source_guard!=now.source_guard ||
           before.observed!=now.observed ||
           before.canonical_source_key!=now.canonical_source_key)
            return refuse("UNIFIED_CUE_SECOND_PASS_FILE_SOURCE_CHANGED");
        const auto& ref_before=previous.observed_file_references[i];
        const auto& ref_now=refreshed_host.file_destinations[i];
        if(ref_before.reference_index!=ref_now.reference_index ||
           ref_before.expected_filename!=ref_now.expected_filename ||
           ref_before.resolved_source_key!=ref_now.resolved_source_key ||
           ref_before.proposed_filename!=ref_now.proposed_filename ||
           ref_before.uniquely_resolved!=ref_now.uniquely_resolved ||
           ref_before.destination_reference_verified!=
               ref_now.destination_reference_verified ||
           ref_before.verified_destination_key!=
               ref_now.verified_destination_key)
            return refuse("UNIFIED_CUE_SECOND_PASS_REFERENCE_OR_TARGET_CHANGED");
    }
    const auto fresh=preview_unified_cue_file_plan(
        captured,cue_index,refreshed_audio,reread_cue_bytes,refreshed_host);
    if(!fresh.eligible)
        return refuse("UNIFIED_CUE_SECOND_PASS_FRESH_PLAN_UNQUALIFIED");
    if(previous.review.plan_fingerprint!=fresh.review.plan_fingerprint ||
       previous.cue.original_cue_fingerprint!=
           fresh.cue.original_cue_fingerprint ||
       previous.cue.proposed_cue_fingerprint!=
           fresh.cue.proposed_cue_fingerprint ||
       previous.cue.proposed_cue_bytes!=fresh.cue.proposed_cue_bytes ||
       previous.cue.related_selected_audio_ids!=
           fresh.cue.related_selected_audio_ids ||
       previous.cue.changes_references!=fresh.cue.changes_references ||
       previous.changed_audio_sources!=fresh.changed_audio_sources ||
       previous.cue_bytes_change!=fresh.cue_bytes_change ||
       previous.audio_postimages.size()!=fresh.audio_postimages.size())
        return refuse("UNIFIED_CUE_SECOND_PASS_POSTIMAGE_OR_PLAN_CHANGED");
    for(std::size_t i=0;i<fresh.audio_postimages.size();++i)
        if(previous.audio_postimages[i].physical.planned!=
               fresh.audio_postimages[i].physical.planned ||
           previous.audio_postimages[i].physical.changed!=
               fresh.audio_postimages[i].physical.changed)
            return refuse("UNIFIED_CUE_SECOND_PASS_AUDIO_TAGS_CHANGED");

    UnifiedCueSecondPassCheck result;
    result.eligible=true;
    result.exact_noop=std::all_of(fresh.review.decisions.begin(),
         fresh.review.decisions.end(),[](const ItemDecision& row) {
             return !row.metadata_write_needed && !row.cue_bytes_write_needed &&
                    !row.file_operation_needed && row.status==PlanStatus::Unchanged;
         });
    result.changed_audio_sources=fresh.changed_audio_sources;
    result.cue_bytes_change=fresh.cue_bytes_change;
    return result;
}
} // namespace djmeta
