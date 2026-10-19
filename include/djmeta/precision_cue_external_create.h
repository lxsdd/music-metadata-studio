#pragma once
// Create-only external CUE backend. Not wired to any foobar write action.
// No overwrite API exists here. The caller must collect real source/target
// handle snapshots and explicit user approval; fabricated tokens are not proof.
#include "djmeta/precision_cue_write_preflight.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace djmeta {

enum class PrecisionCueExternalCreateStatus {
    Created,
    NoOp,
    Collision,
    ReviewRequired,
    Rejected,
    IoError
};

struct PrecisionCueExternalCreateResult {
    PrecisionCueExternalCreateStatus status = PrecisionCueExternalCreateStatus::Rejected;
    std::string diagnostic;
};

struct PrecisionCueExternalFreshSnapshot {
    PrecisionCueWritePreflight preflight;
    std::uint64_t source_snapshot_token = 0;
    std::uint64_t target_snapshot_token = 0;
};

struct PrecisionCueExternalCreateRequest {
    PrecisionCuePlan plan;
    std::string mp3_basename;
    // Caller has already qualified this directory against the actual local
    // physical MP3 using host file handles, not a playlist virtual subsong.
    std::filesystem::path qualified_sibling_directory;
    PrecisionCueWritePreflight reviewed_preflight;
    std::uint64_t reviewed_source_snapshot_token = 0;
    std::uint64_t reviewed_target_snapshot_token = 0;
    std::array<std::uint8_t, 32> reviewed_cue_sha256{};
    bool explicitly_approved_for_external_create = false;
};

namespace precision_cue_external_detail {
inline bool write_new_stage(const std::filesystem::path& path,
                            std::string_view bytes) noexcept {
#ifdef _WIN32
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL |
                                FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    bool okay = true;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto amount = static_cast<DWORD>(
            (std::min)(bytes.size() - offset, std::size_t{1 << 20}));
        DWORD written = 0;
        if (!WriteFile(handle, bytes.data() + offset, amount, &written, nullptr) ||
            written != amount) { okay = false; break; }
        offset += written;
    }
    if (okay && !FlushFileBuffers(handle)) okay = false;
    if (!CloseHandle(handle)) okay = false;
    return okay;
#else
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = ::open(path.c_str(), flags, 0600);
    if (fd < 0) return false;
    bool okay = true;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto n = ::write(fd, bytes.data() + offset, bytes.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { okay = false; break; }
        offset += static_cast<std::size_t>(n);
    }
    if (okay && ::fsync(fd) != 0) okay = false;
    if (::close(fd) != 0) okay = false;
    return okay;
#endif
}

inline bool publish_without_replace(const std::filesystem::path& stage,
                                    const std::filesystem::path& target) noexcept {
#ifdef _WIN32
    // Without MOVEFILE_REPLACE_EXISTING, an existing target MUST NOT be replaced.
    return MoveFileExW(stage.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
#else
    // link() atomically fails with EEXIST even if a concurrent actor creates
    // target between the final probe and publication. rename() is unsafe here.
    return ::link(stage.c_str(), target.c_str()) == 0;
#endif
}

inline std::string random_suffix() {
    std::random_device device;
    constexpr char digits[] = "0123456789abcdef";
    std::string suffix;
    suffix.reserve(32);
    for (int i = 0; i < 4; ++i) {
        const auto word = static_cast<std::uint32_t>(device());
        for (int shift = 28; shift >= 0; shift -= 4)
            suffix.push_back(digits[(word >> shift) & 0xf]);
    }
    return suffix;
}

inline bool exact_existing_bytes(const std::filesystem::path& target,
                                 std::string_view expected) {
    std::error_code ec;
    if (std::filesystem::is_symlink(target, ec) ||
        !std::filesystem::is_regular_file(target, ec) ||
        std::filesystem::file_size(target, ec) != expected.size() || ec)
        return false;
    std::ifstream source(target, std::ios::binary);
    if (!source) return false;
    std::string actual(expected.size(), '\0');
    if (!source.read(actual.data(), static_cast<std::streamsize>(actual.size())))
        return false;
    return actual == expected && source.peek() == std::char_traits<char>::eof();
}
} // namespace precision_cue_external_detail

