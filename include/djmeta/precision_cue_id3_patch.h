#pragma once
// Pure, bounded ID3v2 CUESHEET postimage planner. NO filesystem operations,
// no physical audio writes, no foobar virtual handle writes.
// Only an existing uniquely identified ID3 TXXX CUESHEET may be replaced.
// Untouched frame bytes and tag allocation stay bit-identical. A separate
// qualified atomic host writer, backup and end-device validation are REQUIRED.
#include "djmeta/precision_cue_embedded_crosscheck.h"
#include "djmeta/precision_cue_sha256.h"
#include "djmeta/external_cue.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace djmeta {

enum class PrecisionCueId3PatchState {
    Rejected, ReviewRequired, NoOp, PreviewReady
};
struct PrecisionCueId3Patch {
    PrecisionCueId3PatchState state = PrecisionCueId3PatchState::Rejected;
    std::string diagnostic = "ID3_CUE_PATCH_REJECTED";
    std::string postimage_tag;   // EXACT same number of bytes as original tag.
    std::array<std::uint8_t, 32> source_sha256{};
    std::array<std::uint8_t, 32> postimage_sha256{};
    unsigned untouched_frames = 0;
    std::size_t tag_bytes = 0;
};

namespace precision_cue_id3_patch_detail {
inline bool synchsafe(std::string_view bytes, std::size_t at, std::uint32_t& n) {
    n = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const auto c = static_cast<unsigned char>(bytes[at+i]);
        if (c & 0x80) return false;
        n = (n << 7) | c;
    }
    return true;
}
inline std::uint32_t be32(std::string_view s, std::size_t at) {
    std::uint32_t n = 0;
    for (unsigned i = 0; i < 4; ++i)
        n = (n << 8) | static_cast<unsigned char>(s[at+i]);
    return n;
}
inline void encode_size(std::string& header, std::uint32_t n, bool safe) {
    for (int i = 3; i >= 0; --i) {
        const unsigned shift = (safe ? 7u : 8u) * static_cast<unsigned>(i);
        header[7-i] = static_cast<char>((n >> shift) & (safe ? 127u : 255u));
    }
}
inline bool utf8_to_codepoints(std::string_view bytes, std::vector<std::uint32_t>& cps) {
    cps.clear();
    for (std::size_t i = 0; i < bytes.size();) {
        const auto a = static_cast<unsigned char>(bytes[i]);
        if (a == 0) return false;
        unsigned width = a < 0x80 ? 1 : (a >= 0xc2 && a <= 0xdf ? 2 :
                         a >= 0xe0 && a <= 0xef ? 3 :
                         a >= 0xf0 && a <= 0xf4 ? 4 : 0);
        if (width == 0 || width > bytes.size()-i) return false;
        std::uint32_t cp = (a & (width == 1 ? 0x7fu :
                           width == 2 ? 0x1fu : width == 3 ? 0x0fu : 0x07u));
        for (unsigned j = 1; j < width; ++j) {
            const auto b = static_cast<unsigned char>(bytes[i+j]);
            if ((b & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (b & 0x3fu);
        }
        if ((width == 2 && cp < 0x80) || (width == 3 && cp < 0x800) ||
            (width == 4 && cp < 0x10000) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff)) return false;
        cps.push_back(cp);
        i += width;
    }
    return true;
}
inline bool append_utf8(std::uint32_t cp, std::string& out) {
    if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 63)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
        out.push_back(static_cast<char>(0x80 | (cp & 63)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
        out.push_back(static_cast<char>(0x80 | (cp & 63)));
    }
    return true;
}
inline void append_unit(std::string& out, unsigned cp, bool little) {
    if (little) {
        out.push_back(static_cast<char>(cp & 255));
        out.push_back(static_cast<char>((cp >> 8) & 255));
    } else {
        out.push_back(static_cast<char>((cp >> 8) & 255));
        out.push_back(static_cast<char>(cp & 255));
    }
}
inline bool to_utf16(std::string_view utf8, bool little, std::string& out) {
    std::vector<std::uint32_t> cps;
    if (!utf8_to_codepoints(utf8, cps)) return false;
    out.clear();
    for (auto cp : cps) {
        if (cp < 0x10000) append_unit(out, cp, little);
        else {
            cp -= 0x10000;
            append_unit(out, 0xd800u + (cp >> 10), little);
            append_unit(out, 0xdc00u + (cp & 0x3ffu), little);
        }
    }
    return true;
}
inline bool from_utf16(std::string_view bytes, bool little, std::string& out) {
    if ((bytes.size() & 1u) != 0) return false;
    out.clear();
    for (std::size_t i = 0; i < bytes.size(); i += 2) {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = static_cast<unsigned char>(bytes[i+1]);
        std::uint32_t cp = little ? (a | (unsigned(b) << 8)) :
                                    ((unsigned(a) << 8) | b);
        if (cp >= 0xd800 && cp <= 0xdbff) {
            if (bytes.size()-i < 4) return false;
            const auto c = static_cast<unsigned char>(bytes[i+2]);
            const auto d = static_cast<unsigned char>(bytes[i+3]);
            const std::uint32_t low = little ? (c | (unsigned(d) << 8)) :
                                              ((unsigned(c) << 8) | d);
            if (low < 0xdc00 || low > 0xdfff) return false;
            cp = 0x10000 + ((cp-0xd800) << 10) + (low-0xdc00);
            i += 2;
        } else if (cp >= 0xdc00 && cp <= 0xdfff) return false;
        if (!append_utf8(cp, out)) return false;
    }
    return true;
}
struct Payload {
    bool valid = false;
    bool cue_name = false;
    unsigned encoding = 0;
    bool little = false;
    bool value_bom = false;
    std::string prefix;
    std::string cue_utf8;
};
inline Payload decode_txxx(std::string_view bytes) {
    Payload p;
    if (bytes.empty()) return p;
    p.encoding = static_cast<unsigned char>(bytes.front());
    std::size_t at = 1;
    if (p.encoding == 0 || p.encoding == 3) {
        const auto end = bytes.find('\0', 1);
        if (end == std::string_view::npos) return p;
        const auto name = bytes.substr(1, end-1);
        p.cue_name = precision_cue_embedded_detail::cue_field(name);
        p.prefix = std::string(bytes.substr(0, end+1));
        p.cue_utf8 = std::string(bytes.substr(end+1));
        if (p.encoding == 0) {
            for (const unsigned char c : p.cue_utf8)
                if (c >= 128) return p;
        }
        std::vector<std::uint32_t> codepoints;
        if (!utf8_to_codepoints(p.cue_utf8, codepoints)) return p;
        p.valid = true;
        return p;
    }
    if (p.encoding != 1 && p.encoding != 2) return p;
    if (p.encoding == 1) {
        if (bytes.size() < 3) return p;
        const auto b1 = static_cast<unsigned char>(bytes[1]);
        const auto b2 = static_cast<unsigned char>(bytes[2]);
        if (b1 == 0xff && b2 == 0xfe) p.little = true;
        else if (b1 == 0xfe && b2 == 0xff) p.little = false;
        else return p;
        at = 3;
    }
    std::string desc;
    bool separator = false;
    for (; at + 1 < bytes.size(); at += 2) {
        const auto a = static_cast<unsigned char>(bytes[at]);
        const auto b = static_cast<unsigned char>(bytes[at+1]);
        if (a == 0 && b == 0) { separator = true; at += 2; break; }
        desc.push_back(bytes[at]);
        desc.push_back(bytes[at+1]);
    }
    if (!separator) return p;
    std::string name;
    if (!from_utf16(desc,p.little,name)) return p;
    p.cue_name = precision_cue_embedded_detail::cue_field(name);
    p.prefix = std::string(bytes.substr(0,at));
    if (p.encoding == 1 && bytes.size()-at >= 2) {
        const auto a = static_cast<unsigned char>(bytes[at]);
        const auto b = static_cast<unsigned char>(bytes[at+1]);
        if ((p.little && a == 0xff && b == 0xfe) ||
            (!p.little && a == 0xfe && b == 0xff)) {
            p.value_bom = true;
            p.prefix.append(bytes.substr(at,2));
            at += 2;
        }
    }
    if (!from_utf16(bytes.substr(at),p.little,p.cue_utf8)) return p;
    p.valid = true;
    return p;
}
inline bool encode_payload(const Payload& old, std::string_view new_cue,
                           std::string& result) {
    result = old.prefix;
    if (old.encoding == 0) {
        for (const unsigned char c : new_cue)
            if (c == 0 || c >= 128) return false;
        result.append(new_cue);
        return true;
    }
    if (old.encoding == 3) {
        std::vector<std::uint32_t> cps;
        if (!utf8_to_codepoints(new_cue,cps)) return false;
        result.append(new_cue);
        return true;
    }
    std::string utf16;
    if (!to_utf16(new_cue,old.little,utf16)) return false;
    result += utf16;
    return true;
}
} // namespace precision_cue_id3_patch_detail

