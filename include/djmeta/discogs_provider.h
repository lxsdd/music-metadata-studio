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
inline bool catalog_label(std::string_view value, std::size_t maximum=512) {
    if (value.empty() || value.size()>maximum ||
        !valid_utf8_metadata_text(value)) return false;
    for (const unsigned char c:value)
        if (c<0x20 || c==0x7f) return false;
    return true;
}

// Discogs duration is a display string, never a sample-accurate CUE offset.
// Accept only M:SS or H:MM:SS; malformed or unbounded values stay unknown.
inline std::int64_t duration_ms(std::string_view value) {
    if (value.size()<4 || value.size()>8) return -1;
    const auto colon=value.find(':');
    if (colon==std::string_view::npos) return -1;
    const auto second=value.find(':',colon+1);
    if (second!=std::string_view::npos &&
        value.find(':',second+1)!=std::string_view::npos) return -1;
    const auto number=[](std::string_view text)->std::int64_t {
        if (text.empty() || text.size()>3) return -1;
        std::int64_t n=0;
        const auto [end,error]=std::from_chars(
            text.data(),text.data()+text.size(),n);
        return error==std::errc{} && end==text.data()+text.size()?n:-1;
    };
    const auto first=number(value.substr(0,colon));
    const auto middle=number(value.substr(colon+1,
        second==std::string_view::npos ? value.size()-colon-1 :
        second-colon-1));
    if (first<0 || middle<0) return -1;
    std::int64_t seconds=0;
    if (second==std::string_view::npos) {
        if (value.size()-colon-1!=2 || middle>=60) return -1;
        seconds=first*60+middle;
    } else {
        const auto last=number(value.substr(second+1));
        if (second-colon-1!=2 || value.size()-second-1!=2 ||
            middle>=60 || last<0 || last>=60) return -1;
        seconds=first*3600+middle*60+last;
    }
    return seconds>0 && seconds<=10*60*60 ? seconds*1000 : -1;
}

