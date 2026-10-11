# Unified Prepare-Track Workflow

## Product goal

Music Metadata Studio replaces the current multi-step foobar2000 workflow with one explicit, reviewable plan:

1. analyze and normalize metadata;
2. derive the final filename from the canonical metadata;
3. derive the destination path and file action from the same canonical metadata;
4. preflight the complete batch;
5. show one unified preview;
6. apply only after explicit approval.

The shared engine computes desired state. It does not write tags or touch the filesystem.

## Host Policy Inheritance

foobar2000 remains the authority for host behavior wherever the host already defines policy.

The foobar component must therefore prefer supported foobar2000 SDK services and conventions for:

- metadata updates;
- title formatting;
- filename/path formatting behavior;
- rename/copy/move execution;
- timestamp behavior associated with host operations;
- progress/cancel integration;
- library/database notifications;
- Preferences, Apply/Cancel/Reset behavior;
- Dark Mode and DPI behavior;
- persistent component configuration.

The plugin must not create parallel settings for host behavior merely because it can.

If a required foobar policy is not exposed by the public SDK, the component may add a compatibility adapter only after that gap is documented and qualified. Such an adapter should emulate host behavior rather than invent a second user-facing policy.

## Configuration conventions

foobar-specific state belongs to foobar's normal configuration mechanisms and Preferences UI.

Rules:

- stable configuration GUIDs;
- cfg-backed persistence or the current SDK-equivalent host configuration service;
- Preferences changes are staged until Apply/OK;
- Cancel must not persist staged changes;
- Reset restores one centralized source of defaults;
- visible controls and HasChanged/apply logic use the same normalized readers;
- obsolete hidden settings are not deleted blindly when profile compatibility depends on their GUIDs;
- Dark Mode support is advertised and implemented through host hooks;
- DPI and keyboard/accessibility behavior follow native foobar conventions.

These conventions intentionally reuse lessons already qualified in `foo_smart_tempo` and `foo_metadata_isolation`; those repositories remain separate products and are not runtime dependencies.

The one intentional cross-surface exception is the shared versioned normalizer ruleset:

`%APPDATA%\DJMetadataNormalizer\ruleset.json`

It is shared because DJ Library and foobar must execute identical normalization semantics. It is not a substitute for foobar-specific component configuration.

## Desired-state plan

For each physical source file, the planner eventually emits a single desired-state record containing:

- exact source path;
- exact source identity / subsong evidence;
- exact metadata input fingerprint;
- exact ruleset revision;
- selected metadata proposals;
- canonical metadata document;
- evaluated filename;
- evaluated destination directory;
- final target path;
- file action: none / rename / move / copy;
- effective naming/routing profile plus any explicit manual overrides;
- associated external-cue/sidecar dependencies and proposed coordinated changes;
- per-target overwrite decision and optional batch-scoped overwrite approval;
- safety classification;
- rule provenance;
- conflict state.

Virtual subsongs may contribute metadata evidence but must not create duplicate physical file operations.

## Approved safety improvements

### Plan fingerprint

Approval is bound to the complete computed plan, not merely the tag proposal.

The future apply gate must invalidate the plan when any relevant input changes, including:

- metadata fingerprint;
- active ruleset revision;
- source path;
- naming/routing rule inputs, selected metadata proposals, selected route/action and manual overrides;
- computed target path and associated companion-file plan;
- overwrite decisions and batch-approval scope.

A stale plan is never partially trusted.

### Whole-batch preflight

Before the first mutation, the component evaluates the entire selected batch.

Preflight must detect at minimum:

- multiple sources targeting the same destination (a distinct, non-overridable intra-batch ambiguity unless the user first resolves the mapping);
- destination already exists (a reviewable overwrite conflict, not an unconditional prohibition);
- source disappeared or moved;
- illegal/unsupported target paths;
- empty filename/directory outcomes;
- duplicate physical operations caused by subsongs;
- unresolved REVIEW proposals;
- any stale metadata/ruleset/plan binding.

Foreseeable conflicts are reported before writes begin.

### Native title formatting for naming and routing

Filename and destination expressions should use foobar2000's own title-formatting implementation where the SDK exposes it.

The project must not grow an independent parser for `%field%`, `$if()`, or related foobar syntax.

Naming/routing evaluation always runs against the planned canonical metadata, so a metadata normalization and the resulting filename/path are one coherent transaction plan.

### Apply report

After execution, the component shows a compact host-native result report with counts and per-item failures/skips:

