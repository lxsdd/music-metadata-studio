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
    // This gate is mandatory: a hidden/non-Find grid must not skip scope QA.
    require(state->metadata_track_list != nullptr);
    const auto original_entries=state->entries;
    const auto original_order=state->track_view_order;
    const auto original_summaries=state->track_summaries;
    const auto original_labels=state->cached_track_sort_labels;
    const auto original_ascending=state->cached_name_order_ascending;
    const auto original_descending=state->cached_name_order_descending;
    const auto original_discovery=state->track_discovery;
    const auto original_grid=state->track_grid;
    const auto original_detail=state->selected_track_index;
    const auto original_selected=selected_native_view_ids(
        state->metadata_track_list,state->track_view_order);
    const bool original_candidate=state->show_candidate;
    const bool original_updating=state->updating_track_selection;
    const auto restore=[&] {
        state->updating_track_selection=true;
        state->entries=original_entries;
        state->track_view_order=original_order;
        state->track_summaries=original_summaries;
        state->cached_track_sort_labels=original_labels;
        state->cached_name_order_ascending=original_ascending;
        state->cached_name_order_descending=original_descending;
        state->track_discovery=original_discovery;
        state->track_grid=original_grid;
        state->selected_track_index=original_detail;
        state->show_candidate=original_candidate;
        state->stale=false;
        ListView_SetItemCountEx(state->metadata_track_list,
            static_cast<int>(original_order.size()),LVSICF_NOSCROLL);
        restore_native_view_selection(state->metadata_track_list,original_order,
            original_selected,state->selected_track_index);
        state->updating_track_selection=original_updating;
    };
    try {
        // Synthetic captured records only. SDK handles are identities; no
        // files are created/read, no Generate handler or writer is invoked.
        state->updating_track_selection=true;
        state->show_candidate=true;
        state->entries.resize(12);
        for(std::size_t i=0;i<state->entries.size();++i) {
            auto& entry=state->entries[i];
            entry.input.source_path="file://C:/mms-synthetic-scope/track-"+
                std::to_string(i)+".mp3";
            metadb::get()->handle_create(entry.handle,
                make_playable_location(entry.input.source_path.c_str(),0));
            entry.input.physical_source_qualified=true;
            entry.observed_physical_key="synthetic-id-"+std::to_string(i);
            entry.observed_source_guard="synthetic-guard-"+std::to_string(i);
        }
        const auto project=[&](std::vector<std::size_t> order,
                               std::vector<std::size_t> selected) {
            state->track_view_order=std::move(order);
            ListView_SetItemCountEx(state->metadata_track_list,
                static_cast<int>(state->track_view_order.size()),LVSICF_NOSCROLL);
            restore_native_view_selection(state->metadata_track_list,
                state->track_view_order,selected,state->selected_track_index);
            // Exercise the original notification handler even when a projection
            // has the same row count and Windows emits no item change event.
            NMLISTVIEW changed{};
            changed.hdr.hwndFrom=state->metadata_track_list;
            changed.hdr.idFrom=IDC_METADATA_TRACK_LIST;
            changed.hdr.code=LVN_ITEMCHANGED;
            SendMessageW(dialog,WM_NOTIFY,IDC_METADATA_TRACK_LIST,
                reinterpret_cast<LPARAM>(&changed));
        };
        const auto accepted=[&](std::vector<std::size_t> expected) {
            require(marked_precision_cue_sources(*state)==expected);
            require(IsWindowEnabled(GetDlgItem(dialog,IDC_METADATA_GENERATE_CUE))!=FALSE);
        };
        const auto rejected=[&] {
            bool blocked=false;
            try { (void)marked_precision_cue_sources(*state); }
            catch(const std::invalid_argument&) { blocked=true; }
            require(blocked && !marked_precision_cue_ready(*state));
        };
        // More than eight captured records, exactly two marked. The returned
        // identities follow the visible projection, never the entire batch.
        project({0,1,2,3,4,5,6,7,8,9,10,11},{2,10}); accepted({2,10});
        project({11,10,9,8,7,6,5,4,3,2,1,0},{2,10}); accepted({10,2});
        project({10,7,2},{2,10}); accepted({10,2});
        project({7,2},{2,10}); accepted({2}); // hidden identity 10 excluded
        project({7,2},{}); rejected();
        require(!IsWindowEnabled(GetDlgItem(dialog,IDC_METADATA_GENERATE_CUE)));
        project({0,1,2,3,4,5,6,7,8},{0,1,2,3,4,5,6,7,8}); rejected();
        project({2,2},{2}); rejected(); // ambiguous row-to-source mapping
        project({2,10},{2,10}); accepted({2,10});
        state->entries[10].observed_physical_key=state->entries[2].observed_physical_key;
        rejected();
        state->entries[10].observed_physical_key="synthetic-id-10";
        state->entries[10].observed_source_guard.clear(); rejected();
        state->entries[10].observed_source_guard="synthetic-guard-10";
        state->entries[10].input.source_path="file://C:/mms-synthetic-scope/not-mp3.flac";
        rejected();
        state->entries[10].input.source_path="file://C:/mms-synthetic-scope/track-10.mp3";
        metadb::get()->handle_create(state->entries[10].handle,
            make_playable_location(state->entries[10].input.source_path.c_str(),1));
        rejected(); // a virtual subsong must never become a physical CUE input
        metadb::get()->handle_create(state->entries[10].handle,
            make_playable_location(state->entries[10].input.source_path.c_str(),0));
        state->stale=true; rejected(); state->stale=false;
        state->show_candidate=false; rejected(); state->show_candidate=true;
        project({2,10},{2,10}); accepted({2,10});
        // Real production table rebuild: two explicitly marked MP3s are
        // filtered away while other qualified MP3s remain visible.
        // A visible detail/focus fallback is NOT a new CUE selection.
        state->track_summaries.resize(12);
        state->cached_track_sort_labels.clear();
        for(std::size_t i=0;i<12;++i) {
            state->track_summaries[i].source_index=i;
            state->track_summaries[i].music_changes=(i==2 || i==10)?0:1;
            state->track_summaries[i].extended_changes=0;
            state->cached_track_sort_labels.push_back(
                "synthetic-track-"+std::to_string(100+i));
        }
        state->cached_name_order_ascending.clear();
        state->cached_name_order_descending.clear();
        state->track_grid.sort_column=0;
        state->track_grid.sort_descending=false;
        state->track_discovery=djmeta::TrackDiscovery::All;
        project({0,1,2,3,4,5,6,7,8,9,10,11},{2,10});
        update_master_table(*state);
        accepted({2,10});
        state->track_discovery=djmeta::TrackDiscovery::Changed;
        update_master_table(*state);
        require(!state->track_view_order.empty());
        require(std::find(state->track_view_order.begin(),
                          state->track_view_order.end(),std::size_t{2})==state->track_view_order.end());
        require(std::find(state->track_view_order.begin(),
                          state->track_view_order.end(),std::size_t{10})==state->track_view_order.end());
        require(ListView_GetSelectedCount(state->metadata_track_list)==0);
        rejected();
        require(!IsWindowEnabled(GetDlgItem(dialog,IDC_METADATA_GENERATE_CUE)));
        // A user mark after filtering, not automatic focus, authorizes scope.
        const auto newly_visible=state->track_view_order.front();
        project(state->track_view_order,{newly_visible});
        accepted({newly_visible});
        // A partial intersection retains only the explicitly marked visible
        // identity, including across descending sort.
        state->track_discovery=djmeta::TrackDiscovery::All;
        project({0,1,2,3,4,5,6,7,8,9,10,11},{2,10});
        state->track_summaries[2].music_changes=1;
        state->track_grid.sort_descending=true;
        update_master_table(*state);
        state->track_discovery=djmeta::TrackDiscovery::Changed;
        update_master_table(*state);
        accepted({2});
        // Refresh-like empty mark: rebuilding must never invent selection.
        project(state->track_view_order,{});
        update_master_table(*state);
        require(ListView_GetSelectedCount(state->metadata_track_list)==0);
        rejected();
    } catch (...) { restore(); throw; }
    restore();
}
