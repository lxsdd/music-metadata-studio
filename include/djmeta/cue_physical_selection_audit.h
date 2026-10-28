#pragma once
// Host-observed external CUE FILE -> selected physical audio reconciliation.
// This is source-only evidence. It cannot supply or authorize final FileOps
// destination names, CUE byte writes, physical tags or overwrites.
#include <cstddef>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {
constexpr std::size_t cue_unselected_source_row =
    (std::numeric_limits<std::size_t>::max)();

struct CueObservedFileSource {
    std::size_t file_index=0;
    std::string host_physical_key;
    std::string source_guard;
    bool observed=false;
    // Real foobar filesystem canonical source path returned by the same
    // independently inspected physical FILE; not an inferred CUE filename.
    // Source-only audits can omit this, but the unified FilePlan MUST bind
    // it to the separately qualified FILE reference for every participant.
    std::string canonical_source_key{};
};
struct CueSelectedPhysicalSource {
    std::size_t source_row=0;
    std::string host_physical_key;
    std::string source_guard;
    bool qualified=false;
};
struct CuePhysicalSelectionAudit {
    bool eligible=false; // ONLY selected physical source mapping, not final CUE
    std::string rejection;
    std::vector<std::size_t> file_to_selected_row;
    std::size_t selected_matched=0;
    std::size_t unselected_files=0;
    std::size_t selected_unreferenced=0;
};

// Input must come from independent physical OS+foobar source observations,
// never filenames, subsong IDs, CUE ordinals or guessed path normalization.
// Every selected standalone audio MUST appear in this one CUE's FILE set;
// a mixed/unrelated selection is disclosed and blocked, never partly matched.
inline CuePhysicalSelectionAudit audit_cue_physical_selection(
    std::string_view cue_carrier_physical_key,
    const std::vector<CueObservedFileSource>& files,
    const std::vector<CueSelectedPhysicalSource>& selected) {
    const auto reject=[](const char* why) {
        CuePhysicalSelectionAudit out;out.rejection=why;return out;
    };
    if(cue_carrier_physical_key.empty() || files.empty() ||
       files.size()>128 || selected.size()>100000)
        return reject("CUE_SOURCE_SELECTION_SHAPE_UNQUALIFIED");

    std::map<std::string,const CueSelectedPhysicalSource*> by_physical;
    std::set<std::size_t> unique_rows;
    for(const auto& row:selected) {
        if(!row.qualified || row.host_physical_key.empty() ||
           row.source_guard.empty() ||
           !unique_rows.insert(row.source_row).second ||
           !by_physical.emplace(row.host_physical_key,&row).second ||
           row.host_physical_key==cue_carrier_physical_key)
            return reject("CUE_SELECTED_AUDIO_IDENTITY_AMBIGUOUS");
    }

    CuePhysicalSelectionAudit out;
    out.file_to_selected_row.reserve(files.size());
    std::set<std::string> seen_files;
    for(std::size_t i=0;i<files.size();++i) {
        const auto& file=files[i];
        if(file.file_index!=i || !file.observed ||
           file.host_physical_key.empty() || file.source_guard.empty())
            return reject("CUE_FILE_SOURCE_MISSING_OR_UNQUALIFIED");
        if(file.host_physical_key==cue_carrier_physical_key)
            return reject("CUE_FILE_ALIASES_CARRIER");
        if(!seen_files.insert(file.host_physical_key).second)
            return reject("CUE_FILE_DUPLICATE_PHYSICAL_SOURCE");
        const auto it=by_physical.find(file.host_physical_key);
        if(it==by_physical.end()) {
            ++out.unselected_files;
            out.file_to_selected_row.push_back(cue_unselected_source_row);
        } else {
            if(file.source_guard!=it->second->source_guard)
                return reject("CUE_SELECTED_SOURCE_VERSION_STALE");
            ++out.selected_matched;
            out.file_to_selected_row.push_back(it->second->source_row);
        }
    }
    out.selected_unreferenced=selected.size()-out.selected_matched;
    if(out.selected_unreferenced) {
        out.rejection="CUE_SELECTED_AUDIO_NOT_REFERENCED";
        return out;
    }
    out.eligible=true;
    return out;
}
} // namespace djmeta
