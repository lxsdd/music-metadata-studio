#pragma once

// Discogs official public catalog, AUTHENTICATION-GATED read-only adapter.
// Only documents request paths and parses bounded official JSON. No network,
// token storage, embedded credentials, filesystem access or mutation here.
// Catalog fields have explicit edition provenance. A master is never treated
// as an exact release edition, and absence NEVER proposes clearing a tag.
#include "djmeta/musicbrainz_provider.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta::online::discogs {

enum class EntityKind { Release, Master };
struct SearchHit {
    EntityKind kind = EntityKind::Release;
    std::uint64_t id = 0;
    std::string title; // Discogs search title can contain artist and release.
    std::string artist; // Empty unless Discogs explicitly provides it.
    std::string country;
    std::string year;
    std::vector<std::string> genres;
    std::vector<std::string> styles;
    std::string canonical_url; // Synthesized URL, never trusted remote URL.
};
struct SearchResults { std::vector<SearchHit> hits; };

inline constexpr std::string_view attribution =
    "This application uses Discogs' API but is not affiliated with, sponsored or endorsed by Discogs.";
inline constexpr std::string_view data_attribution = "Data provided by Discogs.";

inline std::uint64_t object_id(const musicbrainz::Json& value) {
    if (value.type != musicbrainz::Json::Type::Number) return 0;
    const auto& text = value.text;
    std::uint64_t n=0;
    const auto [end,error] = std::from_chars(text.data(),text.data()+text.size(),n);
    if (error!=std::errc{} || end!=text.data()+text.size() ||
        n==0 || n>999999999999ULL) return 0;
    return n;
}
inline std::uint64_t id_field(const musicbrainz::Json& node) {
    const auto* id=node.get("id");
    return id?object_id(*id):0;
}
inline std::string string_field(const musicbrainz::Json& node,std::string_view key) {
    const auto* v=node.get(key);
    return v&&v->type==musicbrainz::Json::Type::String?v->text:std::string{};
}
inline std::string year_field(const musicbrainz::Json& node) {
    const auto* v=node.get("year");
    if(!v || v->type!=musicbrainz::Json::Type::Number ||
       v->text.size()!=4) return {};
    int year=0;
    const auto [end,error]=std::from_chars(
        v->text.data(),v->text.data()+v->text.size(),year);
    if(error!=std::errc{} || end!=v->text.data()+v->text.size() ||
       year<1850 || year>2200) return {};
    return v->text; // Edition year only, never an original-recording date.
}
inline std::vector<std::string> string_array(
    const musicbrainz::Json& node,std::string_view key,std::size_t max=32) {
    std::vector<std::string> out;
    const auto* array=node.get(key);
    if(!array || array->type!=musicbrainz::Json::Type::Array) return out;
    for(const auto& v:array->array) {
        if(out.size()==max) break;
        if(v.type!=musicbrainz::Json::Type::String ||
           v.text.empty() || v.text.size()>512) continue;
        if(std::find(out.begin(),out.end(),v.text)==out.end())
            out.push_back(v.text);
    }
    return out;
}
inline std::string canonical_url(EntityKind kind,std::uint64_t id) {
    if(!id || id>999999999999ULL)
        throw std::invalid_argument("Unqualified Discogs object identifier");
    return std::string("https://www.discogs.com/")+
        (kind==EntityKind::Master?"master/":"release/")+std::to_string(id);
}
// The /database/search endpoint requires user-provided authorization.
// The token MUST be passed in a separate native HTTPS request header by
// the future host adapter, never in this URI, code, logs or git.
inline std::string validated_term(std::string_view source) {
    if(source.empty() || source.size()>150 ||
       !valid_utf8_metadata_text(source))
        throw std::invalid_argument("Invalid Discogs search text");
    for(const unsigned char c:source)
        if(c<0x20 || c==0x7f)
            throw std::invalid_argument("Control character in Discogs query");
    return std::string(source);
}
inline std::string search_path(std::string_view title,
                               std::string_view artist = {}) {
    const auto q=validated_term(title);
    std::string result="/database/search?q="+
        musicbrainz::encode_url_query(q)+"&type=release";
    if(!artist.empty())
        result+="&artist="+musicbrainz::encode_url_query(
            validated_term(artist));
    result+="&page=1&per_page=20";
    if(result.size()>2048)
        throw std::invalid_argument("Discogs query too large");
    return result;
}
inline std::string release_path(std::uint64_t id) {
    if(!id || id>999999999999ULL)
        throw std::invalid_argument("Unqualified Discogs release identifier");
    return "/releases/"+std::to_string(id);
}
// These are search candidates, never host track write targets.
inline SearchResults parse_search(std::string_view json) {
    SearchResults result;
    const auto root=musicbrainz::JsonParser(json).parse();
    const auto* rows=root.get("results");
    if(!rows || rows->type!=musicbrainz::Json::Type::Array)
        throw std::invalid_argument("Discogs response lacks a result list");
    for(const auto& row:rows->array) {
        if(result.hits.size()>=20) break;
        if(row.type!=musicbrainz::Json::Type::Object) continue;
        const auto kind=string_field(row,"type");
        if(kind!="release" && kind!="master") continue;
        const auto id=id_field(row);
        const auto title=string_field(row,"title");
        if(!id || title.empty() || title.size()>2048) continue;
        SearchHit hit;
        hit.kind=kind=="master"?EntityKind::Master:EntityKind::Release;
        hit.id=id;hit.title=title;
        hit.artist=string_field(row,"artist");
        hit.year=year_field(row);
        hit.country=string_field(row,"country");
        hit.genres=string_array(row,"genre");
        hit.styles=string_array(row,"style");
        hit.canonical_url=canonical_url(hit.kind,id);
        result.hits.push_back(std::move(hit));
    }
    return result;
}
inline ReleaseEdition parse_release(std::string_view json,std::uint64_t expected_id) {
    if(!expected_id || expected_id>999999999999ULL)
        throw std::invalid_argument("Missing expected Discogs edition ID");
    const auto root=musicbrainz::JsonParser(json).parse();
    if(id_field(root)!=expected_id)
        throw std::invalid_argument("Discogs edition identity mismatch");
    const std::string title=string_field(root,"title");
    if(title.empty())
        throw std::invalid_argument("Discogs edition has no title");
    ReleaseEdition edition;
    edition.provider="discogs";
    edition.edition_id=std::to_string(expected_id);
    const auto add=[&](std::string field,std::vector<std::string> values,
                       DateMeaning meaning=DateMeaning::NotDate) {
        if(values.empty()) return;
        edition.fields.push_back({std::move(field),std::move(values),"discogs",
            edition.edition_id,EvidenceScope::Edition,meaning});
    };
    add("ALBUM",{title});
    add("DATE",year_field(root).empty()?std::vector<std::string>{}:
        std::vector<std::string>{year_field(root)},DateMeaning::EditionRelease);
    add("GENRE",string_array(root,"genres"));
    add("STYLE",string_array(root,"styles"));
    const auto* artists=root.get("artists");
    std::vector<std::string> credited;
    if(artists&&artists->type==musicbrainz::Json::Type::Array)
        for(const auto& artist:artists->array) {
            const auto name=string_field(artist,"name");
            if(!name.empty() && credited.size()<64) credited.push_back(name);
        }
    add("ALBUM ARTIST",credited);
    const auto* labels=root.get("labels");
    std::vector<std::string> label_names,catalogues;
    if(labels&&labels->type==musicbrainz::Json::Type::Array)
        for(const auto& label:labels->array) {
            if(label_names.size()>=64) break;
            const auto name=string_field(label,"name");
            const auto cat=string_field(label,"catno");
            if(!name.empty() && std::find(label_names.begin(),label_names.end(),name)==label_names.end())
                label_names.push_back(name);
            if(!cat.empty() && std::find(catalogues.begin(),catalogues.end(),cat)==catalogues.end())
                catalogues.push_back(cat);
        }
    add("LABEL",label_names);
    add("CATALOGNUMBER",catalogues);
    const auto* tracks=root.get("tracklist");
    if(!tracks || tracks->type!=musicbrainz::Json::Type::Array)
        throw std::invalid_argument("Discogs edition is missing tracklist");
    for(const auto& track:tracks->array) {
        if(edition.tracks.size()>=256)
            throw std::invalid_argument("Discogs tracklist exceeds safe limit");
        if(track.type!=musicbrainz::Json::Type::Object ||
           string_field(track,"type_")=="heading") continue;
        const auto track_title=string_field(track,"title");
        if(track_title.empty()) continue;
        EditionTrack item;
        item.source_track_id=string_field(track,"position");
        item.recording.title=track_title;
        item.recording.primary_artist=credited.size()==1?credited[0]:std::string{};
        const auto* credits=track.get("artists");
        if(credits && credits->type==musicbrainz::Json::Type::Array &&
           credits->array.size()==1)
            item.recording.primary_artist=string_field(credits->array[0],"name");
        if(!item.source_track_id.empty()) {
            int position=0;
            const auto [end,error]=std::from_chars(
                item.source_track_id.data(),
                item.source_track_id.data()+item.source_track_id.size(),position);
            if(error==std::errc{} &&
               end==item.source_track_id.data()+item.source_track_id.size() &&
               position>0 && position<=256)
                item.track_number=position;
            // "A1", "1-01", etc. are preserved only as source IDs, NEVER
            // converted to fictitious numerical disc/track positions.
        }
        edition.tracks.push_back(std::move(item));
    }
    if(edition.tracks.empty())
        throw std::invalid_argument("Discogs edition has no qualified tracks");
    return edition;
}
} // namespace djmeta::online::discogs
