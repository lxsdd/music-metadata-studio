# Native Discogs lookup (Draft PR #45; read-only)

The foobar Prepare Tracks / Find & compare page now offers a source selector
**MusicBrainz** / **Discogs (token)**. Discogs uses the existing fixed-host
WinHTTP GET adapter and the existing bounded official catalog/edition parsers.

- Enter a **personal access token authorized by Discogs** in the masked
  temporary native control. It is neither persisted, logged, committed, added
  to URLs, nor stored in the user's foobar configuration.
- Search one deliberate request at a time. Search attempts broaden only
  after an explicit additional click and a zero-result response. A canonical
  `https://www.discogs.com/release/<id>` URL performs one fixed-host
  release lookup directly. Unknown hosts, masters and unqualified IDs cannot
  become exact release metadata.
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
- For a previously inspected qualified CUE, match via
  `review_cue_release_edition`; only source-verified exact edition track
  rows may be compared with CUE metadata. Ambiguity stays blocked.
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
