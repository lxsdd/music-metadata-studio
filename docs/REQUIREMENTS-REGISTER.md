# Music Metadata Studio — verbindliches Anforderungsregister

**Stand:** 2026-10-24 · **Produkt:** foobar2000 Music Metadata Studio
**Entwicklungszweig:** `feature/online-metadata-discovery-foundation-20261009` · **Integration:** Draft PR #45

> **Source of truth for WHAT must be delivered.** This file collects the user-confirmed
> workflow, UI, configuration, rules, provider, CUE, file-management, safety and
> performance requirements in one stable index. The linked technical documents/issues
> remain authoritative for HOW each requirement is implemented. A green build is
> not evidence of visual or productive host acceptance.

## Änderungs- und Nachverfolgungsregeln

1. Each requirement retains its `MMS-<AREA>-NN` identifier permanently.
   New requests receive a new ID. Do not delete or silently weaken an ID,
   including when implementing a simpler initial version.
2. Status means: **OPEN** (not delivered), **PARTIAL** (some code or tests),
   **CI** (implemented and exact-SHA CI qualified, host gate pending),
   **HOST** (accepted in installed foobar for that scope), **BLOCKED**
   (document precise external prerequisite). This register is not a
   substitute for evidence; status upgrades require a PR/commit and test link.
3. Every relevant PR/commit must list covered IDs, safety effects and
   verification. Every new owner request must be added here before marking it
   complete, even if the implementation spans multiple branches.
4. Never equate a technical default with permanent approval. Source matches,
   rule changes, file replacements and physical CUE operations require
   current-batch review and exact current-source revalidation.
5. Distinguish **software-complete** from **requires one final personal foobar
   acceptance**. Do not ask for repeated throwaway Windows tests. Do not
   merge, tag, publish binaries or enable production writers without the
   owner-approved final gate.
6. The separate `foo_discogger`, `foo_metadata_isolation`,
   `rekordbox-mytag-sync` and `foo_smart_tempo` products keep their own
   issue lists and releases. Shared infrastructure and rule semantics may be
   reused without duplicating production functionality here.
7. On each major continuation inspect this register, Draft PR #45 and
   open issues #33, #46–#51 before coding. Resolve discrepancies visibly.
   A later change may add requirements but must not drop earlier promises.

## WF — durchgängiger Alltagsablauf

| ID | Status | Verbindliches Verhalten und Abnahme | Detail |
|---|---|---|---|
| MMS-WF-01 | PARTIAL | One **modeless single-instance workspace** alongside foobar playback, playlists and Properties; no application-locking wizard or forced topmost; minimize/maximize/taskbar; preserve an existing in-progress batch. | #47 |
| MMS-WF-02 | PARTIAL | One intelligible journey: **Find/compare → normalize/review → file plan → one final confirmation**, with no redundant dialogs or double entry. Three central views for daily work; inline guidance. | #48, [workflow](UNIFIED-WORKFLOW.md) |
| MMS-WF-03 | PARTIAL | Select one/many tracks from foobar; show one compact **row per track** with grouped expandable field differences rather than an endless one-row-per-change list; multi-selection and clear status counts. | #48, #49 |
| MMS-WF-04 | OPEN | Final workspace clearly separates **Metadaten, Dateien, Verlauf** without scattering actions across windows; shared staged canonical metadata is the only input to routing. | [workflow](UNIFIED-WORKFLOW.md) |
| MMS-WF-05 | PARTIAL | Accept/Reject/Reset/manual value for a field, selected tracks or all; applicable actions must name their scope. No provider result or SAFE rule silently approves a write. | #49 |
| MMS-WF-06 | PARTIAL | Batch overview reports changed/unchanged/blocked/conflicting and shows precisely what will happen before **Änderungen prüfen & ausführen**. No-op rows stay out of writes. | #33, #50 |
| MMS-WF-07 | OPEN | After execution show a compact per-item report, skips/errors and a useful **history** for the current workflow; no duplicate permanent metadata database. | [workflow](UNIFIED-WORKFLOW.md) |
| MMS-WF-08 | PARTIAL | Incomplete/stale/switching track or source shows explicit loading/stale indications; long tasks give progress, cancellation when safe, and preserve still-valid in-memory decisions. | #47, #48 |
| MMS-WF-09 | OPEN | Consistent workflow for 1, 10, 100 or 500 selected items; clear empty/loading/ambiguous states, without requiring repetitive individual dialogs. | #50 |

