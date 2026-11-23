#pragma once
// Official MusicBrainz exact-release fields compared against ONE physical
// foobar audio file. Search/release hits do not authorize physical writes.
#include "djmeta/discogs_release_review.h"
#include "djmeta/musicbrainz_provider.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace djmeta::online {

// A release-track occurrence MBID, not a recording MBID, owns the
// edition-scoped track evidence. Matching titles alone never grant trust.
inline bool musicbrainz_physical_track_provenance_trusted(
    const ReleaseEdition& edition, std::size_t index) {
    if (edition.provider!="musicbrainz" || index>=edition.tracks.size())
        return false;
    const auto& track=edition.tracks[index];
    if (!musicbrainz::valid_mbid(track.source_track_id) ||
        track.fields.empty()) return false;
    bool has_direct_title=false;
    for (const auto& field:track.fields) {
        if (field.provider!="musicbrainz" ||
            field.scope!=EvidenceScope::Edition ||
            field.source_id!=track.source_track_id ||
            field.values.size()!=1) return false;
        if (field.field=="TITLE" && field.values.front()==track.recording.title)
            has_direct_title=true;
    }
    return has_direct_title;
}

// A local catalogue number or barcode that contradicts an official release
// is negative edition identity evidence, never a reason to rewrite a tag.
// If the remote release doesn't supply one, missing is UNKNOWN.
inline bool musicbrainz_physical_edition_identifier_conflict(
    const MetadataDocument& originals,const ReleaseEdition& edition) {
    for (const auto* name:{"BARCODE","CATALOGNUMBER"}) {
        std::vector<std::string> provided;
        for (const auto& field:edition.fields)
            if (ascii_upper_field(field.field)==name) {
                if (field.provider!="musicbrainz" ||
                    field.source_id!=edition.edition_id ||
                    field.scope!=EvidenceScope::Edition) return true;
                provided.insert(provided.end(),
                    field.values.begin(),field.values.end());
            }
        if (provided.empty()) continue;
        bool seen=false;
        for (const auto& field:originals.fields) {
            if (ascii_upper_field(field.name)!=name) continue;
            if (seen || field.values.empty()) return true;
            seen=true;
            for (const auto& value:field.values)
                if (value.empty() || std::find(
                        provided.begin(),provided.end(),value)==provided.end())
                    return true;
        }
    }
    return false;
}

