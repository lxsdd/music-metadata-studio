#include "stdafx.h"
#include "beatport_public_http.h"
#include "djmeta/beatport_public_page.h"
#include <windows.h>
#include <winhttp.h>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace djmeta_foobar {
namespace {
class OwnedHttp {
public:
    explicit OwnedHttp(HINTERNET value=nullptr):value_(value){}
    ~OwnedHttp(){ if(value_) WinHttpCloseHandle(value_); }
    OwnedHttp(const OwnedHttp&)=delete;
    OwnedHttp& operator=(const OwnedHttp&)=delete;
    HINTERNET get() const noexcept { return value_; }
private:
    HINTERNET value_ = nullptr;
};
void require(bool value,const char* text) {
    if(!value) throw std::runtime_error(text);
}
void cancel_if_requested(std::stop_token stop) {
    if(stop.stop_requested())
        throw std::runtime_error("Public page lookup cancelled.");
}
std::mutex request_mutex;
std::uint64_t last_public_page_tick=0;
}

std::string fetch_beatport_public_html_readonly(
    std::string_view verified_track_url,std::stop_token stop) {
    const auto qualified=djmeta::online::qualify_beatport_public_track_url(
        verified_track_url);
    cancel_if_requested(stop);
    // At most one per three seconds; no automated pagination, discovery,
    // no follow-up to a 403, 429, challenge, or missing markup.
    for(;;) {
        cancel_if_requested(stop);
        std::uint64_t remaining=0;
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            const auto now=GetTickCount64();
            remaining=last_public_page_tick && now-last_public_page_tick<3000
                ? 3000-(now-last_public_page_tick):0;
            if(!remaining) { last_public_page_tick=now; break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(
            remaining<50?remaining:50));
    }
    cancel_if_requested(stop);
    OwnedHttp session(WinHttpOpen(
        L"MusicMetadataStudio/0.1 (+https://github.com/lxsdd/music-metadata-studio)",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    require(session.get()!=nullptr,"Cannot initialize public-page HTTPS.");
    require(WinHttpSetTimeouts(session.get(),2500,2500,7000,7000)!=FALSE,
            "Cannot bound public-page HTTP timeouts.");
    DWORD no_redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    require(WinHttpSetOption(session.get(),WINHTTP_OPTION_REDIRECT_POLICY,
            &no_redirect,sizeof(no_redirect))!=FALSE,
            "Cannot prohibit public-page redirects.");
    DWORD no_login=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    require(WinHttpSetOption(session.get(),WINHTTP_OPTION_AUTOLOGON_POLICY,
            &no_login,sizeof(no_login))!=FALSE,
            "Cannot prevent implicit public-page authentication.");
    OwnedHttp conn(WinHttpConnect(session.get(),L"www.beatport.com",
                                  INTERNET_DEFAULT_HTTPS_PORT,0));
    require(conn.get()!=nullptr,"Cannot connect to public Beatport host.");
    constexpr std::string_view origin="https://www.beatport.com";
    const std::string path=qualified.canonical_url.substr(origin.size());
    const std::wstring route(path.begin(),path.end());
    OwnedHttp request(WinHttpOpenRequest(
        conn.get(),L"GET",route.c_str(),nullptr,WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    require(request.get()!=nullptr,"Cannot create public-page HTTPS request.");
    static constexpr wchar_t accept[]=L"Accept: text/html\r\n";
    require(WinHttpAddRequestHeaders(request.get(),accept,
        static_cast<DWORD>(-1),WINHTTP_ADDREQ_FLAG_ADD)!=FALSE,
        "Cannot request public HTML.");
    cancel_if_requested(stop);
    require(WinHttpSendRequest(request.get(),WINHTTP_NO_ADDITIONAL_HEADERS,0,
        WINHTTP_NO_REQUEST_DATA,0,0,0)!=FALSE,
        "Public Beatport HTTPS GET failed without file changes.");
    cancel_if_requested(stop);
    require(WinHttpReceiveResponse(request.get(),nullptr)!=FALSE,
        "Beatport public page returned no response.");
    DWORD status=0,bytes=sizeof(status);
    require(WinHttpQueryHeaders(request.get(),WINHTTP_QUERY_STATUS_CODE |
        WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,
        &bytes,WINHTTP_NO_HEADER_INDEX)!=FALSE,
        "Cannot validate Beatport public-page response status.");
    if(status==401 || status==403 || status==429)
        throw std::runtime_error(
            "Beatport public page denied or throttled this request. "
            "Stop; no workaround, retry, or data change was attempted.");
    if(status!=200)
        throw std::runtime_error("Beatport public page returned HTTP "+
            std::to_string(status)+". No files changed.");
    wchar_t type[128]{};
    DWORD type_bytes=sizeof(type);
    require(WinHttpQueryHeaders(request.get(),WINHTTP_QUERY_CONTENT_TYPE,
            WINHTTP_HEADER_NAME_BY_INDEX,type,&type_bytes,
            WINHTTP_NO_HEADER_INDEX)!=FALSE,
            "Beatport page did not declare an HTML content type.");
    const std::wstring_view mime(type);
    if(!mime.starts_with(L"text/html"))
        throw std::runtime_error("Public Beatport response was not HTML.");
    std::string response;
    constexpr std::size_t maximum=2u*1024u*1024u;
    for(;;) {
        cancel_if_requested(stop);
        DWORD available=0;
        require(WinHttpQueryDataAvailable(request.get(),&available)!=FALSE,
                "Cannot read the public page.");
        if(!available) break;
        if(available>maximum-response.size())
            throw std::runtime_error("Public Beatport HTML exceeds 2 MiB.");
        const auto offset=response.size();
        response.resize(offset+available);
        DWORD received=0;
        require(WinHttpReadData(request.get(),response.data()+offset,
                available,&received)!=FALSE && received>0,
                "Public Beatport page was incomplete.");
        response.resize(offset+received);
    }
    cancel_if_requested(stop);
    if(response.empty())
        throw std::runtime_error("Beatport public page returned no HTML.");
    return response;
}
} // namespace djmeta_foobar
