# Precision CUE: Deezer reference and Apple timing boundary

Issue #51, in coordination with #46/#50. Read-only; no audio/tag/file writes.

## Provider separation

- Deezer album URLs are **release identity hints**, not audio-download authorization or proof that a Deezer API is accessible to this application. Accept only `https://deezer.com[/xx]/album/<decimal-id>` or `https://www.deezer.com[/xx]/album/<decimal-id>`; reject alternate hosts, HTTP, credentials, ports, query/fragment, redirects, non-album routes, encoded separators, and path traversal. Canonicalize the ID only after strict validation. Never fetch an arbitrary URL supplied by a user.
- The Deezer developer documentation currently redirects to a login. New developer app availability/rights are not established by the public catalog endpoint alone. Therefore a Deezer online transport must remain disabled until official documentation, authorization and terms are verified. Do not scrape pages, use private endpoints or ask for tokens in chat. A locally user-supplied, independently obtained reference tracklist can be evaluated offline without claiming API support.
- Apple's official iTunes Search API provides candidate album IDs and millisecond durations. A candidate is never selected merely by title; require independent release, disc, track order, artist, title/version and duration evidence. Use integer cumulative milliseconds, round each boundary once to 75 CUE frames, reset per disc. No sample-perfect claim.
- A Deezer track `duration` measured in whole seconds is not millisecond precision; never use it to construct final CUE boundaries. At most use it for release-matching evidence with a conservative duration tolerance. Unknown, ambiguous, truncated or conflicting metadata requires REVIEW/REJECT.

## Physical file safety

- External `FILE` references must be MP3 **basenames**, never absolute/relative paths. Reject Win32 reserved device basenames (`CON`, `PRN`, `AUX`, `NUL`, `COM1..9`, `LPT1..9`, including extensions), Windows-forbidden characters, trailing space/dot and control characters. A file reference is not a write authorization.
- Compare complete local physical MP3 duration with candidate disc duration; the current experimental policy is <=1000 ms eligible for read-only preview, 1001..3000 ms REVIEW, >3000 ms REJECT. This is not a sample/mastering identity proof.
- Any external/embedded write remains disabled until #46 physical/virtual isolation, stale snapshot, no-op and installed-foobar integrity gates pass. Preview and lookup must never write.

## Qualification

Use synthetic 2-disc/30-track fixtures and negative URL, malformed metadata, ambiguous mix, wrong edition, missing track, Unicode, Windows reserved filename, +/-1 frame and no-write tests. Compile with C++20 strict warnings, ASan/UBSan where available, then exact-head Windows Win32/x64 GitHub Actions and one final installed-host acceptance. Public repository must contain no private original CUE, personal music or credentials.
