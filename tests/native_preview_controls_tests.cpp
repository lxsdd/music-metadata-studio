#include "../src/foobar/native_preview_controls.h"
#include "../src/foobar/readonly_network_request.h"
#include "../src/foobar/review_grid_controls.h"
#include "../src/foobar/host_file_probe.h"
#include "../src/foobar/legacy_routing_profiles.h"

#include <commctrl.h>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <vector>
#include <array>
#include <utility>
#include <future>
#include <atomic>
#include <chrono>
#include <thread>
#include <stdexcept>

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
int read_only_worker_notifications = 0;
int query_edit_events = 0;
djmeta_foobar::ReadOnlyContextVersion test_request_context;
INT_PTR CALLBACK test_dialog_proc(HWND dialog, UINT message, WPARAM wp, LPARAM lp) {
    if(message==WM_COMMAND && LOWORD(wp)==IDC_METADATA_MB_QUERY &&
       HIWORD(wp)==EN_CHANGE) {
        ++query_edit_events;
        test_request_context.invalidate();
        return TRUE;
    }
    if(message==djmeta_foobar::kReadOnlyNetworkDoneMessage) {
        ++read_only_worker_notifications;
        return TRUE;
    }
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
    if (GetComboBoxInfo(input, &info)) {
        b=info.rcItem;
        MapWindowPoints(input,HWND_DESKTOP,
                        reinterpret_cast<POINT*>(&b),2);
    }
    const int offset = (a.top + a.bottom) - (b.top + b.bottom);
    return offset >= -2 && offset <= 2;
}
}

