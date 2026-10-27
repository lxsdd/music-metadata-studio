#include "../src/foobar/native_preview_controls.h"
#include "../src/foobar/field_evidence_receipt.h"
#include "../src/foobar/session_preview_history.h"
#include "../src/foobar/readonly_network_request.h"
#include "../src/foobar/review_grid_controls.h"
#include "../src/foobar/host_file_probe.h"
#include "../src/foobar/legacy_routing_profiles.h"

#include <commctrl.h>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <limits>
#include <vector>
#include <array>
#include <tuple>
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
int notified_master_deselections = 0;
int notified_find_row = -99;
std::vector<std::wstring> active_track_names;
bool rebuilding_master = false;
bool live_included_online_fields = false;
std::size_t current_included_physical_source = 0;
int blocked_online_rebind_notifications = 0;
int read_only_worker_notifications = 0;
int query_edit_events = 0;
int split_notifications = 0;
int query_key_notifications = 0;
UINT last_query_key = 0;
djmeta_foobar::NativeReviewSplitAction last_split_action = djmeta_foobar::NativeReviewSplitAction::None;
djmeta_foobar::ReadOnlyContextVersion test_request_context;
INT_PTR CALLBACK test_dialog_proc(HWND dialog, UINT message, WPARAM wp, LPARAM lp) {
    if(message==djmeta_foobar::kNativeQueryKeyMessage) {
        ++query_key_notifications;
        last_query_key=static_cast<UINT>(wp);
        return TRUE;
    }
    if(message==djmeta_foobar::kNativeReviewSplitMessage) {
        ++split_notifications;
        last_split_action=static_cast<djmeta_foobar::NativeReviewSplitAction>(wp);
        return TRUE;
    }
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
            hdr->code == LVN_ITEMCHANGING) {
            const auto* change=reinterpret_cast<const NMLISTVIEW*>(lp);
            if(djmeta_foobar::native_online_source_selection_veto(
                *change,active_track_order,current_included_physical_source,
                live_included_online_fields,rebuilding_master)) {
                ++blocked_online_rebind_notifications;
                SetWindowLongPtrW(dialog,DWLP_MSGRESULT,TRUE);
                return TRUE;
            }
        }
        if (hdr && hdr->idFrom == IDC_METADATA_TRACK_LIST &&
            hdr->code == LVN_ITEMCHANGED) {
            const auto* change = reinterpret_cast<const NMLISTVIEW*>(lp);
            if (const auto track = djmeta_foobar::native_selected_track_change(
                    *change, active_track_order, rebuilding_master))
                notified_track_identity = *track;
            else if (djmeta_foobar::native_master_track_deselected(
                         *change,rebuilding_master))
                ++notified_master_deselections;
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
    using djmeta_foobar::native_verified_album_source_target;
    using djmeta_foobar::native_online_source_selection_veto;
    using djmeta_foobar::NativeRoutePreviewScope;
    using djmeta_foobar::native_route_preview_source_ids;
    // Real production format is an explicit read-only source/field receipt,
    // not flattened values, a synthetic approval or a provider confidence claim.
    using djmeta_foobar::NativeFieldEvidenceReceiptRow;
    using djmeta_foobar::native_render_evidence_receipt;
    using djmeta_foobar::native_evidence_receipt_allowed;
    NativeFieldEvidenceReceiptRow evidence{};
    evidence.field=L"TITLE";
    evidence.origin=L"Discogs API";
    evidence.source_id=L"release/37332711";
    evidence.scope=L"exact edition track, not recording";
    evidence.date_meaning=L"not a date";
    evidence.reason=L"mix identity unverified";
    evidence.status=L"Review";
    evidence.original_values={L"A|B",L"",L"line\ninjection"};
    evidence.evidence_values={L"Original Version",L"\u202ereverse"};
    const auto receipt=native_render_evidence_receipt(2,{evidence});
    check(receipt &&
          receipt->find(L"Physical source #: 3")!=std::wstring::npos &&
          receipt->find(L"Scope: exact edition track, not recording")!=std::wstring::npos &&
          receipt->find(L"Original value count: 3")!=std::wstring::npos &&
          receipt->find(L"Original[1]: A|B")!=std::wstring::npos &&
          receipt->find(L"Original[2]: \r\n")!=std::wstring::npos &&
          receipt->find(L"Original[3]: line[LF]injection")!=std::wstring::npos &&
          receipt->find(L"Evidence value count: 2")!=std::wstring::npos &&
          receipt->find(L"Evidence[2]: [RLO]reverse")!=std::wstring::npos &&
          receipt->find(L"No tag, CUE or file change was authorized")!=std::wstring::npos,
          "lossless ordered original/provider values and exact-source provenance");
    auto poisoned=evidence;
    poisoned.source_id=L"123\r\nOrigin: forged";
    poisoned.original_values={std::wstring(L"a\0b",3),L"tabs\tand\rreturns"};
    const auto sanitized=native_render_evidence_receipt(0,{poisoned});
    check(sanitized &&
          sanitized->find(L"123[CR][LF]Origin: forged")!=std::wstring::npos &&
          sanitized->find(L"a[NUL]b")!=std::wstring::npos &&
          sanitized->find(L"tabs[TAB]and[CR]returns")!=std::wstring::npos &&
          sanitized->find(L'\0')==std::wstring::npos,
          "control/bidi payload cannot forge source or truncate clipboard");
    check(native_render_evidence_receipt(0,
              std::vector<NativeFieldEvidenceReceiptRow>(128,evidence)) &&
          !native_render_evidence_receipt(0,{}) &&
          !native_render_evidence_receipt(0,
              std::vector<NativeFieldEvidenceReceiptRow>(129,evidence)),
          "multi-field copy bounded by exact visible 128-field selection");
    auto incomplete=evidence; incomplete.field.clear();
    auto enormous=evidence; enormous.reason=std::wstring(17000,L'x');
    check(!native_render_evidence_receipt(0,{incomplete}) &&
          !native_render_evidence_receipt(0,{enormous}) &&
          !native_render_evidence_receipt(2147483647u,{evidence}),
          "invalid field, oversized value and absent source identity rejected");
    check(native_evidence_receipt_allowed(
              true,true,true,false,false,false,1,2,0,1) &&
          !native_evidence_receipt_allowed(
              true,true,true,true,false,false,1,2,0,1) &&
          !native_evidence_receipt_allowed(
              true,true,true,false,true,false,1,2,0,1) &&
          !native_evidence_receipt_allowed(
              true,true,true,false,false,true,1,2,0,1) &&
          !native_evidence_receipt_allowed(
              true,true,false,false,false,false,1,2,0,1) &&
          !native_evidence_receipt_allowed(
              false,true,true,false,false,false,1,2,0,1) &&
          !native_evidence_receipt_allowed(
              true,true,true,false,false,false,2,2,0,1) &&
          !native_evidence_receipt_allowed(
              true,true,true,false,false,false,1,0,0,1) &&
          !native_evidence_receipt_allowed(
              true,true,true,false,false,false,1,129,0,1) &&
          !native_evidence_receipt_allowed(
              true,true,true,false,false,false,1,1,1,1),
          "stale or changing provider, source, group and field fail closed");
    check(djmeta_foobar::native_evidence_copy_hotkey('C',true,false) &&
          !djmeta_foobar::native_evidence_copy_hotkey('C',false,false) &&
          !djmeta_foobar::native_evidence_copy_hotkey('C',true,true) &&
          !djmeta_foobar::native_evidence_copy_hotkey('V',true,false),
          "only deliberate Ctrl+C triggers evidence copy");

    // Captured source indices survive native grid sorting and review jumps.
    check(native_verified_album_source_target(
          "Physical #2","discogs_live_release_group",true,true,3)==
          std::optional<std::size_t>{1},
          "exact Discogs group navigates captured physical ID");
    check(native_verified_album_source_target(
          "Physical #128","musicbrainz_live_release_group",true,true,128)==
          std::optional<std::size_t>{127},
          "MusicBrainz last album source is still addressable");
    check(native_verified_album_source_target(
          "Physical #1","local_physical_group",true,true,2)==
          std::optional<std::size_t>{0},
          "local physical original summary resolves first source");
    for(const char* invalid:{"Physical #0","Physical #01","Physical #-1",
                             "Physical #2 / TITLE","Physical #999",
                             "Physical #2tail","Physical #2 "}) {
        check(!native_verified_album_source_target(
            invalid,"discogs_live_release_group",true,true,3),
            "malformed album source label cannot navigate");
    }
    check(!native_verified_album_source_target(
              "Physical #2","manual_pasted_candidate_group",true,true,3) &&
          !native_verified_album_source_target(
              "Physical #2","discogs_live_release_group",false,true,3) &&
          !native_verified_album_source_target(
              "Physical #2","discogs_live_release_group",true,false,3) &&
          !native_verified_album_source_target(
              "Physical #2","discogs_live_release_group",true,true,129),
          "unverified, unloaded, stale and oversized groups cannot navigate");
    const std::vector<std::size_t> sorted_track_ids={2,0,1};
    NMLISTVIEW switching{};
    switching.iItem=2; switching.uChanged=LVIF_STATE;
    switching.uOldState=0; switching.uNewState=LVIS_SELECTED;
    check(native_online_source_selection_veto(
              switching,sorted_track_ids,0,true,false),
          "included online choices block another physical source selection");
    switching.iItem=1; switching.uOldState=LVIS_SELECTED;
    switching.uNewState=0;
    check(native_online_source_selection_veto(
              switching,sorted_track_ids,0,true,false),
          "included online choices veto deselection of original source");
    switching.uOldState=0; switching.uNewState=LVIS_SELECTED;
    check(!native_online_source_selection_veto(
              switching,sorted_track_ids,0,true,false),
          "reselecting same captured source never blocks");
    switching.iItem=-1;
    check(native_online_source_selection_veto(
              switching,sorted_track_ids,0,true,false),
          "unknown selected source cannot drop included choices");
    switching.iItem=2;
    check(!native_online_source_selection_veto(
              switching,sorted_track_ids,0,false,false) &&
          !native_online_source_selection_veto(
              switching,sorted_track_ids,0,true,true),
          "empty included choices and guarded UI rebuild permit selection");
    switching.uOldState=0; switching.uNewState=LVIS_FOCUSED;
    check(!native_online_source_selection_veto(
              switching,sorted_track_ids,0,true,false),
          "focus-only event does not alter included field identity");
    const std::vector<std::size_t> visible_routes_fixture={4,1,3};
    check(native_route_preview_source_ids(NativeRoutePreviewScope::Visible,
          5,visible_routes_fixture,{})==visible_routes_fixture,"visible route scope keeps sorted source IDs");
    check(native_route_preview_source_ids(NativeRoutePreviewScope::Selected,
          5,visible_routes_fixture,{1,2})==std::vector<std::size_t>({1,3}),
          "selected visible rows map to exact physical source IDs");
    check(native_route_preview_source_ids(NativeRoutePreviewScope::All,
          5,visible_routes_fixture,{})==std::vector<std::size_t>({0,1,2,3,4}),
          "all routing includes hidden source IDs");
    check(native_route_preview_source_ids(NativeRoutePreviewScope::All,
          54129,visible_routes_fixture,{}).size()==54129,"large batch all-source routing");
    for(const auto& bad:std::vector<std::vector<std::size_t>>{{4,4},{5},{}}) {
        bool rejected=false;
        try {(void)native_route_preview_source_ids(
            NativeRoutePreviewScope::Visible,5,bad,{});}
        catch(const std::invalid_argument&) {rejected=true;}
        check(rejected,"invalid/duplicate/empty visible routing rejected");
    }
    for(const auto& bad:std::vector<std::vector<std::size_t>>{{3},{1,1},{}}) {
        bool rejected=false;
        try {(void)native_route_preview_source_ids(
            NativeRoutePreviewScope::Selected,5,visible_routes_fixture,bad);}
        catch(const std::invalid_argument&) {rejected=true;}
        check(rejected,"invalid/duplicate/empty selected routing rejected");
    }
    check(djmeta_foobar::native_workspace_command_check_scope(
        djmeta_foobar::PreviewCommand::None,WM_COMMAND,
        MAKEWPARAM(IDC_BATCH_APPLY_VISIBLE,BN_CLICKED))==
        djmeta_foobar::NativeWorkspaceCheckScope::FullBatch,
        "visible route command rechecks full captured batch");
    using djmeta_foobar::SessionPreviewHistory;
    using djmeta_foobar::PreviewHistoryKind;
    SessionPreviewHistory history;
    check(!history.append(PreviewHistoryKind::Accept,0,0,0) &&
          history.events().empty(),"no-op preview is never a history event");
    for(std::size_t i=0;i<300;++i)
        check(history.append(PreviewHistoryKind::Route,1,1,2),
              "bounded session history records explicit route action");
    check(history.events().size()==SessionPreviewHistory::limit &&
          history.events().front().sequence==45 &&
          history.events().back().sequence==300 &&
          history.events().back().scope==2,
          "history retains the latest 256 events and stable ordinal");
    using djmeta_foobar::PreviewHistoryFilter;
    using djmeta_foobar::preview_history_visible_indices;
    using djmeta_foobar::preview_history_single_source;
    check(preview_history_visible_indices(history,PreviewHistoryFilter::Routing).size()==
              SessionPreviewHistory::limit &&
          preview_history_visible_indices(history,PreviewHistoryFilter::Review).empty() &&
          preview_history_visible_indices(history,PreviewHistoryFilter::Undo).empty(),
          "bounded session history filters preserve event order");
    history.clear();
    check(history.append(PreviewHistoryKind::Accept,1,2,0,7) &&
          history.append(PreviewHistoryKind::Reject,3,4,1,8) &&
          history.append(PreviewHistoryKind::Route,1,1,1,2) &&
          history.append(PreviewHistoryKind::UndoRoute,1,1,0,2),
          "mixed preview actions retain only single-source indexes");
    check(preview_history_visible_indices(history,PreviewHistoryFilter::Review)==
              std::vector<std::size_t>({0,1}) &&
          preview_history_visible_indices(history,PreviewHistoryFilter::Routing)==
              std::vector<std::size_t>({2}) &&
          preview_history_visible_indices(history,PreviewHistoryFilter::Undo)==
              std::vector<std::size_t>({3}),
          "history filtering preserves stable source event indexes");
    check(preview_history_single_source(history.events()[0],8)==7 &&
          !preview_history_single_source(history.events()[0],7) &&
          !preview_history_single_source(history.events()[1],100) &&
          preview_history_single_source(history.events()[2],3)==2 &&
          !preview_history_single_source(history.events()[2],2),
          "single-source navigation rejects stale, out-of-range and bulk events");
    history.clear();
    check(history.events().empty() &&
          history.append(PreviewHistoryKind::UndoReview,3,5,0) &&
          history.events().front().sequence==1,
          "refresh resets session-only history without old decisions");
    using djmeta_foobar::native_recheck_preserved_online_selection;
    const auto same_source_selection=[](bool online_pane_visible) {
        // The native tab switch is a presentation change: the guard has
        // no page-mode parameter and must remain true on BOTH tabs.
        (void)online_pane_visible;
        return native_recheck_preserved_online_selection(true,5,5,2);
    };
    check(same_source_selection(true) && same_source_selection(false),
          "preserved online source is rechecked after returning to local Review");
    check(!native_recheck_preserved_online_selection(false,5,5,2) &&
          !native_recheck_preserved_online_selection(true,5,6,2) &&
          !native_recheck_preserved_online_selection(true,5,5,0),
          "absent snapshot, changed source and no selection avoid unrelated crosschecks");
    using djmeta_foobar::native_joint_raw_observation_matches;
    check(native_joint_raw_observation_matches(
              5,5,"raw/house/a","raw/house/a",
              "physical:a","physical:a","guard:v1","guard:v1"),
          "exact joint raw target source, route and version are stable");
    check(!native_joint_raw_observation_matches(
              5,6,"raw/house/a","raw/house/a",
              "physical:a","physical:a","guard:v1","guard:v1") &&
          !native_joint_raw_observation_matches(
              5,5,"raw/house/a","raw/house/b",
              "physical:a","physical:a","guard:v1","guard:v1") &&
          !native_joint_raw_observation_matches(
              5,5,"raw/house/a","raw/house/a",
              "physical:a","physical:b","guard:v1","guard:v1") &&
          !native_joint_raw_observation_matches(
              5,5,"raw/house/a","raw/house/a",
              "physical:a","physical:a","guard:v1","guard:v2") &&
          !native_joint_raw_observation_matches(
              5,5,"","", "physical:a","physical:a","guard:v1","guard:v1") &&
          !native_joint_raw_observation_matches(
              5,5,"raw/house/a","raw/house/a",
              "physical:a","physical:a","",""),
          "changed source, raw route, physical ID, guard and empty evidence fail closed");
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
    const HWND field_scope_control=GetDlgItem(dialog,IDC_METADATA_REVIEW_SCOPE);
    check(field_scope_control!=nullptr &&
          (GetWindowLongPtrW(field_scope_control,GWL_STYLE)&CBS_DROPDOWNLIST)==CBS_DROPDOWNLIST &&
          (GetWindowLongPtrW(field_scope_control,GWL_STYLE)&WS_TABSTOP)!=0,
          "field-specific review uses keyboard-accessible native scope picker");
    const HWND file_action=GetDlgItem(dialog,IDC_BATCH_FILE_ACTION);
    check(file_action!=nullptr &&
          (GetWindowLongPtrW(file_action,GWL_STYLE)&CBS_DROPDOWNLIST)==CBS_DROPDOWNLIST &&
          (GetWindowLongPtrW(file_action,GWL_STYLE)&WS_TABSTOP)!=0,
          "native File locations action selection is keyboard accessible");
    for(const wchar_t* item:{L"Move",L"Copy",L"Rename",L"No file action"})
        check(SendMessageW(file_action,CB_ADDSTRING,0,
              reinterpret_cast<LPARAM>(item))!=CB_ERR,
              "four distinct read-only action intents are selectable");
    check(SendMessageW(file_action,CB_GETCOUNT,0,0)==4 &&
          SendMessageW(file_action,CB_SETCURSEL,3,0)==3 &&
          SendMessageW(file_action,CB_GETCURSEL,0,0)==3,
          "No file action is deliberately selectable and never auto-executes");
    const HWND cue_return=GetDlgItem(dialog,IDC_BATCH_BACK_TO_CUE);
    check(cue_return!=nullptr &&
          (GetWindowLongPtrW(cue_return,GWL_STYLE)&WS_TABSTOP)!=0,
          "Back to CUE action is keyboard accessible in the original dialog");
    wchar_t back_text[64]{};
    GetWindowTextW(cue_return,back_text,64);
    check(std::wstring(back_text)==L"Back to CUE",
          "native physical CUE drilldown has explicit return action");
    RECT back_rect{},route_rect{};
    GetWindowRect(cue_return,&back_rect);
    GetWindowRect(GetDlgItem(dialog,IDC_BATCH_APPLY_SELECTED),&route_rect);
    check(back_rect.right<route_rect.left &&
          back_rect.top<route_rect.bottom &&
          route_rect.top<back_rect.bottom,
          "CUE return does not overlap scoped file preview at default DPI");
    const HWND next_cue=GetDlgItem(dialog,IDC_BATCH_NEXT_CUE);
    check(next_cue!=nullptr &&
          (GetWindowLongPtrW(next_cue,GWL_STYLE)&WS_TABSTOP)!=0,
          "native Next CUE is keyboard accessible");
    RECT cue_rect{},selected_rect{};
    GetWindowRect(next_cue,&cue_rect);
    GetWindowRect(GetDlgItem(dialog,IDC_BATCH_APPLY_SELECTED),&selected_rect);
    check(cue_rect.right<selected_rect.left,
          "Next CUE does not overlap selected file plan at default DPI");
    const HWND visible_route=GetDlgItem(dialog,IDC_BATCH_APPLY_VISIBLE);
    check(visible_route!=nullptr &&
          (GetWindowLongPtrW(visible_route,GWL_STYLE)&WS_TABSTOP)!=0,
          "production native Visible route button is keyboard accessible");
    const HWND history_grid=GetDlgItem(dialog,IDC_BATCH_HISTORY_LIST);
    const HWND history_filter=GetDlgItem(dialog,IDC_BATCH_HISTORY_FILTER);
    check(history_filter!=nullptr &&
          (GetWindowLongPtrW(history_filter,GWL_STYLE)&CBS_DROPDOWNLIST)==CBS_DROPDOWNLIST,
          "native History view provides an action filter");
    RECT history_rect{},footer_rect_history{};
    check(history_grid!=nullptr &&
          (GetWindowLongPtrW(history_grid,GWL_STYLE)&LVS_TYPEMASK)==LVS_REPORT &&
          !IsWindowVisible(history_grid) &&
          GetWindowRect(history_grid,&history_rect) &&
          GetWindowRect(GetDlgItem(dialog,IDC_BATCH_HINT),&footer_rect_history) &&
          history_rect.bottom<=footer_rect_history.top,
          "production fourth history page is a hidden native report grid above footer");
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
    HWND provider_prefs = CreateDialogParamW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_PROVIDER_PREFERENCES), dialog, test_dialog_proc, 0);
    check(provider_prefs != nullptr,
          "production Online providers Preferences child page instantiates");
    const HWND provider_order = GetDlgItem(provider_prefs, IDC_PROVIDER_PRIORITY_LIST);
    const HWND provider_up = GetDlgItem(provider_prefs, IDC_PROVIDER_PRIORITY_UP);
    const HWND provider_down = GetDlgItem(provider_prefs, IDC_PROVIDER_PRIORITY_DOWN);
    check(provider_order != nullptr &&
          (GetWindowLongPtrW(provider_order, GWL_STYLE) & LBS_NOTIFY) != 0 &&
          provider_up != nullptr && provider_down != nullptr,
          "production source priority has a notified native list and move controls");
    wchar_t move_caption[40]{};
    if (provider_up) GetWindowTextW(provider_up, move_caption, 40);
    check(std::wcscmp(move_caption, L"Move up") == 0,
          "source reorder Up is visibly labelled in shipping resource");
    if (provider_down) GetWindowTextW(provider_down, move_caption, 40);
    check(std::wcscmp(move_caption, L"Move down") == 0,
          "source reorder Down is visibly labelled in shipping resource");
    if (provider_prefs) DestroyWindow(provider_prefs);
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
    HWND audit=GetDlgItem(dialog,IDC_BATCH_AUDIT);
    HWND undo=GetDlgItem(dialog,IDC_BATCH_UNDO);
    HWND copy_audit=GetDlgItem(dialog,IDC_BATCH_COPY_AUDIT);
    HWND copy_issues=GetDlgItem(dialog,IDC_BATCH_COPY_ISSUES);
    HWND guide=GetDlgItem(dialog,IDC_BATCH_GUIDE);
    check(refresh!=nullptr && IsWindowEnabled(refresh) && help && audit && undo &&
          copy_audit && copy_issues && guide,
          "production resource offers Refresh, Help and stage guidance");
    RECT help_box{},audit_box{},undo_box{},copy_box{},issues_box{},hint_box{};
    check(GetWindowRect(help,&help_box) &&
          GetWindowRect(audit,&audit_box) &&
          GetWindowRect(undo,&undo_box) &&
          GetWindowRect(copy_audit,&copy_box) &&
          GetWindowRect(copy_issues,&issues_box) &&
          GetWindowRect(GetDlgItem(dialog,IDC_BATCH_HINT),&hint_box) &&
          help_box.right<audit_box.left &&
          audit_box.right<undo_box.left &&
          undo_box.right<copy_box.left &&
          copy_box.right<issues_box.left &&
          audit_box.bottom<=hint_box.top &&
          undo_box.bottom<=hint_box.top &&
          copy_box.bottom<=hint_box.top &&
          issues_box.bottom<=hint_box.top,
          "native undo and batch audit do not overlap toolbar or footer");
    wchar_t caption[96]{};
    GetWindowTextW(audit,caption,96);
    check(std::wcscmp(caption,L"Audit batch")==0,
          "audit action names its read-only scope");
    GetWindowTextW(copy_audit,caption,96);
    check(std::wcscmp(caption,L"Copy audit")==0 &&
          djmeta_foobar::native_audit_clipboard_text_is_safe(L"#1\t0\t0") &&
          !djmeta_foobar::native_audit_clipboard_text_is_safe(L"") &&
          !djmeta_foobar::native_audit_clipboard_text_is_safe(
              std::wstring_view(L"A\0B",3)) &&
          !djmeta_foobar::native_audit_clipboard_text_is_safe(
              std::wstring(16u*1024u*1024u+1u,L'A')),
          "native complete-audit copy has a visible action and bounded payload");
    GetWindowTextW(copy_issues,caption,96);
    check(std::wcscmp(caption,L"Copy issues")==0,
          "native attention-only audit has a visible distinct command");
    check(djmeta_foobar::native_audit_clipboard_text_is_safe(
              std::wstring(2u*1024u*1024u,L'A')),
          "multi-megabyte library audit is allowed by native clipboard guard");
    GetWindowTextW(undo,caption,96);
    check(std::wcscmp(caption,L"Undo preview")==0,
          "undo labels an in-memory view operation rather than a file write");
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
    const HWND track_tip_list=GetDlgItem(dialog,IDC_METADATA_TRACK_LIST);
    const HWND detail_tip_list=GetDlgItem(dialog,IDC_METADATA_LIST);
    check(track_tip_list && detail_tip_list,
          "resource-backed review lists exist for independent row infotips");
    ListView_SetExtendedListViewStyleEx(track_tip_list,LVS_EX_INFOTIP,LVS_EX_INFOTIP);
    ListView_SetExtendedListViewStyleEx(detail_tip_list,LVS_EX_INFOTIP,LVS_EX_INFOTIP);
    check(ListView_GetToolTips(track_tip_list) != nullptr &&
          ListView_GetToolTips(detail_tip_list) != nullptr,
          "each native ListView owns a separate row infotip HWND");
    djmeta_foobar::native_workspace_pop_tooltips(tip,
        {track_tip_list,detail_tip_list,GetDlgItem(dialog,IDC_BATCH_LIST),nullptr});
    check(IsWindow(tip) && IsWindow(ListView_GetToolTips(detail_tip_list)),
          "dismissing help and row infotips never destroys native controls");
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
    // Regression for large foobar libraries: Alt-Tab and read-only online
    // actions validate the selected source only. Batch decisions and Apply
    // still revalidate all 54,129 sources before using old decisions.
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

    // WM_COMMAND policy is shared with the real foobar dialog. Only the
    // selected source is verified for online discovery/release/CUE read-only
    // actions; full batch verification is mandatory before batch decisions
    // and batch Apply. Notification type may not silently widen a check.
    using djmeta_foobar::NativeWorkspaceCheckScope;
    const auto scope_for=[](int id, WORD notice=BN_CLICKED) {
        const auto command=MAKEWPARAM(id,notice);
        return djmeta_foobar::native_workspace_command_check_scope(
            djmeta_foobar::native_preview_command(WM_COMMAND,command),
            WM_COMMAND,command);
    };
    for(const int id : {IDC_METADATA_MB_SEARCH,IDC_METADATA_MB_LOAD_RELEASE,
                        IDC_METADATA_DISCOGS_NEXT_PAGE,IDC_METADATA_INSPECT_CUE,
                        IDC_METADATA_IMPORT_CANDIDATE,IDC_ONLINE_WRITE_SELECTED}) {
        check(scope_for(id)==NativeWorkspaceCheckScope::SelectedSource,
              "selected-source online action avoids a full-library pre-scan");
        check(scope_for(id,EN_CHANGE)==NativeWorkspaceCheckScope::None,
              "text changes never cause a bulk source scan");
    }
    for(const int id : {IDC_METADATA_ACCEPT,IDC_METADATA_REJECT,
                        IDC_METADATA_RESET,IDC_METADATA_USE_VALUE,
                        IDC_BATCH_APPLY_SELECTED,IDC_BATCH_APPLY_ALL,
                        IDC_BATCH_AUDIT,IDC_BATCH_COPY_AUDIT,
                        IDC_BATCH_COPY_ISSUES,IDC_BATCH_UNDO})
        check(scope_for(id)==NativeWorkspaceCheckScope::FullBatch,
              "batch approval and Apply require every captured source");
    check(scope_for(IDC_METADATA_VISIBLE_WHITESPACE)==
              NativeWorkspaceCheckScope::None &&
          scope_for(IDC_BATCH_REFRESH)==NativeWorkspaceCheckScope::None &&
          scope_for(IDC_METADATA_MB_QUERY,EN_CHANGE)==
              NativeWorkspaceCheckScope::None &&
          djmeta_foobar::native_workspace_command_check_scope(
              djmeta_foobar::PreviewCommand::Accept,
              WM_ACTIVATE,MAKEWPARAM(IDC_METADATA_ACCEPT,BN_CLICKED))==
              NativeWorkspaceCheckScope::None,
          "display commands, typing and non-command messages do not scan");
    std::size_t online_search_checks=0, batch_apply_checks=0;
    for(std::size_t i=0;i<kLargeBatch;++i) {
        online_search_checks+=djmeta_foobar::native_workspace_should_validate_entry(
            i,29017,kLargeBatch,
            scope_for(IDC_METADATA_MB_SEARCH)==NativeWorkspaceCheckScope::FullBatch);
        batch_apply_checks+=djmeta_foobar::native_workspace_should_validate_entry(
            i,29017,kLargeBatch,
            scope_for(IDC_BATCH_APPLY_SELECTED)==NativeWorkspaceCheckScope::FullBatch);
    }
    check(online_search_checks==1 && batch_apply_checks==kLargeBatch,
          "54k-track online Search stays O(1) in source probes; Apply stays O(N)");

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
    const auto mb_empty=djmeta_foobar::native_empty_online_candidate_hint(false,false);
    const auto discogs_empty=djmeta_foobar::native_empty_online_candidate_hint(true,false);
    const auto beatport_empty=djmeta_foobar::native_empty_online_candidate_hint(false,true);
    check(std::wcsstr(mb_empty,L"MusicBrainz") != nullptr &&
          std::wcsstr(mb_empty,L"Deezer") != nullptr &&
          std::wcsstr(mb_empty,L"Spotify") != nullptr &&
          std::wcsstr(discogs_empty,L"Discogs") != nullptr &&
          std::wcsstr(discogs_empty,L"Beatport") == nullptr &&
          std::wcsstr(beatport_empty,L"Find in Crates") != nullptr &&
          std::wcsstr(beatport_empty,L"MusicBrainz") == nullptr &&
          std::wcsstr(mb_empty,L"read-only") != nullptr,
          "first-use candidate guidance follows provider and labels manual claims");
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
    check(djmeta_foobar::native_workspace_saved_maximized(true,false,false) &&
          djmeta_foobar::native_workspace_saved_maximized(false,true,true) &&
          !djmeta_foobar::native_workspace_saved_maximized(false,true,false) &&
          !djmeta_foobar::native_workspace_saved_maximized(false,false,true),
          "minimize-to-close remembers the last actual maximize state");
    RECT remembered_normal{};
    check(djmeta_foobar::native_capture_normal_screen_bounds(
              dialog,remembered_normal),
          "production capture records a real noniconic screen rectangle");
    ShowWindow(dialog,SW_MINIMIZE);
    RECT ignored_iconic{0,0,0,0};
    check(!djmeta_foobar::native_capture_normal_screen_bounds(
              dialog,ignored_iconic) &&
          ignored_iconic.left==0 && ignored_iconic.top==0 &&
          ignored_iconic.right==0 && ignored_iconic.bottom==0,
          "minimized normal window cannot replace screen geometry with workspace values");
    ShowWindow(dialog,SW_RESTORE);
    check(djmeta_foobar::native_capture_normal_screen_bounds(
              dialog,remembered_normal),
          "normal window after minimize yields usable screen geometry again");
    // Exercise the exact restore helper called by the production WM_INITDIALOG.
    // GetWindowPlacement normal bounds are workspace coordinates for the
    // resource's WS_EX_APPWINDOW dialog. Repeated maximal restores must not
    // reinterpret those as desktop screen coordinates.
    WINDOWPLACEMENT initial_workspace{};
    initial_workspace.length=sizeof(initial_workspace);
    check(GetWindowPlacement(dialog,&initial_workspace)!=FALSE,
          "native dialog provides WINDOWPLACEMENT coordinates");
    const RECT saved_workspace=initial_workspace.rcNormalPosition;
    check(djmeta_foobar::native_restore_saved_workspace_bounds(
              dialog,saved_workspace,true) &&
          IsZoomed(dialog)!=FALSE,
          "production native HWND maximized restore uses SetWindowPlacement");
    WINDOWPLACEMENT after_max{};
    after_max.length=sizeof(after_max);
    check(GetWindowPlacement(dialog,&after_max)!=FALSE &&
          EqualRect(&saved_workspace,&after_max.rcNormalPosition)!=FALSE,
          "restored native maximized normal position roundtrips unchanged");
    ShowWindow(dialog,SW_RESTORE);
    RECT normal_screen{};
    check(GetWindowRect(dialog,&normal_screen)!=FALSE &&
          djmeta_foobar::native_restore_saved_workspace_bounds(
              dialog,normal_screen,false),
          "production native restored window path accepts a screen rectangle");
    RECT after_normal{};
    check(GetWindowRect(dialog,&after_normal)!=FALSE &&
          EqualRect(&normal_screen,&after_normal)!=FALSE &&
          IsZoomed(dialog)==FALSE,
          "native normal window restore retains exact screen position and size");
    check(!djmeta_foobar::native_restore_saved_workspace_bounds(
              dialog,RECT{5,5,5,7},true),
          "malformed native saved placement must not activate or resize window");
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
          GetDlgItem(dialog, IDC_BATCH_ROUTE_LABEL) &&
          GetDlgItem(dialog, IDC_BATCH_PROFILE_SAVE) &&
          GetDlgItem(dialog, IDC_BATCH_PROFILE_DELETE),
          "named route save/delete are wired to the original Win32 dialog");
    RECT picker_rect{},save_rect{},delete_rect{};
    GetWindowRect(combo,&picker_rect);
    GetWindowRect(GetDlgItem(dialog,IDC_BATCH_PROFILE_SAVE),&save_rect);
    GetWindowRect(GetDlgItem(dialog,IDC_BATCH_PROFILE_DELETE),&delete_rect);
    check(picker_rect.right<save_rect.left && save_rect.right<delete_rect.left &&
          picker_rect.top==save_rect.top && save_rect.top==delete_rect.top,
          "saved-route controls align without overlapping the native picker");

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
    check(djmeta_foobar::native_online_lookup_can_cancel(true,false) &&
          !djmeta_foobar::native_online_lookup_can_cancel(true,true) &&
          !djmeta_foobar::native_online_lookup_can_cancel(false,false),
          "only a pending uncancelled provider request offers cancellation");
    check(djmeta_foobar::native_apply_online_cancel_button(
              mb_search,true,false) && IsWindowEnabled(mb_search),
          "real production Search HWND exposes Cancel lookup");
    wchar_t cancel_label[64]{};
    GetWindowTextW(mb_search,cancel_label,64);
    check(std::wcscmp(cancel_label,L"Cancel lookup")==0,
          "native Search button caption advertises cancellation");
    check(djmeta_foobar::native_apply_online_cancel_button(
              mb_search,true,true) && !IsWindowEnabled(mb_search),
          "native Cancelling state prevents repeat request");
    GetWindowTextW(mb_search,cancel_label,64);
    check(std::wcscmp(cancel_label,L"Cancelling...")==0 &&
          !djmeta_foobar::native_apply_online_cancel_button(
              nullptr,true,false) &&
          !djmeta_foobar::native_apply_online_cancel_button(
              mb_search,false,false),
          "missing HWND or completed request never mutates the native button");
    SetWindowTextW(mb_search,L"Search MusicBrainz");
    EnableWindow(mb_search,TRUE);
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
          "only Find-tab MusicBrainz/Discogs activation authorizes an official API search");
    check(djmeta_foobar::native_track_activation_starts_crates_handoff(0,true) &&
          !djmeta_foobar::native_track_activation_starts_crates_handoff(0,false) &&
          !djmeta_foobar::native_track_activation_starts_crates_handoff(1,true) &&
          !djmeta_foobar::native_track_activation_starts_crates_handoff(2,true) &&
          !djmeta_foobar::native_track_activation_starts_crates_handoff(-1,true),
          "Beatport-first track activation opens Crates only from Find, never "
          "from Review/File tabs or an official API source");
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
    check(djmeta_foobar::native_window_dpi(dialog)>=96,
          "native per-monitor DPI resolver returns usable window metrics");
    for(const auto [dpi,pad,offset]:{
            std::tuple{96,6,4},std::tuple{120,8,5},
            std::tuple{144,9,6},std::tuple{192,12,8}}) {
        check(djmeta_foobar::native_edit_padding_for_dpi(6,dpi)==pad &&
              djmeta_foobar::native_edit_padding_for_dpi(4,dpi)==offset,
              "search and scope optical insets scale at 100-200 percent DPI");
    }
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
    // The fourth bulk scope is native and non-editable; verify its actual
    // Win32 combo capacity and roundtrip alongside the manual-value editor.
    for(const wchar_t* label:{L"Selected changes",L"Selected tracks",
                              L"All tracks",L"Visible queue"})
        check(SendMessageW(review_scope,CB_ADDSTRING,0,
              reinterpret_cast<LPARAM>(label))!=CB_ERR,
              "native review scope accepts all four choices");
    check(SendMessageW(review_scope,CB_GETCOUNT,0,0)==4 &&
          SendMessageW(review_scope,CB_SETCURSEL,3,0)==3 &&
          SendMessageW(review_scope,CB_GETCURSEL,0,0)==3,
          "visible filtered queue is selectable without editing source IDs");
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
    RECT track_header_bounds{};
    check(GetWindowRect(track_header,&track_header_bounds)!=FALSE,
          "real review track header has screen bounds for keyboard menu");
    const auto keyboard_column_point=djmeta_foobar::native_column_menu_point(
        track_header,static_cast<LPARAM>(-1));
    check(keyboard_column_point.x>=track_header_bounds.left &&
          keyboard_column_point.x<track_header_bounds.right &&
          keyboard_column_point.y>=track_header_bounds.top &&
          keyboard_column_point.y<track_header_bounds.bottom,
          "Shift+F10 column menu anchors to the focused native header");
    const auto mouse_column_point=djmeta_foobar::native_column_menu_point(
        track_header,MAKELPARAM(321,654));
    check(mouse_column_point.x==321 && mouse_column_point.y==654,
          "mouse column menu retains the actual screen click location");
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
    // Same real HWND with candidate menu routing, user widths and 150% DPI.
    auto& chosen_online=djmeta_foobar::active_review_grid_layout(
        track_layout,online_grid,true);
    auto& chosen_review=djmeta_foobar::active_review_grid_layout(
        track_layout,online_grid,false);
    check(&chosen_online==&online_grid && &chosen_review==&track_layout,
          "context menu routes candidate and review columns independently");
    const int original_block_width=online_grid.widths[3];
    djmeta_foobar::apply_review_grid_controls(track_list,chosen_online,144,3);
    check(ListView_GetColumnWidth(track_list,0)==MulDiv(342,144,96) &&
          ListView_GetColumnWidth(track_list,3)==0,
          "DPI rescale retains transient online browse suppression");
    djmeta_foobar::capture_review_grid_controls(track_list,online_grid,144);
    check(online_grid.widths[0]==342 &&
          online_grid.widths[3]==original_block_width,
          "DPI capture retains logical widths instead of browse-only zero");
    djmeta_foobar::apply_review_grid_controls(track_list,online_grid,96);
    check(ListView_GetColumnWidth(track_list,3)==original_block_width,
          "return from online browse restores remembered fourth width");
    check(djmeta::set_review_column_visible(online_grid,3,false),
          "explicit candidate fourth-column visibility is separately editable");
    djmeta_foobar::apply_review_grid_controls(track_list,online_grid,96);
    check(ListView_GetColumnWidth(track_list,3)==0 &&
          (online_grid.visible_mask & (1u<<3))==0 &&
          track_layout.widths[0]==190,
          "candidate visibility settings cannot reset review widths");
    auto detail_layout = djmeta::default_review_grid_layout<5>({80, 105, 105, 60, 75});
    detail_layout.order = {4, 0, 1, 3, 2};
    HWND detail_list = GetDlgItem(dialog, IDC_METADATA_LIST);
    RECT detail_header_bounds{};
    const HWND detail_header=ListView_GetHeader(detail_list);
    check(GetWindowRect(detail_header,&detail_header_bounds)!=FALSE,
          "real review detail header has screen bounds for keyboard menu");
    const auto detail_keyboard_point=djmeta_foobar::native_column_menu_point(
        detail_header,static_cast<LPARAM>(-1));
    check(detail_keyboard_point.x>=detail_header_bounds.left &&
          detail_keyboard_point.x<detail_header_bounds.right &&
          detail_keyboard_point.y>=detail_header_bounds.top &&
          detail_keyboard_point.y<detail_header_bounds.bottom,
          "detail-grid keyboard column menu anchors to its own header");
    djmeta_foobar::apply_review_grid_controls(detail_list, detail_layout, 96);
    std::array<int, 5> actual_detail_order{};
    check(ListView_GetColumnOrderArray(detail_list, 5, actual_detail_order.data()) &&
          actual_detail_order == detail_layout.order,
          "real five-column detail header supports persisted logical order");
    djmeta_foobar::capture_review_grid_controls(detail_list, detail_layout, 96);
    check(detail_layout.order == actual_detail_order,
          "detail header order roundtrips via production adapter");
    auto candidate_detail=djmeta::default_review_grid_layout<5>(
        {260,165,130,95,160});
    check(&djmeta_foobar::active_review_grid_layout(
              detail_layout,candidate_detail,true)==&candidate_detail &&
          &djmeta_foobar::active_review_grid_layout(
              detail_layout,candidate_detail,false)==&detail_layout,
          "candidate detail context menu has its own five-column layout");
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
    HWND file_queue=GetDlgItem(dialog,IDC_BATCH_LOCATION_FILTER);
    HWND file_table=GetDlgItem(dialog,IDC_BATCH_LIST);
    check(file_queue!=nullptr && file_table!=nullptr &&
          (static_cast<DWORD>(GetWindowLongPtrW(file_queue,GWL_STYLE))&0x3u)
              ==CBS_DROPDOWNLIST,
          "native File locations queue must be non-editable");
    RECT queue_rect{},table_rect{};
    check(GetWindowRect(file_queue,&queue_rect) &&
          GetWindowRect(file_table,&table_rect) &&
          queue_rect.bottom<=table_rect.top,
          "native File locations filter must not overlap its table");
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
    const int deselections_before=notified_master_deselections;
    ListView_SetItemState(master, 2, 0, LVIS_SELECTED);
    check(notified_master_deselections==deselections_before+1 &&
          ListView_GetSelectedCount(master)==1,
          "real master deselection updates marked count without retargeting source");
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
    // Production dialog resource ID and owner-data ListView HWND: verify
    // that an actual Win32 WM_NOTIFY round-trip can veto the source
    // mutation before previously included field choices are removed.
    live_included_online_fields=true;
    current_included_physical_source=9;
    NMLISTVIEW source_change_notice{};
    source_change_notice.hdr.hwndFrom=master;
    source_change_notice.hdr.idFrom=IDC_METADATA_TRACK_LIST;
    source_change_notice.hdr.code=LVN_ITEMCHANGING;
    source_change_notice.iItem=2; // sorted physical source 5, not source 9
    source_change_notice.uChanged=LVIF_STATE;
    source_change_notice.uOldState=0;
    source_change_notice.uNewState=LVIS_SELECTED;
    const int blocked_before=blocked_online_rebind_notifications;
    check(SendMessageW(dialog,WM_NOTIFY,IDC_METADATA_TRACK_LIST,
             reinterpret_cast<LPARAM>(&source_change_notice))==TRUE &&
          blocked_online_rebind_notifications==blocked_before+1,
          "actual native dialog dispatch vetoes cross-source included choice loss");
    source_change_notice.iItem=0; // currently included source 9
    source_change_notice.uOldState=LVIS_SELECTED;
    source_change_notice.uNewState=0;
    check(SendMessageW(dialog,WM_NOTIFY,IDC_METADATA_TRACK_LIST,
             reinterpret_cast<LPARAM>(&source_change_notice))==TRUE &&
          blocked_online_rebind_notifications==blocked_before+2,
          "native message dispatch also rejects deselecting included source");
    source_change_notice.uOldState=0;
    source_change_notice.uNewState=LVIS_SELECTED;
    check(SendMessageW(dialog,WM_NOTIFY,IDC_METADATA_TRACK_LIST,
             reinterpret_cast<LPARAM>(&source_change_notice))==FALSE &&
          blocked_online_rebind_notifications==blocked_before+2,
          "native same-source reselect is allowed without losing decisions");
    check(djmeta_foobar::native_online_preview_requires_explicit_discard(
              9,5,2) &&
          !djmeta_foobar::native_online_preview_requires_explicit_discard(
              9,9,2) &&
          !djmeta_foobar::native_online_preview_requires_explicit_discard(
              9,5,0) &&
          djmeta_foobar::native_online_preview_requires_explicit_discard(
              9,9,1,true),
          "explicit source/provider switch needs default-No discard only when "
          "in-memory online choices would otherwise be lost");
    live_included_online_fields=false;
    source_change_notice.iItem=2;
    check(SendMessageW(dialog,WM_NOTIFY,IDC_METADATA_TRACK_LIST,
             reinterpret_cast<LPARAM>(&source_change_notice))==FALSE,
          "native source switching resumes once online selection is reset");
    HWND detail = GetDlgItem(dialog, IDC_METADATA_LIST);
    check(ListView_SetItemCountEx(detail, 3, 0) != FALSE,
          "production virtual proposal grid accepts three rows");
    ListView_SetItemState(detail, 1, LVIS_SELECTED, LVIS_SELECTED);
    check(djmeta_foobar::selected_native_view_ids(detail, {4, 11, 2}) ==
          std::vector<std::size_t>({11}),
          "detail proposal selection resolves sorted proposal identity");
    const auto header=(std::numeric_limits<std::size_t>::max)();
    ListView_SetItemState(detail,0,LVIS_SELECTED,LVIS_SELECTED);
    check(djmeta_foobar::selected_native_proposal_ids(
              detail,{header,11,2},12)==std::vector<std::size_t>({11}),
          "native group header cannot become a field action target");
    ListView_SetItemState(detail,1,0,LVIS_SELECTED);
    check(djmeta_foobar::selected_native_proposal_ids(
              detail,{header,11,2},12).empty(),
          "header-only native selection produces no action targets");
    ListView_SetItemState(detail,0,0,LVIS_SELECTED);
    ListView_SetItemState(detail,1,LVIS_SELECTED,LVIS_SELECTED);

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

    // Keyboard actions share mouse geometry; modifier leakage, inactive
    // views and unrelated keys cannot change the pane offset or track row.
    using djmeta_foobar::NativeReviewSplitAction;
    using djmeta_foobar::native_review_split_key_action;
    using djmeta_foobar::native_review_split_next_offset;
    check(native_review_split_key_action(VK_LEFT,true,true,false,true)==
              NativeReviewSplitAction::NarrowSource &&
          native_review_split_key_action(VK_RIGHT,true,true,false,true)==
              NativeReviewSplitAction::WidenSource &&
          native_review_split_key_action(VK_HOME,true,true,false,true)==
              NativeReviewSplitAction::Reset &&
          native_review_split_key_action(VK_LEFT,true,false,false,true)==
              NativeReviewSplitAction::None &&
          native_review_split_key_action(VK_LEFT,false,true,false,true)==
              NativeReviewSplitAction::None &&
          native_review_split_key_action(VK_LEFT,true,true,true,true)==
              NativeReviewSplitAction::None &&
          native_review_split_key_action(VK_LEFT,true,true,false,false)==
              NativeReviewSplitAction::None &&
          native_review_split_key_action(VK_RETURN,true,true,false,true)==
              NativeReviewSplitAction::None,
          "divider hotkeys are exact and cannot steal normal selection/navigation");
    check(native_review_split_next_offset(72,NativeReviewSplitAction::NarrowSource)==48 &&
          native_review_split_next_offset(72,NativeReviewSplitAction::WidenSource)==96 &&
          native_review_split_next_offset(72,NativeReviewSplitAction::Reset)==0 &&
          native_review_split_next_offset(1999,NativeReviewSplitAction::WidenSource)==2000 &&
          native_review_split_next_offset(-1999,NativeReviewSplitAction::NarrowSource)==-2000 &&
          native_review_split_next_offset((std::numeric_limits<int>::max)(),
              NativeReviewSplitAction::WidenSource)==2000 &&
          native_review_split_next_offset((std::numeric_limits<int>::min)(),
              NativeReviewSplitAction::NarrowSource)==-2000 &&
          native_review_split_next_offset(72,NativeReviewSplitAction::None)==72,
          "keyboard and context menu offsets are bounded and resettable");

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

    // A profile-local divider displacement must preserve the native gap,
    // minimum pane widths and physical HWND geometry. It is view-only.
    const auto dragged = djmeta_foobar::review_split_geometry(
        original_master,original_detail,241,80,72,160,180);
    check(dragged.master.right -
              djmeta_foobar::review_split_geometry(
                  original_master,original_detail,241,80).master.right == 72 &&
          dragged.detail.left-dragged.master.right==base_gap &&
          dragged.master.right-dragged.master.left>=160 &&
          dragged.detail.right-dragged.detail.left>=180 &&
          djmeta_foobar::review_split_gap_hit(
              dragged.master,dragged.detail,dragged.master.right,
              dragged.master.top+10) &&
          !djmeta_foobar::review_split_gap_hit(
              dragged.master,dragged.detail,dragged.master.right-1,
              dragged.master.top+10),
          "dragged splitter moves both panes without confusing list clicks");
    const auto clamped= djmeta_foobar::review_split_geometry(
        original_master,original_detail,241,80,2000,160,180);
    check(clamped.detail.right-clamped.detail.left==180 &&
          clamped.detail.left-clamped.master.right==base_gap,
          "extreme persisted divider offset cannot collapse detail pane");
    check(djmeta_foobar::apply_review_split_geometry(
              master,detail,original_master,original_detail,241,80,72),
          "real resource ListViews accept a user-adjusted divider");
    const auto moved_master=client_rect(dialog,master);
    const auto moved_detail=client_rect(dialog,detail);
    check(moved_master.right==dragged.master.right &&
          moved_detail.left==dragged.detail.left,
          "real HWND pane positions match draggable geometry");

    // Exercise the real common-controls keyboard subclass on BOTH production
    // HWNDs. Ctrl+Shift+Right must be consumed before the owner-data list
    // changes the selected track; ordinary arrows remain native.
    check(SetWindowSubclass(master,
              djmeta_foobar::native_review_split_keyboard_proc,7,0) &&
          SetWindowSubclass(detail,
              djmeta_foobar::native_review_split_keyboard_proc,7,0),
          "actual Win32 source/detail grids accept divider key subclasses");
    BYTE old_keyboard[256]{}, pressed_keyboard[256]{};
    check(GetKeyboardState(old_keyboard)!=FALSE,
          "native test can snapshot thread keyboard state");
    std::copy(std::begin(old_keyboard),std::end(old_keyboard),
              std::begin(pressed_keyboard));
    pressed_keyboard[VK_CONTROL]=0x80;
    pressed_keyboard[VK_SHIFT]=0x80;
    pressed_keyboard[VK_MENU]=0;
    check(SetKeyboardState(pressed_keyboard)!=FALSE,
          "native test can set exact modifier state");
    const int before_split_notifications=split_notifications;
    const auto before_master_selected=ListView_GetSelectedCount(master);
    SendMessageW(master,WM_KEYDOWN,VK_RIGHT,0);
    SendMessageW(detail,WM_KEYDOWN,VK_HOME,0);
    check(SetKeyboardState(old_keyboard)!=FALSE,
          "native test restores keyboard state after synthetic key gesture");
    check(split_notifications==before_split_notifications+2 &&
          last_split_action==NativeReviewSplitAction::Reset,
          "real ListView subclasses dispatch resize and reset to parent");
    check(ListView_GetSelectedCount(master)==before_master_selected,
          "resize hotkey preserves the current owner-data selection");

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
    NMLISTVIEW removed{};
    removed.iItem=1;
    removed.uChanged=LVIF_STATE;
    removed.uOldState=LVIS_SELECTED;
    removed.uNewState=0;
    check(djmeta_foobar::native_master_track_deselected(removed,false) &&
          !djmeta_foobar::native_master_track_deselected(removed,true),
          "only real user deselection refreshes the marked-scope caption");
    removed.uNewState=LVIS_SELECTED;
    check(!djmeta_foobar::native_master_track_deselected(removed,false),
          "selection events still use current-source activation path");
    const auto no_tracks=djmeta_foobar::native_review_scope_hint(
        0,2,1,1,false);
    const auto one_track=djmeta_foobar::native_review_scope_hint(
        1,2,1,1,false);
    const auto many_tracks=djmeta_foobar::native_review_scope_hint(
        500,2,1,1,true);
    check(no_tracks.find(L"No tracks marked")!=std::wstring::npos &&
          one_track.find(L"Current track:")!=std::wstring::npos &&
          one_track.find(L"500 tracks")==std::wstring::npos &&
          many_tracks.find(L"500 tracks marked")!=std::wstring::npos &&
          many_tracks.find(L"Right side shows current track")!=std::wstring::npos &&
          many_tracks.find(L"Action scope below")!=std::wstring::npos &&
          many_tracks.find(L"No proposals for this track in this view")!=
              std::wstring::npos,
          "multi-selection hint separates current detail from batch action scope");
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

    // Actual production EDIT receives keyboard messages via the same
    // subclass used by Studio; no network, tag, CUE or file I/O in this test.
    using djmeta_foobar::NativeQueryKeyAction;
    using djmeta_foobar::native_query_key_action;
    check(native_query_key_action(VK_RETURN,false,false,false,
              true,false,false,true)==NativeQueryKeyAction::Search &&
          native_query_key_action(VK_ESCAPE,false,false,false,
              true,true,false,true)==NativeQueryKeyAction::Cancel &&
          native_query_key_action(VK_ESCAPE,false,false,false,
              true,false,false,true)==NativeQueryKeyAction::Close,
          "Enter searches explicitly, Escape cancels active or closes idle");
    for(const auto key:{VK_RETURN,VK_ESCAPE}) {
        check(native_query_key_action(key,true,false,false,
                  true,false,false,true)==NativeQueryKeyAction::None &&
              native_query_key_action(key,false,true,false,
                  true,false,false,true)==NativeQueryKeyAction::None &&
              native_query_key_action(key,false,false,true,
                  true,false,false,true)==NativeQueryKeyAction::None &&
              native_query_key_action(key,false,false,false,
                  false,false,false,true)==NativeQueryKeyAction::None,
              "modified keys and inactive Find tab cannot dispatch a lookup");
    }
    check(native_query_key_action(VK_RETURN,false,false,false,
              true,true,false,true)==NativeQueryKeyAction::None &&
          native_query_key_action(VK_ESCAPE,false,false,false,
              true,true,true,false)==NativeQueryKeyAction::None &&
          native_query_key_action(VK_RETURN,false,false,false,
              true,false,false,false)==NativeQueryKeyAction::None,
          "pending, cancelling and disabled Search reject duplicate gestures");
    djmeta_foobar::NativeQueryKeyboardState edit_keys{};
    check(SetWindowSubclass(mb_query,
              djmeta_foobar::native_query_edit_keyboard_proc,1,
              reinterpret_cast<DWORD_PTR>(&edit_keys))!=FALSE,
          "native query EDIT subclass installed on actual production resource");
    check((SendMessageW(mb_query,WM_GETDLGCODE,VK_RETURN,0)&
           DLGC_WANTMESSAGE)!=0 &&
          (SendMessageW(mb_query,WM_GETDLGCODE,VK_ESCAPE,0)&
           DLGC_WANTMESSAGE)!=0,
          "native edit claims Enter/Escape before default Close button");
    const int before_keys=query_key_notifications;
    SendMessageW(mb_query,WM_KEYDOWN,VK_RETURN,0);
    SendMessageW(mb_query,WM_CHAR,L'\r',0);
    check(query_key_notifications==before_keys+1 &&
          last_query_key==VK_RETURN,
          "real EDIT Enter dispatches exactly one explicit parent action");
    SendMessageW(mb_query,WM_KEYDOWN,VK_ESCAPE,0);
    SendMessageW(mb_query,WM_CHAR,L'\x1b',0);
    check(query_key_notifications==before_keys+2 &&
          last_query_key==VK_ESCAPE,
          "real EDIT Escape dispatches once without duplicate dialog command");
    edit_keys.ime_composing=true;
    check((SendMessageW(mb_query,WM_GETDLGCODE,VK_RETURN,0)&
           DLGC_WANTMESSAGE)==0,
          "IME composition leaves native edit dialog key handling intact");
    edit_keys.ime_composing=false;
    SendMessageW(mb_query,WM_KEYDOWN,VK_TAB,0);
    check(query_key_notifications==before_keys+2,
          "Tab and non-action keys do not issue provider actions");
    check(RemoveWindowSubclass(mb_query,
              djmeta_foobar::native_query_edit_keyboard_proc,1)!=FALSE,
          "native query keyboard subclass cleans up without HWND retention");

    // Exact row counts from the real Windows RC/HWND, not guessed source IDs.
    const HWND evidence_view=GetDlgItem(dialog,IDC_METADATA_MB_DETAILS);
    check(evidence_view && ListView_GetColumnWidth(evidence_view,0)>0,
          "native field evidence grid from real production dialog exists");
    LVITEMW one_field{};
    one_field.mask=LVIF_TEXT;
    one_field.pszText=const_cast<LPWSTR>(L"TITLE");
    one_field.iItem=0;
    check(ListView_InsertItem(evidence_view,&one_field)>=0,
          "native field evidence test row inserted");
    ListView_SetItemState(evidence_view,0,LVIS_SELECTED|LVIS_FOCUSED,
                          LVIS_SELECTED|LVIS_FOCUSED);
    check(native_evidence_receipt_allowed(
              true,true,true,false,false,false,1,
              ListView_GetSelectedCount(evidence_view),0,1),
          "real selected field permits exact-source receipt");
    ListView_SetItemState(evidence_view,0,0,LVIS_SELECTED);
    check(!native_evidence_receipt_allowed(
              true,true,true,false,false,false,1,
              ListView_GetSelectedCount(evidence_view),0,1),
          "actual deselection prevents stale default-row receipt");

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
        check(djmeta_foobar::native_apply_online_cancel_button(
                  mb_search,true,false) && IsWindowEnabled(mb_search),
              "native Cancel lookup remains responsive during network wait");
        test_request_context.invalidate();
        djmeta_foobar::request_readonly_network_stop(*pending);
        check(djmeta_foobar::native_apply_online_cancel_button(
                  mb_search,true,true) && !IsWindowEnabled(mb_search) &&
              !test_request_context.matches(pending_context),
              "native cancellation stops worker and invalidates response epoch");
        for(int n=0;n<200 && !djmeta_foobar::readonly_network_finished(*pending);++n)
            Sleep(5);
        check(djmeta_foobar::readonly_network_finished(*pending) &&
              saw_cancel.load(std::memory_order_acquire) &&
              pending->response.empty() &&
              pending->error=="Provider request cancelled.",
              "close cancellation prevents stale provider reply");
        if(pending->worker.joinable()) pending->worker.join();
        pending.reset();
        SetWindowTextW(mb_search,L"Search MusicBrainz");
        EnableWindow(mb_search,TRUE);
        check(IsWindowEnabled(mb_search),
              "native Search is restored after asynchronous cancel completion");
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