- metadata changed;
- renamed;
- moved;
- copied;
- unchanged;
- skipped;
- failed.

The report is traceability, not an independent backup database.

## Batch-scoped overwrite approvals

The existing foobar File Operations presets include `overwrite=yes`. The replacement must preserve the **ability** to overwrite without silently inheriting an unsafe unconditional overwrite mode.

- Default/ordinary operation: detect existing destinations and prominently warn in the unified preview and before final Apply; do not overwrite without approval.
- Individual override: approve a specific already-existing target.
- Bulk mode: approve **all explicitly enumerated overwrite conflicts in the reviewed batch with one deliberate confirmation**, without per-file prompts. The dialog states the number of destinations and which planned actions will replace data.
- Approval is scoped to the current plan and to the conflict targets enumerated at approval time; it is not a persistent global `overwrite=true` switch.
- A newly appearing destination, changed destination content, changed source, changed rule or changed plan is not implicitly covered by the old approval. Re-preflight/re-approval is required.
- Two selected sources resolving to the same target are an ambiguous intra-batch collision: never choose a winner solely by processing order or the bulk overwrite checkbox. Require manual routing/naming adjustment or explicit removal of a conflicting source from the plan.
- Non-overridable integrity gates (stale plan, unsafe physical/virtual tag mapping, ambiguous cue dependency, unverifiable file identity) remain enforced even in bulk mode.
- Safe, independent approved operations run as a batch; errors are collected in the apply report. The executor must not promise cross-file atomicity that foobar's public SDK does not provide.

The exact confirmation widgets should follow foobar's host UI conventions. This contract does not assert that the host exposes its native File Operations confirmation mechanism as a public SDK API.

## Date field semantics and flexibility

The initial user's established tagging convention is:

- `DATE` normally contains a four-digit year (e.g. `1998`).
- `DATE_RAW` is the separately retained original/full date when present (e.g. `1998-06-15`).

This is a **default user/profile policy**, not a hardcoded global engine rule. The normalizer may recognize discrepancies or propose changes according to explicitly configured field rules, but it must not:

- erase or truncate `DATE_RAW` merely because `DATE` is year-only;
- silently replace a valid full `DATE_RAW` with the year;
- invent day/month information not present in the source;
- rewrite an existing `DATE` or `DATE_RAW` based on a naming convenience;
- force other collections or profiles to use the same date representation.

Host Title Formatting expressions may derive a year or other display component without changing the persisted metadata. User-defined/custom tags remain first-class inputs to rules, naming and routing.

## Manual routing, naming and operation overrides

Automatic Single/Album/Liveset inference is a **proposed route**, not an irreversible decision.

At any time **before final Apply**, the user may override:

- route/profile (e.g. Singles ↔ Alben ↔ Livesets);
- output directory or per-item target path;
- naming/title-format expression or evaluated proposed filename;
- action (none / rename / move / copy);
- selection of individual metadata proposals;
- handling of companion files including external cues.

Overrides may target one track/physical source, a multi-selection, or all tracks in the current batch. The preview distinguishes automatic inference from manual overrides.

Each override triggers complete recomputation of every affected canonical/tag-dependent filename and path, conflict scan, cue/sidecar dependency mapping and **new plan fingerprint**. No manual override may bypass structural integrity checks.

Routing ambiguity stays in REVIEW until the user resolves it; a manually selected destination is allowed even when automatic classification was uncertain. The user is never forced to accept a folder hierarchy dictated by the engine.

Naming/routing profiles and host UI state use foobar configuration conventions; the shared rule engine stays the authority for metadata semantics.

## External CUE and companion-file coordination

Before a physical audio rename/move/copy, the planner must inspect related external `.cue` files wherever they are part of the selected source/companion operation, not only a matching basename guess.

The plan must distinguish:

- **External CUE**: a separate physical sidecar containing `FILE` references that may need updating;
- **Embedded cuesheet**: cue data stored in the audio container, handled under the separate physical/virtual metadata safety gate;
- **Other companion files** (artwork, logs, etc.): include only with a reviewed companion-file policy, not an uncontrolled `moveOtherFiles` clone.

For a qualified, unambiguous external CUE relation, expose user-selectable behavior:

- keep the CUE filename, but repair its `FILE` references when needed;
- rename the CUE alongside the related audio (e.g. the same basename), with references kept valid;
- use a separate foobar Title-Formatting naming expression for the CUE;
- leave the companion file untouched **only when that choice cannot create an invalid reference**; otherwise flag/block it;
- explicitly exclude the operation or source from the batch.

