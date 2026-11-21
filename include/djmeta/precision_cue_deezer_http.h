#pragma once
// User-initiated, fixed-host, read-only official Deezer album JSON request.
// No audio download, redirect, scraper, token, filesystem or tag operations.
#include <cstddef>
#include <string>

namespace djmeta {
struct DeezerPrecisionCueHttpRequest {
    std::string album_id; // strictly decimal, validated before any network I/O
    bool user_initiated = false;
    bool track_page = false;
    std::size_t track_index = 0; // fixed indices 0,100,...,700
};
std::string deezer_precision_cue_http_path(
    const DeezerPrecisionCueHttpRequest& request);
// Windows WinHTTP: TLS, fixed api.deezer.com host, no redirects,
// bounded response/time/rate, no automatic retry, no implicit authorization.
std::string fetch_deezer_precision_cue_json(
    const DeezerPrecisionCueHttpRequest& request);
} // namespace djmeta
