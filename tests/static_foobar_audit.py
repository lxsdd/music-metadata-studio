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

# The new SDK tag-writer has a distinct audited source boundary. Its
# source may contain update APIs, while the default foobar preview sources
# above MUST remain read-only. No generic FileOps or direct Win32 mutations.
writer = (root / "src" / "writer" / "physical_tag_writer.cpp").read_text(
    encoding="utf-8")
qa = (root / "src" / "writer" / "physical_tag_writer_qa.cpp").read_text(
    encoding="utf-8")
for token in ("file_info_filter::create(", "update_info_async(",
              "physical_identity_unchanged(", "return false;",
              "input_entry::g_open_for_info_read(",
              "verify_frozen_physical_write("):
    if token not in writer:
        raise SystemExit("STATIC SDK WRITE GATE FAIL: " + token)
for token in ("WriteFile(", "DeleteFile(", "MoveFile(", "ReplaceFile(",
              "GENERIC_WRITE", "filesystem::g_open_write"):
    if token in writer:
        raise SystemExit("STATIC SDK WRITE GATE FAIL: bypass " + token)
for token in ("#if defined(MMS_ENABLE_PHYSICAL_WRITES)",
              "MMS_QA_WRITE_SOURCE", "GITHUB_ACTIONS",
              "mms-writer-qa.flac"):
    if token not in qa:
        raise SystemExit("STATIC SDK QA ISOLATION FAIL: " + token)

project = (root / "foo_music_metadata_studio.vcxproj").read_text(encoding="utf-8")
for token in [
    "FOOBAR2000_TARGET_VERSION=81",
    "src\\core\\normalizer.cpp",
    "src\\core\\rule_loader.cpp",
    "foo_music_metadata_studio",
]:
    if token not in project:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: project contract missing " + token)

for token in ("src\\writer\\physical_tag_writer.cpp",
              "src\\writer\\physical_tag_writer_qa.cpp",
              "EnablePhysicalWrites", "MMS_ENABLE_PHYSICAL_WRITES"):
    if token not in project:
        raise SystemExit("STATIC SDK WRITER BUILD GATE FAIL: " + token)
batch_writer_ui = (root / "src" / "foobar" / "batch_preview_dialog.cpp").read_text(
    encoding="utf-8")
for token in ("MMS_ENABLE_PHYSICAL_WRITES",
              "format_physical_write_confirmation(",
              "physical_write_pending->exchange(true)",
              "state->online_context_version.current()!=confirmation_version",
              "GetWindowLongPtrW(dialog,GWLP_USERDATA)",
              "g_workspace_generation!=confirmation_workspace",
              "g_workspace_generation!=generation",
              "waiting for SDK verification"):
    if token not in batch_writer_ui:
        raise SystemExit("STATIC SDK WRITER UI GATE FAIL: " + token)

# Group headings in the local Review ListView are display-only. Every
# actionable child still maps to a captured proposal ID, and only an explicit
# keyboard/mouse activation changes visibility (never an approval or network).
for token in (
    "std::array<bool,3> review_group_expanded",
    "djmeta::group_review_rows(",
    "if(index<state.focused_metadata_rows.size() &&",
    "state.metadata_view_group=grouped.group_at_row;",
    "selected_native_proposal_ids(",
    "state->review_group_expanded[group]=!state->review_group_expanded[group];",
):
    if token not in batch_writer_ui:
        raise SystemExit("STATIC LOCAL REVIEW GROUP GATE FAIL: " + token)

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
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: four official-provider epoch gates required; public page has its own ctx guard")
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

# The shared source-pinned SDK tree replaces the old SDK archive download.
# Preserve source commit/tree identity and reject stale, unsupported caches.
bootstrap = (root / "scripts" / "bootstrap-sdk.ps1").read_text(encoding="utf-8")
for token in (
    "31a35947a7527826c2199491692802e3ce3d8bd7",
    "2ba8caeb73f72633ad7f60ee5f72ed8074a0eb17",
    "rev-parse 'HEAD:vendor/sdk'",
    "Pinned first-party SDK template commit/tree mismatch.",
    "SDK-PIN.json",
    ".owner-sdk-pin.txt",
    "Unverified or stale SDK already staged",
    "FOOBAR_SDK_ORIGINAL_7Z_ARCHIVE_SHA256=NOT_TESTED",
):
    if token not in bootstrap:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: source-pinned SDK guard missing: " + token)
if "Invoke-WebRequest" in bootstrap or "7z.exe" in bootstrap:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: legacy manufacturer SDK transport restored")

host_probe = (foobar / "host_file_probe.cpp").read_text(encoding="utf-8")
for token in (
    "filesystem::g_get_canonical_path",
    "filesystem::g_get_native_path",
    "filesystem::g_get_stats2",
    "FILE_READ_ATTRIBUTES",
    "OPEN_EXISTING",
    "GetFileInformationByHandle",
    "FILE_SHARE_DELETE",
    "FILE_FLAG_OPEN_REPARSE_POINT",
    "valid_native_regular_file_attributes(native_info.dwFileAttributes)",
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
    "input.raw_target_host_canonical = result.observed.canonical_path",
    "RAW_TARGET_SHARED_HOST_CANONICAL",
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
    "djmeta::should_replace_auto_online_query(",
    "term,local_artist", "IDC_BATCH_GUIDE", "IDC_BATCH_HELP",
    "install_workspace_tooltips(", "TOOLTIPS_CLASSW", "TTM_ADDTOOLW",
    "show_workspace_help(", "header->code == NM_DBLCLK",
    "MAKEWPARAM(IDC_METADATA_MB_SEARCH,BN_CLICKED)",
    "search_attempt_notice", "recording_search_plan(", "TTDT_AUTOPOP",
    "dismiss_workspace_tooltips(", "align_native_preview_form(state.dialog)",
    'L"Reload the tracks already open in Music Metadata Studio?\\n\\n"',
):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: guided search, help or refresh text missing " + token)

# Successive MusicBrainz stages must merge only same-source exact-ID
# evidence and failed requests must not advance the strategy.
for token in (
    "merge_search_stage_results(", "request.plan_identity",
    "state.search_plan_identity==request.plan_identity",
    "musicbrainz_has_published_attempt",
    "musicbrainz_published_attempt_index",
    "state.last_musicbrainz_stage_empty,qualified",
    "DISCOVERY STRATEGY (read-only)",
    "REPEATED IDENTITY CONFLICT (read-only)",
    "state.musicbrainz_results=merged.result.candidates",
):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: staged MusicBrainz browser disconnected: "+token)

# The production WM_COMMAND path must use the tested per-command scope:
# read-only online actions validate the selected source; batch decisions
# and batch Apply continue to verify all captured sources before mutation.
native_scope_start = batch_table_dialog.split(
    "const auto check_scope=native_workspace_command_check_scope(",1)[1].split(
    "if(id==IDC_METADATA_DISCOGS_NEXT_PAGE",1)[0]
for token in (
    "native_command,message,wp",
    "check_scope!=NativeWorkspaceCheckScope::None",
    "revalidate_workspace(*state,",
    "check_scope==NativeWorkspaceCheckScope::FullBatch",
    "if (state->stale) return TRUE",
):
    if token not in native_scope_start:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: per-command source guard missing: " + token)
native_scope_header = (foobar / "native_preview_controls.h").read_text(encoding="utf-8")
for token in ("NativeWorkspaceCheckScope::FullBatch",
              "NativeWorkspaceCheckScope::SelectedSource",
              "IDC_BATCH_APPLY_SELECTED", "IDC_BATCH_APPLY_ALL",
              "IDC_METADATA_MB_SEARCH", "IDC_METADATA_MB_LOAD_RELEASE",
              "IDC_METADATA_DISCOGS_NEXT_PAGE", "IDC_METADATA_INSPECT_CUE",
              "IDC_METADATA_IMPORT_CANDIDATE", "IDC_ONLINE_WRITE_SELECTED"):
    if token not in native_scope_header:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing native validation scope: " + token)
for token in ("scope_for(IDC_METADATA_MB_SEARCH)",
              "scope_for(IDC_BATCH_APPLY_SELECTED)",
              "online_search_checks==1 && batch_apply_checks==kLargeBatch"):
    if token not in (root / "tests" / "native_preview_controls_tests.cpp").read_text(encoding="utf-8"):
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing 54k lookup/apply scope test")

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

# The compact FileOps status column may show only one finding, but the actual
# native row tooltip must expose every issue, including secondary collisions.
fileops_tip = batch_table_dialog.split(
    "header->idFrom == IDC_BATCH_LIST &&\n"
    "            header->code == LVN_GETINFOTIPW", 1)[1].split(
    "header->code == LVN_COLUMNCLICK", 1)[0]
for token in ("state->table.rows[source_row].issues", "for (const auto& issue : issues)",
              "issue_status_text(issue)", "detail += issue;"):
    if token not in fileops_tip:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: FileOps tooltip hides safety finding " + token)
if fileops_tip.index('"Safety findings:"') > fileops_tip.index('"Physical source: "'):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: long paths may truncate FileOps safety findings")
fileops_status = batch_table_dialog.split("std::string status_text(", 1)[1].split(
    "// Strip foobar's file:// locator", 1)[0]
for token in ("CUE_DEPENDENCIES_UNCHECKED", "FILESYSTEM_TARGET_UNCHECKED",
              "continue;", "return issue_status_text(issue);", "Raw candidate absent"):
    if token not in fileops_status:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: FileOps short status changed global limitation policy " + token)
for token in ('"Final CUE links: not checked"', '"Final target: not checked"'):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: FileOps tooltip omits global limitation " + token)

# Every context route must enter the same modeless Studio HWND rather than
# silently showing an unrelated legacy popup with different SAFE semantics.
for obsolete in ("show_normalization_preview(retained)",
                 "show_legacy_route_preview(retained"):
    if obsolete in route_menu:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: split legacy context workflow " + obsolete)
