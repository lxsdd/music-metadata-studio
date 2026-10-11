# Online metadata for external and embedded CUE — product contract

**Product:** Music Metadata Studio  
**Status (2026-10-09): REQUIREMENT APPROVED BY USER; ARCHITECTURE / READ-ONLY NEXT.** This document does not claim that CUE tagging or writing works in the current component. Draft PR #45 remains preview-only and unmerged.

## 1. Mandatory product behavior

Online metadata discovery, matching, normalization, approval and File Operations planning **must support complete CUE albums and individual CUE tracks**, not only standalone files. Support both:
1. **External** `.cue` text files, including single audio-file, multiple FILE directives, albums, various index/pregap values and CUEs whose referenced audio lies inside/outside the current selection;
2. **Embedded textual** CUESHEET records in MP3 ID3v2, FLAC Vorbis Comment, WavPack APEv2 when host and format adapters pass exact preservation tests;
3. **FLAC native Type-5** binary CUESHEET chapters for discovery/read-only comparison initially, with productive writes only after a completely independent qualified writer gate; Type-5 cannot represent arbitrary textual metadata.

A source `.cue` chosen in the current **Candidate comparison** native tab may still be displayed as a row, but the v0 preview **must not** offer generic `FIELD=VALUE` tag import on that raw `.cue` handle. That is intentional until a CUE-aware read-only source/track model and dedicated carrier writer exist. The same applies to virtual subsong writes. **Never** convert selection to a virtual `file_info` generic write.

## 2. Semantic layers are distinct and independently previewed

For any selection, capture three separate immutable original states:

- **Physical audio layer**: actual file/container metadata, e.g. ID3 `TITLE`, `ARTIST`, `ALBUM`, `DATE`, `GENRE`, `BPM`, `KEY`, ReplayGain, artwork, unknown/custom frames.
- **CUE document layer**: external file's raw bytes or single embedded textual CUESHEET raw value, with global album fields, labels, source encoding, filename references and timing/structure.
- **Virtual CUE track layer**: effective per-track title, performer, songwriter, ISRC and inherited global values, plus track ordinal, index boundaries, physical parent identity and source Cue token offsets.

GUI should identify each field's **real target carrier** before displaying `Accept`. A visible track `TITLE` from a virtual CUE must never be mistaken for a physical `TITLE` tag in the full-file MP3/FLAC. A document-global `TITLE` maps to effective `ALBUM` and has cross-track impact; per-track `TITLE` maps to that specific virtual `TITLE`. The same display field can have global and track scope and inheritance semantics; show them separately.

Suggested GUI tabs within the **existing Prepare Tracks window** (do not make a fourth independent dialog):
`Find metadata`/Candidate comparison -> `Metadata changes` with physical vs CUE scope selector and collapsible `Album/CUE globals` + `CUE tracks` sections -> `File locations` coordinated preview. Clear `Source: Discogs release #...`, `Target: embedded CUE track 04`, `Old -> New`, `Impact: 12 inheriting tracks` indicators; one-click global release selection followed by per-track exceptions and optional batch approval.

Matching must use the **complete Cue tracklist** in addition to local foobar handles: physical audio parent, parent cue file, exact ordinal / explicit track numbers, artist/performer roles, original/extended/live/remaster mix qualifiers, runtime computed from INDEX and verified duration, ISRC and release edition. Do not assume a single CUE maps to one release, identical track names imply identical recordings, or a multi-FILE Cue has one physical audio source. Avoid treating CUE global album TITLE as a track TITLE. Preserve unknown values; source catalog credits do not imply writable CUE fields.

## 3. Scope and field capability contract — no guessed REM tags

Existing independently verified `foo_metadata_isolation` v1.0 RC capability is the authoritative reference for **embedded textual** Cue single-value operations, but it is not imported code and does not automatically qualify this plugin's writer:

