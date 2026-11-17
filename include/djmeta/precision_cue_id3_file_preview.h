#pragma once
// Read-only bounded ID3 header acquisition. Never reads MP3 audio into memory,
// never opens a write handle, and never returns physical data to logs.
#include "djmeta/precision_cue_id3_patch.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

namespace djmeta {

inline std::optional<std::string> read_precision_cue_id3_tag_for_preview(
    const std::filesystem::path& file) {
    std::ifstream input(file,std::ios::binary);
    if (!input) return std::nullopt;
    std::array<char,10> h{};
    if (!input.read(h.data(), static_cast<std::streamsize>(h.size())))
        return std::nullopt;
    const std::string_view header(h.data(),h.size());
    if (header.substr(0,3)!="ID3" ||
        (header[3]!=3 && header[3]!=4) ||
        header[4]!=0 || header[5]!=0) return std::nullopt;
    std::uint32_t size=0;
    if (!precision_cue_id3_patch_detail::synchsafe(header,6,size) ||
        size > 4u*1024u*1024u ||
        size > static_cast<unsigned>((std::numeric_limits<std::streamsize>::max)()-10))
        return std::nullopt;
    std::string data(h.data(),h.size());
    data.resize(h.size()+size,'\0');
    if (!input.read(data.data()+h.size(),static_cast<std::streamsize>(size)))
        return std::nullopt;
    return data;
}
} // namespace djmeta
