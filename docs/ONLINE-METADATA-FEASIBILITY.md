# Online Metadata Discovery — concept and feasibility study

Status: **DESIGN / READ-ONLY FOUNDATION** (2026-10-09). Not a shipping tagger, not a license approval, and not a host-runtime qualification.

## 0. Product decision and non-negotiable invariants

Add online candidate discovery and comparison to **Music Metadata Studio**, not a standalone tagging program and not a fork of Discogger. Preserve the existing unified **Prepare Tracks** workflow, shared deterministic rules engine, native foobar2000 Settings/context menus, read-only Bridge, and DJ Library as a future optional review surface.

A successful user journey: select track(s) in foobar -> open one Prepare Tracks window -> optional **Find metadata** action -> inspect ranked recording/release candidates from chosen permitted sources -> compare each proposed field with local originals -> resolve uncertainties -> preview normalization and consequent naming/routing in the same window -> explicitly approve -> later execute through qualified host adapters with complete no-op and stale checks.

**No live writes in this phase**. Never automatically search on component load; no background tag/cover/file writes; do not expose personal music paths, library databases, credentials or media to a provider. Do not merge implementation branches or enable mutations before existing host acceptance gate #33.

User priority: functional completeness, natural foobar integration, intuitive operation, accessible large-batch performance, configurable but excellent defaults.

## 1. Confirmed repo / ecosystem baseline

* Canonical project **Music Metadata Studio**: GitHub slug is currently `lxsdd/dj-metadata-normalizer` until the owner renames the same repository to `lxsdd/music-metadata-studio`. On 2026-10-09 main is commit `b0f6501cc4c4c1a61768c21f6d55dda5336ae041` (2026-10-08 UTC).
* Existing C++20 core provides immutable metadata vectors, SHA-256 metadata fingerprints, stable per-rule provenance, SAFE / CONFIDENT / REVIEW, native foobar preview with a one-row-per-track master/detail view and a unified naming/routing preview; currently **no productive writers**.
* Metadata isolation defect learned in foobar v2.25.10/2.26 previews: writing a virtual subsong's projected `file_info` can drop unrepresented *physical* ID3 fields. Therefore only a qualified physical metadata writer can commit physical changes, cue edits have dedicated routing, and ambiguous virtual edits are hard-blocked.
* Existing component uses the official 2026-10-01 foobar2000 SDK pin; user-specific naming preferences and profile-specific settings belong to foobar, while the shared versioned rule file remains at `%APPDATA%\DJMetadataNormalizer\ruleset.json`.
* `foo_dj_library_bridge` remains read-only and profile-local. Reuse its metadata vector snapshot for DJ Library; do not introduce a duplicate music database, export channel, cloud service or dependency on `rekordbox-mytag-sync`.
* Official source facts to revalidate before implementing each adapter:
  * Discogs API TOU: https://support.discogs.com/hc/en-us/articles/360009334593-API-Terms-of-Use ; its source model distinguishes master / physical or digital release / position / credits. Discogger confirms release-accurate matching is intrinsically laborious: https://github.com/ghDaYuYu/foo_discogger
  * Beatport Developer portal: https://api.beatport.com/v4/docs/ ; Beatport terms expressly require an issued API key for approved licensees: https://support.beatport.com/hc/en-us/articles/4414997837716-Terms-and-Conditions . Access **NOT VERIFIED** for this project. Do not scrape or reuse third-party/client credentials.
  * Spotify Feb 2026 Dev Mode: https://developer.spotify.com/blog/2026-02-06-update-on-developer-access-and-platform-security ; migration: https://developer.spotify.com/documentation/web-api/tutorials/february-2026-migration-guide ; terms: https://developer.spotify.com/terms . Premium, restricted users/endpoints, storage policies and field omissions mean **optional research-only** until use-case and tag-writing rights are confirmed.
  * MusicBrainz API: https://musicbrainz.org/doc/MusicBrainz_API with user-agent and conservative 1 request/sec per client/IP. Recommended genuinely open fallback, respecting provider terms.
  * Deezer public catalog access, app registration and permitted persistence: **BLOCKED / NOT VERIFIED**. Deezer Community as of 2026-05-21 states that new developer app registration remains closed with no reopening ETA: https://en.deezercommunity.com/features-feedback-44/app-registration-82666 ; 2025-2026 API authorizations have also been disabled in some cases: https://en.deezercommunity.com/your-account-favorites-and-playlists-70/oauth-exception-81676 . Do not depend on legacy third-party credentials or unauthenticated endpoints without verified permission.
