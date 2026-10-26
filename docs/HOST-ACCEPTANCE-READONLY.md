# Music Metadata Studio — final installed-foobar acceptance gate

This document describes the **remaining gate**, not an instruction to test a
development snapshot now. Draft PR #45 stays open, `main` stays unchanged and
productive physical-tag, CUE and FileOps writers stay disabled unless
separately qualified and explicitly approved. Earlier preliminary host-smoke
instructions are historical and must not be used as a substitute for the
single consolidated final acceptance in Issue #50 and
`docs/REQUIREMENTS-REGISTER.md`.

## Before asking for the owner's Windows action

The development branch must have one exact 40-character HEAD with completed
Core and foobar Win32/x64 checks, no known build/test failure and documented
limits for every provider and writer path. A regular OAuth health check is
not an authenticated Discogs Search → Release → Edition → Compare test.
Protected live QA must be reported separately; a skipped protected job is
not PASS. CI and SDK host smoke are not installed-foobar visual acceptance.

The acceptance build is **private and local** using
`scripts/local-acceptance-build.ps1` at that exact SHA. Follow
`docs/LOCAL_ACCEPTANCE_BUILD.md` only after the candidate is qualified.
No public Actions artifact, release, tag or binary distribution is an
acceptance prerequisite. The package manifest and `SHA256SUMS.txt` must
identify the same source SHA and exact `.fb2k-component` bytes. A package
hash from an earlier commit is invalid after any source change.

## One consolidated disposable-media session

Only when the software-side gates are complete, provide the owner one short
integrated script for foobar2000 2.x Win32 and x64, including:

- installation/load and the single modeless workspace alongside normal
  playback and playlist use; close/reopen, minimize/maximize, Alt-Tab and
  restart placement/column/sort persistence;
- 100/125/150/200% DPI and light/dark visual inspection of search, scope
  actions, review groups, pane split, tooltips and keyboard/double-click;
- explicit MusicBrainz and authorized Discogs read-only Search → Release
  → Edition → field/CUE comparison, with source identity and conflict states;
- full physical/custom/multivalue original-tag inventory, normalization
  Accept/Reject/Reset/manual preview, selected-track and batch scope, and
  a safe file plan with target/collision/no-op and final confirmation;
- disposable MP3/FLAC and external/embedded CUE cases, including virtual
  subsongs that never become generic physical write targets;
- before/after SHA-256, length and timestamps for each disposable source,
  plus an outcome report that omits personal paths, metadata and secrets.

`scripts/host-acceptance-noop.ps1` can prepare and verify disposable copies
as an integrity helper, but its green result alone does not prove the above
UI, provider or writer matrix. The ordinary preview build must not perform
productive tag/CUE/FileOps writes. Any separately qualified write-acceptance
variant must be identified explicitly and limited to disposable fixtures with
fresh source revalidation and an individual final click; no blanket consent
or automatic write is inferred.

The owner should never paste an API token into chat, GitHub, logs or an
acceptance report. If a protected live run cannot be initiated through the
available integration, ask only for its protected manual workflow start when
all independent work is finished. A successful installed session is the
human HOST gate, not permission to merge, tag, release or activate production
writers; those decisions remain explicit.

## 2026-10-26 — Real native physical/CUE writer QA on trusted CI pull requests

The previous `physical-sdk-runtime-qa` job was incorrectly restricted to
`push` events, while the actual Studio code checks appeared only as
`pull_request` runs; a green foobar Preview workflow therefore did **not**
exercise its real SDK writer/no-op tests. The workflow now permits the
**same-repository Studio branch → main PR only**, in addition to the existing
branch-push trigger; forks and unrelated PRs remain ineligible. The QA
component is built only on disposable Windows GitHub runners and tested
against runner-generated silent FLAC plus an external CUE inside a fresh
`RUNNER_TEMP` subtree. The runner rejects a pre-existing foobar process,
restricts the component to exact fixture paths, checks real SDK write/readback,
first and subsequent no-op bytes/timestamps and exact CUE-value postimages,
then deletes disposable fixtures and QA DLLs. Neither normal Studio builds
nor owner files gain writer access; no DLL is packaged, uploaded or retained.
This qualification is separate from final foobar FileOps target resolution,
production-writer activation and installed owner GUI acceptance. The new
negative static workflow contract prevents accidental fork eligibility,
secret use, architecture loss and upload enablement.

## Protected native Discogs PR execution failure (2026-10-26)

At source SHA `58c7e01606b475e748b8b8fca7fab31b288f5176`, both
experimental protected PR Win32/x64 Discogs jobs failed before receiving
any runner steps. Therefore **no** native authenticated Search → Edition →
Compare was executed. The protected environment's policy must not be bypassed
by weakening secret, deployment or permission controls. The automatic PR
eligibility has been withdrawn; the existing explicit `workflow_dispatch`
path on the canonical Studio branch retains its original environment and
credentials. Green ordinary Core/host/API-health results are not a substitute
for this separate native protected gate.

## Environment rule corrected and automatic protected Discogs check restored (2026-10-26)

The initial automatic internal PR #45 native Discogs Win32/x64 QA was
rejected before runner startup by the `discogs-qa` environment deployment
branch pattern. The owner subsequently approved the **exact PR ref only**,
`refs/pull/45/merge`; a retried run reached real Windows host runners.
That retry was cancelled when a concurrent Studio CUE commit superseded its
head. The temporary manual-only rollback is therefore no longer the right
state: the original **exact-PR #45 / same-repo / main-base / protected
`discogs-qa` / step-local-secret-only** automation is restored with
negative static tests. No published binaries or production writes.

A successful workflow must still show actual protected host-probe output for
both architectures; job eligibility alone is not native API acceptance.

## Exact upstream native host action contract found (2026-10-26)

On the protected internal PR #45 exact SHA `419075e88f7ca8617b3a3d4a0b8aad4e669c2b34`,
the **real official authorized staged Discogs search** for public release
`37332711` passed (`FULL_ORIGINAL_ARTIST → AUTHORIZED_SEARCH → EXACT_EDITION →
TRACKLIST`). Both protected native foobar host steps nevertheless failed at
the pinned shared `foobar-host-smoke/host-smoke.ps1:207` preflight, **before
the native authenticated UI probe**. That shared action explicitly requires
`GITHUB_EVENT_NAME=workflow_dispatch` and the feature-branch ref. A PR event
cannot meet that invariant. The shared action belongs to the separately
owned infrastructure repository; Studio does not spoof GitHub event variables
or change that dependency's security policy.

Until the upstream action is separately qualified for the exact protected
internal PR, Studio PR jobs perform **only** the authenticated official API
staged search and display `MMS_PROTECTED_NATIVE_HOST=SKIPPED_PINNED_ACTION_MANUAL_ONLY`.
The real native host step remains executable on the existing protected manual
`workflow_dispatch` path. This is a known external qualification gap, **not**
a successful native Search → Edition → Compare or installed owner acceptance.