for label, source, token in [
    ("normalize and Prepare share workspace", route_menu,
     "if (index == 0 || index == 4)"),
    ("preset resolves the exact legacy route", route_menu,
     "legacy_move_routes[route_index]"),
    ("preset opens the same modeless workspace", route_menu,
     "show_batch_preview_dialog(retained, RoutePreviewChoice{"),
    ("only explicit route menu commands request preset change", route_menu,
     "preset.foobar_titleformat}, true);"),
    ("ordinary Prepare entry has no implicit preset request", prepare_dialog,
     "show_batch_preview_dialog(handles, choice);"),
    ("only explicit route requests warn on mismatch", batch_table_dialog,
     "const bool different_route = explicit_route_request && existing &&"),
    ("route switch compares actual current workspace", batch_table_dialog,
     "existing->current_choice.destination_root != initial_choice.destination_root"),
    ("different preset preserves existing decisions", batch_table_dialog,
     "Another route preset was requested."),
    ("route warning does not mask a stale source", batch_table_dialog,
     "if (different_route && !existing->stale)"),
]:
    if token not in source:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: " + label)

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
    ("selected row identity", batch_table_dialog, "native_route_preview_source_ids("),
    ("selected row identity adapter", (root / "src" / "foobar" / "native_preview_controls.h").read_text(encoding="utf-8"), "ROUTE_SCOPE_SELECTED_ROW_INVALID"),
    ("visible route button", resources, "IDC_BATCH_APPLY_VISIBLE"),
    ("visible route command", batch_table_dialog, "NativeRoutePreviewScope::Visible"),
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
    ("group route override", batch_table_dialog, "apply_to_rows(dialog, *state, NativeRoutePreviewScope::All)"),
    ("selected route override", batch_table_dialog, "apply_to_rows(dialog, *state, NativeRoutePreviewScope::Selected)"),
    ("selection stale guard", batch_table_dialog, "verify_snapshot(old)"),
    ("sparse transactional route edits", batch_table_dialog, "swap(state.entries[index],next)"),
    ("unverified CUE gate", batch_table_dialog, "next.input.cue_dependencies_checked=false"),
    ("unverified destination gate", batch_table_dialog, "next.input.filesystem_target_checked=false"),
    ("unapproved SAFE not used in routes", route_preview, "djmeta::project_explicit_review_decisions"),
    ("foobar titleformat projection", route_preview, "formatter.evaluate("),
    ("stale metadata guard", route_preview, "djmeta::fingerprint(latest)"),
    ("subsong physical guard", route_preview, "get_subsong_index() != 0"),
    ("raw target duplicate warning", route_preview, "raw_target_counts"),
    ("no-write banner", route_preview, "No tags were written"),
]:
    if token not in source:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing " + label)

# A single Accept/Reject/Reset must refresh only affected track counters.
# Full snapshot summaries are rebuilt only at initial capture/Refresh; all
# source guards and transactional action validation stay unchanged.
for token in (
    "void update_pending_review_summaries(",
    "djmeta::summarize_track_review_progress(",
    "summary.review_required=progress.pending_review;",
    "summary.pending_proposals=progress.pending_proposals;",
    "summary.staged_changes=progress.staged_changes;",
    "update_pending_review_summaries(state,affected_tracks);",
    "refresh_review_summaries(next);",
    "refresh_review_summaries(*state);",
):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing scoped review recount: " + token)
review_action_body=batch_table_dialog.split(
    "void apply_review_action(HWND dialog, PreviewState& state,",1)[1].split(
    "void apply_to_rows(HWND dialog, PreviewState& state,",1)[0]
if "refresh_review_summaries(state);" in review_action_body:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: full review recount after local decision")

# Original source filenames are immutable within the captured workspace.
# Reuse their already-parsed UTF-8 sort labels on every review decision,
# column sort and filter. Refresh swaps them with its complete new snapshot.
for token in (
    "std::vector<std::string> cached_track_sort_labels;",
    "state.track_summaries,state.cached_track_sort_labels,",
    "next.cached_track_sort_labels.push_back(name);",
    "state->cached_track_sort_labels.push_back(name);",
    "state.cached_track_sort_labels.swap(next.cached_track_sort_labels);",
    "state.cached_track_sort_labels.size()!=state.entries.size()",
):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing immutable 54k track-name cache: " + token)
master_sort_body = batch_table_dialog.split(
    "void update_master_table(PreviewState& state) {",1)[1].split(
    "void update_live_provider_controls(PreviewState& state) {",1)[0]
if "labels.push_back(readable_track_name(path))" in master_sort_body:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: O(54k) name rematerialization reintroduced")

# The default filename sort is stable throughout in-memory decisions.
# Cache both orders (not a reversed ascending order, which would flip ties),
# but keep review-count sorting live after Accept/Reject/Reset.
for token in (
    "cached_name_order_ascending",
    "cached_name_order_descending",
    "if(state.track_grid.sort_column==0)",
    "cached=djmeta::sort_track_summaries(",
    "sorted_sources=djmeta::sort_track_summaries(",
    "state.cached_name_order_ascending.clear();",
    "state.cached_name_order_descending.clear();",
):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: source-name sort cache incomplete: " + token)
if "dynamic review count cannot" not in (
    root / "tests" / "track_review_tests.cpp").read_text(encoding="utf-8"):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing immutable-vs-dynamic sort regression")

# Switching among 54k selected tracks must not scan every proposal again.
# The exact source-local index is built with the snapshot and atomically
# exchanged on Refresh; the full proposal/approval data remains immutable.
review_core = (root / "src" / "core" / "track_review.cpp").read_text(encoding="utf-8")
review_tests = (root / "tests" / "track_review_tests.cpp").read_text(encoding="utf-8")
for token in (
    "djmeta::TrackDiffIndex metadata_row_index;",
    "state.metadata_rows, state.metadata_row_index,",
    "state->metadata_row_index = djmeta::index_track_diffs(",
    "next.metadata_row_index=djmeta::index_track_diffs(",
    "std::swap(state.metadata_row_index,next.metadata_row_index);",
):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: source-local diff index wiring missing: " + token)
for token in ("TrackDiffIndex index_track_diffs(",
              "index.offsets[source_index]", "index.row_positions[i]",
              "all[position].source_index!=source_index",
              "index.captured_row_count!=all.size()"):
    if token not in review_core:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: indexed diff integrity missing: " + token)
for token in ("kLargeBatch=54129",
              "indexed diff retains proposal identity and rule provenance",
              "54k-track lookup touches only selected source",
              "same-size row reassignment cannot cross source identities"):
    if token not in review_tests:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: indexed diff regression missing: " + token)

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
    ("Single-track source refuses virtual CUE assignment", "Single-track source import has no qualified CUE ordinal"),
    ("Crates bounded visible source parser", "parse_crates_public_track_clipboard(text)"),
    ("Crates exact physical recording review", "review_crates_public_track("),
    ("MusicBrainz result group detail", "group_musicbrainz_release_rows("),
    ("MusicBrainz one row per hit", "state.candidate_rows=std::move(rows.summary)"),
    ("MusicBrainz separate field details", "refresh_musicbrainz_detail_rows(state)"),
    ("MusicBrainz stable candidate id", "state->candidate_rows[rowid].candidate.source_id"),
    ("position/mode restored", "load_batch_preview_window_placement()"),


    ("user clicked official MusicBrainz search", "id == IDC_METADATA_MB_SEARCH && HIWORD(wp) == BN_CLICKED"),
    ("CUE MusicBrainz release staged fallback", "release_search_plan("),
    ("CUE album CATALOG read from qualified inventory", 'unique_cue_album_value(inventory,"CATALOG")'),
    ("manual title never reuses source CUE barcode", "suggested_query?local_barcode:std::string{}"),
    ("CUE barcode-only native guard", "valid_release_search_barcode("),
    ("title-free discovery remains exact identifier only", "const bool identifier_only=suggested_query"),
    ("MusicBrainz plan source/kind isolation", 'std::string("release|")'),
    ("official lookup is separate user action", "id == IDC_METADATA_MB_LOAD_RELEASE && HIWORD(wp)==BN_CLICKED"),
    ("fixed-host reader", "fetch_musicbrainz_json_readonly(route,cancel)"),
    ("MusicBrainz background request", "begin_musicbrainz_network(dialog,*state"),
    ("MusicBrainz main-thread completion", "consume_musicbrainz_network(dialog,*state)"),
    ("bounded zero-hit continuation", "continue_empty_search_plan("),
    ("clicked next-strategy discovery", "next_clicked_search_attempt("),
    ("zero-hit continuation bound to same click", "state.online_context_version.matches(request->context_version)"),
    ("automatic search uses existing explicit command", "MAKEWPARAM(IDC_METADATA_MB_SEARCH,BN_CLICKED)"),
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
if "fetch_beatport_json(" in batch_table_dialog or "begin_beatport_network(" in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: unlicensed Beatport catalog API client")

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

# The MusicBrainz exact-release URL must include official label data, and the
# native fixed-host transport must allow that SAME builder output. A previous
# divergence omitted %2Blabels only in the transport allowlist, preventing
# "Load MB release" and release-row double-click before any HTTPS request.
mb_provider=(root / "include" / "djmeta" / "musicbrainz_provider.h").read_text(encoding="utf-8")
for token in ("release_lookup_request_suffix", "search_request_suffix",
              "max_search_candidates=25", "recordings%2Bartist-credits%2Bisrcs%2Blabels"):
    if token not in mb_provider:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: MusicBrainz shared route contract missing: "+token)
# Crucially, native WinHTTP now executes the SAME pure predicate exercised
# by musicbrainz_provider_tests, not a textually similar but drifting copy.
if "musicbrainz::qualified_musicbrainz_request_path(path)" not in online_transport:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: native MusicBrainz gate bypasses shared predicate")
if "inline bool qualified_musicbrainz_request_path(" not in mb_provider or \
   "qualified_musicbrainz_request_path(" not in (root / "tests" / "musicbrainz_provider_tests.cpp").read_text(encoding="utf-8"):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: MusicBrainz path validator lacks core execution")
if "constexpr std::string_view lookup_suffix=" in online_transport or \
   "constexpr std::string_view search_suffix=" in online_transport:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: duplicated native MusicBrainz route suffix")

# Native track tooltip must describe both guarded Find-tab actions:
# official MB/Discogs HTTPS or explicit Crates browser hand-off. Review/File
# navigation must not trigger either a provider request or browser launch.
if "search MusicBrainz from any tab" in batch_table_dialog or \
   "Find tab: double-click/Enter searches MusicBrainz or Discogs, or opens Crates" not in batch_table_dialog or \
   "Other tabs only navigate to Find without launching an online action." not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: misleading track activation tooltip")

# Track-selection prefill is provider-aware: never insert TITLE in a URL
# control, and never reuse a URL from another track. Provider switch also
# clears old title evidence before opening Beatport/Crates.
for token in (
    "if(state.beatport_offline_selected) {",
    "state.auto_search_track!=state.selected_track_index",
    "state.auto_search_track=state.selected_track_index;",
    "state.auto_search_text.clear();",
    'L"Exact Beatport URL (or paste from clipboard)"',
    "return; // no network, no auto-generated or guessed Beatport URL",
):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: Beatport source-bound URL prefill missing: "+token)

# One query box serves TITLE discovery and Beatport URL. Switching back
# to MB/Discogs must not try to search the old Beatport URL as a title.
for token in (
    "provider_query_transition(",
    "ProviderQueryTransition::ClearAndSuggest",
    "prefill_musicbrainz_query(*state); // no network until explicit Search",
):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: provider switch query reset/prefill missing: "+token)

