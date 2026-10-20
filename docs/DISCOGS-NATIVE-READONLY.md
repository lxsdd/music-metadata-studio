# Native Discogs lookup (Draft PR #45; read-only)

The foobar Prepare Tracks / Find & compare page now offers a source selector
**MusicBrainz** / **Discogs (token)**. Discogs uses the existing fixed-host
WinHTTP GET adapter and the existing bounded official catalog/edition parsers.

- Official Discogs **search and exact-edition HTTPS requests** execute on a
  dedicated, joined native worker. Only the fixed API path and runtime token
  cross that boundary; foobar SDK calls, source/CUE reads, parsing, visual
  updates and pre/post-snapshot verification stay on the main UI thread.
  Closing during an active request hides the workspace and completes
  destruction after its bounded response; a late result after refresh,
  provider switch or source selection change is discarded without changes.
  Native Windows HWND regression tests cover delayed completion, responsive
  event dispatch, errors and safe request lifetime.
- Enter a **personal access token authorized by Discogs** in the masked
  temporary native control. It is neither persisted, logged, committed, added
  to URLs, nor stored in the user's foobar configuration.
- When the selected physical track has a single valid BARCODE and/or
  CATALOGNUMBER, the explicitly initiated Discogs search first tries those
  documented, fixed-host **edition** filters (preserving leading zeros and
  punctuation) before returning to release/track/artist text search.
  Duplicate or multivalue identifiers are never chosen arbitrarily. Manually
  edited queries bypass these automatic local identifier filters.
  The provider search hit is still only a candidate until the exact release
  detail is inspected; a barcode cannot authorize tag writes.
- A qualified single-value physical **ALBUM** (or a parsed CUE-global
  **TITLE**) adds an independent official `release_title` search stage,
  before TITLE-based release/track fallback. The original track TITLE stays
  unchanged, and a missing/ambiguous/malformed album is not guessed.
  Explicitly edited search text does not silently mix in local ALBUM.
  This is a candidate-discovery hint, never proof of the exact edition.
- Search begins only with a deliberate user action. After zero hits its
  bounded official strategies automatically broaden; a nonempty result
  requires another deliberate Search click. The joined network worker waits
  for the 1.6-second request interval without blocking foobar, and closing
  can cancel that wait. A canonical
  `https://www.discogs.com/release/<id>` URL performs one fixed-host
  release lookup directly. A canonical
  `https://www.discogs.com/master/<id>` instead requests the documented
  master versions page 1 on the same official token-header transport.
  Each page displays at most 20 concrete release editions, including available
  format/label/country/catalog/year. **Next master page** explicitly loads
  exactly one subsequent official page; the native page counter shows progress.
  Pages replace rather than silently append candidates, and the bounded
  canonical transport accepts only page numbers 1–100000. Provider-advertised
  totals above that limit remain explicitly incomplete. A master/version list
  is never exact tag evidence: select a release and **Load release** separately.
  Foreign pagination, unknown hosts and unqualified IDs are rejected.
- **Local physical** original-only tags appear in a separate native Physical
  section, not as part of the inspected Discogs edition. A provider-absent
  custom/ReplayGain/multivalue tag is kept intact and explicitly displays
  **Keep original**, without a fabricated remote source/edition ID. The
  same carrier-origin separation applies to MusicBrainz search and manually
  entered Beatport review; edition evidence retains its real source ID.
- Discogs and MusicBrainz search details show all original physical tags
  up to the native 1,024-row capacity, rather than silently dropping them
  after 50 rows. If an unusually large inventory exceeds the native pane,
  a blocked, explicit **Additional physical tags not displayed** row shows
  the remaining count and points to **Review proposals > All current tags**,
  where the complete local inventory can be inspected independently.
  Provider facts remain separate from the local original rows.
- Native result and field detail provenance distinguish official Discogs API,
  official MusicBrainz API and user-pasted evidence; hovering a field shows
  exact provider source ID, scope, date meaning and refusal reason. Up to
  1,024 detail rows are rendered; excessive evidence is explicitly marked,
  never silently truncated or treated as write approval.
- Discogs exact-edition tracklists expand qualified `index` `sub_tracks`
  into independently sourced, display-only recording rows. Headings/videos
  remain non-audio. Missing index children, malformed nested entries and
  unknown track types reject the entire edition rather than displaying an
  incomplete album as verified. No CUE timing or write approval is inferred.
- Every Discogs edition track now has a protected, display-only MATCH
  diagnostic. Suggested identity displays independent evidence such as
  title, artist, duration and position without implying certainty or write
  approval. Missing physical duration, duplicate local identity, wrong remix,
  ambiguous assignment and tainted provider provenance explain why the
  individual track is blocked. MATCH_DIAGNOSTIC can never be a physical tag.
- Inspect a verified release: full multivalue edition fields are shown
  separately from individual recording/track fields, each preserving source
  IDs. A qualified physical file may compare track-specific fields only when
  its TITLE, ARTIST, actual foobar duration and optional mix/position/ISRC
  yield a unique Suggested one-to-one alignment without conflicts.
  Missing duration, duplicate local identities, ambiguous editions, wrong
  remix and unverified track provenance remain blocked. The current
  Discogs tracklist adapter does not provide verified track ISRC: any
  local ISRC remains visible but is not used as remote identity evidence.
  No matched field is approved for writing.
- For an inspected parsed CUE missing a unique TITLE/PERFORMER on any track,
  the native exact-edition view now retains **BLOCKED album-only** comparison
  rows with original CUE-global values and verified edition provenance, plus
  explicit CUE-track MATCH diagnostics. No remote credit is assigned to a
  track and no online field becomes an editable proposal. Unparsed CUE syntax
  or invalid edition source identities still rejects the entire comparison.
- For an inspected qualified CUE, `review_cue_release_edition` separates
  verified track candidates from weaker evidence. Missing independent
  duration/ISRC no longer hides all useful exact-edition fields: when an
  otherwise unique title/artist track association is under-corroborated, its
  provider fields are shown next to original CUE values exclusively as
  **BLOCKED, non-actionable evidence**. Wrong remix, ambiguous alignment,
  forged source IDs and unverified track provenance cannot borrow credits.
- Both **Inspect CUE** and the exact Discogs edition detail use the same
  read-only source inventory. The comparison shows every parsed original
  CUE-global and CUE-track field (including repeated values and unknown REM)
  plus FILE references in separate **CUE Original** groups with actual source
  lines and external/embedded carrier identity. Provider-absent values display
  **Keep original CUE** and **(not supplied)**; no remote identity is invented.
  Unknown REM remains display-only, not an editable/inherited canonical tag.
  CUE original inventory is bounded to 10,000 rows; the 1,024-row native
  detail view explicitly marks overflow instead of silent truncation.
- The existing source/rules snapshot and CUE post-read guards run before
  publishing any result. The online page remains a **read-only comparison**:
  no accepting online metadata as a write operation, audio modification,
  CUE modification, ReplayGain change or file organization is enabled.
- Network access requires an authorized token and the provider's applicable
  terms. No fallback to scraping, paid subscriptions or undocumented endpoints.

Qualification still required: exact-HEAD Core and Win32/x64 CI, real
foobar2000 Win32 and x64 native layout/token/source/track interaction at
100%–200% scaling, plus end-to-end authorized live lookup. CI success
alone is **not** installed-host acceptance.
