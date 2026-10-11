# Music Metadata Studio

Shared, deterministic metadata-normalization engine for foobar2000 and DJ Library.

## Scope

This repository is deliberately separate from `rekordbox-mytag-sync`. It owns the normalization rule model, preview engine, deterministic tests, and the future foobar2000 normalization component. It does **not** own Rekordbox MyTag synchronization.

## Product contract

**One engine – two surfaces – one rule set.**

- **Core:** immutable metadata input -> ordered normalization proposals.
- **foobar2000 adapter:** selection/context-menu/preview and, only after explicit approval, SDK-based tag writes.
- **DJ Library adapter:** analysis/review over the same core and rules.
- **Bridge:** `foo_dj_library_bridge` remains a read-only snapshot producer.

Analysis never writes metadata. Original values and proposed canonical values remain separate.

## Safety classes

- `SAFE`: deterministic, non-semantic cleanup that may later be eligible for a "safe only" apply action.
- `CONFIDENT`: strongly supported canonicalization that still deserves user visibility.
- `REVIEW`: ambiguous or semantic change requiring an explicit decision.

The initial development phase implements only analysis/preview behavior. Real tag writing is intentionally out of scope until the rule engine, trace output, stale-input protection and deterministic tests are qualified.

## Repository layout

- `include/djmeta/` – public core API.
- `src/core/` – normalization engine.
- `rules/` – versioned canonical rule set and schema.
- `tests/` – deterministic golden/unit tests.
- `docs/` – architecture and integration contracts.
- `.github/workflows/` – reproducible CI.

## Current phase

Phase 0/1: architecture contract + deterministic preview core. No audio-file or tag writes.

Modeless foobar workspace and its mandatory installed-host gate: [read-only acceptance checklist](docs/MODELESS-WORKSPACE-ACCEPTANCE.md).