The default should prefer a safe, minimal coordinated update with a clear preview. Auto-selected choices remain manually editable for individual entries or whole batches.

CUE qualification must include: relative vs absolute `FILE` targets, multiple `FILE` directives and audio files, quoted/escaped paths, encodings, newline preservation, source/target collisions, and existing cue fields/timing/track structure. Unknown syntax, ambiguous linkage, unsafe encoding round-trips or changed source files cause REVIEW or a hard stop; never silently rewrite/drop structural cue data.

When `moveOtherFiles=yes` or `removeEmpty=yes` is present in a source foobar preset, reflect that behavior in the sidecar/dependency preview, but do not blindly move unrelated files or remove folders containing unresolved dependencies. Directory cleanup takes place only after all relevant actions have completed successfully.

## Acceptance scenarios for the eventual UI and executor

1. One existing destination -> clear warning, user may approve just this overwrite.
2. 100 existing destinations -> one explicit batch overwrite approval, no 100 popups; every target is listed and bound to the approved plan.
3. A new collision occurs after approval -> cannot overwrite on the old approval; refresh/review required.
4. Two selected sources produce the same target -> bulk overwrite approval is not sufficient; user fixes routing/selection.
5. `DATE=1998`, `DATE_RAW=1998-06-15` -> neither loses information because the other is used in folder naming.
6. Album auto-routed as Single -> user selects Alben, target path/filename and conflicts recalculate before Apply.
7. A renamed audio file has one qualified external CUE reference -> the preview includes selectable cue rename/reference-repair options.
8. A CUE references several physical audio files -> never treat it as a trivial one-audio basename rename.
9. Missing/malformed/unqualified CUE relations -> no automatic destructive rename/move/copy.
10. Approved operations preserve host-defined time/timestamp behavior where SDK policy can actually be inherited; undocumented behavior is not assumed.

## Write ordering

The intended executor sequence is:

1. revalidate the complete plan;
2. apply approved metadata changes through supported foobar SDK APIs;
3. obtain/confirm the resulting host metadata state;
4. execute approved rename/move/copy through supported host services where available;
5. collect final outcomes;
6. show the apply report.

Exact ordering may be refined if the foobar SDK's file-operation services impose a stronger transactional contract, but it must never bypass stale-plan validation.

## Physical vs virtual metadata

The project must preserve the safety lesson proven by `foo_metadata_isolation`: a projected virtual-subsong `file_info` is not automatically a safe authority for physical tags.

Before tag writes are enabled, the foobar adapter needs a dedicated physical-vs-embedded-cuesheet qualification gate.

Generic virtual writes that could reconstruct incomplete physical metadata are forbidden.

## User-facing scope

The normal workflow should ultimately be:

**Prepare Tracks... → Analyze → Unified Preview → Apply**

The preview combines:

- metadata changes;
- final filename;
- final destination;
- action;
- SAFE / CONFIDENT / REVIEW;
- conflicts and stale-state status.

No background write or automatic file move is permitted.


## SDK planner qualification note

The public foobar2000 SDK exposes `titleformat_compiler` as a core service and a standard `file_info`-based evaluation path. The planner therefore evaluates naming/routing expressions against an in-memory canonical metadata projection using the host compiler; it does not implement foobar title-format syntax itself.

The public SDK also exposes low-level filesystem move/copy primitives and file-operation notifications, but the project has not yet identified a parameterized public service that executes foobar's built-in File Operations command with its complete user policy. Therefore the current milestone is deliberately planning-only. The eventual executor remains gated on an explicit host-policy mapping for timestamp and File Operations behavior.


## Portable batch preflight engine (implementation milestone)

The C++20 core now defines `include/djmeta/batch_plan.h` and
`src/core/batch_plan.cpp` as an **analysis-only, pure** batch planner. It is
explicitly separate from the future foobar execution and UI adapters.

The caller supplies each physical file exactly once, including:

- host-canonical source/destination identity keys (the portable engine does
  not guess Windows path equivalence or foobar path normalization);
- a nonempty, host-observed source identity/version guard for **every** row,
  including metadata-only, external CUE and companion rows; changing the
  observed source after approval invalidates the entire batch;
- a coherent ruleset revision across **all** selected rows, including
  otherwise unchanged or tag-only rows;
- original and selected canonical metadata fingerprints;
- ruleset revision, route/profile, naming expression and manual override;
- host-observed destination presence and, when present, an identity guard;
- qualified external-CUE linkage and reference postimage fingerprint;
- separately approved sidecar policies.

