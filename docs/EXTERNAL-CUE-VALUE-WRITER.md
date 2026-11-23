# Exact external-CUE metadata write path (private acceptance)

Status: implemented on the PR #45 feature branch for the opt-in
`EnablePhysicalWrites=true` Windows build. **The normal/shipping build
remains read-only. No merge, release or public component binary is authorized.**

## What the button does

1. In the modeless Candidate view, load an exact, independently verified
   MusicBrainz or Discogs release edition for an external `.cue`.
2. Select specific fields in the lower detail list, then use **Preview selected
   CUE values**. The original and proposed values remain visible in the grid.
3. If at least one selected field changes and the preview is eligible, the
   private acceptance build displays **Write CUE**. A final confirmation
   enumerates each exact old/new value (refuses overlong confirmations).
4. Only after **Yes**, the bridge re-reads the CUE bytes, re-probes its
   physical identity and every same-directory FILE reference, re-evaluates
   provider edition/track provenance, and compares the entire immutable
   batch-plan fingerprint with the reviewed snapshot.
5. The Windows sink holds the physical CUE source without write sharing,
   stages the exact changed UTF-8 CUE bytes on the same volume, verifies the
   staged content, performs one `ReplaceFileW` with recovery backup, and
   reads back the result. A byte-identical postimage performs **zero writes**.

The operation never modifies audio tags, embedded CUESHEET fields, FILE paths,
audio samples, or File Operations targets. The generic tag writer is not
called for an external CUE or a virtual subsong.

## Deliberately fail-closed cases

- Unverified or incomplete provider edition / ambiguous album or track match.
- Unselected fields, inherited ambiguous values, unsupported encoding,
  malformed CUE, or changed selected postimage.
- Missing, duplicate, aliased, nested, absolute, unsupported or changed
  FILE references. All participants must resolve to stable physical files.
- UNC / reparse / hardlinked / read-only CUE carriers, unsupported native
  paths, unsafe source identity, concurrent changes or stale workspace.
- Confirmation text too large to show every actual old/new value: select
  fewer fields and preview again.

A retained backup or failed final readback is **not** reported as verified
success. The UI disables re-use of the consent and requires reinspection.

## Evidence boundary

The portable exact-edition batch adapter and preflight have deterministic
regression tests. The native sink has isolated Windows filesystem transaction
tests. In [host qualification run 39962237786](https://github.com/lxsdd/music-metadata-studio/actions/runs/39962237786),
the private QA component was compiled and loaded into separate **real foobar2000
2.26.1 Win32 and x64 hosts**. Each host performed a disposable two-field
external CUE value transaction through the production controller and native
sink, then proved the exact UTF-8 postimage and unchanged referenced FLAC
bytes. The same run also proved an actual physical SDK tag no-op without a
timestamp change and an explicitly selected physical tag write with decoder
readback. Both architectures passed.

The synthetic host context commands are QA-only, source-path-locked to the
disposable GitHub runner, and absent from the normal component. This is
backend evidence, **not** an automated proof that a human can correctly
select, confirm, save and see readback through every UI state. One private
installed-foobar UI acceptance on disposable files remains mandatory before
general write enablement.

The opt-in local acceptance helper is `scripts/local-acceptance-build.ps1
-EnablePhysicalWrites`; its manifest must declare both
`physical_tags_write_enabled=true` and
`external_cue_write_enabled=true`. The default invocation declares both
false. This capability is limited to exact external CUE **value-only**
edits; external FILE-reference changes, embedded-CUE writes and file moves
remain outside this writer.
