#pragma once
// Versioned foobar-profile window geometry. Pure input validation; no registry,
// filesystem or media access. Monitor work areas are supplied by the Win32 host.
#include <algorithm>
#include <array>
#include <charconv>
#include <string>
#include <string_view>
#include <system_error>

namespace djmeta {

struct PrecisionCueWindowGeometry {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool maximized = false;
};

struct PrecisionCueWorkArea {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

inline std::string encode_precision_cue_window_geometry(
    const PrecisionCueWindowGeometry& v) {
    return "v1|" + std::to_string(v.x) + "|" + std::to_string(v.y) +
        "|" + std::to_string(v.width) + "|" + std::to_string(v.height) +
        (v.maximized ? "|1" : "|0");
}

inline bool parse_precision_cue_window_geometry(
    std::string_view text, PrecisionCueWindowGeometry& out) noexcept {
    std::array<std::string_view, 6> parts{};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto pos = text.find('|');
        if (i < parts.size() - 1 && pos == std::string_view::npos)
            return false;
        if (i == parts.size() - 1 && pos != std::string_view::npos)
            return false;
        parts[i] = text.substr(0, pos);
        if (pos != std::string_view::npos) text.remove_prefix(pos + 1);
    }
    if (parts[0] != "v1") return false;
    std::array<int, 5> numbers{};
    for (std::size_t i = 0; i < numbers.size(); ++i) {
        const auto segment = parts[i + 1];
        if (segment.empty()) return false;
        const auto [end, error] = std::from_chars(
            segment.data(), segment.data() + segment.size(), numbers[i]);
        if (error != std::errc{} || end != segment.data() + segment.size())
            return false;
    }
    if (numbers[0] < -100000 || numbers[0] > 100000 ||
        numbers[1] < -100000 || numbers[1] > 100000 ||
        numbers[2] < 400 || numbers[2] > 10000 ||
        numbers[3] < 300 || numbers[3] > 10000 ||
        (numbers[4] != 0 && numbers[4] != 1))
        return false;
    out = {numbers[0], numbers[1], numbers[2], numbers[3], numbers[4] == 1};
    return true;
}

// Keep the restored window fully inside a present monitor's work area.
// If the monitor was disconnected, Win32 supplies the nearest active one.
// Min track size is only an ideal target when the active monitor fits it.
inline bool fit_precision_cue_window_to_work_area(
    PrecisionCueWindowGeometry& v, const PrecisionCueWorkArea& work,
    int min_width, int min_height) noexcept {
    const auto ww = static_cast<long long>(work.right) - work.left;
    const auto wh = static_cast<long long>(work.bottom) - work.top;
    if (ww <= 0 || wh <= 0 || ww > 100000 || wh > 100000 ||
        min_width <= 0 || min_height <= 0) return false;
    const int aw = static_cast<int>(ww);
    const int ah = static_cast<int>(wh);
    v.width = std::clamp(v.width, (std::min)(aw, min_width), aw);
    v.height = std::clamp(v.height, (std::min)(ah, min_height), ah);
    v.x = std::clamp(v.x, work.left, work.right - v.width);
    v.y = std::clamp(v.y, work.top, work.bottom - v.height);
    return true;
}

} // namespace djmeta
