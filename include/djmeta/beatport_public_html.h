#pragma once
// Pure, strict parser for ONE explicitly requested public Beatport HTML page.
// No network, scripts, DOM execution, HTML scraping fallback, or writes.
// If site markup cannot independently identify one recording, fail closed.
#include "djmeta/beatport_public_page.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>

namespace djmeta::online {
inline std::string lowercase_ascii(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for(unsigned char c:input)
        out.push_back(c>='A' && c<='Z'?static_cast<char>(c+('a'-'A')):
                      static_cast<char>(c));
    return out;
}
inline bool public_jsonld_script_tag(std::string_view tag) {
    // Accept both single and double quoted values and arbitrary attribute
    // order, not an unquoted or partial "type" occurring in another value.
    const auto lower=lowercase_ascii(tag);
    std::size_t pos=0;
    while((pos=lower.find("type",pos))!=std::string::npos) {
        if(pos>0 && ((lower[pos-1]>='a' && lower[pos-1]<='z') ||
                     lower[pos-1]=='-' || lower[pos-1]=='_')) {
            pos+=4; continue;
        }
        std::size_t n=pos+4;
        while(n<lower.size() && (lower[n]==' ' || lower[n]=='\t')) ++n;
        if(n>=lower.size() || lower[n]!='=') { pos+=4; continue; }
        ++n;
        while(n<lower.size() && (lower[n]==' ' || lower[n]=='\t')) ++n;
        if(n>=lower.size() || (lower[n]!='"' && lower[n]!='\'')) {
            pos+=4; continue;
        }
        const char quote=lower[n++];
        const auto end=lower.find(quote,n);
        if(end!=std::string::npos &&
           lower.substr(n,end-n)=="application/ld+json")
            return true;
        pos+=4;
    }
    return false;
}
inline void find_public_recording_nodes(
    const beatport::Json& node,std::vector<const beatport::Json*>& matches,
    unsigned depth=0) {
    if(depth>4 || matches.size()>2)
        throw std::invalid_argument("Unbounded public structured-data graph");
    if(node.type==beatport::Json::Type::Array) {
        if(node.array.size()>64)
            throw std::invalid_argument("Unbounded public structured-data array");
        for(const auto& item:node.array)
            find_public_recording_nodes(item,matches,depth+1);
    } else if(node.type==beatport::Json::Type::Object) {
        if(public_music_recording_type(node))
            matches.push_back(&node);
        if(const auto* graph=node.get("@graph"))
            find_public_recording_nodes(*graph,matches,depth+1);
    }
}
inline beatport::TrackEvidence parse_beatport_public_html(
    std::string_view requested_url,std::string_view html) {
    const auto target=qualify_beatport_public_track_url(requested_url);
    if(html.empty() || html.size()>2u*1024u*1024u ||
       html.find('\0')!=std::string_view::npos)
        throw std::invalid_argument("Public page is empty, binary, or over 2 MiB");
    // Index tags case-insensitively, keeping original bytes for JSON strings.
    const auto lower=lowercase_ascii(html);
    std::size_t cursor=0;
    std::optional<beatport::TrackEvidence> chosen;
    std::size_t examined=0;
    while(true) {
        const auto at=lower.find("<script",cursor);
        if(at==std::string::npos) break;
        if(++examined>128)
            throw std::invalid_argument("Unbounded public page script count");
        const auto next=at+7;
        if(next<lower.size() && lower[next]!=' ' && lower[next]!='\t' &&
           lower[next]!='\r' && lower[next]!='\n' && lower[next]!='>') {
            cursor=next; continue;
        }
        const auto end=lower.find('>',next);
        if(end==std::string::npos || end-next>2048)
            throw std::invalid_argument("Invalid public script tag");
        const auto close=lower.find("</script",end+1);
        if(close==std::string::npos)
            throw std::invalid_argument("Unclosed public page script tag");
        const auto close_end=lower.find('>',close+8);
        if(close_end==std::string::npos || close_end-close>64)
            throw std::invalid_argument("Malformed public page script close");
        if(public_jsonld_script_tag(html.substr(at,end-at+1))) {
            const auto body=html.substr(end+1,close-end-1);
            if(body.size()>65536)
                throw std::invalid_argument("Public JSON-LD too large");
            const auto graph=musicbrainz::JsonParser(body).parse();
            std::vector<const beatport::Json*> found;
            find_public_recording_nodes(graph,found);
            for(const auto* item:found) {
                // Ignore unrelated recommendations WITH explicit foreign URL;
                // missing identities and two qualified matching objects fail.
                const auto* url=item->get("url");
                const auto* id=item->get("@id");
                if(url && url->type==beatport::Json::Type::String &&
                   url->text!=target.canonical_url) continue;
                if(id && id->type==beatport::Json::Type::String &&
                   id->text!=target.canonical_url) continue;
                if(chosen)
                    throw std::invalid_argument(
                        "More than one matching public Beatport recording");
                chosen=parse_beatport_public_jsonld_object(*item,target,true);
            }
        }
        cursor=close_end+1;
    }
    if(!chosen)
        throw std::invalid_argument(
            "No independently identified MusicRecording JSON-LD on public page");
    return *chosen;
}
} // namespace djmeta::online
