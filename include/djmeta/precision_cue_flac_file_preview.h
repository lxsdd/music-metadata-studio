#pragma once
// Bounded read-only FLAC metadata-prefix acquisition. Does not read audio or
// write any filesystem data. Unsupported metadata layouts return nullopt.
#include "djmeta/precision_cue_flac_patch.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace djmeta {
inline std::optional<std::string> read_precision_cue_flac_metadata_prefix(
    const std::filesystem::path& physical_flac) {
    std::ifstream file(physical_flac,std::ios::binary);
    if (!file) return std::nullopt;
    std::array<char,4> head{};
    if (!file.read(head.data(),4) ||
        std::string_view(head.data(),4)!="fLaC") return std::nullopt;
    std::string data="fLaC";
    for (unsigned i=0; i<128; ++i) {
        if (!file.read(head.data(),4)) return std::nullopt;
        const unsigned type=static_cast<unsigned char>(head[0]) & 127u;
        const unsigned size=
            (unsigned(static_cast<unsigned char>(head[1]))<<16) |
            (unsigned(static_cast<unsigned char>(head[2]))<<8) |
             unsigned(static_cast<unsigned char>(head[3]));
        constexpr std::size_t limit=4u*1024u*1024u;
        if (type==127 || data.size()>limit-4 ||
            size>limit-data.size()-4) return std::nullopt;
        data.append(head.data(),4);
        const auto before=data.size();
        data.resize(before+size,'\0');
        if (!file.read(data.data()+before,static_cast<std::streamsize>(size)))
            return std::nullopt;
        if ((static_cast<unsigned char>(head[0]) & 128u)!=0) return data;
    }
    return std::nullopt;
}
} // namespace djmeta
