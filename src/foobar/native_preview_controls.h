#pragma once

// SDK-independent native Win32 layout adapter shared by production dialogs
// and resource-backed runtime tests. IDs come from the real dialog resource.
#include "resource.h"
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <vector>
#include <string>
#include <string_view>

namespace djmeta_foobar {


/*
 * This small Win32 event/identity adapter is called by the production
 * dialog procedure as well as the resource-backed Windows smoke test.
 * All identities refer to underlying sources, never to sorted UI rows.
 * No host SDK or filesystem write API is needed here.
 */
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

// Keep the two real master/detail ListViews balanced across a resize.
// Rectangles use the dialog client coordinate system and remain separate
// from review identity, normalization and write logic.
struct ReviewSplitGeometry {
    RECT master{};
    RECT detail{};
};
inline ReviewSplitGeometry review_split_geometry(
    RECT master, RECT detail, int width_delta, int height_delta) {
    const int master_delta = width_delta / 2;
    master.right += master_delta;
    master.bottom += height_delta;
    detail.left += master_delta;
    detail.right += width_delta;
    detail.bottom += height_delta;
    return {master, detail};
}

inline bool apply_review_split_geometry(HWND master_list, HWND detail_list,
                                        RECT original_master, RECT original_detail,
                                        int width_delta, int height_delta) {
    if (!IsWindow(master_list) || !IsWindow(detail_list)) return false;
    const auto next = review_split_geometry(original_master, original_detail,
                                             width_delta, height_delta);
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
    HWND master_list, HWND detail_list, int width_delta, int height_delta) {
    if (children.empty() || children.size() > 256) return false;
    const RECT* master = nullptr;
    const RECT* detail = nullptr;
    for (const auto& child : children) {
        if (child.window == master_list) master = &child.original;
        if (child.window == detail_list) detail = &child.original;
    }
    const auto split = master && detail
        ? review_split_geometry(*master, *detail, width_delta, height_delta)
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

// Native single-line EDIT fields put text near the TOP of a tall box;
// moving that tall box cannot align its glyphs with a themed button.
// Size the field to the actual UI font plus bounded padding first, and
// center the resulting edit *frame* against its adjacent action.
inline bool align_native_search_field(HWND dialog) {
    const HWND edit=GetDlgItem(dialog,IDC_METADATA_MB_QUERY);
    const HWND button=GetDlgItem(dialog,IDC_METADATA_MB_SEARCH);
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
    // The combo RC height includes its dropdown; align *rcItem* with the
    // adjacent action buttons, not the full expanded list rectangle.
    align_native_action_row(dialog, IDC_METADATA_REVIEW_SCOPE,
                            IDC_METADATA_ACCEPT, true, 0);
}

} // namespace djmeta_foobar
