#include "stdafx.h"

#include <SDK/coreDarkMode.h>
#include <SDK/modeless_dialog.h>

#include "batch_preview_dialog.h"
#include "host_file_probe.h"
#include "cue_readonly_source.h"
#include "musicbrainz_http.h"
#include "discogs_http.h"
#include "beatport_public_http.h"
#include "readonly_network_request.h"
#include "batch_table_settings.h"
#include "legacy_routing_profiles.h"
#include "djmeta/named_route_profiles.h"
#include "native_preview_controls.h"
#include "field_evidence_receipt.h"
#include "session_preview_history.h"
#include "review_grid_controls.h"
#include "metadata_adapter.h"
#include "resource.h"
#include "routing_preview.h"
#include "rules_runtime.h"
#include "titleformat_planner.h"

#include "djmeta/batch_preview.h"
#include "djmeta/file_action_intent.h"
#include "djmeta/metadata_diff.h"
#include "djmeta/online_intake.h"
#include "djmeta/online_query_prefill.h"
#include "djmeta/cue_manual_preview.h"
#include "djmeta/musicbrainz_provider.h"
#include "djmeta/musicbrainz_search_review.h"
#include "djmeta/musicbrainz_physical_review.h"
#include "djmeta/musicbrainz_batch_review.h"
#include "djmeta/discogs_batch_review.h"
#include "djmeta/physical_album_batch_scope.h"
#include "djmeta/online_field_staging.h"
#include "djmeta/online_physical_postimage.h"
#include "djmeta/online_field_ui_selection.h"
#include "djmeta/discogs_provider.h"
#include "djmeta/cue_release_review.h"
#include "djmeta/cue_musicbrainz_release_gate.h"
#include "djmeta/cue_review_postimage.h"
#include "djmeta/embedded_cue_sdk_postimage.h"
#include "djmeta/cue_original_inventory.h"
#include "djmeta/beatport_manual_intake.h"
#include "djmeta/beatport_public_page.h"
#include "djmeta/beatport_public_html.h"
#include "djmeta/crates_browser_bridge.h"
#include "djmeta/crates_visible_intake.h"
#include "djmeta/crates_visible_release.h"
#include "djmeta/beatport_release_review.h"
#include "djmeta/discogs_release_review.h"
#include "djmeta/native_candidate_labels.h"
#include "djmeta/discogs_search_hints.h"
#include "djmeta/cue_online_bridge.h"
#include "djmeta/cue_reference_locator.h"
#include "djmeta/cue_physical_selection_audit.h"
#include "djmeta/cue_raw_route_impact.h"
#include "djmeta/cue_inspection_queue.h"
#include "djmeta/cue_file_navigation.h"
#include "djmeta/physical_selection.h"
#include "djmeta/physical_sdk_postimage.h"
#if defined(MMS_ENABLE_PHYSICAL_WRITES)
#include "../writer/physical_tag_writer.h"
#include "external_cue_write_controller.h"
#include "djmeta/physical_write_confirmation.h"
#endif
#include "djmeta/review_decisions.h"
#include "djmeta/workflow_preview_audit.h"
#include "djmeta/workflow_file_action_audit.h"
#include "djmeta/review_physical_postimage.h"
#include "djmeta/unified_review_batch_preflight.h"
#include "djmeta/review_display.h"
#include "djmeta/track_review.h"
#include "djmeta/table_layout.h"
#include "djmeta/staging.h"
#include "djmeta/rules_snapshot.h"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <charconv>
#include <limits>
#include <memory>
#include <optional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace djmeta_foobar {
namespace {

struct PreviewEntry {
    metadb_handle_ptr handle;
    djmeta::MetadataDocument staged;
    djmeta::BatchPreviewInputRow input;
    std::string input_fingerprint;
    std::string route_expression; // per-row foobar titleformat for review recomputation
    djmeta::FileAction file_action=djmeta::FileAction::Move; // preview intent only
    bool file_action_explicitly_staged=false; // default Move is NOT user sidecar consent
    std::string observed_physical_key; // actual host-reported file ID, not a path guess
    std::string observed_source_guard; // read-only file version evidence
    std::string source_probe_detail;   // only shown as a preview diagnostic
    std::string raw_target_probe_detail; // never represents foobar's final FileOps destination
};

// Only the LAST read-only local review/route action is reversible. We keep
// sparse previous rows, never foobar writes, provider credentials or a
// persisted approval. Very large bulk actions discard undo to bound memory.
struct PreviewUndoFrame {
    enum class Kind { Review, Route };
    Kind kind=Kind::Review;
    std::map<std::size_t,PreviewEntry> previous_entries;
    std::map<std::size_t,std::vector<djmeta::ReviewDecision>> previous_decisions;
    RoutePreviewChoice previous_choice;
};
constexpr std::size_t kMaxPreviewUndoSources=4096;

// Pure data captured on the main foobar UI thread before starting a single
// official Discogs GET. The network worker never reads foobar SDK handles,
// local music files, CUE carriers, or native dialog HWND controls.
struct PendingDiscogsContext {
    enum class Kind { Search, Release, MasterYear };
    Kind kind=Kind::Search;
    std::size_t source_index=0;
    std::uint64_t context_version=0;
    bool cue_mode=false;
    std::string cue_before;
    djmeta::MetadataDocument originals;
    std::uint64_t release_id=0;
    std::uint64_t master_id=0; // versions are candidates only
    std::uint64_t master_page=1;
    std::uint64_t result_page=1;
    std::string result_first_path; // verified official page-one strategy
    std::string plan_identity;
    std::string strategy;
    std::size_t attempt_index=0;
    std::size_t total=1;
};

struct PendingMusicBrainzContext {
    enum class Kind { Search, Release };
    Kind operation=Kind::Search;
    std::size_t source_index=0;
    std::uint64_t context_version=0;
    djmeta::online::musicbrainz::SearchKind kind =
        djmeta::online::musicbrainz::SearchKind::Recording;
    bool cue_mode=false;
    std::string cue_before;
    std::string local_title;
    std::string local_artist;
    djmeta::MetadataDocument originals;
    std::string strategy;
    std::string plan_identity; // source+query+kind of this clicked stage
    std::size_t attempt_index=0;
    std::size_t total=1;
    std::string release_mbid;
    // Explicit browser URL identities, never arbitrary network URLs.
    std::string exact_recording_mbid;
    bool direct_release=false;
    // Search pagination is an explicit click, never an automatic scan.
    std::size_t page=1;
    std::string page_one_path;
};

struct PendingBeatportPageContext {
    std::size_t source_index=0;
    std::uint64_t context_version=0;
    std::string url;
    djmeta::MetadataDocument originals;
    std::int64_t duration_ms=-1;
};
// Single exact Discogs source's read-only raw target evidence, NEVER a
// foobar-sanitized final path, frozen approval, or writer authorization.
struct JointRawTargetObservation {
    std::size_t source_index=0;
    std::string raw_candidate;
    std::string source_physical_key;
    std::string source_guard;
    HostFileObservation target;
};
struct PreviewState {
    HWND dialog = nullptr;
    HWND help_tooltips = nullptr; // Native tooltip HWND lives/dies with dialog.
    std::wstring auto_search_text;
    djmeta::online::musicbrainz::RecordingSearchPlan search_plan;
    std::string search_plan_identity;
    std::size_t search_attempt_index = 0;
    // Failed/cancelled HTTPS must not consume the next strategy.
    bool musicbrainz_has_published_attempt=false;
    std::size_t musicbrainz_published_attempt_index=0;
    bool last_musicbrainz_stage_empty=false;
    // Last successfully published official search page, source- and
    // strategy-bound. Retain one selected source's original evidence only.
    std::string musicbrainz_page_one_path;
    std::string musicbrainz_page_identity;
    std::string musicbrainz_page_strategy;
    std::size_t musicbrainz_result_page=0;
    std::size_t musicbrainz_page_attempt_total=0;
    bool musicbrainz_more_pages=false;
    bool musicbrainz_page_cue_mode=false;
    std::string musicbrainz_page_cue_before;
    djmeta::MetadataDocument musicbrainz_page_originals;
    std::wstring search_attempt_notice;
    std::size_t auto_search_track = (std::numeric_limits<std::size_t>::max)();
    bool modeless_registered = false;
    std::uint64_t workspace_generation = 0;
    bool stale = false;
#if defined(MMS_ENABLE_PHYSICAL_WRITES)
    // Shared with the SDK completion; survives modeless window destruction.
    std::shared_ptr<std::atomic_bool> physical_write_pending =
        std::make_shared<std::atomic_bool>(false);
#endif
    bool checking_snapshot = false;
    bool refreshing = false;
    int initial_client_width = 0;
    int initial_client_height = 0;
    int active_dpi = 96;
    int initial_list_bottom = 0;
    int initial_window_width = 0;
    int initial_window_height = 0;
    int initial_window_left = 0;
    int initial_window_top = 0;
    // WM_SIZE tracks maximization before minimize/hide. GetWindowPlacement
    // does not retain WPF_RESTORETOMAXIMIZED reliably in its flags member.
    bool last_nonminimized_maximized=false;
    RECT last_normal_screen_bounds{};
    bool has_last_normal_screen_bounds=false;
    std::vector<NativePreviewResizeChild> resize_controls;
    int review_split_offset_96 = 0;
    int split_drag_start_x = 0;
    int split_drag_start_offset_96 = 0;
    bool split_dragging = false;
    std::vector<PreviewEntry> entries;
    djmeta::CueInspectionQueue cue_inspections; // session-only CUE audit
    std::size_t next_cue_cursor=0; // original captured source ordinal
    std::optional<PreviewUndoFrame> last_preview_undo;
    djmeta::BatchPreviewTable table;
    RoutePreviewChoice current_choice;
    djmeta::RulesTextSnapshot captured_rules;
    std::wstring cell_buffer;
    fb2k::CCoreDarkModeHooks dark;
    HWND list = nullptr;
    HWND metadata_list = nullptr;
    HWND metadata_track_list = nullptr;
    HWND musicbrainz_details = nullptr;
    HWND musicbrainz_details_label = nullptr;
    HWND metadata_filter = nullptr;
    HWND metadata_track_filter = nullptr;
    HWND metadata_scope = nullptr;
    HWND tabs = nullptr;
    HWND history_list = nullptr;
    SessionPreviewHistory preview_history;
    PreviewHistoryFilter history_filter=PreviewHistoryFilter::All;
    std::vector<std::size_t> history_view_order;
    bool show_metadata = true;
    bool show_candidate = false; // third tab, never a write path
    bool cue_inspection_mode = false; // exact CUE source view, no online proposal
    struct CueReturnIdentity {
        std::size_t source_index=0;
        std::string physical_key;
        std::string source_guard;
    };
    std::optional<CueReturnIdentity> cue_return; // transient, reset on batch refresh
    bool cue_candidate_comparison_mode = false; // evidence-only CUE field diff
    // Qualified exact-edition CUE fields can be previewed ONLY in memory
    // from a selected detail row; never physical, never a writer.
    std::optional<djmeta::online::CuePostimageReviewSnapshot> cue_postimage_snapshot;
    std::size_t cue_postimage_source=(std::numeric_limits<std::size_t>::max)();
    bool cue_postimage_discogs=false;
    // Exact CUE byte postimage and selected row IDs for continuous, modeless
    // read-only review. Neither value authorizes an SDK or filesystem write.
    std::optional<djmeta::CueValuePostimage> cue_value_preview;
    std::vector<std::size_t> cue_value_preview_indices;
    bool musicbrainz_live_view = false; // only set from successful official HTTPS
    bool musicbrainz_release_loaded = false;
    djmeta::online::musicbrainz::SearchKind musicbrainz_kind =
        djmeta::online::musicbrainz::SearchKind::Recording;
    std::vector<djmeta::online::musicbrainz::SearchCandidate> musicbrainz_results;
    std::shared_ptr<ReadOnlyNetworkJob> musicbrainz_request;
    std::optional<PendingMusicBrainzContext> musicbrainz_context;
    std::shared_ptr<ReadOnlyNetworkJob> discogs_request;
    std::optional<PendingDiscogsContext> discogs_context;
    std::shared_ptr<ReadOnlyNetworkJob> beatport_public_request;
    std::optional<PendingBeatportPageContext> beatport_public_context;
    bool online_close_requested=false;
    bool online_user_cancel_requested=false; // UI-only, never a write authorization
    ReadOnlyContextVersion online_context_version;
    bool discogs_provider_selected = false; // visible runtime-only source selector
    bool beatport_offline_selected = false; // no approved Beatport API transport
    bool discogs_live_view = false; // last response, never inferred from combo
    bool discogs_search_was_cue = false;
    std::vector<djmeta::online::discogs::SearchHit> discogs_results;
    std::uint64_t discogs_master_id=0,discogs_master_page=0,
                  discogs_master_pages=0;
    std::uint64_t discogs_result_page=0,discogs_result_pages=0;
    std::string discogs_result_first_path;
    std::string discogs_result_identity;
    std::string discogs_result_strategy;
    std::size_t discogs_result_attempt_index=0,discogs_result_attempt_total=0;
    std::optional<djmeta::online::ReleaseEdition> discogs_direct_edition;
    // The only online field selections eligible for a detached preview:
    // exact, API-verified Discogs EDITION and a qualified PHYSICAL file.
    // Never enables a generic writer or virtual-CUE carrier.
    std::optional<djmeta::online::ReleaseEdition> online_physical_edition;
    std::optional<djmeta::online::OnlineFieldReviewSnapshot> online_field_snapshot;
    std::optional<djmeta::online::OnlineFieldStagedPreview> online_field_preview;
    // Host Title-Formatting evaluated twice against detached original and
    // selected online tags. Advisory raw paths only, never FileOps authority.
    bool online_raw_route_valid=false;
    std::string online_raw_route_before, online_raw_route_after;
    std::optional<JointRawTargetObservation> online_raw_target_observation;
    std::vector<std::size_t> online_included_fields;
    std::map<std::size_t,std::string> online_date_targets;
    std::size_t online_edition_source=(std::numeric_limits<std::size_t>::max)();
    std::vector<std::vector<djmeta::online::FieldReviewRow>> musicbrainz_detail_groups;
    std::string musicbrainz_original_title;
    std::string musicbrainz_original_artist;
    bool show_whitespace = false;
    std::size_t candidate_source_index = (std::numeric_limits<std::size_t>::max)();
    std::vector<djmeta::online::FieldReviewRow> candidate_rows;
    std::vector<std::size_t> candidate_view_order; // independent of normalization rows
    int candidate_sort_column = -1; // API relevance until explicit column click
    bool candidate_sort_descending = false;
    djmeta::ReviewGridLayout<4> track_grid;
    djmeta::ReviewGridLayout<5> detail_grid;
    djmeta::ReviewGridLayout<4> candidate_track_grid;
    djmeta::ReviewGridLayout<5> candidate_detail_grid;
    djmeta::ReviewGridLayout<4> online_detail_grid;
    bool online_detail_has_saved=false;
    bool online_detail_columns_applied=false;
    bool candidate_online_track_columns_active=false;
    bool updating_track_selection = false;
    bool updating_musicbrainz_selection = false;
    bool musicbrainz_browse_columns_active = false;
    bool native_grid_candidate_mode = false; // still tracks layout on File locations
    bool existing_tags_view = false; // raw captured physical tags, never proposals
    std::vector<std::pair<std::wstring,std::wstring>> existing_tag_rows;
    std::size_t selected_track_index = 0;
    std::size_t focused_track_index = (std::numeric_limits<std::size_t>::max)();
    djmeta::MetadataFocus metadata_focus = djmeta::MetadataFocus::Music;
    djmeta::TrackDiscovery track_discovery = djmeta::TrackDiscovery::All;
    std::vector<djmeta::TrackReviewSummary> track_summaries;
    std::vector<std::size_t> track_view_order;
    // Basename order is immutable until Refresh, even when review counts change.
    // Cache ascending and descending separately to keep stable equal-name ties.
    std::vector<std::size_t> cached_name_order_ascending;
    std::vector<std::size_t> cached_name_order_descending;
    std::vector<djmeta::MetadataDiffRow> focused_metadata_rows;
    std::vector<djmeta::AnalysisResult> analyses;
    std::vector<std::vector<djmeta::ReviewDecision>> review_decisions;
    std::size_t next_issue_cursor=0; // underlying source order
    std::vector<djmeta::MetadataDiffRow> metadata_rows;
    djmeta::TrackDiffIndex metadata_row_index; // built once per snapshot
    std::vector<std::size_t> metadata_view_order;
    // Headers are display-only; they have no proposal identity.
    std::vector<int> metadata_view_group;
    std::array<bool,3> review_group_expanded{false,false,false};
    std::array<std::size_t,3> review_group_counts{};
    std::vector<std::string> source_labels;
    std::vector<std::string> cached_track_sort_labels;
    std::vector<std::wstring> cached_track_names;
    djmeta::BatchTableLayout layout = djmeta::default_batch_table_layout();
    // Visible ListView item index -> underlying input row identity.
    std::vector<std::size_t> view_order;
    djmeta::BatchLocationQueue location_queue=djmeta::BatchLocationQueue::All;
};

// Owned only by foobar's main UI thread. g_pending_state protects the rare
// CreateDialogParamW failure before WM_INITDIALOG takes ownership.
HWND g_workspace = nullptr;
std::uint64_t g_workspace_generation = 0;
PreviewState* g_pending_state = nullptr;

// Edited search context cancels pending results without ever joining on UI.
void invalidate_online_request(PreviewState& state) {
    state.online_context_version.invalidate();
    state.cue_postimage_snapshot.reset();
    state.cue_value_preview.reset();
    state.cue_value_preview_indices.clear();
    state.online_raw_route_valid=false;
    state.online_raw_route_before.clear();
    state.online_raw_route_after.clear();
    state.online_raw_target_observation.reset();
    state.cue_postimage_source=(std::numeric_limits<std::size_t>::max)();
    // Any provider/track/query change discards prior edition evidence.

    if(state.discogs_request)
        request_readonly_network_stop(*state.discogs_request);
    if(state.musicbrainz_request)
        request_readonly_network_stop(*state.musicbrainz_request);
    if(state.beatport_public_request)
        request_readonly_network_stop(*state.beatport_public_request);
}

// External CUE files may appear as top-level physical handles in foobar. They
// are not audio-file tag targets. Never trust subsong == 0 alone here.
bool is_external_cue_locator(std::string_view path) {
    if (path.size() < 4) return false;
    auto tail = path.substr(path.size() - 4);
    const char last[] = {'.', 'c', 'u', 'e'};
    for (std::size_t i = 0; i < 4; ++i) {
        const char c = tail[i] >= 'A' && tail[i] <= 'Z'
            ? static_cast<char>(tail[i] - 'A' + 'a') : tail[i];
        if (c != last[i]) return false;
    }
    return true;
}

std::wstring from_utf8(std::string_view text) {
    if (text.empty()) return {};
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        throw std::invalid_argument("Preview text is too long.");
    const int length = static_cast<int>(text.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          text.data(), length, nullptr, 0);
    if (count <= 0) return L"[Invalid UTF-8]";
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            text.data(), length, out.data(), count) != count)
        return L"[Invalid UTF-8]";
    return out;
}

std::string to_utf8(std::wstring_view text) {
    if (text.empty()) return {};
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        throw std::invalid_argument("Preview input is too long.");
    const int size = static_cast<int>(text.size());
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text.data(), size, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) throw std::invalid_argument("Invalid Unicode in preview input.");
    std::string out(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text.data(), size, out.data(), bytes, nullptr, nullptr) != bytes)
        throw std::runtime_error("Unicode conversion failed.");
    return out;
}

std::string read_manual_clipboard_text(HWND dialog, SIZE_T max_bytes=131074) {
    if (!OpenClipboard(dialog))
        throw std::runtime_error("Cannot open clipboard.");
    struct CloseClipboardGuard {
        ~CloseClipboardGuard() { CloseClipboard(); }
    } closer;
    HANDLE value = GetClipboardData(CF_UNICODETEXT);
    if (!value)
        throw std::invalid_argument(
            "Copy a plain-text candidate first. Clipboard has no Unicode text.");
    const SIZE_T bytes = GlobalSize(value);
    if (!bytes || bytes > max_bytes || bytes % sizeof(wchar_t) != 0)
        throw std::invalid_argument("Clipboard candidate exceeds size limit.");
    const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(value));
    if (!text) throw std::runtime_error("Cannot read clipboard text.");
    const std::size_t max_chars = bytes / sizeof(wchar_t);
    std::size_t length = 0;
    while (length < max_chars && text[length] != L'\0') ++length;
    if (length == max_chars) {
        GlobalUnlock(value);
        throw std::invalid_argument("Clipboard text is not null-terminated.");
    }
    std::wstring captured(text, length);
    GlobalUnlock(value);
    return to_utf8(captured);
}

std::wstring read_control(HWND window, int id) {
    const HWND child = GetDlgItem(window, id);
    if (!child) throw std::runtime_error("Missing batch editor field.");
    const int count = GetWindowTextLengthW(child);
    if (count < 0 || count > 16384)
        throw std::invalid_argument("Preview field exceeds 16,384 characters.");
    std::wstring out(static_cast<std::size_t>(count) + 1, L'\0');
    const int read = GetWindowTextW(child, out.data(), count + 1);
    out.resize(static_cast<std::size_t>(read));
    return out;
}

bool valid_user_text(std::wstring_view text) {
    if (text.empty()) return false;
    for (wchar_t ch : text) if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}

djmeta::FileAction read_file_action(HWND dialog) {
    switch(SendDlgItemMessageW(dialog,IDC_BATCH_FILE_ACTION,CB_GETCURSEL,0,0)) {
    case 0: return djmeta::FileAction::Move;
    case 1: return djmeta::FileAction::Copy;
    case 2: return djmeta::FileAction::Rename;
    case 3: return djmeta::FileAction::None;
    default: throw std::invalid_argument("Select a file action for the preview.");
    }
}

RoutePreviewChoice read_choice(HWND window) {
    const std::wstring name = read_control(window, IDC_BATCH_PROFILE_PICKER);
    const std::wstring root = read_control(window, IDC_BATCH_DESTINATION);
    const std::wstring script = read_control(window, IDC_BATCH_PATTERN);
    if (!valid_user_text(name) || !valid_user_text(root) ||
        !valid_user_text(script))
        throw std::invalid_argument(
            "Profile, destination and naming expression are required and "
            "must not contain control characters.");
    return {to_utf8(name), to_utf8(root), to_utf8(script)};
}

void load_profile(HWND window,int index) {
    if(index<0) return;
    if(static_cast<std::size_t>(index)<legacy_move_route_count) {
        const auto& p=legacy_move_routes[index];
        SetDlgItemTextW(window,IDC_BATCH_DESTINATION,from_utf8(p.destination_root).c_str());
        SetDlgItemTextW(window,IDC_BATCH_PATTERN,from_utf8(p.foobar_titleformat).c_str());
        return;
    }
    const auto saved=load_named_routes();
    const auto at=static_cast<std::size_t>(index)-legacy_move_route_count;
    if(at>=saved.size()) throw std::invalid_argument("Saved route selection changed.");
    SetDlgItemTextW(window,IDC_BATCH_DESTINATION,from_utf8(saved[at].destination_root).c_str());
    SetDlgItemTextW(window,IDC_BATCH_PATTERN,from_utf8(saved[at].titleformat_expression).c_str());
}

void rebuild_route_picker(HWND window,std::wstring_view name) {
    const auto saved=load_named_routes();
    const auto combo=GetDlgItem(window,IDC_BATCH_PROFILE_PICKER);
    SendMessageW(combo,CB_RESETCONTENT,0,0);
    for(const auto& p:legacy_move_routes) {
        const auto wide=from_utf8(p.name);
        SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide.c_str()));
    }
    for(const auto& p:saved) {
        const auto wide=from_utf8(p.name);
        SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide.c_str()));
    }
    SetWindowTextW(combo,std::wstring(name).c_str());
}

void save_route_template(HWND window) {
    const auto choice=read_choice(window);
    const djmeta::NamedRouteProfile requested{
        choice.display_name,choice.destination_root,choice.titleformat_expression};
    if(!djmeta::named_route_valid(requested))
        throw std::invalid_argument(
            "A named route requires a unique non-preset name, valid destination "
            "and native Title Formatting expression.");
    auto saved=load_named_routes();
    const auto found=std::find_if(saved.begin(),saved.end(),[&](const auto& p) {
        return djmeta::route_name_equal(p.name,requested.name);
    });
    if(found!=saved.end() && *found==requested) {
        SetDlgItemTextW(window,IDC_BATCH_HINT,
            L"Profile already saved. No changes or file operations.");
        return;
    }
    const bool overwrite=found!=saved.end();
    if(overwrite) {
        if(MessageBoxW(window,
            L"Replace the saved named route with these settings? This affects "
            L"future RAW previews only; no files are changed.",
            L"Replace named route",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES) return;
        const auto current=read_choice(window);
        if(current.display_name!=choice.display_name ||
           current.destination_root!=choice.destination_root ||
           current.titleformat_expression!=choice.titleformat_expression ||
           load_named_routes()!=saved)
            throw std::invalid_argument("Named route changed during confirmation.");
    }
    if(!djmeta::upsert_named_route(saved,requested,overwrite)) return;
    store_named_routes(saved);
    rebuild_route_picker(window,from_utf8(requested.name));
    SetDlgItemTextW(window,IDC_BATCH_HINT,
        L"Named route saved to this foobar profile. Preview again for current "
        L"RAW targets; final FileOps/CUE approval remains unavailable.");
}

void delete_route_template(HWND window) {
    const auto name=to_utf8(read_control(window,IDC_BATCH_PROFILE_PICKER));
    auto saved=load_named_routes();
    if(std::none_of(saved.begin(),saved.end(),[&](const auto& p) {
        return djmeta::route_name_equal(p.name,name);
    })) throw std::invalid_argument(
        "Select a saved custom route. Built-in presets cannot be deleted.");
    if(MessageBoxW(window,
        L"Delete this saved route from your foobar profile? "
        L"Current previews and files remain unchanged.",
        L"Delete named route",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES) return;
    if(load_named_routes()!=saved ||
       to_utf8(read_control(window,IDC_BATCH_PROFILE_PICKER))!=name)
        throw std::invalid_argument("Named route changed during confirmation.");
    if(!djmeta::erase_named_route(saved,name))
        throw std::invalid_argument("Saved route disappeared.");
    store_named_routes(saved);
    rebuild_route_picker(window,from_utf8(name));
    SetDlgItemTextW(window,IDC_BATCH_HINT,
        L"Saved route deleted. Current RAW preview and all source files unchanged.");
}


void copy_named_route_templates(HWND window) {
    const auto saved=load_named_routes();
    if(saved.empty()) {
        SetDlgItemTextW(window,IDC_BATCH_HINT,
            L"No custom profiles to copy. Built-in presets are never exported.");
        return;
    }
    const auto transfer=djmeta::export_named_routes_transfer(saved);
    native_copy_audit_text_to_clipboard(window,from_utf8(transfer));
    SetDlgItemTextW(window,IDC_BATCH_HINT,
        L"Copied custom route templates to Windows clipboard (including "
        L"destination roots). No source, CUE, tags or files changed.");
}

void paste_named_route_templates(HWND window) {
    const SIZE_T max_bytes=2*(131072+
        djmeta::named_route_transfer_prefix.size()+1);
    const auto clipboard=read_manual_clipboard_text(window,max_bytes);
    const auto incoming=djmeta::parse_named_routes_transfer(clipboard);
    if(!incoming || incoming->empty())
        throw std::invalid_argument(
            "Clipboard has no valid MMS-NAMED-ROUTES/1 custom templates.");
    const auto before=load_named_routes();
    const auto selected=read_control(window,IDC_BATCH_PROFILE_PICKER);
    const auto plan=djmeta::plan_named_routes_import(before,*incoming);
    if(!plan.changed()) {
        SetDlgItemTextW(window,IDC_BATCH_HINT,
            L"All imported templates already match; no settings changed.");
        return;
    }
    std::wstring question=
        L"Import "+std::to_wstring(plan.added)+L" new and replace "+
        std::to_wstring(plan.replaced)+L" existing named route(s)?\n\n"
        L"Replacements affect future previews only. Clipboard templates "
        L"may contain local destination paths. Built-in presets and "
        L"current RAW plans remain unchanged. No files are moved or written.";
    if(MessageBoxW(window,question.c_str(),L"Import named routes",
        MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES) return;
    if(load_named_routes()!=before ||
       read_control(window,IDC_BATCH_PROFILE_PICKER)!=selected)
        throw std::invalid_argument(
            "Named route settings changed during import confirmation.");
    store_named_routes(plan.merged);
    rebuild_route_picker(window,selected);
    SetDlgItemTextW(window,IDC_BATCH_HINT,
        L"Imported templates to this foobar profile. Select a template "
        L"and Preview again; FileOps/CUE approval remains unavailable.");
}

void verify_rules_snapshot(const djmeta::RulesTextSnapshot& captured) {
    // One shared-rule file read per whole-batch checkpoint, never one
    // read per track in the 54k-item host library.
    const auto current = load_rules_text();
    djmeta::require_rules_snapshot(captured, current.json, current.source_label);
}

void verify_snapshot(const PreviewEntry& entry) {
    const auto info = entry.handle->get_info_ref();
    const auto latest = metadata_from_file_info(info->info());
    if (djmeta::fingerprint(latest) != entry.input_fingerprint)
        throw std::runtime_error(
            "Metadata changed since this preview was created. "
            "Close and reopen Prepare Tracks.");
}

void reset_raw_target_observation(PreviewEntry& entry) {
    entry.input.raw_target_presence = djmeta::RawTargetPresence::NotInspected;
    entry.input.raw_target_physical_key.clear();
    entry.input.raw_target_guard.clear();
    entry.input.raw_target_host_canonical.clear();
    entry.raw_target_probe_detail.clear();
    // This flag is reserved for the actual host-resolved, post-sanitization
    // destination. A raw file probe must never set it.
    entry.input.filesystem_target_checked = false;
}

// A live overview of the entire captured selection, not just the focused
// proposal or the currently sorted FileOps row. No SDK writer is reachable.
std::vector<std::size_t> captured_cue_carriers(const PreviewState& state) {
    std::vector<std::size_t> rows;
    for(std::size_t i=0;i<state.entries.size();++i)
        if(is_external_cue_locator(state.entries[i].input.source_path))
            rows.push_back(i);
    return rows;
}
// Resolve a single highlighted physical File locations row against current
// CUE FILE receipts. Sorted/filtered ListView positions are never source IDs.
// This is display/navigation only and never authorizes an operation.
std::vector<djmeta::CueRelatedCarrier> related_cues_for_selected_file(
    const PreviewState& state) {
    if(!state.list || state.show_metadata || state.stale || state.refreshing ||
       state.cue_inspections.plan.size()!=state.entries.size() ||
       ListView_GetSelectedCount(state.list)!=1)
        return {};
    const int selected=ListView_GetNextItem(state.list,-1,LVNI_SELECTED);
    if(selected<0 || static_cast<std::size_t>(selected)>=state.view_order.size())
        return {};
    const auto source=state.view_order[static_cast<std::size_t>(selected)];
    if(source>=state.entries.size()) return {};
    const auto& entry=state.entries[source];
    if(!entry.input.physical_source_qualified ||
       entry.handle->get_subsong_index()!=0 ||
       is_external_cue_locator(entry.input.source_path))
        return {};
    return djmeta::related_inspected_cue_carriers(
        state.cue_inspections,captured_cue_carriers(state),source);
}
void update_workflow_audit_hint(PreviewState& state) {
    if(!state.dialog || state.show_metadata) return;
    const auto audit=djmeta::audit_workflow_preview(
        state.table,state.analyses,state.review_decisions);
    const std::wstring hint=L"File queue "+std::to_wstring(state.view_order.size())+
        L"/"+std::to_wstring(state.table.rows.size())+
        L" visible (whole batch audited). Batch "+std::to_wstring(audit.tracks)+
        L": local explicit changes "+std::to_wstring(audit.explicit_changed_values)+
        L" in "+std::to_wstring(audit.normalizer_changed_tracks)+
        L" tracks; "+std::to_wstring(audit.normalizer_unchanged_tracks)+
        L" without staged changes; pending tracks "+
        std::to_wstring(audit.tracks_with_pending)+
        L" (SAFE "+std::to_wstring(audit.pending_safe_values)+
        L", semantic "+std::to_wstring(audit.pending_semantic_values)+
        L"); raw collisions "+std::to_wstring(audit.raw_collision_tracks)+
        L", FileOps unqualified "+std::to_wstring(audit.file_plan_blocked_tracks)+
        L". Action-stage filters show BLOCKED / RAW ONLY / SKIP from the "
        L"complete action ledger; Preview all and Audit still cover hidden "
        L"rows. No writes.";
    const auto cue_ids=captured_cue_carriers(state);
    const auto cue_summary=state.cue_inspections.plan.size()==state.entries.size()
        ? state.cue_inspections.counts(cue_ids) : djmeta::CueQueueCounts{};
    const auto detailed=hint+(cue_ids.empty() ? std::wstring{} :
        L" CUE carriers "+std::to_wstring(cue_ids.size())+
        L", needing inspection "+std::to_wstring(
            cue_summary.missing+cue_summary.stale+cue_summary.blocked)+L".");
    SetDlgItemTextW(state.dialog,IDC_BATCH_HINT,
        native_workspace_display_hint(state.stale,detailed.c_str()));
}

std::vector<djmeta::BatchPreviewInputRow> current_batch_route_inputs(
    const PreviewState& state) {
    std::vector<djmeta::BatchPreviewInputRow> inputs;
    inputs.reserve(state.entries.size());
    // File Locations must disclose the accepted local + selected exact
    // Discogs fields in the SAME raw collision table, not silently show only
    // the local normalizer naming candidate. The complete edition remains
    // an advisory route overlay: no host-resolved target or CUE approval.
    for (std::size_t index=0;index<state.entries.size();++index) {
        auto input=state.entries[index].input;
        if(native_recheck_preserved_online_selection(
               state.online_field_snapshot.has_value(),
               state.online_edition_source,index,
               state.online_included_fields.size())) {
            input.raw_relative_path=state.online_raw_route_valid
                ? state.online_raw_route_after:std::string{};
            input.raw_target_presence=djmeta::RawTargetPresence::NotInspected;
            input.raw_target_physical_key.clear();
            input.raw_target_guard.clear();
            input.raw_target_host_canonical.clear();
            input.filesystem_target_checked=false;
            input.cue_dependencies_checked=false;
            if(state.online_raw_route_valid &&
               state.online_raw_target_observation) {
                const auto& probe=*state.online_raw_target_observation;
                const auto candidate=input.destination_root+"\\"+
                    input.raw_relative_path;
                if(native_joint_raw_observation_matches(
                       probe.source_index,index,probe.raw_candidate,candidate,
                       probe.source_physical_key,
                       state.entries[index].observed_physical_key,
                       probe.source_guard,
                       state.entries[index].observed_source_guard)) {
                    switch(probe.target.state) {
                    case HostFileState::Missing:
                        input.raw_target_presence=djmeta::RawTargetPresence::Missing;
                        input.raw_target_host_canonical=probe.target.canonical_path;
                        break;
                    case HostFileState::ExistingFile:
                        input.raw_target_presence=djmeta::RawTargetPresence::Existing;
                        input.raw_target_host_canonical=probe.target.canonical_path;
                        input.raw_target_physical_key=probe.target.host_physical_key;
                        input.raw_target_guard=probe.target.source_guard;
                        break;
                    case HostFileState::Unqualified:
                        input.raw_target_presence=djmeta::RawTargetPresence::Unqualified;
                        break;
                    }
                }
            }
        }
        inputs.push_back(std::move(input));
    }
    return inputs;
}

std::vector<djmeta::CueQueuePlanRow> current_cue_plan_rows(
    const PreviewState& state,
    const std::vector<djmeta::BatchPreviewInputRow>& inputs) {
    if(inputs.size()!=state.entries.size() ||
       inputs.size()!=state.table.rows.size())
        throw std::invalid_argument("CUE_QUEUE_PLAN_SHAPE_STALE");
    std::vector<djmeta::CueQueuePlanRow> rows;
    rows.reserve(inputs.size());
    for(std::size_t i=0;i<inputs.size();++i) {
        const auto& source=state.entries[i];
        const auto& input=inputs[i];
        rows.push_back({source.observed_physical_key,
            source.observed_source_guard,input.destination_root,
            input.raw_relative_path,source.file_action,
            source.file_action_explicitly_staged,
            input.physical_source_qualified,
            input.semantic_proposals_pending,
            state.table.rows[i].has_raw_target_collision});
    }
    return rows;
}

void update_table(PreviewState& state) {
    // Preserve underlying identities, NOT virtual screen positions, when
    // a sort or user override causes displayed rows to change order.
    std::set<std::size_t> selected_entries;
    if (state.list) {
        int view_row = -1;
        while ((view_row = ListView_GetNextItem(state.list, view_row, LVNI_SELECTED)) >= 0) {
            const auto index = static_cast<std::size_t>(view_row);
            if (index < state.view_order.size())
                selected_entries.insert(state.view_order[index]);
        }
    }

    const auto inputs=current_batch_route_inputs(state);
    state.table = djmeta::describe_batch_preview(inputs);
    state.cue_inspections.observe(current_cue_plan_rows(state,inputs));
    const auto sorted=djmeta::sort_batch_table_view(
        inputs, state.table, state.layout.sort_column, state.layout.sort_descending);
    if(state.location_queue==djmeta::BatchLocationQueue::CueAll ||
       state.location_queue==djmeta::BatchLocationQueue::CueAttention ||
       state.location_queue==djmeta::BatchLocationQueue::CueRawOnly ||
       state.location_queue==djmeta::BatchLocationQueue::CueOriginalKept) {
        state.view_order=djmeta::filter_cue_queue_view(
            state.cue_inspections,captured_cue_carriers(state),
            sorted,state.location_queue);
    } else if(state.location_queue==djmeta::BatchLocationQueue::FileActionBlocked ||
       state.location_queue==djmeta::BatchLocationQueue::FileActionRawOnly ||
       state.location_queue==djmeta::BatchLocationQueue::FileActionSkipped) {
        // A stage queue is based on the SAME complete-batch review and route
        // evidence as Audit/Copy issues, not on visible rows or UI labels.
        const auto review=djmeta::audit_workflow_preview(
            state.table,state.analyses,state.review_decisions);
        std::vector<djmeta::FileAction> actions;
        actions.reserve(state.entries.size());
        for(const auto& entry:state.entries) actions.push_back(entry.file_action);
        const auto ledger=djmeta::audit_workflow_file_actions(
            state.table,inputs,actions,review);
        state.view_order=djmeta::filter_workflow_file_action_view(
            ledger,sorted,state.location_queue);
    } else {
        state.view_order=djmeta::filter_batch_location_view(
            state.table,sorted,state.location_queue);
    }

    if (state.list) {
        ListView_SetItemCountEx(state.list, static_cast<int>(state.view_order.size()),
            LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
        // Owner-data selections are VIEW positions; never let hidden rows
        // inherit a stale selection after a filter or sort.
        ListView_SetItemState(state.list, -1, 0, LVIS_SELECTED);
        if (!selected_entries.empty()) {
            for (std::size_t i = 0; i < state.view_order.size(); ++i)
                if (selected_entries.count(state.view_order[i]))
                    ListView_SetItemState(state.list, static_cast<int>(i),
                        LVIS_SELECTED, LVIS_SELECTED);
        }
        InvalidateRect(state.list, nullptr, FALSE);
    }
    update_workflow_audit_hint(state);
}

const char* issue_status_text(std::string_view issue) {
    if (issue == "PHYSICAL_SOURCE_UNQUALIFIED") return "Physical source: REVIEW";
    if (issue == "TARGET_EXPRESSION_EMPTY") return "Empty target: REVIEW";
    if (issue == "UNSAFE_RAW_DESTINATION_ROOT") return "Destination root: unsafe";
    if (issue == "UNSAFE_RAW_RELATIVE_TARGET") return "Raw target: unsafe";
    if (issue == "DUPLICATE_RAW_TARGET") return "Duplicate raw target";
    if (issue == "RAW_TARGET_CASE_OR_SEPARATOR_COLLISION")
        return "Possible Windows target collision";
    if (issue == "RAW_TARGET_ALIASES_SOURCE") return "Raw target: source alias";
    if (issue == "RAW_TARGET_IS_BATCH_SOURCE") return "Raw target: batch source";
    if (issue == "RAW_TARGET_SHARED_PHYSICAL_ID") return "Raw targets: same file";
    if (issue == "RAW_TARGET_SHARED_HOST_CANONICAL")
        return "Raw targets: same host path";
    if (issue == "RAW_TARGET_HOST_CANONICAL_UNQUALIFIED")
        return "Raw host target: unqualified";
    if (issue == "RAW_TARGET_EXISTS") return "Raw candidate exists";
    if (issue == "RAW_TARGET_OBSERVATION_CONTRADICTORY")
        return "Raw target: inconsistent observation";
    if (issue == "RAW_TARGET_PROBE_UNQUALIFIED") return "Raw target: unqualified";
    if (issue == "UNAPPROVED_METADATA_PROPOSALS") return "Metadata: REVIEW";
    if (issue == "CUE_DEPENDENCIES_UNCHECKED") return "Final CUE links: not checked";
    if (issue == "FILESYSTEM_TARGET_UNCHECKED") return "Final target: not checked";
    return "Other safety finding";
}

djmeta::WorkflowFileActionAudit current_file_action_audit(
    const PreviewState& state,const djmeta::WorkflowPreviewAudit& review) {
    const auto inputs=current_batch_route_inputs(state);
    const auto recomputed=djmeta::describe_batch_preview(inputs);
    if(recomputed.rows.size()!=state.table.rows.size() ||
       recomputed.raw_collision_items!=state.table.raw_collision_items ||
       recomputed.unqualified_rows!=state.table.unqualified_rows)
        throw std::invalid_argument("File action audit: route snapshot stale.");
    for(std::size_t i=0;i<recomputed.rows.size();++i) {
        if(recomputed.rows[i].issues!=state.table.rows[i].issues ||
           recomputed.rows[i].raw_destination!=state.table.rows[i].raw_destination ||
           recomputed.rows[i].raw_target_presence!=state.table.rows[i].raw_target_presence ||
           recomputed.rows[i].has_raw_target_collision!=state.table.rows[i].has_raw_target_collision)
            throw std::invalid_argument("File action audit: route evidence changed.");
    }
    std::vector<djmeta::FileAction> actions;
    actions.reserve(state.entries.size());
    for(const auto& entry:state.entries) actions.push_back(entry.file_action);
    return djmeta::audit_workflow_file_actions(
        state.table,inputs,actions,review);
}

std::wstring cue_inspection_summary(const PreviewState& state) {
    if(state.cue_inspections.plan.size()!=state.entries.size())
        return L"CUE inspection queue not initialized.";
    const auto s=state.cue_inspections.counts(captured_cue_carriers(state));
    return L"CUE carriers: "+std::to_wstring(s.total)+
        L"; not inspected "+std::to_wstring(s.missing)+
        L", stale "+std::to_wstring(s.stale)+
        L", blocked "+std::to_wstring(s.blocked)+
        L", RAW-only "+std::to_wstring(s.raw_only)+
        L", original kept "+std::to_wstring(s.kept)+
        L". No final FILE links qualified.";
}

std::wstring workflow_audit_summary(
    const PreviewState& state,
    std::optional<djmeta::WorkflowOnlineSourceCounts>* verified_online=nullptr) {
    if(verified_online) verified_online->reset();
    verify_rules_snapshot(state.captured_rules);
    const auto audit=djmeta::audit_workflow_preview(
        state.table,state.analyses,state.review_decisions);
    std::wstring report=L"Captured tracks: "+std::to_wstring(audit.tracks)+
        L"\nLocal normalizer: "+std::to_wstring(audit.normalizer_changed_tracks)+
        L" changed / "+std::to_wstring(audit.normalizer_unchanged_tracks)+
        L" with no staged changes (may still be pending)\nPending tracks: "+
        std::to_wstring(audit.tracks_with_pending)+
        L"; changed and pending: "+
        std::to_wstring(audit.tracks_with_changed_and_pending)+
        L"\nNo proposals: "+std::to_wstring(audit.tracks_without_proposals)+
        L"; resolved no-op/rejected-only: "+
        std::to_wstring(audit.tracks_with_only_noops_or_rejections)+
        L"\nExplicit changed values: "+
        std::to_wstring(audit.explicit_changed_values)+
        L"\nExplicit no-ops: "+std::to_wstring(audit.explicit_noop_values)+
        L"; rejected: "+std::to_wstring(audit.rejected_values)+
        L"\nStill pending: SAFE "+std::to_wstring(audit.pending_safe_values)+
        L", semantic "+std::to_wstring(audit.pending_semantic_values)+
        L" in "+std::to_wstring(audit.tracks_with_pending_semantic)+
        L" tracks\nRaw target collision rows: "+
        std::to_wstring(audit.raw_collision_tracks)+
        L"\nUnqualified FileOps rows: "+
        std::to_wstring(audit.file_plan_blocked_tracks);
    const auto file_actions=current_file_action_audit(state,audit);
    report+=L"\nFile action intentions (not executable): Move "+
        std::to_wstring(file_actions.moves)+L", Copy "+
        std::to_wstring(file_actions.copies)+L", Rename "+
        std::to_wstring(file_actions.renames)+
        L", No file action "+std::to_wstring(file_actions.skips)+
        L"\nFile action ledger: "+std::to_wstring(file_actions.blocked)+
        L" blocked, "+std::to_wstring(file_actions.raw_only)+
        L" RAW ONLY candidates (not final FileOps paths).";
    std::size_t shown=0;
    for(std::size_t i=0;i<file_actions.rows.size() && shown<5;++i) {
        const auto& row=file_actions.rows[i];
        if(!row.blocked) continue;
        if(shown++==0) report+=L"\nFirst blocked file actions:";
        report+=L"\n#"+std::to_wstring(i+1)+L" "+
            from_utf8(djmeta::file_action_label(row.action))+
            L": "+from_utf8(row.reason);
    }
    if(!state.online_included_fields.empty()) {
        // Audit the selected physical source independently of which tab is
        // visible. A summary of online-only edits is NOT the postimage when
        // the normalizer already has accepted decisions for this source.
        const auto track=state.online_edition_source;
        if(!state.online_field_snapshot || track>=state.entries.size() ||
           track>=state.analyses.size() ||
           track>=state.review_decisions.size())
            throw std::invalid_argument(
                "Batch audit: selected online source evidence is missing.");
        const auto& entry=state.entries[track];
        verify_snapshot(entry);
        if(!entry.input.physical_source_qualified ||
           entry.handle->get_subsong_index()!=0 ||
           is_external_cue_locator(entry.input.source_path))
            throw std::invalid_argument(
                "Batch audit: selected online source is not a physical audio file.");
        const auto latest=probe_host_file_readonly(entry.input.source_path);
        if(latest.state!=HostFileState::ExistingFile ||
           latest.host_physical_key!=entry.observed_physical_key ||
           latest.source_guard!=entry.observed_source_guard)
            throw std::runtime_error(
                "Batch audit: selected online physical source changed; refresh.");
        const auto current=metadata_from_file_info(
            entry.handle->get_info_ref()->info());
        djmeta::UnifiedReviewBatchReprobe unified;
        unified.normalizer.live.captured={
            true,false,entry.observed_physical_key,
            entry.observed_source_guard,current};
        unified.normalizer.live.fresh_tags=current;
        unified.normalizer.live.live_physical_key=latest.host_physical_key;
        unified.normalizer.live.live_source_guard=latest.source_guard;
        unified.normalizer.captured_analysis=state.analyses[track];
        unified.normalizer.active_ruleset_revision=
            state.analyses[track].ruleset_revision;
        unified.normalizer.decisions=state.review_decisions[track];
        unified.online_capture=*state.online_field_snapshot;
        unified.selected_online_indices=state.online_included_fields;
        unified.explicit_date_targets=state.online_date_targets;
        const auto combined=djmeta::preview_unified_review_physical_tags(unified);
        if(!combined.physical.eligible)
            throw std::invalid_argument(
                "Batch audit: local + Discogs field selection unqualified: "+
                combined.rejection);
        if(verified_online)
            *verified_online=djmeta::WorkflowOnlineSourceCounts{
                track,combined.physical.explicitly_changed,
                combined.normalizer_changed_fields,
                combined.online_changed_fields,
                combined.mutually_agreed_fields,
                state.online_included_fields.size(),latest.source_guard};
        report+=L"\nVerified Discogs source #"+std::to_wstring(track+1)+
            L": explicit local field differences "+
            std::to_wstring(combined.normalizer_changed_fields)+
            L", selected online field differences "+
            std::to_wstring(combined.online_changed_fields)+
            L", mutually identical "+
            std::to_wstring(combined.mutually_agreed_fields)+
            L", combined physical field differences "+
            std::to_wstring(combined.physical.explicitly_changed)+
            L". This ONE source has a verified detached postimage; "
            L"the full FileOps/CUE batch remains unqualified."+
            std::wstring(state.online_raw_route_valid
                ? L" Advisory raw filename available."
                : L" Raw filename must be recalculated or is unqualified.");
    } else if(state.online_field_preview) {
        report+=L"\nDiscogs field selection: none. "
                L"Only explicitly accepted local normalization is staged.";
    }
    report+=L"\n"+cue_inspection_summary(state);
    if(state.cue_value_preview)
        report+=L"\nCUE: independent read-only postimage; not a batch write.";
    std::size_t listed=0;
    for(std::size_t i=0;i<state.table.rows.size() && listed<8;++i) {
        const auto& row=state.table.rows[i];
        if(row.issues.empty()) continue;
        const std::string* reason=&row.issues.front();
        for(const auto& issue:row.issues)
            if(issue!="CUE_DEPENDENCIES_UNCHECKED" &&
               issue!="FILESYSTEM_TARGET_UNCHECKED") {
                reason=&issue;break;
            }
        if(listed++==0) report+=L"\n\nFirst unqualified rows (source order):";
        report+=L"\n#"+std::to_wstring(i+1)+L": "+
            from_utf8(issue_status_text(*reason));
    }
    report+=L"\n\nREAD-ONLY AUDIT. SAFE is not implicit approval. "
            L"Raw paths are not sanitized foobar FileOps destinations. "
            L"Source/target/CUE preflight and one final batch confirmation "
            L"are still required. No tags, CUE, audio or files were changed.";
    return report;
}

void show_workflow_audit(HWND dialog,const PreviewState& state) {
    const auto report=workflow_audit_summary(state);
    MessageBoxW(dialog,report.c_str(),L"Music Metadata Studio - Batch audit",
                MB_OK|MB_ICONINFORMATION);
}

std::string status_text(const djmeta::BatchPreviewRow& row) {
    if (row.issues.empty()) return "Qualified inputs (not approved)";
    // Global preview limitations belong in the footer and full tooltip, not
    // repeatedly in every narrow status cell. Preserve the first row-specific
    // warning even when only global issues precede or follow it.
    for (const auto& issue : row.issues) {
        if (issue == "CUE_DEPENDENCIES_UNCHECKED" ||
            issue == "FILESYSTEM_TARGET_UNCHECKED")
            continue;
        return issue_status_text(issue);
    }
    if (row.raw_target_presence == djmeta::RawTargetPresence::Missing)
        return "Raw candidate absent";
    return "";
}

// Strip foobar's file:// locator only for presentation, never for identity.
std::string display_file_path(const std::string& path) {
    if (path.compare(0, 8, "file:///") == 0)
        return path.substr(8);
    if (path.compare(0, 7, "file://") == 0)
        return path.substr(7);
    return path;
}

std::wstring cell_text(PreviewState& state, std::size_t row, int column) {
    if (row >= state.entries.size() || row >= state.table.rows.size()) return {};
    const auto& entry = state.entries[row];
    const auto& summary = state.table.rows[row];
    switch (column) {
    case 0: return from_utf8(display_file_path(entry.input.source_path));
    case 1: return from_utf8(entry.input.profile);
    case 2: {
        if(native_recheck_preserved_online_selection(
               state.online_field_snapshot.has_value(),
               state.online_edition_source,row,
               state.online_included_fields.size()) &&
           !state.online_raw_route_valid)
            return L"(joint route needs recalculation)";
        std::string display = summary.raw_destination;
        for (char& ch : display) if (ch == '/') ch = '\\';
        return from_utf8(display);
    }
    case 3: {
        if(state.location_queue==djmeta::BatchLocationQueue::CueAll ||
           state.location_queue==djmeta::BatchLocationQueue::CueAttention ||
           state.location_queue==djmeta::BatchLocationQueue::CueRawOnly ||
           state.location_queue==djmeta::BatchLocationQueue::CueOriginalKept) {
            // CUE filter rows remain physical carrier identities; disclose
            // the matching independent session stage instead of a generic
            // raw target state. The final FILE postimage remains unqualified.
            return L"CUE: "+from_utf8(djmeta::cue_queue_label(
                state.cue_inspections.stage(row)))+
                L" (final links unchecked)";
        }
        const auto intent=djmeta::inspect_file_action_intent(
            entry.file_action,entry.input,summary);
        if(intent.file_skip) return L"No file action (staged)";
        const std::wstring prefix=
            from_utf8(djmeta::file_action_label(entry.file_action))+L": ";
        if(intent.blocked) return prefix+from_utf8(intent.reason);
        if(native_recheck_preserved_online_selection(
               state.online_field_snapshot.has_value(),
               state.online_edition_source,row,
               state.online_included_fields.size())) {
            if(!state.online_raw_route_valid)
                return L"Joint raw route: unqualified";
            const auto issue=status_text(summary);
            if(!issue.empty() && issue!="Qualified inputs (not approved)")
                return prefix+from_utf8(issue);
            return prefix+(state.online_raw_target_observation &&
                summary.raw_target_presence!=djmeta::RawTargetPresence::NotInspected
                ? L"Joint raw inspected; not final"
                : L"Joint raw preview; not probed");
        }
        return prefix+from_utf8(status_text(summary));
    }
    default: return {};
    }
}

std::string readable_track_name(const std::string& path) {
    const std::string displayed = display_file_path(path);
    const auto split = displayed.find_last_of("/\\");
    return split == std::string::npos ? displayed : displayed.substr(split+1);
}

// No MusicBrainz request may contain a local path, file hash or audio bytes.
// These values come solely from explicitly selected metadata or the text the
// user typed in the short native search field. Duplicates are ambiguous.
std::string unique_metadata_value(const djmeta::MetadataDocument& doc,
                                  std::string_view wanted) {
    std::string result;
    std::size_t matches=0;
    for (const auto& field:doc.fields)
        if (djmeta::online::ascii_upper_field(field.name)==wanted) {
            matches += field.values.size();
            if (field.values.size()==1) result=field.values[0];
        }
    if (matches>1)
        throw std::invalid_argument("Ambiguous local title or artist field.");
    return result;
}

// Search text is suggested from the SELECTED track's existing metadata.
 // Never start a network request during selection, tab switching or refresh.
void prefill_musicbrainz_query(PreviewState& state, bool force = false) {
    if (!state.dialog || state.selected_track_index >= state.entries.size()) return;
    if(state.beatport_offline_selected) {
        // A title is NOT a Beatport URL. A newly selected physical source
        // must not inherit the previous track's still-valid public URL.
        // Repaint/selection of the same track preserves a manually typed URL.
        if(state.auto_search_track!=state.selected_track_index)
            SetDlgItemTextW(state.dialog,IDC_METADATA_MB_QUERY,L"");
        state.auto_search_track=state.selected_track_index;
        state.auto_search_text.clear();
        return; // no network, no auto-generated or guessed Beatport URL
    }
    const auto& entry = state.entries[state.selected_track_index];
    std::string title;
    // A CUE needs an explicitly inspected album inventory before online
    // search; its raw file handle is not a usable track TITLE source.
    if (!is_external_cue_locator(entry.input.source_path)) {
        try {
            const auto info = entry.handle->get_info_ref();
            const auto original = metadata_from_file_info(info->info());
            title = unique_metadata_value(original, "TITLE");
            if (title.empty()) title = unique_metadata_value(original, "ALBUM");
        } catch (const std::exception&) {
            // Ambiguous/malformed metadata has no automatic query. Do not
            // leave the previous track's automatic query in the edit box.
        }
    }
    const auto suggested = from_utf8(title);
    const auto current = read_control(state.dialog, IDC_METADATA_MB_QUERY);
    if (!djmeta::should_replace_auto_online_query(
            current, state.auto_search_text, force))
        return; // A deliberate manual query survives ordinary repaint.
    state.auto_search_text = suggested;
    state.auto_search_track = state.selected_track_index;
    if (current != suggested)
        SetDlgItemTextW(state.dialog,IDC_METADATA_MB_QUERY,suggested.c_str());
}

std::string unique_cue_album_value(const djmeta::CueMetadataInventory& cue,
                                   std::string_view name) {
    std::string value;
    std::size_t count=0;
    for (const auto& field:cue.globals)
        if (field.name==name) {
            value=field.value;
            ++count;
        }
    if (count>1)
        throw std::invalid_argument("Ambiguous CUE album title or artist.");
    return value;
}

// One MusicBrainz candidate == one visible master row. Field-level
// differences are retained only in the secondary, selected-candidate pane.
struct MusicBrainzBrowseRows {
    std::vector<djmeta::online::FieldReviewRow> summary;
    std::vector<std::vector<djmeta::online::FieldReviewRow>> detail;
};
MusicBrainzBrowseRows musicbrainz_search_rows(
    const djmeta::online::musicbrainz::SearchResult& found,
    const std::string& local_title, const std::string& local_artist,
    const djmeta::MetadataDocument& original_tags) {
    using namespace djmeta::online;
    MusicBrainzBrowseRows result;
    for (const auto& hit : found.candidates) {
        FieldReviewRow summary;
        summary.field=hit.title;
        summary.original_values={hit.artist};
        if (!hit.release_date.empty()) summary.candidate.values={hit.release_date};
        summary.candidate.field=musicbrainz::recording_identity_label(
            local_title,local_artist,hit.title,hit.artist);
        if(hit.repeated_identity_conflict)
            summary.candidate.field+=" [conflicting search evidence]";
        summary.candidate.provider="musicbrainz";
        summary.candidate.source_id=hit.mbid;
        summary.candidate.scope=hit.kind==musicbrainz::SearchKind::Release
            ? EvidenceScope::Edition:EvidenceScope::Recording;
        summary.reason="musicbrainz_live_candidate_rank_"+std::to_string(hit.search_score);
        summary.state=FieldReviewState::NeedsReview;
        result.summary.push_back(std::move(summary));
        std::vector<FieldReviewRow> detail;
        const auto add=[&](const std::string& field,
                           const std::string& original,
                           const std::string& proposed) {
            if (proposed.empty()) return;
            FieldReviewRow row;
            row.field=field;
            if (!original.empty()) row.original_values={original};
            row.candidate.field=field;
            row.candidate.provider="musicbrainz";
            row.candidate.source_id=hit.mbid;
            row.candidate.scope=hit.kind==musicbrainz::SearchKind::Release
                ? EvidenceScope::Edition:EvidenceScope::Recording;
            row.candidate.values={proposed};
            row.state=(!original.empty() &&
                       comparable(original)==comparable(proposed))
                ? FieldReviewState::Unchanged : FieldReviewState::NeedsReview;
            row.reason="musicbrainz_live_candidate_detail";
            detail.push_back(std::move(row));
        };
        // Search title/artist describe DIFFERENT physical tag scopes:
        // recording TITLE + ARTIST, versus edition ALBUM + ALBUM ARTIST.
        // Compare with the complete local snapshot, never with the track
        // title as a surrogate album and never flatten multivalue originals.
        const auto mapped=review_musicbrainz_search_hit_fields(
            original_tags,hit.mbid,
            hit.kind==musicbrainz::SearchKind::Release,
            hit.title,hit.artist);
        detail.insert(detail.end(),mapped.begin(),mapped.end());
        if(hit.kind==musicbrainz::SearchKind::Recording) {
            if (!hit.recording_isrcs.empty()) {
                FieldReviewRow identifiers;
                identifiers.field="RECORDING ISRC (read-only)";
                identifiers.candidate={
                    identifiers.field,hit.recording_isrcs,"musicbrainz",
                    hit.mbid,EvidenceScope::Recording,DateMeaning::NotDate};
                identifiers.state=FieldReviewState::NeedsReview;
                identifiers.reason="musicbrainz_live_search_isrc_identity_only";
                detail.push_back(std::move(identifiers));
            }
            add("IDENTITY (read-only)", "",
                musicbrainz::recording_identity_label(
                    local_title,local_artist,hit.title,hit.artist));
            add("MUSICBRAINZ RANK (not certainty)", "",
                std::to_string(hit.search_score));
        }
        if (hit.kind==musicbrainz::SearchKind::Release)
            if (auto date=musicbrainz_search_edition_date(
                    hit.mbid,hit.release_date))
                detail.push_back(std::move(*date));
        add("DISCOVERY STRATEGY (read-only)","",hit.discovery_strategy);
        if(hit.repeated_identity_conflict)
            add("REPEATED IDENTITY CONFLICT (read-only)","",
                "Same exact MusicBrainz ID had inconsistent search snippets. "
                "Inspect the official edition before considering any field.");
        // Display the full original physical inventory independently from
        // search-hit fields. Keep all values and source identities separate;
        // above native capacity show an explicit overflow count, not silence.
        append_local_physical_inventory(detail,original_tags);
        result.detail.push_back(std::move(detail));
    }
    return result;
}

// Discogs search snippets combine titles and artists; they are not album
// tag proposals. Only explicitly loaded release details may supply fields.
MusicBrainzBrowseRows discogs_search_rows(
    const djmeta::online::discogs::SearchResults& found,
    const djmeta::MetadataDocument& originals) {
    using namespace djmeta::online;
    MusicBrainzBrowseRows out;
    for(const auto& hit:found.hits) {
        const bool master=hit.kind==discogs::EntityKind::Master;
        const std::string id=(master?"master:":"release:")+
            std::to_string(hit.id);
        FieldReviewRow summary;
        summary.field=hit.title;
        if(!hit.artist.empty()) summary.original_values={hit.artist};
        if(!hit.year.empty()) summary.candidate.values={hit.year};
        summary.candidate.field="SEARCH RESULT (not a tag proposal)";
        summary.candidate.provider="discogs";
        summary.candidate.source_id=id;
        summary.candidate.scope=EvidenceScope::Edition;
        summary.reason=master?"discogs_live_master_not_exact_edition":
            "discogs_live_candidate_search_relevance_only";
        summary.state=master?FieldReviewState::Blocked:FieldReviewState::NeedsReview;
        out.summary.push_back(std::move(summary));
        std::vector<FieldReviewRow> details;
        const auto add=[&](std::string field,std::vector<std::string> values) {
            if(values.empty()) return;
            FieldReviewRow row;
            row.field=field;
            row.candidate={field,std::move(values),"discogs",id,
                EvidenceScope::Edition,DateMeaning::NotDate};
            row.reason="discogs_live_search_unverified_field";
            row.state=master?FieldReviewState::Blocked:FieldReviewState::NeedsReview;
            details.push_back(std::move(row));
        };
        add("SEARCH TITLE (not verified album)",{hit.title});
        if(!hit.artist.empty()) add("SEARCH ARTIST",{hit.artist});
        if(!hit.year.empty()) add("YEAR",{hit.year});
        if(!hit.country.empty()) add("COUNTRY",{hit.country});
        add("GENRE",hit.genres);
        add("STYLE",hit.styles);
        if(!hit.label.empty()) add("EDITION LABEL",{hit.label});
        if(!hit.catalog_number.empty())
            add("EDITION CATALOG NUMBER",{hit.catalog_number});
        if(!hit.format.empty()) add("EDITION FORMAT",{hit.format});
        add("CANONICAL URL (read-only)",{hit.canonical_url});
        if(master) add("EDITION REQUIRED",{"Select a concrete Discogs release"});
        // Search previews are evidence, not confirmed edition fields. Show
        // bounded original physical inventory with explicit overflow notice.
        append_local_physical_inventory(details,originals);
        out.detail.push_back(std::move(details));
    }
    return out;
}

// Only a selected, verified MusicBrainz release MBID is allowed to feed this
// advisory tracklist comparison. The Hungarian assignment never writes CUE.
std::vector<djmeta::online::FieldReviewRow> musicbrainz_cue_release_rows(
    const djmeta::CueMetadataInventory& cue,
    const djmeta::online::ReleaseEdition& edition) {
    using namespace djmeta::online;
    if (cue.status != djmeta::CueSyntaxStatus::Parsed)
        throw std::invalid_argument("Unqualified CUE cannot be matched to a release.");
    std::vector<FieldReviewRow> output;
    const auto review_to_row=[&](const CueCandidateFieldReview& input,
                                 const std::string& prefix) {
        FieldReviewRow row;
        row.field=prefix+(input.cue_field.empty()
            ? input.candidate.field : input.cue_field);
        row.candidate=input.candidate;
        row.original_values=input.original_values;
        row.state=input.state;
        row.reason="musicbrainz_live_release_"+input.reason+
            (input.original_inherited?"_inherited":"");
        return row;
    };
    for (const auto& field:edition.fields) {
        const auto reviewed=review_cue_candidate_fields(cue,std::nullopt,{field});
        for(const auto& item:reviewed)
            output.push_back(review_to_row(item,"Album / "));
    }
    const auto local=prepare_cue_release_lookup(cue);
    ReleaseAlignment alignment;
    if (local.eligible)
        alignment=align_release(local.tracks,edition);
    const bool media_code_conflict=cue_media_barcode_conflict(cue,edition);
    const bool whole_edition_qualified=
        qualified_musicbrainz_cue_album_alignment(cue,edition,alignment);
    if(!whole_edition_qualified)
        for(auto& row:output)
            if(row.state==FieldReviewState::NeedsReview){
                row.state=FieldReviewState::Blocked;
                row.reason=(media_code_conflict
                    ? "musicbrainz_cue_media_barcode_conflict_"
                    : "musicbrainz_cue_edition_full_alignment_required_")+
                           row.reason;
            }
    // Each remote track is displayed even if local identity is missing:
    // never invent a position-to-CUE assignment from the tracklist alone.
    for (std::size_t i=0;i<edition.tracks.size();++i) {
        if (output.size()>9998)
            throw std::invalid_argument("MusicBrainz preview row limit exceeded.");
        const auto& remote=edition.tracks[i];
        // A plausible alignment is insufficient if the occurrence fields
        // are absent, foreign or do not belong to this exact track MBID.
        const bool verified_occurrence=
            musicbrainz::valid_mbid(remote.source_track_id) &&
            qualified_cue_edition_track_provenance(edition,i) &&
            std::all_of(remote.fields.begin(),remote.fields.end(),
                [&](const FieldEvidence& evidence) {
                    return evidence.source_id==remote.source_track_id;
                });
        std::optional<std::size_t> local_ordinal;
        for (const auto& link:alignment.tracks) {
            if (link.edition_track_index==i &&
                link.decision==MatchDecision::Suggested &&
                link.conflicts.empty() && verified_occurrence) {
                local_ordinal=link.source_index;
                break;
            }
        }
        for (const std::string name:{"TITLE","ARTIST"}) {
            const auto evidence_it=std::find_if(
                remote.fields.begin(),remote.fields.end(),
                [&](const FieldEvidence& evidence) {
                    return evidence.field==name;
                });
            const bool verified_value=verified_occurrence &&
                std::count_if(remote.fields.begin(),remote.fields.end(),
                    [&](const FieldEvidence& evidence) {
                        return evidence.field==name;
                    })==1 && evidence_it!=remote.fields.end() &&
                evidence_it->values.size()==1 &&
                !evidence_it->values.front().empty();
            const std::string value=verified_value
                ? evidence_it->values.front()
                : (name=="TITLE" ? remote.recording.title
                                 : remote.recording.primary_artist);
            // This is the exact release's track occurrence, NOT a recording
            // MBID. Never impersonate a missing occurrence ID by substituting
            // the enclosing release MBID or recording-level provenance.
            FieldEvidence field{name,{value},"musicbrainz",
                remote.source_track_id,
                EvidenceScope::Edition,DateMeaning::NotDate};
            FieldReviewRow row;
            row.field="Disc "+std::to_string(remote.disc_number)+
                " / Track "+std::to_string(remote.track_number)+
                " / "+name;
            row.candidate=field;
            if (local_ordinal && verified_value) {
                const auto review=review_cue_candidate_fields(
                    cue,*local_ordinal,{field},true);
                row.original_values=review.front().original_values;
                row.state=review.front().state;
                row.reason="musicbrainz_live_release_track_match_review_"+
                           review.front().reason;
            } else {
                row.state=FieldReviewState::Blocked;
                row.reason=verified_occurrence && !verified_value
                    ? "musicbrainz_live_release_track_field_unverified"
                    : "musicbrainz_live_release_unmatched_no_safe_track_identity";
            }
            output.push_back(std::move(row));
        }
    }
    // The same exact-edition conflict gate protects the native display lane;
    // no changed album or track field may look reviewable against a different
    // disc barcode merely because the tracklist happens to align.
    if(media_code_conflict)
        for(auto& row:output)
            if(row.state==FieldReviewState::NeedsReview) {
                row.state=FieldReviewState::Blocked;
                row.reason="musicbrainz_cue_media_barcode_conflict_"+row.reason;
            }
    // The complete local CUE remains its own source lane after loading the
    // official edition. Search/provider fields never replace original
    // CATALOG, unknown REM occurrences or structural FILE references.
    const auto original=cue_original_inventory_rows(cue,"CUE Original / ");
    if(original.size()>10000 || output.size()>10000-original.size())
        throw std::invalid_argument(
            "MusicBrainz CUE comparison exceeds 10,000 display rows.");
    output.insert(output.end(),original.begin(),original.end());
    return output;
}

// The core matcher is the only authority that can attach a Discogs track
// credit to a local CUE ordinal. Ambiguity is displayed without a proposal.
std::vector<djmeta::online::FieldReviewRow> discogs_cue_release_rows(
    const djmeta::CueMetadataInventory& cue,
    const djmeta::online::ReleaseEdition& edition) {
    using namespace djmeta::online;
    const auto match=review_cue_release_edition(cue,edition);
    // Invalid CUE syntax or forged remote provenance still fails closed.
    // A parsed carrier with incomplete track identity may show only
    // BLOCKED album facts plus per-CUE-track diagnostics, never candidates.
    if(!match.eligible && match.album_fields.empty())
        throw std::invalid_argument("Discogs CUE edition alignment is unqualified.");
    std::vector<FieldReviewRow> rows;
    const auto append=[&](std::string prefix,const CueCandidateFieldReview& field) {
        FieldReviewRow row;
        row.field=std::move(prefix)+(field.cue_field.empty()
            ? field.candidate.field:field.cue_field);
        row.original_values=field.original_values;
        row.candidate=field.candidate;
        row.state=field.state;
        row.reason="discogs_live_release_"+field.reason;
        rows.push_back(std::move(row));
    };
    for(const auto& field:match.album_fields)
        append("Album / ",field);
    if(!match.eligible) {
        FieldReviewRow row;
        row.field="CUE Source / MATCH";
        row.candidate.field="MATCH_DIAGNOSTIC";
        row.candidate.provider="discogs";
        row.candidate.source_id=edition.edition_id;
        row.candidate.scope=EvidenceScope::Edition;
        row.state=FieldReviewState::Blocked;
        row.reason="discogs_live_release_cue_track_inventory_unqualified";
        row.candidate.values=match.reasons;
        if(row.candidate.values.empty())
            row.candidate.values={"No verified track identities; album facts only"};
        rows.push_back(std::move(row));
    }
    for(const auto& item:match.tracks) {
        if(rows.size()>2000)
            throw std::invalid_argument("Discogs CUE preview exceeds row limit.");
        const std::string prefix="CUE Track "+
            std::to_string(item.cue_ordinal+1)+" / ";
        if(item.fields.empty()) {
            FieldReviewRow row;
            row.field=prefix+"MATCH";
            row.candidate.field="MATCH";
            row.candidate.provider="discogs";
            row.candidate.source_id=edition.edition_id;
            row.state=FieldReviewState::Blocked;
            row.reason="discogs_live_release_unverified_track_assignment";
            row.candidate.values=item.reasons;
            if(row.candidate.values.empty())
                row.candidate.values={"No uniquely verified edition track"};
            rows.push_back(std::move(row));
            // Native read-only field details: uncertain assignments are
            // displayed exclusively as blocked evidence with original values.
            for(const auto& field:item.unverified_fields)
                append(prefix,field);
        } else for(const auto& field:item.fields)
            append(prefix,field);
    }
    // The complete actual CUE carrier belongs to a SEPARATE local lane.
    // Missing provider fields must not hide original tags, unknown REM,
    // duplicate occurrences or structural FILE references.
    const auto original=cue_original_inventory_rows(cue,"CUE Original / ");
    if (original.size() > 10000 || rows.size() > 10000 - original.size())
        throw std::invalid_argument("Discogs CUE comparison exceeds row limit.");
    rows.insert(rows.end(),original.begin(),original.end());
    return rows;
}

MusicBrainzBrowseRows group_musicbrainz_release_rows(
    const std::vector<djmeta::online::FieldReviewRow>& flat) {
    using namespace djmeta::online;
    MusicBrainzBrowseRows out;
    for (const auto& row:flat) {
        // Album and each Disc/Track are one summary item, irrespective
        // of how many individual fields that item exposes. The existing
        // separate detail rows remain intact after user selection.
        const auto at=row.field.rfind(" / ");
        const std::string name=at==std::string::npos
            ? row.field:row.field.substr(0,at);
        if (out.summary.empty() || out.summary.back().field!=name) {
            FieldReviewRow summary;
            summary.field=name;
            summary.candidate.provider=row.candidate.provider;
            summary.candidate.source_id=row.candidate.source_id;
            const bool physical=is_local_physical_original(row);
            const bool local_cue=is_local_cue_inventory(row);
            summary.candidate.field=physical ? "LOCAL PHYSICAL SOURCE"
                : local_cue ? "LOCAL CUE SOURCE" : "RELEASE TRACK";
            summary.state=row.state;
            summary.reason=physical ? "local_physical_group" :
                local_cue ? "local_cue_group" :
                row.reason.starts_with("album_batch_scope_")
                    ? "local_selection_scope_group" :
                row.field.starts_with("Pasted ")
                    ? "manual_pasted_candidate_group"
                    : row.candidate.provider=="discogs"
                    ? "discogs_live_release_group"
                    : row.candidate.provider=="beatport" &&
                      row.reason.starts_with("beatport_public_webpage_")
                    ? "beatport_public_webpage_group"
                    : "musicbrainz_live_release_group";
            out.summary.push_back(std::move(summary));
            out.detail.emplace_back();
        }
        auto& summary=out.summary.back();
        if (row.field.ends_with(" / TITLE") ||
            row.field.ends_with(" / ALBUM") ||
            row.field=="Discogs Master / DATE" ||
            (summary.candidate.values.empty() &&
             row.field.ends_with(" / PERFORMER"))) {
            summary.original_values=row.original_values;
            summary.candidate.values=row.candidate.values;
        }
        if (row.state==FieldReviewState::Blocked)
            summary.state=FieldReviewState::Blocked;
        else if (row.state==FieldReviewState::NeedsReview &&
                 summary.state!=FieldReviewState::Blocked)
            summary.state=FieldReviewState::NeedsReview;
        out.detail.back().push_back(row);
    }
    return out;
}

// A pasted structured candidate is a comparison group, not an authenticated
// online search hit. Keep this distinction in browser headers and hints.
inline bool pasted_comparison_group_view(const PreviewState& state) {
    return state.musicbrainz_live_view && !state.candidate_rows.empty() &&
           state.candidate_rows.front().reason=="manual_pasted_candidate_group";
}

std::wstring track_master_cell(PreviewState& state, std::size_t row, int col) {
    if (row >= state.track_view_order.size()) return {};
    const auto index = state.track_view_order[row];
    if (index >= state.track_summaries.size()) return {};
    const auto& track = state.track_summaries[index];
    if (state.show_candidate && col != 0) {
        if (state.candidate_source_index != index) return L"—";
        const auto& rows = state.candidate_rows;
        if (col == 1) return std::to_wstring(rows.size());
        if (state.musicbrainz_live_view) {
            if (col == 3) return L""; // no meaningless third online summary
            return std::to_wstring(static_cast<std::size_t>(std::count_if(
                rows.begin(),rows.end(),[](const auto& item) {
                    return item.state==djmeta::online::FieldReviewState::Blocked;
                })));
        }
        if (state.cue_inspection_mode) return L"0";
        const auto kind = col == 2 ? djmeta::online::FieldReviewState::NeedsReview
                                   : djmeta::online::FieldReviewState::Blocked;
        return std::to_wstring(static_cast<std::size_t>(std::count_if(
            rows.begin(), rows.end(), [kind](const auto& row) {
                return row.state == kind;
            })));
    }
    switch(col) {
        case 0: return track.source_index < state.source_labels.size()
            ? from_utf8(readable_track_name(state.source_labels[track.source_index]))
            : std::wstring{};
        case 1: return std::to_wstring(track.music_changes);
        case 2: return std::to_wstring(track.extended_changes);
        case 3: return std::to_wstring(track.review_required);
        default: return {};
    }
}

const djmeta::ReviewDecision* decision_for_row(
    const PreviewState& state, const djmeta::MetadataDiffRow& item) {
    if (item.source_index >= state.review_decisions.size()) return nullptr;
    const auto& per_track = state.review_decisions[item.source_index];
    return item.proposal_index < per_track.size()
        ? &per_track[item.proposal_index] : nullptr;
}

std::wstring review_decision_caption(
    const PreviewState& state, const djmeta::MetadataDiffRow& item) {
    const auto* decision = decision_for_row(state, item);
    if (!decision) return L"Unavailable";
    switch (decision->action) {
        case djmeta::ReviewAction::Pending:
            return item.safety == djmeta::SafetyClass::Safe ? L"SAFE preview" : L"Pending";
        case djmeta::ReviewAction::Accept: return L"Accepted";
        case djmeta::ReviewAction::Reject: return L"Original";
        case djmeta::ReviewAction::ManualValue: return L"Manual";
    }
    return L"Unavailable";
}

void update_pending_review_summaries(
    PreviewState& state, const std::set<std::size_t>& affected_tracks) {
    if(state.track_summaries.size()!=state.entries.size() ||
       state.analyses.size()!=state.entries.size() ||
       state.review_decisions.size()!=state.entries.size())
        throw std::invalid_argument("Track review summary snapshot is stale.");
    for(const auto track:affected_tracks) {
        if(track>=state.entries.size())
            throw std::invalid_argument("Changed track is outside captured review.");
        const auto progress=djmeta::summarize_track_review_progress(
            state.analyses[track],state.review_decisions[track]);
        auto& summary=state.track_summaries[track];
        summary.review_required=progress.pending_review;
        summary.pending_proposals=progress.pending_proposals;
        summary.staged_changes=progress.staged_changes;
    }
}

void refresh_review_summaries(PreviewState& state) {
    state.track_summaries = djmeta::summarize_track_changes(
        state.entries.size(), state.metadata_rows);
    if(state.analyses.size()!=state.entries.size() ||
       state.review_decisions.size()!=state.entries.size())
        throw std::invalid_argument("Track review summary snapshot is stale.");
    for(std::size_t track=0;track<state.entries.size();++track) {
        const auto progress=djmeta::summarize_track_review_progress(
            state.analyses[track],state.review_decisions[track]);
        auto& summary=state.track_summaries[track];
        summary.review_required=progress.pending_review;
        summary.pending_proposals=progress.pending_proposals;
        summary.staged_changes=progress.staged_changes;
    }
}

std::wstring show_field_values(const std::vector<std::string>& values) {
    if (values.empty()) return L"(missing)";
    std::wstring result;
    for (const auto& value : values) {
        if (!result.empty()) result += L" | ";
        try {
            result += from_utf8(value);
        } catch (const std::exception&) {
            result += L"(invalid UTF-8 in existing tag)";
        }
    }
    return result;
}

std::wstring candidate_cell_text(const djmeta::online::FieldReviewRow& item,
                                 int column) {
    // Keep every native visible source/status token in one testable contract.
    // The virtual SysListView32 requests these cells via LVN_GETDISPINFOW.
    switch (column) {
    case 0: return from_utf8(item.field);
    case 1: return show_field_values(item.original_values);
    case 2: return djmeta::online::native_candidate_cue_inventory(item)
        ? L"" : show_field_values(item.candidate.values);
    case 3: return from_utf8(djmeta::online::native_candidate_source_label(item));
    case 4: return from_utf8(djmeta::online::native_candidate_status_label(item));
    default: return {};
    }
}

const wchar_t* local_review_group_name(int group) noexcept {
    switch(group) {
    case 0: return L"Needs review";
    case 1: return L"Confident proposals";
    default: return L"Safe proposals";
    }
}

std::wstring metadata_cell_text(PreviewState& state,
                                std::size_t row, int column) {
    if (state.show_candidate) {
        if (state.candidate_source_index != state.selected_track_index ||
            row >= state.candidate_view_order.size()) return {};
        const auto candidate_index = state.candidate_view_order[row];
        if (candidate_index >= state.candidate_rows.size()) return {};
        return candidate_cell_text(state.candidate_rows[candidate_index], column);
    }
    if (state.existing_tags_view) {
        if (row >= state.existing_tag_rows.size()) return {};
        if (column==0) return state.existing_tag_rows[row].first;
        if (column==1) return state.existing_tag_rows[row].second;
        if (column==3) return L"Original file";
        if (column==4) return L"Read-only";
        return {};
    }
    if (row >= state.metadata_view_order.size() ||
        row >= state.metadata_view_group.size()) return {};
    const int group=state.metadata_view_group[row];
    if(group>=0) {
        if(group>=3) return {};
        if(column==0) return std::wstring(state.review_group_expanded[group]
            ? L"\u25bc " : L"\u25b6 ")+local_review_group_name(group);
        if(column==1) return std::to_wstring(state.review_group_counts[group])+
            L" difference(s)";
        if(column==4) return L"Double-click / Enter to expand";
        return {};
    }
    const auto index = state.metadata_view_order[row];
    if (index >= state.focused_metadata_rows.size()) return {};
    const auto& item = state.focused_metadata_rows[index];
    switch (column) {
        case 0: return from_utf8(item.field);
        case 1: return preview_whitespace_text(from_utf8(item.original),
                                               state.show_whitespace);
        case 2: {
            const auto* decision = decision_for_row(state, item);
            return preview_whitespace_text(from_utf8(
                djmeta::review_visible_value(item.original, item.proposed,
                                             decision)), state.show_whitespace);
        }
        case 3: return from_utf8(djmeta::to_string(item.safety));
        case 4: return review_decision_caption(state, item);
        default: return {};
    }
}

void layout_musicbrainz_detail_pane(PreviewState& state) {
    if (!state.dialog || !state.metadata_list ||
        !state.musicbrainz_details || !state.musicbrainz_details_label ||
        state.resize_controls.empty()) return;
    const NativePreviewResizeChild *left=nullptr, *right=nullptr;
    for (const auto& child:state.resize_controls) {
        if (child.window==state.metadata_track_list) left=&child;
        if (child.window==state.metadata_list) right=&child;
    }
    if (!left || !right) return;
    RECT client{};
    GetClientRect(state.dialog,&client);
    const auto split=review_split_geometry(left->original,right->original,
        client.right-state.initial_client_width,
        client.bottom-state.initial_client_height,
        MulDiv(state.review_split_offset_96,state.active_dpi,96),
        MulDiv(160,state.active_dpi,96),
        MulDiv(180,state.active_dpi,96));
    const auto frame=split.detail;
    const int width=frame.right-frame.left;
    const int height=frame.bottom-frame.top;
    const bool active=state.show_candidate && state.musicbrainz_live_view &&
        state.candidate_source_index==state.selected_track_index;
    // Commit any user-adjusted widths even when this track switch hides the
    // online details table. Never defer capture until a hidden HWND is shown.
    if (state.online_detail_columns_applied)
        capture_review_grid_controls(state.musicbrainz_details,
            state.online_detail_grid,state.active_dpi);
    if (!active) {
        SetWindowPos(state.metadata_list,nullptr,frame.left,frame.top,
            (std::max)(8,width),(std::max)(8,height),
            SWP_NOZORDER|SWP_NOACTIVATE);
        ShowWindow(state.musicbrainz_details,SW_HIDE);
        ShowWindow(state.musicbrainz_details_label,SW_HIDE);
        return;
    }
    // Reserve a real caption line and vertical breathing room between the
    // browser and the nested table. The old +2/+17 geometry was overlapping
    // native ListView borders/fonts (visible at 100%-200% scaling).
    const auto regions=musicbrainz_pane_bounds(frame,state.active_dpi);
    const auto place=[&](HDWP& hdwp,HWND window,const RECT& bounds) {
        hdwp=DeferWindowPos(hdwp,window,nullptr,
            bounds.left,bounds.top,
            (std::max<int>)(8,bounds.right-bounds.left),
            (std::max<int>)(8,bounds.bottom-bounds.top),
            SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);
        return hdwp!=nullptr;
    };
    HDWP defer=BeginDeferWindowPos(3);
    if (!defer) return;
    if (!place(defer,state.metadata_list,regions.hits)) return;
    if (!place(defer,state.musicbrainz_details_label,regions.caption)) return;
    if (!place(defer,state.musicbrainz_details,regions.details)) return;
    EndDeferWindowPos(defer);
    ShowWindow(state.musicbrainz_details_label,SW_SHOW);
    ShowWindow(state.musicbrainz_details,SW_SHOW);
    if (!state.online_detail_columns_applied && !state.online_detail_has_saved) {
        // Auto-size once for a new profile, not on each selection/resize.
        const int widths[]={28,23,32,17};
        const int available=(std::max)(160,static_cast<int>(regions.details.right-regions.details.left)-8);
        for (int i=0;i<4;++i)
            ListView_SetColumnWidth(state.musicbrainz_details,i,
                (std::max)(48,available*widths[i]/100));
        capture_review_grid_controls(state.musicbrainz_details,
            state.online_detail_grid,state.active_dpi);
    } else {
        apply_review_grid_controls(state.musicbrainz_details,
            state.online_detail_grid,state.active_dpi);
    }
    state.online_detail_columns_applied=true;
}

// Separate the exact-edition physical lane from MusicBrainz CUE browsing,
// unverified clipboard results and Discogs search snippets. The field choices
// exist only in this in-memory HWND and have no writer access.
bool qualified_online_field_preview(const PreviewState& state) {
    if (!state.dialog || !state.show_candidate ||
        !state.discogs_provider_selected || !state.discogs_live_view ||
        !state.musicbrainz_live_view || !state.musicbrainz_release_loaded ||
        state.stale || state.refreshing ||
        state.discogs_request || state.musicbrainz_request ||
        !state.online_physical_edition || !state.online_field_snapshot ||
        state.selected_track_index!=state.candidate_source_index ||
        state.selected_track_index!=state.online_edition_source ||
        state.selected_track_index>=state.entries.size())
        return false;
    const auto& entry=state.entries[state.selected_track_index];
    return entry.input.physical_source_qualified &&
           entry.handle->get_subsong_index()==0 &&
           !is_external_cue_locator(entry.input.source_path) &&
           state.online_physical_edition->provider=="discogs" &&
           !state.online_physical_edition->edition_id.empty();
}

#if defined(MMS_ENABLE_PHYSICAL_WRITES)
// A CUE write is a separate external-file transaction, never an SDK tag
// write. It is exposed only after explicit in-grid selected-byte review.
bool qualified_cue_write_preview(const PreviewState& state) {
    if (!state.dialog || !state.show_candidate ||
        !state.musicbrainz_release_loaded || state.stale || state.refreshing ||
        state.discogs_request || state.musicbrainz_request ||
        state.beatport_public_request || state.physical_write_pending->load() ||
        state.selected_track_index != state.candidate_source_index ||
        state.selected_track_index != state.cue_postimage_source ||
        state.selected_track_index >= state.entries.size() ||
        state.cue_postimage_discogs != state.discogs_live_view ||
        !state.cue_postimage_snapshot ||
        !state.cue_postimage_snapshot->eligible ||
        !state.cue_postimage_snapshot->verified_edition ||
        state.cue_postimage_snapshot->carrier != djmeta::CueCarrierKind::ExternalText ||
        !state.cue_value_preview || !state.cue_value_preview->eligible ||
        !state.cue_value_preview->changed ||
        state.cue_value_preview_indices.empty())
        return false;
    return is_external_cue_locator(
        state.entries[state.selected_track_index].input.source_path);
}

std::wstring printable_cue_consent_value(const std::string& raw) {
    const auto wide = from_utf8(raw);
    std::wstring out;
    for (const auto c : wide) {
        if (c == L'\r') out += L"\\r";
        else if (c == L'\n') out += L"\\n";
        else if (c == L'\t') out += L"\\t";
        else if (c < 32 || c == 127)
            throw std::invalid_argument(
                "CUE confirmation contains undisplayable control characters.");
        else out += c;
        if (out.size() > 6000)
            throw std::invalid_argument(
                "CUE confirmation is too long. Select fewer fields.");
    }
    return out;
}

std::wstring exact_cue_write_confirmation(
    const djmeta::online::CuePostimageReviewSnapshot& source,
    const ExternalCueHostReview& review) {
    std::wstring out =
        L"Write ONLY the selected external CUE metadata values?\n\nFile: " +
        printable_cue_consent_value(review.source_path) +
        L"\n\nThe original CUE is atomically replaced only after fresh "
        L"source, FILE-reference and edition checks. No audio tags or "
        L"FILE paths are changed.\n";
    std::size_t changes = 0;
    for (const auto index : review.selected_indices) {
        if (index >= source.rows.size())
            throw std::invalid_argument("CUE selection expired.");
        const auto& row = source.rows[index].reviewed;
        if (row.state == djmeta::online::FieldReviewState::Unchanged)
            continue;
        if (row.state != djmeta::online::FieldReviewState::NeedsReview ||
            row.original_values.size() != 1 ||
            row.candidate.values.size() != 1)
            throw std::invalid_argument("CUE change is not exactly reviewable.");
        out += L"\n";
        if (row.target == djmeta::online::CueReviewTarget::TrackLocal)
            out += L"Track " + std::to_wstring(row.track_ordinal + 1) + L" / ";
        else
            out += L"Album / ";
        out += printable_cue_consent_value(row.cue_field) + L":\n  OLD: " +
            printable_cue_consent_value(row.original_values.front()) +
            L"\n  NEW: " +
            printable_cue_consent_value(row.candidate.values.front()) + L"\n";
        ++changes;
        if (out.size() > 6000)
            throw std::invalid_argument(
                "Exact CUE confirmation exceeds display limit. Select fewer fields.");
    }
    if (changes == 0 || !review.postimage.changed)
        throw std::invalid_argument("No changed CUE values require writing.");
    out += L"\nChanges: " + std::to_wstring(changes) +
        L". Unchanged fields cause NO write.\n\nProceed?";
    if (out.size() > 6000)
        throw std::invalid_argument(
            "Exact CUE confirmation exceeds display limit. Select fewer fields.");
    return out;
}
#endif

void update_online_field_preview_controls(PreviewState& state) {
    if(!state.dialog) return;
    const bool ready=qualified_online_field_preview(state);
    for(int id:{IDC_ONLINE_INCLUDE_FIELDS,IDC_ONLINE_IGNORE_FIELDS,
                IDC_ONLINE_RESET_FIELDS,IDC_ONLINE_DATE_TARGET_LABEL,
                IDC_ONLINE_DATE_TARGET}) {
        ShowWindow(GetDlgItem(state.dialog,id),ready?SW_SHOW:SW_HIDE);
        EnableWindow(GetDlgItem(state.dialog,id),ready?TRUE:FALSE);
    }
    // Only the isolated acceptance variant contains a real SDK write action.
    // Shipping preview builds keep the same control completely invisible.
#if defined(MMS_ENABLE_PHYSICAL_WRITES)
    const bool local_decisions_present=
        state.selected_track_index<state.review_decisions.size() &&
        std::any_of(state.review_decisions[state.selected_track_index].begin(),
                    state.review_decisions[state.selected_track_index].end(),
                    [](const djmeta::ReviewDecision& value) {
                        return value.action==djmeta::ReviewAction::Accept ||
                               value.action==djmeta::ReviewAction::ManualValue;
                    });
    // The restricted QA one-file writer is online-only. It must never apply
    // a partial postimage while a different local decision is accepted.
    const bool physical_writer_ready=ready && !local_decisions_present &&
        state.online_field_preview &&
        state.online_field_preview->explicitly_changed>0 &&
        !state.online_included_fields.empty() &&
        !state.physical_write_pending->load();
    const bool cue_writer_ready=qualified_cue_write_preview(state);
    const bool writer_ready=physical_writer_ready || cue_writer_ready;
    SetDlgItemTextW(state.dialog,IDC_ONLINE_WRITE_SELECTED,
        cue_writer_ready?L"Write CUE":L"Write tags");
    ShowWindow(GetDlgItem(state.dialog,IDC_ONLINE_WRITE_SELECTED),
               writer_ready?SW_SHOW:SW_HIDE);
    EnableWindow(GetDlgItem(state.dialog,IDC_ONLINE_WRITE_SELECTED),
                 writer_ready?TRUE:FALSE);
#else
    ShowWindow(GetDlgItem(state.dialog,IDC_ONLINE_WRITE_SELECTED),SW_HIDE);
    EnableWindow(GetDlgItem(state.dialog,IDC_ONLINE_WRITE_SELECTED),FALSE);
#endif
    // Inspect release and the dedicated field-preview reset share a physical
    // row only when the latter is NOT active; never overlap visible actions.
    ShowWindow(GetDlgItem(state.dialog,IDC_METADATA_MB_LOAD_RELEASE),
        state.show_candidate && !ready?SW_SHOW:SW_HIDE);
}

void refresh_musicbrainz_detail_rows(PreviewState& state) {
    if (!state.musicbrainz_details) return;
    ListView_DeleteAllItems(state.musicbrainz_details);
    if (!state.show_candidate || !state.musicbrainz_live_view ||
        state.candidate_source_index!=state.selected_track_index) return;
    const int selected=ListView_GetNextItem(state.metadata_list,-1,LVNI_SELECTED);
    if (selected<0 ||
        static_cast<std::size_t>(selected)>=state.candidate_view_order.size()) return;
    const std::size_t index=state.candidate_view_order[static_cast<std::size_t>(selected)];
    if (index>=state.musicbrainz_detail_groups.size()) return;
    const auto& detail=state.musicbrainz_detail_groups[index];
    // Render a bounded but substantial field inventory. Never silently hide
    // additional provider/local fields behind the former 50-row cutoff.
    constexpr std::size_t max_detail_rows=1024;
    for (std::size_t i=0;i<detail.size()&&i<max_detail_rows;++i) {
        const auto& row=detail[i];
        const auto field=from_utf8(row.field);
        const auto original=show_field_values(row.original_values);
        const bool local_original=djmeta::online::is_local_physical_original(row);
        const bool local_cue=djmeta::online::is_local_cue_inventory(row);
        const bool cue_previewed=state.cue_value_preview &&
            state.cue_value_preview->eligible &&
            state.cue_postimage_snapshot &&
            state.cue_postimage_source==state.selected_track_index &&
            state.cue_postimage_discogs==state.discogs_live_view &&
            djmeta::online::cue_detail_row_in_preview(
                *state.cue_postimage_snapshot,
                state.cue_value_preview_indices,row);
        bool included=false;
        std::wstring selected_target;
        if (qualified_online_field_preview(state)) {
            auto field_index=djmeta::online::physical_discogs_stageable_field_index(
                *state.online_physical_edition,
                state.online_field_snapshot->evidence,row,true);
            if(!field_index)
                field_index=djmeta::online::physical_discogs_master_field_index(
                    *state.online_physical_edition,
                    state.online_field_snapshot->evidence,row,true);
            included=field_index && std::find(
                state.online_included_fields.begin(),
                state.online_included_fields.end(),*field_index)!=
                    state.online_included_fields.end();
            if(included) {
                if(const auto it=state.online_date_targets.find(*field_index);
                   it!=state.online_date_targets.end())
                    selected_target=from_utf8(it->second);
            }
        }
        const auto proposed=(local_original || local_cue)
            ? L"(not supplied)" : show_field_values(row.candidate.values);
        const std::wstring status=row.reason=="local_physical_inventory_overflow"
            ? L"More original tags" :
            row.candidate.field=="MATCH_DIAGNOSTIC"
            ? (row.state==djmeta::online::FieldReviewState::Unchanged
                ? L"Suggested identity only" : L"Not safely matched") :
            local_original ? L"Keep original" :
            local_cue ? L"Keep original CUE" :
            cue_previewed ? (row.state==djmeta::online::FieldReviewState::Unchanged
                              ? L"Previewed: no-op" : L"Previewed: change") :
            included ? (selected_target.empty()?L"Included (preview)":
                L"Included -> "+selected_target) :
            qualified_online_field_preview(state) &&
            row.state==djmeta::online::FieldReviewState::NeedsReview &&
            djmeta::online::online_date_requires_explicit_target(row.candidate)
                ? L"Date policy needed" :
            row.reason=="musicbrainz_live_search_edition_date_display_only"
                ? L"Edition date only" :
            row.reason=="musicbrainz_live_search_isrc_identity_only"
                ? L"Recording ID only" :
            row.state==
            djmeta::online::FieldReviewState::Blocked ? L"Blocked"
            : row.state==djmeta::online::FieldReviewState::Unchanged
            ? L"No change" : L"Review";
        if (!insert_native_preview_detail_row(state.musicbrainz_details,
              static_cast<int>(i),field,original,proposed,status))
            throw std::runtime_error("Native candidate detail columns could not be populated.");
    }
    if (detail.size()>max_detail_rows) {
        const auto count=std::to_wstring(detail.size()-max_detail_rows);
        if (!insert_native_preview_detail_row(state.musicbrainz_details,
              static_cast<int>(max_detail_rows),
              L"Additional fields not displayed",count,L"",
              L"Limit reached; no writes"))
            throw std::runtime_error("Native detail overflow warning could not be displayed.");
    }
    // The online selection changes the actual foobar raw naming expression
    // in memory. Show that delta alongside field proposals, not in a popup
    // and never as an already validated filesystem destination.
    if(qualified_online_field_preview(state) &&
       state.online_field_preview && state.online_raw_route_valid) {
        const int row=static_cast<int>(
            (std::min)(detail.size(),max_detail_rows))+
            (detail.size()>max_detail_rows ? 1 : 0);
        if(!insert_native_preview_detail_row(state.musicbrainz_details,
             row,L"Raw filename (what-if)",
             from_utf8(state.online_raw_route_before),
             from_utf8(state.online_raw_route_after),
             state.online_raw_route_before==state.online_raw_route_after
                ? L"No naming delta" : L"Raw delta; not FileOps"))
            throw std::runtime_error("Online naming comparison could not be shown.");
    }
}

void update_musicbrainz_browse_headers(PreviewState& state) {
    if (!state.metadata_list) return;
    const bool browse=state.show_candidate && state.musicbrainz_live_view &&
        state.candidate_source_index==state.selected_track_index;
    const wchar_t* names[5]={L"Field",L"Original",L"Proposed",L"Source",L"Status"};
    if (state.existing_tags_view && !state.show_candidate) {
        names[0]=L"Tag field"; names[1]=L"All original values";
        names[2]=L""; names[3]=L"Source"; names[4]=L"Write status";
    }
    if (browse) {
        if(pasted_comparison_group_view(state)) {
            names[0]=L"Comparison group";
            names[1]=state.cue_candidate_comparison_mode
                ? L"CUE original" : L"Physical";
            names[2]=L"Pasted";
            names[3]=L"Origin";
            names[4]=L"Review";
        } else {
            names[0]=state.musicbrainz_release_loaded
                ? L"Album / Track" : L"Recording / Release";
            names[1]=state.musicbrainz_release_loaded?L"Current":L"Artist";
            names[2]=state.musicbrainz_release_loaded?L"Suggested":L"Date";
            names[3]=L"Source";
            names[4]=state.musicbrainz_release_loaded?L"Match":L"Identity check";
        }
    }
    for(int i=0;i<5;++i) {
        LVCOLUMNW column{};
        column.mask=LVCF_TEXT;
        column.pszText=const_cast<LPWSTR>(names[i]);
        ListView_SetColumn(state.metadata_list,i,&column);
    }
    // Widths are a user setting, not a property of the selected track.
    // Apply only when changing between review and candidate modes; never
    // when a list is repainted, resorted, or another track is selected.
    if (browse != state.musicbrainz_browse_columns_active) {
        if (!browse && state.musicbrainz_browse_columns_active) {
            capture_review_grid_controls(state.metadata_list,
                state.candidate_detail_grid,state.active_dpi);
        }
        if (browse)
            apply_review_grid_controls(state.metadata_list,
                state.candidate_detail_grid,state.active_dpi);
        else
            apply_review_grid_controls(state.metadata_list,
                state.show_candidate?state.candidate_detail_grid:state.detail_grid,
                state.active_dpi);
        state.musicbrainz_browse_columns_active=browse;
    }
}

// The native action is enabled only for the selected verified release, not
// merely when some unrelated release is present in provider results.
void update_online_release_load_button(PreviewState& state) {
    if(!state.dialog || !state.metadata_list) return;
    std::vector<std::string> exact_ids;
    if(state.discogs_live_view) {
        for(const auto& hit:state.discogs_results) {
            const bool master=hit.kind==djmeta::online::discogs::EntityKind::Master;
            exact_ids.push_back(std::string(master?"master:":"release:")+
                                std::to_string(hit.id));
        }
    } else {
        for(const auto& hit:state.musicbrainz_results)
            if(hit.kind==djmeta::online::musicbrainz::SearchKind::Release)
                exact_ids.push_back(hit.mbid);
    }
    std::string selected_identity;
    const int selected=ListView_GetNextItem(state.metadata_list,-1,LVNI_SELECTED);
    if(selected>=0 && static_cast<std::size_t>(selected)<state.candidate_view_order.size()) {
        const auto index=state.candidate_view_order[static_cast<std::size_t>(selected)];
        if(index<state.candidate_rows.size())
            selected_identity=state.candidate_rows[index].candidate.source_id;
    }
    const bool ready=native_release_loadable(
        state.show_candidate && state.musicbrainz_live_view &&
            state.musicbrainz_kind==djmeta::online::musicbrainz::SearchKind::Release,
        state.musicbrainz_release_loaded,
        state.candidate_source_index==state.selected_track_index,
        state.discogs_request!=nullptr || state.musicbrainz_request!=nullptr,
        selected_identity,exact_ids);
    EnableWindow(GetDlgItem(state.dialog,IDC_METADATA_MB_LOAD_RELEASE),
                 native_workspace_action_ready(state.stale,ready)?TRUE:FALSE);
    update_online_field_preview_controls(state);
}

void update_local_review_hint(PreviewState& state) {
    if(!state.dialog || !state.show_metadata || state.show_candidate ||
       state.existing_tags_view) return;
    if(state.selected_track_index>=state.track_summaries.size()) {
        SetDlgItemTextW(state.dialog,IDC_BATCH_HINT,
            native_workspace_display_hint(state.stale,
                L"No tracks match this filter. Choose All tracks to restore the full list."));
        return;
    }
    const auto& summary=state.track_summaries[state.selected_track_index];
    const auto count=ListView_GetSelectedCount(state.metadata_track_list);
    std::wstring notice=native_review_scope_hint(
        static_cast<std::size_t>(count),
        summary.music_changes,summary.extended_changes,
        summary.review_required,state.focused_metadata_rows.empty());
    notice+=L" Staged edits: "+std::to_wstring(summary.staged_changes)+
            L"; pending proposals: "+std::to_wstring(summary.pending_proposals)+
            L". Filter changes only the visible review queue.";
    SetDlgItemTextW(state.dialog,IDC_BATCH_HINT,
        native_workspace_display_hint(state.stale,notice.c_str()));
}

void update_metadata_table(PreviewState& state) {
    if (state.show_candidate) {
        update_online_field_preview_controls(state);
        // Candidate counts describe result identities, not field-change rows.
        // Update headers when entering live search, not just on tab switch.
        const wchar_t* candidate_headers[3]={
            state.musicbrainz_live_view
                ? (pasted_comparison_group_view(state)?L"Groups":L"Hits") : L"Tags",
            state.musicbrainz_live_view?L"Blocked":L"Diffs",
            state.musicbrainz_live_view?L"":L"Block"
        };
        for (int i=0;i<3;++i) {
            LVCOLUMNW column{};
            column.mask=LVCF_TEXT;
            column.pszText=const_cast<LPWSTR>(candidate_headers[i]);
            ListView_SetColumn(state.metadata_track_list,i+1,&column);
        }
        // A new track cannot reset a user-resized column. The online
        // track grid is initialized only when entering candidate mode.
        const bool browse_track=state.musicbrainz_live_view &&
            state.candidate_source_index==state.selected_track_index;
        if (browse_track && !state.candidate_online_track_columns_active) {
            capture_review_grid_controls(state.metadata_track_list,
                state.candidate_track_grid,state.active_dpi);
            ListView_SetColumnWidth(state.metadata_track_list,3,0);
            state.candidate_online_track_columns_active=true;
        } else if (!browse_track && state.candidate_online_track_columns_active) {
            // The fourth column is only meaningless during online browsing.
            const bool visible=(state.candidate_track_grid.visible_mask & (1u<<3))!=0;
            ListView_SetColumnWidth(state.metadata_track_list,3,
                visible ? MulDiv(state.candidate_track_grid.widths[3],
                                 state.active_dpi,96) : 0);
            state.candidate_online_track_columns_active=false;
        }
        const bool selected_valid = state.selected_track_index < state.entries.size();
        const bool physical = selected_valid &&
            state.entries[state.selected_track_index].input.physical_source_qualified &&
            !is_external_cue_locator(
                state.entries[state.selected_track_index].input.source_path);
        const bool selected_cue_preview = selected_valid &&
            state.candidate_source_index == state.selected_track_index &&
            (state.cue_inspection_mode || state.cue_candidate_comparison_mode);
        EnableWindow(GetDlgItem(state.dialog, IDC_METADATA_IMPORT_CANDIDATE),
                     native_workspace_action_ready(
                         state.stale,physical || selected_cue_preview)?TRUE:FALSE);
        const bool cue_readable = selected_valid &&
            (is_external_cue_locator(
                state.entries[state.selected_track_index].input.source_path) ||
             (physical && state.entries[state.selected_track_index].handle->get_subsong_index() == 0) ||
             // A foobar embedded CUE may be selected as a virtual track.
             // Enable read-only inspection, not physical/tag write access.
             (!is_external_cue_locator(
                 state.entries[state.selected_track_index].input.source_path) &&
              state.entries[state.selected_track_index].handle->get_subsong_index() != 0));
        EnableWindow(GetDlgItem(state.dialog, IDC_METADATA_INSPECT_CUE),
                     native_workspace_action_ready(state.stale,cue_readable)?TRUE:FALSE);
        const bool pending_online=state.discogs_request ||
            state.musicbrainz_request || state.beatport_public_request;
        EnableWindow(GetDlgItem(state.dialog, IDC_METADATA_MB_SEARCH),
            native_online_lookup_can_cancel(
                pending_online,state.online_user_cancel_requested) ||
            ((physical || (!state.beatport_offline_selected && cue_readable)) &&
             !pending_online && !state.stale) ? TRUE : FALSE);
        // Preserve the candidate, not its old sorted view row.
        std::string selected_browser_id;
        const int selected_before=ListView_GetNextItem(
            state.metadata_list,-1,LVNI_SELECTED);
        if (selected_before>=0 &&
            static_cast<std::size_t>(selected_before)<state.candidate_view_order.size()) {
            const auto old_index=state.candidate_view_order[
                static_cast<std::size_t>(selected_before)];
            if (old_index<state.candidate_rows.size()) {
                const auto& old=state.candidate_rows[old_index];
                selected_browser_id=old.candidate.source_id+"|"+old.field;
            }
        }
        state.candidate_view_order.clear();
        if ((physical || state.cue_inspection_mode ||
             state.cue_candidate_comparison_mode || state.musicbrainz_live_view) &&
            state.selected_track_index == state.candidate_source_index) {
            state.candidate_view_order=native_candidate_view_order(
                state.candidate_rows.size(),state.candidate_sort_column,
                state.candidate_sort_descending,
                [&](std::size_t row,int column) {
                    return candidate_cell_text(state.candidate_rows[row],column);
                });
        }
        ListView_SetItemCountEx(state.metadata_list,
            static_cast<int>(state.candidate_view_order.size()),
            LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
        layout_musicbrainz_detail_pane(state);
        update_musicbrainz_browse_headers(state);
        if (state.musicbrainz_live_view &&
            state.candidate_source_index==state.selected_track_index) {
            std::size_t target=0;
            bool found=false;
            for (std::size_t i=0;i<state.candidate_view_order.size();++i) {
                const auto& row=state.candidate_rows[state.candidate_view_order[i]];
                if (!selected_browser_id.empty() &&
                    row.candidate.source_id+"|"+row.field==selected_browser_id) {
                    target=i;
                    found=true;
                    break;
                }
            }
            const int selected_now=ListView_GetNextItem(
                state.metadata_list,-1,LVNI_SELECTED);
            if (!state.candidate_view_order.empty() &&
                native_browser_selection_requires_reset(
                    found,selected_now,target)) {
                state.updating_musicbrainz_selection=true;
                ListView_SetItemState(state.metadata_list,-1,0,LVIS_SELECTED);
                ListView_SetItemState(state.metadata_list,static_cast<int>(target),
                    LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
                state.updating_musicbrainz_selection=false;
            }
            refresh_musicbrainz_detail_rows(state);
        } else {
            ListView_DeleteAllItems(state.musicbrainz_details);
        }
        update_online_release_load_button(state);
        InvalidateRect(state.metadata_list, nullptr, FALSE);
        InvalidateRect(state.metadata_track_list, nullptr, FALSE); // counts may change after import
        const std::wstring hint = !selected_valid
            ? L"Select an audio file or CUE to inspect. No files are written."
            : state.musicbrainz_live_view &&
              state.candidate_source_index == state.selected_track_index
            ? (state.musicbrainz_release_loaded
                ? L"Release details and tentative CUE assignment, read-only. "
                  L"Unmatched tracks are blocked. No tags were written."
                : state.search_attempt_notice.empty()
                    ? L"Select a candidate for its field-by-field identity evidence. "
                      L"Online search rank is not match certainty; no files changed."
                    : state.search_attempt_notice)
            : state.cue_inspection_mode &&
              state.candidate_source_index == state.selected_track_index
            ? L"Source CUE metadata displayed read-only. To compare a clipboard candidate, "
              L"use @scope=edition for album, or @scope=recording and "
              L"@cue_track_ordinal=N for an exact CUE track. No tags were written."
            : state.cue_candidate_comparison_mode &&
              state.candidate_source_index == state.selected_track_index
            ? L"Manual clipboard evidence (provider unverified). CUE album / track comparison "
              L"only; Review/Blocked are not write approvals. No files were changed."
            : !physical
            ? L"Select Inspect CUE first, then Import clipboard to compare source "
              L"metadata. Direct virtual-subtrack writes remain blocked."
            : !state.candidate_view_order.empty()
            ? L"Pasted evidence (provider unverified). No change / Review / Blocked are "
              L"comparison statuses, not write approvals. No tags were written."
            : native_empty_online_candidate_hint(
                state.discogs_provider_selected,
                state.beatport_offline_selected);
        SetDlgItemTextW(state.dialog, IDC_BATCH_HINT,
            native_workspace_display_hint(state.stale,hint.c_str()));
        return;
    }
    // A separate inventory mode is not a proposal and can never be accepted.
    if (state.existing_tags_view) {
        state.existing_tag_rows.clear();
        if (state.selected_track_index < state.entries.size()) {
            const auto& entry=state.entries[state.selected_track_index];
            verify_snapshot(entry);
            if (entry.input.physical_source_qualified &&
                entry.handle->get_subsong_index()==0) {
                const auto current=entry.handle->get_info_ref();
                const auto doc=metadata_from_file_info(current->info());
                if (doc.fields.size()>10000)
                    throw std::runtime_error("Physical metadata inventory too large.");
                for (const auto& field:doc.fields) {
                    state.existing_tag_rows.emplace_back(
                        from_utf8(field.name),show_field_values(field.values));
                }
            }
        }
        ListView_SetItemCountEx(state.metadata_list,
            static_cast<int>(state.existing_tag_rows.size()),
            LVSICF_NOINVALIDATEALL|LVSICF_NOSCROLL);
        SetDlgItemTextW(state.dialog,IDC_BATCH_HINT,
            native_workspace_display_hint(state.stale,
                L"All original physical tag fields and multivalues are displayed. "
                L"Unchanged tags are included; the inventory is read-only. "
                L"Virtual CUE subtracks are intentionally excluded."));
        InvalidateRect(state.metadata_list,nullptr,FALSE);
        return;
    }
    // Preserve source-local proposal identity rather than virtual row position.
    std::set<std::size_t> selected_proposals;
    if (state.metadata_list &&
        state.focused_track_index == state.selected_track_index) {
        int row = -1;
        while ((row = ListView_GetNextItem(state.metadata_list, row, LVNI_SELECTED)) >= 0) {
            const auto view_row = static_cast<std::size_t>(row);
            if (view_row < state.metadata_view_order.size() &&
                state.metadata_view_order[view_row] < state.focused_metadata_rows.size())
                selected_proposals.insert(
                    state.focused_metadata_rows[state.metadata_view_order[view_row]].proposal_index);
        }
    }
    state.focused_metadata_rows = djmeta::selected_track_diffs(
        state.metadata_rows, state.metadata_row_index,
        state.selected_track_index, state.metadata_focus);
    state.focused_track_index = state.selected_track_index;
    const int logical_sort_column = state.detail_grid.sort_column == 4
        ? 99 : state.detail_grid.sort_column + 1;
    auto sorted_proposals = djmeta::sort_metadata_diff_rows(
        state.focused_metadata_rows, state.source_labels,
        logical_sort_column, state.detail_grid.sort_descending);
    if (state.detail_grid.sort_column == 4) {
        std::stable_sort(sorted_proposals.begin(), sorted_proposals.end(),
            [&](std::size_t a, std::size_t b) {
                const auto lhs = review_decision_caption(state, state.focused_metadata_rows[a]);
                const auto rhs = review_decision_caption(state, state.focused_metadata_rows[b]);
                return state.detail_grid.sort_descending ? lhs > rhs : lhs < rhs;
            });
    }
    // Projection validates every sorted index before any native ListView access.
    const auto grouped=djmeta::group_review_rows(
        state.focused_metadata_rows,sorted_proposals,state.review_group_expanded);
    state.metadata_view_order=grouped.row_order;
    state.metadata_view_group=grouped.group_at_row;
    state.review_group_counts=grouped.counts;
    if (state.metadata_list) {
        ListView_SetItemCountEx(state.metadata_list,
            static_cast<int>(state.metadata_view_order.size()),
            LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
        ListView_SetItemState(state.metadata_list, -1, 0, LVIS_SELECTED);
        for (std::size_t row = 0; row < state.metadata_view_order.size(); ++row) {
            const auto index=state.metadata_view_order[row];
            if(index<state.focused_metadata_rows.size() &&
               selected_proposals.count(
                   state.focused_metadata_rows[index].proposal_index))
                ListView_SetItemState(state.metadata_list, static_cast<int>(row),
                    LVIS_SELECTED, LVIS_SELECTED);
        }
        InvalidateRect(state.metadata_list, nullptr, FALSE);
    }
    update_local_review_hint(state);
}

void update_master_table(PreviewState& state) {
    // Retain every underlying source identity, not just the focused row.
    // This is essential for Selected tracks actions after a sort/filter.
    const auto previously_selected =
        selected_native_view_ids(state.metadata_track_list, state.track_view_order);
    // Immutable source names are parsed once when capturing/refreshing the
    // snapshot. Avoid 54k repeated string allocations on every review action.
    if(state.cached_track_sort_labels.size()!=state.entries.size())
        throw std::runtime_error("Track sort-label cache is stale; refresh snapshot.");
    std::vector<std::size_t> sorted_sources;
    if(state.track_grid.sort_column==0) {
        // File identity/name cannot be edited by in-memory review decisions.
        // Reuse the stable filename sort during 54k-track batch approvals.
        auto& cached=state.track_grid.sort_descending
            ? state.cached_name_order_descending
            : state.cached_name_order_ascending;
        if(cached.size()!=state.entries.size())
            cached=djmeta::sort_track_summaries(
                state.track_summaries,state.cached_track_sort_labels,
                0,state.track_grid.sort_descending);
        sorted_sources=cached;
    } else {
        // Review counts change after Accept/Reject/Reset: always re-sort
        // those dynamic columns against the newly refreshed summaries.
        sorted_sources=djmeta::sort_track_summaries(
            state.track_summaries,state.cached_track_sort_labels,
            state.track_grid.sort_column,state.track_grid.sort_descending);
    }
    state.track_view_order = djmeta::filter_track_view(
        state.track_summaries, sorted_sources, state.track_discovery);
    const std::set<std::size_t> visible(state.track_view_order.begin(),
                                        state.track_view_order.end());
    std::vector<std::size_t> selected;
    for (const auto id : previously_selected)
        if (visible.count(id) != 0) selected.push_back(id);
    if (selected.empty() && !state.track_view_order.empty()) {
        selected.push_back(visible.count(state.selected_track_index)
            ? state.selected_track_index : state.track_view_order.front());
    }
    // Focus and selected-track detail always refer to one selected source.
    if (!selected.empty() &&
        std::find(selected.begin(), selected.end(), state.selected_track_index) == selected.end())
        state.selected_track_index = selected.front();
    else if (selected.empty())
        state.selected_track_index = state.track_summaries.size();
    if (!state.metadata_track_list) return;
    state.updating_track_selection = true;
    ListView_SetItemCountEx(state.metadata_track_list,
        static_cast<int>(state.track_view_order.size()),
        LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    restore_native_view_selection(state.metadata_track_list, state.track_view_order,
                                  selected, state.selected_track_index);
    state.updating_track_selection = false;
    InvalidateRect(state.metadata_track_list, nullptr, FALSE);
}

void update_live_provider_controls(PreviewState& state) {
    if(!state.dialog) return;
    const bool candidate=state.show_candidate;
    const bool discogs=candidate && state.discogs_provider_selected;
    ShowWindow(GetDlgItem(state.dialog,IDC_METADATA_ONLINE_PROVIDER),
        candidate?SW_SHOW:SW_HIDE);
    ShowWindow(GetDlgItem(state.dialog,IDC_METADATA_DISCOGS_TOKEN_LABEL),
        discogs?SW_SHOW:SW_HIDE);
    ShowWindow(GetDlgItem(state.dialog,IDC_METADATA_DISCOGS_TOKEN),
        discogs?SW_SHOW:SW_HIDE);
    const bool next_master=discogs && state.discogs_live_view &&
        state.candidate_source_index==state.selected_track_index &&
        state.discogs_master_id &&
        state.discogs_master_page<state.discogs_master_pages &&
        state.discogs_master_page<
            djmeta::online::discogs::max_master_version_page;
    const bool pending=state.discogs_request || state.musicbrainz_request ||
                       state.beatport_public_request;
    const bool published_search=discogs && state.discogs_live_view &&
        !state.discogs_result_first_path.empty() &&
        state.search_plan_identity==state.discogs_result_identity &&
        !state.discogs_results.empty();
    const HWND search=GetDlgItem(state.dialog,IDC_METADATA_MB_SEARCH);
    if(!native_apply_online_cancel_button(
          search,pending,state.online_user_cancel_requested))
        SetDlgItemTextW(state.dialog,IDC_METADATA_MB_SEARCH,
            state.beatport_offline_selected ? L"Load Beatport URL" :
            native_discogs_search_caption(state.discogs_provider_selected,
                next_master,published_search,
                state.candidate_source_index==state.selected_track_index,
                state.musicbrainz_release_loaded,pending,
                state.discogs_result_attempt_index,
                state.discogs_result_attempt_total));
    if(state.beatport_offline_selected && !pending) {
        const bool selected=state.selected_track_index<state.entries.size();
        const bool physical=selected &&
            state.entries[state.selected_track_index].input.physical_source_qualified &&
            state.entries[state.selected_track_index].handle->get_subsong_index()==0 &&
            !is_external_cue_locator(
                state.entries[state.selected_track_index].input.source_path);
        EnableWindow(GetDlgItem(state.dialog,IDC_METADATA_MB_SEARCH),
                     state.show_candidate && physical && !pending && !state.stale);
    }
    SetDlgItemTextW(state.dialog,IDC_METADATA_IMPORT_CANDIDATE,
        L"Import clipboard");
    const bool next_result=discogs && state.discogs_live_view &&
        !state.musicbrainz_release_loaded && !state.discogs_master_id &&
        state.candidate_source_index==state.selected_track_index &&
        !state.stale && !state.discogs_request && !state.musicbrainz_request &&
        !state.discogs_result_first_path.empty() &&
        !state.discogs_result_identity.empty() &&
        state.search_plan_identity==state.discogs_result_identity &&
        state.search_attempt_index==state.discogs_result_attempt_index &&
        state.discogs_result_page>0 &&
        state.discogs_result_page<state.discogs_result_pages &&
        state.discogs_result_page<djmeta::online::discogs::max_search_result_page;
    const bool next_musicbrainz=state.show_candidate &&
        !discogs && !state.beatport_offline_selected &&
        state.musicbrainz_live_view && !state.discogs_live_view &&
        !state.musicbrainz_release_loaded &&
        state.candidate_source_index==state.selected_track_index &&
        !state.stale && !state.refreshing && !pending &&
        state.musicbrainz_more_pages &&
        state.musicbrainz_result_page>0 &&
        state.musicbrainz_result_page<
            djmeta::online::musicbrainz::max_search_page &&
        state.musicbrainz_results.size()<=
            djmeta::online::musicbrainz::max_search_candidates*7 &&
        !state.musicbrainz_page_one_path.empty() &&
        state.search_plan_identity==state.musicbrainz_page_identity &&
        state.search_attempt_index==state.musicbrainz_published_attempt_index;
    const bool crates_search=state.show_candidate &&
        state.beatport_offline_selected && !state.stale &&
        !state.refreshing && !pending &&
        state.selected_track_index<state.entries.size() &&
        state.entries[state.selected_track_index].input.physical_source_qualified &&
        state.entries[state.selected_track_index].handle->get_subsong_index()==0 &&
        !is_external_cue_locator(
            state.entries[state.selected_track_index].input.source_path);
    SetDlgItemTextW(state.dialog,IDC_METADATA_DISCOGS_NEXT_PAGE,
        state.beatport_offline_selected ? L"Find in Crates" : L"Next results page");
    ShowWindow(GetDlgItem(state.dialog,IDC_METADATA_DISCOGS_NEXT_PAGE),
               next_result || next_musicbrainz || crates_search?SW_SHOW:SW_HIDE);
    EnableWindow(GetDlgItem(state.dialog,IDC_METADATA_DISCOGS_NEXT_PAGE),
                 next_result || next_musicbrainz || crates_search?TRUE:FALSE);
}

const wchar_t* preview_history_action(PreviewHistoryKind kind) noexcept {
    switch(kind) {
    case PreviewHistoryKind::Accept: return L"Accept proposals";
    case PreviewHistoryKind::Reject: return L"Reject proposals";
    case PreviewHistoryKind::Reset: return L"Reset decisions";
    case PreviewHistoryKind::Manual: return L"Manual replacement";
    case PreviewHistoryKind::Route: return L"Recalculate route";
    case PreviewHistoryKind::UndoReview: return L"Undo review";
    case PreviewHistoryKind::UndoRoute: return L"Undo route";
    }
    return L"Unknown";
}
const wchar_t* preview_history_scope(const PreviewHistoryEvent& event) noexcept {
    if(event.kind==PreviewHistoryKind::Route)
        return event.scope==2 ? L"All tracks" : L"Selected tracks";
    if(event.kind==PreviewHistoryKind::UndoReview ||
       event.kind==PreviewHistoryKind::UndoRoute) return L"Last action";
    switch(event.scope) {
    case 0: return L"Selected fields";
    case 1: return L"Selected tracks";
    case 2: return L"All tracks";
    case 3: return L"Visible queue";
    default: return L"Unknown";
    }
}
void refresh_preview_history_list(PreviewState& state) {
    if(!state.history_list) return;
    ListView_DeleteAllItems(state.history_list);
    const auto& events=state.preview_history.events();
    state.history_view_order=preview_history_visible_indices(
        state.preview_history,state.history_filter);
    for(std::size_t i=0;i<state.history_view_order.size();++i) {
        const auto& event=events[state.history_view_order[i]];
        std::wstring sequence=std::to_wstring(event.sequence);
        LVITEMW row{};
        row.mask=LVIF_TEXT;
        row.iItem=static_cast<int>(i);
        row.pszText=sequence.data();
        const int index=static_cast<int>(SendMessageW(state.history_list,LVM_INSERTITEMW,0,
            reinterpret_cast<LPARAM>(&row)));
        if(index<0) throw std::runtime_error("Cannot display preview history.");
        std::wstring sources=std::to_wstring(event.sources);
        std::wstring items=std::to_wstring(event.items);
        ListView_SetItemText(state.history_list,index,1,
            const_cast<LPWSTR>(preview_history_action(event.kind)));
        ListView_SetItemText(state.history_list,index,2,
            const_cast<LPWSTR>(preview_history_scope(event)));
        ListView_SetItemText(state.history_list,index,3,sources.data());
        ListView_SetItemText(state.history_list,index,4,items.data());
    }
}
void record_preview_history(PreviewState& state,PreviewHistoryKind kind,
                            std::size_t sources,std::size_t items,unsigned scope,
                            std::size_t source_index=static_cast<std::size_t>(-1)) {
    if(state.preview_history.append(kind,sources,items,scope,source_index) &&
       state.tabs && TabCtrl_GetCurSel(state.tabs)==3)
        refresh_preview_history_list(state);
}

void show_preview_page(HWND dialog, PreviewState& state,
                       bool metadata, bool candidate = false) {
    if(state.history_list) ShowWindow(state.history_list,SW_HIDE);
    for(int id:{IDC_BATCH_HISTORY_FILTER,IDC_BATCH_HISTORY_FILTER_LABEL})
        ShowWindow(GetDlgItem(dialog,id),SW_HIDE);
    if (metadata && state.native_grid_candidate_mode!=candidate &&
        state.metadata_track_list && state.metadata_list) {
        // Capture the outgoing grid ONCE at the actual previous display DPI.
        // Review and online search never reuse each other's column widths.
        if (state.native_grid_candidate_mode) {
            capture_review_grid_controls(state.metadata_track_list,
                state.candidate_track_grid,state.active_dpi);
            capture_review_grid_controls(state.metadata_list,
                state.candidate_detail_grid,state.active_dpi);
            state.candidate_online_track_columns_active=false;
            state.musicbrainz_browse_columns_active=false;
            apply_review_grid_controls(state.metadata_track_list,state.track_grid,
                                       state.active_dpi);
            apply_review_grid_controls(state.metadata_list,state.detail_grid,
                                       state.active_dpi);
        } else {
            capture_review_grid_controls(state.metadata_track_list,
                state.track_grid,state.active_dpi);
            capture_review_grid_controls(state.metadata_list,
                state.detail_grid,state.active_dpi);
            apply_review_grid_controls(state.metadata_track_list,
                state.candidate_track_grid,state.active_dpi);
            apply_review_grid_controls(state.metadata_list,
                state.candidate_detail_grid,state.active_dpi);
        }
        state.native_grid_candidate_mode=candidate;
    }
    state.show_metadata = metadata;
    state.show_candidate = candidate;
    SetDlgItemTextW(dialog, IDC_BATCH_GUIDE, candidate
        ? L"1. Select a track. Double-click to search or click Search. Compare right-hand evidence; drag the divider or use Ctrl+Shift+arrows."
        : metadata
        ? L"2. Review proposals. Ctrl+Shift+arrows resize panes; Accept/Manual affects only preview filenames, never files."
        : L"3. Preview file names and folders from accepted values only. Paths remain unverified; nothing is moved.");
    if (candidate) prefill_musicbrainz_query(state);
    ShowWindow(GetDlgItem(dialog,IDC_BATCH_UNDO),
               candidate ? SW_HIDE : SW_SHOW);
    EnableWindow(GetDlgItem(dialog,IDC_BATCH_UNDO),
                 state.last_preview_undo && !state.stale ? TRUE : FALSE);
    update_live_provider_controls(state);
    ShowWindow(GetDlgItem(dialog, IDC_METADATA_IMPORT_CANDIDATE),
        candidate ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_METADATA_INSPECT_CUE),
        candidate ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_METADATA_MB_QUERY),
        candidate ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_METADATA_MB_SEARCH),
        candidate ? SW_SHOW : SW_HIDE);
    update_live_provider_controls(state); // also gates search page navigation
    ShowWindow(GetDlgItem(dialog, IDC_METADATA_MB_LOAD_RELEASE),
        candidate ? SW_SHOW : SW_HIDE);
    update_online_field_preview_controls(state);
    layout_musicbrainz_detail_pane(state);
    update_musicbrainz_browse_headers(state);
    const wchar_t* headers[2] = {
        candidate || state.existing_tags_view ? L"Source" : L"Safety",
        candidate ? L"Status" : state.existing_tags_view
            ? L"Write status" : L"Decision"
    };
    for (int i = 0; i < 2; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT;
        col.pszText = const_cast<LPWSTR>(headers[i]);
        ListView_SetColumn(state.metadata_list, i + 3, &col);
    }
    const wchar_t* track_headers[3] = {
        candidate ? (state.musicbrainz_live_view
                ? (pasted_comparison_group_view(state)?L"Groups":L"Hits") : L"Tags") :
            state.existing_tags_view?L"Tags":L"Music",
        candidate ? (state.musicbrainz_live_view?L"Blocked":L"Diffs") :
            state.existing_tags_view?L"":L"Other",
        candidate ? (state.musicbrainz_live_view?L"":L"Block") :
            state.existing_tags_view?L"":L"Review"
    };
    for (int i = 0; i < 3; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT;
        col.pszText = const_cast<LPWSTR>(track_headers[i]);
        ListView_SetColumn(state.metadata_track_list, i + 1, &col);
    }
    InvalidateRect(state.metadata_track_list, nullptr, FALSE);
    auto sort_view = state.detail_grid;
    if (candidate) {
        sort_view.sort_column = state.candidate_sort_column;
        sort_view.sort_descending = state.candidate_sort_descending;
    }
    show_review_grid_sort_arrow(state.metadata_list, sort_view);
    ShowWindow(state.metadata_list, metadata ? SW_SHOW : SW_HIDE);
    ShowWindow(state.metadata_track_list, metadata ? SW_SHOW : SW_HIDE);
    ShowWindow(state.metadata_filter, metadata && !candidate ? SW_SHOW : SW_HIDE);
    ShowWindow(state.metadata_track_filter, metadata && !candidate ? SW_SHOW : SW_HIDE);
    ShowWindow(state.metadata_scope, metadata && !candidate ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_METADATA_VISIBLE_WHITESPACE),
               metadata && !candidate ? SW_SHOW : SW_HIDE);
    for (int id : {IDC_METADATA_ACCEPT, IDC_METADATA_REJECT,
                   IDC_METADATA_RESET, IDC_METADATA_MANUAL_INPUT,
                   IDC_METADATA_USE_VALUE})
        ShowWindow(GetDlgItem(dialog,id),
                   metadata && !candidate && !state.existing_tags_view
                       ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_METADATA_TRACK_FILTER_LABEL),
        metadata && !candidate ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_METADATA_FILTER_LABEL),
        metadata && !candidate ? SW_SHOW : SW_HIDE);
    ShowWindow(state.list, metadata ? SW_HIDE : SW_SHOW);
    for(const int id:{IDC_BATCH_LOCATION_FILTER,IDC_BATCH_FILE_ACTION,
                      IDC_BATCH_LOCATION_FILTER_LABEL,IDC_BATCH_LOCATION_NOTE})
        ShowWindow(GetDlgItem(dialog,id),metadata?SW_HIDE:SW_SHOW);
    ShowWindow(GetDlgItem(dialog,IDC_BATCH_BACK_TO_CUE),
        !metadata && state.cue_return.has_value() ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog,IDC_BATCH_NEXT_CUE),
        !metadata && !captured_cue_carriers(state).empty()
            ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog,IDC_BATCH_AUDIT),
        metadata ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(dialog,IDC_BATCH_COPY_AUDIT),
        metadata ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(dialog,IDC_BATCH_COPY_ISSUES),
        metadata ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(dialog,IDC_BATCH_NEXT_ISSUE),
        candidate ? SW_HIDE : SW_SHOW);
    EnableWindow(GetDlgItem(dialog,IDC_BATCH_NEXT_ISSUE),
        state.stale ? FALSE : TRUE);
    for (int id : {IDC_BATCH_ROUTE_LABEL,
                   IDC_BATCH_DEST_LABEL, IDC_BATCH_PATTERN_LABEL,
                   IDC_BATCH_PROFILE_PICKER, IDC_BATCH_PROFILE_SAVE,
                   IDC_BATCH_PROFILE_DELETE, IDC_BATCH_PROFILE_COPY,
                   IDC_BATCH_PROFILE_PASTE,
                   IDC_BATCH_DESTINATION, IDC_BATCH_PATTERN,
                   IDC_BATCH_APPLY_SELECTED, IDC_BATCH_APPLY_VISIBLE, IDC_BATCH_APPLY_ALL}) {
        ShowWindow(GetDlgItem(dialog,id), metadata ? SW_HIDE : SW_SHOW);
    }
    if (metadata) {
        update_metadata_table(state);
    } else {
        update_workflow_audit_hint(state);
    }
}

void show_history_page(HWND dialog,PreviewState& state) {
    // Reuse the same modeless workspace and source snapshot; no new writer,
    // window, persistent history database or clipboard access.
    show_preview_page(dialog,state,false,false);
    for(const int id:{IDC_BATCH_LIST,IDC_BATCH_LOCATION_FILTER,
        IDC_BATCH_FILE_ACTION,IDC_BATCH_BACK_TO_CUE,IDC_BATCH_NEXT_CUE,
        IDC_BATCH_LOCATION_FILTER_LABEL,IDC_BATCH_LOCATION_NOTE,
        IDC_BATCH_ROUTE_LABEL,IDC_BATCH_DEST_LABEL,IDC_BATCH_PATTERN_LABEL,
        IDC_BATCH_PROFILE_PICKER,IDC_BATCH_PROFILE_SAVE,
        IDC_BATCH_PROFILE_DELETE,IDC_BATCH_PROFILE_COPY,
        IDC_BATCH_PROFILE_PASTE,IDC_BATCH_DESTINATION,IDC_BATCH_PATTERN,
        IDC_BATCH_APPLY_SELECTED, IDC_BATCH_APPLY_VISIBLE, IDC_BATCH_APPLY_ALL,IDC_BATCH_AUDIT,
        IDC_BATCH_COPY_AUDIT,IDC_BATCH_COPY_ISSUES,IDC_BATCH_NEXT_ISSUE})
        ShowWindow(GetDlgItem(dialog,id),SW_HIDE);
    refresh_preview_history_list(state);
    ShowWindow(state.history_list,SW_SHOW);
    for(int id:{IDC_BATCH_HISTORY_FILTER,IDC_BATCH_HISTORY_FILTER_LABEL})
        ShowWindow(GetDlgItem(dialog,id),SW_SHOW);
    SetDlgItemTextW(dialog,IDC_BATCH_GUIDE,
        L"4. Filter history; double-click a single-source action to inspect its current preview. No writes.");
    SetDlgItemTextW(dialog,IDC_BATCH_HINT,
        L"Session only (last 256 actions). Multi-track actions are summaries. Refresh clears history.");
}

// Fail closed on stale source/rules/route evidence even when invoked from
// unmanaged WM_NOTIFY: no C++ exception may escape a native window proc.
void mark_stale_workspace(PreviewState& state);

// A deliberate double-click/Enter, Next issue/CUE, history jump, album
// drilldown or provider switch may leave a source-bound online preview.
// Offer ONE default-No choice rather than silently losing explicit Include
// and date mappings or trapping the user in the current source forever.
// Ordinary single-click selection still uses LVN_ITEMCHANGING veto.
bool confirm_online_preview_navigation(HWND dialog,PreviewState& state,
    std::size_t next_source,bool changing_provider=false) {
    if(!native_online_preview_requires_explicit_discard(
           state.selected_track_index,next_source,
           state.online_included_fields.size(),changing_provider))
        return true;
    if(state.stale || state.refreshing ||
       state.selected_track_index>=state.entries.size() ||
       (!changing_provider && next_source>=state.entries.size())) {
        SetDlgItemTextW(dialog,IDC_BATCH_HINT,
            L"Online source switch refused: stale or invalid source identity.");
        return false;
    }
    try {
        verify_snapshot(state.entries[state.selected_track_index]);
        verify_rules_snapshot(state.captured_rules);
    } catch (...) {
        mark_stale_workspace(state);
        return false;
    }
    const std::wstring prompt=L"The current physical source has "+
        std::to_wstring(state.online_included_fields.size())+
        L" explicitly included online field(s).\n\n"
        L"Switching to "+std::wstring(changing_provider
            ? L"another provider" : L"another track")+
        L" will discard ONLY these in-memory Include/date choices and "
        L"their raw naming overlay. Original tags, CUE data, local review "
        L"decisions and saved profiles remain unchanged.\n\n"
        L"Discard this preview and continue? (Default: No)";
    if(MessageBoxW(dialog,prompt.c_str(),L"Switch online preview source?",
                   MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2)!=IDYES) {
        SetDlgItemTextW(dialog,IDC_BATCH_HINT,
            L"Online field choices preserved; source/provider unchanged. "
            L"Double-click/Enter again to switch, or Reset online preview.");
        return false;
    }
    invalidate_online_request(state);
    state.online_included_fields.clear();
    state.online_date_targets.clear();
    state.online_field_preview.reset();
    state.online_physical_edition.reset();
    state.online_field_snapshot.reset();
    state.online_raw_target_observation.reset();
    state.online_edition_source=(std::numeric_limits<std::size_t>::max)();
    state.online_raw_route_valid=false;
    state.online_raw_route_before.clear();
    state.online_raw_route_after.clear();
    // Rebuild the complete source-order collision and CUE queue from the
    // local staged metadata, never from a now-discarded provider overlay.
    try {
        update_table(state);
    } catch (...) {
        mark_stale_workspace(state);
        return false;
    }
    return true;
}

// Inspect one historical source without replaying an action, starting a
// network request, or treating the session history as write authorization.
void navigate_preview_history(HWND dialog,PreviewState& state,int visible_row) {
    if(state.stale) throw std::invalid_argument("History source snapshot stale; refresh.");
    if(visible_row<0 ||
       static_cast<std::size_t>(visible_row)>=state.history_view_order.size()) return;
    const auto index=state.history_view_order[static_cast<std::size_t>(visible_row)];
    if(index>=state.preview_history.events().size())
        throw std::invalid_argument("History selection changed; retry.");
    const auto& event=state.preview_history.events()[index];
    const auto source=preview_history_single_source(event,state.entries.size());
    if(!source) {
        SetDlgItemTextW(dialog,IDC_BATCH_HINT,
            L"Multiple sources: inspect the current Review/File locations queue instead.");
        return;
    }
    if(!confirm_online_preview_navigation(dialog,state,*source)) return;
    verify_snapshot(state.entries[*source]);
    verify_rules_snapshot(state.captured_rules);
    if(*source!=state.selected_track_index) invalidate_online_request(state);
    const bool route=event.kind==PreviewHistoryKind::Route ||
                     event.kind==PreviewHistoryKind::UndoRoute;
    if(route) {
        state.location_queue=djmeta::BatchLocationQueue::All;
        state.selected_track_index=*source;
        SendDlgItemMessageW(dialog,IDC_BATCH_LOCATION_FILTER,CB_SETCURSEL,0,0);
        update_table(state);
        TabCtrl_SetCurSel(state.tabs,2);
        show_preview_page(dialog,state,false,false);
        const auto it=std::find(state.view_order.begin(),state.view_order.end(),*source);
        if(it==state.view_order.end())
            throw std::invalid_argument("History source not in file queue.");
        const int row=static_cast<int>(it-state.view_order.begin());
        ListView_SetItemState(state.list,-1,0,LVIS_SELECTED);
        ListView_SetItemState(state.list,row,LVIS_SELECTED|LVIS_FOCUSED,
                              LVIS_SELECTED|LVIS_FOCUSED);
        ListView_EnsureVisible(state.list,row,FALSE);
        SetFocus(state.list);
    } else {
        state.track_discovery=djmeta::TrackDiscovery::All;
        SendDlgItemMessageW(dialog,IDC_METADATA_TRACK_FILTER,CB_SETCURSEL,0,0);
        state.updating_track_selection=true;
        ListView_SetItemState(state.metadata_track_list,-1,0,LVIS_SELECTED);
        state.updating_track_selection=false;
        state.selected_track_index=*source;
        state.focused_track_index=static_cast<std::size_t>(-1);
        update_master_table(state);
        TabCtrl_SetCurSel(state.tabs,1);
        show_preview_page(dialog,state,true,false);
        const auto it=std::find(state.track_view_order.begin(),
                                state.track_view_order.end(),*source);
        if(it==state.track_view_order.end())
            throw std::invalid_argument("History source not in review queue.");
        ListView_EnsureVisible(state.metadata_track_list,
            static_cast<int>(it-state.track_view_order.begin()),FALSE);
        SetFocus(state.metadata_track_list);
    }
    SetDlgItemTextW(dialog,IDC_BATCH_HINT,
        L"History jump: current preview only. No action replayed or FileOps/CUE approval.");
}

// Navigate the entire captured read-only batch; no implicit search or write.
void navigate_next_workflow_issue(HWND dialog,PreviewState& state) {
    if(state.stale) throw std::runtime_error("Workspace stale; refresh.");
    const auto audit=djmeta::audit_workflow_preview(
        state.table,state.analyses,state.review_decisions);
    const auto next=djmeta::next_workflow_issue(
        audit,state.table,state.analyses,state.review_decisions,
        state.next_issue_cursor);
    using Kind=djmeta::WorkflowNextIssueKind;
    if(next.kind==Kind::None || next.kind==Kind::FinalFileOpsUnqualified) {
        SetDlgItemTextW(dialog,IDC_BATCH_HINT,
            next.kind==Kind::FinalFileOpsUnqualified
            ? L"Local review/raw route queue resolved. Final foobar File "
              L"Operations destination, collision and CUE checks remain "
              L"UNQUALIFIED. No write or final approval."
            : L"No pending local proposals or raw route findings. "
              L"Final FileOps/CUE approval is not implied.");
        return;
    }
    if(next.source_index>=state.entries.size())
        throw std::runtime_error("Next issue source identity stale.");
    if(!confirm_online_preview_navigation(dialog,state,next.source_index)) return;
    if(next.source_index!=state.selected_track_index)
        invalidate_online_request(state);
    state.next_issue_cursor=(next.source_index+1)%state.entries.size();
    if(next.kind==Kind::RawRouteConflict || next.kind==Kind::RawRouteBlocker) {
        // Guided navigation reveals hidden source IDs without weakening audit.
        state.location_queue=djmeta::BatchLocationQueue::All;
        SendDlgItemMessageW(dialog,IDC_BATCH_LOCATION_FILTER,CB_SETCURSEL,0,0);
        update_table(state);
        TabCtrl_SetCurSel(state.tabs,2);
        show_preview_page(dialog,state,false,false);
        const auto it=std::find(state.view_order.begin(),state.view_order.end(),
                                next.source_index);
        if(it==state.view_order.end())
            throw std::runtime_error("Raw route source not visible.");
        const auto row=static_cast<int>(it-state.view_order.begin());
        ListView_SetItemState(state.list,-1,0,LVIS_SELECTED);
        ListView_SetItemState(state.list,row,LVIS_SELECTED|LVIS_FOCUSED,
                              LVIS_SELECTED|LVIS_FOCUSED);
        ListView_EnsureVisible(state.list,row,FALSE);
        SetFocus(state.list);
        const std::wstring hint=L"Raw route issue on source #"+
            std::to_wstring(next.source_index+1)+L": "+
            from_utf8(issue_status_text(next.issue_code))+
            L". Review route; final FileOps destination and CUE links "
            L"remain unqualified. No writes.";
        SetDlgItemTextW(dialog,IDC_BATCH_HINT,hint.c_str());
        return;
    }
    state.track_discovery=djmeta::TrackDiscovery::All;
    state.metadata_focus=djmeta::MetadataFocus::All;
    state.existing_tags_view=false;
    state.review_group_expanded={true,true,true};
    SendDlgItemMessageW(dialog,IDC_METADATA_TRACK_FILTER,CB_SETCURSEL,0,0);
    SendDlgItemMessageW(dialog,IDC_METADATA_FILTER,CB_SETCURSEL,2,0);
    state.updating_track_selection=true;
    ListView_SetItemState(state.metadata_track_list,-1,0,LVIS_SELECTED);
    state.updating_track_selection=false;
    state.selected_track_index=next.source_index;
    state.focused_track_index=(std::numeric_limits<std::size_t>::max)();
    update_master_table(state);
    TabCtrl_SetCurSel(state.tabs,1);
    show_preview_page(dialog,state,true,false);
    const auto it=std::find(state.track_view_order.begin(),
                            state.track_view_order.end(),next.source_index);
    if(it==state.track_view_order.end())
        throw std::runtime_error("Review source not visible.");
    ListView_EnsureVisible(state.metadata_track_list,
        static_cast<int>(it-state.track_view_order.begin()),FALSE);
    bool found=false;
    for(std::size_t row=0;row<state.metadata_view_order.size();++row) {
        if(row<state.metadata_view_group.size() &&
           state.metadata_view_group[row]>=0) continue;
        const auto index=state.metadata_view_order[row];
        if(index<state.focused_metadata_rows.size() &&
           state.focused_metadata_rows[index].proposal_index==next.proposal_index) {
            ListView_SetItemState(state.metadata_list,-1,0,LVIS_SELECTED);
            ListView_SetItemState(state.metadata_list,static_cast<int>(row),
                LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
            ListView_EnsureVisible(state.metadata_list,static_cast<int>(row),FALSE);
            found=true;break;
        }
    }
    if(!found) throw std::runtime_error(
        "Pending proposal no longer visible. Refresh snapshot.");
    SetFocus(state.metadata_list);
    const wchar_t* level=next.kind==Kind::PendingReview?L"REVIEW":
        next.kind==Kind::PendingConfident?L"CONFIDENT":L"SAFE";
    const std::wstring hint=L"Next pending "+std::wstring(level)+
        L" proposal: source #"+std::to_wstring(next.source_index+1)+
        L". Inspect original/proposed values, then explicitly "
        L"Accept, Reject or Manual. No writes.";
    SetDlgItemTextW(dialog,IDC_BATCH_HINT,hint.c_str());
}

void add_column(HWND list, int index, const wchar_t* name, int width) {
    LVCOLUMNW column = {};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = const_cast<LPWSTR>(name);
    column.cx = width;
    column.iSubItem = index;
    if (ListView_InsertColumn(list, index, &column) < 0)
        throw std::runtime_error("Unable to add a preview table column.");
}


// All column IDs are logical IDs; the user may rearrange their visual order.
constexpr const wchar_t* kBatchColumnNames[djmeta::kBatchPreviewColumnCount] = {
    L"Source file", L"Profile", L"Proposed raw target", L"Status"
};
constexpr unsigned kColumnMenuBase = 41000u;
constexpr unsigned kColumnMenuReset = 41020u;

constexpr std::array<const wchar_t*, 4> kTrackGridColumnNames{
    L"Track", L"Music", L"Other", L"Review"
};
constexpr std::array<const wchar_t*, 5> kDetailGridColumnNames{
    L"Field", L"Original", L"Proposed", L"Safety", L"Decision"
};
constexpr std::array<const wchar_t*, 4> kCandidateTrackGridColumnNames{
    L"Track", L"Hits / Tags", L"Blocked / Diffs", L"Block"
};
constexpr std::array<const wchar_t*, 5> kCandidateDetailGridColumnNames{
    L"Field", L"Original / Artist", L"Candidate / Date", L"Source", L"Status"
};
constexpr unsigned kTrackColumnMenuBase = 41100u;
constexpr unsigned kDetailColumnMenuBase = 41200u;

int current_dpi(HWND window) {
    return native_window_dpi(window);
}
int to_pixels(HWND window, int logical) {
    return MulDiv(logical, current_dpi(window), 96);
}
int to_logical(HWND window, int pixels) {
    return MulDiv(pixels, 96, current_dpi(window));
}

void show_sort_arrow(PreviewState& state) {
    if (!state.list) return;
    const HWND header = ListView_GetHeader(state.list);
    if (!header) return;
    for (int col = 0; col < djmeta::kBatchPreviewColumnCount; ++col) {
        HDITEMW header_item{};
        header_item.mask = HDI_FORMAT;
        if (SendMessageW(header, HDM_GETITEMW, static_cast<WPARAM>(col),
                reinterpret_cast<LPARAM>(&header_item)) == 0) continue;
        header_item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (col == state.layout.sort_column)
            header_item.fmt |= state.layout.sort_descending ? HDF_SORTDOWN : HDF_SORTUP;
        SendMessageW(header, HDM_SETITEMW, static_cast<WPARAM>(col),
            reinterpret_cast<LPARAM>(&header_item));
    }
}

void apply_column_layout(PreviewState& state) {
    if (!state.list) return;
    for (int col = 0; col < djmeta::kBatchPreviewColumnCount; ++col) {
        const unsigned flag = 1u << static_cast<unsigned>(col);
        const int width = (state.layout.visible_mask & flag) != 0
            ? to_pixels(state.list, state.layout.widths[static_cast<std::size_t>(col)])
            : 0;
        ListView_SetColumnWidth(state.list, col, width);
    }
    ListView_SetColumnOrderArray(
        state.list, djmeta::kBatchPreviewColumnCount, state.layout.order.data());
    show_sort_arrow(state);
}

void capture_column_layout(PreviewState& state) {
    if (!state.list) return;
    std::array<int, djmeta::kBatchPreviewColumnCount> current_order{};
    if (ListView_GetColumnOrderArray(
            state.list, djmeta::kBatchPreviewColumnCount,
            current_order.data())) {
        state.layout.order = current_order;
    }
    for (int col = 0; col < djmeta::kBatchPreviewColumnCount; ++col) {
        if ((state.layout.visible_mask & (1u << static_cast<unsigned>(col))) == 0)
            continue; // preserve remembered width of hidden columns
        const int pixels = ListView_GetColumnWidth(state.list, col);
        if (pixels > 0) {
            const int width = to_logical(state.list, pixels);
            state.layout.widths[static_cast<std::size_t>(col)] =
                (std::max)(48, (std::min)(3000, width));
        }
    }
}

void show_column_menu(HWND dialog, PreviewState& state, LPARAM pointer) {
    if (!state.list) return;
    capture_column_layout(state);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (unsigned col = 0; col < djmeta::kBatchPreviewColumnCount; ++col) {
        const unsigned bit = 1u << col;
        const bool shown = (state.layout.visible_mask & bit) != 0;
        const UINT flags = MF_STRING |
            (shown ? MF_CHECKED : MF_UNCHECKED) |
            (shown && state.layout.visible_mask == bit ? MF_GRAYED : 0u);
        AppendMenuW(menu, flags, kColumnMenuBase + col, kBatchColumnNames[col]);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kColumnMenuReset, L"Reset column layout");

    const POINT location=native_column_menu_point(
        ListView_GetHeader(state.list),pointer);
    const UINT selected = TrackPopupMenu(menu,
        TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, location.x, location.y,
        0, dialog, nullptr);
    DestroyMenu(menu);

    if (selected >= kColumnMenuBase &&
        selected < kColumnMenuBase + djmeta::kBatchPreviewColumnCount) {
        const unsigned column = selected - kColumnMenuBase;
        const unsigned bit = 1u << column;
        if ((state.layout.visible_mask & bit) != 0) {
            if (state.layout.visible_mask == bit) return; // one visible minimum
            state.layout.visible_mask &= ~bit;
        } else {
            state.layout.visible_mask |= bit;
        }
        apply_column_layout(state);
    } else if (selected == kColumnMenuReset) {
        state.layout = djmeta::default_batch_table_layout();
        apply_column_layout(state);
        update_table(state);
    }
}

// Shared context menu for the two independently configurable review grids.
// Drag reorder and widths are first captured from the real Windows header.
template<std::size_t N>
bool show_review_grid_column_menu(
    HWND dialog, HWND list, djmeta::ReviewGridLayout<N>& layout,
    const djmeta::ReviewGridLayout<N>& defaults,
    const std::array<const wchar_t*, N>& names, unsigned menu_base,
    LPARAM pointer, int temporarily_hidden_column = -1) {
    if (!list) return false;
    capture_review_grid_controls(list, layout, current_dpi(list));
    HMENU menu = CreatePopupMenu();
    if (!menu) return false;
    for (std::size_t i = 0; i < N; ++i) {
        const unsigned bit = 1u << i;
        const bool shown = (layout.visible_mask & bit) != 0;
        const UINT flags = MF_STRING |
            (shown ? MF_CHECKED : MF_UNCHECKED) |
            (shown && layout.visible_mask == bit ? MF_GRAYED : 0u);
        AppendMenuW(menu, flags, menu_base + static_cast<unsigned>(i), names[i]);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, menu_base + static_cast<unsigned>(N),
                L"Reset columns");

    const POINT location=native_column_menu_point(
        ListView_GetHeader(list),pointer);
    const UINT selected = TrackPopupMenu(menu,
        TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        location.x, location.y, 0, dialog, nullptr);
    DestroyMenu(menu);
    if (selected >= menu_base && selected < menu_base + N) {
        const std::size_t column = static_cast<std::size_t>(selected - menu_base);
        const bool visible = (layout.visible_mask & (1u << column)) != 0;
        if (!djmeta::set_review_column_visible(layout, column, !visible))
            return false;
    } else if (selected == menu_base + N) {
        layout = defaults;
        apply_review_grid_controls(list, layout, current_dpi(list),
                                   temporarily_hidden_column);
        return true; // sort key may also have changed: refresh source mapping
    } else return false;
    apply_review_grid_controls(list, layout, current_dpi(list),
                               temporarily_hidden_column);
    return false;
}

bool show_active_track_grid_column_menu(HWND dialog, PreviewState& state,
                                        LPARAM pointer) {
    const bool candidate=state.native_grid_candidate_mode;
    auto& layout=active_review_grid_layout(
        state.track_grid,state.candidate_track_grid,candidate);
    const auto defaults=candidate
        ? djmeta::default_review_grid_layout<4>({280,75,90,85})
        : default_track_grid_layout();
    return show_review_grid_column_menu(dialog,state.metadata_track_list,
        layout,defaults,
        candidate?kCandidateTrackGridColumnNames:kTrackGridColumnNames,
        kTrackColumnMenuBase,pointer,
        candidate && state.candidate_online_track_columns_active ? 3 : -1);
}

bool show_active_detail_grid_column_menu(HWND dialog, PreviewState& state,
                                         LPARAM pointer) {
    const bool candidate=state.native_grid_candidate_mode;
    auto& layout=active_review_grid_layout(
        state.detail_grid,state.candidate_detail_grid,candidate);
    const auto defaults=candidate
        ? djmeta::default_review_grid_layout<5>({260,165,130,95,160})
        : default_detail_grid_layout();
    return show_review_grid_column_menu(dialog,state.metadata_list,
        layout,defaults,
        candidate?kCandidateDetailGridColumnNames:kDetailGridColumnNames,
        kDetailColumnMenuBase,pointer);
}

// The header is a child of the ListView, not the dialog. Subclassing it
// ensures keyboard/mouse WM_CONTEXTMENU reliably reaches our column picker.
LRESULT CALLBACK batch_header_proc(
    HWND header, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR subclass_id, DWORD_PTR ref_data) {
    auto* state = reinterpret_cast<PreviewState*>(ref_data);
    if (message == WM_CONTEXTMENU && state && state->list) {
        const HWND dialog = GetParent(state->list);
        if (header == ListView_GetHeader(state->list)) {
            show_column_menu(dialog, *state, lp);
        } else if (header == ListView_GetHeader(state->metadata_track_list)) {
            if (show_active_track_grid_column_menu(dialog,*state,lp)) {
                update_master_table(*state);
                update_metadata_table(*state);
            }
        } else if (header == ListView_GetHeader(state->metadata_list)) {
            if (show_active_detail_grid_column_menu(dialog,*state,lp))
                update_metadata_table(*state);
        } else {
            return DefSubclassProc(header, message, wp, lp);
        }
        return 0;
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(header, batch_header_proc, subclass_id);
    return DefSubclassProc(header, message, wp, lp);
}

void capture_resize_layout(PreviewState& state) {
    const HWND dialog = state.dialog;
    if (!dialog || !state.list) return;
    RECT client{}, window_rect{}, list_rect{};
    GetClientRect(dialog, &client);
    GetWindowRect(dialog, &window_rect);
    GetWindowRect(state.list, &list_rect);
    MapWindowPoints(HWND_DESKTOP, dialog,
                    reinterpret_cast<POINT*>(&list_rect), 2);
    state.initial_list_bottom = list_rect.bottom;
    state.initial_client_width = client.right;
    state.initial_client_height = client.bottom;
    state.initial_window_width = window_rect.right - window_rect.left;
    state.initial_window_height = window_rect.bottom - window_rect.top;
    state.initial_window_left = window_rect.left;
    state.initial_window_top = window_rect.top;
    state.resize_controls.clear();

    EnumChildWindows(dialog, [](HWND control, LPARAM state_ptr) -> BOOL {
        auto& current = *reinterpret_cast<PreviewState*>(state_ptr);
        if (GetParent(control) != current.dialog) return TRUE;
        NativePreviewResizeChild layout;
        layout.window = control;
        GetWindowRect(control, &layout.original);
        MapWindowPoints(HWND_DESKTOP, current.dialog,
                        reinterpret_cast<POINT*>(&layout.original), 2);
        const int id = GetDlgCtrlID(control);
        layout.stretch_width =
            id == IDC_BATCH_LIST || id == IDC_BATCH_HISTORY_LIST ||
            id == IDC_BATCH_TABS || id == IDC_BATCH_PROFILE_PICKER ||
            id == IDC_BATCH_DESTINATION || id == IDC_BATCH_PATTERN ||
            id == IDC_METADATA_MANUAL_INPUT ||
            (id == -1 && layout.original.right >
             current.initial_client_width - 24);
        layout.stretch_height = id == IDC_BATCH_LIST ||
                                id == IDC_BATCH_HISTORY_LIST ||
                                id == IDC_METADATA_LIST ||
                                id == IDC_METADATA_TRACK_LIST;
        layout.shift_down = id != IDC_BATCH_LIST &&
            id != IDC_BATCH_HISTORY_LIST && id != IDC_METADATA_LIST &&
            id != IDC_METADATA_TRACK_LIST &&
            id != IDC_BATCH_TABS &&
            layout.original.top >= current.initial_list_bottom;
        if (id == IDC_BATCH_GUIDE) layout.stretch_width=true;
        layout.shift_right = id == IDC_METADATA_INSPECT_CUE ||
                             id == IDC_METADATA_MB_LOAD_RELEASE ||
                             id == IDC_METADATA_IMPORT_CANDIDATE ||
                             id == IDC_METADATA_USE_VALUE ||
                             id == IDC_BATCH_PROFILE_SAVE ||
                             id == IDC_BATCH_PROFILE_DELETE ||
                             id == IDC_BATCH_PROFILE_COPY ||
                             id == IDC_BATCH_PROFILE_PASTE ||
                             id == IDC_BATCH_FILE_ACTION ||
                             id == IDC_BATCH_LOCATION_NOTE ||
                             id == IDC_BATCH_APPLY_SELECTED ||
                              id == IDC_BATCH_APPLY_VISIBLE ||
                              id == IDC_BATCH_APPLY_ALL || id == IDCANCEL;
        current.resize_controls.push_back(layout);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&state));
}

bool review_split_originals(const PreviewState& state, RECT& master, RECT& detail) {
    bool found_master=false, found_detail=false;
    for(const auto& child:state.resize_controls) {
        if(child.window==state.metadata_track_list) {
            master=child.original;
            found_master=true;
        }
        if(child.window==state.metadata_list) {
            detail=child.original;
            found_detail=true;
        }
    }
    return found_master && found_detail;
}

bool current_review_split_rects(const PreviewState& state,
                                RECT& master, RECT& detail) {
    if(!state.dialog || !state.metadata_track_list || !state.metadata_list ||
       !GetWindowRect(state.metadata_track_list,&master) ||
       !GetWindowRect(state.metadata_list,&detail)) return false;
    MapWindowPoints(HWND_DESKTOP,state.dialog,
                    reinterpret_cast<POINT*>(&master),2);
    MapWindowPoints(HWND_DESKTOP,state.dialog,
                    reinterpret_cast<POINT*>(&detail),2);
    return true;
}

void resize_batch_dialog(PreviewState& state, int width, int height) {
    if (state.resize_controls.empty() || width < 1 || height < 1) return;
    const int dx = width - state.initial_client_width;
    const int dy = height - state.initial_client_height;
    if (!apply_native_preview_resize(
            state.resize_controls, state.metadata_track_list, state.metadata_list,
            dx, dy, MulDiv(state.review_split_offset_96,state.active_dpi,96),
            MulDiv(160,state.active_dpi,96),
            MulDiv(180,state.active_dpi,96))) return;
    align_native_preview_form(state.dialog);
    layout_musicbrainz_detail_pane(state);
    if (state.musicbrainz_live_view)
        update_musicbrainz_browse_headers(state);
    // Repaint moved siblings as a single region. Do not force synchronous
    // painting for every WM_SIZE while the user is dragging the border.
    RedrawWindow(state.dialog, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
}

// Mouse, context menu and keyboard share the same clamped geometry and
// persisted 96-DPI offset. Only move HWNDs when a real visible change occurs.
bool apply_review_split_offset(PreviewState& state, int requested_96) {
    RECT original_master{},original_detail{},client{};
    if(!state.show_metadata ||
       !review_split_originals(state,original_master,original_detail) ||
       !GetClientRect(state.dialog,&client) || state.active_dpi<96)
        return false;
    const int dx=client.right-state.initial_client_width;
    const int dy=client.bottom-state.initial_client_height;
    const auto bounded=review_split_geometry(
        original_master,original_detail,dx,dy,
        MulDiv((std::clamp)(requested_96,-2000,2000),state.active_dpi,96),
        MulDiv(160,state.active_dpi,96),
        MulDiv(180,state.active_dpi,96));
    const int effective_px=bounded.master.right-
        (original_master.right+dx/2);
    const int effective_96=(std::clamp)(
        MulDiv(effective_px,96,state.active_dpi),-2000,2000);
    if(state.review_split_offset_96==effective_96) return false;
    state.review_split_offset_96=effective_96;
    resize_batch_dialog(state,client.right,client.bottom);
    return true;
}

void restore_batch_dialog_window_size(PreviewState& state) {
    if (!state.dialog || state.initial_window_width <= 0) return;
    const int dpi = (std::max)(96, state.active_dpi);
    const auto saved_placement=load_batch_preview_window_placement();
    const auto saved_size=load_batch_preview_window_size();
    if (!saved_placement && !saved_size) return;

    const int target_width = saved_placement
        ? MulDiv(saved_placement->width_at_96_dpi,dpi,96)
        : MulDiv(saved_size->width_at_96_dpi,dpi,96);
    const int target_height = saved_placement
        ? MulDiv(saved_placement->height_at_96_dpi,dpi,96)
        : MulDiv(saved_size->height_at_96_dpi,dpi,96);
    RECT proposed{};
    if (!GetWindowRect(state.dialog,&proposed)) return;
    int left=saved_placement
        ? MulDiv(saved_placement->left_at_96_dpi,dpi,96) : proposed.left;
    int top=saved_placement
        ? MulDiv(saved_placement->top_at_96_dpi,dpi,96) : proposed.top;

    RECT search_rect{left,top,left+target_width,top+target_height};
    HMONITOR monitor=MonitorFromRect(&search_rect,MONITOR_DEFAULTTONEAREST);
    MONITORINFO screen{};
    screen.cbSize=sizeof(screen);
    if (!monitor || !GetMonitorInfoW(monitor,&screen)) return;
    const bool workspace_coordinates=
        saved_placement && saved_placement->maximized;
    const auto clamped=djmeta::clamp_preview_restore_pixels(
        {left,top,target_width,target_height},
        state.initial_window_width,state.initial_window_height,
        {screen.rcMonitor.left,screen.rcMonitor.top,
         screen.rcWork.left,screen.rcWork.top,
         screen.rcWork.right-screen.rcWork.left,
         screen.rcWork.bottom-screen.rcWork.top},
        workspace_coordinates);
    if(!clamped) return;
    const RECT target{clamped->left,clamped->top,
                      clamped->left+clamped->width,
                      clamped->top+clamped->height};
    // This is the exact production HWND path tested by the Windows harness.
    // A maximized top-level dialog has workspace rcNormalPosition; normal
    // windows use actual screen rectangles. Do not interchange Win32 APIs.
    if(native_restore_saved_workspace_bounds(
           state.dialog,target,workspace_coordinates))
        state.last_nonminimized_maximized=workspace_coordinates;
}

void save_batch_dialog_window_size(const PreviewState& state) {
    if (!state.dialog || state.initial_window_width<=0 ||
        state.initial_window_height<=0) return;
    WINDOWPLACEMENT placement{};
    placement.length=sizeof(placement);
    if (!GetWindowPlacement(state.dialog,&placement))
        return;
    // For maximized dialogs rcNormalPosition holds the restored rectangle.
    // A normal window uses its actual desktop rect for correct screen position.
    RECT bounds=placement.rcNormalPosition;
    const bool iconic=IsIconic(state.dialog)!=FALSE;
    const bool maximized=native_workspace_saved_maximized(
        IsZoomed(state.dialog)!=FALSE,iconic,
        state.last_nonminimized_maximized);
    if(iconic && !maximized) {
        // GetWindowPlacement returns workspace coordinates for an iconic
        // window too. Keep the last real screen bounds captured on move/size.
        if(!state.has_last_normal_screen_bounds) return;
        bounds=state.last_normal_screen_bounds;
    } else if(!maximized &&
              !native_capture_normal_screen_bounds(state.dialog,bounds)) {
        return;
    }
    const int width=bounds.right-bounds.left;
    const int height=bounds.bottom-bounds.top;
    if (width<=0 || height<=0) return;
    const int dpi=(std::max)(96,state.active_dpi);
    djmeta::PreviewWindowPlacement saved{
        MulDiv(bounds.left,96,dpi), MulDiv(bounds.top,96,dpi),
        MulDiv(width,96,dpi), MulDiv(height,96,dpi), maximized};
    if (!djmeta::valid_preview_window_placement(saved)) return;
    if (!load_batch_preview_window_placement() && !maximized &&
        bounds.left==state.initial_window_left &&
        bounds.top==state.initial_window_top &&
        width==state.initial_window_width &&
        height==state.initial_window_height &&
        !load_batch_preview_window_size()) return;
    store_batch_preview_window_placement(saved);
}

void apply_review_action(HWND dialog, PreviewState& state,
                         djmeta::ReviewAction action) {
    const int scope = static_cast<int>(SendDlgItemMessageW(
        dialog, IDC_METADATA_REVIEW_SCOPE, CB_GETCURSEL, 0, 0));
    if (scope < 0 || scope > 5)
        throw std::invalid_argument("Choose a review scope.");

    // Resolve the one selected detail row to its immutable source/proposal
    // identity, not a sorted ListView coordinate or CUE virtual subsong.
    const auto selected_field_anchor=[&]() {
        const auto rows=selected_native_proposal_ids(
            state.metadata_list,state.metadata_view_order,
            state.focused_metadata_rows.size());
        if(rows.size()!=1)
            throw std::invalid_argument(
                "Select exactly one field proposal for field-scoped review.");
        const auto& row=state.focused_metadata_rows.at(rows.front());
        if(row.source_index>=state.analyses.size() ||
           row.proposal_index>=state.analyses[row.source_index].proposals.size() ||
           row.source_index>=state.entries.size() ||
           !state.entries[row.source_index].input.physical_source_qualified ||
           state.entries[row.source_index].handle->get_subsong_index()!=0 ||
           is_external_cue_locator(
               state.entries[row.source_index].input.source_path) ||
           state.entries[row.source_index].observed_physical_key.empty() ||
           state.entries[row.source_index].observed_source_guard.empty() ||
           row.field!=state.analyses[row.source_index]
                         .proposals[row.proposal_index].field)
            throw std::invalid_argument(
                "Selected field or physical source is stale or unqualified.");
        return std::pair<std::size_t,std::size_t>{
            row.source_index,row.proposal_index};
    };
    const auto field_scope_sources=[&](int requested_scope) {
        std::vector<std::size_t> ids;
        if(requested_scope==4) ids=state.track_view_order;
        else if(requested_scope==5) {
            ids.reserve(state.entries.size());
            for(std::size_t i=0;i<state.entries.size();++i) ids.push_back(i);
        } else throw std::invalid_argument("Unknown field review scope.");
        if(state.entries.size()!=state.analyses.size())
            throw std::invalid_argument("Field review batch snapshot stale.");
        std::vector<std::size_t> physical;
        physical.reserve(ids.size());
        for(const auto source:ids) {
            if(source>=state.entries.size())
                throw std::invalid_argument("Field review source identity stale.");
            const auto& entry=state.entries[source];
            if(entry.input.physical_source_qualified &&
               entry.handle->get_subsong_index()==0 &&
               !is_external_cue_locator(entry.input.source_path) &&
               !entry.observed_physical_key.empty() &&
               !entry.observed_source_guard.empty())
                physical.push_back(source);
        }
        return physical;
    };
    std::pair<std::size_t,std::size_t> field_anchor{};
    std::string field_name;
    std::vector<std::size_t> field_sources;

    // The selected identity always refers to analysis.proposals; neither
    // a sorted UI row nor a filtered field name is an edit target.
    std::set<std::pair<std::size_t, std::size_t>> targets;
    if (state.existing_tags_view)
        throw std::invalid_argument(
            "Original tag inventory is read-only. Select a proposal view first.");
    if (scope == 0) {
        for (const auto proposal_row : selected_native_proposal_ids(
                state.metadata_list, state.metadata_view_order,
                state.focused_metadata_rows.size())) {
            const auto& diff = state.focused_metadata_rows[proposal_row];
            targets.emplace(diff.source_index, diff.proposal_index);
        }
    } else if (scope == 1) {
        for (const auto track :
                selected_native_view_ids(state.metadata_track_list, state.track_view_order)) {
            if (track >= state.analyses.size()) continue;
            for (std::size_t j = 0; j < state.analyses[track].proposals.size(); ++j)
                targets.emplace(track, j);
        }
    } else if (scope == 2) {
        for (std::size_t track = 0; track < state.analyses.size(); ++track)
            for (std::size_t j = 0; j < state.analyses[track].proposals.size(); ++j)
                targets.emplace(track, j);
    } else if(scope==3) {
        // Filtered source IDs, not all selected tracks or visible row offsets.
        const auto visible=djmeta::visible_review_proposal_targets(
            state.analyses,state.track_view_order);
        targets.insert(visible.begin(),visible.end());
    } else {
        field_anchor=selected_field_anchor();
        field_name=state.analyses[field_anchor.first]
                        .proposals[field_anchor.second].field;
        field_sources=field_scope_sources(scope);
        const auto matching=djmeta::same_field_review_proposal_targets(
            state.analyses,field_sources,field_name);
        targets.insert(matching.begin(),matching.end());
    }
    if (targets.empty()) {
        MessageBoxW(dialog, L"Select changes or tracks with proposals first.",
                    L"Metadata Review", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Manual replacement is either one selected proposal, or one scalar
    // proposal of the SAME field per qualified physical source. Multivalue,
    // duplicate field names, virtual tracks and CUE remain out of scope.
    std::string manual_value;
    if(action==djmeta::ReviewAction::ManualValue) {
        if((scope!=0 && scope!=4 && scope!=5) ||
           (scope==0 && targets.size()!=1))
            throw std::invalid_argument(
                "Manual replacement requires one proposal or a physical same-field scope.");
        manual_value=to_utf8(read_control(dialog,IDC_METADATA_MANUAL_INPUT));
        if(!djmeta::valid_manual_review_value(manual_value) ||
           (scope>=4 && (manual_value.size()>512 ||
                          manual_value.find('\t')!=std::string::npos)))
            throw std::invalid_argument(
                "Enter a nonempty, single-line manual value (max 512 bytes for bulk).");
        if(scope>=4) {
            std::set<std::size_t> unique_sources;
            for(const auto& [source,proposal]:targets) {
                if(source>=state.entries.size() ||
                   !unique_sources.insert(source).second)
                    throw std::invalid_argument(
                        "Bulk Manual requires exactly one proposal per physical source.");
                const auto& entry=state.entries[source];
                verify_snapshot(entry);
                const auto original=metadata_from_file_info(
                    entry.handle->get_info_ref()->info());
                if(!djmeta::scalar_manual_review_target(
                       original,state.analyses[source],proposal,field_name))
                    throw std::invalid_argument(
                        "Bulk Manual excludes multivalue, duplicate, protected or stale fields.");
            }
        }
    }

    // Bulk scope is always default-No. A modal dialog pumps Windows messages:
    // validate the exact source/proposal set, selected field, rules and all
    // physical snapshots again before staging a single in-memory decision.
    if(scope>=3) {
        const std::vector<std::pair<std::size_t,std::size_t>> captured_targets{
            targets.begin(),targets.end()};
        const auto changed=action==djmeta::ReviewAction::ManualValue
            ? djmeta::count_manual_review_action_changes(
                captured_targets,state.review_decisions,manual_value)
            : djmeta::count_visible_review_action_changes(
                captured_targets,state.review_decisions,action);
        if(changed==0) return;
        const auto visible_sources=state.track_view_order;
        const auto filter=state.track_discovery;
        const auto focus=state.metadata_focus;
        const auto generation=state.workspace_generation;
        const auto selected_source=state.selected_track_index;
        const wchar_t* verb=action==djmeta::ReviewAction::Accept?L"ACCEPT":
            action==djmeta::ReviewAction::Reject?L"REJECT":
            action==djmeta::ReviewAction::Pending?L"RESET":L"MANUAL REPLACE";
        std::wstring confirmation;
        if(scope==3) {
            confirmation=L"Apply "+std::wstring(verb)+
                L" to all "+std::to_wstring(targets.size())+
                L" proposals in "+std::to_wstring(visible_sources.size())+
                L" currently visible tracks? "+std::to_wstring(changed)+
                L" decisions will actually change.\n\nThe Tracks filter selects sources; "+
                L"ALL proposal fields for those sources are included, even if "+
                L"the Music/Extended detail filter hides some.";
        } else {
            confirmation=L"Apply "+std::wstring(verb)+
                L" to the exact field '"+from_utf8(field_name)+L"' across "+
                std::to_wstring(field_sources.size())+
                L" qualified physical tracks ("+
                std::to_wstring(targets.size())+L" proposals, "+
                std::to_wstring(changed)+L" decisions changing)?\n\n"+
                (scope==4
                    ? L"Only the currently visible filtered source queue is in scope."
                    : L"ALL captured physical source tracks are in scope, including hidden rows.")+
                std::wstring(L"\nOnly this field (all its existing proposal values) is selected. ")+
                L"Other tags and CUE/virtual tracks are excluded.";
            if(action==djmeta::ReviewAction::ManualValue)
                confirmation+=L"\n\nManual replacement: \""+
                    from_utf8(manual_value)+
                    L"\"\nExactly one original scalar field value per physical source. "
                    L"Multivalues, duplicate-name fields and CUE are excluded.";
        }
        confirmation+=L"\n\nOnly in-memory review decisions and raw filename previews "+
                      std::wstring(L"change. No tags, CUE or audio files are written.");
        if(MessageBoxW(dialog,confirmation.c_str(),
                       L"Confirm bulk metadata review",
                       MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)
            return;
        if(!IsWindow(dialog) || g_workspace!=dialog ||
           g_workspace_generation!=generation ||
           GetWindowLongPtrW(dialog,GWLP_USERDATA)!=
               reinterpret_cast<LONG_PTR>(&state))
            return;
        if(state.stale || !state.show_metadata || state.show_candidate ||
           state.existing_tags_view ||
           state.track_discovery!=filter || state.metadata_focus!=focus ||
           state.selected_track_index!=selected_source ||
           state.track_view_order!=visible_sources ||
           SendDlgItemMessageW(dialog,IDC_METADATA_REVIEW_SCOPE,
                               CB_GETCURSEL,0,0)!=scope)
            throw std::invalid_argument(
                "Bulk review selection changed during confirmation. Retry.");
        std::vector<std::pair<std::size_t,std::size_t>> fresh_targets;
        if(scope==3) {
            fresh_targets=djmeta::visible_review_proposal_targets(
                state.analyses,state.track_view_order);
        } else {
            if(selected_field_anchor()!=field_anchor ||
               state.analyses[field_anchor.first]
                   .proposals[field_anchor.second].field!=field_name ||
               field_scope_sources(scope)!=field_sources)
                throw std::invalid_argument(
                    "Field review anchor or physical source scope changed.");
            fresh_targets=djmeta::same_field_review_proposal_targets(
                state.analyses,field_sources,field_name);
        }
        std::sort(fresh_targets.begin(),fresh_targets.end());
        if(fresh_targets!=captured_targets ||
           (action==djmeta::ReviewAction::ManualValue
               ? djmeta::count_manual_review_action_changes(
                   captured_targets,state.review_decisions,manual_value)
               : djmeta::count_visible_review_action_changes(
                   captured_targets,state.review_decisions,action))!=changed ||
           (action==djmeta::ReviewAction::ManualValue &&
            to_utf8(read_control(dialog,IDC_METADATA_MANUAL_INPUT))!=manual_value))
            throw std::invalid_argument(
                "Bulk review proposal identities, manual value or decisions changed.");
        for(const auto& entry:state.entries) verify_snapshot(entry);
        if(action==djmeta::ReviewAction::ManualValue) {
            for(const auto& [source,proposal]:captured_targets) {
                const auto original=metadata_from_file_info(
                    state.entries.at(source).handle->get_info_ref()->info());
                if(!djmeta::scalar_manual_review_target(
                       original,state.analyses.at(source),proposal,field_name))
                    throw std::invalid_argument(
                        "Bulk Manual field changed during confirmation.");
            }
        }
        verify_rules_snapshot(state.captured_rules);
    }

    // Stage only touched tracks. Deep-copying all captured metadata and
    // decisions for each Accept/Reject makes 50k+ track reviews O(library
    // size) in allocations even when changing a single field.
    std::map<std::size_t,std::vector<djmeta::ReviewDecision>> next_decisions;
    std::map<std::size_t,PreviewEntry> next_entries;
    std::set<std::size_t> affected_tracks;
    std::size_t changed_decisions=0;
    // Recalculate the currently selected online source's advisory raw path
    // together with a local decision. Never publish a route computed from
    // yesterday's accepted values or a half-applied batch.
    bool reviewed_online_route=false;
    std::optional<std::pair<std::string,std::string>> refreshed_online_route;
    for (const auto& target : targets) {
        const auto track = target.first;
        const auto proposal = target.second;
        if (track >= state.review_decisions.size() ||
            proposal >= state.review_decisions[track].size() ||
            track >= state.entries.size())
            throw std::invalid_argument("Selection no longer matches captured proposals.");
        const auto& previous=state.review_decisions[track][proposal];
        if(previous.action==action &&
           (action!=djmeta::ReviewAction::ManualValue ||
            previous.manual_value==manual_value))
            continue; // repeated Accept/Reject/Reset is a true preview no-op
        auto [it, inserted] = next_decisions.try_emplace(
            track,state.review_decisions[track]);
        it->second[proposal] = {action, manual_value};
        affected_tracks.insert(track);
        ++changed_decisions;
    }

    // A repeated identical decision is not a new action. Preserve the last
    // reversible preview state, layout, and costly 54k-row update.
    if(affected_tracks.empty()) return;
    TitleformatBatchEvaluator formatter;
    for (const auto track : affected_tracks) {
        auto [it, inserted] = next_entries.try_emplace(
            track,state.entries[track]);
        auto& entry = it->second;
        verify_snapshot(entry);
        const auto current = entry.handle->get_info_ref();
        const auto original = metadata_from_file_info(current->info());
        auto staged = djmeta::project_explicit_review_decisions(
            original, state.analyses[track], next_decisions[track]);
        // Unlike the visible SAFE-inclusive normalization preview, a
        // *potential physical write* must contain ONLY explicitly Accept or
        // ManualValue decisions. Recheck it against the actual physical
        // carrier and fresh source guard before displaying the new decision.
        // No SDK tag writer is called here; virtual/CUE selections remain
        // independently read-only.
        if(entry.input.physical_source_qualified) {
            bool selected_postimage_changed=false;
            djmeta::PhysicalTagSourceSnapshot physical;
            physical.qualified_physical=true;
            physical.host_physical_key=entry.observed_physical_key;
            physical.source_guard=entry.observed_source_guard;
            physical.original=original;
            const auto verified=djmeta::preview_explicit_normalizer_physical_tags(
                physical,original,entry.observed_physical_key,
                entry.observed_source_guard,
                state.analyses[track],state.analyses[track].ruleset_revision,
                next_decisions[track]);
            if(!verified.eligible)
                throw std::invalid_argument(
                    "Physical tag decisions rejected: "+verified.rejection);
            selected_postimage_changed=verified.changed;
            // An online selection already visible for THIS physical source
            // must remain compatible with the new local Accept/Manual choice.
            // Re-stage both independent immutable evidence streams before
            // publishing any new local decision; never prefer one silently.
            if(native_recheck_preserved_online_selection(
                   state.online_field_snapshot.has_value(),
                   state.online_edition_source,track,
                   state.online_included_fields.size())) {
                djmeta::UnifiedReviewBatchReprobe unified;
                unified.normalizer.live.captured=physical;
                unified.normalizer.live.fresh_tags=original;
                unified.normalizer.live.live_physical_key=
                    entry.observed_physical_key;
                unified.normalizer.live.live_source_guard=
                    entry.observed_source_guard;
                unified.normalizer.captured_analysis=state.analyses[track];
                unified.normalizer.active_ruleset_revision=
                    state.analyses[track].ruleset_revision;
                unified.normalizer.decisions=next_decisions[track];
                unified.online_capture=*state.online_field_snapshot;
                unified.selected_online_indices=state.online_included_fields;
                unified.explicit_date_targets=state.online_date_targets;
                const auto joint=djmeta::preview_unified_review_physical_tags(
                    unified);
                if(!joint.physical.eligible)
                    throw std::invalid_argument(
                        "Local change conflicts with selected online fields. "
                        "Review/ignore one source first: "+joint.rejection);
                reviewed_online_route=true;
                selected_postimage_changed=joint.physical.changed;
                // The current route belongs to this exact source only if its
                // per-track override agrees with the global native controls.
                // Otherwise clear the old raw preview; never claim it applies.
                try {
                    const auto choice=read_choice(dialog);
                    if(choice.display_name==state.current_choice.display_name &&
                       choice.destination_root==state.current_choice.destination_root &&
                       choice.titleformat_expression==
                           state.current_choice.titleformat_expression &&
                       entry.input.profile==choice.display_name &&
                       entry.input.destination_root==choice.destination_root &&
                       entry.route_expression==choice.titleformat_expression &&
                       djmeta::raw_destination_root_lexically_safe(
                           choice.destination_root)) {
                        const auto before=evaluate_titleformat_against_canonical(
                            entry.handle->get_location(),current->info(),
                            original,choice.titleformat_expression);
                        const auto after=evaluate_titleformat_against_canonical(
                            entry.handle->get_location(),current->info(),
                            joint.physical.planned,choice.titleformat_expression);
                        if(before.size()<=2048 && after.size()<=2048 &&
                           djmeta::raw_relative_path_lexically_safe(before) &&
                           djmeta::raw_relative_path_lexically_safe(after))
                            refreshed_online_route=std::make_pair(before,after);
                    }
                } catch(const std::exception&) {
                    // Only the advisory filename is unqualified; the
                    // independently verified tag selection stays available.
                }
            }
            // Reset/Reject/Pending and accepted exact no-ops MUST NOT issue
            // thousands of OS probes in a 54k-track library. There is no
            // candidate physical mutation for these decisions.
            if(selected_postimage_changed) {
                const auto live=probe_host_file_readonly(entry.input.source_path);
                if(live.state!=HostFileState::ExistingFile ||
                   live.host_physical_key!=entry.observed_physical_key ||
                   live.source_guard!=entry.observed_source_guard)
                    throw std::runtime_error(
                        "Physical file changed during metadata review. Refresh snapshot.");
            }
            // A changed detached postimage is NOT authorization to write.
        }
        entry.staged = std::move(staged.document);
        entry.input.semantic_proposals_pending = staged.unresolved_semantic > 0;
        entry.input.raw_relative_path.clear();
        reset_raw_target_observation(entry);
        // Any decision invalidates prior filesystem/CUE preflight, even if a
        // user picks the original value. No file or tag writer is invoked.
        entry.input.filesystem_target_checked = false;
        entry.input.cue_dependencies_checked = false;
        if (entry.input.physical_source_qualified) {
            entry.input.raw_relative_path = formatter.evaluate(
                entry.handle->get_location(), current->info(),
                entry.staged, entry.route_expression);
        }
    }
    // Transactional whole-batch stale-input gate before publishing preview.
    for (const auto& entry : state.entries) verify_snapshot(entry);
    verify_rules_snapshot(state.captured_rules);

    // Every affected row and the entire source/ruleset snapshot have passed.
    // Commit only touched identities. No public writer or filesystem action.
    for (auto& [track, entry] : next_entries) {
        using std::swap;
        swap(state.entries[track],entry);
        swap(state.review_decisions[track],next_decisions.at(track));
    }
    if(next_entries.size()<=kMaxPreviewUndoSources) {
        PreviewUndoFrame frame;
        frame.kind=PreviewUndoFrame::Kind::Review;
        frame.previous_entries=std::move(next_entries);
        frame.previous_decisions=std::move(next_decisions);
        state.last_preview_undo=std::move(frame);
    } else state.last_preview_undo.reset();
    EnableWindow(GetDlgItem(dialog,IDC_BATCH_UNDO),
                 state.last_preview_undo ? TRUE : FALSE);
    if(reviewed_online_route) {
        state.online_raw_target_observation.reset();
        // Every affected source was validated before the local batch was
        // committed. A stale or per-track overridden route is never shown.
        state.online_raw_route_valid=refreshed_online_route.has_value();
        state.online_raw_route_before=refreshed_online_route
            ? std::move(refreshed_online_route->first):std::string{};
        state.online_raw_route_after=refreshed_online_route
            ? std::move(refreshed_online_route->second):std::string{};
    }
    // Source metadata and proposal types are snapshot-immutable. A decision
    // changes only the touched tracks' pending/staged/REVIEW queue counts.
    update_pending_review_summaries(state,affected_tracks);
    update_table(state);
    update_master_table(state);
    update_metadata_table(state);
    if (action == djmeta::ReviewAction::ManualValue)
        SetDlgItemTextW(dialog, IDC_METADATA_MANUAL_INPUT, L"");
    const auto kind=action==djmeta::ReviewAction::Accept
        ? PreviewHistoryKind::Accept
        : action==djmeta::ReviewAction::Reject
        ? PreviewHistoryKind::Reject
        : action==djmeta::ReviewAction::ManualValue
        ? PreviewHistoryKind::Manual : PreviewHistoryKind::Reset;
    record_preview_history(state,kind,affected_tracks.size(),
        changed_decisions,static_cast<unsigned>(scope),
        affected_tracks.size()==1 ? *affected_tracks.begin() : static_cast<std::size_t>(-1));
}

void revalidate_workspace(PreviewState& state,bool full_source_check);
void apply_to_rows(HWND dialog, PreviewState& state, NativeRoutePreviewScope scope) {
    if(state.stale || state.refreshing || TabCtrl_GetCurSel(state.tabs)!=2)
        throw std::invalid_argument("File locations requires a fresh source snapshot.");
    verify_rules_snapshot(state.captured_rules);
    const auto choice=read_choice(dialog);
    const auto action=read_file_action(dialog);
    const auto selected_rows=[&]() {
        std::vector<std::size_t> rows;
        if(scope==NativeRoutePreviewScope::Selected) {
            int row=-1;
            while((row=ListView_GetNextItem(state.list,row,LVNI_SELECTED))>=0)
                rows.push_back(static_cast<std::size_t>(row));
        }
        return rows;
    };
    const auto selected=native_route_preview_source_ids(
        scope,state.entries.size(),state.view_order,selected_rows());
    revalidate_workspace(state,true);
    if(state.stale) throw std::runtime_error("Source changed; refresh preview.");
    std::map<std::size_t,PreviewEntry> candidate;
    TitleformatBatchEvaluator formatter;
    for(auto index:selected) {
        const auto& old=state.entries.at(index);
        verify_snapshot(old);
        PreviewEntry next=old;
        next.file_action=action;
        next.file_action_explicitly_staged=true;
        next.input.profile=choice.display_name;
        next.input.destination_root=choice.destination_root;
        next.route_expression=choice.titleformat_expression;
        next.input.raw_relative_path.clear();
        if(next.input.physical_source_qualified) {
            const auto current=next.handle->get_info_ref();
            next.input.raw_relative_path=formatter.evaluate(
                next.handle->get_location(),current->info(),next.staged,
                choice.titleformat_expression);
        }
        if(next.file_action==old.file_action &&
           next.file_action_explicitly_staged==old.file_action_explicitly_staged &&
           next.input.profile==old.input.profile &&
           next.input.destination_root==old.input.destination_root &&
           next.route_expression==old.route_expression &&
           next.input.raw_relative_path==old.input.raw_relative_path)
            continue; // exact no-op: keep target evidence, undo and history
        reset_raw_target_observation(next);
        next.input.filesystem_target_checked=false;
        next.input.cue_dependencies_checked=false;
        candidate.emplace(index,std::move(next));
    }
    verify_rules_snapshot(state.captured_rules);
    if(candidate.empty()) {
        SetDlgItemTextW(dialog,IDC_BATCH_HINT,
            L"Identical route preview. No history, undo or file writes.");
        return;
    }
    // Single-source previews are read-only and need no redundant modal
    // consent. Multi-source actions always disclose their exact scope.
    if(selected.size()>1) {
        const wchar_t* name=scope==NativeRoutePreviewScope::Visible
            ? L"VISIBLE filtered queue (hidden rows excluded)"
            : scope==NativeRoutePreviewScope::All
            ? L"ALL captured tracks (including hidden rows)"
            : L"selected visible rows";
        const std::wstring scope_confirmation=std::wstring(L"Preview routing for ")+name+
            L"?\n\nSources: "+std::to_wstring(selected.size())+
            L"\nChosen file action: "+from_utf8(djmeta::file_action_label(action))+
            L" (read-only intent)"+
            L"\nChanged routes/intents: "+std::to_wstring(candidate.size())+
            L"\nExact no-ops: "+std::to_wstring(selected.size()-candidate.size())+
            L"\n\nOnly in-memory RAW paths and operation intentions change. Final foobar File Operations "
            L"destinations, CUE references and overwrites remain UNQUALIFIED. "
            L"No music files or tags are written.";
        if(MessageBoxW(dialog,scope_confirmation.c_str(),L"Confirm route preview scope",
                       MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)
            return;
    }
    if(!IsWindow(dialog) || g_workspace!=dialog ||
       state.workspace_generation!=g_workspace_generation ||
       GetWindowLongPtrW(dialog,GWLP_USERDATA)!=
           reinterpret_cast<LONG_PTR>(&state) ||
       TabCtrl_GetCurSel(state.tabs)!=2) return;
    revalidate_workspace(state,true);
    if(state.stale) throw std::runtime_error("Source changed during confirmation.");
    const auto now=read_choice(dialog);
    if(now.display_name!=choice.display_name ||
       now.destination_root!=choice.destination_root ||
       now.titleformat_expression!=choice.titleformat_expression ||
       read_file_action(dialog)!=action ||
       native_route_preview_source_ids(
           scope,state.entries.size(),state.view_order,selected_rows())!=selected)
        throw std::runtime_error("Route scope or settings changed during confirmation.");
    verify_rules_snapshot(state.captured_rules);
    const auto changed=candidate.size();
    const auto single=changed==1 ? candidate.begin()->first
        : static_cast<std::size_t>(-1);
    for(auto& [index,next]:candidate) {
        using std::swap;
        swap(state.entries[index],next);
    }
    if(candidate.size()<=kMaxPreviewUndoSources) {
        PreviewUndoFrame frame;
        frame.kind=PreviewUndoFrame::Kind::Route;
        frame.previous_entries=std::move(candidate);
        frame.previous_choice=state.current_choice;
        state.last_preview_undo=std::move(frame);
    } else state.last_preview_undo.reset();
    state.current_choice=choice;
    state.online_raw_route_valid=false;
    state.online_raw_route_before.clear();
    state.online_raw_route_after.clear();
    state.online_raw_target_observation.reset();
    EnableWindow(GetDlgItem(dialog,IDC_BATCH_UNDO),
                 state.last_preview_undo ? TRUE : FALSE);
    update_table(state); // collision scan always covers the entire batch
    record_preview_history(state,PreviewHistoryKind::Route,
        changed,changed,scope==NativeRoutePreviewScope::Visible ? 3u :
        scope==NativeRoutePreviewScope::All ? 2u : 1u,single);
    const std::wstring status=L"Read-only route preview: "+
        std::to_wstring(changed)+L" changed of "+
        std::to_wstring(selected.size())+
        L" scoped tracks. Full-batch collision checks retained; "
        L"final FileOps/CUE not qualified. No writes.";
    SetDlgItemTextW(dialog,IDC_BATCH_HINT,status.c_str());
}

void undo_last_preview_action(HWND dialog, PreviewState& state) {
    if(!state.last_preview_undo || state.show_candidate)
        throw std::invalid_argument("No local preview change is available to undo.");
    // Undo is a reversible in-memory view action, not a source writer.
    // The command dispatcher also performs full source/physical checks.
    for(const auto& entry:state.entries) verify_snapshot(entry);
    verify_rules_snapshot(state.captured_rules);
    const auto& saved=*state.last_preview_undo;
    if(saved.previous_entries.empty() ||
       saved.previous_entries.size()>kMaxPreviewUndoSources)
        throw std::invalid_argument("Preview undo snapshot is invalid.");
    for(const auto& [index, before]:saved.previous_entries) {
        if(index>=state.entries.size() ||
           before.input.source_path!=state.entries[index].input.source_path ||
           before.input_fingerprint!=state.entries[index].input_fingerprint)
            throw std::invalid_argument("Preview undo source identity is stale.");
        if(saved.kind==PreviewUndoFrame::Kind::Review &&
           (index>=state.review_decisions.size() ||
            !saved.previous_decisions.contains(index) ||
            saved.previous_decisions.at(index).size()!=
                state.review_decisions[index].size()))
            throw std::invalid_argument("Preview undo decisions are stale.");
    }
    auto rollback=std::move(*state.last_preview_undo);
    state.last_preview_undo.reset();
    const bool review=rollback.kind==PreviewUndoFrame::Kind::Review;
    std::set<std::size_t> affected;
    for(auto& [index, previous]:rollback.previous_entries) {
        using std::swap;
        swap(state.entries[index],previous);
        // A restored older preview is NOT fresh evidence for an existing
        // destination, CUE dependency or overwrite. Keep the user's raw
        // route and choices, but force all external target checks to be
        // repeated before any later approval or operation.
        reset_raw_target_observation(state.entries[index]);
        state.entries[index].input.cue_dependencies_checked=false;
        affected.insert(index);
        if(review)
            swap(state.review_decisions[index],
                 rollback.previous_decisions.at(index));
    }
    if(!review) {
        state.current_choice=std::move(rollback.previous_choice);
        SetDlgItemTextW(dialog,IDC_BATCH_PROFILE_PICKER,
                        from_utf8(state.current_choice.display_name).c_str());
        SetDlgItemTextW(dialog,IDC_BATCH_DESTINATION,
                        from_utf8(state.current_choice.destination_root).c_str());
        SetDlgItemTextW(dialog,IDC_BATCH_PATTERN,
                        from_utf8(state.current_choice.titleformat_expression).c_str());
    } else {
        update_pending_review_summaries(state,affected);
    }
    // Undo can restore a different local TITLE, GENRE, DATE or route.
    // Keep the verified online fields, but never retain a previous filename.
    state.online_raw_route_valid=false;
    state.online_raw_route_before.clear();
    state.online_raw_route_after.clear();
    state.online_raw_target_observation.reset();
    EnableWindow(GetDlgItem(dialog,IDC_BATCH_UNDO),FALSE);
    update_table(state);
    if(review) {
        update_master_table(state);
        update_metadata_table(state);
    }
    record_preview_history(state,review?PreviewHistoryKind::UndoReview:
        PreviewHistoryKind::UndoRoute,affected.size(),affected.size(),0,
        affected.size()==1 ? *affected.begin() : static_cast<std::size_t>(-1));
}

// User-initiated inspection of RAW, unsanitized candidate paths only. No
// host File Operations resolution, no write/overwrite approval, no CUE gate.
// Scope deliberately follows selected underlying row IDs after sorting.
// One deliberate, bounded read-only FILE reference inventory for the
// selected external CUE. This is independent of target routing and never
// sets filesystem_target_checked or cue_dependencies_checked; real File
// Operations path sanitation and write safety are NOT certified here.
void append_external_cue_file_reference_evidence(
    PreviewState& state, const PreviewEntry& entry,
    const djmeta::CueMetadataInventory& inventory,
    std::vector<djmeta::online::FieldReviewRow>& rows) {
    if (!is_external_cue_locator(entry.input.source_path)) return;
    const auto original = probe_host_file_readonly(entry.input.source_path);
    if (original.state != HostFileState::ExistingFile ||
        original.source_guard.empty() ||
        original.source_guard != entry.observed_source_guard ||
        original.host_physical_key != entry.observed_physical_key)
        throw std::runtime_error(
            "External CUE physical source changed before FILE-reference inspection.");
    constexpr std::size_t maximum_probes=128;
    std::map<std::string, HostFileObservation> probes;
    std::map<std::string, HostDestinationParentChain> parent_probes;
    std::set<std::string> observed_ids;
    std::vector<djmeta::CueObservedFileSource> file_sources;
    std::vector<std::size_t> appended_file_rows;
    file_sources.reserve((std::min)(inventory.files.size(),maximum_probes));
    auto append = [&](std::size_t index, std::string filename,
                      std::string result) {
        if (rows.size() >= 10000)
            throw std::invalid_argument("Too many original CUE rows to show source checks.");
        djmeta::online::FieldReviewRow row;
        row.field = "FILE #" + std::to_string(index+1) +
            " / Physical source preflight";
        row.original_values.push_back(std::move(filename));
        row.candidate.field = "Read-only FILE dependency (not an approved move)";
        row.candidate.provider = "Local filesystem (read-only)";
        row.candidate.scope = djmeta::online::EvidenceScope::Edition;
        row.candidate.source_id = "cue-file:" + std::to_string(index+1);
        row.candidate.values.push_back(std::move(result));
        row.state = djmeta::online::FieldReviewState::Blocked;
        row.reason = "cue_reference_preflight_only";
        appended_file_rows.push_back(rows.size());
        rows.push_back(std::move(row));
    };
    for (std::size_t i=0;i<inventory.files.size();++i) {
        const auto& item=inventory.files[i];
        if (i>=maximum_probes) {
            if (i==maximum_probes) {
                // Force shape rejection in the shared pure matcher. Never
                // claim full coverage from a truncated 128-FILE prefix.
                file_sources.push_back({i,{}, {},false});
                append(i,"(further FILE references)",
                       "NOT INSPECTED: over 128 FILE references; operation blocked");
            }
            break;
        }
        const auto candidate = djmeta::preview_cue_sibling_locator(
            entry.input.source_path,item.filename);
        if (!candidate.eligible) {
            file_sources.push_back({i,{}, {},false});
            append(i,item.filename,"NOT QUALIFIED: "+candidate.reason);
            continue;
        }
        const auto [parent_it,parent_inserted]=parent_probes.try_emplace(
            candidate.source_candidate);
        if(parent_inserted)
            parent_it->second=probe_raw_destination_parent_chain_readonly(
                candidate.source_candidate,candidate.source_parent);
        const auto& parents=parent_it->second;
        // Existing CUE directory must be the EXACT physically observed root
        // of the FILE reference. A nested junction, absent directory or
        // out-of-tree alias cannot become a qualified audio dependency.
        const bool parent_qualified=parents.qualified &&
            std::all_of(parents.ancestors.begin(),parents.ancestors.end(),
                [](const HostDestinationAncestor& a) {return a.existing;});
        if(!parent_qualified) {
            file_sources.push_back({i,{}, {},false});
            append(i,item.filename,"NOT QUALIFIED: "+
                (parents.qualified?std::string("CUE_FILE_PARENT_MISSING"):
                                   parents.rejection));
            continue;
        }
        const auto [it, inserted] = probes.try_emplace(
            candidate.source_candidate);
        if (inserted)
            it->second = probe_host_file_readonly(candidate.source_candidate);
        const auto& observed=it->second;
        file_sources.push_back({
            i,observed.host_physical_key,observed.source_guard,
            observed.state==HostFileState::ExistingFile,
            observed.canonical_path});
        if (observed.state == HostFileState::ExistingFile &&
            !observed.host_physical_key.empty() &&
            !observed.source_guard.empty()) {
            if (observed.host_physical_key == original.host_physical_key)
                append(i,item.filename,
                       "NOT QUALIFIED: FILE resolves to the CUE carrier itself");
            else if (!observed_ids.insert(observed.host_physical_key).second)
                append(i,item.filename,
                       "ALIAS: physical audio already referenced; operation unqualified");
            else
                append(i,item.filename,
                       "PRESENT: independently observed physical file; read-only");
        } else if (observed.state==HostFileState::Missing) {
            append(i,item.filename,"MISSING: CUE FILE source absent");
        } else {
            append(i,item.filename,
                   "NOT QUALIFIED: source identity or filesystem unsupported");
        }
    }
    // The inventory can involve many FILE entries and slow network storage.
    // Before publishing any PRESENT/MISSING/ALIAS labels, re-observe EVERY
    // distinct physical reference. A CUE could remain byte-identical while
    // one of its audio dependencies is replaced, created or modified.
    // A stale or unsupported re-probe invalidates the entire preview.
    for (const auto& [candidate_path, earlier] : probes) {
        const HostFileObservation latest =
            probe_host_file_readonly(candidate_path);
        if (!stable_raw_target_observation(earlier, latest))
            throw std::runtime_error(
                "External CUE FILE dependency changed during read-only inspection. Refresh snapshot.");
    }
    for(const auto& [candidate_path,earlier]:parent_probes) {
        const auto latest=probe_raw_destination_parent_chain_readonly(
            candidate_path,
            // The exact CUE parent is derived from the inspected carrier,
            // never from a destination FileOps profile.
            entry.input.source_path.substr(
                0,entry.input.source_path.find_last_of("/\\")+1));
        if(!stable_raw_destination_parent_chain(earlier,latest))
            throw std::runtime_error(
                "External CUE FILE parent directory or junction changed during inspection.");
    }
    const auto current = probe_host_file_readonly(entry.input.source_path);
    if (current.state != HostFileState::ExistingFile ||
        current.host_physical_key != original.host_physical_key ||
        current.source_guard != original.source_guard)
        throw std::runtime_error(
            "External CUE changed during FILE-reference inspection.");

    // Match physical FILE source identities against ALL separately selected
    // top-level audio files, independent of how foobar displays/sorts rows.
    // Selected source IDs are never inferred from raw FILE names. Unsupported
    // virtual and ambiguous audio selections MUST be reported as blocked.
    std::vector<djmeta::CueSelectedPhysicalSource> selected_audio;
    selected_audio.reserve(state.entries.size());
    for(std::size_t index=0;index<state.entries.size();++index) {
        const auto& selected=state.entries[index];
        if(is_external_cue_locator(selected.input.source_path) ||
           selected.handle->get_subsong_index()!=0)
            continue;
        selected_audio.push_back({index,
            selected.observed_physical_key,selected.observed_source_guard,
            selected.input.physical_source_qualified});
    }
    const auto matched=djmeta::audit_cue_physical_selection(
        original.host_physical_key,file_sources,selected_audio);

    // Recheck the matching selected native file_info metadata snapshot after
    // slow host FILE observations. A matching physical file ID but changed
    // metadata/guard must never impersonate a selected review source.
    for(const auto index:matched.file_to_selected_row)
        if(index!=djmeta::cue_unselected_source_row) {
            const auto& selected=state.entries.at(index);
            verify_snapshot(selected);
            const auto live=probe_host_file_readonly(selected.input.source_path);
            if(live.state!=HostFileState::ExistingFile ||
               live.host_physical_key!=selected.observed_physical_key ||
               live.source_guard!=selected.observed_source_guard)
                throw std::runtime_error(
                    "Selected audio source changed during CUE FILE inspection.");
        }
    verify_rules_snapshot(state.captured_rules);

    // All presented rows are BLOCKED candidates, even when source identity
    // matches. The final foobar-sanitized destination and CUE relative FILE
    // postimage remain completely unchecked and cannot be approved here.
    if(matched.eligible &&
       matched.file_to_selected_row.size()==file_sources.size())
        for(std::size_t i=0;i<file_sources.size();++i) {
            const auto selected=matched.file_to_selected_row[i];
            if(selected!=djmeta::cue_unselected_source_row)
                rows.at(appended_file_rows[i]).candidate.values.front()+=
                    "; MATCHED selected physical audio row #"+
                    std::to_string(selected+1)+" (source only)";
        }
    // Show which *selected physical audio* a verified FILE depends on AND
    // what the currently staged Title Formatting would propose. This is a
    // user-visible impact assessment, not a new filename engine, and no RAW
    // spelling is ever promoted to a final foobar FileOps/CUE destination.
    std::vector<djmeta::CueRawRouteTrack> route_tracks(state.entries.size());
    for(std::size_t i=0;i<state.entries.size();++i) {
        const auto& source=state.entries[i];
        auto& target=route_tracks[i];
        target.host_physical_key=source.observed_physical_key;
        target.source_guard=source.observed_source_guard;
        target.file_action=source.file_action;
        target.qualified_physical=source.input.physical_source_qualified &&
            source.handle->get_subsong_index()==0 &&
            !is_external_cue_locator(source.input.source_path);
        target.destination_root=source.input.destination_root;
        const bool online=native_recheck_preserved_online_selection(
            state.online_field_snapshot.has_value(),
            state.online_edition_source,i,state.online_included_fields.size());
        target.raw_relative_path=online
            ? (state.online_raw_route_valid ? state.online_raw_route_after
                                             : std::string{})
            : source.input.raw_relative_path;
        target.pending_metadata_review=source.input.semantic_proposals_pending ||
            (online && !state.online_raw_route_valid);
        if(i<state.table.rows.size())
            target.raw_batch_collision=state.table.rows[i].has_raw_target_collision;
    }
    const auto impact=djmeta::inspect_cue_raw_route_impact(
        file_sources,matched,route_tracks);
    std::size_t shown=0;
    for(const auto& route:impact.references) {
        if(shown>=appended_file_rows.size()) break;
        if(rows.size()>=10000)
            throw std::invalid_argument("CUE RAW routing impact display limit reached.");
        djmeta::online::FieldReviewRow row;
        row.field="FILE #"+std::to_string(route.reference_index+1)+
            " / RAW routing impact";
        row.original_values=rows.at(appended_file_rows[shown]).original_values;
        row.candidate.field="Staged audio routing (NOT final FileOps/CUE)";
        row.candidate.provider="Local foobar Title Formatting (read-only)";
        row.candidate.scope=djmeta::online::EvidenceScope::Edition;
        row.candidate.source_id=route.selected_source_row==
            djmeta::cue_unselected_source_row
            ? "cue-raw-route:"+std::to_string(shown+1)
            : djmeta::cue_file_navigation_id(
                route.reference_index,route.selected_source_row);
        std::string explanation=std::string(
            djmeta::describe_cue_raw_route_code(route.code))+
            " ["+route.code+"]";
        if(route.selected_source_row!=djmeta::cue_unselected_source_row)
            explanation+="; selected physical row #"+
                std::to_string(route.selected_source_row+1)+
                "; intent: "+std::string(djmeta::file_action_label(
                    route.requested_action));
        if(!route.raw_candidate.empty())
            explanation+="; RAW ONLY: "+route.raw_candidate;
        explanation+="; final FileOps target / relative CUE FILE unchecked";
        if(route.selected_source_row!=djmeta::cue_unselected_source_row)
            explanation+="; double-click or press Enter to inspect that physical source in File locations";
        row.candidate.values.push_back(std::move(explanation));
        row.state=djmeta::online::FieldReviewState::Blocked;
        row.reason="cue_raw_routing_impact_not_final_approval";
        rows.push_back(std::move(row));
        ++shown;
    }
    if(rows.size()>=10000)
        throw std::invalid_argument("CUE RAW impact summary display limit reached.");
    djmeta::online::FieldReviewRow impact_summary;
    impact_summary.field="CUE / RAW relocation impact (entire captured batch)";
    impact_summary.candidate.field="Source-linked preview; no FileOps approval";
    impact_summary.candidate.provider="Local foobar Title Formatting (read-only)";
    impact_summary.candidate.scope=djmeta::online::EvidenceScope::Edition;
    impact_summary.candidate.source_id="cue-raw-route-summary";
    impact_summary.candidate.values.push_back(
        "Move "+std::to_string(impact.move_raw_candidates)+
        " RAW-only (CUE rewrite needed), Copy "+
        std::to_string(impact.copy_raw_candidates)+
        " RAW-only (original FILE unchanged), No file action "+
        std::to_string(impact.kept_in_place)+
        ", Rename blocked "+std::to_string(impact.rename_blocked)+
        ", partial-graph Move blocked "+std::to_string(impact.partial_move_blocked)+
        ", other blocked "+std::to_string(
            impact.selected_blocked-impact.rename_blocked-impact.partial_move_blocked)+
        ", unselected FILE dependencies "+std::to_string(impact.unselected)+
        "; actual foobar FileOps targets/CUE postimages/writes NOT qualified");
    impact_summary.state=djmeta::online::FieldReviewState::Blocked;
    impact_summary.reason="cue_raw_routing_impact_not_final_approval";
    rows.push_back(std::move(impact_summary));

    // Reuse the file-action staging of the external CUE carrier itself.
    // The default Move template is NOT an explicit CUE sidecar choice;
    // a user must preview/stage that physical carrier in File locations.
    if(rows.size()>=10000)
        throw std::invalid_argument("CUE carrier action display limit reached.");
    const bool qualified_carrier=entry.input.physical_source_qualified &&
        !entry.observed_physical_key.empty() &&
        !entry.observed_source_guard.empty();
    const auto sidecar=djmeta::inspect_cue_sidecar_raw_action(
        impact,entry.file_action,entry.file_action_explicitly_staged,
        qualified_carrier,entry.input.semantic_proposals_pending,
        state.table.rows.size()>state.selected_track_index &&
        state.table.rows[state.selected_track_index].has_raw_target_collision,
        entry.input.destination_root,entry.input.raw_relative_path);
    djmeta::online::FieldReviewRow sidecar_row;
    sidecar_row.field="CUE carrier / Explicit sidecar operation";
    sidecar_row.candidate.field="Staged carrier and FILE consequences (RAW ONLY)";
    sidecar_row.candidate.provider="Local foobar File locations (read-only)";
    sidecar_row.candidate.scope=djmeta::online::EvidenceScope::Edition;
    sidecar_row.candidate.source_id="cue-sidecar-action";
    std::string sidecar_explanation=
        std::string(djmeta::describe_cue_sidecar_action(sidecar.code))+
        " ["+sidecar.code+"]"+
        "; carrier action: "+std::string(djmeta::file_action_label(entry.file_action));
    if(!sidecar.raw_candidate.empty())
        sidecar_explanation+="; carrier RAW ONLY: "+sidecar.raw_candidate;
    sidecar_explanation+="; double-click or press Enter to inspect the CUE carrier in File locations";
    sidecar_explanation+="; final FileOps/CUE postimage NOT approved";
    sidecar_row.candidate.values.push_back(std::move(sidecar_explanation));
    sidecar_row.state=djmeta::online::FieldReviewState::Blocked;
    sidecar_row.reason="cue_carrier_action_not_final_approval";
    rows.push_back(std::move(sidecar_row));
    if(rows.size()>=10000)
        throw std::invalid_argument("CUE physical audit display limit reached.");
    djmeta::online::FieldReviewRow coverage;
    coverage.field="CUE / Selected physical audio coverage";
    coverage.candidate.field="Source-only batch CUE FILE mapping";
    coverage.candidate.provider="Local filesystem (read-only)";
    coverage.candidate.scope=djmeta::online::EvidenceScope::Edition;
    coverage.candidate.source_id="cue-physical-selection";
    coverage.candidate.values.push_back(matched.eligible
        ? "SOURCE MATCH ONLY: "+std::to_string(matched.selected_matched)+
          " selected audio, "+std::to_string(matched.unselected_files)+
          " unselected FILE sources; final FileOps/CUE target unqualified"
        : "BLOCKED: "+matched.rejection+
          "; source-to-batch mapping unqualified (no final file action)");
    coverage.state=djmeta::online::FieldReviewState::Blocked;
    coverage.reason="cue_physical_source_only_not_fileops_approval";
    rows.push_back(std::move(coverage));
    // Failed or truncated CUE inventory must never be recorded as inspected.
    state.cue_inspections.observe(current_cue_plan_rows(
        state,current_batch_route_inputs(state)));
    state.cue_inspections.record(state.selected_track_index,impact,sidecar);
}

// Always independently derive the literal joint target from the current
// physical source, explicit local decisions, official online edition and
// actual foobar Title Formatting. No prior raw string is a source of truth.
std::string recheck_joint_raw_target_candidate(
    PreviewState& state,std::size_t index,const RoutePreviewChoice& choice) {
    if(!native_recheck_preserved_online_selection(
           state.online_field_snapshot.has_value(),
           state.online_edition_source,index,
           state.online_included_fields.size()) ||
       index>=state.entries.size() || index>=state.analyses.size() ||
       index>=state.review_decisions.size() || !state.online_raw_route_valid)
        throw std::invalid_argument(
            "Joint raw route missing or expired; reselect online fields.");
    const auto& entry=state.entries[index];
    if(!entry.input.physical_source_qualified ||
       entry.handle->get_subsong_index()!=0 ||
       is_external_cue_locator(entry.input.source_path) ||
       choice.display_name!=entry.input.profile ||
       choice.destination_root!=entry.input.destination_root ||
       choice.titleformat_expression!=entry.route_expression ||
       !djmeta::raw_destination_root_lexically_safe(choice.destination_root))
        throw std::invalid_argument(
            "Joint target has an unqualified physical source or per-row route.");
    verify_snapshot(entry);
    const auto host=probe_host_file_readonly(entry.input.source_path);
    if(host.state!=HostFileState::ExistingFile ||
       host.host_physical_key!=entry.observed_physical_key ||
       host.source_guard!=entry.observed_source_guard)
        throw std::runtime_error("Joint raw target physical source changed.");
    const auto info=entry.handle->get_info_ref();
    const auto current=metadata_from_file_info(info->info());
    djmeta::UnifiedReviewBatchReprobe unified;
    unified.normalizer.live.captured={
        true,false,entry.observed_physical_key,
        entry.observed_source_guard,current};
    unified.normalizer.live.fresh_tags=current;
    unified.normalizer.live.live_physical_key=host.host_physical_key;
    unified.normalizer.live.live_source_guard=host.source_guard;
    unified.normalizer.captured_analysis=state.analyses[index];
    unified.normalizer.active_ruleset_revision=
        state.analyses[index].ruleset_revision;
    unified.normalizer.decisions=state.review_decisions[index];
    unified.online_capture=*state.online_field_snapshot;
    unified.selected_online_indices=state.online_included_fields;
    unified.explicit_date_targets=state.online_date_targets;
    const auto post=djmeta::preview_unified_review_physical_tags(unified);
    if(!post.physical.eligible)
        throw std::invalid_argument(
            "Joint raw target conflicts with selected fields: "+post.rejection);
    const auto before=evaluate_titleformat_against_canonical(
        entry.handle->get_location(),info->info(),current,
        choice.titleformat_expression);
    const auto after=evaluate_titleformat_against_canonical(
        entry.handle->get_location(),info->info(),post.physical.planned,
        choice.titleformat_expression);
    if(before!=state.online_raw_route_before ||
       after!=state.online_raw_route_after ||
       before.size()>2048 || after.size()>2048 ||
       !djmeta::raw_relative_path_lexically_safe(before) ||
       !djmeta::raw_relative_path_lexically_safe(after))
        throw std::invalid_argument(
            "Joint raw Title Formatting changed. Reselect online fields.");
    return choice.destination_root+"\\"+after;
}

void inspect_selected_raw_targets(HWND dialog, PreviewState& state) {
    const RoutePreviewChoice typed = read_choice(dialog);
    if (typed.display_name != state.current_choice.display_name ||
        typed.destination_root != state.current_choice.destination_root ||
        typed.titleformat_expression != state.current_choice.titleformat_expression)
        throw std::invalid_argument(
            "Apply edited route settings to the preview before inspecting targets.");

    std::set<std::size_t> selected;
    int view_row = -1;
    while ((view_row = ListView_GetNextItem(state.list, view_row, LVNI_SELECTED)) >= 0) {
        const auto view = static_cast<std::size_t>(view_row);
        if (view < state.view_order.size())
            selected.insert(state.view_order[view]);
    }
    if (selected.empty())
        throw std::invalid_argument("Select raw destination rows before inspection.");
    // Local and joint raw names can be inspected in one bounded read-only
    // pass, with the actual joint postimage re-derived instead of guessed.
    // Until the SDK-backed async/batch target probe is qualified, keep
    // explicit GUI-thread filesystem reads bounded on slow network drives.
    if (selected.size() > 128)
        throw std::invalid_argument(
            "Select at most 128 raw candidates per preview inspection. "
            "Large-batch asynchronous host inspection is not qualified yet.");
    verify_rules_snapshot(state.captured_rules);

    struct RawObservation {
        std::size_t index;
        std::string raw_candidate;
        HostFileObservation observed;
        HostDestinationParentChain parents;
        bool joint=false;
    };
    std::vector<RawObservation> observations;
    observations.reserve(selected.size());
    for (const auto index : selected) {
        const PreviewEntry& entry = state.entries.at(index);
        verify_snapshot(entry);
        if (!entry.input.physical_source_qualified ||
            entry.observed_physical_key.empty() ||
            entry.observed_source_guard.empty())
            throw std::invalid_argument(
                "An unqualified physical source cannot be inspected as a file move.");
        const bool joint=native_recheck_preserved_online_selection(
            state.online_field_snapshot.has_value(),
            state.online_edition_source,index,
            state.online_included_fields.size());
        if (!djmeta::raw_destination_root_lexically_safe(entry.input.destination_root) ||
            (!joint &&
             !djmeta::raw_relative_path_lexically_safe(entry.input.raw_relative_path)))
            throw std::invalid_argument(
                "Destination root or raw filename is unsafe; adjust routing before inspection.");

        const HostFileObservation latest =
            probe_host_file_readonly(entry.input.source_path);
        if (latest.state != HostFileState::ExistingFile ||
            latest.host_physical_key != entry.observed_physical_key ||
            latest.source_guard != entry.observed_source_guard)
            throw std::runtime_error(
                "Physical source changed since preview. Reopen Prepare Tracks.");

        // Reading the literal route does not mimic foobar's filename
        // sanitization or automatic source-extension handling.
        const std::string raw_candidate=joint
            ? recheck_joint_raw_target_candidate(state,index,typed)
            : entry.input.destination_root+"\\"+entry.input.raw_relative_path;
        observations.push_back({index,raw_candidate,
            probe_host_file_readonly(raw_candidate),
            probe_raw_destination_parent_chain_readonly(
                raw_candidate,entry.input.destination_root),joint});
    }
    // All-or-nothing UI update: revalidate the entire captured metadata and
    // ruleset snapshot after the slowest selected filesystem observation.
    for (const auto& entry : state.entries) verify_snapshot(entry);
    verify_rules_snapshot(state.captured_rules);
    // Host metadb may retain cached metadata while another process modifies
    // the actual audio file. Re-probe EVERY selected physical source after
    // the final target lookup, before any observation is published.
    // On mismatch, the whole inspection fails without partially replacing
    // existing GUI observations or granting final-FileOps qualification.
    for (const auto index : selected) {
        const PreviewEntry& entry = state.entries.at(index);
        const HostFileObservation current =
            probe_host_file_readonly(entry.input.source_path);
        if (current.state != HostFileState::ExistingFile ||
            current.host_physical_key != entry.observed_physical_key ||
            current.source_guard != entry.observed_source_guard)
            throw std::runtime_error(
                "Physical source changed during raw destination inspection. Reopen Prepare Tracks.");
    }
    // A previously missing destination might have been created while
    // earlier target/source probes ran, or an existing hardlink may have
    // been swapped. Do not publish a mixture of before/after facts: all
    // raw target observations must remain identical on a second bounded
    // read-only pass immediately before the atomic table update.
    for (const auto& result : observations) {
        if(result.joint &&
           recheck_joint_raw_target_candidate(state,result.index,typed)!=
               result.raw_candidate)
            throw std::runtime_error(
                "Joint raw route changed while inspecting destinations.");
        const HostFileObservation current =
            probe_host_file_readonly(result.raw_candidate);
        if (!stable_raw_target_observation(result.observed, current))
            throw std::runtime_error(
                "Raw destination changed during filesystem inspection. Refresh preview.");
        const auto current_parents=
            probe_raw_destination_parent_chain_readonly(
                result.raw_candidate,
                state.entries.at(result.index).input.destination_root);
        if(!stable_raw_destination_parent_chain(result.parents,current_parents))
            throw std::runtime_error(
                "Raw target parent directory, junction or filesystem identity "
                "changed during inspection. Refresh the entire preview.");
    }
    std::size_t unqualified_parents=0;
    std::string first_parent_rejection;
    for (auto& result : observations) {
        PreviewEntry& entry = state.entries[result.index];
        if(!result.parents.qualified) {
            ++unqualified_parents;
            if(first_parent_rejection.empty())
                first_parent_rejection=result.parents.rejection;
            // An observed raw file is never enough when its ancestor
            // chain contains a junction, changed directory or unsupported
            // physical identity. Never copy raw presence or source guard as
            // a usable target observation in that case.
            result.observed.state=HostFileState::Unqualified;
            result.observed.host_physical_key.clear();
            result.observed.source_guard.clear();
            result.observed.detail="Raw destination parent unqualified: "+
                result.parents.rejection;
        }
        if(result.joint) {
            state.online_raw_target_observation=JointRawTargetObservation{
                result.index,result.raw_candidate,
                entry.observed_physical_key,entry.observed_source_guard,
                std::move(result.observed)};
            continue; // local-only routing input must remain untouched
        }
        auto& input = entry.input;
        input.raw_target_physical_key.clear();
        input.raw_target_guard.clear();
        input.raw_target_host_canonical.clear();
        entry.raw_target_probe_detail = result.observed.detail;
        switch (result.observed.state) {
        case HostFileState::Missing:
            input.raw_target_presence = djmeta::RawTargetPresence::Missing;
            input.raw_target_host_canonical = result.observed.canonical_path;
            break;
        case HostFileState::ExistingFile:
            input.raw_target_presence = djmeta::RawTargetPresence::Existing;
            input.raw_target_host_canonical = result.observed.canonical_path;
            input.raw_target_physical_key = result.observed.host_physical_key;
            input.raw_target_guard = result.observed.source_guard;
            break;
        case HostFileState::Unqualified:
        default:
            input.raw_target_presence = djmeta::RawTargetPresence::Unqualified;
            break;
        }
        input.filesystem_target_checked = false; // invariant: final target unknown
    }
    update_table(state);
    std::size_t present = 0, missing = 0, unqualified = 0;
    std::size_t physical_source_aliases = 0;
    for (const auto& result : observations) {
        const auto& issues = state.table.rows.at(result.index).issues;
        for (const auto& issue : issues)
            if (issue == "RAW_TARGET_ALIASES_SOURCE" ||
                issue == "RAW_TARGET_IS_BATCH_SOURCE") {
                ++physical_source_aliases;
                break;
            }
        switch (result.joint
                    ? state.online_raw_target_observation->target.state
                    : result.observed.state) {
        case HostFileState::ExistingFile: ++present; break;
        case HostFileState::Missing: ++missing; break;
        case HostFileState::Unqualified: default: ++unqualified; break;
        }
    }
    // Preserve the distinction between an observed raw location and a
    // final foobar File Operations destination. These counts are UX-only.
    const std::wstring hint =
        L"Raw target inspection: " + std::to_wstring(observations.size()) +
        L" candidate(s); present " + std::to_wstring(present) +
        L", missing " + std::to_wstring(missing) +
        L", unqualified " + std::to_wstring(unqualified) +
        L", physical source aliases " + std::to_wstring(physical_source_aliases) +
        L". Final foobar destinations/CUE dependencies UNVERIFIED; no writes."
        L" Raw target ancestry only; unqualified parent chains: " +
        std::to_wstring(unqualified_parents) + L"." +
        (first_parent_rejection.empty()
            ? std::wstring{}
            : L" First cause: "+from_utf8(first_parent_rejection)+L".");
    SetDlgItemTextW(dialog, IDC_BATCH_HINT, hint.c_str());
}

std::vector<PreviewEntry> capture_preview(
    const metadb_handle_list& handles,
    const RoutePreviewChoice& choice,
    std::vector<djmeta::AnalysisResult>& analyses,
    djmeta::RulesTextSnapshot& captured_rules) {

    const auto loaded = load_rules_text();
    const auto rules = djmeta::parse_ruleset_json(loaded.json);
    // Source is part of the provenance: fallback to bundled rules or
    // migration to a roaming override invalidates existing previews.
    const djmeta::RulesTextSnapshot starting_rules{loaded.json, loaded.source_label};

    // Multiple virtual subsongs from the SAME external CUE all refer to
    // one physical, read-only carrier. Probe its bytes/version once per
    // distinct path, never once per subsong or as a writable audio file.
    std::map<std::string, HostFileObservation> cue_source_probes;
    std::map<std::string, std::size_t> count_per_path;
    for (t_size i = 0; i < handles.get_count(); ++i)
        ++count_per_path[std::string(handles[i]->get_path())];

    std::vector<PreviewEntry> entries;
    entries.reserve(static_cast<std::size_t>(handles.get_count()));
    for (t_size i = 0; i < handles.get_count(); ++i) {
        const metadb_handle_ptr handle = handles[i];
        const auto info_ref = handle->get_info_ref();
        const file_info& info = info_ref->info();
        const auto original = metadata_from_file_info(info);
        auto result = djmeta::Engine{}.analyze(
            original, rules.rules, rules.revision);
        // FileOps always uses only explicitly accepted/manual values. SAFE
        // remains visible as a suggestion in Review, not an implicit rename.
        auto staged = djmeta::project_explicit_review_decisions(original,result);

        PreviewEntry entry;
        entry.handle = handle;
        entry.input_fingerprint = result.input_fingerprint;
        entry.staged = std::move(staged.document);
        entry.input.source_path = handle->get_path();
        entry.input.physical_id = handle->get_path();
        entry.input.profile = choice.display_name;
        entry.input.destination_root = choice.destination_root;
        entry.route_expression = choice.titleformat_expression;
        entry.input.semantic_proposals_pending = staged.unresolved_semantic > 0;
        // Top-level subsong and raw-path uniqueness are only preliminary.
        // A real read-only OS+foobar probe supplies physical identity. This
        // prevents hardlinked files at different paths from being treated as
        // independent physical operations.
        const bool candidate = handle->get_subsong_index() == 0 &&
            count_per_path[entry.input.source_path] == 1;
        const bool external_cue = is_external_cue_locator(entry.input.source_path);
        if (candidate || external_cue) {
            HostFileObservation probe;
            if (external_cue) {
                const auto [it, inserted] =
                    cue_source_probes.try_emplace(entry.input.source_path);
                if (inserted)
                    it->second = probe_host_file_readonly(entry.input.source_path);
                probe = it->second;
            } else {
                probe = probe_host_file_readonly(entry.input.source_path);
            }
            entry.source_probe_detail = probe.detail;
            if (probe.state == HostFileState::ExistingFile) {
                entry.observed_physical_key = probe.host_physical_key;
                entry.observed_source_guard = probe.source_guard;
            }
        } else {
            entry.source_probe_detail =
                "Virtual subsong or duplicate selected foobar source path.";
        }
        // Raw titleformat outcomes are not validated File Operations paths.
        // A filesystem/cuesheet target gate remains permanently unqualified.
        entry.input.filesystem_target_checked = false;
        entry.input.cue_dependencies_checked = false;
        entry.input.physical_source_qualified = false;
        entries.push_back(std::move(entry));
        analyses.push_back(std::move(result));
    }

    std::vector<djmeta::PhysicalSelectionEvidence> evidence;
    evidence.reserve(entries.size());
    for (const auto& entry : entries)
        evidence.push_back({
            entry.handle->get_subsong_index() == 0 &&
                count_per_path[entry.input.source_path] == 1,
            entry.observed_physical_key, entry.observed_source_guard
        });
    const auto checked = djmeta::qualify_physical_selection(evidence);
    TitleformatBatchEvaluator formatter;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        auto& entry = entries[i];
        if (!checked[i].qualified) {
            entry.source_probe_detail = checked[i].reason + ": " +
                entry.source_probe_detail;
            continue;
        }
        entry.input.physical_source_qualified = true;
        entry.input.source_physical_key = entry.observed_physical_key;
        const auto current = entry.handle->get_info_ref();
        entry.input.raw_relative_path = formatter.evaluate(
            entry.handle->get_location(), current->info(), entry.staged,
            choice.titleformat_expression);
    }
    for (const auto& entry : entries) verify_snapshot(entry);
    verify_rules_snapshot(starting_rules);
    captured_rules = starting_rules;
    return entries;
}

// Revalidation is synchronous and read-only. We never replace an open set of
// field decisions merely because foobar selection, playlist or tags changed.
void mark_stale_workspace(PreviewState& state) {
    if (state.stale) return;
    state.stale = true;
    for (int id : {IDC_METADATA_ACCEPT, IDC_METADATA_REJECT,
                   IDC_METADATA_RESET, IDC_METADATA_USE_VALUE,
                   IDC_BATCH_APPLY_SELECTED, IDC_BATCH_APPLY_VISIBLE, IDC_BATCH_APPLY_ALL,
                   IDC_BATCH_UNDO, IDC_BATCH_COPY_AUDIT,
                   IDC_BATCH_COPY_ISSUES, IDC_BATCH_NEXT_ISSUE,
                   IDC_METADATA_MB_SEARCH, IDC_METADATA_MB_LOAD_RELEASE,
                   IDC_METADATA_DISCOGS_NEXT_PAGE,
                   IDC_ONLINE_INCLUDE_FIELDS,IDC_ONLINE_IGNORE_FIELDS,
                   IDC_ONLINE_RESET_FIELDS, IDC_ONLINE_DATE_TARGET_LABEL,
                   IDC_ONLINE_DATE_TARGET, IDC_ONLINE_WRITE_SELECTED,
                   IDC_METADATA_INSPECT_CUE, IDC_METADATA_IMPORT_CANDIDATE})
        EnableWindow(GetDlgItem(state.dialog,id), FALSE);
    SetWindowTextW(state.dialog,L"Music Metadata Studio - STALE snapshot (Read-only)");
    SetDlgItemTextW(state.dialog,IDC_BATCH_HINT,kNativeStaleWorkspaceHint);
}

// Verify the physical source on the UI thread before publishing asynchronous
// provider results. A cached foobar metadb fingerprint alone cannot detect
// replacement on disk. Only the selected track is probed: no batch-wide scan.
void verify_online_source_snapshot(PreviewState& state, const PreviewEntry& entry) {
    try {
        verify_snapshot(entry);
        if (!entry.observed_source_guard.empty() ||
            entry.input.physical_source_qualified ||
            is_external_cue_locator(entry.input.source_path)) {
            if (entry.observed_source_guard.empty() ||
                entry.observed_physical_key.empty())
                throw std::runtime_error("Online source lacks a physical identity guard.");
            const auto latest = probe_host_file_readonly(entry.input.source_path);
            if (latest.state != HostFileState::ExistingFile ||
                latest.host_physical_key != entry.observed_physical_key ||
                latest.source_guard != entry.observed_source_guard)
                throw std::runtime_error(
                    "Physical source changed during the online lookup. Refresh snapshot.");
        }
        // External/embedded CUE has an independent original raw-text check
        // in each search/release publication path, including virtual subsongs.
    } catch (...) {
        mark_stale_workspace(state);
        throw;
    }
}

void revalidate_workspace(PreviewState& state, bool full_source_check = false) {
    if (state.stale || state.checking_snapshot || state.refreshing) return;
    state.checking_snapshot = true;
    bool fresh = true;
    try {
        verify_rules_snapshot(state.captured_rules);
        for (std::size_t i = 0; i < state.entries.size(); ++i) {
            // Focus and selected-source online actions avoid fetching
            // file_info for all 54k sources. Batch decisions and Apply still
            // check ALL captured tags and physical guards.
            if (!native_workspace_should_validate_entry(
                    i, state.selected_track_index, state.entries.size(),
                    full_source_check))
                continue;
            const auto& entry = state.entries[i];
            verify_snapshot(entry);
            // A virtual external CUE must never bypass stale checks merely
            // because it is NOT a qualified physical audio/write target.
            if (entry.observed_source_guard.empty()) {
                if (is_external_cue_locator(entry.input.source_path))
                    throw std::runtime_error(
                        "External CUE lacks a captured physical source guard.");
                continue;
            }
            const auto latest = probe_host_file_readonly(entry.input.source_path);
            if (latest.state != HostFileState::ExistingFile ||
                latest.host_physical_key != entry.observed_physical_key ||
                latest.source_guard != entry.observed_source_guard) {
                throw std::runtime_error("A physical source changed on disk.");
            }
        }
        verify_rules_snapshot(state.captured_rules);
    } catch (...) {
        fresh = false;
    }
    state.checking_snapshot = false;
    if (!fresh) mark_stale_workspace(state);
}

bool same_workspace_selection(const PreviewState& state,
                              const metadb_handle_list& handles) {
    if (handles.get_count() != state.entries.size()) return false;
    for (t_size i=0;i<handles.get_count();++i) {
        const auto& old = state.entries[static_cast<std::size_t>(i)].handle;
        if (std::string(old->get_path()) != std::string(handles[i]->get_path()) ||
            old->get_subsong_index() != handles[i]->get_subsong_index())
            return false;
    }
    return true;
}

void refresh_workspace_snapshot(PreviewState& state) {
#if defined(MMS_ENABLE_PHYSICAL_WRITES)
    if(state.physical_write_pending->load())
        throw std::runtime_error(
            "Wait for the pending physical write result before refreshing.");
#endif
    if (MessageBoxW(state.dialog,
            L"Reload the tracks already open in Music Metadata Studio?\n\n"
            L"All preview decisions, MusicBrainz hits, pasted comparisons and "
            L"file-location plans will be discarded.\n\n"
            L"To work on different tracks, close Studio and open it again "
            L"from their foobar context menu.\n\n"
            L"No music files or tags will be changed.",
            L"Refresh preview only",MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2)
        != IDYES) return;
    // Declining Refresh must never cancel an unrelated in-flight lookup.
    invalidate_online_request(state);
    state.refreshing=true;
    try {
        // Refresh replaces captured metadata, not user-authored search text.
        // Keep its selected physical track so the manual query stays bound to
        // the same source instead of silently moving to row zero.
        const auto selected_before_refresh=state.selected_track_index;
        const auto query_before_refresh=read_control(
            state.dialog,IDC_METADATA_MB_QUERY);
        const bool preserve_manual_query=
            !djmeta::should_replace_auto_online_query(
                query_before_refresh,state.auto_search_text,false);
        const RoutePreviewChoice choice=read_choice(state.dialog);
        metadb_handle_list handles;
        for (const auto& entry: state.entries) handles.add_item(entry.handle);
        // Build the replacement fully before touching the displayed snapshot.
        PreviewState next;
        next.current_choice=choice;
        next.entries=capture_preview(handles,choice,next.analyses,next.captured_rules);
        next.metadata_rows=djmeta::describe_metadata_diffs(next.analyses);
        next.metadata_row_index=djmeta::index_track_diffs(
            next.metadata_rows,next.entries.size());
        for (const auto& analysis: next.analyses)
            next.review_decisions.emplace_back(analysis.proposals.size());
        for (const auto& entry: next.entries) {
            next.source_labels.push_back(entry.input.source_path);
            const auto name=readable_track_name(entry.input.source_path);
            next.cached_track_sort_labels.push_back(name);
            next.cached_track_names.push_back(from_utf8(name));
        }
        refresh_review_summaries(next);
        state.entries.swap(next.entries);
        // A refreshed source snapshot must never inherit a preview rollback
        // or any stale user decisions from the previous captured batch.
        state.last_preview_undo.reset();
        state.preview_history.clear();
        EnableWindow(GetDlgItem(state.dialog,IDC_BATCH_UNDO),FALSE);
        state.analyses.swap(next.analyses);
        state.metadata_rows.swap(next.metadata_rows);
        std::swap(state.metadata_row_index,next.metadata_row_index);
        state.review_decisions.swap(next.review_decisions);
        state.track_summaries.swap(next.track_summaries);
        state.source_labels.swap(next.source_labels);
        state.cached_track_sort_labels.swap(next.cached_track_sort_labels);
        state.cached_track_names.swap(next.cached_track_names);
        state.captured_rules=std::move(next.captured_rules);
        state.current_choice=std::move(next.current_choice);
        state.candidate_rows.clear();
        state.candidate_view_order.clear();
        state.musicbrainz_results.clear();
        state.discogs_results.clear();
        state.discogs_master_id=0;
        state.discogs_master_page=state.discogs_master_pages=0;
        state.discogs_result_page=state.discogs_result_pages=0;
        state.discogs_result_first_path.clear();
        state.discogs_result_identity.clear();
        state.discogs_result_strategy.clear();
        state.discogs_result_attempt_index=state.discogs_result_attempt_total=0;
        state.discogs_direct_edition.reset();
        state.discogs_live_view=false;
        state.discogs_search_was_cue=false;
        state.musicbrainz_detail_groups.clear();
        state.musicbrainz_original_title.clear();
        state.musicbrainz_original_artist.clear();
        state.auto_search_text.clear();
        state.search_plan= {};
        state.search_plan_identity.clear();
        state.search_attempt_index=0;
        state.musicbrainz_has_published_attempt=false;
        state.musicbrainz_published_attempt_index=0;
        state.last_musicbrainz_stage_empty=false;
        state.search_attempt_notice.clear();
        state.auto_search_track=(std::numeric_limits<std::size_t>::max)();
        SetDlgItemTextW(state.dialog,IDC_METADATA_MB_QUERY,
            preserve_manual_query ? query_before_refresh.c_str() : L"");
        state.musicbrainz_live_view=false;
        state.musicbrainz_release_loaded=false;
        state.cue_inspection_mode=false;
        state.cue_candidate_comparison_mode=false;
        state.candidate_source_index=(std::numeric_limits<std::size_t>::max)();
        state.selected_track_index=selected_before_refresh<state.entries.size()
            ? selected_before_refresh : 0;
        state.focused_track_index=(std::numeric_limits<std::size_t>::max)();
        state.track_discovery=djmeta::TrackDiscovery::All;
        state.next_issue_cursor=0;
        state.focused_metadata_rows.clear();
        state.track_view_order.clear();
        state.cached_name_order_ascending.clear();
        state.cached_name_order_descending.clear();
        state.metadata_view_order.clear();
        state.metadata_view_group.clear();
        state.review_group_counts={};
        state.view_order.clear();
        state.stale=false;
        prefill_musicbrainz_query(state);
        for (int id : {IDC_METADATA_ACCEPT, IDC_METADATA_REJECT,
                       IDC_METADATA_RESET, IDC_METADATA_USE_VALUE,
                       IDC_BATCH_APPLY_SELECTED, IDC_BATCH_APPLY_VISIBLE, IDC_BATCH_APPLY_ALL})
            EnableWindow(GetDlgItem(state.dialog,id), TRUE);
        SetWindowTextW(state.dialog,L"Music Metadata Studio - Prepare Tracks (Read-only)");
        update_table(state);
        update_master_table(state);
        if(TabCtrl_GetCurSel(state.tabs)==3)
            show_history_page(state.dialog,state);
        else show_preview_page(state.dialog,state,state.show_metadata,state.show_candidate);
        SetDlgItemTextW(state.dialog,IDC_BATCH_HINT,
            L"Snapshot refreshed. Previous candidate results and decisions discarded. "
            L"Read-only: nothing was written.");
    } catch (...) {
        state.refreshing=false;
        throw;
    }
    state.refreshing=false;
}

// Native context help, using HWND-owned tooltip storage and static immutable
// descriptions. No extra settings files, background requests or UI framework.
void install_workspace_tooltips(PreviewState& state) {
    const HWND dialog=state.dialog;
    const HWND tips=CreateWindowExW(0,TOOLTIPS_CLASSW,nullptr,
        WS_POPUP|TTS_NOPREFIX,
        CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,
        dialog,nullptr,core_api::get_my_instance(),nullptr);
    if (!tips) throw std::runtime_error("Cannot initialize native tooltips.");
    state.help_tooltips=tips;
    SendMessageW(tips,TTM_SETMAXTIPWIDTH,0,MulDiv(340,current_dpi(dialog),96));
    SendMessageW(tips,TTM_SETDELAYTIME,TTDT_INITIAL,600);
    SendMessageW(tips,TTM_SETDELAYTIME,TTDT_RESHOW,200);
    SendMessageW(tips,TTM_SETDELAYTIME,TTDT_AUTOPOP,5200);
    struct HelpText { int id; const wchar_t* text; };
    const HelpText texts[] = { // static-lifetime literal text, dynamic provider ID
        {IDC_BATCH_TABS,L"1. Find and compare. 2. Review proposals. 3. File locations. 4. Session history. No writes."},
        {IDC_BATCH_GUIDE,L"Follow the steps from left to right. Online matches from any provider are evidence, never approved tag edits."},
        {IDC_METADATA_TRACK_LIST,L"Find tab: double-click/Enter searches MusicBrainz or Discogs, or opens Crates in Beatport mode using full TITLE and qualified ARTIST credits where available. Other tabs only navigate to Find without launching an online action."},
        {IDC_METADATA_MB_QUERY,L"MusicBrainz: physical TITLE searches recordings; album: NAME searches releases. An exact release URL compares qualified physical tracks read-only. For CUEs use Inspect CUE. Discogs: title or exact release/master URL. Beatport: canonical public track URL or clipboard. Only an explicit Search makes a request."},
        {IDC_METADATA_ONLINE_PROVIDER,L"MusicBrainz and Discogs use official APIs. Beatport + Crates supports single public Beatport URLs, lawful clipboard evidence and a hand-off to the public Crates browser; no logged-in API impersonation."},
        {IDC_METADATA_DISCOGS_TOKEN_LABEL,L"Discogs token only lives in this password control."},
        {IDC_METADATA_DISCOGS_TOKEN,L"Personal Discogs API token. Not saved, logged, embedded or copied into URLs."},
        {IDC_METADATA_MB_SEARCH,native_search_action_tooltip(
            state.discogs_provider_selected,state.beatport_offline_selected)},
        {IDC_METADATA_DISCOGS_NEXT_PAGE,L"Discogs: explicitly load one more page. Beatport: Find in Crates opens a public search with full TITLE/mix plus qualified ARTIST credits when available (otherwise TITLE only). Clipboard is unchanged on success; a failed browser launch copies the query as a fallback. No credentials, cookies or audio files are transferred."},
        {IDC_METADATA_MB_LOAD_RELEASE,L"Inspect a verified exact edition. MusicBrainz physical releases are comparison-only; unmatched track credits remain blocked."},
        {IDC_ONLINE_INCLUDE_FIELDS,L"Include highlighted exact edition album fields or independently verified release-track credits in a detached preview. No files are changed."},
        {IDC_ONLINE_IGNORE_FIELDS,L"Remove highlighted edition fields from the in-memory preview. Existing tags remain intact."},
        {IDC_ONLINE_RESET_FIELDS,L"Clear every included online field for this exact edition. Does not modify any file or saved metadata."},
        {IDC_ONLINE_WRITE_SELECTED,L"QA-only: submit the exact displayed physical tag differences for this one verified file after a separate final confirmation. No CUE or file move."},
        {IDC_METADATA_INSPECT_CUE,L"Inspect original CUE FILE sources and sidecar actions. Double-click/Enter a linked FILE routing row to jump to its exact physical source, or the carrier row to stage its sidecar action in File locations. No file changes."},
        {IDC_METADATA_IMPORT_CANDIDATE,L"Import structured clipboard evidence for Beatport/Crates, MusicBrainz, Discogs, Deezer or Spotify. Manual @provider claims are unverified, even if an ID is supplied. No authentication claim, tag change or file write."},
        {IDC_METADATA_LIST,L"Select a candidate or proposed change to inspect its source and status."},
        {IDC_METADATA_MB_DETAILS,L"Read-only field evidence: select rows; right-click or Ctrl+C copies full origin, scope, reason and ordered original/proposed values."},
        {IDC_ONLINE_DATE_TARGET_LABEL,L"Do not infer an edition date from your current physical DATE."},
        {IDC_ONLINE_DATE_TARGET,L"Choose a tag for each verified release date. DATE keeps only the four-digit year (YYYY); RELEASEDATE and DATE_RAW keep the full ISO date. PAIR stages Master -> ORIGINALDATE (or year-only DATE) plus Edition -> RELEASEDATE. Memory-only preview, no file write."},
        {IDC_METADATA_FILTER,L"Limit normalization review to music, extended or all tags. In source/detail lists, Ctrl+Shift+Left/Right resizes the divider; Ctrl+Shift+Home resets it."},
        {IDC_METADATA_TRACK_FILTER,L"Review queue: all, proposals, pending REVIEW, pending any, staged edits, completed review, or no proposals. Filtering never changes source files or approvals."},
        {IDC_METADATA_REVIEW_SCOPE,L"Review selected changes/tracks, all tracks, visible queue, or one selected field across visible/all qualified physical tracks. Bulk Manual applies one scalar value per source; multivalues/duplicate fields/CUE are excluded. Default-No confirmation; preview only."},
        {IDC_METADATA_ACCEPT,L"Mark chosen rule proposals as accepted in memory. No music file is written."},
        {IDC_METADATA_REJECT,L"Reject chosen local rule proposals in memory, without changing source files."},
        {IDC_METADATA_RESET,L"Restore pending decisions for selected local proposals, preview only."},
        {IDC_METADATA_MANUAL_INPUT,L"Enter a replacement value for exactly one selected local rule proposal."},
        {IDC_METADATA_USE_VALUE,L"Use the entered value for one proposal or the selected scalar field across visible/all qualified physical tracks. Bulk requires default-No confirmation; no writes."},
        {IDC_METADATA_VISIBLE_WHITESPACE,L"Display tabs, spaces and other invisible characters visibly; original tags remain unchanged."},
        {IDC_BATCH_LIST,L"Proposed source-to-target rows. Right-click one physical source to inspect its uniquely linked, previously inspected CUE carrier. Multiple or stale CUE links are not guessed. Final foobar File Operations targets remain unqualified."},
        {IDC_BATCH_HISTORY_LIST,L"Last 256 session actions. Double-click a single-source action to inspect its current preview, never replay it."},
        {IDC_BATCH_HISTORY_FILTER,L"Filter session actions; source order and original event identities remain unchanged."},
        {IDC_BATCH_LOCATION_FILTER,L"Display-only full-batch raw route and staged FileAction queues: BLOCKED, RAW ONLY or SKIP. Selected/visible previews use only visible original source IDs; Preview all and Audit include hidden rows. No final FileOps/CUE approval."},
        {IDC_BATCH_FILE_ACTION,L"Explicit action intent for the next selected/visible/all preview: Move, Copy, Rename or No file action. Each track retains its own intent; Rename awaits verified same-parent host destination. Nothing is executed."},
        {IDC_BATCH_BACK_TO_CUE,L"Return to the original CUE carrier and reread its full physical FILE reference and sidecar action evidence. Rechecks all sources; never writes files."},
        {IDC_BATCH_NEXT_CUE,L"Open the next uninspected, stale or blocked external CUE carrier from original source order. Reread FILE dependencies on demand. No online search or writer."},
        {IDC_BATCH_PROFILE_PICKER,L"Choose a built-in or saved route, or type a new name. Saved templates do not approve files."},
        {IDC_BATCH_PROFILE_SAVE,L"Save this name, destination and Title Formatting expression in the foobar profile. Existing names require default-No confirmation. Preview again for RAW routes."},
        {IDC_BATCH_PROFILE_DELETE,L"Delete only a saved custom route after default-No confirmation. Built-in Singles/Alben/Livesets are immutable."},
        {IDC_BATCH_PROFILE_COPY,L"Copy all saved custom templates to Windows clipboard for another foobar profile. Includes destination roots; never share publicly."},
        {IDC_BATCH_PROFILE_PASTE,L"Paste a versioned template collection from Windows clipboard; validate every route and confirm all changes (default No). No files are changed."},
        {IDC_BATCH_DESTINATION,L"Destination root for the preview only. No folder is created."},
        {IDC_BATCH_PATTERN,L"foobar Title Formatting pattern for preview file names only. Actual paths are not yet validated."},
        {IDC_BATCH_APPLY_SELECTED,L"Preview selected visible physical sources only; full-batch recheck, no writes."},
        {IDC_BATCH_APPLY_VISIBLE,L"Preview all currently visible filtered File queue rows; hidden rows excluded, no writes."},
        {IDC_BATCH_APPLY_ALL,L"Preview every captured source, including hidden File queue rows; no writes."},
        {IDC_BATCH_REFRESH,L"Reload the original selection after a confirmation. Discards pending decisions, search hits and routing previews. Does not import a different selection."},
        {IDC_BATCH_HELP,L"Open the three-stage walkthrough and explain what each tab can and cannot do."},
        {IDC_BATCH_AUDIT,L"Recheck every selected source, then show local explicit/no-op/pending decisions and raw FileOps blockers. Read-only; no approval."},
        {IDC_BATCH_NEXT_ISSUE,L"Navigate to pending REVIEW/CONFIDENT/SAFE proposals and raw route blockers; never searches, approves or writes."},
        {IDC_BATCH_COPY_AUDIT,L"Recheck every captured physical source and copy all source-order audit rows, including 54k+ libraries. No paths, tags, tokens or write approval."},
        {IDC_BATCH_COPY_ISSUES,L"Recheck the WHOLE batch, then copy only source rows needing attention: staged changes, pending decisions, raw conflicts or unqualified FileOps/CUE. Hidden rows are still validated. No writes."},
        {IDC_BATCH_UNDO,L"Undo the last local Accept/Reject/Manual or file route preview in memory. Up to 4096 affected tracks; no file writes or persisted approvals."},
        {IDCANCEL,L"Close the workspace without writing tags, CUE data or music files."}
    };
    for (const auto& item : texts) {
        // Tables have native row-specific infotips. Registering another
        // whole-table tracking tooltip caused 'stuck' explanatory overlays.
        if (item.id==IDC_METADATA_TRACK_LIST || item.id==IDC_METADATA_LIST ||
            item.id==IDC_BATCH_LIST || item.id==IDC_METADATA_MB_DETAILS)
            continue;
        const HWND child=GetDlgItem(dialog,item.id);
        if (!child) continue; // Optional page control absent on older hosts.
        TOOLINFOW tool{};
        tool.cbSize=TTTOOLINFOW_V2_SIZE;
        tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
        tool.hwnd=dialog;
        tool.uId=reinterpret_cast<UINT_PTR>(child);
        tool.lpszText=const_cast<LPWSTR>(item.text);
        if (!SendMessageW(tips,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool)))
            throw std::runtime_error("Native tooltip registration failed.");
    }
}

void dismiss_workspace_tooltips(PreviewState& state) {
    native_workspace_pop_tooltips(state.help_tooltips,
        {state.metadata_track_list,state.metadata_list,
         state.musicbrainz_details,state.list});
}

// Tool info is HWND-identified exactly as when initially registered.
// A provider switch updates its text in place and dismisses any prior popup.
void update_workspace_search_tooltip(PreviewState& state) {
    const HWND child=GetDlgItem(state.dialog,IDC_METADATA_MB_SEARCH);
    if(!state.help_tooltips || !child) return;
    TOOLINFOW tool{};
    tool.cbSize=TTTOOLINFOW_V2_SIZE;
    tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
    tool.hwnd=state.dialog;
    tool.uId=reinterpret_cast<UINT_PTR>(child);
    tool.lpszText=const_cast<LPWSTR>(native_search_action_tooltip(
        state.discogs_provider_selected,state.beatport_offline_selected));
    SendMessageW(state.help_tooltips,TTM_UPDATETIPTEXTW,0,
                 reinterpret_cast<LPARAM>(&tool));
    dismiss_workspace_tooltips(state);
}

void show_workspace_help(HWND dialog) {
    MessageBoxW(dialog,
        L"1. Find & compare\n"
        L"Select a track on the left. Its TITLE is already in the search box. "
        L"Choose Discogs or MusicBrainz and click Search. Discogs tries "
        L"the next bounded official strategy automatically only on zero hits; "
        L"click Search again to broaden even when results are present. "
        L"Paste an exact HTTPS Discogs release URL to inspect its edition directly. "
        L"Discogs search and release lookups use an isolated network worker, "
        L"so browsing and playback remain responsive during HTTPS. "
        L"Select a result to compare original and suggested fields below. "
        L"For physical audio, type album: Album Name to find releases, or "
        L"paste an exact MusicBrainz release URL. Its tracklist is read-only "
        L"evidence, never permission to import/write tags. For CUE, click "
        L"Inspect CUE first to compare its original contents.\n\n"
        L"2. Review proposals\n"
        L"See the separate normalization suggestions generated by the local "
        L"rule engine. Accept, Reject and Use value only change the preview. "
        L"Online hits are NOT automatically imported as tag edits. "
        L"Discogs shows linked master and edition dates separately; "
        L"choose a tag for each or PAIR to preview both in one click.\n\n"
        L"Beatport + Crates is not an authorized catalog API. On Find, "
        L"double-click/Enter on a physical track to open Crates in your "
        L"browser with its ARTIST + full TITLE search already applied. "
        L"Only the search query enters the public browser URL; you choose "
        L"the actual release/track. Clipboard stays unchanged on success. "
        L"Load Beatport URL fetches ONE exact public track page only after "
        L"your explicit click, without login, cookies or API impersonation. "
        L"Alternatively select Import clipboard for lawfully supplied "
        L"@beatport-track-json on the first line, followed by track JSON. "
        L"Neither browser hand-off nor clipboard intake writes files. "
        L"Physical title, artist, version, ISRC and duration are compared. "
        L"Deezer and Spotify currently have no authenticated live lookup in "
        L"Studio. To compare facts you personally verified there, choose "
        L"Import clipboard with a plain-text candidate: "
        L"@provider=deezer (or spotify), then @id=track:NUMBER, then "
        L"TITLE=... and ARTIST=... on separate lines. Explicit dates use "
        L"DATE@digital=YYYY-MM-DD. These fields are labeled Pasted, never "
        L"official API evidence, and cannot silently change physical tags. "
        L"For a single Crates recording, open a track detail page and run "
        L"scripts/crates-copy-visible-track-facts.js as a bookmarklet. "
        L"For an entire release including its catalog number and tracklist, "
        L"open the release page and run "
        L"scripts/crates-copy-visible-release-facts.js. Select Import clipboard "
        L"to review @crates-public-track or @crates-public-release evidence. "
        L"The source remains Crates and its visible Date is NOT assumed to be "
        L"a qualified edition or recording date. "
        L"Alternatively @beatport-track-facts accepts explicitly typed "
         L"TITLE/ARTIST/VERSION/DURATION=MM:SS and other visible factual "
         L"fields with @provider=beatport and @id=track:NNN. "
         L"To compare an exact Beatport edition, paste @beatport-release-json "
         L"then the release detail JSON, then @beatport-tracks-json and its "
         L"complete track page. Incomplete or foreign pages fail closed. "
         L"Physical and inspected CUE tracks get independent, read-only "
         L"mix/identity evidence; no CUE ordinal is inferred from order. "
         L"Mismatches are blocked.\n\n"
        L"3. Plan file locations\n"
        L"Choose a routing profile, destination and naming expression, then "
        L"preview the proposed paths. Neither moves nor renames are enabled.\n\n"
        L"Hover over buttons or fields for details. Refresh snapshot discards "
        L"pending work and reloads the SAME selected tracks. Close Studio and "
        L"open it again in foobar to work on a different selection.\n\n"
        L"ALL THREE TABS ARE READ-ONLY: audio files, CUE, tags and timestamps "
        L"are never changed by these actions.",
        L"How to use Music Metadata Studio",MB_OK|MB_ICONINFORMATION);
}

// Only immutable path/token strings cross into the worker. The thread has
// no access to foobar SDK, HWND controls, MetadataDocument or CUE bytes.
void begin_discogs_network(HWND dialog, PreviewState& state,
                           std::string path, std::string token,
                           PendingDiscogsContext context) {
    if(state.discogs_request || state.musicbrainz_request ||
       state.beatport_public_request)
        throw std::runtime_error("An official provider request is already active.");
    // A worker-owned token is wiped whether WinHTTP succeeds or throws.
    auto transport=[route=std::move(path),secret=std::move(token)](
        std::stop_token cancel) mutable {
        struct Wipe {
            std::string& value;
            ~Wipe(){ if(!value.empty())
                SecureZeroMemory(value.data(),value.size()); }
        } wipe{secret};
        return fetch_discogs_json_readonly(route,secret,cancel);
    };
    context.context_version=state.online_context_version.current();
    auto job=start_readonly_network(dialog,std::move(transport));
    state.discogs_context.emplace(std::move(context));
    state.discogs_request=std::move(job);
    state.online_user_cancel_requested=false;
    update_live_provider_controls(state); // actual native button: Cancel lookup
    EnableWindow(GetDlgItem(dialog,IDC_METADATA_MB_LOAD_RELEASE),FALSE);
    EnableWindow(GetDlgItem(dialog,IDC_METADATA_DISCOGS_NEXT_PAGE),FALSE);
}



// Explicit Crates browser hand-off. The normal browser owns any Beatport
// account login. Only this one local TITLE+ARTIST search phrase crosses via
// the user's clipboard; no cookies, OAuth/session tokens, music paths,
// file bytes, ISRC inventory, or hidden Crates API requests leave foobar.
void copy_crates_search_text(HWND dialog,std::wstring_view query) {
    if(query.empty() || query.size()>200)
        throw std::invalid_argument("Crates clipboard query is invalid");
    if(!OpenClipboard(dialog))
        throw std::runtime_error("Could not open Windows clipboard for Crates search");
    struct Close { ~Close(){CloseClipboard();} } closer;
    const auto bytes=(query.size()+1)*sizeof(wchar_t);
    HGLOBAL handle=GlobalAlloc(GMEM_MOVEABLE,bytes);
    if(!handle) throw std::bad_alloc();
    auto* memory=static_cast<wchar_t*>(GlobalLock(handle));
    if(!memory) {
        GlobalFree(handle);
        throw std::runtime_error("Could not prepare Crates search clipboard text");
    }
    std::copy(query.begin(),query.end(),memory);
    memory[query.size()]=L'\0';
    GlobalUnlock(handle);
    if(!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT,handle)) {
        GlobalFree(handle);
        throw std::runtime_error("Could not copy Crates query to clipboard");
    }
    // System owns handle only after SetClipboardData succeeds.
}
// Explicit, read-only clipboard action. Verify the online/local source
// postimage exactly as Audit batch does before copying the entire source-order
// report. No disk, tags, CUE, or FileOps writer is reachable here.
void copy_workflow_audit(HWND dialog,const PreviewState& state,
                         bool attention_only=false) {
    std::optional<djmeta::WorkflowOnlineSourceCounts> verified_online;
    const auto verified_summary=workflow_audit_summary(state,&verified_online);
    const auto audit=djmeta::audit_workflow_preview(
        state.table,state.analyses,state.review_decisions);
    const auto tsv=attention_only
        ? djmeta::format_workflow_audit_tsv(state.table,audit,verified_online,
              djmeta::WorkflowAuditExportScope::AttentionRows)
        : djmeta::format_workflow_audit_tsv(
              state.table,audit,verified_online);
    const auto file_actions=current_file_action_audit(state,audit);
    const auto ledger=djmeta::format_workflow_file_action_tsv(
        file_actions,attention_only
            ? djmeta::WorkflowAuditExportScope::AttentionRows
            : djmeta::WorkflowAuditExportScope::AllRows);
    if(state.cue_inspections.plan.size()!=state.entries.size())
        throw std::invalid_argument("CUE_QUEUE_EXPORT_PLAN_STALE");
    const auto cue_ledger=state.cue_inspections.export_tsv(
        captured_cue_carriers(state),attention_only);
    const auto report=verified_summary+L"\r\n\r\n"+from_utf8(tsv)+
        L"\r\n"+from_utf8(ledger)+L"\r\n"+from_utf8(cue_ledger);
    native_copy_audit_text_to_clipboard(dialog,report);
    SetDlgItemTextW(dialog,IDC_BATCH_HINT,attention_only
        ? L"Read-only attention audit copied, including current CUE FILE action/risk codes by captured source row. No paths, tags or writes."
        : L"Full read-only audit copied, including per-FILE CUE source/action risks. No music paths, tag values or writes.");
}

void open_crates_for_selected_physical_track(
    HWND dialog,PreviewState& state) {
    if(!state.show_candidate || !state.beatport_offline_selected ||
       state.selected_track_index>=state.entries.size() ||
       state.stale || state.refreshing ||
       state.beatport_public_request || state.discogs_request ||
       state.musicbrainz_request)
        throw std::invalid_argument("Crates search requires a stable selected physical track");
    const auto& entry=state.entries[state.selected_track_index];
    verify_online_source_snapshot(state,entry);
    verify_rules_snapshot(state.captured_rules);
    if(!entry.input.physical_source_qualified ||
       entry.handle->get_subsong_index()!=0 ||
       is_external_cue_locator(entry.input.source_path))
        throw std::invalid_argument("Crates search cannot use a virtual CUE track");
    const auto info=entry.handle->get_info_ref();
    const auto original=metadata_from_file_info(info->info());
    const auto search=djmeta::online::prepare_crates_browser_search(original);
    verify_online_source_snapshot(state,entry);
    const auto url=from_utf8(search.browser_url);
    const HINSTANCE launched=ShellExecuteW(dialog,L"open",url.c_str(),
                                            nullptr,nullptr,SW_SHOWNORMAL);
    if(reinterpret_cast<INT_PTR>(launched)<=32) {
        // Only a failed browser launch needs the original clipboard fallback.
        copy_crates_search_text(dialog,from_utf8(search.search_text));
        throw std::runtime_error(
            "Could not open Crates in the default browser. The search query was copied to clipboard for manual search.");
    }
    // A new deliberate Crates search must not leave an OLD, still-valid
    // Beatport URL in the native query box. Otherwise the next Load click
    // would silently load the previous track instead of the newly copied
    // Crates result, even though the clipboard contains the fresh URL.
    SetDlgItemTextW(dialog,IDC_METADATA_MB_QUERY,L"");
    SetDlgItemTextW(dialog,IDC_BATCH_HINT,
        L"Crates opened with a source-derived TITLE/mix search, using "
        L"qualified ARTIST credits where available. Select "
        L"the exact edition in your browser, then copy its Beatport "
        L"track URL into Load Beatport URL. Clipboard and metadata "
        L"were not changed by this search.");
}

// A Beatport public URL is not a catalog API identity or staging authority.
// Exactly one user-requested page is fetched on an isolated cancellable worker.
void perform_beatport_public_page_lookup(HWND dialog,PreviewState& state) {
    using namespace djmeta::online;
    if(!state.beatport_offline_selected || !state.show_candidate ||
       state.selected_track_index>=state.entries.size())
        throw std::invalid_argument("Select a physical audio track in Beatport mode.");
    if(state.beatport_public_request || state.discogs_request ||
       state.musicbrainz_request)
        throw std::runtime_error("Another provider request is still running.");
    const auto& entry=state.entries[state.selected_track_index];
    verify_online_source_snapshot(state,entry);
    verify_rules_snapshot(state.captured_rules);
    if(!entry.input.physical_source_qualified ||
       entry.handle->get_subsong_index()!=0 ||
       is_external_cue_locator(entry.input.source_path))
        throw std::invalid_argument(
            "Beatport page requires one qualified physical track, not virtual CUE.");
    const auto target=qualify_beatport_public_track_url(
        to_utf8(read_control(dialog,IDC_METADATA_MB_QUERY)));
    const auto info=entry.handle->get_info_ref();
    PendingBeatportPageContext ctx;
    ctx.source_index=state.selected_track_index;
    ctx.context_version=state.online_context_version.current();
    ctx.url=target.canonical_url;
    ctx.originals=metadata_from_file_info(info->info());
    const double seconds=info->info().get_length();
    if(seconds>0 && seconds<=36000)
        ctx.duration_ms=static_cast<std::int64_t>(seconds*1000.0+0.5);
    verify_online_source_snapshot(state,entry);
    auto job=start_readonly_network(dialog,
        [route=ctx.url](std::stop_token stop) {
            return fetch_beatport_public_html_readonly(route,stop);
        });
    state.beatport_public_context.emplace(std::move(ctx));
    state.beatport_public_request=std::move(job);
    state.online_user_cancel_requested=false;
    update_live_provider_controls(state); // actual native button: Cancel lookup
    SetDlgItemTextW(dialog,IDC_BATCH_HINT,
        L"Loading ONE public Beatport page. Access denials stop without "
        L"retry, bypass or changes to your files.");
}
void consume_beatport_public_network(HWND dialog,PreviewState& state) {
    auto job=state.beatport_public_request;
    if(!job || !readonly_network_finished(*job)) return;
    if(job->worker.joinable()) job->worker.join();
    auto ctx=std::move(state.beatport_public_context);
    state.beatport_public_request.reset();
    state.beatport_public_context.reset();
    const bool cancelled=state.online_user_cancel_requested;
    state.online_user_cancel_requested=false;
    if(state.online_close_requested) {
        DestroyWindow(dialog);
        return;
    }
    try {
        update_live_provider_controls(state);
        if(cancelled) {
            update_metadata_table(state);
            SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                L"Lookup cancelled. Existing comparisons and review decisions "
                L"were preserved; no files changed.");
            return;
        }
        if(!ctx || !state.online_context_version.matches(ctx->context_version)) {
            update_metadata_table(state);
            return; // provider/track/source changed while HTTP was running
        }
        if(!job->error.empty()) throw std::runtime_error(job->error);
        if(state.stale || state.refreshing || !state.show_candidate ||
           !state.beatport_offline_selected ||
           ctx->source_index>=state.entries.size() ||
           state.selected_track_index!=ctx->source_index)
            throw std::invalid_argument("Public-page request is stale.");
        const auto& entry=state.entries[ctx->source_index];
        verify_online_source_snapshot(state,entry);
        verify_rules_snapshot(state.captured_rules);
        if(!entry.input.physical_source_qualified ||
           entry.handle->get_subsong_index()!=0 ||
           is_external_cue_locator(entry.input.source_path))
            throw std::invalid_argument("Source is no longer qualified audio.");
        const auto info=entry.handle->get_info_ref();
        const auto current=metadata_from_file_info(info->info());
        const double duration=info->info().get_length();
        const auto current_ms=duration>0 && duration<=36000
            ? static_cast<std::int64_t>(duration*1000.0+0.5)
            : std::int64_t{-1};
        if(current!=ctx->originals || current_ms!=ctx->duration_ms)
            throw std::invalid_argument("Original audio metadata/duration changed.");
        const auto record=djmeta::online::parse_beatport_public_html(
            ctx->url,job->response);
        auto reviewed=djmeta::online::review_beatport_user_track(
            current,ctx->duration_ms,record);
        for(auto& row:reviewed.fields) {
            if(djmeta::online::is_local_physical_original(row))
                row.field="Physical / "+row.field;
            else {
                row.field="Beatport Track / "+row.field;
                row.reason="beatport_public_webpage_"+row.reason;
            }
        }
        auto grouped=group_musicbrainz_release_rows(reviewed.fields);
        verify_online_source_snapshot(state,entry);
        verify_rules_snapshot(state.captured_rules);
        state.candidate_rows=std::move(grouped.summary);
        state.musicbrainz_detail_groups=std::move(grouped.detail);
        state.candidate_source_index=ctx->source_index;
        state.cue_inspection_mode=false;
        state.cue_candidate_comparison_mode=false;
        state.musicbrainz_live_view=true; // grouped browser, not MB authorization
        state.musicbrainz_release_loaded=true;
        state.musicbrainz_kind=djmeta::online::musicbrainz::SearchKind::Recording;
        state.discogs_live_view=false;
        state.online_physical_edition.reset(); // NOT authenticated API data
        state.online_raw_target_observation.reset();
        state.online_field_snapshot.reset();
        state.online_field_preview.reset();
        state.online_included_fields.clear();
        state.online_date_targets.clear();
        state.search_attempt_notice=
            L"Public Beatport HTML (unverified website facts). Independent "
            L"track/mix/ISRC/duration comparison only; never auto-stage or write.";
        update_metadata_table(state);
        SetDlgItemTextW(dialog,IDC_BATCH_HINT,
            state.search_attempt_notice.c_str());
    } catch(const std::exception& error) {
        if(state.stale) return;
        state.search_attempt_notice=L"Beatport public HTML: "+
            from_utf8(error.what())+L" No metadata or files were changed.";
        update_metadata_table(state);
        SetDlgItemTextW(dialog,IDC_BATCH_HINT,
            state.search_attempt_notice.c_str());
    }
}

// Official Discogs requests share the existing foobar snapshot/rules guards.
// Token is entered in a password-style control and never persisted or logged.
// One deliberate Search starts a bounded official plan: zero-hit responses
// may advance automatically, while nonempty hits require an explicit click.
void perform_discogs_search(HWND dialog, PreviewState& state) {
    using namespace djmeta::online;
    using namespace djmeta::online::discogs;
    if(state.discogs_request || state.musicbrainz_request ||
       state.beatport_public_request)
        throw std::runtime_error("Wait for the previous official provider request.");
    if(!state.show_candidate || state.selected_track_index>=state.entries.size())
        throw std::invalid_argument("Select a qualified audio or CUE source first.");
    const auto& entry=state.entries[state.selected_track_index];
    verify_snapshot(entry);
    verify_rules_snapshot(state.captured_rules);
    const bool cue_mode=is_external_cue_locator(entry.input.source_path) ||
        (state.candidate_source_index==state.selected_track_index &&
         (state.cue_inspection_mode || state.cue_candidate_comparison_mode ||
          (state.discogs_live_view && state.discogs_search_was_cue)));
    djmeta::MetadataDocument originals;
    std::string local_title,local_artist,local_album,local_version,cue_before;
    std::string local_barcode,local_catalog_number;
    if(cue_mode) {
        const auto raw=read_cue_raw_on_demand(entry.handle,entry.input.source_path);
        const auto cue=djmeta::inspect_cue_metadata(raw.raw_text,raw.carrier);
        if(cue.status!=djmeta::CueSyntaxStatus::Parsed)
            throw std::invalid_argument("Discogs CUE search needs a qualified CUE inventory.");
        const auto hints=cue_search_hints(cue);
        local_title=hints.title;
        local_artist=hints.artist;
        local_album=hints.album; // Qualified CUE-global album, not a virtual tag.
        cue_before=raw.raw_text;
    } else {
        if(!entry.input.physical_source_qualified)
            throw std::invalid_argument("Discogs search requires a qualified physical source.");
        const auto info=entry.handle->get_info_ref();
        originals=metadata_from_file_info(info->info());
        // An exact manually entered URL (or a manual text query) must
        // remain reachable even if local TITLE, ARTIST, ALBUM or identifiers
        // are ambiguous. No local hint proves a provider track association.
        const auto hints=physical_search_hints(originals);
        local_title=hints.title;
        local_artist=hints.artist;
        local_album=hints.album;
        local_version=hints.version;
        local_barcode=hints.barcode;
        local_catalog_number=hints.catalog_number;
    }
    const auto entered=to_utf8(read_control(dialog,IDC_METADATA_MB_QUERY));
    const bool auto_query=entered.empty() ||
        (state.auto_search_track==state.selected_track_index &&
         entered==to_utf8(state.auto_search_text));
    const std::string term=entered.empty()?local_title:entered;
    if(term.empty() && (!auto_query ||
       (local_album.empty() && local_barcode.empty() &&
        local_catalog_number.empty() && local_artist.empty())))
        throw std::invalid_argument(
            "Enter a track, artist, album, edition identifier or exact Discogs release URL.");
    auto token=to_utf8(read_control(dialog,IDC_METADATA_DISCOGS_TOKEN));
    if(token.empty()) throw std::invalid_argument(
        "Discogs needs an authorized personal access token (not saved).");
    const auto direct_id=release_id_from_url(term);
    const auto master_id=master_id_from_url(term);
    if(term.starts_with("https://") && !direct_id && !master_id)
        throw std::invalid_argument(
            "Only canonical HTTPS Discogs release/master URLs are accepted.");
    std::string path,strategy,identity;
    std::uint64_t master_page=1;
    std::size_t total=1;
    std::size_t attempt_index=0;
    if(direct_id) {
        path=release_path(direct_id);
        strategy="Exact release URL";
        identity="discogs|direct|"+std::to_string(direct_id)+"|"+
            entry.input.source_path;
    } else if(master_id) {
        identity="discogs|master|"+std::to_string(master_id)+"|"+
            entry.input.source_path+"|"+std::to_string(state.selected_track_index);
        const bool continuing=state.discogs_live_view &&
            !state.musicbrainz_release_loaded &&
            state.candidate_source_index==state.selected_track_index &&
            state.discogs_master_id==master_id &&
            state.search_plan_identity==identity &&
            state.discogs_master_page>0 &&
            state.discogs_master_page<state.discogs_master_pages &&
            state.discogs_master_page<max_master_version_page;
        if(continuing) master_page=state.discogs_master_page+1;
        path=master_versions_path(master_id,master_page);
        strategy="Master edition candidates, page "+std::to_string(master_page);
    } else {
        const auto plan=track_search_plan(
            term,auto_query?local_artist:std::string{},
            auto_query?local_barcode:std::string{},
            auto_query?local_catalog_number:std::string{},
            auto_query?local_album:std::string{},true,
            auto_query?local_version:std::string{});
        identity="discogs|"+entry.input.source_path+"|"+
            std::to_string(state.selected_track_index)+"|"+term+"|"+
            (auto_query?local_artist:"")+"|"+
            (auto_query?local_barcode:"")+"|"+
            (auto_query?local_catalog_number:"")+"|"+
            (auto_query?local_album:"")+"|"+
            (auto_query?local_version:"");
        // Only the last PUBLISHED response advances the plan. A network
        // failure does not consume a strategy; even irrelevant nonempty
        // hits can be broadened by another deliberate Search click.
        attempt_index=next_search_attempt(
            state.search_plan_identity,identity,
            state.search_attempt_index,plan.size());
        total=plan.size();
        const auto& attempt=plan.at(attempt_index);
        path=attempt.path;
        strategy=attempt.label;
    }
    SetDlgItemTextW(dialog,IDC_BATCH_HINT,
        (L"Discogs official HTTPS (read-only): "+from_utf8(strategy)+L" ["+
         std::to_wstring(attempt_index+1)+L"/"+
         std::to_wstring(total)+L"]...").c_str());
    RedrawWindow(dialog,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
    PendingDiscogsContext context;
    context.kind=PendingDiscogsContext::Kind::Search;
    context.source_index=state.selected_track_index;
    context.cue_mode=cue_mode;
    context.cue_before=std::move(cue_before);
    context.originals=std::move(originals);
    context.release_id=direct_id;
    context.master_id=master_id;
    context.master_page=master_page;
    context.plan_identity=std::move(identity);
    context.strategy=std::move(strategy);
    context.attempt_index=attempt_index;
    context.total=total;
    if(!direct_id && !master_id)
        context.result_first_path=path; // official page-one search only
    begin_discogs_network(dialog,state,std::move(path),std::move(token),
                          std::move(context));
}

// A separate user action navigates ONE extra page, without advancing the
// staged query or borrowing an unrelated source's provider identity.
void perform_discogs_next_page(HWND dialog, PreviewState& state) {
    using namespace djmeta::online::discogs;
    if(state.discogs_request || state.musicbrainz_request ||
       state.stale || !state.show_candidate || !state.discogs_provider_selected ||
       !state.discogs_live_view || state.musicbrainz_release_loaded ||
       state.selected_track_index!=state.candidate_source_index ||
       state.selected_track_index>=state.entries.size() ||
       state.discogs_result_first_path.empty() ||
       state.search_plan_identity!=state.discogs_result_identity ||
       state.search_attempt_index!=state.discogs_result_attempt_index ||
       !state.discogs_result_page ||
       state.discogs_result_page>=state.discogs_result_pages ||
       state.discogs_result_page>=max_search_result_page)
        throw std::invalid_argument("No qualified next Discogs result page.");
    const auto& entry=state.entries[state.selected_track_index];
    verify_snapshot(entry);
    verify_rules_snapshot(state.captured_rules);
    PendingDiscogsContext request;
    request.kind=PendingDiscogsContext::Kind::Search;
    request.source_index=state.selected_track_index;
    request.cue_mode=state.discogs_search_was_cue;
    if(request.cue_mode) {
        const auto raw=read_cue_raw_on_demand(entry.handle,entry.input.source_path);
        if(djmeta::inspect_cue_metadata(raw.raw_text,raw.carrier).status!=
           djmeta::CueSyntaxStatus::Parsed)
            throw std::invalid_argument("CUE source is no longer qualified.");
        request.cue_before=raw.raw_text;
    } else {
        if(!entry.input.physical_source_qualified ||
           is_external_cue_locator(entry.input.source_path))
            throw std::invalid_argument("Physical source is no longer qualified.");
        const auto info=entry.handle->get_info_ref();
        request.originals=metadata_from_file_info(info->info());
    }
    request.result_page=state.discogs_result_page+1;
    request.result_first_path=state.discogs_result_first_path;
    request.plan_identity=state.discogs_result_identity;
    request.strategy=state.discogs_result_strategy;
    request.attempt_index=state.discogs_result_attempt_index;
    request.total=state.discogs_result_attempt_total;
    const auto path=search_result_page_path(
        request.result_first_path,request.result_page);
    auto token=to_utf8(read_control(dialog,IDC_METADATA_DISCOGS_TOKEN));
    if(token.empty())
        throw std::invalid_argument("Authorized Discogs token required.");
    SetDlgItemTextW(dialog,IDC_BATCH_HINT,
        (L"Discogs official search page "+std::to_wstring(request.result_page)+
         L" (read-only)...").c_str());
    begin_discogs_network(dialog,state,path,std::move(token),std::move(request));
}

void publish_discogs_release(HWND dialog, PreviewState& state,
                             const PendingDiscogsContext& request,
                             const djmeta::online::ReleaseEdition& edition);

void publish_discogs_search(HWND dialog, PreviewState& state,
                            const PendingDiscogsContext& request,
                            const std::string& reply) {
    (void)dialog;
    using namespace djmeta::online;
    using namespace djmeta::online::discogs;
    if(!state.online_context_version.matches(request.context_version) ||
       state.stale || state.refreshing ||
       !state.show_candidate || !state.discogs_provider_selected ||
       state.selected_track_index!=request.source_index ||
       request.source_index>=state.entries.size())
        throw std::runtime_error("Discogs result discarded: the selected source changed.");
    const auto& entry=state.entries[request.source_index];
    verify_online_source_snapshot(state, entry);
    verify_rules_snapshot(state.captured_rules);
    if(request.cue_mode && read_cue_raw_on_demand(
        entry.handle,entry.input.source_path).raw_text!=request.cue_before)
        throw std::runtime_error("CUE changed during Discogs lookup. Retry.");
    SearchResults result;
    std::optional<MasterVersionsPage> version_page;
    std::optional<SearchResultPage> search_page;
    std::optional<ReleaseEdition> direct_edition;
    if(request.master_id) {
        version_page=parse_master_versions_page(
            reply,request.master_id,request.master_page);
        result=version_page->results;
    } else if(request.release_id) {
        direct_edition=parse_release(reply,request.release_id);
        SearchHit hit;
        hit.kind=EntityKind::Release;
        hit.id=request.release_id;
        hit.canonical_url=canonical_url(hit.kind,request.release_id);
        for(const auto& field:direct_edition->fields) {
            if(field.values.empty()) continue;
            if(field.field=="ALBUM") hit.title=field.values.front();
            else if(field.field=="DATE") hit.year=field.values.front();
            else if(field.field=="COUNTRY") hit.country=field.values.front();
            else if(field.field=="ALBUM ARTIST") hit.artist=field.values.front();
        }
        result.hits.push_back(std::move(hit));
    } else {
        search_page=parse_search_result_page(reply,request.result_page);
        result=search_page->results;
    }
    // The final host recheck is on the UI thread immediately before display;
    // a remote response never becomes tag-write authorization.
    verify_online_source_snapshot(state, entry);
    verify_rules_snapshot(state.captured_rules);
    auto browse=discogs_search_rows(result,request.originals);
    state.candidate_rows=std::move(browse.summary);
    state.musicbrainz_detail_groups=std::move(browse.detail);
    state.musicbrainz_results.clear();
    state.discogs_results=std::move(result.hits);
    state.discogs_master_id=version_page?request.master_id:0;
    state.discogs_master_page=version_page?version_page->page:0;
    state.discogs_master_pages=version_page?version_page->pages:0;
    state.discogs_result_page=search_page?search_page->page:0;
    state.discogs_result_pages=search_page?search_page->pages:0;
    state.discogs_result_first_path=search_page?request.result_first_path:std::string{};
    state.discogs_result_identity=search_page?request.plan_identity:std::string{};
    state.discogs_result_strategy=search_page?request.strategy:std::string{};
    state.discogs_result_attempt_index=request.attempt_index;
    state.discogs_result_attempt_total=request.total;
    state.discogs_direct_edition=std::move(direct_edition);
    state.discogs_search_was_cue=request.cue_mode;
    state.candidate_source_index=request.source_index;
    state.musicbrainz_kind=musicbrainz::SearchKind::Release;
    state.discogs_live_view=true;
    state.musicbrainz_live_view=true;
    state.musicbrainz_release_loaded=false;
    state.cue_inspection_mode=false;
    state.cue_candidate_comparison_mode=false;
    state.search_plan_identity=request.plan_identity;
    state.search_attempt_index=request.attempt_index;
    const bool broaden_empty=!request.release_id && !request.master_id &&
        request.result_page==1 && state.discogs_results.empty() &&
        request.attempt_index+1<request.total;
    state.search_attempt_notice=request.master_id
        ? L"Discogs master "+std::to_wstring(request.master_id)+L", page "+
          std::to_wstring(state.discogs_master_page)+L"/"+
          std::to_wstring(state.discogs_master_pages)+L": "+
          std::to_wstring(state.discogs_results.size())+
          L" edition candidates. Select a release and Load release. "+
          (state.discogs_master_page<state.discogs_master_pages &&
           state.discogs_master_page<max_master_version_page
              ? L"Click Next master page for more (read-only)."
              : L"Last supported page. No tags written.")
        : L"Discogs query "+std::to_wstring(request.attempt_index+1)+L"/"+
        std::to_wstring(request.total)+L" - "+from_utf8(request.strategy)+
        L" (page "+std::to_wstring(state.discogs_result_page)+L"/"+
        std::to_wstring(state.discogs_result_pages)+L"): "+
        std::to_wstring(state.discogs_results.size())+L" result(s). "+
        (broaden_empty
            ? L"No hits; trying the next bounded official query automatically."
            : state.discogs_results.empty()
                ? L"No release found. Paste an exact HTTPS Discogs release URL."
                : request.attempt_index+1<request.total
                    ? L"Review any exact release or click Broaden Discogs "
                      L"to try the next official strategy. "
                      L"Nonmatching results are never accepted."
                : state.discogs_result_page<state.discogs_result_pages &&
                  state.discogs_result_page<max_search_result_page
                    ? L"Next results page available; select only an exact edition."
                    : L"Select an exact release; Search restarts the plan.");
    ListView_SetItemState(state.metadata_list,-1,0,LVIS_SELECTED);
    update_metadata_table(state);
    update_live_provider_controls(state);
    if(state.discogs_direct_edition) {
        // The deliberate exact URL search already fetched this edition.
        // Reuse it for read-only Track/CUE field comparison in one action.
        const auto target="release:"+std::to_string(request.release_id);
        std::size_t view_index=state.candidate_view_order.size();
        for(std::size_t i=0;i<state.candidate_view_order.size();++i) {
            const auto row=state.candidate_view_order[i];
            if(row<state.candidate_rows.size() &&
               state.candidate_rows[row].candidate.source_id==target) {
                view_index=i;
                break;
            }
        }
        if(view_index==state.candidate_view_order.size())
            throw std::runtime_error("Exact Discogs release missing from native result list.");
        state.updating_musicbrainz_selection=true;
        ListView_SetItemState(state.metadata_list,-1,0,LVIS_SELECTED);
        ListView_SetItemState(state.metadata_list,static_cast<int>(view_index),
            LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        state.updating_musicbrainz_selection=false;
        PendingDiscogsContext exact_request=request;
        exact_request.kind=PendingDiscogsContext::Kind::Release;
        exact_request.context_version=state.online_context_version.current();
        publish_discogs_release(dialog,state,exact_request,
                                *state.discogs_direct_edition);
    } else if(broaden_empty) {
        // Each request remains isolated, cancelable, token-header-only and
        // snapshot-checked. Stop on the first nonempty page or an error.
        // No provider call occurs without the initiating Search click.
        perform_discogs_search(dialog,state);
    }
}

void perform_discogs_load(HWND dialog, PreviewState& state) {
    using namespace djmeta::online;
    using namespace djmeta::online::discogs;
    if(state.discogs_request || state.musicbrainz_request)
        throw std::runtime_error("Another provider request is still in progress.");
    if(!state.show_candidate || !state.discogs_live_view ||
       state.musicbrainz_release_loaded ||
       state.candidate_source_index!=state.selected_track_index ||
       state.selected_track_index>=state.entries.size())
        throw std::invalid_argument("Run a Discogs search for this source first.");
    const int selected=ListView_GetNextItem(state.metadata_list,-1,LVNI_SELECTED);
    if(selected<0 || static_cast<std::size_t>(selected)>=state.candidate_view_order.size())
        throw std::invalid_argument("Select a concrete Discogs release first.");
    const auto rowid=state.candidate_view_order[static_cast<std::size_t>(selected)];
    if(rowid>=state.candidate_rows.size())
        throw std::invalid_argument("Selected Discogs result expired.");
    const auto source_id=state.candidate_rows[rowid].candidate.source_id;
    if(source_id.starts_with("master:")) {
        // A master is a navigation target only. This explicit Load action
        // searches its official bounded versions page, not release details.
        const auto digits=std::string_view(source_id).substr(7);
        std::uint64_t master_id=0;
        const auto [end,ec]=std::from_chars(
            digits.data(),digits.data()+digits.size(),master_id);
        if(ec!=std::errc{} || end!=digits.data()+digits.size() || !master_id ||
           source_id!="master:"+std::to_string(master_id))
            throw std::invalid_argument("Invalid Discogs master identity.");
        const bool verified=std::any_of(
            state.discogs_results.begin(),state.discogs_results.end(),
            [master_id](const auto& hit) {
                return hit.kind==EntityKind::Master && hit.id==master_id;
            });
        if(!verified)
            throw std::invalid_argument("Discogs master was not in the current results.");
        const auto& entry=state.entries[state.selected_track_index];
        verify_snapshot(entry);
        verify_rules_snapshot(state.captured_rules);
        const auto url=from_utf8(canonical_url(EntityKind::Master,master_id));
        SetDlgItemTextW(dialog,IDC_METADATA_MB_QUERY,url.c_str());
        perform_discogs_search(dialog,state);
        return;
    }
    if(!source_id.starts_with("release:"))
        throw std::invalid_argument("Selected Discogs result is not a release.");
    const auto digits=std::string_view(source_id).substr(8);
    std::uint64_t id=0;
    const auto [end,ec]=std::from_chars(digits.data(),digits.data()+digits.size(),id);
    if(ec!=std::errc{} || end!=digits.data()+digits.size() || !id)
        throw std::invalid_argument("Discogs release identity is invalid.");
    const bool verified=std::any_of(
        state.discogs_results.begin(),state.discogs_results.end(),
        [id](const auto& hit) {
            return hit.kind==EntityKind::Release && hit.id==id;
        });
    if(!verified)
        throw std::invalid_argument("Selected Discogs release was not in the current result set.");
    const auto& entry=state.entries[state.selected_track_index];
    verify_snapshot(entry);
    verify_rules_snapshot(state.captured_rules);
    std::string cue_before;
    if(state.discogs_search_was_cue) {
        const auto raw=read_cue_raw_on_demand(entry.handle,entry.input.source_path);
        const auto cue=djmeta::inspect_cue_metadata(raw.raw_text,raw.carrier);
        if(cue.status!=djmeta::CueSyntaxStatus::Parsed)
            throw std::invalid_argument("CUE changed or cannot be parsed.");
        cue_before=raw.raw_text;
    } else if(!entry.input.physical_source_qualified ||
              is_external_cue_locator(entry.input.source_path)) {
        throw std::invalid_argument("Discogs physical selection is unqualified.");
    }
    std::optional<ReleaseEdition> cached;
    if(state.discogs_direct_edition &&
       state.discogs_direct_edition->edition_id==std::to_string(id))
        cached=state.discogs_direct_edition;
    PendingDiscogsContext context;
    context.kind=PendingDiscogsContext::Kind::Release;
    context.source_index=state.selected_track_index;
    context.release_id=id;
    context.cue_mode=state.discogs_search_was_cue;
    context.cue_before=std::move(cue_before);
    context.context_version=state.online_context_version.current();
    if(cached) {
        publish_discogs_release(dialog,state,context,*cached);
    } else {
        SetDlgItemTextW(dialog,IDC_BATCH_HINT,
            L"Retrieving Discogs edition asynchronously; foobar remains usable...");
        auto token=to_utf8(read_control(dialog,IDC_METADATA_DISCOGS_TOKEN));
        begin_discogs_network(dialog,state,release_path(id),std::move(token),
                              std::move(context));
    }
}

void publish_discogs_release(HWND dialog, PreviewState& state,
                             const PendingDiscogsContext& request,
                             const djmeta::online::ReleaseEdition& edition) {
    (void)dialog;
    if(!state.online_context_version.matches(request.context_version) ||
       state.stale || state.refreshing ||
       !state.show_candidate || !state.discogs_provider_selected ||
       !state.discogs_live_view || state.musicbrainz_release_loaded ||
       state.candidate_source_index!=request.source_index ||
       state.selected_track_index!=request.source_index ||
       request.source_index>=state.entries.size() ||
       edition.provider!="discogs" ||
       edition.edition_id!=std::to_string(request.release_id))
        throw std::runtime_error("Discogs edition discarded: source or release changed.");
    // The highlighted release can change while HTTPS runs even when the
    // local source and provider remain unchanged. Never present a response
    // under a different selected candidate's field provenance.
    const int selected=ListView_GetNextItem(
        state.metadata_list,-1,LVNI_SELECTED);
    if(selected<0 ||
       static_cast<std::size_t>(selected)>=state.candidate_view_order.size())
        throw std::runtime_error(
            "Discogs release selection changed during lookup.");
    const auto row=state.candidate_view_order[
        static_cast<std::size_t>(selected)];
    if(row>=state.candidate_rows.size() ||
       !djmeta::online::discogs::selected_exact_release_id(
           state.candidate_rows[row].candidate.source_id,
           request.release_id))
        throw std::runtime_error(
            "Discogs release selection changed during lookup.");
    const auto& entry=state.entries[request.source_index];
    verify_online_source_snapshot(state, entry);
    verify_rules_snapshot(state.captured_rules);
    std::optional<djmeta::CueMetadataInventory> cue;
    djmeta::MetadataDocument originals;
    if(request.cue_mode) {
        const auto raw=read_cue_raw_on_demand(entry.handle,entry.input.source_path);
        if(raw.raw_text!=request.cue_before)
            throw std::runtime_error("CUE changed during Discogs release lookup.");
        cue=djmeta::inspect_cue_metadata(raw.raw_text,raw.carrier);
        if(cue->status!=djmeta::CueSyntaxStatus::Parsed)
            throw std::invalid_argument("CUE changed or cannot be parsed.");
    } else {
        if(!entry.input.physical_source_qualified ||
           is_external_cue_locator(entry.input.source_path))
            throw std::invalid_argument("Discogs physical selection unqualified.");
        const auto info=entry.handle->get_info_ref();
        originals=metadata_from_file_info(info->info());
    }
    std::int64_t physical_duration_ms=-1;
    if(!cue) {
        const auto info=entry.handle->get_info_ref();
        const double seconds=info->info().get_length();
        if(seconds>0.0 && seconds<=36000.0)
            physical_duration_ms=static_cast<std::int64_t>(
                seconds*1000.0+0.5);
    }
    auto flat=cue?discogs_cue_release_rows(*cue,edition):
        djmeta::online::review_discogs_physical_edition(
            originals,edition,physical_duration_ms);
    // Keep batch diagnostics OUT of the selected track's staging inventory.
    std::vector<djmeta::online::FieldEvidence> stageable;
    if(!cue && entry.input.physical_source_qualified &&
       entry.handle->get_subsong_index()==0)
        stageable=djmeta::online::physical_discogs_stageable_evidence(
            edition,originals,physical_duration_ms,flat);
    std::optional<std::pair<std::size_t,std::size_t>> batch_counts;
    if(!cue && state.entries.size()>1) {
        // A mixed physical/CUE/virtual or oversized album must explain WHY
        // the 2-128 physical match was skipped, never silently disappear.
        // No 54k-source enumeration for an oversized online selection.
        std::vector<djmeta::online::PhysicalAlbumBatchSourceScope> scope_sources;
        if(state.entries.size()<=128) {
            scope_sources.reserve(state.entries.size());
            for(std::size_t i=0;i<state.entries.size();++i) {
                const auto& source=state.entries[i];
                scope_sources.push_back({i,source.input.physical_source_qualified,
                    source.handle->get_subsong_index()==0,
                    is_external_cue_locator(source.input.source_path)});
            }
        }
        const auto scope=djmeta::online::review_physical_album_batch_scope(
            state.entries.size(),scope_sources,"discogs",edition.edition_id);
        if(!scope.eligible) {
            batch_counts=std::pair{std::size_t{0},state.entries.size()};
            flat.insert(flat.end(),scope.rows.begin(),scope.rows.end());
        } else {
            std::vector<djmeta::online::PhysicalBatchTrack> sources;
            sources.reserve(state.entries.size());
            for(std::size_t i=0;i<state.entries.size();++i) {
                const auto& source=state.entries[i];
                verify_online_source_snapshot(state,source);
                const auto observed=source.handle->get_info_ref();
                const double duration=observed->info().get_length();
                const std::int64_t ms=duration>0.0 && duration<=36000.0
                    ? static_cast<std::int64_t>(duration*1000.0+0.5):-1;
                sources.push_back({i,metadata_from_file_info(observed->info()),
                                   ms,true});
            }
            const auto batch=djmeta::online::review_discogs_physical_batch(
                sources,edition);
            batch_counts=std::pair{batch.matched,sources.size()};
            flat.insert(flat.end(),batch.rows.begin(),batch.rows.end());
            for(const auto& source:state.entries)
                verify_online_source_snapshot(state,source);
        }
    }
    // The exact official edition and original physical CUE text have both
    // passed the source recheck; create a detached in-memory field preview.
    std::optional<djmeta::online::CuePostimageReviewSnapshot> cue_preview;
    if(cue) {
        const auto source=read_cue_raw_on_demand(
            entry.handle,entry.input.source_path);
        if(source.raw_text!=request.cue_before)
            throw std::runtime_error("CUE changed before edition preview.");
        auto candidate=djmeta::online::capture_qualified_cue_edition_postimage(
            source.raw_text,source.carrier,edition);
        if(candidate.eligible) cue_preview.emplace(std::move(candidate));
    }
    auto grouped=group_musicbrainz_release_rows(flat);
    verify_online_source_snapshot(state, entry);
    verify_rules_snapshot(state.captured_rules);
    // Store verified immutable field evidence, but ONLY for qualified
    // physical sources. A Discogs CUE release still has full read-only
    // comparison, and can never use the physical online staging controls.
    std::optional<djmeta::online::OnlineFieldReviewSnapshot> physical_review;
    if(!cue && entry.input.physical_source_qualified &&
       entry.handle->get_subsong_index()==0) {
        if(!stageable.empty())
            physical_review=djmeta::online::capture_online_field_review(
                originals,stageable,true,true);
    }
    state.cue_value_preview.reset();
    state.cue_value_preview_indices.clear();
    state.cue_postimage_snapshot=std::move(cue_preview);
    state.cue_postimage_source=state.cue_postimage_snapshot
        ? request.source_index:(std::numeric_limits<std::size_t>::max)();
    state.cue_postimage_discogs=static_cast<bool>(state.cue_postimage_snapshot);
    state.online_physical_edition=physical_review
        ? std::optional<djmeta::online::ReleaseEdition>{edition}
        : std::nullopt;
    state.online_field_snapshot=std::move(physical_review);
    state.online_included_fields.clear();
    state.online_date_targets.clear();
    state.online_field_preview.reset();
    state.online_raw_route_valid=false;
    state.online_raw_route_before.clear();
    state.online_raw_route_after.clear();
    state.online_raw_target_observation.reset();
    state.online_edition_source=state.online_physical_edition
        ? request.source_index:(std::numeric_limits<std::size_t>::max)();
    state.candidate_rows=std::move(grouped.summary);
    state.musicbrainz_detail_groups=std::move(grouped.detail);
    state.musicbrainz_release_loaded=true;
    update_live_provider_controls(state); // no paging after exact edition
    state.search_attempt_notice=L"Exact Discogs edition "+
        std::to_wstring(request.release_id)+
        L" loaded. Unverified track fields remain blocked. Read-only.";
    if(batch_counts)
        state.search_attempt_notice+=L" Album selection: "+
            std::to_wstring(batch_counts->first)+L"/"+
            std::to_wstring(batch_counts->second)+
            L" uniquely matched; see Batch details (no bulk approval).";
    ListView_SetItemState(state.metadata_list,-1,0,LVIS_SELECTED);
    update_metadata_table(state);
    // Master lookup is a distinct official GET, initiated only as part of
    // this explicit exact-release Load action, never by browsing or focus.
    if(state.online_field_snapshot && !edition.master_id.empty()) {
        std::uint64_t id=0;
        const auto [end,ec]=std::from_chars(
            edition.master_id.data(),
            edition.master_id.data()+edition.master_id.size(),id);
        if(ec==std::errc{} &&
           end==edition.master_id.data()+edition.master_id.size() &&
           id>0 && id<=999999999999ULL) {
            auto token=to_utf8(read_control(dialog,IDC_METADATA_DISCOGS_TOKEN));
            if(!token.empty()) {
                PendingDiscogsContext ctx;
                ctx.kind=PendingDiscogsContext::Kind::MasterYear;
                ctx.source_index=request.source_index;
                ctx.release_id=request.release_id;
                ctx.master_id=id;
                begin_discogs_network(dialog,state, 
                    djmeta::online::discogs::master_detail_path(id),
                    std::move(token),std::move(ctx));
                SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                    L"Exact edition qualified. Fetching its linked master year "
                    L"from the official API (read-only)...");
            }
        }
    }
}

void publish_discogs_master_year(
    HWND dialog,PreviewState& state,const PendingDiscogsContext& ctx,
    std::string_view json) {
    using namespace djmeta::online;
    using namespace djmeta::online::discogs;
    if(ctx.kind!=PendingDiscogsContext::Kind::MasterYear ||
       !state.online_context_version.matches(ctx.context_version) ||
       state.stale || state.refreshing || !state.show_candidate ||
       !state.discogs_provider_selected || !state.discogs_live_view ||
       !state.musicbrainz_release_loaded ||
       state.selected_track_index!=ctx.source_index ||
       state.candidate_source_index!=ctx.source_index ||
       state.online_edition_source!=ctx.source_index ||
       ctx.source_index>=state.entries.size() ||
       !state.online_physical_edition || !state.online_field_snapshot ||
       state.online_physical_edition->edition_id!=std::to_string(ctx.release_id) ||
       state.online_physical_edition->master_id!=std::to_string(ctx.master_id))
        throw std::invalid_argument("Stale or unqualified Discogs master-year response");
    const auto candidate=parse_master_year(json,ctx.master_id);
    if(!candidate) {
        update_metadata_table(state);
        SetDlgItemTextW(dialog,IDC_BATCH_HINT,
            L"Discogs master year unavailable. Existing tags unchanged; "
            L"the edition's separate date remains selectable.");
        return;
    }
    if(!verified_discogs_master_year(*state.online_physical_edition,*candidate))
        throw std::invalid_argument("Discogs master date provenance is unqualified");
    const auto& entry=state.entries[ctx.source_index];
    verify_online_source_snapshot(state,entry);
    verify_rules_snapshot(state.captured_rules);
    if(!entry.input.physical_source_qualified ||
       entry.handle->get_subsong_index()!=0 ||
       is_external_cue_locator(entry.input.source_path))
        throw std::invalid_argument("Master-year stage requires physical audio");
    const auto info=entry.handle->get_info_ref();
    const auto current=metadata_from_file_info(info->info());
    if(current!=state.online_field_snapshot->physical_before)
        throw std::invalid_argument("Physical tag snapshot changed during master lookup");
    const double seconds=info->info().get_length();
    const auto duration=seconds>0 && seconds<=36000
        ? static_cast<std::int64_t>(seconds*1000.0+0.5)
        : std::int64_t{-1};
    if(!independently_verified_discogs_track(
           current,*state.online_physical_edition,duration))
        throw std::invalid_argument("Master-year physical recording no longer verified");
    auto evidence=state.online_field_snapshot->evidence;
    if(evidence.size()>=1024 ||
       std::find(evidence.begin(),evidence.end(),*candidate)!=evidence.end())
        throw std::invalid_argument("Duplicate or excessive master-year evidence");
    evidence.push_back(*candidate);
    auto snapshot=capture_online_field_review(current,evidence,true,true);
    auto reviewed=review_online_fields(current,{*candidate});
    if(reviewed.size()!=1 || reviewed.front().state==FieldReviewState::Blocked)
        throw std::invalid_argument("Master-year field review refused");
    reviewed.front().field="Discogs Master / DATE";
    reviewed.front().reason="discogs_live_master_year_verified_"+
                             reviewed.front().reason;
    auto grouped=group_musicbrainz_release_rows(reviewed);
    if(grouped.summary.size()!=1 || grouped.detail.size()!=1)
        throw std::invalid_argument("Master date review group ambiguous");
    std::optional<OnlineFieldStagedPreview> preserved;
    if(!state.online_included_fields.empty())
        preserved=stage_selected_online_fields(
            snapshot,current,state.online_included_fields,
            state.online_date_targets);
    state.online_field_snapshot=std::move(snapshot);
    state.online_field_preview=std::move(preserved);
    state.candidate_rows.push_back(std::move(grouped.summary.front()));
    state.musicbrainz_detail_groups.push_back(
        std::move(grouped.detail.front()));
    update_metadata_table(state);
    SetDlgItemTextW(dialog,IDC_BATCH_HINT,
        L"Master year and exact edition date are separate. "
        L"Select a destination tag for each (preview only, no writes).");
}

void consume_discogs_network(HWND dialog, PreviewState& state) {
    auto job=state.discogs_request;
    if(!job || !readonly_network_finished(*job)) return;
    if(job->worker.joinable()) job->worker.join();
    auto context=std::move(state.discogs_context);
    state.discogs_request.reset();
    state.discogs_context.reset();
    const bool cancelled=state.online_user_cancel_requested;
    state.online_user_cancel_requested=false;
    if(state.online_close_requested) {
        DestroyWindow(dialog);
        return;
    }
    EnableWindow(GetDlgItem(dialog,IDC_METADATA_MB_SEARCH),
                 native_workspace_action_ready(state.stale,true)?TRUE:FALSE);
    try {
        update_live_provider_controls(state);
        if(cancelled) {
            update_metadata_table(state);
            SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                L"Lookup cancelled. Existing comparisons and review decisions "
                L"were preserved; no files changed.");
            return;
        }
        if(context && !state.online_context_version.matches(context->context_version)) {
            // Context changes deliberately cancel this reply, without a popup.
            update_metadata_table(state);
            return;
        }
        if(!context)
            throw std::runtime_error("Discogs request lost its source context.");
        if(!job->error.empty())
            throw std::runtime_error(job->error);
        if(context->kind==PendingDiscogsContext::Kind::Search)
            publish_discogs_search(dialog,state,*context,job->response);
        else if(context->kind==PendingDiscogsContext::Kind::MasterYear)
            publish_discogs_master_year(dialog,state,*context,job->response);
        else
            publish_discogs_release(dialog,state,*context,
                djmeta::online::discogs::parse_release(
                    job->response,context->release_id));
    } catch(const std::exception& error) {
        if(state.stale) return; // Preserve disabled controls and stale warning.
        state.search_attempt_notice=L"Discogs read-only lookup failed: "+
            from_utf8(error.what())+L" No changes were made.";
        update_metadata_table(state);
        update_live_provider_controls(state); // restore gated paging on recoverable error
        SetDlgItemTextW(dialog,IDC_BATCH_HINT,state.search_attempt_notice.c_str());
    }
}

// MusicBrainz uses the same network-only joined worker as Discogs. Parsing,
// SDK access, local-CUE identity checks and presentation are main-thread only.
void begin_musicbrainz_network(HWND dialog, PreviewState& state,
                               std::string path, PendingMusicBrainzContext ctx) {
    if(state.discogs_request || state.musicbrainz_request ||
       state.beatport_public_request)
        throw std::runtime_error("Another official provider request is active.");
    ctx.context_version=state.online_context_version.current();
    auto job=start_readonly_network(dialog,
        [route=std::move(path)](std::stop_token cancel) {
            return fetch_musicbrainz_json_readonly(route,cancel);
        });
    state.musicbrainz_context.emplace(std::move(ctx));
    state.musicbrainz_request=std::move(job);
    state.online_user_cancel_requested=false;
    update_live_provider_controls(state); // actual native button: Cancel lookup
    EnableWindow(GetDlgItem(dialog,IDC_METADATA_MB_LOAD_RELEASE),FALSE);
}

void publish_musicbrainz_search(
    PreviewState& state, const PendingMusicBrainzContext& request,
    const std::string& reply) {
    if(!state.online_context_version.matches(request.context_version) ||
       state.stale || state.refreshing || !state.show_candidate ||
       state.discogs_provider_selected ||
       state.selected_track_index!=request.source_index ||
       request.source_index>=state.entries.size())
        throw std::runtime_error("MusicBrainz search result discarded after source change.");
    const auto& entry=state.entries[request.source_index];
    verify_online_source_snapshot(state, entry);
    verify_rules_snapshot(state.captured_rules);
    if(request.cue_mode && read_cue_raw_on_demand(
        entry.handle,entry.input.source_path).raw_text!=request.cue_before)
        throw std::runtime_error("CUE changed during online search. Retry.");
    if(request.exact_recording_mbid.empty() &&
       (request.plan_identity.empty() ||
        request.plan_identity!=state.search_plan_identity))
        throw std::runtime_error(
            "MusicBrainz search strategy no longer belongs to this source/query.");
    auto fresh=djmeta::online::musicbrainz::parse_search(
        reply,request.kind);
    if(!request.exact_recording_mbid.empty()) {
        if(request.kind!=djmeta::online::musicbrainz::SearchKind::Recording ||
           !djmeta::online::musicbrainz::valid_mbid(
               request.exact_recording_mbid))
            throw std::runtime_error("Exact recording URL context is invalid.");
        fresh.candidates.erase(std::remove_if(
            fresh.candidates.begin(),fresh.candidates.end(),
            [&](const auto& candidate) {
                return candidate.kind!=request.kind ||
                    candidate.mbid!=request.exact_recording_mbid;
            }),fresh.candidates.end());
    }
    const bool raw_stage_empty=fresh.candidates.empty();
    if(request.page>1) {
        if(request.page>djmeta::online::musicbrainz::max_search_page ||
           request.page!=state.musicbrainz_result_page+1 ||
           !state.musicbrainz_more_pages ||
           request.page_one_path!=state.musicbrainz_page_one_path ||
           request.plan_identity!=state.musicbrainz_page_identity ||
           request.attempt_index!=state.musicbrainz_published_attempt_index ||
           request.kind!=state.musicbrainz_kind ||
           !request.exact_recording_mbid.empty() ||
           fresh.offset!=static_cast<std::int64_t>(
               (request.page-1)*djmeta::online::musicbrainz::max_search_candidates))
            throw std::runtime_error(
                "MusicBrainz next page no longer matches the published search.");
    }
    const bool append_page=request.page>1 &&
        state.musicbrainz_live_view && !state.discogs_live_view &&
        !state.musicbrainz_release_loaded &&
        state.candidate_source_index==request.source_index;
    const bool append_previous=request.exact_recording_mbid.empty() &&
        request.attempt_index>0 &&
        state.musicbrainz_has_published_attempt &&
        state.musicbrainz_published_attempt_index+1==request.attempt_index &&
        state.musicbrainz_live_view && !state.discogs_live_view &&
        !state.musicbrainz_release_loaded &&
        state.musicbrainz_kind==request.kind &&
        state.candidate_source_index==request.source_index &&
        state.search_plan_identity==request.plan_identity;
    djmeta::online::musicbrainz::SearchResult previous;
    previous.kind=request.kind;
    if(append_previous || append_page)
        previous.candidates=state.musicbrainz_results;
    const auto merged=djmeta::online::musicbrainz::merge_search_stage_results(
        previous,fresh,request.strategy,append_previous || append_page);
    auto rows=musicbrainz_search_rows(
        merged.result,request.local_title,request.local_artist,request.originals);
    verify_online_source_snapshot(state, entry);
    verify_rules_snapshot(state.captured_rules);
    state.candidate_rows=std::move(rows.summary);
    state.musicbrainz_detail_groups=std::move(rows.detail);
    state.musicbrainz_original_title=request.local_title;
    state.musicbrainz_original_artist=request.local_artist;
    state.candidate_source_index=request.source_index;
    state.musicbrainz_results=merged.result.candidates;
    state.musicbrainz_has_published_attempt=true;
    state.musicbrainz_published_attempt_index=request.attempt_index;
    state.last_musicbrainz_stage_empty=raw_stage_empty;
    state.musicbrainz_page_one_path=request.page_one_path;
    state.musicbrainz_page_identity=request.plan_identity;
    state.musicbrainz_page_strategy=request.strategy;
    state.musicbrainz_result_page=request.page;
    state.musicbrainz_page_attempt_total=request.total;
    state.musicbrainz_page_cue_mode=request.cue_mode;
    state.musicbrainz_page_cue_before=request.cue_before;
    state.musicbrainz_page_originals=request.originals;
    state.musicbrainz_more_pages=
        djmeta::online::musicbrainz::musicbrainz_has_next_page(
            fresh,request.page) &&
        state.musicbrainz_results.size()<=
            djmeta::online::musicbrainz::max_search_candidates*7 &&
        !request.page_one_path.empty() &&
        request.exact_recording_mbid.empty();
    state.discogs_live_view=false;
    state.discogs_results.clear();
    state.discogs_master_id=0;
    state.discogs_master_page=state.discogs_master_pages=0;
    state.discogs_direct_edition.reset();
    state.musicbrainz_kind=request.kind;
    state.musicbrainz_live_view=true;
    state.musicbrainz_release_loaded=false;
    state.updating_musicbrainz_selection=true;
    ListView_SetItemState(state.metadata_list,-1,0,LVIS_SELECTED);
    state.updating_musicbrainz_selection=false;
    state.cue_inspection_mode=false;
    state.cue_candidate_comparison_mode=false;
    state.search_attempt_index=request.attempt_index;
    state.search_attempt_notice=
        L"MusicBrainz query "+std::to_wstring(request.attempt_index+1)+
        L"/"+std::to_wstring(request.total)+
        L", page "+std::to_wstring(request.page)+L" - "+
        from_utf8(request.strategy)+L": "+
        std::to_wstring(merged.added)+L" new, "+
        std::to_wstring(merged.repeated)+L" repeated; "+
        std::to_wstring(state.musicbrainz_results.size())+
        L" unique hits retained across stages. "+
        (merged.conflicting ? std::to_wstring(merged.conflicting)+
            L" conflicting repeated identities require review. " : L"")+
        (raw_stage_empty && request.attempt_index+1<request.total
            ? L"No hits in this stage; trying the next bounded strategy."
            : raw_stage_empty && state.musicbrainz_results.empty()
            ? L"No more search variants. Edit the title to retry."
            : request.attempt_index+1<request.total
            ? L"Compare identity evidence. Click Search for another strategy; no writes."
            : L"Compare retained identity evidence. Search restarts the plan; no writes.");
    update_metadata_table(state);
}

void publish_musicbrainz_release(
    PreviewState& state,const PendingMusicBrainzContext& request,
    const std::string& reply) {
    if(!state.online_context_version.matches(request.context_version) ||
       state.stale || state.refreshing || !state.show_candidate ||
       state.discogs_provider_selected ||
       (!request.direct_release &&
        (!state.musicbrainz_live_view || state.musicbrainz_release_loaded ||
         state.musicbrainz_kind!=
             djmeta::online::musicbrainz::SearchKind::Release ||
         state.candidate_source_index!=request.source_index)) ||
       state.selected_track_index!=request.source_index ||
       request.source_index>=state.entries.size() ||
       (request.direct_release &&
           !djmeta::online::musicbrainz::valid_mbid(request.release_mbid)))
        throw std::runtime_error("MusicBrainz release result discarded after source change.");
    const bool still_verified=std::any_of(
        state.musicbrainz_results.begin(),state.musicbrainz_results.end(),
        [&](const auto& result) {
            return result.kind==djmeta::online::musicbrainz::SearchKind::Release &&
                   result.mbid==request.release_mbid;
        });
    if(!request.direct_release && !still_verified)
        throw std::runtime_error("MusicBrainz release no longer belongs to the current search.");
    if(!request.direct_release) {
        const int selected=ListView_GetNextItem(state.metadata_list,-1,LVNI_SELECTED);
        if(selected<0 ||
           static_cast<std::size_t>(selected)>=state.candidate_view_order.size() ||
           state.candidate_view_order[static_cast<std::size_t>(selected)]>=
               state.candidate_rows.size() ||
           state.candidate_rows[
               state.candidate_view_order[static_cast<std::size_t>(selected)]]
                   .candidate.source_id!=request.release_mbid)
            throw std::runtime_error("MusicBrainz release selection changed during lookup.");
    }
    const auto& entry=state.entries[request.source_index];
    verify_online_source_snapshot(state, entry);
    verify_rules_snapshot(state.captured_rules);
    const auto release=djmeta::online::musicbrainz::parse_release_lookup(
        reply,request.release_mbid);
    std::vector<djmeta::online::FieldReviewRow> compared;
    std::optional<djmeta::online::CuePostimageReviewSnapshot> cue_preview;
    bool physical_batch_compared=false;
    if(request.cue_mode) {
        const auto raw=read_cue_raw_on_demand(
            entry.handle,entry.input.source_path);
        if(raw.raw_text!=request.cue_before)
            throw std::runtime_error(
                "CUE changed during lookup; discard stale results.");
        const auto cue=djmeta::inspect_cue_metadata(raw.raw_text,raw.carrier);
        if(cue.status!=djmeta::CueSyntaxStatus::Parsed)
            throw std::invalid_argument("MusicBrainz CUE became unqualified.");
        compared=musicbrainz_cue_release_rows(cue,release);
        auto candidate=djmeta::online::capture_qualified_cue_edition_postimage(
            raw.raw_text,raw.carrier,release);
        if(candidate.eligible) cue_preview.emplace(std::move(candidate));
    } else {
        if(!entry.input.physical_source_qualified ||
           entry.handle->get_subsong_index()!=0 ||
           is_external_cue_locator(entry.input.source_path))
            throw std::invalid_argument(
                "MusicBrainz release needs qualified physical audio or CUE.");
        const auto info=entry.handle->get_info_ref();
        const auto original=metadata_from_file_info(info->info());
        if(original!=request.originals)
            throw std::runtime_error(
                "Physical tags changed after MusicBrainz release search.");
        const double seconds=info->info().get_length();
        const std::int64_t ms=seconds>0.0 && seconds<=36000.0
            ? static_cast<std::int64_t>(seconds*1000.0+0.5) : -1;
        compared=djmeta::online::review_musicbrainz_physical_edition(
            original,release,ms);
        // An explicitly selected small album batch gets a whole-selection
        // exact-edition alignment, not one independent title-only guess per
        // file. Never bulk-scan a 54k library during an online lookup.
        if(state.entries.size()>1) {
            // One common explicit physical-selection scope contract for
            // MusicBrainz and Discogs; never infer host identity from tags.
            std::vector<djmeta::online::PhysicalAlbumBatchSourceScope> scope_sources;
            if(state.entries.size()<=128) {
                scope_sources.reserve(state.entries.size());
                for(std::size_t i=0;i<state.entries.size();++i) {
                    const auto& source=state.entries[i];
                    scope_sources.push_back({i,source.input.physical_source_qualified,
                        source.handle->get_subsong_index()==0,
                        is_external_cue_locator(source.input.source_path)});
                }
            }
            const auto scope=djmeta::online::review_physical_album_batch_scope(
                state.entries.size(),scope_sources,"musicbrainz",release.edition_id);
            if(!scope.eligible) {
                compared.insert(compared.end(),scope.rows.begin(),scope.rows.end());
            } else {
                std::vector<djmeta::online::PhysicalBatchTrack> sources;
                sources.reserve(state.entries.size());
                for(std::size_t i=0;i<state.entries.size();++i) {
                    const auto& source=state.entries[i];
                    verify_online_source_snapshot(state,source);
                    const auto observed=source.handle->get_info_ref();
                    const double duration=observed->info().get_length();
                    const std::int64_t milliseconds=
                        duration>0.0 && duration<=36000.0
                        ? static_cast<std::int64_t>(duration*1000.0+0.5):-1;
                    sources.push_back({i,
                        metadata_from_file_info(observed->info()),
                        milliseconds,true});
                }
                const auto batch=djmeta::online::review_musicbrainz_physical_batch(
                    sources,release);
                compared.insert(compared.end(),
                    batch.rows.begin(),batch.rows.end());
                // A changed hidden source invalidates the entire audit.
                for(const auto& source:state.entries)
                    verify_online_source_snapshot(state,source);
                physical_batch_compared=true;
            }
        }
        if(metadata_from_file_info(entry.handle->get_info_ref()->info())!=
           request.originals)
            throw std::runtime_error(
                "Physical tags changed during MusicBrainz release comparison.");
    }
    auto grouped=group_musicbrainz_release_rows(compared);
    verify_online_source_snapshot(state, entry);
    verify_rules_snapshot(state.captured_rules);
    state.candidate_rows=std::move(grouped.summary);
    state.musicbrainz_detail_groups=std::move(grouped.detail);
    if(request.direct_release) {
        // A direct URL is an explicit edition lookup, NOT a synthetic search
        // result. Only the response's exact MBID and original CUE establish
        // evidence. No physical tag stage survives a direct CUE lookup.
        state.candidate_source_index=request.source_index;
        state.musicbrainz_original_title=request.local_title;
        state.musicbrainz_original_artist=request.local_artist;
        state.musicbrainz_results.clear();
        state.musicbrainz_kind=djmeta::online::musicbrainz::SearchKind::Release;
        state.musicbrainz_live_view=true;
        state.discogs_live_view=false;
        state.discogs_results.clear();
        state.discogs_direct_edition.reset();
        state.cue_inspection_mode=false;
        state.cue_candidate_comparison_mode=false;
        state.online_physical_edition.reset();
        state.online_field_snapshot.reset();
        state.online_field_preview.reset();
        state.online_raw_target_observation.reset();
        state.online_included_fields.clear();
        state.online_date_targets.clear();
        state.online_edition_source=(std::numeric_limits<std::size_t>::max)();
        state.search_attempt_notice=L"Exact MusicBrainz release URL "+
            from_utf8(request.release_mbid)+
            L" verified against official WS/2 response and original CUE. "
            L"Read-only; compare each track/edition field before staging.";
    }
    state.cue_value_preview.reset();
    state.cue_value_preview_indices.clear();
    state.cue_postimage_snapshot=std::move(cue_preview);
    // Physical MusicBrainz releases are comparison evidence only until
    // a separately qualified source-aware field-staging adapter exists.
    state.online_physical_edition.reset();
    state.online_field_snapshot.reset();
    state.online_field_preview.reset();
    state.online_raw_target_observation.reset();
    state.online_included_fields.clear();
    state.online_date_targets.clear();
    state.online_edition_source=(std::numeric_limits<std::size_t>::max)();
    state.cue_postimage_source=state.cue_postimage_snapshot
        ? request.source_index:(std::numeric_limits<std::size_t>::max)();
    state.cue_postimage_discogs=false;
    state.musicbrainz_page_cue_mode=request.cue_mode;
    state.musicbrainz_release_loaded=true;
    if(!request.cue_mode)
        state.search_attempt_notice=physical_batch_compared
            ? L"Exact MusicBrainz edition: physical batch match, individual "
              L"source tags and original vectors compared. Incomplete album "
              L"or disputed tracks remain blocked. No staging or writes."
            : L"Exact MusicBrainz edition and physical tracklist compared. "
              L"Unmatched or unverified fields are blocked; comparison only. "
              L"No tag selection or file writes.";
    ListView_SetItemState(state.metadata_list,-1,0,LVIS_SELECTED);
    update_metadata_table(state);
}

void consume_musicbrainz_network(HWND dialog, PreviewState& state) {
    auto job=state.musicbrainz_request;
    if(!job || !readonly_network_finished(*job)) return;
    if(job->worker.joinable()) job->worker.join();
    auto request=std::move(state.musicbrainz_context);
    state.musicbrainz_request.reset();
    state.musicbrainz_context.reset();
    const bool cancelled=state.online_user_cancel_requested;
    state.online_user_cancel_requested=false;
    if(state.online_close_requested) {
        DestroyWindow(dialog);
        return;
    }
    EnableWindow(GetDlgItem(dialog,IDC_METADATA_MB_SEARCH),
                 native_workspace_action_ready(state.stale,true)?TRUE:FALSE);
    try {
        update_live_provider_controls(state);
        if(cancelled) {
            update_metadata_table(state);
            SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                L"Lookup cancelled. Existing comparisons and review decisions "
                L"were preserved; no files changed.");
            return;
        }
        if(request && !state.online_context_version.matches(request->context_version)) {
            update_metadata_table(state); // stale responses cannot interrupt foobar
            return;
        }
        if(!request)
            throw std::runtime_error("MusicBrainz request lost its context.");
        if(!job->error.empty())throw std::runtime_error(job->error);
        if(request->operation==PendingMusicBrainzContext::Kind::Search) {
            publish_musicbrainz_search(state,*request,job->response);
            // One user-initiated Search may advance through its finite
            // already-reviewed strategy plan. Unlike ordinary track
            // selection, this is a continuation of the SAME explicit user
            // action and only while its source, kind and request generation
            // remain valid. No writer and no foobar UI-thread wait.
            const bool qualified =
                !state.stale && !state.refreshing && state.show_candidate &&
                !state.discogs_provider_selected && state.musicbrainz_live_view &&
                state.musicbrainz_kind==request->kind &&
                state.selected_track_index==request->source_index &&
                state.candidate_source_index==request->source_index &&
                state.online_context_version.matches(request->context_version) &&
                !state.musicbrainz_request && !state.discogs_request;
            if(request->page==1 &&
               djmeta::online::musicbrainz::continue_empty_search_plan(
                    request->attempt_index,request->total,
                    state.last_musicbrainz_stage_empty,qualified))
                SendMessageW(dialog,WM_COMMAND,
                    MAKEWPARAM(IDC_METADATA_MB_SEARCH,BN_CLICKED),
                    reinterpret_cast<LPARAM>(
                        GetDlgItem(dialog,IDC_METADATA_MB_SEARCH)));
        } else {
            publish_musicbrainz_release(state,*request,job->response);
        }
    } catch(const std::exception& error) {
        if(state.stale) return; // Never re-enable actions after file replacement.
        state.search_attempt_notice=L"MusicBrainz read-only lookup failed: "+
            from_utf8(error.what())+L" No files were changed.";
        update_metadata_table(state);
        SetDlgItemTextW(dialog,IDC_BATCH_HINT,state.search_attempt_notice.c_str());
    }
}

void change_online_field_preview(PreviewState& state,int action) {
    if(!qualified_online_field_preview(state))
        throw std::invalid_argument(
            "Load a verified exact Discogs edition for a physical audio file first.");
    const auto& entry=state.entries[state.selected_track_index];
    verify_online_source_snapshot(state,entry);
    verify_rules_snapshot(state.captured_rules);
    const auto info=entry.handle->get_info_ref();
    const auto current=metadata_from_file_info(info->info());

    std::set<std::size_t> included(state.online_included_fields.begin(),
                                   state.online_included_fields.end());
    auto date_targets=state.online_date_targets;
    const int chosen_target=static_cast<int>(SendDlgItemMessageW(
        state.dialog,IDC_ONLINE_DATE_TARGET,CB_GETCURSEL,0,0));
    if(action==IDC_ONLINE_RESET_FIELDS) {
        included.clear();
        date_targets.clear();
    } else if(action==IDC_ONLINE_INCLUDE_FIELDS &&
              (chosen_target==6 || chosen_target==7)) {
        const auto pair=djmeta::online::discogs_dual_date_indices(
            *state.online_physical_edition,
            state.online_field_snapshot->evidence);
        if(!pair)
            throw std::invalid_argument(
                "The exact edition AND its official linked Discogs master "
                "must both provide a qualified date for paired staging.");
        // Atomic preset replaces only previous date choices, preserving
        // every independently selected nondate tag in the preview.
        for(auto it=included.begin();it!=included.end();) {
            if(*it<state.online_field_snapshot->evidence.size() &&
               state.online_field_snapshot->evidence[*it].date_meaning!=
                   djmeta::online::DateMeaning::NotDate) {
                date_targets.erase(*it);
                it=included.erase(it);
            } else ++it;
        }
        included.insert(pair->master_index);
        included.insert(pair->edition_index);
        date_targets[pair->master_index]=
            chosen_target==6?"ORIGINALDATE":"DATE";
        date_targets[pair->edition_index]="RELEASEDATE";
    } else {
        const int active=ListView_GetNextItem(state.metadata_list,-1,LVNI_SELECTED);
        if(active<0 || static_cast<std::size_t>(active)>=
                state.candidate_view_order.size())
            throw std::invalid_argument("Choose an exact edition field group first.");
        const auto group=state.candidate_view_order[static_cast<std::size_t>(active)];
        if(group>=state.musicbrainz_detail_groups.size())
            throw std::invalid_argument("Edition detail group expired.");
        const auto& rows=state.musicbrainz_detail_groups[group];
        int row=-1;
        std::size_t selected_count=0;
        while((row=ListView_GetNextItem(
                state.musicbrainz_details,row,LVNI_SELECTED))>=0) {
            if(static_cast<std::size_t>(row)>=rows.size() || row>=1024)
                throw std::invalid_argument("Visible edition field index expired.");
            const auto& detail=rows[static_cast<std::size_t>(row)];
            const bool is_date=djmeta::online::online_date_requires_explicit_target(
                detail.candidate);
            const int target_choice=chosen_target;
            static constexpr const char* target_names[]={
                "", "DATE", "RELEASEDATE", "ORIGINALDATE", "DATE_RAW", "PUBLISHDATE"};
            const bool target_chosen=target_choice>0 && target_choice<6;
            auto index=djmeta::online::physical_discogs_stageable_field_index(
                *state.online_physical_edition,
                state.online_field_snapshot->evidence,detail,
                is_date && (target_chosen || action==IDC_ONLINE_IGNORE_FIELDS));
            if(!index)
                index=djmeta::online::physical_discogs_master_field_index(
                    *state.online_physical_edition,
                    state.online_field_snapshot->evidence,detail,
                    is_date && (target_chosen || action==IDC_ONLINE_IGNORE_FIELDS));
            if(!index)
                throw std::invalid_argument(
                    "Only verified edition, independently linked master year "
                    "and matched release track fields are selectable. "
                    "CUE, search, blocked or local-only tags cannot be staged.");
            if(action==IDC_ONLINE_INCLUDE_FIELDS) {
                included.insert(*index);
                if(is_date) date_targets[*index]=target_names[target_choice];
            } else {
                included.erase(*index);
                date_targets.erase(*index);
            }
            ++selected_count;
        }
        if(!selected_count)
            throw std::invalid_argument(
                "Highlight one or more fields in the lower comparison table first.");
    }
    const std::vector<std::size_t> proposed(included.begin(),included.end());
    // Recompute the ENTIRE projection against the immutable physical snapshot
    // before committing any in-memory selection. An exception leaves all
    // previous selections and the original untouched.
    auto projected=djmeta::online::stage_selected_online_fields(
        *state.online_field_snapshot,current,proposed,date_targets);
    // Independent postimage safety boundary: preserve ALL original custom,
    // multi-value and embedded-CUE metadata. Changes must be exactly the
    // user-selected provider fields, and tied to host-observed physical ID.
    const auto latest=probe_host_file_readonly(entry.input.source_path);
    if(latest.state!=HostFileState::ExistingFile ||
       latest.host_physical_key!=entry.observed_physical_key ||
       latest.source_guard!=entry.observed_source_guard)
        throw std::runtime_error(
            "Physical audio changed during online tag postimage preview.");
    const djmeta::PhysicalTagSourceSnapshot physical{
        entry.input.physical_source_qualified,
        entry.handle->get_subsong_index()!=0,
        entry.observed_physical_key,entry.observed_source_guard,current
    };
    const auto qualified=djmeta::online::verify_online_physical_tag_postimage(
        physical,current,latest.host_physical_key,latest.source_guard,
        *state.online_field_snapshot,proposed,date_targets,projected);
    if(!qualified.eligible)
        throw std::invalid_argument(
            "Online physical tag postimage refused: "+qualified.rejection);
    // The exact same original tags underpin the already accepted local
    // normalizer decisions and this verified Discogs edition selection.
    // Produce ONE detached what-if postimage. Conflict blocks the selection
    // before the native UI commits it; no batch approval is granted here.
    djmeta::UnifiedReviewBatchReprobe unified;
    unified.normalizer.live.captured=physical;
    unified.normalizer.live.fresh_tags=current;
    unified.normalizer.live.live_physical_key=latest.host_physical_key;
    unified.normalizer.live.live_source_guard=latest.source_guard;
    unified.normalizer.captured_analysis=
        state.analyses[state.selected_track_index];
    unified.normalizer.active_ruleset_revision=
        state.analyses[state.selected_track_index].ruleset_revision;
    unified.normalizer.decisions=
        state.review_decisions[state.selected_track_index];
    unified.online_capture=*state.online_field_snapshot;
    unified.selected_online_indices=proposed;
    unified.explicit_date_targets=date_targets;
    const auto combined=djmeta::preview_unified_review_physical_tags(unified);
    if(!combined.physical.eligible)
        throw std::invalid_argument(
            "Selected online fields conflict with accepted local changes. "
            "Resolve conflicting tag before including it: "+
            combined.rejection);
    // Construct the actual detached foobar SDK file_info postimage using ONLY
    // verified edits. This is a copy/roundtrip integrity gate; no metadb_io,
    // input writer, filesystem or playlist metadata mutation is invoked.
    const auto approved_sdk_fields=djmeta::physical_sdk_changed_field_approvals(
        current,combined.physical.planned);
    file_info_impl sdk_postimage;
    const auto sdk_projection=djmeta::project_explicit_physical_sdk_postimage(
        info->info(),sdk_postimage,physical,latest.host_physical_key,
        latest.source_guard,approved_sdk_fields,combined.physical,
        [](const file_info& source) { return metadata_from_file_info(source); });
    if(sdk_projection.status==djmeta::PhysicalSdkPostimageStatus::Blocked ||
       (sdk_projection.status==djmeta::PhysicalSdkPostimageStatus::Detached)!=
           combined.physical.changed)
        throw std::runtime_error(
            "Native physical SDK postimage refused: "+sdk_projection.rejection);
    if(combined.physical.changed && !file_info::g_is_info_equal(
            info->info(),sdk_postimage))
        throw std::runtime_error(
            "SDK projection unexpectedly altered technical file information.");
    // Both values come from foobar's own Title Formatting runtime,
    // evaluated against the identical physical file_info. No file move,
    // target overwrite or tag write can occur via this preview.
    std::string raw_before,raw_after;
    bool raw_route_valid=false;
    try {
        const auto typed_route=read_choice(state.dialog);
        if(typed_route.display_name==state.current_choice.display_name &&
           typed_route.destination_root==state.current_choice.destination_root &&
           typed_route.titleformat_expression==state.current_choice.titleformat_expression &&
           entry.input.profile==typed_route.display_name &&
           entry.input.destination_root==typed_route.destination_root &&
           entry.route_expression==typed_route.titleformat_expression &&
           djmeta::raw_destination_root_lexically_safe(
                typed_route.destination_root)) {
            raw_before=evaluate_titleformat_against_canonical(
                entry.handle->get_location(),info->info(),current,
                typed_route.titleformat_expression);
            raw_after=evaluate_titleformat_against_canonical(
                entry.handle->get_location(),info->info(),combined.physical.planned,
                typed_route.titleformat_expression);
            raw_route_valid=raw_before.size()<=2048 && raw_after.size()<=2048 &&
                djmeta::raw_relative_path_lexically_safe(raw_before) &&
                djmeta::raw_relative_path_lexically_safe(raw_after);
        }
    } catch(const std::exception&) {
        // Unqualified expressions do not invalidate approved provider
        // evidence; they only suppress the optional naming what-if.
        raw_route_valid=false;
    }
    state.online_raw_route_valid=raw_route_valid;
    state.online_raw_route_before=raw_route_valid?std::move(raw_before):std::string{};
    state.online_raw_route_after=raw_route_valid?std::move(raw_after):std::string{};
    state.online_included_fields=proposed;
    state.online_date_targets=std::move(date_targets);
    state.online_field_preview=std::move(projected);
    state.online_raw_target_observation.reset();
    // The File Locations table consumes the SAME verified joint raw candidate.
    // This rebuild also checks collisions against every other batch source;
    // no target filesystem observation is trusted or performed here.
    update_table(state);
    refresh_musicbrainz_detail_rows(state);
    const auto& result=*state.online_field_preview;
    const bool year_only_date_target=std::any_of(
        state.online_date_targets.begin(),state.online_date_targets.end(),
        [](const auto& choice){return choice.second=="DATE";});
    const std::wstring notice=L"Online tag preview: "+
        std::to_wstring(state.online_included_fields.size())+
        L" selected, "+std::to_wstring(result.explicitly_changed)+
        L" proposed changes, "+std::to_wstring(result.exact_noops)+
        L" no-ops (memory only). Combined local + online postimage: "+
        std::to_wstring(combined.physical.explicitly_changed)+
        L" changed physical fields. No writes."+
        (year_only_date_target
            ? std::wstring(L" DATE uses YYYY; complete provider date preserved.")
            : std::wstring{})+
        (state.online_raw_route_valid
            ? (state.online_raw_route_before==state.online_raw_route_after
                ? std::wstring(L" Raw filename unchanged.")
                : std::wstring(L" Raw filename delta shown in comparison table."))
            : std::wstring(L" Naming what-if needs a qualified route."));
    SetDlgItemTextW(state.dialog,IDC_BATCH_HINT,notice.c_str());
}

INT_PTR CALLBACK batch_dialog_proc(HWND dialog, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_INITDIALOG) {
        auto* state = reinterpret_cast<PreviewState*>(lp);
        g_pending_state=nullptr; // WM_NCDESTROY now owns and frees this state.
        g_workspace=dialog;
        state->workspace_generation=++g_workspace_generation;
        state->dialog=dialog;
        SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        try {
        state->dark.AddDialogWithControls(dialog);
        state->list = GetDlgItem(dialog, IDC_BATCH_LIST);
        state->history_list = GetDlgItem(dialog, IDC_BATCH_HISTORY_LIST);
        state->metadata_list = GetDlgItem(dialog, IDC_METADATA_LIST);
        state->musicbrainz_details = GetDlgItem(dialog, IDC_METADATA_MB_DETAILS);
        state->musicbrainz_details_label = GetDlgItem(dialog, IDC_METADATA_MB_DETAIL_LABEL);
        state->metadata_track_list = GetDlgItem(dialog, IDC_METADATA_TRACK_LIST);
        state->metadata_filter = GetDlgItem(dialog, IDC_METADATA_FILTER);
        state->metadata_track_filter = GetDlgItem(dialog, IDC_METADATA_TRACK_FILTER);
        state->metadata_scope = GetDlgItem(dialog, IDC_METADATA_REVIEW_SCOPE);
        state->tabs = GetDlgItem(dialog, IDC_BATCH_TABS);
        if (!state->list || !state->history_list ||
            !GetDlgItem(dialog,IDC_BATCH_HISTORY_FILTER) ||
            !state->metadata_list || !state->musicbrainz_details ||
            !state->musicbrainz_details_label || !state->metadata_track_list ||
            !state->metadata_filter || !state->metadata_track_filter ||
            !state->metadata_scope || !state->tabs)
            throw std::runtime_error("Required native dialog controls missing.");
        for (const wchar_t* name : {L"1. Find & compare", L"2. Review proposals",
                                    L"3. Plan file locations", L"4. History"}) {
            TCITEMW tab{};
            tab.mask = TCIF_TEXT;
            tab.pszText = const_cast<wchar_t*>(name);
            TabCtrl_InsertItem(state->tabs, TabCtrl_GetItemCount(state->tabs), &tab);
        }
        const auto workspace_defaults = load_workspace_defaults();
        TabCtrl_SetCurSel(state->tabs, workspace_defaults.opening_tab);
        ListView_SetExtendedListViewStyle(state->history_list,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        add_column(state->history_list,0,L"Step",54);
        add_column(state->history_list,1,L"Action",153);
        add_column(state->history_list,2,L"Scope",143);
        add_column(state->history_list,3,L"Tracks",76);
        add_column(state->history_list,4,L"Items",76);
        for(const wchar_t* choice:{L"All actions",L"Review decisions",
            L"Route previews",L"Undo actions"})
            SendDlgItemMessageW(dialog,IDC_BATCH_HISTORY_FILTER,CB_ADDSTRING,
                0,reinterpret_cast<LPARAM>(choice));
        SendDlgItemMessageW(dialog,IDC_BATCH_HISTORY_FILTER,CB_SETCURSEL,0,0);
        ListView_SetExtendedListViewStyle(state->metadata_track_list,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP |
            LVS_EX_HEADERDRAGDROP);
        add_column(state->metadata_track_list, 0, L"Track", 173);
        add_column(state->metadata_track_list, 1, L"Music", 40);
        add_column(state->metadata_track_list, 2, L"Other", 40);
        add_column(state->metadata_track_list, 3, L"Review", 45);
        state->track_grid = load_track_grid_layout();
        apply_review_grid_controls(state->metadata_track_list, state->track_grid,
                                   current_dpi(state->metadata_track_list));
        ListView_SetExtendedListViewStyle(state->metadata_list,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP |
            LVS_EX_HEADERDRAGDROP);
        ListView_SetExtendedListViewStyle(state->musicbrainz_details,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_HEADERDRAGDROP);
        add_column(state->musicbrainz_details, 0, L"Field", 92);
        add_column(state->musicbrainz_details, 1, L"Original", 75);
        add_column(state->musicbrainz_details, 2, L"Suggested", 88);
        add_column(state->musicbrainz_details, 3, L"Status", 65);
        state->online_detail_grid=load_online_detail_grid_layout();
        state->online_detail_has_saved=has_saved_online_detail_grid_layout();
        add_column(state->metadata_list, 0, L"Field", 78);
        add_column(state->metadata_list, 1, L"Original", 95);
        add_column(state->metadata_list, 2, L"Proposed", 95);
        add_column(state->metadata_list, 3, L"Safety", 57);
        add_column(state->metadata_list, 4, L"Decision", 68);
        state->detail_grid = load_detail_grid_layout();
        state->candidate_track_grid = load_candidate_track_grid_layout();
        state->candidate_detail_grid = load_candidate_detail_grid_layout();
        apply_review_grid_controls(state->metadata_list, state->detail_grid,
                                   current_dpi(state->metadata_list));
        for (const wchar_t* focus : {L"Music proposals", L"Extended proposals",
                                      L"All proposals", L"All current tags (read-only)"})
            SendDlgItemMessageW(dialog, IDC_METADATA_FILTER, CB_ADDSTRING,
                0, reinterpret_cast<LPARAM>(focus));
        SendDlgItemMessageW(dialog, IDC_METADATA_FILTER, CB_SETCURSEL, 0, 0);
        for (const wchar_t* status : {L"All tracks", L"With proposals",
                                      L"Pending REVIEW", L"Pending any",
                                      L"Staged edits", L"Review completed",
                                      L"No proposals"})
            SendDlgItemMessageW(dialog, IDC_METADATA_TRACK_FILTER, CB_ADDSTRING,
                0, reinterpret_cast<LPARAM>(status));
        SendDlgItemMessageW(dialog, IDC_METADATA_TRACK_FILTER, CB_SETCURSEL, 0, 0);
        for (const wchar_t* scope : {L"Selected changes", L"Selected tracks",
                                     L"All tracks", L"Visible queue",
                                      L"Field: visible", L"Field: all"})
            SendDlgItemMessageW(dialog, IDC_METADATA_REVIEW_SCOPE, CB_ADDSTRING,
                0, reinterpret_cast<LPARAM>(scope));
        SendDlgItemMessageW(dialog, IDC_METADATA_REVIEW_SCOPE, CB_SETDROPPEDWIDTH, 175, 0);
        SendDlgItemMessageW(dialog, IDC_METADATA_REVIEW_SCOPE, CB_SETCURSEL, 0, 0);
        SendDlgItemMessageW(dialog, IDC_METADATA_MANUAL_INPUT, EM_LIMITTEXT, 16384, 0);
        // Do not truncate long, valid Beatport track slugs at the old MB text cap.
        SendDlgItemMessageW(dialog, IDC_METADATA_MB_QUERY, EM_LIMITTEXT,
            djmeta::online::maximum_public_beatport_url_length, 0);
        SendDlgItemMessageW(dialog, IDC_METADATA_DISCOGS_TOKEN, EM_LIMITTEXT, 256, 0);
        for(const wchar_t* target:{
                L"Keep original date",L"DATE (year YYYY; explicit replace)",
                L"RELEASEDATE",L"ORIGINALDATE",L"DATE_RAW",L"PUBLISHDATE",
                L"PAIR: Master -> ORIGINALDATE, edition -> RELEASEDATE",
                L"PAIR: Master -> DATE, edition -> RELEASEDATE"})
            SendDlgItemMessageW(dialog,IDC_ONLINE_DATE_TARGET,CB_ADDSTRING,
                0,reinterpret_cast<LPARAM>(target));
        const int remembered_date=load_online_date_target_preference();
        SendDlgItemMessageW(dialog,IDC_ONLINE_DATE_TARGET,
            CB_SETCURSEL,remembered_date,0);
        SetDlgItemTextW(dialog,IDC_ONLINE_INCLUDE_FIELDS,
            remembered_date>=6 ? L"Preview both dates" :
                                 L"Include fields (preview)");
        SendDlgItemMessageW(dialog,IDC_ONLINE_DATE_TARGET,
            CB_SETDROPPEDWIDTH,360,0);
        for(const wchar_t* name:{L"MusicBrainz",L"Discogs (token)",
                                 L"Beatport + Crates"})
            SendDlgItemMessageW(dialog,IDC_METADATA_ONLINE_PROVIDER,CB_ADDSTRING,
                0,reinterpret_cast<LPARAM>(name));
        // Workspace 'first available' honors the profile's ranking while
        // preserving the existing explicit-only provider search workflow.
        // Deezer has no native lookup yet and is never auto-selected here.
        const int search_mode=workspace_defaults.online_provider == 3
            ? djmeta::online::first_supported_search_mode(
                load_online_provider_preferences())
            : workspace_defaults.online_provider;
        SendDlgItemMessageW(dialog,IDC_METADATA_ONLINE_PROVIDER,
            CB_SETCURSEL,search_mode,0);
        state->discogs_provider_selected = search_mode == 1;
        state->beatport_offline_selected = search_mode == 2;
        SendDlgItemMessageW(dialog, IDC_METADATA_MB_QUERY, EM_SETCUEBANNER,
            FALSE, reinterpret_cast<LPARAM>(
                state->beatport_offline_selected
                    ? L"Exact Beatport URL (or paste from clipboard)"
                    : state->discogs_provider_selected
                        ? L"Title or Discogs release/master URL"
                        : L"TITLE, album: NAME or MusicBrainz URL"));
        state->show_whitespace = workspace_defaults.show_whitespace;
        SendDlgItemMessageW(dialog, IDC_METADATA_VISIBLE_WHITESPACE,
            BM_SETCHECK, state->show_whitespace ? BST_CHECKED : BST_UNCHECKED, 0);

        ListView_SetExtendedListViewStyle(state->list,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
            LVS_EX_INFOTIP | LVS_EX_HEADERDRAGDROP);
        add_column(state->list, 0, L"Source file", 170);
        add_column(state->list, 1, L"Profile", 82);
        add_column(state->list, 2, L"Proposed path (unverified)", 275);
        add_column(state->list, 3, L"Status", 160);
        for(const wchar_t* choice:{L"All file rows",L"Raw collisions",
            L"Unsafe / unqualified raw",L"Existing raw target",
            L"Raw target not inspected",L"Missing raw target",
            L"Pending metadata",L"No raw findings (NOT approved)",
            L"File action: BLOCKED",L"File action: RAW ONLY",
            L"File action: SKIP",L"CUE: all carriers",
            L"CUE: inspect / blocked",L"CUE: RAW only",
            L"CUE: original kept"})
            SendDlgItemMessageW(dialog,IDC_BATCH_LOCATION_FILTER,CB_ADDSTRING,
                0,reinterpret_cast<LPARAM>(choice));
        SendDlgItemMessageW(dialog,IDC_BATCH_LOCATION_FILTER,CB_SETCURSEL,0,0);
        for(const wchar_t* action:{L"Move",L"Copy",L"Rename",L"No file action"})
            SendDlgItemMessageW(dialog,IDC_BATCH_FILE_ACTION,CB_ADDSTRING,
                0,reinterpret_cast<LPARAM>(action));
        SendDlgItemMessageW(dialog,IDC_BATCH_FILE_ACTION,CB_SETCURSEL,0,0);
        state->layout = load_batch_table_layout();
        apply_column_layout(*state);
        const HWND batch_header = ListView_GetHeader(state->list);
        if (batch_header &&
            !SetWindowSubclass(batch_header, batch_header_proc, 1,
                reinterpret_cast<DWORD_PTR>(state)))
            throw std::runtime_error("Unable to attach batch column menu.");
        for (const HWND grid : {state->metadata_track_list, state->metadata_list}) {
            const HWND header = ListView_GetHeader(grid);
            if (!header || !SetWindowSubclass(header, batch_header_proc, 1,
                     reinterpret_cast<DWORD_PTR>(state)))
                throw std::runtime_error("Unable to attach review column menu.");
            if(!SetWindowSubclass(grid,native_review_split_keyboard_proc,1,0))
                throw std::runtime_error("Unable to attach accessible divider keys.");
        }
        // Provider detail is another native pane on the same right-hand side.
        if(!SetWindowSubclass(state->musicbrainz_details,
                native_review_split_keyboard_proc,1,0))
            throw std::runtime_error("Unable to attach online detail divider keys.");
        for (const int id : {IDC_BATCH_DESTINATION, IDC_BATCH_PATTERN})
            SendDlgItemMessageW(dialog, id, EM_LIMITTEXT, 16384, 0);
        SendDlgItemMessageW(dialog, IDC_BATCH_PROFILE_PICKER, CB_LIMITTEXT, 16384, 0);
        rebuild_route_picker(dialog,from_utf8(state->current_choice.display_name));
        int initial_profile = -1;
        for (std::size_t i = 0; i < legacy_move_route_count; ++i) {
            const auto& profile = legacy_move_routes[i];
            if (state->current_choice.display_name == profile.name &&
                state->current_choice.destination_root == profile.destination_root &&
                state->current_choice.titleformat_expression == profile.foobar_titleformat) {
                initial_profile = static_cast<int>(i);
                break;
            }
        }
        SendDlgItemMessageW(dialog, IDC_BATCH_PROFILE_PICKER, CB_SETCURSEL,
                            static_cast<WPARAM>(initial_profile), 0);
        SetDlgItemTextW(dialog, IDC_BATCH_PROFILE_PICKER,
                        from_utf8(state->current_choice.display_name).c_str());
        SetDlgItemTextW(dialog, IDC_BATCH_DESTINATION,
                        from_utf8(state->current_choice.destination_root).c_str());
        SetDlgItemTextW(dialog, IDC_BATCH_PATTERN,
                        from_utf8(state->current_choice.titleformat_expression).c_str());
        state->dialog = dialog;
        state->active_dpi = current_dpi(dialog);
        install_workspace_tooltips(*state);
        update_table(*state);
        update_master_table(*state);
        update_metadata_table(*state);
        state->review_split_offset_96=load_review_split_offset();
        show_preview_page(dialog, *state,
            workspace_defaults.opening_tab != 2,
            workspace_defaults.opening_tab == 0);
        if (workspace_defaults.opening_tab == 0)
            prefill_musicbrainz_query(*state,true);
        align_native_preview_form(dialog);
        capture_resize_layout(*state);
        restore_batch_dialog_window_size(*state);
        RECT initial_screen{};
        if(native_capture_normal_screen_bounds(dialog,initial_screen)) {
            state->last_normal_screen_bounds=initial_screen;
            state->has_last_normal_screen_bounds=true;
        }
        // Fit first-open headers to a bounded sample of real Unicode titles,
        // only while the profile still uses untouched factory widths.
        // Never recompute these during selection, sorting or tab navigation.
        const int native_dpi=current_dpi(state->metadata_track_list);
        const int ideal_title=fit_initial_track_title_column(
            state->metadata_track_list,state->cached_track_names,native_dpi);
        const auto track_defaults=default_track_grid_layout();
        if (state->track_grid.widths==track_defaults.widths)
            state->track_grid.widths[0]=ideal_title;
        if (state->candidate_track_grid.widths==
                (std::array<int,4>{280,75,90,85}))
            state->candidate_track_grid.widths[0]=ideal_title;
        apply_review_grid_controls(state->metadata_track_list,
            state->show_candidate ? state->candidate_track_grid:state->track_grid,
            native_dpi);
        modeless_dialog_manager::g_add(dialog);
        state->modeless_registered=true;
        return TRUE;
        } catch (...) {
            // WM_INITDIALOG returning FALSE does not abort creation. Always
            // destroy to release HWND and heap state on partial initialization.
            DestroyWindow(dialog);
            return TRUE;
        }
    }

    auto* state = reinterpret_cast<PreviewState*>(
        GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (!state) return FALSE;

    if (message == WM_NCDESTROY) {
        // No SDK or DLL code outlives the native workspace lifetime.
        // WinHTTP timeouts bound the shutdown join; workers never detach.
        // Forced host teardown can bypass Close; stop before joining.
        if(state->musicbrainz_request)
            request_readonly_network_stop(*state->musicbrainz_request);
        if(state->discogs_request)
            request_readonly_network_stop(*state->discogs_request);
        if(state->beatport_public_request)
            request_readonly_network_stop(*state->beatport_public_request);
        state->beatport_public_request.reset();
        state->beatport_public_context.reset();
        state->musicbrainz_request.reset();
        state->musicbrainz_context.reset();
        state->discogs_request.reset();
        state->discogs_context.reset();
        if (state->help_tooltips && IsWindow(state->help_tooltips)) {
            DestroyWindow(state->help_tooltips);
            state->help_tooltips=nullptr;
        }
        if (state->modeless_registered) {
            modeless_dialog_manager::g_remove(dialog);
            state->modeless_registered=false;
        }
        if (g_workspace == dialog) g_workspace=nullptr;
        SetWindowLongPtrW(dialog,GWLP_USERDATA,0);
        delete state;
        return FALSE;
    }
    if (message == kReadOnlyNetworkDoneMessage) {
        // One active official provider worker at a time. A completion may
        // destroy the HWND when Close was requested: never touch state after.
        if(state->discogs_request &&
           readonly_network_finished(*state->discogs_request))
            consume_discogs_network(dialog,*state);
        else if(state->musicbrainz_request &&
                readonly_network_finished(*state->musicbrainz_request))
            consume_musicbrainz_network(dialog,*state);
        else if(state->beatport_public_request &&
                readonly_network_finished(*state->beatport_public_request))
            consume_beatport_public_network(dialog,*state);
        return TRUE;
    }
    if (native_workspace_dismisses_tooltip(message,wp))
        dismiss_workspace_tooltips(*state);
    if(message==WM_WINDOWPOSCHANGED) {
        RECT normal_screen{};
        if(native_capture_normal_screen_bounds(dialog,normal_screen)) {
            state->last_normal_screen_bounds=normal_screen;
            state->has_last_normal_screen_bounds=true;
        }
        return FALSE; // Let the real dialog procedure emit WM_SIZE/WM_MOVE.
    }
    if (message == WM_CLOSE) {
        // Closing the modeless workspace must not freeze foobar while an
        // in-flight HTTPS request finishes. Hide, then destroy on completion.
        if((state->discogs_request &&
            !readonly_network_finished(*state->discogs_request)) ||
           (state->musicbrainz_request &&
            !readonly_network_finished(*state->musicbrainz_request)) ||
           (state->beatport_public_request &&
            !readonly_network_finished(*state->beatport_public_request))) {
            state->online_close_requested=true;
            if(state->discogs_request)
                request_readonly_network_stop(*state->discogs_request);
            if(state->musicbrainz_request)
                request_readonly_network_stop(*state->musicbrainz_request);
            if(state->beatport_public_request)
                request_readonly_network_stop(*state->beatport_public_request);
            // Keep HWND alive for the worker's completion notification;
            // a blocking WinHTTP call still observes its fixed timeouts.
            ShowWindow(dialog,SW_HIDE);
            return TRUE;
        }
        DestroyWindow(dialog);
        return TRUE;
    }
    if (message == WM_ACTIVATE && LOWORD(wp)!=WA_INACTIVE) {
        revalidate_workspace(*state);
        return FALSE;
    }

    if (message == WM_GETMINMAXINFO && state->initial_window_width > 0) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lp);
        limits->ptMinTrackSize.x = state->initial_window_width;
        limits->ptMinTrackSize.y = state->initial_window_height;
        return TRUE;
    }
    if (message == WM_SIZE && state->initial_client_width > 0) {
        if(wp==SIZE_MAXIMIZED) state->last_nonminimized_maximized=true;
        else if(wp==SIZE_RESTORED)
            state->last_nonminimized_maximized=false;
        const auto width=static_cast<unsigned>(LOWORD(lp));
        const auto height=static_cast<unsigned>(HIWORD(lp));
        if (!native_workspace_should_relayout_on_size(wp,width,height))
            return TRUE; // Do not realign against an iconic/empty client.
        resize_batch_dialog(*state,static_cast<int>(width),
                           static_cast<int>(height));
        align_native_preview_form(dialog);
        return TRUE;
    }
    if (message == WM_SETCURSOR &&
        reinterpret_cast<HWND>(wp)==dialog && LOWORD(lp)==HTCLIENT &&
        state->show_metadata) {
        POINT cursor{};
        RECT master{},detail{};
        if(GetCursorPos(&cursor) && ScreenToClient(dialog,&cursor) &&
           current_review_split_rects(*state,master,detail) &&
           review_split_gap_hit(master,detail,cursor.x,cursor.y)) {
            SetCursor(LoadCursorW(nullptr,IDC_SIZEWE));
            return TRUE;
        }
    }
    if (message == WM_LBUTTONDOWN && state->show_metadata) {
        RECT master{},detail{};
        const int x=static_cast<short>(LOWORD(lp));
        const int y=static_cast<short>(HIWORD(lp));
        if(current_review_split_rects(*state,master,detail) &&
           review_split_gap_hit(master,detail,x,y)) {
            state->split_dragging=true;
            state->split_drag_start_x=x;
            state->split_drag_start_offset_96=state->review_split_offset_96;
            SetCapture(dialog);
            return TRUE;
        }
    }
    if (message == WM_MOUSEMOVE && state->split_dragging) {
        const int x=static_cast<short>(LOWORD(lp));
        const int wanted_96=state->split_drag_start_offset_96+
            MulDiv(x-state->split_drag_start_x,96,state->active_dpi);
        apply_review_split_offset(*state,wanted_96);
        return TRUE;
    }
    if (message == WM_LBUTTONUP && state->split_dragging) {
        state->split_dragging=false;
        if(GetCapture()==dialog) ReleaseCapture();
        return TRUE;
    }
    if (message == WM_CAPTURECHANGED && state->split_dragging) {
        state->split_dragging=false;
        return FALSE;
    }
    if (message == WM_EXITSIZEMOVE) {
        // Native buttons and hidden tab siblings must not leave paint ghosts
        // when a user stops dragging the window edge.
        RedrawWindow(dialog, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        return TRUE;
    }

    if(message==kNativeReviewSplitMessage) {
        // HWND-only view geometry. No metadata/rule/provider state changes.
        const auto action=static_cast<NativeReviewSplitAction>(wp);
        if(state->show_metadata && !state->split_dragging &&
           native_review_split_valid_action(action)) {
            const int next=native_review_split_next_offset(
                state->review_split_offset_96,action);
            const bool changed=apply_review_split_offset(*state,next);
            if(changed || action==NativeReviewSplitAction::Reset)
                SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                    L"Source/detail divider adjusted (read-only). "
                    L"Ctrl+Shift+Left/Right resizes; Ctrl+Shift+Home resets. "
                    L"Pane geometry is saved with this foobar profile.");
        }
        return TRUE;
    }
    if (message == WM_NOTIFY) {
        const auto* header = reinterpret_cast<const NMHDR*>(lp);
        if (header && header->idFrom == IDC_BATCH_TABS &&
            header->code == TCN_SELCHANGE) {
            dismiss_workspace_tooltips(*state);
            invalidate_online_request(*state);
            const int selected_tab = TabCtrl_GetCurSel(state->tabs);
            if(selected_tab==3) show_history_page(dialog,*state);
            else show_preview_page(dialog, *state,
                selected_tab != 2, selected_tab == 0);
            return TRUE;
        }
        if(header && header->idFrom==IDC_BATCH_HISTORY_LIST &&
           (header->code==NM_DBLCLK || header->code==LVN_KEYDOWN)) {
            int row=-1;
            if(header->code==NM_DBLCLK) {
                const auto* click=reinterpret_cast<const NMITEMACTIVATE*>(lp);
                if(click) row=click->iItem;
            } else {
                const auto* key=reinterpret_cast<const NMLVKEYDOWN*>(lp);
                if(!key || key->wVKey!=VK_RETURN) return FALSE;
                row=ListView_GetNextItem(state->history_list,-1,LVNI_FOCUSED);
                if(row<0) row=ListView_GetNextItem(state->history_list,-1,LVNI_SELECTED);
            }
            // Source/rules/route exceptions must not escape unmanaged
            // WM_NOTIFY. An aborted navigation cannot replay an action.
            try {
                navigate_preview_history(dialog,*state,row);
            } catch (...) {
                mark_stale_workspace(*state);
                SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                    L"History navigation aborted: stale source or route. "
                    L"Refresh; nothing was replayed.");
            }
            return TRUE;
        }
        if (header && header->idFrom == IDC_METADATA_TRACK_LIST &&
            (header->code == NM_DBLCLK || header->code == LVN_KEYDOWN)) {
            // Owner-data ListView reliably emits NM_DBLCLK for mouse and
            // LVN_KEYDOWN for Return; LVN_ITEMACTIVATE varies by host style.
            // Do not also process ITEMACTIVATE: one gesture must issue only
            // one MusicBrainz request.
            int row=-1;
            if(header->code==NM_DBLCLK) {
                const auto* clicked=reinterpret_cast<const NMITEMACTIVATE*>(lp);
                if(clicked) row=clicked->iItem;
            } else {
                const auto* key=reinterpret_cast<const NMLVKEYDOWN*>(lp);
                if(!key || key->wVKey!=VK_RETURN) return FALSE;
                row=ListView_GetNextItem(state->metadata_track_list,-1,LVNI_FOCUSED);
                if(row<0) row=ListView_GetNextItem(
                    state->metadata_track_list,-1,LVNI_SELECTED);
            }
            if (row<0 || static_cast<std::size_t>(row)>=state->track_view_order.size())
                return TRUE;
            // The original tab is the user's gesture context. Capture it
            // before switching the visible page to Find & compare.
            const int originating_tab=TabCtrl_GetCurSel(state->tabs);
            const auto next_source=state->track_view_order[
                static_cast<std::size_t>(row)];
            if(!confirm_online_preview_navigation(dialog,*state,next_source))
                return TRUE;
            invalidate_online_request(*state);
            state->selected_track_index=next_source;
            TabCtrl_SetCurSel(state->tabs,0);
            show_preview_page(dialog,*state,true,true);
            if(originating_tab != 0) {
                // A review/file-planning gesture navigates, but NEVER
                // authorizes an implicit provider call or overwrites manual
                // search text. A later explicit Search click is required.
                SetFocus(GetDlgItem(dialog,IDC_METADATA_MB_QUERY));
                SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                    native_workspace_display_hint(state->stale,
                        L"Selected track opened in Find & compare. Review the "
                        L"search text and click Search or Load Beatport URL "
                        L"explicitly; no network request was made."));
                return TRUE;
            }
            prefill_musicbrainz_query(*state,true);
            if(state->beatport_offline_selected) {
                // A Find-tab activation explicitly asks for discovery. Use
                // the existing guarded Crates browser hand-off, which copies
                // ARTIST + full TITLE for the user's own browser search. Never
                // call a gated Beatport catalog API or read arbitrary clipboard
                // content. Other tabs only navigate here without side effects.
                if(native_track_activation_starts_crates_handoff(
                       originating_tab,state->beatport_offline_selected)) {
                    SendMessageW(dialog,WM_COMMAND,
                        MAKEWPARAM(IDC_METADATA_DISCOGS_NEXT_PAGE,BN_CLICKED),
                        reinterpret_cast<LPARAM>(GetDlgItem(
                            dialog,IDC_METADATA_DISCOGS_NEXT_PAGE)));
                }
                return TRUE;
            }
            // Only a Find-tab track gesture may authorize a single provider
            // request. The shared policy is also exercised by native HWND tests.
            if(native_track_activation_starts_online_search(
                    originating_tab,state->beatport_offline_selected)) {
                SendMessageW(dialog,WM_COMMAND,
                    MAKEWPARAM(IDC_METADATA_MB_SEARCH,BN_CLICKED),
                    reinterpret_cast<LPARAM>(GetDlgItem(dialog,IDC_METADATA_MB_SEARCH)));
            }
            return TRUE;
        }
        if (header && header->idFrom == IDC_METADATA_TRACK_LIST &&
            header->code == LVN_GETDISPINFOW) {
            auto* info = reinterpret_cast<NMLVDISPINFOW*>(lp);
            if ((info->item.mask & LVIF_TEXT) != 0 && info->item.iItem >= 0) {
                state->cell_buffer = track_master_cell(*state,
                    static_cast<std::size_t>(info->item.iItem), info->item.iSubItem);
                info->item.pszText = state->cell_buffer.data();
            }
            return TRUE;
        }
        if (header && header->idFrom == IDC_METADATA_TRACK_LIST &&
            header->code == LVN_ODFINDITEMW) {
            const auto* find = reinterpret_cast<const NMLVFINDITEMW*>(lp);
            int row = -1;
            if ((find->lvfi.flags & (LVFI_STRING | LVFI_PARTIAL)) != 0 &&
                find->lvfi.psz != nullptr) {
                row = find_native_track_prefix(
                    state->track_view_order, state->cached_track_names,
                    find->lvfi.psz, find->iStart,
                    (find->lvfi.flags & LVFI_WRAP) != 0);
            }
            SetWindowLongPtrW(dialog, DWLP_MSGRESULT, row);
            return TRUE;
        }
        if (header && header->idFrom == IDC_METADATA_TRACK_LIST &&
            header->code == LVN_COLUMNCLICK) {
            const auto* click = reinterpret_cast<const NMLISTVIEW*>(lp);
            if (click->iSubItem >= 0 && click->iSubItem < 4) {
                if (click->iSubItem == state->track_grid.sort_column)
                    state->track_grid.sort_descending = !state->track_grid.sort_descending;
                else {
                    state->track_grid.sort_column = click->iSubItem;
                    state->track_grid.sort_descending = false;
                }
                update_master_table(*state);
                update_metadata_table(*state);
                show_review_grid_sort_arrow(state->metadata_track_list, state->track_grid);
            }
            return TRUE;
        }
        if (header && header->idFrom == IDC_METADATA_TRACK_LIST &&
            header->code == LVN_ITEMCHANGING) {
            const auto* changing=reinterpret_cast<const NMLISTVIEW*>(lp);
            if(changing && native_online_source_selection_veto(
                *changing,state->track_view_order,state->selected_track_index,
                !state->online_included_fields.empty(),
                state->updating_track_selection)) {
                SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                    L"Included online fields belong to this source. "
                    L"Double-click/Enter to switch with confirmation, or "
                    L"Reset online preview. Nothing was discarded.");
                SetWindowLongPtrW(dialog,DWLP_MSGRESULT,TRUE);
                return TRUE; // veto BEFORE owner-data selection changes
            }
        }
        if (header && header->idFrom == IDC_METADATA_TRACK_LIST &&
            header->code == LVN_ITEMCHANGED) {
            const auto* changed = reinterpret_cast<const NMLISTVIEW*>(lp);
            if (const auto track = native_selected_track_change(
                    *changed, state->track_view_order, state->updating_track_selection)) {
                const bool selected_source_changed =
                    *track != state->selected_track_index;
                if (selected_source_changed) {
                    invalidate_online_request(*state);
                    state->online_included_fields.clear();
                    state->online_date_targets.clear();
                    state->online_field_preview.reset();
                    state->online_physical_edition.reset();
                    state->online_field_snapshot.reset();
                    state->online_raw_target_observation.reset();
                    state->online_edition_source=(std::numeric_limits<std::size_t>::max)();
                }
                state->selected_track_index = *track;
                // Ordinary selection must not override manually typed terms.
                // Only a deliberate track activation (double-click/Enter)
                // is allowed to force a new query and issue a request.
                prefill_musicbrainz_query(*state);
                update_metadata_table(*state);
                if (selected_source_changed) {
                    // The newly visible source needs its own tag/file guard.
                    // Do not re-read every background row; any subsequent
                    // search, edit or file-plan action still checks the batch.
                    revalidate_workspace(*state);
                }
            } else if (native_master_track_deselected(
                           *changed,state->updating_track_selection)) {
                // The marked set shrank but the focused source did not change.
                // Do not rerender/sort the detail grid or probe every track.
                update_local_review_hint(*state);
            }
            return TRUE;
        }
        if (header && header->idFrom == IDC_METADATA_TRACK_LIST &&
            header->code == LVN_GETINFOTIPW) {
            auto* tip = reinterpret_cast<NMLVGETINFOTIPW*>(lp);
            if (tip->iItem >= 0 && tip->pszText && tip->cchTextMax > 0) {
                const auto row = static_cast<std::size_t>(tip->iItem);
                if (row < state->track_view_order.size()) {
                    const auto index = state->track_view_order[row];
                    if (index < state->source_labels.size()) {
                        std::wstring text = from_utf8(
                            display_file_path(state->source_labels[index]));
                        if(index<state->track_summaries.size()) {
                            const auto& progress=state->track_summaries[index];
                            text+=L"\nStaged edits: "+
                                  std::to_wstring(progress.staged_changes)+
                                  L" | Pending proposals: "+
                                  std::to_wstring(progress.pending_proposals)+
                                  L" | Pending REVIEW: "+
                                  std::to_wstring(progress.review_required);
                        }
                        lstrcpynW(tip->pszText, text.c_str(), tip->cchTextMax);
                    }
                }
            }
            return TRUE;
        }
        if(header && header->idFrom==IDC_METADATA_LIST &&
           !state->show_candidate && !state->existing_tags_view &&
           (header->code==NM_DBLCLK ||
            (header->code==LVN_KEYDOWN &&
             reinterpret_cast<const NMLVKEYDOWN*>(lp)->wVKey==VK_RETURN))) {
            const int row=header->code==NM_DBLCLK
                ? reinterpret_cast<const NMITEMACTIVATE*>(lp)->iItem
                : ListView_GetNextItem(state->metadata_list,-1,LVNI_FOCUSED);
            if(row>=0 && static_cast<std::size_t>(row)<state->metadata_view_group.size()) {
                const int group=state->metadata_view_group[static_cast<std::size_t>(row)];
                if(group>=0 && group<3) {
                    state->review_group_expanded[group]=!state->review_group_expanded[group];
                    update_metadata_table(*state);
                    return TRUE;
                }
            }
        }
        // The read-only CUE inventory connects one observed FILE dependency
        // directly to the selected physical File locations source. Interpret
        // only our bounded ID, never a CUE filename or tooltip/path text.
        if(header && header->idFrom==IDC_METADATA_LIST &&
           (header->code==NM_DBLCLK ||
            (header->code==LVN_KEYDOWN &&
             reinterpret_cast<const NMLVKEYDOWN*>(lp)->wVKey==VK_RETURN)) &&
           state->cue_inspection_mode && state->show_candidate &&
           !state->stale && !state->refreshing &&
           !state->musicbrainz_request && !state->discogs_request &&
           !state->beatport_public_request &&
           state->candidate_source_index==state->selected_track_index &&
           state->selected_track_index<state->entries.size()) {
            const int clicked=header->code==NM_DBLCLK
                ? reinterpret_cast<const NMITEMACTIVATE*>(lp)->iItem : -1;
            const int focused=ListView_GetNextItem(
                state->metadata_list,-1,LVNI_FOCUSED);
            const int clicked_selected=clicked>=0 &&
                (ListView_GetItemState(state->metadata_list,clicked,
                                      LVIS_SELECTED)&LVIS_SELECTED)!=0;
            const int focused_selected=focused>=0 &&
                (ListView_GetItemState(state->metadata_list,focused,
                                      LVIS_SELECTED)&LVIS_SELECTED)!=0;
            const int activated=native_selected_release_activation_row(
                header->code,clicked,focused,clicked_selected!=0,
                focused_selected!=0,
                header->code==LVN_KEYDOWN?VK_RETURN:0);
            if(activated<0 ||
               static_cast<std::size_t>(activated)>=
                   state->candidate_view_order.size())
                return TRUE;
            const auto candidate=state->candidate_view_order[
                static_cast<std::size_t>(activated)];
            if(candidate>=state->candidate_rows.size()) return TRUE;
            const auto& row=state->candidate_rows[candidate];
            if(row.reason!="cue_raw_routing_impact_not_final_approval" &&
               row.reason!="cue_carrier_action_not_final_approval")
                return TRUE;
            const auto target=djmeta::resolve_cue_file_navigation(
                row.candidate.source_id,state->selected_track_index,
                state->entries.size());
            if(!target) {
                SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                    L"This CUE FILE reference has no qualified selected physical audio. "
                    L"Inspect the missing source before planning relocation; no writes.");
                return TRUE;
            }
            if(!confirm_online_preview_navigation(
                   dialog,*state,target->source_index)) return TRUE;
            const auto& cue=state->entries.at(state->selected_track_index);
            verify_snapshot(cue);
            verify_rules_snapshot(state->captured_rules);
            const auto& physical=state->entries.at(target->source_index);
            if(!target->carrier &&
               (!physical.input.physical_source_qualified ||
                physical.handle->get_subsong_index()!=0 ||
                is_external_cue_locator(physical.input.source_path)))
                throw std::runtime_error(
                    "CUE reference no longer maps to qualified physical audio. Refresh.");
            verify_snapshot(physical);
            revalidate_workspace(*state,true);
            if(state->stale)
                throw std::runtime_error(
                    "CUE linked source changed; refresh the read-only workspace.");
            // Re-evaluate the full batch AFTER re-probing, preserving all
            // mixed Move/Copy/Skip intents and whole-batch raw collisions.
            state->cue_return=PreviewState::CueReturnIdentity{
                state->selected_track_index,cue.observed_physical_key,
                cue.observed_source_guard};
            state->location_queue=djmeta::BatchLocationQueue::All;
            SendDlgItemMessageW(dialog,IDC_BATCH_LOCATION_FILTER,CB_SETCURSEL,0,0);
            update_table(*state);
            TabCtrl_SetCurSel(state->tabs,2);
            show_preview_page(dialog,*state,false,false);
            const auto where=std::find(state->view_order.begin(),
                                       state->view_order.end(),
                                       target->source_index);
            if(where==state->view_order.end())
                throw std::runtime_error(
                    "Linked source no longer visible in complete File locations queue.");
            const auto position=static_cast<int>(where-state->view_order.begin());
            ListView_SetItemState(state->list,-1,0,LVIS_SELECTED);
            ListView_SetItemState(state->list,position,
                                  LVIS_SELECTED|LVIS_FOCUSED,
                                  LVIS_SELECTED|LVIS_FOCUSED);
            ListView_EnsureVisible(state->list,position,FALSE);
            SetFocus(state->list);
            const std::wstring hint=target->carrier
                ? L"CUE carrier opened in File locations. Stage an explicit "
                  L"sidecar action; no final FileOps/CUE approval."
                : L"CUE FILE #"+std::to_wstring(target->file_index+1)+
                  L" linked physical source #"+
                  std::to_wstring(target->source_index+1)+
                  L" opened. RAW routes only; no final FileOps/CUE approval.";
            SetDlgItemTextW(dialog,IDC_BATCH_HINT,hint.c_str());
            return TRUE;
        }
        // Exact-edition album source drill-down: authoritative captured
        // physical source identity, never a sorted candidate ListView row,
        // user tag, external provider label or bulk approval.
        if(header && header->idFrom==IDC_METADATA_LIST &&
           (header->code==NM_DBLCLK ||
            (header->code==LVN_KEYDOWN &&
             reinterpret_cast<const NMLVKEYDOWN*>(lp)->wVKey==VK_RETURN)) &&
           state->show_candidate && state->musicbrainz_live_view &&
           state->musicbrainz_release_loaded &&
           (state->discogs_live_view ||
            state->musicbrainz_kind==
                djmeta::online::musicbrainz::SearchKind::Release) &&
           !state->stale && !state->refreshing &&
           !state->musicbrainz_request && !state->discogs_request &&
           !state->beatport_public_request &&
           state->candidate_source_index==state->selected_track_index) {
            const int clicked=header->code==NM_DBLCLK
                ? reinterpret_cast<const NMITEMACTIVATE*>(lp)->iItem:-1;
            const int focused=ListView_GetNextItem(
                state->metadata_list,-1,LVNI_FOCUSED);
            const bool click_selected=clicked>=0 &&
                (ListView_GetItemState(state->metadata_list,clicked,
                    LVIS_SELECTED)&LVIS_SELECTED)!=0;
            const bool focus_selected=focused>=0 &&
                (ListView_GetItemState(state->metadata_list,focused,
                    LVIS_SELECTED)&LVIS_SELECTED)!=0;
            const int activated=native_selected_release_activation_row(
                header->code,clicked,focused,click_selected,
                focus_selected,header->code==LVN_KEYDOWN?VK_RETURN:0);
            if(activated>=0 &&
               static_cast<std::size_t>(activated)<state->candidate_view_order.size()) {
                const auto row_id=state->candidate_view_order[
                    static_cast<std::size_t>(activated)];
                if(row_id<state->candidate_rows.size()) {
                    const auto& row=state->candidate_rows[row_id];
                    const auto target=native_verified_album_source_target(
                        row.field,row.reason,true,true,state->entries.size());
                    if(target) {
                        try {
                        if(!confirm_online_preview_navigation(
                               dialog,*state,*target)) return TRUE;
                        // All selected sources, including hidden rows,
                        // still require independent host/rules freshness.
                        revalidate_workspace(*state,true);
                        if(state->stale) return TRUE;
                        for(const auto& source:state->entries)
                            verify_online_source_snapshot(*state,source);
                        verify_rules_snapshot(state->captured_rules);
                        const auto& physical=state->entries.at(*target);
                        if(!physical.input.physical_source_qualified ||
                           physical.handle->get_subsong_index()!=0 ||
                           is_external_cue_locator(physical.input.source_path))
                            throw std::runtime_error(
                                "Album row no longer targets qualified physical audio.");
                        invalidate_online_request(*state);
                        state->selected_track_index=*target;
                        state->track_discovery=djmeta::TrackDiscovery::All;
                        state->focused_track_index=
                            (std::numeric_limits<std::size_t>::max)();
                        SendDlgItemMessageW(dialog,IDC_METADATA_TRACK_FILTER,
                            CB_SETCURSEL,0,0);
                        update_master_table(*state);
                        TabCtrl_SetCurSel(state->tabs,1);
                        show_preview_page(dialog,*state,true,false);
                        const auto where=std::find(
                            state->track_view_order.begin(),
                            state->track_view_order.end(),*target);
                        if(where==state->track_view_order.end())
                            throw std::runtime_error(
                                "Exact album physical source not in Review queue.");
                        const int position=static_cast<int>(
                            where-state->track_view_order.begin());
                        state->updating_track_selection=true;
                        ListView_SetItemState(state->metadata_track_list,
                            -1,0,LVIS_SELECTED);
                        ListView_SetItemState(state->metadata_track_list,
                            position,LVIS_SELECTED|LVIS_FOCUSED,
                            LVIS_SELECTED|LVIS_FOCUSED);
                        state->updating_track_selection=false;
                        ListView_EnsureVisible(state->metadata_track_list,
                            position,FALSE);
                        SetFocus(state->metadata_track_list);
                        const std::wstring hint=L"Physical album source #"+
                            std::to_wstring(*target+1)+
                            L" opened in Review. Read-only; no bulk field "
                            L"approval or network request.";
                        SetDlgItemTextW(dialog,IDC_BATCH_HINT,hint.c_str());
                        return TRUE;
                        } catch (...) {
                            // Host source, rules, allocation or UI mapping
                            // failure cannot unwind through Win32 WndProc or
                            // leave a partially retargeted usable workspace.
                            state->updating_track_selection=false;
                            mark_stale_workspace(*state);
                            SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                                L"Exact album source changed or could not be "
                                L"revalidated. Refresh the read-only workspace; "
                                L"no choices or media were written.");
                            return TRUE;
                        }
                    }
                }
            }
        }
        const bool release_activation=header &&
            header->idFrom==IDC_METADATA_LIST &&
            (header->code==NM_DBLCLK ||
             (header->code==LVN_KEYDOWN &&
              reinterpret_cast<const NMLVKEYDOWN*>(lp)->wVKey==VK_RETURN));
        // The native Load release command consumes the FIRST selected row,
        // but a mouse/Return gesture can target ANOTHER selected row. Bind
        // the command to that exact activated identity, never a preceding
        // multi-selection or the item behind a blank-space double-click.
        const int release_focused_row=release_activation
            ? ListView_GetNextItem(state->metadata_list,-1,LVNI_FOCUSED) : -1;
        const int release_clicked_row=release_activation &&
            header->code==NM_DBLCLK
            ? reinterpret_cast<const NMITEMACTIVATE*>(lp)->iItem : -1;
        const bool clicked_selected=release_clicked_row>=0 &&
            (ListView_GetItemState(state->metadata_list,release_clicked_row,
                                  LVIS_SELECTED)&LVIS_SELECTED)!=0;
        const bool focused_selected=release_focused_row>=0 &&
            (ListView_GetItemState(state->metadata_list,release_focused_row,
                                  LVIS_SELECTED)&LVIS_SELECTED)!=0;
        const int activated_release_row=release_activation
            ? native_selected_release_activation_row(header->code,
                release_clicked_row,release_focused_row,
                clicked_selected,focused_selected,
                header->code==LVN_KEYDOWN?VK_RETURN:0) : -1;
        const bool activation_current=activated_release_row>=0 &&
            static_cast<std::size_t>(activated_release_row)<
                state->candidate_view_order.size() &&
            !state->stale && state->show_candidate &&
            // Pasted comparison groups are not provider release hits. A
            // double-click may inspect them but must not collapse a user's
            // multi-selection into a fake Load release action.
            !pasted_comparison_group_view(*state) &&
            state->candidate_source_index==state->selected_track_index &&
            !state->discogs_request && !state->musicbrainz_request &&
            !state->beatport_public_request &&
            ((state->discogs_live_view && !state->musicbrainz_release_loaded) ||
             (state->musicbrainz_live_view &&
              state->musicbrainz_kind==
                  djmeta::online::musicbrainz::SearchKind::Release &&
              !state->musicbrainz_release_loaded));
        bool exact_release_activation=false;
        if(activation_current) {
            // Suppress intermediate selection-change callbacks while changing
            // the active row; refresh controls after the atomic retarget.
            state->updating_musicbrainz_selection=true;
            exact_release_activation=native_focus_exact_release_row(
                state->metadata_list,activated_release_row);
            state->updating_musicbrainz_selection=false;
            if(exact_release_activation) {
                refresh_musicbrainz_detail_rows(*state);
                update_online_release_load_button(*state);
            }
        }
        const bool discogs_activation=exact_release_activation &&
            state->discogs_live_view &&
            !state->musicbrainz_release_loaded;
        if (exact_release_activation &&
            IsWindowEnabled(GetDlgItem(dialog,IDC_METADATA_MB_LOAD_RELEASE)) &&
            (discogs_activation || may_activate_musicbrainz_release(header->code,
                state->show_candidate && state->musicbrainz_live_view,
                state->musicbrainz_kind==
                    djmeta::online::musicbrainz::SearchKind::Release,
                state->musicbrainz_release_loaded,
                state->candidate_source_index==state->selected_track_index,
                header->code==LVN_KEYDOWN?VK_RETURN:0))) {
            // A native ListView double-click or Enter is equivalent to
            // clicking Load release. Both providers verify selected identities;
            // a Discogs master navigates to editions, never to tag fields.
            SendMessageW(dialog,WM_COMMAND,
                MAKEWPARAM(IDC_METADATA_MB_LOAD_RELEASE,BN_CLICKED),
                reinterpret_cast<LPARAM>(GetDlgItem(dialog,
                                                    IDC_METADATA_MB_LOAD_RELEASE)));
            return TRUE;
        }
        if (header && header->idFrom == IDC_METADATA_LIST &&
            header->code == LVN_ITEMCHANGED && state->show_candidate &&
            state->musicbrainz_live_view) {
            if (!state->updating_musicbrainz_selection) {
                const auto* changed=reinterpret_cast<const NMLISTVIEW*>(lp);
                if (changed && (changed->uChanged & LVIF_STATE) &&
                    ((changed->uNewState & LVIS_SELECTED) !=
                     (changed->uOldState & LVIS_SELECTED))) {
                    invalidate_online_request(*state);
                    refresh_musicbrainz_detail_rows(*state);
                    update_online_release_load_button(*state);
                }
            }
            return TRUE;
        }
        if(header && header->idFrom==IDC_METADATA_MB_DETAILS &&
           header->code==LVN_KEYDOWN) {
            const auto* key=reinterpret_cast<const NMLVKEYDOWN*>(lp);
            if(key && native_evidence_copy_hotkey(
                  key->wVKey,(GetKeyState(VK_CONTROL)&0x8000)!=0,
                  (GetKeyState(VK_MENU)&0x8000)!=0)) {
                SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(41411u,0),0);
                return TRUE;
            }
        }
        // Field-level source identity, evidence scope and date meaning are
        // available on the real lower native ListView, not just the summary.
        if (header && header->idFrom == IDC_METADATA_MB_DETAILS &&
            header->code == LVN_GETINFOTIPW) {
            auto* tip=reinterpret_cast<NMLVGETINFOTIPW*>(lp);
            if (tip->iItem>=0 && tip->pszText && tip->cchTextMax>0 &&
                state->show_candidate && state->musicbrainz_live_view &&
                state->candidate_source_index==state->selected_track_index) {
                const int selected=ListView_GetNextItem(
                    state->metadata_list,-1,LVNI_SELECTED);
                if (selected>=0 &&
                    static_cast<std::size_t>(selected)<state->candidate_view_order.size()) {
                    const auto idx=state->candidate_view_order[
                        static_cast<std::size_t>(selected)];
                    if (idx<state->musicbrainz_detail_groups.size()) {
                        const auto& rows=state->musicbrainz_detail_groups[idx];
                        const auto row=static_cast<std::size_t>(tip->iItem);
                        if (row<rows.size() && row<1024) {
                            const auto& item=rows[row];
                            const bool physical_source=
                                djmeta::online::is_local_physical_original(item);
                            const bool cue_source=
                                djmeta::online::is_local_cue_inventory(item);
                            const auto label=
                                L"Source: "+(cue_source
                                    ? from_utf8(item.candidate.provider)
                                    : from_utf8(djmeta::online::online_evidence_origin_label(item)))+
                                L"\nSource line / provider ID: "+(physical_source
                                    ? std::wstring(L"(not supplied; original local tag)")
                                    : from_utf8(item.candidate.source_id))+
                                L"\nScope: "+(physical_source
                                    ? std::wstring(L"physical audio metadata")
                                    : cue_source
                                    ? std::wstring(L"original CUE album/track/structure")
                                    : from_utf8(djmeta::online::online_evidence_scope_label(
                                        item.candidate.scope)))+
                                L"\nDate meaning: "+from_utf8(
                                    djmeta::online::online_evidence_date_label(
                                        item.candidate.date_meaning))+
                                L"\nReason: "+from_utf8(item.reason)+
                                L"\nOriginal: "+show_field_values(item.original_values)+
                                L"\nEvidence: "+show_field_values(item.candidate.values)+
                                L"\nRead-only; no tag or CUE write authorization.";
                            lstrcpynW(tip->pszText,label.c_str(),tip->cchTextMax);
                        }
                    }
                }
            }
            return TRUE;
        }
        if (header && header->idFrom == IDC_METADATA_LIST &&
            header->code == LVN_GETINFOTIPW) {
            auto* tip = reinterpret_cast<NMLVGETINFOTIPW*>(lp);
            if (tip->iItem >= 0 && tip->pszText && tip->cchTextMax > 0) {
                const auto row = static_cast<std::size_t>(tip->iItem);
                if (state->show_candidate) {
                    if (state->candidate_source_index == state->selected_track_index &&
                        row < state->candidate_view_order.size()) {
                        const auto idx = state->candidate_view_order[row];
                        if (idx < state->candidate_rows.size()) {
                            const auto& item = state->candidate_rows[idx];
                            const auto album_target=native_verified_album_source_target(
                                item.field,item.reason,state->musicbrainz_release_loaded,
                                state->candidate_source_index==state->selected_track_index,
                                state->entries.size());
                            if(album_target && state->musicbrainz_live_view) {
                                const std::wstring guidance=
                                    L"Verified read-only physical album source #"+
                                    std::to_wstring(*album_target+1)+
                                    L". Double-click or Enter to inspect this "
                                    L"captured source in Review. Included online "
                                    L"fields must be reset before switching. "
                                    L"No bulk approval.";
                                lstrcpynW(tip->pszText,guidance.c_str(),
                                    tip->cchTextMax);
                                return TRUE;
                            }
                            const bool is_cue_inventory =
                                item.reason == "cue_inventory_read_only" ||
                                item.reason == "cue_inventory_unqualified";
                            const auto official_source=
                                djmeta::online::online_evidence_origin_label(item);
                            const bool browse_result =
                                item.reason.starts_with("musicbrainz_live_candidate_rank_");
                            const std::wstring label = browse_result
                                ? (L"Source: official MusicBrainz HTTPS search"
                                   L"\nTitle: " + from_utf8(item.field) +
                                   L"\nArtist: " + show_field_values(item.original_values) +
                                   L"\nEdition date: " +
                                       show_field_values(item.candidate.values) +
                                   L"\nMusicBrainz ID: " +
                                       from_utf8(item.candidate.source_id) +
                                   L"\nRank is only API search relevance, NOT match confidence.")
                                : std::wstring(is_cue_inventory
                                    ? L"Source: actual local CUE carrier"
                                    : official_source=="Discogs API"
                                    ? L"Source: official Discogs HTTPS API (read-only)"
                                    : official_source=="MusicBrainz API"
                                    ? L"Source: official MusicBrainz HTTPS API (read-only)"
                                    : L"Source: manual paste, provider unverified") +
                                L"\nDeclared provider: " + from_utf8(item.candidate.provider) +
                                L"\nSource ID: " + from_utf8(item.candidate.source_id) +
                                L"\nField: " + from_utf8(item.field) +
                                L"\nReason: " + from_utf8(item.reason) +
                                L"\nOriginal: " + show_field_values(item.original_values) +
                                L"\nProposal: " + show_field_values(item.candidate.values);
                            lstrcpynW(tip->pszText, label.c_str(), tip->cchTextMax);
                        }
                    }
                    return TRUE; // never show stale normalization rule details
                }
                if (state->existing_tags_view) {
                    if (row < state->existing_tag_rows.size()) {
                        const auto& original=state->existing_tag_rows[row];
                        const std::wstring label=original.first+L": "+
                            original.second+L"\nOriginal physical tag. "+
                            L"Missing online evidence never deletes this physical tag.";
                        lstrcpynW(tip->pszText,label.c_str(),tip->cchTextMax);
                    }
                    return TRUE; // never leak an unrelated proposal tooltip
                }
                if (row < state->metadata_view_group.size() &&
                    state->metadata_view_group[row]>=0) {
                    lstrcpynW(tip->pszText,
                        L"Display group only. Double-click or press Enter to show its "
                        L"proposals. Select individual proposal rows for field actions; "
                        L"the group itself never approves a change.",tip->cchTextMax);
                    return TRUE;
                }
                if (row < state->metadata_view_order.size()) {
                    const auto index = state->metadata_view_order[row];
                    if (index < state->focused_metadata_rows.size()) {
                        const auto& entry = state->focused_metadata_rows[index];
                        std::string manual_info;
                        if (const auto* decision = decision_for_row(*state, entry);
                            decision && decision->action == djmeta::ReviewAction::ManualValue)
                            manual_info = "\nManual staged value: " + decision->manual_value;
                        const auto label = from_utf8(
                            "Rules: " + entry.rule_ids + "\nWhy: " +
                            entry.rationales + "\nOriginal: " + entry.original +
                            "\nRule proposal: " + entry.proposed + manual_info);
                        lstrcpynW(tip->pszText, label.c_str(), tip->cchTextMax);
                    }
                }
            }
            return TRUE;
        }
        if (header && header->idFrom == IDC_METADATA_LIST &&
            header->code == LVN_COLUMNCLICK) {
            const auto* click = reinterpret_cast<const NMLISTVIEW*>(lp);
            if (click->iSubItem >= 0 && click->iSubItem < 5) {
                if (state->show_candidate) {
                    if (click->iSubItem == state->candidate_sort_column)
                        state->candidate_sort_descending = !state->candidate_sort_descending;
                    else {
                        state->candidate_sort_column = click->iSubItem;
                        state->candidate_sort_descending = false;
                    }
                    update_metadata_table(*state);
                    auto sort_view = state->detail_grid;
                    sort_view.sort_column = state->candidate_sort_column;
                    sort_view.sort_descending = state->candidate_sort_descending;
                    show_review_grid_sort_arrow(state->metadata_list, sort_view);
                } else {
                    if (click->iSubItem == state->detail_grid.sort_column)
                        state->detail_grid.sort_descending = !state->detail_grid.sort_descending;
                    else {
                        state->detail_grid.sort_column = click->iSubItem;
                        state->detail_grid.sort_descending = false;
                    }
                    update_metadata_table(*state);
                    show_review_grid_sort_arrow(state->metadata_list, state->detail_grid);
                }
            }
            return TRUE;
        }
        if (header && header->idFrom == IDC_METADATA_LIST &&
            header->code == LVN_GETDISPINFOW) {
            auto* info = reinterpret_cast<NMLVDISPINFOW*>(lp);
            if ((info->item.mask & LVIF_TEXT) != 0 && info->item.iItem >= 0) {
                state->cell_buffer = metadata_cell_text(*state,
                    static_cast<std::size_t>(info->item.iItem), info->item.iSubItem);
                info->item.pszText = state->cell_buffer.data();
            }
            return TRUE;
        }
        if (header && header->idFrom == IDC_BATCH_LIST &&
            header->code == LVN_GETINFOTIPW) {
            auto* tip = reinterpret_cast<NMLVGETINFOTIPW*>(lp);
            if (tip->iItem >= 0 && tip->pszText && tip->cchTextMax > 0) {
                const auto view_row = static_cast<std::size_t>(tip->iItem);
                if (view_row < state->view_order.size()) {
                    const auto source_row = state->view_order[view_row];
                    if (source_row < state->entries.size()) {
                        const auto& entry = state->entries[source_row];
                        std::string detail="Selected file action: "+
                            std::string(djmeta::file_action_label(entry.file_action))+
                            " (preview only)";
                        if(source_row < state->table.rows.size())
                            detail+="; "+std::string(djmeta::inspect_file_action_intent(
                                entry.file_action,entry.input,state->table.rows[source_row]).reason);
                        detail+="\n";
                        if (source_row < state->table.rows.size()) {
                            const auto& issues = state->table.rows[source_row].issues;
                            if (!issues.empty()) detail = "Safety findings:";
                            for (const auto& issue : issues) {
                                detail += "\n- ";
                                detail += issue_status_text(issue);
                                detail += " [";
                                detail += issue;
                                detail += "]";
                            }
                        }
                        // Put safety first: long source/target paths must not
                        // truncate a secondary warning in the native infotip.
                        if (!detail.empty()) detail += "\n";
                        detail += "Physical source: " + entry.source_probe_detail +
                            "\nRaw candidate: " +
                            (entry.raw_target_probe_detail.empty()
                                ? std::string("not inspected") : entry.raw_target_probe_detail) +
                            "\nFinal foobar destination and CUE links remain unqualified.";
                        const std::wstring wide_detail = from_utf8(detail);
                        lstrcpynW(tip->pszText, wide_detail.c_str(), tip->cchTextMax);
                    }
                }
            }
            return TRUE;
        }
        if (header && header->idFrom == IDC_BATCH_LIST &&
            header->code == LVN_COLUMNCLICK) {
            const auto* click = reinterpret_cast<const NMLISTVIEW*>(lp);
            if (click->iSubItem >= 0 &&
                click->iSubItem < djmeta::kBatchPreviewColumnCount) {
                if (state->layout.sort_column == click->iSubItem)
                    state->layout.sort_descending = !state->layout.sort_descending;
                else {
                    state->layout.sort_column = click->iSubItem;
                    state->layout.sort_descending = false;
                }
                update_table(*state); // preserves selected underlying row IDs
                show_sort_arrow(*state);
            }
            return TRUE;
        }
        if (header && header->idFrom == IDC_BATCH_LIST &&
            header->code == LVN_GETDISPINFOW) {
            auto* info = reinterpret_cast<NMLVDISPINFOW*>(lp);
            if ((info->item.mask & LVIF_TEXT) != 0 && info->item.iItem >= 0) {
                const auto view_index = static_cast<std::size_t>(info->item.iItem);
                if (view_index < state->view_order.size())
                    state->cell_buffer = cell_text(*state,
                        state->view_order[view_index], info->item.iSubItem);
                else
                    state->cell_buffer.clear();
                info->item.pszText = state->cell_buffer.data();
            }
            return TRUE;
        }
    }

    if (message == WM_CONTEXTMENU) {
        // Fallback for dialog-forwarded header notifications; the subclass
        // handles native keyboard and right-click delivery directly.
        const HWND requested = reinterpret_cast<HWND>(wp);
        if(requested==dialog && state->show_metadata) {
            // Right-click only the actual gap; ordinary list/header menus
            // and keyboard context menus keep their existing semantics.
            RECT master{},detail{};
            POINT point{};
            if(lp==static_cast<LPARAM>(-1)) {
                if(current_review_split_rects(*state,master,detail)) {
                    point.x=(master.right+detail.left)/2;
                    point.y=(std::max)(master.top,detail.top)+12;
                    ClientToScreen(dialog,&point);
                }
            } else {
                point.x=static_cast<short>(LOWORD(lp));
                point.y=static_cast<short>(HIWORD(lp));
            }
            POINT hit=point;
            if(ScreenToClient(dialog,&hit) &&
               current_review_split_rects(*state,master,detail) &&
               review_split_gap_hit(master,detail,hit.x,hit.y)) {
                const HMENU menu=CreatePopupMenu();
                if(!menu) return TRUE;
                constexpr UINT narrow=41520u,widen=41521u,reset=41522u;
                AppendMenuW(menu,MF_STRING,narrow,
                            L"Narrow source pane (Ctrl+Shift+Left)");
                AppendMenuW(menu,MF_STRING,widen,
                            L"Widen source pane (Ctrl+Shift+Right)");
                AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
                AppendMenuW(menu,MF_STRING,reset,
                            L"Reset divider (Ctrl+Shift+Home)");
                const UINT selected=TrackPopupMenu(menu,
                    TPM_RIGHTBUTTON|TPM_RETURNCMD|TPM_NONOTIFY,
                    point.x,point.y,0,dialog,nullptr);
                DestroyMenu(menu);
                const auto action=selected==narrow
                    ? NativeReviewSplitAction::NarrowSource
                    : selected==widen
                    ? NativeReviewSplitAction::WidenSource
                    : selected==reset
                    ? NativeReviewSplitAction::Reset
                    : NativeReviewSplitAction::None;
                if(native_review_split_valid_action(action))
                    SendMessageW(dialog,kNativeReviewSplitMessage,
                                 static_cast<WPARAM>(action),0);
                return TRUE;
            }
        }
        if (requested == state->list) {
            const HMENU menu = CreatePopupMenu();
            if (!menu) return TRUE;
            constexpr UINT inspectCommand = 41300u;
            AppendMenuW(menu, MF_STRING |
                (ListView_GetSelectedCount(state->list) ? MF_ENABLED : MF_GRAYED),
                inspectCommand, L"Inspect selected raw targets (read-only)");
            // A reverse link exists only after Inspect CUE produced a
            // current physical FILE graph. Ambiguous multiple CUE carriers
            // cannot be chosen by sorting or by a guessed basename.
            constexpr UINT relatedCueCommand = 41301u;
            std::size_t related_count=0;
            try { related_count=related_cues_for_selected_file(*state).size(); }
            catch(const std::exception&) { related_count=0; }
            AppendMenuW(menu, MF_STRING |
                (related_count==1 ? MF_ENABLED : MF_GRAYED),
                relatedCueCommand,
                related_count>1
                    ? L"Multiple related CUEs (inspect CUE queue)"
                    : L"Inspect related CUE (read-only)");
            POINT point{};
            if (lp == static_cast<LPARAM>(-1)) {
                RECT rect{};
                GetWindowRect(state->list, &rect);
                point.x = rect.left + 12;
                point.y = rect.top + 12;
            } else {
                point.x = static_cast<short>(LOWORD(lp));
                point.y = static_cast<short>(HIWORD(lp));
            }
            const UINT command = TrackPopupMenu(menu,
                TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y,
                0, dialog, nullptr);
            DestroyMenu(menu);
            if (command == inspectCommand || command == relatedCueCommand)
                SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(command, 0), 0);
            return TRUE;
        }
        if (requested == state->metadata_track_list) {
            if (show_active_track_grid_column_menu(dialog,*state,lp)) {
                update_master_table(*state);
                update_metadata_table(*state);
            }
            return TRUE;
        }
        if (requested == state->metadata_list) {
            if (show_active_detail_grid_column_menu(dialog,*state,lp))
                update_metadata_table(*state);
            return TRUE;
        }
        if (requested == state->musicbrainz_details) {
            // Contextual single-field read-only action. Disabled for search
            // snippets, unverified clipboard, physical tags and stale CUEs.
            constexpr UINT cuePreviewCommand=41410u;
            constexpr UINT copyEvidenceCommand=41411u;
            const int selected_group=ListView_GetNextItem(
                state->metadata_list,-1,LVNI_SELECTED);
            std::size_t evidence_group=state->musicbrainz_detail_groups.size();
            if(selected_group>=0 &&
               static_cast<std::size_t>(selected_group)<state->candidate_view_order.size())
                evidence_group=state->candidate_view_order[
                    static_cast<std::size_t>(selected_group)];
            const bool can_copy=native_evidence_receipt_allowed(
                state->show_candidate,state->musicbrainz_live_view,
                state->candidate_source_index==state->selected_track_index &&
                state->selected_track_index<state->entries.size(),
                state->stale,state->refreshing,
                state->musicbrainz_request || state->discogs_request ||
                state->beatport_public_request,
                ListView_GetSelectedCount(state->metadata_list),
                ListView_GetSelectedCount(state->musicbrainz_details),
                evidence_group,state->musicbrainz_detail_groups.size());
            const bool possible=state->show_candidate &&
                state->musicbrainz_release_loaded &&
                state->cue_postimage_snapshot.has_value() &&
                state->cue_postimage_source==state->selected_track_index &&
                state->candidate_source_index==state->selected_track_index &&
                state->cue_postimage_discogs==state->discogs_live_view &&
                !state->stale &&
                ListView_GetSelectedCount(state->musicbrainz_details)==1;
            const HMENU menu=CreatePopupMenu();
            if(!menu) return TRUE;
            AppendMenuW(menu,MF_STRING | (possible?MF_ENABLED:MF_GRAYED),
                        cuePreviewCommand,
                        L"Preview selected CUE values (read-only)");
            AppendMenuW(menu,MF_STRING | (can_copy?MF_ENABLED:MF_GRAYED),
                        copyEvidenceCommand,
                        L"Copy selected field evidence (Ctrl+C, read-only)");
            POINT point{};
            if(lp==static_cast<LPARAM>(-1)) {
                RECT rect{};
                GetWindowRect(state->musicbrainz_details,&rect);
                point.x=rect.left+16;point.y=rect.top+16;
            } else {
                point.x=static_cast<short>(LOWORD(lp));
                point.y=static_cast<short>(HIWORD(lp));
            }
            const UINT selected=TrackPopupMenu(
                menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,
                point.x,point.y,0,dialog,nullptr);
            DestroyMenu(menu);
            if(selected==cuePreviewCommand || selected==copyEvidenceCommand)
                SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(selected,0),0);
            return TRUE;
        }
        if (requested == ListView_GetHeader(state->list)) {
            show_column_menu(dialog, *state, lp);
            return TRUE;
        }
        if (requested == ListView_GetHeader(state->metadata_track_list)) {
            if (show_active_track_grid_column_menu(dialog,*state,lp)) {
                update_master_table(*state);
                update_metadata_table(*state);
            }
            return TRUE;
        }
        if (requested == ListView_GetHeader(state->metadata_list)) {
            if (show_active_detail_grid_column_menu(dialog,*state,lp))
                update_metadata_table(*state);
            return TRUE;
        }
    }
    if (message == WM_DESTROY) {
        SetDlgItemTextW(dialog,IDC_METADATA_DISCOGS_TOKEN,L"");
        try {
            save_batch_dialog_window_size(*state);
        } catch (const std::exception&) {
            // Invalid display state never blocks closing the preview.
        }
        // UI layout is independent of preview Cancel/Close. Save only display
        // preferences, never route edits, media metadata or file operations.
        try {
            store_review_split_offset(state->review_split_offset_96);
        } catch (const std::exception&) {
            // Corrupt visual geometry must not block closing.
        }
        try {
            if (state->initial_client_width <= 0 || !state->metadata_track_list ||
                !state->metadata_list || !state->list) return FALSE;
            capture_column_layout(*state);
            store_batch_table_layout(state->layout);
            if (state->native_grid_candidate_mode) {
                capture_review_grid_controls(state->metadata_track_list,
                    state->candidate_track_grid,current_dpi(state->metadata_track_list));
                capture_review_grid_controls(state->metadata_list,
                    state->candidate_detail_grid,current_dpi(state->metadata_list));
            } else if (state->show_metadata) {
                capture_review_grid_controls(state->metadata_track_list,
                    state->track_grid,current_dpi(state->metadata_track_list));
                capture_review_grid_controls(state->metadata_list,
                    state->detail_grid,current_dpi(state->metadata_list));
            }
            store_track_grid_layout(state->track_grid);
            store_detail_grid_layout(state->detail_grid);
            store_candidate_track_grid_layout(state->candidate_track_grid);
            if(state->online_detail_columns_applied)
                capture_review_grid_controls(state->musicbrainz_details,
                    state->online_detail_grid,current_dpi(state->musicbrainz_details));
            store_online_detail_grid_layout(state->online_detail_grid);
            store_candidate_detail_grid_layout(state->candidate_detail_grid);
        } catch (const std::exception&) {
            // Invalid profile display state is non-critical; retain defaults.
        }
        return FALSE;
    }

    if (message == WM_DPICHANGED) {
        const int new_dpi = static_cast<int>(LOWORD(wp));
        if (new_dpi >= 96 && new_dpi <= 768 && state->active_dpi != new_dpi) {
            // Capture at the *previous* DPI before applying a new scale.
            // Preserve hidden logical widths and current user resizing.
            if (state->native_grid_candidate_mode) {
                capture_review_grid_controls(state->metadata_track_list,
                    state->candidate_track_grid,state->active_dpi);
                capture_review_grid_controls(state->metadata_list,
                    state->candidate_detail_grid,state->active_dpi);
            } else if (state->show_metadata) {
                capture_review_grid_controls(state->metadata_track_list,
                    state->track_grid,state->active_dpi);
                capture_review_grid_controls(state->metadata_list,
                    state->detail_grid,state->active_dpi);
            }
            if (state->online_detail_columns_applied)
                capture_review_grid_controls(state->musicbrainz_details,
                    state->online_detail_grid,state->active_dpi);
            state->active_dpi = new_dpi;
            // WM_SIZE can run synchronously from SetWindowPos below.
            // Scale the hidden/visible lower table before that re-layout so
            // it cannot be captured at the new DPI with old pixel widths.
            if (state->online_detail_columns_applied)
                apply_review_grid_controls(state->musicbrainz_details,
                    state->online_detail_grid,state->active_dpi);
        }
        const auto* dimensions = reinterpret_cast<const RECT*>(lp);
        if (dimensions) SetWindowPos(dialog, nullptr,
            dimensions->left, dimensions->top,
            dimensions->right - dimensions->left,
            dimensions->bottom - dimensions->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        apply_review_grid_controls(state->metadata_track_list,
            state->native_grid_candidate_mode?state->candidate_track_grid:state->track_grid,
            state->active_dpi,
            state->native_grid_candidate_mode &&
            state->candidate_online_track_columns_active ? 3 : -1);
        apply_review_grid_controls(state->metadata_list,
            state->native_grid_candidate_mode?state->candidate_detail_grid:state->detail_grid,
            state->active_dpi);
        align_native_preview_form(dialog);
        return TRUE;
    }

    if (message != WM_COMMAND) return FALSE;
    const int id = LOWORD(wp);
    const auto native_command = native_preview_command(message, wp);
    try {
        if(id==IDC_METADATA_MB_QUERY && HIWORD(wp)==EN_CHANGE) {
            // A delayed response must fail closed even after edit-and-restore.
            invalidate_online_request(*state);
            state->search_plan_identity.clear();
            state->musicbrainz_has_published_attempt=false;
            if(state->discogs_provider_selected)
                SetDlgItemTextW(dialog,IDC_METADATA_MB_SEARCH,L"Search Discogs");
            update_live_provider_controls(*state);
            return TRUE;
        }
        if(id==IDC_METADATA_ONLINE_PROVIDER && HIWORD(wp)==CBN_SELCHANGE) {
            if(!confirm_online_preview_navigation(
                   dialog,*state,state->selected_track_index,true)) {
                const int original=state->beatport_offline_selected?2:
                    state->discogs_provider_selected?1:0;
                SendDlgItemMessageW(dialog,IDC_METADATA_ONLINE_PROVIDER,
                    CB_SETCURSEL,original,0);
                return TRUE;
            }
            invalidate_online_request(*state);
            const auto selected_provider=SendDlgItemMessageW(
                dialog,IDC_METADATA_ONLINE_PROVIDER,CB_GETCURSEL,0,0);
            const bool was_beatport=state->beatport_offline_selected;
            state->discogs_provider_selected=selected_provider==1;
            state->beatport_offline_selected=selected_provider==2;
            update_workspace_search_tooltip(*state);
            const auto query_transition=djmeta::provider_query_transition(
                was_beatport,state->beatport_offline_selected);
            if(query_transition!=djmeta::ProviderQueryTransition::Preserve) {
                // Beatport URL may be valid but belongs to another provider.
                // Clear it before suggesting local TITLE/ALBUM on return.
                SetDlgItemTextW(dialog,IDC_METADATA_MB_QUERY,L"");
                state->auto_search_text.clear();
                state->auto_search_track=state->selected_track_index;
            }
            SendDlgItemMessageW(dialog,IDC_METADATA_MB_QUERY,EM_SETCUEBANNER,
                FALSE,reinterpret_cast<LPARAM>(
                    state->beatport_offline_selected
                    ? L"Exact Beatport URL (or paste from clipboard)"
                    : state->discogs_provider_selected
                        ? L"Title or Discogs release/master URL"
                        : L"TITLE, album: NAME or MusicBrainz URL"));
            if(!state->discogs_provider_selected)
                SetDlgItemTextW(dialog,IDC_METADATA_DISCOGS_TOKEN,L"");
            state->musicbrainz_live_view=false;
            state->musicbrainz_release_loaded=false;
            state->discogs_live_view=false;
            state->discogs_results.clear();
            state->discogs_master_id=0;
            state->discogs_master_page=state->discogs_master_pages=0;
            state->discogs_direct_edition.reset();
            state->online_physical_edition.reset();
            state->online_field_snapshot.reset();
            state->online_field_preview.reset();
            state->online_included_fields.clear();
            state->online_date_targets.clear();
            state->online_raw_target_observation.reset();
            state->online_edition_source=(std::numeric_limits<std::size_t>::max)();
            state->musicbrainz_results.clear();
            state->musicbrainz_detail_groups.clear();
            state->candidate_rows.clear();
            state->search_plan_identity.clear();
            state->search_attempt_index=0;
            state->musicbrainz_has_published_attempt=false;
            state->search_attempt_notice.clear();
            update_live_provider_controls(*state);
            update_metadata_table(*state);
            if(query_transition==djmeta::ProviderQueryTransition::ClearAndSuggest)
                prefill_musicbrainz_query(*state); // no network until explicit Search
            if(state->beatport_offline_selected)
                SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                    native_workspace_display_hint(state->stale,
                        L"Beatport offline: copy a public page's JSON-LD, "
                        L"authorized track-detail JSON "
                        L"(@beatport-track-json), complete edition+tracks JSON "
                        L"or explicit @beatport-track-facts, or use the "
                        L"@crates-public-track public-page helper, then Import clipboard. "
                        L"Alternatively enter ONE public track URL and click "
                        L"Load Beatport URL. Catalog API access remains separate."));
            return TRUE;
        }
        if (id == IDCANCEL) {
            // ESC/Cancel follows the same modeless close lifecycle as the
            // caption X. Never join a live WinHTTP worker on the UI thread.
            SendMessageW(dialog,WM_CLOSE,0,0);
            return TRUE;
        }
        if (id == IDC_BATCH_REFRESH && HIWORD(wp) == BN_CLICKED) {
            refresh_workspace_snapshot(*state);
            return TRUE;
        }
        if (id == IDC_BATCH_HELP && HIWORD(wp) == BN_CLICKED) {
            show_workspace_help(dialog);
            return TRUE;
        }
        if(id==IDC_ONLINE_DATE_TARGET && HIWORD(wp)==CBN_SELCHANGE) {
            const auto choice=static_cast<int>(SendDlgItemMessageW(
                dialog,IDC_ONLINE_DATE_TARGET,CB_GETCURSEL,0,0));
            store_online_date_target_preference(choice);
            SetDlgItemTextW(dialog,IDC_ONLINE_INCLUDE_FIELDS,
                choice==6 || choice==7
                  ? L"Preview both dates" : L"Include fields (preview)");
            if(choice==6 || choice==7)
                SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                    native_workspace_display_hint(state->stale,
                        choice==6
                          ? L"Dual date preview: master year -> ORIGINALDATE; "
                            L"exact edition date -> RELEASEDATE. "
                            L"Click Preview both dates (nothing is written)."
                          : L"Dual date preview: master year -> DATE; "
                            L"exact edition date -> RELEASEDATE. "
                            L"Click Preview both dates (nothing is written)."));
            return TRUE;
        }
        if ((id==IDC_ONLINE_INCLUDE_FIELDS || id==IDC_ONLINE_IGNORE_FIELDS ||
             id==IDC_ONLINE_RESET_FIELDS) && HIWORD(wp)==BN_CLICKED) {
            change_online_field_preview(*state,id);
            return TRUE;
        }
        if(id==41411u) {
            // Explicit clipboard action, with a fresh immutable source probe.
            // The current selected candidate/field identities are checked
            // before constructing anything; no provider request or SDK writer.
            revalidate_workspace(*state);
            const int selected_group=ListView_GetNextItem(
                state->metadata_list,-1,LVNI_SELECTED);
            std::size_t group=state->musicbrainz_detail_groups.size();
            if(selected_group>=0 &&
               static_cast<std::size_t>(selected_group)<state->candidate_view_order.size())
                group=state->candidate_view_order[
                    static_cast<std::size_t>(selected_group)];
            const int field_count=ListView_GetSelectedCount(state->musicbrainz_details);
            if(!native_evidence_receipt_allowed(
                  state->show_candidate,state->musicbrainz_live_view,
                  state->candidate_source_index==state->selected_track_index &&
                  state->selected_track_index<state->entries.size(),
                  state->stale,state->refreshing,
                  state->musicbrainz_request || state->discogs_request ||
                  state->beatport_public_request,
                  ListView_GetSelectedCount(state->metadata_list),field_count,
                  group,state->musicbrainz_detail_groups.size()))
                throw std::invalid_argument(
                    "Select one current provider result and 1-128 evidence fields.");
            const auto& rows=state->musicbrainz_detail_groups[group];
            std::vector<NativeFieldEvidenceReceiptRow> receipt_rows;
            receipt_rows.reserve(static_cast<std::size_t>(field_count));
            int selected=-1;
            while((selected=ListView_GetNextItem(
                       state->musicbrainz_details,selected,LVNI_SELECTED))>=0) {
                if(selected>=1024 || selected<0 ||
                   static_cast<std::size_t>(selected)>=rows.size())
                    throw std::invalid_argument(
                        "Selected field is outside the current displayed evidence.");
                const auto& item=rows[static_cast<std::size_t>(selected)];
                const bool local=djmeta::online::is_local_physical_original(item);
                const bool cue=djmeta::online::is_local_cue_inventory(item);
                NativeFieldEvidenceReceiptRow field;
                field.field=from_utf8(item.field);
                field.origin=cue?from_utf8(item.candidate.provider):
                    from_utf8(djmeta::online::online_evidence_origin_label(item));
                field.source_id=local?L"(local physical tag; no provider ID)":
                    from_utf8(item.candidate.source_id);
                field.scope=local?L"original physical audio metadata":
                    cue?L"original CUE carrier inventory":
                    from_utf8(djmeta::online::online_evidence_scope_label(
                        item.candidate.scope));
                field.date_meaning=from_utf8(
                    djmeta::online::online_evidence_date_label(
                        item.candidate.date_meaning));
                field.reason=from_utf8(item.reason);
                wchar_t status[512]{};
                ListView_GetItemText(state->musicbrainz_details,selected,3,status,512);
                field.status=status;
                for(const auto& value:item.original_values)
                    field.original_values.push_back(from_utf8(value));
                if(!local && !cue)
                    for(const auto& value:item.candidate.values)
                        field.evidence_values.push_back(from_utf8(value));
                receipt_rows.push_back(std::move(field));
            }
            const auto receipt=native_render_evidence_receipt(
                state->selected_track_index,receipt_rows);
            if(!receipt)
                throw std::invalid_argument("Evidence receipt invalid or too large.");
            native_copy_audit_text_to_clipboard(dialog,*receipt);
            SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                L"Selected exact-source field evidence copied with provenance, "
                L"status and ordered multivalues. Read-only; nothing written.");
            return TRUE;
        }
        if(id==41410u) {
            // Explicitly selected, immutable provider edition evidence only.
            // This command shows a pre/post value in memory; no Save/Apply.
            if(!state->show_candidate || !state->musicbrainz_release_loaded ||
               !state->cue_postimage_snapshot ||
               state->cue_postimage_source!=state->selected_track_index ||
               state->candidate_source_index!=state->selected_track_index ||
               state->cue_postimage_discogs!=state->discogs_live_view ||
               state->selected_track_index>=state->entries.size() ||
               state->discogs_request || state->musicbrainz_request ||
               state->beatport_public_request || state->stale)
                throw std::invalid_argument(
                    "No verified current CUE edition is available for a value preview.");
            const int selected_count=ListView_GetSelectedCount(
                state->musicbrainz_details);
            if(selected_count<=0 || selected_count>512)
                throw std::invalid_argument(
                    "Select up to 512 CUE fields in the lower detail pane.");
            const int active=ListView_GetNextItem(
                state->metadata_list,-1,LVNI_SELECTED);
            if(active<0 ||
               static_cast<std::size_t>(active)>=state->candidate_view_order.size())
                throw std::invalid_argument("CUE detail group selection expired.");
            const auto group=state->candidate_view_order[
                static_cast<std::size_t>(active)];
            if(group>=state->musicbrainz_detail_groups.size())
                throw std::invalid_argument("CUE detail group selection expired.");
            const auto& displayed=state->musicbrainz_detail_groups[group];
            std::vector<std::size_t> selected_display_rows;
            int chosen=-1;
            while((chosen=ListView_GetNextItem(
                state->musicbrainz_details,chosen,LVNI_SELECTED))>=0)
                selected_display_rows.push_back(static_cast<std::size_t>(chosen));
            const auto selected_indices=
                djmeta::online::resolve_cue_detail_selection(
                    *state->cue_postimage_snapshot,displayed,selected_display_rows);
            const auto& entry=state->entries[state->selected_track_index];
            verify_snapshot(entry);
            verify_rules_snapshot(state->captured_rules);
            const auto source=read_cue_raw_on_demand(
                entry.handle,entry.input.source_path);
            if(source.carrier!=state->cue_postimage_snapshot->carrier)
                throw std::runtime_error("CUE carrier changed; refresh release.");
            const auto proposal=djmeta::online::preview_selected_cue_review_postimage(
                *state->cue_postimage_snapshot,source.raw_text,selected_indices);
            if(!proposal.eligible)
                throw std::runtime_error("CUE preview refused: "+proposal.rejection);
            // Only a qualified physical subsong-0 source may construct a
            // detached SDK CUESHEET postimage. Virtual tracks still receive
            // read-only comparison; they NEVER become physical writers.
            if(source.carrier==djmeta::CueCarrierKind::EmbeddedText &&
               state->cue_postimage_snapshot->verified_edition &&
               entry.input.physical_source_qualified &&
               entry.handle->get_subsong_index()==0) {
                const auto observed=probe_host_file_readonly(entry.input.source_path);
                if(observed.state!=HostFileState::ExistingFile ||
                   observed.host_physical_key!=entry.observed_physical_key ||
                   observed.source_guard!=entry.observed_source_guard)
                    throw std::runtime_error(
                        "Embedded CUE physical source changed during SDK review.");
                const auto tag_info=entry.handle->get_info_ref();
                const auto tags=metadata_from_file_info(tag_info->info());
                const djmeta::PhysicalTagSourceSnapshot physical{
                    true,false,observed.host_physical_key,
                    observed.source_guard,tags};
                file_info_impl detached;
                const auto sdk=djmeta::online::project_embedded_cue_into_physical_sdk(
                    tag_info->info(),detached,physical,
                    observed.host_physical_key,observed.source_guard,
                    *state->cue_postimage_snapshot,selected_indices,proposal,
                    [](const file_info& value) {
                        return metadata_from_file_info(value);
                    });
                if(sdk.status==djmeta::online::EmbeddedCueSdkStatus::Blocked ||
                   (sdk.status==djmeta::online::EmbeddedCueSdkStatus::Detached)!=
                      proposal.changed)
                    throw std::runtime_error(
                        "Embedded CUE SDK postimage refused: "+sdk.rejection);
                if(proposal.changed && !file_info::g_is_info_equal(
                        tag_info->info(),detached))
                    throw std::runtime_error(
                        "Embedded CUE SDK postimage altered technical metadata.");
            }
            // Render all selected CUE fields together in the existing
            // modeless comparison grid, without a truncated modal popup.
            state->cue_value_preview=proposal;
            state->cue_value_preview_indices=selected_indices;
            update_online_field_preview_controls(*state);
            std::size_t changed=0;
            for(auto idx:selected_indices)
                if(state->cue_postimage_snapshot->rows[idx].reviewed.state==
                   djmeta::online::FieldReviewState::NeedsReview)
                    ++changed;
            refresh_musicbrainz_detail_rows(*state);
            for(const auto row:selected_display_rows)
                ListView_SetItemState(state->musicbrainz_details,
                    static_cast<int>(row),LVIS_SELECTED,LVIS_SELECTED);
            const std::wstring cue_preview_notice=L"CUE preview: "+
                std::to_wstring(selected_indices.size())+L" selected; "+
                std::to_wstring(changed)+L" proposed changes; "+
                std::to_wstring(selected_indices.size()-changed)+
                L" no-ops. See every field in the table. Read-only; no writes.";
            SetDlgItemTextW(dialog,IDC_BATCH_HINT,cue_preview_notice.c_str());
            return TRUE;
        }
        const auto check_scope=native_workspace_command_check_scope(
            native_command,message,wp);
        if (check_scope!=NativeWorkspaceCheckScope::None) {
            // Provider lookup, release load and CUE inspection check the
            // selected source. Batch approval and Apply check every source.
            revalidate_workspace(*state,
                check_scope==NativeWorkspaceCheckScope::FullBatch);
            if (state->stale) return TRUE;
        }
        if(id==IDC_METADATA_DISCOGS_NEXT_PAGE && HIWORD(wp)==BN_CLICKED) {
            if(state->beatport_offline_selected)
                open_crates_for_selected_physical_track(dialog,*state);
            else if(!state->discogs_provider_selected) {
                // Explicit page navigation of the currently published
                // exact source/query/strategy; no automatic HTTP fan-out.
                if(state->stale || state->refreshing ||
                   !state->show_candidate || !state->musicbrainz_live_view ||
                   state->discogs_live_view || state->musicbrainz_release_loaded ||
                   state->selected_track_index!=state->candidate_source_index ||
                   state->musicbrainz_request || state->discogs_request ||
                   !state->musicbrainz_more_pages ||
                   state->musicbrainz_result_page==0 ||
                   state->musicbrainz_result_page>=
                       djmeta::online::musicbrainz::max_search_page ||
                   state->search_plan_identity!=state->musicbrainz_page_identity ||
                   state->search_attempt_index!=
                       state->musicbrainz_published_attempt_index ||
                   state->musicbrainz_results.size()>
                       djmeta::online::musicbrainz::max_search_candidates*7)
                    throw std::invalid_argument(
                        "No qualified MusicBrainz results page remains.");
                const auto next=state->musicbrainz_result_page+1;
                const auto path=djmeta::online::musicbrainz::musicbrainz_search_page_path(
                    state->musicbrainz_page_one_path,next);
                auto& entry=state->entries.at(state->selected_track_index);
                verify_online_source_snapshot(*state,entry);
                verify_rules_snapshot(state->captured_rules);
                if(state->musicbrainz_page_cue_mode &&
                   read_cue_raw_on_demand(entry.handle,entry.input.source_path)
                       .raw_text!=state->musicbrainz_page_cue_before)
                    throw std::runtime_error("CUE changed before next page.");
                PendingMusicBrainzContext request;
                request.operation=PendingMusicBrainzContext::Kind::Search;
                request.source_index=state->selected_track_index;
                request.kind=state->musicbrainz_kind;
                request.cue_mode=state->musicbrainz_page_cue_mode;
                request.cue_before=state->musicbrainz_page_cue_before;
                request.local_title=state->musicbrainz_original_title;
                request.local_artist=state->musicbrainz_original_artist;
                request.originals=state->musicbrainz_page_originals;
                request.strategy=state->musicbrainz_page_strategy;
                request.plan_identity=state->musicbrainz_page_identity;
                request.attempt_index=state->musicbrainz_published_attempt_index;
                request.total=state->musicbrainz_page_attempt_total;
                request.page=next;
                request.page_one_path=state->musicbrainz_page_one_path;
                SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                    (L"Loading MusicBrainz page "+std::to_wstring(next)+
                     L" (read-only)...").c_str());
                begin_musicbrainz_network(dialog,*state,path,std::move(request));
            } else
                perform_discogs_next_page(dialog,*state);
            return TRUE;
        }
        if (id == IDC_METADATA_MB_SEARCH && HIWORD(wp) == BN_CLICKED) {
            // Cancel the pending request on the same native Search button.
            // Invalidate ONLY the network response epoch, preserving every
            // existing local/online review and CUE/naming what-if postimage.
            // Completion joins the stopped worker asynchronously.
            const bool pending=state->musicbrainz_request ||
                state->discogs_request || state->beatport_public_request;
            if(pending) {
                if(state->online_user_cancel_requested) return TRUE;
                state->online_context_version.invalidate();
                state->online_user_cancel_requested=true;
                if(state->musicbrainz_request)
                    request_readonly_network_stop(*state->musicbrainz_request);
                if(state->discogs_request)
                    request_readonly_network_stop(*state->discogs_request);
                if(state->beatport_public_request)
                    request_readonly_network_stop(*state->beatport_public_request);
                update_live_provider_controls(*state);
                SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                    L"Cancelling read-only provider lookup. Existing comparisons "
                    L"and review decisions remain intact.");
                return TRUE;
            }
            if(!state->online_included_fields.empty())
                throw std::runtime_error(
                    "Reset included Discogs fields before starting another search. "
                    "The selected field choices remain intact.");
            if(state->beatport_offline_selected) {
                // Crates hand-off can place one exact track URL on clipboard.
                // Read clipboard ONLY after this explicit click, and ONLY when
                // the URL box is empty. Refuse anything other than an exact
                // public Beatport HTTPS track path before network dispatch.
                if(read_control(dialog,IDC_METADATA_MB_QUERY).empty()) {
                    const auto pasted=read_manual_clipboard_text(dialog);
                    const auto qualified=
                        djmeta::online::qualify_beatport_public_track_url(pasted);
                    SetDlgItemTextW(dialog,IDC_METADATA_MB_QUERY,
                        from_utf8(qualified.canonical_url).c_str());
                }
                perform_beatport_public_page_lookup(dialog,*state);
                return TRUE;
            }
            if(state->musicbrainz_request || state->discogs_request ||
               state->beatport_public_request)
                throw std::runtime_error("Wait for the current provider lookup.");
            if(state->discogs_provider_selected) {
                perform_discogs_search(dialog,*state);
                return TRUE;
            }
            if (!state->show_candidate ||
                state->selected_track_index >= state->entries.size())
                throw std::invalid_argument("Select a physical audio or CUE source first.");
            const auto& entry=state->entries[state->selected_track_index];
            verify_snapshot(entry);
            verify_rules_snapshot(state->captured_rules);

            const bool cue_mode=is_external_cue_locator(entry.input.source_path) ||
                (state->candidate_source_index==state->selected_track_index &&
                 (state->cue_inspection_mode ||
                  state->cue_candidate_comparison_mode ||
                  (state->musicbrainz_live_view &&
                   state->musicbrainz_kind==
                     djmeta::online::musicbrainz::SearchKind::Release &&
                   state->musicbrainz_page_cue_mode)));
            std::string local_title, local_artist, local_isrc, local_barcode, cue_before;
            djmeta::MetadataDocument original_tags;
            // An explicit "album:" request is a RELEASE search even for a
            // physical song; ordinary physical searches stay RECORDING-only.
            const auto entered=to_utf8(read_control(dialog,IDC_METADATA_MB_QUERY));
            const auto album_term=cue_mode
                ? std::optional<std::string>{}
                : djmeta::online::musicbrainz::explicit_physical_album_term(entered);
            const auto kind=(cue_mode || album_term)
                ? djmeta::online::musicbrainz::SearchKind::Release
                : djmeta::online::musicbrainz::SearchKind::Recording;
            if (cue_mode) {
                const auto raw=read_cue_raw_on_demand(
                    entry.handle,entry.input.source_path);
                const auto inventory=djmeta::inspect_cue_metadata(
                    raw.raw_text,raw.carrier);
                if (inventory.status!=djmeta::CueSyntaxStatus::Parsed)
                    throw std::invalid_argument(
                        "This CUE must have a qualified track inventory to search.");
                local_title=unique_cue_album_value(inventory,"TITLE");
                local_artist=unique_cue_album_value(inventory,"PERFORMER");
                try { local_barcode=unique_cue_album_value(inventory,"CATALOG"); }
                catch (const std::invalid_argument&) {} // ambiguous barcode is excluded
                cue_before=raw.raw_text;
            } else {
                if (!entry.input.physical_source_qualified)
                    throw std::invalid_argument(
                        "Online search requires a uniquely qualified physical file "
                        "or a CUE previously selected with Inspect CUE.");
                const auto info=entry.handle->get_info_ref();
                original_tags=metadata_from_file_info(info->info());
                local_title=unique_metadata_value(original_tags,"TITLE");
                local_artist=unique_metadata_value(original_tags,"ARTIST");
                try {local_isrc=unique_metadata_value(original_tags,"ISRC");}
                catch (const std::invalid_argument&) {} // ambiguous ISRC is excluded
            }
            if(!cue_mode && kind==djmeta::online::musicbrainz::SearchKind::Release) {
                try {
                    const auto album_artist=unique_metadata_value(
                        original_tags,"ALBUM ARTIST");
                    if(!album_artist.empty()) local_artist=album_artist;
                } catch(const std::invalid_argument&) {
                    local_artist.clear(); // ambiguous album artist is not search evidence
                }
            }
            const auto exact_url=
                djmeta::online::musicbrainz::parse_exact_entity_url(entered);
            if(exact_url) {
                if(exact_url->kind!=kind &&
                   !( !cue_mode &&
                      exact_url->kind==
                          djmeta::online::musicbrainz::SearchKind::Release))
                    throw std::invalid_argument(
                        kind==djmeta::online::musicbrainz::SearchKind::Release
                            ? "This CUE needs an exact MusicBrainz RELEASE URL, not a recording URL."
                            : "Use an exact recording/release URL or type album: before an album title.");
                // One explicit gesture, no Lucene fallback or synthetic hit.
                // Rebuild the official fixed-host route from the verified ID.
                state->search_plan_identity.clear();
                state->search_attempt_index=0;
                state->musicbrainz_has_published_attempt=false;
                PendingMusicBrainzContext request;
                request.source_index=state->selected_track_index;
                request.kind=exact_url->kind;
                request.cue_mode=cue_mode;
                request.cue_before=std::move(cue_before);
                request.originals=std::move(original_tags);
                request.local_title=std::move(local_title);
                request.local_artist=std::move(local_artist);
                request.strategy=exact_url->kind==
                    djmeta::online::musicbrainz::SearchKind::Release
                    ? "Exact release URL" : "Exact recording URL";
                request.total=1;
                std::string route;
                if(exact_url->kind==djmeta::online::musicbrainz::SearchKind::Release) {
                    request.operation=PendingMusicBrainzContext::Kind::Release;
                    request.direct_release=true;
                    request.release_mbid=exact_url->mbid;
                    route=djmeta::online::musicbrainz::make_release_lookup_path(
                        exact_url->mbid);
                } else {
                    request.operation=PendingMusicBrainzContext::Kind::Search;
                    request.exact_recording_mbid=exact_url->mbid;
                    route=djmeta::online::musicbrainz::
                        make_exact_recording_search_path(exact_url->mbid);
                }
                SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                    L"Inspecting exact MusicBrainz identity (official HTTPS, read-only)...");
                begin_musicbrainz_network(dialog,*state,std::move(route),
                                          std::move(request));
                return TRUE;
            }
            const bool suggested_query = entered.empty() ||
                (state->auto_search_track == state->selected_track_index &&
                 entered == to_utf8(state->auto_search_text));
            const std::string term=album_term
                ? *album_term : entered.empty()?local_title:entered;
            const bool identifier_only=suggested_query &&
                (kind==djmeta::online::musicbrainz::SearchKind::Recording
                    ? djmeta::online::musicbrainz::valid_search_isrc(local_isrc)
                    : djmeta::online::musicbrainz::valid_release_search_barcode(
                          local_barcode));
            if (term.empty() && !identifier_only)
                throw std::invalid_argument(
                    "No TITLE is available. Enter a title/album or provide a valid "
                    "physical ISRC or global CUE CATALOG barcode.");
            // One explicit request per activation; never silently loop through
            // an unbounded provider search or block foobar on multiple retries.
            // A zero-result next click broadens to the next visible strategy.
            // For CUE we search the qualified release, not a virtual subtrack.
            using namespace djmeta::online::musicbrainz;
            std::string path;
            std::string strategy;
            std::size_t strategy_count=1;
            if (kind==SearchKind::Recording) {
                const auto plan=recording_search_plan(
                    term,local_artist,
                    suggested_query?local_isrc:std::string{});
                const auto identity=std::string("recording|")+
                    entry.input.source_path+"|"+
                    std::to_string(state->selected_track_index)+"|"+
                    term+"|"+local_artist+"|"+
                    (suggested_query?local_isrc:"");
                if(identity!=state->search_plan_identity) {
                    state->search_attempt_index=0;
                    state->search_plan_identity=identity;
                    state->musicbrainz_has_published_attempt=false;
                } else {
                    state->search_attempt_index=next_clicked_search_attempt(
                        state->musicbrainz_published_attempt_index,plan.attempts.size(),
                        state->musicbrainz_has_published_attempt &&
                        state->musicbrainz_live_view &&
                        state->musicbrainz_kind==kind &&
                        state->candidate_source_index==state->selected_track_index);
                }
                state->search_plan=plan;
                const auto& attempt=plan.attempts.at(state->search_attempt_index);
                path=attempt.path;
                strategy=attempt.label;
                strategy_count=plan.attempts.size();
            } else {
                const auto release_attempts=release_search_plan(
                    term,local_artist,
                    suggested_query?local_barcode:std::string{});
                const auto identity=std::string("release|")+
                    entry.input.source_path+"|"+
                    std::to_string(state->selected_track_index)+"|"+
                    term+"|"+local_artist+"|"+
                    (suggested_query?local_barcode:"");
                if(identity!=state->search_plan_identity) {
                    state->search_attempt_index=0;
                    state->search_plan_identity=identity;
                    state->musicbrainz_has_published_attempt=false;
                } else {
                    state->search_attempt_index=next_clicked_search_attempt(
                        state->musicbrainz_published_attempt_index,release_attempts.size(),
                        state->musicbrainz_has_published_attempt &&
                        state->musicbrainz_live_view &&
                        state->musicbrainz_kind==kind &&
                        state->candidate_source_index==state->selected_track_index);
                }
                const auto& attempt=release_attempts.at(
                    state->search_attempt_index);
                path=attempt.path;
                strategy=attempt.label;
                strategy_count=release_attempts.size();
            }
            const std::wstring status=std::wstring(L"Searching MusicBrainz (read-only): ")+
                from_utf8(strategy)+L" ["+
                std::to_wstring(state->search_attempt_index+1)+L"/"+
                std::to_wstring(strategy_count)+L"]...";
            SetDlgItemTextW(dialog,IDC_BATCH_HINT,status.c_str());
            RedrawWindow(dialog,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
            PendingMusicBrainzContext request;
            request.operation=PendingMusicBrainzContext::Kind::Search;
            request.source_index=state->selected_track_index;
            request.kind=kind;
            request.cue_mode=cue_mode;
            request.cue_before=std::move(cue_before);
            request.originals=std::move(original_tags);
            request.local_title=std::move(local_title);
            request.local_artist=std::move(local_artist);
            request.strategy=std::move(strategy);
            request.plan_identity=state->search_plan_identity;
            request.attempt_index=state->search_attempt_index;
            request.total=strategy_count;
            request.page_one_path=path;
            begin_musicbrainz_network(dialog,*state,std::move(path),
                                      std::move(request));
            return TRUE;
        }
        if (id == IDC_METADATA_MB_LOAD_RELEASE && HIWORD(wp)==BN_CLICKED) {
            if(state->musicbrainz_request || state->discogs_request)
                throw std::runtime_error("Wait for the current provider lookup.");
            if(state->discogs_live_view) {
                perform_discogs_load(dialog,*state);
                return TRUE;
            }
            if (!state->show_candidate || !state->musicbrainz_live_view ||
                state->musicbrainz_release_loaded ||
                state->musicbrainz_kind!=
                    djmeta::online::musicbrainz::SearchKind::Release ||
                state->candidate_source_index!=state->selected_track_index ||
                state->selected_track_index>=state->entries.size())
                throw std::invalid_argument(
                    "Search for a MusicBrainz release before loading its tracks.");
            const int selected=ListView_GetNextItem(
                state->metadata_list,-1,LVNI_SELECTED);
            if (selected<0 ||
                static_cast<std::size_t>(selected)>=state->candidate_view_order.size())
                throw std::invalid_argument(
                    "Select one candidate row in the right table first.");
            const auto rowid=state->candidate_view_order[
                static_cast<std::size_t>(selected)];
            if(rowid>=state->candidate_rows.size())
                throw std::invalid_argument("Candidate selection expired.");
            const auto mbid=state->candidate_rows[rowid].candidate.source_id;
            bool found=false;
            for(const auto& result:state->musicbrainz_results)
                if (result.kind==djmeta::online::musicbrainz::SearchKind::Release &&
                    result.mbid==mbid) found=true;
            if(!found || !djmeta::online::musicbrainz::valid_mbid(mbid))
                throw std::invalid_argument("Selected release was not verified by this search.");

            const auto& entry=state->entries[state->selected_track_index];
            verify_snapshot(entry);
            verify_rules_snapshot(state->captured_rules);
            const bool release_cue_mode=state->musicbrainz_page_cue_mode;
            std::string cue_before;
            if(release_cue_mode) {
                const auto raw=read_cue_raw_on_demand(
                    entry.handle,entry.input.source_path);
                const auto cue=djmeta::inspect_cue_metadata(
                    raw.raw_text,raw.carrier);
                if(cue.status!=djmeta::CueSyntaxStatus::Parsed)
                    throw std::invalid_argument(
                        "Selected CUE track inventory is unqualified.");
                cue_before=raw.raw_text;
            } else if(!entry.input.physical_source_qualified ||
                      entry.handle->get_subsong_index()!=0 ||
                      is_external_cue_locator(entry.input.source_path)) {
                throw std::invalid_argument(
                    "MusicBrainz physical release source is unqualified.");
            }
            SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                L"Retrieving the selected MusicBrainz release tracklist (read-only)...");
            RedrawWindow(dialog,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
            PendingMusicBrainzContext request;
            request.operation=PendingMusicBrainzContext::Kind::Release;
            request.source_index=state->selected_track_index;
            request.kind=djmeta::online::musicbrainz::SearchKind::Release;
            request.cue_mode=release_cue_mode;
            request.cue_before=std::move(cue_before);
            request.originals=state->musicbrainz_page_originals;
            request.release_mbid=mbid;
            begin_musicbrainz_network(dialog,*state,
                djmeta::online::musicbrainz::make_release_lookup_path(mbid),
                std::move(request));
            return TRUE;
        }
        if (id == IDC_METADATA_INSPECT_CUE && HIWORD(wp) == BN_CLICKED) {
            if(!state->online_included_fields.empty())
                throw std::runtime_error(
                    "Reset included online fields before replacing this view with CUE evidence.");
            invalidate_online_request(*state);
            if (!state->show_candidate ||
                state->selected_track_index >= state->entries.size())
                throw std::invalid_argument("Select one audio/CUE source to inspect.");
            auto& entry = state->entries[state->selected_track_index];
            verify_snapshot(entry);
            verify_rules_snapshot(state->captured_rules);
            // Qualify the physical backing source solely as a READ-ONLY
            // snapshot guard for virtual audio tracks. Do not touch
            // physical_source_qualified, which governs actual file writes.
            if (entry.handle->get_subsong_index() != 0 &&
                !is_external_cue_locator(entry.input.source_path) &&
                entry.observed_source_guard.empty()) {
                const auto observation =
                    probe_host_file_readonly(entry.input.source_path);
                if (observation.state != HostFileState::ExistingFile ||
                    observation.host_physical_key.empty() ||
                    observation.source_guard.empty())
                    throw std::invalid_argument(
                        "Virtual embedded CUE has no qualified on-disk source.");
                entry.observed_physical_key = observation.host_physical_key;
                entry.observed_source_guard = observation.source_guard;
            }
            const auto raw = read_cue_raw_on_demand(entry.handle, entry.input.source_path);
            const auto inventory =
                djmeta::inspect_cue_metadata(raw.raw_text, raw.carrier);
            if (inventory.status == djmeta::CueSyntaxStatus::Invalid ||
                inventory.encoding == djmeta::CueTextEncoding::Unknown ||
                inventory.encoding == djmeta::CueTextEncoding::UnsupportedUtf16)
                throw std::invalid_argument(
                    "CUE syntax or encoding is not qualified for read-only inventory.");
            // Inspect CUE is deliberate read-only work. Offer its real album
            // TITLE as the next release search term without contacting MB.
            if (inventory.status == djmeta::CueSyntaxStatus::Parsed) {
                try {
                    const auto album=unique_cue_album_value(inventory,"TITLE");
                    if (!album.empty()) {
                        state->auto_search_text=from_utf8(album);
                        state->auto_search_track=state->selected_track_index;
                        SetDlgItemTextW(dialog,IDC_METADATA_MB_QUERY,
                                        state->auto_search_text.c_str());
                    }
                } catch (const std::exception&) {
                    // An ambiguous CUE title requires manual search text.
                }
            }
            // Shared source-only inventory for Inspect CUE and Discogs.
            auto fields=djmeta::online::cue_original_inventory_rows(inventory);
            append_external_cue_file_reference_evidence(*state,entry,inventory,fields);
            state->candidate_rows = std::move(fields);
            state->candidate_source_index = state->selected_track_index;
            state->cue_inspection_mode = true;
            state->cue_candidate_comparison_mode = false;
            state->musicbrainz_live_view = false;
            state->discogs_live_view = false;
            state->discogs_results.clear();
            state->discogs_master_id=0;
            state->discogs_master_page=state->discogs_master_pages=0;
            state->discogs_direct_edition.reset();
            state->discogs_search_was_cue = false;
            state->musicbrainz_results.clear();
            state->musicbrainz_detail_groups.clear();
            update_metadata_table(*state);
            return TRUE;
        }
        if (id == IDC_METADATA_IMPORT_CANDIDATE && HIWORD(wp) == BN_CLICKED) {
            if(!state->online_included_fields.empty())
                throw std::runtime_error(
                    "Reset included online fields before replacing this view with clipboard evidence.");
            invalidate_online_request(*state);
            if (!state->show_candidate ||
                state->selected_track_index >= state->entries.size())
                throw std::invalid_argument("Select a track before importing a candidate.");
            const auto& entry = state->entries[state->selected_track_index];
            verify_snapshot(entry);
            verify_rules_snapshot(state->captured_rules);
            const bool compare_cue =
                (state->cue_inspection_mode || state->cue_candidate_comparison_mode) &&
                state->candidate_source_index == state->selected_track_index;
            const auto text = read_manual_clipboard_text(dialog);
            const bool beatport_user_json =
                djmeta::online::is_beatport_user_json_clipboard(text);
            const bool beatport_user_facts =
                djmeta::online::is_beatport_user_facts_clipboard(text);
            const bool beatport_user_release =
                djmeta::online::is_beatport_user_release_clipboard(text);
            const bool beatport_public_page =
                djmeta::online::is_beatport_public_page_clipboard(text);
            const bool crates_visible_track =
                djmeta::online::is_crates_public_track_clipboard(text);
            const bool crates_visible_release =
                djmeta::online::is_crates_public_release_clipboard(text);
            const bool beatport_user_track=
                beatport_user_json || beatport_user_facts || beatport_public_page;
            // Clipboard intake is explicitly user-triggered and self-describing.
            // Selecting Beatport for SEARCH must not block a qualified manual
            // @provider candidate already copied by the user. Parsing and all
            // physical/CUE snapshot, provenance and REVIEW guards still apply.
            const bool manual_declared_candidate=text.starts_with("@provider=");
            if(state->beatport_offline_selected &&
               !beatport_user_release && !beatport_user_track &&
               !crates_visible_track && !crates_visible_release &&
               !manual_declared_candidate)
                throw std::invalid_argument(
                     "Beatport + Crates mode requires @beatport-track-json, "
                    "@beatport-track-facts, @beatport-public-jsonld, "
                    "@crates-public-track, @crates-public-release, or both "
                    "@beatport-release-json and @beatport-tracks-json, or a "
                    "structured @provider manual candidate. Plain webpage text "
                    "cannot be treated as authorized API evidence.");
            std::optional<djmeta::online::ManualCandidate> user_candidate;
            std::optional<djmeta::online::ReleaseEdition> user_edition;
            if(beatport_user_release)
                user_edition=djmeta::online::parse_beatport_user_release_clipboard(text);
            else if(crates_visible_release)
                user_edition=djmeta::online::parse_crates_public_release_clipboard(text);
            else if(!beatport_user_track && !crates_visible_track)
                user_candidate=djmeta::online::parse_manual_candidate(text);
            std::vector<djmeta::online::FieldReviewRow> rows;
            if (compare_cue) {
                if(crates_visible_release)
                    throw std::invalid_argument(
                        "Crates visible edition has no independently qualified "
                        "CUE-to-release mapping. Compare selected physical tracks.");
                if(beatport_user_track || crates_visible_track)
                    throw std::invalid_argument(
                        "Single-track source import has no qualified CUE ordinal. "
                        "Use structured @cue_track_ordinal evidence instead.");
                // Dedicated CUE reader: exact external bytes or a qualified
                // physical subsong-0 CUESHEET. A generic virtual file_info
                // must never become the source or target of a CUE write.
                const auto raw =
                    read_cue_raw_on_demand(entry.handle, entry.input.source_path);
                const auto cue = djmeta::inspect_cue_metadata(raw.raw_text, raw.carrier);
                if(beatport_user_release)
                    rows=djmeta::online::review_beatport_user_edition_cue(
                        cue,*user_edition);
                else {
                    rows = djmeta::online::review_manual_cue_candidate(
                        cue, *user_candidate);
                    djmeta::online::append_manual_cue_comparison_source(
                        rows,cue,user_candidate->provider);
                }
                if(read_cue_raw_on_demand(
                    entry.handle,entry.input.source_path).raw_text!=raw.raw_text)
                    throw std::runtime_error("CUE changed during clipboard comparison.");
            } else {
                if (!entry.input.physical_source_qualified ||
                    is_external_cue_locator(entry.input.source_path))
                    throw std::invalid_argument(
                        "Select a qualified physical audio file, or choose "
                        "Inspect CUE first to compare a CUE.");
                if (user_candidate && user_candidate->cue_track_ordinal)
                    throw std::invalid_argument(
                        "@cue_track_ordinal applies only to CUE comparison.");
                const auto ref = entry.handle->get_info_ref();
                const auto original = metadata_from_file_info(ref->info());
                if(beatport_user_release || crates_visible_release) {
                    const double seconds=ref->info().get_length();
                    const std::int64_t duration_ms=
                        seconds>0.0 && seconds<=36000.0
                        ? static_cast<std::int64_t>(seconds*1000.0+0.5)
                        : -1;
                    rows=crates_visible_release
                        ? djmeta::online::review_crates_public_release_physical(
                            original,duration_ms,*user_edition)
                        : djmeta::online::review_beatport_user_edition_physical(
                            original,duration_ms,*user_edition);
                } else if(crates_visible_track) {
                    const auto crates_track =
                        djmeta::online::parse_crates_public_track_clipboard(text);
                    const double seconds=ref->info().get_length();
                    const std::int64_t duration_ms=seconds>0.0 && seconds<=36000.0
                        ? static_cast<std::int64_t>(seconds*1000.0+0.5):-1;
                    rows=djmeta::online::review_crates_public_track(
                        original,duration_ms,crates_track).fields;
                } else if(beatport_user_track) {
                    const auto beatport_track = beatport_user_json
                        ? djmeta::online::parse_beatport_user_json_clipboard(text)
                        : beatport_user_facts
                            ? djmeta::online::parse_beatport_user_facts_clipboard(text)
                            : djmeta::online::parse_beatport_public_jsonld_clipboard(text);
                    const double seconds=ref->info().get_length();
                    std::int64_t duration_ms=-1;
                    if(seconds>0.0 && seconds<=36000.0)
                        duration_ms=static_cast<std::int64_t>(
                            seconds*1000.0+0.5);
                    rows=djmeta::online::review_beatport_user_track(
                        original,duration_ms,beatport_track).fields;
                } else {
                    rows = djmeta::online::review_online_fields(
                        original, user_candidate->fields);
                    djmeta::online::append_pasted_physical_comparison_rows(
                        rows,original,user_candidate->provider);
                }
            }
            verify_snapshot(entry);
            verify_rules_snapshot(state->captured_rules);
            const bool pasted_physical=user_candidate.has_value() && !compare_cue;
            const bool pasted_cue=user_candidate.has_value() && compare_cue;
            if(beatport_user_release || crates_visible_release ||
               beatport_user_track || crates_visible_track) {
                if(beatport_user_track || crates_visible_track) {
                    // One recording is ONE browser item; all its field
                    // comparisons belong to the independent details pane.
                    // The complete original physical tag inventory stays in
                    // a separate source group, not a flattened edit list.
                    for(auto& row:rows) {
                        if(djmeta::online::is_local_physical_original(row))
                            row.field="Physical / "+row.field;
                        else row.field=(crates_visible_track
                            ? "Crates Track / " : "Beatport Track / ")+row.field;
                    }
                }
                auto grouped=group_musicbrainz_release_rows(rows);
                state->candidate_rows=std::move(grouped.summary);
                state->musicbrainz_detail_groups=std::move(grouped.detail);
            } else if(pasted_physical || pasted_cue) {
                // The online static audit requires the existing Beatport
                // branch to stay explicit. Pasted values still use the same
                // two-level review, with independent unverified provenance.
                auto grouped=group_musicbrainz_release_rows(rows);
                state->candidate_rows=std::move(grouped.summary);
                state->musicbrainz_detail_groups=std::move(grouped.detail);
            } else {
                state->candidate_rows=std::move(rows);
                state->musicbrainz_detail_groups.clear();
            }
            state->candidate_source_index = state->selected_track_index;
            state->cue_inspection_mode = false;
            state->cue_candidate_comparison_mode = compare_cue;
            state->musicbrainz_live_view =
                beatport_user_release || crates_visible_release ||
                beatport_user_track || crates_visible_track;
            if(pasted_physical || pasted_cue) state->musicbrainz_live_view = true;
            state->musicbrainz_release_loaded =
                beatport_user_release || crates_visible_release ||
                beatport_user_track || crates_visible_track;
            state->musicbrainz_kind = pasted_physical
                ? djmeta::online::musicbrainz::SearchKind::Recording
                : djmeta::online::musicbrainz::SearchKind::Release;
            state->search_attempt_notice = beatport_user_release
                ? L"Beatport edition imported from user-provided JSON. "
                  L"Complete tracklist compared. Source is UNVERIFIED; "
                  L"no official API authentication or writes."
                : crates_visible_release
                ? L"Crates full visible release imported with catalog and "
                  L"tracklist. Exact physical identity checked, source "
                  L"UNVERIFIED, Date omitted. Preview only; no file writes."
                : crates_visible_track
                ? L"Crates visible track imported from a user-selected public page. "
                  L"Version/artist/duration checked, source UNVERIFIED. "
                  L"Preview only; no file writes." : beatport_user_track
                ? L"Beatport track imported from user-provided evidence. "
                  L"Mix/version, ISRC and duration checked. Source is "
                  L"UNVERIFIED; preview only, no writes."
                : pasted_physical
                ? L"Structured provider values pasted by the user are "
                  L"UNVERIFIED. Select a group to compare every field "
                  L"against all original physical tags; no file writes."
                : pasted_cue
                ? L"Structured CUE values pasted by the user are UNVERIFIED. "
                  L"Select Album, Track or CUE Original groups to inspect "
                  L"all parsed original CUE tags and FILE references. No writes."
                : L"";
            state->discogs_live_view = false;
            state->discogs_results.clear();
            state->discogs_master_id=0;
            state->discogs_master_page=state->discogs_master_pages=0;
            state->discogs_direct_edition.reset();
            state->discogs_search_was_cue = false;
            state->musicbrainz_results.clear();
            update_metadata_table(*state);
            return TRUE;
        }
        // Candidate comparisons are evidence only, not normalization decisions.
        if (state->show_candidate && (native_command == PreviewCommand::Accept ||
            native_command == PreviewCommand::Reject ||
            native_command == PreviewCommand::Reset ||
            native_command == PreviewCommand::ManualValue)) return TRUE;
        if (native_command == PreviewCommand::VisibleWhitespaceChanged) {
            state->show_whitespace = SendDlgItemMessageW(
                dialog, IDC_METADATA_VISIBLE_WHITESPACE,
                BM_GETCHECK, 0, 0) == BST_CHECKED;
            InvalidateRect(state->metadata_list, nullptr, FALSE);
            return TRUE;
        }
        if (native_command == PreviewCommand::Accept ||
            native_command == PreviewCommand::Reject ||
            native_command == PreviewCommand::Reset ||
            native_command == PreviewCommand::ManualValue) {
            const auto action = native_command == PreviewCommand::Accept
                ? djmeta::ReviewAction::Accept
                : native_command == PreviewCommand::Reject
                ? djmeta::ReviewAction::Reject
                : native_command == PreviewCommand::Reset
                ? djmeta::ReviewAction::Pending
                : djmeta::ReviewAction::ManualValue;
            apply_review_action(dialog, *state, action);
            return TRUE;
        }
        if(id==IDC_BATCH_HISTORY_FILTER && HIWORD(wp)==CBN_SELCHANGE) {
            const auto selected=SendDlgItemMessageW(
                dialog,IDC_BATCH_HISTORY_FILTER,CB_GETCURSEL,0,0);
            if(selected<0 || selected>3)
                throw std::invalid_argument("Unknown history filter.");
            state->history_filter=static_cast<PreviewHistoryFilter>(selected);
            refresh_preview_history_list(*state);
            return TRUE;
        }
        if(id==IDC_BATCH_LOCATION_FILTER && HIWORD(wp)==CBN_SELCHANGE) {
            const auto selected=SendDlgItemMessageW(
                dialog,IDC_BATCH_LOCATION_FILTER,CB_GETCURSEL,0,0);
            if(selected<0 ||
               selected>static_cast<int>(djmeta::BatchLocationQueue::CueOriginalKept))
                throw std::invalid_argument("Unknown File locations filter.");
            state->location_queue=static_cast<djmeta::BatchLocationQueue>(selected);
            update_table(*state);
            return TRUE;
        }
        if (native_command == PreviewCommand::TrackFilterChanged) {
            const auto choice = SendDlgItemMessageW(
                dialog, IDC_METADATA_TRACK_FILTER, CB_GETCURSEL, 0, 0);
            switch(choice) {
                case 0: state->track_discovery=djmeta::TrackDiscovery::All; break;
                case 1: state->track_discovery=djmeta::TrackDiscovery::Changed; break;
                case 2: state->track_discovery=djmeta::TrackDiscovery::NeedsReview; break;
                case 3: state->track_discovery=djmeta::TrackDiscovery::PendingAny; break;
                case 4: state->track_discovery=djmeta::TrackDiscovery::StagedEdits; break;
                case 5: state->track_discovery=djmeta::TrackDiscovery::Reviewed; break;
                case 6: state->track_discovery=djmeta::TrackDiscovery::Unchanged; break;
                default: throw std::invalid_argument("Unknown track filter selection.");
            }
            update_master_table(*state);
            update_metadata_table(*state);
            return TRUE;
        }
        if (native_command == PreviewCommand::FocusFilterChanged) {
            const auto choice = SendDlgItemMessageW(
                dialog, IDC_METADATA_FILTER, CB_GETCURSEL, 0, 0);
            state->existing_tags_view = choice == 3;
            state->metadata_focus = choice == 1 ? djmeta::MetadataFocus::Extended :
                choice == 2 ? djmeta::MetadataFocus::All :
                djmeta::MetadataFocus::Music;
            show_preview_page(dialog,*state,true,false);
            return TRUE;
        }
        if (id == 41300u) {
            inspect_selected_raw_targets(dialog, *state);
            return TRUE;
        }
        if(id==41301u) {
            // File locations -> inspected CUE carrier, the inverse of the
            // existing CUE FILE -> physical audio drilldown. No HTTP or
            // FileOps; only exact captured source IDs and fresh host evidence.
            if(state->stale || state->refreshing ||
               TabCtrl_GetCurSel(state->tabs)!=2 ||
               ListView_GetSelectedCount(state->list)!=1)
                throw std::invalid_argument(
                    "Select exactly one fresh physical File locations row.");
            const int selected=ListView_GetNextItem(
                state->list,-1,LVNI_SELECTED);
            if(selected<0 ||
               static_cast<std::size_t>(selected)>=state->view_order.size())
                throw std::invalid_argument("Selected physical row expired.");
            const auto audio_index=state->view_order[
                static_cast<std::size_t>(selected)];
            const auto links=related_cues_for_selected_file(*state);
            if(links.size()!=1)
                throw std::invalid_argument(
                    "Inspect CUE FILE sources first; multiple or missing "
                    "qualified CUE carriers cannot be selected automatically.");
            const auto cue_index=links.front().carrier_source_row;
            const auto& audio=state->entries.at(audio_index);
            const auto& cue=state->entries.at(cue_index);
            if(!is_external_cue_locator(cue.input.source_path))
                throw std::runtime_error("Related CUE carrier identity changed.");
            if(!confirm_online_preview_navigation(dialog,*state,cue_index))
                return TRUE;
            verify_snapshot(audio);
            verify_snapshot(cue);
            verify_rules_snapshot(state->captured_rules);
            revalidate_workspace(*state,true);
            if(state->stale)
                throw std::runtime_error("Related CUE source changed. Refresh.");
            const auto checked=related_cues_for_selected_file(*state);
            if(checked.size()!=1 ||
               checked.front().carrier_source_row!=cue_index ||
               checked.front().referenced_files!=links.front().referenced_files)
                throw std::runtime_error(
                    "CUE FILE relationship changed during navigation. Reinspect.");
            state->cue_return.reset();
            state->track_discovery=djmeta::TrackDiscovery::All;
            state->metadata_focus=djmeta::MetadataFocus::All;
            SendDlgItemMessageW(dialog,IDC_METADATA_TRACK_FILTER,CB_SETCURSEL,0,0);
            SendDlgItemMessageW(dialog,IDC_METADATA_FILTER,CB_SETCURSEL,2,0);
            state->selected_track_index=cue_index;
            TabCtrl_SetCurSel(state->tabs,1);
            show_preview_page(dialog,*state,true,true);
            update_master_table(*state);
            const auto it=std::find(state->track_view_order.begin(),
                                    state->track_view_order.end(),cue_index);
            if(it==state->track_view_order.end())
                throw std::runtime_error("Related CUE no longer visible.");
            const auto row=static_cast<int>(it-state->track_view_order.begin());
            state->updating_track_selection=true;
            ListView_SetItemState(state->metadata_track_list,-1,0,LVIS_SELECTED);
            ListView_SetItemState(state->metadata_track_list,row,
                                  LVIS_SELECTED|LVIS_FOCUSED,
                                  LVIS_SELECTED|LVIS_FOCUSED);
            state->updating_track_selection=false;
            ListView_EnsureVisible(state->metadata_track_list,row,FALSE);
            if(!IsWindowEnabled(GetDlgItem(dialog,IDC_METADATA_INSPECT_CUE)))
                throw std::runtime_error("Related CUE is not inspectable.");
            SendMessageW(dialog,WM_COMMAND,
                MAKEWPARAM(IDC_METADATA_INSPECT_CUE,BN_CLICKED),
                reinterpret_cast<LPARAM>(GetDlgItem(dialog,IDC_METADATA_INSPECT_CUE)));
            SetFocus(state->metadata_list);
            return TRUE;
        }
        if(id==IDC_BATCH_PROFILE_SAVE && HIWORD(wp)==BN_CLICKED) {
            save_route_template(dialog);
            return TRUE;
        }
        if(id==IDC_BATCH_PROFILE_DELETE && HIWORD(wp)==BN_CLICKED) {
            delete_route_template(dialog);
            return TRUE;
        }
        if(id==IDC_BATCH_PROFILE_COPY && HIWORD(wp)==BN_CLICKED) {
            copy_named_route_templates(dialog);
            return TRUE;
        }
        if(id==IDC_BATCH_PROFILE_PASTE && HIWORD(wp)==BN_CLICKED) {
            paste_named_route_templates(dialog);
            return TRUE;
        }
        if (id == IDC_BATCH_PROFILE_PICKER && HIWORD(wp) == CBN_SELCHANGE) {
            const auto sel = SendDlgItemMessageW(
                dialog, IDC_BATCH_PROFILE_PICKER, CB_GETCURSEL, 0, 0);
            load_profile(dialog, static_cast<int>(sel));
            return TRUE;
        }
#if defined(MMS_ENABLE_PHYSICAL_WRITES)
        if(id==IDC_ONLINE_WRITE_SELECTED && HIWORD(wp)==BN_CLICKED &&
           qualified_cue_write_preview(*state)) {
            // A separate external CUE byte transaction. Never route this
            // through the SDK physical tag writer or any virtual subsong.
            const auto& entry=state->entries[state->selected_track_index];
            verify_snapshot(entry);
            verify_rules_snapshot(state->captured_rules);
            const auto edition=*state->cue_postimage_snapshot->verified_edition;
            const auto selected=state->cue_value_preview_indices;
            const auto reviewed=prepare_external_cue_host_review(
                entry.handle,entry.input.source_path,edition,selected,
                *state->cue_value_preview);
            const auto confirmation=exact_cue_write_confirmation(
                *state->cue_postimage_snapshot,reviewed);
            const auto confirmation_version=state->online_context_version.current();
            const auto confirmation_workspace=state->workspace_generation;
            const auto confirmation_track=state->selected_track_index;
            const auto handle=entry.handle;
            if(MessageBoxW(dialog,confirmation.c_str(),
                           L"Confirm exact external CUE changes",
                           MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)
                return TRUE;
            // The confirmation dialog pumps messages; its approval cannot
            // survive a changed workspace, source, edition or field selection.
            if(!IsWindow(dialog) || g_workspace!=dialog ||
               g_workspace_generation!=confirmation_workspace ||
               GetWindowLongPtrW(dialog,GWLP_USERDATA)!=
                   reinterpret_cast<LONG_PTR>(state))
                return TRUE;
            if(state->stale ||
               state->selected_track_index!=confirmation_track ||
               state->online_context_version.current()!=confirmation_version ||
               !qualified_cue_write_preview(*state) ||
               state->cue_value_preview_indices!=selected ||
               !state->cue_value_preview ||
               state->cue_value_preview->bytes!=reviewed.postimage.bytes ||
               !state->cue_postimage_snapshot ||
               state->cue_postimage_snapshot->original_bytes!=reviewed.original_bytes)
                throw std::runtime_error(
                    "CUE review changed during confirmation. Refresh the preview.");
            verify_snapshot(state->entries[confirmation_track]);
            verify_rules_snapshot(state->captured_rules);
            if(state->physical_write_pending->exchange(true))
                throw std::runtime_error("A file write is already pending.");
            SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                L"Checking exact CUE and audio sources, then committing selected bytes...");
            djmeta::online::ExternalCueCommitResult report;
            try {
                report=commit_confirmed_external_cue_review(
                    reviewed,handle,edition);
            } catch(...) {
                state->physical_write_pending->store(false);
                state->stale=true; // a failed post-replacement read is uncertain
                update_online_field_preview_controls(*state);
                throw;
            }
            state->physical_write_pending->store(false);
            state->stale=true; // never re-use consent after any file transaction
            update_online_field_preview_controls(*state);
            std::wstring result;
            switch(report.status) {
            case djmeta::online::ExternalCueCommitStatus::Committed:
                result=L"External CUE saved; exact bytes verified. ";break;
            case djmeta::online::ExternalCueCommitStatus::ExactNoop:
                result=L"External CUE unchanged; zero file writes. ";break;
            case djmeta::online::ExternalCueCommitStatus::BackupRetained:
                result=L"CUE result requires inspection; backup retained. ";break;
            default:
                result=L"External CUE write refused; original retained. ";break;
            }
            result+=from_utf8(readable_track_name(reviewed.source_path));
            SetDlgItemTextW(dialog,IDC_BATCH_HINT,result.c_str());
            if(report.status==djmeta::online::ExternalCueCommitStatus::Refused ||
               report.status==djmeta::online::ExternalCueCommitStatus::BackupRetained) {
                auto detail=result+L"\n\n"+from_utf8(report.reason);
                if(!report.retained_backup.empty())
                    detail+=L"\nBackup: "+report.retained_backup;
                MessageBoxW(dialog,detail.c_str(),L"External CUE write result",
                            MB_OK|MB_ICONWARNING);
            }
            return TRUE;
        }
        if(id==IDC_ONLINE_WRITE_SELECTED && HIWORD(wp)==BN_CLICKED) {
            // No automatic commit when loading/searching/selecting a provider.
            // One explicit user click, one physical source, one final YES.
            if(!qualified_online_field_preview(*state) ||
               !state->online_field_preview ||
               state->online_field_preview->explicitly_changed==0 ||
               state->online_included_fields.empty())
                throw std::invalid_argument("No reviewed physical tag changes to write.");
            const auto& local_decisions=
                state->review_decisions.at(state->selected_track_index);
            if(std::any_of(local_decisions.begin(),local_decisions.end(),
                   [](const djmeta::ReviewDecision& decision) {
                       return decision.action==djmeta::ReviewAction::Accept ||
                              decision.action==djmeta::ReviewAction::ManualValue;
                   }))
                throw std::invalid_argument(
                    "QA online-only writer cannot ignore accepted local normalization. "
                    "Reset those decisions or use a future unified writer.");
            const auto& entry=state->entries[state->selected_track_index];
            verify_snapshot(entry);
            verify_rules_snapshot(state->captured_rules);
            const auto observation=probe_host_file_readonly(entry.input.source_path);
            if(observation.state!=HostFileState::ExistingFile ||
               observation.host_physical_key!=entry.observed_physical_key ||
               observation.source_guard!=entry.observed_source_guard)
                throw std::runtime_error("Physical source changed before final consent.");
            const auto latest=entry.handle->get_info_ref();
            const auto tags=metadata_from_file_info(latest->info());
            const djmeta::PhysicalTagSourceSnapshot source{
                true,false,observation.host_physical_key,
                observation.source_guard,state->online_field_snapshot->physical_before};
            const djmeta::online::FrozenPhysicalTagWrite frozen{
                entry.input.source_path,source,*state->online_field_snapshot,
                *state->online_field_preview,state->online_included_fields,
                state->online_date_targets};
            const auto verified=djmeta::online::verify_frozen_physical_write(
                frozen,tags,entry.input.source_path,observation.host_physical_key,
                observation.source_guard);
            if(verified.state!=djmeta::online::PhysicalWriteGateState::Changed)
                throw std::invalid_argument(
                    "Changes are stale, unqualified or already applied. Refresh.");
            const auto exact_changes=djmeta::physical_sdk_changed_field_approvals(
                frozen.physical.original,verified.postimage.planned);
            if(exact_changes.size()!=verified.postimage.explicitly_changed)
                throw std::runtime_error(
                    "Final physical field diff disagrees with frozen approval.");
            const auto confirmation_version=state->online_context_version.current();
            const auto confirmation_workspace=state->workspace_generation;
            const auto confirmation_track=state->selected_track_index;
            // Refuse rather than truncate a large multivalue confirmation.
            // Every old/new value is visible before the one-file YES action.
            const auto warning=from_utf8(
                djmeta::format_physical_write_confirmation(
                    frozen.source_path,exact_changes));
            if(MessageBoxW(dialog,warning.c_str(),
                           L"Confirm exact physical tag changes",
                           MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)
                return TRUE;
            // The modal message pump can process a pending network/UI event.
            // Revalidate the precise reviewed evidence after the human YES.
            if(!IsWindow(dialog) || g_workspace!=dialog ||
               g_workspace_generation!=confirmation_workspace ||
               GetWindowLongPtrW(dialog,GWLP_USERDATA)!=
                   reinterpret_cast<LONG_PTR>(state))
                return TRUE;
            if(state->stale ||
               state->selected_track_index!=confirmation_track ||
               state->online_context_version.current()!=confirmation_version ||
               !qualified_online_field_preview(*state) ||
               !state->online_field_preview || !state->online_field_snapshot ||
               state->online_included_fields!=frozen.selected ||
               state->online_date_targets!=frozen.date_targets ||
               state->online_field_preview->planned!=frozen.reviewed.planned ||
               state->online_field_snapshot->physical_before!=
                   frozen.official.physical_before)
                throw std::runtime_error(
                    "Reviewed selection changed during confirmation. Refresh.");
            verify_snapshot(entry);
            verify_rules_snapshot(state->captured_rules);
            const auto source_name=from_utf8(
                readable_track_name(frozen.source_path));
            if(state->physical_write_pending->exchange(true))
                throw std::runtime_error("A physical write is already pending.");
            // Callback owns no PreviewState; its result can outlive the HWND.
            // Prevent duplicate submissions and refresh during asynchronous IO.
            const auto pending=state->physical_write_pending;
            const auto generation=state->workspace_generation;
            state->stale=true;
            update_live_provider_controls(*state);
            SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                L"Submitting one physical tag write; waiting for SDK verification...");
            try {
                const auto result=submit_frozen_physical_tag_write(
                    entry.handle,frozen,
                    [dialog,pending,generation,source_name](PhysicalWriteReport report){
                        pending->store(false);
                        // HWND values can be reused by a later workspace.
                        // Never deliver an old operation result to a new one.
                        if(!IsWindow(dialog) || g_workspace!=dialog ||
                           g_workspace_generation!=generation)return;
                        std::wstring status;
                        switch(report.outcome) {
                        case PhysicalWriteOutcome::Completed:
                            status=L"Physical tags saved and verified: ";break;
                        case PhysicalWriteOutcome::Noop:
                            status=L"Exactly unchanged; no file write: ";break;
                        case PhysicalWriteOutcome::Cancelled:
                            status=L"SDK cancelled; verify file before retry: ";break;
                        case PhysicalWriteOutcome::ReadbackMismatch:
                            status=L"Write outcome uncertain; inspect file: ";break;
                        default:
                            status=L"Physical tag write refused or failed: ";break;
                        }
                        const auto label=status+source_name;
                        SetDlgItemTextW(dialog,IDC_BATCH_HINT,label.c_str());
                        if(report.outcome!=PhysicalWriteOutcome::Completed &&
                           report.outcome!=PhysicalWriteOutcome::Noop) {
                            const auto detail=label+L"\n\n"+
                                from_utf8(report.diagnostic);
                            MessageBoxW(dialog,detail.c_str(),
                                L"Physical tag result",MB_OK|MB_ICONWARNING);
                        }
                    });
                if(result.outcome==PhysicalWriteOutcome::Noop)
                    pending->store(false);
                if(result.outcome!=PhysicalWriteOutcome::Submitted &&
                   result.outcome!=PhysicalWriteOutcome::Noop)
                    throw std::runtime_error(result.diagnostic);
                if(result.outcome==PhysicalWriteOutcome::Noop)
                    SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                        L"Exactly unchanged; no physical file write was needed.");
            } catch(...) {
                pending->store(false);
                throw;
            }
            // This frozen approval can never be reused for a second write.
            return TRUE;
        }
#endif
        if (id == IDC_BATCH_UNDO && HIWORD(wp) == BN_CLICKED) {
            undo_last_preview_action(dialog,*state);
            return TRUE;
        }
        if (id == IDC_BATCH_NEXT_ISSUE && HIWORD(wp) == BN_CLICKED) {
            // A hidden source re-probe may invalidate the entire queue;
            // keep native WM_COMMAND exceptions inside this boundary.
            try {
                navigate_next_workflow_issue(dialog,*state);
            } catch (...) {
                mark_stale_workspace(*state);
                SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                    L"Next issue aborted: stale source or route. "
                    L"Refresh; no file operation was approved.");
            }
            return TRUE;
        }
        if (id == IDC_BATCH_AUDIT && HIWORD(wp) == BN_CLICKED) {
            // The generic command gate has already re-probed the FULL batch.
            // A stale source aborts without presenting an outdated audit.
            show_workflow_audit(dialog,*state);
            return TRUE;
        }
        if (id == IDC_BATCH_COPY_AUDIT && HIWORD(wp) == BN_CLICKED) {
            // Same full-batch guard as Audit. No implicit clipboard access.
            copy_workflow_audit(dialog,*state);
            return TRUE;
        }
        if (id == IDC_BATCH_COPY_ISSUES && HIWORD(wp) == BN_CLICKED) {
            // Filtered report only; global source and route checks are intact.
            copy_workflow_audit(dialog,*state,true);
            return TRUE;
        }
        if(id==IDC_BATCH_NEXT_CUE && HIWORD(wp)==BN_CLICKED) {
            if(state->stale || state->refreshing ||
               TabCtrl_GetCurSel(state->tabs)!=2)
                throw std::invalid_argument("Next CUE requires fresh File locations.");
            const auto carrier_ids=captured_cue_carriers(*state);
            const auto next=state->cue_inspections.next_attention(
                carrier_ids,state->next_cue_cursor);
            if(next>=state->entries.size()) {
                SetDlgItemTextW(dialog,IDC_BATCH_HINT,
                    L"No remaining uninspected, stale or blocked CUE source. "
                    L"Final foobar FileOps/CUE permission remains unavailable.");
                return TRUE;
            }
            // Preserve uncommitted online comparison choices for their
            // original source, just like the existing Next issue action.
            if(!confirm_online_preview_navigation(dialog,*state,next))
                return TRUE;
            verify_snapshot(state->entries[next]);
            verify_rules_snapshot(state->captured_rules);
            revalidate_workspace(*state,true);
            if(state->stale)
                throw std::runtime_error("CUE queue changed. Refresh.");
            if(next!=state->selected_track_index)
                invalidate_online_request(*state);
            state->next_cue_cursor=next;
            state->track_discovery=djmeta::TrackDiscovery::All;
            state->metadata_focus=djmeta::MetadataFocus::All;
            SendDlgItemMessageW(dialog,IDC_METADATA_TRACK_FILTER,CB_SETCURSEL,0,0);
            SendDlgItemMessageW(dialog,IDC_METADATA_FILTER,CB_SETCURSEL,2,0);
            state->selected_track_index=next;
            TabCtrl_SetCurSel(state->tabs,1);
            show_preview_page(dialog,*state,true,true);
            update_master_table(*state);
            const auto it=std::find(state->track_view_order.begin(),
                                    state->track_view_order.end(),next);
            if(it==state->track_view_order.end())
                throw std::runtime_error("CUE source not visible in full Find queue.");
            const auto row=static_cast<int>(it-state->track_view_order.begin());
            state->updating_track_selection=true;
            ListView_SetItemState(state->metadata_track_list,-1,0,LVIS_SELECTED);
            ListView_SetItemState(state->metadata_track_list,row,
                                  LVIS_SELECTED|LVIS_FOCUSED,
                                  LVIS_SELECTED|LVIS_FOCUSED);
            state->updating_track_selection=false;
            ListView_EnsureVisible(state->metadata_track_list,row,FALSE);
            SendMessageW(dialog,WM_COMMAND,
                MAKEWPARAM(IDC_METADATA_INSPECT_CUE,BN_CLICKED),
                reinterpret_cast<LPARAM>(GetDlgItem(dialog,IDC_METADATA_INSPECT_CUE)));
            SetFocus(state->metadata_list);
            return TRUE;
        }
        if(id==IDC_BATCH_BACK_TO_CUE && HIWORD(wp)==BN_CLICKED) {
            if(state->stale || state->refreshing || !state->cue_return ||
               TabCtrl_GetCurSel(state->tabs)!=2)
                throw std::invalid_argument(
                    "Back to CUE requires an intact File locations drilldown.");
            const auto saved=*state->cue_return;
            if(saved.source_index>=state->entries.size())
                throw std::runtime_error("CUE source identity changed. Refresh.");
            const auto& carrier=state->entries.at(saved.source_index);
            if(!is_external_cue_locator(carrier.input.source_path) ||
               carrier.observed_physical_key!=saved.physical_key ||
               carrier.observed_source_guard!=saved.source_guard)
                throw std::runtime_error("CUE carrier version changed. Refresh.");
            if(!confirm_online_preview_navigation(
                   dialog,*state,saved.source_index)) return TRUE;
            verify_snapshot(carrier);
            verify_rules_snapshot(state->captured_rules);
            revalidate_workspace(*state,true);
            if(state->stale)
                throw std::runtime_error("CUE source changed. Refresh.");
            // Discard the old displayed FILE evidence by invoking Inspect
            // CUE again on the exact original carrier; no network/writes.
            state->track_discovery=djmeta::TrackDiscovery::All;
            state->metadata_focus=djmeta::MetadataFocus::All;
            SendDlgItemMessageW(dialog,IDC_METADATA_TRACK_FILTER,CB_SETCURSEL,0,0);
            SendDlgItemMessageW(dialog,IDC_METADATA_FILTER,CB_SETCURSEL,2,0);
            state->selected_track_index=saved.source_index;
            TabCtrl_SetCurSel(state->tabs,1);
            show_preview_page(dialog,*state,true,true);
            update_master_table(*state);
            const auto it=std::find(state->track_view_order.begin(),
                                    state->track_view_order.end(),saved.source_index);
            if(it==state->track_view_order.end())
                throw std::runtime_error("CUE carrier no longer selectable.");
            const auto row=static_cast<int>(it-state->track_view_order.begin());
            state->updating_track_selection=true;
            ListView_SetItemState(state->metadata_track_list,-1,0,LVIS_SELECTED);
            ListView_SetItemState(state->metadata_track_list,row,
                                  LVIS_SELECTED|LVIS_FOCUSED,
                                  LVIS_SELECTED|LVIS_FOCUSED);
            state->updating_track_selection=false;
            ListView_EnsureVisible(state->metadata_track_list,row,FALSE);
            if(!IsWindowEnabled(GetDlgItem(dialog,IDC_METADATA_INSPECT_CUE)))
                throw std::runtime_error("CUE source is no longer inspectable. Refresh.");
            SendMessageW(dialog,WM_COMMAND,
                MAKEWPARAM(IDC_METADATA_INSPECT_CUE,BN_CLICKED),
                reinterpret_cast<LPARAM>(GetDlgItem(dialog,IDC_METADATA_INSPECT_CUE)));
            SetFocus(state->metadata_list);
            return TRUE;
        }
        if (id == IDC_BATCH_APPLY_SELECTED && HIWORD(wp) == BN_CLICKED) {
            apply_to_rows(dialog, *state, NativeRoutePreviewScope::Selected);
            return TRUE;
        }
        if (id == IDC_BATCH_APPLY_VISIBLE && HIWORD(wp) == BN_CLICKED) {
            apply_to_rows(dialog, *state, NativeRoutePreviewScope::Visible);
            return TRUE;
        }
        if (id == IDC_BATCH_APPLY_ALL && HIWORD(wp) == BN_CLICKED) {
            apply_to_rows(dialog, *state, NativeRoutePreviewScope::All);
            return TRUE;
        }
    } catch (const std::exception& error) {
        const std::wstring message_text = from_utf8(error.what());
        MessageBoxW(dialog, message_text.c_str(), L"Batch Preview",
                    MB_OK | MB_ICONWARNING);
        return TRUE;
    }
    return FALSE;
}

class workspace_shutdown : public initquit {
public:
    void on_init() override {}
    void on_quit() override {
        // No windows, timers or callback targets survive component shutdown.
        if (g_workspace && IsWindow(g_workspace)) DestroyWindow(g_workspace);
    }
};
initquit_factory_t<workspace_shutdown> g_workspace_shutdown;

} // namespace

void show_batch_preview_dialog(
    const metadb_handle_list& handles,
    const RoutePreviewChoice& initial_choice,
    bool explicit_route_request) {
    try {
        if (handles.get_count() > static_cast<t_size>((std::numeric_limits<int>::max)()))
            throw std::runtime_error("Too many tracks for the batch preview table.");

        if (g_workspace && IsWindow(g_workspace)) {
            auto* existing = reinterpret_cast<PreviewState*>(
                GetWindowLongPtrW(g_workspace,GWLP_USERDATA));
            const bool same = existing && same_workspace_selection(*existing,handles);
            // Only a deliberate preset request may warn about a different
            // route. Normalize/Prepare reopening uses saved defaults but must
            // not misrepresent them as a new user-selected preset or replace
            // in-progress accepted/manual decisions.
            const bool different_route = explicit_route_request && existing &&
                (existing->current_choice.display_name != initial_choice.display_name ||
                 existing->current_choice.destination_root != initial_choice.destination_root ||
                 existing->current_choice.titleformat_expression !=
                     initial_choice.titleformat_expression);
            if (existing) existing->online_close_requested=false;
            if (IsIconic(g_workspace)) ShowWindow(g_workspace,SW_RESTORE);
            ShowWindow(g_workspace,SW_SHOW);
            SetForegroundWindow(g_workspace);
            if (existing && !same) {
                SetDlgItemTextW(g_workspace,IDC_BATCH_HINT,
                    L"Different foobar selection: existing snapshot and decisions retained. "
                    L"Close this workspace, then reopen for the new selection.");
            } else if (existing) {
                revalidate_workspace(*existing);
                if (different_route && !existing->stale)
                    SetDlgItemTextW(g_workspace,IDC_BATCH_HINT,
                        L"Another route preset was requested. Existing route "
                        L"and review decisions are preserved. Choose a profile "
                        L"in File locations, or close and reopen for that preset.");
            }
            return;
        }

        auto state=std::make_unique<PreviewState>();
        state->current_choice = initial_choice;
        state->entries = capture_preview(
            handles, initial_choice, state->analyses, state->captured_rules);
        state->metadata_rows = djmeta::describe_metadata_diffs(state->analyses);
        state->metadata_row_index = djmeta::index_track_diffs(
            state->metadata_rows,state->entries.size());
        for (const auto& analysis : state->analyses)
            state->review_decisions.emplace_back(analysis.proposals.size());
        refresh_review_summaries(*state);
        for (const auto& item : state->entries) {
            state->source_labels.push_back(item.input.source_path);
            const auto name=readable_track_name(item.input.source_path);
            state->cached_track_sort_labels.push_back(name);
            state->cached_track_names.push_back(from_utf8(name));
        }
        INITCOMMONCONTROLSEX controls = {};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES;
        if (!InitCommonControlsEx(&controls))
            throw std::runtime_error("Unable to initialize the native list view.");

        // A standalone top-level HWND has its own taskbar/Alt-Tab entry and
        // does not stay forcibly above the foobar window as an owned popup.
        g_pending_state=state.release();
        HWND created=CreateDialogParamW(
            core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_BATCH_PREVIEW),
            nullptr,batch_dialog_proc,reinterpret_cast<LPARAM>(g_pending_state));
        if (!created) {
            if (g_pending_state) {
                delete g_pending_state;
                g_pending_state=nullptr;
            }
            throw std::runtime_error("Unable to create the modeless workspace.");
        }
        ShowWindow(created,IsZoomed(created)?SW_SHOWMAXIMIZED:SW_SHOW);
        UpdateWindow(created);
    } catch (const std::exception& error) {
        std::string message =
            "Unable to prepare the read-only batch preview. Nothing was changed.\n\n";
        message += error.what();
        popup_message::g_show(message.c_str(), "Music Metadata Studio");
    }
}

} // namespace djmeta_foobar