// The observer must independently re-read both physical-source identity and
// target-existence/content snapshots from qualified OS handles. It is called
// before staging and AGAIN before atomic publication. Source audio/tags are
// never opened for writing; embedded CUE writes are not supported.
inline PrecisionCueExternalCreateResult create_new_precision_cue_external(
    const PrecisionCueExternalCreateRequest& request,
    const std::function<PrecisionCueExternalFreshSnapshot()>& observe) {
    using Status = PrecisionCueExternalCreateStatus;
    const auto export_plan =
        plan_precision_cue_external_export(request.plan, request.mp3_basename);
    if (!export_plan.eligible || !observe ||
        !request.explicitly_approved_for_external_create ||
        request.plan.external_cue_utf8.size() > 1024 * 1024)
        return {Status::Rejected, "CUE_CREATE_NOT_AUTHORIZED_OR_INVALID"};

    const auto content_hash = precision_cue_sha256(request.plan.external_cue_utf8);
    if (request.reviewed_cue_sha256 == std::array<std::uint8_t, 32>{} ||
        content_hash != request.reviewed_cue_sha256)
        return {Status::ReviewRequired, "CUE_CONTENT_CHANGED_SINCE_REVIEW"};

    // A basename is NEVER a destination. Refuse relative, noncanonical or
    // symlink-derived parent locations; the host must qualify the MP3 itself.
    std::error_code ec;
    const auto& parent = request.qualified_sibling_directory;
    if (!parent.is_absolute() || parent != parent.lexically_normal() ||
        !std::filesystem::is_directory(parent, ec) || ec ||
        std::filesystem::canonical(parent, ec) != parent || ec)
        return {Status::Rejected, "CUE_TARGET_DIRECTORY_UNQUALIFIED"};

    const auto target = parent / std::filesystem::u8path(
        export_plan.target_cue_basename);
    if (target.parent_path() != parent)
        return {Status::Rejected, "CUE_TARGET_PATH_UNQUALIFIED"};

    const auto inspect = [&]() {
        PrecisionCueWriteCommitEvidence e;
        e.preview = request.reviewed_preflight;
        e.source_snapshot_token_preview = request.reviewed_source_snapshot_token;
        e.target_snapshot_token_preview = request.reviewed_target_snapshot_token;
        e.reviewed_cue_sha256 = request.reviewed_cue_sha256;
        e.at_commit_cue_sha256 = precision_cue_sha256(request.plan.external_cue_utf8);
        const auto fresh = observe();
        e.at_commit = fresh.preflight;
        e.source_snapshot_token_commit = fresh.source_snapshot_token;
        e.target_snapshot_token_commit = fresh.target_snapshot_token;
        return e;
    };

    try {
        auto evidence = inspect();
        // No-op NEVER opens a write handle. Confirm the exact existing bytes,
        // not just a flag supplied by an upstream provider or foobar handle.
        if (preflight_precision_cue_external_write(evidence.preview) ==
                PrecisionCueWriteDecision::NoOp &&
            preflight_precision_cue_external_write(evidence.at_commit) ==
                PrecisionCueWriteDecision::NoOp &&
            evidence.source_snapshot_token_preview != 0 &&
            evidence.target_snapshot_token_preview != 0 &&
            evidence.source_snapshot_token_preview ==
                evidence.source_snapshot_token_commit &&
            evidence.target_snapshot_token_preview ==
                evidence.target_snapshot_token_commit &&
            precision_cue_external_detail::exact_existing_bytes(
                target, request.plan.external_cue_utf8))
            return {Status::NoOp, "CUE_IDENTICAL_TARGET_UNCHANGED"};

        if (evidence.preview.target_exists || evidence.at_commit.target_exists)
            return {Status::Collision, "CUE_TARGET_ALREADY_EXISTS"};
        if (revalidate_precision_cue_external_commit(evidence) !=
            PrecisionCueWriteDecision::ReadyForExplicitWrite)
            return {Status::ReviewRequired, "CUE_SOURCE_OR_TARGET_STALE"};

        const auto stage = parent / std::filesystem::u8path(
            "." + export_plan.target_cue_basename + ".precision-cue-" +
            precision_cue_external_detail::random_suffix() + ".tmp");
        // CREATE_NEW / O_EXCL: never truncate a preexisting stage.
        if (!precision_cue_external_detail::write_new_stage(
                stage, request.plan.external_cue_utf8))
            return {Status::IoError, "CUE_STAGING_CREATE_FAILED"};
        const auto cleanup = [&]() {
            std::error_code ignored;
            std::filesystem::remove(stage, ignored);
        };
        // Prevent publishing bytes whose reviewed source/target identity
        // changed while the temporary file was being written and flushed.
        evidence = inspect();
        if (revalidate_precision_cue_external_commit(evidence) !=
            PrecisionCueWriteDecision::ReadyForExplicitWrite) {
            cleanup();
            return {Status::ReviewRequired, "CUE_CHANGED_DURING_STAGING"};
        }
        const bool published =
            precision_cue_external_detail::publish_without_replace(stage, target);
        cleanup();
        if (!published) {
            if (std::filesystem::exists(target, ec))
                return {Status::Collision, "CUE_TARGET_APPEARED_DURING_PUBLISH"};
            return {Status::IoError, "CUE_ATOMIC_PUBLISH_FAILED"};
        }
        return {Status::Created, "CUE_CREATE_NEW_ONLY"};
    } catch (const std::exception&) {
        return {Status::IoError, "CUE_FILESYSTEM_OR_SNAPSHOT_ERROR"};
    }
}

} // namespace djmeta