## UI — konsistente foobar-Bedienung

| ID | Status | Verbindliches Verhalten und Abnahme | Detail |
|---|---|---|---|
| MMS-UI-01 | PARTIAL | Native text baselines: search **Edit and Search** button, scope selector and adjacent actions align visually at DPI 100/125/150/200%; no clipping, truncation or overlapping elements. | #48, #33 |
| MMS-UI-02 | PARTIAL | Double-click/Enter on a source track triggers guarded explicit search in Find view; elsewhere navigates to Find without a network request. Double-click/Enter on release loads inspected edition. | #48 |
| MMS-UI-03 | PARTIAL | Hover tooltips explain icons, controls, stage and consequences, disappear at correct lifetime; no lingering tooltip over other windows or after switching stages. | #48 |
| MMS-UI-04 | PARTIAL | Persist window position, logical size, maximized state, per-grid column width/order/visibility/sort; custom widths survive track changes/restarts; initial auto-fit only before customization. | #33, #47, #49 |
| MMS-UI-05 | PARTIAL | Native modeless focus/keyboard/Tab/Escape/Alt-Tab and light/dark theme; no forced foobar owner blocking; minimize/maximize/repeated open/close robust. | #47, #33 |
| MMS-UI-06 | OPEN | Accessible icons, high-DPI scaling and consistent spacing; no horizontal page scroll when reasonable; column context menu/reset and keyboard alternatives. | #33 |
| MMS-UI-07 | PARTIAL | Track/result filters, source provenance and clear field comparison; optional invisible-character display; selection remains stable across sort/filter. | #49 |
| MMS-UI-08 | OPEN | Consistent design/behavior across all Studio subviews: shared control metrics, dialog policy, tooltip lifecycle, status/loading style and keyboard semantics; regression tests common to foobar products where applicable. | #33 |
| MMS-UI-09 | OPEN | Compact, responsive wide-window layout: source list, grouped review details, and provider/conflict evidence; panes resize with sensible minima and preserve user geometry. | #48, #49 |

## CFG — zwei Ebenen von Einstellungen

| ID | Status | Verbindliches Verhalten und Abnahme | Detail |
|---|---|---|---|
| MMS-CFG-01 | PARTIAL | Native foobar **Preferences > Tools > Music Metadata Studio** for persistent per-foobar-profile settings, with Apply/Cancel/Reset and stable GUIDs; no parallel host configuration framework. | [configuration](CONFIGURATION-ARCHITECTURE.md) |
| MMS-CFG-02 | PARTIAL | Child page Workspace: opening view, initial provider, default Singles/Alben/Livesets route and whitespace display. Stored via strict versioned safe schema; changes affect **new** windows only. | Commit 67bb646 |
| MMS-CFG-03 | OPEN | Preferences areas: General, Online Metadata, Metadata Fields, Normalization, File Organization, CUE, Safety and Appearance, with simple defaults and optional advanced controls. | [configuration](CONFIGURATION-ARCHITECTURE.md) |
| MMS-CFG-04 | OPEN | Per-job overrides (provider, rule and file profile, selected fields, routing) directly in Studio, without editing global Preferences; cog opens relevant settings. | [configuration](CONFIGURATION-ARCHITECTURE.md) |
| MMS-CFG-05 | OPEN | User-maintained **named profile** create/duplicate/rename/delete, import/export and portable migration; Singles, Alben, Livesets, normalize-only and search-only as useful presets. | [workflow](UNIFIED-WORKFLOW.md) |
| MMS-CFG-06 | PARTIAL | Persist only UX choices (window, columns, filters, selected profile, source preference); **never persist an approval or batch overwrite authorization** across a new plan/restart. | #33 |
| MMS-CFG-07 | OPEN | Full field include/exclude, priority and mapping editor that does not silently introduce unnecessary extra DJ tags or overwrite user-specific DATE/GENRE/BPM conventions. | #49 |
| MMS-CFG-08 | OPEN | Rule-profile import/export/version/migration, staged validation and explicit preview of effects before changing the active rule revision; reset to deterministic default. | [architecture](ARCHITECTURE.md) |
| MMS-CFG-09 | OPEN | Source account connection/status UI with secrets stored only through an appropriate approved secure mechanism; never in rules JSON, public CI, log, URLs or browser clipboard. | #50 |
| MMS-CFG-10 | OPEN | Settings search/help, sensible first-run defaults, easy reset of view/profile and no mandatory first-run wizard or extra foobar plugins. | #48 |

