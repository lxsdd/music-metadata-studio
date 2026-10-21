from pathlib import Path

root = Path(__file__).resolve().parents[1]
foobar = root / "src" / "foobar"
sources = "\\n".join(
    p.read_text(encoding="utf-8", errors="strict")
    for p in sorted(foobar.rglob("*"))
    if p.suffix.lower() in {".cpp", ".h", ".rc"}
)

required = {
    "preview menu": "Normalize metadata (Preview)",
    "async metadata load": "load_info_async",
    "read-only shared rules": "GENERIC_READ",
    "existing-file rules open": "OPEN_EXISTING",
    "shared rules path": "DJMetadataNormalizer",
    "explicit no-write UX": "No tags were written",
}
missing = [name for name, token in required.items() if token not in sources]
if missing:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing " + ", ".join(missing))

forbidden = [
    "update_info_async",
    "update_info_simple",
    "metadb_io_v2::update",
    "file_info_filter",
    "GENERIC_WRITE",
    "WriteFile(",
    "DeleteFile",
    "MoveFile",
    "ReplaceFile",
    "fileOpenWrite",
    "filesystem::g_open_write",
]
found = [token for token in forbidden if token in sources]
if found:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: write-capable token(s): " + ", ".join(found))

project = (root / "foo_music_metadata_studio.vcxproj").read_text(encoding="utf-8")
for token in [
    "FOOBAR2000_TARGET_VERSION=81",
    "src\\core\\normalizer.cpp",
    "src\\core\\rule_loader.cpp",
    "foo_music_metadata_studio",
]:
    if token not in project:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: project contract missing " + token)

# A single captured shared rules snapshot must be checked at each
# transactional UI edit and during initial loading, in addition to
# per-track metadata fingerprints.
batch_rules_guard = (foobar / "batch_preview_dialog.cpp").read_text(encoding="utf-8")
for token in (
    "djmeta::RulesTextSnapshot captured_rules",
    "verify_rules_snapshot(state.captured_rules)",
    "verify_rules_snapshot(starting_rules)",
    "djmeta::require_rules_snapshot(captured, current.json, current.source_label)",
):
    if token not in batch_rules_guard:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing shared rules stale-input gate: " + token)

# Official master URL discovery reaches the native worker, then concrete
# release selection. A master ID never becomes a release-detail write source.
for token in (
    "master_id_from_url(term)", "master_versions_path(master_id,master_page)",
    "context.master_id=master_id", "context.master_page=master_page",
    "parse_master_versions_page(", "state.discogs_master_page+1",
    "if(source_id.starts_with(\"master:\"))", "perform_discogs_search(dialog,state);",
):
    if token not in batch_rules_guard:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: master edition route missing "+token)

# Master search belongs to the provider implementation. Check the documented
# canonical path and the native opt-in independently, not in one source.
discogs_provider = (root / "include" / "djmeta" / "discogs_provider.h").read_text(encoding="utf-8")
for token in ("master_search_path(core)", "type=master",
              "max_master_version_page", "master_versions_path",
              "if(master && (!q || !tail.empty())) return false;"):
    if token not in discogs_provider:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: Discogs master provider gate missing " + token)
if "auto_query?local_album:std::string{},true" not in batch_rules_guard:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: native Discogs master opt-in missing")

# Refresh cancellation is only permitted after affirmative confirmation.
refresh_source = batch_rules_guard.split("void refresh_workspace_snapshot(PreviewState& state)",1)[1]
refresh_source = refresh_source.split("// Native context help",1)[0]
if refresh_source.index("!= IDYES) return;") > refresh_source.index("invalidate_online_request(state);"):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: Refresh cancels before consent")
if "invalidate_online_request(*state);\n            refresh_workspace_snapshot(*state);" in batch_rules_guard:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: Refresh command cancels before consent")

# Search and release completion for BOTH providers must reject context
# mutations even if the visible query returns to the original text.
if batch_rules_guard.count(
        "state.online_context_version.matches(request.context_version)") != 4:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: four provider epoch gates required")
for token in ("HIWORD(wp)==EN_CHANGE",
              "invalidate_online_request(*state)",
              "state.online_context_version.invalidate()",
              "context.context_version=state.online_context_version.current()",
              "ctx.context_version=state.online_context_version.current()"):
    if token not in batch_rules_guard:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: online epoch wiring missing " + token)

preview = (foobar / "preview.cpp").read_text(encoding="utf-8")
for token in [
    "item.result.input_fingerprint",
    "djmeta::fingerprint(current)",
    "The preview is stale",
]:
    if token not in preview:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: stale-input preview guard missing " + token)

host_probe = (foobar / "host_file_probe.cpp").read_text(encoding="utf-8")
for token in (
    "filesystem::g_get_canonical_path",
    "filesystem::g_get_native_path",
    "filesystem::g_get_stats2",
    "FILE_READ_ATTRIBUTES",
    "OPEN_EXISTING",
    "GetFileInformationByHandle",
    "FILE_SHARE_DELETE",
):
    if token not in host_probe:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing read-only host probe: " + token)
for token in ("GENERIC_WRITE", "CREATE_ALWAYS", "CREATE_NEW", "TRUNCATE_EXISTING"):
    if token in host_probe:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: mutating host probe: " + token)
if '"UNSAFE_RAW_DESTINATION_ROOT"' not in batch_rules_guard:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: unsafe root lacks native status")
if "djmeta::qualify_physical_selection(evidence)" not in batch_rules_guard:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing physical-identity collision gate")
for token in (
    "inspect_selected_raw_targets(dialog, *state)",
    "probe_host_file_readonly(raw_candidate)",
    "reset_raw_target_observation(entry)",
    "input.filesystem_target_checked = false; // invariant: final target unknown",
    "raw_relative_path_lexically_safe(entry.input.raw_relative_path)",
    "raw_destination_root_lexically_safe(entry.input.destination_root)",
    "Physical source changed during raw destination inspection. Reopen Prepare Tracks.",
    "stable_raw_target_observation(result.observed, current)",
    "Raw destination changed during filesystem inspection. Refresh preview.",
):
    if token not in batch_rules_guard:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing raw candidate isolation: " + token)

