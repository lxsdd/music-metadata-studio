# MusicBrainz WS/2 — first official online discovery adapter (read-only)

**Status:** first implementation on Draft PR #45. Not a full tagger, not a validated network runtime until host acceptance. No real media writes. No automatic background lookups or personal library upload.

## Source and right to query

Official **MusicBrainz WS/2** API at `https://musicbrainz.org/ws/2/`, using JSON (`fmt=json`). Official developer references:
- https://musicbrainz.org/doc/MusicBrainz_API
- https://musicbrainz.org/doc/MusicBrainz_API/Search
- https://musicbrainz.org/doc/MusicBrainz_API/Rate_Limiting

The first adapter requires **no account, personal API token, vendor key, paid service or web scraping**. It sends a meaningful application User-Agent with a public source/contact repository. Requests are fixed HTTPS GET to `musicbrainz.org` only: no redirect, no implicit user logon, no cookies added, bounded response size (2 MiB) and WinHTTP connection/timeouts. In-memory process-level rate guard rejects requests closer than 1.15 seconds (MusicBrainz asks for at most one request per second/IP; other apps sharing the same public IP cannot be centrally policed by this component). HTTP 429/503 is surfaced as an error and **not retried in a loop**.

The query is triggered **only** when the user clicks **Find MB** in the native Candidate comparison tab. A second explicit click on **Load MB release** is required to retrieve the full tracklist of the selected release edition; the plugin does not follow every candidate automatically.

## Privacy

Only the locally selected *track title + optional artist*, or an explicitly typed title/album override, are sent as percent-encoded search text. When the title/album term is edited, the selected source's qualified ARTIST / CUE global PERFORMER remains an **initial discovery filter**; later stages include title-only fallback. ISRC / CUE barcode identifiers are sent only for the unchanged auto-suggested source query. A search filter never proves mix or edition identity. In CUE mode only the qualified global album `TITLE` and global `PERFORMER` are used. **No media bytes, CUE document content, filenames, paths, library catalogs, cover artwork, hashes, timestamps, playlists or credentials are sent.** The optional query field permits searching without a stored TITLE. The user remains responsible for deciding whether to send artist/title search text to a public provider.

No persistent search history or HTTP response cache. The UI retains the most recent candidate rows only during the open preview dialog; switching files invalidates that association. No network call occurs merely by opening the dialog, inspecting CUE, opening Preferences, importing a clipboard candidate or switching tabs.

## Functionality and intended UX

1. Select one **physical audio file** in foobar, open **Prepare Tracks → Candidate comparison**, then click **Find MB**. This uses recording search and displays the top eight recording candidates, separated by TITLE / ARTIST. Missing tags require a user-provided title in the small optional title/album input.
2. Select an **external CUE** or **physical audio with an embedded textual CUESHEET** (for embedded first use **Inspect CUE**, so the source type is unambiguous). Click **Find MB**. This searches exact release editions, not release groups/master IDs. For an external Cue with no global `TITLE`, enter an album search term in the optional input.
3. Results display **MusicBrainz API** (verified response source), stable MusicBrainz IDs and an **uncalibrated search ranking**. `Review` or `No change` means comparison information only, never a commit approval.
4. For a CUE search, select a release row (TITLE, ARTIST or DATE) in the right table, then click **Load MB release**. This makes one further official read-only GET and compares album fields; tracks are aligned through the existing guarded one-to-one recording-vs-release matcher. With missing, ambiguous or unqualified local CUE track titles/artists, the release tracks are shown as **Blocked/unmatched** instead of assigned by position. CUE FILE/INDEX and container data are untouched.
5. No automatic metadata acceptance, writing, background matching, artwork download, file operation or CUE rewrite. The previously qualified manual clipboard flow and CUE inspection remain independent.

## Current browser UX and modal-vs-modeless decision

The initial official MusicBrainz search has been accepted in installed foobar: read-only API candidates load from `musicbrainz.org`. A later host screenshot showed **eight main rows per eight releases** and a subsequent selected album tracklist with one album + individual disc/track rows. New defects appeared in the nested comparison window: its caption was clipped against the detail ListView header, and Original/Proposed/Status subitems appeared empty. The search EDIT caption baseline and the `Selected changes` combobox did not align with nearby buttons. These are treated as **open regressions**, not as cosmetic acceptance.

The next patch:
- Aligns actual edit/combo input rectangles with adjacent action buttons, **not merely the dialog-unit top coordinate**; the combo uses `GetComboBoxInfo` and must preserve x-position during screen/client conversions.
- Reserves a separate DPI-scaled caption row before the nested detail ListView, and populates each subitem via the native Windows ListView API, with a real-control round-trip test.
- Offers double-click / Enter to load the **selected, verified MusicBrainz release ID** through the existing explicit action, respecting its HTTP rate guard and source checks. Only the online release-browse state permits that activation.
- Keeps one release/recording per main row, one album/track per loaded-release row, selected-field details underneath and compact source `Track / Hits / Blocked` counters; normal foobar grid preferences remain separate.

