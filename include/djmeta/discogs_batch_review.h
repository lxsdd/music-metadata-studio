#pragma once
// Exact Discogs edition against a user-selected 2-128 physical-file album.
// One shared one-to-one aligner; diagnostics never become bulk write consent.
#include "djmeta/musicbrainz_batch_review.h" // shared PhysicalBatchTrack model
#include <charconv>
#include <map>
#include <set>

namespace djmeta::online {
struct DiscogsPhysicalBatchReview {
    std::vector<FieldReviewRow> rows;
    std::size_t matched=0;
    bool complete=false;
};

inline DiscogsPhysicalBatchReview review_discogs_physical_batch(
    const std::vector<PhysicalBatchTrack>& sources,
    const ReleaseEdition& edition) {
    std::uint64_t id=0;
    const auto [end,ec]=std::from_chars(edition.edition_id.data(),
        edition.edition_id.data()+edition.edition_id.size(),id);
    if(sources.size()<2 || sources.size()>128 ||
       edition.provider!="discogs" || edition.edition_id.empty() ||
       edition.edition_id.front()=='0' || ec!=std::errc{} ||
       end!=edition.edition_id.data()+edition.edition_id.size() ||
       id==0 || id>999999999999ULL || edition.tracks.empty() ||
       edition.tracks.size()>256 || edition.fields.size()>256)
        throw std::invalid_argument("Discogs batch shape invalid");
    for(const auto& f:edition.fields)
        if(f.provider!="discogs" || f.scope!=EvidenceScope::Edition ||
           f.source_id!=edition.edition_id)
            throw std::invalid_argument("Discogs batch edition provenance invalid");

    std::set<std::size_t> ids;
    std::optional<std::string> common_album,common_artist;
    std::vector<LocalReleaseTrack> local;
    local.reserve(sources.size());
    bool qualified=true;
    // Report blockers against captured source IDs, not display row ordinals.
    // A source-specific reason is diagnostic evidence only, never approval.
    std::map<std::size_t,std::string> source_problems;
    for(const auto& source:sources) {
        if(!ids.insert(source.source_index).second ||
           source.originals.fields.size()>10000)
            throw std::invalid_argument("Duplicate/oversize Discogs batch source");
        const auto block=[&](std::string reason) {
            qualified=false;
            source_problems.try_emplace(source.source_index,std::move(reason));
        };
        for(const auto* key:{"ALBUM","ALBUM ARTIST"}) {
            const auto type=std::string_view(key)=="ALBUM"
                ? "ALBUM" : "ALBUM_ARTIST";
            const bool present=std::any_of(source.originals.fields.begin(),
                source.originals.fields.end(),[&](const auto& f) {
                    return ascii_upper_field(f.name)==key;
                });
            const auto value=unique_discogs_physical_tag(source.originals,key);
            if(present && (!value || value->empty()))
                block(std::string("AMBIGUOUS_")+type);
            if(value && !value->empty()) {
                auto& common=std::string_view(key)=="ALBUM"
                    ?common_album:common_artist;
                if(common && *common!=*value)
                    block(std::string("CONFLICTING_")+type);
                if(!common)common=*value;
            }
        }
        if(!source.physical_qualified)
            block("NOT_PHYSICAL_SOURCE");
        if(source.duration_ms<=0 || source.duration_ms>36000000)
            block("DURATION_UNQUALIFIED");
        if(discogs_edition_identifier_conflict(source.originals,edition))
            block("EDITION_IDENTIFIER_CONFLICT");
        auto identity=discogs_physical_identity(source.originals,source.duration_ms);
        if(!identity) block("TRACK_IDENTITY_UNQUALIFIED");
        else {
            identity->source_index=source.source_index;
            local.push_back(std::move(*identity));
        }
    }
    ReleaseAlignment aligned;
    if(qualified && local.size()==sources.size())
        aligned=align_release(local,edition);
    std::map<std::size_t,std::size_t> verified;
    std::set<std::size_t> occurrences;
    if(aligned.tracks.size()==sources.size())
        for(const auto& match:aligned.tracks)
            if(match.decision==MatchDecision::Suggested &&
               match.conflicts.empty() &&
               match.edition_track_index<edition.tracks.size() &&
               discogs_edition_track_provenance_trusted(
                   edition,match.edition_track_index) &&
               occurrences.insert(match.edition_track_index).second)
                verified.emplace(match.source_index,match.edition_track_index);
    DiscogsPhysicalBatchReview out;
    out.matched=verified.size();
    out.complete=qualified && out.matched==sources.size() &&
                 sources.size()==edition.tracks.size();
    FieldReviewRow summary;
    summary.field="Batch / MATCH";
    summary.candidate.field="MATCH_DIAGNOSTIC";
    summary.candidate.provider="discogs";
    summary.candidate.source_id=edition.edition_id;
    summary.candidate.scope=EvidenceScope::Edition;
    summary.candidate.values={
        std::to_string(out.matched)+"/"+std::to_string(sources.size())+
        " unique physical files; exact edition contains "+
        std::to_string(edition.tracks.size())+" tracks",
        out.complete ? "Complete read-only comparison; no bulk approval"
                     : "Incomplete/ambiguous selection; no approval"};
    summary.state=out.complete?FieldReviewState::Unchanged:FieldReviewState::Blocked;
    summary.reason=out.complete?"discogs_batch_complete_readonly":
        "discogs_batch_unverified_or_partial";
    out.rows.push_back(std::move(summary));
    auto album=review_online_fields(sources.front().originals,edition.fields);
    for(auto& row:album) {
        row.field="Batch / "+row.field;
        row.reason="discogs_batch_readonly_"+row.reason;
        if(!out.complete)row.state=FieldReviewState::Blocked;
        out.rows.push_back(std::move(row));
    }
    for(const auto& source:sources) {
        const auto prefix="Physical #"+std::to_string(source.source_index+1)+" / ";
        const auto it=verified.find(source.source_index);
        const bool matched=it!=verified.end();
        FieldReviewRow match;
        match.field=prefix+"MATCH";
        match.candidate.field="MATCH_DIAGNOSTIC";
        match.candidate.provider="discogs";
        match.candidate.scope=EvidenceScope::Edition;
        match.candidate.source_id=matched
            ? edition.tracks[it->second].source_track_id:edition.edition_id;
        const auto problem=source_problems.find(source.source_index);
        match.candidate.values={matched
            ? out.complete
                ? "Unique edition occurrence; read-only"
                : "Track occurrence matches, but full edition selection is incomplete"
            : problem!=source_problems.end()
                ? "Physical source blocker: "+problem->second
                : qualified
                    ? "No unique edition-track occurrence (title/mix/number/duration)"
                    : "Whole-batch qualification blocked; inspect other physical sources"};
        match.state=matched && out.complete
            ? FieldReviewState::Unchanged:FieldReviewState::Blocked;
        match.reason=matched && out.complete
            ? "discogs_batch_track_verified_readonly"
            : matched
                ? "discogs_batch_partial_edition_not_complete"
                : problem!=source_problems.end()
                    ? "discogs_batch_"+problem->second
                    : "discogs_batch_track_unverified";
        out.rows.push_back(std::move(match));
        for(const auto& original:source.originals.fields) {
            auto row=make_local_physical_original(
                original,"discogs_batch_original_preserved");
            row.field=prefix+"Original "+original.name;
            out.rows.push_back(std::move(row));
        }
        if(matched) {
            auto rows=review_online_fields(
                source.originals,edition.tracks[it->second].fields);
            for(auto& row:rows) {
                row.field=prefix+"Provider "+row.field;
                row.reason="discogs_batch_readonly_"+row.reason;
                if(!out.complete)row.state=FieldReviewState::Blocked;
                out.rows.push_back(std::move(row));
            }
        }
        if(out.rows.size()>20000)
            throw std::invalid_argument("Discogs batch detail bound exceeded");
    }
    return out;
}
} // namespace djmeta::online