inline PrecisionCueId3Patch preview_precision_cue_id3_txxx_patch(
    std::string_view original_tag, std::string_view proposed_cue_utf8,
    PrecisionCueEmbeddedCrosscheckState physical_crosscheck) {
    using namespace precision_cue_id3_patch_detail;
    PrecisionCueId3Patch result;
    const auto reject = [&](PrecisionCueId3PatchState state, const char* why) {
        result.state = state;
        result.diagnostic = why;
        result.postimage_tag.clear();
        return result;
    };
    if (physical_crosscheck !=
            PrecisionCueEmbeddedCrosscheckState::TextualAgreementReadOnly)
        return reject(PrecisionCueId3PatchState::ReviewRequired,
                      "PHYSICAL_EMBEDDED_TAG_NOT_UNIQUELY_QUALIFIED");
    if (original_tag.size() < 20 || original_tag.size() > 4u*1024u*1024u+10u ||
        original_tag.substr(0,3) != "ID3" ||
        (original_tag[3] != 3 && original_tag[3] != 4) ||
        original_tag[4] != 0 || original_tag[5] != 0)
        return reject(PrecisionCueId3PatchState::Rejected, "UNSUPPORTED_ID3_HEADER");
    std::uint32_t size = 0;
    if (!synchsafe(original_tag,6,size) || size+10u != original_tag.size())
        return reject(PrecisionCueId3PatchState::Rejected, "ID3_TAG_SIZE_INVALID");
    if (proposed_cue_utf8.empty() || proposed_cue_utf8.size() > 1024*1024 ||
        inspect_external_cue(proposed_cue_utf8).status != CueSyntaxStatus::Parsed)
        return reject(PrecisionCueId3PatchState::Rejected, "PROPOSED_CUE_SYNTAX_INVALID");

    const unsigned version = static_cast<unsigned char>(original_tag[3]);
    const auto data = original_tag.substr(10);
    std::size_t at = 0, padding_at = data.size(), cue_at = 0, cue_length = 0;
    std::string cue_frame;
    std::string old_cue;
    unsigned cue_count = 0;
    while (at < data.size()) {
        if (data[at] == '\0') {
            for (std::size_t i = at; i < data.size(); ++i)
                if (data[i] != '\0')
                    return reject(PrecisionCueId3PatchState::Rejected,
                                  "ID3_NONZERO_PADDING");
            padding_at = at;
            break;
        }
        if (data.size()-at < 10)
            return reject(PrecisionCueId3PatchState::Rejected, "ID3_SHORT_FRAME");
        const auto frame = data.substr(at,10);
        for (unsigned j = 0; j < 4; ++j) {
            const unsigned char c = static_cast<unsigned char>(frame[j]);
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
                return reject(PrecisionCueId3PatchState::Rejected, "ID3_INVALID_FRAME_ID");
        }
        std::uint32_t frame_size = 0;
        if (version == 4) {
            if (!synchsafe(frame,4,frame_size))
                return reject(PrecisionCueId3PatchState::Rejected,
                              "ID3_INVALID_FRAME_SIZE");
        } else frame_size = be32(frame,4);
        if (frame_size == 0 || frame_size > data.size()-at-10)
            return reject(PrecisionCueId3PatchState::Rejected, "ID3_FRAME_OUT_OF_RANGE");
        if (frame.substr(0,4) == "TXXX") {
            if (frame[8] != '\0' || frame[9] != '\0')
                return reject(PrecisionCueId3PatchState::Rejected,
                              "ID3_TXXX_HAS_UNSUPPORTED_FRAME_FLAGS");
            const auto parsed = decode_txxx(data.substr(at+10,frame_size));
            if (!parsed.valid)
                return reject(PrecisionCueId3PatchState::ReviewRequired,
                              "ID3_TXXX_ENCODING_UNSUPPORTED");
            if (parsed.cue_name) {
                ++cue_count;
                if (cue_count > 1)
                    return reject(PrecisionCueId3PatchState::ReviewRequired,
                                  "ID3_DUPLICATE_CUESHEET_FRAMES");
                cue_at = at;
                cue_length = 10+frame_size;
                old_cue = parsed.cue_utf8;
                std::string payload;
                if (!encode_payload(parsed,proposed_cue_utf8,payload) ||
                    payload.size() > 0x0fffffffu)
                    return reject(PrecisionCueId3PatchState::ReviewRequired,
                                  "ID3_CUESHEET_ENCODING_NOT_REPRESENTABLE");
                cue_frame = std::string(frame);
                encode_size(cue_frame,static_cast<std::uint32_t>(payload.size()),
                            version == 4);
                cue_frame += payload;
            }
        } else {
            ++result.untouched_frames;
        }
        at += 10+frame_size;
    }
    if (cue_count != 1)
        return reject(PrecisionCueId3PatchState::ReviewRequired,
                      "ID3_CUESHEET_FRAME_NOT_UNIQUE");
    const auto old = inspect_external_cue(old_cue);
    const auto fresh = inspect_external_cue(proposed_cue_utf8);
    if (old.status != CueSyntaxStatus::Parsed ||
        old.encoding != CueTextEncoding::Utf8 ||
        fresh.encoding != CueTextEncoding::Utf8 ||
        old.references.size() != fresh.references.size() ||
        old.references.empty())
        return reject(PrecisionCueId3PatchState::ReviewRequired,
                      "OLD_OR_NEW_CUE_FILE_INVENTORY_UNQUALIFIED");
    for (std::size_t i = 0; i < old.references.size(); ++i) {
        if (old.references[i].filename != fresh.references[i].filename ||
            old.references[i].file_type != fresh.references[i].file_type)
            return reject(PrecisionCueId3PatchState::ReviewRequired,
                          "EMBEDDED_CUE_FILE_IDENTITY_CHANGED");
    }
    result.source_sha256 = precision_cue_sha256(original_tag);
    result.tag_bytes = original_tag.size();
    if (old_cue == proposed_cue_utf8) {
        result.state = PrecisionCueId3PatchState::NoOp;
        result.diagnostic = "ID3_CUE_EXACT_NOOP";
        result.postimage_sha256 = result.source_sha256;
        return result;
    }
    const std::size_t used = padding_at;
    const std::size_t without_cue = used-cue_length;
    if (cue_frame.size() > data.size()-without_cue)
        return reject(PrecisionCueId3PatchState::ReviewRequired,
                      "ID3_INSUFFICIENT_EXISTING_TAG_PADDING");
    result.postimage_tag = std::string(original_tag.substr(0,10));
    result.postimage_tag.append(data.substr(0,cue_at));
    result.postimage_tag += cue_frame;
    result.postimage_tag.append(
        data.substr(cue_at+cue_length,used-(cue_at+cue_length)));
    result.postimage_tag.resize(original_tag.size(),'\0');
    if (result.postimage_tag.size() != original_tag.size())
        return reject(PrecisionCueId3PatchState::Rejected,
                      "ID3_POSTIMAGE_ALLOCATION_INVALID");
    result.postimage_sha256 = precision_cue_sha256(result.postimage_tag);
    result.state = PrecisionCueId3PatchState::PreviewReady;
    result.diagnostic = "ID3_TXXX_CUE_POSTIMAGE_PREVIEW_ONLY";
    return result;
}

} // namespace djmeta
