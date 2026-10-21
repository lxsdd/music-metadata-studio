#pragma once
// Crates.co assists HUMAN browsing in the user's normal browser.
// No undocumented Crates API, external credential/token, cookie retrieval,
// Beatport authorization impersonation, or HTML scraping in this adapter.
// The user's existing Crates/Beatport browser session remains in the browser.
#include "djmeta/beatport_provider.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace djmeta::online {
struct CratesBrowserSearch {
    std::string search_text;
    // Public, verified Crates landing page. Do not invent an internal
    // query URL or carry local tags in URL parameters.
    std::string browser_url = "https://crates.co/";
};

inline CratesBrowserSearch prepare_crates_browser_search(
    std::string_view title,std::string_view artist) {
    if(!beatport::text_ok(title,150) ||
       (!artist.empty() && !beatport::text_ok(artist,150)))
        throw std::invalid_argument("Crates search requires valid TITLE and ARTIST");
    // Preserve TITLE exactly, including an Extended/Radio/Remix label.
    // Search broadening is a user's decision, not evidence of a match.
    std::string query;
    if(!artist.empty() && artist.size()+1+title.size()<=150) {
        query=std::string(artist);
        query.push_back(' ');
    }
    query.append(title);
    if(!beatport::text_ok(query,150))
        throw std::invalid_argument("Crates browser search query exceeds 150 bytes");
    return CratesBrowserSearch{std::move(query)};
}

// Only one value is eligible for an unambiguous human search. Multiple
// artist values may be meaningful DJ collaborations; do not invent an
// ordering or assume the first is the only creator. In that case search
// by the full title alone, preserving all local original tags.
inline CratesBrowserSearch prepare_crates_browser_search(
    const MetadataDocument& physical) {
    std::string title,artist;
    bool title_seen=false,artist_seen=false,artist_ambiguous=false;
    for(const auto& field:physical.fields) {
        const auto name=ascii_upper_field(field.name);
        if(name=="TITLE") {
            if(title_seen || field.values.size()!=1)
                throw std::invalid_argument("Ambiguous physical TITLE for Crates lookup");
            title_seen=true;
            title=field.values.front();
        } else if(name=="ARTIST") {
            if(artist_seen || field.values.size()!=1)
                artist_ambiguous=true;
            artist_seen=true;
            if(field.values.size()==1) artist=field.values.front();
        }
    }
    if(!title_seen) throw std::invalid_argument("No physical TITLE for Crates lookup");
    return prepare_crates_browser_search(
        title,artist_ambiguous?std::string_view{}:std::string_view{artist});
}
} // namespace djmeta::online
