# Candidate comparison — local read-only clipboard intake

**Status:** physical-track clipboard preview and Inspect CUE native UI accepted in foobar; **new CUE-specific clipboard comparison** remains subject to exact-head CI and a separate installed-host read-only acceptance.
The **Candidate comparison** tab in `Music Metadata Studio → Prepare Tracks (Preview)` accepts a deliberately structured plain-text candidate that you copied or authored yourself. **Source column `Pasted: discogs` identifies only the user's declared provider; it is not a Discogs API result or source verification.** The tooltip retains the declared source ID and marks the claim unverified. It **never automatically fetches** Beatport, Discogs, Deezer, Spotify, or other services, never logs into third-party sites, and never writes tags/files. It is an initial zero-cost/offline fallback alongside planned authorized provider connectors.

## How to try it (after the exact candidate build passes CI and installed-host QA)

1. Select **one real physical audio track** in foobar2000 and open **Music Metadata Studio → Prepare Tracks (Preview)**. Do not choose an embedded CUE subsong for this initial comparison.
2. Copy the following **synthetic sample** as plain text to the Windows clipboard. Substitute factual metadata for a user-chosen candidate only when known:

```text
@provider=discogs
@id=release:123456
@scope=edition
ARTIST=Example Artist
TITLE=Example Track (Extended Mix)
GENRE=House
GENRE=Deep House
DATE_RAW@edition=1998-06-15
LABEL=Example Records
```

3. Open **Candidate comparison**, click **Import clipboard** and inspect the two existing native tables: selected foobar track and `Field | Original | Proposed | Source | Status`.
4. `No change` means exact ordered multivalue equality, `Review` means new or different value, `Blocked` means structural tag, malformed value, conflicting date semantics or ambiguous local duplicate field. These are **not** approval decisions. Changing tab returns to the original normalization editor without modifying its pending choices.
5. Close the entire window with Close. **No tags, CUE records, ReplayGain, naming expressions, paths, music files or timestamp values are touched.**

## Compare clipboard metadata against an external or embedded CUE (new)

This is a **read-only** field comparison. It does not call Discogs/Beatport/Deezer or authorize any physical or virtual foobar writes. CUE `FILE` references, `INDEX` timing and unknown REM directives are never rewritten.

1. Select an **external `.cue`** from the foobar playlist, or the **physical subsong 0 of an audio file with an embedded textual CUESHEET**. Open **Prepare Tracks → Candidate comparison → Inspect CUE**. This is a required, explicit switch to CUE semantics. (Individual virtual subsongs remain unsupported as independent physical writer targets.)
2. For an **album/release** proposal, copy this entirely synthetic, user-supplied sample:

```text
@provider=discogs
@id=release:12345
@scope=edition
ALBUM=Example Album
GENRE=Trance
LABEL=Example Records
```

3. Click **Import clipboard** in the same view. `ALBUM` maps only to the album-global CUE `TITLE`. `GENRE` maps to `REM GENRE`. An unsupported `LABEL` is **Blocked** (we never invent `REM LABEL`). `No change` means exact equality; `Review` only displays a proposed difference and **does not enable writes**.
4. For an **individual CUE track** proposal, use a separate clipboard candidate. The track ordinal is mandatory and starts at **1 for the first `TRACK` in CUE order**, even if two different `FILE` sections both contain `TRACK 01`:

```text
@provider=beatport
@id=track:98765
@scope=recording
@cue_track_ordinal=2
TITLE=Example Second Track (Extended Mix)
ARTIST=Example Performer
KEY=G minor
```

5. The ordinal must exist and identify an AUDIO CUE track; otherwise comparison refuses the candidate rather than guessing. `TITLE` maps to the selected CUE track `TITLE`, `ARTIST` to the track's `PERFORMER` (with global inheritance shown), `KEY` is **Blocked** because it has no qualified standard text-CUE destination.
6. Switching between **Inspect CUE** and **Import clipboard** only changes the visible comparison rows. Selecting a different source invalidates the previous source-specific candidate mapping. A physical file without explicitly selected CUE inspection continues to use the ordinary physical-tag comparison.
7. This syntax is a development/diagnostic intake, **not the intended final user-facing online search workflow**. Real provider lookup/release alignment, batch CUE track candidates and write approval remain future gated steps.

The CUE comparison rejects ambiguous/unqualified source syntax, unknown encoding, unsupported values, duplicate target tokens, impossible track ordinals and misleading album-versus-track scope. It reads CUE bytes through the existing qualified read-only source adapter and verifies the selected source and rules before comparing. For embedded CUE, it requires the exact textual CUESHEET from physical subsong 0; FLAC binary Type-5 is not rewritten.

## Strict input rules

- A single candidate per import: mandatory `@provider` and `@id` headers precede all fields; optional `@scope=recording|release|edition` (default `recording`). **For CUE recording scope**, `@cue_track_ordinal=N` (1–1024, bounded by the actual CUE) is mandatory; it is forbidden in album scope. It never means the declared `TRACK NN` number.
- `@id` is a user-supplied provider object identifier (such as `release:123456`); it is not automatically verified or fetched.
- Each tag is exactly `FIELD=VALUE`; duplicate names represent ordered multivalues; the semicolon character is literal and is **never split**. Keep artist and remix information separate where possible.
- For `DATE` or `DATE_RAW`, explicitly append `@original`, `@edition`, or `@digital`: these dates have different meanings. The plugin will not guess.
- Up to 64 KiB input, 160 lines, 96 distinct fields, 32 values/field; valid UTF-8; reject unknown directives, incomplete identity, empty values and NUL bytes.
- Real foobar rule metadata remains read-only and protected; a physical track is rechecked for stale metadata/rules immediately before comparison. Virtual subsongs are not accepted as physical input during this initial host-qualified flow.
- UI does not merge manual fields into normalizer decisions or filesystem operations. User-supplied `@provider` is **not evidence of an authenticated provider API call**.
- Source field text is displayed without automatic changes to capitalization, dates, aliases, remix names or tag order.

## Known limitations and host qualification

- Plain text must be prepared in this simple syntax; copying an arbitrary Beatport/Discogs web page is **not** yet a valid structured candidate.
- This tab displays unverified source evidence and is not yet connected to a real provider search or release-level matching/approval UI; original normalizer's approvals remain independent.
- Candidate preview currently shows one local physical-track or explicitly selected CUE album/track candidate at a time; no multi-source synthesis, offline saved sessions, cover import, automatic release matching, or clipboard-to-file changes.
- Mandatory next gate: test in actual foobar2000 2.x (Win32/x64), including button visibility, tab/keyboard navigation, large/multivalue Unicode, physical-vs-virtual selection, Dark Mode, DPI and clipboard unavailable/malformed behavior. GitHub Actions cannot substitute for this installed-host test.
- Run existing no-write integrity test on disposable MP3/CUE samples for installed-host acceptance. The tool intentionally has no new writer path.

## Architecture

`Windows CF_UNICODETEXT` → bounded UTF-8 conversion → `parse_manual_candidate` → (physical source: `review_online_fields`; explicit inspected CUE: `read_cue_raw_on_demand` → `inspect_cue_metadata` → `review_manual_cue_candidate`) → same native foobar review table (display only). **The CUE branch reads the CUE source on demand**; neither branch opens files for writing or uses HTTP. No control with tag-writing authority.
