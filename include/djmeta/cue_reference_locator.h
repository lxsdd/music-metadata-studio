#pragma once
// Read-only Studio File-organization preflight of EXISTING external CUE FILE
// paths. The path is a candidate to probe, not proof of physical association,
// canonical destination, file safety or any right to write.
#include "djmeta/structural_guard.h"
#include <string>
#include <string_view>

namespace djmeta {

struct CueSiblingLocator {
    std::string source_candidate;
    std::string reason; // stable, user-visible fail-closed diagnostic
    bool eligible = false;
};

inline CueSiblingLocator preview_cue_sibling_locator(
    std::string_view cue_locator, std::string_view raw_filename) {
    if (cue_locator.empty() || raw_filename.empty() ||
        raw_filename.size() > 1024 ||
        raw_filename == "." || raw_filename == "..")
        return {{}, "CUE_FILE_PATH_UNQUALIFIED", false};
    if (!valid_utf8_metadata_text(cue_locator) ||
        !valid_utf8_metadata_text(raw_filename))
        return {{}, "CUE_FILE_ENCODING_UNQUALIFIED", false};
    // Initially only same-directory FILE basenames are safely resolved.
    // No traversal, ADS/drive aliases, URI escapes, device paths, UNC
    // injection or wildcard targets. Nested directories need a separately
    // qualified host resolver rather than an unsafe lexical guess.
    constexpr std::string_view forbidden = "\\/:*?\"<>|%";
    for (const unsigned char c : raw_filename)
        if (c < 32 || c == 127 || forbidden.find(static_cast<char>(c)) !=
            std::string_view::npos)
            return {{}, "CUE_FILE_UNSAFE_OR_NESTED_PATH", false};
    if (raw_filename.back() == '.' || raw_filename.back() == ' ')
        return {{}, "CUE_FILE_AMBIGUOUS_WINDOWS_NAME", false};
    std::string stem;
    for (const char c : raw_filename) {
        if (c == '.') break;
        stem += c >= 'a' && c <= 'z'
            ? static_cast<char>(c - 'a' + 'A') : c;
    }
    // Win32 ignores spaces preceding an extension when resolving device
    // aliases (CON .flac) and reserves superscript COM/LPT digits as well.
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    const bool com_lpt = stem.starts_with("COM") || stem.starts_with("LPT");
    const auto suffix = stem.size() >= 3
        ? std::string_view(stem).substr(3) : std::string_view{};
    const bool device_digit =
        suffix.size() == 1 && suffix.front() >= '1' && suffix.front() <= '9';
    const bool device_superscript =
        suffix == "\xC2\xB9" || suffix == "\xC2\xB2" || suffix == "\xC2\xB3";
    if (stem=="CON" || stem=="PRN" || stem=="AUX" || stem=="NUL" ||
        (com_lpt && (device_digit || device_superscript)))
        return {{}, "CUE_FILE_RESERVED_DEVICE", false};
    const auto split = cue_locator.find_last_of("/\\");
    if (split == std::string_view::npos || split+1 >= cue_locator.size())
        return {{}, "CUE_PARENT_LOCATOR_UNQUALIFIED", false};
    return {std::string(cue_locator.substr(0,split+1)) +
            std::string(raw_filename), "CUE_FILE_HOST_PROBE_REQUIRED", true};
}

} // namespace djmeta
