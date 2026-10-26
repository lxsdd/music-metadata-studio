# CUE responsibility boundaries — Music Metadata Studio / Precision CUE

Status: coordination contract on PR #45 (Studio). Precision CUE PR #52 is an
**independent active worker** and must not be cherry-picked, overwritten,
merged or modified blindly.

## One eventual native foobar2000 component

**Studio owns:** Existing external/embedded CUE inspection as a read-only
input to Discogs/Beatport/Deezer/MusicBrainz metadata discovery, original
physical/CUE-global/CUE-track provenance, tag normalization, candidate
comparison, Accept/Reject/Reset review, source-aware file-organization
preview and user-facing modeless foobar UI.

**Precision CUE owns:** Generating a **new** CUE for a selected
single-file continuous mix: exact edition/tracklist and duration evidence,
integer timing and 75fps quantization, gap/transition uncertainty,
externally staged CUE creation and separately qualified embedded-write
research and preflight. Its timing/generation implementation is NOT
replicated in Studio.

**Shared once after explicit integration review:** Source version/identity
guards, original-CUE carrier readers, protected physical vs. virtual
semantics, external/embedded CUE encoding/structure protection, no-op
comparison, batch-approved writer/rollback policies, and compatible native
visual controls. Until that integration, each worker can maintain local
**read-only safety assertions** independently; that is temporary
qualification overlap, not permission to ship duplicate writers.

Integration cannot be assumed from green CI on either branch.
`component.rc`, `resource.h`, native workspace code, CMake, SDK
project and GitHub workflows are touched by both branches. Resolve
their conflicts deliberately with regression tests and one final real
Win32/x64 foobar acceptance, rather than a blind merge.

**Current hard boundary:** Neither a parser, a read-only native host
preview, a CUE postimage calculator nor a successful test fixture is
permission to write a physical/virtual audio track, an external/embedded
CUE, a FLAC Type-5 block or artwork. Zero unapproved media writes.

## Multi-FILE read-only batch preflight — October 2026

The Studio-specific file plan now binds every selected physical
audio participant of a Multi-FILE external CUE into one immutable
approval fingerprint, while retaining the legacy single-source
association. Missing, duplicated, unqualified or contradictory
participants block review. The separate FILE-reference association
checker refuses selected audio absent from the parsed CUE or one
physical source projected to incompatible FILE names.

These are NO-I/O **previews**, not an executor or proof of native OS
canonical paths. Exact host verification and the future write/rollback
gate remain mandatory. Precision CUE #52 is untouched.

## Integrated existing-CUE file-organization preview

The Studio core regression now exercises a source-derived complete
chain: parse two literal `FILE` lines, match independently observed
physical audio keys, compute a no-I/O CUE reference postimage, bind all
resolved physical IDs and both CUE fingerprints into the batch plan,
freeze the *exact reviewed* proposal, then fail closed when an audio
dependency, its ordering or the CUE postimage changes. Existing unrelated
CUE contents (for example REM GENRE) remain byte-preserved in the
postimage, and no actual file or tag mutation occurs.

The live foobar File locations tab still needs a separately qualified
host-level canonical destination/CUE dependency discovery step before
it can offer real file operations. This test does **not** certify that
future executor, nor duplicate any Precision CUE timing/generation work.

## Native external FILE source inventory (October 2026)

In the real modeless Studio **Inspect CUE** action for external text
`.cue` carriers, the UI now additionally displays one read-only
`FILE #N / Physical source preflight` row per inspected dependency.
After validating the original CUE's OS/foobar identity, a bounded
(128-reference) host probe checks each *same-directory* filename and
reports PRESENT, MISSING, ALIAS or NOT QUALIFIED. Unicode names remain
intact. Relative directories, traversal, drive/URI/ADS aliases, reparse
points and Windows device names fail closed instead of being guessed.
The CUE's source guard is checked again after the observations, and the
original CUE rows/bytes remain untouched. A large CUE retains its full
original inventory but explicitly reports remaining dependencies as
uninspected. This is extra evidence for the user, **not** certification
of source-to-destination routing or a FILE rewrite. The File locations
tab's `cue_dependencies_checked` and `filesystem_target_checked`
still remain false pending separately qualified native FileOps mapping.
Neither actual file movement nor Precision CUE generator code is invoked.