planner = (foobar / "titleformat_planner.cpp").read_text(encoding="utf-8")
for token in [
    "titleformat_compiler::get()",
    "compiled->run_simple",
    "__meta_add_unsafe_ex",
    "meta_add_value_ex",
]:
    if token not in planner:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: host-native planner contract missing " + token)
if ("TitleformatBatchEvaluator" not in planner or
        "titleformat_compiler::get()->compile" not in planner):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing host titleformat cache")
if "TitleformatBatchEvaluator formatter;" not in batch_rules_guard:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing native preview batch formatter reuse")
if "TitleformatBatchEvaluator formatter;" not in (foobar / "routing_preview.cpp").read_text(encoding="utf-8"):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing route preview formatter reuse")
for forbidden in [
    "$if(",
    "%artist%",
    "std::filesystem",
    "MoveFile",
    "CopyFile",
    "filesystem::g_move",
    "filesystem::g_copy",
]:
    if forbidden in planner:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: planner bypasses host-native read-only boundary: " + forbidden)

route_menu = (foobar / "context_menu.cpp").read_text(encoding="utf-8")
menu_settings = (foobar / "menu_settings.cpp").read_text(encoding="utf-8")
menu_preferences = (foobar / "menu_preferences.cpp").read_text(encoding="utf-8")
prepare_dialog = (foobar / "prepare_dialog.cpp").read_text(encoding="utf-8")
batch_table_dialog = (foobar / "batch_preview_dialog.cpp").read_text(encoding="utf-8")

for token in (
    "prefill_musicbrainz_query(", "state->auto_search_track",
    "suggested_query?local_artist", "IDC_BATCH_GUIDE", "IDC_BATCH_HELP",
    "install_workspace_tooltips(", "TOOLTIPS_CLASSW", "TTM_ADDTOOLW",
    "show_workspace_help(", "header->code == NM_DBLCLK",
    "MAKEWPARAM(IDC_METADATA_MB_SEARCH,BN_CLICKED)",
    "search_attempt_notice", "recording_search_plan(", "TTDT_AUTOPOP",
    "TTM_POP", "align_native_preview_form(state.dialog)",
    'L"Reload the tracks already open in Music Metadata Studio?\\n\\n"',
):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: guided search, help or refresh text missing " + token)

# Modeless must be a genuine owned-lifetime foobar workspace. Reject a cosmetic
# minimize flag on a blocking modal dialog, stranded state or unpaired manager.
for token in ("CreateDialogParamW(", "modeless_dialog_manager::g_add(dialog)",
              "modeless_dialog_manager::g_remove(dialog)", "WM_NCDESTROY",
              "DestroyWindow(dialog)", "g_pending_state", "g_workspace",
              "same_workspace_selection", "revalidate_workspace",
              "refresh_workspace_snapshot", "initquit_factory_t<workspace_shutdown>",
              "IDC_BATCH_REFRESH", "observed_source_guard"):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: modeless/stale lifecycle missing " + token)
for forbidden in ("DialogBoxParamW(", "EndDialog("):
    if forbidden in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: blocking modal preview " + forbidden)

layout_storage = (foobar / "batch_table_settings.cpp").read_text(encoding="utf-8")
route_preview = (foobar / "routing_preview.cpp").read_text(encoding="utf-8")
legacy_profiles = (foobar / "legacy_routing_profiles.h").read_text(encoding="utf-8")
resources = (foobar / "component.rc").read_text(encoding="utf-8")
if "WS_MAXIMIZEBOX" not in resources:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: native batch dialog lost maximizer")


