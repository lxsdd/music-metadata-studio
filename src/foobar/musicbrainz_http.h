#pragma once

#include <string>
#include <string_view>

namespace djmeta_foobar {

// Sends one explicitly requested official MusicBrainz WS/2 GET to the fixed
// HTTPS origin only. No audio/path upload, authentication, cookies, redirects,
// scraping, persistent cache, background tasks or write-capable file I/O.
// Caller MUST construct path using musicbrainz_provider.h's bounded builders.
// A one-request-per-second guard rejects fast repeats rather than blocking UI.
std::string fetch_musicbrainz_json_readonly(std::string_view official_api_path);

} // namespace djmeta_foobar