# MB exact release/CUE album comparisons must use the same qualified
# all-track gate as the compiled core tests; partial/unverified tracks are
# explicitly BLOCKED in the real foobar native presentation.
for token in (
    "qualified_musicbrainz_cue_album_alignment(cue,edition,alignment)",
    "musicbrainz_cue_edition_full_alignment_required_",
    "cue_media_barcode_conflict(cue,edition)",
    "musicbrainz_cue_media_barcode_conflict_",
    "cue,*local_ordinal,{field},true",
    "link.decision==MatchDecision::Suggested",
    "musicbrainz::valid_mbid(remote.source_track_id)",
    "EvidenceScope::Edition,DateMeaning::NotDate};",
):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: MB/CUE release full alignment gate missing: "+token)

# Native MusicBrainz CUE rows must use the same occurrence-bound field
# provenance as the parsed release and the selected postimage adapter.
mb_cue_provenance = batch_table_dialog.split(
    "musicbrainz_cue_release_rows(", 1)[-1].split(
    "// The core matcher is the only authority", 1)[0]
for token in (
    "qualified_cue_edition_track_provenance(edition,i)",
    "evidence.source_id==remote.source_track_id",
    "evidence_it->values.size()==1",
    "const std::string value=verified_value",
    "std::count_if(remote.fields.begin(),remote.fields.end()",
    "if (local_ordinal && verified_value)",
    "musicbrainz_live_release_track_field_unverified",
):
    if token not in mb_cue_provenance:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: MB CUE occurrence field provenance missing: "+token)

# The loaded MusicBrainz edition must keep the complete real CUE carrier
# visible as a separate source group, just like the Discogs edition view.
mb_cue_rows = batch_table_dialog.split(
    "musicbrainz_cue_release_rows(", 1)[-1].split(
    "// The core matcher is the only authority", 1)[0]
for token in (
    'cue_original_inventory_rows(cue,"CUE Original / ")',
    'output.insert(output.end(),original.begin(),original.end())',
    'output.size()>9998',
    'output.size()>10000-original.size()',
):
    if token not in mb_cue_rows:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: MB CUE original inventory missing: "+token)

# Read-only selected CUE detail action is only available from verified
# release snapshots. The original CUE must be freshly inspected, identical,
# and sent solely to the pure postimage function (no SDK metadata writer).
for token in (
    '"djmeta/cue_review_postimage.h"',
    "capture_qualified_cue_edition_postimage(",
    "preview_selected_cue_review_postimage(",
    "source.carrier!=state->cue_postimage_snapshot->carrier",
    "cue_postimage_source!=state->selected_track_index",
    "state->cue_value_preview=proposal;",
    "state->cue_value_preview_indices=selected_indices;",
    "refresh_musicbrainz_detail_rows(*state);",
    "cue_detail_row_in_preview(",
    "CUE preview: ",
    "Read-only; no writes.",
):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: selected CUE postimage UI source gate missing: "+token)

# UX regression: a four-row modal summary hides the rest of a multi-field
# CUE preview and interrupts normal playlist navigation. Statuses now remain
# visible in the native detail ListView, including when the provider was sorted.
if ("additional selected fields omitted from message" in batch_table_dialog or
        "MessageBoxW(dialog,notice.c_str()" in batch_table_dialog):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: CUE preview is still a truncated modal popup")

# Selected, source-verified online tag postimages must also recalculate
# a read-only raw filename using foobar's OWN Title Formatting runtime.
# Do not treat that result as a filesystem collision/overwrite verdict.
for token in (
    "online_raw_route_valid",
    "evaluate_titleformat_against_canonical(",
    "raw_relative_path_lexically_safe(raw_before)",
    "raw_relative_path_lexically_safe(raw_after)",
    "Raw filename (what-if)",
    "Raw delta; not FileOps",
):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: online selected-tag naming what-if missing: " + token)

# Rapid manual MusicBrainz retry may happen immediately after a zero-hit
# search. It must wait in the worker, rather than reject and require a
# second manually timed click or sleep on the UI thread.
for token in ("request_spacing_wait_ms(", "std::this_thread::sleep_for(",
              "reject_cancelled(cancel)", "last_request_tick=tick"):
    if token not in online_transport:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: MusicBrainz worker pacing missing: "+token)
if "MusicBrainz rate limit: wait at least 1.2 seconds and try again." in online_transport:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: MusicBrainz retries still self-reject")

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
    "sdk-readme.html", "sdk-license.txt",
    "Get-Command 'git.exe'", "FOOBAR_SDK_GIT_SOURCE_VERIFIED=PASS"
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
for token in ('apply_native_preview_resize(', 'return native_window_dpi(window);', 'state->active_dpi', 'WM_DPICHANGED',
              'restore_batch_dialog_window_size(*state)', 'save_batch_dialog_window_size(*state)',
              'WM_EXITSIZEMOVE', 'load_batch_preview_window_size()'):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: production responsive per-monitor layout missing " + token)
native_dpi_header=(foobar / "native_preview_controls.h").read_text(encoding="utf-8")
for token in ('GetProcAddress(user32,"GetDpiForWindow")',
              'inline int native_window_dpi(HWND window) noexcept',
              'native_edit_padding_for_dpi(6,dpi)'):
    if token not in native_dpi_header:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: shared DPI policy missing " + token)
for token in ('review_split_gap_hit(', 'WM_LBUTTONDOWN', 'SetCapture(dialog)',
              'load_review_split_offset()', 'store_review_split_offset('):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: draggable review pane wiring missing " + token)
if ('native_review_scope_hint(' not in batch_table_dialog or
    'ListView_GetSelectedCount(state.metadata_track_list)' not in batch_table_dialog or
    'native_review_scope_hint(' not in gui_test):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: current-track versus batch scope hint missing")
if ('native_master_track_deselected(' not in batch_table_dialog or
    'update_local_review_hint(*state)' not in batch_table_dialog or
    'notified_master_deselections' not in gui_test):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: master deselection hint refresh missing")
if ('native_column_menu_point(' not in native_controls or
    'native_column_menu_point(' not in gui_test or
    'requested == state->metadata_track_list' not in batch_table_dialog or
    'requested == state->metadata_list' not in batch_table_dialog or
    'ListView_GetHeader(list),pointer' not in batch_table_dialog):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: review grid keyboard/list column menu missing")
if 'review_split_gap_hit(' not in gui_test:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: splitter HWND test missing")
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
if ('project_explicit_review_decisions(' not in batch_table_dialog or
    'verify_snapshot(entry)' not in batch_table_dialog):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: explicit review projection lacks analysis/stale gate")
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
# The third pinned SDK checkout is exclusively for isolated synthetic
# write QA; all three Windows jobs must use the same immutable SDK commit.
if workflow.count("repository: lxsdd/foobar2000_component_template") != 3 or (
    workflow.count("ref: 31a35947a7527826c2199491692802e3ce3d8bd7") != 3
) or workflow.count("persist-credentials: false") < 4:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: SDK checkout pins missing on one of three Windows jobs")
if (workflow.count("repository: lxsdd/dev-infrastructure-public") != 1 or
    workflow.count("ref: 31930d4fca571f449f2c57044bea5ba2e01f0ca7") != 1 or
    "  physical-sdk-runtime-qa:\n    if: github.event_name == 'push' && github.ref == 'refs/heads/feature/online-metadata-discovery-foundation-20261009'" not in workflow or
    "MMS_NATIVE_PHYSICAL_WRITER_QA" not in (root / "scripts" / "qa-physical-writer-host.ps1").read_text(encoding="utf-8")):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: synthetic SDK writer QA not isolated and pinned")
# Exactly two *step-scoped* references are now allowed: one for the
# authenticated staged-search probe and one for the actual native host gate.
# Both belong exclusively to the same manually approved discogs-qa job.
# NEVER move the secret into job-global env or an unprotected PR job.
if "pull_request_target:" in workflow or workflow.count(
    "secrets.MMS_DISCOGS_QA_TOKEN"
) != 2:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: unsafe Discogs secret propagation")
if "  workflow_dispatch:\n    inputs:" not in workflow:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing explicit manual QA trigger")
if "  discogs-authorized-native:\n" not in workflow:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing separated Discogs QA job")
public_jobs, protected_job = workflow.split("  discogs-authorized-native:\n", 1)
protected_job = protected_job.split("\n  # Internal PRs", 1)[0]
if "secrets.MMS_DISCOGS_QA_TOKEN" in public_jobs:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: Discogs secret exposed to PR jobs")
if "authorized-discogs-full-artist-probe:" in public_jobs:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: protected full-artist probe must not run in PR jobs")
if protected_job.count("secrets.MMS_DISCOGS_QA_TOKEN") != 2 or (
    "      - name: Authorized official Discogs staged search and exact-edition QA\n"
    "        if: matrix.platform == 'Win32'\n"
    "        shell: pwsh\n"
    "        env:\n"
    "          MMS_DISCOGS_QA_TOKEN: ${{ secrets.MMS_DISCOGS_QA_TOKEN }}"
) not in protected_job:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: second protected Discogs QA secret scope unqualified")
for token in (
    "github.event_name == 'workflow_dispatch'",
    "inputs.discogs_live == true",
    "github.ref == 'refs/heads/feature/online-metadata-discovery-foundation-20261009'",
    "environment: discogs-qa",
    "FB2K_DISCOGS_QA_TOKEN:",
    "secrets.MMS_DISCOGS_QA_TOKEN",
    "authorized-discogs-probe: 'true'",
    "authorized-discogs-full-artist-probe: ${{ inputs.discogs_release_id == '37332711' && 'true' || 'false' }}",
):
    if token not in protected_job:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing protected live QA guard: " + token)
if workflow.count(
    "lxsdd/dev-infrastructure-public/.github/actions/foobar-host-smoke@"
    "31930d4fca571f449f2c57044bea5ba2e01f0ca7"
) != 2:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: mismatched protected/ordinary host pins")
# Public workflows are source-only. Previously this gate accepted two
# upload conditions targeting main, but those conditions could still publish
# unqualified DLLs. Require the stronger merged #55 safety hold instead.
for guarded in (
    "      - name: Upload DLL\n"
    "        if: false # Safety hold: no public DLL artifacts",
    "  package:\n"
    "    if: false # Safety hold: do not package public DLLs",
    "      - name: Upload combined component\n"
    "        if: false # Safety hold: no public binary packages",
):
    if workflow.count(guarded) != 1:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: public binary gate missing: " + guarded)
if "if: github.event_name == 'push' && github.ref == 'refs/heads/main'" in workflow:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: public DLL distribution re-enabled")

# An external .cue selected as a virtual foobar subsong must still retain
# a physical *read-only* carrier guard. This is independent of its status
# as an ineligible physical audio/tag/CUE write target.
# A double-click/Return on Find & compare may start the guarded online
# search exactly once. From Review/File locations, the gesture is navigation
# only: no provider request, and no overwrite of manually edited search text.
track_activation = batch_rules_guard.split(
    "if (header && header->idFrom == IDC_METADATA_TRACK_LIST &&\n"
    "            (header->code == NM_DBLCLK || header->code == LVN_KEYDOWN)) {", 1)[1].split(
    "if (header && header->idFrom == IDC_METADATA_TRACK_LIST &&\n"
    "            header->code == LVN_GETDISPINFOW)", 1)[0]
