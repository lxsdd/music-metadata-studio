#pragma once
// User-selected public Beatport webpage facts, bounded and source-only.
// No HTTP/HTML automation, login, anti-bot circumvention, cookies, APIs,
// credential reuse, CUE writer, or file modifications in this pure adapter.
// The clipboard contents are USER-SUPPLIED and NOT authenticated Beatport.
#include "djmeta/beatport_manual_intake.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta::online {

// Native query controls must accept the longest canonical Beatport path.
inline constexpr std::size_t maximum_public_beatport_url_length=220;

struct BeatportPublicTrackUrl {
    std::uint64_t id;
    std::string canonical_url;
};
inline BeatportPublicTrackUrl qualify_beatport_public_track_url(
    std::string_view url) {
    constexpr std::string_view prefix="https://www.beatport.com/track/";
    if(!url.starts_with(prefix) ||
       url.size()>maximum_public_beatport_url_length)
        throw std::invalid_argument("Only a public canonical Beatport HTTPS track URL is accepted");
    auto rest=url.substr(prefix.size());
    const auto separator=rest.find('/');
    if(separator==std::string_view::npos || separator==0 ||
       separator>125 || separator+1>=rest.size())
        throw std::invalid_argument("Missing canonical Beatport slug or track ID");
    const auto slug=rest.substr(0,separator);
    for(const char c:slug)
        if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='-'))
            throw std::invalid_argument("Unqualified Beatport track slug");
    const auto numeric=rest.substr(separator+1);
    if(numeric.empty() || numeric.size()>12 || numeric.front()=='0')
        throw std::invalid_argument("Unqualified Beatport public track ID");
    std::uint64_t id=0;
    const auto [end,error]=std::from_chars(
        numeric.data(),numeric.data()+numeric.size(),id);
    if(error!=std::errc{} || end!=numeric.data()+numeric.size() ||
       id==0 || id>beatport::maximum_catalog_id)
        throw std::invalid_argument("Unqualified Beatport public track identity");
    return {id,std::string(url)};
}

inline bool is_beatport_public_page_clipboard(std::string_view raw) {
    return raw.starts_with("@beatport-public-jsonld\n") ||
           raw.starts_with("@beatport-public-jsonld\r\n");
}

inline std::optional<std::int64_t> public_iso_duration_ms(
    std::string_view duration) {
    // MusicRecording JSON-LD uses ISO 8601 durations. A fraction on
    // SECONDS is valid (e.g. PT6M42.125S), not a malformed track duration.
    // Convert with exact integer arithmetic: never lose milliseconds through
    // floating-point rounding or interpret a missing unit heuristically.
    if(duration.size()<4 || duration.size()>24 || !duration.starts_with("PT"))
        return std::nullopt;
    duration.remove_prefix(2);
    std::int64_t milliseconds=0;
    unsigned previous=0;
    while(!duration.empty()) {
        const auto count=duration.find_first_not_of("0123456789");
        if(count==0 || count==std::string_view::npos || count>4)
            return std::nullopt;
        unsigned number=0;
        const auto [end,error]=std::from_chars(
            duration.data(),duration.data()+count,number);
        if(error!=std::errc{} || end!=duration.data()+count)
            return std::nullopt;
        char unit=duration[count];
        std::size_t consumed=count+1;
        unsigned fraction_ms=0;
        if(unit=='.') {
            // Only fractional seconds are supported; precision beyond
            // 1 ms would require lossy rounding, so it remains blocked.
            const auto suffix=duration.substr(count+1);
            const auto digits=suffix.find_first_not_of("0123456789");
            if(digits==0 || digits==std::string_view::npos ||
               digits>3 || suffix[digits]!='S')
                return std::nullopt;
            unsigned value=0;
            const auto [fraction_end,fraction_error]=std::from_chars(
                suffix.data(),suffix.data()+digits,value);
            if(fraction_error!=std::errc{} ||
               fraction_end!=suffix.data()+digits)
                return std::nullopt;
            fraction_ms=value*(digits==1?100u:digits==2?10u:1u);
            unit='S';
            consumed+=digits+1;
        }
        const unsigned order=unit=='H'?1:unit=='M'?2:unit=='S'?3:0;
        if(!order || order<=previous) return std::nullopt;
        previous=order;
        milliseconds+=static_cast<std::int64_t>(number)*
            (unit=='H'?3600000:unit=='M'?60000:1000);
        milliseconds+=fraction_ms;
        duration.remove_prefix(consumed);
    }
    if(milliseconds<1000 || milliseconds>21600000)
        return std::nullopt;
    return milliseconds;
}

