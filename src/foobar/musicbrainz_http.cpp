#include "stdafx.h"
#include "musicbrainz_http.h"
#include "djmeta/musicbrainz_provider.h"

#include <winhttp.h>
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
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
    // This is the exact shared predicate tested by the core regression suite.
    // No parallel native URL parser may drift from the provider builder.
    return djmeta::online::musicbrainz::qualified_musicbrainz_request_path(path);
}
void reject_cancelled(std::stop_token cancel) {
    if(cancel.stop_requested())
        throw std::runtime_error("Provider request cancelled.");
}
std::mutex request_clock_mutex;
std::uint64_t last_request_tick = 0;
} // namespace

std::string fetch_musicbrainz_json_readonly(
    std::string_view path, std::stop_token cancel) {
    if (!safe_path(path))
        throw std::invalid_argument("Invalid official MusicBrainz request path.");
    reject_cancelled(cancel);
    // Reserve one time slot in the network worker. A second explicit
    // search waits in short cancelable slices; never block foobar's UI
    // thread, busy-spin, or bypass the official per-second rate ceiling.
    for(;;) {
        reject_cancelled(cancel);
        std::uint64_t remaining=0;
        {
            std::lock_guard<std::mutex> lock(request_clock_mutex);
            const auto tick=GetTickCount64();
            remaining=djmeta::online::musicbrainz::request_spacing_wait_ms(
                last_request_tick,tick);
            if(remaining==0) {
                last_request_tick=tick; // Reserve even if HTTPS fails.
                break;
            }
        }
        reject_cancelled(cancel);
        const auto slice=remaining>50?50:remaining;
        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
    }
    // Synchronous, explicit user click only. WinHTTP respects OS proxy, TLS
    // certificate validation and OS trust. No authorization or session state.
    reject_cancelled(cancel);
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
    reject_cancelled(cancel);
    must(WinHttpReceiveResponse(request.get(),nullptr)!=FALSE,
        "MusicBrainz did not provide a response.");
    reject_cancelled(cancel);
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
        reject_cancelled(cancel);
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
        reject_cancelled(cancel);
    }
    reject_cancelled(cancel);
    if (response.empty())
        throw std::runtime_error("MusicBrainz returned an empty response.");
    return response;
}
} // namespace djmeta_foobar