The engine computes a SHA-256 `djmeta-batch-v2` plan fingerprint using length-prefixed fields
and returns per-item statuses: Ready, Unchanged, NeedsOverwriteApproval or
Blocked. It never opens files, writes tags, parses Cue files or initiates a
move/copy. `BatchApproval` binds a single bulk-overwrite confirmation to
the exact snapshot; two sources mapped to one destination still block.

CI tests cover 100 existing destinations under one reviewed batch consent,
stale approval when a target identity changes, item-level overrides,
intra-batch target collisions, selected-source collisions, subsong duplicate
physical operations, CUE dependency gating, companion authorization and
immutable fingerprints.

**Not yet delivered:** foobar-specific target probes/identity derivation,
external CUE parser and reference rewrite, interactive routing UI,
native Apply/Cancel dialogs, output of plans via the shared C ABI, or an
executor. The `ready_to_apply` core result is advisory only; the future
executor must re-read all source and destination guards immediately before
any mutation.

## No implicit field deletion through normalization

The read-only engine never converts a nonempty tag value to an empty
postimage, even when trimming all whitespace would normally yield the
empty string. SAFE staging and Accept review reject malformed proposals
that try to do so. Reject keeps the original bytes untouched.

A future explicit Delete Field / Delete Value choice will require its own
per-field confirmation, physical-vs-virtual proof, and source fingerprint
guard. No automatic blanket trim or overwrite approval grants deletion.

### Source-observation and ruleset-coherence contract

An uninspected physical source is a hard block even for tag-only selection:
`SOURCE_NOT_INSPECTED`. The source guard is host-supplied, **not** inferred
from a path string, metadata fingerprint or file size alone. The planner binds
it to the immutable batch fingerprint, while the future foobar executor must
independently re-probe before every mutation. A change in this guard makes a
previous batch approval stale, including approval of existing destinations.

All items of a batch must use the same ruleset revision. A mixture is blocked
as `MIXED_RULESET_REVISIONS` for every row rather than applying different
normalization snapshots to CUE, companion and audio files. This is an
analysis-only preflight change: no source probing or writes were added to the
portable engine, and the component does not yet construct executable plans.

### Read-only host filesystem observation (preview-only milestone)

The native foobar preview has a **read-only physical source probe**. It first
uses the pinned public foobar SDK filesystem handler to canonicalize each
selected source, extract an actual native path and obtain host file statistics.
On supported Windows physical paths it opens only file attributes and reads
the OS volume/file ID, size, creation and last-modification timestamps and
attributes. Unsupported, reparse-point, missing or inaccessible sources are
explicitly unqualified. A portable selection gate rejects multiple paths to
the same OS physical file ID (hardlink aliases), as well as virtual subsongs
and duplicate selected host paths. Native details are visible as tooltips.

This host observation is **not** the final host source/target/CUE approval
gate: the current native preview deliberately retains both
`filesystem_target_checked=false` and `cue_dependencies_checked=false`.
The source guard is held in preview state only and cannot authorize any
mutation; a future executable plan needs a complete live re-probe immediately
before each operation. Candidate target path sanitization and probing,
external CUE linkage, low-level host File Operations preferences, timestamps
and cancel semantics remain unresolved. The portable host policy function
has deterministic Linux/Windows regression tests; component compilation and
packaging are checked separately for Win32/x64. Neither is an installed-host
acceptance result.

Host filesystem physical probes add per-file I/O, especially on large/NAS
batches. Before enabling any writer, qualify latency, progress/cancellation
and batching against representative 1/100/15,000-item selections. Use the
SDK's available read-only batch-stats API where practical without replacing
true physical identity evidence with a guessed path key.

### Raw Title Formatting path safeguard

Before foobar File Operations defines the final destination, the read-only
portable preview now flags obvious unsafe **raw relative path** fragments with
`UNSAFE_RAW_RELATIVE_TARGET`. The conservative detector rejects absolute or
traversing paths, empty segments, Windows device aliases such as `CON.mp3`
or `LPT1`, alternate-data-stream colons, control characters, malformed
UTF-8 and invalid/trailing Windows filename characters. Case-preserving,
non-ASCII ordinary names remain allowed.

This deliberately does **not** reimplement foobar's target-name sanitization:
foobar may legitimately repair a raw filename in its native File Operations
dialog. Such paths remain REVIEW/unqualified until a read-only adapter can
compare the **actual host-resolved post-sanitization destination** and its
filesystem identity. The existing destination and CUE gates remain in force.