## NRM — gemeinsame Normalisierungs-Engine

| ID | Status | Verbindliches Verhalten und Abnahme | Detail |
|---|---|---|---|
| MMS-NRM-01 | PARTIAL | One deterministic versioned engine/ruleset shared by foobar and DJ Library (read-only Bridge), keeping raw and canonical values distinct and immutable in analysis. | [architecture](ARCHITECTURE.md) |
| MMS-NRM-02 | PARTIAL | SAFE scalar whitespace / Unicode normalization with protected CUE/lyrics/multiline data, exact value identity and no implicit blank-field deletion; malformed UTF-8 rejected. | [architecture](ARCHITECTURE.md) |
| MMS-NRM-03 | OPEN | Semantic artist and label aliases; feat./featuring/vs./collaboration delimiters and artist credits, with case and exception dictionaries. New alias rules default to REVIEW. | #49 |
| MMS-NRM-04 | OPEN | Title/version/mix/remix logic (Extended Mix, Radio Edit, Original Version, DJ Version), parentheses/separators and cross-field TITLE ↔ VERSION decisions. Preserve ambiguous version text. | #50 |
| MMS-NRM-05 | OPEN | Configurable genre/style aliases and casing, Unicode/spaces/separator conventions and user-specific exceptions, never forcing a genre taxonomy without review. | #49 |
| MMS-NRM-06 | OPEN | Graphical rules editor: enabled/disabled, priority, stable IDs, search/filter, reason, before/after sample and impact preview, safe reset/import/export. | [architecture](ARCHITECTURE.md) |
| MMS-NRM-07 | OPEN | **Create rule from my correction** only when explicitly requested; rule starts REVIEW, shows scope/sample/collision effects and never auto-learns in background. | [configuration](CONFIGURATION-ARCHITECTURE.md) |
| MMS-NRM-08 | PARTIAL | SAFE/CONFIDENT/REVIEW visible in grouped review; semantic proposals require explicit individual or batch approval, Reset restores pending; stale/reordered decisions rejected. | #50 |
| MMS-NRM-09 | OPEN | Connect accepted **normalizer decisions**, not just selected online provider fields, to a host-safe physical/CUE writer after exact fresh preflight; skip unchanged files entirely. | #33, #50 |
| MMS-NRM-10 | PARTIAL | Lossless multivalues, duplicate same-named fields, custom fields and current original tags regardless of whether a rule/provider changed them. | #49 |

## SRC — Online-Metadaten und Feldbedeutung

| ID | Status | Verbindliches Verhalten und Abnahme | Detail |
|---|---|---|---|
| MMS-SRC-01 | PARTIAL | User-initiated MusicBrainz and authenticated Discogs discovery; title/artist/version plus fallback ISRC/catalog/barcode/release IDs and exact provider-ID dedup; no silent network requests. | #50, #48 |
| MMS-SRC-02 | PARTIAL | Exact release/master/edition/tracklist matching, manual URL input, double-click load, paginated search, confidence and source attribution, preserve disputed mix identities. | #50 |
| MMS-SRC-03 | PARTIAL | Discogs documented API and its authentication/license/attribution policies; use central protected QA evidence without exposing personal tokens. | #50 |
| MMS-SRC-04 | PARTIAL | Beatport + Crates lawful public URL/clipboard workflow; unsupported/deprecated/credential-gated Beatport, Deezer or Spotify endpoints clearly disabled instead of simulated. | #50 |
| MMS-SRC-05 | PARTIAL | Compare full available original fields with provider evidence, including label/catalog/edition, credits, genre/style, remixers, ISRC, release dates, custom fields and multivalues. Unknown/absent never means delete. | #49 |
| MMS-SRC-06 | PARTIAL | Separate recording, mix, original release, exact edition and CUE-global/track provenance; date values are not interchangeable. | #49 |
| MMS-SRC-07 | OPEN | Field-specific source priority/allowlist/ignore policy, including transparent conflicts, user-selected values and repeatable confidence explanation. | #49 |
| MMS-SRC-08 | PARTIAL | Concrete regression search cases: The KLF — What Time Is Love (Original Version) and difficult DJ cross-artist/version queries, alongside current official provider tests. | #50 |
| MMS-SRC-09 | PARTIAL | DATE as user's year-only convention, DATE_RAW as full existing date when present, ORIGINALDATE vs edition RELEASEDATE separately configurable; BPM is already supplied and should not be recalculated. | [workflow](UNIFIED-WORKFLOW.md) |

