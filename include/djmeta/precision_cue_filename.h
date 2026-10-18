#pragma once
// Conservative Windows/foobar MP3 FILE basename validation, no I/O.
#include <cstddef>
#include <string_view>

namespace djmeta {
inline bool precision_cue_safe_mp3_basename(std::string_view s) noexcept {
    if (s.size() < 5 || s.size() > 255 || s.front() == ' ' || s.back() == ' ' ||
        s.front() == '.' || s.back() == '.') return false;
    for (unsigned char c : s) {
        if (c < 32 || c == 127 || c == '<' || c == '>' || c == ':' ||
            c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') return false;
    }
    auto lower = [](char c) -> char {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
    };
    const auto n = s.size();
    if (s[n - 4] != '.' || lower(s[n - 3]) != 'm' ||
        lower(s[n - 2]) != 'p' || s[n - 1] != '3') return false;
    auto base = s.substr(0, n - 4);
    const auto dot = base.find('.');
    if (dot != std::string_view::npos) base = base.substr(0, dot);
    while (!base.empty() && base.back() == ' ') base.remove_suffix(1);
    if (base.empty()) return false;
    auto eq = [&](std::string_view device) {
        if (base.size() != device.size()) return false;
        for (std::size_t i = 0; i < base.size(); ++i)
            if (lower(base[i]) != device[i]) return false;
        return true;
    };
    if (eq("con") || eq("prn") || eq("aux") || eq("nul") ||
        eq("conin$") || eq("conout$")) return false;
    if (base.size() == 4 && base[3] >= '1' && base[3] <= '9') {
        const auto prefix = base.substr(0, 3);
        if (prefix == "COM" || prefix == "LPT" ||
            (lower(prefix[0]) == 'c' && lower(prefix[1]) == 'o' && lower(prefix[2]) == 'm') ||
            (lower(prefix[0]) == 'l' && lower(prefix[1]) == 'p' && lower(prefix[2]) == 't')) return false;
    }
    return true;
}
} // namespace djmeta
