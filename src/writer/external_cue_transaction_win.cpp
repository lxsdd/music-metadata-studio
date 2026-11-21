#include "external_cue_transaction_win.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "djmeta/cue_value_batch_adapter.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace djmeta::online {
namespace {
constexpr std::size_t max_cue_bytes = 8u * 1024u * 1024u;

struct NativeHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit NativeHandle(HANDLE v = INVALID_HANDLE_VALUE) noexcept : value(v) {}
    NativeHandle(const NativeHandle&) = delete;
    NativeHandle& operator=(const NativeHandle&) = delete;
    ~NativeHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    bool valid() const noexcept { return value != INVALID_HANDLE_VALUE; }
    void close() noexcept {
        if (valid()) CloseHandle(value);
        value = INVALID_HANDLE_VALUE;
    }
};
struct StageCleanup {
    std::wstring path;
    ~StageCleanup() { if (!path.empty()) DeleteFileW(path.c_str()); }
};
std::uint64_t joined(DWORD hi, DWORD lo) noexcept {
    return (static_cast<std::uint64_t>(hi) << 32) |
        static_cast<std::uint64_t>(lo);
}
bool regular_native_file(const BY_HANDLE_FILE_INFORMATION& info) noexcept {
    return info.dwFileAttributes != INVALID_FILE_ATTRIBUTES &&
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
                                 FILE_ATTRIBUTE_REPARSE_POINT |
                                 FILE_ATTRIBUTE_READONLY)) == 0 &&
        info.dwVolumeSerialNumber != 0 &&
        joined(info.nFileIndexHigh, info.nFileIndexLow) != 0 &&
        joined(info.nFileIndexHigh, info.nFileIndexLow) != UINT64_MAX &&
        info.nNumberOfLinks == 1;
}
std::string physical_key(const BY_HANDLE_FILE_INFORMATION& info) {
    return "win-file-v1:" + std::to_string(info.dwVolumeSerialNumber) +
        ":" + std::to_string(joined(info.nFileIndexHigh, info.nFileIndexLow));
}
bool same_source(const BY_HANDLE_FILE_INFORMATION& a,
                 const BY_HANDLE_FILE_INFORMATION& b) noexcept {
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
        a.nFileIndexHigh == b.nFileIndexHigh &&
        a.nFileIndexLow == b.nFileIndexLow &&
        a.nFileSizeHigh == b.nFileSizeHigh &&
        a.nFileSizeLow == b.nFileSizeLow &&
        a.ftCreationTime.dwHighDateTime == b.ftCreationTime.dwHighDateTime &&
        a.ftCreationTime.dwLowDateTime == b.ftCreationTime.dwLowDateTime &&
        a.ftLastWriteTime.dwHighDateTime == b.ftLastWriteTime.dwHighDateTime &&
        a.ftLastWriteTime.dwLowDateTime == b.ftLastWriteTime.dwLowDateTime &&
        a.dwFileAttributes == b.dwFileAttributes;
}
bool native_cue_path(const std::wstring& path) noexcept {
    // Only a normal local drive path is qualified at this stage. UNC,
    // device namespaces, ADS and remote filesystem semantics are excluded.
    if (path.size() < 7 || path.size() >= MAX_PATH ||
        !((path[0] >= L'A' && path[0] <= L'Z') ||
          (path[0] >= L'a' && path[0] <= L'z')) ||
        path[1] != L':' || path[2] != L'\\' ||
        path.find(L'\0') != std::wstring::npos ||
        path.find(L':', 2) != std::wstring::npos)
        return false;
    const auto lower=[](wchar_t c) noexcept {
        return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + 32) : c;
    };
    const std::size_t n=path.size();
    return lower(path[n-4]) == L'.' &&
        lower(path[n-3]) == L'c' &&
        lower(path[n-2]) == L'u' &&
        lower(path[n-1]) == L'e';
}
std::wstring containing_directory(const std::wstring& path) {
    const auto pos=path.find_last_of(L'\\');
    if (pos == std::wstring::npos || pos < 2)
        throw std::runtime_error("CUE_NATIVE_DIRECTORY_UNQUALIFIED");
    return path.substr(0,pos+1);
}
BY_HANDLE_FILE_INFORMATION get_info(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION result{};
    if (!GetFileInformationByHandle(file,&result))
        throw std::runtime_error("CUE_NATIVE_IDENTITY_UNAVAILABLE");
    return result;
}
void rewind_file(HANDLE file) {
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN))
        throw std::runtime_error("CUE_NATIVE_SEEK_FAILED");
}
std::string read_exact(HANDLE file) {
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file,&length) || length.QuadPart <= 0 ||
        static_cast<std::uint64_t>(length.QuadPart) > max_cue_bytes)
        throw std::runtime_error("CUE_NATIVE_SOURCE_SIZE_REFUSED");
    rewind_file(file);
    std::string bytes(static_cast<std::size_t>(length.QuadPart),'\0');
    std::size_t cursor=0;
    while (cursor < bytes.size()) {
        DWORD got=0;
        const DWORD want=static_cast<DWORD>(
            (bytes.size()-cursor)>65536u ? 65536u : bytes.size()-cursor);
        if (!ReadFile(file,bytes.data()+cursor,want,&got,nullptr) || got==0)
            throw std::runtime_error("CUE_NATIVE_READ_FAILED");
        cursor+=got;
    }
    return bytes;
}
void write_exact(HANDLE file,std::string_view bytes) {
    std::size_t cursor=0;
    while (cursor<bytes.size()) {
        const DWORD want=static_cast<DWORD>(
            (bytes.size()-cursor)>65536u ? 65536u : bytes.size()-cursor);
        DWORD wrote=0;
        if (!WriteFile(file,bytes.data()+cursor,want,&wrote,nullptr) ||
            wrote!=want)
            throw std::runtime_error("CUE_NATIVE_STAGE_WRITE_FAILED");
        cursor+=wrote;
    }
    if (!FlushFileBuffers(file))
        throw std::runtime_error("CUE_NATIVE_STAGE_FLUSH_FAILED");
}
ExternalCueCommitResult reject(const char* reason) {
    return {ExternalCueCommitStatus::Refused,reason,{}};
}
} // namespace