inline bool public_music_recording_type(const beatport::Json& obj) {
    if(obj.type!=beatport::Json::Type::Object) return false;
    const auto* type=obj.get("@type");
    if(!type) return false;
    const auto matches=[](std::string_view value) {
        return value=="MusicRecording" ||
               value=="https://schema.org/MusicRecording";
    };
    if(type->type==beatport::Json::Type::String)
        return matches(type->text);
    if(type->type==beatport::Json::Type::Array &&
       type->array.size()<=8)
        return std::any_of(type->array.begin(),type->array.end(),
            [&](const beatport::Json& item) {
                return item.type==beatport::Json::Type::String &&
                       matches(item.text);
            });
    return false;
}

// Browser clipboard and native HTML both require the JSON-LD recording
// to independently attest the exact canonical track URL. The clipboard
// @url locator alone cannot establish source identity. No unrelated
// recording from @graph or a recommendation list may be used.
inline beatport::TrackEvidence parse_beatport_public_jsonld_object(
    const beatport::Json& obj,const BeatportPublicTrackUrl& target,
    bool require_embedded_identity=false) {
    if(!public_music_recording_type(obj))
        throw std::invalid_argument("A MusicRecording JSON-LD object is required");
    bool attested=false;
    for(const std::string_view key:{"url","@id"}) {
        if(const auto* source=obj.get(key)) {
            if(source->type!=beatport::Json::Type::String ||
               source->text!=target.canonical_url)
                throw std::invalid_argument(
                    "Public JSON-LD identity differs from the selected Beatport track");
            attested=true;
        }
    }
    if(require_embedded_identity && !attested)
        throw std::invalid_argument(
            "Public HTML lacks an independent exact track URL in structured data");
    beatport::TrackEvidence track;
    track.id=target.id;
    const auto title=beatport::label(obj,"name",2048);
    if(title.empty())
        throw std::invalid_argument("Public page has no qualified recording title");
    const auto annotated=split_title_mix(title);
    track.title=annotated.version.empty()?title:annotated.base;
    track.mix_name=annotated.version;
    if(const auto* artist=obj.get("byArtist")) {
        if(artist->type==beatport::Json::Type::Array)
            track.artists=beatport::names(artist);
        else {
            const auto name=beatport::object_name(*artist);
            if(!name.empty()) track.artists={name};
        }
    }
    const auto duration=public_iso_duration_ms(musicbrainz::str(obj,"duration"));
    if(!duration || track.artists.empty())
        throw std::invalid_argument(
            "Public page lacks separately qualified artist/duration");
    track.duration_ms=*duration;
    track.isrc=beatport::label(obj,"isrcCode",24);
    if(const auto* genre=obj.get("genre"))
        track.genre=beatport::object_name(*genre);
    // Generic datePublished / inAlbum are NOT verified exact-edition facts.
    // Never create DATE, RELEASEDATE or ALBUM from these ambiguous sources.
    return track;
}
inline beatport::TrackEvidence parse_beatport_public_jsonld_clipboard(
    std::string_view raw) {
    if(!is_beatport_public_page_clipboard(raw) || raw.size()>65536 ||
       raw.find('\0')!=std::string_view::npos)
        throw std::invalid_argument("Unsupported or oversized public page metadata");
    const auto first=raw.find('\n');
    const auto second=raw.find('\n',first+1);
    if(first==std::string_view::npos || second==std::string_view::npos ||
       !raw.substr(first+1,second-first-1).starts_with("@url="))
        throw std::invalid_argument("Public page requires exact @url= header");
    auto url=raw.substr(first+1+5,second-first-1-5);
    if(url.ends_with("\r")) url.remove_suffix(1);
    const auto target=qualify_beatport_public_track_url(url);
    const auto obj=musicbrainz::JsonParser(raw.substr(second+1)).parse();
    return parse_beatport_public_jsonld_object(obj,target,true);
}

} // namespace djmeta::online
