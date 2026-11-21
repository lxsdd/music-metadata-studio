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

## Original mastergrid scope regression

The mandatory original-dialog QA now uses twelve synthetic captured identities and the real owner-data ListView to check two marked sources, descending and filtered projections, exclusion of hidden sources, empty and oversized selections, ambiguous mappings, duplicate physical IDs, missing guards, non-MP3, virtual subsongs, stale snapshots and non-Find views. Original WM_NOTIFY dispatch updates button eligibility. These are selection/identity regressions, not media decoding or export acceptance; no source files or writers are invoked. The prior optional conditional that could skip scope QA is removed. Exact-head native Win32/x64 CI remains required for this addition.

## Mastergrid rebuild and explicit CUE scope

On the scheduled successor to `d179d2aabd5cb16450343c47748184cc2d1a1a9a`, `update_master_table` retains the intersection of marked physical source identities and the new filtered view, but never synthesizes a new `LVIS_SELECTED` mark when that intersection is empty. A separate visible focus/detail target is permitted without authorizing Generate CUE. The button is recalculated after the actual owner-data rebuild, including the no-notification case. Original dialog QA uses synthetic physical MP3 identities and the production rebuild for complete filter exclusion, deliberate re-selection, partial intersection, descending sort, and empty refresh; no writer is called. Exact-head Win32/x64 qualification remains mandatory before treating this slice as complete.

## Studio-owned CUE lifetime (18 November 2026)

MMS-WF-01/02, MMS-UI-05/08 and issue #51: Precision CUE now uses the
actual Studio HWND as its modeless owner. Closing CUE keeps Studio usable;
closing Studio (including its asynchronous hide-before-network-completion
path), accepting Refresh, or detecting a stale snapshot retires the dependent
CUE preview. Declining Refresh preserves it. Forced owner destruction also
tears it down through Win32 ownership. Closing cancels the request revision
and requests worker stop while the existing pool retains worker lifetime.
No UI-thread network join, media operation, new writer or provider request is
introduced. Reopening starts with a fresh captured marked scope.

Original-foobar QA uses the production creation factory and original dialog
procedure to verify real owner identity, enabled Studio, CUE-only close,
reopen, stop request, wrong/null-owner isolation, forced owner teardown and
invalid-owner rejection. Static call-site checks additionally cover stale,
confirmed Refresh and Studio's early asynchronous close. Exact-head native
Win32/x64 qualification is required. This fixes dependent-window lifetime;
it is not the outstanding single-surface composition or final owner acceptance.

## Embedded Precision CUE page (18 November 2026)

The dependent popup is replaced by a DS_CONTROL / WS_CHILD page in the one
Studio top-level window. Generate CUE captures and revalidates the explicit
marked MP3 scope, then opens the Precision CUE tab. All four existing tabs
remain available. Tab switching and Back to Find preserve CUE inputs,
release review and worker state; Escape cancels a pending lookup before
returning to Find. Merely opening an empty CUE tab returns to Find with
instructions, without starting a provider request or inferring a source.
Studio's resize and DPI path positions the page below the tab strip. The
page uses native dark-mode hooks and nested dialog keyboard navigation;
Studio alone owns persisted top-level geometry. Hidden page tooltips close.
Confirmed refresh, stale snapshots and Studio teardown still discard the
child and cancel its request revision. Writer fences remain unchanged.

Original-host QA now checks WS_CHILD/DS_CONTROL delivery, one root HWND,
real tab dispatch through all existing pages, preserved text and HWND,
keyboard traversal into the page, Back navigation, growing/restoring bounds,
visible action bounds, and the actual Studio stale handler. These extend the
existing original CUE response/cancel/disabled-writer tests. Exact-head
Win32/x64 CI is mandatory. Visual multi-monitor/DPI and installed owner
acceptance remain separate; other register requirements are not declared done.
