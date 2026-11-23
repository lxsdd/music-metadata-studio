#include "stdafx.h"
#include "external_cue_write_controller.h"
#include "cue_readonly_source.h"
#include "host_file_probe.h"
#include "djmeta/cue_reference_locator.h"
#include "djmeta/cue_value_batch_adapter.h"
#include "djmeta/cue_apply_preflight.h"
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace djmeta_foobar {
namespace {
constexpr const char* cue_rules_revision = "mms-exact-online-cue-values-v1";
constexpr std::size_t maximum_cue_files = 128;

void require_same(const HostFileObservation& before,
                  const HostFileObservation& after, const char* reason) {
    if (before.state != HostFileState::ExistingFile ||
        !stable_raw_target_observation(before, after) ||
        before.host_physical_key.empty() || before.source_guard.empty())
        throw std::runtime_error(reason);
}

djmeta::FilePlanItem source_item(
    const std::string& path, const HostFileObservation& observed,
    djmeta::FileRole role) {
    if (observed.state != HostFileState::ExistingFile ||
        observed.canonical_path.empty() ||
        observed.host_physical_key.empty() || observed.source_guard.empty())
        throw std::runtime_error("CUE_HOST_PHYSICAL_SOURCE_UNQUALIFIED");
    djmeta::FilePlanItem item;
    item.physical_id = observed.host_physical_key;
    item.source_path = path;
    item.source_key = observed.canonical_path;
    item.source_guard = observed.source_guard;
    item.role = role;
    item.action = djmeta::FileAction::None;
    item.cue_links = djmeta::CueLinkState::Verified;
    item.ruleset_revision = cue_rules_revision;
    if (role == djmeta::FileRole::Audio) {
        // The CUE-only operation does not propose any audio tag mutation.
        item.metadata_fingerprint = "CUE_ONLY_AUDIO_TAGS_UNTOUCHED";
        item.planned_metadata_fingerprint = item.metadata_fingerprint;
    }
    return item;
}

struct CapturedPlan {
    std::vector<djmeta::FilePlanItem> items;
    std::string raw;
    std::wstring native_path;
    std::string physical_key;
    std::size_t cue_index = 0;
    djmeta::CueValuePostimage postimage;
};

CapturedPlan capture_plan(
    const metadb_handle_ptr& cue_handle, const std::string& cue_path,
    const djmeta::online::ReleaseEdition& edition,
    const std::vector<std::size_t>& selected_indices) {
    if (!cue_handle.is_valid() || cue_path.empty() ||
        selected_indices.empty() || selected_indices.size() > 512)
        throw std::invalid_argument("CUE_HOST_SELECTION_UNQUALIFIED");
    const auto original = probe_host_file_readonly(cue_path);
    require_same(original, original, "CUE_HOST_CARRIER_UNQUALIFIED");
    const auto source = read_cue_raw_on_demand(cue_handle, cue_path);
    if (source.carrier != djmeta::CueCarrierKind::ExternalText)
        throw std::invalid_argument("CUE_HOST_EMBEDDED_REQUIRES_SDK_WRITER");
    const auto inventory = djmeta::inspect_cue_metadata(
        source.raw_text, source.carrier);
    if (inventory.status != djmeta::CueSyntaxStatus::Parsed ||
        inventory.files.empty() || inventory.files.size() > maximum_cue_files ||
        inventory.tracks.empty())
        throw std::invalid_argument("CUE_HOST_FILE_INVENTORY_UNQUALIFIED");
    for (const auto& track : inventory.tracks)
        if (track.file_reference_index >= inventory.files.size())
            throw std::invalid_argument("CUE_HOST_TRACK_FILE_REFERENCE_UNQUALIFIED");

    CapturedPlan result;
    result.raw = source.raw_text;
    result.physical_key = original.host_physical_key;
    result.native_path = resolve_native_external_cue_path(cue_path);
    std::set<std::string> physical_ids{original.host_physical_key};
    std::set<std::string> canonical_keys{original.canonical_path};
    std::map<std::string, HostFileObservation> dependencies;
    std::vector<std::string> audio_ids;
    for (const auto& file : inventory.files) {
        const auto locator = djmeta::preview_cue_sibling_locator(
            cue_path, file.filename);
        if (!locator.eligible)
            throw std::invalid_argument("CUE_HOST_UNSAFE_FILE_REFERENCE: " +
                                        locator.reason);
        const auto [it, inserted] = dependencies.try_emplace(
            locator.source_candidate);
        if (!inserted)
            throw std::invalid_argument("CUE_HOST_DUPLICATE_FILE_REFERENCE");
        it->second = probe_host_file_readonly(locator.source_candidate);
        const auto& observed = it->second;
        require_same(observed, observed, "CUE_HOST_AUDIO_REFERENCE_UNQUALIFIED");
        if (!physical_ids.insert(observed.host_physical_key).second ||
            !canonical_keys.insert(observed.canonical_path).second)
            throw std::invalid_argument("CUE_HOST_AUDIO_ALIAS_OR_DUPLICATE");
        result.items.push_back(source_item(
            locator.source_candidate, observed, djmeta::FileRole::Audio));
        audio_ids.push_back(observed.host_physical_key);
    }
    require_same(original, probe_host_file_readonly(cue_path),
                 "CUE_HOST_CARRIER_CHANGED_DURING_CAPTURE");
    for (const auto& [path, observed] : dependencies)
        require_same(observed, probe_host_file_readonly(path),
                     "CUE_HOST_DEPENDENCY_CHANGED_DURING_CAPTURE");

    auto cue = source_item(cue_path, original, djmeta::FileRole::ExternalCue);
    cue.associated_audio_ids = std::move(audio_ids);
    const auto projected = djmeta::online::stage_exact_external_cue_values_for_batch(
        cue, result.raw, edition, selected_indices);
    if (!projected.eligible)
        throw std::invalid_argument("CUE_HOST_EDITION_PLAN_REFUSED: " +
                                    projected.rejection);
    result.cue_index = result.items.size();
    result.items.push_back(projected.planned);
    result.postimage = projected.cue;
    return result;
}

djmeta::BatchApproval approve_exact_plan(
    const std::vector<djmeta::FilePlanItem>& items) {
    const auto observed = djmeta::review_batch_plan(items);
    if (!observed.ready_to_apply || observed.plan_fingerprint.empty())
        throw std::invalid_argument("CUE_HOST_BATCH_PLAN_UNQUALIFIED");
    for (std::size_t i = 0; i < observed.decisions.size(); ++i) {
        if (observed.decisions[i].metadata_write_needed ||
            observed.decisions[i].file_operation_needed ||
            observed.decisions[i].will_replace_existing_target ||
            (i + 1 < observed.decisions.size() &&
             observed.decisions[i].cue_bytes_write_needed))
            throw std::invalid_argument("CUE_HOST_MIXED_WRITE_PLAN_REFUSED");
    }
    // This object binds a possible later consent to the exact snapshot;
    // preparing it does not grant permission or perform any I/O mutation.
    return {observed.plan_fingerprint, false, {}};
}
} // namespace

ExternalCueHostReview prepare_external_cue_host_review(
    const metadb_handle_ptr& cue_handle, const std::string& cue_path,
    const djmeta::online::ReleaseEdition& edition,
    const std::vector<std::size_t>& selected_indices,
    const djmeta::CueValuePostimage& displayed_postimage) {
    auto capture = capture_plan(cue_handle, cue_path, edition, selected_indices);
    if (!displayed_postimage.eligible ||
        displayed_postimage.changed != capture.postimage.changed ||
        displayed_postimage.bytes != capture.postimage.bytes ||
        !capture.postimage.changed)
        throw std::invalid_argument("CUE_HOST_DISPLAYED_SELECTION_CHANGED");
    auto approval = approve_exact_plan(capture.items);
    const auto frozen = djmeta::freeze_reviewed_batch_preview(
        capture.items, approval);
    const auto gate = djmeta::online::preflight_exact_external_cue_values(
        frozen, approval, capture.items, capture.cue_index, capture.raw,
        edition, selected_indices, true, true, true, true);
    if (gate.readiness != djmeta::online::CueApplyReadiness::DetachedCandidate ||
        gate.postimage.bytes != displayed_postimage.bytes)
        throw std::invalid_argument("CUE_HOST_INITIAL_PREFLIGHT_REFUSED");
    ExternalCueHostReview result;
    result.source_path = cue_path;
    result.native_path = std::move(capture.native_path);
    result.original_bytes = std::move(capture.raw);
    result.physical_key = std::move(capture.physical_key);
    result.items = std::move(capture.items);
    result.approval = std::move(approval);
    result.cue_index = capture.cue_index;
    result.selected_indices = selected_indices;
    result.postimage = std::move(capture.postimage);
    return result;
}

djmeta::online::ExternalCueCommitResult commit_confirmed_external_cue_review(
    const ExternalCueHostReview& reviewed, const metadb_handle_ptr& cue_handle,
    const djmeta::online::ReleaseEdition& edition) {
    if (reviewed.items.empty() || reviewed.cue_index + 1 != reviewed.items.size() ||
        reviewed.approval.reviewed_plan_fingerprint.empty() ||
        reviewed.original_bytes.empty() || !reviewed.postimage.changed)
        return {djmeta::online::ExternalCueCommitStatus::Refused,
                "CUE_HOST_REVIEW_NOT_FROZEN", {}};
    const auto frozen = djmeta::freeze_reviewed_batch_preview(
        reviewed.items, reviewed.approval);
    const auto fresh = capture_plan(
        cue_handle, reviewed.source_path, edition, reviewed.selected_indices);
    if (fresh.native_path != reviewed.native_path ||
        fresh.physical_key != reviewed.physical_key ||
        fresh.raw != reviewed.original_bytes ||
        fresh.cue_index != reviewed.cue_index)
        return {djmeta::online::ExternalCueCommitStatus::Refused,
                "CUE_HOST_SOURCE_OR_RESOLUTION_CHANGED", {}};
    const auto gate = djmeta::online::preflight_exact_external_cue_values(
        frozen, reviewed.approval, fresh.items, fresh.cue_index, fresh.raw,
        edition, reviewed.selected_indices, true, true, true, true);
    if (gate.readiness != djmeta::online::CueApplyReadiness::DetachedCandidate ||
        gate.postimage.bytes != reviewed.postimage.bytes)
        return {djmeta::online::ExternalCueCommitStatus::Refused,
                "CUE_HOST_FRESH_APPROVED_PLAN_CHANGED", {}};
    return djmeta::online::commit_preflighted_external_cue_windows(
        reviewed.native_path, reviewed.physical_key,
        reviewed.original_bytes, gate);
}
} // namespace djmeta_foobar
