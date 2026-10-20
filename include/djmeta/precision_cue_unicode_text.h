#pragma once
// Generated CUE metadata and provider identities must not carry invisible
// Unicode controls or bidi overrides that misrepresent a reviewed edition.
// Preserve valid multilingual text, combining marks, emoji and ZWJ.
#include <cstdint>
#include <string_view>

namespace djmeta {

inline bool precision_cue_safe_unicode_text(std::string_view text) noexcept {
    for (std::size_t pos = 0; pos < text.size();) {
        const auto first = static_cast<unsigned char>(text[pos]);
        std::uint32_t cp = 0;
        unsigned width = 0;
        std::uint32_t minimum = 0;
        if (first < 0x80) { cp = first; width = 1; }
        else if ((first & 0xe0u) == 0xc0u) {
            cp = first & 0x1fu; width = 2; minimum = 0x80u;
        } else if ((first & 0xf0u) == 0xe0u) {
            cp = first & 0x0fu; width = 3; minimum = 0x800u;
        } else if ((first & 0xf8u) == 0xf0u) {
            cp = first & 0x07u; width = 4; minimum = 0x10000u;
        } else return false;
        if (width > text.size() - pos) return false;
        for (unsigned index = 1; index < width; ++index) {
            const auto byte = static_cast<unsigned char>(text[pos + index]);
            if ((byte & 0xc0u) != 0x80u) return false;
            cp = (cp << 6u) | (byte & 0x3fu);
        }
        if (cp < minimum || cp > 0x10ffffu ||
            (cp >= 0xd800u && cp <= 0xdfffu) ||
            cp < 0x20u || (cp >= 0x7fu && cp <= 0x9fu) ||
            cp == 0x061cu || cp == 0x200eu || cp == 0x200fu ||
            (cp >= 0x2028u && cp <= 0x202eu) ||
            (cp >= 0x2066u && cp <= 0x2069u) ||
            cp == 0xfeffu ||
            (cp >= 0xfdd0u && cp <= 0xfdefu) ||
            (cp & 0xfffeu) == 0xfffeu)
            return false;
        pos += width;
    }
    return true;
}
} // namespace djmeta
