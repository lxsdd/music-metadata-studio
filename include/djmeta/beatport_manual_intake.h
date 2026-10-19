#pragma once

// User-initiated Beatport track JSON import. OFFLINE and read-only.
// Source text must have been obtained lawfully by the user independently;
// this adapter grants no Beatport API access, authorization or usage rights.
#include "djmeta/beatport_provider.h"
#include "djmeta/online_fields.h"
#include "djmeta/online_intake.h"

#include <algorithm>
#include <cstdint>
#include <charconv>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta::online {

inline constexpr std::string_view beatport_user_json_header="@beatport-track-json";

inline bool is_beatport_user_json_clipboard(std::string_view raw) {
    return raw.starts_with("@beatport-track-json\n") ||
           raw.starts_with("@beatport-track-json\r\n");
}

inline beatport::TrackEvidence parse_beatport_user_json_clipboard(
    std::string_view raw) {
    if(!is_beatport_user_json_clipboard(raw))
        throw std::invalid_argument("Expected explicit @beatport-track-json header");
    if(raw.size()>65536 || raw.find('\0')!=std::string_view::npos)
        throw std::invalid_argument("Beatport user JSON exceeds safe text limit");
    const auto separator=raw.find('\n');
    const auto body=raw.substr(separator+1);
    if(body.empty())
        throw std::invalid_argument("Empty Beatport user JSON");
    const auto root=musicbrainz::JsonParser(body).parse();
    const auto track=beatport::parse_track_object(root);
    // A search teaser without actual artist or timing identity is not a
    // qualified track. Never invent mix labels or fields absent from source.
    if(track.artists.empty() || track.duration_ms<=0)
        throw std::invalid_argument(
            "Beatport user JSON lacks a qualified track artist/duration");
    return track;
}

// Optional lawful fallback when the user has only visible *factual* track
// metadata, not a licensed Beatport API response. This is explicitly typed
// input, NEVER scraped page HTML or an assertion of Beatport authorization.
// Format (example):
// @beatport-track-facts
// @provider=beatport
// @id=track:321
// TITLE=Home
// ARTIST=DJ Example
// VERSION=Extended Mix
// DURATION=6:42
// GENRE=Progressive House
// DATE@edition=2026-10-16
// The standard strict manual parser handles UTF-8, duplicate fields, limits
// and provenance; DURATION is identification evidence, not a physical tag.
inline constexpr std::string_view beatport_user_facts_header=
    "@beatport-track-facts";

inline bool is_beatport_user_facts_clipboard(std::string_view raw) {
    return raw.starts_with("@beatport-track-facts\n") ||
           raw.starts_with("@beatport-track-facts\r\n");
}

inline std::uint64_t beatport_fact_integer(
    std::string_view value, std::uint64_t upper) {
    if(value.empty() || value.front()=='0' || value.size()>15)
        throw std::invalid_argument("Noncanonical Beatport numeric identity");
    std::uint64_t result=0;
    const auto end=std::from_chars(value.data(),value.data()+value.size(),result);
    if(end.ec!=std::errc{} || end.ptr!=value.data()+value.size() ||
       result==0 || result>upper)
        throw std::invalid_argument("Invalid Beatport numeric identity");
    return result;
}