| Metadata source meaning | Textual CUE target | Preconditions |
| --- | --- | --- |
| Album name | Global `TITLE` | all inheriting effective tracks and CUE document postimage verified |
| Album artist | Global `PERFORMER` | distinguish album-artist vs inherited track artist; mixed overrides require strict impact analysis |
| Track title | `TRACK N` -> `TITLE` | exact track identity and one unambiguous token |
| Track performer | `TRACK N` -> `PERFORMER` | preserve global inheritance; distinguish override/new/delete |
| Track songwriter | Track `SONGWRITER` | exact scope; single value |
| Track ISRC | Track `ISRC` | exact track; don't confuse release/catalog ID |
| Genre | Global `REM GENRE` | single value, approved mapping; multiple genres remain REVIEW or unsupported |
| Date | qualified global/track `REM DATE` | distinguish original recording, album release, digital release and reissue; user policy decides |
| Comment | qualified global/track `REM COMMENT` | preserve inheritance; single value |
| Disc number / total discs | qualified global `REM DISCNUMBER`, `REM TOTALDISCS` | whole-document postimage; no silent reposition |
| Labels, remixers, BPM, KEY, BARCODE, arbitrary `DISCOGS_*` | **No generic mapping guaranteed** | show as physical-tag candidate, derived evidence, or `CUE representation unsupported`; never auto-generate `REM SOMETHING` or lose data |
| `FILE`, `TRACK`, `INDEX`, `PREGAP`, `POSTGAP` | **Structural** | use separate protected structure/association planner, never normalize as free text |

The independent reference's exact qualified subset includes MP3 embedded textual Cue; FLAC textual Cue without competing Type-5; and WavPack textual Cue. Ogg Vorbis/Opus CUESHEET values may exist as physical text but do not imply virtual subsong authoring support; native FLAC Type-5 is read/preserve only.

**External CUE metadata field writer is not yet qualified**. Generic foobar CUE tag writes are known from Metadata Isolation research to risk loss of unknown `REM` and `CDTEXTFILE` directives; do not use. Need dedicated token-preserving patcher for album and track metadata; reuse concepts but independently qualify against raw bytes and full host playback/metadata postimages. No arbitrary credit fields, multi-values or unknown source encodings written absent verified semantics.

## 4. Safe transaction and no-op requirements

Independent explicit decisions for each carrier:
1. **Physical only**: approved changes to ordinary tags must preserve embedded Cue raw bytes and all non-target physical fields; no-op means zero SDK writer calls.
2. **External textual Cue only**: after exact encoding and raw original fingerprint, patch only explicitly approved exact token value spans, retain whitespace/EOL/BOM, unknown REM/flags/index/timing/raw FILE references byte-for-byte. Write the `.cue` text only if proposed raw bytes differ, atomically with rollback and post-write independently reopened Cue validation. Audio file contents/times unchanged.
3. **Embedded textual Cue only**: patch the single physical Cue carrier via qualified format writer using safe physical-subSong-0 route; never write a virtual `file_info` projection. If Cue raw value unchanged, **zero container writer calls**. Preserve MPEG frames/payload, FLAC metadata blocks, WavPack non-target items, ReplayGain, artwork, timestamps where chosen, and independent physical tags.
4. **Coupled external Cue + audio rename/move**: use existing `inspect_external_cue`, `qualify_external_cue_association`, `preview_external_cue_reference_rewrite`. All FILE directives must be checked against *host-verifiable physical identity*, not basename. Validate path-relative names and exact batch targets, including **unselected** audio files and unselected CUE tracks. Treat affected Cue/audio sidecars as one coordinated plan or decline; incomplete global changes must not silently affect unselected tracks. Do not rewrite Cue FILE references if no path change.
5. **Coupled embedded Cue + physical tag change**: one original container, one qualified physical writer operation combining accepted postimages where possible; never call separate writers that could overwrite each other's changes without a full postimage handshake.
6. **Native FLAC Type-5**: read/compare and preserve native binary block; **no write** until independent libFLAC or explicit qualified adapter satisfies structural legality, sample-accurate offsets, preservation of original block/metadata chain, host reload, and full postimage validation. Do not convert Type-5 to textual Cue automatically.
7. **Stale/duplicate/partial-failure**: re-read exact source file/embedded Cue fingerprint, identity, all indexes/references and ruleset immediately before commit. On ambiguity, unknown directives/multivalue loss, blocked format, destination conflict or concurrent change -> **STOP**, no partial silent write. Batch rollback and independent byte/duration/CUE coverage checks.

**Source content never leaves machine** by default: remote lookup uses only user-approved artist/title/ISRC/release queries and must not upload audio, entire Cue text or local paths. Stale provider metadata may be displayed but is not silently refreshed after approval.

## 5. Specific UX cases to cover

