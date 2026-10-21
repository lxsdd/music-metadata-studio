# Explicit offline Beatport track JSON import

The Beatport catalog adapter in Music Metadata Studio does **not** have
authorized Beatport OAuth credentials or an enabled live network connector.
Do not scrape Beatport or use credentials from unrelated applications.

If you already lawfully possess a complete Beatport **track-detail JSON**
record, you may compare it locally in the existing foobar Prepare Tracks
**Find & compare** page:

1. Select one physical audio file. Its original tags and host playback duration
   are captured read-only. This action does not send anything to a provider.
2. Copy UTF-8 plain text with the line `@beatport-track-json`, followed by a
   complete JSON track object from an independently authorized source:
   ```text
   @beatport-track-json
   {"id":321,"name":"Home","mix_name":"Extended Mix",
    "artists":[{"name":"DJ Example"}],"length_ms":402500,
    "isrc":"GBXYZ2601234","genre":{"name":"Progressive House"}}
   ```
3. Click **Import clipboard**. The bounded JSON is parsed locally; Beatport
   never receives an HTTP request. The UI labels the values as **Pasted:
   beatport**, not as official API-fetched or independently verified evidence.
4. Physical TITLE/ARTIST/VERSION, optional ISRC and actual foobar duration
   are compared with the user-supplied identity. Mismatches, missing duration
   and ambiguous mix identity BLOCK provider proposals. Distinct recording
   and edition field scopes, original custom/multivalue and ReplayGain tags
   are preserved in a read-only comparison.
5. The raw JSON format cannot silently assign a track to any virtual CUE
   subsong. For a CUE source, use the existing explicit structured candidate
   syntax with `@cue_track_ordinal`, keeping unrepresentable tags blocked.

All comparison outcomes are review evidence only. No original tag, artwork,
ReplayGain, CUE carrier, media byte or file is written. This facility does
**not** certify the authenticity, licensing, API availability or correctness
of a user-provided JSON payload.

Automated scope: core positive/negative parsing and mix identity tests,
native production clipboard-handler static audit, exact-SHA Windows
Win32/x64 builds. Installed foobar and genuine provider integration remain
independent acceptance gates.

## Full exact-edition clipboard review (physical and CUE)

A separate **read-only release/tracklist** path is available when you already
lawfully possess both original JSON documents. It never downloads or scrapes
Beatport and never treats pasted evidence as an authenticated API response.

Select a physical track, or use **Inspect CUE** first for an external/embedded
textual CUE, and paste the following **single UTF-8 clipboard text**:

```text
@beatport-release-json
{"id":987,"name":"Home EP","track_count":2}
@beatport-tracks-json
{"count":2,"results":[
  {"id":321,"name":"Home","mix_name":"Extended Mix","number":1,
   "artists":[{"name":"DJ Example"}],"length_ms":402500,
   "release":{"id":987,"name":"Home EP"}},
  {"id":322,"name":"Home","mix_name":"Radio Edit","number":2,
   "artists":[{"name":"DJ Example"}],"length_ms":202500,
   "release":{"id":987,"name":"Home EP"}}
]}
```

The two JSON documents must be complete, separately identifiable, and fit
within the 64-KiB clipboard envelope. **Import clipboard** displays grouped
album and individual recording/edition field comparisons. Track identity
requires a unique, nonconflicting association; radio/extended mixes cannot
borrow each other's credits. For a CUE, global fields compare against real
CUE-global tokens, and track evidence is only associated when the existing
one-to-one matcher has independent identity evidence. CUE INDEX timestamps
are **not** treated as reliable recording durations. Weak associations can
show original values and pasted candidate facts only as **Blocked** evidence.

A missing/invalid release `track_count`, missing or foreign per-track release
IDs, duplicate track IDs/positions, mismatched page `count`, non-first pages,
or a non-null next/previous page indicator **reject the entire edition**.
No partial track page is silently promoted to a complete album. Incomplete
JSON can still be handled as a **single track** using the separate
`@beatport-track-json` path if that one track itself qualifies; never claim
it represents the complete release.

The native exact-edition CUE comparison now also displays a separate **CUE Original** source lane with each original global, track-local, repeated REM and FILE occurrence. A readable CUE with incomplete track identity keeps this inventory visible but blocks all pasted provider proposals. These facts are not Beatport authorization and never approve virtual or physical writes.

Original physical multivalue, ReplayGain and custom fields remain visible
and untouched. Neither imported facts nor displayed Review states authorize
physical/CUE writes, artwork changes, File Operations or external requests.
Live Beatport API discovery is still **disabled** without a verified lawful
provider credential route. Exact-head CI and actual foobar host validation
remain separate from parser correctness.

## Full-edition CUE consistency and duplicate evidence (20 October 2026)

The native user's pasted complete Beatport release now passes **all album
candidate fields together** through the CUE mapping check and **all recording
fields per associated release track together**. Duplicate or aliased CUE
targets cannot bypass duplicate detection by arriving in separate source
fields. Parsed CUE syntax by itself is **not** evidence that the remote edition
is a match. Album-global candidate rows stay blocked unless every local CUE
track and every remote edition track has a unique, conflict-free suggested
one-to-one association. Weak/missing/mismatched mix assignments remain
read-only evidence; other provider-only fields have no fabricated CUE target.
The entire original CUE inventory (including repeated REM/FILE tokens)
remains visible and no writer is invoked.

## Integrated foobar provider mode (21 October 2026)

The **Find & compare** provider selector now lists **Beatport (offline)**
alongside MusicBrainz and Discogs. It intentionally has **no live Beatport
Search**: the button is disabled and labeled `Beatport API locked`, because
Beatport does not supply a licensed application credential/partner agreement to
this project. The existing button becomes **Import Beatport**. Users can
explicitly import complete track-detail JSON, complete edition plus tracklist,
or fully specified factual records from an independently lawful source.
A raw copied webpage is **not** a JSON API response. The app refuses it.

Both the single-track comparison and complete release comparison now use
**one grouped source row per recording/album**, with original physical fields
in a separate group and all field-level evidence in the selected details pane.
Every Beatport-only value stays labelled **user-provided/unverified** and
never enters the verified official Discogs physical staging lane. Beatport
URL/search/OAuth integration remains a separate **license-dependent** gate,
not an undocumented scraper or borrowed public-client credential.
