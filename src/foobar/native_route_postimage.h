#pragma once
// SDK-free production route postimage gate. Used by the foobar dialog and
// resource-backed Win32/x64 HWND regression. No writer or filesystem I/O.
#include "djmeta/unified_review_batch_preflight.h"
#include "djmeta/batch_preview.h"
#include "host_file_probe.h"
#include "native_preview_controls.h"
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace djmeta_foobar {
// The exact selected source's advisory raw target observation. This remains
// separate from both the foobar File Operations destination and any writer.
struct JointRawTargetObservation {
    std::size_t source_index=0;
    std::string raw_candidate;
    std::string source_physical_key;
    std::string source_guard;
    HostFileObservation target;
};

// The native owner-data table and the Win32 HWND regression share the same
// projection of accepted online fields onto a captured physical input row.
// Invalidation clears the overlay and every old host target observation before
// describe_batch_preview() scans the complete source-order batch again.
inline djmeta::BatchPreviewInputRow native_joint_route_queue_input(
    djmeta::BatchPreviewInputRow input,
    bool has_verified_snapshot, std::size_t online_source,
    std::size_t index, std::size_t included_count,
    bool route_valid, const std::string& route_after,
    const std::string& observed_key, const std::string& observed_guard,
    const std::optional<JointRawTargetObservation>& observation) {
    if(!native_recheck_preserved_online_selection(
           has_verified_snapshot,online_source,index,included_count))
        return input;
    input.raw_relative_path=route_valid ? route_after:std::string{};
    input.raw_target_presence=djmeta::RawTargetPresence::NotInspected;
    input.raw_target_physical_key.clear();
    input.raw_target_guard.clear();
    input.raw_target_host_canonical.clear();
    input.filesystem_target_checked=false;
    input.cue_dependencies_checked=false;
    if(route_valid && observation) {
        const auto& probe=*observation;
        const auto candidate=input.destination_root+"\\"+input.raw_relative_path;
        if(native_joint_raw_observation_matches(
               probe.source_index,index,probe.raw_candidate,candidate,
               probe.source_physical_key,observed_key,
               probe.source_guard,observed_guard)) {
            switch(probe.target.state) {
            case HostFileState::Missing:
                input.raw_target_presence=djmeta::RawTargetPresence::Missing;
                input.raw_target_host_canonical=probe.target.canonical_path;
                break;
            case HostFileState::ExistingFile:
                input.raw_target_presence=djmeta::RawTargetPresence::Existing;
                input.raw_target_host_canonical=probe.target.canonical_path;
                input.raw_target_physical_key=probe.target.host_physical_key;
                input.raw_target_guard=probe.target.source_guard;
                break;
            case HostFileState::Unqualified:
                input.raw_target_presence=djmeta::RawTargetPresence::Unqualified;
                break;
            }
        }
    }
    return input;
}

inline djmeta::UnifiedReviewPostimage native_verified_joint_route_postimage(
    const djmeta::PhysicalTagSourceSnapshot& captured,
    const djmeta::MetadataDocument& fresh_tags,
    const std::string& live_physical_key,
    const std::string& live_source_guard,
    const djmeta::AnalysisResult& analysis,
    const std::string& active_ruleset_revision,
    const std::vector<djmeta::ReviewDecision>& local_decisions,
    const djmeta::online::OnlineFieldReviewSnapshot& online,
    const std::vector<std::size_t>& included_fields,
    const std::map<std::size_t,std::string>& date_targets) {
    if(!captured.qualified_physical || captured.virtual_subsong ||
       captured.host_physical_key.empty() || captured.source_guard.empty() ||
       captured.host_physical_key!=live_physical_key ||
       captured.source_guard!=live_source_guard ||
       captured.original!=fresh_tags ||
       active_ruleset_revision.empty() ||
       active_ruleset_revision!=analysis.ruleset_revision)
        throw std::invalid_argument(
            "Physical source or rules changed before joint raw route preview.");
    djmeta::UnifiedReviewBatchReprobe row;
    row.normalizer.live.captured=captured;
    row.normalizer.live.fresh_tags=fresh_tags;
    row.normalizer.live.live_physical_key=live_physical_key;
    row.normalizer.live.live_source_guard=live_source_guard;
    row.normalizer.captured_analysis=analysis;
    row.normalizer.active_ruleset_revision=active_ruleset_revision;
    row.normalizer.decisions=local_decisions;
    row.online_capture=online;
    row.selected_online_indices=included_fields;
    row.explicit_date_targets=date_targets;
    return djmeta::preview_unified_review_physical_tags(row);
}
} // namespace djmeta_foobar
