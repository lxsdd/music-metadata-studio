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
#include <set>
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
    // Official master-version page: candidate attributes, never tag fields.
    std::string label, catalog_number, format;
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

// Pacing is mandatory for a user-initiated zero-hit search: the next
// bounded stage must wait, not immediately reject a valid request.
inline constexpr std::uint64_t minimum_request_spacing_ms=1600;
inline std::uint64_t request_spacing_wait_ms(
    std::uint64_t last_tick,std::uint64_t now_tick) noexcept {
    if(last_tick==0) return 0; // First request.
    if(now_tick<last_tick) return minimum_request_spacing_ms;
    const auto elapsed=now_tick-last_tick;
    return elapsed>=minimum_request_spacing_ms?0:
        minimum_request_spacing_ms-elapsed;
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
// A full release date describes this edition, never the first recording date.
inline std::string released_full_date(const musicbrainz::Json& node) {
    const auto value=string_field(node,"released");
    if(value.size()!=10 || value[4]!='-' || value[7]!='-') return {};
    for(std::size_t i=0;i<value.size();++i)
        if(i!=4 && i!=7 && (value[i]<'0' || value[i]>'9')) return {};
    const auto number=[&](std::size_t pos,std::size_t len) {
        int result=0;
        const auto [end,error]=std::from_chars(
            value.data()+pos,value.data()+pos+len,result);
        return error==std::errc{} && end==value.data()+pos+len?result:-1;
    };
    const int year=number(0,4),month=number(5,2),day=number(8,2);
    if(year<1850 || year>2200 || month<1 || month>12) return {};
    constexpr int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap=year%4==0 && (year%100!=0 || year%400==0);
    if(day<1 || day>days[month-1]+(month==2 && leap?1:0)) return {};
    const auto declared_year=year_field(node);
    if(!declared_year.empty() && declared_year!=value.substr(0,4))
        return {};
    return value;
}
// Barcodes are textual identities; keep leading zeros and supplied spacing.
inline std::vector<std::string> edition_barcodes(const musicbrainz::Json& node) {
    std::vector<std::string> values;
    const auto* ids=node.get("identifiers");
    if(!ids || ids->type!=musicbrainz::Json::Type::Array ||
       ids->array.size()>256) return values;
    for(const auto& id:ids->array) {
        if(id.type!=musicbrainz::Json::Type::Object ||
           string_field(id,"type")!="Barcode") continue;
        const auto value=string_field(id,"value");
        if(!catalog_label(value,128)) continue;
        if(std::find(values.begin(),values.end(),value)==values.end())
            values.push_back(value);
        if(values.size()>=64) break;
    }
    return values;
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
// Discogs extraartists are track-specific credits. Map only exact, known
// roles; never guess a remixer from a mix title or inherit release-wide credits.
// Multiple independently credited people remain separate metadata values.
inline std::vector<std::string> explicit_track_credits(
    const musicbrainz::Json& track, std::string_view field) {
    const auto* entries=track.get("extraartists");
    if(!entries || entries->type!=musicbrainz::Json::Type::Array ||
       entries->array.size()>128) return {};
    std::vector<std::string> credits;
    for(const auto& entry:entries->array) {
        if(entry.type!=musicbrainz::Json::Type::Object) continue;
        const auto role=string_field(entry,"role");
        const bool remixer=field=="REMIXER" &&
            (role=="Remix" || role=="Remix By");
        const bool songwriter=field=="SONGWRITER" &&
            (role=="Written-By" || role=="Written By" || role=="Composed By");
        if(!remixer && !songwriter) continue;
        const auto name=string_field(entry,"name");
        if(!catalog_label(name,512)) continue;
        if(std::find(credits.begin(),credits.end(),name)==credits.end())
            credits.push_back(name);
        if(credits.size()>=32) break;
    }
    return credits;
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
// Official master candidates are navigation only. A master is never an
// exact edition and its catalog fields must not become tag proposals.
inline std::string master_search_path(std::string_view keywords) {
    const auto result="/database/search?q="+
        musicbrainz::encode_url_query(validated_term(keywords))+
        "&type=master&page=1&per_page=20";
    if(result.size()>2048)
        throw std::invalid_argument("Discogs master query too large");
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
// The documented release_title filter searches a release's album name,
// rather than the track title or the combined artist - release title field.
// Search hits are still unverified candidates until exact edition lookup.
inline std::string release_title_search_path(std::string_view album,
                                             std::string_view artist = {}) {
    std::string path="/database/search?release_title="+
        musicbrainz::encode_url_query(validated_term(album))+
        "&type=release";
    if(!artist.empty())
        path+="&artist="+musicbrainz::encode_url_query(validated_term(artist));
    path+="&page=1&per_page=20";
    if(path.size()>2048)
        throw std::invalid_argument("Discogs release-title request too large");
    return path;
}
// Discogs documents barcode and catno as edition search filters.
 // Both identifiers are text, never converted to numbers (leading zeros
 // and punctuation in a catalog number are significant).
inline std::string edition_identifier_search_path(
    std::string_view kind, std::string_view value) {
    if(kind!="barcode" && kind!="catno")
        throw std::invalid_argument("Unsupported Discogs identifier filter");
    const auto validated=validated_term(value);
    const auto result="/database/search?"+std::string(kind)+"="+
        musicbrainz::encode_url_query(validated)+
        "&type=release&page=1&per_page=20";
    if(result.size()>2048)
        throw std::invalid_argument("Discogs identifier request too large");
    return result;
}
struct SearchAttempt {
    std::string label; // Displayable reason for this fixed official request.
    std::string path;  // Only official relative API paths. No tokens or URL I/O.
};
inline std::vector<SearchAttempt> track_search_plan(
    std::string_view title, std::string_view artist = {},
    std::string_view barcode = {}, std::string_view catalog_number = {},
    std::string_view album = {}, bool include_master = false) {
    // Some physical sources carry a qualified ALBUM, barcode or catalog
    // number but no TITLE. Those can still search exact editions without
    // inventing a recording identity or sending an empty track filter.
    if (!title.empty()) validated_term(title);
    if (!artist.empty()) validated_term(artist);
    if (!album.empty()) validated_term(album);
    const auto annotation = split_title_mix(title);
    const auto core = annotation.version.empty()
        ? title : annotation.base;
    std::vector<SearchAttempt> plan;
    const auto add = [&](std::string label, std::string path) {
        for (const auto& previous : plan)
            if (previous.path == path) return;
        plan.push_back({std::move(label),std::move(path)});
    };
    // Exact physical edition identifiers get priority only when the host
    // supplied unambiguous tags and the user explicitly initiates Search.
    // No result is treated as verified until its release detail is opened.
    if(!barcode.empty())
        add("Exact edition barcode",
            edition_identifier_search_path("barcode",barcode));
    if(!catalog_number.empty())
        add("Exact edition catalog number",
            edition_identifier_search_path("catno",catalog_number));
    // A qualified physical ALBUM or CUE-global TITLE is independent of
    // the recording TITLE. Try the documented album-specific filter first.
    // An empty, ambiguous or invalid local album must be omitted by the host.
    if(!album.empty())
        add("Tagged album release title",release_title_search_path(album,artist));
    // Then broaden by staged release / track search on deliberate user
    // initiation; empty responses can advance within the same bounded plan.
    if (!title.empty()) {
        add("Release title with artist", search_path(title,artist));
        add("Exact track in release", track_search_path(title,artist));
        if (!artist.empty())
            add("Track without artist restriction",track_search_path(title));
        if (core != title)
            add("Track without mix suffix",track_search_path(core));
        add("Broad catalog keywords",search_path(core));
        // Only the native user-initiated workflow opts in: masters may
        // reveal editions that release/track index queries did not find.
        if(include_master)
            add("Master edition discovery (navigation only)",
                master_search_path(core));
    }
    if (plan.empty())
        throw std::invalid_argument(
            "Discogs search requires a track title, album or exact edition identifier");
    return plan;
}
// Advance only after a successfully published search response. Failed
// HTTPS requests must retry the same stage, never silently skip it.
// An unchanged query with nonempty hits can still be broadened explicitly.
inline std::size_t next_search_attempt(
    std::string_view completed_identity, std::string_view requested_identity,
    std::size_t completed_index, std::size_t plan_size) {
    if(plan_size==0 || plan_size>16)
        throw std::invalid_argument("Unqualified Discogs search plan");
    if(completed_identity!=requested_identity) return 0;
    if(completed_index>=plan_size)
        throw std::invalid_argument("Expired Discogs search stage");
    return completed_index+1==plan_size?0:completed_index+1;
}
// Canonical pasted Discogs URLs can directly select a release, bypassing the
// search index. Never fetch the supplied URL: only extract an integer ID and
// construct our own fixed API release_path after qualification.
inline std::uint64_t catalog_id_from_url(std::string_view url, bool master) {
    if (url.empty() || url.size()>2048) return 0;
    for (unsigned char c:url)
        if (c<0x20 || c==0x7f) return 0;
    const std::string_view releases[]={
        "https://www.discogs.com/release/",
        "https://discogs.com/release/",
        "https://www.discogs.com/releases/",
        "https://discogs.com/releases/"
    };
    const std::string_view masters[]={
        "https://www.discogs.com/master/",
        "https://discogs.com/master/",
        "https://www.discogs.com/masters/",
        "https://discogs.com/masters/"
    };
    std::string_view rest;
    bool recognized=false;
    for (std::size_t i=0;i<4;++i) {
        const auto base=master?masters[i]:releases[i];
        if (url.starts_with(base)) {
            rest=url.substr(base.size());
            recognized=true;
            break;
        }
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
inline std::uint64_t release_id_from_url(std::string_view url) {
    return catalog_id_from_url(url,false);
}
inline std::uint64_t master_id_from_url(std::string_view url) {
    return catalog_id_from_url(url,true);
}
// Selection verification is pure and can be tested without an installed
// foobar HWND. A release response belongs only to its still-highlighted
// exact edition, never merely to the original source file or search batch.
inline bool selected_exact_release_id(
    std::string_view selected_source_id, std::uint64_t expected_id) {
    if(!expected_id || expected_id>999999999999ULL ||
       !selected_source_id.starts_with("release:")) return false;
    const auto digits=selected_source_id.substr(8);
    if(digits.empty() || digits.size()>12 || digits.front()=='0') return false;
    std::uint64_t id=0;
    const auto [end,error]=std::from_chars(
        digits.data(),digits.data()+digits.size(),id);
    return error==std::errc{} && end==digits.data()+digits.size() &&
           id==expected_id;
}
inline std::string release_path(std::uint64_t id) {
    if(!id || id>999999999999ULL)
        throw std::invalid_argument("Unqualified Discogs release identifier");
    return "/releases/"+std::to_string(id);
}
// Every additional page is separately user-initiated; never scrape/scan all.
inline constexpr std::uint64_t max_master_version_page=100000;
inline std::string master_versions_path(std::uint64_t id,
                                        std::uint64_t page=1) {
    if(!id || id>999999999999ULL || !page ||
       page>max_master_version_page)
        throw std::invalid_argument("Unqualified Discogs master page");
    return "/masters/"+std::to_string(id)+"/versions?page="+
        std::to_string(page)+"&per_page=20";
}
// The host HTTPS adapter may accept ONLY canonical paths generated by this
// provider. Even if an upstream UI passes a string, it cannot change host,
// method, query keys, pagination, identity or encoded control bytes.
inline bool decode_canonical_term(std::string_view encoded, std::string& decoded) {
    if (encoded.empty() || encoded.size()>900) return false;
    decoded.clear();
    const auto hex=[](char c)->int {
        if(c>='0' && c<='9') return c-'0';
        if(c>='A' && c<='F') return c-'A'+10;
        return -1;
    };
    for(std::size_t i=0;i<encoded.size();++i) {
        const char c=encoded[i];
        if(c=='%') {
            if(i+2>=encoded.size()) return false;
            const int h=hex(encoded[i+1]),l=hex(encoded[i+2]);
            if(h<0 || l<0) return false;
            decoded.push_back(static_cast<char>((h<<4)|l));
            i+=2;
        } else if ((c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
                   (c>='0'&&c<='9') || c=='-' || c=='_' ||
                   c=='.' || c=='~') {
            decoded.push_back(c);
        } else return false;
    }
    return musicbrainz::encode_url_query(decoded)==encoded;
}
inline bool allowed_readonly_path(std::string_view path) {
    if(path.empty() || path.size()>2048) return false;
    constexpr std::string_view release_prefix="/releases/";
    if(path.starts_with(release_prefix)) {
        const auto digits=path.substr(release_prefix.size());
        if(digits.empty() || digits.size()>12) return false;
        std::uint64_t parsed=0;
        const auto [end,error]=std::from_chars(
            digits.data(),digits.data()+digits.size(),parsed);
        return error==std::errc{} &&
               end==digits.data()+digits.size() &&
               parsed>0 && parsed<=999999999999ULL &&
               path==release_path(parsed);
    }
    constexpr std::string_view master_prefix="/masters/";
    if(path.starts_with(master_prefix)) {
        const auto rest=path.substr(master_prefix.size());
        const auto separator=rest.find('/');
        if(separator==std::string_view::npos) return false;
        const auto digits=rest.substr(0,separator);
        if(digits.empty() || digits.size()>12) return false;
        std::uint64_t id=0;
        const auto [end,error]=std::from_chars(
            digits.data(),digits.data()+digits.size(),id);
        if(error!=std::errc{} || end!=digits.data()+digits.size() ||
           !id || id>999999999999ULL) return false;
        constexpr std::string_view page_prefix="/versions?page=";
        constexpr std::string_view page_suffix="&per_page=20";
        const auto route=rest.substr(separator);
        if(!route.starts_with(page_prefix) || !route.ends_with(page_suffix))
            return false;
        const auto value=route.substr(page_prefix.size(),
            route.size()-page_prefix.size()-page_suffix.size());
        if(value.empty() || value.size()>6) return false;
        std::uint64_t page=0;
        const auto [finish,err]=std::from_chars(
            value.data(),value.data()+value.size(),page);
        return err==std::errc{} &&
               finish==value.data()+value.size() &&
               page>0 && page<=max_master_version_page &&
               path==master_versions_path(id,page);
    }
    constexpr std::string_view prefix="/database/search?";
    constexpr std::string_view suffix="&page=1&per_page=20";
    constexpr std::string_view type_release="&type=release";
    constexpr std::string_view type_master="&type=master";
    if(!path.starts_with(prefix) || !path.ends_with(suffix)) return false;
    auto inner=path.substr(prefix.size(),
        path.size()-prefix.size()-suffix.size());
    const auto release_marker=inner.find(type_release);
    const bool master=release_marker==std::string_view::npos;
    const auto marker=master?inner.find(type_master):release_marker;
    if(marker==std::string_view::npos) return false;
    const auto raw_term=inner.substr(0,marker);
    const auto tail=inner.substr(marker+(master?type_master.size():type_release.size()));
    constexpr std::string_view artist_prefix="&artist=";
    if(!tail.empty() && !tail.starts_with(artist_prefix)) return false;
    const auto raw_artist=tail.empty()?std::string_view{}:
        tail.substr(artist_prefix.size());
    std::string term,artist;
    const bool track=raw_term.starts_with("track=");
    const bool q=raw_term.starts_with("q=");
    const bool barcode=raw_term.starts_with("barcode=");
    const bool catno=raw_term.starts_with("catno=");
    const bool release_title=raw_term.starts_with("release_title=");
    if(!track && !q && !barcode && !catno && !release_title) return false;
    if(master && (!q || !tail.empty())) return false;
    // Edition identifiers are standalone official filters; never append
    // arbitrary query keys, artist constraints, tokens or pagination.
    if((barcode || catno) && !tail.empty()) return false;
    const auto raw_value=raw_term.substr(
        track?6:q?2:barcode?8:catno?6:14);
    if(!decode_canonical_term(raw_value,term) ||
       (!raw_artist.empty() &&
        !decode_canonical_term(raw_artist,artist)) ||
       (!tail.empty() && raw_artist.empty())) return false;
    try {
        if(master)
            return path==master_search_path(validated_term(term));
        if(barcode || catno)
            return path==edition_identifier_search_path(
                barcode?"barcode":"catno",validated_term(term));
        if(release_title)
            return path==release_title_search_path(validated_term(term),artist);
        return path==(track?track_search_path(validated_term(term),artist):
                            search_path(validated_term(term),artist));
    } catch(const std::invalid_argument&) {
        return false;
    }
}

// These are search candidates, never host track write targets.
inline SearchResults parse_search(std::string_view json) {
    SearchResults result;
    const auto root=musicbrainz::JsonParser(json).parse();
    const auto* rows=root.get("results");
    if(!rows || rows->type!=musicbrainz::Json::Type::Array)
        throw std::invalid_argument("Discogs response lacks a result list");
    std::set<std::pair<EntityKind, std::uint64_t>> seen;
    for(const auto& row:rows->array) {
        if(result.hits.size()>=20) break;
        if(row.type!=musicbrainz::Json::Type::Object) continue;
        const auto kind=string_field(row,"type");
        if(kind!="release" && kind!="master") continue;
        const auto id=id_field(row);
        const auto title=string_field(row,"title");
        if(!id || !catalog_label(title,2048)) continue;
        const auto entity_kind=kind=="master"?EntityKind::Master:EntityKind::Release;
        // Search pagination can repeat the same catalog identity. Keep the
        // first qualified row, but never collapse a master into a release.
        if(!seen.emplace(entity_kind,id).second) continue;
        SearchHit hit;
        hit.kind=entity_kind;
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
// A master's paginated versions are candidate editions, never master fields.
struct MasterVersionsPage {
    SearchResults results;
    std::uint64_t page=1;
    std::uint64_t pages=1;
};
inline MasterVersionsPage parse_master_versions_page(
    std::string_view json,std::uint64_t expected_master_id,
    std::uint64_t expected_page=1) {
    if(!expected_master_id || expected_master_id>999999999999ULL ||
       !expected_page || expected_page>max_master_version_page)
        throw std::invalid_argument("Unqualified Discogs master page");
    const auto root=musicbrainz::JsonParser(json).parse();
    const auto* pagination=root.get("pagination");
    const auto* versions=root.get("versions");
    if(!pagination || pagination->type!=musicbrainz::Json::Type::Object ||
       !versions || versions->type!=musicbrainz::Json::Type::Array ||
       versions->array.size()>20)
        throw std::invalid_argument("Discogs master versions page unqualified");
    const auto* page=pagination->get("page");
    const auto* per_page=pagination->get("per_page");
    const auto* pages=pagination->get("pages");
    const auto total_pages=pages?object_id(*pages):0;
    if(!page || !per_page || !pages ||
       object_id(*page)!=expected_page || object_id(*per_page)!=20 ||
       !total_pages || total_pages<expected_page)
        throw std::invalid_argument("Discogs master pagination unqualified");
    SearchResults results;
    std::set<std::uint64_t> seen;
    for(const auto& row:versions->array) {
        if(row.type!=musicbrainz::Json::Type::Object) continue;
        const auto id=id_field(row);
        const auto title=string_field(row,"title");
        if(!id || !catalog_label(title,2048) || !seen.insert(id).second)
            continue;
        SearchHit hit;
        hit.kind=EntityKind::Release;
        hit.id=id;
        hit.title=title;
        hit.canonical_url=canonical_url(EntityKind::Release,id);
        hit.country=string_field(row,"country");
        if(!hit.country.empty() && !catalog_label(hit.country,128))
            hit.country.clear();
        hit.label=string_field(row,"label");
        if(!hit.label.empty() && !catalog_label(hit.label,512))
            hit.label.clear();
        hit.catalog_number=string_field(row,"catno");
        if(!hit.catalog_number.empty() &&
           !catalog_label(hit.catalog_number,256))
            hit.catalog_number.clear();
        hit.format=string_field(row,"format");
        if(!hit.format.empty() && !catalog_label(hit.format,512))
            hit.format.clear();
        const auto released=string_field(row,"released");
        if(released.size()>=4 &&
           released.substr(0,4).find_first_not_of("0123456789")==
               std::string::npos) {
            int year=0;
            const auto [end,error]=std::from_chars(
                released.data(),released.data()+4,year);
            if(error==std::errc{} && end==released.data()+4 &&
               year>=1850 && year<=2200)
                hit.year=released.substr(0,4);
        }
        results.hits.push_back(std::move(hit));
    }
    return {std::move(results),expected_page,total_pages};
}
inline SearchResults parse_master_versions(
    std::string_view json,std::uint64_t expected_master_id) {
    return parse_master_versions_page(json,expected_master_id).results;
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
    // Distinguish exact edition date from the existing year-only DATE field.
    const auto full_date=released_full_date(root);
    if(!full_date.empty())
        add("RELEASEDATE",{full_date},DateMeaning::EditionRelease);
    const auto country=string_field(root,"country");
    if(catalog_label(country,128)) add("COUNTRY",{country});
    add("BARCODE",edition_barcodes(root));
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
        // Track-level evidence must refer to this exact edition row. A
        // fallback album artist is useful for matching, but is NOT proof
        // that Discogs independently credited this track's ARTIST field.
        const std::string track_source=edition.edition_id+"/track/"+
            std::to_string(edition.tracks.size()+1);
        const auto add_track=[&](std::string field,std::string value) {
            if(!value.empty())
                item.fields.push_back({std::move(field),{std::move(value)},
                    "discogs",track_source,EvidenceScope::Edition,
                    DateMeaning::NotDate});
        };
        add_track("TITLE",track_title);
        if(track.get("artists"))
            add_track("ARTIST",joined_artist_credit(track));
        if(item.track_number>0)
            add_track("TRACKNUMBER",std::to_string(item.track_number));
        for(const auto* field : {"REMIXER","SONGWRITER"}) {
            auto credits=explicit_track_credits(track,field);
            if(!credits.empty())
                item.fields.push_back({field,std::move(credits),"discogs",
                    track_source,EvidenceScope::Edition,DateMeaning::NotDate});
        }
        edition.tracks.push_back(std::move(item));
    }
    if(edition.tracks.empty())
        throw std::invalid_argument("Discogs edition has no qualified tracks");
    return edition;
}
} // namespace djmeta::online::discogs
