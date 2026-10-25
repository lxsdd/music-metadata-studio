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
