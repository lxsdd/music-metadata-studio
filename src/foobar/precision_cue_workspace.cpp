#include "stdafx.h"

#include "guid.h"
#include "resource.h"

#include "djmeta/precision_cue.h"
#include "djmeta/precision_cue_apple.h"
#include "djmeta/precision_cue_apple_http.h"
#include "djmeta/precision_cue_apple_link.h"
#include "djmeta/precision_cue_filename.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace djmeta_foobar {
namespace {

constexpr UINT_PTR kPollTimer = 9001;
HWND g_precision_workspace = nullptr; // Only accessed from foobar's UI thread.


struct LocalFile {
    std::string basename;
    std::int64_t duration_ms = 0;
};

struct LookupJob {
    std::atomic<bool> ready{false};
    djmeta::PrecisionCueRelease release;
    std::string error;
    std::jthread worker;
};

// Own workers independently from the dialog. Closing the modeless window
// must NEVER join a network thread on the foobar UI thread, and workers
// must NEVER post to stale/reused HWNDs or access foobar SDK objects.
// Finished jobs are collected at next launch, outstanding jobs joined
// only during process/component teardown.
struct LookupJobPool {
    std::vector<std::shared_ptr<LookupJob>> jobs;
    ~LookupJobPool() {
        // A worker captures its own shared state. Join BEFORE removing the
        // pool's ownership so its final reference can never disappear on
        // that same worker thread (which would self-join/terminate).
        for (const auto& job : jobs) {
            if (job->worker.joinable()) job->worker.join();
        }
    }
};
LookupJobPool g_lookup_pool;

void reap_finished_jobs() {
    auto& jobs = g_lookup_pool.jobs;
    jobs.erase(
        std::remove_if(jobs.begin(), jobs.end(),
            [](const std::shared_ptr<LookupJob>& job) {
                if (!job->ready.load(std::memory_order_acquire)) return false;
                if (job->worker.joinable()) job->worker.join();
                return true;
            }),
        jobs.end());
}

struct Workspace {
    HWND dialog = nullptr;
    bool registered = false;
    std::vector<LocalFile> files;
    std::shared_ptr<LookupJob> job;
};
Workspace* g_pending_workspace = nullptr; // Only main thread, owned until WM_INITDIALOG.

std::wstring from_utf8(std::string_view value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        throw std::invalid_argument("Invalid Unicode length");
    const int bytes = static_cast<int>(value.size());
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), bytes, nullptr, 0);
    if (needed <= 0) throw std::invalid_argument("Invalid Unicode text");
    std::wstring output(static_cast<std::size_t>(needed), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), bytes, output.data(), needed) != needed)
        throw std::invalid_argument("Invalid Unicode conversion");
    return output;
}

std::string to_utf8(std::wstring_view value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        throw std::invalid_argument("Invalid Unicode input length");
    const int chars = static_cast<int>(value.size());
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), chars, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) throw std::invalid_argument("Invalid Unicode input");
    std::string output(static_cast<std::size_t>(needed), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), chars, output.data(), needed, nullptr, nullptr) != needed)
        throw std::invalid_argument("Invalid Unicode conversion");
    return output;
}

std::wstring url_entry(HWND dialog) {
    const HWND child = GetDlgItem(dialog, IDC_PRECISION_APPLE_LINK);
    if (!child) throw std::runtime_error("Apple Music URL field is missing");
    const int length = GetWindowTextLengthW(child);
    if (length <= 0 || length > 512)
        throw std::invalid_argument("Enter one Apple Music album URL (up to 512 characters)");
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    const int written = GetWindowTextW(child, value.data(), length + 1);
    if (written != length) throw std::runtime_error("Could not read album URL");
    value.resize(static_cast<std::size_t>(written));
    return value;
}

void show_text(HWND dialog, std::wstring_view value) {
    const std::wstring text(value);
    SetDlgItemTextW(dialog, IDC_PRECISION_PREVIEW_TEXT, text.c_str());
}

void show_error(HWND dialog, std::string_view error) {
    std::wstring text = L"Precision CUE: unable to produce a verified proposal.\r\n";
    text += L"Nothing was changed. Please review the exact release and local files.\r\n\r\n";
    try { text += from_utf8(error); }
    catch (...) { text += L"Unexpected or invalid provider metadata"; }
    show_text(dialog, text);
}

