#pragma once
// Read-only native source integrity: distinct paths/names may be hardlinks
// to the same physical continuous-mix MP3, never two independent discs.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#endif
namespace djmeta {
enum class PrecisionCuePhysicalSelectionState {
    Qualified, InvalidSource, DuplicatePhysicalIdentity, AmbiguousBasename
};
struct PrecisionCuePhysicalSelectionEvidence {
    std::string basename;
    std::uint64_t volume_id = 0;
    std::uint64_t file_id = 0;
};
inline bool precision_cue_basenames_ambiguous(
        std::string_view a, std::string_view b) {
    if (a == b) return true;
#ifdef _WIN32
    try {
        const auto native = [](std::string_view s) {
            std::u8string bytes;
            bytes.reserve(s.size());
            for (unsigned char c : s) bytes.push_back(static_cast<char8_t>(c));
            return std::filesystem::path(bytes).wstring();
        };
        const auto left = native(a), right = native(b);
        const int order = CompareStringOrdinal(left.c_str(), -1,
                                                right.c_str(), -1, TRUE);
        return order == 0 || order == CSTR_EQUAL;
    } catch (...) { return true; }
#else
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        unsigned char x = static_cast<unsigned char>(a[i]);
        unsigned char y = static_cast<unsigned char>(b[i]);
        if (x >= 'A' && x <= 'Z') x += static_cast<unsigned char>('a' - 'A');
        if (y >= 'A' && y <= 'Z') y += static_cast<unsigned char>('a' - 'A');
        if (x != y) return false;
    }
    return true;
#endif
}
inline PrecisionCuePhysicalSelectionState
validate_precision_cue_physical_selection(
        const std::vector<PrecisionCuePhysicalSelectionEvidence>& sources) {
    using State = PrecisionCuePhysicalSelectionState;
    if (sources.empty() || sources.size() > 8) return State::InvalidSource;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const auto& s = sources[i];
        if (s.basename.empty() || s.volume_id == 0 ||
            s.volume_id == UINT64_MAX || s.file_id == 0 ||
            s.file_id == UINT64_MAX) return State::InvalidSource;
        for (std::size_t j = 0; j < i; ++j) {
            const auto& other = sources[j];
            if (s.volume_id == other.volume_id && s.file_id == other.file_id)
                return State::DuplicatePhysicalIdentity;
            if (precision_cue_basenames_ambiguous(s.basename, other.basename))
                return State::AmbiguousBasename;
        }
    }
    return State::Qualified;
}
} // namespace djmeta
