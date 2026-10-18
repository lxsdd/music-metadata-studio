#include "djmeta/precision_cue_apple_http.h"
#include "djmeta/precision_cue_apple.h"

#if !defined(_WIN32)
#error "This HTTP transport is intended only for the Windows foobar host."
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace djmeta {
namespace {

constexpr std::string_view kAppleBase = "https://itunes.apple.com";
constexpr std::size_t kMaximumResponseBytes = 4u * 1024u * 1024u;
constexpr auto kMinimumInterval = std::chrono::milliseconds(3100);

class HttpHandle {
public:
    explicit HttpHandle(HINTERNET h) : value_(h) {
        if (!value_) throw std::runtime_error("Apple HTTPS setup failed");
    }
    ~HttpHandle() { WinHttpCloseHandle(value_); }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
    HINTERNET get() const noexcept { return value_; }
private:
    HINTERNET value_;
};

bool acceptable_content_type(const wchar_t* content_type) {
    std::wstring s(content_type);
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
        return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
    });
    return s.starts_with(L"application/json") ||
           s.starts_with(L"text/javascript") ||
           s.starts_with(L"application/javascript");
}

void check_winhttp(BOOL ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void enforce_process_rate_limit() {
    // Fail fast rather than sleep on the foobar UI thread. The caller must
    // schedule its next *user-requested* call; never auto-loop/retry.
    static std::mutex gate;
    static auto last_request = std::chrono::steady_clock::time_point::min();
    const std::lock_guard<std::mutex> lock(gate);
    const auto now = std::chrono::steady_clock::now();
    if (last_request != std::chrono::steady_clock::time_point::min() &&
        now - last_request < kMinimumInterval)
        throw std::runtime_error("Apple API rate limit: wait before another request");
    last_request = now;
}

} // namespace

std::string apple_precision_cue_http_path(const ApplePrecisionCueHttpRequest& request) {
    std::string url;
    switch (request.kind) {
        case ApplePrecisionCueHttpKind::AlbumLookup:
            url = apple_precision_cue_lookup_url(request.parameter, request.storefront);
            break;
        case ApplePrecisionCueHttpKind::AlbumSearch:
            url = apple_precision_cue_album_search_url(request.parameter, request.storefront);
            break;
        default:
            throw std::invalid_argument("Unsupported Apple API request");
    }

    if (!url.starts_with(kAppleBase) || url.size() <= kAppleBase.size() ||
        url.size() > 4096)
        throw std::invalid_argument("Apple API request not permitted");
    url.erase(0, kAppleBase.size());
    if (!(url.starts_with("/lookup?") || url.starts_with("/search?")))
        throw std::invalid_argument("Apple API endpoint not permitted");
    // All non-ASCII term bytes must already be percent-encoded.
    for (unsigned char ch : url)
        if (ch < 0x21u || ch > 0x7eu || ch == '#' || ch == '\\')
            throw std::invalid_argument("Invalid Apple API URL characters");
    return url;
}

std::string fetch_apple_precision_cue_json(const ApplePrecisionCueHttpRequest& request) {
    if (!request.user_initiated)
        throw std::invalid_argument("Apple network request requires explicit user action");
    const std::string path = apple_precision_cue_http_path(request);
    enforce_process_rate_limit();

    HttpHandle session(WinHttpOpen(
        L"MusicMetadataStudio-PrecisionCUE/0.1",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    check_winhttp(WinHttpSetTimeouts(session.get(), 5000, 5000, 10000, 10000),
                  "Apple HTTPS timeout configuration failed");

    HttpHandle connection(WinHttpConnect(
        session.get(), L"itunes.apple.com", WINHTTP_DEFAULT_HTTPS_PORT, 0));
    const std::wstring request_path(path.begin(), path.end());
    HttpHandle handle(WinHttpOpenRequest(
        connection.get(), L"GET", request_path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));

    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    check_winhttp(WinHttpSetOption(handle.get(), WINHTTP_OPTION_REDIRECT_POLICY,
                                  &redirect_policy, sizeof(redirect_policy)),
                  "Apple HTTPS redirect policy could not be enforced");
    constexpr wchar_t accept[] = L"Accept: application/json\r\n";
    check_winhttp(WinHttpSendRequest(handle.get(), accept, static_cast<DWORD>(-1),
                                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0),
                  "Apple HTTPS request failed");
    check_winhttp(WinHttpReceiveResponse(handle.get(), nullptr),
                  "Apple HTTPS response failed");

    DWORD status = 0;
    DWORD length = sizeof(status);
    check_winhttp(WinHttpQueryHeaders(
        handle.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX),
        "Apple HTTP status unavailable");
    if (status != 200)
        throw std::runtime_error("Apple API returned non-success HTTP status");

    std::array<wchar_t, 256> type_buffer{};
    length = static_cast<DWORD>(type_buffer.size() * sizeof(wchar_t));
    check_winhttp(WinHttpQueryHeaders(
        handle.get(), WINHTTP_QUERY_CONTENT_TYPE,
        WINHTTP_HEADER_NAME_BY_INDEX, type_buffer.data(), &length, WINHTTP_NO_HEADER_INDEX),
        "Apple API content type missing");
    type_buffer.back() = L'\0';
    if (!acceptable_content_type(type_buffer.data()))
        throw std::runtime_error("Apple API returned non-JSON content");

    std::string body;
    std::array<char, 16384> buffer{};
    for (;;) {
        DWORD received = 0;
        check_winhttp(WinHttpReadData(handle.get(), buffer.data(),
                                     static_cast<DWORD>(buffer.size()), &received),
                      "Apple HTTPS download failed");
        if (received == 0) break;
        if (received > kMaximumResponseBytes - body.size())
            throw std::runtime_error("Apple API JSON response too large");
        body.append(buffer.data(), received);
    }
    if (body.empty())
        throw std::runtime_error("Apple API returned an empty response");
    return body; // strict UTF-8/JSON validation is performed by the adapter
}

} // namespace djmeta
