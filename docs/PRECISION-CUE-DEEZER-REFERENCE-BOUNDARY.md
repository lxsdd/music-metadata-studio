# Precision CUE: Deezer reference and Apple timing boundary

Issue #51, coordinated with #46/#50. Discovery and preview are read-only. Only a separately reviewed and explicitly confirmed create-only external `.cue` is supported; no MP3/FLAC/tag/embedded writes.

## Provider separation

- Deezer album URLs are **release identity hints**, not audio-download authorization or proof that a Deezer API is accessible to this application. Accept only `https://deezer.com[/xx]/album/<decimal-id>` or `https://www.deezer.com[/xx]/album/<decimal-id>`; reject alternate hosts, HTTP, credentials, ports, query/fragment, redirects, non-album routes, encoded separators, and path traversal. Canonicalize the ID only after strict validation. Never fetch an arbitrary URL supplied by a user.
- **Current implementation vs authorization:** The native prototype currently performs **user-initiated read-only** requests to the published `api.deezer.com/album/<id>` and `/tracks` routes; unlike the earlier planning text, this transport is **not disabled**. The repository's successful synthetic CI tests do **not** prove network availability or Deezer developer authorization. Merely reaching a public endpoint does not establish any right to deploy or redistribute an integration.
- Official [Deezer developer terms](https://developers.deezer.com/termsofuse) place API access under their terms and access process; app-account registration availability and permission for this particular integration remain unverified. The prototype remains Draft, with no general-release qualification. Before packaging a public release, confirm developer access/permission and permitted request pattern, or disable the online Deezer transport and use a separately qualified offline reference workflow. Do not scrape pages, use private endpoints, bypass access restrictions, download protected audio or ask for tokens in chat.
- An independently obtained local reference tracklist can be validated by the pure offline parser without claiming API access.
- Apple's official iTunes Search API provides candidate album IDs and millisecond durations. A candidate is never selected merely by title; require independent release, disc, track order, artist, title/version and duration evidence. Use integer cumulative milliseconds, round each boundary once to 75 CUE frames, reset per disc. No sample-perfect claim.
- A Deezer track `duration` measured in whole seconds is not millisecond precision; never use it to construct final CUE boundaries. At most use it for release-matching evidence with a conservative duration tolerance. Unknown, ambiguous, truncated or conflicting metadata requires REVIEW/REJECT.

## Physical file safety

- External `FILE` references must be MP3 **basenames**, never absolute/relative paths. Reject Win32 reserved device basenames (`CON`, `PRN`, `AUX`, `NUL`, `COM1..9`, `LPT1..9`, including extensions), Windows-forbidden characters, trailing space/dot and control characters. A file reference is not a write authorization.
- Compare complete local physical MP3 duration with candidate disc duration; the current experimental policy is <=1000 ms eligible for read-only preview, 1001..3000 ms REVIEW, >3000 ms REJECT. This is not a sample/mastering identity proof.
- **External create-only exception:** After native disc/release review, the explicit `Create new external .cue` action requires a separate confirmation and fresh OS physical-source/target checks. It must never overwrite a present target. Search, preview, copy and shutdown do not write media or tags.
- **Embedded and physical media writes are still disabled:** Neither #46 isolation, dry-run patches nor a verified backup permits a host embedded CUE/tag write until independent installed-foobar integrity gates and user approval. No generic virtual-subsong writer is permitted.

## Reviewed external CUE creation and non-atomic multi-disc outcome

- Both **Create selected .cue** and **Create all reviewed .cues** are native,
  user-triggered actions after a fresh visual edition/mapping review and a
  separate modal confirmation. The single and batch actions now share an
  exact high-resolution Apple-edition gate: every selected disc's complete
  CUE bytes, track metadata, integer-millisecond totals and quantized 75fps
  boundaries must be reproducible from the one selected complete edition.
- A multi-disc operation first checks every physical MP3's canonical identity,
  original full-file SHA-256 and an absent sibling target, then checks them
  again after consent. Atomic **no-replace applies per target only**; the
  group of distinct CUE files is **not** one filesystem transaction.
- If another process changes the second MP3 or creates the second target
  after the whole-batch preflight, the independent second-disc writer must
  reject the stale input/collision **without deleting or overwriting**
  the first, successfully published CUE. The reviewed UI reports each full
  target path as **created/verified**, **blocked/not attempted**, or
  **potentially published but not verified**. Published targets are never
  automatically rolled back or removed. The user must re-inspect the whole
  set and explicitly approve a new review; no auto-resume is authorized.
- Unexpected exceptions **before** a per-disc writer distinguish an
  interrupted, unattempted disc from an exception **inside** the writer,
  where a new target might already have been published. Synthetic race
  regressions exercise post-preflight source mutation with restored mtime,
  second-disc target collisions, first-disc races and interruptions before
  the first or second writer against the real create-only backend.

## Qualification

Use synthetic 2-disc/30-track fixtures and negative URL, malformed metadata, ambiguous mix, wrong edition, missing track, Unicode, Windows reserved filename, +/-1 frame and no-write tests. Compile with C++20 strict warnings, ASan/UBSan where available, then exact-head Windows Win32/x64 GitHub Actions and one final installed-host acceptance. Public repository must contain no private original CUE, personal music or credentials.

## Spotify and Amazon optional offline corroboration (owner-approved 2026-10-27)

The pure `precision_cue_optional_provider_review.h` adapter and canonical
`prepare_precision_cue_studio_review` handoff accept **separately user-reviewed
offline** catalog records for the *same independently selected Apple edition*.
This does not grant Spotify/Amazon API access, audio retrieval, producer
authentication, mastering identity, a second foobar workspace or CUE creation.

- Spotify Web API: `duration_ms` is an integer millisecond field. Exact
  matching can supply independent **reported clock** evidence with integer-ms
  start/end comparisons, but not sample-perfect mix boundary proof. All
  supplemental rows remain advisory `REVIEW` and may never substitute the
  primary Apple CUE clock.
- Amazon Music Web API V2: track `duration` is in **seconds**. The adapter
  converts previously reviewed seconds to integer ms; a claimed fractional
  second in that Web track field is rejected, not upgraded to 75fps accuracy.
- Amazon Music Device API: an **approximate** millisecond duration has a
  1ms *field unit* but does not establish 1ms accuracy. The conservative
  comparison gate uses 1000ms uncertainty for review purposes; this is a
  safety policy rather than a scientifically measured Amazon error bound.
- Whole album/edition/track/disc/title/artist/date mismatches, forged
  provider labels, missing offline review, repeated providers, excessive
  candidates and invalid durations are fail-closed.
- No new provider network transport, browser scraping, OAuth, playback,
  media/tag/embedded CUE write or external CUE write permission is added.
  The production writer remains disabled, and the single native Studio
  integration remains independently unqualified.

The full source-only regression covers Spotify 1ms reports, Amazon seconds
and approximate milliseconds, 2-disc/30-track timing reset, forged fraction,
one-frame disagreement, wrong mix, wrong date and duplicate candidates.
CI qualification is exact-SHA source/host only, not installed owner GUI.
Provider field documentation:
https://developer.spotify.com/documentation/web-api/reference/get-track
https://developer.amazon.com/docs/music/API_web_track_v2.html