## CUE — physisch/virtuell und Precision CUE

| ID | Status | Verbindliches Verhalten und Abnahme | Detail |
|---|---|---|---|
| MMS-CUE-01 | PARTIAL | External and embedded text CUE: global, inherited, track fields and complete original inventory, Unicode/encoding, multivalues and safe structural preservation. | #46 |
| MMS-CUE-02 | PARTIAL | Strict physical audio tags vs virtual subsong isolation; never write a generic virtual handle; ReplayGain/custom/unknown frames and audio payload remain untouched. | #46, #33 |
| MMS-CUE-03 | PARTIAL | Match full/partial, single/multi-file and multi-disc CUE tracks to exact source edition; blocked ambiguous mix/version/track alignment, field provenance per scope. | #46 |
| MMS-CUE-04 | PARTIAL | Dedicated byte-preserving external CUE field writer with independent final user confirmation and no-op; verified only within guarded synthetic/QA scope. | #46 |
| MMS-CUE-05 | OPEN | Qualified embedded textual CUE writer for each supported format with physical-tag isolation; native FLAC Type-5 remains read/preserve until a safe binary writer is individually qualified. | #46 |
| MMS-CUE-06 | OPEN | Coordinated audio rename/move plus external CUE filename and all FILE references, handling unselected references, orphan risks and explicit sidecar choices. | #46, [workflow](UNIFIED-WORKFLOW.md) |
| MMS-CUE-07 | PARTIAL | Precision CUE read-only multi-disc audio-free boundary estimator with exact edition/duration agreement, 75-fps integer quantization and visible ±frame uncertainty. Never claim sample accuracy from provider times. | #51, PR #52 |
| MMS-CUE-08 | OPEN | Explicitly approved external CUE export/generation and qualified embedded option for continuous mixes, without full track downloads or modification of source MP3 during discovery. | #51 |

## FILE — sichere Dateiorganisation

| ID | Status | Verbindliches Verhalten und Abnahme | Detail |
|---|---|---|---|
| MMS-FILE-01 | PARTIAL | Native foobar Title Formatting, no duplicate parser; compute proposed name and folder from exactly the same accepted staged metadata as tag preview. | [workflow](UNIFIED-WORKFLOW.md) |
| MMS-FILE-02 | PARTIAL | Singles/Alben/Livesets presets with editable per-job destination/pattern; eventual persistent named profiles and per-track multi-select overrides. | #33 |
| MMS-FILE-03 | PARTIAL | Whole-batch preflight: source identity/staleness, target collisions, same-key/case aliases, permissions, CUE/sidecars and dangerous paths, before first mutation. | #33 |
| MMS-FILE-04 | OPEN | Rename/move/copy/no-action through qualified foobar host policy, explicit final batch approval and progress/results, not ad hoc invisible file operations. | #50 |
| MMS-FILE-05 | PARTIAL | Overwrite individual or explicitly enumerated batch targets only, never global overwrite=yes; impossible intra-batch collisions are non-overridable. | [workflow](UNIFIED-WORKFLOW.md) |
| MMS-FILE-06 | PARTIAL | Zero file/tag/CUE/timestamp rewrite when desired output equals original; no-op verified through real synthetic host QA as well as offline tests. | #33 |
| MMS-FILE-07 | OPEN | Backup and recovery strategy where host/file format permits; accurately report partially executed batches and cancellation limits, without implying impossible cross-file atomicity. | [workflow](UNIFIED-WORKFLOW.md) |

