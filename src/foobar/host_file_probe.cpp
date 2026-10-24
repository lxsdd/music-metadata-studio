#include "stdafx.h"
#include "host_file_probe.h"

#include <SDK/filesystem.h>

#include <climits>
#include <cstdint>
#include <string>
#include <string_view>

namespace djmeta_foobar {
namespace {

std::wstring utf8_to_native(std::string_view input) {
    if (input.empty() || input.size() > static_cast<std::size_t>(INT_MAX) ||
        input.find('\0') != std::string_view::npos)
        throw std::invalid_argument("Malformed native file path.");
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (length <= 0) throw std::invalid_argument("Native file path is not UTF-8.");
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            input.data(), static_cast<int>(input.size()),
            out.data(), length) != length)
        throw std::invalid_argument("Native file path conversion failed.");
    return out;
}

std::uint64_t time_number(const FILETIME& t) {
    return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) |
        static_cast<std::uint64_t>(t.dwLowDateTime);
}
std::uint64_t file_number(DWORD high, DWORD low) {
    return (static_cast<std::uint64_t>(high) << 32) |
        static_cast<std::uint64_t>(low);
}

} // namespace

HostFileObservation probe_host_file_readonly(const std::string& path) {
    HostFileObservation out;
    try {
        if (path.empty() || path.find('\0') != std::string::npos) {
            out.detail = "Empty path or embedded NUL.";
            return out;
        }

        // Canonicalize with foobar's own filesystem handler first. URI
        // stripping, ASCII-only case folding and path heuristics are unsafe.
        pfc::string8 canonical, native;
        filesystem::g_get_canonical_path(path.c_str(), canonical);
        out.canonical_path = canonical.get_ptr();
        if (out.canonical_path.empty() ||
            !filesystem::g_get_native_path(canonical.get_ptr(), native, fb2k::noAbort)) {
            out.detail = "foobar cannot resolve a native physical file path.";
            return out;
        }
        const std::wstring native_path = utf8_to_native(native.get_ptr());
        const DWORD attrs = GetFileAttributesW(native_path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                out.state = HostFileState::Missing;
                out.detail = "Physical destination/source is absent.";
            } else {
                out.detail = "Cannot inspect native file attributes.";
            }
            return out;
        }
        if (!valid_native_regular_file_attributes(attrs)) {
            out.detail = "Directory or reparse-point file is not qualified.";
            return out;
        }

        // Minimal read-attributes-only access; no write access or content
        // streaming, no Create/Move/Replace/Delete. FILE_SHARE_DELETE avoids
        // blocking normal foobar operations while the brief probe executes.
        const HANDLE file_handle = CreateFileW(native_path.c_str(),
            FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file_handle == INVALID_HANDLE_VALUE) {
            out.detail = "Cannot open physical file for read-only attributes.";
            return out;
        }
        BY_HANDLE_FILE_INFORMATION native_info{};
        const BOOL have_native_info =
            GetFileInformationByHandle(file_handle, &native_info);
        CloseHandle(file_handle);
        if (!have_native_info) {
            out.detail = "Host cannot provide a physical file ID.";
            return out;
        }
        // A last-component symlink/reparse substitute must never inherit
        // the originally inspected physical file's eligibility. The handle
        // was opened without following that reparse point.
        if (!valid_native_regular_file_attributes(native_info.dwFileAttributes)) {
            out.detail = "Physical file became a reparse point or directory.";
            return out;
        }
        const auto index = file_number(native_info.nFileIndexHigh,
                                       native_info.nFileIndexLow);
        // Unsupported virtual/network filesystems can return an absent
        // index with a nonzero volume serial or the reverse. BOTH parts
        // must be usable; a shared zero index is never a source identity.
        if (!valid_native_file_identity(native_info.dwVolumeSerialNumber, index)) {
            out.detail = "Physical file identity is unavailable.";
            return out;
        }

        // Cross-check the OS handle observation with the foobar SDK's own
        // physical filesystem stats. A disagreement may indicate a racing
        // mutation, alias change or unsupported host filesystem.
        constexpr std::uint32_t requested = stats2_size |
            stats2_timestamp | stats2_fileOrFolder | stats2_readOnly;
        const t_filestats2 stats =
            filesystem::g_get_stats2(canonical.get_ptr(), requested, fb2k::noAbort);
        if (!stats.haveSize() || !stats.haveTimestamp() ||
            !stats.attrib_valid(t_filestats2::attr_folder) ||
            stats.is_folder() ||
            stats.m_size != file_number(native_info.nFileSizeHigh,
                                        native_info.nFileSizeLow)) {
            out.detail = "foobar and native physical file stats are incomplete or disagree.";
            return out;
        }

        out.host_physical_key = "win-file-v1:" +
            std::to_string(native_info.dwVolumeSerialNumber) + ":" +
            std::to_string(index);
        out.source_guard = out.host_physical_key + ":" +
            std::to_string(stats.m_size) + ":" +
            std::to_string(stats.m_timestamp) + ":" +
            std::to_string(time_number(native_info.ftCreationTime)) + ":" +
            std::to_string(time_number(native_info.ftLastWriteTime)) + ":" +
            std::to_string(native_info.dwFileAttributes);
        out.state = HostFileState::ExistingFile;
        out.detail = "Physical identity and source attributes inspected (read-only).";
    } catch (const std::exception&) {
        out.detail = "foobar filesystem identity/statistics probe failed.";
    } catch (...) {
        out.detail = "Unsupported foobar filesystem probe failure.";
    }
    return out;
}

} // namespace djmeta_foobar
