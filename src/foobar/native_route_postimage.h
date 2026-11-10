#pragma once
// SDK-free production route postimage gate. Used by the foobar dialog and
// resource-backed Win32/x64 HWND regression. No writer or filesystem I/O.
#include "djmeta/unified_review_batch_preflight.h"
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace djmeta_foobar {
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
