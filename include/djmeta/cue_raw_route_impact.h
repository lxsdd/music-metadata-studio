#pragma once
// Existing external CUE FILE -> staged RAW route impact, for native read-only
// inspection. This is deliberately NOT a foobar File Operations path resolver
// or a CUE rewrite plan: those two final host gates remain unqualified.
#include "djmeta/batch_preview.h"
#include "djmeta/file_action_intent.h"
#include "djmeta/cue_physical_selection_audit.h"
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
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
    FileAction file_action = FileAction::Move; // explicit per-row native staging
};

struct CueRawRouteFinding {
    std::size_t reference_index=0;
    std::size_t selected_source_row=cue_unselected_source_row;
    std::string code;
    std::string raw_candidate; // ONLY raw Title Formatting, never final FileOps
    FileAction requested_action=FileAction::None; // advisory only
};

struct CueRawRouteImpact {
    // Graph means SOURCE correlation only; no field grants a final approval.
    bool source_graph_qualified=false;
    std::vector<CueRawRouteFinding> references;
    std::size_t raw_candidates=0;
    std::size_t selected_blocked=0;
    std::size_t unselected=0;
    std::size_t move_raw_candidates=0; // would require CUE FILE rewrite
    std::size_t copy_raw_candidates=0; // original CUE still points to original
    std::size_t kept_in_place=0;
    std::size_t rename_blocked=0;
    std::size_t partial_move_blocked=0; // mixed selected/unselected FILE graph
};

// Explanations name exactly what the action would imply for the original CUE.
inline std::string_view describe_cue_raw_route_code(std::string_view code) noexcept {
    if(code=="CUE_FILE_AUDIO_LEFT_IN_PLACE")
        return "No file action: original FILE link stays at its source";
    if(code=="CUE_AUDIO_COPY_RAW_ONLY_SOURCE_CUE_UNCHANGED")
        return "Copy: original FILE link stays on source; new sidecar links not approved";
    if(code=="CUE_AUDIO_MOVE_RAW_ONLY_REWRITE_UNQUALIFIED")
        return "Move: original CUE FILE link would need a qualified rewrite";
    if(code=="CUE_MOVE_PARTIAL_DEPENDENCIES_UNQUALIFIED")
        return "Move blocked: other FILE references are outside selected batch";
    if(code=="CUE_RENAME_PARENT_UNVERIFIED")
        return "Rename blocked: same-parent final foobar destination unverified";
    if(code=="CUE_ROUTE_PENDING_METADATA_REVIEW")
        return "Pending metadata decision prevents a valid route preview";
    if(code=="CUE_ROUTE_RAW_BATCH_COLLISION")
        return "Raw target conflicts with another captured batch source";
    if(code=="CUE_ROUTE_RAW_PATH_UNSAFE_OR_EMPTY")
        return "Unsafe or absent raw destination path";
    if(code=="CUE_SELECTED_SOURCE_STALE")
        return "Selected physical source or version changed";
    if(code=="CUE_FILE_AUDIO_NOT_SELECTED")
        return "FILE source is not in captured physical audio selection";
    if(code=="CUE_UNKNOWN_FILE_ACTION")
        return "Unknown requested file action is blocked";
    return code;
}

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
    const bool exact_shape=source_graph.file_to_selected_row.size()==file_sources.size();
    const auto not_selected=exact_shape
        ? static_cast<std::size_t>(std::count(
            source_graph.file_to_selected_row.begin(),
            source_graph.file_to_selected_row.end(),
            cue_unselected_source_row))
        : std::size_t{0};
    out.source_graph_qualified=source_graph.eligible && exact_shape &&
        source_graph.unselected_files==not_selected &&
        source_graph.selected_matched==file_sources.size()-not_selected;
    const bool partial_selection=not_selected!=0;
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
            row.requested_action=track.file_action;
            if(!track.qualified_physical ||
               track.host_physical_key!=file.host_physical_key ||
               track.source_guard!=file.source_guard) {
                row.code="CUE_SELECTED_SOURCE_STALE";
                ++out.selected_blocked;
            } else if(track.file_action==FileAction::None) {
                // No file movement; CUE's existing audio FILE token remains
                // untouched even if metadata elsewhere still needs review.
                row.code="CUE_FILE_AUDIO_LEFT_IN_PLACE";
                ++out.kept_in_place;
            } else if(track.file_action!=FileAction::Move &&
                      track.file_action!=FileAction::Copy &&
                      track.file_action!=FileAction::Rename) {
                row.code="CUE_UNKNOWN_FILE_ACTION";
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
            } else if(track.file_action==FileAction::Rename) {
                row.code="CUE_RENAME_PARENT_UNVERIFIED";
                ++out.rename_blocked;
                ++out.selected_blocked;
            } else if(track.file_action==FileAction::Move && partial_selection) {
                // CUE has FILEs outside our batch: moving just the selected
                // source requires an explicit mixed-graph CUE sidecar plan.
                row.code="CUE_MOVE_PARTIAL_DEPENDENCIES_UNQUALIFIED";
                ++out.partial_move_blocked;
                ++out.selected_blocked;
            } else {
                row.raw_candidate=track.destination_root+"\\"+track.raw_relative_path;
                row.code=track.file_action==FileAction::Move
                    ? "CUE_AUDIO_MOVE_RAW_ONLY_REWRITE_UNQUALIFIED"
                    : "CUE_AUDIO_COPY_RAW_ONLY_SOURCE_CUE_UNCHANGED";
                if(track.file_action==FileAction::Move)
                    ++out.move_raw_candidates;
                else
                    ++out.copy_raw_candidates;
                ++out.raw_candidates;
            }
        }
        out.references.push_back(std::move(row));
    }
    return out;
}