- Selecting a *physical album image with embedded Cue* should default to an album-level search and one candidate tracklist matching all virtual tracks, not a separate search per chapter; independent title/artist and album edits with clear scope.
- Selecting *one CUE subsong* should display the enclosing parent audio and document impact, allow reviewing only its track override when safe; request global album changes only with complete impact validation, not implicit full-album editing.
- Selecting *external single-/multi-FILE Cue* should read all FILE references, present unified album/track candidate list, suggest missing tracks, and keep audio physically untouched when only cue text changes.
- Selecting a mixture of external Cue, individual referenced MP3s and embedded Cue tracks should **deduplicate physical source identities**, avoid double-writing, and show one carrier-level execution plan.
- Cases: same title multiple times, duplicate track 01 on separate discs, INDEX00/PREGAP, changing disc number without disc structure mutation, file rename with spaces, encoding UTF-8 BOM/no BOM, CP932/1251/1252 preserved or clearly blocked, 1/2/10/99/101/999 tracks, 2+ disc, selected subset, hardlinks, duplicate FILE references, FLAC text+Type5 simultaneously, malformed and unqualified carriers, cancellation and stale file races.

## 6. How the current preview fits

- **Existing proven read-only plumbing:** external Cue FILE inventory, filename-span rewrite preview and host association **pure core**, no host writer; online candidate rank + release alignment + per-field provenance; physical-only clipboard comparison.
- **Actual installed-host observation 2026-10-09:** Preferences, three tabs, candidate comparison of **sample-a.mp3** displays `ARTIST: Activa → Activa, No change`, `GENRE: Melodic-Trance → House, Review`, `TITLE: Affirmation (Tom Colontonio Mix) → Candidate Import Test, Review`. User's second host result reports `FILE_INTEGRITY=PASS`, unchanged hashes and creation/modified times. This is **a read-only host preview result**, not a complete external Cue or embedded Cue writer qualification.
- **Current temporary block:** local clipboard import for an external `.cue` or an unqualified virtual subsong is refused; the generic normalizer's CUESHEET field protection remains in place. This is an intentional write-boundary, not an approved permanent feature omission.

## 7. Implementation order and test gates

**C1 (next autonomous):** unified immutable `CueCarrierSnapshot` / `CueTrackSnapshot` and per-field `EvidenceScope` mapping. Add pure C++ read-only per-track album matching for external and embedded cues. It must work on known real projected Cue metadata and pre-parsed external Cue track directives, never assume foobar virtual fields equal physical tags. Synthetic regression tests across scope/version/genre/ID/date and mixed FILE/cue conditions.

**C2:** connect real read-only external Cue inventory and per-track metadata parser, plus embedded Cue raw inventory/host projection to the existing Candidate comparison native master/detail view; display CUE carriers, inheritance/conflict and unavailable mapping reasons. Existing external FILE reference-only parser does **not** yet parse full CUE metadata tokens, so C2 must implement that independently.

**C3:** first permitted free online provider (MusicBrainz/Discogs) ranked album-release tracklist candidates for both standalone files and CUE, with no API/credential requirements unless explicitly allowed. Match releases **globally** to tracklist, use shared field evidence and source IDs.

**C4:** dedicated external raw-byte cue metadata patcher with exact value/token-span postimage plus encoding/track/global and atomic rollback gates; independent Win32/x64 host acceptance.

**C5:** dedicated embedded Cue writer adapter using Metadata Isolation findings, with qualified per-format path and independent host acceptance. Treat FLAC Type-5 as separate deferred research lane, not silently supported.

**C6:** integrate approved CUE + physical metadata + rename/copy/move + FILE reference changes into one whole-batch plan and explicit final consent, including foobar-configured file times and zero writes on no-op.

A successful feature requires one-window intuitive operation, exact comparison, correct album-vs-track and physical-vs-virtual scopes, all supported Cue formats, zero unexpected writes/deletions, clear fail-closed unsupported cases, and real installed foobar Win32/x64 validation on disposable fixtures. No user media, app secrets, scraping, paid providers, or premature `main` merge.

## Reference findings

`lxsdd/foo_metadata_isolation` `docs/VIRTUAL_METADATA_CAPABILITY_MATRIX.md`, `docs/PHYSICAL_METADATA_CAPABILITY_MATRIX.md`, `docs/reports/generated/global-scope-20260809.md`, `docs/reports/generated/native-flac-type5-go-no-go-20260822.md`, `docs/reports/generated/internationalization-unicode-cuesheet-encoding-20260822.md`; and Music Metadata Studio `docs/EXTERNAL-CUE-READONLY.md`, `include/djmeta/external_cue.h`, `include/djmeta/cue_association.h`. No reference findings constitute a release or automatic write qualification for the new component.
