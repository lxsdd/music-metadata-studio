#pragma once
// SYNTHETIC-QUALIFICATION ONLY: create a temporary full-file embedded CUE
// candidate, independently prove prefix/suffix integrity, then DELETE it.
// No publish/rename/overwrite/rollback operation exists in this API.
// This is NOT connected to native foobar production file actions.
#include "djmeta/precision_cue_full_file_snapshot.h"
#include "djmeta/precision_cue_id3_patch.h"
#include "djmeta/precision_cue_flac_patch.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

namespace djmeta {

enum class PrecisionCueEmbeddedStageStatus {
    Rejected,
    NoOp,
    ReviewRequired,
    VerifiedAndCleaned,
    IoError
};
struct PrecisionCueEmbeddedStageResult {
    PrecisionCueEmbeddedStageStatus status = PrecisionCueEmbeddedStageStatus::Rejected;
    std::string diagnostic = "EMBEDDED_STAGE_NOT_AUTHORIZED";
    std::array<std::uint8_t,32> staged_full_sha256{};
    std::uintmax_t total_bytes = 0;
};
using PrecisionCueEmbeddedReviewedPatch =
    std::variant<PrecisionCueId3Patch,PrecisionCueFlacPatch>;

enum class PrecisionCueEmbeddedStageCheckpoint {
    AfterClone, AfterPatch, AfterIndependentVerification
};

namespace precision_cue_embedded_stage_detail {
struct ScopedTemporaryFile {
    std::filesystem::path name;
    bool owned = false;
    ~ScopedTemporaryFile() {
        if (owned) {
            std::error_code ignored;
            std::filesystem::remove(name,ignored);
        }
    }
    ScopedTemporaryFile(const ScopedTemporaryFile&)=delete;
    ScopedTemporaryFile& operator=(const ScopedTemporaryFile&)=delete;
    explicit ScopedTemporaryFile(std::filesystem::path p):name(std::move(p)) {}
};
inline std::string random_suffix() {
    std::random_device random;
    constexpr char digits[]="0123456789abcdef";
    std::string suffix;
    suffix.reserve(32);
    for (unsigned i=0;i<4;++i) {
        const auto n=static_cast<std::uint32_t>(random());
        for (int j=28;j>=0;j-=4)
            suffix.push_back(digits[(n>>j)&15u]);
    }
    return suffix;
}
inline bool unchanged_suffixes(
    const std::filesystem::path& source,
    const std::filesystem::path& candidate,
    std::uintmax_t prefix_bytes,std::uintmax_t file_bytes) {
    if (prefix_bytes>file_bytes) return false;
    std::ifstream old(source,std::ios::binary), fresh(candidate,std::ios::binary);
    if (!old || !fresh) return false;
    old.seekg(static_cast<std::streamoff>(prefix_bytes));
    fresh.seekg(static_cast<std::streamoff>(prefix_bytes));
    if (!old || !fresh) return false;
    std::array<char,64*1024> before{}, after{};
    std::uintmax_t remaining=file_bytes-prefix_bytes;
    while (remaining>0) {
        const auto n=static_cast<std::size_t>(
            (std::min)(remaining,std::uintmax_t(before.size())));
        old.read(before.data(),static_cast<std::streamsize>(n));
        fresh.read(after.data(),static_cast<std::streamsize>(n));
        if (old.gcount()!=static_cast<std::streamsize>(n) ||
            fresh.gcount()!=static_cast<std::streamsize>(n) ||
            !std::equal(before.begin(),before.begin()+n,after.begin()))
            return false;
        remaining-=n;
    }
    return true;
}
inline bool is_mp3(const std::filesystem::path& physical) {
    return physical.extension()==".mp3";
}
inline bool is_flac(const std::filesystem::path& physical) {
    return physical.extension()==".flac";
}
} // namespace precision_cue_embedded_stage_detail

// Optional checkpoint is for deterministic synthetic failure injection ONLY.
// Despite the approval flag this NEVER replaces or modifies the source.
// A later independent implementation must own the backup, handle locking,
// atomic publication and rollback; this dry run cannot authorize them.
inline PrecisionCueEmbeddedStageResult
verify_precision_cue_embedded_staged_candidate(
    const PrecisionCuePhysicalFullSnapshot& reviewed,
    std::string_view original_metadata_prefix,
    const PrecisionCueEmbeddedReviewedPatch& reviewed_patch,
    bool explicitly_permit_transient_stage = false,
    const std::function<void(PrecisionCueEmbeddedStageCheckpoint,
                             const std::filesystem::path&)>& test_checkpoint = {}) {
    using Status=PrecisionCueEmbeddedStageStatus;
    if (!explicitly_permit_transient_stage)
        return {Status::Rejected,"EMBEDDED_STAGE_NOT_EXPLICITLY_PERMITTED"};
    if (!revalidate_precision_cue_physical_full_snapshot(
            reviewed,original_metadata_prefix))
        return {Status::ReviewRequired,"EMBEDDED_PHYSICAL_SOURCE_STALE"};

    std::string_view postimage_prefix;
    bool preview_ready=false, no_op=false, carrier_matches=false;
    std::array<std::uint8_t,32> before_hash{},after_hash{};
    std::visit([&](const auto& patch) {
        using Type=std::decay_t<decltype(patch)>;
        if constexpr (std::is_same_v<Type,PrecisionCueId3Patch>) {
            carrier_matches=precision_cue_embedded_stage_detail::is_mp3(
                reviewed.canonical_path) &&
                original_metadata_prefix.starts_with("ID3");
            preview_ready=patch.state==PrecisionCueId3PatchState::PreviewReady;
            no_op=patch.state==PrecisionCueId3PatchState::NoOp;
            postimage_prefix=patch.postimage_tag;
        } else {
            carrier_matches=precision_cue_embedded_stage_detail::is_flac(
                reviewed.canonical_path) &&
                original_metadata_prefix.starts_with("fLaC");
            preview_ready=patch.state==PrecisionCueFlacPatchState::PreviewReady;
            no_op=patch.state==PrecisionCueFlacPatchState::NoOp;
            postimage_prefix=patch.postimage_metadata;
        }
        before_hash=patch.source_sha256;
        after_hash=patch.postimage_sha256;
    },reviewed_patch);
    if (!carrier_matches || before_hash!=reviewed.reviewed_prefix_sha256)
        return {Status::ReviewRequired,"EMBEDDED_PREFIX_CARRIER_OR_HASH_INVALID"};
    if (no_op)
        return {Status::NoOp,"EMBEDDED_EXACT_NOOP_NO_STAGING",{},
                reviewed.physical.bytes};
    if (!preview_ready || postimage_prefix.size()!=original_metadata_prefix.size() ||
        postimage_prefix.empty() ||
        after_hash!=precision_cue_sha256(postimage_prefix) ||
        original_metadata_prefix==postimage_prefix)
        return {Status::Rejected,"EMBEDDED_PATCH_PLAN_OR_ALLOCATION_INVALID"};
    try {
        const auto& parent=reviewed.canonical_path.parent_path();
        std::error_code ec;
        if (!parent.is_absolute() ||
            std::filesystem::canonical(parent,ec)!=parent || ec)
            return {Status::Rejected,"EMBEDDED_STAGING_DIRECTORY_NOT_QUALIFIED"};
        // No source basename appears in the temporary name. The original is
        // never opened with write access and never renamed.
        precision_cue_embedded_stage_detail::ScopedTemporaryFile temp(
            parent / (".precision-cue-stage-" +
                precision_cue_embedded_stage_detail::random_suffix() + ".tmp"));
        if (!std::filesystem::copy_file(reviewed.canonical_path,temp.name,
                std::filesystem::copy_options::none,ec) || ec)
            return {Status::IoError,"EMBEDDED_CREATE_ONLY_CLONE_FAILED"};
        temp.owned=true;
        if (test_checkpoint)
            test_checkpoint(PrecisionCueEmbeddedStageCheckpoint::AfterClone,
                            temp.name);
        if (!revalidate_precision_cue_physical_full_snapshot(
                reviewed,original_metadata_prefix))
            return {Status::ReviewRequired,"EMBEDDED_ORIGINAL_CHANGED_DURING_CLONE"};
        {
            // Write ONLY into the privately staged clone; never open source
            // media with write access. The prefix has an invariant byte size.
            std::fstream writer(temp.name,std::ios::binary|
                               std::ios::in|std::ios::out);
            if (!writer)
                return {Status::IoError,"EMBEDDED_CLONE_NOT_WRITABLE"};
            writer.seekp(0);
            writer.write(postimage_prefix.data(),
                         static_cast<std::streamsize>(postimage_prefix.size()));
            writer.flush();
            if (!writer)
                return {Status::IoError,"EMBEDDED_CLONE_WRITE_FAILED"};
            writer.close();
            if (!writer)
                return {Status::IoError,"EMBEDDED_CLONE_FLUSH_OR_CLOSE_FAILED"};
        }
        if (test_checkpoint)
            test_checkpoint(PrecisionCueEmbeddedStageCheckpoint::AfterPatch,
                            temp.name);
        if (std::filesystem::file_size(temp.name,ec)!=reviewed.physical.bytes || ec)
            return {Status::ReviewRequired,"EMBEDDED_STAGED_FILE_LENGTH_CHANGED"};
        const auto staged=capture_precision_cue_physical_full_snapshot(
            temp.name,postimage_prefix);
        if (!staged ||
            !precision_cue_embedded_stage_detail::unchanged_suffixes(
                reviewed.canonical_path,temp.name,
                postimage_prefix.size(),reviewed.physical.bytes))
            return {Status::ReviewRequired,"EMBEDDED_STAGED_CONTENT_MISMATCH"};
        if (test_checkpoint)
            test_checkpoint(
                PrecisionCueEmbeddedStageCheckpoint::AfterIndependentVerification,
                temp.name);
        if (!revalidate_precision_cue_physical_full_snapshot(
                *staged,postimage_prefix) ||
            !revalidate_precision_cue_physical_full_snapshot(
                reviewed,original_metadata_prefix))
            return {Status::ReviewRequired,"EMBEDDED_REVALIDATION_AFTER_STAGE_FAILED"};
        // The staged file is ALWAYS deleted by RAII. Return its digest only.
        return {Status::VerifiedAndCleaned,"EMBEDDED_DRY_RUN_POSTIMAGE_VERIFIED",
                staged->full_file_sha256,staged->physical.bytes};
    } catch (const std::exception&) {
        return {Status::IoError,"EMBEDDED_DRY_RUN_IO_OR_FAULT_INJECTION"};
    }
}
} // namespace djmeta
