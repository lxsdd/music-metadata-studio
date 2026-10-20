# Music Metadata Studio — modeless workspace host gate

**Status:** source candidate on PR #45, issue #47. Not a completed installed-foobar acceptance. This page records the conditions for the first necessary Windows test. The component is read-only; no tag, audio, CUE or filesystem write is authorized.

## Contract under test

- One independent top-level native foobar dialog, created with `CreateDialogParamW` and registered with `modeless_dialog_manager`. It is not a modal foobar-owned popup and must not pin itself above foobar.
- A second Prepare Tracks invocation activates the current workspace without discarding candidate rows, routing previews or field decisions. A **different** selection leaves the existing snapshot intact with an explicit warning; close and reopen to choose another selection.
- Window-owned state is freed at `WM_NCDESTROY`, the modeless registration is paired, a failed initialization destroys the HWND, and `initquit::on_quit` closes the workspace.
- Each user action checks rules and underlying metadata/source identity; returning to the window checks all selected source snapshots. A changed source disables decision, import, search and preview actions. **Refresh snapshot** is the only way to accept a new basis: it requires explicit confirmation and discards all earlier decisions, external candidates and online results. There are no background writes or network requests.
- Saved normal window placement/maximized state remains in the original profile-scoped foobar cfg keys, including when the workspace is closed while minimized.

## CI tests (not a replacement for host acceptance)

Verify the **same exact SHA** through Windows/Linux core CTest, production resource-backed Windows HWND tests, no-write static audit, Win32/x64 component build, SDK linkage and PE/package/manifest checks. The Windows test must assert real minimizable/maximizable styles, absence of forced owner, independent taskbar identity, non-overlapping refresh/status/action controls, Tab handling through `IsDialogMessage`, minimize/restore/maximize and HWND destruction. Compile success cannot prove real foobar message-pump navigation or shutdown order.

## Manual host test — disposable music files only

Use `scripts/host-acceptance-noop.ps1 -Mode Prepare` from the **same SHA** as the successful Action's `foo-music-metadata-studio-component` artifact. Follow `docs/HOST-ACCEPTANCE-READONLY.md` to produce disposable copies of one MP3 and an external CUE; never test on irreplaceable originals. Before the test record the bundle's `BUILD-MANIFEST.txt` commit and SHA256 digest.

1. Install the combined `.fb2k-component` matching your foobar architecture and restart foobar. Open **Prepare Tracks** for disposable track(s).
2. Keep Studio open. Use foobar normally: play/pause, browse a different playlist, open Properties on another file, switch back. Studio must not block these actions or stay permanently above foobar. Check taskbar, Alt-Tab, minimize/restore and maximize. Check optical alignment of Find MB and Selected changes, complete Original/Suggested/Status detail cells and release double-click/Enter.
3. Invoke Prepare Tracks again with the **same** selection: one existing Studio is focused, decisions intact. Invoke it with another selection: one window remains; warning tells you close/reopen rather than silently replacing pending decisions. Test Tab, Shift+Tab, Esc/Close and normal foobar shortcuts.
4. Edit a **disposable copy's** tag in foobar with Studio open; re-activate Studio. Read-only actions must refuse the stale snapshot. Click **Refresh snapshot**, confirm its discard warning, verify new metadata appears and old online candidates/decisions vanish. Repeat with a new CUE reference or changed rules only if using disposable copies/configuration, and verify source change blocks stale work.
5. Resize and reposition, minimize, then close using both Close and the titlebar X; reopen and verify restored placement. Maximize, close, reopen and verify maximized restoration. Repeat after foobar restart, including repeated open/close loops and shutting down foobar while Studio remains open.
6. Complete the integrity script's `-Mode Verify` step. Source copies, CUE text and timestamps must remain unmodified by Studio. Ensure no writer path ran. The MusicBrainz search is optional and must be manually clicked; no request may fire on restore, refresh or foobar playback/playlist changes.

**Pass gate:** record foobar version, architecture, Windows DPI/theme, tested commit, exact component artifact and package SHA256, script integrity result and any screenshots/errors. Leave PR #45 Draft and writer routes disabled regardless of runtime result until separately qualified.

## Automated real-workspace host gate (20 October 2026)

