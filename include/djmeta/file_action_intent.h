#pragma once
// Explicit session-only native FileOps intent built on existing FileAction.
// NO file destination finalization, executor, filesystem service or writer.
#include "djmeta/batch_plan.h"
#include "djmeta/batch_preview.h"
#include <string_view>

namespace djmeta {
inline constexpr std::string_view file_action_label(FileAction action) noexcept {
    switch(action) {
    case FileAction::None: return "No file action";
    case FileAction::Move: return "Move";
    case FileAction::Copy: return "Copy";
    case FileAction::Rename: return "Rename";
    default: return "Invalid";
    }
}
struct FileActionIntent {
    bool raw_candidate=false;
    bool file_skip=false;
    bool blocked=true;
    const char* reason="UNKNOWN_FILE_ACTION";
};
// Presentation-only; the existing batch table remains the sole raw collision
// evaluator. No source+target equality or sanitizer is inferred from strings.
inline FileActionIntent inspect_file_action_intent(
    FileAction action, const BatchPreviewInputRow& source,
    const BatchPreviewRow& raw) noexcept {
    if(action==FileAction::None)
        return {false,true,false,"NO_FILE_OPERATION"};
    if(action!=FileAction::Move && action!=FileAction::Copy &&
       action!=FileAction::Rename)
        return {false,false,true,"UNKNOWN_FILE_ACTION"};
    if(!source.physical_source_qualified ||
       source.source_physical_key.empty())
        return {false,false,true,"PHYSICAL_SOURCE_UNQUALIFIED"};
    if(source.semantic_proposals_pending)
        return {false,false,true,"PENDING_METADATA_REVIEW"};
    if(raw.has_raw_target_collision)
        return {false,false,true,"RAW_BATCH_COLLISION"};
    if(source.destination_root.size()>4096 ||
       source.raw_relative_path.size()>2048 ||
       !raw_destination_root_lexically_safe(source.destination_root) ||
       !raw_relative_path_lexically_safe(source.raw_relative_path))
        return {false,false,true,"UNSAFE_OR_EMPTY_RAW_ROUTE"};
    // Rename requires a SAME DIRECTORY final destination; a Title Formatting
    // string does not prove it. Host FileOps + filesystem source/target/CUE
    // checks remain required for Move/Copy too.
    if(action==FileAction::Rename)
        return {false,false,true,"RENAME_PARENT_UNVERIFIED"};
    return {true,false,false,"RAW_ONLY_FINAL_FILEOPS_CUE_UNQUALIFIED"};
}
} // namespace djmeta
