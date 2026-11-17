// Compiled only into the disposable real-foobar QA component. Deliberately
// included in the production TU: messages reach batch_dialog_proc itself.
void run_studio_original_dialog_qa(HWND dialog) {
    auto* state=reinterpret_cast<PreviewState*>(GetWindowLongPtrW(dialog,GWLP_USERDATA));
    const auto require=[](bool ok) {
        if(!ok) throw std::runtime_error("Original Studio dialog regression failed");
    };
    require(state && !state->entries.empty() && !state->stale);
    const auto seed=[&] {
        state->online_raw_route_valid=true;
        state->online_raw_route_before="synthetic-before";
        state->online_raw_route_after="synthetic-after";
        state->cue_postimage_snapshot.emplace();
        state->cue_value_preview.emplace();
        state->cue_value_preview_indices={0};
        // Detect a missing actual owner-data refresh, not just helper calls.
        ListView_SetItemCountEx(state->list,777,LVSICF_NOSCROLL);
    };
    seed();
    auto version=state->online_context_version.current();
    SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_METADATA_MB_QUERY,EN_SETFOCUS),0);
    require(state->online_context_version.matches(version) && state->online_raw_route_valid);
    SetDlgItemTextW(dialog,IDC_METADATA_MB_QUERY,L"Synthetic original query edit");
    require(!state->online_context_version.matches(version));
    require(!state->online_raw_route_valid && state->online_raw_route_after.empty());
    require(!state->cue_postimage_snapshot && !state->cue_value_preview &&
            state->cue_value_preview_indices.empty());
    require(ListView_GetItemCount(state->list)==static_cast<int>(state->view_order.size()));
    require(state->table.rows.size()==state->entries.size() && !state->stale);
    // Editing back cannot re-authorize a response captured before the edit.
    SetDlgItemTextW(dialog,IDC_METADATA_MB_QUERY,L"");
    require(!state->online_context_version.matches(version));
    seed();
    version=state->online_context_version.current();
    SendDlgItemMessageW(dialog,IDC_METADATA_ONLINE_PROVIDER,CB_SETCURSEL,1,0);
    SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_METADATA_ONLINE_PROVIDER,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(GetDlgItem(dialog,IDC_METADATA_ONLINE_PROVIDER)));
    require(state->discogs_provider_selected && !state->online_context_version.matches(version));
    require(!state->online_raw_route_valid && !state->cue_postimage_snapshot &&
            !state->cue_value_preview && state->cue_value_preview_indices.empty());
    require(ListView_GetItemCount(state->list)==static_cast<int>(state->view_order.size()));
    require(!state->stale);
    // Real original ListView selection, not a helper-only dialog substitute.
    // A cleared selection must disable Generate CUE and direct WM_COMMAND
    // must fail closed before reaching any Precision CUE writer.
    if(state->metadata_track_list && state->show_candidate) {
        ListView_SetItemState(state->metadata_track_list,-1,0,LVIS_SELECTED);
        require(!marked_precision_cue_ready(*state));
        EnableWindow(GetDlgItem(dialog,IDC_METADATA_GENERATE_CUE),
                     marked_precision_cue_ready(*state));
        require(!IsWindowEnabled(GetDlgItem(dialog,IDC_METADATA_GENERATE_CUE)));
        bool rejected=false;
        try {
            (void)marked_precision_cue_sources(*state);
        } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected);
    }
}
