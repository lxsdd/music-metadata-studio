#pragma once
// One deliberately copied, browser-visible Crates recording. Offline facts
// only; no Crates API, cookies, browser session, network or write capability.
// Crates is a distinct, user-supplied source, NEVER Beatport API evidence.
#include "djmeta/beatport_public_page.h"

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta::online {

inline bool is_crates_public_track_clipboard(std::string_view raw) {
    return raw.starts_with("@crates-public-track\n") ||
           raw.starts_with("@crates-public-track\r\n");
}

inline beatport::TrackEvidence parse_crates_public_track_clipboard(
    std::string_view raw) {
    if(!is_crates_public_track_clipboard(raw) || raw.size()>65536 ||
       raw.find('\0')!=std::string_view::npos ||
       !valid_utf8_metadata_text(raw))
        throw std::invalid_argument("Invalid Crates visible-track clipboard");
    const auto first=raw.find('\n');
    const auto second=raw.find('\n',first+1);
    const auto third=raw.find('\n',second+1);
    if(first==std::string_view::npos ||
       second==std::string_view::npos ||
       third==std::string_view::npos)
        throw std::invalid_argument("Crates source requires two independent URLs");
    const auto page=strip_line_end(raw.substr(first+1,second-first-1));
    const auto beatport_link=strip_line_end(raw.substr(second+1,third-second-1));
    constexpr std::string_view page_header="@url=";
    constexpr std::string_view beatport_header="@beatport-url=";
    constexpr std::string_view crates_prefix="https://crates.co/track/";
    constexpr std::string_view crates_www_prefix="https://www.crates.co/track/";
    if(!page.starts_with(page_header) ||
       !beatport_link.starts_with(beatport_header))
        throw std::invalid_argument("Crates source URL headers are missing");
    const auto page_url=page.substr(page_header.size());
    const auto beatport_url=beatport_link.substr(beatport_header.size());
    const auto rest=page_url.starts_with(crates_prefix)
        ? page_url.substr(crates_prefix.size())
        : page_url.starts_with(crates_www_prefix)
            ? page_url.substr(crates_www_prefix.size())
            : std::string_view{};
    const auto slash=rest.find('/');
    if(slash==std::string_view::npos || slash==0 || slash>125 ||
       slash+1>=rest.size())
        throw std::invalid_argument("Crates page is not an exact public track");
    const auto slug=rest.substr(0,slash);
    for(char ch:slug)
        if(!((ch>='a'&&ch<='z') || (ch>='0'&&ch<='9') || ch=='-'))
            throw std::invalid_argument("Noncanonical Crates slug");
    const auto digits=rest.substr(slash+1);
    const auto track_id=beatport_fact_integer(
        digits,beatport::maximum_catalog_id);
    const auto beatport_target=qualify_beatport_public_track_url(beatport_url);
    if(track_id!=beatport_target.id ||
       beatport_target.canonical_url.substr(
           std::string_view("https://www.beatport.com/track/").size())
           !=rest)
        throw std::invalid_argument("Crates/Beatport public track identities conflict");
    const auto facts=parse_manual_candidate(raw.substr(third+1));
    if(facts.provider!="crates" ||
       facts.source_id!="track:"+std::to_string(track_id) ||
       facts.scope!=EvidenceScope::Recording || facts.cue_track_ordinal)
        throw std::invalid_argument("Crates source identity is inconsistent");
    beatport::TrackEvidence track;
    track.id=track_id;
    bool saw_title=false,saw_duration=false,saw_version=false;
    const auto singleton=[](const FieldEvidence& field)->std::string {
        if(field.values.size()!=1)
            throw std::invalid_argument("Crates field requires one visible value");
        return field.values.front();
    };
    for(const auto& field:facts.fields) {
        if(field.date_meaning!=DateMeaning::NotDate)
            throw std::invalid_argument("Crates visible date meaning unqualified");
        const auto key=ascii_upper_field(field.field);
        if(key=="TITLE") {
            track.title=singleton(field);
            saw_title=true;
        } else if(key=="ARTIST") track.artists=field.values;
        else if(key=="REMIXER") track.remixers=field.values;
        else if(key=="VERSION") {
            track.mix_name=singleton(field);
            saw_version=true;
        } else if(key=="DURATION") {
            track.duration_ms=beatport_visible_duration_ms(singleton(field));
            saw_duration=true;
        } else if(key=="BPM") {
            track.bpm=static_cast<int>(beatport_fact_integer(
                singleton(field),300));
            if(track.bpm<30)
                throw std::invalid_argument("Crates BPM outside DJ range");
        } else if(key=="KEY") track.musical_key=singleton(field);
        else if(key=="GENRE") track.genre=singleton(field);
        else if(key=="LABEL") track.label_name=singleton(field);
        else if(key=="ALBUM") track.release_title=singleton(field);
        else
            throw std::invalid_argument("Unqualified Crates field");
    }
    if(!saw_title || !saw_duration ||
       !beatport::text_ok(track.title,2048) ||
       !beatport::text_ok(track.mix_name,512) ||
       track.artists.empty() || track.artists.size()>32 ||
       track.remixers.size()>32)
        throw std::invalid_argument("Crates track lacks qualified visible facts");
    for(const auto& value:track.artists)
        if(!beatport::text_ok(value))
            throw std::invalid_argument("Invalid Crates artist credit");
    for(const auto& value:track.remixers)
        if(!beatport::text_ok(value))
            throw std::invalid_argument("Invalid Crates remixer credit");
    // If an explicit mix is present it must agree with the complete title.
    // The user-selected row is never allowed to invent a different version.
    if(saw_version) {
        const std::string title=track.title;
        const std::string version=track.mix_name;
        bool matched=false;
        for(const std::string pattern:{
                " ("+version+")", " ["+version+"]", " - "+version,
                " \xE2\x80\x93 "+version, " \xE2\x80\x94 "+version}) {
            if(title.size()>pattern.size() && title.ends_with(pattern))
                matched=true;
        }
        if(!matched)
            throw std::invalid_argument("Crates title and version disagree");
    }
    return track;
}

// The existing physical recording matcher checks artist, full title, version
// and duration. All Crates fields remain read-only, distinct from Beatport.
// An unqualified recording identity blocks every proposed field.
inline BeatportUserReview review_crates_public_track(
    const MetadataDocument& originals,std::int64_t duration_ms,
    const beatport::TrackEvidence& track) {
    auto result=review_beatport_user_track(originals,duration_ms,track);
    for(auto& row:result.fields) {
        if(row.candidate.provider=="beatport") {
            row.candidate.provider="crates";
            row.candidate.source_id="track:"+std::to_string(track.id);
            row.reason="crates_public_visible_"+row.reason;
        }
    }
    return result;
}

} // namespace djmeta::online