* Existing independent implementations (Discogger; OneTagger, GPL-3.0) are reference designs, **not** a legal basis to paste implementation code into a differently licensed repository. Document license compatibility before reuse.

## 2. Source availability and deployment matrix

| Source | Role | Network connector readiness | Product default |
| --- | --- | --- | --- |
| Discogs | exact release / master / track listing, credits, catalog number, label, year, media, cover references | official documented API; user authorization and quotas, field rights must be validated | candidate for first implemented provider |
| MusicBrainz | recording-vs-release distinction, ISRC, label, recording credits, release group | documented public API with mandatory rate-limiting | candidate for first implemented provider |
| Beatport | electronic music mix version, DJ genre, musical key, BPM, label, extended duration | actual app/license credentials unverified; strict gate | disabled until authorized |
| Deezer | supplementary digital release/track candidate, possibly ISRC | new app registration reportedly closed; access and long-term usage rights unverified | disabled until authorized |
| Spotify | corroboration (ISRC, recording, duration, album), not BPM source | restricted Dev Mode / distribution terms | opt-in only, no persistence/write use until authorized |

One failed, rate-limited or unauthorized provider is a **provider-local** status, not global failure. UI displays `Available`, `Needs authorization`, `Rate limited`, `Unavailable`, `Policy disabled`, `Offline` clearly, never fake successful results.

Potential future providers: MusicBrainz / AcoustID (independent user consent for acoustic fingerprinting), Bandcamp/Juno/Traxsource subject to the same legal gate; no feature assumed available solely because a website exists.

## 3. Data ontology — the hard architectural problem

Entities are **Recording**, **Track/Version**, **Release**, **Release Edition**, **Artist**, **Label**, **SourceEvidence**. Distinguish: a canonical recording vs an edit/mix/remaster (materially different audio), a particular release and an edition with different year/country/media/catalog number, and original recording/release year vs reissue year vs digital publication date.

Normalized read-only candidate snapshot (`CandidateSnapshotV1`) contains:
* provider ID, immutable provider object ID, optional URL, entity kind and parent release/edition ID;
* complete raw provider payload evidence where permitted and normalized UTF-8 display vectors, **never** flatten multiple artists/genres/credits;
* search hints: recording title, artists, explicit mix/version/remix roles, runtime ms, ISRC, UPC/barcode, catalog no., track position/total/disc, label, original/release dates **with granularity and date meaning**, genre/style and provider BPM/key;
* field-level provenance, acquisition time, license/storage policy and availability.
* zero assumptions about unknown fields or a missing `ISRC`, `KEY`, `BPM` or `LABEL`.

Only provider adapters parse remote payloads; the shared matcher consumes an immutable, portable candidate model. Provider-specific tags must not leak directly into foobar file_info.

ISRC is powerful evidence but not proof of the same remix/master; reuse/erroneous registration exists. Matching ISRC cannot override strongly conflicting mix version/duration. BPM/key are supporting evidence only (half/double-time BPM; different key notation or analysis engines); never overwrite values computed by foo_smart_tempo or tagged by user without explicit field-policy approval.

## 4. Search and matching pipeline

