#include "djmeta/precision_cue_apple_http.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <stdexcept>
#include <string>

namespace {

template<class F> void rejects(F f) {
    bool caught = false;
    try { f(); } catch (const std::invalid_argument&) { caught = true; }
    assert(caught);
}

void request_routes() {
    djmeta::ApplePrecisionCueHttpRequest request;
    request.kind = djmeta::ApplePrecisionCueHttpKind::AlbumLookup;
    request.parameter = "000123456";
    request.storefront = "de";
    assert(djmeta::apple_precision_cue_http_path(request) ==
        "/lookup?id=123456&entity=song&limit=200&country=DE");

    request.kind = djmeta::ApplePrecisionCueHttpKind::AlbumSearch;
    request.parameter = "Mix & 東京";
    assert(djmeta::apple_precision_cue_http_path(request) ==
        "/search?media=music&entity=album&limit=25&country=DE&term=Mix%20%26%20%E6%9D%B1%E4%BA%AC");
}

void strict_gate_does_not_open_network() {
    djmeta::ApplePrecisionCueHttpRequest request;
    request.parameter = "123456";
    request.storefront = "DE";
    request.user_initiated = false;
    // Must reject even a perfectly valid request before WinHTTP is invoked.
    rejects([&] { (void)djmeta::fetch_apple_precision_cue_json(request); });

    request.parameter = "https://example.com/";
    rejects([&] { (void)djmeta::apple_precision_cue_http_path(request); });
    request.parameter = "123&entity=podcast";
    rejects([&] { (void)djmeta::apple_precision_cue_http_path(request); });
    request.parameter = "1";
    request.storefront = "D&";
    rejects([&] { (void)djmeta::apple_precision_cue_http_path(request); });
    request.storefront = "DE";
    request.kind = static_cast<djmeta::ApplePrecisionCueHttpKind>(999);
    rejects([&] { (void)djmeta::apple_precision_cue_http_path(request); });
}

} // namespace

int main() {
    request_routes();
    strict_gate_does_not_open_network();
}