The shared `foobar-host-smoke` action is SHA-pinned in `.github/workflows/foobar.yml`. It now drives the actual **Prepare Tracks (Preview)** context command in an isolated official foobar2000 2.26.1 Win32/x64 instance, using only a runner-generated synthetic WAV (never the user's library). It checks that the native Studio modeless window opens, has the expected core HWND controls, responds to the message loop and is reused rather than duplicated on a second invocation. The synthetic WAV SHA256/size/timestamps are checked after actual foobar CLI import and dialog activation. No tag, CUE or media writes are authorized.

**Qualification boundary:** only actual context entry, native window/control presence, modeless reuse, and disposable WAV no-write behavior are covered. Visual DPI alignment, all tab interactions, physical/CUE-specific review, authenticated live Discogs/Beatport/MB providers, release matching, Apply gating and full human acceptance remain **NOT_TESTED** until independently verified. A passing host smoke must not be reported as `HOST_FULL_PASS`.

## Expanded offline native clipboard host path (20 October 2026)

The SHA-pinned shared host runner now opens the actual native modeless Studio
dialog, activates the production `Import clipboard` button on a disposable
WAV and verifies that new candidate field rows populate the actual Win32
ListView. The clipboard contains only deterministic, user-declared synthetic
Discogs *claims*, **not** authenticated Discogs API results. This is a real
read-only UI-handler test, not a live provider test and not a substitute for
checking provenance or comparison values in a final human review. The
existing same-source workspace reuse and synthetic WAV integrity checks
remain mandatory. The runner clipboard is cleared after this probe.

## Expanded synthetic Beatport release and stale source host gates

The exact-SHA pinned runner now optionally imports a complete **invented**
Beatport release document and an independently invented two-track page with
Extended Mix and Radio Edit. The production Import clipboard control must
populate at least three release/track summary rows, a native field-details
list, and the read-only release-details status. This is **manual offline
source-declared evidence only**; it is not an official API query and cannot
approve a match when the synthetic WAV has no matching recording identity.

After confirming byte/timestamp integrity of the original disposable WAV,
the runner deliberately modifies **only this temporary test WAV** and
activates Import clipboard again. A PASS requires the real Studio window to
enter `STALE snapshot (Read-only)` mode and disable that native control.
Production music, physical CUE, ReplayGain, artwork and actual writer paths
are never modified. These focused host passes are not a full provider or
writer-safety qualification.


## Additional native review content qualification — blocked, not certified

On 20 October 2026, three increasingly strict external-host text probes
demonstrated a Windows testing limitation, not a proven metadata failure.
Both foobar builds load and render a read-only two-mix Beatport release with
native release/detail rows and no modified WAV. However, the virtual
`LVS_OWNERDATA` ListView does not support external `LVM_GETITEMTEXT`;
cross-process `WM_NOTIFY` is prohibited; Windows UI Automation on the
current host does not expose `GridPattern` for this control. These
unqualified experiments are **not** certified PASS, and are deliberately
unhooked from the production host gate. The ordinary host runner is pinned
back to the last successful, actual Win32/x64 build/GUI/offline
edition/stale-source gate.

A future *supported* in-process native test must call the same presentation
function as the production UI and assert both exact mix identities, source
provenance and blocked/no-op review states; independent full-host acceptance
remains pending. No debug IPC/backdoor, injected messages, secret access or
production media writes are justified by the test limitation.

## In-process native row-label contract

The actual `batch_preview_dialog.cpp` owner-data display handler now calls
the same pure `native_candidate_source_label` and
`native_candidate_status_label` functions used in the positive/negative
Discogs and Beatport full-release review tests. These tests exercise
complete edition parsing, track/mix matching, unverified-track blocking,
source-aware custom multivalue retention, then inspect the **actual text
contract** consumed by the Win32 virtual ListView. Both Windows and Linux
core CI validate it; the full foobar host still verifies control creation,
real import and proper stale lockdown. This closes an isolated presentation
logic coverage gap without falsely claiming that the inaccessible external
UI automation readback passed. Visual correctness and CUE host workflows
remain for eventual real foobar acceptance.

## Actual external CUE host boundary (20 October 2026)

The pinned shared Win32/x64 foobar host now attempts an **independent,
disposable external text-CUE user workflow** after the existing audio-only
review: it creates a two-track synthetic `.cue` referencing the isolated
verified 8kHz WAV (the only audio source), closes the modeless workspace,
reopens the real Prepare Tracks context action on the CUE, invokes **Inspect
CUE**, and requires a real native CUE original inventory. It then presses the
production **Import clipboard** button with a complete, fabricated two-mix
Beatport edition and checks release/track summary and detail controls. It
verifies both original CUE bytes/timestamp and audio bytes/timestamp remain
unchanged and returns to the original WAV to retain the independent disk-stale
gate. The CUE content intentionally contains two global REM GENRE values and
a custom REM field; no CUE rewrite is ever performed.

The host gate only proves control activation, read-only source inventory,
native summary/detail population, and file integrity. The exact field-level
agreement/abstention, multivalue and duplicate semantic decisions are checked
separately by portable in-process core tests. No authenticated provider request,
productive CUE writer, FLAC Type-5 writer or user-collection access occurs.

## Negative real-host external CUE stale-snapshot gate

After passing the original CUE plus WAV byte/size/timestamp integrity
checks, the isolated runner deliberately appends a clearly identified
`QA_ONLY` line to **its own temporary CUE** and changes only that fixture's
mtime. The production Studio Import action must compare captured physical
source identity, mark the native workspace `STALE snapshot (Read-only)`,
disable further clipboard comparison, and preserve the audio WAV. This
guards against a file changed outside foobar while its metadb info remains
cached. Mutating the temporary CUE is an intentional **negative test**, not a
component-initiated metadata or production-media write.

## Fixed real-host virtual external CUE stale guard

The negative Win32 host test first revealed that foobar may supply a
`.cue` as a **virtual subsong**. The preview previously recorded a
physical source version only for a unique subsong-0 selection, allowing a
CUE on-disk mutation to escape the cached metadb fingerprint. Source capture
now observes the immutable physical identity/version of the external
`.cue` regardless of virtual track index, with one OS probe per distinct
CUE path even when several virtual tracks were selected. This establishes
read-only source validation **without** qualifying virtual tracks for writes.
Both interactive and asynchronous source checks fail closed if an external
CUE lacks a qualified guard or changes on disk. The runner deliberately
mutates only a disposable fixture after intact comparison and requires a
`STALE snapshot` lockdown.

## Embedded FLAC CUESHEET native host qualification (20 October 2026)

The isolated Win32/x64 host test now creates a **deterministic valid FLAC**
containing exactly one second of silent 8-kHz mono PCM compressed as a
constant FLAC frame. A self-contained Python-standard-library generator
adds a multiline textual `CUESHEET` **Vorbis comment** with two tracks,
two original REM GENRE values and an unknown REM CUSTOM value. It is a
synthetic fixture, not user music and not a third-party download. The runner
must run the real foobar context action against the FLAC, enable and click
**Inspect CUE**, populate the native source inventory, then import an
invented complete two-mix Beatport edition through the real clipboard
button. SHA-256, byte length and last-modified time for both the FLAC and
original WAV must be identical after these read-only operations. The
original WAV's independent stale guard still executes afterward.

This qualifies **embedded textual Vorbis-comment CUESHEET observation only**.
It does not qualify embedded CUE playback, write-safe tag/CUE commits,
FLAC Type-5 binary CUESHEET block rewriting, artwork, ReplayGain or any
user-library modifications. These remain blocked until separately proven.

## Real host: dual text/Type-5 FLAC preservation and embedded stale lock

The embedded FLAC fixture now contains both an editable-text **Vorbis
Comment CUESHEET** and an independent, structurally valid **native FLAC
Type-5 binary CUESHEET** metadata block. A standalone FLAC 1.5.0 reference
decoder and `metaflac` parser validate the fixture's 1-second audio and
three-entry binary cue block. Native Win32/x64 Studio reads the textual
carrier and stages the synthetic two-mix online comparison, then requires
*identical SHA-256/byte length/mtime of the entire FLAC*, inherently
preserving both CUE representations. Afterward the isolated runner
deliberately alters just its own FLAC's disk bytes and timestamp. The
open native Studio must enter `STALE snapshot (Read-only)` and disable
subsequent import actions, including when foobar previously selected a
virtual embedded-CUE track. Type-5 **writing** remains prohibited and
unqualified; the test validates *read-only byte preservation* only.

## Real Multi-FILE CUE host qualification (October 2026)

The exact-SHA Win32/x64 shared foobar runner now opens an isolated CUE
referencing **two distinct disposable physical WAV files**, each with its
own `FILE` line and intentionally restarted `TRACK 01`. It invokes
the native **Inspect CUE** and a complete, invented two-mix Beatport
edition comparison through the actual modeless Studio window. The original
external CUE and **both** audio files must retain their exact bytes, sizes
and timestamps. No remote Beatport request or writer path is involved.

A corresponding in-process cross-release matching gate explicitly
suppresses duplicated track-number position evidence across FILE blocks,
without losing the individual CUE source ordinal or fabricating a disc
number. Sequential globally unique track numbers remain usable. This
does not claim that the existence or rename safety of arbitrary unselected
FILE paths has yet been qualified for any physical write plan.

## Native Multi-FILE TRACK numbering qualification boundary

The first two-source host experiment used `TRACK 01` in both `FILE`
sections; the real foobar2000 context route did not open the Studio window
on that CUE, so that *host configuration is not claimed as supported or
qualified*. The deterministic host fixture now uses a sequential
`TRACK 01` / `TRACK 02` across the two distinct WAV `FILE`s.
The separate Core matching gate still tests repeated track numbers across
FILEs, and withholds any unverified numbered-position evidence. This
avoids weakening validation or misclassifying a parser-only result as a
real host success.

## Native File locations tab end-to-end read-only QA

The exact-SHA shared Windows runner now navigates the **actual third tab
in real isolated Win32/x64 foobar**, enters a disposable runner directory
as the raw preview destination, evaluates `%filename%` for the synthetic
WAV, selects its native ListView item and invokes the actual context-menu
command `Inspect selected raw targets`. The native footer must show
one inspected candidate and explicitly state that final foobar FileOps
destinations and CUE dependencies remain unverified. The original
synthetic WAV's SHA-256, byte count and modification time must remain
identical. The test returns to online comparison before the existing
Discogs/Beatport/CUE checks. All cross-process UI interactions avoid
pointer-bearing tab/ListView messages and operate on isolated fixtures.
This proves an interactive **raw candidate inspection**, never target
authorization, user music writes or OS rename/move safety.

## Physical source-alias feedback in real File locations

The read-only raw target inspection footer now includes a count of targets
that physically alias any selected source (including another selected
audio in the batch). The alias result is derived from the existing OS
physical-file-ID and source-guard checks; it is not inferred from
filename text or an extension. An alias remains a conflict and **never**
turns the raw target into a qualified File Operations destination.
