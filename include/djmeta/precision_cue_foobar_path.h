#pragma once
// Conservative foobar file:// MP3 URI to local Windows path conversion.
// Parsing is write-free; actual filesystem identity must be checked separately.
#include "djmeta/precision_cue_filename.h"
#include <optional>
#include <string>
#include <string_view>

namespace djmeta {
inline std::optional<std::string> precision_cue_foobar_local_windows_mp3_path(
    std::string_view uri) {
    constexpr std::string_view prefix = "file://";
    // foobar2000's real /context_command can supply a drive-absolute
    // filesystem path instead of its usual file:// URI. Accept ONLY those
    // two equivalent local forms. All later segment/filename checks and the
    // independent canonical/physical-file identity gates remain mandatory.
    if (uri.starts_with(prefix)) uri.remove_prefix(prefix.size());
    const auto drive = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    };
    const auto separator = [](char c) { return c == '\\' || c == '/'; };
    // Both foobar's file://C:\... and the conventional file:///C:/...
    if (uri.size() >= 4 && uri[0] == '/' && drive(uri[1]) &&
        uri[2] == ':' && separator(uri[3])) uri.remove_prefix(1);
    if (uri.size() < 8 || !drive(uri[0]) || uri[1] != ':' ||
        !separator(uri[2])) return std::nullopt;
    std::string path;
    path.reserve(uri.size());
    for (std::size_t i = 0; i < uri.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(uri[i]);
        if (c < 0x20 || c == 0x7f || c == '"' || c == '<' || c == '>' ||
            c == '?' || c == '*' || c == '|' || (c == ':' && i != 1))
            return std::nullopt;
        path.push_back(separator(static_cast<char>(c)) ? '\\' :
                       static_cast<char>(c));
    }
    std::size_t cursor = 3;
    while (cursor < path.size()) {
        const auto end = path.find('\\', cursor);
        const auto item = std::string_view(path).substr(
            cursor, end == std::string::npos ? std::string::npos : end - cursor);
        if (item.empty() || item == "." || item == ".." ||
            item.back() == ' ' || item.back() == '.') return std::nullopt;
        if (end == std::string::npos) break;
        cursor = end + 1;
    }
    if (path.empty() || path.back() == '\\') return std::nullopt;
    const auto slash = path.find_last_of('\\');
    if (slash == std::string::npos ||
        !precision_cue_safe_mp3_basename(
            std::string_view(path).substr(slash + 1))) return std::nullopt;
    return path;
}
} // namespace djmeta
