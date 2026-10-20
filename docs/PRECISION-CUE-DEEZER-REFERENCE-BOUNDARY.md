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

## Qualification

Use synthetic 2-disc/30-track fixtures and negative URL, malformed metadata, ambiguous mix, wrong edition, missing track, Unicode, Windows reserved filename, +/-1 frame and no-write tests. Compile with C++20 strict warnings, ASan/UBSan where available, then exact-head Windows Win32/x64 GitHub Actions and one final installed-host acceptance. Public repository must contain no private original CUE, personal music or credentials.