inline std::int64_t beatport_visible_duration_ms(std::string_view text) {
    // Visible time is second-precision, not sample-accurate; the existing
    // recording matcher applies its conservative 3-second tolerance.
    // MM:SS (or H:MM:SS), never an inferred duration from metadata.
    const auto colon=text.find(':');
    if(colon==std::string_view::npos || colon==0) 
        throw std::invalid_argument("Beatport duration must be MM:SS");
    const auto second=text.find(':',colon+1);
    if(second!=std::string_view::npos &&
       text.find(':',second+1)!=std::string_view::npos)
        throw std::invalid_argument("Invalid Beatport visible duration");
    auto number=[](std::string_view part)->std::uint64_t {
        if(part.empty() || part.size()>3)
            throw std::invalid_argument("Invalid Beatport duration segment");
        std::uint64_t out=0;
        const auto parsed=std::from_chars(
            part.data(),part.data()+part.size(),out);
        if(parsed.ec!=std::errc{} ||
           parsed.ptr!=part.data()+part.size())
            throw std::invalid_argument("Nondecimal Beatport duration");
        return out;
    };
    const auto first=number(text.substr(0,colon));
    const auto middle=number(text.substr(colon+1,
        second==std::string_view::npos
            ? std::string_view::npos:second-colon-1));
    if(second==std::string_view::npos) {
        if(text.size()-colon-1!=2 || first>359 || middle>59)
            throw std::invalid_argument("Invalid Beatport MM:SS duration");
        const auto seconds=first*60+middle;
        if(seconds<1 || seconds>6*60*60)
            throw std::invalid_argument("Beatport duration out of range");
        return static_cast<std::int64_t>(seconds)*1000;
    }
    const auto last=number(text.substr(second+1));
    if(text.size()-colon-1<5 || 
       second-colon-1!=2 || text.size()-second-1!=2 ||
       first>5 || middle>59 || last>59)
        throw std::invalid_argument("Invalid Beatport H:MM:SS duration");
    const auto seconds=first*3600+middle*60+last;
    if(seconds<1 || seconds>6*60*60)
        throw std::invalid_argument("Beatport duration out of range");
    return static_cast<std::int64_t>(seconds)*1000;
}

inline beatport::TrackEvidence parse_beatport_user_facts_clipboard(
    std::string_view raw) {
    if(!is_beatport_user_facts_clipboard(raw))
        throw std::invalid_argument("Expected @beatport-track-facts marker");
    if(raw.size()>65536)
        throw std::invalid_argument("Beatport fact text exceeds 64 KiB");
    const auto after_header=raw.find('\n');
    const auto facts=parse_manual_candidate(raw.substr(after_header+1));
    if(facts.provider!="beatport" ||
       facts.scope!=EvidenceScope::Recording ||
       facts.cue_track_ordinal || !facts.source_id.starts_with("track:"))
        throw std::invalid_argument("Beatport track facts require exact recording identity");
    beatport::TrackEvidence track;
    track.id=beatport_fact_integer(
        std::string_view(facts.source_id).substr(6),
        beatport::maximum_catalog_id);
    bool saw_duration=false;
    bool saw_version=false;
    const auto singleton=[](const FieldEvidence& field)->std::string {
        if(field.values.size()!=1)
            throw std::invalid_argument(
                "Beatport fact must be a single explicit value");
        return field.values.front();
    };
    for(const auto& field:facts.fields) {
        const auto key=ascii_upper_field(field.field);
        if(key=="TITLE") track.title=singleton(field);
        else if(key=="ARTIST") track.artists=field.values;
        else if(key=="VERSION") {
            track.mix_name=singleton(field);
            saw_version=true;
        } else if(key=="DURATION") {
            track.duration_ms=beatport_visible_duration_ms(singleton(field));
            saw_duration=true;
        } else if(key=="REMIXER") track.remixers=field.values;
        else if(key=="ISRC") track.isrc=singleton(field);
        else if(key=="GENRE") track.genre=singleton(field);
        else if(key=="STYLE") track.subgenre=singleton(field);
        else if(key=="KEY") track.musical_key=singleton(field);
        else if(key=="BPM") {
            track.bpm=static_cast<int>(beatport_fact_integer(
                singleton(field),300));
            if(track.bpm<30)
                throw std::invalid_argument("Beatport BPM outside DJ range");
        } else if(key=="ALBUM") track.release_title=singleton(field);
        else if(key=="LABEL") track.label_name=singleton(field);
        else if(key=="CATALOGNUMBER") track.catalog_number=singleton(field);
        else if(key=="BARCODE") track.upc=singleton(field);
        else if(key=="TRACKNUMBER") track.track_number=static_cast<int>(
            beatport_fact_integer(singleton(field),256));
        else if(key=="RELEASE_ID") track.release_id=beatport_fact_integer(
            singleton(field),beatport::maximum_catalog_id);
        else if(key=="DATE") {
            const auto date=singleton(field);
            if(!beatport::calendar_date(date))
                throw std::invalid_argument("Unqualified Beatport fact date");
            if(field.date_meaning==DateMeaning::EditionRelease)
                track.release_date=date;
            else if(field.date_meaning==DateMeaning::DigitalPublication)
                track.publish_date=date;
            else
                throw std::invalid_argument("Beatport date meaning unverified");
        } else {
            throw std::invalid_argument(
                "Unknown Beatport fact: use generic manual evidence for custom fields");
        }
        if(key!="DATE" && field.date_meaning!=DateMeaning::NotDate)
            throw std::invalid_argument("Unexpected Beatport date role");
    }
    if(!beatport::text_ok(track.title,2048) ||
       !beatport::text_ok(track.mix_name,512) ||
       track.artists.empty() || !saw_version || !saw_duration ||
       track.duration_ms<=0)
        throw std::invalid_argument(
            "Beatport facts need base TITLE, ARTIST, VERSION and exact visible DURATION");
    for(const auto& artist:track.artists)
        if(!beatport::text_ok(artist))
            throw std::invalid_argument("Invalid Beatport artist credit");
    return track;
}