// This read-only CUE carrier/sidecar decision uses the EXISTING native
// FileAction staging of the CUE file. No extra default move, second renderer,
// filename sanitizer, CUE writer or execution token is introduced.
struct CueSidecarRawImpact {
    std::string code="CUE_SIDECAR_UNQUALIFIED";
    bool raw_only_candidate=false; // never write approval
    std::string raw_candidate; // UI-only, not host-final FileOps
};
// Every external CUE has its OWN carrier action and source snapshot.
// Explicit staging is mandatory because the native default Move is a template,
// not authorization to relocate a sidecar.
inline CueSidecarRawImpact inspect_cue_sidecar_raw_action(
    const CueRawRouteImpact& audio,
    FileAction sidecar_action,bool explicitly_staged,
    bool physical_carrier_qualified,bool pending_cue_metadata,
    bool carrier_raw_collision,std::string_view root,
    std::string_view raw_relative_name) {
    const auto reject=[](const char* code) {
        CueSidecarRawImpact result;result.code=code;return result;
    };
    if(!audio.source_graph_qualified || audio.references.empty() ||
       audio.references.size()>128)
        return reject("CUE_SIDECAR_SOURCE_GRAPH_UNQUALIFIED");
    if(!physical_carrier_qualified)
        return reject("CUE_SIDECAR_PHYSICAL_CARRIER_UNQUALIFIED");
    if(!explicitly_staged)
        return reject("CUE_SIDECAR_ACTION_NOT_EXPLICITLY_STAGED");
    if(sidecar_action==FileAction::None) {
        // Leaving a CUE behind while selected audio moves or renames can
        // orphan its FILE tokens even when a move is still preliminarily
        // blocked. Copies preserve original sources/links.
        for(const auto& row:audio.references)
            if(row.requested_action==FileAction::Move ||
               row.requested_action==FileAction::Rename)
                return reject("CUE_SIDECAR_LEFT_WITH_MOVED_AUDIO_RISK");
        return reject("CUE_SIDECAR_AND_ORIGINAL_FILE_LINKS_UNCHANGED");
    }
    if(sidecar_action!=FileAction::Move &&
       sidecar_action!=FileAction::Copy &&
       sidecar_action!=FileAction::Rename)
        return reject("CUE_SIDECAR_ACTION_INVALID");
    if(sidecar_action==FileAction::Rename)
        return reject("CUE_SIDECAR_RENAME_FINAL_PARENT_UNQUALIFIED");
    if(pending_cue_metadata)
        return reject("CUE_SIDECAR_CUE_VALUES_PENDING");
    if(carrier_raw_collision)
        return reject("CUE_SIDECAR_RAW_BATCH_COLLISION");
    if(audio.unselected!=0 || audio.selected_blocked!=0)
        return reject("CUE_SIDECAR_INCOMPLETE_OR_BLOCKED_AUDIO_GRAPH");
    for(const auto& row:audio.references) {
        if(row.selected_source_row==cue_unselected_source_row ||
           row.requested_action!=sidecar_action ||
           row.raw_candidate.empty())
            return reject("CUE_SIDECAR_AUDIO_ACTIONS_MIXED_OR_UNQUALIFIED");
        if(sidecar_action==FileAction::Move &&
           row.code!="CUE_AUDIO_MOVE_RAW_ONLY_REWRITE_UNQUALIFIED")
            return reject("CUE_SIDECAR_AUDIO_ACTIONS_MIXED_OR_UNQUALIFIED");
        if(sidecar_action==FileAction::Copy &&
           row.code!="CUE_AUDIO_COPY_RAW_ONLY_SOURCE_CUE_UNCHANGED")
            return reject("CUE_SIDECAR_AUDIO_ACTIONS_MIXED_OR_UNQUALIFIED");
    }
    if(root.size()>4096 || raw_relative_name.size()>2048 ||
       !raw_destination_root_lexically_safe(root) ||
       !raw_relative_path_lexically_safe(raw_relative_name))
        return reject("CUE_SIDECAR_RAW_CARRIER_PATH_UNQUALIFIED");
    CueSidecarRawImpact out;
    out.code=sidecar_action==FileAction::Move
        ? "CUE_SIDECAR_MOVE_RAW_ONLY_REWRITE_AND_FINAL_CUE_LINKS_UNQUALIFIED"
        : "CUE_SIDECAR_COPY_RAW_ONLY_COPIED_LINKS_UNQUALIFIED";
    out.raw_only_candidate=true;
    out.raw_candidate=std::string(root)+"\\"+std::string(raw_relative_name);
    return out;
}

