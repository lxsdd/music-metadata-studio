// Runs in the real foobar SDK process against precision_dialog_proc, without
// HTTP workers, media fixtures, clipboard access or writer invocation.
void run_precision_dialog_qa_impl() {
    const auto require=[](bool ok) {
        if(!ok) throw std::runtime_error("Original Precision CUE dialog regression failed");
    };
    auto* state=new Workspace();
    const HWND dialog=CreateDialogParamW(core_api::get_my_instance(),
        MAKEINTRESOURCEW(IDD_PRECISION_CUE_PREVIEW),nullptr,precision_dialog_proc,
        reinterpret_cast<LPARAM>(state));
    if(!dialog) { delete state; throw std::runtime_error("Precision QA creation failed"); }
    try {
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
    DestroyWindow(dialog);
    require(g_precision_workspace==nullptr);
}