for label, source, token in [
    ("default Singles caption", menu_settings, "Prepare: Singles (Preview)"),
    ("default Alben caption", menu_settings, "Prepare: Albums (Preview)"),
    ("default Livesets caption", menu_settings, "Prepare: Live Sets (Preview)"),
    ("default main caption", menu_settings, "Prepare Tracks (Preview)"),
    ("cfg-backed captions", menu_settings, "cfg_string"),
    ("stable individual caption GUID", menu_settings, "guid_caption_primary"),
    ("dynamic captions", route_menu, "effective_menu_caption(index)"),
    ("new main route command", route_menu, "guids::prepare_tracks"),
    ("main dialog dispatch", route_menu, "show_prepare_tracks_dialog(retained)"),
    ("menu count", route_menu, "get_num_items() override { return 5; }"),
    ("foobar menu DEFAULT_ON", route_menu, "contextmenu_item::DEFAULT_ON"),
    ("foobar menu DEFAULT_OFF", route_menu, "contextmenu_item::DEFAULT_OFF"),
    ("host cfg Preferences", menu_preferences, "preferences_page_v3"),
    ("host Apply", menu_preferences, "void apply() override"),
    ("host Reset", menu_preferences, "void reset() override"),
    ("host staged changes", menu_preferences, "preferences_state::changed"),
    ("host dark-mode Preferences", menu_preferences, "preferences_state::dark_mode_supported"),
    ("native prefs dialog", resources, "IDD_MENU_PREFERENCES DIALOGEX"),
    ("unified single window", prepare_dialog, "show_batch_preview_dialog(handles, choice)"),
    ("real metadata proposals", batch_table_dialog, "djmeta::describe_metadata_diffs"),
    ("one row per physical selected track", batch_table_dialog, "djmeta::summarize_track_changes"),
    ("track list as production widget", batch_table_dialog, "IDC_METADATA_TRACK_LIST"),
    ("track master selection", batch_table_dialog, "LVN_ITEMCHANGED"),
    ("selection resolves source identity", batch_table_dialog, "native_selected_track_change("),
    ("track-specific detail", batch_table_dialog, "djmeta::selected_track_diffs"),
    ("music versus extended focus", batch_table_dialog, "MetadataFocus::Music"),
    ("extended/custom fields reachable", batch_table_dialog, "MetadataFocus::All"),
    ("track list sorted independently", batch_table_dialog, "sort_track_summaries"),
    ("actual tag diff provenance tooltip", batch_table_dialog, "LVN_GETINFOTIPW"),
    ("metadata routing fields hidden", batch_table_dialog, "IDC_BATCH_ROUTE_LABEL"),

    ("metadata and file tab control", batch_table_dialog, "TCN_SELCHANGE"),
    ("metadata diff view", batch_table_dialog, "IDC_METADATA_LIST"),
    ("metadata safety", batch_table_dialog, "item.safety"),
    ("metadata rule provenance in production tooltip", batch_table_dialog, "entry.rule_ids"),
    ("metadata sort", batch_table_dialog, "sort_metadata_diff_rows"),
    ("source path presentation only", batch_table_dialog, "display_file_path"),
    ("unified tabs resource", resources, "IDC_BATCH_TABS"),
    ("virtualized batch table resource", resources, "IDD_BATCH_PREVIEW DIALOGEX"),
    ("virtualized ListView", batch_table_dialog, "ListView_SetItemCountEx"),
    ("header drag/drop", batch_table_dialog, "LVS_EX_HEADERDRAGDROP"),
    ("sort on header click", batch_table_dialog, "LVN_COLUMNCLICK"),
    ("virtual view identity mapping", batch_table_dialog, "state->view_order[view_index]"),
    ("selected row identity", batch_table_dialog, "state.view_order[static_cast<std::size_t>(index)]"),
    ("persistent column order", batch_table_dialog, "ListView_GetColumnOrderArray"),
    ("persistent column widths", batch_table_dialog, "ListView_GetColumnWidth"),
    ("column visibility", batch_table_dialog, "visible_mask"),
    ("column header popup", batch_table_dialog, "show_column_menu"),
    ("reliable header submenu", batch_table_dialog, "SetWindowSubclass"),
    ("header submenu teardown", batch_table_dialog, "RemoveWindowSubclass"),
    ("resizable table layout", batch_table_dialog, "resize_batch_dialog"),
    ("resize minimum", batch_table_dialog, "WM_GETMINMAXINFO"),
    ("reset column layout", batch_table_dialog, "Reset column layout"),
    ("layout settings read", batch_table_dialog, "load_batch_table_layout()"),
    ("layout settings persist", batch_table_dialog, "store_batch_table_layout(state->layout)"),
    ("foobar profile layout cfg", layout_storage, "cfg_string"),
    ("batch table model", batch_table_dialog, "djmeta::describe_batch_preview"),
    ("group route override", batch_table_dialog, "apply_to_rows(dialog, *state, true)"),
    ("selected route override", batch_table_dialog, "apply_to_rows(dialog, *state, false)"),
    ("selection stale guard", batch_table_dialog, "verify_snapshot(entry)"),
    ("per-batch atomic UI edits", batch_table_dialog, "state.entries = std::move(candidate)"),
    ("unverified CUE gate", batch_table_dialog, "entry.input.cue_dependencies_checked = false"),
    ("unverified destination gate", batch_table_dialog, "entry.input.filesystem_target_checked = false"),
    ("safe-only projection", route_preview, "djmeta::stage_safe_only"),
    ("foobar titleformat projection", route_preview, "formatter.evaluate("),
    ("stale metadata guard", route_preview, "djmeta::fingerprint(latest)"),
    ("subsong physical guard", route_preview, "get_subsong_index() != 0"),
    ("raw target duplicate warning", route_preview, "raw_target_counts"),
    ("no-write banner", route_preview, "No tags were written"),
]:
    if token not in source:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing " + label)

# Product policy: every built-in user-visible label must be English.
# Original media tags / externally supplied routing profile values may
# legitimately use other languages and must never be transliterated.
non_english_ui_literals = [
    "Metadaten normalisieren", "Tracks vorbereiten",
    "Vorbereiten:", "Die Vorschau", "Es wurden keine",
    "Zielordner:", "Dateinamensmuster:",
]
for ui_file in ("preview.cpp", "routing_preview.cpp", "prepare_dialog.cpp",
                "context_menu.cpp", "menu_preferences.cpp", "menu_settings.cpp",
                "component.rc"):
    text = (foobar / ui_file).read_text(encoding="utf-8")
    for forbidden_ui in non_english_ui_literals:
        if forbidden_ui in text:
            raise SystemExit("STATIC FOOBAR AUDIT FAIL: non-English UI in " +
                             ui_file + ": " + forbidden_ui)