1. From exact physical selection and metadata snapshot, make normalized *search tokens* without mutating original metadata. Read title, artist aliases, explicit remix/edit/extended/live/version qualifiers, ISRC, release metadata and duration. Preserve original strings for display.
2. Query generation is staged: provider IDs/URLs saved in tags -> exact ISRC where supported -> normalized artist/title/version -> release/album/label/catalog/UPC -> controlled broadening. Debounce edited query, cancel obsolete results.
3. Fetch bounded candidate lists by provider. Deduplicate by provider ID and separately correlate evidence for putatively identical recordings; retain every original result and its release editions.
4. Score with **explainable per-signal evidence** and conservative hard conflicts: title, artist, mix/version, duration, ISRC, release context, position, label. Distinguish unknown from disagreement. Provider agreement can increase corroboration but not by counting duplicate releases or a dozen copies of one faulty database record as independent votes.
5. For album/EP batches use **global one-to-one assignment** (track vs tracklist) and evaluate missing/bonus tracks, media side positions, volume and release count. Partial-album selections valid; do not force a full release match.
6. Apply calibratable thresholds to `suggested` / `review` / `rejected` *for search only*. Even strong candidates are **never approved tag writes**. Return evidence and specific contradiction reasons.
7. Candidate selected -> one **field-diff** proposal per affected local track, tagged `REVIEW` unless explicitly qualified user profile policy. No implicit deletions. Field sources may differ (e.g. release credit from Discogs, extended-mix BPM from Beatport) **only after** the same recording/edition and rights are reconciled.
8. The normalizer runs on a staged metadata document with accepted changes, then native foobar title formatting calculates filenames and locations. Approvals refer to the exact candidate snapshots, field choices, ruleset bytes and host source identity, never a global approval bit.

Do NOT automatically trust first search result, provider popularity, generic genre hierarchies, earliest release year as if it always described the file, or an ISRC that conflicts with the displayed version.

## 5. Native UX: one intuitive primary window

Reuse the existing one-window **Prepare Tracks (Preview)** master/detail design, and add a **Find metadata** workflow/section within the existing metadata tab rather than a second standalone component/window.

Default first view:
* left/top **selected physical tracks** (full-row selection, count, sortable configurable columns, persistent layouts) with visual statuses: Unsearched / Searching / Candidate / Conflicting / No result / Offline;
* center/right **top candidate cards/table**, source name, artist, title, exact mix/version, duration, album/release/edition, year, match rationale; uncertainty highlighted plainly;
* bottom/detail **field differences** showing `Existing | Proposal | Source | Reason | Decision`, with clear actions **Keep existing**, **Use suggestion**, **Choose other candidate**, **Skip**. No hidden tag deletion.
* separate **Release editions** expansion for Discogs with filters (vinyl/CD/digital/country/year/catalog), always preserving the exact master vs edition identity. A simple user need not navigate manually through artist-tree/master/release dialogs.
* one persistent customisable search box prefilled from tags; editable for bad/no matches; typeahead, direct Discogs URL or ID input; keyboard shortcuts; collapse complex metadata into Extended / All without discarding any fields.
* show default-only changed fields; Advanced reveals field provenance and raw source data. Tooltips cannot expose confusing internal implementation names; respect English-only built-in UI, native Dark Mode, DPI, locale-neutral source text.
* meaningful empty/error/loading/cancel states; preserve track selected and scroll when sorting/filtering; no invisible network loops.
* batching: **Search selected**, **Search missing**, **Recheck selected** and **Cancel**. One full batch preview, explicit Apply later, a post-apply per-item report. Never open hundreds of modals.
* provider preference screen in native foobar Preferences: enabled sources, credentials status, maximum results, quotas/cache TTL within source rules, matching profile, preferred fields per provider and no-overwrite policy; search source filters are quick toggles.
* one consolidated global overview when multiple tracks have unresolved candidates. Support auto-suggestions only after a measured false-positive calibration; user can multi-select a batch for identical decision patterns but no implicit per-track rewrite.

Menus use stable GUIDs and foobar's built-in context-menu visibility/hotkey controls; foobar `cfg_*` (or current SDK equivalent) with Apply/Cancel/Reset, native theme/DPI, no independent registry/profile DB. User layouts/decisions persist where allowed; encrypted tokens via appropriate OS credential storage, not source code, public CI artifacts, or plaintext ruleset.

## 6. Field mapping and governance

