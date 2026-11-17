#pragma once
// Conservative read-only physical-media envelope for a FUTURE embedded writer.
// The full-file content hash covers the default data stream, not other NTFS
// streams, link aliases, encryption, compression, sparse extents or ACLs.
// This probe REJECTS risky envelopes; it DOES NOT authorize replacing source.
#include "djmeta/precision_cue_full_file_snapshot.h"

#include <cstdint>
#include <filesystem>
#include <string>
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
enum class PrecisionCuePhysicalEnvelopeState {
    QualifiedForReadOnlyPlanning,
    NeedsReview
};
struct PrecisionCuePhysicalEnvelope {
    PrecisionCuePhysicalEnvelopeState state =
        PrecisionCuePhysicalEnvelopeState::NeedsReview;
    std::string diagnostic = "PHYSICAL_ENVELOPE_UNQUALIFIED";
    std::uint32_t hard_link_count = 0;
    std::uint32_t stream_count = 0;
    std::uint32_t attributes = 0;
};

// This API is always read-only. Even QualifiedForReadOnlyPlanning must not
// translate into "write safe" without ACL/owner/metadata copying, durable
// backup, Windows handle locking, and atomic-recovery qualification.
inline PrecisionCuePhysicalEnvelope inspect_precision_cue_physical_envelope(
    const PrecisionCuePhysicalFullSnapshot& captured,
    std::string_view reviewed_prefix) {
    using State=PrecisionCuePhysicalEnvelopeState;
    PrecisionCuePhysicalEnvelope result;
    if (!revalidate_precision_cue_physical_full_snapshot(captured,reviewed_prefix)) {
        result.diagnostic="PHYSICAL_ENVELOPE_SOURCE_STALE";
        return result;
    }
    const auto& path=captured.canonical_path;
#ifdef _WIN32
    const HANDLE handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
        OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if (handle==INVALID_HANDLE_VALUE) {
        result.diagnostic="PHYSICAL_ENVELOPE_HANDLE_UNAVAILABLE";
        return result;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const bool available=GetFileInformationByHandle(handle,&info)!=0;
    const bool closed=CloseHandle(handle)!=0;
    if (!available || !closed) {
        result.diagnostic="PHYSICAL_ENVELOPE_INFO_UNAVAILABLE";
        return result;
    }
    result.hard_link_count=info.nNumberOfLinks;
    result.attributes=info.dwFileAttributes;
    constexpr DWORD reject_flags=FILE_ATTRIBUTE_REPARSE_POINT |
        FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_COMPRESSED |
        FILE_ATTRIBUTE_SPARSE_FILE | FILE_ATTRIBUTE_OFFLINE |
        FILE_ATTRIBUTE_DIRECTORY;
    if ((info.dwFileAttributes&reject_flags)!=0 || info.nNumberOfLinks!=1) {
        result.diagnostic="PHYSICAL_ENVELOPE_REPARSE_HARDLINK_OR_SPECIAL";
        return result;
    }
    // FindFirstStreamW explicitly inspects NTFS named streams. Never silently
    // treat unsupported filesystem stream enumeration as "no ADS".
    WIN32_FIND_STREAM_DATA stream{};
    const HANDLE query=FindFirstStreamW(path.c_str(),FindStreamInfoStandard,
                                         &stream,0);
    if (query==INVALID_HANDLE_VALUE) {
        result.diagnostic="PHYSICAL_ENVELOPE_STREAM_ENUMERATION_UNAVAILABLE";
        return result;
    }
    bool streams_ok=true;
    do {
        ++result.stream_count;
        if (result.stream_count>64 ||
            std::wstring_view(stream.cStreamName)!=L"::$DATA") {
            streams_ok=false;
            break;
        }
    } while (FindNextStreamW(query,&stream)!=0);
    const DWORD last_error=GetLastError();
    const bool find_closed=FindClose(query)!=0;
    if (!streams_ok || !find_closed ||
        (last_error!=ERROR_HANDLE_EOF && last_error!=ERROR_SUCCESS) ||
        result.stream_count!=1) {
        result.diagnostic="PHYSICAL_ENVELOPE_NAMED_STREAM_OR_INCOMPLETE_ENUM";
        return result;
    }
    // No guarantees about custom ACLs/security descriptors are made here.
#else
    struct stat info{};
    if (::lstat(path.c_str(),&info)!=0 || !S_ISREG(info.st_mode)) {
        result.diagnostic="PHYSICAL_ENVELOPE_LSTAT_FAILED";
        return result;
    }
    result.hard_link_count=static_cast<std::uint32_t>(info.st_nlink);
    if (info.st_nlink!=1) {
        result.diagnostic="PHYSICAL_ENVELOPE_HARDLINK_ALIAS";
        return result;
    }
#endif
    // The slow full-file check closes the race against stale bytes; it still
    // cannot grant an exclusive host lock. A later writer must fail closed.
    if (!revalidate_precision_cue_physical_full_snapshot(captured,reviewed_prefix)) {
        result.diagnostic="PHYSICAL_ENVELOPE_CHANGED_DURING_INSPECTION";
        return result;
    }
    result.state=State::QualifiedForReadOnlyPlanning;
    result.diagnostic="PHYSICAL_ENVELOPE_READ_ONLY_PLAN_ONLY";
    return result;
}
} // namespace djmeta