# The third preview tab must be a read-only evidence view. Every clipboard
# comparison revalidates physical source and rules; it must never reach
# the normalizer approval handler or perform a provider network request.
for label, token in (
    ("guided read-only search tab", 'L"1. Find & compare"'),
    ("on-demand CUE button", "IDC_METADATA_INSPECT_CUE"),
    ("read-only host CUE reader", "read_cue_raw_on_demand(entry.handle, entry.input.source_path)"),
    ("raw CUE parser", "djmeta::inspect_cue_metadata(raw.raw_text, raw.carrier)"),
    ("cue inventory presentation", "cue_inventory_read_only"),
    ("clipboard command", "IDC_METADATA_IMPORT_CANDIDATE"),
    ("bounded manual parser", "djmeta::online::parse_manual_candidate(text)"),
    ("CUE candidate scope identity", "state->cue_candidate_comparison_mode"),
    ("CUE candidate manual review", "djmeta::online::review_manual_cue_candidate("),
    ("Beatport intentional clipboard import", "is_beatport_user_json_clipboard(text)"),
    ("Beatport bounded user JSON parser", "parse_beatport_user_json_clipboard(text)"),
    ("Beatport visible fact paste", "is_beatport_user_facts_clipboard(text)"),
    ("Beatport bounded fact parser", "parse_beatport_user_facts_clipboard(text)"),
    ("Beatport physical identity guard", "review_beatport_user_track("),
    ("Beatport refuses virtual CUE assignment", "Beatport track import has no qualified CUE ordinal"),
    ("MusicBrainz result group detail", "group_musicbrainz_release_rows("),
    ("MusicBrainz one row per hit", "state.candidate_rows=std::move(rows.summary)"),
    ("MusicBrainz separate field details", "refresh_musicbrainz_detail_rows(state)"),
    ("MusicBrainz stable candidate id", "state->candidate_rows[rowid].candidate.source_id"),
    ("position/mode restored", "load_batch_preview_window_placement()"),


    ("user clicked official MusicBrainz search", "id == IDC_METADATA_MB_SEARCH && HIWORD(wp) == BN_CLICKED"),
    ("official lookup is separate user action", "id == IDC_METADATA_MB_LOAD_RELEASE && HIWORD(wp)==BN_CLICKED"),
    ("fixed-host reader", "fetch_musicbrainz_json_readonly(route,cancel)"),
    ("MusicBrainz background request", "begin_musicbrainz_network(dialog,*state"),
    ("MusicBrainz main-thread completion", "consume_musicbrainz_network(dialog,*state)"),
    ("MusicBrainz source revalidation", "publish_musicbrainz_search(state,*request,job->response)"),
    ("MusicBrainz edition revalidation", "publish_musicbrainz_release(state,*request,job->response)"),
    ("MusicBrainz release selection stale check", "MusicBrainz release selection changed during lookup."),
    ("No detached MusicBrainz host worker", "state->musicbrainz_request.reset()"),
    ("official Discogs search action", "perform_discogs_search(dialog,*state)"),
    ("official Discogs release action", "perform_discogs_load(dialog,*state)"),
    ("Discogs auth only from runtime password field", "read_control(dialog,IDC_METADATA_DISCOGS_TOKEN)"),
    ("Discogs URL constructed by provider", "release_path(direct_id)"),
    ("Discogs physical barcode search", "local_barcode=hints.barcode;"),
    ("Discogs physical catalog search", "local_catalog_number=hints.catalog_number;"),
    ("Discogs physical mix identity", "local_version=hints.version;"),
    ("Discogs mix-aware official search", "auto_query?local_version:std::string{}"),
    ("Discogs staged edition identifier search", "auto_query?local_barcode:std::string{}"),
    ("Discogs physical/CUE album search wiring", "auto_query?local_album:std::string{}"),
    ("Discogs physical album tag inventory", "local_album=hints.album;"),
    ("Discogs CUE global album provenance", "local_album=hints.album; // Qualified CUE-global album"),
    ("Discogs source rank separation", "discogs_live_master_not_exact_edition"),
    ("Discogs CUE edition matcher", "review_cue_release_edition(cue,edition)"),
    ("Discogs CUE stale check", "CUE changed during Discogs release lookup."),
    ("Discogs official HTTPS worker", "begin_discogs_network(dialog,state"),
    ("Discogs worker completion on main thread", "consume_discogs_network(dialog,*state)"),
    ("Discogs network results revalidated", "publish_discogs_search(dialog,state,*context,job->response)"),
    ("No detached WinHTTP worker", "state->discogs_request.reset()"),
    ("Discogs production physical comparison", "originals,edition,physical_duration_ms)"),
    ("Discogs host physical duration", "info->info().get_length()"),
    ("official MusicBrainz parser", "djmeta::online::musicbrainz::parse_search(\n        reply,request.kind)"),
    ("no stale online match", "CUE changed during online search"),
    ("provider-origin source label", "online_evidence_origin_label(item)"),
    ("Discogs edition grouping", '"discogs_live_release_group"'),
    ("Discogs exact URL opens compare", "publish_discogs_release(dialog,state,exact_request,"),
    ("Discogs official search relevance preserved", "state.candidate_view_order=native_candidate_view_order("),
    ("Discogs selected release load gate", "update_online_release_load_button(*state)"),
    ("Modeless context edit cancels provider", "invalidate_online_request(*state)"),
    ("Discogs stale reply silently discarded", "state.online_context_version.matches(context->context_version)"),
    ("MusicBrainz stale reply silently discarded", "state.online_context_version.matches(request->context_version)"),
    ("Discogs async exact release selection", "selected_exact_release_id("),
    ("Cancel delegates to safe asynchronous close", "SendMessageW(dialog,WM_CLOSE,0,0)"),
    ("Discogs stale candidate rejection", "Discogs release selection changed during lookup."),
    ("native field provenance tooltip", "header->idFrom == IDC_METADATA_MB_DETAILS &&"),
    ("native field evidence scope tooltip", "online_evidence_scope_label("),
    ("native field date meaning tooltip", "online_evidence_date_label("),
    ("native overflow disclosure", 'L"Additional fields not displayed"'),

    ("provider origin helper consulted", "online_evidence_origin_label(item)"),
    ("unverified provider tooltip", 'L"Source: manual paste, provider unverified"'),

    ("CUE source re-read on import", "const auto cue = djmeta::inspect_cue_metadata(raw.raw_text, raw.carrier)"),
    ("no virtual fallback", "read_cue_raw_on_demand(entry.handle, entry.input.source_path)"),

    ("field evidence review", "djmeta::online::review_online_fields("),
    ("pre-import source check", "verify_snapshot(entry)"),
    ("pre-import rules check", "verify_rules_snapshot(state->captured_rules)"),
    ("CUE path rejection", "is_external_cue_locator(entry.input.source_path)"),
    ("candidate dedicated order", "candidate_sort_column"),
    ("candidate source-specific tooltip", "state->candidate_view_order[row]"),
    ("physical qualification", "entry.input.physical_source_qualified"),
    ("candidate guard", "if (state->show_candidate && (native_command == PreviewCommand::Accept"),
):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing read-only candidate gate: " + label)
for token in ("IDC_METADATA_MB_QUERY", "IDC_METADATA_MB_SEARCH", "IDC_METADATA_MB_LOAD_RELEASE",
              "IDC_METADATA_ONLINE_PROVIDER", "IDC_METADATA_DISCOGS_TOKEN"):
    if token not in resources:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing official MusicBrainz control: " + token)