if (
    "const int originating_tab=TabCtrl_GetCurSel(state->tabs);" not in track_activation
    or "if(originating_tab != 0)" not in track_activation
    or "SetFocus(GetDlgItem(dialog,IDC_METADATA_MB_QUERY))" not in track_activation
    or "prefill_musicbrainz_query(*state,true);" not in track_activation
    or "native_track_activation_starts_online_search(" not in track_activation
    or "native_track_activation_starts_crates_handoff(" not in track_activation
    or track_activation.count("MAKEWPARAM(IDC_METADATA_MB_SEARCH,BN_CLICKED)") != 1
    or track_activation.count("MAKEWPARAM(IDC_METADATA_DISCOGS_NEXT_PAGE,BN_CLICKED)") != 1
    or "already_searching" in track_activation
    or "header->code == LVN_ITEMACTIVATE" in track_activation
):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: track activation must navigate"
                     " without network from Review/File tabs and search"
                     " exactly once only from Find")
if "native_track_activation_starts_online_search(0,false)" not in gui_test or (
    "native_track_activation_starts_online_search(1,false)" not in gui_test or
    "native_track_activation_starts_online_search(0,true)" not in gui_test or
    "native_track_activation_starts_crates_handoff(0,true)" not in gui_test or
    "native_track_activation_starts_crates_handoff(0,false)" not in gui_test or
    "native_track_activation_starts_crates_handoff(1,true)" not in gui_test
):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: missing native tab-scoped"
                     " official API and Crates activation regression tests")

# Track navigation and an explicit track activation must have different
# search-prefill semantics. A mere selection NEVER resets manual edits or
# triggers online lookup. Refresh may reset provider result caches, but keeps
# the selected source and any manual query across snapshot replacement.
track_selection = batch_rules_guard.split(
    "if (header && header->idFrom == IDC_METADATA_TRACK_LIST &&\n"
    "            header->code == LVN_ITEMCHANGED) {", 1)[1].split(
    "if (header && header->idFrom == IDC_METADATA_TRACK_LIST &&\n"
    "            header->code == LVN_GETINFOTIPW)", 1)[0]
if (
    "prefill_musicbrainz_query(*state);" not in track_selection
    or "prefill_musicbrainz_query(*state,true);" in track_selection
    or "IDC_METADATA_MB_SEARCH,BN_CLICKED" in track_selection
):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: ordinary track selection overwrites manual query")
for token in (
    "const auto selected_before_refresh=state.selected_track_index;",
    "const auto query_before_refresh=read_control(",
    "!djmeta::should_replace_auto_online_query(",
    "preserve_manual_query ? query_before_refresh.c_str() : L\"\"",
    "state.selected_track_index=selected_before_refresh<state.entries.size()",
    "prefill_musicbrainz_query(state);",
):
    if token not in refresh_source:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: refresh discards manual search or selected source: " + token)
if "prefill_musicbrainz_query(state,true);" in refresh_source:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: refresh unconditionally overwrites manual search")

# A multi-selected release ListView must first resolve the actual selected
# double-click/Return target, then atomically retarget the first selected row
# consumed by the existing Load release command. This is stronger than merely
# comparing the clicked row to the first selected row: the user can activate
# any highlighted release, and no stale/blank/unselected identity can load.
release_activation = batch_rules_guard.split(
    "const bool release_activation=header &&",1)[1].split(
    "if (header && header->idFrom == IDC_METADATA_LIST &&",1)[0]
for token in (
    "const int release_focused_row=release_activation",
    "reinterpret_cast<const NMITEMACTIVATE*>(lp)->iItem",
    "const bool clicked_selected=release_clicked_row>=0",
    "const bool focused_selected=release_focused_row>=0",
    "native_selected_release_activation_row(header->code",
    "!state->stale && state->show_candidate",
    "!pasted_comparison_group_view(*state)",
    "native_focus_exact_release_row(",
    "state->updating_musicbrainz_selection=true",
    "update_online_release_load_button(*state);",
    "if (exact_release_activation &&",
):
    if token not in release_activation:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: release row intent is not qualified: "+token)
for token in (
    "return clicked_row >= 0 && clicked_selected ? clicked_row : -1;",
    "return focused_row >= 0 && focused_selected ? focused_row : -1;",
    "ListView_SetItemState(list, -1, 0, LVIS_SELECTED);",
    "ListView_GetSelectedCount(list) == 1",
    "ListView_GetNextItem(list, -1, LVNI_SELECTED) == target_row",
):
    if token not in sources:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: exact release target policy missing: "+token)

# Native popup tooltips must be explicitly dismissed BEFORE hiding the
# modeless dialog while an in-flight HTTPS worker finishes. The popup HWND
# otherwise survives the invisible parent and may appear to stick over foobar.
tooltip_header = (foobar / "native_preview_controls.h").read_text(encoding="utf-8")
for token in (
    "native_workspace_dismisses_tooltip(",
    "native_workspace_pop_tooltips(",
    "ListView_GetToolTips(list)",
    "case WM_CLOSE:",
    "case WM_CANCELMODE:",
    "case WM_SHOWWINDOW:",
    "case WM_NCACTIVATE:",
    "case WM_COMMAND:",
):
    if token not in tooltip_header:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: tooltip dismissal policy missing: "+token)
tooltip_wiring = batch_rules_guard.split(
    "if (message == kReadOnlyNetworkDoneMessage)",1)[1].split(
    "if (message == WM_GETMINMAXINFO",1)[0]
if not (
    "native_workspace_dismisses_tooltip(message,wp)" in tooltip_wiring
    and "dismiss_workspace_tooltips(*state);" in tooltip_wiring
    and "native_workspace_pop_tooltips(state.help_tooltips," in batch_rules_guard
    and "dismiss_workspace_tooltips(*state);" in batch_rules_guard.split(
        "header->code == TCN_SELCHANGE",1)[1].split(
        "invalidate_online_request(*state)",1)[0]
    and tooltip_wiring.index("native_workspace_dismisses_tooltip(message,wp)") <
        tooltip_wiring.index("if (message == WM_CLOSE)") <
        tooltip_wiring.index("ShowWindow(dialog,SW_HIDE)")
):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: tooltip must pop before hidden close")

# Search-button hover help must follow the chosen online source rather
# than leaving Beatport-only instructions visible on MusicBrainz/Discogs.
provider_change = batch_rules_guard.split(
    "if(id==IDC_METADATA_ONLINE_PROVIDER && HIWORD(wp)==CBN_SELCHANGE)",1)[1].split(
    "if (id == IDCANCEL)",1)[0]
if not (
    "update_workspace_search_tooltip(*state);" in provider_change
    and "TTM_UPDATETIPTEXTW" in batch_rules_guard
    and "native_search_action_tooltip(" in batch_rules_guard
):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: provider help is not updated")

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

# Beatport must appear as a distinct native offline provider, not silently
# obtain token access by reusing the official MB/Discogs worker. Preserve
# source provenance and the existing bounded matching/clipboard parser.
for marker in (
    'L"Beatport + Crates"',
    'state->beatport_offline_selected=selected_provider==2;',
    'if(state->beatport_offline_selected)',
    'L"Import clipboard"',
    'if(beatport_user_release || crates_visible_release ||\n               beatport_user_track || crates_visible_track)',
    'djmeta::online::parse_beatport_user_release_clipboard(text)',
    'djmeta::online::review_beatport_user_edition_physical(',
    'djmeta::online::review_beatport_user_edition_cue(',
    'djmeta::online::review_beatport_user_track(',
    'djmeta::online::parse_crates_public_release_clipboard(text)',
    'djmeta::online::review_crates_public_release_physical(',
    'Crates visible edition has no independently qualified ',
    'row.field=(crates_visible_track',
    'state->musicbrainz_live_view =\n                beatport_user_release || crates_visible_release ||\n                beatport_user_track || crates_visible_track;',
):
    if marker not in batch_rules_guard:
        raise SystemExit('STATIC FOOBAR AUDIT FAIL: Beatport native integration missing: '+marker)
if 'fetch_beatport_json(' in batch_rules_guard or 'void begin_beatport_network(' in batch_rules_guard:
    raise SystemExit('STATIC FOOBAR AUDIT FAIL: Beatport API must remain license-gated')

# One user-click Beatport HTML GET uses a fixed TLS origin and never bypasses denial.
transport=(foobar/"beatport_public_http.cpp").read_text(encoding="utf-8")
for marker in ("L\"www.beatport.com\"", "WINHTTP_OPTION_REDIRECT_POLICY_NEVER",
               "WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH",
               "status==401 || status==403 || status==429",
               "WINHTTP_FLAG_SECURE"):
    if marker not in transport:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: unsafe Beatport page transport: "+marker)
for marker in ("Authorization:", "WINHTTP_ACCESS_TYPE_NO_PROXY"):
    if marker in transport:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: public-page credentials/proxy bypass")
for marker in ("perform_beatport_public_page_lookup(dialog,*state)",
               "consume_beatport_public_network(dialog,*state)",
               "request_readonly_network_stop(*state->beatport_public_request)",
               "state.online_context_version.matches(ctx->context_version)",
               "ctx->url,job->response)"):
    if marker not in batch_rules_guard:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: native Beatport page safety missing: "+marker)

# Account-safe Crates is a browser hand-off, not an undocumented logged-in API.
for marker in (
    'L"Find in Crates"',
    'prepare_crates_browser_search(original)',
    'from_utf8(search.browser_url)',
    'ShellExecuteW(dialog,L"open",url.c_str()',
    'copy_crates_search_text(dialog,from_utf8(search.search_text))',
):
    if marker not in batch_rules_guard:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: native Crates browser hand-off absent: "+marker)
for marker in ("crates.co/api", "Crates token=", "beatport_password", "crates_session"):
    if marker in batch_rules_guard:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: Crates credentials/undocumented API leaked into native host")

# A track change can hide the lower online detail pane. Capture live user
# widths BEFORE the inactive early-return; otherwise persisting only after
# re-showing a hidden ListView can restore stale widths.
online_pane = batch_rules_guard.split(
    "void layout_musicbrainz_detail_pane(PreviewState& state) {",1)[1].split(
    "\n// Separate the exact-edition physical lane",1)[0]
if online_pane.find("capture_review_grid_controls(state.musicbrainz_details,") == -1 or \
   online_pane.index("capture_review_grid_controls(state.musicbrainz_details,") > \
   online_pane.index("if (!active) {"):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: online columns not captured before pane hide")

