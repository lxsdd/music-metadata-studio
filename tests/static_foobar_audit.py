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

# Search and release completion for BOTH providers must reject context
# mutations even if the visible query returns to the original text.
if batch_rules_guard.count(
        "state.online_context_version.matches(request.context_version)") != 4:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: four provider epoch gates required")
for token in ("HIWORD(wp)==EN_CHANGE",
              "state->online_context_version.invalidate()",
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
if "djmeta::qualify_physical_selection(evidence)" not in batch_rules_guard:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing physical-identity collision gate")
for token in (
    "inspect_selected_raw_targets(dialog, *state)",
    "probe_host_file_readonly(raw_candidate)",
    "reset_raw_target_observation(entry)",
    "input.filesystem_target_checked = false; // invariant: final target unknown",
    "raw_relative_path_lexically_safe(entry.input.raw_relative_path)",
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
    ("Beatport physical identity guard", "review_beatport_user_track("),
    ("Beatport refuses virtual CUE assignment", "Beatport track JSON has no qualified CUE ordinal"),
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
    ("Discogs physical barcode search", 'unique_discogs_physical_tag(\n            originals,"BARCODE")'),
    ("Discogs physical catalog search", 'unique_discogs_physical_tag(\n            originals,"CATALOGNUMBER")'),
    ("Discogs staged edition identifier search", "auto_query?local_barcode:std::string{}"),
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

# The fallback is implemented in the shared review-row provenance helper.
# The UI must not need an obsolete inline "Pasted:" concatenation.
origin_model=(root / "include" / "djmeta" / "online_fields.h").read_text(encoding="utf-8")
if 'return "Pasted: " + row.candidate.provider;' not in origin_model or \
   'row.reason.starts_with("discogs_live_")' not in origin_model or \
   'row.reason.starts_with("musicbrainz_live_")' not in origin_model:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing provider-aware origin fallback")
# Multiline native tooltips must have actual newline escape sequences, not
# visible backslash-n text from overescaped C++ string literals.
if r'L"\\nProvider ID: "' in batch_table_dialog or \
   r'L"\\nScope: "' in batch_table_dialog or \
   r'L"\\nDate meaning: "' in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: tooltip contains literal backslash-n")
if r'L"\nProvider ID: "' not in batch_table_dialog or \
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
