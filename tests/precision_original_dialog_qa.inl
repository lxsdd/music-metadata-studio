// Runs in the real foobar SDK process against precision_dialog_proc, without
// HTTP workers, media fixtures, clipboard access or writer invocation.
void run_precision_dialog_qa_impl(HWND studio) {
    std::size_t assertion=0;
    const auto require=[&](bool ok) {
        ++assertion;
        if(!ok) throw std::runtime_error("Precision QA assertion "+std::to_string(assertion));
    };
    auto pending=std::make_unique<Workspace>();
    auto* state=pending.get();
    const HWND dialog=create_precision_dialog(studio,std::move(pending));
    try {
        require(GetWindow(dialog,GW_OWNER)==studio);
        require(IsWindowEnabled(studio)); // Ownership must not turn this modal.
        close_studio_precision_cue_generator(nullptr);
        require(IsWindow(dialog)); // A foreign/absent owner cannot close it.
        require(GetDlgItem(dialog,IDC_PRECISION_CANCEL_LOOKUP)!=nullptr);
        SetDlgItemTextW(dialog,IDC_PRECISION_APPLE_LINK,L"Synthetic album");
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
}
