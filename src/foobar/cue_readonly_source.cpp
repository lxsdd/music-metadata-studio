#include "stdafx.h"
#include "cue_readonly_source.h"

#include "host_file_probe.h"
#include "metadata_adapter.h"

#include "djmeta/structural_guard.h"

#include <SDK/filesystem.h>

#include <climits>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace djmeta_foobar {
namespace {

bool external_cue_path(std::string_view path) {
    if (path.size() < 4) return false;
    const char* expected = ".cue";
    const auto extension = path.substr(path.size() - 4);
    for (std::size_t i = 0; i < 4; ++i) {
        const char c = extension[i] >= 'A' && extension[i] <= 'Z'
            ? static_cast<char>(extension[i] - 'A' + 'a') : extension[i];
        if (c != expected[i]) return false;
    }
    return true;
}
std::wstring strict_native_path(std::string_view raw) {
    if (raw.empty() || raw.size() > static_cast<std::size_t>(INT_MAX) ||
        raw.find('\0') != std::string_view::npos)
        throw std::invalid_argument("Unqualified CUE native file path.");
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        raw.data(), static_cast<int>(raw.size()), nullptr, 0);
    if (count <= 0) throw std::invalid_argument("Invalid UTF-8 CUE native path.");
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            raw.data(), static_cast<int>(raw.size()), out.data(), count) != count)
        throw std::runtime_error("Unable to convert native CUE path.");
    return out;
}

std::string exact_external_cue_bytes(const std::string& source_path) {
    constexpr std::uint64_t maximum = 8u * 1024u * 1024u;
    const auto before = probe_host_file_readonly(source_path);
    if (before.state != HostFileState::ExistingFile ||
        before.source_guard.empty() || before.host_physical_key.empty())
        throw std::invalid_argument("External CUE physical source is unqualified.");

    pfc::string8 canonical, native;
    filesystem::g_get_canonical_path(source_path.c_str(), canonical);
    if (!filesystem::g_get_native_path(canonical.get_ptr(), native, fb2k::noAbort))
        throw std::invalid_argument("External CUE has no native local file path.");
    const auto wide = strict_native_path(native.get_ptr());
    const HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot open the external CUE for reading.");
    struct CloseHandleOnExit {
        HANDLE file;
        ~CloseHandleOnExit() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
    } guard{file};

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > maximum)
        throw std::invalid_argument("External CUE exceeds read-only parser size limit.");
    std::string raw(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t cursor = 0;
    while (cursor < raw.size()) {
        const auto remaining = raw.size() - cursor;
        const DWORD chunk = static_cast<DWORD>(remaining > 65536u ? 65536u : remaining);
        DWORD read = 0;
        if (!ReadFile(file, raw.data() + cursor, chunk, &read, nullptr) || read == 0)
            throw std::runtime_error("External CUE read incomplete.");
        cursor += read;
    }

    const auto after = probe_host_file_readonly(source_path);
    if (after.state != HostFileState::ExistingFile ||
        after.host_physical_key != before.host_physical_key ||
        after.source_guard != before.source_guard)
        throw std::runtime_error("External CUE changed while being read.");
    return raw;
}

} // namespace

CueReadOnlySource read_cue_raw_on_demand(
    const metadb_handle_ptr& handle, const std::string& source_path) {
    if (!handle.is_valid())
        throw std::invalid_argument("No audio/CUE selection.");
    CueReadOnlySource result;
    if (external_cue_path(source_path)) {
        result.raw_text = exact_external_cue_bytes(source_path);
        result.carrier = djmeta::CueCarrierKind::ExternalText;
        result.diagnostic = "External CUE source inspected read-only.";
        return result;
    }
    if (handle->get_subsong_index() != 0)
        throw std::invalid_argument(
            "Embedded CUE requires its physical subsong 0; "
            "virtual CUE handles cannot supply a writable physical carrier.");

    const auto snapshot = handle->get_info_ref();
    const auto metadata = metadata_from_file_info(snapshot->info());
    const djmeta::MetadataField* cue = nullptr;
    for (const auto& field : metadata.fields) {
        if (!djmeta::is_protected_cue_metadata(field.name)) continue;
        if (cue)
            throw std::invalid_argument("Multiple embedded CUE carriers are ambiguous.");
        cue = &field;
    }
    if (!cue || cue->values.size() != 1 || cue->values.front().empty() ||
        cue->values.front().size() > 8u * 1024u * 1024u)
        throw std::invalid_argument(
            "No unique embedded text CUESHEET is visible on this physical source.");

    result.carrier = djmeta::CueCarrierKind::EmbeddedText;
    result.raw_text = cue->values.front();
    result.diagnostic = "Embedded textual CUESHEET inspected from physical metadata (read-only).";
    return result;
}

} // namespace djmeta_foobar
