#pragma once
// Exact normalizer decisions -> complete frozen physical batch revalidation.
// Pure core: NO host handles, SDK writer, filesystem, authorization or I/O.
#include "djmeta/physical_apply_preflight.h"
#include "djmeta/review_physical_postimage.h"
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace djmeta {

struct NormalizerBatchReprobe {
    PhysicalApplyReprobe live;
    AnalysisResult captured_analysis;
    std::vector<ReviewDecision> decisions;
    std::string active_ruleset_revision;
};

// Compute an unapproved batch plan from the very same explicit normalizer
// decisions the subsequent frozen-plan preflight will re-evaluate.
// This does not freeze a plan or constitute an approval. All caller-supplied
// source/target/CUE observations remain subject to review_batch_plan.
struct NormalizerBatchPlanPreview {
    bool eligible=false;
    std::string rejection;
    std::vector<FilePlanItem> items;
    std::vector<PhysicalTagPostimage> postimages;
    BatchPlanReview review;
};

inline NormalizerBatchPlanPreview preview_explicit_normalizer_batch_plan(
    const std::vector<NormalizerBatchReprobe>& rows) {
    const auto fail=[](const char* why) {
        NormalizerBatchPlanPreview result;
        result.rejection=why;
        return result;
    };
    if(rows.empty() || rows.size()>100000)
        return fail("NORMALIZER_PLAN_SHAPE_INVALID");

    NormalizerBatchPlanPreview result;
    result.items.reserve(rows.size());
    result.postimages.reserve(rows.size());
    for(const auto& row:rows) {
        const auto& live=row.live;
        const auto& item=live.observed_item;
        if(!live.source_reprobed || !live.tags_reread ||
           !live.target_reprobed || !live.captured.qualified_physical ||
           live.captured.virtual_subsong ||
           live.physical_id.empty() || item.physical_id!=live.physical_id ||
           live.live_physical_key.empty() ||
           live.live_physical_key!=live.captured.host_physical_key ||
           live.live_source_guard.empty() ||
           live.live_source_guard!=live.captured.source_guard ||
           item.source_guard!=live.live_source_guard)
            return fail("NORMALIZER_PLAN_SOURCE_UNQUALIFIED");
        if(row.active_ruleset_revision.empty() ||
           row.active_ruleset_revision!=row.captured_analysis.ruleset_revision ||
           row.active_ruleset_revision!=item.ruleset_revision ||
           !live.approvals.empty())
            return fail("NORMALIZER_PLAN_RULES_OR_APPROVALS_INVALID");
        if(item.metadata_fingerprint.empty() ||
           item.metadata_fingerprint!=fingerprint(live.captured.original))
            return fail("NORMALIZER_PLAN_SOURCE_TAGS_STALE");
        const auto post=preview_explicit_normalizer_physical_tags(
            live.captured,live.fresh_tags,
            live.live_physical_key,live.live_source_guard,
            row.captured_analysis,row.active_ruleset_revision,row.decisions);
        if(!post.eligible)
            return fail("NORMALIZER_PLAN_DECISION_POSTIMAGE_UNQUALIFIED");
        const auto planned_fp=fingerprint(post.planned);
        // An already-staged different plan cannot be silently replaced.
        // A fresh source item may carry its original/no-op fingerprint.
        if(!item.planned_metadata_fingerprint.empty() &&
           item.planned_metadata_fingerprint!=item.metadata_fingerprint &&
           item.planned_metadata_fingerprint!=planned_fp)
            return fail("NORMALIZER_PLAN_PREVIOUS_STAGING_STALE");
        auto planned=item;
        planned.planned_metadata_fingerprint=planned_fp;
        result.items.push_back(std::move(planned));
        result.postimages.push_back(post);
    }
    result.review=review_batch_plan(result.items);
    if(result.review.blocked!=0)
        return fail("NORMALIZER_PLAN_BATCH_CONFLICT");
    // Overwrite proposals can remain awaiting explicit consent. Nothing is
    // automatically frozen and no file/metadata writer is reachable here.
    result.eligible=true;
    return result;
}

// The caller must independently obtain fresh host observations, immutable
// review decisions, and explicit *whole-plan* BatchApproval. Re-derive each
// physical tag change from the exact original normalizer evidence; never
// accept a caller-supplied field approval list or implicit SAFE preview.
// Final readiness is detached evidence only, NOT SDK write authorization.
inline PhysicalApplyPreflight preflight_explicit_normalizer_physical_batch(
    const FrozenBatchPreview& frozen,
    const BatchApproval& approval,
    const std::vector<NormalizerBatchReprobe>& rows) {
    const auto fail=[](const char* why) {
        PhysicalApplyPreflight result;
        result.rejection=why;
        return result;
    };
    if(rows.empty() || rows.size()>100000 || rows.size()!=frozen.items.size())
        return fail("NORMALIZER_BATCH_SHAPE_INVALID");

    std::vector<PhysicalApplyReprobe> verified;
    verified.reserve(rows.size());
    for(std::size_t i=0;i<rows.size();++i) {
        const auto& row=rows[i];
        const auto& live=row.live;
        if(row.active_ruleset_revision.empty() ||
           row.active_ruleset_revision!=row.captured_analysis.ruleset_revision ||
           row.active_ruleset_revision!=frozen.items[i].ruleset_revision ||
           row.active_ruleset_revision!=live.observed_item.ruleset_revision)
            return fail("NORMALIZER_BATCH_RULESET_STALE");
        // This adapter has one authority: the immutable normalizer decisions.
        // A second independent approval vector must not bypass its review.
        if(!live.approvals.empty())
            return fail("NORMALIZER_BATCH_EXTERNAL_FIELD_APPROVALS");
        const auto post=preview_explicit_normalizer_physical_tags(
            live.captured,live.fresh_tags,live.live_physical_key,
            live.live_source_guard,row.captured_analysis,
            row.active_ruleset_revision,row.decisions);
        if(!post.eligible)
            return fail("NORMALIZER_BATCH_DECISION_POSTIMAGE_UNQUALIFIED");
        if(post.planned.fields.size()!=live.captured.original.fields.size())
            return fail("NORMALIZER_BATCH_FIELD_STRUCTURE_CHANGED");
        PhysicalApplyReprobe prepared=live;
        for(std::size_t n=0;n<post.planned.fields.size();++n) {
            const auto& before=live.captured.original.fields[n];
            const auto& after=post.planned.fields[n];
            if(before.name!=after.name)
                return fail("NORMALIZER_BATCH_FIELD_STRUCTURE_CHANGED");
            if(before.values==after.values) continue;
            if(prepared.approvals.size()>=1024)
                return fail("NORMALIZER_BATCH_FIELD_LIMIT");
            prepared.approvals.push_back(
                {before.name,before.values,after.values});
        }
        if(prepared.approvals.size()!=post.explicitly_changed ||
           fingerprint(post.planned)!=
               live.observed_item.planned_metadata_fingerprint)
            return fail("NORMALIZER_BATCH_STAGED_PLAN_MISMATCH");
        verified.push_back(std::move(prepared));
    }
    // Whole-batch frozen consent, source/target/physical-key re-probes,
    // immutable plan fingerprint, CUE/FileOps separation and no-op checks.
    return preflight_exact_physical_batch(frozen,approval,verified);
}

} // namespace djmeta