## OPS — Qualität, Wartbarkeit und Abschluss

| ID | Status | Verbindliches Verhalten und Abnahme | Detail |
|---|---|---|---|
| MMS-OPS-01 | PARTIAL | Shared foobar Win32/x64 SDK templates, pinned dependencies, reusable host UI/design/CI helpers; one normalization core, no duplicated product-specific engines. | #50 |
| MMS-OPS-02 | PARTIAL | CI on exact commit: portable core, Windows RC/HWND, Win32/x64 foobar hosted synthetic no-op, CUE/physical writer gates and non-secret provider tests; compare the same SHA before claiming a PASS. | #33 |
| MMS-OPS-03 | PARTIAL | 40k+ catalog performance, virtual list stability, UI responsiveness, bulk preview complexity and bounded memory, with representative stress fixtures. | #50 |
| MMS-OPS-04 | PARTIAL | Public CI must not leak tokens, library data or publish component binaries; preserve protected environments and separate private local acceptance instructions. | #50 |
| MMS-OPS-05 | OPEN | One final consolidated installed-foobar acceptance using disposable files, both architectures/DPI/theme, field/CUE/write paths, exact SHA and SHA256 report; no repeated intermediate user tests. | #33, #50 |
| MMS-OPS-06 | OPEN | No merge, release, tag or production write activation without final user gate; remaining legitimate provider limitations separately recorded and visibly disabled. | #50 |
| MMS-OPS-07 | OPEN | Every development PR explicitly maps changed behavior to MMS IDs, tests and status; do not silently close requests on green CI alone. | This register |

## Last register checkpoint

- 2026-10-24: Initial consolidation of previous user feedback, proposed final
  workflow, configuration hierarchy and earlier open issues #33, #46–#51.
- Code milestone `67bb646`: Workspace Preferences defaults persisted in foobar.
  **Do not mark MMS-CFG-02 CI/HOST before exact-SHA gates finish.**
- Historical technical pages remain linked for detailed algorithms and fixture
  gates. Older prose may describe prior phases; this register + exact current
  source/CI determines implementation status.
- 2026-10-24: Studio-focus performance qualification (MMS-OPS-03):
  ordinary activation and switching to a different source revalidate
  only the selected stable source identity, with full-batch fallback if the
  index is invalid. Full batch verification remains mandatory for batch
  review decisions and Apply; selected-source online operations use the
  source-local evidence guard. Exact-SHA CI and installed-host responsiveness
  remain separate gates.
- 2026-10-24: Stable master-grid filename order (MMS-OPS-03 /
  MMS-WF-03): ascending and descending original-filename sort orders are
  memoized independently for a captured snapshot. Accept/Reject/Reset
  change review counts but cannot change the original filename key; each
  recalculation now filters cached order rather than performing another
  54k-row stable sort. Count columns still re-sort dynamically; explicit
  Refresh clears both caches. Unit tests cover decisions changing counts,
  Unicode-independent stable equal-name ties and separate sort directions.
- 2026-10-24: Immutable master-grid sort labels (MMS-OPS-03):
  UTF-8 basename keys for original sources are calculated once alongside
  the existing Unicode display labels, and swapped together on Refresh.
  Review decisions and column/filter changes reuse these keys instead of
  allocating 54,129 new strings before sorting. The source filename is
  never inferred from a proposed destination; stale cache count blocks
  the update. Native static audit enforces the reuse; host latency remains
  unqualified until installed Windows acceptance.
- 2026-10-24: Indexed local normalization review (MMS-OPS-03 /
  MMS-WF-03): the per-track detail grid now accesses an immutable
  source-to-proposal index built once with each captured snapshot. Navigating
  between 54,129 tracks visits only the chosen source's proposal rows,
  retaining original field order, proposal IDs, provenance, focus filters,
  multivalues and decisions. Explicit Refresh atomically swaps the index
  with the captured diff rows; mismatched row count/source identity fail
  closed. Automated core stress fixtures and source/audit gates qualify the
  index; installed-host latency remains an independent gate.
