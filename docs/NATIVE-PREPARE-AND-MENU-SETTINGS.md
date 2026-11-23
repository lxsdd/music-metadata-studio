# Native Prepare Tracks dialog and configurable menu captions

Status: **SOURCE IMPLEMENTED, WINDOWS COMPILATION AND HOST INTEGRATION PENDING**.

This feature branch extends the unmerged:
`feature/custom-menu-and-routing-overrides-20261008`
→ `feature/foobar-route-preview-20261008`
→ `feature/cue-readonly-inventory-20261008`
→ Batch Planner PR #6.

No file/tag writes have been enabled.

## User-facing behavior intended by this change

The new default-visible menu command is **Tracks vorbereiten (Vorschau)...**.
The previous metadata-only and three dedicated route preview commands retain
their original stable GUIDs and remain available, **off by default** under
foobar's native `File > Preferences > Display > Context Menu` visibility
editor. The plugin does not duplicate foobar's visibility or hierarchy UI.

### Main modal editor

A native Win32 modal dialog contains:

- a choice of the three original Singles / Alben / Livesets presets or
  `Benutzerdefiniert`;
- an editable profile name;
- an editable destination root;
- an editable **foobar Title Formatting** naming expression;
- `Vorschau` and `Abbrechen`.

Changing the chosen preset populates the three editable fields with the
literal historical values. The dialog never imports `overwrite=yes`,
`moveOtherFiles=yes` or other execution flags from that source fixture.

`Vorschau` evaluates the **current, one-time** choice with the existing
foobar `titleformat_compiler` and SAFE-only projected metadata.
`Abbrechen` discards the choice. The modal dialog intentionally does
not persist its edits to the user's profile. It is not yet the final
per-item grid.

A blank/invalid entry is rejected, and a malformed Title Formatting
expression is rejected downstream by the native host compiler.
No target is probed and no path/file actions are executed.

### Customizable menu names

A new foobar-native
`File > Preferences > Tools > Music Metadata Studio`
page has five independent caption edits:

1. metadata-only preview;
2. Singles preview;
3. Alben preview;
4. Livesets preview;
5. central Prepare Tracks command.

All five captions use stable foobar `cfg_string` GUIDs. The editor reads
live foobar-profile values, marks changed state on edit, and only commits
the complete validated set during foobar's `Apply`. `Cancel` destroys
staged edits; `Reset` stages centralized defaults and does **not**
write until Apply/OK. This avoids a parallel config file for host UI state.
All command GUIDs remain unchanged, including shortcuts.

Both dialogs declare native core Dark Mode hooks. Modal resize follows the
Windows DPI-change suggested bounds; the Preferences page reports
`dark_mode_supported`.

## Qualification gates

Portable C++ core route override, cue and SAFE-only staging tests were
previously executed locally. New Win32-only dialog/Preferences source
has **not** been compiled with Visual Studio and has **not** run inside
foobar2000 yet. Static source-contract assertions are not a replacement
for build or host integration testing.

### Remaining product work

- actual foobar-host path normalization/sanitizing and destination checks,
  performed across a full batch before any Apply;
- an interactive per-item, per-group editable grid with feedback as edits
  change targets;
- persistent user-defined routing profiles in foobar Preferences (only
  **menu captions** are persistent at this stage);
- external CUE association to live host resolved identities;
- native move/copy/tag executor after host policy and physical-vs-virtual
  metadata safety qualification;
- MSVC Win32/x64 compilation, smoke tests and packaging.

This branch deliberately contains no new GitHub Actions runs and is
not eligible for merging or release until build gates pass.
