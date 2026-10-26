#include "djmeta/batch_preview.h"
#include "djmeta/structural_guard.h"

#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <string>
#include <vector>

namespace djmeta {
namespace {

bool ascii_reserved_windows_device(std::string_view component) {
    // Windows rejects device aliases even with a filename extension.
    // Compare only the base before the first dot using ASCII case folding.
    const auto dot = component.find('.');
    auto base = component.substr(0, dot);
    // Win32 also treats trailing spaces immediately before an extension
    // as insignificant for device aliases, e.g. "CON .mp3".
    while (!base.empty() && base.back() == ' ') base.remove_suffix(1);
    std::string up;
    up.reserve(base.size());
    for (char ch : base)
        up.push_back(ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch);
    if (up == "CON" || up == "PRN" || up == "AUX" || up == "NUL") return true;
    if (up.size() < 4 || (up.substr(0, 3) != "COM" &&
                           up.substr(0, 3) != "LPT")) return false;
    if (up.size() == 4 && up[3] >= '1' && up[3] <= '9') return true;
    // Windows reserves the ISO-8859-1 superscript digits as COM/LPT
    // suffixes too. Our metadata is validated UTF-8 before this point.
    const std::string_view suffix(up.data() + 3, up.size() - 3);
    return suffix == "\xC2\xB9" || suffix == "\xC2\xB2" ||
           suffix == "\xC2\xB3";
}

// A *warning* for Windows-style raw Title Formatting destinations,
// not a substitute for foobar's canonical File Operations target or an
// authoritative filesystem identity (notably for non-ASCII case folding).
std::string preliminary_windows_spelling_key(std::string_view path) {
    std::string result;
    result.reserve(path.size());
    for (unsigned char byte : path) {
        if (byte == '/') byte = '\\';
        if (byte >= 'a' && byte <= 'z')
            byte = static_cast<unsigned char>(byte - ('a' - 'A'));
        result.push_back(static_cast<char>(byte));
    }
    return result;
}

// Raw Title Formatting may contain well-formed UTF-8 that changes how a
// filename is *displayed* without changing its bytes. Treat C1 controls and
// invisible bidirectional overrides/isolates as unqualified path evidence;
// this does not reject ordinary non-ASCII or right-to-left letters.
bool unsafe_invisible_path_control(std::string_view component) {
    for (std::size_t i = 0; i < component.size(); ++i) {
        const auto first = static_cast<unsigned char>(component[i]);
        if (first == 0xc2 && i + 1 < component.size()) {
            const auto second = static_cast<unsigned char>(component[i + 1]);
            if (second >= 0x80 && second <= 0x9f) return true; // U+0080..009F
        }
        if (first == 0xe2 && i + 2 < component.size()) {
            const auto second = static_cast<unsigned char>(component[i + 1]);
            const auto third = static_cast<unsigned char>(component[i + 2]);
            if (second == 0x80 &&
                (third == 0x8e || third == 0x8f ||
                 (third >= 0xaa && third <= 0xae)))
                return true; // LRM/RLM and U+202A..202E overrides
            if (second == 0x81 && third >= 0xa6 && third <= 0xa9)
                return true; // U+2066..2069 directional isolates
        }
    }
    return false;
}

bool safe_component(std::string_view component) {
    if (component.empty() || component == "." || component == ".." ||
        component.back() == '.' || component.back() == ' ' ||
        ascii_reserved_windows_device(component) ||
        unsafe_invisible_path_control(component)) return false;
    for (unsigned char ch : component) {
        if (ch < 0x20 || ch == 0x7f || ch == '<' || ch == '>' ||
            ch == ':' || ch == '\"' || ch == '|' || ch == '?' || ch == '*')
            return false;
    }
    return true;
}

} // namespace

bool raw_relative_path_lexically_safe(std::string_view raw) {
    if (raw.empty() || raw.size() > 32767 || !valid_utf8_metadata_text(raw))
        return false;
    std::size_t first = 0;
    while (first < raw.size()) {
        const auto separator = raw.find_first_of("/\\\\", first);
        const auto last = separator == std::string_view::npos ? raw.size() : separator;
        if (!safe_component(raw.substr(first, last - first))) return false;
        if (separator == std::string_view::npos) return true;
        first = separator + 1;
    }
    // A trailing slash is not a finished filename.
    return false;
}

bool raw_destination_root_lexically_safe(std::string_view root) {
    if (root.empty() || root.size() > 32767 || !valid_utf8_metadata_text(root))
        return false;
    const auto separator = [](char c) { return c == '\\' || c == '/'; };
    std::size_t first = 0;
    bool unc = false;
    if (root.size() >= 3 &&
        ((root[0] >= 'A' && root[0] <= 'Z') ||
         (root[0] >= 'a' && root[0] <= 'z')) &&
        root[1] == ':' && separator(root[2])) {
        first = 3; // Drive absolute: Z:\Music
    } else if (root.size() >= 5 && separator(root[0]) &&
               separator(root[1]) && !separator(root[2])) {
        first = 2; // UNC: \\server\share\optional-subfolders
        unc = true;
    } else {
        return false; // drive-relative, root-relative, device namespaces
    }
    std::size_t components = 0;
    while (first < root.size()) {
        const auto end = root.find_first_of("/\\", first);
        const auto last = end == std::string_view::npos ? root.size() : end;
        if (!safe_component(root.substr(first, last - first))) return false;
        ++components;
        if (end == std::string_view::npos) break;
        first = end + 1;
        // Permit one trailing slash, never empty interior components.
        if (first == root.size()) break;
    }
    return !unc || components >= 2; // UNC requires server and share
}

BatchPreviewTable describe_batch_preview(
    const std::vector<BatchPreviewInputRow>& items) {
    BatchPreviewTable out;
    out.rows.resize(items.size());
    std::map<std::string, std::size_t> counts;
    std::map<std::string, std::size_t> raw_windows_spelling_counts;
    std::map<std::string, std::size_t> observed_target_key_counts;
    std::map<std::string, std::size_t> observed_canonical_counts;
    std::set<std::string> observed_source_keys;
    for (const auto& item : items) {
        if (item.physical_source_qualified && !item.source_physical_key.empty())
            observed_source_keys.insert(item.source_physical_key);
        if (item.raw_target_presence == RawTargetPresence::Existing &&
            !item.raw_target_physical_key.empty() && !item.raw_target_guard.empty())
            ++observed_target_key_counts[item.raw_target_physical_key];
        if ((item.raw_target_presence == RawTargetPresence::Existing ||
             item.raw_target_presence == RawTargetPresence::Missing) &&
            !item.raw_target_host_canonical.empty())
            ++observed_canonical_counts[item.raw_target_host_canonical];
    }
    // Deliberately raw, case-sensitive counts only. Windows canonicalization
    // remains a responsibility of the future foobar host filesystem planner.
    for (const auto& item : items) {
        // Report raw target conflicts for every displayed candidate, even
        // when one source is independently unqualified. That row remains
        // blocked, but its target must not hide a collision from another row.
        if (!item.destination_root.empty() && !item.raw_relative_path.empty()) {
            const std::string candidate = item.destination_root + "\\" +
                item.raw_relative_path;
            ++counts[candidate];
            ++raw_windows_spelling_counts[preliminary_windows_spelling_key(candidate)];
        }
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        auto& row = out.rows[i];
        row.raw_target_presence = item.raw_target_presence;
        if (!item.destination_root.empty() && !item.raw_relative_path.empty())
            row.raw_destination =
                item.destination_root + "\\" + item.raw_relative_path;
        if (!item.physical_source_qualified)
            row.issues.emplace_back("PHYSICAL_SOURCE_UNQUALIFIED");
        if (!raw_destination_root_lexically_safe(item.destination_root))
            row.issues.emplace_back("UNSAFE_RAW_DESTINATION_ROOT");
        if (row.raw_destination.empty())
            row.issues.emplace_back("TARGET_EXPRESSION_EMPTY");
        else if (!raw_relative_path_lexically_safe(item.raw_relative_path))
            row.issues.emplace_back("UNSAFE_RAW_RELATIVE_TARGET");
        if (!row.raw_destination.empty() && counts[row.raw_destination] > 1) {
            row.has_raw_target_collision = true;
            row.issues.emplace_back("DUPLICATE_RAW_TARGET");
            ++out.raw_collision_items;
        } else if (!row.raw_destination.empty() &&
                   raw_windows_spelling_counts[
                       preliminary_windows_spelling_key(row.raw_destination)] > 1) {
            row.has_raw_target_collision = true;
            row.issues.emplace_back("RAW_TARGET_CASE_OR_SEPARATOR_COLLISION");
            ++out.raw_collision_items;
        }
        // Compare foobar-canonical observations IN ADDITION to raw filename
        // spelling and existing physical IDs. Even two absent destinations
        // may map to the same canonical raw candidate. This does not predict
        // File Operations' eventual post-sanitization output.
        if ((item.raw_target_presence == RawTargetPresence::Existing ||
             item.raw_target_presence == RawTargetPresence::Missing) &&
            !item.raw_target_host_canonical.empty() &&
            observed_canonical_counts[item.raw_target_host_canonical] > 1) {
            if (!row.has_raw_target_collision) {
                row.has_raw_target_collision = true;
                ++out.raw_collision_items;
            }
            row.issues.emplace_back("RAW_TARGET_SHARED_HOST_CANONICAL");
        }
        // These findings concern the unsanitized raw candidate only. A
        // real host-resolved destination may be different and MUST receive
        // its own independent preflight before any future file operation.
        switch (item.raw_target_presence) {
        case RawTargetPresence::NotInspected:
            if (!item.raw_target_physical_key.empty() ||
                !item.raw_target_guard.empty() ||
                !item.raw_target_host_canonical.empty())
                row.issues.emplace_back("RAW_TARGET_OBSERVATION_CONTRADICTORY");
            break;
        case RawTargetPresence::Missing:
            if (!item.raw_target_physical_key.empty() ||
                !item.raw_target_guard.empty())
                row.issues.emplace_back("RAW_TARGET_OBSERVATION_CONTRADICTORY");
            if (item.raw_target_host_canonical.empty())
                row.issues.emplace_back("RAW_TARGET_HOST_CANONICAL_UNQUALIFIED");
            break;
        case RawTargetPresence::Existing:
            if (item.raw_target_host_canonical.empty())
                row.issues.emplace_back("RAW_TARGET_HOST_CANONICAL_UNQUALIFIED");
            if (item.raw_target_physical_key.empty() || item.raw_target_guard.empty())
                row.issues.emplace_back("RAW_TARGET_PROBE_UNQUALIFIED");
            else if (!item.source_physical_key.empty() &&
                     item.source_physical_key == item.raw_target_physical_key)
                row.issues.emplace_back("RAW_TARGET_ALIASES_SOURCE");
            else if (observed_source_keys.count(item.raw_target_physical_key) != 0)
                row.issues.emplace_back("RAW_TARGET_IS_BATCH_SOURCE");
            else if (observed_target_key_counts[item.raw_target_physical_key] > 1)
                row.issues.emplace_back("RAW_TARGET_SHARED_PHYSICAL_ID");
            else
                row.issues.emplace_back("RAW_TARGET_EXISTS");
            break;
        case RawTargetPresence::Unqualified:
        default:
            if (!item.raw_target_physical_key.empty() ||
                !item.raw_target_guard.empty() ||
                !item.raw_target_host_canonical.empty())
                row.issues.emplace_back("RAW_TARGET_OBSERVATION_CONTRADICTORY");
            row.issues.emplace_back("RAW_TARGET_PROBE_UNQUALIFIED");
            break;
        }
        if (item.semantic_proposals_pending)
            row.issues.emplace_back("UNAPPROVED_METADATA_PROPOSALS");
        if (!item.cue_dependencies_checked)
            row.issues.emplace_back("CUE_DEPENDENCIES_UNCHECKED");
        if (!item.filesystem_target_checked)
            row.issues.emplace_back("FILESYSTEM_TARGET_UNCHECKED");
        if (!row.issues.empty())
            ++out.unqualified_rows;
    }
    return out;
}

bool batch_location_queue_matches(
    const BatchPreviewRow& row, BatchLocationQueue queue) noexcept {
    const auto has=[&](std::string_view code) {
        for(const auto& issue:row.issues) if(issue==code) return true;
        return false;
    };
    switch(queue) {
    case BatchLocationQueue::All: return true;
    case BatchLocationQueue::RawCollisions: return row.has_raw_target_collision;
    case BatchLocationQueue::RawUnsafe:
        return has("PHYSICAL_SOURCE_UNQUALIFIED") ||
            has("TARGET_EXPRESSION_EMPTY") ||
            has("UNSAFE_RAW_DESTINATION_ROOT") ||
            has("UNSAFE_RAW_RELATIVE_TARGET") ||
            has("RAW_TARGET_OBSERVATION_CONTRADICTORY") ||
            has("RAW_TARGET_PROBE_UNQUALIFIED") ||
            has("RAW_TARGET_HOST_CANONICAL_UNQUALIFIED") ||
            has("RAW_TARGET_ALIASES_SOURCE") ||
            has("RAW_TARGET_IS_BATCH_SOURCE") ||
            has("RAW_TARGET_SHARED_PHYSICAL_ID") ||
            has("RAW_TARGET_SHARED_HOST_CANONICAL");
    case BatchLocationQueue::ExistingRaw:
        return row.raw_target_presence==RawTargetPresence::Existing;
    case BatchLocationQueue::UninspectedRaw:
        return row.raw_target_presence==RawTargetPresence::NotInspected;
    case BatchLocationQueue::MissingRaw:
        return row.raw_target_presence==RawTargetPresence::Missing;
    case BatchLocationQueue::PendingMetadata:
        return has("UNAPPROVED_METADATA_PROPOSALS");
    case BatchLocationQueue::NoRawFindings:
        if(row.has_raw_target_collision) return false;
        for(const auto& issue:row.issues)
            if(issue!="FILESYSTEM_TARGET_UNCHECKED" &&
               issue!="CUE_DEPENDENCIES_UNCHECKED" &&
               issue!="UNAPPROVED_METADATA_PROPOSALS") return false;
        return true;
    default: return false;
    }
}
std::vector<std::size_t> filter_batch_location_view(
    const BatchPreviewTable& complete_table,
    const std::vector<std::size_t>& sorted_source_ids,
    BatchLocationQueue queue) {
    if(queue<BatchLocationQueue::All || queue>BatchLocationQueue::NoRawFindings)
        throw std::invalid_argument("Unknown File locations queue.");
    std::vector<unsigned char> seen(complete_table.rows.size(),0);
    std::vector<std::size_t> visible;
    visible.reserve(sorted_source_ids.size());
    for(const auto index:sorted_source_ids) {
        if(index>=seen.size() || seen[index])
            throw std::invalid_argument("File locations source view is invalid.");
        seen[index]=1;
        if(batch_location_queue_matches(complete_table.rows[index],queue))
            visible.push_back(index);
    }
    return visible;
}
} // namespace djmeta
