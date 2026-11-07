# Architecture

## Product boundary

Music Metadata Studio is a separate product from Rekordbox MyTag Sync.

The architecture is deliberately split into four layers:

1. **normalizer core** — pure deterministic analysis;
2. **foobar adapter** — selection, preview UI and later explicit SDK-based writes;
3. **DJ Library adapter** — review and rule management over the same core;
4. **Bridge integration** — read-only metadata snapshots from `foo_dj_library_bridge`.

There is one normalization implementation and one versioned rule model.

## Core contract

Input is an immutable ordered metadata document. Each field retains its original name and its vector of values; multivalue metadata is never flattened by the core.

Output contains:

- SHA-256 input fingerprint;
- ruleset revision;
- an ordered low-level per-rule change trace;
- aggregated user-facing final proposals;
- exact field index + field name + multivalue index;
- raw original value;
- value immediately before each rule;
- final proposed value;
- ordered rule IDs and rationales;
- aggregated SAFE / CONFIDENT / REVIEW classification;
- complete canonical preview document.

The separate field index is intentional: duplicate same-named metadata entries remain structurally distinguishable.

Rules execute by ascending priority and then stable rule ID. A later rule sees the preview result of earlier rules, while every trace entry retains the raw original value.

Analysis performs no I/O and cannot write tags.

## Safety model

### SAFE

Reserved for transformations demonstrated to be deterministic and non-semantic. The qualified allow-list is intentionally small: Unicode White_Space mapping to ASCII space, leading/trailing ASCII whitespace removal, and repeated ASCII whitespace collapse.

### CONFIDENT

Strong canonicalization with domain meaning. It remains visible and selected only according to the future review UI policy.

### REVIEW

Ambiguous, semantic, newly learned or not-yet-qualified transformations. New rules derived from user corrections begin here.

The rules validator fails closed if a generic semantic replacement rule is marked SAFE.

## Stale-preview contract

Every analysis is bound to both:

- `input_fingerprint` — SHA-256 over a length-prefixed exact metadata representation;
- `ruleset_revision`.

Before a future write, the foobar adapter must re-read the selected item's metadata and re-evaluate both conditions. A mismatch invalidates the preview and requires re-analysis. The writer must never treat a stale preview as authority.

`(path, subsong)` identifies the track; it does not replace the metadata fingerprint.

## Rule model and schema evolution

The persisted representation is explicitly versioned.

- `rules/schema-v1.json` remains supported for existing rule files.
- `rules/schema-v2.json` adds named primitives without changing v1 semantics.
- `rules/default-rules.json` uses the newest qualified schema.

v1 supports:

- match always;
- match exact value;
- trim ASCII whitespace;
- collapse ASCII whitespace;
- replace with a canonical value.

v2 adds:

- normalize Unicode White_Space code points to ASCII space.

The v2 transform deliberately preserves every non-whitespace UTF-8 byte and leaves malformed UTF-8 byte sequences untouched rather than guessing. Existing trim/collapse rules then operate deterministically on the mapped ASCII spaces.

The schema continues to evolve through named, testable primitives. Artist collaboration parsing, title/version extraction, genre logic, aliases, separators, case handling and cross-field rules are not hidden inside an opaque scripting language.

The C++ engine owns the strict v1/v2 JSON loader. A schema-v1 document using a v2-only transform is rejected. Adapters do not implement their own rule parser.

## Shared-engine integration

The foobar component links the C++ core directly.

DJ Library must not port rules into C#. A versioned analysis-only C ABI is implemented in this repository for C#/P/Invoke integration. It accepts UTF-8 Bridge-v3 `metadata_vectors_json` plus the canonical ruleset JSON and returns deterministic preview JSON. ABI-owned strings have an explicit release function and exceptions never cross the ABI.

The canonical active runtime rules path is `%APPDATA%\DJMetadataNormalizer\ruleset.json`; the foobar preview opens it read-only and falls back to the embedded versioned default ruleset when absent.

This keeps behavior identical across both surfaces while avoiding C++ ABI coupling to .NET.

## Write boundary

The future foobar writer is an adapter, not part of the core. It will use supported foobar2000 SDK metadata update APIs and mutate only explicitly approved fields.

No direct audio-file editing is permitted in DJ Library or in the Bridge.

The first foobar integration milestone is deliberately analysis-only. A static CI audit rejects write-capable SDK/file tokens before Win32/x64 component builds run.

Virtual subsongs require a separate qualification gate because writes must not erase physical metadata that is absent from a projected subsong view.

## Unified prepare-track workflow

The approved end-state extends planning beyond tags to filename and destination. See `docs/UNIFIED-WORKFLOW.md`. The shared engine computes desired state; the foobar adapter inherits host policy for configuration, title formatting, tag writes and file operations wherever supported by the SDK.

## Structural metadata protection (core safety hotfix)

The default ruleset remains schema v2, revision `2026-10-07.2`, and the shared
C ABI remains v1. Its three wildcard SAFE whitespace rules continue to normalize
single-line scalar tags. The engine now **always excludes** embedded `CUESHEET`,
`CUE_SHEET` and `__CUESHEET` values from generic proposals; only dedicated
CUE tools may modify embedded cue records. Generic whitespace transformations
are also ineligible for lyrics fields and values containing CR/LF, NEL, U+2028
or U+2029 line boundaries. This prevents flattening structured/multiline
metadata into a single line.

Explicit `replace_with` rules for lyrics remain possible as `REVIEW`, not as
implicit SAFE normalization. The CUE exclusion applies even to explicit
generic rules. The input document, fields, value cardinality, and original
bytes remain untouched.

Shared-consumer compatibility: DJ Library invokes `djmeta_analyze_json_v1`
and consumes schema-v1 analysis JSON; this change does not alter either
interface, the rule file, or the TITLE self-test revision. Normalizer unit
tests and a native ABI test include structural examples. A full DJ Library
WPF runtime test remains independently blocked by its private Windows CI
runner and is **not** claimed PASS.

## Official foobar2000 SDK qualification (2026-10-08)

The foobar Win32 and x64 CI builds download the official SDK dated 2026-10-01,
checking the exact archive size (793,738 bytes) and SHA-256
d4c55077336fae81bf8df0259b5b2748fa45ea84132c656ead93eb123cbcdc26
from the verified lxsdd/foobar2000_component_template SDK-PIN.json.
The bootstrap refuses changed bytes, bad 7z archives, invalid SDK directory
layout and unexpected sdk-readme version before extracting into the active SDK.
Existing local 2026-09-17 SDK workspaces are upgraded, not silently reused.
No upstream SDK source/binaries are committed to this repository.

Successful CI establishes build compatibility, not installed-foobar runtime
acceptance; physical/virtual tag and foobar File Operations host gates remain
tracked in issue #33, with all productive writers still disabled.
