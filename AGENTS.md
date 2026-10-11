# Repository operating rules

This repository is the canonical source of truth for Music Metadata Studio.

## Scope isolation

- Keep this project fully separate from `rekordbox-mytag-sync`: no shared branches, issues, release lifecycle or write path.
- `foo_dj_library_bridge` remains a read-only integration/data source.
- `dj-library` remains a separate product repository and must consume the shared normalizer contract rather than reimplement normalization rules.

## Safety contract

- Analysis and writes are different operations.
- The core is pure analysis: immutable metadata in, preview proposals out.
- Original values and canonical preview values must remain separately observable.
- No background writes.
- Only an explicit foobar2000 UI action may later invoke a write adapter.
- A writer must reject stale previews if the input metadata or ruleset revision changed.
- New semantic rules start as REVIEW unless specifically qualified otherwise.
- SAFE is reserved for transformations proven deterministic and non-semantic.

## Shared UI/UX and rules across products

- Apply `lxsdd/dev-infrastructure/STANDARDS/SHARED-DESKTOP-UX.md` (v1)
  and `STANDARDS/UI-RUNTIME-QUALIFICATION.md` to every foobar window.
  Preserve English-only built-in foobar UI, source/target identity, proper
  DPI/baselines and native profile layout persistence.
- Primary metadata review uses **one row per selected physical track**,
  with a detail surface showing original/proposed values, rule IDs and
  explicit SAFE/CONFIDENT/REVIEW status. Discogs/freeform text remains
  accessible under Extended/All, never erased by a display filter.
- Share ONE versioned normalizer core and ruleset with DJ Library via
  its existing native ABI. Do not push a new ruleset revision without
  consumer compatibility checks against DJ Library and the read-only
  bridge schema; neither other components' private editing policies nor
  unrelated BPM/scanner heuristics may be silently rewritten.
- Native Windows/foobar and WPF controls may differ in implementation;
  user-facing sorting, full-row selection, contextual detail, tooltips,
  persistence, form alignment and confirmation scopes must agree.
- Keep every functional preview read-only until physical/virtual, CUE,
  target collision and stale-snapshot authorization gates pass.

## Engineering

- C++20 core, deterministic behavior on Win32/x64 and CI hosts.
- GitHub Actions is the build authority.
- Tests must cover rule ordering, multivalue preservation, safety classification, immutable input behavior and fingerprints.
- Do not add a second snapshot/export channel when the existing Bridge snapshot can be extended compatibly.
- Any change to the Bridge must preserve exact `(path, subsong)` identity, profile-local behavior and read-only audio/tag semantics.

## Development order

1. core model + preview engine;
2. versioned ruleset contract + deterministic tests;
3. Bridge metadata-vector compatibility, only as required;
4. foobar preview UI;
5. DJ Library review integration using the same engine/rules;
6. foobar write adapter only after stale-preview and regression gates are qualified.

## Mandatory zero-write contract

For this and all other maintained foobar components, **do not write any
physical file whose approved postimage is already present**. Compare full
physical textual metadata with multivalue preservation, ReplayGain
separately, external CUE as exact bytes, embedded CUE only through its safe
editor, and every sidecar/file operation independently. Treat uncertain
equality as REVIEW and stale source evidence as BLOCK. Re-check immediately
before future mutation. Code reviews and tests must prove zero writer calls
for no-op operations. See `docs/UNIFIED-WORKFLOW.md` and the canonical
cross-repository no-op policy in `dev-infrastructure`. Productive writes
remain disabled until real-host acceptance.
