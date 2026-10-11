# Music Metadata Studio — modeless workspace host gate

**Status:** source candidate on PR #45, issue #47. Not a completed installed-foobar acceptance. This page records the conditions for the first necessary Windows test. The component is read-only; no tag, audio, CUE or filesystem write is authorized.

## Contract under test

- One independent top-level native foobar dialog, created with `CreateDialogParamW` and registered with `modeless_dialog_manager`. It is not a modal foobar-owned popup and must not pin itself above foobar.
- A second Prepare Tracks invocation activates the current workspace without discarding candidate rows, routing previews or field decisions. A **different** selection leaves the existing snapshot intact with an explicit warning; close and reopen to choose another selection.
- Window-owned state is freed at `WM_NCDESTROY`, the modeless registration is paired, a failed initialization destroys the HWND, and `initquit::on_quit` closes the workspace.
- Each user action checks rules and underlying metadata/source identity; returning to the window checks all selected source snapshots. A changed source disables decision, import, search and preview actions. **Refresh snapshot** is the only way to accept a new basis: it requires explicit confirmation and discards all earlier decisions, external candidates and online results. There are no background writes or network requests.
- Saved normal window placement/maximized state remains in the original profile-scoped foobar cfg keys, including when the workspace is closed while minimized.

## CI tests (not a replacement for host acceptance)

Verify the **same exact SHA** through Windows/Linux core CTest, production resource-backed Windows HWND tests, no-write static audit, Win32/x64 component build, SDK linkage and PE/package/manifest checks. The Windows test must assert real minimizable/maximizable styles, absence of forced owner, independent taskbar identity, non-overlapping refresh/status/action controls, Tab handling through `IsDialogMessage`, minimize/restore/maximize and HWND destruction. Compile success cannot prove real foobar message-pump navigation or shutdown order.

## Manual host test — disposable music files only

Use `scripts/host-acceptance-noop.ps1 -Mode Prepare` from the **same SHA** as the successful Action's `foo-music-metadata-studio-component` artifact. Follow `docs/HOST-ACCEPTANCE-READONLY.md` to produce disposable copies of one MP3 and an external CUE; never test on irreplaceable originals. Before the test record the bundle's `BUILD-MANIFEST.txt` commit and SHA256 digest.

1. Install the combined `.fb2k-component` matching your foobar architecture and restart foobar. Open **Prepare Tracks** for disposable track(s).
2. Keep Studio open. Use foobar normally: play/pause, browse a different playlist, open Properties on another file, switch back. Studio must not block these actions or stay permanently above foobar. Check taskbar, Alt-Tab, minimize/restore and maximize. Check optical alignment of Find MB and Selected changes, complete Original/Suggested/Status detail cells and release double-click/Enter.
3. Invoke Prepare Tracks again with the **same** selection: one existing Studio is focused, decisions intact. Invoke it with another selection: one window remains; warning tells you close/reopen rather than silently replacing pending decisions. Test Tab, Shift+Tab, Esc/Close and normal foobar shortcuts.
4. Edit a **disposable copy's** tag in foobar with Studio open; re-activate Studio. Read-only actions must refuse the stale snapshot. Click **Refresh snapshot**, confirm its discard warning, verify new metadata appears and old online candidates/decisions vanish. Repeat with a new CUE reference or changed rules only if using disposable copies/configuration, and verify source change blocks stale work.
5. Resize and reposition, minimize, then close using both Close and the titlebar X; reopen and verify restored placement. Maximize, close, reopen and verify maximized restoration. Repeat after foobar restart, including repeated open/close loops and shutting down foobar while Studio remains open.
6. Complete the integrity script's `-Mode Verify` step. Source copies, CUE text and timestamps must remain unmodified by Studio. Ensure no writer path ran. The MusicBrainz search is optional and must be manually clicked; no request may fire on restore, refresh or foobar playback/playlist changes.

**Pass gate:** record foobar version, architecture, Windows DPI/theme, tested commit, exact component artifact and package SHA256, script integrity result and any screenshots/errors. Leave PR #45 Draft and writer routes disabled regardless of runtime result until separately qualified.
