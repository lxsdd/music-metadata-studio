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

Only the locally selected *track title + optional artist*, or an explicitly typed title/album override, are sent as percent-encoded search text. In CUE mode only the qualified global album `TITLE` and global `PERFORMER` are used. **No media bytes, CUE document content, filenames, paths, library catalogs, cover artwork, hashes, timestamps, playlists or credentials are sent.** The optional query field permits searching without a stored TITLE. The user remains responsible for deciding whether to send artist/title search text to a public provider.

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
- Use a disposable physical MP3 or external CUE. To obtain predictable results, in the optional query input type a known public artist/song or album title before clicking Find MB. A manually typed title deliberately omits the local artist filter, allowing public searches even when the source tags or album artist are missing or incorrect.
- Confirm the title/artist rows with **MusicBrainz API** provenance. For a CUE, select an album candidate row and **Load MB release** after at least 1.2 seconds, checking that track title/artist names and any album title/date are shown without enabling writes.
- Repeat the local SHA-256, creation/mtime no-write test and test the no-network behavior on initial dialog opening. Do not upload personal paths or test music to the repository.
- Include a screenshot and short PASS/FAIL result, plus meaningful anonymized failure messages if any. CI PASS alone does not prove actual TLS/proxy routing or host URL connectivity.

## Future release gate

The official online source is an **optional feature** of a standalone foobar plugin. It must never introduce a dependency on `foo_dj_library_bridge` or local DJ Library personal catalog. Discogs, Beatport, Deezer and Spotify remain separate provider research/gates; this adapter must not claim they are implemented. Merge, production tag writes and physical/CUE file operations remain prohibited until separate explicit host gates.