inline std::string_view describe_cue_sidecar_action(std::string_view code) noexcept {
    if(code=="CUE_SIDECAR_ACTION_NOT_EXPLICITLY_STAGED")
        return "Choose a separate file action for the CUE carrier in File locations";
    if(code=="CUE_SIDECAR_LEFT_WITH_MOVED_AUDIO_RISK")
        return "Leaving original CUE behind could orphan FILE references after an audio move";
    if(code=="CUE_SIDECAR_AND_ORIGINAL_FILE_LINKS_UNCHANGED")
        return "CUE carrier and original FILE targets stay at their sources";
    if(code=="CUE_SIDECAR_MOVE_RAW_ONLY_REWRITE_AND_FINAL_CUE_LINKS_UNQUALIFIED")
        return "CUE move candidate requires full host-final FILE postimage and source/target checks";
    if(code=="CUE_SIDECAR_COPY_RAW_ONLY_COPIED_LINKS_UNQUALIFIED")
        return "CUE copy candidate requires independent relative links to copied audio";
    if(code=="CUE_SIDECAR_AUDIO_ACTIONS_MIXED_OR_UNQUALIFIED")
        return "Sidecar action conflicts with one or more physical audio actions";
    if(code=="CUE_SIDECAR_INCOMPLETE_OR_BLOCKED_AUDIO_GRAPH")
        return "Sidecar cannot follow an incomplete, unselected or blocked CUE FILE graph";
    if(code=="CUE_SIDECAR_RENAME_FINAL_PARENT_UNQUALIFIED")
        return "Rename requires host-final same-parent foobar File Operations target";
    return code;
}
} // namespace djmeta
