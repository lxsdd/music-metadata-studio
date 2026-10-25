#pragma once
// One read-only physical postimage and frozen-plan preflight for decisions
// explicitly made in BOTH the local normalizer and verified online review.
// No SDK/HTTP handle, filesystem access, persistent approval or writer.
#include "djmeta/normalizer_batch_preflight.h"
#include "djmeta/online_physical_postimage.h"
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace djmeta {

struct UnifiedReviewBatchReprobe {
    NormalizerBatchReprobe normalizer;
    online::OnlineFieldReviewSnapshot online_capture;
    std::vector<std::size_t> selected_online_indices;
    std::map<std::size_t,std::string> explicit_date_targets;
};

struct UnifiedReviewPostimage {
    PhysicalTagPostimage physical;
    std::size_t normalizer_changed_fields=0;
    std::size_t online_changed_fields=0;
    std::size_t mutually_agreed_fields=0;
    // Case-insensitive physical tag names with incompatible accepted values.
    std::vector<std::string> conflicting_fields;
    std::string rejection;
};

// Each side independently re-derives its proposal from immutable evidence
// against the SAME original physical tags and live identity/rules guards.
// We merge only complete-field, byte-exact accepted postimages; a normalizer
// multivalue edit and an online vector replacement are NOT merged per value.
inline UnifiedReviewPostimage preview_unified_review_physical_tags(
    const UnifiedReviewBatchReprobe& row) {
    const auto refuse=[](const char* why) {
        UnifiedReviewPostimage out;
        out.rejection=why;
        return out;
    };
    const auto& n=row.normalizer;
    const auto& live=n.live;
    if(!live.approvals.empty() ||
       row.online_capture.physical_before!=live.captured.original)
        return refuse("UNIFIED_EXTERNAL_APPROVAL_OR_ORIGINAL_MISMATCH");
    const auto local=preview_explicit_normalizer_physical_tags(
        live.captured,live.fresh_tags,live.live_physical_key,
        live.live_source_guard,n.captured_analysis,
        n.active_ruleset_revision,n.decisions);
    if(!local.eligible)
        return refuse("UNIFIED_NORMALIZER_EVIDENCE_INVALID");
    online::OnlineFieldStagedPreview selected;
    try {
        selected=online::stage_selected_online_fields(
            row.online_capture,live.fresh_tags,
            row.selected_online_indices,row.explicit_date_targets);
    } catch(const std::invalid_argument&) {
        return refuse("UNIFIED_ONLINE_SELECTION_INVALID");
    }
    const auto remote=online::verify_online_physical_tag_postimage(
        live.captured,live.fresh_tags,live.live_physical_key,
        live.live_source_guard,row.online_capture,
        row.selected_online_indices,row.explicit_date_targets,selected);
    if(!remote.eligible)
        return refuse("UNIFIED_ONLINE_PHYSICAL_EVIDENCE_INVALID");

    std::map<std::string,std::vector<std::string>> before;
    for(const auto& field:live.captured.original.fields) {
        const auto name=physical_fold_name(field.name);
        if(!before.emplace(name,field.values).second)
            return refuse("UNIFIED_AMBIGUOUS_ORIGINAL_FIELDS");
    }
    std::map<std::string,PhysicalTagValueApproval> merged;
    UnifiedReviewPostimage out;
    const auto take=[&](const PhysicalTagPostimage& post, bool is_online) {
        for(const auto& field:post.planned.fields) {
            const auto name=physical_fold_name(field.name);
            const auto original=before.find(name);
            const std::vector<std::string> old=original==before.end()
                ? std::vector<std::string>{}:original->second;
            if(old==field.values) continue;
            if(is_online) ++out.online_changed_fields;
            else ++out.normalizer_changed_fields;
            const auto [where,inserted]=merged.emplace(
                name,PhysicalTagValueApproval{field.name,old,field.values});
            if(!inserted) {
                if(where->second.replacement_values!=field.values) {
                    out.conflicting_fields.push_back(name);
                    return false;
                }
                ++out.mutually_agreed_fields;
            }
        }
        return true;
    };
    if(!take(local,false) || !take(remote,true)) {
        out.rejection="UNIFIED_FIELD_CONFLICT_REQUIRES_EXPLICIT_RESOLUTION";
        return out;
    }
    if(out.normalizer_changed_fields!=local.explicitly_changed ||
       out.online_changed_fields!=remote.explicitly_changed ||
       merged.size()>1024)
        return refuse("UNIFIED_CHANGED_FIELD_CARDINALITY_MISMATCH");

    std::vector<PhysicalTagValueApproval> approvals;
    approvals.reserve(merged.size());
    for(const auto& [name,approval]:merged) {
        (void)name;
        approvals.push_back(approval);
    }
    out.physical=preview_explicit_physical_tag_values(
        live.captured,live.fresh_tags,live.live_physical_key,
        live.live_source_guard,approvals);
    if(!out.physical.eligible ||
       out.physical.explicitly_changed!=merged.size()) {
        out.rejection="UNIFIED_COMBINED_PHYSICAL_GUARD_FAILED";
        return out;
    }
    // A proposed SAFE change, a conflicting provider vector and an
    // unselected online field never silently enter the shared routing input.
    return out;
}

struct UnifiedReviewBatchPlanPreview {
    bool eligible=false;
    std::string rejection;
    std::vector<FilePlanItem> items;
    std::vector<UnifiedReviewPostimage> postimages;
    BatchPlanReview review;
};

// All batch records must supply independent fresh host source/target probes.
// Routing and CUE observations are supplied by the host and remain subject
// to review_batch_plan; this function never invents an allowed destination.
inline UnifiedReviewBatchPlanPreview preview_unified_review_batch_plan(
    const std::vector<UnifiedReviewBatchReprobe>& rows) {
    const auto refuse=[](const char* why) {
        UnifiedReviewBatchPlanPreview out;
        out.rejection=why;
        return out;
    };
    if(rows.empty() || rows.size()>100000)
        return refuse("UNIFIED_BATCH_SHAPE_INVALID");
    UnifiedReviewBatchPlanPreview out;
    out.items.reserve(rows.size());
    out.postimages.reserve(rows.size());
    for(const auto& row:rows) {
        const auto& n=row.normalizer;
        const auto& live=n.live;
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
            return refuse("UNIFIED_BATCH_SOURCE_UNQUALIFIED");
        if(n.active_ruleset_revision.empty() ||
           n.active_ruleset_revision!=n.captured_analysis.ruleset_revision ||
           n.active_ruleset_revision!=item.ruleset_revision ||
           item.metadata_fingerprint!=fingerprint(live.captured.original))
            return refuse("UNIFIED_BATCH_RULESET_OR_TAGS_STALE");
        auto post=preview_unified_review_physical_tags(row);
        if(!post.physical.eligible)
            return refuse("UNIFIED_BATCH_POSTIMAGE_UNQUALIFIED");
        const auto planned_fp=fingerprint(post.physical.planned);
        if(!item.planned_metadata_fingerprint.empty() &&
           item.planned_metadata_fingerprint!=item.metadata_fingerprint &&
           item.planned_metadata_fingerprint!=planned_fp)
            return refuse("UNIFIED_BATCH_PREVIOUS_STAGING_STALE");
        auto planned=item;
        planned.planned_metadata_fingerprint=planned_fp;
        out.items.push_back(std::move(planned));
        out.postimages.push_back(std::move(post));
    }
    out.review=review_batch_plan(out.items);
    if(out.review.blocked)
        return refuse("UNIFIED_BATCH_CONFLICT");
    out.eligible=true;
    return out;
}

// Revalidate BOTH independent selection streams immediately before the
// existing frozen whole-batch physical-only gate. CUE, FileOps, sidecars and
// actual SDK writes stay separately disabled. DetachedCandidate is NOT an
// authorization to execute against user media.
inline PhysicalApplyPreflight preflight_unified_review_physical_batch(
    const FrozenBatchPreview& frozen,const BatchApproval& approval,
    const std::vector<UnifiedReviewBatchReprobe>& rows) {
    const auto refuse=[](const char* why) {
        PhysicalApplyPreflight out;
        out.rejection=why;
        return out;
    };
    if(rows.empty() || rows.size()>100000 || rows.size()!=frozen.items.size())
        return refuse("UNIFIED_PREFLIGHT_BATCH_SHAPE_INVALID");
    std::vector<PhysicalApplyReprobe> prepared;
    prepared.reserve(rows.size());
    for(std::size_t i=0;i<rows.size();++i) {
        const auto& n=rows[i].normalizer;
        const auto& live=n.live;
        if(n.active_ruleset_revision.empty() ||
           n.active_ruleset_revision!=n.captured_analysis.ruleset_revision ||
           n.active_ruleset_revision!=frozen.items[i].ruleset_revision ||
           n.active_ruleset_revision!=live.observed_item.ruleset_revision ||
           !live.approvals.empty())
            return refuse("UNIFIED_PREFLIGHT_RULES_OR_APPROVALS_STALE");
        const auto combined=preview_unified_review_physical_tags(rows[i]);
        if(!combined.physical.eligible)
            return refuse("UNIFIED_PREFLIGHT_DECISIONS_UNQUALIFIED");
        if(fingerprint(combined.physical.planned)!=
           live.observed_item.planned_metadata_fingerprint ||
           fingerprint(combined.physical.planned)!=
           frozen.items[i].planned_metadata_fingerprint)
            return refuse("UNIFIED_PREFLIGHT_STAGED_PLAN_MISMATCH");
        PhysicalApplyReprobe verified=live;
        std::map<std::string,std::vector<std::string>> original;
        for(const auto& field:live.captured.original.fields)
            if(!original.emplace(physical_fold_name(field.name),
                                 field.values).second)
                return refuse("UNIFIED_PREFLIGHT_DUPLICATE_ORIGINAL");
        for(const auto& field:combined.physical.planned.fields) {
            const auto old=original.find(physical_fold_name(field.name));
            const std::vector<std::string> before=old==original.end()
                ? std::vector<std::string>{}:old->second;
            if(before==field.values) continue;
            if(verified.approvals.size()>=1024)
                return refuse("UNIFIED_PREFLIGHT_FIELD_LIMIT");
            verified.approvals.push_back({field.name,before,field.values});
        }
        if(verified.approvals.size()!=combined.physical.explicitly_changed)
            return refuse("UNIFIED_PREFLIGHT_FIELD_COUNT_MISMATCH");
        prepared.push_back(std::move(verified));
    }
    return preflight_exact_physical_batch(frozen,approval,prepared);
}
} // namespace djmeta
