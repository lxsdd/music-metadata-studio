#pragma once

// Pure/portable window size persistence schema. Physical Win32 pixels and
// monitor geometry remain a host-layer responsibility. No disk/network/audio.
#include <charconv>
#include <optional>
#include <string>
#include <string_view>

namespace djmeta {

struct PreviewWindowSize {
    int width_at_96_dpi = 0;
    int height_at_96_dpi = 0;
    bool operator==(const PreviewWindowSize&) const = default;
};

inline bool valid_preview_window_size(PreviewWindowSize size) {
    return size.width_at_96_dpi >= 320 && size.width_at_96_dpi <= 8192 &&
           size.height_at_96_dpi >= 240 && size.height_at_96_dpi <= 8192;
}

inline std::optional<PreviewWindowSize> parse_preview_window_size(std::string_view value) {
    if (!value.starts_with("v1|")) return std::nullopt;
    value.remove_prefix(3);
    const auto separator = value.find('|');
    if (separator == std::string_view::npos ||
        separator == 0 || separator == value.size() - 1 ||
        value.find('|', separator + 1) != std::string_view::npos)
        return std::nullopt;
    const auto parse_number = [](std::string_view digits, int& number) {
        const auto result = std::from_chars(digits.data(),
                                           digits.data() + digits.size(), number);
        return result.ec == std::errc{} &&
               result.ptr == digits.data() + digits.size();
    };
    PreviewWindowSize out;
    if (!parse_number(value.substr(0, separator), out.width_at_96_dpi) ||
        !parse_number(value.substr(separator + 1), out.height_at_96_dpi) ||
        !valid_preview_window_size(out))
        return std::nullopt;
    return out;
}

inline std::string serialize_preview_window_size(PreviewWindowSize size) {
    if (!valid_preview_window_size(size)) return {};
    return "v1|" + std::to_string(size.width_at_96_dpi) + "|" +
           std::to_string(size.height_at_96_dpi);
}



// Full normal-window placement, separate from the legacy size-only v1 data.
// Coordinates are monitor/desktop independent *logical* Win32 workspace units;
// negative x/y support monitors to the left or above the primary screen.
// Maximized windows remember their normal (restored) position and size.
struct PreviewWindowPlacement {
    int left_at_96_dpi = 0;
    int top_at_96_dpi = 0;
    int width_at_96_dpi = 0;
    int height_at_96_dpi = 0;
    bool maximized = false;
    bool operator==(const PreviewWindowPlacement&) const = default;
};
inline bool valid_preview_window_placement(PreviewWindowPlacement p) {
    return p.left_at_96_dpi >= -65536 && p.left_at_96_dpi <= 65536 &&
           p.top_at_96_dpi >= -65536 && p.top_at_96_dpi <= 65536 &&
           valid_preview_window_size({p.width_at_96_dpi,p.height_at_96_dpi});
}
inline std::string serialize_preview_window_placement(PreviewWindowPlacement p) {
    if (!valid_preview_window_placement(p)) return {};
    return "v1|" + std::to_string(p.left_at_96_dpi) + "|" +
           std::to_string(p.top_at_96_dpi) + "|" +
           std::to_string(p.width_at_96_dpi) + "|" +
           std::to_string(p.height_at_96_dpi) + "|" +
           (p.maximized ? "1" : "0");
}
inline std::optional<PreviewWindowPlacement> parse_preview_window_placement(
    std::string_view input) {
    if (!input.starts_with("v1|")) return std::nullopt;
    input.remove_prefix(3);
    int fields[4]{};
    for (int& field : fields) {
        const auto separator = input.find('|');
        if (separator == std::string_view::npos || separator == 0)
            return std::nullopt;
        const auto value=input.substr(0,separator);
        const auto parsed=std::from_chars(value.data(),value.data()+value.size(),field);
        if(parsed.ec!=std::errc{} ||
           parsed.ptr!=value.data()+value.size())
            return std::nullopt;
        input.remove_prefix(separator+1);
    }
    if (input!="0" && input!="1") return std::nullopt;
    PreviewWindowPlacement p{
        fields[0],fields[1],fields[2],fields[3],input=="1"};
    if (!valid_preview_window_placement(p)) return std::nullopt;
    return p;
}

} // namespace djmeta
