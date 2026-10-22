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

The **Find & compare** provider selector now lists **Beatport + Crates**
alongside MusicBrainz and Discogs. It intentionally has **no licensed Beatport catalog API Search**; a **single public track URL lookup** is now supported, because
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

## OneTagger feature parity — search-only fallback

The public OneTagger Beatport integration informed the **architecture**, not
copied source code or provider credentials. The C++ provider now plans the
original artist + unmodified **base title** search first; only afterward a
bounded feature-credit-free fallback for `feat.`, `ft.`, `featuring` may be
used by a future **authorized** API connector. The original complete local
artist/title, named mix, ISRC and duration remain required by the independent
match gate. This fallback is intentionally *not* a new scraping transport,
not fuzzy permission to accept the wrong version, and never a writer.
OneTagger is GPL-3.0; no code or embedded/shared OAuth client secrets are
transplanted. Beatport API authorization remains a separate release gate.

## ISRC-first discovery contract

Like OneTagger, a future properly licensed Beatport API connector may first
search with a *validated* normalized ISRC before the original artist+base-title
and title-only strategies. The fallback ordering is deterministic and bounded.
A shared ISRC alone **does not identify a specific Extended/Radio/Remix**;
all retrieved candidates still undergo independent strict mix-name, artist,
original title and physical duration verification. Never take the first ISRC
hit as automatically correct or write-authorized. This is an inert catalog
path plan until the project's own legally approved Beatport transport exists.

## Public Beatport webpage intake — source-only foundation (21 October 2026)

**Private user-initiated retrieval of public pages is not blanket-illegal in
Germany.** API licensing and website browsing are separate contracts;
applicable copyright/database rights, any binding website conditions,
robots/access restrictions and local laws still matter. No assertion that
Beatport has permitted an HTTP crawler is made here. The external development
tool received HTTP 403 when trying to read `beatport.com/robots.txt`, so
we could not verify a crawl policy or live page layout from that environment.

Music Metadata Studio now accepts the following small, explicit, bounded
*user-supplied* format from a public track page:

```text
@beatport-public-jsonld
@url=https://www.beatport.com/track/home/321
{"@type":"MusicRecording","url":"https://www.beatport.com/track/home/321","name":"Home (Extended Mix)","byArtist":{"name":"DJ Example"},"duration":"PT6M42S"}
```

The source URL must be canonical HTTPS `www.beatport.com/track/slug/id`.
The JSON-LD must identify the *same track URL* if it declares a URL and must
provide a recording title, artist and real duration. The normalizer verifies
physical TITLE, ARTIST, mix/version, optional ISRC and foobar duration.
Unverified `datePublished` or `inAlbum` metadata cannot be silently
promoted to physical `DATE` or `ALBUM` without separate edition provenance.
This is **not yet automatic fetching** or a verified official API response,
and does not authorize tag, CUE, file, artwork or ReplayGain writes.
The next implementation gate is a bounded, explicit, public-page transport,
if its applicable access policy can be checked without circumventing
restrictions. No batch crawler, hidden endpoints, tokens or login capture.

## Browser-assisted public page import (no added HTTP requests)

The development helper `scripts/beatport-copy-public-jsonld.js` runs only
when the user invokes it on a public **already open** Beatport track page.
It reads standard `application/ld+json` data already present in the page,
requires a single matching `MusicRecording` object, copies it in the bounded
`@beatport-public-jsonld` format, and makes no further requests. Nothing
is sent to a third party or persisted by the helper. No foobar writes occur.
The user's action is then **Beatport + Crates > Import Beatport**.

To experiment without extensions, paste the helper's JavaScript source into
the browser console while viewing the public track page. Alternatively,
prefix the complete single-line function with `javascript:` as the URL
of a personally created bookmark. It is **not** a site-crawling service,
and no background or batch mode exists.

The structure actually embedded by Beatport is not yet verified: if
`MusicRecording` JSON-LD is missing, ambiguous or inconsistent with the
visible canonical URL, the helper refuses instead of scraping private
JavaScript state, calling undocumented API endpoints, or guessing a mix.
These failures are useful evidence for deciding on a qualified native
single-page HTML/URL integration. The right to read public pages and the
site's access policies must still be assessed for the intended use.