**Architecture decision:** The current `Prepare Tracks` dialog is still **modal** (`DialogBoxParamW`) and deliberately has no `WS_MINIMIZEBOX`. A Minimize button on a modal owner-blocking dialog would not permit other foobar operations. The intended finished UX is a single-instance **modeless** foobar-owned browser/workspace that supports minimize/restore, playlist and playback activity in parallel. Converting this dialog requires a separate tested lifetime/focus design: heap-owned snapshot until destroy, reliable foobar dialog keyboard integration, shutdown cleanup, change/staleness checks while another playlist or tag editor modifies source files, no duplicate simultaneous write approvals. Do not claim that this modeless refactor is already complete.

## Exact boundaries and limitations

- Search results are a source of **candidate metadata only**, not proof of the exact audio mix, reissue, extended/radio version, mastering or Discogs-style credit assignments.
- MusicBrainz may lack Beatport-specific BPM/KEY and remix/release details; these are **never invented**, and no automatic online-field-to-physical-writer link exists.
- CUE album and individual track scope remain explicit; incomplete cue track identity blocks alignment. An unavailable field is never inferred from the album title or FILE basename.
- The first WinHTTP call is synchronous on the user click and may temporarily occupy the foobar modal window up to its bounded timeouts. A future cancellable asynchronous request/response layer should be qualified before large batch search.
- Search is restricted to one selected source, up to eight candidates, no automatic background batch fan-out. The in-memory rate limiter currently applies per process, not across unrelated clients sharing an IP.
- User-facing UI is English, and candidate table uses existing sort / column layout settings. The `Find MB` button deliberately uses a compact caption to fit the existing resizable native dialog; later UX consolidation should use a proper metadata source picker rather than competing buttons.
- Responses are strictly bounded and parsed with an offline-tested JSON reader; malicious/invalid JSON, duplicate keys, invalid UTF-8 or Unicode surrogate pairs, unexpected MBIDs, incomplete media, oversized responses and cross-host redirects are rejected. A provider HTTP response does not carry tag-write authority.

## CI and required first real provider host test

Core Windows/Linux must qualify `musicbrainz_provider_tests.cpp` (offline synthetic recording/release lookup, Unicode/escape/security, edition/track matching) and all existing tests; foobar Win32/x64 must compile WinHTTP adapter, button resources and no-writer static guards and produce one exact-SHA `.fb2k-component` package.

**Only after both CI checks pass**, perform first installed foobar test:
- Use a disposable physical MP3 or external CUE. To obtain predictable results, in the optional query input type a known public artist/song or album title before clicking Find MB. A manually typed title first retains the selected source's qualified artist as a **discovery hint**, then offers title-only fallback if the local artist is incorrect or indexed differently. ISRC/barcode identifiers are never reused for an edited query.
- Confirm the title/artist rows with **MusicBrainz API** provenance. For a CUE, select an album candidate row and **Load MB release** after at least 1.2 seconds, checking that track title/artist names and any album title/date are shown without enabling writes.
- Repeat the local SHA-256, creation/mtime no-write test and test the no-network behavior on initial dialog opening. Do not upload personal paths or test music to the repository.
- Include a screenshot and short PASS/FAIL result, plus meaningful anonymized failure messages if any. CI PASS alone does not prove actual TLS/proxy routing or host URL connectivity.

## Future release gate

The official online source is an **optional feature** of a standalone foobar plugin. It must never introduce a dependency on `foo_dj_library_bridge` or local DJ Library personal catalog. Discogs, Beatport, Deezer and Spotify remain separate provider research/gates; this adapter must not claim they are implemented. Merge, production tag writes and physical/CUE file operations remain prohibited until separate explicit host gates.


### 2026-10-25 — CUE release discovery for joined album-artist credits

A user-initiated native MusicBrainz **release** search now uses the same
bounded collaboration parser already used for recording searches. When an
original CUE album artist contains a uniquely recognized `x`, `feat.`,
`vs.`, etc., the native search presents explicit, separately labeled
stages: complete credit first; leading credited artist; other credited
artist; title-only last. A valid CUE CATALOG barcode still takes priority.
This improves recall when MusicBrainz indexes a release under one credited
artist rather than the complete collaboration. For example, `Kyau & Albert
X Maywave` preserves the ampersand within `Kyau & Albert`; the individual
`Maywave` query is a separate, review-only attempt. Invalid, overlong,
control-bearing or ambiguous credits cannot be sent as partial searches.
Each stage is a single user-triggered fixed-host HTTPS request; it does
not silently match, select, import or rewrite any source metadata.
No physical/virtual/CUE/FileOps writer is enabled.


### 2026-10-26 — Retained multi-strategy MusicBrainz discovery

