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

## Third-party provider authorization boundary — 26 October 2026

Official-format public Deezer and Apple catalogue parsing, exact album/edition
comparison and the deterministic offline synthetic 30-track test are software
evidence only. They do **not** establish a right to operate a new third-party
Deezer application or guarantee live API requests will be accepted.
Deezer's developer Terms require an accepted developer relationship and
app approval when applicable (official terms:
https://developers.deezer.com/termsofuse).
Public Deezer staff explanations in 2025–26 describe restrictions on new
individual API registrations. For permission to deploy this integrated
application, request guidance/approval directly via
https://developers.deezer.com/; do not borrow credentials, scrape content
or bypass errors. Catalog HTTP 403/429/outage remains fail-closed and does not
authorize substitute full-track downloads or inaccurate CUE boundaries.

A validated *Apple-only* timing comparison may still be reviewed as a
separate evidence path when the official edition and high-resolution
durations are sufficient. A missing Deezer authorization cannot be silently
converted into a fabricated Deezer consensus or sample-exact promise.