# DPI transitions must snapshot lower online detail widths at the OLD DPI and
# apply them at the NEW DPI before SetWindowPos synchronously triggers WM_SIZE.
dpi_change = batch_rules_guard.split("if (message == WM_DPICHANGED) {",1)[1].split(
    "\n    if (message != WM_COMMAND)",1)[0]
dpi_markers = (
    "capture_review_grid_controls(state->musicbrainz_details,",
    "state->active_dpi = new_dpi;",
    "apply_review_grid_controls(state->musicbrainz_details,",
    "const auto* dimensions = reinterpret_cast<const RECT*>(lp);",
)
if any(marker not in dpi_change for marker in dpi_markers) or \
   sorted(dpi_change.index(marker) for marker in dpi_markers) != \
   [dpi_change.index(marker) for marker in dpi_markers]:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: online column DPI snapshot/order missing")

# A new Crates browser hand-off must discard the previous direct Beatport
# URL before the user can paste or explicitly load a newly discovered track.
# Otherwise the next button press could fetch an old, valid unrelated ID.
crates_handoff=batch_rules_guard.split("void open_crates_for_selected_physical_track(",1)[1].split("\n// A Beatport public URL",1)[0]
if 'SetDlgItemTextW(dialog,IDC_METADATA_MB_QUERY,L"");' not in crates_handoff:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: Crates hand-off may reuse a stale Beatport URL")
if 'copy_crates_search_text' in crates_handoff.split('const HINSTANCE launched=',1)[0]:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: successful Crates search overwrites clipboard")
if 'copy_crates_search_text(dialog,from_utf8(search.search_text))' not in \
   crates_handoff.split('if(reinterpret_cast<INT_PTR>(launched)<=32)',1)[1]:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: failed Crates browser launch has no safe clipboard fallback")

# A canonical Beatport URL may exceed the old 150-character MusicBrainz
# input cap. The native edit limit must match the strict URL validator.
if 'maximum_public_beatport_url_length, 0);' not in batch_rules_guard or \
   'EM_LIMITTEXT, 150, 0' in batch_rules_guard:
    raise SystemExit('STATIC FOOBAR AUDIT FAIL: Beatport URL query may truncate')


# Provider priority first-launch production wiring (MMS-CFG-02).
# This is a structural path check; interactive foobar runtime still needs
# its separate visual acceptance, and no search may launch on window open.
workspace_cfg = (foobar / "batch_table_settings.cpp").read_text(encoding="utf-8")
workspace_model = (root / "include" / "djmeta" / "workspace_defaults.h").read_text(encoding="utf-8")
workspace_prefs = (foobar / "menu_preferences.cpp").read_text(encoding="utf-8")
workspace_dialog = (foobar / "batch_preview_dialog.cpp").read_text(encoding="utf-8")
for label, present in (
    ("profile default selects preference-driven mode",
     'g_workspace_defaults(kWorkspaceDefaultsGuid, "v1|0|3|0|0")' in workspace_cfg),
    ("model default agrees with native profile",
     "int online_provider = 3;" in workspace_model),
    ("preferences allows explicit user override",
     "IDC_DEFAULT_ONLINE_PROVIDER" in workspace_prefs and
     "store_workspace_defaults(edited)" in workspace_prefs),
    ("new window resolves persisted provider order",
     "first_supported_search_mode(" in workspace_dialog and
     "load_online_provider_preferences()" in workspace_dialog and
     "CB_SETCURSEL,search_mode" in workspace_dialog),
    ("priority is identified as a manual search selection",
     "First supported in priority order" in workspace_prefs),
):
    if not present:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: provider priority wiring: " + label)

# A provider-search default cannot invalidate a separately user-declared
# clipboard import. This is the real native WM_COMMAND handler; the shared
# foobar host-smoke exercises it on an actual control and synthetic WAV.
for token in (
    'const bool manual_declared_candidate=text.starts_with("@provider=");',
    '!manual_declared_candidate)',
    'djmeta::online::parse_manual_candidate(text)',
    'L"Import clipboard"',
):
    if token not in workspace_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: cross-provider manual clipboard: " + token)

# Native Accept/Reject/Manual must cross the exact protected physical-tag
# postimage review without granting any write authority. The UI's generic
# SAFE-inclusive routing preview is deliberately NOT a physical approval.
native_review = workspace_dialog.split(
    "void apply_review_action(HWND dialog, PreviewState& state,",1)[1].split(
    "void apply_to_rows(HWND dialog, PreviewState& state,",1)[0]
if '#include "djmeta/review_physical_postimage.h"' not in workspace_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: native normalizer physical bridge not included")
for marker in (
    "if(entry.input.physical_source_qualified) {",
    "probe_host_file_readonly(entry.input.source_path)",
    "live.host_physical_key!=entry.observed_physical_key",
    "live.source_guard!=entry.observed_source_guard",
    "djmeta::PhysicalTagSourceSnapshot physical;",
    "physical.original=original;",
    "djmeta::preview_explicit_normalizer_physical_tags(",
    "state.analyses[track],state.analyses[track].ruleset_revision,",
    "next_decisions[track]",
    "if(!verified.eligible)",
    "if(selected_postimage_changed) {",
    "entry.staged = std::move(staged.document);",
):
    if marker not in native_review:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: native physical decision review missing: "+marker)
if native_review.index("if(!verified.eligible)") > native_review.index(
        "entry.staged = std::move(staged.document);"):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: native physical gate is after visible commit")
if "commit_preflighted_" in native_review or "metadb_io" in native_review:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: read-only review entered physical writer")


# MMS-WF-02 / MMS-WF-05 / MMS-FILE-01: the native route must be
# computed from the identical explicit-only projection used by physical
# postimage preflight. SAFE-in-Pending is a review suggestion, never a
# hidden renamed filename or physical write. The ordinary display projector
# remains available for comparing proposals but must not feed FileOps.
if 'project_explicit_review_decisions(original,result)' not in workspace_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: initial route auto-applies SAFE")
review_for_route = workspace_dialog.split(
    "void apply_review_action(HWND dialog, PreviewState& state,",1)[1].split(
    "void apply_to_rows(HWND dialog, PreviewState& state,",1)[0]
if ('djmeta::project_explicit_review_decisions(' not in review_for_route or
    'original, state.analyses[track], next_decisions[track]' not in review_for_route or
    'entry.staged = std::move(staged.document)' not in review_for_route):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: accepted choices do not own route")
if ('djmeta::project_review_decisions(' in review_for_route or
    'djmeta::stage_safe_only(' in workspace_dialog):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: unapproved SAFE feeds FileOps")
if ('staged.unresolved_semantic > 0' not in workspace_dialog):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: pending semantic route status stale")

# The old context-menu routing popup has no per-field Accept action. Its
# source may show SAFE suggestion counts, but must never sneak those values
# into foobar titleformat filenames. Real consent is in the modeless workspace.
if ('djmeta::stage_safe_only(' in route_preview or
    'project_explicit_review_decisions(' not in route_preview or
    'info, staged.document,' not in route_preview or
    'Unapproved SAFE suggestions (NOT routed)' not in route_preview):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: legacy filename route auto-accepted SAFE")


# Field-scoped review remains in the native normalizer decision path.
anchor_block=workspace_dialog.split(
    'const auto selected_field_anchor=[&]() {',1)[1].split(
    'const auto field_scope_sources=[&]',1)[0]
if ('is_external_cue_locator(' not in anchor_block or
    'get_subsong_index()!=0' not in anchor_block):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: CUE/virtual field anchor accepted")
for marker in (
    'L"Field: visible"', 'L"Field: all"',
    'same_field_review_proposal_targets(',
    'selected_field_anchor()', 'field_scope_sources(scope)',
    'fresh_targets!=captured_targets',
    'MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2',
):
    if marker not in workspace_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: field-scoped review lost: "+marker)

# MMS-WF-09 / MMS-OPS-03: keep 50k+ source batches interactive.
# A single native metadata Accept or a selected route override must stage
# only touched source rows; global full-batch source/rules revalidation stays.
review_action=workspace_dialog.split(
    "void apply_review_action(HWND dialog, PreviewState& state,",1)[1].split(
    "void apply_to_rows(HWND dialog, PreviewState& state,",1)[0]
route_action=workspace_dialog.split(
    "void apply_to_rows(HWND dialog, PreviewState& state,",1)[1].split(
    "// User-initiated inspection of RAW, unsanitized candidate paths",1)[0]
for name, body in (("review",review_action),("FileOps routing",route_action)):
    if name == "review":
        required = ("std::map<std::size_t,PreviewEntry>", "try_emplace(",
                    "for (const auto& entry : state.entries) verify_snapshot(entry);",
                    "verify_rules_snapshot(state.captured_rules);")
    else:
        # Scoped routing builds only changed sparse rows and performs a full
        # physical source re-probe both before and after modal confirmation.
        required = ("std::map<std::size_t,PreviewEntry>", "candidate.emplace(",
                    "revalidate_workspace(state,true);",
                    "verify_rules_snapshot(state.captured_rules);",
                    "native_route_preview_source_ids(", "MB_DEFBUTTON2")
    if any(marker not in body for marker in required):
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: sparse "+name+
                         " editing lost atomic revalidation")
    if ("auto next_entries = state.entries;" in body or
        "auto candidate = state.entries;" in body or
        "state.entries = std::move(" in body):
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: "+name+
                         " deep-copies full music library")
if ("std::map<std::size_t,std::vector<djmeta::ReviewDecision>> next_decisions;" not in review_action or
    "swap(state.review_decisions[track],next_decisions.at(track));" not in review_action):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: native review decisions not sparse")


# MMS-WF-05 / MMS-WF-07 / MMS-UI-05: native, transient one-step Undo.
# Only previously staged in-memory preview rows/decisions are restored;
# the command does not save approvals, touch files, or bypass fresh checks.
if ('#define IDC_BATCH_UNDO 310' not in
    (foobar / 'resource.h').read_text(encoding='utf-8') or
    '"Undo preview", IDC_BATCH_UNDO' not in resources):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: native Undo control missing")
undo_start='void undo_last_preview_action(HWND dialog, PreviewState& state) {'
undo_end='// User-initiated inspection of RAW, unsanitized candidate paths only.'
if undo_start not in batch_table_dialog or undo_end not in batch_table_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: undo handler missing")
undo_action=batch_table_dialog.split(undo_start,1)[1].split(undo_end,1)[0]
for marker in (
    'if(!state.last_preview_undo || state.show_candidate)',
    'for(const auto& entry:state.entries) verify_snapshot(entry);',
    'verify_rules_snapshot(state.captured_rules);',
    'before.input_fingerprint!=state.entries[index].input_fingerprint',
    'state.last_preview_undo.reset();',
    'swap(state.entries[index],previous);',
    'reset_raw_target_observation(state.entries[index]);',
    'state.entries[index].input.cue_dependencies_checked=false;',
    'swap(state.review_decisions[index],',
    'update_pending_review_summaries(state,affected);',
    'update_table(state);',
):
    if marker not in undo_action:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: Undo missing "+marker)
