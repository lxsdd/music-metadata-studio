#pragma once

// User-initiated Beatport track JSON import. OFFLINE and read-only.
// Source text must have been obtained lawfully by the user independently;
// this adapter grants no Beatport API access, authorization or usage rights.
#include "djmeta/beatport_provider.h"
#include "djmeta/online_fields.h"

#include <algorithm>
#include <cstdint>
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
    if(!title || !artist) {
        output.conflicts={"local_recording_identity_unqualified"};
    } else {
        RecordingIdentity local;
        local.title=*title;
        local.primary_artist=*artist;
        if(const auto version=unique_beatport_local_value(originals,"VERSION"))
            local.mix_name=*version;
        if(const auto isrc=unique_beatport_local_value(originals,"ISRC"))
            local.isrc=*isrc;
        if(duration_ms>0 && duration_ms<=36000000)
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
        FieldReviewRow row;
        row.field=field.name;
        row.original_values=field.values;
        row.candidate.field=field.name;
        row.candidate.provider="beatport";
        row.candidate.source_id=std::to_string(track.id);
        row.state=FieldReviewState::Unchanged;
        row.reason="manual_beatport_local_only_no_provider_value";
        output.fields.push_back(std::move(row));
    }
    return output;
}

} // namespace djmeta::online
