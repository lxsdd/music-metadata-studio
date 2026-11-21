#pragma once
// Read-only sibling external CUE target classification for foobar previews.
// The result is NOT commit authorization; a writer must revalidate everything.
#include "djmeta/precision_cue_external_create.h"

#include <filesystem>
#include <array>
#include <cstdint>
#include <cerrno>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace djmeta {

enum class PrecisionCueExistingTargetState {
    Absent,
    Identical,
    Different,
    Unqualified
};
struct PrecisionCueExistingTargetInspection {
    PrecisionCueExistingTargetState state = PrecisionCueExistingTargetState::Unqualified;
    std::string diagnostic;
    // Optional bounded bytes of a different existing reference, for a
    // read-only timing diff only. Not a stable snapshot or write approval.
    std::optional<std::string> observed_different_cue;

    PrecisionCueExistingTargetInspection() = default;
    // Avoid -Wmissing-field-initializers in strict C++20 Linux CI while
    // keeping a missing advisory text snapshot explicit and non-authoritative.
    PrecisionCueExistingTargetInspection(
        PrecisionCueExistingTargetState observed_state,
        std::string observed_diagnostic,
        std::optional<std::string> observed_bytes = std::nullopt)
        : state(observed_state),
          diagnostic(std::move(observed_diagnostic)),
          observed_different_cue(std::move(observed_bytes)) {}
};

