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
