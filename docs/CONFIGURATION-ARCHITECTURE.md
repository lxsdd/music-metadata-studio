# Music Metadata Studio configuration contract (2026-10-24)

All cross-workflow configuration, UX and rules requirements are indexed
under the stable `MMS-CFG-*`/`MMS-NRM-*` IDs in
[REQUIREMENTS-REGISTER.md](REQUIREMENTS-REGISTER.md). The index is kept in
version control and checked in Core CI; this document remains the detailed
configuration contract.

## Two deliberate levels

1. **foobar2000 Preferences > Tools > Music Metadata Studio**
   stores durable profile-wide choices via foobar-native cfg variables.
   **Workspace** is a child page for normal daily defaults.
   Existing parent page controls only context-menu captions; foobar's
   Display > Context Menu editor continues to own menu visibility.
2. The modeless **Studio workspace** owns the current batch:
   the source selection, provider search, field approvals, routing
   overrides, and final explicit write confirmation.

No editing of Preferences initiates provider HTTP requests, touches music files,
approves semantic normalizations or grants write rights. Settings apply to new
Studio windows, not to an in-progress review. The active user may override all
of these defaults within the current workspace without mutating Preferences.

## Implemented settings in this slice

The new **Workspace** child page offers:
- opening view (Find / Review / Files), default Find;
- initial provider (MusicBrainz / Discogs / Beatport + Crates / first supported
  source in saved priority), default first supported;
- default route preview (Singles / Alben / Livesets), default Singles;
- show invisible whitespace initially in normalization review, default Off.

Every setting uses a versioned strict schema in the active foobar profile.
Corrupted or unknown schemas reset to safe display defaults. Only Apply/OK
commits them; Reset stages defaults and Cancel discards changes. Existing profile choices including a saved manual MusicBrainz selection remain
unchanged. The new default only affects profiles without a saved setting.
Selecting the first supported source configures the initial **manual** search
control; it never starts an HTTP lookup or skips a failed network request.
No provider
token, search content, track identity or review decision is persisted.

Other existing state uses independent foobar profile keys: window placement,
window size, grid order/width/visibility, and preferred date-target selector.
The shared normalizer ruleset is loaded (read only) from
`%APPDATA%\DJMetadataNormalizer\ruleset.json` or an embedded default.
Do not duplicate these stores or reuse their GUIDs/schema for new preferences.

## Final planned Preferences hierarchy

- General: language, workspace start behavior, view density.
- Online Metadata: enabled sources, per-field source priority, manual search
  behavior and authenticated connection status; secrets never in public
  config/logs, no surprise network requests.
- Metadata Fields: include/exclude fields, exact DATE/ORIGINALDATE meanings,
  source-specific mappings, preservation of existing fields.
- Normalization: versioned rule packs, SAFE/CONFIDENT/REVIEW severity,
  alias management, case, artist collaboration, genre and version extraction,
  illustrated before/after rule editor, import/export and individual enable.
- File Organization: named user-maintained profiles, target templates, safety
  validation and legacy preset import; no implicit move/overwrite.
- CUE: physical/virtual scope, preservation, global/inherited tag policies,
  external/embedded CUE qualification.
- Safety: no-op suppression, full-batch conflict review, backup behavior,
  explicit final commit (non-removable guard).
- Appearance: persistent multi-grid columns, filter/selection state, DPI,
  window geometry, accessible shortcuts, Dark Mode and tooltip lifecycle.

A new persisted display preference must never change the write gate. Any
source-scoring/normalization profile changes must invalidate cached previews
and force a fresh read-only projection before approval.

## Acceptance gates

- MSVC Win32 and x64 foobar component build.
- Native Windows Preferences instance can be opened, saved, canceled,
  reset and reopened without overwriting menu captions or grid settings.
- All 54 combinations of opening view, provider, route and whitespace
  round-trip; corrupt/future versions revert to safe defaults.
- Every opening view/provider combination triggers **zero** network calls
  until the user explicitly searches and **zero** metadata/file writes.
- Reopening a new workspace follows Preferences; an already open workspace
  preserves its in-memory actions; no approvals survive restart.
- Smoke test of modeless foobar focus/minimize/maximize, DPI, and native
  layout remains a later real-host acceptance gate.
