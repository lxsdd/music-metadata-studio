#pragma once
#include <stop_token>
#include <string>
#include <string_view>
namespace djmeta_foobar {
// Deliberate, single public-page HTTPS GET only. The host stays pinned to
// www.beatport.com, no cookies, login, redirect, retries, API or bot bypass.
// A 403/429 is a hard stop, not a signal to impersonate a browser.
std::string fetch_beatport_public_html_readonly(
    std::string_view verified_track_url,std::stop_token cancel={});
} // namespace djmeta_foobar
