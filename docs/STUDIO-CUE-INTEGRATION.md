# Combined Studio and Precision CUE integration

Source baseline: Studio `5001f54bb70ab5a1ef4f45cb78fbf46385462abb`.
Read-only donor sources: integrated PR63 `cff4a1cf0f566c2e881c8c3b6c5a7a29c6cea9d9` and Precision CUE PR66 `f33dfbc020bd883a360ff8b6e3302f660337999e`.

| Requirement | Integration | Evidence / remaining gate |
| --- | --- | --- |
| Shared engine and Studio entry | Latest PR66 precision sources and complete synthetic suites, PR63 Studio Generate CUE wiring; no standalone context menu | Combined core suite; native Win32/x64 CI required |
| Preserve newer Studio behavior | Existing Studio files retained; only CUE wiring ported from PR63 | Original Studio suite retained |
| Query/provider invalidation | Original query dispatch exercised; provider switch now also refreshes File locations after dropping online route | Disposable original SDK dialog QA, both architectures |
| CUE cancel/stale/negative dispatch | Current response, rejected obsolete response, candidate change, cancel and direct disabled writer commands reach precision_dialog_proc | Disposable original SDK dialog QA |
| Write and provider limits | PR66 operation fence retained in UI and both writer handlers; PR63 Deezer deployment fence retained before request/reset | Core negative suites, original handler QA; no productive writes |
| Publication | Existing package/upload holds and protected API environment unchanged | Zero artifacts required |

Local Linux build and 70 CTest targets passed during preparation. This is not Windows or final product acceptance. The combined commit must pass its own six required jobs, including the new original-dialog checks inside Win32/x64 windows-build. Owner installed GUI/precision acceptance, productive writer activation, and full one-surface final UX remain open. No merge, release or final acceptance is asserted.

The Precision CUE preview is reached from the captured Studio batch and remains a modeless read-only child workflow. This ports the existing reviewed integration; it does not claim that the final one-window composition or every overall Studio requirement is finished.

CI source correction: required core/static/host/native jobs explicitly check out the PR head (or push SHA) and assert `git rev-parse HEAD` before testing. The first integration run passed against GitHub's synthetic merge tree, so it is not exact-head qualification. Only the new explicitly pinned run qualifies the product commit.

## Marked Precision CUE source scope (next source slice)

Generate CUE now resolves only selected visible Find mastergrid rows through immutable `track_view_order` source indices. It rejects empty, stale, virtual, duplicate, non-MP3 or unguarded selections, and caps the marked set at eight independently of batch size. Each selected physical identity is re-probed before the read-only Precision workspace opens. Button eligibility and tooltip use the same scope. The original dialog QA exercises the cleared-selection negative path; broad synthetic multi-row, sort/filter and writer-invocation evidence remains a qualification gate until exact-head CI and fixture expansion.