Field names are user-configurable and aligned with foobar's editable tag syntax; defaults map explicit semantics:
* `ARTIST`, `ALBUM ARTIST`, `TITLE`, `ALBUM`, `TRACKNUMBER`, `DISCNUMBER`;
* `DATE`: default four-digit year, `DATE_RAW`: true available date; keep provenance on *which* publication date is suggested;
* `GENRE`, `STYLE`/user-defined extended fields, `LABEL`, `CATALOGNUMBER`, `ISRC`, `BARCODE`, `REMIXER`, `VERSION`, `COMMENT`, `DISCOGS_RELEASE_ID`, `DISCOGS_MASTER_ID`, provider IDs if explicitly enabled;
* BPM/key suggestions are per-provider evidence with per-field manual consent; preserve existing BPM/key when not approved;
* preserve `CUESHEET`, `CUE_SHEET`, `__CUESHEET`, lyrics, ReplayGain, user/private/custom tags, unknown fields, multivalue arrays, existing Discogger tags by default. Never map labels/country/edition year to original recording date silently.
* per-field policies: only if empty / propose differences / explicitly overwrite approved / never suggest / source priority; all policies versioned with explainable preview.
* do not confuse suggestion confidence with the existing SAFE/CONFIDENT/REVIEW normalization rules. External semantic changes default to REVIEW until field-specific exception is accepted.

Images: release vs artist cover, provenance and copyright controls, external/embedded options only after source usage confirmation and own qualified host artwork adapter. Cover availability does not imply permission for download or embedding.

## 7. Write, file and cue safety (hard stop gates)

* Candidate gathering, cache and comparison are **read-only**, including requests initiated from DJ Library.
* Approval requires explicit per-field/per-group decision and a complete plan fingerprint over physical source identity, exact metadata vectors, chosen provider record IDs/versions, field mapping, ruleset snapshot, naming/routing expressions, associated sidecars, overwrite decisions, resolved destination.
* Before actual action, re-read physical metadata & plan and revalidate current target; stale source, provider-record-dependent decision, ambiguous or unsafe subsong -> STOP with new preview. Remote provider data is a snapshot; do not silently refetch/rebase approved fields after approval.
* No-op: if approved physical postimage is byte/semantic-equivalent under field-specific safe comparison, **zero writer calls**. If no resulting filename/path/sidecar change, no filesystem copy/move/timestamp touch. Favor idempotent repeated executions and deterministic plan creation.
* Physical vs virtual: (path, subsong) is selection identity, not write target identity. Group subsongs per physical file; protect embedded cues and global/inherited fields; cue changes only via qualified dedicated cue editor, and FLAC Type-5 cue read/preserve-only where applicable.
* Sidecars/external cues: detect references, ensure plan includes needed repair/coordination or block; zero deletion on partial failure; preflight all targets, paths, duplicates and existing destinations; overwrite approval only for enumerated conflicts, not generic overwrite=yes.
* Renames/moves/copies and file dates honor foobar File Operations policies only when public SDK access or faithfully qualified equivalence is proven. Never imply host-compatible behavior based on simulated string output.
* No credentials or remote requests from write executor. No network or filesystem writes in normalizer core.

## 8. Networking, privacy, robustness and performance

* Network exclusively in an asynchronous, cancelable **foobar provider-adapter layer** outside the deterministic core, using a dedicated request queue with provider-specific rate limit, 429/Retry-After, exponential backoff with jitter and user-visible partial success, no unbounded automatic retries.
* Strict HTTPS, redirect/URL allowlists, response size/time bounds, JSON depth/string limits, UTF-8 validation, safe escaping, no script/webview injection. OAuth PKCE or provider-mandated authorized flow if supported; no embedded reusable provider application secrets.
* Cache is optional and **per foobar profile**, size-bounded, expiration-aware and restricted by each provider's license; encrypted user token store separate; never shared with public GitHub or logs. Personal raw search strings should not enter telemetry.
* Searches only send artist/title/ISRC/categorical queries where genuinely needed; never paths, user IDs, local timestamps or audio bytes. Fingerprinting (AcoustID) requires a separate explicit opt-in and provider policy.
* Prioritize interactive selection, show first results progressively, cancel on changed selection, virtualize display rows, bounded concurrent requests, per-provider queues, shared deduplicated request cache.
* 15,000-item library remains renderable; bulk online lookup limited to deliberate user-selected batches and provider quota; no automatic crawl of all 54k foobar entries, no reliance on rate-limit evasion.
* Offline mode supports local preview/normalizer/routing and authorized temporary cached candidates; clearly label age and no fresh verification. All functions except remote search survive service outage.

