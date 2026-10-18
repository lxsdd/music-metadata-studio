#include "../src/foobar/native_preview_controls.h"
#include "../src/foobar/review_grid_controls.h"
#include "../src/foobar/legacy_routing_profiles.h"

#include <commctrl.h>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <vector>
#include <array>
#include <utility>

namespace {
void check(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}
djmeta_foobar::PreviewCommand last_command = djmeta_foobar::PreviewCommand::None;
int command_notifications = 0;
std::vector<std::size_t> active_track_order;
std::size_t notified_track_identity = static_cast<std::size_t>(-1);
int notified_find_row = -99;
std::vector<std::wstring> active_track_names;
bool rebuilding_master = false;
INT_PTR CALLBACK test_dialog_proc(HWND dialog, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_NOTIFY) {
        const auto* hdr = reinterpret_cast<const NMHDR*>(lp);
        if (hdr && hdr->idFrom == IDC_METADATA_TRACK_LIST &&
            hdr->code == LVN_ODFINDITEMW) {
            const auto* find = reinterpret_cast<const NMLVFINDITEMW*>(lp);
            notified_find_row = djmeta_foobar::find_native_track_prefix(
                active_track_order, active_track_names, find->lvfi.psz,
                find->iStart, (find->lvfi.flags & LVFI_WRAP) != 0);
            SetWindowLongPtrW(dialog, DWLP_MSGRESULT, notified_find_row);
            return TRUE;
        }
        if (hdr && hdr->idFrom == IDC_METADATA_TRACK_LIST &&
            hdr->code == LVN_ITEMCHANGED) {
            const auto* change = reinterpret_cast<const NMLISTVIEW*>(lp);
            if (const auto track = djmeta_foobar::native_selected_track_change(
                    *change, active_track_order, rebuilding_master))
                notified_track_identity = *track;
        }
    }
    const auto command = djmeta_foobar::native_preview_command(message, wp);
    if (command != djmeta_foobar::PreviewCommand::None) {
        last_command = command;
        ++command_notifications;
        return TRUE;
    }
    return FALSE;
}
bool centers_match(HWND dialog, int label_id, int control_id) {
    HWND label = GetDlgItem(dialog, label_id);
    HWND input = GetDlgItem(dialog, control_id);
    if (!label || !input) return false;
    RECT a{}, b{};
    if (!GetWindowRect(label, &a) || !GetWindowRect(input, &b))
        return false;
    COMBOBOXINFO info{};
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(input, &info)) b = info.rcItem;
    const int offset = (a.top + a.bottom) - (b.top + b.bottom);
    return offset >= -2 && offset <= 2;
}
}