- 2026-10-24: Online selected-source validation scope (MMS-OPS-03 /
  MMS-WF-08): Search, Load release, next Discogs page, Inspect CUE,
  clipboard candidate comparison and single-source online write entry
  revalidate the chosen source/rules only; an invalid selected index falls
  back to the whole batch. Review Accept/Reject/Reset/manual decisions and
  batch Apply still require full-batch tag and physical guard revalidation.
  Native Win32 scope tests assert one versus 54,129 probes; actual installed
  foobar UI latency and final physical-write consent remain separate gates.
- 2026-10-24: STALE UI freeze invariant (MMS-WF-08 / MMS-UI-05):
  after an invalid source/rules/physical guard, candidate rerenders, tab
  switching and delayed Discogs/MusicBrainz completion must not re-enable
  online search, release loading, clipboard comparison, CUE inspection or
  gated write controls. The stale warning has priority over normal stage
  hints until a separate explicit Refresh succeeds. Native resource tests
  qualify wiring; installed-host interaction remains outstanding.
- 2026-10-24: Exact candidate activation (MMS-UI-02 / MMS-SRC-02):
  a release ListView can have multiple highlighted candidates, but the Load
  command resolves the first selected row. A double-click or Enter now
  targets the explicitly activated selected row, collapses that candidate
  selection only on activation, refreshes the action eligibility, and never
  loads a previous different edition. Blank-space, unselected, pending, stale,
  wrong-source and recording-only contexts refuse to dispatch. Both the pure
  keyboard/mouse resolver and the actual owner-data RC ListView are regression
  tested; real installed-foobar UX acceptance is still pending.
- 2026-10-24: STALE hint retains priority during nonconsequential
  source-track activation/navigation, Beatport-offline provider selection,
  and user date-target changes (MMS-WF-08 / MMS-UI-05). These controls remain
  available for navigation/preferences but cannot erase the stale warning or
  re-enable source-dependent actions. Native HWND test covers the real hint
  control; installed UX acceptance remains outstanding.
- 2026-10-24: Tooltip popup lifetime hardening (MMS-UI-03 /
  MMS-UI-05): owner-deactivation, hide-during-HTTPS-close, cancel/capture
  changes, movement and explicit commands dismiss the separate tooltip
  HWND. Explicit WM_CLOSE dismissal occurs before hiding for a bounded
  network worker, while ordinary typing/activation does not. Pure native
  message-policy tests are automated; a final installed foobar mouse-over
  visual check is still required.
- 2026-10-24: Provider-sensitive Search hover help (MMS-UI-03 /
  MMS-SRC-01): MusicBrainz, Discogs and Beatport-offline each explain the
  currently selected lookup and its consent/authorization boundaries.
  Changing the native provider combo updates the existing tooltip tool ID
  without re-registering, timer state or a background request. Pure policy
  tests protect against misleading Beatport text on other providers;
  installed visual mouse-over remains a final user gate.
- 2026-10-24: Scoped Pending/Review refresh (MMS-OPS-03 / MMS-NRM-08):
  after an explicit Accept/Reject/Reset/manual action, unchanged snapshot
  field counts, source identities and other tracks' review counters are left
  intact. Only affected tracks recount unresolved REVIEW proposals. Exact
  decision-vector cardinality is checked fail-closed. Initial capture and
  Refresh still build complete summaries; every Review action still passes
  the original full-batch stale-input/ruleset verification. Commit
  `832ae231`, all five exact-SHA GitHub CI runs SUCCESS. Real 54k installed
  foobar responsiveness is pending, so MMS-OPS-03 remains PARTIAL.
- 2026-10-24: MusicBrainz read-only DJ suffix discovery (MMS-SRC-01 /
  MMS-SRC-08): explicitly recognized Vocal/Dub/Instrumental/Main Mix,
  Club/Extended Edit, Original Club/Extended Mix and Extended Club Mix
  create a base-title search attempt ahead of the preserved exact full-title
  attempt. No tag values, exact edition identity or trust classification
  change, and generic results remain Version review, not auto-approved.
  Commit `63c6cc88`, Core CI Ubuntu/Windows, foobar Win32/x64 host smoke,
  read-only source audits and isolated synthetic physical/no-op QA PASS on
  the exact SHA. Official catalog hit rate and installed UI comparison remain
  unqualified, hence MMS-SRC-08 is PARTIAL rather than HOST.