// Read-only bounded target snapshot through a no-follow OS handle.  The
// preview is advisory, never a write authorization.  Opening after a path
// symlink_status check must not follow a concurrently substituted link.
namespace precision_cue_inspect_detail {
inline std::optional<std::string> read_regular_target(
    const std::filesystem::path& target) {
    constexpr std::size_t limit = 1024 * 1024;
#ifdef _WIN32
    const auto open = [&] {
        return CreateFileW(target.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    };
    const auto same = [](const BY_HANDLE_FILE_INFORMATION& a,
                         const BY_HANDLE_FILE_INFORMATION& b) {
        return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
               a.nFileIndexHigh == b.nFileIndexHigh &&
               a.nFileIndexLow == b.nFileIndexLow &&
               a.nFileSizeHigh == b.nFileSizeHigh &&
               a.nFileSizeLow == b.nFileSizeLow &&
               CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) == 0;
    };
    const HANDLE handle = open();
    if (handle == INVALID_HANDLE_VALUE) return std::nullopt;
    BY_HANDLE_FILE_INFORMATION before{}, after{};
    bool okay = GetFileInformationByHandle(handle, &before) != 0;
    const auto size = (static_cast<std::uint64_t>(before.nFileSizeHigh) << 32) |
                       static_cast<std::uint64_t>(before.nFileSizeLow);
    okay = okay && (before.dwFileAttributes &
        (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0 &&
        size <= limit;
    std::string bytes;
    if (okay) bytes.resize(static_cast<std::size_t>(size));
    std::size_t offset = 0;
    while (okay && offset < bytes.size()) {
        const auto amount = static_cast<DWORD>(
            (std::min)(bytes.size() - offset, std::size_t{64 * 1024}));
        DWORD count = 0;
        if (!ReadFile(handle, bytes.data() + offset, amount, &count, nullptr) ||
            count != amount) { okay = false; break; }
        offset += count;
    }
    if (okay) {
        char extra = 0;
        DWORD count = 0;
        okay = ReadFile(handle, &extra, 1, &count, nullptr) != 0 &&
               count == 0 && GetFileInformationByHandle(handle, &after) != 0 &&
               same(before, after);
    }
    if (!CloseHandle(handle) || !okay) return std::nullopt;
    // A second no-follow open catches replacement of the directory entry
    // while the first handle was read (including same-size/time replacements).
    const HANDLE current = open();
    if (current == INVALID_HANDLE_VALUE) return std::nullopt;
    BY_HANDLE_FILE_INFORMATION now{};
    const bool current_ok = GetFileInformationByHandle(current, &now) != 0 &&
        (now.dwFileAttributes &
         (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0 &&
        same(before, now);
    const bool closed = CloseHandle(current) != 0;
    return current_ok && closed ? std::optional<std::string>(std::move(bytes))
                                : std::nullopt;
#else
#ifndef O_NOFOLLOW
    return std::nullopt;
#else
    int flags = O_RDONLY | O_NOFOLLOW | O_NONBLOCK;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    const auto open = [&] { return ::open(target.c_str(), flags); };
    const auto same = [](const struct stat& a, const struct stat& b) {
        bool equal = a.st_dev == b.st_dev && a.st_ino == b.st_ino &&
                     a.st_size == b.st_size && a.st_mtime == b.st_mtime &&
                     a.st_ctime == b.st_ctime;
#ifdef __linux__
        equal = equal && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
                a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
#endif
        return equal;
    };
    const int fd = open();
    if (fd < 0) return std::nullopt;
    struct stat before{}, after{};
    bool okay = ::fstat(fd, &before) == 0 && S_ISREG(before.st_mode) &&
                before.st_size >= 0 &&
                static_cast<std::uintmax_t>(before.st_size) <= limit;
    std::string bytes;
    if (okay) bytes.resize(static_cast<std::size_t>(before.st_size));
    std::size_t offset = 0;
    while (okay && offset < bytes.size()) {
        const auto count = ::read(fd, bytes.data() + offset,
                                  bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { okay = false; break; }
        offset += static_cast<std::size_t>(count);
    }
    if (okay) {
        char extra = 0;
        auto count = ::read(fd, &extra, 1);
        while (count < 0 && errno == EINTR)
            count = ::read(fd, &extra, 1);
        okay = count == 0 && ::fstat(fd, &after) == 0 && same(before, after);
    }
    if (::close(fd) != 0 || !okay) return std::nullopt;
    const int current = open();
    if (current < 0) return std::nullopt;
    struct stat now{};
    const bool current_ok = ::fstat(current, &now) == 0 &&
        S_ISREG(now.st_mode) && same(before, now);
    const bool closed = ::close(current) == 0;
    return current_ok && closed ? std::optional<std::string>(std::move(bytes))
                                : std::nullopt;
#endif
#endif
}
} // namespace precision_cue_inspect_detail

inline PrecisionCueExistingTargetInspection inspect_precision_cue_existing_target(
    const PrecisionCuePlan& plan, const std::filesystem::path& selected_physical_mp3,
    std::string_view mp3_basename) {
    using State = PrecisionCueExistingTargetState;
    namespace fs = std::filesystem;
    try {
        const auto proposed = plan_precision_cue_external_export(plan, mp3_basename);
        if (!proposed.eligible)
            return {State::Unqualified, "CUE_EXPORT_PLAN_INVALID"};
        const auto parent = selected_physical_mp3.parent_path();
        std::error_code ec;
        if (!parent.is_absolute() || parent != parent.lexically_normal() ||
            fs::canonical(parent, ec) != parent || ec ||
            !precision_cue_external_detail::physical_source_snapshot(
                selected_physical_mp3, parent, mp3_basename))
            return {State::Unqualified, "CUE_SOURCE_NOT_QUALIFIED"};
        const auto target = parent /
            precision_cue_external_detail::utf8_path(proposed.target_cue_basename);
        if (target.parent_path() != parent)
            return {State::Unqualified, "CUE_TARGET_PATH_INVALID"};
        const auto status = fs::symlink_status(target, ec);
        if (ec == std::errc::no_such_file_or_directory ||
            (!ec && status.type() == fs::file_type::not_found))
            return {State::Absent, "CUE_TARGET_ABSENT_AT_PREVIEW"};
        if (ec || status.type() != fs::file_type::regular)
            return {State::Unqualified, "CUE_TARGET_SYMLINK_OR_SPECIAL"};
        const auto existing =
            precision_cue_inspect_detail::read_regular_target(target);
        if (!existing)
            return {State::Unqualified, "CUE_EXISTING_CONTENT_UNQUALIFIED"};
        if (*existing == plan.external_cue_utf8)
            return {State::Identical, "CUE_IDENTICAL_NOOP_ONLY"};
        return {State::Different, "CUE_CONFLICT_NO_OVERWRITE",
                std::move(*existing)};
    } catch (const std::exception&) {
        return {State::Unqualified, "CUE_PREVIEW_TARGET_INSPECTION_FAILED"};
    }
}

} // namespace djmeta