int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls),
                                  ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES};
    check(InitCommonControlsEx(&controls) != FALSE,
          "native listview and tabs initialization");
    HWND dialog = CreateDialogParamW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_BATCH_PREVIEW), nullptr, test_dialog_proc, 0);
    check(dialog != nullptr, "actual production dialog template instantiation");

    HWND combo = GetDlgItem(dialog, IDC_BATCH_PROFILE_PICKER);
    check(combo && !GetDlgItem(dialog, IDC_BATCH_PROFILE_NAME),
          "single routing profile picker; no duplicate name editor");
    const auto style = static_cast<DWORD>(GetWindowLongPtrW(combo, GWL_STYLE));
    check((style & 0x3u) == CBS_DROPDOWN,
          "routing profile picker is editable with real Win32 control");
    check(GetDlgItem(dialog, IDC_BATCH_DESTINATION) &&
          GetDlgItem(dialog, IDC_BATCH_PATTERN) &&
          GetDlgItem(dialog, IDC_BATCH_ROUTE_LABEL),
          "route profile is accompanied by destination and naming expression");

    const wchar_t* names[] = {L"Singles", L"Alben", L"Livesets"};
    for (const auto* name : names)
        check(SendMessageW(combo, CB_ADDSTRING, 0,
              reinterpret_cast<LPARAM>(name)) != CB_ERR,
              "historical route name accepted");
    check(SendMessageW(combo, CB_SETCURSEL, 1, 0) == 1,
          "historical preset can be selected");
    wchar_t value[128]{};
    GetWindowTextW(combo, value, 128);
    check(std::wcscmp(value, L"Alben") == 0,
          "exact user-owned preset name preserved");
    check(SetWindowTextW(combo, L"My DJ profile") != FALSE,
          "custom profile name editable");
    GetWindowTextW(combo, value, 128);
    check(std::wcscmp(value, L"My DJ profile") == 0,
          "custom profile text survives in the actual control");

    HWND review_scope = GetDlgItem(dialog, IDC_METADATA_REVIEW_SCOPE);
    HWND manual = GetDlgItem(dialog, IDC_METADATA_MANUAL_INPUT);
    check(review_scope && manual &&
          (static_cast<DWORD>(GetWindowLongPtrW(review_scope, GWL_STYLE)) & 0x3u) ==
              CBS_DROPDOWNLIST,
          "review scope is the real non-editable Windows selection control");
    check(SetWindowTextW(manual, L"Custom review value") != FALSE,
          "manual value control accepts editor text");
    wchar_t manual_text[128]{};
    GetWindowTextW(manual, manual_text, 128);
    check(std::wcscmp(manual_text, L"Custom review value") == 0,
          "manual value roundtrip uses the actual Win32 control");
    RECT client{};
    GetClientRect(dialog, &client);
    for (int id : {IDC_METADATA_ACCEPT, IDC_METADATA_REJECT,
                   IDC_METADATA_RESET, IDC_METADATA_USE_VALUE}) {
        HWND button = GetDlgItem(dialog, id);
        check(button != nullptr, "production review action exists");
        RECT bounds{};
        GetWindowRect(button, &bounds);
        MapWindowPoints(HWND_DESKTOP, dialog,
                        reinterpret_cast<POINT*>(&bounds), 2);
        const bool inside = bounds.left >= 0 && bounds.right <= client.right &&
                            bounds.top >= 0 && bounds.bottom <= client.bottom;
        if (!inside)
            std::cerr << "Review action geometry: id=" << id
                      << " x=" << bounds.left << ".." << bounds.right
                      << " y=" << bounds.top << ".." << bounds.bottom
                      << " client=" << client.right << "x" << client.bottom << "\n";
        check(inside, "review actions fit inside minimum-sized native dialog");
    }

    for (int id : {IDC_METADATA_TRACK_LIST, IDC_METADATA_LIST}) {
        HWND list = GetDlgItem(dialog, id);
        check(list != nullptr, "actual master/detail list control present");
        const auto flags = static_cast<DWORD>(GetWindowLongPtrW(list, GWL_STYLE));
        check((flags & LVS_TYPEMASK) == LVS_REPORT &&
              (flags & LVS_OWNERDATA) != 0,
              "master/detail controls support virtualized report view");
    }
    // Initialize columns on the real resource-backed Windows ListViews,
    // exactly as the foobar dialog does, then exercise the shared adapter.
    for (const auto entry : {std::pair{IDC_METADATA_TRACK_LIST, 4},
                             std::pair{IDC_METADATA_LIST, 5}}) {
        HWND list = GetDlgItem(dialog, entry.first);
        for (int col = 0; col < entry.second; ++col) {
            LVCOLUMNW c{};
            c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            c.pszText = const_cast<LPWSTR>(L"Column");
            c.cx = 90;
            c.iSubItem = col;
            check(ListView_InsertColumn(list, col, &c) == col,
                  "insert physical header column into production ListView");
        }
    }
    auto track_layout = djmeta::default_review_grid_layout<4>({150, 48, 48, 48});
    track_layout.order = {3, 1, 0, 2};
    track_layout.sort_column = 1;
    track_layout.sort_descending = true;
    check(djmeta::set_review_column_visible(track_layout, 2, false),
          "track review column hidden");
    HWND track_list = GetDlgItem(dialog, IDC_METADATA_TRACK_LIST);
    djmeta_foobar::apply_review_grid_controls(track_list, track_layout, 96);
    check(ListView_GetColumnWidth(track_list, 2) == 0,
          "real virtual track grid hides review column without deletion");
    std::array<int, 4> actual_track_order{};
    check(ListView_GetColumnOrderArray(track_list, 4, actual_track_order.data()) &&
          actual_track_order == track_layout.order,
          "real track header supports independent drag order");
    const HWND track_header = ListView_GetHeader(track_list);
    HDITEMW selected_sort{};
    selected_sort.mask = HDI_FORMAT;
    check(SendMessageW(track_header, HDM_GETITEMW, 1,
                       reinterpret_cast<LPARAM>(&selected_sort)) &&
          (selected_sort.fmt & HDF_SORTDOWN) != 0,
          "real Windows review header shows descending sort indicator");
    const auto hidden_width = track_layout.widths[2];
    ListView_SetColumnWidth(track_list, 0, 190);
    djmeta_foobar::capture_review_grid_controls(track_list, track_layout, 96);
    check(track_layout.widths[0] == 190 && track_layout.widths[2] == hidden_width,
          "real resizing persists visible width but retains hidden width");
    auto detail_layout = djmeta::default_review_grid_layout<5>({80, 105, 105, 60, 75});
    detail_layout.order = {4, 0, 1, 3, 2};
    HWND detail_list = GetDlgItem(dialog, IDC_METADATA_LIST);
    djmeta_foobar::apply_review_grid_controls(detail_list, detail_layout, 96);
    std::array<int, 5> actual_detail_order{};
    check(ListView_GetColumnOrderArray(detail_list, 5, actual_detail_order.data()) &&
          actual_detail_order == detail_layout.order,
          "real five-column detail header supports persisted logical order");
    djmeta_foobar::capture_review_grid_controls(detail_list, detail_layout, 96);
    check(detail_layout.order == actual_detail_order,
          "detail header order roundtrips via production adapter");
    check(djmeta::parse_review_grid_layout(
              djmeta::serialize_review_grid_layout(detail_layout),
              djmeta::default_review_grid_layout<5>({80, 105, 105, 60, 75})).order ==
          detail_layout.order,
          "native detail layout roundtrips through the strict persisted schema");

    // The actual display-only control lives on the production RC dialog.
    HWND whitespace = GetDlgItem(dialog, IDC_METADATA_VISIBLE_WHITESPACE);
    check(whitespace != nullptr &&
          (GetWindowLongPtrW(whitespace, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX,
          "real production show-whitespace checkbox is checkable");
    SendMessageW(whitespace, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(dialog, WM_COMMAND,
                 MAKEWPARAM(IDC_METADATA_VISIBLE_WHITESPACE, BN_CLICKED),
                 reinterpret_cast<LPARAM>(whitespace));
    check(last_command == djmeta_foobar::PreviewCommand::VisibleWhitespaceChanged &&
          SendMessageW(whitespace, BM_GETCHECK, 0, 0) == BST_CHECKED,
          "real checkbox sends shared production display-event command");

    const std::wstring original = L"  A\tB\u00a0C\u202fD\u200bE\ufeff\r\n";
    const std::wstring visible = djmeta_foobar::preview_whitespace_text(original, true);
    check(visible == L"\u00b7\u00b7A\u2192B[NBSP]C[NNBSP]D[ZWSP]E[BOM]\u21b5\u00b6",
          "display-only renderer distinguishes spaces, tabs, newlines and invisible unicode");
    check(djmeta_foobar::preview_whitespace_text(original, false) == original &&
          original == L"  A\tB\u00a0C\u202fD\u200bE\ufeff\r\n",
          "disable view leaves input metadata unchanged");
    check(djmeta_foobar::preview_whitespace_text(L"", true).empty(),
          "empty metadata remains empty in visible-whitespace view");

    // Send genuine BN_CLICKED notifications from the production buttons
    // through the same decoder used by the real foobar dialog procedure.
    const struct {int id; djmeta_foobar::PreviewCommand expected;} buttons[] = {
        {IDC_METADATA_ACCEPT, djmeta_foobar::PreviewCommand::Accept},
        {IDC_METADATA_REJECT, djmeta_foobar::PreviewCommand::Reject},
        {IDC_METADATA_RESET, djmeta_foobar::PreviewCommand::Reset},
        {IDC_METADATA_USE_VALUE, djmeta_foobar::PreviewCommand::ManualValue}
    };
    for (const auto& button : buttons) {
        const int before = command_notifications;
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(button.id, BN_CLICKED),
                     reinterpret_cast<LPARAM>(GetDlgItem(dialog, button.id)));
        check(command_notifications == before + 1 && last_command == button.expected,
              "production review command routes to the intended action");
    }
    HWND track_filter = GetDlgItem(dialog, IDC_METADATA_TRACK_FILTER);
    check(track_filter != nullptr &&
          (static_cast<DWORD>(GetWindowLongPtrW(track_filter, GWL_STYLE)) & 0x3u) == CBS_DROPDOWNLIST,
          "real native status filter is a non-editable combo");
    for (const auto& filter : {
        std::pair{IDC_METADATA_TRACK_FILTER, djmeta_foobar::PreviewCommand::TrackFilterChanged},
        std::pair{IDC_METADATA_FILTER, djmeta_foobar::PreviewCommand::FocusFilterChanged}
    }) {
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(filter.first, CBN_SELCHANGE),
                     reinterpret_cast<LPARAM>(GetDlgItem(dialog, filter.first)));
        check(last_command == filter.second, "production combo selection notification");
    }
    // The actual owner-data ListView must retain source IDs across a re-sort:
    // the second view has different row positions for both selected tracks.
    HWND master = GetDlgItem(dialog, IDC_METADATA_TRACK_LIST);
    check(ListView_SetItemCountEx(master, 3, 0) != FALSE,
          "production virtual master accepts three rows");
    ListView_SetItemState(master, 0, LVIS_SELECTED, LVIS_SELECTED);
    ListView_SetItemState(master, 2, LVIS_SELECTED, LVIS_SELECTED);
    const std::vector<std::size_t> initial_order{13, 5, 9};
    active_track_order = initial_order;
    // Simulated incremental owner-data search notification routed through
    // the *same* helper as production; the actual RC master is the sender.
    active_track_names.resize(14);
    active_track_names[13] = L"Zebra - Last.flac";
    active_track_names[5] = L"Alpha - Start.mp3";
    active_track_names[9] = L"Ti\u00ebsto - Adagio.wav";
    auto find_from_master = [&](const wchar_t* needle, int first, UINT flags) {
        NMLVFINDITEMW request{};
        request.hdr.hwndFrom = master;
        request.hdr.idFrom = IDC_METADATA_TRACK_LIST;
        request.hdr.code = LVN_ODFINDITEMW;
        request.iStart = first;
        request.lvfi.flags = flags;
        request.lvfi.psz = needle;
        notified_find_row = -99;
        SendMessageW(dialog, WM_NOTIFY, IDC_METADATA_TRACK_LIST,
                     reinterpret_cast<LPARAM>(&request));
        return notified_find_row;
    };
    check(find_from_master(L"ALPHA", 0, LVFI_STRING) == 1,
          "actual resource-backed owner-data notification searches case-insensitive source name");
    check(find_from_master(L"tie", 0, LVFI_STRING) == -1,
          "search must not silently remove or transliterate accented artist names");
    check(find_from_master(L"zebra", 1, LVFI_STRING) == -1 &&
          find_from_master(L"ZEBRA", 1, LVFI_STRING | LVFI_WRAP) == 0,
          "incremental search respects start position and explicit wrap");
    check(djmeta_foobar::find_native_track_prefix(
              {9, 13}, active_track_names, L"zebra", 0, false) == 1,
          "filtered and sorted view mapping resolves stable original source identity");
    check(djmeta_foobar::find_native_track_prefix(
              {}, active_track_names, L"zebra", 0, true) == -1 &&
          djmeta_foobar::find_native_track_prefix(
              {13}, active_track_names, L"", 0, true) == -1,
          "empty grid and empty needle fail closed");

    // The ListView fires real LVN_ITEMCHANGED notifications as its selection
    // changes. The dialog callback uses the production identity resolver.
    ListView_SetItemState(master, 2, 0, LVIS_SELECTED);
    ListView_SetItemState(master, 2, LVIS_SELECTED, LVIS_SELECTED);
    check(notified_track_identity == 9,
          "real LVN_ITEMCHANGED notification identifies source 9");
    const auto selected = djmeta_foobar::selected_native_view_ids(master, initial_order);
    check(selected == std::vector<std::size_t>({13, 9}),
          "multiple real listview selections resolve to stable track identities");
    const std::vector<std::size_t> sorted_order{9, 13, 5};
    active_track_order = sorted_order;
    rebuilding_master = true;
    djmeta_foobar::restore_native_view_selection(master, sorted_order, selected, 9);
    rebuilding_master = false;
    check(djmeta_foobar::selected_native_view_ids(master, sorted_order) ==
          std::vector<std::size_t>({9, 13}),
          "sort preserves all selected track identities in owner-data listview");
    check((ListView_GetItemState(master, 0, LVIS_FOCUSED) & LVIS_FOCUSED) != 0,
          "focused track survives independently from sorted row identity");
    ListView_SetItemState(master, 2, LVIS_SELECTED, LVIS_SELECTED);
    check(notified_track_identity == 5,
          "real LVN_ITEMCHANGED after sorting resolves new source, not stale view row");
    HWND detail = GetDlgItem(dialog, IDC_METADATA_LIST);
    check(ListView_SetItemCountEx(detail, 3, 0) != FALSE,
          "production virtual proposal grid accepts three rows");
    ListView_SetItemState(detail, 1, LVIS_SELECTED, LVIS_SELECTED);
    check(djmeta_foobar::selected_native_view_ids(detail, {4, 11, 2}) ==
          std::vector<std::size_t>({11}),
          "detail proposal selection resolves sorted proposal identity");

    // Exercise the same shared split-layout function on the actual RC HWNDs.
    auto client_rect = [](HWND parent, HWND child) {
        RECT rect{};
        check(GetWindowRect(child, &rect) != FALSE,
              "physical list bounds available");
        MapWindowPoints(HWND_DESKTOP, parent,
                        reinterpret_cast<POINT*>(&rect), 2);
        return rect;
    };
    const auto original_master = client_rect(dialog, master);
    const auto original_detail = client_rect(dialog, detail);
    const int base_gap = original_detail.left - original_master.right;
    for (int expansion : {120, 241}) {
        const auto projected = djmeta_foobar::review_split_geometry(
            original_master, original_detail, expansion, 80);
        check(projected.detail.left - projected.master.right == base_gap &&
              projected.detail.right - original_detail.right == expansion &&
              projected.master.bottom == projected.detail.bottom,
              "logical master/detail resize shares space and preserves the gap");
        check(djmeta_foobar::apply_review_split_geometry(
                  master, detail, original_master, original_detail,
                  expansion, 80),
              "resize both physical production listview controls");
        const auto physical_master = client_rect(dialog, master);
        const auto physical_detail = client_rect(dialog, detail);
        check(physical_master.left == projected.master.left &&
              physical_master.right == projected.master.right &&
              physical_detail.left == projected.detail.left &&
              physical_detail.right == projected.detail.right &&
              physical_master.bottom == projected.master.bottom &&
              physical_detail.bottom == projected.detail.bottom,
              "physical master/detail controls follow production resize geometry");
    }

    // Real Windows header widths at 100%, 125%, 150%, 200% DPI. This is
    // programmatic column-DPI qualification, not a visual host acceptance.
    for (int dpi : {96, 120, 144, 192}) {
        auto dpi_layout = djmeta::default_review_grid_layout<5>(
            {80, 105, 105, 60, 75});
        check(djmeta::set_review_column_visible(dpi_layout, 3, false),
              "DPI test hides safety column");
        djmeta_foobar::apply_review_grid_controls(detail, dpi_layout, dpi);
        check(ListView_GetColumnWidth(detail, 1) ==
                  MulDiv(dpi_layout.widths[1], dpi, 96) &&
              ListView_GetColumnWidth(detail, 3) == 0,
              "real Windows ListView has DPI-scaled and hidden columns");
        djmeta_foobar::capture_review_grid_controls(detail, dpi_layout, dpi);
        check(dpi_layout.widths[1] == 105 && dpi_layout.widths[3] == 60,
              "DPI capture retains logical width and hidden column width");
    }

    djmeta_foobar::align_native_preview_form(dialog);
    const int rows[][2] = {
        {IDC_METADATA_FILTER_LABEL, IDC_METADATA_FILTER},
        {IDC_METADATA_TRACK_FILTER_LABEL, IDC_METADATA_TRACK_FILTER},
        {IDC_BATCH_ROUTE_LABEL, IDC_BATCH_PROFILE_PICKER},
        {IDC_BATCH_DEST_LABEL, IDC_BATCH_DESTINATION},
        {IDC_BATCH_PATTERN_LABEL, IDC_BATCH_PATTERN},
    };
    for (const auto& row : rows)
        check(centers_match(dialog, row[0], row[1]),
              "labels align to text portion of actual Windows input controls");
    check(GetDlgItem(dialog, IDC_BATCH_APPLY_ALL) &&
          GetDlgItem(dialog, IDC_BATCH_APPLY_SELECTED),
          "preview actions exist in production resource");

    // Precision CUE uses its own native, standalone modeless HWND resource.
    // Verify the real Windows controls (no synthetic UI mocks), including
    // read-only CUE output and retained editable Apple URL entry.
    HWND cue_dialog = CreateDialogParamW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_PRECISION_CUE_PREVIEW), nullptr, test_dialog_proc, 0);
    check(cue_dialog != nullptr, "Precision CUE production dialog instantiation");
    const DWORD cue_style = static_cast<DWORD>(
        GetWindowLongPtrW(cue_dialog, GWL_STYLE));
    check((cue_style & WS_THICKFRAME) && (cue_style & WS_MINIMIZEBOX) &&
          (cue_style & WS_MAXIMIZEBOX) && (cue_style & WS_SYSMENU),
          "Precision CUE standalone dialog has normal resize/minimize/close chrome");
    HWND cue_link = GetDlgItem(cue_dialog, IDC_PRECISION_APPLE_LINK);
    HWND cue_lookup = GetDlgItem(cue_dialog, IDC_PRECISION_LOOKUP);
    HWND cue_text = GetDlgItem(cue_dialog, IDC_PRECISION_PREVIEW_TEXT);
    HWND cue_hint = GetDlgItem(cue_dialog, IDC_PRECISION_SOURCE_HINT);
    check(cue_link && cue_lookup && cue_text && cue_hint,
          "Precision CUE has actual Apple link, lookup, status and preview controls");
    const auto link_style = static_cast<DWORD>(
        GetWindowLongPtrW(cue_link, GWL_STYLE));
    const auto preview_style = static_cast<DWORD>(
        GetWindowLongPtrW(cue_text, GWL_STYLE));
    check((link_style & ES_READONLY) == 0 &&
          (preview_style & ES_READONLY) &&
          (preview_style & ES_MULTILINE) &&
          (preview_style & WS_VSCROLL),
          "Apple URL input is editable, proposed CUE output is strictly read-only");
    check(SetWindowTextW(cue_link,
          L"https://music.apple.com/de/album/synthetic/123456789") != FALSE,
          "native Apple URL input accepts standard Apple Music share link");
    wchar_t cue_link_text[128]{};
    GetWindowTextW(cue_link, cue_link_text, 128);
    check(std::wcscmp(cue_link_text,
          L"https://music.apple.com/de/album/synthetic/123456789") == 0,
          "native Apple URL input preserves user-edited Unicode text");
    check((static_cast<DWORD>(GetWindowLongPtrW(cue_lookup, GWL_STYLE)) &
           WS_DISABLED) == 0,
          "Lookup action is enabled before an explicitly user-initiated request");
    DestroyWindow(cue_dialog);

    DestroyWindow(dialog);
    std::cout << "PASS: Win32 reviewed bindings, master/detail resizing and 96-192 DPI column geometry; "
                 "native preview resource, editable profile, "
                 "virtual master/detail controls and shared label alignment\n";
    return 0;
}