# Async official Discogs worker: network-only callable, posted HWND signal,
# UI-thread CUE/source/rules rechecks and owner-joined DLL lifetime.
transport=(foobar / "readonly_network_request.h").read_text(encoding="utf-8")
for token in ("std::jthread", "PostMessageW(notify,kReadOnlyNetworkDoneMessage,0,0)",
              "std::memory_order_release", "std::memory_order_acquire",
              "if(worker.joinable()) worker.join()"):
    if token not in transport:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: native HTTP worker lifecycle missing: "+token)
if "detach(" in transport or "metadb_handle" in transport or "get_info_ref" in transport:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: network worker lifetime or SDK access unsafe")

# Native artist-only search requires an independently qualified local
# ARTIST/PERFORMER and passes only the canonical, read-only Discogs filter.
discogs_provider = (root / "include" / "djmeta" / "discogs_provider.h").read_text(encoding="utf-8")
for token in ("artist_search_path(", "if(!artist.empty())",
              'add("Artist-only release discovery (broad; verify edition)"',
              "if(artist_only)", "path==artist_search_path(validated_term(term))"):
    if token not in discogs_provider:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: Discogs artist-only guard missing: "+token)
if "local_catalog_number.empty() && local_artist.empty()" not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: artist-only native search still blocked")
# Paginated search must be a separate opt-in native command with its
# own source/rules/CUE guards, strictly canonical page path and UI lifetime.
for token in ("perform_discogs_next_page(", "search_result_page_path(",
              "parse_search_result_page(reply,request.result_page)",
              "state.discogs_result_page<state.discogs_result_pages",
              "request.cue_before=raw.raw_text;",
              "request.result_first_path=state.discogs_result_first_path;",
              "IDC_METADATA_DISCOGS_NEXT_PAGE",
              "state.search_plan_identity==state.discogs_result_identity"):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: native Discogs pagination gate missing: "+token)
discogs_provider=(root / "include" / "djmeta" / "discogs_provider.h").read_text(encoding="utf-8")
for token in ("max_search_result_page=100000",
              "allowed_readonly_path(first);",
              "parse_search_result_page(", "object_id(*per_page)!=20",
              "search_result_page_path("):
    if token not in discogs_provider:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: unsafe provider paging: "+token)
if "IDC_METADATA_DISCOGS_NEXT_PAGE" not in resources:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: no native Discogs paging control")
if 'PUSHBUTTON      "Next results page", IDC_METADATA_DISCOGS_NEXT_PAGE' not in (
        foobar / "component.rc").read_text(encoding="utf-8"):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing real Windows pager button")
# Verify the WinHTTP worker respects Discogs pacing between automatic
# zero-hit fallback stages; the former immediate exception broke the workflow.
discogs_http=(foobar / "discogs_http.cpp").read_text(encoding="utf-8")
for token in ("request_spacing_wait_ms(", "std::this_thread::sleep_for(",
              "reject_cancelled(cancel)", "last_request_tick=tick"):
    if token not in discogs_http:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: staged Discogs worker pacing missing: "+token)
if "Discogs rate limit: try again after the request interval." in discogs_http:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: zero-hit fallback still self-rejects")

# Unconfirmed CUE/Discogs track alignments render source-verified evidence
# only in a separately BLOCKED lane, not qualified candidate fields.
cue_release_review=(root / "include" / "djmeta" / "cue_release_review.h").read_text(encoding="utf-8")
for token in ("unverified_fields", "cue_unverified_alignment_evidence_only_",
              "field.state=FieldReviewState::Blocked"):
    if token not in cue_release_review:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing blocked CUE evidence guard: "+token)
if "item.unverified_fields" not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing native blocked CUE evidence")
# Optional physical/CUE search hints must abstain on ambiguous values,
# not prevent a user-entered exact Discogs URL from reaching the official API.
hint_header=(root / "include" / "djmeta" / "discogs_search_hints.h").read_text(encoding="utf-8")
for token in ("physical_search_hints(", "cue_search_hints(",
              "unique_discogs_physical_tag(source,name)",
              "if(found) return {};", "validated_term(*value)",
              "hints.album=hints.title;"):
    if token not in hint_header:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: Discogs optional source hint guard missing: "+token)
for token in ("physical_search_hints(originals)",
              "cue_search_hints(cue)", "release_id_from_url(term)"):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: native Discogs URL/hint chain missing: "+token)
# Every original CUE token must stay source-only in both native lanes.
cue_inventory_header=(root / "include" / "djmeta" / "cue_original_inventory.h").read_text(encoding="utf-8")
for token in ("cue_original_inventory_rows(", "cue-line:", "row.original_values.push_back(",
              "row.state = FieldReviewState::Unchanged", "CUE original inventory exceeds"):
    if token not in cue_inventory_header:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: unsafe CUE original inventory: "+token)
for token in ('cue_original_inventory_rows(cue,"CUE Original / ")',
              "is_local_cue_inventory(row)", "Keep original CUE",
              "cue_original_inventory_rows(inventory)"):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: CUE original provenance missing in native UI: "+token)

# Never relabel original-only physical source fields as provider evidence
# in Discogs/MusicBrainz/Beatport comparison or native release grouping.
origin_header=(root / "include" / "djmeta" / "online_fields.h").read_text(encoding="utf-8")
for token in ("make_local_physical_original(", "is_local_physical_original(",
              'return "Local physical"', '"physical-original"'):
    if token not in origin_header:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: physical source provenance missing: "+token)
discogs_review=(root / "include" / "djmeta" / "discogs_release_review.h").read_text(encoding="utf-8")
if 'retained.field="Physical / "' not in discogs_review:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: physical tags mixed with edition group")
for token in ("local_physical_group", "is_local_physical_original(row)",
              "is_local_physical_original(item)",
              "append_local_physical_inventory("):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: native local source grouping missing: "+token)

# No 50-row truncation without disclosure in the provider search detail.
for token in ("append_local_physical_inventory(detail,original_tags)",
              "append_local_physical_inventory(details,originals)",
              "local_physical_inventory_overflow", "More original tags"):
    sources_to_check=batch_table_dialog+(root / "include" / "djmeta" / "online_fields.h").read_text(encoding="utf-8")
    if token not in sources_to_check:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: original tag inventory not fully disclosed: "+token)