std::vector<LocalFile> capture_files(metadb_handle_list_cref handles) {
    if (handles.get_count() < 1 || handles.get_count() > 8)
        throw std::invalid_argument("Select between one and eight physical continuous-mix MP3 files");
    std::vector<LocalFile> out;
    for (t_size index = 0; index < handles.get_count(); ++index) {
        const auto& handle = handles[index];
        if (handle->get_subsong_index() != 0)
            throw std::invalid_argument("Select physical MP3 files, not virtual CUE subsongs");
        const std::string path = handle->get_path();
        if (path.rfind("file://", 0) != 0)
            throw std::invalid_argument("Precision CUE preview requires local physical MP3 files");
        const auto last_sep = path.find_last_of("/\\");
        const std::string name = path.substr(last_sep == std::string::npos ? 0 : last_sep + 1);
        if (!djmeta::precision_cue_safe_mp3_basename(name))
            throw std::invalid_argument("An MP3 filename is unsafe or not supported");
        const double seconds = handle->get_length();
        const auto duration = std::isfinite(seconds) && seconds > 0 &&
            seconds < 24 * 60 * 60
            ? static_cast<std::int64_t>(std::llround(seconds * 1000.0)) : 0;
        out.push_back({name, duration});
    }
    // Deterministic ordering; this is a PROPOSAL and never a verified
    // disc-to-file mapping. The UI must display each chosen basename.
    std::sort(out.begin(), out.end(), [](const LocalFile& a, const LocalFile& b) {
        return a.basename < b.basename;
    });
    for (std::size_t i = 1; i < out.size(); ++i)
        if (out[i].basename == out[i - 1].basename)
            throw std::invalid_argument("Duplicate MP3 basenames cannot be mapped safely");
    return out;
}

void show_candidate(Workspace& state, const djmeta::PrecisionCueRelease& release) {
    const auto count = release.tracks.empty() ? 0u : release.tracks.back().disc_number;
    if (count == 0 || count != state.files.size()) {
        throw std::invalid_argument(
            "Selected physical MP3 count differs from provider disc count. "
            "Select all disc MP3s; no partial CUE proposal was produced");
    }
    std::vector<djmeta::PrecisionCuePlan> plans;
    std::wstring details;
    bool duration_warning = false;
    for (unsigned d = 1; d <= count; ++d) {
        const LocalFile& physical = state.files[d - 1];
        djmeta::PrecisionCueDisc disc;
        disc.album_title = release.album_title;
        disc.album_performer = release.album_performer;
        disc.audio_file_basename = physical.basename;
        disc.disc_number = d;
        disc.total_discs = count;
        for (const auto& track : release.tracks) {
            if (track.disc_number == d)
                disc.tracks.push_back({track.title, track.performer, track.duration_ms});
        }
        auto plan = djmeta::make_precision_cue(disc);
        if (physical.duration_ms <= 0) {
            duration_warning = true;
        } else {
            const auto delta = std::llabs(plan.total_duration_ms - physical.duration_ms);
            if (delta > 3000)
                throw std::invalid_argument(
                    "Apple catalog duration does not match the local MP3; no CUE proposal");
            if (delta > 1000) duration_warning = true;
        }
        plans.push_back(std::move(plan));
    }
    details += L"READ-ONLY PREVIEW / REVIEW REQUIRED\r\n";
    details += L"Apple catalog metadata are not independent proof of the exact mixed edition.\r\n";
    details += L"Do not treat frame-aligned CUE markers as sample-exact audio boundaries.\r\n";
    details += L"Nothing has been written to audio, tags, the clipboard or CUE files.\r\n";
    details += L"Album: " + from_utf8(release.album_title) + L"\r\n";
    details += L"Provider: Apple / collection " + from_utf8(release.provider_release_id) + L"\r\n";
    if (duration_warning)
        details += L"WARNING: At least one local duration is unknown or requires review.\r\n";
    details += L"DISC-TO-MP3 ORDER IS PROVISIONAL (sorted by filename).\r\n";
    for (std::size_t i = 0; i < plans.size(); ++i) {
        details += L"\r\n=== DISC " + std::to_wstring(i + 1) +
                   L" / " + from_utf8(state.files[i].basename) + L" ===\r\n";
        details += from_utf8(plans[i].external_cue_utf8);
    }
    show_text(state.dialog, details);
}

void start_lookup(Workspace& state) {
    if (state.job && !state.job->ready.load(std::memory_order_acquire))
        return;
    const auto url = to_utf8(url_entry(state.dialog));
    const auto parsed = djmeta::parse_apple_precision_cue_album_link(url);
    djmeta::ApplePrecisionCueHttpRequest request;
    request.kind = djmeta::ApplePrecisionCueHttpKind::AlbumLookup;
    request.parameter = parsed.collection_id;
    request.storefront = parsed.storefront.empty() ? "DE" : parsed.storefront;
    request.user_initiated = true;
    // Validate before the background worker even starts.
    (void)djmeta::apple_precision_cue_http_path(request);
    reap_finished_jobs();
    auto job = std::make_shared<LookupJob>();
    job->worker = std::jthread([job, request] {
        try {
            const auto json = djmeta::fetch_apple_precision_cue_json(request);
            job->release = djmeta::parse_apple_precision_cue_lookup(json, request.parameter);
        } catch (const std::exception& error) {
            job->error = error.what();
        } catch (...) {
            job->error = "Unexpected Apple API adapter error";
        }
        job->ready.store(true, std::memory_order_release);
    });
    g_lookup_pool.jobs.push_back(job);
    state.job = std::move(job);
    EnableWindow(GetDlgItem(state.dialog, IDC_PRECISION_LOOKUP), FALSE);
    show_text(state.dialog, L"Requesting official Apple album metadata...\r\n"
                            L"foobar2000 remains usable. No files are modified.");
    SetTimer(state.dialog, kPollTimer, 250, nullptr);
}

