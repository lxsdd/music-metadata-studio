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

## Automatic protected native Discogs QA on the canonical Studio PR

The existing `discogs-authorized-native` Win32/x64 real-foobar read-only
Search → exact edition → track comparison is now eligible **automatically
only** on same-repository Draft PR #45 from
`feature/online-metadata-discovery-foundation-20261009` to `main`.
Static, offline host and both normal Win32/x64 host-build checks must
succeed first. The protected job checks out the actual PR HEAD SHA, not
GitHub's temporary merge SHA. The bounded automatic test uses public
Discogs release `37332711` for exact collaboration/artist/version discovery.
The existing manually dispatched selection of other regression release IDs
remains supported.

The existing `discogs-qa` GitHub environment exclusively supplies its
previously provisioned `MMS_DISCOGS_QA_TOKEN` to the two native test steps.
No fork or arbitrary PR receives it, no credential reaches public logs or
artifacts, and all ordinary static/host/build/package steps remain secret
free. This is a **Studio token-authenticated test**, distinct from the
already scheduled **central OAuth1 API health**; central OAuth credentials
are never copied into the Studio repository. GitHub environment protection
rules may still require approval before the protected job starts. These
automated runner tests do not certify installed owner GUI acceptance or
authorize production tag/CUE/FileOps writes, merges or releases.