## Direct public Beatport URL retrieval in foobar (read-only)

In Find & compare choose Beatport + Crates, select a physical audio recording,
paste its exact public HTTPS URL in the query field, and click Load Beatport URL.
The component performs ONE bounded GET to www.beatport.com; it never sends
cookies/credentials, follows redirects, retries, uses partner APIs or bypasses
access challenges. The HTML parser only recognizes exactly identified,
uniquely matching MusicRecording JSON-LD, which is compared with local title,
mix, ISRC and physical duration. Source data is NOT an authenticated API
record and never receives tag/CUE/file writer authority.

HTTP 401/403/429, redirect, missing embedded identity, or inaccessible markup
stop immediately. The user-initiated browser helper remains the fallback.
This feature is not automatic search, not a batch crawl, and not a verified
live Beatport API integration. Genuine public-page layout and permissions
have not yet been qualified by a real Beatport fetch.

## Crates.co account-safe browser search (2026-10-21)

In the native **Beatport + Crates** provider, select a physical recording and
click **Find in Crates**. The browser hand-off copies only the original
physical artist and full title (including e.g. Extended Mix) to the Windows
clipboard and opens the **verified public** https://crates.co/ homepage in
the user's default browser. Paste the copied query into Crates' existing
Search field. There is **no assumed deep-link URL or undocumented search
API**. Crates is usable without login; when the user logs into Crates with
their **own Beatport account inside the normal browser**, that login stays
there. Neither a username/password nor a session/OAuth token is read by
foobar, stored in a component, copied to GitHub or sent to this application.

After choosing a Crates hit, copy its *actual exact Beatport track link*
using the visible site's normal link/share functionality when available,
paste that link into the studio query box, and click **Load Beatport URL**.
The existing pinned-host one-page HTML review checks independent title,
artist, mix, ID, ISRC and duration. A Crates suggestion is **discovery,
not a verified source**; it never lifts the no-write gate and does not
automatically authorize a different recording or mix. If the public Beatport
site denies HTTP access or lacks its required JSON-LD, the component stops;
use the separately user-triggered Beatport browser helper if appropriate.

Crates account login is **optional for metadata discovery** (though useful
for My Beatport/account-specific browsing), but is **not an entitlement
to Crates' private API or a transferable Beatport developer token**. Any
future direct Crates data connector requires provider authorization and a
separate secure login consent/permissions path, without requesting passwords
in chat.

## Browser-only Crates track-link helper

`scripts/crates-copy-selected-beatport-link.js` can be run on an already
open Crates.co tab (in a browser console, or as a user-created bookmarklet).
It examines **visible** track links and copies only a fully qualified public
Beatport `https://www.beatport.com/track/slug/id` URL. It never accesses
cookies, network, Crates' private API or application login state. If the
user has selected text in a specific track row, that row has priority.
Without a selected row exactly one visible matching link must exist on
the page. Multiple links fail closed instead of taking the first search hit.

This helper is **conditional on the Crates DOM exposing ordinary public
Beatport anchors**. That is supported as a plausible use case by an
independent Crates link-extraction extension, but actual 2026 logged-in
page markup has **not yet been verified**. When no link is present, the
helper reports that fact without inspecting private application state.
No extension is required; the alternative is the built-in Crates share
feature (shortcut S) and manual lookup of the visible Beatport link.

Never request the user's Beatport password, session cookie, Crates
OAuth token or authentication code in chat, CI or a public issue.

## Browser hand-off credential and visibility hardening (2026-10-22)

The Crates.co optional browser helper now rejects Beatport links carrying
URL-embedded username/password, nonstandard ports, query parameters,
fragments, and DOM elements concealed with CSS visibility, display, or zero
opacity. This avoids copying hidden/unintended links or accidental credentials.
The browser script never reads cookies, local storage, OAuth tokens, or
Crates' internal API responses. The site URL must be the official Crates
HTTPS origin on its standard port, and a single visible matching track URL
remains mandatory. Offline Node DOM safety tests cover the rejection cases.
