# Discogs master and exact-edition dates — source-qualified preview

This document describes the source-only draft implementation on PR #45.
It is **not** a production file/tag writer and does not qualify a release.

## Provenance is not interchangeable

- After an explicit user **Load exact Discogs edition** action, the native
  foobar dialog reads the exact official `/releases/{id}` detail. A returned
  `master_id` may permit an additional **read-only** request to the
  canonical official `/masters/{master_id}` endpoint using the same
  in-memory user authorization. This is not a search result or a scrape.
- The master `year` is a **four-digit master year**, not a proven full
  day/month or a recording creation date. Source ID: `master:<id>`,
  scope: release/master, date meaning: original release.
- Exact edition `year` is independent; when present and internally
  consistent, its `released` full date is preferred for a paired preview.
  Source ID: exact edition ID, scope: edition, date meaning: edition release.
- Missing master linkage, a missing or malformed master year, mismatched
  response IDs, stale local tags, absent duration, mismatched track/mix,
  a CUE carrier or ambiguous provider origin **never** authorize a master
  date proposal. Do not synthesize missing values from the exact edition.
- Both values remain in detached `OnlineFieldReviewSnapshot`. Their
  selection is neither a file-write permission nor a normalization decision.

## User-facing choices

On the **Find & compare** page, select and load a verified Discogs release.
The comparison groups include the physical original, album/edition, matched
tracks and — when independently verified — **Discogs Master**.

The **Date target** dropdown defaults to `Keep original date`. Select
a concrete source date row and choose a target (`DATE`, `ORIGINALDATE`,
`RELEASEDATE`, `DATE_RAW` or `PUBLISHDATE`), then **Include fields
(preview)**. Repeat for the other source if desired. Rejections leave the
original physical tags untouched.

Two deliberate one-click pairing options are also available:

| Preset | Master year (field A) | Exact edition date (field B) |
|---|---|---|
| Preserve existing `DATE` | `ORIGINALDATE` | `RELEASEDATE` |
| Master in `DATE` | `DATE` | `RELEASEDATE` |

Choose a `PAIR:` dropdown option, then **Preview both dates**. If either
separately verified source is absent, the atomic pairing is refused. This
operation replaces only previous **in-memory date selections**, preserving
selected nondate tags. It does not apply on browse, playback, automatic
refresh, close or shutdown. A collision where both remote dates target the
same physical tag fails without partial staging; missing provider fields
cannot delete originals; identical values are true no-ops.

## Qualification still outstanding

Offline Linux/Windows tests validate provider identity and date mapping.
Win32/x64 native host smoke validates load and synthetic source integrity.
The protected credentialed **full master-detail + exact edition + UI**
end-to-end test remains distinct and must run on the **same exact HEAD**,
as must final user-installed foobar acceptance. Until all owner-approved
release/writer gates pass, no mutation, DLL distribution, merge or release
is permitted.
