// Runs in the real foobar SDK process against precision_dialog_proc, without
// HTTP workers, media fixtures, clipboard access or writer invocation.
void run_precision_dialog_qa_impl(HWND studio, void (*invalidate)(HWND),
                                  const std::function<std::wstring(bool)>& check_plan) {
    std::size_t assertion=0;
    const auto require=[&](bool ok) {
        ++assertion;
        if(!ok) throw std::runtime_error("Precision QA assertion "+std::to_string(assertion));
    };
    auto pending=std::make_unique<Workspace>();
    auto* state=pending.get();
    bool file_plan_current = true;
    state->studio_review_check = [&]() -> std::wstring {
        return check_plan(file_plan_current);
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
        // Copy readiness requires actual selected disc evidence; no writer runs.
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
        require(!IsWindowEnabled(GetDlgItem(dialog,IDC_PRECISION_COPY_CUE)));
        require(!IsWindowEnabled(GetDlgItem(dialog,IDC_PRECISION_SAVE_CUE)));
        GetDlgItemTextW(dialog,IDC_PRECISION_SOURCE_HINT,cue_hint,256);
        require(std::wstring(cue_hint)==L"BLOCKED: select a reviewed disc with valid CUE text before copying.");
        // Dispatch and controls consume the identical fresh decision.
        const auto reviewed_without_export=current_cue_review_decision(*state);
        require(!reviewed_without_export.copy_ready);
        require(!reviewed_without_export.single_ready && !reviewed_without_export.batch_ready);
        require(!djmeta::precision_cue_native_operation_allowed(
            djmeta::PrecisionCueNativeOperation::CreateExternalSingle));
        require(!djmeta::precision_cue_native_operation_allowed(
            djmeta::PrecisionCueNativeOperation::CreateExternalBatch));
        // A new sibling CUE after review must invalidate both the visible
        // action and a queued click, even while source identity stays current.
        {
            namespace fs = std::filesystem;
            const auto dir = fs::canonical(fs::temp_directory_path()) /
                (L"studio-cue-target-" + std::to_wstring(GetCurrentProcessId()) +
                 L"-" + std::to_wstring(GetTickCount64()));
            require(fs::create_directory(dir));
            struct Cleanup {
                fs::path path;
                ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
            } cleanup{dir};
            djmeta::PrecisionCueDisc disc;
            disc.album_title = "Synthetic album";
            disc.album_performer = "Synthetic artist";
            disc.audio_file_basename = "Synthetic.mp3";
            disc.tracks = {{"Synthetic track", "Synthetic artist", 60000}};
            const auto physical = dir / disc.audio_file_basename;
            { std::ofstream out(physical, std::ios::binary); out << "synthetic source"; require(out.good()); }
            const auto snapshot = djmeta::precision_cue_external_detail::physical_source_snapshot(
                physical, dir, disc.audio_file_basename);
            require(snapshot.has_value());
            const auto plan = djmeta::make_precision_cue(disc);
            const auto reviewed = djmeta::capture_precision_cue_external_review(
                plan, physical, disc.audio_file_basename, *snapshot);
            require(reviewed.has_value());
            LocalFile source{};
            source.basename = disc.audio_file_basename;
            source.physical_absolute_path = physical;
            source.snapshot = *snapshot;
            state->files.push_back(source);
            state->reviewed_exports.push_back(reviewed);
            state->reviewed_cues.push_back(plan.external_cue_utf8);
            djmeta::PrecisionCueRelease release{};
            release.album_title=disc.album_title;
            release.album_performer=disc.album_performer;
            state->release=release;
            require(SendDlgItemMessageW(dialog,IDC_PRECISION_DISC_PICKER,CB_ADDSTRING,0,
                reinterpret_cast<LPARAM>(L"Disc 1"))>=0);
            require(SendDlgItemMessageW(dialog,IDC_PRECISION_DISC_PICKER,CB_SETCURSEL,0,0)==0);
            update_reviewed_actions(*state);
            require(IsWindowEnabled(GetDlgItem(dialog,IDC_PRECISION_COPY_CUE)));
            // A same-count selection replacement is not the reviewed source.
            // Exercise the original Studio callback, native hint and queued
            // Copy dispatch without clipboard or external CUE publication.
            const auto original_studio_check = state->studio_review_check;
            const std::vector<std::wstring> reviewed_ids{L"Synthetic.mp3"};
            std::vector<std::wstring> selected_ids = reviewed_ids;
            state->studio_review_check = [&]() -> std::wstring {
                if (selected_ids != reviewed_ids)
                    return L"BLOCKED: physical selection changed; review again.";
                return original_studio_check();
            };
            require(current_cue_review_decision(*state).copy_ready);
            selected_ids = {L"Different.mp3"}; // same number, different identity
            require(!current_cue_review_decision(*state).copy_ready);
            update_reviewed_actions(*state);
            GetDlgItemTextW(dialog,IDC_PRECISION_SOURCE_HINT,cue_hint,256);
            require(std::wstring(cue_hint)==L"BLOCKED: physical selection changed; review again.");
            require(!IsWindowEnabled(GetDlgItem(dialog,IDC_PRECISION_COPY_CUE)));
            // Even a previously queued click must not use the old approval.
            SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_PRECISION_COPY_CUE,BN_CLICKED),0);
            require(!state->reviewed_preview_ready);
            require(state->reviewed_exports.empty() && state->reviewed_cues.empty());
            GetDlgItemTextW(dialog,IDC_PRECISION_SOURCE_HINT,cue_hint,256);
            require(std::wstring(cue_hint)==L"BLOCKED: physical selection changed; review again.");
            // Restore only synthetic evidence to continue the target-race test.
            selected_ids = reviewed_ids;
            state->studio_review_check = original_studio_check;
            state->reviewed_exports.push_back(reviewed);
            state->reviewed_cues.push_back(plan.external_cue_utf8);
            state->reviewed_preview_ready = true;
            CheckDlgButton(dialog,IDC_PRECISION_EDITION_CONFIRMED,BST_CHECKED);
            update_reviewed_actions(*state);
            require(IsWindowEnabled(GetDlgItem(dialog,IDC_PRECISION_COPY_CUE)));
            const auto proposal = djmeta::plan_precision_cue_external_export(plan, source.basename);
            require(proposal.eligible);
            const auto target = dir / proposal.target_cue_basename;
            { std::ofstream out(target, std::ios::binary); out << "existing target"; require(out.good()); }
            // The UI still displays the previously valid review. A queued
            // command must reobserve the target BEFORE any clipboard action.
            require(IsWindowEnabled(GetDlgItem(dialog,IDC_PRECISION_COPY_CUE)));
            require(!current_cue_review_decision(*state).copy_ready);
            SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_PRECISION_COPY_CUE,BN_CLICKED),0);
            require(!state->reviewed_preview_ready);
            GetDlgItemTextW(dialog,IDC_PRECISION_SOURCE_HINT,cue_hint,256);
            require(std::wstring(cue_hint)==L"BLOCKED: reviewed CUE target changed.");
            require(!IsWindowEnabled(GetDlgItem(dialog,IDC_PRECISION_COPY_CUE)));
            // A fresh read-only paint must also explain the collision, not
            // merely leave a disabled button with an obsolete READY caption.
            // Dispatch intentionally invalidates and clears the reviewed exports.
            // Reinstall the original synthetic review evidence to exercise a
            // fresh read-only paint against the same now-existing target.
            state->reviewed_exports.push_back(reviewed);
            state->reviewed_preview_ready=true;
            CheckDlgButton(dialog,IDC_PRECISION_EDITION_CONFIRMED,BST_CHECKED);
            update_reviewed_actions(*state);
            GetDlgItemTextW(dialog,IDC_PRECISION_SOURCE_HINT,cue_hint,256);
            require(std::wstring(cue_hint)==L"BLOCKED: reviewed CUE target changed.");
            require(!IsWindowEnabled(GetDlgItem(dialog,IDC_PRECISION_COPY_CUE)));
            require(djmeta::precision_cue_external_detail::physical_source_unchanged(
                *snapshot,physical,dir,source.basename));
            state->files.pop_back();
            state->reviewed_exports.clear();
            state->reviewed_cues.clear();
            state->release.reset();
            SendDlgItemMessageW(dialog,IDC_PRECISION_DISC_PICKER,CB_RESETCONTENT,0,0);
            state->reviewed_preview_ready=true;
            CheckDlgButton(dialog,IDC_PRECISION_EDITION_CONFIRMED,BST_CHECKED);
        }
        // An unexpected non-standard SDK exception must not escape the
        // original child HWND. Display and dispatch must share the blocker.
        const auto normal_review=state->studio_review_check;
        state->studio_review_check=[]() -> std::wstring { throw 42; };
        update_reviewed_actions(*state);
        GetDlgItemTextW(dialog,IDC_PRECISION_SOURCE_HINT,cue_hint,256);
        require(std::wstring(cue_hint)==L"BLOCKED: physical selection or review evidence unavailable.");
        require(!IsWindowEnabled(GetDlgItem(dialog,IDC_PRECISION_COPY_CUE)));
        const auto exception_decision=current_cue_review_decision(*state);
        require(!exception_decision.copy_ready && !exception_decision.single_ready &&
            !exception_decision.batch_ready);
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_PRECISION_COPY_CUE,BN_CLICKED),0);
        require(!state->reviewed_preview_ready);
        state->studio_review_check=normal_review;
        state->reviewed_preview_ready=true;
        CheckDlgButton(dialog,IDC_PRECISION_EDITION_CONFIRMED,BST_CHECKED);
        update_reviewed_actions(*state);
        require(!IsWindowEnabled(GetDlgItem(dialog,IDC_PRECISION_COPY_CUE)));
        // Even a synthetic command sent to a disabled control must recheck
        // the stale Studio plan without accessing the clipboard.
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