## Native FILE-reference rows: cross-architecture test gate

The shared Win32/x64 runner now requires not only that a real disposable
CUE can be opened and imported, but that the native **Inspect CUE** table
contains at least the complete original CUE inventory **plus one physical
read-only FILE source preflight row per referenced audio** (one FILE
for the standalone fixture, two for the Multi-FILE fixture). Missing
those rows makes CI fail, not simply report a successful window open.
These are source observations, not confirmed relocation destinations,
write approvals, or a substitute for eventual real-user visual inspection.

## Ambiguous CUE graph block (Studio read-only batch preflight)

The exact immutable file-organization batch now **blocks the audio source
and every external CUE carrier** if multiple selected external CUEs claim
the same physical audio identity. This includes an otherwise inactive
second CUE and cannot be overridden with batch overwrite consent.
A single Multi-FILE CUE legitimately referencing several *different*
audio sources remains eligible, as do distinct audio/CUE pairs. The
contract does not guess which CUE should prevail; the eventual user must
resolve the ambiguity with an explicit selection before physical
operations can be enabled. No CUE generation or writer is involved.

## Existing-target physical identity gate (October 2026)

An actual existing destination now requires a **separately observed host
physical file ID** in addition to its canonical path and version guard.
The approved plan fingerprint binds that ID. Two apparently different
destination names which are OS hardlinks/aliases of the *same file* are
blocked, as is an existing destination that physically aliases ANY
selected source, even under another canonical path. An existing file
without an observed identity is blocked; an allegedly missing target with
a nonempty physical ID is contradictory and blocked. Physical ID/version
changes invalidate the entire frozen approval; blanket overwrite consent
cannot bypass these tests. All tests are pure/fixture-only and non-writing.
The native Studio target path remains unqualified until foobar's final
File Operations target mapping and filesystem readback are separately
proved; this does not enable a write-capable implementation.

## Early Windows raw target spelling collisions

Studio flags two proposed *raw* Title Formatting destinations with
different text but the same ASCII-case-folded Windows path and
slash/backslash separators, including differences in root spelling.
Both rows display `RAW_TARGET_CASE_OR_SEPARATOR_COLLISION` in the
File locations preview. This is only a **possible** collision,
not a canonical filesystem collision or a Unicode case-folding oracle.
A separate final foobar File Operations sanitizer, OS physical-ID probe
and approval are mandatory; no file write path is enabled.

## Raw target preview: post-probe source consistency

A user-triggered RAW destination inspection now verifies *all selected
physical source identities and versions twice*: once before the bounded
host filesystem reads, and again **after the last destination probe**
but before any new inspection result reaches the native table. foobar's
cached metadb values alone are not used as a substitute for the physical
source version. If a second process changes a file during that window,
the inspection fails as one unit without publishing partially updated
target states. This deliberately leaves the final FileOps destination
and CUE dependency gates unchanged/unqualified. No writes were enabled.

## Double-observed native raw destination integrity

The bounded GUI-triggered raw-target inspection now performs **two separate
read-only observations of every selected raw candidate**. After the
physical audio-source and metadb/rules checks, target identities, versions
and even observed absence must still match a second filesystem pass.
A newly created formerly absent target, an exchanged hardlink, changes
to the target version or canonical path, and a changing unsupported
filesystem result all abort the whole inspection **before any new table
rows are published**. Windows native regression tests cover each state
transition. These are preliminary Title Formatting paths and do not
enable foobar's final File Operations or any media writer.

