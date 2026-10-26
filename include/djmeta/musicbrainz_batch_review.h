#pragma once
// Read-only whole-selection physical album-to-exact-edition audit.
// No SDK handle, write adapter, implicit tag approval or remote query.
#include "djmeta/musicbrainz_physical_review.h"
#include <set>

namespace djmeta::online {
struct PhysicalBatchTrack {
    std::size_t source_index=0; // original captured foobar batch identity
    MetadataDocument originals;
    std::int64_t duration_ms=-1;
    bool physical_qualified=false; // host-supplied, never inferred from tags
};
struct PhysicalBatchEditionReview {
    std::vector<FieldReviewRow> rows;
    std::size_t matched=0;
    bool complete=false; // only an exact entire edition, NOT write approval
};

inline PhysicalBatchEditionReview review_musicbrainz_physical_batch(
    const std::vector<PhysicalBatchTrack>& sources,
    const ReleaseEdition& edition) {
    if(sources.size()<2 || sources.size()>128 ||
       edition.provider!="musicbrainz" ||
       !musicbrainz::valid_mbid(edition.edition_id) ||
       edition.tracks.empty() || edition.tracks.size()>256 ||
       edition.fields.size()>256)
        throw std::invalid_argument("MusicBrainz physical batch shape invalid");
    for(const auto& f:edition.fields)
        if(f.provider!="musicbrainz" ||
           f.scope!=EvidenceScope::Edition ||
           f.source_id!=edition.edition_id)
            throw std::invalid_argument("MusicBrainz batch edition provenance invalid");
    std::set<std::size_t> ids;
    std::vector<LocalReleaseTrack> local;
    local.reserve(sources.size());
    bool qualified=true;
    std::optional<std::string> common_album,common_album_artist;
    for(const auto& source:sources) {
        if(!ids.insert(source.source_index).second ||
           source.originals.fields.size()>10000)
            throw std::invalid_argument("MusicBrainz duplicate/oversize batch source");
        const auto album=unique_discogs_physical_tag(source.originals,"ALBUM");
        const auto artist=unique_discogs_physical_tag(source.originals,"ALBUM ARTIST");
        if(!album || album->empty() ||
           (common_album && *album!=*common_album))
            qualified=false;
        if(!common_album)common_album=album;
        if(artist && !artist->empty()) {
            if(common_album_artist && *artist!=*common_album_artist)
                qualified=false;
            if(!common_album_artist)common_album_artist=artist;
        }
        if(!source.physical_qualified ||
           source.duration_ms<=0 || source.duration_ms>36000000 ||
           musicbrainz_physical_edition_identifier_conflict(
               source.originals,edition))
            qualified=false;
        auto identity=discogs_physical_identity(
            source.originals,source.duration_ms);
        const auto isrc=unique_discogs_physical_tag(source.originals,"ISRC");
        const bool has_isrc=std::any_of(
            source.originals.fields.begin(),source.originals.fields.end(),
            [](const auto& f){return ascii_upper_field(f.name)=="ISRC";});
        if(has_isrc && (!isrc || normalized_isrc(*isrc).empty()))
            qualified=false;
        if(!identity)qualified=false;
        if(identity) {
            identity->source_index=source.source_index;
            if(isrc)identity->recording.isrc=*isrc;
            local.push_back(std::move(*identity));
        }
    }
    ReleaseAlignment alignment;
    if(qualified && local.size()==sources.size())
        alignment=align_release(local,edition);
    std::map<std::size_t,std::size_t> verified;
    if(alignment.tracks.size()==sources.size())
        for(const auto& match:alignment.tracks)
            if(match.decision==MatchDecision::Suggested &&
               match.conflicts.empty() &&
               match.edition_track_index<edition.tracks.size() &&
               musicbrainz_physical_track_provenance_trusted(
                   edition,match.edition_track_index))
                verified.emplace(match.source_index,match.edition_track_index);
    PhysicalBatchEditionReview out;
    out.matched=verified.size();
    out.complete=qualified && verified.size()==sources.size() &&
                 sources.size()==edition.tracks.size();
    FieldReviewRow summary;
    summary.field="Batch / MATCH";
    summary.candidate.field="MATCH_DIAGNOSTIC";
    summary.candidate.provider="musicbrainz";
    summary.candidate.source_id=edition.edition_id;
    summary.candidate.scope=EvidenceScope::Edition;
    summary.candidate.values={
        std::to_string(out.matched)+"/"+std::to_string(sources.size())+
        " unique physical tracks matched; edition has "+
        std::to_string(edition.tracks.size())+" tracks",
        out.complete ? "Complete exact-edition read-only comparison"
                     : "Incomplete, ambiguous or conflicting batch; no approval"};
    summary.state=out.complete?FieldReviewState::Unchanged:
                               FieldReviewState::Blocked;
    summary.reason=out.complete
        ? "musicbrainz_physical_batch_complete_readonly"
        : "musicbrainz_physical_batch_unverified_or_partial";
    out.rows.push_back(std::move(summary));
    // Album facts are visible even when unverified, but never stageable.
    auto album=review_online_fields(sources.front().originals,edition.fields);
    for(auto& row:album) {
        row.field="Batch / "+row.field;
        row.reason="musicbrainz_physical_batch_"+row.reason;
        if(!out.complete)row.state=FieldReviewState::Blocked;
        out.rows.push_back(std::move(row));
    }
    for(const auto& source:sources) {
        const auto prefix="Physical #"+std::to_string(source.source_index+1)+" / ";
        FieldReviewRow match;
        match.field=prefix+"MATCH";
        match.candidate.field="MATCH_DIAGNOSTIC";
        match.candidate.provider="musicbrainz";
        match.candidate.scope=EvidenceScope::Edition;
        const auto it=verified.find(source.source_index);
        const bool found=it!=verified.end();
        match.candidate.source_id=found
            ? edition.tracks[it->second].source_track_id:edition.edition_id;
        match.candidate.values={found
            ? "Unique edition-track occurrence matched (read-only)"
            : "No unique qualified edition-track occurrence"};
        match.state=found?FieldReviewState::Unchanged:
                          FieldReviewState::Blocked;
        match.reason=found
            ? "musicbrainz_physical_batch_track_verified_readonly"
            : "musicbrainz_physical_batch_track_unverified";
        out.rows.push_back(std::move(match));
        // Complete untouched original field vectors, including custom tags,
        // multivalues and ReplayGain, remain visible for every source.
        for(const auto& original:source.originals.fields) {
            auto row=make_local_physical_original(
                original,"musicbrainz_batch_local_original_preserved");
            row.field=prefix+"Original "+original.name;
            out.rows.push_back(std::move(row));
        }
        if(found) {
            auto rows=review_online_fields(
                source.originals,edition.tracks[it->second].fields);
            for(auto& row:rows) {
                row.field=prefix+"Provider "+row.field;
                row.reason="musicbrainz_physical_batch_verified_"+row.reason;
                out.rows.push_back(std::move(row));
            }
        }
        if(out.rows.size()>20000)
            throw std::invalid_argument("MusicBrainz physical batch detail limit");
    }
    return out;
}
} // namespace djmeta::online
