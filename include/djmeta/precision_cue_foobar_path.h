#pragma once
// Conservative foobar file:// MP3 URI to local Windows path conversion.
// Parsing is write-free; actual filesystem identity must be checked separately.
#include "djmeta/precision_cue_filename.h"
#include <optional>
#include <string>
#include <string_view>

namespace djmeta {
// Scheme comparison is ASCII case-insensitive (RFC 3986). Only file:// is
// admitted; all other schemes, relative paths and UNC shares remain rejected.
inline bool precision_cue_foobar_file_scheme(std::string_view uri) noexcept {
    if (uri.size() < 7 || uri.substr(4, 3) != "://") return false;
    constexpr std::string_view name = "file";
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = uri[i];
        if ((c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c)
            != name[i]) return false;
    }
    return true;
}

// Privacy-preserving diagnostics: never return source path or basename.
// Only fixed, non-identifying scheme categories are reported in public CI logs.
// This does NOT authorize opening a virtual, remote or unsupported URI.
inline std::string_view precision_cue_foobar_scheme_kind(
    std::string_view uri) noexcept {
    const auto delimiter = uri.find("://");
    if (delimiter == std::string_view::npos) return "none";
    const auto scheme = uri.substr(0, delimiter);
    if (scheme == "unpack") return "virtual-unpack";
    if (scheme == "http" || scheme == "https") return "remote-http";
    if (scheme == "file" || scheme == "FILE") return "local-file";
    if (scheme == "zip" || scheme == "rar") return "virtual-archive";
    return "unrecognized";
}

// Privacy-preserving diagnostics: never return source path or basename.
// Distinguishes foobar CLI path representations without broadening admission.
// A URI scheme is a protocol identifier, never its authority/path/query.
// Diagnostics deliberately cap it at 18 RFC-style ASCII characters; user
// filenames, hosts, usernames, tokens or directory data cannot reach CI logs.
inline std::string precision_cue_foobar_safe_scheme_token(
    std::string_view uri) {
    const auto delimiter=uri.find("://");
    if(delimiter==std::string_view::npos || delimiter==0 || delimiter>18)
        return "unknown";
    for(std::size_t i=0;i<delimiter;++i) {
        const unsigned char c=static_cast<unsigned char>(uri[i]);
        const bool letter=(c>='a'&&c<='z')||(c>='A'&&c<='Z');
        const bool digit=c>='0'&&c<='9';
        if(!letter && (i==0 || (!digit && c!='+' && c!='-' && c!='.')))
            return "unknown";
    }
    return std::string(uri.substr(0,delimiter));
}

inline std::string_view precision_cue_foobar_path_form(std::string_view uri) noexcept {
    const auto drive = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    };
    const auto sep = [](char c) { return c == '/' || c == '\\'; };
    if (precision_cue_foobar_file_scheme(uri)) return "file-uri";
    if (uri.size() >= 4 && sep(uri[0]) && sep(uri[1]) &&
        (uri[2] == '?' || uri[2] == '.') && sep(uri[3]))
        return "extended-device-path";
    if (uri.size() >= 2 && sep(uri[0]) && sep(uri[1])) return "unc-path";
    if (uri.size() >= 3 && drive(uri[0]) && uri[1] == ':' && sep(uri[2]))
        return "drive-absolute";
    if (uri.size() >= 4 && uri[0] == '/' && drive(uri[1]) &&
        uri[2] == ':' && sep(uri[3])) return "slash-drive-absolute";
    if (uri.size() >= 5) {
        constexpr std::string_view name = "file";
        bool is_file = true;
        for (std::size_t i = 0; i < name.size(); ++i) {
            const char c = uri[i];
            if ((c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c)
                != name[i]) is_file = false;
        }
        if (is_file && uri[4] == ':') return "malformed-file-uri";
    }
    if (!uri.empty() && sep(uri[0])) return "rooted-no-drive";
    if (uri.find("://") != std::string_view::npos) return "other-uri";
    return "relative-or-unknown";
}

inline std::optional<std::string> precision_cue_foobar_local_windows_mp3_path(
    std::string_view uri) {
    constexpr std::string_view prefix = "file://";
    // foobar2000's real /context_command can supply a drive-absolute
    // filesystem path instead of its usual file:// URI. Accept ONLY those
    // two equivalent local forms. All later segment/filename checks and the
    // independent canonical/physical-file identity gates remain mandatory.
    if (precision_cue_foobar_file_scheme(uri)) uri.remove_prefix(prefix.size());
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
