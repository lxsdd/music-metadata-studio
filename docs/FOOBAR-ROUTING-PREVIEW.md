# Host-native route preview — first integrated foobar slice

Status: **SOURCE IMPLEMENTED / LOCAL PORTABLE TESTS PASS / WINDOWS BUILD PENDING**

This branch extends `feature/cue-readonly-inventory-20261008`, which in
turn extends unmerged Batch Planner PR #6. It is **not independent of
these unmerged parent branches**.

## Existing foobar integration

The native context menu now groups four preview-only commands under
`Music Metadata Studio`:

1. `Metadaten normalisieren (Vorschau)...`
2. `Vorbereiten: Singles (Vorschau)...`
3. `Vorbereiten: Alben (Vorschau)...`
4. `Vorbereiten: Livesets (Vorschau)...`

These commands operate only on the current explicit selection using the
existing `metadb_io_v2::load_info_async` service.

Each of the three routing commands **manually selects** a historical
foobar File Operations profile. This is a deliberately limited preview
affordance, *not* automatic routing classification, and does not provide
yet the final per-item editable plan grid or custom naming templates.

## Exact historical routing templates

The three supplied move-preset expressions are retained literally in
`src/foobar/legacy_routing_profiles.h` and guarded by a separate
host-independent C++20 regression test:

- Singles: `Z:\Music\Singles` +
  `%album artist%/%album%/%artist% - %title%`
- Alben: `Z:\Music\Alben` +
  `%album artist%/%album%[ '('%date%')']/%tracknumber%. %artist% - %title%`
- Livesets: `Z:\Music\Livesets` +
  `%artist%\%artist% - %album%`

They are reference data, not automatically enabled move/overwrite
policies and not a commitment that these paths are always correct.
The final editable routing profiles belong in foobar-native configuration.

## SAFE-only input, no semantic leakage

`djmeta::stage_safe_only()` receives an exact raw metadata input and its
normalizer analysis. It verifies the input fingerprint before staging.

The staged version receives only proposals whose **aggregated safety
classification** is SAFE. All CONFIDENT/REVIEW proposals — including
chained SAFE+REVIEW changes in a single field — remain unselected.
The raw input is never modified.

In particular, artist transliteration and remix interpretation in old
Masstagger rules are not silently used to name files. Four new C++20
test suites verify the restriction, independent `DATE`/`DATE_RAW`,
multivalue/duplicate-field preservation, and stale/invalid-proposal
rejection.

The native route renderer sends this staged metadata to foobar's
`titleformat_compiler` against an **in-memory** `file_info`, not a
bespoke parser.

## Early preview safeguards

- exact source metadata snapshots are rechecked after complete analysis;
- selected virtual subsongs and repeated source paths are not used as
  authoritative physical file operations;
- a maximum of 35 entries are expanded in the initial popup;
- empty evaluated filename/path expressions are flagged;
- duplicate **byte-identical raw destination strings** are flagged;
- no destination is probed, written to, copied, moved, overwritten, or
  renamed.

This initial popup is NOT a final unified file plan. In particular,
host File Operations filename sanitizing, absolute path/alias equivalence,
case folding, existing destinations, companion discovery, cue dependencies,
per-item manual overrides and timestamps are not qualified yet.
Distinct raw strings may collide at the actual filesystem layer.

## Qualification and costs

- C++20 route-profile test passed under Linux g++ `-Wall -Wextra
  -Wpedantic -Werror` without Actions.
- Isolated staging source compiled and exercised locally with a
  *test-only* fingerprint stub. This is not equivalent to linking the
  full normalizer core.
- Native foobar Win32/x64 compilation and actual host behavior **remain
  untested** because GitHub Actions jobs have recently failed before
  assigning a runner. No CI result is claimed.
- Do not merge, release, or enable writing until full future gates pass.
