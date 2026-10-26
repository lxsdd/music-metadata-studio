#pragma once
// Bounded read-only inspection of physical embedded CUE carriers.
// MP3 ID3v2.3/2.4 TXXX, APEv2; FLAC Vorbis comments and native Type-5.
// Unsupported or malformed metadata fails closed. NEVER modifies audio/tags.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <istream>
#include <limits>
#include <string>
#include <string_view>

namespace djmeta {
enum class PrecisionCueEmbeddedCarrier { Mp3, Flac };
enum class PrecisionCueEmbeddedState {
    NoneDetected, TextualPresent, NativeBinaryPresent,
    MultipleOrConflicting, Unqualified
};
struct PrecisionCueEmbeddedProbe {
    PrecisionCueEmbeddedState state = PrecisionCueEmbeddedState::Unqualified;
    unsigned cue_locations = 0;
    std::string diagnostic = "EMBEDDED_METADATA_UNQUALIFIED";
};

namespace precision_cue_embedded_detail {
constexpr std::uint64_t max_tag_bytes = 4ULL * 1024 * 1024;
struct Inventory {
    unsigned textual = 0, binary = 0;
    bool bad = false;
};
inline std::uint32_t le32(std::string_view s, std::size_t i) {
    std::uint32_t n = 0;
    for (unsigned k = 0; k != 4; ++k)
        n |= std::uint32_t(static_cast<unsigned char>(s[i+k])) << (k*8);
    return n;
}
inline std::uint32_t be32(std::string_view s, std::size_t i) {
    std::uint32_t n = 0;
    for (unsigned k = 0; k != 4; ++k)
        n = (n << 8) | static_cast<unsigned char>(s[i+k]);
    return n;
}
inline bool synchsafe(std::string_view s, std::size_t i, std::uint32_t& n) {
    n = 0;
    for (unsigned k = 0; k != 4; ++k) {
        const auto byte = static_cast<unsigned char>(s[i+k]);
        if (byte & 0x80) return false;
        n = (n << 7) | byte;
    }
    return true;
}
inline bool ascii_equal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto upper = [](unsigned char c) {
            return c >= 'a' && c <= 'z' ? static_cast<unsigned char>(c-32) : c;
        };
        if (upper(static_cast<unsigned char>(a[i])) !=
            upper(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}
inline bool cue_field(std::string_view name) {
    return ascii_equal(name, "CUESHEET") || ascii_equal(name, "CUE_SHEET");
}
inline bool read_at(std::istream& f, std::uint64_t pos,
                    char* data, std::size_t size) {
    if (pos > static_cast<std::uint64_t>(
            (std::numeric_limits<std::streamoff>::max)()) ||
        size > static_cast<std::size_t>(
            (std::numeric_limits<std::streamsize>::max)())) return false;
    f.clear();
    f.seekg(static_cast<std::streamoff>(pos));
    if (!f) return false;
    f.read(data, static_cast<std::streamsize>(size));
    return static_cast<std::size_t>(f.gcount()) == size;
}
inline bool read_block(std::istream& f, std::uint64_t pos,
                       std::size_t size, std::string& data) {
    if (size > max_tag_bytes) return false;
    data.assign(size, '\0');
    return read_at(f, pos, data.data(), size);
}
inline bool txxx_cue(std::string_view payload, bool& is_cue) {
    is_cue = false;
    if (payload.empty()) return false;
    const auto enc = static_cast<unsigned char>(payload[0]);
    if (enc == 0 || enc == 3) {
        const auto end = payload.find('\0', 1);
        if (end == std::string_view::npos) return false;
        is_cue = cue_field(payload.substr(1, end-1));
        return true;
    }
    if (enc != 1 && enc != 2) return false;
    bool little = false;
    std::size_t i = 1;
    if (enc == 1) {
        if (payload.size() < 4) return false;
        const auto a = static_cast<unsigned char>(payload[1]);
        const auto b = static_cast<unsigned char>(payload[2]);
        if (a == 0xff && b == 0xfe) little = true;
        else if (a == 0xfe && b == 0xff) little = false;
        else return false;
        i = 3;
    }
    std::string label;
    for (; i+1 < payload.size(); i += 2) {
        const auto a = static_cast<unsigned char>(payload[i]);
        const auto b = static_cast<unsigned char>(payload[i+1]);
        const unsigned unit = little ? unsigned(a | (b << 8)) :
                                      unsigned((a << 8) | b);
        if (unit == 0) { is_cue = cue_field(label); return true; }
        if (label.size() >= 255) return false;
        label.push_back(unit < 128 ? static_cast<char>(unit) : '?');
    }
    return false;
}
inline void id3(std::istream& f, std::uint64_t length, Inventory& acc) {
    if (length < 10) return;
    char prefix[10]{};
    if (!read_at(f, 0, prefix, 10)) { acc.bad = true; return; }
    if (std::string_view(prefix, 3) != "ID3") return;
    const auto version = static_cast<unsigned char>(prefix[3]);
    if ((version != 3 && version != 4) ||
        prefix[4] != 0 || prefix[5] != 0) { acc.bad = true; return; }
    std::uint32_t size = 0;
    if (!synchsafe(std::string_view(prefix, 10), 6, size) ||
        size > max_tag_bytes || 10ULL+size > length) { acc.bad = true; return; }
    std::string tag;
    if (!read_block(f, 10, size, tag)) { acc.bad = true; return; }
    std::size_t i = 0;
    while (i < tag.size()) {
        if (tag[i] == '\0') {
            for (std::size_t p = i; p < tag.size(); ++p)
                if (tag[p] != '\0') { acc.bad = true; return; }
            return;
        }
        if (tag.size()-i < 10) { acc.bad = true; return; }
        const auto frame = std::string_view(tag).substr(i, 10);
        for (unsigned k = 0; k < 4; ++k) {
            const unsigned char c = static_cast<unsigned char>(frame[k]);
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
                acc.bad = true; return;
            }
        }
        std::uint32_t size_frame = 0;
        if (version == 4) {
            if (!synchsafe(frame, 4, size_frame)) { acc.bad = true; return; }
        } else size_frame = be32(frame, 4);
        if (size_frame == 0 || size_frame > tag.size()-i-10) {
            acc.bad = true; return;
        }
        if (frame.substr(0,4) == "TXXX") {
            if (frame[8] || frame[9]) { acc.bad = true; return; }
            bool cue = false;
            if (!txxx_cue(std::string_view(tag).substr(i+10, size_frame), cue)) {
                acc.bad = true; return;
            }
            if (cue) ++acc.textual;
        }
        i += 10+size_frame;
    }
}
inline void ape(std::istream& f, std::uint64_t length, Inventory& acc) {
    if (length < 32) return;
    std::uint64_t end = length;
    if (length >= 160) {
        char v1[3]{};
        if (!read_at(f, length-128, v1, 3)) { acc.bad = true; return; }
        if (std::string_view(v1,3) == "TAG") end -= 128;
    }
    char tail[32]{};
    if (!read_at(f, end-32, tail, 32)) { acc.bad = true; return; }
    const auto footer = std::string_view(tail, 32);
    if (footer.substr(0,8) != "APETAGEX") return;
    const auto version = le32(footer, 8);
    const auto size = le32(footer, 12);
    const auto count = le32(footer, 16);
    if (version != 2000 || size < 32 || size > max_tag_bytes ||
        count > 4096 || size > end) { acc.bad = true; return; }
    std::string body;
    if (!read_block(f, end-size, size-32, body)) { acc.bad = true; return; }
    std::size_t p = 0;
    for (std::uint32_t n = 0; n < count; ++n) {
        if (body.size()-p < 9) { acc.bad = true; return; }
        const auto value_size = le32(body, p);
        const auto flags = le32(body, p+4);
        p += 8;
        const auto name_end = body.find('\0', p);
        if (name_end == std::string::npos || name_end == p ||
            name_end-p > 255) { acc.bad = true; return; }
        if (cue_field(std::string_view(body).substr(p, name_end-p))) {
            if ((flags & 6u) != 0) acc.bad = true;
            else ++acc.textual;
        }
        p = name_end+1;
        if (value_size > body.size()-p) { acc.bad = true; return; }
        p += value_size;
    }
    if (p != body.size()) acc.bad = true;
}
inline void vorbis(std::string_view tag, Inventory& acc) {
    if (tag.size() < 8) { acc.bad = true; return; }
    std::size_t p = 0;
    const auto vendor_size = le32(tag,p);
    p += 4;
    if (vendor_size > tag.size()-p ||
        tag.size()-p-vendor_size < 4) { acc.bad = true; return; }
    p += vendor_size;
    const auto count = le32(tag,p);
    p += 4;
    if (count > 65536) { acc.bad = true; return; }
    for (std::uint32_t i = 0; i < count; ++i) {
        if (tag.size()-p < 4) { acc.bad = true; return; }
        const auto len = le32(tag,p);
        p += 4;
        if (len > tag.size()-p) { acc.bad = true; return; }
        const auto value = tag.substr(p,len);
        const auto sep = value.find('=');
        if (sep != std::string_view::npos &&
            cue_field(value.substr(0,sep))) ++acc.textual;
        p += len;
    }
    if (p != tag.size()) acc.bad = true;
}
inline void flac(std::istream& f, std::uint64_t length, Inventory& acc) {
    char head[4]{};
    if (length < 8 || !read_at(f,0,head,4) ||
        std::string_view(head,4) != "fLaC") { acc.bad = true; return; }
    std::uint64_t p = 4;
    for (unsigned i = 0; i < 128; ++i) {
        if (length-p < 4 || !read_at(f,p,head,4)) { acc.bad = true; return; }
        const auto flag = static_cast<unsigned char>(head[0]);
        const auto type = flag & 127u;
        const bool last = (flag & 128u) != 0;
        const unsigned sz =
            (unsigned(static_cast<unsigned char>(head[1])) << 16) |
            (unsigned(static_cast<unsigned char>(head[2])) << 8) |
             unsigned(static_cast<unsigned char>(head[3]));
        p += 4;
        if (type == 127 || sz > max_tag_bytes || sz > length-p) {
            acc.bad = true; return;
        }
        if (type == 5) ++acc.binary;
        if (type == 4) {
            std::string tag;
            if (!read_block(f,p,sz,tag)) { acc.bad = true; return; }
            vorbis(tag,acc);
            if (acc.bad) return;
        }
        p += sz;
        if (last) return;
    }
    acc.bad = true;
}
} // namespace precision_cue_embedded_detail

inline PrecisionCueEmbeddedProbe inspect_precision_cue_embedded(
    std::istream& f, PrecisionCueEmbeddedCarrier carrier) {
    using namespace precision_cue_embedded_detail;
    f.clear();
    f.seekg(0,std::ios::end);
    const auto end = f.tellg();
    if (!f || end <= 0) return {};
    Inventory found;
    if (carrier == PrecisionCueEmbeddedCarrier::Mp3) {
        id3(f,static_cast<std::uint64_t>(end),found);
        if (!found.bad) ape(f,static_cast<std::uint64_t>(end),found);
    } else flac(f,static_cast<std::uint64_t>(end),found);
    const auto count = found.textual + found.binary;
    if (found.bad) return {PrecisionCueEmbeddedState::Unqualified,
                            count,"EMBEDDED_TAG_MALFORMED_OR_UNSUPPORTED"};
    if (count > 1) return {PrecisionCueEmbeddedState::MultipleOrConflicting,
                            count,"MULTIPLE_EMBEDDED_CUE_SOURCES"};
    if (found.binary) return {PrecisionCueEmbeddedState::NativeBinaryPresent,
                               count,"FLAC_NATIVE_TYPE5_PRESERVE_ONLY"};
    if (found.textual) return {PrecisionCueEmbeddedState::TextualPresent,
                                count,"EMBEDDED_TEXTUAL_CUE_EXISTS"};
    return {PrecisionCueEmbeddedState::NoneDetected,0,
            "NO_SUPPORTED_EMBEDDED_CUE_DETECTED"};
}
inline PrecisionCueEmbeddedProbe inspect_precision_cue_embedded_file(
    const std::filesystem::path& path, PrecisionCueEmbeddedCarrier carrier) {
    std::ifstream input(path,std::ios::binary);
    if (!input) return {};
    return inspect_precision_cue_embedded(input,carrier);
}
} // namespace djmeta
