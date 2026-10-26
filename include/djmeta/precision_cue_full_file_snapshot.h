#pragma once
// Full physical source SHA256 snapshot, read-only and bounded in RAM.
// An integrity guard for a future embedded transaction, NEVER permission
// to write, replace, bypass foobar host locks, or touch physical tags.
#include "djmeta/precision_cue_sha256_stream.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace djmeta {
struct PrecisionCuePhysicalFileIdentity {
    std::uintmax_t bytes = 0;
    std::filesystem::file_time_type last_write{};
    std::uint64_t volume_id = 0;
    std::uint64_t file_id = 0;
};
struct PrecisionCuePhysicalFullSnapshot {
    std::filesystem::path canonical_path;
    PrecisionCuePhysicalFileIdentity physical;
    std::array<std::uint8_t,32> full_file_sha256{};
    std::array<std::uint8_t,32> reviewed_prefix_sha256{};
    std::uint64_t reviewed_prefix_size = 0;
};

namespace precision_cue_full_snapshot_detail {
inline std::optional<PrecisionCuePhysicalFileIdentity> identity(
    const std::filesystem::path& path) {
    namespace fs=std::filesystem;
    std::error_code ec;
    if (!path.is_absolute() || path!=path.lexically_normal() ||
        fs::is_symlink(path,ec) || ec ||
        !fs::is_regular_file(path,ec) || ec ||
        fs::canonical(path,ec)!=path || ec)
        return std::nullopt;
    PrecisionCuePhysicalFileIdentity result;
#ifdef _WIN32
    const HANDLE h=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
        OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if (h==INVALID_HANDLE_VALUE) return std::nullopt;
    BY_HANDLE_FILE_INFORMATION info{};
    const bool ok=GetFileInformationByHandle(h,&info)!=0 &&
        (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))==0;
    const bool closed=CloseHandle(h)!=0;
    if (!ok || !closed) return std::nullopt;
    result.bytes=(static_cast<std::uint64_t>(info.nFileSizeHigh)<<32)|
                  static_cast<std::uint64_t>(info.nFileSizeLow);
    result.volume_id=info.dwVolumeSerialNumber;
    result.file_id=(static_cast<std::uint64_t>(info.nFileIndexHigh)<<32)|
                    static_cast<std::uint64_t>(info.nFileIndexLow);
#else
    struct stat info{};
    if (::lstat(path.c_str(),&info)!=0 || !S_ISREG(info.st_mode) ||
        info.st_size<=0) return std::nullopt;
    result.bytes=static_cast<std::uintmax_t>(info.st_size);
    result.volume_id=static_cast<std::uint64_t>(info.st_dev);
    result.file_id=static_cast<std::uint64_t>(info.st_ino);
#endif
    // Conservative ceiling: 4 TiB, explicit source-file length required.
    constexpr std::uintmax_t max_file=std::uintmax_t{1}<<42;
    if (result.bytes==0 || result.bytes>max_file ||
        fs::file_size(path,ec)!=result.bytes || ec) return std::nullopt;
    result.last_write=fs::last_write_time(path,ec);
    if (ec) return std::nullopt;
    return result;
}
inline bool same(const PrecisionCuePhysicalFileIdentity& a,
                 const PrecisionCuePhysicalFileIdentity& b) noexcept {
    return a.bytes==b.bytes && a.last_write==b.last_write &&
        a.volume_id==b.volume_id && a.file_id==b.file_id;
}
} // namespace precision_cue_full_snapshot_detail

inline std::optional<PrecisionCuePhysicalFullSnapshot>
capture_precision_cue_physical_full_snapshot(
    const std::filesystem::path& canonical_source,
    std::string_view reviewed_existing_metadata_prefix) {
    using namespace precision_cue_full_snapshot_detail;
    constexpr std::size_t max_prefix=4u*1024u*1024u+10u;
    if (reviewed_existing_metadata_prefix.empty() ||
        reviewed_existing_metadata_prefix.size()>max_prefix)
        return std::nullopt;
    try {
        const auto first=identity(canonical_source);
        if (!first || reviewed_existing_metadata_prefix.size()>first->bytes)
            return std::nullopt;
        std::ifstream reader(canonical_source,std::ios::binary);
        if (!reader) return std::nullopt;
        PrecisionCueSha256Stream sha;
        std::array<char,64*1024> buffer{};
        std::uintmax_t offset=0;
        while (offset<first->bytes) {
            const auto count=static_cast<std::size_t>(
                (std::min)(std::uintmax_t(buffer.size()),first->bytes-offset));
            reader.read(buffer.data(),static_cast<std::streamsize>(count));
            if (static_cast<std::size_t>(reader.gcount())!=count)
                return std::nullopt;
            if (offset<reviewed_existing_metadata_prefix.size()) {
                const auto prefix_bytes=(std::min)(
                    count,reviewed_existing_metadata_prefix.size()-
                          static_cast<std::size_t>(offset));
                if (std::string_view(buffer.data(),prefix_bytes)!=
                    reviewed_existing_metadata_prefix.substr(
                        static_cast<std::size_t>(offset),prefix_bytes))
                    return std::nullopt;
            }
            sha.update(std::string_view(buffer.data(),count));
            offset+=count;
        }
        char extra{};
        if (reader.get(extra))
            return std::nullopt; // Source grew during hashing.
        reader.close();
        const auto second=identity(canonical_source);
        if (!second || !same(*first,*second))
            return std::nullopt;
        PrecisionCuePhysicalFullSnapshot snapshot;
        snapshot.canonical_path=canonical_source;
        snapshot.physical=*first;
        snapshot.full_file_sha256=sha.finalize();
        snapshot.reviewed_prefix_sha256=
            precision_cue_sha256(reviewed_existing_metadata_prefix);
        snapshot.reviewed_prefix_size=reviewed_existing_metadata_prefix.size();
        return snapshot;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}
inline bool revalidate_precision_cue_physical_full_snapshot(
    const PrecisionCuePhysicalFullSnapshot& reviewed,
    std::string_view originally_reviewed_prefix) {
    if (reviewed.canonical_path.empty() ||
        reviewed.reviewed_prefix_size!=originally_reviewed_prefix.size() ||
        reviewed.reviewed_prefix_sha256!=
            precision_cue_sha256(originally_reviewed_prefix))
        return false;
    const auto fresh=capture_precision_cue_physical_full_snapshot(
        reviewed.canonical_path,originally_reviewed_prefix);
    return fresh &&
        precision_cue_full_snapshot_detail::same(
            reviewed.physical,fresh->physical) &&
        reviewed.full_file_sha256==fresh->full_file_sha256 &&
        reviewed.reviewed_prefix_sha256==fresh->reviewed_prefix_sha256;
}
} // namespace djmeta