- 2026-10-24: Owner-approved source priority (MMS-SRC-04 /
  MMS-SRC-07 / MMS-SRC-08): Beatport and Crates, then Discogs and Deezer,
  are the user's main discovery sources; MusicBrainz is complementary.
  The owner expressly permits LOW-VOLUME private reading of publicly
  visible single-track/release facts from Beatport, crates.co and Deezer.
  This is consent to add opt-in public-page extraction, not a provider
  license, private API access, token reuse, CAPTCHA/rate-limit bypass or
  mass crawl. Crates already has Find-in-Crates and a selected Beatport-link
  browser bridge. Beatport already has a bounded public JSON-LD track
  fetch. Crates' own field extraction and Deezer public-page adapters
  remained NOT IMPLEMENTED at this checkpoint pending source/identity validation. Missing/
  conflicting evidence stays REVIEW/BLOCK and physical/CUE writes disabled.
- 2026-10-24: Cross-provider DJ title discovery (MMS-SRC-01 /
  MMS-SRC-04 / MMS-SRC-08): Discogs and offline Beatport query planners
  now reuse the conservative MusicBrainz generic trailing-mix suffix hint
  for hyphen, en dash and em dash (Extended Mix, Radio Edit, etc.). Discogs
  preserves full-title release/track attempts before adding qualified
  base-title track search; Beatport preserves full-title artist/solo paths
  while adding base-title attempts. Unknown suffixes are not stripped,
  exact mix matching remains independent, no additional network transport
  or write permission is introduced. Automated CI is required before
  upgrading this checkpoint beyond CODE.
- 2026-10-24: Crates public DOM source qualification (MMS-SRC-04 /
  MMS-SRC-07 / MMS-SRC-08): verified an actually accessible logged-out
  Crates track detail page with one exact track row and the same public
  Beatport track ID (`Stella (Extended Mix)`, 17136574). Added an opt-in,
  one-page, *visible-DOM-only* browser clipboard helper for TITLE, ARTIST,
  VERSION, REMIXER, LABEL, ALBUM, GENRE, BPM, KEY and duration; no crawling,
  API, session or stored credentials. C++ import independently validates
  canonical Crates/Beatport public track identities and the complete version,
  then uses the existing exact physical recording reviewer under `crates`
  provenance. Conflicting source IDs, mix labels and duration are blocked;
  the visible `Date` is deliberately excluded pending source meaning.
  Native import remains preview-only, public binaries remain blocked. This
  is a CODE/CI qualification, not installed foobar HOST acceptance.
- 2026-10-24: Crates visible *release* import extended (MMS-SRC-04 /
  MMS-SRC-05 / MMS-SRC-07): verified an unauthenticated release detail page
  with two complete tracks, matching public Beatport release ID, source
  label and catalog number SNA146. Opt-in browser helper copies one visible
  full tracklist with exact row IDs and no page navigation, credentials,
  network calls or date inference. Strict offline source parser validates
  both release URLs, identities, track sequence and credits. Native physical
  reviewer only unlocks album/recording comparison for the exact matched mix;
  mismatch and CUE remain blocked, and no source import enables a write.
  CI and installed foobar visual acceptance remain distinct gates.
- **Remaining decisions that can only be made by the owner:** one final
  installed Windows foobar acceptance; explicit merge/release/production
  write activation when qualified; any new provider legal terms or genuine
  account authorization. No repetitive intermediate confirmation required.

- 2026-10-24: Bounded two-credit MusicBrainz discovery (MMS-SRC-01 /
  MMS-SRC-08): a single, unambiguous collaborative ARTIST such as
  `Kyau & Albert x Maywave` adds one read-only core-title request under
  the second credited artist. The untouched full/lead/title-only and
  optional exact-ISRC attempts remain; at most eight official searches
  are possible, with no change to mix/artist confirmation or write gates.
  Invalid/unbounded original ARTIST never generates truncated lead/partner
  requests. Code + deterministic edge and source-allowlist tests accompany
  the change; exact-SHA CI and installed host acceptance are distinct.