if "if(detail.size()>=50) break;" in batch_table_dialog or    "if(details.size()>=50) break;" in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: silent 50-row source tag truncation")

# The fallback is implemented in the shared review-row provenance helper.
# The UI must not need an obsolete inline "Pasted:" concatenation.
origin_model=(root / "include" / "djmeta" / "online_fields.h").read_text(encoding="utf-8")
if 'return "Pasted: " + row.candidate.provider;' not in origin_model or \
   'row.reason.starts_with("discogs_live_")' not in origin_model or \
   'row.reason.starts_with("musicbrainz_live_")' not in origin_model:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing provider-aware origin fallback")
# Multiline native tooltips must have actual newline escape sequences, not
# visible backslash-n text from overescaped C++ string literals.
if r'L"\\nSource line / provider ID: "' in batch_table_dialog or \
   r'L"\\nScope: "' in batch_table_dialog or \
   r'L"\\nDate meaning: "' in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: tooltip contains literal backslash-n")
if r'L"\nSource line / provider ID: "' not in batch_table_dialog or \
   r'L"\nScope: "' not in batch_table_dialog or \
   r'L"\nDate meaning: "' not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: source provenance tooltip lacks multiline details")

# User-supplied Beatport JSON is parsed OFFLINE. It must not acquire an
# unauthorized live fetcher, a cached token or a virtual CUE write route.
beatport_manual = (root / "include" / "djmeta" / "beatport_manual_intake.h").read_text(encoding="utf-8")
for token in ("@beatport-track-json", "parse_track_object(root)",
              "manual_beatport_identity_unverified",
              "online_evidence_origin_label"):
    if token == "online_evidence_origin_label":
        if token not in (root / "include" / "djmeta" / "online_fields.h").read_text(encoding="utf-8"):
            raise SystemExit("STATIC FOOBAR AUDIT FAIL: manual Beatport origin guard missing")
    elif token not in beatport_manual:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: manual Beatport safety missing: " + token)
if "fetch_beatport" in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: unauthorized Beatport network client")

# Explicit trusted-host read-only transport is permitted; generic web scraping
# or transport in core is NOT. Do not overfit the previous offline-only policy.
online_transport = (foobar / "musicbrainz_http.cpp").read_text(encoding="utf-8")
for token in ("WinHttpOpen(", "WINHTTP_FLAG_SECURE", 'L"musicbrainz.org"',
              "WINHTTP_OPTION_REDIRECT_POLICY_NEVER", "GENERIC_WRITE"):
    if token == "GENERIC_WRITE":
        if token in online_transport:
            raise SystemExit("STATIC FOOBAR AUDIT FAIL: MusicBrainz adapter contains a writer")
    elif token not in online_transport:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: official MusicBrainz network boundary: " + token)
discogs_physical = (root / "include" / "djmeta" / "discogs_release_review.h").read_text(encoding="utf-8")
for token in ("review_online_fields(originals,edition.fields)",
              "discogs_local_only_no_provider_value",
              "discogs_live_release_track_unassigned",
              "discogs_live_match_requires_review",
              "discogs_live_match_other_or_conflicting_track",
              "MATCH_DIAGNOSTIC",
              "discogs_live_release_unverified_track_provenance",
              "discogs_live_release_verified_track_",
              "link.decision==MatchDecision::Suggested",
              "link.conflicts.empty()"):
    if token not in discogs_physical:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing Discogs physical review safety: " + token)
if "IDC_METADATA_INSPECT_CUE" not in resources:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing read-only CUE inspect control")
if 'GENERIC_READ' not in sources or 'OPEN_EXISTING' not in sources:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: CUE reader must use existing read-only file")
if "IDC_METADATA_IMPORT_CANDIDATE" not in resources:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing native clipboard import control")
if "online_intake.h" not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: candidate parser not linked to foobar UI")

if "IDC_METADATA_TRACK_LIST" not in resources or "IDC_METADATA_FILTER" not in resources:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: track master/metadata filter missing from production resources")
if "ShowWindow(state.metadata_track_list" not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: master/detail tab visibility not wired")
if "IDD_PREPARE_TRACKS DIALOGEX" in resources:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: obsolete two-dialog wizard remains")
if "contextmenu_item::FORCE_OFF" in route_menu:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: host visibility disabled")
for token in [
    r"Z:\Music\Singles",
    r"Z:\Music\Alben",
    r"Z:\Music\Livesets",
    "%album artist%/%album%/%artist% - %title%",
    "%album artist%/%album%[ '('%date%')']/%tracknumber%. %artist% - %title%",
]:
    if token not in legacy_profiles:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: legacy route fixture drift: " + token)
for unsafe in [
    "update_info_async", "GENERIC_WRITE", "MoveFile(", "CopyFile(",
    "DeleteFile(", "filesystem::g_move", "filesystem::g_copy",
]:
    if unsafe in route_preview or unsafe in prepare_dialog or unsafe in menu_preferences or unsafe in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: preview includes write path: " + unsafe)
for path in [
    r"src\foobar\routing_preview.cpp",
    r"src\foobar\batch_preview_dialog.cpp",
    r"src\core\batch_preview.cpp",
    r"src\core\table_layout.cpp",
    r"src\core\track_review.cpp",
    r"src\core\review_decisions.cpp",
    r"src\foobar\batch_table_settings.cpp",
    r"src\foobar\prepare_dialog.cpp",
    r"src\foobar\menu_settings.cpp",
    r"src\foobar\menu_preferences.cpp",
    r"src\core\staging.cpp",
    r"src\core\routing_overrides.cpp",
]:
    if path not in project:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: source missing from Win32/x64: " + path)

sdk_bootstrap = (root / "scripts" / "bootstrap-sdk.ps1").read_text(encoding="utf-8")
for token in (
    "2026-10-01", "793738",
    "d4c55077336fae81bf8df0259b5b2748fa45ea84132c656ead93eb123cbcdc26",
    "Get-FileHash", "SHA256", "Official SDK 7z integrity check",
    "sdk-readme.html"
):
    if token not in sdk_bootstrap:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: verified official SDK pin missing: " + token)

