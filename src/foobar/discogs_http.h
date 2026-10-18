#pragma once

#include <string>
#include <string_view>

namespace djmeta_foobar {

// One explicit, synchronous, read-only GET to api.discogs.com. Caller must
// supply an independently authorized user token at runtime (never embedded
// in code, logs, URLs, GitHub, or another component's private settings).
// Use ONLY paths generated and allowed by djmeta::online::discogs.
// No retries, redirects, automatic bulk lookups, cookies, file or tag writes.
// Not wired to user preferences/UI until the authentication and host gates
// have been qualified; absence of credentials is a provider-local blocker.
std::string fetch_discogs_json_readonly(
    std::string_view canonical_api_path,
    std::string_view personal_access_token);

} // namespace djmeta_foobar
