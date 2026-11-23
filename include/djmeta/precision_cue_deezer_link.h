#pragma once
// Pure, write-free Deezer album-link identity extraction.
// A URL is not proof of provider authorization or an instruction to fetch.
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace djmeta {
inline std::string deezer_precision_cue_album_id(std::string_view url) {
    auto reject = []() -> void {
        throw std::invalid_argument("Expected a canonical HTTPS deezer.com album URL");
    };
    constexpr std::string_view prefix = "https://";
    if (url.empty() || url.size() > 256 || !url.starts_with(prefix)) reject();
    url.remove_prefix(prefix.size());
    const auto slash = url.find('/');
    if (slash == std::string_view::npos) reject();
    const auto host = url.substr(0, slash);
    if (host != "www.deezer.com" && host != "deezer.com") reject();
    url.remove_prefix(slash + 1);
    if (url.find_first_of("?#%@\\") != std::string_view::npos) reject();
    const auto first = url.find('/');
    if (first == std::string_view::npos) reject();
    const auto maybe_locale = url.substr(0, first);
    bool locale = maybe_locale.size() == 2;
    for (char ch : maybe_locale)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))) locale = false;
    if (locale) url.remove_prefix(first + 1);
    constexpr std::string_view album = "album/";
    if (!url.starts_with(album)) reject();
    url.remove_prefix(album.size());
    if (url.ends_with('/')) url.remove_suffix(1);
    if (url.empty() || url.size() > 19) reject();
    for (char ch : url) if (ch < '0' || ch > '9') reject();
    std::uint64_t value = 0;
    const auto [ptr, ec] = std::from_chars(url.data(), url.data() + url.size(), value);
    if (ec != std::errc{} || ptr != url.data() + url.size() ||
        value == 0 || value > INT64_MAX) reject();
    return std::to_string(value);
}
} // namespace djmeta
