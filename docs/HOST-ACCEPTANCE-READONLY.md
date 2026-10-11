# First installed-foobar host acceptance — preview-only / no writes

**Gate:** the first real foobar2000 runtime smoke test. This is NOT writer
acceptance and does not authorize an Apply path. Every existing tag and File
Operations mutation route in this plugin remains disabled.

## What has already passed in Actions

- The immutable combined `.fb2k-component` candidate must come from the
  **exact successful main commit** of this test-gate change.
- The foobar CI verifies Win32 and x64 PE binaries, native SDK compilation,
  a static absence-of-writers audit and archive layout.
- The additional Windows `host-gate-script` job validates that the
  acceptance script does not alter its source MP3, that an untouched
  synthetic disposable sample passes, and that deliberate byte changes
  are detected. The synthetic file is a script test, NOT a foobar runtime
  sample.

These CI checks do not prove that the actual foobar host can load the DLL,
open the dialog, or preserve live files. The user performs only that
unavoidable installed-host gate.

## One short, safe Windows test

1. Download the successful run's `foo-music-metadata-studio-component`
   Actions artifact, extract it, and use `SHA256SUMS.txt` to verify the
   `.fb2k-component` integrity. Install it into foobar2000 2.x via
   Preferences → Components → Install, or an isolated/portable foobar
   profile. Record foobar version and x64/Win32 architecture.
2. Save `scripts/host-acceptance-noop.ps1` from the **same commit** locally.
   Run the following in PowerShell with the path to ONE ordinary MP3:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File "$env:USERPROFILE\Downloads\host-acceptance-noop.ps1" -Mode Prepare -SampleFile "Z:\Music\path\one-track.mp3"
   ```

   This creates a new unique folder under Windows Temp and uses
   `Copy-Item` to create `sample-a.mp3` and `sample-b.mp3`; a short
   external `sample-a.cue` references the first. The original MP3 is
   **read only**, is not tagged, moved or otherwise modified. All tests
   below use only the disposable copies.
3. Add the copied MP3 files to a temporary foobar playlist; optionally
   open `sample-a.cue` separately to inspect its virtual track. Invoke
   the Music Metadata Studio **Prepare Tracks (Preview)** context menu
   on the MP3 file(s). Switch between Metadata changes and File
   locations. Review the raw target / no-write statuses; optionally
   select one or two File locations rows, right-click and select
   **Inspect selected raw targets (read-only)**. Confirm the dialog
   opens without error and that virtual CUE subsongs are not treated
   as independent physical tag-write targets. Close/Cancel the dialog.
   **Do not run foobar's own ordinary tag writer, ReplayGain scan or File
   Operations on the test files** during this test.
4. Run verification using the exact `TEST_FOLDER` printed by Prepare:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File "$env:USERPROFILE\Downloads\host-acceptance-noop.ps1" -Mode Verify -TestFolder "C:\Users\...\AppData\Local\Temp\DJMetaHostGate-..."
   ```

   The script recomputes SHA-256, length, creation and modified timestamps
   for both copied MP3s and the CUE, compares against the baseline and
   prints `FILE_INTEGRITY=PASS` or detailed failure. It writes only
   `HOST-RESULT.txt` in the disposable folder; it never edits media.
5. Report only: foobar architecture/version, whether Prepare Tracks dialog
   appeared without error, whether selecting the physical and virtual
   tracks behaved as expected, and `HOST-RESULT.txt`. The report omits
   the original personal music path and metadata contents. Screenshots
   are optional only if the UI fails. Do not send personal MP3 files.

## Interpretation

- **PASS** means the tested preview dialog was actually usable and the
  disposable MP3/CUE bytes and both tracked timestamps remained unchanged.
- **FAIL** means no host acceptance; preserve the failing report and
  investigate exact component SHA plus foobar version without exposing
  private library files.
- **NOT YET TESTED:** productive tag writer-call counts, full physical
  ReplayGain/embedded-CUE preservation under writes, host File Operations
  timestamp preferences, copy/move collision behavior, Win32 if only x64
  was run, asynchronous throughput at 15,000/NAS files and final executable
  batch gating. A later, separately approved mutation candidate must
  receive one consolidated host acceptance with these checks.

The first gate is intentionally small: the entire project remains
GitHub-first and no persistent local source repository or local compiler
is required. The user may delete the Temp test directory afterward.