rules_runtime = (foobar / "rules_runtime.cpp").read_text(encoding="utf-8")
if "CreateFileW" not in rules_runtime or "GENERIC_READ" not in rules_runtime:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: shared rules are not opened read-only")
if "CREATE_ALWAYS" in rules_runtime or "OPEN_ALWAYS" in rules_runtime:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: rules runtime contains a file-creation path")

# The GUI contract is backed by an actual Windows HWND runtime test.
# Static checks here only verify that production uses the tested adapter.
native_controls = (foobar / "native_preview_controls.h").read_text(encoding="utf-8")
gui_test = (root / "tests" / "native_preview_controls_tests.cpp").read_text(encoding="utf-8")
if 'IDC_BATCH_PROFILE_NAME' in resources or 'IDC_BATCH_NAME_LABEL' in resources:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: duplicate route profile input still instantiated")
if 'CBS_DROPDOWN | CBS_AUTOHSCROLL' not in resources:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: routing combo is not editable")
if 'read_control(window, IDC_BATCH_PROFILE_PICKER)' not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: custom route picker not used for profile name")
if 'align_native_preview_form(dialog)' not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: common layout adapter not wired")
if 'GetComboBoxInfo' not in native_controls or 'CreateDialogParamW' not in gui_test:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: native runtime resource/alignment test missing")

if 'IDC_METADATA_TRACK_FILTER' not in resources or 'IDC_METADATA_TRACK_FILTER_LABEL' not in resources:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: track discovery widgets missing")
if 'native_command == PreviewCommand::TrackFilterChanged' not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: track discovery control event not handled")
if 'djmeta::filter_track_view' not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: filtered master data not wired")
if '{IDC_METADATA_TRACK_FILTER_LABEL, IDC_METADATA_TRACK_FILTER}' not in native_controls:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: native status filter alignment not wired")
if 'native_preview_command(message, wp)' not in batch_table_dialog or 'native_preview_command(message, wp)' not in gui_test:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: actual production event decoder not shared with resource-backed GUI test")
if 'native_selected_track_change(' not in batch_table_dialog or 'native_selected_track_change(' not in gui_test:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: actual track selection notification not exercised with shared event adapter")
if 'selected_native_view_ids(' not in batch_table_dialog or 'restore_native_view_selection(' not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: production sort/review does not use tested stable selection adapter")
for token in ['load_track_grid_layout()', 'load_detail_grid_layout()',
              'store_track_grid_layout(state->track_grid)',
              'store_detail_grid_layout(state->detail_grid)',
              'show_review_grid_column_menu(', 'capture_review_grid_controls(',
              'apply_review_grid_controls(', 'show_review_grid_sort_arrow(']:
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: review grid production persistence/control missing: " + token)
for token in ['apply_review_grid_controls(', 'capture_review_grid_controls(',
              'ListView_GetColumnOrderArray', 'ListView_GetColumnWidth']:
    if token not in gui_test:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: review grid real HWND acceptance missing: " + token)
for token in ('apply_native_preview_resize(', 'GetProcAddress(user32, "GetDpiForWindow")', 'state->active_dpi', 'WM_DPICHANGED',
              'restore_batch_dialog_window_size(*state)', 'save_batch_dialog_window_size(*state)',
              'WM_EXITSIZEMOVE', 'load_batch_preview_window_size()'):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: production responsive per-monitor layout missing " + token)
if 'apply_native_preview_resize(' not in gui_test or 'review_split_geometry(' not in gui_test:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: actual resized HWND geometry test absent")
if 'WS_CLIPCHILDREN' not in resources or 'WS_CLIPSIBLINGS' not in resources:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: resize paint clipping removed")
if 'BeginDeferWindowPos(' not in native_controls or 'DeferWindowPos(' not in native_controls:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: native atomic resize adapter missing")
if 'for (int dpi : {96, 120, 144, 192})' not in gui_test:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: DPI runtime column checks absent")
for token in ("IDC_METADATA_VISIBLE_WHITESPACE", "preview_whitespace_text(",
              "PreviewCommand::VisibleWhitespaceChanged", "state->show_whitespace"):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: production visible-whitespace view missing " + token)
if 'AUTOCHECKBOX    "Show whitespace"' not in resources:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: visible-whitespace real RC checkbox missing")
for token in ("preview_whitespace_text(", "IDC_METADATA_VISIBLE_WHITESPACE",
              "VisibleWhitespaceChanged", "[NBSP]", "[ZWSP]"):
    if token not in gui_test:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: visible-whitespace runtime GUI test missing " + token)
if 'LVN_ODFINDITEMW' not in batch_table_dialog or 'find_native_track_prefix(' not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: owner-data incremental search not wired to production WM_NOTIFY")
if 'cached_track_names.push_back' not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: search not backed by immutable source display names")
for token in ('NMLVFINDITEMW', 'find_native_track_prefix(', 'LVFI_WRAP'):
    if token not in gui_test:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: actual resource-backed incremental search test missing " + token)
if 'project_review_decisions(' not in batch_table_dialog or 'verify_snapshot(entry)' not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: actual review projection lacks analysis/stale gate")
if 'formatter.evaluate(' not in batch_table_dialog or 'entry.route_expression' not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: reviewed stage does not reevaluate its own route expression")
if 'LVNI_SELECTED' not in batch_table_dialog or 'diff.proposal_index' not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: review action target does not preserve proposal index")
if 'IDC_METADATA_REVIEW_SCOPE' not in resources or 'IDC_METADATA_MANUAL_INPUT' not in resources:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: real review scope/manual editor missing")
for token in ['IDC_METADATA_ACCEPT', 'IDC_METADATA_REJECT',
              'IDC_METADATA_RESET', 'IDC_METADATA_USE_VALUE']:
    if token not in resources or token not in batch_table_dialog or token not in gui_test:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: native review action not wired: " + token)
