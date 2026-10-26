#pragma once
// Provider-independent user-facing diagnostic for the only permitted
// read-only catalog HTTPS status classes. Never authorizes retries, writes,
// credential prompts or redirect handling.
#include <string_view>
namespace djmeta {
[[nodiscard]] constexpr std::string_view precision_cue_catalog_http_diagnostic(
    unsigned status) noexcept {
    switch (status) {
    case 200: return {}; // The caller handles success, never throws here.
    case 401: case 403:
        return "access denied; verify authorized API access";
    case 404: return "album not available in this catalog";
    case 408: return "request timed out; retry later";
    case 429: return "rate limit reached; retry later";
    default:
        return status >= 500 && status <= 599
            ? "catalog temporarily unavailable"
            : "unexpected HTTP response";
    }
}
} // namespace djmeta
