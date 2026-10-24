#pragma once
// One deliberately copied, browser-visible Crates release and its complete
// track table. Pure offline parser: no HTTP, authentication or mutations.
// A release table's "Date" is unqualified and must never become DATE.
#include "djmeta/crates_visible_intake.h"
#include "djmeta/beatport_release_review.h"

#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta::online {

inline bool is_crates_public_release_clipboard(std::string_view raw) {
    return raw.starts_with("@crates-public-release\n") ||
           raw.starts_with("@crates-public-release\r\n");
}

inline ReleaseEdition parse_crates_public_release_clipboard(
    std::string_view raw) {
    if(!is_crates_public_release_clipboard(raw) || raw.size()>65536 ||
       raw.find('\0')!=std::string_view::npos ||
       !valid_utf8_metadata_text(raw))
        throw std::invalid_argument("Unqualified Crates release clipboard");
    std::string normalized;
    normalized.reserve(raw.size());
    for(std::size_t i=0;i<raw.size();++i) {
        if(raw[i]=='\r') {
            if(i+1==raw.size() || raw[++i]!='\n')
                throw std::invalid_argument("Malformed Crates line ending");
            normalized+='\n';
        } else normalized+=raw[i];
    }
    const std::string_view source=normalized;
    const auto first=source.find('\n');
    const auto second=source.find('\n',first+1);
    const auto third=source.find('\n',second+1);
    if(first==std::string_view::npos ||
       second==std::string_view::npos ||
       third==std::string_view::npos)
        throw std::invalid_argument("Crates release source URLs missing");
    constexpr std::string_view url_header="@url=";
    constexpr std::string_view beatport_header="@beatport-url=";
    const auto page_line=source.substr(first+1,second-first-1);
    const auto bp_line=source.substr(second+1,third-second-1);
    if(!page_line.starts_with(url_header) ||
       !bp_line.starts_with(beatport_header))
        throw std::invalid_argument("Crates release URL headers missing");
    const auto page_url=page_line.substr(url_header.size());
    const auto bp_url=bp_line.substr(beatport_header.size());
    constexpr std::string_view crates="https://crates.co/release/";
    constexpr std::string_view crates_www="https://www.crates.co/release/";
    constexpr std::string_view bp="https://www.beatport.com/release/";
    const auto rest=page_url.starts_with(crates)
        ? page_url.substr(crates.size())
        : page_url.starts_with(crates_www)
          ? page_url.substr(crates_www.size())
          : std::string_view{};
    if(rest.size()>150 || !bp_url.starts_with(bp) ||
       bp_url.substr(bp.size())!=rest)
        throw std::invalid_argument("Conflicting Crates/Beatport release URL");
    const auto slash=rest.find('/');
    if(slash==std::string_view::npos || slash==0 || slash>125 ||
       slash+1>=rest.size())
        throw std::invalid_argument("Crates release has no canonical edition");
    for(char ch:rest.substr(0,slash))
        if(!((ch>='a'&&ch<='z') || (ch>='0'&&ch<='9') || ch=='-'))
            throw std::invalid_argument("Unqualified Crates release slug");
    const auto id=beatport_fact_integer(
        rest.substr(slash+1),beatport::maximum_catalog_id);
    const auto separator=source.find("\n@track\n",third+1);
    if(separator==std::string_view::npos)
        throw std::invalid_argument("Crates release has no visible tracklist");
    const auto album=parse_manual_candidate(
        source.substr(third+1,separator-third-1));
    const auto edition_source="release:"+std::to_string(id);
    if(album.provider!="crates" || album.source_id!=edition_source ||
       album.scope!=EvidenceScope::Edition || album.cue_track_ordinal)
        throw std::invalid_argument("Crates release identity does not match URL");
    ReleaseEdition edition;
    edition.provider="crates";
    edition.edition_id=edition_source;
    bool album_seen=false,label_seen=false,catalog_seen=false;
    for(const auto& evidence:album.fields) {
        if(evidence.date_meaning!=DateMeaning::NotDate ||
           evidence.values.size()!=1)
            throw std::invalid_argument("Unqualified Crates edition field");
        const auto key=ascii_upper_field(evidence.field);
        if(key=="ALBUM") {
            if(album_seen) throw std::invalid_argument("Repeated Crates album");
            album_seen=true;
        } else if(key=="LABEL") {
            if(label_seen) throw std::invalid_argument("Repeated Crates label");
            label_seen=true;
        } else if(key=="CATALOGNUMBER") {
            if(catalog_seen) throw std::invalid_argument("Repeated Crates catalog");
            catalog_seen=true;
        } else throw std::invalid_argument("Unsupported Crates release field");
        if(!beatport::text_ok(evidence.values.front(),2048))
            throw std::invalid_argument("Malformed Crates edition field");
        edition.fields.push_back(evidence);
    }
    if(!album_seen || !label_seen)
        throw std::invalid_argument("Crates release album or label missing");
    std::set<std::string> track_ids;
    std::size_t cursor=separator+std::string_view("\n@track\n").size();
    while(cursor<source.size()) {
        const auto next=source.find("\n@track\n",cursor);
        const auto chunk=source.substr(cursor,next==std::string_view::npos
            ? std::string_view::npos:next-cursor);
        const auto facts=parse_manual_candidate(chunk);
        if(facts.provider!="crates" ||
           facts.scope!=EvidenceScope::Recording ||
           facts.cue_track_ordinal || !facts.source_id.starts_with("track:") ||
           !track_ids.insert(facts.source_id).second ||
           edition.tracks.size()>=128)
            throw std::invalid_argument("Crates release track is not unique and scoped");
        (void)beatport_fact_integer(
            std::string_view(facts.source_id).substr(6),
            beatport::maximum_catalog_id);
        EditionTrack track;
        track.source_track_id=facts.source_id;
        bool saw_title=false,saw_duration=false,saw_number=false,saw_release=false;
        bool saw_bpm=false,saw_key=false;
        std::vector<std::string> artists;
        for(const auto& field:facts.fields) {
            if(field.date_meaning!=DateMeaning::NotDate)
                throw std::invalid_argument("Crates release Date is unqualified");
            const auto key=ascii_upper_field(field.field);
            if(key=="ARTIST" || key=="REMIXER") {
                if(field.values.empty() || field.values.size()>32)
                    throw std::invalid_argument("Unbounded Crates release credits");
                for(const auto& value:field.values)
                    if(!beatport::text_ok(value))
                        throw std::invalid_argument("Invalid Crates release credits");
                if(key=="ARTIST") {
                    if(!artists.empty())
                        throw std::invalid_argument("Duplicate Crates artist block");
                    artists=field.values;
                }
                track.fields.push_back(field);
                continue;
            }
            if(field.values.size()!=1 ||
               !beatport::text_ok(field.values.front(),2048))
                throw std::invalid_argument("Malformed Crates track singleton");
            const auto& value=field.values.front();
            if(key=="RELEASE_ID") {
                if(saw_release || beatport_fact_integer(
                       value,beatport::maximum_catalog_id)!=id)
                    throw std::invalid_argument("Crates track has another release");
                saw_release=true;
            } else if(key=="TRACKNUMBER") {
                if(saw_number ||
                   beatport_fact_integer(value,128)!=edition.tracks.size()+1)
                    throw std::invalid_argument("Crates track sequence differs");
                saw_number=true;
                track.track_number=static_cast<int>(edition.tracks.size()+1);
                track.fields.push_back(field);
            } else if(key=="TITLE") {
                if(saw_title) throw std::invalid_argument("Repeated Crates title");
                saw_title=true;
                track.recording.title=value;
                track.fields.push_back(field);
            } else if(key=="DURATION") {
                if(saw_duration) throw std::invalid_argument("Repeated Crates duration");
                saw_duration=true;
                track.recording.duration_ms=beatport_visible_duration_ms(value);
            } else if(key=="BPM") {
                if(saw_bpm ||
                   beatport_fact_integer(value,300)<30)
                    throw std::invalid_argument("Invalid Crates release BPM");
                saw_bpm=true;
                track.fields.push_back(field);
            } else if(key=="KEY") {
                if(saw_key) throw std::invalid_argument("Repeated Crates key");
                saw_key=true;
                track.fields.push_back(field);
            } else
                throw std::invalid_argument("Unsupported Crates release track field");
        }
        if(!saw_release || !saw_number || !saw_title ||
           !saw_duration || artists.empty())
            throw std::invalid_argument("Incomplete Crates edition track");
        track.recording.primary_artist=artists.front();
        const auto annotation=split_title_mix(track.recording.title);
        track.recording.mix_name=std::string(annotation.version);
        track.recording.mix=annotation.kind;
        edition.tracks.push_back(std::move(track));
        if(next==std::string_view::npos) break;
        cursor=next+std::string_view("\n@track\n").size();
    }
    if(edition.tracks.empty())
        throw std::invalid_argument("Empty Crates edition tracklist");
    validate_manual_beatport_edition(edition,"crates");
    return edition;
}

// All candidates retain their own Crates source IDs and remain read-only.
// Album fields are blocked until one physical title/artist/mix/duration
// matches an exact edition track; no ambiguous CUE association is attempted.
inline std::vector<FieldReviewRow> review_crates_public_release_physical(
    const MetadataDocument& originals,std::int64_t duration_ms,
    const ReleaseEdition& edition) {
    auto rows=review_beatport_user_edition_physical(
        originals,duration_ms,edition,"crates");
    for(auto& row:rows) {
        if(row.candidate.provider=="crates" &&
           row.reason.starts_with("manual_beatport_"))
            row.reason="crates_public_release_"+
                row.reason.substr(std::string_view("manual_beatport_").size());
    }
    return rows;
}

} // namespace djmeta::online
