# Official-provider nonblocking read-only network boundary

The native modeless foobar2000 workspace uses one bounded, explicit HTTPS
request per user activation for both **Discogs** (authorized personal token
in a temporary password control) and **MusicBrainz** (public read-only API).
Provider rate limits and fixed-host WinHTTP validation remain unchanged.

## Native network worker

- Both providers now use `start_readonly_network` in
  `src/foobar/readonly_network_request.h`. The C++20 worker receives only
  an already validated provider path and, for Discogs, the temporary
  authorized token. **No foobar SDK handle, metadata record, physical audio,
  CUE carrier, native control or personal filesystem path is passed to it.**
- Worker completion is delivered by `PostMessageW` with no pointer or token
  payload. `std::atomic` release/acquire publishes the bounded JSON or a
  provider-local error. `std::jthread` is joined before the response storage
  or DLL code can be destroyed; no detached threads survive foobar shutdown.
- Each search/release request captures a UI-thread-only context version.
  Query edits (even edit-and-restore), provider switches (even switch-back),
  track/release selection, tab changes, refresh, CUE inspection and clipboard
  imports invalidate it. Both providers fail closed before publishing stale
  search or release responses.
- The main UI thread parses provider JSON, rechecks original source and rule
  snapshots and CUE bytes, verifies the same selected file/provider/release
  identity **including the still-selected exact Discogs release row**, and
  then replaces only the read-only candidate rows. Changing the highlighted
  release while its detail request is running discards that response, even
  though the physical file and provider are unchanged.
- Only **one** official provider network request may be active in the
  workspace. Search/load commands remain disabled until a response arrives.
  A source change, provider switch, refresh or altered CUE discards results.
- While a provider request is in flight, foobar playback and other windows
  can continue running. **Window X, Escape and Cancel** use the same safe
  `WM_CLOSE` route: the window hides until the request completes, then
  destroys its worker and state; none of these controls synchronously
  joins an in-flight WinHTTP request on the UI thread. Forced application
  shutdown safely joins bounded WinHTTP calls. A failed provider lookup
  cannot approve or write a metadata field.

## Qualification and remaining gates

The Windows native RC/HWND regression test exercises delayed network worker
completion, native query-edit and edit-restore staleness, event dispatch
while waiting, error delivery and joined teardown
without any real network or user credentials. Static source audits verify
that production UI handlers call the worker rather than blocking in WinHTTP.
Linux and Windows core and foobar Win32/x64 exact-HEAD CI must pass.

These gates **do not** constitute a real authorized Discogs response, a
live MusicBrainz service test, foobar installed-host acceptance or permission
to alter tags, artwork, ReplayGain, CUE bytes, audio or file timestamps.
Those remain separately gated before the final acceptance candidate.
