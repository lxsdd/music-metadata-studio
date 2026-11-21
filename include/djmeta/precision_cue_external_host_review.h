#pragma once
// Physical foobar MP3 -> reviewed external CUE create-only handoff.
// Captures actual OS file IDs/snapshots and target non-existence at preview.
// It cannot write; writes require an independent user action and confirmation.
#include "djmeta/precision_cue_external_create.h"
#include "djmeta/precision_cue_sha256_stream.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>

namespace djmeta {

struct PrecisionCueReviewedExternalCreate {
    PrecisionCueExternalCreateRequest request;
    precision_cue_external_detail::PhysicalSourceFingerprint reviewed_source;
    std::array<std::uint8_t, 32> reviewed_source_sha256{};
};

// Streaming SHA guard: same-inode, same-size, restored-mtime edits must
// invalidate a reviewed physical mix. Read-only; bounded 64 KiB RAM.
// Hash the selected physical mix only through a no-follow read handle.
// A path-level preflight alone cannot protect the subsequent stream open from
// a swapped symlink, directory, FIFO or another physical file. No audio writes.
inline std::optional<std::array<std::uint8_t, 32>>
precision_cue_review_source_sha256(
    const std::filesystem::path& physical, const std::filesystem::path& parent,
    std::string_view basename) {
    const auto before = precision_cue_external_detail::physical_source_snapshot(
        physical, parent, basename);
    if (!before || before->length > (std::uintmax_t{1} << 42))
        return std::nullopt;
    PrecisionCueSha256Stream digest;
    std::array<char, 64 * 1024> block{};
    std::uintmax_t remaining = before->length;
#ifdef _WIN32
    const HANDLE handle = CreateFileW(physical.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return std::nullopt;
    BY_HANDLE_FILE_INFORMATION opened{}, finished{};
    bool okay = GetFileInformationByHandle(handle, &opened) != 0;
    const auto length = (static_cast<std::uint64_t>(opened.nFileSizeHigh) << 32) |
                         static_cast<std::uint64_t>(opened.nFileSizeLow);
    const auto identity = (static_cast<std::uint64_t>(opened.nFileIndexHigh) << 32) |
                           static_cast<std::uint64_t>(opened.nFileIndexLow);
    okay = okay && (opened.dwFileAttributes &
        (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0 &&
        opened.dwVolumeSerialNumber == before->volume_id &&
        identity == before->file_id && length == before->length;
    while (okay && remaining != 0) {
        const DWORD amount = static_cast<DWORD>((std::min)(
            remaining, std::uintmax_t(block.size())));
        DWORD count = 0;
        if (!ReadFile(handle, block.data(), amount, &count, nullptr) ||
            count != amount) { okay = false; break; }
        digest.update(std::string_view(block.data(), count));
        remaining -= count;
    }
    if (okay) {
        char extra = 0;
        DWORD count = 0;
        okay = ReadFile(handle, &extra, 1, &count, nullptr) != 0 &&
               count == 0 &&
               GetFileInformationByHandle(handle, &finished) != 0 &&
               opened.dwVolumeSerialNumber == finished.dwVolumeSerialNumber &&
               opened.nFileIndexHigh == finished.nFileIndexHigh &&
               opened.nFileIndexLow == finished.nFileIndexLow &&
               opened.nFileSizeHigh == finished.nFileSizeHigh &&
               opened.nFileSizeLow == finished.nFileSizeLow &&
               CompareFileTime(&opened.ftLastWriteTime,
                               &finished.ftLastWriteTime) == 0;
    }
    if (!CloseHandle(handle) || !okay) return std::nullopt;
#else
#ifndef O_NOFOLLOW
    return std::nullopt;
#else
    int flags = O_RDONLY | O_NOFOLLOW | O_NONBLOCK;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    const int fd = ::open(physical.c_str(), flags);
    if (fd < 0) return std::nullopt;
    struct stat opened{}, finished{};
    bool okay = ::fstat(fd, &opened) == 0 && S_ISREG(opened.st_mode) &&
        opened.st_size > 0 &&
        static_cast<std::uintmax_t>(opened.st_size) == before->length &&
        static_cast<std::uint64_t>(opened.st_dev) == before->volume_id &&
        static_cast<std::uint64_t>(opened.st_ino) == before->file_id;
    while (okay && remaining != 0) {
        const auto amount = static_cast<std::size_t>((std::min)(
            remaining, std::uintmax_t(block.size())));
        const auto count = ::read(fd, block.data(), amount);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { okay = false; break; }
        digest.update(std::string_view(block.data(),
                                      static_cast<std::size_t>(count)));
        remaining -= static_cast<std::size_t>(count);
    }
    if (okay) {
        char extra = 0;
        auto count = ::read(fd, &extra, 1);
        while (count < 0 && errno == EINTR)
            count = ::read(fd, &extra, 1);
        okay = count == 0 && ::fstat(fd, &finished) == 0 &&
            S_ISREG(finished.st_mode) &&
            opened.st_dev == finished.st_dev &&
            opened.st_ino == finished.st_ino &&
            opened.st_size == finished.st_size &&
            opened.st_mtime == finished.st_mtime &&
            opened.st_ctime == finished.st_ctime;
#ifdef __linux__
        okay = okay &&
            opened.st_mtim.tv_nsec == finished.st_mtim.tv_nsec &&
            opened.st_ctim.tv_nsec == finished.st_ctim.tv_nsec;
#endif
    }
    if (::close(fd) != 0 || !okay) return std::nullopt;
#endif
#endif
    // Revalidate the named path after closing the handle. A replaced source,
    // even with a restored mtime and identical size, is never the reviewed mix.
    if (!precision_cue_external_detail::physical_source_unchanged(
            *before, physical, parent, basename))
        return std::nullopt;
    return digest.finalize();
}

// Nonzero deterministic token is only a stale-state fingerprint, NOT an
// authorization or evidence of a private file's contents.
inline std::uint64_t precision_cue_source_snapshot_token(
    const std::filesystem::path& path,
    const precision_cue_external_detail::PhysicalSourceFingerprint& file) {
    std::uint64_t value = 14695981039346656037ULL;
    const auto add = [&](std::uint64_t word) {
        for (unsigned i = 0; i < 8; ++i) {
            value ^= static_cast<unsigned char>((word >> (i * 8)) & 255);
            value *= 1099511628211ULL;
        }
    };
    const auto chars = path.u8string();
    for (const auto c : chars) {
        value ^= static_cast<unsigned char>(c);
        value *= 1099511628211ULL;
    }
    add(file.volume_id);
    add(file.file_id);
    add(static_cast<std::uint64_t>(file.length));
    add(static_cast<std::uint64_t>(
        file.last_write.time_since_epoch().count()));
    return value == 0 ? 1 : value;
}

inline std::uint64_t precision_cue_absent_target_token(
    const std::filesystem::path& path) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const auto c : path.u8string()) {
        value ^= static_cast<unsigned char>(c);
        value *= 1099511628211ULL;
    }
    // Include target absence in token domain. Existing targets must fail
    // closed rather than reuse the preview's absence token.
    value ^= 0xa5;
    value *= 1099511628211ULL;
    return value == 0 ? 1 : value;
}

inline bool precision_cue_target_is_absent(
    const std::filesystem::path& target) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(target, ec);
    return ec == std::errc::no_such_file_or_directory ||
           (!ec && status.type() == std::filesystem::file_type::not_found);
}

inline std::optional<PrecisionCueReviewedExternalCreate>
capture_precision_cue_external_review(
    const PrecisionCuePlan& plan, const std::filesystem::path& physical,
    std::string_view mp3_basename,
    const precision_cue_external_detail::PhysicalSourceFingerprint& initially_selected) {
    namespace fs = std::filesystem;
    const auto proposal = plan_precision_cue_external_export(plan, mp3_basename);
    if (!proposal.eligible || !physical.is_absolute() ||
        physical != physical.lexically_normal())
        return std::nullopt;
    const auto parent = physical.parent_path();
    std::error_code ec;
    if (fs::canonical(parent, ec) != parent || ec) return std::nullopt;
    const auto current = precision_cue_external_detail::physical_source_snapshot(
        physical, parent, mp3_basename);
    if (!current ||
        !precision_cue_external_detail::physical_source_unchanged(
            initially_selected, physical, parent, mp3_basename))
        return std::nullopt;
    const auto source_hash = precision_cue_review_source_sha256(
        physical, parent, mp3_basename);
    if (!source_hash) return std::nullopt;
    const auto target = parent /
        precision_cue_external_detail::utf8_path(proposal.target_cue_basename);
    if (target.parent_path() != parent ||
        !precision_cue_target_is_absent(target)) return std::nullopt;

    PrecisionCueReviewedExternalCreate reviewed;
    auto& req = reviewed.request;
    req.plan = plan;
    req.mp3_basename = std::string(mp3_basename);
    req.physical_mp3_absolute_path = physical;
    req.qualified_sibling_directory = parent;
    req.reviewed_source_snapshot_token =
        precision_cue_source_snapshot_token(physical, *current);
    req.reviewed_target_snapshot_token =
        precision_cue_absent_target_token(target);
    req.reviewed_cue_sha256 = precision_cue_sha256(plan.external_cue_utf8);
    // Edition and physical disc assignment were explicitly reviewed in the
    // native UI. The *separate* create confirmation has NOT happened yet.
    req.reviewed_preflight.preview_available = true;
    req.reviewed_preflight.preview_qualified = true;
    req.reviewed_preflight.explicit_review_resolved = true;
    req.reviewed_preflight.release_identity_confirmed = true;
    req.reviewed_preflight.disc_identity_confirmed = true;
    req.reviewed_preflight.source_snapshot_current = true;
    req.reviewed_preflight.target_snapshot_current = true;
    req.reviewed_preflight.path_qualified = true;
    req.reviewed_preflight.cue_content_qualified = true;
    req.reviewed_preflight.target_exists = false;
    req.reviewed_preflight.overwrite_approved = false;
    req.reviewed_preflight.explicit_user_approval = false;
    req.explicitly_approved_for_external_create = false;
    reviewed.reviewed_source = *current;
    reviewed.reviewed_source_sha256 = *source_hash;
    return reviewed;
}

// Only a verified, still-absent target returns the review's target token.
// A different or inaccessible filesystem state becomes stale immediately.
inline PrecisionCueExternalFreshSnapshot observe_precision_cue_external_review(
    const PrecisionCueReviewedExternalCreate& reviewed) {
    PrecisionCueExternalFreshSnapshot observed;
    observed.preflight = reviewed.request.reviewed_preflight;
    const auto& physical = reviewed.request.physical_mp3_absolute_path;
    const auto& parent = reviewed.request.qualified_sibling_directory;
    const auto current = precision_cue_external_detail::physical_source_snapshot(
        physical, parent, reviewed.request.mp3_basename);
    if (!current ||
        !precision_cue_external_detail::physical_source_unchanged(
            reviewed.reviewed_source, physical, parent,
            reviewed.request.mp3_basename)) {
        observed.preflight.source_snapshot_current = false;
    } else {
        const auto current_hash = precision_cue_review_source_sha256(
            physical, parent, reviewed.request.mp3_basename);
        if (!current_hash || *current_hash != reviewed.reviewed_source_sha256) {
            observed.preflight.source_snapshot_current = false;
        } else {
            observed.source_snapshot_token =
                precision_cue_source_snapshot_token(physical, *current);
        }
    }
    const auto proposal = plan_precision_cue_external_export(
        reviewed.request.plan, reviewed.request.mp3_basename);
    if (!proposal.eligible) {
        observed.preflight.cue_content_qualified = false;
        observed.preflight.target_snapshot_current = false;
        return observed;
    }
    const auto target = parent /
        precision_cue_external_detail::utf8_path(proposal.target_cue_basename);
    if (!precision_cue_target_is_absent(target)) {
        observed.preflight.target_snapshot_current = false;
        observed.preflight.target_exists = true;
    } else {
        observed.target_snapshot_token = precision_cue_absent_target_token(target);
    }
    return observed;
}

// Deliberately does not grant permission. Only the native confirmation handler
// may set BOTH explicit-approval flags on a *copy* of the reviewed request.
inline PrecisionCueExternalCreateResult
create_confirmed_precision_cue_external(
    PrecisionCueReviewedExternalCreate reviewed) {
    if (!reviewed.request.explicitly_approved_for_external_create ||
        !reviewed.request.reviewed_preflight.explicit_user_approval)
        return {PrecisionCueExternalCreateStatus::Rejected,
                "CUE_CREATE_REQUIRES_SEPARATE_USER_CONFIRMATION"};
    const auto created = create_new_precision_cue_external(
        reviewed.request,
        [&reviewed] { return observe_precision_cue_external_review(reviewed); });
    if (created.status == PrecisionCueExternalCreateStatus::Created) {
        const auto after = precision_cue_review_source_sha256(
            reviewed.request.physical_mp3_absolute_path,
            reviewed.request.qualified_sibling_directory,
            reviewed.request.mp3_basename);
        if (!after || *after != reviewed.reviewed_source_sha256)
            return {PrecisionCueExternalCreateStatus::PublishedNeedsReview,
                    "CUE_PUBLISHED_SOURCE_HASH_CHANGED_REVIEW"};
    }
    return created;
}

} // namespace djmeta