## 9. Engineering boundaries and interfaces

```
foobar Context Menu / Preferences
  -> Prepare Tracks native UI (track master/detail; search/decisions)
      -> foobar read-only source snapshot and physical identity
      -> provider gateway (network, credentials, rate limits, cache)
          -> Discogs / MusicBrainz / optional authorized providers
      -> CandidateSnapshotV1 (immutable, evidence; no file_info writes)
      -> pure C++20 matching/ranking/release assignment
      -> explicit field-decision plan (REVIEW)
      -> existing deterministic Normalizer Engine / shared C ABI
      -> existing native foobar Title Formatting and file-route planner
      -> whole-batch preflight and no-op guard
      -> FUTURE qualified host SDK writer (disabled until gates)
```

Add matcher code under `include/djmeta/` and `src/core/` with provider-free synthetic fixtures and CMake tests. Do not contaminate `rules/default-rules.json` or change DJ Library ABI before a versioned contract is reviewed. New network adapters outside core, no unaudited dependencies. Reuse foobar SDK 2026-10-01, C++20, MSVC Win32/x64, GitHub Actions.

## 10. Technical acceptance / measurable quality targets (targets, not achieved)

* Unit: normalization vs matching distinction, immutable originals, stable index/track mapping; add at least 100 synthetic scenarios covering artist punctuation, Unicode, featurings, mix edits, live/remaster, same ISRC different duration, absent ISRC, multiple editions, compilations, 2+CD, LP sides, time discrepancies, field-level conflicts.
* Gold-standard calibration before enabling auto-suggest: >=1000 manually labelled private/local evaluation cases across genres and eras, **without uploading user collection**; publication uses only synthetic/non-identifying aggregate results. False positive goal under 0.1% for strong candidate suggestions; otherwise no high-confidence automation. Precision/recall and abstention always published as actual measured results, never promises.
* Deterministic rerun: identical input + provider snapshot + profile -> exact candidate ordering/explanations and candidate IDs. Source failure / reorder / duplicates -> stable track identity.
* Performance targets, measured in Windows foobar: render 20k rows without UI blocking; rank 1000 candidates within 250ms typical local CI machine, excluding network; bounded memory/requests; rate quota obeyed per provider; online batch 100+ selected tracks remains cancelable and progressive.
* Contract test: provider JSON/HTTP fixture for error 401/403/404/429/500, schema changes, pagination, missing fields, repeated fields, text escaping, oversized payload, revoked login and offline.
* Host tests: foobar 2.x x64 and Win32, native dark/light, 100–200% DPI, keyboard/screen-reader navigation, profile roundtrip, full-row selection, thumbnails/column resize, no writer calls in read-only path, sample MP3/FLAC/WavPack/embedded/external CUE integrity hashes.
* No-op test: unchanged audio, timestamps and tag bytes before/after a deliberately reviewed no-op. Real writer acceptance **separate** with disposable samples and only after issue #33.
* Secrets/license/security audit required before any network source enabled by default and before public release.

## 11. Phased execution and critical dependencies

**P0 (now):** freeze concept, inspect current repo, create candidate-model / deterministic matcher spike (pure, read-only, no external API calls), tests, design draft PR; no user action.

**P1 (next autonomous work):** mature ontology, batch release assignment, field provenance, C ABI extension behind version guard, native search UI prototype; run Win32/x64 CI and tests, preserve existing UI and routing.

**P2:** authorized first production-safe source connector (Discogs OR MusicBrainz) with mocked/fixture tests, user credential settings, cache/rate-limit/privacy controls, candidate table. Prefer MusicBrainz if Discogs access is not ready; never use undocumented endpoints.

**P3:** Discogs full release/edition/tracklist logic, smart compact presentation, precise track-to-release mapping, links to existing Discogger IDs. Add album/compilation/partial-EP tests, real-host read-only acceptance.

**P4:** Beatport/Deezer/Spotify connectors **only** if each access and rights gate passes; else clean disabled slots and alternative official sources. No legal shortcuts; need no user confirmation for read-only code, but do not infer user API consent or buy access.

**P5:** field-level review and mapping preferences, approved normalization -> naming -> file-routing transaction plan, collision/cue checks, UI and 15k+ performance acceptance.

