#include "djmeta/cue_association.h"
#include "djmeta/normalizer.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace djmeta {
namespace {
std::string hash_cue_bytes(std::string_view bytes) {
    MetadataDocument doc;
    doc.fields.push_back({"EXTERNAL_CUE_SOURCE_BYTES", {std::string(bytes)}});
    return fingerprint(doc);
}
void fail(CueAssociationPlan& plan, const char* reason) {
    plan.diagnostics.emplace_back(reason);
}
} // namespace

CueAssociationPlan qualify_external_cue_association(
    std::string_view raw_cue_bytes,
    const std::vector<CueHostReference>& host_references,
    const std::vector<CueSelectedAudio>& selected_audios) {
    CueAssociationPlan plan;
    plan.original_cue_fingerprint = hash_cue_bytes(raw_cue_bytes);
    const auto cue = inspect_external_cue(raw_cue_bytes);
    if (cue.status != CueSyntaxStatus::Parsed) {
        fail(plan, "CUE_SYNTAX_NOT_QUALIFIED");
        return plan;
    }
    if (host_references.size() != cue.references.size()) {
        fail(plan, "CUE_HOST_REFERENCE_COVERAGE_MISMATCH");
        return plan;
    }

    std::map<std::string, const CueSelectedAudio*> selected_source_to_audio;
    std::set<std::string> selected_physical_ids;
    for (const auto& audio : selected_audios) {
        if (audio.physical_id.empty() || audio.source_key.empty() ||
            audio.target_key.empty() ||
            !selected_physical_ids.insert(audio.physical_id).second ||
            !selected_source_to_audio.emplace(audio.source_key, &audio).second) {
            fail(plan, "DUPLICATE_OR_UNQUALIFIED_SELECTED_AUDIO");
            return plan;
        }
    }

    std::map<std::size_t, const CueHostReference*> by_index;
    for (const auto& host : host_references) {
        if (host.reference_index >= cue.references.size() ||
            !by_index.emplace(host.reference_index, &host).second) {
            fail(plan, "DUPLICATE_OR_OUT_OF_RANGE_CUE_REFERENCE");
            return plan;
        }
    }

    std::set<std::string> related;
    std::map<std::string, std::string> proposed_name_per_physical_source;
    std::map<std::string, std::string> destination_to_source;
    for (std::size_t i = 0; i < cue.references.size(); ++i) {
        const auto found = by_index.find(i);
        if (found == by_index.end()) {
            fail(plan, "MISSING_CUE_REFERENCE_RESOLUTION");
            return plan;
        }
        const CueHostReference& host = *found->second;
        const ExternalCueReference& original = cue.references[i];
        if (host.expected_filename != original.filename) {
            fail(plan, "CUE_REFERENCE_STALE");
            return plan;
        }
        if (!host.uniquely_resolved || host.resolved_source_key.empty()) {
            fail(plan, "CUE_REFERENCE_AMBIGUOUS_OR_MISSING");
            return plan;
        }
        if (!host.destination_reference_verified ||
            host.proposed_filename.empty() ||
            host.verified_destination_key.empty()) {
            fail(plan, "CUE_DESTINATION_RELATION_UNVERIFIED");
            return plan;
        }
        // Two genuinely different physical sources must not alias one final
        // FILE destination even if their raw filename strings look distinct.
        const auto [dest, unique]=destination_to_source.emplace(
            host.verified_destination_key,host.resolved_source_key);
        if(!unique && dest->second!=host.resolved_source_key) {
            fail(plan, "CUE_MULTIPLE_SOURCES_SHARE_DESTINATION");
            return plan;
        }
        const auto [destination, inserted] =
            proposed_name_per_physical_source.emplace(
                host.resolved_source_key, host.proposed_filename);
        if (!inserted && destination->second != host.proposed_filename) {
            fail(plan, "CUE_SAME_SOURCE_CONFLICTING_DESTINATION_REFERENCES");
            return plan;
        }
        // For files not selected in the batch, the host must nevertheless
        // verify the post-move FILE reference still points to that file.
        const auto selected=selected_source_to_audio.find(host.resolved_source_key);
        if(selected!=selected_source_to_audio.end()) {
            if(host.verified_destination_key!=selected->second->target_key) {
                fail(plan, "CUE_TARGET_DIFFERS_FROM_SELECTED_AUDIO_PLAN");
                return plan;
            }
            related.insert(selected->second->physical_id);
        }
        if (host.proposed_filename != original.filename)
            plan.changes.push_back({i, original.filename, host.proposed_filename});
    }

    if (related.size() != selected_physical_ids.size()) {
        fail(plan, "SELECTED_AUDIO_NOT_REFERENCED_BY_CUE");
        return plan;
    }
    const auto post = preview_external_cue_reference_rewrite(raw_cue_bytes, plan.changes);
    if (!post.eligible) {
        fail(plan, "CUE_REFERENCE_POSTIMAGE_INVALID");
        return plan;
    }
    plan.proposed_cue_bytes = post.proposed_bytes;
    plan.proposed_cue_fingerprint = hash_cue_bytes(post.proposed_bytes);
    plan.related_selected_audio_ids.assign(related.begin(), related.end());
    plan.changes_references = post.changed;
    plan.qualified = true;
    return plan;
}

} // namespace djmeta