if any(token in undo_action for token in
       ('metadb_io','commit_preflighted','physical_tag_writer')):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: preview Undo entered writer")
if ('case IDC_BATCH_UNDO:' not in native_controls or
    'IDC_BATCH_UNDO,' not in batch_table_dialog.split(
        'void mark_stale_workspace(PreviewState& state) {',1)[1].split(
        '// Verify the physical source on the UI thread',1)[0] or
    'if (id == IDC_BATCH_UNDO && HIWORD(wp) == BN_CLICKED)' not in batch_table_dialog):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: Undo command not protected")
refresh_action=batch_table_dialog.split(
    'void refresh_workspace_snapshot(PreviewState& state) {',1)[1].split(
    'void show_workspace_help(',1)[0]
if 'state.last_preview_undo.reset();' not in refresh_action:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: Refresh retains previous Undo")
if ('kMaxPreviewUndoSources=4096' not in batch_table_dialog or
    'std::optional<PreviewUndoFrame> last_preview_undo' not in batch_table_dialog):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: Undo not bounded and transient")
if ('frame.previous_entries=std::move(next_entries);' not in batch_table_dialog or
    'frame.previous_entries=std::move(candidate);' not in batch_table_dialog):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: Undo not connected to both native actions")


# One-step Undo must not be displaced by a repeated Accept/Reject/Reset or
# an identical manual correction. Such a gesture has no new planned change.
for marker in (
    'const auto& previous=state.review_decisions[track][proposal];',
    'previous.action==action',
    'previous.manual_value==manual_value',
    'if(affected_tracks.empty()) return;',
):
    if marker not in review_action:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: no-op action clobbers Undo: "+marker)


# Source-aware native bridge: accepted local fields and independently selected
# Discogs edition fields must be jointly projected BEFORE a preview UI update.
# No write calls, global approval or silent provider precedence is added.
review_bridge=workspace_dialog.split(
    "void apply_review_action(HWND dialog, PreviewState& state,",1)[1].split(
    "void apply_to_rows(HWND dialog, PreviewState& state,",1)[0]
online_bridge=workspace_dialog.split(
    "void change_online_field_preview(PreviewState& state,int action) {",1)[1].split(
    "INT_PTR CALLBACK batch_dialog_proc(",1)[0]
for marker in (
    'djmeta::UnifiedReviewBatchReprobe unified;',
    'unified.normalizer.decisions=next_decisions[track];',
    'unified.selected_online_indices=state.online_included_fields;',
    'native_recheck_preserved_online_selection(',
    'state.online_field_snapshot.has_value(),',
    'djmeta::preview_unified_review_physical_tags(',
    'if(!joint.physical.eligible)',
):
    if marker not in review_bridge:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: unified local guard missing "+marker)
for marker in (
    'unified.normalizer.decisions=',
    'state.review_decisions[state.selected_track_index];',
    'unified.selected_online_indices=proposed;',
    'unified.explicit_date_targets=date_targets;',
    'if(!combined.physical.eligible)',
    'current,combined.physical.planned',
    'latest.source_guard,approved_sdk_fields,combined.physical,',
    'entry.handle->get_location(),info->info(),combined.physical.planned,',
):
    if marker not in online_bridge:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: unified online preview missing "+marker)
if ('metadb_io::' in online_bridge or 'commit_preflighted_' in online_bridge or
    'MMS_ENABLE_PHYSICAL_WRITES' in review_bridge):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: unified read-only bridge entered writer")

if 'if(qualified_online_field_preview(state) &&' in review_bridge:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: hidden online tab bypasses local conflict check")


# Native read-only combined what-if is recomputed atomically with a local
# decision, including the independent physical guard for online-only edits.
# Undo and routing overrides invalidate old advisory filenames. Regression
# ensures no partial QA physical postimage can ignore accepted local edits.
local_audit=workspace_dialog.split(
    "void apply_review_action(HWND dialog, PreviewState& state,",1)[1].split(
    "void apply_to_rows(HWND dialog, PreviewState& state,",1)[0]
native_route=workspace_dialog.split(
    "void apply_to_rows(HWND dialog, PreviewState& state,",1)[1].split(
    "void undo_last_preview_action(HWND dialog, PreviewState& state)",1)[0]
native_undo=workspace_dialog.split(
    "void undo_last_preview_action(HWND dialog, PreviewState& state)",1)[1].split(
    "// User-initiated inspection of RAW",1)[0]
for marker in (
    'bool reviewed_online_route=false;',
    'const auto joint=djmeta::preview_unified_review_physical_tags(',
    'selected_postimage_changed=joint.physical.changed;',
    'joint.physical.planned,choice.titleformat_expression)',
    'if(selected_postimage_changed) {',
    'if(reviewed_online_route) {',
    'state.online_raw_route_valid=refreshed_online_route.has_value();',
):
    if marker not in local_audit:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: combined raw route stale: "+marker)
for name,fragment in (("route",native_route),("undo",native_undo)):
    if not all(x in fragment for x in (
        'state.online_raw_route_valid=false;',
        'state.online_raw_route_before.clear();',
        'state.online_raw_route_after.clear();')):
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: stale online raw route after "+name)
if ("entry.route_expression==typed_route.titleformat_expression" not in
    workspace_dialog.split(
        "void change_online_field_preview(PreviewState& state,int action) {",1)[1].split(
        "INT_PTR CALLBACK batch_dialog_proc",1)[0]):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: online what-if used wrong route")
if ("const bool physical_writer_ready=ready && !local_decisions_present" not in
    workspace_dialog or
    '"QA online-only writer cannot ignore accepted local normalization.' not in
    workspace_dialog):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: QA-only writer may apply partial postimage")


# Batch Audit must revalidate exact same-source joint physical evidence,
# including independent on-disk guard. Provider-only counts cannot be shown
# as though they were accepted into the frozen FileOps/CUE plan.
native_batch_audit=workspace_dialog.split(
    "std::wstring workflow_audit_summary(",1)[1].split(
    "std::string status_text(",1)[0]
for marker in (
    'if(!state.online_included_fields.empty()) {',
    'const auto track=state.online_edition_source;',
    'verify_snapshot(entry);',
    'probe_host_file_readonly(entry.input.source_path)',
    'unified.normalizer.decisions=state.review_decisions[track];',
    'unified.online_capture=*state.online_field_snapshot;',
    'unified.selected_online_indices=state.online_included_fields;',
    'const auto combined=djmeta::preview_unified_review_physical_tags(unified);',
    'if(!combined.physical.eligible)',
    'the full FileOps/CUE batch remains unqualified.',
):
    if marker not in native_batch_audit:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: joint source audit missing: "+marker)


# File Locations is a read-only joint RAW-collision overlay for the only
# selected, exact physical Discogs source. It must replace the local-only
# raw candidate on every online inclusion/removal and MUST NOT let the old
# local-only host target inspector probe a different path.
native_update_table=workspace_dialog.split(
    "std::vector<djmeta::BatchPreviewInputRow> current_batch_route_inputs(",1)[1].split(
    "const char* issue_status_text(",1)[0]
if "const auto inputs=current_batch_route_inputs(state);" not in native_update_table:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: shared joint route builder not used by table")

native_fileops_cell=workspace_dialog.split(
    "std::wstring cell_text(PreviewState& state,",1)[1].split(
    "std::string readable_track_name(",1)[0]
native_target_inspect=workspace_dialog.split(
    "void inspect_selected_raw_targets(HWND dialog, PreviewState& state)",1)[1].split(
    "void mark_stale_workspace(",1)[0] if (
    "void mark_stale_workspace(" in workspace_dialog.split(
        "void inspect_selected_raw_targets(HWND dialog, PreviewState& state)",1)[1]) else workspace_dialog.split(
    "void inspect_selected_raw_targets(HWND dialog, PreviewState& state)",1)[1].split(
    "void verify_online_source_snapshot(",1)[0]
native_online_change=workspace_dialog.split(
    "void change_online_field_preview(PreviewState& state,int action)",1)[1].split(
    "INT_PTR CALLBACK batch_dialog_proc",1)[0]
for marker in (
    'input.raw_relative_path=state.online_raw_route_valid',
    'state.online_raw_route_after:std::string{};',
    'input.raw_target_presence=djmeta::RawTargetPresence::NotInspected;',
    'input.filesystem_target_checked=false;',
    'input.cue_dependencies_checked=false;',
    'state.table = djmeta::describe_batch_preview(inputs);',
):
    if marker not in native_update_table:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: joint FileOps table missing "+marker)
if ('return L"Joint raw route: unqualified";' not in native_fileops_cell or
    'L"Joint raw preview; not probed"' not in native_fileops_cell or
    'L"Joint raw inspected; not final"' not in native_fileops_cell):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: joint route lacks visible inspected/unqualified status")
for marker in (
    'recheck_joint_raw_target_candidate(state,index,typed)',
    'recheck_joint_raw_target_candidate(state,result.index,typed)',
    'state.online_raw_target_observation=JointRawTargetObservation{',
    'if(result.joint) {',
):
    if marker not in native_target_inspect:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: joint raw inspection missing "+marker)
if 'update_table(state);\n    refresh_musicbrainz_detail_rows(state);' not in native_online_change:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: online change did not rebuild batch collisions")


# Joint raw target inspection must rebuild the source-bound physical postimage
# rather than treating an old online-only/local-only path as authority.
joint_recheck=workspace_dialog.split(
    "std::string recheck_joint_raw_target_candidate(",1)[1].split(
    "void inspect_selected_raw_targets(",1)[0]
for marker in (
    'verify_snapshot(entry);',
    'probe_host_file_readonly(entry.input.source_path)',
    'djmeta::preview_unified_review_physical_tags(unified)',
    'unified.normalizer.decisions=state.review_decisions[index]',
    'unified.selected_online_indices=state.online_included_fields',
    'before!=state.online_raw_route_before',
    'after!=state.online_raw_route_after',
    'return choice.destination_root+',
):
    if marker not in joint_recheck:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: joined source reread lacks "+marker)
if ('native_joint_raw_observation_matches(' not in native_update_table or
    'state.online_raw_target_observation.reset();' not in
    workspace_dialog.split("void change_online_field_preview(PreviewState& state,int action)",1)[1].split(
        "INT_PTR CALLBACK batch_dialog_proc",1)[0] or
    'native_joint_raw_observation_matches(' not in gui_test):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: joined target snapshot may be stale")

# Full-batch, user-clicked source-order audit export. No media paths, tags,
# tokens or writer approval may leave the native read-only preview.
audit_header=(root / "include" / "djmeta" / "workflow_preview_audit.h").read_text(encoding="utf-8")
for marker in (
    "struct WorkflowAuditRow",
    "format_workflow_audit_tsv(",
    "WORKFLOW_AUDIT_EXPORT_UNSAFE_CODE",
    "audit.tracks>100000",
    'out+="#"+std::to_string(i+1)',
):
    if marker not in audit_header:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: full batch audit missing "+marker)