**P6:** enable guarded mutation adapter in a **separate explicit approval** and after real installed-foobar host tests; never silently upgrade preview to write.

### Gate checklist

| Gate | Status at study time |
| --- | --- |
| Independent C++20 matching engine specification | DESIGN |
| Verified official source metadata semantics | PARTIAL |
| Official Discogs authorization available for this project | NOT VERIFIED |
| Beatport licensed API key for this project | NOT VERIFIED |
| Deezer API permission and lifecycle | NOT VERIFIED |
| Spotify applicable API rights for local tagging | NOT VERIFIED |
| Win32/x64 build of online extension | NOT RUN |
| Network integration real source test | NOT RUN |
| foobar installed-host read-only acceptance | REQUIRED / NOT CLAIMED |
| Productive metadata or FileOps write permission | DISABLED |

## 12. Why not copy Discogger or OneTagger wholesale?

Discogger is optimized for exact Discogs editions and fully customizable expression mappings; its own documentation says manual matching is appropriate. We reuse its *conceptual lessons* (edition tree, stable release IDs, track-match preview and customized field map), not its UI flow or implementation. OneTagger demonstrates multi-provider DJ enrichment but its licensing, data-source mechanics and writing stack cannot simply be assumed compatible; copying it would abandon our host-integrated, single-core, explicit-approval/no-op design.

## 13. Major risks and mitigations

1. **Rights/access (high)**: fail-closed provider flag and no undocumented credentials/scraping; official terms/approval evidence per release.
2. **Wrong mix/reissue tagged (very high)**: explicit version/date/edition semantics, conflicts, reason codes, abstention, release-group global alignment.
3. **Physical data loss (critical)**: zero direct virtual file_info writes; metadata isolation physical guard, dedicated cue routes, no-op guard, host sample hash checks.
4. **UI complexity (high)**: one sensible default workflow; progressive reveal advanced details; keyboard/multitrack design with saved column layouts.
5. **Slow/brittle online searches (high)**: async workers, quotas, cancellation, cache, offline fallback, bounded batches.
6. **Settings drift (medium)**: foobar-native Preferences conventions, stable GUIDs, shared rules only for core, no shadow config.
7. **Compatibility with other products (high)**: retain C ABI v1 and Bridge read-only; introduce versioned extension rather than changing old signatures.

## 14. Open research questions (non-blocking for P0/P1)

Document provider-specific permitted metadata caching/writes, exact access mode, artwork licensing, OAuth credentials, genre semantics; decide first legal connector only after that verification. Investigate foobar SDK host API coverage for File Operations policy and metadata/update notifications before designing any productive executor. Confirm native UI workflow with a 1-file and 100-file *read-only* host fixture before making auto-approval behavior.

## 15. Product success definition

For an ordinary DJ single, the correct *mix* should appear among clearly labelled candidates with few clicks, and competing editions should be easy to distinguish. For an album, a single correct release selection should align its full tracklist with only unusual tracks needing review. For an ambiguous case, the product should clearly say **Cannot decide** rather than invent certainty. No remote service outage may compromise local metadata or browsing, and re-running an approved unchanged plan must touch no file at all.

**Feasibility verdict:** Core matching, host-native UI and provider abstraction are technically feasible; comprehensive rights-cleared Beatport/Spotify/Deezer connectivity is **conditional**, not presently proven. Current state is not a functioning online tagger.

## 16. Private zero-cost mode: access without an official developer key

**Requirement added 2026-10-09:** The primary user is a single non-commercial hobbyist. The whole local foobar experience should be usable at **EUR 0 incremental subscription/API/server cost**, without sacrificing data integrity, configurable field mappings or user experience. A publicly buildable core plus a separate optional local configuration / lawful parser is preferable to requiring any paid API or cloud service. **Private use does not waive API terms, website access contracts, copyright, German/EU database rights, rate limits, or anti-circumvention rules.**