## File locations native read-only inspection feedback

After a user-triggered raw destination inspection, the real Studio
File locations footer reports how many selected raw candidates were
observed as present, missing or unqualified, with an explicit statement
that *final foobar File Operations destinations and CUE dependencies
remain unverified*. It cannot turn an advisory observation into an
approved file operation and does not write files.

## Contradictory raw destination observation (2026-10-21)

The read-only Studio File locations table now flags `RAW_TARGET_OBSERVATION_CONTRADICTORY` whenever a destination marked Missing, NotInspected, or Unqualified still carries a previous physical target ID or version guard. The native status makes this inconsistency visible before any subsequent final foobar File Operations qualification. This is not proof of a canonical target, does not approve a write, and does not touch Precision CUE.

## SDK host-runner transport resilience (2026-10-21)

The official foobar SDK bootstrap uses three bounded retries only when the HTTPS transport fails, deleting incomplete download files between attempts. The original official URL, pinned expected archive length, SHA-256 digest, archive integrity and version checks remain unchanged; a hash mismatch fails closed instead of selecting an unaudited mirror. This addresses an observed remote host outage and does not qualify or enable media writing.

## Lexical existing-CUE FILE safety parity (2026-10-21)

The Studio-only same-directory `FILE` locator now rejects malformed UTF-8
and Win32 device aliases with spaces before extensions or superscript COM/LPT
digits. A cross-platform core regression covers accepted UTF-8 basenames,
reserved devices, drive/ADS escapes, traversal and malformed carrier paths.
This is an advisory source-lookup preflight, **not** a FileOps destination
sanitizer, final physical dependency association, CUE creation or writer.
Precision CUE PR #52 remains independent and unchanged.

## SDK transport fallback: pinned template tree (2026-10-21)

Studio first attempts the same official foobar2000 SDK 2026-10-01 HTTPS
download with bounded retries and requires the exact original 793738-byte
archive SHA-256, valid 7z and correct SDK version. If **transport alone**
fails, the Windows runner may use the already vendored, license-noticed
SDK from our own public `foobar2000_component_template`, checked out at
Git commit `31a35947a7527826c2199491692802e3ce3d8bd7`, after checking
that the `vendor/sdk` Git tree is exactly
`2ba8caeb73f72633ad7f60ee5f72ed8074a0eb17`. Its `SDK-PIN.json`,
license, version and required project files must match as well. This
**Git-tree proof is not claimed to be a SHA-256 of a downloaded archive**.
An official download with mismatched hash, wrong size or invalid extraction
never switches to the fallback. No SDK bytes are added to Studio history,
and no component DLL is published from a draft PR. This does not solve
unavailability of a separately downloaded official foobar2000 host installer.

## Physical OS file ID sentinel guard (2026-10-21)

The native Win32 read-only host probe now rejects *either* a zero volume
serial **or** a zero file index, and rejects an all-ones index as an unknown
sentinel. Previously a nonzero volume serial could inadvertently qualify
an unsupported filesystem that reported `fileIndex=0` for every file.
That could collapse different audio sources into one apparent physical
identity. A new portable regression covers the exact sentinel combinations
and snapshot version guards. Ordinary foobar source/target access remains
read-only, while unusual unsupported/network file systems fail closed until
their identity can be proven. No changes to Precision CUE timing/writers.


## 2026-10-26 Studio-only action-aware native FILE relocation impact

The external **Inspect CUE** evidence now distinguishes explicit per-audio
**Move / Copy / Rename / No file action** decisions, without borrowing any
Precision CUE PR #52 timing/generation behavior. Source identity always comes
from independent host-observed FILE physical IDs and version guards. A Move
with *any unselected* CUE FILE source is blocked instead of proposing a
partial CUE rewrite; Copy reports its proposed RAW copy without changing the
original FILE reference; Rename cannot pass the still-absent actual
same-directory final FileOps destination gate; No action is a true no-FileOps
choice even if metadata review remains pending. The native pane reports
full-source counts and original-CUE consequences. Every row remains BLOCKED
for actual FileOps/CUE execution, with final host-sanitized destinations and
exact relative FILE postimages unqualified. Positive, mixed and negative
synthetic fixtures ensure the action intent cannot silently become an
audio move or CUE writer.