inline std::optional<std::string> unique_beatport_local_value(
    const MetadataDocument& local, std::string_view key) {
    std::optional<std::string> value;
    for(const auto& field:local.fields) {
        if(ascii_upper_field(field.name)!=key) continue;
        if(value || field.values.size()!=1 || field.values.front().empty())
            return std::nullopt;
        value=field.values.front();
    }
    return value;
}

struct BeatportUserReview {
    MatchDecision identity = MatchDecision::Rejected;
    std::vector<std::string> conflicts;
    std::vector<FieldReviewRow> fields;
};

inline BeatportUserReview review_beatport_user_track(
    const MetadataDocument& originals,
    std::int64_t duration_ms,
    const beatport::TrackEvidence& track) {
    const auto title=unique_beatport_local_value(originals,"TITLE");
    const auto artist=unique_beatport_local_value(originals,"ARTIST");
    BeatportUserReview output;
    auto proposed=beatport::fields(track);
    output.fields=review_online_fields(originals,proposed);
    const auto has_field=[&](std::string_view name) {
        return std::any_of(originals.fields.begin(),originals.fields.end(),
            [&](const auto& f){return ascii_upper_field(f.name)==name;});
    };
    const auto version=unique_beatport_local_value(originals,"VERSION");
    const auto isrc=unique_beatport_local_value(originals,"ISRC");
    if(!title || !artist) {
        output.conflicts={"local_recording_identity_unqualified"};
    } else if(duration_ms<=0 || duration_ms>36000000) {
        output.conflicts={"physical_audio_duration_unavailable"};
    } else if(has_field("VERSION") && (!version ||
              !valid_evidence_origin(*version,512))) {
        output.conflicts={"local_version_ambiguous"};
    } else if(has_field("ISRC") && (!isrc ||
              !valid_evidence_origin(*isrc,128))) {
        output.conflicts={"local_isrc_ambiguous"};
    } else {
        RecordingIdentity local;
        local.title=*title;
        local.primary_artist=*artist;
        if(version) {
            local.mix_name=*version;
            local.mix=classify_mix_annotation(*version);
        }
        if(isrc) local.isrc=*isrc;
        local.duration_ms=duration_ms;
        const auto match=compare(local,beatport::to_candidate(track));
        output.identity=match.decision;
        output.conflicts=match.conflicts;
    }
    // Even strong recording evidence is still USER-SUPPLIED, never official
    // HTTP proof or permission to apply. Weak or conflicting identity blocks
    // every proposal; the user can still inspect the evidence and reasons.
    for(auto& row:output.fields) {
        if(output.identity!=MatchDecision::Suggested) {
            row.state=FieldReviewState::Blocked;
            row.reason="manual_beatport_identity_unverified";
            if(!output.conflicts.empty())
                row.reason+="_"+output.conflicts.front();
        } else row.reason="manual_beatport_readonly_"+row.reason;
    }
    // Preserve every physical original field without a provider counterpart,
    // including ReplayGain, artwork and arbitrary custom/multivalue tags.
    for(const auto& field:originals.fields) {
        if(std::any_of(proposed.begin(),proposed.end(),[&](const auto& item) {
            return ascii_upper_field(item.field)==ascii_upper_field(field.name);
        })) continue;
        output.fields.push_back(make_local_physical_original(
            field,"manual_beatport_local_only_no_provider_value"));
    }
    return output;
}

} // namespace djmeta::online