INT_PTR CALLBACK precision_dialog_proc(HWND dialog, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_INITDIALOG) {
        auto* state = reinterpret_cast<Workspace*>(lp);
        if (!state) return FALSE;
        state->dialog = dialog;
        if (g_pending_workspace == state) g_pending_workspace = nullptr;
        SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        modeless_dialog_manager::g_add(dialog);
        state->registered = true;
        g_precision_workspace = dialog;
        show_text(dialog, L"Paste an Apple Music album URL and choose Look up.\r\n"
                          L"Selected local files are read only; generated CUE text is a "
                          L"proposal requiring independent release verification.");
        return TRUE;
    }
    auto* state = reinterpret_cast<Workspace*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (!state) return FALSE;
    if (message == WM_TIMER && wp == kPollTimer) {
        if (state->job && state->job->ready.load(std::memory_order_acquire)) {
            KillTimer(dialog, kPollTimer);
            EnableWindow(GetDlgItem(dialog, IDC_PRECISION_LOOKUP), TRUE);
            try {
                if (!state->job->error.empty()) show_error(dialog, state->job->error);
                else show_candidate(*state, state->job->release);
            } catch (const std::exception& error) {
                show_error(dialog, error.what());
            }
            state->job.reset();
        }
        return TRUE;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wp) == IDC_PRECISION_LOOKUP && HIWORD(wp) == BN_CLICKED) {
            try { start_lookup(*state); }
            catch (const std::exception& error) { show_error(dialog, error.what()); }
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL && HIWORD(wp) == BN_CLICKED) {
            DestroyWindow(dialog);
            return TRUE;
        }
    }
    if (message == WM_CLOSE) { DestroyWindow(dialog); return TRUE; }
    if (message == WM_NCDESTROY) {
        KillTimer(dialog, kPollTimer);
        if (state->registered) modeless_dialog_manager::g_remove(dialog);
        if (g_precision_workspace == dialog) g_precision_workspace = nullptr;
        SetWindowLongPtrW(dialog, GWLP_USERDATA, 0);
        delete state; // worker is independent, never owns the HWND.
        return TRUE;
    }
    return FALSE;
}

void open_precision_workspace(metadb_handle_list_cref handles) {
    if (g_precision_workspace && IsWindow(g_precision_workspace)) {
        ShowWindow(g_precision_workspace, SW_RESTORE);
        SetForegroundWindow(g_precision_workspace);
        return;
    }
    try {
        auto state = std::make_unique<Workspace>();
        state->files = capture_files(handles);
        // This is a standalone modeless top-level window, not a child/owner
        // that disables foobar playback or playlist navigation.
        Workspace* const raw = state.release();
        g_pending_workspace = raw;
        const HWND window = CreateDialogParamW(
            core_api::get_my_instance(), MAKEINTRESOURCEW(IDD_PRECISION_CUE_PREVIEW),
            nullptr, precision_dialog_proc, reinterpret_cast<LPARAM>(raw));
        if (!window) {
            // WM_INITDIALOG might never have taken ownership, or it might
            // already have destroyed the HWND. Only the untouched pending
            // pointer can be deleted here; never dereference a freed state.
            if (g_pending_workspace) {
                delete g_pending_workspace;
                g_pending_workspace = nullptr;
            }
            throw std::runtime_error("Failed to create Precision CUE native preview");
        }
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
    } catch (const std::exception& error) {
        popup_message::g_show(error.what(), "Precision CUE Preview");
    }
}

class precision_context_menu : public contextmenu_item_simple {
public:
    GUID get_parent() override { return guids::context_group; }
    unsigned get_num_items() override { return 1; }
    t_enabled_state get_enabled_state(unsigned) override {
        return contextmenu_item::DEFAULT_ON;
    }
    void get_item_name(unsigned, pfc::string_base& output) override {
        output = "Precision CUE (Preview)";
    }
    GUID get_item_guid(unsigned) override {
        return {0xcf5f5ed7, 0x7e3c, 0x4b97,
                {0x9c, 0xe3, 0x62, 0x98, 0xb8, 0x2a, 0x68, 0xf4}};
    }
    bool get_item_description(unsigned, pfc::string_base& output) override {
        output = "Look up an Apple Music continuous mix and preview its CUE timing "
                 "for selected local MP3s. Read-only, independent review required.";
        return true;
    }
    bool context_get_display(unsigned index, metadb_handle_list_cref data,
        pfc::string_base& output, unsigned& flags, const GUID& caller) override {
        if (index != 0 || data.get_count() == 0 || data.get_count() > 8)
            return false;
        return contextmenu_item_simple::context_get_display(
            index, data, output, flags, caller);
    }
    void context_command(unsigned index, metadb_handle_list_cref data, const GUID&) override {
        if (index == 0) open_precision_workspace(data);
    }
};

contextmenu_item_factory_t<precision_context_menu> g_precision_context_factory;

} // namespace
} // namespace djmeta_foobar
