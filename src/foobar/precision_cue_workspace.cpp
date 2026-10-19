#include "stdafx.h"

#include "guid.h"
#include "resource.h"

#include "djmeta/precision_cue.h"
#include "djmeta/precision_cue_assemble.h"
#include "djmeta/precision_cue_boundary_audit.h"
#include "djmeta/precision_cue_apple.h"
#include "djmeta/precision_cue_apple_http.h"
#include "djmeta/precision_cue_apple_link.h"
#include "djmeta/precision_cue_request_revision.h"
#include "djmeta/precision_cue_filename.h"
#include "djmeta/precision_cue_write_preflight.h"
#include "djmeta/precision_cue_foobar_path.h"
#include "djmeta/precision_cue_external_inspect.h"
#include "djmeta/precision_cue_external_host_review.h"
#include "djmeta/precision_cue_external_create.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
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
    std::filesystem::path physical_absolute_path;
    djmeta::precision_cue_external_detail::PhysicalSourceFingerprint snapshot;
};

struct LookupJob {
    std::atomic<bool> ready{false};
    djmeta::PrecisionCueRelease release;
    std::vector<djmeta::ApplePrecisionCueAlbumHit> hits;
    djmeta::ApplePrecisionCueHttpKind kind = djmeta::ApplePrecisionCueHttpKind::AlbumLookup;
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
    std::optional<djmeta::PrecisionCueRelease> release;
    std::vector<std::size_t> reviewed_mapping; // file-count sentinel = unassigned
    std::vector<std::string> reviewed_cues; // Only after manual mapping preview.
    std::vector<std::optional<djmeta::PrecisionCueReviewedExternalCreate>> reviewed_exports;
    std::string lookup_url; // Input bound to the asynchronous provider request.
    std::string search_query;
    std::vector<djmeta::ApplePrecisionCueAlbumHit> search_hits;
    bool reviewed_preview_ready = false;
    djmeta::PrecisionCueRequestRevision input_revision;
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
        const auto parsed = djmeta::precision_cue_foobar_local_windows_mp3_path(
            handle->get_path());
        if (!parsed)
            throw std::invalid_argument(
                "Precision CUE requires a physical local drive MP3 path, "
                "not a virtual, remote or ambiguous file URL");
        const auto source_path = djmeta::precision_cue_external_detail::utf8_path(*parsed);
        std::error_code ec;
        const auto canonical = std::filesystem::canonical(source_path, ec);
        if (ec || canonical != source_path.lexically_normal())
            throw std::invalid_argument(
                "Physical MP3 path could not be qualified (symlinks/aliases unsupported)");
        const auto last_sep = parsed->find_last_of("/\\");
        const std::string name = parsed->substr(last_sep + 1);
        const auto physical = djmeta::precision_cue_external_detail::physical_source_snapshot(
            canonical, canonical.parent_path(), name);
        if (!physical)
            throw std::invalid_argument("Physical MP3 identity could not be verified");
        const double seconds = handle->get_length();
        const auto duration = std::isfinite(seconds) && seconds > 0 &&
            seconds < 24 * 60 * 60
            ? static_cast<std::int64_t>(std::llround(seconds * 1000.0)) : 0;
        out.push_back({name, duration, canonical, *physical});
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

std::vector<std::int64_t> provider_disc_totals(
    const djmeta::PrecisionCueRelease& release, std::size_t count) {
    if (count == 0 || count > 8)
        throw std::invalid_argument("Invalid provider disc count");
    std::vector<std::int64_t> totals(count, 0);
    for (const auto& track : release.tracks) {
        if (track.disc_number == 0 || track.disc_number > count ||
            track.duration_ms <= 0 ||
            track.duration_ms > 86400000LL - totals[track.disc_number - 1])
            throw std::invalid_argument("Invalid provider disc duration");
        totals[track.disc_number - 1] += track.duration_ms;
    }
    return totals;
}

void invalidate_cue_copy(Workspace& state) {
    state.reviewed_cues.clear();
    state.reviewed_exports.clear();
    state.reviewed_preview_ready = false;
    EnableWindow(GetDlgItem(state.dialog, IDC_PRECISION_COPY_CUE), FALSE);
    EnableWindow(GetDlgItem(state.dialog, IDC_PRECISION_SAVE_CUE), FALSE);
}

void update_reviewed_actions(Workspace& state) {
    const bool approved = state.reviewed_preview_ready &&
        IsDlgButtonChecked(state.dialog, IDC_PRECISION_EDITION_CONFIRMED) == BST_CHECKED;
    EnableWindow(GetDlgItem(state.dialog, IDC_PRECISION_COPY_CUE), approved);
    const auto chosen = SendDlgItemMessageW(state.dialog, IDC_PRECISION_DISC_PICKER,
                                            CB_GETCURSEL, 0, 0);
    const bool saveable = approved && chosen != CB_ERR &&
        static_cast<std::size_t>(chosen) < state.reviewed_exports.size() &&
        state.reviewed_exports[static_cast<std::size_t>(chosen)].has_value();
    EnableWindow(GetDlgItem(state.dialog, IDC_PRECISION_SAVE_CUE),
                 saveable ? TRUE : FALSE);
}

void show_candidate(Workspace& state, const djmeta::PrecisionCueRelease& release,
                    const std::vector<std::size_t>* reviewed = nullptr) {
    invalidate_cue_copy(state);
    const auto count = release.tracks.empty() ? 0u : release.tracks.back().disc_number;
    if (count == 0 || count != state.files.size()) {
        throw std::invalid_argument(
            "Selected physical MP3 count differs from provider disc count. "
            "Select all disc MP3s; no partial CUE proposal was produced");
    }
    // Read-only source identity recheck: never derive a new approval from
    // a stale foobar selection or provider response.
    for (const auto& file : state.files) {
        if (!djmeta::precision_cue_external_detail::physical_source_unchanged(
                file.snapshot, file.physical_absolute_path,
                file.physical_absolute_path.parent_path(), file.basename))
            throw std::invalid_argument(
                "Selected physical MP3 changed since capture; reselect the file");
    }
    const auto totals = provider_disc_totals(release, count);
    std::vector<djmeta::PrecisionCuePhysicalDisc> physicals;
    for (const auto& file : state.files)
        physicals.push_back({file.basename, file.duration_ms});
    const auto mapping = reviewed
        ? djmeta::review_precision_cue_disc_mapping(totals, physicals, *reviewed)
        : djmeta::resolve_precision_cue_disc_mapping(totals, physicals);
    if (mapping.state == djmeta::PrecisionCueMatchState::Ambiguous)
        throw std::invalid_argument("Ambiguous MP3-to-disc assignment; review required");
    if (mapping.state == djmeta::PrecisionCueMatchState::Rejected)
        throw std::invalid_argument("Local MP3s incompatible with provider disc durations");
    std::vector<djmeta::PrecisionCuePlan> plans;
    std::vector<djmeta::PrecisionCueExternalExportPlan> exports;
    std::vector<std::string> validated_cues;
    std::vector<std::optional<djmeta::PrecisionCueReviewedExternalCreate>> reviewed_exports;
    std::wstring details;
    bool duration_warning = mapping.state != djmeta::PrecisionCueMatchState::Selected;
    for (unsigned d = 1; d <= count; ++d) {
        const LocalFile& physical = state.files[mapping.physical_index_by_disc[d - 1]];
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
        auto export_plan = djmeta::plan_precision_cue_external_export(plan, physical.basename);
        if (!export_plan.eligible)
            throw std::invalid_argument("Generated external CUE export proposal failed validation");
        exports.push_back(std::move(export_plan));
        validated_cues.push_back(plan.external_cue_utf8);
        plans.push_back(std::move(plan));
    }
    if (!djmeta::precision_cue_export_targets_unique(exports))
        throw std::invalid_argument("Two proposed external CUE target names collide");
    details += L"READ-ONLY PREVIEW / REVIEW REQUIRED\r\n";
    details += L"Apple catalog metadata are not independent proof of the exact mixed edition.\r\n";
    details += L"Do not treat frame-aligned CUE markers as sample-exact audio boundaries.\r\n";
    details += L"Nothing has been written to audio, tags, the clipboard or CUE files.\r\n";
    details += L"Album: " + from_utf8(release.album_title) + L"\r\n";
    details += L"Provider: Apple / collection " + from_utf8(release.provider_release_id) + L"\r\n";
    if (duration_warning)
        details += L"WARNING: At least one local duration is unknown or requires review.\r\n";
    details += reviewed
        ? L"DISC-TO-MP3 MAPPING MANUALLY CHOSEN; EXACT EDITION STILL REQUIRES REVIEW.\r\n"
        : L"DISC-TO-MP3 MAPPING IS DURATION-INFERRED, NOT VERIFIED.\r\n";
    for (std::size_t i = 0; i < plans.size(); ++i) {
        details += L"\r\n=== DISC " + std::to_wstring(i + 1) +
                   L" / " + from_utf8(state.files[mapping.physical_index_by_disc[i]].basename) + L" ===\r\n";
        details += L"Proposed sibling CUE (NOT SAVED): " +
                   from_utf8(exports[i].target_cue_basename) + L"\r\n";
        details += from_utf8(plans[i].external_cue_utf8);
        // The existing sibling target is inspected read-only and is NEVER
        // opened with write access. Results are only valid at preview time.
        const auto& file = state.files[mapping.physical_index_by_disc[i]];
        const auto inspection = djmeta::inspect_precision_cue_existing_target(
            plans[i], file.physical_absolute_path, file.basename);
        if (reviewed) {
            reviewed_exports.push_back(
                djmeta::capture_precision_cue_external_review(
                    plans[i], file.physical_absolute_path, file.basename,
                    file.snapshot));
        }
        using TargetState = djmeta::PrecisionCueExistingTargetState;
        switch (inspection.state) {
        case TargetState::Absent:
            details += L"\r\nTARGET CHECK: No existing sibling CUE at preview time.\r\n";
            break;
        case TargetState::Identical:
            details += L"\r\nTARGET CHECK: Existing CUE is byte-identical (NO-OP; keep file).\r\n";
            break;
        case TargetState::Different:
            details += L"\r\nTARGET CHECK: Existing DIFFERENT CUE; NEVER overwrite it.\r\n";
            break;
        case TargetState::Unqualified:
            details += L"\r\nTARGET CHECK: Existing target or path is not safely qualified.\r\n";
            break;
        }
        details += L"Target inspection is read-only and must be repeated before writing.\r\n";
        details += L"\r\n75-FPS QUANTIZATION AUDIT (catalog timing, NOT audio-verified):\r\n";
        for (const auto& boundary : plans[i].boundaries) {
            const auto offset_us = djmeta::precision_cue_quantization_offset_microseconds(boundary);
            details += L"Track " + std::to_wstring(boundary.track_number) +
                       L": " + std::to_wstring(boundary.elapsed_ms) + L" ms -> " +
                       from_utf8(djmeta::cue_frame_to_index(boundary.cue_frame)) +
                       L"; frame rounding " + (offset_us >= 0 ? L"+" : L"") +
                       std::to_wstring(offset_us) + L" us\r\n";
        }
    }
    show_text(state.dialog, details);
    if (reviewed) {
        state.reviewed_cues = std::move(validated_cues);
        state.reviewed_exports = std::move(reviewed_exports);
        state.reviewed_preview_ready = true;
        update_reviewed_actions(state);
    }
}


void clear_search_results(Workspace& state) {
    state.search_query.clear();
    state.search_hits.clear();
    SendDlgItemMessageW(state.dialog, IDC_PRECISION_ALBUM_RESULTS, CB_RESETCONTENT, 0, 0);
}

void reset_disc_controls(Workspace& state) {
    invalidate_cue_copy(state);
    CheckDlgButton(state.dialog, IDC_PRECISION_EDITION_CONFIRMED, BST_UNCHECKED);
    state.release.reset();
    state.reviewed_mapping.clear();
    SendDlgItemMessageW(state.dialog, IDC_PRECISION_DISC_PICKER, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessageW(state.dialog, IDC_PRECISION_MP3_PICKER, CB_RESETCONTENT, 0, 0);
    for (int id : {IDC_PRECISION_DISC_PICKER, IDC_PRECISION_MP3_PICKER,
                   IDC_PRECISION_ASSIGN_DISC, IDC_PRECISION_PREVIEW_ASSIGNED})
        EnableWindow(GetDlgItem(state.dialog, id), FALSE);
}

void initialize_disc_controls(Workspace& state) {
    if (!state.release || state.release->tracks.empty())
        throw std::invalid_argument("Album has no disc tracklist");
    const unsigned count = state.release->tracks.back().disc_number;
    if (count == 0 || count != state.files.size())
        throw std::invalid_argument("Album disc count differs from selected MP3 count");
    state.reviewed_mapping.assign(count, state.files.size());
    for (unsigned d = 0; d < count; ++d) {
        const std::wstring label = L"Disc " + std::to_wstring(d + 1);
        if (SendDlgItemMessageW(state.dialog, IDC_PRECISION_DISC_PICKER, CB_ADDSTRING,
                0, reinterpret_cast<LPARAM>(label.c_str())) < 0)
            throw std::runtime_error("Cannot populate disc picker");
    }
    for (const auto& file : state.files) {
        const auto label = from_utf8(file.basename);
        if (SendDlgItemMessageW(state.dialog, IDC_PRECISION_MP3_PICKER, CB_ADDSTRING,
                0, reinterpret_cast<LPARAM>(label.c_str())) < 0)
            throw std::runtime_error("Cannot populate MP3 picker");
    }
    SendDlgItemMessageW(state.dialog, IDC_PRECISION_DISC_PICKER, CB_SETCURSEL, 0, 0);
    SendDlgItemMessageW(state.dialog, IDC_PRECISION_MP3_PICKER, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    for (int id : {IDC_PRECISION_DISC_PICKER, IDC_PRECISION_MP3_PICKER,
                   IDC_PRECISION_ASSIGN_DISC})
        EnableWindow(GetDlgItem(state.dialog, id), TRUE);
    EnableWindow(GetDlgItem(state.dialog, IDC_PRECISION_PREVIEW_ASSIGNED), FALSE);
}

void assign_selected_disc(Workspace& state) {
    if (!state.release || state.reviewed_mapping.empty())
        throw std::invalid_argument("Look up an album before assigning discs");
    const auto disc = SendDlgItemMessageW(state.dialog, IDC_PRECISION_DISC_PICKER,
                                           CB_GETCURSEL, 0, 0);
    const auto file = SendDlgItemMessageW(state.dialog, IDC_PRECISION_MP3_PICKER,
                                           CB_GETCURSEL, 0, 0);
    if (disc == CB_ERR || file == CB_ERR ||
        static_cast<std::size_t>(disc) >= state.reviewed_mapping.size() ||
        static_cast<std::size_t>(file) >= state.files.size())
        throw std::invalid_argument("Select one disc and one MP3");
    for (std::size_t d = 0; d < state.reviewed_mapping.size(); ++d)
        if (d != static_cast<std::size_t>(disc) &&
            state.reviewed_mapping[d] == static_cast<std::size_t>(file))
            throw std::invalid_argument("This MP3 is already assigned to another disc");
    // Validate a copy before committing the UI choice; partial matches may
    // consume the only file compatible with another disc.
    auto proposed = state.reviewed_mapping;
    proposed[static_cast<std::size_t>(disc)] = static_cast<std::size_t>(file);
    const auto totals = provider_disc_totals(*state.release, proposed.size());
    std::vector<djmeta::PrecisionCuePhysicalDisc> physicals;
    physicals.reserve(state.files.size());
    for (const auto& physical : state.files)
        physicals.push_back({physical.basename, physical.duration_ms});
    const auto validation = djmeta::validate_precision_cue_partial_disc_mapping(
        totals, physicals, proposed);
    if (validation.state != djmeta::PrecisionCueMatchState::Review) {
        if (std::find(validation.diagnostics.begin(), validation.diagnostics.end(),
                      "PARTIAL_DISC_MAPPING_NO_COMPLETION") != validation.diagnostics.end())
            throw std::invalid_argument(
                "This choice leaves no complete duration-compatible disc assignment");
        throw std::invalid_argument(
            "Disc assignment rejected: duplicate, invalid or incompatible MP3");
    }
    invalidate_cue_copy(state);
    state.reviewed_mapping = std::move(proposed);
    const bool complete = std::all_of(state.reviewed_mapping.begin(),
        state.reviewed_mapping.end(),
        [&state](std::size_t index) { return index < state.files.size(); });
    EnableWindow(GetDlgItem(state.dialog, IDC_PRECISION_PREVIEW_ASSIGNED),
                 complete ? TRUE : FALSE);
    std::wstring summary = L"EXPLICIT DISC ASSIGNMENTS (NO WRITES)\r\n";
    for (std::size_t d = 0; d < state.reviewed_mapping.size(); ++d) {
        summary += L"Disc " + std::to_wstring(d + 1) + L": ";
        const auto index = state.reviewed_mapping[d];
        summary += index < state.files.size() ? from_utf8(state.files[index].basename)
                                               : L"(choose an MP3)";
        summary += L"\r\n";
    }
    summary += complete
        ? L"All discs assigned. Preview reviewed mapping to validate durations."
        : L"Assign each disc before previewing the mapping.";
    show_text(state.dialog, summary);
}

void copy_reviewed_disc_cue(Workspace& state) {
    if (!state.reviewed_preview_ready || !state.release ||
        IsDlgButtonChecked(state.dialog, IDC_PRECISION_EDITION_CONFIRMED) != BST_CHECKED)
        throw std::invalid_argument("Review disc mapping and confirm the exact mix edition first");
    for (const auto& file : state.files) {
        if (!djmeta::precision_cue_external_detail::physical_source_unchanged(
                file.snapshot, file.physical_absolute_path,
                file.physical_absolute_path.parent_path(), file.basename)) {
            invalidate_cue_copy(state);
            throw std::invalid_argument(
                "Physical MP3 changed since reviewed preview; reopen Precision CUE");
        }
    }
    const auto selection = SendDlgItemMessageW(state.dialog, IDC_PRECISION_DISC_PICKER,
                                               CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR || static_cast<std::size_t>(selection) >= state.reviewed_cues.size())
        throw std::invalid_argument("Choose a reviewed disc to copy");
    const auto& cue = state.reviewed_cues[static_cast<std::size_t>(selection)];
    const auto wide = from_utf8(cue);
    if (wide.empty() || wide.size() > (std::numeric_limits<std::size_t>::max() / sizeof(wchar_t)) - 1)
        throw std::runtime_error("CUE clipboard text is invalid");
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
    if (!memory) throw std::runtime_error("Clipboard allocation failed");
    auto* buffer = static_cast<wchar_t*>(GlobalLock(memory));
    if (!buffer) { GlobalFree(memory); throw std::runtime_error("Clipboard lock failed"); }
    std::copy(wide.begin(), wide.end(), buffer);
    buffer[wide.size()] = L'\0';
    GlobalUnlock(memory);
    if (!OpenClipboard(state.dialog)) {
        GlobalFree(memory);
        throw std::runtime_error("Clipboard is busy; nothing was copied");
    }
    const bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    if (!copied) { GlobalFree(memory); throw std::runtime_error("Could not copy CUE text"); }
    const auto status = L"Reviewed CUE copied (Unicode). Save manually as UTF-8 .cue; no files changed.";
    SetDlgItemTextW(state.dialog, IDC_PRECISION_SOURCE_HINT, status);
}

// Only this explicit user action can authorize one new external .cue.
// No overwrite path or audio/tag/embedded write API is used.
void save_reviewed_disc_cue(Workspace& state) {
    if (!state.reviewed_preview_ready || !state.release ||
        IsDlgButtonChecked(state.dialog, IDC_PRECISION_EDITION_CONFIRMED) != BST_CHECKED)
        throw std::invalid_argument("Verify the exact edition and review the disc mapping");
    const auto chosen = SendDlgItemMessageW(state.dialog, IDC_PRECISION_DISC_PICKER,
                                            CB_GETCURSEL, 0, 0);
    if (chosen == CB_ERR ||
        static_cast<std::size_t>(chosen) >= state.reviewed_exports.size() ||
        !state.reviewed_exports[static_cast<std::size_t>(chosen)])
        throw std::invalid_argument(
            "Only a reviewed disc with an absent sibling CUE can be saved");
    const auto& original = *state.reviewed_exports[static_cast<std::size_t>(chosen)];
    // The preview and target-absence evidence must still be current BEFORE
    // showing a confirmation containing the final fully qualified path.
    const auto fresh = djmeta::observe_precision_cue_external_review(original);
    if (!fresh.preflight.source_snapshot_current ||
        !fresh.preflight.target_snapshot_current ||
        fresh.source_snapshot_token != original.request.reviewed_source_snapshot_token ||
        fresh.target_snapshot_token != original.request.reviewed_target_snapshot_token) {
        invalidate_cue_copy(state);
        throw std::invalid_argument(
            "Source or target changed after preview. Review the album and disc again");
    }
    const auto proposal = djmeta::plan_precision_cue_external_export(
        original.request.plan, original.request.mp3_basename);
    if (!proposal.eligible)
        throw std::invalid_argument("Generated CUE no longer qualifies for export");
    const auto destination = original.request.qualified_sibling_directory /
        djmeta::precision_cue_external_detail::utf8_path(proposal.target_cue_basename);
    const std::wstring message =
        L"Create ONE new external CUE at:\r\n\r\n" + destination.wstring() +
        L"\r\n\r\nThis never replaces an existing file and never changes MP3s, "
        L"ID3 tags, ReplayGain, FLAC, embedded CUEs, or rekordbox.\r\n\r\n"
        L"The selected mix edition and disc mapping must be correct.\r\n"
        L"Proceed with this new .cue creation?";
    if (MessageBoxW(state.dialog, message.c_str(), L"Precision CUE - explicit create",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    // A deliberately distinct approval after the explicit modal user choice.
    // Fresh OS snapshots are collected AGAIN immediately before publication.
    auto confirmed = original;
    confirmed.request.explicitly_approved_for_external_create = true;
    confirmed.request.reviewed_preflight.explicit_user_approval = true;
    const auto created = djmeta::create_confirmed_precision_cue_external(
        std::move(confirmed));
    using Status = djmeta::PrecisionCueExternalCreateStatus;
    switch (created.status) {
    case Status::Created:
        invalidate_cue_copy(state);
        SetDlgItemTextW(state.dialog, IDC_PRECISION_SOURCE_HINT,
            L"New external CUE created. No audio or tags changed. Reopen to review.");
        break;
    case Status::NoOp:
        invalidate_cue_copy(state);
        SetDlgItemTextW(state.dialog, IDC_PRECISION_SOURCE_HINT,
            L"Identical existing CUE retained (no-op). No audio or tags changed.");
        break;
    default:
        invalidate_cue_copy(state);
        throw std::runtime_error(
            "External CUE NOT created: " + created.diagnostic +
            ". Refresh the reviewed preview; existing files were not overwritten");
    }
}

void start_search(Workspace& state) {
    if (state.job && !state.job->ready.load(std::memory_order_acquire))
        return;
    const auto query = to_utf8(url_entry(state.dialog));
    if (query.find("://") != std::string::npos)
        throw std::invalid_argument(
            "Search requires artist and album text, not a provider URL. "
            "For an Apple album URL use Look up selected; a Deezer URL "
            "is only an identity hint, not an authorized metadata API.");
    djmeta::ApplePrecisionCueHttpRequest request;
    request.kind = djmeta::ApplePrecisionCueHttpKind::AlbumSearch;
    request.parameter = query;
    request.storefront = "DE";
    request.user_initiated = true;
    (void)djmeta::apple_precision_cue_http_path(request);
    reap_finished_jobs();
    reset_disc_controls(state);
    clear_search_results(state);
    auto job = std::make_shared<LookupJob>();
    job->kind = request.kind;
    job->worker = std::jthread([job, request] {
        try {
            const auto json = djmeta::fetch_apple_precision_cue_json(request);
            job->hits = djmeta::parse_apple_precision_cue_album_search(json);
        } catch (const std::exception& error) {
            job->error = error.what();
        } catch (...) {
            job->error = "Unexpected Apple search response";
        }
        job->ready.store(true, std::memory_order_release);
    });
    state.lookup_url = query;
    state.input_revision.begin_request();
    g_lookup_pool.jobs.push_back(job);
    state.job = std::move(job);
    EnableWindow(GetDlgItem(state.dialog, IDC_PRECISION_SEARCH), FALSE);
    EnableWindow(GetDlgItem(state.dialog, IDC_PRECISION_LOOKUP), FALSE);
    show_text(state.dialog, L"Searching official Apple album catalog...\r\n"
                            L"Choose an exact candidate; search results never authorize a CUE.");
    SetTimer(state.dialog, kPollTimer, 250, nullptr);
}

void start_lookup(Workspace& state) {
    if (state.job && !state.job->ready.load(std::memory_order_acquire))
        return;
    const auto input = to_utf8(url_entry(state.dialog));
    djmeta::ApplePrecisionCueHttpRequest request;
    request.kind = djmeta::ApplePrecisionCueHttpKind::AlbumLookup;
    request.user_initiated = true;
    if (input.starts_with("https://")) {
        const auto parsed = djmeta::parse_apple_precision_cue_album_link(input);
        request.parameter = parsed.collection_id;
        request.storefront = parsed.storefront.empty() ? "DE" : parsed.storefront;
    } else {
        if (input != state.search_query || state.search_hits.empty())
            throw std::invalid_argument(
                "Search for artist and album, then explicitly choose an Apple release");
        const auto choice = SendDlgItemMessageW(state.dialog,
            IDC_PRECISION_ALBUM_RESULTS, CB_GETCURSEL, 0, 0);
        if (choice == CB_ERR ||
            static_cast<std::size_t>(choice) >= state.search_hits.size())
            throw std::invalid_argument("Choose one exact Apple album candidate");
        request.parameter =
            state.search_hits[static_cast<std::size_t>(choice)].collection_id;
        request.storefront = "DE";
    }
    // Search hits never authorize a CUE: fetch the full ordered tracklist
    // in a second, explicitly initiated, rate-limited request.
    (void)djmeta::apple_precision_cue_http_path(request);
    reap_finished_jobs();
    reset_disc_controls(state);
    auto job = std::make_shared<LookupJob>();
    job->kind = request.kind;
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
    state.lookup_url = input;
    state.input_revision.begin_request();
    g_lookup_pool.jobs.push_back(job);
    state.job = std::move(job);
    EnableWindow(GetDlgItem(state.dialog, IDC_PRECISION_SEARCH), FALSE);
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
        reset_disc_controls(*state);
        show_text(dialog, L"Paste an Apple Music album URL, or enter artist and album terms and Search albums.\r\n"
                          L"Select the exact candidate before Look up selected. All local files are read only.");
        return TRUE;
    }
    auto* state = reinterpret_cast<Workspace*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (!state) return FALSE;
    if (message == WM_TIMER && wp == kPollTimer) {
        if (state->job && state->job->ready.load(std::memory_order_acquire)) {
            KillTimer(dialog, kPollTimer);
            EnableWindow(GetDlgItem(dialog, IDC_PRECISION_SEARCH), TRUE);
            EnableWindow(GetDlgItem(dialog, IDC_PRECISION_LOOKUP), TRUE);
            try {
                if (!state->input_revision.request_is_current() ||
                    to_utf8(url_entry(dialog)) != state->lookup_url) {
                    reset_disc_controls(*state);
                    clear_search_results(*state);
                    show_error(dialog, "Album text or selected release changed during provider request; retry");
                } else if (!state->job->error.empty()) show_error(dialog, state->job->error);
                else if (state->job->kind == djmeta::ApplePrecisionCueHttpKind::AlbumSearch) {
                    clear_search_results(*state);
                    state->search_hits = std::move(state->job->hits);
                    state->search_query = state->lookup_url;
                    for (const auto& hit : state->search_hits) {
                        const auto label = from_utf8(hit.title + " / " + hit.performer +
                            " (" + std::to_string(hit.track_count) + " tracks; Apple " +
                            hit.collection_id + ")");
                        if (SendDlgItemMessageW(dialog, IDC_PRECISION_ALBUM_RESULTS,
                                CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())) < 0)
                            throw std::runtime_error("Could not populate Apple candidates");
                    }
                    show_text(dialog, state->search_hits.empty()
                        ? L"No Apple albums found. Refine the artist and album search."
                        : L"Choose the exact Apple album candidate in the list, then click "
                          L"Look up selected. The full tracklist must still pass review. "
                          L"Wait 3.1 seconds between Apple requests.");
                } else {
                    state->release = std::move(state->job->release);
                    initialize_disc_controls(*state);
                    try { show_candidate(*state, *state->release); }
                    catch (const std::invalid_argument& error) {
                        if (std::string(error.what()).find("Ambiguous MP3-to-disc assignment")
                                == std::string::npos) throw;
                        show_text(dialog,
                            L"Multiple MP3-to-disc assignments fit the catalog durations.\r\n"
                            L"Choose each disc and its MP3, click Assign disc, then Preview reviewed mapping.\r\n"
                            L"No files are changed.");
                    }
                }
            } catch (const std::exception& error) {
                show_error(dialog, error.what());
            }
            state->job.reset();
        }
        return TRUE;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wp) == IDC_PRECISION_APPLE_LINK && HIWORD(wp) == EN_CHANGE) {
            state->input_revision.input_changed();
            reset_disc_controls(*state);
            clear_search_results(*state);
            return TRUE;
        }
        if (LOWORD(wp) == IDC_PRECISION_ALBUM_RESULTS && HIWORD(wp) == CBN_SELCHANGE) {
            state->input_revision.input_changed();
            reset_disc_controls(*state);
            return TRUE;
        }
        if (LOWORD(wp) == IDC_PRECISION_SEARCH && HIWORD(wp) == BN_CLICKED) {
            try { start_search(*state); }
            catch (const std::exception& error) { show_error(dialog, error.what()); }
            return TRUE;
        }
        if (LOWORD(wp) == IDC_PRECISION_EDITION_CONFIRMED && HIWORD(wp) == BN_CLICKED) {
            update_reviewed_actions(*state);
            return TRUE;
        }
        if (LOWORD(wp) == IDC_PRECISION_SAVE_CUE && HIWORD(wp) == BN_CLICKED) {
            try { save_reviewed_disc_cue(*state); }
            catch (const std::exception& error) { show_error(dialog, error.what()); }
            return TRUE;
        }
        if (LOWORD(wp) == IDC_PRECISION_COPY_CUE && HIWORD(wp) == BN_CLICKED) {
            try { copy_reviewed_disc_cue(*state); }
            catch (const std::exception& error) { show_error(dialog, error.what()); }
            return TRUE;
        }
        if (LOWORD(wp) == IDC_PRECISION_LOOKUP && HIWORD(wp) == BN_CLICKED) {
            try { start_lookup(*state); }
            catch (const std::exception& error) { show_error(dialog, error.what()); }
            return TRUE;
        }
        if (LOWORD(wp) == IDC_PRECISION_DISC_PICKER && HIWORD(wp) == CBN_SELCHANGE) {
            const auto disc = SendDlgItemMessageW(dialog, IDC_PRECISION_DISC_PICKER,
                                                   CB_GETCURSEL, 0, 0);
            const auto selected = disc >= 0 &&
                static_cast<std::size_t>(disc) < state->reviewed_mapping.size()
                ? state->reviewed_mapping[static_cast<std::size_t>(disc)]
                : state->files.size();
            SendDlgItemMessageW(dialog, IDC_PRECISION_MP3_PICKER, CB_SETCURSEL,
                selected < state->files.size() ? static_cast<WPARAM>(selected)
                                               : static_cast<WPARAM>(-1), 0);
            update_reviewed_actions(*state);
            return TRUE;
        }
        if (LOWORD(wp) == IDC_PRECISION_ASSIGN_DISC && HIWORD(wp) == BN_CLICKED) {
            try { assign_selected_disc(*state); }
            catch (const std::exception& error) { show_error(dialog, error.what()); }
            return TRUE;
        }
        if (LOWORD(wp) == IDC_PRECISION_PREVIEW_ASSIGNED && HIWORD(wp) == BN_CLICKED) {
            try {
                if (!state->release) throw std::invalid_argument("No album loaded");
                show_candidate(*state, *state->release, &state->reviewed_mapping);
            } catch (const std::exception& error) { show_error(dialog, error.what()); }
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
        output = "Search or look up an Apple Music continuous mix and preview its CUE timing "
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
