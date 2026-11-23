#pragma once

// Parse an Apple Music share URL without requesting network resources.
// Only the validated, decimal collection ID may later be submitted to
// the official iTunes API. No filesystem, tagging or foobar calls occur.

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace djmeta {

struct ApplePrecisionCueAlbumLink {
    std::string collection_id;
    std::string storefront; // ISO alpha-2 uppercase or empty
};

inline ApplePrecisionCueAlbumLink parse_apple_precision_cue_album_link(
    std::string_view url) {
    const auto reject = []() -> void {
        throw std::invalid_argument("Invalid Apple Music album URL");
    };
    constexpr std::string_view prefix = "https://music.apple.com/";
    if (!url.starts_with(prefix) || url.size() <= prefix.size() ||
        url.size() > 512) reject();
    url.remove_prefix(prefix.size());
    if (url.find_first_of("#@\\:") != std::string_view::npos) reject();

    const auto query_at = url.find('?');
    if (query_at != std::string_view::npos) {
        const auto query = url.substr(query_at + 1);
        if (!query.starts_with("i=")) reject();
        const auto deep_link_id = query.substr(2);
        if (deep_link_id.empty() || deep_link_id.size() > 19) reject();
        for (const auto c : deep_link_id)
            if (c < '0' || c > '9') reject();
        std::int64_t track_id = 0;
        const auto [end, ec] = std::from_chars(
            deep_link_id.data(), deep_link_id.data() + deep_link_id.size(),
            track_id);
        if (ec != std::errc{} ||
            end != deep_link_id.data() + deep_link_id.size() ||
            track_id <= 0) reject();
        url = url.substr(0, query_at);
    }

    ApplePrecisionCueAlbumLink result;
    const auto first_slash = url.find('/');
    if (first_slash == 2) {
        const auto cc = url.substr(0, 2);
        for (const auto c : cc) {
            if (c >= 'a' && c <= 'z')
                result.storefront.push_back(static_cast<char>(c - 'a' + 'A'));
            else if (c >= 'A' && c <= 'Z')
                result.storefront.push_back(c);
            else reject();
        }
        url.remove_prefix(3);
    }

    constexpr std::string_view segment = "album/";
    if (!url.starts_with(segment)) reject();
    url.remove_prefix(segment.size());
    if (url.empty() || url.ends_with('/')) reject();

    std::string_view id = url;
    const auto split = url.find('/');
    if (split != std::string_view::npos) {
        const auto slug = url.substr(0, split);
        if (slug.empty() || slug.size() > 200 ||
            slug == "." || slug == "..") reject();
        for (std::size_t i = 0; i < slug.size(); ++i) {
            const auto c = static_cast<unsigned char>(slug[i]);
            if (c == '%' && i + 2 < slug.size()) {
                const auto hex = [](unsigned char v) -> int {
                    if (v >= '0' && v <= '9') return v - '0';
                    if (v >= 'A' && v <= 'F') return v - 'A' + 10;
                    if (v >= 'a' && v <= 'f') return v - 'a' + 10;
                    return -1;
                };
                const int a = hex(static_cast<unsigned char>(slug[i + 1]));
                const int b = hex(static_cast<unsigned char>(slug[i + 2]));
                if (a < 0 || b < 0) reject();
                const unsigned decoded = static_cast<unsigned>(a * 16 + b);
                if (decoded < 0x20 || decoded == 0x7f ||
                    decoded == '/' || decoded == '\\' ||
                    decoded == '?' || decoded == '#') reject();
                i += 2;
            } else if (c == '%' || c <= 0x20 || c == 0x7f ||
                       c == '&' || c == '=' || c == '?' || c == '/') reject();
        }
        id = url.substr(split + 1);
        if (id.find('/') != std::string_view::npos) reject();
    }
    if (id.empty() || id.size() > 19) reject();
    for (const auto c : id) if (c < '0' || c > '9') reject();
    std::int64_t id_number = 0;
    const auto [end, ec] = std::from_chars(
        id.data(), id.data() + id.size(), id_number);
    if (ec != std::errc{} || end != id.data() + id.size() ||
        id_number <= 0) reject();
    result.collection_id = std::to_string(id_number);
    return result;
}

} // namespace djmeta
