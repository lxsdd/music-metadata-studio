#pragma once
#include <string>
#include <string_view>

namespace djmeta {

// Presentation-only defaults: never an authorization, approval or source tag.
// A new workspace follows the profile's source order by default, but opens
// only its manual search mode. It performs no background network lookup.
struct WorkspaceDefaults {
    int opening_tab = 0;       // Find, Review, Files
    int online_provider = 3;   // First supported provider in saved preference order; manual override remains available
    int routing_profile = 0;   // Singles, Alben, Livesets
    bool show_whitespace = false;

    bool operator==(const WorkspaceDefaults&) const = default;
};

inline bool workspace_defaults_valid(const WorkspaceDefaults& s) noexcept {
    return s.opening_tab >= 0 && s.opening_tab <= 2 &&
           s.online_provider >= 0 && s.online_provider <= 3 &&
           s.routing_profile >= 0 && s.routing_profile <= 2;
}

inline std::string serialize_workspace_defaults(const WorkspaceDefaults& s) {
    if (!workspace_defaults_valid(s)) return {};
    std::string out = "v1|";
    out += static_cast<char>('0' + s.opening_tab);
    out += '|';
    out += static_cast<char>('0' + s.online_provider);
    out += '|';
    out += static_cast<char>('0' + s.routing_profile);
    out += '|';
    out += s.show_whitespace ? '1' : '0';
    return out;
}

inline WorkspaceDefaults parse_workspace_defaults(std::string_view raw) noexcept {
    // Damaged, unknown and future settings fall back without inheriting rights.
    if (raw.size() != 10 || raw.substr(0, 3) != "v1|" ||
        raw[4] != '|' || raw[6] != '|' || raw[8] != '|' ||
        raw[3] < '0' || raw[3] > '2' ||
        raw[5] < '0' || raw[5] > '3' ||
        raw[7] < '0' || raw[7] > '2' ||
        (raw[9] != '0' && raw[9] != '1'))
        return {};
    return {raw[3] - '0', raw[5] - '0', raw[7] - '0', raw[9] == '1'};
}

} // namespace djmeta