### 2026-10-24 — Providerübergreifende DATE-Abbildung (MMS-SRC-05/06/09, MMS-FILE-06)

- Explizit auf `DATE` abgebildete, qualifizierte ISO-Fakten aus Discogs,
  Beatport/Crates oder einem anderen überprüften Provider ergeben im
  *abgekoppelten Preview* ausschließlich `YYYY` gemäß DJ-Sammlungsstandard.
  `RELEASEDATE`, `DATE_RAW` und `PUBLISH_DATE` behalten auf ausdrückliche
  Auswahl ihre volle ISO-Präzision; Originalwerte und Provider-Evidenz bleiben
  unverändert. Die reine Policy ist providerunabhängig; das native
  Live-Field-Staging ist gegenwärtig auf unabhängig verifizierte Discogs-
  Editions-/Masterfelder begrenzt. Beatport/Crates bleiben ein expliziter
  manueller Vergleich ohne Schreibbefugnis.
- Ungültige Kalenderdaten, mehrwertige DATE-Abbildungen, unqualifizierte
  Provider-Evidenz und stale Snapshots werden blockiert. Dieselbe Jahreszahl
  bei ursprünglich voller Provider-Datumsangabe ist ein echter No-op.
- Regression: `tests/online_fields_tests.cpp` — Schaltjahre, ungültige Monats-/
  Tageswerte, nicht-ISO-/Timestamp-artige Angaben, PUBLISH_DATE, No-op, Erhalt
  von DATE_RAW und Multiwerten. Die native Date-Target-Combobox und der
  Preview-Status benennen die YYYY-Abbildung explizit. Installed foobar-Abnahme
  steht separat aus.

### 2026-10-24 — Modeless workspace: positionsstabile Maximierung (MMS-WF-01, MMS-UI-04/05)

- Windows unterscheidet den `WINDOWPLACEMENT.rcNormalPosition`-
  Arbeitsbereich vom Bildschirmbereich von `GetWindowRect`. Ein maximiertes
  Workspace-Fenster wird mit `SetWindowPlacement`, ein normalisiertes
  Fenster mit `SetWindowPos` wiederhergestellt. Die Bildschirmränder und
  die Workarea/Taskbar werden im jeweiligen Koordinatensystem geklemmt.
- `WM_SIZE` bewahrt den letzten Nicht-Minimierungs-Maximierungszustand;
  `WM_WINDOWPOSCHANGED` hält die letzte echte `GetWindowRect`-
  Bildschirmposition für das Schließen eines normal minimierten Fensters.
  Damit führen beide Minimieren/Schließen-Fälle zu konsistenten Koordinaten.
  Position, Größe, Tab und Benutzeraktionen bleiben unabhängig.
- Regression: `tests/preview_window_geometry_tests.cpp` (Taskbar oben/links,
  negativer Mehrmonitor-Ursprung, entferntes Display, zu kleine Workarea)
  sowie die echte Windows-`HWND`-Strecke `tests/native_preview_controls_tests.cpp`
  (Maximiert-/Normal-Koordinaten mit gemeinsamen Produktionshelfern).
  Live-foobar-Multimonitor-/DPI-Abnahme weiterhin separat ausstehend.

### 2026-10-24 — Crates collaboration search from physical multivalues (MMS-SRC-04/08, MMS-UI-02)

- Explicit user action Find in Crates now includes all ordered, valid
  ARTIST values from one physical multivalue field with the complete TITLE,
  including DJ mix annotation. No invented collaborator separator or provider
  match and never sends paths, other tags or audio.
- Duplicate physical ARTIST fields, invalid/empty credit vectors, more than
  eight values and overlong queries fall back to complete TITLE; ambiguous
  TITLE is blocked. The UI describes this conditional behavior honestly.
- Regression: tests/beatport_manual_intake_tests.cpp, including Kyau & Albert /
  Maywave, URL encoding, tag immutability, and bounded failure cases.
  Manual Crates browser inspection remains a final host gate. No change in
  API authorization or physical file writes.