## Studio-only physical external CUE sidecar action preflight (2026-10-26)

An explicitly user-staged action for the physical **CUE carrier** itself
is now required before any RAW sidecar Move/Copy impact can be displayed.
The native default Move is never treated as sidecar consent. One CUE source
is bound to its complete independently probed FILE audio graph; a sidecar
Move requires every participant to Move, a Copy requires every participant
to Copy, and No action explains when a planned audio move could orphan the
original CUE. Mixed actions, partial FILE sets, Rename or unknown source
guards fail closed. The preview and static regression tests make no change
to the separate Precision CUE PR #52 timing/generation ownership, and grant
no final FileOps or CUE write authorization.

## Integration candidate – Draft PR #63 (26 October 2026)

The **separate, protected integration branch** `feature/studio-precision-cue-integration-20261026`
is rooted in a fixed Studio PR #45 commit. It imports the exact reviewed
Precision CUE generator core and regression sources from PR #52 *without*
changing either independent development PR or `main`. This is a **candidate**,
not a merge authorization for Studio or Precision CUE.

The Studio **Find & compare** tab owns one **Generate CUE** native action.
Its source list is copied from the immutable selected Studio input; every
source must have a current physical foobar snapshot, a real qualified physical
MP3 and subsong 0, and the selection must contain 1–8 files. The native
generator retains its OWN subsequent full-file SHA-256, album edition,
75fps boundary, disc mapping, fresh-target and separately confirmed create-only
gates. No Precision CUE context-menu factory remains and no second DLL is
built. The existing Studio read-only normalization, CUE inspection and
FileOps preview are not turned into an implicit writer.

Exact Studio+Precision linked builds and portable/noop regressions now run
together. On Windows the **real foobar host** must expose the new launcher
control ID 345, while an independent production-resource CTest verifies the
full CUE dialog's controls and that both create actions are disabled until
review. Public DLL/package/release artifacts remain disabled. CI synthetic
fixtures never contain private recordings or personal CUE metadata.

**Not certified by this integration:** real user's mix edition correctness,
sample-level audio boundaries, owner-installed visual acceptance, Deezer
third-party usage authorization, productive embedded CUE/physical tag mutation,
final Studio FileOps plan writes, or acceptance/merge of either original Draft
PR. The Studio and Precision branches may continue independently; changes
after the fixed import require an explicit conflict-aware qualification update.

## Integrated package permission reconciliation

**Precision CUE create-only was misrepresented by the inherited Studio
`analysis_only=true` / `external_cue_write_enabled=false` manifest.**
The integrated candidate now advertises its separately confirmed
new-sibling external `.cue` create ability explicitly even when Studio
physical-tag writes are disabled. This declaration does **not** activate
the separate Studio physical writer, embedded CUE mutation or FileOps
executor; genuine application-side source identity/approval/absent-target
gates remain mandatory.

## One protected live-Discogs qualification on the integrated candidate

The authenticated Discogs Search → Release → Edition → Compare runner now
accepts a **manual** `workflow_dispatch` on exactly the canonical Studio
worker or the isolated Studio/Precision CUE integration branch. It still
requires explicit `discogs_live=true`, the allowlisted public release ID,
the protected `discogs-qa` GitHub environment and step-scoped token
injection. Ordinary pushes, PRs, forks, public releases and package runners
cannot access that secret. This is **preparation only**: unless the
protected workflow has actually run and the exact commit's Win32/x64
native results are recorded, do NOT mark live Discogs acceptance passed.