Successive user-clicked, bounded official MusicBrainz search strategies for the
same physical source or CUE retain previous candidate rows in the modeless
Find browser. A MusicBrainz recording/release ID appears **once**, ordered by
first discovery. The original rank and first search strategy remain visible
in each selected candidate's detail pane. If another stage returns
inconsistent nonempty evidence for that exact ID, the candidate and its
detail are explicitly flagged rather than silently replaced. The native
status reports new/repeated/total unique hits; an empty later stage does not
erase previous results and can continue automatically within the existing
bounded plan. Failed HTTPS stages retry without skipping. A new source,
query, provider, exact URL or restarted plan cannot mix stale candidates.
Maximum: eight explicit strategies of 25 results each (200 in memory), no
background catalog scan, automatic matching, caching or tag/CUE/file writes.


### 2026-10-26 — Explicit MusicBrainz result pagination

The existing **Next results page** control now works for **MusicBrainz**
recording searches and exact release discovery as well as Discogs. The
official count and offset in the published MusicBrainz response must
prove another page exists. The user explicitly clicks each subsequent
page; at most four 25-result pages (offsets 0, 25, 50, 75) of the **same
source, original query, provider, kind and strategy** are permitted. The
native fixed-host allowlist rejects arbitrary offsets, extra query
parameters, release lookup pagination and noncanonical URLs.

Successful pages are combined with existing search-stage results using
exact recording/release MBIDs, preserving first-seen evidence and
marking contradictory duplicate identities for manual review. A changed
source, CUE bytes, rules, query, provider, selected strategy or async
request generation invalidates navigation. The full page remains an
untrusted discovery hint; no edition or mix is automatically selected,
no provider result is a tag-write permission, and no CUE, physical file
or File Operations writer is enabled. Offline tests include full
25-record pages, exhausted count, malformed count, mismatched offset,
out-of-range page and URL injection. The existing Win32/x64 host GUI
tests and protected provider-health checks remain separate gates.


## Physical-track MusicBrainz release comparison (2026-10-26)

A user-selected qualified physical audio source now supports a deliberately
entered `album: Album Name` query (official bounded RELEASE search), or a
full exact `https://musicbrainz.org/release/<MBID>` URL. Ordinary physical
TITLE searches remain RECORDING searches. Release candidates must be
explicitly selected and loaded; neither track selection nor a snippet starts
an unsolicited release lookup.

The exact official response is parsed into independent edition and
release-track occurrence evidence. Only a unique, independently corroborated
physical TITLE/ARTIST/mix/duration/optional ISRC association with a direct
release-track MBID can make album and track facts reviewable. Local
barcode/catalogue conflicts, unknown release-track MBIDs, mismatched
provider field provenance and changed local snapshots block promotion.
All physical custom tags, ReplayGain, multivalues and original dates remain
visible and unmodified. Other edition tracks show blocked diagnostics.
A CUE source continues to use its existing dedicated CUE path.

**This path is strictly read-only comparison:** it intentionally does NOT
reuse Discogs physical staging or enable MusicBrainz physical field approval,
tag/CUE/FileOps writers, release-group substitution or background search.
Native Windows/foobar installed GUI acceptance and real public MusicBrainz
host-network behavior remain separate from deterministic CI qualification.

## Explicit physical album batch vs exact MusicBrainz edition (read-only)

When a user loads an exact official release while the existing foobar selection
contains **2–128 qualified physical audio files**, the native grouped Find
comparison now adds **Batch / MATCH** and **Physical #N** groups alongside the
focused-track album and release-track inventory. Each group retains original
field vectors, including custom/multivalue/ReplayGain values, and shows
track-occurrence MBID-backed provider comparisons only after a unique
one-to-one identity match. A complete edition requires every selected physical
source to match a different verified release-track occurrence and the exact
track counts to agree. Mixed album identities, unavailable duration, invalid
ISRC, conflicting barcode/catalog, virtual subsongs and mismatched mixes never
qualify an album. Incomplete selections remain visibly blocked. A selection
above 128 sources, or one containing CUE/nonphysical items, retains the prior
single-source comparison without a bulk media scan. A source or rules change
during network lookup discards the batch audit. The existing search/edition
transport and normalizer remain unchanged. This is **comparison only**: no
batch acceptance, no tag/file/CUE writes, and no persisted approval.

**Missing ALBUM is supported:** An absent physical ALBUM tag does not
contradict a fully verified exact-edition tracklist; this permits read-only
comparison before the user has tagged the album. Supplied nonempty ALBUM
values must agree across the batch; duplicate or blank physical ALBUM tags
fail closed. Tests cover both missing ALBUM and conflicting/duplicate ALBUM.

### Physical album batch: per-source blocking reasons

An exact-edition album comparison now labels the problematic **Physical #N / MATCH** row with an explicit source-specific blocker for ambiguous or conflicting ALBUM, contradictory ALBUM ARTIST, invalid/unqualified physical source, duration, ISRC, missing track identity, or conflicting release identifiers. When the source itself is otherwise qualified but edition-track matching fails, the row distinguishes a title/mix/number/duration mismatch from a whole-batch blocker. These are read-only diagnostics inside the existing native comparison, never implicit tag selection or write authority. Complete original custom, multivalue, ReplayGain and CUE fields remain preserved.
