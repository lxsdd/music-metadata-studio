#pragma once

// SDK-independent native Win32 layout adapter shared by production dialogs
// and resource-backed runtime tests. IDs come from the real dialog resource.
#include "resource.h"
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <cstddef>
#include <numeric>
#include <optional>
#include <set>
#include <vector>
#include <string>
#include <string_view>

namespace djmeta_foobar {

// Only normal, unminimized windows yield genuine screen coordinates.
// An iconic WINDOWPLACEMENT normal rectangle is always workspace-based;
// retaining the last observed normal screen rectangle avoids switching
// coordinate systems when the user closes a minimized normal window.
inline bool native_capture_normal_screen_bounds(HWND dialog,RECT& screen) {
    if(!IsWindow(dialog) || IsIconic(dialog) || IsZoomed(dialog)) return false;
    RECT observed{};
    if(!GetWindowRect(dialog,&observed) ||
       observed.right<=observed.left || observed.bottom<=observed.top)
        return false;
    screen=observed;
    return true;
}

// GetWindowPlacement flags cannot be relied on to describe a minimized
// window's previous zoom state. WM_SIZE remembers the last non-iconic state.
inline constexpr bool native_workspace_saved_maximized(
    bool zoomed_now,bool iconic_now,bool last_nonminimized_maximized) noexcept {
    return zoomed_now || (iconic_now && last_nonminimized_maximized);
}

// The resource-backed Win32 production dialog and the Windows smoke test
// use the SAME restore adapter. A maximized dialog's normal rectangle came
// from GetWindowPlacement (workspace coordinates). Only SetWindowPlacement
// may consume those values; SetWindowPos expects screen coordinates.
inline bool native_restore_saved_workspace_bounds(
    HWND dialog,const RECT& restore_rect,bool maximized) {
    if(!IsWindow(dialog) || restore_rect.right<=restore_rect.left ||
       restore_rect.bottom<=restore_rect.top)
        return false;
    if(maximized) {
        WINDOWPLACEMENT placement{};
        placement.length=sizeof(placement);
        if(!GetWindowPlacement(dialog,&placement)) return false;
        placement.flags=0;
        placement.rcNormalPosition=restore_rect;
        placement.showCmd=SW_SHOWMAXIMIZED;
        return SetWindowPlacement(dialog,&placement)!=FALSE;
    }
    return SetWindowPos(dialog,nullptr,restore_rect.left,restore_rect.top,
                        restore_rect.right-restore_rect.left,
                        restore_rect.bottom-restore_rect.top,
                        SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;
}

/*
 * This small Win32 event/identity adapter is called by the production
 * dialog procedure as well as the resource-backed Windows smoke test.
 * All identities refer to underlying sources, never to sorted UI rows.
 * No host SDK or filesystem write API is needed here.
 */
// Preserve official API relevance order until a user explicitly sorts.
template<class CellText>
std::vector<std::size_t> native_candidate_view_order(
    std::size_t count, int sort_column, bool descending, CellText cell_text) {
    std::vector<std::size_t> order(count);
    std::iota(order.begin(),order.end(),std::size_t{0});
    if(sort_column>=0 && sort_column<5)
        std::stable_sort(order.begin(),order.end(),[&](auto left,auto right) {
            const auto a=cell_text(left,sort_column);
            const auto b=cell_text(right,sort_column);
            return descending?a>b:a<b;
        });
    return order;
}

// A track activation only dispatches an online request from Find & compare.
// Review/File locations use the same gesture as navigation to Find, not as
// implicit network consent. Beatport + Crates has no licensed catalog-search
// transport, so a track gesture never fetches a public URL from clipboard.
inline bool native_track_activation_starts_online_search(
    int source_tab_index, bool beatport_browser_mode) noexcept {
    return source_tab_index == 0 && !beatport_browser_mode;
}

// In the Beatport + Crates mode, Find-tab activation is explicit consent to
// open the existing Crates browser hand-off, not permission to call a gated
// Beatport catalog API or silently read a clipboard URL. From Review/File
// pages even this browser action must remain navigation-only.
inline bool native_track_activation_starts_crates_handoff(
    int source_tab_index, bool beatport_browser_mode) noexcept {
    return source_tab_index == 0 && beatport_browser_mode;
}

// Once a source/rules/physical guard marks the workspace STALE, no delayed
// network completion, tab refresh or candidate-selection callback may make
// source-dependent actions appear executable again. Refresh is separate.
inline constexpr bool native_workspace_action_ready(
    bool stale_snapshot, bool eligible) noexcept {
    return !stale_snapshot && eligible;
}

inline constexpr const wchar_t* kNativeStaleWorkspaceHint =
    L"Source metadata, file identity or rules changed. Results are stale. "
    L"Refresh snapshot to discard old decisions; no files were changed.";

inline constexpr const wchar_t* native_workspace_display_hint(
    bool stale_snapshot, const wchar_t* normal_text) noexcept {
    return stale_snapshot ? kNativeStaleWorkspaceHint : normal_text;
}

// A master ID, unverified candidate, stale context or pending request
// can never enable release-track inspection.
inline bool native_release_loadable(
    bool live, bool loaded, bool source_matches, bool request_pending,
    std::string_view selected_id, const std::vector<std::string>& exact_ids) {
    return live && !loaded && source_matches && !request_pending &&
        !selected_id.empty() &&
        std::find(exact_ids.begin(),exact_ids.end(),selected_id)!=exact_ids.end();
}

enum class PreviewCommand {
    None, Accept, Reject, Reset, ManualValue, TrackFilterChanged, FocusFilterChanged, VisibleWhitespaceChanged
};

inline PreviewCommand native_preview_command(UINT message, WPARAM wp) {
    if (message != WM_COMMAND) return PreviewCommand::None;
    if (HIWORD(wp) == BN_CLICKED) {
        switch (LOWORD(wp)) {
        case IDC_METADATA_ACCEPT: return PreviewCommand::Accept;
        case IDC_METADATA_REJECT: return PreviewCommand::Reject;
        case IDC_METADATA_RESET: return PreviewCommand::Reset;
        case IDC_METADATA_USE_VALUE: return PreviewCommand::ManualValue;
        case IDC_METADATA_VISIBLE_WHITESPACE: return PreviewCommand::VisibleWhitespaceChanged;
        default: break;
        }
    }
    if (HIWORD(wp) == CBN_SELCHANGE) {
        switch (LOWORD(wp)) {
        case IDC_METADATA_TRACK_FILTER: return PreviewCommand::TrackFilterChanged;
        case IDC_METADATA_FILTER: return PreviewCommand::FocusFilterChanged;
        default: break;
        }
    }
    return PreviewCommand::None;
}

// Read-only discovery/CUE inspection depends on one selected physical source.
// A full 54k-source scan before each online search blocks the host UI.
// Full batch validation still precedes every batch decision/Apply action;
// selected-source physical writers separately freeze and verify exact evidence.
enum class NativeWorkspaceCheckScope { None, SelectedSource, FullBatch };

inline constexpr NativeWorkspaceCheckScope native_workspace_command_check_scope(
    PreviewCommand command, UINT message, WPARAM wp) noexcept {
    if (message != WM_COMMAND || HIWORD(wp) != BN_CLICKED)
        return NativeWorkspaceCheckScope::None;
    switch(command) {
    case PreviewCommand::Accept:
    case PreviewCommand::Reject:
    case PreviewCommand::Reset:
    case PreviewCommand::ManualValue:
        return NativeWorkspaceCheckScope::FullBatch;
    default:
        break;
    }
    switch(LOWORD(wp)) {
    case IDC_BATCH_APPLY_SELECTED:
    case IDC_BATCH_APPLY_ALL:
        return NativeWorkspaceCheckScope::FullBatch;
    case IDC_METADATA_MB_SEARCH:
    case IDC_METADATA_MB_LOAD_RELEASE:
    case IDC_METADATA_DISCOGS_NEXT_PAGE:
    case IDC_METADATA_INSPECT_CUE:
    case IDC_METADATA_IMPORT_CANDIDATE:
    case IDC_ONLINE_WRITE_SELECTED:
        return NativeWorkspaceCheckScope::SelectedSource;
    default:
        return NativeWorkspaceCheckScope::None;
    }
}

// Display-only escaping of invisible metadata characters. Never pass the
// decorated output to the normalization engine or foobar tag/file adapters.
// No escaping is performed when the mode is disabled.
inline std::wstring preview_whitespace_text(std::wstring_view original, bool show) {
    if (!show) return std::wstring(original);
    std::wstring display;
    display.reserve(original.size());
    for (wchar_t ch : original) {
        switch (ch) {
        case L' ': display.push_back(L'\u00b7'); break;      // middle dot
        case L'\t': display.push_back(L'\u2192'); break;    // tab arrow
        case L'\r': display.push_back(L'\u21b5'); break;    // carriage return
        case L'\n': display.push_back(L'\u00b6'); break;    // paragraph mark
        case L'\u00a0': display.append(L"[NBSP]"); break;
        case L'\u202f': display.append(L"[NNBSP]"); break;
        case L'\u200b': display.append(L"[ZWSP]"); break;
        case L'\ufeff': display.append(L"[BOM]"); break;
        // Directional controls can visually reorder source, target and
        // provenance text. Expose them only in the optional display view;
        // never transform the actual value used for review or comparison.
        case L'\u200e': display.append(L"[LRM]"); break;
        case L'\u200f': display.append(L"[RLM]"); break;
        case L'\u202a': display.append(L"[LRE]"); break;
        case L'\u202b': display.append(L"[RLE]"); break;
        case L'\u202c': display.append(L"[PDF]"); break;
        case L'\u202d': display.append(L"[LRO]"); break;
        case L'\u202e': display.append(L"[RLO]"); break;
        case L'\u2066': display.append(L"[LRI]"); break;
        case L'\u2067': display.append(L"[RLI]"); break;
        case L'\u2068': display.append(L"[FSI]"); break;
        case L'\u2069': display.append(L"[PDI]"); break;
        case L'\u2060': display.append(L"[WJ]"); break;
        case L'\u2028': display.append(L"[LS]"); break;
        case L'\u2029': display.append(L"[PS]"); break;
        case L'\0': display.append(L"[NUL]"); break;
        case L'\x1b': display.append(L"[ESC]"); break;
        case L'\x7f': display.append(L"[DEL]"); break;
        default: display.push_back(ch); break;
        }
    }
    return display;
}

// Owner-data ListViews do not implement type-to-find without
// LVN_ODFINDITEM. Search cached Unicode display names through the *current*
// filtered/sorted view while preserving the source identity separately.
// This code never searches raw filesystem URLs or edits a metadata value.
inline int find_native_track_prefix(
    const std::vector<std::size_t>& view_order,
    const std::vector<std::wstring>& track_names,
    std::wstring_view prefix, int first_row, bool wrap) {
    if (prefix.empty() || prefix.size() > 16384 ||
        view_order.empty() || view_order.size() > 2147483647u)
        return -1;
    const int size = static_cast<int>(view_order.size());
    int start = first_row < 0 ? 0 : first_row;
    if (start >= size) {
        if (!wrap) return -1;
        start = 0;
    }
    const auto matches = [&](int view_index) {
        const auto source = view_order[static_cast<std::size_t>(view_index)];
        if (source >= track_names.size()) return false;
        const auto& name = track_names[source];
        if (name.size() < prefix.size()) return false;
        return CompareStringOrdinal(name.data(), static_cast<int>(prefix.size()),
                                    prefix.data(), static_cast<int>(prefix.size()),
                                    TRUE) == CSTR_EQUAL;
    };
    for (int row = start; row < size; ++row)
        if (matches(row)) return row;
    if (wrap)
        for (int row = 0; row < start; ++row)
            if (matches(row)) return row;
    return -1;
}

// Resolve the actual ListView LVN_ITEMCHANGED notification. Synthetic
// view-row events and refresh-generated changes must not retarget reviews.
inline std::optional<std::size_t> native_selected_track_change(
    const NMLISTVIEW& change, const std::vector<std::size_t>& view_order,
    bool refreshing) {
    if (refreshing || change.iItem < 0 ||
        (change.uChanged & LVIF_STATE) == 0 ||
        (change.uNewState & LVIS_SELECTED) == 0 ||
        (change.uOldState & LVIS_SELECTED) != 0)
        return std::nullopt;
    const auto row = static_cast<std::size_t>(change.iItem);
    if (row >= view_order.size()) return std::nullopt;
    return view_order[row];
}

inline std::vector<std::size_t> selected_native_view_ids(
    HWND list, const std::vector<std::size_t>& view_order) {
    std::vector<std::size_t> ids;
    if (!list) return ids;
    int row = -1;
    while ((row = ListView_GetNextItem(list, row, LVNI_SELECTED)) >= 0) {
        if (static_cast<std::size_t>(row) < view_order.size())
            ids.push_back(view_order[static_cast<std::size_t>(row)]);
    }
    return ids;
}

// Preserve multiselection and focus after sorting/filtering a virtual grid.
// The caller must supply the new item count before restoring selections.
inline void restore_native_view_selection(
    HWND list, const std::vector<std::size_t>& view_order,
    const std::vector<std::size_t>& selected_ids, std::size_t focused_id) {
    if (!list) return;
    const std::set<std::size_t> selected(selected_ids.begin(), selected_ids.end());
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    for (std::size_t row = 0; row < view_order.size(); ++row) {
        const bool chosen = selected.count(view_order[row]) != 0;
        if (!chosen && view_order[row] != focused_id) continue;
        const UINT state = (chosen ? LVIS_SELECTED : 0u) |
                           (view_order[row] == focused_id ? LVIS_FOCUSED : 0u);
        ListView_SetItemState(list, static_cast<int>(row), state,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
}

// Passive activation and read-only online commands verify the visible source;
// invalid selected indices fall back to full verification. Batch decisions
// and batch Apply check every source and physical guard. No approval is
// inferred from a selected-source lookup.
// Indexes are stable source identities, independent of sorted ListView rows.
inline constexpr bool native_workspace_should_validate_entry(
    std::size_t source_index, std::size_t selected_source_index,
    std::size_t source_count, bool full_source_check) noexcept {
    return full_source_check || selected_source_index >= source_count ||
           source_index == selected_source_index;
}

// WM_SIZE on minimization reports a zero client area. The real dialog
// must retain its last usable child geometry, tooltips and preview columns
// until a restored/maximized size with nonzero dimensions arrives.
// This is a presentation-only guard, shared with native HWND regression.
inline constexpr bool native_workspace_should_relayout_on_size(
    WPARAM size_kind, unsigned client_width, unsigned client_height) noexcept {
    return size_kind != SIZE_MINIMIZED &&
           client_width > 0 && client_height > 0;
}

// Native tooltips are popup HWNDs. Hiding a modeless workspace while a
// bounded network worker completes must dismiss the popup first: the parent
// HWND survives and ordinary TTF_SUBCLASS mouse-leave may never arrive.
// Also dismiss on activation loss, cancellation and explicit UI commands.
// This policy does not create timers, hook foobar globally or block playback.
inline constexpr bool native_workspace_dismisses_tooltip(
    UINT message, WPARAM wp) noexcept {
    switch(message) {
    case WM_CLOSE:
    case WM_CANCELMODE:
    case WM_MOVE:
    case WM_SIZE:
    case WM_ENTERSIZEMOVE:
    case WM_CAPTURECHANGED:
    case WM_KILLFOCUS:
    case WM_MOUSELEAVE:
        return true;
    case WM_SHOWWINDOW:
    case WM_NCACTIVATE:
    case WM_ENABLE:
        return wp == FALSE;
    case WM_ACTIVATE:
        return LOWORD(wp) == WA_INACTIVE;
    case WM_COMMAND:
        return HIWORD(wp) == BN_CLICKED || HIWORD(wp) == CBN_SELCHANGE;
    default:
        return false;
    }
}

// The empty candidate view must explain the SELECTED discovery path. A
// Beatport clipboard instruction under MusicBrainz/Discogs looked like a
// mandatory extra step and concealed lawful pasted Deezer/Spotify evidence.
inline constexpr const wchar_t* native_empty_online_candidate_hint(
    bool discogs_selected, bool beatport_offline_selected) noexcept {
    if (beatport_offline_selected)
        return L"Beatport: click Find in Crates, then paste a public Beatport "
               L"track URL or structured @beatport-track-facts and choose "
               L"Import clipboard. Comparison only; no tags or files change.";
    if (discogs_selected)
        return L"Discogs: click Search for the selected track, then inspect an "
               L"exact release. Structured clipboard evidence can also be "
               L"imported, but pasted provider claims are unverified. Read-only.";
    return L"MusicBrainz: click Search for the selected title/album, then "
           L"inspect an exact release. Deezer or Spotify facts can be compared "
           L"via Import clipboard with @provider=deezer or @provider=spotify; "
           L"pasted content is unverified and read-only.";
}

// One search action serves three very different authorized transports.
// A static Beatport-only tooltip on the MusicBrainz/Discogs Search button
// wrongly describes how that button acts after changing providers. Keep
// provider help pure and backed by immutable storage for tooltip HWNDs.
inline constexpr const wchar_t* native_search_action_tooltip(
    bool discogs_selected, bool beatport_offline_selected) noexcept {
    if (beatport_offline_selected)
        return L"Beatport offline: search is an explicit public track URL "
               L"lookup. Paste one canonical URL or leave the box empty "
               L"to use a URL you just copied from Crates. No catalog login.";
    if (discogs_selected)
        return L"Search Discogs with the current title/artist/version or "
               L"exact release/master URL. Use Broaden/Next page explicitly. "
               L"Your personal token is never stored.";
    return L"Search MusicBrainz with title/album plus available source "
           L"identity and guarded fallback strategies. This is evidence, "
           L"not an approved metadata change.";
}

// Keep the two real master/detail ListViews balanced across a resize.
// Rectangles use the dialog client coordinate system and remain separate
// from review identity, normalization and write logic.
struct ReviewSplitGeometry {
    RECT master{};
    RECT detail{};
};
inline ReviewSplitGeometry review_split_geometry(
    RECT master, RECT detail, int width_delta, int height_delta,
    int preferred_offset = 0, int min_master = 160, int min_detail = 180) {
    const int gap = detail.left - master.right;
    const int base_right = master.right + width_delta / 2;
    const int left_limit = master.left + min_master;
    const int right_limit = detail.right + width_delta - gap - min_detail;
    // A very narrow host rectangle keeps the existing proportional split;
    // otherwise a dragged divider cannot collapse either ListView.
    const int boundary = right_limit >= left_limit
        ? (std::clamp)(base_right + preferred_offset, left_limit, right_limit)
        : base_right;
    const int master_delta = boundary - master.right;
    master.right = boundary;
    master.bottom += height_delta;
    detail.left += master_delta;
    detail.right += width_delta;
    detail.bottom += height_delta;
    return {master, detail};
}

inline bool review_split_gap_hit(RECT master, RECT detail, int x, int y) noexcept {
    return x >= master.right && x < detail.left &&
           y >= (std::max)(master.top, detail.top) &&
           y < (std::min)(master.bottom, detail.bottom);
}

inline bool apply_review_split_geometry(HWND master_list, HWND detail_list,
                                        RECT original_master, RECT original_detail,
                                        int width_delta, int height_delta, int preferred_offset = 0) {
    if (!IsWindow(master_list) || !IsWindow(detail_list)) return false;
    const auto next = review_split_geometry(original_master, original_detail,
                                             width_delta, height_delta,
                                             preferred_offset);
    const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    return SetWindowPos(master_list, nullptr, next.master.left, next.master.top,
                        next.master.right - next.master.left,
                        next.master.bottom - next.master.top, flags) != FALSE &&
           SetWindowPos(detail_list, nullptr, next.detail.left, next.detail.top,
                        next.detail.right - next.detail.left,
                        next.detail.bottom - next.detail.top, flags) != FALSE;
}

// Single atomic move for all native dialog controls.  Moving siblings one by
// one during WM_SIZE left stale button invalidation/overlap artifacts. The
// original rectangles are always the unresized resource geometry, so repeated
// drag-resizes cannot accumulate rounding drift.
struct NativePreviewResizeChild {
    HWND window = nullptr;
    RECT original{};
    bool stretch_width = false;
    bool stretch_height = false;
    bool shift_down = false;
    bool shift_right = false;
};

inline bool apply_native_preview_resize(
    const std::vector<NativePreviewResizeChild>& children,
    HWND master_list, HWND detail_list, int width_delta, int height_delta,
    int preferred_offset = 0, int min_master = 160, int min_detail = 180) {
    if (children.empty() || children.size() > 256) return false;
    const RECT* master = nullptr;
    const RECT* detail = nullptr;
    for (const auto& child : children) {
        if (child.window == master_list) master = &child.original;
        if (child.window == detail_list) detail = &child.original;
    }
    const auto split = master && detail
        ? review_split_geometry(*master, *detail, width_delta, height_delta,
                                preferred_offset, min_master, min_detail)
        : ReviewSplitGeometry{};
    HDWP defer = BeginDeferWindowPos(static_cast<int>(children.size()));
    if (!defer) return false;
    for (const auto& child : children) {
        if (!IsWindow(child.window)) continue;
        const RECT& source = child.window == master_list && master ? split.master :
                             child.window == detail_list && detail ? split.detail :
                             child.original;
        const bool split_child = child.window == master_list || child.window == detail_list;
        const int x = source.left + (!split_child && child.shift_right ? width_delta : 0);
        const int y = source.top + (!split_child && child.shift_down ? height_delta : 0);
        const int w = source.right - source.left +
            (!split_child && child.stretch_width ? width_delta : 0);
        const int h = source.bottom - source.top +
            (!split_child && child.stretch_height ? height_delta : 0);
        defer = DeferWindowPos(defer, child.window, nullptr, x, y,
            (std::max)(8, w), (std::max)(8, h),
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        if (!defer) return false;
    }
    return EndDeferWindowPos(defer) != FALSE;
}

// Content-aware FIRST-OPEN column fitting. Samples at most 96 displayed
// names, not the user's full foobar library. Never call for a track change or
// overwrite a custom profile layout. Returns logical 96-DPI units.
inline int fit_initial_track_title_column(HWND list,
    const std::vector<std::wstring>& labels,int dpi) {
    if(!list || dpi<96) return 265;
    RECT area{};
    if(!GetClientRect(list,&area)) return 265;
    const int available=(std::max)(MulDiv(165,dpi,96),
        static_cast<int>(area.right-area.left)-MulDiv(225,dpi,96));
    int desired=static_cast<int>(SendMessageW(list,LVM_GETSTRINGWIDTHW,0,
        reinterpret_cast<LPARAM>(L"Track")))+MulDiv(34,dpi,96);
    const std::size_t max_samples=(std::min)(labels.size(),std::size_t(96));
    for(std::size_t i=0;i<max_samples;++i) {
        const auto& label=labels[i];
        if(label.empty()) continue;
        const int measured=static_cast<int>(SendMessageW(list,LVM_GETSTRINGWIDTHW,0,
            reinterpret_cast<LPARAM>(label.c_str())));
        if(measured>0) desired=(std::max)(desired,measured+MulDiv(30,dpi,96));
    }
    const int min_px=MulDiv(165,dpi,96);
    const int max_px=(std::max)(min_px,(std::min)(available,MulDiv(600,dpi,96)));
    const int actual=(std::clamp)(desired,min_px,max_px);
    return (std::max)(48,MulDiv(actual,96,dpi));
}

inline bool align_label_to_input(HWND dialog, int label_id, int input_id) {
    const HWND label = GetDlgItem(dialog, label_id);
    const HWND input = GetDlgItem(dialog, input_id);
    if (!label || !input) return false;
    RECT label_rect{}, input_rect{};
    if (!GetWindowRect(label, &label_rect) ||
        !GetWindowRect(input, &input_rect)) return false;

    // A combo box's RC height includes its drop-down list. Use the editable
    // field's actual screen rectangle, not the expanded list rectangle.
    COMBOBOXINFO combo{};
    combo.cbSize = sizeof(combo);
    if (GetComboBoxInfo(input, &combo)) {
        // COMBOBOXINFO.rcItem is LOCAL to the combo itself, not a desktop
        // rectangle! The previous code compared y=3..20 with a screen
        // y≈700 action, shifting label/control disastrously on a moved dialog.
        input_rect=combo.rcItem;
        MapWindowPoints(input,HWND_DESKTOP,
                        reinterpret_cast<POINT*>(&input_rect),2);
    }

    MapWindowPoints(HWND_DESKTOP, dialog,
                    reinterpret_cast<POINT*>(&label_rect), 2);
    MapWindowPoints(HWND_DESKTOP, dialog,
                    reinterpret_cast<POINT*>(&input_rect), 2);
    const int label_height = label_rect.bottom - label_rect.top;
    const int target_y = input_rect.top +
        ((input_rect.bottom - input_rect.top) - label_height) / 2;
    return SetWindowPos(label, nullptr, label_rect.left, target_y, 0, 0,
                        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) != 0;
}

// Align real native input text rows rather than assuming edit/combo fonts
// occupy the same vertical pixels as themed pushbutton captions. Repeated
// WM_SIZE calls use current control geometry, so alignment is idempotent.
// A multi-select ListView can have a first selected row DIFFERENT from
// the one the user actually activates. Resolve the explicit mouse/keyboard
// target only when THAT target is selected. Empty-space and unrelated
// selection notifications must not authorize a provider request.
inline int native_selected_release_activation_row(
    UINT notification, int clicked_row, int focused_row,
    bool clicked_selected, bool focused_selected, UINT key = 0) noexcept {
    if (notification == NM_DBLCLK)
        return clicked_row >= 0 && clicked_selected ? clicked_row : -1;
    if (notification == LVN_KEYDOWN && key == VK_RETURN)
        return focused_row >= 0 && focused_selected ? focused_row : -1;
    return -1;
}

// The existing Load release command consumes the first selected ListView row.
// Retarget it to the explicitly activated identity BEFORE dispatch, otherwise
// a multi-selection could load the wrong edition. This deliberately collapses
// the candidate selection on activation, never during ordinary comparison.
inline bool native_focus_exact_release_row(HWND list, int target_row) {
    if (!list || target_row < 0 || target_row >= ListView_GetItemCount(list) ||
        (ListView_GetItemState(list, target_row, LVIS_SELECTED) &
         LVIS_SELECTED) == 0)
        return false;
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
    ListView_SetItemState(list, target_row,
        LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    return ListView_GetSelectedCount(list) == 1 &&
        ListView_GetNextItem(list, -1, LVNI_SELECTED) == target_row &&
        ListView_GetNextItem(list, -1, LVNI_FOCUSED) == target_row;
}

// LVN_ITEMACTIVATE is the ListView's platform-native user intent for
// double-click and keyboard activation. Never allow it to trigger network
// access on an unrelated tab, physical-recording hit or loaded track list.
inline bool may_activate_musicbrainz_release(UINT notification,
    bool live, bool release_search, bool release_loaded, bool source_matches,
    unsigned key = 0) {
    // LVN_ITEMACTIVATE is not consistently emitted for owner-data ListViews.
    // Use exactly one mouse event OR an explicit Return key notification.
    return (notification==NM_DBLCLK ||
            (notification==LVN_KEYDOWN && key==VK_RETURN)) &&
           live && release_search && !release_loaded && source_matches;
}

inline bool align_native_action_row(HWND dialog, int input_id,
                                     int button_id, bool combo,
                                     int optical_offset_px = 0) {
    const HWND input=GetDlgItem(dialog,input_id);
    const HWND button=GetDlgItem(dialog,button_id);
    if (!input || !button) return false;
    RECT input_rect{}, button_rect{}, outer_rect{};
    if (!GetWindowRect(input,&outer_rect) ||
        !GetWindowRect(button,&button_rect)) return false;
    input_rect=outer_rect;
    if (combo) {
        COMBOBOXINFO info{};
        info.cbSize=sizeof(info);
        if (!GetComboBoxInfo(input,&info)) return false;
        input_rect=info.rcItem;
        // Local combo edit item to desktop, THEN compare with GetWindowRect.
        MapWindowPoints(input,HWND_DESKTOP,
                        reinterpret_cast<POINT*>(&input_rect),2);
    }
    const int actual_middle=input_rect.top+(input_rect.bottom-input_rect.top)/2;
    const int button_middle=button_rect.top+(button_rect.bottom-button_rect.top)/2;
    const int delta=button_middle+optical_offset_px-actual_middle;
    if (delta == 0) return true;
    // Convert the current SCREEN rect to dialog CLIENT coordinates before
    // SetWindowPos. Using screen y here would shove controls off-screen on
    // moved/maximized windows, and x=0 would lose the search-field offset.
    MapWindowPoints(HWND_DESKTOP,dialog,
                    reinterpret_cast<POINT*>(&outer_rect),2);
    return SetWindowPos(input,nullptr,outer_rect.left,outer_rect.top+delta,0,0,
                        SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;
}

// Structural bounds for the two-level MusicBrainz browser. The caption
// belongs to its own positive-height line and MUST NOT collide with the
// nested ListView's column header at 100%-200% DPI.
struct MusicBrainzPaneBounds {
    RECT hits{};
    RECT caption{};
    RECT details{};
};
inline MusicBrainzPaneBounds musicbrainz_pane_bounds(RECT frame,int dpi) {
    const int height=(std::max<int>)(0,frame.bottom-frame.top);
    const int detail_height=(std::clamp)(height/3,98,210);
    const int gap=(std::max)(26,MulDiv(29,dpi,96));
    const int top_height=(std::max)(70,height-detail_height-gap);
    const int upper_end=frame.top+top_height;
    return {
        {frame.left,frame.top,frame.right,upper_end},
        {frame.left,upper_end+5,frame.right,upper_end+23},
        {frame.left,upper_end+gap,frame.right,frame.bottom}
    };
}

// Set *all* fields with explicit LVIF_TEXT rather than relying on a
// ListView_SetItemText macro binding that can silently leave subitems blank.
// Caller keeps metadata data in its own immutable candidate model.
inline bool insert_native_preview_detail_row(HWND list, int index,
    const std::wstring& field, const std::wstring& original,
    const std::wstring& proposed, const std::wstring& status) {
    if (!list || index<0) return false;
    LVITEMW first{};
    first.mask=LVIF_TEXT;
    first.iItem=index;
    first.iSubItem=0;
    first.pszText=const_cast<LPWSTR>(field.c_str());
    const int inserted=static_cast<int>(SendMessageW(
        list,LVM_INSERTITEMW,0,reinterpret_cast<LPARAM>(&first)));
    if (inserted<0) return false;
    const std::wstring* values[]={&original,&proposed,&status};
    for(int column=1;column<4;++column) {
        LVITEMW item{};
        item.mask=LVIF_TEXT;
        item.iItem=inserted;
        item.iSubItem=column;
        item.pszText=const_cast<LPWSTR>(values[column-1]->c_str());
        if (!SendMessageW(list,LVM_SETITEMW,0,
                          reinterpret_cast<LPARAM>(&item))) {
            ListView_DeleteItem(list,inserted);
            return false;
        }
    }
    return true;
}

// After a new provider query or a changed track, a virtual ListView may
// retain selection INDEX 0/1 while representing an entirely different
// release. A selected row is not an identity. Unless the exact source ID
// survives the rebuilt candidate order, reselect the explicit first row and
// refresh its evidence. This guards the Load release action from stale rows.
inline bool native_browser_selection_requires_reset(
    bool same_candidate_identity, int actual_selected_row,
    std::size_t verified_view_row) noexcept {
    if(!same_candidate_identity) return true;
    return actual_selected_row<0 ||
        static_cast<std::size_t>(actual_selected_row)!=verified_view_row;
}

// Search progress is visible in the actual native button, not buried in a
// status label: a second deliberate click broadens nonmatching first-page
// Discogs hits without browsing an artist tree. No request starts merely
// because this caption is shown. Direct editions and master pagination never
// masquerade as a new search stage.
inline const wchar_t* native_discogs_search_caption(
    bool discogs_selected, bool master_next, bool has_published_results,
    bool correct_source, bool exact_edition_loaded, bool request_pending,
    std::size_t completed_stage, std::size_t stage_count) noexcept {
    if(!discogs_selected) return L"Search MusicBrainz";
    if(master_next) return L"Next master page";
    const bool may_broaden=has_published_results && correct_source &&
        !exact_edition_loaded && !request_pending &&
        stage_count>1 && stage_count<=16 &&
        completed_stage<stage_count-1;
    return may_broaden ? L"Broaden Discogs" : L"Search Discogs";
}

// Native single-line EDIT fields put text near the TOP of a tall box;
// moving that tall box cannot align its glyphs with a themed button.
// Size the field to the actual UI font plus bounded padding first, and
// center the resulting edit *frame* against its adjacent action.
inline bool align_native_edit_to_action(HWND dialog,
                                        int edit_id, int button_id) {
    const HWND edit=GetDlgItem(dialog,edit_id);
    const HWND button=GetDlgItem(dialog,button_id);
    if (!edit || !button) return false;
    RECT field{}, action{};
    if (!GetWindowRect(edit,&field) || !GetWindowRect(button,&action))
        return false;
    HDC dc=GetDC(edit);
    if (!dc) return false;
    HFONT font=reinterpret_cast<HFONT>(SendMessageW(edit,WM_GETFONT,0,0));
    HGDIOBJ previous=font?SelectObject(dc,font):nullptr;
    TEXTMETRICW metrics{};
    const bool measured=GetTextMetricsW(dc,&metrics)!=FALSE;
    if (previous) SelectObject(dc,previous);
    ReleaseDC(edit,dc);
    if (!measured) return false;
    const int actionHeight=action.bottom-action.top;
    // Keep the native EDIT text's upper inset close to its actual text height.
    // At 100-200% Windows scaling avoid an unreasonably tall input frame.
    const int height=(std::min)(actionHeight,(std::max)(static_cast<int>(metrics.tmHeight)+6,15));
    const int targetY=(action.top+action.bottom+4-height)/2;
    MapWindowPoints(HWND_DESKTOP,dialog,reinterpret_cast<POINT*>(&field),2);
    POINT top{0,targetY};
    MapWindowPoints(HWND_DESKTOP,dialog,&top,1);
    if (field.top==top.y && field.bottom-field.top==height) return true;
    return SetWindowPos(edit,nullptr,field.left,top.y,
        field.right-field.left,height,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;
}

// Keep the search field and its action aligned as one native toolbar.
inline bool align_native_search_field(HWND dialog) {
    return align_native_edit_to_action(
        dialog,IDC_METADATA_MB_QUERY,IDC_METADATA_MB_SEARCH);
}

inline void align_native_preview_form(HWND dialog) {
    constexpr int rows[][2] = {
        {IDC_METADATA_FILTER_LABEL, IDC_METADATA_FILTER},
        {IDC_METADATA_TRACK_FILTER_LABEL, IDC_METADATA_TRACK_FILTER},
        {IDC_BATCH_ROUTE_LABEL, IDC_BATCH_PROFILE_PICKER},
        {IDC_BATCH_DEST_LABEL, IDC_BATCH_DESTINATION},
        {IDC_BATCH_PATTERN_LABEL, IDC_BATCH_PATTERN},
    };
    for (const auto& row : rows)
        align_label_to_input(dialog, row[0], row[1]);
    // Theme-dependent text in a standard EDIT normally sits slightly higher
    // than text in a PUSHBUTTON with the same outer frame height.
    align_native_search_field(dialog);
    // The manual replacement EDIT is also a single-line native control.
    // Use the same font-measured optical baseline as the Search EDIT so
    // neither toolbar's value and action drift apart after a DPI resize.
    align_native_edit_to_action(dialog, IDC_METADATA_MANUAL_INPUT,
                                IDC_METADATA_USE_VALUE);
    // The combo RC height includes its dropdown; align *rcItem* with the
    // adjacent action buttons, not the full expanded list rectangle.
    align_native_action_row(dialog, IDC_METADATA_REVIEW_SCOPE,
                            IDC_METADATA_ACCEPT, true, 0);
}

} // namespace djmeta_foobar
