#pragma once
// Read-only Discogs -> physical metadata edition preview.
// Never maps individual edition tracks to a physical source implicitly.
#include "djmeta/discogs_provider.h"
#include "djmeta/online_fields.h"

#include <algorithm>
#include <cstddef>
#include <charconv>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace djmeta::online {


/* A qualified physical file is one local recording, never a virtual CUE
   subsong. Duplicate identity tags are not arbitrarily flattened. */
inline std::optional<std::string> unique_discogs_physical_tag(
    const MetadataDocument& original, std::string_view wanted) {
    std::optional<std::string> result;
    for(const auto& tag:original.fields) {
        if(ascii_upper_field(tag.name)!=wanted) continue;
        if(result || tag.values.size()!=1) return std::nullopt;
        result=tag.values.front();
    }
    return result;
}
inline int discogs_physical_position(const MetadataDocument& original,
                                     std::string_view wanted) {
    const auto value=unique_discogs_physical_tag(original,wanted);
    if(!value || value->empty() || value->size()>3) return -1;
    int number=0;
    const auto [end,error]=std::from_chars(
        value->data(),value->data()+value->size(),number);
    return error==std::errc{} && end==value->data()+value->size() &&
           number>0 && number<=256 ? number : -1;
}
inline std::optional<LocalReleaseTrack> discogs_physical_identity(
    const MetadataDocument& original, std::int64_t duration_ms) {
    const auto title=unique_discogs_physical_tag(original,"TITLE");
    const auto artist=unique_discogs_physical_tag(original,"ARTIST");
    if(!title || title->empty() || !artist || artist->empty() ||
       !valid_evidence_origin(*title,2048) ||
       !valid_evidence_origin(*artist,2048)) return std::nullopt;
    // A separate VERSION is recording identity. Conflicting/multivalue
    // versions cannot be ignored in favor of an apparently matching TITLE.
    const auto version=unique_discogs_physical_tag(original,"VERSION");
    const bool has_version=std::any_of(original.fields.begin(),original.fields.end(),
        [](const auto& tag){return ascii_upper_field(tag.name)=="VERSION";});
    if(has_version && (!version || version->empty() ||
                       !valid_evidence_origin(*version,512)))
        return std::nullopt;
    LocalReleaseTrack local;
    local.source_index=0;
    local.recording.title=*title;
    local.recording.primary_artist=*artist;
    if(version) {
        local.recording.mix_name=*version;
        local.recording.mix=classify_mix_annotation(*version);
    }
    // Discogs release tracklists do not expose independently verified ISRC
    // in this adapter. A one-sided local ISRC must not falsely veto a
    // duration-and-mix-qualified association; it stays in original tags.
    if(duration_ms>0 && duration_ms<=36000000)
        local.recording.duration_ms=duration_ms;
    local.track_number=discogs_physical_position(original,"TRACKNUMBER");
    local.disc_number=discogs_physical_position(original,"DISCNUMBER");
    return local;
}

// Track-field provenance is part of release identity, not merely a detail
// rendering condition. An otherwise matching physical recording cannot
// promote edition-wide album/date/genre proposals through a tainted track.
inline bool discogs_edition_track_provenance_trusted(
    const ReleaseEdition& edition, std::size_t index) {
    if (index >= edition.tracks.size()) return false;
    const auto& track = edition.tracks[index];
    const auto expected = edition.edition_id + "/track/" +
        std::to_string(index + 1);
    return !track.fields.empty() && std::all_of(
        track.fields.begin(), track.fields.end(), [&](const auto& field) {
            return field.provider == "discogs" &&
                   field.scope == EvidenceScope::Edition &&
                   field.source_id == expected;
        });
}

