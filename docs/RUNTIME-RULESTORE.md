# Runtime Rule Store

## Canonical location

The shared runtime ruleset is:

`%APPDATA%\DJMetadataNormalizer\ruleset.json`

Both the foobar2000 component and DJ Library must resolve this same per-user location. They must not maintain independent private copies with different semantics.

## Current phase: read-only consumption

The current foobar preview component:

1. checks the canonical shared path;
2. opens an existing file read-only;
3. parses it with the normalizer core's strict versioned rules loader (schema v1 or v2);
4. displays the exact ruleset revision in preview;
5. falls back to the repository's embedded `rules/default-rules.json` only when no shared file exists.

Analysis does not create or modify the runtime ruleset.

The embedded fallback makes a newly installed preview component usable before a rule-management surface exists, while preserving one versioned schema family and one engine.

## Future rule-management writes

A future Rule Manager may update the shared runtime ruleset only as an explicit administrative action. That write path is separate from track analysis and separate from audio/tag writing.

Requirements for rule-store updates:

- validate the complete new ruleset with the shared core before replacing the active file;
- write a temporary sibling file;
- flush/close it;
- atomically replace the active ruleset where the platform permits;
- retain or expose the previous revision for recovery;
- never mutate a ruleset merely because a track was analyzed;
- new rules derived from user corrections begin disabled or REVIEW until impact analysis is accepted.

## Revision binding

Each analysis result carries the active `ruleset_revision`. A future tag-write approval is valid only if both:

- the current metadata fingerprint still matches the preview input; and
- the active ruleset revision still matches the preview.

Any mismatch requires a fresh analysis.

## Distribution authority

The canonical schema and defaults belong to **Music Metadata Studio**, currently hosted at `lxsdd/dj-metadata-normalizer` pending the GitHub admin rename to `lxsdd/music-metadata-studio`.

DJ Library must call the shared engine/native ABI instead of translating the rules into C#.

## Schema compatibility

The runtime loader remains backward compatible with qualified schema-v1 rule files. The embedded default advances independently to the newest qualified schema. A newer transform is never silently accepted under an older schema version.
