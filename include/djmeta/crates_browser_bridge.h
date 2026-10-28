#pragma once
// Crates.co assists HUMAN browsing in the user's normal browser.
// No undocumented Crates API, external credential/token, cookie retrieval,
// Beatport authorization impersonation, or HTML scraping in this adapter.
// The user's existing Crates/Beatport browser session remains in the browser.
#include "djmeta/beatport_provider.h"
#include "djmeta/online_search_credits.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace djmeta::online {
struct CratesBrowserSearch {
    std::string search_text;
    // Verified public browser SEARCH route. Only physical artist/title
    // appear in q; no local paths, other tags, login details or tokens.
    std::string browser_url;
};

// Encode each UTF-8 byte for the public Crates browser route. Never use
// untrusted physical tags as URL syntax, query delimiters or fragments.
inline std::string crates_percent_encode(std::string_view query) {
    constexpr char hex[]="0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(query.size()*3);
    for(const unsigned char ch:query) {
        if((ch>='A' && ch<='Z') || (ch>='a' && ch<='z') ||
           (ch>='0' && ch<='9') || ch=='-' || ch=='_' ||
           ch=='.' || ch=='~') {
            encoded.push_back(static_cast<char>(ch));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[ch>>4]);
            encoded.push_back(hex[ch&15]);
        }
    }
    return encoded;
}

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
    const auto url=std::string("https://crates.co/search?q=")+
                   crates_percent_encode(query);
    return CratesBrowserSearch{std::move(query),std::move(url)};
}

// Use the same optional physical ARTIST collaboration search hint as the
// Discogs path. A missing/ambiguous credit falls back to full TITLE only.
inline CratesBrowserSearch prepare_crates_browser_search(
    const MetadataDocument& physical) {
    std::string title;
    bool title_seen=false;
    for(const auto& field:physical.fields) {
        if(ascii_upper_field(field.name)!="TITLE") continue;
        if(title_seen || field.values.size()!=1)
            throw std::invalid_argument("Ambiguous physical TITLE for Crates lookup");
        title_seen=true;
        title=field.values.front();
    }
    if(!title_seen) throw std::invalid_argument("No physical TITLE for Crates lookup");
    return prepare_crates_browser_search(
        title,physical_collaborator_search_hint(physical));
}
} // namespace djmeta::online