for marker in (
    "IDC_BATCH_COPY_AUDIT",
    "native_copy_audit_text_to_clipboard(dialog,report);",
    "const auto verified_summary=workflow_audit_summary(state,&verified_online);",
    "djmeta::format_workflow_audit_tsv(",
    "state.table,audit,verified_online)",
    "if (id == IDC_BATCH_COPY_AUDIT && HIWORD(wp) == BN_CLICKED)",
):
    if marker not in workspace_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: native audit copy missing "+marker)
if "IDC_BATCH_COPY_AUDIT" not in native_controls or (
    '"Copy audit", IDC_BATCH_COPY_AUDIT' not in resources):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: audit export lacks visible full-batch gate")
for marker in ("IDC_BATCH_COPY_ISSUES", "WorkflowAuditExportScope::AttentionRows"):
    if marker not in workspace_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: attention-only export not wired "+marker)
if '"Copy issues", IDC_BATCH_COPY_ISSUES' not in resources or (
    "IDC_BATCH_COPY_ISSUES" not in native_controls):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: attention export lacks full-batch native control")


# The real Inspect CUE action must bind independently probed FILE source
# physical IDs to selected native audio rows, with a second guard recheck.
cue_inspect_source=workspace_dialog.split(
    "void append_external_cue_file_reference_evidence(",1)[1].split(
    "// Always independently derive the literal joint target",1)[0]
for marker in (
    "djmeta::audit_cue_physical_selection(",
    "selected.input.physical_source_qualified",
    "probe_host_file_readonly(selected.input.source_path)",
    "verify_snapshot(selected);",
    "verify_rules_snapshot(state.captured_rules)",
    "matched.file_to_selected_row",
    "cue_physical_source_only_not_fileops_approval",
):
    if marker not in cue_inspect_source:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: CUE physical-selection source-only bridge missing "+marker)
if "append_external_cue_file_reference_evidence(*state,entry,inventory,fields)" not in workspace_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: native CUE source bridge not invoked")
if "cue_physical_selection_audit.h" not in workspace_dialog:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: native CUE source bridge header missing")

if ('file_sources.push_back({i,{}, {},false});' not in cue_inspect_source or
    'if (i>=maximum_probes)' not in cue_inspect_source):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: truncated CUE FILE prefix appears qualified")


# Copying the full audit must attach the EXACT re-probed online+local
# physical postimage counts to the real source-order TSV row rather than
# exporting local-only decisions while displaying a richer summary.
native_summary_source=workspace_dialog.split(
    "std::wstring workflow_audit_summary(",1)[1].split(
    "void show_workflow_audit(",1)[0]
for marker in (
    "verify_rules_snapshot(state.captured_rules);",
    "if(verified_online) verified_online->reset();",
    "if(verified_online)",
    "djmeta::WorkflowOnlineSourceCounts{",
    "combined.physical.explicitly_changed",
    "combined.online_changed_fields",
    "latest.source_guard",
):
    if marker not in native_summary_source:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: merged online TSV source not verified: "+marker)
if ('std::optional<djmeta::WorkflowOnlineSourceCounts> verified_online;' not in workspace_dialog or
    'workflow_audit_summary(state,&verified_online)' not in workspace_dialog or
    'state.table,audit,verified_online)' not in workspace_dialog):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: full TSV export drops native merged Discogs review")
if ("WORKFLOW_AUDIT_ONLINE_SOURCE_EVIDENCE_INVALID" not in audit_header or
    "SOURCE_VERIFIED_POSTIMAGE_ONLY" not in audit_header):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: joint TSV evidence guard missing")


# One pure detached all-source metadata+external-CUE FILE+FilePlan adapter
# must preserve exact host target/probe and independent source->destination
# identity checks; no second writer or raw-filename sanitizer is introduced.
unified_cue_header=(root / "include" / "djmeta" / "unified_cue_file_plan_preview.h").read_text(encoding="utf-8")
for marker in (
    "preview_unified_cue_file_plan(",
    "review_batch_plan(captured).plan_fingerprint",
    "audit_cue_physical_selection(",
    "reference.resolved_source_key!=captured[selected].source_key",
    "preview_unified_review_physical_tags(reprobe)",
    "preview_cue_bound_file_batch(",
    "cue_bytes_change=out.review.decisions[cue_index].cue_bytes_write_needed",
    "UNIFIED_CUE_WHOLE_HOST_PLAN_CHANGED",
):
    if marker not in unified_cue_header:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: detached combined CUE metadata/FileOps preflight missing "+marker)
if ("test_joint_audio_online_normalizer_and_cue_fileplan();" not in
    (root / "tests" / "unified_review_batch_tests.cpp").read_text(encoding="utf-8")):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: combined CUE and online regression not run")


# The detached integrator's independent second-pass source, selection,
# provenance and future destination checks must not grant write access.
unified_cue_core=(root / "include" / "djmeta" / "unified_cue_file_plan_preview.h").read_text(encoding="utf-8")
for marker in (
    "recheck_unified_cue_file_plan(",
    "unified_cue_review_selection_fingerprint(",
    "UNIFIED_CUE_SECOND_PASS_SELECTION_OR_PROVENANCE_CHANGED",
    "UNIFIED_CUE_SECOND_PASS_FILE_SOURCE_CHANGED",
    "UNIFIED_CUE_SECOND_PASS_REFERENCE_OR_TARGET_CHANGED",
    "UNIFIED_CUE_SECOND_PASS_POSTIMAGE_OR_PLAN_CHANGED",
    "fresh.cue.proposed_cue_fingerprint",
    "fresh.audio_postimages[i].physical.planned",
    "row.status==PlanStatus::Unchanged",
):
    if marker not in unified_cue_core:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: detached joint CUE second-pass check missing "+marker)
if "unselected_swapped.file_sources[2].source_guard" not in (
    root / "tests" / "unified_review_batch_tests.cpp").read_text(encoding="utf-8"):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: unselected CUE source replacement regression missing")


# Real native external CUE FILE inventory must preserve foobar's canonical
# source key together with independently observed file ID/version; the
# detached whole-batch planner has to match ALL FILEs, including unselected
# participants, to separately qualified source references.
if 'observed.canonical_path});' not in cue_inspect_source:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: native CUE FILE canonical path not retained")
unified_cue_strict=(root / "include" / "djmeta" / "unified_cue_file_plan_preview.h").read_text(encoding="utf-8")
for marker in (
    "physical_file.canonical_source_key.empty()",
    "physical_file.canonical_source_key!=reference.resolved_source_key",
    "UNIFIED_CUE_FILE_CANONICAL_SOURCE_MISMATCH",
    "before.canonical_source_key!=now.canonical_source_key",
    "LOCAL_PROPOSAL_RULE_IDS",
    "LOCAL_PROPOSAL_RATIONALES",
    "LOCAL_CHANGE",
):
    if marker not in unified_cue_strict:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: canonical all-FILE/proposal bound missing "+marker)


# Literal RAW paths remain unqualified for foobar FileOps, but their complete
# Windows parent directory chain now has independent read-only physical
# observations, double-probed atomically with raw candidates. Junctions,
# symlinks, replaced directories and missing-then-created parents must not
# yield an apparently stable raw target or a false final destination.
native_probe=(root / "src" / "foobar" / "host_file_probe.cpp").read_text(encoding="utf-8")
native_probe_header=(root / "src" / "foobar" / "host_file_probe.h").read_text(encoding="utf-8")
for marker in (
    "probe_raw_destination_parent_chain_readonly(",
    "FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT",
    "FILE_ATTRIBUTE_REPARSE_POINT",
    "RAW_PARENT_DIRECTORY_REPLACED",
    "RAW_PARENT_NO_OBSERVED_DIRECTORY",
):
    if marker not in native_probe:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: native raw parent chain source evidence missing "+marker)
for marker in (
    "stable_raw_destination_parent_chain(",
    "before.ancestors.size()!=after.ancestors.size()",
    "old.physical_key!=now.physical_key",
    "old.guard!=now.guard",
):
    if marker not in native_probe_header:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: parent chain identity/stability guard missing "+marker)
raw_inspect_native=workspace_dialog.split(
    "void inspect_selected_raw_targets(",1)[1].split(
    "std::vector<PreviewEntry> capture_preview(",1)[0]
for marker in (
    "probe_raw_destination_parent_chain_readonly(",
    "raw_candidate,entry.input.destination_root",
    "state.entries.at(result.index).input.destination_root",
    "stable_raw_destination_parent_chain(result.parents,current_parents)",
    "if(!result.parents.qualified)",
    "result.observed.state=HostFileState::Unqualified",
    "Final foobar destinations/CUE",
):
    if marker not in raw_inspect_native:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: raw target ancestry native UI gate missing "+marker)


# The raw destination path cannot be qualified from any existing ancestor:
# the exact user-selected FileOps destination ROOT must be present, physically
# inspected, within the raw path, and independently re-probed as well.
for marker in (
    "selected_destination_root",
    "filesystem::g_get_canonical_path(",
    "out.native_destination_root=root.native();",
    "CompareStringOrdinal(",
    "RAW_SELECTED_DESTINATION_ROOT_MISSING",
    "RAW_SELECTED_ROOT_NOT_ANCESTOR_OF_TARGET",
    "out.destination_root_physical_key=part.physical_key",
    "found_selected_root",
):
    if marker not in native_probe:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: selected destination root not physically bound: "+marker)
for marker in (
    "destination_root_physical_key",
    "destination_root_guard",
    "matched_root",
):
    if marker not in native_probe_header:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: selected root witness missing: "+marker)
if ("raw_candidate,entry.input.destination_root" not in raw_inspect_native or
    "state.entries.at(result.index).input.destination_root" not in raw_inspect_native):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: native raw destination not bound to selected root on both probes")


# Nested external CUE FILE sources are read-only relative paths anchored to
# the actual CUE carrier directory; every OS ancestor must exist and remain
# stable on a SECOND native probe. This must never set the final CUE/FileOps
# write-qualification gates.
cue_locator=(root/"include/djmeta/cue_reference_locator.h").read_text(encoding="utf-8")
for marker in ("raw_relative_path_lexically_safe(relative)",
               "CUE_FILE_HOST_PROBE_REQUIRED", "source_parent",
               "relative.find('/')", "CUE_FILE_RESERVED_DEVICE"):
    if marker not in cue_locator:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: nested CUE lexical source qualification missing: "+marker)
cue_native=workspace_dialog.split(
    "void append_external_cue_file_reference_evidence(",1)[1].split(
    "std::string recheck_joint_raw_target_candidate(",1)[0]
