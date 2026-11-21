// Runs in the real foobar SDK process against precision_dialog_proc, without
// HTTP workers, media fixtures, clipboard access or writer invocation.
void run_precision_dialog_qa_impl(HWND studio, void (*invalidate)(HWND)) {
    std::size_t assertion=0;
    const auto require=[&](bool ok) {
        ++assertion;
        if(!ok) throw std::runtime_error("Precision QA assertion "+std::to_string(assertion));
    };
    auto pending=std::make_unique<Workspace>();
    auto* state=pending.get();
    bool file_plan_current = true;
    state->studio_review_check = [&]() -> std::wstring {
        return file_plan_current ? L"" : L"BLOCKED: file plan stale; preview again.";
    };
    const HWND dialog=create_precision_dialog(studio,std::move(pending));
    try {
        require(GetParent(dialog)==studio && GetAncestor(dialog,GA_ROOT)==studio);
        require((GetWindowLongPtrW(dialog,GWL_STYLE)&WS_CHILD)!=0);
        require((GetWindowLongPtrW(dialog,GWL_STYLE)&(WS_POPUP|WS_CAPTION))==0);
        require((GetWindowLongPtrW(dialog,GWL_EXSTYLE)&WS_EX_CONTROLPARENT)!=0);
        require(IsWindowEnabled(studio)); // Ownership must not turn this modal.
        close_studio_precision_cue_generator(nullptr);
        require(IsWindow(dialog)); // A foreign/absent owner cannot close it.
        require(GetDlgItem(dialog,IDC_PRECISION_CANCEL_LOOKUP)!=nullptr);
        SetDlgItemTextW(dialog,IDC_PRECISION_APPLE_LINK,L"Synthetic album");
        const HWND tabs=GetDlgItem(studio,IDC_BATCH_TABS);
        const auto select_tab=[&](int index) {
            TabCtrl_SetCurSel(tabs,index);
            NMHDR notification{tabs,IDC_BATCH_TABS,TCN_SELCHANGE};
            SendMessageW(studio,WM_NOTIFY,IDC_BATCH_TABS,reinterpret_cast<LPARAM>(&notification));
        };
        require(TabCtrl_GetItemCount(tabs)==5);
        select_tab(4);
        require(IsWindowVisible(dialog) && IsWindowVisible(tabs));
        require(!IsWindowVisible(GetDlgItem(studio,IDC_METADATA_TRACK_LIST)));
        require(GetFocus()==GetDlgItem(dialog,IDC_PRECISION_APPLE_LINK));
        const HWND next=GetNextDlgTabItem(studio,tabs,FALSE);
        require(next && IsChild(dialog,next));
        for (const int page:{0,1,2,3}) {
            select_tab(page);
            require(!IsWindowVisible(dialog) && IsWindowEnabled(studio));
            select_tab(4);
            require(IsWindowVisible(dialog) && studio_precision_cue_view(studio)==dialog);
            wchar_t input[100]{};
            GetDlgItemTextW(dialog,IDC_PRECISION_APPLE_LINK,input,100);
            require(std::wstring(input)==L"Synthetic album");
        }
        RECT original{},pane{},client{};
        GetWindowRect(studio,&original);
        for (const int delta:{160,0,320,0}) {
            SetWindowPos(studio,nullptr,0,0,original.right-original.left+delta,
                original.bottom-original.top+delta,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            GetClientRect(studio,&client);
            GetWindowRect(dialog,&pane);
            MapWindowPoints(HWND_DESKTOP,studio,reinterpret_cast<POINT*>(&pane),2);
            require(pane.left>=0 && pane.top>0 && pane.right<=client.right && pane.bottom<=client.bottom);
            RECT button{},inner{};
            GetClientRect(dialog,&inner);
            GetWindowRect(GetDlgItem(dialog,IDCANCEL),&button);
            MapWindowPoints(HWND_DESKTOP,dialog,reinterpret_cast<POINT*>(&button),2);
            require(button.left>=0 && button.top>=0 && button.right<=inner.right && button.bottom<=inner.bottom);
        }
        // Back navigates in the original parent; it does not discard CUE input.
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDCANCEL,BN_CLICKED),
            reinterpret_cast<LPARAM>(GetDlgItem(dialog,IDCANCEL)));
        require(IsWindow(dialog) && !IsWindowVisible(dialog) && TabCtrl_GetCurSel(tabs)==0);
        select_tab(4);
        // A current synthetic search response must populate the real COMBOBOX.
        state->lookup_url="Synthetic album";
        state->input_revision.begin_request();
        state->job=std::make_shared<LookupJob>();
        state->job->kind=PrecisionCueJobKind::AppleSearch;
        state->job->hits.push_back({"123", "Synthetic album", "Synthetic artist", 2});
        state->job->ready.store(true);
        SendMessageW(dialog,WM_TIMER,kPollTimer,0);
        require(!state->job && state->search_hits.size()==1 &&
            SendDlgItemMessageW(dialog,IDC_PRECISION_ALBUM_RESULTS,CB_GETCOUNT,0,0)==1);
        // Candidate changes invalidate the original revision even with equal text.
        state->input_revision.begin_request();
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_PRECISION_ALBUM_RESULTS,CBN_SELCHANGE),0);
        require(!state->input_revision.request_is_current());
        state->job=std::make_shared<LookupJob>();
        state->job->kind=PrecisionCueJobKind::AppleSearch;
        state->job->hits.push_back({"999", "Stale album", "Synthetic artist", 2});
        state->job->ready.store(true);
        SendMessageW(dialog,WM_TIMER,kPollTimer,0);
        require(!state->job && state->search_hits.empty());
        require(SendDlgItemMessageW(dialog,IDC_PRECISION_ALBUM_RESULTS,CB_GETCOUNT,0,0)==0);
        state->input_revision.begin_request();
        state->job=std::make_shared<LookupJob>();
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_PRECISION_CANCEL_LOOKUP,BN_CLICKED),0);
        require(!state->job && !state->input_revision.request_is_current());
        // Synthetic positive/negative review states through the real HWND.
        // Copy readiness is advisory; neither case invokes a writer.
        state->reviewed_preview_ready=false;
        update_reviewed_actions(*state);
        wchar_t cue_hint[256]{};
        GetDlgItemTextW(dialog,IDC_PRECISION_SOURCE_HINT,cue_hint,256);
        require(std::wstring(cue_hint).find(L"preview stale")!=std::wstring::npos);
        require(!IsWindowEnabled(GetDlgItem(dialog,IDC_PRECISION_COPY_CUE)));
        state->reviewed_preview_ready=true;
        CheckDlgButton(dialog,IDC_PRECISION_EDITION_CONFIRMED,BST_CHECKED);
        SendMessageW(dialog,WM_COMMAND,
            MAKEWPARAM(IDC_PRECISION_EDITION_CONFIRMED,BN_CLICKED),0);
        require(IsWindowEnabled(GetDlgItem(dialog,IDC_PRECISION_COPY_CUE)));
        require(!IsWindowEnabled(GetDlgItem(dialog,IDC_PRECISION_SAVE_CUE)));
        GetDlgItemTextW(dialog,IDC_PRECISION_SOURCE_HINT,cue_hint,256);
        require(std::wstring(cue_hint).find(L"selected disc export evidence missing")!=std::wstring::npos);
        // Dispatch and controls consume the identical fresh decision.
        const auto reviewed_without_export=current_cue_review_decision(*state);
        require(reviewed_without_export.copy_ready);
        require(!reviewed_without_export.single_ready && !reviewed_without_export.batch_ready);
        require(!djmeta::precision_cue_native_operation_allowed(
            djmeta::PrecisionCueNativeOperation::CreateExternalSingle));
        require(!djmeta::precision_cue_native_operation_allowed(
            djmeta::PrecisionCueNativeOperation::CreateExternalBatch));
        // The plan changed after paint. Dispatch the real copy command while
        // the button is still enabled, without accessing the clipboard.
        file_plan_current=false;
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_PRECISION_COPY_CUE,BN_CLICKED),0);
        GetDlgItemTextW(dialog,IDC_PRECISION_SOURCE_HINT,cue_hint,256);
        require(std::wstring(cue_hint)==L"BLOCKED: file plan stale; preview again.");
        require(!IsWindowEnabled(GetDlgItem(dialog,IDC_PRECISION_COPY_CUE)));
        require(!state->reviewed_preview_ready);
        file_plan_current=true;
        CheckDlgButton(dialog,IDC_PRECISION_EDITION_CONFIRMED,BST_UNCHECKED);
        update_reviewed_actions(*state);
        require(!IsWindowEnabled(GetDlgItem(dialog,IDC_PRECISION_COPY_CUE)));
        state->reviewed_preview_ready=false;
        update_reviewed_actions(*state);
        // Disabled buttons alone are insufficient: invoke the original handlers.
        for(const int id:{IDC_PRECISION_SAVE_CUE,IDC_PRECISION_SAVE_ALL_CUES}) {
            require(!IsWindowEnabled(GetDlgItem(dialog,id)));
            SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);
            wchar_t text[2048]{};
            GetDlgItemTextW(dialog,IDC_PRECISION_PREVIEW_TEXT,text,2048);
            require(std::wstring(text).find(L"locked pending separate owner")!=std::wstring::npos);
        }
        SetDlgItemTextW(dialog,IDC_PRECISION_APPLE_LINK,L"https://www.deezer.com/album/123");
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_PRECISION_LOOKUP,BN_CLICKED),0);
        wchar_t text[2048]{};
        GetDlgItemTextW(dialog,IDC_PRECISION_PREVIEW_TEXT,text,2048);
        require(!state->job && std::wstring(text).find(L"no Deezer request was sent")!=std::wstring::npos);
    } catch (...) { DestroyWindow(dialog); throw; }
    SendMessageW(dialog,WM_CLOSE,0,0);
    require(IsWindow(dialog) && !IsWindowVisible(dialog));
    close_studio_precision_cue_generator(studio);
    require(g_precision_workspace==nullptr && IsWindow(studio) && IsWindowEnabled(studio));
    // Reopen through the production factory, retain a synthetic worker in the
    // real pool and prove close requests stop without waiting on that worker.
    auto again=std::make_unique<Workspace>();
    auto* active=again.get();
    const HWND reopened=create_precision_dialog(studio,std::move(again));
    auto job=std::make_shared<LookupJob>();
    // A stop source is sufficient here: no network, timing or sleeps in QA.
    job->worker=std::jthread([](std::stop_token) {});
    const auto stop=job->worker.get_stop_token();
    g_lookup_pool.jobs.push_back(job);
    active->job=job;
    close_studio_precision_cue_generator(studio);
    require(!IsWindow(reopened) && g_precision_workspace==nullptr);
    require(stop.stop_requested() && IsWindowEnabled(studio));
    job->ready.store(true);
    reap_finished_jobs();
    // Native ownership itself also handles forced owner destruction without
    // WM_CLOSE (host teardown), using the exact same production dialog path.
    const HWND owner=CreateWindowExW(0,L"STATIC",L"Synthetic Studio owner",
        WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,core_api::get_my_instance(),nullptr);
    require(owner!=nullptr);
    const HWND orphan_check=create_precision_dialog(owner,std::make_unique<Workspace>());
    DestroyWindow(owner);
    require(!IsWindow(orphan_check) && g_precision_workspace==nullptr);
    bool rejected=false;
    try { create_precision_dialog(nullptr,std::make_unique<Workspace>()); }
    catch(const std::invalid_argument&) { rejected=true; }
    require(rejected && g_precision_workspace==nullptr);
    // Exercise the actual Studio stale handler, not a duplicate test helper.
    const HWND stale_panel=create_precision_dialog(studio,std::make_unique<Workspace>());
    const HWND tabs=GetDlgItem(studio,IDC_BATCH_TABS);
    TabCtrl_SetCurSel(tabs,4);
    NMHDR notification{tabs,IDC_BATCH_TABS,TCN_SELCHANGE};
    SendMessageW(studio,WM_NOTIFY,IDC_BATCH_TABS,reinterpret_cast<LPARAM>(&notification));
    require(IsWindowVisible(stale_panel));
    invalidate(studio);
    require(!IsWindow(stale_panel) && studio_precision_cue_view(studio)==nullptr);
    require(TabCtrl_GetCurSel(tabs)==0 && IsWindowEnabled(studio));
}
