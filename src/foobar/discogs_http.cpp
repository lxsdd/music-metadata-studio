#include "stdafx.h"
#include "discogs_http.h"
#include "djmeta/discogs_provider.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace djmeta_foobar {
namespace {
class NativeHandle {
public:
    explicit NativeHandle(HINTERNET handle=nullptr) : handle_(handle) {}
    ~NativeHandle() { if(handle_) WinHttpCloseHandle(handle_); }
    NativeHandle(const NativeHandle&)=delete;
    NativeHandle& operator=(const NativeHandle&)=delete;
    HINTERNET get() const { return handle_; }
private:
    HINTERNET handle_ = nullptr;
};
void must(bool value, const char* message) {
    if(!value) throw std::runtime_error(message);
}
bool valid_personal_token(std::string_view token) {
    if(token.size()<8 || token.size()>256) return false;
    for(unsigned char c:token)
        if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
             (c>='0'&&c<='9') || c=='_' || c=='-'))
            return false;
    return true;
}
std::mutex request_mutex;
std::uint64_t last_request_tick = 0;
} // namespace

std::string fetch_discogs_json_readonly(
    std::string_view path, std::string_view user_token) {
    if(!djmeta::online::discogs::allowed_readonly_path(path))
        throw std::invalid_argument("Invalid official Discogs request path.");
    if(!valid_personal_token(user_token))
        throw std::invalid_argument(
            "Discogs requires a separately configured authorized user token.");
    // Conservative single process limit, provider-local. Do not block foobar's
    // UI thread waiting for rate limits or retry in the background.
    {
        std::lock_guard<std::mutex> lock(request_mutex);
        const auto tick=GetTickCount64();
        if(last_request_tick!=0 && tick-last_request_tick<1600)
            throw std::runtime_error(
                "Discogs rate limit: try again after the request interval.");
        last_request_tick=tick;
    }
    NativeHandle session(WinHttpOpen(
        L"MusicMetadataStudio/0.1 (+https://github.com/lxsdd/music-metadata-studio)",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    must(session.get()!=nullptr,"Cannot initialize Discogs HTTPS session.");
    must(WinHttpSetTimeouts(session.get(),2500,2500,7000,7000)!=FALSE,
         "Unable to configure Discogs HTTP timeouts.");
    DWORD no_redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    must(WinHttpSetOption(session.get(),WINHTTP_OPTION_REDIRECT_POLICY,
         &no_redirect,sizeof(no_redirect))!=FALSE,
         "Unable to enforce Discogs no-redirect policy.");
    DWORD no_autologon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    must(WinHttpSetOption(session.get(),WINHTTP_OPTION_AUTOLOGON_POLICY,
         &no_autologon,sizeof(no_autologon))!=FALSE,
         "Unable to disable implicit Discogs login.");
    NativeHandle connection(WinHttpConnect(
        session.get(),L"api.discogs.com",INTERNET_DEFAULT_HTTPS_PORT,0));
    must(connection.get()!=nullptr,"Cannot connect to official Discogs API.");
    const std::wstring route(path.begin(),path.end());
    NativeHandle request(WinHttpOpenRequest(
        connection.get(),L"GET",route.c_str(),nullptr,WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    must(request.get()!=nullptr,"Cannot initialize Discogs read-only request.");
    std::wstring headers=L"Accept: application/json\r\nAuthorization: Discogs token=";
    for (unsigned char c:user_token) headers.push_back(static_cast<wchar_t>(c));
    headers+=L"\r\n";
    must(WinHttpAddRequestHeaders(request.get(),headers.c_str(),
         static_cast<DWORD>(-1),WINHTTP_ADDREQ_FLAG_ADD)!=FALSE,
         "Cannot set authorized Discogs request headers.");
    // The full header is never printed or persisted; clear our temporary.
    SecureZeroMemory(headers.data(),headers.size()*sizeof(wchar_t));
    must(WinHttpSendRequest(request.get(),WINHTTP_NO_ADDITIONAL_HEADERS,0,
         WINHTTP_NO_REQUEST_DATA,0,0,0)!=FALSE,
         "Discogs HTTPS request failed.");
    must(WinHttpReceiveResponse(request.get(),nullptr)!=FALSE,
         "Discogs did not return a response.");
    DWORD status=0;
    DWORD status_size=sizeof(status);
    must(WinHttpQueryHeaders(request.get(),WINHTTP_QUERY_STATUS_CODE |
         WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,
         &status_size,WINHTTP_NO_HEADER_INDEX)!=FALSE,
         "Cannot validate Discogs response status.");
    if(status==401 || status==403)
        throw std::runtime_error(
            "Discogs authorization denied; provider credentials need review.");
    if(status==429)
        throw std::runtime_error(
            "Discogs rate limit reached. No metadata or files were changed.");
    if(status!=200)
        throw std::runtime_error(
            "Discogs returned HTTP "+std::to_string(status)+
            ". No metadata or files were changed.");
    std::string response;
    constexpr std::size_t maximum=2u*1024u*1024u;
    for(;;) {
        DWORD available=0;
        must(WinHttpQueryDataAvailable(request.get(),&available)!=FALSE,
             "Cannot read Discogs response.");
        if(available==0) break;
        if(available>maximum-response.size())
            throw std::runtime_error("Discogs response exceeds 2 MiB.");
        const auto start=response.size();
        response.resize(start+available);
        DWORD received=0;
        must(WinHttpReadData(request.get(),response.data()+start,
             available,&received)!=FALSE,"Incomplete Discogs response.");
        if(received==0)
            throw std::runtime_error("Truncated Discogs response.");
        response.resize(start+received);
    }
    if(response.empty())
        throw std::runtime_error("Discogs returned an empty catalog response.");
    return response;
}
} // namespace djmeta_foobar
