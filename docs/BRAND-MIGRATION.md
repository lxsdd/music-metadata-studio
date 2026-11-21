# Music Metadata Studio — single-brand migration plan

**Status: Source implementation is staged in draft PR #45. The GitHub repository itself has NOT yet been renamed, and new Windows/host acceptance is pending.**

## Canonical identity
- User-visible product, foobar component name, context menu and Preferences: **Music Metadata Studio**
- GitHub destination: `lxsdd/music-metadata-studio` — **rename the existing repository in GitHub Settings**, never create a parallel repository.
- Component distribution: `foo_music_metadata_studio.dll` and `foo_music_metadata_studio-<version>.fb2k-component`
- Public Core C ABI and C++ namespace `djmeta`: **remain stable for DJ Library integration**; they are compatibility identifiers, not a product name.
- Stable foobar context-menu and Preferences GUIDs: **preserved**, so configured shortcuts/menu choices remain valid.

## Rollout safety gates
1. Run Core CI, Win32/x64 foobar build, static no-write audit, package layout and integrity tests for the exact commit.
2. **Never install old and new DLLs concurrently**. They share the same stable foobar service/context menu GUIDs; remove the older `foo_dj_metadata_normalizer.dll` first when a real isolated-host acceptance test is authorized.
3. Existing shared rules file `%APPDATA%\DJMetadataNormalizer\ruleset.json` is **not renamed in this patch**. DJ Library and foobar both read it; relocating it independently would split the shared rules. A future coordinated migration must prefer a single canonical file and support a safe legacy fallback, with fresh path-bound preview fingerprints. Do not create duplicate active rules files.
4. Current GitHub administrator action: **Settings → General → Repository name → music-metadata-studio → Rename**. GitHub retains existing history/PRs/issues and redirects repository links and git remotes. Review any external `uses: owner/old-name` GitHub Actions references manually because GitHub does not redirect action repository references.
5. After the rename, verify CI on the new repository path, doc URLs and all linked consumers, and update repository description and any remaining badges. Do not rename any other repository such as `dj-library` or `rekordbox-mytag-sync`.
6. No tag, cue, file copy/move, real metadata lookup or host writer capability is enabled by this rebranding.

## Compatibility note
A single user-visible brand does **not** imply that every historical SDK namespace, ruleset directory, commit message or README history can safely be renamed. Retain internal old identifiers only where breaking them could lose settings or corrupt dependent applications.