// Multi-artist primary identity is safe only with explicit Discogs joins.
// Never fabricate a collaboration separator from multiple credited names.
inline std::string joined_artist_credit(const musicbrainz::Json& node) {
    const auto* arr=node.get("artists");
    if(!arr || arr->type!=musicbrainz::Json::Type::Array ||
       arr->array.empty() || arr->array.size()>64) return {};
    std::string joined;
    for(std::size_t i=0;i<arr->array.size();++i) {
        const auto name=string_field(arr->array[i],"name");
        const auto join=string_field(arr->array[i],"join");
        if(!catalog_label(name) ||
           (!join.empty() && !catalog_label(join,64)) ||
           (i+1<arr->array.size() && join.empty()) ||
           (i+1==arr->array.size() && !join.empty())) return {};
        joined+=name;
        joined+=join;
        if(joined.size()>1024) return {};
    }
    return joined;
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
           !catalog_label(v.text)) continue;
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
// Discogs supports 'track' as a distinct database search field. A release
// named differently from a contained track must not require browsing an
// artist tree or repeatedly clearing an artist cache.
inline std::string track_search_path(std::string_view title,
                                     std::string_view artist = {}) {
    std::string path="/database/search?track="+
        musicbrainz::encode_url_query(validated_term(title))+
        "&type=release";
    if (!artist.empty())
        path+="&artist="+musicbrainz::encode_url_query(validated_term(artist));
    path+="&page=1&per_page=20";
    if(path.size()>2048)
        throw std::invalid_argument("Discogs query too large");
    return path;
}
struct SearchAttempt {
    std::string label; // Displayable reason for this fixed official request.
    std::string path;  // Only official relative API paths. No tokens or URL I/O.
};
inline std::vector<SearchAttempt> track_search_plan(
    std::string_view title, std::string_view artist = {}) {
    validated_term(title);
    if (!artist.empty()) validated_term(artist);
    const auto annotation = split_title_mix(title);
    const auto core = annotation.version.empty()
        ? title : annotation.base;
    std::vector<SearchAttempt> plan;
    const auto add = [&](std::string label, std::string path) {
        for (const auto& previous : plan)
            if (previous.path == path) return;
        plan.push_back({std::move(label),std::move(path)});
    };
    // First search narrow and cheap, then broaden only on explicit request
    // when previous attempts have no qualified hits. Never bulk crawl.
    add("Release title with artist", search_path(title,artist));
    add("Exact track in release", track_search_path(title,artist));
    if (!artist.empty())
        add("Track without artist restriction",track_search_path(title));
    if (core != title)
        add("Track without mix suffix",track_search_path(core));
    add("Broad catalog keywords",search_path(core));
    return plan;
}
// Canonical pasted Discogs URLs can directly select a release, bypassing the
// search index. Never fetch the supplied URL: only extract an integer ID and
// construct our own fixed API release_path after qualification.
inline std::uint64_t release_id_from_url(std::string_view url) {
    if (url.empty() || url.size()>2048) return 0;
    for (unsigned char c:url)
        if (c<0x20 || c==0x7f) return 0;
    const std::string_view origins[]={
        "https://www.discogs.com/release/",
        "https://discogs.com/release/",
        "https://www.discogs.com/releases/",
        "https://discogs.com/releases/"
    };
    std::string_view rest;
    bool recognized=false;
    for (auto base:origins)
        if (url.starts_with(base)) {
            rest=url.substr(base.size());
            recognized=true;
            break;
        }
    if(!recognized || rest.empty()) return 0;
    const auto length=rest.find_first_not_of("0123456789");
    const auto digits=rest.substr(0,length);
    if(digits.empty() || digits.size()>12) return 0;
    if(length!=std::string_view::npos &&
       rest[length]!='-' && rest[length]!='/' &&
       rest[length]!='?' && rest[length]!='#') return 0;
    std::uint64_t id=0;
    const auto [end,error]=std::from_chars(
        digits.data(),digits.data()+digits.size(),id);
    return error==std::errc{} && end==digits.data()+digits.size() &&
           id>0 && id<=999999999999ULL ? id : 0;
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
        if(!id || !catalog_label(title,2048)) continue;
        SearchHit hit;
        hit.kind=kind=="master"?EntityKind::Master:EntityKind::Release;
        hit.id=id;hit.title=title;
        hit.artist=string_field(row,"artist");
        if(!hit.artist.empty() && !catalog_label(hit.artist,1024))
            hit.artist.clear();
        hit.year=year_field(row);
        hit.country=string_field(row,"country");
        if(!hit.country.empty() && !catalog_label(hit.country,128))
            hit.country.clear();
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
    if(!catalog_label(title,2048))
        throw std::invalid_argument("Discogs edition has no safe title");
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
    // Preserve a Discogs collaboration as one credited artist. Multiple
    // names without explicit joins are ambiguous and must not be guessed.
    const auto album_artist=joined_artist_credit(root);
    if(!album_artist.empty()) add("ALBUM ARTIST",{album_artist});
    const auto* labels=root.get("labels");
    std::vector<std::string> label_names,catalogues;
    if(labels&&labels->type==musicbrainz::Json::Type::Array)
        for(const auto& label:labels->array) {
            if(label_names.size()>=64) break;
            const auto name=string_field(label,"name");
            const auto cat=string_field(label,"catno");
            if(catalog_label(name) && std::find(label_names.begin(),label_names.end(),name)==label_names.end())
                label_names.push_back(name);
            if(catalog_label(cat) && std::find(catalogues.begin(),catalogues.end(),cat)==catalogues.end())
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
        if(track.type!=musicbrainz::Json::Type::Object) continue;
        const auto kind=string_field(track,"type_");
        // Index/heading/subtrack containers are NOT ordinary track rows.
        // Unknown types are never silently flattened into a false track.
        if(!kind.empty() && kind!="track") continue;
        const auto track_title=string_field(track,"title");
        if(!catalog_label(track_title,2048)) continue;
        EditionTrack item;
        item.source_track_id=string_field(track,"position");
        if(!item.source_track_id.empty() &&
           !catalog_label(item.source_track_id,128))
            item.source_track_id.clear();
        item.recording.title=track_title;
        item.recording.duration_ms=duration_ms(string_field(track,"duration"));
        // Explicit per-track credits override album credits; malformed or
        // ambiguous multi-artist joins must not fall back to album identity.
        item.recording.primary_artist=track.get("artists")
            ? joined_artist_credit(track) : joined_artist_credit(root);
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
