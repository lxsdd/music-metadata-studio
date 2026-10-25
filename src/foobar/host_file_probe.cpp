#include "stdafx.h"
#include "host_file_probe.h"

#include <SDK/filesystem.h>

#include <climits>
#include <cstdint>
#include <filesystem>
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

// Observe ALL native parent components, not merely the last file entry:
// a missing target below a junction or a parent replaced between source and
// destination probes must never appear to be a stable raw destination.
// Explicitly no SDK File Operations sanitization, path creation or writes.
HostDestinationParentChain probe_raw_destination_parent_chain_readonly(
    const std::string& literal_candidate,
    const std::string& selected_destination_root) {
    HostDestinationParentChain out;
    try {
        if(literal_candidate.empty() ||
           literal_candidate.find('\0')!=std::string::npos ||
           selected_destination_root.empty() ||
           selected_destination_root.find('\0')!=std::string::npos)
            throw std::invalid_argument("Empty or NUL raw target/root");
        pfc::string8 canonical,native;
        filesystem::g_get_canonical_path(literal_candidate.c_str(),canonical);
        if(canonical.is_empty() ||
           !filesystem::g_get_native_path(
               canonical.get_ptr(),native,fb2k::noAbort))
            throw std::invalid_argument("Unsupported native destination");
        out.native_target=utf8_to_native(native.get_ptr());
        pfc::string8 root_canonical,root_native;
        filesystem::g_get_canonical_path(
            selected_destination_root.c_str(),root_canonical);
        if(root_canonical.is_empty() ||
           !filesystem::g_get_native_path(
               root_canonical.get_ptr(),root_native,fb2k::noAbort))
            throw std::invalid_argument("Host cannot resolve destination root");
        const std::filesystem::path target(
            std::filesystem::path(out.native_target).lexically_normal());
        const std::filesystem::path root(
            std::filesystem::path(utf8_to_native(root_native.get_ptr()))
                .lexically_normal());
        if(!target.is_absolute() || !target.has_parent_path() ||
           !root.is_absolute() || root.empty())
            throw std::invalid_argument("Nonabsolute native destination or root");
        out.native_destination_root=root.native();
        std::filesystem::path parent=target.parent_path();
        bool have_existing_ancestor=false;
        bool found_selected_root=false;
        for(std::size_t depth=0;depth<64;++depth) {
            if(parent.empty())
                break;
            HostDestinationAncestor part;
            part.native_path=parent.native();
            // Use Windows native filename semantics for spelling/case of
            // the selected root, never a guessed UTF-8 path or raw TF text.
            const bool selected_root_here =
                CompareStringOrdinal(part.native_path.c_str(),-1,
                                     out.native_destination_root.c_str(),-1,
                                     TRUE)==CSTR_EQUAL;
            const DWORD attrs=GetFileAttributesW(part.native_path.c_str());
            if(attrs==INVALID_FILE_ATTRIBUTES) {
                const DWORD error=GetLastError();
                if(error!=ERROR_FILE_NOT_FOUND &&
                   error!=ERROR_PATH_NOT_FOUND) {
                    out.rejection="RAW_PARENT_ATTRIBUTES_UNAVAILABLE";
                    return out;
                }
                // Missing directory is not a permission to create it.
                // Its exact position/name is included in both observations.
            } else {
                if(!(attrs & FILE_ATTRIBUTE_DIRECTORY) ||
                   (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    out.rejection="RAW_PARENT_NOT_DIRECTORY_OR_REPARSE";
                    return out;
                }
                const HANDLE handle=CreateFileW(part.native_path.c_str(),
                    FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr,OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                    nullptr);
                if(handle==INVALID_HANDLE_VALUE) {
                    out.rejection="RAW_PARENT_DIRECTORY_HANDLE_UNAVAILABLE";
                    return out;
                }
                BY_HANDLE_FILE_INFORMATION info{};
                const BOOL valid=GetFileInformationByHandle(handle,&info);
                CloseHandle(handle);
                if(!valid ||
                   !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                   (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    out.rejection="RAW_PARENT_DIRECTORY_REPLACED";
                    return out;
                }
                const auto index=file_number(
                    info.nFileIndexHigh,info.nFileIndexLow);
                if(!valid_native_file_identity(
                    info.dwVolumeSerialNumber,index)) {
                    out.rejection="RAW_PARENT_IDENTITY_UNAVAILABLE";
                    return out;
                }
                part.existing=true;
                part.physical_key="win-dir-v1:"+
                    std::to_string(info.dwVolumeSerialNumber)+":"+
                    std::to_string(index);
                part.guard=part.physical_key+":"+
                    std::to_string(time_number(info.ftCreationTime))+":"+
                    std::to_string(time_number(info.ftLastWriteTime))+":"+
                    std::to_string(info.dwFileAttributes);
                have_existing_ancestor=true;
            }
            if(selected_root_here) {
                // File Operations requires the named root to EXIST; it can
                // create nested pattern folders only after separate consent.
                if(!part.existing) {
                    out.rejection="RAW_SELECTED_DESTINATION_ROOT_MISSING";
                    return out;
                }
                found_selected_root=true;
                out.destination_root_physical_key=part.physical_key;
                out.destination_root_guard=part.guard;
            }
            out.ancestors.push_back(std::move(part));
            const auto next=parent.parent_path();
            if(next==parent) {
                out.qualified=have_existing_ancestor && found_selected_root;
                if(!out.qualified)
                    out.rejection=found_selected_root
                        ? "RAW_PARENT_NO_OBSERVED_DIRECTORY"
                        : "RAW_SELECTED_ROOT_NOT_ANCESTOR_OF_TARGET";
                return out;
            }
            parent=next;
        }
        out.rejection="RAW_PARENT_DEPTH_OR_ROOT_UNQUALIFIED";
    } catch(const std::exception&) {
        out.rejection="RAW_PARENT_NATIVE_RESOLUTION_UNQUALIFIED";
    } catch(...) {
        out.rejection="RAW_PARENT_UNSUPPORTED_HOST_FAILURE";
    }
    return out;
}

} // namespace djmeta_foobar
