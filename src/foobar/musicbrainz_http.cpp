#include "stdafx.h"
#include "musicbrainz_http.h"
#include "djmeta/musicbrainz_provider.h"

#include <winhttp.h>
#include <windows.h>

#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace djmeta_foobar {
namespace {

class WinHttpHandle {
public:
    explicit WinHttpHandle(HINTERNET h=nullptr) : h_(h) {}
    ~WinHttpHandle() { if (h_) WinHttpCloseHandle(h_); }
    WinHttpHandle(const WinHttpHandle&)=delete;
    WinHttpHandle& operator=(const WinHttpHandle&)=delete;
    HINTERNET get() const { return h_; }
private:
    HINTERNET h_ = nullptr;
};
void must(bool success, const char* detail) {
    if (!success) throw std::runtime_error(detail);
}
bool safe_path(std::string_view path) {
    // Origin, endpoint, query grammar and release identity are all fixed.
    // No user-controlled host, port, relative path, redirect, HTTP method or
    // arbitrary query parameters may cross this WinHTTP boundary.
    if (path.empty() || path.size()>2048) return false;
    for (unsigned char ch:path)
        if (ch<0x21 || ch>0x7e || ch=='#' || ch=='\\')
            return false;
    constexpr std::string_view search_suffix="&fmt=json&limit=8";
    for (const std::string_view prefix : {
        std::string_view("/ws/2/recording?query="),
        std::string_view("/ws/2/release?query=")}) {
        if (!path.starts_with(prefix) || !path.ends_with(search_suffix)) continue;
        const auto encoded=path.substr(prefix.size(),
            path.size()-prefix.size()-search_suffix.size());
        if (encoded.empty()) return false;
        for (std::size_t i=0;i<encoded.size();++i) {
            const unsigned char c=encoded[i];
            if ((c>='A'&&c<='Z') || (c>='a'&&c<='z') ||
                (c>='0'&&c<='9') || c=='-' || c=='_' ||
                c=='.' || c=='~') continue;
            if (c!='%' || i+2>=encoded.size()) return false;
            const auto hex=[](unsigned char h) {
                return (h>='0'&&h<='9') ||
                    (h>='A'&&h<='F') || (h>='a'&&h<='f');
            };
            if (!hex(encoded[i+1]) || !hex(encoded[i+2])) return false;
            i+=2;
        }
        return true;
    }
    constexpr std::string_view lookup_prefix="/ws/2/release/";
    constexpr std::string_view lookup_suffix=
        "?inc=recordings%2Bartist-credits%2Bisrcs&fmt=json";
    if (path.starts_with(lookup_prefix) && path.ends_with(lookup_suffix)) {
        const auto id=path.substr(lookup_prefix.size(),
            path.size()-lookup_prefix.size()-lookup_suffix.size());
        return djmeta::online::musicbrainz::valid_mbid(id);
    }
    return false;
}
std::mutex request_clock_mutex;
std::uint64_t last_request_tick = 0;
} // namespace

std::string fetch_musicbrainz_json_readonly(std::string_view path) {
    if (!safe_path(path))
        throw std::invalid_argument("Invalid official MusicBrainz request path.");
    {
        std::lock_guard<std::mutex> lock(request_clock_mutex);
        const auto tick=GetTickCount64();
        if (last_request_tick != 0 && tick-last_request_tick < 1150)
            throw std::runtime_error(
                "MusicBrainz rate limit: wait at least 1.2 seconds and try again.");
        last_request_tick=tick; // Reserve even if this request fails.
    }
    // Synchronous, explicit user click only. WinHTTP respects OS proxy, TLS
    // certificate validation and OS trust. No authorization or session state.
    WinHttpHandle session(WinHttpOpen(
        L"MusicMetadataStudio/0.1 (+https://github.com/lxsdd/music-metadata-studio)",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    must(session.get()!=nullptr,"Unable to initialize the MusicBrainz HTTPS client.");
    must(WinHttpSetTimeouts(session.get(),2500,2500,7000,7000)!=FALSE,
         "Unable to configure MusicBrainz request timeouts.");
    const DWORD no_redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    must(WinHttpSetOption(session.get(),WINHTTP_OPTION_REDIRECT_POLICY,
        const_cast<DWORD*>(&no_redirect),sizeof(no_redirect))!=FALSE,
        "Unable to enforce MusicBrainz no-redirect policy.");
    const DWORD high_security=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    must(WinHttpSetOption(session.get(),WINHTTP_OPTION_AUTOLOGON_POLICY,
        const_cast<DWORD*>(&high_security),sizeof(high_security))!=FALSE,
        "Unable to disable implicit MusicBrainz credentials.");
    WinHttpHandle connection(WinHttpConnect(
        session.get(),L"musicbrainz.org",INTERNET_DEFAULT_HTTPS_PORT,0));
    must(connection.get()!=nullptr,"Cannot connect to the official MusicBrainz host.");
    const std::wstring route(path.begin(),path.end()); // URL builder yields ASCII.
    WinHttpHandle request(WinHttpOpenRequest(
        connection.get(),L"GET",route.c_str(),nullptr,WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    must(request.get()!=nullptr,"Cannot start MusicBrainz HTTPS request.");
    static constexpr wchar_t accept[]=L"Accept: application/json\r\n";
    must(WinHttpAddRequestHeaders(request.get(),accept,
        static_cast<DWORD>(-1),WINHTTP_ADDREQ_FLAG_ADD)!=FALSE,
        "Unable to request JSON from MusicBrainz.");
    must(WinHttpSendRequest(request.get(),WINHTTP_NO_ADDITIONAL_HEADERS,0,
        WINHTTP_NO_REQUEST_DATA,0,0,0)!=FALSE,
        "MusicBrainz HTTPS request failed.");
    must(WinHttpReceiveResponse(request.get(),nullptr)!=FALSE,
        "MusicBrainz did not provide a response.");
    DWORD status=0;
    DWORD status_size=sizeof(status);
    must(WinHttpQueryHeaders(request.get(),WINHTTP_QUERY_STATUS_CODE |
        WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,
        &status_size,WINHTTP_NO_HEADER_INDEX)!=FALSE,
        "Cannot validate MusicBrainz HTTP status.");
    if (status == 503 || status == 429)
        throw std::runtime_error(
            "MusicBrainz is throttling requests; please wait before retrying.");
    if (status != 200)
        throw std::runtime_error(
            "MusicBrainz returned HTTP " + std::to_string(status) +
            ". No tags or files were changed.");
    std::string response;
    constexpr std::size_t maximum=2u*1024u*1024u;
    while (true) {
        DWORD pending=0;
        must(WinHttpQueryDataAvailable(request.get(),&pending)!=FALSE,
             "Cannot read MusicBrainz response.");
        if (pending==0) break;
        if (pending > maximum-response.size())
            throw std::runtime_error("MusicBrainz response exceeds 2 MiB.");
        const std::size_t current=response.size();
        response.resize(current+pending);
        DWORD received=0;
        must(WinHttpReadData(request.get(),response.data()+current,
            pending,&received)!=FALSE,
            "Incomplete MusicBrainz response.");
        if (received==0) throw std::runtime_error("Truncated MusicBrainz response.");
        response.resize(current+received);
    }
    if (response.empty())
        throw std::runtime_error("MusicBrainz returned an empty response.");
    return response;
}
} // namespace djmeta_foobar
