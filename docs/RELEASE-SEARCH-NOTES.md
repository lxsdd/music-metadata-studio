# MusicBrainz CUE release discovery

The existing foobar search UI currently makes one strict MusicBrainz release
request with the qualified CUE album TITLE and PERFORMER. Compilation releases
may not be indexed under the CUE PERFORMER, so a zero-result response must
not be treated as proof that no release exists.

## Bounded read-only fallback contract

The independent provider function in
`include/djmeta/release_search_plan.h` generates up to two official WS/2
release search paths, in this order:

1. Album title + album artist, when both are available.
2. Album title alone, explicitly labeled as requiring edition review.

Each step requires a separate deliberate Search activation. No automatic
network loop, credential, scraper, provider-derived tag deletion or metadata
write is permitted. Never deduplicate unrelated editions by their display
titles; a result is an untrusted discovery candidate until a selected MBID
is loaded and its complete tracklist compared with the local CUE.

## Integration and qualification still required

The provider plan is committed on this branch, but the modeless foobar dialog
still calls the strict single-query path. Wire it into the existing
`search_plan_identity` and `search_attempt_index` state machine, preserving
the existing per-click request limit and CUE snapshot verification.
Add offline tests for compilations, missing artist, Unicode, malicious input
and no duplicate query. Confirm exact-head Windows/Linux Core CI and
Win32/x64 foobar builds before offering a user package.

**Status: foundation only; not integrated, not CI-qualified, no host PASS.**