Separation:
1. **No-key official/public source first:** MusicBrainz API offers free non-commercial, unauthenticated GET with identifiable user agent and <=1 request/s; authorized Discogs CC0 catalogue data via the documented API, including allowed unauthenticated GET endpoints, with rate limits and attribution. See https://musicbrainz.org/doc/MusicBrainz_API and https://support.discogs.com/hc/de/articles/360009334593-API-Nutzungsbedingungen .
2. **Published public Deezer catalog endpoints:** Deezer support documents `https://api.deezer.com/search?q=...`; third-party documentation indicates track/album catalog GET may work without OAuth, but actual access could not be verified from the research environment. New app registration was reported closed May 2026. Treat authentication-less operation, endpoint terms and persistent tagging rights as **UNVERIFIED** until an actual supported GET and terms assessment, not inherently forbidden or guaranteed. See https://support.deezer.com/hc/en-gb/articles/360011538897-Deezer-FAQs-For-Developers .
3. **Public Beatport track and release pages:** User-visible information includes title, mix version, artist, BPM, key, label, duration, genre, publication date. A single manually supplied URL can be opened in the user's browser with source link. A read-only HTML extraction adapter must **not** automatically be enabled without checking current Beatport website terms and applicable law; automatic access to protected/undocumented v4 endpoints using embedded secrets from websites or third-party source repositories is not an approved fallback. Beatport's official API explicitly requires pre-approved keys: https://support.beatport.com/hc/en-us/articles/4414997837716-Terms-and-Conditions . This is not proof that all kinds of publicly accessible information are forbidden to read; rights and permitted extraction require distinct review.
4. **Spotify:** Explicit anti-scraping clauses in Spotify's current user guidelines make unofficial crawling an unsuitable fallback even for private use. Provide deep-link/manual lookup only, with no crawler. See https://www.spotify.com/de/legal/user-guidelines/ .
5. **Manual/local fallback for every provider:** User can paste a recognized release/track URL or explicitly copy visible *factual metadata* into a structured local intake (e.g. human-created TSV/JSON or native clipboard fields), preview and match those candidate facts against original tags, then select fields to use. Do not automatically scrape forbidden pages behind that control. Store source URL and provenance if authorized. Avoid bulk import of protected databases, duplicate artwork or copyrighted narrative/biography. A user can therefore complete the exact same foobar-native per-field review, normalization, routing and explicit commit flow even where the automatic provider adapter is disabled.
6. **Pluggable authorized readers:** A single `ProviderCapabilities` / `CandidateSnapshotV1` contract handles keyless API, licensed API, permitted public-page adapter, offline/user-imported candidate. Keep transport separate from matcher/rules/UI. A temporarily unavailable provider must never block other candidates or local edits.
7. **Financial/operational guardrails:** No purchased API, third-party proxy, hosted search database, subscription, cloud worker or fallback billed requests. All network requests originate from user's machine, are deliberately user-initiated, bounded, cancelable, rate-limited and cache-controlled under source terms. Do not commit private secrets or music paths. Never evade CAPTCHA, login/paywalls, IP blocks, throttles, or reuse someone else's API client secret.

**Evidence that unofficial technical integrations exist, not proof they are permitted:** OneTagger's public source has Beatport and Deezer plugins and a May-June 2026 draft fix for Beatport search breaking after the removal of Next.js `__NEXT_DATA__`. The proposed fix switched from HTML extraction to an authenticated v4 catalog interface. This illustrates both possible private technical capability and the fragility/legal credential risks. See https://github.com/Marekkon5/onetagger/pull/526 . Do not transplant its GPL implementation or embedded secrets.

**German legal risk:** Database-maker rights (UrhG §§87b–87e) are not simply waived for a private project; even repeated extraction of many individually small database parts may be restricted. §87c's private-copy exception excludes certain electronically individually accessible databases. This is a product risk note, **not legal advice**. See https://www.gesetze-im-internet.de/urhg/__87b.html and https://www.gesetze-im-internet.de/urhg/__87c.html .

**Conclusion:** A capable zero-cost private tagger with Discogs/MusicBrainz and manual/public-data fallbacks is technically feasible. Automatic full Beatport/Spotify scraping without app approval cannot presently be claimed lawful or reliably supported. The MVP must not be blocked waiting for their APIs. All online retrieval remains disabled pending per-provider qualification, while the candidate core/UI and local import contract can proceed independently.