// All original tag vectors remain visible and untouched. Only a unique,
// evidence-complete physical title/artist/version/duration/ISRC association
// to a DIRECTLY identified edition track can make album facts reviewable.
// This function exposes NO stageable list and NO approval or write surface.
inline std::vector<FieldReviewRow> review_musicbrainz_physical_edition(
    const MetadataDocument& originals,const ReleaseEdition& edition,
    std::int64_t duration_ms) {
    if (edition.provider!="musicbrainz" ||
        !musicbrainz::valid_mbid(edition.edition_id) ||
        originals.fields.size()>10000 || edition.fields.size()>256 ||
        edition.tracks.empty() || edition.tracks.size()>256)
        throw std::invalid_argument("MusicBrainz physical edition shape invalid");
    for (const auto& field:edition.fields)
        if (field.provider!="musicbrainz" ||
            field.scope!=EvidenceScope::Edition ||
            field.source_id!=edition.edition_id)
            throw std::invalid_argument("MusicBrainz mixed release provenance");

    auto rows=review_online_fields(originals,edition.fields);
    for (auto& row:rows) {
        row.field="Album / "+row.field;
        row.reason="musicbrainz_live_physical_edition_"+row.reason;
    }
    for (const auto& original:originals.fields) {
        const auto key=ascii_upper_field(original.name);
        const bool supplied=std::any_of(
            edition.fields.begin(),edition.fields.end(),[&](const auto& evidence) {
                return ascii_upper_field(evidence.field)==key;
            });
        if (!supplied) {
            auto original_row=make_local_physical_original(
                original,"musicbrainz_local_only_no_provider_value");
            original_row.field="Physical / "+original.name;
            rows.push_back(std::move(original_row));
        }
    }
    const bool identifier_conflict=
        musicbrainz_physical_edition_identifier_conflict(originals,edition);
    auto local=discogs_physical_identity(originals,duration_ms);
    // MusicBrainz explicitly supplies recording ISRCs for a release track;
    // preserve and independently compare a unique local ISRC if available.
    const auto isrc=unique_discogs_physical_tag(originals,"ISRC");
    const bool tagged_isrc=std::any_of(
        originals.fields.begin(),originals.fields.end(),[](const auto& f) {
            return ascii_upper_field(f.name)=="ISRC";
        });
    if (tagged_isrc && (!isrc || normalized_isrc(*isrc).empty()))
        local.reset();
    if (local && isrc) local->recording.isrc=*isrc;

    std::size_t verified=no_release_track;
    std::optional<TrackAssociation> association;
    if (local && duration_ms>0 && duration_ms<=36000000 &&
        !identifier_conflict) {
        const auto matched=align_release({*local},edition);
        if (matched.tracks.size()==1) {
            association=matched.tracks.front();
            if (association->decision==MatchDecision::Suggested &&
                association->conflicts.empty() &&
                musicbrainz_physical_track_provenance_trusted(
                    edition,association->edition_track_index))
                verified=association->edition_track_index;
        }
    }
    if (verified==no_release_track)
        for (auto& row:rows)
            if (row.field.starts_with("Album / ")) {
                row.state=FieldReviewState::Blocked;
                row.reason="musicbrainz_live_physical_edition_source_unverified_"+
                           row.reason;
            }
    for (std::size_t i=0;i<edition.tracks.size();++i) {
        const auto& track=edition.tracks[i];
        const auto prefix="Release Track "+std::to_string(i+1)+" / ";
        const bool trusted=musicbrainz_physical_track_provenance_trusted(edition,i);
        FieldReviewRow match;
        match.field=prefix+"MATCH";
        match.candidate.field="MATCH_DIAGNOSTIC";
        match.candidate.provider="musicbrainz";
        match.candidate.source_id=track.source_track_id.empty()
            ? edition.edition_id:track.source_track_id;
        match.candidate.scope=EvidenceScope::Edition;
        match.state=i==verified?FieldReviewState::Unchanged:
                                FieldReviewState::Blocked;
        match.reason=i==verified
            ? "musicbrainz_live_physical_track_verified_readonly"
            : !trusted
            ? "musicbrainz_live_physical_track_provenance_unverified"
            : identifier_conflict
            ? "musicbrainz_live_physical_edition_identifier_conflict"
            : "musicbrainz_live_physical_track_match_unverified";
        match.candidate.values=i==verified && association
            ? association->evidence : std::vector<std::string>{};
        if (match.candidate.values.empty())
            match.candidate.values={"No qualified physical release-track match"};
        if (i==verified)
            match.candidate.values.push_back("Read-only comparison; no tag approval");
        rows.push_back(std::move(match));
        if (i==verified && trusted) {
            auto comparisons=review_online_fields(originals,track.fields);
            for (auto& row:comparisons) {
                row.field=prefix+row.field;
                row.reason="musicbrainz_live_physical_verified_track_"+row.reason;
                rows.push_back(std::move(row));
            }
        } else {
            for (const auto& field:track.fields) {
                FieldReviewRow blocked;
                blocked.field=prefix+field.field;
                blocked.candidate=field;
                blocked.state=FieldReviewState::Blocked;
                blocked.reason="musicbrainz_live_physical_track_unassigned";
                rows.push_back(std::move(blocked));
            }
        }
        if (rows.size()>12000)
            throw std::invalid_argument("MusicBrainz physical detail bound exceeded");
    }
    return rows;
}

} // namespace djmeta::online
