#pragma once

// Windows-only official Apple/iTunes search/lookup transport. This module
// makes NO network request unless explicitly invoked by the host following
// a user-initiated action. It never downloads audio, scrapes pages or writes
// tags, CUE sheets or files. Do not call it on foobar's UI thread.

#include <string>
#include <string_view>

namespace djmeta {

enum class ApplePrecisionCueHttpKind {
    AlbumSearch,
    AlbumLookup
};

struct ApplePrecisionCueHttpRequest {
    ApplePrecisionCueHttpKind kind = ApplePrecisionCueHttpKind::AlbumLookup;
    std::string parameter; // official Apple collection ID or album search terms
    std::string storefront = "DE";
    bool user_initiated = false;
};

// Verify request and derive an exact official HTTPS API path, without I/O.
// Throws std::invalid_argument on arbitrary URLs / bad IDs / bad terms.
std::string apple_precision_cue_http_path(const ApplePrecisionCueHttpRequest& request);

// Windows 10+ WinHTTP, secure TLS, fixed host itunes.apple.com, no redirects,
// 4 MiB maximum, bounded timeouts, 3.1-second request interval (per process).
// Throws on network errors or access/HTTP failures; no implicit retries.
// It is an inert building block until the foobar host wires a user action.
std::string fetch_apple_precision_cue_json(
    const ApplePrecisionCueHttpRequest& request);

} // namespace djmeta
