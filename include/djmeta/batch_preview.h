#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {

// Preliminary observation of the literal/raw candidate path only. It must
// never be confused with a foobar File Operations sanitized/final destination.
enum class RawTargetPresence { NotInspected, Missing, Existing, Unqualified };

// The table explicitly displays unqualified operations; raw Title Formatting
// paths are NOT proof of a valid final foobar File Operations destination.
struct BatchPreviewInputRow {
    std::string source_path;
    std::string physical_id;
    std::string profile;
    std::string destination_root;
    std::string raw_relative_path;
    RawTargetPresence raw_target_presence = RawTargetPresence::NotInspected;
    std::string source_physical_key; // genuine host-observed source identity
    std::string raw_target_physical_key; // genuine host-observed target identity, when existing
    std::string raw_target_guard; // observed target state, not overwrite approval
    bool physical_source_qualified = false;
    bool semantic_proposals_pending = false;
    bool cue_dependencies_checked = false;
    bool filesystem_target_checked = false;
};

struct BatchPreviewRow {
    std::string raw_destination;
    std::vector<std::string> issues; // ordered; first is most urgent
    bool has_raw_target_collision = false;
    RawTargetPresence raw_target_presence = RawTargetPresence::NotInspected;
};

struct BatchPreviewTable {
    std::vector<BatchPreviewRow> rows;
    std::size_t raw_collision_items = 0;
    std::size_t unqualified_rows = 0;
};

// Conservative lexical check of raw foobar Title Formatting output only.
// True means only that the relative fragment has no obvious Windows path
// traps; it is NOT proof of final foobar File Operations sanitization.
bool raw_relative_path_lexically_safe(std::string_view raw);
// Preliminary absolute Windows drive/UNC destination root validation.
// This does not assert host File Operations sanitation or approval.
bool raw_destination_root_lexically_safe(std::string_view root);

// Pure read-only table description; no filesystem I/O, no OS path identity
// assumptions, no use of this result as an executor approval token.
BatchPreviewTable describe_batch_preview(
    const std::vector<BatchPreviewInputRow>& items);

} // namespace djmeta