print("PASS: foobar preview is analysis-only; no tag/file write path is present")

# Four live-provider publication chains must recheck physical file identity
# as well as foobar's cached tags. No source-wide scan per remote response,
# and no stale warning may be overwritten by post-network UI refresh.
for token in (
    "void verify_online_source_snapshot(PreviewState& state, const PreviewEntry& entry)",
    "if (!entry.observed_source_guard.empty() ||",
    "latest.host_physical_key != entry.observed_physical_key",
    "latest.source_guard != entry.observed_source_guard",
    "mark_stale_workspace(state);",
    "if(state.stale) return; // Preserve disabled controls and stale warning.",
    "if(state.stale) return; // Never re-enable actions after file replacement.",
):
    if token not in batch_rules_guard:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing online file snapshot gate: " + token)

for function in (
    "publish_discogs_search", "publish_discogs_release",
    "publish_musicbrainz_search", "publish_musicbrainz_release",
):
    marker = "void " + function + "("
    if marker not in batch_rules_guard:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: no online publisher " + function)
    section = batch_rules_guard.rsplit(marker, 1)[1].split("\nvoid ", 1)[0]
    if section.count("verify_online_source_snapshot(state, entry);") != 2:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: incomplete disk recheck " + function)
    if "verify_snapshot(entry);" in section:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: metadb-only provider publish " + function)

# Only the explicitly invoked, environment-gated canonical Studio branch may
# receive the Discogs QA secret; PR, fork, push, and regular workflow runs
# must remain credential-free. No manual run may publish a draft artifact.
workflow = (root / ".github" / "workflows" / "foobar.yml").read_text(encoding="utf-8")
if "pull_request_target:" in workflow or workflow.count(
    "secrets.MMS_DISCOGS_QA_TOKEN"
) != 1:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: unsafe Discogs secret propagation")
if "  workflow_dispatch:\n    inputs:" not in workflow:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing explicit manual QA trigger")
if "  discogs-authorized-native:\n" not in workflow:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing separated Discogs QA job")
public_jobs, protected_job = workflow.split("  discogs-authorized-native:\n", 1)
protected_job = protected_job.split("\n  # Internal PRs", 1)[0]
if "secrets.MMS_DISCOGS_QA_TOKEN" in public_jobs:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: Discogs secret exposed to PR jobs")
for token in (
    "github.event_name == 'workflow_dispatch'",
    "inputs.discogs_live == true",
    "github.ref == 'refs/heads/feature/online-metadata-discovery-foundation-20261009'",
    "environment: discogs-qa",
    "FB2K_DISCOGS_QA_TOKEN:",
    "secrets.MMS_DISCOGS_QA_TOKEN",
    "authorized-discogs-probe: 'true'",
):
    if token not in protected_job:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing protected live QA guard: " + token)
if workflow.count(
    "lxsdd/dev-infrastructure-public/.github/actions/foobar-host-smoke@"
    "9005a3c160bcafb2c1e8d318558133dad5536090"
) != 2:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: mismatched protected/ordinary host pins")
if workflow.count(
    "github.event_name == 'push' && github.ref == 'refs/heads/main'"
) != 2:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: manual QA may publish unqualified DLL")

# An external .cue selected as a virtual foobar subsong must still retain
# a physical *read-only* carrier guard. This is independent of its status
# as an ineligible physical audio/tag/CUE write target.
# An explicit double-click/Return on a master track must actually search
# MusicBrainz even when originating from Review/File locations, and must use
# the same one-shot guarded handler as the visible Search button.
track_activation = batch_rules_guard.split(
    "if (header && header->idFrom == IDC_METADATA_TRACK_LIST &&\n"
    "            (header->code == NM_DBLCLK || header->code == LVN_KEYDOWN)) {", 1)[1].split(
    "if (header && header->idFrom == IDC_METADATA_TRACK_LIST &&\n"
    "            header->code == LVN_GETDISPINFOW)", 1)[0]
if (
    "prefill_musicbrainz_query(*state,true);" not in track_activation
    or track_activation.count("MAKEWPARAM(IDC_METADATA_MB_SEARCH,BN_CLICKED)") != 1
    or "already_searching" in track_activation
    or "header->code == LVN_ITEMACTIVATE" in track_activation
):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: track activation must search exactly once from every tab")

# External-CUE inventories may involve slow multi-FILE source probes. All
# distinct references must be rechecked together after the initial scan and
# before publishing even advisory PRESENT/MISSING/ALIAS labels.
cue_inventory = batch_rules_guard.split(
    "void append_external_cue_file_reference_evidence(", 1)[1].split(
    "void inspect_selected_raw_targets(", 1)[0]
if not (
    "for (const auto& [candidate_path, earlier] : probes)" in cue_inventory
    and "probe_host_file_readonly(candidate_path)" in cue_inventory
    and "stable_raw_target_observation(earlier, latest)" in cue_inventory
    and "External CUE FILE dependency changed during read-only inspection." in cue_inventory
    and "observed.host_physical_key == original.host_physical_key" in cue_inventory
    and "NOT QUALIFIED: FILE resolves to the CUE carrier itself" in cue_inventory
    and cue_inventory.index("observed.host_physical_key == original.host_physical_key") <
        cue_inventory.index("observed_ids.insert(observed.host_physical_key)")
    and cue_inventory.index("stable_raw_target_observation(earlier, latest)") <
        cue_inventory.index("const auto current = probe_host_file_readonly(entry.input.source_path);")
):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: CUE FILE dependencies lack atomic read-only recheck")

for token in (
    "std::map<std::string, HostFileObservation> cue_source_probes;",
    "const bool external_cue = is_external_cue_locator(entry.input.source_path);",
    "if (candidate || external_cue)",
    "cue_source_probes.try_emplace(entry.input.source_path)",
    "entry.observed_source_guard = probe.source_guard;",
    "is_external_cue_locator(entry.input.source_path)) {",
    "External CUE lacks a captured physical source guard.",
):
    if token not in batch_rules_guard:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: virtual CUE source guard missing: " + token)
