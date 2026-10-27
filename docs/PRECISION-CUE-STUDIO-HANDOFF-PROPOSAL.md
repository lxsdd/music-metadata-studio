# Precision CUE → canonical Studio review handoff (source-only proposal)

Base: Precision CUE Draft #52 at \`2e3c0bb7de0e11d2dcbe56389f24e4eb7418330c\`.
Coordination: Studio Draft #45, Issues #46/#50/#51, central source/publication guards.

## Narrow integration seam

\`include/djmeta/precision_cue_studio_handoff.h\` produces immutable-by-value,
**read-only review rows** from the already existing Deezer/Apple match, per-disc
integer-ms/75fps plan, physical selection and optional external CUE reference.
The caller must obtain foobar source identities independently and supply a
read-only physical-file probe for **subsong 0 only**. The adapter recomputes
release selection, mapping and canonical CUE plans rather than trusting mutable
GUI state. A different release, track, duration, file, mapping, CUE text or
virtual subsong yields **no rows**. It never returns a writer approval token.

Every row carries the exact disc/track, physical selection index, source
basename, Apple edition ID, track identity, integer-ms boundary, CUE frame and
optional signed reference delta. Reference equality is explicitly
\`ExactFrameNotAudioProof\`; 1-frame agreement is also not mastering proof.
Unknown/unsupported reference CUE bytes never become write evidence.

The adapter is a **proposal for consumption by #45's existing single modeless
workspace**, not a second permanent window, context-menu command, HTTP engine
or write controller. Do not transplant #52's experimental window/resource IDs
or independent create controller into #45. Keep #45's selected-handle lifecycle,
source/target review, CUE physical/virtual inventory, and all file-plan guards.
The external create-only backend is a separately confirmed operation and is
NOT invoked by this adapter. Embedded CUE, tags, ReplayGain and FileOps remain
disabled.

## Conflict ownership

#45 owns \`src/foobar/batch_preview_dialog.cpp\`, native controls/resources,
\`src/foobar/component.rc\`, \`src/foobar/resource.h\`, shared workflow files,
and its single workspace. #52 owns the pure \`precision_cue_*\` provider,
matching, timing, audit and safe create-only contracts. Resolve the overlapping
CMake/RC/native-test/workflow paths by explicit source review on an isolated
integration branch; do not blind-merge/rebase either draft. The current
proposal changes only a new core header and #52's existing synthetic E2E test.

## Qualification boundary

Offline two-disc/30-track synthetic tests cover row provenance, both disc
zero points, exact/one-frame/invalid reference classification, stale plans,
altered edition/duration, remapped/duplicate/incomplete discs, invalid source
basenames, missing physical probe and virtual subsong rejection. CI evidence
must be checked against the exact new proposal SHA. These tests do NOT
establish installed foobar GUI, real provider access rights, audio mastering
identity, source-distribution permission or productive-write authorization.

## Per-track reference fidelity (2026-10-27)

The read-only handoff now distinguishes **per-track** INDEX 01 evidence from
**disc-wide** structural CUE warnings. A 30-track comparison with one
one-frame discrepancy labels 29 individual rows exact-frame and only the
changed row within-one-frame; two-or-more-frame errors require review on the
specific row. Any INDEX 00, PREGAP or POSTGAP keeps the entire reference disc
in review, even when all INDEX 01 markers agree. Invalid reference syntax
remains unqualified. The row also exposes signed integer-microsecond 75-fps
quantization error and the structural-gap flag; neither is audio evidence.
The entire Studio handoff remains read-only with no create/write token.

## High-resolution sub-frame duration conflicts (2026-10-27)

Independent high-resolution catalog durations can differ by 1 ms yet round to
identical 75-fps CUE frames. The consensus engine now keeps those discrepancies
as REVIEW, including the last track of each disc (which has no following INDEX
01) and compensated +1/-1 ms pairs that cancel before the next frame boundary.
The optional Studio row exposes `same_frame_duration_disagreement` alongside
`disc_tail_duration_disagreement` and the signed millisecond deltas; coarse
Deezer seconds and approximate Amazon-device milliseconds are never upgraded
to frame-resolution evidence. These warnings are **catalog disagreements**, not
proof of audible drift, mastering identity or sample-perfect positions.
No native writer, external CUE publication, second foobar workspace, or provider
network access is enabled by this change.