### Optional on-demand raw candidate destination inspection

The File locations view now supports **right-click selected rows → Inspect
selected raw targets (read-only)**. It can inspect only the current raw
Title Formatting candidate path, not foobar File Operations' eventual
sanitized path or automatically appended extension. The optional probe
reuses the SDK-canonical/Win32 read-only host file observation, and reports:

- raw candidate absent (observed at that instant; not a promise it stays absent);
- existing raw candidate with guarded physical identity (never overwrite consent);
- existing raw candidate that aliases the same or another selected source;
- distinct raw candidates that alias one existing physical target via hardlinks;
- unqualified inspection, e.g. inaccessible/reparse/unsupported paths.

The user must first apply any route-form edits to the **preview only**.
The operation verifies current source guards, all metadata snapshots and the
rules snapshot and publishes results only when the entire requested subset
is consistent. Preview sorting does not change the selected underlying track
identities. Changed metadata/naming/routing invalidates the provisional
target observations. These results are visible in native row statuses and
tooltips, but never make `filesystem_target_checked` true; external-CUE
qualification is independent and also remains false.

This provisional check is deliberately **opt-in** so merely scrolling/sorting
large libraries does not trigger more filesystem I/O. The synchronous GUI
action is temporarily bounded to 128 selected candidates per invocation,
pending a qualified asynchronous, cancelable, SDK-backed bulk preflight.
The final host File Operations path, source-extension behavior, timestamp
policy, collisions and CUE repair still require independent qualification
before any executable plan is available. No file writer was introduced.

### Per-transaction Title Formatting compiler reuse

The native foobar routing and Prepare Tracks preview now compile each distinct
host Title Formatting expression **once per preview or editing transaction**
rather than recompiling the same script for each selected track. Compiled
objects are held in a short-lived transaction-local map and released after
the current operation. The same pinned foobar SDK compiler and `run_simple`
implementation remain responsible for evaluation against the identical
canonical metadata projection; no alternative titleformat interpreter or
persistent/global cache was added.

This is a structural reduction in compilation work, **not** an independently
measured wall-clock speedup. Large-batch profiling and GUI responsiveness
with real 1/100/15,000-file collections still require installed-foobar host
qualification. No metadata or filesystem writer is introduced.

### Mandatory zero-write/no-op protection (all foobar components)

The mandatory cross-component rule is: **an unchanged approved postimage must
cause zero write-capable calls**, including no unnecessary media reserialization,
rename/copy, ReplayGain, external/embedded CUE rewrite, sidecar replacement or
timestamp update. The canonical cross-repository policy is
`lxsdd/dev-infrastructure/STANDARDS/FOOBAR2000-NOOP-WRITES.md`.

The portable Normalizer has gained a separately tested physical-text-tag
comparison (`include/djmeta/write_guard.h`), which compares full field/value
sequences while ignoring only field enumeration order and ASCII tag-name
letter case. Multi-value order and value casing remain significant. Duplicate
case-insensitive names, missing values or embedded NUL cannot silently pass
as an equal qualified postimage. External CUE equality is byte-for-byte,
including encoding and line endings. Technical fields, binary artwork,
ReplayGain and embedded-CUE metadata need their own dedicated guards.

The portable batch reviewer independently describes
`metadata_write_needed`, `cue_bytes_write_needed` and
`file_operation_needed`. If the approved tag fingerprints are identical,
the plan does not request tag writes even when a file move is required.
A metadata-only edit is **Ready**, not `Unchanged`, and does not
request filesystem movement. A redundant move-to-self with identical tags
is `Unchanged`; a redundant move-to-self with changed tags remains tag-only.
Identical CUE byte postimages never require rewriting. Every blocked or stale
decision clears all mutation intents.

These flags are **advisory**, and a fingerprint difference is not by itself
permission to write: a future physical-tag writer must compare the actual
*live full physical tag projection* immediately before opening anything for
writing and avoid any write when the live approved postimage already exists.
A CUE writer must similarly compare exact bytes and reverify the reference
mapping. Changes solely to field listing order must remain no-ops. A
`FrozenBatchPreview` is still not an executable permission, and no productive
writer has been introduced.

The first required real-foobar acceptance includes a zero-write observation
of an unchanged MP3 and CUE, preservation of file modified/created times,
tag-only vs move-only isolation and proving that virtual subsongs never cause
partial physical ID3 serialization. The current preview-only component
cannot independently demonstrate writer-call suppression because it has no
productive writer; no such claim is made.