ExternalCueCommitResult commit_preflighted_external_cue_windows(
    const std::wstring& native_path,
    std::string_view expected_physical_key,
    std::string_view reviewed_source_bytes,
    const CueApplyPreflight& preflight) {
    if (!native_cue_path(native_path) || expected_physical_key.empty() ||
        reviewed_source_bytes.empty() ||
        reviewed_source_bytes.size()>max_cue_bytes ||
        preflight.readiness==CueApplyReadiness::Blocked ||
        !preflight.postimage.eligible ||
        preflight.postimage.bytes.empty() ||
        preflight.postimage.bytes.size()>max_cue_bytes)
        return reject("CUE_NATIVE_PREFLIGHT_OR_PATH_REFUSED");
    const bool changed=preflight.readiness==CueApplyReadiness::DetachedCandidate;
    if (preflight.postimage.changed!=changed ||
        (preflight.postimage.bytes!=reviewed_source_bytes)!=changed ||
        cue_carrier_content_fingerprint(reviewed_source_bytes)==
            cue_carrier_content_fingerprint(preflight.postimage.bytes) &&
            changed)
        return reject("CUE_NATIVE_POSTIMAGE_CONTRADICTION");
    // Native source is held without FILE_SHARE_WRITE. Existing/new writers
    // cannot update the original bytes during staging and replacement.
    NativeHandle source(CreateFileW(native_path.c_str(),GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if (!source.valid()) return reject("CUE_NATIVE_SOURCE_LOCK_FAILED");

    bool replaced=false;
    std::wstring backup;
    StageCleanup stage;
    try {
        const auto original_info=get_info(source.value);
        if (!regular_native_file(original_info) ||
            physical_key(original_info)!=expected_physical_key ||
            (GetFileAttributesW(native_path.c_str()) &
                (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT |
                 FILE_ATTRIBUTE_READONLY)) != 0)
            return reject("CUE_NATIVE_SOURCE_IDENTITY_CHANGED");
        const auto original=read_exact(source.value);
        if (original!=reviewed_source_bytes)
            return reject("CUE_NATIVE_SOURCE_BYTES_CHANGED");
        if (!changed)
            return {ExternalCueCommitStatus::ExactNoop,
                    "CUE_NATIVE_EXACT_NOOP_NO_WRITE",{}};

        // GetTempFileNameW creates a unique file on the SAME volume. No
        // existing destination is overwritten when staging.
        wchar_t temp_name[MAX_PATH]{};
        const auto dir=containing_directory(native_path);
        if (!GetTempFileNameW(dir.c_str(),L"mms",0,temp_name))
            throw std::runtime_error("CUE_NATIVE_STAGE_CREATE_FAILED");
        stage.path=temp_name;
        backup=stage.path+L".bak";
        if (GetFileAttributesW(backup.c_str())!=INVALID_FILE_ATTRIBUTES)
            throw std::runtime_error("CUE_NATIVE_BACKUP_COLLISION");
        NativeHandle staged(CreateFileW(stage.path.c_str(),
            GENERIC_READ | GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,nullptr));
        if (!staged.valid())
            throw std::runtime_error("CUE_NATIVE_STAGE_OPEN_FAILED");
        write_exact(staged.value,preflight.postimage.bytes);
        if (read_exact(staged.value)!=preflight.postimage.bytes)
            throw std::runtime_error("CUE_NATIVE_STAGE_READBACK_MISMATCH");
        staged.close();

        // Recheck both the held inode and current pathname before the one
        // filesystem replacement. Failed checks retain the original file.
        if (read_exact(source.value)!=reviewed_source_bytes ||
            !same_source(original_info,get_info(source.value)))
            throw std::runtime_error("CUE_NATIVE_SOURCE_CHANGED_WHILE_STAGING");
        NativeHandle current(CreateFileW(native_path.c_str(),GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if (!current.valid() ||
            !same_source(original_info,get_info(current.value)) ||
            read_exact(current.value)!=reviewed_source_bytes)
            throw std::runtime_error("CUE_NATIVE_PATH_CHANGED_WHILE_STAGING");
        current.close();

        // ReplaceFileW preserves Windows metadata and keeps the old file at
        // a separate backup path. Never truncate the user's only CUE copy.
        if (!ReplaceFileW(native_path.c_str(),stage.path.c_str(),
                          backup.c_str(),0,nullptr,nullptr))
            throw std::runtime_error("CUE_NATIVE_ATOMIC_REPLACE_FAILED");
        replaced=true;
        NativeHandle final_file(CreateFileW(native_path.c_str(),GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if (!final_file.valid() ||
            !regular_native_file(get_info(final_file.value)) ||
            read_exact(final_file.value)!=preflight.postimage.bytes)
            throw std::runtime_error("CUE_NATIVE_FINAL_READBACK_MISMATCH");
        final_file.close();
        source.close();
        if (!DeleteFileW(backup.c_str()))
            return {ExternalCueCommitStatus::BackupRetained,
                    "CUE_NATIVE_SAVED_BUT_BACKUP_CLEANUP_FAILED",backup};
        return {ExternalCueCommitStatus::Committed,
                "CUE_NATIVE_REPLACED_AND_READBACK_VERIFIED",{}};
    } catch (const std::exception& error) {
        // Never auto-restore over a potentially changed destination. Retain
        // the backup for explicit recovery if replacement was uncertain.
        if (replaced ||
            (!backup.empty() &&
             GetFileAttributesW(backup.c_str())!=INVALID_FILE_ATTRIBUTES))
            return {ExternalCueCommitStatus::BackupRetained,error.what(),backup};
        return reject(error.what());
    }
}
} // namespace djmeta::online
