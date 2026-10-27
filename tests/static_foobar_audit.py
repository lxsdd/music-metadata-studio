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

project = (root / "foo_dj_metadata_normalizer.vcxproj").read_text(encoding="utf-8")
for token in [
    "FOOBAR2000_TARGET_VERSION=81",
    "src\\core\\normalizer.cpp",
    "src\\core\\rule_loader.cpp",
    "foo_dj_metadata_normalizer",
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

precision = (foobar / "precision_cue_workspace.cpp").read_text(encoding="utf-8")
for token in (
    "invalidate_cue_copy(state)",
    "state.reviewed_preview_ready",
    "IDC_PRECISION_EDITION_CONFIRMED",
    "IDC_PRECISION_COPY_CUE",
    "CF_UNICODETEXT",
    "state.lookup_url",
    "start_search(*state)",
    "parse_apple_precision_cue_album_search(json)",
    "state->search_query = state->lookup_url",
    "IDC_PRECISION_ALBUM_RESULTS, CB_GETCURSEL",
    "state.search_hits[static_cast<std::size_t>(choice)].collection_id",
    "job->selected_album = std::move(selected_album)",
    "apple_precision_cue_selection_matches_lookup(",
):
    if token not in precision:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: precision CUE copy review gate missing " + token)

# A single-disc external CUE must never have weaker album/track/time
# qualification than the all-disc create action. The user must still approve
# its own full destination after a fresh physical source/target snapshot.
single_create = precision.split(
    "void save_reviewed_disc_cue(Workspace& state) {", 1
)[-1].split("void save_all_reviewed_disc_cues(Workspace& state) {", 1)[0]
for token in (
    "validate_precision_cue_selected_disc(",
    "original, *state.release, static_cast<std::size_t>(chosen)",
    "observe_precision_cue_external_review(original)",
    "MessageBoxW(state.dialog, message.c_str()",
    "MB_DEFBUTTON2",
    "create_confirmed_precision_cue_selected_disc(",
    "std::move(confirmed), *state.release,",
):
    if token not in single_create:
        raise SystemExit(
            "STATIC FOOBAR AUDIT FAIL: exact-release single-disc create gate missing: " + token)
if not (
    single_create.find("validate_precision_cue_selected_disc(") <
    single_create.find("MessageBoxW(state.dialog, message.c_str()") <
    single_create.find("create_confirmed_precision_cue_selected_disc(")
):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: single-disc edition/confirmation gate reordered")

# The multi-disc create action is separate from search, preview, copy and
# single-disc create. It must remain impossible to dispatch without a real
# BN_CLICKED command, a whole-batch preflight and an independent modal YES.
# Static checks complement, never replace, the Windows host HWND/resource gate.
batch_click = (
    "LOWORD(wp) == IDC_PRECISION_SAVE_ALL_CUES && HIWORD(wp) == BN_CLICKED"
)
if batch_click not in precision or (
        precision.count("save_all_reviewed_disc_cues(*state)") != 1):
    raise SystemExit(
        "STATIC FOOBAR AUDIT FAIL: reviewed multi-disc create must require one real button click")
batch_handler = precision.split(
    "void save_all_reviewed_disc_cues(Workspace& state) {", 1
)[-1].split("void start_search(Workspace& state) {", 1)[0]
for token in (
    "IsDlgButtonChecked(state.dialog, IDC_PRECISION_EDITION_CONFIRMED)",
    "preflight_precision_cue_external_batch(",
    "batch, *state.release",
    "before.target_paths[i].wstring()",
    "MessageBoxW(state.dialog, question.c_str()",
    "MB_DEFBUTTON2",
    "create_confirmed_precision_cue_external_batch(",
    "invalidate_cue_copy(state)",
):
    if token not in batch_handler:
        raise SystemExit(
            "STATIC FOOBAR AUDIT FAIL: native multi-disc preflight/confirmation missing: " + token)
if (batch_handler.find("preflight_precision_cue_external_batch(") >
        batch_handler.find("MessageBoxW(state.dialog, question.c_str()") or
        batch_handler.find("MessageBoxW(state.dialog, question.c_str()") >
        batch_handler.find("create_confirmed_precision_cue_external_batch(")):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: batch consent/preflight order changed")
# A partial multi-file operation MUST show the precise per-disc destination
# and distinguish verified publishes from an uncertain latest publish. A
# generic error/exception loses vital user evidence after an earlier write.
for token in (
    "std::wstring outcome = L\"EXTERNAL CUE BATCH DID NOT COMPLETE.",
    "result.created_count",
    "result.failed_disc_index",
    "CREATED AND VERIFIED - RETAIN",
    "MAY BE PUBLISHED - INSPECT AND RETAIN",
    "before.target_paths[i].wstring()",
    "Do NOT delete, overwrite or automatically restore targets",
):
    if token not in batch_handler:
        raise SystemExit(
            "STATIC FOOBAR AUDIT FAIL: lost partial CUE publication evidence: " + token)
# Dismissal must invalidate the pending request and its reviewed export
# without joining a network worker on the foobar UI thread.
cancel_block = precision.split(
    "void cancel_pending_lookup(Workspace& state, bool announce) {", 1
)[-1].split("void initialize_disc_controls(Workspace& state) {", 1)[0]
for token in (
    "state.input_revision.cancel_request()",
    "state.job->worker.request_stop()",
    "state.job.reset()",
    "KillTimer(state.dialog, kPollTimer)",
    "update_lookup_ui(state, false)",
    "reset_disc_controls(state)",
    "clear_search_results(state)",
):
    if token not in cancel_block:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: native lookup cancel guard missing " + token)
if "job->worker.join()" in cancel_block or "job->worker.joinable()" in cancel_block:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: network worker joined on UI cancel")
for token in (
    "IDC_PRECISION_CANCEL_LOOKUP && HIWORD(wp) == BN_CLICKED",
    "cancel_pending_lookup(*state, true)",
    "install_precision_tooltips(*state)",
    "TTM_ADDTOOLW",
    "TTF_IDISHWND | TTF_SUBCLASS",
):
    if token not in precision:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: native Cancel/tooltip binding missing " + token)
resources_for_batch = (foobar / "component.rc").read_text(encoding="utf-8")
if "IDC_PRECISION_CANCEL_LOOKUP" not in resources_for_batch:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: native Cancel lookup control missing")
if "IDC_PRECISION_SAVE_ALL_CUES" not in resources_for_batch:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: batch create lacks a native control")

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
layout_storage = (foobar / "batch_table_settings.cpp").read_text(encoding="utf-8")
route_preview = (foobar / "routing_preview.cpp").read_text(encoding="utf-8")
legacy_profiles = (foobar / "legacy_routing_profiles.h").read_text(encoding="utf-8")
resources = (foobar / "component.rc").read_text(encoding="utf-8")

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

# Reopening the native context command with a different selection must
# invalidate all prior approvals and reject stale source files, even if a
# background provider request has not finished. This is only a static gate;
# the exact-head Win32/x64 real-host smoke remains independently mandatory.
reopen_body = precision.split(
    "if (g_precision_workspace && IsWindow(g_precision_workspace)) {", 1
)[-1].split("    try {\n        auto state = std::make_unique<Workspace>();", 1)[0]
for token in (
    "active->input_revision.cancel_request();",
    "active->job->worker.request_stop();",
    "active->job.reset();",
    "reset_disc_controls(*active);",
    "clear_search_results(*active);",
    "active->deezer_reference.reset();",
    "active->files.clear();",
    "active->files = capture_files(handles);",
):
    if token not in reopen_body:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: stale selection reuse: " + token)
if reopen_body.find("active->files.clear();") > reopen_body.find(
        "active->files = capture_files(handles);"):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: old physical selection retained")

# Precision CUE native mapping must be explicitly selectable and validated.
precision = (foobar / "precision_cue_workspace.cpp").read_text(encoding="utf-8")
# Regression: an earlier edition confirmation must never silently authorize
# a newly reviewed disc mapping or a new provider response.
invalidation = precision.split("void invalidate_cue_copy(Workspace& state) {", 1)[-1].split(
    "void update_reviewed_actions(Workspace& state)", 1)[0]
if "CheckDlgButton(state.dialog, IDC_PRECISION_EDITION_CONFIRMED, BST_UNCHECKED)" not in invalidation:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: stale Precision CUE edition approval survives preview invalidation")
for token in (
    "initialize_disc_controls(*state)", "assign_selected_disc(*state)",
    "review_precision_cue_disc_mapping(totals, physicals, *reviewed)",
    "show_candidate(*state, *state->release, &state->reviewed_mapping)",
):
    if token not in precision:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: Precision CUE mapping not wired: " + token)
# The native UI must validate before committing a proposed selection.
assign_body = precision.split("void assign_selected_disc(Workspace& state) {", 1)[-1].split(
    "void start_lookup(Workspace& state)", 1)[0]
if ("validate_precision_cue_partial_disc_mapping(" not in assign_body or
        "provider_disc_totals(*state.release, proposed.size())" not in assign_body or
        assign_body.find("validate_precision_cue_partial_disc_mapping(") >
        assign_body.find("state.reviewed_mapping = std::move(proposed);")):
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: partial mapping not validated before mutation")
if "if (validation.state != djmeta::PrecisionCueMatchState::Review)" not in assign_body:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: partial mapping rejection gate missing")
for token in (
    "COMBOBOX        IDC_PRECISION_ALBUM_RESULTS",
    "PUSHBUTTON      \"Search albums\", IDC_PRECISION_SEARCH",
    "PUSHBUTTON      \"Look up selected\", IDC_PRECISION_LOOKUP",
    "COMBOBOX        IDC_PRECISION_DISC_PICKER",
    "COMBOBOX        IDC_PRECISION_MP3_PICKER",
    "PUSHBUTTON      \"Assign disc\", IDC_PRECISION_ASSIGN_DISC",
    "PUSHBUTTON      \"Preview reviewed mapping\", IDC_PRECISION_PREVIEW_ASSIGNED",
):
    if token not in resources:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: Precision CUE resource missing: " + token)

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
    "31a35947a7527826c2199491692802e3ce3d8bd7",
    "2ba8caeb73f72633ad7f60ee5f72ed8074a0eb17",
    "SDK-PIN.json", "HEAD:vendor/sdk", "sdk-license.txt", "sdk-readme.html"
):
    if token not in sdk_bootstrap:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: pinned SDK Git-tree provenance missing: " + token)
if "Invoke-WebRequest" in sdk_bootstrap:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: unsupported repeated vendor SDK download")

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
for token in ('apply_review_split_geometry(', 'GetProcAddress(user32, "GetDpiForWindow")', 'state->active_dpi', 'WM_DPICHANGED'):
    if token not in batch_table_dialog:
        raise SystemExit("STATIC FOOBAR AUDIT FAIL: production responsive per-monitor layout missing " + token)
if 'apply_review_split_geometry(' not in gui_test or 'review_split_geometry(' not in gui_test:
    raise SystemExit("STATIC FOOBAR AUDIT FAIL: actual resized HWND geometry test absent")
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
