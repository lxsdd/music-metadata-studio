#pragma once
// Existing external CUE FILE -> staged RAW route impact, for native read-only
// inspection. This is deliberately NOT a foobar File Operations path resolver
// or a CUE rewrite plan: those two final host gates remain unqualified.
#include "djmeta/batch_preview.h"
#include "djmeta/cue_physical_selection_audit.h"
#include <cstddef>
#include <string>
#include <vector>

namespace djmeta {

struct CueRawRouteTrack {
    std::string host_physical_key;
    std::string source_guard;
    std::string destination_root;
    std::string raw_relative_path;
    bool qualified_physical = false;
    bool pending_metadata_review = false;
    bool raw_batch_collision = false; // from the COMPLETE File locations batch
};

struct CueRawRouteFinding {
    std::size_t reference_index=0;
    std::size_t selected_source_row=cue_unselected_source_row;
    std::string code;
    std::string raw_candidate; // ONLY raw Title Formatting, never final FileOps
};

struct CueRawRouteImpact {
    // Graph means SOURCE correlation only; no field grants a final approval.
    bool source_graph_qualified=false;
    std::vector<CueRawRouteFinding> references;
    std::size_t raw_candidates=0;
    std::size_t selected_blocked=0;
    std::size_t unselected=0;
};

// All tracked source indices refer to the original captured batch order,
// never visible ListView rows, subsong indices, CUE filename strings or a
// sorted UI. The host must have reprobed sources, rules and FILE references
// before using this pure projection; final destination sanitation, relative
// CUE FILE names, collisions and writes remain a separate later gate.
inline CueRawRouteImpact inspect_cue_raw_route_impact(
    const std::vector<CueObservedFileSource>& file_sources,
    const CuePhysicalSelectionAudit& source_graph,
    const std::vector<CueRawRouteTrack>& captured_tracks) {
    CueRawRouteImpact out;
    if(file_sources.empty() || file_sources.size()>128 ||
       captured_tracks.size()>100000)
        return out;
    out.source_graph_qualified=source_graph.eligible &&
        source_graph.file_to_selected_row.size()==file_sources.size();
    out.references.reserve(file_sources.size());
    for(std::size_t i=0;i<file_sources.size();++i) {
        CueRawRouteFinding row;
        row.reference_index=i;
        row.code="CUE_SOURCE_GRAPH_UNQUALIFIED";
        if(!out.source_graph_qualified) {
            ++out.selected_blocked;
            out.references.push_back(std::move(row));
            continue;
        }
        const auto& file=file_sources[i];
        if(file.file_index!=i || !file.observed ||
           file.host_physical_key.empty() || file.source_guard.empty()) {
            row.code="CUE_FILE_SOURCE_STALE";
            ++out.selected_blocked;
            out.references.push_back(std::move(row));
            continue;
        }
        row.selected_source_row=source_graph.file_to_selected_row[i];
        if(row.selected_source_row==cue_unselected_source_row) {
            row.code="CUE_FILE_AUDIO_NOT_SELECTED";
            ++out.unselected;
        } else if(row.selected_source_row>=captured_tracks.size()) {
            row.code="CUE_SELECTED_ROW_STALE";
            ++out.selected_blocked;
        } else {
            const auto& track=captured_tracks[row.selected_source_row];
            if(!track.qualified_physical ||
               track.host_physical_key!=file.host_physical_key ||
               track.source_guard!=file.source_guard) {
                row.code="CUE_SELECTED_SOURCE_STALE";
                ++out.selected_blocked;
            } else if(track.pending_metadata_review) {
                row.code="CUE_ROUTE_PENDING_METADATA_REVIEW";
                ++out.selected_blocked;
            } else if(track.raw_batch_collision) {
                row.code="CUE_ROUTE_RAW_BATCH_COLLISION";
                ++out.selected_blocked;
            } else if(track.raw_relative_path.size()>2048 ||
                      track.destination_root.size()>4096 ||
                      !raw_destination_root_lexically_safe(track.destination_root) ||
                      !raw_relative_path_lexically_safe(track.raw_relative_path)) {
                row.code="CUE_ROUTE_RAW_PATH_UNSAFE_OR_EMPTY";
                ++out.selected_blocked;
            } else {
                row.code="CUE_ROUTE_RAW_CANDIDATE_ONLY";
                row.raw_candidate=track.destination_root+"\\"+track.raw_relative_path;
                ++out.raw_candidates;
            }
        }
        out.references.push_back(std::move(row));
    }
    return out;
}

} // namespace djmeta