for marker in ("candidate.source_parent", "parent_probes",
               "probe_raw_destination_parent_chain_readonly(",
               "std::all_of(parents.ancestors.begin()",
               "stable_raw_destination_parent_chain(earlier,latest)",
               "file_sources.push_back({i,{}, {},false})"):
    if marker not in cue_native:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: nested CUE parent witness missing: "+marker)


# Exact pasted MusicBrainz browser URLs select identities, not arbitrary
# WinHTTP routes or fabricated search results. Physical recording and CUE
# release both require explicit Search and the same source rechecks.
for token in (
    "parse_exact_entity_url(", "make_exact_recording_search_path(",
    "request.exact_recording_mbid", "candidate.mbid!=request.exact_recording_mbid",
    "request.direct_release", "make_release_lookup_path(",
    "state.candidate_source_index=request.source_index;",
    "begin_musicbrainz_network(dialog,*state,std::move(route)",
):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: exact MusicBrainz URL native path: "+token)
for token in (
    "Only exact HTTPS musicbrainz.org recording/release URLs",
    "valid_mbid(path)", 'encode_url_query("rid:"',
    "qualified_musicbrainz_request_path(",
):
    if token not in mb_provider:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: MusicBrainz URL allowlist missing: "+token)

# Named route clipboard transfer must remain an explicit native UI action,
# never a writer, a hidden provider upload or an automatic startup import.
for marker in (
    'IDC_BATCH_PROFILE_COPY', 'IDC_BATCH_PROFILE_PASTE',
    'copy_named_route_templates(dialog);',
    'paste_named_route_templates(dialog);',
    'plan_named_routes_import(before,*incoming)',
    'MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2',
    'store_named_routes(plan.merged);',
):
    if marker not in workspace_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: native route clipboard transfer missing "+marker)
for marker in ('"Copy", IDC_BATCH_PROFILE_COPY', '"Paste", IDC_BATCH_PROFILE_PASTE'):
    if marker not in resources:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: route clipboard button absent "+marker)

# Native action-aware CUE impact must consume per-source File locations
# staging, and must never activate the writer/host FileOps path.
cue_impact=(root / "include" / "djmeta" / "cue_raw_route_impact.h").read_text(encoding="utf-8")
for marker in ('FileAction file_action = FileAction::Move',
               'CUE_FILE_AUDIO_LEFT_IN_PLACE',
               'CUE_AUDIO_COPY_RAW_ONLY_SOURCE_CUE_UNCHANGED',
               'CUE_AUDIO_MOVE_RAW_ONLY_REWRITE_UNQUALIFIED',
               'CUE_MOVE_PARTIAL_DEPENDENCIES_UNQUALIFIED',
               'CUE_RENAME_PARENT_UNVERIFIED',
               'CUE_UNKNOWN_FILE_ACTION'):
    if marker not in cue_impact:
        raise SystemExit("STATIC CUE ACTION IMPACT FAIL: "+marker)
for marker in ('target.file_action=source.file_action;',
               'describe_cue_raw_route_code(route.code)',
               'impact.partial_move_blocked',
               'impact.copy_raw_candidates'):
    if marker not in batch_table_dialog:
        raise SystemExit("STATIC NATIVE CUE ACTION WIRING FAIL: "+marker)


# Native CUE carrier action must be explicitly staged, must be bound to its
# own physical CUE source and the complete FILE graph, and never enable writes.
cue_sidecar_model=(root / "include" / "djmeta" / "cue_raw_route_impact.h").read_text(encoding="utf-8")
for marker in ("inspect_cue_sidecar_raw_action(", "CUE_SIDECAR_ACTION_NOT_EXPLICITLY_STAGED",
               "CUE_SIDECAR_LEFT_WITH_MOVED_AUDIO_RISK",
               "CUE_SIDECAR_AUDIO_ACTIONS_MIXED_OR_UNQUALIFIED",
               "CUE_SIDECAR_COPY_RAW_ONLY_COPIED_LINKS_UNQUALIFIED"):
    if marker not in cue_sidecar_model:
        raise SystemExit("STATIC CUE SIDECAR CONTRACT FAIL: "+marker)
for marker in ("next.file_action_explicitly_staged=true;",
               "entry.file_action_explicitly_staged,",
               "sidecar_row.reason=\"cue_carrier_action_not_final_approval\""):
    if marker not in workspace_dialog:
        raise SystemExit("STATIC NATIVE CUE SIDECAR WIRING FAIL: "+marker)


# CUE review-to-file-plan native drilldown requires exact captured physical
# source identity, independent source/rules reprobe and no network/writer.
cue_nav=(root / "include" / "djmeta" / "cue_file_navigation.h").read_text(encoding="utf-8")
for marker in ("resolve_cue_file_navigation(", "cue_file_navigation_id(",
               "cue-sidecar-action", '"/source/"', "source>captured_tracks"):
    if marker not in cue_nav:
        raise SystemExit("STATIC CUE NAVIGATION FAIL: "+marker)
for marker in ('row.reason!="cue_raw_routing_impact_not_final_approval"',
               'row.reason!="cue_carrier_action_not_final_approval"',
               "verify_snapshot(cue);", "verify_snapshot(physical);",
               "revalidate_workspace(*state,true);",
               "state->location_queue=djmeta::BatchLocationQueue::All;",
               "target->source_index", "SetFocus(state->list);"):
    if marker not in workspace_dialog:
        raise SystemExit("STATIC NATIVE CUE NAVIGATION FAIL: "+marker)


# Physical CUE drilldown return is a native same-window user action, bound
# to the original physical carrier and fresh source evidence, never a write.
for marker in ("IDC_BATCH_BACK_TO_CUE",
               "state->cue_return=PreviewState::CueReturnIdentity{",
               "carrier.observed_physical_key!=saved.physical_key",
               "carrier.observed_source_guard!=saved.source_guard",
               "revalidate_workspace(*state,true);",
               "TabCtrl_SetCurSel(state->tabs,1);",
               "MAKEWPARAM(IDC_METADATA_INSPECT_CUE,BN_CLICKED)"):
    if marker not in workspace_dialog:
        raise SystemExit("STATIC CUE RETURN WORKFLOW FAIL: "+marker)


# CUE inspection queue is per-source read-only evidence: full batch plan
# changes invalidate receipts and Copy issues never exports source paths.
cue_queue=(root / "include" / "djmeta" / "cue_inspection_queue.h").read_text(encoding="utf-8")
for marker in ("void observe(", "void record(", "STALE_REINSPECT",
               "next_attention(", "export_tsv(", "NOT_QUALIFIED"):
    if marker not in cue_queue:
        raise SystemExit("STATIC CUE INSPECTION QUEUE FAIL: "+marker)
for marker in ("state.cue_inspections.observe(", "state.cue_inspections.record(",
               "cue_ledger=state.cue_inspections.export_tsv(",
               "IDC_BATCH_NEXT_CUE", "revalidate_workspace(*state,true);"):
    if marker not in workspace_dialog:
        raise SystemExit("STATIC NATIVE CUE INSPECTION QUEUE FAIL: "+marker)


# Physical CUE File locations stages project the original full-batch source
# permutation; never silently change metadata decisions or enable writers.
cue_filter=(root / "include" / "djmeta" / "cue_inspection_queue.h").read_text(encoding="utf-8")
for marker in ("filter_cue_queue_view(", "CUE_QUEUE_FILTER_SORTED_ID",
               "BatchLocationQueue::CueAttention",
               "BatchLocationQueue::CueRawOnly",
               "BatchLocationQueue::CueOriginalKept"):
    if marker not in cue_filter:
        raise SystemExit("STATIC CUE FILTER MODEL FAIL: "+marker)
for marker in ("state.view_order=djmeta::filter_cue_queue_view(",
               'L"CUE: inspect / blocked"',
               "selected>static_cast<int>(djmeta::BatchLocationQueue::CueOriginalKept)",
               "Selected online fields belong to the current source."):
    if marker not in workspace_dialog:
        raise SystemExit("STATIC CUE FILTER NATIVE SAFETY FAIL: "+marker)


# In filtered CUE views, the native status cell explains the independent
# CUE inspection stage, not a misleading generic RAW route status.
for marker in ('return L"CUE: "+from_utf8(djmeta::cue_queue_label(',
               'L" (final links unchecked)"'):
    if marker not in workspace_dialog:
        raise SystemExit("STATIC NATIVE CUE FILTER STATUS FAIL: "+marker)


# Native MusicBrainz paging reuses the existing Discogs/Crates button,
# same exact source snapshot and same provider worker; never new transports.
mb_provider=(root / "include" / "djmeta" / "musicbrainz_provider.h").read_text(encoding="utf-8")
for marker in ("musicbrainz_search_page_path(", "musicbrainz_has_next_page(",
               "max_search_page=4"):
    if marker not in mb_provider:
        raise SystemExit("STATIC MUSICBRAINZ PAGING MODEL FAIL: "+marker)
for marker in ("const bool next_musicbrainz=", "request.page_one_path=path;",
               "request.page=next;", "musicbrainz_more_pages=",
               "request->page==1 &&", "verify_online_source_snapshot(*state,entry);",
               "read_cue_raw_on_demand(entry.handle,entry.input.source_path)",
               "state->musicbrainz_results.size()>"):
    if marker not in workspace_dialog:
        raise SystemExit("STATIC MUSICBRAINZ NATIVE PAGING FAIL: "+marker)


# Physical MusicBrainz release discovery and comparison must be native UI
# wired to a direct exact-edition source and never enable tag staging.
mb_physical=(root / "include" / "djmeta" /
             "musicbrainz_physical_review.h").read_text(encoding="utf-8")
for marker in ("review_musicbrainz_physical_edition(",
               "musicbrainz_physical_track_provenance_trusted(",
               "musicbrainz_physical_edition_identifier_conflict(",
               'match.candidate.field="MATCH_DIAGNOSTIC"',
               "musicbrainz_local_only_no_provider_value"):
    if marker not in mb_physical:
        raise SystemExit("STATIC MB PHYSICAL EVIDENCE FAIL: "+marker)
for marker in ("explicit_physical_album_term(entered)",
               "request.kind=exact_url->kind;",
               "state->musicbrainz_page_cue_mode",
               "request.originals=state->musicbrainz_page_originals;",
               "if(original!=request.originals)",
               "review_musicbrainz_physical_edition(",
               "state.online_field_snapshot.reset();",
               "state.cue_postimage_snapshot=std::move(cue_preview);",
               "MusicBrainz edition and physical tracklist compared"):
    if marker not in workspace_dialog:
        raise SystemExit("STATIC MB PHYSICAL NATIVE WIRING FAIL: "+marker)
if ("qualified_online_field_preview(const PreviewState& state)" not in workspace_dialog or
    'state.online_physical_edition->provider=="discogs"' not in workspace_dialog):
    raise SystemExit("STATIC MB PHYSICAL MUST NOT ENABLE TAG STAGING")