inline std::vector<FieldReviewRow> review_discogs_physical_edition(
    const MetadataDocument& originals, const ReleaseEdition& edition,
    std::int64_t physical_duration_ms=-1) {
    if(edition.provider!="discogs" ||
       !valid_evidence_origin(edition.edition_id,512))
        throw std::invalid_argument("Not a qualified Discogs release edition");
    if(edition.fields.size()>256 || edition.tracks.size()>256 ||
       originals.fields.size()>10000)
        throw std::invalid_argument("Unbounded physical edition review");
    for(const auto& field:edition.fields)
        if(field.provider!="discogs" ||
           field.source_id!=edition.edition_id ||
           field.scope!=EvidenceScope::Edition)
            throw std::invalid_argument("Mixed-provider Discogs edition evidence");

    std::vector<FieldReviewRow> rows=review_online_fields(originals,edition.fields);
    for(auto& row:rows) {
        row.field="Album / "+row.field;
        row.reason="discogs_live_release_edition_"+row.reason;
    }
    // An absent provider field is never a tag deletion proposal. Include
    // original-only custom, ReplayGain, picture, unknown and multivalue tags.
    for(const auto& original:originals.fields) {
        const auto name=ascii_upper_field(original.name);
        const bool supplied=std::any_of(edition.fields.begin(),edition.fields.end(),
            [&](const auto& evidence) {
                return ascii_upper_field(evidence.field)==name;
            });
        if(supplied) continue;
        auto retained=make_local_physical_original(
            original,"discogs_local_only_no_provider_value");
        retained.field="Physical / "+original.name;
        rows.push_back(std::move(retained));
    }
    // Only a unique Suggested association with no conflicts may expose
    // track-specific fields for comparison. This is NEVER write approval.
    std::size_t verified_track=no_release_track;
    const auto local=discogs_physical_identity(
        originals,physical_duration_ms);
    std::optional<TrackAssociation> chosen_association;
    if(local) {
        const auto alignment=align_release({*local},edition);
        if(alignment.tracks.size()==1) {
            const auto& link=alignment.tracks.front();
            chosen_association=link;
            if(link.decision==MatchDecision::Suggested &&
               link.conflicts.empty() &&
               discogs_edition_track_provenance_trusted(
                   edition, link.edition_track_index))
                verified_track=link.edition_track_index;
        }
    }
    // An exact catalogue edition is only provider evidence until a unique
    // physical recording matches one specific edition track. Do not suggest
    // ALBUM/DATE/GENRE changes for unrelated files merely because an edition
    // was selected by search or pasted URL. Preserve the evidence and all
    // originals visibly, but block reviewable album proposals.
    if(verified_track==no_release_track) {
        for(auto& row:rows) {
            if(!row.field.starts_with("Album / ")) continue;
            row.state=FieldReviewState::Blocked;
            row.reason="discogs_live_release_edition_source_unverified_"+row.reason;
        }
    }
    for(std::size_t i=0;i<edition.tracks.size();++i) {
        const auto& track=edition.tracks[i];
        const auto expected=edition.edition_id+"/track/"+std::to_string(i+1);
        const bool trusted=discogs_edition_track_provenance_trusted(edition,i);
        // Display-only proof/uncertainty for each edition track. This row
        // is not a candidate metadata tag and cannot approve a write.
        FieldReviewRow diagnosis;
        diagnosis.field="Release Track "+std::to_string(i+1)+" / MATCH";
        diagnosis.candidate.field="MATCH_DIAGNOSTIC";
        diagnosis.candidate.provider="discogs";
        diagnosis.candidate.source_id=expected;
        diagnosis.candidate.scope=EvidenceScope::Edition;
        diagnosis.state=FieldReviewState::Blocked;
        if(!trusted) {
            diagnosis.reason="discogs_live_match_track_provenance_unqualified";
            diagnosis.candidate.values={"Incomplete or inconsistent track provenance"};
        } else if(!local) {
            diagnosis.reason="discogs_live_match_local_identity_ambiguous";
            diagnosis.candidate.values={"Unique physical TITLE and ARTIST are required"};
        } else if(physical_duration_ms<=0 || physical_duration_ms>36000000) {
            diagnosis.reason="discogs_live_match_duration_unavailable";
            diagnosis.candidate.values={"Physical audio duration unavailable"};
        } else if(i==verified_track) {
            diagnosis.state=FieldReviewState::Unchanged;
            diagnosis.reason="discogs_live_match_suggested_readonly";
            diagnosis.candidate.values=chosen_association
                ? chosen_association->evidence : std::vector<std::string>{};
            diagnosis.candidate.values.push_back("Suggested only; no write approval");
        } else if(chosen_association &&
                  chosen_association->edition_track_index==i) {
            diagnosis.reason="discogs_live_match_requires_review";
            diagnosis.candidate.values=chosen_association->conflicts;
            if(diagnosis.candidate.values.empty())
                diagnosis.candidate.values={"Insufficient independent evidence"};
        } else {
            const auto compared=compare(local->recording,
                Candidate{"discogs",track.source_track_id,track.recording});
            diagnosis.reason="discogs_live_match_other_or_conflicting_track";
            diagnosis.candidate.values=compared.conflicts;
            if(diagnosis.candidate.values.empty())
                diagnosis.candidate.values={"Not the uniquely qualified edition track"};
        }
        rows.push_back(std::move(diagnosis));
        if(i==verified_track && trusted) {
            auto compared=review_online_fields(originals,track.fields);
            for(auto& row:compared) {
                row.field="Release Track "+std::to_string(i+1)+" / "+row.field;
                row.reason="discogs_live_release_verified_track_"+row.reason;
                rows.push_back(std::move(row));
            }
            continue;
        }
        for(const auto& field:track.fields) {
            if(rows.size()>=12000)
                throw std::invalid_argument("Discogs detail display row limit");
            FieldReviewRow blocked;
            blocked.field="Release Track "+std::to_string(i+1)+" / "+field.field;
            blocked.state=FieldReviewState::Blocked;
            blocked.reason="discogs_live_release_track_unassigned";
            if(field.provider=="discogs" &&
               field.scope==EvidenceScope::Edition &&
               field.source_id==expected) {
                blocked.candidate=field; // evidence only, never approval
            } else {
                blocked.candidate.field=field.field;
                blocked.candidate.provider="discogs";
                blocked.candidate.source_id=edition.edition_id;
                blocked.reason="discogs_live_release_unverified_track_provenance";
            }
            rows.push_back(std::move(blocked));
        }
    }
    return rows;
}

} // namespace djmeta::online
