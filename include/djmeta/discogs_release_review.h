#pragma once
// Read-only Discogs -> physical metadata edition preview.
// Never maps individual edition tracks to a physical source implicitly.
#include "djmeta/discogs_provider.h"
#include "djmeta/online_fields.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace djmeta::online {

inline std::vector<FieldReviewRow> review_discogs_physical_edition(
    const MetadataDocument& originals, const ReleaseEdition& edition) {
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
        FieldReviewRow retained;
        retained.field="Album / "+original.name;
        retained.original_values=original.values;
        retained.candidate.field=original.name;
        retained.candidate.provider="discogs";
        retained.candidate.source_id=edition.edition_id;
        retained.candidate.scope=EvidenceScope::Edition;
        retained.state=FieldReviewState::Unchanged;
        retained.reason="discogs_local_only_no_provider_value";
        rows.push_back(std::move(retained));
    }
    // Complete edition tracklist is useful for choosing an edition; however
    // there is no verified per-file track assignment at this boundary.
    for(std::size_t i=0;i<edition.tracks.size();++i) {
        const auto& track=edition.tracks[i];
        const auto expected=edition.edition_id+"/track/"+std::to_string(i+1);
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