int main() {
    // Pure read-only target-state stability contract. No filesystem access
    // or real user audio required by these negative tests.
    using djmeta_foobar::HostFileObservation;
    using djmeta_foobar::HostFileState;
    using djmeta_foobar::stable_raw_target_observation;
    HostFileObservation missing;
    missing.state=HostFileState::Missing;
    missing.canonical_path="file:///C:/sandbox/raw-missing.mp3";
    check(stable_raw_target_observation(missing,missing),
          "stable missing raw target remains an advisory missing observation");
    auto created=missing;
    created.state=HostFileState::ExistingFile;
    created.host_physical_key="os-file-123";
    created.source_guard="os-file-123:time-v1";
    check(!stable_raw_target_observation(missing,created),
          "created formerly missing target invalidates preview before publication");
    auto replaced=created;
    replaced.host_physical_key="os-file-456";
    check(!stable_raw_target_observation(created,replaced),
          "physical target replacement invalidates advisory observation");
    replaced=created;
    replaced.source_guard="os-file-123:time-v2";
    check(!stable_raw_target_observation(created,replaced),
          "same target physical ID with new attributes invalidates observation");
    replaced=created;
    replaced.canonical_path="file:///C:/sandbox/other.mp3";
    check(!stable_raw_target_observation(created,replaced),
          "re-resolved raw candidate cannot silently change canonical path");
    check(stable_raw_target_observation(created,created),
          "identical existing physical target remains stable");
    HostFileObservation unqualified;
    unqualified.state=HostFileState::Unqualified;
    unqualified.canonical_path="file:///C:/sandbox/unsupported.mp3";
    unqualified.detail="Unsupported filesystem";
    check(stable_raw_target_observation(unqualified,unqualified),
          "unchanged unsupported filesystem stays advisory and unqualified");
    auto changed_reason=unqualified;
    changed_reason.detail="Permission changed";
    check(!stable_raw_target_observation(unqualified,changed_reason),
          "unqualified target reason change blocks mixed inspection");
    HostFileObservation no_canonical=missing;
    no_canonical.canonical_path.clear();
    check(!stable_raw_target_observation(no_canonical,no_canonical),
          "missing target without canonical host locator cannot be treated as stable");

    INITCOMMONCONTROLSEX controls{sizeof(controls),
                                  ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES};
    check(InitCommonControlsEx(&controls) != FALSE,
          "native listview and tabs initialization");
    HWND dialog = CreateDialogParamW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_BATCH_PREVIEW), nullptr, test_dialog_proc, 0);
    check(dialog != nullptr, "actual production dialog template instantiation");
    HWND workspace_prefs = CreateDialogParamW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_WORKSPACE_PREFERENCES), dialog, test_dialog_proc, 0);
    check(workspace_prefs != nullptr,
          "native Workspace Preferences child page instantiates");
    for (const int id : {IDC_DEFAULT_START_TAB, IDC_DEFAULT_ONLINE_PROVIDER,
                         IDC_DEFAULT_ROUTE_PROFILE}) {
        const HWND selector = GetDlgItem(workspace_prefs, id);
        check(selector != nullptr &&
              (GetWindowLongPtrW(selector, GWL_STYLE) & 0x3u) == CBS_DROPDOWNLIST,
              "Workspace default selector is a real native drop-down");
    }
    check(GetDlgItem(workspace_prefs, IDC_DEFAULT_SHOW_WHITESPACE) != nullptr,
          "Workspace Preferences include the initial whitespace switch");
    DestroyWindow(workspace_prefs);
    // Test real RC-derived HWND styles, not a fabricated lookalike dialog.
    const DWORD workspace_style=static_cast<DWORD>(GetWindowLongPtrW(dialog,GWL_STYLE));
    const DWORD workspace_exstyle=static_cast<DWORD>(GetWindowLongPtrW(dialog,GWL_EXSTYLE));
    check((workspace_style & WS_MINIMIZEBOX) != 0 &&
          (workspace_style & WS_MAXIMIZEBOX) != 0 &&
          (workspace_style & WS_THICKFRAME) != 0 &&
          (workspace_style & WS_SYSMENU) != 0 &&
          (workspace_style & DS_MODALFRAME) == 0,
          "production workspace is resizable, minimizable and not a modal-frame dialog");
    check((workspace_exstyle & WS_EX_APPWINDOW) != 0 &&
          GetWindow(dialog,GW_OWNER)==nullptr,
          "workspace has independent taskbar/Alt-Tab identity and no forced owner");
    HWND refresh=GetDlgItem(dialog,IDC_BATCH_REFRESH);
    HWND help=GetDlgItem(dialog,IDC_BATCH_HELP);
    HWND guide=GetDlgItem(dialog,IDC_BATCH_GUIDE);
    check(refresh!=nullptr && IsWindowEnabled(refresh) && help && guide,
          "production resource offers Refresh, Help and stage guidance");
    wchar_t caption[96]{};
    GetWindowTextW(GetDlgItem(dialog,IDC_METADATA_MB_SEARCH),caption,96);
    check(std::wcscmp(caption,L"Search MusicBrainz")==0,
          "search button names its online source");
    HWND provider=GetDlgItem(dialog,IDC_METADATA_ONLINE_PROVIDER);
    HWND token=GetDlgItem(dialog,IDC_METADATA_DISCOGS_TOKEN);
    HWND token_label=GetDlgItem(dialog,IDC_METADATA_DISCOGS_TOKEN_LABEL);
    check(provider && token && token_label &&
          (GetWindowLongPtrW(provider,GWL_STYLE)&0x3u)==CBS_DROPDOWNLIST &&
          (GetWindowLongPtrW(token,GWL_STYLE)&ES_PASSWORD)!=0,
          "production provider selector and non-echoing Discogs token field exist");
    // Provider 3 deliberately reuses native controls; it must not require
    // a second modal window or an embedded browser.
    check(SendMessageW(provider,CB_ADDSTRING,0,
          reinterpret_cast<LPARAM>(L"MusicBrainz"))>=0 &&
          SendMessageW(provider,CB_ADDSTRING,0,
          reinterpret_cast<LPARAM>(L"Discogs (token)"))>=0 &&
          SendMessageW(provider,CB_ADDSTRING,0,
          reinterpret_cast<LPARAM>(L"Beatport + Crates"))>=0 &&
          SendMessageW(provider,CB_GETCOUNT,0,0)==3 &&
          SendMessageW(provider,CB_SETCURSEL,2,0)==2,
          "native provider selector supports distinct Beatport offline mode");
    wchar_t beatport_provider[96]{};
    SendMessageW(provider,CB_GETLBTEXT,2,
        reinterpret_cast<LPARAM>(beatport_provider));
    check(std::wcscmp(beatport_provider,L"Beatport + Crates")==0,
          "Beatport mode is clearly labelled offline rather than misleadingly live");
    RECT provider_rect{}, token_rect{}, query_rect{}, track_rect{};
    check(GetWindowRect(provider,&provider_rect) &&
          GetWindowRect(token,&token_rect) &&
          GetWindowRect(GetDlgItem(dialog,IDC_METADATA_MB_QUERY),&query_rect) &&
          GetWindowRect(GetDlgItem(dialog,IDC_METADATA_TRACK_LIST),&track_rect) &&
          provider_rect.right<token_rect.left &&
          provider_rect.top<query_rect.top &&
          token_rect.bottom<=query_rect.top &&
          query_rect.bottom<track_rect.top,
          "provider/token row and search row are distinct and never cover tracks");
    GetWindowTextW(GetDlgItem(dialog,IDC_METADATA_MB_LOAD_RELEASE),caption,96);
    check(std::wcscmp(caption,L"Inspect release tracks")==0,
          "release action is clearly read-only inspection");
    RECT guide_rect{},search_rect{};
    check(GetWindowRect(guide,&guide_rect) &&
          GetWindowRect(GetDlgItem(dialog,IDC_METADATA_MB_QUERY),&search_rect) &&
          guide_rect.bottom<=search_rect.top,
          "stage guidance sits above search controls");
    HWND tip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,
          WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,
          CW_USEDEFAULT,CW_USEDEFAULT,dialog,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(tip!=nullptr,"native tooltip window can be created");
    static wchar_t hover_text[]=L"Refresh reloads captured tracks; no files change.";
    TOOLINFOW tip_info{};
    tip_info.cbSize=TTTOOLINFOW_V2_SIZE;
    tip_info.hwnd=dialog;
    tip_info.uFlags=TTF_SUBCLASS|TTF_IDISHWND;
    tip_info.uId=reinterpret_cast<UINT_PTR>(refresh);
    tip_info.lpszText=hover_text;
    check(SendMessageW(tip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tip_info))!=FALSE,
          "native Refresh button supports hover explanation");
    DestroyWindow(tip);
    RECT refresh_rect{}, hint_rect{}, footer_rect{};
    check(GetWindowRect(refresh,&refresh_rect) &&
          GetWindowRect(GetDlgItem(dialog,IDC_BATCH_HINT),&hint_rect) &&
          GetWindowRect(GetDlgItem(dialog,IDC_METADATA_ACCEPT),&footer_rect) &&
          refresh_rect.bottom < hint_rect.top &&
          hint_rect.bottom < footer_rect.top,
          "refresh, two-line status and action row are distinct and non-overlapping");
    check(!djmeta_foobar::native_workspace_should_relayout_on_size(
              SIZE_MINIMIZED,0,0) &&
          !djmeta_foobar::native_workspace_should_relayout_on_size(
              SIZE_RESTORED,0,700) &&
          !djmeta_foobar::native_workspace_should_relayout_on_size(
              SIZE_RESTORED,900,0) &&
          djmeta_foobar::native_workspace_should_relayout_on_size(
              SIZE_RESTORED,900,700) &&
          djmeta_foobar::native_workspace_should_relayout_on_size(
              SIZE_MAXIMIZED,900,700),
          "minimized or empty WM_SIZE cannot realign native controls");
    // Regression for large foobar libraries: Alt-Tab only validates the
    // selected original source, but every explicitly consequential action
    // revalidates all 54,129 sources before acting on stale data.
    constexpr std::size_t kLargeBatch=54129;
    std::size_t activation_checks=0, consequential_checks=0;
    for(std::size_t i=0;i<kLargeBatch;++i) {
        if(djmeta_foobar::native_workspace_should_validate_entry(
                i,29017,kLargeBatch,false))
            ++activation_checks;
        if(djmeta_foobar::native_workspace_should_validate_entry(
                i,29017,kLargeBatch,true))
            ++consequential_checks;
    }
    check(activation_checks==1 && consequential_checks==kLargeBatch,
          "passive focus avoids full-library reads; explicit actions scan all sources");
    check(djmeta_foobar::native_workspace_should_validate_entry(
              0,kLargeBatch,kLargeBatch,false) &&
          djmeta_foobar::native_workspace_should_validate_entry(
              kLargeBatch-1,kLargeBatch,kLargeBatch,false),
          "invalid selected source index fails closed to full batch revalidation");
    check(!djmeta_foobar::native_workspace_should_validate_entry(
              0,3,10,false) &&
          djmeta_foobar::native_workspace_should_validate_entry(
              3,3,10,false),
          "passive selection check uses stable source index, not display row");

    // The actual shipping resource controls must stay disabled after a stale
    // source is detected, including when candidate/UI refresh code recomputes
    // normal eligibility or an asynchronous provider response completes.
    for(const int id : {IDC_METADATA_MB_SEARCH,IDC_METADATA_MB_LOAD_RELEASE,
                        IDC_METADATA_IMPORT_CANDIDATE,IDC_METADATA_INSPECT_CUE,
                        IDC_ONLINE_INCLUDE_FIELDS,IDC_ONLINE_DATE_TARGET,
                        IDC_ONLINE_WRITE_SELECTED}) {
        HWND action=GetDlgItem(dialog,id);
        check(action!=nullptr,"stale-gated action exists in actual dialog resource");
        const bool previously_enabled=IsWindowEnabled(action)!=FALSE;
        EnableWindow(action,djmeta_foobar::native_workspace_action_ready(true,true));
        check(IsWindowEnabled(action)==FALSE,
              "stale snapshot cannot reenable production action control");
        EnableWindow(action,djmeta_foobar::native_workspace_action_ready(false,true));
        check(IsWindowEnabled(action)!=FALSE,
              "fresh verified action can be reenabled");
        EnableWindow(action,djmeta_foobar::native_workspace_action_ready(false,false));
        check(IsWindowEnabled(action)==FALSE,
              "ineligible action stays disabled even if snapshot is fresh");
        EnableWindow(action,previously_enabled?TRUE:FALSE);
    }
    check(std::wcscmp(djmeta_foobar::native_workspace_display_hint(
              true,L"Old candidate found"),djmeta_foobar::kNativeStaleWorkspaceHint)==0 &&
          std::wcscmp(djmeta_foobar::native_workspace_display_hint(
              false,L"Old candidate found"),L"Old candidate found")==0,
          "stale warning survives tab/candidate review text refresh");
    HWND workspace_status=GetDlgItem(dialog,IDC_BATCH_HINT);
    check(workspace_status!=nullptr,
          "production workspace hint control is present");
    wchar_t previous_status[512]{};
    GetWindowTextW(workspace_status,previous_status,512);
    SetDlgItemTextW(dialog,IDC_BATCH_HINT,
        djmeta_foobar::native_workspace_display_hint(
            true,L"Provider changed: select a result"));
    wchar_t displayed_status[512]{};
    GetWindowTextW(workspace_status,displayed_status,512);
    check(std::wcscmp(displayed_status,
          djmeta_foobar::kNativeStaleWorkspaceHint)==0,
          "real HWND refuses to hide stale warning under provider/date hint");
    SetDlgItemTextW(dialog,IDC_BATCH_HINT,
        djmeta_foobar::native_workspace_display_hint(
            false,L"Provider changed: select a result"));
    GetWindowTextW(workspace_status,displayed_status,512);
    check(std::wcscmp(displayed_status,
          L"Provider changed: select a result")==0,
          "fresh snapshot restores ordinary provider guidance");
    SetDlgItemTextW(dialog,IDC_BATCH_HINT,previous_status);
    check(djmeta_foobar::native_workspace_dismisses_tooltip(
              WM_CLOSE,0) &&
          djmeta_foobar::native_workspace_dismisses_tooltip(
              WM_SHOWWINDOW,FALSE) &&
          djmeta_foobar::native_workspace_dismisses_tooltip(
              WM_ACTIVATE,WA_INACTIVE) &&
          djmeta_foobar::native_workspace_dismisses_tooltip(
              WM_NCACTIVATE,FALSE) &&
          djmeta_foobar::native_workspace_dismisses_tooltip(
              WM_CANCELMODE,0) &&
          djmeta_foobar::native_workspace_dismisses_tooltip(
              WM_ENTERSIZEMOVE,0) &&
          djmeta_foobar::native_workspace_dismisses_tooltip(
              WM_COMMAND,MAKEWPARAM(IDC_BATCH_HELP,BN_CLICKED)) &&
          djmeta_foobar::native_workspace_dismisses_tooltip(
              WM_COMMAND,MAKEWPARAM(IDC_METADATA_ONLINE_PROVIDER,CBN_SELCHANGE)),
          "native tooltip always closes before hidden-inflight shutdown "
          "and on loss of active interaction");
    check(!djmeta_foobar::native_workspace_dismisses_tooltip(
              WM_SHOWWINDOW,TRUE) &&
          !djmeta_foobar::native_workspace_dismisses_tooltip(
              WM_ACTIVATE,WA_ACTIVE) &&
          !djmeta_foobar::native_workspace_dismisses_tooltip(
              WM_COMMAND,MAKEWPARAM(IDC_METADATA_MB_QUERY,EN_CHANGE)) &&
          !djmeta_foobar::native_workspace_dismisses_tooltip(
              WM_PAINT,0),
          "normal typing, painting and window reactivation cannot "
          "prematurely dismiss an explanatory tooltip");
    const auto mb_help=djmeta_foobar::native_search_action_tooltip(false,false);
    const auto discogs_help=djmeta_foobar::native_search_action_tooltip(true,false);
    const auto beatport_help=djmeta_foobar::native_search_action_tooltip(false,true);
    check(std::wcsstr(mb_help,L"MusicBrainz")!=nullptr &&
          std::wcsstr(mb_help,L"Beatport")==nullptr &&
          std::wcsstr(discogs_help,L"Discogs")!=nullptr &&
          std::wcsstr(discogs_help,L"MusicBrainz")==nullptr &&
          std::wcsstr(beatport_help,L"Beatport")!=nullptr &&
          std::wcsstr(beatport_help,L"Discogs")==nullptr &&
          djmeta_foobar::native_search_action_tooltip(true,true)==beatport_help,
          "search action tooltips follow selected MusicBrainz/Discogs/"
          "Beatport transport rather than claiming Beatport for every source");
    ShowWindow(dialog,SW_SHOWNA);
    ShowWindow(dialog,SW_MINIMIZE);
    check(IsIconic(dialog)!=FALSE,"native workspace minimizes independently");
    ShowWindow(dialog,SW_RESTORE);
    check(IsIconic(dialog)==FALSE,"native workspace restores from taskbar");
    ShowWindow(dialog,SW_MAXIMIZE);
    check(IsZoomed(dialog)!=FALSE,"native workspace maximizes");
    ShowWindow(dialog,SW_RESTORE);
    check(IsZoomed(dialog)==FALSE,"native workspace restores maximized state");
    SetFocus(GetDlgItem(dialog,IDC_METADATA_MB_QUERY));
    MSG navigation{};
    navigation.hwnd=GetDlgItem(dialog,IDC_METADATA_MB_QUERY);
    navigation.message=WM_KEYDOWN;
    navigation.wParam=VK_TAB;
    check(IsDialogMessageW(dialog,&navigation)!=FALSE,
          "Win32 modeless IsDialogMessage consumes Tab for native dialog navigation");


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

    HWND mb_query = GetDlgItem(dialog, IDC_METADATA_MB_QUERY);
    HWND mb_search = GetDlgItem(dialog, IDC_METADATA_MB_SEARCH);
    HWND mb_load = GetDlgItem(dialog, IDC_METADATA_MB_LOAD_RELEASE);
    check(mb_query != nullptr && mb_search != nullptr && mb_load != nullptr,
          "production official MusicBrainz online search/query and release controls exist");
    RECT mb_query_rect{}, mb_search_rect{}, mb_load_rect{};
    check(GetWindowRect(mb_query, &mb_query_rect) != FALSE &&
          GetWindowRect(mb_search, &mb_search_rect) != FALSE &&
          GetWindowRect(mb_load, &mb_load_rect) != FALSE &&
          mb_query_rect.right <= mb_search_rect.left &&
          mb_load_rect.right > mb_load_rect.left,
          "MusicBrainz search action does not overlay text query");

    HWND online_details=GetDlgItem(dialog,IDC_METADATA_MB_DETAILS);
    HWND online_detail_label=GetDlgItem(dialog,IDC_METADATA_MB_DETAIL_LABEL);
    check(online_details && online_detail_label,
          "new production MusicBrainz nested field pane and label exist");
    const DWORD detail_style=static_cast<DWORD>(GetWindowLongPtrW(
        online_details,GWL_STYLE));
    check((detail_style & LVS_REPORT)==LVS_REPORT &&
          (detail_style & LVS_OWNERDATA)==0,
          "selected candidate details have normal independent native ListView rows");
    for (int i=0;i<4;++i) {
        LVCOLUMNW column{};
        column.mask=LVCF_TEXT|LVCF_WIDTH;
        column.cx=100;
        std::wstring title=(i==0?L"Field":i==1?L"Original":i==2?L"Suggested":L"Status");
        column.pszText=title.data();
        check(static_cast<int>(SendMessageW(online_details,LVM_INSERTCOLUMNW,
                         i,reinterpret_cast<LPARAM>(&column)))==i,
              "production online detail column created");
    }
    check(djmeta_foobar::insert_native_preview_detail_row(online_details,0,
        L"ARTIST",L"(missing)",L"Daft Punk",L"Review"),
        "native online detail renderer writes all subitems");
    wchar_t detail_value[128]{};
    const wchar_t* expected[]={L"ARTIST",L"(missing)",L"Daft Punk",L"Review"};
    for(int i=0;i<4;++i) {
        detail_value[0]=0;
        LVITEMW fetched{};
        fetched.iSubItem=i;
        fetched.pszText=detail_value;
        fetched.cchTextMax=128;
        SendMessageW(online_details,LVM_GETITEMTEXTW,0,
                     reinterpret_cast<LPARAM>(&fetched));
        check(std::wcscmp(detail_value,expected[i])==0,
              "actual native control preserves each of the four online detail cells");
    }
    ListView_DeleteAllItems(online_details);
    for (int dpi : {96,120,144,192}) {
        for (int height : {310,510,790}) {
            RECT browser_frame{500,120,1120,120+height};
            const auto pane=djmeta_foobar::musicbrainz_pane_bounds(browser_frame,dpi);
            check(pane.hits.top==browser_frame.top &&
                  pane.hits.bottom<pane.caption.top &&
                  pane.caption.bottom+3<=pane.details.top &&
                  pane.details.bottom==browser_frame.bottom &&
                  pane.details.top<pane.details.bottom &&
                  pane.caption.left==pane.hits.left &&
                  pane.details.right==pane.hits.right,
                  "DPI-aware selected-candidate label never collides with list header");
        }
    }
    const std::vector<std::wstring> provider_ranked{
        L"Zeta (provider rank 1)",L"Alpha (provider rank 2)",L"Beta (provider rank 3)"};
    const auto native_order=[&](int column,bool descending) {
        return djmeta_foobar::native_candidate_view_order(
            provider_ranked.size(),column,descending,
            [&](std::size_t i,int) {return provider_ranked.at(i);});
    };
    check(native_order(-1,false)==std::vector<std::size_t>({0,1,2}) &&
          native_order(0,false)==std::vector<std::size_t>({1,2,0}) &&
          native_order(0,true)==std::vector<std::size_t>({0,2,1}),
          "official API relevance order preserved until explicit column sort");
    check(djmeta_foobar::native_track_activation_starts_online_search(0,false) &&
          !djmeta_foobar::native_track_activation_starts_online_search(1,false) &&
          !djmeta_foobar::native_track_activation_starts_online_search(2,false) &&
          !djmeta_foobar::native_track_activation_starts_online_search(-1,false) &&
          !djmeta_foobar::native_track_activation_starts_online_search(0,true) &&
          !djmeta_foobar::native_track_activation_starts_online_search(1,true),
          "only Find-tab track activation authorizes an online search, never "
          "cross-tab navigation or offline Beatport browser handoff");
    const std::vector<std::string> exact_ids{
        "release:38012673", "release:37332711"};
    check(djmeta_foobar::native_release_loadable(
              true,false,true,false,"release:38012673",exact_ids) &&
          !djmeta_foobar::native_release_loadable(
              true,false,true,false,"master:38012673",exact_ids) &&
          !djmeta_foobar::native_release_loadable(
              true,false,true,false,"release:999",exact_ids) &&
          !djmeta_foobar::native_release_loadable(
              true,false,true,true,"release:38012673",exact_ids) &&
          !djmeta_foobar::native_release_loadable(
              true,false,false,false,"release:38012673",exact_ids) &&
          !djmeta_foobar::native_release_loadable(
              true,true,true,false,"release:38012673",exact_ids),
          "native release action rejects masters, unrelated, pending, stale and loaded rows");
    check(djmeta_foobar::native_selected_release_activation_row(
        NM_DBLCLK,3,3,true,true)==3 &&
          djmeta_foobar::native_selected_release_activation_row(
        LVN_KEYDOWN,-1,3,false,true,VK_RETURN)==3 &&
          djmeta_foobar::native_selected_release_activation_row(
        NM_DBLCLK,2,3,true,true)==2 &&
          djmeta_foobar::native_selected_release_activation_row(
        LVN_KEYDOWN,-1,2,false,true,VK_RETURN)==2,
        "native double-click/Enter uses its selected target even when a lower "
        "row precedes it in a multi-selection");
    check(djmeta_foobar::native_selected_release_activation_row(
        NM_DBLCLK,-1,3,false,true)==-1 &&
          djmeta_foobar::native_selected_release_activation_row(
        NM_DBLCLK,2,3,false,true)==-1 &&
          djmeta_foobar::native_selected_release_activation_row(
        NM_DBLCLK,3,-1,false,false)==-1 &&
          djmeta_foobar::native_selected_release_activation_row(
        LVN_KEYDOWN,-1,3,false,false,VK_RETURN)==-1 &&
          djmeta_foobar::native_selected_release_activation_row(
        LVN_KEYDOWN,-1,3,false,true,VK_SPACE)==-1 &&
          djmeta_foobar::native_selected_release_activation_row(
        LVN_ITEMACTIVATE,3,3,true,true)==-1,
        "empty-space, unselected, unfocused, unrelated-key and duplicate "
        "activation notifications are refused");
    check(djmeta_foobar::may_activate_musicbrainz_release(
        NM_DBLCLK,true,true,false,true) &&
          djmeta_foobar::may_activate_musicbrainz_release(
        LVN_KEYDOWN,true,true,false,true,VK_RETURN),
        "double-click or Enter can load a verified MusicBrainz release");
    check(!djmeta_foobar::may_activate_musicbrainz_release(
        NM_DBLCLK,true,false,false,true) &&
          !djmeta_foobar::may_activate_musicbrainz_release(
        NM_DBLCLK,true,true,true,true) &&
          !djmeta_foobar::may_activate_musicbrainz_release(
        NM_DBLCLK,true,true,false,false) &&
          !djmeta_foobar::may_activate_musicbrainz_release(
        LVN_ITEMACTIVATE,true,true,false,true) &&
          !djmeta_foobar::may_activate_musicbrainz_release(
        LVN_KEYDOWN,true,true,false,true,VK_SPACE),
        "release activation refuses recording, loaded, stale and double-delivered events");
    HWND cue_inspect = GetDlgItem(dialog, IDC_METADATA_INSPECT_CUE);
    check(cue_inspect != nullptr,
          "production read-only CUE inspect button exists in real dialog resource");
    RECT cue_inspect_rect{};
    check(GetWindowRect(cue_inspect, &cue_inspect_rect) != FALSE &&
          cue_inspect_rect.right > cue_inspect_rect.left,
          "read-only CUE inspect button is visible-sized");

    HWND candidate_import = GetDlgItem(dialog, IDC_METADATA_IMPORT_CANDIDATE);
    check(candidate_import != nullptr,
          "production candidate comparison clipboard import button exists");
    RECT candidate_rect{};
    RECT candidate_client_bounds{};
    check(GetWindowRect(candidate_import, &candidate_rect) != FALSE &&
          GetClientRect(dialog, &candidate_client_bounds) != FALSE,
          "candidate import button geometry readable");
    const auto visual_center2=[](const RECT& r) { return r.top+r.bottom; };
    const int baseline=visual_center2(mb_query_rect);
    const auto close_center=[](int a,int b) {
        return a>=b ? a-b<=2 : b-a<=2;
    };
    check(close_center(baseline,visual_center2(mb_search_rect)) &&
          close_center(baseline,visual_center2(cue_inspect_rect)) &&
          close_center(baseline,visual_center2(candidate_rect)),
          "search input, search, CUE and clipboard controls share one toolbar centerline");
    MapWindowPoints(HWND_DESKTOP, dialog,
                    reinterpret_cast<POINT*>(&candidate_rect), 2);
    check(candidate_rect.left >= 0 &&
          candidate_rect.right <= candidate_client_bounds.right &&
          candidate_rect.top >= 0 && candidate_rect.bottom <= candidate_client_bounds.bottom,
          "candidate import button fits minimum dialog size");

    // Exercise the same atomic resize adapter as the production preview
    // using its genuine dialog resource.  New toolbar buttons must not
    // intersect or paint outside the expanded client rect.
    auto bounds_in_dialog = [&](HWND control) {
        RECT r{};
        check(GetWindowRect(control, &r) != FALSE,
              "real native child bounds are available");
        MapWindowPoints(HWND_DESKTOP, dialog, reinterpret_cast<POINT*>(&r), 2);
        return r;
    };
    RECT initial_window{};
    check(GetWindowRect(dialog, &initial_window) != FALSE,
          "initial production dialog window bounds readable");
    const int window_width = initial_window.right - initial_window.left;
    const int window_height = initial_window.bottom - initial_window.top;
    std::vector<djmeta_foobar::NativePreviewResizeChild> movable;
    for (const int id : {
        IDC_METADATA_INSPECT_CUE, IDC_METADATA_IMPORT_CANDIDATE,
        IDC_METADATA_TRACK_LIST, IDC_METADATA_LIST
    }) {
        djmeta_foobar::NativePreviewResizeChild control;
        control.window = GetDlgItem(dialog, id);
        check(control.window != nullptr, "resource resize target exists");
        control.original = bounds_in_dialog(control.window);
        control.shift_right = id == IDC_METADATA_INSPECT_CUE ||
                              id == IDC_METADATA_IMPORT_CANDIDATE;
        movable.push_back(control);
    }
    // Windows CI can impose a small virtual desktop. SetWindowPos() can
    // succeed but clamp the requested size to the OS maximum track size.
    // Always feed the ACTUAL WM_SIZE client delta to production layout code.
    RECT initial_client{};
    GetClientRect(dialog, &initial_client);
    constexpr int requested_extra_width = 310;
    constexpr int requested_extra_height = 180;
    check(SetWindowPos(dialog, nullptr, 0, 0,
            window_width + requested_extra_width,
            window_height + requested_extra_height,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
          "native dialog accepts Windows resize request");
    RECT expanded_client{};
    GetClientRect(dialog, &expanded_client);
    const int extra_width = expanded_client.right - initial_client.right;
    const int extra_height = expanded_client.bottom - initial_client.bottom;
    check(extra_width >= 0 && extra_height >= 0,
          "actual resize never unexpectedly reduces the available client area");
    check(djmeta_foobar::apply_native_preview_resize(movable,
            GetDlgItem(dialog, IDC_METADATA_TRACK_LIST),
            GetDlgItem(dialog, IDC_METADATA_LIST),
            extra_width, extra_height),
          "shared production atomic resize uses actual Win32 client change");
    RECT inspect_after = bounds_in_dialog(cue_inspect);
    RECT import_after = bounds_in_dialog(candidate_import);
    const bool actions_inside =
        inspect_after.right + 8 <= import_after.left &&
        import_after.right <= expanded_client.right &&
        inspect_after.left > 0 && import_after.top >= 0 &&
        import_after.bottom <= expanded_client.bottom;
    if (!actions_inside) {
        std::cerr << "RESIZE_GEOMETRY: inspect=(" << inspect_after.left << ","
                  << inspect_after.top << "," << inspect_after.right << ","
                  << inspect_after.bottom << ") import=(" << import_after.left
                  << "," << import_after.top << "," << import_after.right
                  << "," << import_after.bottom << ") client=("
                  << expanded_client.right << "," << expanded_client.bottom
                  << ") width_delta=" << extra_width << "\n";
    }
    check(actions_inside,
          "resized buttons stay separate and inside the native client area");
    check(inspect_after.left == movable[0].original.left + extra_width &&
          import_after.left == movable[1].original.left + extra_width,
          "both actions remain aligned to right edge with stable spacing");
    RECT master_after = bounds_in_dialog(GetDlgItem(dialog, IDC_METADATA_TRACK_LIST));
    RECT detail_after = bounds_in_dialog(GetDlgItem(dialog, IDC_METADATA_LIST));
    check(master_after.right < detail_after.left &&
          detail_after.right <= expanded_client.right &&
          master_after.bottom == movable[2].original.bottom + extra_height &&
          detail_after.bottom == movable[3].original.bottom + extra_height,
          "resized two-pane tables retain separation without child overlay");
    check(SetWindowPos(dialog, nullptr, 0, 0, window_width, window_height,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE &&
          djmeta_foobar::apply_native_preview_resize(movable,
              GetDlgItem(dialog, IDC_METADATA_TRACK_LIST),
              GetDlgItem(dialog, IDC_METADATA_LIST), 0, 0),
          "shrinking back to initial native geometry succeeds");
    const RECT inspect_restored = bounds_in_dialog(cue_inspect);
    const RECT import_restored = bounds_in_dialog(candidate_import);
    check(inspect_restored.left == movable[0].original.left &&
          import_restored.left == movable[1].original.left &&
          inspect_restored.right + 8 <= import_restored.left,
          "no cumulative resize drift or overlapping paint region");
    const DWORD dialog_style = static_cast<DWORD>(
        GetWindowLongPtrW(dialog, GWL_STYLE));
    check((dialog_style & WS_CLIPCHILDREN) != 0 &&
          (dialog_style & WS_CLIPSIBLINGS) != 0 &&
          (dialog_style & WS_MAXIMIZEBOX) != 0 &&
          (dialog_style & WS_THICKFRAME) != 0,
          "resizable production dialog supports native maximize/restore and clean clipping");

    // Text baselines of both editable action rows share a single native
    // layout routine. Exercise the actual font/resource HWNDs and verify that
    // repeated recomputation and moving the entire dialog cannot accumulate
    // displacement or send the input offscreen.
    RECT alignment_client{};
    check(GetClientRect(dialog,&alignment_client)!=FALSE,
          "native client bounds for edit alignment");
    djmeta_foobar::align_native_preview_form(dialog);
    const auto action_alignment = [&]() {
        for (auto pair : {
                std::pair{IDC_METADATA_MB_QUERY, IDC_METADATA_MB_SEARCH},
                std::pair{IDC_METADATA_MANUAL_INPUT, IDC_METADATA_USE_VALUE}}) {
            const RECT input = bounds_in_dialog(GetDlgItem(dialog,pair.first));
            const RECT button = bounds_in_dialog(GetDlgItem(dialog,pair.second));
            const int midpoint_delta = (input.top+input.bottom) -
                                       (button.top+button.bottom);
            check(midpoint_delta >= -6 && midpoint_delta <= 6 &&
                  input.bottom-input.top <= button.bottom-button.top &&
                  input.left >= 0 && input.right <= alignment_client.right &&
                  input.top >= 0 && input.bottom <= alignment_client.bottom,
                  "real font-measured Search and manual action rows align without clipping");
        }
    };
    RECT move_before{};
    check(GetWindowRect(dialog,&move_before)!=FALSE,"native placement for alignment test");
    const RECT original_query_aligned=bounds_in_dialog(
        GetDlgItem(dialog,IDC_METADATA_MB_QUERY));
    const RECT original_manual_aligned=bounds_in_dialog(
        GetDlgItem(dialog,IDC_METADATA_MANUAL_INPUT));
    for (int n=0;n<6;++n) djmeta_foobar::align_native_preview_form(dialog);
    action_alignment();
    const RECT query_aligned_again=bounds_in_dialog(GetDlgItem(dialog,IDC_METADATA_MB_QUERY));
    const RECT manual_aligned_again=bounds_in_dialog(GetDlgItem(dialog,IDC_METADATA_MANUAL_INPUT));
    check(EqualRect(&original_query_aligned,&query_aligned_again)!=FALSE &&
          EqualRect(&original_manual_aligned,&manual_aligned_again)!=FALSE,
          "optical native edit alignment is idempotent");
    check(SetWindowPos(dialog,nullptr,move_before.left+101,move_before.top+67,0,0,
            SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,
          "dialog can move before font alignment");
    djmeta_foobar::align_native_preview_form(dialog);
    action_alignment();
    const RECT query_after_move=bounds_in_dialog(GetDlgItem(dialog,IDC_METADATA_MB_QUERY));
    const RECT manual_after_move=bounds_in_dialog(GetDlgItem(dialog,IDC_METADATA_MANUAL_INPUT));
    check(EqualRect(&original_query_aligned,&query_after_move)!=FALSE &&
          EqualRect(&original_manual_aligned,&manual_after_move)!=FALSE,
          "screen-to-client conversions do not move text actions after window relocation");
    check(SetWindowPos(dialog,nullptr,move_before.left,move_before.top,0,0,
            SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,
          "dialog restores initial position after alignment test");

    // Three read-only online field decisions are physically separate from
    // the native Close button, and have actual resource-backed HWNDs.
    RECT online_client{};
    check(GetClientRect(dialog,&online_client)!=FALSE,
          "client bounds for native online edit controls");
    RECT previous_action{};
    bool first_online_action=true;
    for (const int id:{IDC_ONLINE_INCLUDE_FIELDS,IDC_ONLINE_IGNORE_FIELDS,
                       IDC_ONLINE_RESET_FIELDS}) {
        const HWND button=GetDlgItem(dialog,id);
        check(button!=nullptr,"exact-edition native preview action exists");
        const RECT bounds=bounds_in_dialog(button);
        check(bounds.left>=0 && bounds.right<=online_client.right &&
              bounds.top>=0 && bounds.bottom<=online_client.bottom,
              "field preview action fits the minimum native dialog");
        if(!first_online_action)
            check(previous_action.right+3<=bounds.left &&
                  previous_action.top==bounds.top,
                  "field preview controls have no overlap or row drift");
        previous_action=bounds;
        first_online_action=false;
    }
    const RECT close_online=bounds_in_dialog(GetDlgItem(dialog,IDCANCEL));
    check(previous_action.right+3<=close_online.left,
          "reset online preview never overlaps Close");

    const HWND date_combo=GetDlgItem(dialog,IDC_ONLINE_DATE_TARGET);
    const HWND date_label=GetDlgItem(dialog,IDC_ONLINE_DATE_TARGET_LABEL);
    check(date_combo && date_label &&
          (GetWindowLongPtrW(date_combo,GWL_STYLE)&0x3u)==CBS_DROPDOWNLIST,
          "date target is a native non-editable selection");
    const RECT date_bounds=bounds_in_dialog(date_combo);
    const RECT help_bounds=bounds_in_dialog(GetDlgItem(dialog,IDC_BATCH_HELP));
    const RECT hint_bounds=bounds_in_dialog(GetDlgItem(dialog,IDC_BATCH_HINT));
    check(date_bounds.left>=help_bounds.right+3 &&
          date_bounds.right<=online_client.right &&
          date_bounds.top>=0 && date_bounds.top<hint_bounds.top,
          "date destination control stays apart from Help and status rows");
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
    const auto measured=djmeta_foobar::fit_initial_track_title_column(
        track_list,{L"Short.mp3",L"The KLF - What Time Is Love (Original Version).mp3"},96);
    check(measured>=165 && measured<=600,
          "first-open native font measurement is bounded");
    // Owner-data re-selection must preserve widths independently for Review
    // and MusicBrainz. Mode changes apply persisted widths, never defaults.
    auto online_grid=djmeta::default_review_grid_layout<4>({280,75,90,85});
    djmeta_foobar::apply_review_grid_controls(track_list,online_grid,96);
    ListView_SetColumnWidth(track_list,0,342);
    ListView_SetItemCountEx(track_list,2,LVSICF_NOINVALIDATEALL|LVSICF_NOSCROLL);
    check(ListView_GetColumnWidth(track_list,0)==342,
          "switching owner-data track does not reset custom online column width");
    djmeta_foobar::capture_review_grid_controls(track_list,online_grid,96);
    check(online_grid.widths[0]==342 && track_layout.widths[0]==190,
          "candidate and review track widths are separately remembered");
    djmeta_foobar::apply_review_grid_controls(track_list,track_layout,96);
    check(ListView_GetColumnWidth(track_list,0)==190,
          "returning to review restores its prior custom width");
    djmeta_foobar::apply_review_grid_controls(track_list,online_grid,96);
    check(ListView_GetColumnWidth(track_list,0)==342,
          "reopening search restores the previously customized online width");
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
    check(djmeta_foobar::native_workspace_should_validate_entry(
              notified_track_identity, notified_track_identity, 24, false) &&
          !djmeta_foobar::native_workspace_should_validate_entry(
              0, notified_track_identity, 24, false),
          "selected real HWND event revalidates its stable track source only");
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
    ListView_SetItemState(detail, 2,
        LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    check(ListView_GetSelectedCount(detail)==2 &&
          ListView_GetNextItem(detail,-1,LVNI_SELECTED)==1,
          "actual owner-data candidate ListView has earlier selected item");
    check(djmeta_foobar::native_focus_exact_release_row(detail,2) &&
          ListView_GetSelectedCount(detail)==1 &&
          ListView_GetNextItem(detail,-1,LVNI_SELECTED)==2 &&
          ListView_GetNextItem(detail,-1,LVNI_FOCUSED)==2,
          "real native multi-selection activation retargets first selected "
          "release to the actual double-click/Enter identity");
    check(!djmeta_foobar::native_focus_exact_release_row(detail,-1) &&
          !djmeta_foobar::native_focus_exact_release_row(detail,0) &&
          !djmeta_foobar::native_focus_exact_release_row(detail,3),
          "real native activation rejects empty, unselected and stale result rows");

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

    check(djmeta_foobar::native_browser_selection_requires_reset(false,0,0) &&
          djmeta_foobar::native_browser_selection_requires_reset(false,2,0) &&
          djmeta_foobar::native_browser_selection_requires_reset(true,-1,0) &&
          djmeta_foobar::native_browser_selection_requires_reset(true,1,0) &&
          !djmeta_foobar::native_browser_selection_requires_reset(true,0,0) &&
          !djmeta_foobar::native_browser_selection_requires_reset(true,3,3),
          "a reused owner-data list row can never stand in for a new release ID");
    // The real production search button must advertise explicit, bounded
    // progression; native rendering uses this pure caption contract.
    const auto mb_caption=djmeta_foobar::native_discogs_search_caption(
        false,false,false,false,false,false,0,0);
    const auto new_caption=djmeta_foobar::native_discogs_search_caption(
        true,false,false,true,false,false,0,8);
    const auto broaden_caption=djmeta_foobar::native_discogs_search_caption(
        true,false,true,true,false,false,1,8);
    check(std::wstring(mb_caption)==L"Search MusicBrainz" &&
          std::wstring(new_caption)==L"Search Discogs" &&
          std::wstring(broaden_caption)==L"Broaden Discogs" &&
          std::wstring(djmeta_foobar::native_discogs_search_caption(
              true,true,true,true,false,false,1,8))==L"Next master page" &&
          std::wstring(djmeta_foobar::native_discogs_search_caption(
              true,false,true,false,false,false,1,8))==L"Search Discogs" &&
          std::wstring(djmeta_foobar::native_discogs_search_caption(
              true,false,true,true,true,false,1,8))==L"Search Discogs" &&
          std::wstring(djmeta_foobar::native_discogs_search_caption(
              true,false,true,true,false,true,1,8))==L"Search Discogs" &&
          std::wstring(djmeta_foobar::native_discogs_search_caption(
              true,false,true,true,false,false,7,8))==L"Search Discogs" &&
          std::wstring(djmeta_foobar::native_discogs_search_caption(
              true,false,true,true,false,false,1,17))==L"Search Discogs",
          "native Discogs button distinguishes deliberate staged breadth "
          "from stale, pending, master, exhausted or loaded editions");
    SetDlgItemTextW(dialog, IDC_METADATA_MB_SEARCH, broaden_caption);
    wchar_t action_label[64]{};
    GetWindowTextW(GetDlgItem(dialog,IDC_METADATA_MB_SEARCH),action_label,64);
    check(std::wstring(action_label)==L"Broaden Discogs",
          "actual production RC HWND reflects explicit multi-strategy action");
    SetDlgItemTextW(dialog,IDC_METADATA_MB_SEARCH,mb_caption);
    djmeta_foobar::align_native_preview_form(dialog);
    RECT aligned_query{}, aligned_search{}, aligned_review_button{};
    COMBOBOXINFO scope_info{};
    scope_info.cbSize=sizeof(scope_info);
    check(GetWindowRect(mb_query,&aligned_query)!=FALSE &&
          GetWindowRect(mb_search,&aligned_search)!=FALSE &&
          GetWindowRect(GetDlgItem(dialog,IDC_METADATA_ACCEPT),
                        &aligned_review_button)!=FALSE &&
          GetComboBoxInfo(GetDlgItem(dialog,IDC_METADATA_REVIEW_SCOPE),
                          &scope_info)!=FALSE,
          "actual production toolbar and scope input rectangles readable");
    const auto center=[](const RECT& r){return (r.top+r.bottom)/2;};
    check(std::abs(center(aligned_query)-center(aligned_search)-2)<=1 &&
          aligned_query.bottom-aligned_query.top <
              aligned_search.bottom-aligned_search.top,
          "font-sized search EDIT aligns optically without a tall top-biased text box");
    RECT scope_item=scope_info.rcItem;
    MapWindowPoints(GetDlgItem(dialog,IDC_METADATA_REVIEW_SCOPE),
                    HWND_DESKTOP,reinterpret_cast<POINT*>(&scope_item),2);
    const int scope_center=center(scope_item);
    const int accept_center=center(aligned_review_button);
    if (scope_center != accept_center)
        std::cerr << "SCOPE_ALIGNMENT: combo_center=" << scope_center
                  << " accept_center=" << accept_center
                  << " combo_top=" << scope_item.top
                  << " combo_bottom=" << scope_item.bottom
                  << " action_top=" << aligned_review_button.top
                  << " action_bottom=" << aligned_review_button.bottom
                  << " outer_rect=" << GetWindowLongPtrW(
                        GetDlgItem(dialog,IDC_METADATA_REVIEW_SCOPE),GWL_STYLE)
                  << "\n";
    check(scope_center==accept_center,
          "Select changes combo item aligns with Accept action baseline");
    djmeta_foobar::align_native_preview_form(dialog);
    RECT repeated{};
    GetWindowRect(mb_query,&repeated);
    check(repeated.top==aligned_query.top,
          "repeated layout pass does not accumulate input alignment offset");
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

    // Real native EDIT notifications invalidate a captured request even
    // when the operator restores the identical query before completion.
    const auto old_query_version=test_request_context.current();
    check(SetWindowTextW(mb_query,L"First edition search")!=FALSE &&
          query_edit_events>0 &&
          !test_request_context.matches(old_query_version),
          "native query edit invalidates pending request context");
    const auto captured_query_version=test_request_context.current();
    check(SetWindowTextW(mb_query,L"Second edition search")!=FALSE &&
          SetWindowTextW(mb_query,L"First edition search")!=FALSE &&
          !test_request_context.matches(captured_query_version),
          "edit-and-restore cannot revive a stale response");
    const auto before_provider_switch=test_request_context.current();
    test_request_context.invalidate();
    test_request_context.invalidate();
    check(!test_request_context.matches(before_provider_switch),
          "provider away-and-back invalidates the old request");

    // Production request worker primitive: a delayed transport must not
    // block the host dialog thread or perform post-close SDK operations.
    {
        std::promise<void> release;
        auto gate=release.get_future().share();
        std::atomic<int> calls{0};
        const auto request_version=test_request_context.current();
        auto job=djmeta_foobar::start_readonly_network(dialog,[&](std::stop_token) {
            ++calls;
            gate.wait();
            return std::string("synthetic read-only JSON");
        });
        check(!djmeta_foobar::readonly_network_finished(*job),
              "network request remains pending before synthetic reply");
        check(SendMessageW(dialog,WM_NULL,0,0)==0,
              "modeless host dispatch remains responsive during provider wait");
        check(SetWindowTextW(mb_query,L"Changed while transport waits")!=FALSE &&
              !test_request_context.matches(request_version),
              "late worker response is stale after a real Win32 query edit");
        release.set_value();
        for(int n=0;n<200 && !djmeta_foobar::readonly_network_finished(*job);++n)
            Sleep(5);
        check(djmeta_foobar::readonly_network_finished(*job) &&
              job->response=="synthetic read-only JSON" &&
              job->error.empty() && calls==1,
              "one background response is published with release/acquire ordering");
        MSG notification{};
        while(PeekMessageW(&notification,dialog,
                           djmeta_foobar::kReadOnlyNetworkDoneMessage,
                           djmeta_foobar::kReadOnlyNetworkDoneMessage,PM_REMOVE))
            DispatchMessageW(&notification);
        check(read_only_worker_notifications==1,
              "completion posts one native message without a pointer or secret");
        if(job->worker.joinable()) job->worker.join();
        job.reset(); // owned jthread safely joined after completion
        bool rejected=false;
        try {
            (void)djmeta_foobar::start_readonly_network(
                nullptr,[](std::stop_token) {return std::string("not allowed");});
        } catch(const std::invalid_argument&) {
            rejected=true;
        }
        check(rejected,"native worker rejects missing callback HWND");
        auto failure=djmeta_foobar::start_readonly_network(dialog,[](std::stop_token) -> std::string {
            throw std::runtime_error("synthetic provider outage");
        });
        for(int n=0;n<200 && !djmeta_foobar::readonly_network_finished(*failure);++n)
            Sleep(5);
        check(djmeta_foobar::readonly_network_finished(*failure) &&
              failure->response.empty() &&
              failure->error=="synthetic provider outage",
              "network errors are captured without crashing the dialog thread");
        if(failure->worker.joinable()) failure->worker.join();
        failure.reset();
        // An edited query cancels the old reply without blocking UI.
        const auto pending_context=test_request_context.current();
        std::atomic<bool> saw_cancel{false};
        auto pending=djmeta_foobar::start_readonly_network(dialog,
            [&](std::stop_token cancel) -> std::string {
                for(int i=0;i<250 && !cancel.stop_requested();++i)
                    Sleep(2);
                if(cancel.stop_requested()) {
                    saw_cancel.store(true,std::memory_order_release);
                    throw std::runtime_error("synthetic cancellation");
                }
                return "uncancelled request";
            });
        check(SendMessageW(dialog,WM_NULL,0,0)==0 &&
              SetWindowTextW(mb_query,L"New query cancels pending provider")!=FALSE &&
              !test_request_context.matches(pending_context),
              "real native edit invalidates pending transport without blocking UI");
        djmeta_foobar::request_readonly_network_stop(*pending);
        for(int n=0;n<200 && !djmeta_foobar::readonly_network_finished(*pending);++n)
            Sleep(5);
        check(djmeta_foobar::readonly_network_finished(*pending) &&
              saw_cancel.load(std::memory_order_acquire) &&
              pending->response.empty() &&
              pending->error=="Provider request cancelled.",
              "close cancellation prevents stale provider reply");
        if(pending->worker.joinable()) pending->worker.join();
        pending.reset();
        while(PeekMessageW(&notification,dialog,
                           djmeta_foobar::kReadOnlyNetworkDoneMessage,
                           djmeta_foobar::kReadOnlyNetworkDoneMessage,PM_REMOVE))
            DispatchMessageW(&notification);
    }

    check(DestroyWindow(dialog)!=FALSE && !IsWindow(dialog),
          "modeless HWND shuts down without lingering native controls");
    std::cout << "PASS: Win32 reviewed bindings, modeless taskbar lifecycle, master/detail resizing and 96-192 DPI column geometry; "
                 "native preview resource, editable profile, "
                 "virtual master/detail controls and shared label alignment\n";
    return 0;
}
